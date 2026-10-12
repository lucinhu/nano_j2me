/*
 * nano_j2me - tests/test_midi.c
 *
 * The MIDI renderer, driven by a Standard MIDI File built here in C.
 *
 * The point of building the file by hand is that every number in it is known:
 * one track, 480 ticks per quarter note, 500000 microseconds per quarter note
 * (the MIDI default), one A4 held for exactly one quarter note.  So the render
 * has to be 0.5 s of 440 Hz - and "0.5 s at 8 kHz" and "440 Hz" are two
 * numbers a test can count.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nj2me/nj_types.h"
#include "nj_midi.h"    /* nj_midi_mem: the renderer borrows this allocator */

static void *t_alloc(void *ud, nj_size n)
{
    (void)ud;
    return malloc(n);
}

static void t_free(void *ud, void *p)
{
    (void)ud;
    free(p);
}

static const nj_midi_mem MEM = { NULL, t_alloc, t_free };

static int g_checks;
static int g_failures;

static void check(int ok, const char *what)
{
    g_checks++;
    if (!ok) {
        g_failures++;
        printf("FAIL %s\n", what);
    }
}

/* MThd, format 0, one track, 480 ticks per quarter. */
static const nj_u1 g_header[14] = {
    'M', 'T', 'h', 'd', 0, 0, 0, 6u,
    0u, 0u,             /* format 0 */
    0u, 1u,             /* one track */
    0x01u, 0xE0u        /* 480 */
};

/* MTrk: tempo 500000, A4 on at tick 0, off at tick 480, end of track. */
static const nj_u1 g_track[28] = {
    'M', 'T', 'r', 'k', 0u, 0u, 0u, 20u,
    0x00u, 0xFFu, 0x51u, 0x03u, 0x07u, 0xA1u, 0x20u,
    0x00u, 0x90u, 0x45u, 0x64u,
    0x83u, 0x60u, 0x80u, 0x45u, 0x40u,
    0x00u, 0xFFu, 0x2Fu, 0x00u
};

/* The same file, with the note-on removed: a MIDI file with nothing to play
 * must report that, so the caller can stay silent instead of playing zeros. */
static const nj_u1 g_track_silent[23] = {
    'M', 'T', 'r', 'k', 0u, 0u, 0u, 15u,
    0x00u, 0xFFu, 0x51u, 0x03u, 0x07u, 0xA1u, 0x20u,
    0x00u, 0xFFu, 0x2Fu, 0x00u
};

/* Sixteen notes in a row - more notes than there are voices.  Each is held for
 * 60 ticks and followed by 60 ticks of rest, so at most one sounds at a time
 * and a renderer that reuses a voice whose note has ended plays all sixteen.
 * One that never frees a voice stops sounding after the twelfth note: that is
 * exactly what a 32-second song did, half a second of audio and then silence. */
static const nj_u1 g_phrase_head[14] = {
    'M', 'T', 'h', 'd', 0, 0, 0, 6u,
    0u, 0u,             /* format 0 */
    0u, 1u,             /* one track */
    0x01u, 0xE0u        /* 480 */
};

static nj_u4 build_phrase(nj_u1 *out)
{
    nj_u4 n = 0u;
    nj_u4 k;

    memcpy(out, g_phrase_head, sizeof(g_phrase_head));
    n = sizeof(g_phrase_head);
    memcpy(out + n, "MTrk", 4u);
    n += 4u;
    out[n++] = 0x00u;
    out[n++] = 0x00u;
    out[n++] = 0x00u;
    out[n++] = 0x8Bu;   /* 139 bytes: tempo, sixteen note pairs, end of track */
    out[n++] = 0x00u;
    out[n++] = 0xFFu;
    out[n++] = 0x51u;
    out[n++] = 0x03u;
    out[n++] = 0x07u;
    out[n++] = 0xA1u;
    out[n++] = 0x20u;   /* 500000 us per quarter */
    for (k = 0u; k < 16u; k++) {
        out[n++] = (k == 0u) ? 0x00u : 0x78u;   /* 120 ticks between notes */
        out[n++] = 0x90u;
        out[n++] = 60u;
        out[n++] = 0x64u;
        out[n++] = 0x3Cu;                       /* held for 60 ticks */
        out[n++] = 0x80u;
        out[n++] = 60u;
        out[n++] = 0x40u;
    }
    out[n++] = 0x00u;
    out[n++] = 0xFFu;
    out[n++] = 0x2Fu;
    out[n++] = 0x00u;
    return n;
}


/* ------------------------------------------------------------------ */
/* Measuring what came out: crest factor, level, zero crossings        */
/* ------------------------------------------------------------------ */

static nj_s4 pcm_at(const nj_u1 *pcm, nj_u4 i)
{
    return (nj_s4)(short)((nj_u2)pcm[i * 2u] | ((nj_u2)pcm[i * 2u + 1u] << 8));
}

/* Integer square root, so the test does not need libm. */
static nj_u8 isqrt64(nj_u8 v)
{
    nj_u8 r = 0u;
    nj_u8 bit = (nj_u8)1u << 62;
    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0u) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

static nj_u8 pcm_rms(const nj_u1 *pcm, nj_u4 frames, nj_u4 from, nj_u4 to)
{
    nj_u8 sumsq = 0u;
    nj_u4 n = 0u;
    nj_u4 i;

    for (i = from; i < to && i < frames; i++) {
        nj_s4 v = pcm_at(pcm, i);
        sumsq += (nj_u8)(nj_s4)(v * v);
        n++;
    }
    return (n == 0u) ? 0u : isqrt64(sumsq / n);
}

static nj_s4 pcm_peak(const nj_u1 *pcm, nj_u4 frames, nj_u4 from, nj_u4 to)
{
    nj_s4 peak = 0;
    nj_u4 i;

    for (i = from; i < to && i < frames; i++) {
        nj_s4 v = pcm_at(pcm, i);
        nj_s4 a = (v < 0) ? -v : v;
        if (a > peak) {
            peak = a;
        }
    }
    return peak;
}

/* Peak over RMS, times 100: about 100 for a square wave, 150 to 180 for a
 * triangle or a saw, and past 200 for noise. */
static int pcm_crest(const nj_u1 *pcm, nj_u4 frames, nj_u4 from, nj_u4 to)
{
    nj_u8 rms = pcm_rms(pcm, frames, from, to);
    if (rms == 0u) {
        return 0;
    }
    return (int)((nj_u8)pcm_peak(pcm, frames, from, to) * 100u / rms);
}

static nj_u4 pcm_crossings(const nj_u1 *pcm, nj_u4 frames, nj_u4 from, nj_u4 to)
{
    nj_u4 i;
    nj_u4 n = 0u;
    int prev = 0;

    for (i = from; i < to && i < frames; i++) {
        nj_s4 v = pcm_at(pcm, i);
        int sign = (v > 0) ? 1 : ((v < 0) ? -1 : 0);
        if (sign != 0 && prev != 0 && sign != prev) {
            n++;
        }
        if (sign != 0) {
            prev = sign;
        }
    }
    return n;
}

/* How much the crossing count varies from window to window: a steady note
 * keeps it flat, a note with vibrato swings with the wobble. */
static void pcm_cross_spread(const nj_u1 *pcm, nj_u4 frames, nj_u4 win,
                             nj_u4 from, nj_u4 to, nj_u4 *mn_out, nj_u4 *mx_out)
{
    nj_u4 a, mn = 0xFFFFFFFFu, mx = 0u;

    if (to > frames) {
        to = frames;
    }
    for (a = from; a + win <= to; a += win) {
        nj_u4 c = pcm_crossings(pcm, frames, a, a + win);
        if (c < mn) {
            mn = c;
        }
        if (c > mx) {
            mx = c;
        }
    }
    *mn_out = (mn == 0xFFFFFFFFu) ? 0u : mn;
    *mx_out = mx;
}

/* A one-note file with a prefix of channel messages in front of it: a program
 * change, a control change, or both. */
static nj_u4 build_voiced(nj_u1 *out, const nj_u1 *prefix, nj_u4 plen,
                          nj_u1 chan, nj_u1 note)
{
    nj_u4 n = 0u;
    /* tempo(7) + [delta + prefix] + note on(4) + note off(5) + end(4) */
    nj_u4 len = 7u + ((plen != 0u) ? 1u : 0u) + plen + 4u + 5u + 4u;
    nj_u4 i;

    memcpy(out, g_header, sizeof(g_header));
    n = sizeof(g_header);
    memcpy(out + n, "MTrk", 4u);
    n += 4u;
    out[n++] = (nj_u1)((len >> 24) & 0xFFu);
    out[n++] = (nj_u1)((len >> 16) & 0xFFu);
    out[n++] = (nj_u1)((len >> 8) & 0xFFu);
    out[n++] = (nj_u1)(len & 0xFFu);
    out[n++] = 0x00u;
    out[n++] = 0xFFu;
    out[n++] = 0x51u;
    out[n++] = 0x03u;
    out[n++] = 0x07u;
    out[n++] = 0xA1u;
    out[n++] = 0x20u;
    if (plen != 0u) {
        out[n++] = 0x00u;           /* the prefix starts at tick 0 too */
    }
    for (i = 0u; i < plen; i++) {
        out[n++] = prefix[i];
    }
    out[n++] = 0x00u;
    out[n++] = (nj_u1)(0x90u | chan);
    out[n++] = note;
    out[n++] = 0x64u;
    out[n++] = 0x83u;
    out[n++] = 0x60u;               /* held for 480 ticks */
    out[n++] = (nj_u1)(0x80u | chan);
    out[n++] = note;
    out[n++] = 0x40u;
    out[n++] = 0x00u;
    out[n++] = 0xFFu;
    out[n++] = 0x2Fu;
    out[n++] = 0x00u;
    return n;
}

/* The level of the sample-to-sample difference: a crude high pass, which is
 * what tells a bright note from a dull one at the same loudness. */
static nj_u8 pcm_hf(const nj_u1 *pcm, nj_u4 frames, nj_u4 from, nj_u4 to)
{
    nj_u8 sumsq = 0u;
    nj_u4 n = 0u;
    nj_u4 i;

    for (i = from + 1u; i < to && i < frames; i++) {
        nj_s4 d = pcm_at(pcm, i) - pcm_at(pcm, i - 1u);
        sumsq += (nj_u8)(nj_s4)(d * d);
        n++;
    }
    return (n == 0u) ? 0u : isqrt64(sumsq / n);
}

static nj_u4 build_track(nj_u1 *out, const nj_u1 *body, nj_u4 blen);

/* A sample of one channel of an interleaved buffer. */
static nj_s4 pcm_at_ch(const nj_u1 *pcm, nj_u4 frame, nj_u4 ch, nj_u4 chans)
{
    nj_u4 i = (frame * chans + ch) * 2u;
    return (nj_s4)(nj_s2)((pcm[i] & 0xFFu) | ((nj_u4)pcm[i + 1u] << 8));
}

static nj_u8 pcm_rms_ch(const nj_u1 *pcm, nj_u4 frames, nj_u4 chans, nj_u4 ch,
                        nj_u4 from, nj_u4 to)
{
    nj_u8 sumsq = 0u;
    nj_u4 n = 0u;
    nj_u4 i;

    for (i = from; i < to && i < frames; i++) {
        nj_s4 v = pcm_at_ch(pcm, i, ch, chans);
        sumsq += (nj_u8)(nj_s4)(v * v);
        n++;
    }
    return (n == 0u) ? 0u : isqrt64(sumsq / n);
}

/* How far the period wanders, in samples: a vibrato makes successive periods
 * differ, a steady tone keeps them the same.  Only useful on a low note, where
 * a fraction of a percent of pitch is several samples of period. */
static nj_u4 pcm_period_spread(const nj_u1 *pcm, nj_u4 frames, nj_u4 from, nj_u4 to)
{
    nj_u4 last = 0u, lo = 0u, hi = 0u, n = 0u, i;

    for (i = from + 1u; i < to && i < frames; i++) {
        if (pcm_at(pcm, i - 1u) < 0 && pcm_at(pcm, i) >= 0) {
            if (last != 0u) {
                nj_u4 d = i - last;
                if (n == 0u || d < lo) {
                    lo = d;
                }
                if (n == 0u || d > hi) {
                    hi = d;
                }
                n++;
            }
            last = i;
        }
    }
    return (n < 4u) ? 0u : (hi - lo);
}

/* A one-note file at a chosen velocity: a hard note has to be brighter than a
 * soft one at the same pitch, and only two of them side by side show that. */
static nj_u4 build_at(nj_u1 *out, nj_u1 chan, nj_u1 note, nj_u1 vel, nj_u1 prog)
{
    static const nj_u1 body[] = {
        0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,   /* tempo */
        0x00, 0xC0, 40,                             /* strings: bright and held */
        0x00, 0x90, 0x45, 0x64,                     /* note on: patched below */
        0x83, 0x60, 0x80, 0x45, 0x40,               /* +480: note off */
        0x00, 0xFF, 0x2F, 0x00
    };
    nj_u1 b[sizeof(body)];

    memcpy(b, body, sizeof(body));
    b[9] = prog;
    b[11] = (nj_u1)(0x90u | chan);
    b[12] = note;
    b[13] = vel;
    b[16] = (nj_u1)(0x80u | chan);
    b[17] = note;
    return build_track(out, b, (nj_u4)sizeof(b));
}

/* Wrap a track body that the test wrote out byte by byte. */
static nj_u4 build_track(nj_u1 *out, const nj_u1 *body, nj_u4 blen)
{
    nj_u4 n = 0u;

    memcpy(out, g_header, sizeof(g_header));
    n = sizeof(g_header);
    memcpy(out + n, "MTrk", 4u);
    n += 4u;
    out[n++] = (nj_u1)((blen >> 24) & 0xFFu);
    out[n++] = (nj_u1)((blen >> 16) & 0xFFu);
    out[n++] = (nj_u1)((blen >> 8) & 0xFFu);
    out[n++] = (nj_u1)(blen & 0xFFu);
    memcpy(out + n, body, blen);
    n += blen;
    return n;
}

/* Render one such file and hand back the PCM. */
static nj_u1 *render_voiced(const nj_u1 *prefix, nj_u4 plen, nj_u1 chan,
                            nj_u1 note, nj_u4 *frames_out)
{
    nj_u1 file[128];
    nj_u4 n = build_voiced(file, prefix, plen, chan, note);
    return nj_midi_render(file, n, frames_out, &MEM);
}

int main(void)
{
    nj_u1 file[sizeof(g_header) + sizeof(g_track)];
    nj_u1 quiet[sizeof(g_header) + sizeof(g_track_silent)];
    nj_u4 frames = 0u;
    nj_u1 *pcm;
    nj_u4 i;
    int peak = 0;
    int crossings = 0;
    int last_sign = 0;

    memcpy(file, g_header, sizeof(g_header));
    memcpy(file + sizeof(g_header), g_track, sizeof(g_track));
    memcpy(quiet, g_header, sizeof(g_header));
    memcpy(quiet + sizeof(g_header), g_track_silent, sizeof(g_track_silent));

    check(nj_midi_render(NULL, 0u, &frames, &MEM) == NULL,
          "a null file renders nothing");
    check(nj_midi_render("not a midi file at all", 23u, &frames, &MEM) == NULL,
          "a file without MThd renders nothing");
    check(nj_midi_render(quiet, sizeof(quiet), &frames, &MEM) == NULL,
          "a MIDI file with no notes renders nothing");

    pcm = nj_midi_render(file, sizeof(file), &frames, &MEM);
    check(pcm != NULL, "the A4 file renders");
    if (pcm == NULL) {
        printf("%d checks, %d failures\n", g_checks, g_failures);
        return g_failures != 0;
    }

    /* 480 ticks at 500000 us per quarter is 0.5 s, plus the tail the renderer
     * adds for the release: the longest release any note in the file asked
     * for - the piano bank's 140 ms here - and never less than 50 ms.  The
     * sample count follows the rate. */
    check(frames >= NJ_MIDI_RATE / 2u
          && frames <= NJ_MIDI_RATE / 2u + NJ_MIDI_RATE * 3u / 20u,
          "the render is 0.5 s plus the release tail");
    if (frames < NJ_MIDI_RATE / 2u) {
        printf("     frames = %u\n", (unsigned)frames);
    }

    for (i = 0u; i < NJ_MIDI_RATE / 2u && i * 2u + 1u < frames * 2u; i++) {
        int s = (int)(short)((nj_u2)pcm[i * 2u] | ((nj_u2)pcm[i * 2u + 1u] << 8));
        int sign = (s > 200) ? 1 : ((s < -200) ? -1 : 0);
        if (s > peak) {
            peak = s;
        } else if (-s > peak) {
            peak = -s;
        }
        if (sign != 0) {
            if (last_sign != 0 && sign != last_sign) {
                crossings++;
            }
            last_sign = sign;
        }
    }

    check(peak > 8000, "the note is loud, not a whisper");
    if (peak <= 8000) {
        printf("     peak = %d\n", peak);
    }
    /* 440 Hz for 0.5 s is 220 cycles, i.e. 440 sign changes. */
    check(crossings >= 400 && crossings <= 480,
          "the pitch is A4 (440 Hz)");
    if (crossings < 400 || crossings > 480) {
        printf("     crossings = %d (expected about 440)\n", crossings);
    }

    {
        static nj_u1 phrase[256];
        nj_u4 pn = build_phrase(phrase);
        nj_u4 pf = 0u;
        nj_u1 *pp = nj_midi_render(phrase, pn, &pf, &MEM);
        int loud = 0;

        check(pp != NULL, "a sixteen-note phrase renders");
        if (pp != NULL) {
            /* Longer than the single note above; the exact tail is the
             * renderer's business, and the point here is the last quarter. */
            check(pf > 15000u, "the phrase renders far past the single note");
            /*
             * Every quarter must sound.  A renderer that hands out voices
             * before the mix keeps only the notes still held when that pass
             * ends - or, with no reuse at all, only the first ones - so a
             * phrase longer than the voice count loses its beginning or its
             * end.  Thirty seconds of a real song came out as silence that way.
             */
            loud = 1;
            if (pf != 0u) {
                nj_u4 q;
                for (q = 0u; q < 4u; q++) {
                    nj_u4 from = pf * q / 4u;
                    nj_u4 to = pf * (q + 1u) / 4u;
                    int heard = 0;
                    for (i = from; i < to; i++) {
                        int s = (int)(short)((nj_u2)pp[i * 2u]
                                             | ((nj_u2)pp[i * 2u + 1u] << 8));
                        if (s > 200 || s < -200) {
                            heard = 1;
                            break;
                        }
                    }
                    if (!heard) {
                        printf("     quarter %u of the phrase is silent\n",
                               (unsigned)q);
                        loud = 0;
                    }
                }
            }
            check(loud, "every quarter of a phrase longer than the voice count"
                        " is audible");
            t_free(NULL, pp);
        }
    }


    /*
     * The program number picks the waveform, and the waveforms differ in a way
     * a test can count: peak over RMS is about 1.2 for a square, 1.5 to 1.8
     * for a triangle or a saw, and past 2 for noise.
     */
    {
        static const nj_u1 p_brass[2]  = { 0xC0u, 56u };   /* square */
        static const nj_u1 p_bass[2]   = { 0xC0u, 32u };   /* triangle */
        static const nj_u1 p_guitar[2] = { 0xC0u, 24u };   /* saw, struck */
        static const nj_u1 p_noise[2]  = { 0xC0u, 120u };  /* noise */
        nj_u4 fr = 0u;
        nj_u4 to;
        nj_u1 *b = render_voiced(p_brass, 2u, 0u, 69u, &fr);
        int c_brass = 0, c_bass = 0, c_guitar = 0, c_noise = 0;

        check(b != NULL && fr > 0u, "a program change still renders");
        if (b != NULL) {
            to = fr / 2u;
            c_brass = pcm_crest(b, fr, 0u, to);
            t_free(NULL, b);
        }
        b = render_voiced(p_bass, 2u, 0u, 45u, &fr);
        if (b != NULL) {
            c_bass = pcm_crest(b, fr, 0u, fr / 2u);
            t_free(NULL, b);
        }
        b = render_voiced(p_guitar, 2u, 0u, 64u, &fr);
        if (b != NULL) {
            c_guitar = pcm_crest(b, fr, 0u, fr / 2u);
            t_free(NULL, b);
        }
        b = render_voiced(p_noise, 2u, 0u, 69u, &fr);
        if (b != NULL) {
            c_noise = pcm_crest(b, fr, 0u, fr / 2u);
            t_free(NULL, b);
        }
        check(c_brass > 0 && c_bass > c_brass,
              "a triangle has a higher crest factor than a square");
        check(c_guitar > c_brass,
              "a saw has a higher crest factor than a square");
        check(c_noise > c_brass + 40,
              "noise has a much higher crest factor than a pitched wave");
        if (!(c_bass > c_brass && c_guitar > c_brass && c_noise > c_brass + 40)) {
            printf("     crest: brass %d, bass %d, guitar %d, noise %d\n",
                   c_brass, c_bass, c_guitar, c_noise);
        }
    }

    /* Channel 10 is drums, and a hi-hat is noise: in its first 50 ms it
     * changes sign far more often than any pitch could, and it decays away
     * instead of holding like a note. */
    {
        static const nj_u1 p_none[1] = { 0u };
        nj_u4 fr = 0u;
        nj_u1 *hat = render_voiced(p_none, 0u, 9u, 42u, &fr);
        nj_u4 crossings = 0u;
        nj_u8 early = 0u, late = 0u;

        check(hat != NULL && fr > 0u, "a drum note still renders");
        if (hat != NULL) {
            nj_u4 window = NJ_MIDI_RATE / 20u;          /* 50 ms */
            crossings = pcm_crossings(hat, fr, 0u, window);
            early = pcm_rms(hat, fr, 0u, window);
            late = pcm_rms(hat, fr, fr * 3u / 4u, fr);
            t_free(NULL, hat);
        }
        check(crossings > 200u,
              "a hi-hat on channel 10 is noise, not a pitched note");
        check(early > 0u && late * 4u < early,
              "a hi-hat decays away instead of holding");
        if (crossings <= 200u || !(early > 0u && late * 4u < early)) {
            printf("     hi-hat crossings in 50 ms: %u, rms %llu then %llu\n",
                   (unsigned)crossings, (unsigned long long)early,
                   (unsigned long long)late);
        }
    }

    /* Controller 7 is the channel's volume: a quiet channel has to be quiet. */
    {
        static const nj_u1 p_loud[3] = { 0xB0u, 7u, 127u };
        static const nj_u1 p_soft[3] = { 0xB0u, 7u, 40u };
        nj_u4 fr1 = 0u, fr2 = 0u;
        nj_u1 *loud = render_voiced(p_loud, 3u, 0u, 69u, &fr1);
        nj_u1 *soft = render_voiced(p_soft, 3u, 0u, 69u, &fr2);
        nj_u8 r_loud = 0u, r_soft = 0u;

        if (loud != NULL) {
            r_loud = pcm_rms(loud, fr1, 0u, fr1 / 2u);
            t_free(NULL, loud);
        }
        if (soft != NULL) {
            r_soft = pcm_rms(soft, fr2, 0u, fr2 / 2u);
            t_free(NULL, soft);
        }
        check(r_loud > 0u && r_soft > 0u, "both volumes render");
        check(r_soft > 0u && r_loud > 0u && r_soft < r_loud * 6u / 10u,
              "volume 40 of 127 is quieter than the default");
        if (!(r_soft < r_loud * 6u / 10u)) {
            printf("     rms loud %llu, soft %llu\n",
                   (unsigned long long)r_loud, (unsigned long long)r_soft);
        }
    }


    /*
     * CC1 modulation: 10,477 events in the corpus, and the one thing a MIDlet
     * ever means by it is vibrato.  A note with CC1 = 127 has to wobble - the
     * crossing count over a quarter of the wobble period has to differ between
     * the top and the bottom - while the same note without CC1 holds a steady
     * pitch.
     */
    {
        /* No delta bytes here: build_voiced() writes one for the prefix. */
        /* Every event needs its own delta: a byte with the top bit set is a
         * VLQ continuation, so the program change needs its 0x00. */
        static const nj_u1 vib_body[] = { 0xB0, 1, 127, 0x00, 0xC0, 40 };
        static const nj_u1 flat_body[] = { 0xC0, 40 };
        nj_u1 *pcm;
        nj_u4 fr = 0u, mn = 0u, mx = 0u, fmn = 0u, fmx = 0u, spread, fspread;

        pcm = render_voiced(vib_body, (nj_u4)sizeof(vib_body), 0, 93, &fr);
        check(pcm != NULL && fr > 0u, "a note with modulation still renders");
        if (pcm != NULL) {
            pcm_cross_spread(pcm, fr, NJ_MIDI_RATE / 20u,
                             NJ_MIDI_RATE * 15u / 100u,
                             NJ_MIDI_RATE * 45u / 100u, &mn, &mx);
            t_free(NULL, pcm);
        }
        pcm = render_voiced(flat_body, (nj_u4)sizeof(flat_body), 0, 93, &fr);
        if (pcm != NULL) {
            pcm_cross_spread(pcm, fr, NJ_MIDI_RATE / 20u,
                             NJ_MIDI_RATE * 15u / 100u,
                             NJ_MIDI_RATE * 45u / 100u, &fmn, &fmx);
            t_free(NULL, pcm);
        }
        spread = mx - mn;
        fspread = fmx - fmn;
        check(spread > fspread, "CC1 vibrato moves the pitch of a sounding note");
        check(mn < fmn || mx > fmx, "and the wobble reaches past the steady pitch");
        if (!(spread > fspread) || !(mn < fmn || mx > fmx)) {
            printf("     vibrato crossings %u..%u, steady %u..%u\n",
                   (unsigned)mn, (unsigned)mx, (unsigned)fmn, (unsigned)fmx);
        }
    }

    /*
     * CC91 reverb send: 2,969 events in the corpus.  A note that is sent to
     * the room has to keep ringing after it stops, and a note that was never
     * sent has to stay dry - the send defaults to nothing, so a file that does
     * not ask for reverb sounds exactly as it did before.
     */
    {
        /* Organ: it holds, and its 40 ms release is over before the tail
         * window opens, so a dry note really is silent there. */
        static const nj_u1 wet_body[] = { 0xB0, 91, 127, 0x00, 0xC0, 16 };
        static const nj_u1 dry_body[] = { 0xC0, 16 };
        nj_u1 *pcm;
        nj_u4 fr = 0u;
        nj_u8 wet_tail = 0u, dry_tail = 0u, wet_note = 0u, dry_note = 0u;

        pcm = render_voiced(wet_body, (nj_u4)sizeof(wet_body), 0, 93, &fr);
        check(pcm != NULL && fr > NJ_MIDI_RATE * 3u / 4u,
              "a reverbed note renders past its note off");
        if (pcm != NULL) {
            wet_note = pcm_rms(pcm, fr, NJ_MIDI_RATE / 10u, NJ_MIDI_RATE / 5u);
            wet_tail = pcm_rms(pcm, fr, NJ_MIDI_RATE * 55u / 100u,
                               NJ_MIDI_RATE * 75u / 100u);
            t_free(NULL, pcm);
        }
        pcm = render_voiced(dry_body, (nj_u4)sizeof(dry_body), 0, 93, &fr);
        if (pcm != NULL) {
            dry_note = pcm_rms(pcm, fr, NJ_MIDI_RATE / 10u, NJ_MIDI_RATE / 5u);
            dry_tail = pcm_rms(pcm, fr, NJ_MIDI_RATE * 55u / 100u,
                               NJ_MIDI_RATE * 75u / 100u);
            t_free(NULL, pcm);
        }
        check(wet_tail > 0u && wet_tail > dry_tail * 20u,
              "the reverb keeps the note ringing after it stops");
        check(wet_note > 0u && wet_note < dry_note * 2u,
              "and the note itself is still there");
        if (!(wet_tail > dry_tail * 20u) || !(wet_note < dry_note * 2u)) {
            printf("     reverb tail %llu vs dry %llu; note %llu vs %llu\n",
                   (unsigned long long)wet_tail, (unsigned long long)dry_tail,
                   (unsigned long long)wet_note, (unsigned long long)dry_note);
        }
    }

    /*
     * Pitch bend.  47,596 bend events in 464 of the corpus files, and none of
     * them used to do anything.  A bend to the top of the range has to raise
     * the pitch of a note that is already sounding: two semitones is 12% more
     * zero crossings, measured on either side of the bend.
     */
    {
        static const nj_u1 bend_body[] = {
            0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,   /* tempo 500000 */
            0x00, 0xC0, 40,                             /* strings: a held wave */
            0x00, 0x90, 69, 100,                        /* A4 on */
            0x81, 0x70, 0xE0, 0x7F, 0x7F,               /* +240 ticks: bend up */
            0x81, 0x70, 0x80, 69, 64,                   /* +240 ticks: note off */
            0x00, 0xFF, 0x2F, 0x00
        };
        nj_u1 file[128];
        nj_u4 n = build_track(file, bend_body, (nj_u4)sizeof(bend_body));
        nj_u4 fr = 0u;
        nj_u1 *pcm = nj_midi_render(file, n, &fr, &MEM);
        nj_u4 early = 0u, late = 0u;

        check(pcm != NULL && fr > 0u, "a bend still renders");
        if (pcm != NULL) {
            early = pcm_crossings(pcm, fr, 0u, NJ_MIDI_RATE / 5u);
            late = pcm_crossings(pcm, fr, NJ_MIDI_RATE * 3u / 10u,
                                 NJ_MIDI_RATE / 2u);
            t_free(NULL, pcm);
        }
        check(early > 0u && late * 100u > early * 108u,
              "a pitch bend raises the pitch of a sounding note");
        if (!(early > 0u && late * 100u > early * 108u)) {
            printf("     crossings before the bend %u, after it %u\n",
                   (unsigned)early, (unsigned)late);
        }
    }

    /*
     * Sustain pedal.  2,226 pedal-down events in 68 files, and the notes used
     * to stop dead at their note off regardless.  With the pedal down the note
     * has to keep sounding, and it has to stop when the pedal comes up.
     */
    {
        static const nj_u1 sus_body[] = {
            0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,
            0x00, 0xC0, 16,                             /* organ: holds, and
                                                         * releases in 40 ms */
            0x00, 0xB0, 64, 127,                        /* pedal down */
            0x00, 0x90, 69, 100,                        /* A4 on */
            0x81, 0x70, 0x80, 69, 64,                   /* +240: note off */
            0x81, 0x70, 0xB0, 64, 0,                    /* +240: pedal up */
            0x00, 0xFF, 0x2F, 0x00
        };
        static const nj_u1 dry_body[] = {
            0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,
            0x00, 0xC0, 16,
            0x00, 0x90, 69, 100,
            0x81, 0x70, 0x80, 69, 64,
            0x00, 0xFF, 0x2F, 0x00
        };
        nj_u1 file[128];
        nj_u4 n, fr = 0u;
        nj_u1 *pcm;
        nj_u8 held = 0u, after = 0u, released = 0u;
        nj_u8 dry_held = 0u, dry_after = 0u;

        n = build_track(file, sus_body, (nj_u4)sizeof(sus_body));
        pcm = nj_midi_render(file, n, &fr, &MEM);
        check(pcm != NULL && fr > 0u, "a pedalled note still renders");
        if (pcm != NULL) {
            held = pcm_rms(pcm, fr, NJ_MIDI_RATE / 10u, NJ_MIDI_RATE / 4u);
            after = pcm_rms(pcm, fr, NJ_MIDI_RATE * 3u / 10u,
                            NJ_MIDI_RATE * 48u / 100u);
            /* The pedal comes up at 0.5 s and the release is 40 ms, so the
             * note is gone by 0.55 s. */
            released = pcm_rms(pcm, fr, NJ_MIDI_RATE * 55u / 100u,
                               NJ_MIDI_RATE * 6u / 10u);
            t_free(NULL, pcm);
        }
        n = build_track(file, dry_body, (nj_u4)sizeof(dry_body));
        pcm = nj_midi_render(file, n, &fr, &MEM);
        if (pcm != NULL) {
            dry_held = pcm_rms(pcm, fr, NJ_MIDI_RATE / 10u, NJ_MIDI_RATE / 4u);
            dry_after = pcm_rms(pcm, fr, NJ_MIDI_RATE * 3u / 10u,
                                NJ_MIDI_RATE * 48u / 100u);
            t_free(NULL, pcm);
        }
        check(held > 0u && after * 3u > held,
              "the sustain pedal holds a note past its note off");
        check(dry_held > 0u && dry_after * 10u < dry_held,
              "without the pedal the same note stops at its note off");
        check(held > 0u && released * 10u < held,
              "the note stops when the pedal comes up");
        if (!(held > 0u && after * 3u > held) || !(dry_after * 10u < dry_held)
            || !(released * 10u < held)) {
            printf("     pedal rms: sounding %llu, after note off %llu,"
                   " after release %llu; dry %llu then %llu\n",
                   (unsigned long long)held, (unsigned long long)after,
                   (unsigned long long)released, (unsigned long long)dry_held,
                   (unsigned long long)dry_after);
        }
    }

    /*
     * Per-family ADSR.  The banks differ in more than waveform now: a string
     * section fades in over 45 ms and then holds its level, a piano is struck
     * and keeps decaying, an organ is at full level within 6 ms and has
     * stopped 40 ms after its note off where a string section is still
     * sounding 130 ms later.
     */
    {
        /* No delta bytes: build_voiced() writes one before the prefix. */
        static const nj_u1 organ_pre[] = { 0xC0u, 16u };
        static const nj_u1 str_pre[]   = { 0xC0u, 40u };
        static const nj_u1 piano_pre[] = { 0xC0u, 0u };
        nj_u1 file[128];
        nj_u4 n, fr = 0u;
        nj_u4 off = NJ_MIDI_RATE / 2u;      /* the note off lands at 0.5 s */
        nj_u4 o_head = 0u, o_body = 0u, o_rel = 0u;
        nj_u4 s_head = 0u, s_body = 0u, s_rel = 0u;
        nj_u4 s_early = 0u, s_late = 0u, p_early = 0u, p_late = 0u;
        nj_u1 *m;

        n = build_voiced(file, organ_pre, (nj_u4)sizeof(organ_pre), 0u, 69u);
        m = nj_midi_render(file, n, &fr, &MEM);
        check(m != NULL, "the organ note renders");
        if (m != NULL) {
            o_head = pcm_rms(m, fr, 0u, NJ_MIDI_RATE / 50u);
            o_body = pcm_rms(m, fr, NJ_MIDI_RATE * 3u / 50u, NJ_MIDI_RATE * 6u / 50u);
            o_rel  = pcm_rms(m, fr, off + NJ_MIDI_RATE / 20u,
                             off + NJ_MIDI_RATE * 13u / 100u);
            t_free(NULL, m);
        }
        n = build_voiced(file, str_pre, (nj_u4)sizeof(str_pre), 0u, 69u);
        m = nj_midi_render(file, n, &fr, &MEM);
        check(m != NULL, "the strings note renders");
        if (m != NULL) {
            s_head = pcm_rms(m, fr, 0u, NJ_MIDI_RATE / 50u);
            s_body = pcm_rms(m, fr, NJ_MIDI_RATE * 3u / 50u, NJ_MIDI_RATE * 6u / 50u);
            s_early = pcm_rms(m, fr, NJ_MIDI_RATE / 10u, NJ_MIDI_RATE / 5u);
            s_late = pcm_rms(m, fr, NJ_MIDI_RATE * 3u / 10u, NJ_MIDI_RATE * 9u / 20u);
            s_rel = pcm_rms(m, fr, off + NJ_MIDI_RATE / 20u,
                            off + NJ_MIDI_RATE * 13u / 100u);
            t_free(NULL, m);
        }
        n = build_voiced(file, piano_pre, (nj_u4)sizeof(piano_pre), 0u, 69u);
        m = nj_midi_render(file, n, &fr, &MEM);
        check(m != NULL, "the piano note renders");
        if (m != NULL) {
            p_early = pcm_rms(m, fr, NJ_MIDI_RATE / 10u, NJ_MIDI_RATE / 5u);
            p_late = pcm_rms(m, fr, NJ_MIDI_RATE * 3u / 10u, NJ_MIDI_RATE * 9u / 20u);
            t_free(NULL, m);
        }

        check(o_body > 0u && o_head * 2u > o_body,
              "the organ reaches its level within 20 ms");
        check(s_body > 0u && s_head * 2u < s_body,
              "the strings are still fading in after 20 ms");
        check(s_early > 0u && s_late * 10u > s_early * 9u,
              "the strings bank holds the level its attack reached");
        check(p_early > 0u && p_late * 10u < p_early * 9u,
              "the piano bank keeps decaying after its attack");
        check(s_rel > 0u && o_rel * 10u < s_rel,
              "an organ stops sooner after the note off than a string section");
        if (!(o_body > 0u && o_head * 2u > o_body)
            || !(s_body > 0u && s_head * 2u < s_body)
            || !(s_early > 0u && s_late * 10u > s_early * 9u)
            || !(p_early > 0u && p_late * 10u < p_early * 9u)
            || !(s_rel > 0u && o_rel * 10u < s_rel)) {
            printf("     adsr rms: organ head %u body %u rel %u | strings head %u"
                   " body %u early %u late %u rel %u | piano early %u late %u\n",
                   (unsigned)o_head, (unsigned)o_body, (unsigned)o_rel,
                   (unsigned)s_head, (unsigned)s_body, (unsigned)s_early,
                   (unsigned)s_late, (unsigned)s_rel, (unsigned)p_early,
                   (unsigned)p_late);
        }
    }

    /*
     * Drum layering.  A kick is not a low note: its skin starts an octave
     * above the note and falls to it, so the first milliseconds cross zero
     * far more often than the rest.  The metal is noise, and the open pieces
     * ring where the closed ones have already stopped.
     */
    {
        nj_u1 file[128];
        nj_u4 n, fr = 0u;
        nj_u1 *m;
        nj_u4 early = 0u, late = 0u;
        nj_u8 open_hat = 0u, closed_hat = 0u, crash = 0u, closed_late = 0u;

        n = build_voiced(file, NULL, 0u, 9u, 36u);      /* kick */
        m = nj_midi_render(file, n, &fr, &MEM);
        check(m != NULL, "a kick renders");
        if (m != NULL) {
            early = pcm_crossings(m, fr, 0u, NJ_MIDI_RATE * 3u / 100u);
            late = pcm_crossings(m, fr, NJ_MIDI_RATE * 6u / 100u,
                                 NJ_MIDI_RATE / 10u);
            t_free(NULL, m);
        }
        check(late > 0u && early * 10u > late * 13u,
              "a kick starts above its note and falls to it");
        if (!(late > 0u && early * 10u > late * 13u)) {
            printf("     kick crossings early %u late %u\n",
                   (unsigned)early, (unsigned)late);
        }

        n = build_voiced(file, NULL, 0u, 9u, 46u);      /* open hat */
        m = nj_midi_render(file, n, &fr, &MEM);
        if (m != NULL) {
            open_hat = pcm_rms(m, fr, NJ_MIDI_RATE / 10u, NJ_MIDI_RATE / 4u);
            t_free(NULL, m);
        }
        n = build_voiced(file, NULL, 0u, 9u, 42u);      /* closed hat */
        m = nj_midi_render(file, n, &fr, &MEM);
        if (m != NULL) {
            closed_hat = pcm_rms(m, fr, NJ_MIDI_RATE / 10u, NJ_MIDI_RATE / 4u);
            closed_late = pcm_rms(m, fr, NJ_MIDI_RATE * 3u / 10u,
                                  NJ_MIDI_RATE * 6u / 10u);
            t_free(NULL, m);
        }
        check(open_hat > 0u && open_hat > closed_hat * 10u,
              "an open hi-hat rings where a closed one has stopped");
        if (!(open_hat > 0u && open_hat > closed_hat * 10u)) {
            printf("     hat rms: open %llu, closed %llu\n",
                   (unsigned long long)open_hat, (unsigned long long)closed_hat);
        }

        n = build_voiced(file, NULL, 0u, 9u, 49u);      /* crash */
        m = nj_midi_render(file, n, &fr, &MEM);
        if (m != NULL) {
            crash = pcm_rms(m, fr, NJ_MIDI_RATE * 3u / 10u, NJ_MIDI_RATE * 6u / 10u);
            t_free(NULL, m);
        }
        check(crash > 0u && crash > closed_late * 10u,
              "a crash is still ringing half a second later");
        if (!(crash > 0u && crash > closed_late * 10u)) {
            printf("     crash rms %llu vs closed hat %llu\n",
                   (unsigned long long)crash, (unsigned long long)closed_late);
        }
    }

    /*
     * A snare is a drum head with wires over it: the tone under the noise is
     * what separates it from a hiss.  Measured as zero crossings in the first
     * twenty milliseconds, where the head is loudest - a hat at the same
     * velocity is all noise and crosses far more often.
     */
    {
        nj_u1 file[128];
        nj_u4 n, fr = 0u, win = NJ_MIDI_RATE / 50u;
        nj_u1 *m;
        nj_u4 snare_c = 0u, hat_c = 0u;

        n = build_voiced(file, NULL, 0u, 9u, 38u);      /* snare */
        m = nj_midi_render(file, n, &fr, &MEM);
        check(m != NULL, "a snare renders");
        if (m != NULL) {
            snare_c = pcm_crossings(m, fr, 0u, win);
            t_free(NULL, m);
        }
        n = build_voiced(file, NULL, 0u, 9u, 42u);      /* closed hat */
        m = nj_midi_render(file, n, &fr, &MEM);
        if (m != NULL) {
            hat_c = pcm_crossings(m, fr, 0u, win);
            t_free(NULL, m);
        }
        check(snare_c > 0u && snare_c * 10u < hat_c * 7u,
              "a snare has a drum head under the wires, not just noise");
        printf("     snare crossings %u, hat crossings %u in the first 20 ms\n",
               (unsigned)snare_c, (unsigned)hat_c);
    }

    /*
     * Velocity drives the filter, not just the level: hit harder, sound
     * brighter.  The two notes are compared as high-frequency level over
     * their own level, so the loudness difference cancels out.
     */
    {
        nj_u1 file[128];
        nj_u4 n, fr = 0u;
        nj_u1 *m;
        nj_u8 loud_hf = 0u, loud_rms = 0u, soft_hf = 0u, soft_rms = 0u;

        n = build_at(file, 0u, 69u, 127u, 40u);
        m = nj_midi_render(file, n, &fr, &MEM);
        check(m != NULL, "a hard note renders");
        if (m != NULL) {
            loud_hf = pcm_hf(m, fr, NJ_MIDI_RATE / 10u, NJ_MIDI_RATE / 2u);
            loud_rms = pcm_rms(m, fr, NJ_MIDI_RATE / 10u, NJ_MIDI_RATE / 2u);
            t_free(NULL, m);
        }
        n = build_at(file, 0u, 69u, 32u, 40u);
        m = nj_midi_render(file, n, &fr, &MEM);
        if (m != NULL) {
            soft_hf = pcm_hf(m, fr, NJ_MIDI_RATE / 10u, NJ_MIDI_RATE / 2u);
            soft_rms = pcm_rms(m, fr, NJ_MIDI_RATE / 10u, NJ_MIDI_RATE / 2u);
            t_free(NULL, m);
        }
        check(loud_rms > 0u && soft_rms > 0u
              && loud_hf * soft_rms > soft_hf * loud_rms * 2u,
              "a hard note is brighter, not only louder");
        printf("     brightness (hf/rms x1000): hard %llu, soft %llu\n",
               (unsigned long long)(loud_hf * 1000u / (loud_rms ? loud_rms : 1u)),
               (unsigned long long)(soft_hf * 1000u / (soft_rms ? soft_rms : 1u)));
    }

    /*
     * A bowed or a blown instrument breathes even when the file never sends
     * CC1, which is why the families that do carry a small default vibrato.
     * Measured on a low note: strings wander, a piano does not.
     */
    {
        nj_u1 file[128];
        nj_u4 n, fr = 0u;
        nj_u1 *m;
        nj_u4 str_spread = 0u, pno_spread = 0u;

        n = build_at(file, 0u, 45u, 100u, 40u);      /* A2, strings */
        m = nj_midi_render(file, n, &fr, &MEM);
        check(m != NULL, "a low strings note renders");
        if (m != NULL) {
            str_spread = pcm_period_spread(m, fr, NJ_MIDI_RATE / 20u,
                                           NJ_MIDI_RATE / 2u);
            t_free(NULL, m);
        }
        n = build_at(file, 0u, 45u, 100u, 0u);       /* A2, piano */
        m = nj_midi_render(file, n, &fr, &MEM);
        if (m != NULL) {
            pno_spread = pcm_period_spread(m, fr, NJ_MIDI_RATE / 20u,
                                           NJ_MIDI_RATE / 2u);
            t_free(NULL, m);
        }
        check(str_spread >= 2u && pno_spread <= 1u,
              "strings breathe on their own, a piano does not");
        printf("     period spread: strings %u, piano %u samples\n",
               (unsigned)str_spread, (unsigned)pno_spread);
    }

    /*
     * CC10 puts a voice between the speakers.  A file that never sends it
     * stays mono - the whole corpus but a few hundred files - and a file that
     * does gets two channels, so the cost is only paid where it buys
     * something.  Hard left has to be loud on one side and silent on the
     * other.
     */
    {
        static const nj_u1 left[] = { 0xB0, 10, 0 };        /* CC10 = 0, hard left */
        nj_u1 file[128];
        nj_u4 n, fr = 0u, chans = 0u;
        nj_u1 *m;
        nj_u8 l = 0u, r = 0u;

        n = build_voiced(file, left, (nj_u4)sizeof(left), 0u, 69u);
        m = nj_midi_render_ex(file, n, &fr, &chans, &MEM);
        check(m != NULL && chans == 2u, "a file that pans renders in stereo");
        if (m != NULL) {
            l = pcm_rms_ch(m, fr, chans, 0u, NJ_MIDI_RATE / 10u, NJ_MIDI_RATE / 2u);
            r = pcm_rms_ch(m, fr, chans, 1u, NJ_MIDI_RATE / 10u, NJ_MIDI_RATE / 2u);
            t_free(NULL, m);
        }
        check(l > 0u && r * 10u < l,
              "hard left is loud on the left and silent on the right");
        printf("     pan left: left %llu, right %llu\n",
               (unsigned long long)l, (unsigned long long)r);

        n = build_voiced(file, NULL, 0u, 0u, 69u);
        m = nj_midi_render_ex(file, n, &fr, &chans, &MEM);
        check(m != NULL && chans == 1u, "a file that never pans stays mono");
        if (m != NULL) {
            t_free(NULL, m);
        }
    }

    /*
     * The stream is the whole-file renderer made resumable: whatever the chunk
     * size, it has to write the same bytes - and a song past the sixty second
     * ceiling has to keep going, because a quarter of the corpus is longer
     * than that and the buffer for one of those fits no heap we run on.
     */
    {
        static nj_u1 got[65536];
        static nj_u1 chunk[4096];
        static const nj_u4 sizes[5] = { 1u, 7u, 64u, 512u, 4096u };
        static const nj_u1 left[] = { 0xB0, 10, 0 };        /* CC10 = 0, hard left */
        nj_u1 file[512];
        nj_u4 pass;

        for (pass = 0u; pass < 2u; pass++) {
            nj_u4 n, whole_frames = 0u, chans = 0u, total = 0u, k = 0u;
            nj_u1 *whole;
            struct nj_midi_stream *s;

            n = build_voiced(file, (pass == 0u) ? NULL : left,
                             (pass == 0u) ? 0u : (nj_u4)sizeof(left), 0u, 69u);
            whole = nj_midi_render_ex(file, n, &whole_frames, &chans, &MEM);
            s = nj_midi_stream_open(file, n, &MEM);
            check(whole != NULL && s != NULL, "a phrase opens as a stream");
            if (whole == NULL || s == NULL) {
                continue;
            }
            check(nj_midi_stream_channels(s) == chans,
                  "the stream knows how many channels the song needs");
            check(nj_midi_stream_frames(s) >= whole_frames,
                  "the stream knows how long the song is");
            for (;;) {
                nj_u4 want = sizes[k % 5u];
                nj_u4 gotn;
                k++;
                /* pull() writes want * chans * 2 bytes: keep it inside chunk */
                if (want > (nj_u4)(sizeof(chunk) / (nj_size)chans / 2u)) {
                    want = (nj_u4)(sizeof(chunk) / (nj_size)chans / 2u);
                }
                gotn = nj_midi_stream_pull(s, chunk, want);
                if (gotn == 0u) {
                    break;
                }
                if ((nj_size)total + (nj_size)gotn * chans * 2u > sizeof(got)) {
                    break;
                }
                memcpy(got + total, chunk, (nj_size)gotn * chans * 2u);
                total += gotn * chans * 2u;
            }
            check(total == whole_frames * chans * 2u,
                  "ragged pulls write exactly as many bytes as the whole file");
            check(total > 0u && memcmp(got, whole, total) == 0,
                  "and the bytes are the same ones");
            printf("     stream pass %u: %u frames, %u channels, %u bytes\n",
                   (unsigned)pass, (unsigned)whole_frames, (unsigned)chans,
                   (unsigned)total);
            t_free(NULL, whole);
            nj_midi_stream_close(s);
        }
    }

    {
        static const nj_u1 long_body[] = {
            0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,   /* tempo: 500000 us */
            0x00, 0xC0, 40,                             /* strings: held */
            0x00, 0x90, 69, 100,                        /* A4 on */
            0x87, 0xA3, 0x00, 0x80, 69, 64,             /* +115200 ticks = 120 s: off */
            0x00, 0xFF, 0x2F, 0x00
        };
        nj_u1 file[128];
        nj_u4 n = build_track(file, long_body, (nj_u4)sizeof(long_body));
        nj_u4 fr = 0u, chans = 0u;
        nj_u1 *whole = nj_midi_render_ex(file, n, &fr, &chans, &MEM);
        struct nj_midi_stream *s = nj_midi_stream_open(file, n, &MEM);

        check(s != NULL && nj_midi_stream_frames(s) > NJ_MIDI_MAX_FRAMES,
              "a stream has no sixty second ceiling");
        check(whole != NULL && fr == NJ_MIDI_MAX_FRAMES,
              "the whole-file render still stops at the ceiling");
        printf("     long song: stream %u frames, whole file %u\n",
               (unsigned)((s != NULL) ? nj_midi_stream_frames(s) : 0u),
               (unsigned)fr);
        t_free(NULL, whole);
        nj_midi_stream_close(s);
    }

    /*
     * The General MIDI effects block is where 仙剑奇侠传 puts both of its
     * 160-note melody lines (program 99).  Rendering it as noise turned the
     * theme into a hiss: a player heard the drums and the string chords and
     * reported no melody at all.  A note on an effects program has to keep
     * its pitch, while the sound-effect block stays noise.
     */
    {
        nj_u1 file[128];
        nj_u4 fr = 0u;
        nj_u1 *m;
        nj_u4 n, fx, sfx;

        n = build_at(file, 0u, 69u, 100u, 99u);
        m = nj_midi_render(file, n, &fr, &MEM);
        fx = (m != NULL) ? pcm_crossings(m, fr, NJ_MIDI_RATE / 20u,
                                         NJ_MIDI_RATE * 3u / 10u) : 0u;
        if (m != NULL) {
            t_free(NULL, m);
        }
        n = build_at(file, 0u, 69u, 100u, 120u);
        m = nj_midi_render(file, n, &fr, &MEM);
        sfx = (m != NULL) ? pcm_crossings(m, fr, NJ_MIDI_RATE / 20u,
                                          NJ_MIDI_RATE * 3u / 10u) : 0u;
        if (m != NULL) {
            t_free(NULL, m);
        }
        check(fx > 0u && fx < 400u, "an effects program still has a pitch");
        check(sfx > 800u, "the sound-effect block is still noise");
        if (!(fx > 0u && fx < 400u)) {
            printf("     effects crossings %u\n", (unsigned)fx);
        }
        if (!(sfx > 800u)) {
            printf("     sound-effect crossings %u\n", (unsigned)sfx);
        }
    }

    /*
     * RP-015: CC121 puts a channel back to its defaults - volume 100,
     * expression 127, pan centre, no modulation, pedal up, wheel centred.
     * 518 of the corpus's controller messages are this one, and ignoring it
     * left a channel quiet or off to one side for the rest of the song.
     */
    {
        static const nj_u1 reset_vol[] = { 0xB0, 7, 40, 0x00, 0xB0, 121, 0 };
        static const nj_u1 plain_vol[] = { 0xB0, 7, 40 };
        static const nj_u1 reset_pan[] = { 0xB0, 10, 0, 0x00, 0xB0, 121, 0 };
        nj_u1 file[128];
        nj_u4 n, fr = 0u, chans = 0u, loud, quiet;
        nj_u1 *m;

        n = build_voiced(file, reset_vol, (nj_u4)sizeof(reset_vol), 0u, 69u);
        m = nj_midi_render(file, n, &fr, &MEM);
        loud = (m != NULL) ? pcm_rms(m, fr, NJ_MIDI_RATE / 10u, NJ_MIDI_RATE / 2u) : 0u;
        if (m != NULL) {
            t_free(NULL, m);
        }
        n = build_voiced(file, plain_vol, (nj_u4)sizeof(plain_vol), 0u, 69u);
        m = nj_midi_render(file, n, &fr, &MEM);
        quiet = (m != NULL) ? pcm_rms(m, fr, NJ_MIDI_RATE / 10u, NJ_MIDI_RATE / 2u) : 0u;
        if (m != NULL) {
            t_free(NULL, m);
        }
        check(quiet > 0u && loud * 3u > quiet * 7u,
              "CC121 puts the channel volume back to 100");
        if (!(quiet > 0u && loud * 3u > quiet * 7u)) {
            printf("     reset rms %u, no reset %u\n", (unsigned)loud, (unsigned)quiet);
        }

        n = build_voiced(file, reset_pan, (nj_u4)sizeof(reset_pan), 0u, 69u);
        m = nj_midi_render_ex(file, n, &fr, &chans, &MEM);
        check(m != NULL && chans == 2u, "a reset after a pan is still stereo");
        if (m != NULL) {
            nj_u8 l = pcm_rms_ch(m, fr, chans, 0u, NJ_MIDI_RATE / 10u, NJ_MIDI_RATE / 2u);
            nj_u8 r = pcm_rms_ch(m, fr, chans, 1u, NJ_MIDI_RATE / 10u, NJ_MIDI_RATE / 2u);
            check(l > 0u && r > 0u && l * 10u < r * 11u && r * 10u < l * 11u,
                  "CC121 puts the pan back to the centre");
            if (!(l > 0u && r > 0u && l * 10u < r * 11u && r * 10u < l * 11u)) {
                printf("     reset pan: left %llu, right %llu\n",
                       (unsigned long long)l, (unsigned long long)r);
            }
            t_free(NULL, m);
        }
    }

    t_free(NULL, pcm);
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures != 0;
}
