/*
 * nano_j2me - nj_web.c
 *
 * The Emscripten / WebAssembly port.
 *
 * A browser owns the main thread, so nj_run() cannot be used here: it blocks
 * until the MIDlet exits. This file is the other half of the contract in
 * include/nj2me/nj_web.h - a *pumped* runner:
 *
 *     njw_start(jar)        create the VM, open the JAR, construct the MIDlet,
 *                           queue startApp()
 *     njw_frame(dt_ms)      run one host frame, from requestAnimationFrame
 *     njw_*                 input, text, audio, saves, metadata
 *     njw_stop()            tear the VM down
 *
 * The structure mirrors two existing embedders instead of inventing a third
 * one: src/runner/nj_run.c for the setup/teardown sequence (RMS directory,
 * JAR blob, device/font/app, constructor, 60-frame no-Displayable grace loop,
 * destroyApp() with a halt budget) and src/port/libretro/nj_port_libretro.c
 * for owning the frame clock (a virtual millisecond counter the host advances,
 * with the constructor's sleeps fast-forwarded so load does not hang).
 *
 * Why single-threaded
 * -------------------
 * The core has no pthread usage and never blocks on the host: every wait a
 * Java thread performs goes through the scheduler, which calls back into the
 * port (idle/sleep_ms) rather than sleeping a host thread. That is why the
 * same code can run with a virtual clock on a browser main thread. NO
 * Asyncify, NO pthreads, NO SharedArrayBuffer/COOP-COEP requirements.
 *
 * The virtual clock
 * -----------------
 * port.millis returns a 64-bit microsecond counter / 1000. In the LOAD phase
 * (MIDlet construction) sleep_ms/idle fast-forward it, which is what breaks
 * the classic "constructor sleeps through a 2 second splash screen" hang: the
 * sleep expires immediately and start-up finishes inside one JS call. In the
 * RUN phase the host advances the clock by dt once per njw_frame(), and a
 * sleep inside a pumped C call gets only a tiny fast-forward (see
 * web_sleep_ms) so a blocking native call terminates without either freezing
 * the tab or making sleep-paced games run at wire speed.
 *
 * Memory
 * ------
 * Emscripten's malloc only guarantees 8-byte alignment in wasm32, while the
 * Java heap arena must be 16-byte aligned (nj_heap.c rejects anything else
 * with "nj_vm_create: heap failed"). The port therefore supplies alloc/free
 * backed by posix_memalign(NJ_OBJ_ALIGN). Save-state blobs are the exception:
 * nj_vm_save_mem() allocates them with plain malloc and documents that the
 * caller frees them with free(), so njw_alloc/njw_free are a separate,
 * 16-aligned malloc/free pair.
 *
 * Like the libretro core, this file has one process-wide runner instance
 * (g_runner): the JS side has no userdata pointer and the module is a single
 * emulator, so one instance is the ABI, not a shortcut. Every byte of MIDlet
 * state still lives in the nj_vm and is released by njw_stop().
 */

/* posix_memalign(), localtime_r() and gmtime_r() are POSIX, not ISO C; their
 * declarations sit behind this feature-test macro in the Emscripten libc. */
#define _GNU_SOURCE 1

#include "nj2me/nj_web.h"

#include "nj2me/nj.h"
#include "nj2me/nj_app.h"
#include "nj2me/nj_config.h"
#include "nj2me/nj_device.h"
#include "nj2me/nj_port.h"
#include "nj2me/nj_vm.h"
#include "nj2me/nj_zip.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#  define NJW_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#  define NJW_EXPORT
#endif

/* ------------------------------------------------------------------ */
/* Limits and tuning                                                   */
/* ------------------------------------------------------------------ */

/* Input queue: a browser can deliver a whole burst (key rollover, pointer
 * moves) between two animation frames.  The runtime drains at most 256 events
 * per frame, so matching that bound loses nothing. */
#define NJW_EVENT_QUEUE 256u

/* Replace-editor text, in UTF-16 code units.  A TextBox's maxSize is usually
 * well under this; a longer paste is truncated. */
#define NJW_REPLACE_UNITS 2048
#define NJW_REPLACE_SLOTS 4

/* Audio ring, in frames.  ~1.4 seconds of stereo at 22050 Hz: long enough for
 * a tone jingle to be queued whole, short enough that a stop() is immediate. */
#define NJW_AUDIO_RING_FRAMES 32768

/* JAR / icon size guards: a browser upload is not trusted, and the wasm heap
 * is not infinite. */
#define NJW_MAX_JAR_BYTES (64u * 1024u * 1024u)
#define NJW_MAX_ICON_BYTES (512u * 1024u)
#define NJW_MAX_MIDLETS 16

/* Loading: how many frames one njw_frame() may pump while startApp() has not
 * produced a Displayable, and the virtual-time budget before the runner gives
 * up the way nj_run's 60-frame grace loop does.  The budget is virtual, so a
 * slow host gets the same amount of emulated work rather than a wall-clock
 * race. */
#define NJW_LOAD_PUMPS 4
#define NJW_DISPLAYABLE_TIMEOUT_US 20000000ull

#define NJW_NOMINAL_TICK_US 16667ull
#define NJW_MAX_DT_US 250000ull

#define NJW_MILLIS_MASK 0xFFFFFFFFull

enum {
    NJW_PHASE_STOP = 0,
    NJW_PHASE_LOAD = 1,   /* constructing the MIDlet: sleeps fast-forward  */
    NJW_PHASE_RUN  = 2    /* host-driven: the clock advances per frame     */
};

#define NJW_STEM_MAX 80
#define NJW_DIR_MAX 192

typedef struct njw_runner {
    /* --- options (njw_option_*) --- */
    int      opt_width;
    int      opt_height;
    nj_u4    opt_heap;
    int      opt_speed;
    char     opt_rms_dir[NJW_DIR_MAX];
    char     opt_fs_dir[NJW_DIR_MAX];
    char     opt_device[64];
    char     opt_midlet_class[NJ_APP_CLASS_MAX];
    char     opt_load_state[NJW_DIR_MAX];
    int      opt_load_state_set;

    /* --- life cycle --- */
    nj_vm       *vm;
    nj_app      *app;
    nj_port      port;              /* copied by the runtime; pointers stay ours */
    unsigned char *jar;             /* the JAR bytes, owned here */
    nj_u4        jar_size;
    char         stem[NJW_STEM_MAX];
    int          phase;
    int          started;
    int          vm_usable;         /* 0 after a failed state restore */
    unsigned     frames;
    unsigned     epoch;
    int          displayable_ready;
    int          drew_this_pump;
    char         error[320];
    char         stop_reason[192];

    /* --- virtual clock --- */
    nj_u8 clock_us;                 /* monotonic, host advanced */
    nj_u8 start_us;                 /* when startApp() was queued */
    unsigned pump_sleep_calls;      /* fast-forward budget of this pump */

    /* --- display --- */
    int           fb_w;
    int           fb_h;
    int           fb_bpp;
    size_t        fb_stride;
    unsigned char *fb;              /* RGB565/RGB888/ARGB8888 target */
    unsigned char *rgba;            /* RGBA8888 staging for JS */

    /* --- effective directories (port.rms_dir/fs_dir point here) --- */
    char rms_dir[NJW_DIR_MAX];
    char fs_dir[NJW_DIR_MAX];

    /* --- input queue --- */
    nj_event events[NJW_EVENT_QUEUE];
    unsigned ev_head;
    unsigned ev_tail;

    /* --- native text editor --- */
    int          text_active;
    int          text_multiline;
    unsigned     text_constraints;
    nj_u4        text_session;
    nj_u4        text_sequence;
    int          text_max_length;
    int          text_length;
    int          text_cursor;
    nj_u2       *text_copy;
    int          text_copy_cap;
    nj_u4        replace_sequence;
    unsigned     replace_slot;
    nj_u2        replace_text[NJW_REPLACE_SLOTS][NJW_REPLACE_UNITS];

    /* --- audio ring --- */
    nj_s2       *audio;
    int          audio_rate;
    int          audio_channels;
    int          audio_head;        /* frame index */
    int          audio_fill;        /* frames queued */
    int          audio_scheduled;   /* pulled, scheduled by JS, not yet heard */

    /* --- probe results (njw_probe / njw_app_*) --- */
    char         app_title[64];
    char         app_vendor[64];
    char         app_version[32];
    int          app_midlet_count;
    char         app_classes[NJW_MAX_MIDLETS][NJ_APP_CLASS_MAX];
    unsigned char *app_icon;
    int          app_icon_len;
} njw_runner;

/* The options defaults must be in place before any JS call, including a
 * njw_start() that never touched njw_option_*; a static initializer keeps
 * them separate from the (also zero-able) run state. */
static njw_runner g_runner = {
    .opt_width = 240,
    .opt_height = 320,
    .opt_speed = 100
};

/* ================================================================== */
/* Small utilities                                                     */
/* ================================================================== */

static void runner_set_error(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

static void runner_set_error(const char *fmt, ...)
{
    va_list ap;
    if (g_runner.error[0] != '\0') {
        return;                                /* keep the first failure */
    }
    va_start(ap, fmt);
    (void)vsnprintf(g_runner.error, sizeof g_runner.error, fmt, ap);
    va_end(ap);
    fprintf(stderr, "[nj2me] error: %s\n", g_runner.error);
}

static int runner_has_error(void)
{
    return g_runner.error[0] != '\0';
}

/* The `why` strings the runtime sets are literals and stay valid after the VM
 * is destroyed, but the exit code needs formatting, so snapshot it into runner
 * storage while the VM is still alive. */
static void runner_capture_stop_reason(void)
{
    nj_s4 code;

    if (g_runner.stop_reason[0] != '\0' || !g_runner.vm) {
        return;
    }
    code = nj_vm_exit_code(g_runner.vm);
    if (nj_vm_stop_why(g_runner.vm) != NULL) {
        (void)snprintf(g_runner.stop_reason, sizeof g_runner.stop_reason,
                       "%s (exit code %d)", nj_vm_stop_why(g_runner.vm),
                       (int)code);
    } else {
        (void)snprintf(g_runner.stop_reason, sizeof g_runner.stop_reason,
                       "the MIDlet stopped (exit code %d)", (int)code);
    }
}

static int clampi(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static int njw_format_bpp(int format)
{
    switch (format) {
    case NJ_PIXFMT_RGB888:   return 3;
    case NJ_PIXFMT_ARGB8888: return 4;
    default:                 return 2;
    }
}

/* "a/b/c.jar" -> "c" (sanitized for a directory name). */
static void stem_from_path(const char *path, char *out, size_t cap)
{
    const char *base;
    size_t n = 0u;

    if (cap == 0u) {
        return;
    }
    out[0] = '\0';
    if (path != NULL) {
        base = strrchr(path, '/');
#ifdef _WIN32
        if (strrchr(path, '\\') != NULL &&
            (base == NULL || strrchr(path, '\\') > base)) {
            base = strrchr(path, '\\');
        }
#endif
        base = (base != NULL) ? base + 1 : path;
        while (*base != '\0' && *base != '.' && n + 1u < cap) {
            char c = *base++;
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') || c == '-' || c == '_' ||
                c == '.') {
                out[n++] = c;
            } else {
                out[n++] = '-';
            }
        }
    }
    if (n == 0u) {
        (void)snprintf(out, cap, "game");
    } else {
        out[n] = '\0';
    }
}

static int mkdir_p(const char *path)
{
    char tmp[NJW_DIR_MAX];
    size_t len;
    size_t i;

    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    len = strlen(path);
    if (len >= sizeof tmp) {
        return -1;
    }
    memcpy(tmp, path, len + 1u);
    for (i = 1u; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
                return -1;
            }
            tmp[i] = '/';
        }
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static unsigned char *read_whole_file(const char *path, nj_u4 *out_len)
{
    FILE *f;
    long end;
    unsigned char *buf;
    size_t got;

    if (out_len != NULL) {
        *out_len = 0u;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0L, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    end = ftell(f);
    if (end <= 0 || (unsigned long)end > (unsigned long)NJW_MAX_JAR_BYTES) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0L, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (unsigned char *)malloc((size_t)end);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    got = fread(buf, 1u, (size_t)end, f);
    fclose(f);
    if (got != (size_t)end) {
        free(buf);
        return NULL;
    }
    if (out_len != NULL) {
        *out_len = (nj_u4)end;
    }
    return buf;
}

/* ------------------------------------------------------------------ */
/* UTF-8 <-> UTF-16                                                    */
/* ------------------------------------------------------------------ */

/*
 * Decode UTF-8 into UTF-16.  Returns the number of code units written, or -1
 * when `utf8` is NULL or the output does not fit.  Invalid bytes become
 * U+FFFD; the caller is a browser text field, and dropping the whole paste
 * because one byte is wrong is worse than substituting it.
 */
static int utf8_to_utf16(const char *utf8, nj_u2 *out, int cap)
{
    const unsigned char *p = (const unsigned char *)utf8;
    int n = 0;

    if (utf8 == NULL) {
        return -1;
    }
    while (*p != 0u) {
        nj_u4 cp;
        unsigned char c = *p++;

        if (c < 0x80u) {
            cp = c;
        } else if ((c & 0xE0u) == 0xC0u) {
            if ((p[0] & 0xC0u) != 0x80u) {
                cp = 0xFFFDu;
            } else {
                cp = ((nj_u4)(c & 0x1Fu) << 6) | (nj_u4)(p[0] & 0x3Fu);
                p += 1;
                if (cp < 0x80u) {
                    cp = 0xFFFDu;
                }
            }
        } else if ((c & 0xF0u) == 0xE0u) {
            if ((p[0] & 0xC0u) != 0x80u || (p[1] & 0xC0u) != 0x80u) {
                cp = 0xFFFDu;
            } else {
                cp = ((nj_u4)(c & 0x0Fu) << 12) |
                     ((nj_u4)(p[0] & 0x3Fu) << 6) | (nj_u4)(p[1] & 0x3Fu);
                p += 2;
                if (cp < 0x800u || (cp >= 0xD800u && cp <= 0xDFFFu)) {
                    cp = 0xFFFDu;
                }
            }
        } else if ((c & 0xF8u) == 0xF0u) {
            if ((p[0] & 0xC0u) != 0x80u || (p[1] & 0xC0u) != 0x80u ||
                (p[2] & 0xC0u) != 0x80u) {
                cp = 0xFFFDu;
            } else {
                cp = ((nj_u4)(c & 0x07u) << 18) |
                     ((nj_u4)(p[0] & 0x3Fu) << 12) |
                     ((nj_u4)(p[1] & 0x3Fu) << 6) | (nj_u4)(p[2] & 0x3Fu);
                p += 3;
                if (cp < 0x10000u || cp > 0x10FFFFu) {
                    cp = 0xFFFDu;
                }
            }
        } else {
            cp = 0xFFFDu;
        }
        if (cp >= 0x10000u) {
            if (n + 2 > cap) {
                return -1;
            }
            cp -= 0x10000u;
            out[n++] = (nj_u2)(0xD800u + (cp >> 10));
            out[n++] = (nj_u2)(0xDC00u + (cp & 0x3FFu));
        } else {
            if (n + 1 > cap) {
                return -1;
            }
            out[n++] = (nj_u2)cp;
        }
    }
    return n;
}

/* Bytes needed to UTF-8 encode `units`, NUL not included; -1 for surrogates
 * that are not a valid pair (encoded as U+FFFD instead, so this never fails). */
static int utf16_to_utf8(const nj_u2 *units, int count, char *out, int cap)
{
    int need = 0;
    int i;

    for (i = 0; i < count; i++) {
        nj_u4 cp = units[i];

        if (cp >= 0xD800u && cp <= 0xDBFFu && i + 1 < count &&
            units[i + 1] >= 0xDC00u && units[i + 1] <= 0xDFFFu) {
            cp = 0x10000u + ((cp - 0xD800u) << 10) +
                 ((nj_u4)units[i + 1] - 0xDC00u);
            i++;
        } else if (cp >= 0xD800u && cp <= 0xDFFFu) {
            cp = 0xFFFDu;
        }
        if (cp < 0x80u) {
            if (out != NULL && need + 1 < cap) {
                out[need] = (char)cp;
            }
            need += 1;
        } else if (cp < 0x800u) {
            if (out != NULL && need + 2 < cap) {
                out[need] = (char)(0xC0u | (cp >> 6));
                out[need + 1] = (char)(0x80u | (cp & 0x3Fu));
            }
            need += 2;
        } else if (cp < 0x10000u) {
            if (out != NULL && need + 3 < cap) {
                out[need] = (char)(0xE0u | (cp >> 12));
                out[need + 1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
                out[need + 2] = (char)(0x80u | (cp & 0x3Fu));
            }
            need += 3;
        } else {
            if (out != NULL && need + 4 < cap) {
                out[need] = (char)(0xF0u | (cp >> 18));
                out[need + 1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
                out[need + 2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
                out[need + 3] = (char)(0x80u | (cp & 0x3Fu));
            }
            need += 4;
        }
    }
    if (out != NULL && cap > 0) {
        out[need < cap ? need : cap - 1] = '\0';
    }
    return need;
}

/* ================================================================== */
/* Virtual clock and port hooks                                        */
/* ================================================================== */

static nj_u4 web_port_millis(void *ud)
{
    (void)ud;
    return (nj_u4)(g_runner.clock_us / 1000ull);
}

/*
 * Fast-forward the clock instead of blocking the browser main thread.
 *
 * LOAD phase: honour the whole request. A constructor sleep is the one wait
 * that must finish before njw_start() can return, and the emulator does nothing
 * else meanwhile, so advancing the clock is the correct semantics rather than a
 * trick.
 *
 * RUN phase: a pumped C call (see nj_vm.c's in_call_pump path) calls this in a
 * loop waiting for a Java thread's deadline. Give the first few calls 1 ms so
 * the loop makes real progress, then allow a little more: a game whose own
 * frame loop is Thread.sleep(50)-paced must not spin through 50 calls of
 * 1 ms each every frame either. The deadline is virtual time, so the thread
 * always resumes - the loop cannot hang.
 */
static void web_port_sleep_ms(void *ud, nj_u4 ms)
{
    (void)ud;
    if (ms == 0u) {
        return;
    }
    if (g_runner.phase == NJW_PHASE_LOAD) {
        g_runner.clock_us += (nj_u8)ms * 1000ull;
        return;
    }
    g_runner.pump_sleep_calls++;
    if (g_runner.pump_sleep_calls > 3u) {
        if (ms > 50u) {
            ms = 50u;
        }
    } else {
        if (ms > 1u) {
            ms = 1u;
        }
    }
    g_runner.clock_us += (nj_u8)ms * 1000ull;
}

static void web_port_idle(void *ud)
{
    (void)ud;
    /* Nothing to do in the browser: the host owns the frame cadence. During
     * construction, though, an idle loop is the only thing standing between
     * the VM and a blocking wait, so nudge the load clock like the libretro
     * LOAD phase does. */
    if (g_runner.phase == NJW_PHASE_LOAD) {
        g_runner.clock_us += 1000ull;
    }
}

/* posix_memalign, because wasm32's malloc only guarantees 8 bytes and the
 * Java heap arena demands NJ_OBJ_ALIGN (16).  See the header comment. */
static void *web_port_alloc(void *ud, nj_size n)
{
    void *p = NULL;

    (void)ud;
    if (n == 0u) {
        n = 1u;
    }
    if (posix_memalign(&p, (size_t)NJ_OBJ_ALIGN, (size_t)n) != 0) {
        return NULL;
    }
    return p;
}

static void web_port_free(void *ud, void *p)
{
    (void)ud;
    free(p);
}

static void web_port_log(void *ud, int level, const char *msg)
{
    (void)ud;
    (void)level;
    if (msg != NULL) {
        fprintf(stderr, "[nj2me] %s\n", msg);
    }
}

/*
 * Device properties.  These are facts about the MACHINE, and the runtime asks
 * the port for every property it does not answer itself.
 *
 * com.nokia.keyboard.type deserves the same comment the headless port carries:
 * titles probe it and call .equals() on the result without checking for null
 * (Bubblet's constructor does exactly that), which is a bug in the game but a
 * bug a real device never exposed.  A browser has no keypad and this port
 * provides its own on-screen pad, so "None" is the honest answer: the game
 * hides its Nokia keypad command, which is what the device would do.
 * microedition.platform is the port's identity, matching every other port's
 * "nano_j2me/<name>" spelling.
 */
static const char *web_device_property(void *ud, const char *key)
{
    (void)ud;
    if (key == NULL) {
        return NULL;
    }
    if (strcmp(key, "com.nokia.keyboard.type") == 0) {
        return "None";
    }
    if (strcmp(key, "com.nokia.mid.ui.version") == 0) {
        return "1.1";
    }
    if (strcmp(key, "microedition.platform") == 0) {
        return "nano_j2me/web";
    }
    return NULL;                    /* honestly unknown */
}

/* Local wall clock for java.util.Date / Calendar.  Emscripten's musl has a
 * real time() (backed by the JS Date), so the only work is the UTC offset. */
static int web_port_now(void *ud, nj_datetime *out)
{
    time_t t;
    struct tm lt;
    struct tm gt;
    struct tm l2;
    struct tm g2;
    long off;

    (void)ud;
    if (out == NULL) {
        return 0;
    }
    t = time(NULL);
    if (t == (time_t)-1) {
        return 0;
    }
    if (localtime_r(&t, &lt) == NULL || gmtime_r(&t, &gt) == NULL) {
        return 0;
    }
    out->year = (nj_s4)(lt.tm_year + 1900);
    out->month = (nj_s4)lt.tm_mon;
    out->day = (nj_s4)lt.tm_mday;
    out->hour = (nj_s4)lt.tm_hour;
    out->minute = (nj_s4)lt.tm_min;
    out->second = (nj_s4)lt.tm_sec;
    out->millis = 0;
    out->weekday = (nj_s4)lt.tm_wday;
    l2 = lt;
    g2 = gt;
    l2.tm_isdst = -1;
    g2.tm_isdst = -1;
    off = (long)(difftime(mktime(&l2), mktime(&g2)));
    out->tz_offset = (nj_s4)(off / 60l);
    return 1;
}

/* ---- display ---- */

static int web_display_init(void *ud, int *width, int *height, int *stride,
                            void **pixels)
{
    njw_runner *r = (njw_runner *)ud;

    if (r == NULL || r->fb == NULL || width == NULL || height == NULL ||
        stride == NULL || pixels == NULL) {
        return -1;
    }
    *width = r->fb_w;
    *height = r->fb_h;
    *stride = (int)r->fb_stride;
    *pixels = r->fb;
    return NJ_PIXFMT;
}

static void web_convert_rect(int x, int y, int w, int h)
{
    njw_runner *r = &g_runner;
    int xx;
    int yy;

    if (r->fb == NULL || r->rgba == NULL) {
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (w > r->fb_w - x) {
        w = r->fb_w - x;
    }
    if (h > r->fb_h - y) {
        h = r->fb_h - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    for (yy = 0; yy < h; yy++) {
        const unsigned char *src = r->fb + (size_t)(y + yy) * r->fb_stride +
                                   (size_t)x * (size_t)r->fb_bpp;
        unsigned char *dst = r->rgba + ((size_t)(y + yy) * (size_t)r->fb_w +
                                        (size_t)x) * 4u;
        for (xx = 0; xx < w; xx++) {
            if (r->fb_bpp == 2) {
                nj_u2 v;
                nj_u4 r5;
                nj_u4 g6;
                nj_u4 b5;
                memcpy(&v, src, sizeof v);
                r5 = ((nj_u4)v >> 11) & 0x1Fu;
                g6 = ((nj_u4)v >> 5) & 0x3Fu;
                b5 = (nj_u4)v & 0x1Fu;
                dst[0] = (unsigned char)((r5 << 3) | (r5 >> 2));
                dst[1] = (unsigned char)((g6 << 2) | (g6 >> 4));
                dst[2] = (unsigned char)((b5 << 3) | (b5 >> 2));
                dst[3] = 0xFFu;
                src += 2;
            } else if (r->fb_bpp == 3) {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = 0xFFu;
                src += 3;
            } else {
                nj_u4 v;
                memcpy(&v, src, sizeof v);
                dst[0] = (unsigned char)((v >> 16) & 0xFFu);
                dst[1] = (unsigned char)((v >> 8) & 0xFFu);
                dst[2] = (unsigned char)(v & 0xFFu);
                dst[3] = (unsigned char)((v >> 24) & 0xFFu);
                src += 4;
            }
            dst += 4;
        }
    }
}

static void web_display_present(void *ud, int x, int y, int w, int h)
{
    njw_runner *r = (njw_runner *)ud;

    if (r == NULL) {
        return;
    }
    /* nj_midp_present() always passes the full screen today, but the contract
     * allows the smallest changed rectangle, so convert what it names. */
    web_convert_rect(x, y, w, h);
    r->drew_this_pump = 1;
    r->epoch++;
}

/* ---- input ---- */

static int web_input_poll(void *ud, nj_event *ev)
{
    njw_runner *r = (njw_runner *)ud;

    if (r == NULL || ev == NULL || r->ev_head == r->ev_tail) {
        return 0;
    }
    *ev = r->events[r->ev_head];
    r->ev_head = (r->ev_head + 1u) % NJW_EVENT_QUEUE;
    return 1;
}

static int web_input_push(void *ud, const nj_event *ev)
{
    njw_runner *r = (njw_runner *)ud;
    unsigned next;

    if (r == NULL || ev == NULL) {
        return 0;
    }
    next = (r->ev_tail + 1u) % NJW_EVENT_QUEUE;
    if (next == r->ev_head) {
        return 0;                              /* full: drop, never overwrite */
    }
    r->events[r->ev_tail] = *ev;
    r->ev_tail = next;
    return 1;
}

static int runner_queue_event(const nj_event *ev)
{
    return web_input_push(&g_runner, ev);
}

/* ---- native text editor snapshot ---- */

static void web_text_input(void *ud, const nj_text_input_state *st)
{
    njw_runner *r = (njw_runner *)ud;

    if (r == NULL || st == NULL) {
        return;
    }
    r->text_active = st->active;
    r->text_multiline = st->multiline;
    r->text_constraints = st->constraints;
    r->text_session = st->session;
    r->text_sequence = st->sequence;
    r->text_max_length = st->max_length;
    r->text_length = st->active ? st->length : 0;
    r->text_cursor = st->active ? st->cursor : 0;
    if (st->active && st->length > 0 && st->text != NULL) {
        if (r->text_copy_cap < st->length || r->text_copy == NULL) {
            int cap = st->length + 32;
            nj_u2 *buf = (nj_u2 *)realloc(r->text_copy,
                                          (size_t)cap * sizeof(nj_u2));
            if (buf == NULL) {
                r->text_length = 0;
                return;
            }
            r->text_copy = buf;
            r->text_copy_cap = cap;
        }
        memcpy(r->text_copy, st->text, (size_t)st->length * sizeof(nj_u2));
    }
}

/* ---- audio ---- */

static int audio_space(void)
{
    njw_runner *r = &g_runner;
    return r->audio != NULL ? (NJW_AUDIO_RING_FRAMES - r->audio_fill) : 0;
}

/* Append interleaved frames, dropping whatever does not fit. */
static void audio_push(const nj_s2 *samples, int frames)
{
    njw_runner *r = &g_runner;
    int chans;
    int left;

    if (r->audio == NULL || samples == NULL || frames <= 0) {
        return;
    }
    chans = r->audio_channels > 0 ? r->audio_channels : 1;
    left = frames;
    if (left > audio_space()) {
        left = audio_space();
    }
    while (left > 0) {
        int run = NJW_AUDIO_RING_FRAMES - r->audio_head;
        nj_s2 *dst;

        if (run > left) {
            run = left;
        }
        dst = r->audio + (size_t)r->audio_head * (size_t)chans;
        memcpy(dst, samples, (size_t)run * (size_t)chans * sizeof(nj_s2));
        samples += (size_t)run * (size_t)chans;
        r->audio_head = (r->audio_head + run) % NJW_AUDIO_RING_FRAMES;
        r->audio_fill += run;
        left -= run;
    }
}

/* A tone sequence becomes a square-wave chime, like the SDL2 port: an integer
 * phase accumulator needs no FPU and is easy to reason about.  Frequency 0 is
 * a rest, not a low note. */
static int web_tone_play(void *ud, const nj_audio_tone *tones, int count)
{
    njw_runner *r = (njw_runner *)ud;
    nj_s2 chunk[512 * 2];
    int chans;
    int i;

    if (r == NULL || tones == NULL || count <= 0) {
        return -1;
    }
    chans = r->audio_channels > 0 ? r->audio_channels : 1;
    for (i = 0; i < count; i++) {
        nj_u4 n = (nj_u4)tones[i].duration_ms *
                  (nj_u4)(r->audio_rate > 0 ? r->audio_rate : 22050) / 1000u;
        nj_u4 done = 0u;
        nj_u4 half = 0u;
        nj_u4 nch = (nj_u4)chans;

        if (n == 0u) {
            continue;
        }
        if (tones[i].freq_hz != 0u) {
            half = (nj_u4)(r->audio_rate > 0 ? r->audio_rate : 22050) /
                   ((nj_u4)tones[i].freq_hz * 2u);
            if (half == 0u) {
                half = 1u;
            }
        }
        while (done < n) {
            nj_u4 run = n - done;
            nj_u4 k;
            if (run > 512u) {
                run = 512u;
            }
            for (k = 0u; k < run; k++) {
                nj_s2 v = 0;
                nj_u4 c;
                if (tones[i].freq_hz != 0u) {
                    v = ((((done + k) / half) & 1u) != 0u) ? 7000 : -7000;
                }
                for (c = 0u; c < nch; c++) {
                    chunk[k * nch + c] = v;
                }
            }
            audio_push(chunk, (int)run);
            done += run;
        }
    }
    return 0;
}

/*
 * Stop means "this sequence is over", not "discard what the host has not read
 * yet".
 *
 * On a platform with a real audio device the queue IS the playback, so the SDL2
 * port flushes it here and the sound stops exactly on time.  In the browser the
 * host pulls frames once per animation frame, while the emulated clock can
 * finish a blocking Manager.playTone() in a handful of scheduler calls (that is
 * what the RUN-phase sleep fast-forward does).  Flushing here would therefore
 * delete almost every sound effect before JavaScript ever saw it.  Keeping the
 * frames is the deliberately different, audible choice; the ring is bounded, so
 * a stopped game cannot grow it without bound.
 */
static void web_tone_stop(void *ud)
{
    (void)ud;
}

static int web_pcm_open(void *ud, int sample_rate, int channels)
{
    njw_runner *r = (njw_runner *)ud;

    if (r == NULL || sample_rate <= 0 || channels <= 0 || channels > 2) {
        return -1;
    }
    r->audio_rate = sample_rate;
    r->audio_channels = channels;
    r->audio_fill = 0;
    r->audio_head = 0;
    r->audio_scheduled = 0;
    return 0;
}

static int web_pcm_write(void *ud, const nj_s2 *samples, int frames)
{
    njw_runner *r = (njw_runner *)ud;
    int space;

    if (r == NULL || samples == NULL || frames <= 0) {
        return -1;
    }
    space = audio_space();
    if (frames > space) {
        frames = space;
    }
    audio_push(samples, frames);
    return frames;
}

/* Same reasoning as web_tone_stop(): a stopped player silences future writes;
 * the frames already queued are left for the host to play out. */
static void web_pcm_close(void *ud)
{
    (void)ud;
}

static int web_pcm_free(void *ud)
{
    (void)ud;
    return audio_space();
}

static int web_pcm_queued(void *ud)
{
    njw_runner *r = (njw_runner *)ud;

    if (r == NULL) {
        return 0;
    }
    return r->audio_fill + r->audio_scheduled;
}

/* ---- frame hook ---- */

/*
 * Called by the VM while a Java thread is parked inside a blocking native
 * (typically a constructor sleeping through a splash screen).  Pump one frame
 * so the display keeps up; nj_midp_frame() itself guards against nested paint
 * delivery, and the VM guards this callback against re-entry.
 */
static int web_frame_hook(nj_vm *vm, void *ud)
{
    (void)ud;
    if (g_runner.phase == NJW_PHASE_STOP || !g_runner.vm_usable ||
        runner_has_error()) {
        return 1;
    }
    (void)nj_midp_frame(vm);
    return nj_vm_running(vm) ? 0 : 1;
}

/* ---- runner helpers ---- */

static void runner_free_buffers(void)
{
    free(g_runner.jar);
    g_runner.jar = NULL;
    g_runner.jar_size = 0u;
    free(g_runner.fb);
    g_runner.fb = NULL;
    g_runner.fb_stride = 0u;
    free(g_runner.rgba);
    g_runner.rgba = NULL;
    free(g_runner.audio);
    g_runner.audio = NULL;
    g_runner.audio_fill = 0;
    g_runner.audio_head = 0;
    g_runner.audio_scheduled = 0;
    free(g_runner.text_copy);
    g_runner.text_copy = NULL;
    g_runner.text_copy_cap = 0;
}

/*
 * Destroy the VM (if any) with the same bounded teardown nj_run() uses, then
 * release every buffer the runner owns. Options and the recorded error are
 * left alone: caller decides whether to reset them.
 */
static void runner_release_vm(void)
{
    njw_runner *r = &g_runner;

    if (r->vm != NULL) {
        runner_capture_stop_reason();
        nj_vm_set_halt_after_ms(r->vm, 2000u);
        if (r->vm_usable) {
            (void)nj_midp_destroy_app(r->vm, 0);
        }
        nj_vm_set_halt_after_ms(r->vm, 0);
        if (r->app != NULL) {
            nj_app_close(r->vm, r->app);
            r->app = NULL;
        }
        nj_vm_destroy(r->vm);
        r->vm = NULL;
    }
    runner_free_buffers();
    r->phase = NJW_PHASE_STOP;
    r->started = 0;
    r->displayable_ready = 0;
    r->vm_usable = 0;
    r->ev_head = 0u;
    r->ev_tail = 0u;
    free(r->app_icon);
    r->app_icon = NULL;
    r->app_icon_len = 0;
}

static void runner_reset_options(void)
{
    njw_runner *r = &g_runner;

    r->opt_width = 240;
    r->opt_height = 320;
    r->opt_heap = 0u;
    r->opt_speed = 100;
    r->opt_rms_dir[0] = '\0';
    r->opt_fs_dir[0] = '\0';
    r->opt_device[0] = '\0';
    r->opt_midlet_class[0] = '\0';
    r->opt_load_state[0] = '\0';
    r->opt_load_state_set = 0;
}

static void runner_copy_str(char *dst, size_t cap, const char *src)
{
    if (cap == 0u) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    (void)snprintf(dst, cap, "%s", src);
}

/* Metadata for the library view: shared by njw_probe() and njw_start(). */
static void runner_copy_app_metadata(nj_app *app)
{
    njw_runner *r = &g_runner;
    int i;

    runner_copy_str(r->app_title, sizeof r->app_title,
                    app->midlet_name[0] != '\0' ? app->midlet_name : r->stem);
    runner_copy_str(r->app_vendor, sizeof r->app_vendor, app->jar_vendor);
    runner_copy_str(r->app_version, sizeof r->app_version, app->jar_version);
    r->app_midlet_count = nj_app_midlet_count(app);
    if (r->app_midlet_count < 0) {
        r->app_midlet_count = 0;
    }
    if (r->app_midlet_count > NJW_MAX_MIDLETS) {
        r->app_midlet_count = NJW_MAX_MIDLETS;
    }
    for (i = 0; i < r->app_midlet_count; i++) {
        const char *cls = nj_app_midlet_class(app, i);
        runner_copy_str(r->app_classes[i], sizeof r->app_classes[i],
                        cls != NULL ? cls : "");
    }
}

/* ================================================================== */
/* Life cycle                                                          */
/* ================================================================== */

static void runner_detect_device(nj_vm *vm)
{
    const nj_device *dev;

    if (g_runner.opt_device[0] == '\0') {
        return;
    }
    dev = nj_device_find(g_runner.opt_device);
    if (dev == NULL) {
        runner_set_error("unknown device profile \"%s\"", g_runner.opt_device);
        return;
    }
    nj_vm_set_device(vm, dev);
}

int njw_start(const char *jar_path)
{
    njw_runner *r = &g_runner;
    const char *class_name;
    nj_ref midlet;
    char title[96];
    int speed;

    if (jar_path == NULL || jar_path[0] == '\0') {
        runner_set_error("njw_start: empty JAR path");
        return -1;
    }
    if (r->vm != NULL) {
        runner_release_vm();
    }
    r->error[0] = '\0';
    r->stop_reason[0] = '\0';
    r->frames = 0u;
    r->epoch = 0u;
    r->drew_this_pump = 0;
    r->started = 0;
    r->vm_usable = 1;
    r->displayable_ready = 0;
    r->phase = NJW_PHASE_LOAD;
    r->clock_us = 0u;
    r->pump_sleep_calls = 0u;
    r->ev_head = 0u;
    r->ev_tail = 0u;
    r->audio_rate = 22050;
    r->audio_channels = 1;
    r->audio_fill = 0;
    r->audio_head = 0;
    r->audio_scheduled = 0;
    free(r->app_icon);
    r->app_icon = NULL;
    r->app_icon_len = 0;
    r->app_midlet_count = 0;
    r->app_title[0] = '\0';
    r->app_vendor[0] = '\0';
    r->app_version[0] = '\0';

    stem_from_path(jar_path, r->stem, sizeof r->stem);
    if (r->opt_rms_dir[0] != '\0') {
        runner_copy_str(r->rms_dir, sizeof r->rms_dir, r->opt_rms_dir);
    } else {
        (void)snprintf(r->rms_dir, sizeof r->rms_dir,
                       "/nano_j2me/saves/%s", r->stem);
    }
    if (r->opt_fs_dir[0] != '\0') {
        runner_copy_str(r->fs_dir, sizeof r->fs_dir, r->opt_fs_dir);
    } else {
        (void)snprintf(r->fs_dir, sizeof r->fs_dir,
                       "/nano_j2me/files/%s", r->stem);
    }
    if (mkdir_p(r->rms_dir) != 0) {
        runner_set_error("cannot create RMS directory %s", r->rms_dir);
        return -1;
    }
    if (mkdir_p(r->fs_dir) != 0) {
        runner_set_error("cannot create filesystem directory %s", r->fs_dir);
        return -1;
    }

    /* ---- display buffers ---- */
    r->fb_w = clampi(r->opt_width, 32, 4096);
    r->fb_h = clampi(r->opt_height, 32, 4096);
    r->fb_bpp = njw_format_bpp(NJ_PIXFMT);
    r->fb_stride = (size_t)r->fb_w * (size_t)r->fb_bpp;
    r->fb = (unsigned char *)calloc(1u, r->fb_stride * (size_t)r->fb_h);
    r->rgba = (unsigned char *)calloc(1u,
                                      (size_t)r->fb_w * (size_t)r->fb_h * 4u);
    if (r->fb == NULL || r->rgba == NULL) {
        runner_set_error("out of memory for a %dx%d display",
                         r->fb_w, r->fb_h);
        runner_release_vm();
        return -1;
    }

    /* ---- audio ring ---- */
    r->audio = (nj_s2 *)calloc((size_t)NJW_AUDIO_RING_FRAMES * 2u,
                               sizeof(nj_s2));
    if (r->audio == NULL) {
        runner_set_error("out of memory for the audio queue");
        runner_release_vm();
        return -1;
    }

    /* ---- JAR ---- */
    r->jar = read_whole_file(jar_path, &r->jar_size);
    if (r->jar == NULL) {
        runner_set_error("cannot read JAR %s", jar_path);
        runner_release_vm();
        return -1;
    }

    /* ---- the port ---- */
    memset(&r->port, 0, sizeof r->port);
    r->port.name = "web";
    r->port.ud = r;
    r->port.millis = web_port_millis;
    r->port.display_init = web_display_init;
    r->port.display_present = web_display_present;
    r->port.input_poll = web_input_poll;
    r->port.input_push = web_input_push;
    r->port.alloc = web_port_alloc;
    r->port.free = web_port_free;
    r->port.idle = web_port_idle;
    r->port.sleep_ms = web_port_sleep_ms;
    r->port.log = web_port_log;
    r->port.now = web_port_now;
    r->port.device_property = web_device_property;
    r->port.tone_play = web_tone_play;
    r->port.tone_stop = web_tone_stop;
    r->port.pcm_open = web_pcm_open;
    r->port.pcm_write = web_pcm_write;
    r->port.pcm_close = web_pcm_close;
    r->port.pcm_free = web_pcm_free;
    r->port.pcm_queued = web_pcm_queued;
    r->port.text_input = web_text_input;
    r->port.width = r->fb_w;
    r->port.height = r->fb_h;
    r->port.fps_cap = 0;                       /* host-owned clock */
    r->port.rms_dir = r->rms_dir;
    r->port.fs_dir = r->fs_dir;

    /* ---- the VM ---- */
    {
        nj_u4 heap = r->opt_heap;
        if (heap == 0u) {
            heap = (nj_u4)NJ_MAX_HEAP;
        }
        heap &= ~(nj_u4)(NJ_OBJ_ALIGN - 1u);
        if (heap < 1024u * 1024u) {
            heap = 1024u * 1024u;
        }
        r->vm = nj_vm_create_with_heap_limit(&r->port, (nj_size)heap, 0u);
        if (r->vm == NULL) {
            runner_set_error("nj_vm_create failed (heap %u bytes)",
                             (unsigned)heap);
            runner_release_vm();
            return -1;
        }
    }
    if (nj_vm_set_frame_hook(r->vm, web_frame_hook, r) != 0) {
        runner_set_error("nj_vm_set_frame_hook failed");
        runner_release_vm();
        return -1;
    }
    runner_detect_device(r->vm);
    if (runner_has_error()) {
        runner_release_vm();
        return -1;
    }
    /* The host owns the frame cadence; keep the internal pacer out of the way
     * (nj_vm.h documents exactly this for frontend-driven clocks). */
    nj_vm_set_frame_cap(r->vm, 0);
    r->app = nj_app_open(r->vm, r->jar, (nj_u4)r->jar_size);
    if (r->app == NULL) {
        if (nj_has_exception(r->vm)) {
            runner_set_error("cannot open JAR: %s",
                             nj_exception_describe(r->vm));
        } else {
            runner_set_error("cannot open JAR %s", jar_path);
        }
        runner_release_vm();
        return -1;
    }
    runner_copy_app_metadata(r->app);
    class_name = (r->opt_midlet_class[0] != '\0')
                     ? r->opt_midlet_class
                     : nj_app_midlet_class(r->app, 0);
    if (class_name == NULL || class_name[0] == '\0') {
        runner_set_error("the JAR has no MIDlet class in its manifest");
        runner_release_vm();
        return -1;
    }
    (void)snprintf(title, sizeof title, "%s", r->app_title);
    fprintf(stderr, "[nj2me] running %s (%s %s)\n", title,
            r->app_vendor[0] != '\0' ? r->app_vendor : "?",
            r->app_version[0] != '\0' ? r->app_version : "?");

    /* ---- an optional snapshot, before the constructor (nj_run order) ---- */
    if (r->opt_load_state_set && r->opt_load_state[0] != '\0') {
        if (nj_vm_load_file(r->vm, r->opt_load_state) == NJ_OK) {
            fprintf(stderr, "[nj2me] resumed %s from %s\n", class_name,
                    r->opt_load_state);
            r->phase = NJW_PHASE_RUN;
            r->started = 1;
            r->start_us = r->clock_us;
            r->displayable_ready = (nj_midp_current(r->vm) != NULL);
            speed = clampi(r->opt_speed, 10, 800);
            nj_vm_set_speed(r->vm, speed);
            return 0;
        }
        /* A refused load installs nothing; start the MIDlet normally. */
        fprintf(stderr,
                "[nj2me] cannot resume from %s; starting the MIDlet instead\n",
                r->opt_load_state);
    }

    /* ---- construct the MIDlet ---- */
    midlet = nj_vm_new_midlet(r->vm, class_name, NULL);
    if (midlet == NULL && !nj_vm_running(r->vm)) {
        /* The constructor consumed the whole run (a title that runs its game
         * loop inside the constructor).  Not a start failure. */
        r->phase = NJW_PHASE_RUN;
        r->started = 1;
        r->start_us = r->clock_us;
        runner_capture_stop_reason();
        return 0;
    }
    if (midlet == NULL || runner_has_error()) {
        if (nj_has_exception(r->vm)) {
            runner_set_error("the MIDlet constructor threw %s",
                             nj_exception_describe(r->vm));
        } else if (!runner_has_error()) {
            runner_set_error("cannot instantiate MIDlet %s", class_name);
        }
        runner_release_vm();
        return -1;
    }

    /* startApp() is queued, not executed: the host's frames drive it. */
    r->phase = NJW_PHASE_RUN;
    r->started = 1;
    r->start_us = r->clock_us;
    r->displayable_ready = (nj_midp_current(r->vm) != NULL);
    if (!r->displayable_ready &&
        nj_vm_start_app_async(r->vm, midlet) != NJ_OK) {
        runner_set_error("nj_vm_start_app_async failed");
        runner_release_vm();
        return -1;
    }
    speed = clampi(r->opt_speed, 10, 800);
    nj_vm_set_speed(r->vm, speed);
    return 0;
}

int njw_frame(double dt_ms)
{
    njw_runner *r = &g_runner;
    nj_u8 dt_us;
    unsigned i;
    unsigned pumps;
    int flags;
    nj_status st;

    if (r->vm == NULL || !r->started) {
        if (!runner_has_error()) {
            runner_set_error("njw_frame: no VM is running");
        }
        return -1;
    }
    if (runner_has_error() || !r->vm_usable) {
        return -1;
    }

    dt_us = (dt_ms > 0.0) ? (nj_u8)(dt_ms * 1000.0) : NJW_NOMINAL_TICK_US;
    if (dt_us > NJW_MAX_DT_US) {
        dt_us = NJW_MAX_DT_US;
    }
    /* While startApp() is still loading, tick at least once per nominal frame
     * so a loaded game becomes playable in roughly real time even on a host
     * that renders slowly. */
    if (!r->displayable_ready && dt_us < NJW_NOMINAL_TICK_US) {
        dt_us = NJW_NOMINAL_TICK_US;
    }
    r->clock_us += dt_us;
    r->pump_sleep_calls = 0u;
    r->drew_this_pump = 0;

    pumps = r->displayable_ready ? 1u : NJW_LOAD_PUMPS;
    for (i = 0u; i < pumps; i++) {
        st = nj_midp_frame(r->vm);
        r->frames++;
        if (!r->displayable_ready && nj_midp_current(r->vm) != NULL) {
            r->displayable_ready = 1;
        }
        if (!nj_vm_running(r->vm)) {
            runner_capture_stop_reason();
            break;
        }
        if (st != NJ_OK) {
            runner_set_error("the display loop failed (status %d)", (int)st);
            break;
        }
        if (r->displayable_ready) {
            break;                        /* the first frame of play is enough */
        }
    }
    if (runner_has_error()) {
        return -1;
    }

    /* A MIDlet that never installs a Displayable would leave the shell on its
     * loading screen forever. nj_run() gives up after 60 frames; the virtual
     * clock makes that a fair budget here too, at 20 emulated seconds. */
    if (nj_vm_running(r->vm) && !r->displayable_ready &&
        r->clock_us - r->start_us > NJW_DISPLAYABLE_TIMEOUT_US) {
        nj_vm_stop_reason(r->vm, 0, "the MIDlet installed no Displayable");
        r->stop_reason[0] = '\0';
        runner_capture_stop_reason();
    }

    flags = 0;
    if (r->vm != NULL && nj_vm_running(r->vm)) {
        flags |= NJW_FRAME_RUNNING;
        if (!r->displayable_ready) {
            flags |= NJW_FRAME_LOADING;
        }
    }
    if (r->drew_this_pump) {
        flags |= NJW_FRAME_DREW;
    }
    return flags;
}

int njw_running(void)
{
    njw_runner *r = &g_runner;

    if (r->vm == NULL || !r->started || !r->vm_usable ||
        runner_has_error()) {
        return 0;
    }
    return nj_vm_running(r->vm) ? 1 : 0;
}

int njw_displayable_ready(void)
{
    return g_runner.displayable_ready ? 1 : 0;
}

void njw_stop(void)
{
    njw_runner *r = &g_runner;

    if (r->vm != NULL) {
        runner_capture_stop_reason();
    }
    runner_release_vm();
    runner_reset_options();
}

const char *njw_error(void)
{
    return g_runner.error;
}

const char *njw_stop_reason(void)
{
    return g_runner.stop_reason;
}

unsigned njw_frames(void)
{
    return g_runner.frames;
}

/* ================================================================== */
/* Framebuffer                                                         */
/* ================================================================== */

const unsigned char *njw_framebuffer(void)
{
    if (g_runner.vm == NULL || g_runner.rgba == NULL) {
        return NULL;
    }
    return g_runner.rgba;
}

int njw_width(void)
{
    return g_runner.fb_w;
}

int njw_height(void)
{
    return g_runner.fb_h;
}

unsigned njw_frame_epoch(void)
{
    return g_runner.epoch;
}

/* ================================================================== */
/* Input                                                               */
/* ================================================================== */

void njw_key(int key, int down)
{
    nj_event ev;

    if (key <= 0 || key >= NJ_KEY_COUNT) {
        return;
    }
    memset(&ev, 0, sizeof ev);
    ev.type = down ? NJ_EV_KEY_DOWN : NJ_EV_KEY_UP;
    ev.key = (nj_u1)key;
    (void)runner_queue_event(&ev);
}

void njw_pointer(int action, int x, int y)
{
    nj_event ev;

    if (x < -32768) {
        x = -32768;
    }
    if (x > 32767) {
        x = 32767;
    }
    if (y < -32768) {
        y = -32768;
    }
    if (y > 32767) {
        y = 32767;
    }
    memset(&ev, 0, sizeof ev);
    switch (action) {
    case NJW_POINTER_DOWN: ev.type = NJ_EV_POINTER_DOWN; break;
    case NJW_POINTER_UP:   ev.type = NJ_EV_POINTER_UP;   break;
    default:               ev.type = NJ_EV_POINTER_MOVE; break;
    }
    ev.x = (nj_s2)x;
    ev.y = (nj_s2)y;
    (void)runner_queue_event(&ev);
}

void njw_text_input(const char *utf8)
{
    nj_event ev;
    njw_runner *r = &g_runner;

    if (utf8 == NULL || !r->text_active || r->text_session == 0u) {
        /* The runtime only accepts text for the focused native editor and
         * ignores events with no session; reproduce that here instead of
         * queueing something that will be dropped. */
        return;
    }
    memset(&ev, 0, sizeof ev);
    ev.type = NJ_EV_TEXT_INPUT;
    ev.text_session = r->text_session;
    (void)snprintf(ev.text, sizeof ev.text, "%s", utf8);
    (void)runner_queue_event(&ev);
}

void njw_text_edit(int edit)
{
    nj_event ev;
    njw_runner *r = &g_runner;

    if (!r->text_active || r->text_session == 0u || edit <= 0 || edit > 6) {
        return;
    }
    memset(&ev, 0, sizeof ev);
    ev.type = NJ_EV_TEXT_EDIT;
    ev.key = (nj_u1)edit;
    ev.text_session = r->text_session;
    (void)runner_queue_event(&ev);
}

void njw_text_replace(const char *utf8, int cursor)
{
    nj_event ev;
    njw_runner *r = &g_runner;
    int units;

    if (!r->text_active || r->text_session == 0u) {
        return;
    }
    units = utf8_to_utf16(utf8, r->replace_text[r->replace_slot],
                          NJW_REPLACE_UNITS);
    if (units < 0) {
        return;                            /* longer than the transfer buffer */
    }
    if (cursor < 0 || cursor > units) {
        cursor = units;
    }
    memset(&ev, 0, sizeof ev);
    ev.type = NJ_EV_TEXT_REPLACE;
    ev.text_session = r->text_session;
    ev.text_sequence = ++r->replace_sequence;
    ev.text_utf16 = r->replace_text[r->replace_slot];
    ev.text_length = units;
    ev.text_cursor = cursor;
    if (runner_queue_event(&ev)) {
        r->replace_slot = (r->replace_slot + 1u) % NJW_REPLACE_SLOTS;
    }
}

int njw_text_active(void)
{
    return g_runner.text_active ? 1 : 0;
}

int njw_text_current(char *out, int cap)
{
    njw_runner *r = &g_runner;

    if (!r->text_active || r->text_copy == NULL || r->text_length <= 0) {
        if (out != NULL && cap > 0) {
            out[0] = '\0';
        }
        return 0;
    }
    return utf16_to_utf8(r->text_copy, r->text_length, out, cap);
}

void njw_text_state(int *active, int *multiline, unsigned *constraints,
                    int *max_length, int *length, int *cursor)
{
    njw_runner *r = &g_runner;

    if (active != NULL) {
        *active = r->text_active;
    }
    if (multiline != NULL) {
        *multiline = r->text_multiline;
    }
    if (constraints != NULL) {
        *constraints = r->text_constraints;
    }
    if (max_length != NULL) {
        *max_length = r->text_max_length;
    }
    if (length != NULL) {
        *length = r->text_length;
    }
    if (cursor != NULL) {
        *cursor = r->text_cursor;
    }
}

/* ================================================================== */
/* Audio pull                                                          */
/* ================================================================== */

int njw_audio_available(void)
{
    return g_runner.audio_fill;
}

int njw_audio_rate(void)
{
    return g_runner.audio_rate > 0 ? g_runner.audio_rate : 22050;
}

int njw_audio_channels(void)
{
    return g_runner.audio_channels > 0 ? g_runner.audio_channels : 1;
}

int njw_audio_read(short *dst, int max_frames)
{
    njw_runner *r = &g_runner;
    int chans;
    int frames;
    short *out = dst;

    if (r->audio == NULL || dst == NULL || max_frames <= 0) {
        return 0;
    }
    chans = r->audio_channels > 0 ? r->audio_channels : 1;
    frames = max_frames;
    if (frames > r->audio_fill) {
        frames = r->audio_fill;
    }
    if (frames <= 0) {
        return 0;
    }
    {
        int left = frames;
        while (left > 0) {
            int run = NJW_AUDIO_RING_FRAMES - r->audio_head;
            if (run > left) {
                run = left;
            }
            memcpy(out, r->audio + (size_t)r->audio_head * (size_t)chans,
                   (size_t)run * (size_t)chans * sizeof(nj_s2));
            out += (size_t)run * (size_t)chans;
            r->audio_head = (r->audio_head + run) % NJW_AUDIO_RING_FRAMES;
            r->audio_fill -= run;
            left -= run;
        }
    }
    return frames;
}

void njw_audio_note_playing(int frames)
{
    if (frames < 0) {
        frames = 0;
    }
    g_runner.audio_scheduled = frames;
}

/* ================================================================== */
/* Options                                                             */
/* ================================================================== */

void njw_options_reset(void)
{
    runner_reset_options();
}

int njw_option_int(int option, int value)
{
    switch (option) {
    case NJW_OPT_SCREEN_WIDTH:
        g_runner.opt_width = clampi(value, 32, 4096);
        return 0;
    case NJW_OPT_SCREEN_HEIGHT:
        g_runner.opt_height = clampi(value, 32, 4096);
        return 0;
    case NJW_OPT_HEAP_BYTES:
        g_runner.opt_heap = (value > 0) ? (nj_u4)value : 0u;
        return 0;
    case NJW_OPT_SPEED_PERCENT:
        g_runner.opt_speed = clampi(value, 10, 800);
        return 0;
    default:
        return -1;
    }
}

int njw_option_string(int option, const char *utf8)
{
    switch (option) {
    case NJW_OPT_RMS_DIR:
        runner_copy_str(g_runner.opt_rms_dir,
                        sizeof g_runner.opt_rms_dir, utf8);
        return 0;
    case NJW_OPT_FS_DIR:
        runner_copy_str(g_runner.opt_fs_dir,
                        sizeof g_runner.opt_fs_dir, utf8);
        return 0;
    case NJW_OPT_DEVICE:
        runner_copy_str(g_runner.opt_device, sizeof g_runner.opt_device, utf8);
        return 0;
    case NJW_OPT_MIDLET_CLASS:
        runner_copy_str(g_runner.opt_midlet_class,
                        sizeof g_runner.opt_midlet_class, utf8);
        return 0;
    case NJW_OPT_LOAD_STATE_PATH:
        runner_copy_str(g_runner.opt_load_state,
                        sizeof g_runner.opt_load_state, utf8);
        g_runner.opt_load_state_set =
            (g_runner.opt_load_state[0] != '\0');
        return 0;
    default:
        return -1;
    }
}

/* ================================================================== */
/* Probe / library metadata                                            */
/* ================================================================== */

int njw_probe(const char *jar_path)
{
    njw_runner *r = &g_runner;
    nj_port port;
    nj_vm *vm = NULL;
    nj_app *app = NULL;
    unsigned char *jar = NULL;
    nj_u4 jar_size = 0u;
    char *icon_name;
    int ok = 0;

    if (jar_path == NULL || jar_path[0] == '\0') {
        runner_set_error("njw_probe: empty JAR path");
        return -1;
    }
    free(r->app_icon);
    r->app_icon = NULL;
    r->app_icon_len = 0;
    r->app_midlet_count = 0;
    r->app_title[0] = '\0';
    r->app_vendor[0] = '\0';
    r->app_version[0] = '\0';
    stem_from_path(jar_path, r->stem, sizeof r->stem);

    jar = read_whole_file(jar_path, &jar_size);
    if (jar == NULL) {
        runner_set_error("njw_probe: cannot read %s", jar_path);
        return -1;
    }
    /* A throwaway VM: the runtime needs one to raise Java errors for a bad
     * archive, and to own the zip reader's allocations. */
    memset(&port, 0, sizeof port);
    port.name = "web-probe";
    port.ud = r;
    port.millis = web_port_millis;
    port.alloc = web_port_alloc;
    port.free = web_port_free;
    port.log = web_port_log;
    vm = nj_vm_create_with_heap_limit(&port, 4u * 1024u * 1024u, 0u);
    if (vm == NULL) {
        runner_set_error("njw_probe: cannot create a probe VM");
        free(jar);
        return -1;
    }
    app = nj_app_open(vm, jar, jar_size);
    if (app == NULL) {
        runner_set_error("njw_probe: %s: %s", jar_path,
                         nj_has_exception(vm) ? nj_exception_describe(vm)
                                              : "not a MIDlet JAR");
        goto done;
    }
    runner_copy_app_metadata(app);
    /* The icon is extracted here rather than through nj_app_resource(): the
     * runtime's returned buffer is freed with the internal nj_host_free(),
     * which a port must not call.  nj_zip is public API and happily works on
     * a plain malloc'd copy of the JAR. */
    icon_name = app->midlet_icon;
    if (icon_name[0] != '\0') {
        nj_zip *zip = nj_zip_open(jar, jar_size, NULL);
        if (zip != NULL) {
            const nj_zip_entry *entry;
            const char *n = icon_name;

            /* MIDlet-1 icon paths are resource names, and the common form
             * starts with '/'.  ZIP entries never do: nj_app_resource() and
             * nj_library's icon lookup both strip one leading slash and then
             * fall back to a case-insensitive match; mirror that here. */
            if (*n == '/') {
                n++;
            }
            entry = nj_zip_find(zip, n);
            if (entry == NULL) {
                entry = nj_zip_find_ci(zip, n);
            }
            nj_u4 icon_len = 0u;
            void *icon = NULL;

            if (entry != NULL && entry->uncomp_size > 0u &&
                entry->uncomp_size <= NJW_MAX_ICON_BYTES) {
                icon = nj_zip_extract_alloc(zip, entry, NULL, &icon_len);
            }
            if (icon != NULL && icon_len > 0u &&
                icon_len <= NJW_MAX_ICON_BYTES) {
                r->app_icon = (unsigned char *)malloc((size_t)icon_len);
                if (r->app_icon != NULL) {
                    memcpy(r->app_icon, icon, (size_t)icon_len);
                    r->app_icon_len = (int)icon_len;
                }
            }
            free(icon);
            nj_zip_close(zip);
        }
    }
    ok = 1;
done:
    if (app != NULL) {
        nj_app_close(vm, app);
    }
    nj_vm_destroy(vm);
    free(jar);
    return ok ? 0 : -1;
}

const char *njw_app_title(void)
{
    return g_runner.app_title;
}

const char *njw_app_vendor(void)
{
    return g_runner.app_vendor;
}

const char *njw_app_version(void)
{
    return g_runner.app_version;
}

int njw_midlet_count(void)
{
    return g_runner.app_midlet_count;
}

const char *njw_midlet_class_at(int index)
{
    if (index < 0 || index >= g_runner.app_midlet_count) {
        return "";
    }
    return g_runner.app_classes[index];
}

const unsigned char *njw_app_icon(int *len)
{
    if (len != NULL) {
        *len = g_runner.app_icon != NULL ? g_runner.app_icon_len : 0;
    }
    return g_runner.app_icon;
}

/* ================================================================== */
/* Save states                                                         */
/* ================================================================== */

int njw_can_save(void)
{
    njw_runner *r = &g_runner;

    if (r->vm == NULL || !r->vm_usable) {
        return 0;
    }
    return nj_vm_can_save(r->vm) ? 1 : 0;
}

int njw_save_state(void **blob, int *len)
{
    njw_runner *r = &g_runner;
    nj_u1 *buf = NULL;
    nj_size size = 0u;

    if (blob != NULL) {
        *blob = NULL;
    }
    if (len != NULL) {
        *len = 0;
    }
    if (r->vm == NULL || !r->vm_usable) {
        runner_set_error("njw_save_state: no usable VM");
        return -1;
    }
    if (nj_vm_save_mem(r->vm, &buf, &size) != NJ_OK) {
        runner_set_error("njw_save_state: the VM refused to serialize");
        return -1;
    }
    if (blob != NULL) {
        *blob = buf;
    } else {
        free(buf);                             /* plain malloc, see header */
    }
    if (len != NULL) {
        *len = (int)size;
    }
    return 0;
}

int njw_load_state(const void *blob, int len)
{
    njw_runner *r = &g_runner;

    if (r->vm == NULL || !r->vm_usable) {
        runner_set_error("njw_load_state: no usable VM");
        return -1;
    }
    if (blob == NULL || len <= 0) {
        runner_set_error("njw_load_state: empty state blob");
        return -1;
    }
    if (nj_vm_load_mem(r->vm, (const nj_u1 *)blob, (nj_size)len) != NJ_OK) {
        /* The runtime documents that a VM which refuses a restore is not
         * usable afterwards: the only recovery is a fresh njw_start(). */
        r->vm_usable = 0;
        runner_set_error("njw_load_state: the state does not match this JAR "
                         "or was corrupted; restart the game");
        return -1;
    }
    r->displayable_ready = (nj_midp_current(r->vm) != NULL);
    runner_capture_stop_reason();
    return 0;
}

void *njw_alloc(int bytes)
{
    void *p = NULL;

    if (bytes <= 0) {
        return NULL;
    }
    if (posix_memalign(&p, (size_t)NJ_OBJ_ALIGN, (size_t)bytes) != 0) {
        return NULL;
    }
    return p;
}

void njw_free(void *ptr)
{
    free(ptr);
}

/* ================================================================== */
/* Identity and tuning                                                 */
/* ================================================================== */

int njw_api_version(void)
{
    return NJW_API_VERSION;
}

const char *njw_runtime_version(void)
{
    return NJ_VERSION_STRING;
}

void njw_set_speed(int percent)
{
    njw_runner *r = &g_runner;

    percent = clampi(percent, 10, 800);
    r->opt_speed = percent;
    if (r->vm != NULL && r->vm_usable) {
        nj_vm_set_speed(r->vm, percent);
    }
}

int njw_speed(void)
{
    if (g_runner.vm != NULL && g_runner.vm_usable) {
        return nj_vm_speed(g_runner.vm);
    }
    return g_runner.opt_speed;
}

/* ================================================================== */
/* Module entry                                                        */
/* ================================================================== */
/*
 * The module is created with -sINVOKE_RUN=0 (see CMakeLists.txt): main() is
 * linked but never called, so the Emscripten FS and the runner live as long as
 * the JS module does, and every frame comes from njw_frame().
 */
int main(void)
{
    return 0;
}
