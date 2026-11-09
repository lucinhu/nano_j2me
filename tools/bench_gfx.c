/* Fixed-work 2D blit microbenchmark. Output columns:
 * case, seconds, ns/source-pixel, FNV-1a destination checksum, calls.
 * Compile with nj_gfx.c and tests/stub_font.c; see docs/OPTIMIZATION_2026-11-09.md.
 * Compare repeated alternating runs with identical flags and checksums.
 */
#define _POSIX_C_SOURCE 200809L
#include "nj2me/nj_gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static void run(const char *name, int fmt, int w, int h, int tr, int rop, int fill)
{
    int bpp = fmt == NJ_PIXFMT_RGB565 ? 2 : fmt == NJ_PIXFMT_RGB888 ? 3 : 4;
    int dw = w > h ? w + 8 : h + 8;
    int stride = dw * bpp + 5;
    size_t sn = (size_t)w * h * bpp, dn = (size_t)stride * dw;
    unsigned char *sb = malloc(sn + 1), *db = malloc(dn + 1);
    nj_image img;
    nj_surface surf;
    nj_gfx gfx;
    unsigned int checksum = 2166136261u;
    int i, n = 20000000 / (w * h);
    size_t k;
    double begin, elapsed;

    if (!sb || !db) exit(2);
    for (k = 0; k < sn; k++) sb[k + 1] = (unsigned char)((k * 73 + k / 5 + 19) & 255);
    memset(db, 0xA5, dn + 1);
    memset(&img, 0, sizeof img);
    memset(&surf, 0, sizeof surf);
    img.width = w; img.height = h; img.format = fmt; img.pixels = (nj_u4 *)(void *)(sb + 1);
    img.has_alpha = 1;
    surf.width = dw; surf.height = dw; surf.stride = stride;
    surf.format = fmt; surf.pixels = db + 1;
    nj_gfx_init(&gfx, &surf);
    nj_gfx_translate(&gfx, 1, 2);
    nj_gfx_set_clip(&gfx, 0, 0, dw - 3, dw - 4);
    nj_gfx_set_color(&gfx, 0xFF234567u);
    if (n < 1000) n = 1000;
    for (i = 0; i < 20; i++) nj_gfx_draw_image_rop(&gfx, &img, 0, 0, 0, 0, w, h, rop, tr);
    begin = now();
    for (i = 0; i < n; i++) {
        if (fill) nj_gfx_fill_rect(&gfx, 0, 0, w, h);
        else nj_gfx_draw_image_rop(&gfx, &img, 0, 0, 0, 0, w, h, rop, tr);
    }
    elapsed = now() - begin;
    for (k = 0; k < dn; k++) checksum = (checksum ^ db[k + 1]) * 16777619u;
    printf("%s %.9f %.4f %08x %d\n", name, elapsed, elapsed * 1e9 / ((double)n * w * h), checksum, n);
    free(sb); free(db);
}

int main(void)
{
    run("565-mirror-1x1", NJ_PIXFMT_RGB565,1,1,NJ_TRANS_MIRROR,NJ_ROP_PAINT,0);
    run("888-rot270-1x1", NJ_PIXFMT_RGB888,1,1,NJ_TRANS_ROT270,NJ_ROP_PAINT,0);
    run("argb-mirror-1x1", NJ_PIXFMT_ARGB8888,1,1,NJ_TRANS_MIRROR,NJ_ROP_SET,0);
    run("argb-rot90-1x4", NJ_PIXFMT_ARGB8888,1,4,NJ_TRANS_ROT90,NJ_ROP_SET,0);
    run("565-mirror-21x19", NJ_PIXFMT_RGB565,21,19,NJ_TRANS_MIRROR,NJ_ROP_PAINT,0);
    run("565-rot90-120x320", NJ_PIXFMT_RGB565,120,320,NJ_TRANS_ROT90,NJ_ROP_PAINT,0);
    run("565-rot180-240x320", NJ_PIXFMT_RGB565,240,320,NJ_TRANS_ROT180,NJ_ROP_SET,0);
    run("888-mirror-37x29", NJ_PIXFMT_RGB888,37,29,NJ_TRANS_MIRROR,NJ_ROP_PAINT,0);
    run("888-rot270-120x320", NJ_PIXFMT_RGB888,120,320,NJ_TRANS_ROT270,NJ_ROP_SET,0);
    run("argb-rot90-21x19", NJ_PIXFMT_ARGB8888,21,19,NJ_TRANS_ROT90,NJ_ROP_SET,0);
    run("argb-mirror-240x320", NJ_PIXFMT_ARGB8888,240,320,NJ_TRANS_MIRROR,NJ_ROP_SET,0);
    run("control-565-none", NJ_PIXFMT_RGB565,240,320,NJ_TRANS_NONE,NJ_ROP_PAINT,0);
    run("control-argb-paint", NJ_PIXFMT_ARGB8888,37,29,NJ_TRANS_ROT90,NJ_ROP_PAINT,0);
    run("control-565-fill", NJ_PIXFMT_RGB565,240,320,NJ_TRANS_NONE,NJ_ROP_SET,1);
    return 0;
}
