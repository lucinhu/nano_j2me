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
 * There is no wavetable and no reverb.  What there is: a General MIDI program
 * is mapped to a waveform family (square, triangle, saw, noise) and an
 * envelope (held, struck, percussive), the note's channel decides its volume
 * (controller 7 and 11) and channel 10 is played as drums.  A note is one
 * oscillator with a short attack and release, mixed with the other notes that
 * are on at the same time.  That is a long way from a SoundFont, and it is a
 * very long way from silence.
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

/* The percussion channel of a General MIDI file. */
#define MID_CHANNEL_DRUMS 9u

/* Waveforms.  Square is the cheapest; the others cost one compare and one
 * multiply, and they are what keeps a bass line from sounding like a buzzer. */
#define MID_W_SQUARE   0u
#define MID_W_TRIANGLE 1u
#define MID_W_SAW      2u
#define MID_W_NOISE    3u

typedef struct {
    nj_u1 wave;
    nj_u2 decay_ms;             /* 0 = the note holds until it is released */
} mid_timbre;

/*
 * One entry per General MIDI program block of eight: piano, chromatic
 * percussion, organ, guitar, bass, strings, ensemble, brass, reed, pipe, synth
 * lead, synth pad, effects, ethnic, percussive, sound effects.
 *
 * The choices are what the corpus rewards: a piano and a guitar are struck and
 * decay, strings and organs hold, a bass is a triangle because its fundamental
 * is what carries, and the effect banks are noise because there is nothing
 * else honest to make of them.
 */
static const mid_timbre mid_timbres[16] = {
    { MID_W_TRIANGLE, 1400u },
    { MID_W_TRIANGLE,  900u },
    { MID_W_SQUARE,      0u },
    { MID_W_SAW,      1200u },
    { MID_W_TRIANGLE,  700u },
    { MID_W_SAW,         0u },
    { MID_W_SAW,         0u },
    { MID_W_SQUARE,      0u },
    { MID_W_SQUARE,      0u },
    { MID_W_TRIANGLE,    0u },
    { MID_W_SAW,         0u },
    { MID_W_TRIANGLE,    0u },
    { MID_W_NOISE,     300u },
    { MID_W_TRIANGLE,  900u },
    { MID_W_NOISE,     200u },
    { MID_W_NOISE,     250u }
};

static void mid_drum(nj_u4 note, nj_u1 *wave, nj_u2 *decay_ms)
{
    if (note == 35u || note == 36u) {               /* kick */
        *wave = MID_W_TRIANGLE;
        *decay_ms = 120u;
    } else if (note == 38u || note == 40u) {        /* snare */
        *wave = MID_W_NOISE;
        *decay_ms = 130u;
    } else if (note == 42u || note == 44u || note == 46u) {  /* hats */
        *wave = MID_W_NOISE;
        *decay_ms = 45u;
    } else if (note >= 47u && note <= 50u) {        /* toms */
        *wave = MID_W_TRIANGLE;
        *decay_ms = 180u;
    } else {
        *wave = MID_W_NOISE;
        *decay_ms = 90u;
    }
}

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
    nj_u1 wave;                 /* resolved from the channel's program */
    nj_u2 decay_ms;             /* 0 = held */
    nj_u4 gain;                 /* channel volume * expression, 0..127 */
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

    /* Channel state, shared by every track: a format 1 file sets a program in
     * one track and plays it in another. */
    nj_u4      program[16];
    nj_u4      volume[16];      /* controller 7, 100 is the General MIDI default */
    nj_u4      expr[16];        /* controller 11, 127 is the default */
} mid_song;

static int mid_add_note(mid_song *s, const nj_midi_mem *mem,
                        nj_u4 tick, nj_u4 note, nj_u4 vel, int on,
                        nj_u1 wave, nj_u2 decay_ms, nj_u4 gain)
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
    s->notes[s->nnotes].wave = wave;
    s->notes[s->nnotes].decay_ms = decay_ms;
    s->notes[s->nnotes].gain = gain;
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
            nj_u4 chan = status & 0x0Fu;
            nj_u4 d1, d2;

            if (!mid_u8(r, &d1)) {
                return;
            }
            if (hi == 0xC0u) {          /* program change: one data byte */
                s->program[chan] = d1 & 0x7Fu;
                continue;
            }
            if (hi == 0xD0u) {
                continue;       /* channel pressure: one data byte, ignored */
            }
            if (!mid_u8(r, &d2)) {
                return;
            }
            if (hi == 0xB0u) {          /* control change */
                nj_u4 cc = d1 & 0x7Fu;
                if (cc == 7u) {
                    s->volume[chan] = d2 & 0x7Fu;
                } else if (cc == 11u) {
                    s->expr[chan] = d2 & 0x7Fu;
                }
            } else if (hi == 0x90u && d2 != 0u) {
                nj_u1 wave;
                nj_u2 decay_ms;
                nj_u4 gain;
                nj_u4 note = d1 & 0x7Fu;

                if (chan == MID_CHANNEL_DRUMS) {
                    mid_drum(note, &wave, &decay_ms);
                } else {
                    mid_timbre t = mid_timbres[(s->program[chan] & 0x7Fu) / 8u];
                    wave = t.wave;
                    decay_ms = t.decay_ms;
                }
                gain = (s->volume[chan] & 0x7Fu) * (s->expr[chan] & 0x7Fu) / 127u;
                if (!mid_add_note(s, mem, tick, note, d2 & 0x7Fu, 1,
                                  wave, decay_ms, gain)) {
                    return;
                }
            } else if (hi == 0x80u || hi == 0x90u) {
                if (!mid_add_note(s, mem, tick, d1 & 0x7Fu, 0u, 0,
                                  0u, 0u, 0u)) {
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
    nj_u1 wave;
    nj_u4 phase;                /* 32-bit phase accumulator */
    nj_u4 step;                 /* phase increment per sample */
    nj_u4 noise;                /* linear feedback shift register, for noise */
    nj_s4 amp;                  /* 0..32767 */
    nj_s4 lp;                   /* one-pole low pass state */
    nj_u4 decay;                /* amplitude lost per sample (0 = held) */
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

/*
 * Events are collected track by track, and a format 1 file may interleave
 * them, so put them in tick order before playback walks them.
 */
static void mid_sort_notes(mid_song *s)
{
    nj_u4 i, j;
    for (i = 1u; i < s->nnotes; i++) {
        mid_note key = s->notes[i];
        j = i;
        while (j > 0u && s->notes[j - 1u].tick > key.tick) {
            s->notes[j] = s->notes[j - 1u];
            j--;
        }
        s->notes[j] = key;
    }
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
    nj_u4 ev = 0u;

    if (frames_out != NULL) {
        *frames_out = 0u;
    }
    if (data == NULL || size < 14u) {
        return NULL;
    }

    memset(&song, 0, sizeof(song));
    for (i = 0u; i < 16u; i++) {
        song.volume[i] = 100u;
        song.expr[i] = 127u;
    }
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

    mid_sort_notes(&song);
    memset(voices, 0, sizeof(voices));

    /*
     * Play the events as they arrive: a voice is taken when its note starts
     * and given back when the note ends.
     *
     * Assigning a voice to every note BEFORE the mix - which is what this used
     * to do - leaves the output with only the notes that were still held when
     * that pass ended: the last twelve notes of the file, or, before voices
     * were ever given back, the first twelve.  Thirty seconds of MIDI came out
     * as half a second of sound plus silence, and once the notes were long
     * enough to hold every voice the whole song was silent - which is what the
     * SDL2 window played.
     */
    for (i = 0u; i < frames; i++) {
        nj_s4 mix = 0;
        nj_s4 scaled;

        while (ev < song.nnotes
               && mid_sample_at(&song, song.notes[ev].tick) <= i) {
            if (song.notes[ev].on) {
                /* A voice whose note has ended is free again.  The short guard
                 * leaves room for the release tail, so a reused voice does not
                 * click. */
                for (v = 0u; v < NJ_MIDI_VOICES; v++) {
                    if (voices[v].active && voices[v].end != 0u
                        && voices[v].end + NJ_MIDI_RATE / 100u <= i) {
                        voices[v].active = 0;
                    }
                }
                for (v = 0u; v < NJ_MIDI_VOICES; v++) {
                    if (!voices[v].active) {
                        break;
                    }
                }
                if (v == NJ_MIDI_VOICES) {
                    ev++;
                    continue;   /* all voices busy: drop the note */
                }
                voices[v].active = 1;
                voices[v].note = song.notes[ev].note;
                voices[v].wave = song.notes[ev].wave;
                voices[v].phase = 0u;
                voices[v].step = mid_step_for(song.notes[ev].note);
                voices[v].noise = 0x13579BDFu | 1u;
                /* ~24% of full scale at velocity 100, then the channel's own
                 * volume and expression. */
                voices[v].amp = (nj_s4)(song.notes[ev].velocity) * 300
                    * (nj_s4)(song.notes[ev].gain) / 127;
                voices[v].lp = 0;
                voices[v].decay = 0u;
                if (song.notes[ev].decay_ms != 0u) {
                    nj_u4 span = (nj_u4)(song.notes[ev].decay_ms)
                        * (NJ_MIDI_RATE / 1000u);
                    if (span != 0u) {
                        voices[v].decay = (nj_u4)(voices[v].amp / (nj_s4)span) + 1u;
                    }
                }
                voices[v].start = i;
                voices[v].end = 0u;
            } else {
                for (v = 0u; v < NJ_MIDI_VOICES; v++) {
                    if (voices[v].active && voices[v].note == song.notes[ev].note
                        && voices[v].end == 0u) {
                        voices[v].end = i;
                        break;
                    }
                }
            }
            ev++;
        }


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
            if (voices[v].decay != 0u) {
                /* A struck note loses amplitude every sample, whether or not
                 * the file ever sends a note off. */
                if (voices[v].amp <= (nj_s4)voices[v].decay) {
                    voices[v].active = 0;
                    continue;
                }
                voices[v].amp -= (nj_s4)voices[v].decay;
                amp = voices[v].amp;
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
            /*
             * Every waveform here is a shape of the same phase accumulator.
             * A square's harmonics fall off as 1/k, which is what makes it
             * buzz; a triangle's fall off as 1/k^2 and a saw's are brighter
             * than a square's but thinner, and noise has no pitch at all,
             * which is exactly what a hi-hat wants.
             *
             * The one-pole low pass costs one add and one shift, and it is
             * what keeps the harmonics above Nyquist from folding back into
             * the audible band as a metallic ring.
             */
            {
                nj_s4 raw;
                if (voices[v].wave == MID_W_TRIANGLE) {
                    nj_s4 t = (nj_s4)(voices[v].phase >> 16);
                    raw = (t < 32768) ? (t * 2 - 32768) : (98303 - t * 2);
                    raw = raw * 3 / 4;
                } else if (voices[v].wave == MID_W_SAW) {
                    raw = ((nj_s4)(voices[v].phase >> 16) - 32768) * 3 / 4;
                } else if (voices[v].wave == MID_W_NOISE) {
                    voices[v].noise = (voices[v].noise >> 1)
                        ^ ((0u - (voices[v].noise & 1u)) & 0x80200003u);
                    raw = ((nj_s4)(voices[v].noise & 0xFFFFu) - 32768) / 2;
                } else {
                    raw = (voices[v].phase < 0x80000000u) ? amp : -amp;
                }
                if (voices[v].wave != MID_W_SQUARE) {
                    raw = raw * amp / 32768;    /* scale to this note's level */
                }
                voices[v].lp += (raw - voices[v].lp) / 2;
                mix += voices[v].lp;
            }
        }

        /*
         * Twelve voices can sum past full scale, and a hard clamp on the sum
         * buzzes.  Above three quarters of the range the mix is compressed
         * four to one and only then clamped, which keeps a loud passage loud
         * without the crackle of clipping it.
         */
        scaled = mix;
        if (scaled > 24000) {
            scaled = 24000 + (scaled - 24000) / 4;
        } else if (scaled < -24000) {
            scaled = -24000 + (scaled + 24000) / 4;
        }
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
