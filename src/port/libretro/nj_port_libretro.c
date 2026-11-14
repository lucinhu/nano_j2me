/*
 * nano_j2me - libretro core
 *
 * A frontend-neutral port: the same runtime that apps/nj2me_main.c drives from
 * a terminal runs inside RetroArch (or any libretro frontend) as a core.
 *
 *     game.jar  ->  retro_load_game()  ->  nj_vm  ->  retro_run()
 *
 * Two facts about libretro shape this file:
 *
 *  1. libretro entry points are process-wide functions; the frontend registers
 *     its callbacks through retro_set_*() and there is no per-instance handle.
 *     The entire core therefore owns exactly one instance, `static nj_lr
 *     g_core`, and every `retro_*` function operates on it.  This is the only
 *     mutable state this port has and it is inherent to the libretro ABI, not
 *     a runtime shortcut - the runtime itself is still stateless as
 *     tools/check_no_statics.sh enforces for src/ (this file is exempt; see
 *     that script).
 *
 *  2. retro_run() must not block for long, but a MIDlet may run its own frame
 *     loop inside startApp(), call Thread.sleep() for seconds, or paint from a
 *     constructor.  So the VM runs on its own thread and this file owns:
 *
 *       - a virtual clock advanced once per retro_run() call (so a Thread.sleep
 *         becomes "wait until the frontend has run that many frames", which is
 *         what makes pause and fast-forward work),
 *       - port.sleep_ms/port.idle that park the VM thread on that clock,
 *       - one framebuffer that the runtime paints into and a second one the
 *         frontend is handed, so paint and present cannot tear,
 *       - a 44.1 kHz stereo ring the runtime's PCM/tone hooks write and
 *         retro_run() drains,
 *       - an event queue the frontend fills and the runtime's input_poll()
 *         drains.
 *
 * What is deliberately not emulated: the handset shell, JAD files (the JAR's
 * manifest names the MIDlet), network and Bluetooth (leave HttpConnection
 * refused by name rather than pretending) and MIDI-to-GM handoff (the runtime
 * renders MIDI itself into the same PCM path).  Save states are supported:
 * the frontend asks from its own thread, so retro_serialize() hands a request
 * to the VM thread - only the thread that owns the VM may take a snapshot -
 * and waits for it to reach a frame boundary (see "Save states" below).
 *
 * Copyright (C) 2026  nano_j2me contributors - MIT, like the rest of the tree.
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#  include <unistd.h>
#endif

#include "libretro.h"
#include "nj2me/nj.h"
#include "nj2me/nj_inflate.h"
#include "../posix/nj_posix_dir.h"

#ifndef NJ_LIBRETRO_HW
#  define NJ_LIBRETRO_HW 0
#endif
#if NJ_LIBRETRO_HW
#  if !NJ_ENABLE_GFX_ACCEL
#    error "NJ_LIBRETRO_HW requires NJ_ENABLE_GFX_ACCEL=1"
#  endif
#  include "nj_libretro_hw.h"
#endif

/* ------------------------------------------------------------------ */
/* Platform layer: threads, clocks, directories                       */
/*                                                                    */
/* The runtime is C99 and so is this file; both pthreads and Win32    */
/* threads have a small enough surface to wrap here.  Everything else */
/* in the core is platform-neutral.                                   */
/* ------------------------------------------------------------------ */

#if defined(_WIN32)
#  if !defined(WIN32_LEAN_AND_MEAN)
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <direct.h>
#  include <process.h>
typedef CRITICAL_SECTION lr_mutex;
typedef CONDITION_VARIABLE lr_cond;
typedef struct lr_thread {
    HANDLE          handle;
    void *(*fn)(void *);
    void           *arg;
} lr_thread;

static void lr_mutex_init(lr_mutex *m)    { InitializeCriticalSection(m); }
static void lr_mutex_destroy(lr_mutex *m) { DeleteCriticalSection(m); }
static void lr_mutex_lock(lr_mutex *m)    { EnterCriticalSection(m); }
static void lr_mutex_unlock(lr_mutex *m)  { LeaveCriticalSection(m); }
static void lr_cond_init(lr_cond *c)      { InitializeConditionVariable(c); }
static void lr_cond_destroy(lr_cond *c)   { (void)c; }
static void lr_cond_wait(lr_cond *c, lr_mutex *m)
{
    (void)SleepConditionVariableCS(c, m, INFINITE);
}
static void lr_cond_broadcast(lr_cond *c) { WakeAllConditionVariable(c); }

/* Timed wait in real milliseconds: 1 when signalled, 0 on timeout (the
 * pthread/Win32 return values are not part of the port surface). */
static int lr_cond_wait_ms(lr_cond *c, lr_mutex *m, unsigned ms)
{
    return SleepConditionVariableCS(c, m, (DWORD)ms) ? 1 : 0;
}

static unsigned __stdcall lr_thread_tramp(void *p)
{
    lr_thread *t = (lr_thread *)p;
    t->fn(t->arg);
    return 0;
}

static int lr_thread_start(lr_thread *t, void *(*fn)(void *), void *arg)
{
    t->fn = fn;
    t->arg = arg;
    t->handle = (HANDLE)_beginthreadex(NULL, 4u * 1024u * 1024u,
                                       lr_thread_tramp, t, 0, NULL);
    return t->handle ? 0 : -1;
}

static void lr_thread_join(lr_thread *t)
{
    if (t->handle) {
        (void)WaitForSingleObject(t->handle, INFINITE);
        (void)CloseHandle(t->handle);
        t->handle = NULL;
    }
}

static uint64_t lr_real_ms(void) { return (uint64_t)GetTickCount64(); }
static void lr_real_sleep(nj_u4 ms) { if (ms) Sleep((DWORD)ms); }
static void lr_mkdir(const char *path) { (void)_mkdir(path); }

#else /* POSIX */

#  include <errno.h>
#  include <pthread.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <time.h>
#  include <unistd.h>

typedef pthread_mutex_t lr_mutex;
typedef pthread_cond_t  lr_cond;
typedef pthread_t       lr_thread;

static void lr_mutex_init(lr_mutex *m)    { (void)pthread_mutex_init(m, NULL); }
static void lr_mutex_destroy(lr_mutex *m) { (void)pthread_mutex_destroy(m); }
static void lr_mutex_lock(lr_mutex *m)    { (void)pthread_mutex_lock(m); }
static void lr_mutex_unlock(lr_mutex *m)  { (void)pthread_mutex_unlock(m); }
static void lr_cond_init(lr_cond *c)      { (void)pthread_cond_init(c, NULL); }
static void lr_cond_destroy(lr_cond *c)   { (void)pthread_cond_destroy(c); }
static void lr_cond_wait(lr_cond *c, lr_mutex *m)
{
    (void)pthread_cond_wait(c, m);
}
static void lr_cond_broadcast(lr_cond *c) { (void)pthread_cond_broadcast(c); }

/* Timed wait in real milliseconds: 1 when signalled, 0 on timeout. */
static int lr_cond_wait_ms(lr_cond *c, lr_mutex *m, unsigned ms)
{
    struct timespec ts;
    int rc;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0;
    }
    ts.tv_sec  += (time_t)(ms / 1000u);
    ts.tv_nsec += (long)(ms % 1000u) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec  += 1;
        ts.tv_nsec -= 1000000000L;
    }
    rc = pthread_cond_timedwait(c, m, &ts);
    return rc == 0 ? 1 : 0;
}

static int lr_thread_start(lr_thread *t, void *(*fn)(void *), void *arg)
{
    return pthread_create(t, NULL, fn, arg) == 0 ? 0 : -1;
}

static void lr_thread_join(lr_thread *t) { (void)pthread_join(*t, NULL); }

static uint64_t lr_real_ms(void)
{
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
    }
#endif
    return (uint64_t)time(NULL) * 1000u;
}

static void lr_real_sleep(nj_u4 ms)
{
    struct timespec ts;
    if (!ms) return;
    ts.tv_sec  = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { }
}

static void lr_mkdir(const char *path) { (void)mkdir(path, 0755); }

#endif

#if NJ_LIBRETRO_HW
#  if defined(_WIN32)
typedef DWORD lr_thread_id;
static lr_thread_id lr_thread_self(void) { return GetCurrentThreadId(); }
static int lr_thread_equal(lr_thread_id a, lr_thread_id b) { return a == b; }
#  else
typedef pthread_t lr_thread_id;
static lr_thread_id lr_thread_self(void) { return pthread_self(); }
static int lr_thread_equal(lr_thread_id a, lr_thread_id b) { return pthread_equal(a, b); }
#  endif
#endif

/* ------------------------------------------------------------------ */
/* Constants and core state                                           */
/* ------------------------------------------------------------------ */

#define LR_AUDIO_RATE      44100
#define LR_FRAMES_PER_RUN  735   /* 44100 / 60, exact */
#define LR_AUDIO_FRAMES    88200 /* two seconds of stereo slack */
#define LR_EVENT_QUEUE     512
#define LR_PATH_MAX        4096
#define LR_TONE_AMPLITUDE  6000
#define LR_MAX_OPTIONS     16
#define LR_MAX_VALUES      32
#define LR_TEXT_INPUT_MAX  512

#define LR_PHASE_STOP 0
#define LR_PHASE_LOAD 1  /* virtual clock fast-forwards: load must not wait  */
#define LR_PHASE_RUN  2  /* virtual clock driven by retro_run()             */

/* Save-state request queue (frontend thread asks, VM thread serves). */
#define LR_REQ_NONE 0
#define LR_REQ_SAVE 1
/* How long retro_serialize() waits for a frame boundary before it gives up.
 * The frontend is blocked while it waits, so this may not be forever: a
 * MIDlet stuck inside a native call must not hang RetroArch. */
#define LR_STATE_TIMEOUT_MS 4000u
/* Snapshot envelope: explicit little-endian fields, CRC over header + payload.
 * The VM's snapshot format still enforces its own architecture/ABI checks. */
#define LR_REQ_LOAD 2
#define LR_STATE_HEADER 64u
#define LR_STATE_MAGIC 0x524c4a4eu /* NJLR */
#define LR_STATE_VERSION 2u /* adds external font identity at byte 60 */

typedef struct lr_opt_value {
    const char *value;
    const char *label;
} lr_opt_value;

#if NJ_LIBRETRO_HW
#define LR_HW_EMPTY     0
#define LR_HW_PENDING   1
#define LR_HW_EXECUTING 2
#define LR_HW_COMPLETE  3
#define LR_HW_FILL      1
#define LR_HW_BLIT      2
#define LR_HW_RASTER    3
#define LR_HW_TRIANGLES 4
#define LR_HW_TRIANGLES_LIST 5
#define LR_HW_BUDGET_MS 2u
#define LR_HW_MAX_OPS   256u
#define LR_HW_QUEUE_MS  100u

typedef struct lr_hw_request {
    int kind;
    const nj_gfx *gfx;
    int x, y, w, h;
    const nj_gfx_blit *blit;
    const nj_gfx_raster *raster;
    const nj_gfx_triangles *triangles;
    const nj_gfx_triangles_list *triangles_list;
    nj_u4 *result;
} lr_hw_request;

typedef struct lr_hw {
    /* gl_lock serializes frontend GL sections with reset/destroy. Worker never
     * takes it. Fields below ready/gate/slot are protected by nj_lr.lock. */
    lr_mutex gl_lock;
    struct retro_hw_render_callback callback;
    const nj_libretro_hw_driver *driver;
    void *context;
    int requested, accepted, ready, gate, slot, rc;
    unsigned generation;
    lr_hw_request request;
    lr_thread_id worker, gl_thread;
    int worker_known, gl_section;
    int parked;
    unsigned long long park_until;
    unsigned long long fills, blits, rasters, triangles, triangle_lists,
                       declines;
    unsigned long long queued, executed, cancelled, presents;
} lr_hw;
#endif

typedef struct nj_lr {
    /* ---- frontend callbacks (registered once) ---- */
    retro_environment_t        environ_cb;
    retro_video_refresh_t      video_cb;
    retro_audio_sample_t       sample_cb;
    retro_audio_sample_batch_t batch_cb;
    retro_input_poll_t         poll_cb;
    retro_input_state_t        input_cb;
    retro_log_printf_t         log_cb;

    /* ---- one-time init ---- */
    int  sync_ready;

    /* ---- content ---- */
    char          game_path[LR_PATH_MAX];
    unsigned char *jar;
    size_t         jar_size;
    char           stem[128];
    int            loaded;

    /* ---- configuration read at load time ---- */
    const nj_device *device;
    int   rotation;   /* 0, 90, 180 or 270 */
    int   heap_mb;
    int   sound;
    int   pointer;
    int   font_mode;  /* 0 = auto, 1 = frontend system directory, 2 = builtin */
    unsigned char *font_data; /* pinned for this session, including state loads */
    size_t font_size;
    nj_u4 font_crc;
    int   button_key[8]; /* nj_key for A, B, X, Y, L, R, Select, Start */
    char  locale[16];    /* resolved microedition.locale, "en-US" = default */
    int   screen_w, screen_h; /* requested canvas; 0 = the built-in size */

    /* Property overrides appended to the selected device profile for this
     * session.  The VM borrows the table, so it must outlive the VM. */
    nj_device_prop *sys_props;
    nj_u4           nsys_props;

    /* ---- frontend feature negotiation ---- */
    int                      input_bitmask; /* frontend accepts the mask call */
    retro_set_rumble_state_t rumble_cb;     /* NULL: no rumble interface    */
    int                      rumble_active;
    int                      rumble_sent;
    uint64_t                 rumble_deadline_ms;
    int                      rumble_warned;

    /* ---- the runtime ---- */
    nj_port        port;
    nj_vm         *vm;
    nj_app        *app;
    nj_ref         midlet;
    lr_thread      thread;
    int            thread_started;
    int            phase;
    int            stop;
    int            vm_done;
    int            vm_failed;
    int            shutdown_sent;

    /* ---- video ---- */
    unsigned char *back;   /* runtime paints here                 */
    unsigned char *front;  /* complete rotated frame, frontend's  */
    unsigned char *send;   /* snapshot handed to the frontend     */
    int            fb_w, fb_h, fb_stride, bpp;
    int            out_w, out_h;
    int            fb_format; /* NJ_PIXFMT_* */
    int            lr_pixfmt; /* RETRO_PIXEL_FORMAT_* */

#if NJ_LIBRETRO_HW
    lr_hw          hw;
#endif

    /* ---- clocks / synchronization ---- */
    lr_mutex          lock;
    lr_cond           cond;
    unsigned long long vclock_frames; /* retro_run() calls so far        */
    unsigned long long load_ms;       /* virtual ms while loading        */
    unsigned long long posted;        /* frames produced by the VM       */
    unsigned long long consumed;      /* frames handed to the frontend   */

    /* ---- input ---- */
    nj_event events[LR_EVENT_QUEUE];
    size_t   event_rd, event_wr;
    unsigned char key_down[NJ_KEY_COUNT];
    int      ptr_down;
    int      ptr_x, ptr_y;

    /* ---- audio ---- */
    int16_t *pcm_ring;   /* resampled 44.1 kHz stereo frames */
    size_t   pcm_cap, pcm_rd, pcm_wr;
    int16_t *tone_ring;
    size_t   tone_cap, tone_rd, tone_wr;
    int      src_rate, src_ch;
    double   src_pos;
    long long src_base;
    int      have_prev;
    int16_t  prev_l, prev_r;
    int16_t  mix[LR_FRAMES_PER_RUN * 2];

    /* ---- save states ---- */
    int            req_kind;
    int            req_done;
    int            req_busy;
    int            req_rc;
    nj_u1         *req_blob;      /* request/result owned by the queue */
    nj_size        req_size;
    nj_u4          jar_crc;
    nj_size        state_bound;   /* retro_serialize_size() answer          */

    /* ---- paths ---- */
    char save_dir[1024];
    char base_dir[2048];
    char fs_dir[2056];
} nj_lr;

/*
 * The libretro ABI passes no userdata to a core, so there is exactly one
 * instance.  Keeping it static (zero-initialised) also means the core is
 * usable before retro_init(), which some frontends rely on.
 */
static nj_lr g_core;

static nj_lr *core(void) { return &g_core; }

static void lr_log(nj_lr *s, enum retro_log_level level, const char *fmt, ...);

#if NJ_LIBRETRO_HW
/* Closing the gate cancels only unclaimed work. EXECUTING must finish before
 * the caller may release borrowed CPU memory; COMPLETE is an immutable result.
 * Caller holds s->lock. No GL or frontend callbacks under that lock. */
static void lr_hw_close_gate(nj_lr *s)
{
    s->hw.gate = 0;
    if (s->hw.slot == LR_HW_PENDING) {
        s->hw.rc = NJ_GFX_ACCEL_DECLINE;
        s->hw.slot = LR_HW_COMPLETE;
        s->hw.cancelled++;
    }
    lr_cond_broadcast(&s->cond);
}

static int lr_hw_submit(nj_lr *s, const lr_hw_request *request)
{
    lr_hw *h = &s->hw;
    uint64_t deadline;
    int rc = NJ_GFX_ACCEL_DECLINE;
    lr_mutex_lock(&s->lock);
    if (s->phase != LR_PHASE_RUN || s->stop || s->vm_done ||
        s->req_kind != LR_REQ_NONE || !h->worker_known ||
        !lr_thread_equal(h->worker, lr_thread_self()) ||
        !h->ready || !h->gate || h->slot != LR_HW_EMPTY) {
        h->declines++;
        lr_mutex_unlock(&s->lock);
        return rc;
    }
    h->request = *request;
    h->slot = LR_HW_PENDING;
    h->queued++;
    deadline = lr_real_ms() + LR_HW_QUEUE_MS;
    lr_cond_broadcast(&s->cond);
    while (h->slot != LR_HW_COMPLETE) {
        if (h->slot == LR_HW_PENDING &&
            (!h->gate || !h->ready || s->stop ||
             s->req_kind != LR_REQ_NONE || lr_real_ms() >= deadline)) {
            lr_hw_close_gate(s);
            break;
        }
        /* Never timeout EXECUTING: the frontend may already have committed
         * CPU color/depth. Returning DECLINE here would double-render/UAF. */
        (void)lr_cond_wait_ms(&s->cond, &s->lock, 10u);
    }
    rc = h->rc;
    memset(&h->request, 0, sizeof h->request);
    h->slot = LR_HW_EMPTY;
    if (rc == NJ_GFX_ACCEL_DONE) {
        if (request->kind == LR_HW_FILL) h->fills++;
        else if (request->kind == LR_HW_BLIT) h->blits++;
        else if (request->kind == LR_HW_RASTER) h->rasters++;
        else if (request->kind == LR_HW_TRIANGLES) h->triangles++;
        else if (request->kind == LR_HW_TRIANGLES_LIST) h->triangle_lists++;
    } else h->declines++;
    lr_cond_broadcast(&s->cond);
    lr_mutex_unlock(&s->lock);
    return rc;
}

static int lr_hw_fill(void *ud, const nj_gfx *g, int x, int y, int w, int h)
{
    lr_hw_request r;
    memset(&r, 0, sizeof r);
    r.kind = LR_HW_FILL; r.gfx = g; r.x = x; r.y = y; r.w = w; r.h = h;
    return lr_hw_submit((nj_lr *)ud, &r);
}

static int lr_hw_blit(void *ud, const nj_gfx *g, const nj_gfx_blit *blit)
{
    lr_hw_request r;
    memset(&r, 0, sizeof r);
    r.kind = LR_HW_BLIT; r.gfx = g; r.blit = blit;
    return lr_hw_submit((nj_lr *)ud, &r);
}

static int lr_hw_raster(void *ud, const nj_gfx_raster *packet, nj_u4 *drawn)
{
    lr_hw_request r;
    memset(&r, 0, sizeof r);
    r.kind = LR_HW_RASTER; r.raster = packet; r.result = drawn;
    return lr_hw_submit((nj_lr *)ud, &r);
}

static int lr_hw_triangles(void *ud, const nj_gfx_triangles *packet, nj_u4 *submitted)
{
    lr_hw_request r;
    memset(&r, 0, sizeof r);
    r.kind = LR_HW_TRIANGLES; r.triangles = packet; r.result = submitted;
    return lr_hw_submit((nj_lr *)ud, &r);
}


static const nj_gfx_accel lr_hw_accel = {
    NJ_GFX_ACCEL_VERSION, lr_hw_fill, lr_hw_blit
};

/* GL section identity is backed by libretro's current-context guarantee. The
 * ABI exposes no native context handle and offers no portable make-current. */
static int lr_hw_is_current(void *ud)
{
    nj_lr *s = (nj_lr *)ud;
    return s->hw.gl_section && lr_thread_equal(s->hw.gl_thread, lr_thread_self());
}

static nj_libretro_gl_proc lr_hw_get_proc(void *ud, const char *name)
{
    nj_lr *s = (nj_lr *)ud;
    return s->hw.callback.get_proc_address ? s->hw.callback.get_proc_address(name) : NULL;
}

static void lr_hw_section(nj_lr *s, int active)
{
    s->hw.gl_thread = lr_thread_self();
    s->hw.gl_section = active;
}

static void RETRO_CALLCONV lr_hw_context_destroy(void)
{
    nj_lr *s = core();
    lr_hw *h = &s->hw;
    if (!s->sync_ready) return;
    lr_mutex_lock(&h->gl_lock);
    lr_mutex_lock(&s->lock);
    h->ready = 0;
    h->generation++;
    lr_hw_close_gate(s);
    lr_mutex_unlock(&s->lock);
    lr_hw_section(s, 1);
    if (h->context) h->driver->destroy(h->context, 0);
    h->context = NULL;
    lr_hw_section(s, 0);
    lr_mutex_unlock(&h->gl_lock);
    lr_log(s, RETRO_LOG_INFO, "hardware context destroyed; CPU drawing continues");
}

static void RETRO_CALLCONV lr_hw_context_reset(void)
{
    nj_lr *s = core();
    lr_hw *h = &s->hw;
    nj_libretro_hw_config config;
    const char *info = NULL;
    int ready;
    if (!s->sync_ready) return;
    lr_mutex_lock(&h->gl_lock);
    lr_mutex_lock(&s->lock);
    h->ready = 0;
    h->generation++;
    lr_hw_close_gate(s);
    lr_mutex_unlock(&s->lock);
    /* reset without destroy means the OLD context is already dead. */
    if (h->context) h->driver->destroy(h->context, 1);
    h->context = NULL;
    lr_hw_section(s, 1);
    memset(&config, 0, sizeof config);
    config.get_proc = lr_hw_get_proc;
    config.is_current = lr_hw_is_current;
    config.ud = s;
    config.context_type = (int)h->callback.context_type;
    config.major = h->callback.version_major;
    config.minor = h->callback.version_minor;
    if (h->accepted && h->driver && h->callback.get_proc_address &&
        h->callback.get_current_framebuffer) {
        h->context = h->driver->create(&config);
    }
    ready = h->context && h->driver->valid(h->context);
    if (ready && h->driver->info) info = h->driver->info(h->context);
    lr_log(s, ready ? RETRO_LOG_INFO : RETRO_LOG_WARN,
           "hardware context %u: %s", h->generation,
           info ? info : (ready ? "enabled" : "unavailable; CPU drawing, waiting for reset"));
    lr_mutex_lock(&s->lock);
    h->ready = ready;
    lr_mutex_unlock(&s->lock);
    lr_hw_section(s, 0);
    lr_mutex_unlock(&h->gl_lock);
}

static int lr_hw_supported(enum retro_hw_context_type type)
{
    return type == RETRO_HW_CONTEXT_OPENGLES2 || type == RETRO_HW_CONTEXT_OPENGL ||
           type == RETRO_HW_CONTEXT_OPENGLES3 || type == RETRO_HW_CONTEXT_OPENGLES_VERSION;
}

static void lr_hw_negotiate(nj_lr *s)
{
    lr_hw *h = &s->hw;
    enum retro_hw_context_type preferred = RETRO_HW_CONTEXT_NONE;
    enum retro_hw_context_type candidates[3];
    bool choice;
    unsigned i, count = 0;
    if (!h->requested || !s->environ_cb) return;
    h->driver = nj_libretro_hw_gl_driver();
    if (!h->driver || !h->driver->create || !h->driver->destroy ||
        !h->driver->begin || !h->driver->end || !h->driver->valid ||
        !h->driver->present) return;
    choice = s->environ_cb(RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER, &preferred);
    /* false with a concrete type means the frontend supports ONLY that API. */
    if (!choice && preferred != RETRO_HW_CONTEXT_NONE && !lr_hw_supported(preferred)) {
        lr_log(s, RETRO_LOG_WARN, "frontend requires unsupported hardware API %d; software", (int)preferred);
        return;
    }
    if (lr_hw_supported(preferred)) candidates[count++] = preferred;
    if (choice || preferred == RETRO_HW_CONTEXT_NONE) {
        if (preferred != RETRO_HW_CONTEXT_OPENGLES2) candidates[count++] = RETRO_HW_CONTEXT_OPENGLES2;
        if (preferred != RETRO_HW_CONTEXT_OPENGL) candidates[count++] = RETRO_HW_CONTEXT_OPENGL;
    }
    for (i = 0; i < count; i++) {
        memset(&h->callback, 0, sizeof h->callback);
        h->callback.context_type = candidates[i];
        h->callback.context_reset = lr_hw_context_reset;
        h->callback.context_destroy = lr_hw_context_destroy;
        h->callback.bottom_left_origin = true;
        if (candidates[i] == RETRO_HW_CONTEXT_OPENGLES_VERSION) {
            h->callback.version_major = 3;
            h->callback.version_minor = 0;
        }
        /* Some frontends synchronously reset during negotiation. */
        h->accepted = 1;
        if (s->environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &h->callback)) {
            lr_log(s, RETRO_LOG_INFO, "hardware API %d accepted (software until context reset)", (int)candidates[i]);
            return;
        }
        h->accepted = 0;
    }
    lr_log(s, RETRO_LOG_WARN, "hardware render rejected; using software video and drawing");
}

/* gl_lock remains held through pump, CPU snapshot and presentation. */
static int lr_hw_begin(nj_lr *s)
{
    lr_hw *h = &s->hw;
    int ready;
    if (!h->accepted) return 0;
    lr_mutex_lock(&h->gl_lock);
    lr_hw_section(s, 1);
    lr_mutex_lock(&s->lock);
    ready = h->ready;
    lr_mutex_unlock(&s->lock);
    if (ready && h->context && h->driver->valid(h->context) &&
        h->driver->begin(h->context)) return 1;
    lr_mutex_lock(&s->lock);
    h->ready = 0;
    lr_hw_close_gate(s);
    lr_mutex_unlock(&s->lock);
    lr_hw_section(s, 0);
    lr_mutex_unlock(&h->gl_lock);
    return 0;
}

static void lr_hw_pump(nj_lr *s)
{
    lr_hw *h = &s->hw;
    uint64_t deadline = lr_real_ms() + LR_HW_BUDGET_MS;
    unsigned count = 0;
    lr_mutex_lock(&s->lock);
    while (h->ready && !s->stop && !s->vm_done && s->req_kind == LR_REQ_NONE &&
           count < LR_HW_MAX_OPS && lr_real_ms() < deadline) {
        if (h->slot == LR_HW_PENDING) {
            lr_hw_request r = h->request;
            int rc = NJ_GFX_ACCEL_DECLINE, valid;
            h->slot = LR_HW_EXECUTING;
            lr_mutex_unlock(&s->lock);
            if (r.kind == LR_HW_FILL && h->driver->accel.fill_rect)
                rc = h->driver->accel.fill_rect(h->context, r.gfx, r.x, r.y, r.w, r.h);
            else if (r.kind == LR_HW_BLIT && h->driver->accel.blit)
                rc = h->driver->accel.blit(h->context, r.gfx, r.blit);
            else if (r.kind == LR_HW_RASTER && h->driver->raster)
                rc = h->driver->raster(h->context, r.raster, r.result);
            else if (r.kind == LR_HW_TRIANGLES && h->driver->triangles)
                rc = h->driver->triangles(h->context, r.triangles, r.result);
            else if (r.kind == LR_HW_TRIANGLES_LIST && h->driver->triangles_list)
                rc = h->driver->triangles_list(h->context, r.triangles_list,
                                               r.result);
            valid = h->driver->valid(h->context);
            lr_mutex_lock(&s->lock);
            /* Preserve DONE even when validity changed after CPU commit. */
            h->rc = rc;
            h->slot = LR_HW_COMPLETE;
            h->executed++;
            if (!valid) { h->ready = 0; h->gate = 0; }
            lr_cond_broadcast(&s->cond);
            count++;
        } else {
            if (h->parked && h->park_until > s->vclock_frames * 1000u / 60u) break;
            (void)lr_cond_wait_ms(&s->cond, &s->lock, 1u);
        }
    }
    lr_hw_close_gate(s);
    lr_mutex_unlock(&s->lock);
}

static int lr_hw_end(nj_lr *s)
{
    lr_hw *h = &s->hw;
    nj_surface surface;
    int presented = 0, restored, ready;
    lr_mutex_lock(&s->lock);
    ready = h->ready;
    lr_mutex_unlock(&s->lock);
    memset(&surface, 0, sizeof surface);
    surface.pixels = s->send;
    surface.width = s->out_w; surface.height = s->out_h;
    surface.stride = s->out_w * s->bpp;
    surface.format = s->fb_format;
    if (ready && h->driver->valid(h->context))
        presented = h->driver->present(h->context,
                    h->callback.get_current_framebuffer(), &surface);
    restored = h->driver->end(h->context);
    if (!restored || !h->driver->valid(h->context)) {
        lr_mutex_lock(&s->lock);
        h->ready = 0;
        lr_hw_close_gate(s);
        lr_mutex_unlock(&s->lock);
        presented = 0;
    }
    if (presented) h->presents++;
    lr_hw_section(s, 0);
    lr_mutex_unlock(&h->gl_lock);
    return presented;
}
#endif

/* The frontend calls this table's callback from its input thread. */
static void RETRO_CALLCONV lr_keyboard_event(bool down, unsigned keycode,
                                             uint32_t character, uint16_t mod);
static struct retro_keyboard_callback lr_keyboard_callback_instance;

/* Forward declarations for the port callbacks. */
static nj_u4 lr_port_millis(void *ud);
static int   lr_display_init(void *ud, int *w, int *h, int *stride, void **px);
static void  lr_display_present(void *ud, int x, int y, int w, int h);
static int   lr_input_poll(void *ud, nj_event *ev);
static int   lr_input_push(void *ud, const nj_event *ev);
static void  lr_port_log(void *ud, int level, const char *msg);
static void  lr_sleep_ms(void *ud, nj_u4 ms);
static void  lr_idle(void *ud);
static int   lr_tone_play(void *ud, const nj_audio_tone *tones, int count);
static void  lr_tone_stop(void *ud);
static int   lr_pcm_open(void *ud, int rate, int channels);
static int   lr_pcm_write(void *ud, const nj_s2 *samples, int frames);
static void  lr_pcm_close(void *ud);
static int   lr_pcm_free(void *ud);
static void  lr_port_vibrate(void *ud, int ms);

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

static void lr_log(nj_lr *s, enum retro_log_level level, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    if (s && s->log_cb) {
        s->log_cb(level, "[nano_j2me] %s\n", buf);
    } else {
        /* Before the frontend has handed over a logger, stderr is the only
         * place a load failure can be seen at all. */
        fprintf(stderr, "[nano_j2me] %s\n", buf);
    }
}

static const char *lr_basename(const char *path)
{
    const char *slash = path ? strrchr(path, '/') : NULL;
#if defined(_WIN32)
    const char *bslash = path ? strrchr(path, '\\') : NULL;
    if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
    return slash ? slash + 1 : (path ? path : "");
}

static void lr_stem_from_path(nj_lr *s, const char *path)
{
    const char *base = lr_basename(path);
    size_t n = 0;
    size_t i;

    for (i = 0; base[i] && base[i] != '.'; i++) {
        unsigned char c = (unsigned char)base[i];
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.') {
            if (n + 1 < sizeof s->stem) s->stem[n++] = (char)c;
        } else if (n > 0 && n + 1 < sizeof s->stem && s->stem[n - 1] != '_') {
            s->stem[n++] = '_';
        }
    }
    while (n > 0 && s->stem[n - 1] == '_') n--;
    s->stem[n] = '\0';
    if (n == 0) {
        memcpy(s->stem, "game", 5);
    }
}

static void lr_mkdir_p(const char *path)
{
    char tmp[1024];
    size_t i;
    size_t len;

    if (!path || !path[0]) return;
    len = strlen(path);
    if (len >= sizeof tmp) len = sizeof tmp - 1;
    memcpy(tmp, path, len);
    tmp[len] = '\0';

    for (i = 1; i < len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char saved = tmp[i];
            tmp[i] = '\0';
            lr_mkdir(tmp);
            tmp[i] = saved;
        }
    }
    lr_mkdir(tmp);
}

static int lr_read_file(const char *path, unsigned char **out, size_t *out_size)
{
    FILE *f;
    long size;
    unsigned char *buf;

    *out = NULL;
    *out_size = 0;
    if (!path || !path[0]) return 0;
    f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    size = ftell(f);
    if (size <= 0 || size > (long)(64 * 1024 * 1024)) { fclose(f); return 0; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    buf = (unsigned char *)malloc((size_t)size);
    if (!buf) { fclose(f); return 0; }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return 0;
    }
    fclose(f);
    *out = buf;
    *out_size = (size_t)size;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Core options                                                       */
/*                                                                    */
/* One metadata table feeds both the modern core-options V2 API and    */
/* the deprecated SET_VARIABLES string API, so a frontend old enough   */
/* not to know V2 still gets the same controls.                        */
/* ------------------------------------------------------------------ */

static const lr_opt_value lr_device_values[] = {
    { "generic",            "Generic J2ME handset" },
    { "nokia6230",          "Nokia 6230"           },
    { "sonyericsson-k750",  "Sony Ericsson K750"   },
    { "motorola-e398",      "Motorola E398"        },
    { "samsung-sgh-e700",   "Samsung SGH-E700"     },
    { "siemens-s65",        "Siemens S65"          },
    { NULL, NULL }
};

static const lr_opt_value lr_rotation_values[] = {
    { "0",   "0 (portrait)"  },
    { "90",  "90 degrees"    },
    { "180", "180 degrees"   },
    { "270", "270 degrees"   },
    { NULL, NULL }
};

static const lr_opt_value lr_heap_values[] = {
    { "auto", "Auto (by RAM)" },
    { "16",   "16 MB"  },
    { "32",   "32 MB"  },
    { "64",   "64 MB"  },
    { "128",  "128 MB" },
    { NULL, NULL }
};

/* ------------------------------------------------------------------ */
/* Automatic heap                                                      */
/* ------------------------------------------------------------------ */
/*
 * The heap is one arena the runtime allocates and clears when the MIDlet
 * starts, so "automatic" means a fraction of the host's RAM rather than one of
 * the fixed sizes: one sixteenth, floored at 32 MB (below that a 3D title dies
 * in its own loader) and capped at 128 MB (above that a single MIDlet only
 * holds memory the rest of the machine could use), on the same 8 MB grid the
 * fixed values use.  The JSR 184 and video titles in the corpus are the reason
 * the floor is not lower.  Hosts that cannot report RAM get the floor.
 */
static long lr_host_ram_mb(void)
{
#if defined(_WIN32)
    MEMORYSTATUSEX st;
    memset(&st, 0, sizeof(st));
    st.dwLength = sizeof(st);
    if (GlobalMemoryStatusEx(&st) && st.ullTotalPhys > 0u) {
        return (long)(st.ullTotalPhys / (1024ull * 1024ull));
    }
    return 0;
#else
    long pages = sysconf(_SC_PHYS_PAGES);
    long page  = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page > 0) {
        return (long)(((unsigned long long)pages *
                       (unsigned long long)page) / (1024ull * 1024ull));
    }
    return 0;
#endif
}

static int lr_auto_heap_mb(void)
{
    long total = lr_host_ram_mb();
    long heap = total > 0 ? total / 16 : 0;
    if (heap < 32) {
        heap = 32;
    }
    if (heap > 128) {
        heap = 128;
    }
    return (int)(heap & ~7L);
}

static const lr_opt_value lr_switch_values[] = {
    { "enabled",  "Enabled"  },
    { "disabled", "Disabled" },
    { NULL, NULL }
};

static const lr_opt_value lr_font_values[] = {
    { "auto", "Auto (system directory, then OS fonts)" },
    { "system", "Frontend system directory only" },
    { "builtin", "Built-in bitmap fonts" },
    { NULL, NULL }
};

/* "auto" deliberately means the runtime default (en-US) rather than the host
 * locale: a MIDlet's resources and text layout were authored for one locale,
 * and the host's LANG is not a property of the handset being emulated. */
static const lr_opt_value lr_locale_values[] = {
    { "auto",  "Auto (en-US)"            },
    { "en-US", "English (United States)" },
    { "zh-CN", "Chinese (Simplified)"    },
    { "zh-TW", "Chinese (Traditional)"   },
    { "ja-JP", "Japanese"                },
    { "ko-KR", "Korean"                  },
    { "de-DE", "German"                  },
    { "fr-FR", "French"                  },
    { "es-ES", "Spanish"                 },
    { "ru-RU", "Russian"                 },
    { NULL, NULL }
};

/* The canvas the MIDlet sees.  The runtime derives the Canvas dimensions from
 * the port width/height at MIDlet construction, so this needs a content
 * reload; the frame buffer and reported AV geometry follow the same values. */
static const lr_opt_value lr_screen_values[] = {
    { "240x320", "240x320 (default)" },
    { "176x208", "176x208 (Nokia S60)" },
    { "128x160", "128x160 (small handset)" },
    { "320x240", "320x240 (landscape QVGA)" },
    { NULL, NULL }
};

/* Button binding values: every canonical nj_key name the runtime understands,
 * minus NONE (a binding may still be disabled by picking "NONE"). */
static const lr_opt_value lr_key_values[] = {
    { "NONE",       "Disabled"    },
    { "FIRE",       "Fire / OK"   },
    { "UP",         "D-Pad Up"    },
    { "DOWN",       "D-Pad Down"  },
    { "LEFT",       "D-Pad Left"  },
    { "RIGHT",      "D-Pad Right" },
    { "0",          "Key 0"       },
    { "1",          "Key 1"       },
    { "2",          "Key 2"       },
    { "3",          "Key 3"       },
    { "4",          "Key 4"       },
    { "5",          "Key 5"       },
    { "6",          "Key 6"       },
    { "7",          "Key 7"       },
    { "8",          "Key 8"       },
    { "9",          "Key 9"       },
    { "STAR",       "Star"        },
    { "POUND",      "Pound"       },
    { "SOFT_LEFT",  "Left soft key"  },
    { "SOFT_RIGHT", "Right soft key" },
    { "CLEAR",      "Clear"       },
    { "GAME_A",     "Game A"      },
    { "GAME_B",     "Game B"      },
    { "GAME_C",     "Game C"      },
    { "GAME_D",     "Game D"      },
    { "VOLUP",      "Volume up"   },
    { "VOLDOWN",    "Volume down" },
    { "POWER",      "Power"       },
    { NULL, NULL }
};

static const char *const lr_button_key[] = {
    "nano_j2me_btn_a",
    "nano_j2me_btn_b",
    "nano_j2me_btn_x",
    "nano_j2me_btn_y",
    "nano_j2me_btn_l",
    "nano_j2me_btn_r",
    "nano_j2me_btn_select",
    "nano_j2me_btn_start"
};

static const char *const lr_button_default[] = {
    "FIRE", "SOFT_RIGHT", "SOFT_LEFT", "CLEAR",
    "GAME_A", "GAME_B", "0", "5"
};

static const unsigned lr_button_id[] = {
    RETRO_DEVICE_ID_JOYPAD_A,
    RETRO_DEVICE_ID_JOYPAD_B,
    RETRO_DEVICE_ID_JOYPAD_X,
    RETRO_DEVICE_ID_JOYPAD_Y,
    RETRO_DEVICE_ID_JOYPAD_L,
    RETRO_DEVICE_ID_JOYPAD_R,
    RETRO_DEVICE_ID_JOYPAD_SELECT,
    RETRO_DEVICE_ID_JOYPAD_START
};

static const struct retro_input_descriptor lr_input_descriptors[] = {
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "D-Pad Up"     },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "D-Pad Down"   },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "D-Pad Left"   },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "D-Pad Right"  },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "Button A (Fire)"    },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "Button B (Soft Right)" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      "Button X (Soft Left)"  },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,      "Button Y (Clear)"   },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,      "Button L (Game A)"  },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,      "Button R (Game B)"  },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Button Select"      },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "Button Start"       },
    { 0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X,    "Pointer X"     },
    { 0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y,    "Pointer Y"     },
    { 0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED, "Pointer press" },
    { 0, 0, 0, 0, NULL }
};

typedef struct lr_option_meta {
    const char         *key;
    const char         *desc;
    const char         *info;
    const char         *category;
    const char         *def;
    const lr_opt_value *values;
} lr_option_meta;

#if NJ_LIBRETRO_HW
static const lr_opt_value lr_renderer_values[] = {
    { "software", "Software" }, { "hardware", "OpenGL hardware" }, { NULL, NULL }
};
#endif

static const lr_option_meta lr_options[] = {
#if NJ_LIBRETRO_HW
    { "nano_j2me_renderer", "Renderer (restart required)",
      "Optional frontend OpenGL drawing and presentation. Reload content after changing it.",
      "system", "software", lr_renderer_values },
#endif
    { "nano_j2me_device", "Device profile",
      "Which handset's properties Game MIDlets see. Restart the content after changing it.",
      "system", "generic", lr_device_values },
    { "nano_j2me_rotation", "Screen rotation",
      "Rotates the picture only; the MIDlet keeps painting its canvas at the "
      "configured screen size.",
      "system", "0", lr_rotation_values },
    { "nano_j2me_screen", "Screen size (restart required)",
      "Canvas size the MIDlet sees. 240x320 is the built-in J2ME screen; the "
      "other sizes emulate smaller or landscape handsets. Reload content after "
      "changing it.",
      "system", "240x320", lr_screen_values },
    { "nano_j2me_locale", "Locale (restart required)",
      "Value of the microedition.locale system property. Auto keeps the "
      "runtime default en-US; the host's locale is not used. Reload content "
      "after changing it.",
      "system", "auto", lr_locale_values },
    { "nano_j2me_heap", "Java heap",
      "Size of the Java heap a MIDlet gets. Auto is a sixteenth of the host's "
      "RAM, 32-128 MB. Needs a content reload.",
      "system", "auto", lr_heap_values },
    { "nano_j2me_font", "Font (restart required)",
      "Auto tries nano_j2me/font.ttf or font.ttc in the frontend system directory, "
      "then common OS fonts. Unsupported/missing fonts use built-in bitmaps.",
      "system", "auto", lr_font_values },
    { "nano_j2me_sound", "Audio output",
      "Sample and tone audio from the MIDlet.", "audio", "enabled",
      lr_switch_values },
    { "nano_j2me_pointer", "Touchscreen pointer",
      "Map the frontend's pointer to the MIDlet's canvas.", "input", "enabled",
      lr_switch_values },
    { "nano_j2me_btn_a", "Button A", "J2ME key for the A button.", "input", "FIRE", lr_key_values },
    { "nano_j2me_btn_b", "Button B", "J2ME key for the B button.", "input", "SOFT_RIGHT", lr_key_values },
    { "nano_j2me_btn_x", "Button X", "J2ME key for the X button.", "input", "SOFT_LEFT", lr_key_values },
    { "nano_j2me_btn_y", "Button Y", "J2ME key for the Y button.", "input", "CLEAR", lr_key_values },
    { "nano_j2me_btn_l", "Button L", "J2ME key for the L button.", "input", "GAME_A", lr_key_values },
    { "nano_j2me_btn_r", "Button R", "J2ME key for the R button.", "input", "GAME_B", lr_key_values },
    { "nano_j2me_btn_select", "Button Select", "J2ME key for the Select button.", "input", "0", lr_key_values },
    { "nano_j2me_btn_start", "Button Start", "J2ME key for the Start button.", "input", "5", lr_key_values }
};

#define LR_OPTION_COUNT (sizeof lr_options / sizeof lr_options[0])

static struct retro_core_option_v2_definition lr_v2_definitions[LR_OPTION_COUNT + 1];
static struct retro_core_options_v2              lr_v2_options;
static char                                      lr_legacy_buf[LR_OPTION_COUNT][2048];
static const char                               *lr_legacy_variables[LR_OPTION_COUNT + 1];
static int                                       lr_options_ready;

static const struct retro_core_option_v2_category lr_v2_categories[] = {
    { "system", "System", "Device and presentation settings" },
    { "input",  "Input",  "Keypad and touchscreen mapping"    },
    { "audio",  "Audio",  "Sound output"                      },
    { NULL, NULL, NULL }
};

static void lr_build_option_definitions(void)
{
    size_t i;
    size_t j;

    if (lr_options_ready) return;

    for (i = 0; i < LR_OPTION_COUNT; i++) {
        struct retro_core_option_v2_definition *d = &lr_v2_definitions[i];
        const lr_option_meta *m = &lr_options[i];

        d->key             = m->key;
        d->desc            = m->desc;
        d->desc_categorized = m->desc;
        d->info            = m->info;
        d->info_categorized = m->info;
        d->category_key    = m->category;
        d->default_value   = m->def;
        for (j = 0; m->values[j].value && j < LR_MAX_VALUES; j++) {
            d->values[j].value = m->values[j].value;
            d->values[j].label = m->values[j].label;
        }
        for (; j < RETRO_NUM_CORE_OPTION_VALUES_MAX; j++) {
            d->values[j].value = NULL;
            d->values[j].label = NULL;
        }

        /* Deprecated string form: "key; Label; value1|value2|..." */
        {
            size_t n = 0;
            int wrote = snprintf(lr_legacy_buf[i], sizeof lr_legacy_buf[i],
                                 "%s; %s;", m->key, m->desc);
            if (wrote > 0) n = (size_t)wrote;
            for (j = 0; m->values[j].value && j < LR_MAX_VALUES; j++) {
                int w = snprintf(lr_legacy_buf[i] + n,
                                 sizeof lr_legacy_buf[i] - n, "%s%s",
                                 j ? "|" : "", m->values[j].value);
                if (w < 0 || (size_t)w >= sizeof lr_legacy_buf[i] - n) break;
                n += (size_t)w;
            }
            lr_legacy_variables[i] = lr_legacy_buf[i];
        }
    }
    lr_v2_definitions[LR_OPTION_COUNT].key = NULL;
    lr_legacy_variables[LR_OPTION_COUNT] = NULL;

    lr_v2_options.categories  =
        (struct retro_core_option_v2_category *)lr_v2_categories;
    lr_v2_options.definitions =
        (struct retro_core_option_v2_definition *)lr_v2_definitions;
    lr_options_ready = 1;
}

static void lr_register_environment(nj_lr *s)
{
    unsigned version = 0;
    bool     no_game = false;
    unsigned level = 2;
    uint64_t quirks = RETRO_SERIALIZATION_QUIRK_INCOMPLETE |
                      RETRO_SERIALIZATION_QUIRK_ENDIAN_DEPENDENT |
                      RETRO_SERIALIZATION_QUIRK_PLATFORM_DEPENDENT;

    if (!s->environ_cb) return;
    (void)s->environ_cb(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS, &quirks);

    lr_build_option_definitions();

    (void)s->environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
    (void)s->environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
    (void)s->environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS,
                        (void *)lr_input_descriptors);
    (void)s->environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK,
                        (void *)&lr_keyboard_callback_instance);

    /* Bitmask polling is an optimisation, not a requirement: when the
     * frontend declines, lr_read_joypad() keeps polling each button. */
    s->input_bitmask =
        s->environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL) ? 1 : 0;

    /* A frontend without rumble is normal (headless, some handheld builds).
     * There is deliberately no error or warning in that case. */
    {
        struct retro_rumble_interface rumble;
        memset(&rumble, 0, sizeof rumble);
        if (s->environ_cb(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble) &&
            rumble.set_rumble_state) {
            s->rumble_cb = rumble.set_rumble_state;
        } else {
            s->rumble_cb = NULL;
        }
    }

    if (s->environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version)) {
        if (version >= 2 && s->environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2,
                                          &lr_v2_options)) {
            return;
        }
    }
    (void)s->environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES,
                        (void *)lr_legacy_variables);
}

static const char *lr_option(nj_lr *s, const char *key, const char *fallback)
{
    struct retro_variable var;

    if (!s->environ_cb) return fallback;
    var.key = key;
    var.value = NULL;
    if (s->environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
        return var.value;
    }
    return fallback;
}

static int lr_key_from_name(const char *name)
{
    static const struct { const char *name; int key; } table[] = {
        { "FIRE", NJ_KEY_FIRE }, { "UP", NJ_KEY_UP }, { "DOWN", NJ_KEY_DOWN },
        { "LEFT", NJ_KEY_LEFT }, { "RIGHT", NJ_KEY_RIGHT },
        { "0", NJ_KEY_0 }, { "1", NJ_KEY_1 }, { "2", NJ_KEY_2 },
        { "3", NJ_KEY_3 }, { "4", NJ_KEY_4 }, { "5", NJ_KEY_5 },
        { "6", NJ_KEY_6 }, { "7", NJ_KEY_7 }, { "8", NJ_KEY_8 },
        { "9", NJ_KEY_9 }, { "STAR", NJ_KEY_STAR }, { "POUND", NJ_KEY_POUND },
        { "SOFT_LEFT", NJ_KEY_SOFT_LEFT }, { "SOFT_RIGHT", NJ_KEY_SOFT_RIGHT },
        { "CLEAR", NJ_KEY_CLEAR },
        { "GAME_A", NJ_KEY_GAME_A }, { "GAME_B", NJ_KEY_GAME_B },
        { "GAME_C", NJ_KEY_GAME_C }, { "GAME_D", NJ_KEY_GAME_D },
        { "VOLUP", NJ_KEY_VOLUP }, { "VOLDOWN", NJ_KEY_VOLDOWN },
        { "POWER", NJ_KEY_POWER },
        { NULL, NJ_KEY_NONE }
    };
    int i;

    if (!name) return NJ_KEY_NONE;
    for (i = 0; table[i].name; i++) {
        if (strcmp(table[i].name, name) == 0) return table[i].key;
    }
    return NJ_KEY_NONE;
}

static const char *lr_locale_value(const char *v)
{
    size_t i;

    if (!v || !v[0]) return NULL;
    for (i = 0; lr_locale_values[i].value; i++) {
        if (strcmp(lr_locale_values[i].value, v) == 0) {
            return lr_locale_values[i].value;
        }
    }
    return NULL;
}

/* Build the property table handed to the VM: the selected handset profile
 * followed by this session's overrides, last match wins (see nj_device.h).
 * Without an override the VM borrows the profile's table directly. */
static void lr_prepare_properties(nj_lr *s)
{
    const nj_device *dev = s->device;
    nj_u4 count = dev ? dev->count : 0u;
    nj_u4 i;

    free(s->sys_props);
    s->sys_props = NULL;
    s->nsys_props = 0;

    if (strcmp(s->locale, "en-US") == 0) return;

    s->sys_props = (nj_device_prop *)calloc((size_t)count + 1u,
                                           sizeof *s->sys_props);
    if (!s->sys_props) {
        lr_log(s, RETRO_LOG_WARN,
               "locale: cannot allocate the property table; keeping en-US");
        return;
    }
    for (i = 0; i < count; i++) {
        s->sys_props[i] = dev->props[i];
    }
    s->sys_props[count].key = "microedition.locale";
    s->sys_props[count].value = s->locale;
    s->nsys_props = count + 1u;
}

/* Install the device profile, then the session overrides on top. Calling
 * nj_vm_set_device() first also selects the profile's soft-key convention;
 * nj_vm_set_system_properties() keeps that choice. */
static void lr_install_properties(nj_lr *s, nj_vm *vm)
{
    if (!s->device) s->device = nj_device_default();
    nj_vm_set_device(vm, s->device);
    if (s->sys_props && s->nsys_props > 0u) {
        nj_vm_set_system_properties(vm, s->sys_props, s->nsys_props);
    }
}

static void lr_read_config(nj_lr *s)
{
    const char *v;
    int i;

#if NJ_LIBRETRO_HW
    s->hw.requested = strcmp(lr_option(s, "nano_j2me_renderer", "software"), "hardware") == 0;
#endif
    v = lr_option(s, "nano_j2me_device", "generic");
    s->device = nj_device_find(v);
    if (!s->device) s->device = nj_device_default();

    v = lr_option(s, "nano_j2me_rotation", "0");
    s->rotation = atoi(v);
    if (s->rotation != 90 && s->rotation != 180 && s->rotation != 270) {
        s->rotation = 0;
    }

    v = lr_option(s, "nano_j2me_screen", "240x320");
    s->screen_w = s->screen_h = 0;
    if (v && v[0] && strcmp(v, "240x320") != 0) {
        int w = 0, h = 0;
        int known = 0;
        size_t k;
        for (k = 0; lr_screen_values[k].value; k++) {
            if (strcmp(lr_screen_values[k].value, v) == 0) known = 1;
        }
        if (known && sscanf(v, "%dx%d", &w, &h) == 2 &&
            w >= 128 && w <= 640 && h >= 128 && h <= 640) {
            s->screen_w = w;
            s->screen_h = h;
        } else {
            lr_log(s, RETRO_LOG_WARN,
                   "screen: unknown size '%s'; using 240x320", v);
        }
    }

    v = lr_option(s, "nano_j2me_locale", "auto");
    {
        const char *loc = lr_locale_value(v);
        if (strcmp(v ? v : "", "auto") == 0 || loc == NULL) {
            if (v && v[0] && strcmp(v, "auto") != 0) {
                lr_log(s, RETRO_LOG_WARN,
                       "locale: unknown value '%s'; using en-US", v);
            }
            snprintf(s->locale, sizeof s->locale, "en-US");
        } else {
            snprintf(s->locale, sizeof s->locale, "%s", loc);
        }
    }
    lr_prepare_properties(s);

    v = lr_option(s, "nano_j2me_heap", "auto");
    if (!v || !v[0] || strcmp(v, "auto") == 0) {
        s->heap_mb = lr_auto_heap_mb();
        lr_log(s, RETRO_LOG_INFO, "heap: automatic, %d MB for this host",
               s->heap_mb);
    } else {
        s->heap_mb = atoi(v);
        if (s->heap_mb < 8)   s->heap_mb = 8;
        if (s->heap_mb > 512) s->heap_mb = 512;
    }

    v = lr_option(s, "nano_j2me_font", "auto");
    s->font_mode = strcmp(v, "builtin") == 0 ? 2 :
                   (strcmp(v, "system") == 0 ? 1 : 0);
    s->sound = strcmp(lr_option(s, "nano_j2me_sound", "enabled"), "disabled") != 0;
    s->pointer = strcmp(lr_option(s, "nano_j2me_pointer", "enabled"),
                        "disabled") != 0;

    for (i = 0; i < 8; i++) {
        s->button_key[i] = lr_key_from_name(
            lr_option(s, lr_button_key[i], lr_button_default[i]));
    }
}

/* Try fonts before creating Java UI objects. The session pins the accepted
 * bytes so a file changed on disk cannot alter a restored VM's layout. */
static int lr_try_font(nj_lr *s, const char *path)
{
    unsigned char *data;
    size_t size;
    if (!lr_read_file(path, &data, &size)) return 0;
    if (!nj_vm_use_font_data(s->vm, data, (nj_u4)size)) {
        lr_log(s, RETRO_LOG_WARN, "font: unsupported or invalid TrueType font: %s", path);
        free(data);
        return 0;
    }
    s->font_data = data;
    s->font_size = size;
    s->font_crc = nj_crc32(data, (nj_u4)size, 0);
    lr_log(s, RETRO_LOG_INFO, "font: loaded %s (crc %08x)", path,
           (unsigned)s->font_crc);
    return 1;
}

static void lr_load_font(nj_lr *s)
{
    const char *dir = NULL;
    char path[LR_PATH_MAX];
    size_t i;
    static const char *const filenames[] = { "font.ttf", "font.ttc" };
#if defined(_WIN32)
    static const char *const os_fonts[] = {
        "Fonts/msyh.ttc", "Fonts/simsun.ttc", "Fonts/segoeui.ttf"
    };
    char windows_dir[MAX_PATH];
#elif defined(__ANDROID__)
    static const char *const os_fonts[] = {
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/DroidSansFallback.ttf", "/system/fonts/Roboto-Regular.ttf"
    };
#elif defined(__APPLE__)
    static const char *const os_fonts[] = {
        "/System/Library/Fonts/PingFang.ttc",
        "/Library/Fonts/Arial Unicode.ttf", "/System/Library/Fonts/Helvetica.ttc"
    };
#else
    static const char *const os_fonts[] = {
        "/usr/share/fonts/wenquanyi/wqy-zenhei/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    };
#endif
    if (s->font_mode == 2) {
        lr_log(s, RETRO_LOG_INFO, "font: using built-in OFL bitmap fonts");
        return;
    }
    if (s->environ_cb &&
        s->environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) &&
        dir && dir[0]) {
        for (i = 0; i < sizeof filenames / sizeof filenames[0]; i++) {
            int n = snprintf(path, sizeof path, "%s/nano_j2me/%s", dir, filenames[i]);
            if (n > 0 && (size_t)n < sizeof path && lr_try_font(s, path)) return;
        }
    }
    if (s->font_mode == 0) {
        for (i = 0; i < sizeof os_fonts / sizeof os_fonts[0]; i++) {
#if defined(_WIN32)
            UINT n = GetWindowsDirectoryA(windows_dir, sizeof windows_dir);
            int written;
            if (!n || n >= sizeof windows_dir) break;
            written = snprintf(path, sizeof path, "%s/%s", windows_dir, os_fonts[i]);
            if (written <= 0 || (size_t)written >= sizeof path) continue;
            if (lr_try_font(s, path)) return;
#else
            if (lr_try_font(s, os_fonts[i])) return;
#endif
        }
    }
    lr_log(s, RETRO_LOG_INFO, "font: no supported external font; using built-in OFL bitmap fonts");
}

/* ------------------------------------------------------------------ */
/* Video: double buffer, rotation, pointer mapping                    */
/* ------------------------------------------------------------------ */

static size_t lr_bytes_per_pixel(int fmt)
{
    switch (fmt) {
    case NJ_PIXFMT_RGB565:   return 2;
    case NJ_PIXFMT_RGB888:   return 3;
    case NJ_PIXFMT_ARGB8888: return 4;
    default:                 return 0;
    }
}

static int lr_video_init(nj_lr *s)
{
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
    size_t pixel_size;
    size_t output_size;

    if (s->environ_cb) {
        if (!s->environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
            fmt = RETRO_PIXEL_FORMAT_XRGB8888;
            if (!s->environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
                lr_log(s, RETRO_LOG_ERROR,
                       "the frontend supports neither RGB565 nor XRGB8888");
                return 0;
            }
        }
    }
    s->lr_pixfmt = (int)fmt;

    switch (fmt) {
    case RETRO_PIXEL_FORMAT_RGB565:
        s->fb_format = NJ_PIXFMT_RGB565;
        break;
    case RETRO_PIXEL_FORMAT_XRGB8888:
        s->fb_format = NJ_PIXFMT_ARGB8888;
        break;
    default:
        lr_log(s, RETRO_LOG_ERROR, "unsupported pixel format %d", (int)fmt);
        return 0;
    }
    s->bpp = (int)lr_bytes_per_pixel(s->fb_format);
    pixel_size = (size_t)s->bpp;

    /* The MIDlet paints a canvas of the configured size; rotation happens when
     * the finished frame is copied for the frontend, so the game's coordinate
     * system never changes under it. */
    s->fb_w = s->screen_w > 0 ? s->screen_w :
              (NJ_SCREEN_WIDTH  > 0 ? NJ_SCREEN_WIDTH  : 240);
    s->fb_h = s->screen_h > 0 ? s->screen_h :
              (NJ_SCREEN_HEIGHT > 0 ? NJ_SCREEN_HEIGHT : 320);
    s->fb_stride = s->fb_w * s->bpp;
    if (s->rotation == 90 || s->rotation == 270) {
        s->out_w = s->fb_h;
        s->out_h = s->fb_w;
    } else {
        s->out_w = s->fb_w;
        s->out_h = s->fb_h;
    }

    output_size = (size_t)s->fb_w * (size_t)s->fb_h * pixel_size;
    s->back  = (unsigned char *)calloc(1, output_size);
    s->front = (unsigned char *)calloc(1, output_size);
    s->send  = (unsigned char *)calloc(1, output_size);
    if (!s->back || !s->front || !s->send) {
        lr_log(s, RETRO_LOG_ERROR, "cannot allocate the framebuffer");
        return 0;
    }
    return 1;
}

static void lr_copy_pixel(unsigned char *dst, const unsigned char *src, int bpp)
{
    switch (bpp) {
    case 2: {
        uint16_t v;
        memcpy(&v, src, 2);
        memcpy(dst, &v, 2);
        break;
    }
    case 3:
        dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
        break;
    default: {
        uint32_t v;
        memcpy(&v, src, 4);
        memcpy(dst, &v, 4);
        break;
    }
    }
}

static void lr_blit(nj_lr *s, int x, int y, int w, int h)
{
    const unsigned char *src = s->back;
    unsigned char *dst = s->front;
    int bpp = s->bpp;
    int W = s->fb_w, H = s->fb_h;
    int OW = s->out_w, OH = s->out_h;
    int row;
    int col;

    if (w <= 0 || h <= 0 || x < 0 || y < 0 ||
        x + w > W || y + h > H) {
        x = 0; y = 0; w = W; h = H;
    }

    if (s->rotation == 0) {
        for (row = 0; row < h; row++) {
            memcpy(dst + (size_t)(y + row) * (size_t)OW * (size_t)bpp +
                        (size_t)x * (size_t)bpp,
                   src + (size_t)(y + row) * (size_t)W * (size_t)bpp +
                        (size_t)x * (size_t)bpp,
                   (size_t)w * (size_t)bpp);
        }
        return;
    }

    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            size_t si = ((size_t)(y + row) * (size_t)W + (size_t)(x + col)) *
                        (size_t)bpp;
            size_t di;
            switch (s->rotation) {
            case 90:  /* dst(x') = H-1-y, dst(y') = x */
                di = ((size_t)(x + col) * (size_t)OW +
                      (size_t)(H - 1 - (y + row))) * (size_t)bpp;
                break;
            case 180:
                di = ((size_t)(OH - 1 - (y + row)) * (size_t)OW +
                      (size_t)(OW - 1 - (x + col))) * (size_t)bpp;
                break;
            default:  /* 270: dst(x') = y, dst(y') = W-1-x */
                di = ((size_t)(W - 1 - (x + col)) * (size_t)OW +
                      (size_t)(y + row)) * (size_t)bpp;
                break;
            }
            lr_copy_pixel(dst + di, src + si, bpp);
        }
    }
}

/* Map a libretro pointer sample (int16, full range across the output) to
 * canvas pixels, undoing the presentation rotation. */
static void lr_pointer_map(nj_lr *s, int px, int py, int *lx, int *ly)
{
    int dx;
    int dy;

    if (px < -32768) px = -32768;
    if (px > 32767)  px = 32767;
    if (py < -32768) py = -32768;
    if (py > 32767)  py = 32767;

    dx = (int)(((long)(px + 0x8000) * s->out_w) >> 16);
    dy = (int)(((long)(py + 0x8000) * s->out_h) >> 16);
    if (dx < 0) dx = 0;
    if (dy < 0) dy = 0;
    if (dx >= s->out_w) dx = s->out_w - 1;
    if (dy >= s->out_h) dy = s->out_h - 1;

    switch (s->rotation) {
    case 90:
        *lx = dy;
        *ly = s->fb_h - 1 - dx;
        break;
    case 180:
        *lx = s->fb_w - 1 - dx;
        *ly = s->fb_h - 1 - dy;
        break;
    case 270:
        *lx = s->fb_w - 1 - dy;
        *ly = dx;
        break;
    default:
        *lx = dx;
        *ly = dy;
        break;
    }
    if (*lx < 0) *lx = 0;
    if (*ly < 0) *ly = 0;
    if (*lx >= s->fb_w) *lx = s->fb_w - 1;
    if (*ly >= s->fb_h) *ly = s->fb_h - 1;
}

/* ------------------------------------------------------------------ */
/* Audio: resampling ring, tone synthesis                             */
/* ------------------------------------------------------------------ */

static size_t lr_ring_count(size_t rd, size_t wr, size_t cap)
{
    return (wr >= rd) ? (wr - rd) : (cap - rd + wr);
}

static int lr_ring_push(int16_t *ring, size_t cap, size_t *rd, size_t *wr,
                        int16_t left, int16_t right)
{
    size_t next = (*wr + 1) % cap;
    if (next == *rd) {
        return 0; /* full: drop this frame, the decoder keeps its place */
    }
    ring[*wr * 2 + 0] = left;
    ring[*wr * 2 + 1] = right;
    *wr = next;
    return 1;
}

static int lr_ring_pop(int16_t *ring, size_t cap, size_t *rd, size_t *wr,
                       int16_t *left, int16_t *right)
{
    if (*rd == *wr) return 0;
    *left  = ring[*rd * 2 + 0];
    *right = ring[*rd * 2 + 1];
    *rd = (*rd + 1) % cap;
    return 1;
}

static int16_t lr_clamp_sample(int v)
{
    if (v > 32767)  return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static void lr_audio_drain(nj_lr *s, int16_t *out, int frames)
{
    int n = 0;

    memset(out, 0, (size_t)frames * 2u * sizeof(int16_t));
    if (!s->sound) return;

    while (n < frames) {
        int16_t pl = 0, pr = 0, tl = 0, tr = 0;
        int got = 0;

        if (lr_ring_pop(s->pcm_ring, s->pcm_cap, &s->pcm_rd, &s->pcm_wr,
                        &pl, &pr)) {
            got = 1;
        }
        if (lr_ring_pop(s->tone_ring, s->tone_cap, &s->tone_rd, &s->tone_wr,
                        &tl, &tr)) {
            got = 1;
        }
        if (!got) break;

        out[n * 2 + 0] = lr_clamp_sample((int)pl + (int)tl);
        out[n * 2 + 1] = lr_clamp_sample((int)pr + (int)tr);
        n++;
    }
}

static int lr_tone_play(void *ud, const nj_audio_tone *tones, int count)
{
    nj_lr *s = (nj_lr *)ud;
    int i;

    if (!s || !s->sound || !tones || count <= 0) return -1;

    lr_mutex_lock(&s->lock);
    for (i = 0; i < count; i++) {
        unsigned freq = tones[i].freq_hz;
        unsigned dur  = tones[i].duration_ms;
        unsigned long long total;
        unsigned long long frame;
        unsigned step = 0;
        unsigned phase = 0;

        if (dur == 0) continue;
        total = ((unsigned long long)dur * LR_AUDIO_RATE) / 1000u;
        if (freq) {
            step = (unsigned)(((unsigned long long)freq << 32) /
                              (unsigned long long)LR_AUDIO_RATE);
        }
        for (frame = 0; frame < total; frame++) {
            int16_t v = 0;
            if (freq) {
                v = (phase & 0x80000000u) ? LR_TONE_AMPLITUDE
                                          : (int16_t)-LR_TONE_AMPLITUDE;
                phase += step;
            }
            if (!lr_ring_push(s->tone_ring, s->tone_cap, &s->tone_rd,
                              &s->tone_wr, v, v)) {
                break;
            }
        }
    }
    lr_mutex_unlock(&s->lock);
    return 0;
}

static void lr_tone_stop(void *ud)
{
    nj_lr *s = (nj_lr *)ud;
    if (!s) return;
    lr_mutex_lock(&s->lock);
    s->tone_rd = s->tone_wr = 0;
    lr_mutex_unlock(&s->lock);
}

static int lr_pcm_open(void *ud, int rate, int channels)
{
    nj_lr *s = (nj_lr *)ud;

    if (!s || !s->sound) return -1;
    if (rate <= 0 || rate > 192000) return -1;
    if (channels < 1 || channels > 2) return -1;

    lr_mutex_lock(&s->lock);
    s->src_rate = rate;
    s->src_ch = channels;
    s->src_pos = 0.0;
    s->src_base = 0;
    s->have_prev = 0;
    s->prev_l = s->prev_r = 0;
    lr_mutex_unlock(&s->lock);
    return 0;
}

/*
 * Accept every frame and resample to 44.1 kHz stereo.  pcm_write() reports
 * frames taken so the runtime only holds back what a full device refused; our
 * ring is two seconds long, so a decoder that pushes a whole WAV in one go
 * finishes immediately and the audio still plays out at the right rate.
 */
static int lr_pcm_write(void *ud, const nj_s2 *samples, int frames)
{
    nj_lr *s = (nj_lr *)ud;
    int i;

    if (!s || !s->sound || !samples || frames <= 0) return 0;
    if (s->src_rate <= 0) {
        s->src_rate = LR_AUDIO_RATE;
        s->src_ch = 2;
    }

    lr_mutex_lock(&s->lock);
    {
        double step = (double)s->src_rate / (double)LR_AUDIO_RATE;

        for (i = 0; i < frames; i++) {
            int16_t l = samples[i * s->src_ch];
            int16_t r = (s->src_ch > 1) ? samples[i * s->src_ch + 1] : l;
            long long k = s->src_base + i;

            if (!s->have_prev) {
                s->prev_l = l;
                s->prev_r = r;
                s->have_prev = 1;
                continue;
            }
            while (s->src_pos < (double)k) {
                double alpha = s->src_pos - (double)(k - 1);
                int16_t ol;
                int16_t orr;

                if (alpha < 0.0) alpha = 0.0;
                if (alpha > 1.0) alpha = 1.0;
                ol = (int16_t)((double)s->prev_l +
                               ((double)l - (double)s->prev_l) * alpha);
                orr = (int16_t)((double)s->prev_r +
                                ((double)r - (double)s->prev_r) * alpha);
                (void)lr_ring_push(s->pcm_ring, s->pcm_cap, &s->pcm_rd,
                                   &s->pcm_wr, ol, orr);
                s->src_pos += step;
            }
            s->prev_l = l;
            s->prev_r = r;
        }
        s->src_base += frames;
    }
    lr_mutex_unlock(&s->lock);
    return frames;
}

static void lr_pcm_close(void *ud) { (void)ud; }

static int lr_pcm_free(void *ud)
{
    nj_lr *s = (nj_lr *)ud;
    size_t used;

    if (!s) return 0;
    lr_mutex_lock(&s->lock);
    used = lr_ring_count(s->pcm_rd, s->pcm_wr, s->pcm_cap);
    lr_mutex_unlock(&s->lock);
    return (int)(s->pcm_cap - used);
}

/* ------------------------------------------------------------------ */
/* Input                                                              */
/* ------------------------------------------------------------------ */

static int lr_push_event(nj_lr *s, nj_u1 type, nj_u1 key, int x, int y)
{
    size_t next = (s->event_wr + 1) % LR_EVENT_QUEUE;
    nj_event *ev;

    if (next == s->event_rd) return 0; /* full: dropping is better than lying */
    ev = &s->events[s->event_wr];
    memset(ev, 0, sizeof *ev);
    ev->type = type;
    ev->key = key;
    ev->x = (nj_s2)x;
    ev->y = (nj_s2)y;
    s->event_wr = next;
    return 1;
}

static void lr_push_event_locked(nj_lr *s, nj_u1 type, nj_u1 key, int x, int y)
{
    lr_mutex_lock(&s->lock);
    (void)lr_push_event(s, type, key, x, y);
    lr_mutex_unlock(&s->lock);
}

static int lr_input_poll(void *ud, nj_event *ev)
{
    nj_lr *s = (nj_lr *)ud;
    int got = 0;

    if (!s || !ev) return 0;
    lr_mutex_lock(&s->lock);
    if (s->event_rd != s->event_wr) {
        *ev = s->events[s->event_rd];
        s->event_rd = (s->event_rd + 1) % LR_EVENT_QUEUE;
        got = 1;
    }
    lr_mutex_unlock(&s->lock);
    return got;
}

static int lr_input_push(void *ud, const nj_event *ev)
{
    nj_lr *s = (nj_lr *)ud;

    if (!s || !ev) return 0;
    lr_mutex_lock(&s->lock);
    if ((s->event_wr + 1) % LR_EVENT_QUEUE == s->event_rd) {
        lr_mutex_unlock(&s->lock);
        return 0;
    }
    s->events[s->event_wr] = *ev;
    s->event_wr = (s->event_wr + 1) % LR_EVENT_QUEUE;
    lr_mutex_unlock(&s->lock);
    return 1;
}

static int lr_keycode_to_key(unsigned keycode)
{
    if (keycode >= RETROK_0 && keycode <= RETROK_9) {
        return NJ_KEY_0 + (int)(keycode - RETROK_0);
    }
    if (keycode >= RETROK_KP0 && keycode <= RETROK_KP9) {
        return NJ_KEY_0 + (int)(keycode - RETROK_KP0);
    }
    switch (keycode) {
    case RETROK_ASTERISK:
#ifdef RETROK_KP_MULTIPLY
    case RETROK_KP_MULTIPLY:
#endif
        return NJ_KEY_STAR;
    case RETROK_HASH:
#ifdef RETROK_KP_DIVIDE
    case RETROK_KP_DIVIDE:
#endif
        return NJ_KEY_POUND;
    case RETROK_RETURN:
#ifdef RETROK_KP_ENTER
    case RETROK_KP_ENTER:
#endif
    case RETROK_SPACE:
        return NJ_KEY_FIRE;
    case RETROK_UP:       return NJ_KEY_UP;
    case RETROK_DOWN:     return NJ_KEY_DOWN;
    case RETROK_LEFT:     return NJ_KEY_LEFT;
    case RETROK_RIGHT:    return NJ_KEY_RIGHT;
    case RETROK_BACKSPACE: return NJ_KEY_SOFT_RIGHT;
    case RETROK_TAB:      return NJ_KEY_SOFT_LEFT;
    case RETROK_ESCAPE:   return NJ_KEY_CLEAR;
    default:              return NJ_KEY_NONE;
    }
}

static void RETRO_CALLCONV lr_keyboard_event(bool down, unsigned keycode,
                                             uint32_t character,
                                             uint16_t mod)
{
    nj_lr *s = core();
    int key = lr_keycode_to_key(keycode);

    (void)character;
    (void)mod;
    if (key == NJ_KEY_NONE) return;
    lr_push_event_locked(s, down ? NJ_EV_KEY_DOWN : NJ_EV_KEY_UP,
                         (nj_u1)key, 0, 0);
}

static struct retro_keyboard_callback lr_keyboard_callback_instance = {
    lr_keyboard_event
};

/* Bit #N is RETRO_DEVICE_ID_JOYPAD_N, so the mask can answer every button in
 * one call.  When the frontend did not accept GET_INPUT_BITMASKS, the same
 * helper falls back to one input_state() call per button. */
static int lr_pad_down(nj_lr *s, uint16_t mask, unsigned id)
{
    if (s->input_bitmask) {
        return (int)((mask >> id) & 1u);
    }
    return s->input_cb(0, RETRO_DEVICE_JOYPAD, 0, id) != 0;
}

static void lr_read_joypad(nj_lr *s)
{
    int i;
    int pressed[NJ_KEY_COUNT];
    int k;
    uint16_t mask = 0;

    if (!s->input_cb) return;

    if (s->input_bitmask) {
        mask = (uint16_t)s->input_cb(0, RETRO_DEVICE_JOYPAD, 0,
                                     RETRO_DEVICE_ID_JOYPAD_MASK);
    }

    memset(pressed, 0, sizeof pressed);
    for (i = 0; i < 8; i++) {
        int id = (int)lr_button_id[i];
        if (s->button_key[i] == NJ_KEY_NONE) continue;
        if (lr_pad_down(s, mask, (unsigned)id)) {
            pressed[s->button_key[i]] = 1;
        }
    }
    /* The D-pad is always wired to the matching J2ME key; the binding table
     * above is for the face and shoulder buttons. */
    if (lr_pad_down(s, mask, RETRO_DEVICE_ID_JOYPAD_UP)) {
        pressed[NJ_KEY_UP] = 1;
    }
    if (lr_pad_down(s, mask, RETRO_DEVICE_ID_JOYPAD_DOWN)) {
        pressed[NJ_KEY_DOWN] = 1;
    }
    if (lr_pad_down(s, mask, RETRO_DEVICE_ID_JOYPAD_LEFT)) {
        pressed[NJ_KEY_LEFT] = 1;
    }
    if (lr_pad_down(s, mask, RETRO_DEVICE_ID_JOYPAD_RIGHT)) {
        pressed[NJ_KEY_RIGHT] = 1;
    }

    lr_mutex_lock(&s->lock);
    for (k = 0; k < NJ_KEY_COUNT; k++) {
        int was = s->key_down[k];
        int is = pressed[k];
        if (is == was) continue;
        s->key_down[k] = (unsigned char)is;
        (void)lr_push_event(s, is ? NJ_EV_KEY_DOWN : NJ_EV_KEY_UP,
                            (nj_u1)k, 0, 0);
    }
    lr_mutex_unlock(&s->lock);
}

static void lr_read_pointer(nj_lr *s)
{
    int px, py, pressed;
    int lx = 0, ly = 0;
    int was;

    if (!s->input_cb) return;

    px = s->input_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
    py = s->input_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);
    pressed = s->input_cb(0, RETRO_DEVICE_POINTER, 0,
                          RETRO_DEVICE_ID_POINTER_PRESSED) != 0;

    lr_pointer_map(s, px, py, &lx, &ly);

    lr_mutex_lock(&s->lock);
    was = s->ptr_down;
    if (pressed && !was) {
        s->ptr_down = 1;
        s->ptr_x = lx;
        s->ptr_y = ly;
        (void)lr_push_event(s, NJ_EV_POINTER_DOWN, 0, lx, ly);
    } else if (pressed) {
        if (lx != s->ptr_x || ly != s->ptr_y) {
            s->ptr_x = lx;
            s->ptr_y = ly;
            (void)lr_push_event(s, NJ_EV_POINTER_MOVE, 0, lx, ly);
        }
    } else if (was) {
        s->ptr_down = 0;
        (void)lr_push_event(s, NJ_EV_POINTER_UP, 0, s->ptr_x, s->ptr_y);
    }
    lr_mutex_unlock(&s->lock);
}

/* ------------------------------------------------------------------ */
/* Rumble: the VM asks on its own thread, the frontend is called here  */
/* ------------------------------------------------------------------ */
/*
 * A MIDlet may call Display.vibrate()/DeviceControl.startVibra() from any Java
 * thread, but the libretro rumble callback belongs to the frontend's run
 * thread.  The port hook therefore only records the request; retro_run()
 * turns it into set_rumble_state() calls.  Duration is wall-clock ms so a
 * slow frame does not stretch the buzz.
 */
static void lr_port_vibrate(void *ud, int ms)
{
    nj_lr *s = (nj_lr *)ud;

    if (!s || !s->sync_ready) return;
    if (ms < 0) ms = 0;

    lr_mutex_lock(&s->lock);
    s->rumble_active = ms > 0;
    s->rumble_deadline_ms = lr_real_ms() + (uint64_t)ms;
    lr_mutex_unlock(&s->lock);
}

/* One weak-motor channel is the honest mapping: the MIDlet asks for "buzz for
 * N ms" and cannot address a second motor.  Strength is full while active. */
static void lr_service_rumble(nj_lr *s)
{
    int want;

    if (!s->rumble_cb) return;

    lr_mutex_lock(&s->lock);
    if (s->rumble_active && lr_real_ms() >= s->rumble_deadline_ms) {
        s->rumble_active = 0;
    }
    want = s->rumble_active;
    lr_mutex_unlock(&s->lock);

    if (want == s->rumble_sent) return;
    if (!s->rumble_cb(0, RETRO_RUMBLE_WEAK,
                      (uint16_t)(want ? 0xFFFFu : 0u)) &&
        want && !s->rumble_warned) {
        lr_log(s, RETRO_LOG_WARN,
               "the frontend rejected a rumble request; continuing without it");
        s->rumble_warned = 1;
    }
    s->rumble_sent = want;
}

/* Called outside retro_run() (unload/deinit): never leave a motor running. */
static void lr_rumble_release(nj_lr *s)
{
    if (s->rumble_cb && s->rumble_sent) {
        (void)s->rumble_cb(0, RETRO_RUMBLE_WEAK, 0);
    }
    s->rumble_sent = 0;
    s->rumble_active = 0;
    s->rumble_deadline_ms = 0;
}

/* ------------------------------------------------------------------ */
/* Clocks and port callbacks                                          */
/* ------------------------------------------------------------------ */

static unsigned long long lr_virtual_ms(const nj_lr *s)
{
    return (s->vclock_frames * 1000ull) / 60ull;
}

static nj_u4 lr_port_millis(void *ud)
{
    nj_lr *s = (nj_lr *)ud;
    unsigned long long now;

    if (!s) return 0;

    lr_mutex_lock(&s->lock);
    switch (s->phase) {
    case LR_PHASE_RUN:  now = lr_virtual_ms(s); break;
    case LR_PHASE_LOAD: now = s->load_ms;       break;
    default:            now = lr_real_ms();     break;
    }
    lr_mutex_unlock(&s->lock);
    return (nj_u4)now;
}

static void lr_sleep_ms(void *ud, nj_u4 ms)
{
    nj_lr *s = (nj_lr *)ud;

    if (!s || ms == 0) return;

    lr_mutex_lock(&s->lock);
    if (s->phase == LR_PHASE_LOAD) {
        /* Loading may run from a constructor: fast-forward the clock instead
         * of waiting for a frontend that is still inside retro_load_game(). */
        s->load_ms += ms;
        lr_cond_broadcast(&s->cond);
        lr_mutex_unlock(&s->lock);
        return;
    }
    if (s->phase == LR_PHASE_RUN) {
        unsigned long long target = lr_virtual_ms(s) + ms;
        /* A request interrupts a host wait, without advancing game time.
         * The scheduler has already parked the Java thread and returns to
         * the outer frame boundary where snapshotting is safe. */
        while (!s->stop && s->req_kind == LR_REQ_NONE &&
               lr_virtual_ms(s) < target) {
#if NJ_LIBRETRO_HW
            s->hw.parked = 1;
            s->hw.park_until = target;
            lr_cond_broadcast(&s->cond);
#endif
            lr_cond_wait(&s->cond, &s->lock);
#if NJ_LIBRETRO_HW
            s->hw.parked = 0;
#endif
        }
        lr_mutex_unlock(&s->lock);
        return;
    }

    /* Teardown: real time, bounded, so destroyApp() cannot outlive the join
     * deadline the frontend set. */
    lr_mutex_unlock(&s->lock);
    lr_real_sleep(ms > 50u ? 50u : ms);
}

static void lr_idle(void *ud)
{
    nj_lr *s = (nj_lr *)ud;

    if (!s) return;
    lr_mutex_lock(&s->lock);
    if (s->phase == LR_PHASE_LOAD) {
        s->load_ms += 1;
    } else if (s->phase == LR_PHASE_RUN) {
        unsigned long long target = lr_virtual_ms(s) + 1;
        while (!s->stop && s->req_kind == LR_REQ_NONE &&
               lr_virtual_ms(s) < target) {
#if NJ_LIBRETRO_HW
            s->hw.parked = 1;
            s->hw.park_until = target;
            lr_cond_broadcast(&s->cond);
#endif
            lr_cond_wait(&s->cond, &s->lock);
#if NJ_LIBRETRO_HW
            s->hw.parked = 0;
#endif
        }
    }
    lr_mutex_unlock(&s->lock);
    if (s->phase == LR_PHASE_STOP) lr_real_sleep(1);
}

static int lr_display_init(void *ud, int *w, int *h, int *stride, void **px)
{
    nj_lr *s = (nj_lr *)ud;

    if (!s || !s->back) return -1;
    if (w) *w = s->fb_w;
    if (h) *h = s->fb_h;
    if (stride) *stride = s->fb_stride;
    if (px) *px = s->back;
    return s->fb_format;
}

static void lr_display_present(void *ud, int x, int y, int w, int h)
{
    nj_lr *s = (nj_lr *)ud;

    if (!s || !s->back || !s->front) return;

    lr_mutex_lock(&s->lock);
    lr_blit(s, x, y, w, h);
    s->posted++;
    lr_cond_broadcast(&s->cond);
    lr_mutex_unlock(&s->lock);
}

static void lr_port_log(void *ud, int level, const char *msg)
{
    nj_lr *s = (nj_lr *)ud;
    enum retro_log_level lv = RETRO_LOG_INFO;

    switch (level) {
    case 0: lv = RETRO_LOG_ERROR; break;
    case 1: lv = RETRO_LOG_WARN;  break;
    case 2: lv = RETRO_LOG_INFO;  break;
    default: lv = RETRO_LOG_DEBUG; break;
    }
    if (s && s->log_cb) {
        s->log_cb(lv, "%s\n", msg ? msg : "");
    }
}

/* ------------------------------------------------------------------ */
/* Port setup                                                         */
/* ------------------------------------------------------------------ */

static void lr_port_init(nj_lr *s)
{
    memset(&s->port, 0, sizeof s->port);
    s->port.name = "libretro";
    s->port.ud = s;
    s->port.user = s;
#if NJ_LIBRETRO_HW
    /* Stable callbacks survive copied nj_gfx and candidate VMs. Availability
     * is checked per call; constructor/teardown never wait for retro_run. */
    if (s->hw.requested) {
        s->port.gfx_accel = &lr_hw_accel;
        s->port.gfx_accel_ud = s;
        s->port.gfx_raster = lr_hw_raster;
        s->port.gfx_raster_ud = s;
        s->port.gfx_triangles = lr_hw_triangles;
        s->port.gfx_triangles_ud = s;
        /*
         * Deliberately no gfx_triangles_list here.  The frontend opens the
         * hardware gate only for LR_HW_BUDGET_MS (2 ms) of each 60 Hz frame
         * and lr_hw_submit declines instead of waiting when the gate is shut,
         * so a deferred batch submitted at a render/barrier boundary is
         * almost always rejected (measured 93k list declines, 2 accepted in
         * 400 Minecraft frames) and every snapshot copy would be wasted.
         * The single-packet path keeps the honest ~0.5% GPU hit rate; a
         * real fix needs frontend-paced submission, not core batching.
         */
    }
#endif

    s->port.millis = lr_port_millis;
    s->port.display_init = lr_display_init;
    s->port.display_present = lr_display_present;
    s->port.input_poll = lr_input_poll;
    s->port.input_push = lr_input_push;
    s->port.log = lr_port_log;
    s->port.sleep_ms = lr_sleep_ms;
    s->port.idle = lr_idle;
    s->port.vibrate = lr_port_vibrate;

    if (s->sound) {
        s->port.tone_play = lr_tone_play;
        s->port.tone_stop = lr_tone_stop;
        s->port.pcm_open = lr_pcm_open;
        s->port.pcm_write = lr_pcm_write;
        s->port.pcm_close = lr_pcm_close;
        s->port.pcm_free = lr_pcm_free;
    }

    s->port.width = s->fb_w;
    s->port.height = s->fb_h;
    s->port.scale = 1;
    /* The frontend owns pacing. An uncapped MIDP frame yields once through
     * port.idle; a snapshot request can interrupt that wait and finish the
     * frame without advancing time. The runtime's capped pacer would keep
     * polling its deadline while retro_serialize() has frozen the clock. */
    s->port.fps_cap = 0;
    s->port.rms_dir = s->base_dir;
    s->port.fs_dir = s->fs_dir;
    /* FileConnection resolves sandbox URLs to host paths before these calls,
     * just as it does for the libc file/stat fallback used by this port. */
    s->port.dir_open = nj_posix_dir_open;
    s->port.dir_next = nj_posix_dir_next;
    s->port.dir_close = nj_posix_dir_close;
}

static const char *lr_save_directory(nj_lr *s)
{
    const char *dir = NULL;

    if (s->environ_cb) {
        if (!s->environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) ||
            !dir || !dir[0]) {
            dir = NULL;
            if (!s->environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) ||
                !dir || !dir[0]) {
                dir = NULL;
            }
        }
    }
    return dir ? dir : ".";
}

static void lr_prepare_directories(nj_lr *s)
{
    const char *save = lr_save_directory(s);

    snprintf(s->save_dir, sizeof s->save_dir, "%s", save);
    snprintf(s->base_dir, sizeof s->base_dir, "%s/nano_j2me/%s",
             s->save_dir, s->stem);
    snprintf(s->fs_dir, sizeof s->fs_dir, "%s/files", s->base_dir);
    lr_mkdir_p(s->base_dir);
    lr_mkdir_p(s->fs_dir);
}

/* ------------------------------------------------------------------ */
/* Save states: the request queue                                     */
/* ------------------------------------------------------------------ */
/* Requests are serviced between scheduler frames on the VM thread. Sleeping
 * Java threads remain parked; no native C stack is serialized. */
static int lr_frame_hook(nj_vm *vm, void *ud);

static nj_u4 lr_state_u32(const nj_u1 *p)
{
    return (nj_u4)p[0] | ((nj_u4)p[1] << 8) |
           ((nj_u4)p[2] << 16) | ((nj_u4)p[3] << 24);
}

static void lr_state_put(nj_u1 *p, nj_u4 v)
{
    p[0] = (nj_u1)v; p[1] = (nj_u1)(v >> 8);
    p[2] = (nj_u1)(v >> 16); p[3] = (nj_u1)(v >> 24);
}

static nj_u4 lr_device_crc(const nj_lr *s)
{
    const char *name = s->device ? s->device->name : "generic";
    return nj_crc32(name, (nj_u4)strlen(name), 0);
}

static nj_u4 lr_state_crc(const nj_u1 *blob, nj_size size)
{
    nj_u4 crc = nj_crc32(blob, 12u, 0);
    return nj_crc32(blob + 16u, size - 16u, crc);
}

static int lr_pack_state(nj_lr *s, nj_u1 **out, nj_size *out_size)
{
    nj_u1 *vm_blob = NULL, *blob;
    nj_size vm_size = 0;
    nj_size pixels = (nj_size)(s->fb_w * s->fb_h * s->bpp);
    nj_size total;
    int rc = nj_vm_save_mem(s->vm, &vm_blob, &vm_size);
    if (rc != NJ_OK) return rc;
    total = LR_STATE_HEADER + 2u * pixels + vm_size;
    blob = (nj_u1 *)calloc(1, total);
    if (!blob) { free(vm_blob); return NJ_ERR_NOMEM; }
    lr_state_put(blob + 0, LR_STATE_MAGIC);
    lr_state_put(blob + 4, LR_STATE_VERSION);
    lr_state_put(blob + 8, total);
    lr_state_put(blob + 16, s->jar_crc);
    lr_state_put(blob + 20, vm_size);
    lr_state_put(blob + 24, pixels);
    lr_state_put(blob + 28, (nj_u4)s->rotation);
    lr_state_put(blob + 32, (nj_u4)s->heap_mb);
    lr_state_put(blob + 36, (nj_u4)s->sound);
    lr_state_put(blob + 40, (nj_u4)s->pointer);
    lr_state_put(blob + 44, lr_device_crc(s));
    lr_state_put(blob + 48, (nj_u4)s->vclock_frames);
    lr_state_put(blob + 52, (nj_u4)(s->vclock_frames >> 32));
    lr_state_put(blob + 56, nj_crc32(NJ_VERSION_STRING,
                                   (nj_u4)strlen(NJ_VERSION_STRING), 0));
    lr_state_put(blob + 60, s->font_crc);
    memcpy(blob + LR_STATE_HEADER, s->back, pixels);
    memcpy(blob + LR_STATE_HEADER + pixels, s->front, pixels);
    memcpy(blob + LR_STATE_HEADER + 2u * pixels, vm_blob, vm_size);
    free(vm_blob);
    lr_state_put(blob + 12, lr_state_crc(blob, total));
    *out = blob;
    *out_size = total;
    return NJ_OK;
}

/* Build a candidate before touching the live VM. A refused/corrupt state must
 * leave the running game, screen and input intact. No Java is executed by the
 * loader, and the candidate's native media handles are cleared by load_mem. */
static int lr_apply_state(nj_lr *s, const nj_u1 *blob, nj_size size)
{
    nj_size total, vm_size, pixels = (nj_size)(s->fb_w * s->fb_h * s->bpp);
    nj_vm *candidate;
    nj_app *app;
    unsigned long long clock_old, clock_new;
    int rc;
    if (size < LR_STATE_HEADER || lr_state_u32(blob) != LR_STATE_MAGIC ||
        lr_state_u32(blob + 4) != LR_STATE_VERSION) return NJ_ERR_FORMAT;
    total = lr_state_u32(blob + 8);
    vm_size = lr_state_u32(blob + 20);
    if (total < LR_STATE_HEADER + 2u * pixels || total > size ||
        vm_size != total - LR_STATE_HEADER - 2u * pixels ||
        lr_state_u32(blob + 24) != pixels ||
        lr_state_u32(blob + 12) != lr_state_crc(blob, total) ||
        lr_state_u32(blob + 16) != s->jar_crc ||
        lr_state_u32(blob + 28) != (nj_u4)s->rotation ||
        lr_state_u32(blob + 32) != (nj_u4)s->heap_mb ||
        lr_state_u32(blob + 36) != (nj_u4)s->sound ||
        lr_state_u32(blob + 40) != (nj_u4)s->pointer ||
        lr_state_u32(blob + 44) != lr_device_crc(s) ||
        lr_state_u32(blob + 60) != s->font_crc ||
        lr_state_u32(blob + 56) != nj_crc32(NJ_VERSION_STRING,
                                      (nj_u4)strlen(NJ_VERSION_STRING), 0))
        return NJ_ERR_FORMAT;
    candidate = nj_vm_create(&s->port, (nj_size)s->heap_mb * 1024u * 1024u);
    if (!candidate) return NJ_ERR_NOMEM;
    app = nj_app_open(candidate, s->jar, (nj_u4)s->jar_size);
    if (!app) { nj_vm_destroy(candidate); return NJ_ERR_FORMAT; }
    if (s->font_data && !nj_vm_use_font_data(candidate, s->font_data,
                                           (nj_u4)s->font_size)) {
        nj_app_close(candidate, app);
        nj_vm_destroy(candidate);
        return NJ_ERR_NOMEM;
    }
    lr_install_properties(s, candidate);
    (void)nj_vm_set_frame_hook(candidate, lr_frame_hook, s);
    clock_new = (unsigned long long)lr_state_u32(blob + 48) |
                ((unsigned long long)lr_state_u32(blob + 52) << 32);
    lr_mutex_lock(&s->lock);
    clock_old = s->vclock_frames;
    s->vclock_frames = clock_new;
    lr_mutex_unlock(&s->lock);
    rc = nj_vm_load_mem(candidate, blob + LR_STATE_HEADER + 2u * pixels, vm_size);
    if (rc != NJ_OK) {
        lr_mutex_lock(&s->lock);
        s->vclock_frames = clock_old;
        lr_mutex_unlock(&s->lock);
        nj_app_close(candidate, app);
        nj_vm_destroy(candidate);
        return rc;
    }
    /* Do not call the old MIDlet's destroyApp(): it may write external saves
     * and must not run when loading a snapshot. */
    nj_app_close(s->vm, s->app);
    nj_vm_destroy(s->vm);
    s->vm = candidate;
    s->app = app;
    s->midlet = NULL;
    lr_mutex_lock(&s->lock);
    memcpy(s->back, blob + LR_STATE_HEADER, pixels);
    memcpy(s->front, blob + LR_STATE_HEADER + pixels, pixels);
    memcpy(s->send, s->front, pixels);
    s->posted++;
    s->consumed = 0;
    s->pcm_rd = s->pcm_wr = s->tone_rd = s->tone_wr = 0;
    s->have_prev = 0;
    s->src_pos = 0;
    s->src_base = 0;
    s->event_rd = s->event_wr = 0;
    memset(s->key_down, 0, sizeof s->key_down);
    s->ptr_down = 0;
    lr_mutex_unlock(&s->lock);
    lr_log(s, RETRO_LOG_INFO, "resumed from a save state");
    return NJ_OK;
}

static void lr_serve_state_request(nj_lr *s)
{
    nj_u1 *blob;
    nj_size size;
    int kind, rc;
    lr_mutex_lock(&s->lock);
    kind = s->req_kind;
    if (kind == LR_REQ_NONE || !nj_vm_can_save(s->vm)) {
        lr_mutex_unlock(&s->lock);
        return;
    }
    s->req_busy = 1;
    blob = s->req_blob;
    size = s->req_size;
    lr_mutex_unlock(&s->lock);
    if (kind == LR_REQ_SAVE) rc = lr_pack_state(s, &blob, &size);
    else {
        rc = lr_apply_state(s, blob, size);
        free(blob);
        blob = NULL;
        size = 0;
    }
    lr_mutex_lock(&s->lock);
    s->req_blob = blob;
    s->req_size = size;
    s->req_rc = rc;
    s->req_kind = LR_REQ_NONE;
    s->req_busy = 0;
    s->req_done = 1;
    lr_cond_broadcast(&s->cond);
    lr_mutex_unlock(&s->lock);
}

/* Frontend owns returned SAVE data; LOAD input is copied so a cancelled wait
 * never leaves a borrowed buffer in use by a late VM request. Once serving
 * begins we join its completion; only waiting for a safe point has a timeout. */
static int lr_request_state(nj_lr *s, int kind, const void *input,
                            nj_size input_size, nj_u1 **out, nj_size *out_size)
{
    uint64_t deadline;
    nj_u1 *copy = NULL;
    int rc = NJ_ERR_STATE;
    if (out) *out = NULL;
    if (out_size) *out_size = 0;
    if (!s->loaded || !s->thread_started) return rc;
    if (input_size) {
        copy = (nj_u1 *)malloc(input_size);
        if (!copy) return NJ_ERR_NOMEM;
        memcpy(copy, input, input_size);
    }
    lr_mutex_lock(&s->lock);
    if (s->req_kind != LR_REQ_NONE || s->vm_done) {
        lr_mutex_unlock(&s->lock);
        free(copy);
        return rc;
    }
#if NJ_LIBRETRO_HW
    lr_hw_close_gate(s);
#endif
    s->req_kind = kind;
    s->req_done = 0;
    s->req_busy = 0;
    s->req_blob = copy;
    s->req_size = input_size;
    lr_cond_broadcast(&s->cond);
    deadline = lr_real_ms() + LR_STATE_TIMEOUT_MS;
    while (!s->req_done && !s->vm_done) {
        uint64_t now = lr_real_ms();
        if (now >= deadline && !s->req_busy) break;
        (void)lr_cond_wait_ms(&s->cond, &s->lock, 100u);
    }
    if (s->req_done) {
        rc = s->req_rc;
        if (out) { *out = s->req_blob; s->req_blob = NULL; }
        if (out_size) *out_size = s->req_size;
    }
    free(s->req_blob);
    s->req_blob = NULL;
    s->req_size = 0;
    s->req_kind = LR_REQ_NONE;
    s->req_done = 0;
    lr_mutex_unlock(&s->lock);
    if (rc != NJ_OK) lr_log(s, RETRO_LOG_WARN,
                           "save-state request refused (%d); the game continues", rc);
    return rc;
}

/* Drives the MIDlet frame by frame and serves save-state requests between
 * frames.  nj_midp_run() would hide exactly that boundary, so it is spelled
 * out here; a snapshot must not be taken while a frame is in progress. */
static void lr_run_frames(nj_lr *s)
{
    while (nj_vm_running(s->vm)) {
        int stop;

        lr_serve_state_request(s);
        lr_mutex_lock(&s->lock);
        stop = s->stop;
        lr_mutex_unlock(&s->lock);
        if (stop) {
            return;
        }
        if (nj_midp_frame(s->vm) != NJ_OK) {
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* VM thread                                                          */
/* ------------------------------------------------------------------ */

static int lr_frame_hook(nj_vm *vm, void *ud)
{
    nj_lr *s = (nj_lr *)ud;
    int stop;

    (void)vm;
    lr_mutex_lock(&s->lock);
    stop = s->stop;
    lr_mutex_unlock(&s->lock);
    if (stop) return 1;
    /*
     * This hook is pumped by the VM while a Java thread is parked inside a
     * blocking native call, so it is not a safe point for a snapshot
     * (nj_vm_can_save() refuses exactly that).  Save requests are served by
     * lr_run_frames() between two frames instead.
     */
    (void)nj_midp_frame(vm);
    return 0;
}

static void *lr_vm_thread(void *arg)
{
    nj_lr *s = (nj_lr *)arg;
    nj_status st;
    int stop;

#if NJ_LIBRETRO_HW
    lr_mutex_lock(&s->lock);
    s->hw.worker = lr_thread_self();
    s->hw.worker_known = 1;
    lr_mutex_unlock(&s->lock);
#endif
    st = nj_vm_start_app_async(s->vm, s->midlet);

    lr_mutex_lock(&s->lock);
    stop = s->stop;
    lr_mutex_unlock(&s->lock);

    if (st != NJ_OK && !stop) {
        const char *why = nj_exception_describe(s->vm);
        lr_log(s, RETRO_LOG_ERROR, "startApp() failed: %s",
               why ? why : "unknown error");
        lr_mutex_lock(&s->lock);
        s->vm_failed = 1;
        lr_mutex_unlock(&s->lock);
    } else if (!stop) {
        int waited = 0;

        /* nj_run() gives a MIDlet that paints late a grace period; the same
         * applies here.  nj_midp_frame() returns NJ_ERR while no Displayable
         * exists, so this cannot spin forever. */
        while (waited < 60 && nj_vm_running(s->vm) &&
               !nj_midp_current(s->vm)) {
            lr_mutex_lock(&s->lock);
            stop = s->stop;
            lr_mutex_unlock(&s->lock);
            if (stop) break;
            lr_serve_state_request(s);
            (void)nj_midp_frame(s->vm);
            waited++;
        }

        lr_mutex_lock(&s->lock);
        stop = s->stop;
        lr_mutex_unlock(&s->lock);

        if (!stop && nj_vm_running(s->vm)) {
            if (!nj_midp_current(s->vm)) {
                lr_log(s, RETRO_LOG_WARN,
                       "the MIDlet installed no Displayable; nothing to show");
            } else {
                lr_run_frames(s);
            }
        }
    }

    {
        nj_s4 code = nj_vm_exit_code(s->vm);
        const char *why = nj_vm_stop_why(s->vm);
        if (code != 0) {
            lr_log(s, RETRO_LOG_WARN, "run ended: %s (exit code %d)",
                   why ? why : "the MIDlet exited", (int)code);
        } else if (why) {
            lr_log(s, RETRO_LOG_INFO, "run ended: %s", why);
        }
    }

    lr_mutex_lock(&s->lock);
    s->vm_done = 1;
#if NJ_LIBRETRO_HW
    lr_hw_close_gate(s);
    s->hw.worker_known = 0;
#endif
    lr_cond_broadcast(&s->cond);
    lr_mutex_unlock(&s->lock);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Load / unload                                                      */
/* ------------------------------------------------------------------ */

static void lr_release_buffers(nj_lr *s)
{
    free(s->jar);
    free(s->font_data);
    s->font_data = NULL;
    s->font_size = 0;
    s->font_crc = 0;
    free(s->back);
    free(s->front);
    free(s->send);
    free(s->pcm_ring);
    free(s->tone_ring);
    s->jar = NULL;
    s->back = NULL;
    s->front = NULL;
    s->send = NULL;
    s->pcm_ring = NULL;
    s->tone_ring = NULL;
    s->jar_size = 0;
    s->pcm_cap = s->pcm_rd = s->pcm_wr = 0;
    s->tone_cap = s->tone_rd = s->tone_wr = 0;
}

static void lr_unload(nj_lr *s)
{
    if (s->sync_ready) {
        lr_mutex_lock(&s->lock);
        s->phase = LR_PHASE_STOP;
        s->stop = 1;
#if NJ_LIBRETRO_HW
        lr_hw_close_gate(s);
#endif
        lr_cond_broadcast(&s->cond);
        lr_mutex_unlock(&s->lock);
    }

    if (s->thread_started) {

        if (s->vm) {
            /* A MIDlet may be inside startApp(), a sleep or destroyApp();
             * a wall-clock deadline makes every one of those return. */
            (void)nj_vm_set_halt_after_ms(s->vm, 100);
            nj_vm_stop_reason(s->vm, 0, "the frontend unloaded the core");
        }
        lr_thread_join(&s->thread);
        s->thread_started = 0;
    }

    if (s->vm) {
        (void)nj_vm_set_halt_after_ms(s->vm, 1500);
        if (s->app) {
            (void)nj_midp_destroy_app(s->vm, 1);
            nj_app_close(s->vm, s->app);
            s->app = NULL;
        }
        (void)nj_vm_destroy(s->vm);
        s->vm = NULL;
    }

#if NJ_LIBRETRO_HW
    if (s->loaded && s->hw.requested) {
        lr_log(s, RETRO_LOG_INFO,
               "GPU completion fills=%llu blits=%llu rasters=%llu triangles=%llu declines=%llu lists=%llu; RPC queued=%llu executed=%llu cancelled=%llu presents=%llu",
               s->hw.fills, s->hw.blits, s->hw.rasters, s->hw.triangles,
               s->hw.declines, s->hw.triangle_lists,
               s->hw.queued, s->hw.executed, s->hw.cancelled, s->hw.presents);
    }
    s->hw.worker_known = 0;
    s->hw.parked = 0;
    s->hw.slot = LR_HW_EMPTY;
    memset(&s->hw.request, 0, sizeof s->hw.request);
    /* Keep GL resources until the frontend's context_destroy callback. Unload
     * and reset do not carry a current-context guarantee. */
#endif
    lr_release_buffers(s);
    lr_rumble_release(s);
    free(s->sys_props);
    s->sys_props = NULL;
    s->nsys_props = 0;
    s->midlet = 0;
    s->loaded = 0;
    s->vm_done = 0;
    s->vm_failed = 0;
    s->shutdown_sent = 0;
    s->stop = 0;
    s->posted = 0;
    s->consumed = 0;
    s->vclock_frames = 0;
    s->load_ms = 0;
    s->event_rd = s->event_wr = 0;
    memset(s->key_down, 0, sizeof s->key_down);
    s->ptr_down = 0;
    /* Nothing is left running that could serve a pending save request. */
    s->req_kind = LR_REQ_NONE;
    s->req_done = 0;
    s->req_busy = 0;
    s->req_rc = NJ_ERR_STATE;
    free(s->req_blob);
    s->req_blob = NULL;
    s->req_size = 0;
    s->jar_crc = 0;
    s->state_bound = 0;
    s->game_path[0] = '\0';
    s->stem[0] = '\0';
}

static int lr_allocate_audio(nj_lr *s)
{
    s->pcm_ring = (int16_t *)calloc(LR_AUDIO_FRAMES, 2 * sizeof(int16_t));
    s->tone_ring = (int16_t *)calloc(LR_AUDIO_FRAMES, 2 * sizeof(int16_t));
    if (!s->pcm_ring || !s->tone_ring) {
        return 0;
    }
    s->pcm_cap = LR_AUDIO_FRAMES;
    s->tone_cap = LR_AUDIO_FRAMES;
    s->pcm_rd = s->pcm_wr = 0;
    s->tone_rd = s->tone_wr = 0;
    return 1;
}

static bool lr_load(const struct retro_game_info *info, int resetting)
{
    nj_lr *s = core();
    const char *midlet_class;
    nj_ref exc = 0;
#if NJ_LIBRETRO_HW
    int requested = s->hw.requested;
#else
    (void)resetting;
#endif

    if (s->loaded) lr_unload(s);
    if (!info) return false;

    s->phase = LR_PHASE_LOAD;
    s->stop = 0;

#if NJ_LIBRETRO_HW
    /* Reset rebuilds Java state without replacing the frontend's context.
     * SET_HW_RENDER is only allowed during retro_load_game, and a frontend
     * need not issue context_reset when retro_reset returns. */
    if (!resetting) {
        s->hw.accepted = 0;
        s->hw.ready = 0;
    }
    s->hw.fills = s->hw.blits = s->hw.rasters = s->hw.triangles =
        s->hw.triangle_lists = 0;
    s->hw.declines = s->hw.queued = s->hw.executed = s->hw.cancelled = s->hw.presents = 0;
#endif
    lr_read_config(s);
#if NJ_LIBRETRO_HW
    /* Changing the renderer requires content reload, not a Java reset. */
    if (resetting) s->hw.requested = requested;
#endif

    if (!lr_read_file(info->path, &s->jar, &s->jar_size)) {
        if (info->data && info->size > 0) {
            s->jar = (unsigned char *)malloc(info->size);
            if (s->jar) {
                memcpy(s->jar, info->data, info->size);
                s->jar_size = info->size;
            }
        }
    }
    if (!s->jar) {
        lr_log(s, RETRO_LOG_ERROR, "cannot read %s",
               info->path ? info->path : "(no path)");
        lr_unload(s);
        return false;
    }
    if (info->path && info->path[0]) {
        snprintf(s->game_path, sizeof s->game_path, "%s", info->path);
    }
    lr_stem_from_path(s, s->game_path[0] ? s->game_path : "game.jar");
    lr_prepare_directories(s);

    if (!lr_video_init(s)) {
        lr_unload(s);
        return false;
    }

    lr_port_init(s);
    if (s->sound && !lr_allocate_audio(s)) {
        lr_log(s, RETRO_LOG_ERROR, "cannot allocate the audio rings");
        lr_unload(s);
        return false;
    }

    s->vm = nj_vm_create(&s->port,
                         (nj_size)s->heap_mb * 1024u * 1024u);
    if (!s->vm) {
        lr_log(s, RETRO_LOG_ERROR, "cannot create a VM with a %d MB heap",
               s->heap_mb);
        lr_unload(s);
        return false;
    }
    lr_load_font(s);
    /* The largest blob this VM can produce; retro_serialize_size() reports it
     * so the frontend can allocate the state file in one piece. */
    s->state_bound = nj_vm_blob_max(s->vm) + LR_STATE_HEADER +
                     2u * (nj_size)(s->fb_w * s->fb_h * s->bpp);
    s->jar_crc = nj_crc32(s->jar, (nj_u4)s->jar_size, 0);

    s->app = nj_app_open(s->vm, s->jar, (nj_u4)s->jar_size);
    if (!s->app) {
        lr_log(s, RETRO_LOG_ERROR,
               "not a usable MIDlet suite (bad JAR or no MIDlet-1 in the "
               "manifest)");
        lr_unload(s);
        return false;
    }
    midlet_class = nj_app_midlet_class(s->app, 0);
    if (!midlet_class) {
        lr_log(s, RETRO_LOG_ERROR, "the manifest names no MIDlet class");
        lr_unload(s);
        return false;
    }

    if (s->device) nj_vm_set_device(s->vm, s->device);
    if (s->sys_props && s->nsys_props > 0u) {
        nj_vm_set_system_properties(s->vm, s->sys_props, s->nsys_props);
    }
    (void)nj_vm_set_frame_hook(s->vm, lr_frame_hook, s);

    /*
     * Construct the MIDlet synchronously: a bad manifest class or a
     * constructor that throws is a load failure the frontend should see, not a
     * core that loads and then exits.  Constructors that sleep are handled by
     * the fast-forwarding load clock, so this does not block on real time.
     */
    s->midlet = nj_vm_new_midlet(s->vm, midlet_class, &exc);
    if (!s->midlet) {
        const char *why = nj_exception_describe(s->vm);
        lr_log(s, RETRO_LOG_ERROR, "cannot instantiate %s: %s", midlet_class,
               why ? why : "unknown error");
        lr_unload(s);
        return false;
    }

#if NJ_LIBRETRO_HW
    if (!resetting) lr_hw_negotiate(s);
#endif
    s->loaded = 1;
    lr_log(s, RETRO_LOG_INFO, "running %s (heap %d MB, device %s)",
           midlet_class, s->heap_mb,
           s->device ? s->device->name : "generic");

    if (lr_thread_start(&s->thread, lr_vm_thread, s) != 0) {
        lr_log(s, RETRO_LOG_ERROR, "cannot start the VM thread");
        lr_unload(s);
        return false;
    }
    s->thread_started = 1;

    /* From here on the VM waits on the virtual clock that retro_run() drives.
     * The thread may already be blocked in startApp(), which is exactly what
     * those first calls wake up. */
    lr_mutex_lock(&s->lock);
    s->phase = LR_PHASE_RUN;
    lr_cond_broadcast(&s->cond);
    lr_mutex_unlock(&s->lock);

    return true;
}

/* ------------------------------------------------------------------ */
/* libretro entry points                                              */
/* ------------------------------------------------------------------ */

RETRO_API void retro_set_environment(retro_environment_t cb)
{
    nj_lr *s = core();

    s->environ_cb = cb;
    if (!s->sync_ready) {
        lr_mutex_init(&s->lock);
        lr_cond_init(&s->cond);
#if NJ_LIBRETRO_HW
        lr_mutex_init(&s->hw.gl_lock);
#endif
        s->sync_ready = 1;
    }
    lr_register_environment(s);
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)
{
    core()->video_cb = cb;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)
{
    core()->sample_cb = cb;
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    core()->batch_cb = cb;
}

RETRO_API void retro_set_input_poll(retro_input_poll_t cb)
{
    core()->poll_cb = cb;
}

RETRO_API void retro_set_input_state(retro_input_state_t cb)
{
    core()->input_cb = cb;
}

RETRO_API void retro_init(void)
{
    nj_lr *s = core();

    if (!s->sync_ready) {
        lr_mutex_init(&s->lock);
        lr_cond_init(&s->cond);
#if NJ_LIBRETRO_HW
        lr_mutex_init(&s->hw.gl_lock);
#endif
        s->sync_ready = 1;
    }
    if (s->environ_cb) {
        struct retro_log_callback lc;
        memset(&lc, 0, sizeof lc);
        if (s->environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &lc)) {
            s->log_cb = lc.log;
        }
    }
    s->phase = LR_PHASE_STOP;
    s->out_w = 240;
    s->out_h = 320;
    lr_log(s, RETRO_LOG_INFO, "nano_j2me %s libretro core", NJ_VERSION_STRING);
}

RETRO_API void retro_deinit(void)
{
    nj_lr *s = core();

    lr_unload(s);
    if (s->sync_ready) {
#if NJ_LIBRETRO_HW
        lr_mutex_lock(&s->hw.gl_lock);
        if (s->hw.context) {
            /* No current-context guarantee at deinit. The frontend owns the
             * context; free CPU bookkeeping only if it omitted destroy. */
            lr_log(s, RETRO_LOG_WARN, "frontend omitted context_destroy; releasing CPU GL bookkeeping only");
            s->hw.driver->destroy(s->hw.context, 1);
            s->hw.context = NULL;
        }
        lr_mutex_unlock(&s->hw.gl_lock);
        lr_mutex_destroy(&s->hw.gl_lock);
#endif
        lr_mutex_destroy(&s->lock);
        lr_cond_destroy(&s->cond);
    }
    memset(s, 0, sizeof *s);
}

RETRO_API unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
    if (!info) return;
    memset(info, 0, sizeof *info);
    info->library_name     = "nano_j2me";
    info->library_version  = NJ_VERSION_STRING;
    info->valid_extensions = "jar";
    info->need_fullpath    = true;  /* the runtime reads the JAR in place */
    info->block_extract    = true;  /* a JAR is the content, not an archive */
}

/* Mandatory entry point added after libretro v1; RetroArch 1.22+ refuses to
 * load a core that does not export it.  The J2ME screen refresh is not tied
 * to any analogue broadcast standard, but the API requires a region. */
RETRO_API unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
    nj_lr *s = core();
    int w = s->out_w > 0 ? s->out_w : 240;
    int h = s->out_h > 0 ? s->out_h : 320;

    if (!info) return;
    memset(info, 0, sizeof *info);
    info->geometry.base_width   = (unsigned)w;
    info->geometry.base_height  = (unsigned)h;
    info->geometry.max_width    = (unsigned)w;
    info->geometry.max_height   = (unsigned)h;
    info->geometry.aspect_ratio = (float)w / (float)h;
    info->timing.fps            = 60.0;
    info->timing.sample_rate    = (double)LR_AUDIO_RATE;
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
    (void)port;
    (void)device;
}

RETRO_API void retro_reset(void)
{
    nj_lr *s = core();
    struct retro_game_info info;
    char path[LR_PATH_MAX];

    if (!s->loaded || !s->game_path[0]) return;
    memcpy(path, s->game_path, sizeof path);
    path[sizeof path - 1] = '\0';

    lr_unload(s);
    memset(&info, 0, sizeof info);
    info.path = path;
    (void)lr_load(&info, 1);
}

RETRO_API void retro_run(void)
{
    nj_lr *s = core();
    int video_ready, shutdown;
#if NJ_LIBRETRO_HW
    int hw_active = 0, hw_presented = 0;
#endif

    if (!s->loaded) {
        /* The frontend may call retro_run() once before content is loaded
         * (retro_load_game returns false); a black frame is the honest
         * answer. */
        static const uint16_t black[240 * 320];
        if (s->video_cb) {
#if NJ_LIBRETRO_HW
            if (s->hw.accepted) s->video_cb(NULL, 240, 320, 0);
            else
#endif
                s->video_cb(black, 240, 320, 240 * (int)sizeof(uint16_t));
        }
        return;
    }

    if (s->poll_cb) s->poll_cb();
    lr_read_joypad(s);
    if (s->pointer) lr_read_pointer(s);
    lr_service_rumble(s);
#if NJ_LIBRETRO_HW
    hw_active = lr_hw_begin(s);
#endif

    /*
     * Keep the critical section short: the VM thread has to take this same
     * lock from its sleep/hook/display callbacks, and a frontend callback
     * (which may hash a whole frame) must never run under it.  A completed
     * frame is copied into the send buffer here, so the video callback sees a
     * stable frame even if the VM thread presents the next one meanwhile.
     */
    lr_mutex_lock(&s->lock);

    /* One frontend frame is one virtual 60 Hz frame.  Everything the runtime
     * sleeps for is measured against this, so a paused frontend pauses the
     * MIDlet and a running one fast-forwards it. */
#if NJ_LIBRETRO_HW
    if (hw_active) s->hw.gate = 1;
#endif
    s->vclock_frames++;
    lr_cond_broadcast(&s->cond);
#if NJ_LIBRETRO_HW
    if (hw_active) {
        lr_mutex_unlock(&s->lock);
        lr_hw_pump(s);
        lr_mutex_lock(&s->lock);
    }
#endif

    if (s->posted > s->consumed) {
        if (s->front && s->send) {
            memcpy(s->send, s->front,
                   (size_t)s->fb_w * (size_t)s->fb_h * (size_t)s->bpp);
        }
        s->consumed = s->posted;
    }

    lr_audio_drain(s, s->mix, LR_FRAMES_PER_RUN);

    lr_mutex_unlock(&s->lock);

#if NJ_LIBRETRO_HW
    if (hw_active) hw_presented = lr_hw_end(s);
#endif
    video_ready = (s->video_cb != NULL && s->send != NULL);
    if (video_ready) {
        int pitch = s->out_w * s->bpp;
#if NJ_LIBRETRO_HW
        if (s->hw.accepted) {
            /* Once hardware was accepted, CPU pointers are never legal video
             * data, including when context/presenter initialization failed. */
            s->video_cb(hw_presented ? RETRO_HW_FRAME_BUFFER_VALID : NULL,
                        (unsigned)s->out_w, (unsigned)s->out_h, 0);
        } else
#endif
            s->video_cb(s->send, (unsigned)s->out_w, (unsigned)s->out_h,
                        (size_t)pitch);
    }

    if (s->batch_cb) {
        (void)s->batch_cb(s->mix, LR_FRAMES_PER_RUN);
    } else if (s->sample_cb) {
        int i;
        for (i = 0; i < LR_FRAMES_PER_RUN; i++) {
            s->sample_cb(s->mix[i * 2 + 0], s->mix[i * 2 + 1]);
        }
    }

    lr_mutex_lock(&s->lock);
    shutdown = s->vm_done && !s->shutdown_sent;
    if (shutdown) s->shutdown_sent = 1;
    lr_mutex_unlock(&s->lock);
    if (shutdown && s->environ_cb) {
        (void)s->environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
    }
}

RETRO_API bool retro_load_game(const struct retro_game_info *info)
{
    nj_lr *s = core();

    if (!s->sync_ready) {
        lr_mutex_init(&s->lock);
        lr_cond_init(&s->cond);
#if NJ_LIBRETRO_HW
        lr_mutex_init(&s->hw.gl_lock);
#endif
        s->sync_ready = 1;
    }
    /* The frontend queries retro_get_system_av_info() after loading. Sending
     * SET_SYSTEM_AV_INFO here initializes its drivers before normal startup
     * and can make Android EGL initialization fail on the second attempt.
     * That environment call is only valid inside retro_run(). */
    return lr_load(info, 0);
}

RETRO_API void retro_unload_game(void)
{
    lr_unload(core());
}

RETRO_API bool retro_load_game_special(unsigned type,
                                       const struct retro_game_info *info,
                                       size_t num_info)
{
    (void)type;
    (void)info;
    (void)num_info;
    return false;
}

RETRO_API size_t retro_serialize_size(void)
{
    nj_lr *s = core();

    if (!s->loaded || s->state_bound == 0) {
        return 0;
    }
    /* A stable upper bound includes the VM arena and both framebuffers.
     * The envelope records its true length; trailing zero padding is ignored
     * when the frontend passes its full serialize_size() allocation back. */
    return (size_t)s->state_bound;
}

RETRO_API bool retro_serialize(void *data, size_t size)
{
    nj_lr *s = core();
    nj_u1 *blob = NULL;
    nj_size blob_size = 0;

    if (!data || size < (size_t)s->state_bound || !s->loaded) {
        return false;
    }
    if (lr_request_state(s, LR_REQ_SAVE, NULL, 0, &blob, &blob_size) != NJ_OK || !blob) {
        free(blob);
        return false;
    }
    if ((size_t)blob_size > size) {
        free(blob);
        return false;
    }
    memcpy(data, blob, (size_t)blob_size);
    if ((size_t)blob_size < size) {
        memset((nj_u1 *)data + blob_size, 0, size - (size_t)blob_size);
    }
    free(blob);
    return true;
}

RETRO_API bool retro_unserialize(const void *data, size_t size)
{
    nj_lr *s = core();
    if (!s->loaded || !data || size < LR_STATE_HEADER ||
        size > (size_t)s->state_bound) return false;
    return lr_request_state(s, LR_REQ_LOAD, data, (nj_size)size,
                            NULL, NULL) == NJ_OK;
}

RETRO_API void retro_cheat_reset(void) { }

RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
    (void)index;
    (void)enabled;
    (void)code;
}

RETRO_API void *retro_get_memory_data(unsigned id)
{
    (void)id;
    return NULL;
}

RETRO_API size_t retro_get_memory_size(unsigned id)
{
    (void)id;
    return 0;
}
