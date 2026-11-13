/* MIDP ImageItem alternate-text contract regression.
 * Public native calls, exception controls and reference retention across GC.
 * No rendering or mutable-image snapshot assumptions.
 */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define II "javax/microedition/lcdui/ImageItem"
#define ITEM "javax/microedition/lcdui/Item"
#define IMAGE "javax/microedition/lcdui/Image"
#define STR "Ljava/lang/String;"
#define IMG "Ljavax/microedition/lcdui/Image;"
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static const char *const ctors[] = {
    "(" STR IMG "I" STR ")V", "(" STR IMG "I" STR "I)V"
};
static unsigned checks, failures;
static const char *group = "setup";
#define CHECK(c, msg) do { \
    ++checks; \
    if (!(c)) { \
        ++failures; \
        fprintf(stderr, "FAIL ImageItem %s line %d: %s\n", group, __LINE__, msg); \
    } \
} while (0)

static nj_ref root(nj_vm *vm, nj_ref r)
{
    nj_u4 mark = nj_root_mark(vm);
    if (!r || nj_root(vm, r) != r || nj_root_mark(vm) != mark + 1u) {
        fprintf(stderr, "ImageItem fixture: allocation/root insertion failed\n");
        exit(2);
    }
    return r;
}

/* Report unexpected exceptions as failures BEFORE clearing to isolate cases. */
static int call_ok(nj_vm *vm, nj_ref self, const char *cls, const char *name,
                   const char *desc, nj_value *args, nj_value *ret)
{
    nj_status st;
    const char *ex;
    if (ret) { memset(ret, 0, sizeof(*ret)); }
    st = nj_call(vm, self, cls, name, desc, args, ret);
    CHECK(st == NJ_OK && !nj_has_exception(vm), name);
    if (st == NJ_OK && !nj_has_exception(vm)) { return 1; }
    ex = nj_ref_class_name(vm->exception);
    fprintf(stderr, "  %s%s status=%d exception=%s\n", name, desc,
            (int)st, ex ? ex : "<none>");
    nj_clear_exception(vm);
    return 0;
}

static void expect_iae(nj_vm *vm, nj_status st)
{
    const char *ex = nj_ref_class_name(vm->exception);
    int ok = st != NJ_OK && ex &&
             strcmp(ex, "java/lang/IllegalArgumentException") == 0;
    CHECK(ok, "invalid layout/appearance still throws IllegalArgumentException");
    if (!ok) {
        fprintf(stderr, "  expected IAE: status=%d exception=%s\n",
                (int)st, ex ? ex : "<none>");
    }
    nj_clear_exception(vm);
}

static void expect_alt(nj_vm *vm, nj_ref item, const char *expected)
{
    nj_value r;
    if (!call_ok(vm, item, II, "getAltText", "()" STR, NULL, &r)) { return; }
    if (!expected) {
        CHECK(r.r == NULL, "getAltText returns null when no alternate text");
    } else {
        CHECK(r.r && nj_heap_owns_object(vm, r.r), "returned alternate text is live");
        if (r.r && nj_heap_owns_object(vm, r.r)) {
            CHECK(nj_string_equals_utf8(r.r, expected), "alternate text contents preserved");
        }
    }
}

static void collect(nj_vm *vm, nj_u4 roots)
{
    nj_u4 old = vm->heap.gc_count;
    CHECK(nj_root_mark(vm) == roots, "only expected host roots remain before GC");
    CHECK(nj_gc_run(vm) == NJ_OK && !nj_has_exception(vm), "explicit GC succeeds");
    CHECK(vm->heap.gc_count > old, "collection actually executed");
    CHECK(nj_root_mark(vm) == roots, "GC does not add host roots");
}

/* Immutable 1x1 image: exercises reference retention without mutable snapshots. */
static nj_ref immutable_image(nj_vm *vm)
{
    nj_u4 mark = nj_root_mark(vm);
    nj_ref pixels = root(vm, nj_int_array_new(vm, 1u));
    nj_value a[4], r;
    nj_status st;
    memset(a, 0, sizeof(a)); memset(&r, 0, sizeof(r));
    nj_array_set_int(pixels, 0u, (nj_s4)0x00123456);
    a[0].r = pixels; a[1].i = 1; a[2].i = 1; a[3].i = 0;
    st = nj_call_static(vm, IMAGE, "createRGBImage", "([IIIZ)" IMG, a, &r);
    if (st != NJ_OK || nj_has_exception(vm) || !r.r) {
        fprintf(stderr, "ImageItem fixture: createRGBImage failed\n");
        exit(2);
    }
    nj_root_release(vm, mark);
    return r.r; /* Caller roots immediately, with no intervening Java allocation. */
}

static void expect_payload(nj_vm *vm, nj_ref item)
{
    nj_value r, n;
    if (call_ok(vm, item, ITEM, "getLabel", "()" STR, NULL, &r)) {
        CHECK(r.r && nj_heap_owns_object(vm, r.r), "label retained through Item field");
        if (r.r && nj_heap_owns_object(vm, r.r)) {
            CHECK(nj_string_equals_utf8(r.r, "image-item-label"), "label contents retained");
        }
    }
    if (call_ok(vm, item, II, "getImage", "()" IMG, NULL, &r)) {
        CHECK(r.r && nj_heap_owns_object(vm, r.r), "image retained through ImageItem fields");
        if (r.r && nj_heap_owns_object(vm, r.r)) {
            if (call_ok(vm, r.r, IMAGE, "getWidth", "()I", NULL, &n)) {
                CHECK(n.i == 1, "retained image width");
            }
            if (call_ok(vm, r.r, IMAGE, "getHeight", "()I", NULL, &n)) {
                CHECK(n.i == 1, "retained image height");
            }
        }
    }
}

static int set_alt(nj_vm *vm, nj_ref item, const char *text)
{
    nj_u4 mark = nj_root_mark(vm);
    nj_value a;
    int ok;
    memset(&a, 0, sizeof(a));
    if (text) { a.r = root(vm, nj_string_new(vm, text)); }
    ok = call_ok(vm, item, II, "setAltText", "(" STR ")V", &a, NULL);
    nj_root_release(vm, mark); /* Non-null setter input is now held by item alone. */
    return ok;
}

static void roundtrip(nj_vm *vm, unsigned which, int mode, int with_text)
{
    static const nj_s4 bad_layout[] = {-1, 4, 0x40, 0x8000};
    nj_u4 mark = nj_root_mark(vm), held;
    nj_ref item = root(vm, nj_object_new(vm, II));
    nj_value a[5], r;
    unsigned i;
    group = which ? "five-argument roundtrip" : "four-argument roundtrip";
    held = nj_root_mark(vm); /* The item is the ONLY root retained across GC. */
    memset(a, 0, sizeof(a));
    a[0].r = root(vm, nj_string_new(vm, "image-item-label"));
    a[1].r = root(vm, immutable_image(vm));
    a[2].i = 0x203; /* Existing accepted layout, not a new layout policy. */
    if (with_text) { a[3].r = root(vm, nj_string_new(vm, "constructor-alt")); }
    a[4].i = mode;
    if (!call_ok(vm, item, II, "<init>", ctors[which], a, NULL)) {
        nj_root_release(vm, mark);
        return;
    }
    nj_root_release(vm, held);
    memset(a, 0, sizeof(a)); /* Stale C values are not VM GC roots. */
    collect(vm, held);
    expect_alt(vm, item, with_text ? "constructor-alt" : NULL);
    expect_payload(vm, item);
    if (which && call_ok(vm, item, II, "getAppearanceMode", "()I", NULL, &r)) {
        CHECK(r.i == mode, "valid explicit appearance retained");
    }
    if (call_ok(vm, item, ITEM, "getLayout", "()I", NULL, &r)) {
        CHECK(r.i == 0x203, "constructor still publishes inherited layout");
    }
    for (i = 0; i < COUNT(bad_layout); ++i) {
        a[0].i = bad_layout[i];
        expect_iae(vm, nj_call(vm, item, II, "setLayout", "(I)V", a, NULL));
        if (call_ok(vm, item, ITEM, "getLayout", "()I", NULL, &r)) {
            CHECK(r.i == 0x203, "invalid setLayout leaves accepted layout intact");
        }
        expect_alt(vm, item, with_text ? "constructor-alt" : NULL);
    }
    if (set_alt(vm, item, "setter-alt-distinct")) {
        collect(vm, held);
        expect_alt(vm, item, "setter-alt-distinct");
        expect_payload(vm, item);
    }
    if (set_alt(vm, item, NULL)) {
        collect(vm, held);
        expect_alt(vm, item, NULL);
        expect_payload(vm, item);
    }
    if (set_alt(vm, item, NULL)) { expect_alt(vm, item, NULL); } /* Idempotent clear. */
    if (set_alt(vm, item, "")) {
        collect(vm, held);
        expect_alt(vm, item, ""); /* Empty is distinct from null. */
    }
    if (set_alt(vm, item, "after-null-alt")) {
        collect(vm, held);
        expect_alt(vm, item, "after-null-alt");
    }
    nj_root_release(vm, mark);
}

static void invalid_constructors(nj_vm *vm)
{
    static const nj_s4 bad_layout[] = {-1, 4, 0x40, 0x8000};
    static const nj_s4 bad_mode[] = {-1, 3, INT_MIN, INT_MAX};
    nj_u4 mark = nj_root_mark(vm);
    nj_ref text = root(vm, nj_string_new(vm, "valid-alt-control"));
    unsigned which, nonnull, i;
    nj_value a[5];
    group = "invalid constructor controls";
    for (which = 0; which < COUNT(ctors); ++which) {
        for (nonnull = 0; nonnull < 2u; ++nonnull) {
            for (i = 0; i < COUNT(bad_layout); ++i) {
                nj_u4 inner = nj_root_mark(vm);
                nj_ref item = root(vm, nj_object_new(vm, II));
                memset(a, 0, sizeof(a));
                a[2].i = bad_layout[i]; a[3].r = nonnull ? text : NULL;
                expect_iae(vm, nj_call(vm, item, II, "<init>", ctors[which], a, NULL));
                nj_root_release(vm, inner);
            }
        }
    }
    for (nonnull = 0; nonnull < 2u; ++nonnull) {
        for (i = 0; i < COUNT(bad_mode); ++i) {
            nj_u4 inner = nj_root_mark(vm);
            nj_ref item = root(vm, nj_object_new(vm, II));
            memset(a, 0, sizeof(a));
            a[3].r = nonnull ? text : NULL; a[4].i = bad_mode[i];
            expect_iae(vm, nj_call(vm, item, II, "<init>", ctors[1], a, NULL));
            nj_root_release(vm, inner);
        }
    }
    nj_root_release(vm, mark);
}

int main(void)
{
    nj_port port;
    nj_vm *vm;
    unsigned which, nonnull;
    int mode;
    memset(&port, 0, sizeof(port));
    if (nj_port_headless(&port) != NJ_OK) { return 2; }
    vm = nj_vm_create(&port, 512u * 1024u);
    if (!vm) { nj_port_shutdown(&port); return 2; }
    for (which = 0; which < COUNT(ctors); ++which) {
        for (nonnull = 0; nonnull < 2u; ++nonnull) {
            for (mode = 0; mode < (which ? 3 : 1); ++mode) {
                roundtrip(vm, which, mode, (int)nonnull);
            }
        }
    }
    invalid_constructors(vm);
    CHECK(!nj_has_exception(vm), "no unexamined exception after cases");
    CHECK(nj_root_mark(vm) == 0u, "fixture releases all host roots");
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    printf("ImageItem altText: %u checks, %u failed\n", checks, failures);
    return failures ? 1 : 0;
}
