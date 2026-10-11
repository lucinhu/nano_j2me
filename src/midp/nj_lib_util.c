/*
 * nano_j2me - nj_lib_util.c
 *
 * java.util: Vector, Stack, Hashtable, Enumeration, Random, Date, Calendar,
 * TimeZone and Timer/TimerTask.
 *
 * Every piece of state lives in Java fields declared in the tables at the
 * bottom of this file - a Vector *is* an Object[] plus a count, a Calendar is
 * a long plus a TimeZone - so the garbage collector owns the lifetime of the
 * class library exactly as it does for a class parsed out of a JAR and
 * nj_vm_destroy() leaves nothing behind.
 *
 * Exception behaviour is deliberately the Java one: elementAt() with a bad
 * index throws ArrayIndexOutOfBoundsException, firstElement() on an empty
 * Vector throws NoSuchElementException and Hashtable rejects null keys and
 * values with a NullPointerException.  Games catch these, so the exact type
 * matters.
 *
 * ---------------------------------------------------------------------------
 * TIMER: WHY THERE IS A TINY CLASS FILE EMBEDDED BELOW
 * ---------------------------------------------------------------------------
 * java.util.Timer is Java code in CLDC: a helper thread loops, sleeps until
 * the next deadline and fires the due tasks.  The loop has to be Java here as
 * well, because a *native* cannot be suspended and resumed: when a native
 * returns NJ_ERR_TIMEOUT the interpreter pops its frame (docs/ARCHITECTURE.md
 * section 5) and only a Java frame above it can be resumed later.  A timer
 * thread implemented as one native loop could therefore never sleep without
 * either stalling the whole VM or dying on the spot.
 *
 * The class library is C, so there is no .class file to load for such a
 * helper.  The 428 bytes in nj_timer_helper_class[] are the compiled form of
 *
 *     package nj2me.util;
 *
 *     public final class TimerThread {
 *         public Object timer;
 *
 *         public TimerThread(Object t) { this.timer = t; }
 *
 *         public void run() {
 *             while (pump(timer)) {
 *             }
 *         }
 *
 *         public static native boolean pump(Object timer);
 *     }
 *
 * (javac --release 8).  `pump` carries no Code attribute because it is native
 * and the loop body is four bytecodes: getfield, invokestatic, ifeq, goto.
 * Its C binding is timer_pump() below and it is listed in nj_lib_util_table[],
 * so the "every native is reachable from the aggregation table" rule holds.
 * The helper keeps no state of its own: the task list, the cancelled flag and
 * the deadlines are read out of java.util.Timer's fields on every pump().
 */
#include "nj_internal.h"

#if NJ_USE_LIBC
#  include <string.h>
#endif

#include <math.h>

#define NJFN(name) NJ_NATIVE_FN(name)

/* ------------------------------------------------------------------ */
/* Field lookup shorthands                                             */
/* ------------------------------------------------------------------ */
static nj_field *fld(nj_vm *vm, const char *cls, const char *name,
                     const char *desc)
{
    return nj_field_lookup(vm, cls, name, desc, 0);
}

static nj_field *sfld(nj_vm *vm, const char *cls, const char *name,
                      const char *desc)
{
    return nj_field_lookup(vm, cls, name, desc, 1);
}

static nj_s4 fget(nj_vm *vm, nj_ref o, nj_field *f, nj_s4 dflt)
{
    return f ? nj_field_read_int(vm, o, f) : dflt;
}

static nj_ref rget(nj_vm *vm, nj_ref o, nj_field *f)
{
    return f ? nj_field_read_ref(vm, o, f) : NULL;
}

/*
 * Bounded C string builder.  Building toString() text in a C buffer and
 * creating the Java string once keeps the rooting story trivial: no partially
 * built String is ever held across an allocation.
 */
static void put_str(char *buf, int cap, int *len, const char *s)
{
    while (s && *s && *len + 1 < cap) {
        buf[(*len)++] = *s++;
    }
    buf[*len] = '\0';
}

static void put_int(char *buf, int cap, int *len, nj_s8 v)
{
    char tmp[24];
    int n = 0;
    int neg = 0;
    nj_u8 u;

    if (v < 0) {
        neg = 1;
        u = (nj_u8)(-(v + 1)) + 1u;
    } else {
        u = (nj_u8)v;
    }
    do {
        tmp[n++] = (char)('0' + (int)(u % 10u));
        u /= 10u;
    } while (u != 0u && n < (int)sizeof(tmp));
    if (neg && *len + 1 < cap) {
        buf[(*len)++] = '-';
    }
    while (n > 0) {
        if (*len + 1 >= cap) {
            break;
        }
        buf[(*len)++] = tmp[--n];
    }
    buf[*len] = '\0';
}

/* Text of an arbitrary object, via String.valueOf (which handles null). */
static void put_value(nj_vm *vm, char *buf, int cap, int *len, nj_ref o)
{
    nj_value a[1];
    nj_ref s;
    char tmp[128];

    a[0].r = o;
    s = nj_call_ref(vm, NULL, "java/lang/String", "valueOf",
                    "(Ljava/lang/Object;)Ljava/lang/String;", a);
    if (!s) {
        nj_clear_exception(vm);
        put_str(buf, cap, len, "null");
        return;
    }
    tmp[0] = '\0';
    (void)nj_string_to_utf8(s, tmp, (int)sizeof(tmp));
    put_str(buf, cap, len, tmp);
}

/* o == null ? e == null : o.equals(e); *err is set if a Java call failed. */
static int obj_equals(nj_vm *vm, nj_ref o, nj_ref e, int *err)
{
    nj_value a[1];
    nj_value r;

    if (o == e) {
        return 1;
    }
    if (!o || !e) {
        return 0;
    }
    a[0].r = e;
    r.i = 0;
    if (nj_call(vm, o, "java/lang/Object", "equals", "(Ljava/lang/Object;)Z",
                a, &r) != NJ_OK) {
        *err = 1;
        return 0;
    }
    return r.i != 0;
}

/* ------------------------------------------------------------------ */
/* java.util.Vector                                                    */
/* ------------------------------------------------------------------ */
#define V_CLS  "java/util/Vector"
#define V_NAME "elementData"
#define V_DESC "[Ljava/lang/Object;"

static nj_field *vec_fdata(nj_vm *vm)  { return fld(vm, V_CLS, V_NAME, V_DESC); }
static nj_field *vec_fcount(nj_vm *vm) { return fld(vm, V_CLS, "elementCount", "I"); }
static nj_field *vec_finc(nj_vm *vm)   { return fld(vm, V_CLS, "capacityIncrement", "I"); }

static nj_ref vec_data(nj_vm *vm, nj_ref v)
{
    return rget(vm, v, vec_fdata(vm));
}

static nj_s4 vec_count(nj_vm *vm, nj_ref v)
{
    return fget(vm, v, vec_fcount(vm), 0);
}

static void vec_store_count(nj_vm *vm, nj_ref v, nj_s4 n)
{
    nj_field *f = vec_fcount(vm);
    if (f) {
        nj_field_write_int(vm, v, f, n);
    }
}

/*
 * Grow elementData so that at least `min_cap` elements fit.  The new array is
 * allocated *before* the old one is read, so the payload stays reachable
 * through the field while the allocation (which may collect) runs.
 */
static nj_status vec_ensure(nj_vm *vm, nj_ref v, nj_s4 min_cap)
{
    nj_field *fdata = vec_fdata(vm);
    nj_ref data;
    nj_ref fresh;
    nj_s4 cap, inc, newcap;

    if (!fdata) {
        return nj_throw(vm, "java/lang/InternalError", "Vector layout");
    }
    data = nj_field_read_ref(vm, v, fdata);
    cap = data ? (nj_s4)data->length : 0;
    if (min_cap <= cap) {
        return NJ_OK;
    }
    inc = fget(vm, v, vec_finc(vm), 0);
    newcap = (inc > 0) ? cap + inc : cap * 2;
    if (newcap < min_cap) {
        newcap = min_cap;
    }
    if (newcap < 1) {
        newcap = 1;
    }
    fresh = nj_object_array_new(vm, "java/lang/Object", (nj_u4)newcap);
    if (!fresh) {
        return nj_throw_oom(vm);
    }
    {
        nj_u4 i;
        for (i = 0u; data && i < data->length; i++) {
            nj_array_set_ref(fresh, i, nj_array_get_ref(data, i));
        }
    }
    nj_field_write_ref(vm, v, fdata, fresh);
    return NJ_OK;
}

static nj_status vec_check(nj_vm *vm, nj_ref v)
{
    if (!v) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (!vec_fdata(vm)) {
        return nj_throw(vm, "java/lang/InternalError", "Vector layout");
    }
    return NJ_OK;
}

/* Drop element `idx`, shifting the tail down.  Also used by the Timer pump. */
static void vec_remove_at(nj_vm *vm, nj_ref v, nj_s4 idx)
{
    nj_ref data = vec_data(vm, v);
    nj_s4 n = vec_count(vm, v);
    nj_s4 i;

    if (!data || idx < 0 || idx >= n) {
        return;
    }
    for (i = idx; i + 1 < n; i++) {
        nj_array_set_ref(data, (nj_u4)i, nj_array_get_ref(data, (nj_u4)(i + 1)));
    }
    nj_array_set_ref(data, (nj_u4)(n - 1), NULL);
    vec_store_count(vm, v, n - 1);
}

/* addElement() without the native call frame, so C code can append too. */
static nj_status vec_append(nj_vm *vm, nj_ref v, nj_ref obj)
{
    nj_s4 n;
    nj_ref data;
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    n = vec_count(vm, v);
    st = vec_ensure(vm, v, n + 1);
    if (st != NJ_OK) {
        return st;
    }
    data = vec_data(vm, v);
    nj_array_set_ref(data, (nj_u4)n, obj);
    vec_store_count(vm, v, n + 1);
    return NJ_OK;
}

NJFN(vec_init)
{
    return NJ_OK;
}

NJFN(vec_init_cap)
{
    nj_s4 cap = NJ_ARG_INT(args, 0);
    if (cap < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative capacity");
    }
    return vec_ensure(vm, NJ_THIS(args), cap);
}

NJFN(vec_init_cap_inc)
{
    nj_ref v = NJ_THIS(args);
    nj_field *f = vec_finc(vm);
    nj_s4 cap = NJ_ARG_INT(args, 0);

    if (f) {
        nj_field_write_int(vm, v, f, NJ_ARG_INT(args, 1));
    }
    if (cap < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative capacity");
    }
    return vec_ensure(vm, v, cap);
}

NJFN(vec_addElement)
{
    return vec_append(vm, NJ_THIS(args), NJ_ARG_REF(args, 0));
}

NJFN(vec_add)   /* boolean add(Object) */
{
    nj_status st = vec_append(vm, NJ_THIS(args), NJ_ARG_REF(args, 0));
    if (st != NJ_OK) {
        return st;
    }
    ret->i = 1;
    return NJ_OK;
}

NJFN(vec_insertElementAt)
{
    nj_ref v = NJ_THIS(args);
    nj_ref obj = NJ_ARG_REF(args, 0);
    nj_s4 idx = NJ_ARG_INT(args, 1);
    nj_s4 n, i;
    nj_ref data;
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    n = vec_count(vm, v);
    if (idx < 0 || idx > n) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    st = vec_ensure(vm, v, n + 1);
    if (st != NJ_OK) {
        return st;
    }
    data = vec_data(vm, v);
    for (i = n; i > idx; i--) {
        nj_array_set_ref(data, (nj_u4)i, nj_array_get_ref(data, (nj_u4)(i - 1)));
    }
    nj_array_set_ref(data, (nj_u4)idx, obj);
    vec_store_count(vm, v, n + 1);
    return NJ_OK;
}

NJFN(vec_removeElement)
{
    nj_ref v = NJ_THIS(args);
    nj_ref o = NJ_ARG_REF(args, 0);
    nj_s4 i, n;
    nj_ref data;
    int err = 0;
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    n = vec_count(vm, v);
    data = vec_data(vm, v);
    for (i = 0; i < n; i++) {
        if (obj_equals(vm, o, nj_array_get_ref(data, (nj_u4)i), &err)) {
            vec_remove_at(vm, v, i);
            ret->i = 1;
            return NJ_OK;
        }
        if (err) {
            return NJ_ERR;
        }
    }
    ret->i = 0;
    return NJ_OK;
}

NJFN(vec_removeElementAt)
{
    nj_ref v = NJ_THIS(args);
    nj_s4 idx = NJ_ARG_INT(args, 0);
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    if (idx < 0 || idx >= vec_count(vm, v)) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    vec_remove_at(vm, v, idx);
    return NJ_OK;
}

NJFN(vec_removeAllElements)
{
    nj_ref v = NJ_THIS(args);
    nj_ref data;
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    data = vec_data(vm, v);
    if (data) {
        nj_u4 i;
        for (i = 0u; i < data->length; i++) {
            nj_array_set_ref(data, i, NULL);
        }
    }
    vec_store_count(vm, v, 0);
    return NJ_OK;
}

NJFN(vec_clear)
{
    return vec_removeAllElements(vm, th, args, ret);
}

NJFN(vec_setElementAt)
{
    nj_ref v = NJ_THIS(args);
    nj_s4 idx = NJ_ARG_INT(args, 1);
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    if (idx < 0 || idx >= vec_count(vm, v)) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    nj_array_set_ref(vec_data(vm, v), (nj_u4)idx, NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(vec_elementAt)
{
    nj_ref v = NJ_THIS(args);
    nj_s4 idx = NJ_ARG_INT(args, 0);
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    if (idx < 0 || idx >= vec_count(vm, v)) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    ret->r = nj_array_get_ref(vec_data(vm, v), (nj_u4)idx);
    return NJ_OK;
}

NJFN(vec_firstElement)
{
    nj_ref v = NJ_THIS(args);
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    if (vec_count(vm, v) <= 0) {
        return nj_throw(vm, "java/util/NoSuchElementException", NULL);
    }
    ret->r = nj_array_get_ref(vec_data(vm, v), 0u);
    return NJ_OK;
}

NJFN(vec_lastElement)
{
    nj_ref v = NJ_THIS(args);
    nj_s4 n;
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    n = vec_count(vm, v);
    if (n <= 0) {
        return nj_throw(vm, "java/util/NoSuchElementException", NULL);
    }
    ret->r = nj_array_get_ref(vec_data(vm, v), (nj_u4)(n - 1));
    return NJ_OK;
}

NJFN(vec_size)
{
    nj_ref v = NJ_THIS(args);
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    ret->i = vec_count(vm, v);
    return NJ_OK;
}

NJFN(vec_capacity)
{
    nj_ref v = NJ_THIS(args);
    nj_ref data;
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    data = vec_data(vm, v);
    ret->i = data ? (nj_s4)data->length : 0;
    return NJ_OK;
}

NJFN(vec_isEmpty)
{
    nj_ref v = NJ_THIS(args);
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    ret->i = (vec_count(vm, v) == 0) ? 1 : 0;
    return NJ_OK;
}

NJFN(vec_contains)
{
    nj_ref v = NJ_THIS(args);
    nj_ref o = NJ_ARG_REF(args, 0);
    nj_s4 i, n;
    nj_ref data;
    int err = 0;
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    n = vec_count(vm, v);
    data = vec_data(vm, v);
    for (i = 0; i < n; i++) {
        if (obj_equals(vm, o, nj_array_get_ref(data, (nj_u4)i), &err)) {
            ret->i = 1;
            return NJ_OK;
        }
        if (err) {
            return NJ_ERR;
        }
    }
    ret->i = 0;
    return NJ_OK;
}

static nj_s4 vec_index_of(nj_vm *vm, nj_ref v, nj_ref o, nj_s4 from, int last)
{
    nj_s4 i, n = vec_count(vm, v);
    nj_ref data = vec_data(vm, v);
    int err = 0;

    if (last) {
        if (from >= n) {
            from = n - 1;
        }
        for (i = from; i >= 0; i--) {
            if (obj_equals(vm, o, nj_array_get_ref(data, (nj_u4)i), &err)) {
                return i;
            }
            if (err) {
                nj_clear_exception(vm);
                return -1;
            }
        }
    } else {
        for (i = from; i < n; i++) {
            if (obj_equals(vm, o, nj_array_get_ref(data, (nj_u4)i), &err)) {
                return i;
            }
            if (err) {
                nj_clear_exception(vm);
                return -1;
            }
        }
    }
    return -1;
}

NJFN(vec_indexOf)
{
    nj_ref v = NJ_THIS(args);
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    ret->i = vec_index_of(vm, v, NJ_ARG_REF(args, 0), 0, 0);
    return NJ_OK;
}

NJFN(vec_indexOf_from)
{
    nj_ref v = NJ_THIS(args);
    nj_s4 from = NJ_ARG_INT(args, 1);
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    if (from < 0) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    ret->i = vec_index_of(vm, v, NJ_ARG_REF(args, 0), from, 0);
    return NJ_OK;
}

NJFN(vec_lastIndexOf)
{
    nj_ref v = NJ_THIS(args);
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    ret->i = vec_index_of(vm, v, NJ_ARG_REF(args, 0), vec_count(vm, v) - 1, 1);
    return NJ_OK;
}

NJFN(vec_lastIndexOf_from)
{
    nj_ref v = NJ_THIS(args);
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    ret->i = vec_index_of(vm, v, NJ_ARG_REF(args, 0), NJ_ARG_INT(args, 1), 1);
    return NJ_OK;
}

NJFN(vec_remove_index)     /* Object remove(int) */
{
    nj_ref v = NJ_THIS(args);
    nj_s4 idx = NJ_ARG_INT(args, 0);
    nj_ref old;
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    if (idx < 0 || idx >= vec_count(vm, v)) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    old = nj_array_get_ref(vec_data(vm, v), (nj_u4)idx);
    vec_remove_at(vm, v, idx);
    ret->r = old;
    return NJ_OK;
}

NJFN(vec_get)
{
    return vec_elementAt(vm, th, args, ret);
}

NJFN(vec_set)
{
    nj_ref v = NJ_THIS(args);
    nj_s4 idx = NJ_ARG_INT(args, 0);
    nj_ref old;
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    if (idx < 0 || idx >= vec_count(vm, v)) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    old = nj_array_get_ref(vec_data(vm, v), (nj_u4)idx);
    nj_array_set_ref(vec_data(vm, v), (nj_u4)idx, NJ_ARG_REF(args, 1));
    ret->r = old;
    return NJ_OK;
}

NJFN(vec_setSize)
{
    nj_ref v = NJ_THIS(args);
    nj_s4 want = NJ_ARG_INT(args, 0);
    nj_s4 n, i;
    nj_ref data;
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    if (want < 0) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    n = vec_count(vm, v);
    if (want > n) {
        st = vec_ensure(vm, v, want);
        if (st != NJ_OK) {
            return st;
        }
    }
    data = vec_data(vm, v);
    if (data) {
        for (i = (want > n) ? n : want; i < ((want > n) ? want : n); i++) {
            nj_array_set_ref(data, (nj_u4)i, NULL);
        }
    }
    vec_store_count(vm, v, want);
    return NJ_OK;
}

NJFN(vec_ensureCapacity)
{
    nj_s4 want = NJ_ARG_INT(args, 0);

    if (want < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", NULL);
    }
    return vec_ensure(vm, NJ_THIS(args), want);
}

NJFN(vec_trimToSize)
{
    nj_ref v = NJ_THIS(args);
    nj_s4 n;
    nj_ref data, fresh;
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    n = vec_count(vm, v);
    data = vec_data(vm, v);
    if (data && (nj_s4)data->length == n) {
        return NJ_OK;
    }
    fresh = nj_object_array_new(vm, "java/lang/Object", (nj_u4)(n > 0 ? n : 0));
    if (!fresh) {
        return nj_throw_oom(vm);
    }
    if (data) {
        nj_s4 i;
        for (i = 0; i < n; i++) {
            nj_array_set_ref(fresh, (nj_u4)i, nj_array_get_ref(data, (nj_u4)i));
        }
    }
    nj_field_write_ref(vm, v, vec_fdata(vm), fresh);
    return NJ_OK;
}

NJFN(vec_copyInto)
{
    nj_ref v = NJ_THIS(args);
    nj_ref dst = NJ_ARG_REF(args, 0);
    nj_s4 n, i;
    nj_ref data;
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    if (!dst) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    n = vec_count(vm, v);
    if ((nj_s4)dst->length < n) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    data = vec_data(vm, v);
    for (i = 0; i < n; i++) {
        nj_array_set_ref(dst, (nj_u4)i, nj_array_get_ref(data, (nj_u4)i));
    }
    return NJ_OK;
}

NJFN(vec_toString)
{
    nj_ref v = NJ_THIS(args);
    char buf[256];
    int len = 0;
    nj_s4 i, n;
    nj_ref data;
    nj_status st = vec_check(vm, v);

    if (st != NJ_OK) {
        return st;
    }
    n = vec_count(vm, v);
    data = vec_data(vm, v);
    buf[0] = '\0';
    put_str(buf, (int)sizeof(buf), &len, "[");
    for (i = 0; i < n; i++) {
        if (i > 0) {
            put_str(buf, (int)sizeof(buf), &len, ", ");
            if (len + 1 >= (int)sizeof(buf)) {
                break;
            }
        }
        put_value(vm, buf, (int)sizeof(buf), &len,
                  nj_array_get_ref(data, (nj_u4)i));
    }
    put_str(buf, (int)sizeof(buf), &len, "]");
    ret->r = nj_string_new(vm, buf);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

/* ------------------------------------------------------------------ */
/* java.util.VectorEnumerator                                          */
/* ------------------------------------------------------------------ */
#define VE_CLS "java/util/VectorEnumerator"

NJFN(ve_init)
{
    nj_field *f = fld(vm, VE_CLS, "veVector", "Ljava/util/Vector;");
    if (f) {
        nj_field_write_ref(vm, NJ_THIS(args), f, NJ_ARG_REF(args, 0));
    }
    return NJ_OK;
}

NJFN(ve_hasMoreElements)
{
    nj_ref e = NJ_THIS(args);
    nj_ref v = rget(vm, e, fld(vm, VE_CLS, "veVector", "Ljava/util/Vector;"));
    nj_s4 idx = fget(vm, e, fld(vm, VE_CLS, "veIndex", "I"), 0);

    if (!v) {
        ret->i = 0;
        return NJ_OK;
    }
    ret->i = (idx >= 0 && idx < vec_count(vm, v)) ? 1 : 0;
    return NJ_OK;
}

NJFN(ve_nextElement)
{
    nj_ref e = NJ_THIS(args);
    nj_ref v = rget(vm, e, fld(vm, VE_CLS, "veVector", "Ljava/util/Vector;"));
    nj_field *fi = fld(vm, VE_CLS, "veIndex", "I");
    nj_s4 idx = fget(vm, e, fi, 0);

    if (!v || idx < 0 || idx >= vec_count(vm, v)) {
        return nj_throw(vm, "java/util/NoSuchElementException", NULL);
    }
    if (fi) {
        nj_field_write_int(vm, e, fi, idx + 1);
    }
    ret->r = nj_array_get_ref(vec_data(vm, v), (nj_u4)idx);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* java.util.Hashtable                                                 */
/* ------------------------------------------------------------------ */
/*
 * Open addressing with linear probing.  htKeys/htValues hold the payload,
 * htHash the cached hashCode and htState the slot state (0 empty, 1 live,
 * 2 tombstone).  Deletion leaves a tombstone so a probe chain is never cut in
 * half, and the table doubles once it is 3/4 full: the load factor is a
 * property of the implementation, not a field.  A null key is rejected before
 * anything is read, which is why "key == null" can safely mean "empty slot".
 */
#define HT_CLS "java/util/Hashtable"
#define HT_EMPTY 0
#define HT_LIVE  1
#define HT_DEAD  2
#define HT_MIN_CAP 11

static nj_field *ht_fkeys(nj_vm *vm)  { return fld(vm, HT_CLS, "htKeys", "[Ljava/lang/Object;"); }
static nj_field *ht_fvals(nj_vm *vm)  { return fld(vm, HT_CLS, "htValues", "[Ljava/lang/Object;"); }
static nj_field *ht_fhash(nj_vm *vm)  { return fld(vm, HT_CLS, "htHash", "[I"); }
static nj_field *ht_fstate(nj_vm *vm) { return fld(vm, HT_CLS, "htState", "[I"); }
static nj_field *ht_fcount(nj_vm *vm) { return fld(vm, HT_CLS, "htCount", "I"); }

static nj_ref ht_key_arr(nj_vm *vm, nj_ref h) { return rget(vm, h, ht_fkeys(vm)); }
static nj_ref ht_val_arr(nj_vm *vm, nj_ref h) { return rget(vm, h, ht_fvals(vm)); }
static nj_s4  ht_count(nj_vm *vm, nj_ref h) { return fget(vm, h, ht_fcount(vm), 0); }

static nj_s4 ht_hashcode(nj_vm *vm, nj_ref key, int *err)
{
    nj_value r;

    r.i = 0;
    if (nj_call(vm, key, "java/lang/Object", "hashCode", "()I", NULL, &r) != NJ_OK) {
        *err = 1;
        return 0;
    }
    return r.i & 0x7FFFFFFF;
}

/*
 * Probe for `key`: returns the slot holding it or -1.  When insert_at is not
 * NULL it receives the slot a new mapping for `key` should use (the first
 * tombstone on the chain, or the empty slot that ends it).
 */
static nj_s4 ht_probe(nj_vm *vm, nj_ref h, nj_ref key, nj_s4 hash,
                      nj_s4 *insert_at, int *err)
{
    nj_ref keys = ht_key_arr(vm, h);
    nj_ref hh = rget(vm, h, ht_fhash(vm));
    nj_ref stt = rget(vm, h, ht_fstate(vm));
    nj_u4 cap, i, probes;
    nj_s4 tomb = -1;

    if (!keys || !hh || !stt) {
        return -1;
    }
    cap = keys->length;
    if (cap == 0u) {
        return -1;
    }
    i = (nj_u4)hash % cap;
    for (probes = 0u; probes < cap; probes++) {
        nj_s4 s = nj_array_get_int(stt, i);
        if (s == HT_EMPTY) {
            if (insert_at) {
                *insert_at = (tomb >= 0) ? tomb : (nj_s4)i;
            }
            return -1;
        }
        if (s == HT_DEAD) {
            if (tomb < 0) {
                tomb = (nj_s4)i;
            }
        } else if (nj_array_get_int(hh, i) == hash) {
            nj_ref k = nj_array_get_ref(keys, i);
            int eq;
            if (k == key) {
                if (insert_at) {
                    *insert_at = (nj_s4)i;
                }
                return (nj_s4)i;
            }
            eq = obj_equals(vm, k, key, err);
            if (*err) {
                return -1;
            }
            if (eq) {
                if (insert_at) {
                    *insert_at = (nj_s4)i;
                }
                return (nj_s4)i;
            }
        }
        i = (i + 1u) % cap;
    }
    if (insert_at) {
        *insert_at = tomb;
    }
    return -1;
}

static nj_status ht_alloc_arrays(nj_vm *vm, nj_ref h, nj_u4 cap)
{
    nj_ref keys = nj_object_array_new(vm, "java/lang/Object", cap);
    nj_ref vals = nj_object_array_new(vm, "java/lang/Object", cap);
    nj_ref hh = nj_int_array_new(vm, cap);
    nj_ref stt = nj_int_array_new(vm, cap);

    if (!keys || !vals || !hh || !stt) {
        return nj_throw_oom(vm);
    }
    nj_field_write_ref(vm, h, ht_fkeys(vm), keys);
    nj_field_write_ref(vm, h, ht_fvals(vm), vals);
    nj_field_write_ref(vm, h, ht_fhash(vm), hh);
    nj_field_write_ref(vm, h, ht_fstate(vm), stt);
    return NJ_OK;
}

static nj_status ht_grow(nj_vm *vm, nj_ref h, nj_u4 want)
{
    nj_ref oldk = ht_key_arr(vm, h);
    nj_ref oldv = ht_val_arr(vm, h);
    nj_ref oldh = rget(vm, h, ht_fhash(vm));
    nj_ref olds = rget(vm, h, ht_fstate(vm));
    nj_u4 cap, i;
    nj_status st;

    cap = (oldk && oldk->length * 2u > want) ? oldk->length * 2u : want;
    if (cap < HT_MIN_CAP) {
        cap = HT_MIN_CAP;
    }
    st = ht_alloc_arrays(vm, h, cap);
    if (st != NJ_OK) {
        return st;
    }
    if (oldk && oldv && oldh && olds) {
        for (i = 0u; i < oldk->length; i++) {
            if (nj_array_get_int(olds, i) == HT_LIVE) {
                nj_s4 slot = -1;
                int err = 0;
                (void)ht_probe(vm, h, nj_array_get_ref(oldk, i),
                               nj_array_get_int(oldh, i), &slot, &err);
                if (err) {
                    return NJ_ERR;
                }
                if (slot < 0) {
                    return nj_throw(vm, "java/lang/InternalError",
                                    "Hashtable rehash");
                }
                nj_array_set_ref(ht_key_arr(vm, h), (nj_u4)slot,
                                 nj_array_get_ref(oldk, i));
                nj_array_set_ref(ht_val_arr(vm, h), (nj_u4)slot,
                                 nj_array_get_ref(oldv, i));
                nj_array_set_int(rget(vm, h, ht_fhash(vm)), (nj_u4)slot,
                                 nj_array_get_int(oldh, i));
                nj_array_set_int(rget(vm, h, ht_fstate(vm)), (nj_u4)slot, HT_LIVE);
            }
        }
    }
    return NJ_OK;
}

static nj_status ht_prepare(nj_vm *vm, nj_ref h)
{
    if (!h) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (!ht_fkeys(vm)) {
        return nj_throw(vm, "java/lang/InternalError", "Hashtable layout");
    }
    if (!ht_key_arr(vm, h)) {
        return ht_alloc_arrays(vm, h, (nj_u4)HT_MIN_CAP);
    }
    return NJ_OK;
}

NJFN(ht_init)
{
    return NJ_OK;   /* arrays are allocated on first use / by <init>(I) */
}

NJFN(ht_init_cap)
{
    nj_s4 want = NJ_ARG_INT(args, 0);

    if (want < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", NULL);
    }
    return ht_alloc_arrays(vm, NJ_THIS(args),
                           (nj_u4)(want > 0 ? want : HT_MIN_CAP));
}

NJFN(ht_put)
{
    nj_ref h = NJ_THIS(args);
    nj_ref key = NJ_ARG_REF(args, 0);
    nj_ref val = NJ_ARG_REF(args, 1);
    nj_s4 hash, slot = -1, found;
    int err = 0;
    nj_status st = ht_prepare(vm, h);

    if (st != NJ_OK) {
        return st;
    }
    if (!key || !val) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    hash = ht_hashcode(vm, key, &err);
    if (err) {
        return NJ_ERR;
    }
    found = ht_probe(vm, h, key, hash, &slot, &err);
    if (err) {
        return NJ_ERR;
    }
    if (found >= 0) {
        nj_ref old = nj_array_get_ref(ht_val_arr(vm, h), (nj_u4)found);
        nj_array_set_ref(ht_val_arr(vm, h), (nj_u4)found, val);
        ret->r = old;
        return NJ_OK;
    }
    if ((ht_count(vm, h) + 1) * 4 >= (nj_s4)ht_key_arr(vm, h)->length * 3) {
        st = ht_grow(vm, h, (nj_u4)((ht_count(vm, h) + 1) * 4 / 3 + 1));
        if (st != NJ_OK) {
            return st;
        }
        slot = -1;
        found = ht_probe(vm, h, key, hash, &slot, &err);
        if (err) {
            return NJ_ERR;
        }
    }
    if (slot < 0) {
        return nj_throw(vm, "java/lang/InternalError", "Hashtable full");
    }
    nj_array_set_ref(ht_key_arr(vm, h), (nj_u4)slot, key);
    nj_array_set_ref(ht_val_arr(vm, h), (nj_u4)slot, val);
    nj_array_set_int(rget(vm, h, ht_fhash(vm)), (nj_u4)slot, hash);
    nj_array_set_int(rget(vm, h, ht_fstate(vm)), (nj_u4)slot, HT_LIVE);
    nj_field_write_int(vm, h, ht_fcount(vm), ht_count(vm, h) + 1);
    ret->r = NULL;
    return NJ_OK;
}

NJFN(ht_get)
{
    nj_ref h = NJ_THIS(args);
    nj_ref key = NJ_ARG_REF(args, 0);
    nj_s4 hash, slot;
    int err = 0;
    nj_status st = ht_prepare(vm, h);

    if (st != NJ_OK) {
        return st;
    }
    if (!key) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (!ht_key_arr(vm, h)) {
        ret->r = NULL;
        return NJ_OK;
    }
    hash = ht_hashcode(vm, key, &err);
    if (err) {
        return NJ_ERR;
    }
    slot = ht_probe(vm, h, key, hash, NULL, &err);
    if (err) {
        return NJ_ERR;
    }
    ret->r = (slot >= 0) ? nj_array_get_ref(ht_val_arr(vm, h), (nj_u4)slot) : NULL;
    return NJ_OK;
}

NJFN(ht_remove)
{
    nj_ref h = NJ_THIS(args);
    nj_ref key = NJ_ARG_REF(args, 0);
    nj_s4 hash, slot;
    nj_ref old;
    int err = 0;
    nj_status st = ht_prepare(vm, h);

    if (st != NJ_OK) {
        return st;
    }
    if (!key) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (!ht_key_arr(vm, h)) {
        ret->r = NULL;
        return NJ_OK;
    }
    hash = ht_hashcode(vm, key, &err);
    if (err) {
        return NJ_ERR;
    }
    slot = ht_probe(vm, h, key, hash, NULL, &err);
    if (err) {
        return NJ_ERR;
    }
    if (slot < 0) {
        ret->r = NULL;
        return NJ_OK;
    }
    old = nj_array_get_ref(ht_val_arr(vm, h), (nj_u4)slot);
    nj_array_set_ref(ht_key_arr(vm, h), (nj_u4)slot, NULL);
    nj_array_set_ref(ht_val_arr(vm, h), (nj_u4)slot, NULL);
    nj_array_set_int(rget(vm, h, ht_fstate(vm)), (nj_u4)slot, HT_DEAD);
    nj_field_write_int(vm, h, ht_fcount(vm), ht_count(vm, h) - 1);
    ret->r = old;
    return NJ_OK;
}

NJFN(ht_containsKey)
{
    nj_ref h = NJ_THIS(args);
    nj_ref key = NJ_ARG_REF(args, 0);
    nj_s4 hash, slot;
    int err = 0;
    nj_status st = ht_prepare(vm, h);

    if (st != NJ_OK) {
        return st;
    }
    if (!key) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (!ht_key_arr(vm, h)) {
        ret->i = 0;
        return NJ_OK;
    }
    hash = ht_hashcode(vm, key, &err);
    if (err) {
        return NJ_ERR;
    }
    slot = ht_probe(vm, h, key, hash, NULL, &err);
    if (err) {
        return NJ_ERR;
    }
    ret->i = (slot >= 0) ? 1 : 0;
    return NJ_OK;
}

NJFN(ht_containsValue)
{
    nj_ref h = NJ_THIS(args);
    nj_ref val = NJ_ARG_REF(args, 0);
    nj_ref keys, vals, stt;
    nj_u4 i;
    int err = 0;
    nj_status st = ht_prepare(vm, h);

    if (st != NJ_OK) {
        return st;
    }
    if (!val) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    keys = ht_key_arr(vm, h);
    vals = ht_val_arr(vm, h);
    stt = rget(vm, h, ht_fstate(vm));
    ret->i = 0;
    for (i = 0u; keys && vals && stt && i < keys->length; i++) {
        if (nj_array_get_int(stt, i) == HT_LIVE &&
            obj_equals(vm, val, nj_array_get_ref(vals, i), &err)) {
            ret->i = 1;
            return NJ_OK;
        }
        if (err) {
            return NJ_ERR;
        }
    }
    return NJ_OK;
}

NJFN(ht_contains)
{
    return ht_containsValue(vm, th, args, ret);
}

NJFN(ht_size)
{
    nj_ref h = NJ_THIS(args);
    nj_status st = ht_prepare(vm, h);

    if (st != NJ_OK) {
        return st;
    }
    ret->i = ht_count(vm, h);
    return NJ_OK;
}

NJFN(ht_isEmpty)
{
    nj_status st = ht_size(vm, th, args, ret);
    if (st != NJ_OK) {
        return st;
    }
    ret->i = (ret->i == 0) ? 1 : 0;
    return NJ_OK;
}

NJFN(ht_clear)
{
    nj_ref h = NJ_THIS(args);
    nj_u4 i;
    nj_ref keys, vals, stt;
    nj_status st = ht_prepare(vm, h);

    if (st != NJ_OK) {
        return st;
    }
    keys = ht_key_arr(vm, h);
    vals = ht_val_arr(vm, h);
    stt = rget(vm, h, ht_fstate(vm));
    for (i = 0u; keys && vals && stt && i < keys->length; i++) {
        nj_array_set_ref(keys, i, NULL);
        nj_array_set_ref(vals, i, NULL);
        nj_array_set_int(stt, i, HT_EMPTY);
    }
    nj_field_write_int(vm, h, ht_fcount(vm), 0);
    return NJ_OK;
}

/* Snapshot the live keys (or values) into a fresh Object[] for an enumerator. */
static nj_ref ht_snapshot(nj_vm *vm, nj_ref h, int want_values)
{
    nj_ref keys = ht_key_arr(vm, h);
    nj_ref vals = ht_val_arr(vm, h);
    nj_ref stt = rget(vm, h, ht_fstate(vm));
    nj_ref src, out;
    nj_s4 n = ht_count(vm, h);
    nj_u4 i, k = 0u;

    out = nj_object_array_new(vm, "java/lang/Object", (nj_u4)(n > 0 ? n : 0));
    if (!out) {
        return NULL;
    }
    if (!keys || !vals || !stt) {
        return out;
    }
    src = want_values ? vals : keys;
    for (i = 0u; i < keys->length && k < (nj_u4)n; i++) {
        if (nj_array_get_int(stt, i) == HT_LIVE) {
            nj_array_set_ref(out, k, nj_array_get_ref(src, i));
            k++;
        }
    }
    return out;
}

static nj_status ht_new_enumerator(nj_vm *vm, nj_ref snap, nj_value *ret)
{
    nj_ref e = nj_object_new(vm, "java/util/HashtableEnumerator");
    nj_field *f;

    if (!e) {
        return NJ_ERR;
    }
    f = fld(vm, "java/util/HashtableEnumerator", "heKeys",
            "[Ljava/lang/Object;");
    if (!f) {
        return nj_throw(vm, "java/lang/InternalError", "enumerator layout");
    }
    nj_field_write_ref(vm, e, f, snap);
    ret->r = e;
    return NJ_OK;
}

NJFN(ht_keys)
{
    nj_ref h = NJ_THIS(args);
    nj_ref snap;
    nj_status st = ht_prepare(vm, h);

    if (st != NJ_OK) {
        return st;
    }
    snap = ht_snapshot(vm, h, 0);
    if (!snap) {
        return nj_throw_oom(vm);
    }
    return ht_new_enumerator(vm, snap, ret);
}

NJFN(ht_elements)
{
    nj_ref h = NJ_THIS(args);
    nj_ref snap;
    nj_status st = ht_prepare(vm, h);

    if (st != NJ_OK) {
        return st;
    }
    snap = ht_snapshot(vm, h, 1);
    if (!snap) {
        return nj_throw_oom(vm);
    }
    return ht_new_enumerator(vm, snap, ret);
}

NJFN(ht_toString)
{
    nj_ref h = NJ_THIS(args);
    char buf[256];
    int len = 0;
    nj_ref keys, vals, stt;
    nj_u4 i;
    int first = 1;
    nj_status st = ht_prepare(vm, h);

    if (st != NJ_OK) {
        return st;
    }
    keys = ht_key_arr(vm, h);
    vals = ht_val_arr(vm, h);
    stt = rget(vm, h, ht_fstate(vm));
    buf[0] = '\0';
    put_str(buf, (int)sizeof(buf), &len, "{");
    for (i = 0u; keys && vals && stt && i < keys->length; i++) {
        if (nj_array_get_int(stt, i) != HT_LIVE) {
            continue;
        }
        if (!first) {
            put_str(buf, (int)sizeof(buf), &len, ", ");
        }
        first = 0;
        put_value(vm, buf, (int)sizeof(buf), &len, nj_array_get_ref(keys, i));
        put_str(buf, (int)sizeof(buf), &len, "=");
        put_value(vm, buf, (int)sizeof(buf), &len, nj_array_get_ref(vals, i));
    }
    put_str(buf, (int)sizeof(buf), &len, "}");
    ret->r = nj_string_new(vm, buf);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

/* ------------------------------------------------------------------ */
/* java.util.HashtableEnumerator                                       */
/* ------------------------------------------------------------------ */
#define HE_CLS "java/util/HashtableEnumerator"

static nj_ref he_array(nj_vm *vm, nj_ref e)
{
    return rget(vm, e, fld(vm, HE_CLS, "heKeys", "[Ljava/lang/Object;"));
}

NJFN(he_init)
{
    nj_field *f = fld(vm, HE_CLS, "heKeys", "[Ljava/lang/Object;");
    if (f) {
        nj_field_write_ref(vm, NJ_THIS(args), f, NJ_ARG_REF(args, 0));
    }
    return NJ_OK;
}

NJFN(he_hasMoreElements)
{
    nj_ref e = NJ_THIS(args);
    nj_ref a = he_array(vm, e);
    nj_s4 idx = fget(vm, e, fld(vm, HE_CLS, "heIndex", "I"), 0);

    ret->i = (a && idx >= 0 && (nj_u4)idx < a->length) ? 1 : 0;
    return NJ_OK;
}

NJFN(he_nextElement)
{
    nj_ref e = NJ_THIS(args);
    nj_ref a = he_array(vm, e);
    nj_field *fi = fld(vm, HE_CLS, "heIndex", "I");
    nj_s4 idx = fget(vm, e, fi, 0);

    if (!a || idx < 0 || (nj_u4)idx >= a->length) {
        return nj_throw(vm, "java/util/NoSuchElementException", NULL);
    }
    if (fi) {
        nj_field_write_int(vm, e, fi, idx + 1);
    }
    ret->r = nj_array_get_ref(a, (nj_u4)idx);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* java.util.Random                                                    */
/* ------------------------------------------------------------------ */
/*
 * The 48-bit linear congruential generator of java.util.Random, reproduced
 * exactly: seed = (seed * 0x5DEECE66D + 0xB) mod 2^48 and next(bits) takes the
 * top bits of the new seed.  A game that calls nextInt(6) therefore gets the
 * same sequence it would get on a real device.
 *
 * nextNextGaussian is declared as a J field rather than a D one: the
 * nj_field_read/write_long accessors *convert* a D field to and from an
 * integer, so the double is stored and fetched as its raw 8-byte pattern.
 */
#define RND_CLS "java/util/Random"
#define RND_MULT 0x5DEECE66DLL
#define RND_ADD  0xBLL
#define RND_MASK ((1LL << 48) - 1)

static nj_field *rnd_fseed(nj_vm *vm)  { return fld(vm, RND_CLS, "seed", "J"); }
static nj_field *rnd_fhave(nj_vm *vm)  { return fld(vm, RND_CLS, "haveNextNextGaussian", "Z"); }
static nj_field *rnd_fgauss(nj_vm *vm) { return fld(vm, RND_CLS, "nextNextGaussian", "J"); }

static nj_s8 dbits(double d)
{
    nj_s8 v;
    memcpy(&v, &d, sizeof(v));
    return v;
}

static double sbits(nj_s8 v)
{
    double d;
    memcpy(&d, &v, sizeof(d));
    return d;
}

static nj_s8 rnd_next_seed(nj_vm *vm, nj_ref r, int bits)
{
    nj_field *f = rnd_fseed(vm);
    nj_s8 seed = f ? nj_field_read_long(vm, r, f) : 0;

    seed = (seed * RND_MULT + RND_ADD) & RND_MASK;
    if (f) {
        nj_field_write_long(vm, r, f, seed);
    }
    return (nj_s8)((nj_u8)seed >> (48 - bits));
}

NJFN(rnd_setSeed)
{
    nj_ref r = NJ_THIS(args);
    nj_field *f = rnd_fseed(vm);
    nj_field *fh = rnd_fhave(vm);
    nj_s8 s = NJ_ARG_LONG(args, 0);

    if (!f) {
        return nj_throw(vm, "java/lang/InternalError", "Random layout");
    }
    nj_field_write_long(vm, r, f, (s ^ RND_MULT) & RND_MASK);
    if (fh) {
        nj_field_write_int(vm, r, fh, 0);
    }
    return NJ_OK;
}

NJFN(rnd_init)
{
    /* new Random() seeds from the wall clock, like a device with no RTC has to. */
    nj_value a[1];

    a[0].j = nj_now_ms(vm);
    return rnd_setSeed(vm, th, a, ret);
}

NJFN(rnd_init_seed)
{
    return rnd_setSeed(vm, th, args, ret);
}

NJFN(rnd_next)
{
    nj_s4 bits = NJ_ARG_INT(args, 0);

    if (bits <= 0 || bits > 32) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", "bits");
    }
    ret->i = (nj_s4)rnd_next_seed(vm, NJ_THIS(args), (int)bits);
    return NJ_OK;
}

NJFN(rnd_nextInt)
{
    ret->i = (nj_s4)rnd_next_seed(vm, NJ_THIS(args), 32);
    return NJ_OK;
}

NJFN(rnd_nextIntN)
{
    nj_ref r = NJ_THIS(args);
    nj_s4 n = NJ_ARG_INT(args, 0);
    nj_s4 bits, val;

    if (n <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "n must be positive");
    }
    if ((n & -n) == n) {   /* n is a power of two */
        ret->i = (nj_s4)(((nj_s8)n * (nj_s8)rnd_next_seed(vm, r, 31)) >> 31);
        return NJ_OK;
    }
    do {
        bits = (nj_s4)rnd_next_seed(vm, r, 31);
        val = bits % n;
    } while (bits - val + (n - 1) < 0);
    ret->i = val;
    return NJ_OK;
}

NJFN(rnd_nextLong)
{
    nj_ref r = NJ_THIS(args);
    nj_s8 hi = (nj_s8)rnd_next_seed(vm, r, 32);
    nj_s8 lo = (nj_s8)rnd_next_seed(vm, r, 32);

    ret->j = (hi << 32) + lo;
    return NJ_OK;
}

NJFN(rnd_nextBoolean)
{
    ret->i = (rnd_next_seed(vm, NJ_THIS(args), 1) != 0) ? 1 : 0;
    return NJ_OK;
}

NJFN(rnd_nextFloat)
{
    nj_s8 v = rnd_next_seed(vm, NJ_THIS(args), 24);

    ret->f = (float)((double)v / (double)(1 << 24));
    return NJ_OK;
}

NJFN(rnd_nextDouble)
{
    nj_ref r = NJ_THIS(args);
    nj_s8 hi = (nj_s8)rnd_next_seed(vm, r, 26);
    nj_s8 lo = (nj_s8)rnd_next_seed(vm, r, 27);

    ret->d = (double)((hi << 27) + lo) / (double)(1LL << 53);
    return NJ_OK;
}

NJFN(rnd_nextGaussian)
{
    nj_ref r = NJ_THIS(args);
    nj_field *fh = rnd_fhave(vm);
    nj_field *fg = rnd_fgauss(vm);

    if (fh && nj_field_read_int(vm, r, fh) != 0) {
        nj_field_write_int(vm, r, fh, 0);
        ret->d = fg ? sbits(nj_field_read_long(vm, r, fg)) : 0.0;
        return NJ_OK;
    }
    for (;;) {
        double v1 = 2.0 * (double)rnd_next_seed(vm, r, 53) /
                    (double)(1LL << 53) - 1.0;
        double v2 = 2.0 * (double)rnd_next_seed(vm, r, 53) /
                    (double)(1LL << 53) - 1.0;
        double s = v1 * v1 + v2 * v2;
        if (s >= 1.0 || s == 0.0) {
            continue;
        }
        s = sqrt(-2.0 * log(s) / s);
        ret->d = v1 * s;
        if (fg && fh) {
            nj_field_write_long(vm, r, fg, dbits(v2 * s));
            nj_field_write_int(vm, r, fh, 1);
        }
        return NJ_OK;
    }
}

/* ------------------------------------------------------------------ */
/* java.util.Date                                                      */
/* ------------------------------------------------------------------ */
/* Days <-> civil date: Howard Hinnant's algorithm (proleptic Gregorian). */
static nj_s8 days_from_civil(nj_s8 y, nj_s8 m, nj_s8 d)
{
    nj_s8 era, yoe, doy, doe;

    y -= (m <= 2) ? 1 : 0;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(nj_s8 z, nj_s8 *py, nj_s8 *pm, nj_s8 *pd)
{
    nj_s8 era, doe, yoe, y, doy, mp, d, m;

    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2) ? 1 : 0;
    *py = y;
    *pm = m;
    *pd = d;
}

/* Split a millisecond count (already shifted into local time) into fields. */
static void split_millis(nj_s8 ms, nj_s8 *y, nj_s8 *mo, nj_s8 *d,
                         nj_s8 *h, nj_s8 *mi, nj_s8 *s, nj_s8 *msec)
{
    nj_s8 days = ms / 86400000;
    nj_s8 rem = ms % 86400000;

    if (rem < 0) {
        rem += 86400000;
        days -= 1;
    }
    civil_from_days(days, y, mo, d);
    *h = rem / 3600000;
    rem %= 3600000;
    *mi = rem / 60000;
    rem %= 60000;
    *s = rem / 1000;
    *msec = rem % 1000;
}

static nj_s8 join_millis(nj_s8 y, nj_s8 mo, nj_s8 d, nj_s8 h, nj_s8 mi,
                         nj_s8 s, nj_s8 msec)
{
    return days_from_civil(y, mo, d) * 86400000 + h * 3600000 + mi * 60000 +
           s * 1000 + msec;
}

/* 0 = Sunday, matching Date.getDay()/Calendar.SUNDAY. */
static int weekday_of(nj_s8 ms)
{
    nj_s8 days = ms / 86400000;
    if (ms % 86400000 < 0) {
        days -= 1;
    }
    return (int)(((days + 4) % 7 + 7) % 7);
}

static const char *const nj_dow_names[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
static const char *const nj_mon_names[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

#define DATE_CLS "java/util/Date"

static nj_s8 date_millis(nj_vm *vm, nj_ref d)
{
    nj_field *f = fld(vm, DATE_CLS, "millis", "J");
    return f ? nj_field_read_long(vm, d, f) : 0;
}

NJFN(date_init)
{
    return NJ_OK;   /* millis is zero-initialised by the allocator */
}

NJFN(date_init_ms)
{
    nj_field *f = fld(vm, DATE_CLS, "millis", "J");

    if (!f) {
        return nj_throw(vm, "java/lang/InternalError", "Date layout");
    }
    nj_field_write_long(vm, NJ_THIS(args), f, NJ_ARG_LONG(args, 0));
    return NJ_OK;
}

NJFN(date_getTime)
{
    ret->j = date_millis(vm, NJ_THIS(args));
    return NJ_OK;
}

NJFN(date_setTime)
{
    nj_field *f = fld(vm, DATE_CLS, "millis", "J");

    if (!f) {
        return nj_throw(vm, "java/lang/InternalError", "Date layout");
    }
    nj_field_write_long(vm, NJ_THIS(args), f, NJ_ARG_LONG(args, 0));
    return NJ_OK;
}

NJFN(date_equals)
{
    nj_ref a = NJ_THIS(args);
    nj_ref b = NJ_ARG_REF(args, 0);

    if (!a || !b) {
        ret->i = 0;
        return NJ_OK;
    }
    if (b->cls && a->cls && !nj_class_is_subclass_of(b->cls, a->cls)) {
        ret->i = 0;
        return NJ_OK;
    }
    ret->i = (date_millis(vm, a) == date_millis(vm, b)) ? 1 : 0;
    return NJ_OK;
}

NJFN(date_hashCode)
{
    nj_s8 ms = date_millis(vm, NJ_THIS(args));

    ret->i = (nj_s4)(ms ^ (ms >> 32));
    return NJ_OK;
}

NJFN(date_compareTo)
{
    nj_ref a = NJ_THIS(args);
    nj_ref b = NJ_ARG_REF(args, 0);
    nj_s8 x, y;

    if (!b) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    x = date_millis(vm, a);
    y = date_millis(vm, b);
    ret->i = (x < y) ? -1 : ((x == y) ? 0 : 1);
    return NJ_OK;
}

NJFN(date_before)
{
    nj_ref b = NJ_ARG_REF(args, 0);

    if (!b) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    ret->i = (date_millis(vm, NJ_THIS(args)) < date_millis(vm, b)) ? 1 : 0;
    return NJ_OK;
}

NJFN(date_after)
{
    nj_ref b = NJ_ARG_REF(args, 0);

    if (!b) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    ret->i = (date_millis(vm, NJ_THIS(args)) > date_millis(vm, b)) ? 1 : 0;
    return NJ_OK;
}

NJFN(date_toString)
{
    nj_s8 ms = date_millis(vm, NJ_THIS(args));
    nj_s8 y, mo, d, h, mi, s, msec;
    char buf[64];
    int len = 0;

    split_millis(ms, &y, &mo, &d, &h, &mi, &s, &msec);
    buf[0] = '\0';
    put_str(buf, (int)sizeof(buf), &len, nj_dow_names[weekday_of(ms)]);
    put_str(buf, (int)sizeof(buf), &len, " ");
    put_str(buf, (int)sizeof(buf), &len, nj_mon_names[mo - 1]);
    put_str(buf, (int)sizeof(buf), &len, " ");
    if (d < 10) {
        put_str(buf, (int)sizeof(buf), &len, "0");
    }
    put_int(buf, (int)sizeof(buf), &len, d);
    put_str(buf, (int)sizeof(buf), &len, " ");
    if (h < 10) {
        put_str(buf, (int)sizeof(buf), &len, "0");
    }
    put_int(buf, (int)sizeof(buf), &len, h);
    put_str(buf, (int)sizeof(buf), &len, ":");
    if (mi < 10) {
        put_str(buf, (int)sizeof(buf), &len, "0");
    }
    put_int(buf, (int)sizeof(buf), &len, mi);
    put_str(buf, (int)sizeof(buf), &len, ":");
    if (s < 10) {
        put_str(buf, (int)sizeof(buf), &len, "0");
    }
    put_int(buf, (int)sizeof(buf), &len, s);
    put_str(buf, (int)sizeof(buf), &len, " UTC ");
    put_int(buf, (int)sizeof(buf), &len, y);
    ret->r = nj_string_new(vm, buf);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

/* The deprecated accessors old MIDlets still use for date display. */
static void date_split(nj_vm *vm, nj_ref d, nj_s8 *y, nj_s8 *mo, nj_s8 *dd,
                        nj_s8 *h, nj_s8 *mi, nj_s8 *s, nj_s8 *msec)
{
    split_millis(date_millis(vm, d), y, mo, dd, h, mi, s, msec);
}

NJFN(date_getYear)
{
    nj_s8 y, mo, d, h, mi, s, msec;
    date_split(vm, NJ_THIS(args), &y, &mo, &d, &h, &mi, &s, &msec);
    ret->i = (nj_s4)(y - 1900);
    return NJ_OK;
}

NJFN(date_getMonth)
{
    nj_s8 y, mo, d, h, mi, s, msec;
    date_split(vm, NJ_THIS(args), &y, &mo, &d, &h, &mi, &s, &msec);
    ret->i = (nj_s4)(mo - 1);
    return NJ_OK;
}

NJFN(date_getDate)
{
    nj_s8 y, mo, d, h, mi, s, msec;
    date_split(vm, NJ_THIS(args), &y, &mo, &d, &h, &mi, &s, &msec);
    ret->i = (nj_s4)d;
    return NJ_OK;
}

NJFN(date_getDay)
{
    ret->i = (nj_s4)weekday_of(date_millis(vm, NJ_THIS(args)));
    return NJ_OK;
}

NJFN(date_getHours)
{
    nj_s8 y, mo, d, h, mi, s, msec;
    date_split(vm, NJ_THIS(args), &y, &mo, &d, &h, &mi, &s, &msec);
    ret->i = (nj_s4)h;
    return NJ_OK;
}

NJFN(date_getMinutes)
{
    nj_s8 y, mo, d, h, mi, s, msec;
    date_split(vm, NJ_THIS(args), &y, &mo, &d, &h, &mi, &s, &msec);
    ret->i = (nj_s4)mi;
    return NJ_OK;
}

NJFN(date_getSeconds)
{
    nj_s8 y, mo, d, h, mi, s, msec;
    date_split(vm, NJ_THIS(args), &y, &mo, &d, &h, &mi, &s, &msec);
    ret->i = (nj_s4)s;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* java.util.TimeZone                                                  */
/* ------------------------------------------------------------------ */
/*
 * UTC only, with the fixed offsets of the "GMT+hh:mm" notation recognised.
 * The default zone comes from the port's wall clock when it provides one; on a
 * port with no `now` the runtime reports UTC, which is what a device without a
 * configured RTC should say.  getOffset() always answers with the raw offset
 * because daylight saving is never modelled.
 */
#define TZ_CLS "java/util/TimeZone"

static nj_field *tz_fid(nj_vm *vm)  { return fld(vm, TZ_CLS, "tzID", "Ljava/lang/String;"); }
static nj_field *tz_foff(nj_vm *vm) { return fld(vm, TZ_CLS, "tzRawOffset", "I"); }
static nj_field *tz_fdst(nj_vm *vm) { return fld(vm, TZ_CLS, "tzUseDaylight", "Z"); }

static nj_s4 tz_port_offset(nj_vm *vm)
{
    nj_datetime dt;

    if (vm && vm->port && vm->port->now) {
        dt.year = 1970; dt.month = 0; dt.day = 1;
        dt.hour = 0; dt.minute = 0; dt.second = 0; dt.millis = 0;
        dt.weekday = 4; dt.tz_offset = 0;
        if (vm->port->now(vm->port->ud, &dt)) {
            return dt.tz_offset;
        }
    }
    return 0;
}

/* Create a TimeZone object `minutes` east of UTC. */
static nj_ref tz_make(nj_vm *vm, nj_s4 minutes)
{
    nj_ref z;
    char id[24];
    int len = 0;

    id[0] = '\0';
    put_str(id, (int)sizeof(id), &len, "GMT");
    if (minutes != 0) {
        nj_s4 m = minutes;
        if (m < 0) {
            put_str(id, (int)sizeof(id), &len, "-");
            m = -m;
        } else {
            put_str(id, (int)sizeof(id), &len, "+");
        }
        if (m / 60 < 10) {
            put_str(id, (int)sizeof(id), &len, "0");
        }
        put_int(id, (int)sizeof(id), &len, m / 60);
        put_str(id, (int)sizeof(id), &len, ":");
        if (m % 60 < 10) {
            put_str(id, (int)sizeof(id), &len, "0");
        }
        put_int(id, (int)sizeof(id), &len, m % 60);
    }
    z = nj_object_new(vm, TZ_CLS);
    if (!z) {
        return NULL;
    }
    if (tz_fid(vm)) {
        nj_ref s = nj_string_new(vm, id);
        if (!s) {
            return NULL;
        }
        nj_field_write_ref(vm, z, tz_fid(vm), s);
    }
    if (tz_foff(vm)) {
        nj_field_write_int(vm, z, tz_foff(vm), minutes * 60000);
    }
    if (tz_fdst(vm)) {
        nj_field_write_int(vm, z, tz_fdst(vm), 0);
    }
    return z;
}

NJFN(tz_init)
{
    nj_ref z = NJ_THIS(args);

    if (tz_foff(vm)) {
        nj_field_write_int(vm, z, tz_foff(vm), 0);
    }
    if (tz_fdst(vm)) {
        nj_field_write_int(vm, z, tz_fdst(vm), 0);
    }
    if (tz_fid(vm)) {
        nj_ref s = nj_string_new(vm, "GMT");
        if (!s) {
            return nj_throw_oom(vm);
        }
        nj_field_write_ref(vm, z, tz_fid(vm), s);
    }
    return NJ_OK;
}

NJFN(tz_getDefault)
{
    ret->r = tz_make(vm, tz_port_offset(vm));
    return ret->r ? NJ_OK : NJ_ERR;
}

/* "GMT+05:30", "GMT-8", "UTC", "GMT" -> minutes east of UTC. */
static nj_s4 tz_parse_offset(const char *id, int *ok)
{
    int i = 0;
    int neg = 0;
    nj_s4 mins = 0;
    int have = 0;

    *ok = 1;
    if (!id || !*id) {
        *ok = 0;
        return 0;
    }
    if ((id[0] == 'G' || id[0] == 'g') && (id[1] == 'M' || id[1] == 'm') &&
        (id[2] == 'T' || id[2] == 't')) {
        i = 3;
    } else if ((id[0] == 'U' || id[0] == 'u') && (id[1] == 'T' || id[1] == 't') &&
               (id[2] == 'C' || id[2] == 'c')) {
        return 0;
    } else {
        *ok = 0;
        return 0;
    }
    if (id[i] == '\0') {
        return 0;
    }
    if (id[i] == '+') {
        i++;
    } else if (id[i] == '-') {
        neg = 1;
        i++;
    } else {
        *ok = 0;
        return 0;
    }
    while (id[i] >= '0' && id[i] <= '9') {
        mins = mins * 10 + (id[i] - '0');
        i++;
        have = 1;
    }
    if (!have) {
        *ok = 0;
        return 0;
    }
    mins *= 60;
    if (id[i] == ':') {
        nj_s4 m = 0;
        int hm = 0;
        i++;
        while (id[i] >= '0' && id[i] <= '9') {
            m = m * 10 + (id[i] - '0');
            i++;
            hm = 1;
        }
        if (!hm) {
            *ok = 0;
            return 0;
        }
        mins += m;
    }
    if (id[i] != '\0') {
        *ok = 0;
        return 0;
    }
    return neg ? -mins : mins;
}

NJFN(tz_getTimeZone)
{
    nj_ref name = NJ_ARG_REF(args, 0);
    char id[64];
    int ok = 0;
    nj_s4 mins;

    if (!name) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    id[0] = '\0';
    (void)nj_string_to_utf8(name, id, (int)sizeof(id));
    mins = tz_parse_offset(id, &ok);
    if (!ok) {
        mins = 0;   /* unknown ID: the GMT zone, exactly like java.util */
    }
    ret->r = tz_make(vm, mins);
    return ret->r ? NJ_OK : NJ_ERR;
}

NJFN(tz_getAvailableIDs)
{
    nj_ref a = nj_object_array_new(vm, "java/lang/String", 2u);
    nj_ref gmt, utc;

    if (!a) {
        return nj_throw_oom(vm);
    }
    gmt = nj_string_new(vm, "GMT");
    utc = nj_string_new(vm, "UTC");
    if (!gmt || !utc) {
        return nj_throw_oom(vm);
    }
    nj_array_set_ref(a, 0u, gmt);
    nj_array_set_ref(a, 1u, utc);
    ret->r = a;
    return NJ_OK;
}

NJFN(tz_getAvailableIDs_offset)
{
    nj_s4 want = NJ_ARG_INT(args, 0);
    nj_ref a = nj_object_array_new(vm, "java/lang/String",
                                   (want == 0) ? 2u : 0u);
    nj_ref s;

    if (!a) {
        return nj_throw_oom(vm);
    }
    if (want != 0) {
        ret->r = a;
        return NJ_OK;
    }
    s = nj_string_new(vm, "GMT");
    if (!s) {
        return nj_throw_oom(vm);
    }
    nj_array_set_ref(a, 0u, s);
    s = nj_string_new(vm, "UTC");
    if (!s) {
        return nj_throw_oom(vm);
    }
    nj_array_set_ref(a, 1u, s);
    ret->r = a;
    return NJ_OK;
}

NJFN(tz_getOffset)
{
    nj_ref z = NJ_THIS(args);
    nj_field *f = tz_foff(vm);

    ret->i = f ? nj_field_read_int(vm, z, f) : 0;
    return NJ_OK;
}

NJFN(tz_getRawOffset)
{
    return tz_getOffset(vm, th, args, ret);
}

NJFN(tz_setRawOffset)
{
    nj_field *f = tz_foff(vm);

    if (f) {
        nj_field_write_int(vm, NJ_THIS(args), f, NJ_ARG_INT(args, 0));
    }
    return NJ_OK;
}

NJFN(tz_getID)
{
    nj_ref z = NJ_THIS(args);
    nj_field *f = tz_fid(vm);

    ret->r = f ? nj_field_read_ref(vm, z, f) : NULL;
    if (!ret->r) {
        ret->r = nj_string_new(vm, "GMT");
    }
    return NJ_OK;
}

NJFN(tz_useDaylightTime)
{
    nj_ref z = NJ_THIS(args);
    nj_field *f = tz_fdst(vm);

    ret->i = f ? nj_field_read_int(vm, z, f) : 0;
    return NJ_OK;
}

NJFN(tz_hasSameRules)
{
    nj_ref a = NJ_THIS(args);
    nj_ref b = NJ_ARG_REF(args, 0);
    nj_field *f = tz_foff(vm);

    if (!b) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    ret->i = (f && nj_field_read_int(vm, a, f) == nj_field_read_int(vm, b, f))
             ? 1 : 0;
    return NJ_OK;
}

NJFN(tz_toString)
{
    nj_ref z = NJ_THIS(args);
    nj_field *fid = tz_fid(vm);
    nj_field *foff = tz_foff(vm);
    char buf[64];
    char tmp[48];
    int len = 0;

    buf[0] = '\0';
    tmp[0] = '\0';
    if (fid) {
        nj_ref s = nj_field_read_ref(vm, z, fid);
        if (s) {
            (void)nj_string_to_utf8(s, tmp, (int)sizeof(tmp));
        }
    }
    put_str(buf, (int)sizeof(buf), &len, tmp);
    if (foff) {
        put_str(buf, (int)sizeof(buf), &len, "[");
        put_int(buf, (int)sizeof(buf), &len,
                nj_field_read_int(vm, z, foff) / 60000);
        put_str(buf, (int)sizeof(buf), &len, " min]");
    }
    ret->r = nj_string_new(vm, buf);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

/* ------------------------------------------------------------------ */
/* java.util.Calendar                                                  */
/* ------------------------------------------------------------------ */
/*
 * A Calendar is (millis, TimeZone): get() decodes the fields from the
 * millisecond value and set() re-encodes them, which behaves identically to
 * the field-caching model for every non-lenient use a MIDlet has.  The
 * calendar maths is done here rather than through the port so a MIDlet sees
 * the same dates on a device with no RTC and on the host.
 */
#define CAL_CLS "java/util/Calendar"

#define NJ_CAL_ERA        0
#define NJ_CAL_YEAR       1
#define NJ_CAL_MONTH      2
#define NJ_CAL_WEEK_YEAR  3
#define NJ_CAL_WEEK_MONTH 4
#define NJ_CAL_DATE       5
#define NJ_CAL_DAY_YEAR   6
#define NJ_CAL_DAY_WEEK   7
#define NJ_CAL_AMPM       9
#define NJ_CAL_HOUR       10
#define NJ_CAL_HOUR_DAY   11
#define NJ_CAL_MINUTE     12
#define NJ_CAL_SECOND     13
#define NJ_CAL_MILLIS     14
#define NJ_CAL_ZONE_OFF   15
#define NJ_CAL_DST_OFF    16

static nj_field *cal_fmillis(nj_vm *vm) { return fld(vm, CAL_CLS, "calMillis", "J"); }
static nj_field *cal_fzone(nj_vm *vm)   { return fld(vm, CAL_CLS, "calZone", "Ljava/util/TimeZone;"); }

static nj_s4 cal_offset(nj_vm *vm, nj_ref cal)
{
    nj_ref z = rget(vm, cal, cal_fzone(vm));
    nj_field *f = tz_foff(vm);

    if (!z || !f) {
        return 0;
    }
    return nj_field_read_int(vm, z, f);
}

static nj_s8 cal_millis(nj_vm *vm, nj_ref cal)
{
    nj_field *f = cal_fmillis(vm);
    return f ? nj_field_read_long(vm, cal, f) : 0;
}

static void cal_set_millis(nj_vm *vm, nj_ref cal, nj_s8 ms)
{
    nj_field *f = cal_fmillis(vm);
    if (f) {
        nj_field_write_long(vm, cal, f, ms);
    }
}

static void cal_to_fields(nj_vm *vm, nj_ref cal, nj_s8 *y, nj_s8 *mo,
                          nj_s8 *d, nj_s8 *h, nj_s8 *mi, nj_s8 *s, nj_s8 *ms)
{
    split_millis(cal_millis(vm, cal) + (nj_s8)cal_offset(vm, cal),
                 y, mo, d, h, mi, s, ms);
}

static nj_s8 cal_month_len(nj_s8 y, nj_s8 mo)
{
    static const nj_s8 mlen[12] = { 31, 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31 };
    if (mo < 1) {
        mo = 1;
    }
    if (mo > 12) {
        mo = 12;
    }
    if (mo == 2) {
        int leap = ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
        return leap ? 29 : 28;
    }
    return mlen[mo - 1];
}

/* Normalise a possibly out-of-range (year, month) pair. */
static void cal_norm_month(nj_s8 *y, nj_s8 *mo)
{
    nj_s8 delta = *mo - 1;

    *y += delta / 12;
    *mo = delta % 12;
    if (*mo < 0) {
        *mo += 12;
        *y -= 1;
    }
    *mo += 1;
}

/* Write the public constants once per VM (javac inlines them into an app, so
 * these slots only matter for reflection-style access). */
static void cal_constants(nj_vm *vm)
{
    static const struct { const char *name; nj_s4 value; } c[] = {
        { "ERA", 0 }, { "YEAR", 1 }, { "MONTH", 2 }, { "WEEK_OF_YEAR", 3 },
        { "WEEK_OF_MONTH", 4 }, { "DATE", 5 }, { "DAY_OF_MONTH", 5 },
        { "DAY_OF_YEAR", 6 }, { "DAY_OF_WEEK", 7 },
        { "DAY_OF_WEEK_IN_MONTH", 8 }, { "AM_PM", 9 }, { "HOUR", 10 },
        { "HOUR_OF_DAY", 11 }, { "MINUTE", 12 }, { "SECOND", 13 },
        { "MILLISECOND", 14 }, { "ZONE_OFFSET", 15 }, { "DST_OFFSET", 16 },
        { "SUNDAY", 1 }, { "MONDAY", 2 }, { "TUESDAY", 3 }, { "WEDNESDAY", 4 },
        { "THURSDAY", 5 }, { "FRIDAY", 6 }, { "SATURDAY", 7 },
        { "JANUARY", 0 }, { "FEBRUARY", 1 }, { "MARCH", 2 }, { "APRIL", 3 },
        { "MAY", 4 }, { "JUNE", 5 }, { "JULY", 6 }, { "AUGUST", 7 },
        { "SEPTEMBER", 8 }, { "OCTOBER", 9 }, { "NOVEMBER", 10 },
        { "DECEMBER", 11 }, { "AM", 0 }, { "PM", 1 }
    };
    nj_field *flag = sfld(vm, CAL_CLS, "calStatics", "Z");
    unsigned i;

    if (!flag || nj_field_read_int(vm, NULL, flag) != 0) {
        return;
    }
    for (i = 0u; i < sizeof(c) / sizeof(c[0]); i++) {
        nj_field *f = sfld(vm, CAL_CLS, c[i].name, "I");
        if (f) {
            nj_field_write_int(vm, NULL, f, c[i].value);
        }
    }
    nj_field_write_int(vm, NULL, flag, 1);
}

NJFN(cal_init)
{
    nj_ref c = NJ_THIS(args);

    cal_constants(vm);
    cal_set_millis(vm, c, 0);
    if (cal_fzone(vm)) {
        nj_ref z = tz_make(vm, tz_port_offset(vm));
        if (!z) {
            return NJ_ERR;
        }
        nj_field_write_ref(vm, c, cal_fzone(vm), z);
    }
    return NJ_OK;
}

NJFN(cal_init_zone)
{
    nj_ref c = NJ_THIS(args);
    nj_ref z = NJ_ARG_REF(args, 0);

    cal_constants(vm);
    cal_set_millis(vm, c, 0);
    if (!z) {
        z = tz_make(vm, 0);
        if (!z) {
            return NJ_ERR;
        }
    }
    if (cal_fzone(vm)) {
        nj_field_write_ref(vm, c, cal_fzone(vm), z);
    }
    return NJ_OK;
}

static nj_status cal_instance(nj_vm *vm, nj_ref zone, nj_value *ret)
{
    nj_ref c = nj_object_new(vm, CAL_CLS);
    nj_value a[1];

    if (!c) {
        return NJ_ERR;
    }
    a[0].r = zone;
    if (nj_call_special(vm, c, CAL_CLS, "<init>",
                        zone ? "(Ljava/util/TimeZone;)V" : "()V",
                        a, NULL) != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = c;
    return NJ_OK;
}

NJFN(cal_getInstance)
{
    cal_constants(vm);
    return cal_instance(vm, NULL, ret);
}

NJFN(cal_getInstance_zone)
{
    cal_constants(vm);
    return cal_instance(vm, NJ_ARG_REF(args, 0), ret);
}

NJFN(cal_get)
{
    nj_ref c = NJ_THIS(args);
    nj_s4 field = NJ_ARG_INT(args, 0);
    nj_s8 y, mo, d, h, mi, s, ms;

    cal_to_fields(vm, c, &y, &mo, &d, &h, &mi, &s, &ms);
    switch (field) {
    case NJ_CAL_ERA:       ret->i = (y > 0) ? 1 : 0; break;
    case NJ_CAL_YEAR:      ret->i = (nj_s4)y; break;
    case NJ_CAL_MONTH:     ret->i = (nj_s4)(mo - 1); break;
    case NJ_CAL_DATE:      ret->i = (nj_s4)d; break;
    case NJ_CAL_DAY_YEAR:  ret->i = (nj_s4)(days_from_civil(y, mo, d) -
                                            days_from_civil(y, 1, 1) + 1); break;
    case NJ_CAL_DAY_WEEK:  ret->i = (nj_s4)(((days_from_civil(y, mo, d) + 4) % 7
                                             + 7) % 7 + 1); break;
    case NJ_CAL_WEEK_YEAR: ret->i = (nj_s4)((days_from_civil(y, mo, d) -
                                             days_from_civil(y, 1, 1)) / 7 + 1);
                           break;
    case NJ_CAL_WEEK_MONTH: ret->i = (nj_s4)((d - 1) / 7 + 1); break;
    case NJ_CAL_AMPM:      ret->i = (h < 12) ? 0 : 1; break;
    case NJ_CAL_HOUR:      ret->i = (nj_s4)(h % 12); break;
    case NJ_CAL_HOUR_DAY:  ret->i = (nj_s4)h; break;
    case NJ_CAL_MINUTE:    ret->i = (nj_s4)mi; break;
    case NJ_CAL_SECOND:    ret->i = (nj_s4)s; break;
    case NJ_CAL_MILLIS:    ret->i = (nj_s4)ms; break;
    case NJ_CAL_ZONE_OFF:  ret->i = cal_offset(vm, c); break;
    case NJ_CAL_DST_OFF:   ret->i = 0; break;
    default:
        return nj_throw(vm, "java/lang/IllegalArgumentException", "field");
    }
    return NJ_OK;
}

NJFN(cal_set)
{
    nj_ref c = NJ_THIS(args);
    nj_s4 field = NJ_ARG_INT(args, 0);
    nj_s4 value = NJ_ARG_INT(args, 1);
    nj_s8 y, mo, d, h, mi, s, ms;
    int recompute = 1;

    cal_constants(vm);
    cal_to_fields(vm, c, &y, &mo, &d, &h, &mi, &s, &ms);
    switch (field) {
    case NJ_CAL_ERA:      if (value <= 0) { y = 1 - y; } break;
    case NJ_CAL_YEAR:     y = value; break;
    case NJ_CAL_MONTH:    mo = (nj_s8)value + 1; break;
    case NJ_CAL_DATE:     d = value; break;
    case NJ_CAL_DAY_YEAR: mo = 1; d = value; break;
    case NJ_CAL_AMPM:     h = (value == 0) ? (h % 12) : (h % 12) + 12; break;
    case NJ_CAL_HOUR:     h = (h >= 12) ? ((nj_s8)value % 12) + 12
                                        : ((nj_s8)value % 12); break;
    case NJ_CAL_HOUR_DAY: h = value; break;
    case NJ_CAL_MINUTE:   mi = value; break;
    case NJ_CAL_SECOND:   s = value; break;
    case NJ_CAL_MILLIS:   ms = value; break;
    case NJ_CAL_DAY_WEEK:
    case NJ_CAL_ZONE_OFF:
    case NJ_CAL_DST_OFF:
        return NJ_OK;                     /* derived: nothing to store */
    default:
        recompute = 0;
        break;
    }
    if (recompute) {
        cal_norm_month(&y, &mo);
        {
            nj_s8 ml = cal_month_len(y, mo);
            if (d < 1) {
                d = 1;
            }
            if (d > ml) {
                d = ml;
            }
        }
        cal_set_millis(vm, c, join_millis(y, mo, d, h, mi, s, ms) -
                              (nj_s8)cal_offset(vm, c));
    }
    return NJ_OK;
}

NJFN(cal_clear)
{
    nj_ref c = NJ_THIS(args);

    cal_constants(vm);
    /* All fields undefined, which reads back as the epoch in this zone. */
    cal_set_millis(vm, c, -(nj_s8)cal_offset(vm, c));
    return NJ_OK;
}

NJFN(cal_clear_field)
{
    nj_ref c = NJ_THIS(args);
    nj_s4 field = NJ_ARG_INT(args, 0);
    nj_s8 y, mo, d, h, mi, s, ms;

    cal_to_fields(vm, c, &y, &mo, &d, &h, &mi, &s, &ms);
    switch (field) {
    case NJ_CAL_YEAR:     y = 1970; break;
    case NJ_CAL_MONTH:    mo = 1; break;
    case NJ_CAL_DATE:     d = 1; break;
    case NJ_CAL_DAY_YEAR: mo = 1; d = 1; break;
    case NJ_CAL_HOUR_DAY: h = 0; break;
    case NJ_CAL_HOUR:     h = h - (h % 12); break;
    case NJ_CAL_AMPM:     h = h % 12; break;
    case NJ_CAL_MINUTE:   mi = 0; break;
    case NJ_CAL_SECOND:   s = 0; break;
    case NJ_CAL_MILLIS:   ms = 0; break;
    default:
        return NJ_OK;
    }
    cal_set_millis(vm, c, join_millis(y, mo, d, h, mi, s, ms) -
                          (nj_s8)cal_offset(vm, c));
    return NJ_OK;
}

NJFN(cal_add)
{
    nj_ref c = NJ_THIS(args);
    nj_s4 field = NJ_ARG_INT(args, 0);
    nj_s4 amount = NJ_ARG_INT(args, 1);
    nj_s8 y, mo, d, h, mi, s, ms;
    nj_s8 nms;

    cal_to_fields(vm, c, &y, &mo, &d, &h, &mi, &s, &ms);
    switch (field) {
    case NJ_CAL_YEAR:
        y += amount;
        nms = join_millis(y, mo, (d > cal_month_len(y, mo)) ? cal_month_len(y, mo) : d,
                          h, mi, s, ms);
        break;
    case NJ_CAL_MONTH:
        mo += amount;
        cal_norm_month(&y, &mo);
        nms = join_millis(y, mo, (d > cal_month_len(y, mo)) ? cal_month_len(y, mo) : d,
                          h, mi, s, ms);
        break;
    case NJ_CAL_DATE:
    case NJ_CAL_DAY_YEAR:
    case NJ_CAL_DAY_WEEK:
        nms = cal_millis(vm, c) + (nj_s8)amount * 86400000;
        break;
    case NJ_CAL_HOUR_DAY:
    case NJ_CAL_HOUR:
        nms = cal_millis(vm, c) + (nj_s8)amount * 3600000;
        break;
    case NJ_CAL_MINUTE:
        nms = cal_millis(vm, c) + (nj_s8)amount * 60000;
        break;
    case NJ_CAL_SECOND:
        nms = cal_millis(vm, c) + (nj_s8)amount * 1000;
        break;
    case NJ_CAL_MILLIS:
        nms = cal_millis(vm, c) + amount;
        break;
    default:
        return NJ_OK;
    }
    cal_set_millis(vm, c, nms);
    return NJ_OK;
}

NJFN(cal_getTimeInMillis)
{
    cal_constants(vm);
    ret->j = cal_millis(vm, NJ_THIS(args));
    return NJ_OK;
}

NJFN(cal_setTimeInMillis)
{
    cal_set_millis(vm, NJ_THIS(args), NJ_ARG_LONG(args, 0));
    return NJ_OK;
}

NJFN(cal_getTime)
{
    nj_ref d = nj_object_new(vm, "java/util/Date");
    nj_field *f = fld(vm, DATE_CLS, "millis", "J");

    if (!d) {
        return NJ_ERR;
    }
    if (!f) {
        return nj_throw(vm, "java/lang/InternalError", "Date layout");
    }
    nj_field_write_long(vm, d, f, cal_millis(vm, NJ_THIS(args)));
    ret->r = d;
    return NJ_OK;
}

NJFN(cal_setTime)
{
    nj_ref date = NJ_ARG_REF(args, 0);
    nj_field *f = fld(vm, DATE_CLS, "millis", "J");

    if (!date) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    cal_set_millis(vm, NJ_THIS(args), nj_field_read_long(vm, date, f));
    return NJ_OK;
}

NJFN(cal_getTimeZone)
{
    nj_ref c = NJ_THIS(args);
    nj_ref z = rget(vm, c, cal_fzone(vm));

    if (!z) {
        z = tz_make(vm, 0);
        if (!z) {
            return NJ_ERR;
        }
        if (cal_fzone(vm)) {
            nj_field_write_ref(vm, c, cal_fzone(vm), z);
        }
    }
    ret->r = z;
    return NJ_OK;
}

NJFN(cal_setTimeZone)
{
    nj_ref z = NJ_ARG_REF(args, 0);

    cal_constants(vm);
    if (!z) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (cal_fzone(vm)) {
        nj_field_write_ref(vm, NJ_THIS(args), cal_fzone(vm), z);
    }
    return NJ_OK;
}

NJFN(cal_isLenient)
{
    ret->i = 1;
    return NJ_OK;
}

NJFN(cal_toString)
{
    nj_ref c = NJ_THIS(args);
    nj_s8 y, mo, d, h, mi, s, ms;
    char buf[96];
    int len = 0;

    cal_to_fields(vm, c, &y, &mo, &d, &h, &mi, &s, &ms);
    buf[0] = '\0';
    put_str(buf, (int)sizeof(buf), &len, "java.util.Calendar[time=");
    put_int(buf, (int)sizeof(buf), &len, cal_millis(vm, c));
    put_str(buf, (int)sizeof(buf), &len, ",YEAR=");
    put_int(buf, (int)sizeof(buf), &len, y);
    put_str(buf, (int)sizeof(buf), &len, ",MONTH=");
    put_int(buf, (int)sizeof(buf), &len, mo - 1);
    put_str(buf, (int)sizeof(buf), &len, ",DAY_OF_MONTH=");
    put_int(buf, (int)sizeof(buf), &len, d);
    put_str(buf, (int)sizeof(buf), &len, ",HOUR_OF_DAY=");
    put_int(buf, (int)sizeof(buf), &len, h);
    put_str(buf, (int)sizeof(buf), &len, "]");
    ret->r = nj_string_new(vm, buf);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

/* ------------------------------------------------------------------ */
/* java.util.Stack                                                     */
/* ------------------------------------------------------------------ */
NJFN(stack_init)
{
    return NJ_OK;
}

NJFN(stack_push)
{
    ret->r = NJ_ARG_REF(args, 0);
    return vec_append(vm, NJ_THIS(args), NJ_ARG_REF(args, 0));
}

NJFN(stack_pop)
{
    nj_ref s = NJ_THIS(args);
    nj_s4 n;
    nj_ref data;

    if (vec_check(vm, s) != NJ_OK) {
        return NJ_ERR;
    }
    n = vec_count(vm, s);
    if (n <= 0) {
        return nj_throw(vm, "java/util/EmptyStackException", NULL);
    }
    data = vec_data(vm, s);
    ret->r = nj_array_get_ref(data, (nj_u4)(n - 1));
    vec_remove_at(vm, s, n - 1);
    return NJ_OK;
}

NJFN(stack_peek)
{
    nj_ref s = NJ_THIS(args);
    nj_s4 n;

    if (vec_check(vm, s) != NJ_OK) {
        return NJ_ERR;
    }
    n = vec_count(vm, s);
    if (n <= 0) {
        return nj_throw(vm, "java/util/EmptyStackException", NULL);
    }
    ret->r = nj_array_get_ref(vec_data(vm, s), (nj_u4)(n - 1));
    return NJ_OK;
}

NJFN(stack_empty)
{
    nj_ref s = NJ_THIS(args);

    if (vec_check(vm, s) != NJ_OK) {
        return NJ_ERR;
    }
    ret->i = (vec_count(vm, s) == 0) ? 1 : 0;
    return NJ_OK;
}

NJFN(stack_search)
{
    nj_ref s = NJ_THIS(args);
    nj_ref o = NJ_ARG_REF(args, 0);
    nj_s4 i, n;
    int err = 0;
    nj_ref data;

    if (vec_check(vm, s) != NJ_OK) {
        return NJ_ERR;
    }
    n = vec_count(vm, s);
    data = vec_data(vm, s);
    for (i = n - 1; i >= 0; i--) {
        if (obj_equals(vm, o, nj_array_get_ref(data, (nj_u4)i), &err)) {
            ret->i = n - i;
            return NJ_OK;
        }
        if (err) {
            return NJ_ERR;
        }
    }
    ret->i = -1;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* java.util.TimerTask / java.util.Timer                               */
/* ------------------------------------------------------------------ */
#define TT_CLS "java/util/TimerTask"
#define TM_CLS "java/util/Timer"

#define TT_VIRGIN    0
#define TT_SCHEDULED 1
#define TT_EXECUTED  2
#define TT_CANCELLED 3

/*
 * A cancelled (or idle) timer parks its helper thread instead of letting the
 * Java loop return: a finished green thread stays RUNNING with an empty stack
 * and would keep the scheduler spinning, so the thread parks in the SLEEPING
 * state, where the scheduler simply waits for its deadline.
 */
#define NJ_TIMER_IDLE_MS 600000

static nj_field *tt_ftime(nj_vm *vm)   { return fld(vm, TT_CLS, "taskTime", "J"); }
static nj_field *tt_fperiod(nj_vm *vm) { return fld(vm, TT_CLS, "taskPeriod", "J"); }
static nj_field *tt_ffixed(nj_vm *vm)  { return fld(vm, TT_CLS, "taskFixedRate", "Z"); }
static nj_field *tt_fstate(nj_vm *vm)  { return fld(vm, TT_CLS, "taskState", "I"); }
static nj_field *tt_fexec(nj_vm *vm)   { return fld(vm, TT_CLS, "taskExecutionTime", "J"); }
static nj_field *tt_ftimer(nj_vm *vm)  { return fld(vm, TT_CLS, "taskTimer", "Ljava/util/Timer;"); }

static nj_field *tm_ftasks(nj_vm *vm)  { return fld(vm, TM_CLS, "ttTasks", "Ljava/util/Vector;"); }
static nj_field *tm_fcancel(nj_vm *vm) { return fld(vm, TM_CLS, "ttCancelled", "Z"); }
static nj_field *tm_fhelper(nj_vm *vm) { return fld(vm, TM_CLS, "ttHelper", "Ljava/lang/Object;"); }

static nj_s8 tt_time(nj_vm *vm, nj_ref task)
{
    nj_field *f = tt_ftime(vm);
    return f ? nj_field_read_long(vm, task, f) : 0;
}

static nj_s4 tt_state(nj_vm *vm, nj_ref task)
{
    return fget(vm, task, tt_fstate(vm), TT_CANCELLED);
}

/*
 * The compiled nj2me/util/TimerThread (the exact source is in the file
 * header).  Loading it goes through the ordinary class loader, so its constant
 * pool, field layout and dispatch are the real thing.
 */
static const nj_u1 nj_timer_helper_class[] = {
    0xca, 0xfe, 0xba, 0xbe, 0x00, 0x00, 0x00, 0x34, 0x00, 0x18, 0x0a, 0x00,
    0x02, 0x00, 0x03, 0x07, 0x00, 0x04, 0x0c, 0x00, 0x05, 0x00, 0x06, 0x01,
    0x00, 0x10, 0x6a, 0x61, 0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f,
    0x4f, 0x62, 0x6a, 0x65, 0x63, 0x74, 0x01, 0x00, 0x06, 0x3c, 0x69, 0x6e,
    0x69, 0x74, 0x3e, 0x01, 0x00, 0x03, 0x28, 0x29, 0x56, 0x09, 0x00, 0x08,
    0x00, 0x09, 0x07, 0x00, 0x0a, 0x0c, 0x00, 0x0b, 0x00, 0x0c, 0x01, 0x00,
    0x16, 0x6e, 0x6a, 0x32, 0x6d, 0x65, 0x2f, 0x75, 0x74, 0x69, 0x6c, 0x2f,
    0x54, 0x69, 0x6d, 0x65, 0x72, 0x54, 0x68, 0x72, 0x65, 0x61, 0x64, 0x01,
    0x00, 0x05, 0x74, 0x69, 0x6d, 0x65, 0x72, 0x01, 0x00, 0x12, 0x4c, 0x6a,
    0x61, 0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f, 0x4f, 0x62, 0x6a,
    0x65, 0x63, 0x74, 0x3b, 0x0a, 0x00, 0x08, 0x00, 0x0e, 0x0c, 0x00, 0x0f,
    0x00, 0x10, 0x01, 0x00, 0x04, 0x70, 0x75, 0x6d, 0x70, 0x01, 0x00, 0x15,
    0x28, 0x4c, 0x6a, 0x61, 0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f,
    0x4f, 0x62, 0x6a, 0x65, 0x63, 0x74, 0x3b, 0x29, 0x5a, 0x01, 0x00, 0x15,
    0x28, 0x4c, 0x6a, 0x61, 0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f,
    0x4f, 0x62, 0x6a, 0x65, 0x63, 0x74, 0x3b, 0x29, 0x56, 0x01, 0x00, 0x04,
    0x43, 0x6f, 0x64, 0x65, 0x01, 0x00, 0x0f, 0x4c, 0x69, 0x6e, 0x65, 0x4e,
    0x75, 0x6d, 0x62, 0x65, 0x72, 0x54, 0x61, 0x62, 0x6c, 0x65, 0x01, 0x00,
    0x03, 0x72, 0x75, 0x6e, 0x01, 0x00, 0x0d, 0x53, 0x74, 0x61, 0x63, 0x6b,
    0x4d, 0x61, 0x70, 0x54, 0x61, 0x62, 0x6c, 0x65, 0x01, 0x00, 0x0a, 0x53,
    0x6f, 0x75, 0x72, 0x63, 0x65, 0x46, 0x69, 0x6c, 0x65, 0x01, 0x00, 0x10,
    0x54, 0x69, 0x6d, 0x65, 0x72, 0x54, 0x68, 0x72, 0x65, 0x61, 0x64, 0x2e,
    0x6a, 0x61, 0x76, 0x61, 0x00, 0x31, 0x00, 0x08, 0x00, 0x02, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x01, 0x00, 0x0b, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x01, 0x00, 0x05, 0x00, 0x11, 0x00, 0x01, 0x00, 0x12, 0x00, 0x00,
    0x00, 0x2a, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x0a, 0x2a, 0xb7,
    0x00, 0x01, 0x2a, 0x2b, 0xb5, 0x00, 0x07, 0xb1, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x13, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x03, 0x00, 0x00, 0x00, 0x06,
    0x00, 0x04, 0x00, 0x07, 0x00, 0x09, 0x00, 0x08, 0x00, 0x01, 0x00, 0x14,
    0x00, 0x06, 0x00, 0x01, 0x00, 0x12, 0x00, 0x00, 0x00, 0x34, 0x00, 0x01,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x0e, 0x2a, 0xb4, 0x00, 0x07, 0xb8, 0x00,
    0x0d, 0x99, 0x00, 0x06, 0xa7, 0xff, 0xf6, 0xb1, 0x00, 0x00, 0x00, 0x02,
    0x00, 0x13, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x02, 0x00, 0x00, 0x00, 0x0b,
    0x00, 0x0d, 0x00, 0x0d, 0x00, 0x15, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02,
    0x00, 0x0c, 0x01, 0x09, 0x00, 0x0f, 0x00, 0x10, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x16, 0x00, 0x00, 0x00, 0x02, 0x00, 0x17
};

#define NJ_TIMER_HELPER_CLASS "nj2me/util/TimerThread"

static nj_class *timer_helper_class(nj_vm *vm)
{
    nj_class *c = nj_class_find(vm, NJ_TIMER_HELPER_CLASS);

    if (c && c->state >= NJ_CLASS_LOADED) {
        return c;
    }
    return nj_class_load(vm, NJ_TIMER_HELPER_CLASS, nj_timer_helper_class,
                         (nj_u4)sizeof(nj_timer_helper_class));
}

NJFN(tt_init)
{
    nj_ref t = NJ_THIS(args);

    if (!tt_fstate(vm) || !tt_ftime(vm)) {
        return nj_throw(vm, "java/lang/InternalError", "TimerTask layout");
    }
    nj_field_write_int(vm, t, tt_fstate(vm), TT_VIRGIN);
    nj_field_write_long(vm, t, tt_ftime(vm), 0);
    nj_field_write_long(vm, t, tt_fperiod(vm), 0);
    nj_field_write_long(vm, t, tt_fexec(vm), 0);
    nj_field_write_int(vm, t, tt_ffixed(vm), 0);
    nj_field_write_ref(vm, t, tt_ftimer(vm), NULL);
    return NJ_OK;
}

NJFN(tt_cancel)
{
    nj_ref t = NJ_THIS(args);

    if (tt_state(vm, t) == TT_SCHEDULED) {
        nj_field_write_int(vm, t, tt_fstate(vm), TT_CANCELLED);
        ret->i = 1;
    } else {
        ret->i = 0;
    }
    return NJ_OK;
}

NJFN(tt_scheduledExecutionTime)
{
    nj_field *f = tt_fexec(vm);

    ret->j = f ? nj_field_read_long(vm, NJ_THIS(args), f) : 0;
    return NJ_OK;
}

/* The body of the helper thread: run due tasks, then sleep until the next. */
NJFN(timer_pump)
{
    nj_ref timer = NJ_ARG_REF(args, 0);
    nj_ref tasks, data;
    nj_s4 i, n, best = -1;
    nj_s8 best_time = 0;

    if (!timer) {
        ret->i = 0;             /* no timer: let the Java loop finish */
        return NJ_OK;
    }
    if (!tm_ftasks(vm) || !tt_fstate(vm) || !tt_ftime(vm)) {
        ret->i = 0;
        return NJ_OK;
    }
    tasks = nj_field_read_ref(vm, timer, tm_ftasks(vm));
    if (fget(vm, timer, tm_fcancel(vm), 0) != 0 || !tasks) {
        ret->i = 1;
        (void)nj_thread_sleep(vm, th, NJ_TIMER_IDLE_MS);
        return NJ_ERR_TIMEOUT;
    }
    data = vec_data(vm, tasks);
    n = vec_count(vm, tasks);
    for (i = 0; i < n; i++) {
        nj_ref task = data ? nj_array_get_ref(data, (nj_u4)i) : NULL;
        if (task && tt_state(vm, task) == TT_SCHEDULED) {
            nj_s8 when = tt_time(vm, task);
            if (best < 0 || when < best_time) {
                best = i;
                best_time = when;
            }
        } else {
            /* Done or cancelled: drop it from the queue. */
            vec_remove_at(vm, tasks, i);
            i--;
            n--;
        }
    }
    if (best < 0) {
        ret->i = 1;                     /* nothing to do: park */
        (void)nj_thread_sleep(vm, th, NJ_TIMER_IDLE_MS);
        return NJ_ERR_TIMEOUT;
    }
    {
        nj_s8 now = nj_now_ms(vm);
        if (now < best_time) {
            nj_s8 wait = best_time - now;
            if (wait > NJ_TIMER_IDLE_MS) {
                wait = NJ_TIMER_IDLE_MS;
            }
            ret->i = 1;                 /* resume the Java loop after waking */
            (void)nj_thread_sleep(vm, th, wait);
            return NJ_ERR_TIMEOUT;
        }
    }
    {
        nj_ref task = nj_array_get_ref(data, (nj_u4)best);
        nj_s8 period = tt_fperiod(vm) ? nj_field_read_long(vm, task, tt_fperiod(vm)) : 0;
        nj_s4 fixed = fget(vm, task, tt_ffixed(vm), 0);
        nj_s8 now = nj_now_ms(vm);

        if (period > 0) {
            nj_s8 next = fixed ? (best_time + period) : (now + period);
            if (next <= now) {
                next = now + period;
            }
            nj_field_write_long(vm, task, tt_ftime(vm), next);
        } else {
            vec_remove_at(vm, tasks, best);
            nj_field_write_int(vm, task, tt_fstate(vm), TT_EXECUTED);
        }
        nj_field_write_long(vm, task, tt_fexec(vm), fixed ? best_time : now);
        ret->i = 1;
        if (nj_call(vm, task, TT_CLS, "run", "()V", NULL, NULL) != NJ_OK) {
            /* An escaping exception kills the timer thread, like Java. */
            nj_ref exc = nj_take_exception(vm);
            nj_logf(vm, 1, "TimerTask.run() threw %s: timer cancelled",
                    (exc && exc->cls) ? exc->cls->name : "an exception");
            nj_field_write_int(vm, timer, tm_fcancel(vm), 1);
        }
        return NJ_OK;
    }
}

NJFN(timer_init)
{
    nj_ref t = NJ_THIS(args);
    nj_ref v, helper;
    nj_class *hc;
    nj_method *run;
    nj_thread *nt;
    nj_field *f;

    if (!tm_ftasks(vm) || !tm_fcancel(vm)) {
        return nj_throw(vm, "java/lang/InternalError", "Timer layout");
    }
    v = nj_object_new(vm, "java/util/Vector");
    if (!v) {
        return NJ_ERR;
    }
    nj_field_write_ref(vm, t, tm_ftasks(vm), v);   /* rooted through the timer */
    nj_field_write_int(vm, t, tm_fcancel(vm), 0);

    hc = timer_helper_class(vm);
    if (!hc) {
        return nj_throw(vm, "java/lang/InternalError", "timer helper class");
    }
    helper = nj_object_new(vm, NJ_TIMER_HELPER_CLASS);
    if (!helper) {
        return NJ_ERR;
    }
    f = fld(vm, NJ_TIMER_HELPER_CLASS, "timer", "Ljava/lang/Object;");
    if (!f) {
        return nj_throw(vm, "java/lang/InternalError", "timer helper layout");
    }
    nj_field_write_ref(vm, helper, f, t);
    if (tm_fhelper(vm)) {
        nj_field_write_ref(vm, t, tm_fhelper(vm), helper);
    }

    if (vm->nthreads >= (nj_u2)NJ_MAX_THREADS) {
        return nj_throw(vm, "java/lang/OutOfMemoryError", "too many threads");
    }
    nt = nj_thread_create(vm, helper, 0);
    if (!nt) {
        return nj_throw(vm, "java/lang/OutOfMemoryError", "timer thread");
    }
    run = nj_method_lookup(hc, "run", "()V");
    if (!run || nj_thread_start(vm, nt, run) != NJ_OK) {
        nj_thread_destroy(vm, nt);
        return nj_throw(vm, "java/lang/InternalError", "timer thread entry");
    }
    nt->state = NJ_THREAD_RUNNING;
    return NJ_OK;
}

NJFN(timer_init_daemon)
{
    /* The daemon flag only matters for JVM shutdown, which a MIDlet does not
     * have; the thread is identical. */
    return timer_init(vm, th, args, ret);
}

/* Common tail of schedule()/scheduleAtFixedRate(). */
static nj_status timer_add(nj_vm *vm, nj_ref timer, nj_ref task, nj_s8 when,
                           nj_s8 period, int fixed)
{
    nj_ref tasks;
    nj_s4 state;

    if (!task) {
        return nj_throw(vm, "java/lang/NullPointerException", "task");
    }
    if (!tt_fstate(vm) || !tt_ftime(vm)) {
        return nj_throw(vm, "java/lang/InternalError", "TimerTask layout");
    }
    state = tt_state(vm, task);
    if (state != TT_VIRGIN) {
        return nj_throw(vm, "java/lang/IllegalStateException",
                        "Task already scheduled or cancelled");
    }
    if (fget(vm, timer, tm_fcancel(vm), 0) != 0) {
        return nj_throw(vm, "java/lang/IllegalStateException",
                        "Timer cancelled");
    }
    tasks = rget(vm, timer, tm_ftasks(vm));
    if (!tasks) {
        return nj_throw(vm, "java/lang/IllegalStateException",
                        "Timer not initialised");
    }
    nj_field_write_long(vm, task, tt_ftime(vm), when);
    nj_field_write_long(vm, task, tt_fperiod(vm), period);
    nj_field_write_int(vm, task, tt_ffixed(vm), fixed ? 1 : 0);
    nj_field_write_long(vm, task, tt_fexec(vm), 0);
    nj_field_write_ref(vm, task, tt_ftimer(vm), timer);
    nj_field_write_int(vm, task, tt_fstate(vm), TT_SCHEDULED);
    return vec_append(vm, tasks, task);
}

NJFN(timer_schedule)
{
    nj_ref task = NJ_ARG_REF(args, 0);
    nj_s8 delay = NJ_ARG_LONG(args, 1);

    if (delay < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative delay");
    }
    return timer_add(vm, NJ_THIS(args), task, nj_now_ms(vm) + delay, 0, 0);
}

NJFN(timer_schedule_period)
{
    nj_ref task = NJ_ARG_REF(args, 0);
    nj_s8 delay = NJ_ARG_LONG(args, 1);
    nj_s8 period = NJ_ARG_LONG(args, 2);

    if (delay < 0 || period <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "non-positive period");
    }
    return timer_add(vm, NJ_THIS(args), task, nj_now_ms(vm) + delay, period, 0);
}

NJFN(timer_schedule_rate)
{
    nj_ref task = NJ_ARG_REF(args, 0);
    nj_s8 delay = NJ_ARG_LONG(args, 1);
    nj_s8 period = NJ_ARG_LONG(args, 2);

    if (delay < 0 || period <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "non-positive period");
    }
    return timer_add(vm, NJ_THIS(args), task, nj_now_ms(vm) + delay, period, 1);
}

NJFN(timer_cancel)
{
    nj_ref t = NJ_THIS(args);
    nj_ref tasks = rget(vm, t, tm_ftasks(vm));
    nj_ref data;
    nj_s4 i, n;

    if (tm_fcancel(vm)) {
        nj_field_write_int(vm, t, tm_fcancel(vm), 1);
    }
    if (!tasks || !tt_fstate(vm)) {
        return NJ_OK;
    }
    data = vec_data(vm, tasks);
    n = vec_count(vm, tasks);
    for (i = 0; i < n; i++) {
        nj_ref task = data ? nj_array_get_ref(data, (nj_u4)i) : NULL;
        if (task && tt_state(vm, task) == TT_SCHEDULED) {
            nj_field_write_int(vm, task, tt_fstate(vm), TT_CANCELLED);
        }
    }
    return NJ_OK;
}

NJFN(timer_purge)
{
    nj_ref t = NJ_THIS(args);
    nj_ref tasks = rget(vm, t, tm_ftasks(vm));
    nj_ref data;
    nj_s4 i, n, removed = 0;

    if (!tasks || !tt_fstate(vm)) {
        ret->i = 0;
        return NJ_OK;
    }
    data = vec_data(vm, tasks);
    n = vec_count(vm, tasks);
    for (i = 0; i < n; i++) {
        nj_ref task = data ? nj_array_get_ref(data, (nj_u4)i) : NULL;
        nj_s4 state = task ? tt_state(vm, task) : TT_CANCELLED;
        if (state == TT_CANCELLED || state == TT_EXECUTED) {
            vec_remove_at(vm, tasks, i);
            i--;
            n--;
            removed++;
        }
    }
    ret->i = removed;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }

static const nj_builtin_field vector_fields[] = {
    { "elementData",       "[Ljava/lang/Object;", 0 },
    { "elementCount",      "I",                   0 },
    { "capacityIncrement", "I",                   0 }
};

static const nj_builtin_method vector_methods[] = {
    M("<init>",              "()V",                              0),
    M("<init>",              "(I)V",                             0),
    M("<init>",              "(II)V",                            0),
    M("addElement",          "(Ljava/lang/Object;)V",            0),
    M("add",                 "(Ljava/lang/Object;)Z",            0),
    M("insertElementAt",     "(Ljava/lang/Object;I)V",           0),
    M("removeElement",       "(Ljava/lang/Object;)Z",            0),
    M("removeElementAt",     "(I)V",                             0),
    M("removeAllElements",   "()V",                              0),
    M("clear",               "()V",                              0),
    M("setElementAt",        "(Ljava/lang/Object;I)V",           0),
    M("elementAt",           "(I)Ljava/lang/Object;",            0),
    M("firstElement",        "()Ljava/lang/Object;",             0),
    M("lastElement",         "()Ljava/lang/Object;",             0),
    M("size",                "()I",                              0),
    M("capacity",            "()I",                              0),
    M("isEmpty",             "()Z",                              0),
    M("contains",            "(Ljava/lang/Object;)Z",            0),
    M("indexOf",             "(Ljava/lang/Object;)I",            0),
    M("indexOf",             "(Ljava/lang/Object;I)I",           0),
    M("lastIndexOf",         "(Ljava/lang/Object;)I",            0),
    M("lastIndexOf",         "(Ljava/lang/Object;I)I",           0),
    M("setSize",             "(I)V",                             0),
    M("ensureCapacity",      "(I)V",                             0),
    M("trimToSize",          "()V",                              0),
    M("copyInto",            "([Ljava/lang/Object;)V",           0),
    M("toString",            "()Ljava/lang/String;",             0),
    M("get",                 "(I)Ljava/lang/Object;",            0),
    M("set",                 "(ILjava/lang/Object;)Ljava/lang/Object;", 0),
    M("remove",              "(I)Ljava/lang/Object;",            0)
};

static const nj_builtin_method enumeration_methods[] = {
    M("hasMoreElements", "()Z", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("nextElement",     "()Ljava/lang/Object;",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT)
};

static const nj_builtin_field vector_enum_fields[] = {
    { "veVector", "Ljava/util/Vector;", 0 },
    { "veIndex",  "I",                  0 }
};

static const nj_builtin_method vector_enum_methods[] = {
    M("<init>",          "(Ljava/util/Vector;)V", 0),
    M("hasMoreElements", "()Z",                   0),
    M("nextElement",     "()Ljava/lang/Object;",  0)
};

static const nj_builtin_field hashtable_enum_fields[] = {
    { "heKeys",  "[Ljava/lang/Object;", 0 },
    { "heIndex", "I",                   0 }
};

static const nj_builtin_method hashtable_enum_methods[] = {
    M("<init>",          "([Ljava/lang/Object;)V", 0),
    M("hasMoreElements", "()Z",                    0),
    M("nextElement",     "()Ljava/lang/Object;",   0)
};

static const nj_builtin_field hashtable_fields[] = {
    { "htKeys",   "[Ljava/lang/Object;", 0 },
    { "htValues", "[Ljava/lang/Object;", 0 },
    { "htHash",   "[I",                  0 },
    { "htState",  "[I",                  0 },
    { "htCount",  "I",                   0 }
};

static const nj_builtin_method hashtable_methods[] = {
    M("<init>",         "()V",                                 0),
    M("<init>",         "(I)V",                                0),
    M("put",            "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", 0),
    M("get",            "(Ljava/lang/Object;)Ljava/lang/Object;", 0),
    M("remove",         "(Ljava/lang/Object;)Ljava/lang/Object;", 0),
    M("containsKey",    "(Ljava/lang/Object;)Z",               0),
    M("contains",       "(Ljava/lang/Object;)Z",               0),
    M("containsValue",  "(Ljava/lang/Object;)Z",               0),
    M("size",           "()I",                                 0),
    M("isEmpty",        "()Z",                                 0),
    M("clear",          "()V",                                 0),
    M("keys",           "()Ljava/util/Enumeration;",           0),
    M("elements",       "()Ljava/util/Enumeration;",           0),
    M("toString",       "()Ljava/lang/String;",                0)
};

static const nj_builtin_field random_fields[] = {
    { "seed",                 "J", 0 },
    { "haveNextNextGaussian", "Z", 0 },
    /* Stored as a J field: see the comment above rnd_fgauss(). */
    { "nextNextGaussian",     "J", 0 }
};

static const nj_builtin_method random_methods[] = {
    M("<init>",        "()V",  0),
    M("<init>",        "(J)V", 0),
    M("setSeed",       "(J)V", 0),
    M("next",          "(I)I", NJ_ACC_PROTECTED),
    M("nextInt",       "()I",  0),
    M("nextInt",       "(I)I", 0),
    M("nextLong",      "()J",  0),
    M("nextBoolean",   "()Z",  0),
    M("nextFloat",     "()F",  0),
    M("nextDouble",    "()D",  0),
    M("nextGaussian",  "()D",  0)
};

static const nj_builtin_field date_fields[] = {
    { "millis", "J", 0 }
};

static const nj_builtin_method date_methods[] = {
    M("<init>",      "()V",                       0),
    M("<init>",      "(J)V",                      0),
    M("getTime",     "()J",                       0),
    M("setTime",     "(J)V",                      0),
    M("equals",      "(Ljava/lang/Object;)Z",     0),
    M("hashCode",    "()I",                       0),
    M("toString",    "()Ljava/lang/String;",      0),
    M("before",      "(Ljava/util/Date;)Z",       0),
    M("after",       "(Ljava/util/Date;)Z",       0),
    M("compareTo",   "(Ljava/util/Date;)I",       0),
    M("getYear",     "()I",                       0),
    M("getMonth",    "()I",                       0),
    M("getDate",     "()I",                       0),
    M("getDay",      "()I",                       0),
    M("getHours",    "()I",                       0),
    M("getMinutes",  "()I",                       0),
    M("getSeconds",  "()I",                       0)
};

static const nj_builtin_field timezone_fields[] = {
    { "tzID",          "Ljava/lang/String;", 0 },
    { "tzRawOffset",   "I",                  0 },
    { "tzUseDaylight", "Z",                  0 }
};

static const nj_builtin_method timezone_methods[] = {
    M("<init>",           "()V",                                 0),
    M("getDefault",       "()Ljava/util/TimeZone;",              NJ_ACC_STATIC),
    M("getTimeZone",      "(Ljava/lang/String;)Ljava/util/TimeZone;", NJ_ACC_STATIC),
    M("getAvailableIDs",  "()[Ljava/lang/String;",               NJ_ACC_STATIC),
    M("getAvailableIDs",  "(I)[Ljava/lang/String;",              NJ_ACC_STATIC),
    M("getOffset",        "(IIIIII)I",                           0),
    M("getRawOffset",     "()I",                                 0),
    M("setRawOffset",     "(I)V",                                0),
    M("getID",            "()Ljava/lang/String;",                0),
    M("useDaylightTime",  "()Z",                                 0),
    M("hasSameRules",     "(Ljava/util/TimeZone;)Z",             0),
    M("toString",         "()Ljava/lang/String;",                0)
};

static const nj_builtin_field calendar_fields[] = {
    { "calMillis",  "J",                   0 },
    { "calZone",    "Ljava/util/TimeZone;", 0 },
    { "calStatics", "Z", NJ_ACC_STATIC | NJ_ACC_PRIVATE },
    /* Public constants; cal_constants() fills the slots on first use. */
    { "ERA",              "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "YEAR",             "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "MONTH",            "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "WEEK_OF_YEAR",     "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "WEEK_OF_MONTH",    "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "DATE",             "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "DAY_OF_MONTH",     "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "DAY_OF_YEAR",      "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "DAY_OF_WEEK",      "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "DAY_OF_WEEK_IN_MONTH", "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "AM_PM",            "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "HOUR",             "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "HOUR_OF_DAY",      "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "MINUTE",           "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "SECOND",           "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "MILLISECOND",      "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "ZONE_OFFSET",      "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "DST_OFFSET",       "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "SUNDAY",           "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "MONDAY",           "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "TUESDAY",          "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "WEDNESDAY",        "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "THURSDAY",         "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "FRIDAY",           "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "SATURDAY",         "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "JANUARY",          "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "FEBRUARY",         "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "MARCH",            "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "APRIL",            "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "MAY",              "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "JUNE",             "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "JULY",             "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "AUGUST",           "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "SEPTEMBER",        "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "OCTOBER",          "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "NOVEMBER",         "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "DECEMBER",         "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "AM",               "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "PM",               "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC }
};

static const nj_builtin_method calendar_methods[] = {
    M("<init>",            "()V",                              0),
    M("<init>",            "(Ljava/util/TimeZone;)V",          0),
    M("getInstance",       "()Ljava/util/Calendar;",           NJ_ACC_STATIC),
    M("getInstance",       "(Ljava/util/TimeZone;)Ljava/util/Calendar;", NJ_ACC_STATIC),
    M("get",               "(I)I",                             0),
    M("set",               "(II)V",                            0),
    M("clear",             "()V",                              0),
    M("clear",             "(I)V",                             0),
    M("add",               "(II)V",                            0),
    M("getTimeInMillis",   "()J",                              0),
    M("setTimeInMillis",   "(J)V",                             0),
    M("getTime",           "()Ljava/util/Date;",               0),
    M("setTime",           "(Ljava/util/Date;)V",              0),
    M("getTimeZone",       "()Ljava/util/TimeZone;",           0),
    M("setTimeZone",       "(Ljava/util/TimeZone;)V",          0),
    M("isLenient",         "()Z",                              0),
    M("toString",          "()Ljava/lang/String;",             0)
};

static const nj_builtin_method stack_methods[] = {
    M("<init>",  "()V",                                    0),
    M("push",    "(Ljava/lang/Object;)Ljava/lang/Object;", 0),
    M("pop",     "()Ljava/lang/Object;",                   0),
    M("peek",    "()Ljava/lang/Object;",                   0),
    M("empty",   "()Z",                                    0),
    M("search",  "(Ljava/lang/Object;)I",                  0)
};

static const nj_builtin_field timertask_fields[] = {
    { "taskTime",          "J",                  0 },
    { "taskPeriod",        "J",                  0 },
    { "taskFixedRate",     "Z",                  0 },
    { "taskState",         "I",                  0 },
    { "taskExecutionTime", "J",                  0 },
    { "taskTimer",         "Ljava/util/Timer;",  0 }
};

static const nj_builtin_method timertask_methods[] = {
    M("<init>",                 "()V", 0),
    M("cancel",                 "()Z", 0),
    M("scheduledExecutionTime", "()J", 0),
    M("run",                    "()V", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT)
};

static const nj_builtin_field timer_fields[] = {
    { "ttTasks",     "Ljava/util/Vector;", 0 },
    { "ttCancelled", "Z",                  0 },
    { "ttHelper",    "Ljava/lang/Object;", 0 }
};

static const nj_builtin_method timer_methods[] = {
    M("<init>",              "()V",                        0),
    M("<init>",              "(Z)V",                       0),
    M("schedule",            "(Ljava/util/TimerTask;J)V",  0),
    M("schedule",            "(Ljava/util/TimerTask;JJ)V", 0),
    M("scheduleAtFixedRate", "(Ljava/util/TimerTask;JJ)V", 0),
    M("cancel",              "()V",                        0),
    M("purge",               "()I",                        0)
};

const nj_builtin_class nj_lib_util_classes[] = {
    { "java/util/Vector", "java/lang/Object", NJ_ACC_PUBLIC,
      vector_fields,
      (nj_u2)(sizeof(vector_fields) / sizeof(vector_fields[0])),
      vector_methods,
      (nj_u2)(sizeof(vector_methods) / sizeof(vector_methods[0])) },

    { "java/util/Enumeration", NULL,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, enumeration_methods,
      (nj_u2)(sizeof(enumeration_methods) / sizeof(enumeration_methods[0])) },

    { "java/util/VectorEnumerator", "java/lang/Object", 0,
      vector_enum_fields,
      (nj_u2)(sizeof(vector_enum_fields) / sizeof(vector_enum_fields[0])),
      vector_enum_methods,
      (nj_u2)(sizeof(vector_enum_methods) / sizeof(vector_enum_methods[0])) },

    { "java/util/HashtableEnumerator", "java/lang/Object", 0,
      hashtable_enum_fields,
      (nj_u2)(sizeof(hashtable_enum_fields) / sizeof(hashtable_enum_fields[0])),
      hashtable_enum_methods,
      (nj_u2)(sizeof(hashtable_enum_methods) / sizeof(hashtable_enum_methods[0])) },

    { "java/util/Hashtable", "java/lang/Object", NJ_ACC_PUBLIC,
      hashtable_fields,
      (nj_u2)(sizeof(hashtable_fields) / sizeof(hashtable_fields[0])),
      hashtable_methods,
      (nj_u2)(sizeof(hashtable_methods) / sizeof(hashtable_methods[0])) },

    { "java/util/Random", "java/lang/Object", NJ_ACC_PUBLIC,
      random_fields,
      (nj_u2)(sizeof(random_fields) / sizeof(random_fields[0])),
      random_methods,
      (nj_u2)(sizeof(random_methods) / sizeof(random_methods[0])) },

    { "java/util/Date", "java/lang/Object", NJ_ACC_PUBLIC,
      date_fields,
      (nj_u2)(sizeof(date_fields) / sizeof(date_fields[0])),
      date_methods,
      (nj_u2)(sizeof(date_methods) / sizeof(date_methods[0])) },

    { "java/util/TimeZone", "java/lang/Object",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      timezone_fields,
      (nj_u2)(sizeof(timezone_fields) / sizeof(timezone_fields[0])),
      timezone_methods,
      (nj_u2)(sizeof(timezone_methods) / sizeof(timezone_methods[0])) },

    { "java/util/Calendar", "java/lang/Object",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      calendar_fields,
      (nj_u2)(sizeof(calendar_fields) / sizeof(calendar_fields[0])),
      calendar_methods,
      (nj_u2)(sizeof(calendar_methods) / sizeof(calendar_methods[0])) },

    { "java/util/Stack", "java/util/Vector", NJ_ACC_PUBLIC,
      NULL, 0u, stack_methods,
      (nj_u2)(sizeof(stack_methods) / sizeof(stack_methods[0])) },

    { "java/util/TimerTask", "java/lang/Object",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      timertask_fields,
      (nj_u2)(sizeof(timertask_fields) / sizeof(timertask_fields[0])),
      timertask_methods,
      (nj_u2)(sizeof(timertask_methods) / sizeof(timertask_methods[0])) },

    { "java/util/Timer", "java/lang/Object", NJ_ACC_PUBLIC,
      timer_fields,
      (nj_u2)(sizeof(timer_fields) / sizeof(timer_fields[0])),
      timer_methods,
      (nj_u2)(sizeof(timer_methods) / sizeof(timer_methods[0])) },

    /* Subclasses with no members of their own: they inherit Throwable's two
     * constructors, which is exactly how the java.lang hierarchy works. */
    { "java/util/NoSuchElementException", "java/lang/RuntimeException",
      NJ_ACC_PUBLIC, NULL, 0u, NULL, 0u },
    { "java/util/EmptyStackException", "java/lang/RuntimeException",
      NJ_ACC_PUBLIC, NULL, 0u, NULL, 0u }
};
const int nj_lib_util_class_count =
    (int)(sizeof(nj_lib_util_classes) / sizeof(nj_lib_util_classes[0]));

const nj_native_entry nj_lib_util_table[] = {
    { "java/util/Vector", "<init>",            "()V",                  vec_init },
    { "java/util/Vector", "<init>",            "(I)V",                 vec_init_cap },
    { "java/util/Vector", "<init>",            "(II)V",                vec_init_cap_inc },
    { "java/util/Vector", "addElement",        "(Ljava/lang/Object;)V", vec_addElement },
    { "java/util/Vector", "add",               "(Ljava/lang/Object;)Z", vec_add },
    { "java/util/Vector", "insertElementAt",   "(Ljava/lang/Object;I)V", vec_insertElementAt },
    { "java/util/Vector", "removeElement",     "(Ljava/lang/Object;)Z", vec_removeElement },
    { "java/util/Vector", "removeElementAt",   "(I)V",                 vec_removeElementAt },
    { "java/util/Vector", "removeAllElements", "()V",                  vec_removeAllElements },
    { "java/util/Vector", "clear",             "()V",                  vec_clear },
    { "java/util/Vector", "setElementAt",      "(Ljava/lang/Object;I)V", vec_setElementAt },
    { "java/util/Vector", "elementAt",         "(I)Ljava/lang/Object;", vec_elementAt },
    { "java/util/Vector", "firstElement",      "()Ljava/lang/Object;", vec_firstElement },
    { "java/util/Vector", "lastElement",       "()Ljava/lang/Object;", vec_lastElement },
    { "java/util/Vector", "size",              "()I",                  vec_size },
    { "java/util/Vector", "capacity",          "()I",                  vec_capacity },
    { "java/util/Vector", "isEmpty",           "()Z",                  vec_isEmpty },
    { "java/util/Vector", "contains",          "(Ljava/lang/Object;)Z", vec_contains },
    { "java/util/Vector", "indexOf",           "(Ljava/lang/Object;)I", vec_indexOf },
    { "java/util/Vector", "indexOf",           "(Ljava/lang/Object;I)I", vec_indexOf_from },
    { "java/util/Vector", "lastIndexOf",       "(Ljava/lang/Object;)I", vec_lastIndexOf },
    { "java/util/Vector", "lastIndexOf",       "(Ljava/lang/Object;I)I", vec_lastIndexOf_from },
    { "java/util/Vector", "setSize",           "(I)V",                 vec_setSize },
    { "java/util/Vector", "ensureCapacity",    "(I)V",                 vec_ensureCapacity },
    { "java/util/Vector", "trimToSize",        "()V",                  vec_trimToSize },
    { "java/util/Vector", "copyInto",          "([Ljava/lang/Object;)V", vec_copyInto },
    { "java/util/Vector", "toString",          "()Ljava/lang/String;", vec_toString },
    { "java/util/Vector", "get",               "(I)Ljava/lang/Object;", vec_get },
    { "java/util/Vector", "set",               "(ILjava/lang/Object;)Ljava/lang/Object;", vec_set },
    { "java/util/Vector", "remove",            "(I)Ljava/lang/Object;", vec_remove_index },

    { "java/util/VectorEnumerator", "<init>",          "(Ljava/util/Vector;)V", ve_init },
    { "java/util/VectorEnumerator", "hasMoreElements", "()Z",                   ve_hasMoreElements },
    { "java/util/VectorEnumerator", "nextElement",     "()Ljava/lang/Object;",  ve_nextElement },

    { "java/util/HashtableEnumerator", "<init>",          "([Ljava/lang/Object;)V", he_init },
    { "java/util/HashtableEnumerator", "hasMoreElements", "()Z",                     he_hasMoreElements },
    { "java/util/HashtableEnumerator", "nextElement",     "()Ljava/lang/Object;",    he_nextElement },

    { "java/util/Hashtable", "<init>",        "()V",                    ht_init },
    { "java/util/Hashtable", "<init>",        "(I)V",                   ht_init_cap },
    { "java/util/Hashtable", "put",           "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", ht_put },
    { "java/util/Hashtable", "get",           "(Ljava/lang/Object;)Ljava/lang/Object;", ht_get },
    { "java/util/Hashtable", "remove",        "(Ljava/lang/Object;)Ljava/lang/Object;", ht_remove },
    { "java/util/Hashtable", "containsKey",   "(Ljava/lang/Object;)Z",  ht_containsKey },
    { "java/util/Hashtable", "contains",      "(Ljava/lang/Object;)Z",  ht_contains },
    { "java/util/Hashtable", "containsValue", "(Ljava/lang/Object;)Z",  ht_containsValue },
    { "java/util/Hashtable", "size",          "()I",                    ht_size },
    { "java/util/Hashtable", "isEmpty",       "()Z",                    ht_isEmpty },
    { "java/util/Hashtable", "clear",         "()V",                    ht_clear },
    { "java/util/Hashtable", "keys",          "()Ljava/util/Enumeration;", ht_keys },
    { "java/util/Hashtable", "elements",      "()Ljava/util/Enumeration;", ht_elements },
    { "java/util/Hashtable", "toString",      "()Ljava/lang/String;",   ht_toString },

    { "java/util/Random", "<init>",       "()V",   rnd_init },
    { "java/util/Random", "<init>",       "(J)V",  rnd_init_seed },
    { "java/util/Random", "setSeed",      "(J)V",  rnd_setSeed },
    { "java/util/Random", "next",         "(I)I",  rnd_next },
    { "java/util/Random", "nextInt",      "()I",   rnd_nextInt },
    { "java/util/Random", "nextInt",      "(I)I",  rnd_nextIntN },
    { "java/util/Random", "nextLong",     "()J",   rnd_nextLong },
    { "java/util/Random", "nextBoolean",  "()Z",   rnd_nextBoolean },
    { "java/util/Random", "nextFloat",    "()F",   rnd_nextFloat },
    { "java/util/Random", "nextDouble",   "()D",   rnd_nextDouble },
    { "java/util/Random", "nextGaussian", "()D",   rnd_nextGaussian },

    { "java/util/Date", "<init>",      "()V",                   date_init },
    { "java/util/Date", "<init>",      "(J)V",                  date_init_ms },
    { "java/util/Date", "getTime",     "()J",                   date_getTime },
    { "java/util/Date", "setTime",     "(J)V",                  date_setTime },
    { "java/util/Date", "equals",      "(Ljava/lang/Object;)Z", date_equals },
    { "java/util/Date", "hashCode",    "()I",                   date_hashCode },
    { "java/util/Date", "toString",    "()Ljava/lang/String;",  date_toString },
    { "java/util/Date", "before",      "(Ljava/util/Date;)Z",   date_before },
    { "java/util/Date", "after",       "(Ljava/util/Date;)Z",   date_after },
    { "java/util/Date", "compareTo",   "(Ljava/util/Date;)I",   date_compareTo },
    { "java/util/Date", "getYear",     "()I",                   date_getYear },
    { "java/util/Date", "getMonth",    "()I",                   date_getMonth },
    { "java/util/Date", "getDate",     "()I",                   date_getDate },
    { "java/util/Date", "getDay",      "()I",                   date_getDay },
    { "java/util/Date", "getHours",    "()I",                   date_getHours },
    { "java/util/Date", "getMinutes",  "()I",                   date_getMinutes },
    { "java/util/Date", "getSeconds",  "()I",                   date_getSeconds },

    { "java/util/TimeZone", "<init>",          "()V",                     tz_init },
    { "java/util/TimeZone", "getDefault",      "()Ljava/util/TimeZone;",  tz_getDefault },
    { "java/util/TimeZone", "getTimeZone",     "(Ljava/lang/String;)Ljava/util/TimeZone;", tz_getTimeZone },
    { "java/util/TimeZone", "getAvailableIDs", "()[Ljava/lang/String;",   tz_getAvailableIDs },
    { "java/util/TimeZone", "getAvailableIDs", "(I)[Ljava/lang/String;",  tz_getAvailableIDs_offset },
    { "java/util/TimeZone", "getOffset",       "(IIIIII)I",               tz_getOffset },
    { "java/util/TimeZone", "getRawOffset",    "()I",                     tz_getRawOffset },
    { "java/util/TimeZone", "setRawOffset",    "(I)V",                    tz_setRawOffset },
    { "java/util/TimeZone", "getID",           "()Ljava/lang/String;",    tz_getID },
    { "java/util/TimeZone", "useDaylightTime", "()Z",                     tz_useDaylightTime },
    { "java/util/TimeZone", "hasSameRules",    "(Ljava/util/TimeZone;)Z", tz_hasSameRules },
    { "java/util/TimeZone", "toString",        "()Ljava/lang/String;",    tz_toString },

    { "java/util/Calendar", "<init>",          "()V",                          cal_init },
    { "java/util/Calendar", "<init>",          "(Ljava/util/TimeZone;)V",      cal_init_zone },
    { "java/util/Calendar", "getInstance",     "()Ljava/util/Calendar;",       cal_getInstance },
    { "java/util/Calendar", "getInstance",     "(Ljava/util/TimeZone;)Ljava/util/Calendar;", cal_getInstance_zone },
    { "java/util/Calendar", "get",             "(I)I",                         cal_get },
    { "java/util/Calendar", "set",             "(II)V",                        cal_set },
    { "java/util/Calendar", "clear",           "()V",                          cal_clear },
    { "java/util/Calendar", "clear",           "(I)V",                         cal_clear_field },
    { "java/util/Calendar", "add",             "(II)V",                        cal_add },
    { "java/util/Calendar", "getTimeInMillis", "()J",                          cal_getTimeInMillis },
    { "java/util/Calendar", "setTimeInMillis", "(J)V",                         cal_setTimeInMillis },
    { "java/util/Calendar", "getTime",         "()Ljava/util/Date;",           cal_getTime },
    { "java/util/Calendar", "setTime",         "(Ljava/util/Date;)V",          cal_setTime },
    { "java/util/Calendar", "getTimeZone",     "()Ljava/util/TimeZone;",       cal_getTimeZone },
    { "java/util/Calendar", "setTimeZone",     "(Ljava/util/TimeZone;)V",      cal_setTimeZone },
    { "java/util/Calendar", "isLenient",       "()Z",                          cal_isLenient },
    { "java/util/Calendar", "toString",        "()Ljava/lang/String;",         cal_toString },

    { "java/util/Stack", "<init>", "()V",                                     stack_init },
    { "java/util/Stack", "push",   "(Ljava/lang/Object;)Ljava/lang/Object;",  stack_push },
    { "java/util/Stack", "pop",    "()Ljava/lang/Object;",                    stack_pop },
    { "java/util/Stack", "peek",   "()Ljava/lang/Object;",                    stack_peek },
    { "java/util/Stack", "empty",  "()Z",                                     stack_empty },
    { "java/util/Stack", "search", "(Ljava/lang/Object;)I",                   stack_search },

    { "java/util/TimerTask", "<init>",                 "()V", tt_init },
    { "java/util/TimerTask", "cancel",                 "()Z", tt_cancel },
    { "java/util/TimerTask", "scheduledExecutionTime", "()J", tt_scheduledExecutionTime },

    { "java/util/Timer", "<init>",              "()V",                        timer_init },
    { "java/util/Timer", "<init>",              "(Z)V",                       timer_init_daemon },
    { "java/util/Timer", "schedule",            "(Ljava/util/TimerTask;J)V",  timer_schedule },
    { "java/util/Timer", "schedule",            "(Ljava/util/TimerTask;JJ)V", timer_schedule_period },
    { "java/util/Timer", "scheduleAtFixedRate", "(Ljava/util/TimerTask;JJ)V", timer_schedule_rate },
    { "java/util/Timer", "cancel",              "()V",                        timer_cancel },
    { "java/util/Timer", "purge",               "()I",                        timer_purge },

    /* The helper thread's body, loaded from the embedded class file. */
    { "nj2me/util/TimerThread", "pump", "(Ljava/lang/Object;)Z", timer_pump }
};
const int nj_lib_util_count =
    (int)(sizeof(nj_lib_util_table) / sizeof(nj_lib_util_table[0]));
