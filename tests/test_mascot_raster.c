/*
 * Mascot Capsule Micro3D v3: the compact texture blob and the triangle
 * rasteriser in src/midp/nj_mascot_raster.c.
 *
 * The blob half builds BMP files in memory (CORE and INFO40 headers, 8-bit
 * indices, bottom-up and top-down rows, 4-byte row padding, the broken
 * raster-offset clamp, NPOT padding to a power of two, the 64x64 environment
 * map) and checks what comes out of query/fill/view.
 *
 * The raster half checks the reference arithmetic directly (flat and smooth
 * colour, HALF/ADD/SUB, toon gating, colour key, shade palette rows) and then
 * compares whole framebuffers against golden hashes.  The golden hashes come
 * from the MIT reference rasteriser: every case below was rendered through
 * .local-tools/freej2me-plus Rasterizer.java by the differential oracle in
 * .local-tools/debug-runs/micro3d-round7-20261022/raster and matched this
 * implementation byte for byte (37/37 cases) before the hashes were frozen.
 *
 * Bounds are part of the contract: a crafted blob must never widen a sample
 * mask past its payload, and the rasteriser must survive off-screen vertices,
 * negative fixed-point attributes and degenerate triangles.  Those paths are
 * exercised here and under -fsanitize=undefined,address.
 */

#include "nj2me/nj.h"
#include "nj_mascot_raster.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 32
#define H 24

static int checks, failures;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printf("  FAIL  %s\n", what);
    } else {
        printf("  ok    %s\n", what);
    }
}

/* ------------------------------------------------------------------ */
/* BMP builders                                                        */
/* ------------------------------------------------------------------ */

static void put_u16(nj_u1 *p, nj_u4 v)
{
    p[0] = (nj_u1)(v & 0xFF);
    p[1] = (nj_u1)((v >> 8) & 0xFF);
}

static void put_u32(nj_u1 *p, nj_u4 v)
{
    p[0] = (nj_u1)(v & 0xFF);
    p[1] = (nj_u1)((v >> 8) & 0xFF);
    p[2] = (nj_u1)((v >> 16) & 0xFF);
    p[3] = (nj_u1)((v >> 24) & 0xFF);
}

/* palette[i] = 0xFF000000 | (i << 16) | (i << 8) | i, raster byte at
 * (row, col) = row * width + col (in file order). */
static nj_u1 *bmp_info40(nj_s4 width, nj_s4 height, nj_s4 top_down,
                         nj_s4 palette_entries, nj_s4 broken_offset,
                         nj_s4 *out_len)
{
    nj_s4 stride = (width + 3) & ~3;
    nj_s4 pal = palette_entries > 0 ? palette_entries : 256;
    nj_s4 off = 14 + 40 + pal * 4;
    nj_s4 len = off + stride * height;
    nj_u1 *b = (nj_u1 *)calloc(1, (size_t)len);
    nj_s4 i, y, x;

    b[0] = 'B';
    b[1] = 'M';
    put_u32(b + 10, broken_offset ? 0u : (nj_u4)off);
    put_u32(b + 14, 40);
    put_u32(b + 18, (nj_u4)width);
    put_u32(b + 22, (nj_u4)(top_down ? -height : height));
    put_u16(b + 28, 8);
    put_u32(b + 34, 0); /* biSizeImage */
    put_u32(b + 46, (nj_u4)palette_entries);
    for (i = 0; i < pal; i++) {
        b[14 + 40 + i * 4] = (nj_u1)i;
        b[14 + 40 + i * 4 + 1] = (nj_u1)i;
        b[14 + 40 + i * 4 + 2] = (nj_u1)i;
    }
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            b[off + y * stride + x] = (nj_u1)((y * width + x) & 0xFF);
        }
    }
    if (out_len) {
        *out_len = len;
    }
    return b;
}

static nj_u1 *bmp_core(nj_s4 width, nj_s4 height, nj_s4 *out_len)
{
    nj_s4 stride = (width + 3) & ~3;
    nj_s4 off = 14 + 12 + 256 * 4;
    nj_s4 len = off + stride * height;
    nj_u1 *b = (nj_u1 *)calloc(1, (size_t)len);
    nj_s4 i, y, x;

    b[0] = 'B';
    b[1] = 'M';
    put_u32(b + 10, (nj_u4)off);
    put_u32(b + 14, 12);
    put_u16(b + 18, (nj_u4)width);
    put_u16(b + 20, (nj_u4)height);
    put_u16(b + 24, 8);
    for (i = 0; i < 256; i++) {
        b[14 + 12 + i * 4] = (nj_u1)(255 - i);
        b[14 + 12 + i * 4 + 1] = (nj_u1)i;
        b[14 + 12 + i * 4 + 2] = (nj_u1)(i / 2);
    }
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            b[off + y * stride + x] = (nj_u1)((y * width + x) & 0xFF);
        }
    }
    if (out_len) {
        *out_len = len;
    }
    return b;
}

/* ------------------------------------------------------------------ */
/* Blob helpers                                                        */
/* ------------------------------------------------------------------ */

static nj_u1 *blob_from_bmp(const nj_u1 *bmp, nj_s4 bmp_len, nj_s4 model,
                            nj_s4 *out_len)
{
    nj_s4 size = 0;
    nj_u1 *blob;

    if (nj_mascot_texture_query(bmp, bmp_len, model, &size) != NJ_MR_OK
        || size <= 0) {
        return NULL;
    }
    blob = (nj_u1 *)calloc(1, (size_t)size);
    if (!blob) {
        return NULL;
    }
    if (nj_mascot_texture_fill(bmp, bmp_len, model, blob, size) != NJ_MR_OK) {
        free(blob);
        return NULL;
    }
    *out_len = size;
    return blob;
}

static nj_u4 pal_at(const nj_mascot_texture *t, nj_s4 i)
{
    const nj_u4 *p = (const nj_u4 *)(const void *)t->palette;
    return p[i];
}

static nj_u4 fnv1a(const nj_u4 *px, nj_s4 count)
{
    nj_u4 h = 2166136261u;
    nj_s4 i;

    for (i = 0; i < count; i++) {
        nj_u4 v = px[i];
        int k;
        for (k = 0; k < 4; k++) {
            h ^= (v >> (k * 8)) & 0xFFu;
            h *= 16777619u;
        }
    }
    return h;
}

/* ------------------------------------------------------------------ */
/* Blob checks                                                         */
/* ------------------------------------------------------------------ */

static void test_blob_info40(void)
{
    nj_s4 len = 0;
    nj_u1 *bmp = bmp_info40(8, 8, 0, 0, 0, &len);
    nj_s4 blen = 0;
    nj_u1 *blob = blob_from_bmp(bmp, len, 1, &blen);
    nj_mascot_texture t;

    check(blob != NULL, "INFO40 8x8 8-bit model texture decodes");
    check(blob && nj_mascot_texture_view(blob, blen, &t) == NJ_MR_OK,
          "the decoded model blob views");
    if (blob && blen > 0) {
        check(t.width_bit == 3 && t.height_bit == 3,
              "8x8 is padded to 8x8 (width/height bits are 3)");
        check(t.len_mask == 63, "the sample mask covers the padded area");
        check((t.flags & NJ_MR_TEX_MODEL) != 0, "the model flag is set");
        /* Bottom-up BMP: bitmapData row 0 is the last file row. */
        check(t.raster[0] == 56, "bottom-up rows are flipped (row 0 = file row 7)");
        check(t.raster[63] == 7, "bottom-up rows are flipped (last row = file row 0)");
        check(pal_at(&t, 3) == 0xFF030303u,
              "palette entries keep the BMP BGR order and get alpha");
    }
    free(blob);
    free(bmp);

    /* Top-down (negative height) keeps the file order. */
    bmp = bmp_info40(3, 7, 1, 0, 0, &len);
    blob = blob_from_bmp(bmp, len, 1, &blen);
    check(blob != NULL, "top-down INFO40 decodes");
    if (blob) {
        check(nj_mascot_texture_view(blob, blen, &t) == NJ_MR_OK
              && t.width_bit == 2 && t.height_bit == 3,
              "3x7 pads to 4x8");
        check(t.raster[0] == 0 && t.raster[1] == 1 && t.raster[2] == 2,
              "top-down rows keep the file order");
        check(t.raster[4] == 3 && t.raster[8] == 6,
              "the 4-byte row padding is dropped");
        check(t.raster[4 + 3] == 0 && t.raster[8 + 3] == 0,
              "padding beyond the source width is zero");
    }
    free(blob);
    free(bmp);

    /* CORE header (12-byte DIB) and the broken raster-offset clamp. */
    bmp = bmp_core(4, 4, &len);
    blob = blob_from_bmp(bmp, len, 1, &blen);
    check(blob != NULL, "CORE 12-byte DIB decodes");
    if (blob) {
        check(nj_mascot_texture_view(blob, blen, &t) == NJ_MR_OK
              && t.width_bit == 2 && t.height_bit == 2,
              "CORE 4x4 pads to 4x4");
        check(t.raster[0] == 12, "CORE rows are bottom-up");
        check(pal_at(&t, 7) == 0xFF0307F8u,
              "CORE palette is BGR with the 0xFF alpha");
    }
    free(blob);
    free(bmp);

    bmp = bmp_info40(4, 4, 0, 0, 1, &len);
    blob = blob_from_bmp(bmp, len, 1, &blen);
    check(blob != NULL,
          "a raster offset pointing into the palette is clamped, not read");
    if (blob) {
        check(nj_mascot_texture_view(blob, blen, &t) == NJ_MR_OK
              && t.raster[0] == 12,
              "the clamped decode reads the real raster data");
    }
    free(blob);
    free(bmp);
}

static void test_blob_env(void)
{
    nj_s4 len = 0;
    nj_u1 *bmp = bmp_info40(10, 6, 1, 0, 0, &len);
    nj_s4 blen = 0;
    nj_u1 *blob = blob_from_bmp(bmp, len, 0, &blen);
    nj_mascot_texture t;

    check(blob != NULL, "an environment map decodes");
    if (blob) {
        check(nj_mascot_texture_view(blob, blen, &t) == NJ_MR_OK,
              "the environment blob views");
        check(t.width_bit == 6 && t.height_bit == 6,
              "the environment map is always 64x64");
        check((t.flags & NJ_MR_TEX_ENVMAP) != 0, "the env flag is set");
        check(t.len_mask == NJ_MR_ENV_SIZE - 1, "the env sample mask is 4095");
        {
            const nj_u4 *e = (const nj_u4 *)(const void *)t.raster;
            check(e[0] == 0, "env index 0 is black (index 0 is not sampled)");
            check((e[1] & 0x00FFFFFFu) == 0x00010101u,
                  "a non-zero env index carries the palette colour");
            check((e[4] & 0x00FFFFFFu) == 0x00040404u,
                  "env row 0 keeps the top-down source order");
        }
    }
    free(blob);
    free(bmp);

    /* An environment map larger than 64x64 is rejected. */
    bmp = bmp_info40(65, 65, 0, 0, 0, &len);
    {
        nj_s4 sz = 0;
        check(nj_mascot_texture_query(bmp, len, 0, &sz) != NJ_MR_OK,
              "an environment map wider than 64 is rejected");
    }
    free(bmp);

    bmp = bmp_info40(4, 4, 0, 0, 0, &len);
    blob = blob_from_bmp(bmp, len, 1, &blen);
    if (blob) {
        nj_u1 *crafted = (nj_u1 *)calloc(1, (size_t)blen);
        memcpy(crafted, blob, (size_t)blen);
        crafted[8] = 16;
        crafted[9] = 16;
        put_u16(crafted + 12, 0);
        put_u16(crafted + 14, 0);
        put_u32(crafted + 20, 0);
        check(nj_mascot_texture_view(crafted, blen, &t) != NJ_MR_OK,
              "a crafted wb=hb=16 blob with an empty raster is rejected");
        crafted[8] = 13;
        crafted[9] = 13;
        put_u16(crafted + 12, 8192);
        put_u16(crafted + 14, 8192);
        check(nj_mascot_texture_view(crafted, blen, &t) != NJ_MR_OK,
              "a crafted 8192x8192 blob is rejected");
        check(nj_mascot_texture_view(blob, 64, &t) != NJ_MR_OK,
              "a truncated blob is rejected");
        crafted[0] = 'X';
        check(nj_mascot_texture_view(crafted, blen, &t) != NJ_MR_OK,
              "a blob with the wrong magic is rejected");
        free(crafted);
    }
    free(blob);
    free(bmp);

    check(nj_mascot_texture_query(NULL, 0, 1, NULL) < 0,
          "query with no BMP is an argument error");
    check(nj_mascot_texture_view(NULL, 0, NULL) == NJ_MR_ERR_ARG,
          "view with no blob is an argument error");
}

/* The padded side is stored in a u16, so width_bit 16 (65536) cannot round
 * trip; 32768 (bit 15) is the largest representable padded side. */
static void test_blob_limits(void)
{
    nj_s4 len = 0;
    nj_s4 sz = 0;
    nj_u1 *bmp = bmp_info40(32768, 1, 1, 0, 0, &len);
    nj_u1 *blob;
    nj_mascot_texture t;

    check(nj_mascot_texture_query(bmp, len, 1, &sz) == NJ_MR_OK
          && sz == (nj_s4)(1048 + 32768),
          "a 32768-wide model texture is accepted (largest padded side)");
    blob = blob_from_bmp(bmp, len, 1, &sz);
    check(blob != NULL && nj_mascot_texture_view(blob, sz, &t) == NJ_MR_OK
          && t.width_bit == 15 && t.height_bit == 0 && t.len_mask == 32767,
          "the 32768-wide blob round-trips through view()");
    free(blob);
    free(bmp);

    bmp = bmp_info40(32769, 1, 1, 0, 0, &len);
    sz = 0;
    check(nj_mascot_texture_query(bmp, len, 1, &sz) == NJ_MR_ERR_SIZE,
          "a 32769-wide model texture is rejected (pad would be 65536)");
    free(bmp);

    bmp = bmp_info40(1, 32769, 1, 0, 0, &len);
    sz = 0;
    check(nj_mascot_texture_query(bmp, len, 1, &sz) == NJ_MR_ERR_SIZE,
          "a 32769-tall model texture is rejected (pad would be 65536)");
    free(bmp);
}

/* ------------------------------------------------------------------ */
/* Rasteriser checks                                                   */
/* ------------------------------------------------------------------ */

static nj_u4 fb[W * H];
static nj_surface surf;
static nj_u1 *model_blob;
static nj_s4 model_len;
static nj_u1 *env_blob;
static nj_s4 env_len;

static void init_surface(void)
{
    memset(&surf, 0, sizeof surf);
    surf.pixels = fb;
    surf.width = W;
    surf.height = H;
    surf.stride = W * 4;
    surf.format = NJ_PIXFMT_ARGB8888;
    surf.bpp = 4;
}

static void fb_pattern(int which)
{
    int i;
    for (i = 0; i < W * H; i++) {
        if (which == 0) {
            fb[i] = 0xFF204060u;
        } else if (which == 1) {
            fb[i] = 0xFF808080u;
        } else {
            fb[i] = 0xFF000000u | ((nj_u4)(i * 7) & 0xFFFFFFu);
        }
    }
}

static nj_u4 render(const nj_mascot_vertex v[3], nj_u4 color, nj_s4 flags,
                    nj_s4 blend, nj_u4 toon, int pattern,
                    const nj_mascot_texture *tex, const nj_mascot_texture *env)
{
    nj_mascot_material m;

    memset(&m, 0, sizeof m);
    m.color = color;
    m.flags = flags;
    m.blend = blend;
    m.toon = toon;
    if (tex) {
        m.texture = model_blob;
        m.texture_len = model_len;
    }
    if (env) {
        m.env = env_blob;
        m.env_len = env_len;
    }
    fb_pattern(pattern);
    (void)nj_mascot_raster_draw(&surf, 0, 0, W, H, v, &m);
    return fnv1a(fb, W * H);
}

static void tri_full(nj_mascot_vertex v[3], nj_s4 light, nj_s4 u, nj_s4 eu)
{
    int i;
    for (i = 0; i < 3; i++) {
        memset(&v[i], 0, sizeof v[i]);
        v[i].light = light;
        v[i].u = u;
        v[i].v = u;
        v[i].eu = eu;
        v[i].ev = eu;
    }
    v[0].x = 2;
    v[0].y = 2;
    v[1].x = 30;
    v[1].y = 4;
    v[2].x = 6;
    v[2].y = 22;
}

static void test_flat_colour(void)
{
    nj_mascot_vertex v[3];
    nj_mascot_material m;
    nj_u4 h;

    /* Unlit flat colour at full light: the reference uses 31<<12 and never
     * consults the light data. */
    tri_full(v, 4095, 0, 0);
    h = render(v, 0x0000FF00u, 0, NJ_MR_BLEND_REPLACE, 0, 0, NULL, NULL);
    check(fb[10 * W + 10] == 0xFF00FF00u,
          "an unlit flat colour draws at full brightness");
    check(h != 0, "the unlit flat frame hashes");

    /* A scene-level toon word must not affect an unlit polygon (reference:
     * `toon = lighting && efxToon`). */
    render(v, 0x0000FF00u, 0, NJ_MR_BLEND_REPLACE,
           NJ_MR_TOON_PACK(1, 8, 0, 0), 0, NULL, NULL);
    check(fb[10 * W + 10] == 0xFF00FF00u,
          "an unlit flat colour ignores a scene toon word");

    /* Flat lit: shade = ((2048>>2)*128)>>12 = 16, channel*(shade+1)>>5. */
    tri_full(v, 2048, 0, 0);
    render(v, 0x0000FF00u, NJ_MR_SHADED, NJ_MR_BLEND_REPLACE, 0, 0, NULL, NULL);
    check(fb[10 * W + 10] == 0xFF008700u,
          "flat lit green uses the reference shade formula");
    tri_full(v, 2048, 0, 0);
    render(v, 0x0000FF00u, NJ_MR_SHADED, NJ_MR_BLEND_REPLACE, 0, 0, NULL, NULL);
    check(fb[10 * W + 10] == 0xFF008700u,
          "flat lit green at light 2048 matches the formula");

    /* Smooth lit with equal lights: level 16, channel*level>>5. */
    render(v, 0x0000FF00u, NJ_MR_LIGHTING, NJ_MR_BLEND_REPLACE, 0, 0, NULL, NULL);
    check(fb[10 * W + 10] == 0xFF007F00u,
          "smooth lit green with equal lights uses channel*level>>5");

    /* Toon: light 4095 picks the high colour, light 1000 with threshold 100
     * picks the low colour. */
    tri_full(v, 4095, 0, 0);
    render(v, 0x0000FF00u, NJ_MR_LIGHTING, NJ_MR_BLEND_REPLACE,
           NJ_MR_TOON_PACK(1, 100, 200, 50), 0, NULL, NULL);
    check(fb[10 * W + 10] == 0xFF00CF00u, "toon picks the high colour above the threshold");
    tri_full(v, 1000, 0, 0);
    render(v, 0x0000FF00u, NJ_MR_LIGHTING, NJ_MR_BLEND_REPLACE,
           NJ_MR_TOON_PACK(1, 100, 200, 50), 0, NULL, NULL);
    check(fb[10 * W + 10] == 0xFF003700u, "toon picks the low colour below the threshold");

    /* The blend arithmetic on a known destination pixel. */
    tri_full(v, 4095, 0, 0);
    render(v, 0x0000FF00u, 0, NJ_MR_BLEND_HALF, 0, 0, NULL, NULL);
    check(fb[10 * W + 10] == 0xFF109F30u, "HALF blends with the reference average");
    render(v, 0x0000FF00u, 0, NJ_MR_BLEND_ADD, 0, 0, NULL, NULL);
    check(fb[10 * W + 10] == 0xFF20FF60u, "ADD saturates per channel");
    render(v, 0x0000FF00u, 0, NJ_MR_BLEND_SUB, 0, 0, NULL, NULL);
    check(fb[10 * W + 10] == 0xFF200060u, "SUB saturates per channel");

    /* Argument handling. */
    memset(&m, 0, sizeof m);
    m.flags = NJ_MR_TEXTURED;
    fb_pattern(0);
    check(nj_mascot_raster_draw(&surf, 0, 0, W, H, v, &m) == NJ_MR_ERR_ARG,
          "a textured material without a texture is rejected");
    check(nj_mascot_raster_draw(NULL, 0, 0, W, H, v, &m) == NJ_MR_ERR_ARG,
          "a NULL surface is rejected");
    m.flags = 0;
    check(nj_mascot_raster_draw(&surf, 8, 8, 8, 8, v, &m) == NJ_MR_OK,
          "an empty clip rectangle is a no-op, not an error");
    /* pixel/line are void and must simply do nothing on a NULL surface. */
    nj_mascot_raster_pixel(NULL, 0, 0, 0xFFFFFFFFu, NJ_MR_BLEND_REPLACE);
    fb_pattern(0);
    nj_mascot_raster_pixel(&surf, 3, 3, 0xFF112233u, NJ_MR_BLEND_REPLACE);
    check(fb[3 * W + 3] == 0xFF112233u, "pixel writes the exact colour");
    nj_mascot_raster_line(&surf, 0, 0, W, H, 0, 0, 8, 8, 0xFF445566u,
                          NJ_MR_BLEND_REPLACE);
    check(fb[0] == 0xFF445566u && fb[8 * W + 8] == 0xFF445566u,
          "line hits both endpoints");
    nj_mascot_raster_line(NULL, 0, 0, W, H, 0, 0, 8, 8, 0, NJ_MR_BLEND_ADD);
}

static void test_textured(void)
{
    nj_mascot_texture tex, env, key;
    nj_mascot_vertex v[3];
    nj_u1 *key_blob;
    nj_s4 key_len = 0;
    nj_u1 *key_bmp;
    nj_s4 key_bmp_len = 0;
    nj_mascot_material km;
    int i;

    check(nj_mascot_texture_view(model_blob, model_len, &tex) == NJ_MR_OK,
          "the model texture views");
    check(nj_mascot_texture_view(env_blob, env_len, &env) == NJ_MR_OK,
          "the environment texture views");

    /* A top-down texture so that padded texel (0,0) is index 0, the key. */
    key_bmp = bmp_info40(8, 8, 1, 0, 0, &key_bmp_len);
    key_blob = blob_from_bmp(key_bmp, key_bmp_len, 1, &key_len);
    free(key_bmp);
    check(key_blob && nj_mascot_texture_view(key_blob, key_len, &key) == NJ_MR_OK
          && key.raster[0] == 0,
          "the colour-key texture puts index 0 at texel (0,0)");

    /* Unshaded texture with the colour key: texel 0 is skipped. */
    for (i = 0; i < 3; i++) {
        memset(&v[i], 0, sizeof v[i]);
        v[i].light = 4095;
    }
    v[0].x = 0;  v[0].y = 0;  v[0].u = 0;      v[0].v = 0;
    v[1].x = 24; v[1].y = 0;  v[1].u = 8 << 8; v[1].v = 0;
    v[2].x = 0;  v[2].y = 20; v[2].u = 0;      v[2].v = 8 << 8;
    memset(&km, 0, sizeof km);
    km.flags = NJ_MR_TEXTURED | NJ_MR_COLORKEY;
    km.texture = key_blob;
    km.texture_len = key_len;
    fb_pattern(0);
    (void)nj_mascot_raster_draw(&surf, 0, 0, W, H, v, &km);
    check(fb[1 * W + 1] == 0xFF204060u,
          "the colour key leaves the background where texel 0 lands");
    check(fb[10 * W + 3] != 0xFF204060u,
          "a non-key texel draws the palette colour");

    /* The same triangle with the key disabled draws texel 0. */
    km.flags = NJ_MR_TEXTURED;
    fb_pattern(0);
    (void)nj_mascot_raster_draw(&surf, 0, 0, W, H, v, &km);
    check(fb[1 * W + 1] == 0xFF000000u,
          "without the colour key texel 0 draws");

    /* A texture blob is not an environment map and vice versa. */
    check(nj_mascot_raster_draw(&surf, 0, 0, W, H, v,
                                &(nj_mascot_material){
                                    .flags = NJ_MR_TEXTURED,
                                    .texture = env_blob,
                                    .texture_len = env_len }) == NJ_MR_ERR_ARG,
          "an env blob cannot be used as a model texture");
    check(nj_mascot_raster_draw(&surf, 0, 0, W, H, v,
                                &(nj_mascot_material){
                                    .flags = NJ_MR_ENVMAP,
                                    .env = model_blob,
                                    .env_len = model_len }) == NJ_MR_ERR_ARG,
          "a model blob cannot be used as an environment map");

    free(key_blob);

    /* Extreme geometry and negative attributes must not fault (UBSan/ASan
     * build runs this too). */
    for (i = 0; i < 3; i++) {
        v[i].x = -32000 + i;
        v[i].y = 32000 - i;
        v[i].u = -4096;
        v[i].v = -(1 << 20);
        v[i].light = -1;
        v[i].eu = -(1 << 20);
        v[i].ev = 1 << 20;
    }
    check(nj_mascot_raster_draw(&surf, 0, 0, W, H, v,
                                &(nj_mascot_material){
                                    .flags = NJ_MR_TEXTURED | NJ_MR_LIGHTING
                                             | NJ_MR_ENVMAP | NJ_MR_SHADED,
                                    .texture = model_blob,
                                    .texture_len = model_len,
                                    .env = env_blob,
                                    .env_len = env_len,
                                    .blend = NJ_MR_BLEND_SUB,
                                    .toon = NJ_MR_TOON_PACK(1, 8, 255, 0) })
          == NJ_MR_OK,
          "off-screen vertices with negative attributes survive");
}

/* ------------------------------------------------------------------ */
/* Golden framebuffers                                                 */
/* ------------------------------------------------------------------ */

static void check_hash(nj_u4 got, nj_u4 want, const char *what)
{
    if (got != want) {
        printf("  (hash %s: got 0x%08Xu want 0x%08Xu)\n", what, got, want);
    }
    check(got == want, what);
}

static void test_golden(void)
{
    nj_mascot_texture tex, env;
    nj_mascot_vertex v[3];
    nj_u4 h;
    int i;

    (void)nj_mascot_texture_view(model_blob, model_len, &tex);
    (void)nj_mascot_texture_view(env_blob, env_len, &env);

    /* G1: textured, colour key, replace, unshaded. */
    for (i = 0; i < 3; i++) {
        memset(&v[i], 0, sizeof v[i]);
        v[i].light = 4095;
    }
    v[0].x = 1;  v[0].y = 1;  v[0].u = 0;       v[0].v = 0;
    v[1].x = 29; v[1].y = 3;  v[1].u = 8 << 8;  v[1].v = 0;
    v[2].x = 5;  v[2].y = 21; v[2].u = 0;       v[2].v = 8 << 8;
    h = render(v, 0, NJ_MR_TEXTURED | NJ_MR_COLORKEY, NJ_MR_BLEND_REPLACE,
               0, 1, &tex, NULL);
    check_hash(h, 0x0971C068u, "golden G1: textured colour-key replace");

    /* G2: textured, smooth shade, HALF. */
    v[0].light = 4095;
    v[1].light = 2048;
    v[2].light = 0;
    h = render(v, 0, NJ_MR_TEXTURED | NJ_MR_SHADED | NJ_MR_LIGHTING,
               NJ_MR_BLEND_HALF, 0, 1, &tex, NULL);
    check_hash(h, 0x497689AFu, "golden G2: textured smooth shade HALF");

    /* G3: textured flat shade + environment, replace. */
    for (i = 0; i < 3; i++) {
        v[i].light = 3072;
        v[i].eu = 100 + i * 200;
        v[i].ev = 400 + i * 150;
    }
    h = render(v, 0, NJ_MR_TEXTURED | NJ_MR_SHADED | NJ_MR_ENVMAP,
               NJ_MR_BLEND_REPLACE, 0, 1, &tex, &env);
    check_hash(h, 0x4846A492u, "golden G3: textured flat shade + env replace");

    /* G4: textured smooth shade + environment, ADD. */
    v[0].light = 4095;
    v[1].light = 1024;
    v[2].light = 300;
    h = render(v, 0, NJ_MR_TEXTURED | NJ_MR_SHADED | NJ_MR_LIGHTING | NJ_MR_ENVMAP,
               NJ_MR_BLEND_ADD, 0, 1, &tex, &env);
    check_hash(h, 0x1A990FA9u, "golden G4: textured smooth + env ADD");

    /* G5: smooth colour + environment, SUB. */
    for (i = 0; i < 3; i++) {
        v[i].light = i == 0 ? 4095 : (i == 1 ? 1024 : 0);
        v[i].eu = 50 + i * 300;
        v[i].ev = 900 - i * 250;
    }
    h = render(v, 0x00C04080u, NJ_MR_LIGHTING | NJ_MR_ENVMAP,
               NJ_MR_BLEND_SUB, 0, 1, NULL, &env);
    check_hash(h, 0x3704F3F3u, "golden G5: smooth colour + env SUB");

    /* G6: flat colour with toon, replace. */
    for (i = 0; i < 3; i++) {
        v[i].light = 3000;
    }
    h = render(v, 0x0040C080u, NJ_MR_SHADED, NJ_MR_BLEND_REPLACE,
               NJ_MR_TOON_PACK(1, 50, 200, 40), 1, NULL, NULL);
    check_hash(h, 0xA7F9688Du, "golden G6: flat toon colour replace");

    /* G7: textured with a clip rectangle smaller than the surface. */
    for (i = 0; i < 3; i++) {
        v[i].light = 4095;
    }
    {
        nj_mascot_material m;
        memset(&m, 0, sizeof m);
        m.flags = NJ_MR_TEXTURED;
        m.texture = model_blob;
        m.texture_len = model_len;
        fb_pattern(1);
        (void)nj_mascot_raster_draw(&surf, 4, 4, 20, 18, v, &m);
        h = fnv1a(fb, W * H);
    }
    check_hash(h, 0x4D0C2EE5u, "golden G7: textured clipped to a sub-rectangle");
}

int main(void)
{
    nj_s4 len = 0;
    nj_u1 *bmp;

    init_surface();

    bmp = bmp_info40(8, 8, 0, 0, 0, &len);
    model_blob = blob_from_bmp(bmp, len, 1, &model_len);
    free(bmp);
    bmp = bmp_info40(16, 8, 0, 0, 0, &len);
    env_blob = blob_from_bmp(bmp, len, 0, &env_len);
    free(bmp);

    check(model_blob != NULL && env_blob != NULL,
          "the test textures decode");

    printf("test_mascot_raster: blob\n");
    test_blob_info40();
    test_blob_env();
    test_blob_limits();
    printf("test_mascot_raster: raster\n");
    test_flat_colour();
    test_textured();
    test_golden();

    free(model_blob);
    free(env_blob);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
