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


#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_UNUSED(x) ((void)(x))

/* ------------------------------------------------------------------ */
/* java.lang.String                                                    */
/* ------------------------------------------------------------------ */

/*
 * Install the characters a String constructor just produced.
 *
 * `new` hands out a plain instance, because the kind tag describes an object's
 * representation and a String has none until its constructor runs - so the
 * constructor is what has to say what the object became.  nj_string_set_chars
 * (which allocates the char[] and sets NJ_STR_WRAPPED) insists the kind already
 * be STRING, and nothing in the runtime ever set it for a `new`ed String: the
 * only writer was the internal factory.  Every one of the six constructors
 * therefore failed with NJ_ERR, which their callers could only report as
 * OutOfMemoryError.  A corpus title hit exactly that, inside
 * new String(byte[], "UTF-8"), and reported it as an out-of-memory at 400 KB.
 */
static nj_status str_install(nj_vm *vm, nj_ref s, const nj_u2 *chars, nj_u4 len)
{
    if (s) {
        s->kind = NJ_OBJ_STRING;
    }
    return nj_string_set_chars(vm, s, chars, len);
}

NJFN(str_init)
{
    static const nj_u2 empty = 0u;
    if (str_install(vm, NJ_THIS(args), &empty, 0u) != NJ_OK) {
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
    if (str_install(vm, NJ_THIS(args), nj_string_chars(src),
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
    if (str_install(vm, NJ_THIS(args), (const nj_u2 *)arr->u.bytes,
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
    if (str_install(vm, NJ_THIS(args),
                            (const nj_u2 *)arr->u.bytes + off,
                            (nj_u4)cnt) != NJ_OK) {
        return nj_throw_oom(vm);
    }
    return NJ_OK;
}

/*
 * String(byte[], String encoding).
 *
 * A game that reads a config file or a save block does this constantly, and its
 * absence is a NoSuchMethodError at the first read.  The runtime advertises
 * microedition.encoding as ISO-8859-1 through System.getProperty, so ISO-8859-1
 * and its aliases decode exactly; UTF-8 is decoded properly as well because it
 * is what a game written after 2005 means by "UTF-8" even though the platform
 * property says otherwise.  ANYTHING ELSE REFUSES by name rather than silently
 * decoding as Latin-1, because a game that asked for Shift_JIS and got mojibake
 * has no way to find out where it went wrong.
 */
/* One spelling of an encoding name, and which decoder it asks for. */
typedef struct { const char *name; int kind; } str_encoding;

/*
 * ENCODING NAMES ARE CASE-INSENSITIVE in Java: `getBytes("utf-8")` and
 * `new String(b, "iso8859_1")` are the same calls as their upper-case
 * spellings.  A strcmp here made the first of those an
 * UnsupportedEncodingException, which a title reads as "this file is not what
 * I thought" and usually answers with garbage or a crash - and the message
 * blamed the game's own resource.  It is not a rare spelling: 48 of the
 * collected titles carry a lower- or mixed-case "utf-8"/"utf8" in their class
 * files, and 疯狂战机_2013 threw here on every frame of a run.
 *
 * The table is the alias set a J2ME title actually writes - the platform
 * encoding this runtime advertises and every spelling of it seen in a jar, the
 * ASCII family, and UTF-8.  Anything else still refuses BY NAME, because a
 * game that asked for Shift_JIS and got Latin-1 has no way to find out.
 */
static const str_encoding str_encodings[] = {
    { "ISO-8859-1",  0 }, { "ISO8859-1",  0 }, { "ISO8859_1",  0 },
    { "ISO_8859-1",  0 }, { "ISO_8859_1", 0 }, { "8859_1",     0 },
    { "8859-1",      0 }, { "ISO-IR-100", 0 }, { "LATIN1",     0 },
    { "LATIN-1",     0 }, { "L1",         0 }, { "CP819",      0 },
    { "IBM819",      0 }, { "819",        0 }, { "CSISOLATIN1", 0 },
    { "ASCII",       0 }, { "US-ASCII",   0 }, { "USASCII",    0 },
    { "ISO646-US",   0 }, { "ISO-IR-6",   0 }, { "646",        0 },
    { "IBM367",      0 }, { "CP367",      0 }, { "CSASCII",    0 },
    { "ANSI_X3.4-1968", 0 }, { "ANSI_X3.4-1986", 0 },
    { "UTF-8",       1 }, { "UTF8",       1 }, { "UTF_8",      1 },
    { "UNICODE-1-1-UTF-8", 1 }
};

/* "is this Java string the ASCII name `name`, ignoring case?"  A length check
 * first, so a name longer than any entry cannot match a prefix of one. */
static int str_name_is(const nj_u2 *c, nj_u4 n, const char *name)
{
    nj_u4 i;

    for (i = 0u; i < n; i++) {
        int a = (int)c[i];
        int b = (unsigned char)name[i];

        if (b == 0) {
            return 0;
        }
        if (a >= 'a' && a <= 'z') {
            a -= 32;
        }
        if (b >= 'a' && b <= 'z') {
            b -= 32;
        }
        if (a != b) {
            return 0;
        }
    }
    return name[n] == '\0';
}

/* Which decoder an encoding name asks for.  0 = one byte per char (ISO-8859-1
 * and the aliases of it this runtime advertises), 1 = UTF-8, -1 = refused by
 * name, with the exception already raised.  The no-argument overloads are
 * separate natives, so a NULL reference here can only be a game passing null,
 * which the spec answers with a NullPointerException - decoding it as the
 * platform encoding instead would hide the game's mistake and hand it text it
 * did not ask for. */
static int str_encoding_kind(nj_vm *vm, nj_ref enc)
{
    const nj_u2 *c;
    nj_u4 n;
    nj_u4 i;

    if (!enc) {
        (void)nj_throw(vm, "java/lang/NullPointerException", NULL);
        return -1;
    }
    c = nj_string_chars(enc);
    n = nj_string_len(enc);
    for (i = 0u; i < (nj_u4)(sizeof(str_encodings) / sizeof(str_encodings[0]));
         i++) {
        if (str_name_is(c, n, str_encodings[i].name)) {
            return str_encodings[i].kind;
        }
    }
    {
        char name[32];

        if (nj_string_to_utf8(enc, name, (int)sizeof(name)) <= 0) {
            return 0;
        }
        (void)nj_throw(vm, "java/io/UnsupportedEncodingException", name);
    }
    return -1;
}

/* Bytes [off, off+len) of `arr`, one byte per char.  The caller has already
 * checked the range, so this only has to allocate. */
static nj_status str_decode_latin1(nj_vm *vm, nj_ref self, nj_ref arr,
                                   nj_u4 off, nj_u4 len)
{
    nj_u2 *tmp;
    nj_status st;
    nj_u4 i;

    tmp = (nj_u2 *)nj_host_alloc(vm, ((nj_size)len + 1u) * sizeof(nj_u2));
    if (!tmp) {
        return nj_throw_oom(vm);
    }
    for (i = 0u; i < len; i++) {
        tmp[i] = (nj_u2)(nj_u1)arr->u.bytes[off + i];
    }
    st = str_install(vm, self, tmp, len);
    nj_host_free(vm, tmp);
    if (st != NJ_OK) {
        return nj_throw_oom(vm);
    }
    return NJ_OK;
}

/* Bytes [off, off+len) of `arr`, decoded as UTF-8.  A malformed sequence
 * becomes one U+FFFD and does not eat the byte after it, so one bad byte in a
 * save file costs one character instead of the rest of the text.  Four-byte
 * sequences become a surrogate pair, because the string is UTF-16. */
static nj_status str_decode_utf8(nj_vm *vm, nj_ref self, nj_ref arr,
                                 nj_u4 off, nj_u4 len)
{
    nj_u2 *tmp;
    nj_u4 i, n = 0u, end = off + len;
    nj_status st;

    tmp = (nj_u2 *)nj_host_alloc(vm, ((nj_size)len + 1u) * sizeof(nj_u2));
    if (!tmp) {
        return nj_throw_oom(vm);
    }

    i = off;
    while (i < end) {
        nj_u4 c = (nj_u4)(nj_u1)arr->u.bytes[i++];

        if (c < 0x80u) {
            tmp[n++] = (nj_u2)c;
        } else if ((c & 0xE0u) == 0xC0u && i < end &&
                   (arr->u.bytes[i] & 0xC0u) == 0x80u) {
            nj_u4 c2 = (nj_u4)(nj_u1)arr->u.bytes[i++];
            tmp[n++] = (nj_u2)(((c & 0x1Fu) << 6) | (c2 & 0x3Fu));
        } else if ((c & 0xF0u) == 0xE0u && i + 1u < end &&
                   (arr->u.bytes[i] & 0xC0u) == 0x80u &&
                   (arr->u.bytes[i + 1u] & 0xC0u) == 0x80u) {
            nj_u4 c2 = (nj_u4)(nj_u1)arr->u.bytes[i++];
            nj_u4 c3 = (nj_u4)(nj_u1)arr->u.bytes[i++];
            tmp[n++] = (nj_u2)(((c & 0x0Fu) << 12) |
                               ((c2 & 0x3Fu) << 6) | (c3 & 0x3Fu));
        } else if ((c & 0xF8u) == 0xF0u && i + 2u < end &&
                   (arr->u.bytes[i] & 0xC0u) == 0x80u &&
                   (arr->u.bytes[i + 1u] & 0xC0u) == 0x80u &&
                   (arr->u.bytes[i + 2u] & 0xC0u) == 0x80u) {
            nj_u4 c2 = (nj_u4)(nj_u1)arr->u.bytes[i++];
            nj_u4 c3 = (nj_u4)(nj_u1)arr->u.bytes[i++];
            nj_u4 c4 = (nj_u4)(nj_u1)arr->u.bytes[i++];
            nj_u4 cp = ((c & 0x07u) << 18) | ((c2 & 0x3Fu) << 12) |
                       ((c3 & 0x3Fu) << 6) | (c4 & 0x3Fu);
            cp -= 0x10000u;
            tmp[n++] = (nj_u2)(0xD800u + (cp >> 10));
            tmp[n++] = (nj_u2)(0xDC00u + (cp & 0x3FFu));
        } else {
            tmp[n++] = 0xFFFDu;
        }
    }

    st = str_install(vm, self, tmp, n);
    nj_host_free(vm, tmp);
    if (st != NJ_OK) {
        return nj_throw_oom(vm);
    }
    return NJ_OK;
}

/* offset/count on a byte[] or char[] constructor.  The spec's exception for a
 * range that does not fit is an index one, and games catch that. */
static nj_status str_range_check(nj_vm *vm, nj_ref arr, nj_s4 off, nj_s4 cnt)
{
    if (off < 0 || cnt < 0 || (nj_u4)off + (nj_u4)cnt > arr->length) {
        (void)nj_throw(vm, "java/lang/StringIndexOutOfBoundsException", NULL);
        return NJ_ERR;
    }
    return NJ_OK;
}

NJFN(str_init_bytes_enc)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    int kind;

    if (!arr) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    kind = str_encoding_kind(vm, NJ_ARG_REF(args, 1));
    if (kind < 0) {
        return NJ_ERR;
    }
    if (kind == 1) {
        return str_decode_utf8(vm, NJ_THIS(args), arr, 0u, arr->length);
    }
    return str_decode_latin1(vm, NJ_THIS(args), arr, 0u, arr->length);
}

/* String(byte[], int offset, int count) - the ranged form, which is how a game
 * decodes one field out of a save block or a packet without copying it out
 * first.  11 titles in the corpus call it. */
NJFN(str_init_bytes_range)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 cnt = NJ_ARG_INT(args, 2);

    if (!arr) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (str_range_check(vm, arr, off, cnt) != NJ_OK) {
        return NJ_ERR;
    }
    return str_decode_latin1(vm, NJ_THIS(args), arr, (nj_u4)off, (nj_u4)cnt);
}

/* ... and the same with the encoding named. */
NJFN(str_init_bytes_range_enc)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 cnt = NJ_ARG_INT(args, 2);
    int kind;

    if (!arr) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    kind = str_encoding_kind(vm, NJ_ARG_REF(args, 3));
    if (kind < 0) {
        return NJ_ERR;
    }
    if (str_range_check(vm, arr, off, cnt) != NJ_OK) {
        return NJ_ERR;
    }
    if (kind == 1) {
        return str_decode_utf8(vm, NJ_THIS(args), arr, (nj_u4)off, (nj_u4)cnt);
    }
    return str_decode_latin1(vm, NJ_THIS(args), arr, (nj_u4)off, (nj_u4)cnt);
}

NJFN(str_init_bytes)
{
    nj_ref arr = NJ_ARG_REF(args, 0);

    if (!arr) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    return str_decode_latin1(vm, NJ_THIS(args), arr, 0u, arr->length);
}

/*
 * String(byte[], "UTF-8").
 *
 * Decoded here rather than aliased to the byte-per-char path above, because the
 * two agree only below 0x80 and a title whose Chinese text is UTF-8 renders as
 * mojibake without it - the corpus found two such titles on the first sweep.
 *
 * UTF-8 never expands on the way to UTF-16: the widest sequence, four bytes, is
 * two code units (a surrogate pair), so the input length is always a sufficient
 * bound and no second pass is needed.  A malformed sequence becomes U+FFFD, one
 * per bad sequence, which is what a JVM's decoder substitutes; refusing the
 * whole string instead would lose the good text around one bad byte.  Overlong
 * forms are accepted rather than rejected, because the platform is defined to
 * speak modified UTF-8 elsewhere (C0 80 is how a DataOutput stream writes NUL)
 * and a title's resource files are written by its own toolchain.
 */
/* String(StringBuffer) - StringBuffer has no pack() in CLDC, so a game that
 * built a line in one converts it this way. */
static nj_field *sb_value(nj_vm *vm);
static nj_field *sb_count(nj_vm *vm);

NJFN(str_init_sb)
{
    nj_ref sb = NJ_ARG_REF(args, 0);
    nj_field *fv = sb_value(vm);
    nj_field *fc = sb_count(vm);
    nj_ref arr;
    nj_s4 cnt;

    if (!sb) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (!fv || !fc) {
        return nj_throw(vm, "java/lang/InternalError", "StringBuffer layout");
    }
    arr = nj_field_read_ref(vm, sb, fv);
    cnt = nj_field_read_int(vm, sb, fc);
    if (!arr || cnt <= 0) {
        return str_init(vm, th, args, ret);
    }
    if ((nj_u4)cnt > arr->length) {
        cnt = (nj_s4)arr->length;
    }
    if (str_install(vm, NJ_THIS(args), (const nj_u2 *)arr->u.bytes,
                    (nj_u4)cnt) != NJ_OK) {
        return nj_throw_oom(vm);
    }
    return NJ_OK;
}

/* String.valueOf(char[]) */
NJFN(str_valueOf_chars)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_ref s;

    if (!arr) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    s = nj_string_new(vm, "");
    if (!s) {
        return nj_throw_oom(vm);
    }
    if (str_install(vm, s, (const nj_u2 *)arr->u.bytes, arr->length) != NJ_OK) {
        return nj_throw_oom(vm);
    }
    ret->r = s;
    return NJ_OK;
}

/* lastIndexOf(int ch, int fromIndex): the search starts at fromIndex and walks
 * back, so an index past the end starts at the end, and a negative one finds
 * nothing. */
NJFN(str_lastIndexOf_from)
{
    nj_ref s = NJ_THIS(args);
    nj_s4 ch = NJ_ARG_INT(args, 0);
    nj_s4 from = NJ_ARG_INT(args, 1);
    nj_u4 i = nj_string_len(s);

    if (from < 0) {
        ret->i = -1;
        return NJ_OK;
    }
    if ((nj_u4)from < i) {
        i = (nj_u4)from + 1u;
    }
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

NJFN(str_length)
{
    ret->i = (nj_s4)nj_string_len(NJ_THIS(args));
    return NJ_OK;
}

/*
 * Case folding for regionMatches.  Only ASCII is folded, which is what the
 * reference implementation does for the ranges a MIDlet can reach without a
 * locale; a Turkish dotless i is not something this runtime can answer.
 */
static nj_u2 rm_fold(nj_u2 c)
{
    return (c >= 'A' && c <= 'Z') ? (nj_u2)(c + 32u) : c;
}

/*
 * Shared body for both overloads.  With five arguments the first is the
 * case-insensitive flag; with four there is no flag and the comparison is
 * exact.  A negative or out-of-range window is not an exception: the Java
 * contract says the answer is simply false.
 */
static nj_status region_matches(nj_vm *vm, nj_value *args, int argc, nj_value *ret)
{
    nj_ref self = NJ_THIS(args);
    int i = (argc == 5) ? 1 : 0;
    int fold = (argc == 5) ? (NJ_ARG_INT(args, 0) ? 1 : 0) : 0;
    nj_s4 toff, ooff, len, k;
    nj_ref other;
    const nj_u2 *a, *b;
    nj_u4 n, m;

    toff = NJ_ARG_INT(args, i + 0);
    other = NJ_ARG_REF(args, i + 1);
    ooff = NJ_ARG_INT(args, i + 2);
    len = NJ_ARG_INT(args, i + 3);
    if (!other) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    n = nj_string_len(self);
    m = nj_string_len(other);
    if (len < 0 || toff < 0 || ooff < 0 ||
        (nj_u4)toff + (nj_u4)len > n || (nj_u4)ooff + (nj_u4)len > m) {
        ret->i = 0;
        return NJ_OK;
    }
    a = nj_string_chars(self);
    b = nj_string_chars(other);
    for (k = 0; k < len; k++) {
        nj_u2 ca = a[toff + k];
        nj_u2 cb = b[ooff + k];

        if (ca == cb) {
            continue;
        }
        if (!fold || rm_fold(ca) != rm_fold(cb)) {
            ret->i = 0;
            return NJ_OK;
        }
    }
    ret->i = 1;
    return NJ_OK;
}

NJFN(str_regionMatches)
{
    return region_matches(vm, args, 4, ret);
}

NJFN(str_regionMatches_ci)
{
    return region_matches(vm, args, 5, ret);
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
    if (a->length == 0u) {
        ret->i = 1;                       /* two empty strings are equal */
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

/*
 * Compare the first `n` characters of two strings.
 *
 * The C standard requires memcmp's pointers to be valid even when the length
 * is zero, and this runtime stores an empty string as a NULL payload - and an
 * argument that is not a String at all has no characters and no pointer.  Both
 * make `memcmp(x, NULL, 0)` undefined behaviour; UBSan reports it as "null
 * pointer passed as argument 2, which is declared to never be null".
 *
 * A title in the corpus (the UC browser) calls String.startsWith with an
 * int[] - it is obfuscated code calling a String method on an array - and the
 * undefined behaviour fired on every such call.
 *
 * Returns 1 when the characters are equal.
 */
static int nj_string_prefix_eq(const nj_ref a, const nj_ref b, nj_u4 n)
{
    const nj_u2 *ca;
    const nj_u2 *cb;

    if (n == 0u) {
        return 1;                       /* every string starts with "" */
    }
    ca = nj_string_chars(a);
    cb = nj_string_chars(b);
    if (!ca || !cb) {
        return 0;                       /* no characters to compare */
    }
    return memcmp(ca, cb, (nj_size)n * sizeof(nj_u2)) == 0 ? 1 : 0;
}

NJFN(str_startsWith)
{
    nj_ref s = NJ_THIS(args);
    nj_ref p = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_u4 pl;
    const nj_u2 *sc;
    const nj_u2 *pc;

    if (!p) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    pl = nj_string_len(p);
    if (off < 0 || (nj_u4)off + pl > nj_string_len(s)) {
        ret->i = 0;
        return NJ_OK;
    }
    if (pl == 0u) {
        ret->i = 1;
        return NJ_OK;
    }
    sc = nj_string_chars(s) + off;
    pc = nj_string_chars(p);
    ret->i = (sc && pc &&
              memcmp(sc, pc, (nj_size)pl * sizeof(nj_u2)) == 0) ? 1 : 0;
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
    ret->i = nj_string_prefix_eq(s, p, pl);
    return NJ_OK;
}

NJFN(str_endsWith)
{
    nj_ref s = NJ_THIS(args);
    nj_ref p = NJ_ARG_REF(args, 0);
    nj_u4 sl, pl;
    const nj_u2 *sc;
    const nj_u2 *pc;

    if (!p) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    sl = nj_string_len(s);
    pl = nj_string_len(p);
    if (pl > sl) {
        ret->i = 0;
        return NJ_OK;
    }
    if (pl == 0u) {
        ret->i = 1;                       /* every string ends with "" */
        return NJ_OK;
    }
    sc = nj_string_chars(s) + (sl - pl);
    pc = nj_string_chars(p);
    ret->i = (sc && pc &&
              memcmp(sc, pc, (nj_size)pl * sizeof(nj_u2)) == 0) ? 1 : 0;
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

/* How many bytes `n` chars need in UTF-8: one, two or three each, four for a
 * surrogate pair. */
static nj_u4 utf8_count(const nj_u2 *c, nj_u4 n)
{
    nj_u4 i, total = 0u;

    for (i = 0u; i < n; i++) {
        nj_u4 ch = c[i];
        if (ch < 0x80u) {
            total += 1u;
        } else if (ch < 0x800u) {
            total += 2u;
        } else if (ch >= 0xD800u && ch <= 0xDBFFu && i + 1u < n &&
                   c[i + 1u] >= 0xDC00u && c[i + 1u] <= 0xDFFFu) {
            total += 4u;
            i++;
        } else {
            total += 3u;
        }
    }
    return total;
}

/* Write `n` chars as UTF-8 into `out`, which must hold utf8_count() bytes. */
static void utf8_write(const nj_u2 *c, nj_u4 n, nj_u1 *out)
{
    nj_u4 i, o = 0u;

    for (i = 0u; i < n; i++) {
        nj_u4 ch = c[i];
        if (ch < 0x80u) {
            out[o++] = (nj_u1)ch;
        } else if (ch < 0x800u) {
            out[o++] = (nj_u1)(0xC0u | (ch >> 6));
            out[o++] = (nj_u1)(0x80u | (ch & 0x3Fu));
        } else if (ch >= 0xD800u && ch <= 0xDBFFu && i + 1u < n &&
                   c[i + 1u] >= 0xDC00u && c[i + 1u] <= 0xDFFFu) {
            nj_u4 cp = 0x10000u + ((ch - 0xD800u) << 10) +
                       ((nj_u4)c[++i] - 0xDC00u);
            out[o++] = (nj_u1)(0xF0u | (cp >> 18));
            out[o++] = (nj_u1)(0x80u | ((cp >> 12) & 0x3Fu));
            out[o++] = (nj_u1)(0x80u | ((cp >> 6) & 0x3Fu));
            out[o++] = (nj_u1)(0x80u | (cp & 0x3Fu));
        } else {
            out[o++] = (nj_u1)(0xE0u | (ch >> 12));
            out[o++] = (nj_u1)(0x80u | ((ch >> 6) & 0x3Fu));
            out[o++] = (nj_u1)(0x80u | (ch & 0x3Fu));
        }
    }
}

/* getBytes(String encoding): the write side of the encodings the constructors
 * read.  A char that ISO-8859-1 cannot hold becomes '?' - that is what the
 * platform's own encoder does, and writing a lone low byte instead would turn
 * a Chinese title into plausible-looking garbage. */
NJFN(str_getBytes_enc)
{
    nj_ref s = NJ_THIS(args);
    int kind = str_encoding_kind(vm, NJ_ARG_REF(args, 0));
    const nj_u2 *c = nj_string_chars(s);
    nj_u4 n = nj_string_len(s);
    nj_u4 need, i;
    nj_ref a;

    if (kind < 0) {
        return NJ_ERR;
    }
    need = (kind == 1) ? utf8_count(c, n) : n;
    a = nj_new_array(vm, NULL, need, NJ_ARRAY_T_BYTE);
    if (!a) {
        return nj_throw_oom(vm);
    }
    if (kind == 1) {
        utf8_write(c, n, a->u.bytes);
    } else {
        for (i = 0u; i < n; i++) {
            a->u.bytes[i] = (c[i] > 0xFFu) ? (nj_u1)'?' : (nj_u1)c[i];
        }
    }
    ret->r = a;
    return NJ_OK;
}

NJFN(str_valueOf_obj)
{
    nj_ref o = NJ_ARG_REF(args, 0);
    nj_value a1[3];
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

/* String.valueOf(double): the text Double.toString writes, from the one
 * decimal formatter the runtime owns (nj_fmt_double; see nj_lib_boxing.c). */
NJFN(str_valueOf_double)
{
    char buf[40];
    nj_fmt_double(buf, sizeof(buf), NJ_ARG_DOUBLE(args, 0));
    ret->r = nj_string_new(vm, buf);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

/* String.valueOf(char[], int, int): the copy of String.valueOf(char[]) over a
 * checked window of the array. */
NJFN(str_valueOf_chars_range)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 cnt = NJ_ARG_INT(args, 2);
    nj_ref s;

    if (!arr) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (off < 0 || cnt < 0 || (nj_u4)off + (nj_u4)cnt > arr->length) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    s = nj_string_new(vm, "");
    if (!s) {
        return nj_throw_oom(vm);
    }
    if (str_install(vm, s, (const nj_u2 *)arr->u.bytes + off,
                    (nj_u4)cnt) != NJ_OK) {
        return nj_throw_oom(vm);
    }
    ret->r = s;
    return NJ_OK;
}

/* String.intern(): the runtime already interns its string literals and every
 * string it builds for a constant-pool entry, so an interned string is the
 * canonical one and there is no separate pool to add the receiver to.  Keeping
 * the receiver is what the caller asked for. */
NJFN(str_intern)
{
    ret->r = NJ_THIS(args);
    return NJ_OK;
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
    nj_value a1[3];

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
    nj_value a1[3];
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

NJFN(sb_append_double)
{
    char buf[40];
    double d = NJ_ARG_DOUBLE(args, 0);
#if NJ_USE_LIBC
    snprintf(buf, sizeof(buf), "%g", d);
#else
    (void)d;
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

/*
 * insert(int, char[]): the same operation as insert(int, String), reached by
 * way of a String, because that is where the bounds check and the shifting
 * already live and a second copy of them would be a second chance to get the
 * bounds wrong.  A title compiled against CLDC 1.0 uses this form.
 */
/*
 * StringBuffer.getChars(ii[ci)v.
 *
 * The buffer already knows how to turn itself into a String, and String
 * already knows how to copy a range out of itself into a char array - both
 * are implemented and both are covered by tests.  Doing it any other way here
 * would be a second implementation of the same rule, which is how the two
 * drift apart.  合金弹头4(完美汉化版)_4053 calls this and stopped drawing
 * until it existed.
 */
NJFN(sb_getChars)
{
    nj_ref str = nj_call_ref(vm, NJ_THIS(args), "java/lang/StringBuffer",
                             "toString", "()Ljava/lang/String;", NULL);
    if (!str) {
        return NJ_ERR;
    }
    return nj_call(vm, str, "java/lang/String", "getChars", "(II[CI)V", args, NULL);
}

static nj_status insert_chars(nj_vm *vm, nj_value *args, nj_value *ret,
                              const char *cls, const char *desc)
{
    nj_ref arr = NJ_ARG_REF(args, 1);
    nj_ref s;
    nj_value a1[3];
    nj_u4 mark;
    nj_status st;

    if (!arr) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    mark = nj_root_mark(vm);
    s = nj_object_new(vm, "java/lang/String");
    if (!s) {
        nj_root_release(vm, mark);
        return nj_throw_oom(vm);
    }
    s = nj_root(vm, s);
    a1[0].r = arr;
    st = nj_call_special(vm, s, "java/lang/String", "<init>", "([C)V", a1, NULL);
    if (st != NJ_OK) {
        nj_root_release(vm, mark);
        return st;
    }
    {
        nj_value a[2];
        nj_status r;
        a[0].i = NJ_ARG_INT(args, 0);
        a[1].r = s;
        r = nj_call(vm, NJ_THIS(args), cls, "insert", desc, a, ret);
        nj_root_release(vm, mark);
        return r;
    }
}

NJFN(sb_insert_chars)
{
    return insert_chars(vm, args, ret, "java/lang/StringBuffer",
                        "(ILjava/lang/String;)Ljava/lang/StringBuffer;");
}

NJFN(sbld_insert_chars)
{
    return insert_chars(vm, args, ret, "java/lang/StringBuilder",
                        "(ILjava/lang/String;)Ljava/lang/StringBuilder;");
}

/* ------------------------------------------------------------------ */
/* Members of the J2SE StringBuffer that no collected title calls and  */
/* that the API audit reports as missing.  Each is one String.valueOf  */
/* away from the String form the class already has, so they share      */
/* these two helpers.                                                  */
/* ------------------------------------------------------------------ */
static int insert_value(nj_vm *vm, nj_value *args, nj_value *ret,
                        const char *cls_desc, const char *value_desc)
{
    nj_value a[2];
    nj_ref s;
    nj_u4 mark;
    nj_status st;

    mark = nj_root_mark(vm);
    s = nj_call_ref(vm, 0, "java/lang/String", "valueOf", value_desc, &args[1]);
    if (!s) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    s = nj_root(vm, s);
    a[0].i = NJ_ARG_INT(args, 0);
    a[1].r = s;
    st = nj_call(vm, NJ_THIS(args), "java/lang/StringBuffer", "insert",
                 cls_desc, a, ret);
    nj_root_release(vm, mark);
    return st;
}

#define SB_INSERT_VALUE(name, vdesc)                                         \
    NJFN(name)                                                               \
    {                                                                        \
        return insert_value(vm, args, ret,                                   \
                            "(ILjava/lang/String;)Ljava/lang/StringBuffer;", \
                            vdesc);                                          \
    }

SB_INSERT_VALUE(sb_insert_obj,    "(Ljava/lang/Object;)Ljava/lang/String;")
SB_INSERT_VALUE(sb_insert_bool,   "(Z)Ljava/lang/String;")
SB_INSERT_VALUE(sb_insert_int,    "(I)Ljava/lang/String;")
SB_INSERT_VALUE(sb_insert_long,   "(J)Ljava/lang/String;")
SB_INSERT_VALUE(sb_insert_float,  "(F)Ljava/lang/String;")
SB_INSERT_VALUE(sb_insert_double, "(D)Ljava/lang/String;")

/* append(char[]) is append(char[], 0, length). */
static int append_chars_all(nj_vm *vm, nj_value *args, nj_value *ret,
                            const char *cls, const char *desc)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_value a[3];

    if (!arr) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    a[0].r = arr;
    a[1].i = 0;
    a[2].i = (nj_s4)nj_array_length(arr);
    return nj_call(vm, NJ_THIS(args), cls, "append", desc, a, ret);
}

NJFN(sb_append_chars_array)
{
    return append_chars_all(vm, args, ret, "java/lang/StringBuffer",
                            "([CII)Ljava/lang/StringBuffer;");
}

NJFN(sbld_append_chars_array)
{
    return append_chars_all(vm, args, ret, "java/lang/StringBuilder",
                            "([CII)Ljava/lang/StringBuilder;");
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
    M("<init>",            "([BLjava/lang/String;)V",          0),
    M("<init>",            "([BII)V",                          0),
    M("<init>",            "([BIILjava/lang/String;)V",        0),
    M("<init>",            "(Ljava/lang/StringBuffer;)V",      0),
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
    M("lastIndexOf",       "(II)I",                            0),
    M("substring",         "(I)Ljava/lang/String;",            0),
    M("substring",         "(II)Ljava/lang/String;",           0),
    M("concat",            "(Ljava/lang/String;)Ljava/lang/String;", 0),
    /*
     * regionMatches is a MIDP 1.0 / CLDC method, and neither overload was
     * here at all: 3D台球之王_5014 calls the five-argument one.
     */
    M("regionMatches",     "(ILjava/lang/String;II)Z",        0),
    M("regionMatches",     "(ZILjava/lang/String;II)Z",       0),
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
    M("getBytes",          "(Ljava/lang/String;)[B",           0),
    M("valueOf",           "(Ljava/lang/Object;)Ljava/lang/String;", NJ_ACC_STATIC),
    M("valueOf",           "(I)Ljava/lang/String;",            NJ_ACC_STATIC),
    M("valueOf",           "(J)Ljava/lang/String;",            NJ_ACC_STATIC),
    M("valueOf",           "(Z)Ljava/lang/String;",            NJ_ACC_STATIC),
    M("valueOf",           "(C)Ljava/lang/String;",            NJ_ACC_STATIC),
    M("valueOf",           "(F)Ljava/lang/String;",            NJ_ACC_STATIC),
    M("valueOf",           "(D)Ljava/lang/String;",            NJ_ACC_STATIC),
    M("valueOf",           "([C)Ljava/lang/String;",           NJ_ACC_STATIC),
    M("valueOf",           "([CII)Ljava/lang/String;",         NJ_ACC_STATIC),
    M("intern",            "()Ljava/lang/String;",             0)
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
    M("append",           "(D)Ljava/lang/StringBuilder;",         0),
    M("append",           "([CII)Ljava/lang/StringBuilder;",      0),
    M("append",           "([C)Ljava/lang/StringBuilder;",        0),
    M("reverse",          "()Ljava/lang/StringBuilder;",          0),
    M("delete",           "(II)Ljava/lang/StringBuilder;",        0),
    M("deleteCharAt",     "(I)Ljava/lang/StringBuilder;",         0),
    M("insert",           "(ILjava/lang/String;)Ljava/lang/StringBuilder;", 0),
    M("insert",           "(IC)Ljava/lang/StringBuilder;",        0),
    M("insert",           "(I[C)Ljava/lang/StringBuilder;",       0)
};

static const nj_builtin_field stringbuffer_fields[] = {
    { "value", "[C", 0 },
    { "count", "I",  0 }
};

static const nj_builtin_method stringbuffer_methods[] = {
    M("getChars", "(II[CI)V", 0),
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
    M("append",           "(D)Ljava/lang/StringBuffer;",         0),
    M("append",           "([CII)Ljava/lang/StringBuffer;",      0),
    M("append",           "([C)Ljava/lang/StringBuffer;",        0),
    M("reverse",          "()Ljava/lang/StringBuffer;",          0),
    M("delete",           "(II)Ljava/lang/StringBuffer;",        0),
    M("deleteCharAt",     "(I)Ljava/lang/StringBuffer;",         0),
    M("insert",           "(ILjava/lang/String;)Ljava/lang/StringBuffer;", 0),
    M("insert",           "(IC)Ljava/lang/StringBuffer;",        0),
    M("insert",           "(I[C)Ljava/lang/StringBuffer;",       0),
    M("insert",           "(IZ)Ljava/lang/StringBuffer;", 0),
    M("insert",           "(II)Ljava/lang/StringBuffer;", 0),
    M("insert",           "(IJ)Ljava/lang/StringBuffer;", 0),
    M("insert",           "(IF)Ljava/lang/StringBuffer;", 0),
    M("insert",           "(ID)Ljava/lang/StringBuffer;", 0),
    M("insert",           "(ILjava/lang/Object;)Ljava/lang/StringBuffer;", 0),
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
    { "java/lang/String", "<init>",           "([BLjava/lang/String;)V",  str_init_bytes_enc },
    { "java/lang/String", "<init>",           "([BII)V",                 str_init_bytes_range },
    { "java/lang/String", "<init>",           "([BIILjava/lang/String;)V", str_init_bytes_range_enc },
    { "java/lang/String", "<init>",           "(Ljava/lang/StringBuffer;)V", str_init_sb },
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
    { "java/lang/String", "lastIndexOf",      "(II)I",                   str_lastIndexOf_from },
    { "java/lang/String", "substring",        "(I)Ljava/lang/String;",   str_substring },
    { "java/lang/String", "substring",        "(II)Ljava/lang/String;",  str_substring_to },
    { "java/lang/String", "concat",           "(Ljava/lang/String;)Ljava/lang/String;", str_concat },
    { "java/lang/String", "replace",          "(CC)Ljava/lang/String;",  str_replace },
    { "java/lang/String", "trim",             "()Ljava/lang/String;",    str_trim },
    { "java/lang/String", "toLowerCase",      "()Ljava/lang/String;",    str_toLowerCase },
    { "java/lang/String", "regionMatches",    "(ILjava/lang/String;II)Z",  str_regionMatches },
    { "java/lang/String", "regionMatches",    "(ZILjava/lang/String;II)Z", str_regionMatches_ci },
    { "java/lang/String", "toUpperCase",      "()Ljava/lang/String;",    str_toUpperCase },
    { "java/lang/String", "startsWith",       "(Ljava/lang/String;)Z",   str_startsWith1 },
    { "java/lang/String", "startsWith",       "(Ljava/lang/String;I)Z",  str_startsWith },
    { "java/lang/String", "endsWith",         "(Ljava/lang/String;)Z",   str_endsWith },
    { "java/lang/String", "compareTo",        "(Ljava/lang/String;)I",   str_compareTo },
    { "java/lang/String", "toCharArray",      "()[C",                    str_toCharArray },
    { "java/lang/String", "getChars",         "(II[CI)V",                str_getChars },
    { "java/lang/String", "getBytes",         "()[B",                    str_getBytes },
    { "java/lang/String", "getBytes",         "(Ljava/lang/String;)[B",  str_getBytes_enc },
    { "java/lang/String", "valueOf",          "(Ljava/lang/Object;)Ljava/lang/String;", str_valueOf_obj },
    { "java/lang/String", "valueOf",          "(I)Ljava/lang/String;",   str_valueOf_int },
    { "java/lang/String", "valueOf",          "(J)Ljava/lang/String;",   str_valueOf_long },
    { "java/lang/String", "valueOf",          "(Z)Ljava/lang/String;",   str_valueOf_bool },
    { "java/lang/String", "valueOf",          "(C)Ljava/lang/String;",   str_valueOf_char },
    { "java/lang/String", "valueOf",          "(F)Ljava/lang/String;",   str_valueOf_float },
    { "java/lang/String", "valueOf",          "(D)Ljava/lang/String;",   str_valueOf_double },
    { "java/lang/String", "valueOf",          "([C)Ljava/lang/String;",  str_valueOf_chars },
    { "java/lang/String", "valueOf",          "([CII)Ljava/lang/String;", str_valueOf_chars_range },
    { "java/lang/String", "intern",           "()Ljava/lang/String;",   str_intern },

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
    { "java/lang/StringBuffer", "append",         "(D)Ljava/lang/StringBuffer;", sb_append_double },
    { "java/lang/StringBuffer", "append",         "([CII)Ljava/lang/StringBuffer;", sb_append_chars_range },
    { "java/lang/StringBuffer", "append",         "([C)Ljava/lang/StringBuffer;", sb_append_chars_array },
    { "java/lang/StringBuffer", "reverse",        "()Ljava/lang/StringBuffer;", sb_reverse },
    { "java/lang/StringBuffer", "delete",         "(II)Ljava/lang/StringBuffer;", sb_delete },
    { "java/lang/StringBuffer", "deleteCharAt",   "(I)Ljava/lang/StringBuffer;", sb_deleteCharAt },
    { "java/lang/StringBuffer", "insert",         "(ILjava/lang/String;)Ljava/lang/StringBuffer;", sb_insert_str },
    { "java/lang/StringBuffer", "getChars",        "(II[CI)V",               sb_getChars },
    { "java/lang/StringBuffer", "insert",         "(IC)Ljava/lang/StringBuffer;", sb_insert_char },
    { "java/lang/StringBuffer", "insert",         "(I[C)Ljava/lang/StringBuffer;", sb_insert_chars },
    { "java/lang/StringBuffer", "insert",         "(IZ)Ljava/lang/StringBuffer;", sb_insert_bool },
    { "java/lang/StringBuffer", "insert",         "(II)Ljava/lang/StringBuffer;", sb_insert_int },
    { "java/lang/StringBuffer", "insert",         "(IJ)Ljava/lang/StringBuffer;", sb_insert_long },
    { "java/lang/StringBuffer", "insert",         "(IF)Ljava/lang/StringBuffer;", sb_insert_float },
    { "java/lang/StringBuffer", "insert",         "(ID)Ljava/lang/StringBuffer;", sb_insert_double },
    { "java/lang/StringBuffer", "insert",         "(ILjava/lang/Object;)Ljava/lang/StringBuffer;", sb_insert_obj },

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
    { "java/lang/StringBuilder", "append",       "(D)Ljava/lang/StringBuilder;", sb_append_double },
    { "java/lang/StringBuilder", "append",       "([CII)Ljava/lang/StringBuilder;", sb_append_chars_range },
    { "java/lang/StringBuilder", "append",       "([C)Ljava/lang/StringBuilder;", sbld_append_chars_array },
    { "java/lang/StringBuilder", "reverse",      "()Ljava/lang/StringBuilder;", sb_reverse },
    { "java/lang/StringBuilder", "delete",       "(II)Ljava/lang/StringBuilder;", sb_delete },
    { "java/lang/StringBuilder", "deleteCharAt", "(I)Ljava/lang/StringBuilder;", sb_deleteCharAt },
    { "java/lang/StringBuilder", "insert",       "(ILjava/lang/String;)Ljava/lang/StringBuilder;", sb_insert_str },
    { "java/lang/StringBuilder", "insert",       "(IC)Ljava/lang/StringBuilder;", sb_insert_char },
    { "java/lang/StringBuilder", "insert",         "(I[C)Ljava/lang/StringBuilder;", sbld_insert_chars },
};
const int nj_lib_string_count =
    (int)(sizeof(nj_lib_string_table) / sizeof(nj_lib_string_table[0]));
