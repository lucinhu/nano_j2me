/* VM integration: physical offscreen content survives a blit, SSAA resolves
 * all four channels, and a dead Java pixel buffer releases its companion. */
#include "nj2me/nj.h"
#include "nj_midp.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "%s:%d: %s\n", \
    __FILE__, __LINE__, #test); exit(1); } } while (0)

static unsigned frames;
static nj_u4 first;
static void capture(void *ud, const nj_surface *s)
{
    (void)ud;
    CHECK(s->width == 24 && s->height == 16 && s->format == NJ_PIXFMT_ARGB8888);
    first = nj_surface_get(s, 0, 0);
    ++frames;
}

/* A scroll overlaps on both axes. Each physical sample is unique so a
 * stale mirror, a wrong row order or a nearest upscale fails the check. */
static void check_image_copies(nj_vm *vm, nj_ref image, nj_surface *logical, nj_surface *hd)
{
    nj_value a[7] = {{0}}, result;
    nj_ref graphics, rotated;
    nj_image view;
    const nj_surface *copy;
    for (int y = 0; y < hd->height; ++y) {
        for (int x = 0; x < hd->width; ++x)
            nj_surface_set(hd, x, y, 0xff000000u | (unsigned)y * 256u | (unsigned)x);
    }
    CHECK(nj_call(vm, image, NJ_C_IMAGE, "getGraphics", "()L" NJ_C_GRAPHICS ";", NULL, &result) == NJ_OK);
    graphics = nj_root(vm, result.r);
    a[2].i = 5; a[3].i = 3; a[4].i = 1; a[5].i = 1;
    a[6].i = NJ_ANCHOR_LEFT | NJ_ANCHOR_TOP;
    CHECK(nj_call(vm, graphics, NJ_C_GRAPHICS, "copyArea", "(IIIIIII)V", a, NULL) == NJ_OK);
    for (int y = 4; y < hd->height; ++y) {
        for (int x = 4; x < hd->width; ++x)
            CHECK(nj_surface_get(hd, x, y) == (0xff000000u | (unsigned)(y-4) * 256u | (unsigned)(x-4)));
    }
    a[0].r = image; a[1].i = 1; a[2].i = 1; a[3].i = 3; a[4].i = 2; a[5].i = NJ_TRANS_ROT90;
    CHECK(nj_call_static(vm, NJ_C_IMAGE, "createImage", "(L" NJ_C_IMAGE ";IIIII)L" NJ_C_IMAGE ";", a, &result) == NJ_OK);
    rotated = nj_root(vm, result.r);
    CHECK(nj_midp_image_size(vm, rotated, &view.width, &view.height));
    view.pixels = nj_midp_image_pixels(vm, rotated);
    CHECK(view.pixels && view.width == 2 && view.height == 3);
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 2; ++x)
        CHECK(view.pixels[y*2+x] == nj_surface_get(logical, 1+y, 2-x));
    copy = nj_midp_hd_source(vm, &view);
    CHECK(copy && copy->width == 8 && copy->height == 12);
    for (int y = 0; y < 12; ++y) for (int x = 0; x < 8; ++x)
        CHECK(nj_surface_get(copy, x, y) == nj_surface_get(hd, 4+y, 11-x));
}

int main(void)
{
    nj_port port = {0};
    nj_vm *vm;
    nj_ref image;
    nj_surface surface, *screen;
    nj_render_mirror *src, *dst;
    nj_image view = {0};
    nj_gfx g;
    const void *key;
    nj_u4 mark;
    CHECK(nj_port_headless_ex(&port, 12, 8, 0, NULL) == NJ_OK);
    port.render_scale = 2; port.render_ssaa = 2; port.display_hd = capture;
    vm = nj_vm_create(&port, 1024u * 1024u);
    CHECK(vm);
    screen = nj_midp_screen(vm);
    CHECK(screen && screen->width == 12 && screen->height == 8);
    dst = nj_midp_hd_target(vm, screen);
    CHECK(dst && dst->scale == 4 && dst->target->width == 48);
    mark = nj_root_mark(vm);
    image = nj_root(vm, nj_midp_image_new(vm, 6, 4, 1));
    CHECK(image && nj_midp_surface(vm, image, &surface) == &surface);
    nj_midp_gfx_init(vm, &g, &surface);
    src = g.mirror;
    CHECK(src && src->scale == 4);
    nj_gfx_clear(&g, 0xff000000u);
    nj_gfx_set_color(&g, 0xffffffffu);
    nj_gfx_line(&g, 0, 0, 4, 3);
    key = surface.pixels;
    CHECK(dst->lookup(dst->ud, key, 6, 4) == src->target);
    nj_gc_collect(vm);
    CHECK(dst->lookup(dst->ud, key, 6, 4) == src->target);
    view.pixels = surface.pixels; view.width = 6; view.height = 4;
    view.format = NJ_PIXFMT_ARGB8888; view.has_alpha = 0;
    nj_midp_gfx_init(vm, &g, screen);
    nj_gfx_draw_image(&g, &view, 1, 1, 0, 0, 6, 4);
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 24; ++x)
        CHECK(nj_surface_get(dst->target, x+4, y+4) == nj_surface_get(src->target, x, y));
    /* Four distinct channels exercise byte-lane rounding independently. */
    nj_surface_set(dst->target, 0, 0, 0x0000ff04u);
    nj_surface_set(dst->target, 0, 1, 0x00ff0000u);
    nj_surface_set(dst->target, 1, 0, 0xff000000u);
    nj_surface_set(dst->target, 1, 1, 0xff000000u);
    CHECK(nj_midp_hd_present(vm) && frames == 1 && first == 0x80404001u);
    nj_surface_set(screen, 0, 0, 0xff000000u);
    nj_midp_hd_invalidate(vm, screen);
    CHECK(nj_midp_hd_present(vm) && frames == 2 && first == 0xff000000u);
    check_image_copies(vm, image, &surface, src->target);
    nj_root_release(vm, mark);
    nj_gc_collect(vm);
    CHECK(dst->lookup(dst->ud, key, 6, 4) == NULL);
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    return 0;
}
