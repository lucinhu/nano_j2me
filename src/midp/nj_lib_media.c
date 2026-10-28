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
#if NJ_ENABLE_VIDEO
#include "nj_mp4.h"
#endif
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

#if NJ_ENABLE_MP3 || NJ_ENABLE_AMR || NJ_ENABLE_VIDEO
/*
 * Every provider-backed path allocates through the VM, and none of them may
 * lean on the MIDI renderer's adapters: -DNJ_ENABLE_MIDI=0 compiles those out.
 * The trim matrix caught this once for MP3; the guard covers AMR and video too
 * so each switch can be turned off on its own.
 */
static void *av_alloc(void *ud, nj_size n)
{
    return nj_host_alloc((nj_vm *)ud, n);
}

static void av_free(void *ud, void *p)
{
    nj_host_free((nj_vm *)ud, p);
}

/*
 * Providers borrow the nj_av_mem they are opened with: the H.263 and MPEG-4
 * decoders keep the pointer and release their frame buffers through it in
 * close().  A createPlayer() stack frame is therefore the wrong home for the
 * hooks - the pump or close() runs later, after that frame is gone, and the
 * borrowed pointer is a use-after-return.  Each video decoder gets one
 * host-allocated copy, owned by the Player's holder until close().
 */
static nj_av_mem *av_mem_new(nj_vm *vm)
{
    nj_av_mem *mem = (nj_av_mem *)nj_host_alloc(vm, (nj_size)sizeof(*mem));

    if (mem != NULL) {
        mem->ud = vm;
        mem->alloc = av_alloc;
        mem->free = av_free;
    }
    return mem;
}
#endif

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */
#define NJ_C_PLAYER   "javax/microedition/media/Player"
#define NJ_C_MANAGER  "javax/microedition/media/Manager"
#define NJ_C_PLISTEN  "javax/microedition/media/PlayerListener"
#define NJ_C_MEXC     "javax/microedition/media/MediaException"
#define NJ_C_CONTROL  "javax/microedition/media/Control"
#define NJ_C_CONTROLLABLE "javax/microedition/media/Controllable"
#define NJ_C_TIMEBASE    "javax/microedition/media/TimeBase"
#define NJ_C_TONE     "javax/microedition/media/control/ToneControl"
#define NJ_C_VOLUME   "javax/microedition/media/control/VolumeControl"
#define NJ_C_MIDICTL  "javax/microedition/media/control/MIDIControl"
#define NJ_C_VIDEOC   "javax/microedition/media/control/VideoControl"

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
#define F_P_DEVROOM  "pDevRoom"    /* I free frames when the device was opened */
#define F_P_DATA     "pData"       /* [B PCM little-endian s16 */
#define F_P_STREAM   "pStream"     /* Z: pData is a stream handle, not PCM */

/*
 * A MIDI Player holds its music in C, not in the Java heap: pData is a handle
 * (the pointer in the first bytes) plus the scratch the pump pulls into.  That
 * is what lets a fifty minute song play on a heap that could never hold its
 * PCM - the whole file would be a hundred and forty megabytes.
 */
#define NJ_MEDIA_STREAM_FRAMES 4096u
typedef struct media_stream_state {
    struct nj_midi_stream *stream;  /* first, shared with release/control paths */
    nj_u4 pending;                 /* rendered frames not accepted by the port */
    nj_u4 offset;                  /* accepted prefix in the scratch buffer */
#if NJ_ENABLE_MP3
    const struct nj_av_audio_ops *aops; /* codec that renders this stream     */
    void *adec;                         /* its decoder, from aops->open()     */
    nj_u4 in_off;                       /* next unread byte of the source     */
#endif
} media_stream_state;

#if NJ_ENABLE_VIDEO
/* What a video Player keeps in its holder: the sample iterators and decoders. */
typedef struct media_video_state {
    nj_mp4 m;                           /* video-track container position     */
    nj_av_dec *vdec;                    /* provider decoder                   */
    const nj_av_video_ops *vops;        /* the provider it came from          */
    nj_av_mem *mem;                     /* hooks the provider borrows, owned  */
                                        /* by this holder until close()       */
#if NJ_ENABLE_AMR
    nj_mp4 am;                          /* the movie's AMR-NB sound track     */
    int    has_audio;                   /* am holds a usable samr track       */
#endif
} media_video_state;
#define NJ_MEDIA_VIDEO_BYTES ((nj_u4)sizeof(media_video_state))
#endif

#define NJ_MEDIA_STREAM_BYTES  ((nj_u4)sizeof(media_stream_state) + NJ_MEDIA_STREAM_FRAMES * 2u * 2u)
#define F_P_RATE     "pRate"       /* I */
#define F_P_CHAN     "pChans"      /* I */
#define F_P_FRAMES   "pFrames"     /* I */
#define F_P_PUSHED   "pPushed"     /* I frames handed to the device */
#define F_P_SRC      "pSrc"        /* [B the file a streamed codec reads  */
#define F_P_AVK      "pAvK"        /* Z a nj_avcodec renders this stream  */
#define F_P_VHOLD    "pVid"        /* [B demuxer + video decoder for a movie */
#define F_P_VBUF     "pVbuf"       /* [B RGB565 frame the decoder writes     */
#define F_P_VW       "pVw"         /* I frame width                          */
#define F_P_VH       "pVh"         /* I frame height                         */
#define F_P_OPEN     "pOpen"       /* Z device open for this player */
/*
 * Direct-video display state, written by VideoControl and read by the
 * frame-boundary overlay.  It lives on the Player because there is exactly one
 * display per Player and the overlay has to read it without allocating a
 * control object; the control itself is a thin handle like VolumeControl.
 */
#define F_P_VDMODE   "pVMode"      /* I 1 = USE_DIRECT_VIDEO, 0 = not given */
#define F_P_VCANVAS  "pVCanvas"    /* Ljavax/microedition/lcdui/Canvas; target */
#define F_P_VDX      "pVx"         /* I display location */
#define F_P_VDY      "pVy"         /* I */
#define F_P_VDW      "pVdw"        /* I display size; 0 = source size */
#define F_P_VDH      "pVdh"        /* I */
#define F_P_VFULL    "pVfull"      /* Z draw full screen */
#define F_P_VVIS     "pVvis"       /* Z the control asked for the video to show */
#define F_P_VSTART   "pVstart"     /* J wall-clock ms that sample 0 is due    */
#define F_P_APCM    "pApcm"        /* [B the movie's AMR track decoded to PCM */
#define F_P_APCMN   "pApcmn"       /* I PCM frames in pApcm                   */
#define F_P_APUSH   "pApush"       /* I PCM frames already handed to the port */
#define F_P_LISTEN   "pListen"     /* Ljava/util/Vector; */
#define F_P_MASTER   "pTimeBase"   /* Ljavax/microedition/media/TimeBase; master clock or null */

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
            /* A restored Player must not keep a Canvas reference from the
               saved arena: the display state is rebuilt by the title, and the
               overlay compares against the live current displayable. */
            w_r(vm, player, F_P_VCANVAS,
                "Ljavax/microedition/lcdui/Canvas;", NULL);
            w_i(vm, player, F_P_VDMODE, 0);
            w_z(vm, player, F_P_VVIS, 0);
            w_z(vm, player, F_P_VFULL, 0);
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
    nj_u4        bytes;      /* bytes of sample data in the chunk */
    nj_u4        frames;
    int          rate;
    int          channels;
    int          bits;
    int          adpcm;      /* 1: Microsoft IMA ADPCM (WAVE format 0x11) */
    int          block;      /* ADPCM block alignment in bytes */
} wav_info;

/*
 * Microsoft IMA ADPCM (WAVE format 0x11).
 *
 * 文明5's whole soundtrack arrives in this format: 8 kHz mono, 4-bit samples,
 * 256-byte blocks.  The PCM-only reader below refused every one of them with
 * "this WAV file holds no PCM samples", and the game's paint loop printed the
 * MediaException every frame while it tried to start its sound.  A block is a
 * 4-byte header per channel - one 16-bit sample and the step index - followed
 * by nibbles; the first sample in a block is stored verbatim, so each block
 * decodes to 2*(block - 4*channels)/channels + 1 frames.
 */
static const int ima_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};
static const int ima_step_table[89] = {
        7,     8,     9,    10,    11,    12,    13,    14,
       16,    17,    19,    21,    23,    25,    28,    31,
       34,    37,    41,    45,    50,    55,    60,    66,
       73,    80,    88,    97,   107,   118,   130,   143,
      157,   173,   190,   209,   230,   253,   279,   307,
      337,   371,   408,   449,   494,   544,   598,   658,
      724,   796,   876,   963,  1060,  1166,  1282,  1411,
     1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
     3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
     7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767
};

static int ima_decode(int nibble, int *step_index, int *predictor)
{
    int step = ima_step_table[*step_index];
    int diff = step >> 3;
    int pred = *predictor;

    if (nibble & 1) {
        diff += step >> 2;
    }
    if (nibble & 2) {
        diff += step >> 1;
    }
    if (nibble & 4) {
        diff += step;
    }
    if (nibble & 8) {
        pred -= diff;
    } else {
        pred += diff;
    }
    if (pred > 32767) {
        pred = 32767;
    } else if (pred < -32768) {
        pred = -32768;
    }
    *step_index += ima_index_table[nibble];
    if (*step_index < 0) {
        *step_index = 0;
    } else if (*step_index > 88) {
        *step_index = 88;
    }
    *predictor = pred;
    return pred;
}

/* Decode whole IMA blocks into little-endian s16.  Returns frames written. */
static nj_u4 wav_ima_decode(const nj_u1 *src, nj_u4 len, int channels,
                            int block, nj_u1 *dst, nj_u4 dst_cap)
{
    nj_u4 frames = 0u;
    nj_u4 pos = 0u;
    int samples_per_block;
    nj_u1 *out = dst;

    if (channels < 1 || channels > 2 || block < 4 * channels + 4) {
        return 0u;
    }
    samples_per_block = 2 * (block - 4 * channels) / channels + 1;
    while (pos + (nj_u4)block <= len) {
        int pred[2];
        int idx[2];
        int c;
        int s;
        const nj_u1 *p = src + pos;

        for (c = 0; c < channels; c++) {
            const nj_u1 *h = p + 4 * c;
            pred[c] = (int)(nj_s2)((nj_u2)h[0] | ((nj_u2)h[1] << 8));
            idx[c] = (int)h[2];
            if (idx[c] > 88) {
                idx[c] = 88;
            }
        }
        for (c = 0; c < channels; c++) {
            if (dst_cap - (nj_u4)(out - dst) < (nj_u4)(channels * 2)) {
                return frames;
            }
            *out++ = (nj_u1)(pred[c] & 0xFF);
            *out++ = (nj_u1)((pred[c] >> 8) & 0xFF);
        }
        frames++;
        /*
         * Sample bytes are grouped in 4-byte units: mono holds eight samples
         * of the one channel, stereo alternates a left unit and a right unit.
         */
        for (s = 1; s < samples_per_block; s += 8) {
            for (c = 0; c < channels; c++) {
                const nj_u1 *q;
                int k;

                if (channels == 1) {
                    q = p + 4 + ((s - 1) / 8) * 4;
                } else {
                    q = p + 8 + ((s - 1) / 8) * 8 + c * 4;
                }
                if (q + 4 > p + block) {
                    break;
                }
                for (k = 0; k < 4; k++) {
                    int nibbles[2];
                    int n;

                    nibbles[0] = (int)(q[k] & 0x0F);
                    nibbles[1] = (int)((q[k] >> 4) & 0x0F);
                    for (n = 0; n < 2; n++) {
                        int v;
                        if (((s - 1) / 8) * 8 + k * 2 + n >= samples_per_block - 1) {
                            break;
                        }
                        v = ima_decode(nibbles[n], &idx[c], &pred[c]);
                        if (dst_cap - (nj_u4)(out - dst) < 2u) {
                            return frames;
                        }
                        *out++ = (nj_u1)(v & 0xFF);
                        *out++ = (nj_u1)((v >> 8) & 0xFF);
                        frames++;
                    }
                }
            }
        }
        pos += (nj_u4)block;
    }
    return frames;
}

/* Parse RIFF/WAVE: PCM (format 1) and Microsoft IMA ADPCM (format 0x11).
 * Both chunk walks are bounded by the buffer, so a truncated or hostile file
 * cannot make this read past the end. */
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
    int block_align = 0;
    int adpcm = 0;

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
                /* blockAlign is at +12: 16 bytes is the whole chunk
                 * for PCM, and an IMA ADPCM file needs it. */
                block_align = (int)(b[body + 12] | (b[body + 13] << 8));
                if (fmt == 1 && (chan == 1 || chan == 2)
                    && (bits == 8 || bits == 16) && rate > 0) {
                    have_fmt = 1;
                    adpcm = 0;
                } else if (fmt == 0x11 && (chan == 1 || chan == 2)
                           && bits == 4 && rate > 0
                           && block_align >= 4 * chan + 4) {
                    have_fmt = 1;
                    adpcm = 1;
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
    out->bytes = data_len;
    out->rate = rate;
    out->channels = chan;
    out->bits = bits;
    out->adpcm = adpcm;
    out->block = block_align;
    if (adpcm) {
        /* Whole blocks only; the last partial block is padding. */
        out->frames = (data_len / (nj_u4)block_align)
                    * (nj_u4)(2 * (block_align - 4 * chan) / chan + 1);
    } else {
        out->frames = data_len / (nj_u4)(chan * (bits / 8));
    }
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
#if NJ_ENABLE_MP3
    {
        media_stream_state st;

        memcpy(&st, b, sizeof(st));
        if (st.adec != NULL && st.aops != NULL) {
            st.aops->close(st.adec);
        }
    }
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

/*
 * The port receives samples for one Player, so a Player's VolumeControl level
 * has to be applied here: the PCM device is shared and knows nothing about
 * which Player a buffer came from.  Muting is level 0.  A buffer larger than
 * the staging buffer is written in pieces; a short write leaves the rest in
 * the source for the next frame, exactly like pcm_write() itself.
 */
#define MEDIA_PCM_STAGE 1024

static int media_pcm_write(nj_vm *vm, nj_ref p, const nj_s2 *src, int frames)
{
    nj_s2 stage[MEDIA_PCM_STAGE * 2];
    nj_s4 vol = r_i(vm, p, F_P_VOLUME);
    nj_s4 ch = r_i(vm, p, F_P_CHAN);
    int done = 0;

    if (ch <= 0) {
        ch = 1;
    }
    if (ch > 2) {
        ch = 2;
    }
    if (r_z(vm, p, F_P_MUTED)) {
        vol = 0;
    }
    if (vol >= 100) {
        return vm->port->pcm_write(vm->port->ud, src, frames);
    }
    if (vol < 0) {
        vol = 0;
    }
    while (done < frames) {
        int n = frames - done;
        int i, took;

        if (n > MEDIA_PCM_STAGE) {
            n = MEDIA_PCM_STAGE;
        }
        for (i = 0; i < n * (int)ch; i++) {
            stage[i] = (nj_s2)(((int)src[(size_t)done * (size_t)ch
                                         + (size_t)i] * (int)vol) / 100);
        }
        took = vm->port->pcm_write(vm->port->ud, stage, n);
        if (took <= 0) {
            break;
        }
        done += took;
        if (took < n) {
            break;              /* device full: the rest stays in the source */
        }
    }
    return done;
}

/*
 * Where playback actually is.  F_P_PUSHED is the write frontier - everything
 * up to it has been handed to the port - but a device queue can hold nearly a
 * second.  A port that reports its unplayed frames lets the media time (and a
 * later start() after stop()) land on the audible position instead, which is
 * what a title pausing its music expects.
 */
static nj_s4 media_played(nj_vm *vm, nj_ref p, nj_s4 *loops)
{
    nj_s8 played = r_i(vm, p, F_P_PUSHED);
    nj_s4 done = r_i(vm, p, F_P_LOOPDONE);

    if (vm->pcm_owner == p && vm->port && vm->port->pcm_queued) {
        nj_s4 queued = vm->port->pcm_queued(vm->port->ud);
        if (queued > 0) {
            nj_s4 frames = r_i(vm, p, F_P_FRAMES);
            played -= queued;
            /* The queue can still contain the previous loop's tail, or even
             * several whole loops of a short effect.  Walk back across those
             * boundaries instead of reporting zero and losing that tail. */
            if (played < 0 && frames > 0 && done > 0) {
                nj_s8 back = (-played + frames - 1) / frames;
                if (back > done) back = done;
                played += back * frames;
                done -= (nj_s4)back;
            }
            if (played < 0) played = 0;
        }
    }
    if (loops) *loops = done;
    return (nj_s4)played;
}

static void media_note_pushed(nj_vm *vm, nj_ref p, nj_s4 pushed)
{
    nj_s4 rate = r_i(vm, p, F_P_RATE);

    w_i(vm, p, F_P_PUSHED, pushed);
    if (rate > 0) {
        w_j(vm, p, F_P_TIME,
            (nj_s8)media_played(vm, p, NULL) * 1000000LL / (nj_s8)rate);
    }
}

/* Move a retained decoder to an exact PCM frame and forget any old scratch.
 * Round UP when translating frames to microseconds: the codec rounds back
 * down, and rounding twice otherwise loses a sample at rates such as 44100. */
static int media_stream_seek(nj_vm *vm, nj_ref p, nj_s4 frame)
{
    nj_ref data = r_r(vm, p, F_P_DATA, "[B");
    media_stream_state state;

    if (!data || barray_len(data) < NJ_MEDIA_STREAM_BYTES) return 0;
    memcpy(&state, barray_bytes(data), sizeof(state));
#if NJ_ENABLE_MP3
    if (r_z(vm, p, F_P_AVK)) {
        nj_s4 rate = r_i(vm, p, F_P_RATE);
        if (rate <= 0 || !state.aops || !state.adec || !state.aops->seek ||
            state.aops->seek(state.adec,
                ((nj_s8)frame * 1000000LL + rate - 1) / rate) != NJ_AV_OK) {
            return 0;
        }
        state.in_off = barray_len(r_r(vm, p, F_P_SRC, "[B"));
    } else
#endif
    {
#if NJ_ENABLE_MIDI
        if (!state.stream || nj_midi_stream_seek(state.stream, (nj_u4)frame)
                             != (nj_u4)frame) return 0;
#else
        (void)frame;
        return 0;
#endif
    }
    state.pending = state.offset = 0u;
    memcpy(data->u.bytes, &state, sizeof(state));
    return 1;
}

/* stop()/preemption throw the device queue away.  Rewind both the position
 * and the renderer, keeping already rendered samples when the scratch still
 * holds the discarded suffix. */
static void media_sync_played(nj_vm *vm, nj_ref p)
{
    nj_s4 rate, played, loops;
    nj_s8 dropped;

    if (!vm->port || !vm->port->pcm_queued) {
        return;
    }
    rate = r_i(vm, p, F_P_RATE);
    played = media_played(vm, p, &loops);
    dropped = (nj_s8)(r_i(vm, p, F_P_LOOPDONE) - loops)
                         * r_i(vm, p, F_P_FRAMES)
              + r_i(vm, p, F_P_PUSHED) - played;
    if (dropped > 0 && r_z(vm, p, F_P_STREAM)) {
        nj_ref data = r_r(vm, p, F_P_DATA, "[B");
        media_stream_state state;
        if (data && barray_len(data) >= NJ_MEDIA_STREAM_BYTES) {
            memcpy(&state, barray_bytes(data), sizeof(state));
            if (loops == r_i(vm, p, F_P_LOOPDONE) &&
                dropped <= (nj_s8)state.offset) {
                state.offset -= (nj_u4)dropped;
                state.pending += (nj_u4)dropped;
                memcpy(data->u.bytes, &state, sizeof(state));
            } else if (!media_stream_seek(vm, p, played)) {
                nj_logf(vm, 2, "media: audio decoder could not restore its position");
            }
        }
    }
    w_i(vm, p, F_P_PUSHED, played);
    w_i(vm, p, F_P_LOOPDONE, loops);
    if (rate > 0) {
        w_j(vm, p, F_P_TIME,
            (nj_s8)played * 1000000LL / (nj_s8)rate);
    }
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
#if NJ_ENABLE_MP3
    if (r_z(vm, player, F_P_AVK)) {
        media_stream_state state;
        nj_u1 *scratch;
        nj_u4 want;

        if (total < NJ_MEDIA_STREAM_BYTES || max_frames <= 0) {
            return 0;
        }
        memcpy(&state, b, sizeof(state));
        if (state.adec == NULL || state.aops == NULL) {
            return 0;
        }
        scratch = (nj_u1 *)(void *)(b + sizeof(state));
        if (state.pending == 0u) {
            state.offset = 0u;
            want = NJ_MEDIA_STREAM_FRAMES;
            if (want > (nj_u4)max_frames) {
                want = (nj_u4)max_frames;
            }
            while (state.pending == 0u) {
                nj_ref src = r_r(vm, player, F_P_SRC, "[B");
                const nj_u1 *sb = barray_bytes(src);
                nj_u4 slen = barray_len(src);
                nj_av_step step;
                int rc;

                if (sb == NULL || state.in_off > slen) {
                    break;
                }
                memset(&step, 0, sizeof(step));
                /* Consumed input can still leave PCM in the decoder's FIFO. */
                step.in = state.in_off < slen ? sb + state.in_off : NULL;
                step.in_len = slen - state.in_off;
                step.out = scratch;
                step.out_cap = want * (nj_u4)chan * 2u;
                rc = state.aops->decode(state.adec, &step);
                state.in_off += step.in_used;
                if (step.frames > 0u) {
                    state.pending = step.frames;
                    break;
                }
                if (rc != NJ_AV_OK || step.in_used == 0u) {
                    break;
                }
            }
            memcpy((void *)b, &state, sizeof(state));
            if (state.pending == 0u) {
                w_i(vm, player, F_P_FRAMES, done);
                return 0;
            }
        }
        want = state.pending;
        if (want > (nj_u4)max_frames) {
            want = (nj_u4)max_frames;
        }
        took = media_pcm_write(vm, player,
            (const nj_s2 *)(const void *)(scratch + state.offset * (nj_u4)chan * 2u),
            (int)want);
        if (took < 0 || (nj_u4)took > want) {
            return 0;
        }
        state.pending -= (nj_u4)took;
        state.offset += (nj_u4)took;
        memcpy((void *)b, &state, sizeof(state));
        media_note_pushed(vm, player, done + took);
        return took;
    }
#endif /* NJ_ENABLE_MP3 */
#if NJ_ENABLE_MIDI
    if (r_z(vm, player, F_P_STREAM)) {
        media_stream_state state;
        nj_u1 *scratch;
        nj_u4 want;

        if (total < NJ_MEDIA_STREAM_BYTES || max_frames <= 0) {
            return 0;
        }
        memcpy(&state, b, sizeof(state));
        if (state.stream == NULL) {
            return 0;
        }
        scratch = (nj_u1 *)(void *)(b + sizeof(state));
        if (state.pending == 0u) {
            want = NJ_MEDIA_STREAM_FRAMES;
            if (want > (nj_u4)max_frames) want = (nj_u4)max_frames;
            state.pending = nj_midi_stream_pull(state.stream, scratch, want);
            state.offset = 0u;
            memcpy((void *)b, &state, sizeof(state));
            if (state.pending == 0u) {
                /* Keep the renderer until close/deallocate so a later seek
                 * can restart it. Never reinterpret its holder as PCM.
                 * Only trust the count when this pass rendered something: a
                 * rewind that could not rebuild the song leaves the renderer
                 * at its end, and overwriting pFrames with zero there used to
                 * turn one failed restart into a Player that could never play
                 * again. */
                if (done > 0) {
                    w_i(vm, player, F_P_FRAMES, done);
                } else {
                    nj_logf(vm, 2, "media: the MIDI renderer ended without "
                                   "producing a frame");
                }
                return 0;
            }
        }
        want = state.pending;
        if (want > (nj_u4)max_frames) want = (nj_u4)max_frames;
        took = media_pcm_write(vm, player,
            (const nj_s2 *)(const void *)(scratch + state.offset * (nj_u4)chan * 2u),
            (int)want);
        /* A short/zero write leaves a suffix to retry. In particular, start()
         * can render before the port reports space; pcm_free is only a hint. */
        if (took < 0 || (nj_u4)took > want) {
            return 0;
        }
        state.pending -= (nj_u4)took;
        state.offset += (nj_u4)took;
        memcpy((void *)b, &state, sizeof(state));
        media_note_pushed(vm, player, done + took);
        return took;
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
    took = media_pcm_write(vm, player,
                           (const nj_s2 *)(const void *)(b + off), frames);
    if (took < 0) {
        return 0;
    }
    media_note_pushed(vm, player, done + took);
    /* The duration is frames over rate: the sample rate never changes. */
    w_j(vm, player, F_P_DURATION,
        (nj_s8)((nj_u4)r_i(vm, player, F_P_FRAMES)) * 1000000LL
            / (nj_s8)r_i(vm, player, F_P_RATE));
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
 * ---- one PCM device, one Player that owns it ----
 *
 * nj_port.pcm_open() configures ONE device: it takes a sample rate and a
 * channel count, and nj_port.pcm_close() is not "close my stream", it is
 * "close the device".  A title that plays a voice line over its music - the
 * ordinary case in the corpus, and the reason 极品飞车14之热力追踪中文版_4160 went
 * silent after a busted animation - calls two Players at once, and drawn
 * literally the second Player's open() reconfigures the hardware under the
 * first, while its close() leaves the first writing into a closed device.
 * That is not a mixing bug the port could fix: on a 2004 phone there was one
 * DAC and the audio system, not the MIDlet, decided who had it.
 *
 * So the runtime arbitrates, the way the device class does:
 *
 *   - the Player that starts last owns the device; the others are paused on
 *     the DEVICE side only.  Their media clock holds, their position holds and
 *     their Player state stays STARTED, because a title that never listens for
 *     a "stopped" event (most of them) must not be left silent forever;
 *   - when the owner ends, stops or closes, the oldest paused Player is handed
 *     the device back at ITS rate and channel count, from its own position;
 *   - an open. that fails (no device, no support) leaves the owner NULL and the
 *     paused Players waiting, instead of pretending to play.
 *
 * Mixing is still not attempted here: a port that can mix (SDL2's queue, the
 * Android ring) is welcome to, but the runtime keeps its side of the deal so
 * that a port which genuinely has one stream does not lose music.
 *
 * A platform MIDI synth Player is not part of this: it owns its own handle and
 * its own clock (see F_P_PLAT), and the pump only asks it whether it finished.
 */
static int media_dev_wants(nj_vm *vm, nj_ref p)
{
    if (p == NULL || r_i(vm, p, F_P_STATE) != 400) {
        return 0;
    }
    if (r_z(vm, p, F_P_PLAT)) {
        return 0;
    }
    if (r_i(vm, p, F_P_KIND) == 2) {
        return r_i(vm, p, F_P_APCMN) > 0;   /* a movie with a sound track */
    }
    return r_i(vm, p, F_P_KIND) == 1;       /* sampled audio */
}

static int media_dev_format(nj_vm *vm, nj_ref p, int *rate, int *chans)
{
    if (r_i(vm, p, F_P_KIND) == 2) {
        *rate = (int)NJ_AMR_RATE;
        *chans = 1;
    } else {
        *rate = (int)r_i(vm, p, F_P_RATE);
        *chans = (int)r_i(vm, p, F_P_CHAN);
    }
    return *rate > 0 && *chans > 0;
}

/* Preserve the audible position unless a successful seek already replaced it. */
static void media_dev_close(nj_vm *vm, int keep_position)
{
    nj_ref owner = vm->pcm_owner;

    if (owner == NULL) {
        return;
    }
    if (keep_position && r_i(vm, owner, F_P_KIND) == 1) {
        media_sync_played(vm, owner);   /* the unplayed tail is about to go */
    }
    if (vm->port && vm->port->pcm_close) {
        vm->port->pcm_close(vm->port->ud);
    }
    w_z(vm, owner, F_P_OPEN, 0);
    vm->pcm_owner = NULL;
}

/* Hand the device to `p`, closing the previous owner.  1 when it is open. */
static int media_dev_claim(nj_vm *vm, nj_ref p)
{
    int rate, chans;
    int had_owner;

    if (!vm->port || !vm->port->pcm_open) {
        return 0;
    }
    if (vm->pcm_owner == p) {
        return r_z(vm, p, F_P_OPEN);
    }
    had_owner = vm->pcm_owner != NULL;
    media_dev_close(vm, 1);
    if (!media_dev_format(vm, p, &rate, &chans)) {
        return 0;
    }
    if (vm->port->pcm_open(vm->port->ud, rate, chans) != 0) {
        return 0;
    }
    vm->pcm_owner = p;
    w_z(vm, p, F_P_OPEN, 1);
    /*
     * How much the queue can hold, measured while it is empty.  The end of a
     * one-shot sound uses it to tell "all handed over" from "all heard".
     */
    if (vm->port->pcm_free) {
        int cap = vm->port->pcm_free(vm->port->ud);
        w_i(vm, p, F_P_DEVROOM, cap > 0 ? cap : 0);
    } else {
        w_i(vm, p, F_P_DEVROOM, 0);
    }
    if (had_owner) {
        /* A title with two sampled Players at once is the case this exists
         * for; the line is what a log-less phone report is missing. */
        nj_logf(vm, 2, "media: the PCM device moved to another Player, "
                       "%d Hz, %d channel(s)", rate, chans);
    }
    return 1;
}

/* Release the device and give the oldest paused Player its turn. */
static void media_dev_release(nj_vm *vm, nj_ref p)
{
    nj_ref v;
    nj_s4 n, i;

    if (vm->pcm_owner != p) {
        return;                 /* not ours: nothing to hand back */
    }
    media_dev_close(vm, 1);
    v = vm->media_players;
    if (v == NULL) {
        return;
    }
    n = vec_size(vm, v);
    for (i = 0; i < n; i++) {
        nj_ref q = vec_at(vm, v, i);
        if (q == NULL || q == p) {
            continue;
        }
        if (media_dev_wants(vm, q) && media_dev_claim(vm, q)) {
            nj_logf(vm, 2, "media: the PCM device went back to a paused "
                           "Player");
            break;
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

/* Rewind the retained renderer: the Player owns the reference to the song
 * bytes (F_P_SRC), the stream holds only a pointer, and a restart re-opens the
 * song instead of allocating another copy in the Java heap. */
static int media_restart(nj_vm *vm, nj_ref p)
{
    if (r_i(vm, p, F_P_FRAMES) <= 0) return 0;
#if NJ_ENABLE_MIDI
    if (r_z(vm, p, F_P_STREAM) && !r_z(vm, p, F_P_AVK)) {
        nj_ref data = r_r(vm, p, F_P_DATA, "[B");
        media_stream_state state;
        if (data == NULL || barray_len(data) < NJ_MEDIA_STREAM_BYTES) return 0;
        memcpy(&state, barray_bytes(data), sizeof(state));
        if (state.stream == NULL) return 0;
        if (!nj_midi_stream_restart(state.stream)) return 0;
        state.pending = state.offset = 0u;
        memcpy(data->u.bytes, &state, sizeof(state));
    }
#endif
#if NJ_ENABLE_MP3
    if (r_z(vm, p, F_P_AVK)) {
        nj_ref data = r_r(vm, p, F_P_DATA, "[B");
        media_stream_state state;

        if (data == NULL || barray_len(data) < NJ_MEDIA_STREAM_BYTES) {
            return 0;
        }
        memcpy(&state, barray_bytes(data), sizeof(state));
        if (state.aops == NULL || state.adec == NULL ||
            state.aops->seek == NULL ||
            state.aops->seek(state.adec, 0) != NJ_AV_OK) {
            return 0;
        }
        state.pending = state.offset = 0u;
        /* seek() positions the retained source; do not feed its prefix again. */
        state.in_off = barray_len(r_r(vm, p, F_P_SRC, "[B"));
        memcpy(data->u.bytes, &state, sizeof(state));
    }
#endif
    w_i(vm, p, F_P_PUSHED, 0);
    w_j(vm, p, F_P_TIME, 0);
    return 1;
}

#if NJ_ENABLE_VIDEO
#if NJ_ENABLE_AMR
/*
 * The movie's own sound track.
 *
 * A video Player used to decode only the video track, so the corpus intros
 * were silent.  The container iterator can select the AMR-NB sound track now
 * (`nj_mp4_open_audio()`), and the whole track is decoded once, at start(),
 * into a PCM byte array on the Player: a sample of a 3GP may hold one AMR
 * frame or several (doom.3gp 1x20 ms, 1942/movie 5x20 ms per sample), so the
 * samples are concatenated behind an "#!AMR" header and decoded by the same
 * whole-file decoder the bare .amr players use - the frame walk then lives in
 * one place instead of being repeated in three.
 *
 * The PCM is then handed to the port progressively from video_audio_pump(),
 * on the same wall clock the picture follows.
 */
nj_u4 nj_media_movie_amr_decode(nj_vm *vm, const nj_u1 *data, nj_u4 size,
                                 nj_u1 *pcm, nj_u4 pcm_cap)
{
    nj_mp4 it;
    nj_mp4_sample s;
    nj_u4 total = 0u, frames, off, got;
    nj_u1 *host;

    if (!vm || !data || !pcm ||
        nj_mp4_open_audio(&it, data, size) != NJ_MP4_OK ||
        it.codec != NJ_MP4_CODEC_SAMR || it.sample_count == 0u) {
        return 0u;
    }
    nj_mp4_rewind(&it);
    while (nj_mp4_next(&it, &s) == 1) {
        if ((nj_u4)s.offset + s.size > size) {
            return 0u;
        }
        total += s.size;
    }
    /* A movie's audio track is a few tens of kilobytes; anything past a
     * megabyte is a table that lies, and it must not become an allocation. */
    if (total == 0u || total > (1u << 20)) {
        return 0u;
    }
    host = (nj_u1 *)nj_host_alloc(vm, 6u + (nj_size)total);
    if (!host) {
        return 0u;
    }
    memcpy(host, "#!AMR\n", 6u);
    off = 6u;
    nj_mp4_rewind(&it);
    while (nj_mp4_next(&it, &s) == 1) {
        memcpy(host + off, data + s.offset, s.size);
        off += s.size;
    }
    frames = nj_amr_frames(host, off);
    if (frames == 0u ||
        frames * (nj_u4)(NJ_AMR_BLOCK * 2) > pcm_cap) {
        nj_host_free(vm, host);
        return 0u;              /* not decodable AMR after all: stay silent */
    }
    got = nj_amr_decode(host, off, pcm);
    nj_host_free(vm, host);
    return got == frames ? frames : 0u;
}

static int video_audio_decode(nj_vm *vm, nj_ref p, const media_video_state *st,
                              nj_ref src)
{
    nj_u4 frames, cap;
    nj_ref pcm;

    (void)st;
    /* Upper bound for the PCM array: 2000 AMR frames = 40 s of 8 kHz mono
     * s16, more than twice the longest corpus movie (17.2 s).  The helper
     * refuses a track that would need more, so a lying table cannot turn
     * this into an enormous allocation, and a Player that does not start
     * never allocates it at all. */
    cap = 2000u * (nj_u4)NJ_AMR_BLOCK * 2u;
    pcm = nj_byte_array_new(vm, cap);
    if (!pcm) {
        return 0;
    }
    frames = nj_media_movie_amr_decode(vm, src->u.bytes, src->length,
                                       pcm->u.bytes, cap);
    if (frames == 0u) {
        return 0;               /* not decodable AMR after all: stay silent */
    }
    w_r(vm, p, F_P_APCM, "[B", pcm);
    /* pApcmn counts PCM frames (samples), not AMR frames: the pump compares
     * it against elapsed milliseconds * 8 kHz. */
    w_i(vm, p, F_P_APCMN, (nj_s4)(frames * (nj_u4)NJ_AMR_BLOCK));
    w_i(vm, p, F_P_APUSH, 0);
    return 1;
}

/* start(): decode the sound track once and ask for the PCM device if we can. */
static void video_audio_start(nj_vm *vm, nj_ref p)
{
    nj_ref hold = r_r(vm, p, F_P_DATA, "[B");
    nj_ref src = r_r(vm, p, F_P_SRC, "[B");
    media_video_state st;

    if (hold && src && hold->length >= (nj_u4)sizeof(st)) {
        memcpy(&st, hold->u.bytes, sizeof(st));
        if (st.has_audio && r_r(vm, p, F_P_APCM, "[B") == NULL) {
            (void)video_audio_decode(vm, p, &st, src);
        }
    }
    /* A movie with a sound track takes the device from the music, the way the
     * device class would have made it; the picture never waits for it. */
    if (r_i(vm, p, F_P_APCMN) > 0 && !r_z(vm, p, F_P_OPEN)) {
        (void)media_dev_claim(vm, p);
    }
}

/* stop()/end of media: give the device back, keep the decoded PCM for a resume. */
static void video_audio_stop(nj_vm *vm, nj_ref p)
{
    media_dev_release(vm, p);
}

/* Hand the port every PCM frame whose movie time has arrived. */
static void video_audio_pump(nj_vm *vm, nj_ref p)
{
    nj_ref pcm;
    nj_s4 total, pushed, vol;
    nj_s8 start, target;
    nj_s2 scratch[1024];
    const nj_s2 *src;

    if (!r_z(vm, p, F_P_OPEN) || !vm->port || !vm->port->pcm_write) {
        return;
    }
    pcm = r_r(vm, p, F_P_APCM, "[B");
    total = r_i(vm, p, F_P_APCMN);
    pushed = r_i(vm, p, F_P_APUSH);
    if (!pcm || total <= 0 || pushed >= total) {
        return;
    }
    start = r_j(vm, p, F_P_VSTART);
    if (nj_now_ms(vm) < start) {
        return;
    }
    target = (nj_now_ms(vm) - start) * (nj_s8)NJ_AMR_RATE / 1000;
    if (target > (nj_s8)total) {
        target = total;
    }
    if (target <= (nj_s8)pushed) {
        return;
    }
    vol = r_i(vm, p, F_P_VOLUME);
    if (vol < 0) { vol = 0; }
    if (vol > 100) { vol = 100; }
    if (r_z(vm, p, F_P_MUTED)) { vol = 0; }
    src = (const nj_s2 *)(const void *)pcm->u.bytes;
    while ((nj_s8)pushed < target) {
        int freev = vm->port->pcm_free
                        ? vm->port->pcm_free(vm->port->ud) : (1 << 20);
        int chunk, i, wrote;

        if (freev <= 0) {
            break;              /* the device is still playing what it has */
        }
        chunk = (int)((nj_s8)target - (nj_s8)pushed);
        if (chunk > freev) { chunk = freev; }
        if (chunk > (int)(sizeof(scratch) / sizeof(scratch[0]))) {
            chunk = (int)(sizeof(scratch) / sizeof(scratch[0]));
        }
        for (i = 0; i < chunk; i++) {
            scratch[i] = (nj_s2)((int)src[pushed + i] * (int)vol / 100);
        }
        wrote = vm->port->pcm_write(vm->port->ud, scratch, chunk);
        if (wrote <= 0) {
            break;
        }
        pushed += wrote;
    }
    w_i(vm, p, F_P_APUSH, pushed);
}
#else /* !NJ_ENABLE_AMR */
static void video_audio_start(nj_vm *vm, nj_ref p)
{
    (void)vm;
    (void)p;
}
static void video_audio_stop(nj_vm *vm, nj_ref p)
{
    (void)vm;
    (void)p;
}
static void video_audio_pump(nj_vm *vm, nj_ref p)
{
    (void)vm;
    (void)p;
}
#endif /* NJ_ENABLE_AMR */

/*
 * Is the next sample of this movie due yet?
 *
 * A movie's clock is real time: sample n belongs at start + n/fps seconds,
 * where `start` is the wall clock read at start() (adjusted on a restart so a
 * stopped Player resumes instead of skipping).  The pump is called once per
 * emulated frame, so on a pale that keeps up with its cap this is also the
 * movie's own rate, and on a frame that is slow the picture follows the clock
 * instead of running slow - which is what a handset's media clock does, and
 * what a title that sleeps for the clip's length expects.
 *
 * A port with no frame cap (the test VMs, bench runs) has no playback clock to
 * follow: there pacing is off and one pump call decodes one sample, which is
 * the deterministic contract the unit tests and the frame-comparison tools
 * rely on.
 */
static int video_sample_due(nj_vm *vm, nj_ref p)
{
    nj_s4 cap = (vm->port && vm->port->fps_cap > 0) ? vm->port->fps_cap : 0;
    nj_s4 fps = r_i(vm, p, F_P_RATE);
    nj_s8 start;
    nj_s4 pushed;

    if (cap <= 0 || fps <= 0) {
        return 1;
    }
    start = r_j(vm, p, F_P_VSTART);
    pushed = r_i(vm, p, F_P_PUSHED);
    return nj_now_ms(vm) >= start + ((nj_s8)pushed * 1000) / (nj_s8)fps;
}

/*
 * Record the wall-clock time at which this Player's next sample is due.
 * Called from start(); the `pushed` subtraction makes stop()+start() resume
 * where it left off rather than treating the paused time as if it played.
 */
static void video_set_start(nj_vm *vm, nj_ref p)
{
    nj_s4 cap = (vm->port && vm->port->fps_cap > 0) ? vm->port->fps_cap : 0;
    nj_s4 fps = r_i(vm, p, F_P_RATE);
    nj_s4 pushed = r_i(vm, p, F_P_PUSHED);
    nj_s8 start = nj_now_ms(vm);

    if (cap > 0 && fps > 0) {
        start -= ((nj_s8)pushed * 1000) / (nj_s8)fps;
    }
    w_j(vm, p, F_P_VSTART, start);
}

/*
 * Re-open the provider decoder a movie Player owns.
 *
 * deallocate() releases the decoder (the holder byte array is its only
 * pointer), and a Player may be realized and started again afterwards, so the
 * next start() rebuilds it from the same source.  A Player whose decoder is
 * still there reports success and does nothing.
 */
static int video_decoder_reopen(nj_vm *vm, nj_ref p)
{
    nj_ref hold = r_r(vm, p, F_P_DATA, "[B");
    nj_ref src = r_r(vm, p, F_P_SRC, "[B");
    media_video_state st;
    nj_av_mem *mem;
    nj_av_info info;
    const char *err = NULL;
    nj_av_dec *dec;
    int fresh;

    if (hold == NULL || src == NULL || hold->length < (nj_u4)sizeof(st)) {
        return 0;
    }
    memcpy(&st, hold->u.bytes, sizeof(st));
    if (st.vdec != NULL) {
        return 1;
    }
    if (st.vops == NULL) {
        return 0;
    }
    mem = st.mem;
    fresh = (mem == NULL);
    if (fresh) {
        mem = av_mem_new(vm);
        if (mem == NULL) {
            return 0;
        }
        st.mem = mem;
    }
    memset(&info, 0, sizeof(info));
    dec = st.vops->open(src->u.bytes, src->length, mem, &info, &err);
    if (dec == NULL) {
        if (fresh) {
            nj_host_free(vm, st.mem);   /* nothing borrowed it: give it back */
            st.mem = NULL;
            memcpy(hold->u.bytes, &st, sizeof(st));
        }
        return 0;
    }
    st.vdec = dec;
    nj_mp4_rewind(&st.m);
    memcpy(hold->u.bytes, &st, sizeof(st));
    w_i(vm, p, F_P_PUSHED, 0);
    w_j(vm, p, F_P_TIME, 0);
    w_i(vm, p, F_P_APUSH, 0);
    w_z(vm, p, F_P_STREAM, 1);
    return 1;
}

/*
 * Move a movie Player to `want' microseconds, for Player.setMediaTime().
 *
 * The container walks samples in order and MPEG-4 Part 2 P-VOPs reference the
 * previous picture, so the only correct seek is: rebuild the decoder, then
 * decode every sample before the target so the reference frames are the ones
 * the target was encoded against.  Skipping samples is not possible without an
 * index of I-VOPs, which 3GP files in the corpus do not carry.
 *
 * `want' arrives clamped to the Player's duration.  Returns the media time
 * actually reached: the target sample's start when the walk gets there, the
 * last sample start when the track stops early, and the unchanged position
 * when the decoder cannot be rebuilt.
 */
static nj_s8 video_seek(nj_vm *vm, nj_ref p, nj_s8 want)
{
    nj_ref hold = r_r(vm, p, F_P_DATA, "[B");
    nj_ref src = r_r(vm, p, F_P_SRC, "[B");
    nj_ref vbuf = r_r(vm, p, F_P_VBUF, "[B");
    media_video_state st;
    nj_mp4_sample smp;
    nj_av_step step;
    nj_av_mem *mem;
    nj_av_info info;
    const char *err = NULL;
    nj_av_dec *dec;
    nj_s8 actual = r_j(vm, p, F_P_TIME);
    nj_s4 decoded = 0;
    int rc = 0;

    if (hold == NULL || src == NULL || vbuf == NULL ||
        hold->length < (nj_u4)sizeof(st)) {
        return actual;
    }
    memcpy(&st, hold->u.bytes, sizeof(st));
    if (st.vops == NULL) {
        return actual;
    }
    mem = st.mem;
    if (mem == NULL) {
        mem = av_mem_new(vm);
        if (mem == NULL) {
            return actual;
        }
        st.mem = mem;
    }
    memset(&info, 0, sizeof(info));
    /* Open the replacement before closing the old decoder: a source that
     * cannot be re-opened leaves the Player decoding on instead of dead. */
    dec = st.vops->open(src->u.bytes, src->length, mem, &info, &err);
    if (dec == NULL) {
        return actual;
    }
    if (st.vdec != NULL) {
        st.vops->close(st.vdec);
    }
    st.vdec = dec;
    nj_mp4_rewind(&st.m);
    while ((rc = nj_mp4_next(&st.m, &smp)) == 1) {
        nj_s8 t = st.m.timescale > 0u
            ? (nj_s8)smp.dts * 1000000LL / (nj_s8)st.m.timescale : 0;

        if (t >= want) {
            actual = t;         /* the pump shows this sample next */
            break;
        }
        if ((nj_u4)smp.offset + smp.size > src->length) {
            break;
        }
        memset(&step, 0, sizeof(step));
        step.in = src->u.bytes + smp.offset;
        step.in_len = smp.size;
        step.out = vbuf->u.bytes;
        step.out_cap = vbuf->length;
        if (st.vops->decode(st.vdec, &step) != NJ_AV_OK) {
            /* The reference chain stops here; leave the iterator at the next
             * sample so the pump carries on instead of replaying this one. */
            actual = t;
            decoded++;
            break;
        }
        decoded++;
        actual = t;
    }
    if (rc != 1 && decoded > 0) {
        /* Walked off the end of the track: the next pump reports endOfMedia,
         * so the position is the end of the movie. */
        actual = want;
    }
    memcpy(hold->u.bytes, &st, sizeof(st));
    w_i(vm, p, F_P_PUSHED, decoded);
    w_j(vm, p, F_P_TIME, actual);
#if NJ_ENABLE_AMR
    /* The movie's PCM is handed to the device against this same clock. */
    w_i(vm, p, F_P_APUSH,
        (nj_s4)((nj_u8)actual * (nj_u8)NJ_AMR_RATE / 1000000u));
#endif
    video_set_start(vm, p);
    return actual;
}

/*
 * Release the decoder a movie Player owns.  close() and deallocate() call
 * this: the holder byte array is the only pointer to the provider state, so
 * dropping it without close() leaks the decoder and its frame buffers until
 * the whole VM is destroyed.  A title that opens and closes a movie Player per
 * menu visit would grow the host heap by one decoder every time.  Safe on a
 * Player that never had one, and on a second call.
 */
static void video_decoder_release(nj_vm *vm, nj_ref p)
{
    nj_ref hold;
    media_video_state st;

    /* Only a movie Player's holder is a media_video_state; every other kind
     * keeps something else in pData (PCM bytes, a MIDI stream, a tone list). */
    if (r_i(vm, p, F_P_KIND) != 2) {
        return;
    }
    hold = r_r(vm, p, F_P_DATA, "[B");
    if (hold == NULL || hold->length < (nj_u4)sizeof(st)) {
        return;
    }
    memcpy(&st, hold->u.bytes, sizeof(st));
    if (st.vdec != NULL && st.vops != NULL && st.vops->close != NULL) {
        st.vops->close(st.vdec);
    }
    if (st.mem != NULL) {
        nj_host_free(vm, st.mem);
    }
    /*
     * Keep pData/pVbuf and the container position: deallocate() puts the
     * Player back into the UNREALIZED state, and start() may realize it again.
     * The decoder is the expensive part, and it is rebuilt from pSrc on the
     * next start; dropping the holder would throw that chance away.
     */
    st.vdec = NULL;
    st.mem = NULL;
    memcpy(hold->u.bytes, &st, sizeof(st));
    w_i(vm, p, F_P_PUSHED, 0);
    w_j(vm, p, F_P_TIME, 0);
}

/*
 * One video frame: demultiplex the next sample and decode it into the Player's
 * RGB565 buffer.  The audio path below pushes to a device; a movie has nowhere
 * to push, so a frame is simply the next decoded picture plus its timestamp.
 */
static void media_video_step(nj_vm *vm, nj_ref p)
{
    nj_ref hold = r_r(vm, p, F_P_DATA, "[B");
    nj_ref src = r_r(vm, p, F_P_SRC, "[B");
    nj_ref vbuf = r_r(vm, p, F_P_VBUF, "[B");
    media_video_state st;
    nj_mp4_sample smp;
    nj_av_step step;
    int rc;

    if (hold == NULL || src == NULL || vbuf == NULL ||
        hold->length < (nj_u4)sizeof(st)) {
        return;
    }
    memcpy(&st, hold->u.bytes, sizeof(st));
    if (st.vops == NULL || st.vdec == NULL) {
        return;
    }
    /*
     * A movie advances only while the Player is STARTED.  Before this check
     * the pump decoded one sample per emulated frame from the moment
     * createPlayer() returned, so a title that realized and set up its display
     * started its movie from wherever the title's own timeline happened to be
     * - and a paused/stopped Player kept consuming samples.  stop() puts the
     * state back to PREFETCHED, so a stopped Player simply waits here.
     */
    if (r_i(vm, p, F_P_STATE) != 400) {
        return;
    }
    video_audio_pump(vm, p);    /* the sound clock and the picture clock agree */
    if (!video_sample_due(vm, p)) {
        return;                 /* the movie's own clock has not reached it */
    }
    if (nj_mp4_next(&st.m, &smp) != 1) {
        video_audio_stop(vm, p);
        w_i(vm, p, F_P_STATE, 300);
        fire(vm, p, "endOfMedia");
        media_unregister(vm, p);
        memcpy(hold->u.bytes, &st, sizeof(st));
        return;
    }
    if ((nj_u4)smp.offset + smp.size > src->length) {
        return;
    }
    memset(&step, 0, sizeof(step));
    step.in = src->u.bytes + smp.offset;
    step.in_len = smp.size;
    step.out = vbuf->u.bytes;
    step.out_cap = vbuf->length;
    rc = st.vops->decode(st.vdec, &step);
    memcpy(hold->u.bytes, &st, sizeof(st));
    if (rc != NJ_AV_OK || step.out_used == 0u) {
        return;
    }
    w_i(vm, p, F_P_PUSHED, r_i(vm, p, F_P_PUSHED) + 1);
    w_j(vm, p, F_P_TIME,
        st.m.timescale > 0u
            ? (nj_s8)smp.dts * 1000000LL / (nj_s8)st.m.timescale : 0);
}

/*
 * Direct-video overlay.  USE_DIRECT_VIDEO means the MIDlet hands the video a
 * Canvas and the implementation paints the movie itself; the Canvas' own
 * paint() knows nothing about it.  This runs at the frame boundary after the
 * Canvas content has been mirrored, and draws the Player's current RGB565
 * frame over the display at the location and size the control asked for.
 */
static void video_blit_rgb565(nj_vm *vm, nj_ref p, nj_surface *scr)
{
    nj_ref vb = r_r(vm, p, F_P_VBUF, "[B");
    nj_s4 vw = r_i(vm, p, F_P_VW);
    nj_s4 vh = r_i(vm, p, F_P_VH);
    const nj_u1 *src;
    nj_gfx g;
    int dx, dy, dw, dh, x, y;

    if (!vb || vw <= 0 || vh <= 0 || !scr || !scr->pixels) {
        return;
    }
    if (vb->length < (nj_u4)vw * (nj_u4)vh * 2u) {
        return;
    }
    if (r_z(vm, p, F_P_VFULL)) {
        dx = 0;
        dy = 0;
        dw = scr->width;
        dh = scr->height;
    } else {
        dx = (int)r_i(vm, p, F_P_VDX);
        dy = (int)r_i(vm, p, F_P_VDY);
        dw = (int)r_i(vm, p, F_P_VDW);
        dh = (int)r_i(vm, p, F_P_VDH);
        if (dw <= 0) { dw = vw; }
        if (dh <= 0) { dh = vh; }
    }
    if (dw <= 0 || dh <= 0) {
        return;
    }
    src = vb->u.bytes;
    nj_gfx_init(&g, scr);
    if (dw == vw && dh == vh) {
        /* Same size is the common case: one image blit, whole rows. */
        nj_image img;

        img.width = vw;
        img.height = vh;
        img.format = NJ_PIXFMT_RGB565;
        img.pixels = (nj_u4 *)(void *)src;
        img.has_alpha = 0;
        nj_gfx_draw_image(&g, &img, dx, dy, 0, 0, vw, vh);
        return;
    }
    /* A display size other than the source size is scaled nearest-neighbour,
       which is what a fixed-function video path of this class does.  The
       destination rectangle is clipped to the display. */
    for (y = 0; y < dh; y++) {
        int py = dy + y;
        int sy;
        const nj_u1 *row;

        if (py < 0 || py >= scr->height) {
            continue;
        }
        sy = (int)((nj_s4)y * vh / dh);
        if (sy >= vh) { sy = vh - 1; }
        row = src + (nj_size)sy * (nj_size)vw * 2u;
        for (x = 0; x < dw; x++) {
            int px = dx + x;
            int sx;
            nj_u4 c, r, gg, b;

            if (px < 0 || px >= scr->width) {
                continue;
            }
            sx = (int)((nj_s4)x * vw / dw);
            if (sx >= vw) { sx = vw - 1; }
            c = (nj_u4)row[sx * 2] | ((nj_u4)row[sx * 2 + 1] << 8);
            r = (c >> 11) & 0x1Fu;
            gg = (c >> 5) & 0x3Fu;
            b = c & 0x1Fu;
            r = (r << 3) | (r >> 2);
            gg = (gg << 2) | (gg >> 4);
            b = (b << 3) | (b >> 2);
            nj_gfx_set_color(&g, 0xFF000000u | (r << 16) | (gg << 8) | b);
            nj_gfx_pixel(&g, px, py);
        }
    }
}

void nj_media_video_overlay(nj_vm *vm)
{
    nj_ref cur, players;
    nj_s4 n, i;
    nj_surface *scr;

    if (!vm || !vm->media_players || !vm->screen.pixels) {
        return;
    }
    cur = nj_midp_current(vm);
    if (!cur) {
        return;
    }
    scr = nj_midp_screen(vm);
    if (!scr || !scr->pixels) {
        return;
    }
    players = vm->media_players;
    n = vec_size(vm, players);
    for (i = 0; i < n; i++) {
        nj_ref p = vec_at(vm, players, i);
        nj_ref target;

        if (!p || r_i(vm, p, F_P_KIND) != 2) {
            continue;
        }
        if (r_i(vm, p, F_P_STATE) != 400 || !r_z(vm, p, F_P_VVIS)) {
            continue;
        }
        if (r_i(vm, p, F_P_VDMODE) != 1) {
            continue;
        }
        target = r_r(vm, p, F_P_VCANVAS,
                     "Ljavax/microedition/lcdui/Canvas;");
        if (target != cur) {
            continue;
        }
        video_blit_rgb565(vm, p, scr);
    }
}

#else /* !NJ_ENABLE_VIDEO */
void nj_media_video_overlay(nj_vm *vm)
{
    (void)vm;
}
#endif /* NJ_ENABLE_VIDEO */

void nj_media_frame(nj_vm *vm)
{
    nj_ref v;
    nj_s4 n, i;

    if (!vm || !vm->media_players || !vm->port) {
        return;
    }
    v = vm->media_players;
    n = vec_size(vm, v);
    for (i = n - 1; i >= 0; i--) {
        nj_ref p = vec_at(vm, v, i);
#if NJ_ENABLE_VIDEO
        if (r_i(vm, p, F_P_KIND) == 2) {
            media_video_step(vm, p);
            continue;
        }
#endif
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
            int finished = mh != NULL && vm->port->midi_finished ?
                vm->port->midi_finished(vm->port->ud, mh) : 0;
            if (finished) {
                nj_s4 loop = r_i(vm, p, F_P_LOOP);
                nj_s4 done = r_i(vm, p, F_P_LOOPDONE) + 1;
                if (finished > 0 && loop > 1 && done < loop) {
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
                        fire(vm, p, finished < 0 ? "stopped" : "endOfMedia");
                    }
                    media_unregister(vm, p);
                }
            }
            continue;
        }
        /*
         * The one PCM device belongs to exactly one of these Players (see the
         * device ownership block above).  A paused Player keeps its position
         * and its STARTED state and writes nothing; if the device came free
         * without a successor it is claimed here, so a start() whose open()
         * failed can still find the device on a later frame.
         */
        if (vm->pcm_owner != p) {
            if (vm->pcm_owner == NULL && !r_z(vm, p, F_P_OPEN) &&
                media_dev_wants(vm, p)) {
                (void)media_dev_claim(vm, p);
            }
            if (vm->pcm_owner != p) {
                continue;
            }
        }
        if (vm->port->pcm_free) {
            room = vm->port->pcm_free(vm->port->ud);
            if (room <= 0) {
                continue;       /* device still full: try again next frame */
            }
        }
        (void)pcm_push(vm, p, room);
        if (r_i(vm, p, F_P_PUSHED) >= r_i(vm, p, F_P_FRAMES)) {
            nj_s4 want = r_i(vm, p, F_P_LOOP);
            nj_s4 done = r_i(vm, p, F_P_LOOPDONE) + 1;

            if ((want < 0 || (want > 1 && done < want)) && media_restart(vm, p)) {
                w_i(vm, p, F_P_LOOPDONE, done);
                continue;
            }
        }
        if (r_i(vm, p, F_P_PUSHED) >= r_i(vm, p, F_P_FRAMES)) {
            /*
             * All the samples have been handed over, but the port may still
             * hold the tail in its queue: closing now throws it away, which is
             * how a one second wave came out as a third of a second.  Wait for
             * the queue to empty first, then give the device back and fire
             * END_OF_MEDIA at the time the sound really ended.
             */
            if (vm->port->pcm_queued) {
                if (vm->port->pcm_queued(vm->port->ud) > 0) continue;
            } else if (vm->port->pcm_free && r_i(vm, p, F_P_DEVROOM) > 0 &&
                       vm->port->pcm_free(vm->port->ud) < r_i(vm, p, F_P_DEVROOM)) {
                continue;
            }
            /* Played to the end: give the device back, STOP the Player and say
             * so, which is what MIDP's END_OF_MEDIA is for. */
            if (r_i(vm, p, F_P_KIND) == 1) {
                nj_s4 frames = r_i(vm, p, F_P_FRAMES);
                nj_s4 rate = r_i(vm, p, F_P_RATE);
                if (vm->port->pcm_queued && rate > 0) {
                    w_j(vm, p, F_P_TIME,
                        (nj_s8)frames * 1000000LL / (nj_s8)rate);
                }
                media_dev_release(vm, p);
            }
            w_i(vm, p, F_P_LOOPDONE, 0);
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
    /*
     * ISO base media: the 'ftyp' box is the first one a muxer writes, so its
     * four characters at offset 4 are the container's signature whatever size
     * the box declares.  The old test looked at the box's size byte instead,
     * which named the 20-byte ftyp of the collection's .amr-named MP4s as
     * "an unrecognised container" - 毁灭战士3's whole soundtrack arriving as
     * an unknown format is exactly that mistake.
     */
    if (len >= 8u && b[4] == 'f' && b[5] == 't' && b[6] == 'y' && b[7] == 'p') {
        return "MP4";
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
        /*
         * Reaching this line means neither track could be built, and the
         * message should say which tracks this runtime does play instead of
         * implying video is missing as a whole: H.263 and MPEG-4 Part 2 video
         * and AMR audio are wired into Player and the pump, while AAC audio
         * has no decoder at all and the other video codecs are not claimed by
         * any installed provider.
         */
        return "no playable track in this MP4/3GP: audio must be AMR and video "
               "must be H.263 or MPEG-4 Part 2 (AAC has no decoder)"
               NJ_MEXC_DATA;
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

/*
 * Streaming WAV front-end for Manager.createPlayer(InputStream).
 *
 * The buffered path (read_stream + wav_parse) holds the whole container in a
 * Java byte array while it allocates the PCM array beside it, so a 400 KB track
 * needs 800 KB of heap - that is what makes createPlayer fail on a small heap
 * (test_lib pins it).  Walking the RIFF chunks through the stream instead lets
 * the samples land directly in the PCM array the Player owns: the peak is the
 * PCM array alone, and because the samples are copied (never aliased) the
 * caller's bytes stay private and unchanged, which test_lib also asserts.
 *
 * Only an exact ByteArrayInputStream is streamed, only plain 8/16-bit PCM, and
 * anything unexpected leaves the stream rewound for the buffered path.
 */
static int wav_stream_rewind(nj_vm *vm, nj_ref in)
{
    nj_field *pos = nj_field_lookup(vm, "java/io/ByteArrayInputStream", "pos", "I", 0);

    if (!pos) {
        return 0;
    }
    nj_field_write_int(vm, in, pos, 0);
    return 1;
}

static nj_ref wav_stream_bytes(nj_vm *vm, nj_ref in, nj_u4 want)
{
    nj_ref buf = nj_byte_array_new(vm, want);
    nj_value a[3];
    nj_value r;

    if (!buf) {
        nj_clear_exception(vm);
        return NULL;
    }
    a[0].r = buf;
    a[1].i = 0;
    a[2].i = (nj_s4)want;
    if (nj_call(vm, in, "java/io/InputStream", "read", "([BII)I", a, &r) != NJ_OK ||
        r.i != (nj_s4)want) {
        nj_clear_exception(vm);
        return NULL;
    }
    return buf;
}

static int wav_stream_skip(nj_vm *vm, nj_ref in, nj_u4 n)
{
    nj_u4 done = 0u;

    while (done < n) {
        nj_u4 step = n - done > 512u ? 512u : n - done;
        nj_ref dump = wav_stream_bytes(vm, in, step);

        if (!dump) {
            return 0;
        }
        done += step;
    }
    return 1;
}

/* Returns a rooted PCM byte array (16-bit LE), or NULL after rewinding `in`. */
static nj_ref wav_stream_try(nj_vm *vm, nj_ref in, wav_info *wi)
{
    nj_class *want_cls = nj_class_find(vm, "java/io/ByteArrayInputStream");
    nj_ref hdr, chunk, pcm = NULL;
    nj_u4 data_left = 0u;
    int have_fmt = 0;

    if (!want_cls || !in || in->cls != want_cls) {
        return NULL;
    }
    hdr = wav_stream_bytes(vm, in, 12u);
    if (!hdr) {
        goto fail;
    }
    if (memcmp(hdr->u.bytes, "RIFF", 4) != 0 ||
        memcmp(hdr->u.bytes + 8, "WAVE", 4) != 0) {
        goto fail;
    }
    for (;;) {
        chunk = wav_stream_bytes(vm, in, 8u);
        if (!chunk) {
            goto fail;
        }
        {
            nj_u4 len = (nj_u4)chunk->u.bytes[4] | ((nj_u4)chunk->u.bytes[5] << 8) |
                        ((nj_u4)chunk->u.bytes[6] << 16) | ((nj_u4)chunk->u.bytes[7] << 24);
            if (memcmp(chunk->u.bytes, "data", 4) == 0) {
                data_left = len;
                break;
            }
            if (memcmp(chunk->u.bytes, "fmt ", 4) == 0) {
                nj_u4 take = len > 40u ? 40u : len;
                nj_u4 rest = len - take + (len & 1u);
                nj_ref fmt;

                if (take < 16u) {
                    goto fail;
                }
                fmt = wav_stream_bytes(vm, in, take);
                if (!fmt) {
                    goto fail;
                }
                {
                    const nj_u1 *f = fmt->u.bytes;
                    int tag = (int)f[0] | ((int)f[1] << 8);

                    wi->channels = (int)f[2] | ((int)f[3] << 8);
                    wi->rate = (int)((nj_u4)f[4] | ((nj_u4)f[5] << 8) |
                                     ((nj_u4)f[6] << 16) | ((nj_u4)f[7] << 24));
                    wi->bits = (int)f[14] | ((int)f[15] << 8);
                    if (tag != 1 || (wi->bits != 8 && wi->bits != 16) ||
                        wi->channels < 1 || wi->channels > 2 || wi->rate <= 0) {
                        goto fail;
                    }
                }
                have_fmt = 1;
                if (rest != 0u && !wav_stream_skip(vm, in, rest)) {
                    goto fail;
                }
                continue;
            }
            if (!wav_stream_skip(vm, in, len + (len & 1u))) {
                goto fail;
            }
        }
    }
    if (!have_fmt || data_left == 0u) {
        goto fail;
    }
    {
        nj_u4 align = (nj_u4)wi->channels * (nj_u4)(wi->bits / 8);
        nj_u4 total = data_left - (data_left % align);
        nj_u4 want = (total / (nj_u4)(wi->bits / 8)) * 2u;   /* 16-bit PCM */
        nj_u4 done = 0u;

        wi->frames = total / align;
        pcm = nj_byte_array_new(vm, want);
        if (!pcm) {
            /*
             * The buffered path let the allocator collect on its way to a
             * 400 KB copy; here the PCM array is the first big allocation, so
             * ask for the collection ourselves before giving up.
             */
            nj_clear_exception(vm);
            (void)nj_gc_run(vm);
            pcm = nj_byte_array_new(vm, want);
            if (!pcm) {
                nj_clear_exception(vm);
                goto fail;
            }
        }
        pcm = nj_root(vm, pcm);
        if (wi->bits == 16) {
            while (done < want) {
                nj_u4 step = want - done > 1024u ? 1024u : want - done;
                nj_value a[3];
                nj_value r;

                a[0].r = pcm;
                a[1].i = (nj_s4)done;
                a[2].i = (nj_s4)step;
                if (nj_call(vm, in, "java/io/InputStream", "read", "([BII)I", a, &r) != NJ_OK ||
                    r.i != (nj_s4)step) {
                    nj_clear_exception(vm);
                    goto fail;
                }
                done += step;
            }
        } else {
            nj_ref tmp = nj_byte_array_new(vm, 1024u);

            if (!tmp) {
                nj_clear_exception(vm);
                goto fail;
            }
            while (done < total) {
                nj_u4 step = total - done > 1024u ? 1024u : total - done;
                nj_value a[3];
                nj_value r;
                nj_u4 k;

                a[0].r = tmp;
                a[1].i = 0;
                a[2].i = (nj_s4)step;
                if (nj_call(vm, in, "java/io/InputStream", "read", "([BII)I", a, &r) != NJ_OK ||
                    r.i != (nj_s4)step) {
                    nj_clear_exception(vm);
                    goto fail;
                }
                for (k = 0u; k < step; k++) {
                    nj_s4 v = ((nj_s4)tmp->u.bytes[k] - 128) * 256;

                    pcm->u.bytes[((done + k) * 2u)] = (nj_u1)(v & 0xFF);
                    pcm->u.bytes[((done + k) * 2u) + 1u] = (nj_u1)((v >> 8) & 0xFF);
                }
                done += step;
            }
            wi->bits = 16;
        }
        wi->data = NULL;
        wi->bytes = want;
        return pcm;
    }
fail:
    (void)wav_stream_rewind(vm, in);
    return NULL;
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
    memset(&wi, 0, sizeof wi);
    /*
     * Plain PCM in a ByteArrayInputStream does not have to be buffered: walk the
     * RIFF chunks through the stream and let the samples land in the PCM array
     * the Player owns.  test_lib pins the failure this avoids (a 400 KB track
     * needing 800 KB of heap) and the two contracts it must keep (the caller's
     * array stays private and unaliased).
     */
    {
    nj_ref spcm = wav_stream_try(vm, in, &wi);

    if (spcm) {
        p = player_new(vm, 1);
        if (!p) {
            nj_clear_exception(vm);
            (void)nj_gc_run(vm);
            p = player_new(vm, 1);
        }
        if (!p) {
            nj_root_release(vm, mark);
            return NJ_ERR;
        }
        p = nj_root(vm, p);
        player_set_type(vm, p, "audio/x-wav");
        w_r(vm, p, F_P_DATA, "[B", spcm);
        w_i(vm, p, F_P_RATE, wi.rate);
        w_i(vm, p, F_P_CHAN, wi.channels);
        w_i(vm, p, F_P_FRAMES, (nj_s4)wi.frames);
        w_j(vm, p, F_P_DURATION,
            (nj_s8)wi.frames * 1000000LL / (nj_s8)wi.rate);
        nj_root_release(vm, mark);
        ret->r = p;
        return NJ_OK;
    }
    }
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
                    /*
                     * The renderer re-opens the song from these bytes every
                     * time the loop restarts, and it holds only a raw pointer:
                     * the Player is what keeps the array in the heap.  Without
                     * this the collector may free it between two loops and the
                     * restart then parses whatever took its place - the music
                     * stops after a few passes instead of looping forever
                     * (极品飞车14之热力追踪 loses its race track music this way).
                     */
                    w_r(vm, p, F_P_SRC, "[B", data);
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
#if NJ_ENABLE_MP3
        /*
         * MP3 through the nj_avcodec seam: the same shape as MIDI below (a
         * holder holds the decoder and a scratch buffer, and the pump pulls
         * one frame's worth of PCM at a time), because PCM cannot live in the
         * Java heap: three minutes of 44.1 kHz stereo is 21 MB.  The file's
         * own bytes stay in F_P_SRC for the decoder to read.
         */
        if (strcmp(what, "MP3") == 0) {
            const nj_av_audio_ops *ops = nj_avcodec_find_audio(
                vm, barray_bytes(data), barray_len(data));

            if (ops != NULL) {
                nj_av_mem memd;
                nj_av_info info;
                const char *err = NULL;
                nj_av_dec *dec;

                memd.ud = vm;
                memd.alloc = av_alloc;
                memd.free = av_free;
                memset(&info, 0, sizeof(info));
                dec = ops->open(barray_bytes(data), barray_len(data), &memd,
                                &info, &err);
                if (dec != NULL && info.rate > 0u && info.channels > 0u) {
                    nj_ref holder = nj_byte_array_new(vm, NJ_MEDIA_STREAM_BYTES);

                    if (holder != NULL) {
                        media_stream_state st;

                        memset(&st, 0, sizeof(st));
                        st.aops = ops;
                        st.adec = dec;
                        st.in_off = 0u;
                        memcpy(holder->u.bytes, &st, sizeof(st));
                        w_r(vm, p, F_P_DATA, "[B", holder);
                        w_r(vm, p, F_P_SRC, "[B", data);
                        w_z(vm, p, F_P_STREAM, 1);
                        w_z(vm, p, F_P_AVK, 1);
                        w_i(vm, p, F_P_KIND, 1);
                        w_i(vm, p, F_P_RATE, (nj_s4)info.rate);
                        w_i(vm, p, F_P_CHAN, (nj_s4)info.channels);
                        w_i(vm, p, F_P_FRAMES, (nj_s4)info.frames);
                        w_j(vm, p, F_P_DURATION,
                            info.frames > 0u
                                ? (nj_s8)info.frames * 1000000LL / (nj_s8)info.rate
                                : (info.duration_ms > 0u
                                       ? (nj_s8)info.duration_ms * 1000LL : -1));
                        player_set_type(vm, p, "audio/mpeg");
                        nj_logf(vm, 3, "media: %s decoded %s, %u frames",
                                ops->name, what, (unsigned)info.frames);
                        nj_root_release(vm, mark);
                        ret->r = p;
                        return NJ_OK;
                    }
                    ops->close(dec);
                }
            }
        }
#endif /* NJ_ENABLE_MP3 */
#if NJ_ENABLE_VIDEO
        /*
         * A 3GP/MP4 movie: nj_mp4 finds the video track and hands the elementary
         * stream to whatever video provider is installed, and the Player keeps
         * both plus one RGB565 frame.  The pump below decodes into that frame
         * one sample per emulated frame; nothing allocates while a title paints.
         */
        if (strcmp(what, "MP4") == 0) {
            nj_mp4 m;

            if (nj_mp4_open(&m, barray_bytes(data), barray_len(data)) == NJ_MP4_OK &&
                m.width > 0u && m.height > 0u && m.sample_count > 0u) {
                /*
                 * The providers decide by looking at the container: H.263
                 * claims s263/H263 sample entries, MPEG-4 Part 2 claims mp4v
                 * after parsing its VOL.  A codec neither provider accepts
                 * falls through to the refusal at the end of createPlayer().
                 */
                const nj_av_video_ops *vops = nj_avcodec_find_video(
                    vm, barray_bytes(data), barray_len(data));

                if (vops != NULL) {
                    nj_av_mem *mem = av_mem_new(vm);
                    nj_av_info info;
                    const char *err = NULL;
                    nj_av_dec *dec = NULL;

                    memset(&info, 0, sizeof(info));
                    if (mem != NULL) {
                        dec = vops->open(barray_bytes(data), barray_len(data),
                                         mem, &info, &err);
                    }
                    if (dec != NULL) {
                        nj_u4 w = info.width > 0u ? info.width : m.width;
                        nj_u4 h = info.height > 0u ? info.height : m.height;
                        nj_ref holder = nj_byte_array_new(vm, NJ_MEDIA_VIDEO_BYTES);
                        nj_ref vbuf = nj_byte_array_new(vm, w * h * 2u);

                        if (holder != NULL && vbuf != NULL) {
                            media_video_state st;

                            memset(&st, 0, sizeof(st));
                            st.m = m;
                            st.vdec = dec;
                            st.vops = vops;
                            st.mem = mem;
#if NJ_ENABLE_AMR
                            /*
                             * The movie's own sound track, if it has a usable
                             * AMR-NB one.  Not decoding it yet: a title that
                             * never starts the Player should not pay for the
                             * PCM (up to ~300 KB for the corpus clips).
                             */
                            if (nj_mp4_open_audio(&st.am, barray_bytes(data),
                                                  barray_len(data)) ==
                                    NJ_MP4_OK &&
                                st.am.codec == NJ_MP4_CODEC_SAMR &&
                                st.am.sample_count > 0u) {
                                st.has_audio = 1;
                            }
#endif
                            memcpy(holder->u.bytes, &st, sizeof(st));
                            w_r(vm, p, F_P_DATA, "[B", holder);
                            w_r(vm, p, F_P_SRC, "[B", data);
                            w_r(vm, p, F_P_VBUF, "[B", vbuf);
                            w_z(vm, p, F_P_STREAM, 1);
                            w_i(vm, p, F_P_KIND, 2);
                            w_i(vm, p, F_P_VW, (nj_s4)w);
                            w_i(vm, p, F_P_VH, (nj_s4)h);
                            w_i(vm, p, F_P_RATE, (nj_s4)m.fps);
                            w_i(vm, p, F_P_FRAMES, (nj_s4)m.sample_count);
                            w_j(vm, p, F_P_DURATION,
                                (m.timescale > 0u && m.duration > 0u)
                                    ? (nj_s8)m.duration * 1000000LL /
                                          (nj_s8)m.timescale
                                    : -1);
                            player_set_type(vm, p, "video/3gpp");
                            nj_logf(vm, 3, "media: %s movie %ux%u, %u samples",
                                    vops->name, (unsigned)w, (unsigned)h,
                                    (unsigned)m.sample_count);
                            nj_root_release(vm, mark);
                            ret->r = p;
                            return NJ_OK;
                        }
                        vops->close(dec);
                        nj_host_free(vm, mem);
                    }
                }
            }
        }
#endif /* NJ_ENABLE_VIDEO */
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
        /*
         * Both layouts: the bare "#!AMR\n" stream, and the same 20 ms frames
         * inside an ISO base media (MP4/3GP) 'samr' track.  Everything after
         * the frame count is identical - size the array once, decode into it -
         * because the container wrapper answers the same shape as the stream.
         *
         * 毁灭战士3 names its theme "snd/d3theme.amr" and puts an MP4 under
         * that name, as do several other titles: the sniffer looks at the ftyp
         * box, so they arrive here as "MP4", not as a broken AMR stream.
         */
        if (strcmp(what, "AMR") == 0 || strcmp(what, "MP4") == 0) {
            const int in_mp4 = (strcmp(what, "MP4") == 0);
            const nj_u1 *src = barray_bytes(data);
            nj_u4 src_len = barray_len(data);
            /*
             * A bare AMR stream goes through nj_avcodec first, so a port with a
             * hardware AMR decoder displaces the software one exactly as it
             * does for MP3.  The wrapped 'samr' track keeps the direct call
             * below: the provider sees an elementary stream, not a container,
             * and unpacking the track is nj_mp4's job, not the codec's.
             */
            if (!in_mp4) {
                const nj_av_audio_ops *aops =
                    nj_avcodec_find_audio(vm, src, src_len);

                if (aops != NULL) {
                    nj_av_mem memd;
                    nj_av_info info;
                    const char *err = NULL;
                    nj_av_dec *dec;

                    memd.ud = vm;
                    memd.alloc = av_alloc;
                    memd.free = av_free;
                    memset(&info, 0, sizeof(info));
                    dec = aops->open(src, src_len, &memd, &info, &err);
                    if (dec != NULL) {
                        nj_u4 want = info.frames * info.channels * 2u;

                        if (info.rate > 0u && info.channels > 0u && want > 0u) {
                            nj_ref apcm = nj_byte_array_new(vm, want);

                            if (apcm != NULL) {
                                nj_av_step step;

                                memset(&step, 0, sizeof(step));
                                step.out = apcm->u.bytes;
                                step.out_cap = want;
                                if (aops->decode(dec, &step) == NJ_AV_OK &&
                                    step.out_used == want) {
                                    w_r(vm, p, F_P_DATA, "[B", apcm);
                                    w_i(vm, p, F_P_RATE, (nj_s4)info.rate);
                                    w_i(vm, p, F_P_CHAN, (nj_s4)info.channels);
                                    w_i(vm, p, F_P_FRAMES, (nj_s4)info.frames);
                                    w_j(vm, p, F_P_DURATION,
                                        (nj_s8)info.frames * 1000000LL /
                                        (nj_s8)info.rate);
                                    aops->close(dec);
                                    nj_logf(vm, 3, "media: %s decoded %u AMR "
                                                   "frames into %u PCM bytes",
                                            aops->name,
                                            (unsigned)info.frames,
                                            (unsigned)want);
                                    nj_root_release(vm, mark);
                                    ret->r = p;
                                    return NJ_OK;
                                }
                            }
                        }
                        aops->close(dec);
                    }
                    if (nj_has_exception(vm)) {
                        nj_clear_exception(vm);     /* the direct path follows */
                    }
                }
            }

            nj_u4 aframes = in_mp4 ? nj_amr_mp4_frames(src, src_len)
                                   : nj_amr_frames(src, src_len);
            nj_ref pcm = NULL;

            if (aframes > 0u) {
                pcm = nj_byte_array_new(vm,
                                        aframes * (nj_u4)NJ_AMR_BLOCK * 2u);
            }
            if (pcm != NULL) {
                nj_u4 decoded =
                    in_mp4 ? nj_amr_mp4_decode(src, src_len, pcm->u.bytes)
                           : nj_amr_decode(src, src_len, pcm->u.bytes);

                if (decoded == aframes) {
                    w_r(vm, p, F_P_DATA, "[B", pcm);
                    w_i(vm, p, F_P_RATE, (nj_s4)NJ_AMR_RATE);
                    w_i(vm, p, F_P_CHAN, 1);
                    w_i(vm, p, F_P_FRAMES, (nj_s4)(aframes * NJ_AMR_BLOCK));
                    w_j(vm, p, F_P_DURATION,
                        (nj_s8)(aframes * NJ_AMR_BLOCK) * 1000000LL /
                        (nj_s8)NJ_AMR_RATE);
                    nj_logf(vm, 3, "media: decoded %u AMR frames from %s into "
                                   "%u PCM bytes",
                            (unsigned)aframes,
                            in_mp4 ? "an MP4 track" : "a stream",
                            (unsigned)(aframes * (nj_u4)NJ_AMR_BLOCK * 2u));
                    nj_root_release(vm, mark);
                    ret->r = p;
                    return NJ_OK;
                }
            }
            /*
             * Nothing to play, or no room for the samples: a silent Player,
             * for the reason the MIDI branch above gives.  The byte array's
             * failure has already raised OutOfMemoryError, so drop that rather
             * than failing createPlayer().
             *
             * An MP4 with no readable AMR track is the exception: that is a
             * container this runtime has no decoder for, and the refusal below
             * says which one instead of presenting silence as success.  A bare
             * AMR file, by contrast, is a sound effect that happens to be
             * empty, and silence is the honest answer for it.
             */
            if (in_mp4 && aframes == 0u) {
                if (nj_has_exception(vm)) {
                    nj_clear_exception(vm);
                }
                nj_logf(vm, 3, "media: no AMR narrowband track in this MP4");
            } else {
                if (nj_has_exception(vm)) {
                    nj_clear_exception(vm);
                }
                w_i(vm, p, F_P_KIND, 0);
                nj_logf(vm, 3, "media: silent Player for %u bytes of %s "
                               "(no narrowband frame)",
                        (unsigned)barray_len(data), what);
                nj_root_release(vm, mark);
                ret->r = p;
                return NJ_OK;
            }
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
    if (wi.adpcm) {
        /*
         * IMA ADPCM is decoded once, here, exactly like MIDI is rendered once:
         * after this the Player holds 16-bit PCM and the pump, the port hook
         * and the duration maths know nothing about blocks or nibbles.
         */
        nj_u4 want = wi.frames * (nj_u4)wi.channels * 2u;
        nj_ref pcm = nj_byte_array_new(vm, want);
        nj_u4 got;

        if (!pcm) {
            nj_root_release(vm, mark);
            return NJ_ERR;
        }
        got = wav_ima_decode(wi.data, wi.bytes, wi.channels, wi.block,
                             pcm->u.bytes, want);
        if (got == 0u) {
            /* A block-aligned chunk with no playable block: stay silent
             * rather than hand the port a byte array of zeroes. */
            nj_logf(vm, 3, "media: IMA ADPCM blocks decoded to no frames");
            nj_root_release(vm, mark);
            return nj_throw(vm, NJ_C_MEXC, "this WAV file holds no PCM samples");
        }
        pcm->length = got * (nj_u4)wi.channels * 2u;
        wi.frames = got;
        wi.bits = 16;
        nj_logf(vm, 3, "media: decoded IMA ADPCM into %u PCM bytes",
                (unsigned)pcm->length);
        w_r(vm, p, F_P_DATA, "[B", pcm);
    } else {
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
    /*
     * Everything this runtime's Players really decode.  It used to answer the
     * single string "audio/x-wav", which was already stale when MIDI, AMR,
     * MP3 and AMR-inside-MP4 worked, and a title scans this list to decide
     * what it may ask for: Doom 3 looks for a "video" type here before it
     * offers its intro at all.  The protocol argument is not used because the
     * same set is reachable through every protocol this runtime has.
     *
     * Every string is rooted while the array is built: a collection between
     * nj_array_new() and the last store would otherwise recycle the strings
     * that are still only C locals, and the caller would hold refs to freed
     * blocks (the same failure media_createPlayer_stream() documents).
     */
    static const char *types[] = {
        "audio/x-wav", "audio/midi", "audio/amr", "audio/amr-wb",
        "audio/mpeg", "video/3gpp"
    };
    nj_u4 mark = nj_root_mark(vm);
    nj_ref a = nj_array_new(vm, "java/lang/String",
                            (nj_u4)(sizeof(types) / sizeof(types[0])));
    nj_u4 i;

    if (!a) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    nj_root(vm, a);
    for (i = 0u; i < (nj_u4)(sizeof(types) / sizeof(types[0])); i++) {
        nj_ref s = nj_string_new(vm, types[i]);

        if (!s) {
            nj_root_release(vm, mark);
            return NJ_ERR;
        }
        a->u.refs[i] = s;
    }
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
    /*
     * A platform MIDI Player that was deallocated gets its song back here.
     * deallocate() releases the port's handle because MIDP says it must free
     * scarce resources, but the song bytes stay in F_P_DATA, and a title that
     * keeps its Player object across a pause expects realize() to re-acquire
     * the synthesizer.  Without this the Player falls through to the sampled
     * path on start() and plays its MIDI bytes as if they were PCM: the
     * picture comes back but the music never does.
     *
     * Only a Player that the platform itself took at creation qualifies:
     * a rendered MIDI Player (F_P_STREAM) keeps its own stream state instead
     * and never reaches here without F_P_STREAM set.
     */
    if (!r_z(vm, self, F_P_PLAT) && r_z(vm, self, F_P_MIDIK) &&
        !r_z(vm, self, F_P_STREAM) && vm->port && vm->port->midi_open) {
        nj_ref song = r_r(vm, self, F_P_DATA, "[B");
        if (song != NULL && barray_len(song) > 0u) {
            nj_u4 dur = 0;
            void *mh = vm->port->midi_open(vm->port->ud,
                                           barray_bytes(song),
                                           barray_len(song), &dur);
            if (mh != NULL && midi_handle_set(vm, self, mh)) {
                w_z(vm, self, F_P_PLAT, 1);
                w_z(vm, self, F_P_OPEN, 0);
                w_i(vm, self, F_P_KIND, 1);
                if (dur > 0u) {
                    w_j(vm, self, F_P_DURATION, (nj_s8)dur * 1000LL);
                }
            } else if (mh != NULL) {
                if (vm->port->midi_close) {
                    vm->port->midi_close(vm->port->ud, mh);
                }
                /*
                 * midi_handle_set() only fails when the heap could not take
                 * the handle.  createPlayer() stays silent in that case
                 * instead of surfacing the error; do the same here, so a
                 * title resuming its music is not interrupted by an
                 * OutOfMemoryError from realize().
                 */
                if (nj_has_exception(vm)) {
                    nj_clear_exception(vm);
                }
            }
        }
    }
    w_i(vm, self, F_P_STATE, 200);
    return NJ_OK;
}

NJFN(player_prefetch)
{
    nj_ref self = NJ_THIS(args);

    /* prefetch() is idempotent, including while playback is already active. */
    if (r_i(vm, self, F_P_STATE) >= 300) {
        return NJ_OK;
    }
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
             * The field holds int pairs (frequency, duration) so that a
             * ToneControl sequence survives the arena; the port asks for
             * nj_audio_tone, which is two 16-bit fields.  Casting the int
             * array looked plausible and was wrong: on a little-endian host
             * the port read 440 Hz and a duration of 0, and the tone player
             * went silent.  Copy the pairs, and cap what one call can carry.
             */
            nj_audio_tone seq[64];
            const nj_s4 *pairs = (const nj_s4 *)(const void *)tones->u.bytes;
            int i;

            if (n > (int)(sizeof(seq) / sizeof(seq[0]))) {
                n = (int)(sizeof(seq) / sizeof(seq[0]));
            }
            for (i = 0; i < n; i++) {
                seq[i].freq_hz = (nj_u2)pairs[i * 2];
                seq[i].duration_ms = (nj_u2)pairs[i * 2 + 1];
            }
            (void)vm->port->tone_play(vm->port->ud, seq, n);
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
        } else {
            return nj_throw(vm, NJ_C_MEXC, "MIDI output unavailable");
        }
    } else if (r_i(vm, self, F_P_KIND) == 2) {
        /*
         * A movie: there is no audio device to open here - the track's sound
         * is a separate Player - and the media frame pump decodes it against
         * its own sample clock while the state is STARTED.  It has to be on
         * the registry for that pump to run, which is all start() does.
         */
        w_i(vm, self, F_P_LOOPDONE, 0);
#if NJ_ENABLE_VIDEO
        if (!video_decoder_reopen(vm, self)) {
            /* The decoder could not be rebuilt (deallocate() released it and
             * the provider refused a fresh open): report it instead of
             * starting a Player that can never show a frame. */
            return nj_throw(vm, NJ_C_MEXC, "movie decoder is not available");
        }
        video_set_start(vm, self);
        video_audio_start(vm, self);
#if NJ_ENABLE_AMR
        /* A seek made before start() moved the picture; the sound track has
         * to resume from the same place instead of replaying from sample 0. */
        if (r_i(vm, self, F_P_APUSH) == 0 && r_i(vm, self, F_P_PUSHED) > 0 &&
            r_i(vm, self, F_P_RATE) > 0) {
            w_i(vm, self, F_P_APUSH,
                (nj_s4)((nj_s8)r_i(vm, self, F_P_PUSHED) * NJ_AMR_RATE /
                        r_i(vm, self, F_P_RATE)));
        }
#endif
#endif
        (void)media_register(vm, self);
    } else if (vm->port && vm->port->pcm_open) {
        /* MIDP restarts a completed Player at the beginning. A paused one
         * keeps its position; explicit seeks below also update pPushed. */
        if (r_i(vm, self, F_P_FRAMES) > 0 &&
            r_i(vm, self, F_P_PUSHED) >= r_i(vm, self, F_P_FRAMES)) {
            if (!media_restart(vm, self)) {
                return nj_throw(vm, NJ_C_MEXC, "audio decoder cannot restart");
            }
            w_i(vm, self, F_P_LOOPDONE, 0);
        }
        /* Ask for the device; whatever was playing sampled audio before is
         * paused, not closed, and gets it back when this Player ends. */
        if (!r_z(vm, self, F_P_OPEN)) {
            (void)media_dev_claim(vm, self);
        }
        if (vm->pcm_owner == self) {
            (void)pcm_push(vm, self, 1 << 20);
        }
        /* Even a device that accepts the whole clip immediately needs the
         * frame pump to finish the Player, close PCM and fire endOfMedia.
         * Otherwise short clips remain STARTED forever; a Player that could
         * not get the device yet is registered too, so the pump can hand it
         * the device the moment whoever has it ends. */
        (void)media_register(vm, self);
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
        } else if (r_i(vm, self, F_P_KIND) == 2) {
#if NJ_ENABLE_VIDEO
            video_audio_stop(vm, self);
#endif
        } else if (r_z(vm, self, F_P_OPEN) || vm->pcm_owner == self) {
            /* media_dev_close() moves the position back to what was heard
             * before it throws the device queue away; doing it here too would
             * subtract the queue twice. */
            media_dev_release(vm, self);
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
#if NJ_ENABLE_VIDEO
    video_decoder_release(vm, self);
#endif
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
    } else if (n == 12 && c[0] == 'V' && c[1] == 'i' && c[2] == 'd'
               && c[3] == 'e' && c[4] == 'o') {
        /*
         * VideoControl only on a Player that really has a video track: a game
         * that gets one for an audio Player would call initDisplayMode() on a
         * movie that does not exist.  Doom 3 and 1942空战 both checkcast the
         * result without a null test, which is legal - but with a real control
         * they play their intro instead of skipping it.
         */
        if (r_i(vm, self, F_P_KIND) == 2) {
            cls = NJ_C_VIDEOC;
        }
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
    if (r_i(vm, self, F_P_KIND) == 2) {
        n++;                /* VolumeControl and VideoControl */
    }
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
    if (r_i(vm, self, F_P_KIND) == 2) {
        a1[0].r = nj_string_new(vm, "VideoControl");
        r.r = NULL;
        if (nj_call(vm, self, NJ_C_PLAYER, "getControl",
                    "(Ljava/lang/String;)Ljavax/microedition/media/Control;",
                    a1, &r) == NJ_OK && r.r != NULL) {
            arr->u.refs[at++] = r.r;
        }
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

NJFN(player_getTimeBase)
{
    ret->r = r_r(vm, NJ_THIS(args), F_P_MASTER, "L" NJ_C_TIMEBASE ";");
    return NJ_OK;
}

NJFN(player_setTimeBase)
{
    nj_ref tb = NJ_ARG_REF(args, 0);

    /* JSR 135: a player without a master clock is legal (getTimeBase()
     * returns null), but it refuses a null master. */
    if (!tb) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "a player's time base must not be null");
        return NJ_ERR;
    }
    w_r(vm, NJ_THIS(args), F_P_MASTER, "L" NJ_C_TIMEBASE ";", tb);
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
    if (vm->pcm_owner == self && r_i(vm, self, F_P_KIND) == 1) {
        nj_s4 rate = r_i(vm, self, F_P_RATE);
        if (rate > 0) {
            w_j(vm, self, F_P_TIME,
                (nj_s8)media_played(vm, self, NULL) * 1000000LL / rate);
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
    }
#if NJ_ENABLE_VIDEO
    else if (r_i(vm, self, F_P_KIND) == 2) {
        /*
         * A movie decodes its way to the new position, because a P-VOP
         * references the previous picture and the samples have no index to
         * jump by.  This has to come before the streamed-MIDI branch below:
         * a movie Player also sets F_P_STREAM, to say "pData is not PCM", and
         * treating its holder as a MIDI stream dereferenced that state and
         * crashed.
         */
        want = video_seek(vm, self, want);
    }
#endif
#if NJ_ENABLE_MP3
    else if (r_z(vm, self, F_P_AVK)) {
        nj_ref data = r_r(vm, self, F_P_DATA, "[B");
        media_stream_state state;
        nj_s4 rate = r_i(vm, self, F_P_RATE);

        if (data == NULL || barray_len(data) < NJ_MEDIA_STREAM_BYTES) {
            return nj_throw(vm, NJ_C_MEXC, "missing audio decoder");
        }
        memcpy(&state, barray_bytes(data), sizeof(state));
        if (state.aops == NULL || state.adec == NULL || state.aops->seek == NULL ||
            state.aops->seek(state.adec, want) != NJ_AV_OK) {
            return nj_throw(vm, NJ_C_MEXC, "audio decoder cannot seek");
        }
        state.pending = state.offset = 0u;
        state.in_off = barray_len(r_r(vm, self, F_P_SRC, "[B"));
        memcpy(data->u.bytes, &state, sizeof(state));
        w_i(vm, self, F_P_PUSHED,
            (nj_s4)((nj_u8)want * (nj_u8)rate / 1000000u));
    }
#endif
    else if (r_z(vm, self, F_P_STREAM)) {
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
                media_stream_state state;
                memcpy(&state, barray_bytes(data), sizeof(state));
                state.pending = state.offset = 0u;
                memcpy(data->u.bytes, &state, sizeof(state));
                w_i(vm, self, F_P_PUSHED, (nj_s4)got);
                if (rate > 0) {
                    want = (nj_s8)got * 1000000LL / (nj_s8)rate;
                }
            }
        }
#endif /* NJ_ENABLE_MIDI */
    }
    else if (r_i(vm, self, F_P_KIND) == 1) {
        nj_s4 rate = r_i(vm, self, F_P_RATE);
        nj_s4 frames = r_i(vm, self, F_P_FRAMES);
        if (rate > 0 && frames >= 0) {
            nj_s8 end = (nj_s8)frames * 1000000 / rate;
            nj_s8 frame = want >= end ? frames :
                          want / 1000000 * rate + (want % 1000000) * rate / 1000000;
            w_i(vm, self, F_P_PUSHED, (nj_s4)frame);
            want = frame * 1000000 / rate;
        }
    }
    w_j(vm, self, F_P_TIME, want);
    if (vm->pcm_owner == self && r_i(vm, self, F_P_KIND) == 1) {
        /* The successful seek replaced both the decoder position and its
         * scratch.  Discard the old device queue without syncing it back over
         * the new position; an idle or displaced Player owns no queue. */
        media_dev_close(vm, 0);
        (void)media_dev_claim(vm, self);
    }
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
    /* pData becomes the renderer handle below; the song bytes the renderer
     * re-reads on every loop restart have to stay reachable somewhere, so the
     * Player keeps them where the other streamed codecs keep theirs. */
    w_r(vm, player, F_P_SRC, "[B", data);
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
            { NJ_C_MANAGER, "TONE_CONSTANT_DURATION", 4 },
            /* VideoControl's display modes: a MIDlet compares the mode it
               passed to initDisplayMode() against these. */
            { NJ_C_VIDEOC, "USE_DIRECT_VIDEO", 1 },
            { NJ_C_VIDEOC, "USE_GUI_PRIMITIVE", 0 }
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
/* Natives: VideoControl                                               */
/* ------------------------------------------------------------------ */
/*
 * The display state lives on the Player (pV* fields): there is exactly one
 * display per video Player, and the frame-boundary overlay reads it directly.
 * The control object is a thin owner handle, the same shape VolumeControl has.
 */
NJFN(videoctl_initDisplayMode)
{
    nj_ref self = NJ_THIS(args);
    nj_ref player = ctl_owner(vm, self);
    nj_s4 mode = NJ_ARG_INT(args, 0);
    nj_ref target = NJ_ARG_REF(args, 1);

    if (!player) {
        return nj_throw(vm, "java/lang/IllegalStateException",
                        "the control has no Player");
    }
    if (mode != 1) {
        /* 0 is USE_GUI_PRIMITIVE, which would need a host GUI toolkit to hand
           back a video component; this runtime renders into the MIDlet's own
           Canvas only, so say so instead of returning a component that can
           never show a frame. */
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "only USE_DIRECT_VIDEO is supported");
    }
    if (!target) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "USE_DIRECT_VIDEO needs a Canvas");
    }
    if (r_i(vm, player, F_P_STATE) < 200) {
        return nj_throw(vm, "java/lang/IllegalStateException",
                        "realize() the Player before initDisplayMode()");
    }
    w_r(vm, player, F_P_VCANVAS, "Ljavax/microedition/lcdui/Canvas;", target);
    w_i(vm, player, F_P_VDMODE, 1);
    w_z(vm, player, F_P_VVIS, 0);
    w_z(vm, player, F_P_VFULL, 0);
    w_i(vm, player, F_P_VDX, 0);
    w_i(vm, player, F_P_VDY, 0);
    w_i(vm, player, F_P_VDW, 0);
    w_i(vm, player, F_P_VDH, 0);
    ret->r = NULL;              /* USE_DIRECT_VIDEO returns null by the spec */
    return NJ_OK;
}

/* Common checks for the display setters: the mode must be direct video. */
static nj_ref videoctl_display_player(nj_vm *vm, nj_ref ctl)
{
    nj_ref player = ctl_owner(vm, ctl);

    if (!player) {
        return NULL;
    }
    if (r_i(vm, player, F_P_VDMODE) != 1) {
        return NULL;
    }
    return player;
}

NJFN(videoctl_setDisplayLocation)
{
    nj_ref player = videoctl_display_player(vm, NJ_THIS(args));

    if (!player) {
        return nj_throw(vm, "java/lang/IllegalStateException",
                        "initDisplayMode() has not been called");
    }
    w_i(vm, player, F_P_VDX, NJ_ARG_INT(args, 0));
    w_i(vm, player, F_P_VDY, NJ_ARG_INT(args, 1));
    return NJ_OK;
}

NJFN(videoctl_setDisplaySize)
{
    nj_ref player = videoctl_display_player(vm, NJ_THIS(args));
    nj_s4 w = NJ_ARG_INT(args, 0);
    nj_s4 h = NJ_ARG_INT(args, 1);

    if (!player) {
        return nj_throw(vm, "java/lang/IllegalStateException",
                        "initDisplayMode() has not been called");
    }
    if (w <= 0 || h <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "display size must be positive");
    }
    w_i(vm, player, F_P_VDW, w);
    w_i(vm, player, F_P_VDH, h);
    w_z(vm, player, F_P_VFULL, 0);
    return NJ_OK;
}

NJFN(videoctl_setDisplayFullScreen)
{
    nj_ref player = videoctl_display_player(vm, NJ_THIS(args));

    if (!player) {
        return nj_throw(vm, "java/lang/IllegalStateException",
                        "initDisplayMode() has not been called");
    }
    w_z(vm, player, F_P_VFULL, NJ_ARG_INT(args, 0) ? 1 : 0);
    return NJ_OK;
}

NJFN(videoctl_setVisible)
{
    nj_ref player = videoctl_display_player(vm, NJ_THIS(args));

    if (!player) {
        return nj_throw(vm, "java/lang/IllegalStateException",
                        "initDisplayMode() has not been called");
    }
    w_z(vm, player, F_P_VVIS, NJ_ARG_INT(args, 0) ? 1 : 0);
    return NJ_OK;
}

/* The effective display size: what the overlay will cover, 0 when not set. */
static nj_s4 videoctl_display_w(nj_vm *vm, nj_ref player)
{
    if (r_z(vm, player, F_P_VFULL)) {
        nj_surface *scr = nj_midp_screen(vm);
        return scr ? (nj_s4)scr->width : 0;
    }
    {
        nj_s4 w = r_i(vm, player, F_P_VDW);
        return w > 0 ? w : r_i(vm, player, F_P_VW);
    }
}

static nj_s4 videoctl_display_h(nj_vm *vm, nj_ref player)
{
    if (r_z(vm, player, F_P_VFULL)) {
        nj_surface *scr = nj_midp_screen(vm);
        return scr ? (nj_s4)scr->height : 0;
    }
    {
        nj_s4 h = r_i(vm, player, F_P_VDH);
        return h > 0 ? h : r_i(vm, player, F_P_VH);
    }
}

NJFN(videoctl_getDisplayWidth)
{
    nj_ref self = NJ_THIS(args);
    nj_ref player = videoctl_display_player(vm, self);
    ret->i = player ? videoctl_display_w(vm, player) : 0;
    return NJ_OK;
}

NJFN(videoctl_getDisplayHeight)
{
    nj_ref self = NJ_THIS(args);
    nj_ref player = videoctl_display_player(vm, self);
    ret->i = player ? videoctl_display_h(vm, player) : 0;
    return NJ_OK;
}

NJFN(videoctl_getSourceWidth)
{
    nj_ref player = ctl_owner(vm, NJ_THIS(args));
    ret->i = player ? r_i(vm, player, F_P_VW) : 0;
    return NJ_OK;
}

NJFN(videoctl_getSourceHeight)
{
    nj_ref player = ctl_owner(vm, NJ_THIS(args));
    ret->i = player ? r_i(vm, player, F_P_VH) : 0;
    return NJ_OK;
}

/*
 * getSnapshot("RGB888") hands back the Player's current frame as a raw byte
 * array.  The encoding string is compared the way getControl() compares names;
 * anything but RGB888 is refused rather than returning bytes in the wrong
 * layout.  video.snapshot.encodings advertises exactly this one.
 */
NJFN(videoctl_getSnapshot)
{
    nj_ref self = NJ_THIS(args);
    nj_ref player = ctl_owner(vm, self);
    nj_ref enc = NJ_ARG_REF(args, 0);
    nj_ref vb, out;
    const nj_u2 *c;
    int n;
    nj_s4 vw, vh, x, y;
    nj_u1 *dst;
    const nj_u1 *src;

    if (!player) {
        return nj_throw(vm, "java/lang/IllegalStateException",
                        "the control has no Player");
    }
    if (r_i(vm, player, F_P_STATE) < 300) {
        return nj_throw(vm, "java/lang/IllegalStateException",
                        "prefetch() the Player before getSnapshot()");
    }
    if (!enc) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "encoding is null");
    }
    c = nj_string_chars(enc);
    n = (int)nj_string_len(enc);
    if (!(n == 6 && c[0] == 'R' && c[1] == 'G' && c[2] == 'B'
          && c[3] == '8' && c[4] == '8' && c[5] == '8')) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "only RGB888 snapshots are supported");
    }
    vb = r_r(vm, player, F_P_VBUF, "[B");
    vw = r_i(vm, player, F_P_VW);
    vh = r_i(vm, player, F_P_VH);
    if (!vb || vw <= 0 || vh <= 0 ||
        vb->length < (nj_u4)vw * (nj_u4)vh * 2u) {
        return nj_throw(vm, "java/lang/IllegalStateException",
                        "no frame has been decoded yet");
    }
    out = nj_byte_array_new(vm, (nj_u4)vw * (nj_u4)vh * 3u);
    if (!out) {
        return NJ_ERR;
    }
    src = vb->u.bytes;
    dst = out->u.bytes;
    for (y = 0; y < vh; y++) {
        for (x = 0; x < vw; x++) {
            nj_u4 p = (nj_u4)src[(nj_size)(y * vw + x) * 2] |
                      ((nj_u4)src[(nj_size)(y * vw + x) * 2 + 1] << 8);
            nj_u4 r = (p >> 11) & 0x1Fu;
            nj_u4 g = (p >> 5) & 0x3Fu;
            nj_u4 b = p & 0x1Fu;

            r = (r << 3) | (r >> 2);
            g = (g << 2) | (g >> 4);
            b = (b << 3) | (b >> 2);
            *dst++ = (nj_u1)r;
            *dst++ = (nj_u1)g;
            *dst++ = (nj_u1)b;
        }
    }
    ret->r = out;
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
    FLD(F_P_DEVROOM,  "I", 0),
    FLD(F_P_DATA,     "[B", 0),
    FLD(F_P_STREAM,   "Z", 0),
    FLD(F_P_RATE,     "I", 0),
    FLD(F_P_CHAN,     "I", 0),
    FLD(F_P_FRAMES,   "I", 0),
    FLD(F_P_PUSHED,   "I", 0),
    FLD(F_P_SRC,      "[B", 0),
    FLD(F_P_AVK,      "Z", 0),
    FLD(F_P_VHOLD,    "[B", 0),
    FLD(F_P_VBUF,     "[B", 0),
    FLD(F_P_VW,       "I", 0),
    FLD(F_P_VH,       "I", 0),
    FLD(F_P_VDMODE,   "I", 0),
    FLD(F_P_VCANVAS,  "Ljavax/microedition/lcdui/Canvas;", 0),
    FLD(F_P_VDX,      "I", 0),
    FLD(F_P_VDY,      "I", 0),
    FLD(F_P_VDW,      "I", 0),
    FLD(F_P_VDH,      "I", 0),
    FLD(F_P_VFULL,    "Z", 0),
    FLD(F_P_VVIS,     "Z", 0),
    FLD(F_P_VSTART,   "J", 0),
    FLD(F_P_APCM,     "[B", 0),
    FLD(F_P_APCMN,    "I", 0),
    FLD(F_P_APUSH,    "I", 0),
    FLD(F_P_OPEN,     "Z", 0),
    FLD(F_P_LISTEN,   "Ljava/util/Vector;", 0),
    FLD(F_P_MIDI,     "[B", 0),
    FLD(F_P_MIDIK,    "Z", 0),
    FLD(F_P_PLAT,     "Z", 0),
    FLD(F_P_MASTER,   "L" NJ_C_TIMEBASE ";", 0),
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
    M("getContentType", "()Ljava/lang/String;", 0),
    M("getTimeBase", "()L" NJ_C_TIMEBASE ";", 0),
    M("setTimeBase", "(L" NJ_C_TIMEBASE ";)V", 0)
};

/* javax.microedition.media.TimeBase is an interface with one method, and a
 * MIDlet that keeps a master clock implements it itself.  A Player only has
 * to accept one and hand it back. */
static const nj_builtin_method timebase_methods[] = {
    M("getTime", "()J", NJ_ACC_ABSTRACT)
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

/* VideoControl is an interface a MIDlet may name and checkcast, and nothing
 * more: this runtime decodes audio, not video.  The class has to exist anyway.
 * 毁灭战士3 fetches its intro Player's control with
 * `(VideoControl) p.getControl("VideoControl")` - the checkcast resolves this
 * class even when the answer is null - and without it the whole MIDlet died on
 * an uncaught NoClassDefFoundError the moment its 3gp audio track started
 * decoding.  getControl() keeps returning null, the game's `catch (Exception)`
 * takes the NPE that follows, and the intro is skipped instead of the game. */
static const nj_builtin_field videoctl_fields[] = {
    FLD(F_C_OWNER, "L" NJ_C_PLAYER ";", 0),
    /* JSR 135's two display modes.  Only USE_DIRECT_VIDEO is usable here, but
       the constants are part of the API surface a MIDlet may reference. */
    FLD("USE_GUI_PRIMITIVE", "I", NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("USE_DIRECT_VIDEO",  "I", NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method videocontrol_methods[] = {
    M("initDisplayMode",     "(ILjava/lang/Object;)Ljava/lang/Object;", 0),
    M("setDisplaySize",      "(II)V", 0),
    M("setDisplayFullScreen", "(Z)V", 0),
    M("setDisplayLocation",  "(II)V", 0),
    M("setVisible",          "(Z)V", 0),
    M("getDisplayWidth",     "()I", 0),
    M("getDisplayHeight",    "()I", 0),
    M("getSourceWidth",      "()I", 0),
    M("getSourceHeight",     "()I", 0),
    M("getSnapshot",         "(Ljava/lang/String;)Ljava/lang/Object;", 0)
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
    { NJ_C_TIMEBASE, NULL, NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, timebase_methods,
      (nj_u2)(sizeof(timebase_methods) / sizeof(timebase_methods[0])) },
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
    /* A real control now, not the interface stub it used to be: a video
     * Player's getControl("VideoControl") returns one of these, exactly as
     * getControl("VolumeControl") does.  Its super slot names Control so a
     * VideoControl is-a Control for the VM's subclass walk. */
    { NJ_C_VIDEOC, NJ_C_CONTROL, NJ_ACC_PUBLIC,
      videoctl_fields,
      (nj_u2)(sizeof(videoctl_fields) / sizeof(videoctl_fields[0])),
      videocontrol_methods,
      (nj_u2)(sizeof(videocontrol_methods) / sizeof(videocontrol_methods[0])) },
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
    { NJ_C_PLAYER, "getTimeBase", "()L" NJ_C_TIMEBASE ";", player_getTimeBase },
    { NJ_C_PLAYER, "setTimeBase", "(L" NJ_C_TIMEBASE ";)V", player_setTimeBase },
    { NJ_C_PLAYER, "getControls",
      "()[Ljavax/microedition/media/Control;", player_getControls },

    { NJ_C_TONE, "setToneSequence", "([B)V", tone_setSequence },
    { NJ_C_TONE, "getToneSequence", "()[B", tone_getSequence },
    { NJ_C_TONE, "setSequence",     "([B)V", tone_setSequence },

    { NJ_C_VOLUME, "setLevel", "(I)I", vol_setLevel },
    { NJ_C_VOLUME, "getLevel", "()I", vol_getLevel },
    { NJ_C_VOLUME, "setMute",  "(Z)V", vol_setMute },
    { NJ_C_VOLUME, "isMuted",  "()Z", vol_isMuted },
    { NJ_C_VIDEOC, "initDisplayMode",
      "(ILjava/lang/Object;)Ljava/lang/Object;", videoctl_initDisplayMode },
    { NJ_C_VIDEOC, "setDisplaySize",       "(II)V", videoctl_setDisplaySize },
    { NJ_C_VIDEOC, "setDisplayFullScreen", "(Z)V",  videoctl_setDisplayFullScreen },
    { NJ_C_VIDEOC, "setDisplayLocation",   "(II)V", videoctl_setDisplayLocation },
    { NJ_C_VIDEOC, "setVisible",           "(Z)V",  videoctl_setVisible },
    { NJ_C_VIDEOC, "getDisplayWidth",      "()I",   videoctl_getDisplayWidth },
    { NJ_C_VIDEOC, "getDisplayHeight",     "()I",   videoctl_getDisplayHeight },
    { NJ_C_VIDEOC, "getSourceWidth",       "()I",   videoctl_getSourceWidth },
    { NJ_C_VIDEOC, "getSourceHeight",      "()I",   videoctl_getSourceHeight },
    { NJ_C_VIDEOC, "getSnapshot",
      "(Ljava/lang/String;)Ljava/lang/Object;", videoctl_getSnapshot },

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

