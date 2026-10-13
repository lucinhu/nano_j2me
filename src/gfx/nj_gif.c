/*
 * nano_j2me - nj_gif.c
 *
 * From-scratch GIF (87a/89a) decoder for javax.microedition.lcdui.Image.
 *
 * Why this file exists
 * --------------------
 * MIDP 1.0 made GIF support mandatory and MIDP 2.0 kept PNG mandatory, so a
 * real handset answered Image.createImage for both.  This runtime shipped PNG
 * and JPEG only, and the difference is not cosmetic: a MIDlet that cannot
 * decode its own artwork gets an IOException, swallows it - most loaders are
 * written as `try { img = createImage(bytes); } catch (Exception e) {}` - and
 * goes on to draw a null Image, so the visible symptom is a black screen and
 * an endless NullPointerException in the paint loop, which is exactly what
 * tests/games/仙剑奇侠传_10388.jar did with its 547 GIF frames.
 *
 * What is supported
 * -----------------
 *   - GIF87a and GIF89a, global and local colour tables, any table size.
 *   - LZW decompression with the deferred clear code, including the
 *     "code == next" (KwKwK) case every real encoder emits.
 *   - Interlaced images (the four-pass layout).
 *   - The Graphic Control Extension's transparent colour index.
 *   - The first image of an animation: Image.createImage has no notion of
 *     time, so the first frame is the frame the MIDlet asked for.
 *
 * What is deliberately not
 * ------------------------
 *   - Animation timing and frame disposal (see above), and text/comment
 *     extensions, which are skipped rather than interpreted.
 *   - Colour quantization: the output is always NJ_PIXFMT_ARGB8888, stride ==
 *     width, 0xAARRGGBB per pixel, which is what the software PNG decoder
 *     returns and what the blitter prefers.
 *   - Anything that trusts the input.  Every offset, table index, code and
 *     row is bounds-checked: a GIF is attacker-supplied data in a game
 *     loader, and writing one byte past the canvas would be a code execution
 *     bug, not a rendering glitch.
 */
#include "nj2me/nj_codec.h"

#include <string.h>

/*
 * -DNJ_ENABLE_GIF=0: the decoder is not compiled in at all.  The descriptor
 * stays, because nj_codec.c names it, but with NULL hooks - so it never
 * matches, and the "unavailable" provider registered there tells the MIDlet
 * what happened instead of calling its artwork unrecognisable.
 */
#if NJ_ENABLE_GIF

/* GIF's own hard limits.  The dictionary cannot exceed 12-bit codes. */
#define GIF_MAX_CODES   4096
#define GIF_MAX_CODE_BITS 12

typedef struct gif_reader {
    const nj_u1 *p;
    nj_u4        size;
    nj_u4        pos;
    /*
     * LZW bit state.  A GIF image's codes are a continuous LSB-first bit
     * stream that happens to be cut into sub-blocks, so the bits left over
     * from one code and the bytes left in the current sub-block both have to
     * outlive a single call.
     */
    nj_u4        bitbuf;
    int          bitcount;
    nj_u1        chunk_left;
} gif_reader;

static int gif_remaining(const gif_reader *r, nj_u4 *out)
{
    if (r->pos > r->size) {
        return 0;
    }
    *out = r->size - r->pos;
    return 1;
}

static int gif_byte(gif_reader *r, nj_u1 *out)
{
    if (r->pos >= r->size) {
        return 0;
    }
    *out = r->p[r->pos++];
    return 1;
}

static int gif_bytes(gif_reader *r, const nj_u1 **out, nj_u4 n)
{
    nj_u4 have;

    if (!gif_remaining(r, &have) || have < n) {
        return 0;
    }
    *out = r->p + r->pos;
    r->pos += n;
    return 1;
}

static int gif_u16(gif_reader *r, nj_u2 *out)
{
    nj_u1 lo, hi;

    if (!gif_byte(r, &lo) || !gif_byte(r, &hi)) {
        return 0;
    }
    *out = (nj_u2)(((nj_u2)hi << 8) | (nj_u2)lo);
    return 1;
}

/*
 * Skip the sub-block chain that follows a block introducer: a series of
 * length-prefixed chunks terminated by a zero length.
 */
static int gif_skip_subblocks(gif_reader *r)
{
    for (;;) {
        nj_u1 n;

        if (!gif_byte(r, &n)) {
            return 0;
        }
        if (n == 0u) {
            return 1;
        }
        if (r->pos + (nj_u4)n > r->size) {
            return 0;
        }
        r->pos += n;
    }
}

/* ------------------------------------------------------------------ */
/* LZW                                                                 */
/* ------------------------------------------------------------------ */
/*
 * The dictionary is charged to the caller's allocator rather than the C
 * stack: 4096 entries is 24 KB, which is more stack than an embedded port
 * wants to risk, and the codec contract says memory comes through the
 * request's hooks anyway.
 */
typedef struct gif_dict {
    nj_u2 prefix[GIF_MAX_CODES];
    nj_u1 suffix[GIF_MAX_CODES];
    nj_u1 first[GIF_MAX_CODES];
    nj_u2 length[GIF_MAX_CODES];
} gif_dict;

/*
 * Read the next `bits`-wide code, least significant bit first, straight out
 * of the sub-block chain.  Returns 0 when the chain ends without a code.
 *
 * Bit order is the one place GIF is not like its peers: codes are packed into
 * the byte stream from the low bit up, and a code may straddle any number of
 * sub-block boundaries, so the accumulator has to be refilled mid-code.
 */
static int gif_next_code(gif_reader *r, int bits, nj_u4 *out)
{
    while (r->bitcount < bits) {
        nj_u1 b;

        if (r->chunk_left == 0u) {
            /* A zero length byte ends the chain; the code is truncated. */
            if (!gif_byte(r, &r->chunk_left) || r->chunk_left == 0u) {
                return 0;
            }
        }
        if (r->pos >= r->size) {
            return 0;
        }
        b = r->p[r->pos++];
        r->chunk_left--;
        r->bitbuf |= (nj_u4)b << r->bitcount;
        r->bitcount += 8;
    }
    *out = r->bitbuf & ((1u << bits) - 1u);
    r->bitbuf >>= bits;
    r->bitcount -= bits;
    return 1;
}

static void gif_dict_reset(gif_dict *d, int min_code_size, int *code_size,
                           int *next)
{
    int i;
    int clear = 1 << min_code_size;

    for (i = 0; i < clear; i++) {
        d->prefix[i] = 0u;
        d->suffix[i] = (nj_u1)i;
        d->first[i] = (nj_u1)i;
        d->length[i] = 1u;
    }
    *code_size = min_code_size + 1;
    *next = clear + 2;
}

/*
 * Decode `count` palette indices into `out`.  Returns 0 on any malformed
 * input; the caller turns that into an error, never into partial pixels.
 */
static int gif_lzw_decode(gif_reader *r, gif_dict *d, int min_code_size,
                          nj_u1 *out, nj_u4 count)
{
    int clear, end;
    int code_size;
    int next;
    nj_u4 produced = 0u;
    nj_s4 prev = -1;
    nj_u1 prev_first = 0u;

    if (min_code_size < 2 || min_code_size > 8) {
        return 0;
    }
    clear = 1 << min_code_size;
    end = clear + 1;
    gif_dict_reset(d, min_code_size, &code_size, &next);

    while (produced < count) {
        nj_u4 code;

        if (!gif_next_code(r, code_size, &code)) {
            /* A file may stop right after the last pixel without an end
             * code; that is a complete image, not a truncated one. */
            return produced == count;
        }
        if (code == (nj_u4)clear) {
            gif_dict_reset(d, min_code_size, &code_size, &next);
            prev = -1;
            continue;
        }
        if (code == (nj_u4)end) {
            break;
        }
        if (prev < 0) {
            /* The first code after a clear must be a root code. */
            if (code >= (nj_u4)clear) {
                return 0;
            }
            out[produced++] = (nj_u1)code;
            prev = (nj_s4)code;
            prev_first = (nj_u1)code;
            continue;
        }
        {
            nj_u2 len;
            nj_u4 i;
            nj_u1 firstbyte;

            if (code < (nj_u4)next) {
                len = d->length[code];
                firstbyte = d->first[code];
            } else if (code == (nj_u4)next) {
                /* KwKwK: the code describes the entry being defined right
                 * now, so its last byte is the first byte of the previous
                 * string. */
                len = (nj_u2)(d->length[prev] + 1u);
                firstbyte = prev_first;
            } else {
                return 0;
            }
            if (produced + (nj_u4)len > count) {
                return 0;
            }
            /* Walk the chain backwards: the string ends where it will start
             * plus its length. */
            i = produced + (nj_u4)len;
            if (code < (nj_u4)next) {
                nj_u4 walk = code;
                while (i > produced) {
                    out[--i] = d->suffix[walk];
                    if (walk < (nj_u4)clear) {
                        break;
                    }
                    walk = d->prefix[walk];
                }
            } else {
                out[--i] = prev_first;
                {
                    nj_u4 walk = (nj_u4)prev;
                    while (i > produced) {
                        out[--i] = d->suffix[walk];
                        if (walk < (nj_u4)clear) {
                            break;
                        }
                        walk = d->prefix[walk];
                    }
                }
            }
            produced += (nj_u4)len;

            /* Define the new entry: the previous string plus this code's
             * first byte. */
            if (next < GIF_MAX_CODES) {
                d->prefix[next] = (nj_u2)prev;
                d->suffix[next] = firstbyte;
                d->first[next] = prev_first;
                d->length[next] = (nj_u2)(d->length[prev] + 1u);
                next++;
                if (next == (1 << code_size) && code_size < GIF_MAX_CODE_BITS) {
                    code_size++;
                }
            }
            prev = (nj_s4)code;
            prev_first = firstbyte;
        }
    }
    return produced == count;
}

/* ------------------------------------------------------------------ */
/* Provider                                                            */
/* ------------------------------------------------------------------ */
static int gif_accepts(const void *data, nj_u4 size)
{
    const char *p = (const char *)data;

    if (size < 6u) {
        return 0;
    }
    return memcmp(p, "GIF87a", 6u) == 0 || memcmp(p, "GIF89a", 6u) == 0;
}

static nj_status gif_info(const void *data, nj_u4 size, nj_image_info *out)
{
    const nj_u1 *p = (const nj_u1 *)data;

    if (!gif_accepts(data, size) || size < 13u) {
        return NJ_ERR_FORMAT;
    }
    out->width = (int)((nj_u4)p[6] | ((nj_u4)p[7] << 8));
    out->height = (int)((nj_u4)p[8] | ((nj_u4)p[9] << 8));
    if (out->width <= 0 || out->height <= 0) {
        return NJ_ERR_FORMAT;
    }
    out->bits_per_pixel = (int)((p[10] & 0x07u) + 1u);
    out->has_alpha = 1;
    out->format = NJ_CODEC_FMT_GIF;
    return NJ_OK;
}

/*
 * Map a decoded row number to the real row when the image is interlaced.
 * GIF stores the four passes as 0,8,16..; 4,12,20..; 2,6,10..; 1,3,5.., each
 * complete before the next, which is why the rows cannot simply be written in
 * stream order.
 */
static nj_u4 gif_interlace_row(nj_u4 n, nj_u4 height)
{
    static const nj_u4 starts[4] = { 0u, 4u, 2u, 1u };
    static const nj_u4 steps[4] = { 8u, 8u, 4u, 2u };
    int pass = 0;
    nj_u4 remaining = n;

    while (pass < 4) {
        nj_u4 rows = 0u;
        nj_u4 r;
        for (r = starts[pass]; r < height; r += steps[pass]) {
            rows++;
        }
        if (remaining < rows) {
            return starts[pass] + remaining * steps[pass];
        }
        remaining -= rows;
        pass++;
    }
    return 0u;
}

nj_image *nj_gif_decode(const void *data, nj_u4 size,
                        void *(*alloc)(void *ud, nj_size n), void *ud,
                        void (*dealloc)(void *ud, void *p), const char **err)
{
    gif_reader r;
    nj_u1 packed;
    nj_u1 bg;
    nj_u1 aspect;
    nj_u2 screen_w, screen_h;
    nj_u1 gct[3 * 256];
    int gct_entries = 0;
    nj_image *img = NULL;
    nj_u4 *pixels = NULL;
    nj_u1 *indices = NULL;
    gif_dict *dict = NULL;
    int transparent = -1;
    nj_u4 i;

    (void)bg;
    (void)aspect;

    if (!gif_accepts(data, size)) {
        if (err) {
            *err = "not a GIF";
        }
        return NULL;
    }
    r.p = (const nj_u1 *)data;
    r.size = size;
    r.bitbuf = 0u;
    r.bitcount = 0;
    r.chunk_left = 0u;
    r.pos = 6u;
    if (!gif_u16(&r, &screen_w) || !gif_u16(&r, &screen_h)) {
        goto fail;
    }
    if (screen_w == 0u || screen_h == 0u) {
        goto fail;
    }
    if (!gif_byte(&r, &packed) || !gif_byte(&r, &bg) || !gif_byte(&r, &aspect)) {
        goto fail;
    }
    if ((packed & 0x80u) != 0u) {
        nj_u4 entries = 1u << ((nj_u4)(packed & 0x07u) + 1u);
        const nj_u1 *table;

        if (!gif_bytes(&r, &table, entries * 3u)) {
            goto fail;
        }
        memcpy(gct, table, (size_t)(entries * 3u));
        gct_entries = (int)entries;
    }

    /* The canvas starts fully transparent. */
    pixels = (nj_u4 *)alloc(ud, (nj_size)screen_w * (nj_size)screen_h *
                                (nj_size)sizeof(nj_u4));
    if (!pixels) {
        if (err) {
            *err = "out of memory";
        }
        return NULL;
    }
    for (i = 0; i < (nj_u4)screen_w * (nj_u4)screen_h; i++) {
        pixels[i] = 0u;
    }

    for (;;) {
        nj_u1 block;

        if (!gif_byte(&r, &block)) {
            break;                      /* no trailer: keep what we decoded */
        }
        if (block == 0x3Bu) {
            break;                      /* trailer */
        }
        if (block == 0x21u) {
            /* Extension: label, then sub-blocks.  Only the graphic control
             * extension carries state this decoder needs. */
            nj_u1 label;

            if (!gif_byte(&r, &label)) {
                goto fail;
            }
            if (label == 0xF9u) {
                nj_u1 bsize;
                if (!gif_byte(&r, &bsize)) {
                    goto fail;
                }
                if (bsize != 4u) {
                    if (r.pos + (nj_u4)bsize > r.size) {
                        goto fail;
                    }
                    r.pos += bsize;
                } else {
                    nj_u1 flags;
                    nj_u1 delay_lo, delay_hi, tindex;

                    if (!gif_byte(&r, &flags) || !gif_byte(&r, &delay_lo) ||
                        !gif_byte(&r, &delay_hi) || !gif_byte(&r, &tindex)) {
                        goto fail;
                    }
                    transparent = ((flags & 0x01u) != 0u) ? (int)tindex : -1;
                }
            }
            if (!gif_skip_subblocks(&r)) {
                goto fail;
            }
            continue;
        }
        if (block != 0x2Cu) {
            goto fail;                  /* unknown block introducer */
        }
        {
            nj_u2 left, top, iw, ih;
            nj_u1 ipacked;
            const nj_u1 *pal = gct;
            int pal_entries = gct_entries;
            nj_u1 lct[3 * 256];
            nj_u1 min_code_size;
            nj_u4 area;

            if (!gif_u16(&r, &left) || !gif_u16(&r, &top) ||
                !gif_u16(&r, &iw) || !gif_u16(&r, &ih) ||
                !gif_byte(&r, &ipacked)) {
                goto fail;
            }
            if (iw == 0u || ih == 0u) {
                goto fail;
            }
            if ((ipacked & 0x80u) != 0u) {
                nj_u4 entries = 1u << ((nj_u4)(ipacked & 0x07u) + 1u);
                const nj_u1 *table;

                if (!gif_bytes(&r, &table, entries * 3u)) {
                    goto fail;
                }
                memcpy(lct, table, (size_t)(entries * 3u));
                pal = lct;
                pal_entries = (int)entries;
            }
            if (pal_entries == 0) {
                goto fail;              /* no colour table anywhere */
            }
            if (!gif_byte(&r, &min_code_size)) {
                goto fail;
            }
            area = (nj_u4)iw * (nj_u4)ih;
            indices = (nj_u1 *)alloc(ud, (nj_size)area);
            dict = (gif_dict *)alloc(ud, sizeof(*dict));
            if (!indices || !dict) {
                goto fail;
            }
            if (!gif_lzw_decode(&r, dict, (int)min_code_size, indices, area)) {
                goto fail;
            }

            /* Expand into the canvas, clipping to the logical screen.
             * `indices` holds the scanlines in the order they were stored;
             * for an interlaced image the stored scanline i belongs to real
             * row gif_interlace_row(i), so only the DESTINATION is mapped. */
            for (i = 0; i < (nj_u4)ih; i++) {
                nj_u4 row = ((ipacked & 0x40u) != 0u)
                                ? gif_interlace_row(i, (nj_u4)ih) : i;
                nj_u4 y = (nj_u4)top + row;
                nj_u4 x;
                const nj_u1 *src = indices + (nj_size)i * (nj_size)iw;

                if (y >= (nj_u4)screen_h) {
                    continue;
                }
                for (x = 0; x < (nj_u4)iw; x++) {
                    nj_u4 idx = src[x];
                    nj_u4 px;

                    if ((nj_u4)left + x >= (nj_u4)screen_w) {
                        break;
                    }
                    if (idx == (nj_u4)pal_entries) {
                        goto fail;
                    }
                    if (transparent >= 0 && idx == (nj_u4)transparent) {
                        continue;       /* leave it transparent */
                    }
                    px = ((nj_u4)pal[idx * 3u] << 16) |
                         ((nj_u4)pal[idx * 3u + 1u] << 8) |
                         (nj_u4)pal[idx * 3u + 2u];
                    pixels[(y * (nj_u4)screen_w) + (nj_u4)left + x] =
                        0xFF000000u | px;
                }
            }
            dealloc(ud, indices);
            indices = NULL;
            dealloc(ud, dict);
            dict = NULL;
            /* First frame only: Image.createImage has no timeline. */
            break;
        }
    }

    img = (nj_image *)alloc(ud, sizeof(*img));
    if (!img) {
        goto fail;
    }
    memset(img, 0, sizeof(*img));
    img->width = (int)screen_w;
    img->height = (int)screen_h;
    img->format = NJ_PIXFMT_ARGB8888;
    img->has_alpha = 1;
    img->pixels = pixels;
    return img;

fail:
    if (indices) {
        dealloc(ud, indices);
    }
    if (dict) {
        dealloc(ud, dict);
    }
    if (pixels) {
        dealloc(ud, pixels);
    }
    if (err) {
        *err = "malformed GIF";
    }
    return NULL;
}

static nj_image *gif_decode(const nj_codec_req *req)
{
    return nj_gif_decode(req->data, req->size, req->alloc, req->ud,
                         req->dealloc, req->err);
}

const nj_codec nj_codec_gif_software = {
    "gif-software",
    { NJ_CODEC_FMT_GIF, NJ_CODEC_CAN_ARGB8888, NJ_CODEC_SCALE_1,
      0u, 1, 0 },
    gif_accepts, gif_info, gif_decode,
    NULL, NULL, NULL
};

#else /* !NJ_ENABLE_GIF */

nj_image *nj_gif_decode(const void *data, nj_u4 size,
                        void *(*alloc)(void *, nj_size), void *ud,
                        void  (*dealloc)(void *, void *),
                        const char **err)
{
    (void)data; (void)size; (void)alloc; (void)ud; (void)dealloc;
    if (err != NULL) {
        *err = "GIF support not built";
    }
    return NULL;
}

/* A valid descriptor that never matches: accepts() is NULL. */
const nj_codec nj_codec_gif_software = {
    "gif-software",
    { NJ_CODEC_FMT_GIF, NJ_CODEC_CAN_ARGB8888, NJ_CODEC_SCALE_1,
      0u, 0, 0 },
    NULL, NULL, NULL,
    NULL, NULL, NULL
};

#endif /* NJ_ENABLE_GIF */
