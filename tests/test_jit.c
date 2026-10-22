/* Runtime integration: same quanta/frames with and without native execution,
 * executable-memory failures, cache ownership, zero-divisor and stack guards. */
#include "nj_internal.h"
#include "nj_jit_internal.h"
#include "nj2me/nj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); \
} } while (0)

#if NJ_ENABLE_JIT && NJ_JIT_HOST_SUPPORTED
/* Test instrumentation only; production has no writable static state. */
static void *(*real_alloc)(void *, nj_size);
static int (*real_commit)(void *, void *, nj_size);
static void (*real_free)(void *, void *, nj_size);
static unsigned allocations, frees, attempts, commits;
static int fail_mode;
static void *expected_ud;
static void *forbidden_ud;
static void *real_ud;
static int ud_sentinel, user_sentinel;
static nj_vm *active_vm;
static const nj_method *active_method;
static unsigned loop_insns;
static void check_saved_frame(void)
{
    if (active_vm) {
        const nj_frame *fr = &active_vm->cur->frames[0];
        unsigned ordinal = fr->pc == 0 ? 0 : fr->pc == 3 ? 1 : fr->pc - 4;
        CHECK(fr->m == active_method && fr->sp == fr->stack);
        CHECK(fr->pc < fr->m->code_len);
        CHECK(active_vm->insns % loop_insns == ordinal);
    }
}
static void *count_alloc(void *ud, nj_size n)
{
    void *p;
    CHECK(ud == expected_ud && ud != forbidden_ud);
    check_saved_frame();
    attempts++;
    if (fail_mode == 1) return NULL;
    p = real_alloc(real_ud, n);
    if (p) allocations++;
    return p;
}
static int count_commit(void *ud, void *p, nj_size n)
{
    CHECK(ud == expected_ud && ud != forbidden_ud);
    check_saved_frame();
    commits++;
    return fail_mode == 2 ? -1 : real_commit(real_ud, p, n);
}
static void count_free(void *ud, void *p, nj_size n)
{
    CHECK(ud == expected_ud && ud != forbidden_ud);
    check_saved_frame();
    frees++;
    real_free(real_ud, p, n);
}

typedef struct result {
    nj_s4 a, b;
    nj_u4 pc;
    nj_u8 insns;
} result;

static result run_loop(int enabled, int fail)
{
    /* No interpreter fusion matches this loop, so quanta are comparable even
     * with NJ_ENABLE_SUPERINSTR=1. The branch is an interpreter boundary. */
    nj_u1 code[NJ_JIT_MIN_BLOCK_INSNS + 12];
    unsigned body = NJ_JIT_MIN_BLOCK_INSNS < 3 ? 3u : NJ_JIT_MIN_BLOCK_INSNS;
    unsigned end = body + 4u;
    int back = -(int)end;
    nj_port port;
    nj_vm *vm;
    nj_thread *th;
    nj_method m;
    nj_jit_stats stats;
    result r;
    void *saved_ud, *saved_user;
    int i;
    CHECK(nj_port_headless(&port) == NJ_OK);
    saved_ud = port.ud;
    saved_user = port.user;
    real_ud = port.ud;
    expected_ud = &ud_sentinel;
    forbidden_ud = &user_sentinel;
    /* The executable-memory callbacks must receive port->ud, never port->user;
     * keep two distinct sentinels so either mix-up fails the CHECK in the hook. */
    port.ud = &ud_sentinel;
    port.user = &user_sentinel;
    real_alloc = port.code_alloc;
    real_commit = port.code_commit;
    real_free = port.code_free;
    port.code_alloc = count_alloc;
    port.code_commit = count_commit;
    port.code_free = count_free;
    attempts = allocations = frees = commits = 0;
    fail_mode = fail;
    vm = nj_vm_create(&port, 1024u * 1024u);
    CHECK(vm && nj_jit_get_stats(vm, &stats));
    CHECK(nj_jit_enable(vm) == NJ_OK); /* idempotent */
    if (!enabled) nj_jit_disable(vm);
    if (enabled == 2) CHECK(nj_jit_set_mode(vm, NJ_JIT_MODE_PROBE_ONLY) == NJ_OK);
    memset(code, 0, sizeof(code));
    code[0] = 0x84; code[1] = 0; code[2] = 1;
    code[3] = 0x84; code[4] = 1; code[5] = 0xfd;
    code[end] = 0xa7;
    code[end + 1u] = (nj_u1)((unsigned)back >> 8);
    code[end + 2u] = (nj_u1)back;
    memset(&m, 0, sizeof(m));
    m.name = "jitLoop"; m.desc = "()V";
    m.code = code; m.code_len = end + 3u;
    m.max_locals = 2; m.max_stack = 1;
    th = nj_thread_create(vm, NULL, 0);
    CHECK(th && nj_frame_push(vm, th, &m, th->top, 0) == NJ_OK);
    vm->cur = th;
    active_vm = vm;
    active_method = &m;
    loop_insns = body + 1u;
    for (i = 0; i < 4; ++i) {
        CHECK(nj_exec(vm, th, 0) == NJ_ERR_YIELD);
        CHECK(th->nframes == 1 && th->frames[0].sp == th->frames[0].stack);
    }
    active_vm = NULL;
    r.a = th->frames[0].locals[0].i;
    r.b = th->frames[0].locals[1].i;
    r.pc = th->frames[0].pc;
    r.insns = vm->insns;
    CHECK(r.insns == (nj_u8)NJ_SCHED_QUANTUM * 4u);
    if (enabled) {
        CHECK(nj_jit_get_stats(vm, &stats));
        CHECK(stats.metadata_bytes >= NJ_JIT_METHOD_ARENA_BYTES &&
              stats.stats_enabled == NJ_JIT_STATS);
        CHECK(stats.detailed_stats_enabled == NJ_JIT_DETAILED_STATS);
        if (enabled == 2) {
            CHECK(attempts == 0 && commits == 0 && frees == 0);
            CHECK(stats.compiled_blocks == 0 && stats.executed_blocks == 0);
            CHECK(stats.bytecodes == 0 && stats.code_bytes == 0);
#if NJ_JIT_DETAILED_STATS
            CHECK(stats.queries > 0 && stats.probe_only_skips > 0);
#endif
        } else if (fail) {
            CHECK(stats.executed_blocks == 0 && stats.declined_blocks > 0);
            CHECK(attempts < 16); /* failed sites are negatively cached */
            CHECK(fail == 1 ? stats.memory_failures > 0 : stats.commit_failures > 0);
        } else {
            CHECK(stats.compiled_blocks > 0 && stats.code_bytes > 0);
            CHECK(stats.peak_code_bytes >= stats.code_bytes);
#if NJ_JIT_STATS
            CHECK(stats.executed_blocks > 0 && stats.bytecodes > 0);
#else
            CHECK(stats.executed_blocks == 0 && stats.bytecodes == 0);
#endif
#if NJ_JIT_DETAILED_STATS
            nj_u8 total = 0;
            unsigned bin;
            CHECK(stats.queries > 0 && stats.code_hits > 0);
            for (bin = 0; bin < 5; ++bin) total += stats.block_lengths[bin];
            CHECK(total == stats.executed_blocks);
#endif
        }
        /* Replacing the borrowed engine never transfers JIT ownership. */
        CHECK(nj_engine_install(vm, nj_engine_null()) == NJ_OK);
        nj_jit_disable(vm);
        CHECK(nj_engine_current(vm) == nj_engine_null());
        CHECK(nj_jit_enable(vm) == NJ_ERR);
        nj_engine_uninstall(vm);
        CHECK(nj_jit_enable(vm) == NJ_OK);
        CHECK(nj_jit_get_stats(vm, &stats) && stats.compiled_blocks == 0);
    }
    nj_vm_destroy(vm);
    CHECK(allocations == frees);
    port.ud = saved_ud;
    port.user = saved_user;
    nj_port_shutdown(&port);
    return r;
}

static void guards(void)
{
    static const nj_u1 divide[] = {0x6c, 0xac};
    nj_u1 push[NJ_JIT_MIN_BLOCK_INSNS + 1];
    nj_port port;
    nj_vm *vm;
    nj_method m;
    nj_frame fr;
    nj_value values[4], locals[1], *sp;
    nj_jit_stats stats;
    nj_u4 pc, i, n;
    CHECK(nj_port_headless(&port) == NJ_OK);
    vm = nj_vm_create(&port, 1024u * 1024u);
    CHECK(vm && nj_jit_enable(vm) == NJ_OK);
    memset(&m, 0, sizeof(m));
    memset(&fr, 0, sizeof(fr));
    memset(values, 0, sizeof(values));
    m.name = "jitDivide"; m.desc = "()I";
    m.code = divide; m.code_len = sizeof(divide); m.max_stack = 2;
    fr.m = &m; fr.locals = locals; fr.stack = values; fr.stack_limit = values + 2;
#if NJ_JIT_MIN_BLOCK_INSNS == 1 && (NJ_JIT_HOST_ARM64 || NJ_JIT_RV32_M)
    for (i = 0; i <= NJ_JIT_HOT_THRESHOLD; ++i) {
        values[0].i = (-2147483647 - 1); values[1].i = -1;
        sp = values + 2; pc = 0;
        n = nj_jit_try_block(vm, &fr, &pc, &sp, 2);
    }
    CHECK(n == 1 && pc == 1 && sp == values + 1);
    CHECK(values[0].i == (-2147483647 - 1));
    values[0].i = 7; values[1].i = 0;
    sp = values + 2; pc = 0;
    n = nj_jit_try_block(vm, &fr, &pc, &sp, 2);
    /* The C entry translates the block's fault word into a completed count:
     * zero here means the interpreter must re-execute the first bytecode. */
    CHECK(n == 0);
    CHECK(pc == 0 && sp == values + 2 && values[0].i == 7);
#if NJ_JIT_STATS
    CHECK(nj_jit_get_stats(vm, &stats) && stats.faults == 1);
#endif
    values[1].i = 1;
    CHECK(nj_jit_try_block(vm, &fr, &pc, &sp, 0) == 0);
    sp = values + 1;
    CHECK(nj_jit_try_block(vm, &fr, &pc, &sp, 2) == 0); /* underflow */
#endif
    {
        nj_thread *th = nj_thread_create(vm, NULL, 0);
        CHECK(th && nj_frame_push(vm, th, &m, th->top, 0) == NJ_OK);
        th->frames[0].stack[0].i = 7;
        th->frames[0].stack[1].i = 0;
        th->frames[0].sp += 2;
        vm->cur = th;
        CHECK(nj_exec(vm, th, 0) == NJ_ERR_HALT);
        CHECK(nj_has_exception(vm));
        CHECK(strcmp(nj_ref_class_name(nj_take_exception(vm)),
                     "java/lang/ArithmeticException") == 0);
        CHECK(th->nframes == 0);
    }
    /* Clear cached immutable metadata before reusing the method structure. */
    nj_jit_disable(vm);
    CHECK(nj_jit_enable(vm) == NJ_OK);
    memset(push, 0, sizeof(push));
    push[0] = 0x04; push[NJ_JIT_MIN_BLOCK_INSNS] = 0xac;
    m.code = push; m.code_len = sizeof(push);
    for (i = 0; i <= NJ_JIT_HOT_THRESHOLD; ++i) {
        sp = values + 2; pc = 0;
        CHECK(nj_jit_try_block(vm, &fr, &pc, &sp, NJ_JIT_MIN_BLOCK_INSNS) == 0);
        CHECK(pc == 0 && sp == values + 2); /* overflow */
    }
    sp = values; pc = 0;
    CHECK(nj_jit_try_block(vm, &fr, &pc, &sp, NJ_JIT_MIN_BLOCK_INSNS - 1) == 0);
    CHECK(pc == 0 && sp == values);
    n = nj_jit_try_block(vm, &fr, &pc, &sp, NJ_JIT_MIN_BLOCK_INSNS);
    CHECK(n == NJ_JIT_MIN_BLOCK_INSNS && pc == NJ_JIT_MIN_BLOCK_INSNS);
    CHECK(sp == values + 1 && values[0].i == 1);
#if NJ_ENABLE_DEBUG
    vm->trace = 1;
    sp = values; pc = 0;
    CHECK(nj_jit_try_block(vm, &fr, &pc, &sp, 1) == 0);
#endif
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
}

static void probe_contract(void)
{
    nj_u1 code[NJ_JIT_MIN_BLOCK_INSNS + 5];
    nj_method m;
    nj_frame fr, saved;
    nj_value values[4], before[4], locals[1], *sp;
    nj_jit_ticket ticket;
    nj_jit_stats stats;
    nj_port port;
    nj_vm *vm;
    unsigned i;
    nj_u4 pc;
    void *saved_ud, *saved_user;
    memset(code, 0, sizeof(code));
    code[NJ_JIT_MIN_BLOCK_INSNS] = 0xb1;
    code[NJ_JIT_MIN_BLOCK_INSNS + 1] = 0x5a; /* unsupported dup_x1 */
    code[NJ_JIT_MIN_BLOCK_INSNS + 2] = 0x04; /* deliberately short */
    code[NJ_JIT_MIN_BLOCK_INSNS + 3] = 0xac;
    memset(&m, 0, sizeof(m));
    m.code = code; m.code_len = sizeof(code); m.max_stack = 4;
    memset(&fr, 0, sizeof(fr));
    memset(values, 0xa5, sizeof(values));
    memcpy(before, values, sizeof(values));
    fr.m = &m; fr.locals = locals; fr.stack = values; fr.stack_limit = values + 4;
    fr.pc = 777; fr.sp = values + 3; /* stale published state is intentional */
    saved = fr;
    CHECK(nj_port_headless(&port) == NJ_OK);
    saved_ud = port.ud;
    saved_user = port.user;
    real_ud = port.ud;
    expected_ud = &ud_sentinel;
    forbidden_ud = &user_sentinel;
    port.ud = &ud_sentinel;
    port.user = &user_sentinel;
    real_alloc = port.code_alloc; real_commit = port.code_commit; real_free = port.code_free;
    port.code_alloc = count_alloc; port.code_commit = count_commit; port.code_free = count_free;
    attempts = allocations = commits = frees = 0; fail_mode = 0;
    vm = nj_vm_create(&port, 1024u * 1024u);
    CHECK(vm && nj_jit_set_mode(vm, NJ_JIT_MODE_PROBE_ONLY) == NJ_OK);
    /* Heat each per-method PC cell independently; keep the ready site last. */
    for (unsigned site = 0; site < 3u; ++site) {
        nj_u4 probe_pc = site < 2u ? NJ_JIT_MIN_BLOCK_INSNS + 1u + site : 0u;
#if NJ_JIT_MIN_BLOCK_INSNS == 1
        if (site == 1u) continue;
#endif
        for (i = 0; i < NJ_JIT_HOT_THRESHOLD + 5u; ++i) {
            CHECK(!nj_jit_probe(vm, &fr, probe_pc, values, 128, &ticket));
            CHECK(memcmp(&fr, &saved, sizeof(fr)) == 0);
            CHECK(memcmp(values, before, sizeof(values)) == 0);
            CHECK(attempts == 0 && commits == 0 && frees == 0);
        }
    }
    CHECK(nj_jit_get_stats(vm, &stats) && stats.unsupported_blocks == 1);
#if NJ_JIT_MIN_BLOCK_INSNS > 1
    CHECK(stats.short_blocks == 1);
#endif
#if NJ_JIT_DETAILED_STATS
    /* The unsupported site was probed several times after its refusal: every
     * later probe must take the negative-cache path, not re-analyze. */
    CHECK(stats.negative_hits >= 1);
#endif
    CHECK(nj_jit_set_mode(vm, (nj_jit_mode)99) == NJ_ERR);
    CHECK(nj_jit_set_mode(vm, NJ_JIT_MODE_EXECUTE) == NJ_OK);
    CHECK(nj_jit_probe(vm, &fr, 0, values, 128, &ticket));
    CHECK(attempts == 0 && commits == 0); /* even a ready ticket is callback-free */
    pc = fr.pc = 0; sp = fr.sp = values;
    CHECK(nj_jit_execute(vm, &fr, &pc, &sp, 128, &ticket) == NJ_JIT_MIN_BLOCK_INSNS);
    CHECK(attempts == 1 && commits == 1 && frees == 0);
    CHECK(nj_jit_get_stats(vm, &stats) && stats.compiled_blocks == 1);
#if NJ_JIT_STATS
    /* Snapshot includes the not-yet-flushed batch, even outside nj_exec. */
    CHECK(stats.executed_blocks == 1 && stats.bytecodes == NJ_JIT_MIN_BLOCK_INSNS);
#endif
    CHECK(nj_jit_set_mode(vm, NJ_JIT_MODE_PROBE_ONLY) == NJ_OK);
    CHECK(!nj_jit_probe(vm, &fr, 0, values, 128, &ticket));
    CHECK(attempts == 1 && frees == 0);
    nj_vm_destroy(vm);
    CHECK(allocations == frees);
    port.ud = saved_ud;
    port.user = saved_user;
    nj_port_shutdown(&port);
}

static void cache_collisions(void)
{
    enum { NSITES = NJ_JIT_CACHE_WAYS + 1 };
    nj_u4 pcs[NSITES], pc, count = 0, target, i, j;
    /* The probe map holds one cell per bytecode offset from a fixed arena, so
     * keep this synthetic method small enough to be mapped (and therefore
     * JIT-eligible); the code cache is still exercised through hash collisions. */
    enum { CODE_LEN = 512 };
    nj_size size = CODE_LEN;
    nj_u1 *code = malloc(size);
    nj_method m;
    nj_frame fr;
    nj_value values[1], *sp;
    nj_port port;
    nj_vm *vm;
    nj_jit_stats stats;
    CHECK(code);
    memset(code, 0xff, size); memset(&m, 0, sizeof(m)); memset(&fr, 0, sizeof(fr));
    m.code = code; m.code_len = (nj_u4)size; m.max_stack = 1;
    /* Select disjoint bytecode starts in one code-cache set. The assertion
     * below tests retention/eviction, not the particular hash distribution. */
    target = (nj_u4)((uintptr_t)&m >> 3) % (NJ_JIT_CACHE_ENTRIES / NJ_JIT_CACHE_WAYS);
    for (pc = 0; pc + NJ_JIT_MIN_BLOCK_INSNS < size && count < NSITES; ++pc) {
        nj_u4 hash = (nj_u4)(((uintptr_t)&m >> 3) ^ (pc * 2654435761u));
        if (hash % (NJ_JIT_CACHE_ENTRIES / NJ_JIT_CACHE_WAYS) != target) continue;
        pcs[count++] = pc;
        memset(code + pc, 0, NJ_JIT_MIN_BLOCK_INSNS);
        pc += NJ_JIT_MIN_BLOCK_INSNS;
    }
    CHECK(count == NSITES);
    fr.m = &m; fr.locals = values; fr.stack = values; fr.stack_limit = values + 1;
    CHECK(nj_port_headless(&port) == NJ_OK);
    vm = nj_vm_create(&port, 1024u * 1024u);
    CHECK(vm && nj_jit_enable(vm) == NJ_OK);
    for (i = 0; i < NJ_JIT_CACHE_WAYS; ++i) {
        for (j = 0; j < NJ_JIT_HOT_THRESHOLD; ++j) {
            pc = pcs[i]; sp = values;
            (void)nj_jit_try_block(vm, &fr, &pc, &sp, 128);
        }
    }
    CHECK(nj_jit_get_stats(vm, &stats));
    CHECK(stats.compiled_blocks == NJ_JIT_CACHE_WAYS && stats.code_evictions == 0);
    for (i = 0; i < NJ_JIT_CACHE_WAYS; ++i) {
        pc = pcs[i]; sp = values;
        CHECK(nj_jit_try_block(vm, &fr, &pc, &sp, 128) == NJ_JIT_MIN_BLOCK_INSNS);
    }
    for (j = 0; j < NJ_JIT_HOT_THRESHOLD; ++j) {
        pc = pcs[NJ_JIT_CACHE_WAYS]; sp = values;
        (void)nj_jit_try_block(vm, &fr, &pc, &sp, 128);
    }
    CHECK(nj_jit_get_stats(vm, &stats));
    CHECK(stats.compiled_blocks == NSITES && stats.code_evictions == 1);
    /* The oldest site must heat again; its retained history identifies a
     * recompile. With tiny history configurations the lower bound may be 0. */
    for (j = 0; j < NJ_JIT_HOT_THRESHOLD; ++j) {
        pc = pcs[0]; sp = values;
        (void)nj_jit_try_block(vm, &fr, &pc, &sp, 128);
    }
    CHECK(nj_jit_get_stats(vm, &stats));
    CHECK(stats.compiled_blocks == NSITES + 1 && stats.code_evictions == 2);
#if NJ_JIT_STATS
    /* Evicting code clears the site's map cell, so the reheat recompiles
     * instead of reusing code; the small workload never overflows the map. */
    CHECK(stats.method_map_evictions == 0);
    CHECK(stats.recompiled_blocks == 0);
#endif
    nj_vm_destroy(vm); nj_port_shutdown(&port); free(code);
}

/* Exercise the method-map arena and the code-reuse path that is only reachable
 * after an LRU map eviction: the compiled code survives in the code cache while
 * its probe cells are released, so a reheat must reuse -- not duplicate -- it. */
static void method_maps(void)
{
    enum {
        FILL_LEN = (int)(NJ_JIT_METHOD_ARENA_BYTES / 32),
        CELLS = (int)(NJ_JIT_METHOD_ARENA_BYTES / 2),
        FILL_COUNT = CELLS / FILL_LEN
    };
    nj_u1 *code;
    nj_method *methods;
    nj_method *a;
    nj_port port;
    nj_vm *vm;
    nj_frame fr;
    nj_value values[1], locals[1], *sp;
    nj_jit_ticket ticket;
    nj_jit_stats stats;
    nj_u4 pc;
    unsigned i;

    if (NJ_JIT_METHOD_ENTRIES < FILL_COUNT + 1 ||
        FILL_LEN < NJ_JIT_MIN_BLOCK_INSNS) {
        /* A tiny method table cannot demonstrate arena LRU without also
         * exercising table eviction, which is a different policy. */
        puts("SKIP JIT method-map arena: method table too small");
        return;
    }
    code = calloc((size_t)(FILL_COUNT + 1) * (size_t)FILL_LEN, 1);
    methods = calloc((size_t)FILL_COUNT + 1u, sizeof(*methods));
    CHECK(code && methods);
    for (i = 0; i <= FILL_COUNT; ++i) {
        methods[i].code = code + (size_t)i * (size_t)FILL_LEN;
        methods[i].code_len = (nj_u4)FILL_LEN;
        methods[i].max_stack = 0;
    }
    a = &methods[0];
    memset(&fr, 0, sizeof(fr));
    memset(values, 0, sizeof(values));
    memset(locals, 0, sizeof(locals));
    fr.m = a; fr.locals = locals; fr.stack = values; fr.stack_limit = values + 1;

    CHECK(nj_port_headless(&port) == NJ_OK);
    vm = nj_vm_create(&port, 1024u * 1024u);
    CHECK(vm && nj_jit_enable(vm) == NJ_OK);
    for (i = 0; i <= NJ_JIT_HOT_THRESHOLD; ++i) {
        pc = 0; sp = values;
        (void)nj_jit_try_block(vm, &fr, &pc, &sp, 128);
    }
    CHECK(nj_jit_get_stats(vm, &stats));
    CHECK(stats.compiled_blocks == 1 && stats.recompiled_blocks == 0);
    CHECK(stats.method_map_evictions == 0 && stats.code_evictions == 0);

    /* Probe-only maps each filler without emitting code. The arena holds
     * CELLS entries, so the last filler evicts A's map. */
    CHECK(nj_jit_set_mode(vm, NJ_JIT_MODE_PROBE_ONLY) == NJ_OK);
    for (i = 1; i <= FILL_COUNT; ++i) {
        fr.m = &methods[i];
        CHECK(nj_jit_probe(vm, &fr, 0, values, 128, &ticket) == 0);
    }
    CHECK(nj_jit_get_stats(vm, &stats));
    CHECK(stats.method_map_evictions == 1);
    CHECK(stats.compiled_blocks == 1 && stats.recompiled_blocks == 0);
    CHECK(stats.code_bytes > 0);

    /* Reheat A in execute mode. Its map is gone, but its code entry is still
     * present; jit_compile must find it and count a reuse instead of emitting a
     * second block. Re-creating A's map evicts one filler (the second LRU). */
    CHECK(nj_jit_set_mode(vm, NJ_JIT_MODE_EXECUTE) == NJ_OK);
    fr.m = a;
    for (i = 0; i <= NJ_JIT_HOT_THRESHOLD; ++i) {
        pc = 0; sp = values;
        (void)nj_jit_try_block(vm, &fr, &pc, &sp, 128);
    }
    CHECK(nj_jit_get_stats(vm, &stats));
    CHECK(stats.method_map_evictions == 2);
    CHECK(stats.recompiled_blocks == 1);
    CHECK(stats.compiled_blocks == 1);
    CHECK(stats.code_evictions == 0);
    CHECK(stats.code_bytes > 0 && stats.code_bytes == stats.peak_code_bytes);

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    free(methods);
    free(code);
}
#endif

int main(void)
{
#if NJ_ENABLE_JIT
    nj_port port;
    nj_vm *vm;
    nj_jit_stats stats;
    CHECK(nj_port_headless(&port) == NJ_OK);
    port.code_alloc = NULL; /* incomplete port must stay interpreted */
    vm = nj_vm_create(&port, 1024u * 1024u);
    CHECK(vm && !nj_jit_get_stats(vm, &stats));
    CHECK(nj_jit_enable(vm) == NJ_ERR_UNSUPPORTED);
    CHECK(stats.compiled_blocks == 0 && stats.code_bytes == 0);
    nj_jit_disable(vm);
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
#if NJ_JIT_HOST_SUPPORTED
    {
        result a = run_loop(0, 0), b = run_loop(1, 0);
        result c = run_loop(1, 1), d = run_loop(1, 2), e = run_loop(2, 0);
        CHECK(a.a == b.a && a.b == b.b && a.pc == b.pc && a.insns == b.insns);
        CHECK(a.a == c.a && a.b == c.b && a.pc == c.pc && a.insns == c.insns);
        CHECK(a.a == d.a && a.b == d.b && a.pc == d.pc && a.insns == d.insns);
        CHECK(a.a == e.a && a.b == e.b && a.pc == e.pc && a.insns == e.insns);
        guards();
        probe_contract();
        cache_collisions();
        method_maps();
        puts("PASS JIT runtime: probe/execute modes, SAVE, quanta, guards, method map, callbacks, cache, ownership, failures");
    }
#else
    CHECK(nj_port_headless(&port) == NJ_OK);
    vm = nj_vm_create(&port, 1024u * 1024u);
    CHECK(vm && nj_jit_enable(vm) == NJ_ERR_UNSUPPORTED);
    CHECK(!nj_jit_get_stats(vm, &stats));
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    puts("PASS JIT runtime: unsupported host and missing port fallback");
#endif
#else
    puts("PASS JIT disabled build");
#endif
    return 0;
}
