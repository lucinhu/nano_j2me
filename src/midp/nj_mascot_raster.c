/*
 * Mascot Capsule Micro3D v3: indexed BMP texture decode + triangle raster.
 *
 * Table-rotation port of the reference Java rasterizer; see
 * nj_mascot_raster.h for the API, the vertex units and the attribution.
 *
 * The rasterizer reproduces the reference Rasterizer.java arithmetic exactly:
 * the same vertex sort, the same DDA setup (including the symmetric ceil used
 * for the scanline derivatives and the "corrected rounding to avoid texture
 * seams"), the same per-pixel palette/sample index and the same blend
 * formulas.  Three deliberate differences:
 *
 *   - coordinates are accumulated in 64 bit where Java overflows int; real
 *     triangles (reference-clamped vertices) never reach the overflow, so the
 *     pictures match, but pathological inputs do not wrap like Java.
 *   - the reference's "replaceFast" path is folded into the normal replace
 *     path (identical output).
 *   - toon shading is evaluated per pixel instead of splitting the triangle
 *     at the threshold in the geometry stage; the picture is the same.
 */

#include "nj2me/nj.h"
#include "nj2me/nj_gfx.h"
#include "nj_mascot_raster.h"

#include <string.h>

/* The reference's fixed point. */
#define MR_FP      12
#define MR_FP_ONE  (1 << MR_FP)

/* The blobs and surface scans are little-endian by construction.  On a
 * little-endian host the word helpers compile to a single load or store; the
 * portable byte-wise form stays for big-endian ports. */
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) \
    && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define MR_LE_HOST 1
#endif

/* Bit manipulation on little-endian host buffers (no alignment assumptions). */
static inline nj_u2 mr_u16(const nj_u1 *p)
{
#ifdef MR_LE_HOST
    nj_u2 v;

    memcpy(&v, p, sizeof v);
    return v;
#else
    return (nj_u2)((nj_u2)p[0] | ((nj_u2)p[1] << 8));
#endif
}

static inline nj_u4 mr_u32(const nj_u1 *p)
{
#ifdef MR_LE_HOST
    nj_u4 v;

    memcpy(&v, p, sizeof v);
    return v;
#else
    return (nj_u4)p[0] | ((nj_u4)p[1] << 8) | ((nj_u4)p[2] << 16)
         | ((nj_u4)p[3] << 24);
#endif
}

static inline void mr_put_u16(nj_u1 *p, nj_u2 v)
{
#ifdef MR_LE_HOST
    memcpy(p, &v, sizeof v);
#else
    p[0] = (nj_u1)(v & 0xFFu);
    p[1] = (nj_u1)((v >> 8) & 0xFFu);
#endif
}

static inline void mr_put_u32(nj_u1 *p, nj_u4 v)
{
#ifdef MR_LE_HOST
    memcpy(p, &v, sizeof v);
#else
    p[0] = (nj_u1)(v & 0xFFu);
    p[1] = (nj_u1)((v >> 8) & 0xFFu);
    p[2] = (nj_u1)((v >> 16) & 0xFFu);
    p[3] = (nj_u1)((v >> 24) & 0xFFu);
#endif
}

/* ------------------------------------------------------------------ */
/* Surface pixels                                                      */
/* ------------------------------------------------------------------ */

static inline nj_s4 mr_bpp_of(const nj_surface *s)
{
    switch (s->format) {
    case NJ_PIXFMT_RGB565:   return 2;
    case NJ_PIXFMT_RGB888:   return 3;
    case NJ_PIXFMT_ARGB8888: return 4;
    default:                 return 0;
    }
}

static int mr_surface_ok(const nj_surface *s)
{
    nj_s4 bpp;

    if (!s || !s->pixels || s->width <= 0 || s->height <= 0) {
        return 0;
    }
    bpp = mr_bpp_of(s);
    if (bpp == 0 || s->stride < s->width * bpp) {
        return 0;
    }
    return 1;
}

/* Reference-Rasterizer read of one pixel as ARGB8888 (565 expanded exactly
 * like nj_surface_get). */
static inline nj_u4 mr_pixel_get(const nj_surface *s, nj_s4 x, nj_s4 y)
{
    const nj_u1 *p = (const nj_u1 *)s->pixels
                   + (nj_s8)y * s->stride + (nj_s8)x * mr_bpp_of(s);

    switch (s->format) {
    case NJ_PIXFMT_RGB565: {
        nj_u4 v = mr_u16(p);
        nj_u4 r = (v >> 11) & 0x1Fu;
        nj_u4 g = (v >> 5) & 0x3Fu;
        nj_u4 b = v & 0x1Fu;
        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    case NJ_PIXFMT_RGB888:
        return 0xFF000000u | ((nj_u4)p[0] << 16) | ((nj_u4)p[1] << 8)
             | (nj_u4)p[2];
    default:
        return mr_u32(p);
    }
}

static inline void mr_pixel_put(nj_surface *s, nj_s4 x, nj_s4 y, nj_u4 argb)
{
    nj_u1 *p = (nj_u1 *)s->pixels + (nj_s8)y * s->stride
             + (nj_s8)x * mr_bpp_of(s);

    switch (s->format) {
    case NJ_PIXFMT_RGB565: {
        nj_u4 r = (argb >> 16) & 0xFFu;
        nj_u4 g = (argb >> 8) & 0xFFu;
        nj_u4 b = argb & 0xFFu;
        mr_put_u16(p, (nj_u2)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));
        break;
    }
    case NJ_PIXFMT_RGB888:
        p[0] = (nj_u1)((argb >> 16) & 0xFFu);
        p[1] = (nj_u1)((argb >> 8) & 0xFFu);
        p[2] = (nj_u1)(argb & 0xFFu);
        break;
    default:
        mr_put_u32(p, argb);
        break;
    }
}

/* Variants of the two helpers above that take the already computed pixel
 * address, for the inner fill loop: that loop walks a row with a running
 * pointer, so it must not recompute y*stride + x*bpp for every pixel. */
static inline nj_u4 mr_raw_get(const nj_u1 *p, nj_s4 fmt)
{
    switch (fmt) {
    case NJ_PIXFMT_RGB565: {
        nj_u4 v = mr_u16(p);
        nj_u4 r = (v >> 11) & 0x1Fu;
        nj_u4 g = (v >> 5) & 0x3Fu;
        nj_u4 b = v & 0x1Fu;
        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    case NJ_PIXFMT_RGB888:
        return 0xFF000000u | ((nj_u4)p[0] << 16) | ((nj_u4)p[1] << 8)
             | (nj_u4)p[2];
    default:
        return mr_u32(p);
    }
}

static inline void mr_raw_put(nj_u1 *p, nj_u4 argb, nj_s4 fmt)
{
    switch (fmt) {
    case NJ_PIXFMT_RGB565: {
        nj_u4 r = (argb >> 16) & 0xFFu;
        nj_u4 g = (argb >> 8) & 0xFFu;
        nj_u4 b = argb & 0xFFu;
        mr_put_u16(p, (nj_u2)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));
        break;
    }
    case NJ_PIXFMT_RGB888:
        p[0] = (nj_u1)((argb >> 16) & 0xFFu);
        p[1] = (nj_u1)((argb >> 8) & 0xFFu);
        p[2] = (nj_u1)(argb & 0xFFu);
        break;
    default:
        mr_put_u32(p, argb);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Reference blendPixel                                                 */
/* ------------------------------------------------------------------ */
static inline nj_u4 mr_blend(nj_u4 src, nj_u4 dst, nj_s4 blend)
{
    switch (blend) {
    case NJ_MR_BLEND_ADD: {
        nj_u4 tmp = (((dst + src - ((dst ^ src) & 0x01010101u)) >> 8)
                     & 0x010101u) * 0xFFu;
        return 0xFF000000u | (dst + src - tmp) | tmp;
    }
    case NJ_MR_BLEND_SUB: {
        nj_u4 nd = ~dst;
        nj_u4 tmp = (((nd + src - ((nd ^ src) & 0x01010101u)) >> 8)
                     & 0x010101u) * 0xFFu;
        return 0xFF000000u | ((dst | tmp) - (src | tmp));
    }
    case NJ_MR_BLEND_HALF:
    default:
        return 0xFF000000u | ((dst & src) + (((dst ^ src) >> 1) & 0x7F7F7Fu));
    }
}

nj_u4 nj_mascot_blend_pixel(nj_u4 src, nj_u4 dst, nj_s4 blend)
{
    return mr_blend(src, dst, blend);
}

void nj_mascot_raster_pixel(nj_surface *dst, nj_s4 x, nj_s4 y,
                            nj_u4 argb, nj_s4 blend)
{
    if (!mr_surface_ok(dst) || x < 0 || y < 0 || x >= dst->width
        || y >= dst->height) {
        return;
    }
    if (blend == NJ_MR_BLEND_REPLACE) {
        mr_pixel_put(dst, x, y, argb);
    } else {
        mr_pixel_put(dst, x, y,
                     nj_mascot_blend_pixel(argb, mr_pixel_get(dst, x, y),
                                           blend));
    }
}

/* Reference Rasterizer.drawLine, untouched. */
void nj_mascot_raster_line(nj_surface *dst,
                           nj_s4 clip_x1, nj_s4 clip_y1,
                           nj_s4 clip_x2, nj_s4 clip_y2,
                           nj_s4 x0, nj_s4 y0, nj_s4 x1, nj_s4 y1,
                           nj_u4 argb, nj_s4 blend)
{
    nj_s4 dx, dy, sx, sy, err;

    if (!mr_surface_ok(dst)) {
        return;
    }
    if (clip_x1 < 0) clip_x1 = 0;
    if (clip_y1 < 0) clip_y1 = 0;
    if (clip_x2 > dst->width) clip_x2 = dst->width;
    if (clip_y2 > dst->height) clip_y2 = dst->height;

    dx = x1 - x0;
    if (dx < 0) dx = -dx;
    dy = y1 - y0;
    if (dy < 0) dy = -dy;
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    err = dx - dy;
    for (;;) {
        if (x0 >= clip_x1 && x0 < clip_x2 && y0 >= clip_y1 && y0 < clip_y2) {
            if (blend == NJ_MR_BLEND_REPLACE) {
                mr_pixel_put(dst, x0, y0, argb);
            } else {
                mr_pixel_put(dst, x0, y0,
                             nj_mascot_blend_pixel(argb,
                                                   mr_pixel_get(dst, x0, y0),
                                                   blend));
            }
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        {
            nj_s4 e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                x0 += sx;
            }
            if (e2 < dx) {
                err += dx;
                y0 += sy;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Indexed BMP decode                                                  */
/* ------------------------------------------------------------------ */
typedef struct mr_bmp {
    nj_s4 width, height;
    nj_s4 palette_off, palette_count;
    nj_s4 raster_off, stride;
    nj_s4 reversed;
} mr_bmp;

static nj_s4 mr_count_bits(nj_s4 v)
{
    /* Smallest i with (1 << i) >= v, exactly like the reference countBits. */
    nj_s4 i = 0;
    nj_u4 x = 1;

    while (x < (nj_u4)v) {
        x <<= 1;
        i++;
    }
    return i;
}

static nj_s4 mr_bmp_parse(const nj_u1 *d, nj_s4 len, nj_s4 is_env,
                          mr_bmp *b)
{
    nj_s4 raster_off, dib;

    if (!d || !b || len < 26) {
        return NJ_MR_ERR_SIZE;
    }
    if (d[0] != 'B' || d[1] != 'M') {
        return NJ_MR_ERR_FORMAT;
    }
    raster_off = (nj_s4)mr_u32(d + 10);
    dib = (nj_s4)mr_u32(d + 14);
    if (dib == 12) {
        /* BITMAPCOREHEADER: 16-bit dimensions, always bottom-up. */
        b->width = (nj_s4)mr_u16(d + 18);
        b->height = (nj_s4)mr_u16(d + 20);
        if (mr_u16(d + 24) != 8) {
            return NJ_MR_ERR_FORMAT;
        }
        b->palette_off = 14 + 12;
        b->palette_count = 256;
        b->reversed = 1;
    } else if (dib == 40) {
        nj_s4 h;
        nj_s4 nc;

        if (len < 14 + 40) {
            return NJ_MR_ERR_SIZE;
        }
        b->width = (nj_s4)mr_u32(d + 18);
        h = (nj_s4)mr_u32(d + 22);
        b->height = h < 0 ? -h : h;
        b->reversed = h >= 0; /* positive height = bottom-up raster */
        if (mr_u16(d + 28) != 8) {
            return NJ_MR_ERR_FORMAT;
        }
        if (mr_u32(d + 30) != 0) {
            return NJ_MR_ERR_FORMAT;
        }
        b->palette_off = 14 + 40;
        nc = (nj_s4)mr_u32(d + 46);
        if (nc == 0) {
            nc = 256;
        }
        if (nc < 0 || nc > 256) {
            return NJ_MR_ERR_FORMAT;
        }
        b->palette_count = nc;
    } else {
        return NJ_MR_ERR_FORMAT;
    }
    if (b->width <= 0 || b->height <= 0) {
        return NJ_MR_ERR_FORMAT;
    }
    /* Environment maps are 64x64 in Micro3D v3 (the reference always pads to
     * 64x64 and would throw for a larger source). */
    if (is_env && (b->width > 64 || b->height > 64)) {
        return NJ_MR_ERR_FORMAT;
    }
    if (b->palette_off + b->palette_count * 4 > len) {
        return NJ_MR_ERR_SIZE;
    }
    b->stride = (b->width + 3) & ~3;
    if (raster_off < b->palette_off + b->palette_count * 4) {
        /* Broken files the reference tolerates by clamping. */
        raster_off = b->palette_off + b->palette_count * 4;
    }
    if (raster_off < 0) {
        return NJ_MR_ERR_FORMAT;
    }
    if ((nj_s8)raster_off + (nj_s8)(b->height - 1) * b->stride
        + b->width > (nj_s8)len) {
        return NJ_MR_ERR_SIZE;
    }
    b->raster_off = raster_off;
    return NJ_MR_OK;
}

static nj_s4 mr_blob_geometry(const mr_bmp *b, nj_s4 is_env, nj_s4 *wb,
                              nj_s4 *hb, nj_s4 *pw, nj_s4 *ph)
{
    nj_s4 w, h;
    nj_s8 area;

    if (is_env) {
        *wb = 6;
        *hb = 6;
        *pw = 64;
        *ph = 64;
        return NJ_MR_OK;
    }
    w = mr_count_bits(b->width);
    h = mr_count_bits(b->height);
    if (w > 16 || h > 16) {
        return NJ_MR_ERR_SIZE;
    }
    *wb = w;
    *hb = h;
    *pw = 1 << w;
    *ph = 1 << h;
    /* The blob stores padded_w/padded_h in u16 fields, so a padded side of
     * 65536 (width_bit 16) could never round-trip: reject it at decode time
     * instead of producing a blob that view() must always refuse. */
    if (*pw > 32768 || *ph > 32768) {
        return NJ_MR_ERR_SIZE;
    }
    area = (nj_s8)(*pw) * (nj_s8)(*ph);
    if (area > (nj_s8)(16 * 1024 * 1024)) {
        return NJ_MR_ERR_SIZE;
    }
    return NJ_MR_OK;
}

static nj_s4 mr_blob_size_of(const mr_bmp *b, nj_s4 is_env)
{
    nj_s4 wb, hb, pw, ph;
    nj_s4 rc = mr_blob_geometry(b, is_env, &wb, &hb, &pw, &ph);

    if (rc != NJ_MR_OK) {
        return rc;
    }
    if (is_env) {
        return NJ_MR_BLOB_RASTER_OFF + (nj_s4)(NJ_MR_ENV_SIZE * 4);
    }
    return (nj_s4)(NJ_MR_BLOB_RASTER_OFF + (nj_s8)pw * ph);
}

nj_s4 nj_mascot_texture_query(const void *bmp, nj_s4 bmp_len,
                              nj_s4 is_for_model, nj_s4 *out_size)
{
    mr_bmp b;
    nj_s4 rc;

    if (!out_size) {
        return NJ_MR_ERR_ARG;
    }
    rc = mr_bmp_parse((const nj_u1 *)bmp, bmp_len, !is_for_model, &b);
    if (rc != NJ_MR_OK) {
        return rc;
    }
    rc = mr_blob_size_of(&b, !is_for_model);
    if (rc < 0) {
        return rc;
    }
    *out_size = rc;
    return NJ_MR_OK;
}

nj_s4 nj_mascot_texture_fill(const void *bmp, nj_s4 bmp_len,
                             nj_s4 is_for_model, void *blob, nj_s4 blob_len)
{
    const nj_u1 *d = (const nj_u1 *)bmp;
    nj_u1 *out = (nj_u1 *)blob;
    mr_bmp b;
    nj_s4 is_env = !is_for_model;
    nj_s4 wb, hb, pw, ph, size, rc, y, x;
    nj_u4 flags;

    rc = mr_bmp_parse(d, bmp_len, is_env, &b);
    if (rc != NJ_MR_OK) {
        return rc;
    }
    rc = mr_blob_geometry(&b, is_env, &wb, &hb, &pw, &ph);
    if (rc != NJ_MR_OK) {
        return rc;
    }
    size = mr_blob_size_of(&b, is_env);
    if (size < 0) {
        return size;
    }
    if (blob_len < size) {
        return NJ_MR_ERR_SIZE;
    }
    memset(out, 0, (size_t)size);

    /* Header. */
    mr_put_u32(out + 0, NJ_MR_BLOB_MAGIC);
    mr_put_u16(out + 4, (nj_u2)b.width);
    mr_put_u16(out + 6, (nj_u2)b.height);
    out[8] = (nj_u1)wb;
    out[9] = (nj_u1)hb;
    out[10] = (nj_u1)(is_env ? NJ_MR_TEX_ENVMAP : NJ_MR_TEX_MODEL);
    out[11] = 0;
    mr_put_u16(out + 12, (nj_u2)pw);
    mr_put_u16(out + 14, (nj_u2)ph);
    mr_put_u32(out + 16, NJ_MR_BLOB_RASTER_OFF);
    mr_put_u32(out + 20, (nj_u4)(is_env ? (nj_s8)NJ_MR_ENV_SIZE
                              : (nj_s8)pw * ph));

    /* Palette: raw BMP BGRA entries with alpha forced opaque. */
    for (y = 0; y < b.palette_count; y++) {
        const nj_u1 *e = d + b.palette_off + y * 4;
        mr_put_u32(out + NJ_MR_BLOB_PALETTE_OFF + y * 4,
                   0xFF000000u | ((nj_u4)e[2] << 16)
                   | ((nj_u4)e[1] << 8) | (nj_u4)e[0]);
    }
    flags = is_env ? NJ_MR_TEX_ENVMAP : NJ_MR_TEX_MODEL;
    if (mr_u32(out + NJ_MR_BLOB_PALETTE_OFF) == 0xFF000000u) {
        flags |= NJ_MR_TEX_FIRST_BLACK;
    }
    out[10] = (nj_u1)flags;

    if (!is_env) {
        /* Padded index raster, rows top-down like the reference's bitmapData
         * after its row reversal, padded with index 0. */
        for (y = 0; y < b.height; y++) {
            nj_s4 src_y = b.reversed ? (b.height - 1 - y) : y;
            const nj_u1 *src = d + b.raster_off + (nj_s8)src_y * b.stride;
            nj_u1 *dstrow = out + NJ_MR_BLOB_RASTER_OFF + (nj_s8)y * pw;
            memcpy(dstrow, src, (size_t)b.width);
        }
        return NJ_MR_OK;
    }

    /* Environment map: build the 64x64 index raster in the first 4096 bytes of
     * the raster area, then expand to ARGB in place (backwards, so the
     * 4x larger writes never clobber unread indices). */
    {
        nj_u1 *idx = out + NJ_MR_BLOB_RASTER_OFF;
        for (y = 0; y < b.height; y++) {
            nj_s4 src_y = b.reversed ? (b.height - 1 - y) : y;
            const nj_u1 *src = d + b.raster_off + (nj_s8)src_y * b.stride;
            memcpy(idx + (nj_s8)y * 64, src, (size_t)b.width);
        }
        (void)x;
        for (x = NJ_MR_ENV_SIZE - 1; x >= 0; x--) {
            nj_u4 id = idx[x];
            nj_u4 env = 0;
            if (id != 0) {
                env = mr_u32(out + NJ_MR_BLOB_PALETTE_OFF + id * 4)
                    & 0x00FFFFFFu;
            }
            mr_put_u32(idx + (nj_s8)x * 4, env);
        }
    }
    return NJ_MR_OK;
}

nj_s4 nj_mascot_texture_view(const void *blob, nj_s4 blob_len,
                             nj_mascot_texture *out)
{
    const nj_u1 *p = (const nj_u1 *)blob;
    nj_u4 magic, flags, raster_off, raster_len, wb, hb, pw, ph, need;
    nj_s8 len;

    if (!p || !out || blob_len < (nj_s4)NJ_MR_BLOB_RASTER_OFF) {
        return NJ_MR_ERR_ARG;
    }
    magic = mr_u32(p + 0);
    if (magic != NJ_MR_BLOB_MAGIC) {
        return NJ_MR_ERR_FORMAT;
    }
    wb = p[8];
    hb = p[9];
    flags = p[10];
    pw = mr_u16(p + 12);
    ph = mr_u16(p + 14);
    raster_off = mr_u32(p + 16);
    raster_len = mr_u32(p + 20);
    /* padded_w/padded_h are u16: width_bit/height_bit 16 would need 65536,
     * which cannot be stored, so the largest representable padded side is
     * 32768 (bit 15).  The decoder enforces the same limit. */
    if (wb > 15 || hb > 15 || pw != (1u << wb) || ph != (1u << hb)) {
        return NJ_MR_ERR_FORMAT;
    }
    /* Crafted blobs must not be able to widen len_mask past the payload:
     * cap the padded area (2^24 texels) and compare sizes in 64 bits. */
    if (wb + hb > 24) {
        return NJ_MR_ERR_FORMAT;
    }
    if ((flags & (NJ_MR_TEX_MODEL | NJ_MR_TEX_ENVMAP)) == 0
        || (flags & (NJ_MR_TEX_MODEL | NJ_MR_TEX_ENVMAP))
           == (NJ_MR_TEX_MODEL | NJ_MR_TEX_ENVMAP)) {
        return NJ_MR_ERR_FORMAT;
    }
    if (raster_off != NJ_MR_BLOB_RASTER_OFF) {
        return NJ_MR_ERR_FORMAT;
    }
    if (flags & NJ_MR_TEX_ENVMAP) {
        if (wb != 6 || hb != 6 || raster_len != NJ_MR_ENV_SIZE) {
            return NJ_MR_ERR_FORMAT;
        }
        need = NJ_MR_BLOB_RASTER_OFF + NJ_MR_ENV_SIZE * 4u;
    } else {
        if (raster_len == 0
            || (nj_u8)pw * (nj_u8)ph != (nj_u8)raster_len) {
            return NJ_MR_ERR_FORMAT;
        }
        need = NJ_MR_BLOB_RASTER_OFF + raster_len;
    }
    len = (nj_s8)blob_len;
    if (len < (nj_s8)need) {
        return NJ_MR_ERR_SIZE;
    }
    out->palette = p + NJ_MR_BLOB_PALETTE_OFF;
    out->raster = p + raster_off;
    out->width_bit = (nj_s4)wb;
    out->height_bit = (nj_s4)hb;
    out->len_mask = (nj_s4)((flags & NJ_MR_TEX_ENVMAP)
                            ? (NJ_MR_ENV_SIZE - 1u)
                            : (raster_len - 1u));
    out->flags = (nj_s4)flags;
    return NJ_MR_OK;
}

/* ------------------------------------------------------------------ */
/* Triangle rasterizer                                                 */
/* ------------------------------------------------------------------ */
typedef struct mr_vert {
    nj_s8 x, y;
    nj_s8 u, v;
    nj_s8 s;
    nj_s8 eu, ev;
} mr_vert;

typedef struct mr_state {
    nj_surface *dst;
    const nj_mascot_texture *tex;
    const nj_mascot_texture *env;
    nj_u4 color;       /* 0x00RRGGBB */
    nj_u4 color_rb, color_g;
    nj_s4 flags;
    nj_s4 blend;
    nj_s4 toon_on;
    nj_s4 toon_thr, toon_hi, toon_lo;
    nj_s4 key_idx;     /* texel index treated as transparent, 256 = none */
    nj_s4 flat_level;  /* palette row for the flat-shaded texture paths */
    nj_s8 flat_shade;  /* flat light in 12.12, from vertex 0 */
    nj_s4 env_smooth;
    nj_s8 flat_eu, flat_ev;
    nj_s4 has_tex, smooth, tex_shaded;
} mr_state;

/* Reference flat colour formula: channel * (shade + 1) >> 5. */
static inline nj_u4 mr_flat_color(nj_u4 color, nj_s8 shade12)
{
    nj_s4 shade = (nj_s4)(shade12 >> MR_FP);
    nj_u4 rgb = 0;

    rgb |= (((color & 0x00FF0000u) * (nj_u4)(shade + 1)) >> 5) & 0x00FF0000u;
    rgb |= (((color & 0x0000FF00u) * (nj_u4)(shade + 1)) >> 5) & 0x0000FF00u;
    rgb |= (((color & 0x000000FFu) * (nj_u4)(shade + 1)) >> 5) & 0x000000FFu;
    return 0xFF000000u | rgb;
}

/* Toon: the reference compares (rawLight >> 4) against the threshold; a raw
 * light of L is shade12 = (L >> 2) << 7, so L >> 4 == shade12 >> 9. */
static inline nj_s4 mr_toon_pick(const mr_state *st, nj_s8 shade12)
{
    nj_s4 light4 = (nj_s4)(shade12 >> 9);

    return light4 <= st->toon_thr ? st->toon_lo : st->toon_hi;
}

static inline nj_u4 mr_pal_get(const nj_u1 *pal, nj_s4 index)
{
    return mr_u32(pal + (nj_s8)index * 4);
}

/* Reference Texture.generateShadedPalette(): channel * shade / 31 truncating,
 * alpha opaque.  The blob keeps only the 256 unshaded entries, so shading is
 * applied at sample time instead of pre-generating 32 rows. */
static inline nj_u4 mr_tex_shade(nj_u4 pal, nj_s4 level)
{
    nj_u4 r, g, b;

    if (level <= 0) {
        return 0xFF000000u;
    }
    if (level > 31) {
        level = 31;
    }
    r = (((pal >> 16) & 0xFFu) * (nj_u4)level) / 31u;
    g = (((pal >> 8) & 0xFFu) * (nj_u4)level) / 31u;
    b = ((pal & 0xFFu) * (nj_u4)level) / 31u;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static inline nj_u4 mr_env_add(nj_u4 color, nj_u4 env)
{
    nj_u4 tmp = (((color + env - ((color ^ env) & 0x01010101u)) >> 8)
                 & 0x010101u) * 0xFFu;

    return 0xFF000000u | (color + env - tmp) | tmp;
}

/* Fill the rows of one clipped segment.  All coordinates are 12.12 fixed
 * point; the per-row edge starts advance by the edge derivatives and the
 * in-row interpolators by the scanline derivatives. */

/* Attribute combinations that dominate real scenes, written as tight span
 * loops.  Every flag is constant for a triangle, so the branch tree the
 * generic loop evaluates per pixel only needs to be resolved once per row;
 * the numeric steps are copied verbatim from the generic body so pixels stay
 * byte-identical.  Everything the table does not name - toon, colour keys in
 * some orders, environment maps, ADD/SUB - keeps using the generic loop. */
enum {
    MR_MODE_GENERIC = 0
#if NJ_MASCOT_RASTER_SPANS
    , MR_MODE_TEX_SMOOTH_PUT,
    MR_MODE_TEX_SMOOTH_HALF,
    MR_MODE_TEX_FLAT_PUT,
    MR_MODE_TEX_FLAT_HALF,
    MR_MODE_COLOR_SMOOTH_PUT,
    MR_MODE_COLOR_SMOOTH_HALF,
    MR_MODE_CONST_PUT,
    MR_MODE_CONST_HALF
#endif
};

#if NJ_MASCOT_RASTER_SPANS
#define MR_DEFINE_TEX_SPAN(NAME, SMOOTH, BLENDED)                            \
static inline void NAME(nj_u1 *pp, nj_s4 px0, nj_s4 px1,                     \
                        nj_s4 uu, nj_s4 vv, nj_s4 ss,                        \
                        nj_s4 du, nj_s4 dv, nj_s4 ds,                        \
                        const nj_u1 *t_raster, const nj_u1 *t_palette,       \
                        nj_s4 t_wbit, nj_s4 t_mask, nj_s4 key_idx,           \
                        nj_s4 flat_level, nj_s4 blend, nj_s4 fmt, nj_s4 bpp)\
{                                                                            \
    nj_s4 px;                                                                \
    for (px = px0; px < px1;                                                 \
         px++, uu += du, vv += dv, ss += ds, pp += bpp) {                    \
        nj_s4 ti = (nj_s4)t_raster[                                          \
            (nj_s4)((((nj_u4)(vv >> MR_FP) << t_wbit)                        \
                     | (nj_u4)(uu >> MR_FP)) & (nj_u4)t_mask)];              \
        nj_u4 pix;                                                           \
        nj_s4 level;                                                         \
        if (ti == key_idx) {                                                 \
            continue;                                                        \
        }                                                                    \
        pix = mr_pal_get(t_palette, ti & 0xFF);                              \
        level = (SMOOTH) ? (nj_s4)((ss >> MR_FP) & 0x1F) : flat_level;       \
        pix = mr_tex_shade(pix, level);                                      \
        if (BLENDED) {                                                       \
            mr_raw_put(pp, mr_blend(pix, mr_raw_get(pp, fmt), blend), fmt);  \
        } else {                                                             \
            mr_raw_put(pp, pix, fmt);                                        \
        }                                                                    \
    }                                                                        \
}

MR_DEFINE_TEX_SPAN(mr_span_tex_smooth_put, 1, 0)
MR_DEFINE_TEX_SPAN(mr_span_tex_smooth_half, 1, 1)
MR_DEFINE_TEX_SPAN(mr_span_tex_flat_put, 0, 0)
MR_DEFINE_TEX_SPAN(mr_span_tex_flat_half, 0, 1)

#define MR_DEFINE_COLOR_SPAN(NAME, BLENDED)                                  \
static inline void NAME(nj_u1 *pp, nj_s4 px0, nj_s4 px1,                     \
                        nj_s4 ss, nj_s4 ds, nj_u4 color_rb, nj_u4 color_g,   \
                        nj_s4 blend, nj_s4 fmt, nj_s4 bpp)                   \
{                                                                            \
    nj_s4 px;                                                                \
    for (px = px0; px < px1; px++, ss += ds, pp += bpp) {                    \
        nj_s4 level = (nj_s4)(ss >> MR_FP);                                   \
        nj_u4 rb = ((color_rb & 0x00FF00FFu) * (nj_u4)level)                 \
                 & (0x00FF00FFu << 5);                                        \
        nj_u4 g = ((color_g & 0x0000FF00u) * (nj_u4)level)                   \
                & (0x0000FF00u << 5);                                         \
        nj_u4 pix = 0xFF000000u | ((rb | g) >> 5);                           \
        if (BLENDED) {                                                        \
            mr_raw_put(pp, mr_blend(pix, mr_raw_get(pp, fmt), blend), fmt);   \
        } else {                                                              \
            mr_raw_put(pp, pix, fmt);                                         \
        }                                                                     \
    }                                                                         \
}

MR_DEFINE_COLOR_SPAN(mr_span_color_smooth_put, 0)
MR_DEFINE_COLOR_SPAN(mr_span_color_smooth_half, 1)

#define MR_DEFINE_CONST_SPAN(NAME, BLENDED)                                  \
static inline void NAME(nj_u1 *pp, nj_s4 px0, nj_s4 px1,                     \
                        nj_u4 pix, nj_s4 blend, nj_s4 fmt, nj_s4 bpp)        \
{                                                                            \
    nj_s4 px;                                                                \
    for (px = px0; px < px1; px++, pp += bpp) {                              \
        if (BLENDED) {                                                        \
            mr_raw_put(pp, mr_blend(pix, mr_raw_get(pp, fmt), blend), fmt);   \
        } else {                                                              \
            mr_raw_put(pp, pix, fmt);                                         \
        }                                                                     \
    }                                                                         \
}

MR_DEFINE_CONST_SPAN(mr_span_const_put, 0)
MR_DEFINE_CONST_SPAN(mr_span_const_half, 1)
#endif

static void mr_fill_segment(const mr_state *st,
                            nj_s4 clip_x1, nj_s4 clip_x2,
                            nj_s4 y_start, nj_s4 y_end,
                            nj_s8 x, nj_s4 dx, nj_s8 x2, nj_s4 dx2,
                            nj_s8 u, nj_s4 du, nj_s8 v, nj_s4 dv,
                            nj_s8 s, nj_s4 ds,
                            nj_s8 eu, nj_s4 deu, nj_s8 ev, nj_s4 dev,
                            nj_s4 du_row, nj_s4 dv_row, nj_s4 ds_row,
                            nj_s4 deu_row, nj_s4 dev_row)
{
    nj_s4 y;
    nj_s4 subpixel = 0;

    /* The pixel stores below write through an unsigned char pointer, which the
     * compiler must assume can alias *st.  Copying every field the inner loop
     * reads into locals keeps them in registers instead of reloading them for
     * each pixel, and lets the row walk keep a running pixel pointer instead
     * of recomputing y*stride + x*bpp per pixel. */
    const nj_s4 has_tex = st->has_tex;
    const nj_s4 smooth = st->smooth;
    const nj_s4 tex_shaded = st->tex_shaded;
    const nj_s4 env_smooth = st->env_smooth;
    const nj_s4 blend = st->blend;
    const nj_s4 key_idx = st->key_idx;
    const nj_s4 toon_on = st->toon_on;
    const nj_s4 toon_thr = st->toon_thr;
    const nj_s4 toon_hi = st->toon_hi;
    const nj_s4 toon_lo = st->toon_lo;
    const nj_s4 flat_level = st->flat_level;
    const nj_s8 flat_shade = st->flat_shade;
    const nj_u4 color = st->color;
    const nj_u4 color_rb = st->color_rb;
    const nj_u4 color_g = st->color_g;
    const nj_u1 *t_raster = has_tex ? st->tex->raster : NULL;
    const nj_u1 *t_palette = has_tex ? st->tex->palette : NULL;
    const nj_s4 t_wbit = has_tex ? st->tex->width_bit : 0;
    const nj_s4 t_mask = has_tex ? st->tex->len_mask : 0;
    const nj_u1 *e_raster = env_smooth ? st->env->raster : NULL;
    const nj_s4 e_wbit = env_smooth ? st->env->width_bit : 0;
    const nj_s4 e_mask = env_smooth ? st->env->len_mask : 0;
    const nj_s4 fmt = st->dst->format;
    const nj_s4 bpp = mr_bpp_of(st->dst);
    const nj_s8 stride = st->dst->stride;
    nj_u1 *const base = (nj_u1 *)st->dst->pixels;

    /* Resolve the per-attribute branch tree once; see the span helpers. */
    int mode = MR_MODE_GENERIC;

#if NJ_MASCOT_RASTER_SPANS
    if (!toon_on && !env_smooth) {
        if (has_tex && tex_shaded) {
            if (smooth) {
                mode = blend == NJ_MR_BLEND_REPLACE ? MR_MODE_TEX_SMOOTH_PUT
                     : blend == NJ_MR_BLEND_HALF ? MR_MODE_TEX_SMOOTH_HALF
                     : MR_MODE_GENERIC;
            } else {
                mode = blend == NJ_MR_BLEND_REPLACE ? MR_MODE_TEX_FLAT_PUT
                     : blend == NJ_MR_BLEND_HALF ? MR_MODE_TEX_FLAT_HALF
                     : MR_MODE_GENERIC;
            }
        } else if (!has_tex && smooth) {
            mode = blend == NJ_MR_BLEND_REPLACE ? MR_MODE_COLOR_SMOOTH_PUT
                 : blend == NJ_MR_BLEND_HALF ? MR_MODE_COLOR_SMOOTH_HALF
                 : MR_MODE_GENERIC;
        } else if (!has_tex && !smooth) {
            mode = blend == NJ_MR_BLEND_REPLACE ? MR_MODE_CONST_PUT
                 : blend == NJ_MR_BLEND_HALF ? MR_MODE_CONST_HALF
                 : MR_MODE_GENERIC;
        }
    }
#else
    (void)mode;
#endif

    /* "Corrected rounding to avoid texture seams": nudge the interpolators by
     * one subpixel in the direction they are moving.  Only the attributes the
     * reference interpolates get this. */
    if (has_tex) {
        if (du_row != 0) u += du_row > 0 ? 1 : -1;
        if (dv_row != 0) v += dv_row > 0 ? 1 : -1;
    }
    if (smooth && ds_row != 0) {
        s += ds_row > 0 ? 1 : -1;
    }
    if (env_smooth) {
        if (deu_row != 0) eu += deu_row > 0 ? 1 : -1;
        if (dev_row != 0) ev += dev_row > 0 ? 1 : -1;
    }
    if (has_tex || smooth || env_smooth) {
        subpixel = 1;
    }

    for (y = y_start; y < y_end; y++) {
        nj_s8 fx = x, fx2 = x2;
        nj_s4 xl = (nj_s4)(fx >> MR_FP);
        nj_s4 xr = (nj_s4)(fx2 >> MR_FP);
        nj_s4 uu = (nj_s4)u, vv = (nj_s4)v, ss = (nj_s4)s;
        nj_s4 eeu = (nj_s4)eu, eev = (nj_s4)ev;
        nj_s4 px;
        nj_u1 *pp;

        if (subpixel) {
            /* "Subpixel precision, ceil rounding". */
            nj_s4 t = MR_FP_ONE - (nj_s4)(x & (MR_FP_ONE - 1));

            if (has_tex) {
                uu += (nj_s4)(((nj_s8)du * t) >> MR_FP);
                vv += (nj_s4)(((nj_s8)dv * t) >> MR_FP);
            }
            if (smooth) {
                ss += (nj_s4)(((nj_s8)ds * t) >> MR_FP);
            }
            if (env_smooth) {
                eeu += (nj_s4)(((nj_s8)deu * t) >> MR_FP);
                eev += (nj_s4)(((nj_s8)dev * t) >> MR_FP);
            }
        }
        if (xl < clip_x1) {
            nj_s4 t = xl - clip_x1;

            if (has_tex) {
                uu -= du * t;
                vv -= dv * t;
            }
            if (smooth) {
                ss -= ds * t;
            }
            if (env_smooth) {
                eeu -= deu * t;
                eev -= dev * t;
            }
            xl = clip_x1;
        }
        if (xr > clip_x2) {
            xr = clip_x2;
        }
        /* xl >= clip_x1 >= 0 here, so the row start is inside the surface when
         * the span is non-empty; empty spans keep a dummy pointer. */
        pp = xl < xr ? base + (nj_s8)y * stride + (nj_s8)xl * bpp : base;
        if (xl < xr) {
            switch (mode) {
#if NJ_MASCOT_RASTER_SPANS
            case MR_MODE_TEX_SMOOTH_PUT:
                mr_span_tex_smooth_put(pp, xl, xr, uu, vv, ss, du, dv, ds,
                                       t_raster, t_palette, t_wbit, t_mask,
                                       key_idx, flat_level, blend, fmt, bpp);
                break;
            case MR_MODE_TEX_SMOOTH_HALF:
                mr_span_tex_smooth_half(pp, xl, xr, uu, vv, ss, du, dv, ds,
                                        t_raster, t_palette, t_wbit, t_mask,
                                        key_idx, flat_level, blend, fmt, bpp);
                break;
            case MR_MODE_TEX_FLAT_PUT:
                mr_span_tex_flat_put(pp, xl, xr, uu, vv, ss, du, dv, ds,
                                     t_raster, t_palette, t_wbit, t_mask,
                                     key_idx, flat_level, blend, fmt, bpp);
                break;
            case MR_MODE_TEX_FLAT_HALF:
                mr_span_tex_flat_half(pp, xl, xr, uu, vv, ss, du, dv, ds,
                                      t_raster, t_palette, t_wbit, t_mask,
                                      key_idx, flat_level, blend, fmt, bpp);
                break;
            case MR_MODE_COLOR_SMOOTH_PUT:
                mr_span_color_smooth_put(pp, xl, xr, ss, ds, color_rb, color_g,
                                         blend, fmt, bpp);
                break;
            case MR_MODE_COLOR_SMOOTH_HALF:
                mr_span_color_smooth_half(pp, xl, xr, ss, ds, color_rb, color_g,
                                          blend, fmt, bpp);
                break;
            case MR_MODE_CONST_PUT:
                mr_span_const_put(pp, xl, xr, mr_flat_color(color, flat_shade),
                                  blend, fmt, bpp);
                break;
            case MR_MODE_CONST_HALF:
                mr_span_const_half(pp, xl, xr, mr_flat_color(color, flat_shade),
                                   blend, fmt, bpp);
                break;
#endif
            default:
            for (px = xl; px < xr;
                 px++, uu += du, vv += dv, ss += ds, eeu += deu, eev += dev,
                 pp += bpp) {
                nj_u4 pix;

                if (has_tex) {
                    nj_s4 ti = (nj_s4)t_raster[
                        (nj_s4)((((nj_u4)(vv >> MR_FP) << t_wbit)
                                 | (nj_u4)(uu >> MR_FP)) & (nj_u4)t_mask)];

                    if (ti == key_idx) {
                        continue;
                    }
                    pix = mr_pal_get(t_palette, ti & 0xFF);
                    if (tex_shaded) {
                        nj_s4 level;

                        if (smooth) {
                            level = toon_on
                                ? ((ss >> 9) <= toon_thr ? toon_lo : toon_hi) >> 3
                                : (nj_s4)((ss >> MR_FP) & 0x1F);
                        } else {
                            level = flat_level;
                        }
                        pix = mr_tex_shade(pix, level);
                    }
                } else if (smooth) {
                    if (toon_on) {
                        nj_s4 col = (ss >> 9) <= toon_thr ? toon_lo : toon_hi;

                        pix = mr_flat_color(color, (nj_s8)col * 512);
                    } else {
                        nj_s4 level = (nj_s4)(ss >> MR_FP);
                        nj_u4 rb = ((color_rb & 0x00FF00FFu) * (nj_u4)level)
                                 & (0x00FF00FFu << 5);
                        nj_u4 g = ((color_g & 0x0000FF00u) * (nj_u4)level)
                                & (0x0000FF00u << 5);

                        pix = 0xFF000000u | ((rb | g) >> 5);
                    }
                } else {
                    nj_s8 shade12 = flat_shade;

                    if (toon_on) {
                        nj_s4 col = (shade12 >> 9) <= toon_thr
                            ? toon_lo : toon_hi;

                        shade12 = (nj_s8)col * 512;
                    }
                    pix = mr_flat_color(color, shade12);
                }

                if (env_smooth) {
                    nj_s4 ei = (nj_s4)
                        ((((nj_u4)(eev >> MR_FP) << e_wbit)
                          | (nj_u4)(eeu >> MR_FP))
                         & (nj_u4)e_mask);
                    nj_u4 ep = mr_u32(e_raster + (nj_s8)ei * 4);

                    if (ep != 0) {
                        pix = mr_env_add(pix, ep);
                    }
                }

                if (blend == NJ_MR_BLEND_REPLACE) {
                    mr_raw_put(pp, pix, fmt);
                } else {
                    mr_raw_put(pp, mr_blend(pix, mr_raw_get(pp, fmt), blend), fmt);
                }
            }
                break;
            }
        }
        x += dx;
        x2 += dx2;
        u += du_row;
        v += dv_row;
        s += ds_row;
        eu += deu_row;
        ev += dev_row;
    }
}

/*
 * One triangle.  The segment setup is a transcription of the reference's
 * fillTriangleAffine* functions: sort the vertices by y, compute the long and
 * short edge derivatives, then two segments (top..middle, middle..bottom)
 * clipped to [clip_y1, clip_y2).
 */
void nj_mascot_raster_triangle(nj_surface *dst,
                               nj_s4 clip_x1, nj_s4 clip_y1,
                               nj_s4 clip_x2, nj_s4 clip_y2,
                               const nj_mascot_vertex verts[3],
                               nj_u4 color,
                               const nj_mascot_texture *tex,
                               const nj_mascot_texture *env,
                               nj_u4 flags, nj_s4 blend, nj_u4 toon)
{
    mr_state st;
    mr_vert v[3];
    mr_vert ta, tb;
    nj_s8 dx_start, dx_end, du_start, dv_start, ds_start, deu_start, dev_start;
    nj_s8 du_end, dv_end, ds_end, deu_end, dev_end;
    nj_s8 du_across, dv_across, ds_across, deu_across, dev_across;
    nj_s8 x_start, x_end, u_start, u_end, v_start, v_end;
    nj_s8 s_start, s_end, eu_start, eu_end, ev_start, ev_end;
    nj_s8 y_start, y_end;
    nj_s8 temp, temp_i;
    nj_s4 i;
    nj_s4 has_tex, smooth;

    if (!mr_surface_ok(dst) || !verts) {
        return;
    }
    memset(&st, 0, sizeof st);
    st.dst = dst;
    st.tex = tex;
    st.env = env;
    st.color = color & 0x00FFFFFFu;
    st.color_rb = color & 0x00FF00FFu;
    st.color_g = color & 0x0000FF00u;
    st.flags = (nj_s4)flags;
    st.blend = blend;
    has_tex = (flags & NJ_MR_TEXTURED) != 0;
    smooth = (flags & NJ_MR_LIGHTING) != 0;
    st.has_tex = has_tex;
    st.smooth = smooth;
    st.tex_shaded = has_tex && (smooth || (flags & NJ_MR_SHADED));

    if (has_tex && !tex) {
        return;
    }
    if ((flags & NJ_MR_ENVMAP) && !env) {
        return;
    }
    if (has_tex && (tex->flags & NJ_MR_TEX_MODEL) == 0) {
        return;
    }
    if ((flags & NJ_MR_ENVMAP) && (env->flags & NJ_MR_TEX_ENVMAP) == 0) {
        return;
    }
    /* The reference interpolates env coordinates for every ENVMAP entry
     * (TE/CE interpolate env while their shade stays flat). */
    st.env_smooth = (flags & NJ_MR_ENVMAP) ? 1 : 0;

    /* Reference Graphics3D gates toon on the material lighting bit:
     * `boolean toon = lighting && efxToon;`.  In this API a lit polygon is
     * marked by NJ_MR_SHADED (flat lit) and/or NJ_MR_LIGHTING (smooth lit);
     * an unlit polygon carries neither and must not be toon-shaded, otherwise
     * a scene-level toon word turns a flat unlit colour into the low toon
     * colour. */
    if ((toon & 1u) && (flags & (NJ_MR_SHADED | NJ_MR_LIGHTING))) {
        st.toon_on = 1;
        st.toon_thr = (nj_s4)((toon >> 8) & 0xFFu);
        st.toon_hi = (nj_s4)((toon >> 16) & 0xFFu);
        st.toon_lo = (nj_s4)((toon >> 24) & 0xFFu);
    }

    /* Transparent texel index.  The reference's ADD and SUB row fillers
     * additionally treat index 0 as transparent when the palette starts with
     * opaque black. */
    if (has_tex) {
        st.key_idx = 256;
        if ((flags & NJ_MR_COLORKEY)
            || ((tex->flags & NJ_MR_TEX_FIRST_BLACK)
                && (blend == NJ_MR_BLEND_ADD || blend == NJ_MR_BLEND_SUB))) {
            st.key_idx = 0;
        }
    }

    /* Clamp the destination rectangle to the surface. */
    if (clip_x1 < 0) clip_x1 = 0;
    if (clip_y1 < 0) clip_y1 = 0;
    if (clip_x2 > dst->width) clip_x2 = dst->width;
    if (clip_y2 > dst->height) clip_y2 = dst->height;
    if (clip_x1 >= clip_x2 || clip_y1 >= clip_y2) {
        return;
    }

    /* Vertex conversion to the reference's 12.12 fixed point. */
    for (i = 0; i < 3; i++) {
        nj_s4 light = verts[i].light;

        v[i].x = verts[i].x;
        v[i].y = verts[i].y;
        v[i].u = (nj_s8)verts[i].u * 16;
        v[i].v = (nj_s8)verts[i].v * 16;
        if (st.toon_on) {
            /* Toon compares the signed light, so do not clamp it. */
            v[i].s = (nj_s8)(((nj_s8)light >> 2) * 128);
        } else {
            if (light < 0) light = 0;
            if (light > 4095) light = 4095;
            v[i].s = (nj_s8)((light >> 2) * 128);
        }
        v[i].eu = (nj_s8)verts[i].eu * 256;
        v[i].ev = (nj_s8)verts[i].ev * 256;
    }

    /* Flat attributes come from vertex 0 (they are the same on all three for
     * a flat polygon). */
    {
        nj_s4 light = verts[0].light;

        if (st.toon_on) {
            st.flat_shade = (nj_s8)(((nj_s8)light >> 2) * 128);
        } else {
            if (light < 0) light = 0;
            if (light > 4095) light = 4095;
            st.flat_shade = (nj_s8)((light >> 2) * 128);
        }
        if (has_tex) {
            if (st.toon_on) {
                st.flat_level = mr_toon_pick(&st, st.flat_shade) >> 3;
            } else if (st.tex_shaded) {
                st.flat_level = (nj_s4)(st.flat_shade >> MR_FP);
            } else {
                st.flat_level = 0; /* unshaded 256-entry palette */
            }
            if (st.flat_level < 0) st.flat_level = 0;
            if (st.flat_level > 31) st.flat_level = 31;
        }
        st.flat_eu = (nj_s8)verts[0].eu * 256;
        st.flat_ev = (nj_s8)verts[0].ev * 256;
    }

    /* Reference sort: two bubble passes by y. */
    if (v[0].y > v[1].y) {
        ta = v[0]; v[0] = v[1]; v[1] = ta;
    }
    if (v[1].y > v[2].y) {
        tb = v[1]; v[1] = v[2]; v[2] = tb;
    }
    if (v[0].y > v[1].y) {
        ta = v[0]; v[0] = v[1]; v[1] = ta;
    }

    {
        const nj_s8 ax = v[0].x, ay = v[0].y;
        const nj_s8 bx = v[1].x, by = v[1].y;
        const nj_s8 cx = v[2].x, cy = v[2].y;
        const nj_s8 au = v[0].u, av = v[0].v, as = v[0].s;
        const nj_s8 aeu = v[0].eu, aev = v[0].ev;
        const nj_s8 bu = v[1].u, bv = v[1].v, bs = v[1].s;
        const nj_s8 beu = v[1].eu, bev = v[1].ev;
        const nj_s8 cu = v[2].u, cv = v[2].v, cs = v[2].s;
        const nj_s8 ceu = v[2].eu, cev = v[2].ev;

        if (cy == ay) {
            return;
        }
        if (cy <= clip_y1 || ay >= clip_y2) {
            return;
        }

        /* Long edge (a -> c) and short edge (a -> b / b -> c).
         * The row filler never reads inactive attributes. Avoid their divides
         * on every edge and scanline derivative, while leaving active rounding
         * and the flat vertex-0 attributes unchanged. */
        temp = cy - ay;
        dx_start = ((cx - ax) * MR_FP_ONE) / temp;
        du_start = has_tex ? (cu - au) / temp : 0;
        dv_start = has_tex ? (cv - av) / temp : 0;
        ds_start = smooth ? (cs - as) / temp : 0;
        deu_start = st.env_smooth ? (ceu - aeu) / temp : 0;
        dev_start = st.env_smooth ? (cev - aev) / temp : 0;

        dx_end = 0;
        du_end = dv_end = ds_end = deu_end = dev_end = 0;
        if (by != ay) {
            temp_i = by - ay;
            dx_end = ((bx - ax) * MR_FP_ONE) / temp_i;
            du_end = has_tex ? (bu - au) / temp_i : 0;
            dv_end = has_tex ? (bv - av) / temp_i : 0;
            ds_end = smooth ? (bs - as) / temp_i : 0;
            deu_end = st.env_smooth ? (beu - aeu) / temp_i : 0;
            dev_end = st.env_smooth ? (bev - aev) / temp_i : 0;
        }

        /* Scanline derivatives across the segment (symmetric ceil). */
        x_start = ax * MR_FP_ONE + dx_start * (by - ay);
        u_start = au + du_start * (by - ay);
        v_start = av + dv_start * (by - ay);
        s_start = as + ds_start * (by - ay);
        eu_start = aeu + deu_start * (by - ay);
        ev_start = aev + dev_start * (by - ay);
        x_end = bx * MR_FP_ONE;
        u_end = bu;
        v_end = bv;
        s_end = bs;
        eu_end = beu;
        ev_end = bev;

        temp = (x_start - x_end) >> MR_FP;
        if (temp < 0) {
            temp -= 1;
        } else {
            temp += 1;
        }
        du_across = has_tex ? (u_start - u_end) / temp : 0;
        dv_across = has_tex ? (v_start - v_end) / temp : 0;
        ds_across = smooth ? (s_start - s_end) / temp : 0;
        deu_across = st.env_smooth ? (eu_start - eu_end) / temp : 0;
        dev_across = st.env_smooth ? (ev_start - ev_end) / temp : 0;

        x_end = x_start = ax * MR_FP_ONE;
        u_end = u_start = au;
        v_end = v_start = av;
        s_end = s_start = as;
        eu_end = eu_start = aeu;
        ev_end = ev_start = aev;

        y_start = ay;
        y_end = by;
        for (i = 0; i < 2; i++) {
            if (y_end > clip_y1) {
                nj_s8 y_draw = y_end < clip_y2 ? y_end : clip_y2;

                if (y_start < clip_y1) {
                    nj_s8 t = y_start - clip_y1;

                    x_start -= dx_start * t;
                    u_start -= du_start * t;
                    v_start -= dv_start * t;
                    s_start -= ds_start * t;
                    eu_start -= deu_start * t;
                    ev_start -= dev_start * t;
                    x_end -= dx_end * t;
                    u_end -= du_end * t;
                    v_end -= dv_end * t;
                    s_end -= ds_end * t;
                    eu_end -= deu_end * t;
                    ev_end -= dev_end * t;
                    y_start = clip_y1;
                }
                if (y_draw > y_start) {
                    nj_s4 left_first = (x_start < x_end)
                        || (x_start == x_end && dx_start < dx_end);

                    if (left_first) {
                        mr_fill_segment(&st, clip_x1, clip_x2,
                                        (nj_s4)y_start, (nj_s4)y_draw,
                                        x_start, (nj_s4)dx_start, x_end,
                                        (nj_s4)dx_end,
                                        u_start, (nj_s4)du_across, v_start,
                                        (nj_s4)dv_across, s_start,
                                        (nj_s4)ds_across, eu_start,
                                        (nj_s4)deu_across, ev_start,
                                        (nj_s4)dev_across, (nj_s4)du_start,
                                        (nj_s4)dv_start, (nj_s4)ds_start,
                                        (nj_s4)deu_start,
                                        (nj_s4)dev_start);
                    } else {
                        mr_fill_segment(&st, clip_x1, clip_x2,
                                        (nj_s4)y_start, (nj_s4)y_draw,
                                        x_end, (nj_s4)dx_end, x_start,
                                        (nj_s4)dx_start,
                                        u_end, (nj_s4)du_across, v_end,
                                        (nj_s4)dv_across, s_end,
                                        (nj_s4)ds_across, eu_end,
                                        (nj_s4)deu_across, ev_end,
                                        (nj_s4)dev_across, (nj_s4)du_end,
                                        (nj_s4)dv_end, (nj_s4)ds_end,
                                        (nj_s4)deu_end,
                                        (nj_s4)dev_end);
                    }
                }
            }
            if (i == 1) {
                break;
            }
            if (by >= clip_y2) {
                break;
            }
            if (cy == by) {
                break;
            }
            temp_i = by - ay;
            x_start = ax * MR_FP_ONE + dx_start * temp_i;
            u_start = au + du_start * temp_i;
            v_start = av + dv_start * temp_i;
            s_start = as + ds_start * temp_i;
            eu_start = aeu + deu_start * temp_i;
            ev_start = aev + dev_start * temp_i;
            x_end = bx * MR_FP_ONE;
            u_end = bu;
            v_end = bv;
            s_end = bs;
            eu_end = beu;
            ev_end = bev;
            temp_i = cy - by;
            dx_end = ((cx - bx) * MR_FP_ONE) / temp_i;
            du_end = has_tex ? (cu - bu) / temp_i : 0;
            dv_end = has_tex ? (cv - bv) / temp_i : 0;
            ds_end = smooth ? (cs - bs) / temp_i : 0;
            deu_end = st.env_smooth ? (ceu - beu) / temp_i : 0;
            dev_end = st.env_smooth ? (cev - bev) / temp_i : 0;
            y_start = by;
            y_end = cy;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public material draw                                                */
/* ------------------------------------------------------------------ */
nj_s4 nj_mascot_raster_draw(nj_surface *dst,
                            nj_s4 clip_x1, nj_s4 clip_y1,
                            nj_s4 clip_x2, nj_s4 clip_y2,
                            const nj_mascot_vertex verts[3],
                            const nj_mascot_material *mat)
{
    nj_mascot_texture tex;
    nj_mascot_texture env;
    const nj_mascot_texture *ptex = 0;
    const nj_mascot_texture *penv = 0;
    nj_u4 flags = mat ? (nj_u4)mat->flags : 0;
    nj_u4 color = mat ? mat->color : 0;
    nj_s4 blend = mat ? mat->blend : NJ_MR_BLEND_REPLACE;
    nj_u4 toon = mat ? mat->toon : 0;
    nj_s4 rc;

    if (!dst || !verts) {
        return NJ_MR_ERR_ARG;
    }
    if (!mat) {
        return NJ_MR_ERR_ARG;
    }
    if (flags & NJ_MR_TEXTURED) {
        if (!mat->texture || mat->texture_len <= 0) {
            return NJ_MR_ERR_ARG;
        }
        rc = nj_mascot_texture_view(mat->texture, mat->texture_len, &tex);
        if (rc != NJ_MR_OK) {
            return rc;
        }
        if ((tex.flags & NJ_MR_TEX_MODEL) == 0) {
            return NJ_MR_ERR_ARG; /* an environment map is not a model texture */
        }
        ptex = &tex;
    }
    if (flags & NJ_MR_ENVMAP) {
        if (!mat->env || mat->env_len <= 0) {
            return NJ_MR_ERR_ARG;
        }
        rc = nj_mascot_texture_view(mat->env, mat->env_len, &env);
        if (rc != NJ_MR_OK) {
            return rc;
        }
        if ((env.flags & NJ_MR_TEX_ENVMAP) == 0) {
            return NJ_MR_ERR_ARG; /* a model texture is not an environment map */
        }
        penv = &env;
    }
    nj_mascot_raster_triangle(dst, clip_x1, clip_y1, clip_x2, clip_y2,
                              verts, color, ptex, penv, flags, blend, toon);
    return NJ_MR_OK;
}
