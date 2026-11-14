/* Optional Android GLES2 binding. All EGL/GL work belongs to nj_run's thread;
 * ANativeWindow remains the existing CPU framebuffer presentation path. */
#include "nj_android_gles2.h"

#if defined(__ANDROID__) && NJ_ANDROID_GLES2 && !NJ_ANDROID_LEGACY
#include <GLES2/gl2.h>
#include <android/log.h>
#include <sys/system_properties.h>
#include <string.h>

/* 1: install the optional 2D fill/blit GPU callbacks (see the comment at the
 * wiring site; the 3D triangle list path is unaffected). */
#ifndef NJ_ANDROID_GLES2_2D_ACCEL
#define NJ_ANDROID_GLES2_2D_ACCEL 0
#endif

#define GLES2_TAG "nj2me"

/* Caller holds lock. Publishing never calls GL and never acquires a port or
 * Java lock. Readers take info_lock only, so driver stalls cannot block UI. */
static void publish(nj_android_gles2 *s)
{
    nj_android_gles2_info info;
    memset(&info, 0, sizeof info);
    info.state = s->stopped ? 4 : s->failed ? 3 : s->backend ? 2 : s->requested ? 1 : 0;
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
           eglGetCurrentSurface(EGL_DRAW) == s->pbuffer &&
           eglGetCurrentSurface(EGL_READ) == s->pbuffer;
}

static void failed(nj_android_gles2 *s, const char *stage, EGLint error)
{
    __android_log_print(ANDROID_LOG_WARN, GLES2_TAG,
                        "gles2: %s (EGL 0x%x); software fallback for this run",
                        stage, (unsigned int)error);
    s->failed = 1;
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
    if (!glGetString(GL_VERSION)) {
        failed(s, "GL context unavailable", EGL_CONTEXT_LOST);
        return 0;
    }
    return 1;
}

static int make_current(nj_android_gles2 *s)
{
    if (!owning_thread(s) || s->failed || s->context == EGL_NO_CONTEXT)
        return 0;
    /* Rebinding also lets EGL report EGL_CONTEXT_LOST before backend GL calls.
     * Never borrow a UI/Surface context or use a window surface. */
    if (!eglMakeCurrent(s->display, s->pbuffer, s->pbuffer, s->context)) {
        failed(s, "eglMakeCurrent failed", eglGetError());
        return 0;
    }
    return usable(s);
}

static int fill_rect(void *ud, const nj_gfx *g, int x, int y, int w, int h)
{
    nj_android_gles2 *s = (nj_android_gles2 *)ud;
    int result = NJ_GFX_ACCEL_DECLINE;
    pthread_mutex_lock(&s->lock);
    if (s->backend && make_current(s)) {
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
    if (s->backend && make_current(s)) {
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
    if (s->backend && make_current(s)) {
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
    if (s->backend && make_current(s)) {
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
    if (s->backend && make_current(s)) {
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
    if (s->backend && make_current(s)) {
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
    if (s->backend && make_current(s)) {
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
    if (s->backend && make_current(s)) nj_gles2_triangles_abort(s->backend);
    pthread_mutex_unlock(&s->lock);
}

static const nj_gfx_accel wrappers = {NJ_GFX_ACCEL_VERSION, fill_rect, blit};

int nj_android_gles2_bind(nj_android_gles2 *s, nj_port *port)
{
    memset(s, 0, sizeof(*s));
    s->display = EGL_NO_DISPLAY;
    s->context = EGL_NO_CONTEXT;
    s->pbuffer = EGL_NO_SURFACE;
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

void nj_android_gles2_start_mode(nj_android_gles2 *s, int mode)
{
    static const EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
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
    /* Publish pending before the first driver call, which may be slow. */
    publish(s);
    if (!s->requested) goto done;
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
    if (!eglChooseConfig(s->display, config_attrs, &config, 1, &count) || count < 1) {
        failed(s, "no GLES2 pbuffer config", eglGetError());
        goto fail;
    }
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
#endif
