/*
 * nano_j2me - tests/test_font_coverage.c
 *
 * The renderer's antialiased-text path, tested without a TrueType font.
 *
 * Why this is separate from tests/test_ttf.c
 * ------------------------------------------
 * Two different things can be wrong with antialiased text, and they fail the
 * same way.  The rasteriser can compute the wrong coverage, and the renderer
 * can mishandle coverage it was handed correctly.  A test that runs both at
 * once cannot tell you which broke: a wrong pixel is a wrong pixel, and the
 * usual first move - stare at the rasteriser - is then a coin flip.
 *
 * So this file supplies coverage values that are known exactly, from a fake
 * font whose "glyph" is a table of chosen numbers, and puts the renderer alone
 * under test: blit_glyph()'s coverage branch in src/gfx/nj_text.c and
 * nj_gfx_blend_coverage() in src/gfx/nj_gfx.c.
 *
 * The assertion that carries the most weight is in test_blend(): a pixel at
 * partial coverage must land strictly between the ink and the background.
 * Both a thresholding rasteriser and a thresholding renderer produce 0 and 255
 * and nothing else, and either one alone turns a thin stem into a gap - which
 * is the failure this whole feature exists to avoid.  Asserting exact rounded
 * values instead would be weaker, because a thresholding implementation that
 * happened to round the same way on the chosen inputs would pass.
 */
#include "nj2me/nj_gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Harness                                                             */
/* ------------------------------------------------------------------ */
typedef struct tctx {
    unsigned checks;
    unsigned fails;
} tctx;

static void t_check(tctx *t, int ok, const char *expr, int line)
{
    t->checks++;
    if (!ok) {
        t->fails++;
        printf("  FAIL (line %d): %s\n", line, expr);
    }
}

#define CHECK(t, cond) t_check((t), (cond) ? 1 : 0, #cond, __LINE__)

#define SENTINEL 0xA5u

typedef struct tsurf {
    nj_surface s;
    nj_u1     *buf;
    int        bpp;
    int        pad;
} tsurf;

static int fmt_bpp_of(int fmt)
{
    return fmt == NJ_PIXFMT_RGB565 ? 2 : (fmt == NJ_PIXFMT_RGB888 ? 3 : 4);
}

static void ts_init(tsurf *ts, int w, int h, int fmt, int pad, nj_u4 bg)
{
    size_t n;

    memset(ts, 0, sizeof *ts);
    ts->bpp = fmt_bpp_of(fmt);
    ts->pad = pad;
    ts->s.width = w;
    ts->s.height = h;
    ts->s.format = fmt;
    ts->s.stride = w * ts->bpp + pad;
    ts->s.bpp = 0;                        /* nj_gfx_init() must cache it */
    n = (size_t)ts->s.stride * (size_t)h;
    ts->buf = (nj_u1 *)malloc(n ? n : 1u);
    if (!ts->buf) {
        printf("out of memory\n");
        exit(2);
    }
    memset(ts->buf, SENTINEL, n);
    ts->s.pixels = ts->buf;
    nj_surface_fill(&ts->s, bg);
}

static void ts_free(tsurf *ts)
{
    free(ts->buf);
    ts->buf = NULL;
}

static nj_u4 px(const tsurf *ts, int x, int y)
{
    return nj_surface_get(&ts->s, x, y);
}

#define C_BLACK 0xFF000000u
#define C_WHITE 0xFFFFFFFFu

/* ------------------------------------------------------------------ */
/* The fake coverage font                                              */
/* ------------------------------------------------------------------ */

/*
 * A 4x4 "glyph" of sixteen different coverage values, chosen so that a
 * mistake is visible rather than plausible:
 *
 *   - no two rows start at the same magnitude, so a transposed or reversed
 *     row moves a specific pixel instead of still looking like text;
 *   - the values descend to 1, which is the magnitude a renderer is most
 *     likely to round away or clamp to zero;
 *   - one row is a constant 128, which is the value a threshold either erases
 *     or saturates, so it fails loudly if the blend is missing.
 */
static const nj_u1 s_cover[16] = {
    255u, 192u, 128u,  64u,
     32u,  16u,   8u,   4u,
      2u,   1u,   0u, 255u,
    128u, 128u, 128u, 128u
};

static const nj_u1 *cover_lookup(void *ud, nj_u2 ch, int *w, int *h)
{
    (void)ud;
    (void)ch;
    *w = 4;
    *h = 4;
    return s_cover;
}

static int cover_advance(void *ud, nj_u2 ch)
{
    (void)ud;
    (void)ch;
    return 4;
}

/*
 * A coverage face whose glyph sits entirely above its own baseline.
 * ascent 4 with a 4-pixel glyph means glyph row 0 is the top of the ascent.
 */
static const nj_font s_cov_font = {
    "cover-4x4",
    6, 4, 2, 0,                          /* height, ascent, descent, leading */
    NJ_FONT_STYLE_PLAIN,
    NJ_FONT_SIZE_MEDIUM,
    0,                                   /* proportional */
    0u, 0u,                              /* first_char, last_char */
    4, 4,                                /* glyph_w, glyph_h */
    NULL,                                /* bitmap */
    0,                                   /* bitmap_coverage */
    cover_lookup,
    1,                                   /* coverage */
    NULL,                                /* glyph_ud */
    cover_advance,
    NULL                                 /* fallback */
};

/* ------------------------------------------------------------------ */
/* [1] Partial coverage blends instead of thresholding                  */
/* ------------------------------------------------------------------ */
static void test_blend(tctx *t)
{
    static const nj_u2 one[1] = { 'A' };
    tsurf ts;
    nj_gfx g;

    printf("[1] coverage blends between ink and background\n");

    /* ARGB8888, so the blend arithmetic is exact 8-bit and the ordering
     * assertions below cannot be perturbed by RGB565 rounding. */
    ts_init(&ts, 16, 16, NJ_PIXFMT_ARGB8888, 0, C_WHITE);
    nj_gfx_init(&g, &ts.s);
    nj_gfx_set_font(&g, &s_cov_font);
    nj_gfx_set_color(&g, C_BLACK);

    /* TOP|LEFT puts glyph row 0 at the anchor and the baseline `ascent`
     * below it, so the 4x4 glyph covers (2,3)..(5,6). */
    nj_gfx_draw_text(&g, one, 1, 2, 3, NJ_ANCHOR_TOP | NJ_ANCHOR_LEFT);

    /* The two ends are exact: full coverage is the ink, none is untouched. */
    CHECK(t, px(&ts, 2, 3) == C_BLACK);
    CHECK(t, px(&ts, 5, 5) == C_BLACK);
    CHECK(t, px(&ts, 4, 5) == C_WHITE);

    /* The assertion this file exists for.  A thresholding renderer gives one
     * of the two endpoints here, never anything between. */
    CHECK(t, px(&ts, 4, 3) != C_WHITE);
    CHECK(t, px(&ts, 4, 3) != C_BLACK);
    CHECK(t, px(&ts, 2, 6) != C_WHITE);
    CHECK(t, px(&ts, 2, 6) != C_BLACK);

    /* Ordering: more coverage means a darker pixel, for every value on the
     * row and for both ends of the range.  This is what a renderer that
     * silently ignored `coverage` would break, even if it blended. */
    CHECK(t, px(&ts, 2, 3) < px(&ts, 3, 3));    /* 255 < 192 */
    CHECK(t, px(&ts, 3, 3) < px(&ts, 4, 3));    /* 192 < 128 */
    CHECK(t, px(&ts, 4, 3) < px(&ts, 5, 3));    /* 128 < 64  */
    CHECK(t, px(&ts, 5, 3) < px(&ts, 2, 4));    /* 64  < 32  */
    CHECK(t, px(&ts, 2, 4) < px(&ts, 3, 4));    /* 32  < 16  */
    CHECK(t, px(&ts, 3, 4) < px(&ts, 4, 4));    /* 16  < 8   */
    CHECK(t, px(&ts, 4, 4) < px(&ts, 5, 4));    /* 8   < 4   */
    CHECK(t, px(&ts, 5, 4) < px(&ts, 2, 5));    /* 4   < 2   */
    CHECK(t, px(&ts, 2, 5) < px(&ts, 3, 5));    /* 2   < 1   */
    CHECK(t, px(&ts, 3, 5) < C_WHITE);          /* 1 still registers */

    /* A constant row: all four pixels identical, and the advance really is
     * the glyph width (a second glyph would start at x+4, not x+6). */
    CHECK(t, px(&ts, 2, 6) == px(&ts, 3, 6));
    CHECK(t, px(&ts, 3, 6) == px(&ts, 4, 6));
    CHECK(t, px(&ts, 4, 6) == px(&ts, 5, 6));

    ts_free(&ts);
}

/* ------------------------------------------------------------------ */
/* [2] The clip still applies                                          */
/* ------------------------------------------------------------------ */
static void test_clip(tctx *t)
{
    static const nj_u2 one[1] = { 'A' };
    tsurf ts;
    nj_gfx g;

    printf("[2] coverage honours the clip\n");

    ts_init(&ts, 16, 16, NJ_PIXFMT_ARGB8888, 0, C_WHITE);
    nj_gfx_init(&g, &ts.s);
    nj_gfx_set_font(&g, &s_cov_font);
    nj_gfx_set_color(&g, C_BLACK);

    /* Glyph at (2,3)..(5,6); keep only x >= 4. */
    nj_gfx_set_clip(&g, 4, 0, 2, 16);
    nj_gfx_draw_text(&g, one, 1, 2, 3, NJ_ANCHOR_TOP | NJ_ANCHOR_LEFT);

    CHECK(t, px(&ts, 4, 3) != C_WHITE);   /* inside */
    CHECK(t, px(&ts, 5, 3) != C_WHITE);   /* inside */
    CHECK(t, px(&ts, 2, 3) == C_WHITE);   /* clipped away, full coverage */
    CHECK(t, px(&ts, 3, 3) == C_WHITE);   /* clipped away, partial */
    CHECK(t, px(&ts, 2, 6) == C_WHITE);
    CHECK(t, px(&ts, 3, 6) == C_WHITE);

    ts_free(&ts);
}

/* ------------------------------------------------------------------ */
/* [3] A fallback glyph uses the fallback's baseline                   */
/* ------------------------------------------------------------------ */
/*
 * The glyph and the font asked for are not always the same font: a character
 * the primary face lacks comes from its fallback, and the two need not share
 * metrics.  Placing such a glyph with the caller's ascent draws it at the
 * wrong height - a bug that is invisible until a fallback with different
 * metrics is actually installed, which is exactly what a TrueType primary over
 * the built-in bitmap fallback is.
 */
static const nj_u1 *none_lookup(void *ud, nj_u2 ch, int *w, int *h)
{
    (void)ud;
    (void)ch;
    (void)w;
    (void)h;
    return NULL;   /* nothing here: force the fallback chain */
}

/* The fallback has ascent 7 and a 2x2 glyph of full coverage. */
static const nj_u1 s_fb_bits[4] = { 255u, 255u, 255u, 255u };

static const nj_u1 *fb_lookup(void *ud, nj_u2 ch, int *w, int *h)
{
    (void)ud;
    (void)ch;
    *w = 2;
    *h = 2;
    return s_fb_bits;
}

static int fb_advance(void *ud, nj_u2 ch)
{
    (void)ud;
    (void)ch;
    return 2;
}

static const nj_font s_fb_font = {
    "fb-2x2",
    9, 7, 2, 0,
    NJ_FONT_STYLE_PLAIN,
    NJ_FONT_SIZE_MEDIUM,
    0,
    0u, 0u,
    2, 2,
    NULL,
    0,
    fb_lookup,
    1,
    NULL,
    fb_advance,
    NULL
};

/* The primary: no glyphs of its own, ascent deliberately different (2). */
static const nj_font s_empty_font = {
    "empty",
    6, 2, 4, 0,
    NJ_FONT_STYLE_PLAIN,
    NJ_FONT_SIZE_MEDIUM,
    0,
    0u, 0u,
    4, 4,
    NULL,
    0,
    none_lookup,
    1,
    NULL,
    NULL,
    &s_fb_font
};

static void test_fallback_ascent(tctx *t)
{
    static const nj_u2 one[1] = { 'A' };
    tsurf ts;
    nj_gfx g;

    printf("[3] a fallback glyph is placed by the fallback's ascent\n");

    ts_init(&ts, 16, 16, NJ_PIXFMT_ARGB8888, 0, C_WHITE);
    nj_gfx_init(&g, &ts.s);
    nj_gfx_set_font(&g, &s_empty_font);
    nj_gfx_set_color(&g, C_BLACK);

    /* Anchor TOP|LEFT at (1,3) puts the baseline at 3 + the *caller's*
     * ascent.  With the empty font that is 3 + 2 = 5, and the fallback's
     * 2-pixel glyph then covers y = 5-7 .. 5-6, i.e. rows 3 and 4 - both
     * above the baseline, as a glyph must be.  Placing it by the caller's
     * ascent of 2 would put the top at 5 - 2 = 3 as well, so pick a larger
     * gap: the empty font's ascent is 2 and the fallback's is 7, so a bug
     * shows as the glyph starting 5 rows too high. */
    nj_gfx_draw_text(&g, one, 1, 1, 3, NJ_ANCHOR_TOP | NJ_ANCHOR_LEFT);

    /* Baseline = 3 + 2 = 5 (the caller's ascent, which is what the anchor
     * maths uses).  Correct placement: top = 5 - 7 = -2, so only the bottom
     * row of the glyph (y = -1) ... that would be off-screen, so use a
     * baseline far enough down instead. */
    ts_free(&ts);

    ts_init(&ts, 16, 16, NJ_PIXFMT_ARGB8888, 0, C_WHITE);
    nj_gfx_init(&g, &ts.s);
    nj_gfx_set_font(&g, &s_empty_font);
    nj_gfx_set_color(&g, C_BLACK);
    nj_gfx_draw_text(&g, one, 1, 1, 8, NJ_ANCHOR_TOP | NJ_ANCHOR_LEFT);

    /* Baseline = 8 + 2 = 10.  The fallback's ascent is 7, so its 2x2 glyph
     * covers rows 3 and 4 at columns 1 and 2.  Using the caller's ascent (2)
     * would put it at rows 8 and 9 - five rows lower, and clearly wrong. */
    CHECK(t, px(&ts, 1, 3) == C_BLACK);
    CHECK(t, px(&ts, 2, 3) == C_BLACK);
    CHECK(t, px(&ts, 1, 4) == C_BLACK);
    CHECK(t, px(&ts, 2, 4) == C_BLACK);
    CHECK(t, px(&ts, 1, 8) == C_WHITE);
    CHECK(t, px(&ts, 1, 9) == C_WHITE);
    CHECK(t, px(&ts, 1, 2) == C_WHITE);
    CHECK(t, px(&ts, 1, 5) == C_WHITE);

    ts_free(&ts);
}

/* ------------------------------------------------------------------ */
/* [4] The bitmap path honours the fixed table's declared format       */
/* ------------------------------------------------------------------ */
static void test_bitmap_unchanged(tctx *t)
{
    static const nj_u2 s[5] = { 'H', 'a', 'n', 'd', 'g' };
    const nj_font *f = nj_font_builtin(NJ_FONT_SIZE_MEDIUM,
                                       NJ_FONT_STYLE_PLAIN);
    tsurf ts;
    nj_gfx g;
    int x, y;
    int intermediate = 0;
    int ink = 0;

    printf("[4] the fixed table writes pixels in its declared coverage\n");

    ts_init(&ts, 64, 24, NJ_PIXFMT_ARGB8888, 0, C_WHITE);
    nj_gfx_init(&g, &ts.s);
    nj_gfx_set_font(&g, f);
    nj_gfx_set_color(&g, C_BLACK);
    nj_gfx_draw_text(&g, s, 5, 1, 2, NJ_ANCHOR_TOP | NJ_ANCHOR_LEFT);

    for (y = 0; y < 24; y++) {
        for (x = 0; x < 64; x++) {
            nj_u4 v = px(&ts, x, y);
            if (v == C_BLACK) {
                ink++;
            } else if (v != C_WHITE) {
                intermediate++;
            }
        }
    }
    /* A coverage value would have leaked here if glyph_lookup()'s caller
     * ignored the face's coverage flags; and the antialiased table must
     * actually blend edges instead of thresholding them away. */
    if (f->bitmap_coverage == 0) {
        CHECK(t, intermediate == 0);
    } else {
        CHECK(t, intermediate > 0);
    }
    CHECK(t, ink > 0);   /* and something was actually drawn */

    ts_free(&ts);
}

/* ------------------------------------------------------------------ */
/* [5] Full coverage still reaches the ordinary plot path              */
/* ------------------------------------------------------------------ */
/*
 * nj_gfx_blend_coverage() hands coverage 255 to nj_plot() rather than to the
 * blender, so the solid interior of a glyph keeps the renderer's real
 * semantics.  XOR is the visible case: it has no partial-coverage reading
 * ("half an XOR" is not a thing), and text drawn in XOR mode must keep
 * working at full coverage.
 */
static void test_full_coverage_plot(tctx *t)
{
    static const nj_u2 one[1] = { 'A' };
    tsurf ts;
    nj_gfx g;

    printf("[5] coverage 255 uses the ordinary plot path\n");

    ts_init(&ts, 16, 16, NJ_PIXFMT_ARGB8888, 0, C_WHITE);
    nj_gfx_init(&g, &ts.s);
    nj_gfx_set_font(&g, &s_cov_font);

    /* XOR with red over white: 0xFFFFFFFF ^ 0xFFFF0000 = 0xFF00FFFF. */
    nj_gfx_set_xor(&g, 0xFFFF0000u);
    nj_gfx_draw_text(&g, one, 1, 2, 3, NJ_ANCHOR_TOP | NJ_ANCHOR_LEFT);

    CHECK(t, px(&ts, 2, 3) == (C_WHITE ^ 0xFFFF0000u));
    CHECK(t, px(&ts, 5, 5) == (C_WHITE ^ 0xFFFF0000u));
    /* Coverage 0 is untouched in XOR mode too. */
    CHECK(t, px(&ts, 4, 5) == C_WHITE);

    ts_free(&ts);
}

/* A wide bitmap has extra rows above/below its baseline. Its storage height
 * must not shift it relative to fixed Latin glyphs, including in a fallback. */
static const nj_u1 *baseline_lookup(void *ud, nj_u2 ch, int *w, int *h)
{
    static const nj_u1 bits[6] = { 0, 0, 0, 255, 0, 255 };
    (void)ud;
    if (ch != 0x3400u) return NULL;
    *w = 1;
    *h = 6;
    return bits;
}

static void test_provider_baseline(tctx *t)
{
    static const nj_u1 latin[4] = { 0, 255, 0, 0 };
    static const nj_u2 text[2] = { 'A', 0x3400u };
    static const int anchors[4] = { NJ_ANCHOR_TOP, NJ_ANCHOR_BOTTOM, 64,
                                  NJ_ANCHOR_VCENTER };
    static const int ys[4] = { 10, 14, 12, 12 };
    int coverage, fallback, a;
    for (coverage = 0; coverage <= 1; coverage++) {
        for (fallback = 0; fallback <= 1; fallback++) {
            nj_font f, wide;
            memset(&f, 0, sizeof f);
            f.height = 4; f.ascent = 2; f.descent = 2;
            f.first_char = f.last_char = 'A';
            f.glyph_w = 1; f.glyph_h = 4; f.bitmap = latin;
            f.coverage = coverage;
            wide = f;
            wide.glyph_lookup = baseline_lookup;
            wide.glyph_ascent = 4;
            if (fallback) f.fallback = &wide;
            else { f.glyph_lookup = baseline_lookup; f.glyph_ascent = 4; }
            for (a = 0; a < 4; a++) {
                tsurf ts;
                nj_gfx g;
                ts_init(&ts, 16, 24, NJ_PIXFMT_ARGB8888, 0, C_WHITE);
                nj_gfx_init(&g, &ts.s);
                nj_gfx_set_font(&g, &f);
                nj_gfx_set_color(&g, C_BLACK);
                nj_gfx_draw_text(&g, text, 2, 2, ys[a], anchors[a]);
                CHECK(t, px(&ts, 2, 11) == C_BLACK);
                CHECK(t, px(&ts, 3, 11) == C_BLACK);
                CHECK(t, px(&ts, 3, 13) == C_BLACK);
                CHECK(t, px(&ts, 3, 15) == C_WHITE);
                CHECK(t, nj_font_string_width(&f, text, 2) == 2);
                ts_free(&ts);
            }
        }
    }
}

/* Odd widths, unused tail bits, mixed fixed/provider glyphs and clipping
 * must give the same pixels as an unpacked coverage provider. */
static const nj_u1 *packed_lookup(void *ud, nj_u2 ch, int *w, int *h)
{
    if (ch == 'A') return NULL;
    *w = 5; *h = 3;
    return (const nj_u1 *)ud;
}

static void test_packed_coverage(tctx *t)
{
    static const nj_u1 packed[] = { 0x1b, 0xff, 0xe4, 0x7f, 0x5a, 0x3f };
    static const nj_u1 unpacked[] = {
        0, 85, 170, 255, 255, 255, 170, 85, 0, 85, 85, 85, 170, 170, 0
    };
    static const nj_u1 latin[] = { 0x80, 0, 0x80 };
    static const nj_u2 text[] = { 'A', 0x4e2d };
    static const int formats[] = { NJ_PIXFMT_RGB565, NJ_PIXFMT_RGB888, NJ_PIXFMT_ARGB8888 };
    int fmt, variant;
    for (fmt = 0; fmt < 3; fmt++) {
        for (variant = 0; variant < 6; variant++) {
            int clip = variant & 1;
            int mode = variant / 2;
            tsurf a, b;
            nj_gfx ga, gb;
            nj_font f;
            memset(&f, 0, sizeof f);
            f.height = f.ascent = 3;
            f.first_char = f.last_char = 'A';
            f.glyph_w = 1; f.glyph_h = 3; f.bitmap = latin;
            f.glyph_lookup = packed_lookup;
            f.coverage = 2; f.glyph_ud = (void *)packed;
            ts_init(&a, 16, 12, formats[fmt], 8, C_WHITE);
            ts_init(&b, 16, 12, formats[fmt], 8, C_WHITE);
            nj_gfx_init(&ga, &a.s); nj_gfx_init(&gb, &b.s);
            nj_gfx_set_color(&ga, mode == 1 ? 0x80000000u : C_BLACK);
            nj_gfx_set_color(&gb, mode == 1 ? 0x80000000u : C_BLACK);
            if (mode == 2) {
                nj_gfx_set_xor(&ga, 0xFFFF0000u);
                nj_gfx_set_xor(&gb, 0xFFFF0000u);
            }
            if (clip) {
                nj_gfx_set_clip(&ga, 4, 3, 3, 1);
                nj_gfx_set_clip(&gb, 4, 3, 3, 1);
            }
            nj_gfx_set_font(&ga, &f);
            nj_gfx_draw_text(&ga, text, 2, 2, 2, NJ_ANCHOR_TOP);
            f.coverage = 1; f.glyph_ud = (void *)unpacked;
            nj_gfx_set_font(&gb, &f);
            nj_gfx_draw_text(&gb, text, 2, 2, 2, NJ_ANCHOR_TOP);
            CHECK(t, memcmp(a.buf, b.buf, (size_t)a.s.stride * a.s.height) == 0);
            if (!clip && mode == 0) {
                CHECK(t, px(&a, 2, 2) == C_BLACK); /* fixed Latin remains 1bpp */
                CHECK(t, px(&a, 4, 2) != C_WHITE && px(&a, 4, 2) != C_BLACK);
            }
            ts_free(&a); ts_free(&b);
        }
    }
}

/* ------------------------------------------------------------------ */
int main(void)
{
    tctx t;

    memset(&t, 0, sizeof t);
    printf("nano_j2me antialiased text test\n\n");

    test_blend(&t);
    test_clip(&t);
    test_fallback_ascent(&t);
    test_bitmap_unchanged(&t);
    test_full_coverage_plot(&t);
    test_provider_baseline(&t);
    test_packed_coverage(&t);

    printf("\ntest_font_coverage: %u assertions, %u failures -> %s\n",
           t.checks, t.fails, t.fails == 0u ? "PASS" : "FAIL");
    return t.fails == 0u ? 0 : 1;
}
