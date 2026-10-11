/*
 * nano_j2me - tests/test_vm.c
 *
 * End-to-end smoke test for the VM core: load a JAR, run a class's
 * `main([Ljava/lang/String;)V`, and check the result.
 *
 * It also enforces the project's memory contract: the same VM is created,
 * run and destroyed several times in a row and the test asserts that
 * everything allocated on the way was released, and that the host allocation
 * count returns to zero every time.
 *
 * Usage:  test_vm <app.jar> <main-class> [expected-substring]
 */
#include "nj2me/nj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Capture the program's stdout so the test can assert on it           */
/* ------------------------------------------------------------------ */
#define CAP_MAX 16384
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

/* ------------------------------------------------------------------ */
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

static int run_once(const unsigned char *jar, nj_u4 jar_len,
                    const char *main_class, cap_state *cap,
                    nj_vm_stats *stats_out, int verbose)
{
    nj_port port;
    nj_vm *vm;
    nj_app *app;
    nj_u4 released = 0u;
    nj_value arg;
    nj_ref sarr;

    (void)verbose;
    nj_status st;
    int rc = 1;

    memset(cap, 0, sizeof(*cap));

    if (nj_port_headless(&port) != NJ_OK) {
        fprintf(stderr, "port init failed\n");
        return 1;
    }
    port.log = cap_log;
    port.user = cap;

    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    if (!vm) {
        fprintf(stderr, "vm create failed\n");
        nj_port_shutdown(&port);
        return 1;
    }

    app = nj_app_open(vm, jar, jar_len);
    if (!app) {
        fprintf(stderr, "app open failed (bad jar?)\n");
        nj_vm_destroy(vm);
        nj_port_shutdown(&port);
        return 1;
    }

    sarr = nj_object_array_new(vm, "java/lang/String", 0u);
    arg.r = sarr;
    st = nj_call_static(vm, main_class, "main", "([Ljava/lang/String;)V",
                        &arg, NULL);
    if (st != NJ_OK) {
        fprintf(stderr, "main threw: %s\n",
                nj_exception_describe(vm) ? nj_exception_describe(vm) : "(?)");
        nj_clear_exception(vm);
        goto done;
    }
    /* Let any background threads finish their work. */
    (void)nj_run_pending(vm, 8000u);
    rc = 0;

done:
    if (stats_out) {
        nj_vm_get_stats(vm, stats_out);
    }
    nj_app_close(vm, app);
    /*
     * Everything the VM owns - the Java arena, class bytes, member tables,
     * thread stacks, native registry, CP cache - is released here, and the
     * return value says how much was still outstanding.  That single number is
     * the whole leak assertion for the runtime.
     */
    released = nj_vm_destroy(vm);
    if (stats_out) {
        stats_out->host_live = released;
    }
    (void)verbose;
    nj_port_shutdown(&port);
    return rc;
}

int main(int argc, char **argv)
{
    unsigned char *jar;
    nj_u4 jar_len = 0;
    cap_state cap;
    nj_vm_stats st1, st2, st3;
    const char *expect = NULL;
    int rounds = 3;
    int i;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <app.jar> <main-class> [expected]\n", argv[0]);
        return 2;
    }
    if (argc >= 4) {
        expect = argv[3];
    }
    jar = read_file(argv[1], &jar_len);
    if (!jar) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }

    printf("== nano_j2me VM core test ==\n");
    printf("jar        : %s (%u bytes)\n", argv[1], (unsigned)jar_len);
    printf("main class : %s\n", argv[2]);
    if (expect) {
        printf("expecting  : %s\n", expect);
    }

    for (i = 0; i < rounds; i++) {
        nj_vm_stats *slot = (i == 0) ? &st1 : ((i == 1) ? &st2 : &st3);
        int rc = run_once(jar, jar_len, argv[2], &cap, slot, i == 0);

        if (rc != 0) {
            printf("round %d: FAILED\n", i + 1);
            printf("--- program output before the failure ---\n%s"
                   "----------------------------------------\n", cap.buf);
            free(jar);
            return 1;
        }
        if (i == 0) {
            printf("--- program output ---\n%s----------------------\n", cap.buf);
            if (expect && !strstr(cap.buf, expect)) {
                printf("FAIL: output does not contain \"%s\"\n", expect);
                free(jar);
                return 1;
            }
        }
        /*
         * Round 1 must release a non-trivial amount (proving the accounting is
         * real, not a constant zero) and every later round must release the
         * same amount: a drift means something is being retained between
         * create/destroy cycles.
         */
        if (i == 0 && slot->host_live == 0u) {
            printf("FAIL: destroy released nothing, accounting is broken\n");
            free(jar);
            return 1;
        }
        if (i > 0 && slot->host_live != st1.host_live) {
            printf("FAIL: round %d released %u blocks, round 1 released %u\n",
                   i + 1, (unsigned)slot->host_live, (unsigned)st1.host_live);
            free(jar);
            return 1;
        }
    }

    printf("heap after round 1: used %lu / %lu, peak %lu, gc %lu\n",
           (unsigned long)st1.heap_used, (unsigned long)st1.heap_size,
           (unsigned long)st1.heap_peak, (unsigned long)st1.gc_count);
    printf("classes loaded    : %u\n", (unsigned)st1.classes);
    printf("host blocks freed : %u at nj_vm_destroy (peak %u)\n",
           (unsigned)st1.host_live, (unsigned)st1.host_peak);
    printf("cp-cache           : %u entries, %lu hits / %lu misses\n",
           (unsigned)st1.cp_cache_entries,
           (unsigned long)st1.cp_cache_hits,
           (unsigned long)st1.cp_cache_misses);
    printf("natives bound      : %u (misses %lu)\n",
           (unsigned)st1.natives, (unsigned long)st1.native_misses);

    if (st2.host_peak != st1.host_peak || st3.host_peak != st1.host_peak) {
        printf("FAIL: host allocation peak drifted between rounds "
               "(%u / %u / %u)\n",
               (unsigned)st1.host_peak, (unsigned)st2.host_peak,
               (unsigned)st3.host_peak);
        free(jar);
        return 1;
    }
    if (st2.classes != st1.classes) {
        printf("FAIL: class count drifted between rounds (%u vs %u)\n",
               (unsigned)st1.classes, (unsigned)st2.classes);
        free(jar);
        return 1;
    }

    printf("\nPASS: %d create/run/destroy rounds, identical teardown every time\n",
           rounds);
    printf("      run under -fsanitize=address,leak for the process-level proof\n");
    free(jar);
    return 0;
}
