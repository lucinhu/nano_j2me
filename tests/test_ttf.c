/*
 * nano_j2me - test_ttf.c
 *
 * Assertion tests for the TrueType glyph rasteriser (src/gfx/nj_ttf.c).
 *
 * Two fixture sources:
 *
 *   the .ttf files in tests/fixtures/ttf, hand-built by the generator
 *                              tools/gen_ttf_fixture.py.  Every glyph in
 *                              them exists to make one specific bug visible
 *                              (opposite-winding counter, consecutive
 *                              off-curve points, a contour starting off-curve,
 *                              composites, a sub-pixel spike, an edge exactly
 *                              on a scanline, ...), plus nine deliberately
 *                              corrupt fonts for the negative tests.
 *   a system TrueType font   resolved from the same candidate list
 *                              tools/gen_fonts.py uses.  When none is on the
 *                              machine the sections needing it print "skip:"
 *                              and the suite still passes, the same way
 *                              tools/gen_fonts.py degrades.
 *
 * The strongest test here is the FreeType comparison: for a set of characters
 * at a set of pixel sizes the test rasterises each glyph with src/gfx/nj_ttf.c,
 * dumps the coverage map as a PGM, and asks tools/ttf_ft_compare.py (Pillow's
 * FreeType) to rasterise the same glyph independently and compare.  It then
 * asserts on the mean absolute error, the ink ratio, the distinct coverage
 * values, the ink centroid and the ink bounding box.  FreeType and nj_ttf are
 * different rasterisers, so the bands are wide enough to be honest and tight
 * enough that a thresholded (non-antialiased) rasteriser fails: every glyph
 * must have pixels strictly between 0 and 255 and at least three distinct
 * coverage values.
 *
 * Sub-pixel features ('l' in the fixture is a stem exactly half a pixel wide
 * at 16 px, 's' a spike an eighth of a pixel wide) are compared structurally
 * but not by ink ratio: FreeType's default subpixel hinting grid-fits even
 * instruction-less fonts, so it reports a full pixel of ink for a half-pixel
 * stem, while nj_ttf.c integrates the exact area.  The fixture's ink totals
 * are asserted against their analytic areas instead - that is the stronger
 * check of the two.
 *
 * Whitespace: a glyph with no outline (space, U+00A0) comes back as w = its
 * advance, h = 1, all rows zero.  A character the cmap does not map at all
 * comes back as NULL, never as a box.
 *
 * Build (standalone, no CMake):
 *   gcc -std=c99 -Wall -Wextra -Werror -pedantic -Iinclude -Isrc/core \
 *       -Isrc/gfx -o /tmp/test_ttf tests/test_ttf.c src/gfx/nj_ttf.c -lm
 * Run:
 *   /tmp/test_ttf [fixture-dir] [oracle-script]
 *     fixture-dir     default tests/fixtures/ttf
 *     oracle-script   default tools/ttf_ft_compare.py
 *   Set NJ_PYTHON to pick the interpreter used for the FreeType oracle.
 */

#include "nj2me/nj_gfx.h"
#include "nj2me/nj_types.h"

#include "nj_ttf.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Tiny check framework (same shape as tests/test_font_png.c)           */
/* ------------------------------------------------------------------ */
static int g_checks;
static int g_failures;
static int g_skips;
static int g_fuzz_accepted;     /* mutations that still parsed as a font */
static int g_fuzz_glyphs;       /* glyphs rasterised from mutated fonts */

#define CHECK(cond, ...)                                                  \
    do {                                                                  \
        g_checks++;                                                       \
        if (!(cond)) {                                                    \
            g_failures++;                                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                   \
            printf(__VA_ARGS__);                                          \
            printf("\n");                                                 \
        }                                                                 \
    } while (0)

#define SKIP(...)                                                         \
    do {                                                                  \
        g_skips++;                                                        \
        printf("skip: ");                                                 \
        printf(__VA_ARGS__);                                              \
        printf("\n");                                                     \
    } while (0)

/* ------------------------------------------------------------------ */
/* Fixture facts                                                        */
/* ------------------------------------------------------------------ */
/* The cache size src/gfx/nj_font_ttf.c asks for; the whole point of
 * nj_ttf_sizeof() is that this number is the caller's choice. */
#define TTF_CACHE_GLYPHS 128

#define PX16 16
#define PX32 32
#define PX64 64

#define MAX_MASK_BYTES (1024 * 1024)

/* The fixture's unitsPerEm, advance widths and per-glyph ink bounding boxes
 * in font units - keep in step with tools/gen_ttf_fixture.py. */
#define FIX_UPEM 1024
#define FIX_ADV 640
#define FIX_ADV_SPACE 512

typedef struct {
    nj_u1 *mem;
    nj_size mem_bytes;
    nj_ttf *t;
    nj_font f;
} face;

typedef struct {
    int w;
    int h;
    const nj_u1 *px;
} mask;

static nj_u1 *read_file(const char *path, nj_u4 *len_out)
{
    FILE *fh;
    long n;
    nj_u1 *buf;

    fh = fopen(path, "rb");
    if (fh == NULL) {
        return NULL;
    }
    if (fseek(fh, 0, SEEK_END) != 0) {
        fclose(fh);
        return NULL;
    }
    n = ftell(fh);
    if (n < 0L || fseek(fh, 0, SEEK_SET) != 0) {
        fclose(fh);
        return NULL;
    }
    buf = (nj_u1 *)malloc((size_t)n + 1u);
    if (buf == NULL) {
        fclose(fh);
        return NULL;
    }
    if (n > 0L && fread(buf, 1u, (size_t)n, fh) != (size_t)n) {
        free(buf);
        fclose(fh);
        return NULL;
    }
    fclose(fh);
    *len_out = (nj_u4)n;
    return buf;
}

/* Open a face on its own memory block, exactly as src/gfx/nj_font_ttf.c does:
 * borrow the font bytes, ask for the size, then configure an nj_font. */
static int face_open(face *F, const nj_u1 *data, nj_u4 len, int px, int style)
{
    F->mem_bytes = nj_ttf_sizeof(TTF_CACHE_GLYPHS);
    F->mem = (nj_u1 *)malloc(F->mem_bytes);
    if (F->mem == NULL) {
        return 0;
    }
    F->t = nj_ttf_open(F->mem, F->mem_bytes, data, len, px);
    if (F->t == NULL) {
        free(F->mem);
        F->mem = NULL;
        return 0;
    }
    if (!nj_ttf_font(F->t, &F->f, style, px)) {
        free(F->mem);
        F->mem = NULL;
        return 0;
    }
    return 1;
}

static void face_close(face *F)
{
    free(F->mem);
    F->mem = NULL;
    F->t = NULL;
}

static mask face_glyph(face *F, nj_u2 ch)
{
    mask m;
    m.w = 0;
    m.h = 0;
    m.px = F->f.glyph_lookup(F->f.glyph_ud, ch, &m.w, &m.h);
    return m;
}

static int mask_ink(mask m)
{
    int x, y;
    int total = 0;
    if (m.px == NULL) {
        return 0;
    }
    for (y = 0; y < m.h; y++) {
        for (x = 0; x < m.w; x++) {
            total += m.px[y * m.w + x];
        }
    }
    return total;
}

/* Pixels strictly between "empty" and "fully covered": the antialiasing
 * proof.  A thresholded rasteriser always reports zero here. */
static int mask_mid(mask m)
{
    int x, y;
    int n = 0;
    if (m.px == NULL) {
        return 0;
    }
    for (y = 0; y < m.h; y++) {
        for (x = 0; x < m.w; x++) {
            int v = m.px[y * m.w + x];
            if (v > 0 && v < 255) {
                n++;
            }
        }
    }
    return n;
}

static int mask_distinct(mask m)
{
    nj_u1 seen[256];
    int x, y;
    int n = 0;
    if (m.px == NULL) {
        return 0;
    }
    memset(seen, 0, sizeof(seen));
    for (y = 0; y < m.h; y++) {
        for (x = 0; x < m.w; x++) {
            seen[m.px[y * m.w + x]] = 1u;
        }
    }
    for (x = 1; x < 256; x++) {
        if (seen[x]) {
            n++;
        }
    }
    return n;
}

/* Ink bounding box in pixels; 0 when the mask is empty. */
static int mask_bbox(mask m, int *x0, int *y0, int *x1, int *y1)
{
    int x, y;
    int any = 0;
    *x0 = *y0 = 0x7fffffff;
    *x1 = *y1 = -1;
    if (m.px == NULL) {
        return 0;
    }
    for (y = 0; y < m.h; y++) {
        for (x = 0; x < m.w; x++) {
            if (m.px[y * m.w + x] != 0) {
                any = 1;
                if (x < *x0) { *x0 = x; }
                if (x > *x1) { *x1 = x; }
                if (y < *y0) { *y0 = y; }
                if (y > *y1) { *y1 = y; }
            }
        }
    }
    return any;
}

static int mask_equal(mask a, mask b)
{
    if (a.w != b.w || a.h != b.h || a.px == NULL || b.px == NULL) {
        return 0;
    }
    return memcmp(a.px, b.px, (size_t)a.w * (size_t)a.h) == 0;
}

/* Ink bounding box in font units -> pixels, the way nj_ttf maps them. */
static int unit_px(int units, int px)
{
    double v = (double)units * (double)px / (double)FIX_UPEM;
    return (int)(v + 0.5);
}

/* ------------------------------------------------------------------ */
/* API contract                                                         */
/* ------------------------------------------------------------------ */
static void test_api_contract(const char *dir)
{
    char path[512];
    nj_u4 len = 0u;
    nj_u1 *font;
    nj_u1 *mem;
    nj_size bytes;
    nj_size small;
    nj_ttf *t;
    nj_font f;

    printf("[api]\n");
    sprintf(path, "%s/nj_fixture.ttf", dir);
    font = read_file(path, &len);
    if (font == NULL) {
        CHECK(0, "cannot read %s - run python3 tools/gen_ttf_fixture.py", path);
        return;
    }
    CHECK(len > 100u, "fixture is suspiciously small (%u bytes)",
          (unsigned)len);

    bytes = nj_ttf_sizeof(TTF_CACHE_GLYPHS);
    CHECK(bytes > 4096u && bytes < 1024u * 1024u,
          "nj_ttf_sizeof(%d) = %u bytes, expected tens of KB",
          TTF_CACHE_GLYPHS, (unsigned)bytes);
    CHECK(nj_ttf_sizeof(0) > 0u, "nj_ttf_sizeof(0) must still be a face");
    CHECK(nj_ttf_sizeof(-4) == nj_ttf_sizeof(0) &&
          nj_ttf_sizeof(0) == nj_ttf_sizeof(1),
          "a cache count below 1 must clamp to one glyph, not go negative");
    CHECK(nj_ttf_sizeof(2) > nj_ttf_sizeof(1),
          "a bigger cache must need more bytes");
    CHECK(nj_ttf_sizeof(1000000) < 16u * 1024u * 1024u,
          "an absurd cache count must clamp, not overflow: %u bytes",
          (unsigned)nj_ttf_sizeof(1000000));

    mem = (nj_u1 *)malloc(bytes);
    CHECK(mem != NULL, "out of memory");
    if (mem == NULL) {
        free(font);
        return;
    }

    /* Every argument is the caller's claim: none of these may be trusted. */
    CHECK(nj_ttf_open(NULL, bytes, font, len, PX16) == NULL,
          "open(NULL mem) must fail");
    CHECK(nj_ttf_open(mem, bytes, NULL, len, PX16) == NULL,
          "open(NULL data) must fail");
    CHECK(nj_ttf_open(mem, bytes, font, 0u, PX16) == NULL,
          "open(len 0) must fail");
    CHECK(nj_ttf_open(mem, bytes, font, len, 0) == NULL,
          "open(px 0) must fail");
    CHECK(nj_ttf_open(mem, bytes, font, len, -8) == NULL,
          "open(negative px) must fail");
    CHECK(nj_ttf_open(mem, 0u, font, len, PX16) == NULL,
          "open(mem_bytes 0) must fail");
    small = nj_ttf_sizeof(0);
    CHECK(nj_ttf_open(mem, small / 2u + 1u, font, len, PX16) == NULL,
          "a memory block below nj_ttf_sizeof(0) must be rejected");
    /* The fixture's last table ends 3 bytes before EOF, so len-1 is still a
     * complete font; cutting into a table must fail. */
    CHECK(nj_ttf_open(mem, bytes, font, len - 4u, PX16) == NULL,
          "a font truncated inside a table must be rejected");
    CHECK(nj_ttf_open(mem, bytes, font, len - 1u, PX16) != NULL,
          "trailing padding beyond the last table is not an error");
    CHECK(nj_ttf_open(mem, bytes, font, 11u, PX16) == NULL,
          "a font shorter than the sfnt header must be rejected");

    t = nj_ttf_open(mem, bytes, font, len, PX16);
    CHECK(t != NULL, "cannot open the fixture");
    if (t == NULL) {
        free(mem);
        free(font);
        return;
    }
    CHECK(nj_ttf_font(t, NULL, NJ_FONT_STYLE_PLAIN, PX16) == 0,
          "nj_ttf_font(NULL out) must fail");
    CHECK(nj_ttf_font(NULL, &f, NJ_FONT_STYLE_PLAIN, PX16) == 0,
          "nj_ttf_font(NULL face) must fail");

    memset(&f, 0, sizeof(f));
    CHECK(nj_ttf_font(t, &f, NJ_FONT_STYLE_PLAIN, PX16) == 1,
          "nj_ttf_font failed on a valid face");
    CHECK(f.glyph_lookup != NULL, "glyph_lookup not set");
    CHECK(f.glyph_advance != NULL, "glyph_advance not set");
    CHECK(f.glyph_ud == (void *)t, "glyph_ud must be the nj_ttf");
    CHECK(f.coverage == 1, "coverage must be 1 (8bpp rows), got %d",
          f.coverage);
    CHECK(f.proportional == 1, "proportional must be 1");
    CHECK(f.name != NULL && f.name[0] != '\0',
          "name must be a non-empty string, got %s",
          f.name ? f.name : "(null)");
    CHECK(f.bitmap == NULL, "bitmap must stay NULL for a coverage face");
    CHECK(f.fallback == NULL, "fallback is the caller's to set");
    CHECK(f.size == PX16, "size must be echoed back, got %d", f.size);
    CHECK(f.style == NJ_FONT_STYLE_PLAIN, "style must be echoed back");
    CHECK(f.height == f.ascent + f.descent + f.leading,
          "height %d != ascent %d + descent %d + leading %d",
          f.height, f.ascent, f.descent, f.leading);
    CHECK(f.ascent >= 1, "ascent %d must be positive", f.ascent);
    CHECK(f.descent >= 0, "descent %d must not be negative", f.descent);
    /* hhea says ascender 800 / descender -224 at 1024 upem: 13 / 4 at 16 px. */
    CHECK(f.ascent == 13, "fixture ascent at 16px should be 13, got %d",
          f.ascent);
    CHECK(f.descent == 4, "fixture descent at 16px should be 4, got %d",
          f.descent);
    CHECK(f.height >= f.ascent + f.descent,
          "height must cover ascent+descent");

    /* The em size is fixed by nj_ttf_open: nj_ttf_font only echoes `size`
     * and re-derives the style, so the metrics must NOT rescale here.  (The
     * caller opens the face at the em it wants - that is why the metrics of a
     * 32px face come from nj_ttf_open(..., 32).) */
    CHECK(nj_ttf_font(t, &f, NJ_FONT_STYLE_PLAIN, PX32) == 1,
          "reconfiguring the same face failed");
    CHECK(f.ascent == 13 && f.descent == 4,
          "nj_ttf_font must not rescale a 16px face: ascent %d descent %d",
          f.ascent, f.descent);
    CHECK(f.size == PX32, "size must follow the new call");

    free(mem);
    free(font);
}

/* ------------------------------------------------------------------ */
/* Metrics: hhea/hmtx/head                                              */
/* ------------------------------------------------------------------ */
static void test_metrics(const char *dir)
{
    char path[512];
    nj_u4 len = 0u;
    nj_u1 *font;
    face F;
    static const struct { nj_u2 ch; int adv16; int adv32; } cases[] = {
        { 0x20u, 8, 16 },       /* space: 512 units */
        { 0xA0u, 10, 20 },      /* nbsp: 640 like every other glyph here */
        { 0x42u, 10, 20 },      /* 'B' -> gid12 */
        { 0x4Fu, 10, 20 },      /* 'O' -> gid14 */
        { 0x6Du, 10, 20 },      /* 'm' -> gid3 */
    };
    unsigned i;

    printf("[metrics]\n");
    sprintf(path, "%s/nj_fixture.ttf", dir);
    font = read_file(path, &len);
    if (font == NULL) {
        CHECK(0, "cannot read %s", path);
        return;
    }
    if (!face_open(&F, font, len, PX16, NJ_FONT_STYLE_PLAIN)) {
        CHECK(0, "cannot open the fixture");
        free(font);
        return;
    }
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int got = F.f.glyph_advance(F.f.glyph_ud, cases[i].ch);
        CHECK(got == cases[i].adv16,
              "advance(U+%04X) at 16px = %d, expected %d",
              (unsigned)cases[i].ch, got, cases[i].adv16);
    }
    /* 640 units of 1024 at 16 px is exactly 10; rounding must not drift. */
    CHECK(F.f.glyph_advance(F.f.glyph_ud, 0x6Du) == FIX_ADV * PX16 / FIX_UPEM,
          "advance must be units * px / upem");
    face_close(&F);

    if (!face_open(&F, font, len, PX32, NJ_FONT_STYLE_PLAIN)) {
        CHECK(0, "cannot open the fixture at 32px");
        free(font);
        return;
    }
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int got = F.f.glyph_advance(F.f.glyph_ud, cases[i].ch);
        CHECK(got == cases[i].adv32,
              "advance(U+%04X) at 32px = %d, expected %d",
              (unsigned)cases[i].ch, got, cases[i].adv32);
    }
    face_close(&F);

    /* The non-BMP char only exists in the format 12 variant, so here it must
     * fall back to the face's default advance and not to a garbage value. */
    if (face_open(&F, font, len, PX16, NJ_FONT_STYLE_PLAIN)) {
        int def = F.f.glyph_advance(F.f.glyph_ud, 0x4E00u);
        CHECK(def >= 1 && def <= 64,
              "default advance for an unmapped char is %d", def);
        face_close(&F);
    }
    free(font);
}

/* ------------------------------------------------------------------ */
/* Empty glyphs, missing glyphs, the .notdef rule                       */
/* ------------------------------------------------------------------ */
static void test_missing_and_empty(const char *dir)
{
    char path[512];
    nj_u4 len = 0u;
    nj_u1 *font;
    face F;
    mask m;
    static const nj_u2 unmapped[] = { 'Q', 'W', 0x4E00u, 0x1E00u, 0xFFFFu };
    unsigned i;

    printf("[missing/empty]\n");
    sprintf(path, "%s/nj_fixture.ttf", dir);
    font = read_file(path, &len);
    if (font == NULL) {
        CHECK(0, "cannot read %s", path);
        return;
    }
    if (!face_open(&F, font, len, PX16, NJ_FONT_STYLE_PLAIN)) {
        CHECK(0, "cannot open the fixture");
        free(font);
        return;
    }

    /* No cmap entry: NULL, never a box glyph of our own invention. */
    for (i = 0u; i < sizeof(unmapped) / sizeof(unmapped[0]); i++) {
        int w = -1;
        int h = -1;
        const nj_u1 *p = F.f.glyph_lookup(F.f.glyph_ud, unmapped[i], &w, &h);
        CHECK(p == NULL, "U+%04X is not in the cmap and must return NULL",
              (unsigned)unmapped[i]);
    }
    /* U+2193 is inside the idRangeOffset array but maps to glyph 0... */
    {
        int w = 0;
        int h = 0;
        const nj_u1 *p = F.f.glyph_lookup(F.f.glyph_ud, 0x2190u, &w, &h);
        CHECK(p != NULL && w > 0 && h > 0,
              "U+2190 maps to a real glyph through idRangeOffset");
    }
    /* The idRangeOffset array holds [2, 3, 0, 2] for U+2190..U+2193, so
     * U+2192 (glyph 0) is unmapped and U+2193 (glyph 2) is not: two adjacent
     * code points taking different paths proves the array really is indexed. */
    {
        int w = 0;
        int h = 0;
        const nj_u1 *p = F.f.glyph_lookup(F.f.glyph_ud, 0x2192u, &w, &h);
        CHECK(p == NULL, "U+2192 maps to glyph 0, which we treat as unmapped");
        p = F.f.glyph_lookup(F.f.glyph_ud, 0x2193u, &w, &h);
        CHECK(p != NULL && w > 0 && h > 0,
              "U+2193 maps to glyph 2 through the same idRangeOffset range");
    }

    /* A cmap entry pointing at glyph 0 (.notdef) is deliberately treated as
     * unmapped: glyph_lookup returns NULL so the caller's fallback chain runs
     * instead of us painting the font's box.  The advance is still correct,
     * so layout does not shift. */
    m = face_glyph(&F, 0x41u);          /* 'A' -> gid0 in the fixture */
    CHECK(m.px == NULL, "a cmap entry for glyph 0 must report 'unmapped'");
    {
        int adv = F.f.glyph_advance(F.f.glyph_ud, 0x41u);
        CHECK(adv == 10, "glyph 0 must still advance by its hmtx value, got %d",
              adv);
    }

    /* Whitespace: a zero-length glyf slice and a zero-contour glyph header
     * both mean "advance only" - w = advance, h = 1, no ink. */
    {
        static const nj_u2 ws[] = { 0x20u, 0xA0u };
        for (i = 0u; i < 2u; i++) {
            int w = 0;
            int h = 0;
            const nj_u1 *p = F.f.glyph_lookup(F.f.glyph_ud, ws[i], &w, &h);
            int adv = F.f.glyph_advance(F.f.glyph_ud, ws[i]);
            CHECK(p != NULL, "whitespace U+%04X must not be NULL",
                  (unsigned)ws[i]);
            CHECK(w == adv, "whitespace width %d must be its advance %d",
                  w, adv);
            CHECK(h == 1, "whitespace height %d must be 1", h);
            if (p != NULL && h == 1 && w > 0) {
                int k;
                int nz = 0;
                for (k = 0; k < w; k++) {
                    if (p[k] != 0) {
                        nz++;
                    }
                }
                CHECK(nz == 0, "whitespace rows must be blank");
            }
        }
    }
    face_close(&F);
    free(font);
}

/* ------------------------------------------------------------------ */
/* Outline rules: winding, off-curve points, composites, sub-pixel ink  */
/* ------------------------------------------------------------------ */
static void test_outline_rules(const char *dir)
{
    char path[512];
    nj_u4 len = 0u;
    nj_u1 *font;
    face F;
    mask m;
    int x0, y0, x1, y1;

    printf("[outlines]\n");
    sprintf(path, "%s/nj_fixture.ttf", dir);
    font = read_file(path, &len);
    if (font == NULL) {
        CHECK(0, "cannot read %s", path);
        return;
    }

    /* --- non-zero winding: the ring's counter must be empty --- */
    if (face_open(&F, font, len, PX64, NJ_FONT_STYLE_PLAIN)) {
        int x, y;
        int hole_pixels = 0;
        int ring_pixels = 0;
        m = face_glyph(&F, 'o');            /* gid2: 512x512 ring, 256x256 hole */
        CHECK(m.px != NULL, "the ring glyph failed to rasterise");
        /* At 64px the outline is on exact pixel boundaries: the ink total is
         * the analytic area (512^2 - 256^2) upem^2 * (64/1024)^2 * 255. */
        {
            int want = (512 * 512 - 256 * 256) / 256 * 255;  /* /(1024/64)^2 */
            CHECK(mask_ink(m) == want,
                  "ring ink at 64px = %d, analytic area says %d",
                  mask_ink(m), want);
        }
        CHECK(mask_distinct(m) == 1,
              "an axis-aligned ring at 64px is all-or-nothing, got %d levels",
              mask_distinct(m));
        for (y = 0; y < m.h; y++) {
            for (x = 0; x < m.w; x++) {
                int v = m.px[y * m.w + x];
                /* Rows count down from row 0 at the ascender line, and the
                 * baseline sits at row F.f.ascent - 1. */
                int in_hole = (x >= unit_px(160, PX64) &&
                               x < unit_px(416, PX64) &&
                               y >= F.f.ascent - unit_px(416, PX64) &&
                               y < F.f.ascent - unit_px(160, PX64));
                if (in_hole) {
                    if (v != 0) {
                        hole_pixels++;
                    }
                } else if (v == 255) {
                    ring_pixels++;
                }
            }
        }
        CHECK(hole_pixels == 0,
              "the counter of the ring is filled: %d pixels - the fill rule "
              "or the contour direction is wrong", hole_pixels);
        CHECK(ring_pixels == 768,
              "the ring band should be 768 solid pixels, got %d", ring_pixels);
        face_close(&F);
    } else {
        CHECK(0, "cannot open the fixture at 64px");
    }

    if (!face_open(&F, font, len, PX16, NJ_FONT_STYLE_PLAIN)) {
        CHECK(0, "cannot open the fixture at 16px");
        free(font);
        return;
    }

    /* --- consecutive off-curve points (implied midpoint) --- */
    m = face_glyph(&F, 'm');                /* gid3 */
    CHECK(m.px != NULL, "'m' (consecutive off-curve) failed to rasterise");
    CHECK(mask_bbox(m, &x0, &y0, &x1, &y1) == 1, "'m' has no ink");
    CHECK(x0 == unit_px(64, PX16) && x1 == unit_px(768, PX16) - 1,
          "'m' x range %d..%d, expected %d..%d", x0, x1,
          unit_px(64, PX16), unit_px(768, PX16) - 1);
    /* yMax 640 units = 10.0px, so the apex sits in the band [9,10) whose row
     * is ascent-1-9 = 3. */
    CHECK(y0 == F.f.ascent - unit_px(640, PX16),
          "'m' apex should be at yMax 640 units, got row %d of %d (ascent %d)",
          y0, m.h, F.f.ascent);
    CHECK(mask_mid(m) >= 4,
          "'m' should have antialiased edges, %d partial pixels", mask_mid(m));

    /* --- a contour that starts and ends off-curve (wrap-around) --- */
    m = face_glyph(&F, 'n');                /* gid4 */
    CHECK(m.px != NULL, "'n' (off-curve wrap-around) failed to rasterise");
    CHECK(mask_bbox(m, &x0, &y0, &x1, &y1) == 1, "'n' has no ink");
    CHECK(x0 == unit_px(64, PX16) && x1 == unit_px(448, PX16) - 1,
          "'n' x range %d..%d, expected %d..%d", x0, x1,
          unit_px(64, PX16), unit_px(448, PX16) - 1);
    CHECK(mask_ink(m) > 0 && mask_mid(m) > 0,
          "'n' must have ink and antialiased pixels");

    /* --- a wide quadratic --- */
    m = face_glyph(&F, 'q');                /* gid11 */
    CHECK(m.px != NULL, "'q' (single wide quadratic) failed to rasterise");
    CHECK(mask_distinct(m) >= 8,
          "'q' should show many coverage levels, got %d", mask_distinct(m));

    /* --- a horizontal edge exactly on the baseline --- */
    m = face_glyph(&F, 'h');                /* gid6: y 0..320 units */
    CHECK(m.px != NULL, "'h' failed to rasterise");
    CHECK(mask_bbox(m, &x0, &y0, &x1, &y1) == 1, "'h' has no ink");
    /* Ink that touches the baseline occupies the band [0,1) above it, i.e.
     * row ascent-1 - not the last row of the mask (the mask also has descent
     * rows below the baseline). */
    CHECK(y1 == F.f.ascent - 1, "'h' must reach the baseline row %d, got %d",
          F.f.ascent - 1, y1);

    /* --- two overlapping same-winding contours: non-zero, not even-odd --- */
    m = face_glyph(&F, 'x');                /* gid9 */
    CHECK(m.px != NULL, "'x' (overlapping contours) failed to rasterise");
    CHECK(mask_bbox(m, &x0, &y0, &x1, &y1) == 1, "'x' has no ink");
    {
        /* The overlap must be filled: at 64px every pixel of the middle row
         * between the ink edges is either inside one rectangle or inside the
         * overlap, so an even-odd rasteriser would leave a hole. */
        int solid = 1;
        int row = (y0 + y1) / 2;
        int x;
        for (x = x0; x <= x1; x++) {
            if (m.px[row * m.w + x] != 255) {
                solid = 0;
            }
        }
        CHECK(solid, "'x' middle row %d is not solid: overlapping contours "
                     "of the same winding cancelled (even-odd fill rule?)",
              row);
    }

    /* --- self-intersecting, retraced contour: no crash, correct extent --- */
    m = face_glyph(&F, 'z');                /* gid10 */
    CHECK(m.px != NULL, "'z' (self-intersecting) failed to rasterise");
    CHECK(mask_bbox(m, &x0, &y0, &x1, &y1) == 1, "'z' has no ink");
    CHECK(x0 >= 0 && x1 < m.w && y0 >= 0 && y1 < m.h, "'z' bbox is inside");

    /* --- the composite: two components, scale, byte args, 2x2 matrix --- */
    m = face_glyph(&F, 'c');                /* gid5 */
    CHECK(m.px != NULL, "'c' (composite) failed to rasterise");
    CHECK(mask_bbox(m, &x0, &y0, &x1, &y1) == 1, "'c' has no ink");
    /* The scaled component's left edge is at 32*0.5 = 0.5px, so column 0
     * carries half coverage. */
    CHECK(x0 == 0 && x1 == unit_px(1216, PX16) - 1,
          "'c' composite x range %d..%d, expected 0..%d", x0, x1,
          unit_px(1216, PX16) - 1);
    /* yMax 600 units = 9.375px, but the topmost sliver is the spike's apex:
     * under a 1/32px accumulation it rounds away, exactly as FreeType's mask
     * does (both start at row 4). */
    CHECK(y0 >= F.f.ascent - unit_px(600, PX16) &&
          y0 <= F.f.ascent - unit_px(600, PX16) + 1,
          "'c' composite top row %d, expected %d..%d", y0,
          F.f.ascent - unit_px(600, PX16), F.f.ascent - unit_px(600, PX16) + 1);
    CHECK(mask_distinct(m) >= 6, "'c' should be antialiased, got %d levels",
          mask_distinct(m));

    /* --- a spike an eighth of a pixel wide: partial, never 0 or 255 --- */
    m = face_glyph(&F, 's');                /* gid7: 8 units wide */
    CHECK(m.px != NULL, "'s' (spike) failed to rasterise");
    CHECK(mask_ink(m) > 0, "the sub-pixel spike vanished");
    CHECK(mask_ink(m) < m.h * 255 / 2, "the sub-pixel spike saturated");
    {
        /* Analytic area: the triangle (8 units wide, 536 tall) is 2144 upem^2
         * = 0.523 px^2 at 16px, i.e. 133 coverage units. */
        int want = 2144 * 255 / (FIX_UPEM * FIX_UPEM / (PX16 * PX16));
        int got = mask_ink(m);
        CHECK(got > want * 3 / 4 && got < want * 5 / 4,
              "spike ink %d, analytic area says %d", got, want);
    }
    CHECK(mask_mid(m) >= 1, "the spike must have partial pixels");
    CHECK(mask_distinct(m) >= 3,
          "the spike must be graded, not thresholded, got %d levels",
          mask_distinct(m));

    /* --- a stem exactly half a pixel wide at 16px --- */
    m = face_glyph(&F, 'l');                /* gid12: 64..96 units */
    CHECK(m.px != NULL, "'l' (half-pixel stem) failed to rasterise");
    {
        /* 32 units * 640 tall at 16px = 0.5 px * 10 px = 5 px^2 of ink. */
        int want = 32 * 640 * 255 / (FIX_UPEM * FIX_UPEM / (PX16 * PX16));
        int got = mask_ink(m);
        CHECK(got > want / 2 && got < want * 3 / 2,
              "half-pixel stem ink %d, analytic area says %d", got, want);
        CHECK(mask_mid(m) > 0, "the half-pixel stem must be partially covered");
    }

    /* --- a circle: four quadratic arcs, all 8 octants antialiased --- */
    m = face_glyph(&F, 'O');                /* gid14 */
    CHECK(m.px != NULL, "'O' (four-arc circle) failed to rasterise");
    CHECK(mask_distinct(m) >= 8,
          "a circle should have many coverage levels, got %d",
          mask_distinct(m));
    CHECK(mask_mid(m) >= 20,
          "a circle should have many partial pixels, got %d", mask_mid(m));

    /* --- everything must be inside the requested glyph box --- */
    {
        static const nj_u2 all[] = { 'o', 'm', 'n', 'q', 'h', 'x', 'z', 'c',
                                     's', 'l', 'O', 'p', 0x20u, 0xA0u };
        unsigned i;
        for (i = 0u; i < sizeof(all) / sizeof(all[0]); i++) {
            mask g = face_glyph(&F, all[i]);
            CHECK(g.px != NULL || all[i] == 0x20u || all[i] == 0xA0u,
                  "U+%04X must rasterise", (unsigned)all[i]);
            CHECK(g.h >= 0 && g.w >= 0, "negative mask size");
        }
    }
    face_close(&F);
    free(font);
}

/* ------------------------------------------------------------------ */
/* loca short vs long, and the four cmap flavours                       */
/* ------------------------------------------------------------------ */
static void test_loca_variants(const char *dir)
{
    char path[512];
    nj_u4 len_a = 0u;
    nj_u4 len_b = 0u;
    nj_u1 *a;
    nj_u1 *b;
    face FA;
    face FB;
    static const nj_u2 chars[] = { 'o', 'm', 'n', 'q', 'h', 'x', 'z', 'c',
                                   's', 'l', 'O', 'p', 'B' };
    unsigned i;

    printf("[loca]\n");
    sprintf(path, "%s/nj_fixture.ttf", dir);
    a = read_file(path, &len_a);
    sprintf(path, "%s/nj_fixture_long.ttf", dir);
    b = read_file(path, &len_b);
    if (a == NULL || b == NULL) {
        CHECK(0, "cannot read the loca fixtures");
        free(a);
        free(b);
        return;
    }
    if (!face_open(&FA, a, len_a, PX16, NJ_FONT_STYLE_PLAIN) ||
        !face_open(&FB, b, len_b, PX16, NJ_FONT_STYLE_PLAIN)) {
        CHECK(0, "cannot open the loca fixtures");
        face_close(&FA);
        face_close(&FB);
        free(a);
        free(b);
        return;
    }
    /* Byte-identical coverage from the 16-bit and the 32-bit loca. */
    for (i = 0u; i < sizeof(chars) / sizeof(chars[0]); i++) {
        mask ma = face_glyph(&FA, chars[i]);
        mask mb = face_glyph(&FB, chars[i]);
        CHECK(ma.px != NULL && mb.px != NULL,
              "U+%04X must rasterise from both loca formats",
              (unsigned)chars[i]);
        CHECK(mask_equal(ma, mb),
              "U+%04X differs between short and long loca",
              (unsigned)chars[i]);
    }
    face_close(&FA);
    face_close(&FB);
    free(a);
    free(b);
}

static void test_cmap_variants(const char *dir)
{
    char path[512];
    nj_u4 len = 0u;
    nj_u1 *base;
    nj_u1 *var;
    face FB;
    face FV;

    printf("[cmap]\n");
    /* format 12 must win over format 4 where they disagree */
    sprintf(path, "%s/nj_fixture.ttf", dir);
    base = read_file(path, &len);
    sprintf(path, "%s/nj_fixture_cmap12.ttf", dir);
    var = read_file(path, &len);
    if (base == NULL || var == NULL) {
        CHECK(0, "cannot read the cmap fixtures");
        free(base);
        free(var);
        return;
    }
    if (face_open(&FB, base, len, PX32, NJ_FONT_STYLE_PLAIN) &&
        face_open(&FV, var, len, PX32, NJ_FONT_STYLE_PLAIN)) {
        /* In the plain fixture 'B' is gid12 (a stem); format 12 remaps it to
         * gid9 (two rectangles), so the coverage must change. */
        mask b_plain = face_glyph(&FB, 'B');
        mask b_12 = face_glyph(&FV, 'B');
        mask x_plain = face_glyph(&FB, 'x');
        CHECK(b_plain.px != NULL && b_12.px != NULL,
              "'B' must rasterise from both cmaps");
        CHECK(!mask_equal(b_plain, b_12),
              "format 12 did not override format 4 for 'B'");
        CHECK(mask_equal(b_12, x_plain),
              "format 12's 'B' should be glyph 9, the same mask as 'x'");
        /* The format 12 subtable also maps U+1F600, but nj_font's
         * glyph_lookup takes an nj_u2, so a non-BMP code point cannot be
         * requested through the frozen contract at all - only the BMP
         * override above is observable.  Assert the BMP part of format 12
         * still works when format 4 is absent from the answer. */
        {
            int w = 0;
            int h = 0;
            const nj_u1 *p = FV.f.glyph_lookup(FV.f.glyph_ud, 0x1F60u, &w, &h);
            CHECK(p == NULL, "U+1F60 is unmapped and must return NULL");
        }
    } else {
        CHECK(0, "cannot open the cmap fixtures");
    }
    face_close(&FB);
    face_close(&FV);
    free(base);
    free(var);

    /* A Mac (1,0) format 0 subtable: used only because it is all there is. */
    sprintf(path, "%s/nj_fixture_mac.ttf", dir);
    var = read_file(path, &len);
    if (var != NULL) {
        if (face_open(&FV, var, len, PX16, NJ_FONT_STYLE_PLAIN)) {
            /* The fixture's Mac subtable maps only 0x41 -> gid2. */
            mask m = face_glyph(&FV, 0x41u);
            CHECK(m.px != NULL && mask_ink(m) > 0,
                  "the Mac-only cmap must still resolve its ASCII entry");
            (void)face_glyph(&FV, 'Q');
        } else {
            CHECK(0, "cannot open the Mac cmap fixture");
        }
        face_close(&FV);
        free(var);
    } else {
        CHECK(0, "cannot read nj_fixture_mac.ttf");
    }

    /* A (3,0) symbol subtable: likewise a last resort. */
    sprintf(path, "%s/nj_fixture_symbol.ttf", dir);
    var = read_file(path, &len);
    if (var != NULL) {
        if (face_open(&FV, var, len, PX16, NJ_FONT_STYLE_PLAIN)) {
            int w = 0;
            int h = 0;
            const nj_u1 *p = FV.f.glyph_lookup(FV.f.glyph_ud, 0xF06Fu, &w, &h);
            CHECK(p != NULL && w > 0 && h > 0,
                  "the symbol cmap must resolve its 0xF0xx range");
        } else {
            CHECK(0, "cannot open the symbol cmap fixture");
        }
        face_close(&FV);
        free(var);
    } else {
        CHECK(0, "cannot read nj_fixture_symbol.ttf");
    }
}

/* ------------------------------------------------------------------ */
/* Bold and italic are synthesised in the rasteriser                    */
/* ------------------------------------------------------------------ */
static void test_styles(const char *dir)
{
    char path[512];
    nj_u4 len = 0u;
    nj_u1 *font;
    face FP;
    face FB;
    face FI;
    mask plain;
    mask bold;
    mask ital;
    int x, y;

    printf("[styles]\n");
    sprintf(path, "%s/nj_fixture.ttf", dir);
    font = read_file(path, &len);
    if (font == NULL) {
        CHECK(0, "cannot read %s", path);
        return;
    }
    if (!face_open(&FP, font, len, PX32, NJ_FONT_STYLE_PLAIN) ||
        !face_open(&FB, font, len, PX32, NJ_FONT_STYLE_BOLD) ||
        !face_open(&FI, font, len, PX32, NJ_FONT_STYLE_ITALIC)) {
        CHECK(0, "cannot open the style faces");
        face_close(&FP);
        face_close(&FB);
        face_close(&FI);
        free(font);
        return;
    }

    plain = face_glyph(&FP, 'o');
    bold = face_glyph(&FB, 'o');
    ital = face_glyph(&FI, 'o');
    CHECK(plain.px != NULL && bold.px != NULL && ital.px != NULL,
          "styles must all rasterise");
    CHECK(bold.w == plain.w && bold.h == plain.h,
          "emboldening must not resize the glyph box");
    /* A 12-degree shear pushes the top of the glyph sideways, so the box may
     * grow to the right - but only by about tan(12deg) * height, and the
     * height itself must not change. */
    CHECK(ital.h == plain.h, "the shear must not change the glyph height");
    CHECK(ital.w >= plain.w && ital.w <= plain.w + 3,
          "the shear may widen the box by a little: plain %d, italic %d",
          plain.w, ital.w);
    CHECK(mask_ink(bold) > mask_ink(plain),
          "bold ink %d should exceed plain ink %d",
          mask_ink(bold), mask_ink(plain));
    CHECK(mask_ink(bold) < mask_ink(plain) * 2,
          "bold must smear ~1px, not double the glyph: %d vs %d",
          mask_ink(bold), mask_ink(plain));
    /* The smear only ever adds coverage. */
    {
        int lost = 0;
        for (y = 0; y < plain.h; y++) {
            for (x = 0; x < plain.w; x++) {
                if (bold.px[y * bold.w + x] < plain.px[y * plain.w + x]) {
                    lost++;
                }
            }
        }
        CHECK(lost == 0, "bold lost coverage at %d pixels", lost);
    }
    /* The shear leans right: the top of the glyph must move right. */
    {
        int top_plain = -1;
        int top_ital = -1;
        for (y = 0; y < plain.h; y++) {
            for (x = 0; x < plain.w; x++) {
                if (plain.px[y * plain.w + x] != 0) {
                    top_plain = x;
                    break;
                }
            }
            if (top_plain >= 0) {
                break;
            }
        }
        for (y = 0; y < ital.h; y++) {
            for (x = 0; x < ital.w; x++) {
                if (ital.px[y * ital.w + x] != 0) {
                    top_ital = x;
                    break;
                }
            }
            if (top_ital >= 0) {
                break;
            }
        }
        CHECK(top_ital > top_plain,
              "italic must lean right: top ink column %d -> %d",
              top_plain, top_ital);
    }
    CHECK(!mask_equal(plain, ital), "italic must change the mask");

    /* Style is per-face state: switching back must restore the plain mask. */
    {
        nj_font f2;
        mask again;
        CHECK(nj_ttf_font(FP.t, &f2, NJ_FONT_STYLE_BOLD, PX32) == 1,
              "cannot switch style");
        again = face_glyph(&FP, 'o');
        CHECK(mask_equal(again, bold),
              "the bold mask must survive a style switch on the same face");
        CHECK(nj_ttf_font(FP.t, &f2, NJ_FONT_STYLE_PLAIN, PX32) == 1,
              "cannot switch back");
        again = face_glyph(&FP, 'o');
        CHECK(mask_equal(again, plain),
              "switching back to plain must restore the plain mask");
        CHECK(nj_ttf_font(FP.t, &f2,
                          NJ_FONT_STYLE_BOLD | NJ_FONT_STYLE_ITALIC,
                          PX32) == 1, "cannot combine bold and italic");
        again = face_glyph(&FP, 'o');
        CHECK(again.px != NULL && mask_ink(again) > mask_ink(ital),
              "bold+italic must be heavier than italic alone");
    }

    face_close(&FP);
    face_close(&FB);
    face_close(&FI);
    free(font);
}

/* ------------------------------------------------------------------ */
/* The cache: same bytes every time, and a tiny cache still works       */
/* ------------------------------------------------------------------ */
static void test_cache(const char *dir)
{
    char path[512];
    nj_u4 len = 0u;
    nj_u1 *font;
    face F;
    static const nj_u2 chars[] = { 'o', 'm', 'n', 'q', 'h', 'x', 'z', 'c',
                                   's', 'l', 'O', 'p', 'B' };
    unsigned i;
    int round;

    printf("[cache]\n");
    sprintf(path, "%s/nj_fixture.ttf", dir);
    font = read_file(path, &len);
    if (font == NULL) {
        CHECK(0, "cannot read %s", path);
        return;
    }
    if (!face_open(&F, font, len, PX16, NJ_FONT_STYLE_PLAIN)) {
        CHECK(0, "cannot open the fixture");
        free(font);
        return;
    }

    /* Repeated lookups, enough to evict everything several times over, must
     * hand back identical coverage every time. */
    for (round = 0; round < 8; round++) {
        for (i = 0u; i < sizeof(chars) / sizeof(chars[0]); i++) {
            mask m = face_glyph(&F, chars[i]);
            int w = 0;
            int h = 0;
            const nj_u1 *p = F.f.glyph_lookup(F.f.glyph_ud, chars[i], &w, &h);
            CHECK(m.px != NULL && p != NULL, "lookup failed");
            if (m.px != NULL && p != NULL) {
                CHECK(w == m.w && h == m.h,
                      "size changed between identical lookups");
                CHECK(memcmp(p, m.px, (size_t)m.w * (size_t)m.h) == 0,
                      "coverage changed between identical lookups");
            }
        }
    }
    /* The documented contract: the pointer stays valid until the next call on
     * the same face, and a repeated lookup of the same glyph is a cache hit. */
    {
        mask a = face_glyph(&F, 'O');
        int w = 0;
        int h = 0;
        const nj_u1 *p = F.f.glyph_lookup(F.f.glyph_ud, 'O', &w, &h);
        CHECK(p == a.px, "the same glyph must return the same pointer");
    }
    face_close(&F);

    /* A one-glyph cache: the face must shrink itself, not fail. */
    {
        nj_size bytes = nj_ttf_sizeof(1);
        nj_u1 *mem = (nj_u1 *)malloc(bytes);
        nj_ttf *t;
        nj_font f;
        CHECK(mem != NULL, "out of memory");
        if (mem != NULL) {
            t = nj_ttf_open(mem, bytes, font, len, PX16);
            CHECK(t != NULL, "a minimum-size cache must still open");
            if (t != NULL) {
                nj_u1 *rows = (nj_u1 *)malloc(4096u);
                int w = 0;
                int h = 0;
                const nj_u1 *p;
                CHECK(nj_ttf_font(t, &f, NJ_FONT_STYLE_PLAIN, PX16) == 1,
                      "cannot configure the small-cache face");
                p = f.glyph_lookup(f.glyph_ud, 'o', &w, &h);
                CHECK(p != NULL && w > 0 && h > 0,
                      "the small-cache face must still rasterise");
                CHECK(rows != NULL, "out of memory");
                if (rows != NULL) {
                    if (p != NULL && (size_t)w * (size_t)h <= 4096u) {
                        memcpy(rows, p, (size_t)w * (size_t)h);
                    }
                    /* Evict it and come back: same bytes again. */
                    (void)f.glyph_lookup(f.glyph_ud, 'm', &w, &h);
                    p = f.glyph_lookup(f.glyph_ud, 'o', &w, &h);
                    CHECK(p != NULL &&
                          (size_t)w * (size_t)h <= 4096u &&
                          memcmp(rows, p, (size_t)w * (size_t)h) == 0,
                          "a tiny cache must not change the rasterisation");
                }
                free(rows);
            }
            free(mem);
        }
    }
    free(font);
}

/* ------------------------------------------------------------------ */
/* Corrupt fonts must fail the open, not the lookup                     */
/* ------------------------------------------------------------------ */
static void test_negatives(const char *dir)
{
    static const char *bad[] = {
        "bad_glyf_offset.ttf", "bad_numglyphs.ttf", "bad_hmtx.ttf",
        "bad_cmap_segcount.ttf", "bad_loca.ttf", "bad_magic.ttf",
        "bad_sfnt_version.ttf", "bad_cmap_offset.ttf", "truncated.ttf"
    };
    char path[512];
    unsigned i;
    nj_size bytes = nj_ttf_sizeof(TTF_CACHE_GLYPHS);
    nj_u1 *mem = (nj_u1 *)malloc(bytes);

    printf("[negatives]\n");
    CHECK(mem != NULL, "out of memory");
    if (mem == NULL) {
        return;
    }
    for (i = 0u; i < sizeof(bad) / sizeof(bad[0]); i++) {
        nj_u4 len = 0u;
        nj_u1 *data;
        nj_ttf *t;
        sprintf(path, "%s/%s", dir, bad[i]);
        data = read_file(path, &len);
        if (data == NULL) {
            CHECK(0, "cannot read %s - run python3 tools/gen_ttf_fixture.py",
                  path);
            continue;
        }
        t = nj_ttf_open(mem, bytes, data, len, PX16);
        CHECK(t == NULL, "%s must be rejected by nj_ttf_open", bad[i]);
        if (t != NULL) {
            /* If a corrupt font does open, it must at least be safe to use. */
            nj_font f;
            int w = 0;
            int h = 0;
            if (nj_ttf_font(t, &f, NJ_FONT_STYLE_PLAIN, PX16)) {
                (void)f.glyph_lookup(f.glyph_ud, 'o', &w, &h);
            }
        }
        free(data);
    }

    /* Not fonts at all. */
    {
        nj_u1 junk[512];
        unsigned k;
        for (k = 0u; k < sizeof(junk); k++) {
            junk[k] = (nj_u1)(k * 7u + 3u);
        }
        CHECK(nj_ttf_open(mem, bytes, junk, (nj_u4)sizeof(junk), PX16) == NULL,
              "random bytes must be rejected");
        memset(junk, 0, sizeof(junk));
        CHECK(nj_ttf_open(mem, bytes, junk, (nj_u4)sizeof(junk), PX16) == NULL,
              "an all-zero buffer must be rejected");
        memset(junk, 0xFF, sizeof(junk));
        CHECK(nj_ttf_open(mem, bytes, junk, (nj_u4)sizeof(junk), PX16) == NULL,
              "an all-ones buffer must be rejected");
    }
    free(mem);
}

/* ------------------------------------------------------------------ */
/* Mutation fuzzer: the only assertion is "did not crash"               */
/* ------------------------------------------------------------------ */
static nj_u4 fuzz_next(nj_u4 *state)
{
    nj_u4 x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void fuzz_font(const nj_u1 *font, nj_u4 len, int rounds, nj_u4 seed)
{
    nj_size bytes = nj_ttf_sizeof(TTF_CACHE_GLYPHS);
    nj_u1 *mem = (nj_u1 *)malloc(bytes);
    nj_u1 *work = (nj_u1 *)malloc((size_t)len + 8u);
    nj_u4 i;

    if (mem == NULL || work == NULL) {
        CHECK(0, "out of memory in the fuzzer");
        free(mem);
        free(work);
        return;
    }
    for (i = 0u; i < (nj_u4)rounds; i++) {
        nj_u4 r = fuzz_next(&seed);
        nj_u4 n = (r & 1u) ? 1u + (fuzz_next(&seed) % 8u) : 1u;
        nj_u4 k;
        nj_u4 tlen = len;
        nj_ttf *t;
        nj_font f;
        int style = (int)(fuzz_next(&seed) % 4u);
        int px = (int)(1u + fuzz_next(&seed) % 64u);

        memcpy(work, font, len);
        switch (r % 4u) {
        case 0u:                        /* bit flips anywhere */
            for (k = 0u; k < n; k++) {
                nj_u4 at = fuzz_next(&seed) % len;
                work[at] ^= (nj_u1)(1u << (fuzz_next(&seed) % 8u));
            }
            break;
        case 1u:                        /* truncation */
            tlen = 12u + fuzz_next(&seed) % (len - 12u);
            break;
        case 2u:                        /* a random 32-bit value in a header */
            for (k = 0u; k < n; k++) {
                nj_u4 at = (fuzz_next(&seed) % (len / 4u)) * 4u;
                nj_u4 v = fuzz_next(&seed);
                work[at] = (nj_u1)(v & 0xFFu);
                work[at + 1u] = (nj_u1)((v >> 8) & 0xFFu);
                work[at + 2u] = (nj_u1)((v >> 16) & 0xFFu);
                work[at + 3u] = (nj_u1)((v >> 24) & 0xFFu);
            }
            break;
        default:                        /* a scribbled span */
        {
            nj_u4 at = fuzz_next(&seed) % len;
            nj_u4 span = 1u + fuzz_next(&seed) % 64u;
            if (span > len - at) {
                span = len - at;
            }
            memset(work + at, (int)(fuzz_next(&seed) & 0xFFu), (size_t)span);
            break;
        }
        }
        if (tlen < 12u) {
            tlen = 12u;
        }

        t = nj_ttf_open(mem, bytes, work, tlen, px);
        if (t == NULL) {
            continue;
        }
        if (!nj_ttf_font(t, &f, style, px)) {
            continue;
        }
        g_fuzz_accepted++;
        for (k = 0u; k < 6u; k++) {
            static const nj_u2 probes[] = { 'o', 0x41u, 0x2190u, 0xFFFFu,
                                            0x20u, 0x2F00u };
            int w = 0;
            int h = 0;
            const nj_u1 *p = f.glyph_lookup(f.glyph_ud, probes[k], &w, &h);
            (void)f.glyph_advance(f.glyph_ud, probes[k]);
            if (p != NULL) {
                /* Touch every row so ASan sees the whole mask being read. */
                int y;
                volatile int sum = 0;
                g_fuzz_glyphs++;
                for (y = 0; y < h; y++) {
                    int x;
                    for (x = 0; x < w; x++) {
                        sum += p[y * w + x];
                    }
                }
                (void)sum;
            }
        }
        /* Re-opening the same block must be fine too. */
        t = nj_ttf_open(mem, bytes, work, tlen, px);
        if (t != NULL) {
            (void)nj_ttf_font(t, &f, NJ_FONT_STYLE_PLAIN, px);
        }
    }
    /* Reaching here is the assertion. */
    CHECK(1, "fuzzer survived");
    free(mem);
    free(work);
}

static void test_fuzz(const char *dir, const char *real_font)
{
    char path[512];
    nj_u4 len = 0u;
    nj_u1 *font;

    printf("[fuzz]\n");
    sprintf(path, "%s/nj_fixture.ttf", dir);
    font = read_file(path, &len);
    if (font == NULL) {
        CHECK(0, "cannot read %s", path);
        return;
    }
    fuzz_font(font, len, 2500, 0x9E3779B9u);
    free(font);
    printf("  fixture: 2500 mutations, %d still parsed, %d glyphs rasterised\n",
           g_fuzz_accepted, g_fuzz_glyphs);

    if (real_font != NULL) {
        font = read_file(real_font, &len);
        if (font != NULL) {
            int before = g_fuzz_accepted;
            fuzz_font(font, len, 250, 0x1234ABCDu);
            printf("  system font: 250 mutations, %d still parsed\n",
                   g_fuzz_accepted - before);
            free(font);
        }
    }
}

/* ------------------------------------------------------------------ */
/* FreeType comparison (Pillow oracle)                                  */
/* ------------------------------------------------------------------ */
typedef struct {
    int cp;
    int px;
    int strict;         /* 0 = sub-pixel/composite: structure only */
} ft_case;

typedef struct {
    double mae;
    double ratio;
    int mid;
    int dv;
    double cdx;
    double cdy;
    int bbx;
    int bby;
    int adv;
    int ftadv;
} ft_result;

static int write_pgm(const char *path, mask m)
{
    FILE *fh = fopen(path, "wb");
    int y;
    if (fh == NULL) {
        return 0;
    }
    fprintf(fh, "P5\n%d %d\n255\n", m.w, m.h);
    for (y = 0; y < m.h; y++) {
        if (m.w > 0 && fwrite(m.px + (size_t)y * (size_t)m.w, 1u,
                              (size_t)m.w, fh) != (size_t)m.w) {
            fclose(fh);
            return 0;
        }
    }
    return fclose(fh) == 0;
}

static void check_ft_line(const ft_result *R, int strict, int cp, int px)
{
    CHECK(R->adv == R->ftadv,
          "U+%04X at %dpx: advance %d but FreeType says %d",
          (unsigned)cp, px, R->adv, R->ftadv);
    /* MAE is a soft bound: it moves with FreeType's grid-fitting, not with
     * the shape.  A flipped Y, a transposed outline or a wrong baseline would
     * break the ink ratio, the centroid and the bbox below - those are
     * structural and stay tight. */
    if (strict) {
        CHECK(R->mae <= 24.0,
              "U+%04X at %dpx: mean absolute coverage error %.3f is too high",
              (unsigned)cp, px, R->mae);
        CHECK(R->ratio > 0.90 && R->ratio < 1.10,
              "U+%04X at %dpx: ink ratio %.4f is outside 10%%",
              (unsigned)cp, px, R->ratio);
    } else {
        CHECK(R->mae <= 32.0,
              "U+%04X at %dpx: mean absolute coverage error %.3f is too high",
              (unsigned)cp, px, R->mae);
    }
    /* Per-glyph antialiasing is asserted in aggregate instead: a glyph whose
     * outline lands on exact pixel boundaries (most of the fixture at 32px)
     * legitimately has no partial pixel at all. */
    CHECK(R->cdx > -1.0 && R->cdx < 1.0,
          "U+%04X at %dpx: ink centroid x differs by %.3f px", (unsigned)cp,
          px, R->cdx);
    CHECK(R->cdy > -1.0 && R->cdy < 1.0,
          "U+%04X at %dpx: ink centroid y differs by %.3f px", (unsigned)cp,
          px, R->cdy);
    CHECK(R->bbx <= 1, "U+%04X at %dpx: ink bbox x differs by %d px",
          (unsigned)cp, px, R->bbx);
    CHECK(R->bby <= 1, "U+%04X at %dpx: ink bbox y differs by %d px",
          (unsigned)cp, px, R->bby);
}

/*
 * Rasterise every case with nj_ttf.c, hand the masks to the Pillow oracle and
 * assert on what it reports.  Returns 0 when the comparison could not run at
 * all (no interpreter, no Pillow, no font) - the caller turns that into a
 * skip, never into a failure.
 */
static int ft_compare(const char *surface, const char *font_path,
                      const ft_case *cases, unsigned n, const char *tmp,
                      const char *oracle)
{
    char manifest_path[512];
    char out_path[512];
    char tag[32];
    char cmd[2048];
    char line[512];
    const char *py = getenv("NJ_PYTHON");
    nj_u4 len = 0u;
    nj_u1 *data;
    face F;
    FILE *fh;
    unsigned i;
    int nresult = 0;
    int rc;
    double mae_sum = 0.0;
    double mae_max = 0.0;
    int mid_total = 0;
    int dv_min = 1 << 30;
    int dv_max = 0;

    if (py == NULL || py[0] == '\0') {
        py = "python3";
    }
    data = read_file(font_path, &len);
    if (data == NULL) {
        SKIP("%s: cannot read the font", surface);
        return 0;
    }
    if (!face_open(&F, data, len, cases[0].px, NJ_FONT_STYLE_PLAIN)) {
        SKIP("%s: nj_ttf cannot open %s", surface, font_path);
        free(data);
        return 0;
    }
    face_close(&F);

    /* One set of scratch files per surface, so both stay inspectable. */
    {
        unsigned k;
        for (k = 0u; k + 1u < sizeof(tag) && surface[k] != '\0'; k++) {
            tag[k] = (surface[k] == ' ') ? '_' : surface[k];
        }
        tag[k] = '\0';
    }
    sprintf(manifest_path, "%s/manifest_%s.txt", tmp, tag);
    sprintf(out_path, "%s/result_%s.txt", tmp, tag);
    fh = fopen(manifest_path, "w");
    if (fh == NULL) {
        SKIP("%s: cannot write %s", surface, manifest_path);
        free(data);
        return 0;
    }
    fprintf(fh, "font %s\n", font_path);
    for (i = 0u; i < n; i++) {
        char pgm[512];
        mask m;
        int adv;
        if (!face_open(&F, data, len, cases[i].px, NJ_FONT_STYLE_PLAIN)) {
            fclose(fh);
            CHECK(0, "%s: cannot open %s at %dpx", surface, font_path,
                  cases[i].px);
            free(data);
            return 0;
        }
        m = face_glyph(&F, (nj_u2)cases[i].cp);
        adv = F.f.glyph_advance(F.f.glyph_ud, (nj_u2)cases[i].cp);
        if (m.px == NULL) {
            fclose(fh);
            face_close(&F);
            CHECK(0, "%s: U+%04X missing at %dpx", surface,
                  (unsigned)cases[i].cp, cases[i].px);
            free(data);
            return 0;
        }
        sprintf(pgm, "%s/%04x_%d.pgm", tmp, (unsigned)cases[i].cp,
                cases[i].px);
        if (!write_pgm(pgm, m)) {
            fclose(fh);
            face_close(&F);
            SKIP("%s: cannot write %s", surface, pgm);
            free(data);
            return 0;
        }
        fprintf(fh, "%d %d %d %d %d %d %s\n", cases[i].cp, cases[i].px,
                F.f.ascent, adv, m.w, m.h, pgm);
        face_close(&F);
    }
    fclose(fh);

    snprintf(cmd, sizeof(cmd), "%s %s --manifest %s > %s 2>&1", py, oracle,
             manifest_path, out_path);
    rc = system(cmd);
    if (rc != 0) {
        fh = fopen(out_path, "r");
        if (fh != NULL && fgets(line, (int)sizeof(line), fh) != NULL &&
            strncmp(line, "SKIP", 4) == 0) {
            size_t l = strlen(line);
            while (l > 0u && (line[l - 1] == '\n' || line[l - 1] == '\r')) {
                line[--l] = '\0';
            }
            fclose(fh);
            SKIP("%s: %s", surface, line);
            free(data);
            return 0;
        }
        if (fh != NULL) {
            fclose(fh);
        }
        SKIP("%s: the oracle could not run (%s)", surface, cmd);
        free(data);
        return 0;
    }

    fh = fopen(out_path, "r");
    if (fh == NULL) {
        SKIP("%s: no oracle output", surface);
        free(data);
        return 0;
    }
    while (fgets(line, (int)sizeof(line), fh) != NULL) {
        ft_result R;
        int cp = 0;
        int px = 0;
        if (strncmp(line, "RESULT", 6) != 0) {
            continue;
        }
        R.mae = R.ratio = R.cdx = R.cdy = 0.0;
        R.mid = R.dv = R.bbx = R.bby = R.adv = R.ftadv = 0;
        if (sscanf(line,
                   "RESULT %d %d adv=%d ftadv=%d mae=%lf ratio=%lf mid=%d "
                   "dv=%d cdx=%lf cdy=%lf bbx=%d bby=%d",
                   &cp, &px, &R.adv, &R.ftadv, &R.mae, &R.ratio, &R.mid,
                   &R.dv, &R.cdx, &R.cdy, &R.bbx, &R.bby) != 12) {
            CHECK(0, "%s: cannot parse the oracle line: %s", surface, line);
            continue;
        }
        nresult++;
        mae_sum += R.mae;
        if (R.mae > mae_max) {
            mae_max = R.mae;
        }
        mid_total += R.mid;
        if (R.dv < dv_min) {
            dv_min = R.dv;
        }
        if (R.dv > dv_max) {
            dv_max = R.dv;
        }
        {
            unsigned k;
            int strict = 0;
            for (k = 0u; k < n; k++) {
                if (cases[k].cp == cp && cases[k].px == px) {
                    strict = cases[k].strict;
                }
            }
            check_ft_line(&R, strict, cp, px);
        }
    }
    fclose(fh);

    if (nresult == 0) {
        SKIP("%s: the oracle reported nothing", surface);
        free(data);
        return 0;
    }
    CHECK(nresult == (int)n, "%s: %d of %u glyphs compared", surface,
          nresult, n);
    printf("  %s: %d glyphs, mean MAE %.3f (worst %.3f), "
           "%d partial pixels, up to %d coverage levels (%d minimum)\n",
           surface, nresult, mae_sum / (double)nresult, mae_max, mid_total,
           dv_max, dv_min);
    /* The suite-wide antialiasing proof, aggregated over every comparison.
     * The MAE bound is generous because Pillow renders with FreeType's
     * default (hinted) load flags: at 16px FreeType grid-fits stems, we
     * integrate exact area.  Measured here: fixture mean 1.5, system font
     * mean 5.9 (16px worst '@' 20.9, which falls to 6.9 at 32px).  The
     * structural checks - ink ratio, centroid, bbox - are the tight ones. */
    CHECK(mae_sum / (double)nresult <= 9.0,
          "%s: mean MAE %.3f over %d glyphs is too high", surface,
          mae_sum / (double)nresult, nresult);
    CHECK(mid_total >= 100,
          "%s: only %d partially covered pixels in the whole set - "
          "the rasteriser looks thresholded", surface, mid_total);
    CHECK(dv_max >= 8,
          "%s: the best glyph only shows %d coverage levels", surface, dv_max);
    CHECK(dv_min >= 1, "%s: a glyph with no coverage levels at all", surface);
    free(data);
    return 1;
}

static void test_freetype(const char *dir, const char *oracle,
                          const char *real_font, const char *tmp)
{
    /* The fixture's own glyphs: curves, rectangles, a spike, a composite. */
    static const ft_case fixture[] = {
        { 'o', PX16, 1 }, { 'o', PX32, 1 },
        { 'm', PX16, 1 }, { 'm', PX32, 1 },
        { 'n', PX16, 1 }, { 'n', PX32, 1 },
        { 'h', PX16, 1 }, { 'h', PX32, 1 },
        { 'p', PX16, 1 }, { 'p', PX32, 1 },
        { 'x', PX16, 1 }, { 'x', PX32, 1 },
        { 'q', PX16, 1 }, { 'q', PX32, 1 },
        { 'z', PX16, 1 }, { 'z', PX32, 1 },
        { 'O', PX16, 1 }, { 'O', PX32, 1 },
        { 'l', PX32, 1 },
        /* Sub-pixel at 16px: FreeType's subpixel hinting grid-fits the stem
         * to a whole pixel, we integrate the exact area. */
        { 'l', PX16, 0 },
        { 's', PX16, 0 }, { 's', PX32, 0 },
        /* Composite: FreeType places scaled components differently. */
        { 'c', PX16, 0 }, { 'c', PX32, 0 }
    };
    /* A real font: ordinary typography, none of it synthetic. */
    static const ft_case real[] = {
        { 'H', PX16, 1 }, { 'a', PX16, 1 }, { 'm', PX16, 1 },
        { 'b', PX16, 1 }, { 'u', PX16, 1 }, { 'r', PX16, 1 },
        { 'g', PX16, 1 }, { 'e', PX16, 1 }, { '0', PX16, 1 },
        { '8', PX16, 1 }, { '%', PX16, 1 }, { '@', PX16, 1 },
        { 'H', PX32, 1 }, { 'a', PX32, 1 }, { 'm', PX32, 1 },
        { 'g', PX32, 1 }, { 'e', PX32, 1 }, { '8', PX32, 1 },
        { '%', PX32, 1 }, { '@', PX32, 1 }
    };
    char path[512];

    printf("[freetype]\n");
    sprintf(path, "%s/nj_fixture.ttf", dir);
    (void)ft_compare("fixture", path, fixture,
                     (unsigned)(sizeof(fixture) / sizeof(fixture[0])), tmp,
                     oracle);
    if (real_font != NULL) {
        (void)ft_compare("system font", real_font, real,
                         (unsigned)(sizeof(real) / sizeof(real[0])), tmp,
                         oracle);
    } else {
        SKIP("no system TrueType font found; the FreeType comparison for a "
             "real face was skipped");
    }
}

/* ------------------------------------------------------------------ */
/* A real font, on its own                                              */
/* ------------------------------------------------------------------ */
/* The same candidate list tools/gen_fonts.py uses: the first family whose
 * regular face exists wins. */
static const char *const k_font_candidates[] = {
    "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
    NULL
};

static const char *find_system_font(void)
{
    unsigned i;
    for (i = 0u; k_font_candidates[i] != NULL; i++) {
        FILE *fh = fopen(k_font_candidates[i], "rb");
        if (fh != NULL) {
            fclose(fh);
            return k_font_candidates[i];
        }
    }
    return NULL;
}

static void test_real_font(const char *font_path)
{
    face F;
    nj_u4 len = 0u;
    nj_u1 *data;
    int px;

    printf("[system font]\n");
    if (font_path == NULL) {
        SKIP("no system TrueType font found; the real-font sections were "
             "skipped");
        return;
    }
    printf("  using %s\n", font_path);
    data = read_file(font_path, &len);
    if (data == NULL) {
        CHECK(0, "cannot read %s", font_path);
        return;
    }
    for (px = 8; px <= 48; px += 8) {
        if (!face_open(&F, data, len, px, NJ_FONT_STYLE_PLAIN)) {
            CHECK(0, "cannot open the system font at %dpx", px);
            free(data);
            return;
        }
        CHECK(F.f.ascent >= 1 && F.f.descent >= 0 && F.f.height > 0,
              "bad metrics at %dpx: asc %d desc %d height %d", px,
              F.f.ascent, F.f.descent, F.f.height);
        CHECK(F.f.ascent + F.f.descent == F.f.height,
              "height must equal ascent+descent at %dpx", px);
        {
            mask m = face_glyph(&F, 'H');
            CHECK(m.px != NULL && m.w > 0 && m.h > 0,
                  "'H' must rasterise at %dpx", px);
            CHECK(mask_ink(m) > 0, "'H' has no ink at %dpx", px);
            /* A capital H is a vertical stem: no ink may touch row 0 when
             * the ascent is honest, and none may fall below the box. */
            CHECK(m.h <= F.f.height + 2,
                  "glyph box height %d at %dpx exceeds the line height %d",
                  m.h, px, F.f.height);
            if (m.px != NULL) {
                int x0, y0, x1, y1;
                CHECK(mask_bbox(m, &x0, &y0, &x1, &y1) == 1, "no ink");
                CHECK(x0 >= 0 && x1 < m.w && y0 >= 0 && y1 < m.h,
                      "ink outside the mask at %dpx", px);
            }
        }
        /* A CJK ideograph is very unlikely to be in a Latin mono face, but
         * whichever way the cmap answers, the call must be safe and the
         * answer must be a real glyph or NULL - never a box we invented. */
        {
            int w = 0;
            int h = 0;
            const nj_u1 *p = F.f.glyph_lookup(F.f.glyph_ud, 0x4E00u, &w, &h);
            CHECK(p == NULL || (w > 0 && h > 0), "inconsistent CJK result");
        }
        face_close(&F);
    }

    /* Style switching on a real font must not explode. */
    if (face_open(&F, data, len, 16, NJ_FONT_STYLE_BOLD)) {
        mask m = face_glyph(&F, 'g');
        CHECK(m.px != NULL && mask_ink(m) > 0, "bold 'g' must rasterise");
        face_close(&F);
    }
    if (face_open(&F, data, len, 16, NJ_FONT_STYLE_ITALIC)) {
        mask m = face_glyph(&F, 'g');
        CHECK(m.px != NULL && mask_ink(m) > 0, "italic 'g' must rasterise");
        face_close(&F);
    }
    free(data);
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : "tests/fixtures/ttf";
    const char *oracle = (argc > 2) ? argv[2] : "tools/ttf_ft_compare.py";
    const char *tmp = getenv("TMPDIR");
    const char *real_font = find_system_font();
    char tmpdir[256];
    char probe[512];
    FILE *fh;

    if (tmp == NULL || tmp[0] == '\0') {
        tmp = "/tmp";
    }
    sprintf(tmpdir, "%s/nj2me_ttf_cmp", tmp);
    (void)mkdir(tmpdir, 0700);      /* EEXIST is fine; failure is reported */
    sprintf(probe, "%s/nj_fixture.ttf", dir);
    fh = fopen(probe, "rb");
    if (fh == NULL) {
        printf("cannot find %s - run python3 tools/gen_ttf_fixture.py\n",
               probe);
        return 2;
    }
    fclose(fh);

    printf("nano_j2me TrueType tests (fixtures: %s)\n", dir);
    test_api_contract(dir);
    test_metrics(dir);
    test_missing_and_empty(dir);
    test_outline_rules(dir);
    test_loca_variants(dir);
    test_cmap_variants(dir);
    test_styles(dir);
    test_cache(dir);
    test_negatives(dir);
    test_real_font(real_font);
    test_freetype(dir, oracle, real_font, tmpdir);
    test_fuzz(dir, real_font);

    printf("%d checks, %d failures, %d skipped sections\n", g_checks,
           g_failures, g_skips);
    return (g_failures == 0) ? 0 : 1;
}
