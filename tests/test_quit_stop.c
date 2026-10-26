/*
 * nano_j2me - tests/test_quit_stop.c
 *
 * Closing the window must be reported as an intentional stop, not as a
 * constructor failure.
 *
 * Doom 3 (tests/games/毁灭战士3_10598.jar) runs its entire game inside
 * Doom.<init>: the constructor starts two threads and joins them, so the frame
 * loop is never reached and the whole play session happens while the MIDlet is
 * still being constructed.  Closing the window there used to print
 *
 *     ERROR: cannot instantiate Doom.Doom: java/lang/InternalError
 *
 * The constructor had not failed.  The port had asked to quit (SDL2 pushes
 * NJ_EV_QUIT and the Android port does the same on a back key), and the frame
 * pump answered it by clearing vm->running without recording a stop reason.
 * nj_run() therefore could not tell "the user quit" from "the MIDlet blew
 * up", took its "cannot instantiate" branch, and the exception synthesiser
 * then invented java/lang/InternalError to explain a MIDlet that was never
 * created.
 *
 * This test drives the real runner end to end with a port that injects
 * NJ_EV_QUIT, and asserts three things:
 *
 *   1. a quit that arrives while the constructor is still running is reported
 *      as "run ended: <why>" and the process exits 0.  BlockDemo sleeps 300 ms
 *      inside its constructor, which is where the frame hook - and therefore
 *      the input queue - runs before the constructor returns;
 *   2. a quit after the MIDlet is up goes through the ordinary loop and is
 *      reported the same way, with the exit code in the line the normal path
 *      prints, and teardown still runs afterwards;
 *   3. a genuine failure - a MIDlet class that does not exist - still reaches
 *      "cannot instantiate", because none of this may hide one.
 *
 * Case 3 is the control: the reason is recorded only by the NJ_EV_QUIT path,
 * never by the "no exception was thrown" fallback in the exception
 * synthesiser, so a real VM fault still reports as one.
 *
 * Usage: test_quit_stop [fixtures-dir]   (default tests/fixtures)
 */
#include "nj2me/nj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
        }                                                                      \
    } while (0)

/* ------------------------------------------------------------------ */
/* Log capture                                                         */
/* ------------------------------------------------------------------ */
static char g_log[65536];
static size_t g_log_len;

static void cap_log(void *ud, int level, const char *msg)
{
    int n;

    (void)ud;
    if (g_log_len + 2u >= sizeof(g_log)) {
        return;
    }
    n = snprintf(g_log + g_log_len, sizeof(g_log) - g_log_len - 1u,
                 "[%d] %s\n", level, msg ? msg : "");
    if (n > 0) {
        g_log_len += (size_t)n;
    }
}

static int log_has(const char *needle)
{
    return strstr(g_log, needle) != NULL;
}

/* Position of the first occurrence, or -1: used to assert that teardown is
 * reported after the run-end line rather than instead of it. */
static long log_pos(const char *needle)
{
    const char *p = strstr(g_log, needle);

    return p ? (long)(p - g_log) : -1;
}

/* ------------------------------------------------------------------ */
/* A port whose input queue delivers one NJ_EV_QUIT                     */
/* ------------------------------------------------------------------ */
static int g_poll_calls;
static int g_quit_at;       /* 1-based input poll that delivers the quit, 0=never */
static int g_quit_sent;

static int quit_input_poll(void *ud, nj_event *ev)
{
    (void)ud;
    g_poll_calls++;
    if (g_quit_at != 0 && !g_quit_sent && g_poll_calls >= g_quit_at) {
        memset(ev, 0, sizeof(*ev));
        ev->type = (nj_u1)NJ_EV_QUIT;
        g_quit_sent = 1;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* One end-to-end run                                                  */
/* ------------------------------------------------------------------ */
static int run_case(const char *dir, const char *jar_name,
                    const char *midlet_class, int quit_at, const char *label)
{
    nj_port port;
    nj_run_opts opts;
    char path[512];
    int rc;

    memset(g_log, 0, sizeof(g_log));
    g_log_len = 0u;
    g_poll_calls = 0;
    g_quit_at = quit_at;
    g_quit_sent = 0;

    snprintf(path, sizeof(path), "%s/%s", dir, jar_name);
    memset(&port, 0, sizeof(port));
    if (nj_port_headless_ex(&port, 240, 320, 0, NULL) != NJ_OK) {
        fprintf(stderr, "FAIL: the headless port is not available\n");
        failures++;
        return -1000;
    }
    /*
     * The port is copied by nj_run(), so overriding the two hooks here is
     * enough: the copy polls our queue and logs into the buffer.  The
     * headless port's own state still reaches every other hook through ud.
     */
    port.log = cap_log;
    port.user = NULL;
    port.input_poll = quit_input_poll;
    port.fps_cap = 0;     /* uncapped: the test is about the stop path */

    memset(&opts, 0, sizeof(opts));
    opts.jar_path = path;
    opts.midlet_class = midlet_class;
    opts.heap_bytes = 4u * 1024u * 1024u;

    rc = nj_run(&port, &opts);
    nj_port_shutdown(&port);

    printf("--- %s ---\n%s(runner returned %d)\n", label, g_log, rc);
    return rc;
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : "tests/fixtures";
    int rc;
    long ended, teardown;

    /*
     * 1. The Doom 3 shape: the quit arrives while the constructor is still
     *    running.  This is the bug: before the fix the runner printed
     *    "cannot instantiate ...: java/lang/InternalError" and returned 1.
     */
    rc = run_case(dir, "blockdemo.jar", NULL, 2, "quit during construction");
    CHECK(rc == 0, "a user quit during construction must exit 0, got %d", rc);
    CHECK(log_has("run ended: the user asked the app to quit"),
          "the run must say who ended it");
    CHECK(!log_has("cannot instantiate"),
          "a user quit is not a construction failure");
    CHECK(!log_has("java/lang/InternalError"),
          "no exception may be invented for a MIDlet that was never created");
    CHECK(log_has("teardown: destroyApp() returned"),
          "teardown must still run and complete after an intentional stop");

    /*
     * 2. The ordinary shape: the MIDlet is up and its frame loop is running.
     *    The stop reason must be the one the port asked for, not the
     *    "the frame budget ran out" fallback the run-end line used to print.
     */
    rc = run_case(dir, "flatdemo.jar", NULL, 4, "quit after startup");
    CHECK(rc == 0, "a user quit after startup must exit 0, got %d", rc);
    CHECK(log_has("run ended: the user asked the app to quit (exit code 0)"),
          "the normal run-end line must carry the user-quit reason and code 0");
    ended = log_pos("run ended: the user asked the app to quit");
    teardown = log_pos("teardown: calling destroyApp()");
    CHECK(ended >= 0 && teardown > ended,
          "teardown must follow the run-end report");
    CHECK(log_has("teardown: destroyApp() returned"),
          "teardown must still run after an intentional stop");
    CHECK(!log_has("destroyApp threw"),
          "the destroyApp call must be legal while stopping");
    CHECK(!log_has("the display loop failed"), "the frame loop must end cleanly");

    /*
     * 3. The control: a real failure still reports as a failure.  The stop
     *    reason path must never be reachable without an NJ_EV_QUIT.
     */
    rc = run_case(dir, "blockdemo.jar", "No.Such.Class", 0, "genuine failure");
    CHECK(rc != 0, "a missing MIDlet class must not exit 0");
    CHECK(log_has("cannot instantiate No.Such.Class: java/lang/ClassNotFoundException"),
          "a genuine constructor failure must still say so");
    CHECK(!log_has("run ended:"),
          "a genuine failure must not be reported as a clean run end");

    if (failures != 0) {
        fprintf(stderr, "\n%d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("\nall %d checks passed\n", checks);
    return 0;
}
