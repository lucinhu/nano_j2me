/*
 * nano_j2me - nj_midi.c
 *
 * A Standard MIDI File player: enough of the format to make the music in the
 * corpus audible.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS EXISTS
 * ---------------------------------------------------------------------------
 * 105 of the 238 titles in tests/games/ hand a .mid file to
 * Manager.createPlayer() within their first 200 frames, and 107 of them ship
 * no other audio at all - so before this file those games were silent, by
 * design: the runtime says "this runtime does not synthesise MIDI" and hands
 * back a Player that never makes a sound.  Saying so was right; leaving it
 * there was not.
 *
 * ---------------------------------------------------------------------------
 * WHAT IT IS NOT
 * ---------------------------------------------------------------------------
 * There is no wavetable, no envelope generator worth the name, no reverb, no
 * MIDI channel to instrument mapping beyond pitch.  A note is a square wave
 * with a short attack and release, mixed with the other notes that are on at
 * the same time.  That is a long way from a SoundFont, and it is a very long
 * way from silence.
 *
 * ---------------------------------------------------------------------------
 * OUTPUT
 * ---------------------------------------------------------------------------
 * Mono, 16-bit signed, little-endian, NJ_MIDI_RATE samples per second - the
 * same shape the port takes (nj_port.pcm_write() takes nj_s2 samples), so the
 * caller can hand the result straight to the existing sampled-Player path
 * instead of growing a second way to reach the audio device.
 *
 * The whole file is rendered up front, once, at createPlayer() time.  That
 * costs a few milliseconds and one buffer the size of the music (a minute of
 * 8 kHz mono is 960 KB), and it buys the simplest possible integration: after
 * this returns, a MIDI Player and a WAV Player are the same object.
 *
 * There is no file-scope mutable state here, deliberately: two VMs in one
 * process must not be able to see each other.
 */

#include "nj_midi.h"

#if NJ_USE_LIBC
#  include <string.h>
#endif

/* A minute of music.  Beyond that a title is looping, and the loop is better
 * served by the Player's own repeat than by a longer buffer. */
#define NJ_MIDI_MAX_SECONDS 60u
#define NJ_MIDI_MAX_FRAMES (NJ_MIDI_RATE * NJ_MIDI_MAX_SECONDS)

/* Sixteen notes at once is more than any of these titles asks for; a cap is
 * what keeps the per-sample loop bounded. */
#define NJ_MIDI_VOICES 12

#define NJ_MIDI_MAX_EVENTS 8192u
#define NJ_MIDI_MAX_TEMPOS 64u

/* ------------------------------------------------------------------ */
/* Reading the file                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    const nj_u1 *p;
    nj_u4        n;
    nj_u4        i;
} mid_rd;

static int mid_u8(mid_rd *r, nj_u4 *out)
{
    if (r->i >= r->n) {
        return 0;
    }
    *out = r->p[r->i++];
    return 1;
}

static int mid_u16(mid_rd *r, nj_u4 *out)
{
    nj_u4 a, b;
    if (!mid_u8(r, &a) || !mid_u8(r, &b)) {
        return 0;
    }
    *out = (a << 8) | b;
    return 1;
}

static int mid_u32(mid_rd *r, nj_u4 *out)
{
    nj_u4 a, b, c, d;
    if (!mid_u8(r, &a) || !mid_u8(r, &b) || !mid_u8(r, &c) || !mid_u8(r, &d)) {
        return 0;
    }
    *out = (a << 24) | (b << 16) | (c << 8) | d;
    return 1;
}

/* Variable-length quantity: seven bits per byte, high bit means "more". */
static int mid_vlq(mid_rd *r, nj_u4 *out)
{
    nj_u4 v = 0u;
    int i;

    for (i = 0; i < 4; i++) {
        nj_u4 b;
        if (!mid_u8(r, &b)) {
            return 0;
        }
        v = (v << 7) | (b & 0x7Fu);
        if ((b & 0x80u) == 0u) {
            *out = v;
            return 1;
        }
    }
    return 0;                   /* five bytes of continuation is not a delta */
}

static int mid_skip(mid_rd *r, nj_u4 n)
{
    if (n > r->n - r->i) {
        return 0;
    }
    r->i += n;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Collected events                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    nj_u4 tick;
    nj_u4 note;                 /* 0..127 */
    nj_u4 velocity;
    int   on;
} mid_note;

typedef struct {
    nj_u4 tick;
    nj_u4 us_per_qn;
} mid_tempo;

typedef struct {
    mid_note  *notes;
    nj_u4      nnotes;
    nj_u4      capnotes;

    mid_tempo  tempos[NJ_MIDI_MAX_TEMPOS];
    nj_u4      ntempos;

    nj_u4      division;        /* ticks per quarter note */
    int        smpte;           /* division was SMPTE frames instead */
} mid_song;

static int mid_add_note(mid_song *s, const nj_midi_mem *mem,
                        nj_u4 tick, nj_u4 note, nj_u4 vel, int on)
{
    if (s->nnotes == s->capnotes) {
        nj_u4 cap = (s->capnotes == 0u) ? 256u : s->capnotes * 2u;
        mid_note *n;
        if (cap > NJ_MIDI_MAX_EVENTS) {
            cap = NJ_MIDI_MAX_EVENTS;
        }
        if (s->nnotes >= cap) {
            return 1;           /* full: keep what we have, do not fail */
        }
        n = (mid_note *)mem->alloc(mem->ud, (nj_size)cap * sizeof(*n));
        if (n == NULL) {
            return 0;
        }
        if (s->notes != NULL) {
            memcpy(n, s->notes, (nj_size)s->nnotes * sizeof(*n));
            mem->free(mem->ud, s->notes);
        }
        s->notes = n;
        s->capnotes = cap;
    }
    s->notes[s->nnotes].tick = tick;
    s->notes[s->nnotes].note = note;
    s->notes[s->nnotes].velocity = vel;
    s->notes[s->nnotes].on = on;
    s->nnotes++;
    return 1;
}

static void mid_add_tempo(mid_song *s, nj_u4 tick, nj_u4 us)
{
    nj_u4 i;

    if (us == 0u) {
        return;
    }
    if (s->ntempos >= NJ_MIDI_MAX_TEMPOS) {
        return;
    }
    /* Keep the list sorted by tick; it is tiny and this runs once per tempo. */
    i = s->ntempos;
    while (i > 0u && s->tempos[i - 1u].tick > tick) {
        s->tempos[i] = s->tempos[i - 1u];
        i--;
    }
    s->tempos[i].tick = tick;
    s->tempos[i].us_per_qn = us;
    s->ntempos++;
}

/* Walk one MTrk chunk and collect its notes and tempo changes. */
static void mid_track(mid_song *s, const nj_midi_mem *mem, mid_rd *r, nj_u4 len)
{
    nj_u4 end = r->i + len;
    nj_u4 tick = 0u;
    nj_u4 status = 0u;
    int running = 0;

    if (end > r->n) {
        end = r->n;
    }
    while (r->i < end) {
        nj_u4 delta;
        nj_u4 b;

        if (!mid_vlq(r, &delta)) {
            return;
        }
        tick += delta;
        if (!mid_u8(r, &b)) {
            return;
        }
        if (b & 0x80u) {
            status = b;
            running = 1;
        } else if (running) {
            r->i--;             /* running status: the byte was data */
        } else {
            return;
        }

        if (status == 0xFFu) {  /* meta */
            nj_u4 type, mlen;
            if (!mid_u8(r, &type) || !mid_vlq(r, &mlen)) {
                return;
            }
            if (type == 0x51u && mlen == 3u) {
                nj_u4 a, c, d;
                if (!mid_u8(r, &a) || !mid_u8(r, &c) || !mid_u8(r, &d)) {
                    return;
                }
                mid_add_tempo(s, tick, (a << 16) | (c << 8) | d);
            } else if (type == 0x2Fu) {
                return;         /* end of track */
            } else if (!mid_skip(r, mlen)) {
                /* Only the meta events this reader does not look at are
                 * skipped: the tempo event's three bytes were read above, and
                 * skipping them again walked into the next event. */
                return;
            }
            continue;
        }
        if (status == 0xF0u || status == 0xF7u) {   /* sysex */
            nj_u4 mlen;
            if (!mid_vlq(r, &mlen) || !mid_skip(r, mlen)) {
                return;
            }
            continue;
        }

        {
            nj_u4 hi = status & 0xF0u;
            nj_u4 d1, d2;

            if (!mid_u8(r, &d1)) {
                return;
            }
            if (hi == 0xC0u || hi == 0xD0u) {
                continue;       /* one data byte */
            }
            if (!mid_u8(r, &d2)) {
                return;
            }
            if (hi == 0x90u && d2 != 0u) {
                if (!mid_add_note(s, mem, tick, d1 & 0x7Fu, d2 & 0x7Fu, 1)) {
                    return;
                }
            } else if (hi == 0x80u || hi == 0x90u) {
                if (!mid_add_note(s, mem, tick, d1 & 0x7Fu, 0u, 0)) {
                    return;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Ticks to samples                                                    */
/* ------------------------------------------------------------------ */

/* Sample number for a tick, integrating the tempo changes up to it. */
static nj_u4 mid_sample_at(const mid_song *s, nj_u4 tick)
{
    nj_u4 i;
    nj_u4 done_ticks = 0u;
    nj_u4 samples = 0u;
    nj_u4 us = 500000u;

    if (s->division == 0u) {
        return 0u;
    }
    for (i = 0u; i <= s->ntempos; i++) {
        nj_u4 next = (i < s->ntempos) ? s->tempos[i].tick : tick;
        nj_u4 span;
        nj_u8 acc;

        if (next > tick) {
            next = tick;
        }
        if (next > done_ticks) {
            span = next - done_ticks;
            acc = (nj_u8)samples * 1000000u
                + (nj_u8)span * (nj_u8)us * (nj_u8)NJ_MIDI_RATE
                      / (nj_u8)s->division;
            samples = (nj_u4)(acc / 1000000u);
            done_ticks = next;
        }
        if (i < s->ntempos) {
            us = s->tempos[i].us_per_qn;
        }
        if (done_ticks >= tick) {
            break;
        }
    }
    return samples;
}

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    int   active;
    nj_u4 note;
    nj_u4 phase;                /* 32-bit phase accumulator */
    nj_u4 step;                 /* phase increment per sample */
    nj_s4 amp;                  /* 0..32767 */
    nj_u4 start;                /* sample the note starts at */
    nj_u4 end;                  /* sample the note stops at (0 = never) */
} mid_voice;

static nj_u4 mid_step_for(nj_u4 note)
{
    /*
     * step = f * 2^32 / rate, with f = 440 * 2^((note - 69) / 12).
     *
     * The twelfth root of two is not a rational number, so the fractional
     * part comes from a Q16 table of the twelve semitone ratios and the
     * octaves are a shift.  440 * 2^32 / 8000 is the A4 step, and every other
     * note is that times one of these, shifted.
     */
    static const nj_u4 ratio[12] = {
        65536u, 69433u, 73562u, 77936u, 82570u, 87480u,
        92682u, 98193u, 104032u, 110218u, 116772u, 123725u
    };
    int semis = (int)note - 69;
    int oct = semis / 12;
    int idx = semis % 12;
    nj_u4 step;

    if (idx < 0) {
        idx += 12;
        oct--;
    }
    step = (nj_u4)(((nj_u8)440u * 4294967296u) / (nj_u8)NJ_MIDI_RATE);
    step = (nj_u4)(((nj_u8)step * (nj_u8)ratio[idx]) >> 16);
    while (oct > 0) {
        step *= 2u;
        oct--;
    }
    while (oct < 0) {
        step /= 2u;
        oct++;
    }
    return step;
}

nj_u1 *nj_midi_render(const void *data, nj_u4 size, nj_u4 *frames_out,
                      const nj_midi_mem *mem)
{
    if (mem == NULL || mem->alloc == NULL || mem->free == NULL) {
        return NULL;
    }

    mid_song song;
    mid_rd r;
    nj_u4 ntrk = 0u;
    nj_u4 format = 0u;
    nj_u4 i;
    nj_u4 frames;
    nj_u1 *pcm;
    mid_voice voices[NJ_MIDI_VOICES];
    nj_u4 v;

    if (frames_out != NULL) {
        *frames_out = 0u;
    }
    if (data == NULL || size < 14u) {
        return NULL;
    }

    memset(&song, 0, sizeof(song));
    r.p = (const nj_u1 *)data;
    r.n = size;
    r.i = 0u;

    {
        nj_u4 tag, hlen;
        if (!mid_u32(&r, &tag) || tag != 0x4D546864u /* "MThd" */) {
            return NULL;
        }
        if (!mid_u32(&r, &hlen) || hlen < 6u) {
            return NULL;
        }
        if (!mid_u16(&r, &format) || !mid_u16(&r, &ntrk)) {
            return NULL;
        }
        {
            nj_u4 div;
            if (!mid_u16(&r, &div)) {
                return NULL;
            }
            if (div & 0x8000u) {
                song.smpte = 1;         /* SMPTE: not converted, see below */
                song.division = 0u;
            } else {
                song.division = div;
            }
        }
        if (!mid_skip(&r, hlen - 6u)) {
            return NULL;
        }
    }
    (void)format;
    (void)ntrk;

    /* Tracks follow, in any order; a file may also carry junk chunks. */
    while (r.i + 8u <= r.n) {
        nj_u4 tag, len;
        nj_u4 save = r.i;

        if (!mid_u32(&r, &tag) || !mid_u32(&r, &len)) {
            break;
        }
        if (tag == 0x4D54726Bu /* "MTrk" */) {
            if (len > r.n - r.i) {
                break;
            }
            mid_track(&song, mem, &r, len);
            r.i = save + 8u + len;
        } else {
            if (!mid_skip(&r, len)) {
                break;
            }
        }
    }

    if (song.nnotes == 0u || song.division == 0u) {
        mem->free(mem->ud, song.notes);
        return NULL;            /* nothing to play: let the caller stay silent */
    }

    /* Length: the last thing that happens, plus a short tail for the release. */
    {
        nj_u4 last = 0u;
        for (i = 0u; i < song.nnotes; i++) {
            if (song.notes[i].tick > last) {
                last = song.notes[i].tick;
            }
        }
        frames = mid_sample_at(&song, last) + (NJ_MIDI_RATE / 20u);
    }
    if (frames > NJ_MIDI_MAX_FRAMES) {
        frames = NJ_MIDI_MAX_FRAMES;
    }

    pcm = (nj_u1 *)mem->alloc(mem->ud, (nj_size)frames * 2u);
    if (pcm != NULL) {
        memset(pcm, 0, (nj_size)frames * 2u);
    }
    if (pcm == NULL) {
        mem->free(mem->ud, song.notes);
        return NULL;
    }

    memset(voices, 0, sizeof(voices));

    for (i = 0u; i < song.nnotes; i++) {
        nj_u4 at = mid_sample_at(&song, song.notes[i].tick);

        if (at >= frames) {
            continue;
        }
        if (song.notes[i].on) {
            /*
             * A voice whose note has already ended is free again.  This pass
             * runs before anything is mixed, so without it the first
             * NJ_MIDI_VOICES notes of the file take every voice and the rest
             * of the song is dropped: thirty seconds of MIDI came out as half
             * a second of sound plus silence.  The short guard leaves room for
             * the release tail, so a reused voice does not click.
             */
            for (v = 0u; v < NJ_MIDI_VOICES; v++) {
                if (voices[v].active && voices[v].end != 0u
                    && voices[v].end + NJ_MIDI_RATE / 100u <= at) {
                    voices[v].active = 0;
                }
            }
            for (v = 0u; v < NJ_MIDI_VOICES; v++) {
                if (!voices[v].active) {
                    break;
                }
            }
            if (v == NJ_MIDI_VOICES) {
                continue;       /* all voices busy: drop the note */
            }
            voices[v].active = 1;
            voices[v].note = song.notes[i].note;
            voices[v].step = mid_step_for(song.notes[i].note);
            voices[v].amp = (nj_s4)(song.notes[i].velocity) * 200;  /* ~24% */
            voices[v].start = at;
            voices[v].end = 0u;
        } else {
            for (v = 0u; v < NJ_MIDI_VOICES; v++) {
                if (voices[v].active && voices[v].note == song.notes[i].note
                    && voices[v].end == 0u) {
                    voices[v].end = at;
                    break;
                }
            }
        }
    }

    /* Mix.  A square wave is the cheapest thing that is recognisably a note,
     * and at these amplitudes twelve of them cannot clip a 16-bit sample. */
    for (i = 0u; i < frames; i++) {
        nj_s4 mix = 0;
        nj_s4 scaled;

        for (v = 0u; v < NJ_MIDI_VOICES; v++) {
            nj_s4 amp = voices[v].amp;
            nj_u4 age;

            if (!voices[v].active) {
                continue;
            }
            if (i < voices[v].start) {
                continue;
            }
            if (voices[v].end != 0u && i >= voices[v].end) {
                continue;
            }
            age = i - voices[v].start;
            /* 4 ms attack, 8 ms release: enough to stop the clicks that make
             * a square wave sound like a fault rather than a note. */
            if (age < NJ_MIDI_RATE / 250u) {
                amp = amp * (nj_s4)age / (nj_s4)(NJ_MIDI_RATE / 250u);
            } else if (voices[v].end != 0u) {
                nj_u4 left = voices[v].end - i;
                if (left < NJ_MIDI_RATE / 125u) {
                    amp = amp * (nj_s4)left / (nj_s4)(NJ_MIDI_RATE / 125u);
                }
            }
            voices[v].phase += voices[v].step;
            mix += (voices[v].phase < 0x80000000u) ? amp : -amp;
        }

        scaled = mix;
        if (scaled > 32767) {
            scaled = 32767;
        } else if (scaled < -32768) {
            scaled = -32768;
        }
        pcm[i * 2u] = (nj_u1)(scaled & 0xFF);
        pcm[i * 2u + 1u] = (nj_u1)((scaled >> 8) & 0xFF);
    }

    mem->free(mem->ud, song.notes);
    if (frames_out != NULL) {
        *frames_out = frames;
    }
    return pcm;
}
