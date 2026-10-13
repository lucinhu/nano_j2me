/*
 * A port can supply the glyphs.
 *
 * The point of the fallback-slot API is that a port whose platform already
 * has a text stack - Android's Paint, a windowing system, a printer - does not
 * have to ship a font file or a glyph table.  This test stands in for such a
 * port: it hands the font set a provider that draws a solid box for any
 * character at or above 0x4E00, and checks that
 *
 *   - the provider is consulted for a wide character the built-in faces
 *     cannot draw, at the size the slot stands for,
 *   - a Latin character still comes from the built-in bitmap face, so
 *     installing a provider cannot change the look of text that already
 *     worked,
 *   - a slot left NULL keeps its own fallback, which is what lets a port fill
 *     in only the sizes it has cells for.
 */
#include "nj2me/nj.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL %s\n", what);
        failures++;
    } else {
        printf("ok   %s\n", what);
    }
}

/* One coverage row per pixel: a filled box, so the bytes are recognisable. */
static nj_u1 g_rows[16 * 16];
static int g_calls;

static const nj_u1 *box_lookup(void *ud, nj_u2 ch, int *w, int *h)
{
    int cell = *(const int *)ud;

    g_calls++;
    if (ch < 0x4E00) {
        return NULL;   /* not ours: let the built-in face answer */
    }
    memset(g_rows, 0x80, (size_t)cell * (size_t)cell);
    *w = cell;
    *h = cell;
    return g_rows;
}

static int box_advance(void *ud, nj_u2 ch)
{
    (void)ch;
    return *(const int *)ud;
}

int main(void)
{
    static int cell_small = 12;
    static int cell_large = 16;
    static nj_font small_face;
    static nj_font large_face;
    const nj_font *slots[NJ_FONTSET_SLOTS];
    nj_fontset fs;
    const nj_font *f;
    int i;
    int latin;
    int wide_medium;
    int wide_large;
    int wide_bold;

    memset(&small_face, 0, sizeof(small_face));
    small_face.name = "test-small";
    small_face.height = 12;
    small_face.ascent = 10;
    small_face.descent = 2;
    small_face.size = NJ_FONT_SIZE_MEDIUM;
    small_face.first_char = 1;
    small_face.last_char = 0;          /* no fixed cell of its own */
    small_face.glyph_w = 12;
    small_face.glyph_h = 12;
    small_face.glyph_lookup = box_lookup;
    small_face.glyph_ud = &cell_small;
    small_face.glyph_advance = box_advance;
    small_face.coverage = 1;           /* antialiased rows */

    large_face = small_face;
    large_face.name = "test-large";
    large_face.size = NJ_FONT_SIZE_LARGE;
    large_face.glyph_w = 16;
    large_face.glyph_h = 16;
    large_face.glyph_ud = &cell_large;

    for (i = 0; i < NJ_FONTSET_SLOTS; i++) {
        slots[i] = NULL;
    }
    /* plain: small(12), medium(12), large(16); bold left alone on purpose */
    slots[0 * NJ_FONT_SLOT_STYLES + 0] = &small_face;
    slots[1 * NJ_FONT_SLOT_STYLES + 0] = &small_face;
    slots[2 * NJ_FONT_SLOT_STYLES + 0] = &large_face;

    nj_fontset_init(&fs);
    nj_fontset_set_fallback_slots(&fs, slots, NJ_FONTSET_SLOTS);

    f = nj_fontset_get(&fs, NJ_FONT_SIZE_MEDIUM, NJ_FONT_STYLE_PLAIN);
    latin = nj_font_char_width(f, 'A');
    wide_medium = nj_font_char_width(f, 0x4E2D);
    check(wide_medium == 12, "medium wide glyph comes from the provider (12)");
    check(latin == f->glyph_w, "latin still comes from the built-in cell");

    f = nj_fontset_get(&fs, NJ_FONT_SIZE_LARGE, NJ_FONT_STYLE_PLAIN);
    wide_large = nj_font_char_width(f, 0x4E2D);
    check(wide_large == 16, "the large slot uses the large provider cell");

    f = nj_fontset_get(&fs, NJ_FONT_SIZE_LARGE, NJ_FONT_STYLE_BOLD);
    wide_bold = nj_font_char_width(f, 0x4E2D);
    check(wide_bold == f->glyph_w,
          "a NULL slot keeps its own fallback, not the provider");

    check(g_calls > 0, "the provider was actually called");

    if (failures == 0) {
        printf("all fallback-font checks passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", failures);
    return 1;
}
