/* Shared GLES2 presentation filters, included by nj_gfx_gles2.c. The input
 * texture starts with the game's top row. Offscreen readback returns top-down
 * RGBA8888. The final window draw flips V and its raw capture is bottom-up.
 * Presentation never writes to the game's framebuffer. */

static const char present_vs[] =
    "attribute vec2 position; attribute vec2 texcoord; varying highp vec2 uv;"
    "void main(){ uv=texcoord; gl_Position=vec4(position,0.0,1.0); }";

/* The input texture carries B,G,R,A bytes even on GLES2 without BGRA
 * extensions. Swizzle at the first sample so all filters see opaque RGB.
 * RCAS samples the already-decoded EASU render target instead. */
#define PRESENT_FRAME_SAMPLE "#define frameSample(p) vec4(texture2D(image,(p)).bgr,1.0)\n"

/* 0: nearest (exact texel replication for integer scales) */
static const char present_fs_nearest[] =
    PRESENT_FRAME_SAMPLE
    "#ifdef GL_ES\nprecision highp float;\n#endif\n"
    "varying highp vec2 uv; uniform highp sampler2D image;"
    "void main(){ gl_FragColor=frameSample(uv); }";

/* 1: sharp-bilinear - nearest at texel centres, bilinear at texel edges */
static const char present_fs_sharp[] =
    PRESENT_FRAME_SAMPLE
    "#ifdef GL_ES\nprecision highp float;\n#endif\n"
    "varying highp vec2 uv; uniform highp sampler2D image; uniform vec2 texel;"
    "void main(){ vec2 size=1.0/texel; vec2 f=fract(uv*size);"
    " vec2 centre=(floor(uv*size)+0.5)*texel;"
    " vec4 near=frameSample(centre); vec4 lin=frameSample(uv);"
    " vec2 w=clamp(abs(f-0.5)*2.0,0.0,1.0);"
    " gl_FragColor=mix(near,lin,max(w.x,w.y)); }";

/* 2: scanline (nearest + every other output row darkened) */
static const char present_fs_scanline[] =
    PRESENT_FRAME_SAMPLE
    "#ifdef GL_ES\nprecision highp float;\n#endif\n"
    "varying highp vec2 uv; uniform highp sampler2D image;"
    "void main(){ vec4 c=frameSample(uv);"
    " if(mod(floor(gl_FragCoord.y),2.0)>0.5) c.rgb*=0.62;"
    " gl_FragColor=c; }";

/* 3: LCD grid (nearest + every third row/column darkened) */
static const char present_fs_lcd[] =
    PRESENT_FRAME_SAMPLE
    "#ifdef GL_ES\nprecision highp float;\n#endif\n"
    "varying highp vec2 uv; uniform highp sampler2D image;"
    "void main(){ vec4 c=frameSample(uv);"
    " if(mod(floor(gl_FragCoord.x),3.0)<0.5 || mod(floor(gl_FragCoord.y),3.0)<0.5) c.rgb*=0.80;"
    " gl_FragColor=c; }";

/* 4: CRT (strong scanlines + column grid + gentle vignette) */
static const char present_fs_crt[] =
    PRESENT_FRAME_SAMPLE
    "#ifdef GL_ES\nprecision highp float;\n#endif\n"
    "varying highp vec2 uv; uniform highp sampler2D image; uniform vec2 outsize;"
    "void main(){ vec4 c=frameSample(uv);"
    " if(mod(floor(gl_FragCoord.y),2.0)>0.5) c.rgb*=0.58;"
    " if(mod(floor(gl_FragCoord.x),3.0)<0.5) c.rgb*=0.82;"
    " vec2 q=gl_FragCoord.xy/outsize-0.5; c.rgb*=1.0-0.30*dot(q,q);"
    " gl_FragColor=c; }";

/* 6: Catmull-Rom bicubic. */
static const char present_fs_bicubic[] =
    PRESENT_FRAME_SAMPLE
    "#ifdef GL_ES\nprecision highp float;\n#endif\n"
    "uniform sampler2D image;\n"
    "uniform vec2 texel;\n"
    "varying vec2 uv;\n"
    "void main() {\n"
    "  vec2 p = uv / texel - 0.5;\n"
    "  vec2 f = fract(p);\n"
    "  vec2 base = floor(p);\n"
    "  vec2 f2 = f * f;\n"
    "  vec2 f3 = f2 * f;\n"
    "  vec2 w0 = -0.5 * f3 + 1.0 * f2 - 0.5 * f;\n"
    "  vec2 w1 =  1.5 * f3 - 2.5 * f2 + 1.0;\n"
    "  vec2 w2 = -1.5 * f3 + 2.0 * f2 + 0.5 * f;\n"
    "  vec2 w3 =  0.5 * f3 - 0.5 * f2;\n"
    "  vec4 acc = vec4(0.0);\n"
    "  for (int j = 0; j < 4; ++j) {\n"
    "    float wy = (j == 0) ? w0.y : ((j == 1) ? w1.y : ((j == 2) ? w2.y : w3.y));\n"
    "    for (int i = 0; i < 4; ++i) {\n"
    "      float wx = (i == 0) ? w0.x : ((i == 1) ? w1.x : ((i == 2) ? w2.x : w3.x));\n"
    "      vec2 o = vec2(float(i) - 1.0, float(j) - 1.0);\n"
    "      acc += frameSample((base + o + 0.5) * texel) * (wx * wy);\n"
    "    }\n"
    "  }\n"
    "  gl_FragColor = vec4(acc.rgb, 1.0);\n"
    "}\n";

/* FSR1 EASU. Fetch the reference stencil directly by source texel offset.
 * After the MIT-licensed AMD FidelityFX FSR1 reference (ffx_fsr1.h).
 * GLES2 has no textureGather, and its row-major emulation would not have
 * the (01,11,10,00) lane order expected by the reference. */
static const char present_fs_easu[] =
    PRESENT_FRAME_SAMPLE
    "#ifdef GL_ES\nprecision highp float;\n#endif\n"
    "varying highp vec2 uv;\n"
    "uniform highp sampler2D image;\n"
    "uniform vec2 insize;\n"
    "float luma(vec3 c) { return dot(c, vec3(0.5, 1.0, 0.5)); }\n"
    "void easu_tap(inout vec3 aC, inout float aW, vec2 off, vec2 dir,\n"
    "              vec2 len2, float lob, float clp, vec3 c)\n"
    "{\n"
    "    vec2 v;\n"
    "    v.x = off.x * dir.x + off.y * dir.y;\n"
    "    v.y = off.x * (-dir.y) + off.y * dir.x;\n"
    "    v *= len2;\n"
    "    float d2 = min(v.x * v.x + v.y * v.y, clp);\n"
    "    float wB = (2.0 / 5.0) * d2 - 1.0;\n"
    "    float wA = lob * d2 - 1.0;\n"
    "    wB *= wB;\n"
    "    wA *= wA;\n"
    "    wB = (25.0 / 16.0) * wB - ((25.0 / 16.0) - 1.0);\n"
    "    float w = wB * wA;\n"
    "    aC += c * w;\n"
    "    aW += w;\n"
    "}\n"
    "\n"
    "void easu_set(inout vec2 dir, inout float len, vec2 pp, int which,\n"
    "              float lA, float lB, float lC, float lD, float lE)\n"
    "{\n"
    "    float w = 0.0;\n"
    "    if (which == 0) w = (1.0 - pp.x) * (1.0 - pp.y);\n"
    "    else if (which == 1) w = pp.x * (1.0 - pp.y);\n"
    "    else if (which == 2) w = (1.0 - pp.x) * pp.y;\n"
    "    else w = pp.x * pp.y;\n"
    "    float dc = lD - lC;\n"
    "    float cb = lC - lB;\n"
    "    float lenX = max(abs(dc), abs(cb));\n"
    "    lenX = 1.0 / max(lenX, 1.0 / 32768.0);\n"
    "    float dirX = lD - lB;\n"
    "    dir.x += dirX * w;\n"
    "    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);\n"
    "    lenX *= lenX;\n"
    "    len += lenX * w;\n"
    "    float ec = lE - lC;\n"
    "    float ca = lC - lA;\n"
    "    float lenY = max(abs(ec), abs(ca));\n"
    "    lenY = 1.0 / max(lenY, 1.0 / 32768.0);\n"
    "    float dirY = lE - lA;\n"
    "    dir.y += dirY * w;\n"
    "    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);\n"
    "    lenY *= lenY;\n"
    "    len += lenY * w;\n"
    "}\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec2 pos = uv * insize - 0.5;\n"
    "    vec2 pp = fract(pos);\n"
    "    vec2 base = floor(pos) + 0.5;\n"
    "#define TAP(x,y) frameSample((base + vec2(x,y)) / insize).rgb\n"
    "    vec3 b = TAP(0.,-1.), c = TAP(1.,-1.);\n"
    "    vec3 e = TAP(-1.,0.), f = TAP(0.,0.), g = TAP(1.,0.), h = TAP(2.,0.);\n"
    "    vec3 i = TAP(-1.,1.), j = TAP(0.,1.), k = TAP(1.,1.), l = TAP(2.,1.);\n"
    "    vec3 n = TAP(0.,2.), o = TAP(1.,2.);\n"
    "    vec2 dir = vec2(0.0);\n"
    "    float len = 0.0;\n"
    "    easu_set(dir, len, pp, 0, luma(b), luma(e), luma(f), luma(g), luma(j));\n"
    "    easu_set(dir, len, pp, 1, luma(c), luma(f), luma(g), luma(h), luma(k));\n"
    "    easu_set(dir, len, pp, 2, luma(f), luma(i), luma(j), luma(k), luma(n));\n"
    "    easu_set(dir, len, pp, 3, luma(g), luma(j), luma(k), luma(l), luma(o));\n"
    "    vec2 dir2 = dir * dir;\n"
    "    float dirR = dir2.x + dir2.y;\n"
    "    bool zro = dirR < (1.0 / 32768.0);\n"
    "    dirR = inversesqrt(max(dirR, 1.0 / 32768.0));\n"
    "    dirR = zro ? 1.0 : dirR;\n"
    "    dir.x = zro ? 1.0 : dir.x;\n"
    "    dir *= dirR;\n"
    "    len = len * 0.5;\n"
    "    len *= len;\n"
    "    float stretch = (dir.x * dir.x + dir.y * dir.y) / max(abs(dir.x), abs(dir.y));\n"
    "    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 + (-0.5) * len);\n"
    "    float lob = 0.5 + ((0.25 - 0.04) - 0.5) * len;\n"
    "    float clp = 1.0 / lob;\n"
    "    vec3 min4 = min(min(f, g), min(j, k));\n"
    "    vec3 max4 = max(max(f, g), max(j, k));\n"
    "    vec3 aC = vec3(0.0);\n"
    "    float aW = 0.0;\n"
    "    easu_tap(aC, aW, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, b);\n"
    "    easu_tap(aC, aW, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, c);\n"
    "    easu_tap(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, i);\n"
    "    easu_tap(aC, aW, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, j);\n"
    "    easu_tap(aC, aW, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, f);\n"
    "    easu_tap(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);\n"
    "    easu_tap(aC, aW, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, k);\n"
    "    easu_tap(aC, aW, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, l);\n"
    "    easu_tap(aC, aW, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, h);\n"
    "    easu_tap(aC, aW, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, g);\n"
    "    easu_tap(aC, aW, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, o);\n"
    "    easu_tap(aC, aW, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, n);\n"
    "    vec3 pix = min(max4, max(min4, aC * (1.0 / aW)));\n"
    "    gl_FragColor = vec4(pix, 1.0);\n"
    "}\n";

/* A box integral in source-texel space. A linear sample combines two adjacent
 * texels in each dimension with their exact covered lengths. At most nine
 * texels intersect an 8-wide fractional interval. Intermediate reductions
 * retain upload byte order, and the final pass decodes RGB. */
static const char present_fs_area[] =
    "#ifdef GL_ES\nprecision highp float;\n#endif\n"
    "varying highp vec2 uv;\n"
    "uniform highp sampler2D image;\n"
    "uniform vec2 insize, footprint;\n"
    "uniform int decode;\n"
    "vec2 pair(float left, float span, float index) {\n"
    "    float a = clamp(left + span - index, 0.0, 1.0) - clamp(left - index, 0.0, 1.0);\n"
    "    float b = clamp(left + span - index - 1.0, 0.0, 1.0) - clamp(left - index - 1.0, 0.0, 1.0);\n"
    "    return vec2(index + 0.5 + b / max(a + b, 1.0e-8), (a + b) / span);\n"
    "}\n"
    "void main() {\n"
    "    vec2 left = uv * insize - 0.5 * footprint;\n"
    "    vec2 start = floor(left);\n"
    "    vec3 sum = vec3(0.0);\n"
    "    for (int y = 0; y < 5; ++y) {\n"
    "        float row = start.y + float(2*y);\n"
    "        if (row >= left.y + footprint.y) break;\n"
    "        vec2 wy = pair(left.y, footprint.y, row);\n"
    "        for (int x = 0; x < 5; ++x) {\n"
    "            float col = start.x + float(2*x);\n"
    "            if (col >= left.x + footprint.x) break;\n"
    "            vec2 wx = pair(left.x, footprint.x, col);\n"
    "            sum += texture2D(image, vec2(wx.x, wy.x) / insize).rgb * (wx.y * wy.y);\n"
    "        }\n"
    "    }\n"
    "    gl_FragColor = vec4(decode != 0 ? sum.bgr : sum, 1.0);\n"
    "}\n";

#undef PRESENT_FRAME_SAMPLE

static const char present_fs_rcas[] =
    "#ifdef GL_ES\n"
    "precision highp float;\n"
    "#endif\n"
    "varying highp vec2 uv;\n"
    "uniform highp sampler2D image;\n"
    "uniform vec2 texel;\n"
    "uniform float sharp;\n"
    "\n"
    "float min3(float a, float b, float c) { return min(min(a, b), c); }\n"
    "float max3(float a, float b, float c) { return max(max(a, b), c); }\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec3 b = texture2D(image, uv + vec2(0.0, -texel.y)).rgb;\n"
    "    vec3 d = texture2D(image, uv + vec2(-texel.x, 0.0)).rgb;\n"
    "    vec3 e = texture2D(image, uv).rgb;\n"
    "    vec3 f = texture2D(image, uv + vec2(texel.x, 0.0)).rgb;\n"
    "    vec3 h = texture2D(image, uv + vec2(0.0, texel.y)).rgb;\n"
    "    float bL = b.b * 0.5 + (b.r * 0.5 + b.g);\n"
    "    float dL = d.b * 0.5 + (d.r * 0.5 + d.g);\n"
    "    float eL = e.b * 0.5 + (e.r * 0.5 + e.g);\n"
    "    float fL = f.b * 0.5 + (f.r * 0.5 + f.g);\n"
    "    float hL = h.b * 0.5 + (h.r * 0.5 + h.g);\n"
    "    float nz = 0.25 * bL + 0.25 * dL + 0.25 * fL + 0.25 * hL - eL;\n"
    "    float range = max3(bL, dL, max3(eL, fL, hL)) - min3(bL, dL, min3(eL, fL, hL));\n"
    "    nz = clamp(abs(nz) / max(range, 1.0 / 32768.0), 0.0, 1.0);\n"
    "    nz = -0.5 * nz + 1.0;\n"
    "    vec3 mn = min(min(b, d), min(f, h));\n"
    "    vec3 mx = max(max(b, d), max(f, h));\n"
    "    vec3 hitMin = min(mn, e) / max(4.0 * mx, vec3(1.0 / 32768.0));\n"
    "    vec3 hitMax = (1.0 - max(mx, e)) / min(4.0 * mn - 4.0, vec3(-1.0 / 32768.0));\n"
    /* A constant zero/one ring cannot violate its corresponding bound. */
    "    hitMin += 1.0 - step(vec3(1.0 / 32768.0), mx);\n"
    "    hitMax -= 1.0 - step(vec3(1.0 / 32768.0), 1.0 - mn);\n"
    "    vec3 limits = max(-hitMin, hitMax);\n"
    "    float lobe = clamp(max3(limits.r, limits.g, limits.b), -0.1875, 0.0) * sharp;\n"
    "    lobe *= nz;\n"
    "    vec3 res = (lobe * b + lobe * d + lobe * h + lobe * f + e) / (4.0 * lobe + 1.0);\n"
    "    gl_FragColor = vec4(res, 1.0);\n"
    "}\n";

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
    if (s->present_reduce_tex) s->gl.DeleteTextures(1, &s->present_reduce_tex);
    s->present_reduce_tex = 0;
    memset(&s->present_reduce_storage, 0, sizeof s->present_reduce_storage);
    if (s->present_area) s->gl.DeleteProgram(s->present_area);
    s->present_area = 0;
    if (s->present_stage_tex) s->gl.DeleteTextures(1, &s->present_stage_tex);
    if (s->present_stage_fbo) s->gl.DeleteFramebuffers(1, &s->present_stage_fbo);
    s->present_stage_tex = s->present_stage_fbo = 0;
    memset(&s->present_stage_storage, 0, sizeof s->present_stage_storage);
    if (s->present_fsr_easu) s->gl.DeleteProgram(s->present_fsr_easu);
    if (s->present_fsr_rcas) s->gl.DeleteProgram(s->present_fsr_rcas);
    s->present_fsr_easu = s->present_fsr_rcas = 0;
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

/* Link a presentation program that only needs the shared quad attributes.
 * Used by the two FSR1 stages, whose uniforms are fetched separately. */
static GLuint present_simple_program(nj_gles2 *s, const char *fragment)
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
    s->gl.DeleteShader(v); s->gl.DeleteShader(f);
    return p;
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
    static const char *const fragments[5] = {
        present_fs_nearest, present_fs_sharp, present_fs_scanline,
        present_fs_lcd, present_fs_crt
    };
    int i;
    if (s->present_ready) return 1;
    if (!current(s)) return 0;
    if (s->present_program[0] || s->present_input || s->present_target || s->present_fbo) {
        present_delete(s);
        if (!current(s) || !clean_errors(s)) return 0;
    }
    for (i = 0; i < 5; ++i) {
        if (!present_program(s, i, fragments[i])) {
            goto fail;
        }
    }
    if (!present_program(s, 6, present_fs_bicubic)) {
        goto fail;
    }
    s->present_fsr_easu = present_simple_program(s, present_fs_easu);
    s->present_fsr_rcas = present_simple_program(s, present_fs_rcas);
    s->present_area = present_simple_program(s, present_fs_area);
    if (!s->present_fsr_easu || !s->present_fsr_rcas || !s->present_area) {
        goto fail;
    }
    s->present_fsr_easu_sampler = s->gl.GetUniformLocation(s->present_fsr_easu, "image");
    s->present_fsr_easu_insize = s->gl.GetUniformLocation(s->present_fsr_easu, "insize");
    s->present_area_sampler = s->gl.GetUniformLocation(s->present_area, "image");
    s->present_area_insize = s->gl.GetUniformLocation(s->present_area, "insize");
    s->present_area_footprint = s->gl.GetUniformLocation(s->present_area, "footprint");
    s->present_area_decode = s->gl.GetUniformLocation(s->present_area, "decode");
    s->present_fsr_rcas_sampler = s->gl.GetUniformLocation(s->present_fsr_rcas, "image");
    s->present_fsr_rcas_texel = s->gl.GetUniformLocation(s->present_fsr_rcas, "texel");
    s->present_fsr_rcas_sharp = s->gl.GetUniformLocation(s->present_fsr_rcas, "sharp");
    if (s->present_fsr_easu_sampler < 0 || s->present_fsr_easu_insize < 0 ||
        s->present_area_sampler < 0 || s->present_area_insize < 0 ||
        s->present_area_footprint < 0 || s->present_area_decode < 0 || s->present_fsr_rcas_sampler < 0 ||
        s->present_fsr_rcas_texel < 0 || s->present_fsr_rcas_sharp < 0) goto fail;
    s->gl.GenTextures(1, &s->present_input);
    s->gl.GenTextures(1, &s->present_target);
    s->gl.GenFramebuffers(1, &s->present_fbo);
    s->gl.GenTextures(1, &s->present_stage_tex);
    s->gl.GenFramebuffers(1, &s->present_stage_fbo);
    if (!s->present_input || !s->present_target || !s->present_fbo ||
        !s->present_stage_tex || !s->present_stage_fbo || !clean_errors(s)) goto fail;
    s->present_ready = 1;
    return 1;
fail:
    if (current(s)) {
        present_delete(s);
        (void)clean_errors(s);
    }
    return 0;
}

/* A physical presentation frame has its own budget, separate from draw ROIs. */
static int present_dimensions(const nj_gles2 *s, int w, int h)
{
    return w > 0 && h > 0 && w <= s->max_texture && h <= s->max_texture &&
           (size_t)w <= NJ_GL_PRESENT_MAX_PIXELS / (size_t)h;
}

static int present_upload(nj_gles2 *s, const nj_surface *src)
{
    const nj_u4 byte_order = 1;
    int bpp = bytes_per_pixel(src->format);
    if (!present_dimensions(s, src->width, src->height) || !bpp ||
        (src->bpp && src->bpp != bpp) || (nj_s8)src->stride < (nj_s8)src->width * bpp)
        return 0;
    size_t row = (size_t)src->width * bpp;
    size_t rows = (size_t)src->height - 1;
    if (rows > (SIZE_MAX - row) / (size_t)src->stride ||
        rows * (size_t)src->stride + row > UINTPTR_MAX - (uintptr_t)src->pixels) return 0;

    int native_bgra = src->format == NJ_PIXFMT_ARGB8888 && *(const nj_u1 *)&byte_order == 1;
    const nj_u1 *pixels = src->pixels;
    if (!native_bgra || (size_t)src->stride != row) {
        size_t out_row = (size_t)src->width * 4u;
        if (!reserve(&s->upload, &s->upload_size, out_row * (size_t)src->height)) return 0;
        for (int y = 0; y < src->height; ++y) {
            const nj_u1 *pixel = pixels + (size_t)y * src->stride;
            nj_u1 *dst = s->upload + (size_t)y * out_row;
            if (native_bgra) {
                memcpy(dst, pixel, row); /* GLES2 has no core UNPACK_ROW_LENGTH. */
            } else {
                for (int x = 0; x < src->width; ++x, dst += 4, pixel += bpp) {
                    nj_u4 c = read_color(pixel, src->format);
                    dst[0] = (nj_u1)c;
                    dst[1] = (nj_u1)(c >> 8);
                    dst[2] = (nj_u1)(c >> 16);
                    dst[3] = 255;
                }
            }
        }
        pixels = s->upload;
    }
    s->gl.ActiveTexture(GL_TEXTURE0);
    texture(s, s->present_input);
    s->gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    return texture_upload(s, &s->present_input_storage, src->width, src->height, GL_RGBA, pixels);
}

/* All destinations use the same spatial filter, with the final quad flipped
 * vertically only for a window (or its capture). */
static int present_draw(nj_gles2 *s, int mode, int sw, int sh, GLuint fbo,
                        int x, int y, int w, int h, int flip);

static int present_offscreen(void *ud, const nj_surface *src, int mode, int out_w, int out_h,
                             GLenum read_format, nj_u1 *out)
{
    nj_gles2 *s = (nj_gles2 *)ud;
#if !defined(__ANDROID__)
#define NJ_PRESENT_DBG(why) do { \
    const char *e = getenv("NJ_GL_PRESENT_DEBUG"); \
    if (e && e[0] == '1') fprintf(stderr, "present decline: %s\n", why); } while (0)
#else
#define NJ_PRESENT_DBG(why) ((void)0)
#endif
    if (!s || !src || !src->pixels || !out || mode < 0 || mode >= NJ_GL_PRESENT_MODES) {
        NJ_PRESENT_DBG("arguments/mode"); return 0;
    }
    if (!current(s) || !present_init(s)) { NJ_PRESENT_DBG("current/init"); return 0; }
    /* Presentation targets are bounded by the drawable/viewport, not by the
     * generic 1M-pixel texture budget (a 1080x2400 window alone exceeds it). */
    if (!present_dimensions(s, out_w, out_h) ||
        out_w > s->max_viewport[0] || out_h > s->max_viewport[1]) {
        NJ_PRESENT_DBG("out size"); return 0;
    }
    if (!present_upload(s, src)) { NJ_PRESENT_DBG("input upload"); return 0; }
    texture(s, s->present_target);
    if (!texture_upload(s, &s->present_target_storage, out_w, out_h, GL_RGBA, NULL)) {
        NJ_PRESENT_DBG("target upload"); return 0;
    }
    s->gl.BindFramebuffer(GL_FRAMEBUFFER, s->present_fbo);
    s->gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               s->present_target, 0);
    if (s->gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        NJ_PRESENT_DBG("fbo incomplete"); return 0;
    }
    if (!present_draw(s, mode, src->width, src->height, s->present_fbo, 0, 0, out_w, out_h, 0))
        return 0;
    s->gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    s->gl.ReadPixels(0, 0, out_w, out_h, read_format, GL_UNSIGNED_BYTE, out);
    s->gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    return clean_errors(s);
}

int nj_gles2_present(void *ud, const nj_surface *src, int mode, int out_w, int out_h,
                     nj_u1 *out)
{
    return present_offscreen(ud, src, mode, out_w, out_h, GL_RGBA, out);
}

int nj_gles2_present_bgra(void *ud, const nj_surface *src, int mode, int out_w, int out_h,
                          nj_u1 *out)
{
    const GLenum bgra = 0x80E1; /* GL_BGRA_EXT, absent from core ES2 headers */
    return present_offscreen(ud, src, mode, out_w, out_h, bgra, out);
}

static void present_quad(nj_gles2 *s, int flip)
{
    static const GLfloat quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    static const GLfloat flat[8] = {0, 0, 1, 0, 0, 1, 1, 1};
    static const GLfloat flipped[8] = {0, 1, 1, 1, 0, 0, 1, 0};
    s->gl.EnableVertexAttribArray(0); s->gl.EnableVertexAttribArray(1);
    s->gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    s->gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, flip ? flipped : flat);
    s->gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    s->gl.DisableVertexAttribArray(0); s->gl.DisableVertexAttribArray(1);
    s->gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    s->gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, NULL);
}

/* Bind a reusable intermediate target, separate from the source and the final
 * destination. The second texture is needed only for successive wide reductions. */
static int present_stage(nj_gles2 *s, GLuint tex, texture_storage *storage, int w, int h)
{
    if (w > s->max_viewport[0] || h > s->max_viewport[1]) return 0;
    texture(s, tex);
    if (!texture_upload(s, storage, w, h, GL_RGBA, NULL)) return 0;
    s->gl.BindFramebuffer(GL_FRAMEBUFFER, s->present_stage_fbo);
    s->gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (s->gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 0;
    s->gl.Viewport(0, 0, w, h);
    return 1;
}

static void present_area_pass(nj_gles2 *s, GLuint src, int sw, int sh,
                              int w, int h, int decode, int flip)
{
    s->gl.UseProgram(s->present_area);
    s->gl.BindTexture(GL_TEXTURE_2D, src);
    s->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    s->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    s->gl.Uniform1i(s->present_area_sampler, 0);
    s->gl.Uniform2f(s->present_area_insize, (float)sw, (float)sh);
    /* Unit width on an expanding axis gives linear interpolation there. */
    s->gl.Uniform2f(s->present_area_footprint, sw > w ? (float)sw/w : 1.0f,
                    sh > h ? (float)sh/h : 1.0f);
    s->gl.Uniform1i(s->present_area_decode, decode);
    present_quad(s, flip);
}

static int present_draw(nj_gles2 *s, int mode, int sw, int sh, GLuint fbo,
                        int x, int y, int w, int h, int flip)
{
    GLuint src = s->present_input;
    s->gl.ActiveTexture(GL_TEXTURE0);
    if (mode == 5 && sw == w && sh == h) mode = 0; /* exact native copy */
    if (mode != 5) {
        s->gl.UseProgram(s->present_program[mode]);
        s->gl.BindTexture(GL_TEXTURE_2D, src);
        s->gl.Uniform1i(s->present_sampler[mode], 0);
        if (s->present_texel[mode] >= 0)
            s->gl.Uniform2f(s->present_texel[mode], 1.0f/sw, 1.0f/sh);
        if (s->present_outsize[mode] >= 0)
            s->gl.Uniform2f(s->present_outsize[mode], (float)w, (float)h);
    } else if (sw > w || sh > h) {
        /* Bound the shader loop on ES2 even for a huge frame in a tiny viewport. */
        while ((sw-1)/8 >= w || (sh-1)/8 >= h) {
            int nw = (sw-1)/8 >= w ? (sw+7)/8 : sw;
            int nh = (sh-1)/8 >= h ? (sh+7)/8 : sh;
            GLuint next = s->present_stage_tex;
            texture_storage *storage = &s->present_stage_storage;
            if (src == next) {
                if (!s->present_reduce_tex) s->gl.GenTextures(1, &s->present_reduce_tex);
                next = s->present_reduce_tex;
                storage = &s->present_reduce_storage;
            }
            if (!next || !present_stage(s, next, storage, nw, nh)) return 0;
            present_area_pass(s, src, sw, sh, nw, nh, 0, 0);
            src = next; sw = nw; sh = nh;
        }
        s->gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
        s->gl.Viewport(x, y, w, h);
        present_area_pass(s, src, sw, sh, w, h, 1, flip);
        return clean_errors(s);
    } else {
        /* EASU's performance preset reconstructs at 2x per axis. For a larger
         * display, reconstruct at that density and interpolate the RCAS cross
         * on that same grid. This keeps sharpening in reconstructed texels. */
        int rw = w > 2*sw ? 2*sw : w;
        int rh = h > 2*sh ? 2*sh : h;
        if (!present_stage(s, s->present_stage_tex, &s->present_stage_storage, rw, rh)) return 0;
        s->gl.UseProgram(s->present_fsr_easu);
        s->gl.BindTexture(GL_TEXTURE_2D, src);
        s->gl.Uniform1i(s->present_fsr_easu_sampler, 0);
        s->gl.Uniform2f(s->present_fsr_easu_insize, (float)sw, (float)sh);
        present_quad(s, 0);
        s->gl.UseProgram(s->present_fsr_rcas);
        s->gl.BindTexture(GL_TEXTURE_2D, s->present_stage_tex);
        if (rw != w || rh != h) {
            s->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            s->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        s->gl.Uniform1i(s->present_fsr_rcas_sampler, 0);
        s->gl.Uniform2f(s->present_fsr_rcas_texel, 1.0f/rw, 1.0f/rh);
        s->gl.Uniform1f(s->present_fsr_rcas_sharp, s->present_sharp);
    }
    s->gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    s->gl.Viewport(x, y, w, h);
    present_quad(s, flip);
    return clean_errors(s);
}

int nj_gles2_present_onscreen_capture(void *ud, const nj_surface *src, int mode,
                                      int vp_x, int vp_y, int vp_w, int vp_h,
                                      int out_w, int out_h, nj_u1 *out_rgba)
{
    nj_gles2 *s = (nj_gles2 *)ud;
    GLuint tex = 0, fbo = 0;
    int w, h, ok = 0;

    if (!s || !src || !src->pixels || !out_rgba) return 0;
    if (mode < 0 || mode >= NJ_GL_PRESENT_MODES) return 0;
    if (!current(s) || !present_init(s)) return 0;
    if (!present_dimensions(s, out_w, out_h) ||
        out_w > s->max_viewport[0] || out_h > s->max_viewport[1]) return 0;
    if (vp_w <= 0 || vp_h <= 0 || vp_w > s->max_viewport[0] || vp_h > s->max_viewport[1] ||
        (nj_s8)vp_x + vp_w > INT_MAX || (nj_s8)vp_y + vp_h > INT_MAX ||
        (mode == 5 && !present_dimensions(s, vp_w, vp_h))) return 0;
    w = out_w;
    h = out_h;
    if (!present_upload(s, src)) return 0;

    s->gl.GenTextures(1, &tex);
    s->gl.BindTexture(GL_TEXTURE_2D, tex);
    s->gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, NULL);
    s->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    s->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    s->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    s->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    s->gl.GenFramebuffers(1, &fbo);
    s->gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    s->gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, tex, 0);
    if (s->gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        s->gl.Disable(GL_SCISSOR_TEST);
        s->gl.Viewport(0, 0, w, h);
        s->gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        s->gl.Clear(GL_COLOR_BUFFER_BIT);
        if (present_draw(s, mode, src->width, src->height, fbo, vp_x, vp_y, vp_w, vp_h, 1)) {
            s->gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
            s->gl.ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out_rgba);
            ok = clean_errors(s);
        }
    }
    s->gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    s->gl.DeleteFramebuffers(1, &fbo);
    s->gl.DeleteTextures(1, &tex);
    (void)clean_errors(s);
    return ok;
}

int nj_gles2_present_readback(void *ud, int w, int h, nj_u1 *out_rgba)
{
    nj_gles2 *s = (nj_gles2 *)ud;
    if (s == NULL || out_rgba == NULL || w <= 0 || h <= 0) return 0;
    if (!current(s)) return 0;
    if (!present_dimensions(s, w, h) || w > s->max_viewport[0] || h > s->max_viewport[1]) return 0;
    s->gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    s->gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    s->gl.ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out_rgba);
    return clean_errors(s);
}

int nj_gles2_present_onscreen(void *ud, const nj_surface *src, int mode,
                              int vp_x, int vp_y, int vp_w, int vp_h)
{
    nj_gles2 *s = (nj_gles2 *)ud;

    if (!s || !src || !src->pixels) return 0;
    if (mode < 0 || mode >= NJ_GL_PRESENT_MODES) return 0;
    if (!current(s) || !present_init(s)) return 0;
    /* Zoomed windows may crop a viewport whose origin is outside the drawable. */
    if (vp_w <= 0 || vp_h <= 0 || vp_w > s->max_viewport[0] || vp_h > s->max_viewport[1] ||
        (nj_s8)vp_x + vp_w > INT_MAX || (nj_s8)vp_y + vp_h > INT_MAX ||
        (mode == 5 && !present_dimensions(s, vp_w, vp_h))) return 0;
    if (!present_upload(s, src)) return 0;

    /* The window path fills the whole drawable: black bars first, then the
     * picture inside the viewport the port computed for the letterbox. */
    s->gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    s->gl.Disable(GL_SCISSOR_TEST);
    s->gl.Viewport(0, 0, s->max_viewport[0], s->max_viewport[1]);
    s->gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    s->gl.Clear(GL_COLOR_BUFFER_BIT);
    return present_draw(s, mode, src->width, src->height, 0, vp_x, vp_y, vp_w, vp_h, 1);
}


/* Tune RCAS sharpness (0.05..1.0). Default mirrors the AMD reference exp2(-0.2). */
void nj_gles2_present_set_sharp(void *ud, float sharp)
{
    nj_gles2 *s = (nj_gles2 *)ud;
    if (!s) return;
    if (sharp < 0.05f) sharp = 0.05f;
    if (sharp > 1.0f) sharp = 1.0f;
    s->present_sharp = sharp;
}
