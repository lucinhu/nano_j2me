/* Heap size arithmetic regressions on a real 64 KiB arena.
 *
 * No 4 GiB allocation or synthetic heap state is needed. nj_size is uint32_t
 * even on LP64. --raw exercises overflow requests that old code misallocates
 * as small blocks; it never writes through those unexpected results. --bump,
 * --null and --strings must each run in a separate child against old code:
 * the historical failures can crash before the fixture can report a failure.
 * Default runs all groups. --arrays is also available independently.
 *
 * Raw allocations have no Java header, so raw-only VMs are destroyed without
 * GC. The reuse case initializes a real header before its explicit collection;
 * its final raw result is never collected. All other GC survivors are rooted.
 */
#include "nj_internal.h"
#include "nj2me/nj.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define HEAP_BYTES (64u * 1024u)
static unsigned checks, failures;
static const char *group;
static uint64_t case_value;

#define CHECK(condition, message) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "FAIL heap %s case 0x%llx line %d: %s\n", \
                group, (unsigned long long)case_value, __LINE__, message); \
    } \
} while (0)

static nj_vm *small_vm(void)
{
    nj_vm *vm = nj_vm_create(NULL, HEAP_BYTES);
    if (!vm || vm->heap.size != HEAP_BYTES) {
        fprintf(stderr, "heap fixture: cannot create real 64 KiB VM\n");
        exit(2);
    }
    return vm;
}

static int bytes_are(const void *data, size_t count, unsigned char value)
{
    const unsigned char *p = (const unsigned char *)data;
    size_t i;
    for (i = 0; i < count; ++i) {
        if (p[i] != value) return 0;
    }
    return 1;
}

/* Only use the strict snapshot for sizes impossible to represent, not ordinary
 * out-of-arena OOM: a legitimate GC can fold the untouched tail into free lists.
 * Failed-allocation accounting is deliberately excluded (it may increment).
 */
typedef struct heap_state {
    nj_size bump, peak, used;
    nj_u4 live, objects, roots, collections;
} heap_state;

static heap_state state_of(nj_vm *vm)
{
    heap_state s;
    s.bump = vm->heap.bump;
    s.peak = vm->heap.peak;
    s.used = vm->heap.used;
    s.live = vm->live_objects;
    s.objects = vm->total_objects;
    s.roots = vm->nroots;
    s.collections = vm->heap.gc_count;
    return s;
}

static void unchanged(nj_vm *vm, heap_state s)
{
    CHECK(vm->heap.bump == s.bump && vm->heap.peak == s.peak &&
          vm->heap.used == s.used, "impossible size must not move arena bounds");
    CHECK(vm->live_objects == s.live && vm->total_objects == s.objects &&
          vm->nroots == s.roots, "impossible size must not change live objects/roots");
    CHECK(vm->heap.gc_count == s.collections, "impossible size needs no collection");
}

/* nj_root returns r even if host-side root-list growth fails. */
static nj_ref checked_root(nj_vm *vm, nj_ref r)
{
    nj_u4 before = vm->nroots;
    if (r && (nj_root(vm, r) != r || vm->nroots != before + 1u)) {
        fprintf(stderr, "heap fixture: cannot register root\n");
        exit(2);
    }
    return r;
}

static nj_ref sentinel(nj_vm *vm)
{
    nj_ref r = checked_root(vm, nj_new_array(vm, NULL, 17u, NJ_ARRAY_T_BYTE));
    if (!r) {
        fprintf(stderr, "heap fixture: cannot allocate sentinel\n");
        exit(2);
    }
    memset(r->u.bytes, 0x6d, 17u);
    return r;
}

static void sentinel_intact(nj_vm *vm, nj_ref r)
{
    CHECK(nj_heap_owns_object(vm, r) && r->kind == NJ_OBJ_ARRAY &&
          r->length == 17u && r->aflags == NJ_ARRAY_T_BYTE &&
          bytes_are(r->u.bytes, 17u, 0x6d), "rooted sentinel survives failure/GC");
}

static void raw_positive(void)
{
    static const nj_size sizes[] = {0u, 1u, 15u, 16u, 17u, 31u, 32u, 33u, 63u, 257u};
    void *p[COUNT(sizes)];
    nj_vm *vm = small_vm();
    size_t i, j;
    group = "raw-positive";
    for (i = 0; i < COUNT(sizes); ++i) {
        case_value = sizes[i];
        p[i] = nj_alloc(vm, sizes[i]);
        CHECK(p[i] != NULL, "ordinary raw allocation including zero succeeds");
        if (!p[i]) continue;
        CHECK(((uintptr_t)p[i] % NJ_OBJ_ALIGN) == 0u, "payload is object-aligned");
        CHECK(bytes_are(p[i], sizes[i], 0u), "requested payload starts zeroed");
        for (j = 0; j < i; ++j) {
            CHECK(p[i] != p[j], "live raw allocations are distinct");
        }
        memset(p[i], (int)(0x40u + i), sizes[i]);
    }
    for (i = 0; i < COUNT(sizes); ++i) {
        case_value = sizes[i];
        if (p[i]) CHECK(bytes_are(p[i], sizes[i], (unsigned char)(0x40u + i)),
                        "neighboring raw allocations preserve canaries");
    }
    /* No further allocation and no GC: these payloads are not Java objects. */
    nj_vm_destroy(vm);
}

static void raw_wrap(void)
{
    /* The independent 64-bit calculation identifies unrepresentable aligned
     * totals; loop includes every default-16 header/alignment neighbor.
     */
    uint64_t bytes;
    uint64_t first = (uint64_t)UINT32_MAX - NJ_BLOCK_HDR - (NJ_OBJ_ALIGN - 1u) + 1u;
    group = "raw-wrap";
    for (bytes = first; bytes <= UINT32_MAX; ++bytes) {
        nj_vm *vm = small_vm();
        unsigned char *canary = (unsigned char *)nj_alloc(vm, 17u);
        heap_state s;
        nj_u4 attempts;
        uint64_t total = bytes + NJ_BLOCK_HDR;
        void *p;
        case_value = bytes;
        if (!canary) { fprintf(stderr, "heap fixture: raw canary allocation\n"); exit(2); }
        memset(canary, 0xa7, 17u);
        total = ((total + NJ_OBJ_ALIGN - 1u) / NJ_OBJ_ALIGN) * NJ_OBJ_ALIGN;
        CHECK(total > UINT32_MAX, "raw test request really exceeds 32-bit block size");
        s = state_of(vm);
        attempts = vm->heap.allocs_since_gc;
        p = nj_alloc(vm, (nj_size)bytes);
        CHECK(p == NULL, "overflow request must not return an undersized block");
        CHECK(vm->heap.allocs_since_gc == attempts + 1u,
              "rejected raw request still counts as one allocation attempt");
        unchanged(vm, s);
        CHECK(bytes_are(canary, 17u, 0xa7), "rejected request preserves raw canary");
        /* Old code can return a tiny allocation. Never touch p or collect it. */
        nj_vm_destroy(vm);
    }
}

static void raw_reuse(void)
{
    nj_vm *vm = small_vm();
    nj_ref keep = sentinel(vm);
    const nj_size bytes = (nj_size)sizeof(nj_object) + 64u;
    nj_object *dead = (nj_object *)nj_alloc(vm, bytes);
    void *again;
    group = "raw-reuse";
    case_value = bytes;
    CHECK(dead != NULL, "ordinary reclaimable block allocated");
    if (!dead) { nj_vm_destroy(vm); return; }
    /* Valid unrooted Java header makes a GC walk safe; cls==NULL, no refs. */
    dead->kind = NJ_OBJ_PLAIN;
    dead->size = bytes;
    memset((nj_u1 *)dead + sizeof(*dead), 0xbc, 64u);
    CHECK(nj_gc_run(vm) == NJ_OK, "explicit collection succeeds");
    sentinel_intact(vm, keep);
    again = nj_alloc(vm, bytes);
    CHECK(again == (void *)dead, "free-list allocation reuses the dead block");
    if (again) CHECK(bytes_are(again, bytes, 0u), "reused payload fully cleared");
    sentinel_intact(vm, keep);
    /* again is raw: destroy without another GC. */
    nj_vm_destroy(vm);
}

static void null_vm(void)
{
    static const nj_size sizes[] = {0u, 1u, UINT32_MAX};
    size_t i;
    group = "null";
    for (i = 0; i < COUNT(sizes); ++i) {
        case_value = sizes[i];
        CHECK(nj_alloc(NULL, sizes[i]) == NULL, "NULL VM raw allocation fails safely");
    }
    CHECK(nj_new_string_utf16(NULL, NULL, INT_MAX) == NULL,
          "NULL VM UTF16 constructor fails safely");
}

static void bump_wrap(void)
{
    nj_vm *vm = small_vm();
    nj_ref keep = sentinel(vm);
    nj_u4 roots = vm->nroots, objects = vm->total_objects;
    /* This request's aligned total still fits u32. Only adding a real nonzero
     * bump overflows in old code, which can then attempt a huge memset.
     */
    nj_size bytes = UINT32_MAX - NJ_BLOCK_HDR - (NJ_OBJ_ALIGN - 1u);
    group = "bump";
    case_value = bytes;
    CHECK(vm->heap.bump > NJ_OBJ_ALIGN, "real sentinel gives nonzero bump");
    CHECK(nj_alloc(vm, bytes) == NULL, "representable huge block cannot fit small arena");
    CHECK(vm->nroots == roots && vm->total_objects == objects,
          "ordinary arena OOM preserves roots/object count");
    sentinel_intact(vm, keep);
    {
        nj_ref recovered = nj_new_array(vm, NULL, 11u, NJ_ARRAY_T_BYTE);
        CHECK(recovered != NULL, "ordinary allocation recovers after huge OOM");
        if (recovered) CHECK(bytes_are(recovered->u.bytes, 11u, 0u), "recovery array is zeroed");
    }
    sentinel_intact(vm, keep);
    nj_vm_destroy(vm);
}

static void arrays(void)
{
    static const struct { int kind; nj_u4 elem; } types[] = {
        {NJ_ARRAY_T_BOOLEAN, 1u}, {NJ_ARRAY_T_BYTE, 1u},
        {NJ_ARRAY_T_CHAR, 2u}, {NJ_ARRAY_T_SHORT, 2u},
        {NJ_ARRAY_T_INT, 4u}, {NJ_ARRAY_T_LONG, 8u},
        {NJ_ARRAY_T_FLOAT, 4u}, {NJ_ARRAY_T_DOUBLE, 8u},
        {NJ_ARRAY_T_REF, (nj_u4)sizeof(nj_ref)}
    };
    nj_vm *vm = small_vm();
    nj_ref keep = sentinel(vm), a[COUNT(types)];
    size_t i, j;
    group = "arrays";
    for (i = 0; i < COUNT(types); ++i) {
        nj_ref empty;
        nj_u4 lengths[4];
        case_value = (uint64_t)types[i].kind;
        empty = nj_new_array(vm, NULL, 0u, types[i].kind);
        CHECK(empty != NULL, "all nine element kinds allow zero length");
        if (empty) {
            CHECK(checked_root(vm, empty) == empty, "empty array rooted");
            CHECK(empty->kind == NJ_OBJ_ARRAY && empty->aflags == types[i].kind &&
                  empty->length == 0u && empty->size == sizeof(nj_object),
                  "zero array metadata is exact");
        }
        a[i] = nj_new_array(vm, NULL, 3u, types[i].kind);
        CHECK(a[i] != NULL, "all nine small arrays succeed");
        if (!a[i]) continue;
        CHECK(checked_root(vm, a[i]) == a[i], "small array rooted");
        CHECK(a[i]->length == 3u && a[i]->aflags == types[i].kind &&
              a[i]->size == sizeof(nj_object) + 3u * types[i].elem,
              "small array size and kind are exact");
        CHECK(bytes_are(a[i]->u.bytes, 3u * types[i].elem, 0u),
              "all nine array payloads start zeroed");
        if (types[i].kind == NJ_ARRAY_T_REF) {
            for (j = 0; j < 3u; ++j) a[i]->u.refs[j] = keep;
        } else {
            memset(a[i]->u.bytes, 0x90 + types[i].kind, 3u * types[i].elem);
        }
        lengths[0] = UINT32_MAX;
        lengths[1] = UINT32_MAX / types[i].elem;
        lengths[2] = UINT32_MAX / types[i].elem - 1u;
        lengths[3] = HEAP_BYTES / types[i].elem + 1u; /* ordinary arena OOM */
        for (j = 0; j < COUNT(lengths); ++j) {
            heap_state s = state_of(vm);
            uint64_t requested = sizeof(nj_object) + (uint64_t)lengths[j] * types[i].elem;
            case_value = ((uint64_t)types[i].kind << 32) | lengths[j];
            CHECK(nj_new_array(vm, NULL, lengths[j], types[i].kind) == NULL,
                  "huge array fails instead of creating a truncated object");
            if (requested > UINT32_MAX) unchanged(vm, s);
            CHECK(vm->nroots == s.roots && vm->total_objects == s.objects,
                  "failed array keeps roots and object count");
            sentinel_intact(vm, keep);
        }
    }
    CHECK(nj_gc_run(vm) == NJ_OK, "array recovery collection succeeds");
    for (i = 0; i < COUNT(types); ++i) {
        case_value = (uint64_t)types[i].kind;
        if (!a[i]) continue;
        CHECK(nj_heap_owns_object(vm, a[i]), "small array remains live after OOM and GC");
        if (types[i].kind == NJ_ARRAY_T_REF) {
            CHECK(a[i]->u.refs[0] == keep && a[i]->u.refs[1] == keep &&
                  a[i]->u.refs[2] == keep, "reference payload preserved");
        } else {
            CHECK(bytes_are(a[i]->u.bytes, 3u * types[i].elem,
                            (unsigned char)(0x90 + types[i].kind)), "primitive payload preserved");
        }
    }
    CHECK(nj_new_array(vm, NULL, 7u, NJ_ARRAY_T_INT) != NULL,
          "ordinary array allocation recovers after huge requests");
    sentinel_intact(vm, keep);
    nj_vm_destroy(vm);
}

static void strings(void)
{
    static const nj_u2 text[] = {0x0041u, 0x0000u, 0xd834u, 0xdd1eu};
    static const int small_lengths[] = {INT_MIN, -1, 0, 1, 4};
    nj_vm *vm = small_vm();
    nj_ref keep = sentinel(vm), copy, zeros;
    size_t i;
    int boundary = (int)(((uint64_t)UINT32_MAX - sizeof(nj_object)) / 2u - 1u);
    int huge[] = {INT_MAX, INT_MAX - 1, INT_MAX - 2, INT_MAX - 15,
                  INT_MAX - 16, INT_MAX - 32, boundary - 1, boundary, boundary + 1};
    group = "strings";
    for (i = 0; i < COUNT(small_lengths); ++i) {
        int len = small_lengths[i] < 0 ? 0 : small_lengths[i];
        nj_ref r;
        case_value = (uint64_t)(int64_t)small_lengths[i];
        r = nj_new_string_utf16(vm, text, small_lengths[i]);
        CHECK(r != NULL, "negative/zero/small UTF16 length succeeds");
        if (!r) continue;
        CHECK(checked_root(vm, r) == r, "small string rooted");
        CHECK(r->kind == NJ_OBJ_STRING && r->length == (nj_u4)len &&
              r->size == sizeof(nj_object) + ((nj_u4)len + 1u) * 2u,
              "UTF16 length and allocation include terminator");
        CHECK(memcmp(r->u.bytes, text, (size_t)len * 2u) == 0 &&
              ((nj_u2 *)r->u.bytes)[len] == 0u, "UTF16 units copied exactly and terminated");
    }
    copy = checked_root(vm, nj_new_string_utf16(vm, text, 4));
    zeros = checked_root(vm, nj_new_string_utf16(vm, NULL, 4));
    CHECK(copy != NULL && zeros != NULL, "normal copy and NULL-source strings succeed");
    if (!copy || !zeros) { nj_vm_destroy(vm); return; }
    CHECK(bytes_are(zeros->u.bytes, 10u, 0u), "NULL source produces zero code units and terminator");
    for (i = 0; i < COUNT(huge); ++i) {
        heap_state s = state_of(vm);
        uint64_t requested = sizeof(nj_object) + ((uint64_t)huge[i] + 1u) * 2u;
        case_value = (uint64_t)huge[i];
        CHECK(nj_new_string_utf16(vm, NULL, huge[i]) == NULL,
              "huge UTF16 length fails before any distant terminator write");
        if (requested > UINT32_MAX) unchanged(vm, s);
        CHECK(vm->nroots == s.roots && vm->total_objects == s.objects,
              "failed UTF16 constructor preserves roots and object count");
        sentinel_intact(vm, keep);
    }
    CHECK(nj_gc_run(vm) == NJ_OK, "string recovery collection succeeds");
    CHECK(nj_heap_owns_object(vm, copy) && copy->length == 4u &&
          memcmp(copy->u.bytes, text, sizeof(text)) == 0 &&
          ((nj_u2 *)copy->u.bytes)[4] == 0u, "embedded NUL/surrogate units survive GC");
    CHECK(nj_heap_owns_object(vm, zeros) && bytes_are(zeros->u.bytes, 10u, 0u),
          "NULL-source string survives GC");
    CHECK(nj_new_string_utf16(vm, text, 4) != NULL, "ordinary UTF16 allocation recovers");
    sentinel_intact(vm, keep);
    nj_vm_destroy(vm);
}

int main(int argc, char **argv)
{
    const char *mode = argc == 2 ? argv[1] : "all";
    int all = argc == 1;
    if (argc > 2 || (!all && strcmp(mode, "--raw") && strcmp(mode, "--null") &&
        strcmp(mode, "--bump") && strcmp(mode, "--arrays") && strcmp(mode, "--strings"))) {
        fprintf(stderr, "usage: %s [--raw|--null|--bump|--arrays|--strings]\n", argv[0]);
        return 2;
    }
    if (all || !strcmp(mode, "--raw")) { raw_positive(); raw_wrap(); raw_reuse(); }
    if (all || !strcmp(mode, "--arrays")) arrays();
    if (all || !strcmp(mode, "--null")) null_vm();
    if (all || !strcmp(mode, "--bump")) bump_wrap();
    if (all || !strcmp(mode, "--strings")) strings();
    printf("heap layout: nj_size=%u object=%u block_header=%u align=%u\n",
           (unsigned)sizeof(nj_size), (unsigned)sizeof(nj_object),
           (unsigned)NJ_BLOCK_HDR, (unsigned)NJ_OBJ_ALIGN);
    printf("heap bounds %s: %u checks, %u failures\n", mode, checks, failures);
    return failures ? 1 : 0;
}
