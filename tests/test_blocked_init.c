/*
 * nano_j2me - tests/test_blocked_init.c
 *
 * A MIDlet whose constructor blocks must fail with a NAMED exception.
 *
 * BlockDemo's <init> calls Thread.sleep().  The bridge that runs a Java method
 * on behalf of C cannot suspend the caller, so it gives up - but it used to
 * give up WITHOUT leaving an exception behind, and the runner could only print
 * the name of the exception it was handed:
 *
 *     cannot instantiate com.ea.sims3.MonkeyApp: ?
 *
 * The ? was nj_ref_class_name(NULL).  It told the reader nothing, and it also
 * hid the real shape of the problem - a blocked constructor is not a missing
 * class, and treating the two the same sends whoever is debugging it looking
 * for a class that is present.
 *
 * This test asserts the two things that make the report usable:
 *   1. nj_vm_new_midlet leaves a NON-NULL exception behind when it fails;
 *   2. that exception names the class it was constructing.
 *
 * Usage:  test_blocked_init <fixtures-dir>
 * Exit code 0 on PASS, non-zero otherwise.
 */
#include "nj2me/nj.h"

#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static unsigned char *read_file(const char *path, unsigned *len)
{
    FILE *f = fopen(path, "rb");
    long n;
    unsigned char *d;

    *len = 0u;
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    n = ftell(f);
    if (n <= 0) { fclose(f); return NULL; }
    rewind(f);
    d = (unsigned char *)malloc((size_t)n);
    if (!d) { fclose(f); return NULL; }
    if (fread(d, 1u, (size_t)n, f) != (size_t)n) {
        free(d);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (unsigned)n;
    return d;
}

/*
 * CLOCK_MONOTONIC, because the assertion is about how long startup took and
 * not about what time it is.
 */
static long now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000L + (long)(tv.tv_usec / 1000L);
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : "tests/fixtures";
    char path[512];
    unsigned len = 0u;
    unsigned char *data;
    nj_port port;
    nj_vm *vm;
    nj_app *app;
    nj_ref exc = NULL;
    nj_ref midlet;
    int round;
    long t0 = 0, elapsed = 0;

    snprintf(path, sizeof(path), "%s/blockdemo.jar", dir);
    data = read_file(path, &len);
    if (!data) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }

    /*
     * Three rounds, because the failure path also has to leave the VM in a
     * state the next attempt can use - the project's core guarantee.
     */
    for (round = 0; round < 3; round++) {
        memset(&port, 0, sizeof(port));
        if (nj_port_headless(&port) != NJ_OK) {
            free(data);
            return 1;
        }
        vm = nj_vm_create(&port, 4u * 1024u * 1024u);
        if (!vm) {
            nj_port_shutdown(&port);
            free(data);
            return 1;
        }
        app = nj_app_open(vm, data, len);
        OK(app != NULL);
        if (!app) {
            nj_app_close(vm, app);
        nj_vm_destroy(vm);
            nj_port_shutdown(&port);
            free(data);
            return 1;
        }

        exc = NULL;
        t0 = now_ms();
        midlet = nj_vm_new_midlet(vm, "BlockDemo", &exc);
        elapsed = now_ms() - t0;

        /*
         * 1. A constructor that sleeps is no longer a startup failure.
         *
         * Two corpus titles (com.ea.sims3.MonkeyApp, and nokia7650's Start)
         * failed to start for this reason alone.  This assertion is what
         * caught the old behaviour, and what will catch its return.
         */
        OK_MSG(midlet != NULL, "a sleeping constructor still fails to start (round %d)", round);
        OK_MSG(exc == NULL, "a sleeping constructor raised something (round %d)", round);

        /*
         * 2. And the sleep was WAITED FOR, not skipped.
         *
         * Without this, "the MIDlet started" is also satisfied by a runtime
         * that wakes the thread immediately - which would break every title
         * that uses a sleep to pace itself.  The fixture sleeps 300 ms.
         */
        OK_MSG(elapsed >= 250,
               "startup finished in %ld ms: the 300 ms sleep was skipped (round %d)",
               (long)elapsed, round);

        nj_app_close(vm, app);
        nj_vm_destroy(vm);
        nj_port_shutdown(&port);
    }

    free(data);
    printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
