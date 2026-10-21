/*
 * nano_j2me - pluggable execution-engine dispatch.
 *
 * nj_exec() is the single entry point every caller outside the interpreter
 * uses to run Java bytecode on a thread:
 *
 *     src/core/nj_vm.c      the C-to-Java bridge loop (stop_depth = the frame
 *                           count the C caller started from)
 *     src/core/nj_thread.c  the cooperative scheduler (stop_depth = 0)
 *
 * This file owns that entry point so an alternative execution engine (a JIT,
 * a tracing compiler, a native fast path) can be installed on a VM without
 * touching the interpreter or any of its callers.  With NJ_ENABLE_ENGINE=0
 * this file compiles to exactly one tail call into the portable interpreter,
 * and the VM carries no engine pointer.
 *
 * See docs/JIT.md for the contract an engine implements, including the exact
 * frame ABI and the invariants a compiled method has to preserve.
 */
#include "nj_internal.h"

#if NJ_ENABLE_ENGINE

/*
 * The null engine.  A shared const object: the ops table and the instance
 * contain only pointers, so ELF puts both in .data.rel.ro (read-only after
 * relocation) and they cost no RAM.  The cast in nj_engine_null() only
 * satisfies the non-const handle API; nothing ever writes through it.
 */
static nj_status nj_null_run(struct nj_engine *engine, struct nj_vm *vm,
                             struct nj_thread *th, int stop_depth)
{
    (void)engine;
    return nj_interp_run(vm, th, stop_depth);
}

static const nj_engine_ops nj_null_ops = {
    "null",
    nj_null_run,
    NULL   /* shared const instance: never destroyed */
};

static const struct nj_engine nj_null_engine = { &nj_null_ops };

struct nj_engine *nj_engine_null(void)
{
    return (struct nj_engine *)(void *)&nj_null_engine;
}

nj_status nj_engine_install(struct nj_vm *vm, struct nj_engine *engine)
{
    if (!vm) {
        return NJ_ERR;
    }
    vm->engine = engine;
    return NJ_OK;
}

struct nj_engine *nj_engine_uninstall(struct nj_vm *vm)
{
    struct nj_engine *old;

    if (!vm) {
        return NULL;
    }
    old = vm->engine;
    vm->engine = NULL;
    return old;
}

struct nj_engine *nj_engine_current(struct nj_vm *vm)
{
    return vm ? vm->engine : NULL;
}

void nj_engine_destroy(struct nj_engine *engine)
{
    if (engine && engine->ops && engine->ops->destroy) {
        engine->ops->destroy(engine);
    }
}

#endif /* NJ_ENABLE_ENGINE */

/*
 * The one entry point.  Kept outside the #if so the symbol, its address and
 * its callers are identical in both configurations; only the body changes.
 *
 * A malformed engine (NULL ops, NULL run) falls back to the interpreter rather
 * than crashing: an engine that half-installs itself is a bug in the embedder,
 * not a reason to lose the VM.
 */
nj_status nj_exec(nj_vm *vm, nj_thread *th, int stop_depth)
{
#if NJ_ENABLE_ENGINE
    struct nj_engine *engine = vm ? vm->engine : NULL;

    if (engine && engine->ops && engine->ops->run) {
        return engine->ops->run(engine, vm, th, stop_depth);
    }
#endif
    return nj_interp_run(vm, th, stop_depth);
}
