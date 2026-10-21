/*
 * nano_j2me - nj_engine.h
 *
 * ============================================================================
 *  THE PLUGGABLE EXECUTION ENGINE CONTRACT (internal)
 * ============================================================================
 *
 * nj_exec() is the single entry point through which every part of the runtime
 * outside the interpreter runs Java bytecode on a thread.  With
 * NJ_ENABLE_ENGINE=1 that entry point dispatches through an optional engine
 * installed on the VM:
 *
 *     nj_exec() --> vm->engine->ops->run(engine, vm, th, stop_depth)
 *               \-> nj_interp_run(vm, th, stop_depth)      [no engine]
 *
 * An engine's run() must be a drop-in replacement for nj_interp_run() - same
 * arguments, same return codes, same observable VM state on every return.
 * The built-in "null" engine (nj_engine_null()) simply forwards to
 * nj_interp_run(); it exists so the dispatch path can be exercised without a
 * compiler, and tests/test_engine.c asserts that installing it changes nothing
 * about what a program computes.
 *
 * ----------------------------------------------------------------------------
 * WHAT AN ENGINE MUST HONOUR (the short list; docs/JIT.md is the full contract)
 * ----------------------------------------------------------------------------
 *   - Run th while th->nframes > stop_depth.  Return NJ_OK once
 *     th->nframes <= stop_depth, with the returned value in th->ret_value.
 *   - Return NJ_ERR_YIELD at least every NJ_SCHED_QUANTUM Java bytecodes,
 *     with every frame's pc and sp saved in its nj_frame.
 *   - Return NJ_ERR_HALT when an exception escapes every frame above
 *     stop_depth, with the exception left in vm->exception.
 *   - Keep the frame stack exactly as interpreted bytecode would (nj_frame_push
 *     / nj_frame_pop_fast / nj_value layout, wide values in two slots with a
 *     zeroed high slot).
 *   - Write frame->pc/frame->sp before any call that can allocate, GC, block
 *     or run more Java.
 *   - Keep vm->cur pointing at the executing thread (monitor and GC state).
 *   - Do not add file-scope mutable state: everything a compiled method needs
 *     lives in the engine instance, the VM, or const data (tools/
 *     check_no_statics.sh is the gate).
 *
 * This header is self-contained and its contents exist only when the feature
 * is compiled in, so a default build carries no engine pointer, no dispatch
 * branch and no ABI surface at all.
 */
#ifndef NJ_ENGINE_H
#define NJ_ENGINE_H

#include "nj2me/nj_config.h"
#include "nj2me/nj_types.h"

#include <stddef.h>   /* offsetof: NJ_ENGINE_FROM below */

#ifdef __cplusplus
extern "C" {
#endif

#if NJ_ENABLE_ENGINE

/* Forward declarations: this header must not drag nj_internal.h into ports. */
struct nj_vm;
struct nj_thread;
struct nj_engine;

/*
 * One engine implementation.
 *
 * run      executes the thread as described above.  It receives its own
 *          `engine` back so an implementation can recover a larger struct it
 *          embeds (see NJ_ENGINE_FROM) without a global lookup.
 * destroy  optional; releases whatever the instance owns.  Never called by the
 *          VM - the caller that created the instance owns it.  Use
 *          nj_engine_destroy() for the dispatch.  NULL is valid (the null
 *          engine is a shared const object and has nothing to release).
 */
typedef struct nj_engine_ops {
    const char *name;
    nj_status (*run)(struct nj_engine *engine, struct nj_vm *vm,
                     struct nj_thread *th, int stop_depth);
    void (*destroy)(struct nj_engine *engine);
} nj_engine_ops;

/*
 * The vtable holder.  `ops` is the only field the runtime knows about; an
 * implementation that needs state embeds this struct as its first member and
 * casts the incoming pointer back.  (Pointer-compatible because a pointer to
 * a struct is a pointer to its first member.)
 */
struct nj_engine {
    const struct nj_engine_ops *ops;
};

/*
 * Recover an embedding struct from the base pointer passed to run()/destroy().
 * The base must be the first member of `type` (which is what makes the cast
 * safe; the macro is just documentation of that requirement).
 */
#define NJ_ENGINE_FROM(engine_ptr, type, member) \
    ((type *)(void *)((nj_u1 *)(void *)(engine_ptr) - offsetof(type, member)))

/*
 * Install `engine` on `vm` (NULL clears it).  The instance is borrowed, not
 * copied: it has to stay alive, and unmodified, until it is uninstalled or the
 * VM is destroyed - whichever comes first.  Installing affects the next
 * nj_exec() call; a caller inside the runtime (a native that calls back into
 * Java) is safe, because dispatch is re-read on every entry.
 *
 * The VM does not destroy engines: nj_vm_destroy() leaves no dangling
 * allocation of its own, but the engine instance belongs to its owner.
 */
nj_status nj_engine_install(struct nj_vm *vm, struct nj_engine *engine);

/* Remove and return the installed engine, or NULL (and NULL vm -> NULL). */
struct nj_engine *nj_engine_uninstall(struct nj_vm *vm);

/* The installed engine, or NULL when the interpreter will run. */
struct nj_engine *nj_engine_current(struct nj_vm *vm);

/*
 * The built-in null engine: run() forwards to nj_interp_run(), so installing
 * it is required to be observationally identical to not installing anything.
 *
 * The returned pointer refers to ONE shared read-only instance; do not modify
 * it, do not call nj_engine_destroy() on it, and do not assume the pointer
 * differs between VMs.
 */
struct nj_engine *nj_engine_null(void);

/* Dispatch ops->destroy (no-op for NULL or an engine without a destroy hook). */
void nj_engine_destroy(struct nj_engine *engine);

#endif /* NJ_ENABLE_ENGINE */

#ifdef __cplusplus
}
#endif

#endif /* NJ_ENGINE_H */
