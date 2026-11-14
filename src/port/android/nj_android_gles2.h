/* Private Android run-thread binding; no EGL dependency in default/legacy builds. */
#ifndef NJ_ANDROID_GLES2_H
#define NJ_ANDROID_GLES2_H

#ifndef NJ_ANDROID_GLES2
#define NJ_ANDROID_GLES2 0
#endif
#ifndef NJ_ANDROID_LEGACY
#define NJ_ANDROID_LEGACY 0
#endif

#include "nj2me/nj_types.h"

/* CPU-only status shared with the UI. No graphics API calls are needed to read
 * it: 0 software, 1 requested/pending, 2 ready, 3 fallback, 4 ended, 5 absent. */
typedef struct nj_android_gles2_info {
    int state;
    nj_u8 fills, blits, rasters, declines;
    int requested;
    nj_u8 triangles, batches; /* committed native primitives / transactions */
} nj_android_gles2_info;

#if defined(__ANDROID__) && NJ_ANDROID_GLES2 && !NJ_ANDROID_LEGACY
#include "nj2me/nj_port.h"
#include "nj2me/nj_gfx_gles2.h"
#include <EGL/egl.h>
#include <pthread.h>

#if !NJ_ENABLE_GFX_ACCEL
#error "NJ_ANDROID_GLES2 requires NJ_ENABLE_GFX_ACCEL=1"
#endif

/* Embedded in android_state: port/Graphics userdata never points at a backend
 * that can be destroyed at run exit. The mutex also serializes late/foreign
 * callbacks, which decline without touching EGL or GL. */
typedef struct nj_android_gles2 {
    pthread_mutex_t lock;
    /* Only lock -> info_lock nesting is permitted. UI reads never take lock
     * (which can cover a slow driver call), and never query GL/EGL state. */
    pthread_mutex_t info_lock;
    nj_android_gles2_info info;
    pthread_t owner;
    int mode; /* -1 property, 0 software, 1 GLES2; frozen when owner_set */
    int owner_set, requested, initialized, failed, stopped;
    EGLDisplay display;
    EGLContext context;
    EGLSurface pbuffer;
    nj_gles2 *backend;
    unsigned int (*reset_status)(void);
    nj_gles2_stats stats;
} nj_android_gles2;

/* Construction only: initializes stable wrappers, without querying properties
 * or calling EGL. A failed mutex initialization leaves the port software-only. */
int nj_android_gles2_bind(nj_android_gles2 *state, nj_port *port);
/* Nonblocking startup selection: -1 property, 0 software, 1 GLES2. Returns 0
 * for an invalid mode, a busy backend or after startup. Does not read properties
 * or call GL/EGL; explicit GLES2 immediately publishes requested/pending. */
int nj_android_gles2_set_mode(nj_android_gles2 *state, int mode);
/* Call once before nj_run on its thread. mode=-1 preserves a pending explicit
 * selection; otherwise reads debug.nj2me.gles2 exactly once. Explicit 0/1 never
 * read that property. The original entry point is equivalent to mode=-1.
 * Direct port users outside this lifecycle stay software-only. */
void nj_android_gles2_start_mode(nj_android_gles2 *state, int mode);
void nj_android_gles2_start(nj_android_gles2 *state);
/* Copies the last completed CPU snapshot under info_lock only. NULL state is
 * software/not started with zero counters. Output storage must be supplied. */
void nj_android_gles2_snapshot(nj_android_gles2 *state, nj_android_gles2_info *out);
/* Same thread, after nj_run has destroyed its VM and before JNI detach. Leaves
 * wrapper userdata alive and inert until port shutdown. */
void nj_android_gles2_stop(nj_android_gles2 *state);
/* No EGL/GL calls. Only after the VM/run thread is finished; returns 0 instead
 * of destroying reachable storage if the owner failed to stop its resources. */
int nj_android_gles2_dispose(nj_android_gles2 *state);
#endif
#endif
