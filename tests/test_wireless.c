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

/* LocalDevice.getProperty(key): the Java string as UTF-8, or NULL when the
 * stack answered null (its word for "unknown") or threw. */
static const char *prop(nj_vm *vm, nj_ref dev, const char *key, char *buf,
                        int cap)
{
    nj_value a[1];
    nj_value rv;

    buf[0] = 0;
    a[0].r = nj_string_new(vm, key);
    rv.r = NULL;
    (void)nj_call(vm, dev, "javax/bluetooth/LocalDevice", "getProperty",
                  "(Ljava/lang/String;)Ljava/lang/String;", a, &rv);
    if (!rv.r || pending_exc(vm)) {
        (void)nj_take_exception(vm);
        return NULL;
    }
    (void)nj_string_to_utf8(rv.r, buf, cap);
    return buf;
}

/* A radio is a port hook, so a test can simply hand the plain headless port
 * one.  This is the smallest thing that answers bt_local; nothing else about
 * the fixture is needed to reach getLocalDevice() and its property table. */
static nj_status fake_bt_local(void *ud, nj_bt_local *out)
{
    (void)ud;
    if (!out) {
        return NJ_ERR_UNSUPPORTED;
    }
    memset(out, 0, sizeof(*out));
    strcpy(out->address, "001122334455");
    strcpy(out->name, "nano_j2me test radio");
    out->device_class = 0x10020Cu;
    out->discoverable = 0x9E8B33;
    out->inquiry_ms = 300;
    return NJ_OK;
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

    /*
     * The JSR 120 interface surface a real title resolves against.  A shim
     * class of the title's own implements MessageConnection and registers
     * itself through the interface, so every method the JSR declares has to
     * exist here even though this runtime never hands out a connection:
     * 魂斗罗V铁血兵团_119 died with NoSuchMethodError on setMessageListener.
     */
    {
        /* nj_class_lookup has no public prototype; the test talks to the VM
         * the same way the runtime does. */
        extern nj_class *nj_class_lookup(nj_vm *vm, const char *name);
        nj_class *mc = nj_class_lookup(vm, "javax/wireless/messaging/MessageConnection");

        ok(mc != NULL, "MessageConnection class resolves");
        ok(mc != NULL && nj_method_lookup(
               mc, "setMessageListener",
               "(Ljavax/wireless/messaging/MessageListener;)V") != NULL,
           "MessageConnection declares setMessageListener(MessageListener)");
        ok(mc != NULL && nj_method_lookup(
               mc, "getMessageListener",
               "()Ljavax/wireless/messaging/MessageListener;") != NULL,
           "MessageConnection declares getMessageListener()");
        ok(mc != NULL && nj_method_lookup(
               mc, "newMessage", "(Ljava/lang/String;Ljava/lang/String;)"
               "Ljavax/wireless/messaging/Message;") != NULL,
           "MessageConnection declares newMessage(String, String)");
        ok(mc != NULL && nj_method_lookup(
               mc, "setMessageListener", "()V") == NULL,
           "control: a wrong descriptor does not resolve");
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

        /* The JSR-82 short form: hex text, right-aligned in the base UUID. */
        {
            nj_ref u2 = nj_object_new(vm, "javax/bluetooth/UUID");
            nj_value a2[2];
            nj_value rv;
            a2[0].r = nj_string_new(vm, "1101");
            a2[1].i = 1;
            rv.r = NULL;
            if (u2) {
                (void)nj_call_special(vm, u2, "javax/bluetooth/UUID", "<init>",
                                      "(Ljava/lang/String;Z)V", a2, &rv);
            }
            ok(u2 != NULL && pending_exc(vm) == NULL,
               "new UUID(\"1101\", true) works");
            (void)nj_take_exception(vm);

            u2 = nj_object_new(vm, "javax/bluetooth/UUID");
            a2[0].r = nj_string_new(vm, "123456789");
            rv.r = NULL;
            if (u2) {
                (void)nj_call_special(vm, u2, "javax/bluetooth/UUID", "<init>",
                                      "(Ljava/lang/String;Z)V", a2, &rv);
            }
            ok(pending_exc(vm) != NULL &&
                   strcmp(pending_exc(vm),
                          "java/lang/IllegalArgumentException") == 0,
               "new UUID(nine digits, true) throws IllegalArgumentException");
            (void)nj_take_exception(vm);
        }

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

    /*
     * The property table, and why this needs a radio at all: without one
     * getLocalDevice() throws above and the table is unreachable.  The three
     * names are the specification's own; the first one is 33 characters long,
     * which is one more than the 32-byte buffer this code used to copy it
     * into.  The truncated copy could never match, so that property was
     * silently always null - and only a trimmed build under -Werror said so.
     */
    if (nj_port_headless_ex(&port, 240, 320, 0, NULL) != NJ_OK) {
        printf("  FAIL  headless port with a fake radio\n");
        return 1;
    }
    port.bt_local = fake_bt_local;
    vm = nj_vm_create(&port, 1024u * 1024u);
    if (!vm) {
        printf("  FAIL  vm with a fake radio\n");
        nj_port_shutdown(&port);
        return 1;
    }
    {
        nj_value rv;
        nj_ref dev;
        char buf[32];

        rv.r = NULL;
        (void)nj_call_static(vm, "javax/bluetooth/LocalDevice",
                             "getLocalDevice",
                             "()Ljavax/bluetooth/LocalDevice;", NULL, &rv);
        dev = rv.r;
        ok(dev != NULL && pending_exc(vm) == NULL,
           "a fake radio makes getLocalDevice() succeed");
        ok(dev != NULL &&
               prop(vm, dev, "bluetooth.sd.attr.retrievable.max", buf,
                    (int)sizeof(buf)) != NULL &&
               strcmp(buf, "64") == 0,
           "getProperty(\"bluetooth.sd.attr.retrievable.max\") is \"64\"");
        ok(dev != NULL &&
               prop(vm, dev, "bluetooth.connected.devices.max", buf,
                    (int)sizeof(buf)) != NULL &&
               strcmp(buf, "7") == 0,
           "getProperty(\"bluetooth.connected.devices.max\") is \"7\"");
        ok(dev != NULL &&
               prop(vm, dev, "bluetooth.sd.trans.max", buf,
                    (int)sizeof(buf)) != NULL &&
               strcmp(buf, "1") == 0,
           "getProperty(\"bluetooth.sd.trans.max\") is \"1\"");
        ok(dev != NULL &&
               prop(vm, dev, "bluetooth.no.such.property", buf,
                    (int)sizeof(buf)) == NULL,
           "control: an undefined property is null, not invented");
    }
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);

    printf("%s (%d checks, %d failures)\n",
           g_fail ? "FAILED" : "PASS", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
