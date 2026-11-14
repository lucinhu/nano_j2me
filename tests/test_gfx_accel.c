/* CPU conformance backend, not a test of a physical GPU.
 * Build this file AND nj_gfx.c/stub_font.c with NJ_ENABLE_GFX_ACCEL=0 or 1.
 * Complex operations use the documented reference helper; opaque operations
 * also use an independent implementation (including all eight transforms).
 */
#include "nj2me/nj_gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, cases, independent_fills, independent_blits;
#define CHECK(c) do { ++checks; if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
    exit(1); } } while (0)

enum { WIDTH = 11, HEIGHT = 9, GUARD = 13, STORAGE = 640 };
typedef struct canvas {
    nj_u1 bytes[STORAGE];
    nj_surface surface;
    nj_gfx gfx;
} canvas;
typedef struct backend_state {
    unsigned fills, blits, legacy, completed, depth;
    int result, independent, legacy_decline, expect_fill, expect_blit;
    int rect[4];
    nj_gfx_blit expected;
    nj_surface *target;
} backend_state;

static int bpp(int format)
{
    return format == NJ_PIXFMT_RGB565 ? 2 : format == NJ_PIXFMT_RGB888 ? 3 : 4;
}
/* These accessors intentionally do not use the rasterizer's conversion helpers. */
static void store(nj_u1 *p, int format, nj_u4 argb)
{
    if (format == NJ_PIXFMT_RGB565) {
        nj_u2 v = (nj_u2)(((argb >> 8) & 0xf800u) |
                          ((argb >> 5) & 0x07e0u) | ((argb >> 3) & 0x001fu));
        memcpy(p, &v, sizeof v);
    } else if (format == NJ_PIXFMT_RGB888) {
        p[0] = (nj_u1)(argb >> 16); p[1] = (nj_u1)(argb >> 8); p[2] = (nj_u1)argb;
    } else memcpy(p, &argb, sizeof argb);
}
static nj_u4 load(const nj_u1 *p, int format)
{
    if (format == NJ_PIXFMT_RGB565) {
        nj_u2 v;
        nj_u4 r, g, b;
        memcpy(&v, p, sizeof v);
        r = (v >> 11) & 31u; g = (v >> 5) & 63u; b = v & 31u;
        return 0xff000000u | (((r << 3) | (r >> 2)) << 16) |
               (((g << 2) | (g >> 4)) << 8) | (b << 3) | (b >> 2);
    } else if (format == NJ_PIXFMT_RGB888) {
        return 0xff000000u | ((nj_u4)p[0] << 16) | ((nj_u4)p[1] << 8) | p[2];
    } else {
        nj_u4 v; memcpy(&v, p, sizeof v); return v;
    }
}
static void init(canvas *c, int format)
{
    int x, y;
    memset(c, 0, sizeof *c);
    memset(c->bytes, 0xa5, sizeof c->bytes);
    c->surface.pixels = c->bytes + GUARD;
    c->surface.width = WIDTH; c->surface.height = HEIGHT;
    c->surface.stride = WIDTH * bpp(format) + 7;
    c->surface.format = format;
    nj_gfx_init(&c->gfx, &c->surface);
    for (y = 0; y < HEIGHT; ++y) for (x = 0; x < WIDTH; ++x)
        store(c->bytes + GUARD + y*c->surface.stride + x*bpp(format), format,
              0xff183149u + (nj_u4)(x*0x701u + y*0x90300u));
}
static void guards(const canvas *c)
{
    int i, x, y;
    for (i = 0; i < GUARD; ++i) CHECK(c->bytes[i] == 0xa5);
    for (y = 0; y < HEIGHT; ++y)
        for (x = WIDTH*c->surface.bpp; x < c->surface.stride; ++x)
            CHECK(c->bytes[GUARD + y*c->surface.stride + x] == 0xa5);
    for (i = GUARD + HEIGHT*c->surface.stride; i < STORAGE; ++i)
        CHECK(c->bytes[i] == 0xa5);
}
static void equal(const canvas *a, const canvas *b)
{
    CHECK(memcmp(a->bytes, b->bytes, STORAGE) == 0);
    guards(b);
    ++cases;
}
static backend_state state_for(canvas *c)
{
    backend_state s;
    memset(&s, 0, sizeof s);
    s.result = NJ_GFX_ACCEL_DONE; s.target = &c->surface;
    return s;
}
static void forward(int transform, int u, int v, int w, int h, int *x, int *y)
{
    switch (transform) {
    case NJ_TRANS_NONE: *x = u; *y = v; break;
    case NJ_TRANS_MIRROR_ROT180: *x = u; *y = h-1-v; break;
    case NJ_TRANS_MIRROR: *x = w-1-u; *y = v; break;
    case NJ_TRANS_ROT180: *x = w-1-u; *y = h-1-v; break;
    case NJ_TRANS_MIRROR_ROT270: *x = v; *y = u; break;
    case NJ_TRANS_ROT90: *x = h-1-v; *y = u; break;
    case NJ_TRANS_ROT270: *x = v; *y = w-1-u; break;
    case NJ_TRANS_MIRROR_ROT90: *x = h-1-v; *y = w-1-u; break;
    default: CHECK(0); *x = *y = 0;
    }
}
static int fill(void *ud, const nj_gfx *g, int x, int y, int w, int h)
{
    backend_state *s = (backend_state *)ud;
    int row, col;
    CHECK(s && s->target == g->surf && s->depth == 0);
    CHECK(x >= 0 && y >= 0 && w > 0 && h > 0);
    CHECK(x+w <= g->surf->width && y+h <= g->surf->height);
    ++s->fills;
    if (s->expect_fill) {
        CHECK(x == s->rect[0] && y == s->rect[1]);
        CHECK(w == s->rect[2] && h == s->rect[3]);
    }
    /* Any result other than DONE must decline before making any writes. */
    if (s->result != NJ_GFX_ACCEL_DONE) return s->result;
    ++s->depth;
    if (s->independent) {
        CHECK(!g->use_xor && (g->blend == NJ_BLEND_NONE || (g->color >> 24) == 255));
        for (row = y; row < y+h; ++row) for (col = x; col < x+w; ++col)
            store((nj_u1 *)g->surf->pixels + row*g->surf->stride + col*g->surf->bpp,
                  g->surf->format, g->color);
        ++independent_fills;
    } else {
        nj_gfx cpu = *g;
        nj_gfx_set_accel(&cpu, NULL, NULL);
        cpu.fill_rect = NULL; cpu.fill_ud = NULL;
        nj_gfx_fill_rect(&cpu, x-g->translate_x, y-g->translate_y, w, h);
    }
    --s->depth; ++s->completed;
    return NJ_GFX_ACCEL_DONE;
}
static void check_descriptor(const nj_gfx_blit *b, const nj_gfx_blit *e)
{
    CHECK(b->pixels == e->pixels && b->stride == e->stride && b->format == e->format);
    CHECK(b->mask == e->mask && b->mask_width == e->mask_width && b->mask_height == e->mask_height);
    CHECK(b->sx == e->sx && b->sy == e->sy && b->width == e->width && b->height == e->height);
    CHECK(b->transform == e->transform && b->rop == e->rop);
    CHECK(b->xor_enabled == e->xor_enabled && b->xor_color == e->xor_color);
    CHECK(b->dx == e->dx && b->dy == e->dy && b->alpha == e->alpha);
    CHECK(b->clip_x == e->clip_x && b->clip_y == e->clip_y);
    CHECK(b->clip_w == e->clip_w && b->clip_h == e->clip_h);
}
static int blit(void *ud, const nj_gfx *g, const nj_gfx_blit *b)
{
    backend_state *s = (backend_state *)ud;
    CHECK(s && s->target == g->surf && s->depth == 0);
    CHECK(b->clip_x >= 0 && b->clip_y >= 0 && b->clip_w > 0 && b->clip_h > 0);
    CHECK(b->clip_x+b->clip_w <= g->surf->width && b->clip_y+b->clip_h <= g->surf->height);
    ++s->blits;
    if (s->expect_blit) check_descriptor(b, &s->expected);
    if (s->result != NJ_GFX_ACCEL_DONE) return s->result;
    ++s->depth;
    if (s->independent) {
        int u, v;
        CHECK(!b->mask && !b->xor_enabled && b->rop == NJ_ROP_SET && b->alpha == 255);
        /* Forward source mapping is deliberately independent of the renderer's
         * inverse destination mapping. All pointers may be unaligned. */
        for (v = 0; v < b->height; ++v) for (u = 0; u < b->width; ++u) {
            int x, y;
            const nj_u1 *p;
            forward(b->transform, u, v, b->width, b->height, &x, &y);
            x += b->dx + g->translate_x; y += b->dy + g->translate_y;
            if (x < b->clip_x || x >= b->clip_x+b->clip_w ||
                y < b->clip_y || y >= b->clip_y+b->clip_h) continue;
            p = b->pixels + (b->sy+v)*b->stride + (b->sx+u)*bpp(b->format);
            store((nj_u1 *)g->surf->pixels + y*g->surf->stride + x*g->surf->bpp,
                  g->surf->format, load(p, b->format));
        }
        ++independent_blits;
    } else {
        /* A recursive backend call fails the depth check above. */
        nj_gfx_blit_software(g, b);
    }
    --s->depth; ++s->completed;
    return NJ_GFX_ACCEL_DONE;
}
static int legacy(void *ud, const nj_surface *surf, int x, int y, int w, int h, nj_u4 packed)
{
    backend_state *s = (backend_state *)ud;
    int col, row;
    CHECK(s && s->target == surf && s->depth == 0);
    ++s->legacy;
    if (s->legacy_decline) return 0;
    for (row = y; row < y+h; ++row) for (col = x; col < x+w; ++col) {
        nj_u1 *p = (nj_u1 *)surf->pixels + row*surf->stride + col*surf->bpp;
        if (surf->format == NJ_PIXFMT_RGB565) {
            nj_u2 v = (nj_u2)packed; memcpy(p, &v, sizeof v);
        } else store(p, surf->format, packed);
    }
    return 1;
}
static const nj_gfx_accel backend = { NJ_GFX_ACCEL_VERSION, fill, blit };
static void bind(canvas *c, backend_state *s)
{
    nj_gfx_set_accel(&c->gfx, &backend, s);
}
static void setup_clip(nj_gfx *g)
{
    nj_gfx_translate(g, 2, 1);
    nj_gfx_set_clip(g, 1, 1, 5, 5); /* final device clip (3,2)-(7,6) */
}
static void expected_region(backend_state *s, const nj_image *img, int t, int rop,
                            int xor_on, nj_u4 xor_color, const nj_image *mask)
{
    nj_gfx_blit *e = &s->expected;
    memset(e, 0, sizeof *e);
    e->pixels = (const nj_u1 *)img->pixels; e->stride = img->width*bpp(img->format);
    e->format = img->format; e->sx = e->sy = 1; e->width = 4; e->height = 3;
    e->transform = t; e->rop = rop; e->xor_enabled = xor_on; e->xor_color = xor_color;
    e->alpha = 255; e->clip_x = 3; e->clip_y = 2;
    e->clip_w = t >= 4 ? 2 : 3; e->clip_h = t >= 4 ? 3 : 2;
    if (mask) {
        e->mask = (const nj_u1 *)mask->pixels; e->mask_width = mask->width;
        e->mask_height = mask->height; e->clip_w = 2;
    }
    s->expect_blit = 1;
}
static void source(nj_u1 *bytes, int format)
{
    int i;
    for (i = 0; i < 35; ++i) {
        nj_u4 alpha = i%3 == 0 ? 0u : i%3 == 1 ? 0x7f000000u : 0xff000000u;
        store(bytes + i*bpp(format), format, alpha | ((nj_u4)(0x153971u+i*0x50309u) & 0xffffffu));
    }
}
static void test_fills(void)
{
    static const int results[] = { NJ_GFX_ACCEL_DONE, NJ_GFX_ACCEL_DECLINE, -7, 2 };
    static const nj_u4 colors[] = { 0xff123456u, 0x7f6bc13eu, 0x00102030u };
    int fmt, mode, ci, blend_mode, xor_on;
    for (fmt = NJ_PIXFMT_RGB565; fmt <= NJ_PIXFMT_ARGB8888; ++fmt)
    for (mode = 0; mode < 4; ++mode) for (ci = 0; ci < 3; ++ci)
    for (blend_mode = 0; blend_mode < 2; ++blend_mode) for (xor_on = 0; xor_on < 2; ++xor_on) {
        canvas a, b;
        backend_state s;
        init(&a, fmt); init(&b, fmt); s = state_for(&b); bind(&b, &s);
        s.result = results[mode]; s.expect_fill = 1;
        s.rect[0] = 3; s.rect[1] = 2; s.rect[2] = 3; s.rect[3] = 3;
        s.independent = !xor_on && (blend_mode == NJ_BLEND_NONE || ci == 0);
        setup_clip(&a.gfx); setup_clip(&b.gfx);
        a.gfx.color = b.gfx.color = colors[ci];
        a.gfx.blend = b.gfx.blend = blend_mode;
        if (xor_on) { nj_gfx_set_xor(&a.gfx, 0x005aa53cu); nj_gfx_set_xor(&b.gfx, 0x005aa53cu); }
        nj_gfx_fill_rect(&a.gfx, -1, 0, 5, 4); nj_gfx_fill_rect(&b.gfx, -1, 0, 5, 4);
        CHECK(s.fills == (unsigned)NJ_ENABLE_GFX_ACCEL);
        CHECK(s.completed == (unsigned)(NJ_ENABLE_GFX_ACCEL && mode == 0));
        equal(&a, &b);
    }
}
static void test_blits(void)
{
    int sf, df, t, rop, masked, xor_mode, mode;
    nj_u4 mask_pixels[12] = { 0, 0xffffffffu, 0x01000000u, 0xff000000u, 0, 0x7f000000u,
                              0, 0xff123456u, 0xffabcdefu, 0xffffffffu, 0, 0 };
    nj_image mask = {3, 4, NJ_PIXFMT_ARGB8888, mask_pixels, 1};
    for (sf = NJ_PIXFMT_RGB565; sf <= NJ_PIXFMT_ARGB8888; ++sf)
    for (df = NJ_PIXFMT_RGB565; df <= NJ_PIXFMT_ARGB8888; ++df)
    for (t = 0; t < 8; ++t) for (rop = 0; rop < 3; ++rop)
    for (masked = 0; masked < 2; ++masked) for (xor_mode = 0; xor_mode < 3; ++xor_mode)
    for (mode = 0; mode < 2; ++mode) {
        nj_u1 data[141], before[141];
        nj_image img = {7, 5, sf, (nj_u4 *)(void *)(data+1), 1};
        canvas a, b;
        backend_state s;
        nj_u4 xor_color = xor_mode == 1 ? 0x005391a7u : xor_mode == 2 ? 0x001745b3u : 0u;
        memset(data, 0x6d, sizeof data); source(data+1, sf); memcpy(before, data, sizeof data);
        init(&a, df); init(&b, df); s = state_for(&b); bind(&b, &s);
        s.result = mode ? NJ_GFX_ACCEL_DECLINE : NJ_GFX_ACCEL_DONE;
        setup_clip(&a.gfx); setup_clip(&b.gfx);
        if (xor_mode == 2) { nj_gfx_set_xor(&a.gfx, xor_color); nj_gfx_set_xor(&b.gfx, xor_color); }
        expected_region(&s, &img, t, rop, xor_mode != 0, xor_color, masked ? &mask : NULL);
        nj_gfx_draw_region(&a.gfx, &img, 1, 1, 4, 3, t, xor_mode == 1,
                           xor_mode == 1 ? xor_color : 0u, masked ? &mask : NULL, 0, 0, rop);
        nj_gfx_draw_region(&b.gfx, &img, 1, 1, 4, 3, t, xor_mode == 1,
                           xor_mode == 1 ? xor_color : 0u, masked ? &mask : NULL, 0, 0, rop);
        CHECK(s.blits == (unsigned)NJ_ENABLE_GFX_ACCEL && s.depth == 0);
        CHECK(s.completed == (unsigned)(NJ_ENABLE_GFX_ACCEL && !mode));
        CHECK(memcmp(before, data, sizeof data) == 0);
        equal(&a, &b);
    }
    /* Independent opaque/SET copies exercise every source/target format and
     * every transform, preserving ARGB bits including zero/partial alpha. */
    for (sf = NJ_PIXFMT_RGB565; sf <= NJ_PIXFMT_ARGB8888; ++sf)
    for (df = NJ_PIXFMT_RGB565; df <= NJ_PIXFMT_ARGB8888; ++df) for (t = 0; t < 8; ++t) {
        nj_u1 data[141];
        nj_image img = {7, 5, sf, (nj_u4 *)(void *)(data+1), 1};
        canvas a, b;
        backend_state s;
        source(data+1, sf); init(&a, df); init(&b, df); s = state_for(&b);
        s.independent = 1; bind(&b, &s); setup_clip(&a.gfx); setup_clip(&b.gfx);
        expected_region(&s, &img, t, NJ_ROP_SET, 0, 0, NULL);
        nj_gfx_draw_image_rop(&a.gfx, &img, 0, 0, 1, 1, 4, 3, NJ_ROP_SET, t);
        nj_gfx_draw_image_rop(&b.gfx, &img, 0, 0, 1, 1, 4, 3, NJ_ROP_SET, t);
        CHECK(s.blits == (unsigned)NJ_ENABLE_GFX_ACCEL);
        equal(&a, &b);
    }
}
static void test_rgb(void)
{
    static const int alphas[] = {-19, 0, 1, 127, 255, 300};
    int fmt, t, ai, rop, mode;
    for (fmt = NJ_PIXFMT_RGB565; fmt <= NJ_PIXFMT_ARGB8888; ++fmt)
    for (t = 0; t < 8; ++t) for (ai = 0; ai < 6; ++ai)
    for (rop = 0; rop < 3; ++rop) for (mode = 0; mode < 2; ++mode) {
        nj_u1 data[5*26+1], before[5*26+1];
        canvas a, b;
        backend_state s;
        int x, y;
        memset(data, 0xd7, sizeof data);
        for (y = 0; y < 5; ++y) for (x = 0; x < 7; ++x)
            store(data+1+y*26+x*3, NJ_PIXFMT_RGB888, 0xff123456u+(nj_u4)(x*0x071305u+y*0x090307u));
        memcpy(before, data, sizeof data);
        init(&a, fmt); init(&b, fmt); s = state_for(&b); bind(&b, &s);
        s.result = mode ? -3 : NJ_GFX_ACCEL_DONE;
        setup_clip(&a.gfx); setup_clip(&b.gfx);
        s.expect_blit = 1;
        s.expected.pixels = data+1; s.expected.stride = 26; s.expected.format = NJ_PIXFMT_RGB888;
        s.expected.sx = s.expected.sy = 1; s.expected.width = 4; s.expected.height = 3;
        s.expected.transform = t; s.expected.rop = rop;
        s.expected.alpha = alphas[ai] < 0 ? 0 : alphas[ai] > 255 ? 255 : alphas[ai];
        s.expected.clip_x = 3; s.expected.clip_y = 2;
        s.expected.clip_w = t >= 4 ? 2 : 3; s.expected.clip_h = t >= 4 ? 3 : 2;
        nj_gfx_draw_rgb(&a.gfx, data+1, 26, alphas[ai], 0, 0, 1, 1, 4, 3, rop, t);
        nj_gfx_draw_rgb(&b.gfx, data+1, 26, alphas[ai], 0, 0, 1, 1, 4, 3, rop, t);
        CHECK(s.blits == (unsigned)NJ_ENABLE_GFX_ACCEL);
        CHECK(memcmp(before, data, sizeof data) == 0);
        equal(&a, &b);
    }
}
static void test_protocol(void)
{
    int fmt, choice;
    nj_u4 pixel = 0xff34a289u;
    nj_image img = {1, 1, NJ_PIXFMT_ARGB8888, &pixel, 0};
    for (fmt = NJ_PIXFMT_RGB565; fmt <= NJ_PIXFMT_ARGB8888; ++fmt)
    for (choice = 0; choice < 7; ++choice) {
        canvas a, b;
        backend_state s;
        nj_gfx_accel custom = backend;
        init(&a, fmt); init(&b, fmt); s = state_for(&b);
        b.gfx.fill_rect = legacy; b.gfx.fill_ud = &s;
        /* DONE, decline->legacy, decline->legacy->CPU, unknown version,
         * NULL backend, NULL fill callback, NULL blit callback. */
        if (choice == 1 || choice == 2) s.result = 0;
        if (choice == 2) s.legacy_decline = 1;
        if (choice == 3) custom.version = NJ_GFX_ACCEL_VERSION+1;
        if (choice == 5) custom.fill_rect = NULL;
        if (choice == 6) custom.blit = NULL;
        s.independent = 1;
        nj_gfx_set_accel(&b.gfx, choice == 4 ? NULL : &custom, &s);
        nj_gfx_set_color(&a.gfx, 0xff17a253u); nj_gfx_set_color(&b.gfx, a.gfx.color);
        nj_gfx_fill_rect(&a.gfx, 1, 2, 5, 3); nj_gfx_fill_rect(&b.gfx, 1, 2, 5, 3);
        CHECK(s.fills == (unsigned)(NJ_ENABLE_GFX_ACCEL && choice != 3 && choice != 4 && choice != 5));
        CHECK(s.legacy == (unsigned)(!NJ_ENABLE_GFX_ACCEL || (choice >= 1 && choice <= 5)));
        equal(&a, &b);
        /* The remaining callback still works when its peer is NULL. */
        nj_gfx_draw_image(&a.gfx, &img, 8, 4, 0, 0, 1, 1);
        s.independent = 0;
        nj_gfx_draw_image(&b.gfx, &img, 8, 4, 0, 0, 1, 1);
        CHECK(s.blits == (unsigned)(NJ_ENABLE_GFX_ACCEL && choice != 3 && choice != 4 && choice != 6));
        equal(&a, &b);
        /* Setter reset must also discard a previously installed backend. */
        nj_gfx_set_accel(&b.gfx, NULL, NULL);
        nj_gfx_draw_image(&a.gfx, &img, 8, 5, 0, 0, 1, 1);
        nj_gfx_draw_image(&b.gfx, &img, 8, 5, 0, 0, 1, 1);
        equal(&a, &b);
    }
    {
        canvas c;
        backend_state s;
        unsigned before;
        init(&c, NJ_PIXFMT_ARGB8888); s = state_for(&c); bind(&c, &s);
        c.gfx.fill_rect = legacy; c.gfx.fill_ud = &s;
        s.result = -2;
        c.gfx.color = 0x70123456u;
        nj_gfx_fill_rect(&c.gfx, 0, 0, 2, 2);
        CHECK(s.legacy == 0); /* legacy cannot perform alpha blending */
        c.gfx.color = 0xff123456u; nj_gfx_set_xor(&c.gfx, 0x00555555u);
        nj_gfx_fill_rect(&c.gfx, 0, 0, 2, 2); CHECK(s.legacy == 0);
        before = s.fills+s.blits;
        nj_gfx_fill_rect(&c.gfx, 0, 0, 0, 3);
        nj_gfx_fill_rect(&c.gfx, 0, 0, -1, 3);
        nj_gfx_fill_rect(&c.gfx, 100, 100, 2, 2);
        nj_gfx_draw_image(&c.gfx, &img, 100, 100, 0, 0, 1, 1);
        nj_gfx_draw_image(&c.gfx, &img, 0, 0, 0, 0, 0, 1);
        nj_gfx_draw_image(&c.gfx, &img, 0, 0, 2, 0, 1, 1);
        nj_gfx_draw_image(&c.gfx, NULL, 0, 0, 0, 0, 1, 1);
        nj_gfx_draw_rgb(&c.gfx, NULL, 3, 255, 0, 0, 0, 0, 1, 1, NJ_ROP_SET, 0);
        nj_gfx_set_clip(&c.gfx, 0, 0, 0, 0);
        nj_gfx_fill_rect(&c.gfx, 0, 0, 3, 3);
        nj_gfx_draw_image(&c.gfx, &img, 0, 0, 0, 0, 1, 1);
        CHECK(s.fills+s.blits == before && s.legacy == 0);
        nj_gfx_set_accel(NULL, &backend, &s);
        nj_gfx_blit_software(NULL, NULL); nj_gfx_blit_software(&c.gfx, NULL);
        guards(&c);
    }
}
static void test_cpu_coherence_and_instances(void)
{
    canvas a, b, other;
    backend_state s, os;
    nj_u4 pixels[4] = {0xff123456u, 0xffa1b2c3u, 0xff998877u, 0xff01ab37u};
    nj_image img = {2, 2, NJ_PIXFMT_ARGB8888, pixels, 0};
    unsigned count;
    init(&a, NJ_PIXFMT_ARGB8888); init(&b, NJ_PIXFMT_ARGB8888);
    init(&other, NJ_PIXFMT_ARGB8888);
    s = state_for(&b); os = state_for(&other); bind(&b, &s); bind(&other, &os);
    nj_gfx_draw_image(&a.gfx, &img, 2, 2, 0, 0, 2, 2);
    nj_gfx_draw_image(&b.gfx, &img, 2, 2, 0, 0, 2, 2);
    CHECK(nj_surface_get(&b.surface, 2, 2) == pixels[0]);
    CHECK(os.blits == 0 && os.fills == 0);
    count = s.blits;
    /* Direct CPU pixel write after return, then accelerated destination read
     * via XOR/alpha, plus a changed source at the SAME pointer. */
    nj_surface_set(&a.surface, 2, 2, 0xff765432u);
    nj_surface_set(&b.surface, 2, 2, 0xff765432u);
    pixels[0] = 0x7fe2c481u;
    nj_gfx_draw_image(&a.gfx, &img, 2, 2, 0, 0, 2, 2);
    nj_gfx_draw_image(&b.gfx, &img, 2, 2, 0, 0, 2, 2);
    equal(&a, &b);
    CHECK(nj_surface_get(&b.surface, 2, 2) != 0xff765432u);
    CHECK(s.blits == count+(unsigned)NJ_ENABLE_GFX_ACCEL);
    nj_gfx_set_xor(&a.gfx, 0x0053a798u); nj_gfx_set_xor(&b.gfx, 0x0053a798u);
    nj_gfx_draw_image(&a.gfx, &img, 2, 2, 0, 0, 2, 2);
    nj_gfx_draw_image(&b.gfx, &img, 2, 2, 0, 0, 2, 2);
    equal(&a, &b);
    nj_gfx_set_color(&a.gfx, 0xff1166aau); nj_gfx_set_color(&b.gfx, a.gfx.color);
    nj_gfx_line(&a.gfx, 0, 2, 8, 2); nj_gfx_line(&b.gfx, 0, 2, 8, 2);
    equal(&a, &b);
    /* Independent second context shares only the immutable backend vtable. */
    count = s.blits;
    os.result = -8;
    nj_gfx_draw_image(&other.gfx, &img, 2, 2, 0, 0, 2, 2);
    CHECK(os.blits == (unsigned)NJ_ENABLE_GFX_ACCEL && os.completed == 0);
    CHECK(s.blits == count && s.target == &b.surface && os.target == &other.surface);
    guards(&other);
}
int main(void)
{
    test_fills(); test_blits(); test_rgb(); test_protocol();
    test_cpu_coherence_and_instances();
    CHECK(independent_fills == (unsigned)(NJ_ENABLE_GFX_ACCEL ? 18 : 0));
    CHECK(independent_blits == (unsigned)(NJ_ENABLE_GFX_ACCEL ? 72 : 0));
    printf("PASS gfx accel=%d: %u cases, %u checks; independent CPU fill=%u blit=%u (no GPU tested)\n",
           NJ_ENABLE_GFX_ACCEL, cases, checks, independent_fills, independent_blits);
    return 0;
}
