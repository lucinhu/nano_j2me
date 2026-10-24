/*
 * nano_j2me - tests/test_lcdui.c
 *
 * The LCDUI module's own test.  It builds a headless VM *without a MIDlet* and
 * drives javax.microedition.lcdui entirely through the C API the class library
 * exposes to other modules (nj_object_new / nj_call / nj_call_static /
 * nj_field_*), which is the same surface the game package, the MIDlet
 * lifecycle and the runner use.
 *
 * What it proves, in order:
 *
 *   1. Image      - createImage(w,h) is mutable and round-trips RGB;
 *                   createImage(byte[],0,len) decodes a real PNG; sub-image
 *                   copies and createRGBImage obey their contracts.
 *   2. Graphics   - every transform field is exercised: setColor/fillRect and
 *                   read-back of the exact ARGB value, clipping, translate,
 *                   drawString advance == Font.stringWidth, drawRegion,
 *                   copyArea, XOR, blending, drawRGB.
 *   3. Font       - interning (same object for equal parameters), metric
 *                   consistency between charWidth/charsWidth/stringWidth/
 *                   substringWidth, ascent+descent == height.
 *   4. Widgets    - Command ordering, List/Form/TextBox/Alert/ChoiceGroup/
 *                   Gauge/DateField/Spinner accessors round-trip.
 *   5. The screen - Display.setCurrent() + nj_midp_paint() renders through the
 *                   built-in widget renderer into the VM framebuffer, and a
 *                   Graphics bound to a real Image produces the exact pixels a
 *                   paint() override would have produced.
 *   6. Errors     - the right exception classes come out.
 *   7. Lifecycle  - create/run/destroy three times, same host-block count every
 *                   round: the project's core memory guarantee.
 *
 * Usage:  test_lcdui [fixtures-dir]
 * Exit code 0 on PASS, non-zero otherwise.
 */
#include "nj2me/nj.h"
#include "nj_internal.h"   /* nj_class_create_builtin: the fixture Runnable */
#include "nj2me/nj_gfx.h"
#include "nj2me/nj_png.h"
#include "nj2me/nj_codec.h"
#include "../src/midp/nj_midp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Descriptors the widgets are addressed by. */
#define D_CMD     "Ljavax/microedition/lcdui/Command;"
#define D_IMAGE   "Ljavax/microedition/lcdui/Image;"
#define D_ALERTTYPE "Ljavax/microedition/lcdui/AlertType;"
#define D_DISPLAY "Ljavax/microedition/lcdui/Display;"

/* Read a static reference field (SELECT_COMMAND, AlertType.WARNING, ...). */
static nj_ref static_ref(nj_vm *vm, const char *cls, const char *name,
                         const char *desc)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 1);
    return f ? nj_field_read_ref(vm, NULL, f) : NULL;
}

/* ------------------------------------------------------------------ */
/* Assertion machinery                                                 */
/* ------------------------------------------------------------------ */
static int g_pass;
static int g_fail;
static const char *g_fixtures = "tests/fixtures";

#define OK(cond)                                                              \
    do {                                                                      \
        if (cond) {                                                           \
            g_pass++;                                                         \
        } else {                                                              \
            g_fail++;                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                     \
    } while (0)

#define OK_MSG(cond, ...)                                                     \
    do {                                                                      \
        if (cond) {                                                           \
            g_pass++;                                                         \
        } else {                                                              \
            g_fail++;                                                         \
            fprintf(stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);   \
            fprintf(stderr, __VA_ARGS__);                                     \
            fputc('\n', stderr);                                              \
        }                                                                     \
    } while (0)

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */
typedef struct blob {
    unsigned char *data;
    unsigned len;
} blob;

static int read_file(const char *path, blob *out)
{
    FILE *f = fopen(path, "rb");
    long n;

    out->data = NULL;
    out->len = 0u;
    if (!f) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    n = ftell(f);
    if (n <= 0) {
        fclose(f);
        return 0;
    }
    rewind(f);
    out->data = (unsigned char *)malloc((size_t)n);
    if (!out->data) {
        fclose(f);
        return 0;
    }
    if (fread(out->data, 1u, (size_t)n, f) != (size_t)n) {
        free(out->data);
        out->data = NULL;
        fclose(f);
        return 0;
    }
    fclose(f);
    out->len = (unsigned)n;
    return 1;
}

/* Build a String argument value. */
static nj_value sref(nj_ref s)
{
    nj_value v;
    v.r = s;
    return v;
}

static nj_value iref(nj_s4 i)
{
    nj_value v;
    v.bits = 0u;
    v.i = i;
    return v;
}

/* Call a public API method and return its int result. */
static nj_s4 call_int(nj_vm *vm, nj_ref obj, const char *cls, const char *name,
                      const char *desc, nj_value *args, int *ok)
{
    nj_value ret;
    ret.bits = 0u;
    if (nj_call(vm, obj, cls, name, desc, args, &ret) != NJ_OK) {
        if (ok) { *ok = 0; }
        return 0;
    }
    if (ok) { *ok = 1; }
    return ret.i;
}

static nj_ref call_ref(nj_vm *vm, nj_ref obj, const char *cls, const char *name,
                       const char *desc, nj_value *args, int *ok)
{
    nj_value ret;
    ret.bits = 0u;
    if (nj_call(vm, obj, cls, name, desc, args, &ret) != NJ_OK) {
        if (ok) { *ok = 0; }
        return NULL;
    }
    if (ok) { *ok = 1; }
    return ret.r;
}

static nj_s4 call_static_int(nj_vm *vm, const char *cls, const char *name,
                             const char *desc, nj_value *args, int *ok)
{
    nj_value ret;
    ret.bits = 0u;
    if (nj_call_static(vm, cls, name, desc, args, &ret) != NJ_OK) {
        if (ok) { *ok = 0; }
        return 0;
    }
    if (ok) { *ok = 1; }
    return ret.i;
}

static nj_ref call_static_ref(nj_vm *vm, const char *cls, const char *name,
                              const char *desc, nj_value *args, int *ok)
{
    nj_value ret;
    ret.bits = 0u;
    if (nj_call_static(vm, cls, name, desc, args, &ret) != NJ_OK) {
        if (ok) { *ok = 0; }
        return NULL;
    }
    if (ok) { *ok = 1; }
    return ret.r;
}

/* A String from a C literal. */
static nj_ref str(nj_vm *vm, const char *s)
{
    return nj_string_new(vm, s);
}

/* Construct an object through its <init>, returning NULL on failure. */
static nj_ref construct(nj_vm *vm, const char *cls, const char *desc,
                        nj_value *args)
{
    nj_ref o = nj_object_new(vm, cls);
    if (!o) {
        return NULL;
    }
    if (nj_call_special(vm, o, cls, "<init>", desc, args, NULL) != NJ_OK) {
        return NULL;
    }
    return o;
}

/* The exception class name currently pending, or NULL. */
static const char *pending_exc(nj_vm *vm)
{
    nj_ref e = vm->exception;
    if (!e || !e->cls) {
        return NULL;
    }
    return e->cls->name;
}

static int exc_is(nj_vm *vm, const char *cls)
{
    const char *n = pending_exc(vm);
    if (!n) {
        return 0;
    }
    nj_clear_exception(vm);
    return strcmp(n, cls) == 0;
}

/* ------------------------------------------------------------------ */
/* Image                                                               */
/* ------------------------------------------------------------------ */
static nj_ref image_new(nj_vm *vm, nj_s4 w, nj_s4 h)
{
    nj_value a[2];
    a[0] = iref(w);
    a[1] = iref(h);
    return call_static_ref(vm, NJ_C_IMAGE, "createImage", "(II)L" NJ_C_IMAGE ";",
                           a, NULL);
}

static void test_image(nj_vm *vm, const blob *png, const blob *png_alpha)
{
    nj_ref img;
    nj_ref g;
    nj_value a[8];
    int ok = 0;

    /* ---- createImage(w,h) ---- */
    img = image_new(vm, 16, 8);
    OK(img != NULL);
    if (!img) {
        return;
    }
    OK(call_int(vm, img, NJ_C_IMAGE, "getWidth", "()I", NULL, &ok) == 16 && ok);
    OK(call_int(vm, img, NJ_C_IMAGE, "getHeight", "()I", NULL, &ok) == 8 && ok);
    OK(call_int(vm, img, NJ_C_IMAGE, "isMutable", "()Z", NULL, &ok) == 1 && ok);

    /* A fresh image is transparent black. */
    {
        nj_ref rgb = nj_int_array_new(vm, 16u * 8u);
        nj_u4 first;
        OK(rgb != NULL);
        a[0] = sref(rgb);
        a[1] = iref(0);
        a[2] = iref(16);
        a[3] = iref(0);
        a[4] = iref(0);
        a[5] = iref(16);
        a[6] = iref(8);
        OK(nj_call(vm, img, NJ_C_IMAGE, "getRGB", "([IIIIIII)V", a, NULL) == NJ_OK);
        first = (nj_u4)nj_array_get_int(rgb, 0u);
        /* MIDP 2.0, Image.createImage(int,int): "Every pixel within the newly
         * created image is white."  A mutable image therefore starts opaque
         * white, not transparent black. */
        OK_MSG(first == 0xFFFFFFFFu, "expected 0xFFFFFFFF, got 0x%08X", first);
    }

    /*
     * A pixel count that does not fit in a Java array length is refused
     * before the multiply wraps: 65536 x 65536 used to plan a zero-length
     * pixel array and then fill 2^32 pixels past it.
     */
    OK(image_new(vm, 65536, 65536) == NULL);
    OK(exc_is(vm, "java/lang/OutOfMemoryError"));
    nj_clear_exception(vm);

    /* ---- getGraphics + getRGB round-trip ---- */
    g = call_ref(vm, img, NJ_C_IMAGE, "getGraphics", "()L" NJ_C_GRAPHICS ";",
                 NULL, &ok);
    OK(g != NULL && ok);
    if (g) {
        /* setColor(int) then fillRect: the exact ARGB value must come back. */
        a[0] = iref((nj_s4)0xFF3366CCu);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL) == NJ_OK);
        OK(call_int(vm, g, NJ_C_GRAPHICS, "getColor", "()I", NULL, &ok) ==
           (nj_s4)0x003366CCu && ok);
        a[0] = iref(2);
        a[1] = iref(1);
        a[2] = iref(5);
        a[3] = iref(3);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "fillRect", "(IIII)V", a, NULL) == NJ_OK);
        {
            nj_ref px = nj_int_array_new(vm, 1u);
            nj_value ga[7];
            ga[0] = sref(px);
            ga[1] = iref(0);
            ga[2] = iref(1);
            ga[3] = iref(3);
            ga[4] = iref(2);
            ga[5] = iref(1);
            ga[6] = iref(1);
            OK(nj_call(vm, img, NJ_C_IMAGE, "getRGB", "([IIIIIII)V", ga,
                       NULL) == NJ_OK);
            OK_MSG((nj_u4)nj_array_get_int(px, 0u) == 0xFF3366CCu,
                   "fillRect pixel = 0x%08X",
                   (unsigned)nj_array_get_int(px, 0u));
        }
    }

    /* ---- immutable images have no Graphics ---- */
    {
        nj_value ba[3];
        nj_ref barr = nj_byte_array_new(vm, (nj_u4)png->len);
        nj_ref dec;
        OK(barr != NULL);
        memcpy(barr->u.bytes, png->data, png->len);
        ba[0] = sref(barr);
        ba[1] = iref(0);
        ba[2] = iref((nj_s4)png->len);
        dec = call_static_ref(vm, NJ_C_IMAGE, "createImage",
                              "([BII)L" NJ_C_IMAGE ";", ba, &ok);
        OK(dec != NULL && ok);
        if (dec) {
            OK(call_int(vm, dec, NJ_C_IMAGE, "isMutable", "()Z", NULL, &ok) == 0 && ok);
            OK(call_int(vm, dec, NJ_C_IMAGE, "getWidth", "()I", NULL, &ok) == 13 && ok);
            OK(call_int(vm, dec, NJ_C_IMAGE, "getHeight", "()I", NULL, &ok) == 7 && ok);
            (void)call_ref(vm, dec, NJ_C_IMAGE, "getGraphics",
                           "()L" NJ_C_GRAPHICS ";", NULL, NULL);
            OK_MSG(exc_is(vm, "java/lang/IllegalStateException"),
                   "expected IllegalStateException, got %s",
                   pending_exc(vm) ? pending_exc(vm) : "(none)");
        }
    }

    /* ---- PNG decode: the pixels must match what PIL wrote ---- */
    {
        nj_value ba[3];
        nj_ref barr = nj_byte_array_new(vm, (nj_u4)png->len);
        nj_ref dec;
        nj_ref px;
        nj_value ga[7];
        static const nj_u4 expect[7] = {
            0xFF000000u,   /* (0,0)  on the black diagonal */
            0xFF00FF00u,   /* (12,0) top-right green       */
            0xFF0000FFu,   /* (0,6)  bottom-left blue      */
            0xFFFFFFFFu,   /* (12,6) bottom-right white    */
            0xFF000000u,   /* (3,3)  on the black diagonal */
            0xFF00FF00u,   /* (6,2)  top-right green       */
            0xFFFFFFFFu    /* (6,5)  bottom-right white    */
        };
        static const int px_x[7] = { 0, 12, 0, 12, 3, 6, 6 };
        static const int px_y[7] = { 0, 0, 6, 6, 3, 2, 5 };
        int i;

        memcpy(barr->u.bytes, png->data, png->len);
        ba[0] = sref(barr);
        ba[1] = iref(0);
        ba[2] = iref((nj_s4)png->len);
        dec = call_static_ref(vm, NJ_C_IMAGE, "createImage",
                              "([BII)L" NJ_C_IMAGE ";", ba, &ok);
        OK_MSG(dec != NULL && ok, "PNG decode failed: %s",
               pending_exc(vm) ? pending_exc(vm) : "(no exception)");
        if (!dec) {
            return;
        }
        px = nj_int_array_new(vm, 13u * 7u);
        ga[0] = sref(px);
        ga[1] = iref(0);
        ga[2] = iref(13);
        ga[3] = iref(0);
        ga[4] = iref(0);
        ga[5] = iref(13);
        ga[6] = iref(7);
        OK(nj_call(vm, dec, NJ_C_IMAGE, "getRGB", "([IIIIIII)V", ga, NULL) == NJ_OK);
        for (i = 0; i < 7; i++) {
            nj_u4 got = (nj_u4)nj_array_get_int(px, (nj_u4)(px_y[i] * 13 + px_x[i]));
            OK_MSG(got == expect[i],
                   "PNG pixel (%d,%d) = 0x%08X, expected 0x%08X",
                   px_x[i], px_y[i], got, expect[i]);
        }
    }

    /* ---- PNG with alpha keeps its alpha channel ---- */
    if (png_alpha && png_alpha->len > 0u) {
        nj_value ba[3];
        nj_ref barr = nj_byte_array_new(vm, (nj_u4)png_alpha->len);
        nj_ref dec;
        memcpy(barr->u.bytes, png_alpha->data, png_alpha->len);
        ba[0] = sref(barr);
        ba[1] = iref(0);
        ba[2] = iref((nj_s4)png_alpha->len);
        dec = call_static_ref(vm, NJ_C_IMAGE, "createImage",
                              "([BII)L" NJ_C_IMAGE ";", ba, &ok);
        OK_MSG(dec != NULL && ok, "alpha PNG decode failed");
        if (dec) {
            nj_ref px = nj_int_array_new(vm, 4u);
            nj_value ga[7];
            ga[0] = sref(px);
            ga[1] = iref(0);
            ga[2] = iref(4);
            ga[3] = iref(0);
            ga[4] = iref(0);
            ga[5] = iref(4);
            ga[6] = iref(1);
            OK(nj_call(vm, dec, NJ_C_IMAGE, "getRGB", "([IIIIIII)V", ga,
                       NULL) == NJ_OK);
            /* Even pixels are 50% red, odd pixels fully transparent. */
            OK_MSG((((nj_u4)nj_array_get_int(px, 0u)) >> 24) < 255u,
                   "expected partial alpha, got 0x%08X",
                   (unsigned)nj_array_get_int(px, 0u));
            OK_MSG((((nj_u4)nj_array_get_int(px, 1u)) >> 24) == 0u,
                   "expected transparent, got 0x%08X",
                   (unsigned)nj_array_get_int(px, 1u));
        }
    }

    /* ---- createImage(Image, x, y, w, h, transform) ---- */
    {
        nj_value ca[6];
        nj_ref sub;
        nj_ref px = nj_int_array_new(vm, 6u);
        nj_value ga[7];
        ca[0] = sref(img);
        ca[1] = iref(2);
        ca[2] = iref(1);
        ca[3] = iref(3);
        ca[4] = iref(2);
        ca[5] = iref(0);
        sub = call_static_ref(vm, NJ_C_IMAGE, "createImage",
                              "(L" NJ_C_IMAGE ";IIIII)L" NJ_C_IMAGE ";",
                              ca, &ok);
        OK(sub != NULL && ok);
        if (sub) {
            OK(call_int(vm, sub, NJ_C_IMAGE, "getWidth", "()I", NULL, &ok) == 3 && ok);
            OK(call_int(vm, sub, NJ_C_IMAGE, "getHeight", "()I", NULL, &ok) == 2 && ok);
            ga[0] = sref(px);
            ga[1] = iref(0);
            ga[2] = iref(3);
            ga[3] = iref(0);
            ga[4] = iref(0);
            ga[5] = iref(3);
            ga[6] = iref(2);
            OK(nj_call(vm, sub, NJ_C_IMAGE, "getRGB", "([IIIIIII)V", ga,
                       NULL) == NJ_OK);
            OK_MSG((nj_u4)nj_array_get_int(px, 0u) == 0xFF3366CCu,
                   "sub-image pixel = 0x%08X",
                   (unsigned)nj_array_get_int(px, 0u));
        }
    }

    /* ---- createRGBImage: 0x00RRGGBB in, opaque ARGB out ---- */
    {
        nj_value ca[4];
        nj_ref rgb = nj_int_array_new(vm, 4u);
        nj_ref out;
        nj_ref px = nj_int_array_new(vm, 4u);
        nj_value ga[7];
        nj_array_set_int(rgb, 0u, (nj_s4)0x00010203u);
        nj_array_set_int(rgb, 1u, (nj_s4)0x00AABBCCu);
        nj_array_set_int(rgb, 2u, (nj_s4)0x00FFFFFFu);
        nj_array_set_int(rgb, 3u, (nj_s4)0x00000000u);
        ca[0] = sref(rgb);
        ca[1] = iref(2);
        ca[2] = iref(2);
        ca[3] = iref(0);
        out = call_static_ref(vm, NJ_C_IMAGE, "createRGBImage", "([IIIZ)L" NJ_C_IMAGE ";",
                              ca, &ok);
        OK(out != NULL && ok);
        if (out) {
            ga[0] = sref(px);
            ga[1] = iref(0);
            ga[2] = iref(2);
            ga[3] = iref(0);
            ga[4] = iref(0);
            ga[5] = iref(2);
            ga[6] = iref(2);
            OK(nj_call(vm, out, NJ_C_IMAGE, "getRGB", "([IIIIIII)V", ga,
                       NULL) == NJ_OK);
            OK_MSG((nj_u4)nj_array_get_int(px, 0u) == 0xFF010203u,
                   "rgb[0] = 0x%08X", (unsigned)nj_array_get_int(px, 0u));
            OK_MSG((nj_u4)nj_array_get_int(px, 3u) == 0xFF000000u,
                   "rgb[3] = 0x%08X", (unsigned)nj_array_get_int(px, 3u));
        }
    }

    /* ---- error cases ---- */
    (void)image_new(vm, 0, 4);
    OK_MSG(exc_is(vm, "java/lang/IllegalArgumentException"),
           "createImage(0,4) did not throw");
    (void)image_new(vm, -3, 4);
    OK_MSG(exc_is(vm, "java/lang/IllegalArgumentException"),
           "createImage(-3,4) did not throw");
}

/* ------------------------------------------------------------------ */
/* Graphics                                                            */
/* ------------------------------------------------------------------ */
static nj_u4 img_pixel(nj_vm *vm, nj_ref img, int x, int y)
{
    nj_ref px = nj_int_array_new(vm, 1u);
    nj_value ga[7];
    nj_u4 v;

    ga[0] = sref(px);
    ga[1] = iref(0);
    ga[2] = iref(1);
    ga[3] = iref(x);
    ga[4] = iref(y);
    ga[5] = iref(1);
    ga[6] = iref(1);
    if (nj_call(vm, img, NJ_C_IMAGE, "getRGB", "([IIIIIII)V", ga, NULL) != NJ_OK) {
        return 0xDEADBEEFu;
    }
    v = (nj_u4)nj_array_get_int(px, 0u);
    return v;
}

static void test_graphics(nj_vm *vm)
{
    nj_ref img = image_new(vm, 24, 16);
    nj_ref g;
    nj_value a[8];
    int ok = 0;

    OK(img != NULL);
    if (!img) {
        return;
    }
    g = call_ref(vm, img, NJ_C_IMAGE, "getGraphics", "()L" NJ_C_GRAPHICS ";",
                 NULL, &ok);
    OK(g != NULL && ok);
    if (!g) {
        return;
    }

    /* ---- default clip covers the whole target ---- */
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getClipX", "()I", NULL, &ok) == 0 && ok);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getClipY", "()I", NULL, &ok) == 0 && ok);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getClipWidth", "()I", NULL, &ok) == 24 && ok);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getClipHeight", "()I", NULL, &ok) == 16 && ok);

    /* ---- translate ---- */
    a[0] = iref(4);
    a[1] = iref(3);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "translate", "(II)V", a, NULL) == NJ_OK);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getTranslateX", "()I", NULL, &ok) == 4 && ok);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getTranslateY", "()I", NULL, &ok) == 3 && ok);
    a[0] = iref((nj_s4)0xFFFF0000u);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL) == NJ_OK);
    a[0] = iref(0);
    a[1] = iref(0);
    a[2] = iref(1);
    a[3] = iref(1);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "fillRect", "(IIII)V", a, NULL) == NJ_OK);
    OK_MSG(img_pixel(vm, img, 4, 3) == 0xFFFF0000u,
           "translate: (4,3) = 0x%08X", img_pixel(vm, img, 4, 3));
    OK_MSG(img_pixel(vm, img, 0, 0) != 0xFFFF0000u,
           "translate: (0,0) must stay untouched");
    a[0] = iref(-4);
    a[1] = iref(-3);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "translate", "(II)V", a, NULL) == NJ_OK);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getTranslateX", "()I", NULL, &ok) == 0 && ok);

    /* ---- setClip really clips ---- */
    a[0] = iref(5);
    a[1] = iref(5);
    a[2] = iref(4);
    a[3] = iref(4);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "setClip", "(IIII)V", a, NULL) == NJ_OK);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getClipX", "()I", NULL, &ok) == 5 && ok);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getClipWidth", "()I", NULL, &ok) == 4 && ok);
    a[0] = iref((nj_s4)0xFF00FF00u);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL) == NJ_OK);
    a[0] = iref(0);
    a[1] = iref(0);
    a[2] = iref(24);
    a[3] = iref(16);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "fillRect", "(IIII)V", a, NULL) == NJ_OK);
    OK_MSG(img_pixel(vm, img, 6, 6) == 0xFF00FF00u,
           "clipped fill inside = 0x%08X", img_pixel(vm, img, 6, 6));
    OK_MSG(img_pixel(vm, img, 10, 6) != 0xFF00FF00u,
           "clipped fill leaked at x=10");
    OK_MSG(img_pixel(vm, img, 6, 10) != 0xFF00FF00u,
           "clipped fill leaked at y=10");
    OK_MSG(img_pixel(vm, img, 3, 3) != 0xFF00FF00u,
           "clipped fill leaked at (3,3)");

    /* ---- clipRect intersects instead of replacing ---- */
    a[0] = iref(6);
    a[1] = iref(6);
    a[2] = iref(2);
    a[3] = iref(2);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "clipRect", "(IIII)V", a, NULL) == NJ_OK);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getClipX", "()I", NULL, &ok) == 6 && ok);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getClipWidth", "()I", NULL, &ok) == 2 && ok);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getClipHeight", "()I", NULL, &ok) == 2 && ok);

    /* ---- colour components ---- */
    a[0] = iref(0x11);
    a[1] = iref(0x22);
    a[2] = iref(0x33);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(III)V", a, NULL) == NJ_OK);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getRedComponent", "()I", NULL, &ok) == 0x11 && ok);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getGreenComponent", "()I", NULL, &ok) == 0x22 && ok);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getBlueComponent", "()I", NULL, &ok) == 0x33 && ok);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getAlphaComponent", "()I", NULL, &ok) == 255 && ok);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getAlpha", "()I", NULL, &ok) == 255 && ok);
    a[0] = iref(128);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "setAlpha", "(I)V", a, NULL) == NJ_OK);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getAlpha", "()I", NULL, &ok) == 128 && ok);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getAlphaComponent", "()I", NULL, &ok) == 128 && ok);
    a[0] = iref(200);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "setGrayScale", "(I)V", a, NULL) == NJ_OK);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getGrayScale", "()I", NULL, &ok) == 200 && ok);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getRedComponent", "()I", NULL, &ok) == 200 && ok);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getGreenComponent", "()I", NULL, &ok) == 200 && ok);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getBlueComponent", "()I", NULL, &ok) == 200 && ok);

    /* ---- stroke / blend / xor state ---- */
    a[0] = iref(0);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "setBlendMode", "(I)V", a, NULL) == NJ_OK);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getBlendMode", "()I", NULL, &ok) == 0 && ok);
    a[0] = iref(1);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "setBlendMode", "(I)V", a, NULL) == NJ_OK);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getBlendMode", "()I", NULL, &ok) == 1 && ok);
    a[0] = iref(1);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "setStrokeStyle", "(I)V", a, NULL) == NJ_OK);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getStrokeStyle", "()I", NULL, &ok) == 1 && ok);
    a[0] = iref(0);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "setStrokeStyle", "(I)V", a, NULL) == NJ_OK);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getStrokeStyle", "()I", NULL, &ok) == 0 && ok);
    a[0] = iref(7);
    (void)nj_call(vm, g, NJ_C_GRAPHICS, "setStrokeStyle", "(I)V", a, NULL);
    OK_MSG(exc_is(vm, "java/lang/IllegalArgumentException"),
           "setStrokeStyle(7) did not throw");

    /* Restore an unclipped target for the drawing tests. */
    a[0] = iref(0);
    a[1] = iref(0);
    a[2] = iref(24);
    a[3] = iref(16);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "setClip", "(IIII)V", a, NULL) == NJ_OK);
    a[0] = iref(0);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "setBlendMode", "(I)V", a, NULL) == NJ_OK);

    /* ---- drawLine / drawRect ---- */
    a[0] = iref((nj_s4)0xFF0000FFu);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL) == NJ_OK);
    a[0] = iref(0);
    a[1] = iref(0);
    a[2] = iref(5);
    a[3] = iref(0);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawLine", "(IIII)V", a, NULL) == NJ_OK);
    OK(img_pixel(vm, img, 3, 0) == 0xFF0000FFu);
    a[0] = iref(10);
    a[1] = iref(10);
    a[2] = iref(6);
    a[3] = iref(5);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawRect", "(IIII)V", a, NULL) == NJ_OK);
    OK_MSG(img_pixel(vm, img, 10, 10) == 0xFF0000FFu, "drawRect outline");
    OK_MSG(img_pixel(vm, img, 12, 12) != 0xFF0000FFu, "drawRect must not fill");

    /* ---- drawRoundRect / arcs / triangles ---- */
    a[0] = iref(0);
    a[1] = iref(0);
    a[2] = iref(8);
    a[3] = iref(6);
    a[4] = iref(2);
    a[5] = iref(2);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawRoundRect", "(IIIIII)V", a, NULL) == NJ_OK);
    a[0] = iref((nj_s4)0xFF00FFFFu);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL) == NJ_OK);
    a[0] = iref(0);
    a[1] = iref(0);
    a[2] = iref(8);
    a[3] = iref(6);
    a[4] = iref(2);
    a[5] = iref(2);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "fillRoundRect", "(IIIIII)V", a, NULL) == NJ_OK);
    OK_MSG(img_pixel(vm, img, 4, 3) == 0xFF00FFFFu, "fillRoundRect = 0x%08X",
           img_pixel(vm, img, 4, 3));
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawArc", "(IIIIII)V", a, NULL) == NJ_OK);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "fillArc", "(IIIIII)V", a, NULL) == NJ_OK);
    a[0] = iref((nj_s4)0xFFFF00FFu);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL) == NJ_OK);
    a[0] = iref(14);
    a[1] = iref(12);
    a[2] = iref(20);
    a[3] = iref(12);
    a[4] = iref(17);
    a[5] = iref(15);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawTriangle", "(IIIIII)V", a, NULL) == NJ_OK);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "fillTriangle", "(IIIIII)V", a, NULL) == NJ_OK);
    OK_MSG(img_pixel(vm, img, 17, 14) == 0xFFFF00FFu,
           "fillTriangle centroid = 0x%08X", img_pixel(vm, img, 17, 14));

    /* ---- drawString advances by Font.stringWidth ---- */
    {
        nj_ref f = call_static_ref(vm, NJ_C_FONT, "getFont",
                                   "(III)L" NJ_C_FONT ";",
                                   (nj_value[]){ iref(0), iref(0), iref(0) },
                                   &ok);
        nj_ref s = str(vm, "Hello");
        nj_s4 w;
        OK(f != NULL && ok);
        if (f) {
            a[0] = sref(f);
            OK(nj_call(vm, g, NJ_C_GRAPHICS, "setFont", "(L" NJ_C_FONT ";)V",
                       a, NULL) == NJ_OK);
            OK(call_ref(vm, g, NJ_C_GRAPHICS, "getFont", "()L" NJ_C_FONT ";",
                        NULL, &ok) == f && ok);
        }
        a[0] = sref(s);
        w = call_int(vm, f, NJ_C_FONT, "stringWidth", "(Ljava/lang/String;)I",
                     a, &ok);
        OK(w > 0 && ok);
        /* Clear a strip, draw the string, then measure where the ink stopped. */
        a[0] = iref(0);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL) == NJ_OK);
        a[0] = iref(0);
        a[1] = iref(0);
        a[2] = iref(24);
        a[3] = iref(8);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "fillRect", "(IIII)V", a, NULL) == NJ_OK);
        a[0] = iref((nj_s4)0xFFFFFFFFu);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL) == NJ_OK);
        a[0] = sref(s);
        a[1] = iref(0);
        a[2] = iref(0);
        /* Graphics.TOP | Graphics.LEFT = 16 | 4 = 20, which is also the
         * commonest anchor in the wild (11342 of 34080 call sites across the
         * corpus).  The runtime's NJ_ANCHOR_* bits carry the same numbers on
         * purpose, so this literal is both the MIDlet's value and the
         * renderer's. */
        a[3] = iref(16 | 4);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawString",
                   "(Ljava/lang/String;III)V", a, NULL) == NJ_OK);
        {
            int x, last = -1;
            for (x = 0; x < 24; x++) {
                int y;
                for (y = 0; y < 8; y++) {
                    if (img_pixel(vm, img, x, y) == 0xFFFFFFFFu) {
                        last = x;
                        break;
                    }
                }
            }
            /* Ink must stay inside the advance the Font reports. */
            OK_MSG(last >= 0, "drawString drew nothing");
            OK_MSG(last < w, "drawString ink reached %d, advance is %d", last, w);
        }
        /* drawChar must produce the same ink as a one-character string. */
        a[0] = iref(0);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL) == NJ_OK);
        a[0] = iref(0);
        a[1] = iref(0);
        a[2] = iref(24);
        a[3] = iref(8);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "fillRect", "(IIII)V", a, NULL) == NJ_OK);
        a[0] = iref((nj_s4)0xFFFFFFFFu);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL) == NJ_OK);
        a[0] = iref('A');
        a[1] = iref(1);
        a[2] = iref(0);
        a[3] = iref(16 | 4);         /* Graphics.TOP | Graphics.LEFT */
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawChar", "(CIII)V", a, NULL) == NJ_OK);
        {
            int x, ink = 0;
            for (x = 0; x < 24; x++) {
                int y;
                for (y = 0; y < 8; y++) {
                    if (img_pixel(vm, img, x, y) == 0xFFFFFFFFu) {
                        ink++;
                        break;
                    }
                }
            }
            OK_MSG(ink > 0, "drawChar drew nothing");
        }
    }

    /* ---- a MIDlet's anchor encoding, not the renderer's ------------------
     * The trap is the *vertical* half.  Graphics.TOP | Graphics.LEFT is
     * 16 | 4 = 20, and the renderer's bits carry the same numbers - but the
     * renderer reads "no vertical bit set at all" as BASELINE, because a text
     * blitter needs some default, while MIDP defines a missing vertical
     * component as TOP.  Pass the value through without spelling TOP out and
     * every such line is drawn `ascent` pixels too high.  On screen that is a
     * game's dialogue text sitting on the top border of its own dialogue box,
     * which is how this was found.
     *
     * The robust way to pin the semantics is not to guess where a glyph's ink
     * starts, but to demand that two anchors describing the same place agree:
     * drawing at (x, y) with TOP|LEFT must leave the same ink as drawing at
     * (x, y + Font.getBaselinePosition()) with BASELINE|LEFT.
     */
    {
        nj_ref f3 = call_static_ref(vm, NJ_C_FONT, "getFont",
                                    "(III)L" NJ_C_FONT ";",
                                    (nj_value[]){ iref(0), iref(0), iref(0) },
                                    &ok);
        nj_ref s3 = str(vm, "Hg");
        nj_s4 base = 0;
        nj_s4 top_y = 6;
        int box_top[4] = { 999, -1, 999, -1 };   /* x0, x1, y0, y1 */
        int box_base[4] = { 999, -1, 999, -1 };

        OK(f3 != NULL && ok);
        if (f3) {
            a[0] = sref(f3);
            OK(nj_call(vm, g, NJ_C_GRAPHICS, "setFont", "(L" NJ_C_FONT ";)V",
                       a, NULL) == NJ_OK);
            base = call_int(vm, f3, NJ_C_FONT, "getBaselinePosition", "()I",
                            NULL, &ok);
        }
        OK_MSG(base > 0, "Font.getBaselinePosition() = %d", (int)base);

        a[0] = iref(0);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL) == NJ_OK);
        a[0] = iref(0);
        a[1] = iref(0);
        a[2] = iref(60);
        a[3] = iref(40);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "fillRect", "(IIII)V", a, NULL) == NJ_OK);
        a[0] = iref((nj_s4)0xFFFFFFFFu);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL) == NJ_OK);

        /* Graphics.TOP | Graphics.LEFT at (1, top_y). */
        a[0] = sref(s3);
        a[1] = iref(1);
        a[2] = iref(top_y);
        a[3] = iref(16 | 4);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawString",
                   "(Ljava/lang/String;III)V", a, NULL) == NJ_OK);
        {
            int x, y;
            for (y = 0; y < 40; y++) {
                for (x = 0; x < 60; x++) {
                    if (img_pixel(vm, img, x, y) == 0xFFFFFFFFu) {
                        if (x < box_top[0]) { box_top[0] = x; }
                        if (x > box_top[1]) { box_top[1] = x; }
                        if (y < box_top[2]) { box_top[2] = y; }
                        if (y > box_top[3]) { box_top[3] = y; }
                    }
                }
            }
        }
        OK_MSG(box_top[1] >= 0, "TOP|LEFT drew nothing");

        a[0] = iref(0);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL) == NJ_OK);
        a[0] = iref(0);
        a[1] = iref(0);
        a[2] = iref(60);
        a[3] = iref(40);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "fillRect", "(IIII)V", a, NULL) == NJ_OK);
        a[0] = iref((nj_s4)0xFFFFFFFFu);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL) == NJ_OK);

        /* Graphics.BASELINE | Graphics.LEFT, moved down by the baseline. */
        a[0] = sref(s3);
        a[1] = iref(1);
        a[2] = iref(top_y + base);
        a[3] = iref(64 | 4);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawString",
                   "(Ljava/lang/String;III)V", a, NULL) == NJ_OK);
        {
            int x, y;
            for (y = 0; y < 40; y++) {
                for (x = 0; x < 60; x++) {
                    if (img_pixel(vm, img, x, y) == 0xFFFFFFFFu) {
                        if (x < box_base[0]) { box_base[0] = x; }
                        if (x > box_base[1]) { box_base[1] = x; }
                        if (y < box_base[2]) { box_base[2] = y; }
                        if (y > box_base[3]) { box_base[3] = y; }
                    }
                }
            }
        }
        OK_MSG(box_base[1] >= 0, "BASELINE|LEFT drew nothing");
        OK_MSG(box_top[0] == box_base[0] && box_top[1] == box_base[1] &&
               box_top[2] == box_base[2] && box_top[3] == box_base[3],
               "TOP|LEFT at y=%d and BASELINE|LEFT at y=%d disagree: "
               "[%d..%d]x[%d..%d] vs [%d..%d]x[%d..%d]",
               (int)top_y, (int)(top_y + base),
               box_top[0], box_top[1], box_top[2], box_top[3],
               box_base[0], box_base[1], box_base[2], box_base[3]);
        /* And TOP must not mean "the y is the baseline": the ink has to start
         * inside the text box, not `ascent` pixels above it. */
        OK_MSG(box_top[2] >= top_y && box_top[2] < top_y + base,
               "TOP|LEFT put the ink top at %d, outside the box starting at y=%d",
               box_top[2], (int)top_y);

        /*
         * Anchor 0.  MIDP 2.0, "Anchor Points": "Zero may also be used as the
         * value of an anchor point.  Using zero for the anchor point value
         * gives results identical to using TOP | LEFT."
         *
         * This is not a corner case.  `drawString(s, x, y, 0)` is what a great
         * many games write, and it is what the bare three-argument
         * `drawString(s, x, y)` means.  The pass-through encoding happened to
         * give it "no vertical bit", which the blitter reads as BASELINE - so
         * every one of those lines was drawn `ascent` pixels too high.  On the
         * 仙剑奇侠传 dialogue that put a line of speech on the top border of
         * the box drawn for it.
         */
        {
            int box_zero[4] = { 999, -1, 999, -1 };
            a[0] = iref(0);
            OK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL) == NJ_OK);
            a[0] = iref(0);
            a[1] = iref(0);
            a[2] = iref(60);
            a[3] = iref(40);
            OK(nj_call(vm, g, NJ_C_GRAPHICS, "fillRect", "(IIII)V", a, NULL) == NJ_OK);
            a[0] = iref((nj_s4)0xFFFFFFFFu);
            OK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL) == NJ_OK);

            a[0] = sref(s3);
            a[1] = iref(1);
            a[2] = iref(top_y);
            a[3] = iref(0);
            OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawString",
                       "(Ljava/lang/String;III)V", a, NULL) == NJ_OK);
            {
                int x, y;
                for (y = 0; y < 40; y++) {
                    for (x = 0; x < 60; x++) {
                        if (img_pixel(vm, img, x, y) == 0xFFFFFFFFu) {
                            if (x < box_zero[0]) { box_zero[0] = x; }
                            if (x > box_zero[1]) { box_zero[1] = x; }
                            if (y < box_zero[2]) { box_zero[2] = y; }
                            if (y > box_zero[3]) { box_zero[3] = y; }
                        }
                    }
                }
            }
            OK_MSG(box_zero[1] >= 0, "anchor 0 drew nothing");
            OK_MSG(box_zero[0] == box_top[0] && box_zero[1] == box_top[1] &&
                   box_zero[2] == box_top[2] && box_zero[3] == box_top[3],
                   "anchor 0 and TOP|LEFT must be identical, but gave "
                   "[%d..%d]x[%d..%d] vs [%d..%d]x[%d..%d]",
                   box_zero[0], box_zero[1], box_zero[2], box_zero[3],
                   box_top[0], box_top[1], box_top[2], box_top[3]);
            /* Spelled out separately so a failure says which way it went:
             * anchor 0 on the baseline is the historical bug, and it is the
             * one that looks almost right. */
            OK_MSG(box_zero[2] >= top_y,
                   "anchor 0 was treated as BASELINE: ink starts at %d, above "
                   "the box at y=%d", box_zero[2], (int)top_y);
        }
    }

    /* ---- drawRegion with a transform ---- */
    {
        nj_value ra[9];   /* drawRegion takes 8 ints in MIDP 2.0 */
        ra[0] = sref(img);
        ra[1] = iref(0);
        ra[2] = iref(0);
        ra[3] = iref(4);
        ra[4] = iref(4);
        /* drawRegion(Image, x_src, y_src, w, h, transform, x_dest, y_dest,
         *            anchor) */
        ra[0] = sref(img);
        ra[1] = iref(0);
        ra[2] = iref(0);
        ra[3] = iref(4);
        ra[4] = iref(4);
        ra[5] = iref(NJ_TRANS_NONE);
        ra[6] = iref(20);
        ra[7] = iref(10);
        ra[8] = iref(-1);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawRegion",
                   "(L" NJ_C_IMAGE ";IIIIIIII)V", ra, NULL) == NJ_OK);
        ra[5] = iref(NJ_TRANS_ROT90);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawRegion",
                   "(L" NJ_C_IMAGE ";IIIIIIII)V", ra, NULL) == NJ_OK);
        ra[5] = iref(NJ_TRANS_NONE);
        ra[8] = iref(NJ_ANCHOR_LEFT | NJ_ANCHOR_TOP);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawRegion",
                   "(L" NJ_C_IMAGE ";IIIIIIII)V", ra, NULL) == NJ_OK);
        /* An anchored region must land where the anchor says. */
        {
            nj_u4 before = img_pixel(vm, img, 20, 10);
            ra[8] = iref(NJ_ANCHOR_HCENTER | NJ_ANCHOR_TOP);
            OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawRegion",
                       "(L" NJ_C_IMAGE ";IIIIIIII)V", ra, NULL) == NJ_OK);
            OK(vm->exception == NULL);
            (void)before;
        }
    }

    /* ---- drawImage: a WHOLE-image blit with an anchor --------------------
     * drawImage(Image, int x, int y, int anchor) has a different argument
     * shape from drawRegion(); passing their argument lists to one shared
     * helper reads past the end of the slots and rejects valid calls. */
    {
        nj_ref src = image_new(vm, 4, 3);
        nj_ref dst = image_new(vm, 12, 8);
        nj_ref sg = src ? call_ref(vm, src, NJ_C_IMAGE, "getGraphics",
                                   "()L" NJ_C_GRAPHICS ";", NULL, &ok) : NULL;
        nj_ref dg = dst ? call_ref(vm, dst, NJ_C_IMAGE, "getGraphics",
                                   "()L" NJ_C_GRAPHICS ";", NULL, &ok) : NULL;
        OK(src && dst && sg && dg);
        if (src && dst && sg && dg) {
            nj_value ia[4];
            ia[0] = iref((nj_s4)0xFFCC2200u);
            OK(nj_call(vm, sg, NJ_C_GRAPHICS, "setColor", "(I)V", ia,
                       NULL) == NJ_OK);
            ia[0] = iref(0);
            ia[1] = iref(0);
            ia[2] = iref(4);
            ia[3] = iref(3);
            OK(nj_call(vm, sg, NJ_C_GRAPHICS, "fillRect", "(IIII)V", ia,
                       NULL) == NJ_OK);
            /* top-left anchored at (5,4): Graphics.LEFT | Graphics.TOP */
            ia[0] = sref(src);
            ia[1] = iref(5);
            ia[2] = iref(4);
            ia[3] = iref(16 | 4);
            OK(nj_call(vm, dg, NJ_C_GRAPHICS, "drawImage",
                       "(L" NJ_C_IMAGE ";III)V", ia, NULL) == NJ_OK);
            OK_MSG(img_pixel(vm, dst, 5, 4) == 0xFFCC2200u,
                   "drawImage LEFT|TOP pixel = 0x%08X", img_pixel(vm, dst, 5, 4));
            OK_MSG(img_pixel(vm, dst, 8, 6) == 0xFFCC2200u,
                   "drawImage covered its whole 4x3 box");
            /* Untouched pixels of a mutable image are the white it started
             * with - "Every pixel within a mutable image is always fully
             * opaque" - so a pixel outside the drawn box stays 0xFFFFFFFF. */
            OK_MSG(img_pixel(vm, dst, 9, 6) == 0xFFFFFFFFu,
                   "drawImage overran its box");
            OK_MSG(img_pixel(vm, dst, 4, 4) == 0xFFFFFFFFu,
                   "drawImage started too early");
            /* horizontally centred at (9,0): Graphics.HCENTER | Graphics.TOP */
            ia[0] = sref(src);
            ia[1] = iref(9);
            ia[2] = iref(0);
            ia[3] = iref(1 | 16);
            OK(nj_call(vm, dg, NJ_C_GRAPHICS, "drawImage",
                       "(L" NJ_C_IMAGE ";III)V", ia, NULL) == NJ_OK);
            OK_MSG(img_pixel(vm, dst, 7, 0) == 0xFFCC2200u,
                   "HCENTER anchor put the image at the wrong x");
        }
    }

    {
        /*
         * MIDP's anchor rule is about CONFLICTS, not omissions: a missing
         * horizontal component means LEFT and a missing vertical means TOP,
         * and 0 is a legal anchor meaning TOP|LEFT.
         *
         * This block used to assert the opposite - that Graphics.TOP on its
         * own throws - which is not what MIDP says, and which made
         * 植物大战僵尸2013完美升级版 throw once per frame and paint nothing.
         * The test was encoding the bug, so it is now the other way round and
         * also checks the case that really is an error.
         */
        nj_ref dst2 = image_new(vm, 4, 4);
        nj_ref dg2 = dst2 ? call_ref(vm, dst2, NJ_C_IMAGE, "getGraphics",
                                     "()L" NJ_C_GRAPHICS ";", NULL, &ok) : NULL;
        if (dg2) {
            nj_value ia[4];
            ia[0] = sref(img);
            ia[1] = iref(0);
            ia[2] = iref(0);

            /* Graphics.TOP alone is legal; the horizontal half defaults to
             * LEFT. */
            ia[3] = iref(16);
            (void)nj_call(vm, dg2, NJ_C_GRAPHICS, "drawImage",
                          "(L" NJ_C_IMAGE ";III)V", ia, NULL);
            OK_MSG(!exc_is(vm, "java/lang/IllegalArgumentException"),
                   "drawImage with TOP alone threw; MIDP defaults it to LEFT");

            /*
             * Graphics.HCENTER | Graphics.VCENTER = 1 | 2 = 3.  This is the
             * fourth commonest anchor in the whole corpus -- 2482 call sites --
             * and it is perfectly legal for an image: one horizontal constant,
             * one vertical.  It must not be rejected.
             *
             * It was, once.  The runtime numbered LEFT as 1 in those days, so 3
             * read as LEFT|HCENTER -- a real conflict -- and every title that
             * centred a sprite threw IllegalArgumentException on every frame
             * instead of drawing.  The numbering was the bug, not the anchor,
             * and "resolve the conflict instead of rejecting it" was treating
             * the symptom.
             */
            {
                nj_ref dst3 = image_new(vm, 4, 4);
                nj_ref dg3 = dst3 ? call_ref(vm, dst3, NJ_C_IMAGE, "getGraphics",
                                             "()L" NJ_C_GRAPHICS ";", NULL, &ok) : NULL;
                if (dg3) {
                    nj_value ja[4];
                    ja[0] = sref(img);
                    ja[1] = iref(0);
                    ja[2] = iref(0);
                    ja[3] = iref(1 | 2);        /* HCENTER | VCENTER */
                    (void)nj_call(vm, dg3, NJ_C_GRAPHICS, "drawImage",
                                  "(L" NJ_C_IMAGE ";III)V", ja, NULL);
                    OK_MSG(!exc_is(vm, "java/lang/IllegalArgumentException"),
                           "drawImage with HCENTER|VCENTER threw; 2482 real call sites pass it");
                }
            }

            /*
             * Two constants from the SAME group is the conflict MIDP actually
             * forbids -- "the anchor point ... is specified in terms of a
             * constant for the horizontal and one for the vertical component".
             * LEFT|RIGHT = 4 | 8 = 12 has no meaning, so it throws rather than
             * quietly picking one.
             */
            {
                nj_ref dst4 = image_new(vm, 4, 4);
                nj_ref dg4 = dst4 ? call_ref(vm, dst4, NJ_C_IMAGE, "getGraphics",
                                             "()L" NJ_C_GRAPHICS ";", NULL, &ok) : NULL;
                if (dg4) {
                    nj_value ja[4];
                    ja[0] = sref(img);
                    ja[1] = iref(0);
                    ja[2] = iref(0);
                    ja[3] = iref(4 | 8);        /* LEFT | RIGHT */
                    (void)nj_call(vm, dg4, NJ_C_GRAPHICS, "drawImage",
                                  "(L" NJ_C_IMAGE ";III)V", ja, NULL);
                    OK_MSG(exc_is(vm, "java/lang/IllegalArgumentException"),
                           "drawImage with LEFT|RIGHT must throw");
                }
            }
        }
    }

    /* ---- copyArea ---- */
    {
        nj_value ca[7];
        ca[0] = iref(0);
        ca[1] = iref(0);
        ca[2] = iref(3);
        ca[3] = iref(3);
        ca[4] = iref(20);
        ca[5] = iref(5);
        ca[6] = iref(NJ_ANCHOR_LEFT | NJ_ANCHOR_TOP);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "copyArea", "(IIIIIII)V", ca,
                   NULL) == NJ_OK);
        /* Overlapping copy: source and destination share rows. */
        ca[0] = iref(10);
        ca[1] = iref(5);
        ca[2] = iref(6);
        ca[3] = iref(6);
        ca[4] = iref(11);
        ca[5] = iref(6);
        ca[6] = iref(NJ_ANCHOR_LEFT | NJ_ANCHOR_TOP);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "copyArea", "(IIIIIII)V", ca,
                   NULL) == NJ_OK);
    }

    /* ---- drawRGB ---- */
    {
        nj_ref rgb = nj_int_array_new(vm, 4u);
        nj_value ra[8];
        nj_array_set_int(rgb, 0u, (nj_s4)0x00FF8000u);
        nj_array_set_int(rgb, 1u, (nj_s4)0x0000FF80u);
        nj_array_set_int(rgb, 2u, (nj_s4)0x008000FFu);
        nj_array_set_int(rgb, 3u, (nj_s4)0x00FFFFFFu);
        ra[0] = sref(rgb);
        ra[1] = iref(0);
        ra[2] = iref(2);
        ra[3] = iref(15);
        ra[4] = iref(12);
        ra[5] = iref(2);
        ra[6] = iref(2);
        ra[7] = iref(0);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawRGB", "([IIIIIIIZ)V", ra,
                   NULL) == NJ_OK);
        OK_MSG(img_pixel(vm, img, 15, 12) == 0xFFFF8000u,
               "drawRGB ignores alpha when requested = 0x%08X",
               img_pixel(vm, img, 15, 12));
        nj_array_set_int(rgb, 0u, (nj_s4)0x00FF00FFu);
        nj_array_set_int(rgb, 1u, (nj_s4)0x80000000u);
        nj_array_set_int(rgb, 2u, (nj_s4)0xFF123456u);
        ra[7] = iref(1);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawRGB", "([IIIIIIIZ)V", ra,
                   NULL) == NJ_OK);
        OK_MSG(img_pixel(vm, img, 15, 12) == 0xFFFF8000u,
               "drawRGB transparent colour key leaves destination intact");
        OK_MSG(img_pixel(vm, img, 16, 12) == 0xFF007F40u,
               "drawRGB blends half alpha = 0x%08X", img_pixel(vm, img, 16, 12));
        OK_MSG(img_pixel(vm, img, 15, 13) == 0xFF123456u,
               "drawRGB copies opaque ARGB pixels");
        ra[1] = iref(2);
        ra[2] = iref(-2);
        ra[3] = iref(20);
        ra[7] = iref(0);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawRGB", "([IIIIIIIZ)V", ra,
                   NULL) == NJ_OK);
        OK_MSG(img_pixel(vm, img, 20, 12) == 0xFF123456u &&
               img_pixel(vm, img, 20, 13) == 0xFFFF00FFu,
               "drawRGB supports negative scanlength");
        ra[1] = iref(3);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawRGB", "([IIIIIIIZ)V", ra,
                   NULL) != NJ_OK);
        OK(exc_is(vm, "java/lang/ArrayIndexOutOfBoundsException"));
        ra[1] = iref(0);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawRGB", "([IIIIIIIZ)V", ra,
                   NULL) != NJ_OK);
        OK(exc_is(vm, "java/lang/ArrayIndexOutOfBoundsException"));
    }

    /* ---- XOR mode ---- */
    {
        nj_value xa[1];
        nj_u4 before, after;
        /* Start from a known pixel. */
        a[0] = iref((nj_s4)0xFF102030u);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL) == NJ_OK);
        a[0] = iref(1);
        a[1] = iref(1);
        a[2] = iref(1);
        a[3] = iref(1);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "fillRect", "(IIII)V", a, NULL) == NJ_OK);
        before = img_pixel(vm, img, 1, 1);
        OK_MSG(before == 0xFF102030u, "setup pixel = 0x%08X", before);
        xa[0] = sref(NULL);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "setXORMode",
                   "(L" NJ_C_GRAPHICS ";)V", xa, NULL) == NJ_OK);
        a[0] = iref(1);
        a[1] = iref(1);
        a[2] = iref(1);
        a[3] = iref(1);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "fillRect", "(IIII)V", a, NULL) == NJ_OK);
        after = img_pixel(vm, img, 1, 1);
        OK_MSG(after != before, "XOR mode produced no change (0x%08X)", before);
        OK_MSG(after == (before ^ 0x00FFFFFFu),
               "XOR gave 0x%08X, expected 0x%08X", after,
               before ^ 0x00FFFFFFu);
        /* setXORMode(false) must leave XOR mode. */
        a[0] = iref(0);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "setXORMode", "(Z)V", a, NULL) == NJ_OK);
        /* Back to normal mode: a plain fill must overwrite again. */
        a[0] = iref((nj_s4)0xFF445566u);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL) == NJ_OK);
        a[0] = iref(1);
        a[1] = iref(1);
        a[2] = iref(1);
        a[3] = iref(1);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "fillRect", "(IIII)V", a, NULL) == NJ_OK);
        OK_MSG(img_pixel(vm, img, 1, 1) == 0xFF445566u,
               "after XOR clear = 0x%08X", img_pixel(vm, img, 1, 1));
    }

    /* ---- error cases ---- */
    a[0] = iref(0);
    a[1] = iref(0);
    a[2] = iref(-4);
    a[3] = iref(4);
    (void)nj_call(vm, g, NJ_C_GRAPHICS, "setClip", "(IIII)V", a, NULL);
    /*
     * b5b8146 made this legal on purpose: MIDlets compute w = width - x with x
     * past the edge all the time (傲世三国志 and 穿越火线 did it every frame)
     * and MIDP calls that an empty clip, not a programming error.  The old
     * assertion below asserted the throw, so it was the test that had to move.
     */
    OK_MSG(!pending_exc(vm), "a negative clip size is an empty clip, not an error");
    /* A negative ORIGIN is legal: MIDP intersects the clip with the surface. */
    a[0] = iref(-3);
    a[1] = iref(-3);
    a[2] = iref(8);
    a[3] = iref(8);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "setClip", "(IIII)V", a, NULL) == NJ_OK);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getClipX", "()I", NULL, &ok) == 0 && ok);
    OK(call_int(vm, g, NJ_C_GRAPHICS, "getClipWidth", "()I", NULL, &ok) == 5 && ok);
    a[0] = iref(1);
    a[1] = iref(1);
    a[2] = iref(1);
    a[3] = iref(1);
    (void)nj_call(vm, g, NJ_C_GRAPHICS, "fillRect", "(IIII)V", a, NULL);
    OK(vm->exception == NULL);
}

/* ------------------------------------------------------------------ */
/* Every declared method must be callable                              */
/* ------------------------------------------------------------------ */
/*
 * THE regression guard for the whole class of bug this module is prone to: a
 * method that is declared in the builtin class table (so javac happily compiles
 * a call to it) but has no native bound, which only shows up as a
 * NoSuchMethodError/UnsatisfiedLinkError when a real MIDlet reaches that exact
 * overload in the field.
 *
 * It walks the class table itself, so it covers every class this module owns,
 * inherited methods included, and it is checked against the native registry the
 * VM will actually use at run time.
 */
static void test_all_methods_bound(nj_vm *vm)
{
    extern const nj_builtin_class nj_lib_lcdui_classes[];
    extern const int nj_lib_lcdui_class_count;
    int ci;
    int checked = 0;

    for (ci = 0; ci < nj_lib_lcdui_class_count; ci++) {
        const nj_builtin_class *bc = &nj_lib_lcdui_classes[ci];
        nj_class *c;
        nj_u2 mi;

        if (!bc->name) {
            continue;
        }
        c = nj_class_lookup(vm, bc->name);
        OK_MSG(c != NULL, "class %s is not registered", bc->name);
        if (!c) {
            continue;
        }
        for (mi = 0u; mi < c->method_count; mi++) {
            nj_method *m = &c->methods[mi];
            if ((m->flags & NJ_ACC_ABSTRACT) != 0u) {
                continue;   /* abstract by design: the subclass must override */
            }
            checked++;
            OK_MSG(nj_native_find(vm, c->name, m->name, m->desc) != NULL,
                   "no native bound for %s.%s%s", c->name, m->name, m->desc);
        }
    }
    OK_MSG(checked > 200, "only %d concrete methods were checked", checked);
}

/* Every constant a MIDlet can read must have been initialised. */
static void test_constants(nj_vm *vm)
{
    static const struct { const char *cls; const char *name; nj_s4 value; } k[] = {
        { "javax/microedition/lcdui/Graphics", "HCENTER", 1 },
        { "javax/microedition/lcdui/Graphics", "VCENTER", 2 },
        { "javax/microedition/lcdui/Graphics", "LEFT",    4 },
        { "javax/microedition/lcdui/Graphics", "RIGHT",   8 },
        { "javax/microedition/lcdui/Graphics", "TOP",     16 },
        { "javax/microedition/lcdui/Graphics", "BOTTOM",  32 },
        { "javax/microedition/lcdui/Graphics", "BASELINE", 64 },
        { "javax/microedition/lcdui/Canvas",   "KEY_NUM0", 48 },
        { "javax/microedition/lcdui/Canvas",   "KEY_STAR", 42 },
        { "javax/microedition/lcdui/Canvas",   "KEY_POUND", 35 },
        { "javax/microedition/lcdui/Canvas",   "KEY_UP",   -1 },
        { "javax/microedition/lcdui/Canvas",   "KEY_DOWN", -2 },
        { "javax/microedition/lcdui/Canvas",   "KEY_LEFT", -3 },
        { "javax/microedition/lcdui/Canvas",   "KEY_RIGHT", -4 },
        { "javax/microedition/lcdui/Canvas",   "KEY_FIRE", -5 },
        { "javax/microedition/lcdui/Canvas",   "KEY_SOFTKEY1", -6 },
        { "javax/microedition/lcdui/Canvas",   "KEY_SOFTKEY2", -7 },
        { "javax/microedition/lcdui/Canvas",   "KEY_SOFTKEY3", -8 },
        { "javax/microedition/lcdui/Canvas",   "UP",    1 },
        { "javax/microedition/lcdui/Canvas",   "DOWN",  6 },
        { "javax/microedition/lcdui/Canvas",   "LEFT",  2 },
        { "javax/microedition/lcdui/Canvas",   "RIGHT", 5 },
        { "javax/microedition/lcdui/Canvas",   "FIRE",  8 },
        { "javax/microedition/lcdui/Canvas",   "GAME_A", 9 },
        { "javax/microedition/lcdui/Canvas",   "GAME_D", 12 },
        { "javax/microedition/lcdui/Command",  "SCREEN", 1 },
        { "javax/microedition/lcdui/Command",  "BACK",   2 },
        { "javax/microedition/lcdui/Command",  "CANCEL", 3 },
        { "javax/microedition/lcdui/Command",  "OK",     4 },
        { "javax/microedition/lcdui/Command",  "HELP",   5 },
        { "javax/microedition/lcdui/Command",  "STOP",   6 },
        { "javax/microedition/lcdui/Command",  "EXIT",   7 },
        { "javax/microedition/lcdui/Command",  "ITEM",   8 },
        { "javax/microedition/lcdui/Command",  "PRIORITY_1", 1 },
        { "javax/microedition/lcdui/Font",     "FACE_SYSTEM", 0 },
        { "javax/microedition/lcdui/Font",     "FACE_MONOSPACED", 32 },
        { "javax/microedition/lcdui/Font",     "FACE_PROPORTIONAL", 64 },
        { "javax/microedition/lcdui/Font",     "STYLE_BOLD", 1 },
        { "javax/microedition/lcdui/Font",     "STYLE_UNDERLINED", 4 },
        { "javax/microedition/lcdui/Font",     "SIZE_SMALL", 8 },
        { "javax/microedition/lcdui/Font",     "SIZE_MEDIUM", 0 },
        { "javax/microedition/lcdui/Font",     "SIZE_LARGE", 16 },
        { "javax/microedition/lcdui/ChoiceGroup", "EXCLUSIVE", 1 },
        { "javax/microedition/lcdui/ChoiceGroup", "MULTIPLE",  2 },
        { "javax/microedition/lcdui/ChoiceGroup", "IMPLICIT",  3 },
        { "javax/microedition/lcdui/List",     "EXCLUSIVE", 1 },
        { "javax/microedition/lcdui/List",     "MULTIPLE",  2 },
        { "javax/microedition/lcdui/List",     "IMPLICIT",  3 },
        { "javax/microedition/lcdui/TextField", "NUMERIC", 2 },
        { "javax/microedition/lcdui/TextField", "PASSWORD", 0x10000 },
        { "javax/microedition/lcdui/TextBox",  "PASSWORD", 0x10000 },
        { "javax/microedition/lcdui/DateField", "DATE", 1 },
        { "javax/microedition/lcdui/DateField", "TIME", 2 },
        { "javax/microedition/lcdui/DateField", "DATE_TIME", 3 },
        { "javax/microedition/lcdui/Gauge",    "INDEFINITE", -1 },
        { "javax/microedition/lcdui/Item",     "LAYOUT_CENTER", 3 },
        { "javax/microedition/lcdui/Item",     "LAYOUT_NEWLINE_BEFORE", 256 },
        { "javax/microedition/lcdui/Display",  "LIST_ELEMENT", 0 }
    };
    unsigned i;

    for (i = 0u; i < sizeof(k) / sizeof(k[0]); i++) {
        nj_field *f = nj_field_lookup(vm, k[i].cls, k[i].name, "I", 1);
        if (!f) {
            OK_MSG(0, "constant %s.%s is not declared", k[i].cls, k[i].name);
            continue;
        }
        OK_MSG(nj_field_read_int(vm, NULL, f) == k[i].value,
               "%s.%s = %d, expected %d", k[i].cls, k[i].name,
               (int)nj_field_read_int(vm, NULL, f), (int)k[i].value);
    }
}

/* ------------------------------------------------------------------ */
/* Font                                                                */
/* ------------------------------------------------------------------ */
static void test_font(nj_vm *vm)
{
    nj_value a[3];
    int ok = 0;
    nj_ref f1, f2, f3, def;
    nj_ref s = str(vm, "Hello, world");
    nj_ref chars;
    nj_s4 w_all, w_sub, w_mid;

    a[0] = iref(0);
    a[1] = iref(0);
    a[2] = iref(0);
    f1 = call_static_ref(vm, NJ_C_FONT, "getFont", "(III)L" NJ_C_FONT ";", a, &ok);
    OK(f1 != NULL && ok);
    f2 = call_static_ref(vm, NJ_C_FONT, "getFont", "(III)L" NJ_C_FONT ";", a, &ok);
    /* Interning: the SAME object, not merely an equal one. */
    OK_MSG(f1 == f2, "getFont is not interned");
    def = call_static_ref(vm, NJ_C_FONT, "getDefaultFont", "()L" NJ_C_FONT ";",
                          NULL, &ok);
    OK(def != NULL && ok);
    OK_MSG(def == f1, "getDefaultFont differs from getFont(MEDIUM,PLAIN)");

    a[0] = iref(1);         /* STYLE_BOLD */
    f3 = call_static_ref(vm, NJ_C_FONT, "getFont", "(I)L" NJ_C_FONT ";", a, &ok);
    OK(f3 != NULL && ok);
    OK_MSG(f3 != f1, "bold font must not be the plain one");
    a[0] = iref(1);
    OK(call_static_ref(vm, NJ_C_FONT, "getFont", "(I)L" NJ_C_FONT ";", a, &ok) == f3);
    OK(call_int(vm, f3, NJ_C_FONT, "isBold", "()Z", NULL, &ok) == 1 && ok);
    OK(call_int(vm, f3, NJ_C_FONT, "isPlain", "()Z", NULL, &ok) == 0 && ok);
    OK(call_int(vm, f1, NJ_C_FONT, "isPlain", "()Z", NULL, &ok) == 1 && ok);
    OK(call_int(vm, f1, NJ_C_FONT, "getStyle", "()I", NULL, &ok) == 0 && ok);
    OK(call_int(vm, f1, NJ_C_FONT, "getSize", "()I", NULL, &ok) == 0 && ok);

    {
        nj_s4 h = call_int(vm, f1, NJ_C_FONT, "getHeight", "()I", NULL, &ok);
        nj_s4 asc = call_int(vm, f1, NJ_C_FONT, "getAscent", "()I", NULL, &ok);
        nj_s4 desc = call_int(vm, f1, NJ_C_FONT, "getDescent", "()I", NULL, &ok);
        nj_s4 base = call_int(vm, f1, NJ_C_FONT, "getBaselinePosition", "()I",
                              NULL, &ok);
        OK_MSG(h > 0, "font height = %d", h);
        OK_MSG(asc > 0, "font ascent = %d", asc);
        OK_MSG(asc + desc == h, "ascent %d + descent %d != height %d",
               asc, desc, h);
        OK_MSG(base == asc, "baseline %d != ascent %d", base, asc);
    }

    /* charWidth / stringWidth / substringWidth / charsWidth agree. */
    {
        nj_s4 cw_a, cw_l;
        a[0] = iref('a');
        cw_a = call_int(vm, f1, NJ_C_FONT, "charWidth", "(C)I", a, &ok);
        a[0] = iref('l');
        cw_l = call_int(vm, f1, NJ_C_FONT, "charWidth", "(C)I", a, &ok);
        OK(cw_a > 0 && cw_l > 0);
        a[0] = sref(s);
        w_all = call_int(vm, f1, NJ_C_FONT, "stringWidth",
                         "(Ljava/lang/String;)I", a, &ok);
        OK(w_all > 0 && ok);
        a[0] = sref(s);
        a[1] = iref(0);
        a[2] = iref(5);
        w_sub = call_int(vm, f1, NJ_C_FONT, "substringWidth",
                         "(Ljava/lang/String;II)I", a, &ok);
        OK(w_sub > 0 && ok);
        a[0] = sref(s);
        a[1] = iref(5);
        a[2] = iref(7);
        w_mid = call_int(vm, f1, NJ_C_FONT, "substringWidth",
                         "(Ljava/lang/String;II)I", a, &ok);
        OK_MSG(w_sub + w_mid == w_all,
               "substring widths %d + %d != full %d", w_sub, w_mid, w_all);
        /* charsWidth over the same characters as the first substring. */
        chars = nj_char_array_new(vm, 5u);
        OK(chars != NULL);
        memcpy(chars->u.bytes, nj_string_chars(s), 5u * sizeof(nj_u2));
        a[0] = sref(chars);
        a[1] = iref(0);
        a[2] = iref(5);
        OK_MSG(call_int(vm, f1, NJ_C_FONT, "charsWidth", "([CII)I", a, &ok) == w_sub,
               "charsWidth != substringWidth");
    }

    /* Every style/size combination must be reachable and distinct where it
     * should be. */
    {
        nj_ref big = call_static_ref(vm, NJ_C_FONT, "getFont",
                                     "(III)L" NJ_C_FONT ";",
                                     (nj_value[]){ iref(64), iref(0), iref(16) },
                                     &ok);
        nj_s4 h = call_int(vm, f1, NJ_C_FONT, "getHeight", "()I", NULL, &ok);
        nj_s4 hb = call_int(vm, big, NJ_C_FONT, "getHeight", "()I", NULL, &ok);
        OK(big != NULL);
        OK_MSG(hb >= h, "large font height %d < medium %d", hb, h);
        OK(call_int(vm, big, NJ_C_FONT, "getSize", "()I", NULL, &ok) == 16 && ok);
    }

    /* Error cases. */
    a[0] = iref(1);
    a[1] = iref(99);        /* an unknown style bit */
    a[2] = iref(0);
    (void)call_static_ref(vm, NJ_C_FONT, "getFont", "(III)L" NJ_C_FONT ";", a,
                          NULL);
    OK_MSG(exc_is(vm, "java/lang/IllegalArgumentException"),
           "getFont with a bad style did not throw");
    a[0] = sref(NULL);
    (void)call_int(vm, f1, NJ_C_FONT, "stringWidth", "(Ljava/lang/String;)I", a,
                   NULL);
    OK_MSG(exc_is(vm, "java/lang/NullPointerException"),
           "stringWidth(null) did not throw");
    a[0] = sref(s);
    a[1] = iref(0);
    a[2] = iref(999);
    (void)call_int(vm, f1, NJ_C_FONT, "substringWidth",
                   "(Ljava/lang/String;II)I", a, NULL);
    OK_MSG(exc_is(vm, "java/lang/StringIndexOutOfBoundsException"),
           "substringWidth out of range did not throw");
}

/* ------------------------------------------------------------------ */
/* Command / Ticker                                                    */
/* ------------------------------------------------------------------ */
static void test_command(nj_vm *vm)
{
    nj_value a[4];
    int ok = 0;
    nj_ref c1, c2, c3, c4;

    c1 = construct(vm, NJ_C_COMMAND, "(Ljava/lang/String;II)V",
                   (nj_value[]){ sref(str(vm, "B")), iref(1), iref(10) });
    c2 = construct(vm, NJ_C_COMMAND, "(Ljava/lang/String;II)V",
                   (nj_value[]){ sref(str(vm, "A")), iref(4), iref(1) });
    c3 = construct(vm, NJ_C_COMMAND, "(Ljava/lang/String;II)V",
                   (nj_value[]){ sref(str(vm, "C")), iref(1), iref(10) });
    c4 = construct(vm, NJ_C_COMMAND, "(Ljava/lang/String;III)V",
                   (nj_value[]){ sref(str(vm, "Okay then")), iref(4), iref(2),
                                 iref('K') });
    OK(c1 && c2 && c3 && c4);
    if (!c1 || !c2 || !c3 || !c4) {
        return;
    }
    OK(call_int(vm, c1, NJ_C_COMMAND, "getCommandType", "()I", NULL, &ok) == 1 && ok);
    OK(call_int(vm, c1, NJ_C_COMMAND, "getPriority", "()I", NULL, &ok) == 10 && ok);
    {
        nj_ref l = call_ref(vm, c1, NJ_C_COMMAND, "getLabel",
                            "()Ljava/lang/String;", NULL, &ok);
        OK(l != NULL && ok && nj_string_equals_utf8(l, "B"));
    }
    {
        nj_ref l = call_ref(vm, c4, NJ_C_COMMAND, "getLongLabel",
                            "()Ljava/lang/String;", NULL, &ok);
        OK(l != NULL && ok && nj_string_equals_utf8(l, "Okay then"));
        l = call_ref(vm, c4, NJ_C_COMMAND, "getLabel",
                     "()Ljava/lang/String;", NULL, &ok);
        OK(l != NULL && nj_string_equals_utf8(l, "Okay then"));
    }

    /* Priority ordering, then type, then equality. */
    a[0] = sref(c2);
    OK_MSG(call_int(vm, c1, NJ_C_COMMAND, "compareTo",
                    "(L" NJ_C_COMMAND ";)I", a, &ok) > 0,
           "priority 10 must sort after priority 1");
    a[0] = sref(c4);
    {
        nj_s4 d = call_int(vm, c2, NJ_C_COMMAND, "compareTo",
                           "(L" NJ_C_COMMAND ";)I", a, &ok);
        OK_MSG(d < 0, "type OK(4) must sort before SCREEN(1) at equal priority");
    }
    a[0] = sref(c3);
    OK_MSG(call_int(vm, c1, NJ_C_COMMAND, "compareTo", "(L" NJ_C_COMMAND ";)I",
                    a, &ok) == 0, "equal commands must compare equal");

    /* Ticker round-trip. */
    {
        nj_ref t = construct(vm, NJ_C_TICKER, "(Ljava/lang/String;)V",
                             (nj_value[]){ sref(str(vm, "breaking news")) });
        nj_ref l;
        OK(t != NULL);
        if (t) {
            l = call_ref(vm, t, NJ_C_TICKER, "getString", "()Ljava/lang/String;",
                         NULL, &ok);
            OK(l != NULL && ok && nj_string_equals_utf8(l, "breaking news"));
            a[0] = sref(str(vm, "later"));
            OK(nj_call(vm, t, NJ_C_TICKER, "setString", "(Ljava/lang/String;)V",
                       a, NULL) == NJ_OK);
            l = call_ref(vm, t, NJ_C_TICKER, "getString", "()Ljava/lang/String;",
                         NULL, &ok);
            OK(l != NULL && nj_string_equals_utf8(l, "later"));
        }
        /* A null ticker text must be rejected. */
        (void)construct(vm, NJ_C_TICKER, "(Ljava/lang/String;)V",
                        (nj_value[]){ sref(NULL) });
        OK_MSG(exc_is(vm, "java/lang/NullPointerException"),
               "Ticker(null) did not throw");
        a[0] = sref(NULL);
        (void)construct(vm, NJ_C_COMMAND, "(Ljava/lang/String;II)V",
                        (nj_value[]){ sref(NULL), iref(1), iref(1) });
        OK_MSG(exc_is(vm, "java/lang/NullPointerException"),
               "Command with a null label did not throw");
    }
}

/* ------------------------------------------------------------------ */
/* Widgets                                                             */
/* ------------------------------------------------------------------ */
static void test_widgets(nj_vm *vm)
{
    nj_value a[8];
    int ok = 0;

    /* ---- List ---- */
    {
        nj_ref list = construct(vm, NJ_C_LIST, "(Ljava/lang/String;I)V",
                                (nj_value[]){ sref(str(vm, "Menu")), iref(3) });
        nj_ref s2 = str(vm, "two");
        nj_ref res;
        OK(list != NULL);
        if (!list) {
            return;
        }
        OK(call_int(vm, list, NJ_C_LIST, "getType", "()I", NULL, &ok) == 3 && ok);
        a[0] = sref(str(vm, "one"));
        OK(call_int(vm, list, NJ_C_LIST, "append", "(Ljava/lang/String;)I",
                    a, &ok) == 0 && ok);
        a[0] = sref(s2);
        OK(call_int(vm, list, NJ_C_LIST, "append", "(Ljava/lang/String;)I",
                    a, &ok) == 1 && ok);
        OK(call_int(vm, list, NJ_C_LIST, "size", "()I", NULL, &ok) == 2 && ok);
        a[0] = iref(1);
        res = call_ref(vm, list, NJ_C_LIST, "getString",
                       "(I)Ljava/lang/String;", a, &ok);
        OK(res == s2 && ok);
        a[0] = iref(0);
        a[1] = sref(str(vm, "ONE"));
        OK(nj_call(vm, list, NJ_C_LIST, "setString", "(ILjava/lang/String;)V",
                   a, NULL) == NJ_OK);
        res = call_ref(vm, list, NJ_C_LIST, "getString",
                       "(I)Ljava/lang/String;", a, &ok);
        OK(res != NULL && nj_string_equals_utf8(res, "ONE"));
        a[0] = iref(1);
        OK(nj_call(vm, list, NJ_C_LIST, "setSelectedIndex", "(I)V", a, NULL) == NJ_OK);
        OK(call_int(vm, list, NJ_C_LIST, "getSelectedIndex", "()I", NULL, &ok) == 1 && ok);
        a[0] = iref(1);
        OK(call_int(vm, list, NJ_C_LIST, "isSelected", "(I)Z", a, &ok) == 1 && ok);
        OK(nj_call(vm, list, NJ_C_LIST, "delete", "(I)V", a, NULL) == NJ_OK);
        OK(call_int(vm, list, NJ_C_LIST, "size", "()I", NULL, &ok) == 1 && ok);
        a[0] = iref(-1);
        (void)nj_call(vm, list, NJ_C_LIST, "setSelectedIndex", "(I)V", a, NULL);
        OK_MSG(exc_is(vm, "java/lang/IndexOutOfBoundsException"),
               "List.setSelectedIndex(-1) did not throw");
        /* set(int, String, Image) rewrites a row in place. */
        a[0] = iref(0);
        a[1] = sref(str(vm, "rewritten"));
        a[2] = sref(NULL);
        OK(nj_call(vm, list, NJ_C_LIST, "set",
                   "(ILjava/lang/String;L" NJ_C_IMAGE ";)V", a, NULL) == NJ_OK);
        a[0] = iref(0);
        res = call_ref(vm, list, NJ_C_LIST, "getString",
                       "(I)Ljava/lang/String;", a, &ok);
        OK(res != NULL && nj_string_equals_utf8(res, "rewritten"));
        /* setSelectedFlags(boolean[]) is void for a List. */
        {
            nj_ref flags = nj_new_array(vm, NULL, 1u, NJ_ARRAY_T_BOOLEAN);
            flags->u.bytes[0] = 1;
            a[0] = sref(flags);
            OK(nj_call(vm, list, NJ_C_LIST, "setSelectedFlags", "([Z)V", a,
                       NULL) == NJ_OK);
            OK(call_int(vm, list, NJ_C_LIST, "getSelectedIndex", "()I", NULL,
                        &ok) == 0 && ok);
        }
        OK(nj_call(vm, list, NJ_C_LIST, "deleteAll", "()V", NULL, NULL) == NJ_OK);
        OK(call_int(vm, list, NJ_C_LIST, "size", "()I", NULL, &ok) == 0 && ok);
        /* The IMPLICIT constructor must have produced a select command. */
        {
            nj_ref sel = static_ref(vm, NJ_C_LIST, "SELECT_COMMAND", D_CMD);
            OK(sel != NULL);
        }
    }

    /* ---- Form + items ---- */
    {
        nj_ref form = construct(vm, NJ_C_FORM, "(Ljava/lang/String;)V",
                                (nj_value[]){ sref(str(vm, "Settings")) });
        nj_ref gauge, cg, tf, df, spacer, si;
        OK(form != NULL);
        if (!form) {
            return;
        }
        a[0] = sref(str(vm, "plain text"));
        OK(call_int(vm, form, NJ_C_FORM, "append", "(Ljava/lang/String;)I",
                    a, &ok) == 0 && ok);

        gauge = construct(vm, NJ_C_GAUGE, "(Ljava/lang/String;ZII)V",
                          (nj_value[]){ sref(str(vm, "Volume")), iref(1),
                                        iref(10), iref(3) });
        OK(gauge != NULL);
        OK(call_int(vm, gauge, NJ_C_GAUGE, "isInteractive", "()Z", NULL, &ok) == 1 && ok);
        OK(call_int(vm, gauge, NJ_C_GAUGE, "getMaxValue", "()I", NULL, &ok) == 10 && ok);
        OK(call_int(vm, gauge, NJ_C_GAUGE, "getValue", "()I", NULL, &ok) == 0 && ok);
        a[0] = iref(7);
        OK(nj_call(vm, gauge, NJ_C_GAUGE, "setValue", "(I)V", a, NULL) == NJ_OK);
        OK(call_int(vm, gauge, NJ_C_GAUGE, "getValue", "()I", NULL, &ok) == 7 && ok);
        a[0] = iref(4);
        (void)nj_call(vm, gauge, NJ_C_GAUGE, "setValue", "(I)V", a, NULL);
        OK(vm->exception == NULL);
        a[0] = iref(99);
        (void)nj_call(vm, gauge, NJ_C_GAUGE, "setValue", "(I)V", a, NULL);
        OK_MSG(exc_is(vm, "java/lang/IllegalArgumentException"),
               "Gauge.setValue(out of range) did not throw");

        /* ChoiceGroup(String label, int type); EXCLUSIVE == 1 in MIDP. */
        cg = construct(vm, NJ_C_CHOICEGROUP, "(Ljava/lang/String;I)V",
                       (nj_value[]){ sref(str(vm, "Pick one")), iref(1) });
        OK(cg != NULL);
        if (cg) {
            a[0] = sref(str(vm, "alpha"));
            OK(call_int(vm, cg, NJ_C_CHOICEGROUP, "append",
                        "(Ljava/lang/String;)I", a, &ok) == 0 && ok);
            a[0] = sref(str(vm, "beta"));
            OK(call_int(vm, cg, NJ_C_CHOICEGROUP, "append",
                        "(Ljava/lang/String;)I", a, &ok) == 1 && ok);
            OK(call_int(vm, cg, NJ_C_CHOICEGROUP, "size", "()I", NULL, &ok) == 2 && ok);
            a[0] = iref(1);
            OK(nj_call(vm, cg, NJ_C_CHOICEGROUP, "setSelectedIndex", "(I)V",
                       a, NULL) == NJ_OK);
            OK(call_int(vm, cg, NJ_C_CHOICEGROUP, "getSelectedIndex", "()I",
                        NULL, &ok) == 1 && ok);
            a[0] = iref(0);
            OK(call_int(vm, cg, NJ_C_CHOICEGROUP, "isSelected", "(I)Z", a,
                        &ok) == 0 && ok);
            a[0] = sref(nj_new_array(vm, NULL, 2u, NJ_ARRAY_T_BOOLEAN));
            OK(call_int(vm, cg, NJ_C_CHOICEGROUP, "getSelectedFlags", "([Z)I",
                        a, &ok) == 1 && ok);
            OK(nj_array_get_int(a[0].r, 0u) == 0 && nj_array_get_int(a[0].r, 1u) == 1);
            /* MIDP's constants: EXCLUSIVE 1, MULTIPLE 2, IMPLICIT 3. */
            {
                nj_field *fe = nj_field_lookup(vm, NJ_C_CHOICEGROUP, "EXCLUSIVE",
                                               "I", 1);
                nj_field *fm = nj_field_lookup(vm, NJ_C_CHOICEGROUP, "MULTIPLE",
                                               "I", 1);
                nj_field *fi = nj_field_lookup(vm, NJ_C_CHOICEGROUP, "IMPLICIT",
                                               "I", 1);
                OK(fe && fm && fi);
                if (fe && fm && fi) {
                    OK(nj_field_read_int(vm, NULL, fe) == 1);
                    OK(nj_field_read_int(vm, NULL, fm) == 2);
                    OK(nj_field_read_int(vm, NULL, fi) == 3);
                }
            }
        }

        tf = construct(vm, NJ_C_TEXTFIELD, "(Ljava/lang/String;Ljava/lang/String;II)V",
                       (nj_value[]){ sref(str(vm, "Name")), sref(str(vm, "abc")),
                                     iref(8), iref(0) });
        OK(tf != NULL);
        if (tf) {
            nj_ref s;
            OK(call_int(vm, tf, NJ_C_TEXTFIELD, "size", "()I", NULL, &ok) == 3 && ok);
            OK(call_int(vm, tf, NJ_C_TEXTFIELD, "getMaxSize", "()I", NULL, &ok) == 8 && ok);
            s = call_ref(vm, tf, NJ_C_TEXTFIELD, "getString",
                         "()Ljava/lang/String;", NULL, &ok);
            OK(s != NULL && ok && nj_string_equals_utf8(s, "abc"));
            a[0] = sref(str(vm, "hello"));
            OK(nj_call(vm, tf, NJ_C_TEXTFIELD, "setString", "(Ljava/lang/String;)V",
                       a, NULL) == NJ_OK);
            s = call_ref(vm, tf, NJ_C_TEXTFIELD, "getString",
                         "()Ljava/lang/String;", NULL, &ok);
            OK(s != NULL && nj_string_equals_utf8(s, "hello"));
            a[0] = sref(nj_char_array_new(vm, 4u));
            OK(call_int(vm, tf, NJ_C_TEXTFIELD, "getChars", "([C)I", a,
                        &ok) == 4 && ok);
            OK(nj_array_get_int(a[0].r, 0u) == 'h');
            OK(nj_array_get_int(a[0].r, 4u) == 0);
            /* insert(String, int) */
            a[0] = sref(str(vm, "AB"));
            a[1] = iref(5);
            OK(nj_call(vm, tf, NJ_C_TEXTFIELD, "insert",
                       "(Ljava/lang/String;I)V", a, NULL) == NJ_OK);
            s = call_ref(vm, tf, NJ_C_TEXTFIELD, "getString",
                         "()Ljava/lang/String;", NULL, &ok);
            OK(s != NULL && nj_string_equals_utf8(s, "helloAB"));
            a[0] = iref(5);
            a[1] = iref(2);
            OK(nj_call(vm, tf, NJ_C_TEXTFIELD, "delete", "(II)V", a, NULL) == NJ_OK);
            /* insert([CII I) */
            {
                nj_ref ins = nj_char_array_new(vm, 2u);
                nj_array_set_int(ins, 0u, 'X');
                nj_array_set_int(ins, 1u, 'Y');
                a[0] = sref(ins);
                a[1] = iref(0);
                a[2] = iref(2);
                a[3] = iref(5);
                OK(nj_call(vm, tf, NJ_C_TEXTFIELD, "insert", "([CIII)V", a,
                           NULL) == NJ_OK);
                s = call_ref(vm, tf, NJ_C_TEXTFIELD, "getString",
                             "()Ljava/lang/String;", NULL, &ok);
                OK_MSG(s && nj_string_equals_utf8(s, "helloXY"), "insert -> %s",
                       s ? "(string)" : "(null)");
            }
            a[0] = iref(5);
            a[1] = iref(2);
            OK(nj_call(vm, tf, NJ_C_TEXTFIELD, "delete", "(II)V", a, NULL) == NJ_OK);
            s = call_ref(vm, tf, NJ_C_TEXTFIELD, "getString",
                         "()Ljava/lang/String;", NULL, &ok);
            OK(s != NULL && nj_string_equals_utf8(s, "hello"));
            a[0] = iref(2);          /* NUMERIC */
            (void)nj_call(vm, tf, NJ_C_TEXTFIELD, "setConstraints", "(I)V", a,
                          NULL);
            OK_MSG(exc_is(vm, "java/lang/IllegalArgumentException"),
                   "NUMERIC constraints accepted for the text \"hello\"");
            a[0] = sref(str(vm, ""));
            OK(nj_call(vm, tf, NJ_C_TEXTFIELD, "setString",
                       "(Ljava/lang/String;)V", a, NULL) == NJ_OK);
            a[0] = iref(2);
            OK(nj_call(vm, tf, NJ_C_TEXTFIELD, "setConstraints", "(I)V", a,
                       NULL) == NJ_OK);
            a[0] = sref(str(vm, "12x"));
            (void)nj_call(vm, tf, NJ_C_TEXTFIELD, "setString",
                          "(Ljava/lang/String;)V", a, NULL);
            OK_MSG(exc_is(vm, "java/lang/IllegalArgumentException"),
                   "NUMERIC TextField accepted '12x'");
            a[0] = sref(str(vm, "too long for the field"));
            (void)nj_call(vm, tf, NJ_C_TEXTFIELD, "setString",
                          "(Ljava/lang/String;)V", a, NULL);
            OK_MSG(exc_is(vm, "java/lang/IllegalArgumentException"),
                   "TextField accepted text longer than maxSize");
        }

        df = construct(vm, NJ_C_DATEFIELD, "(Ljava/lang/String;I)V",
                       (nj_value[]){ sref(str(vm, "When")), iref(3) });
        OK(df != NULL);
        if (df) {
            /* getDate()/setDate() speak java.util.Date, as MIDP specifies. */
            nj_ref when = construct(vm, "java/util/Date", "(J)V",
                                    (nj_value[]){ (nj_value){ .j = 86400000 },
                                                  (nj_value){ .bits = 0 } });
            nj_ref back;
            nj_value da[2];
            OK(when != NULL);
            da[0] = sref(when);
            da[1].bits = 0u;
            OK(nj_call(vm, df, NJ_C_DATEFIELD, "setDate",
                       "(Ljava/util/Date;)V", da, NULL) == NJ_OK);
            back = call_ref(vm, df, NJ_C_DATEFIELD, "getDate",
                            "()Ljava/util/Date;", NULL, &ok);
            OK(back != NULL && ok);
            if (back) {
                nj_value r;
                r.bits = 0u;
                OK(nj_call(vm, back, "java/util/Date", "getTime", "()J", NULL,
                           &r) == NJ_OK);
                OK_MSG(r.j == 86400000, "DateField date = %ld", (long)r.j);
            }
            OK(call_ref(vm, df, NJ_C_DATEFIELD, "getTimeZone",
                        "()Ljava/util/TimeZone;", NULL, &ok) == NULL && ok);
            OK(call_int(vm, df, NJ_C_DATEFIELD, "getInputMode", "()I", NULL,
                        &ok) == -1 && ok);
            da[0].i = 5;
            OK(nj_call(vm, df, NJ_C_DATEFIELD, "setInputMode", "(I)V", da,
                       NULL) == NJ_OK);
            OK(call_int(vm, df, NJ_C_DATEFIELD, "getInputMode", "()I", NULL,
                        &ok) == 5 && ok);
        }

        spacer = construct(vm, NJ_C_SPACER, "(II)V",
                           (nj_value[]){ iref(10), iref(4) });
        OK(spacer != NULL);
        si = construct(vm, NJ_C_STRINGITEM,
                       "(Ljava/lang/String;Ljava/lang/String;I)V",
                       (nj_value[]){ sref(str(vm, "lbl")), sref(str(vm, "txt")),
                                     iref(1) });
        OK(si != NULL);
        /* ImageItem(String label, Image img, int layout, String altText) */
        {
            nj_ref ii = construct(vm, NJ_C_IMAGEITEM,
                                  "(Ljava/lang/String;L" NJ_C_IMAGE ";ILjava/lang/String;)V",
                                  (nj_value[]){ sref(str(vm, "pic")),
                                                sref(image_new(vm, 4, 4)),
                                                iref(0), sref(str(vm, "alt")) });
            nj_ref alt;
            OK(ii != NULL);
            if (ii) {
                alt = call_ref(vm, ii, NJ_C_IMAGEITEM, "getAltText",
                               "()Ljava/lang/String;", NULL, &ok);
                OK(alt != NULL && ok && nj_string_equals_utf8(alt, "alt"));
            }
        }
        /* Form(String, Item[]) */
        {
            nj_ref arr = nj_object_array_new(vm, NJ_C_ITEM, 2u);
            nj_ref f2;
            nj_array_set_ref(arr, 0u, si);
            nj_array_set_ref(arr, 1u, spacer);
            f2 = construct(vm, NJ_C_FORM, "(Ljava/lang/String;[L" NJ_C_ITEM ";)V",
                           (nj_value[]){ sref(str(vm, "Built")), sref(arr) });
            OK(f2 != NULL);
            if (f2) {
                OK(call_int(vm, f2, NJ_C_FORM, "size", "()I", NULL, &ok) == 2 && ok);
            }
        }
        OK(si != NULL);

        /* Form.append(Item) for each widget, then traverse the form. */
        a[0] = sref(gauge);
        OK(call_int(vm, form, NJ_C_FORM, "append", "(L" NJ_C_ITEM ";)I", a,
                    &ok) == 1 && ok);
        a[0] = sref(cg);
        OK(call_int(vm, form, NJ_C_FORM, "append", "(L" NJ_C_ITEM ";)I", a,
                    &ok) == 2 && ok);
        a[0] = sref(tf);
        OK(call_int(vm, form, NJ_C_FORM, "append", "(L" NJ_C_ITEM ";)I", a,
                    &ok) == 3 && ok);
        a[0] = sref(df);
        OK(call_int(vm, form, NJ_C_FORM, "append", "(L" NJ_C_ITEM ";)I", a,
                    &ok) == 4 && ok);
        a[0] = sref(spacer);
        OK(call_int(vm, form, NJ_C_FORM, "append", "(L" NJ_C_ITEM ";)I", a,
                    &ok) == 5 && ok);
        a[0] = sref(si);
        OK(call_int(vm, form, NJ_C_FORM, "append", "(L" NJ_C_ITEM ";)I", a,
                    &ok) == 6 && ok);
        OK(call_int(vm, form, NJ_C_FORM, "size", "()I", NULL, &ok) == 7 && ok);
        a[0] = iref(2);
        OK(call_ref(vm, form, NJ_C_FORM, "get", "(I)L" NJ_C_ITEM ";", a,
                    &ok) == cg && ok);

        /* Item accessors. */
        a[0] = sref(str(vm, "renamed"));
        OK(nj_call(vm, si, NJ_C_ITEM, "setLabel", "(Ljava/lang/String;)V", a,
                   NULL) == NJ_OK);
        {
            nj_ref l = call_ref(vm, si, NJ_C_ITEM, "getLabel",
                                "()Ljava/lang/String;", NULL, &ok);
            OK(l != NULL && ok && nj_string_equals_utf8(l, "renamed"));
        }
        a[0] = iref(24);
        OK(nj_call(vm, si, NJ_C_ITEM, "setPreferredSize", "(II)V", a, NULL) == NJ_OK);
        OK(call_int(vm, si, NJ_C_ITEM, "getMinimumWidth", "()I", NULL, &ok) == 24 && ok);
        a[0] = iref(-5);
        (void)nj_call(vm, si, NJ_C_ITEM, "setPreferredSize", "(II)V", a, NULL);
        OK_MSG(exc_is(vm, "java/lang/IllegalArgumentException"),
               "setPreferredSize(-5,..) did not throw");

        /* delete / insert keep the list coherent. */
        a[0] = iref(0);
        OK(nj_call(vm, form, NJ_C_FORM, "delete", "(I)V", a, NULL) == NJ_OK);
        OK(call_int(vm, form, NJ_C_FORM, "size", "()I", NULL, &ok) == 6 && ok);
        a[0] = iref(0);
        a[1] = sref(si);
        OK(nj_call(vm, form, NJ_C_FORM, "insert", "(IL" NJ_C_ITEM ";)V", a,
                   NULL) == NJ_OK);
        OK(call_int(vm, form, NJ_C_FORM, "size", "()I", NULL, &ok) == 7 && ok);
        a[0] = iref(0);
        OK(call_ref(vm, form, NJ_C_FORM, "get", "(I)L" NJ_C_ITEM ";", a,
                    &ok) == si && ok);
    }

    /* ---- TextBox ---- */
    {
        nj_ref tb = construct(vm, NJ_C_TEXTBOX, "(Ljava/lang/String;Ljava/lang/String;II)V",
                              (nj_value[]){ sref(str(vm, "Compose")),
                                            sref(str(vm, "hi")), iref(16),
                                            iref(0) });
        nj_ref s;
        OK(tb != NULL);
        if (!tb) {
            return;
        }
        OK(call_int(vm, tb, NJ_C_TEXTBOX, "size", "()I", NULL, &ok) == 2 && ok);
        OK(call_int(vm, tb, NJ_C_TEXTBOX, "getMaxSize", "()I", NULL, &ok) == 16 && ok);
        OK(call_int(vm, tb, NJ_C_TEXTBOX, "getConstraints", "()I", NULL, &ok) == 0 && ok);
        a[0] = sref(str(vm, "longer text"));
        OK(nj_call(vm, tb, NJ_C_TEXTBOX, "setString", "(Ljava/lang/String;)V",
                   a, NULL) == NJ_OK);
        s = call_ref(vm, tb, NJ_C_TEXTBOX, "getString", "()Ljava/lang/String;",
                     NULL, &ok);
        OK(s != NULL && ok && nj_string_equals_utf8(s, "longer text"));
        a[0] = sref(nj_char_array_new(vm, 11u));
        OK(call_int(vm, tb, NJ_C_TEXTBOX, "getChars", "([C)I", a, &ok) == 11 && ok);
        OK(nj_array_get_int(a[0].r, 0u) == 'l');
        a[0] = iref(0);
        a[1] = iref(7);
        OK(nj_call(vm, tb, NJ_C_TEXTBOX, "delete", "(II)V", a, NULL) == NJ_OK);
        s = call_ref(vm, tb, NJ_C_TEXTBOX, "getString", "()Ljava/lang/String;",
                     NULL, &ok);
        OK(s != NULL && nj_string_equals_utf8(s, "text"));
        a[0] = iref(4);       /* URL */
        OK(nj_call(vm, tb, NJ_C_TEXTBOX, "setConstraints", "(I)V", a, NULL) == NJ_OK);
        a[0] = sref(str(vm, "http://x"));
        OK(nj_call(vm, tb, NJ_C_TEXTBOX, "setString", "(Ljava/lang/String;)V",
                   a, NULL) == NJ_OK);
        /* An uneditable TextBox keeps its text but refuses a PASSWORD flag
         * combination that violates the constraints. */
        a[0] = sref(str(vm, "with space"));
        (void)nj_call(vm, tb, NJ_C_TEXTBOX, "setString", "(Ljava/lang/String;)V",
                      a, NULL);
        OK_MSG(exc_is(vm, "java/lang/IllegalArgumentException"),
               "URL TextBox accepted a space");
        a[0] = iref(32);
        OK(call_int(vm, tb, NJ_C_TEXTBOX, "setMaxSize", "(I)I", a, &ok) == 32 && ok);
        OK(call_int(vm, tb, NJ_C_TEXTBOX, "getMaxSize", "()I", NULL, &ok) == 32 && ok);
    }

    /* ---- Alert ---- */
    {
        nj_ref alert = construct(vm, NJ_C_ALERT, "(Ljava/lang/String;)V",
                                 (nj_value[]){ sref(str(vm, "Heads up")) });
        nj_ref type;
        nj_ref s;
        OK(alert != NULL);
        if (!alert) {
            return;
        }
        s = call_ref(vm, alert, NJ_C_ALERT, "getString", "()Ljava/lang/String;",
                     NULL, &ok);
        OK(s == NULL || nj_string_len(s) == 0u);   /* no message yet */
        s = call_ref(vm, alert, NJ_C_DISPLAYABLE, "getTitle",
                     "()Ljava/lang/String;", NULL, &ok);
        OK_MSG(s != NULL && ok && nj_string_equals_utf8(s, "Heads up"),
               "Alert title is not the constructor argument");
        a[0] = sref(str(vm, "Low battery"));
        OK(nj_call(vm, alert, NJ_C_ALERT, "setString", "(Ljava/lang/String;)V",
                   a, NULL) == NJ_OK);
        s = call_ref(vm, alert, NJ_C_ALERT, "getString", "()Ljava/lang/String;",
                     NULL, &ok);
        OK(s != NULL && nj_string_equals_utf8(s, "Low battery"));
        /* Alert.FOREVER (-2) is the MIDP default: an Alert is not a banner
         * that disappears after two seconds unless the game asks for it. */
        OK(call_int(vm, alert, NJ_C_ALERT, "getTimeout", "()I", NULL, &ok) == -2 && ok);
        a[0] = iref(5000);
        OK(nj_call(vm, alert, NJ_C_ALERT, "setTimeout", "(I)V", a, NULL) == NJ_OK);
        OK(call_int(vm, alert, NJ_C_ALERT, "getTimeout", "()I", NULL, &ok) == 5000 && ok);
        type = static_ref(vm, NJ_C_ALERTTYPE, "WARNING", D_ALERTTYPE);
        OK(type != NULL);
        a[0] = sref(type);
        OK(nj_call(vm, alert, NJ_C_ALERT, "setType",
                   "(L" NJ_C_ALERTTYPE ";)V", a, NULL) == NJ_OK);
        OK(call_ref(vm, alert, NJ_C_ALERT, "getType",
                    "()L" NJ_C_ALERTTYPE ";", NULL, &ok) == type && ok);
        {
            nj_ref name = call_ref(vm, type, NJ_C_ALERTTYPE, "getTypeName",
                                   "()Ljava/lang/String;", NULL, &ok);
            OK(name != NULL && ok && nj_string_equals_utf8(name, "WARNING"));
        }
        /* playSound must be safe with and without a port tone player. */
        {
            nj_value pa[1];
            pa[0].r = nj_midp_display(vm);
            OK(nj_call(vm, type, NJ_C_ALERTTYPE, "playSound",
                       "(L" NJ_C_DISPLAY ";)Z", pa, NULL) == NJ_OK);
            OK(vm->exception == NULL);
        }
        /* A non-interactive Gauge is the only legal indicator. */
        {
            nj_ref ind = construct(vm, NJ_C_GAUGE, "(Ljava/lang/String;ZII)V",
                                   (nj_value[]){ sref(str(vm, "Wait")), iref(0),
                                                 iref(100), iref(30) });
            a[0] = sref(ind);
            OK(nj_call(vm, alert, NJ_C_ALERT, "setIndicator",
                       "(L" NJ_C_GAUGE ";)V", a, NULL) == NJ_OK);
            OK(call_ref(vm, alert, NJ_C_ALERT, "getIndicator",
                        "()L" NJ_C_GAUGE ";", NULL, &ok) == ind && ok);
        }
        /* These two are STATIC in MIDP, so they are reached with invokestatic. */
        OK(call_static_int(vm, NJ_C_ALERT, "getDefaultTimeout", "()I", NULL,
                           &ok) == 2000 && ok);
        a[0] = iref(3000);
        OK(nj_call_static(vm, NJ_C_ALERT, "setDefaultTimeout", "(I)V", a,
                          NULL) == NJ_OK);
        OK(call_static_int(vm, NJ_C_ALERT, "getDefaultTimeout", "()I", NULL,
                           &ok) == 3000 && ok);
    }
}

/* ------------------------------------------------------------------ */
/* The screen: Display + nj_midp_paint                                 */
/* ------------------------------------------------------------------ */
/* Count how many distinct colours a surface contains. */
static int surface_colours(const nj_surface *s, nj_u4 *first_out)
{
    nj_u4 seen[16];
    int n = 0;
    int x, y;

    for (y = 0; y < s->height && n < 16; y++) {
        for (x = 0; x < s->width && n < 16; x++) {
            nj_u4 c = nj_surface_get(s, x, y);
            int i, found = 0;
            for (i = 0; i < n; i++) {
                if (seen[i] == c) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                seen[n++] = c;
            }
        }
    }
    if (first_out) {
        *first_out = (n > 0) ? seen[0] : 0u;
    }
    return n;
}

static void test_screen(nj_vm *vm)
{
    nj_surface *scr;
    nj_ref form;
    nj_ref list;
    nj_ref alert;
    nj_ref display;
    int ok = 0;

    display = nj_midp_display(vm);
    OK(display != NULL);
    OK(nj_midp_current(vm) == NULL);

    /* ---- the blank frame ---- */
    OK(nj_midp_paint(vm) == NJ_OK);
    scr = nj_midp_screen(vm);
    OK(scr != NULL);
    OK(scr->width > 0 && scr->height > 0);
    OK(scr->stride >= scr->width * scr->bpp);

    /* ---- a Form renders a legible built-in UI ---- */
    form = construct(vm, NJ_C_FORM, "(Ljava/lang/String;)V",
                     (nj_value[]){ sref(str(vm, "Form title")) });
    OK(form != NULL);
    if (form) {
        nj_value a[2];
        a[0] = sref(str(vm, "first line"));
        OK(call_int(vm, form, NJ_C_FORM, "append", "(Ljava/lang/String;)I",
                    a, &ok) == 0 && ok);
        a[0] = sref(str(vm, "second line"));
        OK(call_int(vm, form, NJ_C_FORM, "append", "(Ljava/lang/String;)I",
                    a, &ok) == 1 && ok);
        OK(nj_midp_set_current(vm, form) == NJ_OK);
        OK(nj_midp_current(vm) == form);
        OK(nj_midp_paint(vm) == NJ_OK);
        OK_MSG(surface_colours(scr, NULL) >= 3,
               "a Form frame has only %d colours", surface_colours(scr, NULL));
        /* The title bar occupies the top rows and must not be the page
         * background. */
        OK_MSG(nj_surface_get(scr, 1, 1) != nj_surface_get(scr, 1, scr->height / 2),
               "title bar not drawn");
    }

    /* ---- a List renders rows, and a key moves the selection ---- */
    list = construct(vm, NJ_C_LIST, "(Ljava/lang/String;I)V",
                     (nj_value[]){ sref(str(vm, "Channels")), iref(3) });
    OK(list != NULL);
    if (list) {
        nj_value a[2];
        int i;
        for (i = 0; i < 12; i++) {
            char buf[24];
            snprintf(buf, sizeof(buf), "channel %d", i);
            a[0] = sref(str(vm, buf));
            (void)call_int(vm, list, NJ_C_LIST, "append",
                           "(Ljava/lang/String;)I", a, &ok);
        }
        OK(call_int(vm, list, NJ_C_LIST, "size", "()I", NULL, &ok) == 12 && ok);
        OK(nj_midp_set_current(vm, list) == NJ_OK);
        vm->repaint_pending = 0u;
        OK(nj_midp_paint(vm) == NJ_OK);
        OK_MSG(surface_colours(scr, NULL) >= 3, "a List frame is too uniform");
        /* Scrolling: select the last element and make sure it is on screen. */
        a[0] = iref(11);
        OK(nj_call(vm, list, NJ_C_LIST, "setSelectedIndex", "(I)V", a,
                   NULL) == NJ_OK);
        OK(nj_midp_paint(vm) == NJ_OK);
        OK(call_int(vm, list, NJ_C_LIST, "getSelectedIndex", "()I", NULL,
                    &ok) == 11 && ok);
        /* A key event must reach the screen and be consumed. */
        OK_MSG(nj_midp_key(vm, NJ_KEY_UP, 1) == 1, "keyUp not consumed by a List");
        OK(call_int(vm, list, NJ_C_LIST, "getSelectedIndex", "()I", NULL,
                    &ok) == 10 && ok);
        OK(nj_midp_key(vm, NJ_KEY_UP, 0) == 1);
        OK(nj_midp_paint(vm) == NJ_OK);
        /* An unmapped key still reaches the screen without breaking it. */
        OK(nj_midp_key(vm, NJ_KEY_1, 1) == 1);
        OK(vm->exception == NULL);
    }

    /* ---- TextBox ---- */
    {
        nj_ref tb = construct(vm, NJ_C_TEXTBOX,
                              "(Ljava/lang/String;Ljava/lang/String;II)V",
                              (nj_value[]){ sref(str(vm, "Note")),
                                            sref(str(vm, "some editable text")),
                                            iref(64), iref(0) });
        OK(tb != NULL);
        if (tb) {
            OK(nj_midp_set_current(vm, tb) == NJ_OK);
            OK(nj_midp_paint(vm) == NJ_OK);
            OK_MSG(surface_colours(scr, NULL) >= 3, "a TextBox frame is uniform");
        }
    }

    /* ---- TextBox: key-driven editor (reference parity) ---- */
    /*
     * FreeJ2ME-Plus TextBox.screenKeyPressed is the ground truth the corpus was
     * written for: UP/DOWN cycle one 8-bit character set whose first entry is
     * 'a', FIRE or NUM5 replaces the character under the caret (appending at
     * the end), '*' deletes the character under the caret, '#' inserts a space
     * and LEFT/RIGHT move the caret.  三国杀2．2美化版_1347 types its user name
     * with FIRE taps only, so this path is what unblocks its "创建用户" screen.
     */
    {
        nj_ref tb = construct(vm, NJ_C_TEXTBOX,
                              "(Ljava/lang/String;Ljava/lang/String;II)V",
                              (nj_value[]){ sref(str(vm, "T")),
                                            sref(NULL), iref(10), iref(0) });
        nj_ref s;
        OK(tb != NULL);
        if (tb) {
            OK(nj_midp_set_current(vm, tb) == NJ_OK);
            OK(nj_midp_paint(vm) == NJ_OK);
            OK_MSG(nj_midp_key(vm, NJ_KEY_FIRE, 1) == 1,
                   "FIRE not consumed on a TextBox");
            OK(nj_midp_key(vm, NJ_KEY_FIRE, 0) == 1);
            s = call_ref(vm, tb, NJ_C_TEXTBOX, "getString",
                         "()Ljava/lang/String;", NULL, &ok);
            OK_MSG(s != NULL && ok && nj_string_equals_utf8(s, "a"),
                   "first FIRE did not insert the charset head 'a'");
            OK(call_int(vm, tb, NJ_C_TEXTBOX, "getCaretPosition", "()I",
                        NULL, &ok) == 1 && ok);
            (void)nj_midp_key(vm, NJ_KEY_FIRE, 1);
            (void)nj_midp_key(vm, NJ_KEY_FIRE, 1);
            s = call_ref(vm, tb, NJ_C_TEXTBOX, "getString",
                         "()Ljava/lang/String;", NULL, &ok);
            OK_MSG(s != NULL && ok && nj_string_equals_utf8(s, "aaa"),
                   "three FIRE taps did not type \"aaa\"");
            OK(call_int(vm, tb, NJ_C_TEXTBOX, "getCaretPosition", "()I",
                        NULL, &ok) == 3 && ok);
            /* UP moves the selection to 'b'; FIRE at the end appends it. */
            (void)nj_midp_key(vm, NJ_KEY_UP, 1);
            (void)nj_midp_key(vm, NJ_KEY_FIRE, 1);
            s = call_ref(vm, tb, NJ_C_TEXTBOX, "getString",
                         "()Ljava/lang/String;", NULL, &ok);
            OK_MSG(s != NULL && ok && nj_string_equals_utf8(s, "aaab"),
                   "UP did not cycle the character set to 'b'");
        }
    }
    /* A fresh TextBox selects 'a', so UP then FIRE types 'b'. */
    {
        nj_ref tb = construct(vm, NJ_C_TEXTBOX,
                              "(Ljava/lang/String;Ljava/lang/String;II)V",
                              (nj_value[]){ sref(str(vm, "T")), sref(NULL),
                                            iref(10), iref(0) });
        nj_ref s;
        OK(tb != NULL);
        if (tb) {
            OK(nj_midp_set_current(vm, tb) == NJ_OK);
            (void)nj_midp_key(vm, NJ_KEY_UP, 1);
            (void)nj_midp_key(vm, NJ_KEY_FIRE, 1);
            s = call_ref(vm, tb, NJ_C_TEXTBOX, "getString",
                         "()Ljava/lang/String;", NULL, &ok);
            OK_MSG(s != NULL && ok && nj_string_equals_utf8(s, "b"),
                   "UP + FIRE did not insert 'b'");
        }
    }
    /* '*' deletes the character under the caret; '#' inserts a space there. */
    {
        nj_ref tb = construct(vm, NJ_C_TEXTBOX,
                              "(Ljava/lang/String;Ljava/lang/String;II)V",
                              (nj_value[]){ sref(str(vm, "T")),
                                            sref(str(vm, "ab")), iref(10), iref(0) });
        nj_ref s;
        OK(tb != NULL);
        if (tb) {
            OK(nj_midp_set_current(vm, tb) == NJ_OK);
            /* The caret starts after "ab": move it onto 'b' and delete it. */
            (void)nj_midp_key(vm, NJ_KEY_LEFT, 1);
            (void)nj_midp_key(vm, NJ_KEY_STAR, 1);
            s = call_ref(vm, tb, NJ_C_TEXTBOX, "getString",
                         "()Ljava/lang/String;", NULL, &ok);
            OK_MSG(s != NULL && ok && nj_string_equals_utf8(s, "a"),
                   "'*' did not delete the character under the caret");
            (void)nj_midp_key(vm, NJ_KEY_RIGHT, 1);
            (void)nj_midp_key(vm, NJ_KEY_POUND, 1);
            s = call_ref(vm, tb, NJ_C_TEXTBOX, "getString",
                         "()Ljava/lang/String;", NULL, &ok);
            OK_MSG(s != NULL && ok && nj_string_equals_utf8(s, "a "),
                   "'#' did not insert a space");
        }
    }
    /* maxSize holds: a third FIRE at the end of a full TextBox changes none. */
    {
        nj_ref tb = construct(vm, NJ_C_TEXTBOX,
                              "(Ljava/lang/String;Ljava/lang/String;II)V",
                              (nj_value[]){ sref(str(vm, "T")), sref(NULL),
                                            iref(2), iref(0) });
        nj_ref s;
        OK(tb != NULL);
        if (tb) {
            OK(nj_midp_set_current(vm, tb) == NJ_OK);
            (void)nj_midp_key(vm, NJ_KEY_FIRE, 1);
            (void)nj_midp_key(vm, NJ_KEY_FIRE, 1);
            (void)nj_midp_key(vm, NJ_KEY_FIRE, 1);
            s = call_ref(vm, tb, NJ_C_TEXTBOX, "getString",
                         "()Ljava/lang/String;", NULL, &ok);
            OK_MSG(s != NULL && ok && nj_string_equals_utf8(s, "aa"),
                   "maxSize was not respected by the key editor");
        }
    }
    /* An UNEDITABLE TextBox keeps its Commands: FIRE must not insert text. */
    {
        nj_ref tb = construct(vm, NJ_C_TEXTBOX,
                              "(Ljava/lang/String;Ljava/lang/String;II)V",
                              (nj_value[]){ sref(str(vm, "T")),
                                            sref(str(vm, "read only")),
                                            iref(10), iref(0x20000) });
        nj_ref s;
        OK(tb != NULL);
        if (tb) {
            OK(nj_midp_set_current(vm, tb) == NJ_OK);
            (void)nj_midp_key(vm, NJ_KEY_FIRE, 1);
            s = call_ref(vm, tb, NJ_C_TEXTBOX, "getString",
                         "()Ljava/lang/String;", NULL, &ok);
            OK_MSG(s != NULL && ok && nj_string_equals_utf8(s, "read only"),
                   "an UNEDITABLE TextBox accepted an edit");
        }
    }
    /* A focused Form TextField uses the same editor, including RIGHT-appends. */
    {
        nj_ref form2 = construct(vm, NJ_C_FORM, "(Ljava/lang/String;)V",
                                 (nj_value[]){ sref(str(vm, "Form")) });
        nj_ref tf = construct(vm, NJ_C_TEXTFIELD,
                              "(Ljava/lang/String;Ljava/lang/String;II)V",
                              (nj_value[]){ sref(str(vm, "name")),
                                            sref(str(vm, "")), iref(10), iref(0) });
        nj_ref s;
        OK(form2 != NULL && tf != NULL);
        if (form2 && tf) {
            nj_value fa[1];
            fa[0] = sref(tf);
            OK(call_int(vm, form2, NJ_C_FORM, "append", "(L" NJ_C_ITEM ";)I",
                        fa, &ok) == 0 && ok);
            OK(nj_midp_set_current(vm, form2) == NJ_OK);
            (void)nj_midp_key(vm, NJ_KEY_FIRE, 1);
            (void)nj_midp_key(vm, NJ_KEY_UP, 1);
            (void)nj_midp_key(vm, NJ_KEY_FIRE, 1);
            /* RIGHT at the end of a TextField appends the selected character. */
            (void)nj_midp_key(vm, NJ_KEY_RIGHT, 1);
            s = call_ref(vm, tf, NJ_C_TEXTFIELD, "getString",
                         "()Ljava/lang/String;", NULL, &ok);
            OK_MSG(s != NULL && ok && nj_string_equals_utf8(s, "abb"),
                   "focused TextField editor did not type \"abb\"");
            OK(call_int(vm, tf, NJ_C_TEXTFIELD, "getCaretPosition", "()I",
                        NULL, &ok) == 3 && ok);
            (void)nj_midp_key(vm, NJ_KEY_LEFT, 1);
            (void)nj_midp_key(vm, NJ_KEY_STAR, 1);
            s = call_ref(vm, tf, NJ_C_TEXTFIELD, "getString",
                         "()Ljava/lang/String;", NULL, &ok);
            OK_MSG(s != NULL && ok && nj_string_equals_utf8(s, "ab"),
                   "TextField '*' did not delete the caret character");
        }
    }

    /* ---- Alert ---- */
    alert = construct(vm, NJ_C_ALERT, "(Ljava/lang/String;)V",
                      (nj_value[]){ sref(str(vm, "Low battery")) });
    OK(alert != NULL);
    if (alert) {
        OK(nj_midp_set_current(vm, alert) == NJ_OK);
        OK(nj_midp_paint(vm) == NJ_OK);
        OK_MSG(surface_colours(scr, NULL) >= 3, "an Alert frame is uniform");
    }

    /* ---- MIDP wraps the Alert body text at the panel width ---------- */
    /*
     * A real game's error Alert used to run off the right edge ("java.lang.
     * Except..." on a 176x208 screen) because the renderer drew the string in
     * one run.  MIDP wraps it, so a one-line message and a three-line message
     * must occupy visibly different heights in the content area.
     */
    {
        nj_ref long_alert, short_alert, warning;
        nj_value a[4];
        int short_h = 0, long_h = 0, k, x, y;
        int short_y0 = 1, short_y1 = -1, long_y0 = 1, long_y1 = -1;
        const char *long_text =
            "The quick brown fox jumps over the lazy dog. "
            "The quick brown fox jumps over the lazy dog. "
            "The quick brown fox jumps over the lazy dog.";

        warning = static_ref(vm, NJ_C_ALERTTYPE, "WARNING", D_ALERTTYPE);
        OK(warning != NULL);

        a[0] = sref(str(vm, "Wrap"));
        a[1] = sref(str(vm, "Hi"));
        a[2].r = 0;
        a[3] = sref(warning);
        short_alert = construct(vm, NJ_C_ALERT,
            "(Ljava/lang/String;Ljava/lang/String;" D_IMAGE D_ALERTTYPE ")V", a);

        a[0] = sref(str(vm, "Wrap"));
        a[1] = sref(str(vm, long_text));
        a[2].r = 0;
        a[3] = sref(warning);
        long_alert = construct(vm, NJ_C_ALERT,
            "(Ljava/lang/String;Ljava/lang/String;" D_IMAGE D_ALERTTYPE ")V", a);
        OK(short_alert != NULL && long_alert != NULL);

        for (k = 0; k < 2; k++) {
            nj_ref which = k == 0 ? short_alert : long_alert;

            if (!which) {
                continue;
            }
            OK(nj_midp_set_current(vm, which) == NJ_OK);
            OK(nj_midp_paint(vm) == NJ_OK);
            /* Skip the title bar and the soft-key strip; only body text is
               black on the Alert panel. */
            for (y = 30; y < (int)scr->height - 30; y++) {
                for (x = 0; x < (int)scr->width; x++) {
                    nj_u4 c = nj_surface_get(scr, x, y);

                    if ((c & 0xFFFFFFu) != 0u) {
                        continue;
                    }
                    if (k == 0) {
                        if (y < short_y0) { short_y0 = y; }
                        if (y > short_y1) { short_y1 = y; }
                    } else {
                        if (y < long_y0) { long_y0 = y; }
                        if (y > long_y1) { long_y1 = y; }
                    }
                }
            }
        }
        short_h = short_y1 - short_y0 + 1;
        long_h = long_y1 - long_y0 + 1;
        OK_MSG(short_h > 0, "a one-line Alert draws body text");
        OK_MSG(long_h > short_h * 2,
               "a 3-line Alert wraps (%d px tall) instead of staying one line "
               "(%d px)", long_h, short_h);
    }

    /* ---- back to a Canvas-free Form is still fine ---- */
    OK(nj_midp_set_current(vm, form) == NJ_OK);
    OK(nj_midp_paint(vm) == NJ_OK);

    /* ---- a Canvas subclass would be called; the bare Canvas is abstract ---- */
    {
        nj_ref canvas = nj_object_new(vm, NJ_C_CANVAS);
        OK(canvas != NULL);
        if (canvas) {
            OK(nj_call_special(vm, canvas, NJ_C_CANVAS, "<init>", "()V",
                               NULL, NULL) == NJ_OK);
            OK(call_int(vm, canvas, NJ_C_CANVAS, "getWidth", "()I", NULL,
                        &ok) == (nj_s4)scr->width && ok);
            OK(call_int(vm, canvas, NJ_C_CANVAS, "getHeight", "()I", NULL,
                        &ok) == (nj_s4)scr->height && ok);
            OK(call_int(vm, canvas, NJ_C_CANVAS, "isDoubleBuffered", "()Z",
                        NULL, &ok) == 1 && ok);
            OK(call_int(vm, canvas, NJ_C_CANVAS, "hasPointerEvents", "()Z",
                        NULL, &ok) == ((vm->midp_flags & NJ_MIDP_HAS_POINTER) ? 1 : 0) && ok);
            OK(call_int(vm, canvas, NJ_C_DISPLAYABLE, "isShown", "()Z",
                        NULL, &ok) == 0 && ok);
            OK(nj_midp_set_current(vm, canvas) == NJ_OK);
            /* Launch wrappers query isShown immediately after setCurrent.
             * Delaying it until the first paint can start the MIDlet twice. */
            OK(call_int(vm, canvas, NJ_C_DISPLAYABLE, "isShown", "()Z",
                        NULL, &ok) == 1 && ok);
            OK(call_int(vm, form, NJ_C_DISPLAYABLE, "isShown", "()Z",
                        NULL, &ok) == 0 && ok);
            OK(nj_call(vm, canvas, NJ_C_CANVAS, "setFullScreenMode", "(Z)V",
                       (nj_value[]){ iref(1) }, NULL) == NJ_OK);
            OK(nj_midp_set_current(vm, NULL) == NJ_OK);
            OK(call_int(vm, canvas, NJ_C_DISPLAYABLE, "isShown", "()Z",
                        NULL, &ok) == 0 && ok);
            OK(nj_midp_set_current(vm, form) == NJ_OK);
            OK(call_int(vm, form, NJ_C_DISPLAYABLE, "isShown", "()Z",
                        NULL, &ok) == 1 && ok);
            OK(nj_midp_set_current(vm, canvas) == NJ_OK);
            OK(call_int(vm, canvas, NJ_C_DISPLAYABLE, "isShown", "()Z",
                        NULL, &ok) == 1 && ok);
            /* paint() is abstract on Canvas: a subclass that never overrides it
             * must fail loudly rather than draw a blank frame. */
            OK_MSG(nj_midp_paint(vm) != NJ_OK,
                   "painting a bare Canvas should fail (paint() is abstract)");
            OK_MSG(exc_is(vm, "java/lang/AbstractMethodError"),
                   "expected AbstractMethodError, got %s",
                   pending_exc(vm) ? pending_exc(vm) : "(none)");
            /* The canvas keeps an offscreen buffer at screen size. */
            {
                nj_field *fb = nj_field_lookup(vm, NJ_C_CANVAS,
                                               NJ_F_C_BUFFER, D_IMAGE, 0);
                nj_ref buf = fb ? nj_field_read_ref(vm, canvas, fb) : NULL;
                int bw = 0, bh = 0;
                OK(buf != NULL);
                OK(nj_midp_image_size(vm, buf, &bw, &bh) && bw == scr->width &&
                   bh == scr->height);
            }
            /*
             * Key events reach a bare Canvas and do NOTHING.
             *
             * This assertion used to require AbstractMethodError, on the belief
             * that the input callbacks are abstract like paint().  They are
             * not: MIDP declares only paint() abstract on Canvas, and the
             * input callbacks are concrete with empty bodies.  The old
             * expectation was not merely wrong in theory - it made two real
             * third-party games fail with AbstractMethodError the first time a
             * key was pressed, because a game is entitled to handle only the
             * events it cares about and ignore the rest.
             */
            OK(nj_midp_key(vm, NJ_KEY_UP, 1) == 1);
            OK_MSG(pending_exc(vm) == NULL,
                   "keyPressed on a bare Canvas should do nothing, got %s",
                   pending_exc(vm) ? pending_exc(vm) : "(none)");
        }
    }

    /* ---- the Graphics path a real paint() override would use ---- */
    {
        nj_ref img = image_new(vm, 32, 24);
        nj_ref g;
        nj_value a[4];
        OK(img != NULL);
        g = img ? call_ref(vm, img, NJ_C_IMAGE, "getGraphics",
                           "()L" NJ_C_GRAPHICS ";", NULL, &ok) : NULL;
        OK(g != NULL && ok);
        if (g) {
            /* Fill a known rectangle exactly the way a game would. */
            a[0] = iref((nj_s4)0xFF00A000u);
            OK(nj_call(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL) == NJ_OK);
            a[0] = iref(8);
            a[1] = iref(4);
            a[2] = iref(10);
            a[3] = iref(6);
            OK(nj_call(vm, g, NJ_C_GRAPHICS, "fillRect", "(IIII)V", a, NULL) == NJ_OK);
            OK_MSG(img_pixel(vm, img, 9, 5) == 0xFF00A000u,
                   "paint-path pixel = 0x%08X", img_pixel(vm, img, 9, 5));
            OK_MSG(img_pixel(vm, img, 7, 5) == 0xFFFFFFFFu,
                   "paint-path fill leaked left");
            OK_MSG(img_pixel(vm, img, 18, 5) == 0xFFFFFFFFu,
                   "paint-path fill leaked right");
            OK_MSG(img_pixel(vm, img, 9, 10) == 0xFFFFFFFFu,
                   "paint-path fill leaked below");
        }
    }
}

static void test_canvas_key_methods(nj_vm *vm)
{
    static const char *names[] = { "getGameAction", "getKeyCode", "getKeyName" };
    static const char *descs[] = { "(I)I", "(I)I", "(I)Ljava/lang/String;" };
    nj_class *cls = nj_class_lookup(vm, NJ_C_CANVAS);
    nj_ref canvas = construct(vm, NJ_C_CANVAS, "()V", NULL);
    nj_value a[1], r;
    int ok = 0;
    unsigned i;
    OK(cls != NULL && canvas != NULL);
    if (!cls || !canvas) { return; }
    for (i = 0; i < 3; ++i) {
        /*
         * MIDP 2.0 declares all three static: the assertions below call them
         * with nj_call_static, which is the convention game bytecode uses.
         * getKeyName also keeps an instance alias for older bytecode, so the
         * names themselves only have to resolve.
         */
        OK_MSG(nj_method_lookup(cls, names[i], descs[i]) != NULL ||
               nj_native_find(vm, NJ_C_CANVAS, names[i], descs[i]) != NULL,
               "%s must resolve", names[i]);
    }
    {
        static const int actions[] = { 1, 2, 5, 6, 8, 9, 10, 11, 12 };
        static const int codes[] = {
            NJ_MIDP_KEY_UP, NJ_MIDP_KEY_LEFT, NJ_MIDP_KEY_RIGHT,
            NJ_MIDP_KEY_DOWN, NJ_MIDP_KEY_FIRE, NJ_MIDP_KEY_GAME_A,
            NJ_MIDP_KEY_GAME_B, NJ_MIDP_KEY_GAME_C, NJ_MIDP_KEY_GAME_D
        };
        for (i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i) {
            a[0] = iref(codes[i]);
            OK(call_static_int(vm, NJ_C_CANVAS, "getGameAction", "(I)I", a, &ok) == actions[i] && ok);
            a[0] = iref(actions[i]);
            OK(call_static_int(vm, NJ_C_CANVAS, "getKeyCode", "(I)I", a, &ok) == codes[i] && ok);
        }
    }
    a[0] = iref(NJ_MIDP_KEY_LEFT);
    OK(nj_call_static(vm, NJ_C_CANVAS, "getKeyName", "(I)Ljava/lang/String;", a, &r) == NJ_OK);
    OK(r.r != NULL);
    if (r.r) {
        char text[16];
        nj_string_to_utf8(r.r, text, (int)sizeof(text));
        OK(strcmp(text, "LEFT") == 0);
    }
    /*
     * A key code the device does not define gets the placeholder the
     * reference implementation answers, not an exception: ports ask about
     * their own vendor codes while building help text (see Canvas.getKeyName).
     */
    a[0] = iref(-12345);
    OK(nj_call_static(vm, NJ_C_CANVAS, "getKeyName", "(I)Ljava/lang/String;", a, &r) == NJ_OK);
    OK(r.r != NULL);
    if (r.r) {
        char text[16];
        nj_string_to_utf8(r.r, text, (int)sizeof(text));
        OK(strcmp(text, "-") == 0);
    }
    OK(vm->exception == NULL);
}

/* ------------------------------------------------------------------ */
/* One Graphics per frame, and the same one every frame                */
/* ------------------------------------------------------------------ */
/*
 * Canvas.paint() is handed a Graphics; MIDP implementations give it the
 * platform's graphics rather than minting a new object per frame.  Ours used
 * to allocate one per painted frame, which a heap census showed as one dead
 * javax/microedition/lcdui/Graphics per frame in 超级马里奥_10283 and 160 bytes
 * of heap growth per frame in three other titles.  This test pins both halves:
 * the object is reused, and a second paint allocates nothing.
 */
static nj_ref pg_graphics;
static int pg_paints;

NJ_NATIVE_FN(pg_paint)
{
    (void)th;
    (void)ret;
    pg_paints++;
    pg_graphics = NJ_ARG_REF(args, 0);
    return NJ_OK;
}

static void test_paint_graphics_reused(nj_vm *vm)
{
    static const nj_builtin_method methods[] = {
        { "paint", "(L" NJ_C_GRAPHICS ";)V", NJ_ACC_PUBLIC }
    };
    static const nj_builtin_class fixture = {
        "test/PaintCounter", NJ_C_CANVAS, NJ_ACC_PUBLIC,
        NULL, 0u, methods, 1u
    };
    nj_class *cls;
    nj_ref canvas;
    nj_ref first;
    nj_size used;

    (void)nj_midp_set_current(vm, NULL);
    cls = nj_class_create_builtin(vm, &fixture);
    OK(cls != NULL);
    if (!cls) {
        return;
    }
    cls->methods[0].native = pg_paint;
    canvas = nj_object_new(vm, "test/PaintCounter");
    OK(canvas != NULL);
    if (!canvas) {
        return;
    }
    pg_graphics = NULL;
    pg_paints = 0;
    OK(nj_midp_set_current(vm, canvas) == NJ_OK);

    /* First paint builds the Graphics and gives it to paint(). */
    OK_MSG(nj_midp_paint(vm) == NJ_OK, "the first frame paints");
    OK_MSG(pg_paints == 1, "paint() ran %.1f times on the first frame", (double)pg_paints);
    first = pg_graphics;
    OK(first != NULL);

    /* Second paint: same object, no new allocation. */
    used = nj_heap_used_bytes(vm);
    OK_MSG(nj_midp_paint(vm) == NJ_OK, "the second frame paints");
    OK_MSG(pg_paints == 2, "paint() ran %.1f times over two frames", (double)pg_paints);
    OK_MSG(pg_graphics == first,
           "every frame hands paint() the same Graphics object");
    OK_MSG(nj_heap_used_bytes(vm) == used,
           "a repeated paint allocated %lu bytes, want 0",
           (unsigned long)(nj_heap_used_bytes(vm) - used));
    OK_MSG(!nj_has_exception(vm), "reusing the paint Graphics raises nothing");
}

/* ------------------------------------------------------------------ */
/* callSerially: a self-requeuing runnable runs once per frame         */
/* ------------------------------------------------------------------ */
/*
 * 僵尸危机2_4349's pump (i.run) draws, waits out its own 40ms frame
 * interval, then Display.callSerially(this) to do it again.  A drain that
 * picked up runnables queued *during* the drain executed the whole chain in
 * one paint - 64 x 40ms = 2.56s per frame, 0.4fps, corpus timeout.  MIDP says
 * callSerially runs "before the NEXT paint", so each frame must run the
 * queued runnable exactly once and leave its re-queue for the next frame.
 */
static int cl_runs;

NJ_NATIVE_FN(cl_self_requeue)
{
    nj_ref self = NJ_THIS(args);
    nj_value a[1];

    (void)th;
    (void)ret;
    cl_runs++;
    a[0].r = self;
    (void)nj_call(vm, nj_midp_display(vm), NJ_C_DISPLAY, "callSerially",
                  "(Ljava/lang/Runnable;)V", a, NULL);
    return vm->exception ? NJ_ERR : NJ_OK;
}

static void test_callserially_once_per_frame(nj_vm *vm)
{
    static const nj_builtin_method methods[] = {
        { "run", "()V", NJ_ACC_PUBLIC }
    };
    static const nj_builtin_class fixture = {
        "test/SelfRequeue", "java/lang/Object", NJ_ACC_PUBLIC,
        NULL, 0u, methods, 1u
    };
    nj_class *cls;
    nj_ref disp;
    nj_ref r;
    nj_value a[1];
    int i;

    disp = nj_midp_display(vm);
    OK(disp != NULL);
    /* test_canvas_key_methods leaves a bare Canvas (abstract paint) current;
     * paint() would then fail for reasons unrelated to the drain. */
    OK(nj_midp_set_current(vm, NULL) == NJ_OK);
    cls = nj_class_create_builtin(vm, &fixture);
    OK(cls != NULL);
    if (!disp || !cls) { return; }
    cls->methods[0].native = cl_self_requeue;
    r = nj_object_new(vm, "test/SelfRequeue");
    OK(r != NULL);
    if (!r) { return; }
    cl_runs = 0;

    a[0].r = r;
    OK(nj_call(vm, disp, NJ_C_DISPLAY, "callSerially",
               "(Ljava/lang/Runnable;)V", a, NULL) == NJ_OK);
    OK_MSG(cl_runs == 0, "callSerially does not run the runnable inline");

    for (i = 1; i <= 3; i++) {
        OK_MSG(nj_midp_paint(vm) == NJ_OK,
               "frame %d with a queued runnable paints", i);
        OK_MSG(cl_runs == i,
               "frame %d: self-requeuing runnable ran %d time(s), want %d",
               i, cl_runs, i);
    }
    OK_MSG(!nj_has_exception(vm), "drain finishes without exception");
}

/* ------------------------------------------------------------------ */
/* Alert auto-dismiss timer                                            */
/* ------------------------------------------------------------------ */
/*
 * The timeout is a one-shot millisecond deadline owned by the Alert (the
 * frame-pump equivalent of the reference Alert timer thread), so the tests
 * drive it with a replacement clock rather than sleeping.
 */
static nj_u4 al_fake_ms;
static int al_dispatch_calls;
static nj_ref al_dispatch_command;
static nj_ref al_dispatch_screen;

static nj_u4 al_fake_millis(void *ud)
{
    (void)ud;
    return al_fake_ms;
}

static nj_status al_capture_command(nj_vm *vm, nj_thread *th, nj_value *args,
                                    nj_value *ret)
{
    (void)th;
    (void)ret;
    al_dispatch_calls++;
    al_dispatch_command = NJ_ARG_REF(args, 0);
    al_dispatch_screen = NJ_ARG_REF(args, 1);
    return vm->exception ? NJ_ERR : NJ_OK;
}


/* The specification's Item.getItemCommandListener(), and the StringItem
 * store: the dispatcher reads Item's itCmdListener for every Item, so a
 * StringItem has to write that field too. */
static void test_item_command_listener(nj_vm *vm)
{
    nj_ref si;
    nj_ref listener;
    nj_ref got;
    nj_value a[1];
    int ok;
    nj_field *f;

    si = construct(vm, NJ_C_STRINGITEM,
                   "(Ljava/lang/String;Ljava/lang/String;I)V",
                   (nj_value[]){ sref(str(vm, "lbl")), sref(str(vm, "txt")),
                                 iref(1) });
    OK(si != NULL);
    if (!si) {
        return;
    }
    got = call_ref(vm, si, NJ_C_ITEM, "getItemCommandListener",
                   "()L" NJ_C_ITEMCOMMANDLISTENER ";", NULL, &ok);
    OK(ok);
    OK(got == NULL);
    nj_clear_exception(vm);   /* a missing method must not poison later tests */

    /* An ItemCommandListener is an interface, so the object here is only a
     * ref of the right class: native storage never inspects it. */
    listener = nj_object_new(vm, NJ_C_ITEMCOMMANDLISTENER);
    OK(listener != NULL);
    if (!listener) {
        return;
    }
    a[0] = sref(listener);
    OK(nj_call(vm, si, NJ_C_STRINGITEM, "setItemCommandListener",
               "(L" NJ_C_ITEMCOMMANDLISTENER ";)V", a, NULL) == NJ_OK);
    got = call_ref(vm, si, NJ_C_ITEM, "getItemCommandListener",
                   "()L" NJ_C_ITEMCOMMANDLISTENER ";", NULL, &ok);
    OK(ok);
    OK_MSG(got == listener,
           "getItemCommandListener returns what was set on a StringItem");
    f = nj_field_lookup(vm, NJ_C_ITEM, "itCmdListener",
                        "L" NJ_C_ITEMCOMMANDLISTENER ";", 0);
    OK(f != NULL);
    if (f) {
        OK_MSG(nj_field_read_ref(vm, si, f) == listener,
               "a StringItem stores its listener where the dispatcher reads");
    }
}

static void test_alert_timeout(nj_vm *vm, nj_port *port)
{
    static const nj_builtin_method methods[] = {
        { "commandAction",
          "(L" NJ_C_COMMAND ";L" NJ_C_DISPLAYABLE ";)V", NJ_ACC_PUBLIC }
    };
    static const nj_builtin_class fixture = {
        "test/AlertListener", "java/lang/Object", NJ_ACC_PUBLIC,
        NULL, 0u, methods, 1u
    };
    nj_u4 (*saved_millis)(void *) = port->millis;
    nj_ref display = nj_midp_display(vm);
    nj_ref prev = NULL;
    nj_value a[2];
    int ok = 0;

    OK(display != NULL);
    OK_MSG(saved_millis != NULL, "headless port has no clock");
    if (!display || !saved_millis) {
        return;
    }
    prev = construct(vm, NJ_C_FORM, "(Ljava/lang/String;)V",
                     (nj_value[]){ sref(str(vm, "game screen")) });
    OK(prev != NULL);
    if (!prev) {
        return;
    }
    OK(nj_midp_set_current(vm, prev) == NJ_OK);

    /* Deterministic clock: every timer below is driven by al_fake_ms. */
    al_fake_ms = (nj_u4)nj_now_ms(vm);
    port->millis = al_fake_millis;

    /* (1) The default timeout is FOREVER: an Alert does not dismiss itself
     * unless the game asks for a timeout. */
    {
        nj_ref alert = construct(vm, NJ_C_ALERT, "(Ljava/lang/String;)V",
                                 (nj_value[]){ sref(str(vm, "Hang on")) });
        OK(alert != NULL);
        if (alert) {
            OK(call_int(vm, alert, NJ_C_ALERT, "getTimeout", "()I", NULL,
                        &ok) == -2 && ok);
            a[0] = sref(alert);
            a[1] = sref(prev);
            OK(nj_call(vm, display, NJ_C_DISPLAY, "setCurrent",
                       "(L" NJ_C_ALERT ";L" NJ_C_DISPLAYABLE ";)V", a,
                       NULL) == NJ_OK);
            OK(nj_midp_current(vm) == alert);
            al_fake_ms += 60000u;
            OK(nj_midp_paint(vm) == NJ_OK);
            OK_MSG(nj_midp_current(vm) == alert,
                   "an alert without a timeout dismissed itself");
        }
    }

    /* (2) setTimeout(500): the frame that reaches the deadline returns to the
     * screen the Alert was shown over, exactly once. */
    {
        nj_ref alert = construct(vm, NJ_C_ALERT, "(Ljava/lang/String;)V",
                                 (nj_value[]){ sref(str(vm, "Loading")) });
        OK(alert != NULL);
        if (alert) {
            a[0] = iref(500);
            OK(nj_call(vm, alert, NJ_C_ALERT, "setTimeout", "(I)V", a,
                       NULL) == NJ_OK);
            a[0] = sref(alert);
            a[1] = sref(prev);
            OK(nj_call(vm, display, NJ_C_DISPLAY, "setCurrent",
                       "(L" NJ_C_ALERT ";L" NJ_C_DISPLAYABLE ";)V", a,
                       NULL) == NJ_OK);
            OK(nj_midp_current(vm) == alert);
            al_fake_ms += 499u;
            OK(nj_midp_paint(vm) == NJ_OK);
            OK_MSG(nj_midp_current(vm) == alert,
                   "alert dismissed before its timeout");
            al_fake_ms += 1u;
            OK(nj_midp_paint(vm) == NJ_OK);
            OK_MSG(nj_midp_current(vm) == prev,
                   "alert did not restore the previous screen at its timeout");
            al_fake_ms += 10000u;
            OK(nj_midp_paint(vm) == NJ_OK);
            OK_MSG(nj_midp_current(vm) == prev, "alert timeout fired twice");
            OK(!vm->exception);
        }
    }

    /* (3) Display.setCurrent(Alert) with one argument remembers the screen it
     * covered, so the default command can return to it. */
    {
        nj_ref alert = construct(vm, NJ_C_ALERT, "(Ljava/lang/String;)V",
                                 (nj_value[]){ sref(str(vm, "Cancel?")) });
        OK(alert != NULL);
        if (alert) {
            OK(nj_midp_set_current(vm, prev) == NJ_OK);
            al_fake_ms += 1000u;
            a[0] = iref(300);
            OK(nj_call(vm, alert, NJ_C_ALERT, "setTimeout", "(I)V", a,
                       NULL) == NJ_OK);
            a[0] = sref(alert);
            OK(nj_call(vm, display, NJ_C_DISPLAY, "setCurrent",
                       "(L" NJ_C_DISPLAYABLE ";)V", a, NULL) == NJ_OK);
            OK(nj_midp_current(vm) == alert);
            al_fake_ms += 299u;
            OK(nj_midp_paint(vm) == NJ_OK);
            OK_MSG(nj_midp_current(vm) == alert,
                   "single-argument setCurrent(Alert) dismissed early");
            al_fake_ms += 1u;
            OK(nj_midp_paint(vm) == NJ_OK);
            OK_MSG(nj_midp_current(vm) == prev,
                   "single-argument setCurrent(Alert) forgot the previous "
                   "screen");
        }
    }

    /* (4) A timer that expires while the Alert is not shown does nothing (the
     * reference thread checks isShown()); showing it later must not dismiss
     * it immediately. */
    {
        nj_ref alert = construct(vm, NJ_C_ALERT, "(Ljava/lang/String;)V",
                                 (nj_value[]){ sref(str(vm, "Late")) });
        OK(alert != NULL);
        if (alert) {
            OK(nj_midp_set_current(vm, prev) == NJ_OK);
            a[0] = iref(200);
            OK(nj_call(vm, alert, NJ_C_ALERT, "setTimeout", "(I)V", a,
                       NULL) == NJ_OK);
            al_fake_ms += 5000u;
            OK(nj_midp_paint(vm) == NJ_OK);
            OK(nj_midp_current(vm) == prev);
            a[0] = sref(alert);
            a[1] = sref(prev);
            OK(nj_call(vm, display, NJ_C_DISPLAY, "setCurrent",
                       "(L" NJ_C_ALERT ";L" NJ_C_DISPLAYABLE ";)V", a,
                       NULL) == NJ_OK);
            al_fake_ms += 5000u;
            OK(nj_midp_paint(vm) == NJ_OK);
            OK_MSG(nj_midp_current(vm) == alert,
                   "a stale alert timeout dismissed the alert on show");
            OK(nj_midp_set_current(vm, prev) == NJ_OK);
        }
    }

    /* (5) With a game CommandListener the timeout delivers the left command
     * once on the event-thread equivalent and the listener owns navigation. */
    {
        nj_class *cls = nj_class_create_builtin(vm, &fixture);
        nj_ref alert;
        nj_ref cmd;
        nj_ref listener;

        OK(cls != NULL);
        if (!cls) {
            return;
        }
        cls->methods[0].native = al_capture_command;
        listener = nj_object_new(vm, "test/AlertListener");
        alert = construct(vm, NJ_C_ALERT, "(Ljava/lang/String;)V",
                          (nj_value[]){ sref(str(vm, "Confirm")) });
        cmd = construct(vm, NJ_C_COMMAND, "(Ljava/lang/String;II)V",
                        (nj_value[]){ sref(str(vm, "OK")), iref(4), iref(1) });
        OK(listener != NULL && alert != NULL && cmd != NULL);
        if (listener && alert && cmd) {
            a[0] = sref(cmd);
            OK(nj_call(vm, alert, NJ_C_DISPLAYABLE, "addCommand",
                       "(L" NJ_C_COMMAND ";)V", a, NULL) == NJ_OK);
            a[0] = sref(listener);
            OK(nj_call(vm, alert, NJ_C_DISPLAYABLE, "setCommandListener",
                       "(L" NJ_C_COMMANDLISTENER ";)V", a, NULL) == NJ_OK);
            a[0] = iref(400);
            OK(nj_call(vm, alert, NJ_C_ALERT, "setTimeout", "(I)V", a,
                       NULL) == NJ_OK);
            a[0] = sref(alert);
            a[1] = sref(prev);
            OK(nj_call(vm, display, NJ_C_DISPLAY, "setCurrent",
                       "(L" NJ_C_ALERT ";L" NJ_C_DISPLAYABLE ";)V", a,
                       NULL) == NJ_OK);
            al_dispatch_calls = 0;
            al_dispatch_command = NULL;
            al_dispatch_screen = NULL;
            al_fake_ms += 401u;
            OK(nj_midp_paint(vm) == NJ_OK);
            OK_MSG(al_dispatch_calls == 1,
                   "alert timeout delivered %d commands", al_dispatch_calls);
            OK(al_dispatch_command == cmd);
            OK(al_dispatch_screen == alert);
            OK_MSG(nj_midp_current(vm) == alert,
                   "the built-in listener overrode the game's listener");
            al_fake_ms += 10000u;
            OK(nj_midp_paint(vm) == NJ_OK);
            OK_MSG(al_dispatch_calls == 1, "alert timeout fired twice");
            OK(!vm->exception);
        }
    }

    /* (6) Two game Commands make the Alert modal: setTimeout neither stores
     * the timeout nor starts a timer (the reference "forced modal" rule). */
    {
        nj_ref alert = construct(vm, NJ_C_ALERT, "(Ljava/lang/String;)V",
                                 (nj_value[]){ sref(str(vm, "Pick one")) });
        nj_ref c1 = construct(vm, NJ_C_COMMAND, "(Ljava/lang/String;II)V",
                              (nj_value[]){ sref(str(vm, "A")), iref(4),
                                            iref(1) });
        nj_ref c2 = construct(vm, NJ_C_COMMAND, "(Ljava/lang/String;II)V",
                              (nj_value[]){ sref(str(vm, "B")), iref(1),
                                            iref(2) });
        OK(alert != NULL && c1 != NULL && c2 != NULL);
        if (alert && c1 && c2) {
            a[0] = sref(c1);
            OK(nj_call(vm, alert, NJ_C_DISPLAYABLE, "addCommand",
                       "(L" NJ_C_COMMAND ";)V", a, NULL) == NJ_OK);
            a[0] = sref(c2);
            OK(nj_call(vm, alert, NJ_C_DISPLAYABLE, "addCommand",
                       "(L" NJ_C_COMMAND ";)V", a, NULL) == NJ_OK);
            a[0] = iref(100);
            OK(nj_call(vm, alert, NJ_C_ALERT, "setTimeout", "(I)V", a,
                       NULL) == NJ_OK);
            OK_MSG(call_int(vm, alert, NJ_C_ALERT, "getTimeout", "()I", NULL,
                            &ok) == -2 && ok,
                   "a modal alert stored a timeout");
            a[0] = sref(alert);
            a[1] = sref(prev);
            OK(nj_call(vm, display, NJ_C_DISPLAY, "setCurrent",
                       "(L" NJ_C_ALERT ";L" NJ_C_DISPLAYABLE ";)V", a,
                       NULL) == NJ_OK);
            al_fake_ms += 5000u;
            OK(nj_midp_paint(vm) == NJ_OK);
            OK_MSG(nj_midp_current(vm) == alert,
                   "a modal alert was dismissed by a timeout");
        }
    }

    /* (7) Argument validation matches the reference. */
    {
        nj_ref alert = construct(vm, NJ_C_ALERT, "(Ljava/lang/String;)V",
                                 (nj_value[]){ sref(str(vm, "Args")) });
        OK(alert != NULL);
        if (alert) {
            a[0] = sref(alert);
            a[1].r = NULL;
            OK(nj_call(vm, display, NJ_C_DISPLAY, "setCurrent",
                       "(L" NJ_C_ALERT ";L" NJ_C_DISPLAYABLE ";)V", a,
                       NULL) != NJ_OK);
            OK(exc_is(vm, "java/lang/NullPointerException"));
            nj_clear_exception(vm);
            a[0].r = NULL;
            a[1] = sref(prev);
            OK(nj_call(vm, display, NJ_C_DISPLAY, "setCurrent",
                       "(L" NJ_C_ALERT ";L" NJ_C_DISPLAYABLE ";)V", a,
                       NULL) != NJ_OK);
            OK(exc_is(vm, "java/lang/NullPointerException"));
            nj_clear_exception(vm);
            a[0] = sref(alert);
            a[1] = sref(alert);   /* next must not be an Alert */
            OK(nj_call(vm, display, NJ_C_DISPLAY, "setCurrent",
                       "(L" NJ_C_ALERT ";L" NJ_C_DISPLAYABLE ";)V", a,
                       NULL) != NJ_OK);
            OK(exc_is(vm, "java/lang/IllegalArgumentException"));
            nj_clear_exception(vm);
            a[0] = iref(0);
            OK(nj_call(vm, alert, NJ_C_ALERT, "setTimeout", "(I)V", a,
                       NULL) != NJ_OK);
            OK(exc_is(vm, "java/lang/IllegalArgumentException"));
            nj_clear_exception(vm);
            a[0] = iref(-1);
            OK(nj_call(vm, alert, NJ_C_ALERT, "setTimeout", "(I)V", a,
                       NULL) != NJ_OK);
            OK(exc_is(vm, "java/lang/IllegalArgumentException"));
            nj_clear_exception(vm);
        }
    }

    /* Restore the real clock and leave no Alert on screen for the caller. */
    port->millis = saved_millis;
    OK(nj_midp_set_current(vm, NULL) == NJ_OK);
    {
        nj_field *f = nj_field_lookup(vm, NJ_C_DISPLAY, NJ_F_DISP_ALERT,
                                      "L" NJ_C_DISPLAYABLE ";", 0);
        if (f) {
            nj_field_write_ref(vm, display, f, NULL);
        }
    }
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
static int round_full(const blob *jar, const blob *png, const blob *png_alpha,
                      nj_u4 *released, nj_u4 *peak, nj_u4 *classes)
{
    nj_port port;
    nj_vm *vm;
    nj_app *app = NULL;
    nj_vm_stats stats;
    int rc = 0;

    memset(&port, 0, sizeof(port));
    if (nj_port_headless(&port) != NJ_OK) {
        return 1;
    }
    vm = nj_vm_create(&port, 4u * 1024u * 1024u);
    if (!vm) {
        nj_port_shutdown(&port);
        return 1;
    }
    if (jar && jar->data) {
        app = nj_app_open(vm, jar->data, jar->len);
        if (!app) {
            nj_vm_destroy(vm);
            nj_port_shutdown(&port);
            return 1;
        }
    }

    test_all_methods_bound(vm);
    test_constants(vm);
    test_image(vm, png, png_alpha);
    test_graphics(vm);
    test_font(vm);
    test_command(vm);
    test_widgets(vm);
    test_screen(vm);
    test_canvas_key_methods(vm);
    test_paint_graphics_reused(vm);
    test_callserially_once_per_frame(vm);
    /* Last: it swaps the port clock for a fake one, and the restore leaves the
     * modular clock one wrap ahead - harmless for everything above, so keep
     * this the final test of the round. */
    test_item_command_listener(vm);
    test_alert_timeout(vm, &port);

    /* createImage(String) needs a loaded application. */
    if (app) {
        nj_value a[1];
        int ok = 0;
        nj_ref img;
        a[0] = sref(str(vm, "/lcdui_rgb.png"));
        img = call_static_ref(vm, NJ_C_IMAGE, "createImage",
                              "(Ljava/lang/String;)L" NJ_C_IMAGE ";", a, &ok);
        OK_MSG(img != NULL && ok, "createImage(String) failed: %s",
               pending_exc(vm) ? pending_exc(vm) : "(no exception)");
        if (img) {
            int w = 0, h = 0;
            OK(nj_midp_image_size(vm, img, &w, &h) && w == 13 && h == 7);
        }
        /* A missing resource is an IOException, not a crash. */
        a[0] = sref(str(vm, "/nope.png"));
        (void)call_static_ref(vm, NJ_C_IMAGE, "createImage",
                              "(Ljava/lang/String;)L" NJ_C_IMAGE ";", a, NULL);
        OK_MSG(exc_is(vm, "java/io/IOException"),
               "createImage(missing) did not throw IOException");
    }

    nj_vm_get_stats(vm, &stats);
    if (peak) { *peak = stats.host_peak; }
    if (classes) { *classes = stats.classes; }
    if (app) {
        nj_app_close(vm, app);
    }
    *released = nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    return rc;
}

int main(int argc, char **argv)
{
    blob jar, png, png_alpha;
    char path[512];
    nj_u4 rel[3], peak[3], classes[3];
    int i;

    if (argc > 1) {
        g_fixtures = argv[1];
    }
    memset(&jar, 0, sizeof(jar));
    memset(&png, 0, sizeof(png));
    memset(&png_alpha, 0, sizeof(png_alpha));

    snprintf(path, sizeof(path), "%s/lcdui_res.jar", g_fixtures);
    if (!read_file(path, &jar)) {
        fprintf(stderr, "cannot read %s (run tests/gen_lcdui_fixture.py)\n", path);
        return 2;
    }
    snprintf(path, sizeof(path), "%s/lcdui_rgb.png", g_fixtures);
    if (!read_file(path, &png)) {
        fprintf(stderr, "cannot read %s (run tests/gen_lcdui_fixture.py)\n", path);
        return 2;
    }
    snprintf(path, sizeof(path), "%s/lcdui_rgb_alpha.png", g_fixtures);
    (void)read_file(path, &png_alpha);

    printf("== nano_j2me LCDUI test ==\n");
    printf("fixtures : %s\n", g_fixtures);
    printf("resource jar: %u bytes, png: %u bytes\n", jar.len, png.len);

    for (i = 0; i < 3; i++) {
        if (round_full(&jar, &png, &png_alpha, &rel[i], &peak[i], &classes[i]) != 0) {
            printf("round %d: FAILED to run\n", i + 1);
            free(jar.data);
            free(png.data);
            free(png_alpha.data);
            return 1;
        }
        printf("round %d: %d assertions so far (%d failures), "
               "nj_vm_destroy released %u host blocks\n",
               i + 1, g_pass + g_fail, g_fail, rel[i]);
        /*
         * Every round must start from the same place.  A drift means state
         * survived the teardown - the one thing this runtime must never do.
         */
        if (i > 0 && (rel[i] != rel[0] || classes[i] != classes[0])) {
            printf("FAIL: round %d released %u blocks / %u classes, "
                   "round 1 released %u / %u\n",
                   i + 1, rel[i], classes[i], rel[0], classes[0]);
            g_fail++;
        }
    }
    /* The first round must release something: the accounting has to be real. */
    if (rel[0] == 0u) {
        printf("FAIL: nj_vm_destroy released nothing at all\n");
        g_fail++;
    }

    free(jar.data);
    free(png.data);
    free(png_alpha.data);

    printf("\nassertions: %d passed, %d failed\n", g_pass, g_fail);
    printf("host blocks released: %u / %u / %u (classes %u / %u / %u)\n",
           rel[0], rel[1], rel[2], classes[0], classes[1], classes[2]);
    if (g_fail == 0) {
        printf("PASS: LCDUI (Image, Graphics, Font, widgets, frame) - "
               "3 create/run/destroy rounds, identical teardown\n");
        return 0;
    }
    printf("FAIL: %d assertion(s) failed\n", g_fail);
    return 1;
}
