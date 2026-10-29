/*
 * nano_j2me - nj_lib_nokia.c
 *
 * The Nokia UI API: the handful of `com.nokia.mid.ui` calls that a Series 40
 * game makes and that have no meaning on a device without Nokia's keypad.
 *
 * ---------------------------------------------------------------------------
 * WHY THESE ARE NO-OPS, AND WHY THAT IS CORRECT
 * ---------------------------------------------------------------------------
 * `VirtualKeyboard.hideOpenKeypadCommand(true)` asks the device to take the
 * on-screen keypad away.  A desktop has no on-screen keypad, so there is
 * nothing to hide and the honest implementation is to do nothing and return.
 * That is different from the JSR 211 stub next door, which THROWS: there,
 * "share this score" is a request that genuinely cannot be satisfied and the
 * game needs to know; here the request is already satisfied.
 *
 * A class that exists and does nothing is not the same as a class that is
 * absent.  The first third-party game built from source here needed this class
 * to EXIST - it named it in its constructor - and NoClassDefFoundError ended the
 * run before the first frame was drawn.
 */
#include "nj_midp.h"

#include <math.h>

#define NJFN(name) NJ_NATIVE_FN(name)

#define NJ_C_VKEYBOARD "com/nokia/mid/ui/VirtualKeyboard"
#define NJ_C_MPLISTEN  "com/nokia/mid/ui/multipointtouch/MultipointTouchListener"
#define NJ_C_MPTOUCH   "com/nokia/mid/ui/multipointtouch/MultipointTouch"
#define NJ_C_DEVCTL   "com/nokia/mid/ui/DeviceControl"
#define NJ_C_DUTILS    "com/nokia/mid/ui/DirectUtils"
#define NJ_C_DGRAPHICS "com/nokia/mid/ui/DirectGraphics"
#define NJ_C_GRAPHICS "javax/microedition/lcdui/Graphics"
#define NJ_C_FSELECT   "com/nokia/mid/ui/FileSelect"
#define NJ_C_FSDETAIL  "com/nokia/mid/ui/FileSelectDetail"
#define NJ_C_SOUND     "com/nokia/mid/sound/Sound"
/*
 * Vendor vibration APIs.  They live in this module because it is already the
 * home of the handset-specific classes, and because a separate translation
 * unit would have to be registered in three build systems for two tiny
 * classes.  See the Samsung and Motorola sections below.
 */
#define NJ_C_SVIB      "com/samsung/util/Vibration"
#define NJ_C_MVIB      "com/motorola/multimedia/Vibrator"
#define NJ_C_SLISTEN   "com/nokia/mid/sound/SoundListener"

/*
 * MultipointTouch.
 *
 * Two generations of this API exist.  The Series 40 form is a static
 * setMultipointTouchListener(); the Asha form - which Minecraft PE 0.6.1
 * uses - asks for a singleton with getInstance(), registers with an instance
 * method, and reads each pointer with static getState()/getX()/getY().
 *
 * Both are answered from one per-VM instance, and that lets the runtime feed
 * the pointer events the port already reports (desktop mouse, touchscreen,
 * the --keys pointer entries) into the listener.  A title built for a
 * touchscreen then drives its own touch UI instead of having to be played
 * through the keypad.  A port that reports one pointer reports id 0, which is
 * what a single-touch handset does too.
 */
#define MPT_MAX_POINTERS 4
#define MPT_PRESSED      0
#define MPT_DRAGGED      1
#define MPT_RELEASED     2

static nj_field *mpt_f(nj_vm *vm, const char *name, const char *desc,
                       int is_static)
{
    return nj_field_lookup(vm, NJ_C_MPTOUCH, name, desc, is_static);
}

static nj_ref mpt_instance(nj_vm *vm)
{
    return nj_field_read_ref(vm, NULL,
                             mpt_f(vm, "_nj_instance", "L" NJ_C_MPTOUCH ";", 1));
}

static nj_ref mpt_array(nj_vm *vm, nj_ref inst, const char *name)
{
    return nj_field_read_ref(vm, inst, mpt_f(vm, name, "[I", 0));
}

/* Every pointer starts released; x/y start at the origin. */
static void mpt_seed(nj_vm *vm, nj_ref inst)
{
    static const char *names[3] = { "_nj_state", "_nj_x", "_nj_y" };
    nj_s4 initial[3];
    int k;

    initial[0] = MPT_RELEASED;
    initial[1] = 0;
    initial[2] = 0;
    for (k = 0; k < 3; k++) {
        nj_ref a = mpt_array(vm, inst, names[k]);
        nj_s4 *p = a ? (nj_s4 *)nj_array_data(a) : NULL;
        int i;
        for (i = 0; p && i < MPT_MAX_POINTERS; i++) {
            p[i] = initial[k];
        }
    }
}

NJFN(nokia_mpt_init)     { return NJ_OK; }

static nj_ref mpt_make(nj_vm *vm, nj_thread *th, nj_value *args, nj_value *ret)
{
    nj_ref inst = mpt_instance(vm), st, xs, ys;
    nj_u4 mark;

    if (inst) {
        return inst;
    }
    mark = nj_root_mark(vm);
    inst = nj_root(vm, nj_object_new(vm, NJ_C_MPTOUCH));
    if (!inst) {
        nj_root_release(vm, mark);
        (void)nj_throw_oom(vm);
        return NULL;
    }
    st = nj_root(vm, nj_array_new(vm, "I", MPT_MAX_POINTERS));
    xs = nj_root(vm, nj_array_new(vm, "I", MPT_MAX_POINTERS));
    ys = nj_root(vm, nj_array_new(vm, "I", MPT_MAX_POINTERS));
    if (!st || !xs || !ys) {
        nj_root_release(vm, mark);
        (void)nj_throw_oom(vm);
        return NULL;
    }
    nj_field_write_ref(vm, inst, mpt_f(vm, "_nj_state", "[I", 0), st);
    nj_field_write_ref(vm, inst, mpt_f(vm, "_nj_x", "[I", 0), xs);
    nj_field_write_ref(vm, inst, mpt_f(vm, "_nj_y", "[I", 0), ys);
    mpt_seed(vm, inst);
    nj_field_write_ref(vm, NULL,
                       mpt_f(vm, "_nj_instance", "L" NJ_C_MPTOUCH ";", 1), inst);
    (void)th;
    (void)args;
    (void)ret;
    nj_root_release(vm, mark);
    return mpt_instance(vm);
}

NJFN(nokia_mpt_instance)
{
    nj_ref inst = mpt_make(vm, th, args, ret);
    if (!inst) {
        return NJ_ERR;
    }
    ret->r = inst;
    return NJ_OK;
}

NJFN(nokia_mpt_add)
{
    nj_ref self = NJ_THIS(args), listener = NJ_ARG_REF(args, 0);

    if (!self) {                    /* the legacy static form */
        self = mpt_make(vm, th, args, ret);
    }
    if (!self) {
        return NJ_ERR;
    }
    nj_field_write_ref(vm, self,
                       mpt_f(vm, "_nj_listener", "L" NJ_C_MPLISTEN ";", 0),
                       listener);
    mpt_seed(vm, self);
    return NJ_OK;
}

NJFN(nokia_mpt_remove)
{
    nj_ref self = NJ_THIS(args), listener = NJ_ARG_REF(args, 0);
    nj_ref current;

    if (!self) {
        self = mpt_make(vm, th, args, ret);
    }
    if (!self) {
        return NJ_ERR;
    }
    current = nj_field_read_ref(
        vm, self, mpt_f(vm, "_nj_listener", "L" NJ_C_MPLISTEN ";", 0));
    if (!listener || current == listener) {
        nj_field_write_ref(vm, self,
                           mpt_f(vm, "_nj_listener", "L" NJ_C_MPLISTEN ";", 0),
                           NULL);
    }
    return NJ_OK;
}

static nj_s4 mpt_get(nj_vm *vm, nj_s4 id, const char *name)
{
    nj_ref inst = mpt_instance(vm), a;
    const nj_s4 *p;

    if (!inst) {
        return 0;
    }
    a = mpt_array(vm, inst, name);
    if (!a || id < 0 || (nj_u4)id >= a->length) {
        return 0;
    }
    p = (const nj_s4 *)nj_array_data(a);
    return p ? p[id] : 0;
}

NJFN(nokia_mpt_get_state)
{
    ret->i = mpt_get(vm, NJ_ARG_INT(args, 0), "_nj_state");
    return NJ_OK;
}

NJFN(nokia_mpt_get_x)
{
    ret->i = mpt_get(vm, NJ_ARG_INT(args, 0), "_nj_x");
    return NJ_OK;
}

NJFN(nokia_mpt_get_y)
{
    ret->i = mpt_get(vm, NJ_ARG_INT(args, 0), "_nj_y");
    return NJ_OK;
}

/*
 * One pointer event, already in canvas coordinates.  Called from the MIDP
 * pointer dispatch, so the listener sees the same presses and drags a
 * touchscreen handset would have reported.  The record is updated even before
 * a listener registers, because getState()/getX()/getY() are readable then.
 */
void nj_nokia_mpt_pointer(nj_vm *vm, int x, int y, int kind)
{
    nj_ref inst, listener, a;
    nj_s4 *p;
    nj_s4 state;
    nj_u4 mark;
    nj_value arg[1];

    if (!vm) {
        return;
    }
    inst = mpt_instance(vm);
    if (!inst) {
        return;
    }
    state = (kind == NJ_POINTER_DOWN) ? MPT_PRESSED
          : (kind == NJ_POINTER_MOVE) ? MPT_DRAGGED : MPT_RELEASED;
    a = mpt_array(vm, inst, "_nj_state");
    p = a ? (nj_s4 *)nj_array_data(a) : NULL;
    if (p && a->length > 0u) { p[0] = state; }
    a = mpt_array(vm, inst, "_nj_x");
    p = a ? (nj_s4 *)nj_array_data(a) : NULL;
    if (p && a->length > 0u) { p[0] = (nj_s4)x; }
    a = mpt_array(vm, inst, "_nj_y");
    p = a ? (nj_s4 *)nj_array_data(a) : NULL;
    if (p && a->length > 0u) { p[0] = (nj_s4)y; }

    listener = nj_field_read_ref(
        vm, inst, mpt_f(vm, "_nj_listener", "L" NJ_C_MPLISTEN ";", 0));
    if (!listener) {
        return;
    }
    mark = nj_root_mark(vm);
    nj_root(vm, inst);
    nj_root(vm, listener);
    a = nj_root(vm, nj_array_new(vm, "I", 1u));
    if (a) {
        ((nj_s4 *)nj_array_data(a))[0] = 0;   /* the reported pointer id */
        arg[0].r = a;
        (void)nj_call(vm, listener, nj_ref_class_name(listener),
                      "pointersChanged", "([I)V", arg, NULL);
    }
    nj_root_release(vm, mark);
}

/*
 * FileSelect.  A UI SERVICE, not an API this runtime can satisfy: on a phone it
 * opens a device dialog over the MIDlet and returns what the user picked.  There
 * is no such dialog here, and returning an empty array would be indistinguishable
 * from "the user picked nothing" - so it REFUSES, and the game's own catch
 * handles it.  That is the same choice JSR 211 makes next door.
 */
NJFN(nokia_fselect_launch)
{
    return nj_throw(vm, "java/lang/UnsupportedOperationException",
                    "com.nokia.mid.ui.FileSelect.launch needs a device file "
                    "dialog, which this runtime does not have");
}

/*
 * Give the constants their values.  Runs once when the VM is up, for the same
 * reason LCDUI has a late init: a builtin class cannot carry a ConstantValue,
 * so a static final that is never written reads 0 - and a MIDlet comparing
 * against 0 gets the wrong branch with nothing to show for it.
 */
static void nokia_const_int(nj_vm *vm, const char *cls, const char *name,
                            nj_s4 value)
{
    nj_field *f = nj_field_lookup(vm, cls, name, "I", 1);
    if (f) {
        nj_field_write_int(vm, NULL, f, value);
    }
}

nj_status nj_nokia_late_init(nj_vm *vm);

nj_status nj_nokia_late_init(nj_vm *vm)
{
    if (!vm) {
        return NJ_ERR;
    }
    nokia_const_int(vm, NJ_C_FSELECT, "FILE_SYSTEM_PHONE", 1);
    nokia_const_int(vm, NJ_C_FSELECT, "FILE_SYSTEM_MMC", 2);
    nokia_const_int(vm, NJ_C_FSELECT, "FILE_SYSTEM_ALL", 3);
    nokia_const_int(vm, NJ_C_FSELECT, "MEDIA_TYPE_IMAGE", 1);
    nokia_const_int(vm, NJ_C_FSELECT, "MEDIA_TYPE_VIDEO", 2);
    nokia_const_int(vm, NJ_C_FSELECT, "MEDIA_TYPE_AUDIO", 4);
    nokia_const_int(vm, NJ_C_FSELECT, "MEDIA_TYPE_APPLICATION", 8);
    nokia_const_int(vm, NJ_C_FSELECT, "MEDIA_TYPE_ALL", 16);
    /* MultipointTouch pointer states, read by the MIDlet from the class. */
    nokia_const_int(vm, NJ_C_MPTOUCH, "POINTER_PRESSED",  MPT_PRESSED);
    nokia_const_int(vm, NJ_C_MPTOUCH, "POINTER_DRAGGED",  MPT_DRAGGED);
    nokia_const_int(vm, NJ_C_MPTOUCH, "POINTER_RELEASED", MPT_RELEASED);

    /*
     * These numbers come from Nokia's own class library rather than from a
     * stub: the Nokia 5100 SDK's lib/classes.zip answers
     * `javap -constants com.nokia.mid.sound.Sound` with SOUND_PLAYING = 0,
     * SOUND_STOPPED = 1 and SOUND_UNINITIALIZED = 3, so a MIDlet that compares
     * getState() with the constant sees the same pair a handset produced.
     * SOUND_UNPLAYABLE is not in that SDK; 2 is kept because a title compiled
     * against a stub may reference it, and it collides with no Nokia value.
     */
    nokia_const_int(vm, NJ_C_SOUND, "FORMAT_TONE", 1);
    nokia_const_int(vm, NJ_C_SOUND, "FORMAT_WAV", 5);   /* the spec value; the
                                                         * formats array below
                                                         * has always answered 5 */
    nokia_const_int(vm, NJ_C_SOUND, "SOUND_STOPPED", 1);
    nokia_const_int(vm, NJ_C_SOUND, "SOUND_PLAYING", 0);
    nokia_const_int(vm, NJ_C_SOUND, "SOUND_UNPLAYABLE", 2);

    /* Motorola Vibrator tones, from its own class file. */
    nokia_const_int(vm, NJ_C_MVIB, "VIBRATE_SILENT", 0);
    nokia_const_int(vm, NJ_C_MVIB, "VIBRATE_2SHORT", 1);
    nokia_const_int(vm, NJ_C_MVIB, "VIBRATE_LONG", 2);
    nokia_const_int(vm, NJ_C_MVIB, "VIBRATE_PULSE", 3);
    nokia_const_int(vm, NJ_C_MVIB, "VIBRATE_SHORT", 4);
    nokia_const_int(vm, NJ_C_MVIB, "VIBRATE_SHORT_LONG", 5);
    nokia_const_int(vm, NJ_C_MVIB, "MIN_PAUSE_TIME", 2000);
    nokia_const_int(vm, NJ_C_MVIB, "MAX_VIBRATE_TIME", 0x7FFFFFFF);
    nokia_const_int(vm, NJ_C_SOUND, "SOUND_UNINITIALIZED", 3);
    /*
     * The rest of Nokia's Sound constant block, values from the same
     * javap -constants run over the 5100 SDK's lib/classes.zip.  A title that
     * reads one of these and finds no field gets NoSuchFieldError before it
     * ever asks to play anything.
     */
    nokia_const_int(vm, NJ_C_SOUND, "SUCCESSFUL_OPERATION", 15);
    nokia_const_int(vm, NJ_C_SOUND, "FAIL_OPERATION", 16);
    nokia_const_int(vm, NJ_C_SOUND, "DEFAULT_VOLUME", -1);
    nokia_const_int(vm, NJ_C_SOUND, "TONE_VOLUME_NOT_GIVEN", 255);
    nokia_const_int(vm, NJ_C_SOUND, "FREQUENCY_LOW", 0);
    nokia_const_int(vm, NJ_C_SOUND, "FREQUENCY_HIGH", 7692);
    nokia_const_int(vm, NJ_C_SOUND, "MAX_DURATION", 2000);
    nokia_const_int(vm, NJ_C_SOUND, "RET_BAD_PHONE_STATE", 0);
    nokia_const_int(vm, NJ_C_SOUND, "RET_BAD_STATE", 1);
    nokia_const_int(vm, NJ_C_SOUND, "RET_BAD_SEQUENCE", 2);
    nokia_const_int(vm, NJ_C_SOUND, "RET_PLAY_FAIL", 3);
    nokia_const_int(vm, NJ_C_SOUND, "RET_OK", 4);
    nokia_const_int(vm, NJ_C_SOUND, "RET_NOT_READY", 5);
    nokia_const_int(vm, NJ_C_SOUND, "RET_OUT_OF_MEMORY", 6);
    nokia_const_int(vm, NJ_C_SOUND, "RET_COULD_NOT_SEND", 7);
    nokia_const_int(vm, NJ_C_SOUND, "TYPE_BUZZER", 8);
    nokia_const_int(vm, NJ_C_SOUND, "TYPE_TONE", 9);
    nokia_const_int(vm, NJ_C_SOUND, "TYPE_ALERT", 10);
    nokia_const_int(vm, NJ_C_SOUND, "ALERT_ALARM", 0);
    nokia_const_int(vm, NJ_C_SOUND, "ALERT_CONFIRMATION", 1);
    nokia_const_int(vm, NJ_C_SOUND, "ALERT_ERROR", 2);
    nokia_const_int(vm, NJ_C_SOUND, "ALERT_INFO", 3);
    nokia_const_int(vm, NJ_C_SOUND, "ALERT_WARNING", 4);

    nokia_const_int(vm, NJ_C_DGRAPHICS, "FLIP_HORIZONTAL", 0x2000);
    nokia_const_int(vm, NJ_C_DGRAPHICS, "FLIP_VERTICAL", 0x4000);
    nokia_const_int(vm, NJ_C_DGRAPHICS, "ROTATE_90", 90);
    nokia_const_int(vm, NJ_C_DGRAPHICS, "ROTATE_180", 180);
    nokia_const_int(vm, NJ_C_DGRAPHICS, "ROTATE_270", 270);
    nokia_const_int(vm, NJ_C_DGRAPHICS, "TYPE_BYTE_1_GRAY", 1);
    nokia_const_int(vm, NJ_C_DGRAPHICS, "TYPE_BYTE_1_GRAY_VERTICAL", 2);
    nokia_const_int(vm, NJ_C_DGRAPHICS, "TYPE_BYTE_2_GRAY", 3);
    nokia_const_int(vm, NJ_C_DGRAPHICS, "TYPE_BYTE_4_GRAY", 4);
    nokia_const_int(vm, NJ_C_DGRAPHICS, "TYPE_BYTE_8_GRAY", 5);
    nokia_const_int(vm, NJ_C_DGRAPHICS, "TYPE_BYTE_332_RGB", 6);
    nokia_const_int(vm, NJ_C_DGRAPHICS, "TYPE_USHORT_4444_ARGB", 7);
    nokia_const_int(vm, NJ_C_DGRAPHICS, "TYPE_USHORT_444_RGB", 8);
    nokia_const_int(vm, NJ_C_DGRAPHICS, "TYPE_USHORT_555_RGB", 9);
    nokia_const_int(vm, NJ_C_DGRAPHICS, "TYPE_USHORT_565_RGB", 10);
    nokia_const_int(vm, NJ_C_DGRAPHICS, "TYPE_INT_888_RGB", 11);
    nokia_const_int(vm, NJ_C_DGRAPHICS, "TYPE_INT_8888_ARGB", 12);
    nokia_const_int(vm, NJ_C_DGRAPHICS, "TYPE_USHORT_1555_ARGB", 1555);

    {
        const char *fc = "com/nokia/mid/ui/FullCanvas";
        nokia_const_int(vm, fc, "SOFT_KEY_LEFT", -6);
        nokia_const_int(vm, fc, "SOFT_KEY_MIDDLE", -7);
        nokia_const_int(vm, fc, "SOFT_KEY_RIGHT", -8);
        nokia_const_int(vm, fc, "SOFT_KEY1", -6);
        nokia_const_int(vm, fc, "SOFT_KEY2", -7);
        nokia_const_int(vm, fc, "SOFT_KEY3", -8);
        nokia_const_int(vm, fc, "KEY_SOFTKEY1", -6);
        nokia_const_int(vm, fc, "KEY_SOFTKEY2", -7);
        nokia_const_int(vm, fc, "KEY_SOFTKEY3", -8);
    }
    return NJ_OK;
}

NJFN(nokia_fsd_init) { return NJ_OK; }
NJFN(nokia_fsd_getUrl)  { ret->r = NULL; return NJ_OK; }
NJFN(nokia_fsd_getName) { ret->r = NULL; return NJ_OK; }

NJFN(nokia_vk_hide)     { return NJ_OK; }
NJFN(nokia_vk_show)     { return NJ_OK; }
NJFN(nokia_vk_visible)  { ret->i = 0; return NJ_OK; }

/*
 * com.nokia.mid.sound.Sound.
 *
 * Only four members of it are used anywhere in the collected corpus:
 * getState(), play(int), setGain(int) and stop().  Only those are implemented;
 * inventing the rest would be inventing behaviour nobody asked for.
 *
 * play() sets real, observable state and does NOT synthesise audio.  A Sound is
 * built from a Nokia tone string or from WAV bytes, and converting either into
 * portable audio is a job the media module already does for javax.microedition
 * .media - duplicating it here would give games two audio paths that disagree.
 * getState() reports what actually happened, which is what a game polling for
 * "is it still playing" is really asking.
 */
static nj_field *snd_field(nj_vm *vm, const char *name)
{
    return nj_field_lookup(vm, NJ_C_SOUND, name, "I", 0);
}

NJFN(nokia_snd_init)
{
    nj_field *f = snd_field(vm, "state");
    if (f) {
        /* Nokia's own Sound answers SOUND_STOPPED (1) once init() has run,
         * and its constructor runs init(). */
        nj_field_write_int(vm, NJ_THIS(args), f, 1);
    }
    return NJ_OK;
}

/*
 * Tell a registered SoundListener that the state changed.
 *
 * The listener is a real Java object held in an instance field, so the
 * collector sees it.  Nothing calls this unless the state really did change:
 * a listener fired for no reason makes a game believe something happened.
 */
static void snd_notify(nj_vm *vm, nj_ref self, nj_s4 state)
{
    nj_field *f = nj_field_lookup(vm, NJ_C_SOUND, "_listener",
                                  "L" NJ_C_SLISTEN ";", 0);
    nj_ref l;
    nj_value a[2];

    if (!f) {
        return;
    }
    l = nj_field_read_ref(vm, self, f);
    if (!l) {
        return;
    }
    a[0].i = state;
    (void)nj_call(vm, l, NJ_C_SLISTEN, "soundStateChanged",
                  "(L" NJ_C_SOUND ";I)V", a, NULL);
}

NJFN(nokia_snd_play)
{
    nj_field *f = snd_field(vm, "state");
    if (f) {
        nj_field_write_int(vm, NJ_THIS(args), f, 0);   /* SOUND_PLAYING */
    }
    snd_notify(vm, NJ_THIS(args), 0);
    return NJ_OK;
}

NJFN(nokia_snd_setListener)
{
    nj_field *f = nj_field_lookup(vm, NJ_C_SOUND, "_listener",
                                 "L" NJ_C_SLISTEN ";", 0);
    if (f) {
        nj_field_write_ref(vm, NJ_THIS(args), f, NJ_ARG_REF(args, 0));
    }
    return NJ_OK;
}

NJFN(nokia_snd_stop)
{
    nj_field *f = snd_field(vm, "state");
    if (f) {
        nj_field_write_int(vm, NJ_THIS(args), f, 1);   /* SOUND_STOPPED */
    }
    snd_notify(vm, NJ_THIS(args), 1);
    return NJ_OK;
}

NJFN(nokia_snd_setGain)
{
    nj_field *f = snd_field(vm, "gain");
    if (f) {
        nj_field_write_int(vm, NJ_THIS(args), f, NJ_ARG_INT(args, 0));
    }
    return NJ_OK;
}

NJFN(nokia_snd_getState)
{
    nj_field *f = snd_field(vm, "state");
    ret->i = f ? nj_field_read_int(vm, NJ_THIS(args), f) : 0;
    return NJ_OK;
}

/*
 * resume(): carry on with a sound that is not playing.  This runtime has no
 * paused state of its own - play() is what the port's audio path answers to -
 * so a sound that is not playing is started again, which is what a game that
 * calls resume() is asking for.
 */
NJFN(nokia_snd_resume)
{
    nj_field *f = snd_field(vm, "state");
    nj_s4 st = f ? nj_field_read_int(vm, NJ_THIS(args), f) : 0;

    if (st == 0) {                  /* SOUND_PLAYING */
        return NJ_OK;
    }
    if (f) {
        nj_field_write_int(vm, NJ_THIS(args), f, 0);   /* SOUND_PLAYING */
    }
    snd_notify(vm, NJ_THIS(args), 0);
    return NJ_OK;
}

NJFN(nokia_snd_getGain)
{
    nj_field *f = snd_field(vm, "gain");
    ret->i = f ? nj_field_read_int(vm, NJ_THIS(args), f) : 0;
    return NJ_OK;
}

/*
 * getConcurrentSoundCount(int format): how many sounds of that format can play
 * at once.  The port's audio hook is a single mixer, so the honest answer is
 * one - a game reads this to decide whether the music has to stop before a
 * sound effect.
 */
NJFN(nokia_snd_concurrent)
{
    (void)args;
    ret->i = 1;
    return NJ_OK;
}

/*
 * convertFreqToNote(): the Nokia helper that maps a tone frequency onto a MIDI
 * note number, n = round(12 * log2(f / 8.176)) clamped at zero.  8.176 Hz is
 * MIDI note 0, so the constant 12 / ln 2 is 17.31234; frequencies at or below
 * zero have no note and answer 0, matching the reference implementation's
 * Math.max(..., 0).  Real titles use it to turn an entered frequency into the
 * tone the player then plays.
 */
NJFN(nokia_snd_freq_to_note)
{
    nj_s4 freq = NJ_ARG_INT(args, 0);
    double n;

    ret->i = 0;
    if (freq <= 0) {
        return NJ_OK;
    }
    n = log((double)freq / 8.176) * 17.31234;
    ret->i = (nj_s4)(n > 0.0 ? (nj_s4)(n + 0.5) : 0);
    return NJ_OK;
}

/*
 * getSupportedFormats(): the Nokia format constants init() accepts.
 * FORMAT_TONE (1) and FORMAT_WAV (5) are both real here - the port has a tone
 * path and a PCM path - so both are advertised.
 */
NJFN(nokia_snd_formats)
{
    nj_ref a = nj_int_array_new(vm, 2u);
    nj_s4 *p;

    if (!a) {
        return nj_throw_oom(vm);
    }
    p = (nj_s4 *)a->u.bytes;
    if (p) {
        p[0] = 1;                   /* FORMAT_TONE */
        p[1] = 5;                   /* FORMAT_WAV  */
    }
    ret->r = a;
    return NJ_OK;
}

#define M(name, desc, flags) { name, desc, flags }

static const nj_builtin_method mplisten_methods[] = {
    M("pointersChanged", "([I)V", NJ_ACC_ABSTRACT)
};

static const nj_builtin_method slisten_methods[] = {
    M("soundStateChanged", "(L" NJ_C_SOUND ";I)V", NJ_ACC_ABSTRACT)
};

static const nj_builtin_field mptouch_fields[] = {
    { "_nj_listener", "L" NJ_C_MPLISTEN ";", 0 },
    { "_nj_state",    "[I", 0 },
    { "_nj_x",        "[I", 0 },
    { "_nj_y",        "[I", 0 },
    { "_nj_instance", "L" NJ_C_MPTOUCH ";", NJ_ACC_STATIC },
    { "POINTER_PRESSED",  "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "POINTER_DRAGGED",  "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "POINTER_RELEASED", "I", NJ_ACC_STATIC | NJ_ACC_FINAL }
};

static const nj_builtin_method mptouch_methods[] = {
    M("<init>", "()V", 0),
    /* The Asha generation: a singleton and per-pointer static readers. */
    M("getInstance", "()L" NJ_C_MPTOUCH ";", NJ_ACC_STATIC),
    M("addMultipointTouchListener",
      "(L" NJ_C_MPLISTEN ";)V", 0),
    M("removeMultipointTouchListener",
      "(L" NJ_C_MPLISTEN ";)V", 0),
    M("getState", "(I)I", NJ_ACC_STATIC),
    M("getX",     "(I)I", NJ_ACC_STATIC),
    M("getY",     "(I)I", NJ_ACC_STATIC),
    /* The Series 40 generation, kept because titles still call it. */
    M("setMultipointTouchListener",
      "(L" NJ_C_MPLISTEN ";)V", NJ_ACC_STATIC),
    M("setMultipointTouchListener",
      "(L" NJ_C_MPLISTEN ";I)V", NJ_ACC_STATIC)
};

static const nj_builtin_field sound_fields[] = {
    { "state", "I", 0 },
    { "gain",  "I", 0 },
    { "_listener", "L" NJ_C_SLISTEN ";", 0 },
    { "FORMAT_TONE",   "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "FORMAT_WAV",    "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "SOUND_STOPPED", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "SOUND_PLAYING", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "SOUND_UNPLAYABLE", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "SOUND_UNINITIALIZED", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "SUCCESSFUL_OPERATION", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "FAIL_OPERATION", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "DEFAULT_VOLUME", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "TONE_VOLUME_NOT_GIVEN", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "FREQUENCY_LOW", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "FREQUENCY_HIGH", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "MAX_DURATION", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "RET_BAD_PHONE_STATE", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "RET_BAD_STATE", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "RET_BAD_SEQUENCE", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "RET_PLAY_FAIL", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "RET_OK", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "RET_NOT_READY", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "RET_OUT_OF_MEMORY", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "RET_COULD_NOT_SEND", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "TYPE_BUZZER", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "TYPE_TONE", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "TYPE_ALERT", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "ALERT_ALARM", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "ALERT_CONFIRMATION", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "ALERT_ERROR", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "ALERT_INFO", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "ALERT_WARNING", "I", NJ_ACC_STATIC | NJ_ACC_FINAL }
};

static const nj_builtin_method sound_methods[] = {
    M("<init>",        "(IJ)V", 0),
    M("<init>",        "([BI)V", 0),
    /*
     * The same two payloads are also reachable as a plain init() method, and
     * 3D滚球_11586 calls it that way.  Declaring only the constructors made
     * the class exist and the call fail, which is the worst of both.
     */
    M("init",          "(IJ)V", 0),
    M("init",          "([BI)V", 0),
    M("play",          "(I)V",  0),
    M("stop",          "()V",   0),
    M("setGain",       "(I)V",  0),
    M("getState",      "()I",   0),
    M("resume",       "()V",   0),
    M("getGain",      "()I",   0),
    M("getConcurrentSoundCount", "(I)I", NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("getSupportedFormats", "()[I", NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("convertFreqToNote", "(I)I", NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    /* Only one game in the corpus calls this, and it implements the
     * listener itself: the interface has to exist or that class cannot
     * even be loaded. */
    M("setSoundListener", "(L" NJ_C_SLISTEN ";)V", 0),
    M("release",       "()V",   0)
};

static const nj_builtin_field fsdetail_fields[] = {
    { "url",      "Ljava/lang/String;", 0 },
    { "name",     "Ljava/lang/String;", 0 },
    { "mimeType", "Ljava/lang/String;", 0 },
    { "size",     "J",                  0 }
};

/*
 * The constants.  A builtin class has no ConstantValue attribute, so a static
 * final has to be given its value when the VM comes up - and until it is, the
 * field EXISTS and reads 0, which is worse than absent: a MIDlet compiles,
 * runs, and silently compares against zero.
 *
 * These are declared and set here rather than in the stubs alone because
 * tools/test_consts.c compares every constant the stubs promise against what
 * the runtime actually holds, and it found ten of these missing.
 */
static const nj_builtin_field fselect_fields[] = {
    { "FILE_SYSTEM_PHONE",     "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "FILE_SYSTEM_MMC",       "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "FILE_SYSTEM_ALL",       "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "MEDIA_TYPE_IMAGE",      "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "MEDIA_TYPE_VIDEO",      "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "MEDIA_TYPE_AUDIO",      "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "MEDIA_TYPE_APPLICATION","I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "MEDIA_TYPE_ALL",        "I", NJ_ACC_STATIC | NJ_ACC_FINAL }
};

static const nj_builtin_field fullcanvas_fields[] = {
    { "SOFT_KEY_LEFT",   "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "SOFT_KEY_MIDDLE", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "SOFT_KEY_RIGHT",  "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "SOFT_KEY1",       "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "SOFT_KEY2",       "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "SOFT_KEY3",       "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "KEY_SOFTKEY1",    "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "KEY_SOFTKEY2",    "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "KEY_SOFTKEY3",    "I", NJ_ACC_STATIC | NJ_ACC_FINAL }
};

static const nj_builtin_method fselect_methods[] = {
    M("launch", "(IIZ)[L" NJ_C_FSDETAIL ";", NJ_ACC_STATIC)
};

static const nj_builtin_method fsdetail_methods[] = {
    M("<init>",    "()V", 0),
    M("getUrl",    "()Ljava/lang/String;", 0),
    M("getName",   "()Ljava/lang/String;", 0)
};

static const nj_builtin_method vkeyboard_methods[] = {
    M("hideOpenKeypadCommand", "(Z)V", NJ_ACC_STATIC),
    M("showOpenKeypadCommand", "(Z)V", NJ_ACC_STATIC),
    M("setVisibility",         "(Z)V", NJ_ACC_STATIC),
    M("getVisibility",         "()Z",  NJ_ACC_STATIC)
};

/* com.nokia.mid.ui.DirectUtils ------------------------------------------ */

/* The public-API image type, spelled once. */
#define NJ_DU_IMAGE_CLS "javax/microedition/lcdui/Image"
#define NJ_DU_IMAGE_DESC "Ljavax/microedition/lcdui/Image;"

/*
 * createImage(w, h, argb): a new image entirely of one colour.
 *
 * Built the long way round - create it, take its Graphics, paint the whole
 * rectangle - because that path already exists, is already tested, and cannot
 * disagree with what a game gets when it does the same three steps by hand.
 */
NJFN(nokia_du_createImage3)
{
    nj_value a[5];
    nj_ref img;
    nj_ref gfx;

    a[0] = args[0];
    a[1] = args[1];
    img = nj_call_ref(vm, NULL, NJ_DU_IMAGE_CLS, "createImage",
                      "(II)" NJ_DU_IMAGE_DESC, a);
    if (!img) {
        return NJ_ERR;
    }
    gfx = nj_call_ref(vm, img, NJ_DU_IMAGE_CLS, "getGraphics",
                      "()Ljavax/microedition/lcdui/Graphics;", NULL);
    if (!gfx) {
        return NJ_ERR;
    }
    a[0] = args[2];
    if (nj_call(vm, gfx, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL)
        != NJ_OK) {
        return NJ_ERR;
    }
    a[0].i = 0;
    a[1].i = 0;
    a[2].i = NJ_ARG_INT(args, 0);
    a[3].i = NJ_ARG_INT(args, 1);
    if (nj_call(vm, gfx, NJ_C_GRAPHICS, "fillRect", "(IIII)V", a, NULL)
        != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = img;
    return NJ_OK;
}

NJFN(nokia_du_createImage2)
{
    nj_value a[5];

    a[0] = args[0];
    a[1] = args[1];
    ret->r = nj_call_ref(vm, NULL, NJ_DU_IMAGE_CLS, "createImage",
                         "(II)" NJ_DU_IMAGE_DESC, a);
    return ret->r ? NJ_OK : NJ_ERR;
}

/*
 * createImage(byte[] data, int offset, int length): the vendor spelling of
 * Image.createImage(data, offset, length).  A title hands Nokia the same bytes
 * it would hand MIDP, so this goes straight to the MIDP path rather than
 * through a second decoder that could disagree with it.
 */
NJFN(nokia_du_createImage)
{
    nj_value a[3];

    a[0] = args[0];
    a[1] = args[1];
    a[2] = args[2];
    ret->r = nj_call_ref(vm, NULL, NJ_DU_IMAGE_CLS, "createImage",
                         "([BII)" NJ_DU_IMAGE_DESC, a);
    return ret->r ? NJ_OK : NJ_ERR;
}

/*
 * getDirectGraphics(Graphics) returns that same Graphics - see the note above
 * the interface table for why that is the whole implementation.
 */
NJFN(nokia_du_getDirectGraphics)
{
    ret->r = NJ_ARG_REF(args, 0);
    return NJ_OK;
}

/*
 * com.nokia.mid.ui.DirectUtils and DirectGraphics.
 *
 * DirectGraphics is an INTERFACE on the phone; twelve of the titles collected
 * under tests/games ask for it, nearly all of them for createImage(w, h, argb)
 * - a screen-sized image of one colour, which the standard API can only build
 * by allocating and then filling through a Graphics.
 *
 * getDirectGraphics() hands back the Graphics it was given.  That is not a
 * shortcut: invokeinterface dispatches on the receiver's RUNTIME class, and
 * the seven DirectGraphics methods are declared on javax.microedition.lcdui.
 * Graphics in nj_lib_lcdui.c, which is the file that can reach the nj_gfx
 * primitives.  So the object a caller gets back must BE a Graphics, and it is.
 */
static const nj_builtin_field dgraphics_fields[] = {
    { "FLIP_HORIZONTAL", "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "FLIP_VERTICAL",   "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "ROTATE_90",       "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "ROTATE_180",      "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "ROTATE_270",      "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "TYPE_BYTE_1_GRAY",           "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "TYPE_BYTE_1_GRAY_VERTICAL",  "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "TYPE_BYTE_2_GRAY",           "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "TYPE_BYTE_4_GRAY",           "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "TYPE_BYTE_8_GRAY",           "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "TYPE_BYTE_332_RGB",          "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "TYPE_USHORT_4444_ARGB",      "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "TYPE_USHORT_444_RGB",        "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "TYPE_USHORT_555_RGB",        "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "TYPE_USHORT_565_RGB",        "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "TYPE_INT_888_RGB",           "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "TYPE_INT_8888_ARGB",         "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "TYPE_USHORT_1555_ARGB",      "I", NJ_ACC_STATIC | NJ_ACC_FINAL }
};

static const nj_builtin_method dgraphics_methods[] = {
    M("setARGBColor",         "(I)V",   NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("getNativePixelFormat", "()I",    NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("drawImage",  "(L" NJ_C_IMAGE ";IIII)V", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("fillTriangle", "(IIIIIII)V",     NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("drawTriangle", "(IIIIIII)V",     NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("drawPolygon", "([II[IIII)V",     NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("fillPolygon", "([II[IIII)V",     NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("drawPixels",  "([SZIIIIIIII)V",  NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("getPixels",   "([SIIIIIII)V",    NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("getPixels", "([IIIIIIII)V", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("drawImage",   "([IIII)V",        NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("drawPixels",  "([IZIIIIIIII)V",  NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("drawPixels",  "([B[BIIIIIIII)V", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("getPixels",   "([B[BIIIIIII)V",  NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("getAlphaComponent", "()I",       NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT)
};

static const nj_builtin_method dutils_methods[] = {
    M("createImage", "(II)Ljavax/microedition/lcdui/Image;", NJ_ACC_STATIC),
    M("createImage", "(III)Ljavax/microedition/lcdui/Image;", NJ_ACC_STATIC),
    M("createImage", "([BII)Ljavax/microedition/lcdui/Image;", NJ_ACC_STATIC),
    M("getDirectGraphics",
      "(Ljavax/microedition/lcdui/Graphics;)L" NJ_C_DGRAPHICS ";",
      NJ_ACC_STATIC)
};

/*
 * com.nokia.mid.ui.DeviceControl
 *
 * Vibration, the notification lights and the screen backlight have no
 * equivalent in MIDP, so a MIDlet that wants any of them has no portable way
 * to ask.  Three titles collected here call into this class, and on a device
 * it is four side effects with nothing to observe afterwards.
 *
 * There is no port hook for them yet, so they succeed and do nothing.  That is
 * a deliberate choice over throwing: a game that vibrates on a hit is playable
 * without the buzz, and refusing the call would take the whole game down for a
 * missing motor.  If a port ever grows a hook, these four are where it lands.
 */
static const nj_builtin_method devctl_methods[] = {
    M("setLights",       "(II)V",  NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("setLights",       "(III)V", NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("flashLights",     "(I)V",   NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("vibrate",         "(I)V",   NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("startVibra",      "(IJ)V",  NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("stopVibra",       "()V",    NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("flashLights",     "(J)V",   NJ_ACC_STATIC | NJ_ACC_PUBLIC),
    M("setScreenState",  "(Z)V",   NJ_ACC_STATIC | NJ_ACC_PUBLIC)
};

NJFN(nokia_dc_ok)
{
    (void)vm;
    (void)args;
    (void)ret;
    return NJ_OK;
}

/*
 * startVibra(frequency, duration) / stopVibra(): the Nokia UI API's second
 * shape for the side effect vibrate() performs.  The frequency argument has no
 * meaning for a port whose hook takes milliseconds, so it is ignored, and
 * stopVibra() is a zero-length buzz.  The light calls stay no-ops for the
 * reason above the table: a game that flashes on a hit is playable without the
 * light, and refusing the call would take the whole game down for a missing
 * lamp.
 */
NJFN(nokia_dc_startVibra)
{
    nj_s4 ms = NJ_ARG_INT(args, 1);

    if (ms < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative duration");
    }
    if (vm->port && vm->port->vibrate) {
        vm->port->vibrate(vm->port->ud, (int)ms);
    }
    return NJ_OK;
}

NJFN(nokia_dc_stopVibra)
{
    (void)args;
    if (vm->port && vm->port->vibrate) {
        vm->port->vibrate(vm->port->ud, 0);
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Vendor vibration APIs                                              */
/*                                                                    */
/* Samsung's Vibration and Motorola's Vibrator are handset extras that */
/* titles probe with Class.forName() before using.  Neither can buzz a */
/* headless port, so the implementations validate their arguments the  */
/* way the reference does and otherwise do nothing: the title gets the  */
/* methods it probed for, and no state is kept (so RAM does not grow).  */
/* ------------------------------------------------------------------ */
static nj_status svib_is_supported(nj_vm *vm, nj_thread *th,
                                   nj_value *args, nj_value *ret)
{
    (void)vm; (void)th; (void)args;
    ret->i = 1;
    return NJ_OK;
}

static nj_status svib_start(nj_vm *vm, nj_thread *th, nj_value *args,
                            nj_value *ret)
{
    nj_s4 duration = NJ_ARG_INT(args, 0);
    nj_s4 strength = NJ_ARG_INT(args, 1);

    (void)th; (void)ret;
    /* Reference: duration may be zero, strength is a 1..5 scale. */
    if (duration < 0 || strength < 1 || strength > 5) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "invalid vibration arguments");
    }
    return NJ_OK;
}

static nj_status svib_stop(nj_vm *vm, nj_thread *th, nj_value *args,
                           nj_value *ret)
{
    (void)vm; (void)th; (void)args; (void)ret;
    return NJ_OK;
}

static nj_status mvib_set_tone(nj_vm *vm, nj_thread *th, nj_value *args,
                               nj_value *ret)
{
    nj_s4 tone = NJ_ARG_INT(args, 0);

    (void)th; (void)ret;
    if (tone < 0 || tone > 5) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "unknown vibration tone");
    }
    return NJ_OK;
}

static nj_status mvib_periodic_one(nj_vm *vm, nj_thread *th, nj_value *args,
                                   nj_value *ret)
{
    (void)vm; (void)th; (void)ret;
    if (NJ_ARG_INT(args, 0) < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative vibration time");
    }
    return NJ_OK;
}

static nj_status mvib_periodic_two(nj_vm *vm, nj_thread *th, nj_value *args,
                                   nj_value *ret)
{
    (void)th; (void)ret;
    if (NJ_ARG_INT(args, 0) < 0 || NJ_ARG_INT(args, 1) < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative vibration time");
    }
    return NJ_OK;
}

static nj_status mvib_setup(nj_vm *vm, nj_thread *th, nj_value *args,
                            nj_value *ret)
{
    (void)th; (void)ret;
    if (NJ_ARG_INT(args, 0) < 0 || NJ_ARG_INT(args, 1) < 0 ||
        NJ_ARG_INT(args, 2) < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative vibration time");
    }
    return NJ_OK;
}

static nj_status mvib_noop(nj_vm *vm, nj_thread *th, nj_value *args,
                           nj_value *ret)
{
    (void)vm; (void)th; (void)args; (void)ret;
    return NJ_OK;
}

static const nj_builtin_field mvib_fields[] = {
    { "MAX_VIBRATE_TIME", "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "MIN_PAUSE_TIME",   "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "VIBRATE_2SHORT",   "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "VIBRATE_LONG",     "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "VIBRATE_PULSE",    "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "VIBRATE_SHORT",    "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "VIBRATE_SHORT_LONG","I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "VIBRATE_SILENT",   "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL }
};

static const nj_builtin_method svib_methods[] = {
    M("isSupported", "()Z",   NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("start",       "(II)V", NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("stop",        "()V",   NJ_ACC_PUBLIC | NJ_ACC_STATIC)
};

static const nj_builtin_method mvib_methods[] = {
    M("run",                    "()V",    NJ_ACC_PUBLIC),
    M("setVibrateTone",         "(I)V",   NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("vibrateFor",             "(I)V",   NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("vibratePeriodically",    "(I)V",   NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("vibratePeriodically",    "(II)V",  NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("vibratorOff",            "()V",    NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("vibratorOn",             "()V",    NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("setupVibration",         "(III)V", NJ_ACC_PUBLIC | NJ_ACC_STATIC)
};

const nj_builtin_class nj_lib_nokia_classes[] = {
    { NJ_C_VKEYBOARD, "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, vkeyboard_methods,
      (nj_u2)(sizeof(vkeyboard_methods) / sizeof(vkeyboard_methods[0])) },
    { NJ_C_MPLISTEN, NULL,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, mplisten_methods,
      (nj_u2)(sizeof(mplisten_methods) / sizeof(mplisten_methods[0])) },
    { NJ_C_SVIB, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_FINAL,
      NULL, 0u, svib_methods,
      (nj_u2)(sizeof(svib_methods) / sizeof(svib_methods[0])) },
    { NJ_C_MVIB, "java/util/TimerTask",
      NJ_ACC_PUBLIC,
      mvib_fields,
      (nj_u2)(sizeof(mvib_fields) / sizeof(mvib_fields[0])),
      mvib_methods,
      (nj_u2)(sizeof(mvib_methods) / sizeof(mvib_methods[0])) },
    { NJ_C_MPTOUCH, "java/lang/Object", NJ_ACC_PUBLIC,
      mptouch_fields,
      (nj_u2)(sizeof(mptouch_fields) / sizeof(mptouch_fields[0])),
      mptouch_methods,
      (nj_u2)(sizeof(mptouch_methods) / sizeof(mptouch_methods[0])) },
    { NJ_C_SLISTEN, NULL,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, slisten_methods,
      (nj_u2)(sizeof(slisten_methods) / sizeof(slisten_methods[0])) },
    { NJ_C_FSELECT, "java/lang/Object", NJ_ACC_PUBLIC,
      fselect_fields,
      (nj_u2)(sizeof(fselect_fields) / sizeof(fselect_fields[0])),
      fselect_methods,
      (nj_u2)(sizeof(fselect_methods) / sizeof(fselect_methods[0])) },
    { "com/nokia/mid/ui/FullCanvas", "javax/microedition/lcdui/Canvas",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      fullcanvas_fields,
      (nj_u2)(sizeof(fullcanvas_fields) / sizeof(fullcanvas_fields[0])),
      NULL, 0u },
    { NJ_C_SOUND, "java/lang/Object", NJ_ACC_PUBLIC,
      sound_fields,
      (nj_u2)(sizeof(sound_fields) / sizeof(sound_fields[0])),
      sound_methods,
      (nj_u2)(sizeof(sound_methods) / sizeof(sound_methods[0])) },
    { NJ_C_FSDETAIL, "java/lang/Object", NJ_ACC_PUBLIC,
      fsdetail_fields,
      (nj_u2)(sizeof(fsdetail_fields) / sizeof(fsdetail_fields[0])),
      fsdetail_methods,
      (nj_u2)(sizeof(fsdetail_methods) / sizeof(fsdetail_methods[0])) },
    { NJ_C_DGRAPHICS, NULL,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      dgraphics_fields,
      (nj_u2)(sizeof(dgraphics_fields) / sizeof(dgraphics_fields[0])),
      dgraphics_methods,
      (nj_u2)(sizeof(dgraphics_methods) / sizeof(dgraphics_methods[0])) },
    { NJ_C_DUTILS, "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, dutils_methods,
      (nj_u2)(sizeof(dutils_methods) / sizeof(dutils_methods[0])) },
    { NJ_C_DEVCTL, "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, devctl_methods,
      (nj_u2)(sizeof(devctl_methods) / sizeof(devctl_methods[0])) },
    { NULL, NULL, 0u, NULL, 0u, NULL, 0u }
};
const int nj_lib_nokia_class_count =
    (int)(sizeof(nj_lib_nokia_classes) / sizeof(nj_lib_nokia_classes[0])) - 1;

const nj_native_entry nj_lib_nokia_table[] = {
    { NJ_C_SVIB, "isSupported", "()Z",   svib_is_supported },
    { NJ_C_SVIB, "start",       "(II)V", svib_start },
    { NJ_C_SVIB, "stop",        "()V",   svib_stop },
    { NJ_C_MVIB, "run",                 "()V",    mvib_noop },
    { NJ_C_MVIB, "setVibrateTone",      "(I)V",   mvib_set_tone },
    { NJ_C_MVIB, "vibrateFor",          "(I)V",   mvib_periodic_one },
    { NJ_C_MVIB, "vibratePeriodically", "(I)V",   mvib_periodic_one },
    { NJ_C_MVIB, "vibratePeriodically", "(II)V",  mvib_periodic_two },
    { NJ_C_MVIB, "vibratorOff",         "()V",    mvib_noop },
    { NJ_C_MVIB, "vibratorOn",          "()V",    mvib_noop },
    { NJ_C_MVIB, "setupVibration",      "(III)V", mvib_setup },
    { NJ_C_DEVCTL, "setLights",      "(II)V",  nokia_dc_ok },
    { NJ_C_DEVCTL, "setLights",      "(III)V", nokia_dc_ok },
    { NJ_C_DEVCTL, "flashLights",    "(I)V",   nokia_dc_ok },
    { NJ_C_DEVCTL, "vibrate",        "(I)V",   nokia_dc_ok },
    { NJ_C_DEVCTL, "startVibra",     "(IJ)V",  nokia_dc_startVibra },
    { NJ_C_DEVCTL, "stopVibra",      "()V",    nokia_dc_stopVibra },
    { NJ_C_DEVCTL, "flashLights",    "(J)V",   nokia_dc_ok },
    { NJ_C_DEVCTL, "setScreenState", "(Z)V",   nokia_dc_ok },
    { NJ_C_VKEYBOARD, "hideOpenKeypadCommand", "(Z)V", nokia_vk_hide },
    { NJ_C_VKEYBOARD, "showOpenKeypadCommand", "(Z)V", nokia_vk_show },
    { NJ_C_VKEYBOARD, "setVisibility",         "(Z)V", nokia_vk_hide },
    { NJ_C_VKEYBOARD, "getVisibility",         "()Z",  nokia_vk_visible },
    { NJ_C_MPTOUCH, "<init>", "()V", nokia_mpt_init },
    { NJ_C_MPTOUCH, "getInstance", "()L" NJ_C_MPTOUCH ";", nokia_mpt_instance },
    { NJ_C_MPTOUCH, "addMultipointTouchListener",
      "(L" NJ_C_MPLISTEN ";)V", nokia_mpt_add },
    { NJ_C_MPTOUCH, "removeMultipointTouchListener",
      "(L" NJ_C_MPLISTEN ";)V", nokia_mpt_remove },
    { NJ_C_MPTOUCH, "getState", "(I)I", nokia_mpt_get_state },
    { NJ_C_MPTOUCH, "getX",     "(I)I", nokia_mpt_get_x },
    { NJ_C_MPTOUCH, "getY",     "(I)I", nokia_mpt_get_y },
    { NJ_C_MPTOUCH, "setMultipointTouchListener",
      "(L" NJ_C_MPLISTEN ";)V", nokia_mpt_add },
    { NJ_C_MPTOUCH, "setMultipointTouchListener",
      "(L" NJ_C_MPLISTEN ";I)V", nokia_mpt_add },
    { NJ_C_FSELECT, "launch", "(IIZ)[L" NJ_C_FSDETAIL ";",
      nokia_fselect_launch },
    { NJ_C_FSDETAIL, "<init>",  "()V", nokia_fsd_init },
    { NJ_C_SOUND, "<init>",  "(IJ)V", nokia_snd_init },
    { NJ_C_SOUND, "<init>",  "([BI)V", nokia_snd_init },
    { NJ_C_SOUND, "init",    "(IJ)V", nokia_snd_init },
    { NJ_C_SOUND, "init",    "([BI)V", nokia_snd_init },
    { NJ_C_SOUND, "setSoundListener",
      "(L" NJ_C_SLISTEN ";)V", nokia_snd_setListener },
    { NJ_C_SOUND, "play",    "(I)V",  nokia_snd_play },
    { NJ_C_SOUND, "stop",    "()V",   nokia_snd_stop },
    { NJ_C_SOUND, "setGain", "(I)V",  nokia_snd_setGain },
    { NJ_C_SOUND, "getState","()I",   nokia_snd_getState },
    { NJ_C_SOUND, "resume",   "()V",  nokia_snd_resume },
    { NJ_C_SOUND, "getGain",  "()I",  nokia_snd_getGain },
    { NJ_C_SOUND, "getConcurrentSoundCount", "(I)I", nokia_snd_concurrent },
    { NJ_C_SOUND, "getSupportedFormats",     "()[I", nokia_snd_formats },
    { NJ_C_SOUND, "convertFreqToNote",       "(I)I", nokia_snd_freq_to_note },
    { NJ_C_SOUND, "release", "()V",   nokia_snd_stop },
    { NJ_C_FSDETAIL, "getUrl",  "()Ljava/lang/String;", nokia_fsd_getUrl },
    { NJ_C_FSDETAIL, "getName", "()Ljava/lang/String;", nokia_fsd_getName },
    { NJ_C_DUTILS, "createImage", "(II)" NJ_DU_IMAGE_DESC,
      nokia_du_createImage2 },
    { NJ_C_DUTILS, "createImage", "(III)" NJ_DU_IMAGE_DESC,
      nokia_du_createImage3 },
    { NJ_C_DUTILS, "createImage", "([BII)" NJ_DU_IMAGE_DESC,
      nokia_du_createImage },
    { NJ_C_DUTILS, "getDirectGraphics",
      "(Ljavax/microedition/lcdui/Graphics;)L" NJ_C_DGRAPHICS ";",
      nokia_du_getDirectGraphics },
    { NULL, NULL, NULL, NULL }
};
const int nj_lib_nokia_count =
    (int)(sizeof(nj_lib_nokia_table) / sizeof(nj_lib_nokia_table[0])) - 1;
