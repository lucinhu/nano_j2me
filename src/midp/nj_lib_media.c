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
 *   sampled PCM from a WAV, pushed to the port a frame at a time
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
 * pretending), no MIDI, no video, no audio capture, and no custom controls
 * beyond ToneControl and VolumeControl.  Realize/prefetch are state transitions
 * with real work behind them (the WAV is parsed at realize) rather than no-ops,
 * because a game that checks getState() must see the truth.
 */
#include "nj_midp.h"
#include "nj_midi.h"

#if NJ_USE_LIBC
#  include <string.h>
#  include <stdlib.h>
#endif

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_CALL(name, vm, th, args, ret) name((vm), (th), (args), (ret))
#define NJ_UNUSED(x) ((void)(x))

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

/* Player fields.  Everything the C side needs, on the object. */
#define F_P_STATE    "pState"      /* I */
#define F_P_KIND     "pKind"       /* I: 0 tone, 1 sampled */
#define F_P_DURATION "pDuration"   /* J microseconds */
#define F_P_TIME     "pMediaTime"  /* J microseconds */
#define F_P_LOOP     "pLoopCount"  /* I */
#define F_P_VOLUME   "pVolume"     /* I 0..100 */
#define F_P_MUTED    "pMuted"      /* Z */
#define F_P_SEQ      "pSeq"        /* [B the tone sequence as given */
#define F_P_TONES    "pTones"      /* [I freq,ms pairs */
#define F_P_NTONES   "pNTones"     /* I */
#define F_P_DATA     "pData"       /* [B PCM little-endian s16 */
#define F_P_RATE     "pRate"       /* I */
#define F_P_CHAN     "pChans"      /* I */
#define F_P_FRAMES   "pFrames"     /* I */
#define F_P_PUSHED   "pPushed"     /* I frames handed to the device */
#define F_P_OPEN     "pOpen"       /* Z device open for this player */
#define F_P_LISTEN   "pListen"     /* Ljava/util/Vector; */

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
}

/*
 * Hand PCM to the port.  Returns the number of frames ACCEPTED, which is what
 * the caller stores; the port is allowed to take fewer than offered.
 */
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

    if (!vm || !vm->media_players || !vm->port || !vm->port->pcm_write) {
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
    if (cap > grow) {
        cap = cap;              /* the caller's ceiling, applied as it grows */
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
        return "RIFF, but not a PCM WAV";
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

/* Build a Player from an already-parsed source. */
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

    (void)type;                 /* the content type is advisory: we sniff */
    if (!in) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "stream is null");
    }
    mark = nj_root_mark(vm);
    data = read_stream(vm, in, 4u * 1024u * 1024u);
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
            nj_u4 mframes = 0u;
            nj_u1 *rendered = nj_midi_render(barray_bytes(data),
                                             barray_len(data), &mframes);

            if (rendered != NULL && mframes > 0u) {
                nj_ref pcm = nj_byte_array_new(vm, mframes * 2u);
                if (pcm != NULL) {
                    memcpy(pcm->u.bytes, rendered, (size_t)mframes * 2u);
                    free(rendered);
                    w_r(vm, p, F_P_DATA, "[B", pcm);
                    w_i(vm, p, F_P_RATE, (nj_s4)NJ_MIDI_RATE);
                    w_i(vm, p, F_P_CHAN, 1);
                    w_i(vm, p, F_P_FRAMES, (nj_s4)mframes);
                    w_j(vm, p, F_P_DURATION,
                        (nj_s8)mframes * 1000000LL / (nj_s8)NJ_MIDI_RATE);
                    nj_root_release(vm, mark);
                    ret->r = p;
                    return NJ_OK;
                }
                free(rendered);
            }
            w_i(vm, p, F_P_KIND, 0);
            nj_logf(vm, 3, "media: silent Player for %u bytes of MIDI "
                           "(nothing to play)",
                    (unsigned)barray_len(data));
            nj_root_release(vm, mark);
            ret->r = p;
            return NJ_OK;
        }
        nj_logf(vm, 3, "media: cannot play %s", what);
        nj_root_release(vm, mark);
        return nj_throw(vm, NJ_C_MEXC,
                        "not a PCM WAV" NJ_MEXC_DATA);
    }
    /*
     * Copy out JUST the data chunk.
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
        nj_u4 wide = (wi.bits == 8) ? want * 2u : want;
        nj_ref pcm = nj_byte_array_new(vm, wide);
        if (!pcm) {
            nj_root_release(vm, mark);
            return NJ_ERR;
        }
        if (wi.bits == 8) {
            const nj_u1 *src = barray_bytes(data) + off;
            nj_u1 *dst = pcm->u.bytes;
            nj_u4 i;
            for (i = 0u; i < want; i++) {
                nj_s4 v = ((nj_s4)src[i] - 128) * 256;
                dst[i * 2u] = (nj_u1)(v & 0xFF);
                dst[i * 2u + 1u] = (nj_u1)((v >> 8) & 0xFF);
            }
        } else {
            memcpy(pcm->u.bytes, barray_bytes(data) + off, (size_t)want);
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
        nj_ref p = player_new(vm, 0);
        nj_ref seq;
        nj_u2 *tmp;
        int k;
        if (!p) {
            return NJ_ERR;
        }
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
            ret->r = p;             /* locator form: no sequence yet */
            return NJ_OK;
        }
        seq = nj_byte_array_new(vm, (nj_u4)(n - i));
        if (!seq) {
            return NJ_ERR;
        }
        tmp = (nj_u2 *)nj_string_chars(url);
        for (k = 0; k + i < n; k++) {
            seq->u.bytes[k] = (nj_u1)(tmp[k + i] & 0xffu);
        }
        w_r(vm, p, F_P_SEQ, "[B", seq);
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
    /* One element: "audio/x-wav", which is what this implementation decodes. */
    nj_ref a = nj_array_new(vm, "java/lang/String", 1u);
    if (!a) {
        return NJ_ERR;
    }
    a->u.refs[0] = nj_string_new(vm, "audio/x-wav");
    ret->r = a;
    return NJ_OK;
}

NJFN(media_supported_protocols)
{
    nj_ref a = nj_array_new(vm, "java/lang/String", 1u);
    if (!a) {
        return NJ_ERR;
    }
    a->u.refs[0] = nj_string_new(vm, "tone");
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
                nj_ref arr = nj_array_new(vm, "int", (nj_u4)(n * 2));
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
        if (r_i(vm, self, F_P_KIND) == 0) {
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
    const char *cls = NULL;

    if (!want) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "control type is null");
    }
    c = nj_string_chars(want);
    n = (int)nj_string_len(want);
    /* The argument is the unqualified interface name, per MIDP. */
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
    nj_ref arr = nj_array_new(vm, "javax/microedition/media/Control",
                              (r_i(vm, self, F_P_KIND) == 0) ? 2u : 1u);
    nj_value one;
    if (!arr) {
        return NJ_ERR;
    }
    one.r = self;
    if (r_i(vm, self, F_P_KIND) == 0) {
        nj_value a1[2];
        nj_value r;
        a1[0].r = nj_string_new(vm, "ToneControl");
        r.r = NULL;
        if (nj_call(vm, self, NJ_C_PLAYER, "getControl",
                    "(Ljava/lang/String;)Ljavax/microedition/media/Control;",
                    a1, &r) == NJ_OK) {
            arr->u.refs[0] = r.r;
        }
        a1[0].r = nj_string_new(vm, "VolumeControl");
        r.r = NULL;
        if (nj_call(vm, self, NJ_C_PLAYER, "getControl",
                    "(Ljava/lang/String;)Ljavax/microedition/media/Control;",
                    a1, &r) == NJ_OK) {
            arr->u.refs[1] = r.r;
        }
    } else {
        nj_value a1[2];
        nj_value r;
        a1[0].r = nj_string_new(vm, "VolumeControl");
        r.r = NULL;
        if (nj_call(vm, self, NJ_C_PLAYER, "getControl",
                    "(Ljava/lang/String;)Ljavax/microedition/media/Control;",
                    a1, &r) == NJ_OK) {
            arr->u.refs[0] = r.r;
        }
    }
    (void)one;
    ret->r = arr;
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
    ret->j = r_j(vm, NJ_THIS(args), F_P_TIME);
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
        nj_ref arr = nj_array_new(vm, "int", (nj_u4)(n * 2));
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
    FLD(F_P_DURATION, "J", 0),
    FLD(F_P_TIME,     "J", 0),
    FLD(F_P_LOOP,     "I", 0),
    FLD(F_P_VOLUME,   "I", 0),
    FLD(F_P_MUTED,    "Z", 0),
    FLD(F_P_SEQ,      "[B", 0),
    FLD(F_P_TONES,    "[I", 0),
    FLD(F_P_NTONES,   "I", 0),
    FLD(F_P_DATA,     "[B", 0),
    FLD(F_P_RATE,     "I", 0),
    FLD(F_P_CHAN,     "I", 0),
    FLD(F_P_FRAMES,   "I", 0),
    FLD(F_P_PUSHED,   "I", 0),
    FLD(F_P_OPEN,     "Z", 0),
    FLD(F_P_LISTEN,   "Ljava/util/Vector;", 0),
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
    M("getControls", "()[Ljavax/microedition/media/Control;", 0)
};

static const nj_builtin_field ctl_fields[] = {
    FLD(F_C_OWNER, "L" NJ_C_PLAYER ";", 0)
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

#define NJ_MEDIA_EXC(name, super)                                             \
    { name, super, NJ_ACC_PUBLIC, NULL, 0u, NULL, 0u },

const nj_builtin_class nj_lib_media_classes[] = {
    { NJ_C_PLAYER, "java/lang/Object", NJ_ACC_PUBLIC,
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
    { NJ_C_CONTROLLABLE, NULL,
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
    { NJ_C_PLAYER, "getControls",
      "()[Ljavax/microedition/media/Control;", player_getControls },

    { NJ_C_TONE, "setToneSequence", "([B)V", tone_setSequence },
    { NJ_C_TONE, "getToneSequence", "()[B", tone_getSequence },
    { NJ_C_TONE, "setSequence",     "([B)V", tone_setSequence },

    { NJ_C_VOLUME, "setLevel", "(I)I", vol_setLevel },
    { NJ_C_VOLUME, "getLevel", "()I", vol_getLevel },
    { NJ_C_VOLUME, "setMute",  "(Z)V", vol_setMute },
    { NJ_C_VOLUME, "isMuted",  "()Z", vol_isMuted },

    { NULL, NULL, NULL, NULL }
};
const int nj_lib_media_count =
    (int)(sizeof(nj_lib_media_table) / sizeof(nj_lib_media_table[0])) - 1;

/* ------------------------------------------------------------------ */
/* The per-frame pump                                                  */
/* ------------------------------------------------------------------ */

