/* Real EGL/GLES2 exact-coverage merge conformance, including production 3D.
 * Run EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 on Mesa. Renderer is
 * logged; software GL is real GL, with no hardware/performance claim.
 * Link --wrap=realloc --wrap=glReadPixels --wrap=glCreateShader for faults.
 * Only EGL context initialization may skip (77). All checks survive NDEBUG. */
#include "nj2me/nj_gfx_gles2.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if NJ_ENABLE_GFX_ACCEL != 1 || !NJ_ENABLE_M3G || !NJ_ENABLE_MASCOT
#error "Requires graphics acceleration, M3G and Mascot in the linked core"
#endif
/* Reuse only the public M3G target fixture; its original main remains callable
 * but this test installs an actual GL callback, not a software protocol mock. */
#define main m3g_raster_target_fixture_main
#include "test_m3g_targets.c"
#undef main

static unsigned r_checks, r_cases, r_accepts, r_declines, r_gpu_m3g, r_gpu_mascot;
static const char *r_stage = "initialization";
#define RCHECK(c) do { ++r_checks; if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d [%s]: %s\n", __FILE__, __LINE__, r_stage, #c); exit(1); \
} } while (0)

/* Faults occur in executable-linked backend calls only, never inside the GL
 * driver's own allocator. The readback is real before the late error/corruption. */
static unsigned r_oom_at, r_realloc_calls, r_read_at, r_read_calls;
static int r_read_fault, r_shader_fail;
static int *r_shader_gate;
static GLuint r_retained_shader;
void *__real_realloc(void *, size_t);
void *__wrap_realloc(void *p, size_t n)
{
    if (r_oom_at && ++r_realloc_calls == r_oom_at) return NULL;
    return __real_realloc(p, n);
}
void __real_glReadPixels(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
void __wrap_glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h,
                         GLenum format, GLenum type, void *out)
{
    __real_glReadPixels(x, y, w, h, format, type, out);
    if (r_read_at && ++r_read_calls == r_read_at) {
        if (r_read_fault == 1) glBindTexture((GLenum)0xffffffffu, 0);
        else if (r_read_fault == 2) ((nj_u1 *)out)[(size_t)w*(size_t)h*4u-1u] = 127;
        else if (r_read_fault == 3) ((nj_u1 *)out)[0] = 255;
    }
}
GLuint __real_glCreateShader(GLenum);
GLuint __wrap_glCreateShader(GLenum type)
{
    if (r_shader_fail && --r_shader_fail == 0) {
        if (r_shader_gate) *r_shader_gate=0;
        return 0;
    }
    {
        GLuint name=__real_glCreateShader(type);
        if(r_shader_gate) r_retained_shader=name;
        return name;
    }
}

typedef struct r_egl {
    EGLDisplay display;
    EGLContext context[2];
    EGLSurface surface[2];
} r_egl;
typedef struct r_gate { EGLContext context; int enabled; unsigned calls; } r_gate;
static int r_current(void *ud)
{
    r_gate *g = (r_gate *)ud;
    ++g->calls;
    return g->enabled && eglGetCurrentContext() == g->context;
}
static void r_gl_ok(void) { RCHECK(glGetError() == GL_NO_ERROR); }
static void r_close_egl(r_egl *e)
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
static int r_open_egl(r_egl *e)
{
    static const EGLint ca[] = {EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,
        EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    static const EGLint pa[] = {EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE};
    static const EGLint xa[] = {EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLConfig config;
    EGLint count, major, minor;
    const char *stage = "eglGetDisplay";
    int i;
    memset(e, 0, sizeof *e);
    e->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (e->display == EGL_NO_DISPLAY) goto unavailable;
    stage = "eglInitialize";
    if (!eglInitialize(e->display, &major, &minor)) goto unavailable;
    stage = "eglBindAPI/eglChooseConfig";
    if (!eglBindAPI(EGL_OPENGL_ES_API) ||
        !eglChooseConfig(e->display, ca, &config, 1, &count) || count != 1) goto unavailable;
    for (i = 0; i < 2; ++i) {
        stage = "eglCreatePbufferSurface";
        e->surface[i] = eglCreatePbufferSurface(e->display, config, pa);
        if (e->surface[i] == EGL_NO_SURFACE) goto unavailable;
        stage = "eglCreateContext/eglMakeCurrent";
        e->context[i] = eglCreateContext(e->display, config, EGL_NO_CONTEXT, xa);
        if (e->context[i] == EGL_NO_CONTEXT ||
            !eglMakeCurrent(e->display,e->surface[i],e->surface[i],e->context[i])) goto unavailable;
    }
    printf("EGL %d.%d; vendor=%s; renderer=%s; version=%s\n", major, minor,
           (const char *)glGetString(GL_VENDOR), (const char *)glGetString(GL_RENDERER),
           (const char *)glGetString(GL_VERSION));
    r_gl_ok();
    return 1;
unavailable:
    fprintf(stderr,"SKIP GLES2 raster: EGL context unavailable at %s (0x%x); "
            "try EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1\n",stage,(unsigned)eglGetError());
    r_close_egl(e);
    return 0;
}
static void r_bind(const r_egl *e, int i)
{
    RCHECK(eglMakeCurrent(e->display,e->surface[i],e->surface[i],e->context[i]));
    RCHECK(eglGetCurrentContext() == e->context[i]);
    r_gl_ok();
}
static nj_gles2 *r_create(r_gate *gate, size_t budget)
{
    nj_gles2_config c = {0};
    nj_gles2 *s;
    c.is_current = r_current; c.ud = gate; c.max_pixels = budget;
    s = nj_gles2_create(&c); RCHECK(s != NULL); r_gl_ok(); return s;
}
static nj_gles2_stats r_stats(nj_gles2 *s)
{
    nj_gles2_stats p; nj_gles2_get_stats(s, &p); return p;
}

enum { R_GUARD=17, R_BYTES=2048, R_DEPTH=256, R_KEYS=128, R_W=13, R_H=11, R_ZSTRIDE=16 };
typedef struct r_canvas {
    nj_u1 bytes[R_BYTES];
    nj_s4 depth[R_DEPTH], keys[R_KEYS];
    nj_surface surface;
    nj_gfx_raster packet;
} r_canvas;
static const nj_s4 r_boundary[] = {0,1,254,255,256,65534,65535,65536,16777215,16777216};
static void r_pack(nj_u1 *p, int format, nj_u4 c)
{
    if (format == NJ_PIXFMT_RGB565) {
        nj_u2 v = (nj_u2)(((c>>8)&0xf800u)|((c>>5)&0x7e0u)|((c>>3)&31u));
        memcpy(p,&v,2);
    } else if (format == NJ_PIXFMT_RGB888) {
        p[0]=(nj_u1)(c>>16); p[1]=(nj_u1)(c>>8); p[2]=(nj_u1)c;
    } else memcpy(p,&c,4);
}
static void r_init(r_canvas *c, int fmt, int width, int height)
{
    int i,x,y,bpp=fmt==NJ_PIXFMT_RGB565?2:fmt==NJ_PIXFMT_RGB888?3:4;
    memset(c,0,sizeof *c); memset(c->bytes,0xa5,sizeof c->bytes);
    for(i=0;i<R_DEPTH;++i) c->depth[i]=-181;
    for(i=0;i<R_KEYS;++i) c->keys[i]=-193;
    c->surface.pixels=c->bytes+R_GUARD;
    c->surface.width=R_W; c->surface.height=R_H; c->surface.bpp=bpp;
    c->surface.format=fmt; c->surface.stride=(R_W*bpp+5)|1;
    for(y=0;y<R_H;++y) for(x=0;x<R_W;++x) {
        r_pack(c->bytes+R_GUARD+y*c->surface.stride+x*bpp,fmt,
               ((nj_u4)((x+3*y)%4)*85u<<24)|(0x153971u+(nj_u4)(x*0x172309+y*0x390b17)));
        c->depth[5+y*R_ZSTRIDE+x]=0;
    }
    c->packet.version=NJ_GFX_RASTER_VERSION; c->packet.surface=&c->surface;
    c->packet.x=2; c->packet.y=3; c->packet.width=width; c->packet.height=height;
    c->packet.keys=c->keys+3; c->packet.key_stride=width+3;
    c->packet.depth=c->depth+5; c->packet.depth_stride=R_ZSTRIDE;
    c->packet.depth_test=c->packet.depth_write=1; c->packet.color=0xff39b7e5u;
    for(y=0;y<height;++y) for(x=0;x<width;++x)
        c->keys[3+y*c->packet.key_stride+x]=1;
}
static void r_copy(r_canvas *to, const r_canvas *from)
{
    *to=*from; to->surface.pixels=to->bytes+R_GUARD; to->packet.surface=&to->surface;
    to->packet.keys=to->keys+3;
    to->packet.depth=from->packet.depth?to->depth+5:NULL;
}
/* Independent expected merge: no core raster validator, merge, pixel helper or
 * GPU-derived mask/depth determines the expected coverage, colour or drawn. */
static nj_u4 r_oracle(r_canvas *c)
{
    nj_gfx_raster *p=&c->packet;
    nj_u4 drawn=0;
    int x,y;
    for(y=0;y<p->height;++y) for(x=0;x<p->width;++x) {
        nj_s4 key=c->keys[3+y*p->key_stride+x];
        nj_s4 *z=c->depth+5+(y+p->y)*R_ZSTRIDE+x+p->x;
        if(key && (!p->depth_test || key>=*z)) {
            r_pack(c->bytes+R_GUARD+(y+p->y)*c->surface.stride+
                   (x+p->x)*c->surface.bpp,c->surface.format,p->color);
            if(p->depth_write) *z=key;
            drawn=1;
        }
    }
    return drawn;
}
static void r_equal(const void *want, const void *got, size_t size, const char *which)
{
    const nj_u1 *a=(const nj_u1 *)want,*b=(const nj_u1 *)got;
    size_t i;
    for(i=0;i<size;++i) if(a[i]!=b[i]) {
        fprintf(stderr,"%s mismatch byte %zu want=%02x got=%02x [%s]\n",which,i,a[i],b[i],r_stage);
        RCHECK(0);
    }
    ++r_checks;
}
static void r_storage(const r_canvas *want, const r_canvas *got)
{
    r_equal(want->bytes,got->bytes,sizeof want->bytes,"colour incl padding/guards");
    r_equal(want->depth,got->depth,sizeof want->depth,"depth incl padding/guards");
    r_equal(want->keys,got->keys,sizeof want->keys,"borrowed keys incl padding/guards");
}
static void r_accept(nj_gles2 *state, r_canvas *got)
{
    r_canvas want;
    nj_gles2_stats before=r_stats(state),after;
    nj_u4 drawn=0xdeadbeefu,expected;
    r_copy(&want,got); expected=r_oracle(&want);
    RCHECK(nj_gles2_raster(state,&got->packet,&drawn)==NJ_GFX_ACCEL_DONE);
    RCHECK(drawn==expected); r_storage(&want,got); r_gl_ok();
    after=r_stats(state);
    RCHECK(after.rasters==before.rasters+1 && after.declines==before.declines);
    RCHECK(after.fills==before.fills && after.blits==before.blits);
    ++r_cases; ++r_accepts;
}
static void r_decline_packet(nj_gles2 *state, r_canvas *c, nj_gfx_raster *packet, nj_u4 *out)
{
    r_canvas saved=*c;
    nj_gles2_stats before=r_stats(state),after;
    nj_u4 original=out?*out:0;
    RCHECK(nj_gles2_raster(state,packet,out)==NJ_GFX_ACCEL_DECLINE);
    r_storage(&saved,c);
    if(out) RCHECK(*out==original);
    after=r_stats(state);
    RCHECK(after.rasters==before.rasters && after.declines==before.declines+1);
    ++r_declines;
}
static void r_matrix(nj_gles2 *state)
{
    static const int sizes[][2]={{1,1},{9,7},{3,5}};
    int fmt,test,write,size,phase,mode,x,y;
    char label[120];
    for(fmt=NJ_PIXFMT_RGB565;fmt<=NJ_PIXFMT_ARGB8888;++fmt)
    for(test=0;test<2;++test) for(write=0;write<2;++write)
    for(size=0;size<3;++size) for(phase=0;phase<10;++phase) for(mode=0;mode<4;++mode) {
        r_canvas c;
        snprintf(label,sizeof label,"matrix fmt=%d test=%d write=%d size=%d phase=%d mode=%d",
                 fmt,test,write,size,phase,mode); r_stage=label;
        r_init(&c,fmt,sizes[size][0],sizes[size][1]);
        c.packet.depth_test=test; c.packet.depth_write=write;
        for(y=0;y<c.packet.height;++y) for(x=0;x<c.packet.width;++x) {
            nj_s4 k=r_boundary[(phase+x+3*y)%10],old=k;
            if(mode==1 && old<NJ_GFX_RASTER_MAX_KEY) ++old;
            if(mode==2 && old>0) --old;
            if(mode==3) old=r_boundary[(phase+7*x+y+3)%10];
            c.keys[3+y*c.packet.key_stride+x]=k;
            c.depth[5+(y+c.packet.y)*R_ZSTRIDE+x+c.packet.x]=old;
        }
        r_accept(state,&c);
    }
    r_stage="all occluded then zero coverage drawn=0";
    {
        r_canvas c;
        r_init(&c,NJ_PIXFMT_ARGB8888,9,7);
        for(y=0;y<7;++y) for(x=0;x<9;++x)
            c.depth[5+(y+3)*R_ZSTRIDE+x+2]=NJ_GFX_RASTER_MAX_KEY;
        r_accept(state,&c);
        for(y=0;y<7;++y) for(x=0;x<9;++x) c.keys[3+y*c.packet.key_stride+x]=0;
        r_accept(state,&c);
    }
    r_stage="NULL depth depthless mask holes";
    {
        r_canvas c;
        r_init(&c,NJ_PIXFMT_RGB888,9,7);
        c.packet.depth=NULL; c.packet.depth_stride=0;
        c.packet.depth_test=c.packet.depth_write=0;
        for(y=0;y<7;++y) for(x=0;x<9;++x) c.keys[3+y*c.packet.key_stride+x]=(x+y)%3?1:0;
        r_accept(state,&c);
    }
}
static void r_invalid(nj_gles2 *state)
{
    int i;
    r_stage="invalid packets atomic decline";
    for(i=0;i<24;++i) {
        r_canvas c;
        nj_gfx_raster p;
        nj_u4 drawn=0x98765432u,*out=&drawn;
        r_init(&c,NJ_PIXFMT_ARGB8888,3,2); p=c.packet;
        switch(i) {
        case 0:p.version=2;break;
        case 1:p.surface=NULL;break;
        case 2:p.x=-1;break;
        case 3:p.y=R_H;break;
        case 4:p.width=0;break;
        case 5:p.height=R_H;break;
        case 6:p.keys=NULL;break;
        case 7:p.key_stride=2;break;
        case 8:p.depth=NULL;break;
        case 9:p.depth_stride=R_W-1;break;
        case 10:p.depth_test=2;break;
        case 11:p.depth_write=-1;break;
        case 12:p.color=0x7f123456u;break;
        case 13:c.keys[3]=-1;break;
        case 14:c.keys[3]=NJ_GFX_RASTER_MAX_KEY+1;break;
        case 15:c.depth[5+3*R_ZSTRIDE+2]=-1;break;
        case 16:c.depth[5+3*R_ZSTRIDE+2]=NJ_GFX_RASTER_MAX_KEY+1;break;
        case 17:c.surface.format=17;break;
        case 18:c.surface.stride=1;break;
        case 19:p.depth=(nj_s4 *)c.surface.pixels;break;
        case 20:p.keys=p.depth;break;
        case 21:out=NULL;break;
        case 22:out=(nj_u4 *)(c.depth+5);break;
        case 23:out=(nj_u4 *)&p.color;break;
        }
        r_decline_packet(state,&c,&p,out); r_gl_ok();
    }
}
static void r_fill_2d(nj_gles2 *s, r_canvas *c)
{
    nj_gfx g;
    nj_gles2_stats before=r_stats(s),after;
    int x,y;
    r_canvas want;
    r_copy(&want,c); nj_gfx_init(&g,&c->surface);
    g.color=0xff7391b5u;
    for(y=1;y<4;++y) for(x=1;x<5;++x)
        r_pack(want.bytes+R_GUARD+y*want.surface.stride+x*want.surface.bpp,want.surface.format,g.color);
    RCHECK(nj_gles2_backend()->fill_rect(s,&g,1,1,4,3)==NJ_GFX_ACCEL_DONE);
    r_storage(&want,c); r_gl_ok(); after=r_stats(s);
    RCHECK(after.fills==before.fills+1 && after.rasters==before.rasters);
}
static void r_interleave(nj_gles2 *s)
{
    r_canvas c;
    int i;
    r_stage="CPU/GLES2 2D/3D interleaving and same-pointer reuse";
    r_init(&c,NJ_PIXFMT_ARGB8888,9,7);
    for(i=0;i<5;++i) {
        c.packet.color=0xff121314u+(nj_u4)i*0x10293bu;
        c.keys[3+i]=i?NJ_GFX_RASTER_MAX_KEY:0;
        c.depth[5+3*R_ZSTRIDE+2+i]=i?NJ_GFX_RASTER_MAX_KEY:0;
        r_pack(c.bytes+R_GUARD+3*c.surface.stride+2*4,c.surface.format,0x115579a3u);
        r_accept(s,&c); r_fill_2d(s,&c);
    }
}
static void r_faults(r_gate *gate)
{
    unsigned i;
    r_stage="raster allocations fail atomically; 2D remains live";
    for(i=1;i<=3;++i) {
        nj_gles2 *s=r_create(gate,0);
        r_canvas c;
        nj_u4 drawn=0x123abcdeu;
        r_init(&c,NJ_PIXFMT_ARGB8888,9,7);
        r_realloc_calls=0; r_oom_at=i;
        r_decline_packet(s,&c,&c.packet,&drawn);
        RCHECK(r_realloc_calls==i); r_oom_at=0;
        r_fill_2d(s,&c); r_accept(s,&c);
        RCHECK(nj_gles2_destroy(s)); r_gl_ok();
    }
    r_stage="lazy raster shader failure preserves colour/depth/drawn and 2D";
    {
        nj_gles2 *s=r_create(gate,0);
        r_canvas c;
        nj_u4 drawn=0x123abcdeu;
        r_init(&c,NJ_PIXFMT_RGB888,3,5);
        r_shader_fail=1; r_decline_packet(s,&c,&c.packet,&drawn); r_shader_fail=0;
        r_fill_2d(s,&c); r_accept(s,&c);
        RCHECK(nj_gles2_destroy(s)); r_gl_ok();
    }
    r_stage="failed lazy init retains resources while current gate is false";
    {
        nj_gles2 *s=r_create(gate,0);
        r_canvas c;
        nj_u4 drawn=0x81234567u;
        r_init(&c,NJ_PIXFMT_ARGB8888,3,5);
        /* Keep the first shader live, then fail the second allocation while
         * invalidating the gate. No GL call may follow that loss of current. */
        r_retained_shader=0; r_shader_gate=&gate->enabled; r_shader_fail=2;
        r_decline_packet(s,&c,&c.packet,&drawn);
        RCHECK(gate->enabled==0 && r_retained_shader!=0);
        RCHECK(glIsShader(r_retained_shader)==GL_TRUE);
        r_shader_gate=NULL; gate->enabled=1;
        r_fill_2d(s,&c); r_accept(s,&c);
        RCHECK(nj_gles2_destroy(s));
        RCHECK(glIsShader(r_retained_shader)==GL_FALSE); r_gl_ok();
    }
    r_stage="late real GL readback errors and corrupt readbacks are atomic";
    for(i=0;i<4;++i) {
        nj_gles2 *s=r_create(gate,0);
        r_canvas c;
        nj_u4 drawn=0x13579bdfu;
        r_init(&c,NJ_PIXFMT_ARGB8888,9,7); r_accept(s,&c);
        c.packet.color=0xffe52b49u;
        r_read_calls=0; r_read_at=i==0?1:2; r_read_fault=i<2?1:i==2?2:3;
        /* Corrupt alpha belongs to colour, not depth. */
        if(i==2) r_read_at=1;
        r_decline_packet(s,&c,&c.packet,&drawn);
        RCHECK(r_read_calls>=r_read_at); r_read_at=0; r_read_fault=0;
        r_gl_ok(); r_fill_2d(s,&c); r_accept(s,&c);
        RCHECK(nj_gles2_destroy(s)); r_gl_ok();
    }
}
typedef struct r_production {
    nj_gles2 *state;
    unsigned calls, accepted, occluded, declined;
    int force_decline, mascot;
} r_production;
static int r_production_merge(void *ud, const nj_gfx_raster *packet, nj_u4 *drawn)
{
    r_production *p=(r_production *)ud;
    nj_gles2_stats before=r_stats(p->state),after;
    ++p->calls;
    RCHECK(nj_gfx_raster_validate(packet));
    if(p->force_decline) { ++p->declined; return NJ_GFX_ACCEL_DECLINE; }
    RCHECK(nj_gles2_raster(p->state,packet,drawn)==NJ_GFX_ACCEL_DONE);
    RCHECK(*drawn<=1); r_gl_ok(); after=r_stats(p->state);
    RCHECK(after.rasters==before.rasters+1 && after.declines==before.declines);
    ++p->accepted; if(!*drawn) ++p->occluded;
    if(p->mascot) ++r_gpu_mascot; else ++r_gpu_m3g;
    return NJ_GFX_ACCEL_DONE;
}
static void r_m3g_flat(target *t, nj_u4 color)
{
    nj_value a[2]; a[0].i=0; a[1].r=NULL;
    call(t,t->app,C("Appearance"),"setTexture","(IL" C("Texture2D") ";)V",a);
    integer(t,t->vb,C("VertexBuffer"),"setDefaultColor","(I)V",(nj_s4)color);
}
static void r_m3g_same(target *gpu, target *cpu)
{
    compare(gpu,cpu);
    r_equal(depth(cpu),depth(gpu),W*H*sizeof(nj_s4),"production M3G depth");
    RCHECK(gpu->vm->tris==cpu->vm->tris);
}
static void r_m3g_draw(target *gpu, target *cpu, int z, nj_u4 color)
{
    r_m3g_flat(gpu,color); r_m3g_flat(cpu,color);
    geometry(gpu,z,4); geometry(cpu,z,4);
    render(gpu); render(cpu); r_m3g_same(gpu,cpu);
}
static void r_m3g(nj_gles2 *s)
{
    int fmt,test,write;
    r_stage="production M3G public render -> GLES merge vs original CPU";
    for(fmt=NJ_PIXFMT_RGB565;fmt<=NJ_PIXFMT_ARGB8888;++fmt) {
        target gpu,cpu;
        r_production p;
        memset(&p,0,sizeof p); p.state=s;
        init(&gpu,fmt); init(&cpu,fmt);
        gpu.port.gfx_raster=r_production_merge; gpu.port.gfx_raster_ud=&p;
        fill(&gpu,0xff355b83u); fill(&cpu,0xff355b83u); clear(&gpu); clear(&cpu);
        r_m3g_draw(&gpu,&cpu,-4,0xffca2419u);
        RCHECK(p.accepted>0);
        r_m3g_draw(&gpu,&cpu,-4,0xff23ad68u); /* equal passes */
        r_m3g_draw(&gpu,&cpu,-8,0xff182acdu); /* far is occluded */
        RCHECK(p.occluded>0);
        r_m3g_draw(&gpu,&cpu,-2,0xff49e893u);
        for(test=0;test<2;++test) for(write=0;write<2;++write) {
            integer(&gpu,gpu.cm,C("CompositingMode"),"setDepthTestEnable","(Z)V",test);
            integer(&cpu,cpu.cm,C("CompositingMode"),"setDepthTestEnable","(Z)V",test);
            integer(&gpu,gpu.cm,C("CompositingMode"),"setDepthWriteEnable","(Z)V",write);
            integer(&cpu,cpu.cm,C("CompositingMode"),"setDepthWriteEnable","(Z)V",write);
            r_m3g_draw(&gpu,&cpu,-6,0xff719bd3u);
        }
        /* Actual production decline reaches its original software triangle. */
        p.force_decline=1; r_m3g_draw(&gpu,&cpu,-4,0xffb141e9u);
        RCHECK(p.declined>0); p.force_decline=0;
        fill(&gpu,0xff5d3271u); fill(&cpu,0xff5d3271u); clear(&gpu); clear(&cpu);
        r_m3g_draw(&gpu,&cpu,-4,0xfff28d71u);
        printf("Actual GLES M3G format %d: accepted=%u occluded=%u fallback=%u\n",
               fmt,p.accepted,p.occluded,p.declined);
        destroy(&gpu); destroy(&cpu);
    }
}
#define R_MC "com/mascotcapsule/micro3d/v3/"
static nj_value r_int(int n) { nj_value v; v.bits=0; v.i=n; return v; }
static nj_value r_ref(nj_ref o) { nj_value v; v.bits=0; v.r=o; return v; }
static void r_invoke(nj_vm *vm,nj_ref o,const char *cls,const char *method,const char *desc,nj_value *a)
{ RCHECK(nj_call(vm,o,cls,method,desc,a,NULL)==NJ_OK); }
static nj_ref r_object(nj_vm *vm,const char *cls)
{
    nj_ref o=nj_root(vm,nj_object_new(vm,cls)); RCHECK(o!=NULL);
    RCHECK(nj_call_special(vm,o,cls,"<init>","()V",NULL,NULL)==NJ_OK); return o;
}
static nj_ref r_ints(nj_vm *vm,const nj_s4 *data,nj_u4 n)
{
    nj_ref a=nj_root(vm,nj_int_array_new(vm,n)); RCHECK(a!=NULL);
    memcpy(nj_array_data(a),data,n*sizeof *data); return a;
}
static void r_mascot_submit(nj_vm *vm,nj_ref g3,nj_ref layout,nj_ref effect,
                            nj_ref vertices,nj_ref colors,int command)
{
    nj_value a[11]; memset(a,0,sizeof a);
    a[3]=r_ref(layout); a[4]=r_ref(effect); a[5]=r_int(command); a[6]=r_int(1);
    a[7]=r_ref(vertices); a[10]=r_ref(colors);
    r_invoke(vm,g3,R_MC "Graphics3D","renderPrimitives","(L" R_MC "Texture;IIL"
             R_MC "FigureLayout;L" R_MC "Effect3D;II[I[I[I[I)V",a);
}
static void r_mascot_pixels(nj_vm *vm,nj_ref image,nj_ref array,const nj_u4 expected[36])
{
    nj_value a[7]; unsigned i;
    a[0]=r_ref(array); a[1]=r_int(0); a[2]=r_int(6);
    a[3]=a[4]=r_int(0); a[5]=a[6]=r_int(6);
    r_invoke(vm,image,NJ_C_IMAGE,"getRGB","([IIIIIII)V",a);
    for(i=0;i<36;++i) RCHECK((nj_u4)nj_array_get_int(array,i)==expected[i]);
}
static void r_mascot(nj_gles2 *s)
{
    static const nj_s4 tv[9]={1,1,0,1,5,0,4,5,0},lv[6]={1,4,0,3,4,0};
    const nj_u4 red=0xffe42c56u,green=0xff228e36u,blue=0xff1a4ed2u;
    nj_port port;
    nj_vm *vm;
    r_production p;
    nj_ref img,gfx,g3,fl,fx,verts,line,colors,array;
    nj_value a[4],out;
    nj_u4 expected[36];
    unsigned i,pass,start;
    r_stage="production Mascot queued triangle -> flush -> GLES and CPU mix";
    memset(&p,0,sizeof p); p.state=s; p.mascot=1;
    RCHECK(nj_port_headless(&port)==NJ_OK);
    port.gfx_raster=r_production_merge; port.gfx_raster_ud=&p;
    vm=nj_vm_create(&port,4u*1024u*1024u); RCHECK(vm!=NULL);
    a[0]=a[1]=r_int(6); out.bits=0;
    RCHECK(nj_call_static(vm,NJ_C_IMAGE,"createImage","(II)L" NJ_C_IMAGE ";",a,&out)==NJ_OK);
    img=nj_root(vm,out.r); RCHECK(img!=NULL);
    RCHECK(nj_call(vm,img,NJ_C_IMAGE,"getGraphics","()L" NJ_C_GRAPHICS ";",NULL,&out)==NJ_OK);
    gfx=nj_root(vm,out.r); RCHECK(gfx!=NULL);
    g3=r_object(vm,R_MC "Graphics3D"); fl=r_object(vm,R_MC "FigureLayout");
    fx=r_object(vm,R_MC "Effect3D"); verts=r_ints(vm,tv,9); line=r_ints(vm,lv,6);
    { nj_s4 c=(nj_s4)red; colors=r_ints(vm,&c,1); }
    array=nj_root(vm,nj_int_array_new(vm,36)); RCHECK(array!=NULL);
    a[0]=a[1]=r_int(4096); r_invoke(vm,fl,R_MC "FigureLayout","setScale","(II)V",a);
    for(pass=0;pass<2;++pass) {
        p.force_decline=(int)pass;
        a[0]=r_int(0xffffff); r_invoke(vm,gfx,NJ_C_GRAPHICS,"setColor","(I)V",a);
        a[0]=a[1]=r_int(0); a[2]=a[3]=r_int(6);
        r_invoke(vm,gfx,NJ_C_GRAPHICS,"fillRect","(IIII)V",a);
        for(i=0;i<36;++i) expected[i]=0xffffffffu;
        a[0]=r_ref(gfx);
        r_invoke(vm,g3,R_MC "Graphics3D","bind","(Ljavax/microedition/lcdui/Graphics;)V",a);
        a[0]=a[1]=r_int(0); a[2]=a[3]=r_int(1);
        r_invoke(vm,gfx,NJ_C_GRAPHICS,"setClip","(IIII)V",a);
        start=p.calls; nj_array_set_int(colors,0,(nj_s4)red);
        r_mascot_submit(vm,g3,fl,fx,verts,colors,0x03000400);
        r_mascot_pixels(vm,img,array,expected); RCHECK(p.calls==start);
        r_invoke(vm,g3,R_MC "Graphics3D","flush","()V",NULL);
        expected[19]=expected[25]=expected[26]=red;
        r_mascot_pixels(vm,img,array,expected); RCHECK(p.calls==start+1);
        nj_array_set_int(colors,0,(nj_s4)green);
        r_mascot_submit(vm,g3,fl,fx,verts,colors,0x03000420); /* CPU HALF blend */
        nj_array_set_int(colors,0,(nj_s4)blue);
        r_mascot_submit(vm,g3,fl,fx,line,colors,0x02000400); /* CPU line */
        nj_array_set_int(colors,0,(nj_s4)green);
        r_mascot_submit(vm,g3,fl,fx,verts,colors,0x03000400);
        r_invoke(vm,g3,R_MC "Graphics3D","flush","()V",NULL);
        expected[19]=expected[25]=expected[26]=green; expected[27]=blue;
        r_mascot_pixels(vm,img,array,expected); RCHECK(p.calls==start+2);
        a[0]=r_ref(gfx);
        r_invoke(vm,g3,R_MC "Graphics3D","release","(Ljavax/microedition/lcdui/Graphics;)V",a);
        a[0]=a[1]=r_int(0); a[2]=a[3]=r_int(6);
        r_invoke(vm,gfx,NJ_C_GRAPHICS,"setClip","(IIII)V",a);
    }
    RCHECK(p.accepted==2 && p.declined==2);
    printf("Actual GLES Mascot: accepted=%u CPU fallback=%u\n",p.accepted,p.declined);
    RCHECK(nj_vm_destroy(vm)>0); nj_port_shutdown(&port);
}
static void r_lifetimes(r_egl *e, r_gate gate[2], nj_gles2 *states[2])
{
    r_canvas c;
    nj_u4 drawn=0x76543210u;
    nj_gles2_stats before,after;
    nj_gles2 *small;
    r_stage="independent contexts/state stats; wrong and unavailable contexts";
    r_init(&c,NJ_PIXFMT_ARGB8888,3,2);
    r_bind(e,0); before=r_stats(states[0]);
    r_bind(e,1); r_accept(states[1],&c);
    after=r_stats(states[0]); RCHECK(memcmp(&before,&after,sizeof before)==0);
    r_decline_packet(states[0],&c,&c.packet,&drawn);
    RCHECK(nj_gles2_destroy(states[0])==0); r_gl_ok();
    RCHECK(eglMakeCurrent(e->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
    r_decline_packet(states[0],&c,&c.packet,&drawn);
    r_bind(e,0); gate[0].enabled=0;
    r_decline_packet(states[0],&c,&c.packet,&drawn); gate[0].enabled=1;
    r_accept(states[0],&c);
    r_stage="explicit rectangle pixel budget";
    small=r_create(&gate[0],6); r_accept(small,&c);
    r_init(&c,NJ_PIXFMT_RGB888,3,3); r_decline_packet(small,&c,&c.packet,&drawn);
    RCHECK(nj_gles2_destroy(small)); r_gl_ok();
}
int main(void)
{
    r_egl e;
    r_gate gates[2];
    nj_gles2 *states[2];
    int i;
    if(!r_open_egl(&e)) return 77;
    memset(gates,0,sizeof gates);
    for(i=0;i<2;++i) {
        gates[i].context=e.context[i]; gates[i].enabled=1;
        r_bind(&e,i); states[i]=r_create(&gates[i],0);
    }
    r_bind(&e,0); r_matrix(states[0]); r_invalid(states[0]);
    r_interleave(states[0]); r_faults(&gates[0]);
    r_lifetimes(&e,gates,states);
    r_bind(&e,0); r_m3g(states[0]); r_mascot(states[0]);
    RCHECK(r_gpu_m3g>0 && r_gpu_mascot>0);
    for(i=0;i<2;++i) {
        r_bind(&e,i); RCHECK(gates[i].calls>0); RCHECK(nj_gles2_destroy(states[i])); r_gl_ok();
    }
    r_close_egl(&e);
    printf("PASS actual GLES2 raster: %u independent cases, %u checks; raster DONE=%u "
           "atomic declines=%u; production GPU M3G=%u Mascot=%u; fixture checks=%u\n",
           r_cases,r_checks,r_accepts,r_declines,r_gpu_m3g,r_gpu_mascot,checks);
    return 0;
}
