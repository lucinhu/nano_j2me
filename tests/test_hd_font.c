/* Draw real outlines on a mirrored target and compare each physical pixel
 * against individually placed physical-size glyphs at logical advances. */
#include "nj2me/nj_gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%d: %s\n", __LINE__, #expr); exit(1); } } while (0)

enum { WIDTH = 80, HEIGHT = 36, MAX_SCALE = 4 };
typedef struct allocations {
    void *block[NJ_FONTSET_SLOTS + 1];
    int count;
} allocations;

static void *track(void *ud, nj_size bytes)
{
    allocations *a = ud;
    CHECK(a->count < NJ_FONTSET_SLOTS + 1);
    void *mem = malloc(bytes);
    CHECK(mem);
    a->block[a->count++] = mem;
    return mem;
}

static nj_surface surface(nj_u4 *pixels, int scale)
{
    nj_surface s = { .pixels = (nj_u1 *)pixels, .width = WIDTH * scale,
        .height = HEIGHT * scale, .stride = WIDTH * scale * 4, .format = NJ_PIXFMT_ARGB8888 };
    return s;
}

static void check_draw(nj_font font, int scale)
{
    static nj_u4 low[WIDTH * HEIGHT], control[WIDTH * HEIGHT];
    static nj_u4 high[WIDTH * HEIGHT * MAX_SCALE * MAX_SCALE], expected[sizeof high / sizeof *high];
    static const nj_u2 text[] = { 'O', 0xffffu, 'p' };
    static const nj_u1 mono = 0x80;
    const nj_font fallback = { .ascent = 1, .glyph_w = 2, .glyph_h = 1,
        .first_char = 0xffffu, .last_char = 0xffffu, .bitmap = &mono };
    nj_surface l = surface(low, 1), c = surface(control, 1);
    nj_surface h = surface(high, scale), e = surface(expected, scale);
    nj_render_mirror mirror = { .target = &h, .scale = scale };
    nj_gfx g, ref;
    int detailed = 0;
    font.style |= NJ_FONT_STYLE_UNDERLINED;
    font.fallback = &fallback;
    nj_surface_fill(&l, 0xffffffffu); nj_surface_fill(&c, 0xffffffffu);
    nj_surface_fill(&h, 0xffffffffu); nj_surface_fill(&e, 0xffffffffu);
    nj_gfx_init(&g, &l); nj_gfx_set_font(&g, &font);
    nj_gfx_set_color(&g, 0x99000000u);
    nj_gfx_translate(&g, 2, 3);
    nj_gfx_set_clip(&g, 8, 1, 53, 25);
    ref = g; ref.surf = &c;
    nj_gfx_draw_text(&ref, text, 3, 60, 4, NJ_ANCHOR_RIGHT | NJ_ANCHOR_TOP);
    g.mirror = &mirror;
    nj_gfx_draw_text(&g, text, 3, 60, 4, NJ_ANCHOR_RIGHT | NJ_ANCHOR_TOP);
    CHECK(!memcmp(low, control, sizeof low));

    const nj_font *physical = font.scaled_face(font.glyph_ud, scale);
    CHECK(physical);
    nj_gfx_init(&ref, &e); nj_gfx_set_font(&ref, physical);
    nj_gfx_set_color(&ref, 0x99000000u);
    nj_gfx_translate(&ref, 2 * scale, 3 * scale);
    nj_gfx_set_clip(&ref, 8 * scale, scale, 53 * scale, 25 * scale);
    int width = nj_font_string_width(&font, text, 3);
    int baseline = 4 + font.ascent, start = 60 - width, pen = start;
    for (int i = 0; i < 3; ++i) {
        if (text[i] == 0xffffu)
            nj_gfx_fill_rect(&ref, pen * scale, (baseline - 1) * scale, scale, scale);
        else
            nj_gfx_draw_char(&ref, text[i], pen * scale, baseline * scale);
        pen += nj_font_char_width(&font, text[i]);
    }
    nj_gfx_fill_rect(&ref, start * scale, (baseline + 1) * scale, width * scale, scale);
    CHECK(!memcmp(high, expected, (size_t)h.stride * h.height));
    for (int y = 0; y < h.height; ++y)
        for (int x = 0; x < h.width; ++x)
            detailed += high[y * h.width + x] != low[(y / scale) * WIDTH + x / scale];
    CHECK(detailed > 0);
    printf("%dx: %d physical samples differ from enlarged logical pixels\n", scale, detailed);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "tests/fixtures/ttf/nj_fixture.ttf";
    FILE *fp = fopen(path, "rb");
    allocations a = {0};
    nj_fontset fs;
    CHECK(fp && fseek(fp, 0, SEEK_END) == 0);
    long len = ftell(fp);
    CHECK(len > 0 && len <= 0x7fffffffL && fseek(fp, 0, SEEK_SET) == 0);
    nj_u1 *data = malloc((size_t)len);
    CHECK(data && fread(data, 1, (size_t)len, fp) == (size_t)len);
    fclose(fp);
    nj_fontset_init(&fs);
    CHECK(nj_fontset_use_ttf(&fs, data, (nj_u4)len, track, &a) == NJ_FONTSET_SLOTS);
    const nj_font *f = nj_fontset_get(&fs, NJ_FONT_SIZE_MEDIUM, NJ_FONT_STYLE_PLAIN);
    CHECK(a.count == NJ_FONTSET_SLOTS && f->scaled_face);
    check_draw(*f, 2); check_draw(*f, 4); check_draw(*f, 2);
    CHECK(a.count == NJ_FONTSET_SLOTS + 1);
    while (a.count) free(a.block[--a.count]);
    free(data);
    return 0;
}
