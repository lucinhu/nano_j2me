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

/*
 * The name of an object that has no class.
 *
 * An array the RUNTIME created - String.getBytes() hands one out - was made
 * with a NULL class, so there is no class to take the name from.  The name
 * still has to be the array's type: a phone prints "[B@1a2b3c" for it, and a
 * title that prints an array in a log line should see the same thing.
 */
static nj_ref object_type_name(nj_vm *vm, const nj_object *o)
{
    static const char prims[] = "ZBCSIJFD";   /* const: .rodata, no RAM */
    char buf[4];
    nj_u4 k;

    if (!o || o->kind != NJ_OBJ_ARRAY) {
        return NULL;
    }
    k = o->aflags;
    buf[0] = '[';
    buf[1] = (k >= 1u && k <= 8u) ? prims[k - 1u] : '?';
    buf[2] = '\0';
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
        cn = o->cls ? class_name_string(vm, o->cls) : object_type_name(vm, o);
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
    /*
     * notify() has always required that the caller owns the monitor; without
     * that check a thread could wake a waiter it has no relationship with,
     * which is not a state Java programs can rely on.
     */
    if (!nj_vm_monitor_owned(vm, th, o)) {
        return nj_throw(vm, "java/lang/IllegalMonitorStateException", NULL);
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
    if (!nj_vm_monitor_owned(vm, th, o)) {
        return nj_throw(vm, "java/lang/IllegalMonitorStateException", NULL);
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

/* wait(long, int): the nanosecond part is a refinement this runtime has no
 * clock for, so the timeout is the millisecond part.  The range check is the
 * one the javadoc states: the nanoseconds must be 0..999999. */
NJFN(obj_wait_timeout_ns)
{
    nj_ref o = NJ_THIS(args);
    nj_s8 ms;
    nj_s4 ns;

    if (!o) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    ms = NJ_ARG_LONG(args, 0);
    ns = NJ_ARG_INT(args, 1);
    if (ms < 0 || ns < 0 || ns > 999999) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "timeout value is negative or nanoseconds out of range");
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
    nj_value a[2];
    nj_u4 mark;

    if (!cname) {
        return nj_throw(vm, "java/lang/NullPointerException", "name is null");
    }
    if (!app) {
        return nj_throw(vm, "java/io/IOException", "no application is loaded");
    }
    {
        int n = nj_string_to_utf8(cname, path, (int)sizeof(path));

        if (n < 0) {
            return nj_throw(vm, "java/io/IOException", "bad resource name");
        }
        /*
         * An empty name asks for "the package itself", which is not an entry in
         * a JAR: the specified result is null, not an exception.  4x4 Extreme
         * Rally and Orlen Team - Dakar 2007 probe it during startApp() and turn
         * the IOException into a divide by zero.
         */
        if (n == 0) {
            ret->r = NULL;
            return NJ_OK;
        }
    }
    {
        const char *p = path;
        char rel[256];
        if (*p == '/') {
            p++;                /* absolute: the JAR root */
        } else {
            /*
             * A RELATIVE name resolves against the class's own package
             * directory: class Z/a asking for "D0" means the JAR entry
             * "Z/D0", not "D0".
             *
             * 蛋蛋历险记_11460 loads every level file this way.  Without
             * this, getResourceAsStream() answers NULL, the game's loader
             * NPEs on the null stream inside its own retry loop and hangs
             * forever: it retries the whole level after a System.gc() and
             * the retry can never succeed.
             */
            nj_class *owner = NJ_CLASS_OF(args);
            const char *slash = (owner != NULL && owner->name != NULL)
                                    ? strrchr(owner->name, '/') : NULL;
            if (slash != NULL) {
                nj_size plen = (nj_size)(slash - owner->name);
                nj_size nlen = (nj_size)strlen(p);
                if (plen + 1u + nlen < (nj_size)sizeof(rel)) {
                    memcpy(rel, owner->name, (size_t)plen);
                    rel[plen] = '/';
                    memcpy(rel + plen + 1u, p, (size_t)nlen + 1u);
                    p = rel;
                }
            }
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

/*
 * Class.newInstance(): the no-argument constructor, and nothing else.  Two
 * corpus titles (3D坦克对战1944, CS反恐精英) build their screens this way, and
 * without it the call is a NoSuchMethodError that reads like a missing feature
 * rather than a missing method.
 *
 * The exception on a missing <init>()V is InstantiationException, not
 * NoSuchMethodError: that is what the CLDC specification says a reflective
 * instantiation throws when the class cannot be instantiated this way, and a
 * MIDlet that catches it is entitled to get it.
 */
NJFN(class_newInstance)
{
    nj_class *c = NJ_CLASS_OF(args);
    nj_method *m;
    nj_u4 mark;
    nj_ref o;

    if (!c) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (nj_class_init(vm, c) != NJ_OK) {
        return NJ_ERR;
    }
    m = nj_method_lookup(c, "<init>", "()V");
    if (!m) {
        return nj_throw(vm, "java/lang/InstantiationException", c->name);
    }
    /*
     * ... and IllegalAccessException, not InstantiationException, when the
     * class or its nullary constructor is not accessible.  That is what the
     * specification says, and the difference is visible to a MIDlet that
     * catches one of the two: 1945空战英雄 builds a screen out of a
     * package-private class, and until this check existed it reported a bare
     * `IllegalAccessException` with no message -- because the game's own code
     * threw it after our newInstance() had wrongly succeeded.  Naming the
     * class is what makes the report actionable.
     */
    /*
     * Only the class itself has to be public.  Handsets do not check the
     * constructor's access flags here: Nokia's Snake (snake_2.jar) is
     * `public class ToneDevice` with a package-private `ToneDevice()` and
     * Class.newInstance() is how the MIDlet reaches it.  Refusing that made
     * startApp() NPE in `f.initialise()`.  A package-private *class* still
     * gets IllegalAccessException (tests/excdemo/ExcDemo.java covers it).
     */
    if (!(c->flags & NJ_ACC_PUBLIC)) {
        return nj_throw(vm, "java/lang/IllegalAccessException", c->name);
    }
    mark = nj_root_mark(vm);
    o = nj_object_new(vm, c->name);
    if (!o) {
        nj_root_release(vm, mark);
        return nj_throw_oom(vm);
    }
    o = nj_root(vm, o);
    if (nj_call_special(vm, o, c->name, "<init>", "()V", NULL, NULL) != NJ_OK) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    nj_root_release(vm, mark);
    ret->r = o;
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

/* Class.isAssignableFrom(Class cls): true when a cls may be used where the
 * receiver is expected.  The hierarchy walk is the same one isInstance and
 * checkcast use (nj_class_is_assignable); the argument, not the receiver, is
 * the "from" side. */
NJFN(class_isAssignableFrom)
{
    nj_class *to = NJ_CLASS_OF(args);
    nj_ref a = NJ_ARG_REF(args, 0);
    nj_class *from;

    if (!a) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    from = (a->kind == NJ_OBJ_CLASS) ? a->u.mirror : NULL;
    ret->i = (to && from && nj_class_is_assignable(from, to)) ? 1 : 0;
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
    /* JVMS 5.5 / CLDC: Class.forName initialises the class.  Some titles rely
     * on that: GoldenSolitaire's resource manager calls Class.forName("p") to
     * make the interface's static fields allocate their handles before the
     * archive tables are sized.  Without the initialisation the tables were
     * sized without those slots and the first indexed store in the game's
     * resource manager threw ArrayIndexOutOfBoundsException on every paint. */
    if (nj_class_init(vm, c) != NJ_OK) {
        return NJ_ERR;
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

/*
 * printStackTrace() prints the frames of the running thread, because the VM
 * has them: nj_vm_describe_threads() walks them already.
 *
 * Limitation, stated here because it is visible in the output: this is the
 * call chain AT THE MOMENT OF THE CALL, not a snapshot taken where the
 * exception was constructed.  A faithful stack trace is captured by
 * Throwable.<init>, and exceptions here do not carry one yet.  What this does
 * give is the method and pc of whoever is handling the exception - which is
 * what turned 疯狂企鹅's useless "NullPointerException" with no location into
 * "DCTuxedo.b(I)V pc=2017" and showed the throw was inside the game's own
 * catch block.
 */
/* Where a stack trace goes: the log for printStackTrace(), a caller-supplied
 * PrintStream for printStackTrace(PrintStream). */
typedef struct {
    nj_vm *vm;
    nj_ref stream;      /* NULL = the log */
} trace_out;

/* nj_vm_describe_threads() prefixes every line for the --where flag and indents
 * the frames behind it.  A stack trace has its own indentation, and the first
 * line merely names the thread: both sinks below want the frame text alone. */
static const char *trace_frame(const char *line)
{
    const char *p = line;

    if (p[0]=='w'&&p[1]=='h'&&p[2]=='e'&&p[3]=='r'&&p[4]=='e'&&p[5]==':'&&p[6]==' ') {
        p += 7;
    }
    while (*p == ' ') {
        p++;
    }
    if (p[0]=='t'&&p[1]=='h'&&p[2]=='r'&&p[3]=='e'&&p[4]=='a'&&p[5]=='d'&&p[6]==' ') {
        return NULL;
    }
    return p;
}

/* Append `src` to dst[at..cap) and return the new length. */
static nj_u4 trace_app(char *dst, nj_u4 at, nj_u4 cap, const char *src)
{
    while (*src && at + 1u < cap) {
        dst[at++] = *src++;
    }
    return at;
}

static void trace_line(trace_out *out, const char *text)
{
    if (!out->stream) {
        nj_logf(out->vm, 1, "%s", text);
        return;
    }
    {
        nj_value a[1];
        nj_ref s = nj_string_new(out->vm, text);
        if (!s) {
            /* Out of memory while reporting: say nothing rather than replace
             * the caller's exception with ours. */
            return;
        }
        a[0].r = s;
        (void)nj_call(out->vm, out->stream, "java/io/PrintStream", "println",
                      "(Ljava/lang/String;)V", a, NULL);
    }
}

static void trace_sink(void *ud, const char *line)
{
    trace_out *out = (trace_out *)ud;
    const char *p = trace_frame(line);

    if (p) {
        nj_logf(out->vm, 1, "    at %s", p);
    }
}

static void trace_stream_sink(void *ud, const char *line)
{
    trace_out *out = (trace_out *)ud;
    const char *p = trace_frame(line);
    char buf[256];
    nj_u4 i;

    if (!p) {
        return;
    }
    i = trace_app(buf, 0u, sizeof(buf), "    at ");
    (void)trace_app(buf, i, sizeof(buf), p);
    trace_line(out, buf);
}

/* The shared body of printStackTrace() and printStackTrace(PrintStream): the
 * header line then the live call chain, to the log when `stream` is NULL and to
 * that stream otherwise. */
static nj_status throwable_trace(nj_vm *vm, nj_ref o, nj_ref stream)
{
    nj_field *f = fld_detailMessage(vm);
    nj_ref msg = f ? nj_field_read_ref(vm, o, f) : NULL;
    char name[192];
    char text[256];
    char head[512];
    trace_out out;
    nj_u4 i;

    name[0] = '\0';
    if (o && o->cls && o->cls->name) {
        nj_u4 n = 0u;
        const char *p = o->cls->name;
        while (*p && n + 1u < sizeof(name)) {
            name[n++] = (*p == '/') ? '.' : *p;
            p++;
        }
        name[n] = '\0';
    }
    text[0] = '\0';
    if (msg) {
        nj_string_to_utf8(msg, text, (int)sizeof(text));
    }
    i = trace_app(head, 0u, sizeof(head), "exception: ");
    i = trace_app(head, i, sizeof(head), name);
    if (text[0]) {
        i = trace_app(head, i, sizeof(head), ": ");
        i = trace_app(head, i, sizeof(head), text);
    }
    head[i] = '\0';

    out.vm = vm;
    out.stream = stream;
    trace_line(&out, head);
    /* The log sink formats each frame straight from the walk (so a long frame
     * line is never truncated); the stream sink has to build the String first. */
    if (stream) {
        nj_vm_describe_threads(vm, trace_stream_sink, &out);
    } else {
        nj_vm_describe_threads(vm, trace_sink, &out);
    }
    return NJ_OK;
}

NJFN(throwable_printStackTrace)
{
    return throwable_trace(vm, NJ_THIS(args), NULL);
}

/* Throwable.printStackTrace(PrintStream): the same trace, written to the given
 * stream instead of the log. */
NJFN(throwable_printStackTrace_stream)
{
    nj_ref stream = NJ_ARG_REF(args, 0);

    if (!stream) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    return throwable_trace(vm, NJ_THIS(args), stream);
}

/* ------------------------------------------------------------------ */
/* java.lang.ref.Reference / WeakReference (CLDC 1.1)                   */
/* ------------------------------------------------------------------ */
static nj_field *weak_referent(nj_vm *vm)
{
    return nj_field_lookup(vm, "java/lang/ref/Reference", "referent",
                           "Ljava/lang/Object;", 0);
}

NJFN(weak_init)
{
    nj_field_write_ref(vm, NJ_THIS(args), weak_referent(vm), NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(weak_get)
{
    ret->r = nj_field_read_ref(vm, NJ_THIS(args), weak_referent(vm));
    return NJ_OK;
}

NJFN(weak_clear)
{
    nj_field_write_ref(vm, NJ_THIS(args), weak_referent(vm), NULL);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* java.lang.reflect.Method                                            */
/* ------------------------------------------------------------------ */
/*
 * CLDC had no reflection API, but J2ME titles use it anyway: Class.forName()
 * followed by getMethod()/invoke() is the standard way to call an optional
 * vendor API (Nokia DeviceControl vibration, for one) without a compile-time
 * dependency.  Without it those titles die with NoSuchMethodError.  This is
 * the smallest implementation that makes the idiom work: find a method by
 * name and parameter classes, then invoke it.
 */
static nj_size reflect_text_len(const char *s)
{
    nj_size n = 0u;
    if (s) {
        while (s[n]) {
            n++;
        }
    }
    return n;
}

static int reflect_text_eq(const char *a, const char *b)
{
    nj_size i = 0u;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

/* Descriptor text of one Class mirror: "I" for a primitive, the descriptor
 * itself for an array ("[I"), "Lname;" for a class. */
static int reflect_class_desc(nj_class *c, char *out, nj_size cap)
{
    nj_size n;
    char d = 0;

    if (!c || !out || cap < 2u) {
        return 0;
    }
    if (c->prim) {
        switch (c->prim) {
        case NJ_PT_BOOLEAN: d = 'Z'; break;
        case NJ_PT_BYTE:    d = 'B'; break;
        case NJ_PT_CHAR:    d = 'C'; break;
        case NJ_PT_SHORT:   d = 'S'; break;
        case NJ_PT_INT:     d = 'I'; break;
        case NJ_PT_LONG:    d = 'J'; break;
        case NJ_PT_FLOAT:   d = 'F'; break;
        case NJ_PT_DOUBLE:  d = 'D'; break;
        default:            return 0;
        }
        out[0] = d;
        out[1] = '\0';
        return 1;
    }
    n = reflect_text_len(c->name);
    if (c->is_array) {
        if (n + 1u > cap) {
            return 0;
        }
        memcpy(out, c->name, n + 1u);
        return 1;
    }
    if (n + 3u > cap) {
        return 0;
    }
    out[0] = 'L';
    memcpy(out + 1, c->name, n);
    out[n + 1] = ';';
    out[n + 2] = '\0';
    return 1;
}

/* Length of one field descriptor at p ("I", "[I", "Ljava/lang/String;"). */
static nj_size reflect_desc_len(const char *p)
{
    nj_size n = 1u;

    if (!p || !*p) {
        return 0u;
    }
    if (*p == '[') {
        return 1u + reflect_desc_len(p + 1);
    }
    if (*p == 'L') {
        while (p[n] && p[n] != ';') {
            n++;
        }
        if (p[n] == ';') {
            n++;
        }
        return n;
    }
    return 1u;
}

/* Does a method descriptor take exactly the parameter classes in `params`? */
static int reflect_args_match(const char *desc, nj_ref params)
{
    const char *p;
    nj_u4 i, n;
    char want[300];

    if (!desc || desc[0] != '(') {
        return 0;
    }
    p = desc + 1;
    n = params ? nj_array_length(params) : 0u;
    for (i = 0u; i < n; i++) {
        nj_ref a = nj_array_get_ref(params, i);
        nj_class *ac = (a && a->kind == NJ_OBJ_CLASS) ? a->u.mirror : NULL;
        nj_size span;

        if (!ac || !reflect_class_desc(ac, want, sizeof(want))) {
            return 0;
        }
        span = reflect_desc_len(p);
        if (span == 0u || span != reflect_text_len(want) ||
            memcmp(p, want, span) != 0) {
            return 0;
        }
        p += span;
    }
    return *p == ')';
}

static nj_ref reflect_method_new(nj_vm *vm, nj_class *decl,
                                 const char *name, const char *desc)
{
    nj_class *mc = nj_class_lookup(vm, "java/lang/reflect/Method");
    nj_ref o;
    nj_field *f;
    nj_ref s;

    if (!mc) {
        return NULL;
    }
    o = nj_new_instance(vm, mc);
    if (!o) {
        return NULL;
    }
    f = nj_field_lookup(vm, "java/lang/reflect/Method", "clazz",
                        "Ljava/lang/Class;", 0);
    if (f) {
        nj_field_write_ref(vm, o, f, nj_class_object(vm, decl->name));
    }
    f = nj_field_lookup(vm, "java/lang/reflect/Method", "name",
                        "Ljava/lang/String;", 0);
    if (f) {
        s = nj_string_new(vm, name);
        nj_field_write_ref(vm, o, f, s);
    }
    f = nj_field_lookup(vm, "java/lang/reflect/Method", "desc",
                        "Ljava/lang/String;", 0);
    if (f) {
        s = nj_string_new(vm, desc);
        nj_field_write_ref(vm, o, f, s);
    }
    return o;
}

static nj_ref reflect_method_field(nj_vm *vm, nj_ref m,
                                   const char *name, const char *desc)
{
    nj_field *f = nj_field_lookup(vm, "java/lang/reflect/Method", name, desc, 0);
    return f ? nj_field_read_ref(vm, m, f) : NULL;
}

static nj_status reflect_lookup(nj_vm *vm, nj_class *c, int inherited,
                                nj_ref name_ref, nj_ref params,
                                nj_value *ret)
{
    char name[160];
    nj_class *k;

    if (!c || !name_ref) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (nj_string_to_utf8(name_ref, name, (int)sizeof(name)) <= 0) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    for (k = c; k != NULL; k = (inherited ? k->super : NULL)) {
        nj_u2 i;
        for (i = 0u; i < k->method_count; i++) {
            nj_method *m = &k->methods[i];
            /* Builtin method tables only set NJ_ACC_PUBLIC on a few
             * entries; everything registered here is reachable from Java. */
            if (!reflect_text_eq(m->name, name) ||
                !reflect_args_match(m->desc, params)) {
                continue;
            }
            ret->r = reflect_method_new(vm, k, m->name, m->desc);
            if (!ret->r) {
                return nj_throw_oom(vm);
            }
            return NJ_OK;
        }
    }
    return nj_throw(vm, "java/lang/NoSuchMethodException", name);
}

NJFN(class_getMethod)
{
    return reflect_lookup(vm, NJ_CLASS_OF(args), 1,
                          NJ_ARG_REF(args, 0), NJ_ARG_REF(args, 1), ret);
}

NJFN(class_getDeclaredMethod)
{
    return reflect_lookup(vm, NJ_CLASS_OF(args), 0,
                          NJ_ARG_REF(args, 0), NJ_ARG_REF(args, 1), ret);
}

NJFN(method_getName)
{
    ret->r = reflect_method_field(vm, NJ_THIS(args), "name",
                                  "Ljava/lang/String;");
    return NJ_OK;
}

NJFN(method_getDeclaringClass)
{
    ret->r = reflect_method_field(vm, NJ_THIS(args), "clazz",
                                  "Ljava/lang/Class;");
    return NJ_OK;
}

NJFN(method_getParameterTypes)
{
    nj_ref m = NJ_THIS(args);
    nj_ref desc_ref = reflect_method_field(vm, m, "desc",
                                           "Ljava/lang/String;");
    char desc[400];
    nj_ref arr;
    nj_u4 n = 0u;
    const char *p;

    if (!desc_ref || nj_string_to_utf8(desc_ref, desc, (int)sizeof(desc)) <= 0) {
        ret->r = NULL;
        return NJ_OK;
    }
    for (p = desc + 1; *p && *p != ')'; ) {
        nj_size span = reflect_desc_len(p);
        if (span == 0u) {
            break;
        }
        n++;
        p += span;
    }
    arr = nj_array_new_of(vm, "[Ljava/lang/Class;", n);
    if (!arr) {
        return nj_throw_oom(vm);
    }
    p = desc + 1;
    {
        nj_u4 i;
        for (i = 0u; i < n; i++) {
            char one[300];
            nj_size span = reflect_desc_len(p);
            nj_size j;
            if (span == 0u || span >= sizeof(one)) {
                break;
            }
            for (j = 0u; j < span; j++) {
                one[j] = p[j];
            }
            one[span] = '\0';
            nj_array_set_ref(arr, i, nj_class_object_for_desc(vm, one));
            p += span;
        }
    }
    ret->r = arr;
    return NJ_OK;
}

NJFN(method_getReturnType)
{
    nj_ref m = NJ_THIS(args);
    nj_ref desc_ref = reflect_method_field(vm, m, "desc",
                                           "Ljava/lang/String;");
    char desc[400];
    const char *p;

    if (!desc_ref || nj_string_to_utf8(desc_ref, desc, (int)sizeof(desc)) <= 0) {
        ret->r = NULL;
        return NJ_OK;
    }
    p = desc + 1;
    while (*p && *p != ')') {
        nj_size span = reflect_desc_len(p);
        if (span == 0u) {
            break;
        }
        p += span;
    }
    ret->r = (*p == ')') ? nj_class_object_for_desc(vm, p + 1) : NULL;
    return NJ_OK;
}

/* Box one primitive value through the wrapper's static valueOf. */
static nj_ref reflect_box(nj_vm *vm, nj_u1 ptype, nj_value *v)
{
    const char *cls;
    const char *desc;
    nj_value out;

    switch (ptype) {
    case NJ_PT_BOOLEAN:
        cls = "java/lang/Boolean"; desc = "(Z)Ljava/lang/Boolean;"; break;
    case NJ_PT_BYTE:
        cls = "java/lang/Byte"; desc = "(B)Ljava/lang/Byte;"; break;
    case NJ_PT_CHAR:
        cls = "java/lang/Character"; desc = "(C)Ljava/lang/Character;"; break;
    case NJ_PT_SHORT:
        cls = "java/lang/Short"; desc = "(S)Ljava/lang/Short;"; break;
    case NJ_PT_INT:
        cls = "java/lang/Integer"; desc = "(I)Ljava/lang/Integer;"; break;
    case NJ_PT_LONG:
        cls = "java/lang/Long"; desc = "(J)Ljava/lang/Long;"; break;
    case NJ_PT_FLOAT:
        cls = "java/lang/Float"; desc = "(F)Ljava/lang/Float;"; break;
    case NJ_PT_DOUBLE:
        cls = "java/lang/Double"; desc = "(D)Ljava/lang/Double;"; break;
    default:
        return NULL;
    }
    out.bits = 0u;
    if (nj_call_static(vm, cls, "valueOf", desc, v, &out) != NJ_OK) {
        return NULL;
    }
    return out.r;
}

/* Read a wrapper's value field as the requested primitive.  A null argument
 * is 0/false, as the Java reflection API specifies. */
static int reflect_unbox(nj_vm *vm, nj_u1 ptype, nj_ref box, nj_value *out)
{
    nj_field *f;
    const char *desc;

    out->bits = 0u;
    if (!box) {
        return 1;
    }
    switch (ptype) {
    case NJ_PT_BOOLEAN: desc = "Z"; break;
    case NJ_PT_BYTE:    desc = "B"; break;
    case NJ_PT_CHAR:    desc = "C"; break;
    case NJ_PT_SHORT:   desc = "S"; break;
    case NJ_PT_INT:     desc = "I"; break;
    case NJ_PT_LONG:    desc = "J"; break;
    case NJ_PT_FLOAT:   desc = "F"; break;
    case NJ_PT_DOUBLE:  desc = "D"; break;
    default:            return 0;
    }
    if (!box->cls) {
        return 0;
    }
    f = nj_field_lookup(vm, box->cls->name, "value", desc, 0);
    if (!f) {
        return 0;
    }
    switch (ptype) {
    case NJ_PT_LONG:
        out->j = nj_field_read_long(vm, box, f);
        break;
    case NJ_PT_FLOAT: {
        nj_u4 b = nj_field_read_bits32(vm, box, f);
        memcpy(&out->f, &b, sizeof(b));
        break;
    }
    case NJ_PT_DOUBLE: {
        nj_u8 b = nj_field_read_bits64(vm, box, f);
        memcpy(&out->d, &b, sizeof(b));
        break;
    }
    default:
        out->i = nj_field_read_int(vm, box, f);
        break;
    }
    return 1;
}

NJFN(method_invoke)
{
    nj_ref m = NJ_THIS(args);
    nj_ref target = NJ_ARG_REF(args, 0);
    nj_ref params = NJ_ARG_REF(args, 1);
    nj_ref clazz_ref = reflect_method_field(vm, m, "clazz",
                                            "Ljava/lang/Class;");
    nj_ref name_ref = reflect_method_field(vm, m, "name",
                                           "Ljava/lang/String;");
    nj_ref desc_ref = reflect_method_field(vm, m, "desc",
                                           "Ljava/lang/String;");
    nj_class *decl;
    nj_class *k;
    nj_method *found = NULL;
    char name[160];
    char desc[400];
    nj_value argv[20];
    nj_u2 argc = 0u;
    nj_value result;
    nj_status st;

    if (!clazz_ref || clazz_ref->kind != NJ_OBJ_CLASS ||
        !clazz_ref->u.mirror) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "Method has no declaring class");
    }
    decl = clazz_ref->u.mirror;
    if (!name_ref || !desc_ref ||
        nj_string_to_utf8(name_ref, name, (int)sizeof(name)) <= 0 ||
        nj_string_to_utf8(desc_ref, desc, (int)sizeof(desc)) <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "Method is malformed");
    }
    for (k = decl; k != NULL && !found; k = k->super) {
        nj_u2 i;
        for (i = 0u; i < k->method_count; i++) {
            if (reflect_text_eq(k->methods[i].name, name) &&
                reflect_text_eq(k->methods[i].desc, desc)) {
                found = &k->methods[i];
                break;
            }
        }
    }
    if (!found) {
        return nj_throw(vm, "java/lang/NoSuchMethodException", name);
    }
    memset(argv, 0, sizeof(argv));
    if ((found->flags & NJ_ACC_STATIC) == 0u) {
        if (!target) {
            return nj_throw(vm, "java/lang/NullPointerException", name);
        }
        argv[argc++].r = target;
    }
    {
        const char *p = desc + 1;
        nj_u4 i = 0u;
        while (*p && *p != ')') {
            nj_size span = reflect_desc_len(p);
            nj_u1 pt;
            nj_u1 w = 1u;
            nj_ref arg_ref = params ? nj_array_get_ref(params, i) : NULL;
            nj_value v;

            if (span == 0u) {
                return nj_throw(vm, "java/lang/IllegalArgumentException",
                                name);
            }
            pt = nj_desc_ptype(p, &w);
            v.bits = 0u;
            if (pt == NJ_PT_OBJECT) {
                v.r = arg_ref;
            } else if (!reflect_unbox(vm, pt, arg_ref, &v)) {
                return nj_throw(vm, "java/lang/IllegalArgumentException",
                                name);
            }
            if ((nj_u4)argc + 2u >= (nj_u4)(sizeof(argv) / sizeof(argv[0]))) {
                return nj_throw(vm, "java/lang/IllegalArgumentException",
                                name);
            }
            argv[argc++] = v;
            if (*p == 'J' || *p == 'D') {
                argc++;
            }
            p += span;
            i++;
        }
    }
    result.bits = 0u;
    if ((found->flags & NJ_ACC_STATIC) != 0u) {
        st = nj_call_static(vm, decl->name, name, desc, argv, &result);
    } else {
        st = nj_call(vm, target, decl->name, name, desc, argv, &result);
    }
    if (st != NJ_OK) {
        return st;   /* the target's exception is already pending */
    }
    switch (found->ret_ptype) {
    case NJ_PT_VOID:
        ret->r = NULL;
        break;
    case NJ_PT_OBJECT:
        ret->r = result.r;
        break;
    default:
        ret->r = reflect_box(vm, found->ret_ptype, &result);
        if (!ret->r) {
            return nj_throw_oom(vm);
        }
        break;
    }
    return NJ_OK;
}

NJFN(method_toString)
{
    ret->r = reflect_method_field(vm, NJ_THIS(args), "name",
                                  "Ljava/lang/String;");
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }
static const nj_builtin_field method_fields[] = {
    { "clazz", "Ljava/lang/Class;", 0 },
    { "name",  "Ljava/lang/String;", 0 },
    { "desc",  "Ljava/lang/String;", 0 }
};

static const nj_builtin_method method_methods[] = {
    M("getName",             "()Ljava/lang/String;", 0),
    M("getDeclaringClass",   "()Ljava/lang/Class;", 0),
    M("getParameterTypes",   "()[Ljava/lang/Class;", 0),
    M("getReturnType",       "()Ljava/lang/Class;", 0),
    M("invoke",
      "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;", 0),
    M("toString",            "()Ljava/lang/String;", 0)
};


static const nj_builtin_method object_methods[] = {
    M("<init>",     "()V",                  0),
    M("hashCode",   "()I",                  0),
    M("equals",     "(Ljava/lang/Object;)Z", 0),
    M("getClass",   "()Ljava/lang/Class;",  0),
    M("toString",   "()Ljava/lang/String;", 0),
    M("notify",     "()V",                  NJ_ACC_FINAL),
    M("notifyAll",  "()V",                  NJ_ACC_FINAL),
    M("wait",       "()V",                  NJ_ACC_FINAL),
    M("wait",       "(J)V",                 NJ_ACC_FINAL),
    M("wait",       "(JI)V",                NJ_ACC_FINAL)
};

static const nj_builtin_method class_methods[] = {
    M("getName",        "()Ljava/lang/String;",                 0),
    M("getResourceAsStream",
      "(Ljava/lang/String;)Ljava/io/InputStream;",              0),
    M("isArray",        "()Z",                                  0),
    M("isInterface",    "()Z",                                  0),
    M("isInstance",     "(Ljava/lang/Object;)Z",                0),
    M("isAssignableFrom", "(Ljava/lang/Class;)Z",                 0),
    M("getSuperclass",  "()Ljava/lang/Class;",                  0),
    M("forName",        "(Ljava/lang/String;)Ljava/lang/Class;", NJ_ACC_STATIC),
    M("newInstance",    "()Ljava/lang/Object;",                 0),
    M("getMethod",
      "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;", 0),
    M("getDeclaredMethod",
      "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;", 0),
    M("toString",       "()Ljava/lang/String;",                 0)
};

static const nj_builtin_field reference_fields[] = {
    { "referent", "Ljava/lang/Object;", NJ_ACC_PRIVATE }
};

static const nj_builtin_method reference_methods[] = {
    M("get", "()Ljava/lang/Object;", 0),
    M("clear", "()V", 0)
};

static const nj_builtin_method weak_methods[] = {
    M("<init>", "(Ljava/lang/Object;)V", 0),
    M("get", "()Ljava/lang/Object;", 0),
    M("clear", "()V", 0)
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
    M("printStackTrace",     "()V",                                 0),
    M("printStackTrace",     "(Ljava/io/PrintStream;)V",            0)
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
    X("java/lang/NoSuchMethodException",        "java/lang/Exception")       \
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
    { "java/lang/reflect/Method", "java/lang/Object",
      NJ_ACC_PUBLIC | NJ_ACC_FINAL,
      method_fields, (nj_u2)(sizeof(method_fields) / sizeof(method_fields[0])),
      method_methods,
      (nj_u2)(sizeof(method_methods) / sizeof(method_methods[0])) },
    { "java/lang/Throwable", "java/lang/Object", NJ_ACC_PUBLIC,
      throwable_fields, (nj_u2)(sizeof(throwable_fields) / sizeof(throwable_fields[0])),
      throwable_methods,
      (nj_u2)(sizeof(throwable_methods) / sizeof(throwable_methods[0])) },
    { "java/lang/ref/Reference", "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      reference_fields, (nj_u2)(sizeof(reference_fields) / sizeof(reference_fields[0])),
      reference_methods, (nj_u2)(sizeof(reference_methods) / sizeof(reference_methods[0])) },
    { "java/lang/ref/WeakReference", "java/lang/ref/Reference", NJ_ACC_PUBLIC,
      NULL, 0u,
      weak_methods, (nj_u2)(sizeof(weak_methods) / sizeof(weak_methods[0])) },
    NJ_EXC_LIST(NJ_X_DECL)
};
const int nj_lib_object_class_count =
    (int)(sizeof(nj_lib_object_classes) / sizeof(nj_lib_object_classes[0]));

const nj_native_entry nj_lib_object_table[] = {
    { "java/lang/ref/Reference", "get", "()Ljava/lang/Object;", weak_get },
    { "java/lang/ref/Reference", "clear", "()V", weak_clear },
    { "java/lang/ref/WeakReference", "<init>", "(Ljava/lang/Object;)V", weak_init },
    { "java/lang/ref/WeakReference", "get", "()Ljava/lang/Object;", weak_get },
    { "java/lang/ref/WeakReference", "clear", "()V", weak_clear },
    { "java/lang/Object",    "<init>",            "()V",                   obj_init },
    { "java/lang/Object",    "hashCode",          "()I",                   obj_hashCode },
    { "java/lang/Object",    "equals",            "(Ljava/lang/Object;)Z", obj_equals },
    { "java/lang/Object",    "getClass",          "()Ljava/lang/Class;",   obj_getClass },
    { "java/lang/Object",    "toString",          "()Ljava/lang/String;",  obj_toString },
    { "java/lang/Object",    "notify",            "()V",                   obj_notify },
    { "java/lang/Object",    "notifyAll",         "()V",                   obj_notifyAll },
    { "java/lang/Object",    "wait",              "()V",                   obj_wait },
    { "java/lang/Object",    "wait",              "(J)V",                  obj_wait_timeout },
    { "java/lang/Object",    "wait",              "(JI)V",                 obj_wait_timeout_ns },

    { "java/lang/Class",     "getName",           "()Ljava/lang/String;",  class_getName },
    { "java/lang/Class",     "getResourceAsStream",
      "(Ljava/lang/String;)Ljava/io/InputStream;", class_getResourceAsStream },
    { "java/lang/Class",     "isArray",           "()Z",                   class_isArray },
    { "java/lang/Class",     "isInterface",       "()Z",                   class_isInterface },
    { "java/lang/Class",     "isInstance",        "(Ljava/lang/Object;)Z", class_isInstance },
    { "java/lang/Class",     "isAssignableFrom",  "(Ljava/lang/Class;)Z", class_isAssignableFrom },
    { "java/lang/Class",     "getSuperclass",     "()Ljava/lang/Class;",   class_getSuperclass },
    { "java/lang/Class",     "forName",           "(Ljava/lang/String;)Ljava/lang/Class;", class_forName },
    { "java/lang/Class",     "newInstance",       "()Ljava/lang/Object;",  class_newInstance },
    { "java/lang/Class",     "toString",          "()Ljava/lang/String;",  class_toString },
    { "java/lang/Class",     "getMethod",
      "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;", class_getMethod },
    { "java/lang/Class",     "getDeclaredMethod",
      "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;", class_getDeclaredMethod },

    { "java/lang/reflect/Method", "getName",            "()Ljava/lang/String;", method_getName },
    { "java/lang/reflect/Method", "getDeclaringClass",  "()Ljava/lang/Class;",  method_getDeclaringClass },
    { "java/lang/reflect/Method", "getParameterTypes",  "()[Ljava/lang/Class;", method_getParameterTypes },
    { "java/lang/reflect/Method", "getReturnType",      "()Ljava/lang/Class;",  method_getReturnType },
    { "java/lang/reflect/Method", "invoke",
      "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;", method_invoke },
    { "java/lang/reflect/Method", "toString",           "()Ljava/lang/String;", method_toString },

    { "java/lang/Throwable", "<init>",            "()V",                          throwable_init },
    { "java/lang/Throwable", "<init>",            "(Ljava/lang/String;)V",        throwable_init_msg },
    { "java/lang/Throwable", "getMessage",        "()Ljava/lang/String;",         throwable_getMessage },
    { "java/lang/Throwable", "toString",          "()Ljava/lang/String;",         throwable_toString },
    { "java/lang/Throwable", "fillInStackTrace",  "()Ljava/lang/Throwable;",      throwable_fillInStackTrace },
    { "java/lang/Throwable", "printStackTrace",   "()V",                          throwable_printStackTrace },
    { "java/lang/Throwable", "printStackTrace",   "(Ljava/io/PrintStream;)V",      throwable_printStackTrace_stream }
};
const int nj_lib_object_count =
    (int)(sizeof(nj_lib_object_table) / sizeof(nj_lib_object_table[0]));
