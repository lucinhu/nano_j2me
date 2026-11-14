/* Real host-EGL regression for Android mode selection and CPU-only UI status.
 * Android property/log names must be privately renamed for this target so the
 * executable cannot interpose on the host EGL vendor's platform functions. */
#include "nj_android_gles2.h"
#include <GLES2/gl2.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *property = "0";
static int property_reads, fail_make;
static unsigned snapshots, lock_queries, selections;
static __thread unsigned egl_calls, gl_calls, property_calls;

int __system_property_get(const char *key, char *value)
{
    assert(strcmp(key, "debug.nj2me.gles2") == 0);
    ++property_reads; ++property_calls;
    strcpy(value, property);
    return (int)strlen(value);
}
int __android_log_print(int priority, const char *tag, const char *fmt, ...)
{
    va_list ap;
    (void)priority; (void)tag;
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
    return 0;
}
int __android_log_write(int priority, const char *tag, const char *text)
{ return __android_log_print(priority, tag, "%s", text); }

/* Link wrapping counts all EGL entry points used by the wrapper, including
 * getters. TLS lets UI queries be checked independently of the owner's calls. */
#define OBSERVE(ret, name, args, call) \
    ret __real_##name args; \
    ret __wrap_##name args { ++egl_calls; return __real_##name call; }
OBSERVE(EGLDisplay, eglGetDisplay, (EGLNativeDisplayType d), (d))
OBSERVE(EGLBoolean, eglInitialize, (EGLDisplay d,EGLint *a,EGLint *b), (d,a,b))
OBSERVE(EGLBoolean, eglBindAPI, (EGLenum a), (a))
OBSERVE(EGLBoolean, eglChooseConfig, (EGLDisplay d,const EGLint *a,EGLConfig *c,EGLint n,EGLint *m), (d,a,c,n,m))
OBSERVE(EGLSurface, eglCreatePbufferSurface, (EGLDisplay d,EGLConfig c,const EGLint *a), (d,c,a))
OBSERVE(EGLContext, eglCreateContext, (EGLDisplay d,EGLConfig c,EGLContext s,const EGLint *a), (d,c,s,a))
OBSERVE(EGLDisplay, eglGetCurrentDisplay, (void), ())
OBSERVE(EGLContext, eglGetCurrentContext, (void), ())
OBSERVE(EGLSurface, eglGetCurrentSurface, (EGLint a), (a))
OBSERVE(EGLint, eglGetError, (void), ())
OBSERVE(EGLBoolean, eglDestroyContext, (EGLDisplay d,EGLContext c), (d,c))
OBSERVE(EGLBoolean, eglDestroySurface, (EGLDisplay d,EGLSurface s), (d,s))
OBSERVE(EGLBoolean, eglTerminate, (EGLDisplay d), (d))
OBSERVE(__eglMustCastToProperFunctionPointerType, eglGetProcAddress, (const char *n), (n))
#undef OBSERVE
EGLBoolean __real_eglMakeCurrent(EGLDisplay d,EGLSurface a,EGLSurface b,EGLContext c);
EGLBoolean __wrap_eglMakeCurrent(EGLDisplay d,EGLSurface a,EGLSurface b,EGLContext c)
{
    ++egl_calls;
    if (fail_make && c != EGL_NO_CONTEXT) return EGL_FALSE;
    return __real_eglMakeCurrent(d,a,b,c);
}
const GLubyte *__real_glGetString(GLenum n);
const GLubyte *__wrap_glGetString(GLenum n)
{ ++gl_calls; return __real_glGetString(n); }
GLenum __real_glGetError(void);
GLenum __wrap_glGetError(void)
{ ++gl_calls; return __real_glGetError(); }

static nj_android_gles2_info snapshot(nj_android_gles2 *s)
{
    nj_android_gles2_info info;
    unsigned e = egl_calls, g = gl_calls, p = property_calls;
    memset(&info, 0xa5, sizeof info);
    nj_android_gles2_snapshot(s, &info);
    assert(e == egl_calls && g == gl_calls && p == property_calls);
    __sync_add_and_fetch(&snapshots, 1);
    return info;
}
static void info_equal(nj_android_gles2_info a, nj_android_gles2_info b)
{
    assert(a.state == b.state && a.requested == b.requested);
    assert(a.fills == b.fills && a.blits == b.blits);
    assert(a.rasters == b.rasters && a.declines == b.declines);
    assert(a.triangles == b.triangles && a.batches == b.batches);
}
static void expect(nj_android_gles2 *s, int state, int requested,
                   nj_u8 fills, nj_u8 blits, nj_u8 rasters, nj_u8 declines)
{
    nj_android_gles2_info want;
    memset(&want, 0, sizeof want);
    want.state = state; want.requested = requested;
    want.fills = fills; want.blits = blits; want.rasters = rasters; want.declines = declines;
    info_equal(snapshot(s), want);
}

typedef struct query {
    nj_android_gles2 *state;
    nj_android_gles2_info result;
    pthread_mutex_t lock;
    pthread_cond_t changed;
    int done;
} query;
static void *query_thread(void *arg)
{
    query *q = (query *)arg;
    q->result = snapshot(q->state);
    assert(pthread_mutex_lock(&q->lock) == 0);
    q->done = 1;
    assert(pthread_cond_signal(&q->changed) == 0);
    assert(pthread_mutex_unlock(&q->lock) == 0);
    return NULL;
}
static void foreign_snapshot_with_gl_lock(nj_android_gles2 *s)
{
    query q;
    pthread_t thread;
    struct timespec deadline;
    nj_android_gles2_info before = snapshot(s);
    memset(&q, 0, sizeof q); q.state = s;
    assert(pthread_mutex_init(&q.lock, NULL) == 0);
    assert(pthread_cond_init(&q.changed, NULL) == 0);
    /* Simulate a blocked driver while the owner holds the GL lock. The getter
     * must finish BEFORE this lock is released; it has a separate CPU lock. */
    assert(pthread_mutex_lock(&s->lock) == 0);
    assert(pthread_create(&thread, NULL, query_thread, &q) == 0);
    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0); deadline.tv_sec += 2;
    assert(pthread_mutex_lock(&q.lock) == 0);
    while (!q.done) {
        int result = pthread_cond_timedwait(&q.changed, &q.lock, &deadline);
        if (result != 0) {
            fprintf(stderr, "snapshot blocked behind GL lock (wait=%d)\n", result);
            abort();
        }
    }
    assert(pthread_mutex_unlock(&q.lock) == 0);
    /* Startup setters are also specified to decline a busy backend. */
    assert(!nj_android_gles2_set_mode(s, 0));
    assert(pthread_mutex_unlock(&s->lock) == 0);
    assert(pthread_join(thread, NULL) == 0);
    info_equal(before, q.result);
    assert(pthread_cond_destroy(&q.changed) == 0);
    assert(pthread_mutex_destroy(&q.lock) == 0);
    ++lock_queries;
}

typedef struct fixture {
    nj_android_gles2 state;
    nj_port port;
    nj_u4 pixels[16], source[16];
    nj_surface surface;
    nj_gfx gfx;
} fixture;
static void bind_fixture(fixture *f)
{
    unsigned e = egl_calls, p = property_calls;
    memset(f, 0, sizeof *f);
    memset(f->pixels, 0x55, sizeof f->pixels);
    f->surface.pixels = (nj_u1 *)f->pixels;
    f->surface.width = f->surface.height = 4;
    f->surface.stride = 16; f->surface.format = NJ_PIXFMT_ARGB8888;
    nj_gfx_init(&f->gfx, &f->surface); f->gfx.color = 0xff4286cdu;
    assert(nj_android_gles2_bind(&f->state, &f->port));
    assert(e == egl_calls && p == property_calls);
    expect(&f->state, 0, 0, 0, 0, 0, 0);
}
static int fill(fixture *f)
{ return f->port.gfx_accel->fill_rect(f->port.gfx_accel_ud, &f->gfx, 0, 0, 4, 4); }
static int set_mode(fixture *f, int mode)
{
    unsigned e = egl_calls, g = gl_calls, p = property_calls;
    int result = nj_android_gles2_set_mode(&f->state, mode);
    assert(e == egl_calls && g == gl_calls && p == property_calls);
    return result;
}
static void dispose_fixture(fixture *f)
{ assert(nj_android_gles2_dispose(&f->state)); }

static void selection_cases(void)
{
    /* pending=-2 means no setter. argument=-1 uses the old public start. */
    static const struct { const char *prop; int pending, argument, on, reads; } cases[] = {
        {"1", -2, 0, 0, 0}, {"0", -2, 1, 1, 0},
        {"0", 1, -1, 1, 0}, {"1", 0, -1, 0, 0},
        {"0", 0, 1, 1, 0}, {"1", 1, 0, 0, 0},
        {"1", -1, -1, 1, 1}, {"0", -2, -1, 0, 1}
    };
    size_t i;
    expect(NULL, 0, 0, 0, 0, 0, 0);
    for (i = 0; i < sizeof cases/sizeof cases[0]; ++i) {
        fixture f;
        unsigned e;
        int reads_before;
        bind_fixture(&f); property = cases[i].prop;
        assert(!set_mode(&f, -2) && !set_mode(&f, 2));
        if (cases[i].pending != -2) assert(set_mode(&f, cases[i].pending));
        expect(&f.state, cases[i].pending == 1 ? 1 : 0,
               cases[i].pending == 1, 0, 0, 0, 0);
        foreign_snapshot_with_gl_lock(&f.state);
        /* A never-started selection must not be reported as an ended run. */
        nj_android_gles2_stop(&f.state);
        expect(&f.state, cases[i].pending == 1 ? 1 : 0,
               cases[i].pending == 1, 0, 0, 0, 0);
        reads_before = property_reads; e = egl_calls;
        if (cases[i].argument == -1) nj_android_gles2_start(&f.state);
        else nj_android_gles2_start_mode(&f.state, cases[i].argument);
        assert(property_reads == reads_before + cases[i].reads);
        expect(&f.state, cases[i].on ? 2 : 0, cases[i].on, 0, 0, 0, 0);
        if (!cases[i].on) assert(e == egl_calls && !f.state.backend);
        else assert(f.state.backend && eglGetCurrentContext() == f.state.context);
        /* A second startup cannot re-read a changed property or change mode. */
        property = cases[i].on ? "0" : "1";
        assert(!set_mode(&f, !cases[i].on));
        nj_android_gles2_start(&f.state);
        nj_android_gles2_start_mode(&f.state, !cases[i].on);
        assert(property_reads == reads_before + cases[i].reads);
        expect(&f.state, cases[i].on ? 2 : 0, cases[i].on, 0, 0, 0, 0);
        foreign_snapshot_with_gl_lock(&f.state);
        if (!cases[i].on) {
            int pixel;
            e = egl_calls;
            assert(fill(&f) == NJ_GFX_ACCEL_DECLINE && e == egl_calls);
            for (pixel = 0; pixel < 16; ++pixel) assert(f.pixels[pixel] == 0x55555555u);
            expect(&f.state, 0, 0, 0, 0, 0, 1);
        }
        nj_android_gles2_stop(&f.state);
        expect(&f.state, 4, cases[i].on, 0, 0, 0, !cases[i].on);
        dispose_fixture(&f); ++selections;
    }
}
static unsigned int reset_probes;
static unsigned int reset_after_commit(void)
{ return ++reset_probes == 1 ? GL_NO_ERROR : 0x8253u; }
static void work_and_failure_cases(void)
{
    fixture f;
    nj_gfx_blit b;
    nj_gfx_raster packet;
    nj_s4 key = NJ_GFX_RASTER_MAX_KEY, z[16] = {0};
    nj_u4 drawn, saved[16];
    int i;
    unsigned e;
    bind_fixture(&f); property = "0";
    nj_android_gles2_start_mode(&f.state, 1);
    expect(&f.state, 2, 1, 0, 0, 0, 0); /* ready does not imply completed work */
    assert(fill(&f) == NJ_GFX_ACCEL_DONE);
    for (i = 0; i < 16; ++i) assert(f.pixels[i] == 0xff4286cdu);
    expect(&f.state, 2, 1, 1, 0, 0, 0);
    memset(&b, 0, sizeof b);
    for (i = 0; i < 16; ++i) f.source[i] = 0xff112233u+(nj_u4)i;
    b.pixels = (const nj_u1 *)f.source; b.stride = 16; b.format = NJ_PIXFMT_ARGB8888;
    b.width = b.height = b.clip_w = b.clip_h = 4; b.alpha = 255; b.rop = NJ_ROP_SET;
    assert(f.port.gfx_accel->blit(f.port.gfx_accel_ud, &f.gfx, &b) == NJ_GFX_ACCEL_DONE);
    assert(!memcmp(f.source, f.pixels, sizeof f.pixels));
    memset(&packet, 0, sizeof packet);
    packet.version = NJ_GFX_RASTER_VERSION; packet.surface = &f.surface;
    packet.x = packet.y = 2; packet.width = packet.height = 1;
    packet.keys = &key; packet.key_stride = 1; packet.depth = z; packet.depth_stride = 4;
    packet.depth_test = packet.depth_write = 1; packet.color = 0xffed8139u;
    drawn = 99;
    assert(f.port.gfx_raster(f.port.gfx_raster_ud, &packet, &drawn) == NJ_GFX_ACCEL_DONE);
    assert(drawn == 1 && z[10] == key && f.pixels[10] == packet.color);
    assert(glGetError() == GL_NO_ERROR);
    expect(&f.state, 2, 1, 1, 1, 1, 0); foreign_snapshot_with_gl_lock(&f.state);
    memcpy(saved, f.pixels, sizeof saved);
    nj_gfx_set_xor(&f.gfx, 0x00334455u);
    assert(fill(&f) == NJ_GFX_ACCEL_DECLINE); nj_gfx_clear_xor(&f.gfx);
    assert(!memcmp(saved, f.pixels, sizeof saved));
    expect(&f.state, 2, 1, 1, 1, 1, 1); /* an ordinary decline is not a failed run */
    fail_make = 1; assert(fill(&f) == NJ_GFX_ACCEL_DECLINE); fail_make = 0;
    assert(!memcmp(saved, f.pixels, sizeof saved));
    expect(&f.state, 3, 1, 1, 1, 1, 2); foreign_snapshot_with_gl_lock(&f.state);
    e = egl_calls; assert(fill(&f) == NJ_GFX_ACCEL_DECLINE); assert(e == egl_calls);
    nj_android_gles2_stop(&f.state);
    expect(&f.state, 4, 1, 1, 1, 1, 3); foreign_snapshot_with_gl_lock(&f.state);
    dispose_fixture(&f);

    bind_fixture(&f); fail_make = 1;
    nj_android_gles2_start_mode(&f.state, 1); fail_make = 0;
    expect(&f.state, 3, 1, 0, 0, 0, 0);
    assert(fill(&f) == NJ_GFX_ACCEL_DECLINE);
    for (i = 0; i < 16; ++i) assert(f.pixels[i] == 0x55555555u);
    nj_android_gles2_stop(&f.state); expect(&f.state, 4, 1, 0, 0, 0, 1); dispose_fixture(&f);

    /* Loss after real GPU commit must publish fallback WITH the completed work,
     * preserving DONE/drawn rather than triggering another software merge. */
    bind_fixture(&f); nj_android_gles2_start_mode(&f.state, 1);
    packet.surface = &f.surface; memset(z, 0, sizeof z); drawn = 99;
    reset_probes = 0; f.state.reset_status = reset_after_commit;
    assert(f.port.gfx_raster(f.port.gfx_raster_ud, &packet, &drawn) == NJ_GFX_ACCEL_DONE);
    assert(drawn == 1 && z[10] == key && f.pixels[10] == packet.color && reset_probes == 2);
    expect(&f.state, 3, 1, 0, 0, 1, 0); foreign_snapshot_with_gl_lock(&f.state);
    memcpy(saved, f.pixels, sizeof saved); drawn = 99;
    assert(f.port.gfx_raster(f.port.gfx_raster_ud, &packet, &drawn) == NJ_GFX_ACCEL_DECLINE);
    assert(drawn == 99 && !memcmp(saved, f.pixels, sizeof saved));
    expect(&f.state, 3, 1, 0, 0, 1, 1);
    nj_android_gles2_stop(&f.state); expect(&f.state, 4, 1, 0, 0, 1, 1); dispose_fixture(&f);
}

/* Exercise the stable frontend callback, independently of the backend's own
 * triangle oracle. These interior pixels have constant colour/depth and are
 * away from native edge-coverage differences. */
static void triangle_cases(void)
{
    fixture f;
    nj_gfx_triangles packet;
    nj_gfx_triangle_cmd tri;
    nj_s4 z[16], saved_z[16];
    nj_u4 submitted, saved[16];
    nj_android_gles2_info info;
    int i, late_reset;
    for (late_reset = 0; late_reset < 2; ++late_reset) {
        bind_fixture(&f);
        memset(z, 0, sizeof z); memset(&packet, 0, sizeof packet);
        memset(&tri, 0, sizeof tri);
        tri.vertices[0].x = tri.vertices[0].y = -4;
        tri.vertices[1].x = 40; tri.vertices[1].y = -4;
        tri.vertices[2].x = -4; tri.vertices[2].y = 40;
        for (i = 0; i < 3; ++i) {
            tri.vertices[i].depth = 0.25; tri.vertices[i].inv_w = 1;
            tri.vertices[i].color = 0xff336699u;
        }
        tri.blend = 68; tri.alpha_write = tri.depth_test = tri.depth_write = 1;
        tri.wrap_s = tri.wrap_t = 240; tri.filter = 208; tri.texture_function = 228;
        packet.version = NJ_GFX_TRIANGLES_VERSION; packet.surface = &f.surface;
        packet.depth = z; packet.depth_stride = 4;
        packet.width = packet.height = 4; packet.triangles = &tri; packet.count = 1;
        assert(f.port.gfx_triangles && f.port.gfx_triangles_ud);
        memcpy(saved, f.pixels, sizeof saved); submitted = 99;
        assert(f.port.gfx_triangles(f.port.gfx_triangles_ud, &packet, &submitted)
               == NJ_GFX_ACCEL_DECLINE);
        assert(submitted == 99 && !memcmp(saved, f.pixels, sizeof saved));
        info = snapshot(&f.state); assert(!info.triangles && !info.batches);
        nj_android_gles2_start_mode(&f.state, 1);
        if (late_reset) {
            reset_probes = 0; f.state.reset_status = reset_after_commit;
        }
        assert(f.port.gfx_triangles(f.port.gfx_triangles_ud, &packet, &submitted)
               == NJ_GFX_ACCEL_DONE);
        assert(submitted == 1 && f.pixels[5] == 0xff336699u && z[5] == 12582912);
        info = snapshot(&f.state);
        assert(info.state == (late_reset ? 3 : 2));
        assert(info.triangles == 1 && info.batches == 1 && info.rasters == 0);
        if (late_reset) assert(reset_probes == 2);
        foreign_snapshot_with_gl_lock(&f.state);
        /* A rejected batch must leave all three outputs untouched, including
         * when an earlier completed batch retired the context after commit. */
        memcpy(saved, f.pixels, sizeof saved); memcpy(saved_z, z, sizeof z);
        packet.version = 0; submitted = 99;
        assert(f.port.gfx_triangles(f.port.gfx_triangles_ud, &packet, &submitted)
               == NJ_GFX_ACCEL_DECLINE);
        assert(submitted == 99 && !memcmp(saved, f.pixels, sizeof saved));
        assert(!memcmp(saved_z, z, sizeof z));
        info = snapshot(&f.state); assert(info.triangles == 1 && info.batches == 1);
        nj_android_gles2_stop(&f.state);
        packet.version = NJ_GFX_TRIANGLES_VERSION;
        assert(f.port.gfx_triangles(f.port.gfx_triangles_ud, &packet, &submitted)
               == NJ_GFX_ACCEL_DECLINE);
        assert(submitted == 99 && !memcmp(saved, f.pixels, sizeof saved));
        assert(!memcmp(saved_z, z, sizeof z));
        info = snapshot(&f.state);
        assert(info.state == 4 && info.triangles == 1 && info.batches == 1);
        dispose_fixture(&f);
    }
    puts("PASS Android triangle wrapper: stable prestart/ended decline, real colour/depth/submitted, "
         "independent counters, CPU-only foreign snapshots, invalid batch, late-reset DONE");
}

/* Only independent context preflight may skip; failures after this are bugs. */
static int preflight(void)
{
    static const EGLint attrs[] = {EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    static const EGLint pb[] = {EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
    static const EGLint ctx[] = {EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLSurface s = EGL_NO_SURFACE;
    EGLContext c = EGL_NO_CONTEXT;
    EGLConfig config;
    EGLint n = 0;
    int initialized = 0, ok = 0;
    if (d == EGL_NO_DISPLAY || !eglInitialize(d,NULL,NULL)) goto done;
    initialized = 1;
    if (!eglBindAPI(EGL_OPENGL_ES_API) || !eglChooseConfig(d,attrs,&config,1,&n) || n < 1) goto done;
    s = eglCreatePbufferSurface(d,config,pb); if (s == EGL_NO_SURFACE) goto done;
    c = eglCreateContext(d,config,EGL_NO_CONTEXT,ctx); if (c == EGL_NO_CONTEXT) goto done;
    if (!eglMakeCurrent(d,s,s,c) || !glGetString(GL_VERSION)) goto done;
    printf("Android wrapper host EGL: vendor=%s renderer=%s version=%s\n",
           glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION));
    ok = 1;
done:
    if (!ok) fprintf(stderr,"SKIP Android GLES2 settings: EGL/GLES2 preflight unavailable (0x%x)\n",eglGetError());
    if (initialized) {
        (void)eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if (c != EGL_NO_CONTEXT) (void)eglDestroyContext(d,c);
        if (s != EGL_NO_SURFACE) (void)eglDestroySurface(d,s);
        (void)eglTerminate(d);
    }
    return ok;
}
int main(void)
{
    alarm(90); /* Bound the entire executable as well as each lock regression. */
    if (!preflight()) return 77;
    selection_cases(); work_and_failure_cases(); triangle_cases();
    puts("PASS Android GLES2 settings: explicit mode/property precedence, legacy once-only property, "
         "prestart/pending/ready/fallback/ended snapshots, real fill/blit/raster pixels, preserved DONE after reset");
    printf("PASS CPU snapshots=%u foreign queries while GL lock held=%u selection cases=%u; no EGL/GL/property getter work\n",
           snapshots, lock_queries, selections);
    return 0;
}
