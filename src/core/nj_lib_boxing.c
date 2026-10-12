/*
 * nano_j2me - nj_lib_boxing.c
 *
 * java.lang.Integer / Long / Short / Byte / Character / Boolean / Float /
 * Double.  Each is the usual single-field wrapper plus the static parse and
 * format helpers games actually use.
 */
#include "nj_internal.h"

#if NJ_USE_LIBC
#  include <string.h>
#  include <stdio.h>
#endif

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_UNUSED(x) ((void)(x))

static nj_field *vf(nj_vm *vm, const char *cls, const char *desc)
{
    return nj_field_lookup(vm, cls, "value", desc, 0);
}

/* ------------------------------------------------------------------ */
/* Integer                                                             */
/* ------------------------------------------------------------------ */
NJFN(int_init)
{
    nj_field *f = vf(vm, "java/lang/Integer", "I");
    if (f) {
        nj_field_write_int(vm, NJ_THIS(args), f, NJ_ARG_INT(args, 0));
    }
    return NJ_OK;
}

NJFN(int_intValue)
{
    nj_field *f = vf(vm, "java/lang/Integer", "I");
    ret->i = f ? nj_field_read_int(vm, NJ_THIS(args), f) : 0;
    return NJ_OK;
}

/*
 * Integer.hashCode() is the value it wraps, which is what a Hashtable keyed on
 * Integers needs.  The method was declared in Integer_methods but never bound,
 * so it existed, resolved, and then threw UnsatisfiedLinkError the moment
 * anything called it - which is how an assassin's creed title failed, by
 * putting Integers into a Hashtable.
 *
 * Falling back to Object.hashCode (identity) would have kept it running, so
 * this would have stayed invisible while quietly bucketing every key wrong.
 */
NJFN(int_hashCode)
{
    nj_field *f = vf(vm, "java/lang/Integer", "I");
    ret->i = f ? nj_field_read_int(vm, NJ_THIS(args), f) : 0;
    return NJ_OK;
}

/*
 * An Integer equals another Integer holding the same number - and only that.
 * A Long or a Short holding the same value is NOT equal, which is what the
 * wrapper contract says and what a Hashtable keyed on mixed wrappers relies
 * on.  Like hashCode above, this method was declared and never bound.
 */
NJFN(int_equals)
{
    nj_ref a = NJ_THIS(args);
    nj_ref b = NJ_ARG_REF(args, 0);
    nj_field *f = vf(vm, "java/lang/Integer", "I");

    if (!a || !b) {
        ret->i = 0;
        return NJ_OK;
    }
    if (b->cls && a->cls && !nj_class_is_subclass_of(b->cls, a->cls)) {
        ret->i = 0;
        return NJ_OK;
    }
    ret->i = (f && nj_field_read_int(vm, a, f) ==
                   nj_field_read_int(vm, b, f)) ? 1 : 0;
    return NJ_OK;
}

/* The magnitude of an integer literal in `radix`, with the sign peeled off.
 * Answers 0 for anything that is not a number, including one whose magnitude
 * does not fit in 64 bits, so the caller only has to range-check the width its
 * own type needs. */
static int parse_magnitude(nj_ref s, nj_u4 radix, int *neg_out, nj_u8 *mag_out)
{
    const nj_u2 *p;
    nj_u4 i = 0u;
    int neg = 0;
    nj_u8 acc = 0u;

    if (!s || radix < 2u || radix > 36u) {
        return 0;
    }
    p = nj_string_chars(s);
    if (s->length > 0u && (p[0] == '+' || p[0] == '-')) {
        neg = (p[0] == '-');
        i = 1u;
    }
    if (i >= s->length) {
        return 0;
    }
    for (; i < s->length; i++) {
        nj_u4 c = p[i], d;
        if (c >= '0' && c <= '9') {
            d = c - '0';
        } else if (c >= 'a' && c <= 'z') {
            d = c - 'a' + 10u;
        } else if (c >= 'A' && c <= 'Z') {
            d = c - 'A' + 10u;
        } else {
            return 0;
        }
        if (d >= radix) {
            return 0;
        }
        if (acc > (nj_u8)(0xFFFFFFFFFFFFFFFFull - (nj_u8)d) / (nj_u8)radix) {
            return 0;
        }
        acc = acc * (nj_u8)radix + (nj_u8)d;
    }
    *neg_out = neg;
    *mag_out = acc;
    return 1;
}

/* Integer.parseInt's contract: the digits AND a ramp to -2^31, which is what
 * makes "2147483648" a String but "2147483649" a NumberFormatException.  The
 * old loop only checked 32 bits, so it wrapped instead of throwing. */
static nj_status parse_int(nj_vm *vm, nj_ref s, nj_u4 radix, nj_s4 *out)
{
    int neg = 0;
    nj_u8 mag = 0u;

    if (!parse_magnitude(s, radix, &neg, &mag) ||
        mag > (neg ? 2147483648ull : 2147483647ull)) {
        return nj_throw(vm, "java/lang/NumberFormatException", NULL);
    }
    *out = (nj_s4)(neg ? (nj_u4)0u - (nj_u4)mag : (nj_u4)mag);
    return NJ_OK;
}

static nj_status parse_long(nj_vm *vm, nj_ref s, nj_u4 radix, nj_s8 *out)
{
    int neg = 0;
    nj_u8 mag = 0u;

    if (!parse_magnitude(s, radix, &neg, &mag) ||
        mag > (neg ? 9223372036854775808ull : 9223372036854775807ull)) {
        return nj_throw(vm, "java/lang/NumberFormatException", NULL);
    }
    *out = (nj_s8)(neg ? (nj_u8)0u - mag : mag);
    return NJ_OK;
}

/* A literal that fits in `bits` signed bits, or a NumberFormatException. */
static nj_status parse_small(nj_vm *vm, nj_ref s, nj_s4 lo, nj_s4 hi, nj_s4 *out)
{
    nj_s4 v = 0;

    if (parse_int(vm, s, 10u, &v) != NJ_OK) {
        return NJ_ERR;
    }
    if (v < lo || v > hi) {
        return nj_throw(vm, "java/lang/NumberFormatException", NULL);
    }
    *out = v;
    return NJ_OK;
}

NJFN(int_parseInt)
{
    nj_s4 v = 0;

    if (parse_int(vm, NJ_ARG_REF(args, 0), 10u, &v) != NJ_OK) {
        return NJ_ERR;
    }
    ret->i = v;
    return NJ_OK;
}

NJFN(int_parseInt_radix)
{
    nj_s4 v = 0;
    nj_u4 radix = (nj_u4)NJ_ARG_INT(args, 1);

    if (radix < 2u || radix > 36u) {
        return nj_throw(vm, "java/lang/NumberFormatException", "radix");
    }
    if (parse_int(vm, NJ_ARG_REF(args, 0), radix, &v) != NJ_OK) {
        return NJ_ERR;
    }
    ret->i = v;
    return NJ_OK;
}

/* Integer.valueOf(String) - three corpus titles ask a text field for a boxed
 * int, and without it the call is a NoSuchMethodError before parsing even
 * starts. */
NJFN(int_valueOf_str)
{
    nj_value a1[3];
    nj_ref o;
    nj_s4 v = 0;

    if (parse_int(vm, NJ_ARG_REF(args, 0), 10u, &v) != NJ_OK) {
        return NJ_ERR;
    }
    o = nj_object_new(vm, "java/lang/Integer");
    if (!o) {
        return nj_throw_oom(vm);
    }
    a1[0].i = v;
    if (nj_call_special(vm, o, "java/lang/Integer", "<init>", "(I)V", a1, NULL)
        != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = o;
    return NJ_OK;
}

/*
 * Integer.valueOf(String, int) - CLDC 1.1 declares the radix form beside
 * parseInt(String, int), and a MIDlet that boxes a value it just parsed with a
 * radix needs it: tests/games/QQ飞车2010_5772.jar calls it while building an
 * SMS address and never starts without it.  Same contract as parseInt: a bad
 * digit, no digits at all, or a radix outside 2..36 is a
 * NumberFormatException raised by the parser, not a silent zero.
 */
NJFN(int_valueOf_str_radix)
{
    nj_value a1[3];
    nj_ref o;
    nj_s4 v = 0;

    if (parse_int(vm, NJ_ARG_REF(args, 0), (nj_u4)NJ_ARG_INT(args, 1), &v)
        != NJ_OK) {
        return NJ_ERR;
    }
    o = nj_object_new(vm, "java/lang/Integer");
    if (!o) {
        return nj_throw_oom(vm);
    }
    a1[0].i = v;
    if (nj_call_special(vm, o, "java/lang/Integer", "<init>", "(I)V", a1, NULL)
        != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = o;
    return NJ_OK;
}

NJFN(int_byteValue)
{
    nj_field *f = vf(vm, "java/lang/Integer", "I");
    nj_s4 v = f ? nj_field_read_int(vm, NJ_THIS(args), f) : 0;

    ret->i = (nj_s4)(nj_s1)(v & 0xFF);
    return NJ_OK;
}

NJFN(Short_parseShort)
{
    nj_s4 v = 0;

    if (parse_small(vm, NJ_ARG_REF(args, 0), -32768, 32767, &v) != NJ_OK) {
        return NJ_ERR;
    }
    ret->i = v;
    return NJ_OK;
}

NJFN(Byte_parseByte)
{
    nj_s4 v = 0;

    if (parse_small(vm, NJ_ARG_REF(args, 0), -128, 127, &v) != NJ_OK) {
        return NJ_ERR;
    }
    ret->i = v;
    return NJ_OK;
}

NJFN(Long_parseLong)
{
    nj_s8 v = 0;

    if (parse_long(vm, NJ_ARG_REF(args, 0), 10u, &v) != NJ_OK) {
        return NJ_ERR;
    }
    ret->j = v;
    return NJ_OK;
}

NJFN(Long_parseLong_radix)
{
    nj_s8 v = 0;
    nj_u4 radix = (nj_u4)NJ_ARG_INT(args, 1);

    if (radix < 2u || radix > 36u) {
        return nj_throw(vm, "java/lang/NumberFormatException", "radix");
    }
    if (parse_long(vm, NJ_ARG_REF(args, 0), radix, &v) != NJ_OK) {
        return NJ_ERR;
    }
    ret->j = v;
    return NJ_OK;
}

/*
 * Long.valueOf(String) and Long.valueOf(String, int).  CLDC 1.1 declares all
 * four of parseLong(String), parseLong(String, int), valueOf(String) and
 * valueOf(String, int); only the two parseLong forms existed, so a MIDlet that
 * boxed what it had just parsed failed with a NoSuchMethodError.  The wrapper
 * is built through the ordinary <init>(J)V constructor so the Long class keeps
 * owning the bit pattern.
 */
NJFN(Long_valueOf_str)
{
    nj_value a1[3];
    nj_ref o;
    nj_s8 v = 0;

    if (parse_long(vm, NJ_ARG_REF(args, 0), 10u, &v) != NJ_OK) {
        return NJ_ERR;
    }
    o = nj_object_new(vm, "java/lang/Long");
    if (!o) {
        return nj_throw_oom(vm);
    }
    a1[0].j = v;
    a1[1].bits = 0u;   /* the upper half of the two-slot value */
    if (nj_call_special(vm, o, "java/lang/Long", "<init>", "(J)V", a1, NULL)
        != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = o;
    return NJ_OK;
}

NJFN(Long_valueOf_str_radix)
{
    nj_value a1[3];
    nj_ref o;
    nj_s8 v = 0;

    if (parse_long(vm, NJ_ARG_REF(args, 0), (nj_u4)NJ_ARG_INT(args, 1), &v)
        != NJ_OK) {
        return NJ_ERR;
    }
    o = nj_object_new(vm, "java/lang/Long");
    if (!o) {
        return nj_throw_oom(vm);
    }
    a1[0].j = v;
    a1[1].bits = 0u;   /* the upper half of the two-slot value */
    if (nj_call_special(vm, o, "java/lang/Long", "<init>", "(J)V", a1, NULL)
        != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = o;
    return NJ_OK;
}
/* ------------------------------------------------------------------ */
/* Float and Double bit patterns                                       */
/* ------------------------------------------------------------------ */
/* A wrapper stores the BITS, not (int)value.  Float_init used to write a C
 * cast, so new Float(3.5f).floatValue() was 3.0 and NaN became 0 - which also
 * made Float.isNaN() unanswerable. */
static nj_s4 float_bits(float f)
{
    nj_s4 bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

static float float_from_bits(nj_s4 bits)
{
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

static int float_is_nan_bits(nj_s4 bits)
{
    return (bits & 0x7F800000) == 0x7F800000 && (bits & 0x007FFFFF) != 0;
}

NJFN(Float_intBitsToFloat)
{
    ret->f = float_from_bits(NJ_ARG_INT(args, 0));
    return NJ_OK;
}

/* floatToIntBits collapses every NaN to one pattern, which is what the spec
 * asks for and what a game comparing NaNs depends on. */
NJFN(Float_floatToIntBits)
{
    nj_s4 bits = float_bits(NJ_ARG_FLOAT(args, 0));

    ret->i = float_is_nan_bits(bits) ? 0x7FC00000 : bits;
    return NJ_OK;
}

NJFN(Float_isNaN)
{
    nj_field *f = vf(vm, "java/lang/Float", "F");
    nj_s4 bits = f ? nj_field_read_int(vm, NJ_THIS(args), f) : 0;

    ret->i = float_is_nan_bits(bits) ? 1 : 0;
    return NJ_OK;
}

NJFN(Float_isInfinite)
{
    nj_field *f = vf(vm, "java/lang/Float", "F");
    nj_s4 bits = f ? nj_field_read_int(vm, NJ_THIS(args), f) : 0;

    ret->i = ((bits & 0x7FFFFFFF) == 0x7F800000) ? 1 : 0;
    return NJ_OK;
}

/*
 * The static forms take the value, not a wrapper.  CLDC 1.1 declares both
 * isNaN(float) and isNaN(), and a MIDlet testing a computed value calls the
 * static one; only the no-arg instance form existed here, so those calls did
 * not resolve at all.  急速狂飙完美破解版_4964 stopped with
 * `NoSuchMethodError: java/lang/Float.isNaN(F)Z @ l.b+3` the first time it
 * tried to reject a bad steering value - a hard stop for a game that uses NaN
 * as its "no reading yet" marker, which is the usual idiom in a physics loop.
 */
NJFN(Float_isNaN_v)
{
    nj_s4 bits = float_bits(NJ_ARG_FLOAT(args, 0));

    ret->i = float_is_nan_bits(bits) ? 1 : 0;
    return NJ_OK;
}

NJFN(Float_isInfinite_v)
{
    nj_s4 bits = float_bits(NJ_ARG_FLOAT(args, 0));

    ret->i = ((bits & 0x7FFFFFFF) == 0x7F800000) ? 1 : 0;
    return NJ_OK;
}

NJFN(Double_isNaN)
{
    nj_field *f = vf(vm, "java/lang/Double", "D");
    nj_s8 bits = f ? nj_field_read_long(vm, NJ_THIS(args), f) : 0;

    ret->i = ((bits & 0x7FF0000000000000LL) == 0x7FF0000000000000LL &&
              (bits & 0x000FFFFFFFFFFFFFLL) != 0) ? 1 : 0;
    return NJ_OK;
}

NJFN(Double_isInfinite)
{
    nj_field *f = vf(vm, "java/lang/Double", "D");
    nj_s8 bits = f ? nj_field_read_long(vm, NJ_THIS(args), f) : 0;

    ret->i = ((bits & 0x7FFFFFFFFFFFFFFFLL) == 0x7FF0000000000000LL) ? 1 : 0;
    return NJ_OK;
}

/* The static Double forms, for the same reason as the Float ones above. */
NJFN(Double_isNaN_d)
{
    nj_s8 bits;
    double v = NJ_ARG_DOUBLE(args, 0);

    memcpy(&bits, &v, sizeof(bits));
    ret->i = ((bits & 0x7FF0000000000000LL) == 0x7FF0000000000000LL &&
              (bits & 0x000FFFFFFFFFFFFFLL) != 0) ? 1 : 0;
    return NJ_OK;
}

NJFN(Double_isInfinite_d)
{
    nj_s8 bits;
    double v = NJ_ARG_DOUBLE(args, 0);

    memcpy(&bits, &v, sizeof(bits));
    ret->i = ((bits & 0x7FFFFFFFFFFFFFFFLL) == 0x7FF0000000000000LL) ? 1 : 0;
    return NJ_OK;
}

/* A decimal literal, without strtod: the freestanding build has no libc to
 * borrow one from, and this is four lines of arithmetic for the ordinary
 * "3.14", "-1.5e3", ".5" shapes a game parses out of a config file. */
NJFN(Double_valueOf_str)
{
    nj_ref s = NJ_ARG_REF(args, 0);
    /*
     * Two slots, not one: <init>(D)V takes a double, and a double occupies two
     * JVM slots with the payload in the lower one.  With `nj_value a1[3]` the
     * call below read 8 bytes past the array - ASan called it a
     * stack-buffer-overflow in vm_invoke, which is where the read happens and
     * not where the mistake is.  Without a sanitizer it read whatever the next
     * stack slot happened to hold, so the boxed value was usually right and
     * occasionally a garbage bit pattern that the collector would then scan.
     * The upper slot is zeroed for the same reason every other 2-slot value in
     * the runtime is: an uninitialised half looks like a pointer to the GC.
     */
    nj_value a1[3];
    const nj_u2 *p;
    nj_u4 i = 0u;
    int neg = 0, any = 0;
    double d = 0.0, scale = 1.0;
    int exp = 0;
    nj_ref o;

    if (!s) {
        return nj_throw(vm, "java/lang/NumberFormatException", "null");
    }
    p = nj_string_chars(s);
    if (i < s->length && (p[i] == '+' || p[i] == '-')) {
        neg = (p[i] == '-');
        i++;
    }
    for (; i < s->length && p[i] >= '0' && p[i] <= '9'; i++) {
        d = d * 10.0 + (double)(p[i] - '0');
        any = 1;
    }
    if (i < s->length && p[i] == '.') {
        i++;
        for (; i < s->length && p[i] >= '0' && p[i] <= '9'; i++) {
            scale *= 10.0;
            d = d * 10.0 + (double)(p[i] - '0');
            any = 1;
        }
    }
    if (!any) {
        return nj_throw(vm, "java/lang/NumberFormatException", NULL);
    }
    d /= scale;
    if (i < s->length && (p[i] == 'e' || p[i] == 'E')) {
        int eneg = 0;
        int eany = 0;
        i++;
        if (i < s->length && (p[i] == '+' || p[i] == '-')) {
            eneg = (p[i] == '-');
            i++;
        }
        for (; i < s->length && p[i] >= '0' && p[i] <= '9'; i++) {
            if (exp < 10000) {
                exp = exp * 10 + (int)(p[i] - '0');
            }
            eany = 1;
        }
        if (!eany) {
            return nj_throw(vm, "java/lang/NumberFormatException", NULL);
        }
        if (eneg) {
            exp = -exp;
        }
    }
    if (i != s->length) {
        return nj_throw(vm, "java/lang/NumberFormatException", NULL);
    }
    while (exp > 0) {
        d *= 10.0;
        exp--;
    }
    while (exp < 0) {
        d /= 10.0;
        exp++;
    }
    o = nj_object_new(vm, "java/lang/Double");
    if (!o) {
        return nj_throw_oom(vm);
    }
    a1[0].d = neg ? -d : d;
    a1[1].bits = 0u;   /* the upper half of the two-slot value */
    if (nj_call_special(vm, o, "java/lang/Double", "<init>", "(D)V", a1, NULL)
        != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = o;
    return NJ_OK;
}

/* Character.digit(char, int radix): the inverse of what a number parser
 * uses, and the reason a game can validate a text field before parseInt. */
NJFN(Character_digit)
{
    nj_s4 c = NJ_ARG_INT(args, 0);
    nj_s4 radix = NJ_ARG_INT(args, 1);
    nj_s4 d = -1;

    if (radix >= 2 && radix <= 36) {
        if (c >= '0' && c <= '9') {
            d = c - '0';
        } else if (c >= 'a' && c <= 'z') {
            d = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'Z') {
            d = c - 'A' + 10;
        }
        if (d >= radix) {
            d = -1;
        }
    }
    ret->i = d;
    return NJ_OK;
}

NJFN(int_toString)
{
    ret->r = nj_string_new_from_int(vm, NJ_ARG_INT(args, 0));
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

static nj_ref to_radix_string(nj_vm *vm, nj_u4 v, int neg, nj_u4 radix)
{
    char buf[40];
    char tmp[40];
    int i = 0, j = 0;

    if (v == 0u) {
        tmp[i++] = '0';
    }
    while (v != 0u && i < 39) {
        nj_u4 d = v % radix;
        tmp[i++] = (char)(d < 10u ? ('0' + d) : ('a' + (d - 10u)));
        v /= radix;
    }
    if (neg) {
        buf[j++] = '-';
    }
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
    return nj_string_new(vm, buf);
}

NJFN(int_toHexString)
{
    nj_u4 v = (nj_u4)NJ_ARG_INT(args, 0);
    ret->r = to_radix_string(vm, v, 0, 16u);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

NJFN(int_toString_radix)
{
    nj_s4 v = NJ_ARG_INT(args, 0);
    nj_s4 r = NJ_ARG_INT(args, 1);
    if (r < 2 || r > 36) {
        r = 10;
    }
    ret->r = to_radix_string(vm, (nj_u4)(v < 0 ? -v : v), v < 0, (nj_u4)r);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

/*
 * The 64-bit twin of to_radix_string.  A long needs twenty digits in base ten
 * and sixty-four in base two, and the magnitude has to be taken in unsigned
 * arithmetic so that Long.MIN_VALUE does not overflow on the way in.
 */
static nj_ref to_radix_string64(nj_vm *vm, nj_u8 v, int neg, nj_u8 radix)
{
    char buf[72];
    char tmp[72];
    int i = 0, j = 0;

    if (v == 0u) {
        tmp[i++] = '0';
    }
    while (v != 0u && i < 70) {
        nj_u8 d = v % radix;
        tmp[i++] = (char)(d < 10u ? ('0' + (int)d) : ('a' + (int)(d - 10u)));
        v /= radix;
    }
    if (neg) {
        buf[j++] = '-';
    }
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
    return nj_string_new(vm, buf);
}

static void long_magnitude(nj_s8 v, nj_u8 *mag, int *neg)
{
    *neg = v < 0 ? 1 : 0;
    *mag = *neg ? (nj_u8)(-(v + 1)) + 1u : (nj_u8)v;
}

NJFN(long_toString)
{
    nj_u8 mag;
    int neg;

    long_magnitude(NJ_ARG_LONG(args, 0), &mag, &neg);
    ret->r = to_radix_string64(vm, mag, neg, 10u);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

NJFN(long_toString_radix)
{
    /* A long costs two slots, so the radix sits at index 2, not 1. */
    nj_s4 r = NJ_ARG_INT(args, 2);
    nj_u8 mag;
    int neg;

    if (r < 2 || r > 36) {
        r = 10;
    }
    long_magnitude(NJ_ARG_LONG(args, 0), &mag, &neg);
    ret->r = to_radix_string64(vm, mag, neg, (nj_u8)r);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}


/*
 * The wide `valueOf` statics.
 *
 * CLDC 1.1 declares Long.valueOf(long), Double.valueOf(double) and
 * Float.valueOf(float) beside the String forms, and all three were missing:
 * `Long.valueOf(60L)` raised NoSuchMethodError.
 *
 * The first two also show the trap the C side has to respect.  A long or a
 * double occupies TWO operand slots with the payload in the lower one, and
 * vm_invoke() copies m->arg_slots of them into the frame - so a1[1] must be
 * zeroed explicitly.  Left as whatever the C stack happened to hold, the boxed
 * value comes back with a garbage upper half.  That is the same mistake that
 * made Math.max(0L, 60L) return 0.
 */
NJFN(Long_valueOf)
{
    nj_value a1[3];
    nj_ref o = nj_object_new(vm, "java/lang/Long");
    if (!o) {
        return NJ_ERR;
    }
    a1[0].j = NJ_ARG_LONG(args, 0);
    a1[1].bits = 0u;
    if (nj_call_special(vm, o, "java/lang/Long", "<init>", "(J)V", a1, NULL)
        != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = o;
    return NJ_OK;
}

NJFN(Double_valueOf)
{
    nj_value a1[3];
    nj_ref o = nj_object_new(vm, "java/lang/Double");
    if (!o) {
        return NJ_ERR;
    }
    a1[0].d = NJ_ARG_DOUBLE(args, 0);
    a1[1].bits = 0u;
    if (nj_call_special(vm, o, "java/lang/Double", "<init>", "(D)V", a1, NULL)
        != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = o;
    return NJ_OK;
}

NJFN(Float_valueOf)
{
    nj_value a1[1];
    nj_ref o = nj_object_new(vm, "java/lang/Float");
    if (!o) {
        return NJ_ERR;
    }
    a1[0].f = NJ_ARG_FLOAT(args, 0);
    if (nj_call_special(vm, o, "java/lang/Float", "<init>", "(F)V", a1, NULL)
        != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = o;
    return NJ_OK;
}

NJFN(int_valueOf)
{
    nj_value a1[3];
    nj_ref o = nj_object_new(vm, "java/lang/Integer");
    if (!o) {
        return NJ_ERR;
    }
    a1[0].i = NJ_ARG_INT(args, 0);
    if (nj_call_special(vm, o, "java/lang/Integer", "<init>", "(I)V", a1, NULL)
        != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = o;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Long / Short / Byte / Character / Boolean / Float / Double          */
/* ------------------------------------------------------------------ */
#define WRAPPER_NUM(CLS, CTYPE, FIELD, VALEXPR, SETEXPR)                      \
NJFN(CLS##_init)                                                              \
{                                                                             \
    nj_field *f = vf(vm, "java/lang/" #CLS, FIELD);                           \
    if (f) { nj_field_write_int(vm, NJ_THIS(args), f, (nj_s4)(VALEXPR)); }     \
    return NJ_OK;                                                             \
}                                                                             \
NJFN(CLS##_value)                                                             \
{                                                                             \
    nj_field *f = vf(vm, "java/lang/" #CLS, FIELD);                           \
    ret->i = f ? nj_field_read_int(vm, NJ_THIS(args), f) : 0;                  \
    return NJ_OK;                                                             \
}

WRAPPER_NUM(Boolean, nj_s4, "Z", NJ_ARG_INT(args, 0) ? 1 : 0, )
WRAPPER_NUM(Byte,    nj_s4, "B", (nj_s1)NJ_ARG_INT(args, 0), )
WRAPPER_NUM(Short,   nj_s4, "S", (nj_s2)NJ_ARG_INT(args, 0), )
WRAPPER_NUM(Character,nj_u2,"C", (nj_u2)NJ_ARG_INT(args, 0), )

NJFN(Long_init)
{
    nj_field *f = vf(vm, "java/lang/Long", "J");
    if (f) {
        nj_field_write_long(vm, NJ_THIS(args), f, NJ_ARG_LONG(args, 0));
    }
    return NJ_OK;
}

NJFN(Long_longValue)
{
    nj_field *f = vf(vm, "java/lang/Long", "J");
    ret->j = f ? nj_field_read_long(vm, NJ_THIS(args), f) : 0;
    return NJ_OK;
}

NJFN(Float_init)
{
    nj_field *f = vf(vm, "java/lang/Float", "F");
    if (f) {
        float v = NJ_ARG_FLOAT(args, 0);
        nj_s4 bits;
        memcpy(&bits, &v, sizeof(bits));
        nj_field_write_int(vm, NJ_THIS(args), f, bits);
    }
    return NJ_OK;
}

NJFN(Float_value)
{
    nj_field *f = vf(vm, "java/lang/Float", "F");
    if (f) {
        nj_s4 bits = nj_field_read_int(vm, NJ_THIS(args), f);
        memcpy(&ret->f, &bits, sizeof(ret->f));
    } else {
        ret->f = 0.0f;
    }
    return NJ_OK;
}

NJFN(Double_init)
{
    nj_field *f = vf(vm, "java/lang/Double", "D");
    if (f) {
        double d = NJ_ARG_DOUBLE(args, 0);
        nj_s8 bits;
        memcpy(&bits, &d, 8u);
        nj_field_write_long(vm, NJ_THIS(args), f, bits);
    }
    return NJ_OK;
}

NJFN(Double_value)
{
    nj_field *f = vf(vm, "java/lang/Double", "D");
    if (f) {
        nj_s8 bits = nj_field_read_long(vm, NJ_THIS(args), f);
        memcpy(&ret->d, &bits, 8u);
    } else {
        ret->d = 0.0;
    }
    return NJ_OK;
}

NJFN(Boolean_valueOf)
{
    nj_value a1[3];
    nj_ref o = nj_object_new(vm, "java/lang/Boolean");
    if (!o) {
        return NJ_ERR;
    }
    a1[0].i = NJ_ARG_INT(args, 0) ? 1 : 0;
    if (nj_call_special(vm, o, "java/lang/Boolean", "<init>", "(Z)V", a1, NULL)
        != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = o;
    return NJ_OK;
}

NJFN(Character_isDigit)
{
    nj_s4 c = NJ_ARG_INT(args, 0);
    ret->i = (c >= '0' && c <= '9') ? 1 : 0;
    return NJ_OK;
}

NJFN(Character_isLetter)
{
    nj_s4 c = NJ_ARG_INT(args, 0);
    ret->i = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) ? 1 : 0;
    return NJ_OK;
}

NJFN(Character_isLowerCase)
{
    nj_s4 c = NJ_ARG_INT(args, 0);
    ret->i = (c >= 'a' && c <= 'z') ? 1 : 0;
    return NJ_OK;
}

NJFN(Character_isUpperCase)
{
    nj_s4 c = NJ_ARG_INT(args, 0);
    ret->i = (c >= 'A' && c <= 'Z') ? 1 : 0;
    return NJ_OK;
}

NJFN(Character_toLowerCase)
{
    nj_s4 c = NJ_ARG_INT(args, 0);
    ret->i = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
    return NJ_OK;
}

NJFN(Character_toUpperCase)
{
    nj_s4 c = NJ_ARG_INT(args, 0);
    ret->i = (c >= 'a' && c <= 'z') ? (c - 32) : c;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }
#define F(name, desc, flags) { name, desc, flags }

#define WRAPPER_CLASS(CLS, SUPER, FIELD, VALDESC, EXTRA)                      \
    { "java/lang/" #CLS, SUPER, NJ_ACC_PUBLIC | NJ_ACC_FINAL,                 \
      CLS##_fields,                                                           \
      (nj_u2)(sizeof(CLS##_fields) / sizeof(CLS##_fields[0])),                \
      CLS##_methods,                                                          \
      (nj_u2)(sizeof(CLS##_methods) / sizeof(CLS##_methods[0])) },

static const nj_builtin_field Integer_fields[]   = { { "value", "I", 0 } };
static const nj_builtin_field Long_fields[]      = { { "value", "J", 0 } };
static const nj_builtin_field Short_fields[]     = { { "value", "S", 0 } };
static const nj_builtin_field Byte_fields[]      = { { "value", "B", 0 } };
static const nj_builtin_field Character_fields[] = { { "value", "C", 0 } };
static const nj_builtin_field Boolean_fields[]   = { { "value", "Z", 0 } };
static const nj_builtin_field Float_fields[]     = { { "value", "F", 0 } };
static const nj_builtin_field Double_fields[]    = { { "value", "D", 0 } };

static const nj_builtin_method Integer_methods[] = {
    M("<init>",        "(I)V", 0),
    M("intValue",      "()I",  0),
    M("byteValue",     "()B",  0),
    M("parseInt",      "(Ljava/lang/String;)I", NJ_ACC_STATIC),
    M("parseInt",      "(Ljava/lang/String;I)I", NJ_ACC_STATIC),
    M("toString",      "(I)Ljava/lang/String;", NJ_ACC_STATIC),
    M("toString",      "(II)Ljava/lang/String;", NJ_ACC_STATIC),
    M("toHexString",   "(I)Ljava/lang/String;", NJ_ACC_STATIC),
    M("valueOf",       "(I)Ljava/lang/Integer;", NJ_ACC_STATIC),
    M("valueOf",       "(Ljava/lang/String;)Ljava/lang/Integer;", NJ_ACC_STATIC),
    M("valueOf",       "(Ljava/lang/String;I)Ljava/lang/Integer;", NJ_ACC_STATIC),
    M("equals",        "(Ljava/lang/Object;)Z", 0),
    M("hashCode",      "()I", 0)
};
static const nj_builtin_method Long_methods[] = {
    M("<init>",   "(J)V", 0),
    M("longValue","()J",  0),
    M("intValue", "()I",  0),
    M("parseLong", "(Ljava/lang/String;)J", NJ_ACC_STATIC),
    M("parseLong", "(Ljava/lang/String;I)J", NJ_ACC_STATIC),
    M("valueOf",   "(Ljava/lang/String;)Ljava/lang/Long;", NJ_ACC_STATIC),
    M("valueOf",   "(J)Ljava/lang/Long;", NJ_ACC_STATIC),
    M("valueOf",   "(Ljava/lang/String;I)Ljava/lang/Long;", NJ_ACC_STATIC),
    M("toString",  "(J)Ljava/lang/String;", NJ_ACC_STATIC),
    M("toString",  "(JI)Ljava/lang/String;", NJ_ACC_STATIC)
};
static const nj_builtin_method Short_methods[] = {
    M("<init>",     "(S)V", 0),
    M("shortValue", "()S",  0),
    M("intValue",   "()I",  0),
    M("parseShort", "(Ljava/lang/String;)S", NJ_ACC_STATIC)
};
static const nj_builtin_method Byte_methods[] = {
    M("<init>",    "(B)V", 0),
    M("byteValue","()B",  0),
    M("intValue", "()I",  0),
    M("parseByte", "(Ljava/lang/String;)B", NJ_ACC_STATIC)
};
static const nj_builtin_method Character_methods[] = {
    M("<init>",      "(C)V", 0),
    M("charValue",   "()C",  0),
    M("isDigit",     "(C)Z", NJ_ACC_STATIC),
    M("isLetter",    "(C)Z", NJ_ACC_STATIC),
    M("isLowerCase", "(C)Z", NJ_ACC_STATIC),
    M("isUpperCase", "(C)Z", NJ_ACC_STATIC),
    M("toLowerCase", "(C)C", NJ_ACC_STATIC),
    M("toUpperCase", "(C)C", NJ_ACC_STATIC),
    M("digit",       "(CI)I", NJ_ACC_STATIC)
};
static const nj_builtin_method Boolean_methods[] = {
    M("<init>",      "(Z)V", 0),
    M("booleanValue","()Z",  0),
    M("valueOf",     "(Z)Ljava/lang/Boolean;", NJ_ACC_STATIC)
};
static const nj_builtin_method Float_methods[] = {
    M("<init>",     "(F)V", 0),
    M("floatValue", "()F",  0),
    M("isNaN",      "()Z",  0),
    M("isInfinite", "()Z",  0),
    M("isNaN",      "(F)Z", NJ_ACC_STATIC),
    M("isInfinite", "(F)Z", NJ_ACC_STATIC),
    M("intBitsToFloat", "(I)F", NJ_ACC_STATIC),
    M("floatToIntBits", "(F)I", NJ_ACC_STATIC),
    M("valueOf",        "(F)Ljava/lang/Float;", NJ_ACC_STATIC)
};
static const nj_builtin_method Double_methods[] = {
    M("<init>",      "(D)V", 0),
    M("doubleValue", "()D",  0),
    M("isNaN",       "()Z",  0),
    M("isInfinite",  "()Z",  0),
    M("isNaN",       "(D)Z", NJ_ACC_STATIC),
    M("isInfinite",  "(D)Z", NJ_ACC_STATIC),
    M("valueOf",     "(D)Ljava/lang/Double;", NJ_ACC_STATIC),
    M("valueOf",     "(Ljava/lang/String;)Ljava/lang/Double;", NJ_ACC_STATIC)
};

const nj_builtin_class nj_lib_boxing_classes[] = {
    WRAPPER_CLASS(Integer,   "java/lang/Object", "I", "(I)V", )
    WRAPPER_CLASS(Long,      "java/lang/Object", "J", "(J)V", )
    WRAPPER_CLASS(Short,     "java/lang/Object", "S", "(S)V", )
    WRAPPER_CLASS(Byte,      "java/lang/Object", "B", "(B)V", )
    WRAPPER_CLASS(Character, "java/lang/Object", "C", "(C)V", )
    WRAPPER_CLASS(Boolean,   "java/lang/Object", "Z", "(Z)V", )
    WRAPPER_CLASS(Float,     "java/lang/Object", "F", "(F)V", )
    WRAPPER_CLASS(Double,    "java/lang/Object", "D", "(D)V", )
};
const int nj_lib_boxing_class_count =
    (int)(sizeof(nj_lib_boxing_classes) / sizeof(nj_lib_boxing_classes[0]));

const nj_native_entry nj_lib_boxing_table[] = {
    { "java/lang/Integer",   "<init>",       "(I)V", int_init },
    { "java/lang/Integer",   "intValue",     "()I",  int_intValue },
    { "java/lang/Integer",   "parseInt",     "(Ljava/lang/String;)I", int_parseInt },
    { "java/lang/Integer",   "parseInt",     "(Ljava/lang/String;I)I", int_parseInt_radix },
    { "java/lang/Integer",   "byteValue",    "()B",                   int_byteValue },
    { "java/lang/Integer",   "valueOf",      "(Ljava/lang/String;)Ljava/lang/Integer;", int_valueOf_str },
    { "java/lang/Integer",   "valueOf",      "(Ljava/lang/String;I)Ljava/lang/Integer;", int_valueOf_str_radix },
    { "java/lang/Integer",   "hashCode",     "()I",                   int_hashCode },
    { "java/lang/Integer",   "equals",       "(Ljava/lang/Object;)Z", int_equals },
    { "java/lang/Integer",   "toString",     "(I)Ljava/lang/String;", int_toString },
    { "java/lang/Integer",   "toString",     "(II)Ljava/lang/String;", int_toString_radix },
    { "java/lang/Integer",   "toHexString",  "(I)Ljava/lang/String;", int_toHexString },
    { "java/lang/Integer",   "valueOf",      "(I)Ljava/lang/Integer;", int_valueOf },

    { "java/lang/Long",      "<init>",       "(J)V", Long_init },
    { "java/lang/Long",      "longValue",    "()J",  Long_longValue },
    { "java/lang/Long",      "intValue",     "()I",  Long_longValue },
    { "java/lang/Long",      "parseLong",    "(Ljava/lang/String;)J", Long_parseLong },
    { "java/lang/Long",      "parseLong",    "(Ljava/lang/String;I)J", Long_parseLong_radix },
    { "java/lang/Long",      "valueOf",      "(Ljava/lang/String;)Ljava/lang/Long;", Long_valueOf_str },
    { "java/lang/Long",      "valueOf",      "(Ljava/lang/String;I)Ljava/lang/Long;", Long_valueOf_str_radix },
    { "java/lang/Long",      "valueOf",      "(J)Ljava/lang/Long;", Long_valueOf },
    { "java/lang/Long",      "toString",     "(J)Ljava/lang/String;", long_toString },
    { "java/lang/Long",      "toString",     "(JI)Ljava/lang/String;", long_toString_radix },

    { "java/lang/Short",     "<init>",       "(S)V", Short_init },
    { "java/lang/Short",     "shortValue",   "()S",  Short_value },
    { "java/lang/Short",     "intValue",     "()I",  Short_value },
    { "java/lang/Short",     "parseShort",   "(Ljava/lang/String;)S", Short_parseShort },

    { "java/lang/Byte",      "<init>",       "(B)V", Byte_init },
    { "java/lang/Byte",      "byteValue",    "()B",  Byte_value },
    { "java/lang/Byte",      "intValue",     "()I",  Byte_value },
    { "java/lang/Byte",      "parseByte",    "(Ljava/lang/String;)B", Byte_parseByte },

    { "java/lang/Character", "<init>",       "(C)V", Character_init },
    { "java/lang/Character", "charValue",    "()C",  Character_value },
    { "java/lang/Character", "isDigit",      "(C)Z", Character_isDigit },
    { "java/lang/Character", "isLetter",     "(C)Z", Character_isLetter },
    { "java/lang/Character", "isLowerCase",  "(C)Z", Character_isLowerCase },
    { "java/lang/Character", "isUpperCase",  "(C)Z", Character_isUpperCase },
    { "java/lang/Character", "toLowerCase",  "(C)C", Character_toLowerCase },
    { "java/lang/Character", "toUpperCase",  "(C)C", Character_toUpperCase },
    { "java/lang/Character", "digit",        "(CI)I", Character_digit },

    { "java/lang/Boolean",   "<init>",       "(Z)V", Boolean_init },
    { "java/lang/Boolean",   "booleanValue", "()Z",  Boolean_value },
    { "java/lang/Boolean",   "valueOf",      "(Z)Ljava/lang/Boolean;", Boolean_valueOf },

    { "java/lang/Float",     "<init>",       "(F)V", Float_init },
    { "java/lang/Float",     "floatValue",   "()F",  Float_value },
    { "java/lang/Float",     "isNaN",        "()Z",  Float_isNaN },
    { "java/lang/Float",     "isInfinite",   "()Z",  Float_isInfinite },
    { "java/lang/Float",     "isNaN",        "(F)Z", Float_isNaN_v },
    { "java/lang/Float",     "isInfinite",   "(F)Z", Float_isInfinite_v },
    { "java/lang/Float",     "intBitsToFloat", "(I)F", Float_intBitsToFloat },
    { "java/lang/Float",     "floatToIntBits", "(F)I", Float_floatToIntBits },

    { "java/lang/Double",    "<init>",       "(D)V", Double_init },
    { "java/lang/Double",    "doubleValue",  "()D",  Double_value },
    { "java/lang/Double",    "isNaN",        "()Z",  Double_isNaN },
    { "java/lang/Double",    "isInfinite",   "()Z",  Double_isInfinite },
    { "java/lang/Double",    "isNaN",        "(D)Z", Double_isNaN_d },
    { "java/lang/Double",    "isInfinite",   "(D)Z", Double_isInfinite_d },
    { "java/lang/Double",    "valueOf",      "(Ljava/lang/String;)Ljava/lang/Double;", Double_valueOf_str },
    { "java/lang/Double",    "valueOf",      "(D)Ljava/lang/Double;", Double_valueOf },
    { "java/lang/Float",     "valueOf",      "(F)Ljava/lang/Float;", Float_valueOf }
};
const int nj_lib_boxing_count =
    (int)(sizeof(nj_lib_boxing_table) / sizeof(nj_lib_boxing_table[0]));
