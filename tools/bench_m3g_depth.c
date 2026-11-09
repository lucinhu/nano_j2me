/*
 * Fixed-work M3G depth raster benchmark: 18 cases, 1200 triangles per case,
 * 38400 covered pixels per triangle. This is not a full-game FPS benchmark.
 *
 * Compile this translation unit with the configured build's M3G feature flags
 * and -DNJ_M3G_SOURCE='"/absolute/path/to/nj_lib_m3g.c"'. The included source
 * exposes the private raster entry point; link with that build's libnj2me and
 * normal platform libraries. Do not compile another M3G object alongside it.
 * Define NJ_SCREEN_DEPTH for the projected-depth implementation; leave it
 * undefined for the former inverse-w implementation. Keep both source copies
 * and compiler/link flags fixed when comparing binaries.
 *
 * Output: case,process-CPU-nanoseconds-per-covered-pixel,FNV-1a-checksum.
 * Timing includes per-triangle setup and calls, but excludes checksums and
 * depth validation. Pin to one CPU and alternate before/after executions.
 * "depth_off" disables testing but keeps writes; "disabled" disables both.
 *
 * The checksum includes pixels and five padding bytes per row, with an
 * unaligned target start. Compare all 18 checksums between implementations.
 * Constant depths use exact independent keys. Projected sloped depths use
 * pixel-centre barycentrics with one tick of interpolation rounding tolerance.
 * Depth-disabled cases must leave the entire depth buffer untouched.
 * Raw before/after depth keys intentionally differ and must not be compared.
 */
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include NJ_M3G_SOURCE

static double now_cpu(void)
{
    struct timespec t;

    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t)) {
        abort();
    }
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

int main(void)
{
    static const char *names[] = {
        "flat_argb", "flat_565", "clamp_tinted_argb", "repeat_tinted_565",
        "gouraud_nearest_argb", "linear_argb", "rgb_replace_argb"
    };
    const float xy[6] = {0, 0, 0, 320, 240, 0};
    float inv[3] = {.25f, .25f, .25f};
#ifdef NJ_SCREEN_DEPTH
    double window_z[3];
#endif
    const float uv[6] = {0, 0, 0, 1, 1, 0};
    const nj_u4 flat[3] = {0xffb3d591u, 0xffb3d591u, 0xffb3d591u};
    const nj_u4 gradient[3] = {0xfff52781u, 0xff4ac759u, 0xff75a2d3u};
    nj_u1 *mem = malloc(320u * 965u + 1u), tex[16 * 16 * 4];
    nj_s4 *z = calloc(240u * 320u, sizeof(*z));
    int mode, i, depth;

    if (!mem || !z) {
        return 2;
    }
    for (i = 0; i < 256; i++) {
        tex[i * 4] = (nj_u1)(i * 17);
        tex[i * 4 + 1] = (nj_u1)(i * 43);
        tex[i * 4 + 2] = (nj_u1)(i * 97);
        tex[i * 4 + 3] = 255;
    }
    for (depth = 0; depth < 4; depth++) {
        for (mode = 0; mode < 7; mode++) {
            if (depth >= 2 && mode != 0 && mode != 2) {
                continue;
            }
            nj_surface surf;
            m3g_sw sw;
            double start, end;
            nj_u4 sum = 2166136261u;
            int loops = 1200, bpp = (mode == 1 || mode == 3) ? 2 : 4;

            memset(&sw, 0, sizeof(sw));
            memset(mem, 0xa5, 320u * 965u + 1u);
            memset(z, 0, 240u * 320u * sizeof(*z));
            surf.pixels = mem + 1;
            surf.width = 240;
            surf.height = 320;
            surf.stride = 240 * bpp + 5;
            surf.format = bpp == 2 ? NJ_PIXFMT_RGB565 : NJ_PIXFMT_ARGB8888;
            surf.bpp = bpp;
            sw.surf = &surf;
            sw.z = z;
            sw.vw = 240;
            sw.vh = 320;
            sw.cull = 162;
            sw.winding = 168;
            sw.blend = 68;
            sw.awrite = 1;
            sw.dwrite = depth != 3;
            sw.dtest = depth == 1 || depth == 2;
            sw.wrap_s = sw.wrap_t = mode == 3 ? 241 : 240;
            sw.texfunc = mode == 6 ? 228 : 227;
            sw.filt_image = mode == 5 ? 209 : 210;
            sw.filt_level = 208;
            if (mode >= 2) {
                sw.tex = tex;
                sw.tex_w = 16;
                sw.tex_h = 16;
                sw.tex_bpp = mode == 6 ? 3 : 4;
            }
            start = now_cpu();
            for (i = 0; i < loops; i++) {
                inv[0] = inv[1] = inv[2] =
                    .25f + ((depth == 1 || depth == 2)
                                ? (float)i * (1.0f / 4096.0f) : 0.0f);
                if (depth == 2) {
                    inv[1] += .04f;
                    inv[2] += .08f;
                }
#ifdef NJ_SCREEN_DEPTH
                window_z[0] = 1.0 - (double)inv[0];
                window_z[1] = 1.0 - (double)inv[1];
                window_z[2] = 1.0 - (double)inv[2];
                if (!sw_tri(&sw, xy, inv, window_z, uv, mode >= 2,
                            mode == 4 ? gradient : flat, NULL, 0)) {
                    return 3;
                }
#else
                if (!sw_tri(&sw, xy, inv, uv, mode >= 2,
                            mode == 4 ? gradient : flat, NULL, 0)) {
                    return 3;
                }
#endif
            }
            end = now_cpu();
            for (i = 0; i < 320 * surf.stride; i++) {
                sum ^= mem[1 + i];
                sum *= 16777619u;
            }
            {
                nj_s4 expected;
                int count = 0;
#ifdef NJ_SCREEN_DEPTH
                expected = 1 + (nj_s4)((double)inv[0] * 16777215.0 + 0.5);
#else
                expected = (nj_s4)(inv[0] * 65536.0f);
#endif
                for (i = 0; i < 240 * 320; i++) {
                    if (z[i]) {
                        if (depth == 2) {
#ifdef NJ_SCREEN_DEPTH
                            double iv = (double)inv[0] +
                                ((double)inv[2] - inv[0]) *
                                    ((double)(i % 240) + .5) / 240.0 +
                                ((double)inv[1] - inv[0]) *
                                    ((double)(i / 240) + .5) / 320.0;
                            nj_s4 oracle = 1 + (nj_s4)(iv * 16777215.0 + .5);

                            if (abs(z[i] - oracle) > 1) {
                                fprintf(stderr, "depth oracle %d %d\n", z[i], oracle);
                                return 4;
                            }
#endif
                        } else if (z[i] != expected) {
                            return 4;
                        }
                        count++;
                    }
                }
                if (count != (depth == 3 ? 0 : 38400)) {
                    fprintf(stderr, "covered=%d\n", count);
                    return 5;
                }
            }
            printf("%s_%s,%.6f,%08x\n",
                   depth == 3 ? "disabled" : depth == 2 ? "sloped_on" :
                   depth == 1 ? "non_tie_on" : "depth_off",
                   names[mode], (end - start) / (loops * 38400.0), sum);
        }
    }
    free(z);
    free(mem);
    return 0;
}
