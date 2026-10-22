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

/* Returns the number of bytecodes executed, updating pc/sp only on success.
 * budget is the interpreter's remaining quantum. Never calls into Java. */
nj_u4 nj_jit_try_block(nj_vm *vm, nj_frame *fr, nj_u4 *pc,
                       nj_value **sp, nj_u4 budget);
#endif
#endif
