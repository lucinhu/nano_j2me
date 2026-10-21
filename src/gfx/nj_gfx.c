/*
 * nano_j2me - nj_gfx.c
 *
 * Software rasterizer for javax.microedition.lcdui.Graphics: every primitive
 * MIDP needs, drawn straight into a linear pixel buffer.  Read nj_gfx.h first;
 * this file fixes the conventions the API leaves open.
 *
 * CONVENTIONS AND RESOLVED SPEC AMBIGUITIES
 * -----------------------------------------
 * Coordinate system (MIDP, Graphics.html):
 *   - A pixel is the unit square, so the rectangle (x, y, w, h) *filled* by
 *     fillRect covers exactly w*h pixels, columns x..x+w-1, rows y..y+h-1.
 *   - DRAW operations are one pixel bigger, exactly as the MIDP javadoc says
 *     ("drawRect ... will cover an area (width + 1) pixels wide by
 *     (height + 1) pixels tall"): drawRect/drawRoundRect/drawArc cover the
 *     box [x, x+w] x [y, y+h].  w == 0 or h == 0 therefore still draws a
 *     degenerate line, while w < 0 or h < 0 draws nothing.  Fill primitives
 *     treat w <= 0 or h <= 0 as "nothing".
 *   - Angles are MIDP degrees: 0 at 3 o'clock, positive counter-clockwise
 *     (so 90 is 12 o'clock on a y-down screen).  They are *parametric*
 *     angles, which is what makes "45 degrees always falls on the line from
 *     the centre of the ellipse to the upper right corner of the bounding
 *     rectangle" true for a non-square bounding box: a point at angle `a` is
 *     (cx + a_axis*cos a, cy - b_axis*sin a).
 *   - fillArc fills the PIE WEDGE bounded by the arc and the two radii from
 *     the centre to the arc endpoints (javadoc, fillArc).
 *   - drawRoundRect/fillRoundRect arcWidth/arcHeight are the FULL width and
 *     height of the corner arcs (diameters), not radii; they are clamped to
 *     the rectangle, and rw <= 0 or rh <= 0 degrades to a plain rectangle.
 *
 * Translation and clipping:
 *   - All user coordinates are offset by (translate_x, translate_y).
 *   - The clip is stored in DEVICE space, always intersected with the surface
 *     bounds, and is never moved by nj_gfx_translate() (MIDP: "Operations on
 *     the coordinate system, such as translate(), do not modify the clip").
 *   - nj_gfx_set_clip() takes USER coordinates and converts by adding the
 *     current translate before intersecting, matching Graphics.setClip();
 *     nj_gfx_clip_rect() intersects the existing clip with a user rectangle.
 *
 * Robustness:
 *   - Every coordinate is widened to 64-bit before arithmetic.  Rectangles are
 *     clipped before any pixel loop runs, so loops are bounded by the clip
 *     (i.e. the surface) even for coordinates like INT_MIN or +/-100000.
 *   - Shape arithmetic (line/triangle/ellipse) clamps coordinates to
 *     +/-NJ_GFX_COORD_LIMIT so that products stay inside 64 bits; the limit is
 *     far outside any real panel, and clipping happens first in practice.
 *
 * Pixel formats:
 *   - nj_surface.bpp is the cached bytes-per-pixel (filled in by nj_gfx_init);
 *     stride is BYTES and may exceed width*bpp, so only width pixels of each
 *     row are ever touched (padding is never read or written).
 *   - nj_surface_get() always returns 0xAARRGGBB; RGB565/RGB888 have no alpha
 *     storage, so they read back as fully opaque RGBA-expanded values.
 *   - nj_surface_format_color() packs 0xAARRGGBB into the surface format; the
 *     alpha byte is dropped by the RGB formats.
 *
 * Painting:
 *   - nj_gfx.color is 0xAARRGGBB.  NJ_BLEND_NONE writes the RGB and ignores
 *     alpha; NJ_BLEND_SRC_OVER composites in 8-bit-per-channel space and
 *     repacks.  Source-over is the default because MIDP images need it.
 *   - While XOR mode is on, primitives write dst ^ xor_color (both values
 *     packed into the surface format, so XOR is well defined on 16-bit
 *     panels) and the blend mode is ignored.
 *   - Midpoint-Bresenham lines; DOTTED alternates one pixel on / one off
 *     along the path, and the phase is carried around rectangle, rounded
 *     rectangle and arc outlines so the dots stay evenly spaced.
 *   - Images: destination-anchored inverse mapping for all 8 Sprite
 *     transforms, which keeps clipping trivially correct and allocates
 *     nothing.  NJ_ROP_SET replaces the destination (even for fully
 *     transparent source pixels), NJ_ROP_PAINT skips fully transparent pixels
 *     and alpha blends the rest.
 *   - An nj_image is a PACKED byte buffer whose layout follows nj_image.format
 *     (RGB565: 2 bytes/pixel, RGB888: 3, ARGB8888: 4; stride = width * bpp),
 *     exactly what nj_codec_image_new() allocates.  RGB565/RGB888 sources are
 *     opaque and expanded to 8 bits per channel while blitting, so a decoder
 *     may hand the rasterizer a half-size RGB565 frame.
 *   - XOR: colour primitives write dst ^ xor_color; an image blit writes
 *     dst ^ source ^ xor_argb, so a sprite can be erased and restored with a
 *     bare dst ^= source.
 */

#include "nj2me/nj_gfx.h"

#include <string.h>   /* memcpy: alignment-safe 16/32-bit pixel access */

/* ------------------------------------------------------------------ */
/* Limits / fixed point                                                */
/* ------------------------------------------------------------------ */
/* Largest coordinate any shape helper will work with.  Chosen so that every
 * product in this file stays comfortably inside int64_t. */
#define NJ_GFX_COORD_LIMIT ((nj_s8)1 << 24)

/* Sine table resolution: sin(deg) in Q14. */
#define NJ_GFX_FP_BITS 14
#define NJ_GFX_FP_ONE  (1 << NJ_GFX_FP_BITS)

/* Extra precision kept in the fillArc radius vectors (they are only used for
 * the sign of a cross product, so a positive scale is free). */
#define NJ_GFX_VEC_SHIFT 8

/* ------------------------------------------------------------------ */
/* Immutable file-scope data: sin(0..90 degrees) in Q14               */
/* ------------------------------------------------------------------ */
static const nj_s2 nj_gfx_sin_tab[91] = {
        0,   286,   572,   857,  1143,  1428,  1713,  1997,  2280,  2563,
     2845,  3126,  3406,  3686,  3964,  4240,  4516,  4790,  5063,  5334,
     5604,  5872,  6138,  6402,  6664,  6924,  7182,  7438,  7692,  7943,
     8192,  8438,  8682,  8923,  9162,  9397,  9630,  9860, 10087, 10311,
    10531, 10749, 10963, 11174, 11381, 11585, 11786, 11982, 12176, 12365,
    12551, 12733, 12911, 13085, 13255, 13421, 13583, 13741, 13894, 14044,
    14189, 14330, 14466, 14598, 14726, 14849, 14968, 15082, 15191, 15296,
    15396, 15491, 15582, 15668, 15749, 15826, 15897, 15964, 16026, 16083,
    16135, 16182, 16225, 16262, 16294, 16322, 16344, 16362, 16374, 16382,
    16384
};

/* sin of any integer degree, Q14. */
static int nj_sin_q14(int deg)
{
    int d = deg % 360;

    if (d < 0) {
        d += 360;
    }
    if (d <= 90) {
        return nj_gfx_sin_tab[d];
    }
    if (d <= 180) {
        return nj_gfx_sin_tab[180 - d];
    }
    if (d <= 270) {
        return -nj_gfx_sin_tab[d - 180];
    }
    return -nj_gfx_sin_tab[360 - d];
}

static int nj_cos_q14(int deg)
{
    return nj_sin_q14(deg + 90);
}

/* ------------------------------------------------------------------ */
/* Small integer helpers (all 64-bit, never wrap)                      */
/* ------------------------------------------------------------------ */
static nj_s8 nj_min64(nj_s8 a, nj_s8 b) { return a < b ? a : b; }
static nj_s8 nj_max64(nj_s8 a, nj_s8 b) { return a > b ? a : b; }

static nj_s8 nj_clamp64(nj_s8 v, nj_s8 lo, nj_s8 hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static nj_s8 nj_abs64(nj_s8 v) { return v < 0 ? -v : v; }

/* a / b rounded towards -inf; b must be > 0. */
static nj_s8 nj_floor_div(nj_s8 a, nj_s8 b)
{
    nj_s8 q = a / b;

    if ((a % b) != 0 && a < 0) {
        q--;
    }
    return q;
}

/* a / b rounded towards +inf; b must be > 0. */
static nj_s8 nj_ceil_div(nj_s8 a, nj_s8 b)
{
    nj_s8 q = a / b;

    if ((a % b) != 0 && a > 0) {
        q++;
    }
    return q;
}

/* round(v / 2) for a doubled coordinate, symmetric about zero. */
static nj_s8 nj_half(nj_s8 v)
{
    return v >= 0 ? (v + 1) / 2 : -((-v + 1) / 2);
}

/* floor(sqrt(v)), v <= 2^62. */
static nj_u8 nj_isqrt(nj_u8 v)
{
    nj_u8 r = 0u;
    nj_u8 bit = (nj_u8)1 << 62;

    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0u) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

/* ------------------------------------------------------------------ */
/* Pixel access                                                        */
/* ------------------------------------------------------------------ */
static int nj_format_bpp(int format)
{
    switch (format) {
    case NJ_PIXFMT_RGB565:   return 2;
    case NJ_PIXFMT_RGB888:   return 3;
    case NJ_PIXFMT_ARGB8888: return 4;
    default:                 return 0;
    }
}

/* Trust the cached bpp, but stay usable when a port built the surface by hand
 * and never called nj_gfx_init(). */
static int nj_surface_bpp(const nj_surface *s)
{
    if (s->bpp > 0) {
        return s->bpp;
    }
    return nj_format_bpp(s->format);
}

/* A surface is only addressable when the row pitch can hold a full row. */
static int nj_surface_ok(const nj_surface *s)
{
    int bpp;

    if (!s || !s->pixels || s->width <= 0 || s->height <= 0) {
        return 0;
    }
    bpp = nj_surface_bpp(s);
    if (bpp == 0) {
        return 0;
    }
    if ((nj_s8)s->stride < (nj_s8)s->width * bpp) {
        return 0;
    }
    return 1;
}

static nj_u4 nj_raw_get(const nj_surface *s, int x, int y, int bpp)
{
    const nj_u1 *p = (const nj_u1 *)s->pixels
                   + (nj_s8)y * s->stride + (nj_s8)x * bpp;

    switch (s->format) {
    case NJ_PIXFMT_RGB565: {
        nj_u2 v;
        memcpy(&v, p, sizeof v);
        return (nj_u4)v;
    }
    case NJ_PIXFMT_RGB888:
        return ((nj_u4)p[0] << 16) | ((nj_u4)p[1] << 8) | (nj_u4)p[2];
    default: {
        nj_u4 v;
        memcpy(&v, p, sizeof v);
        return v;
    }
    }
}

static void nj_raw_set(nj_surface *s, int x, int y, int bpp, nj_u4 packed)
{
    nj_u1 *p = (nj_u1 *)s->pixels + (nj_s8)y * s->stride + (nj_s8)x * bpp;

    switch (s->format) {
    case NJ_PIXFMT_RGB565: {
        nj_u2 v = (nj_u2)packed;
        memcpy(p, &v, sizeof v);
        break;
    }
    case NJ_PIXFMT_RGB888:
        p[0] = (nj_u1)((packed >> 16) & 0xFFu);
        p[1] = (nj_u1)((packed >> 8) & 0xFFu);
        p[2] = (nj_u1)(packed & 0xFFu);
        break;
    default: {
        nj_u4 v = packed;
        memcpy(p, &v, sizeof v);
        break;
    }
    }
}

nj_u4 nj_surface_format_color(int format, nj_u4 argb)
{
    nj_u4 r = (argb >> 16) & 0xFFu;
    nj_u4 g = (argb >> 8) & 0xFFu;
    nj_u4 b = argb & 0xFFu;

    switch (format) {
    case NJ_PIXFMT_RGB565:
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    case NJ_PIXFMT_RGB888:
        return (r << 16) | (g << 8) | b;
    default:
        return argb;
    }
}

nj_u4 nj_surface_get(const nj_surface *s, int x, int y)
{
    int bpp;
    nj_u4 v;

    if (!nj_surface_ok(s)) {
        return 0u;
    }
    if (x < 0 || y < 0 || x >= s->width || y >= s->height) {
        return 0u;
    }
    bpp = nj_surface_bpp(s);
    v = nj_raw_get(s, x, y, bpp);

    if (s->format == NJ_PIXFMT_RGB565) {
        nj_u4 r = (v >> 11) & 0x1Fu;
        nj_u4 g = (v >> 5) & 0x3Fu;
        nj_u4 b = v & 0x1Fu;

        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    if (s->format == NJ_PIXFMT_RGB888) {
        return 0xFF000000u | v;
    }
    return v;
}

void nj_surface_set(nj_surface *s, int x, int y, nj_u4 argb)
{
    int bpp;

    if (!nj_surface_ok(s)) {
        return;
    }
    if (x < 0 || y < 0 || x >= s->width || y >= s->height) {
        return;
    }
    bpp = nj_surface_bpp(s);
    nj_raw_set(s, x, y, bpp, nj_surface_format_color(s->format, argb));
}

void nj_surface_fill(nj_surface *s, nj_u4 argb)
{
    int bpp, x, y;
    nj_u4 packed;

    if (!nj_surface_ok(s)) {
        return;
    }
    bpp = nj_surface_bpp(s);
    packed = nj_surface_format_color(s->format, argb);
    for (y = 0; y < s->height; y++) {
        for (x = 0; x < s->width; x++) {
            nj_raw_set(s, x, y, bpp, packed);
        }
    }
}

void nj_gfx_blend_pixel(nj_surface *s, int x, int y, nj_u4 argb, int alpha)
{
    int bpp, a;
    nj_u4 dst, out;
    nj_u4 sr, sg, sb;
    nj_u4 dr, dg, db, da;
    nj_u4 nr, ng, nb, na;

    if (!nj_surface_ok(s)) {
        return;
    }
    if (x < 0 || y < 0 || x >= s->width || y >= s->height) {
        return;
    }
    if (alpha < 0) {
        alpha = 0;
    }
    if (alpha > 255) {
        alpha = 255;
    }
    bpp = nj_surface_bpp(s);
    a = (int)((argb >> 24) & 0xFFu);
    a = (a * alpha + 127) / 255;         /* coverage-scaled source alpha */
    if (a <= 0) {
        return;                          /* fully transparent: leave dst */
    }
    if (a >= 255) {
        nj_raw_set(s, x, y, bpp, nj_surface_format_color(s->format, argb));
        return;
    }

    dst = nj_surface_get(s, x, y);       /* 0xAARRGGBB */
    sr = (argb >> 16) & 0xFFu;
    sg = (argb >> 8) & 0xFFu;
    sb = argb & 0xFFu;
    dr = (dst >> 16) & 0xFFu;
    dg = (dst >> 8) & 0xFFu;
    db = dst & 0xFFu;
    da = (dst >> 24) & 0xFFu;

    /* Source over destination, 8 bits per channel, round to nearest.  The
     * alpha channel follows Porter-Duff exactly (a + da*(1-a)), which keeps
     * an opaque destination opaque; the colour channels are the usual
     * non-premultiplied lerp. */
    nr = (sr * (nj_u4)a + dr * (nj_u4)(255 - a) + 127u) / 255u;
    ng = (sg * (nj_u4)a + dg * (nj_u4)(255 - a) + 127u) / 255u;
    nb = (sb * (nj_u4)a + db * (nj_u4)(255 - a) + 127u) / 255u;
    na = ((nj_u4)a * 255u + da * (nj_u4)(255 - a) + 127u) / 255u;

    out = (na << 24) | (nr << 16) | (ng << 8) | nb;
    nj_raw_set(s, x, y, bpp, nj_surface_format_color(s->format, out));
}

/* ------------------------------------------------------------------ */
/* Graphics state                                                      */
/* ------------------------------------------------------------------ */
void nj_gfx_init(nj_gfx *g, nj_surface *surf)
{
    if (!g) {
        return;
    }
    g->surf = surf;
    g->color = 0xFF000000u;            /* opaque black */
    g->translate_x = 0;
    g->translate_y = 0;
    g->stroke = NJ_STROKE_SOLID;
    g->blend = NJ_BLEND_SRC_OVER;      /* MIDP composites images */
    g->xor_color = 0u;
    g->use_xor = 0;
    g->font = nj_font_builtin(NJ_FONT_SIZE_MEDIUM, NJ_FONT_STYLE_PLAIN);

    if (surf) {
        surf->bpp = nj_format_bpp(surf->format);   /* cache bytes per pixel */
        if (surf->width > 0 && surf->height > 0) {
            g->clip_x = 0;
            g->clip_y = 0;
            g->clip_w = surf->width;
            g->clip_h = surf->height;
        } else {
            g->clip_x = 0;
            g->clip_y = 0;
            g->clip_w = 0;
            g->clip_h = 0;
        }
    } else {
        g->clip_x = 0;
        g->clip_y = 0;
        g->clip_w = 0;
        g->clip_h = 0;
    }
}

int nj_gfx_dev_x(const nj_gfx *g, int x)
{
    nj_s8 v;

    if (!g) {
        return x;
    }
    v = (nj_s8)x + g->translate_x;
    v = nj_clamp64(v, -NJ_GFX_COORD_LIMIT, NJ_GFX_COORD_LIMIT);
    return (int)v;
}

int nj_gfx_dev_y(const nj_gfx *g, int y)
{
    nj_s8 v;

    if (!g) {
        return y;
    }
    v = (nj_s8)y + g->translate_y;
    v = nj_clamp64(v, -NJ_GFX_COORD_LIMIT, NJ_GFX_COORD_LIMIT);
    return (int)v;
}

/* ------------------------------------------------------------------ */
/* Clip                                                                */
/* ------------------------------------------------------------------ */
static void nj_clip_empty(nj_gfx *g)
{
    g->clip_x = 0;
    g->clip_y = 0;
    g->clip_w = 0;
    g->clip_h = 0;
}

void nj_gfx_set_clip(nj_gfx *g, int x, int y, int w, int h)
{
    nj_s8 x0, y0, x1, y1;

    if (!g) {
        return;
    }
    if (!g->surf || w <= 0 || h <= 0) {
        nj_clip_empty(g);
        return;
    }
    /* USER coordinates: offset by the translate, then intersect with the
     * surface bounds (never with the previous clip: setClip replaces it). */
    x0 = (nj_s8)x + g->translate_x;
    y0 = (nj_s8)y + g->translate_y;
    x1 = x0 + w;
    y1 = y0 + h;

    x0 = nj_max64(x0, 0);
    y0 = nj_max64(y0, 0);
    x1 = nj_min64(x1, g->surf->width);
    y1 = nj_min64(y1, g->surf->height);

    if (x1 <= x0 || y1 <= y0) {
        nj_clip_empty(g);
        return;
    }
    g->clip_x = (int)x0;
    g->clip_y = (int)y0;
    g->clip_w = (int)(x1 - x0);
    g->clip_h = (int)(y1 - y0);
}

void nj_gfx_clip_rect(nj_gfx *g, int x, int y, int w, int h)
{
    nj_s8 x0, y0, x1, y1;

    if (!g || !g->surf) {
        return;
    }
    if (w <= 0 || h <= 0 || g->clip_w <= 0 || g->clip_h <= 0) {
        nj_clip_empty(g);
        return;
    }
    x0 = (nj_s8)x + g->translate_x;
    y0 = (nj_s8)y + g->translate_y;
    x1 = x0 + w;
    y1 = y0 + h;

    x0 = nj_max64(x0, g->clip_x);
    y0 = nj_max64(y0, g->clip_y);
    x1 = nj_min64(x1, (nj_s8)g->clip_x + g->clip_w);
    y1 = nj_min64(y1, (nj_s8)g->clip_y + g->clip_h);

    if (x1 <= x0 || y1 <= y0) {
        nj_clip_empty(g);
        return;
    }
    g->clip_x = (int)x0;
    g->clip_y = (int)y0;
    g->clip_w = (int)(x1 - x0);
    g->clip_h = (int)(y1 - y0);
}

void nj_gfx_translate(nj_gfx *g, int dx, int dy)
{
    if (!g) {
        return;
    }
    /*
     * Cumulative, and the clip does NOT move with it.  MIDP 2.0 puts it plainly
     * (Graphics.translate): "Operations on the coordinate system, such as
     * translate(), do not modify the clip.  The methods getClipX(), getClipY(),
     * getClipWidth() and getClipHeight() must return a rectangle that, if passed
     * to setClip without an intervening change to the Graphics object's
     * coordinate system, must result in the identical set of pixels in the
     * clip."
     *
     * So the clip is a rectangle of PIXELS on the target and it stays where it
     * is; only the coordinate system moves.  Shifting it here looked harmless
     * and is not: 帝国时代3-亚洲王朝_6288 draws its map by translating the origin
     * by the camera (-336,-216) and then blitting tiles at map coordinates -
     * every tile lands on the screen, but the clip had been dragged off it, so
     * the whole map was clipped away and the campaign showed a black field with
     * only the untranslated HUD on it.  setClip() after a translate() still
     * takes coordinates in the CURRENT system (it adds the translate itself),
     * so the two orders are deliberately not the same thing.
     */
    g->translate_x = (int)nj_clamp64((nj_s8)g->translate_x + dx,
                                     -NJ_GFX_COORD_LIMIT, NJ_GFX_COORD_LIMIT);
    g->translate_y = (int)nj_clamp64((nj_s8)g->translate_y + dy,
                                     -NJ_GFX_COORD_LIMIT, NJ_GFX_COORD_LIMIT);
}

void nj_gfx_get_translate(const nj_gfx *g, int *dx, int *dy)
{
    if (dx) {
        *dx = g ? g->translate_x : 0;
    }
    if (dy) {
        *dy = g ? g->translate_y : 0;
    }
}

int nj_gfx_get_clip_x(const nj_gfx *g) { return g ? g->clip_x : 0; }
int nj_gfx_get_clip_y(const nj_gfx *g) { return g ? g->clip_y : 0; }
int nj_gfx_get_clip_w(const nj_gfx *g) { return g ? g->clip_w : 0; }
int nj_gfx_get_clip_h(const nj_gfx *g) { return g ? g->clip_h : 0; }

/* ------------------------------------------------------------------ */
/* Color / style                                                       */
/* ------------------------------------------------------------------ */
void nj_gfx_set_color(nj_gfx *g, nj_u4 argb)
{
    if (g) {
        g->color = argb;
    }
}

nj_u4 nj_gfx_get_color(const nj_gfx *g)
{
    return g ? g->color : 0u;
}

/* MIDP setColor(red, green, blue) paints with a fully opaque colour, so the
 * alpha byte is forced to 0xFF; use nj_gfx_set_alpha() (or set_color) for
 * transparency. */
void nj_gfx_set_color_rgb(nj_gfx *g, int r, int g_, int b)
{
    if (!g) {
        return;
    }
    g->color = 0xFF000000u
             | ((nj_u4)(r & 0xFF) << 16)
             | ((nj_u4)(g_ & 0xFF) << 8)
             | (nj_u4)(b & 0xFF);
}

void nj_gfx_set_alpha(nj_gfx *g, int alpha0_255)
{
    int a = alpha0_255;

    if (!g) {
        return;
    }
    if (a < 0) {
        a = 0;
    }
    if (a > 255) {
        a = 255;
    }
    g->color = (g->color & 0x00FFFFFFu) | ((nj_u4)a << 24);
}

int nj_gfx_get_alpha(const nj_gfx *g)
{
    return g ? (int)((g->color >> 24) & 0xFFu) : 0;
}

void nj_gfx_set_blend(nj_gfx *g, int mode)
{
    if (g) {
        g->blend = mode;
    }
}

void nj_gfx_set_stroke(nj_gfx *g, int stroke)
{
    if (g) {
        g->stroke = stroke;
    }
}

void nj_gfx_set_font(nj_gfx *g, const nj_font *f)
{
    if (g) {
        g->font = f;
    }
}

void nj_gfx_set_xor(nj_gfx *g, nj_u4 argb)
{
    if (g) {
        g->xor_color = argb;
        g->use_xor = 1;
    }
}

void nj_gfx_clear_xor(nj_gfx *g)
{
    if (g) {
        g->use_xor = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Painting helpers                                                    */
/* ------------------------------------------------------------------ */
static int nj_in_clip(const nj_gfx *g, int x, int y)
{
    return x >= g->clip_x && y >= g->clip_y
        && x < g->clip_x + g->clip_w && y < g->clip_y + g->clip_h;
}

static void nj_plot(nj_gfx *g, int x, int y, nj_u4 argb);
static void nj_span(nj_gfx *g, int x0, int x1, int y, nj_u4 argb);

/* Must only be called with a point inside the clip. */
static void nj_plot(nj_gfx *g, int x, int y, nj_u4 argb)
{
    if (g->use_xor) {
        /* XOR mode: the destination is XORed with the configured colour and
         * the drawing colour is ignored ("writes dst ^ xor_color"). */
        nj_u4 dst = nj_surface_get(g->surf, x, y);
        nj_u4 v = nj_surface_format_color(g->surf->format, dst)
                ^ nj_surface_format_color(g->surf->format, g->xor_color);
        nj_raw_set(g->surf, x, y, nj_surface_bpp(g->surf), v);
        return;
    }
    if (g->blend == NJ_BLEND_SRC_OVER) {
        nj_gfx_blend_pixel(g->surf, x, y, argb, 255);
        return;
    }
    nj_raw_set(g->surf, x, y, nj_surface_bpp(g->surf),
               nj_surface_format_color(g->surf->format, argb));
}

/*
 * Blend the current colour at `coverage` (0..255), honouring translation
 * from user coordinates and the device clip.
 *
 * This is what an antialiased glyph needs and what nj_plot alone cannot give
 * it: coverage scales the colour's alpha per pixel instead of replacing the
 * destination, so the rim of a stroke fades into the background.
 *
 * A fully covered pixel is handed to nj_plot rather than blended, so the solid
 * interior of a glyph keeps the renderer's exact semantics (XOR mode, blend
 * mode, format conversion) and only the antialiased rim pays for a blend.  On
 * a 1bpp font every pixel takes that path, which is why antialiasing costs
 * nothing until a face actually produces partial coverage.
 */
void nj_gfx_blend_coverage(nj_gfx *g, int x, int y, int coverage)
{
    if (!g || coverage <= 0) {
        return;
    }
    x = nj_gfx_dev_x(g, x);
    y = nj_gfx_dev_y(g, y);
    if (!nj_in_clip(g, x, y)) {
        return;
    }
    if (coverage >= 255) {
        nj_plot(g, x, y, g->color);
        return;
    }
    nj_gfx_blend_pixel(g->surf, x, y, g->color, coverage);
}

/* Inclusive horizontal run, already clipped to the clip rectangle. */
static void nj_span(nj_gfx *g, int x0, int x1, int y, nj_u4 argb)
{
    int x;

    if (x1 < x0) {
        return;
    }
    /*
     * A fully opaque colour leaves every pixel of the run at `argb` whatever
     * the blend mode is, so the run is a fill - and nj_plot() plus
     * nj_gfx_blend_pixel() would re-ask the surface for its format and
     * re-check its bounds once per pixel to discover that.  nj_span showed up
     * as 74% of the rasteriser time in a profiled run of
     * 合金弹头4(完美汉化版)_4053 (230M calls through nj_plot and
     * nj_gfx_blend_pixel for 3000 frames), so write the bytes here.
     */
    if (!g->use_xor
        && (g->blend != NJ_BLEND_SRC_OVER || ((argb >> 24) & 0xFFu) == 255u)) {
        int bpp = nj_surface_bpp(g->surf);
        nj_u4 packed = nj_surface_format_color(g->surf->format, argb);
        nj_u1 *p = (nj_u1 *)g->surf->pixels + (nj_s8)y * g->surf->stride
                 + (nj_s8)x0 * bpp;

        if (bpp == 2) {
            nj_u2 v = (nj_u2)packed;

            for (x = x0; x <= x1; x++) {
                memcpy(p, &v, sizeof v);
                p += 2;
            }
        } else if (bpp == 4) {
            nj_u4 v = packed;

            for (x = x0; x <= x1; x++) {
                memcpy(p, &v, sizeof v);
                p += 4;
            }
        } else if (bpp == 3) {
            nj_u1 r = (nj_u1)((packed >> 16) & 0xFFu);
            nj_u1 gg = (nj_u1)((packed >> 8) & 0xFFu);
            nj_u1 b = (nj_u1)(packed & 0xFFu);

            for (x = x0; x <= x1; x++) {
                p[0] = r;
                p[1] = gg;
                p[2] = b;
                p += 3;
            }
        } else {
            for (x = x0; x <= x1; x++) {
                nj_raw_set(g->surf, x, y, bpp, packed);
            }
        }
        return;
    }
    for (x = x0; x <= x1; x++) {
        nj_plot(g, x, y, argb);
    }
}

/* User-space rectangle -> inclusive device box clipped to the clip rect.
 * Returns 0 when nothing is visible.  w/h <= 0 is an empty request. */
static int nj_box_from_user(const nj_gfx *g, nj_s8 ux, nj_s8 uy, nj_s8 uw,
                            nj_s8 uh, nj_s8 *bx0, nj_s8 *by0, nj_s8 *bx1,
                            nj_s8 *by1)
{
    nj_s8 x0, y0, x1, y1;

    if (!g || !nj_surface_ok(g->surf)) {
        return 0;
    }
    if (uw <= 0 || uh <= 0 || g->clip_w <= 0 || g->clip_h <= 0) {
        return 0;
    }
    x0 = ux + g->translate_x;
    y0 = uy + g->translate_y;
    x1 = x0 + uw - 1;
    y1 = y0 + uh - 1;

    x0 = nj_max64(x0, g->clip_x);
    y0 = nj_max64(y0, g->clip_y);
    x1 = nj_min64(x1, (nj_s8)g->clip_x + g->clip_w - 1);
    y1 = nj_min64(y1, (nj_s8)g->clip_y + g->clip_h - 1);

    if (x0 > x1 || y0 > y1) {
        return 0;
    }
    *bx0 = x0;
    *by0 = y0;
    *bx1 = x1;
    *by1 = y1;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Line clipping (Liang-Barsky, exact fixed point) and Bresenham       */
/* ------------------------------------------------------------------ */
#define NJ_GFX_LB_BITS 30
#define NJ_GFX_LB_ONE  ((nj_s8)1 << NJ_GFX_LB_BITS)

/*
 * Clip the segment to the current clip rectangle, in device space.
 * Returns 0 when the segment is invisible; otherwise the endpoints are
 * updated to (approximately) the visible part.  The parameter t in [0,1] is
 * kept as an integer scaled by 2^30, so the test is exact and the routine
 * cannot loop.
 */
static int nj_clip_segment(const nj_gfx *g, nj_s8 *px1, nj_s8 *py1,
                           nj_s8 *px2, nj_s8 *py2)
{
    nj_s8 xmin = g->clip_x;
    nj_s8 ymin = g->clip_y;
    nj_s8 xmax = (nj_s8)g->clip_x + g->clip_w - 1;
    nj_s8 ymax = (nj_s8)g->clip_y + g->clip_h - 1;
    nj_s8 x1 = *px1, y1 = *py1, x2 = *px2, y2 = *py2;
    nj_s8 dx = x2 - x1, dy = y2 - y1;
    nj_s8 n0 = 0, n1 = NJ_GFX_LB_ONE;
    nj_s8 p[4], q[4];
    int i;

    if (g->clip_w <= 0 || g->clip_h <= 0) {
        return 0;
    }

    p[0] = -dx; q[0] = x1 - xmin;
    p[1] =  dx; q[1] = xmax - x1;
    p[2] = -dy; q[2] = y1 - ymin;
    p[3] =  dy; q[3] = ymax - y1;

    for (i = 0; i < 4; i++) {
        if (p[i] == 0) {
            if (q[i] < 0) {
                return 0;                /* parallel and outside */
            }
        } else if (p[i] > 0) {
            nj_s8 lim = nj_floor_div(q[i] * NJ_GFX_LB_ONE, p[i]);
            if (lim < n1) {
                n1 = lim;
            }
        } else {
            nj_s8 lim = nj_ceil_div(-q[i] * NJ_GFX_LB_ONE, -p[i]);
            if (lim > n0) {
                n0 = lim;
            }
        }
    }
    if (n0 > n1) {
        return 0;
    }

    /* Round the start down and the end up: any pixel outside the clip that
     * this produces is discarded by the per-pixel test, while rounding the
     * other way could lose the last visible pixel. */
    *px1 = x1 + nj_floor_div(dx * n0, NJ_GFX_LB_ONE);
    *py1 = y1 + nj_floor_div(dy * n0, NJ_GFX_LB_ONE);
    *px2 = x1 + nj_ceil_div(dx * n1, NJ_GFX_LB_ONE);
    *py2 = y1 + nj_ceil_div(dy * n1, NJ_GFX_LB_ONE);
    return 1;
}

/*
 * Midpoint-Bresenham between two DEVICE points.
 *
 * `phase` carries the DOTTED pattern around a multi-segment outline: on entry
 * it is the parity of the first pixel of the *unclipped* path, and it is left
 * holding the parity for whatever is drawn next.  Clipping only shifts it by
 * the number of major-axis steps that were cut off the front, which is exact
 * because one Bresenham step advances the major axis by exactly one.
 */
static void nj_line_dev(nj_gfx *g, nj_s8 ux1, nj_s8 uy1, nj_s8 ux2, nj_s8 uy2,
                        int *phase)
{
    nj_s8 x1 = nj_clamp64(ux1, -NJ_GFX_COORD_LIMIT, NJ_GFX_COORD_LIMIT);
    nj_s8 y1 = nj_clamp64(uy1, -NJ_GFX_COORD_LIMIT, NJ_GFX_COORD_LIMIT);
    nj_s8 x2 = nj_clamp64(ux2, -NJ_GFX_COORD_LIMIT, NJ_GFX_COORD_LIMIT);
    nj_s8 y2 = nj_clamp64(uy2, -NJ_GFX_COORD_LIMIT, NJ_GFX_COORD_LIMIT);
    nj_s8 dx, dy, adx, ady, steps, cx1, cy1, cx2, cy2, skipped;
    int p0 = phase ? (*phase & 1) : 0;
    int sx, sy, n;
    nj_s8 err;
    int dotted = (g->stroke == NJ_STROKE_DOTTED);

    dx = x2 - x1;
    dy = y2 - y1;
    adx = nj_abs64(dx);
    ady = nj_abs64(dy);
    steps = (adx > ady ? adx : ady) + 1;      /* pixels in the full path */

    if (phase) {
        *phase = (int)((p0 + (int)(steps & 1)) & 1);
    }

    if (adx == 0 && ady == 0) {
        if (nj_in_clip(g, (int)x1, (int)y1)) {
            if (!dotted || p0 == 0) {
                nj_plot(g, (int)x1, (int)y1, g->color);
            }
        }
        return;
    }

    cx1 = x1; cy1 = y1; cx2 = x2; cy2 = y2;
    if (!nj_clip_segment(g, &cx1, &cy1, &cx2, &cy2)) {
        return;
    }
    /* Keep the dot phase aligned with the unclipped path. */
    skipped = (adx >= ady) ? nj_abs64(cx1 - x1) : nj_abs64(cy1 - y1);
    p0 = (int)((p0 + (int)(skipped & 1)) & 1);

    dx = cx2 - cx1;
    dy = cy2 - cy1;
    adx = nj_abs64(dx);
    ady = nj_abs64(dy);
    if (adx + ady > (nj_s8)(g->clip_w + g->clip_h + 4)) {
        /* Defensive: the clipper should already have made the segment small. */
        nj_s8 xmax = (nj_s8)g->clip_x + g->clip_w - 1;
        nj_s8 ymax = (nj_s8)g->clip_y + g->clip_h - 1;
        cx1 = nj_clamp64(cx1, g->clip_x, xmax);
        cy1 = nj_clamp64(cy1, g->clip_y, ymax);
        cx2 = nj_clamp64(cx2, g->clip_x, xmax);
        cy2 = nj_clamp64(cy2, g->clip_y, ymax);
        dx = cx2 - cx1;
        dy = cy2 - cy1;
        adx = nj_abs64(dx);
        ady = nj_abs64(dy);
    }

    sx = dx < 0 ? -1 : 1;
    sy = dy < 0 ? -1 : 1;
    err = adx - ady;

    for (n = 0;; n++) {
        if (nj_in_clip(g, (int)cx1, (int)cy1)) {
            if (!dotted || ((p0 + n) & 1) == 0) {
                nj_plot(g, (int)cx1, (int)cy1, g->color);
            }
        }
        if (cx1 == cx2 && cy1 == cy2) {
            break;
        }
        {
            nj_s8 e2 = 2 * err;

            if (e2 >= -ady) {
                err -= ady;
                cx1 += sx;
            }
            if (e2 <= adx) {
                err += adx;
                cy1 += sy;
            }
        }
    }
}

/* User-space wrapper. */
static void nj_line_impl(nj_gfx *g, nj_s8 ux1, nj_s8 uy1, nj_s8 ux2, nj_s8 uy2,
                         int *phase)
{
    nj_line_dev(g,
                ux1 + g->translate_x, uy1 + g->translate_y,
                ux2 + g->translate_x, uy2 + g->translate_y,
                phase);
}

/*
 * Plot an axis-aligned run in DEVICE space, honouring the dotted phase.
 * Only the visible part of the run is iterated, so a 10^9 pixel edge costs
 * nothing.
 */
static void nj_run(nj_gfx *g, nj_s8 x0, nj_s8 y0, nj_s8 x1, nj_s8 y1, int *phase)
{
    nj_s8 dx = x1 - x0, dy = y1 - y0;
    nj_s8 steps, i, i0 = 0, i1;
    int p = phase ? (*phase & 1) : 0;
    int dotted = (g->stroke == NJ_STROKE_DOTTED);

    if (dx != 0 && dy != 0) {
        return;                          /* runs are axis aligned */
    }
    steps = (dx != 0) ? nj_abs64(dx) : nj_abs64(dy);
    i1 = steps;
    if (phase) {
        *phase = (int)((p + (int)((steps + 1) & 1)) & 1);
    }
    if (g->clip_w <= 0 || g->clip_h <= 0) {
        return;
    }
    if (dx != 0) {
        /* Horizontal run: the fixed row must be inside the clip. */
        if (y0 < g->clip_y || y0 > (nj_s8)g->clip_y + g->clip_h - 1) {
            return;
        }
        if (dx > 0) {
            i0 = nj_max64(i0, (nj_s8)g->clip_x - x0);
            i1 = nj_min64(i1, (nj_s8)g->clip_x + g->clip_w - 1 - x0);
        } else {
            i0 = nj_max64(i0, x0 - ((nj_s8)g->clip_x + g->clip_w - 1));
            i1 = nj_min64(i1, x0 - (nj_s8)g->clip_x);
        }
    } else if (dy != 0) {
        /* Vertical run: the fixed column must be inside the clip. */
        if (x0 < g->clip_x || x0 > (nj_s8)g->clip_x + g->clip_w - 1) {
            return;
        }
        if (dy > 0) {
            i0 = nj_max64(i0, (nj_s8)g->clip_y - y0);
            i1 = nj_min64(i1, (nj_s8)g->clip_y + g->clip_h - 1 - y0);
        } else {
            i0 = nj_max64(i0, y0 - ((nj_s8)g->clip_y + g->clip_h - 1));
            i1 = nj_min64(i1, y0 - (nj_s8)g->clip_y);
        }
    } else {
        /* single pixel */
        if (!nj_in_clip(g, (int)x0, (int)y0)) {
            return;
        }
    }
    if (i0 < 0) {
        i0 = 0;
    }
    if (i0 > i1) {
        return;
    }
    for (i = i0; i <= i1; i++) {
        int px = (int)(x0 + (dx > 0 ? i : (dx < 0 ? -i : 0)));
        int py = (int)(y0 + (dy > 0 ? i : (dy < 0 ? -i : 0)));

        if (dotted && (((p + (int)i) & 1) != 0)) {
            continue;
        }
        nj_plot(g, px, py, g->color);
    }
}

/* ------------------------------------------------------------------ */
/* Primitives                                                          */
/* ------------------------------------------------------------------ */
void nj_gfx_clear(nj_gfx *g, nj_u4 argb)
{
    /* Device space, ignoring clip, translate, blend and XOR: this is the
     * "repaint the whole frame buffer" entry point. */
    if (!g) {
        return;
    }
    nj_surface_fill(g->surf, argb);
}

void nj_gfx_pixel(nj_gfx *g, int x, int y)
{
    int dx, dy;

    if (!g || !nj_surface_ok(g->surf)) {
        return;
    }
    dx = nj_gfx_dev_x(g, x);
    dy = nj_gfx_dev_y(g, y);
    if (!nj_in_clip(g, dx, dy)) {
        return;
    }
    nj_plot(g, dx, dy, g->color);
}

void nj_gfx_line(nj_gfx *g, int x1, int y1, int x2, int y2)
{
    int phase = 0;

    if (!g || !nj_surface_ok(g->surf)) {
        return;
    }
    nj_line_impl(g, x1, y1, x2, y2, &phase);
}

void nj_gfx_rect(nj_gfx *g, int x, int y, int w, int h)
{
    nj_s8 x0, y0, x1, y1;
    int phase = 0;

    if (!g || !nj_surface_ok(g->surf)) {
        return;
    }
    if (w < 0 || h < 0) {
        return;                          /* MIDP: nothing */
    }
    x0 = (nj_s8)x + g->translate_x;
    y0 = (nj_s8)y + g->translate_y;
    x1 = x0 + w;                         /* draw covers (w+1) x (h+1) */
    y1 = y0 + h;

    /* Perimeter order, corners plotted exactly once. */
    nj_run(g, x0, y0, x1, y0, &phase);
    if (y1 > y0) {
        nj_run(g, x1, y0 + 1, x1, y1, &phase);
    }
    if (y1 > y0 && x1 > x0) {
        nj_run(g, x1 - 1, y1, x0, y1, &phase);
    }
    if (x1 > x0 && y1 > y0) {
        nj_run(g, x0, y1 - 1, x0, y0 + 1, &phase);
    }
}

void nj_gfx_fill_rect(nj_gfx *g, int x, int y, int w, int h)
{
    nj_s8 bx0, by0, bx1, by1, yy;

    if (!g) {
        return;
    }
    if (!nj_box_from_user(g, x, y, w, h, &bx0, &by0, &bx1, &by1)) {
        return;
    }
    for (yy = by0; yy <= by1; yy++) {
        nj_span(g, (int)bx0, (int)bx1, (int)yy, g->color);
    }
}

/* ------------------------------------------------------------------ */
/* Ellipse geometry (doubled coordinates)                              */
/* ------------------------------------------------------------------ */
/*
 * Every arc is described by a doubled centre and doubled semi-axes:
 *
 *     2X = c2x + a2 * cos(deg)     2Y = c2y - b2 * sin(deg)
 *
 * Doubling keeps half-pixel centres exact.  For a FILL the ellipse covers the
 * pixel box [x, x+w-1] x [y, y+h-1], i.e. c2x = 2x + w, a2 = w.  For a DRAW
 * it covers the (w+1) x (h+1) box, i.e. c2x = 2x + w + 1, a2 = w + 1.
 */
typedef struct nj_ellipse {
    nj_s8 c2x, c2y;      /* doubled centre */
    nj_s8 a2, b2;        /* doubled semi axes */
    nj_s8 x0, y0, x1, y1; /* pixel box the shape may touch (inclusive) */
} nj_ellipse;

/* a / b rounded to nearest, b > 0, symmetric about zero.  Truncating instead
 * would shift the apex of a large ellipse by half a pixel and can make the
 * outline miss its own extreme row. */
static nj_s8 nj_round_div(nj_s8 a, nj_s8 b)
{
    if (a >= 0) {
        return (a + b / 2) / b;
    }
    return -((-a + b / 2) / b);
}

/* Pixel on the ellipse at `deg`, clamped into the shape's pixel box. */
static void nj_ellipse_vertex(const nj_ellipse *e, int deg, int *px, int *py)
{
    nj_s8 x = nj_clamp64(nj_half(e->c2x
                                 + nj_round_div(e->a2 * (nj_s8)nj_cos_q14(deg),
                                                NJ_GFX_FP_ONE)),
                         e->x0, e->x1);
    nj_s8 y = nj_clamp64(nj_half(e->c2y
                                 - nj_round_div(e->b2 * (nj_s8)nj_sin_q14(deg),
                                                NJ_GFX_FP_ONE)),
                         e->y0, e->y1);

    *px = (int)x;
    *py = (int)y;
}

/* Doubled horizontal half-width of the ellipse on a scanline whose doubled
 * centre offset is dy2.  Returns -1 when the row misses the ellipse. */
static nj_s8 nj_ellipse_dx2(nj_s8 a2, nj_s8 b2, nj_s8 dy2)
{
    nj_s8 ady = nj_abs64(dy2);

    if (b2 <= 0 || ady > b2) {
        return -1;
    }
    return nj_round_div(a2 * (nj_s8)nj_isqrt((nj_u8)(b2 * b2 - ady * ady)), b2);
}

/*
 * Stroke an arc.  The outline is a polyline whose vertices sit on the
 * ellipse, which keeps arbitrary start/extent angles trivial and reuses the
 * clipped, dotted-aware line rasterizer.  Segment count is chosen so that
 * consecutive vertices stay roughly one pixel apart.
 */
static void nj_arc_impl(nj_gfx *g, const nj_ellipse *e, int start, int extent,
                        int *phase)
{
    nj_s8 dim, ae, ns;
    int n, i, px, py;

    if (extent == 0) {
        return;
    }
    if (extent > 360) {
        extent = 360;
    }
    if (extent < -360) {
        extent = -360;
    }
    dim = nj_max64(e->a2, e->b2) / 2;            /* boundary semi-axis */
    ae = nj_abs64(extent);
    /* Two vertices per pixel of arc length (~0.55 px chords), so the sampled
     * outline hugs the true curve instead of flattening at the apexes. */
    ns = (dim * ae) / 32 + 2;
    if (ns < 2) {
        ns = 2;
    }
    if (ns > 1024) {
        ns = 1024;
    }
    n = (int)ns;

    nj_ellipse_vertex(e, start, &px, &py);
    for (i = 1; i <= n; i++) {
        int deg = (i == n) ? (start + extent)
                           : (int)((nj_s8)start + ((nj_s8)extent * i) / n);
        int qx, qy;

        nj_ellipse_vertex(e, deg, &qx, &qy);
        if (qx != px || qy != py) {
            nj_line_dev(g, px, py, qx, qy, phase);
        }
        px = qx;
        py = qy;
    }
}

void nj_gfx_arc(nj_gfx *g, int x, int y, int w, int h, int start, int extent)
{
    nj_ellipse e;
    int phase = 0;

    if (!g || !nj_surface_ok(g->surf)) {
        return;
    }
    if (w < 0 || h < 0 || extent == 0) {
        return;
    }
    e.c2x = 2 * ((nj_s8)x + g->translate_x) + w + 1;
    e.c2y = 2 * ((nj_s8)y + g->translate_y) + h + 1;
    e.a2 = (nj_s8)w + 1;
    e.b2 = (nj_s8)h + 1;
    e.x0 = (nj_s8)x + g->translate_x;
    e.y0 = (nj_s8)y + g->translate_y;
    e.x1 = e.x0 + w;
    e.y1 = e.y0 + h;
    nj_arc_impl(g, &e, start, extent, &phase);
}

/* Angular wedge of a pie slice, in doubled coordinates. */
typedef struct nj_wedge {
    int   active;      /* 0 = whole ellipse */
    int   inverted;    /* sweep > 180 degrees: inside is the union test */
    nj_s8 vsx, vsy;    /* radius vector at start, scaled by 2^VEC_SHIFT */
    nj_s8 vex, vey;    /* radius vector at start+extent */
} nj_wedge;

static void nj_wedge_init(nj_wedge *wd, const nj_ellipse *e, int start,
                          int extent)
{
    if (extent == 0 || extent >= 360 || extent <= -360) {
        wd->active = 0;
        wd->inverted = 0;
        wd->vsx = 0; wd->vsy = 0; wd->vex = 0; wd->vey = 0;
        return;
    }
    wd->active = 1;
    wd->inverted = (extent > 180 || extent < -180) ? 1 : 0;
    wd->vsx = (e->a2 * (nj_s8)nj_cos_q14(start)) / (NJ_GFX_FP_ONE >> NJ_GFX_VEC_SHIFT);
    wd->vsy = -(e->b2 * (nj_s8)nj_sin_q14(start)) / (NJ_GFX_FP_ONE >> NJ_GFX_VEC_SHIFT);
    wd->vex = (e->a2 * (nj_s8)nj_cos_q14(start + extent)) / (NJ_GFX_FP_ONE >> NJ_GFX_VEC_SHIFT);
    wd->vey = -(e->b2 * (nj_s8)nj_sin_q14(start + extent)) / (NJ_GFX_FP_ONE >> NJ_GFX_VEC_SHIFT);
}

/*
 * Is the pixel centre (doubled ux2, uy2 relative to the centre) in the wedge?
 *
 * Screen y grows downwards, so a NEGATIVE cross product means "b is
 * counter-clockwise from a" in MIDP's angle convention.  A sweep of at most
 * 180 degrees is therefore "at or CCW from the start radius, and not past the
 * end radius"; a larger sweep is the complement of the small sweep that
 * closes the circle.
 */
static int nj_wedge_inside(const nj_wedge *wd, nj_s8 ux2, nj_s8 uy2)
{
    nj_s8 s1, s2;

    if (!wd->active) {
        return 1;
    }
    s1 = wd->vsx * uy2 - wd->vsy * ux2;      /* cross(start radius, u)  */
    s2 = ux2 * wd->vey - uy2 * wd->vex;      /* cross(u, end radius)    */
    if (wd->inverted) {
        return (s1 <= 0 || s2 <= 0);
    }
    return (s1 <= 0 && s2 <= 0);
}

void nj_gfx_fill_arc(nj_gfx *g, int x, int y, int w, int h, int start,
                     int extent)
{
    nj_ellipse e;
    nj_wedge wd;
    nj_s8 ystart, yend, Y;

    if (!g || !nj_surface_ok(g->surf)) {
        return;
    }
    if (w <= 0 || h <= 0 || extent == 0) {
        return;
    }
    e.c2x = 2 * ((nj_s8)x + g->translate_x) + w;
    e.c2y = 2 * ((nj_s8)y + g->translate_y) + h;
    e.a2 = w;
    e.b2 = h;
    e.x0 = (nj_s8)x + g->translate_x;
    e.y0 = (nj_s8)y + g->translate_y;
    e.x1 = e.x0 + w - 1;
    e.y1 = e.y0 + h - 1;

    nj_wedge_init(&wd, &e, start, extent);

    ystart = nj_max64(e.y0, g->clip_y);
    yend = nj_min64(e.y1, (nj_s8)g->clip_y + g->clip_h - 1);
    for (Y = ystart; Y <= yend; Y++) {
        nj_s8 uy2 = 2 * Y + 1 - e.c2y;
        nj_s8 dx2 = nj_ellipse_dx2(e.a2, e.b2, uy2);
        nj_s8 lo, hi, X;

        if (dx2 < 0) {
            continue;
        }
        lo = nj_max64(nj_ceil_div(e.c2x - dx2 - 1, 2), g->clip_x);
        lo = nj_max64(lo, e.x0);
        hi = nj_min64(nj_floor_div(e.c2x + dx2 - 1, 2),
                      (nj_s8)g->clip_x + g->clip_w - 1);
        hi = nj_min64(hi, e.x1);
        if (!wd.active) {
            nj_span(g, (int)lo, (int)hi, (int)Y, g->color);
            continue;
        }
        for (X = lo; X <= hi; X++) {
            if (!nj_wedge_inside(&wd, 2 * X + 1 - e.c2x, uy2)) {
                continue;
            }
            nj_plot(g, (int)X, (int)Y, g->color);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Rounded rectangles                                                  */
/* ------------------------------------------------------------------ */
void nj_gfx_round_rect(nj_gfx *g, int x, int y, int w, int h, int rw, int rh)
{
    nj_s8 X, Y;
    nj_ellipse tl, tr, bl, br;
    int phase = 0;
    int p[8];

    if (!g || !nj_surface_ok(g->surf)) {
        return;
    }
    if (w < 0 || h < 0) {
        return;
    }
    /* arcWidth/arcHeight are the FULL corner arc size (diameters), clamped to
     * the rectangle; a non-positive size degrades to a plain rectangle. */
    if (rw > w) {
        rw = w;
    }
    if (rh > h) {
        rh = h;
    }
    if (rw <= 0 || rh <= 0) {
        nj_gfx_rect(g, x, y, w, h);
        return;
    }

    X = (nj_s8)x + g->translate_x;
    Y = (nj_s8)y + g->translate_y;

    /* Doubled centres of the four corner arcs; the draw box is (w+1)x(h+1). */
    tl.c2x = 2 * X + rw + 1;
    tl.c2y = 2 * Y + rh + 1;
    tl.a2 = (nj_s8)rw + 1;
    tl.b2 = (nj_s8)rh + 1;
    tl.x0 = X;      tl.y0 = Y;
    tl.x1 = X + rw; tl.y1 = Y + rh;

    tr.c2x = 2 * X + 2 * (nj_s8)w - rw + 1;
    tr.c2y = tl.c2y;
    tr.a2 = tl.a2;
    tr.b2 = tl.b2;
    tr.x0 = X + w - rw; tr.y0 = Y;
    tr.x1 = X + w;      tr.y1 = Y + rh;

    bl.c2x = tl.c2x;
    bl.c2y = 2 * Y + 2 * (nj_s8)h - rh + 1;
    bl.a2 = tl.a2;
    bl.b2 = tl.b2;
    bl.x0 = X;          bl.y0 = Y + h - rh;
    bl.x1 = X + rw;     bl.y1 = Y + h;

    br.c2x = tr.c2x;
    br.c2y = bl.c2y;
    br.a2 = tl.a2;
    br.b2 = tl.b2;
    br.x0 = X + w - rw; br.y0 = Y + h - rh;
    br.x1 = X + w;      br.y1 = Y + h;

    /* Junction points, in perimeter order (clockwise from the top edge). */
    nj_ellipse_vertex(&tl, 90, &p[0], &p[1]);    /* top    left  */
    nj_ellipse_vertex(&tr, 90, &p[2], &p[3]);    /* top    right */
    nj_ellipse_vertex(&tr, 0,  &p[4], &p[5]);    /* right  top   */
    nj_ellipse_vertex(&br, 0,  &p[6], &p[7]);    /* right  bottom */
    nj_line_dev(g, p[0], p[1], p[2], p[3], &phase);
    nj_arc_impl(g, &tr, 0, 90, &phase);
    nj_line_dev(g, p[4], p[5], p[6], p[7], &phase);
    nj_arc_impl(g, &br, 270, 90, &phase);
    nj_ellipse_vertex(&br, 270, &p[0], &p[1]);   /* bottom right */
    nj_ellipse_vertex(&bl, 270, &p[2], &p[3]);   /* bottom left  */
    nj_line_dev(g, p[0], p[1], p[2], p[3], &phase);
    nj_arc_impl(g, &bl, 180, 90, &phase);
    nj_ellipse_vertex(&bl, 180, &p[0], &p[1]);   /* left  bottom */
    nj_ellipse_vertex(&tl, 180, &p[2], &p[3]);   /* left  top    */
    nj_line_dev(g, p[0], p[1], p[2], p[3], &phase);
    nj_arc_impl(g, &tl, 90, 90, &phase);
}

void nj_gfx_fill_round_rect(nj_gfx *g, int x, int y, int w, int h, int rw,
                            int rh)
{
    nj_s8 X, Y, ystart, yend, YY;

    if (!g || !nj_surface_ok(g->surf)) {
        return;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    if (rw > w) {
        rw = w;
    }
    if (rh > h) {
        rh = h;
    }
    if (rw <= 0 || rh <= 0) {
        nj_gfx_fill_rect(g, x, y, w, h);
        return;
    }

    X = (nj_s8)x + g->translate_x;
    Y = (nj_s8)y + g->translate_y;

    ystart = nj_max64(Y, g->clip_y);
    yend = nj_min64(Y + h - 1, (nj_s8)g->clip_y + g->clip_h - 1);

    for (YY = ystart; YY <= yend; YY++) {
        nj_s8 dy_top = 2 * YY + 1 - (2 * Y + rh);          /* top corner centre */
        nj_s8 dy_bot = 2 * YY + 1 - (2 * (Y + h) - rh);    /* bottom corner centre */
        nj_s8 dxt = nj_ellipse_dx2(rw, rh, dy_top);
        nj_s8 dxb = nj_ellipse_dx2(rw, rh, dy_bot);
        nj_s8 left = X, right = X + w - 1;

        if (dxt >= 0) {
            nj_s8 l = nj_ceil_div(2 * X + rw - dxt - 1, 2);
            nj_s8 r = nj_floor_div(2 * (X + w) - rw + dxt - 1, 2);
            left = nj_max64(left, l);
            right = nj_min64(right, r);
        }
        if (dxb >= 0) {
            nj_s8 l = nj_ceil_div(2 * X + rw - dxb - 1, 2);
            nj_s8 r = nj_floor_div(2 * (X + w) - rw + dxb - 1, 2);
            left = nj_max64(left, l);
            right = nj_min64(right, r);
        }
        left = nj_max64(left, g->clip_x);
        left = nj_max64(left, X);
        right = nj_min64(right, (nj_s8)g->clip_x + g->clip_w - 1);
        right = nj_min64(right, X + w - 1);
        if (left <= right) {
            nj_span(g, (int)left, (int)right, (int)YY, g->color);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Triangles                                                           */
/* ------------------------------------------------------------------ */
static void nj_tri_fill(nj_gfx *g, nj_s8 x1, nj_s8 y1, nj_s8 x2, nj_s8 y2,
                        nj_s8 x3, nj_s8 y3)
{
    nj_s8 ax, ay, bx, by, cx, cy, t;
    nj_s8 ystart, yend, y;

    /* Sort the vertices by y (then x, for a deterministic result). */
    ax = x1; ay = y1; bx = x2; by = y2; cx = x3; cy = y3;
    if (ay > by || (ay == by && ax > bx)) {
        t = ax; ax = bx; bx = t;
        t = ay; ay = by; by = t;
    }
    if (by > cy || (by == cy && bx > cx)) {
        t = bx; bx = cx; cx = t;
        t = by; by = cy; cy = t;
    }
    if (ay > by || (ay == by && ax > bx)) {
        t = ax; ax = bx; bx = t;
        t = ay; ay = by; by = t;
    }
    if (ay == cy) {
        return;                          /* degenerate: no area */
    }

    ystart = nj_max64(ay, g->clip_y);
    yend = nj_min64(cy, (nj_s8)g->clip_y + g->clip_h - 1);

    for (y = ystart; y <= yend; y++) {
        nj_s8 xa, xb, lo, hi;

        if (y < by) {
            xa = ax + ((bx - ax) * (y - ay)) / (by - ay);
            xb = ax + ((cx - ax) * (y - ay)) / (cy - ay);
        } else if (y > by) {
            xa = bx + ((cx - bx) * (y - by)) / (cy - by);
            xb = ax + ((cx - ax) * (y - ay)) / (cy - ay);
        } else {
            xa = bx;
            xb = ax + ((cx - ax) * (y - ay)) / (cy - ay);
        }
        if (xa > xb) {
            t = xa; xa = xb; xb = t;
        }
        lo = nj_max64(xa, g->clip_x);
        hi = nj_min64(xb, (nj_s8)g->clip_x + g->clip_w - 1);
        if (lo <= hi) {
            nj_span(g, (int)lo, (int)hi, (int)y, g->color);
        }
    }
}

void nj_gfx_triangle(nj_gfx *g, int x1, int y1, int x2, int y2, int x3, int y3,
                     int filled)
{
    if (!g || !nj_surface_ok(g->surf)) {
        return;
    }
    if (!filled) {
        nj_gfx_line(g, x1, y1, x2, y2);
        nj_gfx_line(g, x2, y2, x3, y3);
        nj_gfx_line(g, x3, y3, x1, y1);
        return;
    }
    if (g->clip_w <= 0 || g->clip_h <= 0) {
        return;
    }
    nj_tri_fill(g,
                (nj_s8)x1 + g->translate_x, (nj_s8)y1 + g->translate_y,
                (nj_s8)x2 + g->translate_x, (nj_s8)y2 + g->translate_y,
                (nj_s8)x3 + g->translate_x, (nj_s8)y3 + g->translate_y);
}

/* ------------------------------------------------------------------ */
/* Images                                                              */
/* ------------------------------------------------------------------ */
/* Alignment-safe ARGB8888 word read (source buffers may be arbitrarily
 * aligned byte streams, e.g. nj_gfx_draw_rgb over an int[] payload). */
static nj_u4 nj_word_get(const nj_u1 *base, nj_s8 index)
{
    nj_u4 v;

    memcpy(&v, base + index * 4, sizeof v);
    return v;
}

/* Bytes per pixel of a source image, or 0 when the format is unknown. */
static int nj_image_bpp(int format)
{
    switch (format) {
    case NJ_PIXFMT_RGB565:   return 2;
    case NJ_PIXFMT_RGB888:   return 3;
    case NJ_PIXFMT_ARGB8888: return 4;
    default:                 return 0;
    }
}

/*
 * Source pixel at byte offset `off` -> 0xAARRGGBB.
 *
 * An nj_image is a packed byte buffer: RGB565 is two bytes per pixel (native
 * 16-bit order), RGB888 three, ARGB8888 four (native 32-bit order).  That is
 * what nj_codec_image_new() allocates (w * h * nj_codec_pixel_bytes(fmt)
 * bytes), so a decoder can hand the rasterizer an RGB565 frame to halve the
 * memory a full-screen background costs.
 */
static nj_u4 nj_src_pixel(const nj_u1 *base, nj_s8 off, int format)
{
    if (format == NJ_PIXFMT_RGB565) {
        nj_u2 v;
        nj_u4 r, g, b;

        memcpy(&v, base + off, sizeof v);
        r = ((nj_u4)v >> 11) & 0x1Fu;
        g = ((nj_u4)v >> 5) & 0x3Fu;
        b = (nj_u4)v & 0x1Fu;
        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    if (format == NJ_PIXFMT_RGB888) {
        return 0xFF000000u
             | ((nj_u4)base[off] << 16)
             | ((nj_u4)base[off + 1] << 8)
             | (nj_u4)base[off + 2];
    }
    return nj_word_get(base, off / 4);
}

/* Destination pixel at `p` from 0xAARRGGBB, in the surface's own format. */
static void nj_dst_pixel(nj_u1 *p, int bpp, int format, nj_u4 argb)
{
    if (bpp == 2) {
        nj_u2 v = (nj_u2)nj_surface_format_color(format, argb);

        memcpy(p, &v, sizeof v);
    } else if (bpp == 4) {
        nj_u4 v = argb;

        memcpy(p, &v, sizeof v);
    } else {
        p[0] = (nj_u1)((argb >> 16) & 0xFFu);
        p[1] = (nj_u1)((argb >> 8) & 0xFFu);
        p[2] = (nj_u1)(argb & 0xFFu);
    }
}

/* Destination size of a transformed region: 90/270 swap width and height. */
static void nj_trans_size(int transform, int sw, int sh, int *tw, int *th)
{
    switch (transform) {
    case NJ_TRANS_ROT90:
    case NJ_TRANS_ROT270:
    case NJ_TRANS_MIRROR_ROT90:
    case NJ_TRANS_MIRROR_ROT270:
        *tw = sh;
        *th = sw;
        break;
    default:
        *tw = sw;
        *th = sh;
        break;
    }
}

/*
 * Inverse transform: destination-local (i, j) -> source-local (u, v).
 *
 * Forward definitions, which are the MIDP ones ("mirror about the vertical
 * centre, then rotate clockwise by the transform angle"):
 *   ROT90        (a,b) -> (sh-1-b, a)
 *   ROT270       (a,b) -> (b, sw-1-a)
 *   ROT180       (a,b) -> (sw-1-a, sh-1-b)
 *   MIRROR       (a,b) -> (sw-1-a, b)
 *   MIRROR_ROT90 (a,b) -> (sh-1-b, sw-1-a)
 *   MIRROR_ROT180(a,b) -> (a, sh-1-b)
 *   MIRROR_ROT270(a,b) -> (b, a)
 */
static void nj_trans_inverse(int transform, int i, int j, int sw, int sh,
                             int *u, int *v)
{
    switch (transform) {
    case NJ_TRANS_MIRROR_ROT180:
        *u = i;             *v = sh - 1 - j; break;
    case NJ_TRANS_MIRROR:
        *u = sw - 1 - i;    *v = j;          break;
    case NJ_TRANS_ROT180:
        *u = sw - 1 - i;    *v = sh - 1 - j; break;
    case NJ_TRANS_MIRROR_ROT270:
        *u = j;             *v = i;          break;
    case NJ_TRANS_ROT90:
        *u = j;             *v = sh - 1 - i; break;
    case NJ_TRANS_ROT270:
        *u = sw - 1 - j;    *v = i;          break;
    case NJ_TRANS_MIRROR_ROT90:
        *u = sw - 1 - j;    *v = sh - 1 - i; break;
    case NJ_TRANS_NONE:
    default:
        *u = i;             *v = j;          break;
    }
}

/*
 * Shared blit for drawImage / drawImageROP / drawRGB / drawRegion.
 *
 * `src` points at the first pixel of the packed source buffer, whose row
 * pitch is src_stride_bytes; the region [sx, sy, sw, sh] is given in PIXELS
 * and lives inside it.  The
 * transform is applied by iterating the DESTINATION rectangle and inverse
 * mapping, so clipping is just an intersection with the clip rectangle and no
 * rotated copy is ever allocated.
 *
 * rop NJ_ROP_SET replaces the destination (even where the source is fully
 * transparent).  rop NJ_ROP_PAINT leaves fully transparent source pixels
 * alone and alpha blends the rest with `alpha` (0..255) as an extra coverage
 * multiplier.
 *
 * `mask`, when non-NULL, is an ARGB8888 image with the dimensions of the
 * transformed source, indexed by destination-local coordinates; a destination
 * pixel is written only where the mask pixel is not fully transparent.
 */
/*
 * One translucent ARGB8888 source pixel onto an RGB565 destination, with the
 * exact arithmetic of nj_gfx_blend_pixel() at full source weight: read the
 * destination, expand its five/six/five bits the way nj_surface_get() does,
 * interpolate, and pack again the way nj_surface_format_color() does.  The
 * caller supplies a row pointer it already clipped, so this does no bounds
 * checks and no format switches.
 *
 * Doom 3 paints a screenful of a translucent ARGB image every frame - its
 * 900-frame profile had 14.9 M nj_gfx_blend_pixel() calls, all from that one
 * blit - and each of them went through three nested calls and a handle on the
 * surface to rediscover the same format.
 */
static void nj_blend565(nj_u1 *dp, nj_u4 argb)
{
    nj_u2 v;
    nj_u4 a, sr, sg, sb, dr, dg, db, nr, ng, nb;

    memcpy(&v, dp, sizeof v);
    {
        nj_u4 r5 = (v >> 11) & 0x1Fu;
        nj_u4 g6 = (v >> 5) & 0x3Fu;
        nj_u4 b5 = v & 0x1Fu;

        dr = (r5 << 3) | (r5 >> 2);
        dg = (g6 << 2) | (g6 >> 4);
        db = (b5 << 3) | (b5 >> 2);
    }
    a  = (argb >> 24) & 0xFFu;
    sr = (argb >> 16) & 0xFFu;
    sg = (argb >> 8) & 0xFFu;
    sb = argb & 0xFFu;
    nr = (sr * a + dr * (255u - a) + 127u) / 255u;
    ng = (sg * a + dg * (255u - a) + 127u) / 255u;
    nb = (sb * a + db * (255u - a) + 127u) / 255u;
    v = (nj_u2)(((nr >> 3) << 11) | ((ng >> 2) << 5) | (nb >> 3));
    memcpy(dp, &v, sizeof v);
}

static void nj_region_draw(nj_gfx *g, const nj_u1 *src, nj_s8 src_stride_bytes,
                           int src_format, const nj_u1 *mask,
                           int mask_stride, int sx, int sy, int sw, int sh,
                           int transform, int rop, int xor_on, nj_u4 xor_argb,
                           int dx, int dy, int alpha)
{
    int tw, th, bpp, src_bpp;
    nj_s8 X0, Y0, cx0, cy0, cx1, cy1, X, Y;

    if (!g || !nj_surface_ok(g->surf) || !src) {
        return;
    }
    src_bpp = nj_image_bpp(src_format);
    if (sw <= 0 || sh <= 0 || src_bpp == 0
        || src_stride_bytes < (nj_s8)sw * src_bpp) {
        return;
    }
    if (g->clip_w <= 0 || g->clip_h <= 0) {
        return;
    }
    if (alpha < 0) {
        alpha = 0;
    }
    if (alpha > 255) {
        alpha = 255;
    }
    bpp = nj_surface_bpp(g->surf);
    nj_trans_size(transform, sw, sh, &tw, &th);

    X0 = (nj_s8)dx + g->translate_x;
    Y0 = (nj_s8)dy + g->translate_y;

    cx0 = nj_max64(X0, g->clip_x);
    cy0 = nj_max64(Y0, g->clip_y);
    cx1 = nj_min64(X0 + tw - 1, (nj_s8)g->clip_x + g->clip_w - 1);
    cy1 = nj_min64(Y0 + th - 1, (nj_s8)g->clip_y + g->clip_h - 1);
    if (cx0 > cx1 || cy0 > cy1) {
        return;
    }

    /*
     * The common case by a wide margin is a sprite blit: no transform, no
     * mask, full opacity, and a source whose format is the destination's, so
     * every pixel of the row lands unchanged.  The general loop below asks
     * nj_trans_inverse(), nj_src_pixel() and nj_raw_set() once per pixel (and
     * nj_gfx_blend_pixel() whenever alpha is below 255); a title that blits a
     * full screen every frame lives in there - nj_region_draw was 44% of a
     * profiled run of 合金弹头4(完美汉化版)_4053, with 59M pixels through
     * nj_gfx_blend_pixel - so copy whole rows instead.
     *
     * An RGB565 or RGB888 source carries no alpha at all, which is why PAINT
     * and SET are the same write here; an ARGB8888 source can only be copied
     * when the caller asked for SET, because PAINT has to blend the pixels
     * whose alpha is below 255.
     */
    if (transform == NJ_TRANS_NONE && mask == NULL && !xor_on
        && src_format == g->surf->format && src_bpp == bpp
        && alpha == 255
        && (src_format != NJ_PIXFMT_ARGB8888 || rop == NJ_ROP_SET)) {
        int row_bytes = (int)(cx1 - cx0 + 1) * bpp;
        int first = (int)(cx0 - X0);
        nj_u1 *dst = (nj_u1 *)g->surf->pixels;

        for (Y = cy0; Y <= cy1; Y++) {
            int j = (int)(Y - Y0);
            const nj_u1 *srow = src
                + (nj_s8)(sy + j) * src_stride_bytes
                + (nj_s8)(sx + first) * src_bpp;
            nj_u1 *drow = dst + (nj_s8)Y * g->surf->stride
                        + (nj_s8)cx0 * bpp;

            memcpy(drow, srow, (size_t)row_bytes);
        }
        return;
    }

    /*
     * That copy, through a transform.  合金弹头2013BT版_678 blits a 120x320
     * rotated column and a 21x19 mirrored sprite every frame, and every one of
     * those 54,000 pixels a frame went through nj_trans_inverse() plus two
     * format switches (nj_src_pixel and nj_dst_pixel) to be copied unchanged.
     * Ask the inverse transform per pixel - it is a switch on a constant the
     * caller fixed - and copy the pixel.
     */
    if (mask == NULL && !xor_on && alpha == 255
        && src_format == g->surf->format && src_bpp == bpp
        && (src_format != NJ_PIXFMT_ARGB8888 || rop == NJ_ROP_SET)) {
        nj_u1 *dst = (nj_u1 *)g->surf->pixels;

        for (Y = cy0; Y <= cy1; Y++) {
            int j = (int)(Y - Y0);

            for (X = cx0; X <= cx1; X++) {
                int i = (int)(X - X0);
                int u, v;

                nj_trans_inverse(transform, i, j, sw, sh, &u, &v);
                memcpy(dst + (nj_s8)Y * g->surf->stride + (nj_s8)X * bpp,
                       src + (nj_s8)(sy + v) * src_stride_bytes
                           + (nj_s8)(sx + u) * src_bpp,
                       (size_t)src_bpp);
            }
        }
        return;
    }

    /*
     * Same run, but the source is in another pixel format: 合金弹头4(完美汉化版)
     * blits a full screen of decoded ARGB8888 onto an RGB565 surface every
     * frame, which is 76800 calls to nj_src_pixel() and nj_raw_set() per frame
     * through the general loop below.  The conversions still happen per pixel,
     * but nothing else does: no inverse transform, no clip arithmetic, no
     * surface checks, no format switch per pixel for the destination.
     */
    if (transform == NJ_TRANS_NONE && mask == NULL && !xor_on
        && src_format != NJ_PIXFMT_ARGB8888 && alpha == 255) {
        int first = (int)(cx0 - X0);
        int n = (int)(cx1 - cx0 + 1);
        int i;
        nj_u1 *dst = (nj_u1 *)g->surf->pixels;

        for (Y = cy0; Y <= cy1; Y++) {
            int j = (int)(Y - Y0);
            const nj_u1 *sp = src
                + (nj_s8)(sy + j) * src_stride_bytes
                + (nj_s8)(sx + first) * src_bpp;
            nj_u1 *dp = dst + (nj_s8)Y * g->surf->stride + (nj_s8)cx0 * bpp;

            for (i = 0; i < n; i++) {
                nj_dst_pixel(dp, bpp, g->surf->format,
                             nj_src_pixel(sp, 0, src_format));
                sp += src_bpp;
                dp += bpp;
            }
        }
        return;
    }

    /*
     * ARGB8888 sprites onto a 16-bit screen: 浴火银河2完整版_7689 paints a
     * screenful of them every frame, and until this branch existed every one
     * of those pixels went through the general loop below - two function calls
     * (nj_src_pixel and nj_gfx_blend_pixel) per pixel to rediscover that an
     * alpha of 255 means "replace".  Same rule as the ARGB-to-ARGB branch: an
     * opaque pixel goes through the destination format, a translucent one is
     * blended, a transparent one is left alone, and SET copies whatever the
     * source holds.
     */
    if (transform == NJ_TRANS_NONE && mask == NULL && !xor_on
        && alpha == 255 && src_format == NJ_PIXFMT_ARGB8888
        && g->surf->format != NJ_PIXFMT_ARGB8888 && bpp != 4) {
        int first = (int)(cx0 - X0);
        int n = (int)(cx1 - cx0 + 1);
        int i;
        nj_u1 *dst = (nj_u1 *)g->surf->pixels;
        /*
         * RGB565 is what both desktop ports present, and it is the format the
         * branch above was written for: walk the rows with a two-byte
         * destination pointer and inline the blend instead of calling
         * nj_gfx_blend_pixel() per translucent pixel.  An opaque pixel and a
         * PAINT via NJ_ROP_SET still take the same packed write as the
         * general loop, so the pictures do not move.
         */
        if (g->surf->format == NJ_PIXFMT_RGB565) {
            for (Y = cy0; Y <= cy1; Y++) {
                int j = (int)(Y - Y0);
                const nj_u1 *sp = src
                    + (nj_s8)(sy + j) * src_stride_bytes
                    + (nj_s8)(sx + first) * src_bpp;
                nj_u1 *dp = dst + (nj_s8)Y * g->surf->stride
                          + (nj_s8)cx0 * bpp;

                for (i = 0; i < n; i++) {
                    nj_u4 c = nj_word_get(sp, 0);
                    int sa = (int)((c >> 24) & 0xFFu);

                    if (rop == NJ_ROP_SET || sa >= 255) {
                        nj_u2 v = (nj_u2)((((c >> 19) & 0x1Fu) << 11) |
                                          (((c >> 10) & 0x3Fu) << 5) |
                                          ((c >> 3) & 0x1Fu));

                        memcpy(dp, &v, sizeof v);
                    } else if (sa > 0) {
                        nj_blend565(dp, c);
                    }
                    sp += src_bpp;
                    dp += bpp;
                }
            }
            return;
        }

        for (Y = cy0; Y <= cy1; Y++) {
            int j = (int)(Y - Y0);
            const nj_u1 *sp = src
                + (nj_s8)(sy + j) * src_stride_bytes
                + (nj_s8)(sx + first) * src_bpp;
            nj_u1 *dp = dst + (nj_s8)Y * g->surf->stride
                      + (nj_s8)cx0 * bpp;

            for (i = 0; i < n; i++) {
                nj_u4 c = nj_word_get(sp, 0);
                int sa = (int)((c >> 24) & 0xFFu);

                if (rop == NJ_ROP_SET || sa >= 255) {
                    nj_dst_pixel(dp, bpp, g->surf->format, c);
                } else if (sa > 0) {
                    nj_gfx_blend_pixel(g->surf, (int)cx0 + i, (int)Y, c, 255);
                }
                sp += src_bpp;
                dp += bpp;
            }
        }
        return;
    }

    /*
     * An ARGB8888 source under rop PAINT cannot be copied blindly, but most
     * titles paint opaque images: 合金弹头4(完美汉化版) drawRegions a full
     * screen of decoded ARGB8888 onto an ARGB8888 surface every frame, and
     * every one of those pixels has alpha 255 - so the general loop below
     * spends its time rediscovering that.  Read the pixel, take the copy when
     * it is opaque, and blend only where the image really is translucent.
     */
    if (transform == NJ_TRANS_NONE && mask == NULL && !xor_on
        && alpha == 255 && src_format == NJ_PIXFMT_ARGB8888
        && g->surf->format == NJ_PIXFMT_ARGB8888 && src_bpp == bpp
        && rop != NJ_ROP_SET) {
        int first = (int)(cx0 - X0);
        int n = (int)(cx1 - cx0 + 1);
        int i;
        nj_u1 *dst = (nj_u1 *)g->surf->pixels;

        for (Y = cy0; Y <= cy1; Y++) {
            int j = (int)(Y - Y0);
            const nj_u1 *sp = src
                + (nj_s8)(sy + j) * src_stride_bytes
                + (nj_s8)(sx + first) * src_bpp;
            nj_u1 *dp = dst + (nj_s8)Y * g->surf->stride + (nj_s8)cx0 * bpp;

            for (i = 0; i < n; i++) {
                nj_u4 c = nj_word_get(sp, 0);
                int sa = (int)((c >> 24) & 0xFFu);

                if (sa >= 255) {
                    memcpy(dp, &c, sizeof c);
                } else if (sa > 0) {
                    nj_gfx_blend_pixel(g->surf, (int)cx0 + i, (int)Y, c, 255);
                }
                sp += 4;
                dp += 4;
            }
        }
        return;
    }

    for (Y = cy0; Y <= cy1; Y++) {
        int j = (int)(Y - Y0);

        for (X = cx0; X <= cx1; X++) {
            int i = (int)(X - X0);
            int u, v, sa;
            nj_u4 sp;

            nj_trans_inverse(transform, i, j, sw, sh, &u, &v);
            sp = nj_src_pixel(src, ((nj_s8)(sy + v)) * src_stride_bytes
                                   + (nj_s8)(sx + u) * src_bpp,
                              src_format);

            if (mask) {
                nj_u4 mp = nj_word_get(mask, (nj_s8)j * mask_stride + i);
                if (((mp >> 24) & 0xFFu) == 0u) {
                    continue;
                }
            }

            sa = (src_format == NJ_PIXFMT_ARGB8888)
               ? (int)((sp >> 24) & 0xFFu) : 255;
            sa = (sa * alpha + 127) / 255;

            if (xor_on) {
                /* XOR blit: the destination is XORed with the source pixel,
                 * with xor_argb as an extra mask (0 for a plain dst ^= src,
                 * which is how the game package erases and restores a
                 * sprite without knowing the background). */
                nj_u4 dst, val;
                if (rop != NJ_ROP_SET && sa == 0) {
                    continue;
                }
                dst = nj_surface_get(g->surf, (int)X, (int)Y);
                val = nj_surface_format_color(g->surf->format, dst)
                    ^ nj_surface_format_color(g->surf->format, sp)
                    ^ nj_surface_format_color(g->surf->format, xor_argb);
                nj_raw_set(g->surf, (int)X, (int)Y, bpp, val);
                continue;
            }
            if (rop == NJ_ROP_SET) {
                nj_raw_set(g->surf, (int)X, (int)Y, bpp,
                           nj_surface_format_color(g->surf->format, sp));
                continue;
            }
            if (sa == 0) {
                continue;
            }
            if (sa >= 255) {
                nj_raw_set(g->surf, (int)X, (int)Y, bpp,
                           nj_surface_format_color(g->surf->format, sp));
                continue;
            }
            nj_gfx_blend_pixel(g->surf, (int)X, (int)Y, sp, alpha);
        }
    }
}

/* Clamp a requested source region to the image; 0 when nothing is left. */
static int nj_image_region(const nj_image *img, int *sx, int *sy, int *sw,
                           int *sh)
{
    if (!img || !img->pixels || *sw <= 0 || *sh <= 0) {
        return 0;
    }
    if (*sx < 0) {
        *sw += *sx;
        *sx = 0;
    }
    if (*sy < 0) {
        *sh += *sy;
        *sy = 0;
    }
    if (*sx >= img->width || *sy >= img->height) {
        return 0;
    }
    if (*sx + *sw > img->width) {
        *sw = img->width - *sx;
    }
    if (*sy + *sh > img->height) {
        *sh = img->height - *sy;
    }
    return (*sw > 0 && *sh > 0) ? 1 : 0;
}

/* Effective source format of an nj_image; unknown values mean ARGB8888. */
static int nj_image_format(const nj_image *img)
{
    if (img->format == NJ_PIXFMT_RGB565 || img->format == NJ_PIXFMT_RGB888) {
        return img->format;
    }
    return NJ_PIXFMT_ARGB8888;
}

void nj_gfx_draw_image(nj_gfx *g, const nj_image *img, int dx, int dy,
                       int sx, int sy, int sw, int sh)
{
    int rop, fmt;

    if (!g || !nj_image_region(img, &sx, &sy, &sw, &sh)) {
        return;
    }
    fmt = nj_image_format(img);
    /* No ROP parameter: the blend mode decides.  SRC_OVER (the default) is
     * the MIDP drawImage rule — transparent pixels leave the destination
     * alone, partially transparent pixels are composited. */
    rop = (g->blend == NJ_BLEND_SRC_OVER) ? NJ_ROP_PAINT : NJ_ROP_SET;
    nj_region_draw(g, (const nj_u1 *)img->pixels,
                   (nj_s8)img->width * nj_image_bpp(fmt), fmt,
                   NULL, 0, sx, sy, sw, sh, NJ_TRANS_NONE, rop,
                   g->use_xor, g->xor_color, dx, dy, 255);
}

void nj_gfx_draw_image_rop(nj_gfx *g, const nj_image *img, int dx, int dy,
                           int sx, int sy, int sw, int sh, int rop,
                           int transform)
{
    int fmt;

    if (!g || !nj_image_region(img, &sx, &sy, &sw, &sh)) {
        return;
    }
    fmt = nj_image_format(img);
    nj_region_draw(g, (const nj_u1 *)img->pixels,
                   (nj_s8)img->width * nj_image_bpp(fmt), fmt,
                   NULL, 0, sx, sy, sw, sh, transform, rop,
                   g->use_xor, g->xor_color, dx, dy, 255);
}

void nj_gfx_draw_rgb(nj_gfx *g, const nj_u1 *rgb, int scanlen, int alpha,
                     int dx, int dy, int sx, int sy, int sw, int sh,
                     int rop, int transform)
{
    /* `rgb` is a tightly packed 3-byte-per-pixel RGB stream (what the MIDP
     * drawRGB path repacks its int[] into); `scanlen` is the row pitch in
     * BYTES, (sx, sy) is the region origin in PIXELS, and `alpha` is the
     * global opacity (255 = opaque) because this layout carries no per-pixel
     * alpha. */
    if (!g || !rgb || sw <= 0 || sh <= 0 || scanlen <= 0) {
        return;
    }
    nj_region_draw(g, rgb, scanlen, NJ_PIXFMT_RGB888, NULL, 0,
                   sx, sy, sw, sh, transform, rop,
                   g->use_xor, g->xor_color, dx, dy, alpha);
}

void nj_gfx_draw_region(nj_gfx *g, const nj_image *src, int sx, int sy, int sw,
                        int sh, int transform, int is_xor, nj_u4 xor_argb,
                        const nj_image *clip_src, int dx, int dy, int rop)
{
    const nj_u1 *mask = NULL;
    int mask_stride = 0;
    int xor_on, fmt;

    if (!g || !nj_image_region(src, &sx, &sy, &sw, &sh)) {
        return;
    }
    if (clip_src && clip_src->pixels && clip_src->width > 0
        && clip_src->height > 0) {
        mask = (const nj_u1 *)clip_src->pixels;
        mask_stride = clip_src->width;
    }
    /* The caller's is_xor/xor_argb pair drives sprite drawing; the graphics
     * context's own XOR mode also applies. */
    xor_on = is_xor || g->use_xor;
    fmt = nj_image_format(src);
    nj_region_draw(g, (const nj_u1 *)src->pixels,
                   (nj_s8)src->width * nj_image_bpp(fmt), fmt,
                   mask, mask_stride, sx, sy, sw, sh, transform, rop,
                   xor_on, is_xor ? xor_argb : g->xor_color, dx, dy, 255);
}
