/*
 * Bounded x86-64 (System V AMD64) baseline blocks with guard faults, forward
 * branches and bounded loops.
 *
 * A generated block has the ABI
 *     nj_u4 block(nj_value *incoming_sp, nj_value *locals, nj_u4 budget)
 * i.e. rdi = incoming stack pointer, rsi = locals, edx = budget, result in eax.
 * It uses only rdi/rsi (arguments, unchanged while Java bytecodes execute),
 * edx (budget, read only), r10d (completed counter, only when a block takes a
 * branch), rax/rcx/r8/r9/r11 (scratch) and xmm0/xmm1 (float ALU).  It never
 * changes the native stack, a callee-saved register, or a Java frame.
 *
 * A block returns a success word
 *     (completed << NJ_JIT_EXIT_SHIFT) | exit_index
 * where completed is the number of finished bytecodes and exit_index selects
 * block.exits[]: 0 is the end of the straight-line window, other entries are
 * taken forward branches that leave the window and the loop budget exit.  A
 * guard that can fail at run time (null receiver, array type/length/flags,
 * zero divisor, INT_MIN/-1 division) branches to a per-fault exit that returns
 *     NJ_JIT_FAULT_BIT | (fault_index << 24) | completed
 * before touching memory, so the C caller can rewind pc/sp and let the
 * interpreter re-execute exactly that instruction and raise the Java
 * exception with the interpreter's own PC.  See nj_jit_internal.h.
 *
 * The encoder writes bytes explicitly (little-endian x86 instruction words),
 * never through host instruction pointers.  All compilation state is local to
 * the invocation.
 */
#include "nj_jit_internal.h"
#include <stdio.h>
#include <stdlib.h>

#if NJ_ENABLE_JIT

enum {
    X64_SP_ARG = 7,       /* rdi: incoming operand stack pointer */
    X64_LOCALS_ARG = 6,   /* rsi: locals array                    */
    X64_BUDGET_ARG = 2,   /* edx: budget                          */
    X64_DONE = 10,        /* r10d: completed counter              */
    X64_RAX = 0, X64_RCX = 1, X64_RDX = 2, X64_R8 = 8, X64_R9 = 9,
    X64_SLOT_BYTES = 8,
    /* Header offsets of nj_object.  Instance field payloads follow it. */
    X64_OBJ_DATA = 8, X64_OBJ_LENGTH = 16,
    X64_OBJ_KIND = 24, X64_OBJ_AFLAGS = 26
};

enum {
    X64_FIX_FAULT = 0u,   /* target is a fault-exit index */
    X64_FIX_EXIT = 1u,    /* target is a block-exit index */
    X64_FIX_LABEL = 2u    /* target is an instruction (label) index */
};

#define X64_MAX_FIXUPS 192u
#define X64_NO_LABEL ((nj_size)(-1))

typedef struct x64_fixup {
    nj_size at;      /* offset of the rel32 field inside the buffer */
    nj_u1 kind;      /* X64_FIX_* */
    nj_u2 target;    /* fault index, exit index or instruction index */
} x64_fixup;

typedef struct x64_writer {
    nj_u1 *buf;
    nj_size used;
    nj_size capacity;
    int failed;
    int counter;      /* track completed bytecodes in r10d */
    unsigned nfixups;
    x64_fixup fixups[X64_MAX_FIXUPS];
} x64_writer;

typedef struct x64_bytecode {
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
} x64_bytecode;

/* ---- encoder primitives ---------------------------------------------- */

static void x64_u8(x64_writer *w, unsigned byte)
{
    if (w->failed) return;
    if (w->used >= w->capacity) {
        w->failed = 1;
        return;
    }
    w->buf[w->used++] = (nj_u1)byte;
}

static void x64_u32(x64_writer *w, nj_u4 word)
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

static void x64_u64(x64_writer *w, nj_u8 value)
{
    x64_u32(w, (nj_u4)value);
    x64_u32(w, (nj_u4)(value >> 32));
}

/* REX prefix for reg/base forms.  Only bit 0 (B) and bit 2 (R) are used here. */
static void x64_rex(x64_writer *w, int wide, unsigned reg, unsigned base)
{
    unsigned rex = 0x40u;
    if (wide) rex |= 8u;
    if (reg >= 8u) rex |= 4u;
    if (base >= 8u) rex |= 1u;
    if (rex != 0x40u) x64_u8(w, rex);
}

/* REX with an SIB index register (always rcx in this encoder). */
static void x64_rex_sib(x64_writer *w, unsigned reg, unsigned index,
                        unsigned base)
{
    unsigned rex = 0x40u;
    if (reg >= 8u) rex |= 4u;
    if (index >= 8u) rex |= 2u;
    if (base >= 8u) rex |= 1u;
    if (rex != 0x40u) x64_u8(w, rex);
}

/* ModRM with mod=10 (disp32) followed by the displacement.  base must not be
 * rsp/r12: every frame slot is addressed from rdi, rsi, rax, rcx, r8 or r9. */
static void x64_modrm_disp32(x64_writer *w, unsigned reg, unsigned base,
                             int disp)
{
    x64_u8(w, 0x80u | ((reg & 7u) << 3) | (base & 7u));
    x64_u32(w, (nj_u4)disp);
}

static void x64_ld32(x64_writer *w, unsigned dst, unsigned base, int disp)
{
    x64_rex(w, 0, dst, base);
    x64_u8(w, 0x8bu);
    x64_modrm_disp32(w, dst, base, disp);
}

static void x64_st32(x64_writer *w, unsigned src, unsigned base, int disp)
{
    x64_rex(w, 0, src, base);
    x64_u8(w, 0x89u);
    x64_modrm_disp32(w, src, base, disp);
}

static void x64_ld64(x64_writer *w, unsigned dst, unsigned base, int disp)
{
    x64_rex(w, 1, dst, base);
    x64_u8(w, 0x8bu);
    x64_modrm_disp32(w, dst, base, disp);
}

static void x64_st64(x64_writer *w, unsigned src, unsigned base, int disp)
{
    x64_rex(w, 1, src, base);
    x64_u8(w, 0x89u);
    x64_modrm_disp32(w, src, base, disp);
}

static void x64_ldz8(x64_writer *w, unsigned dst, unsigned base, int disp)
{
    x64_rex(w, 0, dst, base);
    x64_u8(w, 0x0fu);
    x64_u8(w, 0xb6u);
    x64_modrm_disp32(w, dst, base, disp);
}

static void x64_lds8(x64_writer *w, unsigned dst, unsigned base, int disp)
{
    x64_rex(w, 0, dst, base);
    x64_u8(w, 0x0fu);
    x64_u8(w, 0xbeu);
    x64_modrm_disp32(w, dst, base, disp);
}

static void x64_ldz16(x64_writer *w, unsigned dst, unsigned base, int disp)
{
    x64_rex(w, 0, dst, base);
    x64_u8(w, 0x0fu);
    x64_u8(w, 0xb7u);
    x64_modrm_disp32(w, dst, base, disp);
}

static void x64_lds16(x64_writer *w, unsigned dst, unsigned base, int disp)
{
    x64_rex(w, 0, dst, base);
    x64_u8(w, 0x0fu);
    x64_u8(w, 0xbfu);
    x64_modrm_disp32(w, dst, base, disp);
}

static void x64_st8(x64_writer *w, unsigned src, unsigned base, int disp)
{
    x64_rex(w, 0, src, base);
    x64_u8(w, 0x88u);
    x64_modrm_disp32(w, src, base, disp);
}

static void x64_st16(x64_writer *w, unsigned src, unsigned base, int disp)
{
    x64_u8(w, 0x66u);
    x64_rex(w, 0, src, base);
    x64_u8(w, 0x89u);
    x64_modrm_disp32(w, src, base, disp);
}

/* Load/store a 4-byte element from [base + index*4 + disp].  index is rcx and
 * stays below r8 in every use, so only REX.B/R are needed. */
static void x64_ld32_sib(x64_writer *w, unsigned dst, unsigned base,
                         unsigned index, int disp)
{
    x64_rex_sib(w, dst, index, base);
    x64_u8(w, 0x8bu);
    x64_u8(w, 0x80u | ((dst & 7u) << 3) | 0x04u);
    x64_u8(w, (2u << 6) | ((index & 7u) << 3) | (base & 7u));
    x64_u32(w, (nj_u4)disp);
}

static void x64_st32_sib(x64_writer *w, unsigned src, unsigned base,
                         unsigned index, int disp)
{
    x64_rex_sib(w, src, index, base);
    x64_u8(w, 0x89u);
    x64_u8(w, 0x80u | ((src & 7u) << 3) | 0x04u);
    x64_u8(w, (2u << 6) | ((index & 7u) << 3) | (base & 7u));
    x64_u32(w, (nj_u4)disp);
}

static void x64_mov32_imm(x64_writer *w, unsigned reg, nj_u4 imm)
{
    if (reg >= 8u) x64_u8(w, 0x41u);
    x64_u8(w, 0xb8u + (reg & 7u));
    x64_u32(w, imm);
}

static void x64_mov64_imm(x64_writer *w, unsigned reg, nj_u8 imm)
{
    x64_u8(w, (reg >= 8u) ? 0x49u : 0x48u);
    x64_u8(w, 0xb8u + (reg & 7u));
    x64_u32(w, (nj_u4)imm);
    x64_u32(w, (nj_u4)(imm >> 32));
}

static void x64_mov32_reg(x64_writer *w, unsigned dst, unsigned src)
{
    x64_rex(w, 0, src, dst);
    x64_u8(w, 0x89u);
    x64_u8(w, 0xc0u | ((src & 7u) << 3) | (dst & 7u));
}

static void x64_arith32(x64_writer *w, unsigned opcode, unsigned dst,
                        unsigned src)
{
    x64_rex(w, 0, src, dst);
    x64_u8(w, opcode);
    x64_u8(w, 0xc0u | ((src & 7u) << 3) | (dst & 7u));
}

static void x64_imul32(x64_writer *w, unsigned dst, unsigned src)
{
    x64_rex(w, 0, dst, src);
    x64_u8(w, 0x0fu);
    x64_u8(w, 0xafu);
    x64_u8(w, 0xc0u | ((dst & 7u) << 3) | (src & 7u));
}

static void x64_shift32_cl(x64_writer *w, unsigned dst, unsigned ext)
{
    x64_rex(w, 0, 0, dst);
    x64_u8(w, 0xd3u);
    x64_u8(w, 0xc0u | ((ext & 7u) << 3) | (dst & 7u));
}

static void x64_neg32(x64_writer *w, unsigned reg)
{
    x64_rex(w, 0, 0, reg);
    x64_u8(w, 0xf7u);
    x64_u8(w, 0xc0u | (3u << 3) | (reg & 7u));
}

static void x64_idiv32(x64_writer *w, unsigned reg)
{
    x64_rex(w, 0, 0, reg);
    x64_u8(w, 0xf7u);
    x64_u8(w, 0xc0u | (7u << 3) | (reg & 7u));
}

static void x64_cdq(x64_writer *w)
{
    x64_u8(w, 0x99u);
}

static void x64_inc32(x64_writer *w, unsigned reg)
{
    x64_rex(w, 0, 0, reg);
    x64_u8(w, 0xffu);
    x64_u8(w, 0xc0u | (0u << 3) | (reg & 7u));
}

static void x64_xor32(x64_writer *w, unsigned dst, unsigned src)
{
    x64_arith32(w, 0x31u, dst, src);
}

static void x64_cmp32_imm(x64_writer *w, unsigned reg, nj_s4 imm)
{
    x64_rex(w, 0, 0, reg);
    x64_u8(w, 0x81u);
    x64_u8(w, 0xc0u | (7u << 3) | (reg & 7u));
    x64_u32(w, (nj_u4)imm);
}

static void x64_cmp32_reg(x64_writer *w, unsigned a, unsigned b)
{
    x64_rex(w, 0, b, a);
    x64_u8(w, 0x39u);
    x64_u8(w, 0xc0u | ((b & 7u) << 3) | (a & 7u));
}

static void x64_cmp64_reg(x64_writer *w, unsigned a, unsigned b)
{
    x64_rex(w, 1, b, a);
    x64_u8(w, 0x39u);
    x64_u8(w, 0xc0u | ((b & 7u) << 3) | (a & 7u));
}

static void x64_test32(x64_writer *w, unsigned reg)
{
    x64_rex(w, 0, reg, reg);
    x64_u8(w, 0x85u);
    x64_u8(w, 0xc0u | ((reg & 7u) << 3) | (reg & 7u));
}

static void x64_test64(x64_writer *w, unsigned reg)
{
    x64_rex(w, 1, reg, reg);
    x64_u8(w, 0x85u);
    x64_u8(w, 0xc0u | ((reg & 7u) << 3) | (reg & 7u));
}

/* Scalar SSE load/store of the low lane (xmm0) and xmm0 op xmm1.  F3 is the
 * single-precision prefix, F2 the double-precision prefix. */
static void x64_ss_load(x64_writer *w, int is_double, unsigned reg,
                        unsigned base, int disp)
{
    x64_u8(w, is_double ? 0xf2u : 0xf3u);
    x64_rex(w, 0, 0, base);
    x64_u8(w, 0x0fu);
    x64_u8(w, 0x10u);
    x64_modrm_disp32(w, reg, base, disp);
}

static void x64_ss_store(x64_writer *w, int is_double, unsigned base, int disp)
{
    x64_u8(w, is_double ? 0xf2u : 0xf3u);
    x64_rex(w, 0, 0, base);
    x64_u8(w, 0x0fu);
    x64_u8(w, 0x11u);
    x64_modrm_disp32(w, 0u, base, disp);
}

static void x64_ss_arith(x64_writer *w, int is_double, unsigned opcode)
{
    x64_u8(w, is_double ? 0xf2u : 0xf3u);
    x64_u8(w, 0x0fu);
    x64_u8(w, opcode);
    x64_u8(w, 0xc1u); /* xmm0, xmm1 */
}

/* ---- guard branches, block branches and fault exits ------------------- */

static void x64_fixup_add(x64_writer *w, nj_size at, unsigned kind,
                          unsigned target)
{
    if (w->failed) return;
    if (w->nfixups >= X64_MAX_FIXUPS) {
        w->failed = 1;
        return;
    }
    w->fixups[w->nfixups].at = at;
    w->fixups[w->nfixups].kind = (nj_u1)kind;
    w->fixups[w->nfixups].target = (nj_u2)target;
    w->nfixups++;
}

/* Conditional branch with a rel32 fixup slot.  cc is the x86 condition code
 * (4 = z, 5 = nz, 2 = c/b, 3 = nc, 0xc = l, 0xd = ge, 0xe = le, 0xf = g). */
static void x64_jcc(x64_writer *w, unsigned cc, unsigned kind,
                    unsigned target)
{
    if (w->failed) return;
    x64_u8(w, 0x0fu);
    x64_u8(w, 0x80u | (cc & 0x0fu));
    x64_fixup_add(w, w->used, kind, target);
    x64_u32(w, 0u);
}

static void x64_jmp(x64_writer *w, unsigned kind, unsigned target)
{
    if (w->failed) return;
    x64_u8(w, 0xe9u);
    x64_fixup_add(w, w->used, kind, target);
    x64_u32(w, 0u);
}

static void x64_ret(x64_writer *w)
{
    x64_u8(w, 0xc3u);
}

static void x64_patch_rel32(nj_u1 *buf, const x64_fixup *f, nj_size target)
{
    nj_u4 word = (nj_u4)(nj_s4)(target - (f->at + 4u));
    buf[f->at] = (nj_u1)word;
    buf[f->at + 1u] = (nj_u1)(word >> 8);
    buf[f->at + 2u] = (nj_u1)(word >> 16);
    buf[f->at + 3u] = (nj_u1)(word >> 24);
}

static nj_u4 x64_fault_word(unsigned index, nj_u4 completed)
{
    return NJ_JIT_FAULT_BIT |
           ((nj_u4)(index & NJ_JIT_FAULT_INDEX_MASK) <<
            NJ_JIT_FAULT_INDEX_SHIFT) |
           (completed & NJ_JIT_FAULT_COMPLETED_MASK);
}

static void x64_guard64(x64_writer *w, unsigned reg, unsigned fault)
{
    x64_test64(w, reg);
    x64_jcc(w, 4u, X64_FIX_FAULT, fault);
}

static void x64_guard32(x64_writer *w, unsigned reg, unsigned fault)
{
    x64_test32(w, reg);
    x64_jcc(w, 4u, X64_FIX_FAULT, fault);
}

static void x64_guard_eq32(x64_writer *w, unsigned reg, nj_s4 imm,
                           unsigned fault)
{
    x64_cmp32_imm(w, reg, imm);
    x64_jcc(w, 5u, X64_FIX_FAULT, fault);
}

static void x64_guard_ae32(x64_writer *w, unsigned a, unsigned b,
                           unsigned fault)
{
    x64_cmp32_reg(w, a, b);
    x64_jcc(w, 3u, X64_FIX_FAULT, fault);
}

static int x64_s8(unsigned value)
{
    return value < 128u ? (int)value : (int)value - 256;
}

static int x64_s16(unsigned value)
{
    return value < 32768u ? (int)value : (int)value - 65536;
}

/* Access width/sign for an instance field.  LP64 nj_ref is eight bytes (the
 * class linker lays OBJECT fields out with sizeof(nj_ref) and the interpreter
 * copies sizeof(nj_ref) bytes), so OBJECT uses a doubleword load/store here,
 * exactly like LONG/DOUBLE.  Returned size codes: 0=byte, 1=half, 2=word,
 * 3=doubleword. */
static int x64_field_size(const nj_field *f, int *sign)
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

static int x64_decode(nj_vm *vm, const nj_method *m, nj_u4 pc,
                      x64_bytecode *b)
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
        b->immediate = x64_s8(code[1]);
        b->change = 1;
        return 1;
    case 0x11:
        if (left < 3u) return 0;
        b->length = 3u;
        b->immediate = x64_s16(((unsigned)code[1] << 8) | code[2]);
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
        b->immediate = wide ? x64_s16(((unsigned)code[4] << 8) | code[5]) :
                               x64_s8(code[2]);
        return 1;
    case 0x99: case 0x9a: case 0x9b: case 0x9c:
    case 0x9d: case 0x9e: /* if<cond>: pop 1, signed compare against zero */
        b->length = 3u;
        if (left < 3u) return 0;
        b->immediate = x64_s16(((unsigned)code[1] << 8) | code[2]);
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
        b->immediate = x64_s16(((unsigned)code[1] << 8) | code[2]);
        if (pc + (nj_u4)b->immediate >= m->code_len) return 0;
        b->need = 2;
        b->change = -2;
        b->branch = 1;
        b->target = pc + (nj_u4)b->immediate;
        return 1;
    case 0xa5: case 0xa6: /* if_acmpeq/ne: full 64-bit references */
        b->length = 3u;
        if (left < 3u) return 0;
        b->immediate = x64_s16(((unsigned)code[1] << 8) | code[2]);
        if (pc + (nj_u4)b->immediate >= m->code_len) return 0;
        b->need = 2;
        b->change = -2;
        b->branch = 1;
        b->target = pc + (nj_u4)b->immediate;
        return 1;
    case 0xa7: /* goto */
        b->length = 3u;
        if (left < 3u) return 0;
        b->immediate = x64_s16(((unsigned)code[1] << 8) | code[2]);
        if (pc + (nj_u4)b->immediate >= m->code_len) return 0;
        b->branch = 2;
        b->target = pc + (nj_u4)b->immediate;
        return 1;
    case 0xc6: case 0xc7: /* ifnull / ifnonnull */
        b->length = 3u;
        if (left < 3u) return 0;
        b->immediate = x64_s16(((unsigned)code[1] << 8) | code[2]);
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
        if (x64_field_size(f, &slots) < 0) return 0;
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
#if NJ_ENABLE_FLOAT
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

static void x64_emit(x64_writer *w, const x64_bytecode *b, int delta,
                     unsigned fault)
{
    unsigned op = b->op;
    int top = delta * X64_SLOT_BYTES;
    int local = (int)b->local * X64_SLOT_BYTES;

    if (op >= 0x15u && op <= 0x19u) {          /* iload..aload */
        if (b->change == 2) {                  /* lload/dload: two slots */
            x64_ld64(w, X64_RAX, X64_LOCALS_ARG, local);
            x64_st64(w, X64_RAX, X64_SP_ARG, top);
            x64_mov32_imm(w, X64_RAX, 0u);
            x64_st32(w, X64_RAX, X64_SP_ARG, top + 8);
        } else if (op == 0x19u) {              /* aload: LP64 reference */
            x64_ld64(w, X64_RAX, X64_LOCALS_ARG, local);
            x64_st64(w, X64_RAX, X64_SP_ARG, top);
        } else {                               /* int/float: low word */
            x64_ld32(w, X64_RAX, X64_LOCALS_ARG, local);
            x64_st32(w, X64_RAX, X64_SP_ARG, top);
        }
        return;
    }
    if (op >= 0x36u && op <= 0x3au) {          /* istore..astore */
        if (b->need == 2) {
            x64_ld64(w, X64_RAX, X64_SP_ARG, top - 16);
            x64_st64(w, X64_RAX, X64_LOCALS_ARG, local);
            x64_mov32_imm(w, X64_RAX, 0u);
            x64_st32(w, X64_RAX, X64_LOCALS_ARG, local + 8);
        } else if (op == 0x3au) {
            x64_ld64(w, X64_RAX, X64_SP_ARG, top - 8);
            x64_st64(w, X64_RAX, X64_LOCALS_ARG, local);
        } else {
            x64_ld32(w, X64_RAX, X64_SP_ARG, top - 8);
            x64_st32(w, X64_RAX, X64_LOCALS_ARG, local);
        }
        return;
    }

    switch (op) {
    case 0x00: case 0x57: case 0x58:
        return; /* pc/sp changes are committed by the C caller. */
    case 0x01: /* aconst_null writes an entire LP64 reference */
        x64_xor32(w, X64_RAX, X64_RAX);
        x64_st64(w, X64_RAX, X64_SP_ARG, top);
        return;
    case 0x02: case 0x03: case 0x04: case 0x05:
    case 0x06: case 0x07: case 0x08: case 0x0b: case 0x0c: case 0x0d:
    case 0x10: case 0x11: case 0x12: case 0x13:
        x64_mov32_imm(w, X64_RAX, (nj_u4)b->immediate);
        x64_st32(w, X64_RAX, X64_SP_ARG, top);
        return;
    case 0x09: case 0x0a: { /* lconst_0/1 */
        x64_mov32_imm(w, X64_RAX, (nj_u4)b->immediate); /* zero-extends */
        x64_st64(w, X64_RAX, X64_SP_ARG, top);
        x64_mov32_imm(w, X64_RAX, 0u);
        x64_st32(w, X64_RAX, X64_SP_ARG, top + 8);
        return;
    }
    case 0x0e: case 0x0f: { /* dconst_0/1: the immediate is the high word */
        nj_u8 bits = (nj_u8)(nj_u4)b->immediate << 48;
        x64_mov64_imm(w, X64_RAX, bits);
        x64_st64(w, X64_RAX, X64_SP_ARG, top);
        x64_mov32_imm(w, X64_RAX, 0u);
        x64_st32(w, X64_RAX, X64_SP_ARG, top + 8);
        return;
    }
    case 0x59: /* dup */
        x64_ld64(w, X64_RAX, X64_SP_ARG, top - 8);
        x64_st64(w, X64_RAX, X64_SP_ARG, top);
        return;
    case 0x5c: /* dup2 */
        x64_ld64(w, X64_RAX, X64_SP_ARG, top - 16);
        x64_st64(w, X64_RAX, X64_SP_ARG, top);
        x64_ld64(w, X64_RAX, X64_SP_ARG, top - 8);
        x64_st64(w, X64_RAX, X64_SP_ARG, top + 8);
        return;
    case 0x5f: /* swap */
        x64_ld64(w, X64_RAX, X64_SP_ARG, top - 8);
        x64_ld64(w, X64_RCX, X64_SP_ARG, top - 16);
        x64_st64(w, X64_RCX, X64_SP_ARG, top - 8);
        x64_st64(w, X64_RAX, X64_SP_ARG, top - 16);
        return;
    case 0x84: { /* iinc: local += immediate */
        nj_s4 imm = (nj_s4)b->immediate;
        x64_ld32(w, X64_RAX, X64_LOCALS_ARG, local);
        if (imm >= -128 && imm <= 127) {
            unsigned mag = (unsigned)(imm < 0 ? -imm : imm);
            /* add/sub r32, imm8: 83 /0 or 83 /5 */
            x64_rex(w, 0, 0, X64_RAX);
            x64_u8(w, 0x83u);
            x64_u8(w, 0xc0u | ((imm < 0 ? 5u : 0u) << 3) | X64_RAX);
            x64_u8(w, mag & 0xffu);
        } else {
            x64_mov32_imm(w, X64_RCX, (nj_u4)imm);
            x64_arith32(w, imm < 0 ? 0x29u : 0x01u, X64_RAX, X64_RCX);
        }
        x64_st32(w, X64_RAX, X64_LOCALS_ARG, local);
        return;
    }
    case 0x74: /* ineg */
        x64_ld32(w, X64_RAX, X64_SP_ARG, top - 8);
        x64_neg32(w, X64_RAX);
        x64_st32(w, X64_RAX, X64_SP_ARG, top - 8);
        return;
    case 0x91: /* i2b: sign-extend the low byte */
        x64_ld32(w, X64_RAX, X64_SP_ARG, top - 8);
        x64_u8(w, 0x0fu); x64_u8(w, 0xbeu); x64_u8(w, 0xc0u); /* movsx eax,al */
        x64_st32(w, X64_RAX, X64_SP_ARG, top - 8);
        return;
    case 0x92: /* i2c: zero-extend the low halfword */
        x64_ld32(w, X64_RAX, X64_SP_ARG, top - 8);
        x64_u8(w, 0x0fu); x64_u8(w, 0xb7u); x64_u8(w, 0xc0u); /* movzx eax,ax */
        x64_st32(w, X64_RAX, X64_SP_ARG, top - 8);
        return;
    case 0x93: /* i2s: sign-extend the low halfword */
        x64_ld32(w, X64_RAX, X64_SP_ARG, top - 8);
        x64_u8(w, 0x0fu); x64_u8(w, 0xbfu); x64_u8(w, 0xc0u); /* movsx eax,ax */
        x64_st32(w, X64_RAX, X64_SP_ARG, top - 8);
        return;
    case 0x6c: case 0x70: { /* idiv/irem.
                             * x86 idiv raises #DE for INT_MIN/-1, which the
                             * Java result wraps, so a divisor of -1 faults to
                             * the interpreter instead of being executed.  The
                             * instruction is terminal (a64/x64 scan rules) and
                             * edx is dead after it, so cdq may use the budget
                             * register. */
        int is_remainder = op == 0x70u;
        x64_ld32(w, X64_RCX, X64_SP_ARG, top - 8); /* divisor */
        x64_test32(w, X64_RCX);
        x64_jcc(w, 4u, X64_FIX_FAULT, fault);      /* divisor == 0 */
        x64_cmp32_imm(w, X64_RCX, -1);
        x64_jcc(w, 4u, X64_FIX_FAULT, fault);      /* divisor == -1 */
        x64_ld32(w, X64_RAX, X64_SP_ARG, top - 16);/* dividend */
        x64_cdq(w);
        x64_idiv32(w, X64_RCX);
        if (is_remainder) x64_mov32_reg(w, X64_RAX, X64_RDX);
        x64_st32(w, X64_RAX, X64_SP_ARG, top - 16);
        return;
    }
    case 0x2e: { /* iaload: value, then index, then array */
        x64_ld64(w, X64_R8, X64_SP_ARG, top - 16);
        x64_guard64(w, X64_R8, fault);
        x64_ld32(w, X64_RCX, X64_SP_ARG, top - 8);    /* index */
        x64_ldz16(w, X64_RAX, X64_R8, X64_OBJ_KIND);
        x64_guard_eq32(w, X64_RAX, (nj_s4)NJ_OBJ_ARRAY, fault);
        x64_ld32(w, X64_RAX, X64_R8, X64_OBJ_LENGTH);
        x64_guard_ae32(w, X64_RCX, X64_RAX, fault);
        x64_ldz16(w, X64_RAX, X64_R8, X64_OBJ_AFLAGS);
        x64_guard_eq32(w, X64_RAX, (nj_s4)NJ_ARRAY_T_INT, fault);
        x64_ld64(w, X64_R9, X64_R8, X64_OBJ_DATA);
        x64_ld32_sib(w, X64_RAX, X64_R9, X64_RCX, 0);
        x64_st32(w, X64_RAX, X64_SP_ARG, top - 16);
        return;
    }
    case 0x4f: { /* iastore: value, index, array */
        x64_ld64(w, X64_R8, X64_SP_ARG, top - 24);
        x64_guard64(w, X64_R8, fault);
        x64_ld32(w, X64_RCX, X64_SP_ARG, top - 16);   /* index */
        x64_ldz16(w, X64_RAX, X64_R8, X64_OBJ_KIND);
        x64_guard_eq32(w, X64_RAX, (nj_s4)NJ_OBJ_ARRAY, fault);
        x64_ld32(w, X64_RAX, X64_R8, X64_OBJ_LENGTH);
        x64_guard_ae32(w, X64_RCX, X64_RAX, fault);
        x64_ldz16(w, X64_RAX, X64_R8, X64_OBJ_AFLAGS);
        x64_guard_eq32(w, X64_RAX, (nj_s4)NJ_ARRAY_T_INT, fault);
        x64_ld64(w, X64_R9, X64_R8, X64_OBJ_DATA);
        x64_ld32(w, X64_RAX, X64_SP_ARG, top - 8);    /* value, after guards */
        x64_st32_sib(w, X64_RAX, X64_R9, X64_RCX, 0);
        return;
    }
    case 0xbe: { /* arraylength */
        x64_ld64(w, X64_R8, X64_SP_ARG, top - 8);
        x64_guard64(w, X64_R8, fault);
        x64_ld32(w, X64_RAX, X64_R8, X64_OBJ_LENGTH);
        x64_st32(w, X64_RAX, X64_SP_ARG, top - 8);
        return;
    }
    case 0xb4: { /* getfield: receiver -> value */
        const nj_field *f = b->field;
        int sign = 0;
        int size;
        if (!f) { w->failed = 1; return; }
        size = x64_field_size(f, &sign);
        if (size < 0) { w->failed = 1; return; }
        x64_ld64(w, X64_R8, X64_SP_ARG, top - 8);
        x64_guard64(w, X64_R8, fault);
        switch (size) {
        case 0:
            if (sign) x64_lds8(w, X64_RAX, X64_R8, (int)f->offset);
            else      x64_ldz8(w, X64_RAX, X64_R8, (int)f->offset);
            x64_st32(w, X64_RAX, X64_SP_ARG, top - 8);
            break;
        case 1:
            if (sign) x64_lds16(w, X64_RAX, X64_R8, (int)f->offset);
            else      x64_ldz16(w, X64_RAX, X64_R8, (int)f->offset);
            x64_st32(w, X64_RAX, X64_SP_ARG, top - 8);
            break;
        case 2:
            x64_ld32(w, X64_RAX, X64_R8, (int)f->offset);
            x64_st32(w, X64_RAX, X64_SP_ARG, top - 8);
            break;
        default:
            x64_ld64(w, X64_RAX, X64_R8, (int)f->offset);
            x64_st64(w, X64_RAX, X64_SP_ARG, top - 8);
            if (f->width == 2u) {
                x64_mov32_imm(w, X64_RAX, 0u);
                x64_st32(w, X64_RAX, X64_SP_ARG, top);
            }
            break;
        }
        return;
    }
    case 0xb5: { /* putfield: receiver, value (value on top) */
        const nj_field *f = b->field;
        int sign = 0;
        int size;
        int value_slot;
        int receiver_slot;
        if (!f) { w->failed = 1; return; }
        size = x64_field_size(f, &sign);
        if (size < 0) { w->failed = 1; return; }
        value_slot = top - (int)f->width * X64_SLOT_BYTES;
        receiver_slot = value_slot - X64_SLOT_BYTES;
        x64_ld64(w, X64_R8, X64_SP_ARG, receiver_slot);
        x64_guard64(w, X64_R8, fault);
        if (size == 3) {
            x64_ld64(w, X64_RAX, X64_SP_ARG, value_slot);
            x64_st64(w, X64_RAX, X64_R8, (int)f->offset);
        } else {
            x64_ld32(w, X64_RAX, X64_SP_ARG, value_slot);
            if (size == 0)      x64_st8(w, X64_RAX, X64_R8, (int)f->offset);
            else if (size == 1) x64_st16(w, X64_RAX, X64_R8, (int)f->offset);
            else                x64_st32(w, X64_RAX, X64_R8, (int)f->offset);
        }
        return;
    }
#if NJ_ENABLE_FLOAT
    case 0x76: { /* fneg: toggle only the sign bit */
        x64_ld32(w, X64_RAX, X64_SP_ARG, top - 8);
        x64_u8(w, 0x35u); /* xor eax, imm32 */
        x64_u32(w, 0x80000000u);
        x64_st32(w, X64_RAX, X64_SP_ARG, top - 8);
        return;
    }
#if NJ_ENABLE_LONG
    case 0x77: { /* dneg */
        x64_ld64(w, X64_RAX, X64_SP_ARG, top - 8);
        x64_mov64_imm(w, X64_RCX, (nj_u8)1u << 63);
        x64_rex(w, 1, X64_RCX, X64_RAX);
        x64_u8(w, 0x31u);          /* xor rax, rcx */
        x64_u8(w, 0xc8u);
        x64_st64(w, X64_RAX, X64_SP_ARG, top - 8);
        return;
    }
#endif
    case 0x62: case 0x66: case 0x6a: case 0x6e:
#if NJ_ENABLE_LONG
    case 0x63: case 0x67: case 0x6b: case 0x6f:
#endif
    {
        int is_double = (int)(op & 1u);
        int width = is_double ? 16 : 8;
        int dst = top - width * 2;
        unsigned opcode;
        x64_ss_load(w, is_double, 0u, X64_SP_ARG, dst);
        x64_ss_load(w, is_double, 1u, X64_SP_ARG, top - width);
        switch (op & ~1u) {
        case 0x62: opcode = 0x58u; break; /* add */
        case 0x66: opcode = 0x5cu; break; /* sub */
        case 0x6a: opcode = 0x59u; break; /* mul */
        default:   opcode = 0x5eu; break; /* div */
        }
        x64_ss_arith(w, is_double, opcode);
        x64_ss_store(w, is_double, X64_SP_ARG, dst);
        if (is_double) {
            x64_mov32_imm(w, X64_RAX, 0u);
            x64_st32(w, X64_RAX, X64_SP_ARG, dst + 8);
        }
        return;
    }
#endif
    case 0x60: case 0x64: case 0x68:
    case 0x78: case 0x7a: case 0x7c:
    case 0x7e: case 0x80: case 0x82: {
        x64_ld32(w, X64_RAX, X64_SP_ARG, top - 16);
        x64_ld32(w, X64_RCX, X64_SP_ARG, top - 8);
        switch (op) {
        case 0x60: x64_arith32(w, 0x01u, X64_RAX, X64_RCX); break; /* add */
        case 0x64: x64_arith32(w, 0x29u, X64_RAX, X64_RCX); break; /* sub */
        case 0x68: x64_imul32(w, X64_RAX, X64_RCX); break;
        case 0x78: x64_shift32_cl(w, X64_RAX, 4u); break; /* shl */
        case 0x7a: x64_shift32_cl(w, X64_RAX, 7u); break; /* sar */
        case 0x7c: x64_shift32_cl(w, X64_RAX, 5u); break; /* shr */
        case 0x7e: x64_arith32(w, 0x21u, X64_RAX, X64_RCX); break; /* and */
        case 0x80: x64_arith32(w, 0x09u, X64_RAX, X64_RCX); break; /* or */
        default:   x64_arith32(w, 0x31u, X64_RAX, X64_RCX); break; /* xor */
        }
        x64_st32(w, X64_RAX, X64_SP_ARG, top - 16);
        return;
    }
    default:
        w->failed = 1;
        return;
    }
}

/* ---- block scanning -------------------------------------------------- */

typedef struct x64_scan {
    x64_bytecode ins[NJ_JIT_MAX_BLOCK_INSNS];
    nj_u4 pcs[NJ_JIT_MAX_BLOCK_INSNS];
    int delta[NJ_JIT_MAX_BLOCK_INSNS];       /* operand depth before the insn */
    nj_u1 label[NJ_JIT_MAX_BLOCK_INSNS];     /* branch target instructions    */
    nj_u1 br_kind[NJ_JIT_MAX_BLOCK_INSNS];   /* X64_BR_*                      */
    nj_u2 br_target[NJ_JIT_MAX_BLOCK_INSNS]; /* label index / exit index      */
    nj_u1 fault_of[NJ_JIT_MAX_BLOCK_INSNS];  /* fault index, X64_NO_FAULT     */
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
} x64_scan;

enum {
    X64_BR_NONE = 0u,
    X64_BR_INTERNAL = 1u,
    X64_BR_EXIT = 2u,
    X64_BR_BACK = 3u,
    X64_NO_FAULT = 0xffu
};

static int x64_terminates(const x64_bytecode *b)
{
    return b->op == 0x6cu || b->op == 0x70u;
}

static int x64_is_fp(const x64_bytecode *b)
{
#if NJ_ENABLE_FLOAT
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
static int x64_scan_build(nj_vm *vm, const nj_method *m, nj_u4 pc, x64_scan *s)
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
        x64_bytecode b;
        int delta, required, growth;

        if (!x64_decode(vm, m, pos, &b)) break;
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
            s->fault_of[n] = X64_NO_FAULT;
        }
        if (x64_is_fp(&b)) s->fp_used = 1;
        n++;
        pos = (nj_u4)(pos + b.length);
        depth = delta;
        min_stack = required;
        max_growth = growth;
        if (x64_terminates(&b)) break;
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
            s->br_kind[insn] = (nj_u1)X64_BR_BACK;
            s->label[0] = 1u;
            continue;
        }
        if (s->jump_target[i] >= s->end) {
            int exit = nj_jit_record_exit(&s->block, s->start,
                                          s->jump_target[i], s->jump_delta[i]);
            if (exit < 0) return 0;
            s->br_kind[insn] = (nj_u1)X64_BR_EXIT;
            s->br_target[insn] = (nj_u2)exit;
        } else {
            unsigned k;
            int found = 0;
            for (k = 0u; k < n; ++k) {
                if (s->pcs[k] == s->jump_target[i]) {
                    s->label[k] = 1u;
                    s->br_kind[insn] = (nj_u1)X64_BR_INTERNAL;
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
        if (s->fault_of[i] != X64_NO_FAULT &&
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

static int x64_analyze_impl(nj_vm *vm, const nj_method *m, nj_u4 pc,
                            nj_jit_block *out)
{
    x64_scan scan;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!m || !m->code || pc >= m->code_len ||
        sizeof(nj_value) != X64_SLOT_BYTES ||
        m->code_len - pc > 0xffffu) return 0;
    if (!x64_scan_build(vm, m, pc, &scan)) return 0;
    *out = scan.block;
    return 1;
}

int nj_x86_64_analyze(nj_vm *vm, const nj_method *m, nj_u4 pc, nj_jit_block *out)
{
    return x64_analyze_impl(vm, m, pc, out);
}

/* ---- emission -------------------------------------------------------- */

/* Small immediate forms used by the exit stubs. */
static void x64_shift32_imm(x64_writer *w, unsigned reg, unsigned ext,
                            unsigned imm)
{
    x64_rex(w, 0, 0, reg);
    x64_u8(w, 0xc1u);
    x64_u8(w, 0xc0u | ((ext & 7u) << 3) | (reg & 7u));
    x64_u8(w, imm & 0xffu);
}

static void x64_add32_imm8(x64_writer *w, unsigned reg, unsigned imm)
{
    x64_rex(w, 0, 0, reg);
    x64_u8(w, 0x83u);
    x64_u8(w, 0xc0u | (0u << 3) | (reg & 7u));
    x64_u8(w, imm & 0xffu);
}

/* Take the branch at instruction `insn`, popping its operands.  The completed
 * counter is advanced before the compare because x86 INC/ADD modify flags;
 * the block-exit stubs return the updated count. */
static void x64_emit_branch(x64_writer *w, const x64_scan *s, unsigned insn)
{
    const x64_bytecode *b = &s->ins[insn];
    int top = s->delta[insn] * X64_SLOT_BYTES;
    unsigned kind, target, cc;

    if (s->br_kind[insn] == X64_BR_BACK) {
        kind = X64_FIX_LABEL;
        target = 0u;
    } else if (s->br_kind[insn] == X64_BR_INTERNAL) {
        kind = X64_FIX_LABEL;
        target = s->br_target[insn];
    } else {
        kind = X64_FIX_EXIT;
        target = s->br_target[insn];
    }

    if (w->counter) x64_inc32(w, X64_DONE);

    if (b->branch == 2) { /* goto */
        x64_jmp(w, kind, target);
        return;
    }

    switch (b->op) {
    case 0x99: cc = 4u; break;  /* ifeq    */
    case 0x9a: cc = 5u; break;  /* ifne    */
    case 0x9b: cc = 0xcu; break;/* iflt    */
    case 0x9c: cc = 0xdu; break;/* ifge    */
    case 0x9d: cc = 0xfu; break;/* ifgt    */
    case 0x9e: cc = 0xeu; break;/* ifle    */
    case 0x9f: cc = 4u; break;  /* if_icmpeq */
    case 0xa0: cc = 5u; break;  /* if_icmpne */
    case 0xa1: cc = 0xcu; break;/* if_icmplt */
    case 0xa2: cc = 0xdu; break;/* if_icmpge */
    case 0xa3: cc = 0xfu; break;/* if_icmpgt */
    case 0xa4: cc = 0xeu; break;/* if_icmple */
    case 0xa5: cc = 4u; break;  /* if_acmpeq */
    case 0xa6: cc = 5u; break;  /* if_acmpne */
    case 0xc6: cc = 4u; break;  /* ifnull    */
    case 0xc7: cc = 5u; break;  /* ifnonnull */
    default: w->failed = 1; return;
    }

    if (b->op == 0xa5u || b->op == 0xa6u) {
        /* References are full 64-bit values under LP64. */
        x64_ld64(w, X64_RAX, X64_SP_ARG, top - 16);
        x64_ld64(w, X64_RCX, X64_SP_ARG, top - 8);
        x64_cmp64_reg(w, X64_RAX, X64_RCX);
    } else if (b->op == 0xc6u || b->op == 0xc7u) {
        x64_ld64(w, X64_RAX, X64_SP_ARG, top - 8);
        x64_test64(w, X64_RAX);
    } else if (b->need == 2) {
        x64_ld32(w, X64_RAX, X64_SP_ARG, top - 16);
        x64_ld32(w, X64_RCX, X64_SP_ARG, top - 8);
        x64_cmp32_reg(w, X64_RAX, X64_RCX);
    } else {
        x64_ld32(w, X64_RAX, X64_SP_ARG, top - 8);
        x64_cmp32_imm(w, X64_RAX, 0);
    }
    x64_jcc(w, cc, kind, target);
}

static int x64_emit_block(x64_writer *w, const x64_scan *s, nj_size capacity)
{
    const nj_jit_block *block = &s->block;
    nj_size label_at[NJ_JIT_MAX_BLOCK_INSNS];
    nj_size exit_at[NJ_JIT_MAX_EXITS];
    nj_size fault_at[NJ_JIT_MAX_FAULTS];
    nj_size reserve;
    unsigned i;
    unsigned exit_stub = 10u; /* mov eax,r10d; shl eax,4; add eax,imm8; ret */
    unsigned fault_stub = 9u; /* mov eax,imm32; or eax,r10d; ret */

    for (i = 0u; i < (unsigned)NJ_JIT_MAX_BLOCK_INSNS; ++i)
        label_at[i] = X64_NO_LABEL;

    if (!s->njumps) {
        exit_stub = 6u;  /* mov eax,imm32; ret */
        fault_stub = 6u;
    }
    /* Stub bytes are budgeted up front so the instruction stream can never
     * crowd out the exits it branches to.  The bound is exact, so a block can
     * compile at a capacity equal to its own size. */
    reserve = (nj_size)exit_stub * (nj_size)block->nexits;
    reserve += (nj_size)fault_stub * (nj_size)block->nfaults;
    if (capacity < reserve + 4u) return 0;
    w->capacity = capacity - reserve;
    w->counter = s->njumps != 0u;

    if (w->counter) {
        x64_u8(w, 0x45u); /* xor r10d, r10d */
        x64_u8(w, 0x31u);
        x64_u8(w, 0xd2u);
    }

    for (i = 0u; i < s->n; ++i) {
        if (i == 0u && s->loop != 0) {
            label_at[0] = w->used;
            /* remaining = budget - completed; enter another iteration only
             * when a whole window still fits. */
            x64_mov32_reg(w, X64_RCX, X64_RDX); /* mov ecx, edx */
            x64_arith32(w, 0x29u, X64_RCX, X64_DONE); /* sub ecx, r10d */
            x64_cmp32_imm(w, X64_RCX, (nj_s4)s->n);
            x64_jcc(w, 2u, X64_FIX_EXIT, s->budget_exit); /* jb */
        } else if (s->label[i]) {
            label_at[i] = w->used;
        }
        if (s->ins[i].branch != 0) {
            x64_emit_branch(w, s, i);
        } else {
            unsigned fault = s->fault_of[i] == X64_NO_FAULT ?
                             0u : (unsigned)s->fault_of[i];
            x64_emit(w, &s->ins[i], s->delta[i], fault);
            if (w->counter) x64_inc32(w, X64_DONE);
        }
        if (w->failed) return 0;
    }

    w->capacity = capacity;

    /* Success/exit stubs: eax = (completed << 4) | exit_index. */
    for (i = 0u; i < (unsigned)block->nexits; ++i) {
        exit_at[i] = w->used;
        if (w->counter) {
            x64_mov32_reg(w, X64_RAX, X64_DONE);
            x64_shift32_imm(w, X64_RAX, 4u, 4u); /* shl eax,4 */
            if (i != 0u) x64_add32_imm8(w, X64_RAX, i);
        } else {
            x64_mov32_imm(w, X64_RAX,
                          ((nj_u4)block->insns << NJ_JIT_EXIT_SHIFT) |
                          (nj_u4)i);
        }
        x64_ret(w);
    }
    /* Fault stubs: eax = fault word carrying the live completed count. */
    for (i = 0u; i < (unsigned)block->nfaults; ++i) {
        fault_at[i] = w->used;
        if (w->counter) {
            x64_mov32_imm(w, X64_RAX,
                          NJ_JIT_FAULT_BIT |
                          ((nj_u4)i << NJ_JIT_FAULT_INDEX_SHIFT));
            x64_arith32(w, 0x09u, X64_RAX, X64_DONE); /* or eax, r10d */
        } else {
            x64_mov32_imm(w, X64_RAX,
                          x64_fault_word(i, (nj_u4)s->fault_completed[i]));
        }
        x64_ret(w);
    }
    if (w->failed) return 0;

    for (i = 0u; i < w->nfixups; ++i) {
        const x64_fixup *f = &w->fixups[i];
        nj_size target;
        if (f->kind == X64_FIX_FAULT) {
            if (f->target >= block->nfaults) return 0;
            target = fault_at[f->target];
        } else if (f->kind == X64_FIX_EXIT) {
            if (f->target >= block->nexits) return 0;
            target = exit_at[f->target];
        } else if (f->kind == X64_FIX_LABEL) {
            if (f->target >= s->n || label_at[f->target] == X64_NO_LABEL)
                return 0;
            target = label_at[f->target];
        } else {
            return 0;
        }
        if (getenv("X64DBG"))
            fprintf(stderr, "DBG fix at=%u kind=%u idx=%u target=%u\n",
                    (unsigned)f->at, (unsigned)f->kind, (unsigned)f->target,
                    (unsigned)target);
        x64_patch_rel32(w->buf, f, target);
    }
    if (w->used > capacity) return 0;
    return 1;
}

int nj_x86_64_compile(nj_vm *vm, const nj_method *m, nj_u4 pc, nj_u1 *buf,
                      nj_size capacity, nj_jit_block *out)
{
    x64_scan scan;
    x64_writer w;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!m || !m->code || !buf || pc >= m->code_len ||
        sizeof(nj_value) != X64_SLOT_BYTES ||
        m->code_len - pc > 0xffffu) return 0;
    if (capacity > NJ_JIT_MAX_CODE_BYTES) capacity = NJ_JIT_MAX_CODE_BYTES;
    if (!x64_scan_build(vm, m, pc, &scan)) return 0;
    memset(&w, 0, sizeof(w));
    w.buf = buf;
    w.used = 0u;
    w.capacity = capacity;
    w.failed = 0;
    w.nfixups = 0u;
    if (!x64_emit_block(&w, &scan, capacity)) return 0;
    if (w.used > capacity || w.failed) return 0;
    scan.block.size = w.used;
    *out = scan.block;
    return 1;
}
#endif /* NJ_ENABLE_JIT */
