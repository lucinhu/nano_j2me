/*
 * nano_j2me - nj_string.c
 *
 * java.lang.String representation and UTF conversion.
 *
 * Strings are stored as UTF-16 (nj_u2) code units with a trailing NUL, which
 * makes String.charAt()/length() O(1) and exactly matches Java semantics for
 * surrogate pairs.  Class-file constants use "modified UTF-8" (CESU-8): the
 * decoder below accepts both that and standard 4-byte UTF-8, and the encoder
 * always emits modified UTF-8 so round-trips through DataOutputStream and
 * RecordStore behave like a real JVM.
 */
#include "nj_internal.h"

#if NJ_USE_LIBC
#  include <string.h>
#endif

/* ------------------------------------------------------------------ */
/* UTF-8 -> UTF-16                                                     */
/* ------------------------------------------------------------------ */
int nj_utf8_to_utf16(const char *src, int src_len, nj_u2 *dst, int dst_cap)
{
    int i = 0;
    int n = 0;

    if (!src) {
        return 0;
    }
    if (src_len < 0) {
        src_len = (int)strlen(src);
    }

    while (i < src_len) {
        nj_u4 c = (nj_u1)src[i];
        nj_u4 cp;

        if (c < 0x80u) {
            i++;
            cp = c;
        } else if ((c & 0xE0u) == 0xC0u) {
            if (i + 1 >= src_len) {
                break;
            }
            cp = ((c & 0x1Fu) << 6) | ((nj_u1)src[i + 1] & 0x3Fu);
            i += 2;
            /* modified UTF-8 encodes U+0000 as C0 80 */
            if (cp == 0u) {
                cp = 0u;
            }
        } else if ((c & 0xF0u) == 0xE0u) {
            if (i + 2 >= src_len) {
                break;
            }
            cp = ((c & 0x0Fu) << 12) |
                 (((nj_u1)src[i + 1] & 0x3Fu) << 6) |
                 ((nj_u1)src[i + 2] & 0x3Fu);
            i += 3;
        } else if ((c & 0xF8u) == 0xF0u) {
            if (i + 3 >= src_len) {
                break;
            }
            cp = ((c & 0x07u) << 18) |
                 (((nj_u1)src[i + 1] & 0x3Fu) << 12) |
                 (((nj_u1)src[i + 2] & 0x3Fu) << 6) |
                 ((nj_u1)src[i + 3] & 0x3Fu);
            i += 4;
            /* Standard UTF-8 supplementary character: emit a surrogate pair. */
            if (cp >= 0x10000u && cp <= 0x10FFFFu) {
                nj_u4 v = cp - 0x10000u;
                if (dst && n + 1 < dst_cap) {
                    dst[n] = (nj_u2)(0xD800u + (v >> 10));
                    dst[n + 1] = (nj_u2)(0xDC00u + (v & 0x3FFu));
                }
                n += 2;
                continue;
            }
        } else {
            /* Invalid lead byte: substitute U+FFFD and resynchronise. */
            i++;
            cp = 0xFFFDu;
        }

        if (dst && n < dst_cap) {
            dst[n] = (nj_u2)cp;
        }
        n++;
    }
    if (dst && n < dst_cap) {
        dst[n] = 0u;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* UTF-16 -> modified UTF-8                                            */
/* ------------------------------------------------------------------ */
int nj_utf16_to_utf8(const nj_u2 *src, int src_len, char *dst, int dst_cap)
{
    int i;
    int n = 0;
    int written = 0;    /* bytes stored in dst, always a whole number of
                         * characters and never the terminator's slot */

    if (!src) {
        return 0;
    }
    for (i = 0; i < src_len; i++) {
        nj_u4 c = src[i];
        nj_u1 buf[4];
        int len;

        if (c == 0u) {
            buf[0] = 0xC0u; buf[1] = 0x80u; len = 2;    /* modified UTF-8 NUL */
        } else if (c < 0x80u) {
            buf[0] = (nj_u1)c; len = 1;
        } else if (c < 0x800u) {
            buf[0] = (nj_u1)(0xC0u | (c >> 6));
            buf[1] = (nj_u1)(0x80u | (c & 0x3Fu));
            len = 2;
        } else {
            /* Includes surrogates, which is exactly what CESU-8 wants. */
            buf[0] = (nj_u1)(0xE0u | (c >> 12));
            buf[1] = (nj_u1)(0x80u | ((c >> 6) & 0x3Fu));
            buf[2] = (nj_u1)(0x80u | (c & 0x3Fu));
            len = 3;
        }
        if (dst && n + len < dst_cap) {
            /* Only whole characters are ever stored, and never the last byte:
             * that one is kept for the terminator, so a caller that ignores
             * the return value still hands out a valid, terminated string
             * instead of a truncated one with stack garbage behind it. */
            int k;
            for (k = 0; k < len; k++) {
                dst[n + k] = (char)buf[k];
            }
            written = n + len;
        }
        n += len;
    }
    if (dst && dst_cap > 0) {
        dst[written] = '\0';
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* String objects                                                      */
/* ------------------------------------------------------------------ */
nj_ref nj_new_string_utf16(nj_vm *vm, const nj_u2 *s, int len)
{
    nj_object *o;
    nj_size bytes;

    if (!vm) {
        return NULL;
    }
    if (len < 0) {
        len = 0;
    }
    bytes = ((nj_size)len + 1u) * sizeof(nj_u2);
    o = (nj_object *)nj_alloc(vm, sizeof(nj_object) + bytes);
    if (!o) {
        return NULL;
    }
    /* nj_class_find walked the class hash once per String created - 1.86 M
       lookups for "java/lang/String" in 1200 frames of 合金弹头4.  The class
       table already records the class when it is created (src/core/nj_class.c,
       "frequently used classes"), so ask it first; the lookup stays as the
       answer for the window before the class exists. */
    o->cls = vm->cls_string ? vm->cls_string
                            : nj_class_find(vm, "java/lang/String");
    o->kind = NJ_OBJ_STRING;
    o->length = (nj_u4)len;
    o->size = (nj_u4)(sizeof(nj_object) + bytes);
    o->u.bytes = (nj_u1 *)o + sizeof(nj_object);
    if (s && len > 0) {
        memcpy(o->u.bytes, s, (nj_size)len * sizeof(nj_u2));
    }
    ((nj_u2 *)o->u.bytes)[len] = 0u;
    vm->total_objects++;
    return o;
}

nj_ref nj_new_string_utf8(nj_vm *vm, const char *s, int len)
{
    nj_u2 *tmp;
    nj_u4 cap;
    int n;
    nj_ref r;

    if (!vm) {
        return NULL;
    }
    if (!s) {
        return nj_new_string_utf16(vm, NULL, 0);
    }
    if (len < 0) {
        len = (int)strlen(s);
    }
    /* Worst case: every byte becomes one UTF-16 unit. */
    cap = (nj_u4)len + 1u;
    tmp = (nj_u2 *)nj_host_alloc(vm, (nj_size)cap * sizeof(nj_u2));
    if (!tmp) {
        return NULL;
    }
    n = nj_utf8_to_utf16(s, len, tmp, (int)cap);
    r = nj_new_string_utf16(vm, tmp, n);
    nj_host_free(vm, tmp);
    return r;
}

nj_ref nj_new_string_ascii(nj_vm *vm, const char *s)
{
    nj_u2 *tmp;
    nj_u4 len, i;
    nj_ref r;

    if (!vm) {
        return NULL;
    }
    if (!s) {
        return nj_new_string_utf16(vm, NULL, 0);
    }
    len = (nj_u4)strlen(s);
    tmp = (nj_u2 *)nj_host_alloc(vm, ((nj_size)len + 1u) * sizeof(nj_u2));
    if (!tmp) {
        return NULL;
    }
    for (i = 0; i < len; i++) {
        tmp[i] = (nj_u2)(nj_u1)s[i];
    }
    r = nj_new_string_utf16(vm, tmp, (int)len);
    nj_host_free(vm, tmp);
    return r;
}

/*
 * Two string representations, distinguished by NJ_STR_WRAPPED in aflags:
 *
 *   embedded (literals, substring, concat, valueOf) - the UTF-16 payload sits
 *       directly after the object header, so a string costs one allocation;
 *   wrapped  (only for `new String(char[])`-style constructors) - u.raw is an
 *       nj_ref to a java char[] that owns the characters.
 *
 * The wrapped form exists so that <init> can install characters into an object
 * that the interpreter already allocated at a fixed size.
 */
nj_u2 *nj_string_chars(nj_ref s)
{
    if (!s || s->kind != NJ_OBJ_STRING) {
        return NULL;
    }
    if ((s->aflags & NJ_STR_WRAPPED) != 0u) {
        nj_object *arr = (nj_object *)s->u.raw;
        if (!arr || arr->kind != NJ_OBJ_ARRAY) {
            return NULL;
        }
        return (nj_u2 *)arr->u.bytes;
    }
    return (nj_u2 *)s->u.bytes;
}

/*
 * Install a character array as the backing store of an existing String.
 * The characters are copied, matching java.lang.String's value semantics, so
 * mutating the source array afterwards cannot change the string.
 */
nj_status nj_string_set_chars(nj_vm *vm, nj_ref s, const nj_u2 *chars, nj_u4 len)
{
    nj_ref arr;

    if (!vm || !s || s->kind != NJ_OBJ_STRING) {
        return NJ_ERR;
    }
    arr = nj_new_array(vm, NULL, len, NJ_ARRAY_T_CHAR);
    if (!arr) {
        return NJ_ERR_NOMEM;
    }
    if (len > 0u && chars) {
        memcpy(arr->u.bytes, chars, (nj_size)len * sizeof(nj_u2));
    }
    s->u.raw = arr;
    s->aflags = (nj_u2)(s->aflags | NJ_STR_WRAPPED);
    s->length = len;
    return NJ_OK;
}

nj_u4 nj_string_len(nj_ref s)
{
    if (!s || s->kind != NJ_OBJ_STRING) {
        return 0u;
    }
    return s->length;
}

int nj_string_equals_cstr(nj_ref s, const char *cstr)
{
    nj_u4 i;
    nj_u4 n;

    if (!cstr) {
        return s == NULL;
    }
    if (!s || s->kind != NJ_OBJ_STRING) {
        return 0;
    }
    n = (nj_u4)strlen(cstr);
    if (n != s->length) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (nj_string_chars(s)[i] != (nj_u2)(nj_u1)cstr[i]) {
            return 0;
        }
    }
    return 1;
}

int nj_string_hash(nj_ref s)
{
    nj_u4 i;
    nj_s4 h = 0;

    if (!s || s->kind != NJ_OBJ_STRING) {
        return 0;
    }
    for (i = 0; i < s->length; i++) {
        /*
         * String.hashCode() is specified as 32-bit WRAPPING arithmetic.  On a
         * signed type that is undefined behaviour in C, and a compiler is
         * entitled to assume it never happens - which would silently break the
         * hash contract that Hashtable keys depend on.  Unsigned arithmetic
         * wraps by definition, so the specified value is computed without UB.
         * UBSan flagged this on three corpus titles.
         */
        h = (nj_s4)((nj_u4)h * 31u + (nj_u4)nj_string_chars(s)[i]);
    }
    return h;
}

nj_ref nj_string_concat(nj_vm *vm, nj_ref a, nj_ref b)
{
    nj_u4 la = nj_string_len(a);
    nj_u4 lb = nj_string_len(b);
    nj_u2 *tmp;
    nj_ref r;

    tmp = (nj_u2 *)nj_host_alloc(vm, ((nj_size)la + lb + 1u) * sizeof(nj_u2));
    if (!tmp) {
        return NULL;
    }
    if (la) {
        memcpy(tmp, nj_string_chars(a), (nj_size)la * sizeof(nj_u2));
    }
    if (lb) {
        memcpy(tmp + la, nj_string_chars(b), (nj_size)lb * sizeof(nj_u2));
    }
    r = nj_new_string_utf16(vm, tmp, (int)(la + lb));
    nj_host_free(vm, tmp);
    return r;
}

/* ------------------------------------------------------------------ */
/* Interning                                                           */
/* ------------------------------------------------------------------ */

/*
 * Grow the intern table (or allocate it the first time).  Doubling keeps the
 * total copying linear in the number of literals.  Returns 0 when the host
 * allocator refuses; the caller must then NOT publish the string uninterned -
 * a constant-pool cache would keep pointing at it after the collector recycled
 * the block.
 */
static int intern_grow(nj_vm *vm)
{
    nj_u4 cap = vm->intern_cap ? vm->intern_cap * 2u : (nj_u4)NJ_MAX_INTERNED;
    nj_ref *tbl;

    if (cap <= vm->intern_cap) {
        return 0;   /* the capacity is 32-bit: doubling overflowed */
    }
    tbl = (nj_ref *)nj_host_alloc(vm, (nj_size)cap * (nj_size)sizeof(nj_ref));
    if (!tbl) {
        return 0;
    }
    if (vm->ninterned != 0u) {
        memcpy(tbl, vm->interned, (nj_size)vm->ninterned * sizeof(nj_ref));
    }
    if (vm->interned) {
        nj_host_free(vm, vm->interned);
    }
    vm->interned = tbl;
    vm->intern_cap = cap;
    return 1;
}

int nj_intern_reserve(nj_vm *vm, nj_u4 count)
{
    while (vm->intern_cap < count) {
        if (!intern_grow(vm)) {
            return 0;
        }
    }
    return 1;
}

nj_ref nj_intern(nj_vm *vm, nj_ref s)
{
    nj_u4 i;

    if (!vm || !s || s->kind != NJ_OBJ_STRING) {
        return s;
    }
    for (i = 0; i < vm->ninterned; i += 1u) {
        nj_ref o = vm->interned[i];
        if (o && o->length == s->length &&
            memcmp(o->u.bytes, s->u.bytes, (nj_size)s->length * sizeof(nj_u2)) == 0) {
            return o;
        }
    }
    if (vm->ninterned >= vm->intern_cap && !intern_grow(vm)) {
        /*
         * Falling back to an uninterned instance is not an option: a
         * constant-pool cache would keep the address of a String the collector
         * is free to recycle.  An allocation failure is reported as one.
         */
        (void)nj_throw(vm, "java/lang/OutOfMemoryError",
                       "the interned string table could not grow");
        return NULL;
    }
    vm->interned[vm->ninterned++] = s;
    return s;
}

/* ------------------------------------------------------------------ */
/* Public nj_vm.h wrappers                                             */
/* ------------------------------------------------------------------ */
nj_ref nj_string_new(nj_vm *vm, const char *utf8)
{
    return nj_new_string_utf8(vm, utf8, -1);
}

nj_ref nj_string_new_n(nj_vm *vm, const char *utf8, int len)
{
    return nj_new_string_utf8(vm, utf8, len);
}

nj_ref nj_string_new_chars(nj_vm *vm, const nj_u2 *chars, int len)
{
    return nj_new_string_utf16(vm, chars, len);
}

nj_ref nj_string_new_from_int(nj_vm *vm, nj_s4 v)
{
    char buf[16];
    int i = 0;
    int neg = 0;
    nj_u4 u;
    char tmp[16];
    int j;

    if (v < 0) {
        neg = 1;
        u = (nj_u4)(-(nj_s8)v);
    } else {
        u = (nj_u4)v;
    }
    do {
        tmp[i++] = (char)('0' + (u % 10u));
        u /= 10u;
    } while (u != 0u && i < (int)sizeof(tmp));
    if (neg) {
        buf[j = 0] = '-';
        j = 1;
    } else {
        j = 0;
    }
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
    return nj_new_string_ascii(vm, buf);
}

nj_ref nj_string_intern(nj_vm *vm, nj_ref s)
{
    return nj_intern(vm, s);
}

nj_u4 nj_string_length(nj_ref s)
{
    return nj_string_len(s);
}

const nj_u2 *nj_string_data(nj_ref s)
{
    return nj_string_chars(s);
}

int nj_string_to_utf8(nj_ref s, char *out, int cap)
{
    if (!out || cap <= 0) {
        return 0;
    }
    out[0] = '\0';
    if (!s || s->kind != NJ_OBJ_STRING) {
        return 0;
    }
    return nj_utf16_to_utf8(nj_string_chars(s), (int)s->length, out, cap);
}

int nj_string_equals_utf8(nj_ref s, const char *utf8)
{
    return nj_string_equals_cstr(s, utf8);
}
