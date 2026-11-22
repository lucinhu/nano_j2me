/*
 * nano_j2me - src/midp/nj_lib_sprint.c
 *
 * Sprint PCS extensions: `com.sprintpcs.media`, `com.sprintpcs.util`.
 *
 * WHY THIS EXISTS
 * ---------------
 * The Pac-Man mobile archive in the fetched corpus contains the Sprint PCS
 * builds (PAC-MAN, Ms. PAC-MAN, PAC-MANIA 3D).  They are ordinary MIDlets
 * that only need the carrier's small media/vibrator wrapper to link: without
 * `com/sprintpcs/media/Player` the link step of `MsPacman.<init>` throws
 * `NoClassDefFoundError`, so the title never reaches its paint loop.  Three
 * more fetched retail titles (Far Cry 2, Platinum Sudoku, Real Football
 * Manager) only touch `com/sprintpcs.util.System`.
 *
 * WHAT IS IMPLEMENTED
 * -------------------
 * The exact surface the corpus references (checked with javap over every
 * class of the seven titles): the two Clip constructors and
 * getContentType(); Player's static addPlayerListener/play/stop; the
 * PlayerListener interface and its playerUpdate() method; Vibrator.vibrate();
 * Muglet's getMuglet/getURI/getMediaType/getReferringURI; and System's small
 * static utility set.  Clip stores its URI/data, content type, priority and
 * vibration count and validates nothing (the handset class does not either);
 * Player validates the repeat count (IllegalArgumentException for < -1) and
 * keeps the listener reference.
 *
 * Audio through this wrapper is NOT emulated: the reference implementation
 * delegates to javax.microedition.media, and the titles pass `resource:` URIs
 * that our Manager does not open as media.  Rather than throwing a
 * MediaException the game does not expect, play()/stop() are silent no-ops;
 * a title keeps running with no sound through this API (its own MIDI/WAV
 * paths are unaffected).  The vibration motor is likewise absent: vibrate()
 * is a no-op.  Both are documented limitations, not silent corruption.
 */
#include "nj_midp.h"

#define NJFN(name) NJ_NATIVE_FN(name)

#define NJ_C_SPCLIP   "com/sprintpcs/media/Clip"
#define NJ_C_SPPLAYER "com/sprintpcs/media/Player"
#define NJ_C_SPLISTEN "com/sprintpcs/media/PlayerListener"
#define NJ_C_SPVIB    "com/sprintpcs/media/Vibrator"
#define NJ_C_SPMUGLET "com/sprintpcs/util/Muglet"
#define NJ_C_SPSYS    "com/sprintpcs/util/System"

static nj_field *sprint_f(nj_vm *vm, const char *cls, const char *name,
                          const char *desc, int is_static)
{
    return nj_field_lookup(vm, cls, name, desc, is_static);
}

/* ------------------------------------------------------------------ */
/* Clip                                                                */
/* ------------------------------------------------------------------ */
NJFN(sprint_clip_init_uri)
{
    nj_field *fu = sprint_f(vm, NJ_C_SPCLIP, "cUri", "Ljava/lang/String;", 0);
    nj_field *ft = sprint_f(vm, NJ_C_SPCLIP, "cType", "Ljava/lang/String;", 0);
    nj_field *fp = sprint_f(vm, NJ_C_SPCLIP, "cPriority", "I", 0);
    nj_field *fv = sprint_f(vm, NJ_C_SPCLIP, "cVibration", "I", 0);
    (void)th;
    nj_rput(vm, NJ_THIS(args), fu, NJ_ARG_REF(args, 0));
    nj_rput(vm, NJ_THIS(args), ft, NJ_ARG_REF(args, 1));
    nj_iput(vm, NJ_THIS(args), fp, NJ_ARG_INT(args, 2));
    nj_iput(vm, NJ_THIS(args), fv, NJ_ARG_INT(args, 3));
    return NJ_OK;
}

NJFN(sprint_clip_init_data)
{
    nj_field *fd = sprint_f(vm, NJ_C_SPCLIP, "cData", "[B", 0);
    nj_field *ft = sprint_f(vm, NJ_C_SPCLIP, "cType", "Ljava/lang/String;", 0);
    nj_field *fp = sprint_f(vm, NJ_C_SPCLIP, "cPriority", "I", 0);
    nj_field *fv = sprint_f(vm, NJ_C_SPCLIP, "cVibration", "I", 0);
    (void)th;
    nj_rput(vm, NJ_THIS(args), fd, NJ_ARG_REF(args, 0));
    nj_rput(vm, NJ_THIS(args), ft, NJ_ARG_REF(args, 1));
    nj_iput(vm, NJ_THIS(args), fp, NJ_ARG_INT(args, 2));
    nj_iput(vm, NJ_THIS(args), fv, NJ_ARG_INT(args, 3));
    return NJ_OK;
}

NJFN(sprint_clip_get_type)
{
    nj_field *ft = sprint_f(vm, NJ_C_SPCLIP, "cType", "Ljava/lang/String;", 0);
    (void)th;
    ret->r = nj_rget(vm, NJ_THIS(args), ft);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Player                                                              */
/* ------------------------------------------------------------------ */
NJFN(sprint_player_init)
{
    (void)args; (void)ret; (void)th; (void)vm;
    return NJ_OK;
}

NJFN(sprint_player_add_listener)
{
    nj_field *fl = sprint_f(vm, NJ_C_SPPLAYER, "sListener",
                            "L" NJ_C_SPLISTEN ";", 1);
    (void)th;
    nj_field_write_ref(vm, NULL, fl, NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(sprint_player_play)
{
    nj_ref clip = NJ_ARG_REF(args, 0);
    nj_s4 repeat = NJ_ARG_INT(args, 1);
    nj_field *fc = sprint_f(vm, NJ_C_SPPLAYER, "sCurrent", "Ljava/lang/Object;", 1);
    (void)th;
    if (repeat < -1) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "repeat must be -1 or greater");
    }
    /* No audio through the carrier wrapper (see the file comment); keep the
     * clip so a later stop() has something meaningful to clear. */
    nj_field_write_ref(vm, NULL, fc, clip);
    return NJ_OK;
}

NJFN(sprint_player_stop)
{
    nj_field *fc = sprint_f(vm, NJ_C_SPPLAYER, "sCurrent", "Ljava/lang/Object;", 1);
    (void)args; (void)ret; (void)th;
    if (fc != NULL) {
        nj_field_write_ref(vm, NULL, fc, NULL);
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* PlayerListener constants are inlined by javac, so only the method    */
/* needs to exist for a class that implements the interface to link.   */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Vibrator: there is no motor here.                                   */
/* ------------------------------------------------------------------ */
NJFN(sprint_vibrator_vibrate)
{
    (void)args; (void)ret; (void)th; (void)vm;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Muglet                                                              */
/* ------------------------------------------------------------------ */
NJFN(sprint_muglet_init)
{
    (void)args; (void)ret; (void)th; (void)vm;
    return NJ_OK;
}

NJFN(sprint_muglet_get)
{
    nj_ref m;
    (void)args; (void)th;
    nj_u4 mark = nj_root_mark(vm);

    m = nj_root(vm, nj_object_new(vm, NJ_C_SPMUGLET));
    if (m == NULL) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    if (nj_call_special(vm, m, NJ_C_SPMUGLET, "<init>", "()V", NULL, NULL)
        != NJ_OK) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    ret->r = m;
    nj_root_release(vm, mark);
    return NJ_OK;
}

NJFN(sprint_muglet_media_type)
{
    (void)args; (void)th; (void)vm;
    ret->r = NULL;
    return NJ_OK;
}

NJFN(sprint_muglet_uri)
{
    (void)args; (void)th;
    ret->r = nj_string_new(vm, "");
    return NJ_OK;
}

NJFN(sprint_muglet_referring)
{
    (void)args; (void)th; (void)vm;
    ret->r = NULL;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* System                                                              */
/* ------------------------------------------------------------------ */
NJFN(sprint_sys_set_exit_uri)
{
    (void)args; (void)ret; (void)th; (void)vm;
    return NJ_OK;
}

NJFN(sprint_sys_get_state)
{
    (void)args; (void)th;
    ret->r = nj_string_new(vm, "");
    return NJ_OK;
}

NJFN(sprint_sys_get_props)
{
    (void)args; (void)th; (void)vm;
    ret->r = NULL;
    return NJ_OK;
}

NJFN(sprint_sys_noop)
{
    (void)args; (void)ret; (void)th; (void)vm;
    return NJ_OK;
}

NJFN(sprint_sys_get_protected)
{
    (void)args; (void)th; (void)vm;
    ret->r = NULL;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Class and native tables                                             */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }

static const nj_builtin_field clip_fields[] = {
    { "cUri",       "Ljava/lang/String;", 0 },
    { "cType",      "Ljava/lang/String;", 0 },
    { "cData",      "[B",                 0 },
    { "cPriority",  "I",                  0 },
    { "cVibration", "I",                  0 }
};

static const nj_builtin_method clip_methods[] = {
    M("<init>",           "(Ljava/lang/String;Ljava/lang/String;II)V",
      NJ_ACC_PUBLIC),
    M("<init>",           "([BLjava/lang/String;II)V", NJ_ACC_PUBLIC),
    M("getContentType",   "()Ljava/lang/String;", NJ_ACC_PUBLIC)
};

static const nj_builtin_field player_fields[] = {
    { "sListener", "L" NJ_C_SPLISTEN ";", NJ_ACC_STATIC },
    { "sCurrent",  "Ljava/lang/Object;",  NJ_ACC_STATIC }
};

static const nj_builtin_method player_methods[] = {
    M("<init>",              "()V", NJ_ACC_PUBLIC),
    M("addPlayerListener",   "(L" NJ_C_SPLISTEN ";)V", NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("play",                "(L" NJ_C_SPCLIP ";I)V",
      NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("stop",                "()V", NJ_ACC_PUBLIC | NJ_ACC_STATIC)
};

/* Interface constants are INLINED by javac at every use site, so a MIDlet
 * carries the value the compiler saw rather than asking the runtime.  They
 * must still exist in the class table with the same values: tests/test_consts.c
 * checks the stubs and the runtime cannot drift apart (see the List.IMPLICIT
 * incident in that file's header). */
#define D_INT "I"
static const nj_builtin_field listener_fields[] = {
    { "AUDIO_DEVICE_UNAVAILABLE", D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "END_OF_DATA",              D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "ERROR",                    D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "STARTED",                  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "STOPPED",                  D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "PAUSED",                   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "RESUME",                   D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "PREEMPTED",                D_INT, NJ_ACC_STATIC | NJ_ACC_FINAL }
};

static const nj_builtin_method listener_methods[] = {
    M("playerUpdate", "(ILjava/lang/Object;)V", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT)
};

static const nj_builtin_method vibrator_methods[] = {
    M("vibrate", "(I)V", NJ_ACC_PUBLIC | NJ_ACC_STATIC)
};

static const nj_builtin_method muglet_methods[] = {
    M("<init>",           "()V", NJ_ACC_PUBLIC),
    M("getMuglet",        "()L" NJ_C_SPMUGLET ";", NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("getMediaType",     "()Ljava/lang/String;", NJ_ACC_PUBLIC),
    M("getURI",           "()Ljava/lang/String;", NJ_ACC_PUBLIC),
    M("getReferringURI",  "()Ljava/lang/String;", NJ_ACC_PUBLIC)
};

static const nj_builtin_method sys_methods[] = {
    M("setExitURI",         "(Ljava/lang/String;)V", NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("getSystemState",     "(Ljava/lang/String;)Ljava/lang/String;",
      NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("addSystemListener",  "(Ljava/lang/Object;)V", NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("getPropertiesList",  "()[Ljava/lang/String;", NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("setSystemSetting",   "(Ljava/lang/String;Ljava/lang/String;)V",
      NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("getProtectedProperty", "(Ljava/lang/String;)Ljava/lang/String;",
      NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("promptMasterVolume", "()V", NJ_ACC_PUBLIC | NJ_ACC_STATIC)
};

const nj_builtin_class nj_lib_sprint_classes[] = {
    { NJ_C_SPCLIP, "java/lang/Object", NJ_ACC_PUBLIC,
      clip_fields,
      (nj_u2)(sizeof(clip_fields) / sizeof(clip_fields[0])),
      clip_methods,
      (nj_u2)(sizeof(clip_methods) / sizeof(clip_methods[0])) },
    { NJ_C_SPPLAYER, "java/lang/Object", NJ_ACC_PUBLIC,
      player_fields,
      (nj_u2)(sizeof(player_fields) / sizeof(player_fields[0])),
      player_methods,
      (nj_u2)(sizeof(player_methods) / sizeof(player_methods[0])) },
    { NJ_C_SPLISTEN, NULL,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      listener_fields,
      (nj_u2)(sizeof(listener_fields) / sizeof(listener_fields[0])),
      listener_methods,
      (nj_u2)(sizeof(listener_methods) / sizeof(listener_methods[0])) },
    { NJ_C_SPVIB, "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, vibrator_methods,
      (nj_u2)(sizeof(vibrator_methods) / sizeof(vibrator_methods[0])) },
    { NJ_C_SPMUGLET, "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, muglet_methods,
      (nj_u2)(sizeof(muglet_methods) / sizeof(muglet_methods[0])) },
    { NJ_C_SPSYS, "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, sys_methods,
      (nj_u2)(sizeof(sys_methods) / sizeof(sys_methods[0])) }
};
const int nj_lib_sprint_class_count =
    (int)(sizeof(nj_lib_sprint_classes) / sizeof(nj_lib_sprint_classes[0]));

const nj_native_entry nj_lib_sprint_table[] = {
    { NJ_C_SPCLIP, "<init>", "(Ljava/lang/String;Ljava/lang/String;II)V",
      sprint_clip_init_uri },
    { NJ_C_SPCLIP, "<init>", "([BLjava/lang/String;II)V",
      sprint_clip_init_data },
    { NJ_C_SPCLIP, "getContentType", "()Ljava/lang/String;",
      sprint_clip_get_type },
    { NJ_C_SPPLAYER, "<init>", "()V", sprint_player_init },
    { NJ_C_SPPLAYER, "addPlayerListener", "(L" NJ_C_SPLISTEN ";)V",
      sprint_player_add_listener },
    { NJ_C_SPPLAYER, "play", "(L" NJ_C_SPCLIP ";I)V", sprint_player_play },
    { NJ_C_SPPLAYER, "stop", "()V", sprint_player_stop },
    { NJ_C_SPVIB, "vibrate", "(I)V", sprint_vibrator_vibrate },
    { NJ_C_SPMUGLET, "<init>", "()V", sprint_muglet_init },
    { NJ_C_SPMUGLET, "getMuglet", "()L" NJ_C_SPMUGLET ";", sprint_muglet_get },
    { NJ_C_SPMUGLET, "getMediaType", "()Ljava/lang/String;",
      sprint_muglet_media_type },
    { NJ_C_SPMUGLET, "getURI", "()Ljava/lang/String;", sprint_muglet_uri },
    { NJ_C_SPMUGLET, "getReferringURI", "()Ljava/lang/String;",
      sprint_muglet_referring },
    { NJ_C_SPSYS, "setExitURI", "(Ljava/lang/String;)V",
      sprint_sys_set_exit_uri },
    { NJ_C_SPSYS, "getSystemState", "(Ljava/lang/String;)Ljava/lang/String;",
      sprint_sys_get_state },
    { NJ_C_SPSYS, "addSystemListener", "(Ljava/lang/Object;)V",
      sprint_sys_noop },
    { NJ_C_SPSYS, "getPropertiesList", "()[Ljava/lang/String;",
      sprint_sys_get_props },
    { NJ_C_SPSYS, "setSystemSetting", "(Ljava/lang/String;Ljava/lang/String;)V",
      sprint_sys_noop },
    { NJ_C_SPSYS, "getProtectedProperty", "(Ljava/lang/String;)Ljava/lang/String;",
      sprint_sys_get_protected },
    { NJ_C_SPSYS, "promptMasterVolume", "()V", sprint_sys_noop }
};
const int nj_lib_sprint_count =
    (int)(sizeof(nj_lib_sprint_table) / sizeof(nj_lib_sprint_table[0]));

/* ------------------------------------------------------------------ */
/* Late initialisation                                                 */
/* ------------------------------------------------------------------ */
static void sprint_const_int(nj_vm *vm, const char *name, nj_s4 value)
{
    nj_field *f = nj_field_lookup(vm, NJ_C_SPLISTEN, name, "I", 1);

    if (f != NULL) {
        nj_field_write_int(vm, NULL, f, value);
    }
}

nj_status nj_sprint_late_init(nj_vm *vm)
{
    if (vm == NULL) {
        return NJ_ERR;
    }
    sprint_const_int(vm, "AUDIO_DEVICE_UNAVAILABLE", 0);
    sprint_const_int(vm, "END_OF_DATA",              1);
    sprint_const_int(vm, "ERROR",                    2);
    sprint_const_int(vm, "STARTED",                  3);
    sprint_const_int(vm, "STOPPED",                  4);
    sprint_const_int(vm, "PAUSED",                   5);
    sprint_const_int(vm, "RESUME",                   6);
    sprint_const_int(vm, "PREEMPTED",                7);
    return NJ_OK;
}
