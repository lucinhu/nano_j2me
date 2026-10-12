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

    /* 480 ticks at 500000 us per quarter is 0.5 s; at 8 kHz that is 4000
     * samples, plus the 50 ms tail the renderer adds for the release. */
    check(frames == 4400u, "the render is 0.5 s plus the release tail");
    if (frames != 4400u) {
        printf("     frames = %u\n", (unsigned)frames);
    }

    for (i = 0u; i < 4000u && i * 2u + 1u < frames * 2u; i++) {
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

    t_free(NULL, pcm);
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures != 0;
}
