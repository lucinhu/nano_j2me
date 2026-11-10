/* Public Loader regression for SkinnedMesh transform-reference records.
 * All M3G files are synthesized here, with standard Header-inclusive indices
 * and valid section checksums; no game assets or private decoder calls.
 * Records repeat one bone over disjoint vertices (one influence per vertex).
 * 64/65/482 are record counts, not distinct bones or a capabilities claim.
 */
#include "nj2me/nj.h"
#include "nj2me/nj_config.h"
#include "nj2me/nj_vm.h"
#include "nj_internal.h"  /* C fixture roots only; assertions use public M3G APIs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if NJ_ENABLE_M3G
#define C(x) "javax/microedition/m3g/" x
#define SKIN_ID 7007
#define SKEL_ID 6006
#define BONE_ID 5005
#define WORLD_ID 8008
static int checks, failures;
static char context[96];
static void check(int ok, const char *why)
{
    ++checks;
    if (!ok) { ++failures; fprintf(stderr, "FAIL [%s]: %s\n", context, why); }
}
static void die(const char *why)
{ fprintf(stderr, "fixture error [%s]: %s\n", context, why); exit(2); }
static nj_ref root(nj_vm *vm, nj_ref r)
{
    nj_u4 mark = nj_root_mark(vm);
    if (!r) die("allocation");
    nj_root(vm, r);
    if (nj_root_mark(vm) != mark + 1u) die("root reservation");
    return r;
}
static void call(nj_vm *vm, nj_ref r, const char *cls, const char *name,
                 const char *desc, nj_value *a, nj_value *out)
{
    if (nj_call(vm, r, cls, name, desc, a, out) != NJ_OK || nj_has_exception(vm)) {
        fprintf(stderr, "%s.%s%s: %s\n", cls, name, desc, nj_exception_describe(vm));
        die("unexpected API exception");
    }
}
static nj_ref get(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                  const char *desc, nj_value *a)
{
    nj_value r; memset(&r, 0, sizeof r); call(vm, o, cls, name, desc, a, &r); return r.r;
}
static nj_s4 integer(nj_vm *vm, nj_ref o, const char *cls, const char *name)
{
    nj_value r; r.i = -1; call(vm, o, cls, name, "()I", NULL, &r); return r.i;
}
static nj_ref find(nj_vm *vm, nj_ref o, int id)
{
    nj_value a; a.i = id;
    return get(vm, o, C("Object3D"), "find", "(I)L" C("Object3D") ";", &a);
}
static void u16(nj_u1 *p, nj_u4 n)
{ p[0] = (nj_u1)n; p[1] = (nj_u1)(n >> 8); }
static void u32(nj_u1 *p, nj_u4 n)
{ p[0] = (nj_u1)n; p[1] = (nj_u1)(n >> 8); p[2] = (nj_u1)(n >> 16); p[3] = (nj_u1)(n >> 24); }
static void f32(nj_u1 *p, float x)
{ nj_u4 bits; memcpy(&bits, &x, sizeof bits); u32(p, bits); }
static nj_u4 adler(const nj_u1 *p, nj_u4 n)
{
    nj_u4 a = 1u, b = 0u, i;
    for (i = 0u; i < n; ++i) { a = (a + p[i]) % 65521u; b = (b + a) % 65521u; }
    return (b << 16) | a;
}
/* Transformable with optional component transform, then Node. No alignment. */
static void node(nj_u1 *p, int transform)
{
    nj_u4 a = 12u;
    p[a++] = (nj_u1)transform;
    if (transform) {
        /* translation 0, unit scale, zero-angle rotation around Z */
        f32(p + a + 12u, 1.0f); f32(p + a + 16u, 1.0f);
        f32(p + a + 20u, 1.0f); f32(p + a + 36u, 1.0f); a += 40u;
    }
    p[a++] = 0u; p[a++] = 1u; p[a++] = 1u; p[a++] = 255u;
    u32(p + a, 0xffffffffu); a += 4u; p[a] = 0u;
}
enum fault { VALID, NO_TAIL, SHORT_RECORD, SHORT_TAIL, TRAILING_BYTE,
             COUNT_TOO_LARGE, COUNT_TOO_SMALL, COUNT_WRAP, COUNT_MAX,
             SHORT_SUBMESH, BAD_VERTEX_RANGE, ZERO_VERTEX_COUNT, BAD_WEIGHT };
typedef struct fixture { nj_u1 *bytes; nj_u4 size; } fixture;
/* IDs 2..9: positions, VB, IB, Appearance, bone, skeleton, skin, World.
 * The only normative root of a valid file is World. A malformed optional skin
 * tail retains the established bind-pose fallback, leaving skeleton a root. */
static fixture build(nj_u4 records, enum fault fault, int variable_header, int transform)
{
    static const nj_u1 magic[12] = {0xab,0x4a,0x53,0x52,0x31,0x38,0x34,0xbb,13,10,26,10};
    static const nj_u1 types[8] = {20,21,11,3,9,9,16,22};
    nj_u4 nv = records < 3u ? 3u : records, delta = variable_header ? 9u : 0u;
    nj_u4 base = transform ? 78u : 38u, tail = 8u + 16u * records;
    nj_u4 lens[8], body = 0u, i, total, section;
    nj_u1 *b, *p; fixture result;
    if (fault == NO_TAIL || fault == SHORT_SUBMESH) tail = 0u;
    if (fault == SHORT_RECORD) --tail;
    if (fault == SHORT_TAIL) tail = 7u;
    if (fault == TRAILING_BYTE) ++tail;
    lens[0] = 17u + 6u * nv; lens[1] = 48u; lens[2] = 25u; lens[3] = 33u;
    lens[4] = 26u; lens[5] = 30u; lens[6] = base + delta + tail; lens[7] = 38u;
    if (fault == SHORT_SUBMESH) lens[6] -= 1u;
    for (i = 0u; i < 8u; ++i) body += 5u + lens[i];
    section = 13u + body; total = 42u + section;
    b = (nj_u1 *)calloc(total, 1u); if (!b) die("host allocation");
    memcpy(b, magic, 12u);
    b[12] = 0u; u32(b + 13u, 30u); u32(b + 17u, 17u);
    b[21] = 0u; u32(b + 22u, 12u); b[26] = 1u;
    u32(b + 29u, total); u32(b + 33u, total);
    u32(b + 38u, adler(b + 12u, 26u));
    b[42] = 0u; u32(b + 43u, section); u32(b + 47u, body); p = b + 51u;
    for (i = 0u; i < 8u; ++i) {
        nj_u1 *q; p[0] = types[i]; u32(p + 1u, lens[i]); p += 5u; q = p;
        u32(p, 1001u * (i + 1u));
        switch (i) {
        case 0: /* signed-short xyz; vertex positions are irrelevant to getters */
            p[12] = 2u; p[13] = 3u; p[14] = 0u; u16(p + 15u, nv); break;
        case 1:
            u32(p + 12u, 0xffffffffu); u32(p + 16u, 2u); f32(p + 32u, 1.0f); break;
        case 2: /* implicit indices 0,1,2, one length-three strip */
            p[12] = 0u; u32(p + 13u, 0u); u32(p + 17u, 1u); u32(p + 21u, 3u); break;
        case 3: break;
        case 4: node(p, 0); break;
        case 5: node(p, 0); u32(p + 22u, 1u); u32(p + 26u, 6u); break;
        case 6: {
            nj_u4 r, used = tail;
            if (variable_header) {
                u32(p + 8u, 1u); u32(p + 12u, 91u); u32(p + 16u, 1u); p[20] = 42u;
                q += delta;
            }
            node(q, transform);
            u32(q + base - 16u, 3u); u32(q + base - 12u, 1u);
            if (fault == SHORT_SUBMESH) {
                /* Claim two pairs where even the first Appearance is short. */
                u32(q + base - 12u, 2u); u32(q + base - 8u, 4u);
                break;
            }
            u32(q + base - 8u, 4u); u32(q + base - 4u, 5u);
            if (used >= 4u) u32(q + base, 7u);
            if (used >= 8u) {
                nj_u4 count = records;
                if (fault == COUNT_TOO_LARGE) ++count;
                if (fault == COUNT_TOO_SMALL) --count;
                if (fault == COUNT_WRAP) count = 0x10000000u + records;
                if (fault == COUNT_MAX) count = 0xffffffffu;
                u32(q + base + 4u, count);
            }
            for (r = 0u; r < records && used >= 8u + 16u * (r + 1u); ++r) {
                nj_u1 *rec = q + base + 8u + 16u * r;
                u32(rec, 6u); u32(rec + 4u, r); u32(rec + 8u, 1u); u32(rec + 12u, 255u);
                if (r == records - 1u) {
                    if (fault == BAD_VERTEX_RANGE) { u32(rec + 4u, 0x7fffffffu); u32(rec + 8u, 2u); }
                    if (fault == ZERO_VERTEX_COUNT) u32(rec + 8u, 0u);
                    if (fault == BAD_WEIGHT) u32(rec + 12u, 0u);
                }
            }
            break;
        }
        case 7: node(p, 0); u32(p + 22u, 1u); u32(p + 26u, 8u); break;
        }
        p += lens[i];
    }
    u32(p, adler(b + 42u, section - 4u));
    result.bytes = b; result.size = total; return result;
}
static nj_ref load(nj_vm *vm, fixture f)
{
    nj_value a[2], r;
    nj_ref bytes = root(vm, nj_byte_array_new(vm, f.size));
    memcpy(nj_array_data(bytes), f.bytes, f.size); a[0].r = bytes; a[1].i = 0;
    call(vm, 0, C("Loader"), "load", "([BI)[L" C("Object3D") ";", a, &r);
    check(r.r != 0, "Loader returns a root array"); return r.r ? root(vm, r.r) : 0;
}
static nj_ref child(nj_vm *vm, nj_ref group)
{
    nj_value a; a.i = 0;
    check(integer(vm, group, C("Group"), "getChildCount") == 1, "Group has one child");
    return get(vm, group, C("Group"), "getChild", "(I)L" C("Node") ";", &a);
}
static void vertices(nj_vm *vm, nj_ref skin, nj_ref bone, nj_u4 expected)
{
    nj_value a[3], r; nj_u4 k;
    nj_ref indices = root(vm, nj_array_new(vm, "I", expected + 1u));
    nj_ref weights = root(vm, nj_array_new(vm, "F", expected + 1u));
    nj_s4 *iv = (nj_s4 *)nj_array_data(indices); float *wv = (float *)nj_array_data(weights);
    a[0].r = bone; a[1].r = 0; a[2].r = 0;
    call(vm, skin, C("SkinnedMesh"), "getBoneVertices", "(L" C("Node") ";[I[F)I", a, &r);
    check(r.i == (nj_s4)expected, "count-only sees every disjoint record");
    for (k = 0u; k <= expected; ++k) { iv[k] = -77; wv[k] = -77.0f; }
    a[1].r = indices; a[2].r = weights;
    call(vm, skin, C("SkinnedMesh"), "getBoneVertices", "(L" C("Node") ";[I[F)I", a, &r);
    check(r.i == (nj_s4)expected, "fill reports the full record count");
    for (k = 0u; k < expected; ++k) {
        check(iv[k] == (nj_s4)k, "disjoint record vertex index survives Loader");
        check(wv[k] == 1.0f, "one-influence vertex has normalized weight one");
    }
    check(iv[expected] == -77 && wv[expected] == -77.0f, "getBoneVertices preserves output tails");
}
static void range_api(nj_vm *vm, nj_ref skin, nj_ref bone)
{
    static const struct { nj_s4 first, count, weight; const char *exception; } bad[] = {
        {65535, 1, 1, "java/lang/IndexOutOfBoundsException"},
        {0x7fffffff, 2, 1, "java/lang/IndexOutOfBoundsException"},
        {1, 0x7fffffff, 1, "java/lang/IndexOutOfBoundsException"},
        {-1, 1, 1, "java/lang/IndexOutOfBoundsException"},
        {0, 0, 1, "java/lang/IllegalArgumentException"},
        {0, 1, 0, "java/lang/IllegalArgumentException"}
    };
    nj_value a[4], r; nj_u4 i; const char *desc = "(L" C("Node") ";III)V";
    a[0].r = bone; a[1].i = 1; a[2].i = 65534; a[3].i = 1;
    call(vm, skin, C("SkinnedMesh"), "addTransform", desc, a, NULL);
    a[0].r = bone; a[1].r = 0; a[2].r = 0;
    call(vm, skin, C("SkinnedMesh"), "getBoneVertices", "(L" C("Node") ";[I[F)I", a, &r);
    check(r.i == 2, "65534+1 is valid independently of current VertexBuffer length");
    for (i = 0u; i < (nj_u4)(sizeof bad / sizeof bad[0]); ++i) {
        nj_status st; nj_ref exc; const char *name;
        a[0].r = bone; a[1].i = bad[i].weight; a[2].i = bad[i].first; a[3].i = bad[i].count;
        st = nj_call(vm, skin, C("SkinnedMesh"), "addTransform", desc, a, NULL);
        exc = nj_take_exception(vm); name = exc ? nj_ref_class_name(exc) : NULL;
        check(st != NJ_OK && name && strcmp(name, bad[i].exception) == 0,
              "invalid range/count/weight throws the specified exception");
        /* If old code accepted an unsafe range, do not exercise its getters.
         * Keep old-code sensitivity diagnostic instead of invoking signed UB. */
        if (st == NJ_OK) return;
        a[0].r = bone; a[1].r = 0; a[2].r = 0;
        call(vm, skin, C("SkinnedMesh"), "getBoneVertices", "(L" C("Node") ";[I[F)I", a, &r);
        check(r.i == 2, "rejected addTransform leaves the roster unchanged");
    }
}
static void run_case(nj_u4 records, enum fault fault, int variable_header, int transform)
{
    nj_port port; nj_vm *vm; fixture f; nj_ref roots, world, skin, skeleton, bone; nj_u4 nroot, i;
    int bind_only = fault >= NO_TAIL && fault <= COUNT_MAX;
    snprintf(context, sizeof context, "records=%u fault=%d header=%d transform=%d",
             (unsigned)records, (int)fault, variable_header, transform);
    nj_port_headless_ex(&port, 32, 32, 0, NULL); vm = nj_vm_create(&port, 8u * 1024u * 1024u);
    if (!vm || nj_lib_late_init(vm) != NJ_OK) die("VM init");
    f = build(records, fault, variable_header, transform); roots = load(vm, f); free(f.bytes);
    if (!roots) goto done;
    nroot = nj_array_length(roots); world = 0;
    for (i = 0u; i < nroot; ++i) {
        nj_ref o = nj_array_get_ref(roots, i);
        if (integer(vm, o, C("Object3D"), "getUserID") == WORLD_ID) world = o;
    }
    check(world != 0, "authored World is returned"); if (!world) goto done;
    skin = child(vm, world); check(skin && integer(vm, skin, C("Object3D"), "getUserID") == SKIN_ID,
                                  "World child is the authored skin");
    if (!skin) goto done;
    check(find(vm, world, SKIN_ID) == skin, "public find resolves the skin");
    if (fault == SHORT_SUBMESH) goto done; /* Loader/World/skin checks above are the safety control. */
    check(get(vm, skin, C("Mesh"), "getVertexBuffer", "()L" C("VertexBuffer") ";", NULL) != 0,
          "bind-pose VertexBuffer is retained");
    skeleton = get(vm, skin, C("SkinnedMesh"), "getSkeleton", "()L" C("Group") ";", NULL);
    if (bind_only) {
        check(skeleton == 0, "malformed/absent optional tail keeps bind-pose compatibility");
        check(nroot == 2u, "unreferenced skeleton remains a separate root for malformed tail");
        goto done;
    }
    check(nroot == 1u, "valid skin skeleton is not a spurious Loader root");
    check(skeleton != 0, "valid tail retains its skeleton even for zero records");
    if (!skeleton) goto done;
    check(integer(vm, skeleton, C("Object3D"), "getUserID") == SKEL_ID, "skeleton reference names correct object");
    check(get(vm, skeleton, C("Node"), "getParent", "()L" C("Node") ";", NULL) == skin,
          "skeleton parent is the skin");
    bone = child(vm, skeleton); check(bone && integer(vm, bone, C("Object3D"), "getUserID") == BONE_ID,
                                      "bone child reference survives");
    check(find(vm, nj_array_get_ref(roots, nroot - 1u), SKIN_ID) == skin,
          "find on the last root sees the skin without ordering assumptions");
    if (bone) {
        vertices(vm, skin, bone, records - (fault >= BAD_VERTEX_RANGE ? 1u : 0u));
        if (records == 1u && fault == VALID) range_api(vm, skin, bone);
    }
done:
    nj_vm_destroy(vm); nj_port_shutdown(&port);
}
#endif
int main(void)
{
#if NJ_ENABLE_M3G
    static const nj_u4 counts[] = {0u, 1u, 64u, 65u, 482u}; nj_u4 i;
    for (i = 0u; i < (nj_u4)(sizeof counts / sizeof counts[0]); ++i) run_case(counts[i], VALID, 0, 0);
    run_case(482u, VALID, 1, 1);
    for (i = NO_TAIL; i <= COUNT_MAX; ++i) run_case(65u, (enum fault)i, 0, 0);
    run_case(65u, SHORT_SUBMESH, 0, 0);
    run_case(65u, BAD_VERTEX_RANGE, 0, 0);
    run_case(65u, ZERO_VERTEX_COUNT, 0, 0);
    run_case(65u, BAD_WEIGHT, 0, 0);
    printf("%d checks, %d failures\n", checks, failures); return failures ? 1 : 0;
#else
    puts("SKIP: NJ_ENABLE_M3G=0"); return 0;
#endif
}
