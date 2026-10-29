/*
 * com.nokia.mid.ui.multipointtouch.MultipointTouch.
 *
 * Minecraft PE 0.6.1 registers a MultipointTouchListener, and every pointer
 * event the game acts on arrives through pointersChanged().  Before this test
 * the class existed only in its older Series 40 shape: no getInstance(), no
 * getState()/getX()/getY(), and no pointer ever reached a listener.  The game
 * caught the NoSuchMethodError and quietly fell back to keypad-only play.
 *
 * This test pins the Asha API and the state the runtime records for pointer 0.
 * A listener needs a Java implementation, so the delivery itself is exercised
 * by the real title under the desktop port; what is checked here is the class
 * surface and the coordinates getState()/getX()/getY() hand back.
 */
#include <stdio.h>
#include <string.h>

#include "nj2me/nj.h"

#include "nj_internal.h"
#include "nj_midp.h"

static int g_pass;
static int g_fail;

#define OK(cond, msg)                                                          \
    do {                                                                       \
        if (cond) {                                                            \
            g_pass++;                                                          \
        } else {                                                               \
            g_fail++;                                                          \
            printf("FAIL %d: %s\n", __LINE__, (msg));                          \
        }                                                                      \
    } while (0)

#define MPT "com/nokia/mid/ui/multipointtouch/MultipointTouch"
#define MPTL "com/nokia/mid/ui/multipointtouch/MultipointTouchListener"

static nj_s4 constant(nj_vm *vm, const char *name)
{
    nj_field *f = nj_field_lookup(vm, MPT, name, "I", 1);

    return f ? nj_field_read_int(vm, NULL, f) : -9999;
}

static nj_s4 call_int(nj_vm *vm, const char *name, nj_s4 arg)
{
    nj_value a;
    nj_value r;

    a.i = arg;
    r.i = -9999;
    if (nj_call_static(vm, MPT, name, "(I)I", &a, &r) != NJ_OK) {
        return -9998;
    }
    return r.i;
}

int main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_value r;
    nj_ref inst, again;
    nj_value arg;

    printf("== nano_j2me Nokia MultipointTouch test ==\n");

    memset(&port, 0, sizeof(port));
    if (nj_port_headless(&port) != NJ_OK) {
        fprintf(stderr, "cannot start the headless port\n");
        return 2;
    }
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    if (!vm) {
        fprintf(stderr, "cannot create the VM\n");
        nj_port_shutdown(&port);
        return 2;
    }

    OK(nj_class_lookup(vm, MPT) != NULL, "MultipointTouch exists");
    OK(nj_class_lookup(vm, MPTL) != NULL, "the listener interface exists");

    OK(constant(vm, "POINTER_PRESSED") == 0, "POINTER_PRESSED is 0");
    OK(constant(vm, "POINTER_DRAGGED") == 1, "POINTER_DRAGGED is 1");
    OK(constant(vm, "POINTER_RELEASED") == 2, "POINTER_RELEASED is 2");

    r.r = NULL;
    OK(nj_call_static(vm, MPT, "getInstance", "()L" MPT ";", NULL, &r) == NJ_OK,
       "getInstance() is callable");
    inst = r.r;
    OK(inst != NULL, "getInstance() returns an object");
    r.r = NULL;
    OK(nj_call_static(vm, MPT, "getInstance", "()L" MPT ";", NULL, &r) == NJ_OK,
       "getInstance() is callable twice");
    again = r.r;
    OK(again == inst, "getInstance() returns the same singleton");

    OK(call_int(vm, "getState", 0) == 2, "a pointer starts released");
    OK(call_int(vm, "getX", 0) == 0, "a pointer starts at x = 0");
    OK(call_int(vm, "getY", 0) == 0, "a pointer starts at y = 0");

    nj_nokia_mpt_pointer(vm, 12, 34, NJ_POINTER_DOWN);
    OK(call_int(vm, "getState", 0) == 0, "a press is reported as pressed");
    OK(call_int(vm, "getX", 0) == 12, "getX() reports the pressed x");
    OK(call_int(vm, "getY", 0) == 34, "getY() reports the pressed y");

    nj_nokia_mpt_pointer(vm, 15, 40, NJ_POINTER_MOVE);
    OK(call_int(vm, "getState", 0) == 1, "a drag is reported as dragged");
    OK(call_int(vm, "getX", 0) == 15, "getX() follows the drag");

    nj_nokia_mpt_pointer(vm, 15, 40, NJ_POINTER_UP);
    OK(call_int(vm, "getState", 0) == 2, "a release is reported as released");
    OK(call_int(vm, "getState", -1) == 0, "a negative pointer id is answered");
    OK(call_int(vm, "getState", 99) == 0, "an id beyond the table is answered");

    /* Registering and removing a listener must not need a Java implementation
     * here; the listener itself is checked by the title. */
    arg.r = NULL;
    OK(nj_call(vm, inst, MPT, "addMultipointTouchListener",
               "(L" MPTL ";)V", &arg, NULL) == NJ_OK,
       "a null listener is accepted");
    OK(nj_call(vm, inst, MPT, "removeMultipointTouchListener",
               "(L" MPTL ";)V", &arg, NULL) == NJ_OK,
       "removing it is accepted");
    OK(nj_call_static(vm, MPT, "setMultipointTouchListener",
                      "(L" MPTL ";)V", &arg, NULL) == NJ_OK,
       "the legacy static registration is still accepted");

    OK(!nj_has_exception(vm), "no exception was left pending");

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
