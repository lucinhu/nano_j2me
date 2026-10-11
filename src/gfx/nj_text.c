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
    const nj_u1 *bits;   /* 1bpp rows, MSB first, row_bytes per row */
    int w;
    int h;
    int advance;         /* how far the pen moves after this glyph   */
} glyph;

static int f_row_bytes(int w)
{
    return (w + 7) / 8;
}

/*
 * Resolve one character to its bitmap.
 *
 * Characters inside [first_char, last_char] come from the fixed table.  For
 * anything else the font's glyph_lookup hook is consulted, which is how a CJK
 * provider or a proportional face plugs in; when there is none, the fallback
 * font is followed.  A character that nothing can render becomes a blank of
 * the font's own width, so a missing glyph shifts the layout by the right
 * amount instead of piling the rest of the string on top of itself.
 */
static void glyph_lookup(const nj_font *f, nj_u2 ch, glyph *out, int depth)
{
    out->bits = NULL;
    out->w = f->glyph_w;
    out->h = f->glyph_h;
    out->advance = f->glyph_w;

    if (f->glyph_lookup) {
        int w = 0, h = 0;
        const nj_u1 *bits = f->glyph_lookup(f->glyph_ud, ch, &w, &h);
        if (bits && w > 0 && h > 0) {
            out->bits = bits;
            out->w = w;
            out->h = h;
            out->advance = f->glyph_advance
                               ? f->glyph_advance(f->glyph_ud, ch)
                               : w;
            return;
        }
    }

    if (ch >= f->first_char && ch <= f->last_char && f->bitmap) {
        out->bits = f->bitmap +
                    (nj_size)(ch - f->first_char) * f_row_bytes(f->glyph_w) *
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
static void blit_glyph(nj_gfx *g, const nj_font *f, const glyph *gl,
                       int pen_x, int baseline_y)
{
    int row_bytes = f_row_bytes(gl->w);
    int top = baseline_y - f->ascent;
    int y;

    if (!gl->bits || gl->w <= 0 || gl->h <= 0) {
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

/* ------------------------------------------------------------------ */
/* Public entry points                                                 */
/* ------------------------------------------------------------------ */
void nj_gfx_draw_text(nj_gfx *g, const nj_u2 *s, int len, int x, int y,
                      int anchor)
{
    const nj_font *f;
    int w;
    int i;
    int pen;
    int baseline;
    int horiz;
    int vert;

    if (!g || !s) {
        return;
    }
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

    w = nj_font_string_width(f, s, len);

    /* ---- horizontal placement ---- */
    horiz = anchor & (NJ_ANCHOR_LEFT | NJ_ANCHOR_HCENTER | NJ_ANCHOR_RIGHT);
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

    for (i = 0; i < len; i++) {
        glyph gl;
        glyph_lookup(f, s[i], &gl, 0);
        blit_glyph(g, f, &gl, pen, baseline);
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
    nj_u2 buf[2];

    buf[0] = ch;
    buf[1] = 0u;
    /* The documented default anchor is LEFT | BASELINE. */
    nj_gfx_draw_text(g, buf, 1, x, y, NJ_ANCHOR_LEFT);
}
