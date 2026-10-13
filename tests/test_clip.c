/*
 * nano_j2me - tests/test_clip.c
 *
 * setClip() and translate() are the two calls a game uses to draw a small
 * picture inside a big canvas, and the corpus has a class of title that
 * presents frame after frame of one flat colour with paint() running every
 * frame (see --stats paints=).  The obvious way to get that wrong is to drag
 * the clip along with the origin: the clip is a rectangle of PIXELS on the
 * target, and MIDP 2.0 says so in Graphics.translate - "Operations on the
 * coordinate system, such as translate(), do not modify the clip".
 *
 * 帝国时代3-亚洲王朝_6288 is what that costs.  It draws its map by translating
 * the origin by the camera offset and then blitting tiles at map coordinates;
 * with the clip dragged to (-336,-216) every tile was clipped away and the
 * campaign showed a black field with only the untranslated HUD on it.
 *
 * This pins the rule down, and pins the empty clip as well:
 *
 *   1. translate(8,8) then setClip(0,0,8,8) clips at device (8..15, 8..15),
 *      because setClip() takes coordinates in the CURRENT system;
 *   2. setClip(0,0,8,8) then translate(8,8) does NOT: the clip stays at device
 *      (0..7, 0..7) and the drawing, which now lands at (8,8) and beyond, is
 *      discarded;
 *   3. everything outside the clip keeps the colour underneath it;
 *   4. a zero-sized clip discards the drawing and keeps what was there.
 *
 * Usage:  test_clip            (no fixtures needed)
 * Exit code 0 on PASS, non-zero otherwise.
 */
#include "nj2me/nj.h"
#include "../src/midp/nj_midp.h"

#include <stdio.h>
#include <string.h>

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
            fprintf(stderr, "\n");                                            \
        }                                                                     \
    } while (0)

#define RED   0xFFFF0000u
#define GREEN 0xFF00FF00u

static nj_value iref(nj_s4 v)
{
    nj_value x;
    x.bits = 0u;
    x.i = v;
    return x;
}

static nj_ref call_ref(nj_vm *vm, nj_ref obj, const char *cls, const char *name,
                       const char *desc, nj_value *args)
{
    nj_value ret;
    ret.bits = 0u;
    if (nj_call(vm, obj, cls, name, desc, args, &ret) != NJ_OK) {
        return NULL;
    }
    return ret.r;
}

static nj_ref image_new(nj_vm *vm, nj_s4 w, nj_s4 h)
{
    nj_value a[2];
    a[0] = iref(w);
    a[1] = iref(h);
    return call_ref(vm, NULL, NJ_C_IMAGE, "createImage",
                    "(II)L" NJ_C_IMAGE ";", a);
}

static nj_ref image_graphics(nj_vm *vm, nj_ref img)
{
    return call_ref(vm, img, NJ_C_IMAGE, "getGraphics",
                    "()L" NJ_C_GRAPHICS ";", NULL);
}

static void g_call(nj_vm *vm, nj_ref g, const char *name, const char *desc,
                   nj_s4 a, nj_s4 b, nj_s4 c, nj_s4 d)
{
    nj_value args[4];
    args[0] = iref(a);
    args[1] = iref(b);
    args[2] = iref(c);
    args[3] = iref(d);
    (void)call_ref(vm, g, NJ_C_GRAPHICS, name, desc, args);
}

static void set_color(nj_vm *vm, nj_ref g, nj_u4 argb)
{
    nj_value args[1];
    args[0] = iref((nj_s4)argb);
    (void)call_ref(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", args);
}

static nj_u4 pixel(nj_vm *vm, nj_ref img, int x, int y)
{
    nj_ref px = nj_int_array_new(vm, 1u);
    nj_value ga[8];
    ga[0].bits = 0u; ga[0].r = px;
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
    return (nj_u4)nj_array_get_int(px, 0u);
}

/*
 * Paint the whole image red, then draw a green square that a clip of 8x8 must
 * confine.  With the clip set first the clip is at the origin and translate()
 * leaves it there, so the green square (which lands at device (8,8)..(40,40))
 * misses it entirely; with translate first the clip follows the current
 * coordinate system to (8..15, 8..15) and confines the square to it.
 */
static void paint_and_clip(nj_vm *vm, nj_ref g, int clip_first)
{
    set_color(vm, g, RED);
    g_call(vm, g, "fillRect", "(IIII)V", 0, 0, 32, 32);
    if (clip_first) {
        g_call(vm, g, "setClip", "(IIII)V", 0, 0, 8, 8);
        g_call(vm, g, "translate", "(II)V", 8, 8, 0, 0);
    } else {
        g_call(vm, g, "translate", "(II)V", 8, 8, 0, 0);
        g_call(vm, g, "setClip", "(IIII)V", 0, 0, 8, 8);
    }
    set_color(vm, g, GREEN);
    g_call(vm, g, "fillRect", "(IIII)V", 0, 0, 32, 32);
}

int main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_ref img, g;
    int phase;

    nj_port_headless(&port);
    vm = nj_vm_create(&port, 1024u * 1024u);
    if (!vm) {
        fprintf(stderr, "FAIL: no VM\n");
        return 1;
    }

    for (phase = 0; phase < 2; phase++) {
        /* phase 0: translate() first, then setClip().  phase 1: the other way. */
        int clip_first = (phase == 1);
        img = image_new(vm, 32, 32);
        OK(img != NULL);
        g = image_graphics(vm, img);
        OK(g != NULL);
        if (!img || !g) {
            continue;
        }
        paint_and_clip(vm, g, clip_first);

        if (clip_first) {
            /*
             * setClip(0,0,8,8) then translate(8,8): the clip is a rectangle of
             * pixels and a move of the coordinate system does not move it, so
             * the green square - drawn at device (8,8) and beyond - misses the
             * clip at the origin entirely and nothing of it is drawn.
             */
            OK_MSG(pixel(vm, img, 8, 8) == RED,
                   "(8,8) after setClip-then-translate is 0x%08lX",
                   (unsigned long)pixel(vm, img, 8, 8));
            OK_MSG(pixel(vm, img, 15, 15) == RED,
                   "(15,15) after setClip-then-translate is 0x%08lX",
                   (unsigned long)pixel(vm, img, 15, 15));
        } else {
            /*
             * translate(8,8) then setClip(0,0,8,8): setClip() takes coordinates
             * in the CURRENT system, so the clip lands on device (8..15) and
             * confines the square to it.
             */
            OK_MSG(pixel(vm, img, 8, 8) == GREEN, "(8,8) phase %d", phase);
            OK_MSG(pixel(vm, img, 15, 15) == GREEN, "(15,15) phase %d", phase);
        }
        OK_MSG(pixel(vm, img, 7, 7) == RED, "(7,7) phase %d is 0x%08lX",
               phase, (unsigned long)pixel(vm, img, 7, 7));
        OK_MSG(pixel(vm, img, 16, 16) == RED, "(16,16) phase %d", phase);
        OK_MSG(pixel(vm, img, 0, 0) == RED, "(0,0) phase %d", phase);
    }

    /* An empty clip must discard drawing, not clip to the whole image. */
    img = image_new(vm, 16, 16);
    g = image_graphics(vm, img);
    set_color(vm, g, RED);
    g_call(vm, g, "fillRect", "(IIII)V", 0, 0, 16, 16);
    g_call(vm, g, "setClip", "(IIII)V", 4, 4, 0, 0);
    set_color(vm, g, GREEN);
    g_call(vm, g, "fillRect", "(IIII)V", 0, 0, 16, 16);
    OK_MSG(pixel(vm, img, 4, 4) == RED, "zero clip let a draw through");

    printf("%d checks, %d failures\n", g_pass, g_fail);
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    return g_fail == 0 ? 0 : 1;
}
