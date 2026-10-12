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
 *   5. getNativePixelFormat agrees with the pixel format the port was built
 *      with - if that ever stops being RGB565 the constant has to move;
 *   6. drawPixels puts the colours where they were asked for;
 *   7. getPixels is the one method that is deliberately not implemented yet,
 *      and this asserts the CURRENT behaviour (the array is left alone) so
 *      that whoever implements it cannot do so silently.
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
#define NJ_DG_TYPE_USHORT_565_RGB 10

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
    nj_value ga[7];
    nj_u4 v;

    ga[0] = sref(px);
    ga[1] = iref(0);
    ga[2] = iref(1);
    ga[3] = iref(x);
    ga[4] = iref(y);
    ga[5] = iref(1);
    ga[6] = iref(1);
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
    a[2] = iref(0x0000FF00);              /* opaque green, alpha zero on purpose */
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
    /*
     * Every pixel, not just the corner.  The alpha the caller passed is zero,
     * which would draw nothing at all; the runtime ORs it opaque, and this is
     * the assertion that says so out loud.
     */
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
        OK_MSG(ret.i == NJ_DG_TYPE_USHORT_565_RGB,
               "getNativePixelFormat = %d, want %d (the format the port was "
               "built with)", (int)ret.i, NJ_DG_TYPE_USHORT_565_RGB);
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
        /*
         * 0xF800 is 565 red, and it comes back as 0xFFF80000, not 0xFFFF0000:
         * the image stores RGB565, so five bits of red cannot survive a round
         * trip through 888.  The expected value is what the FORMAT makes of
         * the input, not what a reader assumes it means - the same trap that
         * once made a correct tan background look like a bug.
         */
        OK_MSG(img_pixel(vm, img, 2, 18) == 0xFFF80000u,
               "drawPixels(2,18) = 0x%08X, want 0xFFF80000 (565 red)",
               img_pixel(vm, img, 2, 18));
        OK_MSG(img_pixel(vm, img, 3, 18) == 0xFFF80000u,
               "drawPixels(3,18) = 0x%08X, want 0xFFF80000 (565 red)",
               img_pixel(vm, img, 3, 18));
    }

    /* --- getPixels: the documented gap, asserted so it cannot hide --- */
    {
        nj_ref buf = nj_short_array_new(vm, 4u);
        nj_u2 *p = (nj_u2 *)buf->u.bytes;
        int i;

        for (i = 0; i < 4; i++) {
            p[i] = 0x1234u;
        }
        a[0] = sref(buf);
        a[1] = iref(0);
        a[2] = iref(4);
        a[3] = iref(0);
        a[4] = iref(0);
        a[5] = iref(2);
        a[6] = iref(2);
        a[7] = iref(NJ_DG_TYPE_USHORT_565_RGB);
        OK(nj_call(vm, g, NJ_C_GRAPHICS, "getPixels", "([SIIIIIII)V", a, NULL)
           == NJ_OK);
        /*
         * nj_gfx exposes no way to read a surface back, so getPixels is a
         * no-op that does NOT throw - a game reading the screen gets stale
         * data rather than an exception it cannot catch.  When someone adds a
         * read primitive this assertion is the one that must change.
         */
        OK_MSG(p[0] == 0x1234u,
               "getPixels wrote something (0x%04X): if the gap is closed, "
               "update this test rather than deleting it", p[0]);
    }

}

/* ------------------------------------------------------------------ */
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

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);

    printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
