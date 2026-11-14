/* Real GLES2 conformance test. Run Mesa headlessly with
 * EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1. Software GL is valid here;
 * renderer identity is logged, and no hardware/performance claim is made.
 * CHECK deliberately stays active in Release/NDEBUG builds. */
#include "nj2me/nj_gfx_gles2.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !NJ_ENABLE_GFX_ACCEL
#error "test_gfx_gles2 requires NJ_ENABLE_GFX_ACCEL=1 in every linked graphics object"
#endif

static unsigned checks, comparisons;
static unsigned long long accepted_fills, accepted_blits, declined;
static char scenario[160] = "initialization";
#define CHECK(c) do { ++checks; if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d [%s]: %s\n", __FILE__, __LINE__, scenario, #c); \
    exit(1); } } while (0)

enum { GUARD = 17, STORAGE = 8192 };
typedef struct canvas {
    nj_u1 bytes[STORAGE];
    nj_surface surface;
    nj_gfx gfx;
} canvas;
typedef struct current_gate {
    EGLContext context;
    int enabled;
    unsigned calls;
} current_gate;
typedef struct egl_fixture {
    EGLDisplay display;
    EGLContext context[2];
    EGLSurface surface[2];
} egl_fixture;

static int is_current(void *ud)
{
    current_gate *gate = (current_gate *)ud;
    ++gate->calls;
    return gate->enabled && eglGetCurrentContext() == gate->context;
}
static void gl_ok(void)
{
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) fprintf(stderr, "GL error 0x%x [%s]\n", error, scenario);
    CHECK(error == GL_NO_ERROR);
}
static int egl_unavailable(const char *stage)
{
    fprintf(stderr, "SKIP GLES2: EGL context initialization unavailable at %s (EGL error 0x%x). "
                    "For Mesa try EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1.\n",
            stage, (unsigned)eglGetError());
    return 0;
}
static void egl_cleanup(egl_fixture *e)
{
    int i;
    if (e->display == EGL_NO_DISPLAY) return;
    eglMakeCurrent(e->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    for (i = 0; i < 2; ++i) {
        if (e->context[i] != EGL_NO_CONTEXT) eglDestroyContext(e->display, e->context[i]);
        if (e->surface[i] != EGL_NO_SURFACE) eglDestroySurface(e->display, e->surface[i]);
    }
    eglTerminate(e->display);
}
static int egl_init(egl_fixture *e)
{
    static const EGLint config_attributes[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE
    };
    static const EGLint surface_attributes[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
    static const EGLint context_attributes[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLConfig config;
    EGLint count, major, minor;
    int i;
    e->display = EGL_NO_DISPLAY;
    for (i = 0; i < 2; ++i) {
        e->context[i] = EGL_NO_CONTEXT;
        e->surface[i] = EGL_NO_SURFACE;
    }
    e->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (e->display == EGL_NO_DISPLAY) return egl_unavailable("eglGetDisplay");
    if (!eglInitialize(e->display, &major, &minor)) return egl_unavailable("eglInitialize");
    if (!eglBindAPI(EGL_OPENGL_ES_API)) return egl_unavailable("eglBindAPI");
    if (!eglChooseConfig(e->display, config_attributes, &config, 1, &count) || count != 1)
        return egl_unavailable("eglChooseConfig");
    for (i = 0; i < 2; ++i) {
        e->surface[i] = eglCreatePbufferSurface(e->display, config, surface_attributes);
        if (e->surface[i] == EGL_NO_SURFACE) return egl_unavailable("eglCreatePbufferSurface");
        e->context[i] = eglCreateContext(e->display, config, EGL_NO_CONTEXT, context_attributes);
        if (e->context[i] == EGL_NO_CONTEXT) return egl_unavailable("eglCreateContext");
        if (!eglMakeCurrent(e->display, e->surface[i], e->surface[i], e->context[i]))
            return egl_unavailable("eglMakeCurrent");
    }
    printf("EGL %d.%d; GL vendor=%s; renderer=%s; version=%s\n", major, minor,
           (const char *)glGetString(GL_VENDOR), (const char *)glGetString(GL_RENDERER),
           (const char *)glGetString(GL_VERSION));
    gl_ok();
    return 1;
}
static void make_current(const egl_fixture *e, int index)
{
    CHECK(eglMakeCurrent(e->display, e->surface[index], e->surface[index], e->context[index]));
    CHECK(eglGetCurrentContext() == e->context[index]);
    gl_ok();
}
static int bytes_per_pixel(int format)
{
    return format == NJ_PIXFMT_RGB565 ? 2 : format == NJ_PIXFMT_RGB888 ? 3 : 4;
}
/* Independent byte packing keeps initial surfaces and source images independent
 * of the implementation's converters. Deliberately unaligned pixel addresses. */
static void store(nj_u1 *p, int format, nj_u4 color)
{
    if (format == NJ_PIXFMT_RGB565) {
        nj_u2 v = (nj_u2)(((color >> 8) & 0xf800u) |
                         ((color >> 5) & 0x07e0u) | ((color >> 3) & 0x001fu));
        memcpy(p, &v, sizeof v);
    } else if (format == NJ_PIXFMT_RGB888) {
        p[0] = (nj_u1)(color >> 16); p[1] = (nj_u1)(color >> 8); p[2] = (nj_u1)color;
    } else memcpy(p, &color, sizeof color);
}
static void init_canvas(canvas *c, int format, int w, int h, int padding, int mixed_alpha)
{
    int x, y, bpp = bytes_per_pixel(format);
    memset(c, 0, sizeof *c);
    memset(c->bytes, 0xa5, sizeof c->bytes);
    c->surface.pixels = c->bytes + GUARD;
    c->surface.width = w; c->surface.height = h;
    c->surface.stride = w*bpp + padding; c->surface.format = format;
    CHECK(GUARD + h*c->surface.stride + GUARD <= STORAGE);
    nj_gfx_init(&c->gfx, &c->surface);
    for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) {
        unsigned alpha = mixed_alpha ? (unsigned)((x+3*y)%4)*85u : 255u;
        nj_u4 rgb = (0x153971u + (nj_u4)x*0x172309u + (nj_u4)y*0x390b17u) & 0xffffffu;
        store(c->bytes+GUARD+y*c->surface.stride+x*bpp, format, (alpha << 24) | rgb);
    }
}
static void guards(const canvas *c)
{
    int i, y;
    for (i = 0; i < GUARD; ++i) CHECK(c->bytes[i] == 0xa5);
    for (y = 0; y < c->surface.height; ++y)
        for (i = c->surface.width*c->surface.bpp; i < c->surface.stride; ++i)
            CHECK(c->bytes[GUARD+y*c->surface.stride+i] == 0xa5);
    for (i = GUARD+c->surface.height*c->surface.stride; i < STORAGE; ++i)
        CHECK(c->bytes[i] == 0xa5);
}
static void equal(const canvas *cpu, const canvas *gpu)
{
    if (memcmp(cpu->bytes, gpu->bytes, STORAGE) != 0) {
        int i;
        for (i = 0; i < STORAGE && cpu->bytes[i] == gpu->bytes[i]; ++i) {}
        fprintf(stderr, "First mismatch byte %d: CPU=%02x GLES2=%02x [%s]\n",
                i, cpu->bytes[i], gpu->bytes[i], scenario);
        CHECK(0);
    }
    guards(cpu); guards(gpu); ++comparisons;
}
static nj_gles2_stats stats(nj_gles2 *state)
{
    nj_gles2_stats result;
    memset(&result, 0, sizeof result);
    nj_gles2_get_stats(state, &result);
    return result;
}
static void delta(nj_gles2 *state, nj_gles2_stats before, unsigned fills,
                  unsigned blits, unsigned declines)
{
    nj_gles2_stats after = stats(state);
    CHECK(after.fills == before.fills + fills);
    CHECK(after.blits == before.blits + blits);
    CHECK(after.declines == before.declines + declines);
    accepted_fills += fills; accepted_blits += blits; declined += declines;
}
static void bind(canvas *c, nj_gles2 *state)
{
    nj_gfx_set_accel(&c->gfx, nj_gles2_backend(), state);
}
static void setup_clip(nj_gfx *g, int tw, int th, int partial)
{
    int inset_x = partial && tw > 1, inset_y = partial && th > 1;
    nj_gfx_translate(g, 2, -1);
    /* Image origin in user space (1,3), device space (3,2). */
    nj_gfx_set_clip(g, 1+inset_x, 3+inset_y,
                    tw-inset_x-(partial && tw > 2), th-inset_y-(partial && th > 2));
}
static void test_fills(nj_gles2 *state)
{
    static const nj_u4 colors[] = { 0xff176ac3u, 0x7319b052u, 0x00112233u };
    static const int sizes[][2] = { {1,1}, {7,5}, {19,13} };
    int fmt, ci, blend, xor_on, size;
    for (fmt = NJ_PIXFMT_RGB565; fmt <= NJ_PIXFMT_ARGB8888; ++fmt)
    for (ci = 0; ci < 3; ++ci) for (blend = 0; blend < 2; ++blend)
    for (xor_on = 0; xor_on < 2; ++xor_on) for (size = 0; size < 3; ++size) {
        canvas cpu, gpu;
        int accepted = !xor_on && (blend == NJ_BLEND_NONE || ci == 0);
        nj_gles2_stats before = stats(state);
        snprintf(scenario, sizeof scenario, "fill fmt=%d color=%d blend=%d xor=%d size=%d",
                 fmt, ci, blend, xor_on, size);
        init_canvas(&cpu, fmt, 31, 23, 7, 1); init_canvas(&gpu, fmt, 31, 23, 7, 1);
        bind(&gpu, state);
        setup_clip(&cpu.gfx, sizes[size][0], sizes[size][1], size != 0);
        setup_clip(&gpu.gfx, sizes[size][0], sizes[size][1], size != 0);
        cpu.gfx.color = gpu.gfx.color = colors[ci];
        cpu.gfx.blend = gpu.gfx.blend = blend;
        if (xor_on) {
            nj_gfx_set_xor(&cpu.gfx, 0x005aa53cu); nj_gfx_set_xor(&gpu.gfx, 0x005aa53cu);
        }
        nj_gfx_fill_rect(&cpu.gfx, 1, 3, sizes[size][0], sizes[size][1]);
        nj_gfx_fill_rect(&gpu.gfx, 1, 3, sizes[size][0], sizes[size][1]);
        delta(state, before, (unsigned)accepted, 0, (unsigned)!accepted);
        equal(&cpu, &gpu); gl_ok();
    }
}
static void test_images(nj_gles2 *state)
{
    static const int sizes[][2] = { {1,1}, {4,3}, {9,6} };
    int sf, df, t, size, rop, partial;
    for (sf = NJ_PIXFMT_RGB565; sf <= NJ_PIXFMT_ARGB8888; ++sf)
    for (df = NJ_PIXFMT_RGB565; df <= NJ_PIXFMT_ARGB8888; ++df)
    for (t = 0; t < 8; ++t) for (size = 0; size < 3; ++size)
    for (rop = NJ_ROP_SET; rop <= NJ_ROP_OPAQUE; ++rop) for (partial = 0; partial < 2; ++partial) {
        canvas src, cpu, gpu;
        nj_u1 source_before[STORAGE];
        int w = sizes[size][0], h = sizes[size][1];
        nj_image image;
        nj_gles2_stats before = stats(state);
        snprintf(scenario, sizeof scenario, "image sf=%d df=%d t=%d size=%d rop=%d partial=%d",
                 sf, df, t, size, rop, partial);
        init_canvas(&src, sf, w+3, h+2, 0, rop != NJ_ROP_PAINT);
        memcpy(source_before, src.bytes, STORAGE);
        image.width = src.surface.width; image.height = src.surface.height; image.format = sf;
        image.pixels = (nj_u4 *)src.surface.pixels; image.has_alpha = rop != NJ_ROP_PAINT;
        init_canvas(&cpu, df, 31, 23, 11, 1); init_canvas(&gpu, df, 31, 23, 11, 1);
        setup_clip(&cpu.gfx, t >= 4 ? h : w, t >= 4 ? w : h, partial);
        setup_clip(&gpu.gfx, t >= 4 ? h : w, t >= 4 ? w : h, partial);
        bind(&gpu, state);
        nj_gfx_draw_image_rop(&cpu.gfx, &image, 1, 3, 1, 1, w, h, rop, t);
        nj_gfx_draw_image_rop(&gpu.gfx, &image, 1, 3, 1, 1, w, h, rop, t);
        delta(state, before, 0, 1, 0);
        equal(&cpu, &gpu); CHECK(memcmp(src.bytes, source_before, STORAGE) == 0);
        guards(&src); gl_ok();
    }
}
static nj_gfx_blit descriptor(const canvas *src, int w, int h, int t, int rop)
{
    nj_gfx_blit b;
    memset(&b, 0, sizeof b);
    b.pixels = (const nj_u1 *)src->surface.pixels;
    b.stride = src->surface.stride; b.format = src->surface.format;
    b.sx = b.sy = 1; b.width = w; b.height = h;
    b.transform = t; b.rop = rop; b.alpha = 255;
    b.dx = 1; b.dy = 3; b.clip_x = 3; b.clip_y = 2;
    b.clip_w = t >= 4 ? h : w; b.clip_h = t >= 4 ? w : h;
    return b;
}
static nj_u4 load(const nj_u1 *p, int format)
{
    if (format == NJ_PIXFMT_RGB565) {
        nj_u2 v;
        nj_u4 r, g, b;
        memcpy(&v, p, sizeof v);
        r = (v >> 11) & 31u; g = (v >> 5) & 63u; b = v & 31u;
        return 0xff000000u | (((r << 3) | (r >> 2)) << 16) |
               (((g << 2) | (g >> 4)) << 8) | (b << 3) | (b >> 2);
    }
    if (format == NJ_PIXFMT_RGB888)
        return 0xff000000u | ((nj_u4)p[0] << 16) | ((nj_u4)p[1] << 8) | p[2];
    {
        nj_u4 v; memcpy(&v, p, sizeof v); return v;
    }
}
/* Forward source mapping deliberately shares neither the rasterizer's inverse
 * mapping nor the GLES texture coordinates. Verify SET's byte-exact copies,
 * including zero global/source alpha and arbitrary unaligned byte strides. */
static void independent_set(canvas *dst, const canvas *src, const nj_gfx_blit *b,
                            int translate_x, int translate_y)
{
    int u, v;
    for (v = 0; v < b->height; ++v) for (u = 0; u < b->width; ++u) {
        int x = 0, y = 0;
        const nj_u1 *p;
        switch (b->transform) {
        case NJ_TRANS_NONE: x = u; y = v; break;
        case NJ_TRANS_MIRROR_ROT180: x = u; y = b->height-1-v; break;
        case NJ_TRANS_MIRROR: x = b->width-1-u; y = v; break;
        case NJ_TRANS_ROT180: x = b->width-1-u; y = b->height-1-v; break;
        case NJ_TRANS_MIRROR_ROT270: x = v; y = u; break;
        case NJ_TRANS_ROT90: x = b->height-1-v; y = u; break;
        case NJ_TRANS_ROT270: x = v; y = b->width-1-u; break;
        case NJ_TRANS_MIRROR_ROT90: x = b->height-1-v; y = b->width-1-u; break;
        default: CHECK(0);
        }
        x += b->dx+translate_x; y += b->dy+translate_y;
        if (x < b->clip_x || x >= b->clip_x+b->clip_w ||
            y < b->clip_y || y >= b->clip_y+b->clip_h) continue;
        p = (const nj_u1 *)src->surface.pixels + (b->sy+v)*src->surface.stride +
            (b->sx+u)*bytes_per_pixel(src->surface.format);
        store((nj_u1 *)dst->surface.pixels + y*dst->surface.stride + x*dst->surface.bpp,
              dst->surface.format, load(p, src->surface.format));
    }
}
static void test_padded_sources(nj_gles2 *state)
{
    static const int sizes[][2] = { {1,1}, {4,3}, {9,6} };
    int sf, df, t, size;
    for (sf = NJ_PIXFMT_RGB565; sf <= NJ_PIXFMT_ARGB8888; ++sf)
    for (df = NJ_PIXFMT_RGB565; df <= NJ_PIXFMT_ARGB8888; ++df)
    for (t = 0; t < 8; ++t) for (size = 0; size < 3; ++size) {
        canvas src, cpu, gpu, independent;
        nj_u1 source_before[STORAGE];
        nj_gfx_blit b;
        int w = sizes[size][0], h = sizes[size][1];
        nj_gles2_stats before = stats(state);
        snprintf(scenario, sizeof scenario, "padded direct sf=%d df=%d t=%d size=%d", sf, df, t, size);
        init_canvas(&src, sf, w+3, h+2, 9, 1); memcpy(source_before, src.bytes, STORAGE);
        init_canvas(&cpu, df, 31, 23, 7, 1); init_canvas(&gpu, df, 31, 23, 7, 1);
        init_canvas(&independent, df, 31, 23, 7, 1);
        setup_clip(&cpu.gfx, t >= 4 ? h : w, t >= 4 ? w : h, 1);
        setup_clip(&gpu.gfx, t >= 4 ? h : w, t >= 4 ? w : h, 1);
        b = descriptor(&src, w, h, t, NJ_ROP_SET);
        b.clip_x = cpu.gfx.clip_x; b.clip_y = cpu.gfx.clip_y;
        b.clip_w = cpu.gfx.clip_w; b.clip_h = cpu.gfx.clip_h;
        /* SET ignores global alpha, including on ARGB sources. */
        b.alpha = size == 0 ? 0 : size == 1 ? 79 : 255;
        nj_gfx_blit_software(&cpu.gfx, &b);
        independent_set(&independent, &src, &b, 2, -1);
        CHECK(nj_gles2_backend()->blit(state, &gpu.gfx, &b) == NJ_GFX_ACCEL_DONE);
        delta(state, before, 0, 1, 0); equal(&cpu, &gpu); equal(&independent, &gpu);
        CHECK(memcmp(source_before, src.bytes, STORAGE) == 0); guards(&src); gl_ok();
    }
}
static void test_rgb(nj_gles2 *state)
{
    static const int alphas[] = { -7, 0, 1, 127, 254, 255, 300 };
    int df, t, ai, rop;
    for (df = NJ_PIXFMT_RGB565; df <= NJ_PIXFMT_ARGB8888; ++df)
    for (t = 0; t < 8; ++t) for (ai = 0; ai < 7; ++ai)
    for (rop = NJ_ROP_SET; rop <= NJ_ROP_OPAQUE; ++rop) {
        canvas src, cpu, gpu;
        nj_u1 source_before[STORAGE];
        int accepted = rop == NJ_ROP_SET || alphas[ai] >= 255;
        nj_gles2_stats before = stats(state);
        snprintf(scenario, sizeof scenario, "drawRGB df=%d t=%d alpha=%d rop=%d", df, t, alphas[ai], rop);
        init_canvas(&src, NJ_PIXFMT_RGB888, 7, 5, 7, 0); memcpy(source_before, src.bytes, STORAGE);
        init_canvas(&cpu, df, 31, 23, 9, 1); init_canvas(&gpu, df, 31, 23, 9, 1);
        setup_clip(&cpu.gfx, t >= 4 ? 3 : 4, t >= 4 ? 4 : 3, 1);
        setup_clip(&gpu.gfx, t >= 4 ? 3 : 4, t >= 4 ? 4 : 3, 1); bind(&gpu, state);
        nj_gfx_draw_rgb(&cpu.gfx, (const nj_u1 *)src.surface.pixels, src.surface.stride,
                        alphas[ai], 1, 3, 1, 1, 4, 3, rop, t);
        nj_gfx_draw_rgb(&gpu.gfx, (const nj_u1 *)src.surface.pixels, src.surface.stride,
                        alphas[ai], 1, 3, 1, 1, 4, 3, rop, t);
        delta(state, before, 0, (unsigned)accepted, (unsigned)!accepted);
        equal(&cpu, &gpu); CHECK(memcmp(source_before, src.bytes, STORAGE) == 0); gl_ok();
    }
}
/* Calling the backend directly distinguishes a clean decline from a fallback
 * that happens to overwrite an incorrect partial GPU result. */
static void expect_decline(nj_gles2 *state, canvas *target, const nj_gfx_blit *b)
{
    nj_u1 before[STORAGE];
    nj_gles2_stats previous = stats(state);
    memcpy(before, target->bytes, STORAGE);
    CHECK(nj_gles2_backend()->blit(state, &target->gfx, b) == NJ_GFX_ACCEL_DECLINE);
    CHECK(memcmp(before, target->bytes, STORAGE) == 0);
    delta(state, previous, 0, 0, 1); guards(target);
}
static void expect_fill_decline(nj_gles2 *state, canvas *target, int w, int h)
{
    nj_u1 before[STORAGE];
    nj_gles2_stats previous = stats(state);
    memcpy(before, target->bytes, STORAGE);
    CHECK(nj_gles2_backend()->fill_rect(state, &target->gfx, 0, 0, w, h) == NJ_GFX_ACCEL_DECLINE);
    CHECK(memcmp(before, target->bytes, STORAGE) == 0);
    delta(state, previous, 0, 0, 1); guards(target);
}
static void test_direct_declines(nj_gles2 *state)
{
    canvas src, target;
    nj_gfx_blit b, base;
    nj_u4 mask[12] = { 0 };
    nj_u1 source_before[STORAGE];
    int choice;
    init_canvas(&src, NJ_PIXFMT_ARGB8888, 7, 5, 9, 1); memcpy(source_before, src.bytes, STORAGE);
    init_canvas(&target, NJ_PIXFMT_ARGB8888, 31, 23, 7, 1);
    setup_clip(&target.gfx, 4, 3, 0); base = descriptor(&src, 4, 3, 0, NJ_ROP_SET);
    for (choice = 0; choice < 11; ++choice) {
        snprintf(scenario, sizeof scenario, "direct unsupported no-write choice=%d", choice);
        b = base;
        switch (choice) {
        case 0: b.rop = NJ_ROP_PAINT; break; /* mixed source alpha */
        case 1: b.rop = NJ_ROP_OPAQUE; b.alpha = 127; break;
        case 2: b.rop = NJ_ROP_PAINT; b.alpha = 0; break;
        case 3: b.xor_enabled = 1; b.xor_color = 0x00124567u; break;
        case 4: b.mask = (const nj_u1 *)mask; b.mask_width = 4; b.mask_height = 3; break;
        case 5: b.transform = -1; break;
        case 6: b.transform = 8; break;
        case 7: b.rop = 99; break;
        case 8: b.pixels = (const nj_u1 *)target.surface.pixels; b.stride = target.surface.stride; break;
        case 9: b.pixels = (const nj_u1 *)target.surface.pixels+4; b.stride = target.surface.stride; break;
        default: b.format = -1; break;
        }
        expect_decline(state, &target, &b); gl_ok();
        CHECK(memcmp(src.bytes, source_before, STORAGE) == 0);
    }
    snprintf(scenario, sizeof scenario, "direct unsupported fill alpha/XOR no-write");
    target.gfx.color = 0x70112233u; target.gfx.blend = NJ_BLEND_SRC_OVER;
    expect_fill_decline(state, &target, 4, 3);
    target.gfx.color = 0xff112233u; nj_gfx_set_xor(&target.gfx, 0x00123456u);
    expect_fill_decline(state, &target, 4, 3); gl_ok();
}
static void test_fallback_matrix(nj_gles2 *state)
{
    int sf, df, rop, mixed, xor_on, masked;
    nj_u4 mask_pixels[12] = {0, 0xffffffffu, 0x01000000u, 0xff000000u,
                            0x7f000000u, 0, 0xff123456u, 0, 0, 0xffffffffu, 0, 0xffabcdefu};
    nj_image mask = {3, 4, NJ_PIXFMT_ARGB8888, mask_pixels, 1};
    for (sf = NJ_PIXFMT_RGB565; sf <= NJ_PIXFMT_ARGB8888; ++sf)
    for (df = NJ_PIXFMT_RGB565; df <= NJ_PIXFMT_ARGB8888; ++df)
    for (rop = NJ_ROP_SET; rop <= NJ_ROP_OPAQUE; ++rop)
    for (mixed = 0; mixed < 2; ++mixed) for (xor_on = 0; xor_on < 2; ++xor_on)
    for (masked = 0; masked < 2; ++masked) {
        canvas src, cpu, gpu;
        nj_u1 source_before[STORAGE];
        nj_image image;
        int accepted = !xor_on && !masked && (rop != NJ_ROP_PAINT || !mixed || sf != NJ_PIXFMT_ARGB8888);
        nj_gles2_stats before = stats(state);
        snprintf(scenario, sizeof scenario, "fallback sf=%d df=%d rop=%d mixed=%d xor=%d mask=%d",
                 sf, df, rop, mixed, xor_on, masked);
        init_canvas(&src, sf, 7, 5, 0, mixed); memcpy(source_before, src.bytes, STORAGE);
        image.width = 7; image.height = 5; image.format = sf;
        image.pixels = (nj_u4 *)src.surface.pixels; image.has_alpha = mixed;
        init_canvas(&cpu, df, 31, 23, 9, 1); init_canvas(&gpu, df, 31, 23, 9, 1);
        setup_clip(&cpu.gfx, 3, 4, 1); setup_clip(&gpu.gfx, 3, 4, 1); bind(&gpu, state);
        nj_gfx_draw_region(&cpu.gfx, &image, 1, 1, 4, 3, NJ_TRANS_ROT90,
                           xor_on, 0x0056a139u, masked ? &mask : NULL, 1, 3, rop);
        nj_gfx_draw_region(&gpu.gfx, &image, 1, 1, 4, 3, NJ_TRANS_ROT90,
                           xor_on, 0x0056a139u, masked ? &mask : NULL, 1, 3, rop);
        delta(state, before, 0, (unsigned)accepted, (unsigned)!accepted);
        equal(&cpu, &gpu); CHECK(memcmp(src.bytes, source_before, STORAGE) == 0); gl_ok();
    }
}
static void test_coherence(nj_gles2 *state)
{
    canvas src, cpu, gpu;
    nj_image image;
    nj_gles2_stats before;
    snprintf(scenario, sizeof scenario, "interleaved CPU/GLES2/fallback coherence");
    init_canvas(&src, NJ_PIXFMT_ARGB8888, 7, 5, 0, 0);
    image.width = 7; image.height = 5; image.format = NJ_PIXFMT_ARGB8888;
    image.pixels = (nj_u4 *)src.surface.pixels; image.has_alpha = 1;
    init_canvas(&cpu, NJ_PIXFMT_ARGB8888, 31, 23, 9, 1);
    init_canvas(&gpu, NJ_PIXFMT_ARGB8888, 31, 23, 9, 1); bind(&gpu, state);
    before = stats(state);
    nj_gfx_draw_image(&cpu.gfx, &image, 2, 2, 1, 1, 4, 3);
    nj_gfx_draw_image(&gpu.gfx, &image, 2, 2, 1, 1, 4, 3);
    delta(state, before, 0, 1, 0); equal(&cpu, &gpu);
    /* Reuse both pointers after CPU writes; readback must already be visible. */
    nj_surface_set(&cpu.surface, 2, 2, 0xff765432u);
    nj_surface_set(&gpu.surface, 2, 2, 0xff765432u);
    store(src.bytes+GUARD+src.surface.stride+4, NJ_PIXFMT_ARGB8888, 0x7fe2c481u);
    before = stats(state);
    nj_gfx_draw_image(&cpu.gfx, &image, 2, 2, 1, 1, 4, 3);
    nj_gfx_draw_image(&gpu.gfx, &image, 2, 2, 1, 1, 4, 3);
    delta(state, before, 0, 0, 1); equal(&cpu, &gpu);
    CHECK(nj_surface_get(&gpu.surface, 2, 2) != 0xff765432u);
    nj_gfx_set_color(&cpu.gfx, 0xff123abcu); nj_gfx_set_color(&gpu.gfx, cpu.gfx.color);
    nj_gfx_line(&cpu.gfx, 0, 3, 17, 3); nj_gfx_line(&gpu.gfx, 0, 3, 17, 3);
    equal(&cpu, &gpu);
    before = stats(state);
    nj_gfx_draw_image_rop(&cpu.gfx, &image, 3, 2, 1, 1, 4, 3, NJ_ROP_SET, NJ_TRANS_MIRROR);
    nj_gfx_draw_image_rop(&gpu.gfx, &image, 3, 2, 1, 1, 4, 3, NJ_ROP_SET, NJ_TRANS_MIRROR);
    delta(state, before, 0, 1, 0); equal(&cpu, &gpu);
    CHECK(nj_surface_get(&gpu.surface, 6, 2) == 0x7fe2c481u);
    nj_gfx_set_xor(&cpu.gfx, 0x0053a798u); nj_gfx_set_xor(&gpu.gfx, 0x0053a798u);
    before = stats(state);
    nj_gfx_draw_image(&cpu.gfx, &image, 2, 2, 1, 1, 4, 3);
    nj_gfx_draw_image(&gpu.gfx, &image, 2, 2, 1, 1, 4, 3);
    delta(state, before, 0, 0, 1); equal(&cpu, &gpu);
    nj_gfx_clear_xor(&cpu.gfx); nj_gfx_clear_xor(&gpu.gfx);
    before = stats(state);
    nj_gfx_fill_rect(&cpu.gfx, 1, 1, 3, 2); nj_gfx_fill_rect(&gpu.gfx, 1, 1, 3, 2);
    delta(state, before, 1, 0, 0); equal(&cpu, &gpu); gl_ok();
}
static void test_budget(current_gate *gate)
{
    nj_gles2_config config = { .is_current=is_current, .ud=gate, .max_pixels=6 };
    nj_gles2 *limited = nj_gles2_create(&config);
    canvas src, cpu, target;
    nj_gfx_blit b;
    nj_gles2_stats before;
    snprintf(scenario, sizeof scenario, "explicit source/visible destination budget");
    CHECK(limited != NULL);
    init_canvas(&src, NJ_PIXFMT_ARGB8888, 7, 5, 7, 0);
    init_canvas(&cpu, NJ_PIXFMT_ARGB8888, 31, 23, 9, 0);
    init_canvas(&target, NJ_PIXFMT_ARGB8888, 31, 23, 9, 0);
    cpu.gfx.color = target.gfx.color = 0xff136ac9u;
    before = stats(limited);
    nj_gfx_fill_rect(&cpu.gfx, 0, 0, 3, 2);
    CHECK(nj_gles2_backend()->fill_rect(limited, &target.gfx, 0, 0, 3, 2) == NJ_GFX_ACCEL_DONE);
    delta(limited, before, 1, 0, 0); equal(&cpu, &target);
    expect_fill_decline(limited, &target, 7, 1);
    setup_clip(&cpu.gfx, 3, 2, 0); setup_clip(&target.gfx, 3, 2, 0);
    b = descriptor(&src, 3, 2, 0, NJ_ROP_SET);
    before = stats(limited); nj_gfx_blit_software(&cpu.gfx, &b);
    CHECK(nj_gles2_backend()->blit(limited, &target.gfx, &b) == NJ_GFX_ACCEL_DONE);
    delta(limited, before, 0, 1, 0); equal(&cpu, &target);
    /* Source exceeds six pixels even though only one target pixel is visible. */
    b.width = 4; b.height = 3; b.clip_w = b.clip_h = 1;
    expect_decline(limited, &target, &b);
    /* Public dispatch must still produce the complete software result. */
    bind(&target, limited); before = stats(limited);
    nj_gfx_fill_rect(&cpu.gfx, 1, 3, 3, 2); nj_gfx_fill_rect(&target.gfx, 1, 3, 3, 2);
    delta(limited, before, 1, 0, 0); equal(&cpu, &target);
    before = stats(limited);
    {
        canvas packed;
        nj_image image;
        init_canvas(&packed, NJ_PIXFMT_ARGB8888, 7, 5, 0, 0);
        image.width = 7; image.height = 5; image.format = NJ_PIXFMT_ARGB8888;
        image.pixels = (nj_u4 *)packed.surface.pixels; image.has_alpha = 0;
        /* Both sides use the full source region and the same six-pixel clip. */
        nj_gfx_draw_image_rop(&cpu.gfx, &image, 1, 3, 1, 1, 4, 3, NJ_ROP_SET, 0);
        nj_gfx_draw_image_rop(&target.gfx, &image, 1, 3, 1, 1, 4, 3, NJ_ROP_SET, 0);
    }
    delta(limited, before, 0, 0, 1); equal(&cpu, &target);
    nj_gfx_set_clip(&cpu.gfx, -2, 1, 31, 23); nj_gfx_set_clip(&target.gfx, -2, 1, 31, 23);
    before = stats(limited);
    nj_gfx_fill_rect(&cpu.gfx, 1, 3, 4, 2); nj_gfx_fill_rect(&target.gfx, 1, 3, 4, 2);
    delta(limited, before, 0, 0, 1); equal(&cpu, &target);
    CHECK(nj_gles2_destroy(limited) == 1); gl_ok();
}
static void test_default_budget(nj_gles2 *state)
{
    const size_t count = (size_t)1025*1024, length = count*4;
    nj_u1 *pixels = (nj_u1 *)malloc(length), *before = (nj_u1 *)malloc(length);
    nj_surface surface;
    nj_gfx g;
    canvas small;
    nj_gfx_blit b;
    nj_gles2_stats previous;
    snprintf(scenario, sizeof scenario, "default 1M source and visible destination budgets");
    CHECK(pixels != NULL && before != NULL);
    memset(pixels, 0x63, length); memcpy(before, pixels, length);
    memset(&surface, 0, sizeof surface);
    surface.pixels = pixels; surface.width = 1025; surface.height = 1024;
    surface.stride = 1025*4; surface.format = NJ_PIXFMT_ARGB8888;
    nj_gfx_init(&g, &surface); g.color = 0xff123456u;
    previous = stats(state);
    CHECK(nj_gles2_backend()->fill_rect(state, &g, 0, 0, 1025, 1024) == NJ_GFX_ACCEL_DECLINE);
    CHECK(memcmp(pixels, before, length) == 0); delta(state, previous, 0, 0, 1);
    init_canvas(&small, NJ_PIXFMT_ARGB8888, 31, 23, 9, 0);
    memset(&b, 0, sizeof b);
    b.pixels = pixels; b.stride = 1025*4; b.format = NJ_PIXFMT_ARGB8888;
    b.width = 1025; b.height = 1024; b.rop = NJ_ROP_SET; b.alpha = 255;
    b.clip_w = b.clip_h = 1;
    expect_decline(state, &small, &b); CHECK(memcmp(pixels, before, length) == 0);
    free(before); free(pixels); gl_ok();
}
static void test_context_declines(const egl_fixture *e, current_gate *gate, nj_gles2 *state)
{
    canvas src, cpu, gpu;
    nj_gfx_blit b;
    nj_gles2_stats before;
    unsigned callback_count;
    snprintf(scenario, sizeof scenario, "unavailable context direct decline and software fallback");
    init_canvas(&src, NJ_PIXFMT_ARGB8888, 7, 5, 7, 0);
    init_canvas(&cpu, NJ_PIXFMT_ARGB8888, 31, 23, 9, 0);
    init_canvas(&gpu, NJ_PIXFMT_ARGB8888, 31, 23, 9, 0);
    setup_clip(&cpu.gfx, 4, 3, 0); setup_clip(&gpu.gfx, 4, 3, 0);
    b = descriptor(&src, 4, 3, 0, NJ_ROP_SET);
    gate->enabled = 0; callback_count = gate->calls;
    expect_decline(state, &gpu, &b); expect_fill_decline(state, &gpu, 3, 2);
    CHECK(gate->calls >= callback_count+2); gl_ok();
    bind(&gpu, state); before = stats(state);
    nj_gfx_fill_rect(&cpu.gfx, 1, 3, 4, 3); nj_gfx_fill_rect(&gpu.gfx, 1, 3, 4, 3);
    delta(state, before, 0, 0, 1); equal(&cpu, &gpu);
    before = stats(state); nj_gfx_blit_software(&cpu.gfx, &b);
    CHECK(nj_gles2_backend()->blit(state, &gpu.gfx, &b) == NJ_GFX_ACCEL_DECLINE);
    /* Execute the same public software fallback for this rejected descriptor. */
    nj_gfx_blit_software(&gpu.gfx, &b); delta(state, before, 0, 0, 1); equal(&cpu, &gpu);
    CHECK(nj_gles2_destroy(state) == 0); gate->enabled = 1;
    CHECK(eglMakeCurrent(e->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    expect_decline(state, &gpu, &b); expect_fill_decline(state, &gpu, 3, 2);
    CHECK(nj_gles2_destroy(state) == 0);
    make_current(e, 1); /* A real current but wrong GLES2 context. */
    expect_decline(state, &gpu, &b); expect_fill_decline(state, &gpu, 3, 2);
    CHECK(nj_gles2_destroy(state) == 0); gl_ok();
    make_current(e, 0);
    before = stats(state); gpu.gfx.color = cpu.gfx.color = 0xff43a915u;
    nj_gfx_fill_rect(&cpu.gfx, 1, 3, 4, 3); nj_gfx_fill_rect(&gpu.gfx, 1, 3, 4, 3);
    delta(state, before, 1, 0, 0); equal(&cpu, &gpu); gl_ok();
}
static void test_instances(const egl_fixture *e, nj_gles2 *first, nj_gles2 *second)
{
    canvas cpu, gpu;
    nj_gles2_stats first_before = stats(first), second_before = stats(second);
    snprintf(scenario, sizeof scenario, "two contexts have independent backend statistics");
    CHECK(second_before.fills == 0 && second_before.blits == 0 && second_before.declines == 0);
    make_current(e, 1);
    init_canvas(&cpu, NJ_PIXFMT_RGB888, 31, 23, 7, 0);
    init_canvas(&gpu, NJ_PIXFMT_RGB888, 31, 23, 7, 0); bind(&gpu, second);
    cpu.gfx.color = gpu.gfx.color = 0xff17a2c9u;
    nj_gfx_fill_rect(&cpu.gfx, 3, 2, 7, 5); nj_gfx_fill_rect(&gpu.gfx, 3, 2, 7, 5);
    delta(second, second_before, 1, 0, 0); equal(&cpu, &gpu);
    delta(first, first_before, 0, 0, 0); gl_ok();
    second_before = stats(second); make_current(e, 0);
    CHECK(nj_gles2_destroy(second) == 0);
    delta(second, second_before, 0, 0, 0);
}
int main(void)
{
    egl_fixture e;
    current_gate gate[2];
    nj_gles2_config config;
    nj_gles2 *state[2];
    int i;
    if (!egl_init(&e)) { egl_cleanup(&e); return 77; }
    CHECK(nj_gles2_backend() != NULL);
    CHECK(nj_gles2_backend()->version == NJ_GFX_ACCEL_VERSION);
    CHECK(nj_gles2_backend()->fill_rect != NULL && nj_gles2_backend()->blit != NULL);
    CHECK(nj_gles2_destroy(NULL) == 1); nj_gles2_abandon(NULL);
    CHECK(nj_gles2_create(NULL) == NULL);
    memset(&config, 0, sizeof config); CHECK(nj_gles2_create(&config) == NULL);
    for (i = 0; i < 2; ++i) {
        make_current(&e, i);
        gate[i].context = e.context[i]; gate[i].enabled = 0; gate[i].calls = 0;
        config.is_current = is_current; config.ud = &gate[i]; config.max_pixels = 0;
        CHECK(nj_gles2_create(&config) == NULL);
        gate[i].enabled = 1;
        state[i] = nj_gles2_create(&config);
        /* Once EGL works, backend allocation/shader/GL failures are failures. */
        CHECK(state[i] != NULL); CHECK(gate[i].calls >= 2); gl_ok();
    }
    make_current(&e, 0);
    test_fills(state[0]); test_images(state[0]); test_padded_sources(state[0]);
    test_rgb(state[0]); test_direct_declines(state[0]); test_fallback_matrix(state[0]);
    test_coherence(state[0]); test_budget(&gate[0]); test_default_budget(state[0]);
    test_context_declines(&e, &gate[0], state[0]); test_instances(&e, state[0], state[1]);
    snprintf(scenario, sizeof scenario, "current-context destruction and actual-loss abandon");
    /* destroy retained first through all wrong-context attempts. */
    CHECK(nj_gles2_destroy(state[0]) == 1); gl_ok();
    CHECK(eglMakeCurrent(e.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    CHECK(eglDestroyContext(e.display, e.context[1])); e.context[1] = EGL_NO_CONTEXT;
    /* CPU-only abandonment is legal only after EGL reclaimed this context. */
    nj_gles2_abandon(state[1]);
    egl_cleanup(&e);
    CHECK(accepted_fills > 0 && accepted_blits > 0 && declined > 0);
    printf("PASS real GLES2: %u comparisons, %u checks; completed fill=%llu blit=%llu; "
           "declines=%llu; formats/transforms/alpha/ROP/XOR/mask/guards/coherence/budgets/lifecycle\n",
           comparisons, checks, accepted_fills, accepted_blits, declined);
    return 0;
}
