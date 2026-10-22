/* Bounded, per-VM baseline JIT. Machine code is a leaf over ordinary slots;
 * the interpreter retains control of all Java flow, safepoints and exceptions. */
#include "nj_jit_internal.h"
#include "nj2me/nj_jit.h"

#if NJ_ENABLE_JIT

typedef struct nj_jit_entry {
    const nj_method *method;
    nj_u4 pc;
    nj_u4 hits;
    int declined;
    void *code;
    nj_jit_block block;
} nj_jit_entry;

struct nj_jit {
    struct nj_engine base;
    nj_vm *vm;
    nj_jit_stats stats;
    nj_jit_entry entries[NJ_JIT_CACHE_ENTRIES];
};

#if NJ_JIT_HOST_RV32
static nj_status jit_run(struct nj_engine *engine, nj_vm *vm,
                         nj_thread *th, int stop_depth)
{
    (void)engine;
    return nj_interp_run(vm, th, stop_depth);
}

static void jit_destroy(struct nj_engine *engine)
{
    struct nj_jit *jit = NJ_ENGINE_FROM(engine, struct nj_jit, base);
    nj_jit_disable(jit->vm);
}

static const nj_engine_ops jit_ops = { "riscv32-baseline", jit_run, jit_destroy };
#endif

nj_status nj_jit_enable(nj_vm *vm)
{
    if (!vm) return NJ_ERR;
    if (vm->jit && vm->engine == &vm->jit->base) return NJ_OK;
    if (vm->engine) return NJ_ERR;
#if NJ_JIT_HOST_RV32
    if (!vm->port || !vm->port->code_alloc || !vm->port->code_commit ||
        !vm->port->code_free) return NJ_ERR_UNSUPPORTED;
    if (!vm->jit) {
        struct nj_jit *jit = (struct nj_jit *)nj_host_alloc(vm, sizeof(*jit));
        if (!jit) return NJ_ERR_OOM;
        memset(jit, 0, sizeof(*jit));
        jit->base.ops = &jit_ops;
        jit->vm = vm;
        vm->jit = jit;
    }
    return nj_engine_install(vm, &vm->jit->base);
#else
    /* The encoder is portable; executing its output is not. */
    return NJ_ERR_UNSUPPORTED;
#endif
}

void nj_jit_disable(nj_vm *vm)
{
    nj_u4 i;
    struct nj_jit *jit;
    if (!vm || !vm->jit) return;
    jit = vm->jit;
    if (vm->engine == &jit->base) vm->engine = NULL;
    vm->jit = NULL;
    for (i = 0; i < NJ_JIT_CACHE_ENTRIES; ++i) {
        nj_jit_entry *e = &jit->entries[i];
        if (e->code) vm->port->code_free(vm->port->ud, e->code, e->block.size);
    }
    nj_host_free(vm, jit);
}

int nj_jit_get_stats(const nj_vm *vm, nj_jit_stats *out)
{
    if (out) {
        memset(out, 0, sizeof(*out));
        if (vm && vm->jit) *out = vm->jit->stats;
    }
    return vm && vm->jit;
}

#if NJ_JIT_HOST_RV32
static void jit_compile(struct nj_jit *jit, nj_jit_entry *e)
{
    nj_u1 bytes[NJ_JIT_MAX_CODE_BYTES];
    const nj_port *port = jit->vm->port;
    void *code;
    /* A refusal (including executable-memory failure) is negatively cached.
     * No retry storm on every dispatch in a memory-constrained port. */
    e->declined = 1;
    if (!nj_rv32_compile(e->method, e->pc, bytes, sizeof(bytes), &e->block)) {
        jit->stats.declined_blocks++;
        return;
    }
    code = port->code_alloc(port->ud, e->block.size);
    if (!code) {
        jit->stats.declined_blocks++;
        return;
    }
    /* A conforming code allocator provides at least 4-byte alignment. */
    if (((uintptr_t)code & 3u) != 0u) {
        port->code_free(port->ud, code, e->block.size);
        jit->stats.declined_blocks++;
        return;
    }
    memcpy(code, bytes, e->block.size);
    if (port->code_commit(port->ud, code, e->block.size) != 0) {
        port->code_free(port->ud, code, e->block.size);
        jit->stats.declined_blocks++;
        return;
    }
    e->code = code;
    e->declined = 0;
    jit->stats.compiled_blocks++;
    jit->stats.code_bytes += e->block.size;
}
#endif

nj_u4 nj_jit_try_block(nj_vm *vm, nj_frame *fr, nj_u4 *pc,
                       nj_value **sp, nj_u4 budget)
{
#if NJ_JIT_HOST_RV32
    struct nj_jit *jit = vm->jit;
    nj_jit_entry *e;
    nj_u4 slot;
    typedef void (*block_fn)(nj_value *, nj_value *);
    block_fn fn;
    /* The code/data pointer representation is the RV32 psABI's flat address. */
    typedef char pointer_size_check[sizeof(fn) == sizeof(void *) ? 1 : -1];
    (void)sizeof(pointer_size_check);
    if (!jit || vm->engine != &jit->base || !fr->m || !fr->m->code ||
        !budget || *pc >= fr->m->code_len) return 0;
#if NJ_ENABLE_DEBUG
    if (vm->trace) return 0;
#endif
    slot = (nj_u4)(((uintptr_t)fr->m >> 3) ^ (*pc * 2654435761u));
    e = &jit->entries[slot % NJ_JIT_CACHE_ENTRIES];
    if (e->method != fr->m || e->pc != *pc) {
        if (e->code) {
            vm->port->code_free(vm->port->ud, e->code, e->block.size);
            jit->stats.code_bytes -= e->block.size;
        }
        memset(e, 0, sizeof(*e));
        e->method = fr->m;
        e->pc = *pc;
    }
    if (e->declined) return 0;
    if (!e->code) {
        if (++e->hits < NJ_JIT_HOT_THRESHOLD) return 0;
        jit_compile(jit, e);
        if (!e->code) return 0;
    }
    /* Check every stack slot the block may touch BEFORE any native write.
     * On refusal the interpreter sees the untouched pc/sp and throws normally. */
    if (e->block.insns > budget || *sp < fr->stack || *sp > fr->stack_limit ||
        *sp - fr->stack < e->block.min_stack ||
        fr->stack_limit - *sp < e->block.max_growth ||
        (e->block.check_divisor && (*sp)[-1].i == 0)) return 0;
    memcpy(&fn, &e->code, sizeof(fn));
    fn(*sp, fr->locals);
    *pc = e->block.next_pc;
    *sp += e->block.stack_delta;
    jit->stats.executed_blocks++;
    jit->stats.bytecodes += e->block.insns;
    return e->block.insns;
#else
    (void)vm; (void)fr; (void)pc; (void)sp; (void)budget;
    return 0;
#endif
}
#endif
