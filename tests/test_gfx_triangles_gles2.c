/* Real EGL/GLES native triangles. No VM, game, production raster or shader is
 * used as an oracle. Double barycentric interpolation + scalar texture/blend
 * equations check pixel centres strictly away from triangle edges. Native edge
 * coverage is checked separately, never inferred from the GPU's output.
 *
 * cc -std=c99 -Wall -Wextra -Werror -DNJ_ENABLE_GFX_ACCEL=1 -Iinclude \
 * tests/test_gfx_triangles_gles2.c src/port/gles2/nj_gfx_gles2.c \
 * src/port/gles2/nj_gl_api.c src/gfx/nj_gfx_triangles.c \
 * src/gfx/nj_gfx_raster.c src/gfx/nj_gfx.c tests/stub_font.c \
 * -lEGL -lGLESv2 -lm -o test_gfx_triangles_gles2
 * EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 ./test_gfx_triangles_gles2
 * Software GL is real GL; renderer is printed, with no physical-GPU claim.
 * Skip 77 only for EGL/independently checked depth/precision capabilities.
 * Shader creation, backend gate failure on a capable context, or a wrong pixel
 * is a FAILURE. No linker wrappers are required; dispatch faults are per test.
 */
#include "nj2me/nj_gfx_gles2.h"
#include "nj2me/nj_gl_api.h"
#include <EGL/egl.h>
#include <GLES2/gl2ext.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if NJ_ENABLE_GFX_ACCEL != 1
#error "Build with NJ_ENABLE_GFX_ACCEL=1"
#endif
/* Optional same oracle against desktop GL / GLSL 1.20 via an EGL context.
 * Mesa: MESA_GL_VERSION_OVERRIDE=2.1 MESA_GLSL_VERSION_OVERRIDE=120.
 * Force the EGL vendor JSON as well as LIBGL_ALWAYS_SOFTWARE to select Mesa
 * on hosts which also expose NVIDIA: GLVND may otherwise select physical GL. */
#ifndef NJ_TEST_DESKTOP_GL
#define NJ_TEST_DESKTOP_GL 0
#endif

static unsigned checks, cases, accepted, declined, interiors, edge_pixels;
static unsigned max_channel_error, max_key_error;
static const char *stage = "EGL setup";
#define CHECK(c) do { ++checks; if (!(c)) { \
    fprintf(stderr,"FAIL %s:%d [%s]: %s\n",__FILE__,__LINE__,stage,#c); exit(1); \
} } while (0)

typedef struct context {
    EGLDisplay display;
    EGLContext ctx[2];
    EGLSurface pbuffer[2];
} context;
typedef struct gate { EGLContext ctx; int enabled; unsigned calls; } gate;
static int current(void *ud)
{
    gate *g = (gate *)ud;
    ++g->calls;
    return g->enabled && eglGetCurrentContext() == g->ctx;
}
static void gl_ok(void) { CHECK(glGetError() == GL_NO_ERROR); }
static void close_egl(context *e)
{
    int i;
    if (e->display == EGL_NO_DISPLAY) return;
    eglMakeCurrent(e->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    for (i=0;i<2;++i) {
        if (e->ctx[i] != EGL_NO_CONTEXT) eglDestroyContext(e->display,e->ctx[i]);
        if (e->pbuffer[i] != EGL_NO_SURFACE) eglDestroySurface(e->display,e->pbuffer[i]);
    }
    eglTerminate(e->display);
}
static int open_egl(context *e)
{
    const EGLint cfg[] = {EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,
        NJ_TEST_DESKTOP_GL?EGL_OPENGL_BIT:EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    const EGLint pbuf[] = {EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE};
    const EGLint attrs[] = {NJ_TEST_DESKTOP_GL?EGL_NONE:EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLConfig config;
    EGLint n, major, minor;
    int i;
    memset(e,0,sizeof *e);
    e->display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (e->display==EGL_NO_DISPLAY || !eglInitialize(e->display,&major,&minor) ||
        !eglBindAPI(NJ_TEST_DESKTOP_GL?EGL_OPENGL_API:EGL_OPENGL_ES_API) ||
        !eglChooseConfig(e->display,cfg,&config,1,&n) || n!=1) goto unavailable;
    for (i=0;i<2;++i) {
        e->pbuffer[i]=eglCreatePbufferSurface(e->display,config,pbuf);
        e->ctx[i]=eglCreateContext(e->display,config,EGL_NO_CONTEXT,attrs);
        if (e->pbuffer[i]==EGL_NO_SURFACE || e->ctx[i]==EGL_NO_CONTEXT ||
            !eglMakeCurrent(e->display,e->pbuffer[i],e->pbuffer[i],e->ctx[i])) goto unavailable;
    }
    printf("EGL %d.%d; vendor=%s; renderer=%s; version=%s\n",major,minor,
        (const char *)glGetString(GL_VENDOR),(const char *)glGetString(GL_RENDERER),
        (const char *)glGetString(GL_VERSION));
    gl_ok(); return 1;
unavailable:
    fprintf(stderr,"SKIP native triangles: EGL unavailable (0x%x)\n",(unsigned)eglGetError());
    close_egl(e); return 0;
}
static void bind_context(const context *e, int i)
{
    CHECK(eglMakeCurrent(e->display,e->pbuffer[i],e->pbuffer[i],e->ctx[i]));
    CHECK(eglGetCurrentContext()==e->ctx[i]); gl_ok();
}
static int extension(const char *extensions, const char *name)
{
    const char *p=extensions;
    size_t len=strlen(name);
    if (!p) return 0;
    while ((p=strstr(p,name))!=NULL) {
        if ((p==extensions || p[-1]==' ') && (!p[len] || p[len]==' ')) return 1;
        p+=len;
    }
    return 0;
}
/* Independent capability preflight: an advertised format alone is insufficient.
 * Shader compilation is deliberately left to the backend and cannot skip. */
static int depth_capable(void)
{
    const char *ext=(const char *)glGetString(GL_EXTENSIONS);
    GLint range[2], precision, bits=0;
    GLuint tex=0, fbo=0, depth=0;
    GLenum status;
    /* Desktop GLSL 1.20 has fragment depth and float32 as core features;
     * its optional ES precision-query entry point must not be required. */
    precision=23;
    if (!NJ_TEST_DESKTOP_GL)
        glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER,GL_HIGH_FLOAT,range,&precision);
    if ((!NJ_TEST_DESKTOP_GL && (!extension(ext,"GL_EXT_frag_depth") ||
            !extension(ext,"GL_OES_depth24"))) || precision<23) {
        fprintf(stderr,"SKIP native triangles: frag_depth=%d depth24=%d highp=%d\n",
            extension(ext,"GL_EXT_frag_depth"),extension(ext,"GL_OES_depth24"),precision);
        gl_ok(); return 0;
    }
    glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
    glGenRenderbuffers(1,&depth); glBindRenderbuffer(GL_RENDERBUFFER,depth);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24_OES,4,4);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
    status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status==GL_FRAMEBUFFER_COMPLETE)
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_DEPTH_SIZE,&bits);
    glBindFramebuffer(GL_FRAMEBUFFER,0); glDeleteRenderbuffers(1,&depth);
    glDeleteFramebuffers(1,&fbo); glDeleteTextures(1,&tex); gl_ok();
    if (status!=GL_FRAMEBUFFER_COMPLETE || bits!=24) {
        fprintf(stderr,"SKIP native triangles: actual D24 FBO=0x%x bits=%d\n",(unsigned)status,bits);
        return 0;
    }
    printf("Independent capabilities: fragment highp=%d, complete D24 framebuffer\n",precision);
    return 1;
}

/* Faults preserve the real driver operation before changing its observable
 * status/currentness. They never replace a shader, fragment or GPU read. */
static unsigned read_at, read_calls;
static unsigned image_calls, sub_calls, storage_calls, sub_at;
static int read_fault, fail_shader;
static gate *fault_gate;
static context *fault_egl;
static void GL_APIENTRY fault_read(GLint x,GLint y,GLsizei w,GLsizei h,
                                   GLenum format,GLenum type,void *pixels)
{
    glReadPixels(x,y,w,h,format,type,pixels);
    ++read_calls;
    if (read_at && read_calls==read_at) {
        if (read_fault==1) glBindTexture((GLenum)0xffffffffu,0);
        else if (read_fault==2) fault_gate->enabled=0;
        else if (read_fault==3)
            CHECK(eglMakeCurrent(fault_egl->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
    }
}
static GLuint GL_APIENTRY fault_shader(GLenum type)
{ return fail_shader ? 0 : glCreateShader(type); }
static void GL_APIENTRY count_image(GLenum target,GLint level,GLint internal,
    GLsizei w,GLsizei h,GLint border,GLenum format,GLenum type,const void *pixels)
{
    ++image_calls;
    glTexImage2D(target,level,internal,w,h,border,format,type,pixels);
}
static void GL_APIENTRY fault_sub(GLenum target,GLint level,GLint x,GLint y,
    GLsizei w,GLsizei h,GLenum format,GLenum type,const void *pixels)
{
    glTexSubImage2D(target,level,x,y,w,h,format,type,pixels);
    ++sub_calls;
    if (sub_at && sub_calls==sub_at) glBindTexture((GLenum)0xffffffffu,0);
}
static void GL_APIENTRY count_storage(GLenum target,GLenum internal,GLsizei w,GLsizei h)
{
    ++storage_calls;
    glRenderbufferStorage(target,internal,w,h);
}
static void *resolve_gl(void *ud,const char *name)
{
    __eglMustCastToProperFunctionPointerType fn=eglGetProcAddress(name);
    void *p=NULL;
    (void)ud;
    if (sizeof p==sizeof fn) memcpy(&p,&fn,sizeof p);
    return p;
}
static nj_gles2 *create(gate *g, size_t budget)
{
    nj_gl_api api;
    nj_gles2_config config;
    nj_gles2 *s;
    if (NJ_TEST_DESKTOP_GL) CHECK(nj_gl_api_load(&api,resolve_gl,NULL,1));
    else CHECK(nj_gl_api_default(&api));
    api.ReadPixels=fault_read; api.CreateShader=fault_shader;
    api.TexImage2D=count_image; api.TexSubImage2D=fault_sub;
    api.RenderbufferStorage=count_storage;
    memset(&config,0,sizeof config); config.is_current=current; config.ud=g;
    config.gl=&api; config.max_pixels=budget; config.desktop_gl=NJ_TEST_DESKTOP_GL;
    s=nj_gles2_create(&config); CHECK(s!=NULL);
    /* The caller's dispatch storage is borrowed only during create. */
    memset(&api,0,sizeof api); gl_ok(); return s;
}
static nj_gles2_stats stats(nj_gles2 *s)
{ nj_gles2_stats r; memset(&r,0,sizeof r); nj_gles2_get_stats(s,&r); return r; }

enum { W=17,H=15,ZS=21,OFFSET=19,BYTES=1536,ZWORDS=340,MAXTRI=4 };
typedef struct canvas {
    unsigned char bytes[BYTES];
    nj_s4 z[ZWORDS];
    nj_surface surface;
    nj_gfx_triangle_cmd tri[MAXTRI];
    nj_gfx_triangles packet;
} canvas;
static int bpp(int fmt) { return fmt==NJ_PIXFMT_RGB565?2:fmt==NJ_PIXFMT_RGB888?3:4; }
static void pack(unsigned char *p,int fmt,nj_u4 color)
{
    if (fmt==NJ_PIXFMT_RGB565) {
        nj_u2 v=(nj_u2)(((color>>8)&0xf800)|((color>>5)&0x7e0)|((color>>3)&31));
        memcpy(p,&v,2);
    } else if (fmt==NJ_PIXFMT_RGB888) {
        p[0]=(unsigned char)(color>>16); p[1]=(unsigned char)(color>>8); p[2]=(unsigned char)color;
    } else memcpy(p,&color,4);
}
static nj_u4 unpack(const unsigned char *p,int fmt)
{
    nj_u4 c;
    if (fmt==NJ_PIXFMT_RGB565) {
        nj_u2 v; unsigned r,g,b; memcpy(&v,p,2);
        r=v>>11; g=(v>>5)&63; b=v&31;
        return 0xff000000u|((r*8+r/4)<<16)|((g*4+g/16)<<8)|(b*8+b/4);
    }
    if (fmt==NJ_PIXFMT_RGB888) return 0xff000000u|((nj_u4)p[0]<<16)|((nj_u4)p[1]<<8)|p[2];
    memcpy(&c,p,4); return c;
}
static unsigned char *pixel(canvas *c,int x,int y)
{ return c->bytes+OFFSET+y*c->surface.stride+x*c->surface.bpp; }
static nj_s4 *key(canvas *c,int x,int y) { return c->z+5+y*ZS+x; }
static void triangle(nj_gfx_triangle_cmd *t,nj_u4 color,double depth)
{
    int i;
    memset(t,0,sizeof *t);
    t->vertices[0].x=t->vertices[0].y=-4;
    t->vertices[1].x=40; t->vertices[1].y=-4;
    t->vertices[2].x=-4; t->vertices[2].y=40;
    for (i=0;i<3;++i) {
        t->vertices[i].depth=depth; t->vertices[i].inv_w=1;
        t->vertices[i].color=color;
    }
    t->blend=68; t->alpha_write=t->depth_test=t->depth_write=1;
    t->wrap_s=t->wrap_t=240; t->filter=208; t->texture_function=228;
}
static void init(canvas *c,int fmt)
{
    int i,x,y;
    memset(c,0,sizeof *c); memset(c->bytes,0xa5,sizeof c->bytes);
    for (i=0;i<ZWORDS;++i) c->z[i]=-1987;
    c->surface.pixels=c->bytes+OFFSET; c->surface.width=W; c->surface.height=H;
    c->surface.format=fmt; c->surface.bpp=bpp(fmt); c->surface.stride=W*bpp(fmt)+7;
    for (y=0;y<H;++y) for (x=0;x<W;++x) {
        pack(pixel(c,x,y),fmt,0x73294b6du); *key(c,x,y)=0;
    }
    triangle(c->tri,0xffb56937u,0.25);
    c->packet.version=NJ_GFX_TRIANGLES_VERSION; c->packet.surface=&c->surface;
    c->packet.depth=c->z+5; c->packet.depth_stride=ZS;
    c->packet.x=2; c->packet.y=3; c->packet.width=11; c->packet.height=9;
    c->packet.triangles=c->tri; c->packet.count=1;
}
static void copy(canvas *to,const canvas *from)
{
    *to=*from; to->surface.pixels=to->bytes+OFFSET;
    to->packet.surface=&to->surface; to->packet.triangles=to->tri;
    to->packet.depth=from->packet.depth?to->z+5:NULL;
}
static double clamp(double x,double lo,double hi) { return x<lo?lo:x>hi?hi:x; }
static unsigned rounded(double x) { return (unsigned)floor(clamp(x,0,255)+0.5); }
static nj_s4 depth_key(double z)
{ return (nj_s4)(1.0+floor((1.0-clamp(z,0,1))*16777215.0+0.5)); }
static double component(nj_u4 c,int n) { return (double)((c>>(24-8*n))&255); }
/* Return -1 outside, 0 on a native coverage boundary, 1 safely interior. */
static int weights(const nj_gfx_triangle_cmd *t,double x,double y,double w[3])
{
    double x0=t->vertices[0].x,y0=t->vertices[0].y;
    double ax=t->vertices[1].x-x0,ay=t->vertices[1].y-y0;
    double bx=t->vertices[2].x-x0,by=t->vertices[2].y-y0;
    double area=ax*by-ay*bx, min_distance=1e30;
    int i;
    if (area==0) return -1;
    w[1]=((x-x0)*by-(y-y0)*bx)/area;
    w[2]=(ax*(y-y0)-ay*(x-x0))/area; w[0]=1-w[1]-w[2];
    for (i=0;i<3;++i) {
        int a=(i+1)%3,b=(i+2)%3;
        double dx=t->vertices[a].x-t->vertices[b].x;
        double dy=t->vertices[a].y-t->vertices[b].y;
        double distance=w[i]*fabs(area)/sqrt(dx*dx+dy*dy);
        if (distance < -0.02) return -1;
        if (distance<min_distance) min_distance=distance;
    }
    return min_distance>0.02?1:0;
}
static int address(int x,int size) { return x<0?0:x>=size?size-1:x; }
static void texel(const nj_gfx_triangle_cmd *t,int x,int y,double out[4])
{
    const unsigned char *p=t->texture+(y*t->texture_width+x)*t->texture_bpp;
    out[0]=t->texture_bpp==4?p[3]:255; out[1]=p[0]; out[2]=p[1]; out[3]=p[2];
}
static void sample(const nj_gfx_triangle_cmd *t,double u,double v,double out[4])
{
    int k;
    u=t->wrap_s==241?u-floor(u):clamp(u,0,1);
    v=t->wrap_t==241?v-floor(v):clamp(v,0,1);
    if (t->filter==208) {
        texel(t,address((int)floor(u*t->texture_width),t->texture_width),
              address((int)floor(v*t->texture_height),t->texture_height),out);
    } else {
        double x=u*t->texture_width-0.5,y=v*t->texture_height-0.5;
        int ix=(int)floor(x),iy=(int)floor(y),a,b;
        double fx=x-ix,fy=y-iy;
        for (k=0;k<4;++k) out[k]=0;
        for (b=0;b<2;++b) for (a=0;a<2;++a) {
            double c[4],f=(a?fx:1-fx)*(b?fy:1-fy);
            texel(t,address(ix+a,t->texture_width),address(iy+b,t->texture_height),c);
            for (k=0;k<4;++k) out[k]+=c[k]*f;
        }
    }
}
/* Mathematical oracle in double precision, distinct from the production
 * scanline raster and fragment programs. The equations follow the public
 * native packet and M3G compositing rules. Intermediate colour is byte-rounded;
 * blend uses integer truncation as the independent software path does. */
static int fragment(const nj_gfx_triangle_cmd *t,const double w[3],nj_u4 dst,
                    nj_u4 *color,nj_s4 *depth)
{
    double c[4]={0,0,0,0}, iw=0,u=0,v=0,z=0;
    unsigned src[4],d[4],o[4],i,k;
    for (i=0;i<3;++i) {
        iw+=w[i]*t->vertices[i].inv_w;
        u+=w[i]*t->vertices[i].inv_w*t->vertices[i].u;
        v+=w[i]*t->vertices[i].inv_w*t->vertices[i].v;
        z+=w[i]*t->vertices[i].depth;
        for (k=0;k<4;++k) c[k]+=w[i]*component(t->vertices[i].color,(int)k);
    }
    if (t->texture) {
        double tex[4],a=c[0]; sample(t,u/iw,v/iw,tex);
        for (k=1;k<4;++k) c[k]=t->texture_function==227?c[k]*tex[k]/255:tex[k];
        if (t->texture_function==226 || (t->texture_function==228 &&
                (t->texture_no_alpha || t->texture_bpp==3))) c[0]=a;
        else c[0]=t->texture_function==228?tex[0]:a*tex[0]/255;
    }
    if (t->fog) {
        double distance=1/iw;
        double f=t->fog==80?exp(-t->fog_density*distance):
            t->fog_far>t->fog_near?(t->fog_far-distance)/(t->fog_far-t->fog_near):1;
        f=clamp(f,0,1);
        for (k=1;k<4;++k) c[k]=c[k]*f+component(t->fog_color,(int)k)*(1-f);
    }
    for (k=0;k<4;++k) { src[k]=rounded(c[k]); d[k]=(unsigned)component(dst,(int)k); }
    if ((int)src[0]<t->alpha_threshold) return 0;
    for (k=1;k<4;++k) {
        if (t->blend==68) o[k]=src[k];
        else if (t->blend==65) o[k]=src[k]*src[0]/255+d[k];
        else if (t->blend==66 || t->blend==67) o[k]=src[k]*d[k]*(t->blend==67?2:1)/255;
        else o[k]=(src[k]*src[0]+d[k]*(255-src[0]))/255;
        if (o[k]>255) o[k]=255;
    }
    o[0]=!t->alpha_write?d[0]:t->blend==68?src[0]:t->blend==64?
        src[0]+d[0]*(255-src[0])/255:255;
    *color=((nj_u4)o[0]<<24)|((nj_u4)o[1]<<16)|((nj_u4)o[2]<<8)|o[3];
    *depth=depth_key(z); return 1;
}
static void oracle(canvas *c,unsigned char uncertain[H][W])
{
    nj_u4 i;
    int x,y;
    memset(uncertain,0,H*W);
    for (i=0;i<c->packet.count;++i) {
        const nj_gfx_triangle_cmd *t=c->tri+i;
        for (y=c->packet.y;y<c->packet.y+c->packet.height;++y)
        for (x=c->packet.x;x<c->packet.x+c->packet.width;++x) {
            double w[3]; nj_u4 color; nj_s4 z;
            int inside=weights(t,x+0.5,y+0.5,w);
            if (!inside) uncertain[y][x]=1;
            if (inside!=1 || !fragment(t,w,unpack(pixel(c,x,y),c->surface.format),&color,&z)) continue;
            if (t->depth_test && z<*key(c,x,y)) continue;
            pack(pixel(c,x,y),c->surface.format,color);
            if (t->depth_write) *key(c,x,y)=z;
        }
    }
}
static void storage(const canvas *want,const canvas *got)
{
    CHECK(memcmp(want->bytes,got->bytes,BYTES)==0);
    CHECK(memcmp(want->z,got->z,sizeof want->z)==0);
    CHECK(memcmp(want->tri,got->tri,sizeof want->tri)==0);
    CHECK(memcmp(&want->surface,&got->surface,sizeof want->surface)==0);
    CHECK(memcmp(&want->packet,&got->packet,sizeof want->packet)==0);
}
static void compare(canvas *want,canvas *got,unsigned char uncertain[H][W],int tolerance)
{
    unsigned char live[BYTES]={0},zlive[ZWORDS]={0};
    int x,y,k;
    size_t i;
    for (y=0;y<H;++y) for (x=0;x<W;++x) {
        nj_u4 a=unpack(pixel(want,x,y),want->surface.format),b=unpack(pixel(got,x,y),got->surface.format);
        size_t offset=(size_t)(pixel(got,x,y)-got->bytes);
        int outside=x<got->packet.x || x>=got->packet.x+got->packet.width ||
            y<got->packet.y || y>=got->packet.y+got->packet.height;
        for (k=0;k<got->surface.bpp;++k) live[offset+(size_t)k]=1;
        zlive[5+y*ZS+x]=1;
        if (uncertain[y][x]) { ++edge_pixels; CHECK(*key(got,x,y)>=0 && *key(got,x,y)<=16777216); continue; }
        ++interiors;
        for (k=0;k<4;++k) {
            unsigned diff=(unsigned)abs((int)component(a,k)-(int)component(b,k));
            /* RGB565 matrix uses exact packed channels. Eight-bit shader
             * arithmetic permits at most one 8-bit channel unit elsewhere. */
            unsigned allowed=outside || got->surface.format==NJ_PIXFMT_RGB565?0u:(unsigned)tolerance;
            if (diff>max_channel_error) max_channel_error=diff;
            if (diff>allowed) {
                fprintf(stderr,"pixel (%d,%d) component %d expected=%08x got=%08x diff=%u\n",x,y,k,a,b,diff);
                CHECK(diff<=allowed);
            }
            ++checks;
        }
        {
            unsigned diff=(unsigned)abs(*key(want,x,y)-*key(got,x,y));
            unsigned allowed=outside?0u:(unsigned)tolerance;
            if (diff>max_key_error) max_key_error=diff;
            if (diff>allowed) {
                fprintf(stderr,"depth (%d,%d) expected=%d got=%d\n",x,y,*key(want,x,y),*key(got,x,y));
                CHECK(diff<=allowed);
            }
            ++checks;
        }
    }
    for (i=0;i<BYTES;++i) if (!live[i]) CHECK(want->bytes[i]==got->bytes[i]);
    for (i=0;i<ZWORDS;++i) if (!zlive[i]) CHECK(want->z[i]==got->z[i]);
    CHECK(memcmp(want->tri,got->tri,sizeof want->tri)==0);
}
static void accept(nj_gles2 *s,canvas *c,int tolerance)
{
    canvas want;
    unsigned char uncertain[H][W];
    nj_gfx_triangles saved_packet=c->packet;
    nj_surface saved_surface=c->surface;
    nj_u4 submitted=0xdeadbeefu;
    nj_gles2_stats before=stats(s),after;
    copy(&want,c); oracle(&want,uncertain);
    CHECK(nj_gles2_triangles(s,&c->packet,&submitted)==NJ_GFX_ACCEL_DONE);
    CHECK(submitted==c->packet.count);
    CHECK(memcmp(&saved_packet,&c->packet,sizeof saved_packet)==0);
    CHECK(memcmp(&saved_surface,&c->surface,sizeof saved_surface)==0);
    compare(&want,c,uncertain,tolerance); gl_ok();
    after=stats(s); CHECK(after.batches==before.batches+1);
    CHECK(after.triangles==before.triangles+submitted && after.declines==before.declines);
    ++cases; ++accepted;
}
static void decline(nj_gles2 *s,canvas *c,nj_gfx_triangles *packet,nj_u4 *result)
{
    canvas saved=*c;
    nj_u4 before=result?*result:0;
    nj_gles2_stats a=stats(s),b;
    CHECK(nj_gles2_triangles(s,packet,result)==NJ_GFX_ACCEL_DECLINE);
    storage(&saved,c); if (result) CHECK(*result==before);
    b=stats(s); CHECK(b.batches==a.batches && b.triangles==a.triangles);
    ++declined;
}
static void basic_and_geometry(nj_gles2 *s)
{
    int fmt,i;
    static const float xy[][6]={{1,1,14,2,3,13},{3,13,14,2,1,1},
        {-10,8,9,-10,28,25},{-100,-100,-90,-100,-100,-90},
        {2.25f,3.25f,12.75f,3.25f,2.25f,11.75f}};
    char label[100];
    for (fmt=NJ_PIXFMT_RGB565;fmt<=NJ_PIXFMT_ARGB8888;++fmt) {
        canvas c;
        stage="basic formats + clip + colour/depth padding"; init(&c,fmt); accept(s,&c,0);
        for (i=0;i<5;++i) {
            int j; init(&c,fmt);
            snprintf(label,sizeof label,"native geometry fmt=%d shape=%d",fmt,i); stage=label;
            for (j=0;j<3;++j) { c.tri[0].vertices[j].x=xy[i][2*j]; c.tri[0].vertices[j].y=xy[i][2*j+1]; }
            accept(s,&c,0);
        }
    }
}
static void textures(nj_gles2 *s)
{
    unsigned char tex[24]={0},saved[24];
    int filter,ws,wt,func,bytes,noalpha,pattern,mutation,i;
    char label[160];
    for (filter=208;filter<=209;++filter) for (ws=240;ws<=241;++ws)
    for (wt=240;wt<=241;++wt) for (func=226;func<=228;++func)
    for (bytes=3;bytes<=4;++bytes) for (noalpha=0;noalpha<2;++noalpha)
    for (pattern=0;pattern<3;++pattern) for (mutation=0;mutation<2;++mutation) {
        canvas c;
        snprintf(label,sizeof label,"texture filter=%d wrap=%d/%d func=%d bpp=%d noalpha=%d pattern=%d mutation=%d",
            filter,ws,wt,func,bytes,noalpha,pattern,mutation); stage=label;
        for (i=0;i<6;++i) {
            tex[i*bytes]=(unsigned char)(23+19*i+mutation*53);
            tex[i*bytes+1]=(unsigned char)(147-17*i+mutation*29);
            tex[i*bytes+2]=(unsigned char)(71+11*i+mutation*37);
            if (bytes==4) tex[i*bytes+3]=(unsigned char)(31+31*i);
        }
        memcpy(saved,tex,sizeof tex); init(&c,NJ_PIXFMT_ARGB8888);
        c.tri[0].texture=tex; c.tri[0].texture_width=3; c.tri[0].texture_height=2;
        c.tri[0].texture_bpp=bytes; c.tri[0].texture_no_alpha=noalpha;
        c.tri[0].filter=filter; c.tri[0].wrap_s=ws; c.tri[0].wrap_t=wt;
        c.tri[0].texture_function=func;
        for (i=0;i<3;++i) c.tri[0].vertices[i].color=0x9f85b369u;
        c.tri[0].vertices[0].u=pattern==0?0.25f:pattern==1?-1.3f:1.15f;
        /* Avoid intentionally exact repeat seams: either adjacent texel is
         * legitimate under native interpolation at a discontinuity. */
        c.tri[0].vertices[0].v=pattern==0?0.35f:pattern==1?1.713f:-0.659f;
        c.tri[0].vertices[1].u=pattern==0?0.65f:2.813f;
        c.tri[0].vertices[1].v=pattern==0?0.65f:-1.073f;
        c.tri[0].vertices[2].u=pattern==0?0.35f:-0.817f;
        c.tri[0].vertices[2].v=pattern==0?0.85f:2.831f;
        if (pattern==2) {
            c.tri[0].vertices[0].inv_w=0.25f;
            c.tri[0].vertices[1].inv_w=1.75f;
            c.tri[0].vertices[2].inv_w=0.75f;
        }
        accept(s,&c,1); CHECK(memcmp(tex,saved,sizeof tex)==0);
    }
}
static void compositing(nj_gles2 *s)
{
    static const int alphas[]={0,1,64,128,254,255};
    int mode,aw,a,fog;
    char label[100];
    for (mode=64;mode<=68;++mode) for (aw=0;aw<2;++aw)
    for (a=0;a<6;++a) for (fog=0;fog<3;++fog) {
        canvas c;
        snprintf(label,sizeof label,"blend=%d alpha_write=%d alpha=%d fog=%d",mode,aw,alphas[a],fog); stage=label;
        init(&c,NJ_PIXFMT_ARGB8888); c.tri[0].blend=mode; c.tri[0].alpha_write=aw;
        c.tri[0].fog=fog==0?0:fog==1?80:81;
        c.tri[0].fog_color=0x004367b1; c.tri[0].fog_density=0.39f;
        c.tri[0].fog_near=1.25f; c.tri[0].fog_far=5.25f;
        c.tri[0].vertices[0].color=((nj_u4)alphas[a]<<24)|0x00a94581u;
        c.tri[0].vertices[1].color=((nj_u4)alphas[a]<<24)|0x0051c559u;
        c.tri[0].vertices[2].color=((nj_u4)alphas[a]<<24)|0x00d987c3u;
        c.tri[0].vertices[0].inv_w=0.25f; c.tri[0].vertices[1].inv_w=0.625f;
        c.tri[0].vertices[2].inv_w=0.125f;
        accept(s,&c,1);
    }
}
static void packed_compositing(nj_gles2 *s)
{
    int fmt,mode,aw;
    char label[100];
    for (fmt=NJ_PIXFMT_RGB565;fmt<=NJ_PIXFMT_RGB888;++fmt)
    for (mode=64;mode<=68;++mode) for (aw=0;aw<2;++aw) {
        canvas c;
        snprintf(label,sizeof label,"packed format=%d blend=%d alpha_write=%d",fmt,mode,aw); stage=label;
        init(&c,fmt); triangle(c.tri,0x9fb56937u,0.25);
        c.tri[0].blend=mode; c.tri[0].alpha_write=aw;
        /* Constant byte inputs remove interpolation ambiguity: RGB565 is
         * required to match packed output exactly, with zero tolerance. */
        accept(s,&c,fmt==NJ_PIXFMT_RGB565?0:1);
    }
}
static void depths(nj_gles2 *s)
{
    static const nj_s4 boundary[]={0,1,2,254,255,256,65534,65535,65536,8388608,16777214,16777215,16777216};
    int i,j,test,write,x,y;
    char label[100];
    for (i=1;i<13;++i) for (j=0;j<13;++j)
    for (test=0;test<2;++test) for (write=0;write<2;++write) {
        canvas c;
        snprintf(label,sizeof label,"D24 new=%d seed=%d test=%d write=%d",boundary[i],boundary[j],test,write); stage=label;
        init(&c,NJ_PIXFMT_ARGB8888);
        triangle(c.tri,0xffb56937u,1.0-(boundary[i]-1)/16777215.0);
        c.tri[0].depth_test=test; c.tri[0].depth_write=write;
        for (y=0;y<H;++y) for (x=0;x<W;++x) *key(&c,x,y)=boundary[j];
        accept(s,&c,0);
    }
    stage="finite pre-offset depths clamp per fragment, not at vertices";
    for (i=0;i<3;++i) {
        canvas c;
        init(&c,NJ_PIXFMT_ARGB8888);
        c.tri[0].vertices[0].depth=i==0?-3:i==1?4:-0.25;
        c.tri[0].vertices[1].depth=i==0?-3:i==1?4:2.5;
        c.tri[0].vertices[2].depth=i==0?-3:i==1?4:0.5;
        /* Binary-exact 1/32 barycentric increments isolate D24 error from
         * ill-conditioned native interpolation of 1/44 weights. */
        c.tri[0].vertices[1].x=28; c.tri[0].vertices[2].y=28;
        accept(s,&c,i==2?1:0);
    }
    stage="depthless packet and no CPU depth writes";
    { canvas c; init(&c,NJ_PIXFMT_RGB888); c.packet.depth=NULL;
      c.tri[0].depth_test=c.tri[0].depth_write=0; accept(s,&c,0); }
}
static void transparency(nj_gles2 *s)
{
    static const unsigned alpha[]={0,127,128,255};
    int a,reverse;
    for (a=0;a<4;++a) for (reverse=0;reverse<2;++reverse) {
        canvas c;
        unsigned char tex[4]={211,39,103,0};
        stage=reverse?"opaque pass then translucent depth-test-only pass":"cutout near then far: discard must not write depth";
        init(&c,NJ_PIXFMT_ARGB8888); c.packet.count=2; tex[3]=(unsigned char)alpha[a];
        triangle(c.tri,0xffb56937u,0.25); triangle(c.tri+1,0xff2d7bc9u,0.75);
        c.tri[0].texture=tex; c.tri[0].texture_width=c.tri[0].texture_height=1;
        c.tri[0].texture_bpp=4; c.tri[0].alpha_threshold=128;
        if (reverse) {
            nj_gfx_triangle_cmd swap=c.tri[0]; c.tri[0]=c.tri[1]; c.tri[1]=swap;
            c.tri[1].blend=64; c.tri[1].depth_write=0;
        }
        accept(s,&c,1);
    }
}
static void two_d_mix(nj_gles2 *s,canvas *c)
{
    canvas want;
    nj_gfx g;
    nj_gfx_blit b;
    nj_u4 source[4]={0xff123456u,0xff718b39u,0xffca619bu,0xff374951u};
    int x,y;
    stage="native / independent 2D fill+blit / exact fragment storage interop";
    copy(&want,c); nj_gfx_init(&g,&c->surface);
    nj_gfx_set_clip(&g,2,3,11,9); nj_gfx_set_color(&g,0xff3b5987u);
    CHECK(nj_gles2_backend()->fill_rect(s,&g,4,5,3,2)==NJ_GFX_ACCEL_DONE);
    for (y=5;y<7;++y) for (x=4;x<7;++x)
        pack(pixel(&want,x,y),want.surface.format,0xff3b5987u);
    memset(&b,0,sizeof b); b.pixels=(const nj_u1 *)source; b.stride=8;
    b.format=NJ_PIXFMT_ARGB8888; b.width=b.height=2;
    b.dx=6; b.dy=7; b.alpha=255; b.rop=NJ_ROP_SET;
    /* Descriptor clip is the final touched rectangle, as emitted by the core. */
    b.clip_x=6; b.clip_y=7; b.clip_w=2; b.clip_h=2;
    CHECK(nj_gles2_backend()->blit(s,&g,&b)==NJ_GFX_ACCEL_DONE);
    for (y=0;y<2;++y) for (x=0;x<2;++x)
        pack(pixel(&want,x+6,y+7),want.surface.format,source[2*y+x]);
    CHECK(memcmp(want.bytes,c->bytes,sizeof c->bytes)==0);
    CHECK(memcmp(want.z,c->z,sizeof c->z)==0);
}
static void exact_mix(nj_gles2 *s)
{
    canvas c;
    nj_gfx_raster r;
    nj_s4 keys[9*11];
    nj_u4 drawn;
    int i,x,y;
    stage="CPU edits / native triangles / exact fragments share authoritative storage";
    init(&c,NJ_PIXFMT_ARGB8888); accept(s,&c,0);
    for (i=0;i<3;++i) {
        two_d_mix(s,&c);
        memset(&r,0,sizeof r); r.version=NJ_GFX_RASTER_VERSION; r.surface=&c.surface;
        r.depth=c.packet.depth; r.depth_stride=ZS; r.x=2; r.y=3; r.width=11; r.height=9;
        r.keys=keys; r.key_stride=11; r.color=0xff416bcdu; r.depth_test=r.depth_write=1;
        for (y=0;y<9;++y) for (x=0;x<11;++x) keys[y*11+x]=(x+y)%3?16777216:0;
        drawn=0xabcdef01u; CHECK(nj_gles2_raster(s,&r,&drawn)==NJ_GFX_ACCEL_DONE && drawn==1);
        for (y=0;y<9;++y) for (x=0;x<11;++x) if (keys[y*11+x]) {
            CHECK(unpack(pixel(&c,x+2,y+3),c.surface.format)==r.color);
            CHECK(*key(&c,x+2,y+3)==16777216);
        }
        /* Force fresh CPU colour AND depth import at the same addresses. */
        pack(pixel(&c,4,5),c.surface.format,0x3157399bu); *key(&c,4,5)=1;
        triangle(c.tri,(nj_u4)(0xff6b395du+i*0x10101u),0.375); accept(s,&c,0);
    }
}
static void faults(context *e,gate *g,nj_gles2 *s)
{
    int mode,at;
    for (mode=1;mode<=3;++mode) for (at=1;at<=2;++at) {
        canvas c;
        nj_u4 submitted=0x61234567u;
        stage="late real GL read error/current loss is atomic";
        init(&c,NJ_PIXFMT_ARGB8888); read_calls=0; read_at=(unsigned)at;
        read_fault=mode; fault_gate=g; fault_egl=e;
        decline(s,&c,&c.packet,&submitted); CHECK(read_calls>=(unsigned)at);
        read_at=0; read_fault=0; g->enabled=1; bind_context(e,0);
        accept(s,&c,0);
    }
    stage="lazy native shader failure cannot skip and preserves all CPU state";
    {
        canvas c;
        nj_u4 submitted=0x81234567u;
        nj_gles2 *fresh=create(g,0);
        init(&c,NJ_PIXFMT_ARGB8888); fail_shader=1;
        decline(fresh,&c,&c.packet,&submitted); fail_shader=0;
        accept(fresh,&c,0); CHECK(nj_gles2_destroy(fresh)); gl_ok();
    }
}
static void warm_uploads(nj_gles2 *s)
{
    canvas c;
    unsigned char tex[16]={137,57,91,255,137,57,91,255,
                           137,57,91,255,137,57,91,255};
    unsigned images,storage,subs;
    int at;
    stage="warm texture storage reuse observes same-pointer bytes on every call";
    init(&c,NJ_PIXFMT_ARGB8888);
    c.tri[0].texture=tex; c.tri[0].texture_width=c.tri[0].texture_height=2;
    c.tri[0].texture_bpp=4;
    accept(s,&c,0);
    images=image_calls; storage=storage_calls; subs=sub_calls;
    tex[0]=tex[4]=tex[8]=tex[12]=71;
    accept(s,&c,0);
    CHECK(image_calls==images && storage_calls==storage);
    CHECK(sub_calls>=subs+3); /* CPU colour, CPU keys, borrowed source texture. */
    for (at=1;at<=3;++at) {
        nj_u4 submitted=0x67890123u;
        stage="warm target/key/source TexSubImage errors remain atomic and retryable";
        sub_at=sub_calls+(unsigned)at;
        decline(s,&c,&c.packet,&submitted);
        CHECK(sub_calls>=sub_at);
        sub_at=0;
        tex[1]=tex[5]=tex[9]=tex[13]=(unsigned char)(71+at*13);
        accept(s,&c,0);
        gl_ok();
    }
    stage="texture slot count, dimensions and RGB/RGBA changes replace whole sets";
    for (at=0;at<9;++at) {
        int i;
        unsigned char other[16]={91,71,137,255,91,71,137,255,
                                 91,71,137,255,91,71,137,255};
        c.packet.count=(nj_u4)(1+at%3);
        for (i=0;i<(int)c.packet.count;++i) {
            c.tri[i]=c.tri[0];
            c.tri[i].texture=i==1?other:tex;
            c.tri[i].texture_bpp=(at+i)%2?3:4;
            c.tri[i].texture_width=at%2?1:2;
            c.tri[i].texture_height=2;
            c.tri[i].vertices[0].depth=c.tri[i].vertices[1].depth=
                c.tri[i].vertices[2].depth=0.125*(3-i);
        }
        accept(s,&c,0);
    }
}
static void invalid(nj_gles2 *s)
{
    int i;
    stage="invalid descriptor/alias/texture/coordinate transactions never write";
    for (i=0;i<15;++i) {
        canvas c;
        nj_gfx_triangles p;
        nj_u4 submitted=0x91234567u,*out=&submitted;
        init(&c,NJ_PIXFMT_ARGB8888); p=c.packet;
        switch (i) {
        case 0:p.version=2;break; case 1:p.x=-1;break;
        case 2:p.width=W;break; case 3:p.count=0;break;
        case 4:p.depth=NULL;break; case 5:p.depth_stride=W-1;break;
        case 6:c.tri[0].vertices[0].inv_w=0;break;
        case 7:c.tri[0].vertices[0].depth=NAN;break;
        case 8:c.tri[0].vertices[0].x=INFINITY;break;
        case 9:c.tri[0].blend=63;break;
        case 10:c.tri[0].texture=c.surface.pixels;c.tri[0].texture_width=1;
            c.tri[0].texture_height=1;c.tri[0].texture_bpp=4;break;
        case 11:p.depth=(nj_s4 *)c.surface.pixels;break;
        case 12:out=NULL;break; case 13:out=(nj_u4 *)(c.z+5);break;
        case 14:*key(&c,p.x,p.y)=16777217;break;
        }
        decline(s,&c,&p,out); gl_ok();
    }
}
static void lifecycle(context *e,gate g[2],nj_gles2 *s[2])
{
    canvas c;
    nj_u4 submitted=0x71234567u;
    nj_gles2_stats before,after;
    nj_gles2 *small;
    stage="independent instances and contexts";
    init(&c,NJ_PIXFMT_RGB888); before=stats(s[0]);
    bind_context(e,1); accept(s[1],&c,0); after=stats(s[0]);
    CHECK(memcmp(&before,&after,sizeof before)==0);
    decline(s[0],&c,&c.packet,&submitted); CHECK(nj_gles2_destroy(s[0])==0);
    CHECK(eglMakeCurrent(e->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
    decline(s[0],&c,&c.packet,&submitted); bind_context(e,0);
    g[0].enabled=0; decline(s[0],&c,&c.packet,&submitted); g[0].enabled=1;
    accept(s[0],&c,0);
    stage="allocation budget leaves transaction unchanged";
    small=create(g,11*9-1); decline(small,&c,&c.packet,&submitted);
    CHECK(nj_gles2_destroy(small)); gl_ok();
    stage="abandon only after actual owning context destruction";
    CHECK(eglGetCurrentContext()==e->ctx[0]);
    CHECK(eglDestroyContext(e->display,e->ctx[1])); e->ctx[1]=EGL_NO_CONTEXT;
    CHECK(eglDestroySurface(e->display,e->pbuffer[1])); e->pbuffer[1]=EGL_NO_SURFACE;
    nj_gles2_abandon(s[1]); s[1]=NULL; gl_ok();
    CHECK(eglGetCurrentContext()==e->ctx[0]);
}
/* Ordered list path must equal the same packets submitted one at a time. */
static void lists(nj_gles2 *s)
{
    canvas one,all;
    nj_gfx_triangle_cmd tri2;
    nj_gfx_triangles p0,p1,q1;
    const nj_gfx_triangles *packets[2];
    nj_gfx_triangles_list list;
    nj_u4 submitted;
    stage="list";
    init(&one,NJ_PIXFMT_ARGB8888);
    init(&all,NJ_PIXFMT_ARGB8888);
    p0=all.packet;
    p1=p0;
    triangle(&tri2,0xff2a6b3cu,0.6);
    p1.triangles=&tri2;
    p1.x=4; p1.y=4; p1.width=13; p1.height=11;
    packets[0]=&p0;
    packets[1]=&p1;
    list.version=NJ_GFX_TRIANGLES_LIST_VERSION;
    list.surface=&all.surface;
    list.depth=all.z+5;
    list.depth_stride=ZS;
    list.packets=packets;
    list.count=2;
    submitted=7;
    CHECK(nj_gfx_triangles_list_validate(&list,&submitted));
    CHECK(submitted==7);
    q1=p1;
    q1.surface=&one.surface;
    packets[1]=&q1;
    CHECK(!nj_gfx_triangles_list_validate(&list,&submitted));
    packets[1]=&p1;
    submitted=0;
    CHECK(nj_gles2_triangles_list(s,&list,&submitted)==NJ_GFX_ACCEL_DONE);
    CHECK(submitted==2);
    CHECK(nj_gles2_triangles_list(NULL,&list,&submitted)==NJ_GFX_ACCEL_DECLINE);
    submitted=0;
    CHECK(nj_gles2_triangles(s,&one.packet,&submitted)==NJ_GFX_ACCEL_DONE);
    q1=p1;
    q1.surface=&one.surface;
    q1.depth=one.z+5;
    submitted=0;
    CHECK(nj_gles2_triangles(s,&q1,&submitted)==NJ_GFX_ACCEL_DONE);
    CHECK(memcmp(one.bytes,all.bytes,sizeof one.bytes)==0);
    CHECK(memcmp(one.z,all.z,sizeof all.z)==0);
    list.count=1;
    submitted=0;
    CHECK(nj_gles2_triangles_list(s,&list,&submitted)==NJ_GFX_ACCEL_DONE);
    CHECK(submitted==1);
    cases++;
}

int main(void)
{
    context e;
    gate g[2];
    nj_gles2 *s[2];
    int i;
    if (!open_egl(&e)) return 77;
    if (!depth_capable()) { close_egl(&e); return 77; }
    memset(g,0,sizeof g);
    for (i=0;i<2;++i) {
        bind_context(&e,i); g[i].ctx=e.ctx[i]; g[i].enabled=1; s[i]=create(g+i,0);
    }
    bind_context(&e,0); basic_and_geometry(s[0]); textures(s[0]); compositing(s[0]);
    packed_compositing(s[0]); depths(s[0]); transparency(s[0]); exact_mix(s[0]); invalid(s[0]);
    faults(&e,g,s[0]); warm_uploads(s[0]); lists(s[0]); lifecycle(&e,g,s);
    for (i=0;i<2;++i) {
        CHECK(g[i].calls>0);
        if (s[i]) { bind_context(&e,i); CHECK(nj_gles2_destroy(s[i])); gl_ok(); }
    }
    CHECK(nj_gles2_destroy(NULL)); close_egl(&e);
    printf("PASS real GL native triangles: %u cases %u checks, DONE=%u atomic declines=%u; "
        "checked pixels=%u native edge pixels=%u max8bitChannelError=%u maxDepthKeyError=%u\n",
        cases,checks,accepted,declined,interiors,edge_pixels,max_channel_error,max_key_error);
    return 0;
}
