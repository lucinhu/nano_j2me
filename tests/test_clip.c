/*
 * nano_j2me - tests/test_clip.c
 *
 * setClip() and translate() are the two calls a game uses to draw a small
 * picture inside a big canvas, and the corpus has a class of title that
 * presents frame after frame of one flat colour with paint() running every
 * frame (see --stats paints=).  The obvious way to get that wrong is to apply
 * the clip in the wrong coordinate space: translate first and a clip that is
 * stored in device pixels lands somewhere the drawing never reaches, so every
 * draw is discarded and the screen stays the colour it started as.
 *
 * This pins both orders down, and pins the empty clip as well:
 *
 *   1. translate(8,8) then setClip(0,0,8,8) clips at the translated origin;
 *   2. setClip(0,0,8,8) then translate(8,8) clips at the same place;
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
 * Paint the whole image red, then draw a green square that a clip of 8x8 at
 * the translated origin (8,8) must confine to (8..15, 8..15).
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
        img = image_new(vm, 32, 32);
        OK(img != NULL);
        g = image_graphics(vm, img);
        OK(g != NULL);
        if (!img || !g) {
            continue;
        }
        paint_and_clip(vm, g, phase);

        OK_MSG(pixel(vm, img, 8, 8) == GREEN, "(%d,8) phase %d", 8, phase);
        OK_MSG(pixel(vm, img, 15, 15) == GREEN, "(15,15) phase %d", phase);
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
