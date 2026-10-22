/*
 * Bounded little-endian AArch64 baseline blocks. The generated leaf has ABI
 *     void block(nj_value *incoming_sp, nj_value *locals)
 * with x0/x1 unchanged, integer temporaries x9..x13, and s0/s1 or d0/d1.
 * It neither uses the native stack nor touches callee-saved registers. Java
 * control flow, exceptions, allocation, resolution and calls remain in C.
 *
 * The encoder is portable: words are written as bytes, never through native
 * instruction pointers. All compilation state is local to the invocation.
 */
#include "nj_jit_internal.h"

#if NJ_ENABLE_JIT

enum {
    A64_SP_ARG = 0, A64_LOCALS_ARG = 1,
    A64_T0 = 9, A64_T1 = 10, A64_T2 = 11,
    A64_FPCR = 12, A64_ADDR = 13, A64_ZERO = 31,
    A64_F0 = 0, A64_F1 = 1, A64_SLOT_BYTES = 8
};

typedef struct a64_writer {
    nj_u1 *buf;
    nj_size used;
    nj_size capacity;
    int failed;
    int fpcr_saved;
} a64_writer;

typedef struct a64_bytecode {
    unsigned op;
    unsigned local;
    int immediate;
    int need;
    int change;
    nj_u4 length;
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

static void a64_copy_slot(a64_writer *w, unsigned dst, int dst_offset,
                          unsigned src, int src_offset)
{
    a64_load(w, A64_T0, src, src_offset, 1);
    a64_store(w, A64_T0, dst, dst_offset, 1);
}

#if NJ_ENABLE_FLOAT && NJ_JIT_ARM64_FP
static void a64_fp_begin(a64_writer *w)
{
    if (w->fpcr_saved || w->failed) return;
    /* The ordinary return already has four bytes reserved. Reserve another
     * four BEFORE accepting the first FP operation, for restoring FPCR. */
    if (w->capacity < 4u) {
        w->failed = 1;
        return;
    }
    w->capacity -= 4u;
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

static int a64_decode(const nj_method *m, nj_u4 pc, a64_bytecode *b)
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
    case 0x60: case 0x64: case 0x68: case 0x6c: case 0x70:
    case 0x78: case 0x7a: case 0x7c:
    case 0x7e: case 0x80: case 0x82:
        b->need = 2;
        b->change = -1;
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

static void a64_emit(a64_writer *w, const a64_bytecode *b, int delta)
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
    case 0x10: case 0x11:
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
    case 0x6c: case 0x70: opcode = 0x1ac00c00u; break; /* sdiv */
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
    if (op == 0x70u) {
        a64_r3(w, opcode, A64_T2, A64_T0, A64_T1);
        a64_r3(w, 0x1b008000u | ((nj_u4)A64_T0 << 10),
                   A64_T0, A64_T2, A64_T1); /* msub: a - (a / b) * b */
    } else {
        a64_r3(w, opcode, A64_T0, A64_T0, A64_T1);
    }
    /* W-register shifts mask the count to five bits. SDIV wraps INT_MIN/-1
     * as Java requires; the C caller rejects a zero divisor before entry. */
    a64_store(w, A64_T0, A64_SP_ARG, top - 16, 0);
}

int nj_arm64_compile(const nj_method *m, nj_u4 pc, nj_u1 *buf,
                     nj_size capacity, nj_jit_block *out)
{
    a64_writer w;
    nj_jit_block block;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!m || !m->code || !buf || pc >= m->code_len ||
        sizeof(nj_value) != A64_SLOT_BYTES) return 0;
    if (capacity > NJ_JIT_MAX_CODE_BYTES) capacity = NJ_JIT_MAX_CODE_BYTES;
    if (capacity < 8u) return 0; /* BTI C plus RET, even for a nop block. */
    memset(&block, 0, sizeof(block));
    block.next_pc = pc;
    w.buf = buf;
    w.used = 0u;
    w.capacity = capacity - 4u; /* Reserve RET before emitting any bytecode. */
    w.failed = 0;
    w.fpcr_saved = 0;
    /* Indirect calls may target a BTI-guarded executable page. This landing
     * pad is a backwards-compatible HINT on cores without FEAT_BTI. */
    a64_u32(&w, 0xd503245fu); /* bti c */

    while (block.next_pc < m->code_len && block.insns < NJ_JIT_MAX_BLOCK_INSNS) {
        a64_bytecode b;
        nj_size checkpoint = w.used;
        nj_size saved_capacity = w.capacity;
        int saved_fpcr = w.fpcr_saved;
        int delta, required, growth;
        if (!a64_decode(m, block.next_pc, &b)) break;
        /* A possibly throwing operation is its own block, with the divisor
         * still in incoming_sp[-1] and no earlier state changes to undo. */
        if ((b.op == 0x6cu || b.op == 0x70u) && block.insns != 0u) break;
        delta = block.stack_delta + b.change;
        required = b.need - block.stack_delta;
        if (required < block.min_stack) required = block.min_stack;
        growth = delta > block.max_growth ? delta : block.max_growth;
        if (required + growth > (int)m->max_stack) break;
        a64_emit(&w, &b, block.stack_delta);
        if (w.failed) {
            /* A declined first FP operation must also roll back the extra
             * return reservation and FPCR state. Only the prefix stays live. */
            w.used = checkpoint;
            w.capacity = saved_capacity;
            w.fpcr_saved = saved_fpcr;
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
    if (w.fpcr_saved) a64_u32(&w, 0xd51b4400u | A64_FPCR); /* msr fpcr,x12 */
    a64_u32(&w, 0xd65f03c0u); /* ret x30 */
    if (w.failed) return 0;
    block.size = w.used;
    *out = block;
    return 1;
}
#endif /* NJ_ENABLE_JIT */
