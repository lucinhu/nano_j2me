/* Proc-loaded libretro bridge. The frontend owns the context; only the batch
 * between begin/end may use our private objects. No linked GL entry points. */
#include "../libretro/nj_libretro_hw.h"
#include "nj2me/nj_gfx_gles2.h"
#include "nj2me/nj_gl_api.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Tokens outside the ES2 declarations, used only after capability checks. */
#define HW_READ_FRAMEBUFFER 0x8CA8
#define HW_DRAW_FRAMEBUFFER 0x8CA9
#define HW_READ_FRAMEBUFFER_BINDING 0x8CAA
#define HW_VERTEX_ARRAY_BINDING 0x85B5
#define HW_PIXEL_PACK_BUFFER 0x88EB
#define HW_PIXEL_UNPACK_BUFFER 0x88EC
#define HW_PIXEL_PACK_BUFFER_BINDING 0x88ED
#define HW_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#define HW_PACK_ROW_LENGTH 0x0D02
#define HW_PACK_SKIP_ROWS 0x0D03
#define HW_PACK_SKIP_PIXELS 0x0D04
#define HW_UNPACK_ROW_LENGTH 0x0CF2
#define HW_UNPACK_SKIP_ROWS 0x0CF3
#define HW_UNPACK_SKIP_PIXELS 0x0CF4
#define HW_UNPACK_IMAGE_HEIGHT 0x806E
#define HW_UNPACK_SKIP_IMAGES 0x806D
#define HW_PACK_IMAGE_HEIGHT 0x806C
#define HW_PACK_SKIP_IMAGES 0x806B
#define HW_PACK_SWAP_BYTES 0x0D00
#define HW_PACK_LSB_FIRST 0x0D01
#define HW_UNPACK_SWAP_BYTES 0x0CF0
#define HW_UNPACK_LSB_FIRST 0x0CF1
#define HW_SAMPLER_BINDING 0x8919
#define HW_ATTRIB_INTEGER 0x88FD
#define HW_ATTRIB_DIVISOR 0x88FE
#define HW_ATTRIB_LONG 0x874E
#define HW_ATTRIB_BINDING 0x82D4
#define HW_ATTRIB_RELATIVE_OFFSET 0x82D5
#define HW_BINDING_DIVISOR 0x82D6
#define HW_BINDING_OFFSET 0x82D7
#define HW_BINDING_STRIDE 0x82D8
#define HW_BINDING_BUFFER 0x8F4F
#define HW_FRAMEBUFFER_SRGB 0x8DB9
#define HW_COLOR_LOGIC_OP 0x0BF2
#define HW_ALPHA_TEST 0x0BC0
#define HW_RASTERIZER_DISCARD 0x8C89
#define HW_CLIP_PLANE0 0x3000
#define HW_MAX_CLIP_PLANES 0x0D32
#define HW_TRANSFORM_FEEDBACK_ACTIVE 0x8E24
#define HW_TRANSFORM_FEEDBACK_PAUSED 0x8E23
#define HW_POLYGON_MODE 0x0B40
#define HW_POLYGON_SMOOTH 0x0B41
#define HW_SAMPLE_ALPHA_TO_ONE 0x809F
#define HW_DEPTH_CLAMP 0x864F
#define HW_SAMPLE_MASK 0x8E51
#define HW_CLIP_ORIGIN 0x935C
#define HW_CLIP_DEPTH_MODE 0x935D
#define HW_LOWER_LEFT 0x8CA1
#define HW_NEGATIVE_ONE_TO_ONE 0x935E
#define HW_FILL 0x1B02

#define HW_ATTRIBS 4
#define HW_CAPS 48
#define HW_STORES 20

typedef void (GL_APIENTRY *hw_bind_vao_fn)(GLuint);
typedef void (GL_APIENTRY *hw_bind_sampler_fn)(GLuint, GLuint);
typedef void (GL_APIENTRY *hw_get_double_fn)(GLenum, double *);
typedef void (GL_APIENTRY *hw_attrib_integer_fn)(GLuint, GLint, GLenum, GLsizei, const void *);
typedef void (GL_APIENTRY *hw_get_attrib_float_fn)(GLuint, GLenum, GLfloat *);
typedef void (GL_APIENTRY *hw_set_attrib_float_fn)(GLuint, const GLfloat *);
typedef GLenum (GL_APIENTRY *hw_reset_fn)(void);
typedef void (GL_APIENTRY *hw_polygon_mode_fn)(GLenum, GLenum);
typedef void (GL_APIENTRY *hw_clip_control_fn)(GLenum, GLenum);
typedef void (GL_APIENTRY *hw_attrib_divisor_fn)(GLuint, GLuint);
typedef void (GL_APIENTRY *hw_attrib_long_fn)(GLuint, GLint, GLenum, GLsizei, const void *);
typedef void (GL_APIENTRY *hw_bind_vertex_fn)(GLuint, GLuint, GLintptr, GLsizei);
typedef void (GL_APIENTRY *hw_attrib_format_fn)(GLuint, GLint, GLenum, GLboolean, GLuint);
typedef void (GL_APIENTRY *hw_attrib_iformat_fn)(GLuint, GLint, GLenum, GLuint);
typedef void (GL_APIENTRY *hw_attrib_binding_fn)(GLuint, GLuint);
typedef void (GL_APIENTRY *hw_get_indexed_fn)(GLenum, GLuint, GLint *);
typedef void (GL_APIENTRY *hw_get_indexed64_fn)(GLenum, GLuint, int64_t *);
typedef void (GL_APIENTRY *hw_get_attrib_integer_fn)(GLuint, GLenum, GLint *);
typedef void (GL_APIENTRY *hw_set_attrib_integer_fn)(GLuint, const GLint *);
typedef void (GL_APIENTRY *hw_get_attrib_double_fn)(GLuint, GLenum, double *);
typedef void (GL_APIENTRY *hw_set_attrib_double_fn)(GLuint, const double *);

typedef struct hw_attrib {
    GLint enabled, size, type, normalized, stride, buffer, integer, divisor, long_value;
    void *pointer;
    GLfloat current[4];
    GLint current_integer[4];
    double current_double[4];
    GLint binding, relative_offset;
    GLint binding_buffer, binding_stride, binding_divisor;
    int64_t binding_offset;
} hw_attrib;
typedef struct hw_snapshot {
    GLint program, draw_fb, read_fb, renderbuffer, active_texture, textures[2];
    GLint samplers[2], vao, array_buffer, pack_buffer, unpack_buffer;
    GLint viewport[4], scissor[4], depth_func, cull_face, front_face;
    GLint polygon_mode[2], clip_origin, clip_depth;
    GLint blend_src_rgb, blend_dst_rgb, blend_src_alpha, blend_dst_alpha;
    GLint blend_eq_rgb, blend_eq_alpha, stores[HW_STORES];
    GLboolean color_mask[4], depth_mask, enables[HW_CAPS];
    GLfloat clear_color[4], blend_color[4];
    double depth_range[2], clear_depth;
    hw_attrib attrib[HW_ATTRIBS];
} hw_snapshot;
typedef struct hw_gl {
    nj_libretro_hw_config config;
    nj_gl_api gl;
    nj_gles2 *backend;
    hw_bind_vao_fn bind_vao;
    hw_bind_sampler_fn bind_sampler;
    hw_get_double_fn get_double;
    hw_attrib_integer_fn attrib_integer;
    hw_get_attrib_float_fn get_attrib_float;
    hw_set_attrib_float_fn set_attrib_float;
    hw_reset_fn reset;
    hw_polygon_mode_fn polygon_mode;
    hw_clip_control_fn clip_control;
    hw_attrib_divisor_fn attrib_divisor;
    hw_attrib_long_fn attrib_long;
    hw_bind_vertex_fn bind_vertex;
    hw_attrib_format_fn attrib_format;
    hw_attrib_iformat_fn attrib_iformat, attrib_lformat;
    hw_attrib_binding_fn attrib_binding;
    hw_attrib_divisor_fn binding_divisor;
    hw_get_indexed_fn get_indexed;
    hw_get_indexed64_fn get_indexed64;
    hw_get_attrib_integer_fn get_attrib_integer;
    hw_set_attrib_integer_fn set_attrib_integer;
    hw_get_attrib_double_fn get_attrib_double;
    hw_set_attrib_double_fn set_attrib_double;
    GLenum handoff_error;
    char error_info[96];
    int desktop, es3, separate_fb, pbo, integer_attrib, transform_feedback;
    int active, n_caps, n_stores;
    GLenum caps[HW_CAPS], stores[HW_STORES];
    hw_snapshot saved;
    GLuint program, texture;
    GLint sampler, max_texture, max_viewport[2];
    unsigned char *upload;
    size_t upload_size;
    char description[384];
} hw_gl;

static int has_extension(const char *list, const char *name)
{
    size_t n = strlen(name);
    const char *p = list;
    if (!p) return 0;
    while ((p = strstr(p, name)) != NULL) {
        if ((p == list || p[-1] == ' ') && (!p[n] || p[n] == ' ')) return 1;
        p += n;
    }
    return 0;
}
static void *resolve(void *ud, const char *name)
{
    hw_gl *s = (hw_gl *)ud;
    nj_libretro_gl_proc fn = s->config.get_proc(s->config.ud, name);
    void *p = NULL;
    if (sizeof p == sizeof fn) memcpy(&p, &fn, sizeof p);
    if ((uintptr_t)p <= 3 || (uintptr_t)p == UINTPTR_MAX) return NULL;
    return p;
}
#define HW_LOAD(member, name) do { \
    void *hw_p = resolve(s, name); \
    if (sizeof s->member == sizeof hw_p) memcpy(&s->member, &hw_p, sizeof hw_p); \
} while (0)

static int valid(void *ud)
{
    hw_gl *s = (hw_gl *)ud;
    /* Never inspect/drain GL errors: valid is a read-only probe. */
    return s && s->config.is_current(s->config.ud) &&
           (!s->reset || s->reset() == GL_NO_ERROR) &&
           s->gl.GetString(GL_VERSION) != NULL;
}
static int backend_current(void *ud)
{
    hw_gl *s = (hw_gl *)ud;
    return s && s->active && valid(s);
}
static void set_enabled(hw_gl *s, GLenum cap, GLboolean enabled)
{
    if (enabled) s->gl.Enable(cap); else s->gl.Disable(cap);
}
static void save_state(hw_gl *s)
{
    hw_snapshot *b = &s->saved;
    nj_gl_api *g = &s->gl;
    int i;
    g->GetIntegerv(GL_CURRENT_PROGRAM, &b->program);
    g->GetIntegerv(GL_FRAMEBUFFER_BINDING, &b->draw_fb);
    b->read_fb = b->draw_fb;
    if (s->separate_fb) g->GetIntegerv(HW_READ_FRAMEBUFFER_BINDING, &b->read_fb);
    g->GetIntegerv(GL_RENDERBUFFER_BINDING, &b->renderbuffer);
    g->GetIntegerv(GL_ACTIVE_TEXTURE, &b->active_texture);
    for (i = 0; i < 2; ++i) {
        g->ActiveTexture(GL_TEXTURE0+(GLenum)i);
        g->GetIntegerv(GL_TEXTURE_BINDING_2D, &b->textures[i]);
        if (s->bind_sampler) g->GetIntegerv(HW_SAMPLER_BINDING, &b->samplers[i]);
    }
    g->GetIntegerv(GL_ARRAY_BUFFER_BINDING, &b->array_buffer);
    if (s->bind_vao) {
        g->GetIntegerv(HW_VERTEX_ARRAY_BINDING, &b->vao);
        /* Client arrays are legal on ES3 only with VAO 0. Preserve its state
         * independently of whichever VAO the frontend left bound. */
        s->bind_vao(0);
    }
    for (i = 0; i < HW_ATTRIBS; ++i) {
        hw_attrib *a = &b->attrib[i];
        g->GetVertexAttribiv((GLuint)i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &a->enabled);
        g->GetVertexAttribiv((GLuint)i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &a->size);
        g->GetVertexAttribiv((GLuint)i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &a->type);
        g->GetVertexAttribiv((GLuint)i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &a->normalized);
        g->GetVertexAttribiv((GLuint)i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &a->stride);
        g->GetVertexAttribiv((GLuint)i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &a->buffer);
        g->GetVertexAttribPointerv((GLuint)i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &a->pointer);
        a->integer = 0;
        if (s->integer_attrib) g->GetVertexAttribiv((GLuint)i, HW_ATTRIB_INTEGER, &a->integer);
        if (s->attrib_divisor) g->GetVertexAttribiv((GLuint)i, HW_ATTRIB_DIVISOR, &a->divisor);
        if (s->attrib_long) g->GetVertexAttribiv((GLuint)i, HW_ATTRIB_LONG, &a->long_value);
        if (s->bind_vertex) {
            g->GetVertexAttribiv((GLuint)i, HW_ATTRIB_BINDING, &a->binding);
            g->GetVertexAttribiv((GLuint)i, HW_ATTRIB_RELATIVE_OFFSET, &a->relative_offset);
            /* VertexAttribPointer/Divisor touch binding i even if attribute i
             * originally referenced another binding. Save the touched slots. */
            s->get_indexed(HW_BINDING_BUFFER, (GLuint)i, &a->binding_buffer);
            s->get_indexed(HW_BINDING_STRIDE, (GLuint)i, &a->binding_stride);
            s->get_indexed(HW_BINDING_DIVISOR, (GLuint)i, &a->binding_divisor);
            s->get_indexed64(HW_BINDING_OFFSET, (GLuint)i, &a->binding_offset);
        }
        /* Desktop compatibility attribute 0 aliases the current vertex, whose
         * CURRENT_VERTEX_ATTRIB query is not legal. No constant 0 is touched. */
        if (i || !s->desktop) {
            if (a->long_value) s->get_attrib_double((GLuint)i, GL_CURRENT_VERTEX_ATTRIB, a->current_double);
            else if (a->integer) s->get_attrib_integer((GLuint)i, GL_CURRENT_VERTEX_ATTRIB, a->current_integer);
            else s->get_attrib_float((GLuint)i, GL_CURRENT_VERTEX_ATTRIB, a->current);
        }
    }
    if (s->pbo) {
        g->GetIntegerv(HW_PIXEL_PACK_BUFFER_BINDING, &b->pack_buffer);
        g->GetIntegerv(HW_PIXEL_UNPACK_BUFFER_BINDING, &b->unpack_buffer);
    }
    g->GetIntegerv(GL_VIEWPORT, b->viewport);
    g->GetIntegerv(GL_SCISSOR_BOX, b->scissor);
    g->GetBooleanv(GL_COLOR_WRITEMASK, b->color_mask);
    g->GetBooleanv(GL_DEPTH_WRITEMASK, &b->depth_mask);
    g->GetIntegerv(GL_DEPTH_FUNC, &b->depth_func);
    g->GetIntegerv(GL_CULL_FACE_MODE, &b->cull_face);
    g->GetIntegerv(GL_FRONT_FACE, &b->front_face);
    if (s->polygon_mode) g->GetIntegerv(HW_POLYGON_MODE, b->polygon_mode);
    if (s->clip_control) {
        g->GetIntegerv(HW_CLIP_ORIGIN, &b->clip_origin);
        g->GetIntegerv(HW_CLIP_DEPTH_MODE, &b->clip_depth);
    }
    g->GetFloatv(GL_COLOR_CLEAR_VALUE, b->clear_color);
    g->GetFloatv(GL_BLEND_COLOR, b->blend_color);
    g->GetIntegerv(GL_BLEND_SRC_RGB, &b->blend_src_rgb);
    g->GetIntegerv(GL_BLEND_DST_RGB, &b->blend_dst_rgb);
    g->GetIntegerv(GL_BLEND_SRC_ALPHA, &b->blend_src_alpha);
    g->GetIntegerv(GL_BLEND_DST_ALPHA, &b->blend_dst_alpha);
    g->GetIntegerv(GL_BLEND_EQUATION_RGB, &b->blend_eq_rgb);
    g->GetIntegerv(GL_BLEND_EQUATION_ALPHA, &b->blend_eq_alpha);
    if (s->desktop) {
        s->get_double(GL_DEPTH_RANGE, b->depth_range);
        s->get_double(GL_DEPTH_CLEAR_VALUE, &b->clear_depth);
    } else {
        GLfloat range[2], depth;
        g->GetFloatv(GL_DEPTH_RANGE, range);
        g->GetFloatv(GL_DEPTH_CLEAR_VALUE, &depth);
        b->depth_range[0] = range[0]; b->depth_range[1] = range[1]; b->clear_depth = depth;
    }
    for (i = 0; i < s->n_caps; ++i) b->enables[i] = g->IsEnabled(s->caps[i]);
    for (i = 0; i < s->n_stores; ++i) g->GetIntegerv(s->stores[i], &b->stores[i]);
}
static void neutral_state(hw_gl *s)
{
    nj_gl_api *g = &s->gl;
    int i;
    if (s->pbo) {
        g->BindBuffer(HW_PIXEL_PACK_BUFFER, 0);
        g->BindBuffer(HW_PIXEL_UNPACK_BUFFER, 0);
    }
    for (i = 0; i < s->n_stores; ++i)
        g->PixelStorei(s->stores[i], s->stores[i] == GL_PACK_ALIGNMENT ||
                      s->stores[i] == GL_UNPACK_ALIGNMENT ? 1 : 0);
    if (s->bind_sampler) { s->bind_sampler(0, 0); s->bind_sampler(1, 0); }
    if (s->attrib_divisor) for (i = 0; i < HW_ATTRIBS; ++i) s->attrib_divisor((GLuint)i, 0);
    for (i = 0; i < s->n_caps; ++i) g->Disable(s->caps[i]);
    g->ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    g->BindBuffer(GL_ARRAY_BUFFER, 0);
    g->ActiveTexture(GL_TEXTURE0);
    if (s->polygon_mode) s->polygon_mode(GL_FRONT_AND_BACK, HW_FILL);
    if (s->clip_control) s->clip_control(HW_LOWER_LEFT, HW_NEGATIVE_ONE_TO_ONE);
}
static void restore_state(hw_gl *s)
{
    const hw_snapshot *b = &s->saved;
    nj_gl_api *g = &s->gl;
    int i;
    if (s->bind_vao) s->bind_vao(0);
    for (i = 0; i < HW_ATTRIBS; ++i) {
        const hw_attrib *a = &b->attrib[i];
        g->BindBuffer(GL_ARRAY_BUFFER, (GLuint)a->buffer);
        if (a->long_value) s->attrib_long((GLuint)i, a->size, (GLenum)a->type, a->stride, a->pointer);
        else if (a->integer) s->attrib_integer((GLuint)i, a->size, (GLenum)a->type, a->stride, a->pointer);
        else g->VertexAttribPointer((GLuint)i, a->size, (GLenum)a->type,
                                   (GLboolean)a->normalized, a->stride, a->pointer);
        if (s->attrib_divisor) s->attrib_divisor((GLuint)i, (GLuint)a->divisor);
        if (a->enabled) g->EnableVertexAttribArray((GLuint)i);
        else g->DisableVertexAttribArray((GLuint)i);
        if (i || !s->desktop) {
            if (a->long_value) s->set_attrib_double((GLuint)i, a->current_double);
            else if (a->integer) s->set_attrib_integer((GLuint)i, a->current_integer);
            else s->set_attrib_float((GLuint)i, a->current);
        }
    }
    if (s->bind_vertex) {
        for (i = 0; i < HW_ATTRIBS; ++i) {
            const hw_attrib *a = &b->attrib[i];
            s->bind_vertex((GLuint)i, (GLuint)a->binding_buffer,
                           (GLintptr)a->binding_offset, a->binding_stride);
            s->binding_divisor((GLuint)i, (GLuint)a->binding_divisor);
            if (a->long_value) s->attrib_lformat((GLuint)i, a->size, (GLenum)a->type, (GLuint)a->relative_offset);
            else if (a->integer) s->attrib_iformat((GLuint)i, a->size, (GLenum)a->type, (GLuint)a->relative_offset);
            else s->attrib_format((GLuint)i, a->size, (GLenum)a->type,
                                  (GLboolean)a->normalized, (GLuint)a->relative_offset);
            s->attrib_binding((GLuint)i, (GLuint)a->binding);
        }
    }
    if (s->bind_vao) s->bind_vao((GLuint)b->vao);
    g->BindBuffer(GL_ARRAY_BUFFER, (GLuint)b->array_buffer);
    if (s->pbo) {
        g->BindBuffer(HW_PIXEL_PACK_BUFFER, (GLuint)b->pack_buffer);
        g->BindBuffer(HW_PIXEL_UNPACK_BUFFER, (GLuint)b->unpack_buffer);
    }
    for (i = 0; i < 2; ++i) {
        g->ActiveTexture(GL_TEXTURE0+(GLenum)i);
        g->BindTexture(GL_TEXTURE_2D, (GLuint)b->textures[i]);
        if (s->bind_sampler) s->bind_sampler((GLuint)i, (GLuint)b->samplers[i]);
    }
    g->ActiveTexture((GLenum)b->active_texture);
    if (s->separate_fb) {
        g->BindFramebuffer(HW_DRAW_FRAMEBUFFER, (GLuint)b->draw_fb);
        g->BindFramebuffer(HW_READ_FRAMEBUFFER, (GLuint)b->read_fb);
    } else g->BindFramebuffer(GL_FRAMEBUFFER, (GLuint)b->draw_fb);
    g->BindRenderbuffer(GL_RENDERBUFFER, (GLuint)b->renderbuffer);
    g->UseProgram((GLuint)b->program);
    g->Viewport(b->viewport[0], b->viewport[1], b->viewport[2], b->viewport[3]);
    g->Scissor(b->scissor[0], b->scissor[1], b->scissor[2], b->scissor[3]);
    g->ColorMask(b->color_mask[0], b->color_mask[1], b->color_mask[2], b->color_mask[3]);
    g->DepthMask(b->depth_mask);
    g->DepthFunc((GLenum)b->depth_func);
    g->CullFace((GLenum)b->cull_face);
    g->FrontFace((GLenum)b->front_face);
    if (s->polygon_mode) {
        s->polygon_mode(GL_FRONT, (GLenum)b->polygon_mode[0]);
        s->polygon_mode(GL_BACK, (GLenum)b->polygon_mode[1]);
    }
    if (s->clip_control) s->clip_control((GLenum)b->clip_origin, (GLenum)b->clip_depth);
    g->ClearColor(b->clear_color[0], b->clear_color[1], b->clear_color[2], b->clear_color[3]);
    if (s->desktop) {
        g->DepthRange(b->depth_range[0], b->depth_range[1]); g->ClearDepth(b->clear_depth);
    } else {
        g->DepthRangef((GLfloat)b->depth_range[0], (GLfloat)b->depth_range[1]);
        g->ClearDepthf((GLfloat)b->clear_depth);
    }
    g->BlendColor(b->blend_color[0], b->blend_color[1], b->blend_color[2], b->blend_color[3]);
    g->BlendFuncSeparate((GLenum)b->blend_src_rgb, (GLenum)b->blend_dst_rgb,
                         (GLenum)b->blend_src_alpha, (GLenum)b->blend_dst_alpha);
    g->BlendEquationSeparate((GLenum)b->blend_eq_rgb, (GLenum)b->blend_eq_alpha);
    for (i = 0; i < s->n_stores; ++i) g->PixelStorei(s->stores[i], b->stores[i]);
    for (i = 0; i < s->n_caps; ++i) set_enabled(s, s->caps[i], b->enables[i]);
}
static int begin(void *ud)
{
    hw_gl *s = (hw_gl *)ud;
    if (!s || s->active || !valid(s)) return 0;
    /* GL has no error put-back operation. Require an error-free handoff and
     * explicitly report the one consumed foreign error instead of silently
     * letting the backend drain it. No state changes precede this check. */
    s->handoff_error = s->gl.GetError();
    if (s->handoff_error != GL_NO_ERROR) {
        (void)snprintf(s->error_info, sizeof s->error_info,
                       "hardware batch refused: consumed pending frontend GL error 0x%x",
                       (unsigned)s->handoff_error);
        (void)fprintf(stderr, "nj2me libretro GL: %s\n", s->error_info);
        return 0;
    }
    if (s->desktop) {
        static const GLenum scales[4] = {0x0D14, 0x0D18, 0x0D1A, 0x0D1C};
        GLboolean map_color = GL_FALSE;
        int i;
        /* Compatibility pixel transfers affect TexImage/ReadPixels. Refuse a
         * nondefault transfer pipeline instead of silently changing CPU pixels. */
        s->gl.GetBooleanv(0x0D10 /* GL_MAP_COLOR */, &map_color);
        if (map_color) return 0;
        for (i = 0; i < 4; ++i) {
            GLfloat scale, bias;
            s->gl.GetFloatv(scales[i], &scale);
            s->gl.GetFloatv(scales[i]+1u, &bias);
            if (scale != 1.0f || bias != 0.0f) return 0;
        }
    }
    /* Drawing into a frontend's active transform-feedback transaction would
     * alter its buffers/counters even if all bindings were restored. */
    if (s->transform_feedback) {
        GLboolean active = GL_FALSE, paused = GL_FALSE;
        s->gl.GetBooleanv(HW_TRANSFORM_FEEDBACK_ACTIVE, &active);
        s->gl.GetBooleanv(HW_TRANSFORM_FEEDBACK_PAUSED, &paused);
        if (active && !paused) return 0;
    }
    save_state(s);
    neutral_state(s);
    s->active = 1;
    return 1;
}
static int end(void *ud)
{
    hw_gl *s = (hw_gl *)ud;
    int ok;
    if (!s || !s->active) return 0;
    ok = valid(s);
    if (ok) restore_state(s);
    s->active = 0;
    return ok;
}
static int fill_rect(void *ud, const nj_gfx *g, int x, int y, int w, int h)
{
    hw_gl *s = (hw_gl *)ud;
    if (!backend_current(s)) return NJ_GFX_ACCEL_DECLINE;
    return nj_gles2_backend()->fill_rect(s->backend, g, x, y, w, h);
}
static int blit(void *ud, const nj_gfx *g, const nj_gfx_blit *b)
{
    hw_gl *s = (hw_gl *)ud;
    if (!backend_current(s)) return NJ_GFX_ACCEL_DECLINE;
    return nj_gles2_backend()->blit(s->backend, g, b);
}
static int raster(void *ud, const nj_gfx_raster *p, nj_u4 *drawn)
{
    hw_gl *s = (hw_gl *)ud;
    if (!backend_current(s)) return NJ_GFX_ACCEL_DECLINE;
    return nj_gles2_raster(s->backend, p, drawn);
}
static int triangles(void *ud, const nj_gfx_triangles *p, nj_u4 *submitted)
{
    hw_gl *s = (hw_gl *)ud;
    if (!backend_current(s)) return NJ_GFX_ACCEL_DECLINE;
    return nj_gles2_triangles(s->backend, p, submitted);
}
static int triangles_list(void *ud, const nj_gfx_triangles_list *list,
                          nj_u4 *submitted)
{
    hw_gl *s = (hw_gl *)ud;
    if (!backend_current(s)) return NJ_GFX_ACCEL_DECLINE;
    return nj_gles2_triangles_list(s->backend, list, submitted);
}
static GLuint compile_shader(hw_gl *s, GLenum type, const char *text)
{
    GLuint shader = s->gl.CreateShader(type);
    GLint ok = 0;
    const char *parts[2];
    if (!shader) return 0;
    parts[0] = s->desktop ? "#version 120\n" : "";
    parts[1] = text;
    s->gl.ShaderSource(shader, 2, parts, NULL);
    s->gl.CompileShader(shader);
    s->gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) { s->gl.DeleteShader(shader); return 0; }
    return shader;
}
static int presentation_create(hw_gl *s)
{
    static const char vs[] =
        "attribute vec2 position; varying vec2 uv; void main(){"
        "gl_Position=vec4(position,0.0,1.0);"
        "uv=vec2(position.x*0.5+0.5,0.5-position.y*0.5);}";
    static const char fs[] =
        "#ifdef GL_ES\nprecision mediump float;\n#endif\n"
        "varying vec2 uv; uniform sampler2D image;"
        "void main(){gl_FragColor=texture2D(image,uv);}";
    GLuint v = compile_shader(s, GL_VERTEX_SHADER, vs);
    GLuint f = compile_shader(s, GL_FRAGMENT_SHADER, fs);
    GLint ok = 0;
    if (v && f) {
        s->program = s->gl.CreateProgram();
        s->gl.AttachShader(s->program, v); s->gl.AttachShader(s->program, f);
        s->gl.BindAttribLocation(s->program, 0, "position");
        s->gl.LinkProgram(s->program);
        s->gl.GetProgramiv(s->program, GL_LINK_STATUS, &ok);
    }
    if (v) s->gl.DeleteShader(v);
    if (f) s->gl.DeleteShader(f);
    if (!ok) return 0;
    s->sampler = s->gl.GetUniformLocation(s->program, "image");
    s->gl.GenTextures(1, &s->texture);
    return s->sampler >= 0 && s->texture != 0;
}
static int present(void *ud, uintptr_t framebuffer, const nj_surface *surface)
{
    static const GLfloat quad[8] = {-1,-1, 1,-1, -1,1, 1,1};
    hw_gl *s = (hw_gl *)ud;
    size_t bytes;
    int x, y, bpp;
    if (!s || !s->active || !valid(s) || !surface || !surface->pixels ||
        framebuffer > UINT_MAX || surface->width <= 0 || surface->height <= 0 ||
        surface->width > s->max_texture || surface->height > s->max_texture ||
        surface->width > s->max_viewport[0] || surface->height > s->max_viewport[1]) return 0;
    switch (surface->format) {
    case NJ_PIXFMT_ARGB8888: bpp = 4; break;
    case NJ_PIXFMT_RGB888: bpp = 3; break;
    case NJ_PIXFMT_RGB565: bpp = 2; break;
    default: return 0;
    }
    if ((nj_s8)surface->stride < (nj_s8)surface->width*bpp ||
        (size_t)surface->width > SIZE_MAX/4u/(size_t)surface->height ||
        (size_t)(surface->height-1) > (SIZE_MAX-(size_t)surface->width*(size_t)bpp)/(size_t)surface->stride)
        return 0;
    bytes = (size_t)surface->width*(size_t)surface->height*4u;
    if (s->upload_size < bytes) {
        unsigned char *p = (unsigned char *)realloc(s->upload, bytes);
        if (!p) return 0;
        s->upload = p; s->upload_size = bytes;
    }
    for (y = 0; y < surface->height; ++y) for (x = 0; x < surface->width; ++x) {
        nj_u4 c = nj_surface_get(surface, x, y);
        unsigned char *p = s->upload+((size_t)y*(size_t)surface->width+(size_t)x)*4u;
        p[0] = (unsigned char)(c>>16); p[1] = (unsigned char)(c>>8);
        p[2] = (unsigned char)c; p[3] = 255;
    }
    /* The backend may have left depth/blend enabled. Reset only our known
     * state; all frontend values are still in the one batch snapshot. */
    neutral_state(s);
    s->gl.BindFramebuffer(GL_FRAMEBUFFER, (GLuint)framebuffer);
    if (s->gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 0;
    s->gl.Viewport(0, 0, surface->width, surface->height);
    s->gl.ActiveTexture(GL_TEXTURE0);
    s->gl.BindTexture(GL_TEXTURE_2D, s->texture);
    s->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    s->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    s->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    s->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    s->gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, surface->width, surface->height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, s->upload);
    s->gl.UseProgram(s->program);
    s->gl.Uniform1i(s->sampler, 0);
    s->gl.EnableVertexAttribArray(0);
    s->gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    s->gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    s->gl.DisableVertexAttribArray(0);
    s->gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    /* This batch had an error-free frontend handoff. Upload/draw failures are
     * ours, and must not advertise a successfully presented hardware frame. */
    return valid(s) && s->gl.GetError() == GL_NO_ERROR;
}
static const char *info(void *ud)
{
    hw_gl *s = (hw_gl *)ud;
    return s ? (s->handoff_error ? s->error_info : s->description) : "";
}
static void add_cap(hw_gl *s, GLenum cap) { if (s->n_caps < HW_CAPS) s->caps[s->n_caps++] = cap; }
static void add_store(hw_gl *s, GLenum pname) { if (s->n_stores < HW_STORES) s->stores[s->n_stores++] = pname; }

static void *create(const nj_libretro_hw_config *config)
{
    hw_gl *s;
    nj_gles2_config backend_config;
    const char *version, *extensions, *renderer;
    unsigned major = 0, minor = 0;
    int desktop;
    /* GLSL 1.20 + client arrays need desktop compatibility, not a core profile.
     * ES3 retains ES2 shaders/client arrays with VAO 0. */
    if (!config || !config->get_proc || !config->is_current ||
        !config->is_current(config->ud) || config->context_type == 3) return NULL;
    desktop = config->context_type == 1;
    if (!desktop && config->context_type != 2 && config->context_type != 4 && config->context_type != 5) return NULL;
    s = (hw_gl *)calloc(1, sizeof *s);
    if (!s) return NULL;
    s->config = *config; s->desktop = desktop;
    if (!nj_gl_api_load(&s->gl, resolve, s, desktop)) goto fail;
    version = (const char *)s->gl.GetString(GL_VERSION);
    if (!version) goto fail;
    if (desktop) (void)sscanf(version, "%u.%u", &major, &minor);
    else (void)sscanf(version, "OpenGL ES %u.%u", &major, &minor);
    if ((desktop && (major < 2 || (major == 2 && minor < 1))) || (!desktop && major < 2)) goto fail;
    s->es3 = !desktop && major >= 3;
    if (desktop && (major > 3 || (major == 3 && minor >= 2))) {
        GLint profile = 0;
        s->gl.GetIntegerv(0x9126 /* GL_CONTEXT_PROFILE_MASK */, &profile);
        if (profile & 1 /* GL_CONTEXT_CORE_PROFILE_BIT */) goto fail;
    }
    extensions = (const char *)s->gl.GetString(GL_EXTENSIONS);
    HW_LOAD(get_attrib_float, "glGetVertexAttribfv");
    HW_LOAD(set_attrib_float, "glVertexAttrib4fv");
    if (!s->get_attrib_float || !s->set_attrib_float) goto fail;
    if (desktop) {
        HW_LOAD(get_double, "glGetDoublev"); HW_LOAD(polygon_mode, "glPolygonMode");
        if (!s->get_double || !s->polygon_mode) goto fail;
        if (major > 4 || (major == 4 && minor >= 5) || has_extension(extensions, "GL_ARB_clip_control")) {
            HW_LOAD(clip_control, "glClipControl"); if (!s->clip_control) goto fail;
        }
    }
    s->separate_fb = s->es3 || (desktop && (major >= 3 ||
        has_extension(extensions, "GL_ARB_framebuffer_object") || has_extension(extensions, "GL_EXT_framebuffer_blit")));
    s->pbo = s->es3 || desktop || has_extension(extensions, "GL_NV_pixel_buffer_object");
    s->integer_attrib = s->es3 || (desktop && (major >= 3 ||
                        has_extension(extensions, "GL_EXT_gpu_shader4")));
    if (s->integer_attrib) {
        HW_LOAD(attrib_integer, "glVertexAttribIPointer");
        HW_LOAD(get_attrib_integer, "glGetVertexAttribIiv");
        HW_LOAD(set_attrib_integer, "glVertexAttribI4iv");
        if (!s->attrib_integer) HW_LOAD(attrib_integer, "glVertexAttribIPointerEXT");
        if (!s->get_attrib_integer) HW_LOAD(get_attrib_integer, "glGetVertexAttribIivEXT");
        if (!s->set_attrib_integer) HW_LOAD(set_attrib_integer, "glVertexAttribI4ivEXT");
        if (!s->attrib_integer || !s->get_attrib_integer || !s->set_attrib_integer) goto fail;
    }
    if (s->es3 || (desktop && (major > 3 || (major == 3 && minor >= 3) ||
                             has_extension(extensions, "GL_ARB_instanced_arrays")))) {
        HW_LOAD(attrib_divisor, "glVertexAttribDivisor"); if (!s->attrib_divisor) goto fail;
    } else if (has_extension(extensions, "GL_ANGLE_instanced_arrays")) {
        HW_LOAD(attrib_divisor, "glVertexAttribDivisorANGLE"); if (!s->attrib_divisor) goto fail;
    } else if (has_extension(extensions, "GL_EXT_instanced_arrays")) {
        HW_LOAD(attrib_divisor, "glVertexAttribDivisorEXT"); if (!s->attrib_divisor) goto fail;
    }
    if (desktop && (major > 4 || (major == 4 && minor >= 1) ||
                    has_extension(extensions, "GL_ARB_vertex_attrib_64bit"))) {
        HW_LOAD(attrib_long, "glVertexAttribLPointer");
        HW_LOAD(get_attrib_double, "glGetVertexAttribLdv");
        HW_LOAD(set_attrib_double, "glVertexAttribL4dv");
        if (!s->attrib_long || !s->get_attrib_double || !s->set_attrib_double) goto fail;
    }
    /* ES3.1's separate-binding setters reject VAO 0. Only desktop compatibility
     * can have this model in the default VAO we touch; nonzero frontend VAOs
     * are never modified. Keep the ES default VAO on its client-array API. */
    if (desktop && (major > 4 || (major == 4 && minor >= 3) ||
                    has_extension(extensions, "GL_ARB_vertex_attrib_binding"))) {
        HW_LOAD(bind_vertex, "glBindVertexBuffer");
        HW_LOAD(attrib_format, "glVertexAttribFormat");
        HW_LOAD(attrib_iformat, "glVertexAttribIFormat");
        HW_LOAD(attrib_binding, "glVertexAttribBinding");
        HW_LOAD(binding_divisor, "glVertexBindingDivisor");
        HW_LOAD(get_indexed, "glGetIntegeri_v");
        HW_LOAD(get_indexed64, "glGetInteger64i_v");
        if (!s->bind_vertex || !s->attrib_format || !s->attrib_iformat ||
            !s->attrib_binding || !s->binding_divisor || !s->get_indexed || !s->get_indexed64) goto fail;
        if (s->attrib_long) { HW_LOAD(attrib_lformat, "glVertexAttribLFormat"); if (!s->attrib_lformat) goto fail; }
    }
    if (s->es3 || (desktop && (major >= 3 || has_extension(extensions, "GL_ARB_vertex_array_object")))) {
        HW_LOAD(bind_vao, "glBindVertexArray"); if (!s->bind_vao) goto fail;
    } else if (has_extension(extensions, "GL_OES_vertex_array_object")) {
        HW_LOAD(bind_vao, "glBindVertexArrayOES"); if (!s->bind_vao) goto fail;
    }
    if (s->es3 || (desktop && (major > 3 || (major == 3 && minor >= 3) ||
                             has_extension(extensions, "GL_ARB_sampler_objects")))) {
        HW_LOAD(bind_sampler, "glBindSampler"); if (!s->bind_sampler) goto fail;
    }
    if (has_extension(extensions, "GL_KHR_robustness")) HW_LOAD(reset, "glGetGraphicsResetStatusKHR");
    else if (has_extension(extensions, "GL_ARB_robustness")) HW_LOAD(reset, "glGetGraphicsResetStatusARB");
    else if (has_extension(extensions, "GL_EXT_robustness")) HW_LOAD(reset, "glGetGraphicsResetStatusEXT");
    add_cap(s, GL_BLEND); add_cap(s, GL_DITHER); add_cap(s, GL_SCISSOR_TEST);
    add_cap(s, GL_DEPTH_TEST); add_cap(s, GL_STENCIL_TEST); add_cap(s, GL_CULL_FACE);
    add_cap(s, GL_SAMPLE_ALPHA_TO_COVERAGE); add_cap(s, GL_SAMPLE_COVERAGE);
    add_cap(s, GL_POLYGON_OFFSET_FILL);
    if ((s->es3 && (major > 3 || minor >= 1)) || (desktop &&
        (major > 3 || (major == 3 && minor >= 2)))) add_cap(s, HW_SAMPLE_MASK);
    if (desktop && (major > 3 || (major == 3 && minor >= 2) ||
        has_extension(extensions, "GL_ARB_depth_clamp"))) add_cap(s, HW_DEPTH_CLAMP);
    if (s->es3 || (desktop && (major >= 3 || has_extension(extensions, "GL_EXT_transform_feedback"))))
        add_cap(s, HW_RASTERIZER_DISCARD);
    s->transform_feedback = s->es3 || (desktop && (major >= 4 || has_extension(extensions, "GL_ARB_transform_feedback2")));
    if (desktop) {
        GLint n = 0, i;
        add_cap(s, HW_COLOR_LOGIC_OP); add_cap(s, HW_ALPHA_TEST);
        add_cap(s, HW_POLYGON_SMOOTH); add_cap(s, HW_SAMPLE_ALPHA_TO_ONE);
        if (major >= 3 || has_extension(extensions, "GL_ARB_framebuffer_sRGB") ||
            has_extension(extensions, "GL_EXT_framebuffer_sRGB")) add_cap(s, HW_FRAMEBUFFER_SRGB);
        s->gl.GetIntegerv(HW_MAX_CLIP_PLANES, &n);
        if (n < 0 || n > HW_CAPS-s->n_caps) goto fail;
        for (i = 0; i < n; ++i) add_cap(s, HW_CLIP_PLANE0+(GLenum)i);
    }
    add_store(s, GL_PACK_ALIGNMENT); add_store(s, GL_UNPACK_ALIGNMENT);
    if (desktop || s->es3 || has_extension(extensions, "GL_EXT_unpack_subimage")) {
        add_store(s, HW_UNPACK_ROW_LENGTH); add_store(s, HW_UNPACK_SKIP_ROWS); add_store(s, HW_UNPACK_SKIP_PIXELS);
    }
    if (desktop || s->es3 || has_extension(extensions, "GL_NV_pack_subimage")) {
        add_store(s, HW_PACK_ROW_LENGTH); add_store(s, HW_PACK_SKIP_ROWS); add_store(s, HW_PACK_SKIP_PIXELS);
    }
    if (desktop || s->es3) { add_store(s, HW_UNPACK_IMAGE_HEIGHT); add_store(s, HW_UNPACK_SKIP_IMAGES); }
    if (desktop) {
        add_store(s, HW_PACK_IMAGE_HEIGHT); add_store(s, HW_PACK_SKIP_IMAGES);
        add_store(s, HW_PACK_SWAP_BYTES); add_store(s, HW_PACK_LSB_FIRST);
        add_store(s, HW_UNPACK_SWAP_BYTES); add_store(s, HW_UNPACK_LSB_FIRST);
    }
    s->gl.GetIntegerv(GL_MAX_TEXTURE_SIZE, &s->max_texture);
    s->gl.GetIntegerv(GL_MAX_VIEWPORT_DIMS, s->max_viewport);
    renderer = (const char *)s->gl.GetString(GL_RENDERER);
    (void)snprintf(s->description, sizeof s->description, "%s; %s", renderer ? renderer : "unknown", version);
    if (!begin(s)) goto fail;
    memset(&backend_config, 0, sizeof backend_config);
    backend_config.is_current = backend_current; backend_config.ud = s;
    backend_config.gl = &s->gl; backend_config.desktop_gl = desktop;
    s->backend = nj_gles2_create(&backend_config);
    if (s->backend && presentation_create(s) && end(s)) return s;
    /* Still inside the creation snapshot, so failure cleanup is isolated too. */
    if (valid(s)) {
        if (s->backend && !nj_gles2_destroy(s->backend)) nj_gles2_abandon(s->backend);
        if (s->program) s->gl.DeleteProgram(s->program);
        if (s->texture) s->gl.DeleteTextures(1, &s->texture);
    } else nj_gles2_abandon(s->backend);
    s->backend = NULL;
    if (s->active) (void)end(s);
fail:
    free(s->upload); free(s);
    return NULL;
}
static void destroy(void *ud, int lost)
{
    hw_gl *s = (hw_gl *)ud;
    if (!s) return;
    if (!lost && valid(s)) {
        if (s->active) (void)end(s);
        if (begin(s)) {
            if (!nj_gles2_destroy(s->backend)) nj_gles2_abandon(s->backend);
            s->backend = NULL;
            if (s->program) s->gl.DeleteProgram(s->program);
            if (s->texture) s->gl.DeleteTextures(1, &s->texture);
            (void)end(s);
        }
    }
    /* No GL on lost teardown, even if a batch was open when loss happened. */
    nj_gles2_abandon(s->backend);
    free(s->upload); free(s);
}
const nj_libretro_hw_driver *nj_libretro_hw_gl_driver(void)
{
    static const nj_libretro_hw_driver driver = {
        create, destroy, valid, begin, end,
        {NJ_GFX_ACCEL_VERSION, fill_rect, blit}, raster, triangles,
        triangles_list, present, info
    };
    return &driver;
}
