#include "nj2me/nj_gl_api.h"
#include <stdint.h>
#include <string.h>

#ifndef NJ_GLES2_DYNAMIC_ONLY
#define NJ_GLES2_DYNAMIC_ONLY 0
#endif

/* ES2 entry points with identical names and signatures on desktop GL. */
#define NJ_GL_COMMON(X) \
    X(ActiveTexture) \
    X(AttachShader) \
    X(BindAttribLocation) \
    X(BindBuffer) \
    X(BindTexture) \
    X(BlendColor) \
    X(BlendEquationSeparate) \
    X(BlendFuncSeparate) \
    X(BufferData) \
    X(Clear) \
    X(ClearColor) \
    X(ColorMask) \
    X(CompileShader) \
    X(CreateProgram) \
    X(CreateShader) \
    X(CullFace) \
    X(DeleteBuffers) \
    X(DeleteProgram) \
    X(DeleteShader) \
    X(DeleteTextures) \
    X(DepthFunc) \
    X(DepthMask) \
    X(Disable) \
    X(DisableVertexAttribArray) \
    X(DrawArrays) \
    X(DrawElements) \
    X(Enable) \
    X(EnableVertexAttribArray) \
    X(FrontFace) \
    X(GenBuffers) \
    X(GenTextures) \
    X(GetBooleanv) \
    X(GetError) \
    X(GetFloatv) \
    X(GetIntegerv) \
    X(GetProgramInfoLog) \
    X(GetProgramiv) \
    X(GetShaderInfoLog) \
    X(GetShaderiv) \
    X(GetString) \
    X(GetUniformLocation) \
    X(GetVertexAttribiv) \
    X(GetVertexAttribPointerv) \
    X(IsEnabled) \
    X(IsTexture) \
    X(LinkProgram) \
    X(PixelStorei) \
    X(ReadPixels) \
    X(Scissor) \
    X(ShaderSource) \
    X(TexImage2D) \
    X(TexParameteri) \
    X(TexSubImage2D) \
    X(Uniform1f) \
    X(Uniform1i) \
    X(Uniform2f) \
    X(Uniform3f) \
    X(Uniform4f) \
    X(UniformMatrix4fv) \
    X(UseProgram) \
    X(VertexAttribPointer) \
    X(Viewport)

#define NJ_GL_FBO(X) \
    X(BindFramebuffer) \
    X(BindRenderbuffer) \
    X(CheckFramebufferStatus) \
    X(DeleteFramebuffers) \
    X(DeleteRenderbuffers) \
    X(FramebufferRenderbuffer) \
    X(FramebufferTexture2D) \
    X(GenFramebuffers) \
    X(GenRenderbuffers) \
    X(GetRenderbufferParameteriv) \
    X(RenderbufferStorage)

/* WGL may return small integers or -1 instead of NULL for unsupported names.
 * Filter them before an alias lookup or copying into a typed function pointer. */
static void *resolve_valid(void *(*resolve)(void *, const char *),
                           void *ud, const char *name)
{
    void *p = resolve(ud, name);
    uintptr_t value = (uintptr_t)p;
    return value > 3 && value != UINTPTR_MAX ? p : NULL;
}

int nj_gl_api_load(nj_gl_api *out,
                   void *(*resolve)(void *ud, const char *name),
                   void *ud, int desktop_gl)
{
    nj_gl_api api;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!resolve) return 0;
    memset(&api, 0, sizeof(api));
    api.desktop_gl = desktop_gl != 0;

/* memcpy avoids undefined/incompatible function-pointer casts and pedantic
 * object/function-pointer conversion diagnostics. The supported GL ABIs use
 * same-sized representations; reject any target that cannot represent these. */
#define NJ_GL_COPY(name, pointer) do { \
    void *nj_proc = (pointer); \
    if (sizeof(api.name) != sizeof(nj_proc)) return 0; \
    memcpy(&api.name, &nj_proc, sizeof(api.name)); \
} while (0)
#define NJ_GL_LOAD(name) do { \
    void *nj_proc_value = resolve_valid(resolve, ud, "gl" #name); \
    if (!nj_proc_value) return 0; \
    NJ_GL_COPY(name, nj_proc_value); \
} while (0);
    NJ_GL_COMMON(NJ_GL_LOAD)
#undef NJ_GL_LOAD

#define NJ_GL_LOAD_FBO(name) do { \
    void *nj_proc_value = resolve_valid(resolve, ud, "gl" #name); \
    if (!nj_proc_value) nj_proc_value = resolve_valid(resolve, ud, "gl" #name "EXT"); \
    if (!nj_proc_value) nj_proc_value = resolve_valid(resolve, ud, "gl" #name "OES"); \
    if (!nj_proc_value) return 0; \
    NJ_GL_COPY(name, nj_proc_value); \
} while (0);
    NJ_GL_FBO(NJ_GL_LOAD_FBO)
#undef NJ_GL_LOAD_FBO

    if (api.desktop_gl) {
        void *depth = resolve_valid(resolve, ud, "glClearDepth");
        void *range = resolve_valid(resolve, ud, "glDepthRange");
        void *precision = resolve_valid(resolve, ud, "glGetShaderPrecisionFormat");
        if (!depth || !range) return 0;
        NJ_GL_COPY(ClearDepth, depth);
        NJ_GL_COPY(DepthRange, range);
        NJ_GL_COPY(GetShaderPrecisionFormat, precision);
    } else {
        void *depth = resolve_valid(resolve, ud, "glClearDepthf");
        void *range = resolve_valid(resolve, ud, "glDepthRangef");
        void *precision = resolve_valid(resolve, ud, "glGetShaderPrecisionFormat");
        if (!depth || !range || !precision) return 0;
        NJ_GL_COPY(ClearDepthf, depth);
        NJ_GL_COPY(DepthRangef, range);
        NJ_GL_COPY(GetShaderPrecisionFormat, precision);
    }
#undef NJ_GL_COPY
    *out = api;
    return 1;
}

int nj_gl_api_default(nj_gl_api *out)
{
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
#if NJ_GLES2_DYNAMIC_ONLY
    return 0;
#else
#define NJ_GL_LINK(name) out->name = gl##name;
    NJ_GL_COMMON(NJ_GL_LINK)
    NJ_GL_FBO(NJ_GL_LINK)
#undef NJ_GL_LINK
    out->ClearDepthf = glClearDepthf;
    out->DepthRangef = glDepthRangef;
    out->GetShaderPrecisionFormat = glGetShaderPrecisionFormat;
    return 1;
#endif
}
