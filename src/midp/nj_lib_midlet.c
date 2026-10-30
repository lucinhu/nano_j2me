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
    /*
     * No browser, no dialler, no messaging stack.  MIDP 2.0 lets a platform
     * that cannot handle the URL return false, and that is what the reference
     * VM does; the old UnsupportedOperationException was a non-declared
     * runtime exception that killed any title not wrapping the call.
     * Three corpus titles call this and continue on false.
     */
    NJ_UNUSED(args);
    ret->i = 0;
    return NJ_OK;
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
/*
 * strcmp without libc: the freestanding build (NJ_USE_LIBC=0) has its own
 * declarations in nj_libc.h, and pulling in <string.h> there ends in a
 * `_Generic` redefinition.  Only class names are compared, so a small walk is
 * enough.
 */
static int mscexc_named(const nj_class *cls)
{
    const char *want = NJ_C_MSCEXC;
    const char *have;

    if (!cls || !cls->name) {
        return 0;
    }
    have = cls->name;
    while (*want && *want == *have) {
        want++;
        have++;
    }
    return *want == '\0' && *have == '\0';
}

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

        /*
         * A MIDlet may refuse a destroy it was not told is unconditional, and
         * the specification says exactly that: destroyApp(false) throwing
         * MIDletStateChangeException means "not now, I have state to save".
         * We call with false on purpose (several titles flush their
         * RecordStore there and never do it otherwise), so when the MIDlet
         * refuses we ask once more, unconditionally - the AMS is going away
         * whether it agrees or not.
         */
        if (e && !unconditional && mscexc_named(e->cls)) {
            nj_logf(vm, 1, "destroyApp refused; retrying unconditionally");
            a1[0].i = 1;
            (void)nj_call_special(vm, app, app->cls->name, "destroyApp",
                                  "(Z)V", a1, NULL);
            if (vm->exception) {
                e = nj_take_exception(vm);
                nj_logf(vm, 1, "destroyApp threw again: %s",
                        e && e->cls ? e->cls->name : "?");
            }
        }
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
/*
 * Spend `wait` MIDlet milliseconds without parking the MIDlet's own threads.
 *
 * A phone's display refresh does not stop the application's other threads: a
 * title that builds its world, streams a file or runs its game loop in a
 * second thread keeps the CPU while the display is paced.  Sleeping here used
 * to park the whole VM, so such a thread only ran in the frame pump's 8 ms
 * slice and got 8/33 of the CPU at 30 fps - Minecraft's world generation took
 * four times as long as the same work uncapped, which is what a player sees as
 * "very low frame rate" during loading.
 *
 * Sleep only when nothing can run; with a scheduler thread present the gap is
 * spent executing it and the frame period stays the same.
 */
static void frame_pace_wait(nj_vm *vm, nj_s8 wait)
{
    nj_s8 deadline;

    if (wait <= 0 || !vm->port) {
        return;
    }
    deadline = nj_now_ms(vm) + wait;
    while (nj_vm_running(vm) && nj_now_ms(vm) < deadline) {
        nj_s8 left = deadline - nj_now_ms(vm);

        if (nj_scheduler_has_threads(vm)) {
            /*
             * Even a thread that is asleep needs the scheduler: its deadline
             * only passes because the scheduler advances it, and a loader
             * sleeps between chunks.  The budget only bounds how long one call
             * may spend before the frame's wall clock is checked again.
             */
            nj_status st = nj_run_pending(vm,
                (nj_u4)(left > 4 ? 4 : (left > 1 ? left : 1)));
            if (st != NJ_OK && st != NJ_ERR_TIMEOUT) {
                break;
            }
        } else if (vm->port->sleep_ms) {
            /* Nothing to run: park the host, waking at the frame boundary or
             * to look for a thread whose sleep expired. */
            vm->port->sleep_ms(vm->port->ud,
                               (nj_u4)(left > 2 ? 2 : (left > 0 ? left : 1)));
        } else if (vm->port->idle) {
            vm->port->idle(vm->port->ud);
        } else {
            break;
        }
    }
}

static void frame_pace(nj_vm *vm, nj_s8 frame_start)
{
    int cap = vm->port ? vm->port->fps_cap : 0;
    nj_s8 budget_ms;
    nj_s8 elapsed;

    if (cap <= 0) {
        /* Uncapped: no sleep at all, but still give the RTOS a chance to run
         * so a busy-wait does not starve everything else on the device. */
        if (vm->port && vm->port->idle) {
            vm->port->idle(vm->port->ud);
        }
        return;
    }
    /*
     * The budget is kept in microseconds even though the MIDlet's clock is
     * milliseconds: 1000 / 60 is 16, and a pacer that waits 16 ms every frame
     * runs a 60 fps cap at 62.5.  The absolute deadline below carries the
     * remainder, so the callers of this loop see the cap they asked for.
     */
    budget_ms = 1000 / cap;
    elapsed = nj_now_ms(vm) - frame_start;
    if (vm->port && !vm->port->sleep_ms) {
        /* Nothing that can wait: still give the RTOS a chance to run. */
        if (vm->port->idle) {
            vm->port->idle(vm->port->ud);
        }
        return;
    }
    if (vm->port) {
        int speed = (vm->speed_percent > 0) ? vm->speed_percent : 100;
        nj_s8 now = nj_now_ms(vm);
        nj_s8 wait = 0;

        if (speed != 100) {
            /*
             * `wait` is MIDlet time and sleep_ms() is real time: at 200% a
             * 33 ms frame takes 16 ms of the player's life, which is the whole
             * point of the multiplier.  Never round the wait away entirely - a
             * frame of sleep is what keeps the emulator from spinning a core
             * flat out.  The scaled clock moves at `speed` per cent of real
             * time, so the absolute schedule below only lines up when it is
             * running at 100; a scaled frame is therefore paced on its own.
             */
            vm->frame_due_us = 0;
            if (elapsed < budget_ms) {
                /*
                 * Never sleep for longer than one frame budget.
                 *
                 * `elapsed` is a difference of two clock reads, and a clock
                 * that stepped backwards makes it negative - which turns
                 * `budget - elapsed` into a sleep of hours.  A pacer that can
                 * park the emulator for hours is worse than one that runs a
                 * frame early, so the wait is clamped to the budget it came
                 * from.
                 */
                wait = budget_ms - elapsed;
                wait = wait * 100 / speed;
                if (wait < 1) {
                    wait = 1;
                }
            }
        } else {
            nj_s8 budget_us = 1000000 / cap;
            nj_s8 now_us = now * 1000;
            nj_s8 due = vm->frame_due_us;

            /*
             * Aim at an absolute deadline, not at a fresh budget per frame.
             *
             * A frame's work does not always stop at the boundary - a native
             * call one pump slice into the wait cannot be preempted - so every
             * frame loses its overshoot and the rate settles under the cap
             * (Minecraft at --fps 30 ran at 27.4).  Carrying the deadline lets
             * the next frame's wait absorb the overshoot, so the average lands
             * on the cap and only work that genuinely needs longer runs below
             * it.
             */
            if (due == 0) {
                due = frame_start * 1000 + budget_us;
            }
            /*
             * Bounds on the schedule, both of which a fault can reach.
             *
             * A deadline further ahead than a few frames is not work: it is a
             * backwards clock step or a cap that changed under us, and waiting
             * for it would park the emulator.  A deadline more than one frame
             * behind is debt too old to repay - it would turn the cap off, so
             * the schedule restarts a frame behind now and catches up from
             * there.  Between the two, being a little late is carried: the
             * next frame waits less and the average period lands on the
             * budget, which is the whole reason the deadline is absolute.
             */
            if (due > now_us + budget_us * 4) {
                due = now_us + budget_us;
            } else if (due < now_us - budget_us) {
                due = now_us - budget_us;
            }
            wait = (due - now_us) / 1000;
            if (wait > budget_ms) {
                wait = budget_ms;
            }
            vm->frame_due_us = due + budget_us;
        }
        if (wait > 0) {
            frame_pace_wait(vm, wait);
        } else if (vm->port->idle) {
            vm->port->idle(vm->port->ud);
        }
    }
}

/*
 * One frame: drain input, let the background threads run, repaint the current
 * Displayable, present it, then pace.  Splitting this out is what lets an
 * embedder that already owns an event loop (Android, a game engine) drive the
 * MIDlet itself instead of handing control to nj_midp_run().
 */
/* Sink for the escaped-exception diagnostic: nj_vm_describe_threads() writes
 * "where: thread ..." headers and "where:   Method pc=N" frame lines; a stack
 * trace wants the frames alone, indented like Throwable.printStackTrace(). */
static void escape_trace(void *ud, const char *line)
{
    nj_vm *vm = (nj_vm *)ud;
    const char *p = line;

    if (strncmp(p, "where: ", 7) == 0) {
        p += 7;
    }
    if (strncmp(p, "thread ", 7) == 0) {
        return;                 /* one per thread; the frames are the point */
    }
    nj_logf(vm, 1, "    at %s", p);
}

nj_status nj_midp_frame(nj_vm *vm)
{
    nj_event ev;
    nj_s8 frame_start;

    if (!vm) {
        return NJ_ERR;
    }
    /*
     * A paint is atomic, but only against itself: a frame pumped while one is
     * being delivered must not deliver another paint().
     *
     * vm_invoke() pumps frames while a Java thread is parked in a blocking
     * native (nj_vm.c, the frame hook) so a MIDlet that builds its UI inside
     * startApp() - or runs its own loop there - still presents.  If the parked
     * thread is inside paint() instead (a title that loads assets from paint()
     * and sleeps between asset reads), that pump used to run a whole nested
     * frame on top of the still-executing paint; the nested frame cleared
     * vm->painted_in_frame, so the next pump pass re-entered paint() itself.
     * 街头霸王2冠军典藏版_1812 keeps its loader state machine in paint() and
     * hung because of exactly that.
     *
     * The nested frame itself is fine and is still run: its input drain, its
     * scheduler slice and its present are what keep the screen and the other
     * Java threads alive while the paint blocks.  Only the paint is skipped
     * (see the paint gate below).  Refusing the whole frame here instead fixed
     * the hang but broke 纽约人生_1339: that title parks its loader inside
     * paint() across a blocking read, so every pumped frame was refused, yet
     * run_frame_hook() still counted each refusal as a frame - the run spent
     * its entire budget on refused pumps, presented one picture, and the
     * corpus recorded "nothing was ever presented" for a title that was still
     * loading normally.
     */
    if (!nj_midp_screen(vm)) {
        return NJ_ERR;
    }
    frame_start = nj_now_ms(vm);

    /* ---- input ---- */
    nj_midp_text_input_sync(vm);
    if (vm->port && vm->port->input_poll) {
        int guard = 0;
        while (guard++ < 256 && vm->port->input_poll(vm->port->ud, &ev)) {
            switch (ev.type) {
            case NJ_EV_KEY_DOWN:     (void)nj_midp_key_char(vm, ev.key, 1, ev.key_char); break;
            case NJ_EV_KEY_UP:       (void)nj_midp_key_char(vm, ev.key, 0, ev.key_char); break;
            case NJ_EV_KEY_REPEAT:   (void)nj_midp_key_repeat(vm, ev.key); break;
            case NJ_EV_POINTER_DOWN: (void)nj_midp_pointer(vm, ev.x, ev.y, NJ_POINTER_DOWN); break;
            case NJ_EV_POINTER_UP:   (void)nj_midp_pointer(vm, ev.x, ev.y, NJ_POINTER_UP);   break;
            case NJ_EV_POINTER_MOVE: (void)nj_midp_pointer(vm, ev.x, ev.y, NJ_POINTER_MOVE); break;
            case NJ_EV_TEXT_INPUT:
            case NJ_EV_TEXT_EDIT:
            case NJ_EV_TEXT_REPLACE:
                ev.text[NJ_TEXT_INPUT_BYTES - 1] = '\0';
                (void)nj_midp_text_event(vm, &ev); break;
            /*
             * Say who asked for the stop.
             *
             * A quit request is an end, not a failure.  Without the reason,
             * a run that ended while its MIDlet was still being constructed -
             * Doom 3 runs the whole game inside its constructor, so closing
             * the window happens there - reached the runner's "cannot
             * instantiate" branch and was reported as a java/lang/InternalError
             * raised by a MIDlet that was never even created.
             * nj_vm_stop_reason() also clears vm->running and moves the VM to
             * NJ_VM_STOPPING, which is what keeps teardown's destroyApp() call
             * legal.
             */
            case NJ_EV_QUIT:
                nj_vm_stop_reason(vm, 0, "the user asked the app to quit");
                break;
            default: break;
            }
            nj_midp_text_input_sync(vm);
        }
    }
    if (!vm->running) {
        return NJ_OK;
    }

    /* ---- background threads ---- */
    {
        nj_status st = nj_scheduler_frame(vm, 8u);
        if (st != NJ_OK && st != NJ_ERR_HALT && st != NJ_ERR_YIELD &&
            st != NJ_ERR_TIMEOUT) {
            return st;
        }
    }
    if (nj_has_exception(vm)) {
        nj_logf(vm, 1, "exception escaped into the frame loop: %s",
                nj_exception_describe(vm));
        /*
         * Where it came from: the frames that are still on the threads, in the
         * shape of a Java stack trace.  Best effort - the frames the exception
         * unwound through are gone - but it is what a triage otherwise has to
         * recover by disassembling the class named above by hand, which is how
         * the two "escaped" entries in tools/corpus_fatal_known.txt were found.
         * The usual log throttle bounds a title that throws every frame.
         */
        nj_logf(vm, 1, "    frames at the frame boundary (the throwing "
                       "thread may already have unwound):");
        nj_vm_describe_threads(vm, escape_trace, vm);
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
    /*
     * A title that painted synchronously this frame - repaint() followed by
     * serviceRepaints() - has already drawn and presented the frame, so do not
     * paint on top of it.  MIDP paints on request, and the state a title
     * publishes for its paint() only lives across that request: 仙魔飞行棋_417
     * keeps its paint state in mobile/k.f and does
     *
     *     f = state; repaint(); serviceRepaints(); f = 0;
     *
     * so a second, unconditional paint in the same frame finds f back at 0 and
     * wipes what the title just drew.
     *
     * A title that never paints synchronously keeps the per-frame paint: it is
     * the only thing that draws it at all (炸弹人_10311 never calls repaint()
     * from its loop).
     */
    if (!vm->paint_active &&
        !vm->painted_in_frame &&
        (!vm->app_painted || vm->app_paint_idle > vm->app_paint_period)) {
        (void)nj_midp_paint(vm);
    }
    /*
     * ONE present per frame, after everything this frame drew.
     *
     * It used to happen at the end of the paint path, which is not the end of
     * the drawing: 合金弹头2013BT版_678 paints its HUD inside paint() and then
     * keeps drawing with the Graphics it kept from paint() (the out-of-spec
     * but universal habit), so the mid-paint present showed the frame with the
     * scenery layer missing and the previous frame's ground left in the band it
     * would have covered - visible as scenery flickering every other present.
     * The port is told once per frame instead, which is also what a device
     * does: present() shows the finished frame.
     */
    vm->frames++;
    /* The application paints its own frames when it wants to, and the runner
     * must not paint the frames in between: two painters at different states is
     * the 合金弹头2013BT版_678 flicker.  A title that painted once and went quiet
     * is a different thing - 开心农场BT版_6075 paints twice at startup and then
     * never again - so after two silent frames the runner takes the frames
     * back. */
    if (vm->app_painted_frame) {
        if (vm->app_paint_idle > vm->app_paint_period) {
            vm->app_paint_period = vm->app_paint_idle;
        }
        vm->app_paint_idle = 0u;
    } else if (vm->app_paint_idle < 255u) {
        vm->app_paint_idle++;
    }
    vm->app_painted_frame = 0u;
    nj_midp_present(vm);
    vm->repaint_pending = 0u;
    vm->frames_presented++;
    vm->painted_in_frame = 0u;

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


/*
 * MIDletStateChangeException
 *
 * The checked exception MIDlet.startApp()/destroyApp() declare.  Almost every
 * MIDlet drops it from its overrides, so this runtime went without the class -
 * until a title whose destroyApp(Z)V keeps the declaration: resolving that
 * signature needs the class, and a missing one is a NoClassDefFoundError that
 * takes the whole suite down before its first frame.
 */
static const nj_builtin_method mscexc_methods[] = {
    M("<init>", "()V",                   NJ_ACC_PUBLIC),
    M("<init>", "(Ljava/lang/String;)V", NJ_ACC_PUBLIC)
};

NJFN(mscexc_init)
{
    (void)nj_call_special(vm, NJ_THIS(args), "java/lang/Exception", "<init>",
                          "()V", NULL, NULL);
    return NJ_OK;
}

NJFN(mscexc_init_str)
{
    nj_value a[1];

    a[0].r = NJ_ARG_REF(args, 0);
    (void)nj_call_special(vm, NJ_THIS(args), "java/lang/Exception", "<init>",
                          "(Ljava/lang/String;)V", a, NULL);
    return NJ_OK;
}

const nj_builtin_class nj_lib_midlet_classes[] = {
    { NJ_C_MIDLET, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      midlet_fields,
      (nj_u2)(sizeof(midlet_fields) / sizeof(midlet_fields[0])),
      midlet_methods,
      (nj_u2)(sizeof(midlet_methods) / sizeof(midlet_methods[0])) },
    { NJ_C_MSCEXC, "java/lang/Exception", NJ_ACC_PUBLIC,
      NULL, 0u, mscexc_methods,
      (nj_u2)(sizeof(mscexc_methods) / sizeof(mscexc_methods[0])) }
};
const int nj_lib_midlet_class_count = 2;

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
    { NJ_C_MIDLET, "checkPermission", "(Ljava/lang/String;)Z", midlet_checkPermission },
    { NJ_C_MSCEXC, "<init>", "()V",                   mscexc_init },
    { NJ_C_MSCEXC, "<init>", "(Ljava/lang/String;)V", mscexc_init_str }
};
const int nj_lib_midlet_count =
    (int)(sizeof(nj_lib_midlet_table) / sizeof(nj_lib_midlet_table[0]));
