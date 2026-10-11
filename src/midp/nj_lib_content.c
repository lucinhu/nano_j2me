/*
 * nano_j2me - nj_lib_content.c
 *
 * javax.microedition.content (JSR 211), the ContentHandler API.
 *
 * ---------------------------------------------------------------------------
 * A DELIBERATE STUB, AND IT SAYS SO
 * ---------------------------------------------------------------------------
 * JSR 211 hands content to ANOTHER APPLICATION on the device - share a score, a
 * "send to a friend", an "open this in the browser".  There is no other
 * application: this runtime is one process with one MIDlet in it, and there is
 * nothing for an Invocation to be dispatched to.
 *
 * So getRegistry() returns a real Registry (a game stores it in a field and
 * would otherwise NPE on the next line), the Invocation accessors round-trip
 * what they are given, and invoke() throws ContentException.
 *
 * That last one is the whole point.  A silent no-op would tell a game its share
 * succeeded when nothing happened, and the user would be left wondering why the
 * message never arrived.  Real MIDlets wrap invoke() in try/catch - the API is
 * optional on a real device too - so refusing is the failure they are already
 * written to survive.
 */
#include "nj_midp.h"

#define NJFN(name) NJ_NATIVE_FN(name)

#define NJ_C_REGISTRY   "javax/microedition/content/Registry"
#define NJ_C_INVOCATION "javax/microedition/content/Invocation"
#define NJ_C_CEXC       "javax/microedition/content/ContentException"

#define F_I_ACTION "ciAction"
#define F_I_ARGS   "ciArgs"
#define F_I_RESP   "ciResp"

static nj_field *ifld(nj_vm *vm, const char *name, const char *desc)
{
    return nj_field_lookup(vm, NJ_C_INVOCATION, name, desc, 0);
}

NJFN(content_getRegistry)
{
    /* The argument names the calling MIDlet, which has nothing to register
     * with; the object exists so the caller's field is not null. */
    nj_ref r = nj_object_new(vm, NJ_C_REGISTRY);
    ret->r = r;
    return r ? NJ_OK : NJ_ERR;
}

NJFN(content_invoke)
{
    return nj_throw(vm, NJ_C_CEXC,
                    "no content handler exists on this device: JSR 211 is not "
                    "implemented");
}

NJFN(invocation_init)
{
    /* (String id, String type, String url).  Kept so the accessors round-trip;
     * an Invocation with nothing in it would make a response read back wrong. */
    nj_field *f = ifld(vm, F_I_ARGS, "[Ljava/lang/String;");
    if (f) {
        nj_field_write_ref(vm, NJ_THIS(args), f, NJ_ARG_REF(args, 0));
    }
    return NJ_OK;
}

NJFN(invocation_setAction)
{
    nj_field *f = ifld(vm, F_I_ACTION, "Ljava/lang/String;");
    if (f) {
        nj_field_write_ref(vm, NJ_THIS(args), f, NJ_ARG_REF(args, 0));
    }
    return NJ_OK;
}

NJFN(invocation_getAction)
{
    nj_field *f = ifld(vm, F_I_ACTION, "Ljava/lang/String;");
    ret->r = f ? nj_field_read_ref(vm, NJ_THIS(args), f) : NULL;
    return NJ_OK;
}

NJFN(invocation_setArgs)
{
    nj_field *f = ifld(vm, F_I_ARGS, "[Ljava/lang/String;");
    if (f) {
        nj_field_write_ref(vm, NJ_THIS(args), f, NJ_ARG_REF(args, 0));
    }
    return NJ_OK;
}

NJFN(invocation_getArgs)
{
    nj_field *f = ifld(vm, F_I_ARGS, "[Ljava/lang/String;");
    ret->r = f ? nj_field_read_ref(vm, NJ_THIS(args), f) : NULL;
    return NJ_OK;
}

NJFN(invocation_setResponse)
{
    nj_field *f = ifld(vm, F_I_RESP, "Z");
    if (f) {
        nj_field_write_int(vm, NJ_THIS(args), f, NJ_ARG_INT(args, 0) ? 1 : 0);
    }
    return NJ_OK;
}

NJFN(invocation_getResponse)
{
    nj_field *f = ifld(vm, F_I_RESP, "Z");
    ret->i = f ? nj_field_read_int(vm, NJ_THIS(args), f) : 0;
    return NJ_OK;
}

#define M(name, desc, flags) { name, desc, flags }
#define FLD(name, desc, flags) { name, desc, flags }

static const nj_builtin_field invocation_fields[] = {
    FLD(F_I_ACTION, "Ljava/lang/String;",   0),
    FLD(F_I_ARGS,   "[Ljava/lang/String;",  0),
    FLD(F_I_RESP,   "Z",                    0)
};

static const nj_builtin_method invocation_methods[] = {
    M("<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V", 0),
    M("setAction",            "(Ljava/lang/String;)V", 0),
    M("getAction",            "()Ljava/lang/String;", 0),
    M("setArgs",              "([Ljava/lang/String;)V", 0),
    M("getArgs",              "()[Ljava/lang/String;", 0),
    M("setResponseRequired",  "(Z)V", 0),
    M("getResponseRequired",  "()Z", 0)
};

static const nj_builtin_method registry_methods[] = {
    M("getRegistry", "(Ljava/lang/String;)L" NJ_C_REGISTRY ";", NJ_ACC_STATIC),
    M("invoke", "(L" NJ_C_INVOCATION ";)V", 0)
};

const nj_builtin_class nj_lib_content_classes[] = {
    { NJ_C_REGISTRY, "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, registry_methods,
      (nj_u2)(sizeof(registry_methods) / sizeof(registry_methods[0])) },
    { NJ_C_INVOCATION, "java/lang/Object", NJ_ACC_PUBLIC,
      invocation_fields,
      (nj_u2)(sizeof(invocation_fields) / sizeof(invocation_fields[0])),
      invocation_methods,
      (nj_u2)(sizeof(invocation_methods) / sizeof(invocation_methods[0])) },
    { NJ_C_CEXC, "java/lang/Exception", NJ_ACC_PUBLIC, NULL, 0u, NULL, 0u },
    { NULL, NULL, 0u, NULL, 0u, NULL, 0u }
};
const int nj_lib_content_class_count =
    (int)(sizeof(nj_lib_content_classes) / sizeof(nj_lib_content_classes[0])) - 1;

const nj_native_entry nj_lib_content_table[] = {
    { NJ_C_REGISTRY, "getRegistry", "(Ljava/lang/String;)L" NJ_C_REGISTRY ";",
      content_getRegistry },
    { NJ_C_REGISTRY, "invoke", "(L" NJ_C_INVOCATION ";)V", content_invoke },
    { NJ_C_INVOCATION, "<init>",
      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
      invocation_init },
    { NJ_C_INVOCATION, "setAction", "(Ljava/lang/String;)V", invocation_setAction },
    { NJ_C_INVOCATION, "getAction", "()Ljava/lang/String;", invocation_getAction },
    { NJ_C_INVOCATION, "setArgs", "([Ljava/lang/String;)V", invocation_setArgs },
    { NJ_C_INVOCATION, "getArgs", "()[Ljava/lang/String;", invocation_getArgs },
    { NJ_C_INVOCATION, "setResponseRequired", "(Z)V", invocation_setResponse },
    { NJ_C_INVOCATION, "getResponseRequired", "()Z", invocation_getResponse },
    { NULL, NULL, NULL, NULL }
};
const int nj_lib_content_count =
    (int)(sizeof(nj_lib_content_table) / sizeof(nj_lib_content_table[0])) - 1;
