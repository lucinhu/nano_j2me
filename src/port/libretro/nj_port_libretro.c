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
 * refused by name rather than pretending), MIDI-to-GM handoff (the runtime
 * renders MIDI itself into the same PCM path) and save states (a snapshot has
 * to be taken by the thread that owns the VM; that needs a request queue this
 * first version does not have - retro_serialize_size() says 0 instead of
 * promising something it cannot deliver).
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

#include "libretro.h"
#include "nj2me/nj.h"

/* ------------------------------------------------------------------ */
/* Platform layer: threads, clocks, directories                       */
/*                                                                    */
/* The runtime is C99 and so is this file; both pthreads and Win32    */
/* threads have a small enough surface to wrap here.  Everything else */
/* in the core is platform-neutral.                                   */
/* ------------------------------------------------------------------ */

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
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

typedef struct lr_opt_value {
    const char *value;
    const char *label;
} lr_opt_value;

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
    int   button_key[8]; /* nj_key for A, B, X, Y, L, R, Select, Start */

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
    { "16",  "16 MB"  },
    { "32",  "32 MB"  },
    { "64",  "64 MB"  },
    { "128", "128 MB" },
    { NULL, NULL }
};

static const lr_opt_value lr_switch_values[] = {
    { "enabled",  "Enabled"  },
    { "disabled", "Disabled" },
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

static const lr_option_meta lr_options[] = {
    { "nano_j2me_device", "Device profile",
      "Which handset's properties Game MIDlets see. Restart the content after changing it.",
      "system", "generic", lr_device_values },
    { "nano_j2me_rotation", "Screen rotation",
      "Rotates the picture only; the MIDlet keeps painting a 240x320 canvas.",
      "system", "0", lr_rotation_values },
    { "nano_j2me_heap", "Java heap",
      "Size of the Java heap a MIDlet gets. Needs a content reload.",
      "system", "64", lr_heap_values },
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

    if (!s->environ_cb) return;

    lr_build_option_definitions();

    (void)s->environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
    (void)s->environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
    (void)s->environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS,
                        (void *)lr_input_descriptors);
    (void)s->environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK,
                        (void *)&lr_keyboard_callback_instance);

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

static void lr_read_config(nj_lr *s)
{
    const char *v;
    int i;

    v = lr_option(s, "nano_j2me_device", "generic");
    s->device = nj_device_find(v);
    if (!s->device) s->device = nj_device_default();

    v = lr_option(s, "nano_j2me_rotation", "0");
    s->rotation = atoi(v);
    if (s->rotation != 90 && s->rotation != 180 && s->rotation != 270) {
        s->rotation = 0;
    }

    v = lr_option(s, "nano_j2me_heap", "64");
    s->heap_mb = atoi(v);
    if (s->heap_mb < 8)   s->heap_mb = 8;
    if (s->heap_mb > 512) s->heap_mb = 512;

    s->sound = strcmp(lr_option(s, "nano_j2me_sound", "enabled"), "disabled") != 0;
    s->pointer = strcmp(lr_option(s, "nano_j2me_pointer", "enabled"),
                        "disabled") != 0;

    for (i = 0; i < 8; i++) {
        s->button_key[i] = lr_key_from_name(
            lr_option(s, lr_button_key[i], lr_button_default[i]));
    }
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

    /* The canvas the MIDlet sees is always 240x320; rotation happens when the
     * finished frame is copied for the frontend, so the game's coordinate
     * system never changes under it. */
    s->fb_w = NJ_SCREEN_WIDTH  > 0 ? NJ_SCREEN_WIDTH  : 240;
    s->fb_h = NJ_SCREEN_HEIGHT > 0 ? NJ_SCREEN_HEIGHT : 320;
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

static void lr_read_joypad(nj_lr *s)
{
    int i;
    int pressed[NJ_KEY_COUNT];
    int k;

    if (!s->input_cb) return;

    memset(pressed, 0, sizeof pressed);
    for (i = 0; i < 8; i++) {
        int id = (int)lr_button_id[i];
        int down;
        if (s->button_key[i] == NJ_KEY_NONE) continue;
        down = s->input_cb(0, RETRO_DEVICE_JOYPAD, 0, (unsigned)id) != 0;
        if (down) pressed[s->button_key[i]] = 1;
    }
    /* The D-pad is always wired to the matching J2ME key; the binding table
     * above is for the face and shoulder buttons. */
    if (s->input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP)) {
        pressed[NJ_KEY_UP] = 1;
    }
    if (s->input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN)) {
        pressed[NJ_KEY_DOWN] = 1;
    }
    if (s->input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT)) {
        pressed[NJ_KEY_LEFT] = 1;
    }
    if (s->input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT)) {
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

        while (!s->stop && lr_virtual_ms(s) < target) {
            lr_cond_wait(&s->cond, &s->lock);
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
        while (!s->stop && lr_virtual_ms(s) < target) {
            lr_cond_wait(&s->cond, &s->lock);
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

    s->port.millis = lr_port_millis;
    s->port.display_init = lr_display_init;
    s->port.display_present = lr_display_present;
    s->port.input_poll = lr_input_poll;
    s->port.input_push = lr_input_push;
    s->port.log = lr_port_log;
    s->port.sleep_ms = lr_sleep_ms;
    s->port.idle = lr_idle;

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
    /* The runtime paces itself to 60 fps against our virtual clock; every
     * sleep it asks for parks on the clock instead of burning real time. */
    s->port.fps_cap = 60;
    s->port.rms_dir = s->base_dir;
    s->port.fs_dir = s->fs_dir;
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
    (void)nj_midp_frame(vm);
    return 0;
}

static void *lr_vm_thread(void *arg)
{
    nj_lr *s = (nj_lr *)arg;
    nj_status st;
    int stop;

    st = nj_vm_start_app(s->vm, s->midlet);

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
            if (nj_midp_frame(s->vm) != NJ_OK) {
                break;
            }
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
                (void)nj_midp_run(s->vm);
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
    s->phase = LR_PHASE_STOP;

    if (s->thread_started) {
        lr_mutex_lock(&s->lock);
        s->stop = 1;
        lr_cond_broadcast(&s->cond);
        lr_mutex_unlock(&s->lock);

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

    lr_release_buffers(s);
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

static bool lr_load(const struct retro_game_info *info)
{
    nj_lr *s = core();
    const char *midlet_class;
    nj_ref exc = 0;

    if (s->loaded) lr_unload(s);
    if (!info) return false;

    s->phase = LR_PHASE_LOAD;
    s->stop = 0;

    lr_read_config(s);

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
    (void)lr_load(&info);
}

RETRO_API void retro_run(void)
{
    nj_lr *s = core();
    int video_ready;

    if (!s->loaded) {
        /* The frontend may call retro_run() once before content is loaded
         * (retro_load_game returns false); a black frame is the honest
         * answer. */
        static const uint16_t black[240 * 320];
        if (s->video_cb) {
            s->video_cb(black, 240, 320, 240 * (int)sizeof(uint16_t));
        }
        return;
    }

    if (s->poll_cb) s->poll_cb();
    lr_read_joypad(s);
    if (s->pointer) lr_read_pointer(s);

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
    s->vclock_frames++;
    lr_cond_broadcast(&s->cond);

    if (s->posted > s->consumed) {
        if (s->front && s->send) {
            memcpy(s->send, s->front,
                   (size_t)s->fb_w * (size_t)s->fb_h * (size_t)s->bpp);
        }
        s->consumed = s->posted;
    }

    lr_audio_drain(s, s->mix, LR_FRAMES_PER_RUN);

    lr_mutex_unlock(&s->lock);

    video_ready = (s->video_cb != NULL && s->send != NULL);
    if (video_ready) {
        int pitch = s->out_w * s->bpp;
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

    if (s->vm_done && !s->shutdown_sent) {
        lr_mutex_lock(&s->lock);
        s->shutdown_sent = 1;
        lr_mutex_unlock(&s->lock);
        if (s->environ_cb) {
            (void)s->environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
        }
    }
}

RETRO_API bool retro_load_game(const struct retro_game_info *info)
{
    nj_lr *s = core();
    int ok;

    if (!s->sync_ready) {
        lr_mutex_init(&s->lock);
        lr_cond_init(&s->cond);
        s->sync_ready = 1;
    }
    ok = lr_load(info);
    if (ok) {
        struct retro_system_av_info av;
        retro_get_system_av_info(&av);
        if (s->environ_cb) {
            (void)s->environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
        }
    }
    return ok ? true : false;
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
    /* A snapshot must be written by the thread that owns the VM; the request
     * queue that would make that possible is not in this version.  Zero says
     * so instead of promising a save state that would fail. */
    return 0;
}

RETRO_API bool retro_serialize(void *data, size_t size)
{
    (void)data;
    (void)size;
    return false;
}

RETRO_API bool retro_unserialize(const void *data, size_t size)
{
    (void)data;
    (void)size;
    return false;
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
