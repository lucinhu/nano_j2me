/* Optional Android GLES2 binding. All EGL/GL work belongs to nj_run's thread;
 * ANativeWindow remains the existing CPU framebuffer presentation path. */
#include "nj_android_gles2.h"

#if defined(__ANDROID__) && NJ_ANDROID_GLES2 && !NJ_ANDROID_LEGACY
#include <GLES2/gl2.h>
#include <android/log.h>
#include <sys/system_properties.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 1: install the optional 2D fill/blit GPU callbacks (see the comment at the
 * wiring site; the 3D triangle list path is unaffected). */
#ifndef NJ_ANDROID_GLES2_2D_ACCEL
#define NJ_ANDROID_GLES2_2D_ACCEL 0
#endif

#define GLES2_TAG "nj2me"

/* Opt-in present-cost statistics:
 *
 *     adb shell setprop debug.nj2me.present_stats 1
 *
 * The property is sampled once a second. Every NJ_ANDROID_PRESENT_STATS
 * presented frames the GL clock times of the draw (make-current, upload and the
 * filter passes) and of the swap are logged as min/median/p95. The swap figure
 * includes the wait for the next buffer, so the draw figure is the one that
 * describes the filtering cost. The samples live in the heap state because the
 * shared object must keep zero writable static data. Only the run thread calls
 * present(), so the state needs no lock. */
static unsigned long long present_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ull + (unsigned long long)ts.tv_nsec;
}

static int present_stats_wanted(void)
{
    char value[PROP_VALUE_MAX] = {0};
    (void)__system_property_get("debug.nj2me.present_stats", value);
    return value[0] != '\0' && value[0] != '0';
}

static int present_stats_cmp(const void *a, const void *b)
{
    unsigned long long x = *(const unsigned long long *)a;
    unsigned long long y = *(const unsigned long long *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

static void present_stats_record(nj_android_gles2 *s, unsigned long long draw_ns,
                                 unsigned long long swap_ns, int mode, int w, int h)
{
    if (!s->present_stats.enabled || w <= 0 || h <= 0) return;
    if (s->present_stats.frames == NJ_ANDROID_PRESENT_STATS || s->present_stats.mode != mode ||
        s->present_stats.w != w || s->present_stats.h != h) {
        s->present_stats.frames = 0;
        s->present_stats.mode = mode;
        s->present_stats.w = w;
        s->present_stats.h = h;
    }
    s->present_stats.draw[s->present_stats.frames] = draw_ns;
    s->present_stats.swap[s->present_stats.frames] = swap_ns;
    if (++s->present_stats.frames < NJ_ANDROID_PRESENT_STATS) return;
    {
        unsigned long long sorted[NJ_ANDROID_PRESENT_STATS];
        memcpy(sorted, s->present_stats.draw, sizeof sorted);
        qsort(sorted, NJ_ANDROID_PRESENT_STATS, sizeof sorted[0], present_stats_cmp);
        __android_log_print(ANDROID_LOG_INFO, GLES2_TAG,
                            "present stats: mode=%d %dx%d frames=%u draw ms min=%.3f "
                            "median=%.3f p95=%.3f",
                            s->present_stats.mode, s->present_stats.w, s->present_stats.h,
                            (unsigned)NJ_ANDROID_PRESENT_STATS,
                            (double)sorted[0] / 1e6,
                            (double)sorted[NJ_ANDROID_PRESENT_STATS / 2] / 1e6,
                            (double)sorted[(NJ_ANDROID_PRESENT_STATS * 95) / 100] / 1e6);
        memcpy(sorted, s->present_stats.swap, sizeof sorted);
        qsort(sorted, NJ_ANDROID_PRESENT_STATS, sizeof sorted[0], present_stats_cmp);
        __android_log_print(ANDROID_LOG_INFO, GLES2_TAG,
                            "present stats: swap ms min=%.3f median=%.3f p95=%.3f",
                            (double)sorted[0] / 1e6,
                            (double)sorted[NJ_ANDROID_PRESENT_STATS / 2] / 1e6,
                            (double)sorted[(NJ_ANDROID_PRESENT_STATS * 95) / 100] / 1e6);
    }
    s->present_stats.frames = 0;
}

/* Caller holds lock. Publishing never calls GL and never acquires a port or
 * Java lock. Readers take info_lock only, so driver stalls cannot block UI. */
static void publish(nj_android_gles2 *s)
{
    nj_android_gles2_info info;
    memset(&info, 0, sizeof info);
    info.state = s->stopped ? 4 : !s->requested ? 0 : s->failed ? 3 : s->backend ? 2 : 1;
    info.requested = s->requested;
    info.fills = s->stats.fills; info.blits = s->stats.blits;
    info.rasters = s->stats.rasters; info.declines = s->stats.declines;
    info.triangles = s->stats.triangles; info.batches = s->stats.batches;
    pthread_mutex_lock(&s->info_lock);
    s->info = info;
    pthread_mutex_unlock(&s->info_lock);
}
void nj_android_gles2_snapshot(nj_android_gles2 *s, nj_android_gles2_info *out)
{
    if (!out) return;
    if (!s) { memset(out, 0, sizeof *out); return; }
    pthread_mutex_lock(&s->info_lock);
    *out = s->info;
    pthread_mutex_unlock(&s->info_lock);
}
int nj_android_gles2_set_mode(nj_android_gles2 *s, int mode)
{
    int accepted = 0;
    if (!s || mode < -1 || mode > 1 || pthread_mutex_trylock(&s->lock) != 0) return 0;
    if (!s->owner_set && !s->stopped) {
        s->mode = mode;
        s->requested = mode == 1;
        publish(s);
        accepted = 1;
    }
    pthread_mutex_unlock(&s->lock);
    return accepted;
}
static int owning_thread(const nj_android_gles2 *s)
{
    return s->owner_set && pthread_equal(s->owner, pthread_self());
}

/* Called by nj_gles2 while the wrapper lock is already held. */
static int is_current(void *ud)
{
    nj_android_gles2 *s = (nj_android_gles2 *)ud;
    return owning_thread(s) && !s->failed &&
           s->context != EGL_NO_CONTEXT &&
           eglGetCurrentDisplay() == s->display &&
           eglGetCurrentContext() == s->context &&
           ((eglGetCurrentSurface(EGL_DRAW) == s->pbuffer &&
             eglGetCurrentSurface(EGL_READ) == s->pbuffer) ||
            (s->window != EGL_NO_SURFACE &&
             eglGetCurrentSurface(EGL_DRAW) == s->window &&
             eglGetCurrentSurface(EGL_READ) == s->window));
}

static void failed(nj_android_gles2 *s, const char *stage, EGLint error)
{
    __android_log_print(ANDROID_LOG_WARN, GLES2_TAG,
                        "gles2: %s (EGL 0x%x); software fallback for this run",
                        stage, (unsigned int)error);
    s->failed = 1;
    s->current = 0;
    s->version_ok = 0;
    s->version_probes = 0;
    publish(s);
}

/* glGetString also detects an unusable GLES2 context on implementations without
 * robustness. A backend decline never changes CPU pixels, even on late loss.
 * Do not recreate resources: the stable wrapper simply declines until stop. */
static int usable(nj_android_gles2 *s)
{
    if (s->reset_status && s->reset_status() != GL_NO_ERROR) {
        failed(s, "GL context reset", EGL_CONTEXT_LOST);
        return 0;
    }
    /* Probing glGetString for every drawing packet is a driver call per packet
     * for no benefit: only context loss can change the answer.  Keep checking
     * periodically so a lost context still falls back. */
    if (s->version_ok && (++s->version_probes & 1023u) != 0) return 1;
    if (!glGetString(GL_VERSION)) {
        failed(s, "GL context unavailable", EGL_CONTEXT_LOST);
        return 0;
    }
    s->version_ok = 1;
    return 1;
}

static int make_current(nj_android_gles2 *s)
{
    if (!owning_thread(s) || s->failed || s->context == EGL_NO_CONTEXT)
        return 0;
    /* All GL is issued from the owning run thread, so a successful bind stays
     * valid until stop/dispose or a context-loss report. Issuing eglMakeCurrent
     * per packet showed up as measurable CPU in the Android profile. */
    if (s->current) return usable(s);
    /* Rebinding also lets EGL report EGL_CONTEXT_LOST before backend GL calls.
     * Never borrow a UI/Surface context or use a window surface. */
    if (!eglMakeCurrent(s->display, s->pbuffer, s->pbuffer, s->context)) {
        failed(s, "eglMakeCurrent failed", eglGetError());
        return 0;
    }
    s->current = 1;
    s->version_ok = 0;
    s->version_probes = 0;
    return usable(s);
}

static int fill_rect(void *ud, const nj_gfx *g, int x, int y, int w, int h)
{
    nj_android_gles2 *s = (nj_android_gles2 *)ud;
    int result = NJ_GFX_ACCEL_DECLINE;
    pthread_mutex_lock(&s->lock);
    if (s->requested && s->backend && make_current(s)) {
        result = nj_gles2_backend()->fill_rect(s->backend, g, x, y, w, h);
        /* Loss may occur during backend work, whose error handling drains GL
         * errors. Probe again without changing its CPU commit/decline result. */
        (void)usable(s);
    }
    if (result == NJ_GFX_ACCEL_DONE) ++s->stats.fills;
    else ++s->stats.declines;
    publish(s);
    pthread_mutex_unlock(&s->lock);
    return result;
}

static int blit(void *ud, const nj_gfx *g, const nj_gfx_blit *b)
{
    nj_android_gles2 *s = (nj_android_gles2 *)ud;
    int result = NJ_GFX_ACCEL_DECLINE;
    pthread_mutex_lock(&s->lock);
    if (s->requested && s->backend && make_current(s)) {
        result = nj_gles2_backend()->blit(s->backend, g, b);
        (void)usable(s);
    }
    if (result == NJ_GFX_ACCEL_DONE) ++s->stats.blits;
    else ++s->stats.declines;
    publish(s);
    pthread_mutex_unlock(&s->lock);
    return result;
}

static int raster(void *ud, const nj_gfx_raster *packet, nj_u4 *drawn)
{
    nj_android_gles2 *s = (nj_android_gles2 *)ud;
    int result = NJ_GFX_ACCEL_DECLINE;
    pthread_mutex_lock(&s->lock);
    if (s->requested && s->backend && make_current(s)) {
        result = nj_gles2_raster(s->backend, packet, drawn);
        /* DONE has already committed colour/depth and *drawn. A late reset
         * disables future calls, but must not trigger a second software merge. */
        (void)usable(s);
    }
    if (result == NJ_GFX_ACCEL_DONE) ++s->stats.rasters;
    else ++s->stats.declines;
    publish(s);
    pthread_mutex_unlock(&s->lock);
    return result;
}

static int triangles(void *ud, const nj_gfx_triangles *packet, nj_u4 *submitted)
{
    nj_android_gles2 *s = (nj_android_gles2 *)ud;
    int result = NJ_GFX_ACCEL_DECLINE;
    pthread_mutex_lock(&s->lock);
    if (s->requested && s->backend && make_current(s)) {
        result = nj_gles2_triangles(s->backend, packet, submitted);
        /* DONE has committed colour, depth and *submitted. A later reset may
         * retire this backend, but must never replay the batch in software. */
        (void)usable(s);
    }
    if (result == NJ_GFX_ACCEL_DONE) {
        s->stats.triangles += *submitted;
        ++s->stats.batches;
    } else {
        ++s->stats.declines;
    }
    publish(s);
    pthread_mutex_unlock(&s->lock);
    return result;
}

static int triangles_list(void *ud, const nj_gfx_triangles_list *list,
                          nj_u4 *submitted)
{
    nj_android_gles2 *s = (nj_android_gles2 *)ud;
    int result = NJ_GFX_ACCEL_DECLINE;
    pthread_mutex_lock(&s->lock);
    if (s->requested && s->backend && make_current(s)) {
        result = nj_gles2_triangles_list(s->backend, list, submitted);
        (void)usable(s);
    }
    if (result == NJ_GFX_ACCEL_DONE) {
        s->stats.triangles += *submitted;
        ++s->stats.batches;
    } else {
        ++s->stats.declines;
    }
    publish(s);
    pthread_mutex_unlock(&s->lock);
    return result;
}

static int triangles_begin(void *ud, const nj_gfx_triangles_session *session)
{
    nj_android_gles2 *s = (nj_android_gles2 *)ud;
    int result = NJ_GFX_ACCEL_DECLINE;
    pthread_mutex_lock(&s->lock);
    if (s->requested && s->backend && make_current(s)) {
        result = nj_gles2_triangles_begin(s->backend, session);
        (void)usable(s);
    }
    if (result != NJ_GFX_ACCEL_DONE) ++s->stats.declines;
    publish(s);
    pthread_mutex_unlock(&s->lock);
    return result;
}

static int triangles_end(void *ud, nj_u4 *submitted)
{
    nj_android_gles2 *s = (nj_android_gles2 *)ud;
    int result = NJ_GFX_ACCEL_DECLINE;
    pthread_mutex_lock(&s->lock);
    if (s->requested && s->backend && make_current(s)) {
        result = nj_gles2_triangles_end(s->backend, submitted);
        (void)usable(s);
    }
    if (result == NJ_GFX_ACCEL_DONE) {
        s->stats.triangles += *submitted;
        ++s->stats.batches;
    } else {
        ++s->stats.declines;
    }
    publish(s);
    pthread_mutex_unlock(&s->lock);
    return result;
}

static void triangles_abort(void *ud)
{
    nj_android_gles2 *s = (nj_android_gles2 *)ud;
    pthread_mutex_lock(&s->lock);
    if (s->requested && s->backend && make_current(s)) nj_gles2_triangles_abort(s->backend);
    pthread_mutex_unlock(&s->lock);
}

static const nj_gfx_accel wrappers = {NJ_GFX_ACCEL_VERSION, fill_rect, blit};

int nj_android_gles2_bind(nj_android_gles2 *s, nj_port *port)
{
    memset(s, 0, sizeof(*s));
    s->display = EGL_NO_DISPLAY;
    s->context = EGL_NO_CONTEXT;
    s->pbuffer = EGL_NO_SURFACE;
    s->window = EGL_NO_SURFACE;
    s->window_ptr = NULL;
    s->window_ok = 0;
    s->config = NULL;
    s->mode = -1;
    if (pthread_mutex_init(&s->lock, NULL) != 0) return 0;
    if (pthread_mutex_init(&s->info_lock, NULL) != 0) {
        pthread_mutex_destroy(&s->lock);
        return 0;
    }
    /* Synchronous per-op readback makes the 2D backend slower than software on
     * the reference host; install it only when explicitly requested. */
#if NJ_ANDROID_GLES2_2D_ACCEL
    port->gfx_accel = &wrappers;
    port->gfx_accel_ud = s;
#else
    (void)wrappers;
#endif
    port->gfx_raster = raster;
    port->gfx_raster_ud = s;
    port->gfx_triangles = triangles;
    port->gfx_triangles_ud = s;
    port->gfx_triangles_list = triangles_list;
    port->gfx_triangles_list_ud = s;
    port->gfx_triangles_begin = triangles_begin;
    port->gfx_triangles_end = triangles_end;
    port->gfx_triangles_abort = triangles_abort;
    port->gfx_triangles_session_ud = s;
    /* Phone/emulator CPUs vary by an order of magnitude; calibrate once and
     * keep whichever path is faster on this device. */
    port->gfx_triangles_auto = 1;
    return 1;
}

static int extension(const char *list, const char *name)
{
    size_t n = strlen(name);
    const char *p = list;
    if (!p) return 0;
    while ((p = strstr(p, name)) != NULL) {
        if ((p == list || p[-1] == ' ') && (p[n] == '\0' || p[n] == ' '))
            return 1;
        p += n;
    }
    return 0;
}

/* Owner only, under lock. Clear the borrowed backend handle before freeing it.
 * After loss, destroy its actual context before abandoning CPU-only storage. */
static void release(nj_android_gles2 *s)
{
    nj_gles2 *backend = s->backend;
    s->backend = NULL;
    if (backend && make_current(s) && nj_gles2_destroy(backend)) backend = NULL;
    if (s->display != EGL_NO_DISPLAY) {
        if (s->context != EGL_NO_CONTEXT &&
            eglGetCurrentContext() == s->context &&
            !eglMakeCurrent(s->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                            EGL_NO_CONTEXT)) {
            failed(s, "EGL unbind failed; retaining resources", eglGetError());
            s->backend = backend;
            return;
        }
        s->current = 0;
        if (s->context != EGL_NO_CONTEXT) {
            if (!eglDestroyContext(s->display, s->context)) {
                failed(s, "eglDestroyContext failed; retaining resources", eglGetError());
                s->backend = backend;
                return;
            }
            s->context = EGL_NO_CONTEXT;
        }
        /* The context was actually destroyed, so CPU-only abandonment is safe. */
        if (backend) { nj_gles2_abandon(backend); backend = NULL; }
        if (s->window != EGL_NO_SURFACE) {
            (void)eglDestroySurface(s->display, s->window);
            s->window = EGL_NO_SURFACE;
            s->window_ptr = NULL;
        }
        if (s->pbuffer != EGL_NO_SURFACE) {
            if (!eglDestroySurface(s->display, s->pbuffer)) {
                failed(s, "eglDestroySurface failed; retaining resources", eglGetError());
                return;
            }
            s->pbuffer = EGL_NO_SURFACE;
        }
        if (s->initialized && !eglTerminate(s->display)) {
            failed(s, "eglTerminate failed; retaining display", eglGetError());
            return;
        }
    }
    s->display = EGL_NO_DISPLAY;
    s->initialized = 0;
    s->reset_status = NULL;
}

static void start(nj_android_gles2 *s, int mode, int presentation_only)
{
    static const EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    static const EGLint window_config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT | EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    static const EGLint pbuffer_attrs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    static const EGLint context_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    char prop[PROP_VALUE_MAX];
    EGLConfig config;
    EGLint count = 0;
    nj_gles2_config backend_config;
    const char *extensions, *renderer;

    if (!s || mode < -1 || mode > 1) return;
    pthread_mutex_lock(&s->lock);
    if (presentation_only) {
        if (!owning_thread(s) || s->stopped || s->failed || s->initialized) {
            pthread_mutex_unlock(&s->lock);
            return;
        }
    } else {
        if (s->owner_set || s->stopped) {
            pthread_mutex_unlock(&s->lock);
            return;
        }
        s->owner = pthread_self();
        s->owner_set = 1;
        if (mode != -1) s->mode = mode;
        if (s->mode == -1) {
            prop[0] = '\0';
            (void)__system_property_get("debug.nj2me.gles2", prop);
            s->requested = strcmp(prop, "1") == 0;
        } else {
            s->requested = s->mode == 1;
        }
        publish(s);
        if (!s->requested) goto done;
    }
    /* Do not disturb a context belonging to a native embedder on this thread. */
    if (eglGetCurrentContext() != EGL_NO_CONTEXT) {
        failed(s, "run thread already has an EGL context", EGL_SUCCESS);
        goto done;
    }
    s->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (s->display == EGL_NO_DISPLAY) {
        failed(s, "eglGetDisplay failed", eglGetError());
        goto fail;
    }
    if (!eglInitialize(s->display, NULL, NULL)) {
        failed(s, "eglInitialize failed", eglGetError());
        goto fail;
    }
    s->initialized = 1;
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        failed(s, "eglBindAPI failed", eglGetError());
        goto fail;
    }
    /* Prefer a config that can also back a window surface; GL presentation is
     * optional, so a pbuffer-only config still keeps the 3D path working. */
    s->window_ok = 0;
    if (eglChooseConfig(s->display, window_config_attrs, &config, 1, &count) && count >= 1) {
        s->window_ok = 1;
    } else if (!eglChooseConfig(s->display, config_attrs, &config, 1, &count) || count < 1) {
        failed(s, "no GLES2 pbuffer config", eglGetError());
        goto fail;
    }
    s->config = config;
    s->pbuffer = eglCreatePbufferSurface(s->display, config, pbuffer_attrs);
    if (s->pbuffer == EGL_NO_SURFACE) {
        failed(s, "eglCreatePbufferSurface failed", eglGetError());
        goto fail;
    }
    s->context = eglCreateContext(s->display, config, EGL_NO_CONTEXT, context_attrs);
    if (s->context == EGL_NO_CONTEXT) {
        failed(s, "eglCreateContext failed", eglGetError());
        goto fail;
    }
    if (!make_current(s)) goto fail;
    extensions = (const char *)glGetString(GL_EXTENSIONS);
    if (extension(extensions, "GL_KHR_robustness")) {
        s->reset_status = (unsigned int (*)(void))
            eglGetProcAddress("glGetGraphicsResetStatusKHR");
    } else if (extension(extensions, "GL_EXT_robustness")) {
        s->reset_status = (unsigned int (*)(void))
            eglGetProcAddress("glGetGraphicsResetStatusEXT");
    }
    memset(&backend_config, 0, sizeof(backend_config));
    backend_config.is_current = is_current;
    backend_config.ud = s;
    s->backend = nj_gles2_create(&backend_config);
    s->applied_sharp = 0.0f;
    if (!s->backend) {
        failed(s, "GLES2 backend initialization failed", eglGetError());
        goto fail;
    }
    renderer = (const char *)glGetString(GL_RENDERER);
    __android_log_print(ANDROID_LOG_INFO, GLES2_TAG,
                        "gles2: enabled renderer=%s (private pbuffer)",
                        renderer ? renderer : "unknown");
    goto done;
fail:
    release(s);
done:
    publish(s);
    pthread_mutex_unlock(&s->lock);
}

void nj_android_gles2_start_mode(nj_android_gles2 *s, int mode)
{
    start(s, mode, 0);
}

void nj_android_gles2_start(nj_android_gles2 *s)
{
    nj_android_gles2_start_mode(s, -1);
}

void nj_android_gles2_stop(nj_android_gles2 *s)
{
    pthread_mutex_lock(&s->lock);
    if (!s->stopped && owning_thread(s)) {
        release(s);
        s->stopped = 1;
        publish(s);
        if (s->requested) {
            __android_log_print(ANDROID_LOG_INFO, GLES2_TAG,
                                "gles2: final fills=%llu blits=%llu rasters=%llu declines=%llu triangles=%llu batches=%llu%s",
                                (unsigned long long)s->stats.fills,
                                (unsigned long long)s->stats.blits,
                                (unsigned long long)s->stats.rasters,
                                (unsigned long long)s->stats.declines,
                                (unsigned long long)s->stats.triangles,
                                (unsigned long long)s->stats.batches,
                                s->failed ? " (software fallback)" : "");
        }
    }
    pthread_mutex_unlock(&s->lock);
}

int nj_android_gles2_dispose(nj_android_gles2 *s)
{
    int safe;
    pthread_mutex_lock(&s->lock);
    safe = !s->backend && s->display == EGL_NO_DISPLAY &&
           s->context == EGL_NO_CONTEXT && s->pbuffer == EGL_NO_SURFACE;
    pthread_mutex_unlock(&s->lock);
    if (!safe) {
        __android_log_write(ANDROID_LOG_ERROR, GLES2_TAG,
                            "gles2: port shutdown before run-thread teardown; retaining state");
        return 0;
    }
    pthread_mutex_destroy(&s->info_lock);
    pthread_mutex_destroy(&s->lock);
    return 1;
}
int nj_android_gles2_set_window(nj_android_gles2 *s, void *native_window)
{
    int ok = 0;
    if (!s) return 0;
    if (native_window && !s->initialized) start(s, -1, 1);
    pthread_mutex_lock(&s->lock);
    if (owning_thread(s) && (!native_window || !s->failed) && s->initialized &&
        s->display != EGL_NO_DISPLAY) {
        if (s->window_ptr != native_window || !native_window) {
            if (s->window != EGL_NO_SURFACE) {
                (void)eglDestroySurface(s->display, s->window);
                s->window = EGL_NO_SURFACE;
            }
            s->window_ptr = native_window;
            s->surface_w = s->surface_h = 0;
        }
        if (!native_window) {
            ok = 1;
        } else if (s->window_ok && s->config != NULL) {
            EGLint sw = 0, sh = 0;
            if (s->window == EGL_NO_SURFACE) {
                s->window = eglCreateWindowSurface(
                    s->display, s->config, (EGLNativeWindowType)native_window, NULL);
                if (s->window == EGL_NO_SURFACE)
                    __android_log_print(ANDROID_LOG_WARN, GLES2_TAG,
                                        "gles2: no window surface (EGL 0x%x); software presentation",
                                        (unsigned int)eglGetError());
            }
            /* The same SurfaceView/ANativeWindow can be resized between swaps. */
            if (s->window != EGL_NO_SURFACE &&
                eglQuerySurface(s->display, s->window, EGL_WIDTH, &sw) &&
                eglQuerySurface(s->display, s->window, EGL_HEIGHT, &sh) && sw > 0 && sh > 0) {
                s->surface_w = (int)sw;
                s->surface_h = (int)sh;
                ok = 1;
            }
        }
    }
    pthread_mutex_unlock(&s->lock);
    return ok;
}

int nj_android_gles2_present(nj_android_gles2 *s, const nj_surface *src,
                             int mode, float sharp, int vp_x, int vp_y, int vp_w, int vp_h)
{
    int ok = 0;
    unsigned long long t0, t1;
    if (!s || src == NULL) return 0;
    if (--s->present_stats.refresh <= 0) {
        s->present_stats.refresh = 60;
        s->present_stats.enabled = present_stats_wanted();
        if (!s->present_stats.enabled) s->present_stats.frames = 0;
    }
    t0 = present_now_ns();
    pthread_mutex_lock(&s->lock);
    if (owning_thread(s) && s->backend != NULL && s->window != EGL_NO_SURFACE &&
        !s->failed && s->context != EGL_NO_CONTEXT) {
        if (!eglMakeCurrent(s->display, s->window, s->window, s->context)) {
            failed(s, "eglMakeCurrent(window) failed", eglGetError());
        } else {
            s->current = 0;
            if (usable(s)) {
                /* vp_y arrives in window (top-down) pixels; glViewport is
                 * bottom-up, so convert through the real surface height. */
                int gl_y = s->surface_h - vp_y - vp_h;
                if (mode == 5 && s->applied_sharp != sharp) {
                    nj_gles2_present_set_sharp(s->backend, sharp);
                    s->applied_sharp = sharp;
                }
                ok = nj_gles2_present_onscreen(s->backend, src, mode,
                                               vp_x, gl_y, vp_w, vp_h);
                t1 = present_now_ns();
            }
            if (ok && !s->dump_done) {
                /* One-shot debug dump of the presented window framebuffer
                 * (GL rows bottom-up -> PPM rows top-down). */
                char prop[PROP_VALUE_MAX];
                prop[0] = '\0';
                (void)__system_property_get("debug.nj2me.gldump", prop);
                if (prop[0] != '\0' && s->surface_w > 0 && s->surface_h > 0 &&
                    (unsigned)s->surface_w <= NJ_GL_PRESENT_MAX_PIXELS / (unsigned)s->surface_h) {
                    size_t bytes = (size_t)s->surface_w * (size_t)s->surface_h * 4u;
                    unsigned char *px = (unsigned char *)malloc(bytes);
                    if (px != NULL) {
                        int y;
                        int gy = s->surface_h - vp_y - vp_h;
                        /* Some window drivers return black for framebuffer 0.
                         * Replay the same clipped viewport into a readable FBO. */
                        FILE *fp = NULL;
                        if (nj_gles2_present_onscreen_capture(s->backend, src, mode,
                                vp_x, gy, vp_w, vp_h, s->surface_w, s->surface_h, px))
                            fp = fopen(prop, "wb");
                        if (fp != NULL) {
                            fprintf(fp, "P6\n%d %d\n255\n", s->surface_w, s->surface_h);
                            for (y = s->surface_h - 1; y >= 0; --y) {
                                unsigned char *row = px + (size_t)y * (size_t)s->surface_w * 4u;
                                int x;
                                for (x = 0; x < s->surface_w; ++x) {
                                    unsigned char rgb[3];
                                    rgb[0] = row[x * 4 + 0];
                                    rgb[1] = row[x * 4 + 1];
                                    rgb[2] = row[x * 4 + 2];
                                    fwrite(rgb, 1, 3, fp);
                                }
                            }
                            fclose(fp);
                            __android_log_print(ANDROID_LOG_INFO, GLES2_TAG,
                                                "present: dumped %dx%d framebuffer -> %s",
                                                s->surface_w, s->surface_h, prop);
                        }
                        free(px);
                        s->dump_done = 1;
                    }
                }
            }
            /* A failed swap must let the caller present this frame in software.
             * Bind the pbuffer before releasing a resized/lost window surface. */
            if (ok && !eglSwapBuffers(s->display, s->window)) {
                EGLint error = eglGetError();
                ok = 0;
                if (error == EGL_CONTEXT_LOST)
                    failed(s, "eglSwapBuffers lost context", error);
            }
            if (ok)
                present_stats_record(s, t1 > t0 ? t1 - t0 : 0,
                                     present_now_ns() - (t1 > t0 ? t1 : t0),
                                     mode, s->surface_w, s->surface_h);
            if (!eglMakeCurrent(s->display, s->pbuffer, s->pbuffer, s->context)) {
                failed(s, "eglMakeCurrent(pbuffer restore) failed", eglGetError());
            } else {
                s->current = 1;
                s->version_ok = 0;
            }
            if (!ok) {
                (void)eglDestroySurface(s->display, s->window);
                s->window = EGL_NO_SURFACE;
                s->surface_w = s->surface_h = 0;
            }
        }
        publish(s);
    }
    pthread_mutex_unlock(&s->lock);
    return ok;
}

#endif
