/*
 * nano_j2me - nj_lib_midlet.c
 *
 * javax.microedition.midlet.MIDlet and the application-management loop.
 *
 * On a real phone the AMS owns the frame loop: it pumps input, tells the
 * current Displayable to paint, presents the result, and lets background
 * threads run in between.  This file is that loop.  It is what nj_run() calls
 * after it has created the VM and the MIDlet instance.
 *
 * The loop is deliberately a plain C loop rather than a Java one: a MIDlet that
 * never returns from startApp() must still let the scheduler run, and a MIDlet
 * that returns immediately must still get its Canvas painted.
 */
#include "nj_midp.h"
#include "nj2me/nj_app.h"

#if NJ_USE_LIBC
#  include <string.h>
#endif

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_UNUSED(x) ((void)(x))

/* ------------------------------------------------------------------ */
/* MIDlet                                                             */
/* ------------------------------------------------------------------ */
static nj_field *mf(nj_vm *vm, const char *name, const char *desc)
{
    return nj_field_lookup(vm, NJ_C_MIDLET, name, desc, 0);
}

NJFN(midlet_init)
{
    nj_field *f = mf(vm, NJ_F_M_STATE, "I");
    if (f) {
        nj_field_write_int(vm, NJ_THIS(args), f, NJ_MIDLET_UNINITIALISED);
    }
    return NJ_OK;
}

/*
 * startApp / pauseApp / destroyApp are abstract in the MIDP specification, so
 * a compliant MIDlet always overrides them.  They are given working defaults
 * here rather than being declared abstract: a game that calls super.startApp()
 * from its own override must not blow up with AbstractMethodError.
 */
NJFN(midlet_startApp)
{
    nj_field *f = mf(vm, NJ_F_M_STATE, "I");
    if (f) {
        nj_field_write_int(vm, NJ_THIS(args), f, NJ_MIDLET_ACTIVE);
    }
    return NJ_OK;
}

NJFN(midlet_pauseApp)
{
    nj_field *f = mf(vm, NJ_F_M_STATE, "I");
    if (f) {
        nj_field_write_int(vm, NJ_THIS(args), f, NJ_MIDLET_PAUSED);
    }
    return NJ_OK;
}

NJFN(midlet_destroyApp)
{
    nj_field *f = mf(vm, NJ_F_M_STATE, "I");
    if (f) {
        nj_field_write_int(vm, NJ_THIS(args), f, NJ_MIDLET_DESTROYED);
    }
    return NJ_OK;
}

NJFN(midlet_notifyDestroyed)
{
    nj_field *f = mf(vm, NJ_F_M_STATE, "I");
    if (f) {
        nj_field_write_int(vm, NJ_THIS(args), f, NJ_MIDLET_DESTROYED);
    }
    /* The MIDlet is asking the AMS to end the application. */
    nj_vm_stop_reason(vm, 0, "the MIDlet called notifyDestroyed()");
    return NJ_OK;
}

NJFN(midlet_notifyPaused)
{
    nj_field *f = mf(vm, NJ_F_M_STATE, "I");
    if (f) {
        nj_field_write_int(vm, NJ_THIS(args), f, NJ_MIDLET_PAUSED);
    }
    return NJ_OK;
}

NJFN(midlet_resumeRequest)
{
    /* Real AMS queues this until it decides to resume; the effect is the same
     * here because the loop never stops servicing the display. */
    return NJ_OK;
}

NJFN(midlet_getAppProperty)
{
    nj_ref key = NJ_ARG_REF(args, 0);
    nj_app *app = (nj_app *)vm->loader_ud;
    char k[128];
    char v[512];

    if (!key) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (!app) {
        ret->r = NULL;
        return NJ_OK;
    }
    if (nj_string_to_utf8(key, k, (int)sizeof(k)) <= 0) {
        ret->r = NULL;
        return NJ_OK;
    }
    if (nj_app_get_property(app, k, v, (nj_u4)sizeof(v)) != NJ_OK) {
        ret->r = NULL;
        return NJ_OK;
    }
    ret->r = nj_string_new(vm, v);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

NJFN(midlet_platformRequest)
{
    /* No browser, no dialler, no messaging stack: report honestly. */
    NJ_UNUSED(args);
    return nj_throw(vm, "java/lang/UnsupportedOperationException",
                    "platformRequest");
}

NJFN(midlet_checkPermission)
{
    /* Permissions are the AMS's business; grant everything. */
    ret->i = 1;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* The application loop                                               */
/* ------------------------------------------------------------------ */
nj_status nj_midp_destroy_app(nj_vm *vm, int unconditional)
{
    nj_ref app;
    nj_value a1[2];

    if (!vm) {
        return NJ_ERR;
    }

    /* Let go of the sensors first: a connection still open when the MIDlet
     * ends would otherwise leave the port's hardware running (an Android
     * SensorEventListener keeps waking the process up).  This runs even when
     * destroyApp() itself never returns - the runner cuts that short. */
    nj_sensor_shutdown(vm);
    nj_bt_shutdown(vm);

    app = vm->app;
    if (!app) {
        return NJ_OK;
    }
    a1[0].i = unconditional ? 1 : 0;
    (void)nj_call_special(vm, app, app->cls->name, "destroyApp", "(Z)V",
                          a1, NULL);
    if (vm->exception) {
        nj_ref e = nj_take_exception(vm);
        nj_logf(vm, 1, "destroyApp threw: %s",
                e && e->cls ? e->cls->name : "?");
    }
    vm->app = NULL;
    return NJ_OK;
}

/*
 * Frame pacing.  A MIDlet that just paints as fast as it can will starve the
 * event pump on a slow panel, and one that sleeps too long feels laggy; this
 * targets the port's fps_cap by measuring elapsed time between frames rather
 * than assuming a fixed cost.
 *
 * fps_cap == 0 means UNCAPPED, which is what nj_port.h has always documented -
 * and what this did not do.  It read `> 0` and left the default at 30, so a
 * caller that asked for no cap silently got 30 fps.  Every real-game timing in
 * this project's notes was taken through that, which is why they all read
 * exactly 30.2 fps whatever the game did.
 *
 * A negative cap is treated as uncapped too: there is no such thing as a
 * negative frame rate, and spinning is a better failure than dividing by it.
 */
static void frame_pace(nj_vm *vm, nj_s8 frame_start)
{
    int cap = vm->port ? vm->port->fps_cap : 0;
    nj_s8 budget;
    nj_s8 elapsed;

    if (cap <= 0) {
        /* Uncapped: no sleep at all, but still give the RTOS a chance to run
         * so a busy-wait does not starve everything else on the device. */
        if (vm->port && vm->port->idle) {
            vm->port->idle(vm->port->ud);
        }
        return;
    }
    budget = 1000 / cap;
    elapsed = nj_now_ms(vm) - frame_start;
    /*
     * Never sleep for longer than one frame budget.
     *
     * `elapsed` is a difference of two clock reads, and a clock that steps
     * backwards makes it negative - which turns `budget - elapsed` into a sleep
     * of hours.  A pacer that can park the emulator for hours is worse than one
     * that occasionally runs a frame early, so the wait is clamped to the
     * budget it came from.
     *
     * This is a safety bound, not a measured win: it changed nothing on the one
     * title that led here (that run is not stuck in the pacer at all - see the
     * commit message).
     */
    if (elapsed < budget && vm->port && vm->port->sleep_ms) {
        nj_s8 wait = budget - elapsed;
        if (wait > budget) {
            wait = budget;
        }
        vm->port->sleep_ms(vm->port->ud, (nj_u4)wait);
    } else if (vm->port && vm->port->idle) {
        /* Already fast enough: still give the RTOS a chance to run. */
        vm->port->idle(vm->port->ud);
    }
}

/*
 * One frame: drain input, let the background threads run, repaint the current
 * Displayable, present it, then pace.  Splitting this out is what lets an
 * embedder that already owns an event loop (Android, a game engine) drive the
 * MIDlet itself instead of handing control to nj_midp_run().
 */
nj_status nj_midp_frame(nj_vm *vm)
{
    nj_event ev;
    nj_s8 frame_start;

    if (!vm) {
        return NJ_ERR;
    }
    if (!nj_midp_screen(vm)) {
        return NJ_ERR;
    }
    frame_start = nj_now_ms(vm);

    /* ---- input ---- */
    if (vm->port && vm->port->input_poll) {
        int guard = 0;
        while (guard++ < 256 && vm->port->input_poll(vm->port->ud, &ev)) {
            switch (ev.type) {
            case NJ_EV_KEY_DOWN:     (void)nj_midp_key(vm, ev.key, 1); break;
            case NJ_EV_KEY_UP:       (void)nj_midp_key(vm, ev.key, 0); break;
            case NJ_EV_POINTER_DOWN: (void)nj_midp_pointer(vm, ev.x, ev.y, NJ_POINTER_DOWN); break;
            case NJ_EV_POINTER_UP:   (void)nj_midp_pointer(vm, ev.x, ev.y, NJ_POINTER_UP);   break;
            case NJ_EV_POINTER_MOVE: (void)nj_midp_pointer(vm, ev.x, ev.y, NJ_POINTER_MOVE); break;
            case NJ_EV_QUIT:         vm->running = 0u; break;
            default: break;
            }
        }
    }
    if (!vm->running) {
        return NJ_OK;
    }

    /* ---- background threads ---- */
    {
        nj_status st = nj_scheduler_step(vm);
        if (st != NJ_OK && st != NJ_ERR_HALT && st != NJ_ERR_YIELD &&
            st != NJ_ERR_TIMEOUT) {
            return st;
        }
    }
    if (nj_has_exception(vm)) {
        nj_logf(vm, 1, "exception escaped into the frame loop: %s",
                nj_exception_describe(vm));
        nj_clear_exception(vm);
    }

    /* Top up any Player that is still feeding the audio device.  A port whose
     * pcm_write() accepts everything finished on the first push and this costs
     * one call. */
    nj_media_frame(vm);

    /* Deliver sensor samples to any DataListener, at the same once-per-frame
     * cadence: a listener is a poll with a callback attached, and the spec
     * gives no better clock than the program's own frame. */
    nj_sensor_frame(vm);
    /* Bluetooth discovery jobs get the same clock: an inquiry is a poll the
     * port answers a slice at a time, and its listener hears about it here. */
    nj_bt_frame(vm);

    /* ---- paint ---- */
    /*
     * An exception raised while painting must not survive the frame.
     *
     * Nothing else clears vm->exception outside the interpreter, so one throw
     * in a paint path stays pending forever: the next frame's field lookups
     * fail, nj_midp_screen() returns NULL, paint bails out before presenting
     * anything, and the screen freezes on its last image with no error printed
     * anywhere.  Report it once, per frame, and carry on - a game that throws
     * in paint() every frame is broken, but it is broken *visibly*.
     */
    if (nj_has_exception(vm)) {
        nj_logf(vm, 1, "exception while painting the displayable: %s",
                nj_exception_describe(vm));
        nj_clear_exception(vm);
    }
    if (nj_midp_paint(vm) == NJ_OK) {
        vm->frames++;
        nj_surface *s = nj_midp_screen(vm);
        if (s && vm->port && vm->port->display_present) {
            vm->port->display_present(vm->port->ud, 0, 0, s->width, s->height);
        }
        vm->repaint_pending = 0u;
        vm->frames_presented++;
    }

    if (vm->state != NJ_VM_STOPPING) {
        frame_pace(vm, frame_start);
    }
    return NJ_OK;
}

nj_status nj_midp_run(nj_vm *vm)
{
    if (!vm) {
        return NJ_ERR;
    }
    while (vm->running && vm->state != NJ_VM_STOPPING) {
        nj_status st = nj_midp_frame(vm);
        if (st != NJ_OK) {
            return st;
        }
    }
    if (vm->frames_presented == 0u) {
        nj_logf(vm, 1, "the MIDlet never produced a frame");
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Tables                                                             */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }

static const nj_builtin_field midlet_fields[] = {
    { NJ_F_M_PROPERTIES, "Ljava/util/Hashtable;", NJ_ACC_PRIVATE },
    { NJ_F_M_STATE,      "I",                     NJ_ACC_PRIVATE }
};

static const nj_builtin_method midlet_methods[] = {
    M("<init>",            "()V",   NJ_ACC_PROTECTED),
    M("startApp",          "()V",   NJ_ACC_PROTECTED),
    M("pauseApp",          "()V",   NJ_ACC_PROTECTED),
    M("destroyApp",        "(Z)V",  NJ_ACC_PROTECTED),
    M("notifyDestroyed",   "()V",   NJ_ACC_PUBLIC),
    M("notifyPaused",      "()V",   NJ_ACC_PUBLIC),
    M("resumeRequest",     "()V",   NJ_ACC_PUBLIC),
    M("getAppProperty",    "(Ljava/lang/String;)Ljava/lang/String;", NJ_ACC_PUBLIC),
    M("platformRequest",   "(Ljava/lang/String;)Z", NJ_ACC_PUBLIC),
    M("checkPermission",   "(Ljava/lang/String;)Z", NJ_ACC_PUBLIC)
};

const nj_builtin_class nj_lib_midlet_classes[] = {
    { NJ_C_MIDLET, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      midlet_fields,
      (nj_u2)(sizeof(midlet_fields) / sizeof(midlet_fields[0])),
      midlet_methods,
      (nj_u2)(sizeof(midlet_methods) / sizeof(midlet_methods[0])) }
};
const int nj_lib_midlet_class_count = 1;

const nj_native_entry nj_lib_midlet_table[] = {
    { NJ_C_MIDLET, "<init>",          "()V",   midlet_init },
    { NJ_C_MIDLET, "startApp",        "()V",   midlet_startApp },
    { NJ_C_MIDLET, "pauseApp",        "()V",   midlet_pauseApp },
    { NJ_C_MIDLET, "destroyApp",      "(Z)V",  midlet_destroyApp },
    { NJ_C_MIDLET, "notifyDestroyed", "()V",   midlet_notifyDestroyed },
    { NJ_C_MIDLET, "notifyPaused",    "()V",   midlet_notifyPaused },
    { NJ_C_MIDLET, "resumeRequest",   "()V",   midlet_resumeRequest },
    { NJ_C_MIDLET, "getAppProperty",  "(Ljava/lang/String;)Ljava/lang/String;", midlet_getAppProperty },
    { NJ_C_MIDLET, "platformRequest", "(Ljava/lang/String;)Z", midlet_platformRequest },
    { NJ_C_MIDLET, "checkPermission", "(Ljava/lang/String;)Z", midlet_checkPermission }
};
const int nj_lib_midlet_count =
    (int)(sizeof(nj_lib_midlet_table) / sizeof(nj_lib_midlet_table[0]));
