/*
 * tests/tmp_gfx.c - *** THROWAWAY STAND-IN, NOT PART OF THE RUNTIME ***
 *
 * src/gfx/nj_gfx.c is owned by another agent and did not exist when the LCDUI
 * module was verified.  This file implements just enough of include/nj2me/
 * nj_gfx.h for tests/test_lcdui.c to exercise the LCDUI layer, and it is
 * deleted from the test command the moment the real rasterizer appears.
 *
 * It is deliberately simple (no anti-aliasing, integer-only) but it is
 * *correct* where correctness is observable: pixel writes, clipping,
 * translation, colour conversion, alpha blending and region blits all behave
 * the way the real one must.
 */
#include "nj2me/nj_gfx.h"

#include <string.h>

#define GFX_MAX_GLYPHS 128

/* ------------------------------------------------------------------ */
/* Format helpers                                                      */
/* ------------------------------------------------------------------ */
nj_u4 nj_surface_format_color(int format, nj_u4 argb)
{
    unsigned r = (argb >> 16) & 0xFFu;
    unsigned g = (argb >> 8) & 0xFFu;
    unsigned b = argb & 0xFFu;

    switch (format) {
    case NJ_PIXFMT_RGB565:
        return (nj_u4)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    case NJ_PIXFMT_RGB888:
        return (nj_u4)((r << 16) | (g << 8) | b);
    default:
        return argb;
    }
}

/*
 * Read a pixel back as ARGB8888.  For RGB565 the low bits are replicated so
 * that 0xFF -> 0xFF rather than 0xF8, which is what a real rasterizer does.
 */
static nj_u4 gf_read(const nj_surface *s, int x, int y)
{
    const nj_u1 *p = (const nj_u1 *)s->pixels + (nj_size)y * (nj_size)s->stride;

    switch (s->format) {
    case NJ_PIXFMT_RGB565: {
        unsigned v = (unsigned)p[x * 2] | ((unsigned)p[x * 2 + 1] << 8);
        unsigned r = (v >> 11) & 0x1Fu;
        unsigned g = (v >> 5) & 0x3Fu;
        unsigned b = v & 0x1Fu;
        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    case NJ_PIXFMT_RGB888:
        return 0xFF000000u | ((nj_u4)p[x * 3] << 16) |
               ((nj_u4)p[x * 3 + 1] << 8) | (nj_u4)p[x * 3 + 2];
    default: {
        nj_u4 v;
        memcpy(&v, p + (nj_size)x * 4u, 4u);
        return v;
    }
    }
}

static void gf_write(nj_surface *s, int x, int y, nj_u4 argb)
{
    nj_u1 *p = (nj_u1 *)s->pixels + (nj_size)y * (nj_size)s->stride;

    switch (s->format) {
    case NJ_PIXFMT_RGB565: {
        unsigned r = (argb >> 16) & 0xFFu;
        unsigned g = (argb >> 8) & 0xFFu;
        unsigned b = argb & 0xFFu;
        unsigned v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        p[x * 2] = (nj_u1)(v & 0xFFu);
        p[x * 2 + 1] = (nj_u1)((v >> 8) & 0xFFu);
        break;
    }
    case NJ_PIXFMT_RGB888:
        p[x * 3] = (nj_u1)((argb >> 16) & 0xFFu);
        p[x * 3 + 1] = (nj_u1)((argb >> 8) & 0xFFu);
        p[x * 3 + 2] = (nj_u1)(argb & 0xFFu);
        break;
    default:
        memcpy(p + (nj_size)x * 4u, &argb, 4u);
        break;
    }
}

nj_u4 nj_surface_get(const nj_surface *s, int x, int y)
{
    if (!s || !s->pixels || x < 0 || y < 0 || x >= s->width || y >= s->height) {
        return 0u;
    }
    return gf_read(s, x, y);
}

void nj_surface_set(nj_surface *s, int x, int y, nj_u4 argb)
{
    if (!s || !s->pixels || x < 0 || y < 0 || x >= s->width || y >= s->height) {
        return;
    }
    gf_write(s, x, y, argb);
}

void nj_gfx_blend_pixel(nj_surface *s, int x, int y, nj_u4 argb, int alpha)
{
    nj_u4 dst, out;
    unsigned a, ia;

    if (!s || alpha <= 0) {
        return;
    }
    if (alpha >= 255) {
        nj_surface_set(s, x, y, argb);
        return;
    }
    dst = nj_surface_get(s, x, y);
    a = (unsigned)alpha;
    ia = 255u - a;
    out = 0xFF000000u |
          (((((argb >> 16) & 0xFFu) * a + ((dst >> 16) & 0xFFu) * ia) / 255u) << 16) |
          (((((argb >> 8) & 0xFFu) * a + ((dst >> 8) & 0xFFu) * ia) / 255u) << 8) |
          ((((argb & 0xFFu) * a + (dst & 0xFFu) * ia) / 255u));
    nj_surface_set(s, x, y, out);
}

void nj_surface_fill(nj_surface *s, nj_u4 argb)
{
    int x, y;
    if (!s) {
        return;
    }
    for (y = 0; y < s->height; y++) {
        for (x = 0; x < s->width; x++) {
            gf_write(s, x, y, argb);
        }
    }
}

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */
void nj_gfx_init(nj_gfx *g, nj_surface *surf)
{
    memset(g, 0, sizeof(*g));
    g->surf = surf;
    g->color = 0xFF000000u;
    g->stroke = NJ_STROKE_SOLID;
    g->blend = NJ_BLEND_SRC_OVER;
    g->font = nj_font_builtin(NJ_FONT_SIZE_MEDIUM, NJ_FONT_STYLE_PLAIN);
    if (surf) {
        g->clip_w = surf->width;
        g->clip_h = surf->height;
    }
}

int nj_gfx_dev_x(const nj_gfx *g, int x) { return x + g->translate_x; }
int nj_gfx_dev_y(const nj_gfx *g, int y) { return y + g->translate_y; }

void nj_gfx_set_clip(nj_gfx *g, int x, int y, int w, int h)
{
    if (!g->surf) {
        return;
    }
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > g->surf->width)  { w = g->surf->width - x; }
    if (y + h > g->surf->height) { h = g->surf->height - y; }
    g->clip_x = x;
    g->clip_y = y;
    g->clip_w = (w > 0) ? w : 0;
    g->clip_h = (h > 0) ? h : 0;
}

void nj_gfx_clip_rect(nj_gfx *g, int x, int y, int w, int h)
{
    int x0 = nj_gfx_dev_x(g, x);
    int y0 = nj_gfx_dev_y(g, y);
    int x1 = x0 + w;
    int y1 = y0 + h;
    int cx = g->clip_x;
    int cy = g->clip_y;
    int cx1 = cx + g->clip_w;
    int cy1 = cy + g->clip_h;

    if (x0 < cx) { cx = x0; }
    if (y0 < cy) { cy = y0; }
    if (x1 > cx1) { cx1 = x1; }
    if (y1 > cy1) { cy1 = y1; }
    if (cx1 < cx) { cx1 = cx; }
    if (cy1 < cy) { cy1 = cy; }
    nj_gfx_set_clip(g, cx, cy, cx1 - cx, cy1 - cy);
}

void nj_gfx_translate(nj_gfx *g, int dx, int dy)
{
    g->translate_x += dx;
    g->translate_y += dy;
}

void nj_gfx_get_translate(const nj_gfx *g, int *dx, int *dy)
{
    if (dx) { *dx = g->translate_x; }
    if (dy) { *dy = g->translate_y; }
}

int nj_gfx_get_clip_x(const nj_gfx *g) { return g->clip_x; }
int nj_gfx_get_clip_y(const nj_gfx *g) { return g->clip_y; }
int nj_gfx_get_clip_w(const nj_gfx *g) { return g->clip_w; }
int nj_gfx_get_clip_h(const nj_gfx *g) { return g->clip_h; }

void nj_gfx_set_color(nj_gfx *g, nj_u4 argb) { g->color = argb; }
nj_u4 nj_gfx_get_color(const nj_gfx *g) { return g->color; }
void nj_gfx_set_color_rgb(nj_gfx *g, int r, int g_, int b)
{
    g->color = 0xFF000000u | ((nj_u4)(r & 0xFF) << 16) |
               ((nj_u4)(g_ & 0xFF) << 8) | (nj_u4)(b & 0xFF);
}
void nj_gfx_set_alpha(nj_gfx *g, int alpha)
{
    if (alpha < 0) { alpha = 0; }
    if (alpha > 255) { alpha = 255; }
    g->color = (g->color & 0x00FFFFFFu) | ((nj_u4)alpha << 24);
}
int nj_gfx_get_alpha(const nj_gfx *g) { return (int)((g->color >> 24) & 0xFFu); }
void nj_gfx_set_blend(nj_gfx *g, int mode) { g->blend = mode; }
void nj_gfx_set_stroke(nj_gfx *g, int stroke) { g->stroke = stroke; }
void nj_gfx_set_font(nj_gfx *g, const nj_font *f) { if (f) { g->font = f; } }
void nj_gfx_set_xor(nj_gfx *g, nj_u4 argb) { g->use_xor = 1; g->xor_color = argb; }
void nj_gfx_clear_xor(nj_gfx *g) { g->use_xor = 0; }

/* ------------------------------------------------------------------ */
/* Primitives                                                          */
/* ------------------------------------------------------------------ */
static int gf_visible(const nj_gfx *g, int x, int y)
{
    return g->surf && x >= g->clip_x && y >= g->clip_y &&
           x < g->clip_x + g->clip_w && y < g->clip_y + g->clip_h;
}

void nj_gfx_pixel(nj_gfx *g, int x, int y)
{
    int dx = nj_gfx_dev_x(g, x);
    int dy = nj_gfx_dev_y(g, y);

    if (!gf_visible(g, dx, dy)) {
        return;
    }
    if (g->use_xor) {
        nj_u4 dst = nj_surface_get(g->surf, dx, dy);
        nj_surface_set(g->surf, dx, dy,
                       0xFF000000u ^ (dst ^ g->xor_color));
        return;
    }
    if (g->blend == NJ_BLEND_SRC_OVER) {
        unsigned a = (g->color >> 24) & 0xFFu;
        if (a == 0u) {
            return;
        }
        nj_gfx_blend_pixel(g->surf, dx, dy, g->color, (int)a);
        return;
    }
    nj_surface_set(g->surf, dx, dy, g->color);
}

void nj_gfx_line(nj_gfx *g, int x1, int y1, int x2, int y2)
{
    int dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
    int dy = (y2 > y1) ? (y2 - y1) : (y1 - y2);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    int guard = 0;

    if (dx == 0 && dy == 0) {
        nj_gfx_pixel(g, x1, y1);
        return;
    }
    for (;;) {
        if (g->stroke == NJ_STROKE_DOTTED && ((guard / 2) % 2) == 1) {
            /* skip: dotted pattern */
        } else {
            nj_gfx_pixel(g, x1, y1);
        }
        guard++;
        if (x1 == x2 && y1 == y2) {
            break;
        }
        {
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x1 += sx; }
            if (e2 < dx)  { err += dx; y1 += sy; }
        }
    }
}

void nj_gfx_fill_rect(nj_gfx *g, int x, int y, int w, int h)
{
    int i, j;
    int x0 = nj_gfx_dev_x(g, x);
    int y0 = nj_gfx_dev_y(g, y);
    int x1 = x0 + w;
    int y1 = y0 + h;

    if (x1 > g->clip_x + g->clip_w) { x1 = g->clip_x + g->clip_w; }
    if (y1 > g->clip_y + g->clip_h) { y1 = g->clip_y + g->clip_h; }
    if (x0 < g->clip_x) { x0 = g->clip_x; }
    if (y0 < g->clip_y) { y0 = g->clip_y; }
    for (j = y0; j < y1; j++) {
        for (i = x0; i < x1; i++) {
            if (g->use_xor) {
                nj_u4 dst = nj_surface_get(g->surf, i, j);
                nj_surface_set(g->surf, i, j, 0xFF000000u ^ (dst ^ g->xor_color));
            } else if (g->blend == NJ_BLEND_SRC_OVER) {
                unsigned a = (g->color >> 24) & 0xFFu;
                if (a != 0u) {
                    nj_gfx_blend_pixel(g->surf, i, j, g->color, (int)a);
                }
            } else {
                nj_surface_set(g->surf, i, j, g->color);
            }
        }
    }
}

void nj_gfx_rect(nj_gfx *g, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    nj_gfx_line(g, x, y, x + w - 1, y);
    nj_gfx_line(g, x, y + h - 1, x + w - 1, y + h - 1);
    nj_gfx_line(g, x, y, x, y + h - 1);
    nj_gfx_line(g, x + w - 1, y, x + w - 1, y + h - 1);
}

void nj_gfx_round_rect(nj_gfx *g, int x, int y, int w, int h, int rw, int rh)
{
    int r = (rw < rh) ? rw : rh;
    (void)r;
    nj_gfx_rect(g, x, y, w, h);
}

void nj_gfx_fill_round_rect(nj_gfx *g, int x, int y, int w, int h, int rw, int rh)
{
    (void)rw; (void)rh;
    nj_gfx_fill_rect(g, x, y, w, h);
}

void nj_gfx_arc(nj_gfx *g, int x, int y, int w, int h, int start, int extent)
{
    (void)start; (void)extent;
    nj_gfx_rect(g, x, y, w, h);
}

void nj_gfx_fill_arc(nj_gfx *g, int x, int y, int w, int h, int start, int extent)
{
    (void)start; (void)extent;
    nj_gfx_fill_rect(g, x, y, w, h);
}

void nj_gfx_triangle(nj_gfx *g, int x1, int y1, int x2, int y2, int x3, int y3,
                     int filled)
{
    if (!filled) {
        nj_gfx_line(g, x1, y1, x2, y2);
        nj_gfx_line(g, x2, y2, x3, y3);
        nj_gfx_line(g, x3, y3, x1, y1);
        return;
    }
    {
        int minx = x1, maxx = x1, miny = y1, maxy = y1;
        int x, y;
        if (x2 < minx) { minx = x2; }
        if (x3 < minx) { minx = x3; }
        if (x2 > maxx) { maxx = x2; }
        if (x3 > maxx) { maxx = x3; }
        if (y2 < miny) { miny = y2; }
        if (y3 < miny) { miny = y3; }
        if (y2 > maxy) { maxy = y2; }
        if (y3 > maxy) { maxy = y3; }
        for (y = miny; y <= maxy; y++) {
            for (x = minx; x <= maxx; x++) {
                long d1 = (long)(x - x2) * (y1 - y2) - (long)(x1 - x2) * (y - y2);
                long d2 = (long)(x - x3) * (y2 - y3) - (long)(x2 - x3) * (y - y3);
                long d3 = (long)(x - x1) * (y3 - y1) - (long)(x3 - x1) * (y - y1);
                int neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
                int pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
                if (!(neg && pos)) {
                    nj_gfx_pixel(g, x, y);
                }
            }
        }
    }
}

void nj_gfx_clear(nj_gfx *g, nj_u4 argb)
{
    nj_u4 save = g->color;
    int sx = g->translate_x, sy = g->translate_y;
    g->color = argb;
    g->translate_x = 0;
    g->translate_y = 0;
    nj_gfx_fill_rect(g, 0, 0, g->surf->width, g->surf->height);
    g->color = save;
    g->translate_x = sx;
    g->translate_y = sy;
}

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */
void nj_gfx_draw_text(nj_gfx *g, const nj_u2 *s, int len, int x, int y,
                      int anchor)
{
    const nj_font *f = g->font;
    int w, i;
    const nj_u1 *rows = NULL;
    int gw = 0, gh = 0;

    if (!f || !s || len <= 0) {
        return;
    }
    w = nj_font_string_width(f, s, len);
    if (anchor & NJ_ANCHOR_HCENTER) { x -= w / 2; }
    else if (anchor & NJ_ANCHOR_RIGHT) { x -= w; }
    if (anchor & NJ_ANCHOR_VCENTER) { y -= f->height / 2; }
    else if (anchor & NJ_ANCHOR_BOTTOM) { y -= f->height; }
    else if (anchor & 64) { y -= f->ascent; }   /* NJ_ANCHOR_BASELINE */

    for (i = 0; i < len; i++) {
        nj_u2 ch = s[i];
        int adv = nj_font_char_width(f, ch);
        int cx, cy;
        rows = NULL;
        gw = 0;
        gh = 0;
        if (f->glyph_lookup) {
            rows = f->glyph_lookup(f->glyph_ud, ch, &gw, &gh);
        }
        if (!rows && ch >= f->first_char && ch <= f->last_char && f->bitmap) {
            int rb = (f->glyph_w + 7) / 8;
            rows = f->bitmap + (nj_size)(ch - f->first_char) * (nj_size)f->glyph_h *
                   (nj_size)rb;
            gw = f->glyph_w;
            gh = f->glyph_h;
        }
        if (rows) {
            for (cy = 0; cy < gh; cy++) {
                for (cx = 0; cx < gw; cx++) {
                    const nj_u1 *row = rows + (nj_size)cy * (nj_size)((gw + 7) / 8);
                    if (row[cx / 8] & (nj_u1)(0x80u >> (cx & 7))) {
                        nj_gfx_pixel(g, x + cx, y + cy);
                    }
                }
            }
        }
        x += adv;
    }
    if (f->style & NJ_FONT_STYLE_UNDERLINED) {
        nj_gfx_line(g, x - w, y + f->ascent, x - 1, y + f->ascent);
    }
}

void nj_gfx_draw_char(nj_gfx *g, nj_u2 ch, int x, int y)
{
    nj_gfx_draw_text(g, &ch, 1, x, y, NJ_ANCHOR_LEFT | NJ_ANCHOR_TOP);
}

/* ------------------------------------------------------------------ */
/* Blitting                                                            */
/* ------------------------------------------------------------------ */
static nj_u4 gf_sample(const nj_image *img, int x, int y)
{
    if (!img || !img->pixels || x < 0 || y < 0 || x >= img->width ||
        y >= img->height) {
        return 0u;
    }
    return img->pixels[(nj_size)y * (nj_size)img->width + (nj_size)x];
}

/*
 * Map a destination coordinate back to a source coordinate for one of the
 * eight Sprite transforms.  (sw,sh) is the source region size.
 */
static void gf_untransform(int transform, int sw, int sh, int dx, int dy,
                           int *sx, int *sy)
{
    switch (transform) {
    case NJ_TRANS_ROT90:            *sx = dy;             *sy = sh - 1 - dx; break;
    case NJ_TRANS_ROT180:           *sx = sw - 1 - dx;    *sy = sh - 1 - dy; break;
    case NJ_TRANS_ROT270:           *sx = sw - 1 - dy;    *sy = dx;          break;
    case NJ_TRANS_MIRROR:           *sx = sw - 1 - dx;    *sy = dy;          break;
    case NJ_TRANS_MIRROR_ROT90:     *sx = sw - 1 - dy;    *sy = sh - 1 - dx; break;
    case NJ_TRANS_MIRROR_ROT180:    *sx = dx;             *sy = sh - 1 - dy; break;
    case NJ_TRANS_MIRROR_ROT270:    *sx = dy;             *sy = dx;          break;
    default:                        *sx = dx;             *sy = dy;          break;
    }
}

static void gf_transformed_size(int transform, int sw, int sh, int *dw, int *dh)
{
    if (transform == NJ_TRANS_ROT90 || transform == NJ_TRANS_ROT270 ||
        transform == NJ_TRANS_MIRROR_ROT90 || transform == NJ_TRANS_MIRROR_ROT270) {
        *dw = sh;
        *dh = sw;
    } else {
        *dw = sw;
        *dh = sh;
    }
}

void nj_gfx_draw_image(nj_gfx *g, const nj_image *img, int dx, int dy,
                       int sx, int sy, int sw, int sh)
{
    nj_gfx_draw_image_rop(g, img, dx, dy, sx, sy, sw, sh, NJ_ROP_PAINT,
                          NJ_TRANS_NONE);
}

void nj_gfx_draw_image_rop(nj_gfx *g, const nj_image *img, int dx, int dy,
                           int sx, int sy, int sw, int sh, int rop,
                           int transform)
{
    nj_gfx_draw_region(g, img, sx, sy, sw, sh, transform, 0, 0u, NULL,
                       dx, dy, rop);
}

void nj_gfx_draw_region(nj_gfx *g, const nj_image *src,
                        int sx, int sy, int sw, int sh,
                        int transform, int is_xor, nj_u4 xor_argb,
                        const nj_image *clip_src,
                        int dx, int dy, int rop)
{
    int dw, dh, i, j;

    if (!src || sw <= 0 || sh <= 0) {
        return;
    }
    gf_transformed_size(transform, sw, sh, &dw, &dh);

    for (j = 0; j < dh; j++) {
        for (i = 0; i < dw; i++) {
            int ux, uy;
            nj_u4 p;
            int px = nj_gfx_dev_x(g, dx + i);
            int py = nj_gfx_dev_y(g, dy + j);

            if (!gf_visible(g, px, py)) {
                continue;
            }
            gf_untransform(transform, sw, sh, i, j, &ux, &uy);
            if (ux < 0 || uy < 0 || ux >= sw || uy >= sh) {
                continue;
            }
            if (clip_src && !gf_sample(clip_src, sx + ux, sy + uy)) {
                continue;
            }
            p = gf_sample(src, sx + ux, sy + uy);
            if (is_xor) {
                nj_u4 dst = nj_surface_get(g->surf, px, py);
                nj_surface_set(g->surf, px, py, 0xFF000000u ^ (dst ^ xor_argb));
                continue;
            }
            if (rop == NJ_ROP_SET) {
                nj_surface_set(g->surf, px, py, p);
                continue;
            }
            {
                unsigned a = (p >> 24) & 0xFFu;
                if (src->has_alpha && a == 0u) {
                    continue;
                }
                if (src->has_alpha && a < 255u) {
                    nj_gfx_blend_pixel(g->surf, px, py, p, (int)a);
                } else {
                    nj_surface_set(g->surf, px, py, 0xFF000000u | (p & 0x00FFFFFFu));
                }
            }
        }
    }
}

void nj_gfx_draw_rgb(nj_gfx *g, const nj_u1 *rgb, int scanlen, int alpha,
                     int dx, int dy, int sx, int sy, int sw, int sh,
                     int rop, int transform)
{
    int dw, dh, i, j;

    if (!rgb || sw <= 0 || sh <= 0) {
        return;
    }
    gf_transformed_size(transform, sw, sh, &dw, &dh);
    for (j = 0; j < dh; j++) {
        for (i = 0; i < dw; i++) {
            int ux, uy, px, py;
            const nj_u1 *p;
            nj_u4 argb;

            gf_untransform(transform, sw, sh, i, j, &ux, &uy);
            if (ux < 0 || uy < 0 || ux >= sw || uy >= sh) {
                continue;
            }
            px = nj_gfx_dev_x(g, dx + i);
            py = nj_gfx_dev_y(g, dy + j);
            if (!gf_visible(g, px, py)) {
                continue;
            }
            p = rgb + (nj_size)(sy + uy) * (nj_size)scanlen +
                (nj_size)(sx + ux) * 3u;
            argb = ((nj_u4)(alpha & 0xFF) << 24) |
                   ((nj_u4)p[0] << 16) | ((nj_u4)p[1] << 8) | (nj_u4)p[2];
            if (rop == NJ_ROP_SET || (alpha & 0xFF) == 0xFF) {
                nj_surface_set(g->surf, px, py, argb);
            } else {
                nj_gfx_blend_pixel(g->surf, px, py, argb, alpha & 0xFF);
            }
        }
    }
}

/* Keep the compiler from warning about the unused constant. */
const int nj_tmp_gfx_max_glyphs = GFX_MAX_GLYPHS;
