/*
 * Bounded little-endian AArch64 baseline blocks with guard faults, forward
 * branches and bounded loops.
 *
 * A generated block has the ABI
 *     nj_u4 block(nj_value *incoming_sp, nj_value *locals, nj_u4 budget)
 * and uses only x0/x1/x2 (the three arguments, unchanged while Java bytecodes
 * execute), x3/x4/x5 (branch bookkeeping and the loop budget check), x9..x13
 * and s0/s1 or d0/d1.  It never changes the native stack, a callee-saved
 * register, or a Java frame.
 *
 * A block returns a success word
 *     (completed << NJ_JIT_EXIT_SHIFT) | exit_index
 * where completed is the number of finished bytecodes and exit_index selects
 * block.exits[]: 0 is the end of the straight-line window, other entries are
 * taken forward branches that leave the window and the loop budget exit.  A
 * guard that can fail at run time (null receiver, array type/length/flags,
 * zero divisor) branches to a per-fault exit that returns
 *     NJ_JIT_FAULT_BIT | (fault_index << 24) | completed
 * before touching memory, so the C caller can rewind pc/sp and let the
 * interpreter re-execute exactly that instruction and raise the Java
 * exception with the interpreter's own PC.  See nj_jit_internal.h.
 *
 * A block containing any taken branch keeps the completed counter in w3 and
 * derives both success and fault words from it.  A loop keeps the counter
 * across its back edge and, at the entry label, verifies that `budget -
 * completed` still covers a whole window before running another iteration, so
 * native execution never finishes more than `budget` bytecodes.
 *
 * When a block performs floating point, the caller's FPCR is saved at entry
 * and every return stub shares one restore epilogue.  That keeps the caller's
 * rounding mode intact on every path, including exits taken before the first
 * FP operation, while still restoring exactly once per return.
 *
 * This is a portable encoder: target words are emitted as explicit
 * little-endian bytes, never through host instruction pointers.  All
 * compilation state is local to the invocation.
 */
#include "nj_jit_internal.h"

#if NJ_ENABLE_JIT

enum {
    A64_SP_ARG = 0, A64_LOCALS_ARG = 1, A64_BUDGET_ARG = 2, A64_DONE = 3,
    A64_T3 = 4, A64_T4 = 5,
    A64_T0 = 9, A64_T1 = 10, A64_T2 = 11,
    A64_FPCR = 12, A64_ADDR = 13, A64_ZERO = 31,
    A64_F0 = 0, A64_F1 = 1, A64_SLOT_BYTES = 8,
    /* Header offsets of nj_object.  Instance field payloads follow it. */
    A64_OBJ_DATA = 8, A64_OBJ_LENGTH = 16,
    A64_OBJ_KIND = 24, A64_OBJ_AFLAGS = 26
};

enum {
    A64_FIX_FAULT = 0u,   /* target is a fault-exit index             */
    A64_FIX_EXIT = 1u,    /* target is a block-exit index             */
    A64_FIX_LABEL = 2u,   /* target is an instruction (label) index   */
    A64_FIX_EPILOGUE = 3u /* target is the shared FPCR restore epilogue */
};

#define A64_MAX_FIXUPS 192u
#define A64_NO_LABEL ((nj_size)(-1))

typedef struct a64_fixup {
    nj_size at;      /* offset of the branch word inside the buffer */
    nj_u4 word;      /* branch with a zero displacement */
    nj_u1 kind;      /* A64_FIX_* */
    nj_u2 target;    /* fault index, exit index or instruction index */
} a64_fixup;

typedef struct a64_writer {
    nj_u1 *buf;
    nj_size used;
    nj_size capacity;
    int failed;
    int counter;      /* track completed bytecodes in w3 */
    unsigned nfixups;
    a64_fixup fixups[A64_MAX_FIXUPS];
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
    int branch;              /* 0 none, 1 conditional, 2 unconditional */
    nj_u4 target;            /* absolute branch target pc */
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

/* ---- guard branches, block branches and fault exits ------------------- */

static void a64_fixup_emit(a64_writer *w, nj_u4 word, unsigned kind,
                           unsigned target)
{
    if (w->failed) return;
    if (w->nfixups >= A64_MAX_FIXUPS) {
        w->failed = 1;
        return;
    }
    w->fixups[w->nfixups].at = w->used;
    w->fixups[w->nfixups].word = word;
    w->fixups[w->nfixups].kind = (nj_u1)kind;
    w->fixups[w->nfixups].target = (nj_u2)target;
    w->nfixups++;
    a64_u32(w, word);
}

/* Branch to a guard/fault exit.  The placeholder displacement is patched once
 * all stubs have been emitted. */
static void a64_bcond(a64_writer *w, unsigned cond, unsigned fault)
{
    a64_fixup_emit(w, 0x54000000u | (cond & 0xfu), A64_FIX_FAULT, fault);
}

static void a64_cbz(a64_writer *w, unsigned rt, int wide, unsigned fault)
{
    a64_fixup_emit(w, (wide ? 0xb4000000u : 0x34000000u) | (rt & 31u),
                   A64_FIX_FAULT, fault);
}

static void a64_branch_cond(a64_writer *w, unsigned cond, unsigned kind,
                            unsigned target)
{
    a64_fixup_emit(w, 0x54000000u | (cond & 0xfu), kind, target);
}

static void a64_branch(a64_writer *w, unsigned kind, unsigned target)
{
    a64_fixup_emit(w, 0x14000000u, kind, target);
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

static void a64_cmp_reg64(a64_writer *w, unsigned rn, unsigned rm)
{
    a64_u32(w, 0xeb000000u | ((nj_u4)rm << 16) |
                ((nj_u4)rn << 5) | A64_ZERO); /* cmp xN,xM */
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

/* B.cond and CBZ carry a signed 19-bit word displacement; B carries 26 bits.
 * Backward displacements (the loop back edge) fall out of the same unsigned
 * subtraction because the low bits keep the two's-complement value. */
static void a64_patch_branch(nj_u1 *buf, const a64_fixup *f, nj_size target)
{
    nj_u4 word = f->word;
    nj_u4 imm = (nj_u4)((target - f->at) >> 2);
    if ((word & 0xff000000u) == 0x14000000u)
        word |= imm & 0x3ffffffu;
    else
        word |= (imm & 0x7ffffu) << 5;
    buf[f->at] = (nj_u1)word;
    buf[f->at + 1u] = (nj_u1)(word >> 8);
    buf[f->at + 2u] = (nj_u1)(word >> 16);
    buf[f->at + 3u] = (nj_u1)(word >> 24);
}

static nj_u4 a64_fault_word(unsigned index, nj_u4 completed)
{
    return NJ_JIT_FAULT_BIT |
           ((nj_u4)(index & NJ_JIT_FAULT_INDEX_MASK) <<
            NJ_JIT_FAULT_INDEX_SHIFT) |
           (completed & NJ_JIT_FAULT_COMPLETED_MASK);
}

/* One executed bytecode.  ADD (immediate) does not modify the condition
 * flags, so it may sit between a CMP and its conditional branch. */
static void a64_inc_done(a64_writer *w)
{
    a64_u32(w, 0x11000000u | (1u << 10) |
                ((nj_u4)A64_DONE << 5) | A64_DONE);
}

/* Clear the caller's FPCR once per block; the rounding mode is restored by
 * the shared epilogue just before every return. */
static void a64_fp_save(a64_writer *w)
{
#if NJ_ENABLE_FLOAT && NJ_JIT_ARM64_FP
    a64_u32(w, 0xd53b4400u | A64_FPCR); /* mrs x12,fpcr */
    a64_u32(w, 0xd51b4400u | A64_ZERO); /* msr fpcr,xzr: RNE, no FTZ/traps */
#else
    (void)w;
#endif
}

static void a64_fp_restore(a64_writer *w)
{
#if NJ_ENABLE_FLOAT && NJ_JIT_ARM64_FP
    a64_u32(w, 0xd51b4400u | A64_FPCR); /* msr fpcr,x12 */
#else
    (void)w;
#endif
}

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
    b->branch = 0;
    b->target = pc;

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
    case 0x99: case 0x9a: case 0x9b: case 0x9c:
    case 0x9d: case 0x9e: /* if<cond>: pop 1, signed compare against zero */
        b->length = 3u;
        if (left < 3u) return 0;
        b->immediate = a64_s16(((unsigned)code[1] << 8) | code[2]);
        if (pc + (nj_u4)b->immediate >= m->code_len) return 0;
        b->need = 1;
        b->change = -1;
        b->branch = 1;
        b->target = pc + (nj_u4)b->immediate;
        return 1;
    case 0x9f: case 0xa0: case 0xa1: case 0xa2:
    case 0xa3: case 0xa4: /* if_icmp<cond>: pop 2, signed 32-bit compare */
        b->length = 3u;
        if (left < 3u) return 0;
        b->immediate = a64_s16(((unsigned)code[1] << 8) | code[2]);
        if (pc + (nj_u4)b->immediate >= m->code_len) return 0;
        b->need = 2;
        b->change = -2;
        b->branch = 1;
        b->target = pc + (nj_u4)b->immediate;
        return 1;
    case 0xa5: case 0xa6: /* if_acmpeq/ne: full 64-bit references */
        b->length = 3u;
        if (left < 3u) return 0;
        b->immediate = a64_s16(((unsigned)code[1] << 8) | code[2]);
        if (pc + (nj_u4)b->immediate >= m->code_len) return 0;
        b->need = 2;
        b->change = -2;
        b->branch = 1;
        b->target = pc + (nj_u4)b->immediate;
        return 1;
    case 0xa7: /* goto */
        b->length = 3u;
        if (left < 3u) return 0;
        b->immediate = a64_s16(((unsigned)code[1] << 8) | code[2]);
        if (pc + (nj_u4)b->immediate >= m->code_len) return 0;
        b->branch = 2;
        b->target = pc + (nj_u4)b->immediate;
        return 1;
    case 0xc6: case 0xc7: /* ifnull / ifnonnull */
        b->length = 3u;
        if (left < 3u) return 0;
        b->immediate = a64_s16(((unsigned)code[1] << 8) | code[2]);
        if (pc + (nj_u4)b->immediate >= m->code_len) return 0;
        b->need = 1;
        b->change = -1;
        b->branch = 1;
        b->target = pc + (nj_u4)b->immediate;
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

/* ---- block scanning -------------------------------------------------- */

typedef struct a64_scan {
    a64_bytecode ins[NJ_JIT_MAX_BLOCK_INSNS];
    nj_u4 pcs[NJ_JIT_MAX_BLOCK_INSNS];
    int delta[NJ_JIT_MAX_BLOCK_INSNS];       /* operand depth before the insn */
    nj_u1 label[NJ_JIT_MAX_BLOCK_INSNS];     /* branch target instructions    */
    nj_u1 br_kind[NJ_JIT_MAX_BLOCK_INSNS];   /* A64_BR_*                      */
    nj_u2 br_target[NJ_JIT_MAX_BLOCK_INSNS]; /* label index / exit index      */
    nj_u1 fault_of[NJ_JIT_MAX_BLOCK_INSNS];  /* fault index, A64_NO_FAULT     */
    nj_u1 fault_completed[NJ_JIT_MAX_FAULTS];
    nj_u4 jump_insn[NJ_JIT_MAX_BLOCK_INSNS];
    nj_u4 jump_target[NJ_JIT_MAX_BLOCK_INSNS];
    int jump_delta[NJ_JIT_MAX_BLOCK_INSNS];
    nj_u1 jump_back[NJ_JIT_MAX_BLOCK_INSNS];
    unsigned n;
    unsigned njumps;
    nj_u4 start;
    nj_u4 end;
    int final_delta;
    int min_stack;
    int max_growth;
    int loop;
    int fp_used;
    unsigned budget_exit;
    nj_jit_block block;
} a64_scan;

enum {
    A64_BR_NONE = 0u,
    A64_BR_INTERNAL = 1u,
    A64_BR_EXIT = 2u,
    A64_BR_BACK = 3u,
    A64_NO_FAULT = 0xffu
};

static int a64_terminates(const a64_bytecode *b)
{
    return b->op == 0x6cu || b->op == 0x70u;
}

static int a64_is_fp(const a64_bytecode *b)
{
#if NJ_ENABLE_FLOAT && NJ_JIT_ARM64_FP
    switch (b->op) {
    case 0x62: case 0x66: case 0x6a: case 0x6e:
#if NJ_ENABLE_LONG
    case 0x63: case 0x67: case 0x6b: case 0x6f:
#endif
        return 1;
    default:
        return 0;
    }
#else
    (void)b;
    return 0;
#endif
}

/* Decode, measure and classify a window.  Analyze and compile share this
 * function, so their metadata can never disagree.  A branch target inside the
 * window must be an instruction boundary joining with the recorded depth;
 * anything else declines the whole block instead of emitting partial code. */
static int a64_scan_build(nj_vm *vm, const nj_method *m, nj_u4 pc, a64_scan *s)
{
    nj_u4 pos = pc;
    int depth = 0;
    int min_stack = 0;
    int max_growth = 0;
    unsigned n = 0u;
    unsigned njumps = 0u;
    unsigned nfaults = 0u;
    unsigned i;
    int loop = 0;

    memset(s, 0, sizeof(*s));
    s->start = pc;
    while (pos < m->code_len && n < NJ_JIT_MAX_BLOCK_INSNS) {
        a64_bytecode b;
        int delta, required, growth;

        if (!a64_decode(vm, m, pos, &b)) break;
        if (b.guarding && nfaults >= NJ_JIT_MAX_FAULTS) break;
        /* A throwing division may not follow mutations: the interpreter has to
         * observe the incoming stack when it re-executes the instruction. */
        if ((b.op == 0x6cu || b.op == 0x70u) && n != 0u) break;
        /* A forward branch must land on the instruction it was recorded with
         * the same operand depth, or the block is not a single-entry region. */
        for (i = 0u; i < njumps; ++i) {
            if (!s->jump_back[i] && s->jump_target[i] == pos &&
                s->jump_delta[i] != depth) {
                return 0;
            }
        }
        delta = depth + b.change;
        required = b.need - depth;
        if (required < min_stack) required = min_stack;
        growth = delta > max_growth ? delta : max_growth;
        if (required + growth > (int)m->max_stack) break;

        s->ins[n] = b;
        s->pcs[n] = pos;
        s->delta[n] = depth;
        if (b.branch != 0) {
            if (njumps >= NJ_JIT_MAX_BLOCK_INSNS) return 0;
            if (b.target < pos) {
                /* Only a loop back to the entry with an empty operand stack is
                 * bounded by the block's own budget check. */
                if (b.target != s->start || delta != 0) return 0;
                s->jump_back[njumps] = 1u;
                loop = 1;
            } else if (b.target == pos) {
                return 0; /* one-instruction infinite loop */
            } else {
                s->jump_back[njumps] = 0u;
            }
            s->jump_insn[njumps] = n;
            s->jump_target[njumps] = b.target;
            s->jump_delta[njumps] = delta;
            njumps++;
        }
        if (b.guarding) {
            s->fault_of[n] = (nj_u1)nfaults;
            s->fault_completed[nfaults] = (nj_u1)n;
            nfaults++;
        } else {
            s->fault_of[n] = A64_NO_FAULT;
        }
        if (a64_is_fp(&b)) s->fp_used = 1;
        n++;
        pos = (nj_u4)(pos + b.length);
        depth = delta;
        min_stack = required;
        max_growth = growth;
        if (a64_terminates(&b)) break;
    }
    if (n == 0u) return 0;

    s->n = n;
    s->end = pos;
    s->final_delta = depth;
    s->min_stack = min_stack;
    s->max_growth = max_growth;
    s->njumps = njumps;
    s->loop = loop;
    s->budget_exit = 0xffffu;

    /* exits[0] is the end of the straight-line window. */
    s->block.exits[0].pc_off = (nj_u2)(s->end - s->start);
    s->block.exits[0].sp_delta = (nj_s2)s->final_delta;
    s->block.nexits = 1u;

    for (i = 0u; i < njumps; ++i) {
        unsigned insn = s->jump_insn[i];
        if (s->jump_back[i]) {
            s->br_kind[insn] = (nj_u1)A64_BR_BACK;
            s->label[0] = 1u;
            continue;
        }
        if (s->jump_target[i] >= s->end) {
            int exit = nj_jit_record_exit(&s->block, s->start,
                                          s->jump_target[i], s->jump_delta[i]);
            if (exit < 0) return 0;
            s->br_kind[insn] = (nj_u1)A64_BR_EXIT;
            s->br_target[insn] = (nj_u2)exit;
        } else {
            unsigned k;
            int found = 0;
            for (k = 0u; k < n; ++k) {
                if (s->pcs[k] == s->jump_target[i]) {
                    s->label[k] = 1u;
                    s->br_kind[insn] = (nj_u1)A64_BR_INTERNAL;
                    s->br_target[insn] = (nj_u2)k;
                    found = 1;
                    break;
                }
            }
            if (!found) return 0; /* target is not an instruction boundary */
        }
    }
    if (loop) {
        int exit = nj_jit_record_exit(&s->block, s->start, s->start, 0);
        if (exit < 0) return 0;
        s->budget_exit = (unsigned)exit;
        s->label[0] = 1u;
    }
    for (i = 0u; i < n; ++i) {
        if (s->fault_of[i] != A64_NO_FAULT &&
            !nj_jit_record_fault(&s->block, s->start, s->pcs[i],
                                 s->delta[i])) {
            return 0;
        }
    }
    s->block.next_pc = s->end;
    s->block.insns = n;
    s->block.stack_delta = s->final_delta;
    s->block.min_stack = min_stack;
    s->block.max_growth = max_growth;
    s->block.nfaults = (nj_u1)nfaults;
    s->block.loop = (nj_u1)(loop ? 1u : 0u);
    s->block.size = 0u;
    return 1;
}

static int a64_analyze_impl(nj_vm *vm, const nj_method *m, nj_u4 pc,
                            nj_jit_block *out)
{
    a64_scan scan;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!m || !m->code || pc >= m->code_len ||
        sizeof(nj_value) != A64_SLOT_BYTES ||
        m->code_len - pc > 0xffffu) return 0;
    if (!a64_scan_build(vm, m, pc, &scan)) return 0;
    *out = scan.block;
    return 1;
}

int nj_arm64_analyze(nj_vm *vm, const nj_method *m, nj_u4 pc, nj_jit_block *out)
{
    return a64_analyze_impl(vm, m, pc, out);
}

/* ---- emission -------------------------------------------------------- */

/* Take the branch at instruction `insn`, popping its operands.  The completed-
 * bytecode counter is advanced before either path leaves the instruction; ADD
 * leaves the flags alone so it can sit between CMP and B.cond. */
static void a64_emit_branch(a64_writer *w, const a64_scan *s, unsigned insn)
{
    const a64_bytecode *b = &s->ins[insn];
    int top = s->delta[insn] * A64_SLOT_BYTES;
    unsigned kind, target, cond = 0u;

    if (s->br_kind[insn] == A64_BR_BACK) {
        kind = A64_FIX_LABEL;
        target = 0u;
    } else if (s->br_kind[insn] == A64_BR_INTERNAL) {
        kind = A64_FIX_LABEL;
        target = s->br_target[insn];
    } else {
        kind = A64_FIX_EXIT;
        target = s->br_target[insn];
    }

    if (b->branch == 2) { /* goto */
        if (w->counter) a64_inc_done(w);
        a64_branch(w, kind, target);
        return;
    }

    switch (b->op) {
    case 0x99: cond = 0u; break;  /* ifeq    */
    case 0x9a: cond = 1u; break;  /* ifne    */
    case 0x9b: cond = 11u; break; /* iflt    */
    case 0x9c: cond = 10u; break; /* ifge    */
    case 0x9d: cond = 12u; break; /* ifgt    */
    case 0x9e: cond = 13u; break; /* ifle    */
    case 0x9f: cond = 0u; break;  /* if_icmpeq */
    case 0xa0: cond = 1u; break;  /* if_icmpne */
    case 0xa1: cond = 11u; break; /* if_icmplt */
    case 0xa2: cond = 10u; break; /* if_icmpge */
    case 0xa3: cond = 12u; break; /* if_icmpgt */
    case 0xa4: cond = 13u; break; /* if_icmple */
    case 0xa5: cond = 0u; break;  /* if_acmpeq */
    case 0xa6: cond = 1u; break;  /* if_acmpne */
    case 0xc6: cond = 0u; break;  /* ifnull    */
    case 0xc7: cond = 1u; break;  /* ifnonnull */
    default: w->failed = 1; return;
    }

    if (b->op == 0xa5u || b->op == 0xa6u) {
        /* References are full 64-bit values under LP64. */
        a64_load(w, A64_T0, A64_SP_ARG, top - 16, 1);
        a64_load(w, A64_T1, A64_SP_ARG, top - 8, 1);
        a64_cmp_reg64(w, A64_T0, A64_T1);
    } else if (b->op == 0xc6u || b->op == 0xc7u) {
        a64_load(w, A64_T0, A64_SP_ARG, top - 8, 1);
        a64_cmp_reg64(w, A64_T0, A64_ZERO);
    } else if (b->need == 2) {
        a64_load(w, A64_T0, A64_SP_ARG, top - 16, 0);
        a64_load(w, A64_T1, A64_SP_ARG, top - 8, 0);
        a64_cmp_reg(w, A64_T0, A64_T1);
    } else {
        a64_load(w, A64_T0, A64_SP_ARG, top - 8, 0);
        a64_cmp_imm(w, A64_T0, 0u);
    }
    if (w->counter) a64_inc_done(w);
    a64_branch_cond(w, cond, kind, target);
}

static int a64_emit_block(a64_writer *w, const a64_scan *s, nj_size capacity)
{
    const nj_jit_block *block = &s->block;
    nj_size label_at[NJ_JIT_MAX_BLOCK_INSNS];
    nj_size exit_at[NJ_JIT_MAX_EXITS];
    nj_size fault_at[NJ_JIT_MAX_FAULTS];
    nj_size epilogue_at = A64_NO_LABEL;
    nj_size reserve;
    unsigned i;
    int counter = s->njumps != 0u;
    int fp = s->fp_used;

    for (i = 0u; i < (unsigned)NJ_JIT_MAX_BLOCK_INSNS; ++i)
        label_at[i] = A64_NO_LABEL;

    /* Stub bytes are budgeted up front so the instruction stream can never
     * crowd out the exits it branches to.  The bound is exact: exit stubs are
     * 8 bytes (plus 4 for the ADD on a counter-based non-zero exit), fault
     * stubs are 12 or 16 bytes, and the shared FPCR epilogue is 8.  An exact
     * bound lets a block compile at a capacity equal to its own size. */
    reserve = 8u * (nj_size)block->nexits;
    if (counter && block->nexits > 1u)
        reserve += 4u * (nj_size)(block->nexits - 1u);
    reserve += (counter ? 16u : 12u) * (nj_size)block->nfaults;
    if (fp) reserve += 8u;
    if (capacity < reserve + 4u) return 0;
    w->capacity = capacity - reserve;
    w->counter = counter;

    /* Indirect calls may target a BTI-guarded executable page. This landing
     * pad is a backwards-compatible HINT on cores without FEAT_BTI. */
    a64_u32(w, 0xd503245fu); /* bti c */
    if (fp) a64_fp_save(w);
    if (counter) a64_li32(w, A64_DONE, 0u); /* completed = 0 */

    for (i = 0u; i < s->n; ++i) {
        if (i == 0u && s->loop != 0) {
            label_at[0] = w->used;
            /* remaining = budget - completed; enter another iteration only
             * when a whole window still fits. */
            a64_r3(w, 0x4b000000u, A64_T3, A64_BUDGET_ARG, A64_DONE);
            a64_li32(w, A64_T4, (nj_u4)s->n);
            a64_cmp_reg(w, A64_T3, A64_T4);
            a64_branch_cond(w, 3u, A64_FIX_EXIT, s->budget_exit); /* b.lo */
        } else if (s->label[i]) {
            label_at[i] = w->used;
        }
        if (s->ins[i].branch != 0) {
            a64_emit_branch(w, s, i);
        } else {
            unsigned fault = s->fault_of[i] == A64_NO_FAULT ?
                             0u : (unsigned)s->fault_of[i];
            a64_emit(w, &s->ins[i], s->delta[i], fault);
            if (counter) a64_inc_done(w);
        }
        if (w->failed) return 0;
    }

    w->capacity = capacity;

    /* Success/exit stubs: w0 = (completed << 4) | exit_index. */
    for (i = 0u; i < (unsigned)block->nexits; ++i) {
        exit_at[i] = w->used;
        if (counter) {
            /* lsl w0,w3,#4 */
            a64_u32(w, 0x531c6c60u);
            if (i != 0u) {
                a64_u32(w, 0x11000000u | (i << 10)); /* add w0,w0,#i */
            }
        } else {
            a64_li32(w, A64_SP_ARG,
                     ((nj_u4)block->insns << NJ_JIT_EXIT_SHIFT) | (nj_u4)i);
        }
        if (fp)
            a64_branch(w, A64_FIX_EPILOGUE, 0u);
        else
            a64_u32(w, 0xd65f03c0u); /* ret x30 */
    }
    /* Fault stubs: w0 = fault word carrying the live completed count. */
    for (i = 0u; i < (unsigned)block->nfaults; ++i) {
        fault_at[i] = w->used;
        if (counter) {
            a64_li32(w, A64_SP_ARG,
                     NJ_JIT_FAULT_BIT |
                     ((nj_u4)i << NJ_JIT_FAULT_INDEX_SHIFT));
            a64_u32(w, 0x2a030000u); /* orr w0,w0,w3 */
        } else {
            a64_li32(w, A64_SP_ARG,
                     a64_fault_word(i, (nj_u4)s->fault_completed[i]));
        }
        if (fp)
            a64_branch(w, A64_FIX_EPILOGUE, 0u);
        else
            a64_u32(w, 0xd65f03c0u); /* ret x30 */
    }
    if (fp) {
        epilogue_at = w->used;
        a64_fp_restore(w);
        a64_u32(w, 0xd65f03c0u); /* ret x30 */
    }
    if (w->failed) return 0;

    for (i = 0u; i < w->nfixups; ++i) {
        const a64_fixup *f = &w->fixups[i];
        nj_size target;
        if (f->kind == A64_FIX_FAULT) {
            if (f->target >= block->nfaults) return 0;
            target = fault_at[f->target];
        } else if (f->kind == A64_FIX_EXIT) {
            if (f->target >= block->nexits) return 0;
            target = exit_at[f->target];
        } else if (f->kind == A64_FIX_LABEL) {
            if (f->target >= s->n || label_at[f->target] == A64_NO_LABEL)
                return 0;
            target = label_at[f->target];
        } else if (f->kind == A64_FIX_EPILOGUE) {
            if (epilogue_at == A64_NO_LABEL) return 0;
            target = epilogue_at;
        } else {
            return 0;
        }
        a64_patch_branch(w->buf, f, target);
    }
    if (w->used > capacity) return 0;
    return 1;
}

int nj_arm64_compile(nj_vm *vm, const nj_method *m, nj_u4 pc, nj_u1 *buf,
                     nj_size capacity, nj_jit_block *out)
{
    a64_scan scan;
    a64_writer w;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!m || !m->code || !buf || pc >= m->code_len ||
        sizeof(nj_value) != A64_SLOT_BYTES ||
        m->code_len - pc > 0xffffu) return 0;
    if (capacity > NJ_JIT_MAX_CODE_BYTES) capacity = NJ_JIT_MAX_CODE_BYTES;
    if (!a64_scan_build(vm, m, pc, &scan)) return 0;
    memset(&w, 0, sizeof(w));
    w.buf = buf;
    w.used = 0u;
    w.capacity = capacity;
    w.failed = 0;
    w.nfixups = 0u;
    if (!a64_emit_block(&w, &scan, capacity)) return 0;
    if (w.used > capacity || w.failed) return 0;
    scan.block.size = w.used;
    *out = scan.block;
    return 1;
}
#endif /* NJ_ENABLE_JIT */
