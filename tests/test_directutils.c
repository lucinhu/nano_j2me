/* Nokia DirectUtils images preserve ARGB, remain writable after decoding,
 * and survive collections during pixel-store allocation. SDK format constants
 * must also agree with values compiled into real Nokia MIDlets. */
#include "nj2me/nj.h"
#include "../src/midp/nj_midp.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DU "com/nokia/mid/ui/DirectUtils"
#define DG "com/nokia/mid/ui/DirectGraphics"
#define IMAGE_DESC "Ljavax/microedition/lcdui/Image;"
#define CHECK(c, msg) do { if (c) { ++passed; } else { ++failed; \
    fprintf(stderr, "FAIL %d: %s\n", __LINE__, msg); } } while (0)
static unsigned passed, failed;

/* A 3x2 RGBA PNG, with alpha 0, 1, 127, 128, 254, 255 respectively. */
static const nj_u1 png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0, 0, 0, 3, 0, 0, 0, 2, 8, 6, 0, 0, 0,
    0x9d, 0x74, 0x66, 0x1a, 0, 0, 0, 0x23, 0x49, 0x44, 0x41, 0x54,
    0x78, 0x9c, 0x63, 0x10, 0x32, 0x09, 0x63, 0x28, 0x5e, 0x2d, 0xc9,
    0xf8, 0x4f, 0xc0, 0xab, 0x9e, 0x41, 0xe0, 0x7e, 0x66, 0xc3, 0x11,
    0xa6, 0xc6, 0x7f, 0xa6, 0x3e, 0x13, 0xfe, 0x03, 0, 0x69, 0x44,
    0x09, 0xd9, 0x92, 0xe5, 0x12, 0x2e, 0, 0, 0, 0, 0x49, 0x45,
    0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
};
static const nj_u4 png_argb[] = {
    0x00123456u, 0x0173ab19u, 0x7ffe104au,
    0x8010df69u, 0xfec40281u, 0xff354c90u
};

static nj_ref create_color(nj_vm *vm, int w, int h, nj_u4 color)
{
    nj_value a[3], r;
    a[0].i = w; a[1].i = h; a[2].i = (nj_s4)color;
    r.r = NULL;
    CHECK(nj_call_static(vm, DU, "createImage", "(III)" IMAGE_DESC,
                         a, &r) == NJ_OK, "create ARGB image");
    return r.r;
}

static void expect_exception(nj_vm *vm, nj_status status, const char *type)
{
    const char *name = nj_ref_class_name(vm->exception);
    CHECK(status != NJ_OK && name && strcmp(name, type) == 0,
          "documented exception type");
    if (name && strcmp(name, type) != 0) {
        fprintf(stderr, "  expected %s, got %s\n", type, name);
    }
    nj_clear_exception(vm);
}

/* Read through Image.getRGB rather than the internal pixel accessor. */
static void expect_pixels(nj_vm *vm, nj_ref image, int w, int h,
                          const nj_u4 *colors, int uniform)
{
    nj_value a[7];
    nj_u4 mark = nj_root_mark(vm);
    nj_ref out = nj_root(vm, nj_int_array_new(vm, (nj_u4)(w * h + 2)));
    nj_u4 *data;
    int i, equal = 1;
    CHECK(out != NULL, "pixel readback array");
    if (!out) { nj_clear_exception(vm); goto done; }
    data = (nj_u4 *)nj_array_data(out);
    data[0] = data[w * h + 1] = 0xa59c173eu;
    a[0].r = out; a[1].i = 1; a[2].i = w;
    a[3].i = a[4].i = 0; a[5].i = w; a[6].i = h;
    CHECK(nj_call(vm, image, NJ_C_IMAGE, "getRGB", "([IIIIIII)V", a, NULL)
          == NJ_OK, "read exact ARGB through Image.getRGB");
    for (i = 0; i < w * h; ++i) {
        if (data[i + 1] != colors[uniform ? 0 : i]) { equal = 0; break; }
    }
    CHECK(equal, "all image pixels preserve the specified ARGB bits");
    CHECK(data[0] == 0xa59c173eu && data[w * h + 1] == 0xa59c173eu,
          "getRGB leaves surrounding array elements unchanged");
done:
    nj_root_release(vm, mark);
}

static void check_writable(nj_vm *vm, nj_ref image)
{
    nj_value a[4], r;
    nj_u4 mark = nj_root_mark(vm);
    CHECK(nj_call_int(vm, image, NJ_C_IMAGE, "isMutable", "()Z", NULL) == 1,
          "DirectUtils image is mutable");
    r.r = NULL;
    CHECK(nj_call(vm, image, NJ_C_IMAGE, "getGraphics",
          "()Ljavax/microedition/lcdui/Graphics;", NULL, &r) == NJ_OK,
          "mutable image exposes Graphics");
    if (r.r && !nj_has_exception(vm)) {
        nj_ref g = nj_root(vm, r.r);
        a[0].i = 0x2468ac;
        CHECK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL)
              == NJ_OK, "set image drawing color");
        a[0].i = a[1].i = 0; a[2].i = a[3].i = 1;
        CHECK(nj_call(vm, g, NJ_C_GRAPHICS, "fillRect", "(IIII)V", a, NULL)
              == NJ_OK, "draw into DirectUtils image");
        CHECK(nj_midp_image_pixels(vm, image)[0] == 0xff2468acu,
              "drawing changes the decoded image pixel store");
    }
    nj_clear_exception(vm);
    nj_root_release(vm, mark);
}

static void test_constants(nj_vm *vm)
{
    static const struct { const char *name; int value; } values[] = {
        {"TYPE_BYTE_1_GRAY", 1}, {"TYPE_BYTE_1_GRAY_VERTICAL", -1},
        {"TYPE_BYTE_2_GRAY", 2}, {"TYPE_BYTE_4_GRAY", 4},
        {"TYPE_BYTE_8_GRAY", 8}, {"TYPE_BYTE_332_RGB", 332},
        {"TYPE_USHORT_4444_ARGB", 4444}, {"TYPE_USHORT_444_RGB", 444},
        {"TYPE_USHORT_555_RGB", 555}, {"TYPE_USHORT_565_RGB", 565},
        {"TYPE_INT_888_RGB", 888}, {"TYPE_INT_8888_ARGB", 8888},
        {"TYPE_USHORT_1555_ARGB", 1555}
    };
    unsigned i;
    for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        nj_field *f = nj_field_lookup(vm, DG, values[i].name, "I", 1);
        CHECK(f && nj_field_read_int(vm, NULL, f) == values[i].value,
              values[i].name);
    }
}

static void test_argb(nj_vm *vm)
{
    int alpha;
    for (alpha = 0; alpha < 256; ++alpha) {
        nj_u4 mark = nj_root_mark(vm);
        nj_u4 color = ((nj_u4)alpha << 24) | 0x123456u;
        nj_ref image = nj_root(vm, create_color(vm, 3, 2, color));
        CHECK(image != NULL, "ARGB image returned");
        if (image) {
            expect_pixels(vm, image, 3, 2, &color, 1);
            CHECK(nj_call_int(vm, image, NJ_C_IMAGE, "isMutable", "()Z", NULL)
                  == 1, "ARGB image remains mutable for every alpha");
        }
        nj_clear_exception(vm);
        nj_root_release(vm, mark);
    }
    {
        nj_u4 mark = nj_root_mark(vm), white = 0xffffffffu;
        nj_value a[2], r;
        a[0].i = 1; a[1].i = 4; r.r = NULL;
        CHECK(nj_call_static(vm, DU, "createImage", "(II)" IMAGE_DESC, a, &r)
              == NJ_OK && r.r, "legacy two-argument image creation");
        if (r.r) {
            nj_root(vm, r.r);
            expect_pixels(vm, r.r, 1, 4, &white, 1);
            check_writable(vm, r.r);
        }
        nj_root_release(vm, mark);
    }
}

static nj_ref png_array(nj_vm *vm, unsigned pad)
{
    nj_ref data = nj_byte_array_new(vm, (nj_u4)sizeof(png) + 2u * pad);
    if (data) {
        memset(nj_array_data(data), 0xa5, nj_array_length(data));
        memcpy((nj_u1 *)nj_array_data(data) + pad, png, sizeof(png));
    }
    return data;
}

static void test_encoded(nj_vm *vm)
{
    unsigned pad;
    for (pad = 0; pad <= 5; pad += 5) {
        nj_u4 mark = nj_root_mark(vm);
        nj_ref data = nj_root(vm, png_array(vm, pad)), image;
        nj_value a[3], r;
        CHECK(data != NULL, "encoded image byte array");
        if (!data) { nj_clear_exception(vm); nj_root_release(vm, mark); continue; }
        a[0].r = data; a[1].i = (nj_s4)pad; a[2].i = (nj_s4)sizeof(png);
        r.r = NULL;
        CHECK(nj_call_static(vm, DU, "createImage", "([BII)" IMAGE_DESC, a, &r)
              == NJ_OK && r.r, "decode PNG at requested byte offset");
        image = nj_root(vm, r.r);
        if (image) {
            CHECK(nj_call_int(vm, image, NJ_C_IMAGE, "getWidth", "()I", NULL)
                  == 3, "decoded width");
            CHECK(nj_call_int(vm, image, NJ_C_IMAGE, "getHeight", "()I", NULL)
                  == 2, "decoded height");
            expect_pixels(vm, image, 3, 2, png_argb, 0);
            check_writable(vm, image);
            CHECK(memcmp(nj_array_data(data), pad ? "\xa5\xa5\xa5\xa5\xa5" : "", pad)
                  == 0 && memcmp((nj_u1 *)nj_array_data(data) + pad,
                                 png, sizeof(png)) == 0,
                  "creating and drawing never change encoded input bytes");
            CHECK(nj_midp_image_pixels(vm, image)[1] == png_argb[1] &&
                  nj_midp_image_pixels(vm, image)[5] == png_argb[5],
                  "drawing preserves pixels outside the requested rectangle");
        }
        nj_clear_exception(vm);
        r.r = NULL;
        CHECK(nj_call_static(vm, NJ_C_IMAGE, "createImage", "([BII)" IMAGE_DESC,
                             a, &r) == NJ_OK && r.r, "standard MIDP PNG creation");
        if (r.r) {
            nj_root(vm, r.r);
            CHECK(nj_call_int(vm, r.r, NJ_C_IMAGE, "isMutable", "()Z", NULL) == 0,
                  "standard MIDP decoded image remains immutable");
            expect_pixels(vm, r.r, 3, 2, png_argb, 0);
        }
        nj_clear_exception(vm);
        nj_root_release(vm, mark);
    }
}

static void test_invalid(nj_vm *vm)
{
    static const int dims[][2] = {{0,1}, {1,0}, {-1,2}, {2,-1}, {INT_MIN,1}};
    static const int ranges[][2] = {
        {-1,1}, {0,-1}, {1,(int)sizeof(png)}, {INT_MAX,1},
        {1,INT_MAX}, {INT_MAX,INT_MAX}
    };
    nj_u4 mark = nj_root_mark(vm);
    nj_ref data = nj_root(vm, png_array(vm, 0));
    nj_value a[3], r;
    unsigned i;
    nj_status st;
    for (i = 0; i < sizeof(dims) / sizeof(dims[0]); ++i) {
        a[0].i = dims[i][0]; a[1].i = dims[i][1]; a[2].i = 0x10203040;
        st = nj_call_static(vm, DU, "createImage", "(III)" IMAGE_DESC, a, &r);
        expect_exception(vm, st, "java/lang/IllegalArgumentException");
        st = nj_call_static(vm, DU, "createImage", "(II)" IMAGE_DESC, a, &r);
        expect_exception(vm, st, "java/lang/IllegalArgumentException");
    }
    a[0].i = a[1].i = INT_MAX; a[2].i = 0x10203040;
    st = nj_call_static(vm, DU, "createImage", "(III)" IMAGE_DESC, a, &r);
    expect_exception(vm, st, "java/lang/OutOfMemoryError");
    st = nj_call_static(vm, DU, "createImage", "(II)" IMAGE_DESC, a, &r);
    expect_exception(vm, st, "java/lang/OutOfMemoryError");
    a[0].r = NULL; a[1].i = 0; a[2].i = 1;
    st = nj_call_static(vm, DU, "createImage", "([BII)" IMAGE_DESC, a, &r);
    expect_exception(vm, st, "java/lang/NullPointerException");
    for (i = 0; i < sizeof(ranges) / sizeof(ranges[0]); ++i) {
        a[0].r = data; a[1].i = ranges[i][0]; a[2].i = ranges[i][1];
        st = nj_call_static(vm, DU, "createImage", "([BII)" IMAGE_DESC, a, &r);
        expect_exception(vm, st, "java/lang/ArrayIndexOutOfBoundsException");
    }
    a[0].r = data; a[1].i = 0; a[2].i = 0;
    st = nj_call_static(vm, DU, "createImage", "([BII)" IMAGE_DESC, a, &r);
    expect_exception(vm, st, "java/lang/IllegalArgumentException");
    a[1].i = 1; a[2].i = (nj_s4)sizeof(png) - 1;
    st = nj_call_static(vm, DU, "createImage", "([BII)" IMAGE_DESC, a, &r);
    expect_exception(vm, st, "java/lang/IllegalArgumentException");
    nj_root_release(vm, mark);
}

static void test_gc(nj_port *port, int encoded)
{
    nj_vm *vm = nj_vm_create(port, 1024u * 1024u);
    nj_u4 gc, color = 0x40123456u, mark;
    nj_size tail;
    nj_ref image, data = NULL;
    nj_value a[3], r;
    CHECK(vm != NULL, "small GC pressure VM");
    if (!vm) { return; }
    mark = nj_root_mark(vm);
    /* Resolve the same classes and fields before creating the pressure block. */
    image = nj_root(vm, create_color(vm, 1, 1, color));
    if (image) { check_writable(vm, image); }
    if (encoded) { data = nj_root(vm, png_array(vm, 0)); }
    tail = vm->heap.size - vm->heap.bump;
    CHECK(tail > NJ_BLOCK_HDR + 32u, "space to construct deterministic pressure");
    CHECK(nj_alloc(vm, tail - NJ_BLOCK_HDR - 32u) != NULL,
          "unreachable arena filler");
    gc = vm->heap.gc_count;
    r.r = NULL;
    if (encoded) {
        a[0].r = data; a[1].i = 0; a[2].i = (nj_s4)sizeof(png);
        CHECK(nj_call_static(vm, DU, "createImage", "([BII)" IMAGE_DESC, a, &r)
              == NJ_OK && r.r, "decode under Java heap pressure");
        image = nj_root(vm, r.r);
    } else {
        image = nj_root(vm, create_color(vm, 295, 279, color));
    }
    CHECK(vm->heap.gc_count > gc, "collection actually occurred inside creation");
    if (image) {
        CHECK(nj_heap_owns_object(vm, image), "returned Image is a live GC object");
        expect_pixels(vm, image, encoded ? 3 : 295, encoded ? 2 : 279,
                      encoded ? png_argb : &color, !encoded);
        check_writable(vm, image);
        CHECK(nj_gc_run(vm) == NJ_OK, "explicit collection after obtaining Graphics");
        CHECK(nj_midp_image_pixels(vm, image)[0] == 0xff2468acu,
              "image and its modified pixels survive another collection");
    }
    nj_clear_exception(vm);
    nj_root_release(vm, mark);
    nj_vm_destroy(vm);
}

/* Inject failure of precisely the next root-table expansion. Its host header
 * overhead is measured through the allocator, not copied from private structs. */
static void *(*base_alloc)(void *, nj_size);
static nj_size last_size, fail_size;
static unsigned root_failures;
static void *fault_alloc(void *ud, nj_size size)
{
    last_size = size;
    if (size == fail_size) { fail_size = 0; ++root_failures; return NULL; }
    return base_alloc ? base_alloc(ud, size) : malloc(size);
}

static void test_root_failure(nj_port *port)
{
    nj_port local = *port;
    nj_vm *vm;
    nj_ref data;
    nj_u4 mark;
    int encoded, available;
    base_alloc = local.alloc;
    local.alloc = fault_alloc;
    vm = nj_vm_create(&local, 512u * 1024u);
    CHECK(vm != NULL, "root-fault VM");
    if (!vm) { return; }
    mark = nj_root_mark(vm);
    data = nj_root(vm, png_array(vm, 0));
    for (encoded = 0; encoded <= 1; ++encoded) {
        for (available = 0; available < 2; ++available) {
            nj_u4 saved = nj_root_mark(vm), padded;
            nj_size bytes;
            void *probe;
            nj_value a[3], r;
            nj_status st;
            if (vm->nroots == vm->root_cap) { nj_root(vm, NULL); }
            while (vm->nroots < vm->root_cap - (nj_u4)available) { nj_root(vm, NULL); }
            padded = nj_root_mark(vm);
            bytes = (nj_size)(vm->root_cap * 2u) * sizeof(nj_ref);
            probe = nj_host_alloc(vm, bytes);
            CHECK(probe != NULL, "measure next root allocation");
            nj_host_free(vm, probe);
            fail_size = last_size; root_failures = 0;
            if (encoded) {
                a[0].r = data; a[1].i = 0; a[2].i = (nj_s4)sizeof(png);
                st = nj_call_static(vm, DU, "createImage", "([BII)" IMAGE_DESC, a, &r);
            } else {
                a[0].i = 3; a[1].i = 2; a[2].i = 0x40123456;
                st = nj_call_static(vm, DU, "createImage", "(III)" IMAGE_DESC, a, &r);
            }
            fail_size = 0;
            CHECK(root_failures == 1, "root-table failure injection fired");
            expect_exception(vm, st, "java/lang/OutOfMemoryError");
            CHECK(nj_root_mark(vm) == padded, "temporary roots restored on failure");
            nj_root_release(vm, saved);
        }
    }
    nj_root_release(vm, mark);
    nj_vm_destroy(vm);
}

int main(void)
{
    nj_port port;
    nj_vm *vm;
    memset(&port, 0, sizeof(port));
    if (nj_port_headless(&port) != NJ_OK) { return 2; }
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    if (!vm) { nj_port_shutdown(&port); return 2; }
    test_constants(vm);
    test_argb(vm);
    test_encoded(vm);
    test_invalid(vm);
    CHECK(!nj_has_exception(vm), "no pending exception after ordinary cases");
    CHECK(nj_root_mark(vm) == 0, "ordinary calls restore temporary roots");
    nj_vm_destroy(vm);
    test_gc(&port, 0);
    test_gc(&port, 1);
    test_root_failure(&port);
    nj_port_shutdown(&port);
    printf("DirectUtils: %u passed, %u failed\n", passed, failed);
    return failed ? 1 : 0;
}
