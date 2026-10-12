/*
 * nano_j2me - tests/stub_font.c
 *
 * ============================================================================
 * THROWAWAY TEST STUBS -- NOT PART OF THE RUNTIME.
 * ============================================================================
 *
 * The real text layer lives in src/gfx/nj_font.c.  This file survives it on
 * purpose: tests/test_gfx.c compiles nj_gfx.c against these stubs instead of
 * linking the library, so the anchor maths of nj_gfx_draw_text() and
 * nj_gfx_draw_char() is measured against a fixed glyph set rather than against
 * whatever the real bitmap font happens to contain.  Do not delete it to
 * "finish" the port - the CMake rule for test_gfx names it explicitly, and
 * swapping in the real font would quietly make those assertions vacuous.
 *
 * Every glyph below is a hollow 5x7 box drawn through the real primitives from
 * src/gfx/nj_gfx.c (nj_gfx_rect / nj_gfx_pixel), so the stubs honour clip,
 * translate, colour, blend and XOR exactly like the real font module must.
 */
#include "nj2me/nj_gfx.h"

#include <string.h>

/* The header declares every anchor except BASELINE (MIDP value 64); the stub
 * text code still honours it because MIDP drawString() needs it. */
#define STUB_ANCHOR_BASELINE 64

#define STUB_GLYPH_W 5   /* painted box width  (columns x .. x+4) */
#define STUB_GLYPH_H 7   /* painted box height (rows    y .. y+6) */
#define STUB_ADVANCE 6   /* cell width: box + 1 column gap        */

/* Immutable: a fixed-pitch 6x8 cell font with no bitmap (glyph_lookup is the
 * only source of coverage, which keeps this stub tiny). */
static const nj_font stub_font = {
    "stub-6x8",
    8,                      /* height  */
    6,                      /* ascent  */
    2,                      /* descent */
    0,                      /* leading */
    NJ_FONT_STYLE_PLAIN,
    NJ_FONT_SIZE_MEDIUM,
    1,                      /* proportional (advance comes from the cell) */
    0, 0,
    STUB_ADVANCE, 8,
    NULL,                   /* no fixed bitmap */
    NULL,                   /* no glyph_lookup: the stub draws via primitives */
    0,                      /* coverage: 1bpp rows (unused by this stub) */
    NULL,                   /* glyph_ud */
    NULL,                   /* glyph_advance */
    NULL                    /* no fallback */
};

const nj_font *nj_font_builtin(int size, int style)
{
    (void)size;
    (void)style;
    return &stub_font;
}

void nj_fontset_init(nj_fontset *fs)
{
    int i;

    if (!fs) {
        return;
    }
    memset(fs, 0, sizeof *fs);
    for (i = 0; i < NJ_FONTSET_SLOTS; i++) {
        fs->fonts[i] = stub_font;
    }
    fs->initialized = 1;
    fs->count = NJ_FONTSET_SLOTS;
}

const nj_font *nj_fontset_get(nj_fontset *fs, int size, int style)
{
    (void)size;
    (void)style;
    if (fs && fs->initialized) {
        return &fs->fonts[0];
    }
    return &stub_font;
}

void nj_fontset_set_fallback(nj_fontset *fs, const nj_font *fb)
{
    int i;

    if (!fs) {
        return;
    }
    for (i = 0; i < NJ_FONTSET_SLOTS; i++) {
        fs->fonts[i].fallback = fb;
    }
}

int nj_font_char_width(const nj_font *f, nj_u2 ch)
{
    (void)f;
    (void)ch;
    return STUB_ADVANCE;
}

int nj_font_string_width(const nj_font *f, const nj_u2 *s, int len)
{
    (void)f;
    (void)s;
    if (len <= 0) {
        return 0;
    }
    return len * STUB_ADVANCE;
}

int nj_font_chars_that_fit(const nj_font *f, const nj_u2 *s, int len, int avail)
{
    int n;

    (void)f;
    (void)s;
    if (avail <= 0) {
        return 0;
    }
    n = avail / STUB_ADVANCE;
    return n > len ? len : n;
}

void nj_gfx_draw_char(nj_gfx *g, nj_u2 ch, int x, int y)
{
    if (!g || ch == 0) {
        return;
    }
    /* Hollow box: drawRect(x, y, 4, 6) covers exactly 5x7 pixels. */
    nj_gfx_rect(g, x, y, STUB_GLYPH_W - 1, STUB_GLYPH_H - 1);
}

void nj_gfx_draw_text(nj_gfx *g, const nj_u2 *s, int len, int x, int y,
                      int anchor)
{
    const nj_font *f;
    int w, i, cx, cy;

    if (!g || !s || len <= 0) {
        return;
    }
    f = g->font ? g->font : &stub_font;
    w = nj_font_string_width(f, s, len);

    /* MIDP anchor points mark the outside edge of the text bounding box. */
    cx = x;
    if ((anchor & NJ_ANCHOR_HCENTER) != 0) {
        cx = x - w / 2;
    } else if ((anchor & NJ_ANCHOR_RIGHT) != 0) {
        cx = x - w;
    }
    cy = y;
    if ((anchor & NJ_ANCHOR_BOTTOM) != 0) {
        cy = y - f->height;
    } else if ((anchor & STUB_ANCHOR_BASELINE) != 0) {
        cy = y - f->ascent;
    } else if ((anchor & NJ_ANCHOR_VCENTER) != 0) {
        cy = y - f->height / 2;
    }

    for (i = 0; i < len; i++) {
        nj_gfx_draw_char(g, s[i], cx, cy);
        cx += nj_font_char_width(f, s[i]);
    }
}
