/*
 * Bounded little-endian AArch64 baseline blocks with guard faults.
 *
 * A generated block has the ABI
 *     nj_u4 block(nj_value *incoming_sp, nj_value *locals)
 * and uses only x0/x1 (the two arguments, unchanged while Java bytecodes
 * execute), x9..x13 and s0/s1 or d0/d1.  It never changes the native stack,
 * a callee-saved register, or a Java frame.
 *
 * It returns the number of bytecodes it finished.  A guard that can fail at
 * run time (null receiver, array type/length/flags, zero divisor) branches to
 * a per-fault exit that returns
 *     NJ_JIT_FAULT_BIT | (completed << 8) | fault_index
 * before touching memory, so the C caller can rewind pc/sp and let the
 * interpreter re-execute exactly that instruction and raise the Java
 * exception with the interpreter's own PC.  See nj_jit_internal.h.
 *
 * x12 holds the caller's FPCR once a floating-point operation has been
 * emitted.  Both the success return and every later fault exit restore it;
 * x12 is therefore never used as a general temporary.
 *
 * This is a portable encoder: target words are emitted as explicit
 * little-endian bytes, never through host instruction pointers.  All
 * compilation state is local to the invocation.
 */
#include "nj_jit_internal.h"

#if NJ_ENABLE_JIT

enum {
    A64_SP_ARG = 0, A64_LOCALS_ARG = 1,
    A64_T0 = 9, A64_T1 = 10, A64_T2 = 11,
    A64_FPCR = 12, A64_ADDR = 13, A64_ZERO = 31,
    A64_F0 = 0, A64_F1 = 1, A64_SLOT_BYTES = 8,
    /* Header offsets of nj_object.  Instance field payloads follow it. */
    A64_OBJ_DATA = 8, A64_OBJ_LENGTH = 16,
    A64_OBJ_KIND = 24, A64_OBJ_AFLAGS = 26
};

typedef struct a64_fixup {
    nj_size at;      /* offset of the branch word inside the buffer */
    nj_u4 word;      /* conditional branch with a zero displacement */
    nj_u1 fault;     /* fault exit index */
} a64_fixup;

typedef struct a64_writer {
    nj_u1 *buf;
    nj_size used;
    nj_size capacity;
    int failed;
    int fpcr_saved;
    nj_u1 nfixups;
    a64_fixup fixups[NJ_JIT_MAX_FAULTS * 4];
} a64_writer;

typedef struct a64_bytecode {
    unsigned op;
    unsigned local;
    int immediate;
    int need;
    int change;
    nj_u4 length;
    const nj_field *field;   /* getfield/putfield only */
    int guarding;            /* instruction can fault at run time */
} a64_bytecode;

static void a64_u32(a64_writer *w, nj_u4 word)
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

static void a64_r3(a64_writer *w, nj_u4 opcode,
                   unsigned rd, unsigned rn, unsigned rm)
{
    a64_u32(w, opcode | ((nj_u4)rm << 16) | ((nj_u4)rn << 5) | rd);
}

static void a64_li32(a64_writer *w, unsigned rd, nj_u4 value)
{
    a64_u32(w, 0x52800000u | ((value & 0xffffu) << 5) | rd); /* movz w */
    if (value >> 16)
        a64_u32(w, 0x72a00000u | ((value >> 16) << 5) | rd); /* movk w,lsl16 */
}

/* Materialise an offset only when the shorter memory forms cannot encode it.
 * All Java locals (u16 indexes) and all bounded block stack deltas fit this
 * signed 24-bit magnitude. x13 is reserved exclusively for address formation. */
static unsigned a64_address(a64_writer *w, unsigned base, int offset)
{
    nj_u4 magnitude, opcode;
    unsigned high, low;
    if (offset < -0xffffff || offset > 0xffffff) {
        w->failed = 1;
        return base;
    }
    magnitude = (nj_u4)(offset < 0 ? -offset : offset);
    opcode = offset < 0 ? 0xd1000000u : 0x91000000u; /* sub/add x,imm */
    high = magnitude >> 12;
    low = magnitude & 0xfffu;
    if (high) {
        a64_u32(w, opcode | 0x00400000u | ((nj_u4)high << 10) |
                    ((nj_u4)base << 5) | A64_ADDR);
        base = A64_ADDR;
    }
    if (low) {
        a64_u32(w, opcode | ((nj_u4)low << 10) |
                    ((nj_u4)base << 5) | A64_ADDR);
        base = A64_ADDR;
    }
    return base;
}

/* Integer or scalar-FP load/store, 4 or 8 bytes. Prefer unsigned scaled imm12,
 * then signed unscaled imm9; large negative/positive addresses use x13. */
static void a64_mem(a64_writer *w, unsigned reg, unsigned base, int offset,
                    int wide, int fp, int load)
{
    unsigned scale = wide ? 3u : 2u;
    nj_u4 opcode = 0xb8000000u | (wide ? 0x40000000u : 0u) |
                   (fp ? 0x04000000u : 0u) | (load ? 0x00400000u : 0u);
    if (offset >= 0 && ((unsigned)offset & ((1u << scale) - 1u)) == 0u &&
        ((unsigned)offset >> scale) <= 4095u) {
        a64_u32(w, opcode | 0x01000000u |
                    (((nj_u4)offset >> scale) << 10) |
                    ((nj_u4)base << 5) | reg);
    } else if (offset >= -256 && offset <= 255) {
        a64_u32(w, opcode | (((nj_u4)offset & 0x1ffu) << 12) |
                    ((nj_u4)base << 5) | reg);
    } else {
        base = a64_address(w, base, offset);
        a64_u32(w, opcode | 0x01000000u | ((nj_u4)base << 5) | reg);
    }
}

static void a64_load(a64_writer *w, unsigned reg, unsigned base,
                     int offset, int wide)
{
    a64_mem(w, reg, base, offset, wide, 0, 1);
}

static void a64_store(a64_writer *w, unsigned reg, unsigned base,
                      int offset, int wide)
{
    a64_mem(w, reg, base, offset, wide, 0, 0);
}

/* Byte/half/word/doubleword integer load or store with an optional 32-bit
 * sign extension.  Used for object headers and instance-field payloads. */
static void a64_mem_sized(a64_writer *w, unsigned reg, unsigned base,
                          int offset, unsigned size, int load, int sign)
{
    unsigned scale = size;
    nj_u4 opcode = 0x38000000u | (size << 30) |
                   (load ? 0x00400000u : 0u) |
                   (sign ? 0x00800000u : 0u);
    if (offset >= 0 && ((unsigned)offset & ((1u << scale) - 1u)) == 0u &&
        ((unsigned)offset >> scale) <= 4095u) {
        a64_u32(w, opcode | 0x01000000u |
                    (((nj_u4)offset >> scale) << 10) |
                    ((nj_u4)base << 5) | reg);
    } else if (offset >= -256 && offset <= 255) {
        a64_u32(w, opcode | (((nj_u4)offset & 0x1ffu) << 12) |
                    ((nj_u4)base << 5) | reg);
    } else {
        base = a64_address(w, base, offset);
        a64_u32(w, opcode | 0x01000000u | ((nj_u4)base << 5) | reg);
    }
}

static void a64_add_shifted(a64_writer *w, unsigned rd, unsigned rn,
                            unsigned rm, unsigned shift)
{
    a64_u32(w, 0x8b000000u | ((shift & 63u) << 10) |
                ((nj_u4)rm << 16) | ((nj_u4)rn << 5) | rd);
}

static void a64_copy_slot(a64_writer *w, unsigned dst, int dst_offset,
                          unsigned src, int src_offset)
{
    a64_load(w, A64_T0, src, src_offset, 1);
    a64_store(w, A64_T0, dst, dst_offset, 1);
}

/* ---- guard branches and fault exits ---------------------------------- */

static void a64_fixup_emit(a64_writer *w, nj_u4 word, unsigned fault)
{
    if (w->failed) return;
    if (w->nfixups >= (nj_u1)(sizeof(w->fixups) / sizeof(w->fixups[0]))) {
        w->failed = 1;
        return;
    }
    w->fixups[w->nfixups].at = w->used;
    w->fixups[w->nfixups].word = word;
    w->fixups[w->nfixups].fault = (nj_u1)fault;
    w->nfixups++;
    a64_u32(w, word);
}

static void a64_bcond(a64_writer *w, unsigned cond, unsigned fault)
{
    a64_fixup_emit(w, 0x54000000u | (cond & 0xfu), fault);
}

static void a64_cbz(a64_writer *w, unsigned rt, int wide, unsigned fault)
{
    a64_fixup_emit(w, (wide ? 0xb4000000u : 0x34000000u) | (rt & 31u), fault);
}

static void a64_cmp_imm(a64_writer *w, unsigned rn, unsigned imm)
{
    a64_u32(w, 0x71000000u | ((imm & 0xfffu) << 10) |
                ((nj_u4)rn << 5) | A64_ZERO); /* cmp wN,#imm */
}

static void a64_cmp_reg(a64_writer *w, unsigned rn, unsigned rm)
{
    a64_u32(w, 0x6b000000u | ((nj_u4)rm << 16) |
                ((nj_u4)rn << 5) | A64_ZERO); /* cmp wN,wM */
}

static void a64_guard_null(a64_writer *w, unsigned rt, unsigned fault)
{
    a64_cbz(w, rt, 1, fault); /* cbz xN, exit */
}

static void a64_guard_zero32(a64_writer *w, unsigned rt, unsigned fault)
{
    a64_cbz(w, rt, 0, fault); /* cbz wN, exit */
}

static void a64_guard_eq(a64_writer *w, unsigned rn, unsigned imm,
                         unsigned fault)
{
    a64_cmp_imm(w, rn, imm);
    a64_bcond(w, 1u, fault); /* b.ne */
}

static void a64_guard_geu(a64_writer *w, unsigned rn, unsigned rm,
                          unsigned fault)
{
    a64_cmp_reg(w, rn, rm);
    a64_bcond(w, 2u, fault); /* b.hs: unsigned rn >= rm */
}

static void a64_patch_branch(nj_u1 *buf, const a64_fixup *f, nj_size target)
{
    nj_u4 word = f->word;
    nj_u4 imm = (nj_u4)((target - f->at) >> 2);
    word |= (imm & 0x7ffffu) << 5;
    buf[f->at] = (nj_u1)word;
    buf[f->at + 1u] = (nj_u1)(word >> 8);
    buf[f->at + 2u] = (nj_u1)(word >> 16);
    buf[f->at + 3u] = (nj_u1)(word >> 24);
}

static nj_u4 a64_fault_word(unsigned index, nj_u4 completed)
{
    return NJ_JIT_FAULT_BIT |
           ((completed & NJ_JIT_FAULT_COMPLETED_MASK) << 8) |
           (index & NJ_JIT_FAULT_INDEX_MASK);
}

/* Worst-case tail still to be written after the instruction prefix: the
 * success return plus one movz+movk+ret exit per recorded fault (with room for
 * an FPCR restore).  Keeping this free before accepting an instruction means
 * no late rollback when the exits are emitted. */
static nj_size a64_tail_bytes(int fpcr_saved, unsigned nfaults)
{
    return 8u + (fpcr_saved ? 4u : 0u) + (nj_size)nfaults * 16u;
}

#if NJ_ENABLE_FLOAT && NJ_JIT_ARM64_FP
static void a64_fp_begin(a64_writer *w)
{
    if (w->fpcr_saved || w->failed) return;
    w->fpcr_saved = 1;
    a64_u32(w, 0xd53b4400u | A64_FPCR); /* mrs x12,fpcr */
    a64_u32(w, 0xd51b4400u | A64_ZERO); /* msr fpcr,xzr: RNE, no FTZ/traps */
}
#endif

static int a64_s8(unsigned value)
{
    return value < 128u ? (int)value : (int)value - 256;
}

static int a64_s16(unsigned value)
{
    return value < 32768u ? (int)value : (int)value - 65536;
}

/* Access width/sign for an instance field.  LP64 nj_ref is eight bytes (the
 * class linker lays OBJECT fields out with sizeof(nj_ref) and the interpreter
 * copies sizeof(nj_ref) bytes), so OBJECT uses a doubleword load/store here,
 * exactly like LONG/DOUBLE.  Returned size codes: 0=byte, 1=half, 2=word,
 * 3=doubleword. */
static int a64_field_size(const nj_field *f, int *sign)
{
    *sign = 0;
    switch (f->ptype) {
    case NJ_PT_BOOLEAN:
    case NJ_PT_BYTE:   *sign = 1; return 0;
    case NJ_PT_CHAR:   return 1;
    case NJ_PT_SHORT:  *sign = 1; return 1;
    case NJ_PT_INT:
    case NJ_PT_FLOAT:  return 2;
    case NJ_PT_OBJECT:
    case NJ_PT_LONG:
    case NJ_PT_DOUBLE: return 3;
    default:           return -1;
    }
}

static int a64_decode(nj_vm *vm, const nj_method *m, nj_u4 pc,
                      a64_bytecode *b)
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

    if (op == 0xc4u) {
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
        b->need = op >= 0x36u ? slots : 0;
        b->change = op >= 0x36u ? -slots : slots;
        return 1;
    }

    switch (op) {
    case 0x00: return 1;
    case 0x01: /* aconst_null writes an entire LP64 reference */
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
        b->immediate = op == 0x0eu ? 0 : 0x3ff0;
        b->change = 2;
        return 1;
    case 0x10:
        if (left < 2u) return 0;
        b->length = 2u;
        b->immediate = a64_s8(code[1]);
        b->change = 1;
        return 1;
    case 0x11:
        if (left < 3u) return 0;
        b->length = 3u;
        b->immediate = a64_s16(((unsigned)code[1] << 8) | code[2]);
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
    case 0x57: case 0x58:
        b->need = op == 0x57u ? 1 : 2;
        b->change = -b->need;
        return 1;
    case 0x59: case 0x5c:
        b->need = op == 0x59u ? 1 : 2;
        b->change = b->need;
        return 1;
    case 0x5f:
        b->need = 2;
        return 1;
    case 0x60: case 0x64: case 0x68:
    case 0x78: case 0x7a: case 0x7c:
    case 0x7e: case 0x80: case 0x82:
        b->need = 2;
        b->change = -1;
        return 1;
    case 0x6c: case 0x70: /* idiv/irem, zero divisor faults */
        b->need = 2;
        b->change = -1;
        b->guarding = 1;
        return 1;
    case 0x74: case 0x91: case 0x92: case 0x93:
        b->need = 1;
        return 1;
    case 0x84:
        if (b->local >= m->max_locals) return 0;
        b->length = wide ? 6u : 3u;
        if (left < b->length) return 0;
        b->immediate = wide ? a64_s16(((unsigned)code[4] << 8) | code[5]) :
                               a64_s8(code[2]);
        return 1;
    case 0xb4: /* getfield */
    case 0xb5: { /* putfield */
        const nj_field *f;
        unsigned index;
        int is_put = op == 0xb5u;
        unsigned width;
        if (left < 3u) return 0;
        b->length = 3u;
        if (!vm) return 0;
        index = ((unsigned)code[1] << 8) | code[2];
        f = nj_jit_instance_field(vm, m->owner, index);
        if (!f) return 0;
        width = f->width;
        if (width != 1u && width != 2u) return 0;
        if ((f->ptype == NJ_PT_LONG || f->ptype == NJ_PT_DOUBLE) ?
            width != 2u : width != 1u) return 0;
        if (a64_field_size(f, &slots) < 0) return 0;
        /* Defensive re-check against the payload size this encoder will move
         * (the shared helper already bounds pointer-sized OBJECT fields). */
        if ((nj_u4)f->offset + (1u << (unsigned)slots) >
            (nj_u4)f->owner->instance_size) return 0;
        b->field = f;
        if (is_put) {
            b->need = 1 + (int)width;
            b->change = -b->need;
        } else {
            b->need = 1;
            b->change = width == 2u ? 1 : 0;
        }
        b->guarding = 1; /* null receiver */
        return 1;
    }
    case 0xbe: /* arraylength */
        if (!vm) return 0;
        b->need = 1;
        b->guarding = 1;
        return 1;
#if NJ_ENABLE_FLOAT && NJ_JIT_ARM64_FP
    case 0x62: case 0x66: case 0x6a: case 0x6e:
        b->need = 2;
        b->change = -1;
        return 1;
    case 0x76:
        b->need = 1;
        return 1;
#if NJ_ENABLE_LONG
    case 0x63: case 0x67: case 0x6b: case 0x6f:
        b->need = 4;
        b->change = -2;
        return 1;
    case 0x77:
        b->need = 2;
        return 1;
#endif
#endif
    default:
        return 0;
    }
}

static void a64_emit(a64_writer *w, const a64_bytecode *b, int delta,
                     unsigned fault)
{
    unsigned op = b->op;
    int top = delta * A64_SLOT_BYTES;
    int local = (int)b->local * A64_SLOT_BYTES;
    nj_u4 opcode;
    if (op >= 0x15u && op <= 0x19u) {
        if (b->change == 2) {
            a64_copy_slot(w, A64_SP_ARG, top, A64_LOCALS_ARG, local);
            a64_store(w, A64_ZERO, A64_SP_ARG, top + 8, 0);
        } else {
            int reference = op == 0x19u;
            a64_load(w, A64_T0, A64_LOCALS_ARG, local, reference);
            a64_store(w, A64_T0, A64_SP_ARG, top, reference);
        }
        return;
    }
    if (op >= 0x36u && op <= 0x3au) {
        if (b->need == 2) {
            a64_copy_slot(w, A64_LOCALS_ARG, local, A64_SP_ARG, top - 16);
            a64_store(w, A64_ZERO, A64_LOCALS_ARG, local + 8, 0);
        } else {
            int reference = op == 0x3au;
            a64_load(w, A64_T0, A64_SP_ARG, top - 8, reference);
            a64_store(w, A64_T0, A64_LOCALS_ARG, local, reference);
        }
        return;
    }
    switch (op) {
    case 0x00: case 0x57: case 0x58:
        return; /* pc/sp changes are committed by the C caller. */
    case 0x01:
        a64_store(w, A64_ZERO, A64_SP_ARG, top, 1);
        return;
    case 0x02: case 0x03: case 0x04: case 0x05:
    case 0x06: case 0x07: case 0x08: case 0x0b: case 0x0c: case 0x0d:
    case 0x10: case 0x11: case 0x12: case 0x13:
        a64_li32(w, A64_T0, (nj_u4)b->immediate);
        a64_store(w, A64_T0, A64_SP_ARG, top, 0);
        return;
    case 0x09: case 0x0a:
        a64_li32(w, A64_T0, (nj_u4)b->immediate); /* w write zero-extends x */
        a64_store(w, A64_T0, A64_SP_ARG, top, 1);
        a64_store(w, A64_ZERO, A64_SP_ARG, top + 8, 0);
        return;
    case 0x0e: case 0x0f:
        a64_u32(w, 0xd2e00000u | ((nj_u4)b->immediate << 5) | A64_T0);
        a64_store(w, A64_T0, A64_SP_ARG, top, 1); /* movz x,imm,lsl48 */
        a64_store(w, A64_ZERO, A64_SP_ARG, top + 8, 0);
        return;
    case 0x59:
        a64_copy_slot(w, A64_SP_ARG, top, A64_SP_ARG, top - 8);
        return;
    case 0x5c:
        a64_copy_slot(w, A64_SP_ARG, top, A64_SP_ARG, top - 16);
        a64_copy_slot(w, A64_SP_ARG, top + 8, A64_SP_ARG, top - 8);
        return;
    case 0x5f:
        a64_load(w, A64_T0, A64_SP_ARG, top - 8, 1);
        a64_load(w, A64_T1, A64_SP_ARG, top - 16, 1);
        a64_store(w, A64_T0, A64_SP_ARG, top - 16, 1);
        a64_store(w, A64_T1, A64_SP_ARG, top - 8, 1);
        return;
    case 0x84: {
        unsigned magnitude = (unsigned)(b->immediate < 0 ?
                                        -b->immediate : b->immediate);
        a64_load(w, A64_T0, A64_LOCALS_ARG, local, 0);
        if (magnitude <= 4095u) {
            opcode = b->immediate < 0 ? 0x51000000u : 0x11000000u;
            a64_u32(w, opcode | ((nj_u4)magnitude << 10) |
                        ((nj_u4)A64_T0 << 5) | A64_T0);
        } else {
            a64_li32(w, A64_T1, (nj_u4)b->immediate);
            a64_r3(w, 0x0b000000u, A64_T0, A64_T0, A64_T1);
        }
        a64_store(w, A64_T0, A64_LOCALS_ARG, local, 0);
        return;
    }
    case 0x74:
        a64_load(w, A64_T0, A64_SP_ARG, top - 8, 0);
        a64_r3(w, 0x4b000000u, A64_T0, A64_ZERO, A64_T0);
        a64_store(w, A64_T0, A64_SP_ARG, top - 8, 0);
        return;
    case 0x91: case 0x92: case 0x93:
        a64_load(w, A64_T0, A64_SP_ARG, top - 8, 0);
        opcode = op == 0x92u ? 0x53000000u : 0x13000000u; /* ubfm/sbfm */
        a64_u32(w, opcode | ((op == 0x91u ? 7u : 15u) << 10) |
                    ((nj_u4)A64_T0 << 5) | A64_T0);
        a64_store(w, A64_T0, A64_SP_ARG, top - 8, 0);
        return;
    case 0x6c: case 0x70: {
        int is_remainder = op == 0x70u;
        a64_load(w, A64_T1, A64_SP_ARG, top - 8, 0); /* divisor */
        a64_guard_zero32(w, A64_T1, fault);
        a64_load(w, A64_T0, A64_SP_ARG, top - 16, 0); /* dividend */
        if (is_remainder) {
            a64_r3(w, 0x1ac00c00u, A64_T2, A64_T0, A64_T1); /* sdiv */
            a64_r3(w, 0x1b008000u | ((nj_u4)A64_T0 << 10),
                   A64_T0, A64_T2, A64_T1); /* msub: a - (a/b)*b */
        } else {
            a64_r3(w, 0x1ac00c00u, A64_T0, A64_T0, A64_T1);
        }
        a64_store(w, A64_T0, A64_SP_ARG, top - 16, 0);
        return;
    }
    case 0x2e: { /* iaload: index, array -> value */
        a64_load(w, A64_T0, A64_SP_ARG, top - 16, 1);
        a64_guard_null(w, A64_T0, fault);
        a64_load(w, A64_T1, A64_SP_ARG, top - 8, 0); /* index, zero-extended */
        a64_mem_sized(w, A64_T2, A64_T0, A64_OBJ_KIND, 0, 1, 0);
        a64_guard_eq(w, A64_T2, NJ_OBJ_ARRAY, fault);
        a64_mem_sized(w, A64_T2, A64_T0, A64_OBJ_LENGTH, 2, 1, 0);
        a64_guard_geu(w, A64_T1, A64_T2, fault);
        a64_mem_sized(w, A64_T2, A64_T0, A64_OBJ_AFLAGS, 1, 1, 0);
        a64_guard_eq(w, A64_T2, NJ_ARRAY_T_INT, fault);
        a64_mem_sized(w, A64_T2, A64_T0, A64_OBJ_DATA, 3, 1, 0);
        a64_add_shifted(w, A64_T2, A64_T2, A64_T1, 2);
        a64_load(w, A64_T0, A64_T2, 0, 0);
        a64_store(w, A64_T0, A64_SP_ARG, top - 16, 0);
        return;
    }
    case 0x4f: { /* iastore: value, index, array */
        a64_load(w, A64_T0, A64_SP_ARG, top - 24, 1);
        a64_guard_null(w, A64_T0, fault);
        a64_load(w, A64_T1, A64_SP_ARG, top - 16, 0); /* index */
        a64_mem_sized(w, A64_T2, A64_T0, A64_OBJ_KIND, 0, 1, 0);
        a64_guard_eq(w, A64_T2, NJ_OBJ_ARRAY, fault);
        a64_mem_sized(w, A64_T2, A64_T0, A64_OBJ_LENGTH, 2, 1, 0);
        a64_guard_geu(w, A64_T1, A64_T2, fault);
        a64_mem_sized(w, A64_T2, A64_T0, A64_OBJ_AFLAGS, 1, 1, 0);
        a64_guard_eq(w, A64_T2, NJ_ARRAY_T_INT, fault);
        a64_mem_sized(w, A64_T2, A64_T0, A64_OBJ_DATA, 3, 1, 0);
        a64_add_shifted(w, A64_T2, A64_T2, A64_T1, 2);
        a64_load(w, A64_T1, A64_SP_ARG, top - 8, 0); /* value, after guards */
        a64_store(w, A64_T1, A64_T2, 0, 0);
        return;
    }
    case 0xbe: { /* arraylength */
        a64_load(w, A64_T0, A64_SP_ARG, top - 8, 1);
        a64_guard_null(w, A64_T0, fault);
        a64_mem_sized(w, A64_T1, A64_T0, A64_OBJ_LENGTH, 2, 1, 0);
        a64_store(w, A64_T1, A64_SP_ARG, top - 8, 0);
        return;
    }
    case 0xb4: { /* getfield: receiver -> value */
        const nj_field *f = b->field;
        int sign = 0;
        int size;
        if (!f) {
            w->failed = 1;
            return;
        }
        size = a64_field_size(f, &sign);
        if (size < 0) {
            w->failed = 1;
            return;
        }
        a64_load(w, A64_T0, A64_SP_ARG, top - 8, 1);
        a64_guard_null(w, A64_T0, fault);
        a64_mem_sized(w, A64_T1, A64_T0, (int)f->offset,
                      (unsigned)size, 1, sign);
        if (size == 3) {
            a64_store(w, A64_T1, A64_SP_ARG, top - 8, 1);
            if (f->width == 2u)
                a64_store(w, A64_ZERO, A64_SP_ARG, top, 0);
        } else {
            /* int/float: four bytes, high half preserved. */
            a64_store(w, A64_T1, A64_SP_ARG, top - 8, 0);
        }
        return;
    }
    case 0xb5: { /* putfield: receiver, value (value on top) */
        const nj_field *f = b->field;
        int sign = 0;
        int size;
        int value_slot;
        int receiver_slot;
        if (!f) {
            w->failed = 1;
            return;
        }
        size = a64_field_size(f, &sign);
        if (size < 0) {
            w->failed = 1;
            return;
        }
        value_slot = top - (int)f->width * A64_SLOT_BYTES;
        receiver_slot = value_slot - A64_SLOT_BYTES;
        a64_load(w, A64_T0, A64_SP_ARG, receiver_slot, 1);
        a64_guard_null(w, A64_T0, fault);
        if (size == 3) {
            a64_load(w, A64_T1, A64_SP_ARG, value_slot, 1);
            a64_mem_sized(w, A64_T1, A64_T0, (int)f->offset, 3, 0, 0);
        } else {
            a64_load(w, A64_T1, A64_SP_ARG, value_slot, 0);
            a64_mem_sized(w, A64_T1, A64_T0, (int)f->offset,
                          (unsigned)size, 0, 0);
        }
        return;
    }
#if NJ_ENABLE_FLOAT && NJ_JIT_ARM64_FP
    case 0x76:
#if NJ_ENABLE_LONG
    case 0x77:
#endif
    {
        int is_double = (int)(op & 1u);
        int dst = top - (is_double ? 16 : 8);
        a64_load(w, A64_T0, A64_SP_ARG, dst, is_double);
        if (is_double)
            a64_u32(w, 0xd2f00000u | A64_T1); /* movz x10,0x8000,lsl48 */
        else
            a64_li32(w, A64_T1, 0x80000000u);
        a64_r3(w, is_double ? 0xca000000u : 0x4a000000u,
                   A64_T0, A64_T0, A64_T1);
        a64_store(w, A64_T0, A64_SP_ARG, dst, is_double);
        return; /* Toggle only the sign: preserve NaN payload and padding. */
    }
    case 0x62: case 0x66: case 0x6a: case 0x6e:
#if NJ_ENABLE_LONG
    case 0x63: case 0x67: case 0x6b: case 0x6f:
#endif
    {
        int is_double = (int)(op & 1u);
        int width = is_double ? 16 : 8;
        int dst = top - width * 2;
        a64_fp_begin(w);
        a64_mem(w, A64_F0, A64_SP_ARG, dst, is_double, 1, 1);
        a64_mem(w, A64_F1, A64_SP_ARG, top - width, is_double, 1, 1);
        switch (op & ~1u) {
        case 0x62: opcode = 0x1e202800u; break; /* fadd */
        case 0x66: opcode = 0x1e203800u; break; /* fsub */
        case 0x6a: opcode = 0x1e200800u; break; /* fmul */
        default: opcode = 0x1e201800u; break;  /* fdiv */
        }
        if (is_double) opcode |= 0x00400000u;
        a64_r3(w, opcode, A64_F0, A64_F0, A64_F1);
        a64_mem(w, A64_F0, A64_SP_ARG, dst, is_double, 1, 0);
        if (is_double) a64_store(w, A64_ZERO, A64_SP_ARG, dst + 8, 0);
        return;
    }
#endif
    case 0x60: opcode = 0x0b000000u; break; /* add */
    case 0x64: opcode = 0x4b000000u; break; /* sub */
    case 0x68: opcode = 0x1b007c00u; break; /* mul (madd with wzr) */
    case 0x78: opcode = 0x1ac02000u; break; /* lslv */
    case 0x7a: opcode = 0x1ac02800u; break; /* asrv */
    case 0x7c: opcode = 0x1ac02400u; break; /* lsrv */
    case 0x7e: opcode = 0x0a000000u; break; /* and */
    case 0x80: opcode = 0x2a000000u; break; /* orr */
    case 0x82: opcode = 0x4a000000u; break; /* eor */
    default:
        w->failed = 1;
        return;
    }
    a64_load(w, A64_T0, A64_SP_ARG, top - 16, 0);
    a64_load(w, A64_T1, A64_SP_ARG, top - 8, 0);
    a64_r3(w, opcode, A64_T0, A64_T0, A64_T1);
    /* W-register shifts mask the count to five bits. SDIV wraps INT_MIN/-1
     * as Java requires. */
    a64_store(w, A64_T0, A64_SP_ARG, top - 16, 0);
}

/* Decide whether one more instruction is allowed by the fault budget. */
static int a64_accept(const nj_jit_block *block, const a64_bytecode *b)
{
    if (b->guarding && block->nfaults >= NJ_JIT_MAX_FAULTS) return 0;
    /* A throwing integer division may not follow mutations in a block: if it
     * faults, the interpreter must observe the untouched incoming stack. */
    if ((b->op == 0x6cu || b->op == 0x70u) && block->insns != 0u) return 0;
    return 1;
}

static int a64_terminates(const a64_bytecode *b)
{
    return b->op == 0x6cu || b->op == 0x70u;
}

static int a64_analyze_impl(nj_vm *vm, const nj_method *m, nj_u4 pc,
                            nj_jit_block *out)
{
    nj_jit_block block;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!m || !m->code || pc >= m->code_len ||
        sizeof(nj_value) != A64_SLOT_BYTES) return 0;
    memset(&block, 0, sizeof(block));
    block.next_pc = pc;
    while (block.next_pc < m->code_len &&
           block.insns < NJ_JIT_MAX_BLOCK_INSNS) {
        a64_bytecode b;
        int delta, required, growth;
        if (!a64_decode(vm, m, block.next_pc, &b)) break;
        if (!a64_accept(&block, &b)) break;
        delta = block.stack_delta + b.change;
        required = b.need - block.stack_delta;
        if (required < block.min_stack) required = block.min_stack;
        growth = delta > block.max_growth ? delta : block.max_growth;
        if (required + growth > (int)m->max_stack) break;
        if (b.guarding &&
            !nj_jit_record_fault(&block, pc, block.next_pc,
                                 block.stack_delta)) break;
        block.next_pc += b.length;
        block.insns++;
        block.stack_delta = delta;
        block.min_stack = required;
        block.max_growth = growth;
        if (a64_terminates(&b)) break;
    }
    if (block.insns == 0u) return 0;
    *out = block;
    return 1;
}

int nj_arm64_analyze(nj_vm *vm, const nj_method *m, nj_u4 pc, nj_jit_block *out)
{
    return a64_analyze_impl(vm, m, pc, out);
}

int nj_arm64_compile(nj_vm *vm, const nj_method *m, nj_u4 pc, nj_u1 *buf,
                     nj_size capacity, nj_jit_block *out)
{
    a64_writer w;
    nj_jit_block block;
    nj_u1 completed[NJ_JIT_MAX_FAULTS];
    nj_u1 fault_fp[NJ_JIT_MAX_FAULTS];
    nj_size stub_at[NJ_JIT_MAX_FAULTS];
    nj_u1 i;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!m || !m->code || !buf || pc >= m->code_len ||
        sizeof(nj_value) != A64_SLOT_BYTES) return 0;
    if (capacity > NJ_JIT_MAX_CODE_BYTES) capacity = NJ_JIT_MAX_CODE_BYTES;
    if (capacity < 4u) return 0; /* no room for the BTI landing pad */
    memset(&block, 0, sizeof(block));
    block.next_pc = pc;
    memset(&w, 0, sizeof(w));
    memset(completed, 0, sizeof(completed));
    memset(fault_fp, 0, sizeof(fault_fp));
    w.buf = buf;
    w.capacity = capacity;
    /* Indirect calls may target a BTI-guarded executable page. This landing
     * pad is a backwards-compatible HINT on cores without FEAT_BTI. */
    a64_u32(&w, 0xd503245fu); /* bti c */
    if (w.failed) return 0;

    while (block.next_pc < m->code_len &&
           block.insns < NJ_JIT_MAX_BLOCK_INSNS) {
        a64_bytecode b;
        a64_writer checkpoint = w;
        unsigned fault = block.nfaults;
        nj_u1 fp_at_entry = (nj_u1)w.fpcr_saved;
        unsigned tail_faults;
        int delta, required, growth;
        if (!a64_decode(vm, m, block.next_pc, &b)) break;
        if (!a64_accept(&block, &b)) break;
        delta = block.stack_delta + b.change;
        required = b.need - block.stack_delta;
        if (required < block.min_stack) required = block.min_stack;
        growth = delta > block.max_growth ? delta : block.max_growth;
        if (required + growth > (int)m->max_stack) break;
        a64_emit(&w, &b, block.stack_delta, fault);
        if (w.failed) {
            /* Discard only this instruction. The tail written below overwrites
             * its first bytes; no partial instruction enters the live block. */
            w = checkpoint;
            break;
        }
        /* Keep room for the success return and every already-accepted fault
         * exit, plus this instruction's exit when it is guarded. */
        tail_faults = block.nfaults + (b.guarding ? 1u : 0u);
        if (w.used > capacity ||
            capacity - w.used < a64_tail_bytes(w.fpcr_saved, tail_faults)) {
            w = checkpoint;
            break;
        }
        if (b.guarding) {
            if (!nj_jit_record_fault(&block, pc, block.next_pc,
                                     block.stack_delta)) {
                w = checkpoint;
                break;
            }
            completed[fault] = (nj_u1)block.insns;
            fault_fp[fault] = fp_at_entry;
        }
        block.next_pc += b.length;
        block.insns++;
        block.stack_delta = delta;
        block.min_stack = required;
        block.max_growth = growth;
        if (a64_terminates(&b)) break;
    }
    if (block.insns == 0u) return 0;

    /* Success exit: w0 = number of finished bytecodes. */
    a64_li32(&w, A64_SP_ARG, block.insns);
    if (w.fpcr_saved) a64_u32(&w, 0xd51b4400u | A64_FPCR); /* msr fpcr,x12 */
    a64_u32(&w, 0xd65f03c0u); /* ret x30 */

    /* One fault exit per guarded instruction. Each is reached by a forward
     * branch taken before the instruction's first side effect. */
    for (i = 0u; i < block.nfaults; i++) {
        stub_at[i] = w.used;
        a64_li32(&w, A64_SP_ARG,
                 a64_fault_word(i, (nj_u4)completed[i]));
        if (fault_fp[i])
            a64_u32(&w, 0xd51b4400u | A64_FPCR); /* msr fpcr,x12 */
        a64_u32(&w, 0xd65f03c0u); /* ret x30 */
    }
    if (w.failed) return 0;

    for (i = 0u; i < w.nfixups; i++) {
        unsigned f = w.fixups[i].fault;
        if (f >= block.nfaults || stub_at[f] <= w.fixups[i].at) {
            w.failed = 1;
            break;
        }
        a64_patch_branch(buf, &w.fixups[i], stub_at[f]);
    }
    if (w.failed) return 0;

    block.size = w.used;
    *out = block;
    return 1;
}
#endif /* NJ_ENABLE_JIT */
