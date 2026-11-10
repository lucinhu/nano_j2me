/* System.arraycopy through the public call bridge.
 *
 * Authority: CLDC 1.1 java.lang.System.arraycopy: null -> array/primitive
 * compatibility -> bounds -> first failing reference element, with ONLY the
 * successful prefix copied. Same-array copies behave as through a temporary.
 * https://docs.oracle.com/javame/config/cldc/ref-impl/cldc1.1/jsr139/java/lang/System.html
 *
 * Expectations are fixed snapshot-index tables and CLDC contracts, independent
 * of the native copy loop. A desktop Java counterpart lives in the local audit
 * artifacts. Native classless arrays are additional C-runtime compatibility
 * fixtures, not objects that a desktop Java program can directly construct.
 */
#include "nj2me/nj.h"
#include "nj_internal.h" /* GC roots and explicit classless-array fixtures only */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define ASE "java/lang/ArrayStoreException"
#define NPE "java/lang/NullPointerException"
#define BOUNDS "java/lang/IndexOutOfBoundsException"

static unsigned checks, failures, calls;
static const char *context = "setup";
static const int unchanged[6] = { 0, 1, 2, 3, 4, 5 };
static const int right_copy[6] = { 0, 0, 1, 2, 3, 4 };
static const int left_copy[6] = { 1, 2, 3, 4, 5, 5 };

static void check(int yes, const char *what)
{
    ++checks;
    if (!yes) {
        if (failures < 60) fprintf(stderr, "FAIL %s: %s\n", context, what);
        ++failures;
    }
}

static void fixture(int yes, const char *what)
{
    if (!yes) {
        fprintf(stderr, "arraycopy fixture failure: %s\n", what);
        exit(2);
    }
}

static nj_ref keep(nj_vm *vm, nj_ref value)
{
    nj_u4 before = nj_root_mark(vm);
    fixture(value != NULL && !nj_has_exception(vm), "object allocation");
    fixture(nj_root(vm, value) == value && nj_root_mark(vm) == before + 1u,
            "temporary root insertion");
    return value;
}

static nj_ref refs(nj_vm *vm, const char *component, nj_u4 length)
{
    return keep(vm, nj_object_array_new(vm, component, length));
}

static int is_exception(nj_vm *vm, const char *name)
{
    nj_class *cls = vm->exception ? vm->exception->cls : NULL;
    for (; cls; cls = cls->super) {
        if (cls->name && strcmp(cls->name, name) == 0) return 1;
    }
    return 0;
}

static void copy(nj_vm *vm, nj_ref src, nj_s4 from, nj_ref dst, nj_s4 to,
                 nj_s4 count, const char *exception)
{
    nj_value args[5], result;
    nj_status status;
    nj_u4 roots = nj_root_mark(vm);
    fixture(!nj_has_exception(vm), "clean call entry");
    memset(args, 0, sizeof(args));
    memset(&result, 0, sizeof(result));
    args[0].r = src; args[1].i = from; args[2].r = dst;
    args[3].i = to; args[4].i = count;
    ++calls;
    status = nj_call_static(vm, "java/lang/System", "arraycopy",
                           "(Ljava/lang/Object;ILjava/lang/Object;II)V",
                           args, &result);
    if (exception) {
        int ok = status != NJ_OK && nj_has_exception(vm) && is_exception(vm, exception);
        if (!ok && failures < 60) {
            const char *actual = nj_exception_describe(vm);
            fprintf(stderr, "  expected %s, status=%d actual=%s\n", exception,
                    (int)status, actual ? actual : "none");
        }
        check(ok, "specified exception (bounds subclasses accepted)");
    } else {
        check(status == NJ_OK && !nj_has_exception(vm), "copy succeeds");
    }
    /* A failing call is already recorded; clear only to run remaining cases. */
    nj_clear_exception(vm);
    check(nj_root_mark(vm) == roots, "call balances temporary roots");
}

static void put_bits(nj_ref array, size_t width, unsigned i, uint64_t value)
{
    unsigned char *p = (unsigned char *)nj_array_data(array) + width * i;
    switch (width) {
    case 1: { uint8_t x = (uint8_t)value; memcpy(p, &x, sizeof(x)); break; }
    case 2: { uint16_t x = (uint16_t)value; memcpy(p, &x, sizeof(x)); break; }
    case 4: { uint32_t x = (uint32_t)value; memcpy(p, &x, sizeof(x)); break; }
    case 8: memcpy(p, &value, sizeof(value)); break;
    default: fixture(0, "primitive width"); break;
    }
}

static uint64_t bits(nj_ref array, size_t width, unsigned i)
{
    const unsigned char *p = (const unsigned char *)nj_array_data(array) + width * i;
    switch (width) {
    case 1: { uint8_t x; memcpy(&x, p, sizeof(x)); return x; }
    case 2: { uint16_t x; memcpy(&x, p, sizeof(x)); return x; }
    case 4: { uint32_t x; memcpy(&x, p, sizeof(x)); return x; }
    case 8: { uint64_t x; memcpy(&x, p, sizeof(x)); return x; }
    default: fixture(0, "primitive width"); return 0;
    }
}

static const struct primitive {
    const char *type;
    size_t width;
    uint64_t values[6];
} primitives[] = {
    { "Z", 1, { 0, 1, 0, 1, 1, 0 } },
    { "B", 1, { 0, 0x80, 0xff, 0x7f, 1, 0x55 } },
    { "C", 2, { 0, 0x8000, 0xffff, 0x7fff, 1, 0x1234 } },
    { "S", 2, { 0, 0x8000, 0xffff, 0x7fff, 1, 0x4321 } },
    { "I", 4, { 0, UINT32_C(0x80000000), UINT32_C(0xffffffff),
                 UINT32_C(0x7fffffff), 1, UINT32_C(0x12345678) } },
    { "F", 4, { 0, UINT32_C(0x80000000), UINT32_C(0x7fc12345),
                 UINT32_C(0xff800000), UINT32_C(0x7f800000), UINT32_C(0x3fc00000) } },
    { "J", 8, { 0, UINT64_C(0x8000000000000000), UINT64_C(0xffffffffffffffff),
                 UINT64_C(0x7fffffffffffffff), 1, UINT64_C(0x1234567887654321) } },
    { "D", 8, { 0, UINT64_C(0x8000000000000000), UINT64_C(0x7ff8123456789abc),
                 UINT64_C(0xfff0000000000000), UINT64_C(0x7ff0000000000000),
                 UINT64_C(0x3ff8000000000000) } }
};

static void seed_primitive(nj_ref array, const struct primitive *p)
{
    unsigned i;
    for (i = 0; i < 6; ++i) put_bits(array, p->width, i, p->values[i]);
}

static void primitive_snapshot(nj_ref array, const struct primitive *p,
                               const int expected[6])
{
    unsigned i;
    for (i = 0; i < 6; ++i)
        check(bits(array, p->width, i) == p->values[expected[i]],
              "raw primitive component matches fixed snapshot");
}

static void primitive_cases(nj_vm *vm)
{
    static const int distinct[6] = { 0, 1, 1, 2, 3, 5 };
    size_t k, j;
    for (k = 0; k < COUNT(primitives); ++k) {
        const struct primitive *p = &primitives[k];
        nj_u4 mark = nj_root_mark(vm);
        nj_ref src = keep(vm, nj_array_new(vm, p->type, 6));
        nj_ref dst = keep(vm, nj_array_new(vm, p->type, 6));
        context = p->type;
        seed_primitive(src, p); seed_primitive(dst, p);
        copy(vm, src, 1, dst, 2, 3, NULL);
        primitive_snapshot(src, p, unchanged);
        primitive_snapshot(dst, p, distinct);
        seed_primitive(src, p);
        copy(vm, src, 0, src, 1, 5, NULL);
        primitive_snapshot(src, p, right_copy);
        seed_primitive(src, p);
        copy(vm, src, 1, src, 0, 5, NULL);
        primitive_snapshot(src, p, left_copy);
        seed_primitive(src, p);
        copy(vm, src, 0, src, 0, 6, NULL);
        primitive_snapshot(src, p, unchanged);
        copy(vm, src, 6, src, 6, 0, NULL);
        primitive_snapshot(src, p, unchanged);
        for (j = 0; j < COUNT(primitives); ++j) {
            nj_u4 inner = nj_root_mark(vm);
            nj_ref other;
            if (j == k) continue;
            other = keep(vm, nj_array_new(vm, primitives[j].type, 6));
            seed_primitive(other, &primitives[j]);
            copy(vm, src, 0, other, 0, 1, ASE);
            primitive_snapshot(other, &primitives[j], unchanged);
            copy(vm, src, 0, other, 0, 0, ASE);
            primitive_snapshot(other, &primitives[j], unchanged);
            nj_root_release(vm, inner);
        }
        nj_root_release(vm, mark);
    }
}

static void seed_refs(nj_ref array, nj_ref const values[6])
{
    unsigned i;
    for (i = 0; i < 6; ++i) nj_array_set_ref(array, i, values[i]);
}

static void ref_snapshot(nj_ref array, nj_ref const values[6], const int expected[6])
{
    unsigned i;
    for (i = 0; i < 6; ++i)
        check(nj_array_get_ref(array, i) == values[expected[i]],
              "reference identity matches fixed snapshot");
}

static void overlap_refs(nj_vm *vm, nj_ref const values[6], const char *component,
                         int classless)
{
    nj_u4 mark = nj_root_mark(vm);
    nj_ref a = classless ? keep(vm, nj_new_array(vm, NULL, 6, NJ_ARRAY_T_REF)) :
                          refs(vm, component, 6);
    context = classless ? "classless reference overlap" : component;
    seed_refs(a, values);
    copy(vm, a, 0, a, 1, 5, NULL); ref_snapshot(a, values, right_copy);
    seed_refs(a, values);
    copy(vm, a, 1, a, 0, 5, NULL); ref_snapshot(a, values, left_copy);
    seed_refs(a, values);
    copy(vm, a, 1, a, 1, 4, NULL); ref_snapshot(a, values, unchanged);
    copy(vm, a, 6, a, 6, 0, NULL); ref_snapshot(a, values, unchanged);
    nj_root_release(vm, mark);
}

static void reference_cases(nj_vm *vm, nj_ref const strings[6], nj_ref sentinel,
                            nj_ref bad)
{
    static const int prefix[4][6] = {
        { -1, -1, -1, -1, -1, -1 },
        { -1,  0, -1, -1, -1, -1 },
        { -1,  0, -2, -1, -1, -1 },
        { -1,  0, -2,  1, -1, -1 }
    };
    unsigned k, i;
    overlap_refs(vm, strings, "java/lang/String", 0);
    overlap_refs(vm, strings, "java/lang/Object", 0);
    overlap_refs(vm, strings, "java/lang/Object", 1);
    context = "reference prefix on ArrayStoreException";
    for (k = 0; k < 4; ++k) {
        nj_u4 mark = nj_root_mark(vm);
        nj_ref src = refs(vm, "java/lang/Object", 6);
        nj_ref dst = refs(vm, "java/lang/String", 6);
        nj_ref before[6] = { bad, strings[0], NULL, strings[1], strings[2], bad };
        before[k + 1] = bad;
        seed_refs(src, before);
        for (i = 0; i < 6; ++i) nj_array_set_ref(dst, i, sentinel);
        copy(vm, src, 1, dst, 1, 4, ASE);
        for (i = 0; i < 6; ++i) {
            int token = prefix[k][i];
            nj_ref expected = token == -1 ? sentinel : token == -2 ? NULL : strings[token];
            check(nj_array_get_ref(dst, i) == expected,
                  "only valid prefix changes; failing element and tail untouched");
            check(nj_array_get_ref(src, i) == before[i], "source untouched on failure");
        }
        nj_root_release(vm, mark);
    }
    {
        nj_u4 mark = nj_root_mark(vm);
        nj_ref src = refs(vm, "java/lang/Object", 6);
        nj_ref dst = refs(vm, "java/lang/String", 6);
        nj_ref values[6] = { bad, strings[0], NULL, strings[1], bad, bad };
        context = "reference selected range and covariance";
        seed_refs(src, values);
        for (i = 0; i < 6; ++i) nj_array_set_ref(dst, i, sentinel);
        copy(vm, src, 1, dst, 2, 3, NULL);
        check(nj_array_get_ref(dst, 0) == sentinel && nj_array_get_ref(dst, 1) == sentinel &&
              nj_array_get_ref(dst, 2) == strings[0] && nj_array_get_ref(dst, 3) == NULL &&
              nj_array_get_ref(dst, 4) == strings[1] && nj_array_get_ref(dst, 5) == sentinel,
              "Object[] -> String[] accepts selected values; ignores invalid outside range");
        copy(vm, dst, 0, src, 0, 6, NULL);
        for (i = 0; i < 6; ++i)
            check(nj_array_get_ref(src, i) == nj_array_get_ref(dst, i),
                  "String[] -> Object[] widens by reference identity");
        nj_root_release(vm, mark);
    }
}

static void invalid_cases(nj_vm *vm, nj_ref object, nj_ref string)
{
    static const nj_s4 positions[][3] = {
        { -1, 0, 0 }, { 0, -1, 0 }, { 0, 0, -1 },
        { 5, 0, 0 }, { 0, 5, 0 }, { 4, 0, 1 }, { 0, 4, 1 },
        { 0, 0, 5 }, { 3, 0, 2 }, { 0, 3, 2 },
        { INT32_MAX, 0, 1 }, { 0, INT32_MAX, 1 },
        { 1, 0, INT32_MAX }, { 0, 1, INT32_MAX },
        { INT32_MAX, INT32_MAX, INT32_MAX },
        { INT32_MIN, 0, 0 }, { 0, INT32_MIN, 0 }, { 0, 0, INT32_MIN }
    };
    nj_u4 mark = nj_root_mark(vm);
    nj_ref ints = keep(vm, nj_int_array_new(vm, 4));
    nj_ref dst = keep(vm, nj_int_array_new(vm, 4));
    nj_ref bytes = keep(vm, nj_byte_array_new(vm, 4));
    nj_ref objects = refs(vm, "java/lang/Object", 4);
    nj_ref strings = refs(vm, "java/lang/String", 4);
    nj_ref nested = refs(vm, "[B", 4);
    size_t i;
    for (i = 0; i < 4; ++i) {
        nj_array_set_int(ints, (nj_u4)i, (nj_s4)(10 + i));
        nj_array_set_int(dst, (nj_u4)i, (nj_s4)(20 + i));
        nj_array_set_ref(objects, (nj_u4)i, object);
        nj_array_set_ref(strings, (nj_u4)i, string);
    }
    context = "invalid ranges and arithmetic extremes";
    for (i = 0; i < COUNT(positions); ++i) {
        unsigned j;
        copy(vm, ints, positions[i][0], dst, positions[i][1], positions[i][2], BOUNDS);
        for (j = 0; j < 4; ++j) {
            check(nj_array_get_int(dst, j) == (nj_s4)(20 + j), "bounds leaves destination intact");
            check(nj_array_get_int(ints, j) == (nj_s4)(10 + j), "bounds leaves source intact");
        }
        copy(vm, objects, positions[i][0], strings, positions[i][1], positions[i][2], BOUNDS);
        for (j = 0; j < 4; ++j)
            check(nj_array_get_ref(strings, j) == string,
                  "bounds precedes per-element reference incompatibility");
    }
    context = "null and structural type priority, even zero length";
    copy(vm, NULL, -1, dst, -1, -1, NPE);
    copy(vm, ints, -1, NULL, -1, -1, NPE);
    copy(vm, NULL, 0, NULL, 0, 0, NPE);
    copy(vm, object, 0, NULL, 0, 0, NPE);
    copy(vm, NULL, 0, object, 0, 0, NPE);
    copy(vm, object, 0, dst, 0, 0, ASE);
    copy(vm, ints, 0, object, 0, 0, ASE);
    copy(vm, string, -1, dst, -1, -1, ASE);
    copy(vm, ints, -1, bytes, -1, -1, ASE);
    copy(vm, ints, 0, objects, 0, 0, ASE);
    copy(vm, objects, 0, ints, 0, 0, ASE);
    copy(vm, objects, 0, ints, 0, 1, ASE);
    copy(vm, ints, 0, objects, 0, 1, ASE);
    context = "zero length checks bounds, not actual reference elements";
    copy(vm, ints, 4, dst, 4, 0, NULL);
    copy(vm, objects, 4, strings, 4, 0, NULL);
    copy(vm, objects, 0, strings, 0, 0, NULL);
    copy(vm, strings, 0, nested, 0, 0, NULL);
    for (i = 0; i < 4; ++i) {
        check(nj_array_get_int(dst, (nj_u4)i) == (nj_s4)(20 + i), "invalid/zero calls leave primitive tail");
        check(nj_array_get_int(ints, (nj_u4)i) == (nj_s4)(10 + i), "type failures leave int destination intact");
        check(nj_array_get_int(bytes, (nj_u4)i) == 0, "type failures leave byte destination intact");
        check(nj_array_get_ref(strings, (nj_u4)i) == string, "invalid/zero calls leave reference tail");
        check(nj_array_get_ref(objects, (nj_u4)i) == object, "invalid/zero calls leave source references");
        check(nj_array_get_ref(nested, (nj_u4)i) == NULL, "zero incompatible reference types do not write");
    }
    nj_root_release(vm, mark);
}

static void nested_cases(nj_vm *vm, nj_ref string, int classless)
{
    nj_u4 mark = nj_root_mark(vm);
    nj_ref src = refs(vm, "java/lang/Object", 4);
    nj_ref dst = refs(vm, "[B", 4); /* byte[][] */
    nj_ref wide = refs(vm, "java/lang/Object", 4);
    nj_ref narrow = refs(vm, "java/lang/String", 4);
    nj_ref ba = classless ? keep(vm, nj_new_array(vm, NULL, 2, NJ_ARRAY_T_BYTE)) :
                            keep(vm, nj_byte_array_new(vm, 2));
    nj_ref ia = classless ? keep(vm, nj_new_array(vm, NULL, 2, NJ_ARRAY_T_INT)) :
                            keep(vm, nj_int_array_new(vm, 2));
    nj_ref ra = classless ? keep(vm, nj_new_array(vm, NULL, 2, NJ_ARRAY_T_REF)) :
                            refs(vm, "java/lang/Object", 2);
    nj_ref objects2 = refs(vm, "[Ljava/lang/Object;", 4); /* Object[][] */
    unsigned i;
    context = classless ? "nested classless native arrays" : "nested typed arrays";
    for (i = 0; i < 4; ++i) {
        nj_array_set_ref(dst, i, ba);
        nj_array_set_ref(narrow, i, string);
    }
    nj_array_set_ref(src, 0, ba); nj_array_set_ref(src, 1, NULL);
    nj_array_set_ref(src, 2, ia); nj_array_set_ref(src, 3, ra);
    copy(vm, src, 0, dst, 0, 4, ASE);
    check(nj_array_get_ref(dst, 0) == ba && nj_array_get_ref(dst, 1) == NULL &&
          nj_array_get_ref(dst, 2) == ba && nj_array_get_ref(dst, 3) == ba,
          "byte[][] accepts byte[]/null prefix, rejects int[], preserves failing slot and tail");
    copy(vm, src, 0, wide, 0, 4, NULL);
    for (i = 0; i < 4; ++i)
        check(nj_array_get_ref(wide, i) == nj_array_get_ref(src, i),
              "Object[] accepts primitive/reference array values without copying contents");
    copy(vm, src, 0, narrow, 0, 4, ASE);
    for (i = 0; i < 4; ++i)
        check(nj_array_get_ref(narrow, i) == string,
              "String[] rejects array value before any destination modification");
    copy(vm, src, 3, objects2, 1, 1, NULL);
    check(nj_array_get_ref(objects2, 0) == NULL && nj_array_get_ref(objects2, 1) == ra &&
          nj_array_get_ref(objects2, 2) == NULL && nj_array_get_ref(objects2, 3) == NULL,
          "Object[][] accepts an Object[] component");
    copy(vm, src, 0, objects2, 0, 1, ASE);
    check(nj_array_get_ref(objects2, 0) == NULL && nj_array_get_ref(objects2, 1) == ra,
          "Object[][] rejects a primitive array component without disturbing prior cells");
    nj_root_release(vm, mark);
}

int main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_u4 mark;
    nj_ref strings[6], values[6], sentinel, object;
    unsigned i;
    static const char *names[6] = { "A", "B", "C", "D", "E", "F" };
    memset(&port, 0, sizeof(port));
    fixture(nj_port_headless(&port) == NJ_OK, "headless port");
    vm = nj_vm_create(&port, 4u * 1024u * 1024u);
    fixture(vm != NULL, "VM creation");
    mark = nj_root_mark(vm);
    for (i = 0; i < 6; ++i) strings[i] = keep(vm, nj_string_new(vm, names[i]));
    sentinel = keep(vm, nj_string_new(vm, "sentinel"));
    object = keep(vm, nj_object_new(vm, "java/lang/Object"));
    primitive_cases(vm);
    /* Include a null component in all reference overlap snapshots. */
    memcpy(values, strings, sizeof(values)); values[2] = NULL;
    reference_cases(vm, values, sentinel, object);
    invalid_cases(vm, object, sentinel);
    nested_cases(vm, sentinel, 0);
    nested_cases(vm, sentinel, 1);
    context = "cleanup";
    check(!nj_has_exception(vm), "no pending Java exception after diagnostics");
    nj_root_release(vm, mark);
    check(nj_root_mark(vm) == mark, "all fixture roots released");
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    printf("Arraycopy: %u calls, %u checks, %u passed, %u failed\n",
           calls, checks, checks - failures, failures);
    return failures ? 1 : 0;
}
