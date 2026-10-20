/*
 * nano_j2me - automated operation, not just launching.
 *
 * The corpus runner starts a title, dumps frames and counts colours, which
 * means a game stuck on a welcome screen PASSES.  It never presses anything, so
 * every input path, menu and state transition is untested.  This drives a
 * MIDlet with a script covering the whole keypad and asserts the things that
 * must hold:
 *
 *   - the emulator does not crash and does not hang
 *   - frames are actually presented
 *   - the picture CHANGES after input, which is the cheapest evidence that the
 *     key reached the game rather than being swallowed
 *   - the runtime throttles a MIDlet that floods the log, rather than filling
 *     the disk (a real title did 24 MB in ten seconds)
 *
 * Usage: test_poke <interactive.jar> <static.jar>
 *   interactive: a title that needs a key press to do anything (Slime Volleyball)
 *   static:      a title that draws without input (the 15-puzzle)
 */
#include "nj2me/nj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
static int fails;

static void ok(int cond, const char *what)
{
    checks++;
    if (!cond) {
        fails++;
        printf("  FAIL %s\n", what);
    }
}

typedef struct capture {
    char buf[65536];
    size_t n;
    int lines;
    nj_event input[64];
    int input_count;
} capture;

/* Observe the events the runner actually delivers, retaining the real port. */
static capture *input_capture;
static int (*port_input_push)(void *ud, const nj_event *ev);

static int cap_input_push(void *ud, const nj_event *ev)
{
    if (input_capture->input_count < 64) {
        input_capture->input[input_capture->input_count++] = *ev;
    }
    return port_input_push(ud, ev);
}

static void cap_log(void *user, int level, const char *msg)
{
    capture *c = (capture *)user;
    size_t len = strlen(msg);
    (void)level;
    c->lines++;
    if (c->n + len + 2u >= sizeof(c->buf)) {
        return;
    }
    memcpy(c->buf + c->n, msg, len);
    c->n += len;
    c->buf[c->n++] = '\n';
    c->buf[c->n] = '\0';
}

/* A script covering every key the runtime can deliver. */
static const char *ALL_KEYS =
    "20:soft1,32:soft2,44:fire,56:up,68:down,80:left,92:right,"
    "104:0,116:1,128:2,140:3,152:4,164:5,176:6,188:7,200:8,212:9,"
    "224:star,236:pound,248:a,260:b,272:c,284:d,296:clear";

/*
 * A cheap content hash of the frames the headless port dumped.
 *
 * T4 in the goal is "the picture must actually change after input", and that is
 * the only assertion here that proves a key ARRIVED rather than being swallowed.
 * Comparing hashes of the dumped PPMs is blunt and sufficient: the first version
 * of this test checked log lines and exceptions, which is the kind of assertion
 * that passes whatever the runtime does.
 */
static unsigned long hash_frames(const char *dir)
{
    char cmd[512];
    char buf[128];
    FILE *f;
    unsigned long h = 5381u;
    const char *q;

    /* system() + a temporary file rather than popen(), which needs
     * _POSIX_C_SOURCE and is not worth the feature-test macro for this. */
    snprintf(cmd, sizeof(cmd),
             "cat %s/*.ppm 2>/dev/null | md5sum | cut -c1-16 > /tmp/nj_poke.h", dir);
    if (system(cmd) != 0) {
        return 0u;
    }
    f = fopen("/tmp/nj_poke.h", "r");
    if (!f) {
        return 0u;
    }
    if (!fgets(buf, sizeof(buf), f)) {
        buf[0] = '\0';
    }
    fclose(f);
    for (q = buf; *q; q++) {
        h = ((h << 5) + h) ^ (unsigned long)(unsigned char)*q;
    }
    return h;
}

/* Run one MIDlet with a key script and report what happened. */
static int run_capture(const char *jar, const char *keys, capture *cap, int frames,
                       const char *dump_dir, int logging)
{
    nj_port port;
    nj_run_opts opts;
    int rc;

    memset(cap, 0, sizeof(*cap));
    if (nj_port_headless_ex(&port, 176, 208, dump_dir ? 20 : 0,
                            dump_dir) != NJ_OK) {
        return -1;
    }
    port.log = logging ? cap_log : NULL;
    port.user = cap;
    port.fps_cap = 0;
    input_capture = cap;
    port_input_push = port.input_push;
    port.input_push = cap_input_push;

    memset(&opts, 0, sizeof(opts));
    opts.jar_path = jar;
    opts.heap_bytes = 4u * 1024u * 1024u;
    opts.run_frames = (nj_u4)frames;
    opts.keys = keys;

    rc = nj_run(&port, &opts);
    /* The port belongs to the caller, so the observations survive the run. */
    nj_port_shutdown(&port);
    return rc;
}

static int run(const char *jar, const char *keys, capture *cap, int frames)
{
    return run_capture(jar, keys, cap, frames, NULL, 1);
}

int main(int argc, char **argv)
{
    capture cap;
    int rc;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <interactive.jar> <static.jar>\n", argv[0]);
        return 2;
    }
    printf("== nano_j2me automated operation ==\n");

    /* ---- 1. a title that NEEDS a key press before it does anything ---- */
    /*
     * The control runs FIRST and matters as much as the test: without it,
     * "the picture changed" could be the game animating on its own.
     */
    system("rm -rf /tmp/nj_poke_a /tmp/nj_poke_a2 /tmp/nj_poke_b");
    system("mkdir -p /tmp/nj_poke_a /tmp/nj_poke_a2 /tmp/nj_poke_b");

    {
        nj_port port;
        nj_run_opts opts;

        if (nj_port_headless_ex(&port, 176, 208, 20, "/tmp/nj_poke_a/") != NJ_OK) {
            return 2;
        }
        port.log = cap_log;
        port.user = &cap;
        port.fps_cap = 0;
        memset(&opts, 0, sizeof(opts));
        opts.jar_path = argv[1];
        opts.heap_bytes = 4u * 1024u * 1024u;
        opts.run_frames = 300u;
        opts.keys = NULL;                       /* no input at all */
        (void)nj_run(&port, &opts);
        nj_port_shutdown(&port);
    }
    ok(strstr(cap.buf, "startApp() threw") == NULL,
       "the interactive title starts with no input");

    rc = run(argv[1], ALL_KEYS, &cap, 300);
    ok(rc == NJ_OK || rc == 1, "it runs when driven with every key");
    ok(strstr(cap.buf, "startApp() threw") == NULL,
       "no key in the sweep throws out of startApp()");

    /*
     * THE CONTROL COMES FIRST: the same run twice with no input at all.
     *
     * Without it the assertion below proves nothing.  A game that animates on
     * its own has two different frame hashes whatever the input did - maze does,
     * and the first version of this test called that a pass.  Two identical
     * no-input runs establish that the title is deterministic, and only then
     * does "different with input" mean the input did it.
     */
    {
        nj_port port;
        nj_run_opts opts;
        if (nj_port_headless_ex(&port, 176, 208, 20, "/tmp/nj_poke_a2/") != NJ_OK) {
            return 2;
        }
        port.log = cap_log;
        port.user = &cap;
        port.fps_cap = 0;
        memset(&opts, 0, sizeof(opts));
        opts.jar_path = argv[1];
        opts.heap_bytes = 4u * 1024u * 1024u;
        opts.run_frames = 300u;
        opts.keys = NULL;
        (void)nj_run(&port, &opts);
        nj_port_shutdown(&port);
    }
    {
        unsigned long a = hash_frames("/tmp/nj_poke_a");
        unsigned long a2 = hash_frames("/tmp/nj_poke_a2");
        printf("  determinism: %016lx vs %016lx with no input at all\n", a, a2);
        ok(a == a2, "the same run twice with no input gives the same frames");
        if (a != a2) {
            printf("  (this title is not deterministic, so the input test "
                   "below cannot conclude anything about it)\n");
        }
    }

    {
        unsigned long with_keys;

        /* Same run, this time dumping frames, so the two can be compared. */
        nj_port port;
        nj_run_opts opts;
        if (nj_port_headless_ex(&port, 176, 208, 20, "/tmp/nj_poke_b/") != NJ_OK) {
            return 2;
        }
        port.log = cap_log;
        port.user = &cap;
        port.fps_cap = 0;
        memset(&opts, 0, sizeof(opts));
        opts.jar_path = argv[1];
        opts.heap_bytes = 4u * 1024u * 1024u;
        opts.run_frames = 300u;
        opts.keys = ALL_KEYS;
        (void)nj_run(&port, &opts);
        nj_port_shutdown(&port);

        with_keys = hash_frames("/tmp/nj_poke_b");
        printf("  frames: %016lx with no input, %016lx with every key\n",
               hash_frames("/tmp/nj_poke_a"), with_keys);
        ok(hash_frames("/tmp/nj_poke_a") != with_keys,
           "the picture is DIFFERENT after input: the keys reached the game");
    }

    /* ---- 2. a title that draws without input ---- */
    rc = run(argv[2], ALL_KEYS, &cap, 300);
    ok(rc == NJ_OK || rc == 1, "the static title survives the key sweep too");
    ok(strstr(cap.buf, "startApp() threw") == NULL &&
       strstr(cap.buf, "exception escaped") == NULL,
       "nothing escapes into the frame loop while being poked");

    /* ---- 3. the log is throttled ---- */
    ok(cap.lines < 100000,
       "the log stays bounded however loudly the MIDlet complains");
    if (cap.lines >= 100000) {
        printf("  log lines: %d\n", cap.lines);
    }

    /* ---- 4. invalid scripts deliver no partial input ---- */
    {
        char script[12000];
        const char *q;
        int events = 0;
        unsigned long baseline = hash_frames("/tmp/nj_poke_a");

        snprintf(script, sizeof(script), "%s,bad", ALL_KEYS);
        rc = run_capture(argv[1], script, &cap, 300, "/tmp/nj_poke_b/", 1);
        ok(rc == NJ_OK || rc == 1, "malformed input does not prevent startup");
        ok(strstr(cap.buf, "THE WHOLE SCRIPT IS IGNORED") != NULL,
           "a malformed suffix reports rejection");
        ok(hash_frames("/tmp/nj_poke_b") == baseline,
           "a malformed suffix delivers none of its valid prefix");

        strcpy(script, ALL_KEYS);
        for (q = ALL_KEYS; *q; q++) {
            if (*q == ':') { events++; }
        }
        while (events++ < 512) { strcat(script, ",10000:fire"); }
        strcat(script, ", \n# a full script may end with comments\n");
        rc = run(argv[1], script, &cap, 2);
        ok((rc == NJ_OK || rc == 1) &&
           strstr(cap.buf, "THE WHOLE SCRIPT IS IGNORED") == NULL,
           "512 events followed by separators and comments are accepted");

        strcat(script, "10000:fire");
        rc = run_capture(argv[1], script, &cap, 300, "/tmp/nj_poke_b/", 1);
        ok(rc == NJ_OK || rc == 1, "oversized input does not prevent startup");
        ok(strstr(cap.buf, "at most 512") != NULL,
           "the 513th event reports the script limit");
        ok(hash_frames("/tmp/nj_poke_b") == baseline,
           "an oversized script delivers none of its valid prefix");

        rc = run_capture(argv[1], script, &cap, 300, "/tmp/nj_poke_b/", 0);
        ok((rc == NJ_OK || rc == 1) &&
           hash_frames("/tmp/nj_poke_b") == baseline,
           "script rejection also clears input without a log callback");
    }

    /* ---- 5. mixing touch and keypad input must not leave a key held ---- */
    rc = run(argv[1],
             "20:right 20:pressed@30,40 20:released@30,40 23:fire",
             &cap, 60);
    ok((rc == NJ_OK || rc == 1) && cap.input_count == 6,
       "a simultaneous key and touch tap deliver both releases exactly once");
    ok(cap.input_count == 6 &&
       cap.input[0].type == NJ_EV_KEY_DOWN &&
       cap.input[0].key == NJ_KEY_RIGHT &&
       cap.input[1].type == NJ_EV_POINTER_DOWN &&
       cap.input[1].x == 30 && cap.input[1].y == 40 &&
       cap.input[2].type == NJ_EV_POINTER_UP &&
       cap.input[3].type == NJ_EV_KEY_UP &&
       cap.input[3].key == NJ_KEY_RIGHT &&
       cap.input[4].type == NJ_EV_KEY_DOWN &&
       cap.input[4].key == NJ_KEY_FIRE &&
       cap.input[5].type == NJ_EV_KEY_UP &&
       cap.input[5].key == NJ_KEY_FIRE,
       "touch preserves the pending key release and the next key tap");

    rc = run(argv[1], "20:right 21:pressed@30,40 24:released@30,40",
             &cap, 60);
    ok((rc == NJ_OK || rc == 1) && cap.input_count == 4 &&
       cap.input[0].type == NJ_EV_KEY_DOWN &&
       cap.input[1].type == NJ_EV_POINTER_DOWN &&
       cap.input[2].type == NJ_EV_KEY_UP &&
       cap.input[2].key == NJ_KEY_RIGHT &&
       cap.input[3].type == NJ_EV_POINTER_UP,
       "a touch on the release frame does not cancel the key release");

    rc = run(argv[1],
             "20:pressed@30,40 20:right 20:dragged@35,45 24:released@35,45",
             &cap, 60);
    ok((rc == NJ_OK || rc == 1) && cap.input_count == 5 &&
       cap.input[0].type == NJ_EV_POINTER_DOWN &&
       cap.input[1].type == NJ_EV_KEY_DOWN &&
       cap.input[2].type == NJ_EV_POINTER_MOVE &&
       cap.input[3].type == NJ_EV_KEY_UP &&
       cap.input[3].key == NJ_KEY_RIGHT &&
       cap.input[4].type == NJ_EV_POINTER_UP &&
       cap.input[4].x == 35 && cap.input[4].y == 45,
       "a drag around a key tap retains one key release and one touch release");

    /* ---- 6. explicit key edges hold a direction across frames ---- */
    /*
     * Titles that steer with getKeyStates() need the key to STAY down.  An
     * ordinary entry is released one frame after it is pressed, which is right
     * for a menu tap and useless for movement, so `frame:key:down` and
     * `frame:key:up` name the two edges.  The event COUNT is the assertion: an
     * auto-released hold would deliver a fifth event between the down and the
     * up, and a second up would deliver another.
     */
    rc = run(argv[1], "40:down:down,60:fire,80:down:up", &cap, 120);
    ok(rc == NJ_OK || rc == 1, "a script with explicit key edges runs");
    ok(cap.input_count == 4 &&
       cap.input[0].type == NJ_EV_KEY_DOWN && cap.input[0].key == NJ_KEY_DOWN &&
       cap.input[1].type == NJ_EV_KEY_DOWN && cap.input[1].key == NJ_KEY_FIRE &&
       cap.input[2].type == NJ_EV_KEY_UP && cap.input[2].key == NJ_KEY_FIRE &&
       cap.input[3].type == NJ_EV_KEY_UP && cap.input[3].key == NJ_KEY_DOWN,
       "a held key stays down until its explicit up edge");

    /* An up edge of its own is one event, not a tap with an automatic release. */
    rc = run(argv[1], "10:left:up", &cap, 40);
    ok((rc == NJ_OK || rc == 1) && cap.input_count == 1 &&
       cap.input[0].type == NJ_EV_KEY_UP && cap.input[0].key == NJ_KEY_LEFT,
       "an explicit up edge sends exactly one release");

    /* A bad third field is rejected with the rest of the script, not guessed. */
    {
        char script[12000];
        snprintf(script, sizeof(script), "%s,100:fire:sideways", ALL_KEYS);
        rc = run_capture(argv[1], script, &cap, 60, "/tmp/nj_poke_b/", 1);
    }
    ok((rc == NJ_OK || rc == 1) &&
       strstr(cap.buf, "THE WHOLE SCRIPT IS IGNORED") != NULL,
       "an unknown key edge is rejected with the rest of the script");

    printf("\n%d checks, %d failures\n%s\n", checks, fails,
           fails == 0 ? "PASS" : "FAIL");
    return fails == 0 ? 0 : 1;
}
