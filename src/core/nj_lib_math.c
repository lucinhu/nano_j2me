/*
 * nano_j2me - nj_lib_math.c
 *
 * java.lang.Math.  All of it is C natives; no floating point is emulated in
 * Java, so this is the fastest available path on both hosted and embedded
 * targets (and the ESP32-S3 has single-precision hardware anyway).
 */
#include "nj_internal.h"

#if NJ_USE_LIBC
#  include <math.h>
#  include <stdlib.h>
#else
#  include <math.h>
#endif

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_UNUSED(x) ((void)(x))

NJFN(math_abs_i)
{
    nj_s4 v = NJ_ARG_INT(args, 0);
    ret->i = (v < 0) ? -v : v;
    return NJ_OK;
}

NJFN(math_abs_l)
{
    nj_s8 v = NJ_ARG_LONG(args, 0);
    ret->j = (v < 0) ? -v : v;
    return NJ_OK;
}

NJFN(math_abs_f)
{
    float v = NJ_ARG_FLOAT(args, 0);
    ret->f = (v < 0.0f) ? -v : v;
    return NJ_OK;
}

NJFN(math_abs_d)
{
    double v = NJ_ARG_DOUBLE(args, 0);
    ret->d = (v < 0.0) ? -v : v;
    return NJ_OK;
}

NJFN(math_max_i)  { nj_s4 a = NJ_ARG_INT(args,0), b = NJ_ARG_INT(args,1);
                    ret->i = (a >= b) ? a : b; return NJ_OK; }
NJFN(math_min_i)  { nj_s4 a = NJ_ARG_INT(args,0), b = NJ_ARG_INT(args,1);
                    ret->i = (a <= b) ? a : b; return NJ_OK; }
NJFN(math_max_l)  { nj_s8 a = NJ_ARG_LONG(args,0), b = NJ_ARG_LONG(args,1);
                    ret->j = (a >= b) ? a : b; return NJ_OK; }
NJFN(math_min_l)  { nj_s8 a = NJ_ARG_LONG(args,0), b = NJ_ARG_LONG(args,1);
                    ret->j = (a <= b) ? a : b; return NJ_OK; }
NJFN(math_max_f)  { float a = NJ_ARG_FLOAT(args,0), b = NJ_ARG_FLOAT(args,1);
                    ret->f = (a >= b) ? a : b; return NJ_OK; }
NJFN(math_min_f)  { float a = NJ_ARG_FLOAT(args,0), b = NJ_ARG_FLOAT(args,1);
                    ret->f = (a <= b) ? a : b; return NJ_OK; }
NJFN(math_max_d)  { double a = NJ_ARG_DOUBLE(args,0), b = NJ_ARG_DOUBLE(args,1);
                    ret->d = (a >= b) ? a : b; return NJ_OK; }
NJFN(math_min_d)  { double a = NJ_ARG_DOUBLE(args,0), b = NJ_ARG_DOUBLE(args,1);
                    ret->d = (a <= b) ? a : b; return NJ_OK; }

NJFN(math_sqrt)  { double x = NJ_ARG_DOUBLE(args,0);
                   ret->d = (x < 0.0) ? (0.0/0.0) : sqrt(x); return NJ_OK; }
NJFN(math_cbrt)  { ret->d = cbrt(NJ_ARG_DOUBLE(args,0)); return NJ_OK; }
NJFN(math_sin)   { ret->d = sin(NJ_ARG_DOUBLE(args,0)); return NJ_OK; }
NJFN(math_cos)   { ret->d = cos(NJ_ARG_DOUBLE(args,0)); return NJ_OK; }
NJFN(math_tan)   { ret->d = tan(NJ_ARG_DOUBLE(args,0)); return NJ_OK; }
NJFN(math_asin)  { ret->d = asin(NJ_ARG_DOUBLE(args,0)); return NJ_OK; }
NJFN(math_acos)  { ret->d = acos(NJ_ARG_DOUBLE(args,0)); return NJ_OK; }
NJFN(math_atan)  { ret->d = atan(NJ_ARG_DOUBLE(args,0)); return NJ_OK; }
NJFN(math_atan2) { ret->d = atan2(NJ_ARG_DOUBLE(args,0), NJ_ARG_DOUBLE(args,1));
                   return NJ_OK; }
NJFN(math_exp)   { ret->d = exp(NJ_ARG_DOUBLE(args,0)); return NJ_OK; }
NJFN(math_log)   { ret->d = log(NJ_ARG_DOUBLE(args,0)); return NJ_OK; }
NJFN(math_pow)   { ret->d = pow(NJ_ARG_DOUBLE(args,0), NJ_ARG_DOUBLE(args,1));
                   return NJ_OK; }
NJFN(math_ceil)  { ret->d = ceil(NJ_ARG_DOUBLE(args,0)); return NJ_OK; }
NJFN(math_floor) { ret->d = floor(NJ_ARG_DOUBLE(args,0)); return NJ_OK; }
NJFN(math_rint)  { ret->d = rint(NJ_ARG_DOUBLE(args,0)); return NJ_OK; }

/* Math.round(float) follows the Java rule: floor(x + 0.5). */
NJFN(math_round_f)
{
    float x = NJ_ARG_FLOAT(args, 0);
    double r;

    if (x != x) {
        ret->i = 0;
        return NJ_OK;
    }
    r = floor((double)x + 0.5);
    if (r > 2147483647.0) {
        ret->i = 0x7FFFFFFF;
    } else if (r < -2147483648.0) {
        ret->i = (nj_s4)0x80000000;
    } else {
        ret->i = (nj_s4)r;
    }
    return NJ_OK;
}

NJFN(math_round_d)
{
    double x = NJ_ARG_DOUBLE(args, 0);
    double r;

    if (x != x) {
        ret->j = 0;
        return NJ_OK;
    }
    r = floor(x + 0.5);
    if (r > 9223372036854775807.0) {
        ret->j = (nj_s8)0x7FFFFFFFFFFFFFFFLL;
    } else if (r < -9223372036854775808.0) {
        ret->j = (nj_s8)0x8000000000000000LL;
    } else {
        ret->j = (nj_s8)r;
    }
    return NJ_OK;
}

NJFN(math_random)
{
    /*
     * java.lang.Math.random() must return a double in [0,1).  A 48-bit LCG
     * (the classic java.util.Random constants) keeps this identical on every
     * platform, including ones whose libc rand() differs.
     */
    nj_field *seed = nj_field_lookup(vm, "java/lang/Math", "seed", "J", 1);
    nj_u8 s;

    if (!seed) {
        ret->d = 0.0;
        return NJ_OK;
    }
    s = (nj_u8)nj_field_read_long(vm, NULL, seed);
    s = s * 25214903917ULL + 11ULL;
    nj_field_write_long(vm, NULL, seed, (nj_s8)s);
    ret->d = (double)((s >> 16) & 0x7FFFFFFFULL) / 2147483648.0;
    return NJ_OK;
}

NJFN(math_toRadians) { ret->d = NJ_ARG_DOUBLE(args,0) * (3.14159265358979323846 / 180.0);
                       return NJ_OK; }
NJFN(math_toDegrees) { ret->d = NJ_ARG_DOUBLE(args,0) * (180.0 / 3.14159265358979323846);
                       return NJ_OK; }

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }

static const nj_builtin_field math_fields[] = {
    { "seed", "J", NJ_ACC_STATIC | NJ_ACC_PRIVATE },
    { "PI",   "D", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC },
    { "E",    "D", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC }
};

#define D1(n) M(n, "(D)D", NJ_ACC_STATIC)
#define FF2(n) M(n, "(FF)F", NJ_ACC_STATIC)
#define DD2(n) M(n, "(DD)D", NJ_ACC_STATIC)
#define II2(n) M(n, "(II)I", NJ_ACC_STATIC)
#define JJ2(n) M(n, "(JJ)J", NJ_ACC_STATIC)

static const nj_builtin_method math_methods[] = {
    M("abs", "(I)I", NJ_ACC_STATIC),
    M("abs", "(J)J", NJ_ACC_STATIC),
    M("abs", "(F)F", NJ_ACC_STATIC),
    D1("abs"),
    II2("max"), JJ2("max"), FF2("max"), DD2("max"),
    II2("min"), JJ2("min"), FF2("min"), DD2("min"),
    D1("sqrt"), D1("cbrt"), D1("sin"), D1("cos"), D1("tan"),
    D1("asin"), D1("acos"), D1("atan"), DD2("atan2"),
    D1("exp"), D1("log"), DD2("pow"), D1("ceil"), D1("floor"), D1("rint"),
    M("round", "(F)I", NJ_ACC_STATIC),
    M("round", "(D)J", NJ_ACC_STATIC),
    M("random", "()D", NJ_ACC_STATIC),
    D1("toRadians"), D1("toDegrees")
};

const nj_builtin_class nj_lib_math_classes[] = {
    { "java/lang/Math", "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_FINAL,
      math_fields,
      (nj_u2)(sizeof(math_fields) / sizeof(math_fields[0])),
      math_methods,
      (nj_u2)(sizeof(math_methods) / sizeof(math_methods[0])) }
};
const int nj_lib_math_class_count = 1;

const nj_native_entry nj_lib_math_table[] = {
    { "java/lang/Math", "abs",     "(I)I", math_abs_i },
    { "java/lang/Math", "abs",     "(J)J", math_abs_l },
    { "java/lang/Math", "abs",     "(F)F", math_abs_f },
    { "java/lang/Math", "abs",     "(D)D", math_abs_d },
    { "java/lang/Math", "max",     "(II)I", math_max_i },
    { "java/lang/Math", "min",     "(II)I", math_min_i },
    { "java/lang/Math", "max",     "(JJ)J", math_max_l },
    { "java/lang/Math", "min",     "(JJ)J", math_min_l },
    { "java/lang/Math", "max",     "(FF)F", math_max_f },
    { "java/lang/Math", "min",     "(FF)F", math_min_f },
    { "java/lang/Math", "max",     "(DD)D", math_max_d },
    { "java/lang/Math", "min",     "(DD)D", math_min_d },
    { "java/lang/Math", "sqrt",    "(D)D", math_sqrt },
    { "java/lang/Math", "cbrt",    "(D)D", math_cbrt },
    { "java/lang/Math", "sin",     "(D)D", math_sin },
    { "java/lang/Math", "cos",     "(D)D", math_cos },
    { "java/lang/Math", "tan",     "(D)D", math_tan },
    { "java/lang/Math", "asin",    "(D)D", math_asin },
    { "java/lang/Math", "acos",    "(D)D", math_acos },
    { "java/lang/Math", "atan",    "(D)D", math_atan },
    { "java/lang/Math", "atan2",   "(DD)D", math_atan2 },
    { "java/lang/Math", "exp",     "(D)D", math_exp },
    { "java/lang/Math", "log",     "(D)D", math_log },
    { "java/lang/Math", "pow",     "(DD)D", math_pow },
    { "java/lang/Math", "ceil",    "(D)D", math_ceil },
    { "java/lang/Math", "floor",   "(D)D", math_floor },
    { "java/lang/Math", "rint",    "(D)D", math_rint },
    { "java/lang/Math", "round",   "(F)I", math_round_f },
    { "java/lang/Math", "round",   "(D)J", math_round_d },
    { "java/lang/Math", "random",  "()D",  math_random },
    { "java/lang/Math", "toRadians", "(D)D", math_toRadians },
    { "java/lang/Math", "toDegrees", "(D)D", math_toDegrees }
};
const int nj_lib_math_count =
    (int)(sizeof(nj_lib_math_table) / sizeof(nj_lib_math_table[0]));
