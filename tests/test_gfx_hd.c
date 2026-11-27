/* Pixel-level contract for the optional LCDUI physical target. */
#include "nj2me/nj_gfx.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
        mirror.smooth_assets = 1; /* the genuine physical source wins */
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
    mirror.smooth_assets = 1;
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

/* Independent scalar reference: generate the 4x frame in source orientation,
 * then transform this physical image using the ordinary Sprite blitter. */
static void smooth_frames(void)
{
    enum { W = 3, H = 2, A = W + 2 };
    nj_u4 atlas[A * (H + 2)], grown[W * K * H * K], expected[P * P], low[N * N];
    const nj_u4 frame[W * H] = {0xffff0000u, 0x00ff00ffu, 0x80400000u,
                                 0x20e00000u, 0x60000000u, 0xffffff00u};
    nj_image img = {A,H+2,NJ_PIXFMT_ARGB8888,atlas,1};
    nj_image big = {W*K,H*K,NJ_PIXFMT_ARGB8888,grown,0};
    nj_surface ref = {expected,P,P,P*4,NJ_PIXFMT_ARGB8888,4};
    nj_gfx oracle;
    for (int i = 0; i < A*(H+2); ++i) atlas[i] = 0xff0000ffu;
    for (int y = 0; y < H; ++y) memcpy(atlas+(y+1)*A+1, frame+y*W, W*4);
    for (int y = 0; y < H*K; ++y) for (int x = 0; x < W*K; ++x) {
        double rgb[3] = {0,0,0};
        for (int j = 0; j < H; ++j) for (int i = 0; i < W; ++i) {
            double u = (x + 0.5)/K - 0.5, v = (y + 0.5)/K - 0.5;
            u = u < 0 ? 0 : u > W-1 ? W-1 : u;
            v = v < 0 ? 0 : v > H-1 ? H-1 : v;
            double wx = 1 - fabs(i-u), wy = 1 - fabs(j-v);
            if (wx <= 0 || wy <= 0) continue;
            nj_u4 c = frame[j*W+i];
            for (int channel = 0; channel < 3; ++channel)
                rgb[channel] += wx*wy*(c >> 24)*((c >> (channel*8)) & 255u)/255.;
        }
        grown[y*W*K+x] = BG;
        for (int channel = 0; channel < 3; ++channel)
            grown[y*W*K+x] |= (nj_u4)(rgb[channel]+0.5) << (channel*8);
    }
    assert(grown[0] == 0xffff0000u && grown[3] == 0xff9f0000u);
    for (int t = 0; t < 8; ++t) for (int clip = 0; clip <= 1; ++clip) {
        reset(); nj_gfx_translate(&g,1,1);
        if (clip) nj_gfx_set_clip(&g,2,2,1,2);
        nj_gfx_draw_region(&g,&img,1,1,W,H,t,0,0,NULL,1,1,NJ_ROP_PAINT);
        memcpy(low,logical,sizeof low);
        reset(); mirror.smooth_assets = 1;
        nj_gfx_translate(&g,1,1);
        if (clip) nj_gfx_set_clip(&g,2,2,1,2);
        nj_gfx_draw_region(&g,&img,1,1,W,H,t,0,0,NULL,1,1,NJ_ROP_PAINT);
        assert(!memcmp(low,logical,sizeof low));
        nj_gfx_init(&oracle,&ref); nj_gfx_clear(&oracle,BG);
        if (clip) nj_gfx_set_clip(&oracle,3*K,3*K,K,2*K);
        nj_gfx_draw_image_rop(&oracle,&big,2*K,2*K,0,0,W*K,H*K,NJ_ROP_SET,t);
        for (int y = 0; y < P; ++y) for (int x = 0; x < P; ++x)
            assert(physical[y][x] == expected[y*P+x]);
    }
    for (int op = 0; op < 4; ++op) {
        reset(); mirror.smooth_assets = 1;
        nj_image stencil = {W,H,NJ_PIXFMT_ARGB8888,mask,1};
        nj_gfx_draw_region(&g,&img,1,1,W,H,NJ_TRANS_NONE,op==2,WHITE,op==3?&stencil:NULL,
                           0,0,op==0?NJ_ROP_SET:op==1?NJ_ROP_OPAQUE:NJ_ROP_PAINT);
        for (int y = 0; y < P; ++y) for (int x = 0; x < P; ++x)
            assert(physical[y][x] == logical[y/K*N+x/K]);
    }
}

/* Compare the opaque shortcut with the four-tap premultiplied equation at
 * every sample. Include odd scales, the uncached weight path, flat columns,
 * descending channels, a translucent destination and unaligned row starts. */
static void smooth_opaque_ramps(void)
{
    enum { W = 4, H = 3, MAX_K = 33, PAD = 3 };
    const int scales[] = {2,3,4,5,6,7,8,33};
    nj_u4 colors[W * H] = {
        0xffff0080u,0xffff0080u,0xff00ff7fu,0xff01027eu,
        0xff00ff7fu,0xff00ff7fu,0xffff0080u,0xfffe017fu,
        0xff01027eu,0xff01027eu,0xfffeff81u,0xff007f00u
    };
    nj_u4 small[W * H];
    nj_u1 storage[1 + (W * MAX_K * 4 + PAD) * H * MAX_K];
    nj_image image = {W,H,NJ_PIXFMT_ARGB8888,colors,1};
    nj_surface logical_target = {small,W,H,W*4,NJ_PIXFMT_ARGB8888,4};
    for (size_t s = 0; s < sizeof scales / sizeof *scales; ++s) {
        int k = scales[s], pitch = W * k * 4 + PAD;
        nj_surface target = {storage+1,W*k,H*k,pitch,NJ_PIXFMT_ARGB8888,4};
        nj_render_mirror m = {.target = &target, .scale = k, .smooth_assets = NJ_ASSET_SMOOTH};
        nj_gfx paint;
        nj_gfx_init(&paint, &logical_target);
        paint.mirror = &m;
        memset(storage, 0x73, sizeof storage);
        nj_gfx_clear(&paint, 0x7320df45u);
        nj_gfx_draw_image(&paint, &image, 0, 0, 0, 0, W, H);
        assert(!memcmp(small, colors, sizeof small));
        for (int y = 0; y < H*k; ++y) {
            const nj_u1 *row = storage + 1 + y*pitch;
            for (int x = 0; x < W*k; ++x) {
                /* Unbounded coordinate of the physical sample centre in Q8. */
                int u = (int)floor(((x+.5)/k - .5)*256 + .5);
                int v = (int)floor(((y+.5)/k - .5)*256 + .5);
                u = u < 0 ? 0 : u > (W-1)*256 ? (W-1)*256 : u;
                v = v < 0 ? 0 : v > (H-1)*256 ? (H-1)*256 : v;
                nj_u4 want = 0xff000000u, got;
                for (int shift = 0; shift < 24; shift += 8) {
                    nj_u4 sum = 0;
                    for (int sy = 0; sy < H; ++sy) for (int sx = 0; sx < W; ++sx) {
                        int wx = 256 - (int)fabs(sx*256 - u), wy = 256 - (int)fabs(sy*256 - v);
                        if (wx > 0 && wy > 0)
                            sum += (nj_u4)(wx * wy) * 255u * ((colors[sy*W+sx] >> shift) & 255u);
                    }
                    want |= ((((sum + 32768u) >> 16) + 127u) / 255u) << shift;
                }
                memcpy(&got, row+x*4, sizeof got);
                assert(got == want);
            }
            for (int p = 0; p < PAD; ++p) assert(row[W*k*4+p] == 0x73);
        }
        assert(storage[0] == 0x73);
    }
}

/* A filtered sprite may be painted into a transparent Image before it reaches
 * the screen. Both the Image and the second blit consume straight ARGB. */
static void smooth_offscreen(void)
{
    nj_u4 edge[] = {0xffff0000u, 0x0000ff00u};
    nj_image art = {2, 1, NJ_PIXFMT_ARGB8888, edge, 1};
    nj_image layer_image = {3, 2, NJ_PIXFMT_ARGB8888, sprite, 1};
    nj_surface layer = {sprite, 3, 2, 3 * 4, NJ_PIXFMT_ARGB8888, 4};
    nj_render_mirror layer_mirror = {.target = &src_hi, .scale = K, .smooth_assets = 1};
    nj_gfx paint;
    nj_gfx_init(&paint, &layer);
    paint.mirror = &layer_mirror;

    nj_gfx_clear(&paint, 0x000000ffu);
    nj_gfx_draw_image(&paint, &art, 0, 0, 0, 0, 2, 1);
    /* At sample 3 the red texel covers 5/8. Hidden green and blue contribute zero. */
    assert(sprite_hd[0][3] == 0x9fff0000u);
    reset();
    nj_gfx_clear(&g, WHITE);
    mirror.smooth_assets = 1;
    mirror.lookup = lookup;
    mirror.ud = sprite;
    nj_gfx_draw_image(&g, &layer_image, 0, 0, 0, 0, 3, 2);
    assert(physical[0][3] == 0xffff6060u);

    nj_gfx_clear(&paint, 0x800000ffu);
    nj_gfx_draw_image(&paint, &art, 0, 0, 0, 0, 2, 1);
    assert(sprite_hd[0][3] == 0xcfc4003bu);
}

/* Compare a translucent diagonal silhouette to its analytic half-plane in
 * the interior. A second fixture puts a square highlight beside a vertical
 * edge. Render each as a standalone image first, then compare transformed,
 * clipped draws out of a deliberately hostile atlas to that physical image. */
static void pixel_art_frames(void)
{
    enum { W = 7, H = 6, A = W + 2, MAX_K = 8, SIDE = N * MAX_K };
    nj_u4 frame[W * H], atlas[A * (H + 2)], grown[W * H * MAX_K * MAX_K];
    nj_u4 output[SIDE][SIDE + 1], expected[SIDE][SIDE + 1], low[N * N];
    nj_image img = {W, H, NJ_PIXFMT_ARGB8888, frame, 1};
    nj_image sheet = {A, H + 2, NJ_PIXFMT_ARGB8888, atlas, 1};
    const nj_u4 back = 0x800000ffu, red = 0x80ff0000u, over = 0xc0aa0055u;
    int changed = 0;
    for (int k = 2; k <= MAX_K; ++k) {
        int size = N * k;
        nj_surface target = {output, size, size, sizeof output[0], NJ_PIXFMT_ARGB8888, 4};
        nj_surface ref = {expected, size, size, sizeof expected[0], NJ_PIXFMT_ARGB8888, 4};
        nj_render_mirror m = {.target = &target, .scale = k, .smooth_assets = NJ_ASSET_PIXEL_ART};
        nj_image big = {W * k, H * k, NJ_PIXFMT_ARGB8888, grown, 1};
        nj_gfx paint, oracle;
        nj_gfx_init(&paint, &lo);
        paint.mirror = &m;
        nj_gfx_init(&oracle, &ref);
        for (int diagonal = 0; diagonal < 2; ++diagonal) {
            for (int i = 0; i < A * (H + 2); ++i) atlas[i] = red;
            for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
                nj_u4 hidden = (nj_u4)(y * W + x + 1) * 0x1001u;
                nj_u4 c = diagonal ? (x >= y ? red : hidden) : (x < 2 ? BG : WHITE);
                if (!diagonal && x == 4 && y == 3) c = 0xff77bb77u;
                frame[y * W + x] = atlas[(y + 1) * A + x + 1] = c;
            }
            nj_gfx_clear(&paint, back);
            nj_gfx_draw_image(&paint, &img, 0, 0, 0, 0, W, H);
            for (int y = 0; y < H * k; ++y) for (int x = 0; x < W * k; ++x) {
                nj_u4 c = grown[y * W * k + x] = output[y][x];
                if (!diagonal) {
                    assert(c == frame[(y / k) * W + x / k]);
                } else {
                    assert(c == over || c == back);
                    changed += c != (x / k >= y / k ? over : back);
                    if (x >= k && x < (W - 1) * k && y >= k && y < (H - 1) * k)
                        assert(c == (2 * (x - y) >= -k ? over : back));
                }
            }
            for (int t = 0; t < 8; ++t) for (int clip = 0; clip < 2; ++clip) {
                memset(output, 0x6c, sizeof output);
                memset(expected, 0x6c, sizeof expected);
                nj_gfx_init(&paint, &lo);
                nj_gfx_init(&oracle, &ref);
                nj_gfx_clear(&oracle, back);
                nj_gfx_translate(&paint, 1, 1);
                if (clip) {
                    nj_gfx_set_clip(&paint, 2, 1, 3, 4);
                    nj_gfx_set_clip(&oracle, 3 * k, 2 * k, 3 * k, 4 * k);
                }
                for (int pass = 0; pass < 2; ++pass) {
                    paint.mirror = pass ? &m : NULL;
                    nj_gfx_clear(&paint, back);
                    nj_gfx_draw_region(&paint, &sheet, 1, 1, W, H, t, 0, 0, NULL, 0, 0,
                                       NJ_ROP_PAINT);
                    if (!pass) memcpy(low, logical, sizeof low);
                    else assert(!memcmp(low, logical, sizeof low));
                }
                nj_gfx_draw_image_rop(&oracle, &big, k, k, 0, 0, W * k, H * k, NJ_ROP_SET, t);
                assert(!memcmp(output, expected, sizeof output)); /* includes pitch padding */
            }
            nj_gfx_init(&paint, &lo);
            paint.mirror = &m;
        }
    }
    printf("pixel-art: %d physical samples refined on seven two-colour diagonals\n", changed);
}


/* FSR1 artwork sampling: the cubic may reshape the opaque interior, flat runs
 * reproduce their texels exactly, a fully translucent image falls back to the
 * smooth path byte for byte, stencils bypass the filter, and transformed or
 * clipped draws stay consistent with the same pixels produced as a physical
 * image and blitted by the raw path. */
static void fsr1_frames(void)
{
    enum { W = 8, H = 6, MAXK = 8, SIDE = N * MAXK };
    nj_u4 art[W * H], soft[W * H], grown[W * H * MAXK * MAXK];
    nj_u4 output[SIDE][SIDE + 1], snapshot[SIDE][SIDE + 1];
    nj_u4 mask_pixels[W * H], frame[P][P + 1], expected[P][P + 1];
    nj_image img = {W, H, NJ_PIXFMT_ARGB8888, art, 1};
    nj_image soft_img = {W, H, NJ_PIXFMT_ARGB8888, soft, 1};
    nj_image stencil = {W, H, NJ_PIXFMT_ARGB8888, mask_pixels, 1};
    const int scales[] = {2, 3, 4, 5, 8};
    const int dark = 0x20, light = 0xe0;
    for (int i = 0; i < W * H; ++i) {
        int x = i % W, alpha = 0x30 + (i * 29) % 0x90;
        nj_u4 level = (nj_u4)(x < W / 2 ? dark : light);
        art[i] = 0xff000000u | level << 16 | level << 8 | level;
        soft[i] = (nj_u4)alpha << 24 | (nj_u4)(0x40 + (i * 13) % 0x80) << 16 |
                  (nj_u4)(0x30 + (i * 7) % 0x90) << 8 | (nj_u4)(0x20 + (i * 5) % 0xa0);
        mask_pixels[i] = i % 2 ? 0xffffffffu : 0xff000000u;
    }
    /* An isolated highlight verifies the 4x4 clamp: the cubic must not ring
     * past the neighbourhood's brightest value. */
    art[2 * W + 2] = 0xffffffffu;

    for (size_t n = 0; n < sizeof scales / sizeof *scales; ++n) {
        int k = scales[n];
        nj_surface target = {output, N * k, N * k, sizeof output[0], NJ_PIXFMT_ARGB8888, 4};
        nj_render_mirror m;
        int row = (H / 2) * k + k / 2;
        int refined = 0, refined_step = 0, smooth = 0, smooth_step = 0, previous = -1;
        memset(&m, 0, sizeof m);
        m.target = &target;
        m.scale = k;
        m.smooth_assets = NJ_ASSET_FSR1;
        nj_gfx_init(&g, &lo);
        g.mirror = &m;
        nj_gfx_clear(&g, BG);
        nj_gfx_draw_image(&g, &img, 0, 0, 0, 0, W, H);
        for (int y = 0; y < H * k; ++y)
            for (int x = 0; x < W * k; ++x) {
                nj_u4 c = output[y][x];
                assert((c >> 24) == 255u);
                for (int ch = 0; ch < 3; ++ch) {
                    int v = (int)((c >> (ch * 8)) & 255u);
                    assert(v >= dark && v <= 255);
                }
            }
        /* Columns right of the edge keep a clean 4x4 neighbourhood, so their
         * texels must survive the filter unchanged. */
        for (int y = 0; y < H * k; ++y)
            for (int x = (W / 2 + 1) * k; x < W * k; ++x)
                assert(output[y][x] == (0xff000000u | (nj_u4)light << 16 |
                                        (nj_u4)light << 8 | (nj_u4)light));
        for (int x = (W / 2 - 1) * k; x < (W / 2 + 1) * k; ++x) {
            int v = (int)((output[row][x] >> 16) & 255u);
            if (v != dark && v != light) ++refined;
            if (previous >= 0 && abs(v - previous) > refined_step) refined_step = abs(v - previous);
            previous = v;
        }
        m.smooth_assets = NJ_ASSET_SMOOTH;
        nj_gfx_init(&g, &lo);
        g.mirror = &m;
        nj_gfx_clear(&g, BG);
        nj_gfx_draw_image(&g, &img, 0, 0, 0, 0, W, H);
        previous = -1;
        for (int x = (W / 2 - 1) * k; x < (W / 2 + 1) * k; ++x) {
            int v = (int)((output[row][x] >> 16) & 255u);
            if (v != dark && v != light) ++smooth;
            if (previous >= 0 && abs(v - previous) > smooth_step) smooth_step = abs(v - previous);
            previous = v;
        }
        assert(refined <= smooth);
        assert(refined_step >= smooth_step);

        /* Translucent artwork takes the smooth branch verbatim. */
        m.smooth_assets = NJ_ASSET_SMOOTH;
        nj_gfx_init(&g, &lo);
        g.mirror = &m;
        nj_gfx_clear(&g, BG);
        nj_gfx_draw_image(&g, &soft_img, 0, 0, 0, 0, W, H);
        for (int y = 0; y < H * k; ++y) memcpy(snapshot[y], output[y], (size_t)W * k * 4);
        m.smooth_assets = NJ_ASSET_FSR1;
        nj_gfx_init(&g, &lo);
        g.mirror = &m;
        nj_gfx_clear(&g, BG);
        nj_gfx_draw_image(&g, &soft_img, 0, 0, 0, 0, W, H);
        for (int y = 0; y < H * k; ++y)
            assert(!memcmp(output[y], snapshot[y], (size_t)W * k * 4));

        /* A stencil bypasses the filter exactly like the nearest mode. */
        m.smooth_assets = NJ_ASSET_NEAREST;
        nj_gfx_init(&g, &lo);
        g.mirror = &m;
        nj_gfx_clear(&g, BG);
        nj_gfx_draw_region(&g, &img, 0, 0, W, H, NJ_TRANS_NONE, 0, 0, &stencil, 0, 0,
                           NJ_ROP_PAINT);
        for (int y = 0; y < H * k; ++y) memcpy(snapshot[y], output[y], (size_t)W * k * 4);
        m.smooth_assets = NJ_ASSET_FSR1;
        nj_gfx_init(&g, &lo);
        g.mirror = &m;
        nj_gfx_clear(&g, BG);
        nj_gfx_draw_region(&g, &img, 0, 0, W, H, NJ_TRANS_NONE, 0, 0, &stencil, 0, 0,
                           NJ_ROP_PAINT);
        for (int y = 0; y < H * k; ++y)
            assert(!memcmp(output[y], snapshot[y], (size_t)W * k * 4));
    }

    /* Transformed and clipped draws must equal the same pixels produced as a
     * standalone physical image and blitted by the raw path. */
    {
        const int k = K;
        nj_u4 diagonal[W * H];
        nj_image art_img = {W, H, NJ_PIXFMT_ARGB8888, diagonal, 1};
        nj_image big = {W * k, H * k, NJ_PIXFMT_ARGB8888, grown, 1};
        nj_surface mirror_surface = {frame, P, P, sizeof frame[0], NJ_PIXFMT_ARGB8888, 4};
        nj_surface ref_surface = {expected, P, P, sizeof expected[0], NJ_PIXFMT_ARGB8888, 4};
        nj_render_mirror m;
        nj_gfx oracle;
        for (int i = 0; i < W * H; ++i) {
            int x = i % W, y = i / W;
            diagonal[i] = (x + y) % 3 == 0 ? 0x80ff0000u :
                          x < W / 2 ? 0xff202020u : 0xffe0e0e0u;
        }
        memset(&m, 0, sizeof m);
        m.target = &mirror_surface;
        m.scale = k;
        m.smooth_assets = NJ_ASSET_FSR1;
        memset(frame, 0x6c, sizeof frame);
        nj_gfx_init(&g, &lo);
        g.mirror = &m;
        nj_gfx_clear(&g, BG);
        nj_gfx_draw_image(&g, &art_img, 0, 0, 0, 0, W, H);
        for (int y = 0; y < H * k; ++y)
            for (int x = 0; x < W * k; ++x) grown[y * W * k + x] = frame[y][x];
        for (int t = 0; t < 8; ++t) for (int clip = 0; clip < 2; ++clip) {
            memset(frame, 0x6c, sizeof frame);
            memset(expected, 0x6c, sizeof expected);
            nj_gfx_init(&g, &lo);
            g.mirror = &m;
            nj_gfx_translate(&g, 1, 1);
            if (clip) nj_gfx_set_clip(&g, 2, 1, 3, 4);
            nj_gfx_clear(&g, BG);
            nj_gfx_draw_region(&g, &art_img, 0, 0, W, H, t, 0, 0, NULL, 0, 0, NJ_ROP_PAINT);
            nj_gfx_init(&oracle, &ref_surface);
            nj_gfx_clear(&oracle, BG);
            if (clip) nj_gfx_set_clip(&oracle, 3 * k, 2 * k, 3 * k, 4 * k);
            nj_gfx_draw_image_rop(&oracle, &big, k, k, 0, 0, W * k, H * k, NJ_ROP_SET, t);
            for (int y = 0; y < P; ++y) {
                int bad = 0;
                for (int x = 0; x < P; ++x) bad += frame[y][x] != expected[y][x];
                if (bad) {
                    fprintf(stderr, "fsr1 mismatch t=%d clip=%d row=%d bad=%d\n", t, clip, y, bad);
                    assert(0);
                }
            }
        }
    }
    puts("fsr1 assets: opaque interior reshaped, translucent samples and stencils unchanged");
}

/* A visible diagonal ensures the bypass checks exercise art that would
 * otherwise be reshaped. Compare physical output against the raw blitter. */
static void pixel_art_bypass(void)
{
    enum { S = 5 };
    nj_u4 colors[S * S], mask_pixels[S * S], baseline[P][P + 1];
    nj_u1 packed[S * S * 3];
    nj_image img = {S, S, NJ_PIXFMT_ARGB8888, colors, 1};
    nj_image stencil = {S, S, NJ_PIXFMT_ARGB8888, mask_pixels, 1};
    for (int i = 0; i < S * S; ++i) {
        colors[i] = i % S >= i / S ? WHITE : 0xff225522u;
        mask_pixels[i] = i % 3 ? WHITE : 0;
        for (int n = 0; n < 3; ++n) packed[i * 3 + n] = (nj_u1)(colors[i] >> (16 - n * 8));
    }
    for (int op = 0; op < 5; ++op) for (int pass = 0; pass < 2; ++pass) {
        reset();
        mirror.smooth_assets = pass ? NJ_ASSET_PIXEL_ART : NJ_ASSET_NEAREST;
        if (op == 4)
            nj_gfx_draw_rgb(&g, packed, S * 3, 255, 1, 2, 0, 0, S, S, NJ_ROP_PAINT, NJ_TRANS_ROT90);
        else
            nj_gfx_draw_region(&g, &img, 0, 0, S, S, NJ_TRANS_ROT90, op == 2, 0xff123456u,
                               op == 3 ? &stencil : NULL, 1, 2,
                               op == 0 ? NJ_ROP_SET : op == 1 ? NJ_ROP_OPAQUE : NJ_ROP_PAINT);
        if (!pass) memcpy(baseline, physical, sizeof baseline);
        else assert(!memcmp(baseline, physical, sizeof baseline));
    }
    /* The highlight touches two different colours along a staircase. */
    colors[2 * S + 2] = 0xffff6600u;
    colors[S + 2] = colors[2 * S + 1] = BG;
    reset();
    mirror.smooth_assets = NJ_ASSET_PIXEL_ART;
    nj_gfx_draw_image(&g, &img, 0, 0, 0, 0, S, S);
    for (int y = 2 * K; y < 3 * K; ++y) for (int x = 2 * K; x < 3 * K; ++x)
        assert(physical[y][x] == colors[2 * S + 2]);
}

/* Flat cells may share a row copy across colour changes. Transparent and
 * translucent cells must preserve the distinct samples underneath them. */
static void pixel_art_run_boundaries(void)
{
    nj_u4 colors[] = {WHITE, BG, 0x00010203u, WHITE, 0x80408020u, BG, WHITE, BG};
    nj_image strip = {8, 1, NJ_PIXFMT_ARGB8888, colors, 1};
    nj_u4 expected[P][P + 1];
    for (int t = 0; t < 8; ++t) {
        for (int pass = 0; pass < 2; ++pass) {
            reset();
            for (int y = 0; y < P; ++y) for (int x = 0; x < P; ++x)
                physical[y][x] = BG | ((nj_u4)(y * P + x) * 73u);
            mirror.smooth_assets = pass ? NJ_ASSET_PIXEL_ART : NJ_ASSET_NEAREST;
            nj_gfx_set_clip(&g, 1, 1, 8, 7);
            nj_gfx_draw_image_rop(&g, &strip, 2, 2, 0, 0, 8, 1, NJ_ROP_PAINT, t);
            if (!pass) memcpy(expected, physical, sizeof expected);
            else assert(!memcmp(expected, physical, sizeof expected));
        }
    }
}

/* Compare an eligible wide row with one-cell draws over an identical HD
 * background. The last texels exercise a late alpha miss and raw SET. */
static void wide_bitmap_rows(void)
{
    enum { W = 37, S = 3 };
    nj_u4 source[2 * W], low[W * 2], high[2 * S][W * S + 1], expected[2 * S][W * S + 1];
    nj_surface a = {low, W, 2, W * 4, NJ_PIXFMT_ARGB8888, 4};
    nj_surface b = {high, W * S, 2 * S, (W * S + 1) * 4, NJ_PIXFMT_ARGB8888, 4};
    nj_render_mirror m = {.target = &b, .scale = S};
    nj_image img = {W, 2, NJ_PIXFMT_ARGB8888, source, 0};
    for (int i = 0; i < 2 * W; ++i) source[i] = BG | (nj_u4)i * 41237u;
    source[2 * W - 1] = 0x00765432u;
    source[2 * W - 2] = 0x80654321u;
    for (int paint = 0; paint <= 1; ++paint) {
        int rop = paint ? NJ_ROP_PAINT : NJ_ROP_SET;
        for (int wide = 0; wide <= 1; ++wide) {
            nj_gfx ctx;
            memset(low, 0x35, sizeof low);
            memset(high, 0x65, sizeof high);
            nj_gfx_init(&ctx, &a);
            ctx.mirror = &m;
            int width = wide ? W : 1;
            for (int x = 0; x < W; x += width)
                nj_gfx_draw_image_rop(&ctx, &img, x, 0, x, 0, width, 2, rop, NJ_TRANS_NONE);
            if (!wide) memcpy(expected, high, sizeof high);
            else assert(!memcmp(expected, high, sizeof high));
        }
    }
}

/* Adjacent opaque source rows can share filtered output, but a translucent
 * texel just outside the clip still samples each physical backdrop row. Use
 * single-column clips as an independent check of the wide-row shortcut. */
static void smooth_repeated_rows(void)
{
    enum { W = 24, H = 5, S = 3, PITCH = W * S * 4 + 3 };
    nj_u4 colors[W * H], low[W * H];
    nj_u1 high[1 + PITCH * H * S], expected[sizeof high];
    nj_image image = {W, H, NJ_PIXFMT_ARGB8888, colors, 0};
    nj_surface a = {low, W, H, W * 4, NJ_PIXFMT_ARGB8888, 4};
    nj_surface b = {high + 1, W * S, H * S, PITCH, NJ_PIXFMT_ARGB8888, 4};
    nj_render_mirror m = {.target = &b, .scale = S, .smooth_assets = NJ_ASSET_SMOOTH};
    const int transforms[] = {NJ_TRANS_NONE, NJ_TRANS_MIRROR_ROT180};
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x)
        colors[y * W + x] = BG | (nj_u4)x * 0x090715u;
    for (int edge = 0; edge < 3; ++edge) {
        /* Left halo differs vertically; right halo blends onto the target. */
        if (edge == 1) colors[2 * W + 1] = WHITE;
        if (edge == 2) for (int y = 0; y < H; ++y) colors[y * W + W - 2] = 0x804020ffu;
        for (int t = 0; t < 2; ++t) for (int wide = 0; wide <= 1; ++wide) {
            nj_gfx ctx;
            nj_gfx_init(&ctx, &a);
            ctx.mirror = &m;
            memset(low, 0, sizeof low);
            for (size_t i = 0; i < sizeof high; ++i) high[i] = (nj_u1)(i * 17u);
            int width = wide ? W - 4 : 1;
            for (int x = 2; x < W - 2; x += width) {
                nj_gfx_set_clip(&ctx, x, 1, width, H - 2);
                nj_gfx_draw_image_rop(&ctx, &image, 0, 0, 0, 0, W, H, NJ_ROP_PAINT, transforms[t]);
            }
            if (!wide) memcpy(expected, high, sizeof high);
            else assert(!memcmp(expected, high, sizeof high));
        }
    }
}

int main(void)
{
    smooth_repeated_rows();
    wide_bitmap_rows();
    geometry();
    images();
    bitmap_planes();
    smooth_frames();
    smooth_opaque_ramps();
    smooth_offscreen();
    pixel_art_frames();
    fsr1_frames();
    pixel_art_bypass();
    pixel_art_run_boundaries();
    puts("physical LCDUI geometry, image transforms, clipping and compositing passed");
    return 0;
}
