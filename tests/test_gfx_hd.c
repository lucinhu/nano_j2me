/* Pixel-level contract for the optional LCDUI physical target. */
#include "nj2me/nj_gfx.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define N 12
#define K 4
#define P (N * K)
#define BG 0xff000000u
#define WHITE 0xffffffffu
#define HALF 0xff800000u
static nj_u4 logical[N * N], physical[P][P + 1];
static nj_surface lo = {logical, N, N, N * 4, NJ_PIXFMT_ARGB8888, 4};
static nj_surface hi = {physical, P, P, (P + 1) * 4, NJ_PIXFMT_ARGB8888, 4};
static nj_render_mirror mirror;
static nj_gfx g;

static void reset(void)
{
    int i;
    nj_gfx_init(&g, &lo);
    memset(&mirror, 0, sizeof mirror);
    mirror.target = &hi;
    mirror.scale = K;
    g.mirror = &mirror;
    nj_gfx_clear(&g, BG);
    for (i = 0; i < P; i++)
        assert(physical[i][P] == 0);
    nj_gfx_set_color(&g, WHITE);
}

static int detailed(void)
{
    int x, y;
    for (y = 0; y < P; y++)
        for (x = 0; x < P; x++)
            if (physical[y][x] != physical[y - y % K][x - x % K])
                return 1;
    return 0;
}

static void geometry(void)
{
    int x, y;
    nj_gfx p;
    reset();
    nj_gfx_translate(&g, 2, 1);
    nj_gfx_set_clip(&g, 1, 1, 6, 5);
    assert(nj_gfx_mirror_context(&g, &p) == K && !p.mirror);
    assert(p.translate_x == 2 * K && p.clip_x == 3 * K && p.clip_w == 6 * K);
    nj_gfx_line(&g, 0, 2, 9, 2);
    for (y = 0; y < P; y++)
        for (x = 0; x < P; x++)
            assert(physical[y][x] == ((y / K == 3 && x / K >= 3 && x / K < 9) ? WHITE : BG));
    reset();
    nj_gfx_set_color(&g, 0x80ff0000u);
    nj_gfx_line(&g, 2, 2, 8, 6);
    assert(detailed());
    assert(physical[2 * K][2 * K] == HALF && physical[7 * K - 1][9 * K - 1] == HALF);
    assert(physical[2 * K][2 * K - 1] == BG && physical[7 * K][8 * K] == BG);
    reset();
    nj_gfx_line(&g, 1, 1, 9, 9);
    /* At 45 degrees a unit stroke spans sqrt(2) logical units vertically. */
    {
        int n = 0;
        for (y = 0; y < P; y++)
            n += physical[y][5 * K] == WHITE;
        assert(n >= 5 && n <= 6);
    }
    reset();
    nj_gfx_set_color(&g, 0x80ff0000u);
    nj_gfx_round_rect(&g, 2, 2, 5, 4, 0, 0);
    for (y = 0; y < P; y++)
        for (x = 0; x < P; x++) {
            int ix = x / K, iy = y / K;
            int covered = (ix >= 2 && ix <= 7 && iy >= 2 && iy <= 6 &&
                           (ix == 2 || ix == 7 || iy == 2 || iy == 6));
            assert(physical[y][x] == (covered ? HALF : BG));
        }
    reset();
    nj_gfx_set_stroke(&g, NJ_STROKE_DOTTED);
    nj_gfx_line(&g, 1, 1, 9, 1);
    for (x = K; x < 10 * K; x++)
        assert(physical[K][x] == ((x / K & 1) ? WHITE : BG));
    reset();
    nj_gfx_fill_arc(&g, 2, 2, 8, 6, 0, 90);
    assert(detailed() && physical[4 * K][7 * K] == WHITE && physical[6 * K][7 * K] == BG);
    reset();
    nj_gfx_set_color(&g, 0x80ff0000u);
    nj_gfx_arc(&g, 1, 1, 9, 7, 0, 360);
    assert(detailed() && physical[K][6 * K] == HALF && physical[4 * K][6 * K] == BG);
    reset();
    nj_gfx_round_rect(&g, 1, 1, 9, 7, 5, 4);
    assert(detailed() && physical[K][5 * K] == WHITE && physical[K][K] == BG);
    reset();
    nj_gfx_fill_round_rect(&g, 1, 1, 9, 7, 5, 4);
    assert(detailed() && physical[4 * K][5 * K] == WHITE && physical[K][K] == BG);
    reset();
    nj_gfx_set_color(&g, 0x80ff0000u);
    nj_gfx_triangle(&g, 1, 1, 9, 4, 5, 8, 0);
    assert(detailed());
    for (y = 0; y < P; y++)
        for (x = 0; x < P; x++)
            assert(physical[y][x] == BG || physical[y][x] == HALF);
    reset();
    nj_gfx_triangle(&g, 1, 1, 9, 4, 5, 8, 1);
    assert(detailed() && physical[K * 4][K * 5] == WHITE);
    reset();
    nj_gfx_blend_coverage(&g, 5, 2, 128);
    for (y = 0; y < P; y++)
        for (x = 0; x < P; x++)
            assert(physical[y][x] == logical[y / K * N + x / K]);
    nj_gfx_fill_rect(&g, INT_MAX, INT_MAX, 10, 10);
    nj_gfx_line(&g, INT_MIN, INT_MIN, INT_MAX, INT_MAX);
    nj_gfx_arc(&g, 2, 2, 3, 4, INT_MAX, INT_MIN);
    assert(physical[0][P] == 0);
}

/* A non-square sprite with distinguishable subpixels and a padded HD pitch. */
static nj_u4 sprite[6], sprite_hd[2 * K][3 * K + 1], mask[6];
static nj_surface src_hi = {sprite_hd, 3 * K, 2 * K, (3 * K + 1) * 4, NJ_PIXFMT_ARGB8888, 4};
static int lookups;
static const nj_surface *lookup(void *ud, const void *pixels, int w, int h)
{
    assert(ud == sprite);
    lookups++;
    if (pixels == sprite) {
        assert(w == 3 && h == 2);
        return &src_hi;
    }
    return NULL;
}

static void images(void)
{
    int x, y, t;
    nj_image img = {3, 2, NJ_PIXFMT_ARGB8888, sprite, 1};
    nj_image stencil = {2, 3, NJ_PIXFMT_ARGB8888, mask, 1};
    nj_u4 packed[3 * K * 2 * K], expected[P * P];
    nj_image big = {3 * K, 2 * K, NJ_PIXFMT_ARGB8888, packed, 1};
    nj_surface ref = {expected, P, P, P * 4, NJ_PIXFMT_ARGB8888, 4};
    nj_gfx oracle;
    nj_u1 rgb[] = {255, 0, 0, 0, 200, 0, 0, 0, 128, 128, 128, 128};
    for (y = 0; y < 2 * K; y++)
        for (x = 0; x < 3 * K; x++)
            packed[y * 3 * K + x] = sprite_hd[y][x] = ((x % 3 == 0   ? 0u
                                                        : x % 3 == 1 ? 128u
                                                                     : 255u)
                                                       << 24) |
                                                      ((nj_u4)(x * 21) << 16) | (nj_u4)(y * 29);
    for (x = 0; x < 6; x++)
        sprite[x] = x == 0 ? 0 : 0x8066aaffu;
    for (t = 0; t < 8; t++) {
        reset();
        mirror.lookup = lookup;
        mirror.ud = sprite;
        lookups = 0;
        nj_gfx_init(&oracle, &ref);
        nj_gfx_clear(&oracle, BG);
        nj_gfx_translate(&g, 1, 2);
        nj_gfx_set_clip(&g, 1, 1, 2, 3);
        nj_gfx_set_clip(&oracle, 2 * K, 3 * K, 2 * K, 3 * K);
        nj_gfx_draw_image_rop(&g, &img, 0, 1, 0, 0, 3, 2, NJ_ROP_PAINT, t);
        nj_gfx_draw_image_rop(&oracle, &big, K, 3 * K, 0, 0, 3 * K, 2 * K, NJ_ROP_PAINT, t);
        assert(lookups == 1);
        for (y = 0; y < P; y++)
            for (x = 0; x < P; x++)
                assert(physical[y][x] == expected[y * P + x]);
    }
    reset();
    mirror.lookup = lookup;
    mirror.ud = sprite;
    mask[0] = 0;
    mask[1] = WHITE;
    mask[2] = WHITE;
    mask[3] = mask[4] = mask[5] = 0;
    nj_gfx_draw_region(&g, &img, 1, 0, 2, 2, NJ_TRANS_ROT90, 0, 0, &stencil, 3, 1, NJ_ROP_SET);
    for (y = 0; y < 2 * K; y++)
        for (x = 0; x < 2 * K; x++) {
            nj_u4 want = mask[(y / K) * 2 + x / K] ? sprite_hd[2 * K - 1 - x][K + y] : BG;
            assert(physical[K + y][3 * K + x] == want);
        }
    reset();
    nj_gfx_draw_rgb(&g, rgb, 6, 93, 2, 3, 0, 0, 2, 2, NJ_ROP_PAINT, NJ_TRANS_ROT270);
    for (y = 0; y < P; y++)
        for (x = 0; x < P; x++)
            assert(physical[y][x] == logical[y / K * N + x / K]);
    nj_gfx_set_xor(&g, 0xff123456u);
    nj_gfx_draw_image(&g, &img, 2, 1, 0, 0, 3, 2);
    nj_gfx_draw_region(&g, &img, 0, 0, 3, 2, NJ_TRANS_ROT90, 1, 0x0a0b0c0du, &stencil, 2, 4,
                       NJ_ROP_PAINT);
    for (y = 0; y < P; y++)
        for (x = 0; x < P; x++)
            assert(physical[y][x] == logical[y / K * N + x / K]);
}

/* A bitmap blends against the existing high-resolution scene. Each subpixel
 * plane of that scene must match an ordinary draw onto the same plane. */
static void bitmap_planes(void)
{
    enum { W = 5, H = 4, PITCH = P * 4 + 3 };
    const int formats[] = {NJ_PIXFMT_ARGB8888, NJ_PIXFMT_RGB888, NJ_PIXFMT_RGB565};
    const int ops[] = {NJ_ROP_SET, NJ_ROP_PAINT, NJ_ROP_OPAQUE};
    const unsigned alpha[] = {0, 255, 127, 1, 254, 255, 255};
    nj_u1 source[1 + W * H * 4], output[1 + PITCH * P];
    nj_u4 samples[N * N];
    nj_surface dest = {output + 1, P, P, PITCH, NJ_PIXFMT_ARGB8888, 4};
    nj_surface plane = {samples, N, N, N * 4, NJ_PIXFMT_ARGB8888, 4};
    for (int fmt = 0; fmt < 3; ++fmt) {
        nj_surface bitmap = {source + 1, W, H, W * (4 - fmt), formats[fmt], 4 - fmt};
        nj_image image = {W, H, formats[fmt], (nj_u4 *)(void *)(source + 1), 0};
        for (int i = 0; i < W * H; ++i)
            nj_surface_set(&bitmap, i % W, i / W, (alpha[i % 7] << 24) | (0x15813u * (i + 1)));
        for (int t = 0; t < 8; ++t) for (int op = 0; op < 3; ++op) {
            reset();
            mirror.target = &dest;
            nj_gfx_translate(&g, 2, 1);
            nj_gfx_set_clip(&g, 1, 1, 2, 4);
            memset(output, 0x6c, sizeof output);
            for (int y = 0; y < P; ++y) for (int x = 0; x < P; ++x)
                nj_surface_set(&dest, x, y, 0x10304050u + (unsigned)(y * P + x) * 0x20315u);
            nj_gfx_draw_image_rop(&g, &image, 0, 1, 1, 0, 4, 3, ops[op], t);
            nj_gfx ref = g;
            ref.surf = &plane;
            ref.mirror = NULL;
            for (int j = 0; j < K; ++j) for (int i = 0; i < K; ++i) {
                for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
                    unsigned pos = (unsigned)((y * K + j) * P + x * K + i);
                    samples[y * N + x] = 0x10304050u + pos * 0x20315u;
                }
                nj_gfx_draw_image_rop(&ref, &image, 0, 1, 1, 0, 4, 3, ops[op], t);
                for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x)
                    assert(nj_surface_get(&dest, x * K + i, y * K + j) == samples[y * N + x]);
            }
            assert(output[0] == 0x6c);
            for (int y = 0; y < P; ++y) for (int x = P * 4; x < PITCH; ++x)
                assert(output[1 + y * PITCH + x] == 0x6c);
        }
    }
}

int main(void)
{
    geometry();
    images();
    bitmap_planes();
    puts("physical LCDUI geometry, image transforms, clipping and compositing passed");
    return 0;
}
