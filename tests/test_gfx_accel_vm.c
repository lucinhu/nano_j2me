/* Headless integration: actual builtin Graphics/Image/Sprite calls, no MIDlet
 * or external game. The synchronous backend runs on the CPU; no GPU is tested.
 * Link to nj2me with exactly the same NJ_ENABLE_GFX_ACCEL/config definitions.
 */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include "../src/midp/nj_midp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
    exit(1); } } while (0)
#define D_IMAGE "Ljavax/microedition/lcdui/Image;"
#define D_GFX "Ljavax/microedition/lcdui/Graphics;"
typedef struct probe {
    unsigned fills, blits, depth;
    int decline, check_fill, transform;
    void *pixels;
} probe;

#if NJ_ENABLE_GFX_ACCEL
static int cpu_fill(void *ud, const nj_gfx *g, int x, int y, int w, int h)
{
    probe *p = (probe *)ud;
    int col, row;
    CHECK(p && p->depth == 0 && p->pixels == g->surf->pixels);
    CHECK(g->surf->format == NJ_PIXFMT_ARGB8888 && g->surf->bpp == 4);
    ++p->fills;
    if (p->check_fill) {
        CHECK(x == 2 && y == 2 && w == 3 && h == 2);
        CHECK(g->translate_x == 1 && g->translate_y == 1);
        CHECK(g->color == 0xff123456u);
    }
    if (p->decline) return NJ_GFX_ACCEL_DECLINE;
    CHECK(!g->use_xor && (g->color >> 24) == 255);
    /* Independent opaque fill verifies the borrowed surface is authoritative. */
    for (row = y; row < y+h; ++row) for (col = x; col < x+w; ++col)
        memcpy((nj_u1 *)g->surf->pixels + row*g->surf->stride + col*4,
               &g->color, 4);
    return NJ_GFX_ACCEL_DONE;
}
static int cpu_blit(void *ud, const nj_gfx *g, const nj_gfx_blit *b)
{
    probe *p = (probe *)ud;
    CHECK(p && p->depth == 0 && p->pixels == g->surf->pixels);
    CHECK(b->format == NJ_PIXFMT_ARGB8888 && b->width == 2 && b->height == 3);
    CHECK(b->transform == p->transform && b->rop == NJ_ROP_PAINT && b->alpha == 255);
    ++p->blits;
    if (p->decline) return NJ_GFX_ACCEL_DECLINE;
    ++p->depth;
    nj_gfx_blit_software(g, b);
    --p->depth;
    return NJ_GFX_ACCEL_DONE;
}
static const nj_gfx_accel backend = { NJ_GFX_ACCEL_VERSION, cpu_fill, cpu_blit };
#endif

static nj_value integer(int n)
{
    nj_value v; v.bits = 0; v.i = n; return v;
}
static nj_value reference(nj_ref r)
{
    nj_value v; v.bits = 0; v.r = r; return v;
}
static void invoke(nj_vm *vm, nj_ref o, const char *cls, const char *method,
                   const char *desc, nj_value *args)
{
    CHECK(nj_call(vm, o, cls, method, desc, args, NULL) == NJ_OK);
}
static nj_ref mutable_image(nj_vm *vm)
{
    nj_value a[2], r;
    a[0] = integer(8); a[1] = integer(8); r.bits = 0;
    CHECK(nj_call_static(vm, NJ_C_IMAGE, "createImage", "(II)" D_IMAGE, a, &r) == NJ_OK);
    CHECK(r.r != NULL); return nj_root(vm, r.r);
}
static nj_ref graphics(nj_vm *vm, nj_ref image)
{
    nj_value r; r.bits = 0;
    CHECK(nj_call(vm, image, NJ_C_IMAGE, "getGraphics", "()" D_GFX, NULL, &r) == NJ_OK);
    CHECK(r.r != NULL); return nj_root(vm, r.r);
}
static nj_ref source_image(nj_vm *vm, nj_u4 color[6])
{
    nj_ref array = nj_root(vm, nj_int_array_new(vm, 6));
    nj_value a[4], r;
    int i;
    CHECK(array != NULL);
    for (i = 0; i < 6; ++i) nj_array_set_int(array, (nj_u4)i, (nj_s4)color[i]);
    a[0] = reference(array); a[1] = integer(2); a[2] = integer(3); a[3] = integer(1);
    r.bits = 0;
    CHECK(nj_call_static(vm, NJ_C_IMAGE, "createRGBImage", "([IIIZ)" D_IMAGE, a, &r) == NJ_OK);
    CHECK(r.r != NULL); return nj_root(vm, r.r);
}
static void verify(nj_vm *vm, nj_ref image, const nj_u4 expected[64])
{
    nj_ref array = nj_root(vm, nj_int_array_new(vm, 64));
    nj_value a[7];
    int i;
    CHECK(array != NULL);
    a[0] = reference(array); a[1] = integer(0); a[2] = integer(8);
    a[3] = a[4] = integer(0); a[5] = a[6] = integer(8);
    invoke(vm, image, NJ_C_IMAGE, "getRGB", "([IIIIIII)V", a);
    for (i = 0; i < 64; ++i) {
        nj_u4 got = (nj_u4)nj_array_get_int(array, (nj_u4)i);
        if (got != expected[i]) fprintf(stderr, "pixel (%d,%d): %08x expected %08x\n", i%8, i/8, got, expected[i]);
        CHECK(got == expected[i]);
    }
}
static void run_calls(nj_vm *vm, probe *p)
{
    nj_ref image = mutable_image(vm), g = graphics(vm, image), src, sprite;
    nj_u4 color[6] = {0xff153351u, 0xff346987u, 0xff486a93u,
                      0xff7193b5u, 0xff86a1c2u, 0xffa2c3e5u};
    nj_u4 expected[64];
    nj_value a[4];
    nj_surface scratch, *surface;
    unsigned before;
    int x, y, mode;
    surface = nj_midp_surface(vm, image, &scratch);
    CHECK(surface != NULL); p->pixels = surface->pixels;
    for (x = 0; x < 64; ++x) expected[x] = 0xffffffffu;
    a[0] = integer(1); a[1] = integer(1);
    invoke(vm, g, NJ_C_GRAPHICS, "translate", "(II)V", a);
    a[2] = integer(4); a[3] = integer(4);
    invoke(vm, g, NJ_C_GRAPHICS, "setClip", "(IIII)V", a);
    a[0] = integer(0x123456);
    invoke(vm, g, NJ_C_GRAPHICS, "setColor", "(I)V", a);
    p->check_fill = 1;
    for (mode = 0; mode < 2; ++mode) {
        p->decline = mode;
        a[0] = a[1] = integer(0); a[2] = integer(4); a[3] = integer(3);
        before = p->fills;
        invoke(vm, g, NJ_C_GRAPHICS, "fillRect", "(IIII)V", a);
        CHECK(p->fills == before+(unsigned)NJ_ENABLE_GFX_ACCEL);
        for (y = 2; y < 4; ++y) for (x = 2; x < 5; ++x) expected[y*8+x] = 0xff123456u;
        verify(vm, image, expected);
    }
    p->check_fill = 0; p->decline = 0;
    /* Reset transforms through real Java API, then drawImage at TOP|LEFT. */
    a[0] = a[1] = integer(-1);
    invoke(vm, g, NJ_C_GRAPHICS, "translate", "(II)V", a);
    a[0] = a[1] = integer(0); a[2] = a[3] = integer(8);
    invoke(vm, g, NJ_C_GRAPHICS, "setClip", "(IIII)V", a);
    src = source_image(vm, color);
    for (mode = 0; mode < 2; ++mode) {
        p->decline = mode; p->transform = NJ_TRANS_NONE;
        a[0] = reference(src); a[1] = integer(0); a[2] = integer(mode*4);
        a[3] = integer(NJ_ANCHOR_TOP | NJ_ANCHOR_LEFT);
        before = p->blits;
        invoke(vm, g, NJ_C_GRAPHICS, "drawImage", "(" D_IMAGE "III)V", a);
        CHECK(p->blits == before+(unsigned)NJ_ENABLE_GFX_ACCEL);
        for (y = 0; y < 3; ++y) for (x = 0; x < 2; ++x) expected[(mode*4+y)*8+x] = color[y*2+x];
        verify(vm, image, expected);
    }
    /* Game's Sprite.paint constructs its own nj_gfx; this catches missed port
     * binding in that module even when Graphics.drawImage itself is correct. */
    sprite = nj_root(vm, nj_object_new(vm, NJ_C_SPRITE));
    CHECK(sprite != NULL); a[0] = reference(src);
    CHECK(nj_call_special(vm, sprite, NJ_C_SPRITE, "<init>", "(" D_IMAGE ")V", a, NULL) == NJ_OK);
    a[0] = integer(NJ_TRANS_ROT90);
    invoke(vm, sprite, NJ_C_SPRITE, "setTransform", "(I)V", a);
    for (mode = 0; mode < 2; ++mode) {
        p->decline = mode; p->transform = NJ_TRANS_ROT90;
        a[0] = integer(4); a[1] = integer(mode*4);
        invoke(vm, sprite, NJ_C_LAYER, "setPosition", "(II)V", a);
        a[0] = reference(g); before = p->blits;
        invoke(vm, sprite, NJ_C_SPRITE, "paint", "(" D_GFX ")V", a);
        CHECK(p->blits == before+(unsigned)NJ_ENABLE_GFX_ACCEL);
        /* Independent clockwise placement: (u,v) -> (2-v,u). */
        for (y = 0; y < 3; ++y) for (x = 0; x < 2; ++x)
            expected[(mode*4+x)*8+4+2-y] = color[y*2+x];
        verify(vm, image, expected);
    }
    CHECK(p->depth == 0);
}
int main(void)
{
    nj_port ports[2];
    nj_vm *vms[2];
    probe probes[2];
    unsigned fills, blits;
    nj_u4 released[2];
    int i;
    memset(probes, 0, sizeof probes);
    for (i = 0; i < 2; ++i) {
        memset(&ports[i], 0, sizeof ports[i]);
        CHECK(nj_port_headless(&ports[i]) == NJ_OK);
#if NJ_ENABLE_GFX_ACCEL
        ports[i].gfx_accel = &backend; ports[i].gfx_accel_ud = &probes[i];
#endif
        vms[i] = nj_vm_create(&ports[i], 4u*1024u*1024u);
        CHECK(vms[i] != NULL);
    }
    run_calls(vms[0], &probes[0]);
    CHECK(probes[1].fills == 0 && probes[1].blits == 0);
    fills = probes[0].fills; blits = probes[0].blits;
    run_calls(vms[1], &probes[1]);
    CHECK(probes[0].fills == fills && probes[0].blits == blits);
    for (i = 0; i < 2; ++i) {
        CHECK(probes[i].fills == (unsigned)(2*NJ_ENABLE_GFX_ACCEL));
        CHECK(probes[i].blits == (unsigned)(4*NJ_ENABLE_GFX_ACCEL));
        released[i] = nj_vm_destroy(vms[i]);
        CHECK(released[i] > 0);
        nj_port_shutdown(&ports[i]);
    }
    CHECK(released[0] == released[1]);
    printf("PASS gfx accel VM=%d: %u checks, two isolated VMs, fillRect/drawImage/Sprite accepted+declined (no GPU tested)\n",
           NJ_ENABLE_GFX_ACCEL, checks);
    return 0;
}
