/*
 * nano_j2me - nj_thread.c
 *
 * Cooperative ("green") threads, Java frame allocation and the scheduler.
 *
 * WHY GREEN THREADS
 * A J2ME MIDlet uses java.lang.Thread freely, but the runtime must also run on
 * a microcontroller with no MMU and, often, no preemptive scheduler of its
 * own.  Mapping Java threads onto OS threads would force every OS thread to
 * share the GC root set, need real mutexes around every object, and make
 * teardown non-deterministic.  Instead each Java thread is a plain C structure
 * with its own Java stack; the interpreter switches between them at
 * instruction-count boundaries (NJ_SCHED_QUANTUM).  That gives:
 *
 *   - a single-threaded GC with a trivially enumerable root set,
 *   - byte-identical behaviour on Linux, Android and ESP32-S3,
 *   - deterministic, complete teardown.
 *
 * BLOCKING
 * A native that must block (Thread.sleep, Object.wait, Player.realize) sets
 * the thread state and returns NJ_ERR_TIMEOUT.  The interpreter pops the
 * native frame, delivers the return value and suspends the thread; the
 * scheduler skips it until its deadline or a notify() makes it runnable
 * again.  No C stack is ever suspended, so this works on any port.
 */
#include "nj_internal.h"
#include <stdio.h>
#include <stdlib.h>

#if NJ_USE_LIBC
#  include <string.h>
#endif

/* ------------------------------------------------------------------ */
/* Time                                                                */
/* ------------------------------------------------------------------ */
static int clock_epoch_ms(const nj_datetime *dt, nj_s8 *out)
{
    static const nj_u2 month_days[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    static const nj_u1 month_length[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    nj_s8 y, days;
    int leap, max_day;

    /* Check every field used below before indexing or doing arithmetic. */
    if (dt->year < 1 || dt->year > 9999 ||
        dt->month < 0 || dt->month > 11 ||
        dt->hour < 0 || dt->hour > 23 ||
        dt->minute < 0 || dt->minute > 59 ||
        dt->second < 0 || dt->second > 59 ||
        dt->millis < 0 || dt->millis > 999 ||
        dt->tz_offset <= -1440 || dt->tz_offset >= 1440) {
        return 0;
    }
    leap = dt->year % 4 == 0 &&
           (dt->year % 100 != 0 || dt->year % 400 == 0);
    max_day = month_length[dt->month] + (dt->month == 1 && leap);
    if (dt->day < 1 || dt->day > max_day) {
        return 0;
    }
    y = (nj_s8)dt->year - 1;
    /* Gregorian days before this year, relative to 1970-01-01. */
    days = y * 365 + y / 4 - y / 100 + y / 400 - 719162;
    days += month_days[dt->month] + (dt->month > 1 && leap) + dt->day - 1;
    *out = days * 86400000 + (nj_s8)dt->hour * 3600000
         + (nj_s8)dt->minute * 60000 + (nj_s8)dt->second * 1000
         + dt->millis - (nj_s8)dt->tz_offset * 60000;
    return 1;
}

nj_s8 nj_now_ms(nj_vm *vm)
{
    nj_u4 ms;

    if (vm && vm->port && vm->port->millis) {
        ms = vm->port->millis(vm->port->ud);
    } else {
#if NJ_USE_LIBC
        ms = 0u;
#else
        ms = 0u;
#endif
    }
    if (!vm) {
        return (nj_s8)ms;
    }
    /* Fold the 32-bit port clock into the VM's monotonic epoch so that the
     * value keeps increasing across a wrap. */
    {
        nj_u4 last = (nj_u4)(vm->scheduler_ticks & 0xFFFFFFFFu);
        if (ms < last) {
            vm->scheduler_ticks += 0x100000000ull;
        }
        vm->scheduler_ticks = (vm->scheduler_ticks & ~0xFFFFFFFFull) | ms;
    }
    {
        /* A zero here means "not set yet" - a VM that has never been told a
         * speed runs at real time, and it must not depend on an initialiser
         * somewhere else having run. */
        int percent = (vm->speed_percent > 0) ? vm->speed_percent : 100;
        nj_s8 real = (nj_s8)vm->scheduler_ticks;
        nj_s8 virt = vm->speed_virtual
                   + (real - vm->speed_real) * percent / 100;

        if (percent == 100) {
            /* Real time: keep the origin on the clock so the arithmetic above
             * cannot drift over a long session.  It is rebased AFTER the value
             * the MIDlet gets is computed - rebasing first is what made the
             * clock jump backwards when the player turned the multiplier back
             * off, and every parked deadline would have fired at once. */
            vm->speed_virtual = virt;
            vm->speed_real = real;
        }
        return virt;
    }
}

void nj_clock_init(nj_vm *vm)
{
    nj_datetime dt = {0};
    nj_s8 epoch;

    if (vm && vm->port && vm->port->now &&
        vm->port->now(vm->port->ud, &dt) && clock_epoch_ms(&dt, &epoch)) {
        /* Sample/fold the raw counter, then anchor once. Later reads use only
         * millis, so host wall-clock adjustments cannot change deadlines. */
        (void)nj_now_ms(vm);
        vm->speed_real = vm->scheduler_ticks;
        vm->speed_virtual = epoch;
    }
}

void nj_vm_set_speed(nj_vm *vm, int percent)
{
    nj_s8 now;

    if (!vm) {
        return;
    }
    if (percent < 10) {
        percent = 10;
    }
    if (percent > 800) {
        percent = 800;
    }
    /* Read the clock through the OLD scale first: that instant is where the
     * new one starts, so a MIDlet never sees its clock jump. */
    now = nj_now_ms(vm);
    vm->speed_virtual = now;
    vm->speed_real = (nj_s8)vm->scheduler_ticks;
    vm->speed_percent = percent;
}

int nj_vm_speed(const nj_vm *vm)
{
    return vm ? vm->speed_percent : 100;
}

static void port_idle(nj_vm *vm)
{
    if (vm && vm->port && vm->port->idle) {
        vm->port->idle(vm->port->ud);
    }
}

static void port_sleep(nj_vm *vm, nj_u4 ms)
{
    if (vm && vm->port && vm->port->sleep_ms) {
        vm->port->sleep_ms(vm->port->ud, ms);
    } else {
        port_idle(vm);
    }
}

/* ------------------------------------------------------------------ */
/* Thread lifecycle                                                    */
/* ------------------------------------------------------------------ */
nj_thread *nj_thread_create(nj_vm *vm, nj_ref thread_obj, int is_main)
{
    nj_thread *t;
    nj_value *stack;

    if (!vm || vm->nthreads >= (nj_u2)NJ_MAX_THREADS) {
        return NULL;
    }
    t = (nj_thread *)nj_host_alloc(vm, sizeof(nj_thread));
    if (!t) {
        return NULL;
    }
    stack = (nj_value *)nj_host_alloc(
        vm, (nj_size)NJ_MAX_STACK_SLOTS * sizeof(nj_value));
    if (!stack) {
        nj_host_free(vm, t);
        return NULL;
    }
    t->stack_mem = stack;
    t->stack_end = stack + NJ_MAX_STACK_SLOTS;
    t->top = stack;
    t->nframes = 0;
    t->obj = thread_obj;
    t->state = is_main ? NJ_THREAD_RUNNING : NJ_THREAD_NEW;
    t->is_main = (nj_u1)(is_main ? 1 : 0);
    t->priority = 5;
    t->id = ++vm->next_thread_id;
    t->wake_at_ms = 0;
    vm->threads[vm->nthreads++] = t;
    return t;
}

/*
 * Release the slots of threads whose run() has already returned.  A finished
 * thread used to keep its slot until the VM shut down, so a title that starts
 * a fresh thread per level eventually hit NJ_MAX_THREADS and died with a
 * spurious OutOfMemoryError ("too many threads").  The Java handle is the
 * numeric id kept in the object's nativeId field, not a pointer, so a dead
 * thread can be freed once its Java frames and nested C calls have unwound.
 * A recursive scheduler pass can observe DEAD while an outer native still
 * owns the thread; vm->cur alone does not identify every such C stack.
 */
static void nj_thread_reap(nj_vm *vm)
{
    nj_u2 i;

    for (i = vm->nthreads; i > 0u; i--) {
        nj_thread *t = vm->threads[i - 1u];

        if (t && t->state == NJ_THREAD_DEAD && t != vm->cur &&
            !t->is_main && !t->in_c_call && t->nframes == 0u) {
            nj_thread_destroy(vm, t);
        }
    }
}

void nj_thread_destroy(nj_vm *vm, nj_thread *t)
{
    nj_u2 i;

    if (!vm || !t) {
        return;
    }
    for (i = 0u; i < vm->nthreads; i++) {
        if (vm->threads[i] == t) {
            vm->threads[i] = vm->threads[vm->nthreads - 1u];
            vm->nthreads--;
            break;
        }
    }
    if (vm->cur == t) {
        vm->cur = NULL;
    }
    if (t->stack_mem) {
        nj_host_free(vm, t->stack_mem);
    }
    nj_host_free(vm, t);
}

nj_thread *nj_thread_self(nj_vm *vm)
{
    return vm ? vm->cur : NULL;
}

/*
 * The MIDlet's own thread: the one nj_vm_create makes, the one the frame loop
 * drives, and the thread every call the host makes from C belongs to.  NULL
 * only before the VM has a thread at all.
 */
nj_thread *nj_thread_main(nj_vm *vm)
{
    nj_u2 i;

    if (!vm) {
        return NULL;
    }
    for (i = 0u; i < vm->nthreads; i++) {
        if (vm->threads[i] && vm->threads[i]->is_main) {
            return vm->threads[i];
        }
    }
    return NULL;
}

void nj_yield(nj_vm *vm)
{
    if (vm && vm->cur) {
        vm->cur->wakeup_seq |= 0u;   /* no-op, keeps the intent readable */
        vm->cur->state = NJ_THREAD_RUNNING;
    }
    /*
     * No clock here.  This used to do `vm->scheduler_ticks++`, and the same
     * line sits at the end of scheduler_step: together they added a
     * millisecond to the MIDlet's clock on every scheduler pass, on top of
     * the port's own millisecond clock that nj_now_ms folds into the same
     * counter.  A busy title then saw its clock run many times faster than
     * the wall clock - 合金弹头2013BT版_678 gained roughly 30 ms of MIDlet
     * time per 5 ms frame - so the runner's frame pacer measured every frame
     * as over budget, never slept, and ran the title at 154 fps under
     * `--fps 30` (2479 fps uncapped).  Every countdown, animation and sleep
     * in every title was scaled by the same factor.
     */
}

/* ------------------------------------------------------------------ */
/* Frames                                                              */
/* ------------------------------------------------------------------ */
nj_status nj_frame_push(nj_vm *vm, nj_thread *th, nj_method *m,
                        nj_value *argbase, nj_u2 total_slots)
{
    nj_frame *f;
    nj_value *stk;
    nj_u2 i;

    if (!vm || !th || !m) {
        return NJ_ERR;
    }
    if (th->nframes >= NJ_MAX_FRAMES) {
        return nj_throw(vm, "java/lang/StackOverflowError", m->name);
    }
    stk = argbase + m->max_locals;
    if (stk + m->max_stack + 4 > th->stack_end) {
        return nj_throw(vm, "java/lang/StackOverflowError", m->name);
    }
    f = &th->frames[th->nframes++];
    f->m = m;
    f->pc = 0u;
    f->locals = argbase;
    f->stack = stk;
    f->sp = stk;
    /* The operand stack this frame is allowed to use.  A frame in the MIDDLE of
     * the thread stack used to be bounded only by the global stack_end, so a
     * method that pushed more than its own max_stack walked into the NEXT
     * frame's slots - and with a corrupted class file that is a pointer read
     * from class bytes and dereferenced.  See nj_interp.c for the check. */
    f->stack_limit = stk + m->max_stack;
    f->argbase = argbase;
    f->total_slots = total_slots;
    f->monitor = NULL;
    f->is_native = (m->code == NULL) ? 1u : 0u;

    for (i = total_slots; i < m->max_locals; i++) {
        argbase[i].bits = 0u;
    }
    th->top = stk + m->max_stack;
    return NJ_OK;
}

void nj_frame_pop(nj_vm *vm, nj_thread *th)
{
    (void)vm;
    if (!th || th->nframes <= 0) {
        return;
    }
    th->nframes--;
    th->top = th->frames[th->nframes].locals;
}

/*
 * Start a thread on `run` (a no-argument instance method such as run()).
 * Returns NJ_OK once the first frame is in place.
 */
nj_status nj_thread_start(nj_vm *vm, nj_thread *t, nj_method *run)
{
    nj_value *base;
    nj_status st;

    if (!vm || !t || !run) {
        return NJ_ERR;
    }
    if (t->started) {
        return nj_throw(vm, "java/lang/IllegalThreadStateException", NULL);
    }
    base = t->stack_mem;
    if (run->max_locals < 1u) {
        return nj_throw(vm, "java/lang/VerifyError", run->name);
    }
    base[0].r = t->entry_this ? t->entry_this : t->obj;
    st = nj_frame_push(vm, t, run, base, 1u);
    if (st != NJ_OK) {
        return st;
    }
    t->started = 1u;
    t->state = NJ_THREAD_RUNNING;
    return NJ_OK;
}

nj_status nj_thread_sleep(nj_vm *vm, nj_thread *t, nj_s8 ms)
{
    if (!vm || !t) {
        return NJ_ERR;
    }
    if (ms < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "timeout value is negative");
    }
    t->wake_at_ms = nj_now_ms(vm) + ms;
    t->state = NJ_THREAD_SLEEPING;
    return NJ_ERR_TIMEOUT;   /* tells the interpreter to suspend this thread */
}

/* ------------------------------------------------------------------ */
/* Monitors                                                            */
/* ------------------------------------------------------------------ */
/*
 * With cooperative scheduling a thread is never preempted inside a critical
 * section, so monitorenter/monitorexit only need to record the owning thread
 * and let wait()/notify() find the wait set.  There is no lock word: the wait
 * set is discovered by scanning the (small) thread table.
 */
void nj_vm_monitor_enter(nj_vm *vm, nj_ref obj)
{
    if (!vm) {
        return;
    }
    if (vm->cur && vm->cur->nframes > 0) {
        nj_frame *f = &vm->cur->frames[vm->cur->nframes - 1];
        if (!f->monitor) {
            f->monitor = obj;
        }
    }
}

void nj_vm_monitor_exit(nj_vm *vm, nj_ref obj)
{
    if (vm && vm->cur && vm->cur->nframes > 0) {
        nj_frame *f = &vm->cur->frames[vm->cur->nframes - 1];
        if (f->monitor == obj) {
            f->monitor = NULL;
        }
    }
}

/* Block the current thread on `obj` until notified or the timeout expires. */
nj_status nj_vm_monitor_wait(nj_vm *vm, nj_thread *t, nj_ref obj, nj_s8 timeout_ms)
{
    if (!vm || !t) {
        return NJ_ERR;
    }
    t->wait_monitor = obj;
    t->wait_deadline = (timeout_ms == 0) ? 0 : (nj_now_ms(vm) + timeout_ms);
    t->state = NJ_THREAD_WAITING;
    return NJ_ERR_TIMEOUT;
}

/* Wake up to `count` threads waiting on `obj`.  Returns the number woken. */
int nj_vm_monitor_notify(nj_vm *vm, nj_ref obj, int all)
{
    nj_u2 i;
    int woken = 0;

    if (!vm) {
        return 0;
    }
    for (i = 0u; i < vm->nthreads; i++) {
        nj_thread *t = vm->threads[i];
        if (!t || t == vm->cur) {
            continue;
        }
        if (t->state == NJ_THREAD_WAITING && t->wait_monitor == obj) {
            t->state = NJ_THREAD_RUNNING;
            t->wait_monitor = NULL;
            t->wait_deadline = 0;
            woken++;
            if (!all) {
                break;
            }
        }
    }
    return woken;
}

/* ------------------------------------------------------------------ */
/* Scheduler                                                           */
/* ------------------------------------------------------------------ */
static nj_status scheduler_step(nj_vm *vm)
{
    nj_u2 i;
    int alive = 0;
    int waiting = 0;
    int ran = 0;
    nj_s8 now;
    nj_s8 earliest = 0;
    int have_earliest = 0;

    if (!vm || !vm->running) {
        return NJ_ERR_HALT;
    }
    nj_thread_reap(vm);
    now = nj_now_ms(vm);

    for (i = 0u; i < vm->nthreads; i++) {
        nj_thread *t = vm->threads[i];
        nj_status st;

        if (!t || t->state == NJ_THREAD_DEAD) {
            continue;
        }
        alive = 1;

        if (t->state == NJ_THREAD_SLEEPING) {
            if (now >= t->wake_at_ms) {
                t->state = NJ_THREAD_RUNNING;
            } else {
                nj_s8 w = t->wake_at_ms;
                if (!have_earliest || w < earliest) {
                    earliest = w;
                    have_earliest = 1;
                }
                continue;
            }
        }
        if (t->state == NJ_THREAD_WAITING) {
            if (t->wait_deadline != 0 && now >= t->wait_deadline) {
                t->state = NJ_THREAD_RUNNING;
                t->wait_monitor = NULL;
                t->wait_deadline = 0;
            } else {
                nj_s8 w = t->wait_deadline;
                waiting++;
                if (w != 0 && (!have_earliest || w < earliest)) {
                    earliest = w;
                    have_earliest = 1;
                }
                continue;
            }
        }
        if (t->state != NJ_THREAD_RUNNING) {
            continue;
        }
        /*
         * Parked inside a nested C call: its C stack belongs to a native that
         * is still running (or to the pump waiting for it), and the top frame
         * on that stack is the NATIVE's, with code_len 0.  Running it here
         * would fetch an opcode out of an empty method - five collected
         * titles reported exactly that as
         *   VerifyError: code runs past the end of the method
         * at nj2me/util/TimerThread.pump pc=0.  The pump that owns the stack
         * notices the thread becoming runnable and resumes it itself, so all
         * this loop owes the thread is the deadline accounting above (which
         * is why the check sits after it, not before: a thread parked in
         * Thread.sleep() inside a native still has to set `earliest`, or the
         * loop below mistakes "one thread asleep, one waiting" for a
         * deadlock - 极品飞车16：亡命狂飙_1519 died that way).
         */
        if (t->in_c_call) {
            continue;
        }

        /*
         * A thread that has no frames left has finished its run() method:
         * `nj_exec` on an empty stack returns NJ_OK (it has nothing to do),
         * so without this check the slot stayed NJ_THREAD_RUNNING for ever,
         * the scheduler kept walking it, and the title eventually ran out of
         * thread slots ("too many threads").  Only scheduler-owned threads
         * reach here: the main thread and natives parked in a C call are
         * filtered above.
         */
        if (t->nframes == 0u && !t->is_main) {
            t->state = NJ_THREAD_DEAD;
            continue;
        }
        ran = 1;
        vm->cur = t;
        st = nj_exec(vm, t, 0);

        if (st == NJ_ERR_HALT) {
            t->state = NJ_THREAD_DEAD;
            /* An uncaught exception propagates out of the thread. */
            if (vm->exception) {
                const char *desc = nj_exception_describe(vm);
                nj_ref e = nj_take_exception(vm);
                if (t->is_main) {
                    return NJ_ERR;
                }
                /* Full description, not just the class: an uncaught
                 * IllegalArgumentException with no message is barely more
                 * useful than a blank screen. */
                nj_logf(vm, 1, "uncaught exception in thread %u: %s",
                        (unsigned)t->id,
                        desc ? desc : (e && e->cls ? e->cls->name : "?"));
            }
        } else if (st == NJ_OK && !t->is_main && t->started &&
                   t->nframes == 0 && t->obj) {
            nj_field *f = nj_field_lookup(vm, "java/lang/Thread", "alive", "Z", 0);
            /* nj_exec has returned from the worker's bottom Java frame. A
             * nested C invocation is excluded above by in_c_call. Publish
             * completion to isAlive()/join(), but retain the legacy scheduler
             * state: constructor frame-hook pumps currently depend on its idle
             * timing (Doom/Doom's untimed startup wait in particular). */
            if (f && nj_field_read_int(vm, t->obj, f)) {
                nj_field_write_int(vm, t->obj, f, 0);
                (void)nj_vm_monitor_notify(vm, t->obj, 1);
            }
            if (t->obj->cls &&
                strcmp(t->obj->cls->name, "nj2me/util/TimerThread") == 0) {
                t->state = NJ_THREAD_DEAD;
            }
        } else if (st != NJ_OK && st != NJ_ERR_YIELD && st != NJ_ERR_TIMEOUT) {
            return st;
        }
    }

    if (!alive) {
        return NJ_ERR_HALT;
    }
    if (!ran) {
        if (have_earliest) {
            nj_s8 delta = earliest - now;
            if (delta > 0) {
                port_sleep(vm, (nj_u4)(delta > 50 ? 50 : delta));
            }
        } else if (waiting > 0) {
            /* Every remaining thread is parked on Object.wait() with no
             * notifier left: this is a genuine deadlock. */
            nj_logf(vm, 1, "all threads are waiting; assuming the app is done");
            return NJ_ERR_HALT;
        } else {
            port_idle(vm);
        }
    }
    return NJ_OK;
}

/*
 * Run every runnable thread for one quantum, then hand control back to the
 * MIDlet's own thread.
 *
 * Returns NJ_OK when at least one thread ran, NJ_ERR_HALT when every thread is
 * dead (the program is over), or NJ_ERR_STATE on a deadlock that cannot be
 * resolved by waiting (all remaining threads blocked on wait()).
 */
nj_status nj_scheduler_step(nj_vm *vm)
{
    nj_thread *main_thread = nj_thread_main(vm);
    nj_status st = scheduler_step(vm);

    /*
     * scheduler_step points vm->cur at each thread it runs, which is right
     * while that thread is executing and wrong the moment this returns: the
     * caller is C again (nj_midp_frame drains key events, then paints) and
     * vm_invoke starts from vm->cur.  A MIDlet whose own game thread parks in
     * Thread.sleep() therefore had its key callbacks and paint() dispatched on
     * a sleeping thread: nj_exec yielded at once, vm_invoke turned that into
     * NJ_ERR_TIMEOUT, and the canvas logged "paint() failed without raising an
     * exception" every frame while the keys went nowhere.
     */
    if (main_thread) {
        vm->cur = main_thread;
    }
    return st;
}

/* Give background computation a bounded share of a display frame.  A
 * bytecode quantum is a fairness boundary, not a reason to sleep until the
 * next video frame: software renderers often need many quanta per image.
 * Stop when the application presents, blocks, or uses its host-time budget.
 * The round limit also bounds ports with a coarse or stationary clock. */
nj_status nj_scheduler_frame(nj_vm *vm, nj_u4 budget_ms)
{
    nj_s8 start = nj_now_ms(vm);
    unsigned rounds = 0u;
    nj_status st;
    do {
        nj_u2 i;
        int runnable = 0;
        st = nj_scheduler_step(vm);
        if (st != NJ_OK || !vm->running || vm->painted_in_frame ||
            ++rounds >= 256u ||
            nj_now_ms(vm) - start >= (nj_s8)budget_ms) {
            break;
        }
        for (i = 0; i < vm->nthreads; ++i) {
            nj_thread *th = vm->threads[i];
            if (th && th->state == NJ_THREAD_RUNNING && th->nframes &&
                !th->in_c_call) {
                runnable = 1;
                break;
            }
        }
        if (!runnable) break;
    } while (1);
    return st;
}

/* Run the scheduler until every thread is dead or `budget_ms` elapses. */
nj_status nj_scheduler_run_until_idle(nj_vm *vm, nj_u4 budget_ms)
{
    nj_s8 start = nj_now_ms(vm);

    for (;;) {
        nj_status st = nj_scheduler_step(vm);
        if (st == NJ_ERR_HALT) {
            return NJ_OK;
        }
        if (st != NJ_OK) {
            return st;
        }
        if (budget_ms && (nj_now_ms(vm) - start) > (nj_s8)budget_ms) {
            return NJ_ERR_TIMEOUT;
        }
    }
}

void nj_vm_stop(nj_vm *vm, nj_s4 exit_code)
{
    if (!vm) {
        return;
    }
    vm->exit_code = exit_code;
    vm->running = 0u;
    vm->state = NJ_VM_STOPPING;
}

/*
 * Stop the VM, saying who asked for it.
 *
 * "Nothing was ever presented" and "the MIDlet called notifyDestroyed() in its
 * first frame" leave the same empty frame directory, and telling them apart is
 * the difference between a missing class and a game that quit on purpose.  The
 * reason is a string literal: nothing here allocates, so nothing can leak.
 */
void nj_vm_stop_reason(nj_vm *vm, nj_s4 exit_code, const char *why)
{
    if (!vm) {
        return;
    }
    nj_vm_stop(vm, exit_code);
    vm->stop_why = why;
}

const char *nj_vm_stop_why(const nj_vm *vm)
{
    return vm ? vm->stop_why : NULL;
}
