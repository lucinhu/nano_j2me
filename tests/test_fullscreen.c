/*
 * nano_j2me - tests/test_fullscreen.c
 *
 * Selecting a Canvas and delivering its visibility callback are separate.
 *
 * 开心农场 runs this legal sequence inside its Canvas constructor; the mode
 * change must succeed before its first frame:
 *
 *     Display.getDisplay(midlet).setCurrent(this);
 *     setFullScreenMode(true);
 *
 * The runtime used to deliver showNotify() synchronously from setCurrent(), so
 * the mode change threw.  Keep showNotify() deferred until presentation, but
 * report isShown() immediately for the current Displayable: launch wrappers
 * otherwise call startApp() again before their first frame.
 *
 * The fixture (tools/make_fsorder_jar.sh) prints what it observed, so this is
 * an assertion about the ordering rather than about a screenshot.
 *
 * Usage: test_fullscreen <fsorder.jar>
 */
#include "nj2me/nj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int checks;
static int failures;

#define CHECK(cond, what)                                                    \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, (what));        \
        }                                                                    \
    } while (0)

#define CAP_MAX 65536
typedef struct capture {
    char buf[CAP_MAX];
    unsigned len;
    int overflow;
} capture;

static void cap_log(void *ud, int level, const char *msg)
{
    capture *c = (capture *)ud;
    size_t n;

    (void)level;
    n = strlen(msg);
    if (c->len + n + 1u >= CAP_MAX) {
        c->overflow = 1;
        return;
    }
    memcpy(c->buf + c->len, msg, n);
    c->len += (unsigned)n;
    c->buf[c->len++] = '\n';
    c->buf[c->len] = '\0';
}

int main(int argc, char **argv)
{
    nj_port port;
    nj_run_opts opts;
    capture cap;
    const char *jar;
    int rc;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <fsorder.jar>\n", argv[0]);
        return 2;
    }
    jar = argv[1];
    memset(&cap, 0, sizeof(cap));

    printf("== nano_j2me setCurrent / full-screen ordering ==\n");
    printf("jar: %s\n", jar);

    if (nj_port_headless_ex(&port, 240, 320, 0, NULL) != NJ_OK) {
        fprintf(stderr, "cannot create the headless port\n");
        return 2;
    }
    port.log = cap_log;
    port.user = &cap;
    port.fps_cap = 0;
    mkdir("tests/out", 0777);
    mkdir("tests/out/rms", 0777);
    port.rms_dir = "tests/out/rms";

    memset(&opts, 0, sizeof(opts));
    opts.jar_path = jar;
    opts.heap_bytes = 4u * 1024u * 1024u;

    rc = nj_run(&port, &opts);
    nj_port_shutdown(&port);

    if (rc != NJ_OK) {
        printf("--- runtime output ---\n%s\n", cap.buf);
    }

    /*
     * The whole point: the constructor's setFullScreenMode() must not have
     * thrown.  Checked by name as well as by rc, because an exception in
     * startApp() is reported through the log rather than the return code.
     */
    CHECK(rc == NJ_OK, "the MIDlet ran to completion");
    CHECK(strstr(cap.buf, "IllegalStateException") == NULL,
          "setFullScreenMode() after setCurrent() did not throw");
    CHECK(strstr(cap.buf, "setFullScreenMode() on a visible Displayable") == NULL,
          "the ordering rule was not violated");

    /* Selection is observable before the deferred presentation callback. */
    CHECK(strstr(cap.buf, "FSORDER current shown=true shows=0") != NULL,
          "the current Canvas is shown before its deferred showNotify()");

    /* And the mode change must have taken effect. */
    CHECK(strstr(cap.buf, "FSORDER full=true") != NULL,
          "the Canvas is in full-screen mode");

    /* showNotify() still runs exactly once before the first paint(). */
    CHECK(strstr(cap.buf, "FSORDER painted shown=true full=true shows=1") != NULL,
          "the full-screen Canvas receives showNotify() before its first paint");

    if (failures) {
        printf("--- runtime output ---\n%s\n", cap.buf);
    }
    printf("%d checks passed, %d failed\n", checks - failures, failures);
    return failures ? 1 : 0;
}
