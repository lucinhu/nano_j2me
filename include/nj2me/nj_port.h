/*
 * nano_j2me - nj_port.h
 *
 * ============================================================================
 *  THE PORTING INTERFACE
 * ============================================================================
 *
 * Porting nano_j2me to a new platform means implementing the callbacks in
 * `nj_port` (about 10 of them are mandatory) and calling nj_run() with it.
 * Nothing else in the runtime talks to the operating system.
 *
 *   Linux / macOS / Windows ....... src/port/posix/nj_port_sdl2.c   (reference)
 *   Linux headless / CI ........... src/port/posix/nj_port_headless.c
 *   Android (NDK) ................. src/port/android/nj_port_android.c
 *   ESP32-S3 (ESP-IDF) ............ src/port/esp32/nj_port_esp32.c
 *
 * The framebuffer contract is deliberately dumb: the port exposes a linear
 * buffer that the runtime rasterizes into, plus a `present` callback telling
 * the port which rectangle changed.  That is all a SPI LCD, an ANativeWindow
 * or an SDL texture needs.
 *
 * ============================================================================
 *  MEMORY CONTRACT - NO MUTABLE GLOBAL STATE
 * ============================================================================
 * The runtime holds *zero* mutable static/global variables.  Every byte of
 * runtime state lives in `nj_vm`, which is created by nj_vm_create() and
 * completely released by nj_vm_destroy().  Consequences, all of which are
 * enforced by tests/ (see tools/check_no_statics.sh):
 *
 *   - nj_vm_destroy() returns the process to a zero-live-allocation state;
 *     a repeated create/run/destroy loop must show neither growth nor drift.
 *   - Several nj_vm instances may exist at once (no hidden singletons).
 *   - The only file-scope data in the runtime is `const` (native method
 *     tables, font bitmaps, opcode names).  It is immutable and normally
 *     placed in .rodata / flash by the linker, so it costs no RAM.  Mutable
 *     `static` variables are forbidden anywhere in src/.
 *   - Ports follow the same rule: their state lives in a struct the caller
 *     owns and passes through `nj_port.ud`.  The built-in ports therefore
 *     *fill in* a caller-provided nj_port rather than returning a pointer to
 *     a shared static instance.
 */
#ifndef NJ_PORT_H
#define NJ_PORT_H

#include "nj_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration so the port contract can mention the VM without
 * pulling the whole VM API into every port. */
struct nj_vm;

/* ------------------------------------------------------------------ */
/* Input events                                                        */
/* ------------------------------------------------------------------ */
typedef enum nj_event_type {
    NJ_EV_NONE = 0,
    NJ_EV_KEY_DOWN,
    NJ_EV_KEY_UP,
    NJ_EV_POINTER_DOWN,
    NJ_EV_POINTER_UP,
    NJ_EV_POINTER_MOVE,
    NJ_EV_QUIT          /* window closed / power off request */
} nj_event_type;

/*
 * Canonical key codes.  Ports translate their own scancodes into these; the
 * runtime maps them onto MIDP key codes (Canvas.KEY_NUM0..KEY_NUM9, KEY_STAR,
 * KEY_POUND, KEY_UP/DOWN/LEFT/RIGHT/FIRE, and the game actions
 * UP/DOWN/LEFT/RIGHT/FIRE/A/B/C/D) via nj_input_keymap().
 */
typedef enum nj_key {
    NJ_KEY_NONE = 0,
    NJ_KEY_0, NJ_KEY_1, NJ_KEY_2, NJ_KEY_3, NJ_KEY_4,
    NJ_KEY_5, NJ_KEY_6, NJ_KEY_7, NJ_KEY_8, NJ_KEY_9,
    NJ_KEY_STAR, NJ_KEY_POUND,
    NJ_KEY_UP, NJ_KEY_DOWN, NJ_KEY_LEFT, NJ_KEY_RIGHT, NJ_KEY_FIRE,
    NJ_KEY_SOFT_LEFT, NJ_KEY_SOFT_RIGHT, NJ_KEY_CLEAR,
    NJ_KEY_GAME_A, NJ_KEY_GAME_B, NJ_KEY_GAME_C, NJ_KEY_GAME_D,
    NJ_KEY_VOLUP, NJ_KEY_VOLDOWN, NJ_KEY_POWER,
    NJ_KEY_COUNT
} nj_key;

typedef struct nj_event {
    nj_u1 type;   /* nj_event_type */
    nj_u1 key;    /* nj_key, for key/pointer events */
    nj_s2 x, y;   /* pointer coordinates in *device* pixels */
} nj_event;

/* ------------------------------------------------------------------ */
/* Time-of-day / calendar (optional; used by java.util.Date/Calendar)  */
/* ------------------------------------------------------------------ */
typedef struct nj_datetime {
    nj_s4 year;        /* full year, e.g. 2026            */
    nj_s4 month;       /* 0..11                           */
    nj_s4 day;         /* 1..31                           */
    nj_s4 hour;        /* 0..23                           */
    nj_s4 minute;
    nj_s4 second;
    nj_s4 millis;
    nj_s4 weekday;     /* 0=Sunday .. 6=Saturday          */
    nj_s4 tz_offset;   /* minutes east of UTC             */
} nj_datetime;

/* ------------------------------------------------------------------ */
/* Persistence (used by RecordStore and by --rms-dir)                  */
/* ------------------------------------------------------------------ */
typedef struct nj_file nj_file;

struct nj_file {
    void  *handle;     /* port private */
    nj_u4  size;
    nj_u4  pos;
};

/* ------------------------------------------------------------------ */
/* Audio (optional, used by javax.microedition.media tone Player)      */
/* ------------------------------------------------------------------ */
typedef struct nj_audio_tone {
    nj_u2 freq_hz;
    nj_u2 duration_ms;
} nj_audio_tone;

/* ------------------------------------------------------------------ */
/* The port vtable                                                     */
/* ------------------------------------------------------------------ */
typedef struct nj_port {
    const char *name;

    /* ---- state owned by the port itself ----
     * Every callback except `log` receives this pointer.  A built-in port sets
     * it in its constructor; a custom port typically points it at a struct it
     * allocated in its own init. */
    void *ud;

    /* ---- state owned by the embedder ----
     * Passed to the `log` callback so an embedder can capture the MIDlet's
     * System.out without having to know anything about the port's internals.
     * NULL is fine. */
    void *user;

    /* ================= MANDATORY ================= */

    /* Millisecond monotonic clock. */
    nj_u4 (*millis)(void *ud);

    /* Raster target.
     * The port fills `pixels` with a pointer to a linear buffer of at least
     * width*height*bytes_per_pixel, `stride` in bytes, and returns the pixel
     * format (NJ_PIXFMT_*).  Called once at startup. */
    int   (*display_init)(void *ud, int *width, int *height, int *stride,
                          void **pixels);

    /* Push a changed rectangle to the panel.  If `rect` is NULL, present all.
     * May be called every frame; ports that can DMA should use `rect`. */
    void  (*display_present)(void *ud, int x, int y, int w, int h);

    /* Non-blocking input poll.  Returns 1 and fills `ev`, or 0 if empty. */
    int   (*input_poll)(void *ud, nj_event *ev);

    /*
     * Inject a synthetic event.  Returns 1 if taken, 0 if the port has no way
     * to accept one (leave it NULL then).
     *
     * This is what lets a harness DRIVE a game instead of only watching it.
     * Without it a corpus can prove a title screen draws and nothing more,
     * which is a weak claim about a game whose first frame is a splash.
     */
    int   (*input_push)(void *ud, const nj_event *ev);

    /* ================= OPTIONAL (may be NULL) ================= */

    /*
     * Host memory.  When NULL the runtime uses malloc/free (or its own
     *
     * ALIGNMENT IS PART OF THE CONTRACT: every block returned here must be
     * aligned to NJ_OBJ_ALIGN (16).  The Java heap keeps long and double fields
     * in the arena, so nj_heap_init() rejects an arena whose base is not
     * 16-byte aligned - and because it can only reject, a port that aligns to 8
     * fails with "nj_vm_create: heap failed" and no hint as to why.  A bump
     * allocator over a static array must round each request up to 16, not 8.
          * static pools when NJ_USE_LIBC is 0). */
    void *(*alloc)(void *ud, nj_size n);
    void  (*free)(void *ud, void *p);

    /* Cooperative yield so the host RTOS can run its own tasks.  Called by
     * the scheduler every NJ_SCHED_QUANTUM bytecodes.  On ESP-IDF this is
     * typically vTaskDelay(1). */
    void  (*idle)(void *ud);

    void  (*sleep_ms)(void *ud, nj_u4 ms);

    /* Logging.  level: 0=error 1=warn 2=info 3=debug */
    void  (*log)(void *ud, int level, const char *msg);

    /* Local wall clock.  Return 1 on success. */
    int   (*now)(void *ud, nj_datetime *out);

    /* ---- persistent storage: RMS + JAR-from-flash ---- */
    int   (*file_open)(void *ud, const char *name, const char *mode, nj_file *out);
    nj_s4 (*file_read)(void *ud, nj_file *f, void *buf, nj_u4 n);
    nj_s4 (*file_write)(void *ud, nj_file *f, const void *buf, nj_u4 n);
    int   (*file_seek)(void *ud, nj_file *f, nj_u4 pos);
    void  (*file_close)(void *ud, nj_file *f);
    int   (*file_remove)(void *ud, const char *name);
    int   (*file_exists)(void *ud, const char *name);

    /* ---- directory iteration (javax.microedition.io.file) ---- */
    /*
     * Opening, walking and closing a directory.  A port that has no notion of
     * directories leaves all three NULL and FileConnection.list() throws
     * instead of pretending the directory is empty - an empty list and a
     * filesystem that cannot be read are different answers and a game that
     * shows a file picker needs to tell them apart.
     *
     * `path` is host-relative, already resolved against fs_dir by the runtime.
     * dir_next fills `name` with the entry's BASE NAME, not its path, and sets
     * *is_dir; it returns 1 for an entry, 0 at the end, negative on error.
     */
    int   (*dir_open)(void *ud, const char *path, void **out_handle);
    int   (*dir_next)(void *ud, void *handle, char *name, nj_size cap,
                      int *is_dir);
    void  (*dir_close)(void *ud, void *handle);

    /* ---- optional inline JAR payload (ESP32: JAR flashed into a partition
     * or embedded as a C array).  When this returns non-NULL the runtime
     * loads the MIDlet directly from memory and ignores `jar_path`. ---- */
    const void *(*app_blob)(void *ud, nj_u4 *size);

    /* ---- platform image codecs ----
     * Called once, right after the VM is created and before the built-in
     * codecs are registered, so anything installed here takes precedence.
     * Use nj_codec_register(vm, &my_codec, NJ_CODEC_PRIO_PLATFORM).
     *
     * This is how a board's hardware JPEG decoder, a mask-ROM PNG codec or
     * Android's BitmapFactory gets wired in without touching the runtime.
     * See include/nj2me/nj_codec.h. */
    void (*install_codecs)(void *ud, struct nj_vm *vm);

    /* ---- audio ---- */
    /*
     * Start a tone sequence.  `tones` is an array of `count` entries, each a
     * frequency and a duration; a frequency of 0 is a rest.  Returns 0 if the
     * port took it, negative if it has no audio.
     *
     * A port with a real device queues all of them and returns; the runtime
     * does not block either way.  Manager.playTone(TONE_BLOCKING) blocks the
     * CALLING JAVA THREAD on an emulated timer, not on the port, so a port that
     * returns early is not a port that is wrong.
     */
    int   (*tone_play)(void *ud, const nj_audio_tone *tones, int count);
    void  (*tone_stop)(void *ud);

    /*
     * Streaming PCM, for sampled audio (WAV).  Open once with the format, push
     * frames, close.  Samples are signed 16-bit little-endian, interleaved for
     * stereo.
     *
     * pcm_write returns the number of FRAMES accepted, which may be fewer than
     * offered when the device buffer is full - the runtime holds the rest and
     * retries on the next frame, so a port that always returns 0 slows playback
     * down instead of losing it.  A port without audio may leave all three NULL
     * and sampled audio simply does not play.
     */
    int   (*pcm_open)(void *ud, int sample_rate, int channels);
    int   (*pcm_write)(void *ud, const nj_s2 *samples, int frames);
    void  (*pcm_close)(void *ud);
    /* Frames the device can accept right now; 0 or NULL means "as many as you
     * like".  Used to avoid holding a whole WAV in memory on a small part. */
    int   (*pcm_free)(void *ud);

    /* ---- executable memory (for a JIT; unused by the interpreter) ---- */
    /*
     * These are NOT the same request as alloc() above.
     *
     * alloc() gives memory the CPU may read and write.  On every platform this
     * project targets that memory may NOT be executed: it is NX, or it lives in
     * a region the instruction fetcher cannot reach.  A JIT therefore cannot
     * put generated code in it, and there is no way to express "give me memory
     * I can execute" through the existing hooks.
     *
     * NOTHING CALLS THESE YET, and that is the point of adding them now.  A
     * port is a published contract: a capability added later means every port
     * - desktop, Android, ESP32, Cortex-M - has to be revisited and every
     * out-of-tree port breaks.  Adding it now costs one line per port and a
     * NULL check in a caller that does not exist yet.
     *
     * A port that cannot supply executable memory leaves all three NULL, and a
     * JIT would fall back to the interpreter.  That is an honest answer, not a
     * failure: some parts (and some locked-down operating systems) genuinely
     * cannot do this, and the runtime must not assume otherwise.
     *
     * `code_commit` is where the two hard platform details live:
     *   - the INSTRUCTION CACHE must be synchronised after writing (ARM, Xtensa
     *     and RISC-V all need this; x86 does not, so its implementation is
     *     empty);
     *   - a W^X platform must flip the pages from write to execute here, having
     *     handed out writable memory from code_alloc.
     * Returns 0 on success, negative if the memory cannot be made executable.
     */
    void *(*code_alloc)(void *ud, nj_size n);
    void  (*code_free)(void *ud, void *p, nj_size n);
    int   (*code_commit)(void *ud, void *p, nj_size n);

    /* ---- device properties ---- */
    /*
     * A property this DEVICE reports, or NULL.
     *
     * System.getProperty() answers the standard MIDP/CLDC keys itself and falls
     * back to this for everything else.  Vendor keys belong here rather than in
     * the runtime because they are facts about the device, not about the JVM:
     * a Nokia build reports com.nokia.keyboard.type, a desktop reports that it
     * has no keypad, and a port for a different handset reports its own.
     *
     * It matters because games PROBE these without checking for null - the
     * first third-party game built from source here did exactly that - so a
     * port that answers "no keypad" for a machine with no keypad is not
     * inventing anything, it is describing itself.
     */
    const char *(*device_property)(void *ud, const char *key);

    /* ---- vibration ---- */
    void  (*vibrate)(void *ud, int ms);

    /* Released by nj_run() before it returns, so a port that allocated its
     * own context or framebuffer leaves nothing behind either. */
    void  (*shutdown)(void *ud);

    /* ================= TUNABLES ================= */

    int   width;          /* 0 => use NJ_SCREEN_WIDTH  */
    int   height;         /* 0 => use NJ_SCREEN_HEIGHT */
    int   fullscreen;
    int   scale;          /* integer upscale hint for desktop windows (>=1) */
    int   fps_cap;        /* 0 => uncapped */
    const char *rms_dir;  /* where RecordStore files live (hosted ports) */

    /*
     * Where "file:///root/" points on the host, for javax.microedition.io.file.
     *
     * A MIDlet gets NO access outside this directory: the runtime rejects any
     * path that escapes it, including through "..".  On a real handset JSR 75
     * is sandboxed the same way, and a file API that can be talked into reading
     * /etc/passwd is not a compatibility feature.  NULL means no filesystem,
     * and FileConnection then refuses to open anything.
     */
    const char *fs_dir;
} nj_port;

/* ------------------------------------------------------------------ */
/* Convenience: register a port and run a MIDlet/JAR                     */
/* ------------------------------------------------------------------ */
typedef struct nj_run_opts {
    const char *jar_path;      /* path on the port's filesystem, or NULL  */
    const void *jar_data;      /* in-memory jar, or NULL                  */
    nj_u4       jar_size;
    const char *midlet_class;  /* NULL => auto-select from the manifest   */
    int         argc;
    const char *const *argv;
    nj_size     heap_bytes;    /* 0 => NJ_MAX_HEAP                        */
    int         stats;         /* print execution counters before teardown */
    int         where;         /* describe where each thread stopped */
    nj_u4       run_frames;    /* 0 => run until the MIDlet exits */

    /*
     * Keys to press, as "frame:keycode" pairs separated by commas - for
     * example "20:53,40:53" presses key 53 at frame 20 and again at 40.
     *
     * A game that opens on a splash screen needs one of these before it will
     * show anything worth looking at, and "the splash screen renders" is not
     * evidence that a game runs.  A press is a key-down followed by a key-up
     * one frame later, which is what a human does and what most games require:
     * a key that goes down and never comes up leaves many of them stuck.
     */
    const char *keys;
} nj_run_opts;

/*
 * Run the emulator with the given port until the MIDlet exits.
 * Returns 0 on a clean MIDlet exit, negative nj_status on failure.
 * `port` is COPIED and is not owned by this call: it is not shut down on the
 * way out, so the caller can still ask it what it observed (an input queue, a
 * frame count, a PCM device) and is responsible for nj_port_shutdown() itself.
 */
int nj_run(const nj_port *port, const nj_run_opts *opts);

/* ------------------------------------------------------------------ */
/* Built-in ports                                                      */
/* ------------------------------------------------------------------ */
/*
 * Every built-in port *fills in* a caller-owned nj_port structure and returns
 * NJ_OK (or NJ_ERR_UNSUPPORTED when the backend was not compiled in).  There
 * is deliberately no `const nj_port *nj_port_xxx(void)` accessor: that would
 * require a shared static instance, which the runtime does not allow.
 *
 * The caller owns any state the port needs; the port allocates it lazily and
 * releases it from its own shutdown hook, which nj_run() invokes before
 * returning.
 */

/* Headless port: renders into an in-memory framebuffer that can be dumped to
 * PPM/ANSI, keys come from a scripted event list.  Used by the test suite and
 * by serial-console-only boards. */
nj_status nj_port_headless(nj_port *out);

/* SDL2 port (desktop).  `title` may be NULL. */
nj_status nj_port_sdl2(nj_port *out, const char *title);

/* Android NDK port.
 *   nj_port_android_set_window(&port, ANativeWindow *win);
 *   nj_port_android_push_event(&port, const nj_event *ev);
 *   nj_port_android_push_key(&port, int android_keycode, int down); */
nj_status nj_port_android(nj_port *out);
void      nj_port_android_set_window(nj_port *port, void *native_window);
void      nj_port_android_push_event(nj_port *port, const nj_event *ev);
void      nj_port_android_push_key(nj_port *port, int android_keycode, int down);

/* ESP-IDF port for ESP32-S3 (SPI/RGB LCD + GPIO buttons).  Configure it with
 * nj_port_esp32_config before calling nj_port_esp32(). */
typedef struct nj_port_esp32_config {
    int pin_sclk, pin_mosi, pin_cs, pin_dc, pin_rst, pin_bl;
    int spi_host;             /* SPI2_HOST / SPI3_HOST */
    int clock_hz;
    int panel_w, panel_h;
    int offset_x, offset_y;
    int invert_colors;
    int backlight_on_level;
    /* Buttons: map NJ_KEY_* -> GPIO number, -1 to disable. */
    int button_gpio[NJ_KEY_COUNT];
    int button_active_low;
    /* Optional I2S audio out; set audio_bclk < 0 to disable. */
    int audio_bclk, audio_ws, audio_dout;
    const char *rms_partition;   /* e.g. "storage" for RecordStore   */
    const char *jar_partition;   /* e.g. "jar" for the embedded game */
} nj_port_esp32_config;

nj_status nj_port_esp32_config_default(nj_port_esp32_config *out);
nj_status nj_port_esp32(nj_port *out, const nj_port_esp32_config *cfg);

#ifdef __cplusplus
}
#endif

#endif /* NJ_PORT_H */
