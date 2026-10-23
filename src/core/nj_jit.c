/* Bounded per-VM JIT. Probing touches metadata only; compilation and executable
 * memory callbacks run only after the interpreter publishes its exact frame.
 *
 * Probe cost is the dominant installed-JIT cost on small MCUs, because a
 * passing probe runs for every bytecode position in interpreted code. The
 * heat/refusal/compiled state therefore lives in a per-method bytecode map:
 * one 16-bit cell per method offset, addressed by the frame's pc, so a probe is
 * a pointer compare (current method) plus one array load. There is no hash and
 * no scan on the hot path. Maps come from a fixed arena and are refilled by
 * evicting the least recently used method; that only drops metadata, never
 * compiled code, and a later recompile of the same (method, pc) reuses the code
 * cache entry instead of emitting it again. */
#include "nj_jit_internal.h"
#include "nj2me/nj_jit.h"

#if NJ_ENABLE_JIT
#define JIT_CODE_SETS (NJ_JIT_CACHE_ENTRIES / NJ_JIT_CACHE_WAYS)
#define JIT_ARENA_CELLS (NJ_JIT_METHOD_ARENA_BYTES / 2u)

/* Cell states, in one 16-bit word per bytecode offset:
 *   0x0000..0x3FFF  heat counter (0 = never probed)
 *   0x4000          refused: unsupported, short, or a failed compile
 *   0x6000|insns    analyzed; awaiting compile (or a probe-only measurement)
 *   0x8000|slot     compiled; low byte is the code-cache entry index        */
#define JIT_CELL_REFUSED 0x4000u
#define JIT_CELL_READY 0x6000u
#define JIT_CELL_STATE_MASK 0xe000u
#define JIT_CELL_INSNS_MASK 0x1fffu
#define JIT_CELL_COMPILED 0x8000u
#define JIT_CELL_SLOT_MASK 0xffu
#define JIT_CELL_HEAT_MASK 0x3fffu

#define JIT_COUNTERS(X) \
    X(executed_blocks) X(bytecodes) X(method_map_evictions) X(queries) \
    X(code_hits) X(cold_skips) X(negative_hits) X(probe_only_skips) \
    X(budget_skips) X(stack_skips) X(faults)

typedef struct nj_jit_entry {
    const nj_method *method;
    nj_u4 pc;
    void *code;
    nj_jit_block block;
} nj_jit_entry;

typedef struct nj_jit_method {
    const nj_method *method;
    nj_u2 off;   /* first cell in the arena, or 0xFFFF when unmapped */
    nj_u2 pcs;   /* method->code_len when mapped */
    nj_u4 usage; /* LRU stamp */
} nj_jit_method;

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
    nj_jit_method methods[NJ_JIT_METHOD_ENTRIES];
    nj_u2 cells[JIT_ARENA_CELLS];
    nj_u1 code_next[JIT_CODE_SETS];
    /* Hot-path cache: consecutive probes almost always stay in one method, so
     * the map lookup collapses to a pointer compare. Invalidated whenever the
     * arena moves a method's cells. */
    const nj_method *cur_method;
    nj_u2 *cur_map;
    nj_u2 arena_used;
    nj_u4 usage_tick;
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

static void jit_map_use(struct nj_jit *jit, nj_jit_method *slot)
{
    slot->usage = ++jit->usage_tick;
    jit->cur_method = slot->method;
    jit->cur_map = (slot->off == 0xffffu) ? NULL
                                          : &jit->cells[slot->off];
}

/* Free one method's cells and compact the arena. Only metadata is lost: any
 * code already compiled for this method stays in the code cache. */
static void jit_map_release(struct nj_jit *jit, nj_jit_method *slot)
{
    nj_u2 off = slot->off;
    nj_u2 pcs = slot->pcs;
    unsigned i;

    slot->method = NULL;
    slot->off = 0xffffu;
    slot->pcs = 0u;
    slot->usage = 0u;
    if (off == 0xffffu || pcs == 0u) {
        jit->cur_method = NULL;
        return;
    }
    for (i = 0; i < NJ_JIT_METHOD_ENTRIES; ++i) {
        nj_jit_method *o = &jit->methods[i];
        if (o->method && o->off != 0xffffu && o->off > off) {
            o->off = (nj_u2)(o->off - pcs);
        }
    }
    if ((nj_u4)off + pcs < jit->arena_used) {
        memmove(&jit->cells[off], &jit->cells[off + pcs],
                (nj_size)(jit->arena_used - off - pcs) * sizeof(nj_u2));
    }
    jit->arena_used = (nj_u2)(jit->arena_used - pcs);
    jit->cur_method = NULL; /* every remaining offset may have moved */
    jit->cur_map = NULL;
}

static nj_jit_method *jit_map_slot_find(struct nj_jit *jit, const nj_method *m)
{
    unsigned i;
    for (i = 0; i < NJ_JIT_METHOD_ENTRIES; ++i) {
        if (jit->methods[i].method == m) return &jit->methods[i];
    }
    return NULL;
}

static nj_jit_method *jit_map_slot_victim(struct nj_jit *jit,
                                          const nj_method *keep)
{
    nj_jit_method *best = NULL;
    unsigned i;
    for (i = 0; i < NJ_JIT_METHOD_ENTRIES; ++i) {
        nj_jit_method *slot = &jit->methods[i];
        if (!slot->method || slot->method == keep) continue;
        if (!best || slot->usage < best->usage) best = slot;
    }
    return best;
}

/* Map the frame's method, allocating on first use and evicting the least
 * recently used method when the arena is full. Allocation-free on the hot
 * path, because the current-method cache covers runs of the same method. */
static nj_u2 *jit_map_for(struct nj_jit *jit, const nj_method *m)
{
    nj_jit_method *slot;
    unsigned i;

    if (m == jit->cur_method) return jit->cur_map;
    slot = jit_map_slot_find(jit, m);
    if (slot) {
        jit_map_use(jit, slot);
        return jit->cur_map;
    }
    slot = NULL;
    for (i = 0; i < NJ_JIT_METHOD_ENTRIES; ++i) {
        if (!jit->methods[i].method) { slot = &jit->methods[i]; break; }
    }
    if (!slot) {
        slot = jit_map_slot_victim(jit, m);
        if (!slot) return NULL;
        jit_map_release(jit, slot);
    }
    slot->method = m;
    slot->usage = ++jit->usage_tick;
    slot->pcs = (nj_u2)((m->code_len > 0xffffu) ? 0u : m->code_len);
    if (slot->pcs == 0u || (nj_u4)slot->pcs > (nj_u4)JIT_ARENA_CELLS) {
        /* Too large to map: this method stays interpreted permanently. */
        slot->off = 0xffffu;
        slot->pcs = 0u;
        jit->cur_method = m;
        jit->cur_map = NULL;
        return NULL;
    }
    while ((nj_u4)jit->arena_used + slot->pcs > (nj_u4)JIT_ARENA_CELLS) {
        nj_jit_method *victim = jit_map_slot_victim(jit, m);
        if (!victim) {
            slot->off = 0xffffu;
            slot->pcs = 0u;
            jit->cur_method = m;
            jit->cur_map = NULL;
            return NULL;
        }
        JIT_COUNT(jit, method_map_evictions, 1u);
        jit_map_release(jit, victim);
    }
    slot->off = jit->arena_used;
    jit->arena_used = (nj_u2)(jit->arena_used + slot->pcs);
    memset(&jit->cells[slot->off], 0, (nj_size)slot->pcs * sizeof(nj_u2));
    jit_map_use(jit, slot);
    return jit->cur_map;
}

/* Drop one site's map state (its compiled code is being evicted). The current
 * method cache stays valid because cells do not move here. */
static void jit_map_forget(struct nj_jit *jit, const nj_method *m, nj_u4 pc)
{
    nj_jit_method *slot = jit_map_slot_find(jit, m);
    if (!slot || slot->off == 0xffffu || pc >= (nj_u4)slot->pcs) return;
    jit->cells[slot->off + pc] = 0u;
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
    return 1;
}

/* Find an already compiled block for this site. A probe map can be evicted by
 * the arena LRU policy while its code survives; re-analyzing the site then
 * lands here and reuses the code instead of emitting a duplicate. */
static nj_jit_entry *jit_entry_find(struct nj_jit *jit, const nj_method *m,
                                    nj_u4 pc, nj_u4 *slot_out)
{
    nj_u4 set = jit_hash(m, pc) % JIT_CODE_SETS;
    unsigned i;
    for (i = 0; i < NJ_JIT_CACHE_WAYS; ++i) {
        nj_jit_entry *e = &jit->entries[set * NJ_JIT_CACHE_WAYS + i];
        if (e->code && e->method == m && e->pc == pc) {
            if (slot_out) *slot_out = (nj_u4)(e - jit->entries);
            return e;
        }
    }
    return NULL;
}

/* Compile the site named by `cell` (or reuse its code) and mark the cell. */
static nj_jit_entry *jit_compile(struct nj_jit *jit, nj_u2 *cell,
                                 const nj_method *m, nj_u4 pc)
{
    nj_u1 bytes[NJ_JIT_MAX_CODE_BYTES];
    nj_jit_block block;
    const nj_port *port = jit->vm->port;
    nj_u4 set, slot = 0u;
    unsigned i, way;
    nj_jit_entry *e;
    void *code;

    e = jit_entry_find(jit, m, pc, &slot);
    if (e) {
        jit->stats.recompiled_blocks++;
        if (cell) *cell = (nj_u2)(JIT_CELL_COMPILED | (nj_u2)slot);
        return e;
    }
    if (!NJ_JIT_COMPILE(jit->vm, m, pc, bytes, sizeof(bytes), &block)) {
        if (cell) *cell = JIT_CELL_REFUSED;
        jit->stats.unsupported_blocks++;
        jit->stats.declined_blocks++;
        return NULL;
    }
    /* Emission capacity may shorten the analyzed prefix. Refuse before any
     * code allocation; the cell keeps the refusal. A bounded loop amortizes
     * the entry cost over many iterations, so it may be shorter than a plain
     * straight-line block. */
    if (block.insns < NJ_JIT_MIN_BLOCK_INSNS && !block.loop) {
        if (cell) *cell = JIT_CELL_REFUSED;
        jit->stats.short_blocks++;
        jit->stats.declined_blocks++;
        return NULL;
    }
    set = jit_hash(m, pc) % JIT_CODE_SETS;
    way = jit->code_next[set];
    for (i = 0; i < NJ_JIT_CACHE_WAYS; ++i) {
        if (!jit->entries[set * NJ_JIT_CACHE_WAYS + i].code) { way = i; break; }
    }
    e = &jit->entries[set * NJ_JIT_CACHE_WAYS + way];
    slot = (nj_u4)(e - jit->entries);
    if (e->code) {
        jit_map_forget(jit, e->method, e->pc);
        port->code_free(port->ud, e->code, e->block.size);
        jit->stats.code_bytes -= e->block.size;
        jit->stats.code_evictions++;
        memset(e, 0, sizeof(*e));
    }
    jit->code_next[set] = (nj_u1)((way + 1u) % NJ_JIT_CACHE_WAYS);
    code = port->code_alloc(port->ud, block.size);
    if (!code || ((uintptr_t)code & 3u) != 0u) {
        if (code) port->code_free(port->ud, code, block.size);
        if (cell) *cell = JIT_CELL_REFUSED;
        jit->stats.memory_failures++;
        jit->stats.declined_blocks++;
        return NULL;
    }
    memcpy(code, bytes, block.size);
    if (port->code_commit(port->ud, code, block.size) != 0) {
        port->code_free(port->ud, code, block.size);
        if (cell) *cell = JIT_CELL_REFUSED;
        jit->stats.commit_failures++;
        jit->stats.declined_blocks++;
        return NULL;
    }
    e->method = m;
    e->pc = pc;
    e->block = block;
    e->code = code;
    if (cell) *cell = (nj_u2)(JIT_CELL_COMPILED | (nj_u2)slot);
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
    const nj_method *m;
    nj_u2 *cell, v;
    nj_u2 *map;
    if (!jit || vm->engine != &jit->base || !ticket || !fr || !fr->m ||
        !fr->m->code || pc >= fr->m->code_len || !budget) return 0;
#if NJ_ENABLE_DEBUG
    if (vm->trace) return 0;
#endif
    JIT_DETAIL(jit, queries);
    ticket->entry = NULL;
    ticket->cell = NULL;
    m = fr->m;
    map = jit_map_for(jit, m);
    if (!map) return 0;
    cell = &map[pc];
    v = *cell;
    if ((v & JIT_CELL_COMPILED) != 0u) {
        nj_jit_entry *e = &jit->entries[v & JIT_CELL_SLOT_MASK];
        if (e->code && e->method == m && e->pc == pc) {
            JIT_DETAIL(jit, code_hits);
            if (jit->mode == NJ_JIT_MODE_PROBE_ONLY) {
                JIT_DETAIL(jit, probe_only_skips);
                return 0;
            }
            if (!jit_guard(jit, &e->block, fr, sp, budget)) return 0;
            ticket->entry = e;
            ticket->cell = cell;
            return 1;
        }
        /* The code entry was evicted and its slot reused: this site is cold
         * again, so the stale cell is cleared before the heat path. */
        *cell = 0u;
        v = 0u;
    }
    if (v == JIT_CELL_REFUSED) {
        JIT_DETAIL(jit, negative_hits);
        return 0;
    }
    if ((v & JIT_CELL_STATE_MASK) == JIT_CELL_READY) {
        nj_u2 insns = (nj_u2)(v & JIT_CELL_INSNS_MASK);
        if (jit->mode == NJ_JIT_MODE_PROBE_ONLY) {
            JIT_DETAIL(jit, probe_only_skips);
            return 0;
        }
        if ((nj_u4)insns > budget) {
            JIT_DETAIL(jit, budget_skips);
            return 0;
        }
        ticket->cell = cell;
        return 1;
    }
    /* Cold: count heat without touching anything but this cell. */
    if ((nj_u4)v + 1u < (nj_u4)NJ_JIT_HOT_THRESHOLD) {
        *cell = (nj_u2)(v + 1u);
        JIT_DETAIL(jit, cold_skips);
        return 0;
    }
    {
        /* Decode once at the hot threshold, without emission or callbacks.
         * Short/unsupported sites never reach SAVE or executable allocation. */
        nj_jit_block block;
        if (!NJ_JIT_ANALYZE(vm, m, pc, &block)) {
            *cell = JIT_CELL_REFUSED;
            jit->stats.unsupported_blocks++;
            jit->stats.declined_blocks++;
            return 0;
        }
        if (block.insns < NJ_JIT_MIN_BLOCK_INSNS && !block.loop) {
            *cell = JIT_CELL_REFUSED;
            jit->stats.short_blocks++;
            jit->stats.declined_blocks++;
            return 0;
        }
        if (jit->mode == NJ_JIT_MODE_PROBE_ONLY) {
            *cell = (nj_u2)(JIT_CELL_READY | (nj_u2)block.insns);
            JIT_DETAIL(jit, probe_only_skips);
            return 0;
        }
        if (block.insns > budget) {
            *cell = (nj_u2)(JIT_CELL_READY | (nj_u2)block.insns);
            JIT_DETAIL(jit, budget_skips);
            return 0;
        }
        /* Keep the analysis result: a site whose stack guard fails in execute
         * must not re-analyze on every probe. */
        *cell = (nj_u2)(JIT_CELL_READY | (nj_u2)block.insns);
    }
    ticket->cell = cell;
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
    nj_jit_fn fn;
    nj_value *incoming;
    nj_u4 r;
    typedef char pointer_size_check[sizeof(fn) == sizeof(void *) ? 1 : -1];
    (void)sizeof(pointer_size_check);
    /* A ticket is used immediately, after SAVE and without intervening Java
     * execution. Hot entries already passed the stack/budget guards. */
    if (!e) {
        e = jit_compile(jit, ticket->cell, fr->m, *pc);
        if (!e || !jit_guard(jit, &e->block, fr, *sp, budget)) return 0;
    }
    memcpy(&fn, &e->code, sizeof(fn));
    incoming = *sp;
    r = fn(incoming, fr->locals, budget);
    JIT_COUNT(jit, executed_blocks, 1u);
#if NJ_JIT_STATS && NJ_JIT_DETAILED_STATS
    {
        nj_u4 n = e->block.insns;
        unsigned bin = n < 2u ? 0u : n < 4u ? 1u : n < 8u ? 2u : n < 16u ? 3u : 4u;
        jit->pending.block_lengths[bin]++;
    }
#endif
    if ((r & NJ_JIT_FAULT_BIT) != 0u) {
        nj_u4 index = (r >> NJ_JIT_FAULT_INDEX_SHIFT) & NJ_JIT_FAULT_INDEX_MASK;
        nj_u4 completed = r & NJ_JIT_FAULT_COMPLETED_MASK;
        JIT_COUNT(jit, faults, 1u);
        if (index < (nj_u4)e->block.nfaults) {
            *pc = e->pc + e->block.faults[index].pc_off;
            *sp = incoming + e->block.faults[index].sp_delta;
        } else {
            /* Malformed block: treat it as a fault at entry, so the
             * interpreter re-executes the first bytecode. */
            *pc = e->pc;
            *sp = incoming;
            completed = 0u;
        }
        if (completed > budget) completed = budget;
        JIT_COUNT(jit, bytecodes, completed);
        /* The same guard failed before the block completed a single bytecode:
         * pinning the site to the interpreter avoids compile/fault churn. */
        if (completed == 0u && ticket->cell) *ticket->cell = JIT_CELL_REFUSED;
        return completed;
    }
    {
        nj_u4 which = r & NJ_JIT_EXIT_INDEX_MASK;
        nj_u4 completed = r >> NJ_JIT_EXIT_SHIFT;
        if (which >= (nj_u4)e->block.nexits || completed == 0u ||
            completed > budget) {
            /* Malformed block: let the interpreter re-enter at the entry. */
            *pc = e->pc;
            *sp = incoming;
            return 0u;
        }
        *pc = e->pc + e->block.exits[which].pc_off;
        *sp = incoming + e->block.exits[which].sp_delta;
        JIT_COUNT(jit, bytecodes, completed);
        return completed;
    }
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
