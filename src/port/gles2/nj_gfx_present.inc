/* Offscreen GLSL presentation pass (goal 7e37f74a item A1).
 *
 * Included at the end of nj_gfx_gles2.c so it can use the private struct and
 * the static helpers above.  The pass uploads a game frame as an RGBA
 * texture, renders it into an offscreen framebuffer through one of the
 * pluggable post-process shaders and reads the result back.  It never touches
 * the software framebuffer, and it is only run by a frontend that explicitly
 * asks for it (env-gated in the SDL2 port), so the default presentation path
 * is unchanged.
 *
 * Output convention: the quad is drawn without a V flip, so glReadPixels
 * returns top-down rows (framebuffer row 0 is the source top row).  Callers
 * get a tightly packed RGBA8888 buffer, out_w*out_h*4 bytes.
 */

static const char present_vs[] =
    "attribute vec2 position; attribute vec2 texcoord; varying highp vec2 uv;"
    "void main(){ uv=texcoord; gl_Position=vec4(position,0.0,1.0); }";

/* 0: nearest (exact texel replication for integer scales) */
static const char present_fs_nearest[] =
    "#ifdef GL_ES\nprecision highp float;\n#endif\n"
    "varying highp vec2 uv; uniform highp sampler2D image;"
    "void main(){ gl_FragColor=texture2D(image,uv); }";

/* 1: sharp-bilinear - nearest at texel centres, bilinear at texel edges */
static const char present_fs_sharp[] =
    "#ifdef GL_ES\nprecision highp float;\n#endif\n"
    "varying highp vec2 uv; uniform highp sampler2D image; uniform vec2 texel;"
    "void main(){ vec2 size=1.0/texel; vec2 f=fract(uv*size);"
    " vec2 centre=(floor(uv*size)+0.5)*texel;"
    " vec4 near=texture2D(image,centre); vec4 lin=texture2D(image,uv);"
    " vec2 w=clamp(abs(f-0.5)*2.0,0.0,1.0);"
    " gl_FragColor=mix(near,lin,max(w.x,w.y)); }";

/* 2: scanline (nearest + every other output row darkened) */
static const char present_fs_scanline[] =
    "#ifdef GL_ES\nprecision highp float;\n#endif\n"
    "varying highp vec2 uv; uniform highp sampler2D image;"
    "void main(){ vec4 c=texture2D(image,uv);"
    " if(mod(floor(gl_FragCoord.y),2.0)>0.5) c.rgb*=0.62;"
    " gl_FragColor=c; }";

/* 3: LCD grid (nearest + every third row/column darkened) */
static const char present_fs_lcd[] =
    "#ifdef GL_ES\nprecision highp float;\n#endif\n"
    "varying highp vec2 uv; uniform highp sampler2D image;"
    "void main(){ vec4 c=texture2D(image,uv);"
    " if(mod(floor(gl_FragCoord.x),3.0)<0.5 || mod(floor(gl_FragCoord.y),3.0)<0.5) c.rgb*=0.80;"
    " gl_FragColor=c; }";

/* 4: CRT (strong scanlines + column grid + gentle vignette) */
static const char present_fs_crt[] =
    "#ifdef GL_ES\nprecision highp float;\n#endif\n"
    "varying highp vec2 uv; uniform highp sampler2D image; uniform vec2 outsize;"
    "void main(){ vec4 c=texture2D(image,uv);"
    " if(mod(floor(gl_FragCoord.y),2.0)>0.5) c.rgb*=0.58;"
    " if(mod(floor(gl_FragCoord.x),3.0)<0.5) c.rgb*=0.82;"
    " vec2 q=gl_FragCoord.xy/outsize-0.5; c.rgb*=1.0-0.30*dot(q,q);"
    " gl_FragColor=c; }";

/* 5: FSR-lite - sharp-bilinear base + RCAS-style local contrast sharpen.
 * Full FSR1 EASU is not implemented yet; this mode exists so the pipeline,
 * the --upscale switch and the parity tooling are ready for it. */
static const char present_fs_fsr[] =
    "#ifdef GL_ES\nprecision highp float;\n#endif\n"
    "varying highp vec2 uv; uniform highp sampler2D image; uniform vec2 texel;"
    "void main(){ vec2 size=1.0/texel; vec2 f=fract(uv*size);"
    " vec2 centre=(floor(uv*size)+0.5)*texel;"
    " vec4 near=texture2D(image,centre); vec4 lin=texture2D(image,uv);"
    " vec2 w=clamp(abs(f-0.5)*2.0,0.0,1.0);"
    " vec4 base=mix(near,lin,max(w.x,w.y));"
    " vec4 blur=(texture2D(image,uv+vec2(texel.x,0.0))+"
    "            texture2D(image,uv-vec2(texel.x,0.0))+"
    "            texture2D(image,uv+vec2(0.0,texel.y))+"
    "            texture2D(image,uv-vec2(0.0,texel.y)))*0.25;"
    " base.rgb=clamp(base.rgb*1.65-blur.rgb*0.65,0.0,1.0);"
    " gl_FragColor=base; }";

static void present_delete(nj_gles2 *s)
{
    int i;
    for (i = 0; i < NJ_GL_PRESENT_MODES; ++i) {
        if (s->present_program[i]) s->gl.DeleteProgram(s->present_program[i]);
        s->present_program[i] = 0;
        s->present_sampler[i] = -1;
        s->present_texel[i] = -1;
        s->present_outsize[i] = -1;
    }
    if (s->present_input) s->gl.DeleteTextures(1, &s->present_input);
    if (s->present_target) s->gl.DeleteTextures(1, &s->present_target);
    if (s->present_fbo) s->gl.DeleteFramebuffers(1, &s->present_fbo);
    s->present_input = s->present_target = s->present_fbo = 0;
    memset(&s->present_input_storage, 0, sizeof s->present_input_storage);
    memset(&s->present_target_storage, 0, sizeof s->present_target_storage);
    s->present_ready = 0;
}

static int present_program(nj_gles2 *s, int mode, const char *fragment)
{
    GLuint v = 0, f = 0, p = 0;
    GLint ok = 0;
    v = shader(s, GL_VERTEX_SHADER, present_vs);
    f = shader(s, GL_FRAGMENT_SHADER, fragment);
    if (!v || !f) goto fail;
    p = s->gl.CreateProgram();
    if (!p) goto fail;
    s->gl.AttachShader(p, v); s->gl.AttachShader(p, f);
    s->gl.BindAttribLocation(p, 0, "position");
    s->gl.BindAttribLocation(p, 1, "texcoord");
    s->gl.LinkProgram(p);
    s->gl.GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) goto fail;
    s->present_sampler[mode] = s->gl.GetUniformLocation(p, "image");
    s->present_texel[mode] = s->gl.GetUniformLocation(p, "texel");
    s->present_outsize[mode] = s->gl.GetUniformLocation(p, "outsize");
    if (s->present_sampler[mode] < 0) goto fail;
    s->gl.DeleteShader(v); s->gl.DeleteShader(f);
    s->present_program[mode] = p;
    return 1;
fail:
    if (current(s)) {
        if (v) s->gl.DeleteShader(v);
        if (f) s->gl.DeleteShader(f);
        if (p) s->gl.DeleteProgram(p);
        (void)clean_errors(s);
    }
    return 0;
}

static int present_init(nj_gles2 *s)
{
    static const char *const fragments[NJ_GL_PRESENT_MODES] = {
        present_fs_nearest, present_fs_sharp, present_fs_scanline,
        present_fs_lcd, present_fs_crt, present_fs_fsr
    };
    int i;
    if (s->present_ready) return 1;
    if (!current(s)) return 0;
    if (s->present_program[0] || s->present_input || s->present_target || s->present_fbo) {
        present_delete(s);
        if (!current(s) || !clean_errors(s)) return 0;
    }
    for (i = 0; i < NJ_GL_PRESENT_MODES; ++i) {
        if (!present_program(s, i, fragments[i])) goto fail;
    }
    s->gl.GenTextures(1, &s->present_input);
    s->gl.GenTextures(1, &s->present_target);
    s->gl.GenFramebuffers(1, &s->present_fbo);
    if (!s->present_input || !s->present_target || !s->present_fbo || !clean_errors(s)) goto fail;
    s->present_ready = 1;
    return 1;
fail:
    if (current(s)) {
        present_delete(s);
        (void)clean_errors(s);
    }
    return 0;
}

/* Render one frame through the offscreen shader pass.  Returns 1 on success,
 * 0 when the GL context is not current or the pass failed (the caller then
 * falls back to the software presentation unchanged).  Same surface formats
 * as the rest of the backend are accepted; non-RGBA sources are converted on
 * the CPU. */
int nj_gles2_present(void *ud, const nj_surface *src, int mode, int out_w, int out_h,
                     nj_u1 *out)
{
    static const GLfloat quad[8] = {-1,-1, 1,-1, -1,1, 1,1};
    static const GLfloat uv[8] = {0,0, 1,0, 0,1, 1,1};
    nj_gles2 *s = (nj_gles2 *)ud;
    size_t in_bytes;
    int x, y;
    if (!s || !src || !src->pixels || !out || mode < 0 || mode >= NJ_GL_PRESENT_MODES)
        return 0;
    if (!current(s) || !present_init(s)) return 0;
    if (out_w <= 0 || out_h <= 0 || out_w > s->max_viewport[0] || out_h > s->max_viewport[1] ||
        !dimensions(s, out_w, out_h)) return 0;
    if (!dimensions(s, src->width, src->height)) return 0;
    if (src->format != NJ_PIXFMT_RGB565 && src->format != NJ_PIXFMT_RGB888 &&
        src->format != NJ_PIXFMT_ARGB8888) return 0;
    in_bytes = (size_t)src->width * (size_t)src->height * 4u;
    if (!reserve(&s->upload, &s->upload_size, in_bytes)) return 0;
    for (y = 0; y < src->height; ++y) {
        nj_u1 *dst = s->upload + (size_t)y * (size_t)src->width * 4u;
        for (x = 0; x < src->width; ++x) {
            nj_u4 c = nj_surface_get(src, x, y);
            dst[x*4+0] = (nj_u1)(c >> 16);
            dst[x*4+1] = (nj_u1)(c >> 8);
            dst[x*4+2] = (nj_u1)c;
            dst[x*4+3] = 255;
        }
    }
    s->gl.ActiveTexture(GL_TEXTURE0);
    texture(s, s->present_input);
    if (!texture_upload(s, &s->present_input_storage, src->width, src->height, GL_RGBA, s->upload))
        return 0;
    texture(s, s->present_target);
    if (!texture_upload(s, &s->present_target_storage, out_w, out_h, GL_RGBA, NULL))
        return 0;
    s->gl.BindFramebuffer(GL_FRAMEBUFFER, s->present_fbo);
    s->gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               s->present_target, 0);
    if (s->gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 0;
    s->gl.Viewport(0, 0, out_w, out_h);
    s->gl.UseProgram(s->present_program[mode]);
    s->gl.ActiveTexture(GL_TEXTURE0);
    s->gl.BindTexture(GL_TEXTURE_2D, s->present_input);
    s->gl.Uniform1i(s->present_sampler[mode], 0);
    if (s->present_texel[mode] >= 0)
        s->gl.Uniform2f(s->present_texel[mode], 1.0f / (float)src->width, 1.0f / (float)src->height);
    if (s->present_outsize[mode] >= 0)
        s->gl.Uniform2f(s->present_outsize[mode], (float)out_w, (float)out_h);
    s->gl.EnableVertexAttribArray(0); s->gl.EnableVertexAttribArray(1);
    s->gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    s->gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uv);
    s->gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    s->gl.DisableVertexAttribArray(0); s->gl.DisableVertexAttribArray(1);
    s->gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    s->gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    s->gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    s->gl.ReadPixels(0, 0, out_w, out_h, GL_RGBA, GL_UNSIGNED_BYTE, out);
    s->gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    return clean_errors(s);
}
