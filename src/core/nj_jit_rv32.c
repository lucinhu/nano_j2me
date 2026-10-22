/*
 * Bounded RV32 baseline blocks. The generated leaf has the ABI
 *     void block(nj_value *incoming_sp, nj_value *locals)
 * and uses only a0/a1, t0/t1 and ft0/ft1. It does not change either
 * argument, the native stack, or any callee-saved register. Java control flow,
 * exceptions, allocation, resolution and calls stay in the interpreter.
 *
 * This is a portable encoder, including on a non-RISC-V build host. All target
 * words are emitted explicitly little-endian; no host executable pointer or
 * writable file-scope state is needed.
 */
#include "nj_jit_internal.h"

#if NJ_ENABLE_JIT

enum {
    RV_ZERO = 0, RV_RA = 1, RV_T0 = 5, RV_T1 = 6,
    RV_SP_ARG = 10, RV_LOCALS_ARG = 11,
    RV_FT0 = 0, RV_FT1 = 1,
    RV_SLOT_BYTES = 8
};

typedef struct rv_writer {
    nj_u1 *buf;
    nj_size used;
    nj_size capacity;
    int failed;
} rv_writer;

typedef struct rv_bytecode {
    unsigned op;
    unsigned local;
    int immediate;
    int need;
    int change;
    nj_u4 length;
} rv_bytecode;

static void rv_u32(rv_writer *w, nj_u4 word)
{
    if (w->failed) return;
    if (w->capacity - w->used < 4u) {
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
    if (w->capacity - w->used < 2u) {
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
    rv_u32(w, ((nj_u4)funct7 << 25) | ((nj_u4)rs2 << 20) |
           ((nj_u4)rs1 << 15) | ((nj_u4)funct3 << 12) |
           ((nj_u4)rd << 7) | (nj_u4)opcode);
}

static int rv_imm12(int value)
{
    return value >= -2048 && value <= 2047;
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
}

static void rv_addi(rv_writer *w, unsigned rd, unsigned rs1, int immediate)
{
#if NJ_JIT_RV32_C
    if (rd != RV_ZERO && immediate >= -32 && immediate <= 31) {
        nj_u2 imm = (nj_u2)((nj_u4)immediate & 0x3fu);
        if (rs1 == RV_ZERO) { /* c.li rd, imm */
            rv_u16(w, (nj_u2)(0x4001u | (rd << 7) |
                              ((imm & 0x20u) << 7) | ((imm & 31u) << 2)));
            return;
        }
        if (rd == rs1 && immediate != 0) { /* c.addi rd, nzimm */
            rv_u16(w, (nj_u2)(0x0001u | (rd << 7) |
                              ((imm & 0x20u) << 7) | ((imm & 31u) << 2)));
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
    if (rv_imm12(value)) {
        rv_addi(w, rd, RV_ZERO, value);
        return;
    }
    /* Unsigned addition also handles 0x7fffffff and 0xffffffff safely. */
    high = ((nj_u4)value + 0x800u) & 0xfffff000u;
    low = (int)((nj_u4)value & 0xfffu);
    if (low >= 2048) low -= 4096;
    rv_u32(w, high | ((nj_u4)rd << 7) | 0x37u); /* lui */
    if (low != 0) rv_addi(w, rd, rd, low);
}

static void rv_load(rv_writer *w, unsigned rd, unsigned base, int offset)
{
    rv_i(w, 0x03u, 2u, rd, base, offset); /* lw */
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
}

static void rv_store(rv_writer *w, unsigned rs, unsigned base, int offset)
{
    rv_store_op(w, 0x23u, 2u, rs, base, offset); /* sw */
}

static void rv_copy_slot(rv_writer *w, unsigned dst, int dst_offset,
                         unsigned src, int src_offset)
{
    rv_load(w, RV_T0, src, src_offset);
    rv_load(w, RV_T1, src, src_offset + 4);
    rv_store(w, RV_T0, dst, dst_offset);
    rv_store(w, RV_T1, dst, dst_offset + 4);
}

static int rv_s8(unsigned value)
{
    return value < 128u ? (int)value : (int)value - 256;
}

static int rv_s16(unsigned value)
{
    return value < 32768u ? (int)value : (int)value - 65536;
}

static int rv_decode(const nj_method *m, nj_u4 pc, rv_bytecode *b)
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

static void rv_emit(rv_writer *w, const rv_bytecode *b, int delta)
{
    unsigned op = b->op;
    int top = delta * RV_SLOT_BYTES;
    int local = (int)b->local * RV_SLOT_BYTES;
    unsigned funct3 = 0u, funct7 = 0u;
    if (op >= 0x15u && op <= 0x19u) {
        if (b->change == 2) {
            rv_copy_slot(w, RV_SP_ARG, top, RV_LOCALS_ARG, local);
            rv_store(w, RV_ZERO, RV_SP_ARG, top + RV_SLOT_BYTES);
        } else {
            rv_load(w, RV_T0, RV_LOCALS_ARG, local);
            rv_store(w, RV_T0, RV_SP_ARG, top);
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
    case 0x10: case 0x11:
        rv_li(w, RV_T0, (nj_s4)b->immediate);
        rv_store(w, RV_T0, RV_SP_ARG, top);
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
    case 0x6c: funct7 = 1u; funct3 = 4u; break;
    case 0x70: funct7 = 1u; funct3 = 6u; break;
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
    rv_load(w, RV_T0, RV_SP_ARG, top - 16);
    rv_load(w, RV_T1, RV_SP_ARG, top - 8);
    /* RV32 shifts mask rs2 to 5 bits. DIV/REM also implement Java's
     * INT_MIN / -1 overflow result; C rejects zero divisors before entry. */
    rv_r(w, 0x33u, funct7, funct3, RV_T0, RV_T0, RV_T1);
    rv_store(w, RV_T0, RV_SP_ARG, top - 16);
}

int nj_rv32_compile(const nj_method *m, nj_u4 pc, nj_u1 *buf,
                    nj_size capacity, nj_jit_block *out)
{
    rv_writer w;
    nj_jit_block block;
#if NJ_JIT_RV32_C
    const nj_size return_size = 2u;
#else
    const nj_size return_size = 4u;
#endif
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!m || !m->code || !buf || pc >= m->code_len ||
        sizeof(nj_value) != RV_SLOT_BYTES) return 0;
    if (capacity > NJ_JIT_MAX_CODE_BYTES) capacity = NJ_JIT_MAX_CODE_BYTES;
    if (capacity < return_size) return 0;
    memset(&block, 0, sizeof(block));
    block.next_pc = pc;
    w.buf = buf;
    w.used = 0u;
    w.capacity = capacity - return_size; /* reserve a return before any byte */
    w.failed = 0;

    while (block.next_pc < m->code_len && block.insns < NJ_JIT_MAX_BLOCK_INSNS) {
        rv_bytecode b;
        nj_size checkpoint = w.used;
        int delta, required, growth;
        if (!rv_decode(m, block.next_pc, &b)) break;
        /* A throwing integer division may never follow mutations in a block,
         * and its divisor must still be incoming_sp[-1] when C checks it. */
        if ((b.op == 0x6cu || b.op == 0x70u) && block.insns != 0u) break;
        delta = block.stack_delta + b.change;
        required = b.need - block.stack_delta;
        if (required < block.min_stack) required = block.min_stack;
        growth = delta > block.max_growth ? delta : block.max_growth;
        if (required + growth > (int)m->max_stack) break;
        rv_emit(&w, &b, block.stack_delta);
        if (w.failed) {
            /* Discard only this instruction. The reserved return overwrites
             * its first bytes; no partial instruction enters the live block. */
            w.used = checkpoint;
            w.failed = 0;
            break;
        }
        block.next_pc += b.length;
        block.insns++;
        block.stack_delta = delta;
        block.min_stack = required;
        block.max_growth = growth;
        if (b.op == 0x6cu || b.op == 0x70u) {
            block.check_divisor = 1;
            break;
        }
    }
    if (block.insns == 0u) return 0;
    w.capacity = capacity;
#if NJ_JIT_RV32_C
    rv_u16(&w, 0x8082u); /* c.jr ra */
#else
    rv_i(&w, 0x67u, 0u, RV_ZERO, RV_RA, 0); /* jalr zero, ra, 0 */
#endif
    if (w.failed) return 0;
    block.size = w.used;
    *out = block;
    return 1;
}
#endif /* NJ_ENABLE_JIT */
