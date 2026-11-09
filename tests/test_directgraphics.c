/*
 * nano_j2me - tests/test_directgraphics.c
 *
 * com.nokia.mid.ui.DirectUtils and DirectGraphics.
 *
 * Eleven of the titles collected under tests/games call DirectUtils, nearly
 * all of them for createImage(w, h, argb) - and before this existed every one
 * of them died in <init> with NoClassDefFoundError.  A class that merely
 * RESOLVES is not the same as a class that WORKS, so this test draws with
 * every method that is implemented and reads the pixels back:
 *
 *   1. createImage(w, h, argb) really is that colour, everywhere;
 *   2. getDirectGraphics(g) hands back a Graphics that can draw;
 *   3. setARGBColor + fillTriangle fill the triangle and nothing else;
 *   4. fillPolygon / drawPolygon fill the inside but not the outline;
 *   5. getNativePixelFormat describes the actual image or screen surface;
 *   6. int/short drawPixels support Nokia SDK formats, alpha and manipulation;
 *   7. int/short getPixels convert colors, apply translation, ignore clipping,
 *      handle negative scanlength and validate the entire range before writing.
 *
 * Usage:  test_directgraphics [fixtures-dir]      (none needed)
 * Exit code 0 on PASS, non-zero otherwise.
 */
#include "nj2me/nj.h"
#include "../src/midp/nj_midp.h"

#include <stdio.h>
#include <string.h>

#define NJ_C_DUTILS "com/nokia/mid/ui/DirectUtils"

/* Nokia's DirectGraphics.TYPE_USHORT_565_RGB. */
#define NJ_DG_TYPE_USHORT_565_RGB 565

/* The two int formats getPixels(int[], ...) can be asked for. */
#define NJ_DG_TYPE_INT_888_RGB   888
#define NJ_DG_TYPE_INT_8888_ARGB 8888

/* ------------------------------------------------------------------ */
/* Assertion machinery                                                 */
/* ------------------------------------------------------------------ */
static int g_pass;
static int g_fail;

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
/* Small helpers, the same shapes test_lcdui uses                      */
/* ------------------------------------------------------------------ */
static nj_value sref(nj_ref s)
{
    nj_value v;
    v.bits = 0u;
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
    return strcmp(n, cls) == 0;
}

static nj_ref image_new(nj_vm *vm, nj_s4 w, nj_s4 h)
{
    nj_value a[2];
    a[0] = iref(w);
    a[1] = iref(h);
    return call_static_ref(vm, NJ_C_IMAGE, "createImage", "(II)L" NJ_C_IMAGE ";",
                           a, NULL);
}

static nj_ref image_graphics(nj_vm *vm, nj_ref img)
{
    return call_ref(vm, img, NJ_C_IMAGE, "getGraphics",
                    "()L" NJ_C_GRAPHICS ";", NULL, NULL);
}

/* The colour at (x, y), as 0xAARRGGBB. */
static nj_u4 img_pixel(nj_vm *vm, nj_ref img, int x, int y)
{
    nj_ref px = nj_int_array_new(vm, 1u);
    nj_value ga[8];
    nj_u4 v;

    ga[0] = sref(px);
    ga[1] = iref(0);
    ga[2] = iref(1);
    ga[3] = iref(x);
    ga[4] = iref(y);
    ga[5] = iref(1);
    ga[6] = iref(1);
    ga[7] = iref(0);
    if (nj_call(vm, img, NJ_C_IMAGE, "getRGB", "([IIIIIII)V", ga, NULL)
        != NJ_OK) {
        return 0xDEADBEEFu;
    }
    v = (nj_u4)nj_array_get_int(px, 0u);
    return v;
}

/* getWidth returns an int, so it needs the int-returning wrapper. */
static nj_s4 img_width(nj_vm *vm, nj_ref img)
{
    nj_value ret;
    ret.bits = 0u;
    if (nj_call(vm, img, NJ_C_IMAGE, "getWidth", "()I", NULL, &ret) != NJ_OK) {
        return -1;
    }
    return ret.i;
}

static nj_s4 img_height(nj_vm *vm, nj_ref img)
{
    nj_value ret;
    ret.bits = 0u;
    if (nj_call(vm, img, NJ_C_IMAGE, "getHeight", "()I", NULL, &ret) != NJ_OK) {
        return -1;
    }
    return ret.i;
}

/* ------------------------------------------------------------------ */
/* 1. DirectUtils.createImage(w, h, argb)                              */
/* ------------------------------------------------------------------ */
static void test_create_image(nj_vm *vm)
{
    nj_value a[3];
    nj_ref img;
    int x, y, bad = 0;
    nj_s4 w = 20, h = 10;

    a[0] = iref(w);
    a[1] = iref(h);
    a[2] = iref(0xFF00FF00);              /* opaque green */
    img = call_static_ref(vm, NJ_C_DUTILS, "createImage",
                          "(III)L" NJ_C_IMAGE ";", a, NULL);

    OK_MSG(img != NULL, "createImage(20,10,argb) returned null (%s)",
           pending_exc(vm) ? pending_exc(vm) : "no exception");
    if (!img) {
        return;
    }
    OK_MSG(img_width(vm, img) == w, "width = %d, want %d",
           (int)img_width(vm, img), (int)w);
    OK_MSG(img_height(vm, img) == h, "height = %d, want %d",
           (int)img_height(vm, img), (int)h);
    /* Every pixel, including the explicitly supplied opaque alpha. */
    for (y = 0; y < (int)h; y++) {
        for (x = 0; x < (int)w; x++) {
            if (img_pixel(vm, img, x, y) != 0xFF00FF00u) {
                bad++;
            }
        }
    }
    OK_MSG(bad == 0, "%d of %d pixels are not 0xFF00FF00 (first = 0x%08X)",
           bad, (int)(w * h), img_pixel(vm, img, 0, 0));

    /* ... and the two-argument form still works through the same table. */
    a[0] = iref(8);
    a[1] = iref(6);
    img = call_static_ref(vm, NJ_C_DUTILS, "createImage",
                          "(II)L" NJ_C_IMAGE ";", a, NULL);
    OK_MSG(img != NULL, "createImage(8,6) returned null");
    if (img) {
        OK_MSG(img_width(vm, img) == 8 && img_height(vm, img) == 6,
               "createImage(8,6) made %dx%d", (int)img_width(vm, img),
               (int)img_height(vm, img));
    }
}

/* ------------------------------------------------------------------ */
/* 2. getDirectGraphics(g) is a Graphics that draws                    */
/* ------------------------------------------------------------------ */
static void test_get_direct_graphics(nj_vm *vm)
{
    nj_ref img = image_new(vm, 24, 24);
    nj_ref g = image_graphics(vm, img);
    nj_value a[1];
    nj_ref back;

    OK(g != NULL);
    if (!g) {
        return;
    }
    a[0] = sref(g);
    back = call_static_ref(vm, NJ_C_DUTILS, "getDirectGraphics",
                           "(L" NJ_C_GRAPHICS ";)Lcom/nokia/mid/ui/"
                           "DirectGraphics;", a, NULL);
    /*
     * The runtime returns the Graphics itself, which is what makes
     * invokeinterface resolve - see the comment above the interface table in
     * nj_lib_nokia.c.  Identity is the contract, so identity is the assertion.
     */
    OK_MSG(back == g, "getDirectGraphics returned a different object");

    /* A null argument is handed straight back rather than crashing. */
    a[0] = sref(NULL);
    back = call_static_ref(vm, NJ_C_DUTILS, "getDirectGraphics",
                           "(L" NJ_C_GRAPHICS ";)Lcom/nokia/mid/ui/"
                           "DirectGraphics;", a, NULL);
    OK_MSG(back == NULL, "getDirectGraphics(null) invented an object");
    vm->exception = NULL;
}

/* ------------------------------------------------------------------ */
/* 3-6. The DirectGraphics methods, on a real image                    */
/* ------------------------------------------------------------------ */
static void test_drawing(nj_vm *vm)
{
    nj_ref img = image_new(vm, 24, 24);
    nj_ref g = image_graphics(vm, img);
    nj_value a[10];
    nj_ref xs, ys;

    OK(g != NULL);
    if (!g) {
        return;
    }

    /* --- setARGBColor + fillTriangle --- */
    a[0] = iref(0x00FF0000);              /* red, alpha deliberately zero */
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "setARGBColor", "(I)V", a, NULL) == NJ_OK);
    a[0] = iref(0);
    a[1] = iref(0);
    a[2] = iref(23);
    a[3] = iref(0);
    a[4] = iref(0);
    a[5] = iref(23);
    a[6] = iref(0x00FF0000);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "fillTriangle", "(IIIIIII)V", a, NULL)
       == NJ_OK);
    OK_MSG(img_pixel(vm, img, 4, 4) == 0xFFFF0000u,
           "triangle centroid = 0x%08X, want 0xFFFF0000", img_pixel(vm, img, 4, 4));
    OK_MSG(img_pixel(vm, img, 20, 20) != 0xFFFF0000u,
           "fillTriangle painted outside the triangle: (20,20) = 0x%08X",
           img_pixel(vm, img, 20, 20));

    /* --- fillPolygon: a blue square from (8,8) to (16,16) --- */
    {
        int i;
        xs = nj_int_array_new(vm, 4u);
        ys = nj_int_array_new(vm, 4u);
        for (i = 0; i < 4; i++) {
            nj_array_set_int(xs, (nj_u4)i, (8 + (i == 1 || i == 2 ? 8 : 0)));
            nj_array_set_int(ys, (nj_u4)i, (8 + (i >= 2 ? 8 : 0)));
        }
    }
    a[0] = sref(xs);
    a[1] = iref(0);
    a[2] = sref(ys);
    a[3] = iref(0);
    a[4] = iref(4);
    a[5] = iref(0x000000FF);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "fillPolygon", "([II[IIII)V", a, NULL)
       == NJ_OK);
    OK_MSG(img_pixel(vm, img, 12, 12) == 0xFF0000FFu,
           "polygon interior = 0x%08X, want 0xFF0000FF", img_pixel(vm, img, 12, 12));
    OK_MSG(img_pixel(vm, img, 2, 2) != 0xFF0000FFu,
           "fillPolygon painted outside the polygon");

    /* --- drawPolygon on a fresh image must NOT fill --- */
    {
        nj_ref img2 = image_new(vm, 24, 24);
        nj_ref g2 = image_graphics(vm, img2);
        nj_u4 inside;

        a[0] = sref(xs);
        a[1] = iref(0);
        a[2] = sref(ys);
        a[3] = iref(0);
        a[4] = iref(4);
        a[5] = iref(0x00FF00FF);
        OK(nj_call(vm, g2, NJ_C_GRAPHICS, "drawPolygon", "([II[IIII)V", a, NULL)
           == NJ_OK);
        OK_MSG(img_pixel(vm, img2, 8, 8) == 0xFFFF00FFu,
               "drawPolygon outline = 0x%08X, want 0xFFFF00FF",
               img_pixel(vm, img2, 8, 8));
        inside = img_pixel(vm, img2, 12, 12);
        OK_MSG(inside != 0xFFFF00FFu,
               "drawPolygon filled the interior (0x%08X) - it must only outline",
               inside);
    }

    /* --- polygons are bounds-checked, not trusted --- */
    a[0] = sref(xs);
    a[1] = iref(3);                       /* 3 + 4 > length */
    a[2] = sref(ys);
    a[3] = iref(0);
    a[4] = iref(4);
    a[5] = iref(0x00FFFFFF);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "fillPolygon", "([II[IIII)V", a, NULL)
       != NJ_OK);
    OK_MSG(exc_is(vm, "java/lang/ArrayIndexOutOfBoundsException"),
           "out-of-range polygon threw %s",
           pending_exc(vm) ? pending_exc(vm) : "nothing");
    vm->exception = NULL;

    /* --- getNativePixelFormat --- */
    {
        nj_value ret;
        ret.bits = 0u;
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "getNativePixelFormat", "()I", NULL,
                   &ret) == NJ_OK);
        OK_MSG(ret.i == NJ_DG_TYPE_INT_8888_ARGB,
               "getNativePixelFormat = %d, want image ARGB8888 %d",
               (int)ret.i, NJ_DG_TYPE_INT_8888_ARGB);
    }

    /* --- drawPixels: two pixels of 565 red at (2,18) and (3,18) --- */
    {
        nj_ref buf = nj_short_array_new(vm, 2u);
        nj_u2 *p = (nj_u2 *)buf->u.bytes;

        p[0] = 0xF800u;                   /* 565 red */
        p[1] = 0xF800u;
        a[0] = sref(buf);
        a[1] = iref(0);                   /* transparency, ignored */
        a[2] = iref(0);                   /* offset */
        a[3] = iref(2);                   /* scanlength */
        a[4] = iref(2);                   /* x */
        a[5] = iref(18);                  /* y */
        a[6] = iref(2);                   /* w */
        a[7] = iref(1);                   /* h */
        a[8] = iref(0);                   /* manipulation */
        a[9] = iref(NJ_DG_TYPE_USHORT_565_RGB);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawPixels", "([SZIIIIIIII)V", a, NULL)
           == NJ_OK);
        /* Replicate the five source bits to the full eight-bit range. */
        OK_MSG(img_pixel(vm, img, 2, 18) == 0xFFFF0000u,
               "drawPixels(2,18) = 0x%08X, want 0xFFFF0000 (565 red)",
               img_pixel(vm, img, 2, 18));
        OK_MSG(img_pixel(vm, img, 3, 18) == 0xFFFF0000u,
               "drawPixels(3,18) = 0x%08X, want 0xFFFF0000 (565 red)",
               img_pixel(vm, img, 3, 18));
    }

    /* --- getPixels writes the selected rectangle and preserves row padding. --- */
    {
        /*
         * Eight elements with scanlength 4 and a 2x2 read: that touches
         * indices 0, 1, 4 and 5, and 2 and 3 must be left alone.  (A four
         * element array is too small for the same read, which is exactly the
         * check the old no-op version could not exercise.)
         */
        nj_ref buf = nj_short_array_new(vm, 8u);
        nj_u2 *p = (nj_u2 *)buf->u.bytes;
        int i;

        for (i = 0; i < 8; i++) {
            p[i] = 0x1234u;
        }
        a[0] = sref(buf);
        a[1] = iref(0);
        a[2] = iref(4);
        a[3] = iref(2);          /* read back the two 565-red pixels that */
        a[4] = iref(18);         /* drawPixels() put at (2,18) and (3,18) */
        a[5] = iref(2);
        a[6] = iref(2);
        a[7] = iref(NJ_DG_TYPE_USHORT_565_RGB);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "getPixels", "([SIIIIIII)V", a, NULL)
           == NJ_OK);
        OK_MSG(p[0] == 0xF800u,
               "getPixels(2,18) = 0x%04X, want 0xF800 (565 red)", p[0]);
        OK_MSG(p[1] == 0xF800u,
               "getPixels(3,18) = 0x%04X, want 0xF800 (565 red)", p[1]);
        OK_MSG(p[2] == 0x1234u && p[3] == 0x1234u,
               "getPixels touched pixels it was not asked for (0x%04X 0x%04X)",
               p[2], p[3]);
        /* The int[] overload reads the same pixels, as 888 or 8888. */
        {
            nj_ref ibuf = nj_int_array_new(vm, 8u);
            nj_s4 *q = (nj_s4 *)ibuf->u.bytes;
            int k;
            for (k = 0; k < 8; k++) {
                q[k] = 0x12345678;
            }
            a[0] = sref(ibuf);
            a[7] = iref(NJ_DG_TYPE_INT_8888_ARGB);
            OK(nj_call(vm, g, NJ_C_GRAPHICS, "getPixels", "([IIIIIIII)V", a,
                       NULL) == NJ_OK);
            /* Images store ARGB8888; RGB565 red expands to full red. */
            OK_MSG(q[0] == 0xFFFF0000u,
                   "getPixels(int[],8888) = 0x%08X, want 0xFFFF0000 (565 red)",
                   q[0]);
            OK_MSG(q[2] == 0x12345678,
                   "getPixels(int[]) wrote past the rectangle (0x%08X)", q[2]);
        }
    }

}

/* ------------------------------------------------------------------ */
static void test_image_manipulation(nj_vm *vm)
{
    const nj_u4 colors[6] = {
        0xFFFF0000u, 0xFF00FC00u, 0xFF0000F8u,
        0xFFF8FC00u, 0xFF00FCF8u, 0xFFF800F8u
    };
    nj_ref pixels = nj_int_array_new(vm, 6);
    nj_value a[10];
    int i, rotation, flips, clipped;
    for (i = 0; i < 6; ++i) nj_array_set_int(pixels, i, (nj_s4)colors[i]);
    a[0] = sref(pixels); a[1] = iref(3); a[2] = iref(2); a[3] = iref(0);
    nj_ref src = call_static_ref(vm, NJ_C_IMAGE, "createRGBImage",
                                 "([IIIZ)L" NJ_C_IMAGE ";", a, NULL);
    OK(src != NULL);
    for (rotation = 0; rotation < 4; ++rotation) {
        for (flips = 0; flips < 4; ++flips) {
            for (clipped = 0; clipped < 2; ++clipped) {
                nj_ref dst = image_new(vm, 10, 10);
                nj_ref g = image_graphics(vm, dst);
                nj_u4 expected[100];
                int w = rotation % 2 ? 2 : 3, h = rotation % 2 ? 3 : 2;
                int x, y;
                nj_u4 background = img_pixel(vm, dst, 0, 0);
                for (i = 0; i < 100; ++i) expected[i] = background;
                /* Translate, anchor the transformed image at bottom-right,
                 * and clip off its first column, as sprite-atlas users do. */
                a[0] = iref(1); a[1] = iref(2);
                OK(nj_call(vm, g, NJ_C_GRAPHICS, "translate", "(II)V", a, NULL) == NJ_OK);
                if (clipped) {
                    a[0] = iref(7 - w); a[1] = iref(5 - h);
                    a[2] = iref(w - 1); a[3] = iref(h);
                    OK(nj_call(vm, g, NJ_C_GRAPHICS, "setClip", "(IIII)V", a, NULL) == NJ_OK);
                }
                a[0] = sref(src); a[1] = iref(6); a[2] = iref(5);
                a[3] = iref(8 | 32); /* RIGHT | BOTTOM */
                a[4] = iref(rotation * 90 | ((flips & 1) ? 0x2000 : 0) |
                            ((flips & 2) ? 0x4000 : 0));
                OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawImage",
                           "(L" NJ_C_IMAGE ";IIII)V", a, NULL) == NJ_OK);
                for (y = 0; y < 2; ++y) for (x = 0; x < 3; ++x) {
                    int dx = x, dy = y, rw = 3, rh = 2, r;
                    for (r = 0; r < rotation; ++r) {
                        int nx = dy, ny = rw - 1 - dx, tmp = rw;
                        dx = nx; dy = ny; rw = rh; rh = tmp;
                    }
                    if (flips & 1) dx = w - 1 - dx;
                    if (flips & 2) dy = h - 1 - dy;
                    if (!clipped || dx > 0)
                        expected[(7 - h + dy) * 10 + 7 - w + dx] = colors[y * 3 + x];
                }
                for (y = 0; y < 10; ++y) for (x = 0; x < 10; ++x)
                    OK_MSG(img_pixel(vm, dst, x, y) == expected[y * 10 + x],
                           "rotation=%d flips=%d clip=%d pixel=%d,%d",
                           rotation * 90, flips, clipped, x, y);
            }
        }
    }
    {
        const int invalid[] = {-1, 45, 360, 0x1000, 0x8000};
        nj_ref dst = image_new(vm, 10, 10), g = image_graphics(vm, dst);
        a[0] = sref(src); a[1] = iref(0); a[2] = iref(0); a[3] = iref(0);
        for (i = 0; i < (int)(sizeof(invalid) / sizeof(invalid[0])); ++i) {
            a[4] = iref(invalid[i]);
            OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawImage", "(L" NJ_C_IMAGE ";IIII)V", a, NULL) != NJ_OK);
            OK(exc_is(vm, "java/lang/IllegalArgumentException"));
            vm->exception = NULL;
        }
        a[0] = sref(NULL); a[4] = iref(0);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "drawImage", "(L" NJ_C_IMAGE ";IIII)V", a, NULL) != NJ_OK);
        OK(exc_is(vm, "java/lang/NullPointerException"));
        vm->exception = NULL;
    }
}

/* SDK literal formats, independent of the runtime's constant table. */
static nj_status pixels_draw(nj_vm *vm, nj_ref g, nj_ref buf, int ints,
                            int alpha, int off, int scan, int x, int y,
                            int w, int h, int op, int fmt)
{
    nj_value a[10];
    a[0] = sref(buf); a[1] = iref(alpha); a[2] = iref(off); a[3] = iref(scan);
    a[4] = iref(x); a[5] = iref(y); a[6] = iref(w); a[7] = iref(h);
    a[8] = iref(op); a[9] = iref(fmt);
    return nj_call(vm, g, NJ_C_GRAPHICS, "drawPixels",
                   ints ? "([IZIIIIIIII)V" : "([SZIIIIIIII)V", a, NULL);
}

static nj_status pixels_get(nj_vm *vm, nj_ref g, nj_ref buf, int ints,
                           int off, int scan, int x, int y, int w, int h, int fmt)
{
    nj_value a[8];
    a[0] = sref(buf); a[1] = iref(off); a[2] = iref(scan); a[3] = iref(x);
    a[4] = iref(y); a[5] = iref(w); a[6] = iref(h); a[7] = iref(fmt);
    return nj_call(vm, g, NJ_C_GRAPHICS, "getPixels",
                   ints ? "([IIIIIIII)V" : "([SIIIIIII)V", a, NULL);
}

static void pixels_error(nj_vm *vm, nj_status status, const char *exception)
{
    OK(status != NJ_OK);
    OK_MSG(exc_is(vm, exception), "wanted %s, got %s", exception,
           pending_exc(vm) ? pending_exc(vm) : "nothing");
    vm->exception = NULL;
}

static void test_pixel_formats(nj_vm *vm)
{
    static const struct {
        int fmt, ints; nj_u4 packed, argb;
    } cases[] = {
        {888, 1, 0x12123456u, 0xFF123456u},
        {8888, 1, 0x80123456u, 0x80123456u},
        {444, 0, 0xF123u, 0xFF112233u},
        {555, 0, 0xFC1Fu, 0xFFFF00FFu},
        {565, 0, 0x07E0u, 0xFF00FF00u},
        {4444, 0, 0x8123u, 0x88112233u},
        {1555, 0, 0x7C1Fu, 0x00FF00FFu}
    };
    unsigned i; int alpha;
    nj_u4 mark = nj_root_mark(vm);
    nj_ref img = nj_root(vm, image_new(vm, 3, 2));
    nj_ref g = nj_root(vm, image_graphics(vm, img));
    nj_ref ints = nj_root(vm, nj_int_array_new(vm, 8));
    nj_ref shorts = nj_root(vm, nj_short_array_new(vm, 8));
    nj_u4 *raw;
    raw = nj_midp_image_pixels(vm, img);
    OK(raw != NULL);
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        nj_ref buf = cases[i].ints ? ints : shorts;
        for (alpha = 0; alpha < 2; ++alpha) {
            nj_u4 src = cases[i].argb, a, expected; nj_value color;
            raw[0] = 0xFF000000u;
            if (cases[i].ints) ((nj_u4 *)buf->u.bytes)[1] = cases[i].packed;
            else ((nj_u2 *)buf->u.bytes)[1] = (nj_u2)cases[i].packed;
            OK(pixels_draw(vm, g, buf, cases[i].ints, alpha, 1, 1,
                           0, 0, 1, 1, 0, cases[i].fmt) == NJ_OK);
            if (!alpha) src |= 0xFF000000u;
            a = src >> 24;
            expected = 0xFF000000u
                | (((((src >> 16) & 255u) * a + 127u) / 255u) << 16)
                | (((((src >> 8) & 255u) * a + 127u) / 255u) << 8)
                | (((src & 255u) * a + 127u) / 255u);
            OK_MSG(raw[0] == expected, "fmt=%d alpha=%d got=%08X want=%08X",
                   cases[i].fmt, alpha, raw[0], expected);
            OK(nj_call(vm, g, NJ_C_GRAPHICS, "getColor", "()I", NULL, &color) == NJ_OK);
            OK(color.i == 0); /* Pixel operations must preserve current color. */
        }
        /* Read conversion from full ARGB, preserving every non-target slot. */
        raw[0] = cases[i].argb;
        memset(buf->u.bytes, 0x5A, cases[i].ints ? 32u : 16u);
        OK(pixels_get(vm, g, buf, cases[i].ints, 3, -2, 0, 0, 1, 1,
                      cases[i].fmt) == NJ_OK);
        if (cases[i].ints) {
            nj_u4 want = cases[i].fmt == 888 ? cases[i].argb & 0xFFFFFFu : cases[i].argb;
            OK(((nj_u4 *)buf->u.bytes)[3] == want);
            OK(((nj_u4 *)buf->u.bytes)[2] == 0x5A5A5A5Au);
        } else {
            /* 444/555 high unused bits are zero on export. */
            nj_u4 want = cases[i].packed;
            if (cases[i].fmt == 444) want &= 0xFFFu;
            if (cases[i].fmt == 555) want &= 0x7FFFu;
            OK_MSG(((nj_u2 *)buf->u.bytes)[3] == want, "get fmt=%d got=%04X want=%04X",
                   cases[i].fmt, ((nj_u2 *)buf->u.bytes)[3], want);
            OK(((nj_u2 *)buf->u.bytes)[2] == 0x5A5Au);
        }
    }
    /* Both ARGB1555 alpha values, rather than only transparent input. */
    ((nj_u2 *)shorts->u.bytes)[0] = 0xFC1Fu;
    OK(pixels_draw(vm, g, shorts, 0, 1, 0, 1, 0, 0, 1, 1, 0, 1555) == NJ_OK);
    OK(raw[0] == 0xFFFF00FFu);
    nj_root_release(vm, mark);
}

static void test_pixel_manipulations(nj_vm *vm)
{
    static const nj_u4 colors[] = {0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu,
                                  0xFFFFFF00u, 0xFFFF00FFu, 0xFF00FFFFu};
    static const nj_u2 packed[] = {0xF800u, 0x07E0u, 0x001Fu, 0xFFE0u, 0xF81Fu, 0x07FFu};
    int ints, rot, flip, negative, clipped;
    nj_u4 mark = nj_root_mark(vm);
    nj_ref img = nj_root(vm, image_new(vm, 8, 8));
    nj_ref g = nj_root(vm, image_graphics(vm, img));
    nj_ref ib = nj_root(vm, nj_int_array_new(vm, 12));
    nj_ref sb = nj_root(vm, nj_short_array_new(vm, 12));
    nj_u4 *raw; nj_value a[4];
    raw = nj_midp_image_pixels(vm, img);
    OK(raw != NULL);
    a[0] = iref(1); a[1] = iref(2);
    OK(nj_call(vm, g, NJ_C_GRAPHICS, "translate", "(II)V", a, NULL) == NJ_OK);
    for (ints = 0; ints < 2; ++ints) for (rot = 0; rot < 4; ++rot)
    for (flip = 0; flip < 4; ++flip) for (negative = 0; negative < 2; ++negative)
    for (clipped = 0; clipped < 2; ++clipped) {
        nj_u4 want[64]; int sx, sy, k, dw = rot & 1 ? 3 : 2, dh = rot & 1 ? 2 : 3;
        int off = negative ? 9 : 1, scan = negative ? -4 : 4;
        a[0] = iref(clipped ? 1 : -1); a[1] = iref(clipped ? 0 : -2);
        a[2] = iref(clipped ? 2 : 8); a[3] = iref(clipped ? 3 : 8);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "setClip", "(IIII)V", a, NULL) == NJ_OK);
        for (k = 0; k < 64; ++k) raw[k] = want[k] = 0xFFFFFFFFu;
        for (sy = 0; sy < 3; ++sy) for (sx = 0; sx < 2; ++sx) {
            int dx, dy, idx = off + sy * scan + sx;
            ((nj_u4 *)ib->u.bytes)[idx] = colors[sy * 2 + sx];
            ((nj_u2 *)sb->u.bytes)[idx] = packed[sy * 2 + sx];
            if (rot == 0) { dx = sx; dy = sy; }
            else if (rot == 1) { dx = sy; dy = 1 - sx; }
            else if (rot == 2) { dx = 1 - sx; dy = 2 - sy; }
            else { dx = 2 - sy; dy = sx; }
            if (flip & 1) dx = dw - 1 - dx;
            if (flip & 2) dy = dh - 1 - dy;
            dx += 1; dy += 2;
            if (!clipped || (dx >= 2 && dx < 4 && dy >= 2 && dy < 5))
                want[dy * 8 + dx] = colors[sy * 2 + sx];
        }
        OK(pixels_draw(vm, g, ints ? ib : sb, ints, 1, off, scan, 0, 0,
                       2, 3, rot * 90 | (flip << 13), ints ? 8888 : 565) == NJ_OK);
        OK_MSG(memcmp(raw, want, sizeof(want)) == 0,
               "pixels ints=%d rot=%d flip=%d negative=%d", ints, rot, flip, negative);
    }
    /* Reads use translation, ignore clip, support negative scan and preserve holes. */
    {
        int ints2, k;
        for (k = 0; k < 64; ++k) raw[k] = colors[k % 6];
        for (ints2 = 0; ints2 < 2; ++ints2) {
            nj_ref buf = ints2 ? ib : sb;
            memset(buf->u.bytes, 0x5A, ints2 ? 48u : 24u);
            OK(pixels_get(vm, g, buf, ints2, 9, -4, 0, 0, 2, 3, ints2 ? 8888 : 565) == NJ_OK);
            for (k = 0; k < 12; ++k) {
                int row = (9 - k + 1) / 4;
                int selected = (k == 1 || k == 2 || k == 5 || k == 6 || k == 9 || k == 10);
                nj_u4 want = ints2 ? 0x5A5A5A5Au : 0x5A5Au;
                if (selected) {
                    int col = k - (9 - row * 4);
                    int ci = ((row + 2) * 8 + col + 1) % 6;
                    want = ints2 ? colors[ci] : packed[ci];
                }
                OK((ints2 ? ((nj_u4 *)buf->u.bytes)[k] : ((nj_u2 *)buf->u.bytes)[k]) == want);
            }
        }
    }
    nj_root_release(vm, mark);
}

static void test_pixel_errors(nj_vm *vm)
{
    static const struct {int off, scan, x, y, w, h, op, fmt; const char *exc;} bad[] = {
        {0,2,0,0,2,2,45,8888,"java/lang/IllegalArgumentException"},
        {0,2,0,0,2,2,0,12,"java/lang/IllegalArgumentException"},
        {0,2,0,0,-1,2,0,8888,"java/lang/IllegalArgumentException"},
        {0,2,0,0,2,-1,0,8888,"java/lang/IllegalArgumentException"},
        {-1,2,0,0,2,2,0,8888,"java/lang/ArrayIndexOutOfBoundsException"},
        {1,2,0,0,2,2,0,8888,"java/lang/ArrayIndexOutOfBoundsException"},
        {0,-2,0,0,2,2,0,8888,"java/lang/ArrayIndexOutOfBoundsException"},
        {0,2147483647,0,0,2,2,0,8888,"java/lang/ArrayIndexOutOfBoundsException"},
        {2147483647,2,0,0,2,2,0,8888,"java/lang/ArrayIndexOutOfBoundsException"},
        {0,2,-1,0,2,2,0,8888,"java/lang/IllegalArgumentException"},
        {0,2,0,-1,2,2,0,8888,"java/lang/IllegalArgumentException"},
        {0,2,3,0,2,2,0,8888,"java/lang/IllegalArgumentException"}
    };
    int ints, i, k; nj_u4 mark = nj_root_mark(vm);
    nj_ref img = nj_root(vm, image_new(vm, 4, 4)), g = nj_root(vm, image_graphics(vm, img));
    nj_ref ib = nj_root(vm, nj_int_array_new(vm, 4)), sb = nj_root(vm, nj_short_array_new(vm, 4));
    nj_u4 *raw;
    raw = nj_midp_image_pixels(vm, img);
    OK(raw != NULL);
    for (ints = 0; ints < 2; ++ints) {
        nj_ref buf = ints ? ib : sb;
        pixels_error(vm, pixels_draw(vm,g,NULL,ints,0,0,2,0,0,2,2,0,ints?8888:565), "java/lang/NullPointerException");
        pixels_error(vm, pixels_get(vm,g,NULL,ints,0,2,0,0,2,2,ints?8888:565), "java/lang/NullPointerException");
        for (i = 0; i < (int)(sizeof(bad)/sizeof(bad[0])); ++i) {
            int fmt = bad[i].fmt == 8888 ? (ints ? 8888 : 565) : bad[i].fmt;
            for (k = 0; k < 16; ++k) raw[k] = 0xFF123456u;
            memset(buf->u.bytes, 0x5A, ints ? 16u : 8u);
            if (i < 9) {
                pixels_error(vm, pixels_draw(vm,g,buf,ints,1,bad[i].off,bad[i].scan,bad[i].x,bad[i].y,
                                              bad[i].w,bad[i].h,bad[i].op,fmt), bad[i].exc);
                for (k = 0; k < 16; ++k) OK(raw[k] == 0xFF123456u);
            }
            if (i == 0) continue; /* getPixels has no manipulation argument. */
            pixels_error(vm, pixels_get(vm,g,buf,ints,bad[i].off,bad[i].scan,bad[i].x,bad[i].y,
                                         bad[i].w,bad[i].h,fmt), bad[i].exc);
            for (k = 0; k < 4; ++k)
                OK((ints ? ((nj_u4 *)buf->u.bytes)[k] : ((nj_u2 *)buf->u.bytes)[k]) == (ints ? 0x5A5A5A5Au : 0x5A5Au));
        }
        /* Large destination coordinates must clip without signed overflow. */
        OK(pixels_draw(vm,g,buf,ints,0,0,2,2147483647,2147483647,2,2,0,ints?8888:565) == NJ_OK);
        /* Zero dimensions do not access even an otherwise invalid offset. */
        OK(pixels_draw(vm,g,buf,ints,0,-2147483647,0,0,0,0,2,0,ints?8888:565) == NJ_OK);
        OK(pixels_get(vm,g,buf,ints,2147483647,0,0,0,0,2,ints?8888:565) == NJ_OK);
        OK(pixels_draw(vm,g,buf,ints,0,2147483647,0,0,0,2,0,0,ints?8888:565) == NJ_OK);
        OK(pixels_get(vm,g,buf,ints,-2147483647,0,0,0,2,0,ints?8888:565) == NJ_OK);
        pixels_error(vm,pixels_draw(vm,g,buf,ints,0,0,2,0,0,2,2,(-2147483647-1),ints?8888:565),
                     "java/lang/IllegalArgumentException");
        pixels_error(vm,pixels_draw(vm,g,buf,ints,0,0,2,0,0,2,2,0,-8888),
                     "java/lang/IllegalArgumentException");
        pixels_error(vm,pixels_draw(vm,g,buf,ints,0,0,2,0,0,2,2,0,ints?565:8888),
                     "java/lang/IllegalArgumentException");
        pixels_error(vm,pixels_get(vm,g,buf,ints,0,2,0,0,2,2,ints?565:8888),
                     "java/lang/IllegalArgumentException");
        pixels_error(vm,pixels_draw(vm,g,buf,ints,0,0,2147483647,2147483647,0,2147483647,2147483647,0,ints?8888:565),
                     "java/lang/ArrayIndexOutOfBoundsException");
        pixels_error(vm,pixels_get(vm,g,buf,ints,0,(-2147483647-1),0,0,1,2,ints?8888:565),
                     "java/lang/ArrayIndexOutOfBoundsException");
        /* Repeated scanlines can describe huge but mostly clipped rectangles. */
        if (ints) ((nj_u4 *)buf->u.bytes)[0] = 0xFFFF0000u;
        else ((nj_u2 *)buf->u.bytes)[0] = 0xF800u;
        OK(pixels_draw(vm,g,buf,ints,0,0,0,0,-2147483646,1,2147483647,0,ints?8888:565) == NJ_OK);
        OK(raw[0] == 0xFFFF0000u);
    }
    {
        nj_value a[2];
        a[0] = iref(1); a[1] = iref(-1);
        OK(nj_call(vm,g,NJ_C_GRAPHICS,"translate","(II)V",a,NULL) == NJ_OK);
        memset(ib->u.bytes, 0x5A, 16);
        pixels_error(vm,pixels_get(vm,g,ib,1,0,2,-1,1,2,2,8888), "java/lang/IllegalArgumentException");
        pixels_error(vm,pixels_get(vm,g,ib,1,0,2,0,0,2,2,8888), "java/lang/IllegalArgumentException");
        for (k = 0; k < 4; ++k) OK(((nj_u4 *)ib->u.bytes)[k] == 0x5A5A5A5Au);
    }
    nj_root_release(vm, mark);
}

static void test_pixel_screen(nj_vm *vm)
{
    nj_u4 mark = nj_root_mark(vm);
    nj_ref img = nj_root(vm, image_new(vm, 2, 2));
    nj_ref g = nj_root(vm, image_graphics(vm, img));
    nj_ref buf = nj_root(vm, nj_int_array_new(vm, 4));
    nj_surface saved = vm->screen;
    nj_u2 saved_w = vm->screen_w, saved_h = vm->screen_h;
    nj_u4 storage[4];
    const int formats[] = {NJ_PIXFMT_RGB565, NJ_PIXFMT_RGB888, NJ_PIXFMT_ARGB8888};
    const int native[] = {565, 888, 8888};
    int f; nj_field *target = NJ_F(vm, NJ_C_GRAPHICS, NJ_F_G_TARGET, "L" NJ_C_IMAGE ";");
    nj_field_write_ref(vm, g, target, NULL);
    for (f = 0; f < 3; ++f) {
        nj_value ret;
        vm->screen_w = vm->screen_h = 2;
        vm->screen.width = vm->screen.height = 2;
        vm->screen.bpp = f == 0 ? 2 : f == 1 ? 3 : 4;
        vm->screen.stride = 2 * vm->screen.bpp;
        vm->screen.format = formats[f]; vm->screen.pixels = storage;
        nj_surface_set(&vm->screen, 0, 0, 0xFF00FF00u);
        OK(nj_call(vm,g,NJ_C_GRAPHICS,"getNativePixelFormat","()I",NULL,&ret) == NJ_OK);
        OK(ret.i == native[f]);
        OK(pixels_get(vm,g,buf,1,0,2,0,0,1,1,8888) == NJ_OK);
        OK(((nj_u4 *)buf->u.bytes)[0] == 0xFF00FF00u);
    }
    vm->screen = saved;
    vm->screen_w = saved_w; vm->screen_h = saved_h;
    nj_root_release(vm, mark);
}

int main(void)
{
    nj_port port;
    nj_vm *vm;

    memset(&port, 0, sizeof(port));
    if (nj_port_headless(&port) != NJ_OK) {
        fprintf(stderr, "cannot create the headless port\n");
        return 1;
    }
    vm = nj_vm_create(&port, 4u * 1024u * 1024u);
    if (!vm) {
        nj_port_shutdown(&port);
        fprintf(stderr, "cannot create the VM\n");
        return 1;
    }

    test_create_image(vm);
    test_get_direct_graphics(vm);
    test_drawing(vm);
    test_image_manipulation(vm);
    test_pixel_formats(vm);
    test_pixel_manipulations(vm);
    test_pixel_errors(vm);
    test_pixel_screen(vm);

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);

    printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
