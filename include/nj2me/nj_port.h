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
#include "nj_gfx.h"   /* nj_font: the port may supply the glyphs */
#include "nj_device.h" /* nj_device_prop: the properties nj_run() installs */

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

/*
 * One sensor a port can offer, in the vocabulary javax.microedition.sensor
 * (JSR 256) uses.  The runtime copies everything it needs out of this struct
 * when it builds a SensorInfo, so a port may keep the storage in whatever
 * shape suits it - a table of string literals in .rodata, or the heap block it
 * allocated at init.
 *
 * The strings are borrowed for the duration of the call that produced the
 * struct only; the runtime interns what it keeps.
 *
 * A port that cannot see any sensors leaves all four hooks NULL.  That is not
 * an error: SensorManager.findSensors() then returns an empty array, and a
 * MIDlet that needs the accelerometer says so itself instead of reading a
 * fabricated zero.
 */
typedef struct nj_sensor_desc {
    const char *url;          /* "sensor:acceleration;context=user;model=x"  */
    const char *quantity;     /* "acceleration", "luminance", ...            */
    const char *context;      /* "user" | "ambient" | "device" | "vehicle"   */
    const char *model;
    int         channels;     /* 1..NJ_SENSOR_MAX_CHANNELS                   */
    const char *channel_names[NJ_SENSOR_MAX_CHANNELS];
    const char *units[NJ_SENSOR_MAX_CHANNELS];
    double      min_values[NJ_SENSOR_MAX_CHANNELS];
    double      max_values[NJ_SENSOR_MAX_CHANNELS];
    double      resolutions[NJ_SENSOR_MAX_CHANNELS];
    nj_u4       max_rate_hz;  /* 0 when the port does not know               */
} nj_sensor_desc;

/*
 * What a connected socket is, as the MIDlet sees it through
 * SocketConnection: the peer's address and port, and the local ones.
 *
 * A port fills this in when it connects.  A port with nothing to report
 * leaves the strings empty and the ports 0, and the runtime then answers with
 * the host and port that were asked for rather than with an empty string -
 * which is the useful answer, and the one a MIDlet that logs its connection
 * is looking for.  `address` is a name for a TCP peer and a Bluetooth address
 * for an RFCOMM one; the runtime does not interpret it.
 */
typedef struct nj_socket_info {
    char address[64];
    char local_address[64];
    int  port;
    int  local_port;
} nj_socket_info;

/*
 * One Bluetooth device, as javax.bluetooth.RemoteDevice shows it.  The
 * address is twelve hex digits and no separators ("001122334455"), which is
 * the spelling J2ME uses everywhere and the spelling a port has to produce.
 */
typedef struct nj_bt_device {
    char address[18];
    char name[64];       /* the friendly name; may be empty, never NULL */
    nj_u4 device_class;  /* the class-of-device word, 0 when unknown */
    int  paired;
} nj_bt_device;

/*
 * One Bluetooth service, as javax.bluetooth.ServiceRecord shows it.  `url` is
 * a complete J2ME connection URL ("btspp://001122334455:3") so that
 * ServiceRecord.getConnectionURL() can hand it straight to Connector.open().
 * `uuid` is the service's UUID, big-endian: a 16-bit UUID sits in the last two
 * bytes, a 128-bit one fills all sixteen.
 */
typedef struct nj_bt_service {
    char address[18];
    char name[64];
    char url[192];
    nj_u1 uuid[16];
} nj_bt_service;

/* The local adapter, for javax.bluetooth.LocalDevice. */
typedef struct nj_bt_local {
    char address[18];
    char name[64];
    nj_u4 device_class;
    int  discoverable;   /* 0x9E8B00 not discoverable, 0x9E8B33 GIAC */
    /*
     * How long an inquiry should run on this radio, in milliseconds; 0 means
     * the runtime's default (twelve seconds, which is what a real adapter
     * needs).  It is here because the inquiry's length is a property of the
     * radio and not of the MIDlet - the API takes no duration - and a
     * simulated radio that answers on the first call should not make a test
     * wait twelve real seconds to see inquiryCompleted().
     */
    int  inquiry_ms;
} nj_bt_local;

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

    /* ---- MIDI ---- */
    /*
     * Some platforms ship a General MIDI synthesizer of their own.  Android
     * has had one since API 21 - libsonivox.so plus a MIDI extractor - and it
     * is the difference between the four waveforms src/midp/nj_midi.c can
     * draw and a piano.  A port that has one implements this group and the
     * runtime hands the whole Standard MIDI File over instead of rendering it
     * sample by sample; a port that does not leaves all eight NULL and the
     * runtime renders, which is what the desktop, the ESP32 and the headless
     * test port do.
     *
     * The split is the platform's own, not an invention here: open prepares a
     * file and returns an opaque handle, start begins playing it, close
     * releases it.  A handle belongs to ONE Player, so two MIDI Players at
     * once are two handles and the runtime never assumes there is only one.
     *
     * midi_open returning NULL is not an error the MIDlet sees: it means
     * "this port cannot play that file", and the runtime renders it itself.
     * A port may therefore accept some files and decline others, and a build
     * may stop offering the hook at all without anything above it changing.
     *
     * `loop` is 1 to repeat until stopped and 0 to play once.  midi_time and
     * midi_seek are milliseconds from the start, so Player.getMediaTime() and
     * setMediaTime() keep working; a port that cannot seek leaves midi_seek
     * NULL and the runtime keeps its own clock.  midi_finished answers 1 once
     * the file has played out, which is what END_OF_MEDIA is fired from.
     */
    void *(*midi_open)(void *ud, const void *data, nj_u4 len,
                       nj_u4 *duration_ms);
    int   (*midi_start)(void *ud, void *handle, int loop);
    void  (*midi_stop)(void *ud, void *handle);
    void  (*midi_close)(void *ud, void *handle);
    void  (*midi_seek)(void *ud, void *handle, nj_u4 ms);
    nj_u4 (*midi_time)(void *ud, void *handle);
    void  (*midi_volume)(void *ud, void *handle, int volume);  /* 0..100 */
    int   (*midi_finished)(void *ud, void *handle);

    /*
     * Single events, for MIDIControl.
     *
     * A port whose synthesizer can be played note by note offers these and
     * keeps the song when a MIDlet calls MIDIControl.setChannelVolume() or
     * sends an event.  Both return NJ_OK when the event was taken and
     * anything else to decline it; declining is not an error.  A port that
     * cannot take events - Android's MediaPlayer has no such call - leaves
     * both NULL, and the runtime then moves the song into its own renderer at
     * the position the platform had reached, so the call still works.
     *
     * midi_short_event carries one channel message: type is the status byte
     * (0x90 note on, 0x80 note off, 0xB0 control change, 0xC0 program change,
     * 0xE0 pitch bend), data1/data2 the two data bytes.  midi_long_event
     * carries a Standard MIDI File byte stream (one or more complete events,
     * delta times included) for a port that prefers to forward the bytes.
     */
    int   (*midi_short_event)(void *ud, void *handle, int type, int data1,
                              int data2);
    int   (*midi_long_event)(void *ud, void *handle, const void *data,
                             nj_u4 len);

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
    /*
     * Optional audio decoder, for platforms that have one.
     *
     * The runtime decodes PCM WAV and synthesises MIDI itself, and names what
     * it cannot do (8 collected titles ship AMR effects and 5 ship MP3).  A
     * port that can decode more fills this in and the runtime hands it the
     * whole file, then plays whatever PCM comes back -- so a phone with a
     * hardware codec, or a desktop with libavcodec, plays the file instead of
     * being told no.
     *
     * Called twice: once with out == NULL to report how much room is needed,
     * then with a buffer the runtime allocated and therefore owns.  Return
     * NJ_ERR_UNSUPPORTED to decline a container and get the usual refusal.
     * PCM is signed 16-bit little-endian, interleaved.
     */
    nj_status (*audio_decode)(void *ud, const nj_u1 *in, nj_u4 in_len,
                              nj_u1 *out, nj_u4 out_cap, nj_u4 *out_len,
                              nj_u4 *sample_rate, nj_u4 *channels);

    /*
     * Inflate one zlib stream, for the .m3g reader.
     *
     * Unlike audio_decode this needs no two-call dance: an .m3g section
     * declares its uncompressed length up front, so the caller knows how much
     * room to provide and passes it in.  Return NJ_ERR_UNSUPPORTED when the
     * port has no decompressor - the loader then reports the section as
     * unreadable rather than guessing at its contents.
     */
    nj_status (*inflate)(void *ud, const nj_u1 *in, nj_u4 in_len,
                         nj_u1 *out, nj_u4 out_len);

    /*
     * Inflate one RAW DEFLATE stream (RFC 1951) - the form a zip entry holds.
     *
     * The zip reader calls this for every compressed entry, which on a JAR
     * means every class and every resource, so a platform that has zlib should
     * implement it: the built-in decompressor is about 3 KB of code and a
     * platform's is usually faster as well.  Same contract as `inflate` above
     * (the caller knows the uncompressed length from the zip directory and
     * sizes the buffer), and NJ_ERR_UNSUPPORTED when the port has none.
     *
     * With -DNJ_ENABLE_INFLATE=0 this hook is the ONLY way to read a deflated
     * JAR: a build without it can still open a JAR whose entries are stored
     * uncompressed, and reports a compressed entry as unreadable instead of
     * guessing at its contents.
     */
    nj_status (*inflate_raw)(void *ud, const nj_u1 *in, nj_u4 in_len,
                             nj_u1 *out, nj_u4 out_len);

    /*
     * One HTTP request, for javax.microedition.io.HttpConnection.
     *
     * MIDP 2.0 makes HttpConnection mandatory, and it is the one network API
     * MIDlets actually use: 99 of the 396 titles in tests/games reference
     * javax/microedition/io/HttpConnection - high-score tables, ads, "check
     * for updates" screens.  A port with a network stack fills this in and
     * those games work; a bare-metal target leaves it NULL and the runtime
     * refuses "http://" by name, which is what it did before this hook
     * existed.
     *
     * The call is synchronous and the whole response comes back at once.  That
     * is the shape the MIDP API has anyway - a MIDlet asks for a response code
     * and then reads a stream - and it keeps a port's implementation to one
     * function instead of a connection state machine.
     *
     * `headers` and `rheaders` are text blocks, one "Name: value" per line,
     * '\n' terminated; `rheaders` may be truncated (never split mid-line) when
     * the port has more headers than fit.  `body`/`body_len` carry a POST
     * payload (NULL/0 for GET or HEAD).  On success fill *status with the HTTP
     * status code, *out_body with the response body and *out_len with its
     * length, and return NJ_OK.  `alloc` allocates through the runtime's own
     * tracked allocator, so whatever the port puts in *out_body the runtime
     * frees with the matching `aud` - a port must not hand back memory the
     * runtime did not allocate.
     *
     * Return NJ_ERR_UNSUPPORTED when the port has no HTTP stack; any other
     * error means the request itself failed, and the MIDlet then sees an
     * IOException rather than a fabricated 200.
     */
    nj_status (*http_request)(void *ud, const char *url, const char *method,
                              const char *headers,
                              const nj_u1 *body, nj_u4 body_len,
                              int *status, char *rheaders, nj_size rheaders_cap,
                              nj_u1 **out_body, nj_u4 *out_len,
                              void *(*alloc)(void *aud, nj_u4 n), void *aud);

    /*
     * Sensors - the whole of JSR 256, in four calls.
     *
     * JSR 256 is a pull API with a push face: a MIDlet either asks for a
     * sample or registers a listener and is handed one every period.  Both are
     * the same thing underneath - read the newest value the device has - and
     * that is what these hooks are.  The runtime owns the pacing, the
     * listener bookkeeping and the Data objects; the port only has to know
     * what the hardware just measured.
     *
     *   sensor_list     describe every sensor the device has, up to `max`.
     *                   Returns how many were written; out == NULL asks for
     *                   the count alone.  Descriptors are read once per
     *                   findSensors() call and copied.
     *   sensor_open     start measuring the sensor whose url matches.  Returns
     *                   a handle, or NULL when the device cannot (no
     *                   permission, sensor busy).  period_ms is a hint.
     *   sensor_read     copy the newest sample into `values` (one per channel)
     *                   and return the channel count, 0 when nothing has been
     *                   measured yet.  Never blocks: a port whose only source
     *                   is an event callback returns 0 until the first event
     *                   arrives.  The value is a snapshot, not a queue.
     *   sensor_close    stop measuring and release the handle.  Called when a
     *                   MIDlet closes the connection, and by the port's own
     *                   shutdown() for any handle still open.
     *
     * A port that has no sensors leaves all four NULL and nothing is
     * fabricated: findSensors() returns an empty array.
     */
    int   (*sensor_list)(void *ud, nj_sensor_desc *out, int max);
    void *(*sensor_open)(void *ud, const char *url, nj_u4 period_ms);
    int   (*sensor_read)(void *ud, void *handle, double *values, int max_values);
    void  (*sensor_close)(void *ud, void *handle);

    /*
     * Stream sockets, for javax.microedition.io.SocketConnection - and, one
     * day, for javax.bluetooth's RFCOMM, which is the same byte stream over a
     * different radio.
     *
     *   socket_connect  open a stream to `url` ("socket://host:port", or a
     *                   "btspp://..." URL if the port has Bluetooth).  Returns
     *                   a handle, or NULL after writing a sentence into `err`
     *                   for the MIDlet's ConnectionNotFoundException.
     *                   `timeout_ms` is how long the caller will wait; 0 means
     *                   wait indefinitely.
     *   socket_read     block up to `timeout_ms` for at most `len` bytes:
     *                   > 0 is that many bytes, 0 is "nothing arrived yet",
     *                   -1 is "the peer closed".  A port that cannot wait
     *                   returns 0 immediately and the runtime asks again.
     *   socket_write    send all of it; < 0 is an error.
     *   socket_close    hang up and release the handle.  The runtime calls it
     *                   when the MIDlet closes the connection, and the port's
     *                   shutdown() has to close whatever is still open.
     *
     * A port with no socket stack leaves all four NULL, and Connector.open()
     * then refuses socket:// BY NAME - "this build has no socket support" -
     * which is what the specification asks for an unsupported scheme and what
     * a developer can act on.  There is no server socket and no datagram
     * hook: 0 of the 436 titles in the test corpus use either.
     */
    void *(*socket_connect)(void *ud, const char *url, nj_u4 timeout_ms,
                            nj_socket_info *info, char *err, nj_size err_cap);
    int   (*socket_read)(void *ud, void *handle, nj_u1 *buf, int len,
                         nj_u4 timeout_ms);
    int   (*socket_write)(void *ud, void *handle, const nj_u1 *buf, int len);
    void  (*socket_close)(void *ud, void *handle);

    /*
     * Bluetooth, for javax.bluetooth (JSR 82) - nine of the 436 titles in the
     * test corpus name a class from that package, every one of them for a
     * game that plays against another handset.  The radio is the port's; what
     * the runtime keeps is the API around it.
     *
     * A Bluetooth client connection is NOT a hook of its own: Connector.open()
     * hands "btspp://001122334455:3" to socket_connect(), which is the same
     * byte stream over a different radio (see the note above), so a port that
     * has both writes that one function and gets both schemes.
     *
     *   bt_local        describe the local device; NJ_ERR_UNSUPPORTED (or a
     *                   NULL hook) means "this machine has no radio", and
     *                   LocalDevice.getLocalDevice() then throws
     *                   BluetoothStateException naming that, which is exactly
     *                   what the specification asks for.
     *   bt_discoverable 0x9E8B00 not discoverable, 0x9E8B33 GIAC (general),
     *                   0x9E8B01..0x9E8B32 LIAC.  < 0 means the port cannot
     *                   change it.
     *   bt_inquiry      fill in at most `max` devices, waiting no longer than
     *                   `duration_ms`; returns how many, 0 for none, < 0 when
     *                   the radio cannot inquire.  The runtime calls this from
     *                   the frame pump, so a port that cannot block reports 0
     *                   and is asked again - the MIDlet's listener is what
     *                   makes the wait finite.
     *   bt_services     search one device for services whose UUID matches
     *                   `uuid` (NULL = any).  Same rules: returns a count,
     *                   0 or < 0, and never blocks for long.
     *   bt_connect      open a client connection from a
     *                   "btspp://<address>:<channel>" or
     *                   "btl2cap://<address>:<psm>" URL, waiting no longer than
     *                   `timeout_ms` for the peer, returning a connection
     *                   handle (NULL + a sentence in `err` when it cannot).
     *                   This is deliberately NOT socket_connect: a TCP socket
     *                   cannot reach a Bluetooth address, and a port that has
     *                   both would otherwise have to guess from the URL which
     *                   of its two stacks the caller meant.
     *   bt_listen       open a server from a "btspp://localhost:<uuid>;name=x"
     *                   or "btl2cap://localhost:<uuid>;name=x" URL, returning a
     *                   notifier handle (NULL + a sentence in `err` if it
     *                   cannot), or NULL with an empty err when the port has no
     *                   Bluetooth at all - the scheme is then refused by name.
     *   bt_accept       wait up to `timeout_ms` for a peer; NULL is "nobody
     *                   yet", and the caller comes back.
     *   bt_read/write/close  the same contract as socket_read/write/close,
     *                   and close() must also close a notifier.
     *
     * A port with no radio leaves every one of them NULL: the package is still
     * there, findSensors-style "nothing found" answers come back, and
     * LocalDevice.getLocalDevice() says why.
     */
    nj_status (*bt_local)(void *ud, nj_bt_local *out);
    int   (*bt_discoverable)(void *ud, int mode, nj_u4 seconds);
    int   (*bt_inquiry)(void *ud, nj_bt_device *out, int max, nj_u4 duration_ms);
    int   (*bt_services)(void *ud, const char *address, const nj_u1 *uuid,
                         nj_bt_service *out, int max);
    void *(*bt_connect)(void *ud, const char *url, nj_u4 timeout_ms,
                        char *err, nj_size err_cap);
    void *(*bt_listen)(void *ud, const char *url, char *err, nj_size err_cap);
    void *(*bt_accept)(void *ud, void *handle, nj_u4 timeout_ms);
    int   (*bt_read)(void *ud, void *handle, nj_u1 *buf, int len,
                     nj_u4 timeout_ms);
    int   (*bt_write)(void *ud, void *handle, const nj_u1 *buf, int len);
    void  (*bt_close)(void *ud, void *handle);

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

    /*
     * Draw text with a TrueType face from this file, or NULL for the built-in
     * bitmap faces.  Read through the port's file hooks and installed before
     * the MIDlet starts, because MIDP layout is measured rather than
     * re-flowed: the first screen the MIDlet builds is laid out with whatever
     * face was in place at the time.  A font that fails to load leaves the
     * built-in faces alone; nj_run() says so rather than running silently in
     * the wrong typeface.
     */
    const char *font_path;

    /*
     * Faces for slots the platform's own text stack should *be*, not just
     * answer behind.  A fallback is only consulted for characters the built-in
     * cell cannot draw, so a port whose system typeface should draw Latin too
     * has to replace the slot itself; that is what this pair does.
     *
     * Same indexing, same lifetime and the same install window as
     * fallback_fonts.  Installed first, so a slot can be the platform's face
     * and still keep a fallback chain behind it.
     */
    const nj_font *const *primary_fonts;
    int         primary_count;

    /*
     * Fallback faces for glyphs no built-in face can draw - how a port hands
     * the runtime the platform's own text stack instead of shipping a font.
     *
     * Indexed by size_index(size) * NJ_FONT_SLOT_STYLES + bold: small, medium
     * and large, each plain then bold.  A NULL entry leaves that slot as it
     * was, so a port that only knows one cell size fills one entry per size
     * and points them at the same provider.
     *
     * Each face is borrowed, not copied: it must outlive the run.  Install
     * happens before the MIDlet starts, for the same reason font_path is read
     * there - MIDP layout is measured, not re-flowed.
     */
    const nj_font *const *fallback_fonts;
    int         fallback_count;

    /*
     * System properties to answer on top of the runtime's own - the device the
     * MIDlet believes it is running on.  Borrowed, and the table must outlive
     * the run; NULL is the "generic" profile (see nj2me/nj_device.h, which also
     * has the built-in handset profiles and what they answer).
     *
     * Within the table the LAST match wins, so a caller installs a device
     * profile first and its own overrides after it.  A MIDlet asking for a key
     * that is not here, not one of the runtime's own facts, and not something
     * the port knows falls through to NULL - which is what the old code answered
     * for almost everything, including for keys the port DID know.
     */
    const nj_device_prop *props;
    int         nprops;

    /*
     * Resume a saved VM instead of starting the MIDlet normally: nj_run()
     * attaches the JAR, loads this snapshot and then leaves the MIDlet alone,
     * because the restored thread state is already sitting inside startApp()
     * (or wherever the snapshot was taken).  NULL runs as usual.  A snapshot
     * that cannot be restored - a different JAR, a truncated file - is logged
     * and the run falls back to a normal MIDlet start rather than failing:
     * resuming is a convenience, not a promise the caller can lose a run over.
     */
    const char *load_state_path;

    /*
     * Write one snapshot at the frame boundary given by save_state_frame and
     * keep running.  0 means "do not save".
     */
    const char *save_state_path;
    nj_u4       save_state_frame;
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
 *   nj_port_android_push_key(&port, int android_keycode, int down);
 *
 * The port owns an ANativeWindow it was handed, an event queue the UI thread
 * posts into, and (once something asks for sound) an AAudio output stream.
 * Everything else in this list exists because the UI thread needs to say
 * something to the frame loop that the frame loop cannot find out by itself.
 */
nj_status nj_port_android(nj_port *out);
void      nj_port_android_set_window(nj_port *port, void *native_window);
void      nj_port_android_push_event(nj_port *port, const nj_event *ev);
void      nj_port_android_push_key(nj_port *port, int android_keycode, int down);

/* Touch phases, matching MotionEvent's action so the JNI layer can pass one
 * straight through.  CANCEL is delivered to the MIDlet as a release: MIDP has
 * no third state, and a finger the system took away is a finger that is up. */
typedef enum nj_android_touch_phase {
    NJ_ANDROID_TOUCH_DOWN = 0,
    NJ_ANDROID_TOUCH_MOVE,
    NJ_ANDROID_TOUCH_UP,
    NJ_ANDROID_TOUCH_CANCEL
} nj_android_touch_phase;

/*
 * A finger moved, in VIEW pixels.
 *
 * View pixels, not MIDlet pixels, because the letterbox transform lives in
 * display_present() and nothing outside the port knows the scale factor or the
 * black bars.  The caller passes what the MotionEvent said and the port
 * inverts its own transform, so a tap on a scaled-up sprite lands on that
 * sprite.  Only ONE finger drives the MIDP pointer (pointerPressed/Dragged/
 * Released is a single-pointer API); the first finger down owns it and the
 * rest are ignored until it lifts, which is what makes a second thumb on the
 * keypad harmless.
 */
void      nj_port_android_touch(nj_port *port, int pointer_id, int phase,
                                float x, float y);

/*
 * Where this app may write: RecordStore files, and what "file:///root/" means
 * to javax.microedition.io.file.  Both are absolute host paths from Java
 * (Context.getFilesDir()); the port copies them.  fs_dir NULL or empty leaves
 * the MIDlet without a filesystem, which is an honest answer rather than an
 * empty directory.  Call before nj_run().
 */
void      nj_port_android_set_paths(nj_port *port, const char *rms_dir,
                                    const char *fs_dir);

/*
 * Foreground/background.  Android tells Java when the app is hidden, and the
 * runtime has no way to notice by itself: the frame loop would keep running
 * and keep playing audio behind the launcher.  inactive parks the VM thread in
 * the port's idle() hook and pauses the audio stream; active lets it go again.
 */
void      nj_port_android_set_active(nj_port *port, int active);

/*
 * One line describing what this port has observed - screen, scale, frames,
 * keys, pointer, audio state.  Written for logcat and for a diagnostic screen:
 * "the game is running" and "the game is running and its keys arrive" look
 * identical from outside, and this is what tells them apart.  Returns the
 * length written, or negative.
 */
int       nj_port_android_stats(const nj_port *port, char *buf, nj_size cap);

/*
 * How the game's picture is placed in the window.  Values are part of the Java
 * contract (the launcher stores the chosen one per game), so they are fixed.
 */
typedef enum nj_android_view_mode {
    NJ_VIEW_INT = 0,     /* largest whole-number scale that fits, centred */
    NJ_VIEW_FILL,        /* fractional scale: the largest that fits, centred */
    NJ_VIEW_CUSTOM,      /* fit scale times `zoom`, moved by `dx`/`dy` */
    NJ_VIEW_AUTO,        /* crop to the painted area, then as NJ_VIEW_INT */
    NJ_VIEW_MODE_COUNT
} nj_android_view_mode;

/*
 * Place the picture: `zoom` is read only in NJ_VIEW_CUSTOM (a multiplier on the
 * fit scale, 1.0 = default) and `dx`/`dy` are a pan in window pixels, also only
 * there.  Safe to call from the UI thread at any time.
 *
 * This exists because a J2ME screen is small and a title written for 128x128
 * gets a canvas the launcher chose, so the picture can end up a stamp in the
 * corner of a black rectangle.  The player can fill the screen, drag and zoom
 * it, or ask for the area the MIDlet actually paints.
 */
void      nj_port_android_set_view(nj_port *port, int mode, float zoom,
                                   float dx, float dy);

/*
 * The area of the logical screen the MIDlet has painted, as
 * {x, y, w, h, frames}.  Returns 1 when the box is worth using - which is what
 * the launcher shows as "detected resolution" and what NJ_VIEW_AUTO crops to.
 *
 * No manifest in this project's corpus declares a screen size (6 of 395 games),
 * so this is measured instead of read: frames are compared with the one before
 * and the pixels that changed are unioned into a box.  `frames` says how many
 * frames the measurement has seen, so a caller can wait for more than a couple
 * before trusting a box that appeared while the game was still starting.
 *
 * What is returned is the union snapped to a real handset screen by
 * nj_detect_snap() - never a raw box, so a caller can use it as a resolution
 * without checking it first.  The Android port refuses to publish at all when
 * the union is not a screen, and keeps measuring instead.
 */
int       nj_port_android_detect(const nj_port *port, int out[5]);

/*
 * Is the box a port detected a screen, or just the part of one a MIDlet
 * happened to touch?  A port that crops to a raw box zooms a game into a strip
 * of itself the moment the game only ever painted a corner - and one that
 * offers the box as "the game's resolution" writes a size no MIDlet is laid
 * out for.  This snaps the box to the nearest real handset screen (a table of
 * nineteen of them, portrait and landscape) within a sixteenth per dimension,
 * centred on what was detected, or refuses when nothing is close enough.
 *
 * Returns 1 and writes the box to use, or 0 when the caller should keep
 * watching instead of cropping.  Implementation: src/core/nj_detect.c, tests:
 * tests/test_detect.c.
 */
int       nj_detect_snap(int bx, int by, int bw, int bh, int cw, int ch,
                         int *ox, int *oy, int *ow, int *oh);

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
