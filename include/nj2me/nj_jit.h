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
} nj_jit_stats;

/* nj_vm_create tries this automatically when NJ_ENABLE_JIT=1. An unsupported
 * CPU or port returns NJ_ERR_UNSUPPORTED and leaves the interpreter available.
 * An existing external engine is never replaced. No CPU probing is performed:
 * NJ_JIT_RV32_* must describe instructions available on the deployment CPU. */
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
