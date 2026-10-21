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

/* ------------------------------------------------------------------ */
/* A device with a small buffer                                        */
/* ------------------------------------------------------------------ */
/*
 * The pump in nj_media_frame() exists for exactly one case: a WAV longer than
 * the device's buffer, on a port that can only take a little at a time.  The
 * headless port accepts everything, so testing with it proves nothing about
 * that path - it would pass with the pump removed.
 *
 * These wrappers make the device bounded: pcm_write takes at most 256 frames
 * per call and pcm_free reports the same.  The full 4410-frame file can then
 * only arrive if someone keeps pushing across frames.
 */
#define BOUNDED_LIMIT 256
static int (*g_orig_write)(void *, const nj_s2 *, int);

static int bounded_write(void *ud, const nj_s2 *samples, int frames)
{
    if (frames > BOUNDED_LIMIT) {
        frames = BOUNDED_LIMIT;
    }
    return g_orig_write(ud, samples, frames);
}

static int bounded_free(void *ud)
{
    (void)ud;
    return BOUNDED_LIMIT;
}

/*
 * A port that can decode the same file differently.
 *
 * The runtime's own AMR decoder is the fallback; this is what a phone with a
 * hardware codec, or a desktop with libavcodec, fills in instead - and the
 * port wins, because it answers first.  It reports 32 bytes of silence -- 16
 * frames at 8000 Hz, i.e. a 2000 microsecond file -- so the assertions can
 * tell the port's numbers (2000) from the runtime's (80000).
 */
static nj_status fake_amr_decode(void *ud, const nj_u1 *in, nj_u4 in_len,
                                 nj_u1 *out, nj_u4 out_cap, nj_u4 *out_len,
                                 nj_u4 *sample_rate, nj_u4 *channels)
{
    (void)ud;
    *sample_rate = 8000u;
    *channels = 1u;
    if (!out) {
        *out_len = 32u;
        return NJ_OK;
    }
    if (out_cap < 32u) {
        return NJ_ERR_FORMAT;
    }
    if (in_len < 6u || in[0] != '#' || in[2] != 'A') {
        return NJ_ERR_UNSUPPORTED;   /* decline anything that is not AMR */
    }
    memset(out, 0, 32u);
    *out_len = 32u;
    return NJ_OK;
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

        /*
         * The AMR asset is four 4.75 kbit/s frames of silence: 4 * 160 samples
         * at 8000 Hz, i.e. 80000 microseconds.  The runtime decodes it itself;
         * the port hook in the last round is a port's decoder, not the only
         * one there is.  Before nj_amr.c existed createPlayer() threw here and
         * the MIDlet was handed nothing to play - which is the shape of the
         * bug this asserts away.
         */
        ok(field(cap.buf, "AUDIO amr=", v, sizeof(v)) &&
           strcmp(v, "80000") == 0,
           "the AMR from the JAR is decoded by the runtime (80000 us)");
        ok(field(cap.buf, "AUDIO amrstate=", v, sizeof(v)) &&
           strcmp(v, "400") == 0,
           "the AMR Player reaches STARTED with no port decoder");
        ok(field(cap.buf, "AUDIO amrrefused=", v, sizeof(v)) == 0,
           "the AMR asset is no longer refused");

        /*
         * The third asset is IMA ADPCM - 8 kHz, mono, 4-bit, block align 256,
         * four blocks.  A block yields 2 * (256 - 4) + 1 = 505 samples, so
         * 4 * 505 = 2020 frames = 252500 microseconds.  Civilisation V ships
         * its entire soundtrack in this format and used to get "not a PCM WAV"
         * from createPlayer() once per frame inside paint(); the duration is
         * the cheapest proof that the blocks were decoded rather than skipped.
         */
        ok(field(cap.buf, "AUDIO adpcm=", v, sizeof(v)) &&
           strcmp(v, "252500") == 0,
           "the IMA ADPCM WAV is decoded to 2020 PCM frames (252500 us)");
        ok(field(cap.buf, "AUDIO adpcmstate=", v, sizeof(v)) &&
           strcmp(v, "400") == 0,
           "the IMA ADPCM Player reaches STARTED");
        ok(field(cap.buf, "AUDIO adpcmrefused=", v, sizeof(v)) == 0,
           "the IMA ADPCM asset is not refused");

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
        /*
         * Three Players reach the device: the WAV, the AMR the runtime decodes
         * itself, and the IMA ADPCM WAV decoded here. Each opens it once, at
         * its own rate - and the ADPCM is the last of the three, so the format
         * below is its, not the WAV's. The WAV's own rate is asserted in the
         * bounded round, where it is the only Player in the fixture.
         */
        ok(pcm_opens == 3, "each Player opened the PCM device exactly once");
        ok(pcm_frames == 4410 + 640 + 2020,
           "4410 WAV, the AMR's 640 and the ADPCM's 2020 frames all reached "
           "the device");
        ok(rate == 8000,
           "the IMA ADPCM Player's device is opened at 8000 Hz");
        ok(chans == 1, "the device was opened mono, as the AMR is");

        /* The port is the caller's, so it is the caller's to close - and it has
         * to happen AFTER the observations above, which is exactly what used to
         * be impossible. */
        nj_port_shutdown(&port);

        if (fails > 0) {
            break;
        }
    }

    /*
     * ---- a device that can only take a little at a time ----
     *
     * The same MIDlet, on a port whose pcm_write accepts 256 frames per call.
     * The file is 4410 frames, so it can only be delivered if the runtime keeps
     * pushing once per emulated frame.
     */
    {
        long frames;
        int wrate = 0;
        int wchans = 0;

        memset(&cap, 0, sizeof(cap));
        if (nj_port_headless_ex(&port, 176, 208, 0, NULL) != NJ_OK) {
            fprintf(stderr, "cannot create the headless port\n");
            return 2;
        }
        port.log = cap_log;
        port.user = &cap;
        port.fps_cap = 0;

        /* Wrap the port's own writer rather than replacing it, so the counters
         * it keeps are still the evidence. */
        g_orig_write = port.pcm_write;
        port.pcm_write = bounded_write;
        port.pcm_free = bounded_free;

        memset(&opts, 0, sizeof(opts));
        opts.jar_path = jar;
        /* The OTHER MIDlet in the fixture: AudioDemo calls notifyDestroyed()
         * when startApp() returns, so the frame loop would end at once and
         * nothing would pump.  This one stays alive and is bounded by
         * --run-frames instead. */
        opts.midlet_class = "snd.AudioLoop";
        opts.heap_bytes = 4u * 1024u * 1024u;
        opts.run_frames = 60u;      /* 60 * 256 = 15360 frames of room */
        (void)nj_run(&port, &opts);

        frames = nj_port_headless_pcm_frames(&port);
        printf("--- bounded device (256 frames per call) ---\n");
        printf("frames the device accepted: %ld of 4410\n", frames);
        ok(contains(cap.buf, "AUDIO playing=1"),
           "the MIDlet left a WAV playing");
        ok(frames == 4410,
           "the whole file is delivered across frames, not just the first push");
        ok(nj_port_headless_pcm_closes(&port) >= 1,
           "the device is closed once the file ends");
        /*
         * This MIDlet plays one file, so the device's format is the WAV's own
         * and not the AMR's - which is the assertion the first rounds cannot
         * make, because there the AMR Player opens the device last.
         */
        (void)nj_port_headless_pcm_format(&port, &wrate, &wchans);
        ok(wrate == 22050 && wchans == 1,
           "the WAV's own rate and channel count reach the device");
        nj_port_shutdown(&port);
    }

    /*
     * The same fixture again, with a port that has a decoder of its own.  The
     * rounds above assert what the runtime's own AMR decoder produces; this
     * asserts the difference the hook exists to make, and that the numbers
     * come from the port rather than from a guess the runtime made.
     */
    {
        memset(&cap, 0, sizeof(cap));
        if (nj_port_headless_ex(&port, 176, 208, 0, NULL) == NJ_OK) {
            port.log = cap_log;
            port.user = &cap;
            port.fps_cap = 0;
            port.audio_decode = fake_amr_decode;
            memset(&opts, 0, sizeof(opts));
            opts.jar_path = jar;
            opts.heap_bytes = 4u * 1024u * 1024u;
            (void)nj_run(&port, &opts);

            ok(nj_port_headless_pcm_frames(&port) > 0,
               "the PCM the port produced is what reaches the device");
            ok(field(cap.buf, "AUDIO amrrefused=", v, sizeof(v)) == 0,
               "nothing is refused once the port can decode it");
            ok(field(cap.buf, "AUDIO amrstate=", v, sizeof(v)) &&
               strcmp(v, "400") == 0,
               "an AMR player reaches STARTED through the port's decoder");
            ok(field(cap.buf, "AUDIO amr=", v, sizeof(v)) &&
               strcmp(v, "2000") == 0,
               "the duration is the port's PCM, not a guess");
            nj_port_shutdown(&port);
        }
    }

    printf("\n%d checks, %d failures\n", checks, fails);
    printf("%s\n", fails == 0 ? "PASS" : "FAIL");
    return fails == 0 ? 0 : 1;
}
