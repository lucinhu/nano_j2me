/*
 * nano_j2me - nj_text.c
 *
 * Text rendering: nj_gfx_draw_text() and nj_gfx_draw_char().
 *
 * These two live in their own translation unit because they are the seam
 * between the two halves of the graphics layer: the rasterizer owns pixels,
 * clipping, blending and XOR, while the font module owns metrics and glyph
 * bitmaps.  Combining them here means neither module has to know about the
 * other's internals, and a port that installs a CJK glyph provider only has to
 * satisfy the nj_font interface.
 *
 * ---------------------------------------------------------------------------
 * GLYPH FORMAT (frozen, see nj_gfx.h)
 * ---------------------------------------------------------------------------
 * 1 bit per pixel, MSB first, one byte per 8 horizontal pixels, rows packed
 * with no padding:
 *
 *      row_bytes    = (glyph_w + 7) / 8
 *      glyph stride = row_bytes * glyph_h
 *      glyph ch     = bitmap + (ch - first_char) * stride
 *
 * Two more formats may arrive from the hook or from an antialiased fixed
 * table, selected by the face's coverage value: 1 means one unpacked byte
 * per pixel, 2 means packed 2bpp coverage (4 pixels per byte, MSB first,
 * values 0..3 scaled to 0/85/170/255).  blit_glyph decodes each accordingly.
 *
 * ---------------------------------------------------------------------------
 * MIDP ANCHOR SEMANTICS
 * ---------------------------------------------------------------------------
 * The anchor is a bitmask, and the defaults are not the obvious ones:
 *
 *   horizontal: LEFT | HCENTER | RIGHT         - LEFT if none is given
 *   vertical:   TOP | VCENTER | BOTTOM | BASELINE
 *                                              - BASELINE if none is given
 *
 * So `drawString(s, x, y, Graphics.TOP | Graphics.LEFT)` places the *top* of
 * the text box at y, while a bare `drawString(s, x, y)` places the *baseline*
 * there.  Getting this wrong produces text that looks fine until a game mixes
 * both forms, so the arithmetic is written out explicitly below rather than
 * folded into one expression.
 */
#include "nj_internal.h"

/* ------------------------------------------------------------------ */
/* Glyph access                                                        */
/* ------------------------------------------------------------------ */
typedef struct glyph {
    const nj_u1 *bits;   /* 1bpp rows, packed 2bpp, or 8bpp coverage  */
    int w;
    int h;
    int advance;         /* how far the pen moves after this glyph   */
    int coverage;        /* 0 = 1bpp, 2 = packed 2bpp, 1 = 8bpp      */
    int ascent;          /* baseline row of the resolved bitmap      */
    const nj_font *outline; /* provider for a physical-size raster, if any */
} glyph;

/* Bytes per glyph row in the FIXED table, which is 1bpp (coverage 0) or
 * packed 2bpp (coverage 2) depending on which generated header the build
 * selected.  The unpacked 8bpp path never comes through here: it only
 * arrives from a glyph_lookup hook and computes its own stride. */
static int f_row_bytes(int w, int coverage)
{
    return coverage == 2 ? (w * 2 + 7) / 8 : (w + 7) / 8;
}

/*
 * Resolve one character to its bitmap.
 *
 * A provider may override even a character present in the fixed table.
 * Try the provider, the fixed table, then the fallback face. If every face
 * misses, the last face supplies a blank cell.
 */
static void glyph_lookup(const nj_font *f, nj_u2 ch, glyph *out, int depth)
{
    out->bits = NULL;
    out->outline = NULL;
    out->w = f->glyph_w;
    out->h = f->glyph_h;
    out->advance = f->glyph_w;
    out->coverage = f->coverage;
    out->ascent = f->ascent;

    if (f->glyph_lookup) {
        int w = 0, h = 0;
        const nj_u1 *bits = f->glyph_lookup(f->glyph_ud, ch, &w, &h);
        if (bits && w > 0 && h > 0) {
            out->bits = bits;
            out->outline = f->scaled_face ? f : NULL;
            out->ascent = f->glyph_ascent ? f->glyph_ascent : f->ascent;
            out->w = w;
            out->h = h;
            out->advance = f->glyph_advance
                               ? f->glyph_advance(f->glyph_ud, ch)
                               : w;
            return;
        }
    }

    if (ch >= f->first_char && ch <= f->last_char && f->bitmap) {
        /* The fixed table declares its own format: monochrome builds stay
         * 1bpp, antialiased builds carry packed 2bpp rows (bitmap_coverage
         * from nj_font.c's generated header). */
        out->coverage = f->bitmap_coverage;
        out->bits = f->bitmap +
                    (nj_size)(ch - f->first_char) *
                        f_row_bytes(f->glyph_w, f->bitmap_coverage) *
                        (nj_size)f->glyph_h;
        return;
    }

    /* Nothing here can draw it: try the fallback chain, guarding against a
     * cycle between two fonts that point at each other. */
    if (f->fallback && depth < 4) {
        glyph_lookup(f->fallback, ch, out, depth + 1);
        return;
    }
    /* A blank cell of the right width keeps the layout sane. */
}

/* ------------------------------------------------------------------ */
/* One glyph                                                           */
/* ------------------------------------------------------------------ */
/*
 * Draw `bits` so that its baseline row lands on `baseline_y` and its left edge
 * on `pen_x`.
 *
 * Horizontal runs of set bits are emitted as spans through nj_gfx_fill_rect
 * rather than one call per pixel.  A run of eight or more pixels is common in
 * a bitmap face, and the span path also means the glyph inherits the
 * rasterizer's clipping, alpha blending and XOR behaviour for free instead of
 * reimplementing them here.
 */
static void blit_glyph(nj_gfx *g, const glyph *gl,
                       int pen_x, int baseline_y)
{
    int row_bytes = (gl->w + 7) / 8;   /* 1bpp branch only */
    /* The baseline comes from the face that supplied the glyph, not from the
     * face the caller asked: a fallback with different metrics would otherwise
     * be drawn at the wrong height. */
    int top = baseline_y - gl->ascent;
    int y;

    if (!gl->bits || gl->w <= 0 || gl->h <= 0) {
        return;
    }

    if (gl->coverage) {
        /*
         * Unpacked 8bpp or packed 2bpp coverage. Blending preserves partial
         * samples: a coverage of 128 has no 1bpp equivalent, and cutting at 128 is
         * precisely what deletes the thin strokes of "1", "l", "i" and "t"
         * when an antialiased rasteriser is fed through a bitmap one.
         *
         * Covered-but-empty pixels are skipped whole, which is most of an
         * average glyph's box, so this costs less than it looks.
         */
        for (y = 0; y < gl->h; y++) {
            const int stride = gl->coverage == 2 ? (gl->w + 3) / 4 : gl->w;
            const nj_u1 *row = gl->bits + (nj_size)y * stride;
            int x;

            for (x = 0; x < gl->w; x++) {
                int alpha = gl->coverage == 2
                    ? ((row[x >> 2] >> (6 - 2 * (x & 3))) & 3u) * 85
                    : row[x];
                if (alpha != 0) {
                    nj_gfx_blend_coverage(g, pen_x + x, top + y, alpha);
                }
            }
        }
        return;
    }

    for (y = 0; y < gl->h; y++) {
        const nj_u1 *row = gl->bits + (nj_size)y * row_bytes;
        int x = 0;

        while (x < gl->w) {
            int run_start = -1;
            int run_len = 0;

            /* Find the next run of set bits in this row. */
            while (x < gl->w) {
                int byte = x >> 3;
                int bit = 7 - (x & 7);
                int on = (row[byte] >> bit) & 1;
                if (on) {
                    if (run_start < 0) {
                        run_start = x;
                    }
                    run_len++;
                } else if (run_start >= 0) {
                    break;
                }
                x++;
            }
            if (run_start >= 0 && run_len > 0) {
                nj_gfx_fill_rect(g, pen_x + run_start, top + y, run_len, 1);
            }
        }
    }
}

static unsigned glyph_coverage(const glyph *gl, int x, int y)
{
    if (gl->coverage == 1) return gl->bits[(nj_size)y * gl->w + x];
    int bpp = gl->coverage == 2 ? 2 : 1;
    int per_byte = 8 / bpp;
    nj_size pitch = ((nj_size)gl->w + per_byte - 1) / per_byte;
    unsigned byte = gl->bits[(nj_size)y * pitch + x / per_byte];
    unsigned mask = (1u << bpp) - 1;
    return ((byte >> ((per_byte - 1 - x % per_byte) * bpp)) & mask) * (255 / mask);
}

/* Keep a solid core in small bitmap strokes while retaining fractional
 * coverage around their edges. The source coverage itself stays intact. */
static unsigned glyph_filter_weight(nj_s8 position, nj_s8 period, int crisp)
{
    unsigned w = (unsigned)(((position % period) * 256 + period / 2) / period);
    if (!crisp) return w;
    if (w <= 64) return 0;
    if (w >= 192) return 256;
    return 2 * w - 128;
}

/* Sample the resolved bitmap within its own ink cell. Keeping the bearing,
 * advance and cell boundary avoids bleeding adjacent glyphs together. */
static void blit_filtered_bitmap(nj_gfx *physical, const nj_gfx *logical, const glyph *gl,
                                 int pen, int baseline, int scale)
{
    int mode = logical->mirror->smooth_assets;
    /* FSR1 sharpens the artwork with a cubic plus clipped contrast, so the
     * glyph coverage curve gets the same treatment as pixel art instead of
     * the softer smooth ramp. */
    int crisp = mode == NJ_ASSET_PIXEL_ART || mode == NJ_ASSET_FSR1;
    nj_s8 left = ((nj_s8)pen + logical->translate_x) * scale;
    nj_s8 top = ((nj_s8)baseline - gl->ascent + logical->translate_y) * scale;
    nj_s8 right = left + (nj_s8)gl->w * scale, bottom = top + (nj_s8)gl->h * scale;
    int x0 = physical->clip_x, x1 = x0 + physical->clip_w;
    int y0 = physical->clip_y, y1 = y0 + physical->clip_h;
    if (right <= x0 || left >= x1 || bottom <= y0 || top >= y1) return;
    if (left > x0) x0 = (int)left;
    if (top > y0) y0 = (int)top;
    if (right < x1) x1 = (int)right;
    if (bottom < y1) y1 = (int)bottom;
    for (int y = y0; y < y1; ++y) {
        nj_s8 v = 2 * (y - top) + 1 - scale, den = 2 * (nj_s8)scale;
        if (v < 0) v = 0;
        int ya = (int)(v / den), yb = ya + 1 < gl->h ? ya + 1 : ya;
        unsigned wy = glyph_filter_weight(v, den, crisp);
        for (int x = x0; x < x1; ++x) {
            nj_s8 u = 2 * (x - left) + 1 - scale;
            if (u < 0) u = 0;
            int xa = (int)(u / den), xb = xa + 1 < gl->w ? xa + 1 : xa;
            unsigned wx = glyph_filter_weight(u, den, crisp);
            unsigned a = (256 - wx) * glyph_coverage(gl, xa, ya) + wx * glyph_coverage(gl, xb, ya);
            unsigned b = (256 - wx) * glyph_coverage(gl, xa, yb) + wx * glyph_coverage(gl, xb, yb);
            int coverage = (int)(((256 - wy) * a + wy * b + 32768) >> 16);
            if (coverage) nj_gfx_blend_coverage(physical, x, y, coverage);
        }
    }
}

/* Place a physical glyph at the scaled logical pen and baseline. Using
 * device coordinates here also lets us reject off-screen wide products
 * before narrowing them to the rasteriser's int coordinates. */
static int blit_outline(nj_gfx *physical, const nj_gfx *logical, const nj_font *f,
                        nj_u2 ch, int pen, int baseline, int scale)
{
    nj_s8 x = ((nj_s8)pen + logical->translate_x) * scale;
    nj_s8 y = ((nj_s8)baseline + logical->translate_y) * scale;
    const nj_font *face = f->scaled_face(f->glyph_ud, scale);
    glyph gl;
    if (!face) return 0;
    glyph_lookup(face, ch, &gl, 0);
    if (!gl.bits) return 0;
    if (x + gl.w > 0 && x < physical->surf->width &&
        y - gl.ascent + gl.h > 0 && y - gl.ascent < physical->surf->height) {
        blit_glyph(physical, &gl, (int)x, (int)y);
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Public entry points                                                 */
/* ------------------------------------------------------------------ */
void nj_gfx_draw_text(nj_gfx *g, const nj_u2 *s, int len, int x, int y,
                      int anchor)
{
    const nj_font *f;
    int w = 0;
    int i;
    int pen;
    int baseline;
    int horiz;
    int vert;
    int scale = 0;
    nj_gfx physical, logical;

    if (!g || !s) {
        return;
    }
#ifdef NJ_GFX_2D_STATS
    nj_gfx_2d_note_op(NJ2D_TEXT, 0);
#endif
    if (len < 0) {
        len = 0;
        while (s[len] != 0u) {
            len++;
        }
    }
    if (len == 0) {
        return;
    }
    f = g->font;
    if (!f) {
        return;
    }

    /* ---- horizontal placement ---- */
    horiz = anchor & (NJ_ANCHOR_HCENTER | NJ_ANCHOR_RIGHT);
    /* Underline uses the measured width, which can differ from pen travel
     * when a provider overrides a fixed-table glyph. */
    if (horiz || (f->style & NJ_FONT_STYLE_UNDERLINED)) {
        w = nj_font_string_width(f, s, len);
    }
    if (horiz & NJ_ANCHOR_RIGHT) {
        pen = x - w;
    } else if (horiz & NJ_ANCHOR_HCENTER) {
        pen = x - w / 2;
    } else {
        pen = x;                    /* LEFT, and the default */
    }

    /* ---- vertical placement ----
     * The glyph blitter works from a baseline, so each anchor is converted
     * into the baseline y that satisfies it. */
    vert = anchor & (NJ_ANCHOR_TOP | NJ_ANCHOR_VCENTER | NJ_ANCHOR_BOTTOM);
    if (vert & NJ_ANCHOR_TOP) {
        baseline = y + f->ascent;
    } else if (vert & NJ_ANCHOR_BOTTOM) {
        baseline = y - f->descent;
    } else if (vert & NJ_ANCHOR_VCENTER) {
        baseline = y + (f->ascent - f->descent) / 2;
    } else {
        baseline = y;               /* BASELINE, and the default */
    }

    if (g->mirror) {
        scale = nj_gfx_mirror_context(g, &physical);
        if (scale) {
            physical.translate_x = physical.translate_y = 0;
            logical = *g;
            logical.mirror = NULL;
        }
    }
    for (i = 0; i < len; i++) {
        glyph gl;
        glyph_lookup(f, s[i], &gl, 0);
        nj_gfx *target = g;
        if (scale && gl.outline && blit_outline(&physical, g, gl.outline,
                                                s[i], pen, baseline, scale)) {
            target = &logical;
        } else if (scale > 1 && g->mirror->smooth_assets && gl.bits &&
                   !g->use_xor && g->blend == NJ_BLEND_SRC_OVER) {
            blit_filtered_bitmap(&physical, g, &gl, pen, baseline, scale);
            target = &logical;
        }
        blit_glyph(target, &gl, pen, baseline);
        pen += gl.advance;
    }

    /* Underline: one solid run under the whole string, which is what MIDP
     * specifies and what the eye expects. */
    if ((f->style & NJ_FONT_STYLE_UNDERLINED) != 0) {
        nj_gfx_fill_rect(g, pen - w, baseline + 1, w, 1);
    }
}

void nj_gfx_draw_char(nj_gfx *g, nj_u2 ch, int x, int y)
{
    nj_gfx_draw_text(g, &ch, 1, x, y, NJ_ANCHOR_LEFT);
}
