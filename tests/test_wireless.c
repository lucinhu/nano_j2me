/*
 * nano_j2me - test_wireless.c
 *
 * JSR 120 (messaging) and JSR 82 (Bluetooth) exist as classes, and every
 * operation that would need a radio says so.
 *
 * The failure these classes fix was not a missing feature, it was a dead
 * title.  A MIDlet that wants to know whether the handset has Bluetooth asks
 * for the class and falls back when it is absent - and
 *
 *     Class.forName("javax.bluetooth.DiscoveryAgent")
 *       -> ClassNotFoundException, game never draws   (三国杀2．2美化版)
 *     MessageConnection
 *       -> NoClassDefFoundError on first use          (凡人修仙传2-乱星海)
 *
 * is indistinguishable from a broken install.  Having no radio is a normal
 * answer; not having the class is not.  So the test asserts three things:
 * the classes are findable, the one concrete Bluetooth operation throws the
 * JSR's own BluetoothStateException rather than something invented here, and
 * a name that really does not exist still throws ClassNotFoundException - the
 * last one because a lookup that always succeeds would make the first two
 * meaningless.
 */
#include "nj2me/nj.h"
#include "nj_internal.h"

#include <stdio.h>
#include <string.h>

static int g_fail;
static int g_checks;

static void ok(int cond, const char *what)
{
    g_checks++;
    if (cond) {
        printf("  ok    %s\n", what);
    } else {
        g_fail++;
        printf("  FAIL  %s\n", what);
    }
}

static const char *pending_exc(nj_vm *vm)
{
    nj_ref e = vm->exception;
    if (!e || !e->cls) {
        return NULL;
    }
    return e->cls->name;
}

/* Class.forName(name) != null, with the exception drained either way. */
static int forname(nj_vm *vm, const char *name)
{
    nj_value a[1];
    nj_value rv;
    nj_ref s = nj_string_new(vm, name);
    int found;

    if (!s) {
        return 0;
    }
    rv.r = NULL;
    a[0].r = s;
    (void)nj_call_static(vm, "java/lang/Class", "forName",
                         "(Ljava/lang/String;)Ljava/lang/Class;", a, &rv);
    found = (rv.r != NULL) && (pending_exc(vm) == NULL);
    (void)nj_take_exception(vm);
    return found;
}

int main(void)
{
    nj_port port;
    nj_vm *vm;
    static const char *classes[] = {
        "javax/bluetooth/DiscoveryAgent",
        "javax/bluetooth/DiscoveryListener",
        "javax/bluetooth/LocalDevice",
        "javax/bluetooth/RemoteDevice",
        "javax/bluetooth/UUID",
        "javax/wireless/messaging/Message",
        "javax/wireless/messaging/MessageConnection",
        "javax/wireless/messaging/TextMessage",
        "javax/wireless/messaging/BinaryMessage"
    };
    unsigned i;

    printf("== JSR 120 / JSR 82 ==\n");

    if (nj_port_headless_ex(&port, 240, 320, 0, NULL) != NJ_OK) {
        printf("  FAIL  headless port\n");
        return 1;
    }
    vm = nj_vm_create(&port, 1024u * 1024u);
    if (!vm) {
        printf("  FAIL  vm\n");
        nj_port_shutdown(&port);
        return 1;
    }

    for (i = 0; i < sizeof(classes) / sizeof(classes[0]); i++) {
        char msg[80];
        snprintf(msg, sizeof(msg), "Class.forName(%s)", classes[i]);
        ok(forname(vm, classes[i]), msg);
    }

    /*
     * The control.  Without it, `Class.forName` returning non-NULL for
     * everything - which is what a broken lookup would look like - would pass
     * every assertion above.
     */
    ok(!forname(vm, "javax/bluetooth/NoSuchClassAtAll"),
       "control: a name that does not exist still fails");

    /* getLocalDevice() is the call that reports whether there is a radio. */
    {
        nj_value rv;
        rv.r = NULL;
        (void)nj_call_static(vm, "javax/bluetooth/LocalDevice", "getLocalDevice",
                             "()Ljavax/bluetooth/LocalDevice;", NULL, &rv);
        ok(rv.r == NULL && pending_exc(vm) != NULL &&
               strcmp(pending_exc(vm), "javax/bluetooth/BluetoothStateException") == 0,
           "LocalDevice.getLocalDevice() throws BluetoothStateException");
        (void)nj_take_exception(vm);
    }

    /* A UUID a MIDlet can actually build, and one it cannot. */
    {
        nj_ref u = nj_object_new(vm, "javax/bluetooth/UUID");
        nj_value a[1];
        nj_ref s = nj_string_new(vm, "1101");
        a[0].r = s;
        if (u) {
            nj_value rv;
            rv.r = NULL;
            (void)nj_call_special(vm, u, "javax/bluetooth/UUID", "<init>",
                                  "(Ljava/lang/String;)V", a, &rv);
        }
        ok(u != NULL && pending_exc(vm) == NULL, "new UUID(\"1101\") works");
        (void)nj_take_exception(vm);

        a[0].r = nj_string_new(vm, "zzzz");
        u = nj_object_new(vm, "javax/bluetooth/UUID");
        if (u) {
            nj_value rv;
            rv.r = NULL;
            (void)nj_call_special(vm, u, "javax/bluetooth/UUID", "<init>",
                                  "(Ljava/lang/String;)V", a, &rv);
        }
        ok(pending_exc(vm) != NULL &&
               strcmp(pending_exc(vm), "java/lang/IllegalArgumentException") == 0,
           "new UUID(\"zzzz\") throws IllegalArgumentException");
        (void)nj_take_exception(vm);
    }

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);

    printf("%s (%d checks, %d failures)\n",
           g_fail ? "FAILED" : "PASS", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
