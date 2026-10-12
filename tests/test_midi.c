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

    /* 480 ticks at 500000 us per quarter is 0.5 s, plus the 50 ms tail the
     * renderer adds for the release.  The sample count follows the rate. */
    check(frames >= NJ_MIDI_RATE / 2u
          && frames <= NJ_MIDI_RATE / 2u + NJ_MIDI_RATE / 10u,
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
            0x00, 0xC0, 40,                             /* strings */
            0x00, 0xB0, 64, 127,                        /* pedal down */
            0x00, 0x90, 69, 100,                        /* A4 on */
            0x81, 0x70, 0x80, 69, 64,                   /* +240: note off */
            0x81, 0x70, 0xB0, 64, 0,                    /* +240: pedal up */
            0x00, 0xFF, 0x2F, 0x00
        };
        static const nj_u1 dry_body[] = {
            0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,
            0x00, 0xC0, 40,
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
            released = pcm_rms(pcm, fr, NJ_MIDI_RATE * 52u / 100u,
                               NJ_MIDI_RATE * 55u / 100u);
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

    t_free(NULL, pcm);
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures != 0;
}
