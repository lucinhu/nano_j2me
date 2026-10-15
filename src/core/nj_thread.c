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

#if NJ_USE_LIBC
#  include <string.h>
#endif

/* ------------------------------------------------------------------ */
/* Time                                                                */
/* ------------------------------------------------------------------ */
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
        } else if (st == NJ_OK && !t->is_main && t->nframes == 0 &&
                   t->obj && t->obj->cls &&
                   strcmp(t->obj->cls->name, "nj2me/util/TimerThread") == 0) {
            /* Internal timer helpers must finish so cancelled timers can be
             * reclaimed. Ordinary Java thread completion is left unchanged
             * until nested constructor waits have their own regression. */
            t->state = NJ_THREAD_DEAD;
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
