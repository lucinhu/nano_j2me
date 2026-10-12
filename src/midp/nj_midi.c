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
 * (controller 7 and 11), its pitch bend and its sustain pedal, and channel 10
 * is played as drums.  A note is one
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

/*
 * Vibrato: CC1 is how deep, and 5 Hz is the rate a phone synth wobbled at.
 * Full depth is half a semitone, the usual General MIDI figure.  The phase
 * advances once per 64 samples - 2.9 ms, sixty steps per cycle - so the step
 * is recomputed about as often as a wobble can be heard.
 */
#define NJ_MIDI_VIBRATO_HZ   5u
#define NJ_MIDI_VIBRATO_MAX  1920u          /* Q16 ratio at CC1 = 127 (2.9%) */
#define NJ_MIDI_VIBRATO_STEP \
    ((nj_u4)((((nj_u8)1u << 32) / (NJ_MIDI_RATE / 64u)) * NJ_MIDI_VIBRATO_HZ))

/*
 * Reverb: CC91 is how much of a channel goes into the room.  Two feedback
 * combs - 59 ms and 32 ms - are what a cheap box had, and they are enough to
 * stop a chord sounding like it was played in a vacuum.  The send defaults to
 * nothing, so a file that never asks for reverb sounds exactly as it did.
 */
#define NJ_MIDI_REV_A      1300u        /* 59 ms at 22 kHz */
#define NJ_MIDI_REV_B       700u        /* 32 ms */
#define NJ_MIDI_REV_LEN    (NJ_MIDI_REV_A + NJ_MIDI_REV_B)
#define NJ_MIDI_REV_FB_A  15000         /* Q15: 0.46 */
#define NJ_MIDI_REV_FB_B  12000         /* Q15: 0.37 */
#define NJ_MIDI_REV_TAIL  (NJ_MIDI_RATE / 2u)

#define NJ_MIDI_MAX_EVENTS 8192u
#define NJ_MIDI_MAX_TEMPOS 64u
#define NJ_MIDI_MAX_CTLS 2048u

/* Channel events the renderer follows while the notes play. */
#define MID_CTL_BEND  0u
#define MID_CTL_PEDAL 1u
#define MID_CTL_RANGE 2u
#define MID_CTL_MOD   3u            /* CC1: how deep the vibrato goes */
#define MID_CTL_REVERB 4u           /* CC91: how much of the note goes to the room */
#define MID_CTL_PAN   5u            /* CC10: where the note sits between the speakers */

/* Pitch bend: 14 bits, 8192 is no bend. */
#define MID_BEND_CENTRE 8192u
#define MID_BEND_DEFAULT_RANGE 2u

/* The percussion channel of a General MIDI file. */
#define MID_CHANNEL_DRUMS 9u

/* Waveforms.  Square is the cheapest; the others cost one compare and one
 * multiply, and they are what keeps a bass line from sounding like a buzzer. */
#define MID_W_SQUARE   0u
#define MID_W_TRIANGLE 1u
#define MID_W_SAW      2u
#define MID_W_NOISE    3u
#define MID_W_DRUM     4u      /* noise with a tone body under it */

typedef struct {
    nj_u1 wave;
    nj_u2 decay_ms;             /* 0 = the note holds until it is released */
    nj_u2 attack_ms;            /* how long it takes to reach full level */
    nj_u2 release_ms;           /* how long it takes to fall silent */
    nj_u1 sustain;              /* 0..127: the level the decay settles on */
    nj_u1 pitch_drop;           /* 128 = starts an octave high (a kick) */
    nj_u2 pitch_ms;             /* how long the pitch takes to fall */
    nj_u1 tone;                 /* MID_W_DRUM: the tone's share, 0..127 */
    nj_u1 vib;                  /* default vibrato depth, 0..127, no CC1 needed */
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
/*
 * The General MIDI "effects" block (96..103) is not a sound effect: composers
 * use it for pads and bells, and 仙剑奇侠传 puts BOTH of its 160-note melody
 * lines on program 99.  Rendering that block as noise turned the theme into a
 * hiss, so a player heard the drums and the string chords and no melody.
 * Noise is kept for the true sound-effect block (120..127) and for drums.
 */
static const mid_timbre mid_timbres[16] = {
    { MID_W_TRIANGLE, 1400u,   2u, 140u,   0u ,   0u, 0u , 0u , 0u },   /* piano: struck, dies away */
    { MID_W_TRIANGLE,  900u,   1u, 120u,   0u ,   0u, 0u , 0u , 0u },   /* chromatic percussion */
    { MID_W_SQUARE,      0u,   6u,  40u, 110u ,   0u, 0u , 0u , 0u },   /* organ: instant, holds */
    { MID_W_SAW,      1200u,   3u, 130u,   0u ,   0u, 0u , 0u , 0u },   /* guitar */
    { MID_W_TRIANGLE,  700u,   2u,  90u,   0u ,   0u, 0u , 0u , 0u },   /* bass */
    { MID_W_SAW,         0u,  45u, 180u, 100u ,   0u, 0u , 0u , 30u },   /* strings: slow, sustained */
    { MID_W_SAW,         0u,  55u, 200u, 100u ,   0u, 0u , 0u , 26u },   /* ensemble */
    { MID_W_SQUARE,      0u,  25u,  90u, 105u ,   0u, 0u , 0u , 20u },   /* brass: a small swell */
    { MID_W_SQUARE,      0u,  20u,  80u, 110u ,   0u, 0u , 0u , 22u },   /* reed */
    { MID_W_TRIANGLE,    0u,  30u,  70u, 105u ,   0u, 0u , 0u , 18u },   /* pipe */
    { MID_W_SAW,         0u,   8u,  80u, 100u ,   0u, 0u , 0u , 28u },   /* synth lead */
    { MID_W_TRIANGLE,    0u, 120u, 300u,  95u ,   0u, 0u , 0u , 16u },   /* synth pad: the slowest */
    { MID_W_TRIANGLE,    0u,  40u, 250u,  90u ,   0u, 0u , 0u , 0u },   /* effects: a pad, not noise */
    { MID_W_TRIANGLE,  900u,   5u, 120u,   0u ,   0u, 0u , 0u , 0u },   /* ethnic */
    { MID_W_TRIANGLE,  600u,   1u, 120u,   0u ,   0u, 0u , 0u , 0u },   /* percussive: bells, boxes */
    { MID_W_NOISE,     250u,   2u, 150u,   0u ,   0u, 0u , 0u , 0u }    /* sound effects */
};

static void mid_drum(nj_u4 note, mid_timbre *t)
{
    /*
     * A drum is a skin and a rattle.  The skins - kick and toms - start high
     * and fall, which is what makes them sound struck rather than tuned; the
     * metal is noise, and the open pieces ring far longer than the closed
     * ones.  Everything here is short, ends silent, and has no sustain.
     */
    t->wave = MID_W_NOISE;
    t->decay_ms = 90u;
    t->attack_ms = 1u;
    t->release_ms = 40u;
    t->sustain = 0u;
    t->pitch_drop = 0u;
    t->pitch_ms = 0u;
    t->tone = 0u;

    if (note == 35u || note == 36u) {               /* kick */
        t->wave = MID_W_TRIANGLE;
        t->decay_ms = 120u;
        t->pitch_drop = 128u;                       /* an octave above the note */
        t->pitch_ms = 60u;
    } else if (note == 37u) {                       /* side stick */
        t->decay_ms = 40u;
        t->release_ms = 20u;
    } else if (note == 38u || note == 40u) {        /* snare */
        t->wave = MID_W_DRUM;                       /* wires over a 180 Hz head */
        t->tone = 70u;
        t->decay_ms = 130u;
        t->release_ms = 30u;
    } else if (note == 39u) {                       /* hand clap */
        t->decay_ms = 100u;
        t->release_ms = 30u;
    } else if (note == 42u || note == 44u) {        /* closed and pedal hat */
        t->decay_ms = 45u;
        t->release_ms = 15u;
    } else if (note == 46u) {                       /* open hat: it rings */
        t->decay_ms = 300u;
        t->release_ms = 60u;
    } else if (note == 49u || note == 57u) {        /* crash */
        t->decay_ms = 900u;
        t->release_ms = 120u;
    } else if (note == 51u || note == 59u) {        /* ride */
        t->decay_ms = 700u;
        t->release_ms = 120u;
    } else if (note == 52u) {                       /* china */
        t->decay_ms = 800u;
        t->release_ms = 120u;
    } else if (note == 53u) {                       /* bell */
        t->wave = MID_W_TRIANGLE;
        t->decay_ms = 600u;
        t->release_ms = 120u;
    } else if (note == 54u) {                       /* tambourine */
        t->decay_ms = 200u;
        t->release_ms = 40u;
    } else if (note == 55u) {                       /* splash */
        t->decay_ms = 400u;
        t->release_ms = 80u;
    } else if (note == 56u) {                       /* cowbell */
        t->wave = MID_W_SQUARE;
        t->decay_ms = 250u;
        t->release_ms = 60u;
    } else if (note >= 41u && note <= 50u) {        /* toms */
        t->wave = MID_W_TRIANGLE;
        t->decay_ms = (note >= 50u) ? 160u : 200u;
        t->release_ms = 50u;
        t->pitch_drop = 96u;                        /* a fifth and a half up */
        t->pitch_ms = 120u;
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
    nj_u4 chan;
    nj_u1 wave;                 /* resolved from the channel's program */
    nj_u1 tone;                 /* MID_W_DRUM: the tone's share, 0..127 */
    nj_u1 vib;                  /* the family's default vibrato, 0..127 */
    nj_u2 decay_ms;             /* 0 = held */
    nj_u1 pitch_drop;           /* a drum's skin starts above its note */
    nj_u2 pitch_ms;
    nj_u2 attack_ms;
    nj_u2 release_ms;
    nj_u1 sustain;              /* 0..127 of full level */
    nj_u4 gain;                 /* channel volume * expression, 0..127 */
    int   on;
} mid_note;

/* A bend, a pedal or a bend-range change: things that happen to notes that are
 * already sounding, so they cannot live in the note list. */
typedef struct {
    nj_u4 tick;
    nj_u4 chan;
    nj_u4 kind;
    nj_u4 a;                    /* bend: the 14-bit value; else the value */
} mid_ctl;

typedef struct {
    nj_u4 tick;
    nj_u4 us_per_qn;
} mid_tempo;

typedef struct {
    mid_note  *notes;
    nj_u4      nnotes;
    nj_u4      capnotes;

    mid_ctl   *ctls;
    nj_u4      nctls;
    nj_u4      capctls;

    mid_tempo  tempos[NJ_MIDI_MAX_TEMPOS];
    nj_u4      ntempos;

    nj_u4      division;        /* ticks per quarter note */
    int        smpte;           /* division was SMPTE frames instead */

    /* Channel state, shared by every track: a format 1 file sets a program in
     * one track and plays it in another. */
    nj_u4      program[16];
    nj_u4      volume[16];      /* controller 7, 100 is the General MIDI default */
    nj_u4      expr[16];        /* controller 11, 127 is the default */
    nj_u4      bend[16];        /* last pitch bend seen, 14 bits */
    nj_u4      bend_range[16];  /* semitones at full deflection, from RPN 0 */
    nj_u4      pedal[16];       /* sustain pedal state at the end of the file */
    nj_u4      mod[16];         /* CC1 vibrato depth at the end of the file */
    nj_u4      reverb[16];      /* CC91 send at the end of the file */
    nj_u4      rpn[16];         /* the registered parameter being selected */
    nj_u4      pan[16];         /* CC10 at the end of the file, 64 is centre */
    int        pan_used;        /* any CC10 at all: only then is the mix stereo */
    nj_u2      max_release_ms;  /* the longest release any note asked for */
} mid_song;

static int mid_add_note(mid_song *s, const nj_midi_mem *mem,
                        nj_u4 tick, nj_u4 note, nj_u4 vel, int on,
                        const mid_timbre *t, nj_u4 gain, nj_u4 chan)
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
    s->notes[s->nnotes].chan = chan;
    s->notes[s->nnotes].wave = (t != NULL) ? t->wave : 0u;
    s->notes[s->nnotes].tone = (t != NULL) ? t->tone : 0u;
    s->notes[s->nnotes].vib = (t != NULL) ? t->vib : 0u;
    s->notes[s->nnotes].decay_ms = (t != NULL) ? t->decay_ms : 0u;
    s->notes[s->nnotes].attack_ms = (t != NULL) ? t->attack_ms : 0u;
    s->notes[s->nnotes].release_ms = (t != NULL) ? t->release_ms : 0u;
    s->notes[s->nnotes].sustain = (t != NULL) ? t->sustain : 0u;
    s->notes[s->nnotes].pitch_drop = (t != NULL) ? t->pitch_drop : 0u;
    s->notes[s->nnotes].pitch_ms = (t != NULL) ? t->pitch_ms : 0u;
    if (t != NULL && t->release_ms > s->max_release_ms) {
        s->max_release_ms = t->release_ms;      /* the render has to outlast it */
    }
    s->notes[s->nnotes].gain = gain;
    s->notes[s->nnotes].on = on;
    s->nnotes++;
    return 1;
}

static int mid_add_ctl(mid_song *s, const nj_midi_mem *mem,
                       nj_u4 tick, nj_u4 chan, nj_u4 kind, nj_u4 a)
{
    if (s->nctls == s->capctls) {
        nj_u4 cap = (s->capctls == 0u) ? 64u : s->capctls * 2u;
        mid_ctl *c;
        if (cap > NJ_MIDI_MAX_CTLS) {
            cap = NJ_MIDI_MAX_CTLS;
        }
        if (s->nctls >= cap) {
            return 1;           /* full: keep what we have, do not fail */
        }
        c = (mid_ctl *)mem->alloc(mem->ud, (nj_size)cap * sizeof(*c));
        if (c == NULL) {
            return 0;
        }
        if (s->ctls != NULL) {
            memcpy(c, s->ctls, (nj_size)s->nctls * sizeof(*c));
            mem->free(mem->ud, s->ctls);
        }
        s->ctls = c;
        s->capctls = cap;
    }
    s->ctls[s->nctls].tick = tick;
    s->ctls[s->nctls].chan = chan;
    s->ctls[s->nctls].kind = kind;
    s->ctls[s->nctls].a = a;
    s->nctls++;
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
            if (hi == 0xE0u) {          /* pitch bend: 14 bits, centre 8192 */
                nj_u4 val = ((d2 & 0x7Fu) << 7) | (d1 & 0x7Fu);
                s->bend[chan] = val;
                if (!mid_add_ctl(s, mem, tick, chan, MID_CTL_BEND, val)) {
                    return;
                }
            } else if (hi == 0xB0u) {   /* control change */
                nj_u4 cc = d1 & 0x7Fu;
                nj_u4 val = d2 & 0x7Fu;
                if (cc == 7u) {
                    s->volume[chan] = val;
                } else if (cc == 11u) {
                    s->expr[chan] = val;
                } else if (cc == 64u) {         /* sustain pedal */
                    s->pedal[chan] = (val >= 64u) ? 1u : 0u;
                    if (!mid_add_ctl(s, mem, tick, chan, MID_CTL_PEDAL, val)) {
                        return;
                    }
                } else if (cc == 1u) {          /* modulation: vibrato depth */
                    s->mod[chan] = val;
                    if (!mid_add_ctl(s, mem, tick, chan, MID_CTL_MOD, val)) {
                        return;
                    }
                } else if (cc == 91u) {         /* reverb send */
                    s->reverb[chan] = val;
                    if (!mid_add_ctl(s, mem, tick, chan, MID_CTL_REVERB, val)) {
                        return;
                    }
                } else if (cc == 10u) {         /* pan: left 0, centre 64, right 127 */
                    s->pan[chan] = val;
                    s->pan_used = 1;
                    if (!mid_add_ctl(s, mem, tick, chan, MID_CTL_PAN, val)) {
                        return;
                    }
                } else if (cc == 101u) {        /* RPN MSB */
                    s->rpn[chan] = (s->rpn[chan] & 0x7Fu) | (val << 7);
                } else if (cc == 100u) {        /* RPN LSB */
                    s->rpn[chan] = (s->rpn[chan] & 0x3F80u) | val;
                } else if (cc == 6u && s->rpn[chan] == 0u) {
                    /* Data entry on RPN 0: how many semitones full deflection
                     * means.  A file that says 12 is asking for an octave. */
                    nj_u4 range = (val == 0u) ? 1u : ((val > 12u) ? 12u : val);
                    s->bend_range[chan] = range;
                    if (!mid_add_ctl(s, mem, tick, chan, MID_CTL_RANGE, range)) {
                        return;
                    }
                }
            } else if (hi == 0x90u && d2 != 0u) {
                mid_timbre t;
                nj_u4 gain;
                nj_u4 note = d1 & 0x7Fu;

                if (chan == MID_CHANNEL_DRUMS) {
                    mid_drum(note, &t);
                } else {
                    t = mid_timbres[(s->program[chan] & 0x7Fu) / 8u];
                }
                gain = (s->volume[chan] & 0x7Fu) * (s->expr[chan] & 0x7Fu) / 127u;
                if (!mid_add_note(s, mem, tick, note, d2 & 0x7Fu, 1,
                                  &t, gain, chan)) {
                    return;
                }
            } else if (hi == 0x80u || hi == 0x90u) {
                if (!mid_add_note(s, mem, tick, d1 & 0x7Fu, 0u, 0,
                                  NULL, 0u, chan)) {
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
    int   sustained;            /* the pedal is holding this note down */
    nj_u4 note;
    nj_u4 chan;
    nj_u4 note_step;            /* the note's own step, before the bend */
    nj_u4 base_step;            /* phase increment before the bend */
    nj_u4 pitch_mul;            /* Q16: the pitch envelope's multiplier */
    nj_u4 pitch_dec;            /* how much it falls per sample */
    nj_u4 pitch_left;           /* samples of fall left (0 = at the note) */
    nj_u4 lfo;                  /* vibrato phase, 32 bits */
    nj_u4 mod;                  /* vibrato depth, Q16, from the channel */
    nj_u4 rev;                  /* CC91 send, 0..127, from the channel */
    nj_u1 wave;
    nj_u1 tone;                 /* MID_W_DRUM: the tone's share */
    nj_u1 filt;                 /* low pass divisor: small = bright */
    nj_u1 gl, gr;               /* CC10 gains, 127 = full */
    nj_u4 phase;                /* 32-bit phase accumulator */
    nj_u4 step;                 /* phase increment per sample */
    nj_u4 noise;                /* linear feedback shift register, for noise */
    nj_s4 amp;                  /* 0..32767 */
    nj_s4 lp;                   /* one-pole low pass state */
    nj_u4 decay;                /* amplitude lost per sample (0 = held) */
    nj_s4 sustain;              /* the level the decay settles on */
    nj_u4 attack;               /* samples to reach full level */
    nj_u4 release;              /* samples to fall silent after the note off */
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
 * The channel's bend as a Q16 multiplier: 8192 is no bend, and the range says
 * how many semitones full deflection means.  The curve is linear where the ear
 * wants exponential, which is off by under two percent at full deflection -
 * inaudible for a bend.
 */
static nj_u4 mid_bend_ratio(nj_u4 bend, nj_u4 range)
{
    nj_s4 off = (nj_s4)bend - (nj_s4)MID_BEND_CENTRE;
    nj_s4 delta;

    if (off == 0 || range == 0u) {
        return 65536u;
    }
    delta = off * (nj_s4)range * 4014 / 8192;
    if (delta <= -60000) {
        return 5536u;           /* a floor, so the step never reaches zero */
    }
    return (nj_u4)(65536 + delta);
}

/*
 * The step a voice is actually using: its note's step with the channel's
 * bend, plus the vibrato the LFO is asking for right now.  A triangle is
 * what a cheap synth had, and the ear cannot tell it from a sine at 5 Hz.
 */
static void mid_set_step(mid_voice *v)
{
    nj_u4 t = v->lfo >> 16;                 /* 0 .. 65535 */
    nj_s4 lfo = (t < 32768u) ? (nj_s4)t : (nj_s4)(65535u - t);
    /* The pitch envelope rides on top of the bend: a kick starts an octave
     * high and falls to its note, which is what makes it a kick. */
    nj_u4 base = (nj_u4)(((nj_u8)v->base_step * (nj_u8)v->pitch_mul) >> 16);

    lfo = lfo * 2 - 32768;                  /* -32768 .. 32766 */
    if (v->mod == 0u) {
        v->step = base;
        return;
    }
    v->step = (nj_u4)((nj_s4)base
                      + (nj_s4)(((nj_s8)base * (nj_s8)lfo
                                 * (nj_s8)v->mod) >> 31));
}

static void mid_apply_pitch(mid_voice *v, nj_u4 bend, nj_u4 range)
{
    v->base_step = (nj_u4)(((nj_u8)v->note_step
                            * (nj_u8)mid_bend_ratio(bend, range)) >> 16);
    mid_set_step(v);
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

static void mid_sort_ctls(mid_song *s)
{
    nj_u4 i, j;
    for (i = 1u; i < s->nctls; i++) {
        mid_ctl key = s->ctls[i];
        j = i;
        while (j > 0u && s->ctls[j - 1u].tick > key.tick) {
            s->ctls[j] = s->ctls[j - 1u];
            j--;
        }
        s->ctls[j] = key;
    }
}

/*
 * CC10 is 0 at the left speaker, 64 in the middle and 127 at the right.  The
 * taper is linear, so a centred voice is louder than a hard-panned one - which
 * is what the phone synths did as well, and it keeps a file that pans one
 * instrument from going quiet.
 */
static void mid_pan_gains(nj_u4 pan, nj_u1 *gl, nj_u1 *gr)
{
    if (pan > 127u) {
        pan = 127u;
    }
    if (pan <= 64u) {
        *gl = 127u;
        *gr = (nj_u1)(pan * 2u);
    } else {
        *gl = (nj_u1)((127u - pan) * 2u);
        *gr = 127u;
    }
}

nj_u1 *nj_midi_render_ex(const void *data, nj_u4 size, nj_u4 *frames_out,
                         nj_u4 *chans_out, const nj_midi_mem *mem)
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
    nj_u4 chans = 1u;
    nj_u4 ev = 0u;
    nj_u4 cev = 0u;
    nj_u4 bend_now[16];
    nj_u4 range_now[16];
    int   pedal_now[16];
    nj_u4 mod_now[16];
    nj_u4 rev_now[16];
    nj_u4 pan_now[16];
    nj_s4 *rev = NULL;
    nj_u4 rev_pa = 0u, rev_pb = 0u, rev_any = 0u;

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
        song.bend[i] = MID_BEND_CENTRE;
        song.bend_range[i] = MID_BEND_DEFAULT_RANGE;
        song.pan[i] = 64u;
        mod_now[i] = 0u;
        rev_now[i] = 0u;
        pan_now[i] = 64u;
        song.rpn[i] = 0xFFFFu;          /* no registered parameter selected */
        bend_now[i] = MID_BEND_CENTRE;
        range_now[i] = MID_BEND_DEFAULT_RANGE;
        pedal_now[i] = 0;
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
        mem->free(mem->ud, song.ctls);
        return NULL;            /* nothing to play: let the caller stay silent */
    }

    /* Length: the last thing that happens, plus a short tail for the release.
     * Channel events count too: a pedal that comes up after the last note is
     * what stops that note, and a buffer that ended at the note-off would
     * leave it ringing to the end. */
    {
        nj_u4 last = 0u;
        for (i = 0u; i < song.nnotes; i++) {
            if (song.notes[i].tick > last) {
                last = song.notes[i].tick;
            }
        }
        for (i = 0u; i < song.nctls; i++) {
            if (song.ctls[i].tick > last) {
                last = song.ctls[i].tick;
            }
        }
        nj_u4 tail = NJ_MIDI_RATE / 20u;
        nj_u4 rel = (nj_u4)song.max_release_ms * (NJ_MIDI_RATE / 1000u);
        if (rel > tail) {
            tail = rel;         /* a pad's 300 ms release must not be cut off */
        }
        frames = mid_sample_at(&song, last) + tail;
    }
    for (i = 0u; i < 16u; i++) {
        if (song.reverb[i] != 0u) {
            rev_any = 1u;
        }
    }
    if (rev_any != 0u) {
        frames += NJ_MIDI_REV_TAIL;     /* the room needs time to empty */
    }
    if (frames > NJ_MIDI_MAX_FRAMES) {
        frames = NJ_MIDI_MAX_FRAMES;
    }

    if (rev_any != 0u) {
        rev = (nj_s4 *)mem->alloc(mem->ud, (nj_size)NJ_MIDI_REV_LEN * sizeof(nj_s4));
        if (rev != NULL) {
            memset(rev, 0, (nj_size)NJ_MIDI_REV_LEN * sizeof(nj_s4));
        }
    }
    chans = song.pan_used ? 2u : 1u;
    if (chans_out != NULL) {
        *chans_out = chans;
    }
    pcm = (nj_u1 *)mem->alloc(mem->ud, (nj_size)frames * (nj_size)chans * 2u);
    if (pcm != NULL) {
        memset(pcm, 0, (nj_size)frames * 2u);
    }
    if (pcm == NULL) {
        mem->free(mem->ud, song.notes);
        mem->free(mem->ud, song.ctls);
        return NULL;
    }

    mid_sort_notes(&song);
    mid_sort_ctls(&song);
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
        if ((i & 63u) == 0u) {
            for (v = 0u; v < NJ_MIDI_VOICES; v++) {
                if (voices[v].active && voices[v].mod != 0u) {
                    voices[v].lfo += NJ_MIDI_VIBRATO_STEP;
                    mid_set_step(&voices[v]);
                }
            }
        }
        nj_s4 mixL = 0;
        nj_s4 mixR = 0;
        nj_s4 bus = 0;
        nj_s4 scaledL;
        nj_s4 scaledR;

        /*
         * Channel events first: a bend or a pedal that lands on the same
         * sample as a note-on has to be in force for that note.
         */
        while (cev < song.nctls
               && mid_sample_at(&song, song.ctls[cev].tick) <= i) {
            nj_u4 ch = song.ctls[cev].chan;
            nj_u4 kind = song.ctls[cev].kind;

            if (kind == MID_CTL_BEND) {
                bend_now[ch] = song.ctls[cev].a;
                for (v = 0u; v < NJ_MIDI_VOICES; v++) {
                    if (voices[v].active && voices[v].chan == ch) {
                        mid_apply_pitch(&voices[v], bend_now[ch], range_now[ch]);
                    }
                }
            } else if (kind == MID_CTL_MOD) {
                mod_now[ch] = song.ctls[cev].a;
                for (v = 0u; v < NJ_MIDI_VOICES; v++) {
                    if (voices[v].active && voices[v].chan == ch) {
                        voices[v].mod = mod_now[ch] * NJ_MIDI_VIBRATO_MAX / 127u;
                        mid_set_step(&voices[v]);
                    }
                }
            } else if (kind == MID_CTL_PAN) {
                pan_now[ch] = song.ctls[cev].a;
                for (v = 0u; v < NJ_MIDI_VOICES; v++) {
                    if (voices[v].active && voices[v].chan == ch) {
                        mid_pan_gains(pan_now[ch], &voices[v].gl, &voices[v].gr);
                    }
                }
            } else if (kind == MID_CTL_REVERB) {
                rev_now[ch] = song.ctls[cev].a;
                for (v = 0u; v < NJ_MIDI_VOICES; v++) {
                    if (voices[v].active && voices[v].chan == ch) {
                        voices[v].rev = rev_now[ch];
                    }
                }
            } else if (kind == MID_CTL_RANGE) {
                range_now[ch] = song.ctls[cev].a;
                for (v = 0u; v < NJ_MIDI_VOICES; v++) {
                    if (voices[v].active && voices[v].chan == ch) {
                        mid_apply_pitch(&voices[v], bend_now[ch], range_now[ch]);
                    }
                }
            } else if (song.ctls[cev].a >= 64u) {
                pedal_now[ch] = 1;
            } else {
                /* The pedal came up: every note it was holding stops now. */
                pedal_now[ch] = 0;
                for (v = 0u; v < NJ_MIDI_VOICES; v++) {
                    if (voices[v].active && voices[v].chan == ch
                        && voices[v].sustained) {
                        voices[v].sustained = 0;
                        /* The release runs INTO the end, so the note keeps
                         * sounding while it fades. */
                        voices[v].end = i + voices[v].release;
                    }
                }
            }
            cev++;
        }

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
                    /*
                     * All twelve voices are busy.  Dropping the note is what
                     * made a melody vanish under a chord and a drum tail: the
                     * theme of 仙剑奇侠传 is the top line of a dense file, and
                     * a player only hears drums and chords.  The quietest
                     * voice goes instead - a released note first, then the
                     * lowest level, which is almost always a tail.
                     */
                    nj_s4 quiet = 0;
                    nj_u4 pick = 0u;
                    int have = 0;

                    for (v = 0u; v < NJ_MIDI_VOICES; v++) {
                        nj_s4 level = voices[v].amp;
                        if (voices[v].end != 0u) {
                            level /= 4;
                        }
                        if (!have || level < quiet) {
                            quiet = level;
                            pick = v;
                            have = 1;
                        }
                    }
                    v = pick;
                }
                voices[v].active = 1;
                voices[v].sustained = 0;
                voices[v].note = song.notes[ev].note;
                voices[v].chan = song.notes[ev].chan;
                voices[v].wave = song.notes[ev].wave;
                voices[v].tone = song.notes[ev].tone;
                /*
                 * Velocity is not only loudness.  A hard note opens the
                 * filter, the way a struck string or a blown reed gets
                 * brighter the harder it is played; velocity 100 keeps the
                 * divisor this voice used before the filter followed it.
                 */
                voices[v].filt = (nj_u1)(1u
                    + (127u - song.notes[ev].velocity) * 7u / 127u);
                voices[v].phase = 0u;
                voices[v].note_step = mid_step_for(song.notes[ev].note);
                voices[v].base_step = voices[v].note_step;
                voices[v].step = voices[v].base_step;
                /*
                 * The pitch envelope: a struck skin starts above its note and
                 * falls to it, so the first milliseconds are a hit rather
                 * than a note.  128 is an octave, and the fall is linear.
                 */
                voices[v].pitch_mul = 65536u;
                voices[v].pitch_dec = 0u;
                voices[v].pitch_left = 0u;
                if (song.notes[ev].pitch_drop != 0u
                    && song.notes[ev].pitch_ms != 0u) {
                    nj_u4 span = (nj_u4)song.notes[ev].pitch_ms
                        * (NJ_MIDI_RATE / 1000u);
                    if (span != 0u) {
                        nj_u4 hi = 65536u
                            + (nj_u4)song.notes[ev].pitch_drop * 512u;
                        voices[v].pitch_mul = hi;
                        voices[v].pitch_dec = (hi - 65536u) / span;
                        voices[v].pitch_left = span;
                    }
                }
                mid_apply_pitch(&voices[v], bend_now[song.notes[ev].chan],
                                range_now[song.notes[ev].chan]);
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
                voices[v].sustain = (nj_s4)(voices[v].amp
                    * (nj_s4)song.notes[ev].sustain / 127);
                voices[v].attack = (nj_u4)song.notes[ev].attack_ms
                    * (NJ_MIDI_RATE / 1000u);
                if (voices[v].attack == 0u) {
                    voices[v].attack = 1u;      /* no division by zero */
                }
                voices[v].release = (nj_u4)song.notes[ev].release_ms
                    * (NJ_MIDI_RATE / 1000u);
                if (voices[v].release == 0u) {
                    voices[v].release = NJ_MIDI_RATE / 125u;
                }
                voices[v].rev = rev_now[song.notes[ev].chan];
                mid_pan_gains(pan_now[song.notes[ev].chan], &voices[v].gl,
                              &voices[v].gr);
                voices[v].lfo = 0u;
                /*
                 * A string section or a reed breathes even when the file
                 * never sends CC1; a file that does send it wins.
                 */
                if (mod_now[song.notes[ev].chan] != 0u) {
                    voices[v].mod = mod_now[song.notes[ev].chan]
                        * NJ_MIDI_VIBRATO_MAX / 127u;
                } else {
                    voices[v].mod = (nj_u4)song.notes[ev].vib
                        * NJ_MIDI_VIBRATO_MAX / 127u;
                }
                mid_set_step(&voices[v]);
                voices[v].start = i;
                voices[v].end = 0u;
            } else {
                nj_u4 ch = song.notes[ev].chan;
                for (v = 0u; v < NJ_MIDI_VOICES; v++) {
                    if (voices[v].active && voices[v].note == song.notes[ev].note
                        && voices[v].chan == ch
                        && voices[v].end == 0u) {
                        if (pedal_now[ch]) {
                            voices[v].sustained = 1;    /* the pedal holds it */
                        } else {
                            /* The note stops sounding one release later: the
                             * ramp below fades it out over that time. */
                            voices[v].end = i + voices[v].release;
                        }
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
            if (voices[v].pitch_left != 0u) {
                voices[v].pitch_mul -= voices[v].pitch_dec;
                voices[v].pitch_left--;
                if (voices[v].pitch_left == 0u) {
                    voices[v].pitch_mul = 65536u;   /* it reached its note */
                }
                mid_set_step(&voices[v]);
            }
            if (voices[v].decay != 0u && voices[v].amp > voices[v].sustain) {
                /* A struck note loses amplitude every sample, whether or not
                 * the file ever sends a note off, but only down to the level
                 * its family sustains at. */
                voices[v].amp -= (nj_s4)voices[v].decay;
                if (voices[v].amp <= voices[v].sustain) {
                    voices[v].amp = voices[v].sustain;
                    if (voices[v].sustain <= 0) {
                        voices[v].active = 0;
                        continue;
                    }
                }
                amp = voices[v].amp;
            }
            age = i - voices[v].start;
            /*
             * The envelope each family asked for: an attack that is 2 ms on a
             * piano and 120 ms on a pad, and a release that is 15 ms on a
             * hi-hat and 300 ms on a pad.  The old fixed 4 ms/8 ms pair is
             * what made every instrument sound like the same buzzer.
             */
            if (age < voices[v].attack) {
                amp = amp * (nj_s4)age / (nj_s4)voices[v].attack;
            } else if (voices[v].end != 0u) {
                nj_u4 left = voices[v].end - i;
                if (left < voices[v].release) {
                    amp = amp * (nj_s4)left / (nj_s4)voices[v].release;
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
                } else if (voices[v].wave == MID_W_NOISE
                           || voices[v].wave == MID_W_DRUM) {
                    nj_s4 hiss;
                    voices[v].noise = (voices[v].noise >> 1)
                        ^ ((0u - (voices[v].noise & 1u)) & 0x80200003u);
                    hiss = ((nj_s4)(voices[v].noise & 0xFFFFu) - 32768) / 2;
                    if (voices[v].wave == MID_W_DRUM) {
                        /*
                         * A drum head is a tone under a noise skin: the
                         * snare's 180 Hz body below the wires is what makes
                         * it a snare instead of a hiss.
                         */
                        nj_s4 body = (voices[v].phase < 0x80000000u) ? 32767 : -32767;
                        raw = (body * (nj_s4)voices[v].tone
                               + hiss * (nj_s4)(127u - (nj_u4)voices[v].tone)) / 127;
                    } else {
                        raw = hiss;
                    }
                } else {
                    raw = (voices[v].phase < 0x80000000u) ? amp : -amp;
                }
                if (voices[v].wave != MID_W_SQUARE) {
                    raw = raw * amp / 32768;    /* scale to this note's level */
                }
                voices[v].lp += (raw - voices[v].lp) / (nj_s4)voices[v].filt;
                /*
                 * A mono file has both gains at full, so this is the same sum
                 * it always was; a file that pans puts each voice where it
                 * asked to be.
                 */
                mixL += voices[v].lp * (nj_s4)voices[v].gl / 127;
                mixR += voices[v].lp * (nj_s4)voices[v].gr / 127;
                if (voices[v].rev != 0u) {
                    bus += voices[v].lp * (nj_s4)voices[v].rev / 127;
                }
            }
        }

        /*
         * Two feedback combs on the send bus.  A channel with no send never
         * reaches them, so the default is the dry signal this always was.
         */
        if (rev != NULL) {
            nj_s4 ya = rev[rev_pa];
            nj_s4 yb = rev[NJ_MIDI_REV_A + rev_pb];

            rev[rev_pa] = bus + (nj_s4)((ya * NJ_MIDI_REV_FB_A) >> 15);
            rev[NJ_MIDI_REV_A + rev_pb] = bus + (nj_s4)((yb * NJ_MIDI_REV_FB_B) >> 15);
            if (++rev_pa >= NJ_MIDI_REV_A) {
                rev_pa = 0u;
            }
            if (++rev_pb >= NJ_MIDI_REV_B) {
                rev_pb = 0u;
            }
            {
                nj_s4 wet = (ya + yb) * 3 / 8;
                mixL += wet;        /* the room is in the middle */
                mixR += wet;
            }
        }

        /*
         * Twelve voices can sum past full scale, and a hard clamp on the sum
         * buzzes.  Above three quarters of the range the mix is compressed
         * four to one and only then clamped, which keeps a loud passage loud
         * without the crackle of clipping it.
         */
        scaledL = mixL;
        scaledR = mixR;
        if (scaledL > 24000) {
            scaledL = 24000 + (scaledL - 24000) / 4;
        } else if (scaledL < -24000) {
            scaledL = -24000 + (scaledL + 24000) / 4;
        }
        if (scaledL > 32767) {
            scaledL = 32767;
        } else if (scaledL < -32768) {
            scaledL = -32768;
        }
        if (scaledR > 24000) {
            scaledR = 24000 + (scaledR - 24000) / 4;
        } else if (scaledR < -24000) {
            scaledR = -24000 + (scaledR + 24000) / 4;
        }
        if (scaledR > 32767) {
            scaledR = 32767;
        } else if (scaledR < -32768) {
            scaledR = -32768;
        }
        if (chans == 2u) {
            pcm[i * 4u] = (nj_u1)(scaledL & 0xFF);
            pcm[i * 4u + 1u] = (nj_u1)((scaledL >> 8) & 0xFF);
            pcm[i * 4u + 2u] = (nj_u1)(scaledR & 0xFF);
            pcm[i * 4u + 3u] = (nj_u1)((scaledR >> 8) & 0xFF);
        } else {
            pcm[i * 2u] = (nj_u1)(scaledL & 0xFF);
            pcm[i * 2u + 1u] = (nj_u1)((scaledL >> 8) & 0xFF);
        }
    }

    mem->free(mem->ud, rev);
    mem->free(mem->ud, song.notes);
    mem->free(mem->ud, song.ctls);
    if (frames_out != NULL) {
        *frames_out = frames;
    }
    return pcm;
}

nj_u1 *nj_midi_render(const void *data, nj_u4 size, nj_u4 *frames_out,
                      const nj_midi_mem *mem)
{
    return nj_midi_render_ex(data, size, frames_out, NULL, mem);
}
