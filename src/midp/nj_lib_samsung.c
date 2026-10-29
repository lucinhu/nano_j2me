/*
 * nano_j2me - src/midp/nj_lib_samsung.c
 *
 * Samsung's com.samsung.util.SM/SMS: the vendor's short-message helper, which
 * three collected titles use to send a billing or share message.
 *
 * WHY THIS EXISTS
 * ---------------
 * The classes were absent, so a title that builds an SM and hands it to
 * SMS.send() got a NoClassDefFoundError inside its own SMS feature.  That is
 * the same failure shape the Bluetooth and PIM classes were added for: a
 * device without an SMS stack is a normal device, and a MIDlet that asks it to
 * send a message should get the SDK's own catchable answer, not a linkage
 * error that aborts whatever it was doing.
 *
 * WHAT A DEVICE WITHOUT A RADIO CAN HONESTLY ANSWER
 * -------------------------------------------------
 * The Samsung API declares `SMS.send(SM)` as throwing java.io.IOException, so
 * that is exactly what this implementation raises: the message cannot be sent
 * because there is no SMS transport here.  `SM` is a plain value object and
 * keeps what the title sets (data and destination address); `SMS.isSupported()`
 * (the SDK's own presence check) answers false, so a title that guards with it
 * skips the send entirely and one that does not gets an IOException it can
 * catch.  No message is fabricated and nothing pretends to have been sent.
 *
 * Three titles in the corpus use this exact subset - 三国塔防－曹操传_1610,
 * 宠物王国2紫_5550 and 魂斗罗3代-完全版_3784 - and all three call
 * `new SM()`, `setData(String)`, `setDestAddress(String)`, `SMS.send(SM)`.
 */
#include "nj_midp.h"

#if NJ_USE_LIBC
#  include <string.h>
#endif

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_UNUSED(x) ((void)(x))

#define NJ_C_SM   "com/samsung/util/SM"
#define NJ_C_SMS  "com/samsung/util/SMS"

/* The value object's fields: the two things a title sets before sending. */
#define F_SM_DATA "smData"      /* Ljava/lang/String; */
#define F_SM_DEST "smDest"      /* Ljava/lang/String; */

static nj_field *sfld(nj_vm *vm, const char *name, const char *desc)
{
    return nj_field_lookup(vm, NJ_C_SM, name, desc, 0);
}

static nj_ref sget_ref(nj_vm *vm, nj_ref o, const char *name, const char *desc)
{
    nj_field *f = sfld(vm, name, desc);
    return f ? nj_field_read_ref(vm, o, f) : NULL;
}

static void sset_ref(nj_vm *vm, nj_ref o, const char *name, const char *desc,
                     nj_ref v)
{
    nj_field *f = sfld(vm, name, desc);
    if (f) {
        nj_field_write_ref(vm, o, f, v);
    }
}

/* SM's constructor: the object is created by the VM; nothing to do but leave
 * the two fields null, which the allocation already did. */
NJFN(sm_init)
{
    NJ_UNUSED(args);
    NJ_UNUSED(ret);
    return NJ_OK;
}

NJFN(sm_set_data)
{
    sset_ref(vm, NJ_THIS(args), F_SM_DATA, "Ljava/lang/String;",
             NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(sm_get_data)
{
    ret->r = sget_ref(vm, NJ_THIS(args), F_SM_DATA, "Ljava/lang/String;");
    return NJ_OK;
}

NJFN(sm_set_dest)
{
    sset_ref(vm, NJ_THIS(args), F_SM_DEST, "Ljava/lang/String;",
             NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(sm_get_dest)
{
    ret->r = sget_ref(vm, NJ_THIS(args), F_SM_DEST, "Ljava/lang/String;");
    return NJ_OK;
}

/* SMS.isSupported(): there is no SMS transport in this runtime, and a title
 * that asks first is right to skip the message. */
NJFN(sms_is_supported)
{
    NJ_UNUSED(args);
    ret->i = 0;
    return NJ_OK;
}

/* SMS.send(SM): the SDK declares IOException for a message that cannot go
 * out, which is the truth here rather than a silent success. */
NJFN(sms_send)
{
    NJ_UNUSED(args);
    NJ_UNUSED(ret);
    return nj_throw(vm, "java/io/IOException",
                    "SMS is not available on this device");
}

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }
#define FLD(name, desc, flags) { name, desc, flags }

static const nj_builtin_field sm_fields[] = {
    FLD(F_SM_DATA, "Ljava/lang/String;", 0),
    FLD(F_SM_DEST, "Ljava/lang/String;", 0)
};

static const nj_builtin_method sm_methods[] = {
    M("<init>", "()V", 0),
    M("setData", "(Ljava/lang/String;)V", 0),
    M("getData", "()Ljava/lang/String;", 0),
    M("setDestAddress", "(Ljava/lang/String;)V", 0),
    M("getDestAddress", "()Ljava/lang/String;", 0)
};

static const nj_builtin_method sms_methods[] = {
    M("isSupported", "()Z", NJ_ACC_STATIC),
    M("send", "(L" NJ_C_SM ";)V", NJ_ACC_STATIC)
};

const nj_builtin_class nj_lib_samsung_classes[] = {
    { NJ_C_SM, "java/lang/Object", NJ_ACC_PUBLIC,
      sm_fields,
      (nj_u2)(sizeof(sm_fields) / sizeof(sm_fields[0])),
      sm_methods,
      (nj_u2)(sizeof(sm_methods) / sizeof(sm_methods[0])) },
    { NJ_C_SMS, "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, sms_methods,
      (nj_u2)(sizeof(sms_methods) / sizeof(sms_methods[0])) }
};
const int nj_lib_samsung_class_count =
    (int)(sizeof(nj_lib_samsung_classes) / sizeof(nj_lib_samsung_classes[0]));

const nj_native_entry nj_lib_samsung_table[] = {
    { NJ_C_SM, "<init>", "()V", sm_init },
    { NJ_C_SM, "setData", "(Ljava/lang/String;)V", sm_set_data },
    { NJ_C_SM, "getData", "()Ljava/lang/String;", sm_get_data },
    { NJ_C_SM, "setDestAddress", "(Ljava/lang/String;)V", sm_set_dest },
    { NJ_C_SM, "getDestAddress", "()Ljava/lang/String;", sm_get_dest },
    { NJ_C_SMS, "isSupported", "()Z", sms_is_supported },
    { NJ_C_SMS, "send", "(L" NJ_C_SM ";)V", sms_send }
};
const int nj_lib_samsung_count =
    (int)(sizeof(nj_lib_samsung_table) / sizeof(nj_lib_samsung_table[0]));
