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
#define NJ_C_MSGLIS     "javax/wireless/messaging/MessageListener"

/* ---- JSR 82: javax.bluetooth -------------------------------------- */

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

/*
 * JSR 120's callback interface: the one method a MIDlet implements to be told
 * that an SMS arrived.  A MIDlet that merely *implements* it must still link,
 * or the whole class fails to load - and that is exactly how 魂斗罗_5652 died.
 * Its bundled SMS/QQ module has an obfuscated class `g` that implements this
 * interface, so the game stopped with `NoClassDefFoundError: g` before its
 * first frame, on a handset that simply had no radio.  Declaring the interface
 * costs nothing: nothing calls it unless the MIDlet registers a listener with
 * a real MessageConnection, which this runtime never hands out.
 */
static const nj_builtin_method msglis_methods[] = {
    I("notifyIncomingMessage", "(L" NJ_C_MSGCONN ";)V")
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
    { NJ_C_MSGLIS, NULL,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, msglis_methods,
      (nj_u2)(sizeof(msglis_methods) / sizeof(msglis_methods[0])) },

    { NULL, NULL, 0u, NULL, 0u, NULL, 0u }
};
const int nj_lib_wireless_class_count =
    (int)(sizeof(nj_lib_wireless_classes) / sizeof(nj_lib_wireless_classes[0])) - 1;

const nj_native_entry nj_lib_wireless_table[] = {
    { NULL, NULL, NULL, NULL }
};
const int nj_lib_wireless_count =
    (int)(sizeof(nj_lib_wireless_table) / sizeof(nj_lib_wireless_table[0])) - 1;
