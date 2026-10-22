/* Shared baseline-JIT/backend boundary. No backend owns VM state. */
#ifndef NJ_JIT_INTERNAL_H
#define NJ_JIT_INTERNAL_H

#include "nj_internal.h"

#if NJ_ENABLE_JIT
/* One bounded, straight-line block. Stack requirements are relative to its
 * incoming sp; native code only touches frame slots and caller-saved registers.
 * It never calls C, allocates, throws, branches to Java, or changes frames. */
typedef struct nj_jit_block {
    nj_u4 next_pc;
    nj_u4 insns;
    int stack_delta;
    int min_stack;
    int max_growth;
    nj_size size;
    int check_divisor;  /* single idiv/irem: C must reject a zero stack[-1].i */
} nj_jit_block;

/* Portable code generator: usable on any host for encoding tests. Returns 1
 * for a nonempty block, 0 when declined. No writes beyond capacity. */
int nj_rv32_compile(const nj_method *m, nj_u4 pc, nj_u1 *buf,
                    nj_size capacity, nj_jit_block *out);
int nj_arm64_compile(const nj_method *m, nj_u4 pc, nj_u1 *buf,
                     nj_size capacity, nj_jit_block *out);

/* Scan only: no emission, allocation, resolution or callbacks. size is zero;
 * emission may later shorten the prefix to fit the code buffer. */
int nj_rv32_analyze(const nj_method *m, nj_u4 pc, nj_jit_block *out);
int nj_arm64_analyze(const nj_method *m, nj_u4 pc, nj_jit_block *out);

/* Numeric constants are immutable raw class bytes. Object constants must go
 * through the interpreter's resolution/allocation path. Bounds are checked
 * without addition so malformed offsets cannot wrap. */
static inline int nj_jit_numeric_constant(const nj_method *m, unsigned index,
                                          nj_u4 *bits)
{
    const nj_class *c = m ? m->owner : NULL;
    nj_u4 info, off, tag;
    if (!c || !c->cp_info || !c->data || !bits || index == 0u ||
        index >= c->cp_count) return 0;
    info = c->cp_info[index];
    tag = info & 15u;
    if (tag != NJ_CP_INTEGER && tag != NJ_CP_FLOAT) return 0;
    off = info >> 4;
    if (!off || off > c->data_len || c->data_len - off < 4u) return 0;
    *bits = ((nj_u4)c->data[off] << 24) | ((nj_u4)c->data[off + 1u] << 16) |
            ((nj_u4)c->data[off + 2u] << 8) | c->data[off + 3u];
    return 1;
}

/* Conservative opcode filter shared by the native backends. False positives
 * are decoded once into site history; known interpreter-only opcodes avoid
 * even the out-of-line probe. Never inspect operands here. */
static inline int nj_jit_opcode_candidate(nj_u1 op)
{
    return op <= 0x13u || (op >= 0x15u && op <= 0x2du) ||
           (op >= 0x36u && op <= 0x4eu) || (op >= 0x57u && op <= 0x84u) ||
           (op >= 0x91u && op <= 0x93u) || op == 0xc4u;
}

struct nj_jit_entry;
struct nj_jit_site;
typedef struct nj_jit_ticket {
    struct nj_jit_entry *entry;
    struct nj_jit_site *site;
} nj_jit_ticket;
/* Probe mutates only bounded JIT metadata; it never touches frame slots, calls
 * the port, allocates, frees or enters Java. Consume the ticket immediately,
 * after SAVE, before another probe or any engine/lifetime change. */
int nj_jit_probe(nj_vm *vm, const nj_frame *fr, nj_u4 pc, nj_value *sp,
                 nj_u4 budget, nj_jit_ticket *ticket);
nj_u4 nj_jit_execute(nj_vm *vm, nj_frame *fr, nj_u4 *pc, nj_value **sp,
                     nj_u4 budget, const nj_jit_ticket *ticket);
/* Convenience for internal callers that have already published frame state.
 * Returns bytecodes executed, updating pc/sp only on success. */
nj_u4 nj_jit_try_block(nj_vm *vm, nj_frame *fr, nj_u4 *pc,
                       nj_value **sp, nj_u4 budget);
#endif
#endif
