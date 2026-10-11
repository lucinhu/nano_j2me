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

NJFN(nokia_vk_hide)     { return NJ_OK; }
NJFN(nokia_vk_show)     { return NJ_OK; }
NJFN(nokia_vk_visible)  { ret->i = 0; return NJ_OK; }

#define M(name, desc, flags) { name, desc, flags }

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
    { NULL, NULL, 0u, NULL, 0u, NULL, 0u }
};
const int nj_lib_nokia_class_count =
    (int)(sizeof(nj_lib_nokia_classes) / sizeof(nj_lib_nokia_classes[0])) - 1;

const nj_native_entry nj_lib_nokia_table[] = {
    { NJ_C_VKEYBOARD, "hideOpenKeypadCommand", "(Z)V", nokia_vk_hide },
    { NJ_C_VKEYBOARD, "showOpenKeypadCommand", "(Z)V", nokia_vk_show },
    { NJ_C_VKEYBOARD, "setVisibility",         "(Z)V", nokia_vk_hide },
    { NJ_C_VKEYBOARD, "getVisibility",         "()Z",  nokia_vk_visible },
    { NULL, NULL, NULL, NULL }
};
const int nj_lib_nokia_count =
    (int)(sizeof(nj_lib_nokia_table) / sizeof(nj_lib_nokia_table[0])) - 1;
