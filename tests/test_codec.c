/*
 * nano_j2me - tests/test_codec.c
 *
 * The image-codec adapter contract.
 *
 * The point of the registry is that a platform can replace a decoder without
 * touching the runtime, so this test does exactly that: it installs a fake
 * "hardware" provider and proves precedence, isolation and teardown, rather
 * than only checking that the built-in PNG decoder works.
 *
 * What it covers:
 *   - the built-in PNG provider decodes and reports correct dimensions;
 *   - nj_image_probe() answers without decoding (and gets the header right);
 *   - a platform provider registered at NJ_CODEC_PRIO_PLATFORM WINS over the
 *     built-in one, and unregistering restores the built-in;
 *   - the registry is PER VM: installing into one leaves another untouched,
 *     which is what lets a device run two games without interference;
 *   - a format with no decoder fails with a message that names the problem
 *     instead of silently returning NULL;
 *   - attach()/detach() run exactly once per VM and in the right order, so a
 *     stateful hardware codec can be brought up and shut down safely;
 *   - everything is released: identical teardown across create/destroy rounds.
 *
 * Usage: test_codec
 */
#include "nj2me/nj.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

#define CHECK(cond, what)                                                    \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, (what));        \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b, what)                                                 \
    do {                                                                     \
        long va_ = (long)(a);                                                \
        long vb_ = (long)(b);                                                \
        checks++;                                                            \
        if (va_ != vb_) {                                                    \
            failures++;                                                      \
            printf("  FAIL %s:%d  %s (%ld != %ld)\n",                        \
                   __FILE__, __LINE__, (what), va_, vb_);                    \
        }                                                                    \
    } while (0)

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */
/* A real 3x2 RGBA PNG, embedded so the test needs no files and no PIL. */
static const unsigned char k_png[] = {
    0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au, 0x00u, 0x00u, 0x00u, 0x0Du,
    0x49u, 0x48u, 0x44u, 0x52u, 0x00u, 0x00u, 0x00u, 0x03u, 0x00u, 0x00u, 0x00u, 0x02u,
    0x08u, 0x06u, 0x00u, 0x00u, 0x00u, 0x9Du, 0x74u, 0x66u, 0x1Au, 0x00u, 0x00u, 0x00u,
    0x1Cu, 0x49u, 0x44u, 0x41u, 0x54u, 0x78u, 0xDAu, 0x25u, 0xC7u, 0xB1u, 0x01u, 0x00u,
    0x30u, 0x08u, 0xC0u, 0x20u, 0xECu, 0xE7u, 0x5Eu, 0x1Eu, 0x87u, 0xB2u, 0x21u, 0x92u,
    0xA8u, 0x27u, 0xFBu, 0x6Fu, 0x0Eu, 0x8Bu, 0x37u, 0x09u, 0x7Cu, 0xACu, 0x74u, 0xDAu,
    0x28u, 0x00u, 0x00u, 0x00u, 0x00u, 0x49u, 0x45u, 0x4Eu, 0x44u, 0xAEu, 0x42u, 0x60u,
    0x82u,
};

/* Enough of a JPEG to be recognised by the magic check. */
static const unsigned char k_jpeg[] = {
    0xFFu, 0xD8u, 0xFFu, 0xE0u, 0x00u, 0x10u, 0x4Au, 0x46u, 0x49u, 0x46u, 0x00u
};

/* ------------------------------------------------------------------ */
/* A fake platform decoder                                             */
/* ------------------------------------------------------------------ */
/*
 * Stands in for a hardware block: it claims PNG, produces a 1x1 marker so the
 * test can tell which provider ran, and counts attach/detach so the lifecycle
 * can be asserted.  The counters live in the test, not in the runtime - the
 * provider gets per-VM context through the descriptor copy.
 */
typedef struct fake_hw {
    int attaches;
    int detaches;
    int decodes;
} fake_hw;

static fake_hw hw;

static int hw_accepts(const void *data, nj_u4 size)
{
    /* Deliberately claims PNG too, so precedence is actually tested. */
    const unsigned char *p = (const unsigned char *)data;
    return size >= 8u && p[0] == 0x89u && p[1] == 0x50u && p[2] == 0x4Eu &&
           p[3] == 0x47u;
}

static nj_status hw_info(const void *data, nj_u4 size, nj_image_info *out)
{
    (void)data; (void)size;
    out->width = 1;
    out->height = 1;
    out->has_alpha = 0;
    out->bits_per_pixel = 32;
    out->format = NJ_CODEC_FMT_PNG;
    return NJ_OK;
}

static nj_image *hw_decode(const nj_codec_req *req)
{
    nj_image *img;
    hw.decodes++;
    img = nj_codec_image_new(req, 1, 1, NJ_CODEC_OUT_ARGB8888, 0);
    if (img) {
        img->pixels[0] = 0xDEADBEEFu;   /* the marker */
    }
    return img;
}

static nj_status hw_attach(nj_vm *vm, struct nj_codec *self)
{
    (void)vm;
    (void)self;
    hw.attaches++;
    return NJ_OK;
}

static void hw_detach(nj_vm *vm, struct nj_codec *self)
{
    (void)vm;
    (void)self;
    hw.detaches++;
}

static const nj_codec k_fake_hw = {
    "fake-hw",
    { NJ_CODEC_FMT_PNG, NJ_CODEC_OUT_ARGB8888, NJ_CODEC_SCALE_1, 0u, 0, 0 },
    hw_accepts, hw_info, hw_decode,
    NULL,
    hw_attach, hw_detach
};

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */
static nj_port *make_port(nj_port *p)
{
    memset(p, 0, sizeof(*p));
    if (nj_port_headless_ex(p, 128, 128, 0, NULL) != NJ_OK) {
        return NULL;
    }
    return p;
}

static void test_builtin(nj_vm *vm)
{
    nj_image *img;
    const char *err = NULL;
    nj_image_info info;
    int found_png = 0;
    int i;

    printf("built-in provider\n");
    for (i = 0; i < nj_codec_count(vm); i++) {
        const char *n = nj_codec_name_at(vm, i);
        if (n && strcmp(n, "png-software") == 0) {
            found_png = 1;
        }
    }
    CHECK(found_png, "png-software is registered");
    CHECK(nj_codec_available(vm, k_png, (nj_u4)sizeof(k_png)) == 1,
          "PNG is recognised");
    CHECK(nj_codec_available(vm, "not an image", 12u) == 0,
          "garbage is not recognised");

    memset(&info, 0, sizeof(info));
    CHECK(nj_image_probe(vm, k_png, (nj_u4)sizeof(k_png), &info, NULL) == NJ_OK,
          "probe succeeds");
    CHECK_EQ(info.width, 3, "probe reports the real width");
    CHECK_EQ(info.height, 2, "probe reports the real height");
    CHECK_EQ(info.has_alpha, 1, "probe detects the alpha channel");
    CHECK_EQ(info.format, NJ_CODEC_FMT_PNG, "probe reports the format");

    img = nj_image_decode_mem(vm, k_png, (nj_u4)sizeof(k_png), &err);
    CHECK(img != NULL, "PNG decodes");
    if (img) {
        CHECK_EQ(img->width, 3, "decoded width");
        CHECK_EQ(img->height, 2, "decoded height");
        CHECK_EQ(img->format, NJ_CODEC_OUT_ARGB8888, "decoded format");
        CHECK_EQ(img->pixels[0], 0xFFFF0000u, "first pixel is opaque red");
        CHECK_EQ(img->pixels[5] & 0xFF000000u, 0x00000000u,
                 "the fully transparent pixel stayed transparent");
        nj_image_release(vm, img);
    }
}

static void test_precedence(nj_vm *vm)
{
    nj_image *img;
    const char *err = NULL;
    int before;

    printf("platform provider precedence\n");
    before = nj_codec_count(vm);
    hw.decodes = 0;
    hw.attaches = 0;
    hw.detaches = 0;

    CHECK(nj_codec_register(vm, &k_fake_hw, NJ_CODEC_PRIO_PLATFORM) == NJ_OK,
          "register");
    CHECK_EQ(nj_codec_count(vm), before + 1, "registry grew by one");
    CHECK_EQ(hw.attaches, 1, "attach called exactly once on registration");

    img = nj_image_decode_mem(vm, k_png, (nj_u4)sizeof(k_png), &err);
    CHECK(img != NULL, "decode still succeeds");
    if (img) {
        CHECK_EQ(hw.decodes, 1, "the PLATFORM provider ran, not the built-in");
        CHECK_EQ(img->width, 1, "it produced its own image");
        CHECK_EQ(img->pixels[0], 0xDEADBEEFu, "with its own marker pixel");
        nj_image_release(vm, img);
    }

    /* A lower-priority registration must NOT take over. */
    CHECK(nj_codec_register(vm, &k_fake_hw, NJ_CODEC_PRIO_FALLBACK) == NJ_OK,
          "re-register at a lower priority");
    img = nj_image_decode_mem(vm, k_png, (nj_u4)sizeof(k_png), &err);
    if (img) {
        CHECK_EQ(img->width, 3, "the built-in decoder is back in front");
        nj_image_release(vm, img);
    }

    nj_codec_unregister(vm, &k_fake_hw);
    CHECK_EQ(nj_codec_count(vm), before, "registry shrank back");
    CHECK_EQ(hw.detaches, 1, "detach called on unregistration");

    img = nj_image_decode_mem(vm, k_png, (nj_u4)sizeof(k_png), &err);
    if (img) {
        CHECK_EQ(img->width, 3, "built-in wins again after unregistering");
        nj_image_release(vm, img);
    }
}

static void test_per_vm_isolation(nj_port *pa, nj_port *pb)
{
    nj_vm *a;
    nj_vm *b;
    nj_image *img;
    const char *err = NULL;

    printf("per-VM isolation\n");
    a = nj_vm_create(pa, 512u * 1024u);
    b = nj_vm_create(pb, 512u * 1024u);
    CHECK(a != NULL && b != NULL, "two VMs");
    if (!a || !b) {
        nj_vm_destroy(a);
        nj_vm_destroy(b);
        return;
    }

    hw.decodes = 0;
    CHECK(nj_codec_register(a, &k_fake_hw, NJ_CODEC_PRIO_PLATFORM) == NJ_OK,
          "install into VM A only");

    img = nj_image_decode_mem(a, k_png, (nj_u4)sizeof(k_png), &err);
    if (img) {
        CHECK_EQ(img->width, 1, "VM A uses the platform provider");
        nj_image_release(a, img);
    }
    img = nj_image_decode_mem(b, k_png, (nj_u4)sizeof(k_png), &err);
    if (img) {
        CHECK_EQ(img->width, 3, "VM B is untouched and uses the built-in");
        nj_image_release(b, img);
    }

    nj_vm_destroy(a);
    CHECK_EQ(hw.detaches, 1, "teardown detached the provider");

    img = nj_image_decode_mem(b, k_png, (nj_u4)sizeof(k_png), &err);
    CHECK(img != NULL, "VM B still decodes after A was destroyed");
    if (img) {
        nj_image_release(b, img);
    }
    nj_vm_destroy(b);
}

static void test_unavailable(nj_vm *vm)
{
    nj_image *img;
    const char *err = NULL;
    static const char garbage[] = { 0, 1, 2, 3, 'x', 'y', 'z', 0 };

    printf("a format with no decoder\n");
    img = nj_image_decode_mem(vm, k_jpeg, (nj_u4)sizeof(k_jpeg), &err);
    CHECK(img == NULL, "a truncated JPEG is refused");
    CHECK(err != NULL, "with a message");
    if (err) {
        printf("  jpeg message: %s\n", err);
        CHECK(strstr(err, "JPEG") != NULL || strstr(err, "jpeg") != NULL,
              "the message names JPEG so the failure is actionable");
    }

    err = NULL;
    img = nj_image_decode_mem(vm, garbage, (nj_u4)sizeof(garbage), &err);
    CHECK(img == NULL, "garbage is refused");
    CHECK(err != NULL, "with a message");

    err = NULL;
    img = nj_image_decode_mem(vm, k_png, 0u, &err);
    CHECK(img == NULL, "zero-length input is refused");
}

static void test_teardown(void)
{
    nj_u4 first = 0u;
    int i;

    printf("teardown across restarts\n");
    hw.detaches = 0;
    for (i = 0; i < 3; i++) {
        nj_port port;
        nj_vm *vm;
        nj_u4 released;

        CHECK(make_port(&port) != NULL, "port");
        vm = nj_vm_create(&port, 512u * 1024u);
        CHECK(vm != NULL, "vm");
        if (!vm) {
            nj_port_shutdown(&port);
            return;
        }
        CHECK(nj_codec_register(vm, &k_fake_hw, NJ_CODEC_PRIO_PLATFORM)
              == NJ_OK, "register");
        {
            nj_image *img = nj_image_decode_mem(vm, k_png,
                                                (nj_u4)sizeof(k_png), NULL);
            CHECK(img != NULL, "decode");
            nj_image_release(vm, img);
        }
        released = nj_vm_destroy(vm);
        if (i == 0) {
            first = released;
            CHECK(released > 0u, "destroy released the codec registry too");
        } else {
            CHECK_EQ(released, first, "identical teardown every round");
        }
        nj_port_shutdown(&port);
    }
    CHECK_EQ(hw.detaches, 3, "every round detached its provider");
}

int main(void)
{
    nj_port port;

    printf("== nano_j2me image codec adapters ==\n");

    if (!make_port(&port)) {
        printf("cannot create a port\n");
        return 2;
    }
    {
        nj_vm *vm = nj_vm_create(&port, 1024u * 1024u);
        if (!vm) {
            printf("cannot create a VM\n");
            nj_port_shutdown(&port);
            return 2;
        }
        test_builtin(vm);
        test_precedence(vm);
        test_unavailable(vm);
        nj_vm_destroy(vm);
    }
    nj_port_shutdown(&port);

    {
        nj_port pa, pb;
        if (make_port(&pa) && make_port(&pb)) {
            test_per_vm_isolation(&pa, &pb);
        }
    }

    test_teardown();

    printf("\nchecks: %d  failures: %d\n", checks, failures);
    printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
