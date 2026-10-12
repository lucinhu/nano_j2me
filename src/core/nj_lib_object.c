/*
 * nano_j2me - nj_lib_object.c
 *
 * java.lang.Object, java.lang.Class, java.lang.Throwable and the whole
 * exception hierarchy - all implemented as C natives.
 */
#include "nj_internal.h"

#if NJ_USE_LIBC
#  include <string.h>
#endif

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_UNUSED(x) ((void)(x))

/* ------------------------------------------------------------------ */
/* java.lang.Object                                                    */
/* ------------------------------------------------------------------ */
NJFN(obj_init)
{
    NJ_UNUSED(th);
    if (!NJ_THIS(args)) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    return NJ_OK;
}

/*
 * Identity hash.  Objects never move in this runtime, so a value derived from
 * the address is stable for the lifetime of the object and unique within the
 * arena (objects are 16-byte aligned).
 */
nj_s4 nj_identity_hash(nj_vm *vm, nj_ref o)
{
    nj_size p;
    (void)vm;   /* the arena address is the identity; the VM is not needed */
    if (!o) {
        return 0;
    }
    p = (nj_size)(uintptr_t)o;
    return (nj_s4)(((p >> 4) ^ ((p >> 20) & 0xFFFFu)) & 0x7FFFFFFFu);
}

NJFN(obj_hashCode)
{
    ret->i = nj_identity_hash(vm, NJ_THIS(args));
    return NJ_OK;
}

NJFN(obj_equals)
{
    ret->i = (NJ_THIS(args) == NJ_ARG_REF(args, 0)) ? 1 : 0;
    return NJ_OK;
}

NJFN(obj_getClass)
{
    nj_ref o = NJ_THIS(args);
    nj_class *c = o ? o->cls : NULL;

    if (!c) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (!c->class_obj) {
        nj_ref m = nj_new_instance(vm, vm->cls_class);
        if (!m) {
            return nj_throw_oom(vm);
        }
        m->kind = NJ_OBJ_CLASS;
        m->u.mirror = c;
        c->class_obj = m;
    }
    ret->r = c->class_obj;
    return NJ_OK;
}

/* Build "java.lang.String" / "[I" / "[Ljava.lang.String;" for a class. */
static nj_ref class_name_string(nj_vm *vm, nj_class *c)
{
    char buf[300];
    nj_u4 n = 0u;
    const char *p;

    if (!c) {
        return NULL;
    }
    if (c->prim != 0u) {
        return nj_string_new(vm, c->name);
    }
    p = c->name;
    while (*p && n + 1u < (nj_u4)sizeof(buf)) {
        buf[n++] = (*p == '/') ? '.' : *p;
        p++;
    }
    buf[n] = '\0';
    return nj_string_new(vm, buf);
}

nj_ref class_name_string_pub(nj_vm *vm, nj_class *c)
{
    return class_name_string(vm, c);
}

NJFN(obj_toString)
{
    nj_ref o = NJ_THIS(args);
    nj_ref cn;
    nj_ref out;
    char suffix[24];
    nj_u4 n = 0u;
    nj_u4 h;

    if (!o) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    {
        nj_u4 mark = nj_root_mark(vm);
        nj_root(vm, o);
        cn = class_name_string(vm, o->cls);
        if (!cn) {
            nj_root_release(vm, mark);
            return nj_throw_oom(vm);
        }
        nj_root(vm, cn);
        h = (nj_u4)nj_identity_hash(vm, o);
        suffix[n++] = '@';
        {
            char hex[16];
            nj_u4 hn = 0u;
            if (h == 0u) {
                hex[hn++] = '0';
            }
            while (h != 0u) {
                nj_u4 d = h & 0xFu;
                hex[hn++] = (char)(d < 10u ? ('0' + d) : ('a' + (d - 10u)));
                h >>= 4;
            }
            while (hn > 0u && n + 1u < sizeof(suffix)) {
                suffix[n++] = hex[--hn];
            }
        }
        suffix[n] = '\0';
        out = nj_string_concat(vm, cn, nj_string_new(vm, suffix));
        nj_root_release(vm, mark);
    }
    if (!out) {
        return nj_throw_oom(vm);
    }
    ret->r = out;
    return NJ_OK;
}

NJFN(obj_notify)
{
    nj_ref o = NJ_THIS(args);
    if (!o) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    (void)nj_vm_monitor_notify(vm, o, 0);
    return NJ_OK;
}

NJFN(obj_notifyAll)
{
    nj_ref o = NJ_THIS(args);
    if (!o) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    (void)nj_vm_monitor_notify(vm, o, 1);
    return NJ_OK;
}

NJFN(obj_wait)
{
    nj_ref o = NJ_THIS(args);
    nj_s8 ms = 0;

    if (!o) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (nj_string_len(NULL) != 0u) {
        /* unreachable: keeps the compiler from warning about nj_string_len */
    }
    return nj_vm_monitor_wait(vm, th, o, ms);
}

NJFN(obj_wait_timeout)
{
    nj_ref o = NJ_THIS(args);
    nj_s8 ms;

    if (!o) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    ms = NJ_ARG_LONG(args, 0);
    if (ms < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "timeout value is negative");
    }
    return nj_vm_monitor_wait(vm, th, o, ms);
}

/* ------------------------------------------------------------------ */
/* java.lang.Class                                                     */
/* ------------------------------------------------------------------ */
#define NJ_CLASS_OF(args) ((NJ_THIS(args) && NJ_THIS(args)->kind == NJ_OBJ_CLASS) \
                           ? NJ_THIS(args)->u.mirror : NULL)

/*
 * Class.getResourceAsStream().  The leading "/" form is absolute in the JAR and
 * is what games use ("/res/hero.png"); the relative form resolves against the
 * receiver's package, which needs the package name on the Class object, so it is
 * resolved from the class name the object already carries.
 *
 * The resource is copied into a Java byte array and wrapped in a
 * ByteArrayInputStream, because that is what the caller expects to close and
 * skip around in, and because the port's memory for the resource is released
 * here rather than held until the stream is collected.
 */
NJFN(class_getResourceAsStream)
{
    nj_ref cname = NJ_ARG_REF(args, 0);
    nj_app *app = (nj_app *)vm->loader_ud;
    char path[256];
    void *data;
    nj_u4 len = 0u;
    nj_ref ba;
    nj_ref in;
    nj_value a[1];
    nj_u4 mark;

    if (!cname) {
        return nj_throw(vm, "java/lang/NullPointerException", "name is null");
    }
    if (!app) {
        return nj_throw(vm, "java/io/IOException", "no application is loaded");
    }
    if (nj_string_to_utf8(cname, path, (int)sizeof(path)) <= 0) {
        return nj_throw(vm, "java/io/IOException", "bad resource name");
    }
    {
        const char *p = path;
        if (*p == '/') {
            p++;                /* absolute: the JAR root */
        }
        data = nj_app_resource(vm, app, p, &len);
    }
    if (!data || len == 0u) {
        /*
         * Not found is NULL, not an exception - that is what
         * getResourceAsStream() documents, and a game that probes for an
         * optional file depends on it.
         */
        if (data) {
            nj_host_free(vm, data);
        }
        ret->r = NULL;
        return NJ_OK;
    }
    /*
     * Both the byte array and the stream are held only in these C locals, and
     * creating the stream allocates: an unrooted array is collected here and
     * the stream then wraps recycled memory.  寻秦皇陵的诅咒_12450 read every
     * one of its 143 "/name.i" images as empty (Image.createImage(data, 0, 0)
     * -> IllegalArgumentException "no image data") because of exactly that.
     */
    mark = nj_root_mark(vm);
    ba = nj_byte_array_new(vm, len);
    if (!ba) {
        nj_host_free(vm, data);
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    ba = nj_root(vm, ba);
    memcpy(ba->u.bytes, data, (size_t)len);
    nj_host_free(vm, data);

    in = nj_object_new(vm, "java/io/ByteArrayInputStream");
    if (!in) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    in = nj_root(vm, in);
    a[0].r = ba;
    if (nj_call_special(vm, in, "java/io/ByteArrayInputStream", "<init>",
                        "([B)V", a, NULL) != NJ_OK) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    nj_root_release(vm, mark);
    ret->r = in;
    return NJ_OK;
}

NJFN(class_getName)
{
    nj_class *c = NJ_CLASS_OF(args);
    nj_ref s;
    if (!c) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    s = class_name_string(vm, c);
    if (!s) {
        return nj_throw_oom(vm);
    }
    ret->r = s;
    return NJ_OK;
}

NJFN(class_isArray)
{
    nj_class *c = NJ_CLASS_OF(args);
    ret->i = (c && c->is_array) ? 1 : 0;
    return NJ_OK;
}

NJFN(class_isInterface)
{
    nj_class *c = NJ_CLASS_OF(args);
    ret->i = (c && (c->flags & NJ_ACC_INTERFACE)) ? 1 : 0;
    return NJ_OK;
}

NJFN(class_isInstance)
{
    nj_class *c = NJ_CLASS_OF(args);
    nj_ref o = NJ_ARG_REF(args, 0);
    ret->i = (c && o && nj_class_is_assignable(o->cls, c)) ? 1 : 0;
    return NJ_OK;
}

NJFN(class_getSuperclass)
{
    nj_class *c = NJ_CLASS_OF(args);
    if (!c || !c->super) {
        ret->r = NULL;
        return NJ_OK;
    }
    ret->r = nj_class_object(vm, c->super->name);
    return NJ_OK;
}

NJFN(class_forName)
{
    nj_ref s = NJ_ARG_REF(args, 0);
    char buf[300];
    nj_class *c;

    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (nj_string_to_utf8(s, buf, (int)sizeof(buf)) <= 0) {
        return nj_throw(vm, "java/lang/ClassNotFoundException", NULL);
    }
    c = nj_class_lookup(vm, buf);
    if (!c) {
        return nj_throw(vm, "java/lang/ClassNotFoundException", buf);
    }
    ret->r = nj_class_object(vm, c->name);
    return NJ_OK;
}

NJFN(class_toString)
{
    nj_class *c = NJ_CLASS_OF(args);
    nj_ref s;
    if (!c) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    s = class_name_string(vm, c);
    if (!s) {
        return nj_throw_oom(vm);
    }
    ret->r = s;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* java.lang.Throwable                                                 */
/* ------------------------------------------------------------------ */
nj_field *fld_detailMessage_pub(nj_vm *vm)
{
    return nj_field_lookup(vm, "java/lang/Throwable", "detailMessage",
                           "Ljava/lang/String;", 0);
}

#define fld_detailMessage(vm) fld_detailMessage_pub(vm)

NJFN(throwable_init)
{
    nj_ref o = NJ_THIS(args);
    nj_field *f = fld_detailMessage(vm);
    if (f) {
        nj_field_write_ref(vm, o, f, NULL);
    }
    return NJ_OK;
}

NJFN(throwable_init_msg)
{
    nj_ref o = NJ_THIS(args);
    nj_field *f = fld_detailMessage(vm);
    if (f) {
        nj_field_write_ref(vm, o, f, NJ_ARG_REF(args, 0));
    }
    return NJ_OK;
}

NJFN(throwable_getMessage)
{
    nj_field *f = fld_detailMessage(vm);
    ret->r = f ? nj_field_read_ref(vm, NJ_THIS(args), f) : NULL;
    return NJ_OK;
}

NJFN(throwable_toString)
{
    nj_ref o = NJ_THIS(args);
    nj_field *f = fld_detailMessage(vm);
    nj_ref msg = f ? nj_field_read_ref(vm, o, f) : NULL;
    nj_ref cn;
    nj_ref out;

    {
        nj_u4 mark = nj_root_mark(vm);
        nj_root(vm, o);
        nj_root(vm, msg);
        cn = class_name_string(vm, o ? o->cls : NULL);
        if (!cn) {
            nj_root_release(vm, mark);
            return nj_throw_oom(vm);
        }
        if (msg && nj_string_len(msg) > 0u) {
            nj_ref colon = nj_string_new(vm, ": ");
            out = nj_string_concat(vm, cn, colon);
            out = nj_string_concat(vm, out, msg);
        } else {
            out = cn;
        }
        nj_root_release(vm, mark);
    }
    if (!out) {
        return nj_throw_oom(vm);
    }
    ret->r = out;
    return NJ_OK;
}

NJFN(throwable_fillInStackTrace)
{
    ret->r = NJ_THIS(args);
    return NJ_OK;
}

NJFN(throwable_printStackTrace)
{
    nj_ref o = NJ_THIS(args);
    nj_field *f = fld_detailMessage(vm);
    nj_ref msg = f ? nj_field_read_ref(vm, o, f) : NULL;
    char name[192];
    char text[256];

    name[0] = '\0';
    if (o && o->cls && o->cls->name) {
        nj_u4 i = 0u;
        const char *p = o->cls->name;
        while (*p && i + 1u < sizeof(name)) {
            name[i++] = (*p == '/') ? '.' : *p;
            p++;
        }
        name[i] = '\0';
    }
    text[0] = '\0';
    if (msg) {
        nj_string_to_utf8(msg, text, (int)sizeof(text));
    }
    nj_logf(vm, 1, "exception: %s%s%s", name, text[0] ? ": " : "", text);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }

static const nj_builtin_method object_methods[] = {
    M("<init>",     "()V",                  0),
    M("hashCode",   "()I",                  0),
    M("equals",     "(Ljava/lang/Object;)Z", 0),
    M("getClass",   "()Ljava/lang/Class;",  0),
    M("toString",   "()Ljava/lang/String;", 0),
    M("notify",     "()V",                  NJ_ACC_FINAL),
    M("notifyAll",  "()V",                  NJ_ACC_FINAL),
    M("wait",       "()V",                  NJ_ACC_FINAL),
    M("wait",       "(J)V",                 NJ_ACC_FINAL)
};

static const nj_builtin_method class_methods[] = {
    M("getName",        "()Ljava/lang/String;",                 0),
    M("getResourceAsStream",
      "(Ljava/lang/String;)Ljava/io/InputStream;",              0),
    M("isArray",        "()Z",                                  0),
    M("isInterface",    "()Z",                                  0),
    M("isInstance",     "(Ljava/lang/Object;)Z",                0),
    M("getSuperclass",  "()Ljava/lang/Class;",                  0),
    M("forName",        "(Ljava/lang/String;)Ljava/lang/Class;", NJ_ACC_STATIC),
    M("toString",       "()Ljava/lang/String;",                 0)
};

static const nj_builtin_field throwable_fields[] = {
    { "detailMessage", "Ljava/lang/String;", 0 }
};

static const nj_builtin_method throwable_methods[] = {
    M("<init>",              "()V",                                 0),
    M("<init>",              "(Ljava/lang/String;)V",               0),
    M("getMessage",          "()Ljava/lang/String;",                0),
    M("toString",            "()Ljava/lang/String;",                0),
    M("fillInStackTrace",    "()Ljava/lang/Throwable;",             0),
    M("printStackTrace",     "()V",                                 0)
};

/* Every exception type below is a plain subclass with the two constructors. */
#define NJ_EXC_METHODS                                                        \
    M("<init>", "()V", 0),                                                    \
    M("<init>", "(Ljava/lang/String;)V", 0)

#define NJ_EXC_LIST(X)                                                        \
    X("java/lang/Exception",                     "java/lang/Throwable")       \
    X("java/lang/RuntimeException",              "java/lang/Exception")       \
    X("java/lang/Error",                         "java/lang/Throwable")       \
    X("java/lang/ArithmeticException",           "java/lang/RuntimeException")\
    X("java/lang/NullPointerException",          "java/lang/RuntimeException")\
    X("java/lang/ArrayStoreException",           "java/lang/RuntimeException")\
    X("java/lang/ClassCastException",            "java/lang/RuntimeException")\
    X("java/lang/IndexOutOfBoundsException",     "java/lang/RuntimeException")\
    X("java/lang/ArrayIndexOutOfBoundsException","java/lang/IndexOutOfBoundsException")\
    X("java/lang/StringIndexOutOfBoundsException","java/lang/IndexOutOfBoundsException")\
    X("java/lang/NegativeArraySizeException",    "java/lang/RuntimeException")\
    X("java/lang/IllegalArgumentException",      "java/lang/RuntimeException")\
    X("java/lang/NumberFormatException",         "java/lang/IllegalArgumentException")\
    X("java/lang/IllegalStateException",         "java/lang/RuntimeException")\
    X("java/lang/IllegalMonitorStateException",  "java/lang/RuntimeException")\
    X("java/lang/IllegalThreadStateException",   "java/lang/IllegalArgumentException")\
    X("java/lang/InterruptedException",          "java/lang/Exception")       \
    X("java/lang/UnsupportedOperationException", "java/lang/RuntimeException")\
    X("java/lang/SecurityException",             "java/lang/RuntimeException")\
    X("java/lang/ClassNotFoundException",        "java/lang/Exception")       \
    X("java/lang/InstantiationException",        "java/lang/Exception")       \
    X("java/lang/IllegalAccessException",        "java/lang/Exception")       \
    X("java/lang/CloneNotSupportedException",    "java/lang/Exception")       \
    X("java/lang/OutOfMemoryError",              "java/lang/Error")           \
    X("java/lang/StackOverflowError",            "java/lang/Error")           \
    X("java/lang/NoClassDefFoundError",          "java/lang/Error")           \
    X("java/lang/NoSuchMethodError",             "java/lang/Error")           \
    X("java/lang/NoSuchFieldError",              "java/lang/Error")           \
    X("java/lang/UnsatisfiedLinkError",          "java/lang/Error")           \
    X("java/lang/AbstractMethodError",           "java/lang/Error")           \
    X("java/lang/VerifyError",                   "java/lang/Error")           \
    X("java/lang/ClassFormatError",              "java/lang/Error")           \
    X("java/lang/InternalError",                 "java/lang/Error")           \
    X("java/lang/VirtualMachineError",           "java/lang/Error")           \
    X("java/lang/ExceptionInInitializerError",   "java/lang/Error")

/*
 * Every exception type is a plain subclass that declares NO members of its own:
 * it inherits Throwable's fields and its two constructors, which is exactly how
 * java.lang works and means there is one native binding per constructor rather
 * than one per exception class.  (Declaring <init> here without a matching
 * binding would make constructing the exception fail, and constructing *that*
 * failure would recurse forever.)
 */
#define NJ_X_DECL(name, super)                                                \
    { name, super, NJ_ACC_PUBLIC, NULL, 0u, NULL, 0u },

const nj_builtin_class nj_lib_object_classes[] = {
    { "java/lang/Object", NULL, NJ_ACC_PUBLIC, NULL, 0u,
      object_methods,
      (nj_u2)(sizeof(object_methods) / sizeof(object_methods[0])) },
    { "java/lang/Class", "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_FINAL,
      NULL, 0u, class_methods,
      (nj_u2)(sizeof(class_methods) / sizeof(class_methods[0])) },
    { "java/lang/Throwable", "java/lang/Object", NJ_ACC_PUBLIC,
      throwable_fields, (nj_u2)(sizeof(throwable_fields) / sizeof(throwable_fields[0])),
      throwable_methods,
      (nj_u2)(sizeof(throwable_methods) / sizeof(throwable_methods[0])) },
    NJ_EXC_LIST(NJ_X_DECL)
};
const int nj_lib_object_class_count =
    (int)(sizeof(nj_lib_object_classes) / sizeof(nj_lib_object_classes[0]));

const nj_native_entry nj_lib_object_table[] = {
    { "java/lang/Object",    "<init>",            "()V",                   obj_init },
    { "java/lang/Object",    "hashCode",          "()I",                   obj_hashCode },
    { "java/lang/Object",    "equals",            "(Ljava/lang/Object;)Z", obj_equals },
    { "java/lang/Object",    "getClass",          "()Ljava/lang/Class;",   obj_getClass },
    { "java/lang/Object",    "toString",          "()Ljava/lang/String;",  obj_toString },
    { "java/lang/Object",    "notify",            "()V",                   obj_notify },
    { "java/lang/Object",    "notifyAll",         "()V",                   obj_notifyAll },
    { "java/lang/Object",    "wait",              "()V",                   obj_wait },
    { "java/lang/Object",    "wait",              "(J)V",                  obj_wait_timeout },

    { "java/lang/Class",     "getName",           "()Ljava/lang/String;",  class_getName },
    { "java/lang/Class",     "getResourceAsStream",
      "(Ljava/lang/String;)Ljava/io/InputStream;", class_getResourceAsStream },
    { "java/lang/Class",     "isArray",           "()Z",                   class_isArray },
    { "java/lang/Class",     "isInterface",       "()Z",                   class_isInterface },
    { "java/lang/Class",     "isInstance",        "(Ljava/lang/Object;)Z", class_isInstance },
    { "java/lang/Class",     "getSuperclass",     "()Ljava/lang/Class;",   class_getSuperclass },
    { "java/lang/Class",     "forName",           "(Ljava/lang/String;)Ljava/lang/Class;", class_forName },
    { "java/lang/Class",     "toString",          "()Ljava/lang/String;",  class_toString },

    { "java/lang/Throwable", "<init>",            "()V",                          throwable_init },
    { "java/lang/Throwable", "<init>",            "(Ljava/lang/String;)V",        throwable_init_msg },
    { "java/lang/Throwable", "getMessage",        "()Ljava/lang/String;",         throwable_getMessage },
    { "java/lang/Throwable", "toString",          "()Ljava/lang/String;",         throwable_toString },
    { "java/lang/Throwable", "fillInStackTrace",  "()Ljava/lang/Throwable;",      throwable_fillInStackTrace },
    { "java/lang/Throwable", "printStackTrace",   "()V",                          throwable_printStackTrace }
};
const int nj_lib_object_count =
    (int)(sizeof(nj_lib_object_table) / sizeof(nj_lib_object_table[0]));
