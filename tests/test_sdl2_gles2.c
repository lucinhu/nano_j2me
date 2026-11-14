/* Actual SDL2 frontend integration. Run with SDL_VIDEODRIVER=offscreen
 * SDL_AUDIODRIVER=dummy; Mesa may need EGL_PLATFORM=surfaceless and
 * LIBGL_ALWAYS_SOFTWARE=1. No game/JAR/fixtures required. The independent SDL
 * GLES preflight alone may skip (77); a port that silently falls back after
 * that preflight is a failure. CHECK remains active in Release builds. */
#include "nj2me/nj.h"
#include "nj2me/nj_gfx_triangles.h"
#include "nj_internal.h"
#include "../src/midp/nj_midp.h"
#include <SDL.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if NJ_ENABLE_GFX_ACCEL != 1
#error "test_sdl2_gles2 requires NJ_ENABLE_GFX_ACCEL=1"
#endif
#if !defined(NJ_SDL2_GLES2) || NJ_SDL2_GLES2 != 1
#error "test_sdl2_gles2 requires NJ_SDL2_GLES2=1"
#endif

static unsigned checks, accepted_fills, accepted_blits, accepted_rasters, declined;
#define CHECK(c) do { ++checks; if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s (SDL: %s)\n", __FILE__, __LINE__, #c, SDL_GetError()); \
    exit(1); } } while (0)
#define D_IMAGE "Ljavax/microedition/lcdui/Image;"
#define D_GFX "Ljavax/microedition/lcdui/Graphics;"

typedef struct binding {
    SDL_Window *window;
    SDL_GLContext context;
} binding;
typedef struct guard {
    binding current;
    GLuint texture;
} guard;
typedef struct forwarder {
    const nj_gfx_accel *backend;
    void *ud;
    unsigned fills, blits, rasters, declines;
} forwarder;
typedef struct frontend {
    nj_port owner, copied;
    nj_vm *vm;
    nj_surface screen;
    nj_gfx early;
    forwarder probe;
} frontend;

static binding current(void)
{
    binding b;
    b.window = SDL_GL_GetCurrentWindow();
    b.context = SDL_GL_GetCurrentContext();
    return b;
}
static void same_current(binding b)
{
    CHECK(SDL_GL_GetCurrentWindow() == b.window);
    CHECK(SDL_GL_GetCurrentContext() == b.context);
}
static void make_current(binding b)
{
    CHECK(SDL_GL_MakeCurrent(b.window, b.context) == 0);
    same_current(b);
}
static void gl_ok(void)
{
    CHECK(glGetError() == GL_NO_ERROR);
}
static int skip(const char *where, guard *g)
{
    fprintf(stderr, "SKIP SDL2 GLES2: video/GLES context unavailable at %s: %s\n",
            where, SDL_GetError());
    if (g->current.context) SDL_GL_DeleteContext(g->current.context);
    if (g->current.window) SDL_DestroyWindow(g->current.window);
    SDL_Quit();
    return 77;
}
static int preflight(guard *g)
{
    static const nj_u4 pixel = 0xff123456u;
    memset(g, 0, sizeof(*g));
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return skip("SDL_Init video", g);
    if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES) != 0 ||
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2) != 0 ||
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0) != 0 ||
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0) != 0)
        return skip("GLES attributes", g);
    g->current.window = SDL_CreateWindow("SDL GLES2 test sentinel", 0, 0, 16, 16,
                                        SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
    if (!g->current.window) return skip("SDL_CreateWindow", g);
    g->current.context = SDL_GL_CreateContext(g->current.window);
    if (!g->current.context ||
        SDL_GL_MakeCurrent(g->current.window, g->current.context) != 0)
        return skip("SDL_GL_CreateContext/MakeCurrent", g);
    if (!glGetString(GL_VERSION)) return skip("glGetString", g);
    fprintf(stdout, "SDL video=%s; GL vendor=%s; renderer=%s; version=%s\n",
            SDL_GetCurrentVideoDriver(), (const char *)glGetString(GL_VENDOR),
            (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
    glGenTextures(1, &g->texture);
    CHECK(g->texture != 0);
    glBindTexture(GL_TEXTURE_2D, g->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &pixel);
    glViewport(2, 3, 5, 7);
    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
    gl_ok();
    return 0;
}
static void guard_ok(const guard *g)
{
    GLint viewport[4], texture;
    GLfloat clear[4];
    same_current(g->current);
    CHECK(glIsTexture(g->texture) == GL_TRUE);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    CHECK((GLuint)texture == g->texture);
    glGetIntegerv(GL_VIEWPORT, viewport);
    CHECK(viewport[0] == 2 && viewport[1] == 3 && viewport[2] == 5 && viewport[3] == 7);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
    CHECK(clear[0] == 0.25f && clear[1] == 0.5f && clear[2] == 0.75f && clear[3] == 1.0f);
    gl_ok();
}
static int forward_fill(void *ud, const nj_gfx *g, int x, int y, int w, int h)
{
    forwarder *p = (forwarder *)ud;
    binding b = current();
    int rc = p->backend->fill_rect(p->ud, g, x, y, w, h);
    same_current(b);
    CHECK(rc == NJ_GFX_ACCEL_DONE || rc == NJ_GFX_ACCEL_DECLINE);
    if (rc == NJ_GFX_ACCEL_DONE) { ++p->fills; ++accepted_fills; }
    else { ++p->declines; ++declined; }
    return rc;
}
static int forward_blit(void *ud, const nj_gfx *g, const nj_gfx_blit *image)
{
    forwarder *p = (forwarder *)ud;
    binding b = current();
    int rc = p->backend->blit(p->ud, g, image);
    same_current(b);
    CHECK(rc == NJ_GFX_ACCEL_DONE || rc == NJ_GFX_ACCEL_DECLINE);
    if (rc == NJ_GFX_ACCEL_DONE) { ++p->blits; ++accepted_blits; }
    else { ++p->declines; ++declined; }
    return rc;
}
static const nj_gfx_accel counting = {NJ_GFX_ACCEL_VERSION, forward_fill, forward_blit};

static void construct(frontend *f)
{
    nj_u4 pixels[4] = {0x11223344u, 0x55667788u, 0x99aabbccu, 0xddeeff00u};
    nj_u4 before[4], drawn = 0x13579bdfu;
    nj_s4 keys[4] = {1, 0, 255, 256}, depth[4] = {0, 0, 255, 257};
    nj_gfx_raster raster;
    nj_surface s;
    memset(f, 0, sizeof(*f));
    CHECK(nj_port_sdl2(&f->owner, "SDL GLES2 integration") == NJ_OK);
    f->owner.width = 24; f->owner.height = 20; f->owner.scale = 1;
    CHECK(f->owner.gfx_accel != NULL && f->owner.gfx_accel_ud != NULL);
    CHECK(f->owner.gfx_accel->version == NJ_GFX_ACCEL_VERSION);
    CHECK(f->owner.gfx_accel->fill_rect != NULL && f->owner.gfx_accel->blit != NULL);
    CHECK(f->owner.gfx_raster != NULL && f->owner.gfx_raster_ud != NULL);
    CHECK(f->owner.gfx_triangles != NULL && f->owner.gfx_triangles_ud != NULL);
    /* nj_run copies the port before VM/display initialization. Existing gfx
     * bindings and the copy must become live without rewriting either one. */
    f->copied = f->owner;
    memset(&s, 0, sizeof s);
    s.pixels = pixels; s.width = s.height = 2; s.stride = 8; s.format = NJ_PIXFMT_ARGB8888;
    nj_gfx_init(&f->early, &s);
    nj_gfx_set_accel(&f->early, f->copied.gfx_accel, f->copied.gfx_accel_ud);
    memcpy(before, pixels, sizeof before);
    CHECK(f->copied.gfx_accel->fill_rect(f->copied.gfx_accel_ud, &f->early, 0, 0, 2, 2)
          == NJ_GFX_ACCEL_DECLINE);
    CHECK(memcmp(before, pixels, sizeof before) == 0);
    memset(&raster, 0, sizeof raster);
    raster.version = NJ_GFX_RASTER_VERSION; raster.surface = &s;
    raster.width = raster.height = raster.key_stride = raster.depth_stride = 2;
    raster.keys = keys; raster.depth = depth;
    raster.depth_test = raster.depth_write = 1; raster.color = 0xff136ab9u;
    CHECK(f->copied.gfx_raster(f->copied.gfx_raster_ud, &raster, &drawn) == NJ_GFX_ACCEL_DECLINE);
    CHECK(drawn == 0x13579bdfu && memcmp(before, pixels, sizeof before) == 0);
    CHECK(depth[0] == 0 && depth[1] == 0 && depth[2] == 255 && depth[3] == 257);
    ++declined;
    {
        nj_gfx_triangles packet;
        memset(&packet, 0, sizeof packet);
        CHECK(f->copied.gfx_triangles(f->copied.gfx_triangles_ud, &packet, &drawn)
              == NJ_GFX_ACCEL_DECLINE);
        CHECK(drawn == 0x13579bdfu && memcmp(before, pixels, sizeof before) == 0);
    }
    /* early.surf is replaced at display startup; its callback binding survives. */
}
static void initialize(frontend *f)
{
    const nj_gfx_accel *table = f->copied.gfx_accel;
    nj_gfx_raster_fn raster = f->copied.gfx_raster;
    void *raster_ud = f->copied.gfx_raster_ud;
    void *ud = f->copied.gfx_accel_ud, *pixels = NULL;
    int w = 0, h = 0, stride = 0, format;
    f->vm = nj_vm_create(&f->copied, 4u*1024u*1024u);
    CHECK(f->vm != NULL && nj_vm_port(f->vm) == &f->copied);
    format = f->copied.display_init(f->copied.ud, &w, &h, &stride, &pixels);
    CHECK(format >= NJ_PIXFMT_RGB565 && format <= NJ_PIXFMT_ARGB8888);
    CHECK(w == 24 && h == 20 && pixels != NULL && stride > 0);
    CHECK(f->copied.gfx_accel == table && f->copied.gfx_accel_ud == ud);
    CHECK(f->owner.gfx_accel == table && f->owner.gfx_accel_ud == ud);
    CHECK(f->copied.gfx_raster == raster && f->copied.gfx_raster_ud == raster_ud);
    CHECK(f->owner.gfx_raster == raster && f->owner.gfx_raster_ud == raster_ud);
    f->screen.pixels = pixels; f->screen.width = w; f->screen.height = h;
    f->screen.stride = stride; f->screen.format = format;
    f->screen.bpp = format == NJ_PIXFMT_RGB565 ? 2 : format == NJ_PIXFMT_RGB888 ? 3 : 4;
    f->early.surf = &f->screen;
    f->early.clip_x = f->early.clip_y = 0;
    f->early.clip_w = w; f->early.clip_h = h;
    f->probe.backend = table; f->probe.ud = ud;
    f->copied.gfx_accel = &counting; f->copied.gfx_accel_ud = &f->probe;
    /* VM rendering must use the copied port, never a late write to owner. */
    f->owner.gfx_accel = NULL; f->owner.gfx_accel_ud = NULL;
    f->owner.gfx_raster = NULL; f->owner.gfx_raster_ud = NULL;
}
static void compare(const void *a, const void *b, size_t n)
{
    static unsigned comparison;
    const nj_u1 *expected = (const nj_u1 *)a, *actual = (const nj_u1 *)b;
    size_t i;
    ++comparison;
    for (i = 0; i < n; ++i) if (expected[i] != actual[i]) {
        fprintf(stderr, "Comparison %u byte %zu expected=%02x actual=%02x\n",
                comparison, i, expected[i], actual[i]);
        CHECK(0);
    }
}
static void direct_render(frontend *f, const guard *external)
{
    nj_u1 expected[24*20*4], snapshot[24*20*4];
    nj_u4 source[6] = {0xff102030u, 0xff406080u, 0xff8090a0u,
                       0xffaabbccu, 0xff345678u, 0xffabcdefu};
    nj_u4 mask[6] = {0xffffffffu, 0, 0x7fffffffu, 0x40ffffffu, 0xffffffffu, 0x01ffffffu};
    nj_surface cpu_surface;
    nj_gfx cpu, *gpu = &f->early;
    nj_gfx_blit b;
    size_t bytes = (size_t)f->screen.stride*(size_t)f->screen.height;
    binding no_context;
    CHECK(bytes <= sizeof expected);
    memset(f->screen.pixels, 0x5b, bytes); memcpy(expected, f->screen.pixels, bytes);
    cpu_surface = f->screen; cpu_surface.pixels = expected;
    nj_gfx_init(&cpu, &cpu_surface);
    make_current(external->current);
    nj_gfx_set_color(gpu, 0xff395b7du); nj_gfx_set_color(&cpu, 0xff395b7du);
    CHECK(f->probe.backend->fill_rect(f->probe.ud, gpu, 2, 3, 8, 6) == NJ_GFX_ACCEL_DONE);
    ++accepted_fills;
    nj_gfx_fill_rect(&cpu, 2, 3, 8, 6);
    compare(expected, f->screen.pixels, bytes); guard_ok(external);
    memset(&b, 0, sizeof b);
    b.pixels = (const nj_u1 *)source; b.width = 2; b.height = 3; b.stride = 8;
    b.format = NJ_PIXFMT_ARGB8888;
    b.dx = b.clip_x = 4; b.dy = b.clip_y = 5;
    /* Direct descriptors carry the final image/clip intersection. */
    b.clip_w = 3; b.clip_h = 2;
    b.transform = NJ_TRANS_ROT90; b.rop = NJ_ROP_SET; b.alpha = 255;
    CHECK(f->probe.backend->blit(f->probe.ud, gpu, &b) == NJ_GFX_ACCEL_DONE);
    ++accepted_blits;
    nj_gfx_blit_software(&cpu, &b);
    compare(expected, f->screen.pixels, bytes); guard_ok(external);
    /* Unsupported callbacks decline before CPU writes; public dispatch then
     * performs the software operation once, between accepted GL operations. */
    nj_gfx_set_xor(gpu, 0x00334455u); nj_gfx_set_xor(&cpu, 0x00334455u);
    memcpy(snapshot, f->screen.pixels, bytes);
    CHECK(f->probe.backend->fill_rect(f->probe.ud, gpu, 1, 2, 7, 5) == NJ_GFX_ACCEL_DECLINE);
    ++declined; compare(snapshot, f->screen.pixels, bytes); guard_ok(external);
    nj_gfx_fill_rect(gpu, 1, 2, 7, 5); nj_gfx_fill_rect(&cpu, 1, 2, 7, 5);
    compare(expected, f->screen.pixels, bytes); guard_ok(external);
    nj_gfx_clear_xor(gpu); nj_gfx_clear_xor(&cpu);
    b.mask = (const nj_u1 *)mask; b.mask_width = 2; b.mask_height = 3; b.rop = NJ_ROP_PAINT;
    memcpy(snapshot, f->screen.pixels, bytes);
    CHECK(f->probe.backend->blit(f->probe.ud, gpu, &b) == NJ_GFX_ACCEL_DECLINE);
    ++declined; compare(snapshot, f->screen.pixels, bytes); guard_ok(external);
    nj_gfx_blit_software(gpu, &b); nj_gfx_blit_software(&cpu, &b);
    compare(expected, f->screen.pixels, bytes);
    /* A CPU primitive modifies the authoritative buffer before the next GL
     * call; successful output must include that intervening write. */
    nj_gfx_line(gpu, 0, 0, 20, 13); nj_gfx_line(&cpu, 0, 0, 20, 13);
    CHECK(SDL_GL_MakeCurrent(NULL, NULL) == 0);
    no_context = current(); CHECK(no_context.context == NULL);
    nj_gfx_fill_rect(gpu, 5, 4, 3, 3); nj_gfx_fill_rect(&cpu, 5, 4, 3, 3);
    same_current(no_context); compare(expected, f->screen.pixels, bytes);
    b.mask = NULL; b.transform = NJ_TRANS_NONE; b.rop = NJ_ROP_OPAQUE;
    b.clip_w = 2; b.clip_h = 3;
    CHECK(f->probe.backend->blit(f->probe.ud, gpu, &b) == NJ_GFX_ACCEL_DONE);
    ++accepted_blits;
    nj_gfx_blit_software(&cpu, &b);
    same_current(no_context); compare(expected, f->screen.pixels, bytes);
    make_current(external->current); guard_ok(external);
    /* SDL presentation is allowed its own renderer binding; it must leave the
     * authoritative CPU framebuffer intact. Rebind the foreign context after. */
    f->copied.display_present(f->copied.ud, 0, 0, 24, 20);
    compare(expected, f->screen.pixels, bytes);
    make_current(external->current); guard_ok(external);
}
static void raster_color(nj_u1 *p, int format, nj_u4 color)
{
    if (format == NJ_PIXFMT_RGB565) {
        nj_u2 v = (nj_u2)(((color>>8)&0xf800u)|((color>>5)&0x7e0u)|((color>>3)&31u));
        memcpy(p, &v, 2);
    } else if (format == NJ_PIXFMT_RGB888) {
        p[0] = (nj_u1)(color>>16); p[1] = (nj_u1)(color>>8); p[2] = (nj_u1)color;
    } else memcpy(p, &color, 4);
}
static void raster_render(frontend *f, const guard *external)
{
    enum { ZSTRIDE = 27, ZCOUNT = 20*ZSTRIDE+8 };
    static const nj_s4 values[6] = {0, 1, 255, 256, 16777215, 16777216};
    nj_u1 expected[2048];
    nj_s4 keys[18], depth[ZCOUNT], z_expected[ZCOUNT];
    nj_gfx_raster packet;
    nj_u4 drawn, expected_drawn;
    size_t bytes = (size_t)f->screen.stride*20u;
    int i, x, y, pass;
    CHECK(bytes <= sizeof expected);
    memcpy(expected, f->screen.pixels, bytes);
    for (i = 0; i < ZCOUNT; ++i) depth[i] = -181;
    for (y = 0; y < 20; ++y) for (x = 0; x < 24; ++x)
        depth[4+y*ZSTRIDE+x] = values[(x+3*y)%6];
    for (i = 0; i < 18; ++i) keys[i] = -193;
    for (y = 0; y < 3; ++y) for (x = 0; x < 4; ++x) keys[y*6+x] = values[(x+2*y)%6];
    memcpy(z_expected, depth, sizeof depth);
    memset(&packet, 0, sizeof packet);
    packet.version = NJ_GFX_RASTER_VERSION; packet.surface = &f->screen;
    packet.x = 4; packet.y = 6; packet.width = 4; packet.height = 3;
    packet.keys = keys; packet.key_stride = 6;
    packet.depth = depth+4; packet.depth_stride = ZSTRIDE;
    packet.depth_test = packet.depth_write = 1; packet.color = 0xff4b93d1u;
    for (pass = 0; pass < 2; ++pass) {
        binding previous;
        if (!pass) make_current(external->current);
        else CHECK(SDL_GL_MakeCurrent(NULL, NULL) == 0);
        previous = current(); expected_drawn = 0;
        /* Independent packet reference; no production merge/pixel helper. */
        for (y = 0; y < 3; ++y) for (x = 0; x < 4; ++x) {
            nj_s4 key = keys[y*6+x];
            nj_s4 *z = z_expected+4+(y+6)*ZSTRIDE+x+4;
            if (key && key >= *z) {
                raster_color(expected+(y+6)*f->screen.stride+(x+4)*f->screen.bpp,
                             f->screen.format, packet.color);
                *z = key; expected_drawn = 1;
            }
        }
        drawn = 0x123abcdeu;
        CHECK(f->copied.gfx_raster(f->copied.gfx_raster_ud, &packet, &drawn) == NJ_GFX_ACCEL_DONE);
        ++f->probe.rasters; ++accepted_rasters;
        CHECK(drawn == expected_drawn && drawn == 1); same_current(previous);
        compare(expected, f->screen.pixels, bytes);
        compare(z_expected, depth, sizeof depth);
        /* Unsupported nonopaque colour must leave all outputs and drawn intact. */
        packet.color = 0x7f3579bdu; drawn = 0x9abcdef1u;
        CHECK(f->copied.gfx_raster(f->copied.gfx_raster_ud, &packet, &drawn) == NJ_GFX_ACCEL_DECLINE);
        ++declined; CHECK(drawn == 0x9abcdef1u); same_current(previous);
        compare(expected, f->screen.pixels, bytes); compare(z_expected, depth, sizeof depth);
        packet.color = 0xffd52f71u; keys[0] = NJ_GFX_RASTER_MAX_KEY;
        /* CPU-authoritative edits between accepted callbacks are uploaded anew. */
        raster_color((nj_u1 *)f->screen.pixels+6*f->screen.stride+4*f->screen.bpp,
                     f->screen.format, 0xff175391u);
        raster_color(expected+6*f->screen.stride+4*f->screen.bpp, f->screen.format, 0xff175391u);
        depth[4+6*ZSTRIDE+4] = z_expected[4+6*ZSTRIDE+4] = 0;
    }
    make_current(external->current); guard_ok(external);
}
static nj_value integer(int n)
{
    nj_value v; v.bits = 0; v.i = n; return v;
}
static nj_value reference(nj_ref r)
{
    nj_value v; v.bits = 0; v.r = r; return v;
}
static void invoke(nj_vm *vm, nj_ref obj, const char *method, const char *desc, nj_value *a)
{
    binding b = current();
    CHECK(nj_call(vm, obj, NJ_C_GRAPHICS, method, desc, a, NULL) == NJ_OK);
    same_current(b);
}
static nj_ref image(nj_vm *vm, int source, int translucent)
{
    nj_value a[4], r;
    nj_ref array;
    int i;
    r.bits = 0;
    if (!source) {
        a[0] = a[1] = integer(8);
        CHECK(nj_call_static(vm, NJ_C_IMAGE, "createImage", "(II)" D_IMAGE, a, &r) == NJ_OK);
    } else {
        array = nj_root(vm, nj_int_array_new(vm, 6)); CHECK(array != NULL);
        for (i = 0; i < 6; ++i) nj_array_set_int(array, (nj_u4)i,
            (nj_s4)(((translucent && i == 3) ? 0x73000000u : 0xff000000u) |
                    (0x123456u + (nj_u4)i*0x11100bu)));
        a[0] = reference(array); a[1] = integer(2); a[2] = integer(3); a[3] = integer(1);
        CHECK(nj_call_static(vm, NJ_C_IMAGE, "createRGBImage", "([IIIZ)" D_IMAGE, a, &r) == NJ_OK);
    }
    CHECK(r.r != NULL); return nj_root(vm, r.r);
}
static nj_ref graphics(nj_vm *vm, nj_ref im)
{
    nj_value r; r.bits = 0;
    CHECK(nj_call(vm, im, NJ_C_IMAGE, "getGraphics", "()" D_GFX, NULL, &r) == NJ_OK);
    CHECK(r.r != NULL); return nj_root(vm, r.r);
}
static void read_image(nj_vm *vm, nj_ref im, nj_u4 pixels[64])
{
    nj_u4 mark = nj_root_mark(vm);
    nj_ref array = nj_root(vm, nj_int_array_new(vm, 64));
    nj_value a[7];
    int i;
    CHECK(array != NULL);
    a[0] = reference(array); a[1] = integer(0); a[2] = integer(8);
    a[3] = a[4] = integer(0); a[5] = a[6] = integer(8);
    CHECK(nj_call(vm, im, NJ_C_IMAGE, "getRGB", "([IIIIIII)V", a, NULL) == NJ_OK);
    for (i = 0; i < 64; ++i) pixels[i] = (nj_u4)nj_array_get_int(array, (nj_u4)i);
    nj_root_release(vm, mark);
}
static void vm_render(frontend *f, nj_vm *software, const guard *external)
{
    nj_vm *vm[2] = {f->vm, software};
    nj_ref im[2], g[2], opaque[2], alpha[2];
    nj_u4 marks[2] = {nj_root_mark(f->vm), nj_root_mark(software)};
    nj_u4 gpu[64], cpu[64];
    unsigned fills = f->probe.fills, blits = f->probe.blits, rejects = f->probe.declines;
    int side, step;
    make_current(external->current);
    for (side = 0; side < 2; ++side) {
        im[side] = image(vm[side], 0, 0); g[side] = graphics(vm[side], im[side]);
        opaque[side] = image(vm[side], 1, 0); alpha[side] = image(vm[side], 1, 1);
    }
    for (step = 0; step < 6; ++step) {
        for (side = 0; side < 2; ++side) {
            nj_value a[9];
            if (step == 0 || step == 5) {
                a[0] = integer(step == 0 ? 0x345678 : 0x7093b5);
                invoke(vm[side], g[side], "setColor", "(I)V", a);
                a[0] = a[1] = integer(step == 0 ? 1 : 3); a[2] = integer(4); a[3] = integer(3);
                invoke(vm[side], g[side], "fillRect", "(IIII)V", a);
            } else if (step == 1) {
                a[0] = reference(opaque[side]); a[1] = a[2] = integer(0);
                a[3] = integer(2); a[4] = integer(3); a[5] = integer(NJ_TRANS_ROT90);
                a[6] = integer(3); a[7] = integer(2); a[8] = integer(NJ_ANCHOR_TOP | NJ_ANCHOR_LEFT);
                invoke(vm[side], g[side], "drawRegion", "(" D_IMAGE "IIIIIIII)V", a);
            } else if (step == 2 || step == 4) {
                a[0] = reference(step == 2 ? alpha[side] : opaque[side]);
                a[1] = integer(1); a[2] = integer(4); a[3] = integer(NJ_ANCHOR_TOP | NJ_ANCHOR_LEFT);
                invoke(vm[side], g[side], "drawImage", "(" D_IMAGE "III)V", a);
            } else {
                a[0] = a[1] = integer(0); a[2] = integer(7); a[3] = integer(6);
                invoke(vm[side], g[side], "drawLine", "(IIII)V", a);
            }
        }
        read_image(f->vm, im[0], gpu); read_image(software, im[1], cpu);
        compare(cpu, gpu, sizeof gpu); guard_ok(external);
    }
    CHECK(f->probe.fills == fills+2);
    CHECK(f->probe.blits == blits+2);
    CHECK(f->probe.declines == rejects+1);
    for (side = 0; side < 2; ++side) {
        nj_root_release(vm[side], marks[side]);
    }
}
static void triangle_render(frontend *f, const guard *external)
{
    nj_gfx_triangles packet;
    nj_gfx_triangle_cmd tri;
    nj_s4 z[24*20] = {0}, saved_z[24*20];
    nj_u1 saved[24*20*4], expected_pixel[4];
    nj_u4 submitted = 99;
    size_t bytes = (size_t)f->screen.stride*f->screen.height;
    binding empty = {NULL, NULL};
    int i;
    memset(&packet, 0, sizeof packet); memset(&tri, 0, sizeof tri);
    tri.vertices[0].x = tri.vertices[0].y = -4;
    tri.vertices[1].x = 40; tri.vertices[1].y = -4;
    tri.vertices[2].x = -4; tri.vertices[2].y = 40;
    for (i = 0; i < 3; ++i) {
        tri.vertices[i].depth = 0.25; tri.vertices[i].inv_w = 1;
        tri.vertices[i].color = 0xff336699u;
    }
    tri.blend = 68; tri.alpha_write = tri.depth_test = tri.depth_write = 1;
    tri.wrap_s = tri.wrap_t = 240; tri.filter = 208; tri.texture_function = 228;
    packet.version = NJ_GFX_TRIANGLES_VERSION; packet.surface = &f->screen;
    packet.depth = z; packet.depth_stride = f->screen.width;
    packet.width = packet.height = 4; packet.triangles = &tri; packet.count = 1;
    CHECK(bytes <= sizeof saved);
    CHECK(f->copied.gfx_triangles && f->copied.gfx_triangles_ud);
    make_current(external->current);
    CHECK(f->copied.gfx_triangles(f->copied.gfx_triangles_ud, &packet, &submitted)
          == NJ_GFX_ACCEL_DONE);
    CHECK(submitted == 1 && z[1+24] == 12582912);
    raster_color(expected_pixel, f->screen.format, 0xff336699u);
    CHECK(!memcmp((nj_u1 *)f->screen.pixels+f->screen.stride+f->screen.bpp,
                  expected_pixel, (size_t)f->screen.bpp));
    guard_ok(external);
    /* A frontend's dedicated context must be switched back to NULL too. */
    make_current(empty); submitted = 99;
    CHECK(f->copied.gfx_triangles(f->copied.gfx_triangles_ud, &packet, &submitted)
          == NJ_GFX_ACCEL_DONE);
    CHECK(submitted == 1); same_current(empty);
    memcpy(saved, f->screen.pixels, bytes); memcpy(saved_z, z, sizeof z);
    packet.version = 0; submitted = 99;
    CHECK(f->copied.gfx_triangles(f->copied.gfx_triangles_ud, &packet, &submitted)
          == NJ_GFX_ACCEL_DECLINE);
    CHECK(submitted == 99 && !memcmp(saved, f->screen.pixels, bytes));
    CHECK(!memcmp(saved_z, z, sizeof z)); same_current(empty);
    make_current(external->current); guard_ok(external);
}

static void shutdown(frontend *f, const guard *external)
{
    CHECK(nj_vm_destroy(f->vm) > 0); f->vm = NULL;
    make_current(external->current);
    nj_port_shutdown(&f->owner);
    guard_ok(external);
}
static void software_mode(const guard *external, const char *mode)
{
    nj_port port;
    CHECK(SDL_setenv("NJ_SDL2_GFX", mode, 1) == 0);
    CHECK(nj_port_sdl2(&port, "SDL software selection") == NJ_OK);
    CHECK(port.gfx_accel == NULL && port.gfx_accel_ud == NULL);
    CHECK(port.gfx_raster == NULL && port.gfx_raster_ud == NULL);
    CHECK(port.gfx_triangles == NULL && port.gfx_triangles_ud == NULL);
    make_current(external->current);
    nj_port_shutdown(&port);
    guard_ok(external);
}
int main(void)
{
    guard external;
    nj_port cpu_port;
    nj_vm *cpu;
    int round, rc;
    if (!SDL_getenv("SDL_VIDEODRIVER")) CHECK(SDL_setenv("SDL_VIDEODRIVER", "offscreen", 1) == 0);
    if (!SDL_getenv("SDL_AUDIODRIVER")) CHECK(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    CHECK(SDL_setenv("NJ_SDL2_GAMEPAD", "0", 1) == 0);
    CHECK(SDL_setenv("NJ_SDL2_ACCEL", "0", 1) == 0);
    CHECK(SDL_setenv("NJ_SDL2_FULLSCREEN", "0", 1) == 0);
    CHECK(SDL_setenv("NJ_SDL2_MUTE", "1", 1) == 0);
    rc = preflight(&external); if (rc) return rc;
    memset(&cpu_port, 0, sizeof cpu_port);
    CHECK(nj_port_headless(&cpu_port) == NJ_OK);
    CHECK(cpu_port.gfx_accel == NULL);
    cpu = nj_vm_create(&cpu_port, 4u*1024u*1024u); CHECK(cpu != NULL);
    software_mode(&external, ""); software_mode(&external, "software");
    CHECK(SDL_setenv("NJ_SDL2_GFX", "gles2", 1) == 0);
    for (round = 0; round < 3; ++round) {
        frontend f[2];
        unsigned before;
        construct(&f[0]); construct(&f[1]);
        CHECK(f[0].owner.ud != f[1].owner.ud);
        CHECK(f[0].copied.gfx_accel != f[1].copied.gfx_accel);
        CHECK(f[0].copied.gfx_accel_ud != f[1].copied.gfx_accel_ud);
        CHECK(f[0].copied.gfx_raster_ud != f[1].copied.gfx_raster_ud);
        initialize(&f[0]); initialize(&f[1]);
        triangle_render(&f[0], &external); triangle_render(&f[1], &external);
        CHECK(f[0].screen.pixels != f[1].screen.pixels);
        direct_render(&f[0], &external); direct_render(&f[1], &external);
        raster_render(&f[0], &external); before = f[0].probe.rasters;
        raster_render(&f[1], &external); CHECK(f[0].probe.rasters == before);
        vm_render(&f[0], cpu, &external);
        before = f[0].probe.fills + f[0].probe.blits + f[0].probe.declines;
        vm_render(&f[1], cpu, &external);
        CHECK(f[0].probe.fills + f[0].probe.blits + f[0].probe.declines == before);
        shutdown(&f[0], &external);
        /* Releasing one frontend must not invalidate the other's userdata or
         * dedicated context, nor the embedding application's current context. */
        direct_render(&f[1], &external); raster_render(&f[1], &external);
        vm_render(&f[1], cpu, &external);
        shutdown(&f[1], &external);
    }
    CHECK(nj_vm_destroy(cpu) > 0); nj_port_shutdown(&cpu_port);
    make_current(external.current); guard_ok(&external);
    glDeleteTextures(1, &external.texture); gl_ok();
    SDL_GL_DeleteContext(external.current.context);
    SDL_DestroyWindow(external.current.window); SDL_Quit();
    printf("PASS actual SDL2 GLES2: %u checks; observed accepted fills=%u blits=%u rasters=%u declines=%u; "
           "VM port copies, mixed Image/Graphics/raster/software operations, two simultaneous ports, "
           "foreign/null context restore, three init/shutdown rounds\n",
           checks, accepted_fills, accepted_blits, accepted_rasters, declined);
    return 0;
}
