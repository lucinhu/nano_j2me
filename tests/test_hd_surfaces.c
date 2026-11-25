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

/* Compare every pixel with a snapshot, including pixels outside the clip.
 * The HD grid has its own pattern, so upscaling the logical result won't pass. */
static void check_scroll(nj_vm *vm, nj_ref graphics, nj_surface *grid, int scale,
                         int sx, int sy, int dx, int dy, int anchor)
{
    nj_u4 before[48 * 32];
    nj_value a[7] = {{0}};
    const int w = 5, h = 3, tx = 1, ty = 1;
    CHECK(grid->width * grid->height <= (int)(sizeof(before) / sizeof(before[0])));
    for (int y = 0; y < grid->height; ++y) for (int x = 0; x < grid->width; ++x) {
        nj_u4 alpha = x % 3 == 0 ? 0u : x % 3 == 1 ? 0x80000000u : 0xff000000u;
        nj_surface_set(grid, x, y, alpha | (unsigned)x * 0x5020u | (unsigned)y * 7u);
        before[y * grid->width + x] = nj_surface_get(grid, x, y);
    }
    /* Coordinates here are in device space. The caller translated by (1,1). */
    a[0].i = sx - tx; a[1].i = sy - ty; a[2].i = w; a[3].i = h;
    a[4].i = dx - tx; a[5].i = dy - ty; a[6].i = anchor;
    if (anchor & NJ_ANCHOR_RIGHT) a[4].i += w;
    if (anchor & NJ_ANCHOR_HCENTER) a[4].i += w / 2;
    if (anchor & NJ_ANCHOR_BOTTOM) a[5].i += h;
    if (anchor & NJ_ANCHOR_VCENTER) a[5].i += h / 2;
    CHECK(nj_call(vm, graphics, NJ_C_GRAPHICS, "copyArea", "(IIIIIII)V", a, NULL) == NJ_OK);
    for (int y = 0; y < grid->height; ++y) for (int x = 0; x < grid->width; ++x) {
        int from = y * grid->width + x;
        int lx = x / scale, ly = y / scale;
        if (lx >= 1 && lx < 6 && ly < 4 && lx >= dx && lx < dx + w && ly >= dy && ly < dy + h)
            from += ((sy - dy) * grid->width + sx - dx) * scale;
        nj_u4 got = nj_surface_get(grid, x, y);
        if (got != before[from])
            fprintf(stderr, "copyArea scale %d at (%d,%d): got %08x expected %08x\n",
                    scale, x, y, (unsigned)got, (unsigned)before[from]);
        CHECK(got == before[from]);
    }
}

static void check_copy_area(nj_vm *vm, nj_ref graphics, nj_surface *logical, nj_surface *hd)
{
    nj_value a[7] = {{0}};
    a[0].i = 1; a[1].i = 1;
    CHECK(nj_call(vm, graphics, NJ_C_GRAPHICS, "translate", "(II)V", a, NULL) == NJ_OK);
    a[0].i = 0; a[1].i = -1; a[2].i = 5; a[3].i = 4;
    CHECK(nj_call(vm, graphics, NJ_C_GRAPHICS, "setClip", "(IIII)V", a, NULL) == NJ_OK);
    for (int i = 0; i < 2; ++i) {
        nj_surface *grid = i ? hd : logical;
        int scale = i ? 4 : 1;
        check_scroll(vm, graphics, grid, scale, 0, 0, 1, 1, 0);
        check_scroll(vm, graphics, grid, scale, 1, 1, -1, -1, NJ_ANCHOR_RIGHT | NJ_ANCHOR_BOTTOM);
        check_scroll(vm, graphics, grid, scale, 0, 0, 1, 0, NJ_ANCHOR_HCENTER | NJ_ANCHOR_VCENTER);
    }
    /* Exceptions precede drawing even if part of the source is valid or the
     * destination is clipped away. Check a pixel that could be partially copied. */
    const int invalid[][4] = {
        {logical->width - 2, 0, 2, 1}, {-2, 0, 2, 1},
        {0, logical->height - 2, 1, 2}, {0, -2, 1, 2}
    };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        for (int j = 0; j < 4; ++j) a[j].i = invalid[i][j];
        a[4].i = i == 3 ? 20 : 0; a[5].i = 0; a[6].i = 0;
        nj_u4 prev = nj_surface_get(logical, 1, 1), prev_hd = nj_surface_get(hd, 4, 4);
        CHECK(nj_call(vm, graphics, NJ_C_GRAPHICS, "copyArea", "(IIIIIII)V", a, NULL) != NJ_OK);
        nj_ref ex = nj_take_exception(vm);
        CHECK(ex && strcmp(ex->cls->name, "java/lang/IllegalArgumentException") == 0);
        CHECK(nj_surface_get(logical, 1, 1) == prev && nj_surface_get(hd, 4, 4) == prev_hd);
    }
    /* A zero-sized region right on the bottom-right boundary is legal. */
    a[0].i = logical->width - 1; a[1].i = logical->height - 1;
    a[2].i = 0; a[3].i = 0;
    CHECK(nj_call(vm, graphics, NJ_C_GRAPHICS, "copyArea", "(IIIIIII)V", a, NULL) == NJ_OK);
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

/* drawRGB addresses Java int[] samples. Even if that array backs an Image
 * with a detailed HD surface, disjoint and self-copy paths use its raw data. */
static void check_raw_rgb(nj_vm *vm, nj_ref image)
{
    nj_ref other = nj_root(vm,nj_midp_image_new(vm,6,4,1));
    nj_ref data = nj_rget(vm,image,nj_field_lookup(vm,NJ_C_IMAGE,"imgPixels","[I",0));
    nj_surface logical;
    nj_u4 pattern[24];
    vm->render_quality.smooth_assets = 1;
    CHECK(other && data && nj_midp_surface(vm,image,&logical));
    nj_surface *source_hd = nj_midp_hd_target(vm,&logical)->target;
    for (int i=0; i<24; ++i) pattern[i] = i%2 ? 0xffff0000u : 0xff00ff00u;
    for (int alias=0; alias<2; ++alias) {
        nj_ref target = alias ? image : other;
        nj_ref graphics = nj_root(vm,nj_midp_image_graphics(vm,target));
        nj_value a[8] = {{0}};
        memcpy(nj_array_data(data),pattern,sizeof(pattern));
        for (int y=0; y<16; ++y) for (int x=0; x<24; ++x)
            nj_surface_set(source_hd,x,y,0xff0000ffu);
        a[0].r=data; a[2].i=6; a[5].i=6; a[6].i=4; a[7].i=1;
        CHECK(nj_call(vm,graphics,NJ_C_GRAPHICS,"drawRGB","([IIIIIIIZ)V",a,NULL)==NJ_OK);
        CHECK(nj_midp_surface(vm,target,&logical));
        nj_surface *hd = nj_midp_hd_target(vm,&logical)->target;
        for (int y=0; y<16; ++y) for (int x=0; x<24; ++x)
            CHECK(nj_surface_get(hd,x,y) == pattern[(y/4)*6 + x/4]);
    }
    vm->render_quality.smooth_assets = 0;
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
    check_raw_rgb(vm, image);
    check_image_copies(vm, image, &surface, src->target);
    check_copy_area(vm, nj_root(vm, nj_midp_image_graphics(vm, image)), &surface, src->target);
    /* A null target selects the port's framebuffer in this implementation. */
    check_copy_area(vm, nj_root(vm, nj_object_new(vm, NJ_C_GRAPHICS)), screen, dst->target);
    nj_root_release(vm, mark);
    nj_gc_collect(vm);
    CHECK(dst->lookup(dst->ud, key, 6, 4) == NULL);
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    return 0;
}
