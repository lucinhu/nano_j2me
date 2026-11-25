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

/* 5: FSR1 (EASU + RCAS).  Two passes: EASU edge-adaptive upscale into
 * present_stage_tex, then RCAS contrast-adaptive sharpening into the
 * output framebuffer.  Translated from the MIT-licensed AMD FidelityFX
 * FSR 1* reference (ffx_fsr1.h); textureGather is rebuilt with four
 * texture2D taps because GLSL ES 1.0 has no gather instruction.
 * The RCAS sharpness is the reference default of 0.2 stops. */
static const char present_fs_bicubic[] =
    "precision highp float;\n"
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
    "      acc += texture2D(image, (base + o + 0.5) * texel) * (wx * wy);\n"
    "    }\n"
    "  }\n"
    "  gl_FragColor = acc;\n"
    "}\n";

static const char present_fs_easu[] =
    "#ifdef GL_ES\n"
    "precision highp float;\n"
    "#endif\n"
    "varying highp vec2 uv;\n"
    "uniform highp sampler2D image;\n"
    "uniform vec2 insize;\n"
    "uniform vec2 outsize;\n"
    "uniform vec4 con0;\n"
    "uniform vec4 con1;\n"
    "uniform vec4 con2;\n"
    "uniform vec4 con3;\n"
    "\n"
    "/* Manual textureGather for GLSL ES 1.0: the four texels a bilinear tap\n"
    " * would combine, in the (x,y) (x+1,y) (x,y+1) (x+1,y+1) order the FSR1\n"
    " * reference reads from textureGather(). */\n"
    "void easu_gather(vec2 p, out vec4 r, out vec4 g, out vec4 b)\n"
    "{\n"
    "    vec2 ij = floor(p * insize - 0.5);\n"
    "    vec2 base = (ij + 0.5) / insize;\n"
    "    vec2 st = 1.0 / insize;\n"
    "    vec4 t00 = texture2D(image, base);\n"
    "    vec4 t10 = texture2D(image, base + vec2(st.x, 0.0));\n"
    "    vec4 t01 = texture2D(image, base + vec2(0.0, st.y));\n"
    "    vec4 t11 = texture2D(image, base + st);\n"
    "    r = vec4(t00.r, t10.r, t01.r, t11.r);\n"
    "    g = vec4(t00.g, t10.g, t01.g, t11.g);\n"
    "    b = vec4(t00.b, t10.b, t01.b, t11.b);\n"
    "}\n"
    "\n"
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
    "    lenX = 1.0 / lenX;\n"
    "    float dirX = lD - lB;\n"
    "    dir.x += dirX * w;\n"
    "    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);\n"
    "    lenX *= lenX;\n"
    "    len += lenX * w;\n"
    "    float ec = lE - lC;\n"
    "    float ca = lC - lA;\n"
    "    float lenY = max(abs(ec), abs(ca));\n"
    "    lenY = 1.0 / lenY;\n"
    "    float dirY = lE - lA;\n"
    "    dir.y += dirY * w;\n"
    "    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);\n"
    "    lenY *= lenY;\n"
    "    len += lenY * w;\n"
    "}\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec2 ip = uv * outsize - 0.5;\n"
    "    vec2 pp = ip * con0.xy + con0.zw;\n"
    "    vec2 fp = floor(pp);\n"
    "    pp -= fp;\n"
    "    vec2 p0 = fp * con1.xy + con1.zw;\n"
    "    vec2 p1 = p0 + con2.xy;\n"
    "    vec2 p2 = p0 + con2.zw;\n"
    "    vec2 p3 = p0 + con3.xy;\n"
    "    vec4 bczzR, bczzG, bczzB, ijfeR, ijfeG, ijfeB;\n"
    "    vec4 klhgR, klhgG, klhgB, zzonR, zzonG, zzonB;\n"
    "    easu_gather(p0, bczzR, bczzG, bczzB);\n"
    "    easu_gather(p1, ijfeR, ijfeG, ijfeB);\n"
    "    easu_gather(p2, klhgR, klhgG, klhgB);\n"
    "    easu_gather(p3, zzonR, zzonG, zzonB);\n"
    "    vec4 bczzL = bczzB * 0.5 + (bczzR * 0.5 + bczzG);\n"
    "    vec4 ijfeL = ijfeB * 0.5 + (ijfeR * 0.5 + ijfeG);\n"
    "    vec4 klhgL = klhgB * 0.5 + (klhgR * 0.5 + klhgG);\n"
    "    vec4 zzonL = zzonB * 0.5 + (zzonR * 0.5 + zzonG);\n"
    "    float bL = bczzL.x, cL = bczzL.y;\n"
    "    float iL = ijfeL.x, jL = ijfeL.y, fL = ijfeL.z, eL = ijfeL.w;\n"
    "    float kL = klhgL.x, lL = klhgL.y, hL = klhgL.z, gL = klhgL.w;\n"
    "    float oL = zzonL.z, nL = zzonL.w;\n"
    "    vec2 dir = vec2(0.0);\n"
    "    float len = 0.0;\n"
    "    easu_set(dir, len, pp, 0, bL, eL, fL, gL, jL);\n"
    "    easu_set(dir, len, pp, 1, cL, fL, gL, hL, kL);\n"
    "    easu_set(dir, len, pp, 2, fL, iL, jL, kL, nL);\n"
    "    easu_set(dir, len, pp, 3, gL, jL, kL, lL, oL);\n"
    "    vec2 dir2 = dir * dir;\n"
    "    float dirR = dir2.x + dir2.y;\n"
    "    bool zro = dirR < (1.0 / 32768.0);\n"
    "    dirR = inversesqrt(dirR);\n"
    "    dirR = zro ? 1.0 : dirR;\n"
    "    dir.x = zro ? 1.0 : dir.x;\n"
    "    dir *= dirR;\n"
    "    len = len * 0.5;\n"
    "    len *= len;\n"
    "    float stretch = (dir.x * dir.x + dir.y * dir.y) / max(abs(dir.x), abs(dir.y));\n"
    "    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 + (-0.5) * len);\n"
    "    float lob = 0.5 + ((0.25 - 0.04) - 0.5) * len;\n"
    "    float clp = 1.0 / lob;\n"
    "    vec3 c0 = vec3(ijfeR.z, ijfeG.z, ijfeB.z);\n"
    "    vec3 c1 = vec3(klhgR.w, klhgG.w, klhgB.w);\n"
    "    vec3 c2 = vec3(ijfeR.y, ijfeG.y, ijfeB.y);\n"
    "    vec3 c3 = vec3(klhgR.x, klhgG.x, klhgB.x);\n"
    "    vec3 min4 = min(min(c0, c1), min(c2, c3));\n"
    "    vec3 max4 = max(max(c0, c1), max(c2, c3));\n"
    "    vec3 aC = vec3(0.0);\n"
    "    float aW = 0.0;\n"
    "    easu_tap(aC, aW, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, vec3(bczzR.x, bczzG.x, bczzB.x));\n"
    "    easu_tap(aC, aW, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, vec3(bczzR.y, bczzG.y, bczzB.y));\n"
    "    easu_tap(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, vec3(ijfeR.x, ijfeG.x, ijfeB.x));\n"
    "    easu_tap(aC, aW, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, vec3(ijfeR.y, ijfeG.y, ijfeB.y));\n"
    "    easu_tap(aC, aW, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, vec3(ijfeR.z, ijfeG.z, ijfeB.z));\n"
    "    easu_tap(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, vec3(ijfeR.w, ijfeG.w, ijfeB.w));\n"
    "    easu_tap(aC, aW, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, vec3(klhgR.x, klhgG.x, klhgB.x));\n"
    "    easu_tap(aC, aW, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, vec3(klhgR.y, klhgG.y, klhgB.y));\n"
    "    easu_tap(aC, aW, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, vec3(klhgR.z, klhgG.z, klhgB.z));\n"
    "    easu_tap(aC, aW, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, vec3(klhgR.w, klhgG.w, klhgB.w));\n"
    "    easu_tap(aC, aW, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, vec3(zzonR.z, zzonG.z, zzonB.z));\n"
    "    easu_tap(aC, aW, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, vec3(zzonR.w, zzonG.w, zzonB.w));\n"
    "    vec3 pix = min(max4, max(min4, aC * (1.0 / aW)));\n"
    "    gl_FragColor = vec4(pix, 1.0);\n"
    "}\n";

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
    "    float mnR = min3(b.r, d.r, f.r); mnR = min(mnR, h.r);\n"
    "    float mnG = min3(b.g, d.g, f.g); mnG = min(mnG, h.g);\n"
    "    float mnB = min3(b.b, d.b, f.b); mnB = min(mnB, h.b);\n"
    "    float mxR = max3(b.r, d.r, f.r); mxR = max(mxR, h.r);\n"
    "    float mxG = max3(b.g, d.g, f.g); mxG = max(mxG, h.g);\n"
    "    float mxB = max3(b.b, d.b, f.b); mxB = max(mxB, h.b);\n"
    "    float hitMinR = min(mnR, e.r) / (4.0 * mxR);\n"
    "    float hitMinG = min(mnG, e.g) / (4.0 * mxG);\n"
    "    float hitMinB = min(mnB, e.b) / (4.0 * mxB);\n"
    "    float hitMaxR = (1.0 - max(mxR, e.r)) / (4.0 * mnR - 4.0);\n"
    "    float hitMaxG = (1.0 - max(mxG, e.g)) / (4.0 * mnG - 4.0);\n"
    "    float hitMaxB = (1.0 - max(mxB, e.b)) / (4.0 * mnB - 4.0);\n"
    "    float lobeR = max(-hitMinR, hitMaxR);\n"
    "    float lobeG = max(-hitMinG, hitMaxG);\n"
    "    float lobeB = max(-hitMinB, hitMaxB);\n"
    "    float lobe = max(-0.1875, min(max3(lobeR, lobeG, lobeB), 0.0)) * sharp;\n"
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
    if (!s->present_fsr_easu || !s->present_fsr_rcas) {
        goto fail;
    }
    s->present_fsr_easu_sampler = s->gl.GetUniformLocation(s->present_fsr_easu, "image");
    s->present_fsr_easu_insize = s->gl.GetUniformLocation(s->present_fsr_easu, "insize");
    s->present_fsr_easu_outsize = s->gl.GetUniformLocation(s->present_fsr_easu, "outsize");
    s->present_fsr_easu_con[0] = s->gl.GetUniformLocation(s->present_fsr_easu, "con0");
    s->present_fsr_easu_con[1] = s->gl.GetUniformLocation(s->present_fsr_easu, "con1");
    s->present_fsr_easu_con[2] = s->gl.GetUniformLocation(s->present_fsr_easu, "con2");
    s->present_fsr_easu_con[3] = s->gl.GetUniformLocation(s->present_fsr_easu, "con3");
    s->present_fsr_rcas_sampler = s->gl.GetUniformLocation(s->present_fsr_rcas, "image");
    s->present_fsr_rcas_texel = s->gl.GetUniformLocation(s->present_fsr_rcas, "texel");
    s->present_fsr_rcas_sharp = s->gl.GetUniformLocation(s->present_fsr_rcas, "sharp");
    if (s->present_fsr_easu_sampler < 0 || s->present_fsr_easu_insize < 0 ||
        s->present_fsr_easu_outsize < 0 || s->present_fsr_easu_con[0] < 0 ||
        s->present_fsr_easu_con[1] < 0 || s->present_fsr_easu_con[2] < 0 ||
        s->present_fsr_easu_con[3] < 0 || s->present_fsr_rcas_sampler < 0 ||
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

/* FSR1: EASU upscales the uploaded input into present_stage_tex, then RCAS
 * sharpens that into present_target.  Both draws use the shared quad. */
static int present_fsr(nj_gles2 *s, int in_w, int in_h, int out_w, int out_h,
                       nj_u1 *out)
{
    static const GLfloat quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    static const GLfloat uvs[8] = {0, 0, 1, 0, 0, 1, 1, 1};
    GLfloat iw = (GLfloat)in_w, ih = (GLfloat)in_h;
    GLfloat ow = (GLfloat)out_w, oh = (GLfloat)out_h;
    GLfloat sx = iw / ow, sy = ih / oh;
    GLfloat con0[4] = {sx, sy, 0.5f * sx - 0.5f, 0.5f * sy - 0.5f};
    GLfloat con1[4] = {1.0f / iw, 1.0f / ih, 1.0f / iw, -1.0f / ih};
    GLfloat con2[4] = {-1.0f / iw, 2.0f / ih, 1.0f / iw, 2.0f / ih};
    GLfloat con3[4] = {0.0f, 4.0f / ih, 0.0f, 0.0f};

    texture(s, s->present_stage_tex);
    if (!texture_upload(s, &s->present_stage_storage, out_w, out_h, GL_RGBA, NULL))
        return 0;
    s->gl.BindFramebuffer(GL_FRAMEBUFFER, s->present_stage_fbo);
    s->gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               s->present_stage_tex, 0);
    if (s->gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        return 0;
    }
    s->gl.Viewport(0, 0, out_w, out_h);
    s->gl.UseProgram(s->present_fsr_easu);
    s->gl.ActiveTexture(GL_TEXTURE0);
    s->gl.BindTexture(GL_TEXTURE_2D, s->present_input);
    s->gl.Uniform1i(s->present_fsr_easu_sampler, 0);
    s->gl.Uniform2f(s->present_fsr_easu_insize, iw, ih);
    s->gl.Uniform2f(s->present_fsr_easu_outsize, ow, oh);
    s->gl.Uniform4f(s->present_fsr_easu_con[0], con0[0], con0[1], con0[2], con0[3]);
    s->gl.Uniform4f(s->present_fsr_easu_con[1], con1[0], con1[1], con1[2], con1[3]);
    s->gl.Uniform4f(s->present_fsr_easu_con[2], con2[0], con2[1], con2[2], con2[3]);
    s->gl.Uniform4f(s->present_fsr_easu_con[3], con3[0], con3[1], con3[2], con3[3]);
    s->gl.EnableVertexAttribArray(0); s->gl.EnableVertexAttribArray(1);
    s->gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    s->gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uvs);
    s->gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    s->gl.DisableVertexAttribArray(0); s->gl.DisableVertexAttribArray(1);

    texture(s, s->present_target);
    if (!texture_upload(s, &s->present_target_storage, out_w, out_h, GL_RGBA, NULL)) {
        return 0;
    }
    s->gl.BindFramebuffer(GL_FRAMEBUFFER, s->present_fbo);
    s->gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               s->present_target, 0);
    if (s->gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        return 0;
    }
    s->gl.Viewport(0, 0, out_w, out_h);
    s->gl.UseProgram(s->present_fsr_rcas);
    s->gl.ActiveTexture(GL_TEXTURE0);
    s->gl.BindTexture(GL_TEXTURE_2D, s->present_stage_tex);
    s->gl.Uniform1i(s->present_fsr_rcas_sampler, 0);
    s->gl.Uniform2f(s->present_fsr_rcas_texel, 1.0f / ow, 1.0f / oh);
    s->gl.Uniform1f(s->present_fsr_rcas_sharp, s->present_sharp);
    s->gl.EnableVertexAttribArray(0); s->gl.EnableVertexAttribArray(1);
    s->gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    s->gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uvs);
    s->gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    s->gl.DisableVertexAttribArray(0); s->gl.DisableVertexAttribArray(1);
    s->gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    s->gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, NULL);

    s->gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    s->gl.ReadPixels(0, 0, out_w, out_h, GL_RGBA, GL_UNSIGNED_BYTE, out);
    s->gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    return 1;
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
    if (out_w <= 0 || out_h <= 0 || out_w > s->max_viewport[0] || out_h > s->max_viewport[1] ||
        out_w > s->max_texture || out_h > s->max_texture) {
        NJ_PRESENT_DBG("out size"); return 0;
    }
    if (!dimensions(s, src->width, src->height)) { NJ_PRESENT_DBG("source size"); return 0; }
    if (src->format != NJ_PIXFMT_RGB565 && src->format != NJ_PIXFMT_RGB888 &&
        src->format != NJ_PIXFMT_ARGB8888) { NJ_PRESENT_DBG("format"); return 0; }
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
    if (!texture_upload(s, &s->present_input_storage, src->width, src->height, GL_RGBA, s->upload)) {
        NJ_PRESENT_DBG("input upload"); return 0;
    }
    if (mode == 5) {
        if (!present_fsr(s, src->width, src->height, out_w, out_h, out)) {
            return 0;
        }
        return clean_errors(s);
    }
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

/*
 * Upload the source framebuffer as an RGBA texture for the presentation
 * shaders.  Shared by the offscreen (readback) and on-screen variants.
 */
static int present_upload(nj_gles2 *s, const nj_surface *src)
{
    size_t in_bytes;
    int x, y;

    if (!s || !src || !src->pixels) return 0;
    if (!dimensions(s, src->width, src->height)) return 0;
    if (src->format != NJ_PIXFMT_RGB565 && src->format != NJ_PIXFMT_RGB888 &&
        src->format != NJ_PIXFMT_ARGB8888) { NJ_PRESENT_DBG("format"); return 0; }
    in_bytes = (size_t)src->width * (size_t)src->height * 4u;
    if (!reserve(&s->upload, &s->upload_size, in_bytes)) return 0;
    for (y = 0; y < src->height; ++y) {
        nj_u1 *dst = s->upload + (size_t)y * (size_t)src->width * 4u;
        for (x = 0; x < src->width; ++x) {
            nj_u4 c = nj_surface_get(src, x, y);
            dst[x * 4 + 0] = (nj_u1)(c >> 16);
            dst[x * 4 + 1] = (nj_u1)(c >> 8);
            dst[x * 4 + 2] = (nj_u1)c;
            dst[x * 4 + 3] = 255;
        }
    }
    s->gl.ActiveTexture(GL_TEXTURE0);
    texture(s, s->present_input);
    return texture_upload(s, &s->present_input_storage, src->width, src->height,
                          GL_RGBA, s->upload);
}

/*
 * Draw the presentation quad for the loaded input texture.  The caller has
 * already bound the destination framebuffer and set the viewport.
 */
static int present_draw_quad(nj_gles2 *s, int mode, int src_w, int src_h,
                             int out_w, int out_h, int flip)
{
    static const GLfloat quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    static const GLfloat uv_flat[8] = {0, 0, 1, 0, 1, 1, 0, 1};
    static const GLfloat uv_flip[8] = {1, 1, 0, 1, 0, 0, 1, 0};
    static const GLfloat uv_vflip[8] = {0, 1, 1, 1, 0, 0, 1, 0};
    const GLfloat *uv = (flip == 2) ? uv_vflip : (flip ? uv_flip : uv_flat);

    s->gl.UseProgram(s->present_program[mode]);
    s->gl.ActiveTexture(GL_TEXTURE0);
    s->gl.BindTexture(GL_TEXTURE_2D, s->present_input);
    s->gl.Uniform1i(s->present_sampler[mode], 0);
    if (s->present_texel[mode] >= 0)
        s->gl.Uniform2f(s->present_texel[mode], 1.0f / (float)src_w, 1.0f / (float)src_h);
    if (s->present_outsize[mode] >= 0)
        s->gl.Uniform2f(s->present_outsize[mode], (float)out_w, (float)out_h);
    s->gl.EnableVertexAttribArray(0);
    s->gl.EnableVertexAttribArray(1);
    s->gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    s->gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uv);
    s->gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    s->gl.DisableVertexAttribArray(0);
    s->gl.DisableVertexAttribArray(1);
    s->gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    s->gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    return clean_errors(s);
}

/*
 * On-screen variant: render the presentation shader straight into the
 * current draw framebuffer (framebuffer 0) with no readback.  Used by the
 * Android window presentation path; the desktop SDL2 port keeps using
 * nj_gles2_present() so its PPM comparison tooling stays unchanged.
 *
 * Mode 5 (FSR1) is not supported on this path yet: EASU/RCAS write through an
 * intermediate stage texture and read the result back today, so an Android
 * port of the on-screen FSR chain is a separate step.  Returns 0 (caller
 * falls back to software presentation) for unsupported modes.
 */
/* Debug aid: draw the same on-screen quad into a temporary offscreen
 * framebuffer of the full drawable and read it back.  Unlike a window-surface
 * readback this works on drivers that cannot read the window surface before
 * the swap; it answers "is the picture we draw correct?" independently of the
 * system compositor. */
static int present_fsr_screen(nj_gles2 *s, int in_w, int in_h, int vp_x, int vp_y,
                              int vp_w, int vp_h);

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
    if (out_w <= 0 || out_h <= 0) return 0;
    if (vp_w <= 0 || vp_h <= 0 || vp_x < 0 || vp_y < 0) return 0;
    if (vp_x + vp_w > out_w || vp_y + vp_h > out_h) return 0;
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
        s->gl.Viewport(vp_x, vp_y, vp_w, vp_h);
        if (mode == 5 ? present_fsr_screen(s, src->width, src->height, vp_x, vp_y, vp_w, vp_h)
                      : present_draw_quad(s, mode, src->width, src->height, vp_w, vp_h, 2)) {
            s->gl.ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out_rgba);
            ok = 1;
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
    if (!dimensions(s, w, h)) return 0;
    s->gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    s->gl.ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out_rgba);
    (void)clean_errors(s);
    return 1;
}

/* Two-pass FSR1 (EASU -> RCAS) straight into the current framebuffer.  The
 * EASU stage is an offscreen texture the size of the destination viewport;
 * RCAS then draws it into the bound framebuffer (window or capture FBO) with
 * the vertical flip the window path needs.  No readback happens here. */
static int present_fsr_screen(nj_gles2 *s, int in_w, int in_h, int vp_x, int vp_y,
                              int vp_w, int vp_h)
{
    static const GLfloat quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    static const GLfloat uvs[8] = {0, 0, 1, 0, 0, 1, 1, 1};
    static const GLfloat uvs_vflip[8] = {0, 1, 1, 1, 0, 0, 1, 0};
    GLint binding = 0;
    GLfloat iw = (GLfloat)in_w, ih = (GLfloat)in_h;
    GLfloat ow = (GLfloat)vp_w, oh = (GLfloat)vp_h;
    GLfloat sx = iw / ow, sy = ih / oh;
    GLfloat con0[4] = {sx, sy, 0.5f * sx - 0.5f, 0.5f * sy - 0.5f};
    GLfloat con1[4] = {1.0f / iw, 1.0f / ih, 1.0f / iw, -1.0f / ih};
    GLfloat con2[4] = {-1.0f / iw, 2.0f / ih, 1.0f / iw, 2.0f / ih};
    GLfloat con3[4] = {0.0f, 4.0f / ih, 0.0f, 0.0f};

    s->gl.GetIntegerv(GL_FRAMEBUFFER_BINDING, &binding);
    texture(s, s->present_stage_tex);
    if (!texture_upload(s, &s->present_stage_storage, vp_w, vp_h, GL_RGBA, NULL))
        return 0;
    s->gl.BindFramebuffer(GL_FRAMEBUFFER, s->present_stage_fbo);
    s->gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               s->present_stage_tex, 0);
    if (s->gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 0;
    s->gl.Viewport(0, 0, vp_w, vp_h);
    s->gl.UseProgram(s->present_fsr_easu);
    s->gl.ActiveTexture(GL_TEXTURE0);
    s->gl.BindTexture(GL_TEXTURE_2D, s->present_input);
    s->gl.Uniform1i(s->present_fsr_easu_sampler, 0);
    s->gl.Uniform2f(s->present_fsr_easu_insize, iw, ih);
    s->gl.Uniform2f(s->present_fsr_easu_outsize, ow, oh);
    s->gl.Uniform4f(s->present_fsr_easu_con[0], con0[0], con0[1], con0[2], con0[3]);
    s->gl.Uniform4f(s->present_fsr_easu_con[1], con1[0], con1[1], con1[2], con1[3]);
    s->gl.Uniform4f(s->present_fsr_easu_con[2], con2[0], con2[1], con2[2], con2[3]);
    s->gl.Uniform4f(s->present_fsr_easu_con[3], con3[0], con3[1], con3[2], con3[3]);
    s->gl.EnableVertexAttribArray(0); s->gl.EnableVertexAttribArray(1);
    s->gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    s->gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uvs);
    s->gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    s->gl.DisableVertexAttribArray(0); s->gl.DisableVertexAttribArray(1);

    s->gl.BindFramebuffer(GL_FRAMEBUFFER, (GLuint)binding);
    s->gl.Viewport(vp_x, vp_y, vp_w, vp_h);
    s->gl.UseProgram(s->present_fsr_rcas);
    s->gl.ActiveTexture(GL_TEXTURE0);
    s->gl.BindTexture(GL_TEXTURE_2D, s->present_stage_tex);
    s->gl.Uniform1i(s->present_fsr_rcas_sampler, 0);
    s->gl.Uniform2f(s->present_fsr_rcas_texel, 1.0f / ow, 1.0f / oh);
    s->gl.Uniform1f(s->present_fsr_rcas_sharp, s->present_sharp);
    s->gl.EnableVertexAttribArray(0); s->gl.EnableVertexAttribArray(1);
    s->gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    s->gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uvs_vflip);
    s->gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    s->gl.DisableVertexAttribArray(0); s->gl.DisableVertexAttribArray(1);
    s->gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    s->gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    return clean_errors(s);
}

int nj_gles2_present_onscreen(void *ud, const nj_surface *src, int mode,
                              int vp_x, int vp_y, int vp_w, int vp_h)
{
    nj_gles2 *s = (nj_gles2 *)ud;

    if (!s || !src || !src->pixels) return 0;
    if (mode < 0 || mode >= NJ_GL_PRESENT_MODES) return 0;
    if (!current(s) || !present_init(s)) return 0;
    if (vp_w <= 0 || vp_h <= 0 || vp_x < 0 || vp_y < 0) return 0;
    if (vp_x + vp_w > s->max_viewport[0] || vp_y + vp_h > s->max_viewport[1])
        return 0;
    if (!present_upload(s, src)) return 0;

    /* The window path fills the whole drawable: black bars first, then the
     * picture inside the viewport the port computed for the letterbox. */
    s->gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    s->gl.Disable(GL_SCISSOR_TEST);
    s->gl.Viewport(0, 0, s->max_viewport[0], s->max_viewport[1]);
    s->gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    s->gl.Clear(GL_COLOR_BUFFER_BIT);
    s->gl.Viewport(vp_x, vp_y, vp_w, vp_h);
    if (mode == 5)
        return present_fsr_screen(s, src->width, src->height, vp_x, vp_y, vp_w, vp_h);
    /* A window framebuffer's row 0 is the bottom of the visible window, so the
     * texture needs its vertical axis flipped; horizontal orientation matches
     * the CPU framebuffer. */
    return present_draw_quad(s, mode, src->width, src->height, vp_w, vp_h, 2);
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
