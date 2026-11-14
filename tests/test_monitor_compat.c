/* Monitor ownership at Thread.run() entry.
 *
 * A synchronized run() owns the receiver's monitor before its first bytecode,
 * exactly like any other synchronized method.  nj_thread_start() enters run()
 * itself instead of going through interp_invoke(), so it has to take the
 * monitor by hand; when a failed corpus title called notifyAll() from a
 * `public synchronized void run()` and the runtime had not acquired the
 * monitor, the VM correctly threw IllegalMonitorStateException and the title
 * died on its first frame.
 *
 * Fixture: SyncRunThread extends Thread with
 *   public static int runs;
 *   public synchronized void run() { notifyAll(); runs++; }
 * notifyAll() is the ownership probe: it throws IMS unless run() already owns
 * the Thread object.  Compiled once with `javac -source 8 -target 8 -g:none`,
 * embedded so the test needs no JDK at run time.
 */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d: %s\n", __LINE__, #c); failures++; } } while (0)

static const nj_u1 SyncRunThread[] = {
    202, 254, 186, 190, 0, 0, 0, 52, 0, 20, 10, 0,
    2, 0, 3, 7, 0, 4, 12, 0, 5, 0, 6, 1,
    0, 16, 106, 97, 118, 97, 47, 108, 97, 110, 103, 47,
    84, 104, 114, 101, 97, 100, 1, 0, 6, 60, 105, 110,
    105, 116, 62, 1, 0, 3, 40, 41, 86, 10, 0, 8,
    0, 9, 7, 0, 10, 12, 0, 11, 0, 6, 1, 0,
    16, 106, 97, 118, 97, 47, 108, 97, 110, 103, 47, 79,
    98, 106, 101, 99, 116, 1, 0, 9, 110, 111, 116, 105,
    102, 121, 65, 108, 108, 9, 0, 13, 0, 14, 7, 0,
    15, 12, 0, 16, 0, 17, 1, 0, 13, 83, 121, 110,
    99, 82, 117, 110, 84, 104, 114, 101, 97, 100, 1, 0,
    4, 114, 117, 110, 115, 1, 0, 1, 73, 1, 0, 4,
    67, 111, 100, 101, 1, 0, 3, 114, 117, 110, 0, 33,
    0, 13, 0, 2, 0, 0, 0, 1, 0, 9, 0, 16,
    0, 17, 0, 0, 0, 2, 0, 1, 0, 5, 0, 6,
    0, 1, 0, 18, 0, 0, 0, 17, 0, 1, 0, 1,
    0, 0, 0, 5, 42, 183, 0, 1, 177, 0, 0, 0,
    0, 0, 33, 0, 19, 0, 6, 0, 1, 0, 18, 0,
    0, 0, 25, 0, 2, 0, 1, 0, 0, 0, 13, 42,
    182, 0, 7, 178, 0, 12, 4, 96, 179, 0, 12, 177,
    0, 0, 0, 0, 0, 0,
};

static nj_ref make_thread(nj_vm *vm)
{
    nj_ref o = nj_root(vm, nj_object_new(vm, "SyncRunThread"));

    CHECK(o != NULL);
    if (o) {
        CHECK(nj_call_special(vm, o, "SyncRunThread", "<init>", "()V", NULL, NULL) == NJ_OK);
    }
    return o;
}

static void pump(nj_vm *vm, int steps)
{
    int i;

    for (i = 0; i < steps; i++) {
        if (nj_scheduler_step(vm) != NJ_OK) {
            break;
        }
    }
}

/* A synchronized run() on a free monitor: it must own the Thread object
 * before its first bytecode, or notifyAll() inside it throws IMS. */
static void test_uncontended(nj_vm *vm, nj_field *runs)
{
    nj_ref o = make_thread(vm);
    int before = nj_field_read_int(vm, NULL, runs);

    CHECK(o != NULL);
    if (!o) {
        return;
    }
    CHECK(nj_call(vm, o, "java/lang/Thread", "start", "()V", NULL, NULL) == NJ_OK);
    pump(vm, 400);
    CHECK(vm->exception == NULL);
    CHECK(nj_field_read_int(vm, NULL, runs) == before + 1);
}

/* The contended half of the same invariant: a thread whose synchronized run()
 * cannot get the receiver's monitor parks before pc 0, and the owner's
 * monitorexit hands it over so the body then runs. */
static void test_contended(nj_vm *vm, nj_field *runs)
{
    nj_thread *main_thread = nj_thread_main(vm);
    nj_ref o = make_thread(vm);
    nj_thread *worker = NULL;
    int before = nj_field_read_int(vm, NULL, runs);
    nj_u2 i;

    CHECK(o != NULL);
    if (!o) {
        return;
    }
    CHECK(nj_vm_monitor_enter(vm, main_thread, o) == 0);
    CHECK(nj_call(vm, o, "java/lang/Thread", "start", "()V", NULL, NULL) == NJ_OK);
    pump(vm, 200);
    for (i = 0; i < vm->nthreads; i++) {
        if (vm->threads[i] && vm->threads[i]->obj == o) {
            worker = vm->threads[i];
            break;
        }
    }
    CHECK(worker != NULL);
    if (worker) {
        CHECK(worker->state == NJ_THREAD_BLOCKED);
        CHECK(worker->wait_kind == NJ_WAIT_ENTER);
        CHECK(worker->nframes == 1);
        CHECK(worker->monitors[0] == NULL);
    }
    CHECK(nj_field_read_int(vm, NULL, runs) == before);
    CHECK(nj_vm_monitor_exit(vm, main_thread, o) == 0);
    pump(vm, 400);
    CHECK(vm->exception == NULL);
    CHECK(nj_field_read_int(vm, NULL, runs) == before + 1);
}

int main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_field *runs;

    memset(&port, 0, sizeof(port));
    CHECK(nj_port_headless(&port) == NJ_OK);
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    CHECK(vm != NULL);
    if (!vm) {
        return 1;
    }
    CHECK(nj_class_load(vm, "SyncRunThread", SyncRunThread,
                        sizeof(SyncRunThread)) != NULL);
    runs = nj_field_lookup(vm, "SyncRunThread", "runs", "I", 1);
    CHECK(runs != NULL);
    vm->running = 1;
    if (runs) {
        test_uncontended(vm, runs);
        test_contended(vm, runs);
    }
    nj_vm_destroy(vm);
    if (failures) {
        fprintf(stderr, "test_monitor_compat: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_monitor_compat: ok\n");
    return 0;
}
