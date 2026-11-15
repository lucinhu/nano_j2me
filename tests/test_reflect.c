/* java.lang.reflect: Class.getMethod()/getDeclaredMethod() + Method.invoke(),
 * and the wrapper TYPE statics (Integer.TYPE, Long.TYPE, ...).
 *
 * Authority: CLDC 1.1 java.lang.Class/Method contracts.  The reflective
 * "call an optional vendor API if it exists" idiom (Class.forName() then
 * getMethod()/invoke()) is what J2ME titles use for Nokia vibration and
 * similar extensions; before this test existed the runtime had no reflection
 * classes at all and those titles died with NoSuchMethodError.
 *
 * Every expectation is a language contract, independent of the C implementation:
 * getMethod() matches name + parameter classes (including primitives), invoke()
 * unboxes Object arguments and boxes primitive returns, and a mismatch is a
 * checked NoSuchMethodException.
 */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures, calls;
static const char *context = "setup";
static nj_vm *vm;

static void check(int yes, const char *what)
{
    ++checks;
    if (!yes) {
        if (failures < 60) {
            fprintf(stderr, "FAIL %s: %s\n", context, what);
        }
        ++failures;
    }
}

static void fixture(int yes, const char *what)
{
    if (!yes) {
        fprintf(stderr, "reflect fixture failure: %s\n", what);
        exit(2);
    }
}

/* Root a value for the duration of one test block. */
static nj_ref keep(nj_ref value)
{
    nj_ref r = nj_root(vm, value);
    fixture(value == NULL || r == value, "root");
    return r;
}

static nj_ref static_ref(const char *cls, const char *name, const char *desc)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 1);
    return f ? nj_field_read_ref(vm, NULL, f) : NULL;
}

static nj_ref call_ref(nj_ref obj, const char *cls, const char *name,
                       const char *desc, nj_value *args)
{
    nj_value ret;
    ret.bits = 0u;
    ++calls;
    if (nj_call(vm, obj, cls, name, desc, args, &ret) != NJ_OK) {
        return NULL;
    }
    return ret.r;
}

static int call_int(nj_ref obj, const char *cls, const char *name,
                    const char *desc, nj_value *args, nj_s4 *out)
{
    nj_value ret;
    ret.bits = 0u;
    ++calls;
    if (nj_call(vm, obj, cls, name, desc, args, &ret) != NJ_OK) {
        return 0;
    }
    *out = ret.i;
    return 1;
}

static const char *class_name(nj_ref cls)
{
    static char buf[128];
    nj_ref s = call_ref(cls, "java/lang/Class", "getName",
                        "()Ljava/lang/String;", NULL);
    if (!s || nj_string_to_utf8(s, buf, (int)sizeof(buf)) <= 0) {
        return NULL;
    }
    return buf;
}

static nj_ref for_name(const char *name)
{
    nj_value arg;
    nj_ref s = keep(nj_string_new(vm, name));
    arg.bits = 0u;
    arg.r = s;
    return call_ref(NULL, "java/lang/Class", "forName",
                    "(Ljava/lang/String;)Ljava/lang/Class;", &arg);
}

static nj_ref class_array(nj_u4 n, const nj_ref *items)
{
    nj_ref a = keep(nj_array_new_of(vm, "[Ljava/lang/Class;", n));
    nj_u4 i;
    for (i = 0u; i < n; i++) {
        nj_array_set_ref(a, i, items[i]);
    }
    return a;
}

static nj_ref object_array(nj_u4 n, const nj_ref *items)
{
    nj_ref a = keep(nj_array_new_of(vm, "[Ljava/lang/Object;", n));
    nj_u4 i;
    for (i = 0u; i < n; i++) {
        nj_array_set_ref(a, i, items[i]);
    }
    return a;
}

static nj_ref get_method(nj_ref cls, const char *name, nj_ref params)
{
    nj_value args[2];
    nj_ref s = keep(nj_string_new(vm, name));
    args[0].bits = 0u;
    args[0].r = s;
    args[1].bits = 0u;
    args[1].r = params;
    return call_ref(cls, "java/lang/Class", "getMethod",
                    "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;",
                    args);
}

/* The exception a failed call left behind, or NULL. */
static const char *pending_exception(void)
{
    nj_class *cls = vm->exception ? vm->exception->cls : NULL;
    for (; cls; cls = cls->super) {
        if (cls->name) {
            return cls->name;
        }
    }
    return NULL;
}

static void test_type_statics(void)
{
    static const struct { const char *wrapper; const char *prim; } types[] = {
        { "java/lang/Integer",   "int" },
        { "java/lang/Long",      "long" },
        { "java/lang/Short",     "short" },
        { "java/lang/Byte",      "byte" },
        { "java/lang/Character", "char" },
        { "java/lang/Boolean",   "boolean" },
        { "java/lang/Float",     "float" },
        { "java/lang/Double",    "double" }
    };
    nj_u4 i;

    context = "TYPE statics";
    for (i = 0u; i < (nj_u4)(sizeof(types) / sizeof(types[0])); i++) {
        nj_u4 mark = nj_root_mark(vm);
        nj_ref type = keep(static_ref(types[i].wrapper, "TYPE",
                                      "Ljava/lang/Class;"));
        nj_ref same;
        const char *name;

        check(type != NULL, "wrapper TYPE static is not null");
        name = type ? class_name(type) : NULL;
        check(name != NULL && strcmp(name, types[i].prim) == 0,
              "TYPE names the primitive type");
        /* int.class compiles to Integer.TYPE; forName("int") must be the same
         * Class object, because class mirrors are unique per type. */
        same = keep(for_name(types[i].prim));
        check(same != NULL && same == type, "forName(primitive) == TYPE");
        nj_root_release(vm, mark);
    }
}

static void test_get_method_and_invoke(void)
{
    nj_u4 mark = nj_root_mark(vm);
    nj_ref dc, params[2], args2[2];
    nj_ref method, name, ptypes, rettype;
    nj_s4 len = 0;
    const char *s;

    context = "getMethod on a vendor class";
    dc = keep(for_name("com.nokia.mid.ui.DeviceControl"));
    check(dc != NULL, "DeviceControl class exists");
    params[0] = keep(static_ref("java/lang/Integer", "TYPE",
                                "Ljava/lang/Class;"));
    params[1] = keep(static_ref("java/lang/Long", "TYPE",
                                "Ljava/lang/Class;"));
    method = keep(get_method(dc, "startVibra", class_array(2u, params)));
    check(method != NULL, "getMethod(startVibra, int, long) resolves");
    if (method) {
        name = keep(call_ref(method, "java/lang/reflect/Method", "getName",
                             "()Ljava/lang/String;", NULL));
        s = NULL;
        if (name) {
            static char buf[64];
            if (nj_string_to_utf8(name, buf, (int)sizeof(buf)) > 0) {
                s = buf;
            }
        }
        check(s != NULL && strcmp(s, "startVibra") == 0,
              "Method.getName is the method name");
        ptypes = keep(call_ref(method, "java/lang/reflect/Method",
                               "getParameterTypes",
                               "()[Ljava/lang/Class;", NULL));
        len = ptypes ? (nj_s4)nj_array_length(ptypes) : -1;
        check(len == 2, "getParameterTypes has two entries");
        if (len == 2) {
            check(class_name(nj_array_get_ref(ptypes, 0u)) != NULL &&
                  strcmp(class_name(nj_array_get_ref(ptypes, 0u)), "int") == 0,
                  "first parameter is int");
            check(class_name(nj_array_get_ref(ptypes, 1u)) != NULL &&
                  strcmp(class_name(nj_array_get_ref(ptypes, 1u)), "long") == 0,
                  "second parameter is long");
        }
        /* A void return type has no Class mirror; null is the honest answer. */
        rettype = call_ref(method, "java/lang/reflect/Method", "getReturnType",
                           "()Ljava/lang/Class;", NULL);
        check(rettype == NULL, "getReturnType of void is null");
    }
    nj_root_release(vm, mark);

    context = "invoke a builtin method";
    mark = nj_root_mark(vm);
    {
        nj_ref str_cls = keep(for_name("java/lang/String"));
        nj_ref m2 = keep(get_method(str_cls, "length", class_array(0u, NULL)));
        nj_ref target = keep(nj_string_new(vm, "hello"));
        nj_ref empty = keep(object_array(0u, NULL));
        nj_value iv[2];
        nj_ref boxed;
        nj_s4 v = -1;

        check(m2 != NULL, "getMethod(String.length) resolves");
        if (m2 && target && empty) {
            iv[0].bits = 0u;
            iv[0].r = target;
            iv[1].bits = 0u;
            iv[1].r = empty;
            boxed = keep(call_ref(m2, "java/lang/reflect/Method", "invoke",
                                  "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;",
                                  iv));
            check(boxed != NULL, "invoke returns a boxed int");
            if (boxed) {
                check(call_int(boxed, "java/lang/Integer", "intValue", "()I",
                               NULL, &v), "boxed int unboxes");
                check(v == 5, "String.length() through reflection is 5");
            }
        }
    }
    nj_root_release(vm, mark);

    context = "invoke a static method with a primitive argument";
    mark = nj_root_mark(vm);
    {
        nj_ref int_cls = keep(for_name("java/lang/Integer"));
        nj_ref type = keep(static_ref("java/lang/Integer", "TYPE",
                                      "Ljava/lang/Class;"));
        nj_ref m3 = keep(get_method(int_cls, "valueOf",
                                    class_array(1u, &type)));
        nj_ref arg_box = keep(call_ref(NULL, "java/lang/Integer", "valueOf",
                                       "(I)Ljava/lang/Integer;",
                                       (nj_value[]){ { .i = 42 } }));
        nj_ref one = keep(object_array(1u, &arg_box));
        nj_value iv[2];
        nj_ref boxed;
        nj_s4 v = -1;

        check(m3 != NULL, "getMethod(Integer.valueOf, int) resolves");
        if (m3 && one) {
            iv[0].bits = 0u;
            iv[0].r = NULL;
            iv[1].bits = 0u;
            iv[1].r = one;
            boxed = keep(call_ref(m3, "java/lang/reflect/Method", "invoke",
                                  "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;",
                                  iv));
            check(boxed != NULL, "static invoke returns a boxed int");
            if (boxed) {
                check(call_int(boxed, "java/lang/Integer", "intValue", "()I",
                               NULL, &v), "boxed int unboxes");
                check(v == 42, "Integer.valueOf(42) through reflection is 42");
            }
        }
    }
    nj_root_release(vm, mark);
}

static void test_no_such_method(void)
{
    nj_u4 mark = nj_root_mark(vm);
    nj_ref dc, m;
    const char *exc;

    context = "NoSuchMethodException";
    dc = keep(for_name("com.nokia.mid.ui.DeviceControl"));
    m = get_method(dc, "noSuchMethod", class_array(0u, NULL));
    check(m == NULL, "unknown method does not resolve");
    exc = pending_exception();
    check(exc != NULL && strcmp(exc, "java/lang/NoSuchMethodException") == 0,
          "unknown method throws NoSuchMethodException");
    nj_clear_exception(vm);
    /* A wrong parameter list is the same checked failure, not a crash. */
    {
        nj_ref type = keep(static_ref("java/lang/Integer", "TYPE",
                                      "Ljava/lang/Class;"));
        m = get_method(dc, "startVibra", class_array(1u, &type));
        check(m == NULL, "wrong parameter count does not resolve");
        exc = pending_exception();
        check(exc != NULL &&
              strcmp(exc, "java/lang/NoSuchMethodException") == 0,
              "wrong parameter list throws NoSuchMethodException");
        nj_clear_exception(vm);
    }
    nj_root_release(vm, mark);
}

int main(void)
{
    nj_port port;
    memset(&port, 0, sizeof(port));
    fixture(nj_port_headless(&port) == NJ_OK, "headless port");
    vm = nj_vm_create(&port, 8u * 1024u * 1024u);
    fixture(vm != NULL, "VM creation");

    test_type_statics();
    test_get_method_and_invoke();
    test_no_such_method();

    context = "cleanup";
    check(!nj_has_exception(vm), "no pending Java exception at the end");
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    printf("Reflect: %u calls, %u checks, %u passed, %u failed\n",
           calls, checks, checks - failures, failures);
    return failures ? 1 : 0;
}
