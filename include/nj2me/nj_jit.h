/* Optional baseline JIT. All state and executable pages belong to one VM. */
#ifndef NJ_JIT_H
#define NJ_JIT_H
#include "nj_config.h"
#include "nj_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if NJ_ENABLE_JIT
typedef struct nj_jit_stats {
    nj_u8 compiled_blocks;
    nj_u8 executed_blocks;
    nj_u8 bytecodes;
    nj_u8 declined_blocks;
    nj_size code_bytes; /* requested bytes; port page rounding is additional */
    nj_size peak_code_bytes;
    nj_size metadata_bytes; /* fixed per-VM JIT allocation, excluding code */
    nj_u8 code_evictions;
    /* Per-method probe maps are evicted least-recently-used when the arena is
     * full. Only metadata goes away: compiled code stays in the code cache and
     * a later recompile of the same (method, pc) reuses it. */
    nj_u8 method_map_evictions;
    /* Lower bound: counts recompiles only while a probe map still remembers
     * that the site had been compiled before. */
    nj_u8 recompiled_blocks;
    nj_u8 unsupported_blocks;
    nj_u8 short_blocks;
    nj_u8 memory_failures;
    nj_u8 commit_failures;
    /* Native blocks that exited to the interpreter at a guarded instruction
     * (null receiver, bad array, zero divisor). The interpreter re-executes
     * that bytecode, so Java-level behaviour is unchanged. */
    nj_u8 faults;
    /* Detailed counters are zero unless NJ_JIT_DETAILED_STATS=1. */
    nj_u8 queries;
    nj_u8 code_hits;
    nj_u8 cold_skips;
    nj_u8 negative_hits;
    nj_u8 probe_only_skips;
    nj_u8 budget_skips;
    nj_u8 stack_skips;
    /* Execution-weighted lengths: 1, 2-3, 4-7, 8-15, >=16 bytecodes. */
    nj_u8 block_lengths[5];
    int stats_enabled;
    int detailed_stats_enabled;
} nj_jit_stats;

typedef enum nj_jit_mode {
    NJ_JIT_MODE_EXECUTE = 0,
    /* Same probe/heat/analysis, but no emission, port callbacks or native calls.
     * Existing code is retained. Useful for measuring installed-probe overhead. */
    NJ_JIT_MODE_PROBE_ONLY = 1
} nj_jit_mode;
/* Change mode between VM execution entries. Requires an owned JIT. */
nj_status nj_jit_set_mode(nj_vm *vm, nj_jit_mode mode);

/* nj_vm_create tries this automatically when NJ_ENABLE_JIT=1. An unsupported
 * CPU or port returns NJ_ERR_UNSUPPORTED and leaves the interpreter available.
 * An existing external engine is never replaced. No CPU probing is performed:
 * NJ_JIT_RV32_* / NJ_JIT_ARM64_* must match the deployment CPU. */
nj_status nj_jit_enable(nj_vm *vm);
/* Safe when disabled; restores interpreter only if this JIT is still installed.
 * Call between execution entries, not from code_alloc/code_commit callbacks. */
void nj_jit_disable(nj_vm *vm);
/* Returns 1 when this VM owns a JIT (possibly temporarily uninstalled), else 0.
 * Output is zeroed when absent. Stats reset on disable/re-enable. */
int nj_jit_get_stats(const nj_vm *vm, nj_jit_stats *out);
#endif

#ifdef __cplusplus
}
#endif
#endif
