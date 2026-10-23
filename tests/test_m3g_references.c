/*
 * Object3D.getReferences regression tests, with synthetic public-API fixtures.
 *
 * JSR 184 1.1 Object3D.getReferences is the authority for direct references,
 * null omission, duplicate preservation, count/fill, and short-array IAE:
 * https://jcp.org/aboutJava/communityprocess/mrel/jsr184/index.html
 * "Duplicate references are explicitly not eliminated"; order is unspecified.
 * In particular, Mesh's private first-IndexBuffer alias is not another API
 * slot, while two submeshes sharing an IndexBuffer/Appearance ARE two slots.
 *
 * Local FreeJ2ME-Plus Mesh confirms the appearance edges and replacement,
 * but Object3D.addReference de-duplicates, contrary to the official contract.
 * Do not use its unique-object counts for the shared-slot expectations here.
 * Unchanged oversized tails and no partial write on IAE are additional local
 * reference/safety expectations; the Javadoc explicitly requires only IAE.
 *
 * BioCtrl.setTexture in Resident Evil uses the count/allocate/fill recursion
 * reproduced below. No game assets, loader, renderer, or saved data are used.
 */
#include "nj2me/nj.h"
#include "nj2me/nj_config.h"
#include "nj2me/nj_vm.h"
#include "nj_internal.h" /* temporary GC roots and guarded-array length */

#include <stdio.h>
#include <string.h>

#define C(x) "javax/microedition/m3g/" x
#define REFS_DESC "([L" C("Object3D") ";)I"
#define MESH_DESC "(L" C("VertexBuffer") ";L" C("IndexBuffer") ";L" C("Appearance") ";)V"
#define MESH_ARRAY_DESC "(L" C("VertexBuffer") ";[L" C("IndexBuffer") ";[L" C("Appearance") ";)V"

static int checks;
static int failures;

static void check(int ok, const char *what)
{
    ++checks;
    if (!ok) {
        ++failures;
        printf("  FAIL %s\n", what);
    }
}

static nj_ref object(nj_vm *vm, const char *cls)
{
    nj_ref result = nj_root(vm, nj_object_new(vm, cls));
    check(result != 0, "fixture object allocation");
    return result;
}

static int call_ok(nj_vm *vm, nj_ref self, const char *cls, const char *name,
                   const char *desc, nj_value *args, nj_value *ret)
{
    nj_status st = nj_call(vm, self, cls, name, desc, args, ret);
    if (st != NJ_OK || nj_has_exception(vm)) {
        const char *message = nj_exception_describe(vm);
        fprintf(stderr, "call failed: %s.%s%s status=%d exception=%s\n",
                cls, name, desc, (int)st, message ? message : "none");
        nj_clear_exception(vm);
        check(0, "fixture/API call succeeds");
        return 0;
    }
    return 1;
}

static nj_ref make(nj_vm *vm, const char *cls)
{
    nj_ref result = object(vm, cls);
    return result && call_ok(vm, result, cls, "<init>", "()V", NULL, NULL)
               ? result : 0;
}

static nj_ref ref_array(nj_vm *vm, const char *cls, int length)
{
    nj_ref result = nj_root(vm, nj_object_array_new(vm, cls, (nj_u4)length));
    check(result != 0, "fixture reference-array allocation");
    return result;
}

static nj_ref strip(nj_vm *vm)
{
    nj_ref result = object(vm, C("TriangleStripArray"));
    nj_ref lengths = nj_root(vm, nj_int_array_new(vm, 1u));
    nj_value a[2];
    check(lengths != 0, "triangle-strip lengths allocate");
    if (!result || !lengths) return 0;
    nj_array_set_int(lengths, 0u, 3);
    a[0].i = 0; a[1].r = lengths;
    return call_ok(vm, result, C("TriangleStripArray"), "<init>", "(I[I)V", a, NULL)
               ? result : 0;
}

static nj_ref mesh(nj_vm *vm, nj_ref vb, nj_ref ib, nj_ref app)
{
    nj_ref result = object(vm, C("Mesh"));
    nj_value a[3];
    a[0].r = vb; a[1].r = ib; a[2].r = app;
    return result && call_ok(vm, result, C("Mesh"), "<init>", MESH_DESC, a, NULL)
               ? result : 0;
}

static int occurrences(nj_ref arr, int count, nj_ref wanted)
{
    int i, found = 0;
    for (i = 0; i < count; ++i) {
        found += nj_array_get_ref(arr, (nj_u4)i) == wanted;
    }
    return found;
}

/* Compare identities as a multiset. No ordering or internal field is tested. */
static void expect_refs(nj_vm *vm, nj_ref self, const nj_ref *want, int expected,
                        nj_ref sentinel, const char *label)
{
    nj_u4 mark = nj_root_mark(vm);
    nj_ref arr;
    nj_value a, count, filled;
    int i, j, n, valid;
    a.r = 0; count.i = -1;
    if (!call_ok(vm, self, C("Object3D"), "getReferences", REFS_DESC, &a, &count))
        goto done;
    printf("%s: count=%d expected=%d\n", label, (int)count.i, expected);
    check(count.i == expected, "reference count equals direct slot count");
    arr = ref_array(vm, C("Object3D"), expected + 3);
    if (!arr) goto done;
    for (i = 0; i < expected + 3; ++i) nj_array_set_ref(arr, (nj_u4)i, sentinel);
    a.r = arr; filled.i = -1;
    if (!call_ok(vm, self, C("Object3D"), "getReferences", REFS_DESC, &a, &filled))
        goto done;
    check(filled.i == count.i, "count and fill return the same value");
    n = filled.i;
    valid = n >= 0 && n <= expected + 3;
    check(valid, "fill count fits destination");
    if (!valid) goto done;
    for (i = 0; i < n; ++i) {
        nj_ref item = nj_array_get_ref(arr, (nj_u4)i);
        int member = 0;
        for (j = 0; j < expected; ++j) member |= item == want[j];
        check(item != 0 && member, "filled reference is a non-null expected identity");
    }
    for (i = 0; i < expected; ++i) {
        int first = 1, needed = 0;
        for (j = 0; j < i; ++j) if (want[j] == want[i]) first = 0;
        if (!first) continue;
        for (j = 0; j < expected; ++j) needed += want[j] == want[i];
        check(occurrences(arr, n, want[i]) == needed,
              "each identity occurs once per direct reference slot");
    }
    valid = 1;
    for (i = n; i < expected + 3; ++i)
        valid &= nj_array_get_ref(arr, (nj_u4)i) == sentinel;
    check(valid, "oversized destination tail stays unchanged");
    /* A failing old implementation may have copied invalid stack pointers.
       Erase them before any later allocation could trigger a GC. */
    for (i = 0; i < expected + 3; ++i) nj_array_set_ref(arr, (nj_u4)i, 0);
done:
    nj_root_release(vm, mark);
}

static void expect_short(nj_vm *vm, nj_ref self, int expected, nj_ref sentinel)
{
    nj_u4 mark = nj_root_mark(vm);
    nj_ref arr = ref_array(vm, C("Object3D"), expected + 3);
    nj_ref exc;
    nj_value a, r;
    nj_status st;
    int i, unchanged = 1, guarded = 1;
    if (!arr) goto done;
    for (i = 0; i < expected + 3; ++i) nj_array_set_ref(arr, (nj_u4)i, sentinel);
    /* Give the Java array a short logical length while retaining four hidden
       allocated slots as canaries. This white-box harness makes a write past
       Java length observable without performing a C out-of-bounds access. */
    arr->length = (nj_u4)(expected - 1);
    a.r = arr; r.i = -1;
    st = nj_call(vm, self, C("Object3D"), "getReferences", REFS_DESC, &a, &r);
    arr->length = (nj_u4)(expected + 3);
    exc = nj_take_exception(vm);
    check(st != NJ_OK && exc &&
          strcmp(nj_ref_class_name(exc), "java/lang/IllegalArgumentException") == 0,
          "too-small destination throws IllegalArgumentException");
    for (i = 0; i < expected - 1; ++i)
        unchanged &= nj_array_get_ref(arr, (nj_u4)i) == sentinel;
    for (i = expected - 1; i < expected + 3; ++i)
        guarded &= nj_array_get_ref(arr, (nj_u4)i) == sentinel;
    check(unchanged, "too-small destination has no partial writes");
    check(guarded, "too-small destination does not write beyond Java length");
    for (i = 0; i < expected + 3; ++i) nj_array_set_ref(arr, (nj_u4)i, 0);
done:
    nj_root_release(vm, mark);
}

static int set_appearance(nj_vm *vm, nj_ref target, int index, nj_ref app)
{
    nj_value a[2];
    a[0].i = index; a[1].r = app;
    return call_ok(vm, target, C("Mesh"), "setAppearance",
                   "(IL" C("Appearance") ";)V", a, NULL);
}

static void expect_appearance(nj_vm *vm, nj_ref target, int index, nj_ref app)
{
    nj_value a, r;
    a.i = index; r.r = 0;
    if (call_ok(vm, target, C("Mesh"), "getAppearance", "(I)L" C("Appearance") ";", &a, &r))
        check(r.r == app, "getAppearance returns the exact attached identity");
}

static nj_ref texture(nj_vm *vm)
{
    nj_ref image = object(vm, C("Image2D"));
    nj_ref result = object(vm, C("Texture2D"));
    nj_value a[3];
    if (!image || !result) return 0;
    a[0].i = 100; a[1].i = 1; a[2].i = 1; /* RGB, one pixel */
    if (!call_ok(vm, image, C("Image2D"), "<init>", "(III)V", a, NULL)) return 0;
    a[0].r = image;
    return call_ok(vm, result, C("Texture2D"), "<init>", "(L" C("Image2D") ";)V", a, NULL)
               ? result : 0;
}

/* Same count/allocate/fill and Appearance branch as BioCtrl.setTexture.
 * The synthetic graph is acyclic; depth limit diagnoses an unexpected cycle.
 * Appearances are updated, and other returned objects are recursively visited.
 */
static int bio_set_texture(nj_vm *vm, nj_ref self, nj_ref tex, int depth)
{
    nj_u4 mark = nj_root_mark(vm);
    nj_value a[2], count, filled;
    nj_ref refs;
    nj_class *appearance = nj_class_lookup(vm, C("Appearance"));
    int i, updates = 0;
    check(depth < 16, "game-style traversal remains acyclic");
    if (depth >= 16 || !appearance) return -1;
    a[0].r = 0; count.i = -1;
    if (!call_ok(vm, self, C("Object3D"), "getReferences", REFS_DESC, a, &count))
        goto failed;
    if (count.i == 0) goto done;
    if (count.i < 0 || count.i > 128) goto failed;
    refs = ref_array(vm, C("Object3D"), count.i);
    if (!refs) goto failed;
    a[0].r = refs; filled.i = -1;
    if (!call_ok(vm, self, C("Object3D"), "getReferences", REFS_DESC, a, &filled))
        goto failed;
    check(filled.i == count.i, "game-style exact allocation agrees with fill count");
    if (filled.i != count.i) goto failed;
    for (i = 0; i < count.i; ++i) {
        nj_ref item = nj_array_get_ref(refs, (nj_u4)i);
        if (!item) goto failed;
        if (nj_class_is_subclass_of(item->cls, appearance)) {
            a[0].i = 0; a[1].r = tex;
            if (!call_ok(vm, item, C("Appearance"), "setTexture",
                         "(IL" C("Texture2D") ";)V", a, NULL)) goto failed;
            ++updates;
        } else {
            int nested = bio_set_texture(vm, item, tex, depth + 1);
            if (nested < 0) goto failed;
            updates += nested;
        }
    }
done:
    nj_root_release(vm, mark);
    return updates;
failed:
    nj_root_release(vm, mark);
    return -1;
}

static void expect_texture(nj_vm *vm, nj_ref app, nj_ref wanted)
{
    nj_value a, r;
    a.i = 0; r.r = 0;
    if (call_ok(vm, app, C("Appearance"), "getTexture", "(I)L" C("Texture2D") ";", &a, &r))
        check(r.r == wanted, "Appearance.getTexture returns the expected texture identity");
}

static void test_mesh(nj_vm *vm)
{
    nj_ref vb = make(vm, C("VertexBuffer")), ib = strip(vm);
    nj_ref app = make(vm, C("Appearance")), replacement = make(vm, C("Appearance"));
    nj_ref target = mesh(vm, vb, ib, app), tex = texture(vm), old_tex = texture(vm);
    nj_ref want[3];
    nj_value a[2];
    int updates;
    if (!vb || !ib || !app || !replacement || !target || !tex || !old_tex) return;
    want[0] = vb; want[1] = ib; want[2] = app;
    expect_appearance(vm, target, 0, app);
    expect_refs(vm, target, want, 3, replacement, "Mesh single submesh");
    expect_short(vm, target, 3, replacement);
    a[0].i = 0; a[1].r = old_tex;
    if (!call_ok(vm, app, C("Appearance"), "setTexture",
                 "(IL" C("Texture2D") ";)V", a, NULL)) return;
    expect_texture(vm, app, old_tex);
    updates = bio_set_texture(vm, target, tex, 0);
    printf("Mesh BioCtrl recursion: appearance updates=%d expected=1\n", updates);
    check(updates == 1, "game-style traversal reaches Mesh Appearance");
    expect_texture(vm, app, tex);
    if (!set_appearance(vm, target, 0, replacement)) return;
    expect_appearance(vm, target, 0, replacement);
    want[2] = replacement;
    expect_refs(vm, target, want, 3, app, "Mesh replacement excludes old Appearance");
    if (!set_appearance(vm, target, 0, 0)) return;
    expect_appearance(vm, target, 0, 0);
    expect_refs(vm, target, want, 2, app, "Mesh null Appearance");
}

static void test_shared(nj_vm *vm)
{
    nj_ref vb = make(vm, C("VertexBuffer")), ib0 = strip(vm), ib1 = strip(vm);
    nj_ref shared = make(vm, C("Appearance")), replacement = make(vm, C("Appearance"));
    nj_ref ibs = ref_array(vm, C("IndexBuffer"), 3);
    nj_ref apps = ref_array(vm, C("Appearance"), 3);
    nj_ref target = object(vm, C("Mesh"));
    nj_ref want[6];
    nj_value a[3];
    if (!vb || !ib0 || !ib1 || !shared || !replacement || !ibs || !apps || !target) return;
    nj_array_set_ref(ibs, 0u, ib0); nj_array_set_ref(ibs, 1u, ib1); nj_array_set_ref(ibs, 2u, ib0);
    nj_array_set_ref(apps, 0u, shared); nj_array_set_ref(apps, 1u, shared); /* slot 2 null */
    a[0].r = vb; a[1].r = ibs; a[2].r = apps;
    if (!call_ok(vm, target, C("Mesh"), "<init>", MESH_ARRAY_DESC, a, NULL)) return;
    want[0] = vb; want[1] = ib0; want[2] = ib1; want[3] = ib0;
    want[4] = shared; want[5] = shared;
    expect_refs(vm, target, want, 6, replacement, "Shared slots preserve duplicates (normative)");
    if (!set_appearance(vm, target, 0, replacement)) return;
    want[4] = replacement;
    expect_appearance(vm, target, 1, shared);
    expect_refs(vm, target, want, 6, target, "Replacing one shared slot preserves the other");
    if (!set_appearance(vm, target, 1, 0)) return;
    expect_refs(vm, target, want, 5, shared, "Clearing final shared slot removes shared identity");
    if (!set_appearance(vm, target, 0, 0)) return;
    expect_refs(vm, target, want, 4, shared, "All null Appearance slots are omitted");
}

static void test_skin(nj_vm *vm)
{
    nj_ref vb = make(vm, C("VertexBuffer")), ib = strip(vm);
    nj_ref app = make(vm, C("Appearance")), replacement = make(vm, C("Appearance"));
    nj_ref skeleton = make(vm, C("Group")), target = object(vm, C("SkinnedMesh"));
    nj_ref wrapper = make(vm, C("Group")), tex = texture(vm), refs;
    nj_value a[4], r;
    int count, updates;
    if (!vb || !ib || !app || !replacement || !skeleton || !target || !wrapper || !tex) return;
    a[0].r = vb; a[1].r = ib; a[2].r = app; a[3].r = skeleton;
    if (!call_ok(vm, target, C("SkinnedMesh"), "<init>",
                 "(L" C("VertexBuffer") ";L" C("IndexBuffer") ";L" C("Appearance") ";L" C("Group") ";)V", a, NULL)) return;
    expect_appearance(vm, target, 0, app);
    a[0].r = 0; r.i = -1;
    if (!call_ok(vm, target, C("Object3D"), "getReferences", REFS_DESC, a, &r)) return;
    count = r.i;
    check(count >= 0 && count < 16, "small SkinnedMesh direct reference count is bounded");
    if (count < 0 || count >= 16) return;
    refs = ref_array(vm, C("Object3D"), count);
    if (!refs) return;
    a[0].r = refs;
    if (!call_ok(vm, target, C("Object3D"), "getReferences", REFS_DESC, a, &r)) return;
    check(r.i == count, "SkinnedMesh count and exact fill agree");
    check(occurrences(refs, count, vb) == 1 && occurrences(refs, count, ib) == 1,
          "SkinnedMesh inherits vertex and index references");
    check(occurrences(refs, count, app) == 1, "SkinnedMesh inherits Appearance reference");
    /* Do not pin skeleton enumeration or total count in this focused test. */
    a[0].r = target;
    if (!call_ok(vm, wrapper, C("Group"), "addChild", "(L" C("Node") ";)V", a, NULL)) return;
    updates = bio_set_texture(vm, wrapper, tex, 0);
    printf("Group/SkinnedMesh BioCtrl recursion: appearance updates=%d expected=1\n", updates);
    check(updates == 1, "game-style recursion follows Group then SkinnedMesh Appearance");
    expect_texture(vm, app, tex);
    if (!set_appearance(vm, target, 0, replacement)) return;
    expect_appearance(vm, target, 0, replacement);
    updates = bio_set_texture(vm, target, tex, 0);
    check(updates == 1, "game-style recursion sees inherited replacement Appearance");
    expect_texture(vm, replacement, tex);
    if (!set_appearance(vm, target, 0, 0)) return;
    check(bio_set_texture(vm, target, tex, 0) == 0,
          "game-style recursion ignores inherited null Appearance");
}

static void test_large(nj_vm *vm)
{
    enum { CHILDREN = 70 };
    nj_ref parent = make(vm, C("Group")), sentinel = make(vm, C("Appearance"));
    nj_ref want[CHILDREN], exact;
    nj_value a, r;
    int i, all_present;
    if (!parent || !sentinel) return;
    for (i = 0; i < CHILDREN; ++i) {
        want[i] = make(vm, C("Group"));
        if (!want[i]) return;
        a.r = want[i];
        if (!call_ok(vm, parent, C("Group"), "addChild", "(L" C("Node") ";)V", &a, NULL)) return;
    }
    expect_short(vm, parent, CHILDREN, sentinel);
    exact = ref_array(vm, C("Object3D"), CHILDREN);
    if (!exact) return;
    a.r = exact; r.i = -1;
    if (!call_ok(vm, parent, C("Object3D"), "getReferences", REFS_DESC, &a, &r)) return;
    check(r.i == CHILDREN, "more than 64 references fill an exact-size destination");
    all_present = 1;
    for (i = 0; i < CHILDREN; ++i) all_present &= occurrences(exact, CHILDREN, want[i]) == 1;
    check(all_present, "exact-size fill preserves all identities past reference 64");
    for (i = 0; i < CHILDREN; ++i) nj_array_set_ref(exact, (nj_u4)i, 0);
    expect_refs(vm, parent, want, CHILDREN, sentinel, "Group 70 direct references (past old 64 limit)");
}

int main(int argc, char **argv)
{
    static const struct {
        const char *name;
        void (*run)(nj_vm *);
    } cases[] = {
        { "mesh", test_mesh }, { "shared", test_shared },
        { "skin", test_skin }, { "large", test_large }
    };
    nj_port port;
    nj_vm *vm;
    unsigned i;
    int selected = 0;
    if (argc > 2) {
        fprintf(stderr, "usage: %s [mesh|shared|skin|large]\n", argv[0]);
        return 2;
    }
    nj_port_headless_ex(&port, 240, 320, 0, NULL);
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    if (!vm || nj_lib_late_init(vm) != NJ_OK) {
        fprintf(stderr, "cannot initialize runtime\n");
        if (vm) nj_vm_destroy(vm);
        nj_port_shutdown(&port);
        return 2;
    }
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (argc == 1 || strcmp(argv[1], cases[i].name) == 0) {
            nj_u4 mark = nj_root_mark(vm);
            ++selected;
            printf("== %s ==\n", cases[i].name);
            cases[i].run(vm);
            nj_root_release(vm, mark);
        }
    }
    check(selected > 0, "requested case exists");
    printf("m3g references: %d checks, %d failures\n", checks, failures);
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    return failures ? 1 : 0;
}
