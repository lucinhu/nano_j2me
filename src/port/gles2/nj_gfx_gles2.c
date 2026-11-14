/* Synchronous GLES2 port backend. The core and default build do not link GL.
 * Render to private RGBA storage, read back, THEN commit CPU target pixels.
 * No CPU target writes occur on decline, including a late GL error. */
#include "nj2me/nj_gfx_gles2.h"
#include "nj2me/nj_gl_api.h"
#include "nj2me/nj_gfx_triangles.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if !NJ_ENABLE_GFX_ACCEL
#error "The GLES2 port backend requires NJ_ENABLE_GFX_ACCEL=1"
#endif

typedef struct texture_storage {
    int width, height;
    GLenum format;
} texture_storage;

struct nj_gles2 {
    nj_gles2_config config;
    nj_gl_api gl;
    struct triangle_state *geometry;
    nj_gles2_stats stats;
    GLuint program, framebuffer, target, source, shader_orphan;
    texture_storage target_storage, source_storage, raster_depth_storage;
    GLint sampler, force_opaque, max_texture, max_viewport[2];
    nj_u1 *upload, *readback;
    size_t upload_size, readback_size;
    /* Optional 3D merge resources, created only by nj_gles2_raster. */
    GLuint raster_program, raster_depth, raster_vertex, raster_fragment;
    int raster_ready;
    GLint raster_keys, raster_old, raster_test, raster_write, raster_pass, raster_color;
    nj_u1 *depth_readback;
    size_t depth_readback_size;
    /* Persistent triangle session (see nj_gles2_triangles_begin). While
     * tri_session is non-zero the CPU colour/depth buffers are stale and the
     * private framebuffer owns the result; the caller must not observe them. */
    int tri_session;
    nj_surface *tri_session_surface;
    nj_s4 *tri_session_depth;
    int tri_session_stride;
    int tri_session_x, tri_session_y, tri_session_w, tri_session_h;
    int tri_session_write;
    GLuint tri_session_fbo;
    nj_u4 tri_session_triangles;
};

static int clean_errors(nj_gles2 *s)
{
    int i, ok = 1;
    for (i = 0; i < 16; ++i) {
        if (s->gl.GetError() == GL_NO_ERROR) return ok;
        ok = 0;
    }
    return 0;
}
static int current(const nj_gles2 *s)
{
    return s && s->config.is_current(s->config.ud);
}
static int decline(nj_gles2 *s)
{
    if (s) ++s->stats.declines;
    return NJ_GFX_ACCEL_DECLINE;
}
static int bytes_per_pixel(int format)
{
    switch (format) {
    case NJ_PIXFMT_RGB565: return 2;
    case NJ_PIXFMT_RGB888: return 3;
    case NJ_PIXFMT_ARGB8888: return 4;
    default: return 0;
    }
}
static int dimensions(const nj_gles2 *s, int w, int h)
{
    return w > 0 && h > 0 && w <= s->max_texture && h <= s->max_texture &&
           (size_t)w <= s->config.max_pixels / (size_t)h;
}
static int reserve(nj_u1 **buffer, size_t *capacity, size_t bytes)
{
    nj_u1 *p;
    if (*capacity >= bytes) return 1;
    p = (nj_u1 *)realloc(*buffer, bytes);
    if (!p) return 0;
    *buffer = p; *capacity = bytes;
    return 1;
}
static int target_ok(const nj_gfx *g, int x, int y, int w, int h)
{
    const nj_surface *t;
    int bpp;
    if (!g || !(t = g->surf) || !t->pixels) return 0;
    bpp = bytes_per_pixel(t->format);
    return bpp && t->width > 0 && t->height > 0 &&
           (nj_s8)t->stride >= (nj_s8)t->width*bpp &&
           x >= 0 && y >= 0 && w > 0 && h > 0 &&
           (nj_s8)x+w <= t->width && (nj_s8)y+h <= t->height;
}
static void texture(nj_gles2 *s, GLuint name)
{
    s->gl.BindTexture(GL_TEXTURE_2D, name);
    s->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    s->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    s->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    s->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
/* Cache owned storage only. Every non-NULL upload refreshes all source bytes;
 * no caller pointer or cross-call texture-content identity is retained. */
static int texture_upload(nj_gles2 *s, texture_storage *storage,
                          int w, int h, GLenum format, const void *pixels)
{
    int reuse = storage->width == w && storage->height == h && storage->format == format;
    if (reuse && !pixels) return 1;
    /* An allocation/update error must not bless uncertain storage on retry. */
    storage->width = storage->height = 0;
    if (reuse)
        s->gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, format, GL_UNSIGNED_BYTE, pixels);
    else
        s->gl.TexImage2D(GL_TEXTURE_2D, 0, (GLint)format, w, h, 0, format, GL_UNSIGNED_BYTE, pixels);
    if (!clean_errors(s) || !current(s)) return 0;
    storage->width = w; storage->height = h; storage->format = format;
    return 1;
}
static int prepare(nj_gles2 *s, int w, int h)
{
    size_t bytes;
    if (!dimensions(s, w, h) || w > s->max_viewport[0] || h > s->max_viewport[1]) return 0;
    bytes = (size_t)w*(size_t)h*4u;
    if (!reserve(&s->readback, &s->readback_size, bytes)) return 0;
    /* A foreign/pre-existing GL error also declines without CPU writes. */
    if (!clean_errors(s)) return 0;
    s->gl.ActiveTexture(GL_TEXTURE0);
    texture(s, s->target);
    if (!texture_upload(s, &s->target_storage, w, h, GL_RGBA, NULL)) return 0;
    s->gl.BindFramebuffer(GL_FRAMEBUFFER, s->framebuffer);
    s->gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->target, 0);
    s->gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
    if (s->gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 0;
    s->gl.Viewport(0, 0, w, h);
    s->gl.Disable(GL_BLEND); s->gl.Disable(GL_DITHER); s->gl.Disable(GL_SCISSOR_TEST);
    s->gl.Disable(GL_DEPTH_TEST); s->gl.Disable(GL_STENCIL_TEST); s->gl.Disable(GL_CULL_FACE);
    s->gl.Disable(GL_SAMPLE_ALPHA_TO_COVERAGE); s->gl.Disable(GL_SAMPLE_COVERAGE);
    s->gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    s->gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    s->gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    return clean_errors(s);
}
/* RGBA rows are deliberately mapped in the same order as CPU top-down rows.
 * This private framebuffer is never presented directly to a window. */
static int commit(nj_gles2 *s, const nj_gfx *g, int x, int y, int w, int h)
{
    int ix, iy;
    s->gl.ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, s->readback);
    if (!clean_errors(s)) return 0;
    for (iy = 0; iy < h; ++iy) for (ix = 0; ix < w; ++ix) {
        const nj_u1 *p = s->readback + ((size_t)iy*(size_t)w+(size_t)ix)*4u;
        nj_u4 c = ((nj_u4)p[3]<<24) | ((nj_u4)p[0]<<16) | ((nj_u4)p[1]<<8) | p[2];
        nj_surface_set(g->surf, x+ix, y+iy, c);
    }
    return 1;
}
static int fill_rect(void *ud, const nj_gfx *g, int x, int y, int w, int h)
{
    nj_gles2 *s = (nj_gles2 *)ud;
    nj_u4 c;
    if (!current(s) || !target_ok(g, x, y, w, h) || g->use_xor ||
        (g->blend != NJ_BLEND_NONE && g->blend != NJ_BLEND_SRC_OVER) ||
        (g->blend == NJ_BLEND_SRC_OVER && (g->color>>24) != 255u)) return decline(s);
    if (!prepare(s, w, h)) return decline(s);
    c = g->color;
    s->gl.ClearColor((GLfloat)((c>>16)&255u)/255.0f, (GLfloat)((c>>8)&255u)/255.0f,
                 (GLfloat)(c&255u)/255.0f, (GLfloat)(c>>24)/255.0f);
    s->gl.Clear(GL_COLOR_BUFFER_BIT);
    if (!commit(s, g, x, y, w, h)) return decline(s);
    ++s->stats.fills;
    return NJ_GFX_ACCEL_DONE;
}
static nj_u4 read_color(const nj_u1 *p, int format)
{
    if (format == NJ_PIXFMT_ARGB8888) {
        nj_u4 c; memcpy(&c, p, 4); return c;
    }
    if (format == NJ_PIXFMT_RGB888)
        return 0xff000000u | ((nj_u4)p[0]<<16) | ((nj_u4)p[1]<<8) | p[2];
    {
        nj_u2 c;
        nj_u4 r, g, b;
        memcpy(&c, p, 2);
        r = (c>>11)&31u; g = (c>>5)&63u; b = c&31u;
        return 0xff000000u | (((r<<3)|(r>>2))<<16) |
               (((g<<2)|(g>>4))<<8) | (b<<3) | (b>>2);
    }
}
static int aliases(const nj_gfx *g, const nj_gfx_blit *b, int bpp)
{
    uint64_t src_size, dst_size, rows, tail;
    uintptr_t src = (uintptr_t)b->pixels, dst = (uintptr_t)g->surf->pixels;
    /* Region descriptors originate in the validated rasterizer. Still avoid
     * wraparound when checking address ranges for overlapping storage. */
    rows = (uint64_t)(unsigned)b->sy+(unsigned)b->height-1u;
    tail = ((uint64_t)(unsigned)b->sx+(unsigned)b->width)*(unsigned)bpp;
    if (tail > PTRDIFF_MAX || rows > ((uint64_t)PTRDIFF_MAX-tail)/(uint64_t)b->stride) return 1;
    src_size = rows*(uint64_t)b->stride + tail;
    dst_size = (uint64_t)(unsigned)g->surf->height*(unsigned)g->surf->stride;
    if (src_size > UINTPTR_MAX-src || dst_size > UINTPTR_MAX-dst || dst_size > PTRDIFF_MAX) return 1;
    return src < dst+(uintptr_t)dst_size && dst < src+(uintptr_t)src_size;
}
/* Inverse transform of rectangle EDGES, not pixel indices. */
static void texcoord(int transform, float a, float b, int w, int h, GLfloat *out)
{
    float u, v;
    switch (transform) {
    case NJ_TRANS_MIRROR_ROT180: u=a; v=(float)h-b; break;
    case NJ_TRANS_MIRROR: u=(float)w-a; v=b; break;
    case NJ_TRANS_ROT180: u=(float)w-a; v=(float)h-b; break;
    case NJ_TRANS_MIRROR_ROT270: u=b; v=a; break;
    case NJ_TRANS_ROT90: u=b; v=(float)h-a; break;
    case NJ_TRANS_ROT270: u=(float)w-b; v=a; break;
    case NJ_TRANS_MIRROR_ROT90: u=(float)w-b; v=(float)h-a; break;
    default: u=a; v=b; break;
    }
    out[0] = u/(float)w; out[1] = v/(float)h;
}
static int blit(void *ud, const nj_gfx *g, const nj_gfx_blit *b)
{
    static const GLfloat quad[8] = {-1,-1, 1,-1, -1,1, 1,1};
    nj_gles2 *s = (nj_gles2 *)ud;
    GLfloat uv[8];
    nj_s8 left, top;
    size_t bytes;
    int x, y, bpp;
    if (!current(s) || !b || !target_ok(g, b->clip_x, b->clip_y, b->clip_w, b->clip_h) ||
        !b->pixels || b->mask || b->xor_enabled || b->transform < 0 || b->transform > 7 ||
        b->sx < 0 || b->sy < 0 || !(bpp = bytes_per_pixel(b->format)) ||
        !dimensions(s, b->width, b->height) ||
        b->stride < ((nj_s8)b->sx+b->width)*bpp ||
        (b->rop != NJ_ROP_SET && b->rop != NJ_ROP_PAINT && b->rop != NJ_ROP_OPAQUE) ||
        (b->rop != NJ_ROP_SET && b->alpha != 255) || aliases(g, b, bpp)) return decline(s);
    bytes = (size_t)b->width*(size_t)b->height*4u;
    if (!reserve(&s->upload, &s->upload_size, bytes)) return decline(s);
    for (y = 0; y < b->height; ++y) for (x = 0; x < b->width; ++x) {
        const nj_u1 *p = b->pixels + ((nj_s8)b->sy+y)*b->stride + ((nj_s8)b->sx+x)*bpp;
        nj_u4 c = read_color(p, b->format);
        nj_u1 *out = s->upload + ((size_t)y*(size_t)b->width+(size_t)x)*4u;
        if (b->rop == NJ_ROP_PAINT && (c>>24) != 255u) return decline(s);
        out[0]=(nj_u1)(c>>16); out[1]=(nj_u1)(c>>8); out[2]=(nj_u1)c; out[3]=(nj_u1)(c>>24);
    }
    if (!prepare(s, b->clip_w, b->clip_h)) return decline(s);
    texture(s, s->source);
    if (!texture_upload(s, &s->source_storage, b->width, b->height, GL_RGBA, s->upload))
        return decline(s);
    s->gl.UseProgram(s->program);
    s->gl.Uniform1i(s->sampler, 0);
    s->gl.Uniform1f(s->force_opaque, b->rop == NJ_ROP_OPAQUE ? 1.0f : 0.0f);
    left = (nj_s8)b->clip_x - ((nj_s8)b->dx+g->translate_x);
    top = (nj_s8)b->clip_y - ((nj_s8)b->dy+g->translate_y);
    texcoord(b->transform, (float)left, (float)top, b->width, b->height, uv);
    texcoord(b->transform, (float)(left+b->clip_w), (float)top, b->width, b->height, uv+2);
    texcoord(b->transform, (float)left, (float)(top+b->clip_h), b->width, b->height, uv+4);
    texcoord(b->transform, (float)(left+b->clip_w), (float)(top+b->clip_h), b->width, b->height, uv+6);
    s->gl.BindBuffer(GL_ARRAY_BUFFER, 0);
    s->gl.EnableVertexAttribArray(0); s->gl.EnableVertexAttribArray(1);
    s->gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    s->gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uv);
    s->gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    /* Do not leave pointers to automatic storage in persistent GL state. */
    s->gl.DisableVertexAttribArray(0); s->gl.DisableVertexAttribArray(1);
    s->gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    s->gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    if (!commit(s, g, b->clip_x, b->clip_y, b->clip_w, b->clip_h)) return decline(s);
    ++s->stats.blits;
    return NJ_GFX_ACCEL_DONE;
}
static const nj_gfx_accel backend = {NJ_GFX_ACCEL_VERSION, fill_rect, blit};
const nj_gfx_accel *nj_gles2_backend(void) { return &backend; }

static GLuint shader(nj_gles2 *s, GLenum type, const char *text)
{
    GLuint name;
    GLint ok = 0;
    if (!current(s)) return 0;
    if (s->shader_orphan) {
        s->gl.DeleteShader(s->shader_orphan);
        s->shader_orphan = 0;
    }
    name = s->gl.CreateShader(type);
    if (!name) return 0;
    {
        const char *parts[2];
        parts[0] = s->config.desktop_gl ?
            "#version 120\n#define highp\n#define mediump\n#define lowp\n" : "";
        parts[1] = text;
        s->gl.ShaderSource(name, 2, parts, NULL);
    }
    s->gl.CompileShader(name);
    if (!current(s)) { s->shader_orphan = name; return 0; }
    s->gl.GetShaderiv(name, GL_COMPILE_STATUS, &ok);
    if (!current(s)) { s->shader_orphan = name; return 0; }
    if (!ok) { s->gl.DeleteShader(name); return 0; }
    return name;
}
/* Both passes sample the SAME old-depth texture. In particular the colour
 * pass cannot change a depth value against which the depth pass is compared.
 * Never reconstruct a 25-bit key as one float: GLES2 highp need not represent
 * every such integer. Only individual 8-bit channels participate in tests. */
static void raster_delete(nj_gles2 *s)
{
    if (s->raster_vertex) s->gl.DeleteShader(s->raster_vertex);
    if (s->raster_fragment) s->gl.DeleteShader(s->raster_fragment);
    if (s->raster_program) s->gl.DeleteProgram(s->raster_program);
    if (s->raster_depth) s->gl.DeleteTextures(1, &s->raster_depth);
    s->raster_vertex = s->raster_fragment = s->raster_program = s->raster_depth = 0;
    s->raster_ready = 0;
    memset(&s->raster_depth_storage, 0, sizeof s->raster_depth_storage);
}
static int raster_init(nj_gles2 *s)
{
    static const char vs[] =
        "attribute vec2 position; attribute vec2 texcoord; varying highp vec2 uv;"
        "void main(){ uv=texcoord; gl_Position=vec4(position,0.0,1.0); }";
    static const char fs[] =
        "#ifdef GL_ES\nprecision highp float;\n#endif\nvarying highp vec2 uv;"
        "uniform highp sampler2D keys; uniform highp sampler2D oldDepth;"
        "uniform bool depthTest; uniform bool depthWrite; uniform bool depthPass;"
        "uniform vec4 color;"
        "bool ge(vec4 a,vec4 b){"
        "if(a.r!=b.r) return a.r>b.r; if(a.g!=b.g) return a.g>b.g;"
        "if(a.b!=b.b) return a.b>b.b; return a.a>=b.a; }"
        "void main(){ vec4 k=floor(texture2D(keys,uv)*255.0+0.5);"
        "vec4 old=floor(texture2D(oldDepth,uv)*255.0+0.5);"
        "bool covered=any(greaterThan(k,vec4(0.0)));"
        "bool pass=covered && (!depthTest || ge(k,old));"
        "if(depthPass) gl_FragColor=((pass && depthWrite)?k:old)/255.0;"
        "else gl_FragColor=pass?color:vec4(0.0); }";
    GLuint v = 0, f = 0, p = 0, depth = 0;
    GLint ok = 0;
    if (s->raster_ready) return 1;
    /* A failed initialization may have lost context usability before cleanup.
     * Keep those names reachable and delete them before allocating any more. */
    if (s->raster_program || s->raster_depth || s->raster_vertex || s->raster_fragment) {
        if (!current(s)) return 0;
        raster_delete(s);
        if (!clean_errors(s)) return 0;
    }
    v = shader(s, GL_VERTEX_SHADER, vs); f = shader(s, GL_FRAGMENT_SHADER, fs);
    if (!v || !f) goto fail;
    p = s->gl.CreateProgram();
    if (!p) goto fail;
    s->gl.AttachShader(p, v); s->gl.AttachShader(p, f);
    s->gl.BindAttribLocation(p, 0, "position");
    s->gl.BindAttribLocation(p, 1, "texcoord");
    s->gl.LinkProgram(p);
    s->gl.GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) goto fail;
    s->raster_keys = s->gl.GetUniformLocation(p, "keys");
    s->raster_old = s->gl.GetUniformLocation(p, "oldDepth");
    s->raster_test = s->gl.GetUniformLocation(p, "depthTest");
    s->raster_write = s->gl.GetUniformLocation(p, "depthWrite");
    s->raster_pass = s->gl.GetUniformLocation(p, "depthPass");
    s->raster_color = s->gl.GetUniformLocation(p, "color");
    s->gl.GenTextures(1, &depth);
    if (!depth || s->raster_keys < 0 || s->raster_old < 0 || s->raster_test < 0 ||
        s->raster_write < 0 || s->raster_pass < 0 || s->raster_color < 0 ||
        !clean_errors(s)) goto fail;
    s->gl.DeleteShader(v); s->gl.DeleteShader(f); v = f = 0;
    if (!clean_errors(s)) goto fail;
    s->raster_program = p; s->raster_depth = depth; s->raster_ready = 1;
    return 1;
fail:
    s->raster_vertex = v; s->raster_fragment = f;
    s->raster_program = p; s->raster_depth = depth;
    if (current(s)) {
        raster_delete(s);
        (void)clean_errors(s);
    }
    return 0;
}
static void pack_key(nj_u1 *out, nj_u4 key)
{
    out[0] = (nj_u1)(key>>24); out[1] = (nj_u1)(key>>16);
    out[2] = (nj_u1)(key>>8); out[3] = (nj_u1)key;
}
static nj_u4 unpack_key(const nj_u1 *p)
{
    return ((nj_u4)p[0]<<24) | ((nj_u4)p[1]<<16) | ((nj_u4)p[2]<<8) | p[3];
}
/* Called only after the core has validated every buffer span. Include padding
 * BETWEEN rows, just as the reference callback does, and protect metadata. */
static int result_overlaps(uintptr_t result, const void *base, size_t bytes)
{
    uintptr_t start = (uintptr_t)base;
    return result < start+bytes && start < result+sizeof(nj_u4);
}
static int raster_result_ok(const nj_gfx_raster *r, const nj_u4 *drawn)
{
    const nj_surface *t = r->surface;
    uintptr_t out = (uintptr_t)drawn;
    size_t pixels = (size_t)(t->height-1)*(size_t)t->stride +
                    (size_t)t->width*(size_t)bytes_per_pixel(t->format);
    size_t keys = ((size_t)(r->height-1)*(size_t)r->key_stride+(size_t)r->width)*sizeof(nj_s4);
    size_t depth = r->depth ? ((size_t)(t->height-1)*(size_t)r->depth_stride+
                              (size_t)t->width)*sizeof(nj_s4) : 0;
    if (out > UINTPTR_MAX-sizeof *drawn) return 0;
    return !result_overlaps(out, r, sizeof *r) &&
           !result_overlaps(out, t, sizeof *t) &&
           !result_overlaps(out, t->pixels, pixels) &&
           !result_overlaps(out, r->keys, keys) &&
           (!depth || !result_overlaps(out, r->depth, depth));
}
int nj_gles2_raster(void *ud, const nj_gfx_raster *r, nj_u4 *drawn)
{
    static const GLfloat quad[8] = {-1,-1, 1,-1, -1,1, 1,1};
    static const GLfloat uv[8] = {0,0, 1,0, 0,1, 1,1};
    nj_gles2 *s = (nj_gles2 *)ud;
    nj_u4 touched = 0;
    size_t bytes, i;
    int x, y;
    if (!current(s) || !r || !drawn || !dimensions(s, r->width, r->height) ||
        r->width > s->max_viewport[0] || r->height > s->max_viewport[1] ||
        !nj_gfx_raster_validate(r) || !raster_result_ok(r, drawn)) return decline(s);
    bytes = (size_t)r->width*(size_t)r->height*4u;
    if (!reserve(&s->upload, &s->upload_size, bytes) ||
        (r->depth_write && !reserve(&s->depth_readback, &s->depth_readback_size, bytes)) ||
        !prepare(s, r->width, r->height) || !raster_init(s)) return decline(s);
    for (y = 0; y < r->height; ++y) {
        const nj_s4 *key = r->keys+(size_t)y*(size_t)r->key_stride;
        for (x = 0; x < r->width; ++x)
            pack_key(s->upload+((size_t)y*(size_t)r->width+(size_t)x)*4u, (nj_u4)key[x]);
    }
    /* prepare leaves texture unit 0 active. The target is never sampled. */
    texture(s, s->source);
    if (!texture_upload(s, &s->source_storage, r->width, r->height, GL_RGBA, s->upload))
        return decline(s);
    for (y = 0; y < r->height; ++y) {
        const nj_s4 *old = (r->depth_test || r->depth_write) ?
            r->depth+(size_t)(r->y+y)*(size_t)r->depth_stride+(size_t)r->x : NULL;
        for (x = 0; x < r->width; ++x)
            pack_key(s->upload+((size_t)y*(size_t)r->width+(size_t)x)*4u, old ? (nj_u4)old[x] : 0u);
    }
    s->gl.ActiveTexture(GL_TEXTURE1);
    texture(s, s->raster_depth);
    if (!texture_upload(s, &s->raster_depth_storage, r->width, r->height, GL_RGBA, s->upload))
        return decline(s);
    s->gl.UseProgram(s->raster_program);
    s->gl.Uniform1i(s->raster_keys, 0); s->gl.Uniform1i(s->raster_old, 1);
    s->gl.Uniform1i(s->raster_test, r->depth_test); s->gl.Uniform1i(s->raster_write, r->depth_write);
    s->gl.Uniform4f(s->raster_color, (GLfloat)((r->color>>16)&255u)/255.0f,
                 (GLfloat)((r->color>>8)&255u)/255.0f, (GLfloat)(r->color&255u)/255.0f, 1.0f);
    s->gl.Uniform1i(s->raster_pass, 0);
    s->gl.BindBuffer(GL_ARRAY_BUFFER, 0);
    s->gl.EnableVertexAttribArray(0); s->gl.EnableVertexAttribArray(1);
    s->gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    s->gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uv);
    s->gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    s->gl.ReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, s->readback);
    if (r->depth_write) {
        s->gl.Uniform1i(s->raster_pass, 1);
        s->gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        s->gl.ReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, s->depth_readback);
    }
    s->gl.DisableVertexAttribArray(0); s->gl.DisableVertexAttribArray(1);
    s->gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    s->gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    s->gl.ActiveTexture(GL_TEXTURE0);
    if (!clean_errors(s) || !current(s)) return decline(s);
    /* Validate readbacks in full before committing either CPU buffer. Alpha
     * is an unambiguous GPU pass flag because the packet colour is opaque. */
    for (i = 0; i < bytes; i += 4u) {
        if (s->readback[i+3u] != 0 && s->readback[i+3u] != 255) return decline(s);
        if (r->depth_write && unpack_key(s->depth_readback+i) > NJ_GFX_RASTER_MAX_KEY)
            return decline(s);
    }
    for (y = 0; y < r->height; ++y) {
        nj_s4 *depth = r->depth_write ?
            r->depth+(size_t)(r->y+y)*(size_t)r->depth_stride+(size_t)r->x : NULL;
        for (x = 0; x < r->width; ++x) {
            const nj_u1 *color = s->readback+((size_t)y*(size_t)r->width+(size_t)x)*4u;
            if (color[3]) {
                nj_u4 c = 0xff000000u | ((nj_u4)color[0]<<16) | ((nj_u4)color[1]<<8) | color[2];
                nj_surface_set(r->surface, r->x+x, r->y+y, c);
                touched = 1;
            }
            if (depth) depth[x] = (nj_s4)unpack_key(s->depth_readback+
                                       ((size_t)y*(size_t)r->width+(size_t)x)*4u);
        }
    }
    *drawn = touched;
    ++s->stats.rasters;
    return NJ_GFX_ACCEL_DONE;
}
#include "nj_gfx_triangles_gles2.inc"

void nj_gles2_abandon(nj_gles2 *s)
{
    if (s) { triangles_abandon(s); free(s->upload); free(s->readback); free(s->depth_readback); free(s); }
}
int nj_gles2_destroy(nj_gles2 *s)
{
    if (!s) return 1;
    if (!current(s)) return 0;
    s->gl.UseProgram(0); /* A bound program otherwise remains pending deletion. */
    if (s->shader_orphan) s->gl.DeleteShader(s->shader_orphan);
    s->gl.DeleteProgram(s->program);
    raster_delete(s);
    triangles_delete(s);
    s->gl.DeleteFramebuffers(1, &s->framebuffer);
    s->gl.DeleteTextures(1, &s->target);
    s->gl.DeleteTextures(1, &s->source);
    nj_gles2_abandon(s);
    return 1;
}
void nj_gles2_get_stats(const nj_gles2 *s, nj_gles2_stats *out)
{
    if (out) {
        if (s) *out = s->stats;
        else memset(out, 0, sizeof *out);
    }
}
nj_gles2 *nj_gles2_create(const nj_gles2_config *config)
{
    static const char vs[] =
        "attribute vec2 position; attribute vec2 texcoord; varying highp vec2 uv;"
        "void main(){ uv=texcoord; gl_Position=vec4(position,0.0,1.0); }";
    static const char fs[] =
        "#ifdef GL_ES\nprecision highp float;\n#endif\nvarying highp vec2 uv; uniform sampler2D image;"
        "uniform float forceOpaque; void main(){ vec4 c=texture2D(image,uv);"
        "gl_FragColor=vec4(c.rgb,mix(c.a,1.0,forceOpaque)); }";
    nj_gles2 *s;
    GLuint v = 0, f = 0;
    GLint ok = 0;
    if (!config || !config->is_current || !config->is_current(config->ud)) return NULL;
    s = (nj_gles2 *)calloc(1, sizeof *s);
    if (!s) return NULL;
    s->config = *config;
    if (config->gl) s->gl = *config->gl;
    else if (!nj_gl_api_default(&s->gl)) { free(s); return NULL; }
    s->config.gl = NULL; /* The dispatch itself was copied, not retained. */
    if (s->gl.desktop_gl != !!config->desktop_gl ||
        !s->gl.ActiveTexture || !s->gl.AttachShader || !s->gl.BindAttribLocation ||
        !s->gl.BindBuffer || !s->gl.BindFramebuffer || !s->gl.BindRenderbuffer ||
        !s->gl.BindTexture || !s->gl.BlendEquationSeparate || !s->gl.BlendFuncSeparate ||
        !s->gl.CheckFramebufferStatus || !s->gl.Clear || !s->gl.ClearColor ||
        !s->gl.ColorMask || !s->gl.CompileShader || !s->gl.CreateProgram ||
        !s->gl.CreateShader || !s->gl.DeleteFramebuffers || !s->gl.DeleteProgram ||
        !s->gl.DeleteRenderbuffers || !s->gl.DeleteShader || !s->gl.DeleteTextures ||
        !s->gl.DepthFunc || !s->gl.DepthMask || !s->gl.Disable ||
        !s->gl.DisableVertexAttribArray || !s->gl.DrawArrays || !s->gl.Enable ||
        !s->gl.EnableVertexAttribArray || !s->gl.FramebufferRenderbuffer || !s->gl.FramebufferTexture2D ||
        !s->gl.GenFramebuffers || !s->gl.GenRenderbuffers || !s->gl.GenTextures ||
        !s->gl.GetError || !s->gl.GetIntegerv || !s->gl.GetProgramiv ||
        !s->gl.GetRenderbufferParameteriv || !s->gl.GetShaderiv || !s->gl.GetString ||
        !s->gl.GetUniformLocation || !s->gl.LinkProgram || !s->gl.PixelStorei ||
        !s->gl.ReadPixels || !s->gl.RenderbufferStorage || !s->gl.ShaderSource ||
        !s->gl.TexImage2D || !s->gl.TexSubImage2D || !s->gl.TexParameteri || !s->gl.Uniform1f ||
        !s->gl.Uniform1i || !s->gl.Uniform2f || !s->gl.Uniform3f ||
        !s->gl.Uniform4f || !s->gl.UseProgram || !s->gl.VertexAttribPointer ||
        !s->gl.Viewport ||
        (config->desktop_gl ? !s->gl.DepthRange :
         (!s->gl.DepthRangef || !s->gl.GetShaderPrecisionFormat))) {
        free(s); return NULL;
    }
    if (!s->config.max_pixels) s->config.max_pixels = 1024u*1024u;
    if (s->config.max_pixels > SIZE_MAX/4u || !clean_errors(s)) goto fail;
    s->gl.GetIntegerv(GL_MAX_TEXTURE_SIZE, &s->max_texture);
    s->gl.GetIntegerv(GL_MAX_VIEWPORT_DIMS, s->max_viewport);
    v = shader(s, GL_VERTEX_SHADER, vs); f = shader(s, GL_FRAGMENT_SHADER, fs);
    if (!v || !f) goto fail;
    s->program = s->gl.CreateProgram();
    s->gl.AttachShader(s->program, v); s->gl.AttachShader(s->program, f);
    s->gl.BindAttribLocation(s->program, 0, "position");
    s->gl.BindAttribLocation(s->program, 1, "texcoord");
    s->gl.LinkProgram(s->program);
    s->gl.GetProgramiv(s->program, GL_LINK_STATUS, &ok);
    if (!ok) goto fail;
    s->gl.DeleteShader(v); s->gl.DeleteShader(f); v = f = 0;
    s->sampler = s->gl.GetUniformLocation(s->program, "image");
    s->force_opaque = s->gl.GetUniformLocation(s->program, "forceOpaque");
    s->gl.GenTextures(1, &s->target); s->gl.GenTextures(1, &s->source);
    s->gl.GenFramebuffers(1, &s->framebuffer);
    if (!s->target || !s->source || !s->framebuffer || s->max_texture <= 0 ||
        s->max_viewport[0] <= 0 || s->max_viewport[1] <= 0 ||
        s->sampler < 0 || s->force_opaque < 0 || !clean_errors(s)) goto fail;
    return s;
fail:
    /* Context loss during creation must not lose the only CPU-state handle.
     * If unusable, GL objects are reclaimed when the owner destroys context. */
    if (current(s)) {
        if (v) s->gl.DeleteShader(v);
        if (f) s->gl.DeleteShader(f);
        if (!nj_gles2_destroy(s)) nj_gles2_abandon(s);
    } else {
        nj_gles2_abandon(s);
    }
    return NULL;
}
