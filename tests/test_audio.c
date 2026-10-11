/*
 * nano_j2me - javax.microedition.media, end to end.
 *
 * Runs tests/fixtures/audiodemo.jar through the real runner on the headless
 * port and asserts two different things:
 *
 *   1. what the MIDLET saw - Player state transitions, PlayerListener events,
 *      the WAV's duration, and that an unsupported protocol is refused by name;
 *   2. what the PORT saw - how many times tone_play was called, the format the
 *      PCM device was opened with, and how many frames were pushed.
 *
 * The second half is the point of the headless port recording audio instead of
 * playing it.  A port that played the tones and said nothing would leave every
 * one of those assertions unmakeable without a sound card, and "it made a noise
 * on my machine" is not a regression test.
 *
 * Usage: test_audio <audiodemo.jar>
 */
#include "nj2me/nj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks = 0;
static int fails = 0;

static void ok(int cond, const char *what)
{
    checks++;
    if (!cond) {
        fails++;
        printf("  FAIL %s\n", what);
    }
}

/* The runner logs through the port; keep it so the fixture's report is usable. */
typedef struct capture {
    char buf[8192];
    size_t n;
} capture;

static void cap_log(void *user, int level, const char *msg)
{
    capture *c = (capture *)user;
    size_t len = strlen(msg);
    (void)level;
    if (c->n + len + 2u >= sizeof(c->buf)) {
        return;
    }
    memcpy(c->buf + c->n, msg, len);
    c->n += len;
    c->buf[c->n++] = '\n';
    c->buf[c->n] = '\0';
}

/* The value of "key=" in the fixture's report, copied out. */
static int field(const char *buf, const char *key, char *out, size_t cap)
{
    const char *p = buf;
    size_t klen = strlen(key);
    while ((p = strstr(p, key)) != NULL) {
        const char *v = p + klen;
        size_t i = 0;
        /* To end of line: a value may contain spaces ("unsupported protocol:
         * http"), and stopping at the first one silently truncates it. */
        while (v[i] && v[i] != '\n' && i + 1u < cap) {
            out[i] = v[i];
            i++;
        }
        out[i] = '\0';
        return 1;
    }
    return 0;
}

static int contains(const char *buf, const char *s)
{
    return strstr(buf, s) != NULL;
}

int main(int argc, char **argv)
{
    nj_port port;
    nj_run_opts opts;
    capture cap;
    const char *jar;
    char v[128];
    int rc;
    int round;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <audiodemo.jar>\n", argv[0]);
        return 2;
    }
    jar = argv[1];

    printf("== nano_j2me audio (javax.microedition.media) ==\n");
    printf("jar: %s\n", jar);

    /*
     * Two rounds, because a Player that leaves the audio device open would pass
     * the first and fail the second - and because the whole project's promise is
     * that a run leaves nothing behind.
     */
    for (round = 1; round <= 2; round++) {
        int tone_calls;
        int pcm_opens;
        long pcm_frames;
        int rate = 0;
        int chans = 0;

        memset(&cap, 0, sizeof(cap));
        if (nj_port_headless_ex(&port, 176, 208, 0, NULL) != NJ_OK) {
            fprintf(stderr, "cannot create the headless port\n");
            return 2;
        }
        port.log = cap_log;
        port.user = &cap;
        port.fps_cap = 0;

        memset(&opts, 0, sizeof(opts));
        opts.jar_path = jar;
        opts.heap_bytes = 4u * 1024u * 1024u;

        fprintf(stderr, "[round %d] calling nj_run\n", round);
        rc = nj_run(&port, &opts);
        fprintf(stderr, "[round %d] nj_run returned %d\n", round, rc);

        printf("--- fixture report (round %d) ---\n%s", round, cap.buf);

        ok(rc == NJ_OK || contains(cap.buf, "installed no Displayable"),
           "the MIDlet exited cleanly");
        ok(!contains(cap.buf, "AUDIO failed="),
           "the fixture reported no failure");

        /* ---- what the MIDlet saw ---- */

        ok(field(cap.buf, "AUDIO states=", v, sizeof(v)) &&
           strcmp(v, "100,200,300,400,300,100") == 0,
           "Player walks UNREALIZED, REALIZED, PREFETCHED, STARTED, "
           "PREFETCHED, UNREALIZED");

        ok(field(cap.buf, "AUDIO events=", v, sizeof(v)) &&
           strcmp(v, "started;stopped;closed;") == 0,
           "PlayerListener gets started, stopped and closed, in that order");

        /* 22050 frames/s * 0.2 s = 4410 frames = 200000 microseconds. */
        ok(field(cap.buf, "AUDIO wav=", v, sizeof(v)) &&
           strcmp(v, "200000") == 0,
           "the WAV from the JAR reports its real duration (200000 us)");

        ok(field(cap.buf, "AUDIO http=", v, sizeof(v)) &&
           strcmp(v, "unsupported protocol: http") == 0,
           "an unsupported protocol is refused BY NAME, not silently");

        /* ---- what the port saw ---- */

        tone_calls = nj_port_headless_tone_calls(&port);
        pcm_opens = nj_port_headless_pcm_opens(&port);
        pcm_frames = nj_port_headless_pcm_frames(&port);
        (void)nj_port_headless_pcm_format(&port, &rate, &chans);

        /*
         * Three Manager.playTone() calls plus one tone Player's start(): the
         * fixture makes exactly four requests, and a runtime that dropped one
         * of them - a Player that never reached the port, a tone that never
         * left realise() - fails here rather than sounding slightly wrong.
         */
        printf("  port: tone_calls=%d stops=%d pcm_opens=%d frames=%ld "
               "rate=%d chans=%d\n",
               tone_calls, nj_port_headless_tone_stops(&port), pcm_opens,
               pcm_frames, rate, chans);
        ok(tone_calls == 4, "the port was asked to play 4 tone sequences");
        ok(nj_port_headless_tone_stops(&port) >= 1,
           "the port was told to stop at least once");
        ok(pcm_opens == 1, "the PCM device was opened exactly once");
        ok(pcm_frames == 4410, "4410 PCM frames were pushed (22050 Hz * 0.2 s)");
        ok(rate == 22050, "the device was opened at the WAV's own rate");
        ok(chans == 1, "the device was opened mono, as the WAV is");

        /* The port is the caller's, so it is the caller's to close - and it has
         * to happen AFTER the observations above, which is exactly what used to
         * be impossible. */
        nj_port_shutdown(&port);

        if (fails > 0) {
            break;
        }
    }

    printf("\n%d checks, %d failures\n", checks, fails);
    printf("%s\n", fails == 0 ? "PASS" : "FAIL");
    return fails == 0 ? 0 : 1;
}
