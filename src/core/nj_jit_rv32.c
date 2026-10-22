/*
 * Bounded RV32 baseline blocks with guard faults.
 *
 * A generated block has the ABI
 *     nj_u4 block(nj_value *incoming_sp, nj_value *locals)
 * and uses only a0/a1 (the argument registers; a1 is untouched, a0 carries
 * the exit word), t0/t1/t2 and ft0/ft1.
 * It never changes the native stack, a callee-saved register, or a Java frame.
 *
 * It returns the number of bytecodes it finished. A guard that can fail at run
 * time (null receiver, array type/length, zero divisor) branches to a per-fault
 * exit that returns
 *     NJ_JIT_FAULT_BIT | (completed << 8) | fault_index
 * before touching any memory, so the C caller can rewind pc/sp and let the
 * interpreter re-execute exactly that instruction and raise the Java exception
 * with the interpreter's own PC. See nj_jit_internal.h for the contract.
 *
 * This is a portable encoder, including on a non-RISC-V build host. All target
 * words are emitted explicitly little-endian; no host executable pointer or
 * writable file-scope state is needed.
 */
#include "nj_jit_internal.h"

#if NJ_ENABLE_JIT

enum {
    RV_ZERO = 0, RV_RA = 1, RV_T0 = 5, RV_T1 = 6, RV_T2 = 7,
    RV_SP_ARG = 10, RV_LOCALS_ARG = 11,
    RV_FT0 = 0, RV_FT1 = 1,
    RV_SLOT_BYTES = 8,
    /* Header offsets of nj_object; field payloads follow the header. */
    RV_OBJ_DATA = 4, RV_OBJ_LENGTH = 8, RV_OBJ_KIND = 16, RV_OBJ_AFLAGS = 18
};

/* Byte budget kept free while instructions are emitted: a success return plus
 * one fault exit per allowed fault. The exact return path is shorter; this
 * only reserves capacity so a late rollback is never needed. */
#define RV_EXIT_RESERVE 12u

#if NJ_JIT_RV32_REG_CACHE
/* These describe actual register values, never deferred memory writes. Each
 * register remembers at most one 32-bit memory word and one exact constant. */
typedef struct rv_cached {
    unsigned base;
    int offset;
    int memory;
    int known;
    nj_u4 value;
} rv_cached;

#endif

typedef struct rv_fixup {
    nj_size at;       /* offset of the branch word inside the buffer */
    nj_u4 word;       /* branch with a zero displacement */
    nj_u1 fault;      /* fault exit index */
} rv_fixup;

typedef struct rv_writer {
    nj_u1 *buf;
    nj_size used;
    nj_size capacity;
    int failed;
    nj_u1 nfixups;
    rv_fixup fixups[NJ_JIT_MAX_FAULTS * 4];
#if NJ_JIT_RV32_REG_CACHE
    rv_cached regs[2];
#endif
} rv_writer;

typedef struct rv_bytecode {
    unsigned op;
    unsigned local;
    int immediate;
    int need;
    int change;
    nj_u4 length;
    const nj_field *field;   /* getfield/putfield only */
    int guarding;            /* instruction can fault at run time */
} rv_bytecode;

#if NJ_JIT_RV32_REG_CACHE
static rv_cached *rv_reg(rv_writer *w, unsigned reg)
{
    return reg == RV_T0 ? &w->regs[0] :
           (reg == RV_T1 ? &w->regs[1] : NULL);
}

static int rv_constant(rv_writer *w, unsigned reg, nj_u4 *value)
{
    rv_cached *c = rv_reg(w, reg);
    if (reg == RV_ZERO) {
        *value = 0u;
        return 1;
    }
    if (!c || !c->known) return 0;
    *value = c->value;
    return 1;
}

static void rv_result(rv_writer *w, unsigned reg, int known, nj_u4 value)
{
    rv_cached *c = rv_reg(w, reg);
    if (!c) return;
    c->memory = 0;
    c->known = known;
    c->value = value;
}

static int rv_at(rv_writer *w, unsigned reg, unsigned base, int offset)
{
    rv_cached *c = rv_reg(w, reg);
    return c && c->memory && c->base == base && c->offset == offset;
}

/* A dynamic base (an object pointer in t0) may alias either ABI base and is
 * redefined freely, so any cached slot mapping is dropped. */
static void rv_forget_memory(rv_writer *w)
{
    unsigned i;
    for (i = 0u; i < 2u; i++) w->regs[i].memory = 0;
}

static nj_u4 rv_sar(nj_u4 value, unsigned shift)
{
    shift &= 31u;
    if (shift == 0u) return value;
    return (value >> shift) |
           ((value & 0x80000000u) ? (0xffffffffu << (32u - shift)) : 0u);
}

static int rv_binary_constant(unsigned funct7, unsigned funct3,
                              nj_u4 a, nj_u4 b, nj_u4 *value)
{
    if (funct7 == 1u) {
        if (funct3 != 0u) return 0; /* Never fold DIV/REM. */
        *value = a * b;
    } else if (funct7 == 0x20u) {
        if (funct3 == 0u) *value = a - b;
        else if (funct3 == 5u) *value = rv_sar(a, b);
        else return 0;
    } else if (funct7 == 0u) {
        switch (funct3) {
        case 0u: *value = a + b; break;
        case 1u: *value = a << (b & 31u); break;
        case 4u: *value = a ^ b; break;
        case 5u: *value = a >> (b & 31u); break;
        case 6u: *value = a | b; break;
        case 7u: *value = a & b; break;
        default: return 0;
        }
    } else return 0;
    return 1;
}

static void rv_record_i(rv_writer *w, unsigned funct3,
                        unsigned rd, unsigned rs1, int immediate)
{
    nj_u4 value = 0u;
    int known = rv_constant(w, rs1, &value);
    if (known) {
        if (funct3 == 0u) value += (nj_u4)immediate;
        else if (funct3 == 1u) value <<= (unsigned)immediate & 31u;
        else if (funct3 == 5u) {
            if ((immediate & 0x400) != 0)
                value = rv_sar(value, (unsigned)immediate);
            else value >>= (unsigned)immediate & 31u;
        } else known = 0;
    }
    rv_result(w, rd, known, value);
}

static void rv_record_store(rv_writer *w, unsigned opcode, unsigned funct3,
                            unsigned rs, unsigned base, int offset)
{
    unsigned i;
    int width = opcode == 0x27u && funct3 == 3u ? 8 : 4;
    for (i = 0u; i < 2u; i++) {
        rv_cached *c = &w->regs[i];
        /* Distinct ABI bases may alias. Within one base, invalidate every
         * intersecting word, including both halves of a floating double. */
        if (c->memory && (c->base != base ||
            (c->offset < offset + width && offset < c->offset + 4)))
            c->memory = 0;
    }
    if (opcode == 0x23u) {
        rv_cached *c = rv_reg(w, rs);
        if (c) {
            c->base = base;
            c->offset = offset;
            c->memory = 1;
        }
    }
}
#endif

static int rv_imm12(int value)
{
    return value >= -2048 && value <= 2047;
}

static void rv_li(rv_writer *w, unsigned rd, nj_s4 value);

static void rv_u32(rv_writer *w, nj_u4 word)
{
    if (w->failed) return;
    if (w->used > w->capacity || w->capacity - w->used < 4u) {
        w->failed = 1;
        return;
    }
    w->buf[w->used++] = (nj_u1)word;
    w->buf[w->used++] = (nj_u1)(word >> 8);
    w->buf[w->used++] = (nj_u1)(word >> 16);
    w->buf[w->used++] = (nj_u1)(word >> 24);
}

#if NJ_JIT_RV32_C
static void rv_u16(rv_writer *w, nj_u2 word)
{
    if (w->failed) return;
    if (w->used > w->capacity || w->capacity - w->used < 2u) {
        w->failed = 1;
        return;
    }
    w->buf[w->used++] = (nj_u1)word;
    w->buf[w->used++] = (nj_u1)(word >> 8);
}
#endif

static void rv_r(rv_writer *w, unsigned opcode, unsigned funct7,
                 unsigned funct3, unsigned rd, unsigned rs1, unsigned rs2)
{
#if NJ_JIT_RV32_REG_CACHE
    nj_u4 a, b, value = 0u, previous;
    int known = opcode == 0x33u && rv_constant(w, rs1, &a) &&
                rv_constant(w, rs2, &b) &&
                rv_binary_constant(funct7, funct3, a, b, &value);
    if (known && (rv_imm12((nj_s4)value) ||
        (rv_constant(w, rd, &previous) && previous == value))) {
        /* At most one immediate instruction, or no instruction if unchanged.
         * Keep the following store even when the arithmetic folds away. */
        rv_li(w, rd, (nj_s4)value);
        return;
    }
#endif
    rv_u32(w, ((nj_u4)funct7 << 25) | ((nj_u4)rs2 << 20) |
           ((nj_u4)rs1 << 15) | ((nj_u4)funct3 << 12) |
           ((nj_u4)rd << 7) | (nj_u4)opcode);
#if NJ_JIT_RV32_REG_CACHE
    if (opcode == 0x33u) rv_result(w, rd, known, value);
#endif
}

static void rv_i(rv_writer *w, unsigned opcode, unsigned funct3,
                 unsigned rd, unsigned rs1, int immediate)
{
    if (!rv_imm12(immediate)) {
        w->failed = 1;
        return;
    }
    rv_u32(w, (((nj_u4)immediate & 0xfffu) << 20) |
           ((nj_u4)rs1 << 15) | ((nj_u4)funct3 << 12) |
           ((nj_u4)rd << 7) | (nj_u4)opcode);
#if NJ_JIT_RV32_REG_CACHE
    if (opcode == 0x13u) rv_record_i(w, funct3, rd, rs1, immediate);
    else if (opcode == 0x03u) rv_result(w, rd, 0, 0u);
#endif
}

static void rv_addi(rv_writer *w, unsigned rd, unsigned rs1, int immediate)
{
#if NJ_JIT_RV32_C
    if (rd != RV_ZERO && immediate >= -32 && immediate <= 31) {
        nj_u2 imm = (nj_u2)((nj_u4)immediate & 0x3fu);
        if (rs1 == RV_ZERO) { /* c.li rd, imm */
            rv_u16(w, (nj_u2)(0x4001u | (rd << 7) |
                              ((imm & 0x20u) << 7) | ((imm & 31u) << 2)));
#if NJ_JIT_RV32_REG_CACHE
            rv_record_i(w, 0u, rd, rs1, immediate);
#endif
            return;
        }
        if (rd == rs1 && immediate != 0) { /* c.addi rd, nzimm */
            rv_u16(w, (nj_u2)(0x0001u | (rd << 7) |
                              ((imm & 0x20u) << 7) | ((imm & 31u) << 2)));
#if NJ_JIT_RV32_REG_CACHE
            rv_record_i(w, 0u, rd, rs1, immediate);
#endif
            return;
        }
    }
#endif
    rv_i(w, 0x13u, 0u, rd, rs1, immediate);
}

static void rv_li(rv_writer *w, unsigned rd, nj_s4 value)
{
    int low;
    nj_u4 high;
#if NJ_JIT_RV32_REG_CACHE
    nj_u4 previous;
    if (rv_constant(w, rd, &previous) && previous == (nj_u4)value) return;
#endif
    if (rv_imm12(value)) {
        rv_addi(w, rd, RV_ZERO, value);
        return;
    }
    /* Unsigned addition also handles 0x7fffffff and 0xffffffff safely. */
    high = ((nj_u4)value + 0x800u) & 0xfffff000u;
    low = (int)((nj_u4)value & 0xfffu);
    if (low >= 2048) low -= 4096;
    rv_u32(w, high | ((nj_u4)rd << 7) | 0x37u); /* lui */
#if NJ_JIT_RV32_REG_CACHE
    rv_result(w, rd, 1, high);
#endif
    if (low != 0) rv_addi(w, rd, rd, low);
}

static void rv_load(rv_writer *w, unsigned rd, unsigned base, int offset)
{
#if NJ_JIT_RV32_REG_CACHE
    unsigned other = rd == RV_T0 ? RV_T1 : RV_T0;
    rv_cached *c;
    if (rv_at(w, rd, base, offset)) return;
    if (rv_at(w, other, base, offset))
        rv_addi(w, rd, other, 0); /* copy the already-loaded word */
    else
#endif
        rv_i(w, 0x03u, 2u, rd, base, offset); /* lw */
#if NJ_JIT_RV32_REG_CACHE
    c = rv_reg(w, rd);
    if (c) {
        c->base = base;
        c->offset = offset;
        c->memory = 1;
    }
#endif
}

static void rv_store_op(rv_writer *w, unsigned opcode, unsigned funct3,
                        unsigned rs, unsigned base, int offset)
{
    nj_u4 imm;
    if (!rv_imm12(offset)) {
        w->failed = 1;
        return;
    }
    imm = (nj_u4)offset & 0xfffu;
    rv_u32(w, ((imm >> 5) << 25) | ((nj_u4)rs << 20) |
           ((nj_u4)base << 15) | ((nj_u4)funct3 << 12) |
           ((imm & 31u) << 7) | (nj_u4)opcode);
#if NJ_JIT_RV32_REG_CACHE
    if (base == RV_SP_ARG || base == RV_LOCALS_ARG) {
        rv_record_store(w, opcode, funct3, rs, base, offset);
    } else {
        rv_forget_memory(w); /* unknown alias through a dynamic object base */
    }
#endif
}

static void rv_store(rv_writer *w, unsigned rs, unsigned base, int offset)
{
    rv_store_op(w, 0x23u, 2u, rs, base, offset); /* sw */
}

/* Zero-extended/sign-extended loads used for field and array payloads. The
 * base is a dynamic object pointer, never a cached ABI base. */
static void rv_load_mem(rv_writer *w, unsigned rd, unsigned funct3,
                        unsigned base, int offset)
{
    rv_i(w, 0x03u, funct3, rd, base, offset);
#if NJ_JIT_RV32_REG_CACHE
    rv_forget_memory(w);
#endif
}

static void rv_store_mem(rv_writer *w, unsigned funct3, unsigned rs,
                         unsigned base, int offset)
{
    rv_store_op(w, 0x23u, funct3, rs, base, offset);
}

static void rv_copy_slot(rv_writer *w, unsigned dst, int dst_offset,
                         unsigned src, int src_offset)
{
    rv_load(w, RV_T0, src, src_offset);
    rv_load(w, RV_T1, src, src_offset + 4);
    rv_store(w, RV_T0, dst, dst_offset);
    rv_store(w, RV_T1, dst, dst_offset + 4);
}

/* ---- branches and fault exits ---------------------------------------- */

static void rv_branch(rv_writer *w, unsigned funct3, unsigned rs1,
                      unsigned rs2, unsigned fault)
{
    nj_u4 word = ((nj_u4)rs2 << 20) | ((nj_u4)rs1 << 15) |
                 ((nj_u4)funct3 << 12) | 0x63u;
    if (w->failed) return;
    if (w->nfixups >= (nj_u1)(sizeof(w->fixups) / sizeof(w->fixups[0]))) {
        w->failed = 1;
        return;
    }
    if (w->used > w->capacity || w->capacity - w->used < 4u) {
        w->failed = 1;
        return;
    }
    w->fixups[w->nfixups].at = w->used;
    w->fixups[w->nfixups].word = word;
    w->fixups[w->nfixups].fault = (nj_u1)fault;
    w->nfixups++;
    rv_u32(w, word);
}

static void rv_guard_zero(rv_writer *w, unsigned rs, unsigned fault)
{
    rv_branch(w, 0u, rs, RV_ZERO, fault);          /* beq rs, zero */
}

static void rv_guard_nonzero(rv_writer *w, unsigned rs, unsigned fault)
{
    rv_branch(w, 1u, rs, RV_ZERO, fault);          /* bne rs, zero */
}

static void rv_guard_geu(rv_writer *w, unsigned rs1, unsigned rs2,
                         unsigned fault)
{
    rv_branch(w, 7u, rs1, rs2, fault);             /* bgeu rs1, rs2 */
}

static void rv_patch_branch(nj_u1 *buf, const rv_fixup *f, nj_size target)
{
    nj_u4 word = f->word;
    nj_u4 imm = (nj_u4)(target - f->at);
    word |= ((imm >> 12) & 1u) << 31;
    word |= ((imm >> 5) & 0x3fu) << 25;
    word |= ((imm >> 1) & 0xfu) << 8;
    word |= ((imm >> 11) & 1u) << 7;
    buf[f->at] = (nj_u1)word;
    buf[f->at + 1u] = (nj_u1)(word >> 8);
    buf[f->at + 2u] = (nj_u1)(word >> 16);
    buf[f->at + 3u] = (nj_u1)(word >> 24);
}

static nj_u4 rv_fault_word(unsigned index, nj_u4 completed)
{
    return NJ_JIT_FAULT_BIT | ((completed & 0xffu) << 8) |
           (index & NJ_JIT_FAULT_INDEX_MASK);
}

/* ---- decoding -------------------------------------------------------- */

static int rv_s8(unsigned value)
{
    return value < 128u ? (int)value : (int)value - 256;
}

static int rv_s16(unsigned value)
{
    return value < 32768u ? (int)value : (int)value - 65536;
}

static const nj_field *rv_field_operand(nj_vm *vm, const nj_method *m,
                                        nj_u4 index)
{
    const nj_field *f;
    nj_u4 last;
    if (!vm) return NULL;
    f = nj_jit_instance_field(vm, m->owner, index);
    if (!f) return NULL;
    /* A direct imm12 displacement must hold the whole payload. */
    if (f->ptype == NJ_PT_BOOLEAN || f->ptype == NJ_PT_BYTE) last = 1u;
    else if (f->ptype == NJ_PT_CHAR || f->ptype == NJ_PT_SHORT) last = 2u;
    else last = (nj_u4)(f->width == 2u ? 8u : 4u);
    if ((nj_u4)f->offset + last > 2047u) return NULL;
    return f;
}

static int rv_decode(nj_vm *vm, const nj_method *m, nj_u4 pc, rv_bytecode *b)
{
    const nj_u1 *code = m->code + pc;
    nj_u4 left = m->code_len - pc;
    unsigned op = code[0];
    int wide = 0;
    int slots;
    b->local = 0u;
    b->immediate = 0;
    b->need = 0;
    b->change = 0;
    b->length = 1u;
    b->field = NULL;
    b->guarding = 0;

    if (op == 0xc4u) { /* wide: no partial operand reads */
        if (left < 4u) return 0;
        op = code[1];
        if (!((op >= 0x15u && op <= 0x19u) ||
              (op >= 0x36u && op <= 0x3au) || op == 0x84u)) return 0;
        wide = 1;
        b->local = ((unsigned)code[2] << 8) | code[3];
        b->length = 4u;
    } else if ((op >= 0x15u && op <= 0x19u) ||
               (op >= 0x36u && op <= 0x3au) || op == 0x84u) {
        if (left < 2u) return 0;
        b->local = code[1];
        b->length = 2u;
    } else if (op >= 0x1au && op <= 0x2du) {
        b->local = (op - 0x1au) % 4u;
        op = 0x15u + (op - 0x1au) / 4u;
    } else if (op >= 0x3bu && op <= 0x4eu) {
        b->local = (op - 0x3bu) % 4u;
        op = 0x36u + (op - 0x3bu) / 4u;
    }
    b->op = op;

    if ((op >= 0x15u && op <= 0x19u) ||
        (op >= 0x36u && op <= 0x3au)) {
        unsigned type = op >= 0x36u ? op - 0x36u : op - 0x15u;
        slots = (type == 1u || type == 3u) ? 2 : 1;
        if (slots == 2 && !NJ_ENABLE_LONG) return 0;
        if ((type == 2u || type == 3u) && !NJ_ENABLE_FLOAT) return 0;
        if (b->local >= m->max_locals ||
            (unsigned)slots > (unsigned)m->max_locals - b->local) return 0;
        /* This first backend deliberately stops before large local offsets. */
        if (b->local * RV_SLOT_BYTES + (slots == 2 ? 8u : 0u) > 2047u)
            return 0;
        b->need = op >= 0x36u ? slots : 0;
        b->change = op >= 0x36u ? -slots : slots;
        return 1;
    }

    switch (op) {
    case 0x00: return 1; /* nop */
    case 0x01: /* aconst_null: RV32 references are 32 bits */
    case 0x02: case 0x03: case 0x04: case 0x05:
    case 0x06: case 0x07: case 0x08:
        b->immediate = op == 0x01u ? 0 : (int)op - 3;
        b->change = 1;
        return 1;
    case 0x09: case 0x0a:
        if (!NJ_ENABLE_LONG) return 0;
        b->immediate = (int)op - 9;
        b->change = 2;
        return 1;
    case 0x0b: case 0x0c: case 0x0d:
        if (!NJ_ENABLE_FLOAT) return 0;
        b->immediate = op == 0x0bu ? 0 :
                       (op == 0x0cu ? 0x3f800000 : 0x40000000);
        b->change = 1;
        return 1;
    case 0x0e: case 0x0f:
        if (!NJ_ENABLE_FLOAT || !NJ_ENABLE_LONG) return 0;
        b->immediate = op == 0x0eu ? 0 : 0x3ff00000;
        b->change = 2;
        return 1;
    case 0x10:
        if (left < 2u) return 0;
        b->length = 2u;
        b->immediate = rv_s8(code[1]);
        b->change = 1;
        return 1;
    case 0x11:
        if (left < 3u) return 0;
        b->length = 3u;
        b->immediate = rv_s16(((unsigned)code[1] << 8) | code[2]);
        b->change = 1;
        return 1;
    case 0x12: case 0x13: { /* numeric ldc/ldc_w, no resolution */
        nj_u4 bits;
        unsigned index;
        b->length = op == 0x12u ? 2u : 3u;
        if (left < b->length) return 0;
        index = op == 0x12u ? code[1] :
                ((unsigned)code[1] << 8) | code[2];
        if (!nj_jit_numeric_constant(m, index, &bits)) return 0;
        b->immediate = (nj_s4)bits;
        b->change = 1;
        return 1;
    }
    case 0x2e: /* iaload */
        if (!vm) return 0; /* array operands cannot be checked without a VM */
        b->need = 2;
        b->change = -1;
        b->guarding = 1;
        return 1;
    case 0x4f: /* iastore */
        if (!vm) return 0;
        b->need = 3;
        b->change = -3;
        b->guarding = 1;
        return 1;
    case 0x57: case 0x58: /* pop/pop2 */
        b->need = op == 0x57u ? 1 : 2;
        b->change = -b->need;
        return 1;
    case 0x59: case 0x5c: /* dup/dup2 */
        b->need = op == 0x59u ? 1 : 2;
        b->change = b->need;
        return 1;
    case 0x5f: /* swap */
        b->need = 2;
        return 1;
    case 0x60: case 0x64: /* iadd/isub */
    case 0x78: case 0x7a: case 0x7c: /* ishl/ishr/iushr */
    case 0x7e: case 0x80: case 0x82: /* iand/ior/ixor */
        b->need = 2;
        b->change = -1;
        return 1;
#if NJ_JIT_RV32_M
    case 0x68: case 0x6c: case 0x70: /* imul/idiv/irem */
        b->need = 2;
        b->change = -1;
        if (op != 0x68u) b->guarding = 1; /* zero divisor faults */
        return 1;
#endif
    case 0x74: case 0x91: case 0x92: case 0x93: /* ineg/i2b/i2c/i2s */
        b->need = 1;
        return 1;
    case 0x84: /* iinc, including signed 16-bit wide immediates */
        if (b->local >= m->max_locals ||
            b->local * RV_SLOT_BYTES > 2047u) return 0;
        b->length = wide ? 6u : 3u;
        if (left < b->length) return 0;
        b->immediate = wide ? rv_s16(((unsigned)code[4] << 8) | code[5]) :
                              rv_s8(code[2]);
        return 1;
    case 0xb4: /* getfield */
    case 0xb5: { /* putfield */
        unsigned index;
        int is_put = op == 0xb5u;
        if (left < 3u) return 0;
        b->length = 3u;
        index = ((unsigned)code[1] << 8) | code[2];
        b->field = rv_field_operand(vm, m, index);
        if (!b->field) return 0;
        if (is_put) {
            b->need = 1 + (int)b->field->width;
            b->change = -b->need;
        } else {
            b->need = 1;
            b->change = b->field->width == 2u ? 1 : 0;
        }
        b->guarding = 1; /* null receiver */
        return 1;
    }
    case 0xbe: /* arraylength */
        if (!vm) return 0;
        b->need = 1;
        b->guarding = 1;
        return 1;
#if NJ_ENABLE_FLOAT && NJ_JIT_RV32_F
    case 0x62: case 0x66: case 0x6a: case 0x6e: /* float binary */
        b->need = 2;
        b->change = -1;
        return 1;
    case 0x76: /* fneg */
        b->need = 1;
        return 1;
#if NJ_ENABLE_LONG && NJ_JIT_RV32_D
    case 0x63: case 0x67: case 0x6b: case 0x6f: /* double binary */
        b->need = 4;
        b->change = -2;
        return 1;
    case 0x77: /* dneg */
        b->need = 2;
        return 1;
#endif
#endif
    default:
        return 0;
    }
}

/* ---- emission -------------------------------------------------------- */

/* Object/array base pointer kept in t0 while a payload is accessed. */
static void rv_load_base(rv_writer *w, unsigned slot)
{
    rv_load(w, RV_T0, RV_SP_ARG, slot);
#if NJ_JIT_RV32_REG_CACHE
    rv_forget_memory(w); /* t0 is about to be reused as a dynamic base */
#endif
}

static void rv_field_load(rv_writer *w, const nj_field *f, int top,
                          unsigned fault)
{
    unsigned base = RV_T0;
    rv_load_base(w, top - RV_SLOT_BYTES);
    rv_guard_zero(w, base, fault);
    switch (f->ptype) {
    case NJ_PT_BOOLEAN:
    case NJ_PT_BYTE:
        rv_load_mem(w, RV_T1, 0u, base, (int)f->offset); /* lb */
        break;
    case NJ_PT_CHAR:
        rv_load_mem(w, RV_T1, 5u, base, (int)f->offset); /* lhu */
        break;
    case NJ_PT_SHORT:
        rv_load_mem(w, RV_T1, 1u, base, (int)f->offset); /* lh */
        break;
    default:
        rv_load_mem(w, RV_T1, 2u, base, (int)f->offset); /* lw */
        break;
    }
    if (f->width == 2u) {
        /* nj_value.j fills the first slot; the second slot only zeroes .bits. */
        rv_store(w, RV_T1, RV_SP_ARG, top - RV_SLOT_BYTES);
        rv_load_mem(w, RV_T1, 2u, base, (int)f->offset + 4);
        rv_store(w, RV_T1, RV_SP_ARG, top - 4);
        rv_store(w, RV_ZERO, RV_SP_ARG, top);
    } else {
        rv_store(w, RV_T1, RV_SP_ARG, top - RV_SLOT_BYTES);
    }
}

static void rv_emit(rv_writer *w, const rv_bytecode *b, int delta,
                    unsigned fault)
{
    unsigned op = b->op;
    int top = delta * RV_SLOT_BYTES;
    int local = (int)b->local * RV_SLOT_BYTES;
    unsigned funct3 = 0u, funct7 = 0u;
    unsigned push = RV_T0, lhs = RV_T0, rhs = RV_T1;
#if NJ_JIT_RV32_REG_CACHE
    /* Preserve the preceding operand instead of always overwriting t0. */
    if (rv_at(w, RV_T0, RV_SP_ARG, top - RV_SLOT_BYTES)) push = RV_T1;
#endif
    if (op >= 0x15u && op <= 0x19u) {
        if (b->change == 2) {
            rv_copy_slot(w, RV_SP_ARG, top, RV_LOCALS_ARG, local);
            rv_store(w, RV_ZERO, RV_SP_ARG, top + RV_SLOT_BYTES);
        } else {
            rv_load(w, push, RV_LOCALS_ARG, local);
            rv_store(w, push, RV_SP_ARG, top);
        }
        return;
    }
    if (op >= 0x36u && op <= 0x3au) {
        if (b->need == 2) {
            rv_copy_slot(w, RV_LOCALS_ARG, local, RV_SP_ARG, top - 16);
            rv_store(w, RV_ZERO, RV_LOCALS_ARG, local + RV_SLOT_BYTES);
        } else {
            /* nj_value.bits is nj_u4, so istore/fstore copy only 32 bits,
             * just like reference stores on RV32. Leave the upper half alone. */
            rv_load(w, RV_T0, RV_SP_ARG, top - RV_SLOT_BYTES);
            rv_store(w, RV_T0, RV_LOCALS_ARG, local);
        }
        return;
    }
    switch (op) {
    case 0x00: case 0x57: case 0x58:
        return; /* Only the C caller updates the Java sp/pc. */
    case 0x01: case 0x02: case 0x03: case 0x04: case 0x05:
    case 0x06: case 0x07: case 0x08: case 0x0b: case 0x0c: case 0x0d:
    case 0x10: case 0x11: case 0x12: case 0x13:
        rv_li(w, push, (nj_s4)b->immediate);
        rv_store(w, push, RV_SP_ARG, top);
        return;
    case 0x09: case 0x0a:
        rv_li(w, RV_T0, (nj_s4)b->immediate);
        rv_store(w, RV_T0, RV_SP_ARG, top);
        rv_store(w, RV_ZERO, RV_SP_ARG, top + 4);
        rv_store(w, RV_ZERO, RV_SP_ARG, top + RV_SLOT_BYTES);
        return;
    case 0x0e: case 0x0f:
        rv_li(w, RV_T0, (nj_s4)b->immediate);
        rv_store(w, RV_ZERO, RV_SP_ARG, top);
        rv_store(w, RV_T0, RV_SP_ARG, top + 4);
        rv_store(w, RV_ZERO, RV_SP_ARG, top + RV_SLOT_BYTES);
        return;
    case 0x59:
        rv_copy_slot(w, RV_SP_ARG, top, RV_SP_ARG, top - RV_SLOT_BYTES);
        return;
    case 0x5c:
        rv_copy_slot(w, RV_SP_ARG, top, RV_SP_ARG, top - 16);
        rv_copy_slot(w, RV_SP_ARG, top + RV_SLOT_BYTES, RV_SP_ARG, top - 8);
        return;
    case 0x5f:
        /* Swap both halves independently. */
        rv_load(w, RV_T0, RV_SP_ARG, top - 8);
        rv_load(w, RV_T1, RV_SP_ARG, top - 16);
        rv_store(w, RV_T0, RV_SP_ARG, top - 16);
        rv_store(w, RV_T1, RV_SP_ARG, top - 8);
        rv_load(w, RV_T0, RV_SP_ARG, top - 4);
        rv_load(w, RV_T1, RV_SP_ARG, top - 12);
        rv_store(w, RV_T0, RV_SP_ARG, top - 12);
        rv_store(w, RV_T1, RV_SP_ARG, top - 4);
        return;
    case 0x84:
        rv_load(w, RV_T0, RV_LOCALS_ARG, local);
        if (rv_imm12(b->immediate)) {
            rv_addi(w, RV_T0, RV_T0, b->immediate);
        } else {
            rv_li(w, RV_T1, (nj_s4)b->immediate);
            rv_r(w, 0x33u, 0u, 0u, RV_T0, RV_T0, RV_T1);
        }
        rv_store(w, RV_T0, RV_LOCALS_ARG, local);
        return;
    case 0x74:
        rv_load(w, RV_T0, RV_SP_ARG, top - 8);
        rv_r(w, 0x33u, 0x20u, 0u, RV_T0, RV_ZERO, RV_T0); /* sub */
        rv_store(w, RV_T0, RV_SP_ARG, top - 8);
        return;
    case 0x91: case 0x92: case 0x93: {
        int shift = op == 0x91u ? 24 : 16;
        rv_load(w, RV_T0, RV_SP_ARG, top - 8);
        rv_i(w, 0x13u, 1u, RV_T0, RV_T0, shift); /* slli */
        rv_i(w, 0x13u, 5u, RV_T0, RV_T0,
             shift | (op == 0x92u ? 0 : 0x400)); /* srli/srai */
        rv_store(w, RV_T0, RV_SP_ARG, top - 8);
        return;
    }
#if NJ_JIT_RV32_M
    case 0x6c: case 0x70: {
        /* The interpreter raises ArithmeticException from the Java PC when the
         * divisor is zero, so guard first and let it re-execute the bytecode. */
        rv_load(w, RV_T1, RV_SP_ARG, top - 8);
        rv_guard_zero(w, RV_T1, fault);
        rv_load(w, RV_T0, RV_SP_ARG, top - 16);
        rv_r(w, 0x33u, 1u, op == 0x6cu ? 4u : 6u,
             RV_T0, RV_T0, RV_T1);
        rv_store(w, RV_T0, RV_SP_ARG, top - 16);
        return;
    }
#endif
    case 0x2e: { /* iaload: index, array -> value */
        unsigned idx = RV_T1;
        unsigned len = RV_T2;
        rv_load_base(w, top - 16);
        rv_guard_zero(w, RV_T0, fault);
        rv_load(w, idx, RV_SP_ARG, top - RV_SLOT_BYTES);
        rv_load_mem(w, len, 0u, RV_T0, RV_OBJ_KIND);       /* lbu kind */
        rv_addi(w, len, len, -1);
        rv_guard_nonzero(w, len, fault);
        rv_load_mem(w, len, 2u, RV_T0, RV_OBJ_LENGTH);     /* lw length */
        rv_guard_geu(w, idx, len, fault);
        rv_load_mem(w, len, 5u, RV_T0, RV_OBJ_AFLAGS);     /* lhu aflags */
        rv_addi(w, len, len, -(int)NJ_ARRAY_T_INT);
        rv_guard_nonzero(w, len, fault);
        rv_load_mem(w, len, 2u, RV_T0, RV_OBJ_DATA);       /* lw data */
        rv_i(w, 0x13u, 1u, idx, idx, 2);                   /* slli idx, 2 */
        rv_r(w, 0x33u, 0u, 0u, len, len, idx);             /* add */
        rv_load_mem(w, idx, 2u, len, 0);                   /* lw value */
        rv_store(w, idx, RV_SP_ARG, top - 16);
        return;
    }
    case 0x4f: { /* iastore: array, index, value (value on top) */
        unsigned idx = RV_T1;
        unsigned len = RV_T2;
        unsigned val = RV_T1;
        rv_load_base(w, top - 24);
        rv_guard_zero(w, RV_T0, fault);
        rv_load(w, idx, RV_SP_ARG, top - 16);
        rv_load_mem(w, len, 0u, RV_T0, RV_OBJ_KIND);
        rv_addi(w, len, len, -1);
        rv_guard_nonzero(w, len, fault);
        rv_load_mem(w, len, 2u, RV_T0, RV_OBJ_LENGTH);
        rv_guard_geu(w, idx, len, fault);
        rv_load_mem(w, len, 5u, RV_T0, RV_OBJ_AFLAGS);
        rv_addi(w, len, len, -(int)NJ_ARRAY_T_INT);
        rv_guard_nonzero(w, len, fault);
        rv_load_mem(w, len, 2u, RV_T0, RV_OBJ_DATA);
        rv_i(w, 0x13u, 1u, idx, idx, 2);                   /* slli idx, 2 */
        rv_r(w, 0x33u, 0u, 0u, len, len, idx);             /* add */
        rv_load(w, val, RV_SP_ARG, top - RV_SLOT_BYTES);
        rv_store_mem(w, 2u, val, len, 0);                  /* sw value */
        return;
    }
    case 0xbe: { /* arraylength */
        rv_load_base(w, top - RV_SLOT_BYTES);
        rv_guard_zero(w, RV_T0, fault);
        rv_load_mem(w, RV_T1, 2u, RV_T0, RV_OBJ_LENGTH);
        rv_store(w, RV_T1, RV_SP_ARG, top - RV_SLOT_BYTES);
        return;
    }
    case 0xb4: { /* getfield: receiver -> value */
        const nj_field *f = b->field;
        if (!f) {
            w->failed = 1;
            return;
        }
        rv_field_load(w, f, top, fault);
        return;
    }
    case 0xb5: { /* putfield: value, receiver */
        const nj_field *f = b->field;
        int value_slot;
        int receiver_slot;
        if (!f) {
            w->failed = 1;
            return;
        }
        value_slot = top - (int)f->width * RV_SLOT_BYTES;
        receiver_slot = value_slot - RV_SLOT_BYTES;
        rv_load_base(w, receiver_slot);
        rv_guard_zero(w, RV_T0, fault);
        if (f->width == 2u) {
            rv_load(w, RV_T1, RV_SP_ARG, value_slot);
            rv_store_mem(w, 2u, RV_T1, RV_T0, (int)f->offset);
            rv_load(w, RV_T1, RV_SP_ARG, value_slot + 4);
            rv_store_mem(w, 2u, RV_T1, RV_T0, (int)f->offset + 4);
        } else {
            unsigned funct3 = 2u; /* sw */
            rv_load(w, RV_T1, RV_SP_ARG, value_slot);
            if (f->ptype == NJ_PT_BOOLEAN || f->ptype == NJ_PT_BYTE) funct3 = 0u;
            else if (f->ptype == NJ_PT_CHAR || f->ptype == NJ_PT_SHORT) funct3 = 1u;
            rv_store_mem(w, funct3, RV_T1, RV_T0, (int)f->offset);
        }
        return;
    }
#if NJ_ENABLE_FLOAT && NJ_JIT_RV32_F
    case 0x62: case 0x66: case 0x6a: case 0x6e: case 0x76:
#if NJ_ENABLE_LONG && NJ_JIT_RV32_D
    case 0x63: case 0x67: case 0x6b: case 0x6f: case 0x77:
#endif
    {
        int is_double = (int)(op & 1u);
        int width = is_double ? 16 : 8;
        int is_neg = op == 0x76u || op == 0x77u;
        int dst = top - (is_neg ? width : width * 2);
        unsigned format = is_double ? 3u : 2u;
        rv_i(w, 0x07u, format, RV_FT0, RV_SP_ARG, dst); /* flw/fld */
        if (is_neg) {
            rv_r(w, 0x53u, 0x10u + (unsigned)is_double, 1u,
                 RV_FT0, RV_FT0, RV_FT0); /* fsgnjn.s/d preserves NaN bits */
        } else {
            rv_i(w, 0x07u, format, RV_FT1, RV_SP_ARG, top - width);
            /* add/sub/mul/div funct7 = 0/4/8/12 + format. rm=000 (RNE)
             * explicitly ignores the embedding application's dynamic frm. */
            funct7 = ((op - 0x62u) & ~1u) + (unsigned)is_double;
            rv_r(w, 0x53u, funct7, 0u, RV_FT0, RV_FT0, RV_FT1);
        }
        rv_store_op(w, 0x27u, format, RV_FT0, RV_SP_ARG, dst);
        /* Interpreter double arithmetic zeros only the padding's .bits;
         * dneg leaves its padding alone. The upper 32 bits are not a value. */
        if (is_double && !is_neg)
            rv_store(w, RV_ZERO, RV_SP_ARG, dst + RV_SLOT_BYTES);
        return;
    }
#endif
    case 0x60: break;
    case 0x64: funct7 = 0x20u; break;
#if NJ_JIT_RV32_M
    case 0x68: funct7 = 1u; break;
#endif
    case 0x78: funct3 = 1u; break;
    case 0x7a: funct7 = 0x20u; funct3 = 5u; break;
    case 0x7c: funct3 = 5u; break;
    case 0x7e: funct3 = 7u; break;
    case 0x80: funct3 = 6u; break;
    case 0x82: funct3 = 4u; break;
    default:
        w->failed = 1;
        return;
    }
#if NJ_JIT_RV32_REG_CACHE
    if (rv_at(w, RV_T0, RV_SP_ARG, top - 8)) {
        /* Do not destroy a cached right operand while loading the left one. */
        lhs = RV_T1;
        rhs = RV_T0;
    }
#endif
    rv_load(w, lhs, RV_SP_ARG, top - 16);
    rv_load(w, rhs, RV_SP_ARG, top - 8);
    /* RV32 shifts mask rs2 to 5 bits. DIV/REM also implement Java's
     * INT_MIN / -1 overflow result; a zero divisor faults before execution. */
    rv_r(w, 0x33u, funct7, funct3, RV_T0, lhs, rhs);
    rv_store(w, RV_T0, RV_SP_ARG, top - 16);
}

/* ---- block assembly -------------------------------------------------- */

/* Decide whether one more instruction fits the block's fault budget. */
static int rv_accept(const nj_jit_block *block, const rv_bytecode *b)
{
    if (b->guarding && block->nfaults >= NJ_JIT_MAX_FAULTS) return 0;
    /* A throwing integer division may not follow mutations in a block: if it
     * faults, the interpreter must observe the untouched incoming stack. */
    if ((b->op == 0x6cu || b->op == 0x70u) && block->insns != 0u) return 0;
    return 1;
}

static int rv_terminates(const rv_bytecode *b)
{
    return b->op == 0x6cu || b->op == 0x70u;
}

static int rv_analyze_impl(nj_vm *vm, const nj_method *m, nj_u4 pc,
                           nj_jit_block *out)
{
    nj_jit_block block;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!m || !m->code || pc >= m->code_len ||
        sizeof(nj_value) != RV_SLOT_BYTES || m->code_len - pc > 0xffffu)
        return 0;
    memset(&block, 0, sizeof(block));
    block.next_pc = pc;
    while (block.next_pc < m->code_len &&
           block.insns < NJ_JIT_MAX_BLOCK_INSNS) {
        rv_bytecode b;
        int delta, required, growth;
        if (!rv_decode(vm, m, block.next_pc, &b)) break;
        if (!rv_accept(&block, &b)) break;
        delta = block.stack_delta + b.change;
        required = b.need - block.stack_delta;
        if (required < block.min_stack) required = block.min_stack;
        growth = delta > block.max_growth ? delta : block.max_growth;
        if (required + growth > (int)m->max_stack) break;
        if (b.guarding &&
            !nj_jit_record_fault(&block, pc, block.next_pc, block.stack_delta))
            break;
        block.next_pc += b.length;
        block.insns++;
        block.stack_delta = delta;
        block.min_stack = required;
        block.max_growth = growth;
        if (rv_terminates(&b)) break;
    }
    if (block.insns == 0u) return 0;
    *out = block;
    return 1;
}

int nj_rv32_analyze(nj_vm *vm, const nj_method *m, nj_u4 pc, nj_jit_block *out)
{
    return rv_analyze_impl(vm, m, pc, out);
}

int nj_rv32_compile(nj_vm *vm, const nj_method *m, nj_u4 pc, nj_u1 *buf,
                    nj_size capacity, nj_jit_block *out)
{
    rv_writer w;
    nj_jit_block block;
    nj_u1 completed[NJ_JIT_MAX_FAULTS];
    nj_size stub_at[NJ_JIT_MAX_FAULTS];
    nj_u1 i;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!m || !m->code || !buf || pc >= m->code_len ||
        sizeof(nj_value) != RV_SLOT_BYTES || m->code_len - pc > 0xffffu)
        return 0;
    if (capacity > NJ_JIT_MAX_CODE_BYTES) capacity = NJ_JIT_MAX_CODE_BYTES;
    if (capacity < RV_EXIT_RESERVE + 4u) return 0;
    memset(&block, 0, sizeof(block));
    block.next_pc = pc;
    memset(&w, 0, sizeof(w));
    memset(completed, 0, sizeof(completed));
    w.buf = buf;
    w.used = 0u;
    w.capacity = capacity;
    w.failed = 0;
    w.nfixups = 0u;

    while (block.next_pc < m->code_len &&
           block.insns < NJ_JIT_MAX_BLOCK_INSNS) {
        rv_bytecode b;
        rv_writer checkpoint = w;
        int delta, required, growth;
        unsigned fault = block.nfaults;
        if (!rv_decode(vm, m, block.next_pc, &b)) break;
        if (!rv_accept(&block, &b)) break;
        delta = block.stack_delta + b.change;
        required = b.need - block.stack_delta;
        if (required < block.min_stack) required = block.min_stack;
        growth = delta > block.max_growth ? delta : block.max_growth;
        if (required + growth > (int)m->max_stack) break;
        {
            /* Every guarded instruction owns one exit stub after the success
             * exit. Keep those bytes out of this instruction's budget so a
             * fault can never overflow the buffer during final emission. */
            nj_size exits = RV_EXIT_RESERVE *
                (nj_size)(block.nfaults + (b.guarding ? 1u : 0u) + 1u);
            if (capacity < exits || w.used > capacity - exits) break;
            w.capacity = capacity - exits;
        }
        rv_emit(&w, &b, block.stack_delta, fault);
        if (w.failed) {
            /* Discard only this instruction. The reserved return overwrites
             * its first bytes; no partial instruction enters the live block.
             * Restore register knowledge, output position and fixups. */
            w = checkpoint;
            break;
        }
        if (b.guarding) {
            completed[fault] = (nj_u1)block.insns;
            (void)nj_jit_record_fault(&block, pc, block.next_pc,
                                      block.stack_delta);
            block.nfaults = (nj_u1)(fault + 1u);
        }
        block.next_pc += b.length;
        block.insns++;
        block.stack_delta = delta;
        block.min_stack = required;
        block.max_growth = growth;
        if (rv_terminates(&b)) break;
    }
    if (block.insns == 0u) return 0;

    /* Success exit: a0 = number of finished bytecodes, then return to C. */
    w.capacity = capacity;
    rv_li(&w, RV_SP_ARG, (nj_s4)block.insns);
#if NJ_JIT_RV32_C
    rv_u16(&w, 0x8082u); /* c.jr ra */
#else
    rv_i(&w, 0x67u, 0u, RV_ZERO, RV_RA, 0); /* jalr zero, ra, 0 */
#endif
    if (w.failed) return 0;

    /* One fault exit per guarded instruction. Each is a forward branch target
     * reached before the instruction's first side effect. */
    for (i = 0u; i < block.nfaults; i++) {
        stub_at[i] = w.used;
        rv_li(&w, RV_SP_ARG, (nj_s4)rv_fault_word(i, (nj_u4)completed[i]));
#if NJ_JIT_RV32_C
        rv_u16(&w, 0x8082u);
#else
        rv_i(&w, 0x67u, 0u, RV_ZERO, RV_RA, 0);
#endif
    }
    if (w.failed) return 0;

    for (i = 0u; i < w.nfixups; i++) {
        unsigned f = w.fixups[i].fault;
        if (f >= block.nfaults) {
            w.failed = 1;
            break;
        }
        rv_patch_branch(buf, &w.fixups[i], stub_at[f]);
    }
    if (w.failed) return 0;

    block.size = w.used;
    *out = block;
    return 1;
}
#endif /* NJ_ENABLE_JIT */
