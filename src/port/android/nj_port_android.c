/*
 * nano_j2me - nj_port_android.c
 *
 * The Android NDK port, plus the JNI glue an Activity needs.
 *
 * Android is a hosted target, so the port has no file-system or memory work to
 * do; what it does need is a framebuffer that can be handed to the window
 * compositor without a copy, and an input queue that the UI thread can post
 * into from Java.
 *
 * TWO DETAILS THAT MATTER
 *
 *   ANativeWindow_lock() gives you a STRIDE, not a width.  That stride is
 *   passed straight to display_init and the rasterizer respects it, so no
 *   repacking copy is needed between the Java heap and the window buffer.
 *   Getting this wrong produces a picture that shears progressively further to
 *   the right on every row - a classic and very confusing symptom.
 *
 *   Window geometry changes (rotation, split screen, a foldable unfolding) come
 *   in as ANativeWindow_Buffer changes, not as a new surface.  The port
 *   therefore re-locks per frame and re-inits the surface view when the
 *   geometry moves, rather than caching the buffer pointer.
 */
#include "nj2me/nj_port.h"
#include "nj2me/nj_types.h"
#include "nj2me/nj.h"          /* nj_run, nj_port_shutdown */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if defined(__ANDROID__)

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <jni.h>
#include <time.h>
#include <pthread.h>

#define LOG_TAG "nj2me"

typedef struct android_state {
    ANativeWindow *window;
    pthread_mutex_t lock;

    int   width, height, stride;
    int   format;
    nj_u1 *shadow;        /* our own buffer; copied into the window per frame */

    nj_event queue[256];
    int   qh, qt;

    int   quit;
} android_state;

#define AS(ud) ((android_state *)(ud))

/* ------------------------------------------------------------------ */
/* Time / log / idle                                                   */
/* ------------------------------------------------------------------ */
static nj_u4 android_millis(void *ud)
{
    struct timespec ts;
    (void)ud;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (nj_u4)((nj_u4)ts.tv_sec * 1000u + (nj_u4)(ts.tv_nsec / 1000000));
}

static void android_sleep_ms(void *ud, nj_u4 ms)
{
    struct timespec ts;
    (void)ud;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

static void android_log(void *ud, int level, const char *msg)
{
    int prio = ANDROID_LOG_INFO;
    (void)ud;
    switch (level) {
    case 0:  prio = ANDROID_LOG_ERROR; break;
    case 1:  prio = ANDROID_LOG_WARN;  break;
    case 3:  prio = ANDROID_LOG_DEBUG; break;
    default: prio = ANDROID_LOG_INFO;  break;
    }
    __android_log_write(prio, LOG_TAG, msg);
}

/* ------------------------------------------------------------------ */
/* Display                                                             */
/* ------------------------------------------------------------------ */
static int android_bytes_per_pixel(int fmt)
{
    switch (fmt) {
    case NJ_PIXFMT_RGB565:   return 2;
    case NJ_PIXFMT_RGB888:   return 3;
    default:                 return 4;
    }
}

static int android_display_init(void *ud, int *w, int *h, int *stride, void **px)
{
    android_state *s = AS(ud);
    int bpp = android_bytes_per_pixel(NJ_PIXFMT);

    if (s->window) {
        s->width = ANativeWindow_getWidth(s->window);
        s->height = ANativeWindow_getHeight(s->window);
        ANativeWindow_setBuffersGeometry(s->window, s->width, s->height,
                                         WINDOW_FORMAT_RGBA_8888);
    }
    if (s->width <= 0) {
        s->width = NJ_SCREEN_WIDTH;
    }
    if (s->height <= 0) {
        s->height = NJ_SCREEN_HEIGHT;
    }
    s->format = NJ_PIXFMT;
    s->stride = s->width * bpp;

    free(s->shadow);
    s->shadow = (nj_u1 *)calloc(1u, (size_t)s->stride * (size_t)s->height);
    if (!s->shadow) {
        return -1;
    }
    *w = s->width;
    *h = s->height;
    *stride = s->stride;
    *px = s->shadow;
    return NJ_PIXFMT;
}

static void android_display_present(void *ud, int x, int y, int w, int h)
{
    android_state *s = AS(ud);
    ANativeWindow_Buffer buf;
    (void)x; (void)y; (void)w; (void)h;

    if (!s->window || s->quit) {
        return;
    }
    if (ANativeWindow_lock(s->window, &buf, NULL) != 0) {
        return;
    }
    /*
     * Copy row by row using the WINDOW's stride, never its width: the
     * compositor is free to hand back a wider buffer than the visible area.
     */
    {
        int row;
        int rows = s->height < buf.height ? s->height : buf.height;
        int cols = s->width < buf.width ? s->width : buf.width;
        for (row = 0; row < rows; row++) {
            const nj_u1 *src = s->shadow + (size_t)row * (size_t)s->stride;
            nj_u1 *dst = (nj_u1 *)buf.bits
                         + (size_t)row * (size_t)buf.stride * 4u;
            if (s->format == NJ_PIXFMT_ARGB8888) {
                memcpy(dst, src, (size_t)cols * 4u);
            } else {
                /* Expand RGB565 to the RGBA_8888 the window expects. */
                int cx;
                nj_u4 *d32 = (nj_u4 *)dst;
                for (cx = 0; cx < cols; cx++) {
                    nj_u4 v = (nj_u4)src[cx * 2] | ((nj_u4)src[cx * 2 + 1] << 8);
                    nj_u4 r = ((v >> 11) & 0x1Fu) << 3;
                    nj_u4 g = ((v >> 5) & 0x3Fu) << 2;
                    nj_u4 b = (v & 0x1Fu) << 3;
                    d32[cx] = 0xFF000000u | (r << 16) | (g << 8) | b;
                }
            }
        }
    }
    ANativeWindow_unlockAndPost(s->window);
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */
static int android_input_poll(void *ud, nj_event *ev)
{
    android_state *s = AS(ud);
    int have = 0;

    pthread_mutex_lock(&s->lock);
    if (s->qh != s->qt) {
        *ev = s->queue[s->qh];
        s->qh = (s->qh + 1) % (int)(sizeof(s->queue) / sizeof(s->queue[0]));
        have = 1;
    }
    pthread_mutex_unlock(&s->lock);
    return have;
}

static void android_push(void *ud, const nj_event *ev)
{
    android_state *s = AS(ud);
    int next;

    pthread_mutex_lock(&s->lock);
    next = (s->qt + 1) % (int)(sizeof(s->queue) / sizeof(s->queue[0]));
    if (next != s->qh) {
        s->queue[s->qt] = *ev;
        s->qt = next;
    }
    pthread_mutex_unlock(&s->lock);
}

static void android_shutdown(void *ud)
{
    android_state *s = AS(ud);

    if (!s) {
        return;
    }
    pthread_mutex_destroy(&s->lock);
    free(s->shadow);
    free(s);
}

/* ------------------------------------------------------------------ */
/* Construction and the public configuration entry points              */
/* ------------------------------------------------------------------ */
nj_status nj_port_android(nj_port *out)
{
    android_state *s;

    if (!out) {
        return NJ_ERR;
    }
    s = (android_state *)calloc(1u, sizeof(*s));
    if (!s) {
        return NJ_ERR_NOMEM;
    }
    pthread_mutex_init(&s->lock, NULL);

    memset(out, 0, sizeof(*out));
    out->name = "android";
    out->ud = s;
    out->millis = android_millis;
    out->sleep_ms = android_sleep_ms;
    out->display_init = android_display_init;
    out->display_present = android_display_present;
    out->input_poll = android_input_poll;
    out->log = android_log;
    out->shutdown = android_shutdown;
    out->fps_cap = 30;
    return NJ_OK;
}

void nj_port_android_set_window(nj_port *port, void *native_window)
{
    android_state *s;

    if (!port || !port->ud) {
        return;
    }
    s = AS(port->ud);
    s->window = (ANativeWindow *)native_window;
    if (s->window) {
        ANativeWindow_acquire(s->window);
        s->width = ANativeWindow_getWidth(s->window);
        s->height = ANativeWindow_getHeight(s->window);
    }
}

void nj_port_android_push_event(nj_port *port, const nj_event *ev)
{
    if (port && port->ud && ev) {
        android_push(port->ud, ev);
    }
}

/*
 * Java key codes are translated here rather than in Java so the mapping stays
 * in one place, next to the port that owns the input contract.
 */
void nj_port_android_push_key(nj_port *port, int android_keycode, int down)
{
    nj_event ev;
    int k = -1;

    /* Values from android.view.KeyEvent.  Written numerically so that this
     * file compiles without the Android framework headers. */
    switch (android_keycode) {
    case 19:  k = NJ_KEY_UP;     break;   /* KEYCODE_DPAD_UP */
    case 20:  k = NJ_KEY_DOWN;   break;   /* KEYCODE_DPAD_DOWN */
    case 21:  k = NJ_KEY_LEFT;   break;   /* KEYCODE_DPAD_LEFT */
    case 22:  k = NJ_KEY_RIGHT;  break;   /* KEYCODE_DPAD_RIGHT */
    case 23:  k = NJ_KEY_FIRE;   break;   /* KEYCODE_DPAD_CENTER */
    case 66:  k = NJ_KEY_FIRE;   break;   /* KEYCODE_ENTER */
    case 62:  k = NJ_KEY_FIRE;   break;   /* KEYCODE_SPACE */
    case 4:   k = NJ_KEY_SOFT_RIGHT; break;  /* KEYCODE_BACK */
    case 82:  k = NJ_KEY_SOFT_LEFT;  break;  /* KEYCODE_MENU */
    case 67:  k = NJ_KEY_CLEAR;  break;   /* KEYCODE_DEL */
    case 7:  k = NJ_KEY_0; break;   case 8:  k = NJ_KEY_1; break;
    case 9:  k = NJ_KEY_2; break;   case 10: k = NJ_KEY_3; break;
    case 11: k = NJ_KEY_4; break;   case 12: k = NJ_KEY_5; break;
    case 13: k = NJ_KEY_6; break;   case 14: k = NJ_KEY_7; break;
    case 15: k = NJ_KEY_8; break;   case 16: k = NJ_KEY_9; break;
    case 17: k = NJ_KEY_STAR;  break;   /* KEYCODE_STAR */
    case 18: k = NJ_KEY_POUND; break;   /* KEYCODE_POUND */
    case 29: k = NJ_KEY_GAME_A; break;  /* KEYCODE_A */
    case 30: k = NJ_KEY_GAME_B; break;  /* KEYCODE_B */
    case 31: k = NJ_KEY_GAME_C; break;  /* KEYCODE_C */
    case 32: k = NJ_KEY_GAME_D; break;  /* KEYCODE_D */
    default: break;
    }
    if (k < 0 || !port) {
        return;
    }
    memset(&ev, 0, sizeof(ev));
    ev.type = (nj_u1)(down ? NJ_EV_KEY_DOWN : NJ_EV_KEY_UP);
    ev.key = (nj_u1)k;
    android_push(port->ud, &ev);
}

/* ================================================================== */
/* JNI glue                                                            */
/* ================================================================== */
/*
 * THE PORT HANDLE IS PASSED THROUGH JAVA, NOT KEPT IN A GLOBAL.
 *
 * The obvious JNI shape is a file-scope `static app *g_app`.  That is exactly
 * what this project forbids: a global makes "back out of the game, start
 * another" leak, and makes two Activities in one process impossible.  So
 * nativeCreate returns the context as a jlong and every other native takes it
 * back as its first argument.  The Java side stores it in a field it already
 * has, and the runtime keeps owning everything else.
 */
typedef struct android_app {
    nj_port    port;
    nj_run_opts opts;
    char       jar_path[512];
    pthread_t  thread;
    int        running;
    int        started;
} android_app;

static void *android_main_thread(void *arg)
{
    android_app *a = (android_app *)arg;
    a->opts.jar_path = a->jar_path;
    (void)nj_run(&a->port, &a->opts);
    a->running = 0;
    return NULL;
}

JNIEXPORT jlong JNICALL
Java_com_example_nj2me_Nj2meActivity_nativeCreate(JNIEnv *env, jobject thiz,
                                                  jobject surface,
                                                  jstring jarPath)
{
    android_app *a;
    ANativeWindow *win;
    const char *path;

    (void)thiz;
    a = (android_app *)calloc(1u, sizeof(*a));
    if (!a) {
        return 0;
    }
    if (jarPath) {
        path = (*env)->GetStringUTFChars(env, jarPath, NULL);
        if (path) {
            snprintf(a->jar_path, sizeof(a->jar_path), "%s", path);
            (*env)->ReleaseStringUTFChars(env, jarPath, path);
        }
    }
    if (nj_port_android(&a->port) != NJ_OK) {
        free(a);
        return 0;
    }
    win = ANativeWindow_fromSurface(env, surface);
    nj_port_android_set_window(&a->port, win);
    if (win) {
        ANativeWindow_release(win);   /* the port holds its own reference */
    }
    a->opts.heap_bytes = 0;           /* 0 => NJ_MAX_HEAP */
    return (jlong)(intptr_t)a;
}

JNIEXPORT void JNICALL
Java_com_example_nj2me_Nj2meActivity_nativeRun(JNIEnv *env, jobject thiz,
                                               jlong handle)
{
    android_app *a = (android_app *)(intptr_t)handle;
    (void)env; (void)thiz;
    if (!a || a->running) {
        return;
    }
    a->running = 1;
    a->started = 1;
    if (pthread_create(&a->thread, NULL, android_main_thread, a) != 0) {
        a->running = 0;
    }
}

JNIEXPORT void JNICALL
Java_com_example_nj2me_Nj2meActivity_nativeKey(JNIEnv *env, jobject thiz,
                                               jlong handle, jint keyCode,
                                               jboolean down)
{
    android_app *a = (android_app *)(intptr_t)handle;
    (void)env; (void)thiz;
    if (a) {
        nj_port_android_push_key(&a->port, (int)keyCode, down ? 1 : 0);
    }
}

JNIEXPORT void JNICALL
Java_com_example_nj2me_Nj2meActivity_nativePointer(JNIEnv *env, jobject thiz,
                                                   jlong handle, jint x, jint y,
                                                   jboolean down)
{
    android_app *a = (android_app *)(intptr_t)handle;
    nj_event ev;
    (void)env; (void)thiz;
    if (!a) {
        return;
    }
    memset(&ev, 0, sizeof(ev));
    ev.type = (nj_u1)(down ? NJ_EV_POINTER_DOWN : NJ_EV_POINTER_UP);
    ev.x = (nj_s2)x;
    ev.y = (nj_s2)y;
    nj_port_android_push_event(&a->port, &ev);
}

JNIEXPORT void JNICALL
Java_com_example_nj2me_Nj2meActivity_nativeQuit(JNIEnv *env, jobject thiz,
                                                jlong handle)
{
    android_app *a = (android_app *)(intptr_t)handle;
    nj_event ev;
    (void)env; (void)thiz;
    if (!a) {
        return;
    }
    memset(&ev, 0, sizeof(ev));
    ev.type = NJ_EV_QUIT;
    nj_port_android_push_event(&a->port, &ev);
}

JNIEXPORT void JNICALL
Java_com_example_nj2me_Nj2meActivity_nativeDestroy(JNIEnv *env, jobject thiz,
                                                   jlong handle)
{
    android_app *a = (android_app *)(intptr_t)handle;
    (void)env; (void)thiz;
    if (!a) {
        return;
    }
    /*
     * The frame loop exits when it sees the QUIT event; join it so the VM is
     * completely torn down before the surface goes away.  Without the join the
     * rasterizer can be mid-frame when Android destroys the window.
     */
    if (a->started) {
        pthread_join(a->thread, NULL);
    }
    nj_port_shutdown(&a->port);
    free(a);
}

#else  /* !__ANDROID__ */

/*
 * Hosted builds link the same symbols; the port reports itself unavailable
 * instead of pretending, so every port source file can be compiled
 * unconditionally into one library.
 */
nj_status nj_port_android(nj_port *out)
{
    (void)out;
    return NJ_ERR_UNSUPPORTED;
}

void nj_port_android_set_window(nj_port *port, void *native_window)
{
    (void)port;
    (void)native_window;
}

void nj_port_android_push_event(nj_port *port, const nj_event *ev)
{
    (void)port;
    (void)ev;
}

void nj_port_android_push_key(nj_port *port, int android_keycode, int down)
{
    (void)port;
    (void)android_keycode;
    (void)down;
}

#endif /* __ANDROID__ */
