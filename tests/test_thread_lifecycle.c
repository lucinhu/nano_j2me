/* Thread completion regressions. Embedded javac --release 8 -g:none fixtures:
 * ThreadWorker extends Thread: sleep(20), then increment static ran.
 * ThreadBridge constructor: start ThreadWorker, join, assert !isAlive && ran.
 * ThreadWaitBridge constructor: start Thread(this), wait until run notifies.
 * Keep these independent of a JDK and the downloaded game corpus at test time.
 */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include <stdio.h>
#include <string.h>
static const nj_u1 ThreadWorker[] = {
    202,254,186,190,0,0,0,52,0,24,10,0,2,0,3,7,0,4,12,0,5,0,6,1,
    0,16,106,97,118,97,47,108,97,110,103,47,84,104,114,101,97,100,1,0,6,60,105,110,
    105,116,62,1,0,3,40,41,86,5,0,0,0,0,0,0,0,20,10,0,2,0,10,12,
    0,11,0,12,1,0,5,115,108,101,101,112,1,0,4,40,74,41,86,7,0,14,1,0,
    30,106,97,118,97,47,108,97,110,103,47,73,110,116,101,114,114,117,112,116,101,100,69,120,
    99,101,112,116,105,111,110,9,0,16,0,17,7,0,18,12,0,19,0,20,1,0,12,84,
    104,114,101,97,100,87,111,114,107,101,114,1,0,3,114,97,110,1,0,1,73,1,0,4,
    67,111,100,101,1,0,3,114,117,110,1,0,13,83,116,97,99,107,77,97,112,84,97,98,
    108,101,0,33,0,16,0,2,0,0,0,1,0,9,0,19,0,20,0,0,0,2,0,1,
    0,5,0,6,0,1,0,21,0,0,0,17,0,1,0,1,0,0,0,5,42,183,0,1,
    177,0,0,0,0,0,1,0,22,0,6,0,1,0,21,0,0,0,52,0,2,0,2,0,
    0,0,19,20,0,7,184,0,9,167,0,4,76,178,0,15,4,96,179,0,15,177,0,1,
    0,0,0,6,0,9,0,13,0,1,0,23,0,0,0,7,0,2,73,7,0,13,0,0,
    0,
};
static const nj_u1 ThreadBridge[] = {
    202,254,186,190,0,0,0,52,0,38,10,0,2,0,3,7,0,4,12,0,5,0,6,1,
    0,16,106,97,118,97,47,108,97,110,103,47,79,98,106,101,99,116,1,0,6,60,105,110,
    105,116,62,1,0,3,40,41,86,7,0,8,1,0,12,84,104,114,101,97,100,87,111,114,
    107,101,114,10,0,7,0,3,9,0,11,0,12,7,0,13,12,0,14,0,15,1,0,12,
    84,104,114,101,97,100,66,114,105,100,103,101,1,0,5,99,104,105,108,100,1,0,14,76,
    84,104,114,101,97,100,87,111,114,107,101,114,59,10,0,7,0,17,12,0,18,0,6,1,
    0,5,115,116,97,114,116,10,0,7,0,20,12,0,21,0,6,1,0,4,106,111,105,110,
    10,0,7,0,23,12,0,24,0,25,1,0,7,105,115,65,108,105,118,101,1,0,3,40,
    41,90,9,0,7,0,27,12,0,28,0,29,1,0,3,114,97,110,1,0,1,73,7,0,
    31,1,0,26,106,97,118,97,47,108,97,110,103,47,82,117,110,116,105,109,101,69,120,99,
    101,112,116,105,111,110,10,0,30,0,3,1,0,4,67,111,100,101,1,0,13,83,116,97,
    99,107,77,97,112,84,97,98,108,101,1,0,10,69,120,99,101,112,116,105,111,110,115,7,
    0,37,1,0,30,106,97,118,97,47,108,97,110,103,47,73,110,116,101,114,114,117,112,116,
    101,100,69,120,99,101,112,116,105,111,110,0,33,0,11,0,2,0,0,0,1,0,1,0,
    14,0,15,0,0,0,1,0,1,0,5,0,6,0,2,0,33,0,0,0,85,0,3,0,
    1,0,0,0,54,42,183,0,1,42,187,0,7,89,183,0,9,181,0,10,42,180,0,10,
    182,0,16,42,180,0,10,182,0,19,42,180,0,10,182,0,22,154,0,9,178,0,26,154,
    0,11,187,0,30,89,183,0,32,191,177,0,0,0,1,0,34,0,0,0,13,0,2,255,
    0,45,0,1,7,0,11,0,0,7,0,35,0,0,0,4,0,1,0,36,0,0,
};
static const nj_u1 ThreadWaitBridge[] = {
    202,254,186,190,0,0,0,52,0,37,10,0,2,0,3,7,0,4,12,0,5,0,6,1,
    0,16,106,97,118,97,47,108,97,110,103,47,79,98,106,101,99,116,1,0,6,60,105,110,
    105,116,62,1,0,3,40,41,86,7,0,8,1,0,16,106,97,118,97,47,108,97,110,103,
    47,84,104,114,101,97,100,10,0,7,0,10,12,0,5,0,11,1,0,23,40,76,106,97,
    118,97,47,108,97,110,103,47,82,117,110,110,97,98,108,101,59,41,86,10,0,7,0,13,
    12,0,14,0,6,1,0,5,115,116,97,114,116,9,0,16,0,17,7,0,18,12,0,19,
    0,20,1,0,16,84,104,114,101,97,100,87,97,105,116,66,114,105,100,103,101,1,0,4,
    100,111,110,101,1,0,1,90,10,0,2,0,22,12,0,23,0,6,1,0,4,119,97,105,
    116,10,0,2,0,25,12,0,26,0,6,1,0,9,110,111,116,105,102,121,65,108,108,7,
    0,28,1,0,18,106,97,118,97,47,108,97,110,103,47,82,117,110,110,97,98,108,101,1,
    0,4,67,111,100,101,1,0,13,83,116,97,99,107,77,97,112,84,97,98,108,101,7,0,
    32,1,0,19,106,97,118,97,47,108,97,110,103,47,84,104,114,111,119,97,98,108,101,1,
    0,10,69,120,99,101,112,116,105,111,110,115,7,0,35,1,0,30,106,97,118,97,47,108,
    97,110,103,47,73,110,116,101,114,114,117,112,116,101,100,69,120,99,101,112,116,105,111,110,
    1,0,3,114,117,110,0,33,0,16,0,2,0,1,0,27,0,1,0,1,0,19,0,20,
    0,0,0,2,0,1,0,5,0,6,0,2,0,29,0,0,0,101,0,3,0,3,0,0,
    0,44,42,183,0,1,42,89,76,194,187,0,7,89,42,183,0,9,182,0,12,42,180,0,
    15,154,0,10,42,182,0,21,167,255,245,43,195,167,0,8,77,43,195,44,191,177,0,2,
    0,8,0,35,0,38,0,0,0,38,0,41,0,38,0,0,0,1,0,30,0,0,0,23,
    0,4,255,0,19,0,2,7,0,16,7,0,2,0,0,13,68,7,0,31,250,0,4,0,
    33,0,0,0,4,0,1,0,34,0,1,0,36,0,6,0,1,0,29,0,0,0,79,0,
    2,0,3,0,0,0,24,42,89,76,194,42,4,181,0,15,42,182,0,24,43,195,167,0,
    8,77,43,195,44,191,177,0,2,0,4,0,15,0,18,0,0,0,18,0,21,0,18,0,
    0,0,1,0,30,0,0,0,21,0,2,255,0,18,0,2,7,0,16,7,0,2,0,1,
    7,0,31,250,0,4,0,0,
};

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d: %s\n", __LINE__, #c); failures++; } } while (0)
static nj_ref make(nj_vm *vm, const char *cls)
{
    nj_ref o = nj_root(vm, nj_object_new(vm, cls));
    CHECK(o != NULL);
    if (o) CHECK(nj_call_special(vm, o, cls, "<init>", "()V", NULL, NULL) == NJ_OK);
    CHECK(vm->exception == NULL);
    return o;
}
static int alive(nj_vm *vm, nj_ref o)
{
    nj_value ret;
    memset(&ret, 0, sizeof(ret));
    CHECK(nj_call(vm, o, "java/lang/Thread", "isAlive", "()Z", NULL, &ret) == NJ_OK);
    return ret.i;
}
/* A timer callback can pump the scheduler while an outer native is still
 * unwinding on a thread already marked dead. Exercise that actual native
 * frame, rather than relying on the allocator to preserve freed memory. */
static nj_status reap_during_native(nj_vm *vm, nj_thread *th,
                                    nj_value *args, nj_value *ret)
{
    nj_u2 count = vm->nthreads;
    (void)args;
    (void)ret;
    th->state = NJ_THREAD_DEAD;
    vm->cur = nj_thread_main(vm);
    CHECK(nj_scheduler_step(vm) == NJ_OK);
    CHECK(vm->nthreads == count);
    return NJ_OK;
}

static void test_reaping(nj_vm *vm)
{
    nj_thread *main_thread = nj_thread_main(vm);
    nj_method method;
    nj_value ret[2];
    nj_u2 count;
    int i;

    CHECK(nj_scheduler_step(vm) == NJ_OK);
    count = vm->nthreads;
    memset(&method, 0, sizeof(method));
    method.owner = nj_class_find(vm, "java/lang/Object");
    method.name = "reapDuringNative";
    method.desc = "()V";
    method.flags = NJ_ACC_STATIC | NJ_ACC_NATIVE;
    method.native = reap_during_native;
    for (i = 0; i < 2 * NJ_MAX_THREADS; i++) {
        nj_thread *t = nj_thread_create(vm, NULL, 0);
        CHECK(t != NULL);
        if (!t) break;
        t->state = NJ_THREAD_RUNNING;
        vm->cur = t;
        CHECK(nj_invoke_native(vm, t, &method, t->stack_mem, 0, ret) == NJ_OK);
        /* Returning from the C frame now makes reclamation safe. Repeating
         * beyond the slot limit also verifies that reclamation still works. */
        CHECK(nj_scheduler_step(vm) == NJ_OK);
        CHECK(vm->nthreads == count);
    }
    {
        nj_thread *t = nj_thread_create(vm, NULL, 0);
        CHECK(t != NULL);
        if (!t) return;
        t->state = NJ_THREAD_DEAD;
        t->in_c_call = 1;
        CHECK(nj_scheduler_step(vm) == NJ_OK);
        CHECK(vm->nthreads == count + 1);
        t->in_c_call = 0;
        CHECK(nj_scheduler_step(vm) == NJ_OK);
        CHECK(vm->nthreads == count);
    }
    {
        nj_thread *t = nj_thread_create(vm, NULL, 0);
        CHECK(t != NULL);
        if (!t) return;
        /* The C runner retains the main-thread handle even after halt. */
        main_thread->state = NJ_THREAD_DEAD;
        vm->cur = t;
        (void)nj_scheduler_step(vm);
        CHECK(nj_thread_main(vm) == main_thread);
        main_thread->state = NJ_THREAD_RUNNING;
        t->state = NJ_THREAD_DEAD;
        vm->cur = main_thread;
        CHECK(nj_scheduler_step(vm) == NJ_OK);
        CHECK(vm->nthreads == count);
    }
}

int main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_ref worker, bridge;
    nj_thread *main_thread;
    nj_field *ran;
    int rounds;
    memset(&port, 0, sizeof(port));
    CHECK(nj_port_headless(&port) == NJ_OK);
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    CHECK(vm != NULL);
    if (!vm) return 1;
    CHECK(nj_class_load(vm, "ThreadWorker", ThreadWorker, sizeof(ThreadWorker)) != NULL);
    CHECK(nj_class_load(vm, "ThreadBridge", ThreadBridge, sizeof(ThreadBridge)) != NULL);
    CHECK(nj_class_load(vm, "ThreadWaitBridge", ThreadWaitBridge, sizeof(ThreadWaitBridge)) != NULL);
    vm->running = 1;
    main_thread = nj_thread_main(vm);
    test_reaping(vm);
    worker = make(vm, "ThreadWorker");
    CHECK(!alive(vm, worker));
    CHECK(nj_call(vm, worker, "java/lang/Thread", "start", "()V", NULL, NULL) == NJ_OK);
    CHECK(alive(vm, worker));
    /* A native bridge may temporarily own an empty Java stack. It must not
     * look completed to a recursive scheduler pass. Restore the real frame
     * afterwards so the same worker then exercises actual run() return. */
    {
        nj_u2 i;
        for (i = 0; i < vm->nthreads; i++) {
            nj_thread *t = vm->threads[i];
            if (t && t->obj == worker) {
                int depth = t->nframes;
                t->nframes = 0;
                t->in_c_call = 1;
                CHECK(nj_scheduler_step(vm) == NJ_OK);
                CHECK(alive(vm, worker));
                t->in_c_call = 0;
                t->nframes = depth;
            }
        }
    }
    for (rounds = 0; rounds < 100 && alive(vm, worker); rounds++) {
        CHECK(nj_scheduler_step(vm) == NJ_OK);
        if (port.sleep_ms) port.sleep_ms(port.ud, 5u);
    }
    CHECK(!alive(vm, worker));
    ran = nj_field_lookup(vm, "ThreadWorker", "ran", "I", 1);
    CHECK(ran != NULL && nj_field_read_int(vm, NULL, ran) == 1);
    /* Joining an already completed worker must not park the C caller. */
    CHECK(nj_call(vm, worker, "java/lang/Thread", "join", "()V", NULL, NULL) == NJ_OK);
    CHECK(main_thread->state == NJ_THREAD_RUNNING);
    /* C -> Java constructor -> join -> scheduler -> worker return -> notify. */
    bridge = make(vm, "ThreadBridge");
    CHECK(bridge != NULL && nj_field_read_int(vm, NULL, ran) == 2);
    CHECK(main_thread->state == NJ_THREAD_RUNNING);
    /* A constructor's untimed wait must still be woken by a Runnable worker. */
    bridge = make(vm, "ThreadWaitBridge");
    CHECK(bridge != NULL);
    CHECK(nj_field_read_int(vm, bridge, nj_field_lookup(vm, "ThreadWaitBridge", "done", "Z", 0)) == 1);
    CHECK(main_thread->state == NJ_THREAD_RUNNING);
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    printf("thread lifecycle: %d failures\n", failures);
    return failures ? 1 : 0;
}
