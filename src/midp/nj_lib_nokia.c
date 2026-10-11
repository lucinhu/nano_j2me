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
#define NJ_C_FSELECT   "com/nokia/mid/ui/FileSelect"
#define NJ_C_FSDETAIL  "com/nokia/mid/ui/FileSelectDetail"

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

NJFN(nokia_fsd_init) { return NJ_OK; }
NJFN(nokia_fsd_getUrl)  { ret->r = NULL; return NJ_OK; }
NJFN(nokia_fsd_getName) { ret->r = NULL; return NJ_OK; }

NJFN(nokia_vk_hide)     { return NJ_OK; }
NJFN(nokia_vk_show)     { return NJ_OK; }
NJFN(nokia_vk_visible)  { ret->i = 0; return NJ_OK; }

#define M(name, desc, flags) { name, desc, flags }

static const nj_builtin_method mplisten_methods[] = {
    M("pointersChanged", "([I)V", NJ_ACC_ABSTRACT)
};

static const nj_builtin_method mptouch_methods[] = {
    M("setMultipointTouchListener",
      "(L" NJ_C_MPLISTEN ";)V", NJ_ACC_STATIC),
    M("setMultipointTouchListener",
      "(L" NJ_C_MPLISTEN ";I)V", NJ_ACC_STATIC)
};

static const nj_builtin_field fsdetail_fields[] = {
    { "url",      "Ljava/lang/String;", 0 },
    { "name",     "Ljava/lang/String;", 0 },
    { "mimeType", "Ljava/lang/String;", 0 },
    { "size",     "J",                  0 }
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
    { NJ_C_FSELECT, "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, fselect_methods,
      (nj_u2)(sizeof(fselect_methods) / sizeof(fselect_methods[0])) },
    { NJ_C_FSDETAIL, "java/lang/Object", NJ_ACC_PUBLIC,
      fsdetail_fields,
      (nj_u2)(sizeof(fsdetail_fields) / sizeof(fsdetail_fields[0])),
      fsdetail_methods,
      (nj_u2)(sizeof(fsdetail_methods) / sizeof(fsdetail_methods[0])) },
    { NULL, NULL, 0u, NULL, 0u, NULL, 0u }
};
const int nj_lib_nokia_class_count =
    (int)(sizeof(nj_lib_nokia_classes) / sizeof(nj_lib_nokia_classes[0])) - 1;

const nj_native_entry nj_lib_nokia_table[] = {
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
    { NJ_C_FSDETAIL, "getUrl",  "()Ljava/lang/String;", nokia_fsd_getUrl },
    { NJ_C_FSDETAIL, "getName", "()Ljava/lang/String;", nokia_fsd_getName },
    { NULL, NULL, NULL, NULL }
};
const int nj_lib_nokia_count =
    (int)(sizeof(nj_lib_nokia_table) / sizeof(nj_lib_nokia_table[0])) - 1;
