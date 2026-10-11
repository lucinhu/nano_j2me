/*
 * nano_j2me - nj_types.h
 *
 * Fundamental types, values, error codes and limits shared by every module.
 */
#ifndef NJ_TYPES_H
#define NJ_TYPES_H

#include "nj_config.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Scalars                                                             */
/* ------------------------------------------------------------------ */
typedef int8_t   nj_s1;
typedef uint8_t  nj_u1;
typedef int16_t  nj_s2;
typedef uint16_t nj_u2;
typedef int32_t  nj_s4;
typedef uint32_t nj_u4;
typedef int64_t  nj_s8;
typedef uint64_t nj_u8;

typedef nj_u2 nj_char;   /* java char  */
typedef nj_u1 nj_byte;   /* java byte  */
typedef nj_u4 nj_size;

/* ------------------------------------------------------------------ */
/* Status codes                                                        */
/* ------------------------------------------------------------------ */
typedef enum nj_status {
    NJ_OK = 0,
    NJ_ERR = -1,              /* generic failure                       */
    NJ_ERR_OOM = -2,          /* java heap exhausted                   */
    NJ_ERR_HALT = -3,         /* thread finished                       */
    NJ_ERR_YIELD = -4,        /* scheduler quantum expired             */
    NJ_ERR_TIMEOUT = -5,      /* blocking native timed out             */
    NJ_ERR_UNSUPPORTED = -6,  /* feature not compiled in               */
    NJ_ERR_FORMAT = -7,       /* malformed jar/class/png               */
    NJ_ERR_STATE = -8,        /* wrong VM state for the call           */
    NJ_ERR_EXIT = -9,         /* System.exit() requested               */
    NJ_ERR_NOMEM = -10        /* host allocation failed                */
} nj_status;

/* JVM access flags (subset actually consulted by the runtime). */
#define NJ_ACC_PUBLIC       0x0001
#define NJ_ACC_PRIVATE      0x0002
#define NJ_ACC_PROTECTED    0x0004
#define NJ_ACC_STATIC       0x0008
#define NJ_ACC_FINAL        0x0010
#define NJ_ACC_SYNCHRONIZED 0x0020
#define NJ_ACC_NATIVE       0x0100
#define NJ_ACC_ABSTRACT     0x0400
#define NJ_ACC_INTERFACE    0x0200

/* Constant pool tags. */
#define NJ_CP_UTF8               1
#define NJ_CP_INTEGER            3
#define NJ_CP_FLOAT              4
#define NJ_CP_LONG               5
#define NJ_CP_DOUBLE             6
#define NJ_CP_CLASS              7
#define NJ_CP_STRING             8
#define NJ_CP_FIELDREF           9
#define NJ_CP_METHODREF         10
#define NJ_CP_IFACEMETHODREF    11
#define NJ_CP_NAMEANDTYPE       12

/* Primitive type codes used by the class model, arrays and reflection. */
typedef enum nj_ptype {
    NJ_PT_VOID = 0,
    NJ_PT_BOOLEAN,
    NJ_PT_CHAR,
    NJ_PT_BYTE,
    NJ_PT_SHORT,
    NJ_PT_INT,
    NJ_PT_LONG,
    NJ_PT_FLOAT,
    NJ_PT_DOUBLE,
    NJ_PT_OBJECT
} nj_ptype;

/* ------------------------------------------------------------------ */
/* Object model                                                        */
/* ------------------------------------------------------------------ */
typedef struct nj_class  nj_class;
typedef struct nj_object nj_object;

/* A Java reference.  NULL is the Java `null`. */
typedef nj_object *nj_ref;

#define NJ_OBJ_PLAIN  0u  /* ordinary instance                       */
#define NJ_OBJ_ARRAY  1u  /* array (primitive or reference)          */
#define NJ_OBJ_STRING 2u  /* java.lang.String                        */
#define NJ_OBJ_CLASS  3u  /* java.lang.Class mirror                  */
#define NJ_OBJ_THROW  4u  /* reserved                                */

/* Array element kind, stored in flags>>8 for arrays. */
#define NJ_ARRAY_T_BOOLEAN 1u
#define NJ_ARRAY_T_BYTE    2u
#define NJ_ARRAY_T_CHAR    3u
#define NJ_ARRAY_T_SHORT   4u
#define NJ_ARRAY_T_INT     5u
#define NJ_ARRAY_T_LONG    6u
#define NJ_ARRAY_T_FLOAT   7u
#define NJ_ARRAY_T_DOUBLE  8u
#define NJ_ARRAY_T_REF     9u

struct nj_object {
    nj_class *cls;      /* java.lang.Class-visible type of this object   */
    union {
        nj_u1  *bytes;  /* array payload (also string UTF-16 payload)    */
        nj_ref *refs;   /* convenience alias for reference arrays        */
        void   *raw;
        nj_class *mirror; /* NJ_OBJ_CLASS: the mirrored class            */
    } u;
    nj_u4     length;   /* array length, or string length in chars       */
    nj_u4     size;     /* total bytes of this allocation (GC sweep)     */
    nj_u1     kind;     /* NJ_OBJ_*                                      */
    nj_u1     mark;     /* GC mark byte                                  */
    nj_u2     aflags;   /* array element kind (see NJ_ARRAY_T_*)         */
    /* Instance fields follow the header in memory (see nj_heap.c). */
};

/* A JVM value.  long/double occupy two slots in locals/operand stack. */
typedef union nj_value {
    nj_s4   i;
    nj_s8   j;
    float   f;
    double  d;
    nj_ref  r;
    nj_u4   bits;
} nj_value;

/* ------------------------------------------------------------------ */
/* Natives                                                             */
/* ------------------------------------------------------------------ */
struct nj_vm;
struct nj_thread;

/*
 * A native implementation.
 *
 * `args`  points at the first argument slot (i.e. locals[0] of the frame is
 *         the receiver for instance methods; args == &locals[is_static?0:1]).
 * `ret`   receives the return value; for long/double the caller must store
 *         both halves via ret[0] and ret[1].
 *
 * Returning NJ_OK means "value written"; any other code means the native has
 * raised a Java exception (nj_vm_throw*) or wants to block (NJ_ERR_TIMEOUT).
 */
typedef nj_status (*nj_native_fn)(struct nj_vm *vm, struct nj_thread *th,
                                  nj_value *args, nj_value *ret);

/* ------------------------------------------------------------------ */
/* Limitations mirroring real CLDC                                      */
/* ------------------------------------------------------------------ */
#define NJ_MAX_DIMENSIONS 8

#ifdef __cplusplus
}
#endif

#endif /* NJ_TYPES_H */
