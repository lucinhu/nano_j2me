/* Port-side GL dispatch. No process-global loader or context ownership. */
#ifndef NJ_GL_API_H
#define NJ_GL_API_H

/* SDL's Windows development package contains Khronos ES2 declarations even
 * when no separate GLES SDK is installed. This selects declarations only;
 * neither SDL nor GLES libraries are imported by this header. */
#if defined(NJ_GL_USE_SDL_HEADERS) && NJ_GL_USE_SDL_HEADERS
#include <SDL_opengles2_khrplatform.h>
#include <SDL_opengles2_gl2platform.h>
#include <SDL_opengles2_gl2.h>
#else
#include <GLES2/gl2.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (GL_APIENTRY *nj_gl_clear_depth_fn)(double depth);
typedef void (GL_APIENTRY *nj_gl_depth_range_fn)(double near_value, double far_value);

typedef struct nj_gl_api {
    PFNGLACTIVETEXTUREPROC ActiveTexture;
    PFNGLATTACHSHADERPROC AttachShader;
    PFNGLBINDATTRIBLOCATIONPROC BindAttribLocation;
    PFNGLBINDBUFFERPROC BindBuffer;
    PFNGLBINDFRAMEBUFFERPROC BindFramebuffer;
    PFNGLBINDRENDERBUFFERPROC BindRenderbuffer;
    PFNGLBINDTEXTUREPROC BindTexture;
    PFNGLBLENDCOLORPROC BlendColor;
    PFNGLBLENDEQUATIONSEPARATEPROC BlendEquationSeparate;
    PFNGLBLENDFUNCSEPARATEPROC BlendFuncSeparate;
    PFNGLBUFFERDATAPROC BufferData;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus;
    PFNGLCLEARPROC Clear;
    PFNGLCLEARCOLORPROC ClearColor;
    PFNGLCLEARDEPTHFPROC ClearDepthf;
    nj_gl_clear_depth_fn ClearDepth;
    PFNGLCOLORMASKPROC ColorMask;
    PFNGLCOMPILESHADERPROC CompileShader;
    PFNGLCREATEPROGRAMPROC CreateProgram;
    PFNGLCREATESHADERPROC CreateShader;
    PFNGLCULLFACEPROC CullFace;
    PFNGLDELETEBUFFERSPROC DeleteBuffers;
    PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers;
    PFNGLDELETEPROGRAMPROC DeleteProgram;
    PFNGLDELETERENDERBUFFERSPROC DeleteRenderbuffers;
    PFNGLDELETESHADERPROC DeleteShader;
    PFNGLDELETETEXTURESPROC DeleteTextures;
    PFNGLDEPTHFUNCPROC DepthFunc;
    PFNGLDEPTHMASKPROC DepthMask;
    PFNGLDEPTHRANGEFPROC DepthRangef;
    nj_gl_depth_range_fn DepthRange;
    PFNGLDISABLEPROC Disable;
    PFNGLDISABLEVERTEXATTRIBARRAYPROC DisableVertexAttribArray;
    PFNGLDRAWARRAYSPROC DrawArrays;
    PFNGLDRAWELEMENTSPROC DrawElements;
    PFNGLENABLEPROC Enable;
    PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray;
    PFNGLFRAMEBUFFERRENDERBUFFERPROC FramebufferRenderbuffer;
    PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D;
    PFNGLFRONTFACEPROC FrontFace;
    PFNGLGENBUFFERSPROC GenBuffers;
    PFNGLGENFRAMEBUFFERSPROC GenFramebuffers;
    PFNGLGENRENDERBUFFERSPROC GenRenderbuffers;
    PFNGLGENTEXTURESPROC GenTextures;
    PFNGLGETBOOLEANVPROC GetBooleanv;
    PFNGLGETERRORPROC GetError;
    PFNGLGETFLOATVPROC GetFloatv;
    PFNGLGETINTEGERVPROC GetIntegerv;
    PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog;
    PFNGLGETPROGRAMIVPROC GetProgramiv;
    PFNGLGETRENDERBUFFERPARAMETERIVPROC GetRenderbufferParameteriv;
    PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
    PFNGLGETSHADERIVPROC GetShaderiv;
    PFNGLGETSHADERPRECISIONFORMATPROC GetShaderPrecisionFormat;
    PFNGLGETSTRINGPROC GetString;
    PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
    PFNGLGETVERTEXATTRIBIVPROC GetVertexAttribiv;
    PFNGLGETVERTEXATTRIBPOINTERVPROC GetVertexAttribPointerv;
    PFNGLISENABLEDPROC IsEnabled;
    PFNGLISTEXTUREPROC IsTexture;
    PFNGLLINKPROGRAMPROC LinkProgram;
    PFNGLPIXELSTOREIPROC PixelStorei;
    PFNGLREADPIXELSPROC ReadPixels;
    PFNGLRENDERBUFFERSTORAGEPROC RenderbufferStorage;
    PFNGLSCISSORPROC Scissor;
    PFNGLSHADERSOURCEPROC ShaderSource;
    PFNGLTEXIMAGE2DPROC TexImage2D;
    PFNGLTEXPARAMETERIPROC TexParameteri;
    PFNGLTEXSUBIMAGE2DPROC TexSubImage2D;
    PFNGLUNIFORM1FPROC Uniform1f;
    PFNGLUNIFORM1IPROC Uniform1i;
    PFNGLUNIFORM2FPROC Uniform2f;
    PFNGLUNIFORM3FPROC Uniform3f;
    PFNGLUNIFORM4FPROC Uniform4f;
    PFNGLUNIFORMMATRIX4FVPROC UniformMatrix4fv;
    PFNGLUSEPROGRAMPROC UseProgram;
    PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer;
    PFNGLVIEWPORTPROC Viewport;
    int desktop_gl;
} nj_gl_api;

/* Resolve names only while the intended instance's context is current. The
 * resolver owns platform details (e.g. wglGetProcAddress + opengl32 fallback).
 * WGL failure sentinels NULL/1/2/3/-1 are rejected. Core FBO names fall back to
 * EXT then OES aliases. On any required-symbol failure, *out is zeroed.
 * ES2 requires ClearDepthf, DepthRangef and GetShaderPrecisionFormat. Desktop
 * requires the double ClearDepth/DepthRange instead; precision query optional.
 * Float and double entry points are NEVER cast to each other's signature.
 * A populated table is copied by value into an instance, and is invalidated
 * when its associated context/library is destroyed. No GL calls are made. */
int nj_gl_api_load(nj_gl_api *out,
                   void *(*resolve)(void *ud, const char *name),
                   void *ud, int desktop_gl);

/* Populate linked GLES2 symbols; desktop_gl=0. Compile nj_gl_api.c with
 * NJ_GLES2_DYNAMIC_ONLY=1 to make this return 0 (zeroed output), with no linked
 * GL symbol references anywhere in that translation unit. */
int nj_gl_api_default(nj_gl_api *out);

#ifdef __cplusplus
}
#endif
#endif
