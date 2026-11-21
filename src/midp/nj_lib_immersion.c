/*
 * nano_j2me - src/midp/nj_lib_immersion.c
 *
 * Immersion's com.immersion.VibeTonz: the vibration engine several 2007-2008
 * Gameloft-era titles link against from their GameCanvas static initialiser.
 *
 * WHY THIS EXISTS
 * ---------------
 * The class was absent, so `Dig_Dug_240x400` died in GameCanvas.<clinit> with
 * a NoClassDefFoundError before it could even install a Displayable.  That is
 * the same failure shape the Samsung/BT/PIM classes were added for: a device
 * without a vibration motor is a normal device, and a title that asks for one
 * should get the SDK's own answer, not a linkage error.
 *
 * WHAT A DEVICE WITHOUT A MOTOR CAN HONESTLY ANSWER
 * -------------------------------------------------
 * VibeTonz has no standardised status object: every method returns an int
 * error code or void.  OpenDevice() would return a non-zero device handle on a
 * handset with a motor; here it returns 0 (no device).  PlayPeriodicEffect()
 * and friends return 0 ("accepted") and do nothing, which is what the titles
 * check before continuing.  Nothing is fabricated: no vibration happens, and
 * the class never crashes the game's static initialiser.
 *
 * Only `Dig_Dug_240x400` in the corpus references it, and it calls
 * `new VibeTonz()`, `OpenDevice(int)`, `PlayPeriodicEffect(IIIIIIIII)` and
 * `SetDevicePropertyString(IIString)`.
 */
#include "nj_midp.h"

#if NJ_USE_LIBC
#  include <string.h>
#endif

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_UNUSED(x) ((void)(x))

#define NJ_C_VIBETONZ "com/immersion/VibeTonz"

/* The engine handle is not exposed: a title only passes it back to the static
 * calls above, which ignore it. */
NJFN(vibetonz_init)
{
    NJ_UNUSED(args);
    NJ_UNUSED(ret);
    return NJ_OK;
}

NJFN(vibetonz_open)
{
    NJ_UNUSED(args);
    ret->i = 0;                         /* no device opened */
    return NJ_OK;
}

NJFN(vibetonz_close)
{
    NJ_UNUSED(args);
    NJ_UNUSED(ret);
    return NJ_OK;
}

NJFN(vibetonz_play_periodic)
{
    NJ_UNUSED(args);
    ret->i = 0;
    return NJ_OK;
}

NJFN(vibetonz_stop_periodic)
{
    NJ_UNUSED(args);
    NJ_UNUSED(ret);
    return NJ_OK;
}

NJFN(vibetonz_set_string)
{
    NJ_UNUSED(args);
    NJ_UNUSED(ret);
    return NJ_OK;
}

NJFN(vibetonz_set_int)
{
    NJ_UNUSED(args);
    NJ_UNUSED(ret);
    return NJ_OK;
}

NJFN(vibetonz_get_int)
{
    NJ_UNUSED(args);
    ret->i = 0;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }

static const nj_builtin_method vibetonz_methods[] = {
    M("<init>", "()V", 0),
    M("OpenDevice", "(I)I", 0),
    M("CloseDevice", "()V", 0),
    M("PlayPeriodicEffect", "(IIIIIIIII)I", 0),
    M("StopPeriodicEffect", "(I)V", 0),
    M("SetDevicePropertyString", "(IILjava/lang/String;)V", 0),
    M("SetDevicePropertyInt", "(III)V", 0),
    M("GetDevicePropertyInt", "(II)I", 0)
};

const nj_builtin_class nj_lib_immersion_classes[] = {
    { NJ_C_VIBETONZ, "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, vibetonz_methods,
      (nj_u2)(sizeof(vibetonz_methods) / sizeof(vibetonz_methods[0])) }
};
const int nj_lib_immersion_class_count =
    (int)(sizeof(nj_lib_immersion_classes) /
          sizeof(nj_lib_immersion_classes[0]));

const nj_native_entry nj_lib_immersion_table[] = {
    { NJ_C_VIBETONZ, "<init>", "()V", vibetonz_init },
    { NJ_C_VIBETONZ, "OpenDevice", "(I)I", vibetonz_open },
    { NJ_C_VIBETONZ, "CloseDevice", "()V", vibetonz_close },
    { NJ_C_VIBETONZ, "PlayPeriodicEffect", "(IIIIIIIII)I", vibetonz_play_periodic },
    { NJ_C_VIBETONZ, "StopPeriodicEffect", "(I)V", vibetonz_stop_periodic },
    { NJ_C_VIBETONZ, "SetDevicePropertyString", "(IILjava/lang/String;)V",
      vibetonz_set_string },
    { NJ_C_VIBETONZ, "SetDevicePropertyInt", "(III)V", vibetonz_set_int },
    { NJ_C_VIBETONZ, "GetDevicePropertyInt", "(II)I", vibetonz_get_int }
};
const int nj_lib_immersion_count =
    (int)(sizeof(nj_lib_immersion_table) / sizeof(nj_lib_immersion_table[0]));
