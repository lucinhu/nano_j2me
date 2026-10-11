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

NJFN(int_parseInt)
{
    nj_ref s = NJ_ARG_REF(args, 0);
    nj_u4 i = 0u;
    int neg = 0;
    nj_s8 acc = 0;
    const nj_u2 *p;

    if (!s) {
        return nj_throw(vm, "java/lang/NumberFormatException", "null");
    }
    p = nj_string_chars(s);
    if (s->length > 0u && (p[0] == '+' || p[0] == '-')) {
        neg = (p[0] == '-');
        i = 1u;
    }
    if (i >= s->length) {
        return nj_throw(vm, "java/lang/NumberFormatException", NULL);
    }
    for (; i < s->length; i++) {
        nj_u2 c = p[i];
        if (c < '0' || c > '9') {
            return nj_throw(vm, "java/lang/NumberFormatException", NULL);
        }
        acc = acc * 10 + (nj_s8)(c - '0');
        if (acc > 4294967296LL) {
            return nj_throw(vm, "java/lang/NumberFormatException", NULL);
        }
    }
    ret->i = (nj_s4)(neg ? -acc : acc);
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

NJFN(int_valueOf)
{
    nj_value a1[1];
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
        nj_field_write_int(vm, NJ_THIS(args), f, (nj_s4)NJ_ARG_FLOAT(args, 0));
    }
    return NJ_OK;
}

NJFN(Float_value)
{
    nj_field *f = vf(vm, "java/lang/Float", "F");
    ret->f = f ? (float)nj_field_read_int(vm, NJ_THIS(args), f) : 0.0f;
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
    nj_value a1[1];
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
    M("parseInt",      "(Ljava/lang/String;)I", NJ_ACC_STATIC),
    M("toString",      "(I)Ljava/lang/String;", NJ_ACC_STATIC),
    M("toString",      "(II)Ljava/lang/String;", NJ_ACC_STATIC),
    M("toHexString",   "(I)Ljava/lang/String;", NJ_ACC_STATIC),
    M("valueOf",       "(I)Ljava/lang/Integer;", NJ_ACC_STATIC),
    M("equals",        "(Ljava/lang/Object;)Z", 0),
    M("hashCode",      "()I", 0)
};
static const nj_builtin_method Long_methods[] = {
    M("<init>",   "(J)V", 0),
    M("longValue","()J",  0),
    M("intValue", "()I",  0)
};
static const nj_builtin_method Short_methods[] = {
    M("<init>",     "(S)V", 0),
    M("shortValue", "()S",  0),
    M("intValue",   "()I",  0)
};
static const nj_builtin_method Byte_methods[] = {
    M("<init>",    "(B)V", 0),
    M("byteValue","()B",  0),
    M("intValue", "()I",  0)
};
static const nj_builtin_method Character_methods[] = {
    M("<init>",      "(C)V", 0),
    M("charValue",   "()C",  0),
    M("isDigit",     "(C)Z", NJ_ACC_STATIC),
    M("isLetter",    "(C)Z", NJ_ACC_STATIC),
    M("isLowerCase", "(C)Z", NJ_ACC_STATIC),
    M("isUpperCase", "(C)Z", NJ_ACC_STATIC),
    M("toLowerCase", "(C)C", NJ_ACC_STATIC),
    M("toUpperCase", "(C)C", NJ_ACC_STATIC)
};
static const nj_builtin_method Boolean_methods[] = {
    M("<init>",      "(Z)V", 0),
    M("booleanValue","()Z",  0),
    M("valueOf",     "(Z)Ljava/lang/Boolean;", NJ_ACC_STATIC)
};
static const nj_builtin_method Float_methods[] = {
    M("<init>",     "(F)V", 0),
    M("floatValue", "()F",  0)
};
static const nj_builtin_method Double_methods[] = {
    M("<init>",      "(D)V", 0),
    M("doubleValue", "()D",  0)
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
    { "java/lang/Integer",   "toString",     "(I)Ljava/lang/String;", int_toString },
    { "java/lang/Integer",   "toString",     "(II)Ljava/lang/String;", int_toString_radix },
    { "java/lang/Integer",   "toHexString",  "(I)Ljava/lang/String;", int_toHexString },
    { "java/lang/Integer",   "valueOf",      "(I)Ljava/lang/Integer;", int_valueOf },

    { "java/lang/Long",      "<init>",       "(J)V", Long_init },
    { "java/lang/Long",      "longValue",    "()J",  Long_longValue },
    { "java/lang/Long",      "intValue",     "()I",  Long_longValue },

    { "java/lang/Short",     "<init>",       "(S)V", Short_init },
    { "java/lang/Short",     "shortValue",   "()S",  Short_value },
    { "java/lang/Short",     "intValue",     "()I",  Short_value },

    { "java/lang/Byte",      "<init>",       "(B)V", Byte_init },
    { "java/lang/Byte",      "byteValue",    "()B",  Byte_value },
    { "java/lang/Byte",      "intValue",     "()I",  Byte_value },

    { "java/lang/Character", "<init>",       "(C)V", Character_init },
    { "java/lang/Character", "charValue",    "()C",  Character_value },
    { "java/lang/Character", "isDigit",      "(C)Z", Character_isDigit },
    { "java/lang/Character", "isLetter",     "(C)Z", Character_isLetter },
    { "java/lang/Character", "isLowerCase",  "(C)Z", Character_isLowerCase },
    { "java/lang/Character", "isUpperCase",  "(C)Z", Character_isUpperCase },
    { "java/lang/Character", "toLowerCase",  "(C)C", Character_toLowerCase },
    { "java/lang/Character", "toUpperCase",  "(C)C", Character_toUpperCase },

    { "java/lang/Boolean",   "<init>",       "(Z)V", Boolean_init },
    { "java/lang/Boolean",   "booleanValue", "()Z",  Boolean_value },
    { "java/lang/Boolean",   "valueOf",      "(Z)Ljava/lang/Boolean;", Boolean_valueOf },

    { "java/lang/Float",     "<init>",       "(F)V", Float_init },
    { "java/lang/Float",     "floatValue",   "()F",  Float_value },

    { "java/lang/Double",    "<init>",       "(D)V", Double_init },
    { "java/lang/Double",    "doubleValue",  "()D",  Double_value }
};
const int nj_lib_boxing_count =
    (int)(sizeof(nj_lib_boxing_table) / sizeof(nj_lib_boxing_table[0]));
