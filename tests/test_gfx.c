/*
 * nano_j2me - tests/test_gfx.c
 *
 * Assertion-based test for src/gfx/nj_gfx.c: exact pixels after fills, lines,
 * rectangle outlines, arcs and triangles; clip/translate interactions; all
 * eight Sprite transforms; alpha blending against hand-computed values; XOR;
 * image blits, ROPs and masks; a stride-padding guard band fed wildly
 * out-of-range coordinates; and a PPM dump of a demo scene for eyeballing.
 *
 * The six font entry points and nj_gfx_draw_text()/nj_gfx_draw_char() come
 * from tests/stub_font.c (throwaway; src/gfx/nj_font.c owns them for real).
 *
 * Build:
 *   gcc -std=c99 -Wall -Wextra -Werror -pedantic -Iinclude \
 *       -o /tmp/test_gfx tests/test_gfx.c src/gfx/nj_gfx.c tests/stub_font.c -lm
 * Run from the repository root so the demo is written to tests/out/.
 */
#include "nj2me/nj_gfx.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Tiny assertion harness                                              */
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

/* ------------------------------------------------------------------ */
/* Test surfaces: visible area + a sentinel-filled stride guard band    */
/* ------------------------------------------------------------------ */
#define SENTINEL 0xA5u

typedef struct tsurf {
    nj_surface s;
    nj_u1     *buf;
    int        bpp;
    int        pad;      /* guard-band bytes per row */
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
    ts->s.pixels = NULL;
}

/* Every padding byte still holds the sentinel. */
static int ts_pad_ok(const tsurf *ts)
{
    int x, y;

    for (y = 0; y < ts->s.height; y++) {
        const nj_u1 *p = ts->buf + (size_t)y * (size_t)ts->s.stride
                       + (size_t)ts->s.width * (size_t)ts->bpp;
        for (x = 0; x < ts->pad; x++) {
            if (p[x] != SENTINEL) {
                return 0;
            }
        }
    }
    return 1;
}

/* FNV-1a over the whole buffer, padding included. */
static unsigned long ts_checksum(const tsurf *ts)
{
    size_t n = (size_t)ts->s.stride * (size_t)ts->s.height;
    unsigned long h = 2166136261ul;
    size_t i;

    for (i = 0; i < n; i++) {
        h ^= (unsigned long)ts->buf[i];
        h = (h * 16777619ul) & 0xFFFFFFFFul;
    }
    return h;
}

/* Exact bitmap comparison: '#' = ink, '.' = background. */
static void check_bitmap(tctx *t, const tsurf *ts, const char *const *rows,
                         nj_u4 ink, nj_u4 bg, int line)
{
    int x, y, bad = 0;

    for (y = 0; y < ts->s.height && !bad; y++) {
        for (x = 0; x < ts->s.width; x++) {
            nj_u4 got = nj_surface_get(&ts->s, x, y);
            nj_u4 want = (rows[y][x] == '#') ? ink : bg;

            if (got != want) {
                printf("  line %d: pixel (%d,%d) = 0x%08lX want 0x%08lX\n",
                       line, x, y, (unsigned long)got, (unsigned long)want);
                bad = 1;
                break;
            }
        }
    }
    t->checks++;
    if (bad) {
        t->fails++;
    }
}

static void check_px(tctx *t, const tsurf *ts, int x, int y, nj_u4 want,
                     const char *what, int line)
{
    nj_u4 got = nj_surface_get(&ts->s, x, y);

    t->checks++;
    if (got != want) {
        t->fails++;
        printf("  line %d: %s: pixel (%d,%d) = 0x%08lX want 0x%08lX\n",
               line, what, x, y, (unsigned long)got, (unsigned long)want);
    }
}

#define PX(t, ts, x, y, want, what) check_px((t), (ts), (x), (y), (want), (what), __LINE__)

/* Fill the whole surface with `bg` again. */
static void ts_clear(tsurf *ts, nj_u4 bg)
{
    nj_surface_fill(&ts->s, bg);
}

/* ------------------------------------------------------------------ */
/* Colours (all round-trip exactly through RGB565)                     */
/* ------------------------------------------------------------------ */
#define C_BLACK 0xFF000000u
#define C_WHITE 0xFFFFFFFFu
#define C_RED   0xFFFF0000u
#define C_GREEN 0xFF00FF00u
#define C_BLUE  0xFF0000FFu

/* ================================================================== */
/* 1. Formats and surface helpers                                      */
/* ================================================================== */
static void test_formats(tctx *t)
{
    tsurf ts;

    printf("[1] formats, surface get/set/fill\n");

    CHECK(t, nj_surface_format_color(NJ_PIXFMT_RGB565, C_RED) == 0xF800u);
    CHECK(t, nj_surface_format_color(NJ_PIXFMT_RGB565, C_GREEN) == 0x07E0u);
    CHECK(t, nj_surface_format_color(NJ_PIXFMT_RGB565, C_BLUE) == 0x001Fu);
    CHECK(t, nj_surface_format_color(NJ_PIXFMT_RGB565, C_WHITE) == 0xFFFFu);
    CHECK(t, nj_surface_format_color(NJ_PIXFMT_RGB888, 0xFF123456u) == 0x00123456u);
    CHECK(t, nj_surface_format_color(NJ_PIXFMT_ARGB8888, 0x80123456u) == 0x80123456u);

    /* RGB565 readback is expanded and opaque. */
    ts_init(&ts, 4, 4, NJ_PIXFMT_RGB565, 6, C_BLACK);
    nj_surface_set(&ts.s, 1, 1, C_GREEN);
    PX(t, &ts, 1, 1, C_GREEN, "565 set/get round trip");
    PX(t, &ts, 0, 0, C_BLACK, "565 neighbour untouched");
    CHECK(t, nj_surface_get(&ts.s, -1, 0) == 0u);
    CHECK(t, nj_surface_get(&ts.s, 4, 0) == 0u);
    CHECK(t, nj_surface_get(&ts.s, 0, 4) == 0u);
    nj_surface_set(&ts.s, -1, 0, C_RED);            /* must be a no-op */
    nj_surface_set(&ts.s, 4, 4, C_RED);
    PX(t, &ts, 0, 0, C_BLACK, "out-of-range set ignored");
    CHECK(t, ts_pad_ok(&ts));
    ts_free(&ts);

    /* RGB888 */
    ts_init(&ts, 4, 4, NJ_PIXFMT_RGB888, 5, C_WHITE);
    nj_surface_set(&ts.s, 2, 3, 0xFF123456u);
    PX(t, &ts, 2, 3, 0xFF123456u, "888 set/get round trip");
    CHECK(t, ts_pad_ok(&ts));
    ts_free(&ts);

    /* ARGB8888 keeps alpha, even on an odd (unaligned) row pitch. */
    ts_init(&ts, 4, 4, NJ_PIXFMT_ARGB8888, 3, 0x00000000u);
    nj_surface_set(&ts.s, 3, 2, 0x80112233u);
    PX(t, &ts, 3, 2, 0x80112233u, "argb8888 set/get round trip");
    CHECK(t, ts_pad_ok(&ts));
    ts_free(&ts);

    /* nj_gfx_init caches bpp; the surface helpers also work without it. */
    ts_init(&ts, 8, 8, NJ_PIXFMT_RGB565, 4, C_BLACK);
    {
        nj_gfx g;

        nj_gfx_init(&g, &ts.s);
        CHECK(t, ts.s.bpp == 2);
        CHECK(t, g.surf == &ts.s);
        CHECK(t, nj_gfx_get_color(&g) == C_BLACK);
        CHECK(t, nj_gfx_get_alpha(&g) == 255);
        CHECK(t, nj_gfx_get_clip_x(&g) == 0 && nj_gfx_get_clip_y(&g) == 0);
        CHECK(t, nj_gfx_get_clip_w(&g) == 8 && nj_gfx_get_clip_h(&g) == 8);
        {
            int tx = -1, ty = -1;

            nj_gfx_get_translate(&g, &tx, &ty);
            CHECK(t, tx == 0 && ty == 0);
            nj_gfx_get_translate(&g, NULL, NULL);   /* must be callable */
        }
    }
    ts_free(&ts);
}

/* ================================================================== */
/* 2. fill_rect, translate and clipping                                */
/* ================================================================== */
static void test_fill_rect(tctx *t)
{
    tsurf ts;
    nj_gfx g;

    printf("[2] fill_rect, translate, clip\n");

    ts_init(&ts, 16, 8, NJ_PIXFMT_RGB565, 8, C_WHITE);
    nj_gfx_init(&g, &ts.s);
    nj_gfx_set_color(&g, C_RED);
    nj_gfx_fill_rect(&g, 2, 1, 3, 2);
    PX(t, &ts, 2, 1, C_RED, "fill_rect corner");
    PX(t, &ts, 4, 2, C_RED, "fill_rect far corner");
    PX(t, &ts, 1, 1, C_WHITE, "left of fill");
    PX(t, &ts, 5, 1, C_WHITE, "right of fill");
    PX(t, &ts, 2, 0, C_WHITE, "above fill");
    PX(t, &ts, 2, 3, C_WHITE, "below fill");
    CHECK(t, ts_pad_ok(&ts));

    /* Empty / negative extents draw nothing. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_fill_rect(&g, 0, 0, 0, 5);
    nj_gfx_fill_rect(&g, 0, 0, 5, 0);
    nj_gfx_fill_rect(&g, 0, 0, -5, 5);
    nj_gfx_fill_rect(&g, 0, 0, 5, -5);
    PX(t, &ts, 0, 0, C_WHITE, "zero/negative fill_rect is a no-op");

    /* Wildly out-of-range coordinates clamp instead of wrapping. */
    nj_gfx_set_color(&g, C_BLUE);
    nj_gfx_fill_rect(&g, -100000, -100000, 200000, 200000);
    PX(t, &ts, 0, 0, C_BLUE, "huge fill_rect covers the surface");
    PX(t, &ts, 15, 7, C_BLUE, "huge fill_rect covers the far corner");
    ts_clear(&ts, C_WHITE);
    nj_gfx_fill_rect(&g, INT_MAX - 4, INT_MAX - 4, 10, 10);
    nj_gfx_fill_rect(&g, INT_MIN, INT_MIN, 10, 10);
    PX(t, &ts, 0, 0, C_WHITE, "INT_MAX/INT_MIN fill_rect is invisible");

    /* Translation offsets user coordinates. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_set_color(&g, C_RED);
    nj_gfx_translate(&g, 3, 2);
    CHECK(t, nj_gfx_dev_x(&g, 0) == 3 && nj_gfx_dev_y(&g, 0) == 2);
    nj_gfx_fill_rect(&g, 0, 0, 2, 2);
    PX(t, &ts, 3, 2, C_RED, "translated fill origin");
    PX(t, &ts, 4, 3, C_RED, "translated fill far corner");
    PX(t, &ts, 2, 2, C_WHITE, "not drawn before the translated origin");

    /* setClip is in USER coordinates: add the translate. */
    nj_gfx_set_clip(&g, 0, 0, 2, 2);
    CHECK(t, nj_gfx_get_clip_x(&g) == 3 && nj_gfx_get_clip_y(&g) == 2);
    CHECK(t, nj_gfx_get_clip_w(&g) == 2 && nj_gfx_get_clip_h(&g) == 2);
    ts_clear(&ts, C_WHITE);
    nj_gfx_fill_rect(&g, -50, -50, 100, 100);
    PX(t, &ts, 3, 2, C_RED, "clip keeps the origin pixel");
    PX(t, &ts, 4, 3, C_RED, "clip keeps the last pixel");
    PX(t, &ts, 5, 3, C_WHITE, "pixel right of clip untouched");
    PX(t, &ts, 3, 4, C_WHITE, "pixel below clip untouched");
    PX(t, &ts, 2, 2, C_WHITE, "pixel left of clip untouched");

    /* The clip is device space and is not moved by translate(). */
    nj_gfx_translate(&g, 5, 5);
    CHECK(t, nj_gfx_get_clip_x(&g) == 3 && nj_gfx_get_clip_y(&g) == 2);
    PX(t, &ts, 3, 2, C_WHITE, "translate does not repaint");

    /* clipRect intersects in user space. */
    nj_gfx_translate(&g, -5, -5);            /* back to (3,2) */
    nj_gfx_clip_rect(&g, 1, 0, 10, 10);      /* device (4,2) .. */
    CHECK(t, nj_gfx_get_clip_x(&g) == 4 && nj_gfx_get_clip_y(&g) == 2);
    CHECK(t, nj_gfx_get_clip_w(&g) == 1 && nj_gfx_get_clip_h(&g) == 2);
    nj_gfx_clip_rect(&g, 0, 0, 0, 5);        /* empty intersection */
    CHECK(t, nj_gfx_get_clip_w(&g) == 0 && nj_gfx_get_clip_h(&g) == 0);
    ts_clear(&ts, C_WHITE);
    nj_gfx_fill_rect(&g, 0, 0, 100, 100);
    PX(t, &ts, 4, 2, C_WHITE, "empty clip draws nothing");

    /* setClip beyond the surface is intersected with the surface. */
    nj_gfx_set_clip(&g, -1000, -1000, 5000, 5000);
    CHECK(t, nj_gfx_get_clip_x(&g) == 0 && nj_gfx_get_clip_y(&g) == 0);
    CHECK(t, nj_gfx_get_clip_w(&g) == 16 && nj_gfx_get_clip_h(&g) == 8);
    nj_gfx_set_clip(&g, 14, 6, 100, 100);
    CHECK(t, nj_gfx_get_clip_w(&g) == 2 && nj_gfx_get_clip_h(&g) == 2);
    nj_gfx_set_clip(&g, 0, 0, -4, 4);
    CHECK(t, nj_gfx_get_clip_w(&g) == 0 && nj_gfx_get_clip_h(&g) == 0);
    nj_gfx_set_clip(&g, 0, 0, 16, 8);

    /* nj_gfx_clear ignores clip and translate (device space). */
    nj_gfx_translate(&g, 5, 5);
    nj_gfx_set_clip(&g, 1, 1, 2, 2);
    nj_gfx_clear(&g, C_GREEN);
    PX(t, &ts, 0, 0, C_GREEN, "clear paints outside the clip");
    PX(t, &ts, 15, 7, C_GREEN, "clear paints the whole surface");
    CHECK(t, ts_pad_ok(&ts));
    ts_free(&ts);
}

/* ================================================================== */
/* 3. Lines                                                            */
/* ================================================================== */
static void test_lines(tctx *t)
{
    static const char *const diagonal[8] = {
        "#.......",
        ".##.....",
        "...##...",
        ".....#..",
        "........",
        "........",
        "........",
        "........"
    };
    static const char *const dotted[8] = {
        "#.#.#.#.",
        "........",
        "........",
        "........",
        "........",
        "........",
        "........",
        "........"
    };
    tsurf ts;
    nj_gfx g;

    printf("[3] lines (Bresenham, dotted, clipped)\n");

    ts_init(&ts, 8, 8, NJ_PIXFMT_RGB565, 4, C_WHITE);
    nj_gfx_init(&g, &ts.s);
    nj_gfx_set_color(&g, C_BLACK);

    nj_gfx_line(&g, 0, 0, 5, 3);
    check_bitmap(t, &ts, diagonal, C_BLACK, C_WHITE, __LINE__);

    /* Reversed direction paints the same pixels. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_line(&g, 5, 3, 0, 0);
    check_bitmap(t, &ts, diagonal, C_BLACK, C_WHITE, __LINE__);

    /* Single pixel, and vertical/horizontal. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_line(&g, 4, 4, 4, 4);
    PX(t, &ts, 4, 4, C_BLACK, "zero-length line paints one pixel");
    PX(t, &ts, 3, 4, C_WHITE, "zero-length line paints one pixel only");

    ts_clear(&ts, C_WHITE);
    nj_gfx_line(&g, 2, 0, 2, 7);
    PX(t, &ts, 2, 0, C_BLACK, "vertical line top");
    PX(t, &ts, 2, 7, C_BLACK, "vertical line bottom");
    PX(t, &ts, 1, 4, C_WHITE, "vertical line does not bleed left");
    PX(t, &ts, 3, 4, C_WHITE, "vertical line does not bleed right");

    ts_clear(&ts, C_WHITE);
    nj_gfx_line(&g, 0, 6, 7, 6);
    PX(t, &ts, 0, 6, C_BLACK, "horizontal line start");
    PX(t, &ts, 7, 6, C_BLACK, "horizontal line end");
    PX(t, &ts, 7, 5, C_WHITE, "horizontal line does not bleed up");

    /* DOTTED: one pixel on, one off along the path. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_set_stroke(&g, NJ_STROKE_DOTTED);
    nj_gfx_line(&g, 0, 0, 7, 0);
    check_bitmap(t, &ts, dotted, C_BLACK, C_WHITE, __LINE__);
    nj_gfx_set_stroke(&g, NJ_STROKE_SOLID);

    /* Clipping: a line from far outside keeps the same pixels inside. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_set_clip(&g, 2, 2, 4, 4);
    nj_gfx_line(&g, -100, -100, 100, 100);
    PX(t, &ts, 2, 2, C_BLACK, "clipped diagonal start");
    PX(t, &ts, 5, 5, C_BLACK, "clipped diagonal end");
    PX(t, &ts, 1, 1, C_WHITE, "clipped diagonal outside");
    PX(t, &ts, 6, 6, C_WHITE, "clipped diagonal outside (far)");
    PX(t, &ts, 3, 2, C_WHITE, "clipped line stays on the diagonal");

    /* Extreme endpoints must not overflow or hang. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_set_clip(&g, 0, 0, 8, 8);
    nj_gfx_line(&g, INT_MIN, INT_MIN, INT_MAX, INT_MAX);
    nj_gfx_line(&g, INT_MIN, INT_MAX, INT_MAX, INT_MIN);
    nj_gfx_line(&g, 0, 0, INT_MAX, 3);
    nj_gfx_line(&g, -100000, 4, 100000, 4);
    PX(t, &ts, 4, 4, C_BLACK, "extreme lines still draw");
    CHECK(t, ts_pad_ok(&ts));
    ts_free(&ts);
}

/* ================================================================== */
/* 4. Rectangle outlines                                               */
/* ================================================================== */
static void test_rect(tctx *t)
{
    static const char *const outline[10] = {
        "............",
        "............",
        "..#####.....",
        "..#...#.....",
        "..#...#.....",
        "..#####.....",
        "............",
        "............",
        "............",
        "............"
    };
    static const char *const degenerate[10] = {
        "............",
        "............",
        "..#.........",
        "..#.........",
        "..#.........",
        "..#.........",
        "............",
        "............",
        "............",
        "............"
    };
    tsurf ts;
    nj_gfx g;

    printf("[4] drawRect outline (MIDP covers width+1 pixels)\n");

    ts_init(&ts, 12, 10, NJ_PIXFMT_RGB565, 4, C_WHITE);
    nj_gfx_init(&g, &ts.s);
    nj_gfx_set_color(&g, C_BLACK);

    nj_gfx_rect(&g, 2, 2, 4, 3);
    check_bitmap(t, &ts, outline, C_BLACK, C_WHITE, __LINE__);
    CHECK(t, ts_pad_ok(&ts));

    /* width == 0 still draws the (h+1)-tall column; negative draws nothing. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_rect(&g, 2, 2, 0, 3);
    check_bitmap(t, &ts, degenerate, C_BLACK, C_WHITE, __LINE__);

    ts_clear(&ts, C_WHITE);
    nj_gfx_rect(&g, 2, 2, -1, 3);
    nj_gfx_rect(&g, 2, 2, 3, -1);
    PX(t, &ts, 2, 2, C_WHITE, "negative-size drawRect is a no-op");

    /* Zero width and height is a single pixel. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_rect(&g, 5, 5, 0, 0);
    PX(t, &ts, 5, 5, C_BLACK, "drawRect(0,0) is one pixel");

    /* Translating and clipping an outline. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_translate(&g, 1, 1);
    nj_gfx_set_clip(&g, 0, 0, 6, 6);         /* device (1,1)+(6,6) */
    nj_gfx_rect(&g, 0, 0, 10, 10);           /* mostly outside the clip */
    PX(t, &ts, 1, 1, C_BLACK, "clipped outline corner");
    PX(t, &ts, 6, 1, C_BLACK, "clipped outline top edge");
    PX(t, &ts, 1, 6, C_BLACK, "clipped outline left edge");
    PX(t, &ts, 6, 6, C_BLACK, "clipped outline bottom-right corner");
    PX(t, &ts, 7, 7, C_WHITE, "outline clipped away");
    PX(t, &ts, 0, 0, C_WHITE, "outline clipped away (before translate)");
    CHECK(t, ts_pad_ok(&ts));
    ts_free(&ts);
}

/* ================================================================== */
/* 5. Arcs (parametric MIDP angles, pie fills)                         */
/* ================================================================== */
static void test_arcs(tctx *t)
{
    tsurf ts;
    nj_gfx g;
    int ink, x, y;

    printf("[5] drawArc / fillArc\n");

    /* drawArc(0,0,23,23) covers 24x24 pixels; centre is at (12,12). */
    ts_init(&ts, 24, 24, NJ_PIXFMT_RGB565, 4, C_WHITE);
    nj_gfx_init(&g, &ts.s);
    nj_gfx_set_color(&g, C_BLACK);
    nj_gfx_arc(&g, 0, 0, 23, 23, 0, 90);

    PX(t, &ts, 23, 12, C_BLACK, "arc 3 o'clock endpoint");
    PX(t, &ts, 12, 0, C_BLACK, "arc 12 o'clock endpoint");
    PX(t, &ts, 0, 12, C_WHITE, "9 o'clock not in a 0..90 arc");
    PX(t, &ts, 12, 23, C_WHITE, "6 o'clock not in a 0..90 arc");
    PX(t, &ts, 12, 12, C_WHITE, "arc is an outline, not a fill");
    PX(t, &ts, 17, 4, C_BLACK, "arc passes through the upper right");
    PX(t, &ts, 4, 17, C_WHITE, "arc does not reach the lower left");

    ink = 0;
    for (y = 0; y < 24; y++) {
        for (x = 0; x < 24; x++) {
            if (nj_surface_get(&ts.s, x, y) == C_BLACK) {
                ink++;
            }
        }
    }
    /* A quarter circle of radius 12 is ~19 pixels long. */
    CHECK(t, ink >= 15 && ink <= 30);

    /* A full circle touches all four extremes. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_arc(&g, 0, 0, 23, 23, 0, 360);
    PX(t, &ts, 23, 12, C_BLACK, "full arc right");
    PX(t, &ts, 0, 12, C_BLACK, "full arc left");
    PX(t, &ts, 12, 0, C_BLACK, "full arc top");
    PX(t, &ts, 12, 23, C_BLACK, "full arc bottom");
    PX(t, &ts, 12, 12, C_WHITE, "full arc is hollow");

    /* Negative extent sweeps clockwise. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_arc(&g, 0, 0, 23, 23, 0, -90);
    PX(t, &ts, 23, 12, C_BLACK, "clockwise arc starts at 3 o'clock");
    PX(t, &ts, 12, 23, C_BLACK, "clockwise arc ends at 6 o'clock");
    PX(t, &ts, 12, 0, C_WHITE, "clockwise arc does not reach 12 o'clock");

    /* fillArc(0,0,24,24,0,90) is the upper-right pie wedge. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_fill_arc(&g, 0, 0, 24, 24, 0, 90);
    PX(t, &ts, 21, 8, C_BLACK, "pie wedge upper right");
    PX(t, &ts, 12, 10, C_BLACK, "pie wedge near the apex");
    PX(t, &ts, 8, 3, C_WHITE, "pie wedge excludes upper left");
    PX(t, &ts, 21, 21, C_WHITE, "pie wedge excludes lower right");
    PX(t, &ts, 8, 21, C_WHITE, "pie wedge excludes lower left");
    PX(t, &ts, 23, 12, C_WHITE, "pixel centre just outside the wedge");

    /* A 360 degree fill is the whole ellipse. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_fill_arc(&g, 0, 0, 24, 24, 0, 360);
    PX(t, &ts, 12, 12, C_BLACK, "full ellipse centre");
    PX(t, &ts, 0, 12, C_BLACK, "full ellipse left edge");
    PX(t, &ts, 23, 12, C_BLACK, "full ellipse right edge");
    PX(t, &ts, 12, 0, C_BLACK, "full ellipse top edge");
    PX(t, &ts, 12, 23, C_BLACK, "full ellipse bottom edge");
    PX(t, &ts, 0, 0, C_WHITE, "full ellipse excludes the corner");

    /* Half ellipse on the left: 90..270. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_fill_arc(&g, 0, 0, 24, 24, 90, 180);
    PX(t, &ts, 2, 12, C_BLACK, "left half wedge contains 9 o'clock");
    PX(t, &ts, 21, 12, C_WHITE, "left half wedge excludes 3 o'clock");

    /* Degenerate sizes draw nothing. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_fill_arc(&g, 0, 0, 0, 24, 0, 90);
    nj_gfx_fill_arc(&g, 0, 0, 24, 0, 0, 90);
    nj_gfx_arc(&g, 0, 0, -1, 24, 0, 90);
    PX(t, &ts, 12, 12, C_WHITE, "degenerate arcs draw nothing");

    /* Huge and extreme arcs must clamp without hanging. */
    nj_gfx_arc(&g, INT_MIN, INT_MIN, INT_MAX, INT_MAX, 0, 360);
    nj_gfx_fill_arc(&g, INT_MIN, INT_MIN, INT_MAX, INT_MAX, 0, 360);
    nj_gfx_arc(&g, -100000, -100000, 200000, 200000, 45, 270);
    CHECK(t, ts_pad_ok(&ts));
    ts_free(&ts);
}

/* ================================================================== */
/* 6. Rounded rectangles                                               */
/* ================================================================== */
static void test_round_rect(tctx *t)
{
    tsurf ts, ref;
    nj_gfx g;
    int x, y, same;

    printf("[6] round rects\n");

    ts_init(&ts, 24, 24, NJ_PIXFMT_RGB565, 4, C_WHITE);
    nj_gfx_init(&g, &ts.s);
    nj_gfx_set_color(&g, C_BLACK);

    /* fillRoundRect(0,0,20,20,8,8): the corners are cut, the middle is not. */
    nj_gfx_fill_round_rect(&g, 0, 0, 20, 20, 8, 8);
    PX(t, &ts, 10, 10, C_BLACK, "round rect filled centre");
    PX(t, &ts, 0, 0, C_WHITE, "round rect corner cut");
    PX(t, &ts, 1, 0, C_WHITE, "round rect corner cut (2)");
    PX(t, &ts, 0, 1, C_WHITE, "round rect corner cut (3)");
    PX(t, &ts, 2, 0, C_BLACK, "round rect top row starts at the arc");
    PX(t, &ts, 1, 1, C_BLACK, "round rect inset row is filled");
    PX(t, &ts, 0, 2, C_BLACK, "round rect left column resumes");
    PX(t, &ts, 19, 19, C_WHITE, "round rect far corner cut");
    PX(t, &ts, 17, 19, C_BLACK, "round rect bottom row ends at the arc");
    PX(t, &ts, 0, 19, C_WHITE, "round rect bottom-left corner cut");
    PX(t, &ts, 10, 0, C_BLACK, "round rect top edge between the arcs");
    PX(t, &ts, 0, 10, C_BLACK, "round rect left edge between the arcs");
    PX(t, &ts, 19, 10, C_BLACK, "round rect right edge between the arcs");
    PX(t, &ts, 10, 19, C_BLACK, "round rect bottom edge between the arcs");

    /* The outline: straight edges plus four quarter arcs. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_round_rect(&g, 0, 0, 20, 20, 8, 8);
    PX(t, &ts, 5, 0, C_BLACK, "round rect outline top-left junction");
    PX(t, &ts, 17, 0, C_BLACK, "round rect outline top-right junction");
    PX(t, &ts, 10, 0, C_BLACK, "round rect outline top edge");
    PX(t, &ts, 0, 10, C_BLACK, "round rect outline left edge");
    PX(t, &ts, 20, 10, C_BLACK, "round rect outline right edge");
    PX(t, &ts, 10, 20, C_BLACK, "round rect outline bottom edge");
    PX(t, &ts, 0, 0, C_WHITE, "round rect outline corner is rounded");
    PX(t, &ts, 10, 10, C_WHITE, "round rect outline is hollow");

    /* arcWidth/arcHeight <= 0 degrade to a plain rectangle. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_round_rect(&g, 1, 1, 6, 4, 0, 3);
    ts_init(&ref, 24, 24, NJ_PIXFMT_RGB565, 4, C_WHITE);
    {
        nj_gfx g2;

        nj_gfx_init(&g2, &ref.s);
        nj_gfx_set_color(&g2, C_BLACK);
        nj_gfx_rect(&g2, 1, 1, 6, 4);
    }
    same = 1;
    for (y = 0; y < 24 && same; y++) {
        for (x = 0; x < 24; x++) {
            if (nj_surface_get(&ts.s, x, y) != nj_surface_get(&ref.s, x, y)) {
                same = 0;
                break;
            }
        }
    }
    CHECK(t, same);
    ts_free(&ref);

    /* arc sizes larger than the rectangle are clamped, not wrapped. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_fill_round_rect(&g, 0, 0, 10, 10, 100, 100);
    nj_gfx_round_rect(&g, 0, 0, 10, 10, 100, 100);
    PX(t, &ts, 5, 5, C_BLACK, "clamped round rect still paints");

    /* Extreme coordinates. */
    nj_gfx_fill_round_rect(&g, INT_MIN, INT_MIN, INT_MAX, INT_MAX, 100000, 100000);
    nj_gfx_round_rect(&g, -100000, -100000, 200000, 200000, 7, 9);
    CHECK(t, ts_pad_ok(&ts));
    ts_free(&ts);
}

/* ================================================================== */
/* 7. Triangles                                                        */
/* ================================================================== */
static void test_triangles(tctx *t)
{
    static const char *const filled[5] = {
        "#####",
        "####.",
        "###..",
        "##...",
        "#...."
    };
    tsurf ts;
    nj_gfx g;

    printf("[7] triangles\n");

    ts_init(&ts, 5, 5, NJ_PIXFMT_RGB565, 4, C_WHITE);
    nj_gfx_init(&g, &ts.s);
    nj_gfx_set_color(&g, C_BLACK);

    nj_gfx_triangle(&g, 0, 0, 4, 0, 0, 4, 1);
    check_bitmap(t, &ts, filled, C_BLACK, C_WHITE, __LINE__);

    /* Outline: the three edges, hollow inside. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_triangle(&g, 0, 0, 4, 0, 0, 4, 0);
    PX(t, &ts, 0, 0, C_BLACK, "triangle outline vertex");
    PX(t, &ts, 4, 0, C_BLACK, "triangle outline vertex (2)");
    PX(t, &ts, 0, 4, C_BLACK, "triangle outline vertex (3)");
    PX(t, &ts, 2, 0, C_BLACK, "triangle outline top edge");
    PX(t, &ts, 0, 3, C_BLACK, "triangle outline left edge");
    PX(t, &ts, 2, 2, C_BLACK, "triangle outline hypotenuse");
    PX(t, &ts, 1, 1, C_WHITE, "triangle outline is hollow");

    /* Clipped and translated fill. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_translate(&g, 1, 1);
    nj_gfx_set_clip(&g, 0, 0, 3, 3);        /* device (1,1) 3x3 */
    nj_gfx_triangle(&g, 0, 0, 20, 0, 0, 20, 1);
    PX(t, &ts, 1, 1, C_BLACK, "clipped triangle top-left");
    PX(t, &ts, 3, 3, C_BLACK, "clipped triangle bottom-right");
    PX(t, &ts, 4, 4, C_WHITE, "triangle clipped away");

    /* Extreme vertices must not wrap or hang. */
    nj_gfx_set_clip(&g, 0, 0, 5, 5);
    nj_gfx_triangle(&g, INT_MIN, INT_MIN, INT_MAX, INT_MIN, 0, INT_MAX, 1);
    nj_gfx_triangle(&g, INT_MIN, 0, INT_MAX, 0, 0, INT_MAX, 1);
    CHECK(t, ts_pad_ok(&ts));
    ts_free(&ts);
}

/* ================================================================== */
/* 8. Sprite transforms (table driven)                                 */
/* ================================================================== */
static const char *trans_name(int tr)
{
    switch (tr) {
    case NJ_TRANS_NONE:          return "NONE";
    case NJ_TRANS_MIRROR_ROT180: return "MIRROR_ROT180";
    case NJ_TRANS_MIRROR:        return "MIRROR";
    case NJ_TRANS_ROT180:        return "ROT180";
    case NJ_TRANS_MIRROR_ROT270: return "MIRROR_ROT270";
    case NJ_TRANS_ROT90:         return "ROT90";
    case NJ_TRANS_ROT270:        return "ROT270";
    case NJ_TRANS_MIRROR_ROT90:  return "MIRROR_ROT90";
    default:                     return "?";
    }
}

/* Source 3x2:  A B C      A..F are 0xFF010101 .. 0xFF060606
 *              D E F   */
static const char *const TRANS_SRC[2] = { "ABC", "DEF" };

static const struct trans_case {
    int         transform;
    int         tw, th;
    const char *rows[3];
} TRANS_CASES[8] = {
    { NJ_TRANS_NONE,          3, 2, { "ABC", "DEF", "" } },
    { NJ_TRANS_MIRROR_ROT180, 3, 2, { "DEF", "ABC", "" } },
    { NJ_TRANS_MIRROR,        3, 2, { "CBA", "FED", "" } },
    { NJ_TRANS_ROT180,        3, 2, { "FED", "CBA", "" } },
    { NJ_TRANS_MIRROR_ROT270, 2, 3, { "AD", "BE", "CF" } },
    { NJ_TRANS_ROT90,         2, 3, { "DA", "EB", "FC" } },
    { NJ_TRANS_ROT270,        2, 3, { "CF", "BE", "AD" } },
    { NJ_TRANS_MIRROR_ROT90,  2, 3, { "FC", "EB", "DA" } }
};

static void test_transforms(tctx *t)
{
    nj_u4 srcbuf[6];
    nj_image img;
    tsurf ts;
    nj_gfx g;
    int c, i, j, x, y;
    char want[8][9];

    printf("[8] all 8 Sprite transforms (3x2 asymmetric source)\n");

    for (i = 0; i < 6; i++) {
        srcbuf[i] = 0xFF000000u | ((nj_u4)(i + 1) * 0x010101u);
    }
    img.width = 3;
    img.height = 2;
    img.format = NJ_PIXFMT_ARGB8888;
    img.pixels = srcbuf;
    img.has_alpha = 1;

    ts_init(&ts, 8, 8, NJ_PIXFMT_ARGB8888, 4, C_BLACK);
    nj_gfx_init(&g, &ts.s);

    for (c = 0; c < 8; c++) {
        const struct trans_case *tc = &TRANS_CASES[c];
        int bad = 0;

        ts_clear(&ts, C_BLACK);
        nj_gfx_draw_image_rop(&g, &img, 1, 1, 0, 0, 3, 2, NJ_ROP_SET,
                              tc->transform);

        for (y = 0; y < 8; y++) {
            for (x = 0; x < 8; x++) {
                want[y][x] = '.';
            }
            want[y][8] = '\0';
        }
        for (j = 0; j < tc->th; j++) {
            for (i = 0; i < tc->tw; i++) {
                want[1 + j][1 + i] = tc->rows[j][i];
            }
        }
        for (y = 0; y < 8 && !bad; y++) {
            for (x = 0; x < 8; x++) {
                nj_u4 got = nj_surface_get(&ts.s, x, y);
                nj_u4 exp;

                if (want[y][x] == '.') {
                    exp = C_BLACK;
                } else {
                    exp = 0xFF000000u
                        | ((nj_u4)(want[y][x] - 'A' + 1) * 0x010101u);
                }
                if (got != exp) {
                    printf("  FAIL: %s: (%d,%d) = 0x%08lX want 0x%08lX\n",
                           trans_name(tc->transform), x, y,
                           (unsigned long)got, (unsigned long)exp);
                    bad = 1;
                    break;
                }
            }
        }
        t->checks++;
        if (bad) {
            t->fails++;
        }
    }

    /* Sanity: the source table and the source buffer really are A..F. */
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 3; j++) {
            CHECK(t, TRANS_SRC[i][j] == (char)('A' + i * 3 + j));
            CHECK(t, srcbuf[i * 3 + j]
                     == (0xFF000000u | ((nj_u4)(i * 3 + j + 1) * 0x010101u)));
        }
    }
    CHECK(t, ts_pad_ok(&ts));
    ts_free(&ts);
}

/* ================================================================== */
/* 9. Blending and XOR                                                 */
/* ================================================================== */
static void test_blend_xor(tctx *t)
{
    tsurf ts;
    nj_gfx g;

    printf("[9] alpha blending and XOR\n");

    /* 50% red over white: r=255, g=b=(255*127+127)/255=127 -> 0xFFFF7F7F */
    ts_init(&ts, 4, 4, NJ_PIXFMT_ARGB8888, 4, C_WHITE);
    nj_gfx_init(&g, &ts.s);
    nj_gfx_set_color(&g, 0x80FF0000u);
    nj_gfx_fill_rect(&g, 0, 0, 4, 4);
    PX(t, &ts, 0, 0, 0xFFFF7F7Fu, "50% red over white");
    PX(t, &ts, 3, 3, 0xFFFF7F7Fu, "50% red over white (corner)");

    /* 50% red over black in RGB565: r=128 -> 16 -> 0x8000 */
    ts_free(&ts);
    ts_init(&ts, 4, 4, NJ_PIXFMT_RGB565, 4, C_BLACK);
    nj_gfx_init(&g, &ts.s);
    nj_gfx_set_color(&g, 0x80FF0000u);
    nj_gfx_fill_rect(&g, 0, 0, 4, 4);
    PX(t, &ts, 0, 0, 0xFF800000u, "50% red over black (565)");

    /* Fully transparent source paints nothing. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_set_color(&g, 0x00FF0000u);
    nj_gfx_fill_rect(&g, 0, 0, 4, 4);
    PX(t, &ts, 0, 0, C_WHITE, "alpha 0 leaves the destination alone");

    /* NJ_BLEND_NONE ignores alpha entirely. */
    nj_gfx_set_blend(&g, NJ_BLEND_NONE);
    nj_gfx_fill_rect(&g, 0, 0, 4, 4);
    PX(t, &ts, 0, 0, C_RED, "blend NONE ignores alpha");

    /* Alpha survives set_alpha/set_color_rgb round trips. */
    nj_gfx_set_blend(&g, NJ_BLEND_SRC_OVER);
    nj_gfx_set_alpha(&g, 200);
    CHECK(t, nj_gfx_get_alpha(&g) == 200);
    nj_gfx_set_color_rgb(&g, 1, 2, 3);
    CHECK(t, nj_gfx_get_alpha(&g) == 255);
    CHECK(t, nj_gfx_get_color(&g) == 0xFF010203u);

    /* XOR: dst ^= xor_color, and applying it twice restores the pixel. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_set_xor(&g, C_RED);
    nj_gfx_fill_rect(&g, 1, 1, 2, 2);
    PX(t, &ts, 1, 1, 0xFF07FFFFu, "XOR white ^ red");
    nj_gfx_fill_rect(&g, 1, 1, 2, 2);
    PX(t, &ts, 1, 1, C_WHITE, "XOR twice restores");
    nj_gfx_clear_xor(&g);
    nj_gfx_fill_rect(&g, 1, 1, 2, 2);
    PX(t, &ts, 1, 1, C_RED, "clear_xor restores normal painting");

    /* XOR also drives primitives that are not fills. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_set_xor(&g, C_BLUE);
    nj_gfx_line(&g, 0, 0, 3, 0);
    PX(t, &ts, 0, 0, 0xFFFFFF00u, "XOR line");
    PX(t, &ts, 0, 1, C_WHITE, "XOR line only touches its pixels");
    nj_gfx_clear_xor(&g);
    CHECK(t, ts_pad_ok(&ts));
    ts_free(&ts);
}

/* ================================================================== */
/* 10. Images: draw, ROP, region masks, drawRGB                        */
/* ================================================================== */
static void test_images(tctx *t)
{
    /* 4x4 ARGB image; the middle two pixels of row 0 are transparent. */
    nj_u4 buf[16];
    nj_image img;
    tsurf ts, maskts;
    nj_gfx g;
    int i;

    printf("[10] images, ROP, masks, drawRGB\n");

    for (i = 0; i < 16; i++) {
        buf[i] = 0xFF000000u | ((nj_u4)(i + 1) * 0x00010101u);
    }
    buf[1] = 0x00000000u;                       /* fully transparent */
    buf[2] = 0x80008000u;                       /* 50% green        */
    img.width = 4;
    img.height = 4;
    img.format = NJ_PIXFMT_ARGB8888;
    img.pixels = buf;
    img.has_alpha = 1;

    ts_init(&ts, 10, 10, NJ_PIXFMT_ARGB8888, 4, C_WHITE);
    nj_gfx_init(&g, &ts.s);

    /* drawImage: transparent leaves the destination, partial blends. */
    nj_gfx_draw_image(&g, &img, 0, 0, 0, 0, 4, 4);
    PX(t, &ts, 0, 0, buf[0], "drawImage opaque pixel");
    PX(t, &ts, 1, 0, C_WHITE, "drawImage transparent pixel is skipped");
    PX(t, &ts, 2, 0, 0xFF7FBF7Fu, "drawImage 50% green over white");
    PX(t, &ts, 3, 3, buf[15], "drawImage last pixel");

    /* Source rectangle + clipping. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_set_clip(&g, 2, 2, 2, 2);
    nj_gfx_draw_image(&g, &img, 0, 0, 0, 0, 4, 4);
    PX(t, &ts, 2, 2, buf[2 + 2 * 4], "clipped drawImage takes src (2,2)");
    PX(t, &ts, 3, 3, buf[3 + 3 * 4], "clipped drawImage takes src (3,3)");
    PX(t, &ts, 1, 1, C_WHITE, "clipped drawImage outside");
    PX(t, &ts, 4, 4, C_WHITE, "clipped drawImage outside (far)");
    nj_gfx_set_clip(&g, 0, 0, 10, 10);

    /* ROP SET replaces even fully transparent source pixels. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_draw_image_rop(&g, &img, 0, 0, 0, 0, 4, 1, NJ_ROP_SET,
                          NJ_TRANS_NONE);
    PX(t, &ts, 1, 0, C_BLACK, "ROP_SET writes transparent source pixels");

    /* ROP PAINT skips them. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_draw_image_rop(&g, &img, 0, 0, 0, 0, 4, 1, NJ_ROP_PAINT,
                          NJ_TRANS_NONE);
    PX(t, &ts, 1, 0, C_WHITE, "ROP_PAINT skips transparent pixels");
    PX(t, &ts, 0, 0, buf[0], "ROP_PAINT writes opaque pixels");

    /* Translation applies to image blits too. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_translate(&g, 3, 4);
    nj_gfx_draw_image_rop(&g, &img, 0, 0, 0, 0, 4, 1, NJ_ROP_SET,
                          NJ_TRANS_NONE);
    PX(t, &ts, 3, 4, buf[0], "translated image blit");
    PX(t, &ts, 0, 0, C_WHITE, "translated blit leaves the origin alone");
    nj_gfx_translate(&g, -3, -4);

    /* drawRegion with a mask: only opaque mask pixels are written. */
    ts_init(&maskts, 3, 1, NJ_PIXFMT_ARGB8888, 0, C_BLACK);
    maskts.s.bpp = 4;
    nj_surface_set(&maskts.s, 0, 0, 0x00000000u);
    nj_surface_set(&maskts.s, 1, 0, 0xFF000000u);
    nj_surface_set(&maskts.s, 2, 0, 0x00000000u);
    {
        nj_image mask;

        mask.width = 3;
        mask.height = 1;
        mask.format = NJ_PIXFMT_ARGB8888;
        mask.pixels = (nj_u4 *)maskts.s.pixels;
        mask.has_alpha = 1;

        ts_clear(&ts, C_WHITE);
        nj_gfx_draw_region(&g, &img, 0, 0, 3, 1, NJ_TRANS_NONE, 0, 0u,
                           &mask, 1, 1, NJ_ROP_SET);
        PX(t, &ts, 1, 1, buf[0], "masked region: column 0 blocked");
        PX(t, &ts, 2, 1, buf[1], "masked region: column 1 written");
        PX(t, &ts, 3, 1, buf[2], "masked region: column 2 blocked");
        PX(t, &ts, 0, 1, C_WHITE, "masked region outside");

        /* 90 degree rotation swaps the destination extent, so the mask has
         * the transformed dimensions (1 wide x 3 tall) and is indexed by
         * destination-local coordinates. */
        mask.width = 1;
        mask.height = 3;
        nj_surface_set(&maskts.s, 0, 0, 0x00000000u);
        nj_surface_set(&maskts.s, 1, 0, 0xFF000000u);
        nj_surface_set(&maskts.s, 2, 0, 0x00000000u);
        ts_clear(&ts, C_WHITE);
        nj_gfx_draw_region(&g, &img, 0, 0, 3, 1, NJ_TRANS_ROT90, 0, 0u,
                           &mask, 1, 2, NJ_ROP_SET);
        PX(t, &ts, 1, 2, C_WHITE, "rot90 masked region: row 0 blocked");
        PX(t, &ts, 1, 3, buf[1], "rot90 masked region: row 1 written");
        PX(t, &ts, 1, 4, C_WHITE, "rot90 masked region: row 2 blocked");
    }

    /* drawRegion XOR: the sprite's xor colour is used, and painted pixels
     * that are fully transparent are skipped unless the ROP is SET. */
    ts_clear(&ts, C_BLACK);
    nj_gfx_draw_region(&g, &img, 0, 0, 4, 1, NJ_TRANS_NONE, 1, C_GREEN,
                       NULL, 0, 0, NJ_ROP_PAINT);
    PX(t, &ts, 0, 0, C_GREEN, "drawRegion XOR with a zero destination");
    PX(t, &ts, 1, 0, C_BLACK, "drawRegion XOR skips transparent (PAINT)");
    PX(t, &ts, 2, 0, 0xFF00FF80u, "drawRegion XOR of 50% green");
    ts_clear(&ts, C_BLACK);
    nj_gfx_draw_region(&g, &img, 0, 0, 4, 1, NJ_TRANS_NONE, 1, C_GREEN,
                       NULL, 0, 0, NJ_ROP_SET);
    PX(t, &ts, 1, 0, C_GREEN, "drawRegion XOR writes transparent (SET)");

    /* drawRGB: ARGB8888 byte stream, byte scan length, pixel source offset. */
    {
        nj_u1 rgb[2 * 6 * 4];
        int k;

        memset(rgb, 0xEE, sizeof rgb);
        /* Row 0 pixels 0..3 = 0xFF111111.. then 2 padding pixels. */
        for (k = 0; k < 6; k++) {
            nj_u4 v = (k < 4) ? (0xFF101010u * (nj_u4)(k + 1)) : 0xEEEEEEEEu;
            memcpy(rgb + (size_t)k * 4, &v, 4);
        }
        for (k = 0; k < 6; k++) {
            nj_u4 v = (k < 4) ? (0xFF202020u * (nj_u4)(k + 1)) : 0xEEEEEEEEu;
            memcpy(rgb + 24 + (size_t)k * 4, &v, 4);
        }
        ts_clear(&ts, C_WHITE);
        nj_gfx_draw_rgb(&g, rgb, 24, 255, 1, 1, 1, 0, 2, 2, NJ_ROP_SET,
                        NJ_TRANS_NONE);
        PX(t, &ts, 1, 1, 0xFF202020u, "drawRGB src(1,0)");
        PX(t, &ts, 2, 1, 0xFF303030u, "drawRGB src(2,0)");
        PX(t, &ts, 1, 2, 0xFF404040u, "drawRGB src(1,1) with row padding");
        PX(t, &ts, 2, 2, 0xFF606060u, "drawRGB src(2,1) with row padding");
        PX(t, &ts, 0, 1, C_WHITE, "drawRGB honours the source offset");
    }

    /* Whole-surface guard band after all that. */
    CHECK(t, ts_pad_ok(&ts));

    ts_free(&maskts);
    ts_free(&ts);
}

/* ================================================================== */
/* 11. Clipping leaves everything outside untouched                    */
/* ================================================================== */
static void draw_scene(nj_gfx *g, const nj_image *img)
{
    nj_gfx_set_color(g, C_RED);
    nj_gfx_fill_rect(g, 1, 1, 20, 12);
    nj_gfx_set_color(g, C_BLUE);
    nj_gfx_rect(g, 3, 3, 18, 14);
    nj_gfx_set_color(g, C_GREEN);
    nj_gfx_line(g, 0, 0, 31, 23);
    nj_gfx_set_color(g, 0xFF101010u);
    nj_gfx_fill_arc(g, 6, 4, 18, 14, 30, 250);
    nj_gfx_triangle(g, 2, 20, 20, 8, 26, 22, 1);
    nj_gfx_draw_image(g, img, 12, 10, 0, 0, 8, 8);
    nj_gfx_set_stroke(g, NJ_STROKE_DOTTED);
    nj_gfx_set_color(g, C_WHITE);
    nj_gfx_line(g, 1, 22, 30, 2);
    nj_gfx_set_stroke(g, NJ_STROKE_SOLID);
}

static void test_clip_isolation(tctx *t)
{
    nj_u4 imgpx[64];
    nj_image img;
    tsurf ref, clipped, expected;
    nj_gfx g;
    int x, y, bad;

    printf("[11] clip isolation (checksum against a reference render)\n");

    for (x = 0; x < 64; x++) {
        imgpx[x] = 0xC0000000u | ((nj_u4)(x * 3 + 1) & 0x00FFFFFFu);
    }
    img.width = 8;
    img.height = 8;
    img.format = NJ_PIXFMT_ARGB8888;
    img.pixels = imgpx;
    img.has_alpha = 1;

    ts_init(&ref, 32, 24, NJ_PIXFMT_ARGB8888, 5, C_BLACK);
    ts_init(&clipped, 32, 24, NJ_PIXFMT_ARGB8888, 5, C_BLACK);
    ts_init(&expected, 32, 24, NJ_PIXFMT_ARGB8888, 5, C_BLACK);

    nj_gfx_init(&g, &ref.s);
    draw_scene(&g, &img);

    nj_gfx_init(&g, &clipped.s);
    nj_gfx_set_clip(&g, 7, 5, 17, 13);       /* device rect (7,5) 17x13 */
    draw_scene(&g, &img);

    /* Reference "what the clip allows": background everywhere, the unclipped
     * render inside the clip rectangle. */
    for (y = 0; y < 24; y++) {
        for (x = 0; x < 32; x++) {
            nj_u4 v = C_BLACK;

            if (x >= 7 && x < 24 && y >= 5 && y < 18) {
                v = nj_surface_get(&ref.s, x, y);
            }
            nj_surface_set(&expected.s, x, y, v);
        }
    }

    bad = 0;
    for (y = 0; y < 24 && !bad; y++) {
        for (x = 0; x < 32; x++) {
            if (nj_surface_get(&clipped.s, x, y)
                != nj_surface_get(&expected.s, x, y)) {
                printf("  FAIL: clipped scene differs at (%d,%d)\n", x, y);
                bad = 1;
                break;
            }
        }
    }
    t->checks++;
    if (bad) {
        t->fails++;
    }
    CHECK(t, ts_checksum(&clipped) == ts_checksum(&expected));
    /* The scene really did draw something, so the comparison above means
     * something. */
    CHECK(t, ts_checksum(&ref) != ts_checksum(&expected));
    CHECK(t, ts_pad_ok(&ref) && ts_pad_ok(&clipped) && ts_pad_ok(&expected));

    ts_free(&expected);
    ts_free(&clipped);
    ts_free(&ref);
}

/* ================================================================== */
/* 12. Stride guard band vs. wildly out-of-range coordinates           */
/* ================================================================== */
static void wild_batch(nj_gfx *g, const nj_image *img, const nj_u1 *rgb)
{
    const int big = INT_MAX;
    const int neg = INT_MIN;

    nj_gfx_set_color(g, C_RED);
    nj_gfx_pixel(g, neg, neg);
    nj_gfx_pixel(g, big, big);
    nj_gfx_pixel(g, 100000, -100000);
    nj_gfx_line(g, neg, neg, big, big);
    nj_gfx_line(g, neg, big, big, neg);
    nj_gfx_line(g, -100000, 5, 100000, 5);
    nj_gfx_rect(g, neg, neg, big, big);
    nj_gfx_rect(g, -100000, -100000, 200000, 200000);
    nj_gfx_fill_rect(g, neg, neg, big, big);
    nj_gfx_fill_rect(g, -100000, -100000, 200000, 200000);
    nj_gfx_round_rect(g, neg, neg, big, big, big, big);
    nj_gfx_fill_round_rect(g, -100000, -100000, 200000, 200000, 99999, 99999);
    nj_gfx_arc(g, neg, neg, big, big, neg, big);
    nj_gfx_arc(g, -100000, -100000, 200000, 200000, 0, 360);
    nj_gfx_fill_arc(g, neg, neg, big, big, 0, 360);
    nj_gfx_fill_arc(g, -100000, 3, 200000, 9, -720, 1440);
    nj_gfx_triangle(g, neg, neg, big, neg, 0, big, 1);
    nj_gfx_triangle(g, neg, big, big, neg, 3, 3, 0);
    nj_gfx_draw_image(g, img, neg, neg, neg, neg, big, big);
    nj_gfx_draw_image(g, img, 100000, -100000, 0, 0, 8, 8);
    nj_gfx_draw_image_rop(g, img, neg, neg, 0, 0, 8, 8, NJ_ROP_SET,
                          NJ_TRANS_MIRROR_ROT90);
    nj_gfx_draw_image_rop(g, img, big, big, 0, 0, 8, 8, NJ_ROP_PAINT,
                          NJ_TRANS_ROT270);
    nj_gfx_draw_rgb(g, rgb, 32, 128, neg, neg, 0, 0, 8, 8, NJ_ROP_PAINT,
                    NJ_TRANS_ROT90);
    nj_gfx_draw_region(g, img, 0, 0, 8, 8, NJ_TRANS_ROT180, 1, C_GREEN,
                       NULL, neg, big, NJ_ROP_SET);
    nj_gfx_clear(g, C_BLUE);
}

static void test_guard_band(tctx *t)
{
    nj_u4 imgpx[64];
    nj_u1 rgb[8 * 8 * 4];
    nj_image img;
    tsurf ts;
    nj_gfx g;
    int i;

    printf("[12] stride guard band with extreme coordinates\n");

    for (i = 0; i < 64; i++) {
        imgpx[i] = 0xFF000000u | ((nj_u4)i * 0x00010101u);
    }
    for (i = 0; i < (int)sizeof rgb; i++) {
        rgb[i] = (nj_u1)(i * 7);
    }
    img.width = 8;
    img.height = 8;
    img.format = NJ_PIXFMT_ARGB8888;
    img.pixels = imgpx;
    img.has_alpha = 1;

    /* 16-bit panel with a generous pad, and a 32-bit panel with an odd pad so
     * that every row is misaligned for 4-byte access. */
    ts_init(&ts, 21, 9, NJ_PIXFMT_RGB565, 16, C_BLACK);
    nj_gfx_init(&g, &ts.s);
    wild_batch(&g, &img, rgb);
    CHECK(t, ts_pad_ok(&ts));
    ts_free(&ts);

    ts_init(&ts, 21, 9, NJ_PIXFMT_ARGB8888, 13, 0x00000000u);
    nj_gfx_init(&g, &ts.s);
    wild_batch(&g, &img, rgb);
    CHECK(t, ts_pad_ok(&ts));

    /* Same batch, but everything confined to a tiny clip. */
    nj_gfx_set_clip(&g, 2, 2, 3, 3);
    wild_batch(&g, &img, rgb);
    CHECK(t, ts_pad_ok(&ts));
    ts_free(&ts);

    /* RGB888 too. */
    ts_init(&ts, 21, 9, NJ_PIXFMT_RGB888, 7, C_BLACK);
    nj_gfx_init(&g, &ts.s);
    wild_batch(&g, &img, rgb);
    CHECK(t, ts_pad_ok(&ts));
    ts_free(&ts);
}

/* ================================================================== */
/* 13. Text anchors (through the throwaway stub font)                  */
/* ================================================================== */
static void test_text_anchors(tctx *t)
{
    static const nj_u2 two[2] = { 'A', 'B' };
    tsurf ts;
    nj_gfx g;

    printf("[13] draw_text anchor maths (stub font)\n");

    /* The stub font is a 6x8 cell that paints a hollow 5x7 box at the cell
     * origin, so "AB" is 12 wide and 8 tall. */
    ts_init(&ts, 48, 32, NJ_PIXFMT_RGB565, 4, C_WHITE);
    nj_gfx_init(&g, &ts.s);
    nj_gfx_set_color(&g, C_BLACK);

    nj_gfx_draw_text(&g, two, 2, 20, 20, NJ_ANCHOR_TOP | NJ_ANCHOR_LEFT);
    PX(t, &ts, 20, 20, C_BLACK, "TOP|LEFT starts at the anchor");
    PX(t, &ts, 26, 20, C_BLACK, "second cell at x+6");
    PX(t, &ts, 22, 23, C_WHITE, "glyph box is hollow");
    PX(t, &ts, 19, 20, C_WHITE, "nothing left of the anchor");

    ts_clear(&ts, C_WHITE);
    nj_gfx_draw_text(&g, two, 2, 20, 20, NJ_ANCHOR_TOP | NJ_ANCHOR_HCENTER);
    PX(t, &ts, 14, 20, C_BLACK, "TOP|HCENTER starts at x - width/2");
    PX(t, &ts, 19, 20, C_WHITE, "HCENTER leaves the anchor column empty");

    ts_clear(&ts, C_WHITE);
    nj_gfx_draw_text(&g, two, 2, 20, 20, NJ_ANCHOR_TOP | NJ_ANCHOR_RIGHT);
    PX(t, &ts, 8, 20, C_BLACK, "TOP|RIGHT starts at x - width");
    PX(t, &ts, 19, 20, C_BLACK, "RIGHT text reaches the anchor - 1");

    ts_clear(&ts, C_WHITE);
    nj_gfx_draw_text(&g, two, 2, 20, 20, NJ_ANCHOR_BOTTOM | NJ_ANCHOR_LEFT);
    PX(t, &ts, 20, 12, C_BLACK, "BOTTOM puts the box above the anchor (y-8)");
    PX(t, &ts, 20, 20, C_WHITE, "BOTTOM leaves the anchor row empty");

    ts_clear(&ts, C_WHITE);
    nj_gfx_draw_text(&g, two, 2, 20, 20, 64 | NJ_ANCHOR_LEFT);  /* BASELINE */
    PX(t, &ts, 20, 14, C_BLACK, "BASELINE puts the box at y - ascent (y-6)");

    ts_clear(&ts, C_WHITE);
    nj_gfx_draw_text(&g, two, 2, 20, 20, 0);                   /* == TOP|LEFT */
    PX(t, &ts, 20, 20, C_BLACK, "anchor 0 means TOP|LEFT");

    /* Text obeys clip and translate like any other primitive. */
    ts_clear(&ts, C_WHITE);
    nj_gfx_translate(&g, 2, 3);
    nj_gfx_set_clip(&g, 0, 0, 7, 7);
    nj_gfx_draw_text(&g, two, 2, 0, 0, NJ_ANCHOR_TOP | NJ_ANCHOR_LEFT);
    PX(t, &ts, 2, 3, C_BLACK, "translated text origin");
    PX(t, &ts, 8, 3, C_WHITE, "text clipped on the right");
    nj_gfx_translate(&g, -2, -3);
    nj_gfx_set_clip(&g, 0, 0, 48, 32);

    nj_gfx_draw_char(&g, 'A', 40, 28);
    PX(t, &ts, 40, 28, C_BLACK, "draw_char honours the glyph origin");
    CHECK(t, ts_pad_ok(&ts));
    ts_free(&ts);
}

/* ================================================================== */
/* 14. Demo scene dump                                                 */
/* ================================================================== */
static void write_ppm_demo(tctx *t)
{
    tsurf ts;
    nj_gfx g;
    FILE *f;
    const char *path = "tests/out/gfx_demo.ppm";
    int x, y;
    unsigned long written;

    printf("[14] PPM demo dump\n");

    ts_init(&ts, 200, 140, NJ_PIXFMT_RGB565, 0, 0xFF102040u);
    nj_gfx_init(&g, &ts.s);

    /* Sky gradient bands. */
    for (y = 0; y < 140; y += 10) {
        nj_gfx_set_color(&g, 0xFF000000u | ((nj_u4)(y * 3 / 4) << 16)
                               | ((nj_u4)(y * 2 / 3) << 8) | 0x60u);
        nj_gfx_fill_rect(&g, 0, y, 200, 10);
    }

    /* A big filled circle with a wedge cut out, plus its outline. */
    nj_gfx_set_color(&g, 0xFFFFD040u);
    nj_gfx_fill_arc(&g, 20, 20, 80, 80, 30, 300);
    nj_gfx_set_color(&g, 0xFF101010u);
    nj_gfx_arc(&g, 20, 20, 80, 80, 0, 360);

    /* Rounded rectangle, outline and fill. */
    nj_gfx_set_color(&g, 0xFF205020u);
    nj_gfx_fill_round_rect(&g, 115, 15, 70, 45, 24, 20);
    nj_gfx_set_color(&g, 0xFFA0FFA0u);
    nj_gfx_round_rect(&g, 115, 15, 70, 45, 24, 20);

    /* Triangle and a dotted frame. */
    nj_gfx_set_color(&g, 0xFFC04040u);
    nj_gfx_triangle(&g, 20, 120, 80, 60, 130, 120, 1);
    nj_gfx_set_color(&g, 0xFFFFFFFFu);
    nj_gfx_set_stroke(&g, NJ_STROKE_DOTTED);
    nj_gfx_rect(&g, 2, 2, 195, 135);
    nj_gfx_set_stroke(&g, NJ_STROKE_SOLID);

    /* Blit a small ARGB sprite (a checkerboard with an alpha ramp). */
    {
        nj_u4 px[16 * 16];
        nj_image img;
        int i;

        for (i = 0; i < 16 * 16; i++) {
            nj_u4 a = (nj_u4)(0x40 + (i / 16) * 12);
            nj_u4 c = ((i / 4 + i % 4) & 1) ? 0x00FFE0A0u : 0x004080FFu;
            px[i] = (a << 24) | c;
        }
        img.width = 16;
        img.height = 16;
        img.format = NJ_PIXFMT_ARGB8888;
        img.pixels = px;
        img.has_alpha = 1;
        nj_gfx_draw_image(&g, &img, 150, 90, 0, 0, 16, 16);
        nj_gfx_draw_image_rop(&g, &img, 170, 90, 0, 0, 16, 16, NJ_ROP_PAINT,
                              NJ_TRANS_ROT90);
        nj_gfx_set_color(&g, 0xFFFFFFFFu);
        nj_gfx_set_font(&g, NULL);
        {
            static const nj_u2 msg[5] = { 'n', 'j', '2', 'm', 'e' };

            nj_gfx_draw_text(&g, msg, 5, 100, 130,
                             NJ_ANCHOR_BOTTOM | NJ_ANCHOR_HCENTER);
        }
        nj_gfx_set_xor(&g, 0x00FFFFFFu);
        nj_gfx_line(&g, 120, 120, 195, 70);
        nj_gfx_clear_xor(&g);
    }

    f = fopen(path, "wb");
    if (!f) {
        path = "gfx_demo.ppm";
        f = fopen(path, "wb");
    }
    CHECK(t, f != NULL);
    if (!f) {
        ts_free(&ts);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", ts.s.width, ts.s.height);
    written = 0;
    for (y = 0; y < ts.s.height; y++) {
        for (x = 0; x < ts.s.width; x++) {
            nj_u4 v = nj_surface_get(&ts.s, x, y);
            unsigned char rgb[3];

            rgb[0] = (unsigned char)((v >> 16) & 0xFFu);
            rgb[1] = (unsigned char)((v >> 8) & 0xFFu);
            rgb[2] = (unsigned char)(v & 0xFFu);
            written += (unsigned long)fwrite(rgb, 1u, 3u, f);
        }
    }
    fclose(f);
    CHECK(t, written == (unsigned long)ts.s.width * (unsigned long)ts.s.height * 3ul);
    printf("      wrote %s (%lu bytes of pixels)\n", path, written);
    CHECK(t, ts_pad_ok(&ts));
    ts_free(&ts);
}

/* ================================================================== */
int main(void)
{
    tctx t;

    memset(&t, 0, sizeof t);
    printf("nj_gfx rasterizer test\n\n");

    test_formats(&t);
    test_fill_rect(&t);
    test_lines(&t);
    test_rect(&t);
    test_arcs(&t);
    test_round_rect(&t);
    test_triangles(&t);
    test_transforms(&t);
    test_blend_xor(&t);
    test_images(&t);
    test_clip_isolation(&t);
    test_guard_band(&t);
    test_text_anchors(&t);
    write_ppm_demo(&t);

    printf("\ntest_gfx: %u assertions, %u failures -> %s\n",
           t.checks, t.fails, t.fails == 0u ? "PASS" : "FAIL");
    return t.fails == 0u ? 0 : 1;
}
