/* Headless integration test. Use VK_DRIVER_FILES to pin one hardware ICD.
 * Exit 77 means this machine has no supported Vulkan device. --bench also
 * reports steady-state synchronous latency for a full 1280x720 transaction. */
#define _POSIX_C_SOURCE 200809L
#include "nj2me/nj_gfx_vulkan.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#define CHECK(x)                                                                                       \
    do {                                                                                               \
        if (!(x)) {                                                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                    \
            exit(1);                                                                                   \
        }                                                                                              \
    } while (0)
enum { W = 24, H = 16, CS = W + 4, ZS = W + 5 };
typedef struct fixture {
    nj_u4 colors[H][CS];
    nj_s4 keys[H][ZS];
    nj_surface s;
    nj_gfx_triangle_cmd t[3];
    nj_gfx_triangles p;
} fixture;
static const nj_u4 background = 0x79573b83u;
static nj_gfx_triangle_cmd triangle(nj_u4 color, double z) {
    nj_gfx_triangle_cmd t = {0};
    int i;
    t.vertices[1].x = 64;
    t.vertices[2].y = 64;
    for (i = 0; i < 3; ++i) {
        t.vertices[i].inv_w = 1;
        t.vertices[i].color = color;
        t.vertices[i].depth = z;
    }
    t.blend = 68;
    t.depth_test = t.depth_write = t.alpha_write = 1;
    return t;
}
static void init(fixture *f) {
    int x, y;
    memset(f, 0, sizeof *f);
    for (y = 0; y < H; ++y) {
        for (x = 0; x < CS; ++x)
            f->colors[y][x] = background;
        for (x = 0; x < ZS; ++x)
            f->keys[y][x] = x < W ? 0 : -12345;
    }
    f->s = (nj_surface){f->colors, W, H, CS * 4, NJ_PIXFMT_ARGB8888, 4};
    f->t[0] = triangle(0xffb72d91u, 0.25);
    f->p =
        (nj_gfx_triangles){NJ_GFX_TRIANGLES_VERSION, &f->s, &f->keys[0][0], ZS, 2, 3, 19, 11, f->t, 1};
}
static void submit(nj_vk_renderer *r, fixture *f) {
    nj_u4 submitted = 0xdeadbeef;
    CHECK(nj_vk_renderer_triangles(r, &f->p, &submitted) == NJ_GFX_ACCEL_DONE);
    CHECK(submitted == f->p.count);
}
static int in_roi(const fixture *f, int x, int y) {
    return x >= f->p.x && y >= f->p.y && x < f->p.x + f->p.width && y < f->p.y + f->p.height;
}
static void depth_matrix(nj_vk_renderer *r) {
    static const nj_s4 ranks[] = {0,       1,       2,       255,      256,      65535,   65536,
                                  8388607, 8388608, 8388609, 16777214, 16777215, 16777216};
    const int nr = (int)(sizeof ranks / sizeof *ranks);
    int i, test, write, x, y;
    fixture f;
    for (i = 1; i < nr; ++i)
        for (test = 0; test < 2; ++test)
            for (write = 0; write < 2; ++write) {
                init(&f);
                f.t[0] = triangle(0xffd02e17, 1.0 - (ranks[i] - 1) / 16777215.0);
                f.t[0].depth_test = test;
                f.t[0].depth_write = write;
                for (y = 0; y < H; ++y)
                    for (x = 0; x < W; ++x)
                        f.keys[y][x] = ranks[(x + y) % nr];
                submit(r, &f);
                for (y = 0; y < H; ++y)
                    for (x = 0; x < ZS; ++x) {
                        nj_s4 orig = x < W ? ranks[(x + y) % nr] : -12345;
                        int hit = in_roi(&f, x, y) && (!test || ranks[i] >= orig);
                        CHECK(f.keys[y][x] == (hit && write ? ranks[i] : orig));
                        if (x < CS)
                            CHECK(f.colors[y][x] == (hit ? 0xffd02e17 : background));
                    }
            }
}
static void near_color(nj_u4 got, nj_u4 want, int tolerance) {
    int i;
    for (i = 0; i < 32; i += 8)
        if (abs((int)((got >> i) & 255) - (int)((want >> i) & 255)) > tolerance) {
            fprintf(stderr, "color got %08x expected %08x tolerance %d\n", got, want, tolerance);
            exit(1);
        }
}
static double clamp(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }
static void textures(nj_vk_renderer *r) {
    fixture f;
    nj_u1 bytes[4 * 4];
    int bpp, filter, repeat, func, mutation, k, x, y;
    for (bpp = 3; bpp <= 4; ++bpp)
        for (filter = 208; filter <= 209; ++filter)
            for (repeat = 0; repeat <= 1; ++repeat)
                for (func = 226; func <= 228; ++func)
                    for (mutation = 0; mutation < 2; ++mutation) {
                        int i;
                        init(&f);
                        for (i = 0; i < 4; ++i)
                            for (k = 0; k < bpp; ++k)
                                bytes[i * bpp + k] = (nj_u1)(17 + i * 39 + k * 13 + mutation * 31);
                        f.t[0] = triangle(0x8090b0d0, 0.5);
                        f.t[0].texture = bytes;
                        f.t[0].filter = filter;
                        f.t[0].texture_bpp = bpp;
                        f.t[0].texture_function = func;
                        f.t[0].texture_width = f.t[0].texture_height = 2;
                        f.t[0].wrap_s = 240 + repeat;
                        f.t[0].wrap_t = 240 + (1 - repeat);
                        f.t[0].vertices[0].u = -0.32f;
                        f.t[0].vertices[1].u = 3.49f;
                        f.t[0].vertices[2].v = 6.13f;
                        f.t[0].vertices[1].inv_w = 2;
                        f.t[0].vertices[2].inv_w = 0.5f;
                        submit(r, &f);
                        for (y = 3; y < 14; ++y)
                            for (x = 2; x < 21; ++x) {
                                double b = (x + 0.5) / 64, c = (y + 0.5) / 64, a = 1 - b - c,
                                       q = a + 2 * b + 0.5 * c;
                                double u =
                                    (a * f.t[0].vertices[0].u + 2 * b * f.t[0].vertices[1].u) / q;
                                double v = c * 0.5 * f.t[0].vertices[2].v / q;
                                double tex[4] = {0};
                                nj_u4 want = 0;
                                u = repeat ? u - floor(u) : clamp(u, 0, 1);
                                v = repeat ? clamp(v, 0, 1) : v - floor(v);
                                u = u * 2 - (filter == 209 ? 0.5 : 0);
                                v = v * 2 - (filter == 209 ? 0.5 : 0);
                                for (i = 0; i < (filter == 209 ? 4 : 1); ++i) {
                                    int ix = (int)clamp(floor(u) + (i & 1), 0, 1),
                                        iy = (int)clamp(floor(v) + (i >> 1), 0, 1);
                                    double fu = u - floor(u), fv = v - floor(v);
                                    double weight = filter == 208 ? 1
                                                                  : ((i & 1) ? fu : 1 - fu) *
                                                                        ((i >> 1) ? fv : 1 - fv);
                                    for (k = 0; k < 4; ++k)
                                        tex[k] +=
                                            weight *
                                            (k == 3 && bpp == 3 ? 255 : bytes[(iy * 2 + ix) * bpp + k]);
                                }
                                for (k = 0; k < 4; ++k) {
                                    int shift = (16 - k * 8 + 32) % 32;
                                    double tint = (0x8090b0d0u >> shift) & 255;
                                    double value = func == 227 ? tint * tex[k] / 255
                                                   : k == 3 && (func == 226 || bpp == 3) ? tint
                                                                                         : tex[k];
                                    want |= (nj_u4)floor(value + 0.5) << shift;
                                }
                                near_color(f.colors[y][x], want, 1);
                            }
                    }
}
static void materials(nj_vk_renderer *r) {
    fixture f;
    const nj_u4 tint[] = {0x7e225064, 0x9068c4ee, 0xdbb89c2f};
    int blend, fog, i, y, x, k;
    for (blend = 64; blend <= 68; ++blend)
        for (fog = 0; fog < 3; ++fog) {
            init(&f);
            f.t[0].blend = blend;
            f.t[0].fog = fog == 0 ? 0 : 79 + fog;
            f.t[0].fog_color = 0x8745a9;
            f.t[0].fog_near = 0.1f;
            f.t[0].fog_far = 2;
            f.t[0].fog_density = 0.7f;
            for (i = 0; i < 3; ++i) {
                f.t[0].vertices[i].color = tint[i];
                f.t[0].vertices[i].depth = i * 0.375;
            }
            f.t[0].vertices[1].inv_w = 2;
            f.t[0].vertices[2].inv_w = 0.5f;
            submit(r, &f);
            for (y = f.p.y; y < f.p.y + f.p.height; ++y)
                for (x = f.p.x; x < f.p.x + f.p.width; ++x) {
                    double b = (x + 0.5) / 64, c = (y + 0.5) / 64, a = 1 - b - c,
                           distance = 1 / (a + 2 * b + 0.5 * c);
                    double vis = fog == 0   ? 1
                                 : fog == 1 ? exp(-f.t[0].fog_density * distance)
                                            : (2 - distance) / 1.9;
                    double src[4], dst[4];
                    nj_u4 expected = 0;
                    for (k = 0; k < 4; ++k) {
                        int shift = k * 8;
                        src[k] = (a * ((tint[0] >> shift) & 255) + b * ((tint[1] >> shift) & 255) +
                                  c * ((tint[2] >> shift) & 255)) /
                                 255;
                        dst[k] = ((background >> shift) & 255) / 255.0;
                        if (k < 3)
                            src[k] =
                                vis * src[k] + (1 - vis) * ((f.t[0].fog_color >> shift) & 255) / 255.0;
                    }
                    for (k = 0; k < 4; ++k) {
                        double out = src[k];
                        if (k == 3) {
                            if (blend == 64)
                                out = src[3] + dst[3] * (1 - src[3]);
                            if (blend >= 65 && blend <= 67)
                                out = 1;
                        } else
                            switch (blend) {
                            case 64:
                                out = src[k] * src[3] + dst[k] * (1 - src[3]);
                                break;
                            case 65:
                                out = src[k] * src[3] + dst[k];
                                break;
                            case 66:
                                out = src[k] * dst[k];
                                break;
                            case 67:
                                out = 2 * src[k] * dst[k];
                                break;
                            }
                        expected |= (nj_u4)floor(255 * clamp(out, 0, 1) + 0.5) << (k * 8);
                    }
                    near_color(f.colors[y][x], expected, 2);
                    CHECK(abs(f.keys[y][x] -
                              (16777216 - (int)floor((0.375 * b + 0.75 * c) * 16777215 + 0.5))) <= 1);
                }
        }
}
static void order_and_fallback(nj_vk_renderer *r) {
    fixture f;
    nj_u4 result = 0xabcd;
    nj_u1 texture[4] = {55, 222, 17, 127};
    nj_u4 saved_colors[H][CS];
    nj_s4 saved_keys[H][ZS];
    int i;
    init(&f);
    f.p.count = 2;
    f.t[0] = triangle(0x71aabbcc, 0.125);
    f.t[0].texture = texture;
    f.t[0].texture_width = f.t[0].texture_height = 1;
    f.t[0].texture_bpp = 4;
    f.t[0].wrap_s = f.t[0].wrap_t = 241;
    f.t[0].filter = 208;
    f.t[0].texture_function = 228;
    f.t[0].alpha_threshold = 128;
    f.t[1] = triangle(0x7aff1122, 0.75);
    submit(r, &f);
    CHECK(f.colors[4][4] == 0x7aff1122u && f.keys[4][4] == 4194305);
    texture[3] = 128;
    f.t[0].alpha_write = 0;
    submit(r, &f);
    CHECK(f.colors[4][4] == 0x7a37de11u && f.keys[4][4] == 14680064);
    memcpy(saved_colors, f.colors, sizeof saved_colors);
    memcpy(saved_keys, f.keys, sizeof saved_keys);
    f.t[1].blend = 1234;
    CHECK(nj_vk_renderer_triangles(r, &f.p, &result) == NJ_GFX_ACCEL_DECLINE);
    CHECK(result == 0xabcd && !memcmp(f.colors, saved_colors, sizeof saved_colors) &&
          !memcmp(f.keys, saved_keys, sizeof saved_keys));
    /* A valid packet whose second command is outside the supported RGB565 subset. */
    f.t[1].blend = 64;
    f.s.format = NJ_PIXFMT_RGB565;
    f.s.bpp = 2;
    CHECK(nj_gfx_triangles_validate(&f.p, &result));
    CHECK(nj_vk_renderer_triangles(r, &f.p, &result) == NJ_GFX_ACCEL_DECLINE);
    CHECK(result == 0xabcd && !memcmp(f.colors, saved_colors, sizeof saved_colors) &&
          !memcmp(f.keys, saved_keys, sizeof saved_keys));
    init(&f);
    f.p.depth = NULL;
    f.t[0].depth_test = f.t[0].depth_write = 0;
    f.t[0].vertices[1].x = 18;
    f.t[0].vertices[2].y = 13;
    for (i = 0; i < 3; ++i)
        f.t[0].vertices[i].color = 0xff5500cc;
    submit(r, &f);
    CHECK(f.colors[4][4] == 0xff5500cc && f.colors[12][18] == background);
}
static double millis(void) {
#ifdef _WIN32
    LARGE_INTEGER t, hz;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&hz);
    return (double)t.QuadPart * 1000.0 / hz.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
#endif
}
static int compare_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static void bench(nj_vk_renderer *r) {
    enum { BW = 1280, BH = 720, COLS = 40, ROWS = 24, TRIS = 2 * COLS * ROWS, REPS = 50 };
    nj_u4 *pixels = calloc(BW * BH, 4), submitted = 0;
    nj_s4 *keys = calloc(BW * BH, 4);
    nj_gfx_triangle_cmd *t = calloc(TRIS, sizeof *t);
    nj_surface s = {pixels, BW, BH, BW * 4, NJ_PIXFMT_ARGB8888, 4};
    nj_gfx_triangles p = {NJ_GFX_TRIANGLES_VERSION, &s, keys, BW, 0, 0, BW, BH, t, TRIS};
    uint8_t texture[] = {35, 75, 219, 255, 122, 25, 1, 255, 42, 9, 5, 255, 11, 2, 3, 255};
    int i, j;
    double samples[REPS];
    CHECK(pixels && keys && t);
    for (i = 0; i < TRIS; ++i) {
        static const int tx[] = {0, 1, 0, 1, 1, 0}, ty[] = {0, 0, 1, 0, 1, 1};
        int x = (i / 2) % COLS, y = (i / 2) / COLS;
        t[i] = triangle(0xffffffff, 0.25);
        t[i].texture = texture;
        t[i].texture_width = t[i].texture_height = 2;
        t[i].texture_bpp = 4;
        t[i].texture_function = 227;
        t[i].wrap_s = t[i].wrap_t = 241;
        t[i].filter = 209;
        for (j = 0; j < 3; ++j) {
            int k = 3 * (i % 2) + j;
            t[i].vertices[j].x = (float)((x + tx[k]) * BW / COLS);
            t[i].vertices[j].y = (float)((y + ty[k]) * BH / ROWS);
            t[i].vertices[j].u = (float)tx[k];
            t[i].vertices[j].v = (float)ty[k];
        }
    }
    for (i = -5; i < REPS; ++i) {
        double start = millis();
        CHECK(nj_vk_renderer_triangles(r, &p, &submitted) == 1 && submitted == TRIS);
        if (i >= 0)
            samples[i] = millis() - start;
    }
    CHECK(keys[BW * BH - 1] == 12582912 && pixels[BW * BH - 1] != 0);
    qsort(samples, REPS, sizeof *samples, compare_double);
    printf("%dx%d, %d textured triangles, %d synchronous calls: median %.3f ms, p95 %.3f ms\n", BW, BH,
           TRIS, REPS, samples[REPS / 2], samples[47]);
    free(pixels);
    free(keys);
    free(t);
}
/* A mutable atlas reused at the same unaligned address. Time the complete
 * synchronous packet with a small ROI so texture upload cost is visible. */
static void bench_atlas(nj_vk_renderer *r, int side) {
    enum { REPS = 50 };
    fixture f;
    size_t bytes = (size_t)side * side * 4;
    uint8_t *storage = malloc(bytes + 1);
    double samples[REPS];
    CHECK(storage);
    uint8_t *rgba = storage + 1;
    memset(rgba, 0x6a, bytes);
    init(&f);
    f.t[0].depth_test = f.t[0].depth_write = 0;
    f.t[0].texture = rgba;
    f.t[0].texture_width = f.t[0].texture_height = side;
    f.t[0].texture_bpp = 4;
    f.t[0].texture_function = 228;
    f.t[0].wrap_s = f.t[0].wrap_t = 240;
    f.t[0].filter = 208;
    for (int i = 0; i < REPS + 5; ++i) {
        int x = (29 * i) % side, y = (103 * i) % side;
        uint8_t *texel = rgba + ((size_t)y * side + x) * 4;
        texel[0] = (uint8_t)i;
        for (int j = 0; j < 3; ++j) {
            f.t[0].vertices[j].u = ((float)x + 0.5f) / side;
            f.t[0].vertices[j].v = ((float)y + 0.5f) / side;
        }
        double start = millis();
        submit(r, &f);
        if (i >= 5) samples[i - 5] = millis() - start;
        CHECK(f.colors[4][4] == (0x6a006a6au | ((nj_u4)i << 16)));
    }
    qsort(samples, REPS, sizeof *samples, compare_double);
    printf("%dx%d mutable RGBA atlas, %d synchronous calls: median %.3f ms, p95 %.3f ms\n",
           side, side, REPS, samples[REPS / 2], samples[47]);
    free(storage);
}

int main(int argc, char **argv) {
    nj_vk_renderer *r = nj_vk_renderer_create();
    if (!r) {
#if NJ_ENABLE_GFX_ACCEL
        nj_port port = {0}, saved;
        port.gfx_triangles_auto = 1;
        port.gfx_triangles_session_ud = &port;
        port.gfx_triangles_list_ud = &port;
        memcpy(&saved, &port, sizeof saved);
        CHECK(!nj_vk_renderer_attach(&port) && !memcmp(&saved, &port, sizeof port));
#endif
        puts("No supported Vulkan graphics device, original port preserved");
        return 77;
    }
    depth_matrix(r);
    textures(r);
    materials(r);
    order_and_fallback(r);
    puts("Vulkan color/depth, guarded ROI, perspective textures, blends, fog, cutouts, ordering and "
         "atomic declines passed");
    if (argc > 1 && !strcmp(argv[1], "--bench"))
        bench(r);
    if (argc > 1 && !strcmp(argv[1], "--bench-atlas")) {
        bench_atlas(r, 256);
        bench_atlas(r, 1024);
    }
    nj_vk_renderer_destroy(r);
#if NJ_ENABLE_GFX_ACCEL
    {
        nj_port port = {0}, saved;
        port.gfx_triangles_auto = 1;
        port.gfx_triangles_session_ud = &port;
        port.gfx_triangles_list_ud = &port;
        CHECK(nj_vk_renderer_attach(&port));
        CHECK(port.gfx_triangles == nj_vk_renderer_triangles &&
              port.gfx_hd_triangles == nj_vk_renderer_triangles &&
              port.gfx_triangles_ud == port.gfx_hd_triangles_ud && port.gfx_hd_destroy &&
              !port.gfx_triangles_auto && !port.gfx_triangles_session_ud &&
              !port.gfx_triangles_list_ud);
        memcpy(&saved, &port, sizeof saved);
        CHECK(!nj_vk_renderer_attach(&port) && !memcmp(&saved, &port, sizeof port));
        port.gfx_hd_destroy(port.gfx_hd_triangles_ud);
    }
#endif
    return 0;
}
