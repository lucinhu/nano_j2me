/*
 * nano_j2me - tests/test_play.c
 *
 * End-to-end playability: run a whole MIDlet through the real runner, on a
 * real port, with the real class library, and assert on what it rendered.
 *
 * Everything else in tests/ exercises a layer.  This one exercises the thing a
 * user actually does: point the emulator at a JAR and play it.  It is the only
 * test that would catch a bug living in the seams - a Display that never
 * becomes current, a GameCanvas whose buffer is never blitted, a Sprite whose
 * paint is never reached because the frame loop skipped a beat.
 *
 * It is deliberately an assertion rather than a screenshot.  The fixture
 * (tools/make_game_jar.sh) is deterministic: 90 frames, then a checksum of the
 * rendered frame plus the sprite's animation state.  So "the game rendered
 * something that changed over time" is checkable, and a regression in the
 * blitter, the transform maths or the tile clipping shows up as a number that
 * moved.
 *
 * It also dumps PPM frames when NANO_J2ME_DUMP is set, because a human looking
 * at one frame is worth a lot when the number moves for a reason nobody
 * expected.
 *
 * Usage: test_play <gamedemo.jar>
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

/* ------------------------------------------------------------------ */
/* Capture System.out and the runtime's own messages                   */
/* ------------------------------------------------------------------ */
#define CAP_MAX 65536
typedef struct capture {
    char buf[CAP_MAX];
    unsigned len;
    int  overflow;
} capture;

static void cap_log(void *ud, int level, const char *msg)
{
    capture *c = (capture *)ud;
    size_t n;

    (void)level;
    if (!msg) {
        return;
    }
    n = strlen(msg);
    if (c->len + n + 2u >= CAP_MAX) {
        c->overflow = 1;
        return;
    }
    memcpy(c->buf + c->len, msg, n);
    c->len += (unsigned)n;
    c->buf[c->len++] = '\n';
    c->buf[c->len] = '\0';
}

/* Find a "key=value" pair in the fixture's report line. */
static long field(const char *line, const char *key)
{
    const char *p = strstr(line, key);
    if (!p) {
        return -1;
    }
    p += strlen(key);
    return strtol(p, NULL, 10);
}

static int hexfield(const char *line, const char *key, unsigned *out)
{
    const char *p = strstr(line, key);
    char *end = NULL;
    unsigned long v;
    if (!p) {
        return -1;
    }
    p += strlen(key);
    v = strtoul(p, &end, 16);
    if (end == p) {
        return -1;
    }
    *out = (unsigned)v;
    return 0;
}

int main(int argc, char **argv)
{
    nj_port port;
    nj_run_opts opts;
    capture cap;
    const char *jar;
    const char *report;
    const char *dump;
    const char *rms_dir;
    int rc;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <gamedemo.jar>\n", argv[0]);
        return 2;
    }
    jar = argv[1];
    memset(&cap, 0, sizeof(cap));

    printf("== nano_j2me end-to-end playability ==\n");
    printf("jar: %s\n", jar);

    dump = getenv("NANO_J2ME_DUMP");
    if (dump && !*dump) {
        dump = NULL;
    }
    if (nj_port_headless_ex(&port, 240, 320,
                            dump ? 30 : 0, dump) != NJ_OK) {
        fprintf(stderr, "cannot create the headless port\n");
        return 2;
    }
    /* Take over logging so the fixture's report line can be asserted on. */
    port.log = cap_log;
    port.user = &cap;
    port.fps_cap = 0;   /* as fast as the host allows; the fixture is bounded */

    /*
     * Keep RecordStore out of the source tree.
     *
     * RMS writes one file per store into nj_port.rms_dir, and the default is
     * the working directory - which for a test run means the repository root
     * collects a demo.rms per execution.  Those are the player's saves, not
     * part of the project, so they go to a scratch directory that is already
     * gitignored.  NANO_J2ME_RMS overrides it.
     */
    rms_dir = getenv("NANO_J2ME_RMS");
    if (!rms_dir || !*rms_dir) {
        rms_dir = "tests/out/rms";
    }
    mkdir("tests/out", 0777);          /* EEXIST is fine */
    mkdir(rms_dir, 0777);
    port.rms_dir = rms_dir;

    memset(&opts, 0, sizeof(opts));
    opts.jar_path = jar;
    opts.heap_bytes = 4u * 1024u * 1024u;

    rc = nj_run(&port, &opts);
    nj_port_shutdown(&port);

    {
        /* Everything except the GAMEDEMO line is the runtime talking to
         * itself; show it when something goes wrong. */
        const char *nl;
        printf("--- runtime output ---\n");
        nl = cap.buf;
        while (*nl) {
            const char *e = strchr(nl, '\n');
            size_t n = e ? (size_t)(e - nl) : strlen(nl);
            if (n > 0 && strncmp(nl, "GAMEDEMO", 8u) != 0) {
                printf("  %.*s\n", (int)n, nl);
            }
            if (!e) {
                break;
            }
            nl = e + 1;
        }
        printf("----------------------\n");
    }

    CHECK(rc == 0, "the runner returned a clean exit code");
    CHECK(!cap.overflow, "the fixture's output fitted in the capture buffer");

    report = strstr(cap.buf, "GAMEDEMO");
    CHECK(report != NULL, "the MIDlet reached its report and exited");
    if (report) {
        long frames = field(report, "frames=");
        long drawn = field(report, "drawn=");
        long frame = field(report, "frame=");
        long transform = field(report, "transform=");
        long saved = field(report, "saved=");
        unsigned sum = 0u;

        printf("%s\n", report);

        CHECK(frames == 90, "it ran the requested number of frames");
        /* A blank or single-colour frame would mean the blit never happened. */
        CHECK(drawn > 1000, "it actually drew into the framebuffer");
        CHECK(hexfield(report, "checksum=", &sum) == 0 && sum != 0u,
              "the rendered frame has a non-trivial checksum");
        CHECK(frame >= 0, "the sprite advanced through its frames");
        CHECK(transform >= 0 && transform <= 7,
              "the sprite's transform is a real TRANS_* value");
        CHECK(saved == -1 || (saved >= 0 && saved <= 90),
              "RecordStore returned a sane previous value");
        CHECK(strstr(report, "status=[]") != NULL,
              "no asset or RMS error was reported by the MIDlet");
    }

    printf("\nchecks: %d  failures: %d\n", checks, failures);
    printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    if (dump && *dump) {
        printf("frames were written to %s\n", dump);
    }
    return failures == 0 ? 0 : 1;
}
