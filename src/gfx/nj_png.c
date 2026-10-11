/*
 * nano_j2me - nj_png.c
 *
 * From-scratch PNG (ISO/IEC 15948) decoder for javax.microedition.lcdui.Image.
 * Everything in nj_png.h lives here; DEFLATE comes from nj_inflate.c.
 *
 * What is supported
 * -----------------
 *   colour types 0 (grey), 2 (truecolour), 3 (palette), 4 (grey+alpha),
 *   6 (truecolour+alpha), bit depths 1/2/4/8/16 (1/2/4 only where the spec
 *   allows them), non-interlaced and Adam7 interlaced images, tRNS in all
 *   three forms, and any number of IDAT chunks.
 *
 * What is deliberately ignored
 * ----------------------------
 *   Every ancillary chunk (gAMA, sRGB, pHYs, tEXt, iCCP, bKGD, sBIT, ...) is
 *   skipped after its CRC has been verified - refusing them would reject real
 *   game assets for no reason.
 *
 * Safety
 * ------
 *   This parser eats untrusted bytes out of game JARs:
 *     - every read is bounds checked against both the chunk and the file,
 *     - every chunk CRC is verified with nj_crc32(),
 *     - all size arithmetic is done in 64 bits before anything is multiplied
 *       or allocated, and the dimensions are capped at NJ_PNG_MAX_DIM,
 *     - unfiltering never writes outside the output image or the private
 *       scanline buffer, and both are sized from validated numbers,
 *     - every intermediate buffer is released on every error path, and all
 *       allocations go through the caller's allocator.
 *
 * Output is always NJ_PIXFMT_ARGB8888, stride == width, 0xAARRGGBB per pixel.
 */

#include "nj2me/nj_png.h"

#include "nj2me/nj_inflate.h"

#include <string.h>   /* memcmp / memcpy / memset only */

#if NJ_USE_LIBC
#include <stdlib.h>   /* malloc / free when the caller did not supply any */
#endif

/* ------------------------------------------------------------------ */
/* Allocator plumbing (compiled even when PNG support is off, because      */
/* nj_image_load/nj_image_free/nj_image_create still allocate)            */
/* ------------------------------------------------------------------ */

typedef struct png_alloc {
    void *(*alloc)(void *, nj_size);
    void  (*dealloc)(void *, void *);
    void  *ud;
} png_alloc;

static void png_alloc_init(png_alloc *a, void *(*alloc)(void *, nj_size),
                           void *ud, void (*dealloc)(void *, void *))
{
    a->alloc = alloc;
    a->dealloc = dealloc;
    a->ud = ud;
}

static void *png_mem_alloc(png_alloc *a, nj_size n)
{
    if (n == 0u) {
        return NULL;
    }
    if (a->alloc != NULL) {
        return a->alloc(a->ud, n);
    }
#if NJ_USE_LIBC
    return malloc((size_t)n);
#else
    return NULL;
#endif
}

static void png_mem_free(png_alloc *a, void *p)
{
    if (p == NULL) {
        return;
    }
    if (a->dealloc != NULL) {
        a->dealloc(a->ud, p);
        return;
    }
#if NJ_USE_LIBC
    free(p);
#else
    (void)a;
#endif
}

/* ------------------------------------------------------------------ */
/* Limits visible even when PNG support is compiled out                */
/* ------------------------------------------------------------------ */

/* One dimension cap, as requested by the Image API contract. */
#define NJ_PNG_MAX_DIM 4096
#define NJ_PNG_SIG_LEN 8

#if NJ_ENABLE_PNG

/* ------------------------------------------------------------------ */
/* PNG-only limits                                                     */
/* ------------------------------------------------------------------ */

/* Worst case inflate target: 16-bit RGBA scanlines plus the filter byte. */
#define NJ_PNG_MAX_RAW \
    ((nj_u8)NJ_PNG_MAX_DIM * (((nj_u8)NJ_PNG_MAX_DIM * 8u) + 1u))

/* Initial inflate guess: 8x the compressed size plus a floor, clamped to the
 * exact size the image needs.  A stream that expands further (solid colour
 * images do) reports NJ_ERR_OOM and is retried with the exact requirement, so
 * the common case costs one inflate and the pathological one still works. */
#define NJ_PNG_GUESS_SLACK 8u
#define NJ_PNG_GUESS_FLOOR 65536u

/* Adam7: x origin, y origin, x step, y step. */
static const nj_u1 s_adam7[7][4] = {
    { 0, 0, 8, 8 },
    { 4, 0, 8, 8 },
    { 0, 4, 4, 8 },
    { 2, 0, 4, 4 },
    { 0, 2, 2, 4 },
    { 1, 0, 2, 2 },
    { 0, 1, 1, 2 }
};

/* ------------------------------------------------------------------ */
/* Byte helpers                                                        */
/* ------------------------------------------------------------------ */

static nj_u4 be32(const nj_u1 *p)
{
    return ((nj_u4)p[0] << 24) | ((nj_u4)p[1] << 16) |
           ((nj_u4)p[2] << 8) | (nj_u4)p[3];
}

static nj_u4 be16(const nj_u1 *p)
{
    return ((nj_u4)p[0] << 8) | (nj_u4)p[1];
}

/* ------------------------------------------------------------------ */
/* Decoder state                                                       */
/* ------------------------------------------------------------------ */

typedef struct png_info {
    nj_u4 width;
    nj_u4 height;
    int   bit_depth;
    int   colour_type;
    int   interlace;
    int   channels;
    int   bpp_bits;      /* channels * bit_depth                        */
    int   filter_bpp;    /* max(1, bpp_bits / 8) - the Paeth step       */
    nj_u4 palette_entries;
    const nj_u1 *plte;   /* points into the caller's buffer (or NULL)   */
    const nj_u1 *trns;   /* points into the caller's buffer (or NULL)   */
    nj_u4 trns_len;
} png_info;

/* True when the chunk type is critical (bit 5 of the first byte clear). */
static int chunk_is_critical(const nj_u1 *type)
{
    return (type[0] & 0x20u) == 0u;
}

static int type_is(const nj_u1 *type, const char *name)
{
    return type[0] == (nj_u1)name[0] && type[1] == (nj_u1)name[1] &&
           type[2] == (nj_u1)name[2] && type[3] == (nj_u1)name[3];
}

/* ------------------------------------------------------------------ */
/* IHDR                                                                */
/* ------------------------------------------------------------------ */

static int channels_for(int colour_type)
{
    switch (colour_type) {
    case 0: return 1;   /* grey            */
    case 2: return 3;   /* truecolour      */
    case 3: return 1;   /* palette index   */
    case 4: return 2;   /* grey + alpha    */
    case 6: return 4;   /* truecolour + a  */
    default: return 0;
    }
}

static const char *ihdr_check(const nj_u1 *p, nj_u4 len, png_info *info)
{
    nj_u4 w;
    nj_u4 h;
    int depth;
    int colour;
    int interlace;

    if (len != 13u) {
        return "IHDR chunk has the wrong length";
    }
    w = be32(p);
    h = be32(p + 4);
    depth = p[8];
    colour = p[9];

    if (p[10] != 0u) {
        return "unsupported PNG compression method";
    }
    if (p[11] != 0u) {
        return "unsupported PNG filter method";
    }
    interlace = p[12];
    if (interlace != 0 && interlace != 1) {
        return "unsupported PNG interlace method";
    }
    if (w == 0u || h == 0u) {
        return "PNG width and height must be non-zero";
    }
    if (w > (nj_u4)NJ_PNG_MAX_DIM || h > (nj_u4)NJ_PNG_MAX_DIM) {
        return "PNG image is larger than 4096x4096";
    }

    switch (colour) {
    case 0:
        if (depth != 1 && depth != 2 && depth != 4 && depth != 8 &&
            depth != 16) {
            return "illegal bit depth for greyscale PNG";
        }
        break;
    case 2:
    case 4:
    case 6:
        if (depth != 8 && depth != 16) {
            return "illegal bit depth for colour PNG";
        }
        break;
    case 3:
        if (depth != 1 && depth != 2 && depth != 4 && depth != 8) {
            return "illegal bit depth for palette PNG";
        }
        break;
    default:
        return "illegal PNG colour type";
    }

    info->width = w;
    info->height = h;
    info->bit_depth = depth;
    info->colour_type = colour;
    info->interlace = interlace;
    info->channels = channels_for(colour);
    info->bpp_bits = info->channels * depth;
    info->filter_bpp = (info->bpp_bits >= 8) ? (info->bpp_bits / 8) : 1;
    return NULL;
}

static const char *plte_check(const nj_u1 *p, nj_u4 len, png_info *info)
{
    if (len == 0u || (len % 3u) != 0u) {
        return "PLTE chunk length must be a non-zero multiple of 3";
    }
    if (len > 3u * 256u) {
        return "PLTE chunk has more than 256 entries";
    }
    if (info->colour_type == 0 || info->colour_type == 4) {
        /* The spec forbids a palette for greyscale images; ignoring it keeps
         * us reading files that some encoders emit anyway. */
        return NULL;
    }
    info->plte = p;
    info->palette_entries = len / 3u;
    return NULL;
}

static const char *trns_check(const nj_u1 *p, nj_u4 len, const png_info *info)
{
    switch (info->colour_type) {
    case 0:
        if (len != 2u) {
            return "tRNS chunk for a greyscale PNG must be 2 bytes";
        }
        break;
    case 2:
        if (len != 6u) {
            return "tRNS chunk for a truecolour PNG must be 6 bytes";
        }
        break;
    case 3:
        if (info->plte == NULL) {
            return "tRNS chunk before PLTE in a palette PNG";
        }
        if (len > info->palette_entries) {
            return "tRNS chunk has more entries than the palette";
        }
        if (len == 0u) {
            return "tRNS chunk for a palette PNG must not be empty";
        }
        break;
    default:
        return "tRNS chunk is not allowed for this PNG colour type";
    }
    (void)p;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Chunk walk                                                          */
/* ------------------------------------------------------------------ */

/*
 * Walk every chunk.  When `idat` is non-NULL the IDAT payloads are copied
 * into it in order and `*idat_len` receives the total; otherwise the total is
 * only measured.  `verify_crc` is turned off for the second (copying) walk:
 * the input buffer is const and both walks see identical bytes.
 *
 * Returns NULL on success, a static message on failure.
 */
static const char *png_scan(const nj_u1 *d, nj_u4 size, png_info *info,
                            nj_u1 *idat, nj_u4 idat_cap, nj_u4 *idat_len,
                            int verify_crc)
{
    nj_u4 pos = NJ_PNG_SIG_LEN;
    nj_u4 total = 0;
    int seen_ihdr = 0;
    int seen_iend = 0;
    int seen_idat = 0;

    while ((nj_u8)pos + 12u <= (nj_u8)size) {
        nj_u4 len = be32(d + pos);
        const nj_u1 *type = d + pos + 4;
        const nj_u1 *body = d + pos + 8;

        if (len > 0x7FFFFFFFu) {
            return "PNG chunk length is out of range";
        }
        /* 64-bit check first: pos + 12 + len must not wrap. */
        if ((nj_u8)pos + 12u + (nj_u8)len > (nj_u8)size) {
            return "PNG chunk runs past the end of the file";
        }

        if (verify_crc) {
            nj_u4 want = nj_crc32(type, len + 4u, 0u);
            nj_u4 have = be32(body + len);
            if (want != have) {
                return "PNG chunk CRC mismatch";
            }
        }

        if (!seen_ihdr) {
            if (!type_is(type, "IHDR")) {
                return "PNG does not start with an IHDR chunk";
            }
        }

        if (type_is(type, "IHDR")) {
            const char *e;
            if (seen_ihdr) {
                return "duplicate IHDR chunk";
            }
            e = ihdr_check(body, len, info);
            if (e != NULL) {
                return e;
            }
            seen_ihdr = 1;
        } else if (type_is(type, "PLTE")) {
            const char *e = plte_check(body, len, info);
            if (e != NULL) {
                return e;
            }
        } else if (type_is(type, "tRNS")) {
            const char *e = trns_check(body, len, info);
            if (e != NULL) {
                return e;
            }
            info->trns = body;
            info->trns_len = len;
        } else if (type_is(type, "IDAT")) {
            if ((nj_u8)total + (nj_u8)len > 0xFFFFFFFFu) {
                return "PNG image data is too large";
            }
            if (idat != NULL) {
                if (total + len > idat_cap) {
                    return "internal error: IDAT buffer too small";
                }
                memcpy(idat + total, body, (size_t)len);
            }
            total += len;
            seen_idat = 1;
        } else if (type_is(type, "IEND")) {
            if (len != 0u) {
                return "IEND chunk must be empty";
            }
            seen_iend = 1;
            pos += 12u + len;
            break;
        } else if (chunk_is_critical(type)) {
            return "unsupported critical PNG chunk";
        }
        /* everything else is ancillary: verified and skipped */

        pos += 12u + len;
    }

    if (!seen_ihdr) {
        return "PNG is missing its IHDR chunk";
    }
    if (!seen_idat || total == 0u) {
        return "PNG has no image data";
    }
    if (!seen_iend) {
        return "PNG is missing its IEND chunk";
    }
    if (info->colour_type == 3 && info->plte == NULL) {
        return "palette PNG is missing its PLTE chunk";
    }

    *idat_len = total;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */

/* Pixels of `pass` (-1 means "not interlaced") along one axis. */
static nj_u4 pass_extent(nj_u4 total, nj_u4 origin, nj_u4 step)
{
    if (total <= origin) {
        return 0u;
    }
    return (total - origin + step - 1u) / step;
}

/*
 * Exact number of inflated bytes the image needs: for every Adam7 pass (or
 * the single non-interlaced pass) one filter byte plus ceil(pw*bpp/8) bytes
 * per row.  All of it fits comfortably in 64 bits given the 4096 dimension
 * cap; the caller checks it against the platform limits.
 */
static nj_u8 raw_expected(const png_info *info)
{
    nj_u8 total = 0;
    int passes = info->interlace ? 7 : 1;
    int i;

    for (i = 0; i < passes; i++) {
        nj_u4 pw;
        nj_u4 ph;
        nj_u8 row_bytes;

        if (info->interlace) {
            pw = pass_extent(info->width, s_adam7[i][0], s_adam7[i][2]);
            ph = pass_extent(info->height, s_adam7[i][1], s_adam7[i][3]);
        } else {
            pw = info->width;
            ph = info->height;
        }
        if (pw == 0u || ph == 0u) {
            continue;   /* empty passes occupy no bytes at all */
        }
        row_bytes = ((nj_u8)pw * (nj_u8)info->bpp_bits + 7u) / 8u;
        total += (nj_u8)ph * (row_bytes + 1u);
    }
    return total;
}

/* ------------------------------------------------------------------ */
/* Scanline reconstruction                                             */
/* ------------------------------------------------------------------ */

static int paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = (p > a) ? (p - a) : (a - p);
    int pb = (p > b) ? (p - b) : (b - p);
    int pc = (p > c) ? (p - c) : (c - p);

    if (pa <= pb && pa <= pc) {
        return a;
    }
    if (pb <= pc) {
        return b;
    }
    return c;
}

/*
 * Unfilter one scanline in place.  `bpp` is the byte distance used by the
 * Sub/Average/Paeth predictors: max(1, bits_per_pixel / 8).
 */
static void unfilter_row(nj_u1 *row, const nj_u1 *prev, nj_u4 len, int bpp,
                         int filter)
{
    nj_u4 i;

    switch (filter) {
    case 0:   /* None */
        break;
    case 1:   /* Sub */
        for (i = (nj_u4)bpp; i < len; i++) {
            row[i] = (nj_u1)(row[i] + row[i - bpp]);
        }
        break;
    case 2:   /* Up */
        if (prev != NULL) {
            for (i = 0; i < len; i++) {
                row[i] = (nj_u1)(row[i] + prev[i]);
            }
        }
        break;
    case 3:   /* Average */
        for (i = 0; i < len; i++) {
            int left = (i >= (nj_u4)bpp) ? row[i - bpp] : 0;
            int up = (prev != NULL) ? prev[i] : 0;
            row[i] = (nj_u1)(row[i] + (nj_u1)((left + up) >> 1));
        }
        break;
    case 4:   /* Paeth */
        for (i = 0; i < len; i++) {
            int left = (i >= (nj_u4)bpp) ? row[i - bpp] : 0;
            int up = (prev != NULL) ? prev[i] : 0;
            int ul = (prev != NULL && i >= (nj_u4)bpp) ? prev[i - bpp] : 0;
            row[i] = (nj_u1)(row[i] + (nj_u1)paeth(left, up, ul));
        }
        break;
    default:
        break;    /* caller validated the filter byte */
    }
}

/* ------------------------------------------------------------------ */
/* Sample extraction                                                   */
/* ------------------------------------------------------------------ */

/* One sample of a scanline; sub-byte depths are packed MSB first. */
static nj_u4 get_sample(const nj_u1 *row, nj_u4 index, int depth)
{
    switch (depth) {
    case 1:
        return ((nj_u4)row[index >> 3] >> (7u - (index & 7u))) & 0x1u;
    case 2:
        return ((nj_u4)row[index >> 2] >> (6u - 2u * (index & 3u))) & 0x3u;
    case 4:
        return ((nj_u4)row[index >> 1] >> (4u - 4u * (index & 1u))) & 0xFu;
    case 16:
        return ((nj_u4)row[index * 2u] << 8) | (nj_u4)row[index * 2u + 1u];
    default:   /* 8 */
        return (nj_u4)row[index];
    }
}

/* Widen a sample of `depth` bits to the 0..255 range. */
static nj_u4 sample_to_8(nj_u4 v, int depth)
{
    switch (depth) {
    case 1:  return v * 255u;
    case 2:  return v * 85u;
    case 4:  return v * 17u;
    case 16: return v >> 8;
    default: return v;
    }
}

/* ------------------------------------------------------------------ */
/* Pixel assembly                                                      */
/* ------------------------------------------------------------------ */

/*
 * Convert one reconstructed scanline of one pass into ARGB8888.
 *
 * `dst` is the first output pixel, `dst_step` the pixel stride (1 for a
 * non-interlaced image, 2/4/8 for the coarser Adam7 passes) and `count` the
 * number of pixels this pass contributes to the row.
 */
static void emit_row(const png_info *info, const nj_u1 *row, nj_u4 count,
                     nj_u4 *dst, nj_u4 dst_step, int *has_alpha)
{
    nj_u4 i;
    int depth = info->bit_depth;
    nj_u4 trns_grey = 0;
    nj_u4 trns_r = 0;
    nj_u4 trns_g = 0;
    nj_u4 trns_b = 0;

    if (info->trns != NULL) {
        if (info->colour_type == 0) {
            trns_grey = be16(info->trns);
        } else if (info->colour_type == 2) {
            trns_r = be16(info->trns);
            trns_g = be16(info->trns + 2);
            trns_b = be16(info->trns + 4);
        }
    }

    for (i = 0; i < count; i++) {
        nj_u4 r = 0;
        nj_u4 g = 0;
        nj_u4 b = 0;
        nj_u4 a = 255u;

        switch (info->colour_type) {
        case 0: {                                  /* greyscale */
            nj_u4 v = get_sample(row, i, depth);
            if (info->trns != NULL && v == trns_grey) {
                a = 0u;
            }
            v = sample_to_8(v, depth);
            r = g = b = v;
            break;
        }
        case 2: {                                  /* truecolour */
            nj_u4 rv = get_sample(row, i * 3u, depth);
            nj_u4 gv = get_sample(row, i * 3u + 1u, depth);
            nj_u4 bv = get_sample(row, i * 3u + 2u, depth);
            if (info->trns != NULL && rv == trns_r && gv == trns_g &&
                bv == trns_b) {
                a = 0u;
            }
            r = sample_to_8(rv, depth);
            g = sample_to_8(gv, depth);
            b = sample_to_8(bv, depth);
            break;
        }
        case 3: {                                  /* palette */
            nj_u4 idx = get_sample(row, i, depth);
            if (info->plte == NULL || idx >= info->palette_entries) {
                r = g = b = 0u;                    /* matches libpng */
                a = 255u;
            } else {
                r = info->plte[idx * 3u];
                g = info->plte[idx * 3u + 1u];
                b = info->plte[idx * 3u + 2u];
            }
            if (info->trns != NULL && idx < info->trns_len) {
                a = info->trns[idx];
            }
            break;
        }
        case 4: {                                  /* grey + alpha */
            nj_u4 v = get_sample(row, i * 2u, depth);
            nj_u4 av = get_sample(row, i * 2u + 1u, depth);
            v = sample_to_8(v, depth);
            r = g = b = v;
            a = (depth == 16) ? (av >> 8) : av;
            break;
        }
        default: {                                 /* 6: RGBA */
            r = sample_to_8(get_sample(row, i * 4u, depth), depth);
            g = sample_to_8(get_sample(row, i * 4u + 1u, depth), depth);
            b = sample_to_8(get_sample(row, i * 4u + 2u, depth), depth);
            a = sample_to_8(get_sample(row, i * 4u + 3u, depth), depth);
            break;
        }
        }

        if (a != 255u) {
            *has_alpha = 1;
        }
        dst[i * dst_step] = (a << 24) | (r << 16) | (g << 8) | b;
    }
}

/*
 * Walk every pass (or the single non-interlaced one), unfilter each scanline
 * in place and scatter it into the output image.
 */
static const char *png_reconstruct(const png_info *info, nj_u1 *raw,
                                   nj_u4 raw_len, nj_image *img)
{
    nj_u1 *p = raw;
    const nj_u1 *end = raw + raw_len;
    int passes = info->interlace ? 7 : 1;
    int has_alpha = 0;
    int pass;

    for (pass = 0; pass < passes; pass++) {
        nj_u4 pw;
        nj_u4 ph;
        nj_u4 row_bytes;
        nj_u4 y;
        nj_u1 *prev = NULL;
        nj_u4 x0 = 0;
        nj_u4 y0 = 0;
        nj_u4 dx = 1;
        nj_u4 dy = 1;

        if (info->interlace) {
            x0 = s_adam7[pass][0];
            y0 = s_adam7[pass][1];
            dx = s_adam7[pass][2];
            dy = s_adam7[pass][3];
        }
        pw = pass_extent(info->width, x0, dx);
        ph = pass_extent(info->height, y0, dy);
        if (pw == 0u || ph == 0u) {
            continue;
        }
        row_bytes = (pw * (nj_u4)info->bpp_bits + 7u) / 8u;

        for (y = 0u; y < ph; y++) {
            nj_u1 *row;
            int filter;
            nj_u4 *dst;

            /* One filter byte plus the row itself, bounds checked. */
            if (p + 1u + row_bytes > end) {
                return "PNG image data ended early";
            }
            filter = *p;
            if (filter > 4) {
                return "unknown PNG scanline filter type";
            }
            row = p + 1;
            unfilter_row(row, prev, row_bytes, info->filter_bpp, filter);
            prev = row;

            dst = img->pixels + (nj_size)(y0 + y * dy) * (nj_size)info->width +
                  (nj_size)x0;
            emit_row(info, row, pw, dst, dx, &has_alpha);

            p += 1u + row_bytes;
        }
    }

    /* A source alpha channel or a tRNS chunk makes the image transparent even
     * when no pixel happens to use it - the Sprite code keys off this. */
    if (has_alpha || info->colour_type == 4 || info->colour_type == 6 ||
        info->trns != NULL) {
        img->has_alpha = 1;
    }
    return NULL;
}

#endif /* NJ_ENABLE_PNG */

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

nj_image *nj_png_decode(const void *data, nj_u4 size,
                        void *(*alloc)(void *, nj_size), void *ud,
                        void  (*dealloc)(void *, void *),
                        const char **err)
{
#if !NJ_ENABLE_PNG
    if (err != NULL) {
        *err = "PNG support not built";
    }
    (void)data; (void)size; (void)alloc; (void)ud; (void)dealloc;
    return NULL;
#else
    static const nj_u1 sig[NJ_PNG_SIG_LEN] = {
        0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au
    };
    png_alloc mem;
    png_info info;
    nj_u1 *idat = NULL;
    nj_u1 *raw = NULL;
    nj_image *img = NULL;
    nj_u4 idat_len = 0;
    nj_u4 raw_len = 0;
    nj_u8 expected;
    const char *e;

    if (err != NULL) {
        *err = NULL;
    }
    png_alloc_init(&mem, alloc, ud, dealloc);

    if (data == NULL || size < NJ_PNG_SIG_LEN) {
        if (err != NULL) {
            *err = "truncated PNG: no signature";
        }
        return NULL;
    }
    if (memcmp(data, sig, NJ_PNG_SIG_LEN) != 0) {
        if (err != NULL) {
            *err = "not a PNG file (bad signature)";
        }
        return NULL;
    }

    memset(&info, 0, sizeof(info));

    /* Pass 1: validate every chunk and measure the IDAT payload. */
    e = png_scan((const nj_u1 *)data, size, &info, NULL, 0u, &idat_len, 1);
    if (e != NULL) {
        if (err != NULL) {
            *err = e;
        }
        return NULL;
    }

    expected = raw_expected(&info);
    if (expected == 0u || expected > (nj_u8)NJ_PNG_MAX_RAW ||
        expected > 0xFFFFFFFFu) {
        if (err != NULL) {
            *err = "PNG image data size is out of range";
        }
        return NULL;
    }
    /*
     * A DEFLATE stream cannot expand by more than 1032:1 (measured: a uniform
     * 4096x4096 RGBA image compresses 1028.7:1).  Refusing impossible ratios
     * here means a 20-byte file can never talk us into a 128 MB allocation
     * before the stream has proven itself.
     */
    if (expected > (nj_u8)idat_len * 1100u + 65536u) {
        if (err != NULL) {
            *err = "PNG image data is too small for its header";
        }
        return NULL;
    }

    idat = (nj_u1 *)png_mem_alloc(&mem, idat_len);
    if (idat == NULL) {
        if (err != NULL) {
            *err = "out of memory for PNG image data";
        }
        return NULL;
    }
    /* Pass 2: copy the IDAT payloads out (CRCs were checked in pass 1). */
    {
        nj_u4 got = 0;
        e = png_scan((const nj_u1 *)data, size, &info, idat, idat_len, &got, 0);
        if (e != NULL || got != idat_len) {
            png_mem_free(&mem, idat);
            if (err != NULL) {
                *err = (e != NULL) ? e : "PNG IDAT chunks changed size";
            }
            return NULL;
        }
    }

    /* Inflate, growing to the exact requirement when the guess was short. */
    {
        nj_u8 guess = (nj_u8)idat_len * NJ_PNG_GUESS_SLACK +
                      (nj_u8)NJ_PNG_GUESS_FLOOR;
        nj_u4 cap = (guess < expected) ? (nj_u4)guess : (nj_u4)expected;
        int attempt;

        for (attempt = 0; attempt < 4; attempt++) {
            nj_u4 len = cap;
            nj_status st;

            raw = (nj_u1 *)png_mem_alloc(&mem, cap);
            if (raw == NULL) {
                png_mem_free(&mem, idat);
                if (err != NULL) {
                    *err = "out of memory for PNG scanlines";
                }
                return NULL;
            }
            st = nj_inflate_zlib(idat, idat_len, raw, &len);
            if (st == NJ_OK) {
                raw_len = len;
                break;
            }
            png_mem_free(&mem, raw);
            raw = NULL;
            if (st != NJ_ERR_OOM) {
                png_mem_free(&mem, idat);
                if (err != NULL) {
                    *err = "corrupt PNG image data (bad DEFLATE stream)";
                }
                return NULL;
            }
            /* `len` now holds the exact number of bytes the stream holds. */
            if ((nj_u8)len <= (nj_u8)cap || (nj_u8)len > expected) {
                png_mem_free(&mem, idat);
                if (err != NULL) {
                    *err = "PNG image data does not match its header";
                }
                return NULL;
            }
            cap = len;
        }
        png_mem_free(&mem, idat);
        idat = NULL;
        if (raw == NULL) {
            if (err != NULL) {
                *err = "PNG image data could not be decompressed";
            }
            return NULL;
        }
        if ((nj_u8)raw_len < expected) {
            png_mem_free(&mem, raw);
            if (err != NULL) {
                *err = "PNG image data is truncated";
            }
            return NULL;
        }
    }

    /* Output image: ARGB8888, stride == width. */
    img = (nj_image *)png_mem_alloc(&mem, (nj_size)sizeof(nj_image));
    if (img == NULL) {
        png_mem_free(&mem, raw);
        if (err != NULL) {
            *err = "out of memory for PNG image";
        }
        return NULL;
    }
    img->width = (int)info.width;
    img->height = (int)info.height;
    img->format = NJ_PIXFMT_ARGB8888;
    img->has_alpha = 0;
    img->pixels = (nj_u4 *)png_mem_alloc(
        &mem, (nj_size)info.width * (nj_size)info.height * (nj_size)4u);
    if (img->pixels == NULL) {
        png_mem_free(&mem, img);
        png_mem_free(&mem, raw);
        if (err != NULL) {
            *err = "out of memory for PNG pixels";
        }
        return NULL;
    }

    e = png_reconstruct(&info, raw, raw_len, img);
    png_mem_free(&mem, raw);
    if (e != NULL) {
        png_mem_free(&mem, img->pixels);
        png_mem_free(&mem, img);
        if (err != NULL) {
            *err = e;
        }
        return NULL;
    }
    return img;
#endif /* NJ_ENABLE_PNG */
}

void nj_image_free(nj_image *img, void (*dealloc)(void *, void *), void *ud)
{
    png_alloc mem;

    if (img == NULL) {
        return;
    }
    png_alloc_init(&mem, NULL, ud, dealloc);
    png_mem_free(&mem, img->pixels);
    img->pixels = NULL;
    png_mem_free(&mem, img);
}

nj_image *nj_image_create(int w, int h,
                          void *(*alloc)(void *, nj_size), void *ud)
{
    png_alloc mem;
    nj_image *img;
    nj_size bytes;

    if (w <= 0 || h <= 0 || w > NJ_PNG_MAX_DIM || h > NJ_PNG_MAX_DIM) {
        return NULL;
    }
    png_alloc_init(&mem, alloc, ud, NULL);

    img = (nj_image *)png_mem_alloc(&mem, (nj_size)sizeof(nj_image));
    if (img == NULL) {
        return NULL;
    }
    bytes = (nj_size)w * (nj_size)h * (nj_size)4u;
    img->pixels = (nj_u4 *)png_mem_alloc(&mem, bytes);
    if (img->pixels == NULL) {
        png_mem_free(&mem, img);
        return NULL;
    }
    memset(img->pixels, 0, (size_t)bytes);   /* transparent black */
    img->width = w;
    img->height = h;
    img->format = NJ_PIXFMT_ARGB8888;
    img->has_alpha = 0;
    return img;
}

nj_image *nj_image_load(const void *data, nj_u4 size,
                        void *(*alloc)(void *, nj_size), void *ud,
                        void  (*dealloc)(void *, void *),
                        const char **err)
{
    const nj_u1 *p = (const nj_u1 *)data;

    if (err != NULL) {
        *err = NULL;
    }
    if (p == NULL || size < 4u) {
        if (err != NULL) {
            *err = "truncated image data";
        }
        return NULL;
    }

    /* PNG signature. */
    if (size >= NJ_PNG_SIG_LEN && p[0] == 0x89u && p[1] == 0x50u &&
        p[2] == 0x4Eu && p[3] == 0x47u) {
        return nj_png_decode(data, size, alloc, ud, dealloc, err);
    }

    /* JPEG SOI marker. */
    if (p[0] == 0xFFu && p[1] == 0xD8u && p[2] == 0xFFu) {
        /* NJ_ENABLE_JPEG gates a future decoder; until one exists the magic
         * is recognised and reported instead of being called "unknown". */
        if (err != NULL) {
            *err = "JPEG support not built";
        }
        return NULL;
    }

    if (err != NULL) {
        *err = "unrecognised image format";
    }
    return NULL;
}
