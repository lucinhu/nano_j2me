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

#if NJ_ENABLE_JIT && NJ_JIT_HOST_RV32
/* Test instrumentation only; production has no writable static state. */
static void *(*real_alloc)(void *, nj_size);
static int (*real_commit)(void *, void *, nj_size);
static void (*real_free)(void *, void *, nj_size);
static unsigned allocations, frees, attempts, commits;
static int fail_mode;
static void *expected_ud;
static void *count_alloc(void *ud, nj_size n)
{
    void *p;
    CHECK(ud == expected_ud);
    attempts++;
    if (fail_mode == 1) return NULL;
    p = real_alloc(ud, n);
    if (p) allocations++;
    return p;
}
static int count_commit(void *ud, void *p, nj_size n)
{
    CHECK(ud == expected_ud);
    commits++;
    return fail_mode == 2 ? -1 : real_commit(ud, p, n);
}
static void count_free(void *ud, void *p, nj_size n)
{
    CHECK(ud == expected_ud);
    frees++;
    real_free(ud, p, n);
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
    static const nj_u1 code[] = {0x84,0,1, 0x84,1,0xfd, 0x00, 0xa7,0xff,0xf9};
    nj_port port;
    nj_vm *vm;
    nj_thread *th;
    nj_method m;
    nj_jit_stats stats;
    result r;
    int i;
    CHECK(nj_port_headless(&port) == NJ_OK);
    expected_ud = port.ud;
    port.user = &r; /* logging state must never reach executable-memory hooks */
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
    memset(&m, 0, sizeof(m));
    m.name = "jitLoop"; m.desc = "()V";
    m.code = code; m.code_len = sizeof(code);
    m.max_locals = 2; m.max_stack = 1;
    th = nj_thread_create(vm, NULL, 0);
    CHECK(th && nj_frame_push(vm, th, &m, th->top, 0) == NJ_OK);
    vm->cur = th;
    for (i = 0; i < 4; ++i) {
        CHECK(nj_exec(vm, th, 0) == NJ_ERR_YIELD);
        CHECK(th->nframes == 1 && th->frames[0].sp == th->frames[0].stack);
    }
    r.a = th->frames[0].locals[0].i;
    r.b = th->frames[0].locals[1].i;
    r.pc = th->frames[0].pc;
    r.insns = vm->insns;
    CHECK(r.insns == (nj_u8)NJ_SCHED_QUANTUM * 4u);
    if (enabled) {
        CHECK(nj_jit_get_stats(vm, &stats));
        if (fail) {
            CHECK(stats.executed_blocks == 0 && stats.declined_blocks > 0);
            CHECK(attempts < 16); /* failed sites are negatively cached */
        } else {
            CHECK(stats.compiled_blocks > 0 && stats.executed_blocks > 0);
            CHECK(stats.bytecodes > 0 && stats.code_bytes > 0);
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
    nj_port_shutdown(&port);
    return r;
}

static void guards(void)
{
    static const nj_u1 divide[] = {0x6c, 0xac};
    static const nj_u1 push[] = {0x04, 0xac};
    nj_port port;
    nj_vm *vm;
    nj_method m;
    nj_frame fr;
    nj_value values[4], locals[1], *sp;
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
#if NJ_JIT_RV32_M
    for (i = 0; i <= NJ_JIT_HOT_THRESHOLD; ++i) {
        values[0].i = (-2147483647 - 1); values[1].i = -1;
        sp = values + 2; pc = 0;
        n = nj_jit_try_block(vm, &fr, &pc, &sp, 2);
    }
    CHECK(n == 1 && pc == 1 && sp == values + 1);
    CHECK(values[0].i == (-2147483647 - 1));
    values[0].i = 7; values[1].i = 0;
    sp = values + 2; pc = 0;
    CHECK(nj_jit_try_block(vm, &fr, &pc, &sp, 2) == 0);
    CHECK(pc == 0 && sp == values + 2 && values[0].i == 7);
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
    m.code = push; m.code_len = sizeof(push);
    for (i = 0; i <= NJ_JIT_HOT_THRESHOLD; ++i) {
        sp = values + 2; pc = 0;
        CHECK(nj_jit_try_block(vm, &fr, &pc, &sp, 1) == 0); /* overflow */
        CHECK(pc == 0 && sp == values + 2);
    }
    sp = values; pc = 0;
    n = nj_jit_try_block(vm, &fr, &pc, &sp, 1);
    CHECK(n == 1 && pc == 1 && sp == values + 1 && values[0].i == 1);
#if NJ_ENABLE_DEBUG
    vm->trace = 1;
    sp = values; pc = 0;
    CHECK(nj_jit_try_block(vm, &fr, &pc, &sp, 1) == 0);
#endif
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
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
#if NJ_JIT_HOST_RV32
    {
        result a = run_loop(0, 0), b = run_loop(1, 0);
        result c = run_loop(1, 1), d = run_loop(1, 2);
        CHECK(a.a == b.a && a.b == b.b && a.pc == b.pc && a.insns == b.insns);
        CHECK(a.a == c.a && a.b == c.b && a.pc == c.pc && a.insns == c.insns);
        CHECK(a.a == d.a && a.b == d.b && a.pc == d.pc && a.insns == d.insns);
        guards();
        puts("PASS JIT runtime: RV32 native execution, quanta, guards, ownership, failures");
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
