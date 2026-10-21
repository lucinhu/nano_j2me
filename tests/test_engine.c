/*
 * nano_j2me - tests/test_engine.c
 *
 * The pluggable execution engine must be semantics-preserving: installing the
 * null engine (which forwards to nj_interp_run) may change how a program's
 * bytecodes are executed, but not WHAT it computes, how many bytecodes it
 * retires, or how much memory the VM releases at teardown.
 *
 * The workload is tests/fixtures/bench.jar: a compute loop whose answer
 * (`BENCH sum=-136042548 s=5`) is the project's cross-machine behaviour
 * comparand (docs/PERFORMANCE.md).  Its dispatch count is deliberately NOT
 * pinned here: the total is a property of the configured interpreter
 * (NJ_ENABLE_SUPERINSTR and friends) and moves whenever a fusion is added.
 * What must hold is that every engine retires the same count as the
 * interpreter in the same build, so the runs are compared against the
 * baseline below.
 *
 * The test runs the same MIDlet four times in one process:
 *
 *   1. baseline           - no engine installed
 *   2. null engine        - nj_engine_null(), the built-in forwarder
 *   3. probe engine       - a wrapper defined here that counts run() calls and
 *                           delegates to nj_interp_run()
 *   4. broken engine      - ops table with a NULL run hook; nj_exec() must
 *                           fall back to the interpreter instead of crashing
 *
 * and asserts that all four produce byte-identical program output, the same
 * vm->insns count, and the same number of host allocations released by
 * nj_vm_destroy().
 *
 * With NJ_ENABLE_ENGINE=0 there is nothing to test (the API does not exist),
 * so the binary prints SKIP and exits 0.  Registering it in both build
 * configurations is deliberate: it proves the source compiles with the feature
 * off as well.
 *
 * Usage:  test_engine <bench.jar>
 * Exit code 0 on PASS/SKIP, 1 on failure.
 */
#include "nj2me/nj.h"
#include "nj_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BENCH_LINE  "BENCH sum=-136042548 s=5"
/*
 * Sanity floor, not a reference count.  The benchmark retires tens of
 * millions of dispatches on every configuration; a total near zero means the
 * run did not happen or vm->insns stopped being counted.  The exact total is
 * whatever this build's superinstruction set makes it (docs/JIT.md section
 * "Observable equivalence"), which is why the engine comparisons below use
 * the baseline run instead of a constant.
 */
#define BENCH_INSNS_MIN 1000000ULL

static int g_pass;
static int g_fail;

#define OK_MSG(cond, ...)                                                     \
    do {                                                                      \
        if (cond) {                                                           \
            g_pass++;                                                         \
        } else {                                                              \
            g_fail++;                                                         \
            fprintf(stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);   \
            fprintf(stderr, __VA_ARGS__);                                     \
            fputc('\n', stderr);                                              \
        }                                                                     \
    } while (0)

#define OK(cond)                                                              \
    do {                                                                      \
        if (cond) {                                                           \
            g_pass++;                                                         \
        } else {                                                              \
            g_fail++;                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                     \
    } while (0)

/* ------------------------------------------------------------------ */
/* Capture the MIDlet's program output                                 */
/* ------------------------------------------------------------------ */
#define CAP_MAX 8192
typedef struct cap_state {
    char  buf[CAP_MAX];
    nj_u4 len;
    int   overflow;
} cap_state;

static void cap_log(void *ud, int level, const char *msg)
{
    cap_state *c = (cap_state *)ud;
    nj_u4 n;

    (void)level;
    if (!msg) {
        return;
    }
    n = (nj_u4)strlen(msg);
    if (c->len + n + 2u >= CAP_MAX) {
        c->overflow = 1;
        return;
    }
    memcpy(c->buf + c->len, msg, n);
    c->len += n;
    c->buf[c->len++] = '\n';
    c->buf[c->len] = '\0';
}

static unsigned char *read_file(const char *path, nj_u4 *out_len)
{
    FILE *f = fopen(path, "rb");
    long n;
    unsigned char *buf;

    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    n = ftell(f);
    if (n <= 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (unsigned char *)malloc((size_t)n);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1u, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_len = (nj_u4)n;
    return buf;
}

/*
 * In an NJ_ENABLE_ENGINE=0 build the engine type does not exist; the helper
 * only ever receives NULL there, so spell its parameter as void* and keep the
 * file warning-free in both configurations.
 */
#if NJ_ENABLE_ENGINE
typedef struct nj_engine engine_arg;
#else
typedef void engine_arg;
#endif

typedef struct run_result {
    cap_state cap;
    nj_u8     insns;      /* vm->insns, the bytecode counter behind */
    nj_u8     natives;    /* native calls, also compared */
    nj_u4     released;   /* host blocks freed by nj_vm_destroy() */
    int       status;     /* nj_vm_start_app() result */
} run_result;

/*
 * One create / load / construct / startApp / destroy cycle on bench.Bench.
 *
 * `engine`, when non-NULL, is installed on the VM before any Java runs, so
 * even the MIDlet's <init> and the bench static initialiser go through it.
 */
static int run_bench(const unsigned char *jar, nj_u4 jar_len,
                     engine_arg *engine, run_result *out)
{
    nj_port port;
    nj_vm *vm;
    nj_app *app;
    nj_ref midlet;
    nj_ref exc = NULL;
    nj_vm_stats stats;
    int rc = 1;

    memset(out, 0, sizeof(*out));
    memset(&port, 0, sizeof(port));

    if (nj_port_headless(&port) != NJ_OK) {
        fprintf(stderr, "headless port init failed\n");
        return 1;
    }
    port.log = cap_log;
    port.user = &out->cap;

    vm = nj_vm_create(&port, 16u * 1024u * 1024u);
    if (!vm) {
        fprintf(stderr, "vm create failed\n");
        nj_port_shutdown(&port);
        return 1;
    }

#if NJ_ENABLE_ENGINE
    if (engine) {
        OK(nj_engine_current(vm) == NULL);
        OK(nj_engine_install(vm, engine) == NJ_OK);
        OK(nj_engine_current(vm) == engine);
    }
#else
    (void)engine;
#endif

    app = nj_app_open(vm, jar, jar_len);
    if (!app) {
        fprintf(stderr, "app open failed\n");
        goto done;
    }

    midlet = nj_vm_new_midlet(vm, "bench.Bench", &exc);
    if (!midlet) {
        fprintf(stderr, "cannot instantiate bench.Bench: %s\n",
                exc ? nj_ref_class_name(exc) : "(no exception)");
        goto done;
    }

    out->status = (int)nj_vm_start_app(vm, midlet);
    if (vm->exception) {
        fprintf(stderr, "startApp raised: %s\n",
                nj_exception_describe(vm) ? nj_exception_describe(vm) : "(?)");
        nj_clear_exception(vm);
        goto done;
    }

#if NJ_ENABLE_ENGINE
    if (engine) {
        /* The install is per-VM and removable without disturbing the VM. */
        OK(nj_engine_uninstall(vm) == engine);
        OK(nj_engine_current(vm) == NULL);
        OK(nj_engine_install(vm, engine) == NJ_OK);
    }
#endif

    rc = 0;

done:
#if NJ_ENABLE_ENGINE
    if (engine) {
        nj_engine_uninstall(vm);
    }
#endif
    nj_vm_get_stats(vm, &stats);
    out->insns = stats.insns;
    out->natives = stats.native_calls;
    nj_app_close(vm, app);
    out->released = nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    return rc;
}

#if NJ_ENABLE_ENGINE
/* ------------------------------------------------------------------ */
/* Probe engine: prove nj_exec() really dispatches through vm->engine  */
/* ------------------------------------------------------------------ */
typedef struct probe_engine {
    struct nj_engine base;   /* MUST be first: enabled by NJ_ENGINE_FROM */
    nj_u4 runs;              /* run() entries */
    nj_u4 yields;            /* quantum expiries handed back to the caller */
} probe_engine;

static nj_status probe_run(struct nj_engine *engine, struct nj_vm *vm,
                           struct nj_thread *th, int stop_depth)
{
    probe_engine *p = NJ_ENGINE_FROM(engine, probe_engine, base);
    nj_status st;

    p->runs++;
    st = nj_interp_run(vm, th, stop_depth);
    if (st == NJ_ERR_YIELD) {
        p->yields++;
    }
    return st;
}

static const nj_engine_ops probe_ops = {
    "probe", probe_run, NULL
};
#endif /* NJ_ENABLE_ENGINE */

int main(int argc, char **argv)
{
    unsigned char *jar;
    nj_u4 jar_len = 0;
    nj_u8 bench_insns = 0;

    printf("== nano_j2me execution-engine equivalence test ==\n");

#if !NJ_ENABLE_ENGINE
    (void)argc;
    (void)argv;
    printf("NJ_ENABLE_ENGINE=0: engine interface not compiled in, skipping\n");
    printf("SKIP (0 checks; rebuild with -DNJ_ENABLE_ENGINE=1 to run)\n");
    return 0;
#else
    if (argc < 2) {
        fprintf(stderr, "usage: %s <bench.jar>\n", argv[0]);
        return 2;
    }
    jar = read_file(argv[1], &jar_len);
    if (!jar) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }

    /* ---- API surface, before any VM exists ---- */
    OK(nj_engine_null() != NULL);
    OK(nj_engine_null() == nj_engine_null());   /* one shared instance */
    if (nj_engine_null()) {
        OK(nj_engine_null()->ops != NULL);
        OK(nj_engine_null()->ops->name != NULL);
        OK(nj_engine_null()->ops->run != NULL);
        OK(nj_engine_null()->ops->destroy == NULL);
    }
    OK(nj_engine_current(NULL) == NULL);
    OK(nj_engine_install(NULL, NULL) == NJ_ERR);
    OK(nj_engine_uninstall(NULL) == NULL);
    nj_engine_destroy(NULL);   /* must be a no-op, not a crash */

    /* ---- 1. baseline: interpreter only ---- */
    {
        run_result base;

        if (run_bench(jar, jar_len, NULL, &base) != 0) {
            fprintf(stderr, "baseline run failed\n");
            free(jar);
            return 1;
        }
        OK(!base.cap.overflow);
        OK_MSG(strstr(base.cap.buf, BENCH_LINE) != NULL,
               "baseline output does not contain \"%s\":\n%s",
               BENCH_LINE, base.cap.buf);
        OK_MSG(base.insns >= BENCH_INSNS_MIN,
               "baseline retires only %llu bytecodes - the benchmark did not "
               "run, or vm->insns is no longer counted",
               (unsigned long long)base.insns);
        bench_insns = base.insns;
        printf("baseline    : startApp=%d insns=%llu natives=%llu "
               "released=%u\n",
               base.status, (unsigned long long)base.insns,
               (unsigned long long)base.natives, (unsigned)base.released);

        /* ---- 2. null engine ---- */
        {
            run_result nullr;

            if (run_bench(jar, jar_len, nj_engine_null(), &nullr) != 0) {
                fprintf(stderr, "null-engine run failed\n");
                free(jar);
                return 1;
            }
            OK(strcmp(base.cap.buf, nullr.cap.buf) == 0);
            OK_MSG(nullr.insns == base.insns,
                   "null engine retires %llu bytecodes vs %llu",
                   (unsigned long long)nullr.insns,
                   (unsigned long long)base.insns);
            OK(nullr.natives == base.natives);
            OK(nullr.released == base.released);
            OK(nullr.status == base.status);
            printf("null engine : startApp=%d insns=%llu natives=%llu "
                   "released=%u\n",
                   nullr.status, (unsigned long long)nullr.insns,
                   (unsigned long long)nullr.natives,
                   (unsigned)nullr.released);
        }

        /* ---- 3. probe engine: dispatch really goes through the vtable ---- */
        {
            probe_engine probe;
            run_result pr;

            memset(&probe, 0, sizeof(probe));
            probe.base.ops = &probe_ops;

            if (run_bench(jar, jar_len, &probe.base, &pr) != 0) {
                fprintf(stderr, "probe-engine run failed\n");
                free(jar);
                return 1;
            }
            /*
             * The benchmark retires tens of millions of dispatches
             * (NJ_SCHED_QUANTUM = 20,000), so a healthy dispatch shows
             * thousands of run() calls.  >1 is the assertion that the engine
             * was consulted at all; 1000 says the quantum hand-off is
             * reaching it too.
             */
            OK_MSG(probe.runs > 1u,
                   "engine run() was called %u time(s): nj_exec() did not "
                   "dispatch through vm->engine", (unsigned)probe.runs);
            OK_MSG(probe.runs > 1000u,
                   "engine run() was called only %u time(s) - fewer than the "
                   "expected quantum hand-offs", (unsigned)probe.runs);
            OK_MSG(probe.yields > 0u, "engine never saw a quantum expiry");
            OK(strcmp(base.cap.buf, pr.cap.buf) == 0);
            OK(pr.insns == base.insns);
            OK(pr.natives == base.natives);
            OK(pr.released == base.released);
            printf("probe engine: runs=%u yields=%u insns=%llu released=%u\n",
                   (unsigned)probe.runs, (unsigned)probe.yields,
                   (unsigned long long)pr.insns, (unsigned)pr.released);
        }

        /* ---- 4. broken engine: NULL run hook must fall back, not crash ---- */
        {
            struct nj_engine broken;     /* no ops at all */
            struct nj_engine no_run;     /* ops, but run == NULL */
            static const nj_engine_ops no_run_ops = { "no-run", NULL, NULL };
            run_result br;

            memset(&broken, 0, sizeof(broken));
            memset(&no_run, 0, sizeof(no_run));
            no_run.ops = &no_run_ops;

            if (run_bench(jar, jar_len, &broken, &br) != 0) {
                fprintf(stderr, "broken-engine run failed\n");
                free(jar);
                return 1;
            }
            OK(strcmp(base.cap.buf, br.cap.buf) == 0);
            OK(br.insns == base.insns);
            OK(br.released == base.released);

            if (run_bench(jar, jar_len, &no_run, &br) != 0) {
                fprintf(stderr, "no-run-engine run failed\n");
                free(jar);
                return 1;
            }
            OK(strcmp(base.cap.buf, br.cap.buf) == 0);
            OK(br.insns == base.insns);
            OK(br.released == base.released);
            printf("broken eng. : fell back to the interpreter, insns=%llu\n",
                   (unsigned long long)br.insns);
        }
    }

    free(jar);
    printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) {
        printf("PASS: null and probe engines are byte-for-byte semantically "
               "identical to the interpreter (%llu bytecodes, BENCH sum "
               "unchanged)\n", (unsigned long long)bench_insns);
    }
    return g_fail == 0 ? 0 : 1;
#endif /* NJ_ENABLE_ENGINE */
}
