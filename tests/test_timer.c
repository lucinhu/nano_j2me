/* Timer lifecycle regressions: stale Rally callbacks must stop their own
 * timer without retaining its queue/thread or delaying unrelated timers. */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include "nj_midi.h"
#include <stdio.h>
#include <string.h>

/* javac --release 8: class TimerFixture extends java.util.TimerTask {
 * public int runs; public boolean fail;
 * public void run() { runs++; if (fail) throw new NullPointerException(); }
 * } */
static const nj_u1 task_class[] = {202,254,186,190,0,0,0,52,0,26,10,0,2,0,3,7,0,4,12,0,5,0,6,1,0,19,106,97,118,97,47,117,116,105,108,47,84,105,109,101,114,84,97,115,107,1,0,6,60,105,110,105,116,62,1,0,3,40,41,86,9,0,8,0,9,7,0,10,12,0,11,0,12,1,0,12,84,105,109,101,114,70,105,120,116,117,114,101,1,0,4,114,117,110,115,1,0,1,73,9,0,8,0,14,12,0,15,0,16,1,0,4,102,97,105,108,1,0,1,90,7,0,18,1,0,30,106,97,118,97,47,108,97,110,103,47,78,117,108,108,80,111,105,110,116,101,114,69,120,99,101,112,116,105,111,110,10,0,17,0,3,1,0,4,67,111,100,101,1,0,15,76,105,110,101,78,117,109,98,101,114,84,97,98,108,101,1,0,3,114,117,110,1,0,13,83,116,97,99,107,77,97,112,84,97,98,108,101,1,0,10,83,111,117,114,99,101,70,105,108,101,1,0,17,84,105,109,101,114,70,105,120,116,117,114,101,46,106,97,118,97,0,49,0,8,0,2,0,0,0,2,0,1,0,11,0,12,0,0,0,1,0,15,0,16,0,0,0,2,0,1,0,5,0,6,0,1,0,20,0,0,0,29,0,1,0,1,0,0,0,5,42,183,0,1,177,0,0,0,1,0,21,0,0,0,6,0,1,0,0,0,1,0,1,0,22,0,6,0,1,0,20,0,0,0,67,0,3,0,1,0,0,0,26,42,89,180,0,7,4,96,181,0,7,42,180,0,13,153,0,11,187,0,17,89,183,0,19,191,177,0,0,0,2,0,21,0,0,0,14,0,3,0,0,0,5,0,10,0,6,0,25,0,7,0,23,0,0,0,3,0,1,25,0,1,0,24,0,0,0,2,0,25};

static int failures;
static nj_u4 clock_ms;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d: %s\n", __LINE__, #c); failures++; } } while (0)
static nj_u4 millis(void *ud) { (void)ud; return clock_ms; }
static nj_field *field(nj_vm *vm, const char *cls, const char *name, const char *sig)
{
    return nj_field_lookup_in(nj_class_find(vm, cls), name, sig, 0);
}
static nj_ref make(nj_vm *vm, const char *cls)
{
    nj_ref o = nj_root(vm, nj_object_new(vm, cls));
    CHECK(o != NULL);
    CHECK(nj_call_special(vm, o, cls, "<init>", "()V", NULL, NULL) == NJ_OK);
    return o;
}
static nj_status schedule(nj_vm *vm, nj_ref timer, nj_ref task, nj_s8 delay, nj_s8 period)
{
    nj_value a[5];
    memset(a, 0, sizeof(a));
    a[0].r = task; a[1].j = delay; a[3].j = period;
    return nj_call(vm, timer, "java/util/Timer", "schedule",
                   period ? "(Ljava/util/TimerTask;JJ)V" : "(Ljava/util/TimerTask;J)V", a, NULL);
}
static void step(nj_vm *vm)
{
    nj_thread *main = nj_thread_main(vm);
    CHECK(nj_scheduler_step(vm) == NJ_OK);
    vm->cur = main;
    CHECK(main->state == NJ_THREAD_RUNNING);
}
static int runs(nj_vm *vm, nj_ref task)
{
    return nj_field_read_int(vm, task, field(vm, "TimerFixture", "runs", "I"));
}
static void cancel(nj_vm *vm, nj_ref timer)
{
    CHECK(nj_call(vm, timer, "java/util/Timer", "cancel", "()V", NULL, NULL) == NJ_OK);
}
static void queue_empty(nj_vm *vm, nj_ref timer)
{
    nj_ref queue = nj_field_read_ref(vm, timer, field(vm, "java/util/Timer", "ttTasks", "Ljava/util/Vector;"));
    nj_value r;
    CHECK(nj_call(vm, queue, "java/util/Vector", "size", "()I", NULL, &r) == NJ_OK);
    CHECK(r.i == 0);
}
static nj_thread *helper(nj_vm *vm, nj_ref timer)
{
    nj_ref obj = nj_field_read_ref(vm, timer, field(vm, "java/util/Timer", "ttHelper", "Ljava/lang/Object;"));
    nj_u2 i;
    for (i = 0; i < vm->nthreads; i++) if (vm->threads[i]->obj == obj) return vm->threads[i];
    return NULL;
}
int main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_ref timer, task, later, bad, other, good;
    nj_value ret;
    int i;
    memset(&port, 0, sizeof(port));
    CHECK(nj_port_headless(&port) == NJ_OK);
    port.millis = millis;
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    CHECK(vm != NULL);
    if (!vm) return 1;
    CHECK(nj_class_load(vm, "TimerFixture", task_class, sizeof(task_class)) != NULL);
    vm->running = 1;

    /* An initially idle helper must wake immediately, and an earlier insertion
     * must interrupt a pending future deadline. */
    timer = make(vm, "java/util/Timer");
    step(vm);
    CHECK(helper(vm, timer)->state == NJ_THREAD_SLEEPING);
    task = make(vm, "TimerFixture");
    CHECK(schedule(vm, timer, task, 0, 0) == NJ_OK);
    step(vm); CHECK(runs(vm, task) == 1);
    later = make(vm, "TimerFixture");
    CHECK(schedule(vm, timer, later, 1000, 0) == NJ_OK);
    step(vm); CHECK(runs(vm, later) == 0);
    task = make(vm, "TimerFixture");
    CHECK(schedule(vm, timer, task, 0, 0) == NJ_OK);
    step(vm); CHECK(runs(vm, task) == 1); CHECK(runs(vm, later) == 0);
    cancel(vm, timer); queue_empty(vm, timer); step(vm);
    CHECK(helper(vm, timer)->state == NJ_THREAD_DEAD);

    /* A virgin cancelled task is permanently unschedulable despite false. */
    timer = make(vm, "java/util/Timer");
    task = make(vm, "TimerFixture");
    CHECK(nj_call(vm, task, "java/util/TimerTask", "cancel", "()Z", NULL, &ret) == NJ_OK);
    CHECK(ret.i == 0);
    CHECK(schedule(vm, timer, task, 0, 0) != NJ_OK);
    CHECK(nj_take_exception(vm) != NULL);
    cancel(vm, timer); step(vm);

    /* A throwing periodic callback cancels its queue and leaves a second
     * Timer running. Scheduling onto the failed Timer must be rejected. */
    timer = make(vm, "java/util/Timer"); other = make(vm, "java/util/Timer");
    bad = make(vm, "TimerFixture"); later = make(vm, "TimerFixture"); good = make(vm, "TimerFixture");
    nj_field_write_int(vm, bad, field(vm, "TimerFixture", "fail", "Z"), 1);
    CHECK(schedule(vm, timer, bad, 0, 10) == NJ_OK);
    CHECK(schedule(vm, timer, later, 100, 0) == NJ_OK);
    CHECK(schedule(vm, other, good, 0, 10) == NJ_OK);
    step(vm); queue_empty(vm, timer);
    CHECK(runs(vm, bad) == 1); CHECK(runs(vm, later) == 0); CHECK(runs(vm, good) == 1);
    CHECK(vm->exception == NULL);
    task = make(vm, "TimerFixture");
    CHECK(schedule(vm, timer, task, 0, 0) != NJ_OK);
    CHECK(nj_take_exception(vm) != NULL);
    clock_ms += 20; step(vm);
    CHECK(runs(vm, bad) == 1); CHECK(runs(vm, good) == 2);
    cancel(vm, other); step(vm);

    /* More than the VM thread limit worth of cancelled Timers must reuse the
     * internal helper capacity, without changing ordinary Java Thread records. */
    for (i = 0; i < NJ_MAX_THREADS * 2; i++) {
        nj_u4 mark = nj_root_mark(vm);
        timer = make(vm, "java/util/Timer");
        cancel(vm, timer); step(vm);
        CHECK(helper(vm, timer)->state == NJ_THREAD_DEAD);
        CHECK(vm->nthreads <= 2);
        nj_root_release(vm, mark);
    }
    nj_vm_destroy(vm); nj_port_shutdown(&port);
    printf("Timer lifecycle: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
