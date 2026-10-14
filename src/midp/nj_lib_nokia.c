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
#define NJ_C_SLISTEN   "com/nokia/mid/sound/SoundListener"

/*
 * MultipointTouch.  Registering a listener succeeds and the listener is never
 * called, because this runtime has no multitouch panel to report from.
 *
 * That is a real difference from VirtualKeyboard next door, which is a no-op
 * because the request is ALREADY SATISFIED (there is no on-screen keypad to
 * hide).  Here the request cannot be satisfied at all - a game that wants
 * two-finger input will not get it - so the honest thing is that the call
 * succeeds and nothing arrives, which is exactly what happens on a
 * single-touch handset.  A device with no multitouch is a device J2ME games
 * already handle.
 */
NJFN(nokia_mpt_set)     { return NJ_OK; }

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

    /*
     * The stub jar inlines these, so the runtime has to hold the same numbers:
     * a MIDlet comparing getState() against a stub's SOUND_PLAYING of 1 while
     * the runtime answers 0 never notices it is being lied to.
     */
    nokia_const_int(vm, NJ_C_SOUND, "FORMAT_TONE", 1);
    nokia_const_int(vm, NJ_C_SOUND, "FORMAT_WAV", 5);   /* the spec value; the
                                                         * formats array below
                                                         * has always answered 5 */
    nokia_const_int(vm, NJ_C_SOUND, "SOUND_STOPPED", 0);
    nokia_const_int(vm, NJ_C_SOUND, "SOUND_PLAYING", 1);
    nokia_const_int(vm, NJ_C_SOUND, "SOUND_UNPLAYABLE", 2);
    nokia_const_int(vm, NJ_C_SOUND, "SOUND_UNINITIALIZED", -1);

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
        nj_field_write_int(vm, NJ_THIS(args), f, 0);
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
        nj_field_write_int(vm, NJ_THIS(args), f, 1);   /* SOUND_PLAYING */
    }
    snd_notify(vm, NJ_THIS(args), 1);
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
        nj_field_write_int(vm, NJ_THIS(args), f, 0);   /* SOUND_STOPPED */
    }
    snd_notify(vm, NJ_THIS(args), 0);
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

    if (st == 1) {                  /* SOUND_PLAYING */
        return NJ_OK;
    }
    if (f) {
        nj_field_write_int(vm, NJ_THIS(args), f, 1);   /* SOUND_PLAYING */
    }
    snd_notify(vm, NJ_THIS(args), 1);
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

static const nj_builtin_method mptouch_methods[] = {
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
    { "SOUND_UNINITIALIZED", "I", NJ_ACC_STATIC | NJ_ACC_FINAL }
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
    { "TYPE_INT_8888_ARGB",         "I", NJ_ACC_STATIC | NJ_ACC_FINAL }
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

const nj_builtin_class nj_lib_nokia_classes[] = {
    { NJ_C_VKEYBOARD, "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, vkeyboard_methods,
      (nj_u2)(sizeof(vkeyboard_methods) / sizeof(vkeyboard_methods[0])) },
    { NJ_C_MPLISTEN, NULL,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, mplisten_methods,
      (nj_u2)(sizeof(mplisten_methods) / sizeof(mplisten_methods[0])) },
    { NJ_C_MPTOUCH, "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, mptouch_methods,
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
    { NJ_C_MPTOUCH, "setMultipointTouchListener",
      "(L" NJ_C_MPLISTEN ";)V", nokia_mpt_set },
    { NJ_C_MPTOUCH, "setMultipointTouchListener",
      "(L" NJ_C_MPLISTEN ";I)V", nokia_mpt_set },
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
