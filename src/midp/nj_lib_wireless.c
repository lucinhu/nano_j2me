/*
 * nano_j2me - nj_lib_wireless.c
 *
 * JSR 120 (Wireless Messaging) and JSR 82 (Bluetooth), as far as a desktop,
 * a phone emulator or an ESP32 needs them: the classes exist, and every
 * operation that would need a radio says so instead of taking the MIDlet down.
 *
 * Why these exist at all
 * ----------------------
 * Both gaps showed up the same way - not as a missing feature but as a dead
 * title:
 *
 *   Class.forName("javax.bluetooth.DiscoveryAgent")
 *     -> ClassNotFoundException, game never draws          (三国杀2．2美化版)
 *   javax/wireless/messaging/MessageConnection
 *     -> NoClassDefFoundError on first use                 (凡人修仙传2-乱星海)
 *
 * A MIDlet that wants to know whether the handset has Bluetooth asks for the
 * class and falls back when it is absent.  Having no Bluetooth is a normal
 * answer; not having the CLASS is not, because it is indistinguishable from a
 * broken install.  Same for SMS: Connector.open("sms://...") throwing
 * ConnectionNotFoundException is exactly what a device with no radio does.
 *
 * So: the interfaces are declared in full (a MIDlet compiles against them),
 * and the one concrete operation - LocalDevice.getLocalDevice() - throws
 * BluetoothStateException, which is the JSR's own word for "the radio is not
 * available".  DiscoveryAgent exists so that Class.forName succeeds and the
 * game can take its "no Bluetooth" branch.
 *
 * Nothing here allocates, and nothing here is stateful: no writable statics,
 * no per-VM state, nothing to tear down.
 */
#include "nj_internal.h"
#include "nj_midp.h"

/* ---- JSR 120: javax.wireless.messaging ---------------------------- */
#define NJ_C_MSG        "javax/wireless/messaging/Message"
#define NJ_C_MSGCONN    "javax/wireless/messaging/MessageConnection"
#define NJ_C_TEXTMSG    "javax/wireless/messaging/TextMessage"
#define NJ_C_BINMSG     "javax/wireless/messaging/BinaryMessage"

/* ---- JSR 82: javax.bluetooth -------------------------------------- */
#define NJ_C_BTSTATE    "javax/bluetooth/BluetoothStateException"
#define NJ_C_LOCALDEV   "javax/bluetooth/LocalDevice"
#define NJ_C_DISCOVERY  "javax/bluetooth/DiscoveryAgent"
#define NJ_C_DISCLISTEN "javax/bluetooth/DiscoveryListener"
#define NJ_C_UUID       "javax/bluetooth/UUID"
#define NJ_C_REMOTE     "javax/bluetooth/RemoteDevice"

#define NJFN(name) NJ_NATIVE_FN(name)

#define I(name, desc) { name, desc, NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT }
#define M(name, desc, flags) { name, desc, flags }

/* ------------------------------------------------------------------ */
/* JSR 120                                                             */
/* ------------------------------------------------------------------ */
/*
 * Every method of JSR 120 that a MIDlet can name is declared abstract here.
 * That is not a shortcut: MessageConnection is an INTERFACE in the JSR, and
 * the implementation is whatever Connector.open() hands back.  This runtime
 * has no radio, so Connector never returns one, and the methods are
 * unreachable rather than unimplemented.
 */
static const nj_builtin_method msg_methods[] = {
    I("getAddress",     "()Ljava/lang/String;"),
    I("setAddress",     "(Ljava/lang/String;)V"),
    I("getTimestamp",   "()Ljava/util/Date;")
};

static const nj_builtin_method textmsg_methods[] = {
    I("getPayloadText", "()Ljava/lang/String;"),
    I("setPayloadText", "(Ljava/lang/String;)V")
};

static const nj_builtin_method binmsg_methods[] = {
    I("getPayloadData", "()[B"),
    I("setPayloadData", "([B)V")
};

static const nj_builtin_method msgconn_methods[] = {
    I("newMessage",     "(Ljava/lang/String;)L" NJ_C_MSG ";"),
    I("send",           "(L" NJ_C_MSG ";)V"),
    I("receive",        "()L" NJ_C_MSG ";"),
    I("numberOfSegments", "(L" NJ_C_MSG ";)I"),
    I("close",          "()V")
};

/* ------------------------------------------------------------------ */
/* JSR 82                                                              */
/* ------------------------------------------------------------------ */
/*
 * LocalDevice IS concrete in the JSR, and getLocalDevice() is the call that
 * says whether there is a radio.  Throwing BluetoothStateException("no
 * Bluetooth") is the JSR's own answer for that case, and it is what lets a
 * MIDlet's existing try/catch fall back instead of dying.
 *
 * The throw carries a String, which means the constructor it lands in is
 * BluetoothStateException(String) - inherited from IOException.  Exception
 * constructors cannot be declared here (java.io is a different module), and
 * they do not need to be: the runtime resolves the inherited one.
 */
static const nj_builtin_method localdev_methods[] = {
    M("getLocalDevice", "()L" NJ_C_LOCALDEV ";",
      NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("setDiscoverable", "(I)Z", NJ_ACC_PUBLIC),
    M("getRecord",      "(Ljavax/microedition/io/Connection;)Ljavax/bluetooth/ServiceRecord;",
      NJ_ACC_PUBLIC)
};

/*
 * JSR 82 puts the inquiry access codes on DiscoveryAgent, not on LocalDevice.
 * They are declared because a MIDlet can name them, but they are never seeded:
 * every path that reads one first needs a radio, and getLocalDevice() has
 * already thrown by then.  Reading 0 here is unreachable in practice.
 */
static const nj_builtin_field discovery_consts[] = {
    { "NOT_DISCOVERABLE", "I", 0 },
    { "LIAC", "I", 0 },
    { "GIAC", "I", 0 }
};

/*
 * DiscoveryAgent is abstract in the JSR (a MIDlet supplies a DiscoveryListener
 * and the agent does the work).  Only its existence is load-bearing here: the
 * call that fails first is LocalDevice.getLocalDevice(), and by then the game
 * is already in its no-Bluetooth branch.
 */
static const nj_builtin_method discovery_fields_free[] = {
    M("startInquiry", "(ILjavax/bluetooth/DiscoveryListener;)Z",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("cancelInquiry", "(Ljavax/bluetooth/DiscoveryListener;)Z",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("selectService", "(Ljavax/bluetooth/RemoteDevice;I[Ljavax/bluetooth/UUID;)Z",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT)
};

static const nj_builtin_method disclisten_methods[] = {
    I("deviceDiscovered", "(Ljavax/bluetooth/RemoteDevice;Ljavax/bluetooth/DeviceClass;)V"),
    I("servicesDiscovered", "(ILjavax/bluetooth/ServiceRecord;)V"),
    I("inquiryCompleted", "(I)V"),
    I("serviceSearchCompleted", "(II)V")
};

static const nj_builtin_method uuid_methods[] = {
    M("<init>",   "(Ljava/lang/String;)V", NJ_ACC_PUBLIC),
    M("toString", "()Ljava/lang/String;", NJ_ACC_PUBLIC),
    M("equals",   "(Ljava/lang/Object;)Z", NJ_ACC_PUBLIC),
    M("hashCode", "()I", NJ_ACC_PUBLIC)
};

static const nj_builtin_field uuid_fields[] = {
    { "L2CAP", "I", 0 },
    { "RFCOMM", "I", 0 },
    { "OBEX", "I", 0 }
};

static const nj_builtin_method remote_methods[] = {
    M("getBluetoothAddress", "()Ljava/lang/String;", NJ_ACC_PUBLIC),
    M("getFriendlyName",     "(Z)Ljava/lang/String;", NJ_ACC_PUBLIC),
    M("equals",              "(Ljava/lang/Object;)Z", NJ_ACC_PUBLIC),
    M("hashCode",            "()I", NJ_ACC_PUBLIC)
};

/* ------------------------------------------------------------------ */
/* Natives                                                             */
/* ------------------------------------------------------------------ */

/*
 * There is no Bluetooth radio behind this, and saying so is the whole point.
 * The exception is the JSR's own, so a MIDlet that catches
 * BluetoothStateException - which is what every JSR 82 program does - takes
 * the same path it takes on a handset without Bluetooth.
 */
NJFN(bt_no_radio)
{
    (void)ret;
    (void)args;
    return nj_throw(vm, NJ_C_BTSTATE,
                    "no Bluetooth radio on this device");
}

NJFN(bt_set_discoverable)
{
    (void)args;
    ret->i = 0;
    return nj_throw(vm, NJ_C_BTSTATE,
                    "no Bluetooth radio on this device");
}

NJFN(bt_get_record)
{
    (void)args;
    (void)ret;
    return nj_throw(vm, NJ_C_BTSTATE,
                    "no Bluetooth radio on this device");
}

/*
 * UUID.toString()/equals()/hashCode() are reached only after a successful
 * discovery, which cannot happen here, but they are implemented rather than
 * left abstract: a test that walks every declared method must not have to
 * know which ones the emulator can currently reach.  The value is the byte
 * array a UUID was built from, rendered as hexadecimal - the JSR's own
 * format for the short forms.
 */
NJFN(uuid_toString)
{
    /*
     * A UUID here is whatever a MIDlet built; these three methods are only
     * reachable after a successful discovery, which cannot happen without a
     * radio.  They are implemented rather than left abstract so that a test
     * walking every declared method does not have to know which ones an
     * emulator can currently reach - but they are deliberately simple, and
     * identity is the right answer for equals() on objects that never come
     * from a device.
     */
    nj_ref s = nj_string_new(vm, "00000000000000000000000000000000");
    if (!s) {
        return nj_throw(vm, "java/lang/OutOfMemoryError", "out of memory");
    }
    ret->r = s;
    return NJ_OK;
}

NJFN(uuid_equals)
{
    ret->i = (NJ_THIS(args) == NJ_ARG_REF(args, 0)) ? 1 : 0;
    return NJ_OK;
}

NJFN(uuid_hashCode)
{
    ret->i = 0;
    return NJ_OK;
}

/*
 * UUID(String) accepts the short form (4 hex digits, optionally 0x-prefixed)
 * and the 32-digit form.  Nothing is stored: the UUID only has to exist so
 * that `new UUID("1101")` does not die on a missing constructor, and no code
 * that could compare two of them is reachable without a radio.
 */
NJFN(uuid_init)
{
    nj_ref str = NJ_ARG_REF(args, 0);
    char hexbuf[64];
    const char *p;
    nj_s4 len;
    nj_s4 digits = 0;

    if (!str) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", "null UUID");
    }
    if (nj_string_to_utf8(str, hexbuf, (int)sizeof(hexbuf)) < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", "bad UUID text");
    }
    p = hexbuf;
    len = (nj_s4)nj_string_length(str);
    if (len >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        len -= 2;
    }
    while (len-- > 0) {
        char c = *p++;
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F')) {
            digits++;
        } else {
            return nj_throw(vm, "java/lang/IllegalArgumentException",
                            "not a hexadecimal UUID");
        }
    }
    if (digits != 4 && digits != 8 && digits != 32) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "a UUID is 4, 8 or 32 hexadecimal digits");
    }
    return NJ_OK;
}

NJFN(remote_addr)
{
    nj_ref s = nj_string_new(vm, "000000000000");
    if (!s) {
        return nj_throw(vm, "java/lang/OutOfMemoryError", "out of memory");
    }
    ret->r = s;
    return NJ_OK;
}

NJFN(remote_name)
{
    nj_ref s = nj_string_new(vm, "(no device)");
    if (!s) {
        return nj_throw(vm, "java/lang/OutOfMemoryError", "out of memory");
    }
    ret->r = s;
    return NJ_OK;
}

NJFN(bt_false)
{
    (void)args;
    ret->i = 0;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */
const nj_builtin_class nj_lib_wireless_classes[] = {
    { NJ_C_MSG, NULL,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, msg_methods,
      (nj_u2)(sizeof(msg_methods) / sizeof(msg_methods[0])) },
    { NJ_C_MSGCONN, "javax/microedition/io/Connection",
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, msgconn_methods,
      (nj_u2)(sizeof(msgconn_methods) / sizeof(msgconn_methods[0])) },
    { NJ_C_TEXTMSG, NJ_C_MSG,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, textmsg_methods,
      (nj_u2)(sizeof(textmsg_methods) / sizeof(textmsg_methods[0])) },
    { NJ_C_BINMSG, NJ_C_MSG,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, binmsg_methods,
      (nj_u2)(sizeof(binmsg_methods) / sizeof(binmsg_methods[0])) },

    { NJ_C_BTSTATE, "java/io/IOException", NJ_ACC_PUBLIC,
      NULL, 0u, NULL, 0u },
    { NJ_C_LOCALDEV, "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, localdev_methods,
      (nj_u2)(sizeof(localdev_methods) / sizeof(localdev_methods[0])) },
    { NJ_C_DISCOVERY, "java/lang/Object",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      discovery_consts,
      (nj_u2)(sizeof(discovery_consts) / sizeof(discovery_consts[0])),
      discovery_fields_free,
      (nj_u2)(sizeof(discovery_fields_free) / sizeof(discovery_fields_free[0])) },
    { NJ_C_DISCLISTEN, NULL,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, disclisten_methods,
      (nj_u2)(sizeof(disclisten_methods) / sizeof(disclisten_methods[0])) },
    { NJ_C_UUID, "java/lang/Object", NJ_ACC_PUBLIC,
      uuid_fields,
      (nj_u2)(sizeof(uuid_fields) / sizeof(uuid_fields[0])),
      uuid_methods,
      (nj_u2)(sizeof(uuid_methods) / sizeof(uuid_methods[0])) },
    { NJ_C_REMOTE, "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, remote_methods,
      (nj_u2)(sizeof(remote_methods) / sizeof(remote_methods[0])) },
    { NULL, NULL, 0u, NULL, 0u, NULL, 0u }
};
const int nj_lib_wireless_class_count =
    (int)(sizeof(nj_lib_wireless_classes) / sizeof(nj_lib_wireless_classes[0])) - 1;

const nj_native_entry nj_lib_wireless_table[] = {
    { NJ_C_LOCALDEV, "getLocalDevice", "()L" NJ_C_LOCALDEV ";", bt_no_radio },
    { NJ_C_LOCALDEV, "setDiscoverable", "(I)Z",                   bt_set_discoverable },
    { NJ_C_LOCALDEV, "getRecord",
      "(Ljavax/microedition/io/Connection;)Ljavax/bluetooth/ServiceRecord;",
      bt_get_record },
    { NJ_C_UUID, "<init>", "(Ljava/lang/String;)V", uuid_init },
    { NJ_C_UUID, "toString", "()Ljava/lang/String;", uuid_toString },
    { NJ_C_UUID, "equals",   "(Ljava/lang/Object;)Z", uuid_equals },
    { NJ_C_UUID, "hashCode", "()I",                   uuid_hashCode },
    { NJ_C_REMOTE, "getBluetoothAddress", "()Ljava/lang/String;", remote_addr },
    { NJ_C_REMOTE, "getFriendlyName",     "(Z)Ljava/lang/String;", remote_name },
    { NJ_C_REMOTE, "equals",              "(Ljava/lang/Object;)Z", bt_false },
    { NJ_C_REMOTE, "hashCode",            "()I",                   bt_false },
    { NULL, NULL, NULL, NULL }
};
const int nj_lib_wireless_count =
    (int)(sizeof(nj_lib_wireless_table) / sizeof(nj_lib_wireless_table[0])) - 1;
