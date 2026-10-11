/*
 * nano_j2me - nj_lib_string.c
 *
 * java.lang.String and java.lang.StringBuffer.
 *
 * String keeps its UTF-16 payload inside the object (see nj_string.c);
 * StringBuffer is an ordinary field-bearing object:
 *
 *     value  [C   - the character store, grown by doubling
 *     count  I    - the number of valid characters
 */
#include "nj_internal.h"

#if NJ_USE_LIBC
#  include <string.h>
#  include <stdlib.h>
#  include <stdio.h>
#endif

#include <ctype.h>

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_UNUSED(x) ((void)(x))

/* ------------------------------------------------------------------ */
/* java.lang.String                                                    */
/* ------------------------------------------------------------------ */
NJFN(str_init)
{
    static const nj_u2 empty = 0u;
    if (nj_string_set_chars(vm, NJ_THIS(args), &empty, 0u) != NJ_OK) {
        return nj_throw_oom(vm);
    }
    return NJ_OK;
}

NJFN(str_init_str)
{
    nj_ref src = NJ_ARG_REF(args, 0);
    if (!src) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (nj_string_set_chars(vm, NJ_THIS(args), nj_string_chars(src),
                            nj_string_len(src)) != NJ_OK) {
        return nj_throw_oom(vm);
    }
    return NJ_OK;
}

NJFN(str_init_chars)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    if (!arr) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (nj_string_set_chars(vm, NJ_THIS(args), (const nj_u2 *)arr->u.bytes,
                            arr->length) != NJ_OK) {
        return nj_throw_oom(vm);
    }
    return NJ_OK;
}

NJFN(str_init_chars_range)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 cnt = NJ_ARG_INT(args, 2);

    if (!arr) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (off < 0 || cnt < 0 || (nj_u4)(off + cnt) > arr->length) {
        return nj_throw(vm, "java/lang/StringIndexOutOfBoundsException", NULL);
    }
    if (nj_string_set_chars(vm, NJ_THIS(args),
                            (const nj_u2 *)arr->u.bytes + off,
                            (nj_u4)cnt) != NJ_OK) {
        return nj_throw_oom(vm);
    }
    return NJ_OK;
}

NJFN(str_init_bytes)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_u2 *tmp;
    nj_status st;

    if (!arr) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    tmp = (nj_u2 *)nj_host_alloc(vm, ((nj_size)arr->length + 1u) * sizeof(nj_u2));
    if (!tmp) {
        return nj_throw_oom(vm);
    }
    {
        nj_u4 i;
        for (i = 0u; i < arr->length; i++) {
            tmp[i] = (nj_u2)(nj_u1)arr->u.bytes[i];
        }
    }
    st = nj_string_set_chars(vm, NJ_THIS(args), tmp, arr->length);
    nj_host_free(vm, tmp);
    if (st != NJ_OK) {
        return nj_throw_oom(vm);
    }
    return NJ_OK;
}

NJFN(str_length)
{
    ret->i = (nj_s4)nj_string_len(NJ_THIS(args));
    return NJ_OK;
}

NJFN(str_charAt)
{
    nj_ref s = NJ_THIS(args);
    nj_s4 i = NJ_ARG_INT(args, 0);
    if (i < 0 || (nj_u4)i >= nj_string_len(s)) {
        return nj_throw(vm, "java/lang/StringIndexOutOfBoundsException", NULL);
    }
    ret->i = (nj_s4)nj_string_chars(s)[i];
    return NJ_OK;
}

NJFN(str_equals)
{
    nj_ref a = NJ_THIS(args);
    nj_ref b = NJ_ARG_REF(args, 0);
    if (a == b) {
        ret->i = 1;
        return NJ_OK;
    }
    if (!b || b->kind != NJ_OBJ_STRING || a->length != b->length) {
        ret->i = 0;
        return NJ_OK;
    }
    ret->i = (memcmp(nj_string_chars(a), nj_string_chars(b),
                     (nj_size)a->length * sizeof(nj_u2)) == 0) ? 1 : 0;
    return NJ_OK;
}

NJFN(str_equalsIgnoreCase)
{
    nj_ref a = NJ_THIS(args);
    nj_ref b = NJ_ARG_REF(args, 0);
    nj_u4 i;

    if (a == b) {
        ret->i = 1;
        return NJ_OK;
    }
    if (!b || b->kind != NJ_OBJ_STRING || a->length != b->length) {
        ret->i = 0;
        return NJ_OK;
    }
    for (i = 0u; i < a->length; i++) {
        nj_u2 ca = nj_string_chars(a)[i];
        nj_u2 cb = nj_string_chars(b)[i];
        if (ca != cb) {
            if (tolower((int)ca) != tolower((int)cb)) {
                ret->i = 0;
                return NJ_OK;
            }
        }
    }
    ret->i = 1;
    return NJ_OK;
}

NJFN(str_hashCode)
{
    ret->i = nj_string_hash(NJ_THIS(args));
    return NJ_OK;
}

NJFN(str_toString)
{
    ret->r = NJ_THIS(args);
    return NJ_OK;
}

NJFN(str_indexOf)
{
    nj_ref s = NJ_THIS(args);
    nj_s4 ch = NJ_ARG_INT(args, 0);
    nj_u4 i;
    nj_u4 start = 0u;

    for (i = start; i < nj_string_len(s); i++) {
        if ((nj_s4)nj_string_chars(s)[i] == ch) {
            ret->i = (nj_s4)i;
            return NJ_OK;
        }
    }
    ret->i = -1;
    return NJ_OK;
}

NJFN(str_indexOf_from)
{
    nj_ref s = NJ_THIS(args);
    nj_s4 ch = NJ_ARG_INT(args, 0);
    nj_s4 from = NJ_ARG_INT(args, 1);
    nj_u4 i;

    if (from < 0) {
        from = 0;
    }
    for (i = (nj_u4)from; i < nj_string_len(s); i++) {
        if ((nj_s4)nj_string_chars(s)[i] == ch) {
            ret->i = (nj_s4)i;
            return NJ_OK;
        }
    }
    ret->i = -1;
    return NJ_OK;
}

NJFN(str_indexOf_str)
{
    nj_ref s = NJ_THIS(args);
    nj_ref n = NJ_ARG_REF(args, 0);
    nj_u4 i, j, sl, nl;
    nj_s4 from = 0;

    if (!n) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    sl = nj_string_len(s);
    nl = nj_string_len(n);
    if (nl == 0u) {
        ret->i = 0;
        return NJ_OK;
    }
    for (i = (nj_u4)(from < 0 ? 0 : from); i + nl <= sl; i++) {
        for (j = 0u; j < nl; j++) {
            if (nj_string_chars(s)[i + j] != nj_string_chars(n)[j]) {
                break;
            }
        }
        if (j == nl) {
            ret->i = (nj_s4)i;
            return NJ_OK;
        }
    }
    ret->i = -1;
    return NJ_OK;
}

NJFN(str_indexOf_str_from)
{
    nj_ref s = NJ_THIS(args);
    nj_ref n = NJ_ARG_REF(args, 0);
    nj_s4 from = NJ_ARG_INT(args, 1);
    nj_u4 i, j, sl, nl;

    if (!n) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    sl = nj_string_len(s);
    nl = nj_string_len(n);
    if (from < 0) {
        from = 0;
    }
    if (nl == 0u) {
        ret->i = (nj_s4)((nj_u4)from <= sl ? (nj_u4)from : sl);
        return NJ_OK;
    }
    for (i = (nj_u4)from; i + nl <= sl; i++) {
        for (j = 0u; j < nl; j++) {
            if (nj_string_chars(s)[i + j] != nj_string_chars(n)[j]) {
                break;
            }
        }
        if (j == nl) {
            ret->i = (nj_s4)i;
            return NJ_OK;
        }
    }
    ret->i = -1;
    return NJ_OK;
}

NJFN(str_lastIndexOf)
{
    nj_ref s = NJ_THIS(args);
    nj_s4 ch = NJ_ARG_INT(args, 0);
    nj_u4 i = nj_string_len(s);

    while (i > 0u) {
        i--;
        if ((nj_s4)nj_string_chars(s)[i] == ch) {
            ret->i = (nj_s4)i;
            return NJ_OK;
        }
    }
    ret->i = -1;
    return NJ_OK;
}

NJFN(str_substring)
{
    nj_ref s = NJ_THIS(args);
    nj_s4 from = NJ_ARG_INT(args, 0);
    nj_s4 to = (nj_s4)nj_string_len(s);
    nj_ref out;

    if (from < 0 || (nj_u4)from > nj_string_len(s)) {
        return nj_throw(vm, "java/lang/StringIndexOutOfBoundsException", NULL);
    }
    out = nj_new_string_utf16(vm, nj_string_chars(s) + from, to - from);
    if (!out) {
        return nj_throw_oom(vm);
    }
    ret->r = out;
    return NJ_OK;
}

NJFN(str_substring_to)
{
    nj_ref s = NJ_THIS(args);
    nj_s4 from = NJ_ARG_INT(args, 0);
    nj_s4 to = NJ_ARG_INT(args, 1);
    nj_ref out;

    if (from < 0 || to > (nj_s4)nj_string_len(s) || from > to) {
        return nj_throw(vm, "java/lang/StringIndexOutOfBoundsException", NULL);
    }
    out = nj_new_string_utf16(vm, nj_string_chars(s) + from, to - from);
    if (!out) {
        return nj_throw_oom(vm);
    }
    ret->r = out;
    return NJ_OK;
}

NJFN(str_concat)
{
    nj_ref a = NJ_THIS(args);
    nj_ref b = NJ_ARG_REF(args, 0);
    nj_ref out;

    if (!b) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    {
        nj_u4 mark = nj_root_mark(vm);
        nj_root(vm, a);
        nj_root(vm, b);
        out = nj_string_concat(vm, a, b);
        nj_root_release(vm, mark);
    }
    if (!out) {
        return nj_throw_oom(vm);
    }
    ret->r = out;
    return NJ_OK;
}

NJFN(str_replace)
{
    nj_ref s = NJ_THIS(args);
    nj_s4 from = NJ_ARG_INT(args, 0);
    nj_s4 to = NJ_ARG_INT(args, 1);
    nj_ref out;
    nj_u4 i;

    {
        nj_u4 mark = nj_root_mark(vm);
        nj_u2 *tmp;
        nj_root(vm, s);
        tmp = (nj_u2 *)nj_host_alloc(vm, ((nj_size)s->length + 1u) * sizeof(nj_u2));
        if (!tmp) {
            nj_root_release(vm, mark);
            return nj_throw_oom(vm);
        }
        for (i = 0u; i < s->length; i++) {
            nj_u2 c = nj_string_chars(s)[i];
            tmp[i] = ((nj_s4)c == from) ? (nj_u2)to : c;
        }
        out = nj_new_string_utf16(vm, tmp, (int)s->length);
        nj_host_free(vm, tmp);
        nj_root_release(vm, mark);
    }
    if (!out) {
        return nj_throw_oom(vm);
    }
    ret->r = out;
    return NJ_OK;
}

/* Shared implementation for trim / toLowerCase / toUpperCase. */
static nj_ref string_map(nj_vm *vm, nj_ref s, int mode)
{
    nj_u2 *tmp;
    nj_u4 out_n = 0u;
    nj_u4 i;
    nj_ref out;

    tmp = (nj_u2 *)nj_host_alloc(vm, ((nj_size)s->length + 1u) * sizeof(nj_u2));
    if (!tmp) {
        return NULL;
    }
    if (mode == 0) {   /* trim */
        nj_u4 start = 0u;
        nj_u4 end = s->length;
        while (start < end && nj_string_chars(s)[start] <= 0x20u) { start++; }
        while (end > start && nj_string_chars(s)[end - 1u] <= 0x20u) { end--; }
        for (i = start; i < end; i++) {
            tmp[out_n++] = nj_string_chars(s)[i];
        }
    } else {
        for (i = 0u; i < s->length; i++) {
            nj_u2 c = nj_string_chars(s)[i];
            if (c < 0x80u) {
                tmp[out_n++] = (nj_u2)((mode == 1) ? tolower((int)c)
                                                   : toupper((int)c));
            } else {
                tmp[out_n++] = c;
            }
        }
    }
    out = nj_new_string_utf16(vm, tmp, (int)out_n);
    nj_host_free(vm, tmp);
    return out;
}

NJFN(str_trim)
{
    nj_ref out;
    {
        nj_u4 mark = nj_root_mark(vm);
        nj_root(vm, NJ_THIS(args));
        out = string_map(vm, NJ_THIS(args), 0);
        nj_root_release(vm, mark);
    }
    if (!out) {
        return nj_throw_oom(vm);
    }
    ret->r = out;
    return NJ_OK;
}

NJFN(str_toLowerCase)
{
    nj_ref out;
    {
        nj_u4 mark = nj_root_mark(vm);
        nj_root(vm, NJ_THIS(args));
        out = string_map(vm, NJ_THIS(args), 1);
        nj_root_release(vm, mark);
    }
    if (!out) {
        return nj_throw_oom(vm);
    }
    ret->r = out;
    return NJ_OK;
}

NJFN(str_toUpperCase)
{
    nj_ref out;
    {
        nj_u4 mark = nj_root_mark(vm);
        nj_root(vm, NJ_THIS(args));
        out = string_map(vm, NJ_THIS(args), 2);
        nj_root_release(vm, mark);
    }
    if (!out) {
        return nj_throw_oom(vm);
    }
    ret->r = out;
    return NJ_OK;
}

NJFN(str_startsWith)
{
    nj_ref s = NJ_THIS(args);
    nj_ref p = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_u4 pl;

    if (!p) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    pl = nj_string_len(p);
    if (off < 0 || (nj_u4)off + pl > nj_string_len(s)) {
        ret->i = 0;
        return NJ_OK;
    }
    ret->i = (memcmp(nj_string_chars(s) + off, nj_string_chars(p),
                     (nj_size)pl * sizeof(nj_u2)) == 0) ? 1 : 0;
    return NJ_OK;
}

NJFN(str_startsWith1)
{
    nj_ref s = NJ_THIS(args);
    nj_ref p = NJ_ARG_REF(args, 0);
    nj_u4 pl;

    if (!p) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    pl = nj_string_len(p);
    if (pl > nj_string_len(s)) {
        ret->i = 0;
        return NJ_OK;
    }
    ret->i = (memcmp(nj_string_chars(s), nj_string_chars(p),
                     (nj_size)pl * sizeof(nj_u2)) == 0) ? 1 : 0;
    return NJ_OK;
}

NJFN(str_endsWith)
{
    nj_ref s = NJ_THIS(args);
    nj_ref p = NJ_ARG_REF(args, 0);
    nj_u4 sl, pl;

    if (!p) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    sl = nj_string_len(s);
    pl = nj_string_len(p);
    if (pl > sl) {
        ret->i = 0;
        return NJ_OK;
    }
    ret->i = (memcmp(nj_string_chars(s) + (sl - pl), nj_string_chars(p),
                     (nj_size)pl * sizeof(nj_u2)) == 0) ? 1 : 0;
    return NJ_OK;
}

NJFN(str_compareTo)
{
    nj_ref a = NJ_THIS(args);
    nj_ref b = NJ_ARG_REF(args, 0);
    nj_u4 i, n;

    if (!b) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    n = (nj_string_len(a) < nj_string_len(b)) ? nj_string_len(a)
                                              : nj_string_len(b);
    for (i = 0u; i < n; i++) {
        nj_u2 ca = nj_string_chars(a)[i];
        nj_u2 cb = nj_string_chars(b)[i];
        if (ca != cb) {
            ret->i = (nj_s4)ca - (nj_s4)cb;
            return NJ_OK;
        }
    }
    ret->i = (nj_s4)nj_string_len(a) - (nj_s4)nj_string_len(b);
    return NJ_OK;
}

NJFN(str_toCharArray)
{
    nj_ref s = NJ_THIS(args);
    nj_ref a = nj_new_array(vm, NULL, nj_string_len(s), NJ_ARRAY_T_CHAR);
    if (!a) {
        return nj_throw_oom(vm);
    }
    if (s->length > 0u) {
        memcpy(a->u.bytes, nj_string_chars(s), (nj_size)s->length * sizeof(nj_u2));
    }
    ret->r = a;
    return NJ_OK;
}

NJFN(str_getChars)
{
    nj_ref s = NJ_THIS(args);
    nj_s4 src_begin = NJ_ARG_INT(args, 0);
    nj_s4 src_end = NJ_ARG_INT(args, 1);
    nj_ref dst = NJ_ARG_REF(args, 2);
    nj_s4 dst_begin = NJ_ARG_INT(args, 3);

    if (!dst) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (src_begin < 0 || src_end > (nj_s4)nj_string_len(s) ||
        src_begin > src_end || dst_begin < 0 ||
        (nj_u4)(dst_begin + (src_end - src_begin)) > dst->length) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    memcpy((nj_u2 *)dst->u.bytes + dst_begin, nj_string_chars(s) + src_begin,
           (nj_size)(src_end - src_begin) * sizeof(nj_u2));
    return NJ_OK;
}

NJFN(str_getBytes)
{
    nj_ref s = NJ_THIS(args);
    nj_ref a = nj_new_array(vm, NULL, nj_string_len(s), NJ_ARRAY_T_BYTE);
    nj_u4 i;
    if (!a) {
        return nj_throw_oom(vm);
    }
    for (i = 0u; i < s->length; i++) {
        a->u.bytes[i] = (nj_u1)(nj_string_chars(s)[i] & 0xFFu);
    }
    ret->r = a;
    return NJ_OK;
}

NJFN(str_valueOf_obj)
{
    nj_ref o = NJ_ARG_REF(args, 0);
    nj_value a1[1];
    if (!o) {
        ret->r = nj_string_new(vm, "null");
        return ret->r ? NJ_OK : nj_throw_oom(vm);
    }
    a1[0].r = o;
    ret->r = nj_call_ref(vm, o, "java/lang/Object", "toString",
                         "()Ljava/lang/String;", a1);
    if (!ret->r && !vm->exception) {
        return nj_throw_oom(vm);
    }
    return vm->exception ? NJ_ERR : NJ_OK;
}

NJFN(str_valueOf_int)
{
    ret->r = nj_string_new_from_int(vm, NJ_ARG_INT(args, 0));
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

NJFN(str_valueOf_long)
{
    nj_s8 v = NJ_ARG_LONG(args, 0);
    char buf[24];
    int i = 0;
    int neg = (v < 0);
    nj_u8 u = neg ? (nj_u8)(-(v + 1)) + 1u : (nj_u8)v;
    char tmp[24];
    int j = 0;

    while (u != 0u && i < (int)sizeof(tmp)) {
        tmp[i++] = (char)('0' + (int)(u % 10u));
        u /= 10u;
    }
    if (i == 0) {
        tmp[i++] = '0';
    }
    if (neg) {
        buf[j++] = '-';
    }
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
    ret->r = nj_string_new(vm, buf);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

NJFN(str_valueOf_bool)
{
    ret->r = nj_string_new(vm, NJ_ARG_INT(args, 0) ? "true" : "false");
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

NJFN(str_valueOf_char)
{
    nj_u2 c = (nj_u2)NJ_ARG_INT(args, 0);
    ret->r = nj_new_string_utf16(vm, &c, 1);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

NJFN(str_valueOf_float)
{
    float f = NJ_ARG_FLOAT(args, 0);
    char buf[40];
#if NJ_USE_LIBC
    snprintf(buf, sizeof(buf), "%g", (double)f);
#else
    buf[0] = '0'; buf[1] = '\0';
#endif
    ret->r = nj_string_new(vm, buf);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

/* ------------------------------------------------------------------ */
/* java.lang.StringBuffer                                              */
/* ------------------------------------------------------------------ */
static nj_field *sb_value(nj_vm *vm)
{
    return nj_field_lookup(vm, "java/lang/StringBuffer", "value", "[C", 0);
}

static nj_field *sb_count(nj_vm *vm)
{
    return nj_field_lookup(vm, "java/lang/StringBuffer", "count", "I", 0);
}

static nj_status sb_ensure(nj_vm *vm, nj_ref sb, nj_u4 need)
{
    nj_field *fv = sb_value(vm);
    nj_field *fc = sb_count(vm);
    nj_ref arr;

    if (!fv || !fc) {
        return nj_throw(vm, "java/lang/InternalError", "StringBuffer layout");
    }
    arr = nj_field_read_ref(vm, sb, fv);
    if (arr && arr->length >= need) {
        return NJ_OK;
    }
    {
        nj_u4 cap = arr ? arr->length : 16u;
        nj_ref na;
        while (cap < need) {
            cap = cap * 2u + 8u;
        }
        na = nj_new_array(vm, NULL, cap, NJ_ARRAY_T_CHAR);
        if (!na) {
            return nj_throw_oom(vm);
        }
        if (arr && arr->length > 0u) {
            memcpy(na->u.bytes, arr->u.bytes, (nj_size)arr->length * sizeof(nj_u2));
        }
        nj_field_write_ref(vm, sb, fv, na);
    }
    return NJ_OK;
}

static nj_status sb_append_chars(nj_vm *vm, nj_ref sb, const nj_u2 *s, nj_u4 n)
{
    nj_field *fv = sb_value(vm);
    nj_field *fc = sb_count(vm);
    nj_ref arr;
    nj_s4 cnt;

    if (n == 0u) {
        return NJ_OK;
    }
    cnt = nj_field_read_int(vm, sb, fc);
    if (sb_ensure(vm, sb, (nj_u4)cnt + n) != NJ_OK) {
        return NJ_ERR;
    }
    arr = nj_field_read_ref(vm, sb, fv);
    memcpy((nj_u2 *)arr->u.bytes + cnt, s, (nj_size)n * sizeof(nj_u2));
    nj_field_write_int(vm, sb, fc, cnt + (nj_s4)n);
    return NJ_OK;
}

static nj_status sb_append_cstr(nj_vm *vm, nj_ref sb, const char *c)
{
    nj_u2 buf[32];
    nj_u4 n = 0u;
    nj_status st;

    if (!c) {
        return NJ_OK;
    }
    while (*c && n < 32u) {
        buf[n++] = (nj_u2)(nj_u1)*c++;
    }
    st = sb_append_chars(vm, sb, buf, n);
    if (st != NJ_OK) {
        return st;
    }
    /* Longer strings keep going in chunks. */
    while (*c) {
        n = 0u;
        while (*c && n < 32u) {
            buf[n++] = (nj_u2)(nj_u1)*c++;
        }
        st = sb_append_chars(vm, sb, buf, n);
        if (st != NJ_OK) {
            return st;
        }
    }
    return NJ_OK;
}

NJFN(sb_init)
{
    nj_ref sb = NJ_THIS(args);
    nj_field *fv = sb_value(vm);
    nj_field *fc = sb_count(vm);
    if (!fv || !fc) {
        return nj_throw(vm, "java/lang/InternalError", "StringBuffer layout");
    }
    nj_field_write_ref(vm, sb, fv, NULL);
    nj_field_write_int(vm, sb, fc, 0);
    return sb_ensure(vm, sb, 16u);
}

NJFN(sb_init_int)
{
    nj_s4 cap = NJ_ARG_INT(args, 0);
    nj_ref sb = NJ_THIS(args);
    nj_field *fv = sb_value(vm);
    nj_field *fc = sb_count(vm);
    if (!fv || !fc) {
        return nj_throw(vm, "java/lang/InternalError", "StringBuffer layout");
    }
    if (cap < 0) {
        return nj_throw(vm, "java/lang/NegativeArraySizeException", NULL);
    }
    nj_field_write_ref(vm, sb, fv, NULL);
    nj_field_write_int(vm, sb, fc, 0);
    return sb_ensure(vm, sb, (nj_u4)cap + 1u);
}

NJFN(sb_init_str)
{
    nj_ref s = NJ_ARG_REF(args, 0);
    nj_status st = sb_init(vm, th, args, ret);
    if (st != NJ_OK) {
        return st;
    }
    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    return sb_append_chars(vm, NJ_THIS(args), nj_string_chars(s), nj_string_len(s));
}

NJFN(sb_toString)
{
    nj_ref sb = NJ_THIS(args);
    nj_field *fv = sb_value(vm);
    nj_field *fc = sb_count(vm);
    nj_ref arr;
    nj_s4 cnt;

    if (!fv || !fc) {
        return nj_throw(vm, "java/lang/InternalError", "StringBuffer layout");
    }
    arr = nj_field_read_ref(vm, sb, fv);
    cnt = nj_field_read_int(vm, sb, fc);
    {
        nj_u2 *tmp = (nj_u2 *)nj_host_alloc(vm, ((nj_size)cnt + 1u) * sizeof(nj_u2));
        nj_ref out;
        if (!tmp) {
            return nj_throw_oom(vm);
        }
        if (cnt > 0 && arr) {
            memcpy(tmp, arr->u.bytes, (nj_size)cnt * sizeof(nj_u2));
        }
        out = nj_new_string_utf16(vm, tmp, cnt);
        nj_host_free(vm, tmp);
        if (!out) {
            return nj_throw_oom(vm);
        }
        ret->r = out;
    }
    return NJ_OK;
}

NJFN(sb_length)
{
    nj_field *fc = sb_count(vm);
    ret->i = fc ? nj_field_read_int(vm, NJ_THIS(args), fc) : 0;
    return NJ_OK;
}

NJFN(sb_capacity)
{
    nj_field *fv = sb_value(vm);
    nj_ref a = fv ? nj_field_read_ref(vm, NJ_THIS(args), fv) : NULL;
    ret->i = a ? (nj_s4)a->length : 0;
    return NJ_OK;
}

NJFN(sb_ensureCapacity)
{
    nj_s4 min = NJ_ARG_INT(args, 0);
    if (min > 0) {
        (void)sb_ensure(vm, NJ_THIS(args), (nj_u4)min);
    }
    return NJ_OK;
}

NJFN(sb_append_str)
{
    nj_ref s = NJ_ARG_REF(args, 0);
    nj_status st;
    if (!s) {
        st = sb_append_cstr(vm, NJ_THIS(args), "null");
    } else {
        st = sb_append_chars(vm, NJ_THIS(args), nj_string_chars(s),
                             nj_string_len(s));
    }
    if (st == NJ_OK) {
        ret->r = NJ_THIS(args);
    }
    return st;
}

NJFN(sb_append_obj)
{
    nj_ref o = NJ_ARG_REF(args, 0);
    nj_ref s;
    nj_value a1[1];

    if (!o) {
        if (sb_append_cstr(vm, NJ_THIS(args), "null") != NJ_OK) {
            return NJ_ERR;
        }
        ret->r = NJ_THIS(args);
        return NJ_OK;
    }
    a1[0].r = o;
    s = nj_call_ref(vm, o, "java/lang/Object", "toString",
                    "()Ljava/lang/String;", a1);
    if (vm->exception) {
        return NJ_ERR;
    }
    if (!s) {
        return nj_throw_oom(vm);
    }
    if (sb_append_chars(vm, NJ_THIS(args), nj_string_chars(s),
                        nj_string_len(s)) != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = NJ_THIS(args);
    return NJ_OK;
}

NJFN(sb_append_int)
{
    nj_ref s = nj_string_new_from_int(vm, NJ_ARG_INT(args, 0));
    nj_status st;
    if (!s) {
        return nj_throw_oom(vm);
    }
    st = sb_append_chars(vm, NJ_THIS(args), nj_string_chars(s), nj_string_len(s));
    if (st == NJ_OK) {
        ret->r = NJ_THIS(args);
    }
    return st;
}

NJFN(sb_append_long)
{
    nj_value a1[2];
    a1[0].j = NJ_ARG_LONG(args, 0);
    a1[1].bits = 0u;
    {
        nj_value r;
        r.bits = 0u;
        if (nj_call_static(vm, "java/lang/String", "valueOf",
                           "(J)Ljava/lang/String;", a1, &r) != NJ_OK) {
            return NJ_ERR;
        }
        if (sb_append_chars(vm, NJ_THIS(args), nj_string_chars(r.r),
                            nj_string_len(r.r)) != NJ_OK) {
            return NJ_ERR;
        }
    }
    ret->r = NJ_THIS(args);
    return NJ_OK;
}

NJFN(sb_append_char)
{
    nj_u2 c = (nj_u2)NJ_ARG_INT(args, 0);
    if (sb_append_chars(vm, NJ_THIS(args), &c, 1u) != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = NJ_THIS(args);
    return NJ_OK;
}

NJFN(sb_append_bool)
{
    nj_status st = sb_append_cstr(vm, NJ_THIS(args),
                                  NJ_ARG_INT(args, 0) ? "true" : "false");
    if (st != NJ_OK) {
        return st;
    }
    ret->r = NJ_THIS(args);
    return NJ_OK;
}

NJFN(sb_append_float)
{
    char buf[40];
    float f = NJ_ARG_FLOAT(args, 0);
#if NJ_USE_LIBC
    snprintf(buf, sizeof(buf), "%g", (double)f);
#else
    (void)f;
    buf[0] = '0'; buf[1] = '\0';
#endif
    if (sb_append_cstr(vm, NJ_THIS(args), buf) != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = NJ_THIS(args);
    return NJ_OK;
}

NJFN(sb_append_chars_range)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);

    if (!arr) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (off < 0 || len < 0 || (nj_u4)(off + len) > arr->length) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    if (sb_append_chars(vm, NJ_THIS(args), (nj_u2 *)arr->u.bytes + off,
                        (nj_u4)len) != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = NJ_THIS(args);
    return NJ_OK;
}

NJFN(sb_charAt)
{
    nj_ref sb = NJ_THIS(args);
    nj_s4 i = NJ_ARG_INT(args, 0);
    nj_field *fv = sb_value(vm);
    nj_field *fc = sb_count(vm);
    nj_ref arr = nj_field_read_ref(vm, sb, fv);
    nj_s4 cnt = nj_field_read_int(vm, sb, fc);

    if (i < 0 || i >= cnt || !arr) {
        return nj_throw(vm, "java/lang/StringIndexOutOfBoundsException", NULL);
    }
    ret->i = (nj_s4)((nj_u2 *)arr->u.bytes)[i];
    return NJ_OK;
}

NJFN(sb_setCharAt)
{
    nj_ref sb = NJ_THIS(args);
    nj_s4 i = NJ_ARG_INT(args, 0);
    nj_s4 c = NJ_ARG_INT(args, 1);
    nj_field *fv = sb_value(vm);
    nj_field *fc = sb_count(vm);
    nj_ref arr = nj_field_read_ref(vm, sb, fv);
    nj_s4 cnt = nj_field_read_int(vm, sb, fc);

    if (i < 0 || i >= cnt || !arr) {
        return nj_throw(vm, "java/lang/StringIndexOutOfBoundsException", NULL);
    }
    ((nj_u2 *)arr->u.bytes)[i] = (nj_u2)c;
    return NJ_OK;
}

NJFN(sb_setLength)
{
    nj_ref sb = NJ_THIS(args);
    nj_s4 nl = NJ_ARG_INT(args, 0);
    nj_field *fv = sb_value(vm);
    nj_field *fc = sb_count(vm);
    nj_ref arr;
    nj_s4 cnt;

    if (nl < 0) {
        return nj_throw(vm, "java/lang/StringIndexOutOfBoundsException", NULL);
    }
    if (sb_ensure(vm, sb, (nj_u4)nl) != NJ_OK) {
        return NJ_ERR;
    }
    arr = nj_field_read_ref(vm, sb, fv);
    cnt = nj_field_read_int(vm, sb, fc);
    if (nl > cnt && arr) {
        memset((nj_u2 *)arr->u.bytes + cnt, 0,
               (nj_size)(nl - cnt) * sizeof(nj_u2));
    }
    nj_field_write_int(vm, sb, fc, nl);
    return NJ_OK;
}

NJFN(sb_reverse)
{
    nj_ref sb = NJ_THIS(args);
    nj_field *fv = sb_value(vm);
    nj_field *fc = sb_count(vm);
    nj_ref arr = nj_field_read_ref(vm, sb, fv);
    nj_s4 cnt = nj_field_read_int(vm, sb, fc);
    nj_s4 i;

    if (arr) {
        nj_u2 *p = (nj_u2 *)arr->u.bytes;
        for (i = 0; i < cnt / 2; i++) {
            nj_u2 t = p[i];
            p[i] = p[cnt - 1 - i];
            p[cnt - 1 - i] = t;
        }
    }
    ret->r = sb;
    return NJ_OK;
}

NJFN(sb_delete)
{
    nj_ref sb = NJ_THIS(args);
    nj_s4 start = NJ_ARG_INT(args, 0);
    nj_s4 end = NJ_ARG_INT(args, 1);
    nj_field *fv = sb_value(vm);
    nj_field *fc = sb_count(vm);
    nj_ref arr = nj_field_read_ref(vm, sb, fv);
    nj_s4 cnt = nj_field_read_int(vm, sb, fc);

    if (start < 0 || start > cnt || start > end) {
        return nj_throw(vm, "java/lang/StringIndexOutOfBoundsException", NULL);
    }
    if (end > cnt) {
        end = cnt;
    }
    if (arr && end > start) {
        nj_u2 *p = (nj_u2 *)arr->u.bytes;
        memmove(p + start, p + end, (nj_size)(cnt - end) * sizeof(nj_u2));
    }
    nj_field_write_int(vm, sb, fc, cnt - (end - start));
    ret->r = sb;
    return NJ_OK;
}

NJFN(sb_deleteCharAt)
{
    nj_ref sb = NJ_THIS(args);
    nj_s4 idx = NJ_ARG_INT(args, 0);
    nj_field *fv = sb_value(vm);
    nj_field *fc = sb_count(vm);
    nj_ref arr;
    nj_s4 cnt;

    if (!fv || !fc) {
        return nj_throw(vm, "java/lang/InternalError", "StringBuffer layout");
    }
    arr = nj_field_read_ref(vm, sb, fv);
    cnt = nj_field_read_int(vm, sb, fc);
    if (idx < 0 || idx >= cnt || !arr) {
        return nj_throw(vm, "java/lang/StringIndexOutOfBoundsException", NULL);
    }
    {
        nj_u2 *p = (nj_u2 *)arr->u.bytes;
        memmove(p + idx, p + idx + 1, (nj_size)(cnt - idx - 1) * sizeof(nj_u2));
    }
    nj_field_write_int(vm, sb, fc, cnt - 1);
    ret->r = sb;
    return NJ_OK;
}

NJFN(sb_insert_str)
{
    nj_ref sb = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_ref s = NJ_ARG_REF(args, 1);
    nj_field *fv = sb_value(vm);
    nj_field *fc = sb_count(vm);
    nj_ref arr;
    nj_s4 cnt;

    if (!s) {
        s = NULL;
    }
    cnt = nj_field_read_int(vm, sb, fc);
    if (at < 0 || at > cnt) {
        return nj_throw(vm, "java/lang/StringIndexOutOfBoundsException", NULL);
    }
    {
        nj_u4 n = s ? nj_string_len(s) : 4u;
        nj_status st = sb_ensure(vm, sb, (nj_u4)cnt + n);
        if (st != NJ_OK) {
            return st;
        }
        arr = nj_field_read_ref(vm, sb, fv);
        {
            nj_u2 *p = (nj_u2 *)arr->u.bytes;
            const nj_u2 *src;
            nj_u2 nullbuf[4];
            if (s) {
                src = nj_string_chars(s);
            } else {
                nullbuf[0] = 'n'; nullbuf[1] = 'u';
                nullbuf[2] = 'l'; nullbuf[3] = 'l';
                src = nullbuf;
            }
            memmove(p + at + n, p + at, (nj_size)(cnt - at) * sizeof(nj_u2));
            memcpy(p + at, src, (nj_size)n * sizeof(nj_u2));
        }
        nj_field_write_int(vm, sb, fc, cnt + (nj_s4)n);
    }
    ret->r = sb;
    return NJ_OK;
}

NJFN(sb_insert_char)
{
    nj_ref sb = NJ_THIS(args);
    nj_s4 at = NJ_ARG_INT(args, 0);
    nj_u2 c = (nj_u2)NJ_ARG_INT(args, 1);
    nj_field *fv = sb_value(vm);
    nj_field *fc = sb_count(vm);
    nj_ref arr;
    nj_s4 cnt = nj_field_read_int(vm, sb, fc);

    if (at < 0 || at > cnt) {
        return nj_throw(vm, "java/lang/StringIndexOutOfBoundsException", NULL);
    }
    if (sb_ensure(vm, sb, (nj_u4)cnt + 1u) != NJ_OK) {
        return NJ_ERR;
    }
    arr = nj_field_read_ref(vm, sb, fv);
    {
        nj_u2 *p = (nj_u2 *)arr->u.bytes;
        memmove(p + at + 1, p + at, (nj_size)(cnt - at) * sizeof(nj_u2));
        p[at] = c;
    }
    nj_field_write_int(vm, sb, fc, cnt + 1);
    ret->r = sb;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }

static const nj_builtin_method string_methods[] = {
    M("<init>",            "()V",                              0),
    M("<init>",            "(Ljava/lang/String;)V",            0),
    M("<init>",            "([C)V",                            0),
    M("<init>",            "([CII)V",                          0),
    M("<init>",            "([B)V",                            0),
    M("length",            "()I",                              0),
    M("charAt",            "(I)C",                             0),
    M("equals",            "(Ljava/lang/Object;)Z",            0),
    M("equalsIgnoreCase",  "(Ljava/lang/String;)Z",            0),
    M("hashCode",          "()I",                              0),
    M("toString",          "()Ljava/lang/String;",             0),
    M("indexOf",           "(I)I",                             0),
    M("indexOf",           "(II)I",                            0),
    M("indexOf",           "(Ljava/lang/String;)I",            0),
    M("indexOf",           "(Ljava/lang/String;I)I",           0),
    M("lastIndexOf",       "(I)I",                             0),
    M("substring",         "(I)Ljava/lang/String;",            0),
    M("substring",         "(II)Ljava/lang/String;",           0),
    M("concat",            "(Ljava/lang/String;)Ljava/lang/String;", 0),
    M("replace",           "(CC)Ljava/lang/String;",           0),
    M("trim",              "()Ljava/lang/String;",             0),
    M("toLowerCase",       "()Ljava/lang/String;",             0),
    M("toUpperCase",       "()Ljava/lang/String;",             0),
    M("startsWith",        "(Ljava/lang/String;)Z",            0),
    M("startsWith",        "(Ljava/lang/String;I)Z",           0),
    M("endsWith",          "(Ljava/lang/String;)Z",            0),
    M("compareTo",         "(Ljava/lang/String;)I",            0),
    M("toCharArray",       "()[C",                             0),
    M("getChars",          "(II[CI)V",                         0),
    M("getBytes",          "()[B",                             0),
    M("valueOf",           "(Ljava/lang/Object;)Ljava/lang/String;", NJ_ACC_STATIC),
    M("valueOf",           "(I)Ljava/lang/String;",            NJ_ACC_STATIC),
    M("valueOf",           "(J)Ljava/lang/String;",            NJ_ACC_STATIC),
    M("valueOf",           "(Z)Ljava/lang/String;",            NJ_ACC_STATIC),
    M("valueOf",           "(C)Ljava/lang/String;",            NJ_ACC_STATIC),
    M("valueOf",           "(F)Ljava/lang/String;",            NJ_ACC_STATIC)
};

/*
 * StringBuilder: the same operations as StringBuffer with the covariant
 * return type javac actually calls.  Declared as its own table so the two
 * descriptor sets can drift independently if they ever need to.
 */
static const nj_builtin_method stringbuilder_methods[] = {
    M("<init>",           "()V",                                 0),
    M("<init>",           "(I)V",                                0),
    M("<init>",           "(Ljava/lang/String;)V",               0),
    M("toString",         "()Ljava/lang/String;",                0),
    M("length",           "()I",                                 0),
    M("capacity",         "()I",                                 0),
    M("ensureCapacity",   "(I)V",                                0),
    M("charAt",           "(I)C",                                0),
    M("setCharAt",        "(IC)V",                               0),
    M("setLength",        "(I)V",                                0),
    M("append",           "(Ljava/lang/Object;)Ljava/lang/StringBuilder;", 0),
    M("append",           "(Ljava/lang/String;)Ljava/lang/StringBuilder;", 0),
    M("append",           "(I)Ljava/lang/StringBuilder;",         0),
    M("append",           "(J)Ljava/lang/StringBuilder;",         0),
    M("append",           "(C)Ljava/lang/StringBuilder;",         0),
    M("append",           "(Z)Ljava/lang/StringBuilder;",         0),
    M("append",           "(F)Ljava/lang/StringBuilder;",         0),
    M("append",           "([CII)Ljava/lang/StringBuilder;",      0),
    M("reverse",          "()Ljava/lang/StringBuilder;",          0),
    M("delete",           "(II)Ljava/lang/StringBuilder;",        0),
    M("deleteCharAt",     "(I)Ljava/lang/StringBuilder;",         0),
    M("insert",           "(ILjava/lang/String;)Ljava/lang/StringBuilder;", 0),
    M("insert",           "(IC)Ljava/lang/StringBuilder;",        0)
};

static const nj_builtin_field stringbuffer_fields[] = {
    { "value", "[C", 0 },
    { "count", "I",  0 }
};

static const nj_builtin_method stringbuffer_methods[] = {
    M("<init>",           "()V",                                 0),
    M("<init>",           "(I)V",                                0),
    M("<init>",           "(Ljava/lang/String;)V",               0),
    M("toString",         "()Ljava/lang/String;",                0),
    M("length",           "()I",                                 0),
    M("capacity",         "()I",                                 0),
    M("ensureCapacity",   "(I)V",                                0),
    M("charAt",           "(I)C",                                0),
    M("setCharAt",        "(IC)V",                               0),
    M("setLength",        "(I)V",                                0),
    M("append",           "(Ljava/lang/Object;)Ljava/lang/StringBuffer;", 0),
    M("append",           "(Ljava/lang/String;)Ljava/lang/StringBuffer;", 0),
    M("append",           "(I)Ljava/lang/StringBuffer;",         0),
    M("append",           "(J)Ljava/lang/StringBuffer;",         0),
    M("append",           "(C)Ljava/lang/StringBuffer;",         0),
    M("append",           "(Z)Ljava/lang/StringBuffer;",         0),
    M("append",           "(F)Ljava/lang/StringBuffer;",         0),
    M("append",           "([CII)Ljava/lang/StringBuffer;",      0),
    M("reverse",          "()Ljava/lang/StringBuffer;",          0),
    M("delete",           "(II)Ljava/lang/StringBuffer;",        0),
    M("deleteCharAt",     "(I)Ljava/lang/StringBuffer;",         0),
    M("insert",           "(ILjava/lang/String;)Ljava/lang/StringBuffer;", 0),
    M("insert",           "(IC)Ljava/lang/StringBuffer;",        0)
};

/*
 * java.lang.StringBuilder does not exist in CLDC, but every modern javac
 * compiles `a + b` on Strings into a StringBuilder chain, so a JAR built with
 * a recent toolchain needs it.
 *
 * It inherits StringBuffer's FIELDS, but it cannot simply inherit its methods.
 * In Java SE, StringBuilder declares its own append/insert/delete/reverse with
 * a covariant return type (StringBuilder rather than StringBuffer), and javac
 * emits that descriptor at the call site:
 *
 *     invokevirtual java/lang/StringBuilder.append(Ljava/lang/String;)Ljava/lang/StringBuilder;
 *
 * whereas StringBuffer declares
 *
 *     invokevirtual java/lang/StringBuffer.append(Ljava/lang/String;)Ljava/lang/StringBuffer;
 *
 * A JVM distinguishes methods by name AND descriptor, so the covariant
 * override appears as a distinct method that has to exist.  The descriptors
 * therefore have to be duplicated here even though the C implementations are
 * shared - `ret_ptype` is OBJECT either way, so the same native can serve both
 * without knowing which it was reached through.
 */
const nj_builtin_class nj_lib_string_classes[] = {
    { "java/lang/String", "java/lang/Object",
      NJ_ACC_PUBLIC | NJ_ACC_FINAL, NULL, 0u, string_methods,
      (nj_u2)(sizeof(string_methods) / sizeof(string_methods[0])) },
    { "java/lang/StringBuilder", "java/lang/StringBuffer",
      NJ_ACC_PUBLIC | NJ_ACC_FINAL, NULL, 0u, stringbuilder_methods,
      (nj_u2)(sizeof(stringbuilder_methods) / sizeof(stringbuilder_methods[0])) },
    { "java/lang/StringBuffer", "java/lang/Object", NJ_ACC_PUBLIC,
      stringbuffer_fields,
      (nj_u2)(sizeof(stringbuffer_fields) / sizeof(stringbuffer_fields[0])),
      stringbuffer_methods,
      (nj_u2)(sizeof(stringbuffer_methods) / sizeof(stringbuffer_methods[0])) }
};
const int nj_lib_string_class_count =
    (int)(sizeof(nj_lib_string_classes) / sizeof(nj_lib_string_classes[0]));

const nj_native_entry nj_lib_string_table[] = {
    { "java/lang/String", "<init>",           "()V",                     str_init },
    { "java/lang/String", "<init>",           "(Ljava/lang/String;)V",   str_init_str },
    { "java/lang/String", "<init>",           "([C)V",                   str_init_chars },
    { "java/lang/String", "<init>",           "([CII)V",                 str_init_chars_range },
    { "java/lang/String", "<init>",           "([B)V",                   str_init_bytes },
    { "java/lang/String", "length",           "()I",                     str_length },
    { "java/lang/String", "charAt",           "(I)C",                    str_charAt },
    { "java/lang/String", "equals",           "(Ljava/lang/Object;)Z",   str_equals },
    { "java/lang/String", "equalsIgnoreCase", "(Ljava/lang/String;)Z",   str_equalsIgnoreCase },
    { "java/lang/String", "hashCode",         "()I",                     str_hashCode },
    { "java/lang/String", "toString",         "()Ljava/lang/String;",    str_toString },
    { "java/lang/String", "indexOf",          "(I)I",                    str_indexOf },
    { "java/lang/String", "indexOf",          "(II)I",                   str_indexOf_from },
    { "java/lang/String", "indexOf",          "(Ljava/lang/String;)I",   str_indexOf_str },
    { "java/lang/String", "indexOf",          "(Ljava/lang/String;I)I",  str_indexOf_str_from },
    { "java/lang/String", "lastIndexOf",      "(I)I",                    str_lastIndexOf },
    { "java/lang/String", "substring",        "(I)Ljava/lang/String;",   str_substring },
    { "java/lang/String", "substring",        "(II)Ljava/lang/String;",  str_substring_to },
    { "java/lang/String", "concat",           "(Ljava/lang/String;)Ljava/lang/String;", str_concat },
    { "java/lang/String", "replace",          "(CC)Ljava/lang/String;",  str_replace },
    { "java/lang/String", "trim",             "()Ljava/lang/String;",    str_trim },
    { "java/lang/String", "toLowerCase",      "()Ljava/lang/String;",    str_toLowerCase },
    { "java/lang/String", "toUpperCase",      "()Ljava/lang/String;",    str_toUpperCase },
    { "java/lang/String", "startsWith",       "(Ljava/lang/String;)Z",   str_startsWith1 },
    { "java/lang/String", "startsWith",       "(Ljava/lang/String;I)Z",  str_startsWith },
    { "java/lang/String", "endsWith",         "(Ljava/lang/String;)Z",   str_endsWith },
    { "java/lang/String", "compareTo",        "(Ljava/lang/String;)I",   str_compareTo },
    { "java/lang/String", "toCharArray",      "()[C",                    str_toCharArray },
    { "java/lang/String", "getChars",         "(II[CI)V",                str_getChars },
    { "java/lang/String", "getBytes",         "()[B",                    str_getBytes },
    { "java/lang/String", "valueOf",          "(Ljava/lang/Object;)Ljava/lang/String;", str_valueOf_obj },
    { "java/lang/String", "valueOf",          "(I)Ljava/lang/String;",   str_valueOf_int },
    { "java/lang/String", "valueOf",          "(J)Ljava/lang/String;",   str_valueOf_long },
    { "java/lang/String", "valueOf",          "(Z)Ljava/lang/String;",   str_valueOf_bool },
    { "java/lang/String", "valueOf",          "(C)Ljava/lang/String;",   str_valueOf_char },
    { "java/lang/String", "valueOf",          "(F)Ljava/lang/String;",   str_valueOf_float },

    { "java/lang/StringBuffer", "<init>",         "()V",                    sb_init },
    { "java/lang/StringBuffer", "<init>",         "(I)V",                   sb_init_int },
    { "java/lang/StringBuffer", "<init>",         "(Ljava/lang/String;)V",  sb_init_str },
    { "java/lang/StringBuffer", "toString",       "()Ljava/lang/String;",   sb_toString },
    { "java/lang/StringBuffer", "length",         "()I",                    sb_length },
    { "java/lang/StringBuffer", "capacity",       "()I",                    sb_capacity },
    { "java/lang/StringBuffer", "ensureCapacity", "(I)V",                   sb_ensureCapacity },
    { "java/lang/StringBuffer", "charAt",         "(I)C",                   sb_charAt },
    { "java/lang/StringBuffer", "setCharAt",      "(IC)V",                  sb_setCharAt },
    { "java/lang/StringBuffer", "setLength",      "(I)V",                   sb_setLength },
    { "java/lang/StringBuffer", "append",         "(Ljava/lang/Object;)Ljava/lang/StringBuffer;", sb_append_obj },
    { "java/lang/StringBuffer", "append",         "(Ljava/lang/String;)Ljava/lang/StringBuffer;", sb_append_str },
    { "java/lang/StringBuffer", "append",         "(I)Ljava/lang/StringBuffer;", sb_append_int },
    { "java/lang/StringBuffer", "append",         "(J)Ljava/lang/StringBuffer;", sb_append_long },
    { "java/lang/StringBuffer", "append",         "(C)Ljava/lang/StringBuffer;", sb_append_char },
    { "java/lang/StringBuffer", "append",         "(Z)Ljava/lang/StringBuffer;", sb_append_bool },
    { "java/lang/StringBuffer", "append",         "(F)Ljava/lang/StringBuffer;", sb_append_float },
    { "java/lang/StringBuffer", "append",         "([CII)Ljava/lang/StringBuffer;", sb_append_chars_range },
    { "java/lang/StringBuffer", "reverse",        "()Ljava/lang/StringBuffer;", sb_reverse },
    { "java/lang/StringBuffer", "delete",         "(II)Ljava/lang/StringBuffer;", sb_delete },
    { "java/lang/StringBuffer", "deleteCharAt",   "(I)Ljava/lang/StringBuffer;", sb_deleteCharAt },
    { "java/lang/StringBuffer", "insert",         "(ILjava/lang/String;)Ljava/lang/StringBuffer;", sb_insert_str },
    { "java/lang/StringBuffer", "insert",         "(IC)Ljava/lang/StringBuffer;", sb_insert_char },

    /* StringBuilder: the covariant descriptors javac emits.  Same natives as
     * StringBuffer - the return type is OBJECT either way. */
    { "java/lang/StringBuilder", "<init>",       "()V",                    sb_init },
    { "java/lang/StringBuilder", "<init>",       "(I)V",                   sb_init_int },
    { "java/lang/StringBuilder", "<init>",       "(Ljava/lang/String;)V",  sb_init_str },
    { "java/lang/StringBuilder", "toString",     "()Ljava/lang/String;",   sb_toString },
    { "java/lang/StringBuilder", "length",       "()I",                    sb_length },
    { "java/lang/StringBuilder", "capacity",     "()I",                    sb_capacity },
    { "java/lang/StringBuilder", "ensureCapacity","(I)V",                  sb_ensureCapacity },
    { "java/lang/StringBuilder", "charAt",       "(I)C",                   sb_charAt },
    { "java/lang/StringBuilder", "setCharAt",    "(IC)V",                  sb_setCharAt },
    { "java/lang/StringBuilder", "setLength",    "(I)V",                   sb_setLength },
    { "java/lang/StringBuilder", "append",       "(Ljava/lang/Object;)Ljava/lang/StringBuilder;", sb_append_obj },
    { "java/lang/StringBuilder", "append",       "(Ljava/lang/String;)Ljava/lang/StringBuilder;", sb_append_str },
    { "java/lang/StringBuilder", "append",       "(I)Ljava/lang/StringBuilder;", sb_append_int },
    { "java/lang/StringBuilder", "append",       "(J)Ljava/lang/StringBuilder;", sb_append_long },
    { "java/lang/StringBuilder", "append",       "(C)Ljava/lang/StringBuilder;", sb_append_char },
    { "java/lang/StringBuilder", "append",       "(Z)Ljava/lang/StringBuilder;", sb_append_bool },
    { "java/lang/StringBuilder", "append",       "(F)Ljava/lang/StringBuilder;", sb_append_float },
    { "java/lang/StringBuilder", "append",       "([CII)Ljava/lang/StringBuilder;", sb_append_chars_range },
    { "java/lang/StringBuilder", "reverse",      "()Ljava/lang/StringBuilder;", sb_reverse },
    { "java/lang/StringBuilder", "delete",       "(II)Ljava/lang/StringBuilder;", sb_delete },
    { "java/lang/StringBuilder", "deleteCharAt", "(I)Ljava/lang/StringBuilder;", sb_deleteCharAt },
    { "java/lang/StringBuilder", "insert",       "(ILjava/lang/String;)Ljava/lang/StringBuilder;", sb_insert_str },
    { "java/lang/StringBuilder", "insert",       "(IC)Ljava/lang/StringBuilder;", sb_insert_char },
};
const int nj_lib_string_count =
    (int)(sizeof(nj_lib_string_table) / sizeof(nj_lib_string_table[0]));
