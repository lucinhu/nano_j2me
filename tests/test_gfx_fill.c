#include "nj2me/nj_gfx.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { unsigned calls, completed; int decline; } fill_state;
static int fill(void *context, const nj_surface *surface,
                int x, int y, int w, int h, nj_u4 color)
{
    fill_state *state = context;
    ++state->calls;
    assert(x >= 0 && y >= 0 && w > 0 && h > 0);
    assert(x+w <= surface->width && y+h <= surface->height);
    if (state->decline || surface->bpp == 3) return 0;
    for (int row = y; row < y+h; ++row) for (int col = x; col < x+w; ++col)
        memcpy((nj_u1 *)surface->pixels + row*surface->stride + col*surface->bpp,
               &color, surface->bpp);
    ++state->completed;
    return 1;
}
int main(void)
{
    unsigned completed = 0;
    for (int format = NJ_PIXFMT_RGB565; format <= NJ_PIXFMT_ARGB8888; ++format) {
        nj_u1 reference[4096], accelerated[4096];
        int bpp = format == NJ_PIXFMT_RGB565 ? 2 : format == NJ_PIXFMT_RGB888 ? 3 : 4;
        nj_surface a = {.pixels = reference+17, .width = 31, .height = 23,
            .stride = 31*bpp+7, .format = format};
        nj_surface b = a; b.pixels = accelerated+17;
        nj_gfx ga, gb;
        fill_state state = {0};
        memset(reference, 0xa5, sizeof reference);
        memcpy(accelerated, reference, sizeof reference);
        nj_gfx_init(&ga, &a); nj_gfx_init(&gb, &b);
        assert(!gb.fill_rect && !gb.fill_ud);
        gb.fill_rect = fill; gb.fill_ud = &state;
        for (int i = 0; i < 120; ++i) {
            int x = i%47-17, y = i%37-13, w = i%43, h = i%31;
            nj_gfx_set_color(&ga, 0xff000000u+i*1237u);
            nj_gfx_set_color(&gb, ga.color);
            nj_gfx_translate(&ga, i%13-6-ga.translate_x, i%11-5-ga.translate_y);
            nj_gfx_translate(&gb, i%13-6-gb.translate_x, i%11-5-gb.translate_y);
            nj_gfx_set_clip(&ga, i%9, i%7, 31-i%11, 23-i%8);
            nj_gfx_set_clip(&gb, i%9, i%7, 31-i%11, 23-i%8);
            state.decline = i%3 == 0;
            nj_gfx_fill_rect(&ga, x, y, w, h); nj_gfx_fill_rect(&gb, x, y, w, h);
            assert(memcmp(reference, accelerated, sizeof reference) == 0);
        }
        unsigned before = state.calls;
        ga.use_xor = gb.use_xor = 1;
        nj_gfx_fill_rect(&ga, 0, 0, 31, 23); nj_gfx_fill_rect(&gb, 0, 0, 31, 23);
        assert(state.calls == before && memcmp(reference, accelerated, sizeof reference) == 0);
        ga.use_xor = gb.use_xor = 0;
        ga.color = gb.color = 0x70123456;
        nj_gfx_fill_rect(&ga, 0, 0, 31, 23); nj_gfx_fill_rect(&gb, 0, 0, 31, 23);
        assert(state.calls == before && memcmp(reference, accelerated, sizeof reference) == 0);
        completed += state.completed;
    }
    assert(completed > 0);
    printf("PASS clipped opaque fill: %u completed rectangles; fallback, alpha, XOR, padding\n", completed);
}
