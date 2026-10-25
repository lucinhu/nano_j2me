/* A periodic task whose paint takes longer than its period must return to
 * the frame loop. The fake clock makes this independent of machine speed. */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include "nj_midp.h"
#include <stdio.h>

static int failures, calls, paints;
static nj_u4 clock_ms;
static nj_ref canvas;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d: %s\n", __LINE__, #c); failures++; } } while (0)

static nj_u4 now(void *ud) { (void)ud; return clock_ms; }
static void sleep_ms(void *ud, nj_u4 ms) { (void)ud; clock_ms += ms; }
static void idle(void *ud) { (void)ud; }

NJ_NATIVE_FN(paint)
{
    (void)vm; (void)th; (void)args; (void)ret;
    paints++;
    clock_ms += 5;
    return NJ_OK;
}

NJ_NATIVE_FN(task_run)
{
    int before = paints;
    (void)th; (void)args; (void)ret;
    calls++;
    CHECK(nj_call(vm, canvas, NJ_C_CANVAS, "repaint", "()V", NULL, NULL) == NJ_OK);
    /* Waiting for paint to publish a value inside a callback still works.
     * A second request in the same callback must not be silently coalesced. */
    CHECK(paints == before + 1);
    CHECK(nj_call(vm, canvas, NJ_C_CANVAS, "repaint", "()V", NULL, NULL) == NJ_OK);
    CHECK(paints == before + 2);
    return NJ_OK;
}

int main(void)
{
    static const nj_builtin_method cm[] = {
        { "paint", "(Ljavax/microedition/lcdui/Graphics;)V", NJ_ACC_PUBLIC }
    };
    static const nj_builtin_method tm[] = { { "run", "()V", NJ_ACC_PUBLIC } };
    static const nj_builtin_class cc = {
        "test/TimerCanvas", NJ_C_CANVAS, NJ_ACC_PUBLIC, NULL, 0, cm, 1
    }, tc = {
        "test/SlowTimerTask", "java/util/TimerTask", NJ_ACC_PUBLIC, NULL, 0, tm, 1
    };
    nj_port port;
    nj_vm *vm;
    nj_class *cls;
    nj_ref timer, task;
    nj_value args[5] = {{0}};
    int i;
    CHECK(nj_port_headless(&port) == NJ_OK);
    port.millis = now; port.sleep_ms = sleep_ms; port.idle = idle;
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    CHECK(vm != NULL);
    if (!vm) return 1;
    vm->running = 1;
    cls = nj_class_create_builtin(vm, &cc);
    CHECK(cls != NULL);
    if (!cls) goto done;
    cls->methods[0].native = paint;
    cls = nj_class_create_builtin(vm, &tc);
    CHECK(cls != NULL);
    if (!cls) goto done;
    cls->methods[0].native = task_run;
    canvas = nj_root(vm, nj_object_new(vm, cc.name));
    task = nj_root(vm, nj_object_new(vm, tc.name));
    timer = nj_root(vm, nj_object_new(vm, "java/util/Timer"));
    CHECK(canvas && task && timer);
    if (!canvas || !task || !timer) goto done;
    CHECK(nj_midp_set_current(vm, canvas) == NJ_OK);
    CHECK(nj_call_special(vm, task, "java/util/TimerTask", "<init>", "()V", NULL, NULL) == NJ_OK);
    CHECK(nj_call_special(vm, timer, "java/util/Timer", "<init>", "()V", NULL, NULL) == NJ_OK);
    args[0].r = task; args[1].j = 0; args[3].j = 1;
    CHECK(nj_call(vm, timer, "java/util/Timer", "schedule",
                 "(Ljava/util/TimerTask;JJ)V", args, NULL) == NJ_OK);
    for (i = 0; i < 20; i++) {
        int before = calls;
        CHECK(nj_scheduler_step(vm) == NJ_OK);
        CHECK(calls == before + 1);
        CHECK(vm->in_callback == 0 && vm->in_paint == 0);
    }
    CHECK(calls == 20 && paints == 40);
    CHECK(nj_call(vm, timer, "java/util/Timer", "cancel", "()V", NULL, NULL) == NJ_OK);
    CHECK(nj_scheduler_step(vm) == NJ_OK);
    CHECK(calls == 20);
    CHECK(!nj_has_exception(vm));
done:
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    printf("timer fairness: %d failures\n", failures);
    return failures ? 1 : 0;
}
