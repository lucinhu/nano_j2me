/*
 * nano_j2me - nj_lib_media.c
 *
 * javax.microedition.media: Manager, Player, the two controls a J2ME game
 * actually uses, and WAV playback.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS IS NOT A TOY
 * ---------------------------------------------------------------------------
 * A game with no sound is a game missing half its feedback.  Before this file
 * existed every MIDlet that called Manager.playTone() died with
 * NoClassDefFoundError on its first sound effect, which is a hard failure in
 * the middle of gameplay rather than a silent omission.
 *
 * ---------------------------------------------------------------------------
 * STATE LIVES IN THE PLAYER OBJECT
 * ---------------------------------------------------------------------------
 * A Player is an ordinary Java object; every field the C side needs is declared
 * on it below and read back through nj_field_*.  There is no file-scope state
 * anywhere in this file - not a current player, not a device handle, not a
 * cached tone - because the project's rule is that two VMs in one process must
 * not be able to see each other.  The audio DEVICE belongs to the port, which
 * owns it through nj_port.ud like everything else.
 *
 * ---------------------------------------------------------------------------
 * TWO KINDS OF PLAYER
 * ---------------------------------------------------------------------------
 *   tone    a JSR-135 tone sequence, converted to nj_audio_tone entries and
 *           handed to the port in one call
 *   sampled PCM, pushed to the port a frame at a time.  Three containers
 *           arrive here and they all leave as the same object: a PCM WAV is
 *           sliced out of its chunk, a MIDI file is rendered by nj_midi.c, and
 *           an AMR narrowband clip is decoded by nj_amr.c - the two rendered
 *           formats up front, at createPlayer() time, so that by the time a
 *           Player exists there is only ever PCM in it.
 *
 * Sampled audio is pushed rather than dumped because a small part has a small
 * device buffer: pcm_write() returns how many frames it took, the rest stays in
 * the Player, and nj_media_frame() - called once per emulated frame - pushes
 * more.  A port that accepts everything (the headless one) finishes in the
 * first call; a port with a DMA buffer drains over several.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS NOT IMPLEMENTED
 * ---------------------------------------------------------------------------
 * No streaming protocols (http:// and rtsp:// raise MediaException rather than
 * pretending), no MPEG audio, no MP4, no AMR wideband, no video, no audio
 * capture, and no custom controls beyond ToneControl and VolumeControl.
 * Realize/prefetch are state transitions with real work behind them (the WAV is
 * parsed at realize) rather than no-ops, because a game that checks getState()
 * must see the truth.
 */
#include "nj_midp.h"
#include "nj_midi.h"
#if NJ_ENABLE_AMR
#  include "nj_amr.h"
#endif

#if NJ_USE_LIBC
#  include <string.h>
#  include <stdlib.h>
#endif

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_CALL(name, vm, th, args, ret) name((vm), (th), (args), (ret))
#define NJ_UNUSED(x) ((void)(x))

/*
 * The MIDI renderer allocates through the caller (see nj_midi.h) so that
 * nothing in the runtime reaches for libc: every byte goes through
 * nj_host_alloc, which is what lets teardown account for it.
 */
#if NJ_ENABLE_MIDI
static void *midi_alloc(void *ud, nj_size n)
{
    return nj_host_alloc((nj_vm *)ud, n);
}

static void midi_free(void *ud, void *p)
{
    nj_host_free((nj_vm *)ud, p);
}
#endif /* NJ_ENABLE_MIDI */

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */
#define NJ_C_PLAYER   "javax/microedition/media/Player"
#define NJ_C_MANAGER  "javax/microedition/media/Manager"
#define NJ_C_PLISTEN  "javax/microedition/media/PlayerListener"
#define NJ_C_MEXC     "javax/microedition/media/MediaException"
#define NJ_C_CONTROL  "javax/microedition/media/Control"
#define NJ_C_CONTROLLABLE "javax/microedition/media/Controllable"
#define NJ_C_TONE     "javax/microedition/media/control/ToneControl"
#define NJ_C_VOLUME   "javax/microedition/media/control/VolumeControl"
#define NJ_C_MIDICTL  "javax/microedition/media/control/MIDIControl"

/* Player fields.  Everything the C side needs, on the object. */
#define F_P_STATE    "pState"      /* I */
#define F_P_KIND     "pKind"       /* I: 0 tone, 1 sampled */
#define F_P_TYPE     "pType"       /* Ljava/lang/String; declared/known MIME */
#define F_P_DURATION "pDuration"   /* J microseconds */
#define F_P_TIME     "pMediaTime"  /* J microseconds */
#define F_P_LOOP     "pLoopCount"  /* I */
#define F_P_VOLUME   "pVolume"     /* I 0..100 */
#define F_P_MUTED    "pMuted"      /* Z */
#define F_P_SEQ      "pSeq"        /* [B the tone sequence as given */
#define F_P_TONES    "pTones"      /* [I freq,ms pairs */
#define F_P_NTONES   "pNTones"     /* I */
#define F_P_DATA     "pData"       /* [B PCM little-endian s16 */
#define F_P_STREAM   "pStream"     /* Z: pData is a stream handle, not PCM */

/*
 * A MIDI Player holds its music in C, not in the Java heap: pData is a handle
 * (the pointer in the first bytes) plus the scratch the pump pulls into.  That
 * is what lets a fifty minute song play on a heap that could never hold its
 * PCM - the whole file would be a hundred and forty megabytes.
 */
#define NJ_MEDIA_STREAM_FRAMES 4096u
#define NJ_MEDIA_STREAM_BYTES  ((nj_u4)sizeof(void *) + NJ_MEDIA_STREAM_FRAMES * 2u * 2u)
#define F_P_RATE     "pRate"       /* I */
#define F_P_CHAN     "pChans"      /* I */
#define F_P_FRAMES   "pFrames"     /* I */
#define F_P_PUSHED   "pPushed"     /* I frames handed to the device */
#define F_P_OPEN     "pOpen"       /* Z device open for this player */
#define F_P_LISTEN   "pListen"     /* Ljava/util/Vector; */

/*
 * A MIDI Player a PORT is playing (nj_port.midi_open).  The handle the port
 * handed back lives in pMidi the same way a stream handle lives in pData -
 * the pointer in the first bytes of a byte array - because the Java heap is
 * the only place this runtime keeps per-object state and a pointer is not a
 * Java value.
 *
 * pPlat is the flag that says which of the two MIDI paths this Player took,
 * and it is what the start/stop/seek branches below switch on.  pLoopDone
 * counts the plays of a setLoopCount(n) Player, which the platform player
 * cannot count for itself (a MediaPlayer loops forever or not at all).
 */
#define F_P_MIDI     "pMidi"       /* [B the port's MIDI handle, or NULL */
#define F_P_PLAT     "pPlat"       /* Z: pMidi is a port MIDI handle */
#define F_P_LOOPDONE "pLoopDone"   /* I plays finished, for loop count > 1 */
/*
 * pMidiK says the source is a MIDI song.  F_P_KIND cannot say it: kind 1
 * means "streamed", and a WAV is streamed too, so getControl() would hand a
 * MIDIControl to a WAV Player - which is exactly what a game must not get,
 * because every event it sends would be discarded in silence.
 */
#define F_P_MIDIK    "pMidiK"      /* Z: 1 when the source is MIDI */

/*
 * A Control is a separate object that acts on its Player, so it carries the
 * Player it belongs to.  Without this the control's natives would look the
 * Player fields up on the CONTROL - which silently finds nothing, so
 * setToneSequence() would appear to succeed and change nothing at all.  That is
 * the quietest possible way for this to be wrong, which is why it is a field
 * and not an assumption.
 */
#define F_C_OWNER    "cOwner"      /* Ljavax/microedition/media/Player; */

/* MediaException reason strings, in one place so tests can match them. */
#define NJ_MEXC_PROTO ": protocol not supported"
#define NJ_MEXC_TYPE  ": content type not supported"
#define NJ_MEXC_DATA  ": cannot parse the media"

/* ------------------------------------------------------------------ */
/* Field and object helpers                                            */
/* ------------------------------------------------------------------ */
static nj_field *mfld(nj_vm *vm, const char *cls, const char *name,
                      const char *desc)
{
    return nj_field_lookup(vm, cls, name, desc, 0);
}

/*
 * Int and boolean fields are both read and written as ints, but they are
 * DIFFERENT fields with different descriptors, and the lookup is by descriptor.
 * These helpers hardcoded "I", so every read of a Z field - F_P_OPEN is one -
 * silently returned 0 however the field was set.  That is the same mistake as
 * the cOwner lookup on the wrong class: a helper that assumes a descriptor
 * instead of being told one.
 */
static nj_s4 r_i(nj_vm *vm, nj_ref o, const char *name)
{
    nj_field *f = mfld(vm, NJ_C_PLAYER, name, "I");
    return f ? nj_field_read_int(vm, o, f) : 0;
}

static nj_s4 r_z(nj_vm *vm, nj_ref o, const char *name)
{
    nj_field *f = mfld(vm, NJ_C_PLAYER, name, "Z");
    return f ? nj_field_read_int(vm, o, f) : 0;
}

static void w_z(nj_vm *vm, nj_ref o, const char *name, nj_s4 v)
{
    nj_field *f = mfld(vm, NJ_C_PLAYER, name, "Z");
    if (f) {
        nj_field_write_int(vm, o, f, v ? 1 : 0);
    }
}

static void w_i(nj_vm *vm, nj_ref o, const char *name, nj_s4 v)
{
    nj_field *f = mfld(vm, NJ_C_PLAYER, name, "I");
    if (f) {
        nj_field_write_int(vm, o, f, v);
    }
}

static nj_s8 r_j(nj_vm *vm, nj_ref o, const char *name)
{
    nj_field *f = mfld(vm, NJ_C_PLAYER, name, "J");
    return f ? nj_field_read_long(vm, o, f) : 0;
}

static void w_j(nj_vm *vm, nj_ref o, const char *name, nj_s8 v)
{
    nj_field *f = mfld(vm, NJ_C_PLAYER, name, "J");
    if (f) {
        nj_field_write_long(vm, o, f, v);
    }
}

static nj_ref r_r(nj_vm *vm, nj_ref o, const char *name, const char *desc)
{
    nj_field *f = mfld(vm, NJ_C_PLAYER, name, desc);
    return f ? nj_field_read_ref(vm, o, f) : NULL;
}

static void w_r(nj_vm *vm, nj_ref o, const char *name, const char *desc,
                nj_ref v)
{
    nj_field *f = mfld(vm, NJ_C_PLAYER, name, desc);
    if (f) {
        nj_field_write_ref(vm, o, f, v);
    }
}

/*
 * A restored arena contains byte arrays with pointers from another process.
 * Forget them without closing or dereferencing them.  Called for EVERY arena
 * object before restore enables GC, including Players never registered by
 * start().  Native songs become silent, empty sampled Players: closing them
 * here would make the game's next start() throw instead of resuming.  PCM and
 * tone data live entirely in the arena and survive, with device output stopped.
 * This only edits the destination VM; saving must leave its source playing.
 */
void nj_media_restore_object(nj_vm *vm, nj_ref player)
{
    nj_class *c;

    if (!player || player->kind != NJ_OBJ_PLAIN) {
        return;
    }
    for (c = player->cls; c; c = c->super) {
        if (strcmp(c->name, NJ_C_PLAYER) == 0) {
            int native_song = r_z(vm, player, F_P_STREAM) ||
                              r_z(vm, player, F_P_PLAT);
            if (native_song) {
                w_r(vm, player, F_P_DATA, "[B", NULL);
                w_z(vm, player, F_P_MUTED, 1);
                w_i(vm, player, F_P_KIND, 1);
                w_i(vm, player, F_P_FRAMES, 0);
                w_j(vm, player, F_P_TIME, 0);
                w_j(vm, player, F_P_DURATION, 0);
            }
            w_r(vm, player, F_P_MIDI, "[B", NULL);
            w_z(vm, player, F_P_STREAM, 0);
            w_z(vm, player, F_P_PLAT, 0);
            w_z(vm, player, F_P_OPEN, 0);
            w_i(vm, player, F_P_PUSHED, 0);
            w_i(vm, player, F_P_LOOPDONE, 0);
            if (r_i(vm, player, F_P_STATE) == 400) {
                w_i(vm, player, F_P_STATE, 300);
            }
            return;
        }
    }
}

/*
 * Control fields are looked up on the CONTROL's class, not on Player.
 *
 * The r_r/w_r helpers above are hardcoded to NJ_C_PLAYER because almost every
 * field here belongs to a Player - but cOwner does not, and using them for it
 * found nothing and wrote nowhere.  The symptom was a setToneSequence() that
 * appeared to succeed and a getToneSequence() that returned null, which reads
 * like a broken control rather than a wrong field lookup.
 */
static nj_ref ctl_get(nj_vm *vm, nj_ref ctl, const char *name, const char *desc)
{
    nj_field *f;
    if (!ctl || !ctl->cls) {
        return NULL;
    }
    f = nj_field_lookup(vm, ctl->cls->name, name, desc, 0);
    return f ? nj_field_read_ref(vm, ctl, f) : NULL;
}

static void ctl_put(nj_vm *vm, nj_ref ctl, const char *cls, const char *name,
                    const char *desc, nj_ref v)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    if (f) {
        nj_field_write_ref(vm, ctl, f, v);
    }
}

/* The Player a Control acts on. */
static nj_ref ctl_owner(nj_vm *vm, nj_ref ctl)
{
    return ctl_get(vm, ctl, F_C_OWNER, "L" NJ_C_PLAYER ";");
}

/* java.util.Vector, through the usual virtual dispatch. */
static nj_s4 vec_size(nj_vm *vm, nj_ref v)
{
    nj_value r;
    if (!v) {
        return 0;
    }
    r.i = 0;
    if (nj_call(vm, v, "java/util/Vector", "size", "()I", NULL, &r) != NJ_OK) {
        nj_clear_exception(vm);
        return 0;
    }
    return r.i;
}

static nj_ref vec_at(nj_vm *vm, nj_ref v, nj_s4 i)
{
    nj_value a[4];
    nj_value r;
    a[0].i = i;
    r.r = NULL;
    if (nj_call(vm, v, "java/util/Vector", "elementAt",
                "(I)Ljava/lang/Object;", a, &r) != NJ_OK) {
        nj_clear_exception(vm);
        return NULL;
    }
    return r.r;
}

static int vec_add(nj_vm *vm, nj_ref v, nj_ref o)
{
    nj_value a[4];
    a[0].r = o;
    return nj_call(vm, v, "java/util/Vector", "addElement",
                   "(Ljava/lang/Object;)V", a, NULL) == NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Byte arrays                                                         */
/* ------------------------------------------------------------------ */
static nj_u4 barray_len(nj_ref a)
{
    return (a && a->kind == NJ_OBJ_ARRAY) ? a->length : 0u;
}

static const nj_u1 *barray_bytes(nj_ref a)
{
    return (a && a->kind == NJ_OBJ_ARRAY) ? a->u.bytes : NULL;
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */
/*
 * Tell every registered listener what happened.  MIDP lets a listener be
 * registered more than once and expects to be told each time, so this walks the
 * Vector rather than deduplicating - a game that adds a listener twice has
 * asked for two callbacks.
 *
 * The event string is interned through the VM's string table, so a game can
 * compare it with PlayerListener.STARTED using reference equality, which is what
 * the MIDP javadoc tells it to do.
 */
static void fire(nj_vm *vm, nj_ref player, const char *event)
{
    nj_ref lst = r_r(vm, player, F_P_LISTEN, "Ljava/util/Vector;");
    nj_ref ev;
    nj_s4 n;
    nj_s4 i;

    if (!lst) {
        return;
    }
    ev = nj_string_new(vm, event);
    if (!ev) {
        nj_clear_exception(vm);
        return;
    }
    n = vec_size(vm, lst);
    for (i = 0; i < n; i++) {
        nj_ref l = vec_at(vm, lst, i);
        nj_value a[4];
        if (!l) {
            continue;
        }
        a[0].r = player;
        a[1].r = ev;
        a[2].r = NULL;              /* event-specific data, always NULL here */
        if (nj_call(vm, l, NJ_C_PLISTEN, "playerUpdate",
                    "(L" NJ_C_PLAYER ";Ljava/lang/String;Ljava/lang/Object;)V",
                    a, NULL) != NJ_OK) {
            /*
             * A listener that throws is the game's problem, not ours: MIDP says
             * the player keeps going, and letting the exception escape would
             * stop playback for a bug in someone's UI code.  The exception is
             * cleared so the next listener still runs.
             */
            nj_clear_exception(vm);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Tone sequences (JSR 135)                                            */
/* ------------------------------------------------------------------ */
/*
 * A tone sequence is a big-endian length in the first two bytes followed by
 * triples of (note, duration_units, duration).  A note of 0 is a rest; the
 * frequency is 440 * 2^((note - 69) / 12) Hz, the MIDI convention.  Converting here rather than in the port keeps every port free of
 * the format, which is the point of the port being small.
 */
static nj_u2 tone_freq(int note)
{

    /*
     * 440 * 2^((note - 69) / 12), rounded, for the 128 MIDI notes.  A table
     * rather than pow() because a part with no FPU must still be able to play a
     * jingle, and because the result has to be the same on every target.
     *
     * The index IS the MIDI note: index 69 is A4 = 440 Hz, which is the one
     * value that makes this table checkable at a glance.  An earlier version of
     * it was shifted by ten - 440 sat at index 59 - which put every note about
     * a fifth-and-a-half too high.  Nothing failed; the tune was just wrong.
     */
    static const nj_u2 table[128] = {
        8, 9, 9, 10, 10, 11, 12, 12,
        13, 14, 15, 15, 16, 17, 18, 19,
        21, 22, 23, 24, 26, 28, 29, 31,
        33, 35, 37, 39, 41, 44, 46, 49,
        52, 55, 58, 62, 65, 69, 73, 78,
        82, 87, 92, 98, 104, 110, 117, 123,
        131, 139, 147, 156, 165, 175, 185, 196,
        208, 220, 233, 247, 262, 277, 294, 311,
        330, 349, 370, 392, 415, 440, 466, 494,
        523, 554, 587, 622, 659, 698, 740, 784,
        831, 880, 932, 988, 1047, 1109, 1175, 1245,
        1319, 1397, 1480, 1568, 1661, 1760, 1865, 1976,
        2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136,
        3322, 3520, 3729, 3951, 4186, 4435, 4699, 4978,
        5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902,
        8372, 8870, 9397, 9956, 10548, 11175, 11840, 12544
    };
    if (note <= 0 || note > 127) {
        return 0u;      /* 0 is a rest, and >127 is not a note */
    }
    /* Index IS the note.  This read table[note - 1] to match a table that was
     * itself shifted by ten; fixing one without the other just moved the
     * error.  Both are right now, and the check is that table[69] is 440. */
    return table[note];
}

/*
 * Parse a tone sequence into port tones.  Returns the number written, or -1 if
 * the sequence is malformed - a sequence whose declared length disagrees with
 * its size, or whose triples run past the end, is a MediaException and not a
 * silent truncation.
 */
static int tone_parse(const nj_u1 *b, nj_u4 len, nj_audio_tone *out, int cap)
{
    nj_u4 declared;
    nj_u4 p;
    int n = 0;

    if (!b || len < 2u) {
        return -1;
    }
    declared = ((nj_u4)b[0] << 8) | (nj_u4)b[1];
    if (declared == 0u || declared > len - 2u) {
        return -1;
    }
    if ((declared % 3u) != 0u) {
        return -1;              /* not whole triples */
    }
    for (p = 2u; p + 2u < 2u + declared; p += 3u) {
        if (n >= cap) {
            return -1;          /* longer than we can hold: say so */
        }
        out[n].freq_hz = tone_freq((int)b[p]);
        /* One duration unit is 1/12800 s = 10000/128 microseconds. */
        out[n].duration_ms = (nj_u2)(((nj_u4)b[p + 2]) * 10u);
        n++;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* WAV                                                                 */
/* ------------------------------------------------------------------ */
typedef struct wav_info {
    const nj_u1 *data;
    nj_u4        frames;
    int          rate;
    int          channels;
    int          bits;
} wav_info;

/*
 * Parse RIFF/WAVE.  Only PCM (format 1) is accepted: a game shipping an ADPCM
 * or MP3-in-WAV file is rare enough that a clear MediaException beats an
 * almost-right noise.  Both chunk walks are bounded by the buffer, so a
 * truncated or hostile file cannot make this read past the end.
 */
static int wav_parse(const nj_u1 *b, nj_u4 len, wav_info *out)
{
#if !NJ_ENABLE_WAV
    /*
     * -DNJ_ENABLE_WAV=0: no RIFF reader in this build.  Returning 0 sends the
     * file down the same path as an unknown container, where the port's
     * audio_decode hook gets a chance at it and the refusal names WAV rather
     * than pretending the file is malformed.
     */
    (void)b; (void)len; (void)out;
    return 0;
#else
    nj_u4 p;
    int have_fmt = 0;
    int have_data = 0;
    nj_u4 data_off = 0u;
    nj_u4 data_len = 0u;
    int rate = 0;
    int chan = 0;
    int bits = 0;

    if (!b || len < 12u) {
        return 0;
    }
    if (!(b[0] == 'R' && b[1] == 'I' && b[2] == 'F' && b[3] == 'F')) {
        return 0;
    }
    if (!(b[8] == 'W' && b[9] == 'A' && b[10] == 'V' && b[11] == 'E')) {
        return 0;
    }
    p = 12u;
    while (p + 8u <= len) {
        nj_u4 id0 = b[p], id1 = b[p + 1], id2 = b[p + 2], id3 = b[p + 3];
        nj_u4 sz = ((nj_u4)b[p + 4]) | ((nj_u4)b[p + 5] << 8)
                 | ((nj_u4)b[p + 6] << 16) | ((nj_u4)b[p + 7] << 24);
        nj_u4 body = p + 8u;
        if (sz > len - body) {
            sz = len - body;    /* truncated chunk: take what is there */
        }
        if (id0 == 'f' && id1 == 'm' && id2 == 't' && id3 == ' ') {
            if (sz >= 16u) {
                int fmt = (int)(b[body] | (b[body + 1] << 8));
                chan = (int)(b[body + 2] | (b[body + 3] << 8));
                rate = (int)((nj_u4)b[body + 4] | ((nj_u4)b[body + 5] << 8)
                             | ((nj_u4)b[body + 6] << 16)
                             | ((nj_u4)b[body + 7] << 24));
                bits = (int)(b[body + 14] | (b[body + 15] << 8));
                if (fmt == 1 && (chan == 1 || chan == 2)
                    && (bits == 8 || bits == 16) && rate > 0) {
                    have_fmt = 1;
                }
            }
        } else if (id0 == 'd' && id1 == 'a' && id2 == 't' && id3 == 'a') {
            data_off = body;
            data_len = sz;
            have_data = 1;
        }
        /* Chunks are word-aligned; an odd size has a pad byte. */
        p = body + sz + (sz & 1u);
    }
    if (!have_fmt || !have_data || data_len == 0u) {
        return 0;
    }
    out->data = b + data_off;
    out->rate = rate;
    out->channels = chan;
    out->bits = bits;
    out->frames = data_len / (nj_u4)(chan * (bits / 8));
    return out->frames > 0u;
#endif /* NJ_ENABLE_WAV */
}

/*
 * Hand PCM to the port.  Returns the number of frames ACCEPTED, which is what
 * the caller stores; the port is allowed to take fewer than offered.
 */
/* Release the C stream a streamed MIDI Player holds, once. */
static void media_stream_release(nj_vm *vm, nj_ref player)
{
    nj_ref data;
    const nj_u1 *b;
#if NJ_ENABLE_MIDI
    struct nj_midi_stream *st;
#endif

    if (!r_z(vm, player, F_P_STREAM)) {
        return;
    }
    w_z(vm, player, F_P_STREAM, 0);
    data = r_r(vm, player, F_P_DATA, "[B");
    b = barray_bytes(data);
    if (b == NULL || barray_len(data) < NJ_MEDIA_STREAM_BYTES) {
        return;
    }
#if NJ_ENABLE_MIDI
    memcpy(&st, b, sizeof(st));
    if (st != NULL) {
        nj_midi_stream_close(st);
    }
#else
    (void)b;    /* no renderer: F_P_STREAM is never set, so this is dead */
#endif
}

/* The port's MIDI handle a platform MIDI Player holds, or NULL. */
static void *midi_handle(nj_vm *vm, nj_ref player)
{
    nj_ref data = r_r(vm, player, F_P_MIDI, "[B");
    const nj_u1 *b = barray_bytes(data);
    void *h = NULL;

    if (b == NULL || barray_len(data) < (nj_u4)sizeof(h)) {
        return NULL;
    }
    memcpy(&h, b, sizeof(h));
    return h;
}

/* Store one.  Returns 0 when the heap cannot take the byte array, in which
 * case the caller closes the handle and plays the file the long way. */
static int midi_handle_set(nj_vm *vm, nj_ref player, void *h)
{
    nj_ref holder = nj_byte_array_new(vm, (nj_u4)sizeof(h));

    if (holder == NULL) {
        return 0;
    }
    memcpy(holder->u.bytes, &h, sizeof(h));
    w_r(vm, player, F_P_MIDI, "[B", holder);
    return 1;
}

/* Release the port's handle a platform MIDI Player holds, once.  Safe on a
 * Player that never had one, which is why deallocate can call it blindly. */
static void media_midi_release(nj_vm *vm, nj_ref player)
{
    void *h;

    if (!r_z(vm, player, F_P_PLAT)) {
        return;
    }
    h = midi_handle(vm, player);
    if (h != NULL && vm->port && vm->port->midi_close) {
        vm->port->midi_close(vm->port->ud, h);
    }
    w_r(vm, player, F_P_MIDI, "[B", NULL);
    w_z(vm, player, F_P_PLAT, 0);
    w_z(vm, player, F_P_OPEN, 0);
}

static int pcm_push(nj_vm *vm, nj_ref player, int max_frames)
{
    nj_ref data = r_r(vm, player, F_P_DATA, "[B");
    const nj_u1 *b = barray_bytes(data);
    nj_u4 total = barray_len(data);
    nj_s4 done = r_i(vm, player, F_P_PUSHED);
    nj_s4 bits = 16;
    nj_s4 chan = r_i(vm, player, F_P_CHAN);
    nj_u4 off;
    nj_u4 avail;
    int frames;
    int took;

    if (!b || !vm->port || !vm->port->pcm_write || chan <= 0) {
        return 0;
    }
    /*
     * A streamed MIDI Player holds no PCM: the pump pulls what the device has
     * room for, renders it into the handle's scratch and hands that over.  The
     * song's length never reaches the Java heap, which is the whole point - a
     * fifty minute .mid is a hundred and forty megabytes of samples.
     *
     * With -DNJ_ENABLE_MIDI=0 there is no renderer to pull from, so this whole
     * branch is unreachable: F_P_STREAM is only ever set by createPlayer()
     * below, and that path is compiled out with the same macro.
     */
#if NJ_ENABLE_MIDI
    if (r_z(vm, player, F_P_STREAM)) {
        struct nj_midi_stream *st;
        nj_u1 *scratch;
        nj_u4 want;
        nj_u4 got;

        if (total < NJ_MEDIA_STREAM_BYTES) {
            return 0;
        }
        memcpy(&st, b, sizeof(st));
        if (st == NULL) {
            return 0;
        }
        scratch = (nj_u1 *)(void *)(b + sizeof(st));
        want = NJ_MEDIA_STREAM_FRAMES;
        if ((int)want > max_frames && max_frames > 0) {
            want = (nj_u4)max_frames;
        }
        got = nj_midi_stream_pull(st, scratch, want);
        if (got == 0u) {
            nj_midi_stream_close(st);
            w_z(vm, player, F_P_STREAM, 0);
            return 0;
        }
        /*
         * pcm_write() takes FRAMES, not samples: it multiplies by the channel
         * count itself.  Passing got * chan here made the port queue
         * chan times the audio it was handed - four times as much for a
         * stereo Player - and read past the scratch to fill the difference,
         * which is heard as a stutter that alternates between fragments of
         * the song.  The position advances by what the port ACCEPTED, as the
         * whole-buffer path below already did.
         */
        took = vm->port->pcm_write(vm->port->ud,
                                   (const nj_s2 *)(const void *)scratch,
                                   (int)got);
        if (took < 0) {
            return 0;
        }
        w_i(vm, player, F_P_PUSHED, done + (nj_s4)took);
        {
            nj_s8 us = (nj_s8)(done + (nj_s4)got) * 1000000LL
                       / (nj_s8)r_i(vm, player, F_P_RATE);
            w_j(vm, player, F_P_TIME, us);
        }
        return (int)got;
    }
#endif /* NJ_ENABLE_MIDI */
    off = (nj_u4)done * (nj_u4)chan * (nj_u4)(bits / 8);
    if (off >= total) {
        return 0;
    }
    avail = (total - off) / (nj_u4)(chan * (bits / 8));
    frames = (int)avail;
    if (frames > max_frames) {
        frames = max_frames;
    }
    if (frames <= 0) {
        return 0;
    }
    /*
     * The port wants signed 16-bit; the buffer already is, and nj_s2 is exactly
     * that, so this is a cast rather than a conversion.  It is not a strict
     * aliasing violation in practice because the buffer came from the Java heap
     * and is only ever read here, but it is stated rather than assumed.
     */
    took = vm->port->pcm_write(vm->port->ud, (const nj_s2 *)(const void *)(b + off),
                               frames);
    if (took < 0) {
        return 0;
    }
    w_i(vm, player, F_P_PUSHED, done + took);
    /* Media time advances with what has actually been played. */
    {
        nj_s8 us = (nj_s8)(done + took) * 1000000LL / (nj_s8)r_i(vm, player, F_P_RATE);
        w_j(vm, player, F_P_TIME, us);
        w_j(vm, player, F_P_DURATION,
            (nj_s8)((nj_u4)r_i(vm, player, F_P_FRAMES)) * 1000000LL
                / (nj_s8)r_i(vm, player, F_P_RATE));
    }
    return took;
}

/* ------------------------------------------------------------------ */
/* The playback registry                                               */
/* ------------------------------------------------------------------ */
/*
 * Which Players are still feeding the device, so nj_media_frame() can top them
 * up once per emulated frame.
 *
 * The list lives on the VM (nj_vm.media_players), not in this file, because two
 * VMs in one process must not be able to see each other's audio - the same rule
 * every other piece of runtime state follows.
 */
static int media_register(nj_vm *vm, nj_ref player)
{
    nj_ref v = vm->media_players;
    nj_value a[4];

    if (!v) {
        v = nj_object_new(vm, "java/util/Vector");
        if (!v) {
            nj_clear_exception(vm);
            return 0;
        }
        vm->media_players = v;
    }
    a[0].r = player;
    if (nj_call(vm, v, "java/util/Vector", "addElement",
                "(Ljava/lang/Object;)V", a, NULL) != NJ_OK) {
        nj_clear_exception(vm);
        return 0;
    }
    return 1;
}

static void media_unregister(nj_vm *vm, nj_ref player)
{
    nj_ref v = vm->media_players;
    nj_s4 n, i;

    if (!v) {
        return;
    }
    n = vec_size(vm, v);
    for (i = n - 1; i >= 0; i--) {
        if (vec_at(vm, v, i) == player) {
            nj_value a[4];
            a[0].r = player;
            (void)nj_call(vm, v, "java/util/Vector", "removeElement",
                          "(Ljava/lang/Object;)Z", a, NULL);
            break;              /* registered at most once per start() */
        }
    }
}

/*
 * Called once per emulated frame by nj_midp_frame().
 *
 * Pushing a whole WAV from inside start() works on a desktop and overruns the
 * DMA buffer on a part, so the Player keeps its position and this tops the
 * device up.  A port whose pcm_write() accepts everything finishes the file on
 * the first push and the entry comes straight back out again, so this costs one
 * empty walk per frame on a desktop.
 *
 * Walked BACKWARDS: a Player that finishes during the walk removes itself, and
 * iterating forwards would then skip the entry that shifted into its place.
 */

void nj_media_frame(nj_vm *vm)
{
    nj_ref v;
    nj_s4 n, i;

    if (!vm || !vm->media_players || !vm->port
        || (!vm->port->pcm_write && !vm->port->midi_finished)) {
        return;
    }
    v = vm->media_players;
    n = vec_size(vm, v);
    for (i = n - 1; i >= 0; i--) {
        nj_ref p = vec_at(vm, v, i);
        int room = 1 << 20;      /* a port with no pcm_free takes everything */

        if (!p) {
            continue;
        }
        /*
         * A Player the PORT is playing is not topped up - the synthesizer has
         * its own clock and its own buffer - so it is only asked whether it
         * has finished.  A finite loop count is counted here because the
         * platform can only loop forever or not at all.
         */
        if (r_z(vm, p, F_P_PLAT)) {
            void *mh = midi_handle(vm, p);
            if (mh != NULL && vm->port->midi_finished
                && vm->port->midi_finished(vm->port->ud, mh)) {
                nj_s4 loop = r_i(vm, p, F_P_LOOP);
                nj_s4 done = r_i(vm, p, F_P_LOOPDONE) + 1;
                if (loop > 1 && done < loop) {
                    w_i(vm, p, F_P_LOOPDONE, done);
                    if (vm->port->midi_seek) {
                        vm->port->midi_seek(vm->port->ud, mh, 0);
                    }
                    if (vm->port->midi_start) {
                        (void)vm->port->midi_start(vm->port->ud, mh, 0);
                    }
                } else {
                    w_z(vm, p, F_P_OPEN, 0);
                    if (r_i(vm, p, F_P_STATE) == 400) {
                        w_i(vm, p, F_P_STATE, 300);
                        fire(vm, p, "endOfMedia");
                    }
                    media_unregister(vm, p);
                }
            }
            continue;
        }
        if (vm->port->pcm_free) {
            room = vm->port->pcm_free(vm->port->ud);
            if (room <= 0) {
                continue;       /* device still full: try again next frame */
            }
        }
        (void)pcm_push(vm, p, room);
        if (r_i(vm, p, F_P_PUSHED) >= r_i(vm, p, F_P_FRAMES)) {
            /* Played to the end: STOP the Player and say so, which is what
             * MIDP's END_OF_MEDIA is for. */
            if (r_i(vm, p, F_P_KIND) == 1 && r_z(vm, p, F_P_OPEN)) {
                if (vm->port->pcm_close) {
                    vm->port->pcm_close(vm->port->ud);
                }
                w_z(vm, p, F_P_OPEN, 0);
            }
            if (r_i(vm, p, F_P_STATE) == 400) {
                w_i(vm, p, F_P_STATE, 300);
                fire(vm, p, "endOfMedia");
            }
            media_unregister(vm, p);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Natives: Manager                                                    */
/* ------------------------------------------------------------------ */
NJFN(media_playTone)
{
    nj_s4 note = NJ_ARG_INT(args, 0);
    nj_s4 dur = NJ_ARG_INT(args, 1);
    nj_s4 mode = NJ_ARG_INT(args, 2);
    nj_audio_tone t[1];
    nj_s8 total;

    if (note < 0 || dur < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative note or duration");
    }
    if (!vm->port || !vm->port->tone_play) {
        return NJ_OK;           /* a port without audio is not an error */
    }
    t[0].freq_hz = tone_freq(note);
    t[0].duration_ms = (nj_u2)(dur > 65535 ? 65535 : dur);
    (void)vm->port->tone_play(vm->port->ud, t, 1);
    total = dur;

    if ((mode & 2) != 0 && total > 0) {          /* TONE_BLOCKING */
        /*
         * Blocking is the CALLING JAVA THREAD, cooperatively: this returns
         * NJ_ERR_TIMEOUT and the interpreter suspends the thread, exactly as
         * Thread.sleep() does.  Sleeping the C thread here would freeze every
         * other thread in the VM, which for a game means the picture stops.
         */
        return nj_thread_sleep(vm, th, total);
    }
    return NJ_OK;
}

/*
 * Read a whole InputStream into a byte array.  The stream is the game's, so it
 * may be any subclass with any read() behaviour: this calls read([BII) through
 * the normal virtual dispatch and stops at -1 or at the cap.
 */
static nj_ref read_stream(nj_vm *vm, nj_ref in, nj_u4 cap)
{
    nj_u4 grow = 8192u;         /* start small and grow: see below */
    nj_ref buf;
    nj_u4 got = 0u;
    int guard = 0;
    nj_u4 mark;

    if (!in) {
        return NULL;
    }
    if (cap < grow) {
        grow = cap;
    }
    /* available() is only a sizing hint; a stream may legitimately return 0.
     * Resource streams already know their size. Avoid a chain of large copies
     * for music tracks, while still reading arbitrary streams through EOF. */
    {
        nj_value available;
        if (nj_call(vm, in, "java/io/InputStream", "available", "()I",
                    NULL, &available) == NJ_OK) {
            if (available.i > 0 && (nj_u4)available.i > grow) {
                grow = (nj_u4)available.i;
                /* Leave room for the EOF read without doubling the buffer. */
                if (grow < cap) grow++;
                if (grow > cap) grow = cap;
            }
        } else {
            nj_clear_exception(vm);
        }
    }
    buf = nj_byte_array_new(vm, grow);
    if (!buf) {
        nj_clear_exception(vm);
        return NULL;
    }
    /*
     * The buffer is held only in this C local, and both the growing copy below
     * and the reads in the loop allocate.  A collection in either place would
     * free the buffer out from under the bytes already read, so it is rooted
     * for the whole function (firefox/愤怒的小鸟-高清版_2353 crashed inside the
     * memcpy that copies a WAV out of it).
     */
    mark = nj_root_mark(vm);
    buf = nj_root(vm, buf);

    /*
     * Read in growing chunks rather than allocating the ceiling up front.
     *
     * The first version asked for a 4 MB array for every stream, which on a
     * 4 MB heap is the entire heap - and a MIDlet asking for a 20 KB WAV paid
     * for the lot.  Growing from 8 KB means a small file costs a small array,
     * and it is what a part with 512 KB of SRAM needs.
     */
    while (got < cap && guard++ < 1024) {
        nj_value a[4];
        nj_value r;
        nj_s4 n;
        if (got == grow) {
            nj_u4 want = grow * 2u;
            nj_ref nb;
            if (want > cap) {
                want = cap;
            }
            nb = nj_byte_array_new(vm, want);
            if (!nb) {
                nj_clear_exception(vm);
                break;          /* keep what we have */
            }
            memcpy(nb->u.bytes, buf->u.bytes, (size_t)got);
            nj_root_release(vm, mark);
            buf = nj_root(vm, nb);
            grow = want;
        }
        a[0].r = buf;
        a[1].i = (nj_s4)got;
        a[2].i = (nj_s4)(grow - got);
        r.i = 0;
        if (nj_call(vm, in, "java/io/InputStream", "read", "([BII)I",
                    a, &r) != NJ_OK) {
            nj_clear_exception(vm);
            break;
        }
        n = r.i;
        if (n <= 0) {
            break;              /* -1 is end of stream, 0 is "nothing yet" */
        }
        got += (nj_u4)n;
    }
    nj_root_release(vm, mark);
    if (got == 0u) {
        return NULL;
    }
    buf->length = got;          /* the array is exactly as long as the data */
    return buf;
}

/*
 * What is this file, really?
 *
 * "not a PCM WAV" used to be the only answer this code could give, and it gave
 * it for .mid files - which are not a kind of WAV at all.  Two corpus titles
 * (long-huo-2, xue-zhan-san-guo) ship MIDI and were told their WAV was
 * malformed, which sends anyone reading the log looking for the wrong bug.
 * Naming the container costs nothing.
 */
static const char *sniff_media(const nj_u1 *b, nj_u4 len)
{
    if (len >= 4u && b[0] == 'M' && b[1] == 'T' && b[2] == 'h' && b[3] == 'd') {
        return "MIDI";
    }
    if (len >= 4u && b[0] == 'R' && b[1] == 'I' && b[2] == 'F' && b[3] == 'F') {
#if NJ_ENABLE_WAV
        return "RIFF, but not a PCM WAV";
#else
        /* No RIFF reader in this build, so this is a WAV until the port's
         * audio_decode hook says otherwise: calling it malformed would send a
         * MIDlet looking for a bug in its own asset. */
        return "WAV";
#endif
    }
    /*
     * Wideband first: "#!AMR-WB\n" starts with "#!AMR" too, and the two are
     * not the same codec.  The narrowband decoder would read a wideband frame
     * header as narrowband and hand back noise, so the sniffer separates them
     * and the refusal says which one it is.  (The collection has no wideband
     * asset at all - 182 of them are "#!AMR\n" - which is why nothing here
     * decodes it.)
     */
    if (len >= 8u && b[0] == '#' && b[1] == '!' && b[2] == 'A' &&
        b[3] == 'M' && b[4] == 'R' && b[5] == '-' && b[6] == 'W' &&
        b[7] == 'B') {
        return "AMR-WB";
    }
    if (len >= 6u && b[0] == '#' && b[1] == '!' && b[2] == 'A' &&
        b[3] == 'M' && b[4] == 'R') {
        return "AMR";
    }
    if (len >= 3u && b[0] == 'I' && b[1] == 'D' && b[2] == '3') {
        return "MP3";
    }
    if (len >= 2u && b[0] == 0xFFu && (b[1] & 0xE0u) == 0xE0u) {
        return "MP3";
    }
    if (len >= 4u && b[0] == 0x00u && b[1] == 0x00u && b[2] == 0x00u &&
        (b[3] == 0x0Cu || b[3] == 0x18u || b[3] == 0x20u)) {
        return "MP4";
    }
    return "an unrecognised container";
}

/*
 * What to tell the MIDlet when there is no decoder for what it handed us.
 *
 * sniff_media() already knows the container, and the exception used to ignore
 * it: 8 corpus titles ship AMR effects and 5 ship MP3, and every one of them
 * was told its WAV was malformed.  A title that is told the truth about the
 * file it just read can fall back to something else; a title that is told
 * "not a PCM WAV" about an AMR file learns nothing.
 *
 * The strings are spelled out rather than formatted because this file has to
 * build without a libc, and because the set is small and closed.
 */
static const char *media_refusal(const char *what)
{
    if (strcmp(what, "AMR") == 0) {
#if NJ_ENABLE_AMR
        return "this AMR file holds no narrowband speech frame" NJ_MEXC_DATA;
#else
        return "this runtime has no AMR decoder" NJ_MEXC_DATA;
#endif
    }
    if (strcmp(what, "AMR-WB") == 0) {
        return "this runtime decodes AMR narrowband, not wideband" NJ_MEXC_DATA;
    }
    if (strcmp(what, "WAV") == 0) {
#if NJ_ENABLE_WAV
        return "this WAV file holds no PCM samples" NJ_MEXC_DATA;
#else
        return "this build has no WAV reader; the port must decode it" NJ_MEXC_DATA;
#endif
    }
    if (strcmp(what, "MP3") == 0) {
        return "this runtime has no MPEG audio decoder" NJ_MEXC_DATA;
    }
    if (strcmp(what, "MP4") == 0) {
        return "this runtime has no MP4 audio decoder" NJ_MEXC_DATA;
    }
    if (strcmp(what, "an unrecognised container") == 0) {
        return "this is not a container this runtime knows" NJ_MEXC_DATA;
    }
    return "not a PCM WAV" NJ_MEXC_DATA;
}

/* Build a Player from an already-parsed source. */
/*
 * The value Player.getContentType() reports.
 *
 * A caller that declared a type gets that exact string back - the MIDP javadoc
 * promises "the content type of this Player", and Manager.createPlayer(stream,
 * type) is where that type comes from.  A caller that declared nothing gets the
 * container the sniffer found, translated into its MIME spelling.  The set is
 * small and closed, and anything the sniffer cannot name is refused later
 * anyway, so the fallback is the WAV family this runtime advertises in
 * Manager.getSupportedContentTypes().
 */
static const char *mime_for_container(const char *what)
{
    if (!what) {
        return "audio/x-wav";
    }
    if (strcmp(what, "MIDI") == 0) {
        return "audio/midi";
    }
    if (strcmp(what, "AMR") == 0) {
        return "audio/amr";
    }
    if (strcmp(what, "AMR-WB") == 0) {
        return "audio/amr-wb";
    }
    if (strcmp(what, "MP3") == 0) {
        return "audio/mpeg";
    }
    if (strcmp(what, "MP4") == 0) {
        return "audio/mp4";
    }
    return "audio/x-wav";
}

/*
 * Store a MIME type on a Player.
 *
 * The type is metadata: if the string cannot be allocated the Player must still
 * play, so the exception is cleared and getContentType() answers with the
 * runtime's own default instead of handing the title a null.
 */
static void player_set_type(nj_vm *vm, nj_ref p, const char *mime)
{
    nj_ref s = nj_string_new(vm, mime);

    if (!s) {
        nj_clear_exception(vm);
        return;
    }
    w_r(vm, p, F_P_TYPE, "Ljava/lang/String;", s);
}

static nj_ref player_new(nj_vm *vm, int kind)
{
    nj_ref p = nj_object_new(vm, NJ_C_PLAYER);
    nj_ref lst;

    if (!p) {
        return NULL;
    }
    w_i(vm, p, F_P_STATE, 100);      /* UNREALIZED */
    w_i(vm, p, F_P_KIND, kind);
    w_i(vm, p, F_P_LOOP, 1);
    w_i(vm, p, F_P_VOLUME, 100);
    w_j(vm, p, F_P_DURATION, -1);
    w_j(vm, p, F_P_TIME, 0);
    lst = nj_object_new(vm, "java/util/Vector");
    if (lst) {
        w_r(vm, p, F_P_LISTEN, "Ljava/util/Vector;", lst);
    } else {
        nj_clear_exception(vm);
    }
    return p;
}

NJFN(media_createPlayer_stream)
{
    nj_ref in = NJ_ARG_REF(args, 0);
    nj_ref type = NJ_ARG_REF(args, 1);
    nj_ref data;
    nj_ref p;
    wav_info wi;
    nj_u4 mark;

    if (!in) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "stream is null");
    }
    mark = nj_root_mark(vm);
    /* Media competes with the game's objects in the configured Java heap.
     * A fixed 4 MB ceiling silently cut valid long WAV tracks short. */
    data = read_stream(vm, in, (nj_u4)(vm->heap.size > 0x7fffffffu
                                    ? 0x7fffffffu : vm->heap.size));
    if (!data) {
        nj_root_release(vm, mark);
        return nj_throw(vm, NJ_C_MEXC, "empty stream");
    }
    /*
     * read_stream hands back the only reference, and player_new allocates a
     * Player plus a Vector: a collection there used to free the file we are
     * about to parse.  愤怒的小鸟-高清版_2353 crashed in the memcpy below
     * (SIGSEGV inside libc, from this function) because the source block had
     * been recycled by the collector by then.
     */
    data = nj_root(vm, data);
    p = player_new(vm, 1);
    if (!p) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    /*
     * Root the Player as well, for the same reason `data` is rooted above -
     * and with the same title behind it, one step further along.
     *
     * A Player created here is reachable from nothing until this native
     * returns it: the C local is not a root and the caller's operand slot is
     * still empty.  Everything between player_new() and `ret->r = p` allocates
     * (player_new itself allocates the Vector for F_P_LISTEN, the WAV path
     * allocates the PCM array), and on a heap that is nearly full one of those
     * allocations collects - freeing the Player and handing the title a
     * reference to a recycled block.
     *
     * 愤怒的小鸟-高清版_2353 under --heap 32 did exactly that: the game stored
     * the dangling Player in a field, and the SIGSEGV arrived three frames
     * later inside `Player.realize()`, at the invokeinterface that dispatched
     * on the freed object's `cls` word - which the sweep had overwritten with
     * the free list's next pointer (NULL, so it read as "class == 0").
     */
    p = nj_root(vm, p);
    if (type) {
        w_r(vm, p, F_P_TYPE, "Ljava/lang/String;", type);
    } else {
        player_set_type(vm, p,
                        mime_for_container(sniff_media(barray_bytes(data),
                                                       barray_len(data))));
    }
    if (!wav_parse(barray_bytes(data), barray_len(data), &wi)) {
        const char *what = sniff_media(barray_bytes(data), barray_len(data));

        /*
         * MIDI is rendered to PCM here, once, and then plays through exactly
         * the same path as a WAV: after this branch a MIDI Player and a WAV
         * Player are the same object, which is what keeps the audio device
         * contract at one hook.
         *
         * 105 of the 238 titles in tests/games/ reach this branch within their
         * first 200 frames, and 107 of them ship no other audio at all - so
         * before nj_midi.c existed, most of the corpus was silent by design.
         *
         * When the file holds nothing to play - or the heap cannot take the
         * PCM - the silent Player below is still the answer, because a title
         * treats a player that cannot start as a bug and a player that makes
         * no sound as a quiet moment.
         */
        if (strcmp(what, "MIDI") == 0) {
            /*
             * A port with a synthesizer of its own gets the file whole.
             * Android's is Sonivox - the same General MIDI engine the phone's
             * own MIDI player uses, and the same one the popular Android J2ME
             * loaders vendor as a native library - and against the renderer
             * below it is the difference between a piano and a square wave.
             * So it is tried first, and the renderer stays as the answer for
             * every port that has none AND for the files a platform that has
             * one still refuses.
             */
            if (vm->port && vm->port->midi_open) {
                nj_u4 dur = 0;
                void *mh = vm->port->midi_open(vm->port->ud,
                                               barray_bytes(data),
                                               barray_len(data), &dur);
                if (mh != NULL && midi_handle_set(vm, p, mh)) {
                    w_z(vm, p, F_P_PLAT, 1);
                    w_z(vm, p, F_P_MIDIK, 1);
                    w_z(vm, p, F_P_STREAM, 0);
                    w_i(vm, p, F_P_KIND, 1);
                    /*
                     * Keep the song itself.  MIDIControl can arrive at any
                     * moment and a platform synth that cannot take single
                     * events has to be replaced by the renderer in
                     * midi_take_over(), which renders from these bytes - and
                     * the Player is the only place they can live.
                     */
                    w_r(vm, p, F_P_DATA, "[B", data);
                    /* The platform knows the real duration; -1 is what a
                     * Player with no duration reports, and a title that asks
                     * gets an honest "unknown" instead of a wrong number. */
                    w_j(vm, p, F_P_DURATION, dur > 0u ? (nj_s8)dur * 1000LL : -1);
                    nj_root_release(vm, mark);
                    ret->r = p;
                    return NJ_OK;
                }
                if (mh != NULL && vm->port->midi_close) {
                    vm->port->midi_close(vm->port->ud, mh);
                }
                if (nj_has_exception(vm)) {
                    nj_clear_exception(vm);     /* the renderer is the answer */
                }
            }
#if NJ_ENABLE_MIDI
            nj_midi_mem mem;
            struct nj_midi_stream *st;
            nj_u4 mframes;
            nj_u4 mchans;

            mem.ud = vm;
            mem.alloc = midi_alloc;
            mem.free = midi_free;
            st = nj_midi_stream_open(barray_bytes(data), barray_len(data), &mem);
            mframes = nj_midi_stream_frames(st);
            mchans = nj_midi_stream_channels(st);

            if (st != NULL && mframes > 0u && mchans > 0u) {
                nj_ref holder = nj_byte_array_new(vm, NJ_MEDIA_STREAM_BYTES);
                if (holder != NULL) {
                    struct nj_midi_stream *keep = st;
                    memcpy(holder->u.bytes, &keep, sizeof(keep));
                    w_r(vm, p, F_P_DATA, "[B", holder);
                    w_z(vm, p, F_P_STREAM, 1);
                    w_z(vm, p, F_P_MIDIK, 1);
                    w_i(vm, p, F_P_RATE, (nj_s4)NJ_MIDI_RATE);
                    w_i(vm, p, F_P_CHAN, (nj_s4)mchans);
                    w_i(vm, p, F_P_FRAMES, (nj_s4)mframes);
                    w_j(vm, p, F_P_DURATION,
                        (nj_s8)mframes * 1000000LL / (nj_s8)NJ_MIDI_RATE);
                    nj_root_release(vm, mark);
                    ret->r = p;
                    return NJ_OK;
                }
                /*
                 * The heap could not take even the handle.  A silent Player is
                 * the documented answer for a file that cannot be played (see
                 * the comment above), and the byte array's failure has already
                 * raised OutOfMemoryError - so drop that and stay silent
                 * rather than failing createPlayer(): 愤怒的小鸟-高清版_2353
                 * asks for its music while the heap is full, and a title
                 * treats a failed createPlayer() as a bug worth stopping for.
                 */
                if (nj_has_exception(vm)) {
                    nj_clear_exception(vm);
                }
                nj_midi_stream_close(st);
            }
            w_i(vm, p, F_P_KIND, 0);
            nj_logf(vm, 3, "media: silent Player for %u bytes of MIDI "
                           "(nothing to play)",
                    (unsigned)barray_len(data));
#else
            /*
             * -DNJ_ENABLE_MIDI=0: the renderer is not in this build, so a port
             * that has no synthesizer of its own (or that declined this file)
             * gets the silent Player instead.  Same answer as a file with
             * nothing to play, and the log says which of the two it was.
             */
            w_i(vm, p, F_P_KIND, 0);
            nj_logf(vm, 3, "media: silent Player for %u bytes of MIDI "
                           "(this build has no MIDI renderer)",
                    (unsigned)barray_len(data));
#endif /* NJ_ENABLE_MIDI */
            nj_root_release(vm, mark);
            ret->r = p;
            return NJ_OK;
        }
        /*
         * A port that has a decoder of its own gets the file before the
         * refusal.  The runtime owns the buffer it hands over, so the port
         * never has to allocate and never has to free.
         */
        if (vm->port && vm->port->audio_decode) {
            nj_u4 need = 0u, rate = 0u, chan = 0u;
            if (vm->port->audio_decode(vm->port->ud, barray_bytes(data),
                                       barray_len(data), NULL, 0u, &need,
                                       &rate, &chan) == NJ_OK &&
                need > 0u && rate > 0u && chan > 0u) {
                nj_ref pcm = nj_byte_array_new(vm, need);
                if (!pcm) {
                    nj_root_release(vm, mark);
                    return NJ_ERR;
                }
                if (vm->port->audio_decode(vm->port->ud, barray_bytes(data),
                                           barray_len(data), pcm->u.bytes, need,
                                           &need, &rate, &chan) == NJ_OK &&
                    need > 0u) {
                    nj_s4 frames = (nj_s4)(need / (2u * chan));
                    w_r(vm, p, F_P_DATA, "[B", pcm);
                    w_i(vm, p, F_P_RATE, (nj_s4)rate);
                    w_i(vm, p, F_P_CHAN, (nj_s4)chan);
                    w_i(vm, p, F_P_FRAMES, frames);
                    w_j(vm, p, F_P_DURATION,
                        (nj_s8)frames * 1000000LL / (nj_s8)rate);
                    nj_logf(vm, 3, "media: the port decoded %s into %u PCM "
                                   "bytes", what, (unsigned)need);
                    nj_root_release(vm, mark);
                    ret->r = p;
                    return NJ_OK;
                }
            }
        }
#if NJ_ENABLE_AMR
        /*
         * AMR narrowband: the runtime's own decoder, and the last thing tried
         * before the refusal.
         *
         * It sits AFTER the port hook and not before it because the hook is a
         * port saying "I have a codec for this" - a phone with AMR in firmware,
         * a desktop with libavcodec - and a codec that runs in hardware is a
         * better answer than 65 KB of tables and a float DSP.  Both the SDL2
         * port and the headless port have no decoder, so they land here; before
         * this branch existed they were refused, and The Sims 3 dereferenced
         * the null Player it got back, once per sound effect, for as long as it
         * ran (14 of its assets are AMR and not one is named ".amr").
         *
         * The frame count comes first because the decoder allocates nothing:
         * it writes into a buffer the caller sized, which here IS the Java byte
         * array, so the samples are never held twice.  Nothing between the
         * allocation and the decode allocates either, which is what keeps the
         * still-unrooted array safe from the collector.
         */
        if (strcmp(what, "AMR") == 0) {
            nj_u4 aframes = nj_amr_frames(barray_bytes(data), barray_len(data));
            nj_ref pcm = NULL;

            if (aframes > 0u) {
                pcm = nj_byte_array_new(vm,
                                        aframes * (nj_u4)NJ_AMR_BLOCK * 2u);
            }
            if (pcm != NULL &&
                nj_amr_decode(barray_bytes(data), barray_len(data),
                              pcm->u.bytes) == aframes) {
                w_r(vm, p, F_P_DATA, "[B", pcm);
                w_i(vm, p, F_P_RATE, (nj_s4)NJ_AMR_RATE);
                w_i(vm, p, F_P_CHAN, 1);
                w_i(vm, p, F_P_FRAMES, (nj_s4)(aframes * NJ_AMR_BLOCK));
                w_j(vm, p, F_P_DURATION,
                    (nj_s8)(aframes * NJ_AMR_BLOCK) * 1000000LL /
                    (nj_s8)NJ_AMR_RATE);
                nj_logf(vm, 3, "media: decoded %u AMR frames into %u PCM bytes",
                        (unsigned)aframes,
                        (unsigned)(aframes * (nj_u4)NJ_AMR_BLOCK * 2u));
                nj_root_release(vm, mark);
                ret->r = p;
                return NJ_OK;
            }
            /*
             * Nothing to play, or no room for the samples: a silent Player,
             * for the reason the MIDI branch above gives.  The byte array's
             * failure has already raised OutOfMemoryError, so drop that rather
             * than failing createPlayer().
             */
            if (nj_has_exception(vm)) {
                nj_clear_exception(vm);
            }
            w_i(vm, p, F_P_KIND, 0);
            nj_logf(vm, 3, "media: silent Player for %u bytes of AMR "
                           "(no narrowband frame)",
                    (unsigned)barray_len(data));
            nj_root_release(vm, mark);
            ret->r = p;
            return NJ_OK;
        }
#endif /* NJ_ENABLE_AMR */
        nj_logf(vm, 3, "media: cannot play %s", what);
        nj_root_release(vm, mark);
        return nj_throw(vm, NJ_C_MEXC, media_refusal(what));
    }
    /*
     * Keep JUST the data chunk.
     *
     * Storing the whole file and pushing from offset 0 plays the RIFF header as
     * audio: 8864 bytes at 16-bit mono is 4432 frames where the file holds
     * 4410, i.e. 22 frames of header noise, and the reported duration is wrong
     * by the same amount.  Slicing here means pcm_push() needs no idea what a
     * chunk is.
     */
    {
        nj_u4 off = (nj_u4)(wi.data - barray_bytes(data));
        nj_u4 want = wi.frames * (nj_u4)wi.channels * (nj_u4)(wi.bits / 8);

        /*
         * Widen 8-bit samples to the 16-bit the port takes.
         *
         * nj_port.pcm_write() takes nj_s2 samples and pcm_push() reads
         * F_P_DATA as 16-bit, so an 8-bit file pushed as it stands plays at
         * half speed as noise: every byte pair becomes one sample.  WAV's
         * 8-bit samples are UNSIGNED with 128 as silence, which is where the
         * -128 comes from.
         */
        nj_ref pcm;
        if (wi.bits == 8) {
            const nj_u1 *src;
            pcm = nj_byte_array_new(vm, want * 2u);
            if (!pcm) {
                nj_root_release(vm, mark);
                return NJ_ERR;
            }
            src = barray_bytes(data) + off;
            nj_u1 *dst = pcm->u.bytes;
            nj_u4 i;
            for (i = 0u; i < want; i++) {
                nj_s4 v = ((nj_s4)src[i] - 128) * 256;
                dst[i * 2u] = (nj_u1)(v & 0xFF);
                dst[i * 2u + 1u] = (nj_u1)((v >> 8) & 0xFF);
            }
        } else {
            /* read_stream owns this private copy, so 16-bit PCM can stay in
             * the same allocation. Strip the container with an overlapping
             * move and shorten the logical array to exclude trailing chunks.
             * A third full-size copy made Angry Birds HD's 13 MB music track
             * exhaust its 32 MB heap while the input stream was still live. */
            pcm = data;
            memmove(pcm->u.bytes, pcm->u.bytes + off, (size_t)want);
            pcm->length = want;
        }
        w_r(vm, p, F_P_DATA, "[B", pcm);
    }
    w_i(vm, p, F_P_RATE, wi.rate);
    w_i(vm, p, F_P_CHAN, wi.channels);
    w_i(vm, p, F_P_FRAMES, (nj_s4)wi.frames);
    w_j(vm, p, F_P_DURATION,
        (nj_s8)wi.frames * 1000000LL / (nj_s8)wi.rate);
    nj_root_release(vm, mark);
    ret->r = p;
    return NJ_OK;
}

NJFN(media_createPlayer_url)
{
    nj_ref url = NJ_ARG_REF(args, 0);
    const nj_u2 *c;
    int n;
    int i;

    if (!url) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", "url is null");
    }
    c = nj_string_chars(url);
    n = (int)nj_string_len(url);

    /*
     * A tone: URL is a real MIDP form - "tone://..." or just the sequence - and
     * it is what a game that ships a jingle as text uses.  Everything else is
     * refused BY NAME: silently returning a Player that plays nothing is the
     * failure mode that costs an afternoon.
     */
    if (n > 5 && c[0] == 't' && c[1] == 'o' && c[2] == 'n' && c[3] == 'e') {
        nj_u4 tmark = nj_root_mark(vm);
        nj_ref p = player_new(vm, 0);
        nj_ref seq;
        nj_u2 *tmp;
        int k;
        if (!p) {
            nj_root_release(vm, tmark);
            return NJ_ERR;
        }
        /*
         * Rooted, like the stream path: a Player is reachable from nothing
         * until this native returns it, and nj_byte_array_new() below can
         * collect on a heap that is nearly full.
         */
        p = nj_root(vm, p);
        player_set_type(vm, p, "audio/x-tone-seq");
        for (i = 5; i < n && c[i] != (nj_u2)'/'; i++) {
            /* skip the "://" or ":" */
        }
        while (i < n && c[i] == (nj_u2)'/') {
            i++;
        }
        /*
         * "tone://" with nothing after it is Manager.TONE_DEVICE_LOCATOR, and it
         * is the NORMAL way to get a tone Player: the sequence arrives later
         * through ToneControl.setToneSequence().  Treating the empty form as an
         * error - which this did at first - rejects the documented idiom and
         * accepts only the unusual literal-sequence form.
         */
        if (i >= n) {
            nj_root_release(vm, tmark);
            ret->r = p;             /* locator form: no sequence yet */
            return NJ_OK;
        }
        seq = nj_byte_array_new(vm, (nj_u4)(n - i));
        if (!seq) {
            nj_root_release(vm, tmark);
            return NJ_ERR;
        }
        tmp = (nj_u2 *)nj_string_chars(url);
        for (k = 0; k + i < n; k++) {
            seq->u.bytes[k] = (nj_u1)(tmp[k + i] & 0xffu);
        }
        w_r(vm, p, F_P_SEQ, "[B", seq);
        nj_root_release(vm, tmark);
        ret->r = p;
        return NJ_OK;
    }

    /* Everything else: name the protocol, do not pretend. */
    {
        char buf[160];
        int k = 0;
        const char *pfx = "unsupported protocol: ";
        while (*pfx && k < 140) {
            buf[k++] = *pfx++;
        }
        for (i = 0; i < n && k < 150; i++) {
            if (c[i] == (nj_u2)':') {
                break;
            }
            buf[k++] = (char)(c[i] & 0x7fu);
        }
        buf[k] = '\0';
        return nj_throw(vm, NJ_C_MEXC, buf);
    }
}

NJFN(media_supported_types)
{
    /* One element: "audio/x-wav", which is what this implementation decodes.
     *
     * Both objects are rooted while the other is built: a collection between
     * nj_array_new() and the store would otherwise recycle whichever of the
     * two is still only a C local, and the caller would hold a reference to a
     * freed block (the same failure the note in media_createPlayer_stream()
     * describes, one allocation later). */
    nj_u4 mark = nj_root_mark(vm);
    nj_ref s = nj_string_new(vm, "audio/x-wav");
    nj_ref a;
    if (!s) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    s = nj_root(vm, s);
    a = nj_array_new(vm, "java/lang/String", 1u);
    if (!a) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    a->u.refs[0] = s;
    nj_root_release(vm, mark);
    ret->r = a;
    return NJ_OK;
}

NJFN(media_supported_protocols)
{
    /* One element: "tone" - see media_supported_types() for the rooting. */
    nj_u4 mark = nj_root_mark(vm);
    nj_ref s = nj_string_new(vm, "tone");
    nj_ref a;
    if (!s) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    s = nj_root(vm, s);
    a = nj_array_new(vm, "java/lang/String", 1u);
    if (!a) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    a->u.refs[0] = s;
    nj_root_release(vm, mark);
    ret->r = a;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Natives: Player                                                     */
/* ------------------------------------------------------------------ */
NJFN(player_realize)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 st = r_i(vm, self, F_P_STATE);

    if (st == 0) {
        return nj_throw(vm, "java/lang/IllegalStateException", "closed");
    }
    if (st >= 200) {
        return NJ_OK;           /* already realized: MIDP allows the repeat */
    }
    /*
     * Real work, not a no-op: this is where a tone sequence becomes port tones,
     * so a sequence the port cannot express is refused HERE - at the point the
     * game asked for it - instead of at start() in the middle of a frame.
     */
    if (r_i(vm, self, F_P_KIND) == 0) {
        nj_ref seq = r_r(vm, self, F_P_SEQ, "[B");
        nj_audio_tone tmp[256];
        int n;
        /* No sequence yet is not an error: the game may realize() first and set
         * one through ToneControl afterwards, which is what the locator form is
         * for.  Such a Player simply plays nothing. */
        if (seq) {
            n = tone_parse(barray_bytes(seq), barray_len(seq), tmp, 256);
            if (n < 0) {
                return nj_throw(vm, NJ_C_MEXC,
                                "malformed tone sequence" NJ_MEXC_DATA);
            }
            {
                nj_ref arr = nj_array_new(vm, "I", (nj_u4)(n * 2));
                int i;
                if (!arr) {
                    return NJ_ERR;
                }
                for (i = 0; i < n; i++) {
                    ((nj_s4 *)arr->u.bytes)[i * 2] = (nj_s4)tmp[i].freq_hz;
                    ((nj_s4 *)arr->u.bytes)[i * 2 + 1] =
                        (nj_s4)tmp[i].duration_ms;
                }
                w_r(vm, self, F_P_TONES, "[I", arr);
                w_i(vm, self, F_P_NTONES, n);
            }
        }
    }
    w_i(vm, self, F_P_STATE, 200);
    return NJ_OK;
}

NJFN(player_prefetch)
{
    nj_ref self = NJ_THIS(args);

    if (r_i(vm, self, F_P_STATE) < 200) {
        nj_status st = NJ_CALL(player_realize, vm, th, args, ret);
        if (st != NJ_OK) {
            return st;
        }
    }
    w_i(vm, self, F_P_STATE, 300);
    return NJ_OK;
}

NJFN(player_start)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 st = r_i(vm, self, F_P_STATE);

    if (st == 0) {
        return nj_throw(vm, "java/lang/IllegalStateException", "closed");
    }
    if (st < 300) {
        nj_status r = NJ_CALL(player_prefetch, vm, th, args, ret);
        if (r != NJ_OK) {
            return r;
        }
    }
    if (st == 400) {
        return NJ_OK;
    }
    if (r_i(vm, self, F_P_KIND) == 0) {
        nj_ref tones = r_r(vm, self, F_P_TONES, "[I");
        int n = (int)r_i(vm, self, F_P_NTONES);
        if (tones && n > 0 && vm->port && vm->port->tone_play) {
            /*
             * The array is already in the port's layout - interleaved
             * freq/duration - so this is a cast, not a copy, and it is
             * documented here because it looks like a shortcut and is not.
             */
            (void)vm->port->tone_play(vm->port->ud,
                                      (const nj_audio_tone *)(const void *)
                                          tones->u.bytes,
                                      n);
        }
    } else if (r_z(vm, self, F_P_PLAT)) {
        void *mh = midi_handle(vm, self);
        nj_s4 loop = r_i(vm, self, F_P_LOOP);

        /*
         * A platform synthesizer loops forever or not at all, so only -1
         * ("until stopped") is handed to it as a loop and a finite count is
         * played once and counted here (F_P_LOOPDONE, in nj_media_frame).
         * The volume goes down with the start because a Player created at
         * volume 20 must not be heard at 100 for the first frame.
         */
        if (mh != NULL && vm->port && vm->port->midi_start
            && vm->port->midi_start(vm->port->ud, mh, loop == -1) == 0) {
            if (vm->port->midi_volume) {
                vm->port->midi_volume(vm->port->ud, mh,
                                      (int)r_i(vm, self, F_P_VOLUME));
            }
            w_i(vm, self, F_P_LOOPDONE, 0);
            w_z(vm, self, F_P_OPEN, 1);
            (void)media_register(vm, self);
        }
    } else if (vm->port && vm->port->pcm_open) {
        if (!r_z(vm, self, F_P_OPEN)) {
            if (vm->port->pcm_open(vm->port->ud, (int)r_i(vm, self, F_P_RATE),
                                   (int)r_i(vm, self, F_P_CHAN)) == 0) {
                w_z(vm, self, F_P_OPEN, 1);
            }
        }
        (void)pcm_push(vm, self, 1 << 20);
        /* On the registry only while there is more to play: a port that takes
         * the whole file at once is done here and never appears in a frame. */
        if (r_i(vm, self, F_P_PUSHED) < r_i(vm, self, F_P_FRAMES)) {
            (void)media_register(vm, self);
        }
    }
    w_i(vm, self, F_P_STATE, 400);
    fire(vm, self, "started");
    return NJ_OK;
}

NJFN(player_stop)
{
    nj_ref self = NJ_THIS(args);

    if (r_i(vm, self, F_P_STATE) == 400) {
        if (r_z(vm, self, F_P_PLAT)) {
            /* Stop, not close: the handle stays so start() can play it again,
             * which is what a game that pauses its music does. */
            void *mh = midi_handle(vm, self);
            if (mh != NULL && vm->port && vm->port->midi_stop) {
                vm->port->midi_stop(vm->port->ud, mh);
            }
            w_z(vm, self, F_P_OPEN, 0);
        } else if (r_i(vm, self, F_P_KIND) == 0) {
            if (vm->port && vm->port->tone_stop) {
                vm->port->tone_stop(vm->port->ud);
            }
        } else if (r_z(vm, self, F_P_OPEN) && vm->port
                   && vm->port->pcm_close) {
            vm->port->pcm_close(vm->port->ud);
            w_z(vm, self, F_P_OPEN, 0);
        }
        media_unregister(vm, self);
        w_i(vm, self, F_P_STATE, 300);
        fire(vm, self, "stopped");
    }
    return NJ_OK;
}

NJFN(player_deallocate)
{
    nj_ref self = NJ_THIS(args);
    nj_status r = NJ_CALL(player_stop, vm, th, args, ret);

    if (r != NJ_OK) {
        return r;
    }
    media_stream_release(vm, self);
    media_midi_release(vm, self);
    w_i(vm, self, F_P_STATE, 100);
    w_i(vm, self, F_P_PUSHED, 0);
    w_j(vm, self, F_P_TIME, 0);
    return NJ_OK;
}

NJFN(player_close)
{
    nj_ref self = NJ_THIS(args);
    nj_status r = NJ_CALL(player_deallocate, vm, th, args, ret);

    if (r != NJ_OK) {
        return r;
    }
    w_i(vm, self, F_P_STATE, 0);
    fire(vm, self, "closed");
    return NJ_OK;
}

NJFN(player_setLoopCount)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 n = NJ_ARG_INT(args, 0);
    if (n < -1) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "loop count must be >= -1");
    }
    w_i(vm, self, F_P_LOOP, n == 0 ? 1 : n);
    return NJ_OK;
}

NJFN(player_addListener)
{
    nj_ref self = NJ_THIS(args);
    nj_ref l = NJ_ARG_REF(args, 0);
    nj_ref v = r_r(vm, self, F_P_LISTEN, "Ljava/util/Vector;");

    if (!l) {
        return NJ_OK;
    }
    if (!v) {
        v = nj_object_new(vm, "java/util/Vector");
        if (!v) {
            return NJ_ERR;
        }
        w_r(vm, self, F_P_LISTEN, "Ljava/util/Vector;", v);
    }
    (void)vec_add(vm, v, l);
    return NJ_OK;
}

NJFN(player_removeListener)
{
    nj_ref self = NJ_THIS(args);
    nj_ref v = r_r(vm, self, F_P_LISTEN, "Ljava/util/Vector;");
    nj_ref l = NJ_ARG_REF(args, 0);
    nj_s4 n;

    if (!v || !l) {
        return NJ_OK;           /* MIDP: not there is not an error */
    }
    /*
     * Removes the FIRST occurrence, which is what Vector.removeElement does and
     * what MIDP specifies: a listener added twice is two registrations, and one
     * removal takes one away.  This was a no-op, so a game that removed its
     * listener kept being called - and a listener holding a destroyed screen
     * would then be invoked through it.
     */
    n = vec_size(vm, v);
    {
        nj_s4 i;
        for (i = 0; i < n; i++) {
            nj_ref e = vec_at(vm, v, i);
            if (e == l) {
                nj_value a[4];
                a[0].r = l;
                (void)nj_call(vm, v, "java/util/Vector", "removeElement",
                              "(Ljava/lang/Object;)Z", a, NULL);
                break;
            }
        }
    }
    return NJ_OK;
}

NJFN(player_getControl)
{
    nj_ref self = NJ_THIS(args);
    nj_ref want = NJ_ARG_REF(args, 0);
    const nj_u2 *c;
    int n;
    int i;
    const char *cls = NULL;

    if (!want) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "control type is null");
    }
    c = nj_string_chars(want);
    n = (int)nj_string_len(want);
    /*
     * Controllable.getControl takes either the fully qualified class name or
     * the bare one ("...if the package of the class is not given, the package
     * javax.microedition.media.control is assumed"), so both spellings have to
     * work.  Only the bare one used to: 开心农场_240 asks for
     * "javax.microedition.media.control.VolumeControl", got NULL back, and
     * died with NullPointerException at g.b(I)Z pc=65 - it checkcasts the
     * result (which succeeds on null) and calls setLevel on it.  A title
     * cannot work around that, so the last dot-separated component is what
     * gets compared here.  A name in somebody else's package is accepted too;
     * refusing it would only turn a working call into a null.
     */
    for (i = n - 1; i >= 0; i--) {
        if (c[i] == '.') {
            c += i + 1;
            n -= i + 1;
            break;
        }
    }
    if (n == 11 && c[0] == 'T' && c[1] == 'o' && c[2] == 'n' && c[3] == 'e') {
        /*
         * ToneControl only exists on a tone Player.  Returning one for a WAV
         * would let a game set a sequence that can never play and never find
         * out; NULL is what MIDP says to return for an unsupported control.
         */
        if (r_i(vm, self, F_P_KIND) == 0) {
            cls = NJ_C_TONE;
        }
    } else if (n == 13 && c[0] == 'V' && c[1] == 'o' && c[2] == 'l') {
        cls = NJ_C_VOLUME;
    } else if (n == 11 && c[0] == 'M' && c[1] == 'I' && c[2] == 'D'
               && c[3] == 'I') {
        /*
         * MIDIControl is only for a MIDI Player (F_P_KIND 1), for the same
         * reason ToneControl is only for a tone Player: a game that gets one
         * for a WAV would send it events that can never play.
         */
        if (r_z(vm, self, F_P_MIDIK)) {
            cls = NJ_C_MIDICTL;
        }
    }
    if (!cls) {
        ret->r = NULL;
        return NJ_OK;
    }
    {
        nj_ref ctl = nj_object_new(vm, cls);
        if (!ctl) {
            return NJ_ERR;
        }
        ctl_put(vm, ctl, cls, F_C_OWNER, "L" NJ_C_PLAYER ";", self);
        ret->r = ctl;
    }
    return NJ_OK;
}

NJFN(player_getControls)
{
    nj_ref self = NJ_THIS(args);
    nj_ref arr;
    nj_u4 n;
    nj_u4 at = 0u;
    nj_value a1[2];
    nj_value r;

    /*
     * The array holds exactly the controls this Player has: a tone Player has
     * two, a streamed Player has VolumeControl and - only when the source is a
     * MIDI song - MIDIControl.  Building it from getControl() keeps one answer
     * for both entry points, and asking the length first keeps a NULL out of
     * the array, which a game walking getControls() would dereference.
     */
    n = (r_i(vm, self, F_P_KIND) == 0) ? 2u
      : (r_z(vm, self, F_P_MIDIK) ? 2u : 1u);
    arr = nj_array_new(vm, "javax/microedition/media/Control", n);
    if (!arr) {
        return NJ_ERR;
    }
    if (r_i(vm, self, F_P_KIND) == 0) {
        a1[0].r = nj_string_new(vm, "ToneControl");
        r.r = NULL;
        if (nj_call(vm, self, NJ_C_PLAYER, "getControl",
                    "(Ljava/lang/String;)Ljavax/microedition/media/Control;",
                    a1, &r) == NJ_OK && r.r != NULL) {
            arr->u.refs[at++] = r.r;
        }
    }
    a1[0].r = nj_string_new(vm, "VolumeControl");
    r.r = NULL;
    if (nj_call(vm, self, NJ_C_PLAYER, "getControl",
                "(Ljava/lang/String;)Ljavax/microedition/media/Control;",
                a1, &r) == NJ_OK && r.r != NULL) {
        arr->u.refs[at++] = r.r;
    }
    if (r_z(vm, self, F_P_MIDIK)) {
        a1[0].r = nj_string_new(vm, "MIDIControl");
        r.r = NULL;
        if (nj_call(vm, self, NJ_C_PLAYER, "getControl",
                    "(Ljava/lang/String;)Ljavax/microedition/media/Control;",
                    a1, &r) == NJ_OK && r.r != NULL) {
            arr->u.refs[at++] = r.r;
        }
    }
    (void)at;
    ret->r = arr;
    return NJ_OK;
}

NJFN(player_getContentType)
{
    nj_ref self = NJ_THIS(args);
    nj_ref t = r_r(vm, self, F_P_TYPE, "Ljava/lang/String;");

    if (!t) {
        /* Every creation path fills this in; a NULL can only mean the metadata
         * allocation failed when the Player was built, in which case the honest
         * answer is the container this runtime can actually play. */
        t = nj_string_new(vm, "audio/x-wav");
        if (!t) {
            return nj_throw_oom(vm);
        }
        w_r(vm, self, F_P_TYPE, "Ljava/lang/String;", t);
    }
    ret->r = t;
    return NJ_OK;
}

NJFN(player_getState)
{
    ret->i = r_i(vm, NJ_THIS(args), F_P_STATE);
    return NJ_OK;
}

NJFN(player_getDuration)
{
    ret->j = r_j(vm, NJ_THIS(args), F_P_DURATION);
    return NJ_OK;
}

NJFN(player_getMediaTime)
{
    nj_ref self = NJ_THIS(args);

    /*
     * A platform MIDI Player's clock is the device's, not this runtime's: the
     * synthesizer has been playing for as long as it has been playing, and
     * the runtime only knows what it is told.  So the position is read back
     * rather than counted, which is also what makes it survive a slow frame.
     */
    if (r_z(vm, self, F_P_PLAT)) {
        void *mh = midi_handle(vm, self);
        if (mh != NULL && vm->port && vm->port->midi_time) {
            w_j(vm, self, F_P_TIME,
                (nj_s8)vm->port->midi_time(vm->port->ud, mh) * 1000LL);
        }
    }
    ret->j = r_j(vm, self, F_P_TIME);
    return NJ_OK;
}

/*
 * Player.setMediaTime(long) returns the time it settled on, not void: a title
 * seeking past the end reads the return value to find out where it really
 * landed.  Clamped to [0, duration] and to 0 when the duration is unknown
 * (-1), which is how a live stream reports itself.
 */
NJFN(player_setMediaTime)
{
    nj_ref self = NJ_THIS(args);
    nj_s8 want = NJ_ARG_LONG(args, 0);
    nj_s8 dur = r_j(vm, self, F_P_DURATION);

    if (want < 0) {
        want = 0;
    }
    if (dur > 0 && want > dur) {
        want = dur;
    }
    /*
     * A platform MIDI Player is told where to go; a streamed one is moved by
     * the same amount, because its renderer has to skip to the same place.  A
     * port with no midi_seek keeps its own clock and the position stands, so
     * setMediaTime() reports where it landed either way.
     */
    if (r_z(vm, self, F_P_PLAT)) {
        void *mh = midi_handle(vm, self);
        if (mh != NULL && vm->port && vm->port->midi_seek) {
            vm->port->midi_seek(vm->port->ud, mh, (nj_u4)(want / 1000));
        }
    } else if (r_z(vm, self, F_P_STREAM)) {
        /*
         * A streamed Player renders as it goes, so the position it reports has
         * to move the renderer too: a game that stops the theme, seeks to the
         * start and starts it again would otherwise hear the tune resume in
         * the middle.
         *
         * F_P_STREAM is only set by createPlayer(), which sets it under
         * NJ_ENABLE_MIDI - so a build with the renderer compiled out never
         * reaches this branch.
         */
#if NJ_ENABLE_MIDI
        nj_ref data = r_r(vm, self, F_P_DATA, "[B");
        if (data != NULL && barray_bytes(data) != NULL) {
            struct nj_midi_stream *st = NULL;
            nj_s4 rate = r_i(vm, self, F_P_RATE);
            memcpy(&st, barray_bytes(data), sizeof(st));
            if (st != NULL) {
                nj_u4 target = (nj_u4)((nj_u8)want
                                       * (nj_u8)(rate > 0 ? rate : 1)
                                       / 1000000u);
                nj_u4 got = nj_midi_stream_seek(st, target);
                w_i(vm, self, F_P_PUSHED, (nj_s4)got);
                if (rate > 0) {
                    want = (nj_s8)got * 1000000LL / (nj_s8)rate;
                }
            }
        }
#endif /* NJ_ENABLE_MIDI */
    }
    w_j(vm, self, F_P_TIME, want);
    ret->j = want;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Natives: controls                                                   */
/* ------------------------------------------------------------------ */
NJFN(tone_setSequence)
{
    nj_ref self = ctl_owner(vm, NJ_THIS(args));
    nj_ref b = NJ_ARG_REF(args, 0);
    nj_audio_tone tmp[256];
    int n;

    if (!b) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "null sequence");
    }
    n = tone_parse(barray_bytes(b), barray_len(b), tmp, 256);
    if (n < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "malformed tone sequence");
    }
    w_r(vm, self, F_P_SEQ, "[B", b);
    {
        nj_ref arr = nj_array_new(vm, "I", (nj_u4)(n * 2));
        int i;
        if (!arr) {
            return NJ_ERR;
        }
        for (i = 0; i < n; i++) {
            ((nj_s4 *)arr->u.bytes)[i * 2] = (nj_s4)tmp[i].freq_hz;
            ((nj_s4 *)arr->u.bytes)[i * 2 + 1] = (nj_s4)tmp[i].duration_ms;
        }
        w_r(vm, self, F_P_TONES, "[I", arr);
        w_i(vm, self, F_P_NTONES, n);
    }
    return NJ_OK;
}

NJFN(tone_getSequence)
{
    ret->r = r_r(vm, ctl_owner(vm, NJ_THIS(args)), F_P_SEQ, "[B");
    return NJ_OK;
}

NJFN(vol_setLevel)
{
    nj_ref self = ctl_owner(vm, NJ_THIS(args));
    nj_s4 lvl = NJ_ARG_INT(args, 0);
    if (lvl < 0 || lvl > 100) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "level out of range");
    }
    w_i(vm, self, F_P_VOLUME, lvl);
    /*
     * A platform MIDI Player's level is the device's mixer, so it has to be
     * told; the rendered paths scale their own samples and need nothing here.
     */
    if (r_z(vm, self, F_P_PLAT)) {
        void *mh = midi_handle(vm, self);
        if (mh != NULL && vm->port && vm->port->midi_volume) {
            vm->port->midi_volume(vm->port->ud, mh, (int)lvl);
        }
    }
    ret->i = lvl;               /* VolumeControl.setLevel returns the level set */
    return NJ_OK;
}

NJFN(vol_getLevel)
{
    ret->i = r_i(vm, ctl_owner(vm, NJ_THIS(args)), F_P_VOLUME);
    return NJ_OK;
}

NJFN(vol_setMute)
{
    nj_ref self = ctl_owner(vm, NJ_THIS(args));
    nj_s4 m = NJ_ARG_INT(args, 0) ? 1 : 0;
    nj_field *f = mfld(vm, NJ_C_PLAYER, F_P_MUTED, "Z");
    if (f) {
        nj_field_write_int(vm, self, f, m);
    }
    /* Muting is not silence-by-volume: the port is told to stop, so a muted
     * Player costs nothing on a part with no audio hardware either. */
    if (m && r_i(vm, self, F_P_STATE) == 400 && vm->port
        && vm->port->tone_stop) {
        vm->port->tone_stop(vm->port->ud);
    }
    return NJ_OK;
}

NJFN(vol_isMuted)
{
    nj_field *f = mfld(vm, NJ_C_PLAYER, F_P_MUTED, "Z");
    nj_ref p = ctl_owner(vm, NJ_THIS(args));
    ret->i = (f && p) ? nj_field_read_int(vm, p, f) : 0;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Natives: MIDIControl                                                */
/* ------------------------------------------------------------------ */
/*
 * MIDIControl is the one control that acts on a song while it plays, so its
 * calls have to reach whatever is rendering that song rather than the Player
 * object.  Which one that is depends on how the Player was created:
 *
 *   - the renderer in this library (a streamed MIDI Player): the events go
 *     straight into it;
 *   - a port's own synthesizer (a platform-backed Player): the port's
 *     midi_short_event()/midi_long_event() hooks get them first, because a
 *     port whose synth takes note events should keep the song;
 *   - a port that cannot take them - Android's MediaPlayer has no way to be
 *     sent a single note - hands the song back to this library's renderer,
 *     which picks it up at the position the platform had reached.  That is
 *     what makes these calls work at all, and it is why the renderer is kept
 *     in the Android build.
 *
 * With -DNJ_ENABLE_MIDI=0 there is nothing to hand the song to, so a
 * platform-backed Player ignores the events and the queries answer with the
 * General MIDI defaults.  The class is still there: a MIDlet that asks for it
 * links and runs instead of failing with a NoClassDefFoundError.
 */
#if NJ_ENABLE_MIDI
/* The renderer a Player owns, or NULL: a platform-backed Player has none. */
static struct nj_midi_stream *midi_stream_of(nj_vm *vm, nj_ref player)
{
    nj_ref data;
    struct nj_midi_stream *st = NULL;

    if (player == NULL || !r_z(vm, player, F_P_STREAM)) {
        return NULL;
    }
    data = r_r(vm, player, F_P_DATA, "[B");
    if (data == NULL || barray_bytes(data) == NULL
        || barray_len(data) < NJ_MEDIA_STREAM_BYTES) {
        return NULL;
    }
    memcpy(&st, barray_bytes(data), sizeof(st));
    return st;
}

/*
 * Stop the platform's player and continue the song in this library's
 * renderer, at the point the platform had reached.  Returns NULL when there is
 * nothing to take over (no file bytes, or no memory for the stream).
 */
static struct nj_midi_stream *midi_take_over(nj_vm *vm, nj_ref player)
{
    nj_ref data = r_r(vm, player, F_P_DATA, "[B");
    const nj_u1 *b = barray_bytes(data);
    nj_u4 len = barray_len(data);
    nj_midi_mem mem;
    struct nj_midi_stream *st;
    nj_ref holder;
    nj_s8 us;

    if (b == NULL || len == 0u) {
        return NULL;
    }
    mem.ud = vm;
    mem.alloc = midi_alloc;
    mem.free = midi_free;
    st = nj_midi_stream_open(b, len, &mem);
    if (st == NULL) {
        return NULL;
    }
    us = r_j(vm, player, F_P_TIME);
    if (us > 0) {
        nj_u4 frame = (nj_u4)((nj_u8)us * (nj_u8)NJ_MIDI_RATE / 1000000u);
        nj_midi_stream_seek(st, frame);
    }
    holder = nj_byte_array_new(vm, NJ_MEDIA_STREAM_BYTES);
    if (holder == NULL) {
        nj_midi_stream_close(st);
        nj_clear_exception(vm);     /* a silent answer, not a failed call */
        return NULL;
    }
    /*
     * Say it out loud: from here on this Player is rendered by the runtime,
     * not by the platform's synthesizer.  It is the one audible difference
     * MIDIControl makes on a port whose synth cannot take single events
     * (Android's MediaPlayer cannot), and a listener comparing the two would
     * otherwise have no way to tell why the instruments changed.
     */
    nj_logf(vm, 2, "media: the platform synth cannot take MIDI events; "
                   "the runtime took over this song at %u ms",
            (unsigned)(us > 0 ? us / 1000 : 0));
    {
        struct nj_midi_stream *keep = st;
        memcpy(holder->u.bytes, &keep, sizeof(keep));
    }
    w_r(vm, player, F_P_DATA, "[B", holder);
    w_z(vm, player, F_P_STREAM, 1);
    w_i(vm, player, F_P_RATE, (nj_s4)NJ_MIDI_RATE);
    w_i(vm, player, F_P_CHAN, (nj_s4)nj_midi_stream_channels(st));
    w_i(vm, player, F_P_FRAMES, (nj_s4)nj_midi_stream_frames(st));
    w_j(vm, player, F_P_DURATION,
        (nj_s8)nj_midi_stream_frames(st) * 1000000LL / (nj_s8)NJ_MIDI_RATE);
    if (r_z(vm, player, F_P_PLAT)) {
        void *h = midi_handle(vm, player);
        if (h != NULL && vm->port && vm->port->midi_close) {
            vm->port->midi_close(vm->port->ud, h);
        }
        midi_handle_set(vm, player, NULL);
        w_z(vm, player, F_P_PLAT, 0);
    }
    return st;
}
#endif /* NJ_ENABLE_MIDI */

/*
 * Hand one channel message to whatever is playing this Player's song.  Returns
 * 1 when it was taken, 0 when this build has no renderer to fall back on.
 */
static int midi_send_short(nj_vm *vm, nj_ref player, int status, int d1, int d2)
{
#if NJ_ENABLE_MIDI
    struct nj_midi_stream *st;

    if (player != NULL && r_z(vm, player, F_P_PLAT)) {
        void *h = midi_handle(vm, player);
        if (h != NULL && vm->port && vm->port->midi_short_event
            && vm->port->midi_short_event(vm->port->ud, h, status, d1, d2)
               == NJ_OK) {
            return 1;               /* the port's synth took it */
        }
        if (midi_take_over(vm, player) == NULL) {
            return 0;
        }
    }
    st = midi_stream_of(vm, player);
    if (st == NULL) {
        return 0;
    }
    nj_midi_stream_event(st, status, d1, d2);
    return 1;
#else
    (void)vm;
    (void)player;
    (void)status;
    (void)d1;
    (void)d2;
    return 0;                       /* no renderer in this build */
#endif
}

static int midi_send_long(nj_vm *vm, nj_ref player, const nj_u1 *data, nj_u4 len)
{
#if NJ_ENABLE_MIDI
    struct nj_midi_stream *st;

    if (player != NULL && r_z(vm, player, F_P_PLAT)) {
        void *h = midi_handle(vm, player);
        if (h != NULL && vm->port && vm->port->midi_long_event
            && vm->port->midi_long_event(vm->port->ud, h, data, len) == NJ_OK) {
            return 1;
        }
        if (midi_take_over(vm, player) == NULL) {
            return 0;
        }
    }
    st = midi_stream_of(vm, player);
    if (st == NULL) {
        return 0;
    }
    nj_midi_stream_events(st, data, len);
    return 1;
#else
    (void)vm;
    (void)player;
    (void)data;
    (void)len;
    return 0;
#endif
}

/* The channel's volume: the renderer's value, or the General MIDI default
 * while the platform is still playing the song. */
static int midi_channel_volume(nj_vm *vm, nj_ref player, int chan)
{
#if NJ_ENABLE_MIDI
    struct nj_midi_stream *st = midi_stream_of(vm, player);
    int v = 100;
    if (st != NULL && nj_midi_stream_channel_state(st, chan, NULL, NULL, &v)) {
        return v;
    }
#endif
    (void)vm;
    (void)player;
    (void)chan;
    return 100;
}

/* Append a decimal number, without libc. */
static char *midi_put_int(char *p, int v)
{
    char tmp[8];
    int n = 0;

    if (v < 0) {
        *p++ = '-';
        v = -v;
    }
    if (v == 0) {
        *p++ = '0';
        return p;
    }
    while (v > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (n > 0) {
        *p++ = tmp[--n];
    }
    return p;
}

/*
 * The runtime has no soundfont, so it has no per-program names either: the
 * name is the General MIDI family the timbre table is built around plus the
 * number inside it ("Piano 1"), which is a name a title can show and is at
 * least true of the sound it will get.
 */
static const char *const midi_family_names[16] = {
    "Piano", "Chromatic", "Organ", "Guitar", "Bass", "Strings", "Ensemble",
    "Brass", "Reed", "Pipe", "Synth Lead", "Synth Pad", "Synth Effects",
    "Ethnic", "Percussive", "Effects"
};

static const char *const midi_key_names[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

NJFN(midi_setChannelVolume)
{
    nj_ref self = ctl_owner(vm, NJ_THIS(args));
    nj_s4 chan = NJ_ARG_INT(args, 0);
    nj_s4 vol = NJ_ARG_INT(args, 1);

    if (chan < 0 || chan > 15) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "channel out of range");
    }
    if (vol < 0 || vol > 127) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "volume out of range");
    }
    midi_send_short(vm, self, 0xB0 | (int)chan, 7, (int)vol);
    return NJ_OK;
}

NJFN(midi_getChannelVolume)
{
    nj_ref self = ctl_owner(vm, NJ_THIS(args));
    nj_s4 chan = NJ_ARG_INT(args, 0);

    if (chan < 0 || chan > 15) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "channel out of range");
    }
    ret->i = midi_channel_volume(vm, self, (int)chan);
    return NJ_OK;
}

NJFN(midi_shortEvent)
{
    nj_ref self = ctl_owner(vm, NJ_THIS(args));
    nj_s4 type = NJ_ARG_INT(args, 0);
    nj_s4 d1 = NJ_ARG_INT(args, 1);
    nj_s4 d2 = NJ_ARG_INT(args, 2);

    if (type < 0 || type > 255) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "bad event type");
    }
    midi_send_short(vm, self, (int)type, (int)d1, (int)d2);
    return NJ_OK;
}

NJFN(midi_longEvent)
{
    nj_ref self = ctl_owner(vm, NJ_THIS(args));
    nj_ref data = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_u4 total;

    if (data == NULL) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "null data");
    }
    total = barray_len(data);
    if (off < 0 || len < 0 || (nj_u4)off > total
        || (nj_u4)len > total - (nj_u4)off) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "offset or length out of range");
    }
    /*
     * The return value is the number of bytes consumed, per JSR 135: a Player
     * that cannot take them returns -1.  Here the bytes are taken whenever
     * there is a renderer, and 0 is a zero length request.
     */
    if (midi_send_long(vm, self, barray_bytes(data) + (nj_u4)off,
                       (nj_u4)len)) {
        ret->i = len;
    } else {
        ret->i = -1;
    }
    return NJ_OK;
}

NJFN(midi_setProgram)
{
    nj_ref self = ctl_owner(vm, NJ_THIS(args));
    nj_s4 chan = NJ_ARG_INT(args, 0);
    nj_s4 bank = NJ_ARG_INT(args, 1);
    nj_s4 prog = NJ_ARG_INT(args, 2);

    if (chan < 0 || chan > 15) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "channel out of range");
    }
    if (bank < 0 || bank > 127 || prog < 0 || prog > 127) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "bank or program out of range");
    }
    if (bank != 0) {
        /* Bank select MSB first, then the program: the order a file uses. */
        midi_send_short(vm, self, 0xB0 | (int)chan, 0, (int)bank);
    }
    midi_send_short(vm, self, 0xC0 | (int)chan, (int)prog, 0);
    return NJ_OK;
}

/* getProgram returns {bank, program}; a title compiled against the other
 * reading of the same method calls the int form below. */
NJFN(midi_getProgram)
{
    nj_s4 chan = NJ_ARG_INT(args, 0);
    nj_ref arr;
    int bank = 0;
    int prog = 0;

    if (chan < 0 || chan > 15) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "channel out of range");
    }
#if NJ_ENABLE_MIDI
    {
        struct nj_midi_stream *st =
            midi_stream_of(vm, ctl_owner(vm, NJ_THIS(args)));
        if (st != NULL) {
            nj_midi_stream_channel_state(st, (int)chan, &bank, &prog, NULL);
        }
    }
#endif
    arr = nj_array_new(vm, "I", 2u);
    if (arr == NULL) {
        return NJ_ERR;
    }
    ((nj_s4 *)arr->u.bytes)[0] = (nj_s4)bank;
    ((nj_s4 *)arr->u.bytes)[1] = (nj_s4)prog;
    ret->r = arr;
    return NJ_OK;
}

NJFN(midi_getProgramInt)
{
    nj_s4 chan = NJ_ARG_INT(args, 0);
    int prog = 0;

    if (chan < 0 || chan > 15) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "channel out of range");
    }
#if NJ_ENABLE_MIDI
    {
        struct nj_midi_stream *st =
            midi_stream_of(vm, ctl_owner(vm, NJ_THIS(args)));
        if (st != NULL) {
            nj_midi_stream_channel_state(st, (int)chan, NULL, &prog, NULL);
        }
    }
#endif
    ret->i = prog;
    return NJ_OK;
}

/* Only the General MIDI bank exists here, so the list is bank 0. */
NJFN(midi_getBankList)
{
    nj_ref arr = nj_array_new(vm, "I", 1u);
    if (arr == NULL) {
        return NJ_ERR;
    }
    ((nj_s4 *)arr->u.bytes)[0] = 0;
    ret->r = arr;
    return NJ_OK;
}

NJFN(midi_getProgramList)
{
    nj_s4 bank = NJ_ARG_INT(args, 0);
    nj_ref arr = nj_array_new(vm, "I", (bank == 0) ? 128u : 0u);
    int i;

    if (arr == NULL) {
        return NJ_ERR;
    }
    for (i = 0; bank == 0 && i < 128; i++) {
        ((nj_s4 *)arr->u.bytes)[i] = (nj_s4)i;
    }
    ret->r = arr;
    return NJ_OK;
}

NJFN(midi_getProgramName)
{
    nj_s4 bank = NJ_ARG_INT(args, 0);
    nj_s4 prog = NJ_ARG_INT(args, 1);
    char buf[40];
    char *p = buf;
    const char *nm;

    if (bank < 0 || bank > 127 || prog < 0 || prog > 127) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "bank or program out of range");
    }
    nm = midi_family_names[(prog / 8) % 16];
    while (*nm != '\0' && p < buf + (int)sizeof(buf) - 4) {
        *p++ = *nm++;
    }
    *p++ = ' ';
    p = midi_put_int(p, (int)(prog % 8) + 1);
    *p = '\0';
    ret->r = nj_string_new(vm, buf);
    return ret->r ? NJ_OK : NJ_ERR;
}

NJFN(midi_getKeyName)
{
    nj_s4 key = NJ_ARG_INT(args, 2);
    char buf[8];
    char *p = buf;
    const char *nm;

    if (key < 0 || key > 127) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "key out of range");
    }
    nm = midi_key_names[key % 12];
    while (*nm != '\0') {
        *p++ = *nm++;
    }
    p = midi_put_int(p, (int)(key / 12) - 1);
    *p = '\0';
    ret->r = nj_string_new(vm, buf);
    return ret->r ? NJ_OK : NJ_ERR;
}

NJFN(midi_isBankQuerySupported)
{
    ret->i = 0;         /* bank 0 only: there is no second sound bank */
    return NJ_OK;
}


/* ------------------------------------------------------------------ */
/* Late init                                                           */
/* ------------------------------------------------------------------ */
/*
 * A builtin class has no ConstantValue attribute, so a static final String has
 * to be written the first time the VM is up - the same reason LCDUI has a late
 * init.  Only TONE_DEVICE_LOCATOR needs it; the int constants are read through
 * the field table and their values are in the code that uses them.
 */
nj_status nj_media_late_init(nj_vm *vm);
#define lcdui_const_str nj_media_const_str
static void nj_media_const_str(nj_vm *vm, const char *cls, const char *name,
                               const char *value)
{
    nj_field *f = nj_field_lookup(vm, cls, name, "Ljava/lang/String;", 1);
    nj_ref s;
    if (!f) {
        return;
    }
    s = nj_string_new(vm, value);
    if (s) {
        nj_field_write_ref(vm, NULL, f, s);
    } else {
        nj_clear_exception(vm);
    }
}

nj_status nj_media_late_init(nj_vm *vm)
{
    if (!vm) {
        return NJ_ERR;
    }
    nj_media_const_str(vm, NJ_C_MANAGER, "TONE_DEVICE_LOCATOR", "tone://");

    /*
     * MIDIControl's two constants.  176 is the MIDI "control change" status
     * nibble and 144 the "note on" one; a MIDlet that builds an event by hand
     * writes `MIDIControl.CONTROL_CHANGE | channel`, so these are not
     * decorative.
     */
    {
        static const struct { const char *n; nj_s4 v; } mc[] = {
            { "CONTROL_CHANGE", 176 },
            { "NOTE_ON", 144 }
        };
        unsigned mi;
        for (mi = 0u; mi < sizeof(mc) / sizeof(mc[0]); mi++) {
            nj_field *f = nj_field_lookup(vm, NJ_C_MIDICTL, mc[mi].n, "I", 1);
            if (f) {
                nj_field_write_int(vm, NULL, f, mc[mi].v);
            }
        }
    }

    /*
     * The Player STATE constants.  They were declared as fields and never
     * given values, so every one of them read 0 - which meant a MIDlet's
     * `p.getState() == Player.STARTED` compared against 0 and a
     * `p.getState() == Player.CLOSED` compared against 0 as well, i.e. the two
     * were indistinguishable.  The runtime's own state machine used the
     * numbers directly, so nothing failed until a MIDlet read the constant.
     */
    {
        static const struct { const char *n; nj_s4 v; } k[] = {
            { "CLOSED", 0 }, { "UNREALIZED", 100 }, { "REALIZED", 200 },
            { "PREFETCHED", 300 }, { "STARTED", 400 }
        };
        unsigned i;
        for (i = 0u; i < sizeof(k) / sizeof(k[0]); i++) {
            nj_field *f = nj_field_lookup(vm, NJ_C_PLAYER, k[i].n, "I", 1);
            if (f) {
                nj_field_write_int(vm, NULL, f, k[i].v);
            }
        }
        {
            nj_field *f = nj_field_lookup(vm, NJ_C_PLAYER, "TIME_UNKNOWN",
                                          "J", 1);
            if (f) {
                nj_field_write_long(vm, NULL, f, -1);
            }
        }
    }
    {
        /* The TONE_* modes live on the PLAYER here, which is where this
         * runtime reads them; the stubs declare them on Manager, so a MIDlet
         * writes Manager.TONE_STOP.  Both are given values, because a field
         * that reads 0 is a game taking the wrong branch quietly. */
        static const struct { const char *cls; const char *n; nj_s4 v; } k[] = {
            { NJ_C_MANAGER, "TONE_STOP", 1 },
            { NJ_C_MANAGER, "TONE_BLOCKING", 2 },
            { NJ_C_MANAGER, "TONE_CONSTANT_DURATION", 4 }
        };
        unsigned i;
        for (i = 0u; i < sizeof(k) / sizeof(k[0]); i++) {
            nj_field *f = nj_field_lookup(vm, k[i].cls, k[i].n, "I", 1);
            if (f) {
                nj_field_write_int(vm, NULL, f, k[i].v);
            }
        }
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }
#define FLD(name, desc, flags) { name, desc, flags }

static const nj_builtin_field player_fields[] = {
    FLD(F_P_STATE,    "I", 0),
    FLD(F_P_KIND,     "I", 0),
    FLD(F_P_TYPE,     "Ljava/lang/String;", 0),
    FLD(F_P_DURATION, "J", 0),
    FLD(F_P_TIME,     "J", 0),
    FLD(F_P_LOOP,     "I", 0),
    FLD(F_P_VOLUME,   "I", 0),
    FLD(F_P_MUTED,    "Z", 0),
    FLD(F_P_SEQ,      "[B", 0),
    FLD(F_P_TONES,    "[I", 0),
    FLD(F_P_NTONES,   "I", 0),
    FLD(F_P_DATA,     "[B", 0),
    FLD(F_P_STREAM,   "Z", 0),
    FLD(F_P_RATE,     "I", 0),
    FLD(F_P_CHAN,     "I", 0),
    FLD(F_P_FRAMES,   "I", 0),
    FLD(F_P_PUSHED,   "I", 0),
    FLD(F_P_OPEN,     "Z", 0),
    FLD(F_P_LISTEN,   "Ljava/util/Vector;", 0),
    FLD(F_P_MIDI,     "[B", 0),
    FLD(F_P_MIDIK,    "Z", 0),
    FLD(F_P_PLAT,     "Z", 0),
    FLD(F_P_LOOPDONE, "I", 0),
    /* MIDP state constants. */
    FLD("CLOSED",     "I", NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("UNREALIZED", "I", NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("REALIZED",   "I", NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("PREFETCHED", "I", NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("STARTED",    "I", NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("TIME_UNKNOWN", "J", NJ_ACC_STATIC | NJ_ACC_FINAL)
};

/* Manager's own constants.  Declaring them on Player - which is what a first
 * attempt did - leaves them missing from the class a MIDlet actually reads. */
static const nj_builtin_field manager_fields[] = {
    FLD("TONE_DEVICE_LOCATOR",    "Ljava/lang/String;",
        NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("TONE_STOP",              "I", NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("TONE_BLOCKING",          "I", NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("TONE_CONSTANT_DURATION", "I", NJ_ACC_STATIC | NJ_ACC_FINAL)
};

/* The methods javac will look for, exactly as the stubs declare them. */
static const nj_builtin_method player_methods[] = {
    M("realize",    "()V", 0),
    M("prefetch",   "()V", 0),
    M("start",      "()V", 0),
    M("stop",       "()V", 0),
    M("deallocate", "()V", 0),
    M("close",      "()V", 0),
    M("setLoopCount", "(I)V", 0),
    M("addPlayerListener",
      "(L" NJ_C_PLISTEN ";)V", 0),
    M("removePlayerListener",
      "(L" NJ_C_PLISTEN ";)V", 0),
    M("getState",    "()I", 0),
    M("getDuration", "()J", 0),
    M("getMediaTime", "()J", 0),
    M("setMediaTime", "(J)J", 0),
    M("getControl",  "(Ljava/lang/String;)Ljavax/microedition/media/Control;",
      0),
    M("getControls", "()[Ljavax/microedition/media/Control;", 0),
    M("getContentType", "()Ljava/lang/String;", 0)
};

static const nj_builtin_field ctl_fields[] = {
    FLD(F_C_OWNER, "L" NJ_C_PLAYER ";", 0)
};

/*
 * The two constants a MIDlet may read off a MIDIControl.  JSR 135 defines
 * them, the stubs declare them, and a field the runtime does not have is a
 * NoSuchFieldError naming a descriptor the MIDlet never wrote - which is
 * exactly what tests/gen_const_fixture.py exists to catch.  The values are
 * written in nj_media_late_init().
 */
static const nj_builtin_field midictl_fields[] = {
    FLD("CONTROL_CHANGE", "I", NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("NOTE_ON",        "I", NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method manager_methods[] = {
    M("createPlayer", "(Ljava/io/InputStream;Ljava/lang/String;)L"
      NJ_C_PLAYER ";", NJ_ACC_STATIC),
    M("createPlayer", "(Ljava/lang/String;)L" NJ_C_PLAYER ";", NJ_ACC_STATIC),
    M("getSupportedContentTypes",
      "(Ljava/lang/String;)[Ljava/lang/String;", NJ_ACC_STATIC),
    M("getSupportedProtocols",
      "(Ljava/lang/String;)[Ljava/lang/String;", NJ_ACC_STATIC),
    M("playTone", "(III)V", NJ_ACC_STATIC),
    M("TONE_DEVICE_LOCATOR", "Ljava/lang/String;",
      NJ_ACC_STATIC | NJ_ACC_FINAL),
    M("TONE_STOP", "I", NJ_ACC_STATIC | NJ_ACC_FINAL),
    M("TONE_BLOCKING", "I", NJ_ACC_STATIC | NJ_ACC_FINAL),
    M("TONE_CONSTANT_DURATION", "I", NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method listener_methods[] = {
    M("playerUpdate",
      "(L" NJ_C_PLAYER ";Ljava/lang/String;Ljava/lang/Object;)V",
      NJ_ACC_ABSTRACT)
};

/* Controllable is how a game reaches a control without knowing the concrete
 * type: it declares both methods, and the Player supplies them.  Titles call
 * it as `((Controllable) player).getControl("VolumeControl")` - and several
 * call the interface method straight on the Player, without a cast, so all
 * that is needed is for the interface and its methods to exist. */
static const nj_builtin_method controllable_methods[] = {
    M("getControl",  "(Ljava/lang/String;)L" NJ_C_CONTROL ";", NJ_ACC_ABSTRACT),
    M("getControls", "()[L" NJ_C_CONTROL ";", NJ_ACC_ABSTRACT)
};

static const nj_builtin_method toneseq_methods[] = {
    M("setToneSequence", "([B)V", 0),
    M("getToneSequence", "()[B", 0),
    M("setSequence",     "([B)V", 0)
};

static const nj_builtin_method volume_methods[] = {
    M("setLevel", "(I)I", 0),
    M("getLevel", "()I", 0),
    M("setMute",  "(Z)V", 0),
    M("isMuted",  "()Z", 0)
};

/*
 * MIDIControl.  getProgram is declared twice on purpose: JSR 135's reference
 * implementations disagree about whether it returns the program or {bank,
 * program}, and the runtime resolves a native by name AND descriptor, so
 * both readings link.  A title compiled against either one gets an answer.
 */
static const nj_builtin_method midictl_methods[] = {
    M("setChannelVolume",     "(II)V", 0),
    M("getChannelVolume",     "(I)I", 0),
    M("setProgram",           "(III)V", 0),
    M("getProgram",           "(I)[I", 0),
    M("getProgram",           "(I)I", 0),
    M("getBankList",          "(Z)[I", 0),
    M("getProgramList",       "(I)[I", 0),
    M("getProgramName",       "(II)Ljava/lang/String;", 0),
    M("getKeyName",           "(III)Ljava/lang/String;", 0),
    M("isBankQuerySupported", "()Z", 0),
    M("shortMidiEvent",       "(III)V", 0),
    M("longMidiEvent",        "([BII)I", 0)
};

#define NJ_MEDIA_EXC(name, super)                                             \
    { name, super, NJ_ACC_PUBLIC, NULL, 0u, NULL, 0u },

const nj_builtin_class nj_lib_media_classes[] = {
    /* JSR 135 declares Player as `interface Player extends Controllable', and
     * MIDlets reach every control through the cast `(Controllable) player'.
     * This runtime's Player is a class (its instances are the player objects),
     * and a built-in entry has one super slot, so the is-a relationship is
     * spelled as Player's super being Controllable.  Controllable in turn
     * names Object so that a Player is still an Object - the super chain is
     * what nj_class_is_subclass_of walks.  Without this, the cast threw
     * ClassCastException and the MIDlet never got as far as its first note. */
    { NJ_C_PLAYER, NJ_C_CONTROLLABLE, NJ_ACC_PUBLIC,
      player_fields,
      (nj_u2)(sizeof(player_fields) / sizeof(player_fields[0])),
      player_methods,
      (nj_u2)(sizeof(player_methods) / sizeof(player_methods[0])) },
    { NJ_C_MANAGER, "java/lang/Object", NJ_ACC_PUBLIC,
      manager_fields,
      (nj_u2)(sizeof(manager_fields) / sizeof(manager_fields[0])),
      manager_methods,
      (nj_u2)(sizeof(manager_methods) / sizeof(manager_methods[0])) },
    { NJ_C_PLISTEN, NULL, NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, listener_methods,
      (nj_u2)(sizeof(listener_methods) / sizeof(listener_methods[0])) },
    /* Control is a marker interface, and it has to exist even though it
     * declares nothing: Player.getControl's descriptor names it, and a game
     * that assigns the result to a Control local resolves the class. */
    { NJ_C_CONTROL, NULL, NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, NULL, 0u },
    { NJ_C_CONTROLLABLE, "java/lang/Object",
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, controllable_methods,
      (nj_u2)(sizeof(controllable_methods) / sizeof(controllable_methods[0])) },
    { NJ_C_TONE, "java/lang/Object", NJ_ACC_PUBLIC,
      ctl_fields,
      (nj_u2)(sizeof(ctl_fields) / sizeof(ctl_fields[0])), toneseq_methods,
      (nj_u2)(sizeof(toneseq_methods) / sizeof(toneseq_methods[0])) },
    { NJ_C_VOLUME, "java/lang/Object", NJ_ACC_PUBLIC,
      ctl_fields,
      (nj_u2)(sizeof(ctl_fields) / sizeof(ctl_fields[0])), volume_methods,
      (nj_u2)(sizeof(volume_methods) / sizeof(volume_methods[0])) },
    { NJ_C_MIDICTL, "java/lang/Object", NJ_ACC_PUBLIC,
      midictl_fields,
      (nj_u2)(sizeof(midictl_fields) / sizeof(midictl_fields[0])), midictl_methods,
      (nj_u2)(sizeof(midictl_methods) / sizeof(midictl_methods[0])) },
    NJ_MEDIA_EXC(NJ_C_MEXC, "java/lang/Exception")
    /* A sentinel keeps the array non-empty for tooling that assumes arrays
     * rather than counting on the length macro. */
    { NULL, NULL, 0u, NULL, 0u, NULL, 0u }
};
const int nj_lib_media_class_count =
    (int)(sizeof(nj_lib_media_classes) / sizeof(nj_lib_media_classes[0])) - 1;

const nj_native_entry nj_lib_media_table[] = {
    { NJ_C_MANAGER, "playTone", "(III)V", media_playTone },
    { NJ_C_MANAGER, "createPlayer",
      "(Ljava/io/InputStream;Ljava/lang/String;)L" NJ_C_PLAYER ";",
      media_createPlayer_stream },
    { NJ_C_MANAGER, "createPlayer", "(Ljava/lang/String;)L" NJ_C_PLAYER ";",
      media_createPlayer_url },
    { NJ_C_MANAGER, "getSupportedContentTypes",
      "(Ljava/lang/String;)[Ljava/lang/String;", media_supported_types },
    { NJ_C_MANAGER, "getSupportedProtocols",
      "(Ljava/lang/String;)[Ljava/lang/String;", media_supported_protocols },

    { NJ_C_PLAYER, "realize",    "()V", player_realize },
    { NJ_C_PLAYER, "prefetch",   "()V", player_prefetch },
    { NJ_C_PLAYER, "start",      "()V", player_start },
    { NJ_C_PLAYER, "stop",       "()V", player_stop },
    { NJ_C_PLAYER, "deallocate", "()V", player_deallocate },
    { NJ_C_PLAYER, "close",      "()V", player_close },
    { NJ_C_PLAYER, "setLoopCount", "(I)V", player_setLoopCount },
    { NJ_C_PLAYER, "addPlayerListener",
      "(L" NJ_C_PLISTEN ";)V", player_addListener },
    { NJ_C_PLAYER, "removePlayerListener",
      "(L" NJ_C_PLISTEN ";)V", player_removeListener },
    { NJ_C_PLAYER, "getState",    "()I", player_getState },
    { NJ_C_PLAYER, "getDuration", "()J", player_getDuration },
    { NJ_C_PLAYER, "getMediaTime", "()J", player_getMediaTime },
    { NJ_C_PLAYER, "setMediaTime", "(J)J", player_setMediaTime },
    { NJ_C_PLAYER, "getControl",
      "(Ljava/lang/String;)Ljavax/microedition/media/Control;",
      player_getControl },
    { NJ_C_PLAYER, "getContentType", "()Ljava/lang/String;", player_getContentType },
    { NJ_C_PLAYER, "getControls",
      "()[Ljavax/microedition/media/Control;", player_getControls },

    { NJ_C_TONE, "setToneSequence", "([B)V", tone_setSequence },
    { NJ_C_TONE, "getToneSequence", "()[B", tone_getSequence },
    { NJ_C_TONE, "setSequence",     "([B)V", tone_setSequence },

    { NJ_C_VOLUME, "setLevel", "(I)I", vol_setLevel },
    { NJ_C_VOLUME, "getLevel", "()I", vol_getLevel },
    { NJ_C_VOLUME, "setMute",  "(Z)V", vol_setMute },
    { NJ_C_VOLUME, "isMuted",  "()Z", vol_isMuted },
    { NJ_C_MIDICTL, "setChannelVolume",     "(II)V", midi_setChannelVolume },
    { NJ_C_MIDICTL, "getChannelVolume",     "(I)I",  midi_getChannelVolume },
    { NJ_C_MIDICTL, "setProgram",           "(III)V", midi_setProgram },
    { NJ_C_MIDICTL, "getProgram",           "(I)[I", midi_getProgram },
    { NJ_C_MIDICTL, "getProgram",           "(I)I",  midi_getProgramInt },
    { NJ_C_MIDICTL, "getBankList",          "(Z)[I", midi_getBankList },
    { NJ_C_MIDICTL, "getProgramList",       "(I)[I", midi_getProgramList },
    { NJ_C_MIDICTL, "getProgramName",       "(II)Ljava/lang/String;",
      midi_getProgramName },
    { NJ_C_MIDICTL, "getKeyName",           "(III)Ljava/lang/String;",
      midi_getKeyName },
    { NJ_C_MIDICTL, "isBankQuerySupported", "()Z",   midi_isBankQuerySupported },
    { NJ_C_MIDICTL, "shortMidiEvent",       "(III)V", midi_shortEvent },
    { NJ_C_MIDICTL, "longMidiEvent",        "([BII)I", midi_longEvent },

    { NULL, NULL, NULL, NULL }
};
const int nj_lib_media_count =
    (int)(sizeof(nj_lib_media_table) / sizeof(nj_lib_media_table[0])) - 1;

/* ------------------------------------------------------------------ */
/* The per-frame pump                                                  */
/* ------------------------------------------------------------------ */

