/*
 * nano_j2me - tests/test_lifecycle.c
 *
 * The memory contract, tested on its own terms.
 *
 * The runtime claims three things:
 *
 *   1. nj_vm_destroy() releases EVERYTHING.  It returns how many host
 *      allocations it had to sweep, and that number must be identical on every
 *      create/run/destroy cycle - a drift is a leak.
 *   2. Several nj_vm instances can coexist, because no state is global.
 *   3. A failed operation leaves the VM usable, so an embedder can offer
 *      "back to the menu, pick another game" on a device that never reboots.
 *
 * Run this under ASan with detect_leaks=1 as well: the counters here prove the
 * accounting is stable, and the leak checker proves the accounting is honest.
 *
 * Usage: test_lifecycle
 */
#include "nj2me/nj.h"
#include "nj_internal.h"   /* nj_thread: whose thread the host is running on */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
static int failures;

#define CHECK(cond, what)                                                    \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, (what));        \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b, what)                                                 \
    do {                                                                     \
        unsigned long va_ = (unsigned long)(a);                              \
        unsigned long vb_ = (unsigned long)(b);                              \
        checks++;                                                            \
        if (va_ != vb_) {                                                    \
            failures++;                                                      \
            printf("  FAIL %s:%d  %s (%lu != %lu)\n",                        \
                   __FILE__, __LINE__, (what), va_, vb_);                    \
        }                                                                    \
    } while (0)

/* ------------------------------------------------------------------ */
/* 1. Repeated create/destroy with no application                       */
/* ------------------------------------------------------------------ */
static void test_bare_cycle(void)
{
    nj_u4 first = 0u;
    nj_size first_peak = 0u;
    int i;

    printf("bare create/destroy cycles\n");
    for (i = 0; i < 8; i++) {
        nj_port port;
        nj_vm *vm;
        nj_vm_stats st;
        nj_u4 released;

        CHECK(nj_port_headless_ex(&port, 240, 320, 0, NULL) == NJ_OK,
              "headless port");
        vm = nj_vm_create(&port, 1024u * 1024u);
        CHECK(vm != NULL, "vm created");
        if (!vm) {
            nj_port_shutdown(&port);
            return;
        }
        nj_vm_get_stats(vm, &st);
        released = nj_vm_destroy(vm);

        if (i == 0) {
            first = released;
            first_peak = st.host_peak;
            CHECK(released > 0u,
                  "destroy must report the blocks it reclaimed");
            CHECK(st.classes > 0u, "the builtin class library is loaded");
        } else {
            CHECK_EQ(released, first, "teardown size is identical every cycle");
            CHECK_EQ(st.host_peak, first_peak,
                     "host allocation peak does not grow");
        }
        nj_port_shutdown(&port);
    }
}

/* ------------------------------------------------------------------ */
/* 2. Several VMs alive at once                                         */
/* ------------------------------------------------------------------ */
static void test_coexistence(void)
{
    enum { N = 3 };
    nj_port ports[N];
    nj_vm *vms[N];
    nj_u4 released[N];
    int i;

    printf("three VMs alive simultaneously\n");
    memset(ports, 0, sizeof(ports));
    memset(vms, 0, sizeof(vms));

    for (i = 0; i < N; i++) {
        nj_ref o;
        CHECK(nj_port_headless_ex(&ports[i], 128 + i * 16, 160, 0, NULL)
              == NJ_OK, "port");
        vms[i] = nj_vm_create(&ports[i], 512u * 1024u);
        CHECK(vms[i] != NULL, "vm");
        if (!vms[i]) {
            continue;
        }
        /* Give each VM its own objects so their heaps genuinely diverge. */
        o = nj_object_new(vms[i], "java/lang/Object");
        CHECK(o != NULL, "object in vm");
        CHECK_EQ(nj_array_length(nj_int_array_new(vms[i], 100u + (nj_u4)i)),
                 100u + (nj_u4)i, "array length in vm");
    }

    /* Destroy out of order: nothing may depend on creation order. */
    released[1] = nj_vm_destroy(vms[1]);
    released[2] = nj_vm_destroy(vms[2]);
    released[0] = nj_vm_destroy(vms[0]);
    for (i = 0; i < N; i++) {
        CHECK(released[i] > 0u, "each VM released its own memory");
        nj_port_shutdown(&ports[i]);
    }
    CHECK_EQ(released[0], released[1],
             "identical VMs release identical amounts");
    CHECK_EQ(released[1], released[2],
             "identical VMs release identical amounts");
}

/* ------------------------------------------------------------------ */
/* 3. Failure leaves the VM usable                                      */
/* ------------------------------------------------------------------ */
static void test_failure_recovery(void)
{
    nj_port port;
    nj_vm *vm;
    nj_ref o;
    nj_u4 released;
    int i;

    printf("recovery after failures\n");
    CHECK(nj_port_headless_ex(&port, 240, 320, 0, NULL) == NJ_OK, "port");
    vm = nj_vm_create(&port, 1024u * 1024u);
    CHECK(vm != NULL, "vm");
    if (!vm) {
        nj_port_shutdown(&port);
        return;
    }

    /* A missing class must raise, not crash, and must not poison the VM. */
    for (i = 0; i < 50; i++) {
        nj_ref bad = nj_object_new(vm, "com/example/DoesNotExist");
        CHECK(bad == NULL, "unknown class yields NULL");
        CHECK(nj_has_exception(vm), "an exception was raised");
        {
            const char *d = nj_exception_describe(vm);
            CHECK(d != NULL && d[0] != '\0', "it is describable");
        }
        nj_clear_exception(vm);
        CHECK(!nj_has_exception(vm), "cleared");
    }

    /* The VM still works afterwards. */
    o = nj_object_new(vm, "java/lang/Object");
    CHECK(o != NULL, "the VM is still usable");
    CHECK_EQ(nj_vm_destroy(NULL), 0u, "nj_vm_destroy(NULL) is safe");
    {
        nj_vm_stats st;
        nj_vm_get_stats(vm, &st);
        CHECK(st.host_live > 0u, "host blocks are accounted for");
    }

    released = nj_vm_destroy(vm);
    CHECK(released > 0u, "still tears down completely");

    /* Shutting a port down twice must be harmless: nj_run does it, and a
     * caller that also does it must not double-free. */
    nj_port_shutdown(&port);
    nj_port_shutdown(&port);
}

/* ------------------------------------------------------------------ */
/* 4. Heap high-water does not creep                                    */
/* ------------------------------------------------------------------ */
static void test_heap_stability(void)
{
    nj_vm_stats first;
    int i;

    printf("heap high-water across restarts\n");
    memset(&first, 0, sizeof(first));
    for (i = 0; i < 5; i++) {
        nj_port port;
        nj_vm *vm;
        nj_vm_stats st;
        int k;

        CHECK(nj_port_headless_ex(&port, 240, 320, 0, NULL) == NJ_OK, "port");
        vm = nj_vm_create(&port, 1024u * 1024u);
        CHECK(vm != NULL, "vm");
        if (!vm) {
            nj_port_shutdown(&port);
            return;
        }
        /* Churn: allocate and drop enough objects to force collections. */
        for (k = 0; k < 400; k++) {
            nj_ref a = nj_int_array_new(vm, 64u);
            nj_ref s = nj_string_new(vm, "lifecycle");
            CHECK(a != NULL && s != NULL, "allocation");
        }
        (void)nj_gc_collect(vm);
        nj_vm_get_stats(vm, &st);
        if (i == 0) {
            first = st;
        } else {
            CHECK_EQ(st.classes, first.classes, "class count stable");
            CHECK_EQ(st.host_peak, first.host_peak, "host peak stable");
        }
        nj_vm_destroy(vm);
        nj_port_shutdown(&port);
    }
}

/* ------------------------------------------------------------------ */
/* 5. The host gets its own thread back after every scheduling round    */
/* ------------------------------------------------------------------ */
/*
 * The frame loop is C: it drains key events, then repaints.  In between, the
 * scheduler runs the MIDlet's background threads and leaves vm->cur on
 * whichever thread ran last - and for a title with its own animation loop that
 * is a thread parked in Thread.sleep().  Every call the host makes from C
 * starts at vm->cur, so paint() and the key callbacks were being dispatched
 * onto that sleeping thread: nj_exec yielded at once, vm_invoke turned the
 * yield into NJ_ERR_TIMEOUT, and the canvas logged "paint() failed without
 * raising an exception" on every frame while the keys went nowhere.  A title
 * whose own thread sleeps is the normal case, not an exotic one.
 */
static void test_frame_thread_ownership(void)
{
    nj_port port;
    nj_vm *vm;
    nj_thread *bg;

    printf("the host runs on the MIDlet's thread, not the scheduler's\n");
    CHECK(nj_port_headless_ex(&port, 240, 320, 0, NULL) == NJ_OK, "port");
    vm = nj_vm_create(&port, 1024u * 1024u);
    CHECK(vm != NULL, "vm created");
    if (!vm) {
        nj_port_shutdown(&port);
        return;
    }

    CHECK(nj_thread_main(vm) == nj_thread_self(vm),
          "a fresh VM runs on its own thread");

    bg = nj_thread_create(vm, NULL, 0);
    CHECK(bg != NULL, "background thread created");
    if (bg) {
        /* Runnable, with no frames to run: the scheduler selects it and
         * finishes the round current on it, exactly as a game thread does. */
        bg->state = NJ_THREAD_RUNNING;
        CHECK(nj_scheduler_step(vm) == NJ_OK, "scheduling round");
        CHECK(nj_thread_self(vm) == nj_thread_main(vm),
              "the round hands the host back the MIDlet's thread");
        CHECK(nj_thread_self(vm) != bg,
              "and not the background thread it ran last");
    }

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
}

static nj_u4 stationary_clock(void *ud) { (void)ud; return 0u; }

static void test_frame_compute_budget(void)
{
    static const nj_u1 code[] = { 0x84, 0x00, 0x01, 0xa7, 0xff, 0xfd };
    nj_port port;
    nj_vm *vm;
    nj_thread *a, *b;
    nj_method method;
    nj_s4 first, one_round;
    CHECK(nj_port_headless_ex(&port, 240, 320, 0, NULL) == NJ_OK, "port");
    port.millis = stationary_clock;
    vm = nj_vm_create(&port, 1024u * 1024u);
    CHECK(vm != NULL, "budget VM");
    if (!vm) { nj_port_shutdown(&port); return; }
    memset(&method, 0, sizeof(method));
    method.name = "compute";
    method.desc = "()V";
    method.code = code;
    method.code_len = sizeof(code);
    method.max_locals = 1;
    method.max_stack = 1;
    a = nj_thread_create(vm, NULL, 0);
    b = nj_thread_create(vm, NULL, 0);
    CHECK(a && b, "two compute threads");
    if (a && b) {
        CHECK(nj_frame_push(vm, a, &method, a->top, 0) == NJ_OK, "first loop");
        CHECK(nj_frame_push(vm, b, &method, b->top, 0) == NJ_OK, "second loop");
        a->state = b->state = NJ_THREAD_RUNNING;
        vm->running = 1;
        CHECK(nj_scheduler_frame(vm, 0u) == NJ_OK, "one scheduling round");
        first = a->frames[0].locals[0].i;
        one_round = first;
        CHECK(first > 0, "compute progressed");
        CHECK(nj_scheduler_frame(vm, 8u) == NJ_OK, "bounded stationary clock");
        CHECK(a->frames[0].locals[0].i > first, "multiple quanta per frame");
        CHECK_EQ(a->frames[0].locals[0].i, b->frames[0].locals[0].i,
                 "both threads get equal quanta");
        CHECK(nj_thread_self(vm) == nj_thread_main(vm), "host thread restored");
        first = a->frames[0].locals[0].i;
        vm->painted_in_frame = 1;
        CHECK(nj_scheduler_frame(vm, 8u) == NJ_OK, "present stops extra rounds");
        CHECK_EQ(a->frames[0].locals[0].i, first + one_round,
                 "a presented frame gets only one round");
    }
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
}

int main(void)
{
    printf("== nano_j2me lifecycle and memory contract ==\n");

    test_bare_cycle();
    test_coexistence();
    test_failure_recovery();
    test_heap_stability();
    test_frame_thread_ownership();
    test_frame_compute_budget();

    printf("\nchecks: %d  failures: %d\n", checks, failures);
    if (failures == 0) {
        printf("PASS\n");
        printf("run again under -fsanitize=address,undefined with\n"
               "ASAN_OPTIONS=detect_leaks=1 for the process-level proof\n");
        return 0;
    }
    printf("FAIL\n");
    return 1;
}
