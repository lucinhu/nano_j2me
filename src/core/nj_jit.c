/* Bounded per-VM JIT. Probing touches metadata only; compilation and executable
 * memory callbacks run only after the interpreter publishes its exact frame. */
#include "nj_jit_internal.h"
#include "nj2me/nj_jit.h"

#if NJ_ENABLE_JIT
#define JIT_CODE_SETS (NJ_JIT_CACHE_ENTRIES / NJ_JIT_CACHE_WAYS)
#define JIT_SITE_SETS (NJ_JIT_SITE_ENTRIES / 2)
#define JIT_COUNTERS(X) \
    X(executed_blocks) X(bytecodes) X(site_evictions) X(queries) X(code_hits) \
    X(cold_skips) X(negative_hits) X(probe_only_skips) X(budget_skips) \
    X(stack_skips) X(divisor_skips)

typedef struct nj_jit_entry {
    const nj_method *method;
    nj_u4 pc;
    void *code;
    nj_jit_block block;
} nj_jit_entry;

enum { JIT_COLD, JIT_READY, JIT_UNSUPPORTED, JIT_SHORT, JIT_MEMORY };
typedef struct nj_jit_site {
    const nj_method *method;
    nj_u4 pc;
    nj_u2 hits;
    nj_u2 insns;
    nj_u1 state;
    nj_u1 compiled;
} nj_jit_site;

struct nj_jit {
    struct nj_engine base;
    nj_vm *vm;
    nj_jit_stats stats;
    nj_jit_mode mode;
#if NJ_JIT_STATS
    struct {
#define JIT_U32(name) nj_u4 name;
        JIT_COUNTERS(JIT_U32)
#undef JIT_U32
        nj_u4 block_lengths[5];
    } pending;
#endif
    nj_jit_entry entries[NJ_JIT_CACHE_ENTRIES];
    nj_jit_site sites[NJ_JIT_SITE_ENTRIES];
    nj_u1 code_next[JIT_CODE_SETS];
    nj_u1 site_next[JIT_SITE_SETS];
};

#if NJ_JIT_STATS
#  define JIT_COUNT(jit, field, n) ((jit)->pending.field += (n))
#else
#  define JIT_COUNT(jit, field, n) ((void)0)
#endif
#if NJ_JIT_STATS && NJ_JIT_DETAILED_STATS
#  define JIT_DETAIL(jit, field) JIT_COUNT(jit, field, 1u)
#else
#  define JIT_DETAIL(jit, field) ((void)0)
#endif

/* The cache, scheduling and code-memory lifecycle are shared by backends. */
#if NJ_JIT_HOST_RV32
#  define NJ_JIT_COMPILE nj_rv32_compile
#  define NJ_JIT_ANALYZE nj_rv32_analyze
#  define NJ_JIT_ENGINE_NAME "riscv32-baseline"
#elif NJ_JIT_HOST_ARM64
#  define NJ_JIT_COMPILE nj_arm64_compile
#  define NJ_JIT_ANALYZE nj_arm64_analyze
#  define NJ_JIT_ENGINE_NAME "arm64-baseline"
#endif

static void jit_snapshot(const struct nj_jit *jit, nj_jit_stats *out)
{
    *out = jit->stats;
#if NJ_JIT_STATS
#define JIT_ADD(name) out->name += jit->pending.name;
    JIT_COUNTERS(JIT_ADD)
#undef JIT_ADD
    {
        unsigned i;
        for (i = 0; i < 5u; ++i) out->block_lengths[i] += jit->pending.block_lengths[i];
    }
#endif
}

#if NJ_JIT_HOST_SUPPORTED
static void jit_flush(struct nj_jit *jit)
{
#if NJ_JIT_STATS
    nj_jit_stats snapshot;
    jit_snapshot(jit, &snapshot);
    jit->stats = snapshot;
    memset(&jit->pending, 0, sizeof(jit->pending));
#else
    (void)jit;
#endif
}

static nj_status jit_run(struct nj_engine *engine, nj_vm *vm,
                         nj_thread *th, int stop_depth)
{
    nj_status st;
    (void)engine;
    st = nj_interp_run(vm, th, stop_depth);
    /* A quantum bounds all pending hot counters. Snapshots during execution
     * include pending values too, without forcing a flush on every block. */
    if (vm->jit) jit_flush(vm->jit);
    return st;
}

static void jit_destroy(struct nj_engine *engine)
{
    struct nj_jit *jit = NJ_ENGINE_FROM(engine, struct nj_jit, base);
    nj_jit_disable(jit->vm);
}

static const nj_engine_ops jit_ops = { NJ_JIT_ENGINE_NAME, jit_run, jit_destroy };
#endif

nj_status nj_jit_enable(nj_vm *vm)
{
    if (!vm) return NJ_ERR;
    if (vm->jit && vm->engine == &vm->jit->base) return NJ_OK;
    if (vm->engine) return NJ_ERR;
#if NJ_JIT_HOST_SUPPORTED
    if (!vm->port || !vm->port->code_alloc || !vm->port->code_commit ||
        !vm->port->code_free) return NJ_ERR_UNSUPPORTED;
    if (!vm->jit) {
        struct nj_jit *jit = (struct nj_jit *)nj_host_alloc(vm, sizeof(*jit));
        if (!jit) return NJ_ERR_OOM;
        memset(jit, 0, sizeof(*jit));
        jit->base.ops = &jit_ops;
        jit->vm = vm;
        jit->stats.metadata_bytes = sizeof(*jit);
        jit->stats.stats_enabled = NJ_JIT_STATS;
        jit->stats.detailed_stats_enabled = NJ_JIT_DETAILED_STATS;
        vm->jit = jit;
    }
    return nj_engine_install(vm, &vm->jit->base);
#else
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

nj_status nj_jit_set_mode(nj_vm *vm, nj_jit_mode mode)
{
    if (!vm || !vm->jit || (mode != NJ_JIT_MODE_EXECUTE &&
                            mode != NJ_JIT_MODE_PROBE_ONLY)) return NJ_ERR;
    vm->jit->mode = mode;
    return NJ_OK;
}

int nj_jit_get_stats(const nj_vm *vm, nj_jit_stats *out)
{
    if (out) {
        memset(out, 0, sizeof(*out));
        if (vm && vm->jit) jit_snapshot(vm->jit, out);
    }
    return vm && vm->jit;
}

#if NJ_JIT_HOST_SUPPORTED
static nj_u4 jit_hash(const nj_method *m, nj_u4 pc)
{
    return (nj_u4)(((uintptr_t)m >> 3) ^ (pc * 2654435761u));
}

/* Separate two-way site history: replacing code never erases a refusal.
 * This bounded table can itself evict history, reported in site_evictions. */
static nj_jit_site *jit_site(struct nj_jit *jit, const nj_method *m,
                             nj_u4 pc, int create)
{
    nj_u4 h = jit_hash(m, pc);
    nj_u4 set = (h ^ (h >> 16)) % JIT_SITE_SETS;
    nj_jit_site *first = &jit->sites[set * 2u], *site = NULL;
    unsigned i;
    for (i = 0; i < 2u; ++i) {
        if (first[i].method == m && first[i].pc == pc) {
            jit->site_next[set] = (nj_u1)(i ^ 1u);
            return &first[i];
        }
        if (!first[i].method) site = &first[i];
    }
    if (!create) return NULL;
    if (!site) {
        site = &first[jit->site_next[set]];
        JIT_COUNT(jit, site_evictions, 1u);
    }
    jit->site_next[set] = (nj_u1)((site == first) ? 1u : 0u);
    memset(site, 0, sizeof(*site));
    site->method = m;
    site->pc = pc;
    return site;
}

static int jit_guard(struct nj_jit *jit, const nj_jit_block *b,
                      const nj_frame *fr, nj_value *sp, nj_u4 budget)
{
    (void)jit;
    if (b->insns > budget) {
        JIT_DETAIL(jit, budget_skips);
        return 0;
    }
    if (sp < fr->stack || sp > fr->stack_limit ||
        sp - fr->stack < b->min_stack || fr->stack_limit - sp < b->max_growth) {
        JIT_DETAIL(jit, stack_skips);
        return 0;
    }
    if (b->check_divisor && sp[-1].i == 0) {
        JIT_DETAIL(jit, divisor_skips);
        return 0;
    }
    return 1;
}

static nj_jit_entry *jit_compile(struct nj_jit *jit, nj_jit_site *site)
{
    nj_u1 bytes[NJ_JIT_MAX_CODE_BYTES];
    nj_jit_block block;
    const nj_port *port = jit->vm->port;
    nj_u4 set;
    unsigned i, way;
    nj_jit_entry *e;
    void *code;
    if (!NJ_JIT_COMPILE(site->method, site->pc, bytes, sizeof(bytes), &block)) {
        site->state = JIT_UNSUPPORTED;
        jit->stats.unsupported_blocks++;
        jit->stats.declined_blocks++;
        return NULL;
    }
    /* Emission capacity may shorten the analyzed prefix. Refuse before any
     * code allocation; the independent site cache prevents repeated emission. */
    if (block.insns < NJ_JIT_MIN_BLOCK_INSNS) {
        site->state = JIT_SHORT;
        jit->stats.short_blocks++;
        jit->stats.declined_blocks++;
        return NULL;
    }
    set = jit_hash(site->method, site->pc) % JIT_CODE_SETS;
    way = jit->code_next[set];
    for (i = 0; i < NJ_JIT_CACHE_WAYS; ++i) {
        if (!jit->entries[set * NJ_JIT_CACHE_WAYS + i].code) { way = i; break; }
    }
    e = &jit->entries[set * NJ_JIT_CACHE_WAYS + way];
    if (e->code) {
        nj_jit_site *old = jit_site(jit, e->method, e->pc, 0);
        if (old) { old->state = JIT_COLD; old->hits = 0; }
        port->code_free(port->ud, e->code, e->block.size);
        jit->stats.code_bytes -= e->block.size;
        jit->stats.code_evictions++;
        memset(e, 0, sizeof(*e));
    }
    jit->code_next[set] = (nj_u1)((way + 1u) % NJ_JIT_CACHE_WAYS);
    site->state = JIT_MEMORY;
    code = port->code_alloc(port->ud, block.size);
    if (!code || ((uintptr_t)code & 3u) != 0u) {
        if (code) port->code_free(port->ud, code, block.size);
        jit->stats.memory_failures++;
        jit->stats.declined_blocks++;
        return NULL;
    }
    memcpy(code, bytes, block.size);
    if (port->code_commit(port->ud, code, block.size) != 0) {
        port->code_free(port->ud, code, block.size);
        jit->stats.commit_failures++;
        jit->stats.declined_blocks++;
        return NULL;
    }
    e->method = site->method;
    e->pc = site->pc;
    e->block = block;
    e->code = code;
    site->state = JIT_READY;
    if (site->compiled) jit->stats.recompiled_blocks++;
    site->compiled = 1;
    jit->stats.compiled_blocks++;
    jit->stats.code_bytes += block.size;
    if (jit->stats.code_bytes > jit->stats.peak_code_bytes)
        jit->stats.peak_code_bytes = jit->stats.code_bytes;
    return e;
}
#endif

int nj_jit_probe(nj_vm *vm, const nj_frame *fr, nj_u4 pc, nj_value *sp,
                 nj_u4 budget, nj_jit_ticket *ticket)
{
#if NJ_JIT_HOST_SUPPORTED
    struct nj_jit *jit = vm ? vm->jit : NULL;
    nj_jit_site *site;
    nj_u4 set;
    unsigned way;
    if (!jit || vm->engine != &jit->base || !ticket || !fr || !fr->m ||
        !fr->m->code || pc >= fr->m->code_len || !budget) return 0;
#if NJ_ENABLE_DEBUG
    if (vm->trace) return 0;
#endif
    JIT_DETAIL(jit, queries);
    ticket->entry = NULL;
    ticket->site = NULL;
    set = jit_hash(fr->m, pc) % JIT_CODE_SETS;
    for (way = 0; way < NJ_JIT_CACHE_WAYS; ++way) {
        nj_jit_entry *e = &jit->entries[set * NJ_JIT_CACHE_WAYS + way];
        if (e->code && e->method == fr->m && e->pc == pc) {
            JIT_DETAIL(jit, code_hits);
            jit->code_next[set] = (nj_u1)((way + 1u) % NJ_JIT_CACHE_WAYS);
            if (jit->mode == NJ_JIT_MODE_PROBE_ONLY) {
                JIT_DETAIL(jit, probe_only_skips);
                return 0;
            }
            if (!jit_guard(jit, &e->block, fr, sp, budget)) return 0;
            ticket->entry = e;
            return 1;
        }
    }
    site = jit_site(jit, fr->m, pc, 1);
    if (site->state >= JIT_UNSUPPORTED) {
        JIT_DETAIL(jit, negative_hits);
        return 0;
    }
    if (site->state == JIT_COLD) {
        nj_jit_block block;
        if (++site->hits < NJ_JIT_HOT_THRESHOLD) {
            JIT_DETAIL(jit, cold_skips);
            return 0;
        }
        /* Decode once at the hot threshold, without emission or callbacks.
         * Short/unsupported sites never reach SAVE or executable allocation. */
        if (!NJ_JIT_ANALYZE(fr->m, pc, &block)) {
            site->state = JIT_UNSUPPORTED;
            jit->stats.unsupported_blocks++;
            jit->stats.declined_blocks++;
            return 0;
        }
        if (block.insns < NJ_JIT_MIN_BLOCK_INSNS) {
            site->state = JIT_SHORT;
            jit->stats.short_blocks++;
            jit->stats.declined_blocks++;
            return 0;
        }
        site->insns = (nj_u2)block.insns;
        site->state = JIT_READY;
    }
    if (jit->mode == NJ_JIT_MODE_PROBE_ONLY) {
        JIT_DETAIL(jit, probe_only_skips);
        return 0;
    }
    if (site->insns > budget) {
        JIT_DETAIL(jit, budget_skips);
        return 0;
    }
    ticket->site = site;
    return 1;
#else
    (void)vm; (void)fr; (void)pc; (void)sp; (void)budget; (void)ticket;
    return 0;
#endif
}

nj_u4 nj_jit_execute(nj_vm *vm, nj_frame *fr, nj_u4 *pc, nj_value **sp,
                     nj_u4 budget, const nj_jit_ticket *ticket)
{
#if NJ_JIT_HOST_SUPPORTED
    struct nj_jit *jit = vm->jit;
    nj_jit_entry *e = ticket->entry;
    typedef void (*block_fn)(nj_value *, nj_value *);
    block_fn fn;
    typedef char pointer_size_check[sizeof(fn) == sizeof(void *) ? 1 : -1];
    (void)sizeof(pointer_size_check);
    /* A ticket is used immediately, after SAVE and without intervening Java
     * execution. Hot entries already passed the stack/divisor/budget guards. */
    if (!e) {
        e = jit_compile(jit, ticket->site);
        if (!e || !jit_guard(jit, &e->block, fr, *sp, budget)) return 0;
    }
    memcpy(&fn, &e->code, sizeof(fn));
    fn(*sp, fr->locals);
    *pc = e->block.next_pc;
    *sp += e->block.stack_delta;
    JIT_COUNT(jit, executed_blocks, 1u);
    JIT_COUNT(jit, bytecodes, e->block.insns);
#if NJ_JIT_STATS && NJ_JIT_DETAILED_STATS
    {
        nj_u4 n = e->block.insns;
        unsigned bin = n < 2u ? 0u : n < 4u ? 1u : n < 8u ? 2u : n < 16u ? 3u : 4u;
        jit->pending.block_lengths[bin]++;
    }
#endif
    return e->block.insns;
#else
    (void)vm; (void)fr; (void)pc; (void)sp; (void)budget; (void)ticket;
    return 0;
#endif
}

nj_u4 nj_jit_try_block(nj_vm *vm, nj_frame *fr, nj_u4 *pc,
                       nj_value **sp, nj_u4 budget)
{
    nj_jit_ticket ticket;
    return nj_jit_probe(vm, fr, *pc, *sp, budget, &ticket) ?
           nj_jit_execute(vm, fr, pc, sp, budget, &ticket) : 0u;
}
#endif
