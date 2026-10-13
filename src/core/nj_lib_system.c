/*
 * nano_j2me - nj_lib_system.c
 *
 * java.lang.System, java.lang.Runtime, java.io.PrintStream and the
 * java.lang.System.arraycopy workhorse.
 */
#include "nj_internal.h"

#if NJ_USE_LIBC
#  include <string.h>
#  include <stdio.h>
#endif

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_UNUSED(x) ((void)(x))
#define NJ_CALL(name, vm, th, args, ret) name((vm), (th), (args), (ret))

/* ------------------------------------------------------------------ */
/* System                                                              */
/* ------------------------------------------------------------------ */
NJFN(sys_currentTimeMillis)
{
    ret->j = nj_now_ms(vm);
    return NJ_OK;
}

NJFN(sys_nanoTime)
{
    ret->j = nj_now_ms(vm) * 1000000;
    return NJ_OK;
}

/*
 * System.arraycopy is one of the hottest methods in a J2ME game (sprite and
 * tile blitting both go through it), so the copy loop is type-specialised
 * rather than going through per-element helpers.
 */
NJFN(sys_arraycopy)
{
    nj_ref src = NJ_ARG_REF(args, 0);
    nj_s4 src_pos = NJ_ARG_INT(args, 1);
    nj_ref dst = NJ_ARG_REF(args, 2);
    nj_s4 dst_pos = NJ_ARG_INT(args, 3);
    nj_s4 len = NJ_ARG_INT(args, 4);
    nj_u4 esz = 0u;

    if (!src || !dst) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (src->kind != NJ_OBJ_ARRAY || dst->kind != NJ_OBJ_ARRAY) {
        return nj_throw(vm, "java/lang/ArrayStoreException", NULL);
    }
    if (src->aflags != dst->aflags) {
        return nj_throw(vm, "java/lang/ArrayStoreException", NULL);
    }
    if (len < 0 || src_pos < 0 || dst_pos < 0 ||
        (nj_u4)(src_pos + len) > src->length ||
        (nj_u4)(dst_pos + len) > dst->length) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    if (len == 0) {
        return NJ_OK;
    }
    switch (src->aflags) {
    case NJ_ARRAY_T_BOOLEAN:
    case NJ_ARRAY_T_BYTE:   esz = 1u; break;
    case NJ_ARRAY_T_CHAR:
    case NJ_ARRAY_T_SHORT:  esz = 2u; break;
    case NJ_ARRAY_T_INT:
    case NJ_ARRAY_T_FLOAT:  esz = 4u; break;
    case NJ_ARRAY_T_LONG:
    case NJ_ARRAY_T_DOUBLE: esz = 8u; break;
    default:                esz = 0u; break;   /* reference array */
    }

    if (esz != 0u) {
        memmove((nj_u1 *)dst->u.bytes + (nj_size)dst_pos * esz,
                (const nj_u1 *)src->u.bytes + (nj_size)src_pos * esz,
                (nj_size)len * esz);
    } else {
        nj_ref *d = dst->u.refs;
        nj_ref *s = src->u.refs;
        nj_class *comp = dst->cls ? dst->cls->component_cls : NULL;
        nj_s4 i;
        if (d > s) {
            for (i = len - 1; i >= 0; i--) {
                d[dst_pos + i] = s[src_pos + i];
            }
        } else {
            for (i = 0; i < len; i++) {
                d[dst_pos + i] = s[src_pos + i];
            }
        }
        if (comp) {
            for (i = 0; i < len; i++) {
                nj_ref v = d[dst_pos + i];
                if (v && v->cls && !nj_class_is_assignable(v->cls, comp)) {
                    return nj_throw(vm, "java/lang/ArrayStoreException", NULL);
                }
            }
        }
    }
    return NJ_OK;
}

NJFN(sys_identityHashCode)
{
    ret->i = nj_identity_hash(vm, NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(sys_gc)
{
    /*
     * System.gc() is documented as best effort - "there is no guarantee that
     * the virtual machine will actually collect".  When nothing has been
     * allocated since the last collection there is nothing new to reclaim,
     * so the walk is skipped; the next allocation still collects if it needs
     * to, so no memory is stranded.
     */
    if (vm->heap.allocs_since_gc != 0u) {
        (void)nj_gc_run(vm);
    }
    return NJ_OK;
}

NJFN(sys_exit)
{
    nj_s4 code = NJ_ARG_INT(args, 0);
    /* Run finalisation is not modelled; stop the VM cleanly instead. */
    nj_vm_stop_reason(vm, code, "the MIDlet called System.exit()");
    return NJ_OK;
}

/*
 * The system properties the RUNTIME answers, whatever the device is.
 *
 * These are the implementation's own facts - the configuration and profile it
 * implements, the locale it is in, the separator its files use - so they sit
 * below the device table and above the port: a device profile can override them
 * (that is what makes `--device` a real impersonation rather than a decoration),
 * and a port that knows better than the runtime can still answer them.
 */
static const nj_device_prop runtime_props[] = {
    { "microedition.encoding",      "ISO-8859-1" },
    { "microedition.configuration", "CLDC-1.1" },
    { "microedition.profiles",      "MIDP-2.0" },
    { "microedition.locale",        "en-US" },
    { "line.separator",             "\n" },
    { "file.separator",             "/" },
    { "java.version",               "1.4.2" },
    { "os.name",                    "nano_j2me" },
};

/*
 * Answer one system property, or NULL when nobody has an opinion.
 *
 * The order is the whole point of this function:
 *
 *   1. the device table (see nj_device.h) - a profile first, overrides on top,
 *      which is why within the table the LAST match wins;
 *   2. the runtime's own facts, above;
 *   3. the port's device_property hook, which is where a port says
 *      "com.nokia.keyboard.type is None here, because a desktop has no keypad";
 *   4. "nano_j2me" for microedition.platform alone, for a port with no hook.
 *
 * Before this existed the port hook sat in the middle of an if/else chain with
 * no `return` behind it, so every key it did not answer - java.version and
 * os.name among them - came back null on every real port, and the two lines
 * after it were dead code.  Each port's own microedition.platform answer was
 * dead too: the chain matched that key before consulting the hook.
 */
static const char *sys_prop_lookup(nj_vm *vm, const char *k)
{
    const char *v = NULL;
    nj_u4 i;

    for (i = 0; i < vm->nsys_props; i++) {
        if (vm->sys_props[i].value != NULL &&
            strcmp(vm->sys_props[i].key, k) == 0) {
            v = vm->sys_props[i].value;      /* last match wins */
        }
    }
    if (v != NULL) {
        return v;
    }
    for (i = 0; i < (nj_u4)(sizeof(runtime_props) / sizeof(runtime_props[0]));
         i++) {
        if (strcmp(runtime_props[i].key, k) == 0) {
            return runtime_props[i].value;
        }
    }
    if (vm->port != NULL && vm->port->device_property != NULL) {
        v = vm->port->device_property(vm->port->ud, k);
        if (v != NULL) {
            return v;
        }
    }
    if (strcmp(k, "microedition.platform") == 0) {
        return "nano_j2me";
    }
    return NULL;
}

NJFN(sys_getProperty)
{
    nj_ref key = NJ_ARG_REF(args, 0);
    char k[64];
    const char *v;

    if (!key) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (nj_string_to_utf8(key, k, (int)sizeof(k)) <= 0) {
        ret->r = NULL;
        return NJ_OK;
    }
    v = sys_prop_lookup(vm, k);
    ret->r = v != NULL ? nj_string_new(vm, v) : NULL;
    if (vm->exception) {
        return NJ_ERR;
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* java.io.PrintStream                                                 */
/* ------------------------------------------------------------------ */
/*
 * Output is routed through the port's log callback, so on a microcontroller a
 * MIDlet's System.out.println lands on the serial console and on the desktop it
 * lands on stderr.  Nothing is buffered, so a crash cannot swallow output.
 */
static void ps_write_chars(nj_vm *vm, const nj_u2 *s, nj_u4 n)
{
    char buf[256];
    nj_u4 i = 0u;
    nj_u4 out = 0u;

    while (i < n) {
        if (s[i] < 0x80u) {
            buf[out++] = (char)s[i];
        } else {
            buf[out++] = '?';
        }
        i++;
        if (out + 2u >= sizeof(buf) || i == n) {
            buf[out] = '\0';
            if (vm->port && vm->port->log) {
                vm->port->log(vm->port->user, 2, buf);
            } else {
#if NJ_USE_LIBC
                fputs(buf, stderr);
#endif
            }
            out = 0u;
        }
    }
}

NJFN(ps_println)
{
    ps_write_chars(vm, NULL, 0);
    if (vm->port && vm->port->log) {
        vm->port->log(vm->port->user, 2, "");
    }
    return NJ_OK;
}

NJFN(ps_println_str)
{
    nj_ref s = NJ_ARG_REF(args, 0);
    if (s) {
        ps_write_chars(vm, nj_string_chars(s), nj_string_len(s));
    } else {
        static const nj_u2 null_s[] = { 'n', 'u', 'l', 'l' };
        ps_write_chars(vm, null_s, 4u);
    }
    NJ_CALL(ps_println, vm, th, args, ret);
    return NJ_OK;
}

NJFN(ps_print_str)
{
    nj_ref s = NJ_ARG_REF(args, 0);
    if (s) {
        ps_write_chars(vm, nj_string_chars(s), nj_string_len(s));
    }
    return NJ_OK;
}

/* println(Object) and println(boolean) - the two shapes a game reaches for
 * when it dumps a score to System.out during development.  Without them the
 * call is a NoSuchMethodError in the middle of a debug print. */
NJFN(ps_println_obj)
{
    nj_ref o = NJ_ARG_REF(args, 0);

    if (!o) {
        static const nj_u2 null_s[] = { 'n', 'u', 'l', 'l' };
        ps_write_chars(vm, null_s, 4u);
    } else {
        nj_ref s = nj_call_ref(vm, o, "java/lang/Object", "toString",
                               "()Ljava/lang/String;", NULL);
        if (!s) {
            return NJ_ERR;
        }
        ps_write_chars(vm, nj_string_chars(s), nj_string_len(s));
    }
    return NJ_CALL(ps_println, vm, th, args, ret);
}

NJFN(ps_println_bool)
{
    static const nj_u2 true_s[] = { 't', 'r', 'u', 'e' };
    static const nj_u2 false_s[] = { 'f', 'a', 'l', 's', 'e' };

    if (NJ_ARG_INT(args, 0)) {
        ps_write_chars(vm, true_s, 4u);
    } else {
        ps_write_chars(vm, false_s, 5u);
    }
    return NJ_CALL(ps_println, vm, th, args, ret);
}

NJFN(ps_println_int)
{
    nj_ref s = nj_string_new_from_int(vm, NJ_ARG_INT(args, 0));
    nj_value a1[1];
    if (!s) {
        return nj_throw_oom(vm);
    }
    a1[0].r = s;
    return NJ_CALL(ps_println_str, vm, th, a1, ret);
}

NJFN(ps_print_int)
{
    nj_ref s = nj_string_new_from_int(vm, NJ_ARG_INT(args, 0));
    if (!s) {
        return nj_throw_oom(vm);
    }
    ps_write_chars(vm, nj_string_chars(s), nj_string_len(s));
    return NJ_OK;
}

NJFN(ps_print_char)
{
    nj_u2 c = (nj_u2)NJ_ARG_INT(args, 0);
    ps_write_chars(vm, &c, 1u);
    return NJ_OK;
}

NJFN(ps_write_int)
{
    nj_u2 c = (nj_u2)(NJ_ARG_INT(args, 0) & 0xFF);
    ps_write_chars(vm, &c, 1u);
    return NJ_OK;
}

NJFN(ps_flush)
{
    return NJ_OK;
}

NJFN(ps_close)
{
    return NJ_OK;
}

NJFN(runtime_gc)
{
    if (vm->heap.allocs_since_gc != 0u) {
        (void)nj_gc_run(vm);
    }
    return NJ_OK;
}

NJFN(runtime_freeMemory)
{
    ret->j = (nj_s8)nj_heap_free_bytes(vm);
    return NJ_OK;
}

NJFN(runtime_totalMemory)
{
    ret->j = (nj_s8)vm->heap.size;
    return NJ_OK;
}

NJFN(runtime_exit)
{
    nj_vm_stop_reason(vm, NJ_ARG_INT(args, 0), "the MIDlet called Runtime.exit()");
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Late static initialisation                                          */
/* ------------------------------------------------------------------ */
/* Declared locally: see the note below on why this file does not
 * include every subsystem header. */
nj_status nj_rms_late_init(nj_vm *vm);
nj_status nj_m3g_late_init(nj_vm *vm);

nj_status nj_lib_late_init(nj_vm *vm)
{
    nj_field *f_out, *f_err;
    nj_ref out;

    if (!vm) {
        return NJ_ERR;
    }
    /* The MIDP layer installs its own static state (the lcdui constants, the
     * Font/AlertType singletons, the font registry).  One line, as agreed in
     * the module contract; it is independent of System.out below. */
    /*
     * The LCDUI module owns its own late initialisation (the Display
     * singleton and the font registry).  It is a separate translation unit so
     * that a build with LCDUI compiled out still links; the call is therefore
     * declared here rather than included, and a NULL-returning no-op in a
     * build without LCDUI is fine.
     */
    if (nj_lcdui_late_init(vm) != NJ_OK) {
        return NJ_ERR;
    }
    if (nj_media_late_init(vm) != NJ_OK) {
        return NJ_ERR;
    }
    if (nj_nokia_late_init(vm) != NJ_OK) {
        return NJ_ERR;
    }
    if (nj_io_late_init(vm) != NJ_OK) {
        return NJ_ERR;
    }
    if (nj_m3g_late_init(vm) != NJ_OK) {
        return NJ_ERR;
    }
    if (nj_rms_late_init(vm) != NJ_OK) {
        return NJ_ERR;
    }
    f_out = nj_field_lookup(vm, "java/lang/System", "out",
                            "Ljava/io/PrintStream;", 1);
    f_err = nj_field_lookup(vm, "java/lang/System", "err",
                            "Ljava/io/PrintStream;", 1);
    if (!f_out) {
        return NJ_OK;   /* the System class was compiled out */
    }
    out = nj_object_new(vm, "java/io/PrintStream");
    if (!out) {
        nj_clear_exception(vm);
        return NJ_OK;
    }
    nj_field_write_ref(vm, NULL, f_out, out);
    if (f_err) {
        nj_field_write_ref(vm, NULL, f_err, out);
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }

static const nj_builtin_field system_fields[] = {
    { "out", "Ljava/io/PrintStream;", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "err", "Ljava/io/PrintStream;", NJ_ACC_STATIC | NJ_ACC_FINAL }
};

static const nj_builtin_method system_methods[] = {
    M("currentTimeMillis", "()J",   NJ_ACC_STATIC),
    M("nanoTime",          "()J",   NJ_ACC_STATIC),
    M("arraycopy",         "(Ljava/lang/Object;ILjava/lang/Object;II)V", NJ_ACC_STATIC),
    M("identityHashCode",  "(Ljava/lang/Object;)I", NJ_ACC_STATIC),
    M("gc",                "()V",   NJ_ACC_STATIC),
    M("exit",              "(I)V",  NJ_ACC_STATIC),
    M("getProperty",       "(Ljava/lang/String;)Ljava/lang/String;", NJ_ACC_STATIC)
};

static const nj_builtin_method printstream_methods[] = {
    M("println",  "()V",                    0),
    M("println",  "(Ljava/lang/String;)V",  0),
    M("println",  "(Ljava/lang/Object;)V",  0),
    M("println",  "(I)V",                   0),
    M("println",  "(Z)V",                   0),
    M("print",    "(Ljava/lang/String;)V",  0),
    M("print",    "(C)V",                   0),
    M("print",    "(I)V",                   0),
    M("write",    "(I)V",                   0),
    M("flush",    "()V",                    0),
    M("close",    "()V",                    0)
};

static const nj_builtin_method runtime_methods[] = {
    M("gc",           "()V", 0),
    M("freeMemory",   "()J", 0),
    M("totalMemory",  "()J", 0),
    M("exit",         "(I)V", 0),
    M("getRuntime",   "()Ljava/lang/Runtime;", NJ_ACC_STATIC)
};

const nj_builtin_class nj_lib_system_classes[] = {
    { "java/lang/System", "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_FINAL,
      system_fields,
      (nj_u2)(sizeof(system_fields) / sizeof(system_fields[0])),
      system_methods,
      (nj_u2)(sizeof(system_methods) / sizeof(system_methods[0])) },
    { "java/io/PrintStream", "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, printstream_methods,
      (nj_u2)(sizeof(printstream_methods) / sizeof(printstream_methods[0])) },
    { "java/lang/Runtime", "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, runtime_methods,
      (nj_u2)(sizeof(runtime_methods) / sizeof(runtime_methods[0])) }
};
const int nj_lib_system_class_count =
    (int)(sizeof(nj_lib_system_classes) / sizeof(nj_lib_system_classes[0]));

NJFN(runtime_getRuntime)
{
    ret->r = nj_object_new(vm, "java/lang/Runtime");
    return ret->r ? NJ_OK : NJ_ERR;
}

const nj_native_entry nj_lib_system_table[] = {
    { "java/lang/System", "currentTimeMillis", "()J",   sys_currentTimeMillis },
    { "java/lang/System", "nanoTime",          "()J",   sys_nanoTime },
    { "java/lang/System", "arraycopy",         "(Ljava/lang/Object;ILjava/lang/Object;II)V", sys_arraycopy },
    { "java/lang/System", "identityHashCode",  "(Ljava/lang/Object;)I", sys_identityHashCode },
    { "java/lang/System", "gc",                "()V",   sys_gc },
    { "java/lang/System", "exit",              "(I)V",  sys_exit },
    { "java/lang/System", "getProperty",       "(Ljava/lang/String;)Ljava/lang/String;", sys_getProperty },

    { "java/io/PrintStream", "println", "()V",                    ps_println },
    { "java/io/PrintStream", "println", "(Ljava/lang/String;)V",  ps_println_str },
    { "java/io/PrintStream", "println", "(Ljava/lang/Object;)V",  ps_println_obj },
    { "java/io/PrintStream", "println", "(Z)V",                   ps_println_bool },
    { "java/io/PrintStream", "println", "(I)V",                   ps_println_int },
    { "java/io/PrintStream", "print",   "(Ljava/lang/String;)V",  ps_print_str },
    { "java/io/PrintStream", "print",   "(C)V",                   ps_print_char },
    { "java/io/PrintStream", "print",   "(I)V",                   ps_print_int },
    { "java/io/PrintStream", "write",   "(I)V",                   ps_write_int },
    { "java/io/PrintStream", "flush",   "()V",                    ps_flush },
    { "java/io/PrintStream", "close",   "()V",                    ps_close },

    { "java/lang/Runtime", "gc",          "()V",  runtime_gc },
    { "java/lang/Runtime", "freeMemory",  "()J",  runtime_freeMemory },
    { "java/lang/Runtime", "totalMemory", "()J",  runtime_totalMemory },
    { "java/lang/Runtime", "exit",        "(I)V", runtime_exit },
    { "java/lang/Runtime", "getRuntime",  "()Ljava/lang/Runtime;", runtime_getRuntime }
};
const int nj_lib_system_count =
    (int)(sizeof(nj_lib_system_table) / sizeof(nj_lib_system_table[0]));
