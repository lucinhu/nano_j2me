/*
 * nano_j2me - nj_port_android.c
 *
 * The Android NDK port, plus the JNI glue an Activity needs.
 *
 * Android is a hosted target, so the port has no memory work to do and its
 * filesystem is bionic's stdio; what it does need is a framebuffer that can be
 * handed to the window compositor without a copy, an input queue that the UI
 * thread can post into from Java, and an audio device.
 *
 * FOUR DETAILS THAT MATTER
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
 *
 *   The MIDlet screen and the Surface are DIFFERENT SIZES, and the transform
 *   between them is only in here.  display_present() computes an integer scale
 *   and a centring offset; nj_port_android_touch() records the same numbers so
 *   that it can invert them.  Every touch coordinate a game sees is therefore
 *   in the coordinate system its Canvas was given, which is the only thing that
 *   makes a tap land where the user aimed.
 *
 *   THE PORT RENDERS, THE ACTIVITY DRAWS THE KEYPAD.  An on-screen keypad is a
 *   UI concern (it is configured in a settings screen and it changes shape
 *   between titles), so it lives in Java, on top of the SurfaceView, and turns
 *   its buttons into the same key events a hardware keypad would send.  The
 *   port never learns that the keypad exists - it only sees key codes.
 */
#include "nj2me/nj_port.h"
#include "nj2me/nj_types.h"
#include "nj2me/nj.h"          /* nj_run, nj_port_shutdown */
#include "nj2me/nj_codec.h"    /* nj_codec_register, nj_codec_image_new */

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
#include <dirent.h>
#include <sys/stat.h>
#include <zlib.h>              /* DEFLATE, for JAR entries and .m3g sections */

/*
 * Two audio devices, one mixer, one APK.
 *
 * AAudio (API 26) is the device this port wants: one stream, a data callback,
 * and no object graph to build and tear down.  OpenSL ES (API 9) is what every
 * Android older than that has.  Building for only one of them means either an
 * APK that will not install on a 2015 phone or one that cannot use the audio a
 * 2023 phone has, and shipping two APKs is a worse answer than this one.
 *
 * So neither library is linked.  libaaudio.so is opened by hand the first time
 * a sound is played - a hard link would make the loader refuse to load
 * libnj2me.so at all on a device that has no such library - and when it is
 * missing the same mixer is driven by OpenSL ES.  The declarations below are
 * copied from the NDK's <aaudio/AAudio.h>, which marks all of them "introduced
 * in Android 26" and refuses to compile them for an older target; they have to
 * keep matching it.
 */
#include <dlfcn.h>
#include <sys/system_properties.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#ifndef PROP_VALUE_MAX
#define PROP_VALUE_MAX 92   /* __system_property_get's buffer, from <sys/system_properties.h> */
#endif

typedef struct AAudioStreamStruct AAudioStream;
typedef struct AAudioStreamBuilderStruct AAudioStreamBuilder;
typedef int32_t aaudio_result_t;
typedef int32_t aaudio_direction_t;
typedef int32_t aaudio_sharing_mode_t;
typedef int32_t aaudio_performance_mode_t;
typedef int32_t aaudio_format_t;
typedef int32_t aaudio_usage_t;
typedef int32_t aaudio_data_callback_result_t;

#define AAUDIO_OK                        0
#define AAUDIO_DIRECTION_OUTPUT          0
#define AAUDIO_SHARING_MODE_SHARED       1
#define AAUDIO_PERFORMANCE_MODE_NONE     10
#define AAUDIO_FORMAT_PCM_I16            1
#define AAUDIO_USAGE_GAME                14
#define AAUDIO_CALLBACK_RESULT_CONTINUE  0

typedef aaudio_data_callback_result_t (*aaudio_data_cb_t)(AAudioStream *, void *,
                                                          void *, int32_t);
typedef void (*aaudio_error_cb_t)(AAudioStream *, void *, aaudio_result_t);

/* Resolved once per port, on the first sound.  A phone without AAudio leaves
 * lib NULL and the OpenSL ES path takes over. */
typedef struct android_aaudio {
    void  *lib;
    int    tried;
    aaudio_result_t (*createBuilder)(AAudioStreamBuilder **);
    void (*setDirection)(AAudioStreamBuilder *, aaudio_direction_t);
    void (*setSharingMode)(AAudioStreamBuilder *, aaudio_sharing_mode_t);
    void (*setPerformanceMode)(AAudioStreamBuilder *, aaudio_performance_mode_t);
    void (*setFormat)(AAudioStreamBuilder *, aaudio_format_t);
    void (*setChannelCount)(AAudioStreamBuilder *, int32_t);
    void (*setSampleRate)(AAudioStreamBuilder *, int32_t);
    void (*setUsage)(AAudioStreamBuilder *, aaudio_usage_t);   /* API 28 */
    void (*setDataCallback)(AAudioStreamBuilder *, aaudio_data_cb_t, void *);
    void (*setErrorCallback)(AAudioStreamBuilder *, aaudio_error_cb_t, void *);
    aaudio_result_t (*openStream)(AAudioStreamBuilder *, AAudioStream **);
    void (*deleteBuilder)(AAudioStreamBuilder *);
    int32_t (*getSampleRate)(AAudioStream *);
    int32_t (*getChannelCount)(AAudioStream *);
    aaudio_result_t (*requestStart)(AAudioStream *);
    aaudio_result_t (*requestPause)(AAudioStream *);
    aaudio_result_t (*requestStop)(AAudioStream *);
    aaudio_result_t (*close)(AAudioStream *);
} android_aaudio;

#define LOG_TAG "nj2me"

/* ------------------------------------------------------------------ */
/* Audio                                                               */
/* ------------------------------------------------------------------ */
/*
 * A tone synth and a PCM ring, mixed in one AAudio callback.
 *
 * Why both in one stream: MIDlet games play tones and sampled audio at the
 * same time (a jingle over a sound effect), and two output streams would mix
 * them in the hardware mixer with no control over relative level and no way to
 * count what was actually played.
 *
 * The device is opened LAZILY, on the first tone or PCM write.  A MIDlet that
 * never makes a sound never touches the audio device, which matters on a phone.
 *
 * The stream is NOT paused when a sound ends.  Pausing on pcm_close() would cut
 * the tail of anything still sitting in the ring - the runtime throttles writes
 * through pcm_free() and can close the player the moment the last frame is
 * queued - so the stream stays open for as long as the game is in the
 * foreground, and nj_port_android_set_active(0) is what releases it.
 */
#define ANDROID_AUDIO_RATE   44100
#define ANDROID_AUDIO_TONES  16       /* one playTone() sequence            */
#define ANDROID_AUDIO_RING   16384    /* stereo frames, ~370 ms at 44.1 kHz */
#define ANDROID_TONE_AMP     7000     /* of 32767, leaving room for PCM     */

/*
 * OpenSL ES wants a fixed queue of buffers it hands back one at a time, and it
 * plays them on its own thread.  1024 frames is 23 ms at 44.1 kHz: short enough
 * that a tone starts when the game says so, long enough that a slow phone is
 * not woken up four hundred times a second.
 */
#define ANDROID_SL_BUFFERS   4
#define ANDROID_SL_FRAMES    1024

typedef struct android_audio {
    pthread_mutex_t lock;
    int   backend;       /* 0 = no device, 1 = AAudio, 2 = OpenSL ES */
    int   out_rate, out_ch;
    int   failed;        /* open failed once: stop retrying every tone */
    int   stream_paused;

    android_aaudio aa;                 /* backend 1 */
    AAudioStream  *stream;

    SLObjectItf    sl_engine_obj;      /* backend 2 */
    SLEngineItf    sl_engine;
    SLObjectItf    sl_mix_obj;
    SLObjectItf    sl_player_obj;
    SLPlayItf      sl_play;
    SLAndroidSimpleBufferQueueItf sl_queue;
    nj_s2         *sl_buf;
    int            sl_buf_frames;
    int            sl_next;

    /* tone sequencer */
    nj_audio_tone tones[ANDROID_AUDIO_TONES];
    int   tone_count, tone_next, tone_on;
    nj_u4 phase, step, left;   /* 16.16 phase accumulator, step, frames left */

    /* streaming PCM, always stored interleaved stereo */
    nj_s2 *ring;
    int   pcm_on, src_rate, src_ch, rd, wr;
    nj_u4 pcm_pos;             /* 16.16 playback position inside one frame */

    long long tones_played, pcm_frames, frames_rendered, callbacks;
} android_audio;

static nj_u4 android_millis(void *ud);

/*
 * How the logical screen is fitted into the window (android_state.view_mode) is
 * nj_android_view_mode, declared in include/nj2me/nj_port.h: the values travel
 * over JNI and are stored in the jar's sidecar file by Java, so they are part of
 * the Java contract and live where Java's caller can see them.
 */

/* ------------------------------------------------------------------ */
/* Glyphs from the platform                                            */
/* ------------------------------------------------------------------ */
/*
 * The runtime's wide-glyph table is a bitmap face compiled into the library:
 * 7445 GB2312 characters at 12x12 and 16x16, about 480 KB of rodata per ABI -
 * more than half of every libnj2me.so and 1.4 MB across the three ABIs in the
 * APK.
 *
 * Android already has those glyphs, in fonts this runtime cannot parse: the
 * system CJK faces are CFF2 (OTTO), and the TrueType path here reads glyf
 * outlines only.  So the port asks the platform to rasterise instead, through
 * one fallback face per slot (nj_vm_set_fallback_fonts).  Java draws a single
 * character into coverage rows, the port caches them, and the renderer blends
 * them exactly as it blends a compiled-in face.  With this in place the Android
 * build sets NJ_ENABLE_FONT_CJK=0 and ships no glyph table at all: the CJK
 * text is the phone's own font, at the phone's own hinting.
 *
 * A miss costs one JNI call per character per size, so the cache is not an
 * optimisation to add later - a screen of Chinese text would otherwise cross
 * the JNI boundary thousands of times a frame.
 */
#define ANDROID_GLYPH_CELL_MAX 16
#define ANDROID_GLYPH_CACHE    96

typedef struct android_glyph_slot {
    nj_u2 ch;
    int   cell;
    int   bold;                    /* the style the glyph was drawn in */
    int   w, h;
    nj_u1 rows[ANDROID_GLYPH_CELL_MAX * ANDROID_GLYPH_CELL_MAX];
} android_glyph_slot;

typedef struct android_glyph_cache {
    android_glyph_slot slot[ANDROID_GLYPH_CACHE];
    int next;                      /* round-robin victim */
    int calls, hits, misses, failed;
} android_glyph_cache;

typedef struct android_glyph_face {
    nj_font face;                  /* borrowed by the runtime, never freed */
    struct android_state *state;
    int   cell;                    /* the cell Java is asked to fill */
    int   bold;                    /* this slot is the bold style */
    int   last_w;                  /* what the last lookup returned */
} android_glyph_face;

/*
 * How much of the Bluetooth world this bridge keeps: the same shape the
 * runtime's javax.bluetooth module uses, spelled here because a port does not
 * include a MIDP module's private constants.  A busier neighbourhood is
 * truncated, not overflowed.
 */
#define BT_ADDR_CAP 24
#define BT_NAME_CAP 64
#define BT_DEV_MAX  16
#define BT_SVC_MAX  8

typedef struct android_state {
    ANativeWindow *window;
    pthread_mutex_t lock;

    int   width, height, stride;   /* the LOGICAL screen: what the MIDlet sees */
    int   win_w, win_h;            /* the Surface: usually much larger */
    int   format;
    nj_u1 *shadow;        /* the logical screen; scaled into the window */

    /*
     * The transform display_present() applies, kept so that touch can undo it.
     *
     * Floats, not the integer scale this port started with: the player can ask
     * for a fractional scale ("铺满屏幕") and a pan, and a tap mapped with a stale
     * integer scale lands on the wrong pixel of the game.
     *
     * view_src_* is the first LOGICAL pixel of the picture.  It is (0,0) unless
     * the player asked for 适配画面, which crops to the area the MIDlet paints.
     *
     * Guarded by `lock`: the frame loop writes it, the UI thread reads it, and
     * they are different threads.
     */
    float view_scale;              /* window pixels per logical pixel */
    float view_off_x, view_off_y;  /* where the picture starts in the window */
    int   view_src_x, view_src_y;  /* first logical pixel drawn */
    int   view_mode;               /* NJ_VIEW_* */
    float view_zoom;               /* multiplier on the fit scale, every mode */
    float view_dx, view_dy;        /* extra offset, window pixels, every mode */
    int   view_filter;             /* 0 = nearest, 1 = bilinear (fractional) */
    int   glyph_flush;             /* UI asked for the glyph cache to be dropped */
    int   view_limits_valid;       /* a frame has published the pan limits */
    int   view_max_pan_x, view_max_pan_y;  /* how far the pan may reach */
    long  frames_presented;
    int   logged_format;   /* window buffer format already reported to logcat */

    /*
     * Which part of the logical screen the MIDlet actually paints.
     *
     * No manifest in the corpus says what screen a game was written for - 6 of
     * the 395 jars in tests/games have a size key, and guessing from resource
     * names finds 5 - so the only honest answer is the one the game gives by
     * painting: keep the previous frame, union the pixels that changed into a
     * box, and stop once the box covers the screen (a game that paints
     * everywhere) or has held a screen-sized box still for a few seconds (one
     * that does not).  That box is what 适配画面 crops to, and what the launcher
     * offers as the resolution it detected.
     *
     * The union is not published as it stands: it is the raw box, and a port
     * that crops to it zooms a title into a strip of itself whenever the title
     * only ever touched part of its canvas.  See det_raw_* below.
     */
    nj_u1 *det_prev;               /* the previous frame, same size as shadow */
    int   det_x, det_y, det_w, det_h;   /* published: a screen, see below */
    int   det_frames;
    int   det_done;
    /*
     * The union of everything that changed, before it is snapped to a screen
     * size.  The two are kept apart on purpose: the union is the truth about
     * what the MIDlet touched, and the published box is the claim that it is a
     * canvas - a claim nj_detect_snap() refuses when the union is a corner of
     * one.  det_raw_same counts the frames the published box has held still,
     * which is how long a caller waits before trusting it.
     */
    int   det_raw_x, det_raw_y, det_raw_w, det_raw_h;

    /*
     * Game speed, in percent of real time.  The UI thread only ever writes
     * speed_percent; the run thread applies it at the top of a frame, because
     * nothing outside the VM thread may touch the VM.
     */
    nj_vm *vm;
    int   speed_percent;
    int   speed_applied;
    int   det_raw_same;

    /*
     * State snapshots.  The same rule as the speed: the UI thread leaves a
     * request (a path plus a flag) and the run thread serves it at the top of a
     * frame, which is a point where the VM is between frames and outside every
     * native call.  save_done/save_rc are the answer the UI thread waits for,
     * under lock, so "saved" is never printed for a file nobody wrote.
     */
    char  save_path[512];
    int   save_req;
    int   save_busy;
    int   save_done;
    int   save_rc;
    char  restore_path[512];
    int   restore;

    nj_event queue[256];
    int   qh, qt;

    /* observations, for nj_port_android_stats() */
    int   key_events, ptr_events;
    int   ptr_id;              /* finger that owns the MIDP pointer, or -1 */

    /* set_paths() copies these; where the MIDlet may write */
    char  rms_dir[512];
    char  fs_dir[512];

    int   paused;              /* backgrounded: idle() parks the VM thread */
    int   quit;

    android_audio *audio;

    /*
     * Java callbacks.  Owned by the JNI layer below, which sets these in
     * nativeCreate() and releases the global reference in nativeDestroy() -
     * the port itself must stay free of JNI lifecycle.
     */
    JavaVM *jvm;
    jclass  activity_class;
    jmethodID vibrate_mid;

    /*
     * The platform's glyphs.  One face per (size, style) slot, each a copy of
     * the built-in bitmap face of that slot plus a provider hook, so a face
     * keeps the line height MIDlet layout was measured against and only the
     * characters the bitmap cannot draw come from Java.
     */
    android_glyph_face  faces[NJ_FONTSET_SLOTS];
    const nj_font      *face_ptrs[NJ_FONTSET_SLOTS];
    android_glyph_cache *glyph_cache;
    jclass    glyph_class;
    jmethodID glyph_mid;
    JNIEnv   *run_env;         /* the run thread's env, attached on first use */
    int       run_attached;

    /*
     * MIDI.  The platform has a General MIDI synthesizer (Sonivox, in
     * libsonivox.so since API 21) behind MediaPlayer, and MidiPlayer.java
     * wraps it: the port hands it a file, gets back a handle, and the
     * synthesizer plays with real instruments instead of the four waveforms
     * src/midp/nj_midi.c can draw.  The handle is a Java-side id, never a
     * pointer, so nothing here has to know what a MediaPlayer is.
     */
    jclass    midi_class;
    jmethodID midi_open_mid;
    jmethodID midi_length_mid;
    jmethodID midi_start_mid;
    jmethodID midi_stop_mid;
    jmethodID midi_close_mid;
    jmethodID midi_seek_mid;
    jmethodID midi_time_mid;
    jmethodID midi_volume_mid;
    jmethodID midi_done_mid;

    /*
     * The platform's codecs, reached through one small Java class each:
     * PlatformImage wraps BitmapFactory (PNG/JPEG/GIF/BMP/WebP) and
     * PlatformAudio wraps MediaExtractor + MediaCodec (AMR, MP3, AAC, WAV).
     * Both hand back byte arrays, so nothing here has to know what a Bitmap or
     * a MediaCodec is - and neither is required: with no class resolved the
     * providers simply are not installed and the runtime decodes what it can.
     */
    jclass    image_class;
    jmethodID image_info_mid;
    jmethodID image_decode_mid;
    jclass    audio_class;
    jmethodID audio_info_mid;
    jmethodID audio_decode_mid;

    /*
     * HTTP, for javax.microedition.io.HttpConnection.  PlatformHttp.java wraps
     * HttpURLConnection; the runtime calls it once per request and buffers the
     * whole response, because that is the shape the MIDP API has (a MIDlet
     * asks for a response code and then reads a stream).  The request handle
     * is a Java-side long, never a pointer.  With no class resolved the hook
     * stays NULL and "http://" is refused by name, exactly as on a target with
     * no network stack.
     */
    jclass    http_class;
    jmethodID http_request_mid;
    jmethodID http_status_mid;
    jmethodID http_headers_mid;
    jmethodID http_body_mid;
    jmethodID http_close_mid;

    /*
     * JSR 256 sensors.  PlatformSensor decides which of this device's sensors
     * are worth offering (it maps Android sensor types onto the quantities a
     * MIDlet asks for by name) and keeps one listener per open handle, so the
     * runtime only ever holds an opaque long.
     *
     * The text of each list() line is parsed into these buffers rather than
     * into the heap, because nj_sensor_desc borrows its strings for the
     * duration of nj_port.sensor_list() only - and the runtime copies what it
     * keeps.  The buffers are sized by the same limits the runtime enforces
     * (16 descriptors, 4 channels), so a device with more sensors loses the
     * extras instead of overrunning anything.
     */
    jclass    sensor_class;
    jmethodID sensor_list_mid;
    jmethodID sensor_open_mid;
    jmethodID sensor_read_mid;
    jmethodID sensor_close_mid;
    char      sensor_url[NJ_SENSOR_MAX_DESCRIPTORS][NJ_SENSOR_MAX_URL];
    char      sensor_quantity[NJ_SENSOR_MAX_DESCRIPTORS][24];
    char      sensor_context[NJ_SENSOR_MAX_DESCRIPTORS][16];
    char      sensor_model[NJ_SENSOR_MAX_DESCRIPTORS][48];
    char      sensor_names[NJ_SENSOR_MAX_DESCRIPTORS][NJ_SENSOR_MAX_CHANNELS][16];
    char      sensor_units[NJ_SENSOR_MAX_DESCRIPTORS][NJ_SENSOR_MAX_CHANNELS][16];

    /*
     * Stream sockets.  PlatformSocket holds one java.net.Socket per open
     * connection and answers an opaque long, so the runtime never sees a file
     * descriptor and this port never sees the MIDlet's buffer.  Four of the
     * corpus titles open one; a port with no socket stack leaves the hooks
     * NULL and Connector.open() refuses socket:// by name instead.
     */
    jclass    socket_class;
    jmethodID socket_connect_mid;
    jmethodID socket_address_mid;
    jmethodID socket_port_mid;
    jmethodID socket_local_mid;
    jmethodID socket_local_port_mid;
    jmethodID socket_read_mid;
    jmethodID socket_write_mid;
    jmethodID socket_close_mid;

    /*
     * Bluetooth discovery, for javax.bluetooth.  PlatformBluetooth wraps the
     * platform's BluetoothAdapter and its discovery broadcast, and answers the
     * text shapes the hooks take.  Only the discovery half is bridged here:
     * the local adapter (name, address, class, discoverability), an inquiry and
     * the services known for a device.  The connection half - accept, read,
     * write - is left NULL, so a MIDlet that opens a server URL is told the
     * port cannot do it rather than being handed something that cannot work.
     *
     * Nothing here can be exercised on an emulator: there is no Bluetooth
     * adapter in one, which is why available() answering 0 is the path that is
     * actually tested.
     */
    jclass    bt_class;
    jmethodID bt_available_mid;
    jmethodID bt_address_mid;
    jmethodID bt_name_mid;
    jmethodID bt_class_mid;
    jmethodID bt_discoverable_mid;
    jmethodID bt_inquiry_start_mid;
    jmethodID bt_inquiry_devices_mid;
    jmethodID bt_inquiry_stop_mid;
    jmethodID bt_services_mid;
    jmethodID bt_listen_mid;
    jmethodID bt_accept_mid;
    jmethodID bt_connect_mid;
    jmethodID bt_read_mid;
    jmethodID bt_write_mid;
    jmethodID bt_close_mid;
    int       bt_discovering;
    char      bt_address[BT_ADDR_CAP];
    char      bt_name[BT_NAME_CAP];
    char      bt_dev_address[BT_DEV_MAX][BT_ADDR_CAP];
    char      bt_dev_name[BT_DEV_MAX][BT_NAME_CAP];
    nj_u4     bt_dev_class[BT_DEV_MAX];
    char      bt_svc_address[BT_ADDR_CAP];
    char      bt_svc_uuid[40];
    char      bt_svc_url[BT_SVC_MAX][192];
    char      bt_svc_name[BT_SVC_MAX][BT_NAME_CAP];
    nj_u1     bt_svc_uuid_bytes[BT_SVC_MAX][16];
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

/*
 * Called by the scheduler every quantum.  When the app is in the background
 * this is where the VM thread waits: the frame loop, the MIDlet's threads and
 * the audio all stop advancing, the CPU drops to nothing, and resuming is
 * instant because nothing was torn down.  Sleeping rather than spinning keeps a
 * suspended game from showing up as a background battery drain.
 */
static void android_idle(void *ud)
{
    android_state *s = AS(ud);
    while (s->paused && !s->quit) {
        android_sleep_ms(ud, 10);
    }
}

/*
 * Local wall clock, for java.util.Date and Calendar.  Without this the runtime
 * answers in UTC and a game that prints a timestamp prints the wrong one.
 */
static int android_now(void *ud, nj_datetime *out)
{
    struct timespec ts;
    struct tm tmv;
    (void)ud;
    if (!out) {
        return 0;
    }
    clock_gettime(CLOCK_REALTIME, &ts);
    if (!localtime_r(&ts.tv_sec, &tmv)) {
        return 0;
    }
    out->year = tmv.tm_year + 1900;
    out->month = tmv.tm_mon;
    out->day = tmv.tm_mday;
    out->hour = tmv.tm_hour;
    out->minute = tmv.tm_min;
    out->second = tmv.tm_sec;
    out->millis = (nj_s4)(ts.tv_nsec / 1000000);
    out->weekday = tmv.tm_wday;
    out->tz_offset = (nj_s4)(tmv.tm_gmtoff / 60);   /* bionic has tm_gmtoff */
    return 1;
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

    /*
     * The MIDlet gets a FIXED screen, not the Surface.
     *
     * A J2ME game was written for a specific device screen and many of them
     * check: Tumbleweed throws "Screen too large" above 250 in either axis.  A
     * modern phone reports 1080x2400, so handing the game the window means every
     * such title refuses to start - which is exactly what happened the first
     * time this port ran a real one.
     *
     * So the runtime sees s->width x s->height (240x320 by default, set by the
     * caller through nj_port_android_ex) and display_present() scales that into
     * whatever the Surface happens to be.
     */
    if (s->window) {
        s->win_w = ANativeWindow_getWidth(s->window);
        s->win_h = ANativeWindow_getHeight(s->window);
        ANativeWindow_setBuffersGeometry(s->window, s->win_w, s->win_h,
                                         WINDOW_FORMAT_RGBA_8888);
    }
    if (s->width <= 0) {
        s->width = NJ_SCREEN_WIDTH;
    }
    if (s->height <= 0) {
        s->height = NJ_SCREEN_HEIGHT;
    }
    /*
     * Say what the game will actually see.  "It started and shows an Alert"
     * and "it started with a 640x1280 screen" look identical from outside, and
     * the difference cost an hour: the port was handing the MIDlet the Surface
     * because nj_port_android_set_window() overwrote the logical size.
     */
    {
        char b[128];
        snprintf(b, sizeof(b), "display_init: logical %dx%d, window %dx%d",
                 s->width, s->height, s->win_w, s->win_h);
        __android_log_write(ANDROID_LOG_INFO, "nj2me", b);
    }
    s->format = NJ_PIXFMT;
    s->stride = s->width * bpp;

    free(s->shadow);
    s->shadow = (nj_u1 *)calloc(1u, (size_t)s->stride * (size_t)s->height);
    if (!s->shadow) {
        return -1;
    }
    /*
     * The frame the detection compares against, and a fresh box.  Only
     * display_present() uses these; if the allocation fails the port simply
     * has no auto-detect, which is a convenience, not a display.
     */
    free(s->det_prev);
    s->det_prev = (nj_u1 *)calloc(1u, (size_t)s->stride * (size_t)s->height);
    s->det_x = 0;
    s->det_y = 0;
    s->det_w = 0;
    s->det_h = 0;
    s->det_frames = 0;
    s->det_done = 0;
    s->det_raw_x = 0;
    s->det_raw_y = 0;
    s->det_raw_w = 0;
    s->det_raw_h = 0;
    s->det_raw_same = 0;
    *w = s->width;
    *h = s->height;
    *stride = s->stride;
    *px = s->shadow;
    return NJ_PIXFMT;
}

/*
 * One logical pixel as the window wants it.
 *
 * The window's byte order is not something this port may assume away.
 * WINDOW_FORMAT_RGBA_8888 is R,G,B,A in memory starting at the lowest address,
 * so the 32-bit value to store is 0xAABBGGRR.  The natural-looking
 * 0xffRRGGBB swaps red and blue on the panel, and no other check in the tree
 * can see that: the rasteriser really does put red in the high bits of its
 * 5-6-5 word, the headless frame dump really does print those bytes, and both
 * are right.  It is this one store that decides the colour - and an RGB_565
 * window wants red back in the high bits, the opposite packing.
 * tools/make_colordemo_jar.sh paints pure primaries left to right so that this
 * is a measurement (tools/sample_screenshot.py) instead of an opinion.
 */
static nj_u4 android_pixel(const android_state *s, int wide, int lx, int ly)
{
    const nj_u1 *src = s->shadow + (size_t)ly * (size_t)s->stride;
    nj_u4 r, g, b;

    if (s->format == NJ_PIXFMT_ARGB8888) {
        nj_u4 c4 = ((const nj_u4 *)src)[lx];
        r = (c4 >> 16) & 0xffu;
        g = (c4 >> 8) & 0xffu;
        b = c4 & 0xffu;
    } else {
        nj_u2 c = ((const nj_u2 *)src)[lx];
        nj_u4 r5 = (nj_u4)((c >> 11) & 0x1fu);
        nj_u4 g6 = (nj_u4)((c >> 5) & 0x3fu);
        nj_u4 b5 = (nj_u4)(c & 0x1fu);
        /* Always carry 8 bits per channel and narrow again at the end: 5-6-5 ->
         * 8 -> 5-6-5 is exact (the expand replicates the top bits), and one
         * shape for both paths cannot get the second one wrong. */
        r = (r5 << 3) | (r5 >> 2);
        g = (g6 << 2) | (g6 >> 4);
        b = (b5 << 3) | (b5 >> 2);
    }
    if (wide) {
        return 0xff000000u | (b << 16) | (g << 8) | r;
    }
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

/*
 * The same pixel android_pixel() packed, back in three 8-bit channels.
 *
 * Only the anti-aliased path needs this: mixing two pixels means mixing their
 * channels, and the packing differs between an RGBA_8888 window and a 5-6-5
 * one.  The expansion replicates the top bits, exactly as android_pixel()
 * does, so a round trip through here is lossless in both formats.
 */
static void android_channels(nj_u4 c, int wide, nj_u4 *r, nj_u4 *g, nj_u4 *b)
{
    if (wide) {
        *r = c & 0xffu;
        *g = (c >> 8) & 0xffu;
        *b = (c >> 16) & 0xffu;
    } else {
        nj_u4 r5 = (c >> 11) & 0x1fu;
        nj_u4 g6 = (c >> 5) & 0x3fu;
        nj_u4 b5 = c & 0x1fu;

        *r = (r5 << 3) | (r5 >> 2);
        *g = (g6 << 2) | (g6 >> 4);
        *b = (b5 << 3) | (b5 >> 2);
    }
}

static nj_u4 android_pack(int wide, nj_u4 r, nj_u4 g, nj_u4 b)
{
    if (wide) {
        return 0xff000000u | ((b & 0xffu) << 16) | ((g & 0xffu) << 8)
               | (r & 0xffu);
    }
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

/*
 * Bilinear sample of the logical screen, in source coordinates.
 *
 * This is what the picture anti-aliasing switch buys: at a fractional scale a
 * 240-pixel-wide screen stretched over 1080 pixels has four and a half output
 * pixels per source pixel, and nearest neighbour makes every source pixel a
 * visible slab.  Mixing the four neighbours softens the slabs at the cost of
 * being the only per-pixel multiply in the presenter - which is why it is a
 * switch the player owns and not the default.
 *
 * fx/fy are in logical pixels and already carry the detection crop; the
 * half-pixel offset puts the sample at the centre of the output pixel, the
 * usual convention, so a 1:1 stretch is the identity and turning the switch on
 * with a whole-number scale (which never reaches here) would change nothing.
 */
static nj_u4 android_sample_linear(const android_state *s, int wide,
                                   float fx, float fy, int sx0, int sy0,
                                   int sw, int sh)
{
    int x0, y0, x1, y1, wx, wy, lastx, lasty;
    nj_u4 r00, g00, b00, r10, g10, b10, r01, g01, b01, r11, g11, b11;
    nj_u4 rt, gt, bt, rb, gb, bb, r, g, b;

    if (sw < 1 || sh < 1) {
        return 0u;
    }
    if (!(fx >= 0.0f)) {
        fx = 0.0f;                  /* also catches NaN from a zero scale */
    }
    if (!(fy >= 0.0f)) {
        fy = 0.0f;
    }
    lastx = sx0 + sw - 1;
    lasty = sy0 + sh - 1;
    if (fx < (float)sx0) {
        fx = (float)sx0;
    } else if (fx > (float)lastx) {
        fx = (float)lastx;
    }
    if (fy < (float)sy0) {
        fy = (float)sy0;
    } else if (fy > (float)lasty) {
        fy = (float)lasty;
    }
    x0 = (int)fx;
    y0 = (int)fy;
    x1 = (x0 + 1 <= lastx) ? x0 + 1 : x0;
    y1 = (y0 + 1 <= lasty) ? y0 + 1 : y0;
    wx = (int)((fx - (float)x0) * 256.0f);
    wy = (int)((fy - (float)y0) * 256.0f);
    if (wx < 0) {
        wx = 0;
    } else if (wx > 255) {
        wx = 255;
    }
    if (wy < 0) {
        wy = 0;
    } else if (wy > 255) {
        wy = 255;
    }

    android_channels(android_pixel(s, wide, x0, y0), wide, &r00, &g00, &b00);
    android_channels(android_pixel(s, wide, x1, y0), wide, &r10, &g10, &b10);
    android_channels(android_pixel(s, wide, x0, y1), wide, &r01, &g01, &b01);
    android_channels(android_pixel(s, wide, x1, y1), wide, &r11, &g11, &b11);

    rt = (r00 * (nj_u4)(256 - wx) + r10 * (nj_u4)wx) >> 8;
    gt = (g00 * (nj_u4)(256 - wx) + g10 * (nj_u4)wx) >> 8;
    bt = (b00 * (nj_u4)(256 - wx) + b10 * (nj_u4)wx) >> 8;
    rb = (r01 * (nj_u4)(256 - wx) + r11 * (nj_u4)wx) >> 8;
    gb = (g01 * (nj_u4)(256 - wx) + g11 * (nj_u4)wx) >> 8;
    bb = (b01 * (nj_u4)(256 - wx) + b11 * (nj_u4)wx) >> 8;

    r = (rt * (nj_u4)(256 - wy) + rb * (nj_u4)wy) >> 8;
    g = (gt * (nj_u4)(256 - wy) + gb * (nj_u4)wy) >> 8;
    b = (bt * (nj_u4)(256 - wy) + bb * (nj_u4)wy) >> 8;
    return android_pack(wide, r, g, b);
}

/*
 * Grow the box of pixels the MIDlet has painted, one present at a time.
 *
 * This is the answer to "what resolution is this game, really?".  A title
 * written for 128x128 gets the 240x320 canvas every other title gets and paints
 * in a corner of it; nothing in the jar says so (6 of 395 declare a size), but
 * the pixels do.
 *
 * Rows are compared whole first: most of a frame is usually identical to the
 * one before it, so only the rows that differ are scanned for their first and
 * last changed pixel.
 */
static void android_detect(android_state *s)
{
    int bpp = android_bytes_per_pixel(s->format);
    size_t rowbytes = (size_t)s->width * (size_t)bpp;
    int bx0 = s->width, by0 = s->height, bx1 = -1, by1 = -1;
    int y;

    if (!s->det_prev || !s->shadow || s->width <= 0 || s->height <= 0) {
        s->det_done = 1;
        return;
    }
    for (y = 0; y < s->height; y++) {
        const nj_u1 *cur = s->shadow + (size_t)y * (size_t)s->stride;
        const nj_u1 *prv = s->det_prev + (size_t)y * (size_t)s->stride;
        int x;

        if (memcmp(cur, prv, rowbytes) == 0) {
            continue;
        }
        if (y < by0) { by0 = y; }
        if (y > by1) { by1 = y; }
        for (x = 0; x < s->width; x++) {
            if (memcmp(cur + (size_t)x * (size_t)bpp,
                       prv + (size_t)x * (size_t)bpp, (size_t)bpp) != 0) {
                if (x < bx0) { bx0 = x; }
                break;
            }
        }
        for (x = s->width - 1; x >= 0; x--) {
            if (memcmp(cur + (size_t)x * (size_t)bpp,
                       prv + (size_t)x * (size_t)bpp, (size_t)bpp) != 0) {
                if (x > bx1) { bx1 = x; }
                break;
            }
        }
    }
    if (bx1 >= bx0 && by1 >= by0) {
        int x0 = bx0, y0 = by0, x1 = bx1, y1 = by1;

        if (s->det_raw_w > 0 && s->det_raw_h > 0) {
            if (s->det_raw_x < x0) { x0 = s->det_raw_x; }
            if (s->det_raw_y < y0) { y0 = s->det_raw_y; }
            if (s->det_raw_x + s->det_raw_w - 1 > x1) {
                x1 = s->det_raw_x + s->det_raw_w - 1;
            }
            if (s->det_raw_y + s->det_raw_h - 1 > y1) {
                y1 = s->det_raw_y + s->det_raw_h - 1;
            }
        }
        s->det_raw_x = x0;
        s->det_raw_y = y0;
        s->det_raw_w = x1 - x0 + 1;
        s->det_raw_h = y1 - y0 + 1;
    }
    memcpy(s->det_prev, s->shadow, rowbytes * (size_t)s->height);
    s->det_frames++;

    /*
     * Publish the union as a screen - or publish nothing.
     *
     * The union says what the MIDlet touched; it does not say what screen it
     * thinks it is on.  A title that never repaints its status bar, a menu that
     * only draws a corner and a game with a black background and a small HUD
     * all leave a box that is real and is not a canvas, and cropping to one of
     * those zooms the game into a strip of itself.  nj_detect_snap() answers
     * with the handset screen the box most likely is, and refuses when it is
     * not one - in which case this keeps measuring instead of guessing.
     */
    if (s->det_raw_w > 0 && s->det_raw_h > 0) {
        int px = 0, py = 0, pw = 0, ph = 0;

        if (nj_detect_snap(s->det_raw_x, s->det_raw_y, s->det_raw_w, s->det_raw_h,
                           s->width, s->height, &px, &py, &pw, &ph)) {
            if (px == s->det_x && py == s->det_y
                    && pw == s->det_w && ph == s->det_h) {
                s->det_raw_same++;
            } else {
                s->det_x = px;
                s->det_y = py;
                s->det_w = pw;
                s->det_h = ph;
                s->det_raw_same = 0;
            }
            /*
             * Stop when there is nothing left to learn: a game that paints the
             * whole screen has said its resolution is the canvas, and one that
             * paints less has held a screen-sized box still for 300 frames -
             * ten seconds at the frame cap, long enough for a title that draws
             * its screen in two passes.
             */
            if (s->det_w >= s->width - 1 && s->det_h >= s->height - 1) {
                s->det_done = 1;
            } else if (s->det_raw_same >= 300) {
                s->det_done = 1;
            }
        } else if (s->det_frames >= 3000) {
            /* Nothing in the table is close after 100 seconds: that box is a
             * corner, and a picture cropped to it would be a bug report. */
            s->det_done = 1;
        }
    }
}

/*
 * Throw away every cached glyph.
 *
 * The cache key says nothing about the typeface or the anti-aliasing it was
 * drawn with, because it did not have to: the UI used to be unable to change
 * either.  Now it can, so the UI asks for this instead (it sets the word, the
 * frame does the work) and the next paint rasterises with the new settings.
 */
static void android_glyph_cache_flush(android_state *s)
{
    android_glyph_cache *c = s->glyph_cache;
    int i;

    if (!c) {
        return;
    }
    for (i = 0; i < ANDROID_GLYPH_CACHE; i++) {
        memset(&c->slot[i], 0, sizeof c->slot[i]);
    }
    c->next = 0;
}

static void android_display_present(void *ud, int x, int y, int w, int h)
{
    android_state *s = AS(ud);
    ANativeWindow *win;
    ANativeWindow_Buffer buf;
    (void)x; (void)y; (void)w; (void)h;

    /*
     * A settings change (font file, anti-aliasing) asks for the glyph cache to
     * be dropped.  Doing it here rather than in the setter keeps the cache the
     * run thread's alone, the same way the speed word is the UI's to write and
     * the frame's to apply.
     */
    if (s->glyph_flush) {
        s->glyph_flush = 0;
        android_glyph_cache_flush(s);
    }

    /*
     * Take a reference for the duration of the frame rather than holding the
     * lock across it.  The window can be replaced at any time - the activity
     * destroys its surface when the app goes to the background and creates a
     * new one coming back - and holding the lock through a blit would make the
     * UI thread wait for a frame; releasing the reference under it would leave
     * this frame writing into a window the activity had already given up.
     */
    pthread_mutex_lock(&s->lock);
    win = s->window;
    if (win) {
        ANativeWindow_acquire(win);
    }
    pthread_mutex_unlock(&s->lock);
    if (!win || s->quit) {
        if (win) {
            ANativeWindow_release(win);
        }
        return;
    }
    if (ANativeWindow_lock(win, &buf, NULL) != 0) {
        ANativeWindow_release(win);
        return;
    }
    /*
     * Decide the picture: which logical pixels, at what scale, and where.
     *
     * The default is the largest INTEGER factor that fits, centred, with the
     * leftover as black bars - integer because a J2ME game's art is pixel art
     * and a fractional scale turns every sprite edge into a blur, centred
     * because stretching to the panel's aspect ratio would distort the game.
     *
     * On top of that the player gets three things this port cannot guess:
     * 铺满 (use the whole screen, fractional scale), 自定义 (a zoom and a pan),
     * and 适配画面 (crop to the area the MIDlet actually paints and scale that,
     * which is what a title written for 128x128 needs on a 240x320 canvas).
     */
    if (!s->det_done) {
        android_detect(s);
    }
    {
        int sx0 = 0, sy0 = 0, sw = s->width, sh = s->height;
        int fit, ox, oy, drawn_w, drawn_h;
        float scale;

        if (s->view_mode == NJ_VIEW_AUTO && s->det_w >= 16 && s->det_h >= 16
                && (long)s->det_w * s->det_h
                       < (long)s->width * (long)s->height / 10L * 9L) {
            sx0 = s->det_x;
            sy0 = s->det_y;
            sw = s->det_w;
            sh = s->det_h;
        }
        /* the largest whole-number scale that fits the picture */
        {
            int fx = 1, fy = 1;
            if (s->win_w > 0 && s->win_h > 0 && sw > 0 && sh > 0) {
                fx = s->win_w / sw;
                fy = s->win_h / sh;
            }
            fit = (fx < fy) ? fx : fy;
            if (fit < 1) {
                fit = 1;        /* smaller than the game: show the top-left */
            }
        }
        if (s->view_mode == NJ_VIEW_FILL) {
            float fx = (float)buf.width / (float)sw;
            float fy = (float)buf.height / (float)sh;
            scale = (fx < fy) ? fx : fy;
            if (!(scale > 0.0f)) {
                scale = 1.0f;
            }
        } else {
            scale = (float)fit;
        }
        /*
         * The zoom and the pan are the player's in every mode, not just in
         * NJ_VIEW_CUSTOM: choosing 整数倍 / 铺满 / 适配 decides the base fit,
         * it does not take the picture away.  A player whose game sits under a
         * notch, or who wants two more pixels of the sprite sheet on screen,
         * would otherwise have to give up the crisp integer scaling to move it.
         */
        {
            float z = (s->view_zoom > 0.05f) ? s->view_zoom : 1.0f;
            if (z != 1.0f) {
                scale *= z;
            }
        }
        drawn_w = (int)((float)sw * scale);
        drawn_h = (int)((float)sh * scale);
        if (drawn_w < 1) { drawn_w = 1; }
        if (drawn_h < 1) { drawn_h = 1; }
        ox = (buf.width - drawn_w) / 2;
        oy = (buf.height - drawn_h) / 2;
        ox += (int)s->view_dx;
        oy += (int)s->view_dy;
        /* Keep the picture reachable.  One that fits may be parked anywhere
         * inside the window; one that does not is clamped so that no edge can
         * be dragged past the panel and lost. */
        if (drawn_w <= buf.width) {
            if (ox < 0) { ox = 0; }
            if (ox > buf.width - drawn_w) { ox = buf.width - drawn_w; }
        } else if (ox > 0) {
            ox = 0;
        } else if (ox < buf.width - drawn_w) {
            ox = buf.width - drawn_w;
        }
        if (drawn_h <= buf.height) {
            if (oy < 0) { oy = 0; }
            if (oy > buf.height - drawn_h) { oy = buf.height - drawn_h; }
        } else if (oy > 0) {
            oy = 0;
        } else if (oy < buf.height - drawn_h) {
            oy = buf.height - drawn_h;
        }

        /* Publish the transform for nj_port_android_touch() before the frame
         * reaches the panel: a tap that arrives between the two would otherwise
         * be mapped with the previous geometry. */
        pthread_mutex_lock(&s->lock);
        s->view_scale = scale;
        s->view_off_x = (float)ox;
        s->view_off_y = (float)oy;
        s->view_src_x = sx0;
        s->view_src_y = sy0;
        /*
         * How far the pan may go from here, published with the transform so
         * nj_port_android_view_limits() and the picture agree by construction.
         * A picture that fits may be parked anywhere inside the window
         * (half the slack either way); one that does not keeps an edge on the
         * panel, which is the same number measured from the centre.
         */
        s->view_max_pan_x = (buf.width > drawn_w ? buf.width - drawn_w
                                                 : drawn_w - buf.width) / 2;
        s->view_max_pan_y = (buf.height > drawn_h ? buf.height - drawn_h
                                                  : drawn_h - buf.height) / 2;
        s->view_limits_valid = 1;
        pthread_mutex_unlock(&s->lock);

        /*
         * The window's byte order is not something this port may assume away.
         *
         * WINDOW_FORMAT_RGBA_8888 is R,G,B,A in memory starting at the lowest
         * address, so the 32-bit value to store is 0xAABBGGRR.  The
         * natural-looking 0xffRRGGBB swaps red and blue on the panel, and no
         * other check in the tree can see that: the rasteriser really does put
         * red in the high bits of its 5-6-5 word, the headless frame dump
         * really does print those bytes, and both are right.  It is this one
         * store that decides the colour - and an RGB_565 window wants red back
         * in the high bits, the opposite packing.  tools/make_colordemo_jar.sh
         * paints pure primaries left to right so that this is a measurement
         * (tools/sample_screenshot.py) instead of an opinion.
         */
        {
            int wide = (buf.format == WINDOW_FORMAT_RGB_565) ? 0 : 1;
            size_t bpp = wide ? 4u : 2u;
            int row, col;
            /* The destination is clipped by hand when a custom zoom makes the
             * picture larger than the window: the row stride is the window's,
             * so a block that starts off-screen must not be written. */
            int cx0 = (ox < 0) ? -ox : 0;
            int cy0 = (oy < 0) ? -oy : 0;
            int cx1 = (ox + drawn_w > buf.width) ? buf.width - ox : drawn_w;
            int cy1 = (oy + drawn_h > buf.height) ? buf.height - oy : drawn_h;

            if (buf.format != s->logged_format) {
                char msg[96];
                s->logged_format = buf.format;
                snprintf(msg, sizeof(msg),
                         "window buffer %dx%d stride %d format %d, %d bytes/px",
                         buf.width, buf.height, buf.stride, (int)buf.format,
                         (int)bpp);
                android_log(NULL, 2, msg);
            }

            /* Black bars first, so the areas the game does not cover are not
             * whatever the compositor last left there. */
            for (row = 0; row < buf.height; row++) {
                memset((nj_u1 *)buf.bits + (size_t)row * (size_t)buf.stride * bpp,
                       0, (size_t)buf.width * bpp);
            }

            if (scale == (float)fit) {
                /* Whole-number scale: one logical pixel becomes a solid block.
                 * This is the default and by far the cheapest path. */
                int dy;
                for (row = 0; row < sh; row++) {
                    for (dy = 0; dy < fit; dy++) {
                        int ry = row * fit + dy;
                        nj_u1 *drow;
                        if (ry < cy0 || ry >= cy1) {
                            continue;
                        }
                        drow = (nj_u1 *)buf.bits
                               + (size_t)(oy + ry) * (size_t)buf.stride * bpp;
                        for (col = 0; col < sw; col++) {
                            nj_u4 px = android_pixel(s, wide, sx0 + col,
                                                     sy0 + row);
                            int bx = col * fit;
                            int dx0 = (cx0 > bx) ? cx0 : bx;
                            int dx1 = (cx1 < bx + fit) ? cx1 : bx + fit;
                            int dx;
                            if (wide) {
                                nj_u4 *dst = (nj_u4 *)drow;
                                for (dx = dx0; dx < dx1; dx++) {
                                    dst[ox + dx] = px;
                                }
                            } else {
                                nj_u2 *dst = (nj_u2 *)drow;
                                for (dx = dx0; dx < dx1; dx++) {
                                    dst[ox + dx] = (nj_u2)px;
                                }
                            }
                        }
                    }
                }
            } else if (s->view_filter) {
                /* Fractional scale, smoothed (画面反锯齿 on): mix the four
                 * neighbours of every output pixel.  Whole-number scales stay
                 * on the branch above, because a block of a solid colour is
                 * exact and no filter can improve it. */
                int dy;
                for (dy = cy0; dy < cy1; dy++) {
                    nj_u1 *drow = (nj_u1 *)buf.bits
                                  + (size_t)(oy + dy) * (size_t)buf.stride * bpp;
                    float fy = (float)sy0 + ((float)dy + 0.5f) / scale - 0.5f;
                    int dx;
                    for (dx = cx0; dx < cx1; dx++) {
                        float fx = (float)sx0 + ((float)dx + 0.5f) / scale
                                   - 0.5f;
                        nj_u4 px = android_sample_linear(s, wide, fx, fy,
                                                         sx0, sy0, sw, sh);
                        if (wide) {
                            ((nj_u4 *)drow)[ox + dx] = px;
                        } else {
                            ((nj_u2 *)drow)[ox + dx] = (nj_u2)px;
                        }
                    }
                }
            } else {
                /* Fractional scale (铺满 and 自定义): nearest neighbour, one
                 * destination pixel at a time.  A J2ME screen is small and a
                 * phone panel is not, so this is the expensive path - which is
                 * why it is not the default. */
                int dy;
                for (dy = cy0; dy < cy1; dy++) {
                    int sy = sy0 + (int)((float)dy / scale);
                    nj_u1 *drow = (nj_u1 *)buf.bits
                                  + (size_t)(oy + dy) * (size_t)buf.stride * bpp;
                    int dx;
                    if (sy > sy0 + sh - 1) { sy = sy0 + sh - 1; }
                    if (sy < sy0) { sy = sy0; }
                    for (dx = cx0; dx < cx1; dx++) {
                        int sx = sx0 + (int)((float)dx / scale);
                        nj_u4 px;
                        if (sx > sx0 + sw - 1) { sx = sx0 + sw - 1; }
                        if (sx < sx0) { sx = sx0; }
                        px = android_pixel(s, wide, sx, sy);
                        if (wide) {
                            ((nj_u4 *)drow)[ox + dx] = px;
                        } else {
                            ((nj_u2 *)drow)[ox + dx] = (nj_u2)px;
                        }
                    }
                }
            }
        }
    }
    ANativeWindow_unlockAndPost(win);
    ANativeWindow_release(win);
    s->frames_presented++;
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */
static int android_input_poll(void *ud, nj_event *ev)
{
    android_state *s = AS(ud);
    int have = 0;

    if (s->speed_percent > 0 && s->speed_percent != s->speed_applied && s->vm) {
        nj_vm_set_speed(s->vm, s->speed_percent);
        s->speed_applied = s->speed_percent;
        /*
         * Logged because the multiplier is invisible in a screenshot: the
         * frame counter is what the device can draw, not what the MIDlet's
         * clock says, so a player reporting "3x does nothing" has nothing to
         * point at.  This line is that evidence.
         */
        __android_log_print(ANDROID_LOG_INFO, "nj2me",
                            "speed: MIDlet time runs at %d%%", s->speed_percent);
    }
    /*
     * A snapshot the player asked for.  Taken here and not in the JNI call
     * because this is the VM's own thread at the top of a frame: the object
     * arena, every Java stack and the scheduler are all in a state the blob can
     * record, and no native call is in flight.
     */
    pthread_mutex_lock(&s->lock);
    if (s->save_req) {
        s->save_req = 0;
        s->save_busy = 1;
        pthread_mutex_unlock(&s->lock);
        {
            int rc = s->vm ? nj_vm_save_file(s->vm, s->save_path) : NJ_ERR_STATE;
            __android_log_print(rc == NJ_OK ? ANDROID_LOG_INFO : ANDROID_LOG_WARN,
                                "nj2me", "state: save to %s -> %d", s->save_path, rc);
            pthread_mutex_lock(&s->lock);
            s->save_rc = rc;
            s->save_busy = 0;
            s->save_done = 1;
            pthread_mutex_unlock(&s->lock);
        }
    } else {
        pthread_mutex_unlock(&s->lock);
    }
    pthread_mutex_lock(&s->lock);
    if (s->qh != s->qt) {
        *ev = s->queue[s->qh];
        s->qh = (s->qh + 1) % (int)(sizeof(s->queue) / sizeof(s->queue[0]));
        have = 1;
    }
    pthread_mutex_unlock(&s->lock);
    return have;
}

/* Caller holds s->lock. */
static int android_push_locked(android_state *s, const nj_event *ev)
{
    int next = (s->qt + 1) % (int)(sizeof(s->queue) / sizeof(s->queue[0]));

    if (next == s->qh) {
        /* Full.  Dropping the newest beats dropping the oldest: the event the
         * game has not seen yet is the one it can still do without, while the
         * key-down it is waiting on may be what unblocks it. */
        return 0;
    }
    s->queue[s->qt] = *ev;
    s->qt = next;
    return 1;
}

static void android_push(void *ud, const nj_event *ev)
{
    android_state *s = AS(ud);
    pthread_mutex_lock(&s->lock);
    android_push_locked(s, ev);
    pthread_mutex_unlock(&s->lock);
}

/* Queue depth the harness gets to see: the runtime's input_push hook. */
static int android_input_push(void *ud, const nj_event *ev)
{
    android_state *s = AS(ud);
    int taken;
    pthread_mutex_lock(&s->lock);
    taken = android_push_locked(s, ev);
    pthread_mutex_unlock(&s->lock);
    return taken;
}

/*
 * The key table.  ONE table, two users: push_key() translates a hardware key
 * through it, and the keypad editor in Java asks for it over JNI so that the
 * buttons it offers are exactly the codes this port understands.  A second
 * table in Java would be a second place to be wrong.
 */
typedef struct android_keymap_entry {
    int         android;   /* android.view.KeyEvent.KEYCODE_*, written out    */
    nj_u1       nj;        /* nj_key                                         */
    const char *label;     /* shown on the on-screen keypad                  */
} android_keymap_entry;

static const android_keymap_entry k_keymap[] = {
    { 19, NJ_KEY_UP,        "Up"     },   /* KEYCODE_DPAD_UP       */
    { 20, NJ_KEY_DOWN,      "Down"   },   /* KEYCODE_DPAD_DOWN     */
    { 21, NJ_KEY_LEFT,      "Left"   },   /* KEYCODE_DPAD_LEFT     */
    { 22, NJ_KEY_RIGHT,     "Right"  },   /* KEYCODE_DPAD_RIGHT    */
    { 23, NJ_KEY_FIRE,      "Fire"   },   /* KEYCODE_DPAD_CENTER   */
    { 66, NJ_KEY_FIRE,      "Fire"   },   /* KEYCODE_ENTER         */
    { 62, NJ_KEY_FIRE,      "Fire"   },   /* KEYCODE_SPACE         */
    {  4, NJ_KEY_SOFT_RIGHT,"Soft R" },   /* KEYCODE_BACK          */
    { 82, NJ_KEY_SOFT_LEFT, "Soft L" },   /* KEYCODE_MENU          */
    { 67, NJ_KEY_CLEAR,     "Clear"  },   /* KEYCODE_DEL           */
    {  7, NJ_KEY_0,         "0"      },   /* KEYCODE_0             */
    {  8, NJ_KEY_1,         "1"      },
    {  9, NJ_KEY_2,         "2"      },
    { 10, NJ_KEY_3,         "3"      },
    { 11, NJ_KEY_4,         "4"      },
    { 12, NJ_KEY_5,         "5"      },
    { 13, NJ_KEY_6,         "6"      },
    { 14, NJ_KEY_7,         "7"      },
    { 15, NJ_KEY_8,         "8"      },
    { 16, NJ_KEY_9,         "9"      },
    { 17, NJ_KEY_STAR,      "*"      },   /* KEYCODE_STAR          */
    { 18, NJ_KEY_POUND,     "#"      },   /* KEYCODE_POUND         */
    { 29, NJ_KEY_GAME_A,    "A"      },   /* KEYCODE_A             */
    { 30, NJ_KEY_GAME_B,    "B"      },
    { 31, NJ_KEY_GAME_C,    "C"      },
    { 32, NJ_KEY_GAME_D,    "D"      },
    /*
     * Volume.  These two exist for the on-screen pad: a player whose volume
     * rocker the game has swallowed, or whose phone sits in a grip that covers
     * it, can put 音量+ / 音量- on the pad and press them like any other key.
     * The runtime hands them to the MIDlet as keyPressed(24) / keyPressed(25),
     * the codes MIDP phones used for a volume key, so a title that implements
     * its own volume gets it.  The hardware rocker keeps doing what it has
     * always done here (GameActivity.dispatchKeyEvent): it changes the phone's
     * volume, because no J2ME title can be allowed to take that away.
     */
    { 24, NJ_KEY_VOLUP,     "Vol+"   },   /* KEYCODE_VOLUME_UP     */
    { 25, NJ_KEY_VOLDOWN,   "Vol-"   }    /* KEYCODE_VOLUME_DOWN   */
};

#define KEYMAP_N ((int)(sizeof(k_keymap) / sizeof(k_keymap[0])))

void nj_port_android_push_key(nj_port *port, int android_keycode, int down)
{
    nj_event ev;
    int k = NJ_KEY_NONE;
    int i;

    if (!port) {
        return;
    }
    for (i = 0; i < KEYMAP_N; i++) {
        if (k_keymap[i].android == android_keycode) {
            k = (int)k_keymap[i].nj;
            break;
        }
    }
    if (k == NJ_KEY_NONE) {
        return;
    }
    memset(&ev, 0, sizeof(ev));
    ev.type = (nj_u1)(down ? NJ_EV_KEY_DOWN : NJ_EV_KEY_UP);
    ev.key = (nj_u1)k;
    AS(port->ud)->key_events++;
    android_push(port->ud, &ev);
}

void nj_port_android_touch(nj_port *port, int pointer_id, int phase, float x,
                           float y)
{
    android_state *s;
    nj_event ev;
    float scale;
    int lx, ly;

    if (!port || !port->ud) {
        return;
    }
    s = AS(port->ud);

    pthread_mutex_lock(&s->lock);
    /*
     * One finger owns the MIDP pointer.  A second finger on the keypad must not
     * be able to wrench the pointer the first one is dragging, and MIDP has no
     * way to express two - so the extra fingers are simply not the pointer.
     */
    if (phase == NJ_ANDROID_TOUCH_DOWN) {
        if (s->ptr_id >= 0 && s->ptr_id != pointer_id) {
            pthread_mutex_unlock(&s->lock);
            return;
        }
        s->ptr_id = pointer_id;
    } else if (s->ptr_id != pointer_id) {
        pthread_mutex_unlock(&s->lock);
        return;
    }

    /*
     * Undo exactly what display_present() did: the source origin (non-zero when
     * the picture is cropped to the area the MIDlet paints), the offset in the
     * window, and the scale, which may be fractional.  A tap that lands in the
     * black bar around the picture is clamped onto the nearest edge rather than
     * dropped, because dragging a finger off the game area is a drag and the
     * game should see it reach the edge.
     */
    scale = (s->view_scale > 0.0f) ? s->view_scale : 1.0f;
    lx = s->view_src_x + (int)((x - s->view_off_x) / scale);
    ly = s->view_src_y + (int)((y - s->view_off_y) / scale);
    if (lx < 0) { lx = 0; }
    if (ly < 0) { ly = 0; }
    if (lx > s->width - 1)  { lx = s->width - 1; }
    if (ly > s->height - 1) { ly = s->height - 1; }

    if (phase == NJ_ANDROID_TOUCH_UP || phase == NJ_ANDROID_TOUCH_CANCEL) {
        s->ptr_id = -1;
    }
    pthread_mutex_unlock(&s->lock);

    memset(&ev, 0, sizeof(ev));
    switch (phase) {
    case NJ_ANDROID_TOUCH_DOWN: ev.type = NJ_EV_POINTER_DOWN; break;
    case NJ_ANDROID_TOUCH_MOVE: ev.type = NJ_EV_POINTER_MOVE; break;
    default:                    ev.type = NJ_EV_POINTER_UP;   break;
    }
    ev.x = (nj_s2)lx;
    ev.y = (nj_s2)ly;
    s->ptr_events++;
    android_push(port->ud, &ev);
}

/* ------------------------------------------------------------------ */
/* Storage: directory iteration                                        */
/* ------------------------------------------------------------------ */
/*
 * The file hooks are deliberately left NULL.
 *
 * The runtime already falls back to fopen/fread/fwrite for RecordStore and for
 * reading the JAR, and bionic's stdio is exactly what that fallback wants - so
 * a second implementation here would be a second implementation to keep
 * correct.  Directories are different: the runtime has no stdio fallback for
 * them, and leaving them out would make FileConnection.list() throw on a phone
 * that obviously has directories.  So this is the one piece of the filesystem
 * the port implements itself.
 */
typedef struct android_dir {
    DIR  *d;
    char  path[512];
} android_dir;

static int android_dir_open(void *ud, const char *path, void **out_handle)
{
    android_dir *h;
    (void)ud;

    if (!out_handle) {
        return NJ_ERR;
    }
    h = (android_dir *)calloc(1u, sizeof(*h));
    if (!h) {
        return NJ_ERR_NOMEM;
    }
    snprintf(h->path, sizeof(h->path), "%s", (path && path[0]) ? path : ".");
    h->d = opendir(h->path);
    if (!h->d) {
        free(h);
        return NJ_ERR;
    }
    *out_handle = h;
    return NJ_OK;
}

static int android_dir_next(void *ud, void *handle, char *name, nj_size cap,
                            int *is_dir)
{
    android_dir *h = (android_dir *)handle;
    struct dirent *e;
    (void)ud;

    if (!h || !h->d || !name || cap == 0) {
        return NJ_ERR;
    }
    while ((e = readdir(h->d)) != NULL) {
        char full[700];
        struct stat st;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        if (is_dir) {
            *is_dir = 0;
            snprintf(full, sizeof(full), "%s/%s", h->path, e->d_name);
            if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
                *is_dir = 1;
            }
        }
        snprintf(name, cap, "%s", e->d_name);
        return 1;
    }
    return 0;
}

static void android_dir_close(void *ud, void *handle)
{
    android_dir *h = (android_dir *)handle;
    (void)ud;
    if (h) {
        if (h->d) {
            closedir(h->d);
        }
        free(h);
    }
}

/* ------------------------------------------------------------------ */
/* Device properties, vibration                                        */
/* ------------------------------------------------------------------ */
static const char *android_device_property(void *ud, const char *key)
{
    (void)ud;
    if (!key) {
        return NULL;
    }
    /*
     * Games PROBE these without checking for NULL, so the keys a desktop port
     * answers are answered here too - and answered as what this device is,
     * which is a touchscreen with an on-screen keypad, not a handset with a
     * numeric keypad under the screen.
     */
    if (strcmp(key, "com.nokia.keyboard.type") == 0) {
        return "None";              /* no physical keypad: the UI draws one */
    }
    if (strcmp(key, "com.nokia.mid.ui.version") == 0) {
        return "1.1";
    }
    if (strcmp(key, "microedition.platform") == 0) {
        return "nano_j2me/android";
    }
    return NULL;                    /* honestly unknown */
}

/*
 * Vibration goes back into Java: Android's Vibrator is a framework service, and
 * the alternative would be an NDK binder client for one call.  The VM thread is
 * a plain pthread, so it has to attach before it can call in - and detach
 * again, because leaving a thread attached keeps it (and its class loader)
 * alive after the game is over.
 */
static void android_vibrate(void *ud, int ms)
{
    android_state *s = AS(ud);
    JNIEnv *env = NULL;
    int attached = 0;

    if (!s || !s->jvm || !s->activity_class || !s->vibrate_mid || ms <= 0) {
        return;
    }
    if ((*s->jvm)->GetEnv(s->jvm, (void **)&env, JNI_VERSION_1_6) ==
        JNI_EDETACHED) {
        if ((*s->jvm)->AttachCurrentThread(s->jvm, &env, NULL) != JNI_OK) {
            return;
        }
        attached = 1;
    }
    if (env) {
        (*env)->CallStaticVoidMethod(env, s->activity_class, s->vibrate_mid,
                                     (jint)ms);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);   /* a game must not die of a vibrate */
        }
    }
    if (attached) {
        (*s->jvm)->DetachCurrentThread(s->jvm);
    }
}

/* ------------------------------------------------------------------ */
/* Glyph provider                                                      */
/* ------------------------------------------------------------------ */
/*
 * Attach once, not per glyph: this runs on the thread that executes the VM,
 * and that thread lives as long as the game.  android_main_thread() detaches
 * it when the run is over.
 */
static JNIEnv *android_run_env(android_state *s)
{
    JNIEnv *env = NULL;

    if (s->run_env) {
        return s->run_env;
    }
    if (!s->jvm) {
        return NULL;
    }
    if ((*s->jvm)->GetEnv(s->jvm, (void **)&env, JNI_VERSION_1_6) ==
        JNI_EDETACHED) {
        if ((*s->jvm)->AttachCurrentThread(s->jvm, &env, NULL) != JNI_OK) {
            return NULL;
        }
        s->run_attached = 1;
    }
    s->run_env = env;
    return env;
}

static const nj_u1 *android_glyph_lookup(void *ud, nj_u2 ch, int *w, int *h)
{
    android_glyph_face *gf = (android_glyph_face *)ud;
    android_state *s = gf->state;
    android_glyph_cache *c;
    android_glyph_slot *slot = NULL;
    jbyte tmp[2 + ANDROID_GLYPH_CELL_MAX * ANDROID_GLYPH_CELL_MAX];
    jbyteArray arr;
    JNIEnv *env;
    jsize len;
    int i;
    int ww, hh;

    *w = 0;
    *h = 0;
    if (!s || !s->glyph_cache) {
        return NULL;
    }
    c = s->glyph_cache;
    c->calls++;
    for (i = 0; i < ANDROID_GLYPH_CACHE; i++) {
        if (c->slot[i].w > 0 && c->slot[i].cell == gf->cell &&
            c->slot[i].bold == gf->bold && c->slot[i].ch == ch) {
            slot = &c->slot[i];
            c->hits++;
            break;
        }
    }
    if (!slot) {
        env = android_run_env(s);
        if (!env || !s->glyph_class || !s->glyph_mid) {
            c->failed++;
            return NULL;
        }
        arr = (jbyteArray)(*env)->CallStaticObjectMethod(
            env, s->glyph_class, s->glyph_mid,
            (jint)ch, (jint)gf->cell, (jint)gf->bold);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            c->failed++;
            return NULL;
        }
        if (!arr) {
            c->failed++;
            return NULL;
        }
        len = (*env)->GetArrayLength(env, arr);
        if (len < 2 || len > (jsize)sizeof(tmp)) {
            (*env)->DeleteLocalRef(env, arr);
            c->failed++;
            return NULL;
        }
        (*env)->GetByteArrayRegion(env, arr, 0, len, tmp);
        (*env)->DeleteLocalRef(env, arr);

        ww = (int)(unsigned char)tmp[0];
        hh = (int)(unsigned char)tmp[1];
        if (ww <= 0 || hh <= 0 || ww > ANDROID_GLYPH_CELL_MAX ||
            hh > ANDROID_GLYPH_CELL_MAX || (int)len < 2 + ww * hh) {
            c->failed++;
            return NULL;
        }
        slot = &c->slot[c->next];
        c->next = (c->next + 1) % ANDROID_GLYPH_CACHE;
        slot->ch = ch;
        slot->cell = gf->cell;
        slot->bold = gf->bold;
        slot->w = ww;
        slot->h = hh;
        memcpy(slot->rows, tmp + 2, (size_t)(ww * hh));
        c->misses++;
    }

    gf->last_w = slot->w;
    *w = slot->w;
    *h = slot->h;
    return slot->rows;
}

static int android_glyph_advance(void *ud, nj_u2 ch)
{
    android_glyph_face *gf = (android_glyph_face *)ud;

    (void)ch;
    return gf->last_w > 0 ? gf->last_w : gf->cell;
}

/*
 * Build one platform face per slot out of the built-in face of that slot.
 *
 * The copy is the point: MIDP layout is measured against Font.getHeight(), so
 * a provider face that reported its own metrics would re-flow every screen the
 * MIDlet had already laid out.  Only the glyph hook changes.
 *
 * The copied face's fixed cell is then emptied, and that is the other half of
 * the point.  A face whose cell covers ASCII is asked for the cell's advance
 * first (nj_font.c char_width_depth) and drawn from the cell's bitmap, so with
 * the range intact every ASCII character was charged the bundled bitmap face's
 * fixed 8-pixel width and inked from its 1-bit table, while the platform only
 * ever drew the wide glyphs the cell could not reach.  With the range empty the
 * provider answers both questions for every character, which is what makes the
 * phone's typeface the only typeface on screen.
 *
 * That only holds once the face is installed as the *slot* (nj_run_opts
 * .primary_fonts): a fallback is consulted after the cell and would never see
 * an ASCII character at all.
 *
 * The consequence to know about: a character the platform refuses (returns
 * null) used to fall back to the bundled bitmap glyph; now it falls back to
 * nothing, because this face has no fallback of its own.  For the MIDP
 * repertoire that is the right trade - the platform draws all of it - and a
 * missing glyph shows as a blank of the right width rather than an 8-pixel
 * bitmap from a face that was supposed to be replaced.
 */
static void android_glyph_init(android_state *s)
{
    int i;

    if (s->glyph_cache || !s->glyph_mid) {
        return;   /* no platform glyphs: the built-in faces stay in charge */
    }
    s->glyph_cache = (android_glyph_cache *)calloc(1u, sizeof(*s->glyph_cache));
    if (!s->glyph_cache) {
        return;
    }
    for (i = 0; i < NJ_FONTSET_SLOTS; i++) {
        int si = i / NJ_FONT_SLOT_STYLES;
        int style = (i % NJ_FONT_SLOT_STYLES) ? NJ_FONT_STYLE_BOLD
                                              : NJ_FONT_STYLE_PLAIN;
        int size = si == 0 ? NJ_FONT_SIZE_SMALL
                           : (si == 1 ? NJ_FONT_SIZE_MEDIUM
                                      : NJ_FONT_SIZE_LARGE);
        const nj_font *b = nj_font_builtin(size, style);
        android_glyph_face *gf = &s->faces[i];

        gf->face = *b;
        gf->face.name = "android-system";
        gf->face.glyph_lookup = android_glyph_lookup;
        gf->face.glyph_ud = gf;
        gf->face.glyph_advance = android_glyph_advance;
        gf->face.coverage = 1;      /* the platform rasterises antialiased */
        gf->face.fallback = NULL;   /* this face IS the end of the chain */
        gf->face.first_char = 1;    /* empty range: every glyph comes from Java */
        gf->face.last_char = 0;
        gf->state = s;
        gf->cell = b->glyph_h > 0 && b->glyph_h <= ANDROID_GLYPH_CELL_MAX
                       ? b->glyph_h
                       : 12;
        gf->bold = style == NJ_FONT_STYLE_BOLD;
        gf->last_w = 0;
        s->face_ptrs[i] = &gf->face;
    }
}

/* ------------------------------------------------------------------ */
/* MIDI: the platform's own synthesizer                                */
/* ------------------------------------------------------------------ */
/*
 * Android has shipped a General MIDI synthesizer since API 21 - Sonivox, in
 * libsonivox.so, reachable through MediaPlayer (and through the MIDI
 * extractor the media framework registers).  That is the same engine the
 * Android J2ME loaders vendor as a native library, and it is what makes a
 * .mid sound like the music the composer wrote instead of four waveforms.
 *
 * The runtime does not know any of this: it calls nj_port.midi_* and, when
 * the port declines a file, renders it with src/midp/nj_midi.c exactly as it
 * does on the desktop.  So a device whose platform refuses MIDI (or a build
 * with the class missing) is silent in the way it always was, not broken.
 *
 * Everything here runs on the VM thread, which is not the UI thread: JNI
 * needs that thread attached (android_run_env does it once), and MidiPlayer
 * keeps the MediaPlayer objects on the Java side so no Java object is ever
 * held in C.
 */
static void *android_midi_open(void *ud, const void *data, nj_u4 len,
                               nj_u4 *duration_ms)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    jbyteArray arr;
    jlong id;
    jint dur;

    if (duration_ms) {
        *duration_ms = 0u;
    }
    if (!s || !s->midi_class || !s->midi_open_mid || data == NULL || len == 0u) {
        return NULL;
    }
    env = android_run_env(s);
    if (!env) {
        return NULL;
    }
    arr = (*env)->NewByteArray(env, (jsize)len);
    if (!arr) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    (*env)->SetByteArrayRegion(env, arr, 0, (jsize)len, (const jbyte *)data);
    id = (*env)->CallStaticLongMethod(env, s->midi_class, s->midi_open_mid,
                                      arr);
    (*env)->DeleteLocalRef(env, arr);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    if (id == 0) {
        /*
         * The runtime answers a refusal by rendering the file with its own
         * synth, so this is information, not a failure - but it is the one
         * line that explains why a game's music sounds like a square wave on
         * a device whose platform has no MIDI engine.
         */
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                            "midi: the platform declined %u bytes; "
                            "the runtime renders it", (unsigned)len);
        return NULL;                /* the platform would not take the file */
    }
    if (duration_ms && s->midi_length_mid) {
        dur = (*env)->CallStaticIntMethod(env, s->midi_class,
                                          s->midi_length_mid, id);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            dur = 0;
        }
        if (dur > 0) {
            *duration_ms = (nj_u4)dur;
        }
    }
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "midi: the platform synth took %u bytes, %u ms",
                        (unsigned)len,
                        (unsigned)(duration_ms ? *duration_ms : 0u));
    return (void *)(intptr_t)id;
}

static int android_midi_start(void *ud, void *handle, int loop)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    jboolean ok;

    if (!s || !s->midi_class || !s->midi_start_mid || handle == NULL) {
        return NJ_ERR_UNSUPPORTED;
    }
    env = android_run_env(s);
    if (!env) {
        return NJ_ERR_UNSUPPORTED;
    }
    ok = (*env)->CallStaticBooleanMethod(env, s->midi_class, s->midi_start_mid,
                                         (jlong)(intptr_t)handle,
                                         loop ? JNI_TRUE : JNI_FALSE);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return NJ_ERR;
    }
    return ok == JNI_TRUE ? 0 : NJ_ERR;
}

static void android_midi_stop(void *ud, void *handle)
{
    android_state *s = AS(ud);
    JNIEnv *env;

    if (!s || !s->midi_class || !s->midi_stop_mid || handle == NULL) {
        return;
    }
    env = android_run_env(s);
    if (!env) {
        return;
    }
    (*env)->CallStaticVoidMethod(env, s->midi_class, s->midi_stop_mid,
                                 (jlong)(intptr_t)handle);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
}

static void android_midi_close(void *ud, void *handle)
{
    android_state *s = AS(ud);
    JNIEnv *env;

    if (!s || !s->midi_class || !s->midi_close_mid || handle == NULL) {
        return;
    }
    env = android_run_env(s);
    if (!env) {
        return;
    }
    (*env)->CallStaticVoidMethod(env, s->midi_class, s->midi_close_mid,
                                 (jlong)(intptr_t)handle);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
}

static void android_midi_seek(void *ud, void *handle, nj_u4 ms)
{
    android_state *s = AS(ud);
    JNIEnv *env;

    if (!s || !s->midi_class || !s->midi_seek_mid || handle == NULL) {
        return;
    }
    env = android_run_env(s);
    if (!env) {
        return;
    }
    (*env)->CallStaticVoidMethod(env, s->midi_class, s->midi_seek_mid,
                                 (jlong)(intptr_t)handle, (jint)ms);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
}

static nj_u4 android_midi_time(void *ud, void *handle)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    jint ms;

    if (!s || !s->midi_class || !s->midi_time_mid || handle == NULL) {
        return 0u;
    }
    env = android_run_env(s);
    if (!env) {
        return 0u;
    }
    ms = (*env)->CallStaticIntMethod(env, s->midi_class, s->midi_time_mid,
                                     (jlong)(intptr_t)handle);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return 0u;
    }
    return ms > 0 ? (nj_u4)ms : 0u;
}

static void android_midi_volume(void *ud, void *handle, int volume)
{
    android_state *s = AS(ud);
    JNIEnv *env;

    if (!s || !s->midi_class || !s->midi_volume_mid || handle == NULL) {
        return;
    }
    env = android_run_env(s);
    if (!env) {
        return;
    }
    (*env)->CallStaticVoidMethod(env, s->midi_class, s->midi_volume_mid,
                                 (jlong)(intptr_t)handle, (jint)volume);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
}

static int android_midi_finished(void *ud, void *handle)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    jboolean done;

    if (!s || !s->midi_class || !s->midi_done_mid || handle == NULL) {
        return 1;   /* nothing to poll: the runtime stops waiting */
    }
    env = android_run_env(s);
    if (!env) {
        return 1;
    }
    done = (*env)->CallStaticBooleanMethod(env, s->midi_class, s->midi_done_mid,
                                           (jlong)(intptr_t)handle);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return 1;
    }
    return done == JNI_TRUE ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Audio implementation                                                */
/* ------------------------------------------------------------------ */
static int android_audio_ensure(android_state *s, android_audio *a);

static android_audio *android_audio_get(android_state *s)
{
    android_audio *a;

    if (s->audio) {
        return s->audio;
    }
    a = (android_audio *)calloc(1u, sizeof(*a));
    if (!a) {
        return NULL;
    }
    pthread_mutex_init(&a->lock, NULL);
    a->ring = (nj_s2 *)calloc((size_t)ANDROID_AUDIO_RING * 2u, sizeof(nj_s2));
    if (!a->ring) {
        pthread_mutex_destroy(&a->lock);
        free(a);
        return NULL;
    }
    s->audio = a;
    return a;
}

static void android_audio_close(android_state *s)
{
    android_audio *a = s->audio;

    if (!a) {
        return;
    }
    if (a->backend == 1 && a->stream) {
        a->aa.requestStop(a->stream);
        a->aa.close(a->stream);
        a->stream = NULL;
    }
    if (a->backend == 2) {
        if (a->sl_player_obj) {
            (*a->sl_player_obj)->Destroy(a->sl_player_obj);
            a->sl_player_obj = NULL;
        }
        if (a->sl_mix_obj) {
            (*a->sl_mix_obj)->Destroy(a->sl_mix_obj);
            a->sl_mix_obj = NULL;
        }
        if (a->sl_engine_obj) {
            (*a->sl_engine_obj)->Destroy(a->sl_engine_obj);
            a->sl_engine_obj = NULL;
        }
        free(a->sl_buf);
        a->sl_buf = NULL;
    }
    if (a->aa.lib) {
        dlclose(a->aa.lib);
        a->aa.lib = NULL;
    }
    a->backend = 0;
    free(a->ring);
    pthread_mutex_destroy(&a->lock);
    free(a);
    s->audio = NULL;
}

/*
 * Mix `frames` output frames: square-wave tone (if a sequence is playing) plus
 * the PCM ring (if a player is draining), each clamped on the way out.
 *
 * Resampling is nearest-neighbour on purpose.  Sampled audio in a MIDlet is a
 * jingle or an effect at 8-22 kHz; a linear interpolator would cost more than
 * it is worth here, and the ring is already the wrong shape for a good one.
 */
static void android_audio_render(android_audio *a, nj_s2 *dst, int frames,
                                 int out_ch)
{
    int i;
    nj_u4 pcm_step = (a->src_rate > 0 && a->out_rate > 0)
                   ? (nj_u4)(((nj_u8)a->src_rate << 16) / (nj_u8)a->out_rate)
                   : 65536u;

    for (i = 0; i < frames; i++) {
        int acc = 0;
        int pl = 0, pr = 0;

        if (a->tone_on) {
            while (a->left == 0 && a->tone_on) {
                if (a->tone_next >= a->tone_count) {
                    a->tone_on = 0;
                    break;
                }
                {
                    const nj_audio_tone *t = &a->tones[a->tone_next++];
                    nj_u4 f = t->freq_hz;
                    a->left = ((nj_u4)t->duration_ms * (nj_u4)a->out_rate) / 1000u;
                    a->phase = 0;
                    a->step = 0;
                    if (f > 0) {
                        /* Above Nyquist a square wave folds down into a tone
                         * that was never asked for; the top of the band is the
                         * honest ceiling. */
                        if (f > (nj_u4)(a->out_rate / 2)) {
                            f = (nj_u4)(a->out_rate / 2);
                        }
                        a->step = (nj_u4)(((nj_u8)f << 16) / (nj_u8)a->out_rate);
                    }
                }
            }
            if (a->tone_on && a->left > 0) {
                a->left--;
                if (a->step) {
                    acc += (a->phase < 32768u) ? ANDROID_TONE_AMP
                                               : -ANDROID_TONE_AMP;
                    a->phase += a->step;
                    if (a->phase >= 65536u) {
                        a->phase -= 65536u;
                    }
                }
            }
        }

        if (a->pcm_on && a->rd != a->wr) {
            const nj_s2 *src = a->ring + (size_t)a->rd * 2u;
            pl = src[0];
            pr = src[1];
            a->pcm_pos += pcm_step;
            while (a->pcm_pos >= 65536u) {
                a->pcm_pos -= 65536u;
                a->rd = (a->rd + 1) % ANDROID_AUDIO_RING;
                if (a->rd == a->wr) {
                    break;
                }
            }
        }

        {
            int l = acc + pl;
            int r = acc + pr;
            if (l > 32767)  { l = 32767; }
            if (l < -32768) { l = -32768; }
            if (r > 32767)  { r = 32767; }
            if (r < -32768) { r = -32768; }
            dst[(size_t)i * (size_t)out_ch] = (nj_s2)l;
            if (out_ch > 1) {
                dst[(size_t)i * (size_t)out_ch + 1u] = (nj_s2)r;
            }
            {
                int c;
                for (c = 2; c < out_ch; c++) {
                    dst[(size_t)i * (size_t)out_ch + (size_t)c] = (nj_s2)l;
                }
            }
        }
    }
    a->frames_rendered += frames;
}

/* ------------------------------------------------------------------ */
/* The modern device: AAudio, opened by hand                           */
/* ------------------------------------------------------------------ */
/*
 * dlsym through a void* slot rather than casting the result: the function
 * pointer types above are not compatible with void* by the letter of the
 * standard, and the cast is what every compiler warns about.
 */
#define AA_SYM(api, field, name) \
    do { *(void **)(&(api)->field) = dlsym((api)->lib, name); } while (0)

static int android_aaudio_load(android_aaudio *api)
{
    if (api->lib) {
        return NJ_OK;
    }
    if (api->tried) {
        return NJ_ERR_UNSUPPORTED;      /* asked once already */
    }
    api->tried = 1;
    api->lib = dlopen("libaaudio.so", RTLD_NOW | RTLD_LOCAL);
    if (!api->lib) {
        return NJ_ERR_UNSUPPORTED;      /* older than API 26: OpenSL ES it is */
    }
    AA_SYM(api, createBuilder,     "AAudio_createStreamBuilder");
    AA_SYM(api, setDirection,      "AAudioStreamBuilder_setDirection");
    AA_SYM(api, setSharingMode,    "AAudioStreamBuilder_setSharingMode");
    AA_SYM(api, setPerformanceMode,"AAudioStreamBuilder_setPerformanceMode");
    AA_SYM(api, setFormat,         "AAudioStreamBuilder_setFormat");
    AA_SYM(api, setChannelCount,   "AAudioStreamBuilder_setChannelCount");
    AA_SYM(api, setSampleRate,     "AAudioStreamBuilder_setSampleRate");
    AA_SYM(api, setUsage,          "AAudioStreamBuilder_setUsage");
    AA_SYM(api, setDataCallback,   "AAudioStreamBuilder_setDataCallback");
    AA_SYM(api, setErrorCallback,  "AAudioStreamBuilder_setErrorCallback");
    AA_SYM(api, openStream,        "AAudioStreamBuilder_openStream");
    AA_SYM(api, deleteBuilder,     "AAudioStreamBuilder_delete");
    AA_SYM(api, getSampleRate,     "AAudioStream_getSampleRate");
    AA_SYM(api, getChannelCount,   "AAudioStream_getChannelCount");
    AA_SYM(api, requestStart,      "AAudioStream_requestStart");
    AA_SYM(api, requestPause,      "AAudioStream_requestPause");
    AA_SYM(api, requestStop,       "AAudioStream_requestStop");
    AA_SYM(api, close,             "AAudioStream_close");

    /* setUsage is API 28 and is the only optional one: on 26/27 there is no
     * such symbol, and the default routing is right anyway. */
    if (!api->createBuilder || !api->setDirection || !api->setSharingMode
        || !api->setPerformanceMode || !api->setFormat || !api->setChannelCount
        || !api->setSampleRate || !api->setDataCallback
        || !api->setErrorCallback || !api->openStream || !api->deleteBuilder
        || !api->getSampleRate || !api->getChannelCount || !api->requestStart
        || !api->requestPause || !api->requestStop || !api->close) {
        dlclose(api->lib);
        api->lib = NULL;
        return NJ_ERR_UNSUPPORTED;
    }
    return NJ_OK;
}

static aaudio_data_callback_result_t android_aaudio_cb(AAudioStream *stream,
                                                       void *user,
                                                       void *audio_data,
                                                       int32_t num_frames)
{
    android_audio *a = (android_audio *)user;
    int32_t ch = a->aa.getChannelCount(stream);

    if (ch < 1) {
        ch = 2;
    }
    pthread_mutex_lock(&a->lock);
    a->callbacks++;
    if (a->failed) {
        memset(audio_data, 0, (size_t)num_frames * (size_t)ch * sizeof(nj_s2));
    } else {
        android_audio_render(a, (nj_s2 *)audio_data, (int)num_frames, (int)ch);
    }
    pthread_mutex_unlock(&a->lock);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
/* --- AAudio callbacks end --- */

static void android_aaudio_err(AAudioStream *stream, void *user,
                               aaudio_result_t error)
{
    android_audio *a = (android_audio *)user;
    char b[96];
    (void)stream;
    if (!a) {
        return;
    }
    snprintf(b, sizeof(b), "audio: AAudio error %d, sound is off",
             (int)error);
    __android_log_write(ANDROID_LOG_WARN, LOG_TAG, b);
    pthread_mutex_lock(&a->lock);
    a->failed = 1;
    pthread_mutex_unlock(&a->lock);
}
/* --- AAudio error callback end --- */

static int android_aaudio_open(android_audio *a)
{
    AAudioStreamBuilder *b = NULL;
    aaudio_result_t r;

    if (a->aa.createBuilder(&b) != AAUDIO_OK || !b) {
        return NJ_ERR_UNSUPPORTED;
    }
    a->aa.setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    a->aa.setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    a->aa.setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_NONE);
    a->aa.setFormat(b, AAUDIO_FORMAT_PCM_I16);
    a->aa.setChannelCount(b, 2);
    a->aa.setSampleRate(b, ANDROID_AUDIO_RATE);
    /* setUsage arrived in API 28: on 26/27 the symbol is not there at all and
     * the default MEDIA usage is the right routing anyway. */
    if (a->aa.setUsage) {
        a->aa.setUsage(b, AAUDIO_USAGE_GAME);
    }
    a->aa.setDataCallback(b, android_aaudio_cb, a);
    a->aa.setErrorCallback(b, android_aaudio_err, a);
    r = a->aa.openStream(b, &a->stream);
    a->aa.deleteBuilder(b);
    if (r != AAUDIO_OK || !a->stream) {
        a->stream = NULL;
        return NJ_ERR_UNSUPPORTED;
    }
    /* The device may hand back a different rate or channel count than the one
     * asked for, and the mixer has to resample to what it actually got rather
     * than to what it wanted. */
    a->out_rate = a->aa.getSampleRate(a->stream);
    a->out_ch = a->aa.getChannelCount(a->stream);
    if (a->out_rate <= 0) {
        a->out_rate = ANDROID_AUDIO_RATE;
    }
    if (a->out_ch <= 0) {
        a->out_ch = 2;
    }
    if (a->aa.requestStart(a->stream) != AAUDIO_OK) {
        a->aa.close(a->stream);
        a->stream = NULL;
        return NJ_ERR_UNSUPPORTED;
    }
    a->stream_paused = 0;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* The older device: OpenSL ES, linked normally                        */
/* ------------------------------------------------------------------ */
/*
 * OpenSL ES pulls instead of pushing: the player calls back asking for the next
 * buffer, so this is a short ring of buffers refilled from the same mixer the
 * AAudio callback uses.  Same tones, same volume, same PCM - only the plumbing
 * differs, and that is the whole point of keeping one mixer.
 */
static void android_sl_cb(SLAndroidSimpleBufferQueueItf queue, void *user)
{
    android_audio *a = (android_audio *)user;
    int frames = a->sl_buf_frames;
    nj_s2 *buf;

    pthread_mutex_lock(&a->lock);
    buf = a->sl_buf + (size_t)a->sl_next * (size_t)frames * 2u;
    a->sl_next = (a->sl_next + 1) % ANDROID_SL_BUFFERS;
    if (a->failed || a->stream_paused) {
        memset(buf, 0, (size_t)frames * 2u * sizeof(nj_s2));
    } else {
        a->callbacks++;
        android_audio_render(a, buf, frames, 2);
    }
    pthread_mutex_unlock(&a->lock);
    (*queue)->Enqueue(queue, buf,
                      (SLuint32)((size_t)frames * 2u * sizeof(nj_s2)));
}

static int android_sl_open(android_audio *a)
{
    SLDataLocator_AndroidSimpleBufferQueue loc_bq;
    SLDataFormat_PCM fmt;
    SLDataSource src;
    SLDataLocator_OutputMix loc_mix;
    SLDataSink sink;
    const SLInterfaceID ids[2] = { SL_IID_BUFFERQUEUE, SL_IID_VOLUME };
    const SLboolean req[2] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_FALSE };
    int i;

    if (slCreateEngine(&a->sl_engine_obj, 0, NULL, 0, NULL, NULL)
        != SL_RESULT_SUCCESS) {
        return NJ_ERR_UNSUPPORTED;
    }
    if ((*a->sl_engine_obj)->Realize(a->sl_engine_obj, SL_BOOLEAN_FALSE)
        != SL_RESULT_SUCCESS) {
        return NJ_ERR_UNSUPPORTED;
    }
    if ((*a->sl_engine_obj)->GetInterface(a->sl_engine_obj, SL_IID_ENGINE,
                                          &a->sl_engine) != SL_RESULT_SUCCESS) {
        return NJ_ERR_UNSUPPORTED;
    }
    if ((*a->sl_engine)->CreateOutputMix(a->sl_engine, &a->sl_mix_obj, 0, NULL,
                                         NULL) != SL_RESULT_SUCCESS) {
        return NJ_ERR_UNSUPPORTED;
    }
    if ((*a->sl_mix_obj)->Realize(a->sl_mix_obj, SL_BOOLEAN_FALSE)
        != SL_RESULT_SUCCESS) {
        return NJ_ERR_UNSUPPORTED;
    }

    memset(&loc_bq, 0, sizeof(loc_bq));
    memset(&fmt, 0, sizeof(fmt));
    memset(&src, 0, sizeof(src));
    memset(&loc_mix, 0, sizeof(loc_mix));
    memset(&sink, 0, sizeof(sink));
    loc_bq.locatorType = SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE;
    loc_bq.numBuffers = ANDROID_SL_BUFFERS;
    fmt.formatType = SL_DATAFORMAT_PCM;
    fmt.numChannels = 2;
    fmt.samplesPerSec = SL_SAMPLINGRATE_44_1;
    fmt.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16;
    fmt.containerSize = SL_PCMSAMPLEFORMAT_FIXED_16;
    fmt.channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
    fmt.endianness = SL_BYTEORDER_LITTLEENDIAN;
    src.pLocator = &loc_bq;
    src.pFormat = &fmt;
    loc_mix.locatorType = SL_DATALOCATOR_OUTPUTMIX;
    loc_mix.outputMix = a->sl_mix_obj;
    sink.pLocator = &loc_mix;
    sink.pFormat = NULL;

    if ((*a->sl_engine)->CreateAudioPlayer(a->sl_engine, &a->sl_player_obj, &src,
                                           &sink, 2, ids, req)
        != SL_RESULT_SUCCESS) {
        return NJ_ERR_UNSUPPORTED;
    }
    if ((*a->sl_player_obj)->Realize(a->sl_player_obj, SL_BOOLEAN_FALSE)
        != SL_RESULT_SUCCESS) {
        return NJ_ERR_UNSUPPORTED;
    }
    if ((*a->sl_player_obj)->GetInterface(a->sl_player_obj, SL_IID_PLAY,
                                          &a->sl_play) != SL_RESULT_SUCCESS) {
        return NJ_ERR_UNSUPPORTED;
    }
    if ((*a->sl_player_obj)->GetInterface(a->sl_player_obj, SL_IID_BUFFERQUEUE,
                                          &a->sl_queue) != SL_RESULT_SUCCESS) {
        return NJ_ERR_UNSUPPORTED;
    }
    if ((*a->sl_queue)->RegisterCallback(a->sl_queue, android_sl_cb, a)
        != SL_RESULT_SUCCESS) {
        return NJ_ERR_UNSUPPORTED;
    }

    a->sl_buf_frames = ANDROID_SL_FRAMES;
    a->sl_buf = (nj_s2 *)calloc((size_t)ANDROID_SL_BUFFERS
                                * (size_t)ANDROID_SL_FRAMES * 2u, sizeof(nj_s2));
    if (!a->sl_buf) {
        return NJ_ERR_UNSUPPORTED;
    }
    a->sl_next = 0;
    a->out_rate = ANDROID_AUDIO_RATE;
    a->out_ch = 2;
    a->stream_paused = 0;
    if ((*a->sl_play)->SetPlayState(a->sl_play, SL_PLAYSTATE_PLAYING)
        != SL_RESULT_SUCCESS) {
        return NJ_ERR_UNSUPPORTED;
    }
    /* OpenSL ES only starts calling back once something is queued, so the whole
     * ring goes in up front.  Each callback then refills the buffer the player
     * just finished with, in the order they were queued. */
    for (i = 0; i < ANDROID_SL_BUFFERS; i++) {
        nj_s2 *buf = a->sl_buf + (size_t)i * (size_t)ANDROID_SL_FRAMES * 2u;
        memset(buf, 0, (size_t)ANDROID_SL_FRAMES * 2u * sizeof(nj_s2));
        (*a->sl_queue)->Enqueue(a->sl_queue, buf,
                                (SLuint32)((size_t)ANDROID_SL_FRAMES * 2u
                                           * sizeof(nj_s2)));
    }
    return NJ_OK;
}

/*
 * One device per run, chosen by what the phone actually has.  AAudio is tried
 * first because it is the supported API and the one the mixer was written for;
 * a phone that has no libaaudio.so falls through to OpenSL ES, and a phone
 * where both fail simply has no sound for this run - the game keeps running,
 * because a silent game is a far better outcome than a crash on startup.
 *
 * `adb shell setprop debug.nj2me.audio opensl` pins the old backend, which is
 * the only way to exercise that path on a phone that has AAudio.
 */
static int android_audio_ensure(android_state *s, android_audio *a)
{
    char msg[96];
    char pref[PROP_VALUE_MAX];
    int force_opensl = 0;

    (void)s;
    if (a->backend) {
        return NJ_OK;
    }
    if (a->failed) {
        return NJ_ERR_UNSUPPORTED;
    }
    pref[0] = '\0';
    __system_property_get("debug.nj2me.audio", pref);
    if (pref[0] == 'o' || pref[0] == 'O') {
        force_opensl = 1;
    }
    if (!force_opensl && android_aaudio_load(&a->aa) == NJ_OK
        && android_aaudio_open(a) == NJ_OK) {
        a->backend = 1;
        snprintf(msg, sizeof(msg), "audio: AAudio started, %d Hz, %d ch",
                 a->out_rate, a->out_ch);
        __android_log_write(ANDROID_LOG_INFO, LOG_TAG, msg);
        return NJ_OK;
    }
    if (a->aa.lib) {
        dlclose(a->aa.lib);
        a->aa.lib = NULL;
    }
    if (android_sl_open(a) == NJ_OK) {
        a->backend = 2;
        snprintf(msg, sizeof(msg), "audio: OpenSL ES started, %d Hz, %d ch%s",
                 a->out_rate, a->out_ch, force_opensl ? " (pinned)" : "");
        __android_log_write(ANDROID_LOG_INFO, LOG_TAG, msg);
        return NJ_OK;
    }
    a->failed = 1;
    __android_log_write(ANDROID_LOG_WARN, LOG_TAG,
                        "audio: no output device (AAudio and OpenSL ES both "
                        "failed): sound is off for this run");
    return NJ_ERR_UNSUPPORTED;
}

static int android_tone_play(void *ud, const nj_audio_tone *tones, int count)
{
    android_state *s = AS(ud);
    android_audio *a = android_audio_get(s);

    if (!a || !tones || count <= 0) {
        return NJ_ERR;
    }
    if (android_audio_ensure(s, a) != NJ_OK) {
        return NJ_ERR_UNSUPPORTED;
    }
    if (count > ANDROID_AUDIO_TONES) {
        count = ANDROID_AUDIO_TONES;   /* the tail of a huge sequence is lost */
    }
    pthread_mutex_lock(&a->lock);
    memcpy(a->tones, tones, (size_t)count * sizeof(a->tones[0]));
    a->tone_count = count;
    a->tone_next = 0;
    a->tone_on = 1;
    a->left = 0;
    a->phase = 0;
    a->step = 0;
    a->tones_played++;
    pthread_mutex_unlock(&a->lock);
    return 0;
}

static void android_tone_stop(void *ud)
{
    android_state *s = AS(ud);
    android_audio *a = s->audio;

    if (!a) {
        return;
    }
    pthread_mutex_lock(&a->lock);
    a->tone_on = 0;
    a->tone_count = 0;
    a->tone_next = 0;
    a->left = 0;
    pthread_mutex_unlock(&a->lock);
}

static int android_pcm_open(void *ud, int sample_rate, int channels)
{
    android_state *s = AS(ud);
    android_audio *a = android_audio_get(s);

    if (!a) {
        return NJ_ERR_NOMEM;
    }
    if (channels < 1 || channels > 2 || sample_rate <= 0) {
        return NJ_ERR_UNSUPPORTED;
    }
    if (android_audio_ensure(s, a) != NJ_OK) {
        return NJ_ERR_UNSUPPORTED;
    }
    pthread_mutex_lock(&a->lock);
    a->src_rate = sample_rate;
    a->src_ch = channels;
    a->pcm_on = 1;
    a->rd = a->wr = 0;
    a->pcm_pos = 0;
    pthread_mutex_unlock(&a->lock);
    return 0;
}

static int android_pcm_write(void *ud, const nj_s2 *samples, int frames)
{
    android_state *s = AS(ud);
    android_audio *a = s->audio;
    int space, take, i;

    if (!a || !samples || frames <= 0) {
        return 0;
    }
    pthread_mutex_lock(&a->lock);
    if (!a->pcm_on || a->failed) {
        pthread_mutex_unlock(&a->lock);
        return 0;
    }
    space = (a->wr + ANDROID_AUDIO_RING - a->rd) % ANDROID_AUDIO_RING;
    space = ANDROID_AUDIO_RING - 1 - space;
    take = (frames < space) ? frames : space;
    for (i = 0; i < take; i++) {
        nj_s2 *dst = a->ring + (size_t)a->wr * 2u;
        if (a->src_ch == 1) {
            /* Mono is duplicated here, once, so the callback never has to ask
             * what shape the stream is. */
            dst[0] = dst[1] = samples[i];
        } else {
            dst[0] = samples[(size_t)i * 2u];
            dst[1] = samples[(size_t)i * 2u + 1u];
        }
        a->wr = (a->wr + 1) % ANDROID_AUDIO_RING;
    }
    a->pcm_frames += take;
    pthread_mutex_unlock(&a->lock);
    return take;
}

static int android_pcm_free(void *ud)
{
    android_state *s = AS(ud);
    android_audio *a = s->audio;
    int space, used;

    if (!a) {
        return 0;
    }
    pthread_mutex_lock(&a->lock);
    used = (a->wr + ANDROID_AUDIO_RING - a->rd) % ANDROID_AUDIO_RING;
    space = ANDROID_AUDIO_RING - 1 - used;
    pthread_mutex_unlock(&a->lock);
    return space;
}

static void android_pcm_close(void *ud)
{
    android_state *s = AS(ud);
    android_audio *a = s->audio;

    if (!a) {
        return;
    }
    pthread_mutex_lock(&a->lock);
    a->pcm_on = 0;
    a->rd = a->wr = 0;
    a->pcm_pos = 0;
    pthread_mutex_unlock(&a->lock);
}

/*
 * Foreground/background, and the audio side of it.  The stream is paused rather
 * than closed so that resuming a game does not have to reopen the device (which
 * costs tens of milliseconds and can glitch the first sound); everything the
 * mixer holds stays in the ring.
 */
/*
 * Pausing is per backend.  AAudio has a real paused state and stops pulling, so
 * it is told to pause; OpenSL ES keeps pulling whatever happens, so it is left
 * running and the callback hands it silence - same result, no device churn.
 */
static void android_audio_set_paused(android_audio *a, int paused)
{
    if (!a || !a->backend) {
        return;
    }
    if (a->backend == 1 && a->stream) {
        if (paused && !a->stream_paused) {
            if (a->aa.requestPause(a->stream) == AAUDIO_OK) {
                a->stream_paused = 1;
            }
        } else if (!paused && a->stream_paused) {
            if (a->aa.requestStart(a->stream) == AAUDIO_OK) {
                a->stream_paused = 0;
            }
        }
        return;
    }
    if (a->backend == 2) {
        pthread_mutex_lock(&a->lock);
        a->stream_paused = paused ? 1 : 0;
        pthread_mutex_unlock(&a->lock);
    }
}

void nj_port_android_set_active(nj_port *port, int active)
{
    android_state *s;

    if (!port || !port->ud) {
        return;
    }
    s = AS(port->ud);
    s->paused = active ? 0 : 1;
    android_audio_set_paused(s->audio, active ? 0 : 1);
    if (!active) {
        return;
    }
    /* Waking up: park nothing, resume the device, and put the MIDlet where it
     * was when the frame loop stopped. */
}

static void android_shutdown(void *ud)
{
    android_state *s = AS(ud);

    if (!s) {
        return;
    }
    android_audio_close(s);
    pthread_mutex_destroy(&s->lock);
    free(s->shadow);
    free(s->det_prev);
    free(s);
}

/* ------------------------------------------------------------------ */
/* Platform codecs: DEFLATE, images, sampled audio                     */
/* ------------------------------------------------------------------ */

/*
 * DEFLATE, from the platform's zlib.
 *
 * A JAR entry is raw DEFLATE and a PNG's IDAT is DEFLATE behind the zlib
 * wrapper, and the runtime carries its own inflate for both.  zlib has been in
 * the NDK sysroot at every API level this port supports, so the port hands the
 * work over and a build can leave the built-in one out (NJ_ENABLE_INFLATE=0)
 * without losing JAR loading or PNG.
 *
 * Both callers know the uncompressed length before they start - a zip entry's
 * central directory says it, a .m3g section declares it - so the stream is
 * driven with Z_FINISH into a buffer of exactly that size, and a short result
 * is reported as corruption rather than as success.
 */
static nj_status android_inflate_common(const nj_u1 *in, nj_u4 in_len,
                                        nj_u1 *out, nj_u4 out_len, int window)
{
    z_stream zs;
    int rc;

    if (!in || !out || in_len == 0u || out_len == 0u) {
        return NJ_ERR;
    }
    memset(&zs, 0, sizeof(zs));
    zs.next_in = (Bytef *)(void *)in;
    zs.avail_in = (uInt)in_len;
    zs.next_out = (Bytef *)out;
    zs.avail_out = (uInt)out_len;
    if (inflateInit2(&zs, window) != Z_OK) {
        return NJ_ERR_NOMEM;
    }
    rc = inflate(&zs, Z_FINISH);
    if (rc != Z_STREAM_END || zs.total_out != (uLong)out_len) {
        inflateEnd(&zs);
        return NJ_ERR_FORMAT;
    }
    inflateEnd(&zs);
    return NJ_OK;
}

static nj_status android_inflate(void *ud, const nj_u1 *in, nj_u4 in_len,
                                 nj_u1 *out, nj_u4 out_len)
{
    (void)ud;
    return android_inflate_common(in, in_len, out, out_len, 15);
}

static nj_status android_inflate_raw(void *ud, const nj_u1 *in, nj_u4 in_len,
                                     nj_u1 *out, nj_u4 out_len)
{
    (void)ud;
    return android_inflate_common(in, in_len, out, out_len, -15);
}

/*
 * Images, from BitmapFactory.
 *
 * BitmapFactory decodes PNG, JPEG, GIF (first frame), BMP and WebP on every
 * API level this port supports.  BMP and WebP are formats the runtime has no
 * decoder for at all, so on Android they simply work; PNG, JPEG and GIF are
 * already covered.
 *
 * It is registered one step BELOW the built-in providers (priority -1 against
 * their 0) and that is deliberate: Android's own PNG path is known to mishandle
 * palette images with a tRNS chunk - it is why J2ME-Loader re-decodes PNG in
 * pure Java instead of trusting BitmapFactory - while this runtime's PNG
 * decoder handles them.  So the built-in decoder keeps PNG/JPEG/GIF, the
 * platform fills the formats nothing else can read, and a build with
 * -DNJ_ENABLE_PNG=0 (and so on) falls through to the platform automatically.  A
 * port that would rather have BitmapFactory first can register it again at
 * NJ_CODEC_PRIO_PLATFORM.
 *
 * The bytes cross into Java and the pixels come back as an RGBA byte array.
 * Doing the pixel copy in Java keeps this file free of jnigraphics and of the
 * two traps that locking a Bitmap brings: ARGB_8888 is premultiplied, and an
 * ImageDecoder bitmap can be hardware-backed and not lockable at all.
 */
static int android_image_accepts(const void *data, nj_u4 size)
{
    const nj_u1 *p = (const nj_u1 *)data;

    if (!p || size < 12u) {
        return 0;
    }
    if (p[0] == 0x89u && p[1] == 'P' && p[2] == 'N' && p[3] == 'G') {
        return 1;
    }
    if (p[0] == 0xFFu && p[1] == 0xD8u && p[2] == 0xFFu) {
        return 1;
    }
    if (p[0] == 'G' && p[1] == 'I' && p[2] == 'F') {
        return 1;
    }
    if (p[0] == 'B' && p[1] == 'M') {
        return 1;
    }
    if (p[0] == 'R' && p[1] == 'I' && p[2] == 'F' && p[3] == 'F' &&
        p[8] == 'W' && p[9] == 'E' && p[10] == 'B' && p[11] == 'P') {
        return 1;
    }
    return 0;
}

static nj_image *android_image_decode(const nj_codec_req *req)
{
    const nj_port *port;
    android_state *s;
    JNIEnv *env;
    jbyteArray in = NULL, px = NULL;
    jintArray meta = NULL;
    jbyte *elems = NULL;
    jint dims[3];
    jsize nbytes;
    nj_image *img;
    nj_u4 *dst;
    nj_u4 i;
    int w, h, alpha;

    if (!req || !req->data || req->size == 0u || !req->alloc || !req->vm) {
        return NULL;
    }
    port = nj_vm_port(req->vm);
    s = (port && port->ud) ? AS(port->ud) : NULL;
    if (!s || !s->image_class || !s->image_info_mid || !s->image_decode_mid) {
        return NULL;
    }
    env = android_run_env(s);
    if (!env) {
        return NULL;
    }
    in = (*env)->NewByteArray(env, (jsize)req->size);
    if (!in) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    (*env)->SetByteArrayRegion(env, in, 0, (jsize)req->size,
                               (const jbyte *)(const void *)req->data);

    meta = (jintArray)(*env)->CallStaticObjectMethod(env, s->image_class,
                                                     s->image_info_mid, in);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        meta = NULL;
    }
    if (meta) {
        if ((*env)->GetArrayLength(env, meta) < 3) {
            (*env)->DeleteLocalRef(env, meta);
            meta = NULL;
        }
    }
    if (!meta) {
        (*env)->DeleteLocalRef(env, in);
        if (req->err) {
            *req->err = "the platform could not read this image";
        }
        return NULL;
    }
    (*env)->GetIntArrayRegion(env, meta, 0, 3, dims);
    (*env)->DeleteLocalRef(env, meta);
    w = (int)dims[0];
    h = (int)dims[1];
    alpha = (int)dims[2];
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
        (*env)->DeleteLocalRef(env, in);
        if (req->err) {
            *req->err = "the platform reported an unusable image size";
        }
        return NULL;
    }

    px = (jbyteArray)(*env)->CallStaticObjectMethod(env, s->image_class,
                                                    s->image_decode_mid, in,
                                                    (jint)w, (jint)h);
    (*env)->DeleteLocalRef(env, in);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        px = NULL;
    }
    if (!px) {
        if (req->err) {
            *req->err = "the platform could not decode this image";
        }
        return NULL;
    }
    nbytes = (*env)->GetArrayLength(env, px);
    if ((nj_u4)nbytes != (nj_u4)w * (nj_u4)h * 4u) {
        (*env)->DeleteLocalRef(env, px);
        if (req->err) {
            *req->err = "the platform returned the wrong number of pixels";
        }
        return NULL;
    }
    elems = (*env)->GetByteArrayElements(env, px, NULL);
    if (!elems) {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, px);
        return NULL;
    }

    img = nj_codec_image_new(req, w, h, NJ_PIXFMT_ARGB8888, alpha ? 1 : 0);
    if (!img) {
        (*env)->ReleaseByteArrayElements(env, px, elems, JNI_ABORT);
        (*env)->DeleteLocalRef(env, px);
        return NULL;
    }
    dst = img->pixels;
    for (i = 0u; i < (nj_u4)w * (nj_u4)h; i++) {
        nj_u4 a = (nj_u4)(nj_u1)elems[i * 4u + 3u];
        nj_u4 v = ((nj_u4)(nj_u1)elems[i * 4u] << 16) |
                  ((nj_u4)(nj_u1)elems[i * 4u + 1u] << 8) |
                  (nj_u4)(nj_u1)elems[i * 4u + 2u];
        if (!alpha) {
            /* A JPEG or a 24-bit BMP has no alpha channel; BitmapFactory hands
             * back 0xFF for it, but a codec that does not is not worth a
             * transparent game. */
            a = 0xFFu;
        }
        dst[i] = (a << 24) | v;
    }
    (*env)->ReleaseByteArrayElements(env, px, elems, JNI_ABORT);
    (*env)->DeleteLocalRef(env, px);
    return img;
}

static const nj_codec nj_codec_android_image = {
    "android-bitmap",
    { NJ_CODEC_FMT_PNG | NJ_CODEC_FMT_JPEG | NJ_CODEC_FMT_GIF |
      NJ_CODEC_FMT_BMP | NJ_CODEC_FMT_WEBP,
      NJ_CODEC_CAN_ARGB8888, NJ_CODEC_SCALE_1, 0u, 1, 0 },
    android_image_accepts,
    NULL,                 /* no info(): it would need the JNI state, and a
                           * bounds probe is not worth a round trip */
    android_image_decode,
    NULL, NULL, NULL
};

/*
 * Sampled audio, from MediaCodec.
 *
 * The runtime decodes WAV itself and AMR with its own tables; MP3, AAC, AMR-WB
 * and anything else the platform has a decoder for reach the mixer through
 * this hook instead.  It is the same two-call shape the HAL documents: with
 * out == NULL the platform is asked how many PCM bytes the file holds, and the
 * runtime then allocates exactly that and asks again.
 *
 * The result is signed 16-bit little-endian PCM, which is what pcm_write()
 * takes, at whatever rate the decoder produces - 8 kHz for AMR, 44.1 kHz for
 * MP3 - and the port's mixer already resamples.
 */
static nj_status android_audio_decode(void *ud, const nj_u1 *in, nj_u4 in_len,
                                      nj_u1 *out, nj_u4 out_cap, nj_u4 *out_len,
                                      nj_u4 *sample_rate, nj_u4 *channels)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    jbyteArray data = NULL, pcm = NULL;
    jintArray meta = NULL;
    jbyte *elems = NULL;
    jint info[4];
    jsize nbytes;

    if (!s || !in || in_len == 0u || !out_len) {
        return NJ_ERR;
    }
    if (!s->audio_class || !s->audio_info_mid || !s->audio_decode_mid) {
        return NJ_ERR_UNSUPPORTED;
    }
    env = android_run_env(s);
    if (!env) {
        return NJ_ERR_UNSUPPORTED;
    }
    data = (*env)->NewByteArray(env, (jsize)in_len);
    if (!data) {
        (*env)->ExceptionClear(env);
        return NJ_ERR;
    }
    (*env)->SetByteArrayRegion(env, data, 0, (jsize)in_len,
                               (const jbyte *)(const void *)in);

    if (!out) {
        /* First call: how big is it, and what does it play at? */
        meta = (jintArray)(*env)->CallStaticObjectMethod(
            env, s->audio_class, s->audio_info_mid, data);
        (*env)->DeleteLocalRef(env, data);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            return NJ_ERR_UNSUPPORTED;
        }
        if (!meta || (*env)->GetArrayLength(env, meta) < 4) {
            if (meta) {
                (*env)->DeleteLocalRef(env, meta);
            }
            return NJ_ERR_UNSUPPORTED;
        }
        (*env)->GetIntArrayRegion(env, meta, 0, 4, info);
        (*env)->DeleteLocalRef(env, meta);
        if (info[0] <= 0 || info[1] <= 0 || info[3] <= 0) {
            return NJ_ERR_UNSUPPORTED;
        }
        *out_len = (nj_u4)info[3];
        if (sample_rate) {
            *sample_rate = (nj_u4)info[0];
        }
        if (channels) {
            *channels = (nj_u4)info[1];
        }
        return NJ_OK;
    }

    /* Second call: the PCM itself. */
    pcm = (jbyteArray)(*env)->CallStaticObjectMethod(env, s->audio_class,
                                                     s->audio_decode_mid, data);
    (*env)->DeleteLocalRef(env, data);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return NJ_ERR_UNSUPPORTED;
    }
    if (!pcm) {
        return NJ_ERR_UNSUPPORTED;
    }
    nbytes = (*env)->GetArrayLength(env, pcm);
    if (nbytes <= 0 || (nj_u4)nbytes > out_cap) {
        (*env)->DeleteLocalRef(env, pcm);
        return NJ_ERR_UNSUPPORTED;
    }
    elems = (*env)->GetByteArrayElements(env, pcm, NULL);
    if (!elems) {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, pcm);
        return NJ_ERR;
    }
    memcpy(out, elems, (size_t)nbytes);
    (*env)->ReleaseByteArrayElements(env, pcm, elems, JNI_ABORT);
    (*env)->DeleteLocalRef(env, pcm);
    *out_len = (nj_u4)nbytes;
    return NJ_OK;
}

/*
 * install_codecs(): called by the runner once the VM exists.  Only the image
 * provider needs installing - the inflate and audio_decode hooks are plain
 * nj_port fields, so the runtime finds them on its own.
 */
static void android_install_codecs(void *ud, nj_vm *vm)
{
    android_state *s = AS(ud);

    if (!s || !vm) {
        return;
    }
    /* Remember the VM: the settings the player changes mid-game are applied
     * at the top of a frame, never from the JNI thread that delivered them. */
    s->vm = vm;
    s->speed_applied = 0;
    if (s->image_class && s->image_info_mid && s->image_decode_mid) {
        nj_codec_register(vm, &nj_codec_android_image, -1);
    }
}

/* ------------------------------------------------------------------ */
/* HTTP: javax.microedition.io.HttpConnection                          */
/* ------------------------------------------------------------------ */
/*
 * One request, one call.  PlatformHttp.java does the socket work with
 * HttpURLConnection and buffers the whole response; this side copies the
 * strings across, hands the body to the runtime's allocator, and frees the
 * Java-side entry immediately - the runtime owns the bytes from then on.
 *
 * Everything here fails soft: a missing class, a Java exception or a refused
 * request all come back as an error status, and the MIDlet then sees an
 * IOException.  Nothing may throw across JNI, which would take the VM down.
 */
static nj_status android_http_request(void *ud, const char *url,
                                      const char *method, const char *headers,
                                      const nj_u1 *body, nj_u4 body_len,
                                      int *status, char *rheaders,
                                      nj_size rheaders_cap,
                                      nj_u1 **out_body, nj_u4 *out_len,
                                      void *(*alloc)(void *aud, nj_u4 n),
                                      void *aud)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    jstring jurl = NULL;
    jstring jmethod = NULL;
    jstring jheaders = NULL;
    jbyteArray jbody = NULL;
    jlong id;
    jstring jrheaders = NULL;
    jbyteArray jresp = NULL;

    if (out_body) {
        *out_body = NULL;
    }
    if (out_len) {
        *out_len = 0u;
    }
    if (rheaders && rheaders_cap > 0) {
        rheaders[0] = '\0';
    }
    if (!s || !url) {
        return NJ_ERR_UNSUPPORTED;
    }
    env = android_run_env(s);
    if (!env || !s->http_class || !s->http_request_mid) {
        return NJ_ERR_UNSUPPORTED;
    }

    jurl = (*env)->NewStringUTF(env, url);
    jmethod = (*env)->NewStringUTF(env, method ? method : "GET");
    jheaders = (*env)->NewStringUTF(env, headers ? headers : "");
    if (!jurl || !jmethod || !jheaders) {
        goto fail;
    }
    if (body && body_len > 0u) {
        jbody = (*env)->NewByteArray(env, (jsize)body_len);
        if (!jbody) {
            goto fail;
        }
        (*env)->SetByteArrayRegion(env, jbody, 0, (jsize)body_len,
                                   (const jbyte *)body);
    }

    id = (*env)->CallStaticLongMethod(env, s->http_class, s->http_request_mid,
                                      jurl, jmethod, jheaders, jbody);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto fail;
    }
    if (id == 0) {
        /* The Java side logs the reason; nothing to add here. */
        goto fail;
    }

    if (status) {
        *status = (int)(*env)->CallStaticIntMethod(env, s->http_class,
                                                   s->http_status_mid, id);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            *status = 0;
        }
    }

    /*
     * The response header block, as "Name: value\n" lines.  A truncated copy
     * is acceptable: it never ends mid-line, because the string is cut at the
     * last newline that fits.
     */
    jrheaders = (jstring)(*env)->CallStaticObjectMethod(env, s->http_class,
                                                        s->http_headers_mid, id);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        jrheaders = NULL;
    }
    if (jrheaders && rheaders && rheaders_cap > 1u) {
        const char *utf = (*env)->GetStringUTFChars(env, jrheaders, NULL);
        if (utf) {
            size_t n = strlen(utf);
            if (n >= (size_t)rheaders_cap) {
                n = (size_t)rheaders_cap - 1u;
                while (n > 0u && utf[n - 1u] != '\n') {
                    n--;
                }
            }
            memcpy(rheaders, utf, n);
            rheaders[n] = '\0';
            (*env)->ReleaseStringUTFChars(env, jrheaders, utf);
        }
    }

    jresp = (jbyteArray)(*env)->CallStaticObjectMethod(env, s->http_class,
                                                       s->http_body_mid, id);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        jresp = NULL;
    }
    if (jresp && out_body && out_len && alloc) {
        jsize n = (*env)->GetArrayLength(env, jresp);
        if (n > 0) {
            nj_u1 *buf = (nj_u1 *)alloc(aud, (nj_u4)n);
            if (!buf) {
                goto fail;
            }
            (*env)->GetByteArrayRegion(env, jresp, 0, n, (jbyte *)buf);
            if ((*env)->ExceptionCheck(env)) {
                (*env)->ExceptionClear(env);
                goto fail;
            }
            *out_body = buf;
            *out_len = (nj_u4)n;
        }
    }

    /* The Java entry is done with; the body has been copied out. */
    (*env)->CallStaticVoidMethod(env, s->http_class, s->http_close_mid, id);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
    if (jurl) { (*env)->DeleteLocalRef(env, jurl); }
    if (jmethod) { (*env)->DeleteLocalRef(env, jmethod); }
    if (jheaders) { (*env)->DeleteLocalRef(env, jheaders); }
    if (jbody) { (*env)->DeleteLocalRef(env, jbody); }
    if (jrheaders) { (*env)->DeleteLocalRef(env, jrheaders); }
    if (jresp) { (*env)->DeleteLocalRef(env, jresp); }
    return NJ_OK;

fail:
    if (jurl) { (*env)->DeleteLocalRef(env, jurl); }
    if (jmethod) { (*env)->DeleteLocalRef(env, jmethod); }
    if (jheaders) { (*env)->DeleteLocalRef(env, jheaders); }
    if (jbody) { (*env)->DeleteLocalRef(env, jbody); }
    if (jrheaders) { (*env)->DeleteLocalRef(env, jrheaders); }
    if (jresp) { (*env)->DeleteLocalRef(env, jresp); }
    return NJ_ERR;
}

/* ------------------------------------------------------------------ */
/* JSR 256 sensors                                                     */
/* ------------------------------------------------------------------ */
/*
 * One line of PlatformSensor.list() is six tab-separated fields:
 *
 *   url \t quantity \t context \t model \t maxRateHz \t channel,channel,...
 *
 * and one channel is five colon-separated fields:
 *
 *   name:unit:min:max:resolution
 *
 * Everything is copied into the state's buffers: the Java string is released
 * before this returns, while the runtime reads the descriptors after it does,
 * and it is documented to borrow them only for the call - so the copy has to
 * outlive the call by at least as long as the buffers do.
 */
static void android_copy_field(char *dst, nj_size cap, const char *src)
{
    nj_size i = 0u;

    if (!dst || cap == 0u) {
        return;
    }
    if (src) {
        while (src[i] != '\0' && i + 1u < cap) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

/*
 * Fill one descriptor from the parsed fields.  The channel list is the part
 * that has to be right: a MIDlet reads one value per channel and shows the
 * unit and the range next to it.
 */
static void android_sensor_fill(android_state *s, int index, char *const *tab,
                                nj_sensor_desc *out)
{
    char *spec;

    memset(out, 0, sizeof(*out));
    android_copy_field(s->sensor_url[index], sizeof(s->sensor_url[index]),
                       tab[0]);
    android_copy_field(s->sensor_quantity[index],
                       sizeof(s->sensor_quantity[index]), tab[1]);
    android_copy_field(s->sensor_context[index],
                       sizeof(s->sensor_context[index]), tab[2]);
    android_copy_field(s->sensor_model[index], sizeof(s->sensor_model[index]),
                       tab[3]);
    out->url = s->sensor_url[index];
    out->quantity = s->sensor_quantity[index];
    out->context = s->sensor_context[index];
    out->model = s->sensor_model[index];
    out->max_rate_hz = (nj_u4)(tab[4] ? strtoul(tab[4], NULL, 10) : 0ul);

    spec = tab[5];
    while (spec && *spec && out->channels < NJ_SENSOR_MAX_CHANNELS) {
        char *end = strchr(spec, ',');
        char *part[5];
        int n = 0;
        char *p = spec;
        int c = out->channels;

        if (end) {
            *end = '\0';
        }
        part[n++] = p;
        while (*p && n < 5) {
            if (*p == ':') {
                *p = '\0';
                part[n++] = p + 1;
            }
            p++;
        }
        while (n < 5) {
            part[n++] = NULL;
        }
        android_copy_field(s->sensor_names[index][c],
                           sizeof(s->sensor_names[index][c]), part[0]);
        android_copy_field(s->sensor_units[index][c],
                           sizeof(s->sensor_units[index][c]), part[1]);
        out->channel_names[c] = s->sensor_names[index][c];
        out->units[c] = s->sensor_units[index][c];
        out->min_values[c] = part[2] ? strtod(part[2], NULL) : 0.0;
        out->max_values[c] = part[3] ? strtod(part[3], NULL) : 0.0;
        out->resolutions[c] = part[4] ? strtod(part[4], NULL) : 0.0;
        out->channels++;
        spec = end ? end + 1 : NULL;
    }
}

static int android_sensor_list(void *ud, nj_sensor_desc *out, int max)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    jobjectArray arr;
    jsize n, i;
    int count = 0;

    if (!s || !s->sensor_class || !s->sensor_list_mid) {
        return 0;                   /* no bridge: findSensors() is empty */
    }
    env = android_run_env(s);
    if (!env) {
        return 0;
    }
    arr = (jobjectArray)(*env)->CallStaticObjectMethod(env, s->sensor_class,
                                                       s->sensor_list_mid);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    if (!arr) {
        return 0;
    }
    n = (*env)->GetArrayLength(env, arr);
    if (!out || max <= 0) {
        (*env)->DeleteLocalRef(env, arr);
        return (int)n;              /* the count alone */
    }
    for (i = 0; i < n && count < max; i++) {
        jstring js = (jstring)(*env)->GetObjectArrayElement(env, arr, i);
        const char *utf;
        char line[512];
        char *tab[6];
        char *p;
        int k = 0;

        if (!js || (*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            if (js) { (*env)->DeleteLocalRef(env, js); }
            continue;
        }
        utf = (*env)->GetStringUTFChars(env, js, NULL);
        if (!utf) {
            (*env)->ExceptionClear(env);
            (*env)->DeleteLocalRef(env, js);
            continue;
        }
        android_copy_field(line, sizeof(line), utf);
        (*env)->ReleaseStringUTFChars(env, js, utf);
        (*env)->DeleteLocalRef(env, js);

        tab[k++] = line;
        for (p = line; *p && k < 6; p++) {
            if (*p == '\t') {
                *p = '\0';
                tab[k++] = p + 1;
            }
        }
        while (k < 6) {
            tab[k++] = NULL;
        }
        if (!tab[0] || !tab[0][0] || !tab[1] || !tab[1][0]) {
            continue;               /* a line without a url is not a sensor */
        }
        android_sensor_fill(s, count, tab, &out[count]);
        count++;
    }
    (*env)->DeleteLocalRef(env, arr);
    return count;
}

static void *android_sensor_open(void *ud, const char *url, nj_u4 period_ms)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    jstring ju;
    jlong h;

    if (!s || !s->sensor_class || !s->sensor_open_mid || !url) {
        return NULL;
    }
    env = android_run_env(s);
    if (!env) {
        return NULL;
    }
    ju = (*env)->NewStringUTF(env, url);
    if (!ju) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    h = (*env)->CallStaticLongMethod(env, s->sensor_class, s->sensor_open_mid,
                                     ju, (jint)(period_ms ? period_ms : 100u));
    (*env)->DeleteLocalRef(env, ju);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    /* 0 is the Java side's "this device has no such sensor", which is a
     * refusal, not a handle. */
    return h == 0 ? NULL : (void *)(intptr_t)h;
}

static int android_sensor_read(void *ud, void *handle, double *values,
                               int max_values)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    jdoubleArray ja;
    jsize n;
    int take;

    if (!s || !s->sensor_class || !s->sensor_read_mid || !handle || !values) {
        return 0;
    }
    env = android_run_env(s);
    if (!env) {
        return 0;
    }
    ja = (jdoubleArray)(*env)->CallStaticObjectMethod(
        env, s->sensor_class, s->sensor_read_mid, (jlong)(intptr_t)handle);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    if (!ja) {
        return 0;                   /* nothing measured yet */
    }
    n = (*env)->GetArrayLength(env, ja);
    /* The last element is the event timestamp, not a channel. */
    take = (int)n - 1;
    if (take > max_values) {
        take = max_values;
    }
    if (take > 0) {
        (*env)->GetDoubleArrayRegion(env, ja, 0, (jsize)take, values);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            take = 0;
        }
    }
    (*env)->DeleteLocalRef(env, ja);
    return take;
}

static void android_sensor_close(void *ud, void *handle)
{
    android_state *s = AS(ud);
    JNIEnv *env;

    if (!s || !s->sensor_class || !s->sensor_close_mid || !handle) {
        return;
    }
    env = android_run_env(s);
    if (!env) {
        return;
    }
    (*env)->CallStaticVoidMethod(env, s->sensor_class, s->sensor_close_mid,
                                 (jlong)(intptr_t)handle);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
}

/* ------------------------------------------------------------------ */
/* Stream sockets                                                      */
/* ------------------------------------------------------------------ */
/*
 * PlatformSocket does the connecting and hands back an opaque long; what the
 * runtime gets is a handle it passes to read/write/close and the four strings
 * and numbers a MIDlet reads back through getAddress()/getPort().  The peer's
 * address is asked for here rather than read out of the URL because a name
 * that resolved to an address should answer with the address.
 */
static void android_socket_text(JNIEnv *env, jstring js, char *dst, nj_size cap)
{
    const char *utf;
    nj_size n;

    if (!dst || cap == 0u) {
        return;
    }
    dst[0] = '\0';
    if (!js) {
        return;
    }
    utf = (*env)->GetStringUTFChars(env, js, NULL);
    if (!utf) {
        (*env)->ExceptionClear(env);
        return;
    }
    n = (nj_size)strlen(utf);
    if (n >= cap) {
        n = cap - 1u;
    }
    memcpy(dst, utf, n);
    dst[n] = '\0';
    (*env)->ReleaseStringUTFChars(env, js, utf);
}

static void *android_socket_connect(void *ud, const char *url, nj_u4 timeout_ms,
                                    nj_socket_info *info, char *err,
                                    nj_size err_cap)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    jstring ju;
    jstring js;
    jlong h;

    if (err && err_cap > 0u) {
        err[0] = '\0';
    }
    if (!s || !s->socket_class || !s->socket_connect_mid || !url) {
        return NULL;
    }
    env = android_run_env(s);
    if (!env) {
        return NULL;
    }
    ju = (*env)->NewStringUTF(env, url);
    if (!ju) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    h = (*env)->CallStaticLongMethod(env, s->socket_class, s->socket_connect_mid,
                                     ju, (jint)(timeout_ms ? timeout_ms : 30000u));
    (*env)->DeleteLocalRef(env, ju);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        h = 0;
    }
    if (h == 0) {
        /* The Java side logs the reason (it has the exception text); what the
         * MIDlet gets is a sentence that names the connection it asked for. */
        if (err && err_cap > 0u) {
            snprintf(err, (size_t)err_cap, "the platform could not open %s", url);
        }
        return NULL;
    }

    if (info) {
        memset(info, 0, sizeof(*info));
        js = (jstring)(*env)->CallStaticObjectMethod(env, s->socket_class,
                                                     s->socket_address_mid, h);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
        android_socket_text(env, js, info->address, (nj_size)sizeof(info->address));
        if (js) {
            (*env)->DeleteLocalRef(env, js);
        }
        js = (jstring)(*env)->CallStaticObjectMethod(env, s->socket_class,
                                                     s->socket_local_mid, h);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
        android_socket_text(env, js, info->local_address,
                            (nj_size)sizeof(info->local_address));
        if (js) {
            (*env)->DeleteLocalRef(env, js);
        }
        info->port = (int)(*env)->CallStaticIntMethod(env, s->socket_class,
                                                      s->socket_port_mid, h);
        info->local_port = (int)(*env)->CallStaticIntMethod(
            env, s->socket_class, s->socket_local_port_mid, h);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
    }
    return (void *)(intptr_t)h;
}

static int android_socket_read(void *ud, void *handle, nj_u1 *buf, int len,
                               nj_u4 timeout_ms)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    jbyteArray ja;
    jint n;

    if (!s || !s->socket_class || !s->socket_read_mid || !handle || !buf || len <= 0) {
        return -1;
    }
    env = android_run_env(s);
    if (!env) {
        return -1;
    }
    ja = (*env)->NewByteArray(env, (jsize)len);
    if (!ja) {
        (*env)->ExceptionClear(env);
        return -1;
    }
    n = (*env)->CallStaticIntMethod(env, s->socket_class, s->socket_read_mid,
                                    (jlong)(intptr_t)handle, ja, (jint)len,
                                    (jint)(timeout_ms ? timeout_ms : 30000u));
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, ja);
        return -1;
    }
    if (n > 0) {
        if (n > len) {
            n = (jint)len;
        }
        (*env)->GetByteArrayRegion(env, ja, 0, n, (jbyte *)buf);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            n = -1;
        }
    }
    (*env)->DeleteLocalRef(env, ja);
    return (int)n;
}

static int android_socket_write(void *ud, void *handle, const nj_u1 *buf, int len)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    jbyteArray ja;
    jint n;

    if (!s || !s->socket_class || !s->socket_write_mid || !handle || !buf || len <= 0) {
        return -1;
    }
    env = android_run_env(s);
    if (!env) {
        return -1;
    }
    ja = (*env)->NewByteArray(env, (jsize)len);
    if (!ja) {
        (*env)->ExceptionClear(env);
        return -1;
    }
    (*env)->SetByteArrayRegion(env, ja, 0, (jsize)len, (const jbyte *)buf);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, ja);
        return -1;
    }
    n = (*env)->CallStaticIntMethod(env, s->socket_class, s->socket_write_mid,
                                    (jlong)(intptr_t)handle, ja, (jint)len);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        n = -1;
    }
    (*env)->DeleteLocalRef(env, ja);
    return (int)n;
}

static void android_socket_close(void *ud, void *handle)
{
    android_state *s = AS(ud);
    JNIEnv *env;

    if (!s || !s->socket_class || !s->socket_close_mid || !handle) {
        return;
    }
    env = android_run_env(s);
    if (!env) {
        return;
    }
    (*env)->CallStaticVoidMethod(env, s->socket_class, s->socket_close_mid,
                                 (jlong)(intptr_t)handle);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
}

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* Bluetooth discovery (javax.bluetooth)                               */
/* ------------------------------------------------------------------ */
/*
 * Only discovery is bridged.  The four hooks below answer "what is this
 * adapter", "make it discoverable", "what is out there" and "what services
 * does that device offer"; the connection hooks (listen, accept, read, write)
 * are deliberately left NULL, so a MIDlet that opens a server URL is told the
 * port cannot do it instead of being handed a socket that cannot work.
 *
 * The bridge's own entry points never throw: available() answers 0 when there
 * is no adapter or no permission, and the runtime turns that into the JSR's
 * BluetoothStateException - which is the answer a MIDlet already has to
 * handle.  Every JNI call here is also wrapped, because a SecurityException
 * raised inside the platform would otherwise cross JNI and abort the process.
 */

/* A jstring into a fixed buffer: the address and the name are short. */
static void android_bt_jstr(JNIEnv *env, jstring js, char *dst, nj_size cap)
{
    const char *utf;

    if (!dst || cap == 0) {
        return;
    }
    dst[0] = '\0';
    if (!js) {
        return;
    }
    utf = (*env)->GetStringUTFChars(env, js, NULL);
    if (!utf) {
        (*env)->ExceptionClear(env);
        return;
    }
    android_copy_field(dst, cap, utf);
    (*env)->ReleaseStringUTFChars(env, js, utf);
}

static nj_status android_bt_local(void *ud, nj_bt_local *out)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    jstring js;
    int ok;

    if (!s || !out) {
        return NJ_ERR;
    }
    memset(out, 0, sizeof(*out));
    env = android_run_env(s);
    if (!env || !s->bt_class || !s->bt_available_mid) {
        return NJ_ERR_UNSUPPORTED;
    }
    ok = (int)(*env)->CallStaticIntMethod(env, s->bt_class,
                                          s->bt_available_mid);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        ok = 0;
    }
    if (!ok) {
        /* No adapter, or the app has not been granted BLUETOOTH_CONNECT. */
        return NJ_ERR_UNSUPPORTED;
    }
    if (s->bt_address_mid) {
        js = (jstring)(*env)->CallStaticObjectMethod(env, s->bt_class,
                                                     s->bt_address_mid);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        } else {
            android_bt_jstr(env, js, s->bt_address, sizeof(s->bt_address));
        }
        if (js) {
            (*env)->DeleteLocalRef(env, js);
        }
    }
    if (s->bt_name_mid) {
        js = (jstring)(*env)->CallStaticObjectMethod(env, s->bt_class,
                                                     s->bt_name_mid);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        } else {
            android_bt_jstr(env, js, s->bt_name, sizeof(s->bt_name));
        }
        if (js) {
            (*env)->DeleteLocalRef(env, js);
        }
    }
    if (s->bt_class_mid) {
        ok = (int)(*env)->CallStaticIntMethod(env, s->bt_class, s->bt_class_mid);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            ok = 0;
        }
        out->device_class = (nj_u4)(nj_s4)ok;
    }
    android_copy_field(out->address, sizeof(out->address), s->bt_address);
    android_copy_field(out->name, sizeof(out->name), s->bt_name);
    /* Android reports the adapter's own scan mode; a device that has not been
     * asked is not discoverable, which is also the JSR's initial state. */
    out->discoverable = 0x9E8B00;
    out->inquiry_ms = 12000;
    return NJ_OK;
}

static int android_bt_discoverable(void *ud, int mode, nj_u4 seconds)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    int got;

    (void)seconds;
    env = android_run_env(s);
    if (!s || !env || !s->bt_class || !s->bt_discoverable_mid) {
        return 0;
    }
    got = (int)(*env)->CallStaticIntMethod(env, s->bt_class,
                                           s->bt_discoverable_mid, (jint)mode);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    return got;
}

static int android_bt_inquiry(void *ud, nj_bt_device *out, int max,
                              nj_u4 duration_ms)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    jobjectArray arr;
    jsize n;
    int i;

    if (!s || !out || max <= 0) {
        return 0;
    }
    env = android_run_env(s);
    if (!env || !s->bt_class || !s->bt_inquiry_devices_mid) {
        return 0;
    }
    if (!s->bt_discovering && s->bt_inquiry_start_mid) {
        (void)(*env)->CallStaticIntMethod(env, s->bt_class,
                                          s->bt_inquiry_start_mid,
                                          (jint)duration_ms);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
        s->bt_discovering = 1;
    }
    arr = (jobjectArray)(*env)->CallStaticObjectMethod(env, s->bt_class,
                                                       s->bt_inquiry_devices_mid);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    if (!arr) {
        return 0;
    }
    n = (*env)->GetArrayLength(env, arr);
    if (n > max) {
        n = max;
    }
    for (i = 0; i < (int)n; i++) {
        jstring line = (jstring)(*env)->GetObjectArrayElement(env, arr, (jsize)i);
        char text[BT_ADDR_CAP + BT_NAME_CAP + 16];
        char *tab;

        memset(&out[i], 0, sizeof(out[i]));
        if (!line) {
            continue;
        }
        android_bt_jstr(env, line, text, sizeof(text));
        (*env)->DeleteLocalRef(env, line);
        tab = strchr(text, '\t');
        if (!tab) {
            continue;
        }
        *tab = '\0';
        android_copy_field(out[i].address, sizeof(out[i].address), text);
        {
            const char *rest = tab + 1;
            char *tab2 = strchr(rest, '\t');
            if (tab2) {
                *tab2 = '\0';
                out[i].device_class = (nj_u4)strtoul(tab2 + 1, NULL, 10);
            }
            android_copy_field(out[i].name, sizeof(out[i].name), rest);
        }
    }
    (*env)->DeleteLocalRef(env, arr);
    return (int)n;
}

/* The UUID the runtime asks about arrives as sixteen big-endian bytes; the
 * bridge's services() wants hex text. */
static void android_bt_uuid_hex(const nj_u1 *uuid, char *out, nj_size cap)
{
    static const char hex[] = "0123456789abcdef";
    nj_size i;

    if (!out || cap < 33) {
        return;
    }
    for (i = 0; i < 16; i++) {
        out[i * 2] = hex[(uuid[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hex[uuid[i] & 0x0F];
    }
    out[32] = '\0';
}

/* Hex text back into the sixteen big-endian bytes.  A 4- or 8-digit UUID is
 * the short form and sits in the last bytes, the way the JSR spells it. */
static void android_bt_uuid_text(const char *text, nj_u1 out[16])
{
    nj_size len, i;

    memset(out, 0, 16);
    if (!text) {
        return;
    }
    len = strlen(text);
    if (len > 32) {
        return;
    }
    for (i = 0; i < len; i++) {
        char c = text[i];
        nj_u1 v;
        if (c >= '0' && c <= '9') v = (nj_u1)(c - '0');
        else if (c >= 'a' && c <= 'f') v = (nj_u1)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = (nj_u1)(c - 'A' + 10);
        else return;
        {
            nj_size pos = 16 - (len + 1) / 2 + i / 2;
            if (pos >= 16) continue;
            if ((i & 1u) == 0u) out[pos] = (nj_u1)(v << 4);
            else out[pos] |= v;
        }
    }
}

static int android_bt_services(void *ud, const char *address,
                               const nj_u1 *uuid, nj_bt_service *out, int max)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    jobjectArray arr;
    jstring jaddr;
    jstring juuid = NULL;
    jsize n;
    int i;

    if (!s || !address || !out || max <= 0) {
        return 0;
    }
    env = android_run_env(s);
    if (!env || !s->bt_class || !s->bt_services_mid) {
        return 0;
    }
    jaddr = (*env)->NewStringUTF(env, address);
    if (!jaddr) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    if (uuid) {
        char hex[40];
        android_bt_uuid_hex(uuid, hex, sizeof(hex));
        juuid = (*env)->NewStringUTF(env, hex);
    }
    arr = (jobjectArray)(*env)->CallStaticObjectMethod(env, s->bt_class,
                                                       s->bt_services_mid,
                                                       jaddr, juuid);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        arr = NULL;
    }
    (*env)->DeleteLocalRef(env, jaddr);
    if (juuid) {
        (*env)->DeleteLocalRef(env, juuid);
    }
    if (!arr) {
        return 0;
    }
    n = (*env)->GetArrayLength(env, arr);
    if (n > max) {
        n = max;
    }
    for (i = 0; i < (int)n; i++) {
        jstring line = (jstring)(*env)->GetObjectArrayElement(env, arr, (jsize)i);
        char text[192 + BT_NAME_CAP + 40];
        char *tab;

        memset(&out[i], 0, sizeof(out[i]));
        if (!line) {
            continue;
        }
        android_bt_jstr(env, line, text, sizeof(text));
        (*env)->DeleteLocalRef(env, line);
        tab = strchr(text, '\t');
        if (!tab) {
            continue;
        }
        *tab = '\0';
        android_copy_field(out[i].url, sizeof(out[i].url), text);
        {
            char *tab2 = strchr(tab + 1, '\t');
            if (tab2) {
                *tab2 = '\0';
                android_bt_uuid_text(tab2 + 1, out[i].uuid);
            } else {
                android_bt_uuid_text("", out[i].uuid);
            }
            android_copy_field(out[i].name, sizeof(out[i].name), tab + 1);
        }
        android_copy_field(out[i].address, sizeof(out[i].address), address);
    }
    (*env)->DeleteLocalRef(env, arr);
    return (int)n;
}

/* ------------------------------------------------------------------ */
/* Bluetooth connections: servers, clients and the bytes in between     */
/* ------------------------------------------------------------------ */
/*
 * The discovery half of javax.bluetooth is above; this is the half a MIDlet
 * reaches for when it wants to play against another handset.  Every function
 * here is one call into PlatformBluetooth, and every one of them can say "not
 * now": a NULL from connect or listen makes the runtime refuse the URL by
 * name, and a 0 from read means "nothing yet, ask again" - which is what the
 * runtime's timeout loop is written against.
 */
static void *android_bt_listen(void *ud, const char *url, char *err,
                               nj_size err_cap)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    jstring ju;
    jlong h;

    if (err && err_cap) err[0] = '\0';
    if (!s || !url) return NULL;
    env = android_run_env(s);
    if (!env || !s->bt_class || !s->bt_listen_mid) return NULL;
    ju = (*env)->NewStringUTF(env, url);
    if (!ju) return NULL;
    h = (*env)->CallStaticLongMethod(env, s->bt_class, s->bt_listen_mid, ju);
    (*env)->DeleteLocalRef(env, ju);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    if (h == 0) {
        if (err && err_cap) {
            const char *m = "the Bluetooth server could not be opened";
            nj_size n = (nj_size)strlen(m);
            if (n > err_cap - 1) n = err_cap - 1;
            memcpy(err, m, n);
            err[n] = '\0';
        }
        return NULL;
    }
    return (void *)(intptr_t)h;
}

static void *android_bt_accept(void *ud, void *handle, nj_u4 timeout_ms)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    jlong h;

    if (!s || !handle) return NULL;
    env = android_run_env(s);
    if (!env || !s->bt_class || !s->bt_accept_mid) return NULL;
    h = (*env)->CallStaticLongMethod(env, s->bt_class, s->bt_accept_mid,
                                     (jlong)(intptr_t)handle,
                                     (jint)(timeout_ms ? timeout_ms : 250u));
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    return h ? (void *)(intptr_t)h : NULL;
}

static void *android_bt_connect(void *ud, const char *url, nj_u4 timeout_ms,
                                char *err, nj_size err_cap)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    jstring ju;
    jlong h;

    if (err && err_cap) err[0] = '\0';
    if (!s || !url) return NULL;
    env = android_run_env(s);
    if (!env || !s->bt_class || !s->bt_connect_mid) return NULL;
    ju = (*env)->NewStringUTF(env, url);
    if (!ju) return NULL;
    h = (*env)->CallStaticLongMethod(env, s->bt_class, s->bt_connect_mid, ju,
                                     (jint)(timeout_ms ? timeout_ms : 30000u));
    (*env)->DeleteLocalRef(env, ju);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    if (h == 0) {
        if (err && err_cap) {
            const char *m = "the Bluetooth device could not be reached";
            nj_size n = (nj_size)strlen(m);
            if (n > err_cap - 1) n = err_cap - 1;
            memcpy(err, m, n);
            err[n] = '\0';
        }
        return NULL;
    }
    return (void *)(intptr_t)h;
}

static int android_bt_read(void *ud, void *handle, nj_u1 *buf, int len,
                           nj_u4 timeout_ms)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    jbyteArray arr;
    jint n;

    (void)timeout_ms;
    if (!s || !handle || !buf || len <= 0) return -1;
    env = android_run_env(s);
    if (!env || !s->bt_class || !s->bt_read_mid) return -1;
    arr = (*env)->NewByteArray(env, (jsize)len);
    if (!arr) return -1;
    n = (*env)->CallStaticIntMethod(env, s->bt_class, s->bt_read_mid,
                                    (jlong)(intptr_t)handle, arr, (jint)0,
                                    (jint)len);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, arr);
        return -1;
    }
    if (n > 0) {
        (*env)->GetByteArrayRegion(env, arr, 0, (jsize)n, (jbyte *)buf);
    }
    (*env)->DeleteLocalRef(env, arr);
    return (int)n;
}

static int android_bt_write(void *ud, void *handle, const nj_u1 *buf, int len)
{
    android_state *s = AS(ud);
    JNIEnv *env;
    jbyteArray arr;
    jint n;

    if (!s || !handle || !buf || len <= 0) return -1;
    env = android_run_env(s);
    if (!env || !s->bt_class || !s->bt_write_mid) return -1;
    arr = (*env)->NewByteArray(env, (jsize)len);
    if (!arr) return -1;
    (*env)->SetByteArrayRegion(env, arr, 0, (jsize)len, (const jbyte *)buf);
    n = (*env)->CallStaticIntMethod(env, s->bt_class, s->bt_write_mid,
                                    (jlong)(intptr_t)handle, arr, (jint)0,
                                    (jint)len);
    (*env)->DeleteLocalRef(env, arr);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return -1;
    }
    return (int)n;
}

static void android_bt_close(void *ud, void *handle)
{
    android_state *s = AS(ud);
    JNIEnv *env;

    if (!s || !handle) return;
    env = android_run_env(s);
    if (!env || !s->bt_class || !s->bt_close_mid) return;
    (*env)->CallStaticVoidMethod(env, s->bt_class, s->bt_close_mid,
                                 (jlong)(intptr_t)handle);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
}

/* Construction and the public configuration entry points              */
/* ------------------------------------------------------------------ */
/*
 * nj_port_android(): the default 240x320 MIDlet screen.
 * nj_port_android_ex(): pick the logical screen - a title that gates on the
 * screen size needs one it will accept, and the Surface is scaled to fit.
 */
nj_status nj_port_android_ex(nj_port *out, int screen_w, int screen_h);

nj_status nj_port_android(nj_port *out)
{
    return nj_port_android_ex(out, 0, 0);
}

nj_status nj_port_android_ex(nj_port *out, int screen_w, int screen_h)
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

    /* The logical screen.  display_init() keeps this and scales it into the
     * Surface, so a title that refuses anything above 250 in either axis gets a
     * screen it accepts instead of a 1080x2400 phone. */
    s->width = (screen_w > 0) ? screen_w : NJ_SCREEN_WIDTH;
    s->height = (screen_h > 0) ? screen_h : NJ_SCREEN_HEIGHT;
    s->ptr_id = -1;
    s->view_mode = NJ_VIEW_INT;
    s->view_scale = 1.0f;
    s->view_zoom = 1.0f;
    s->view_filter = 0;            /* nearest: what every port has always done */

    memset(out, 0, sizeof(*out));
    out->name = "android";
    out->ud = s;
    out->millis = android_millis;
    out->sleep_ms = android_sleep_ms;
    out->idle = android_idle;
    out->now = android_now;
    out->display_init = android_display_init;
    out->display_present = android_display_present;
    out->input_poll = android_input_poll;
    out->input_push = android_input_push;
    out->dir_open = android_dir_open;
    out->dir_next = android_dir_next;
    out->dir_close = android_dir_close;
    out->device_property = android_device_property;
    out->vibrate = android_vibrate;
    out->tone_play = android_tone_play;
    out->tone_stop = android_tone_stop;
    out->pcm_open = android_pcm_open;
    out->pcm_write = android_pcm_write;
    out->pcm_close = android_pcm_close;
    out->pcm_free = android_pcm_free;
    /*
     * MIDI goes to the platform's synthesizer.  All eight are set, and the
     * port still works when the Java class is missing: midi_open answers NULL
     * and the runtime renders the file itself.
     */
    out->midi_open = android_midi_open;
    out->midi_start = android_midi_start;
    out->midi_stop = android_midi_stop;
    out->midi_close = android_midi_close;
    out->midi_seek = android_midi_seek;
    out->midi_time = android_midi_time;
    out->midi_volume = android_midi_volume;
    out->midi_finished = android_midi_finished;
    /*
     * The platform's codecs.  install_codecs() is called once the VM exists;
     * the other three are plain fields the runtime reads when it needs them.
     */
    out->install_codecs = android_install_codecs;
    out->inflate = android_inflate;
    out->inflate_raw = android_inflate_raw;
    out->audio_decode = android_audio_decode;
    /*
     * http_request is set even when PlatformHttp did not resolve: the hook
     * then answers NJ_ERR_UNSUPPORTED, which is the same answer a port with no
     * network stack gives, and the runtime refuses http:// by name either way.
     */
    out->http_request = android_http_request;
    /*
     * JSR 256.  All four are set for the same reason as http_request: with no
     * PlatformSensor the list comes back empty - SensorManager.findSensors()
     * finds nothing on a device that has no bridge - rather than the runtime
     * claiming the package is missing.  A MIDlet that asks for a sensor it
     * cannot have gets the spec's ConnectionNotFoundException either way.
     */
    out->sensor_list = android_sensor_list;
    out->sensor_open = android_sensor_open;
    out->sensor_read = android_sensor_read;
    out->sensor_close = android_sensor_close;
    /*
     * Sockets, through java.net.  A missing bridge class leaves these NULL and
     * Connector.open("socket://...") then refuses by name - the same answer a
     * desktop build gives, which is what a title that needs the network has to
     * be able to tell apart from a server that is down.
     */
    out->socket_connect = android_socket_connect;
    out->socket_read = android_socket_read;
    out->socket_write = android_socket_write;
    out->socket_close = android_socket_close;
    /*
     * Bluetooth: discovery through the platform's adapter, and connections
     * through BluetoothServerSocket/BluetoothSocket.  Every one of these is
     * optional in the same way the rest of the port is - a device without a
     * radio leaves available() at 0 and the runtime says so, and an accept()
     * that times out is "nobody yet", not an error.
     */
    out->bt_local = android_bt_local;
    out->bt_discoverable = android_bt_discoverable;
    out->bt_inquiry = android_bt_inquiry;
    out->bt_services = android_bt_services;
    out->bt_connect = android_bt_connect;
    out->bt_listen = android_bt_listen;
    out->bt_accept = android_bt_accept;
    out->bt_read = android_bt_read;
    out->bt_write = android_bt_write;
    out->bt_close = android_bt_close;
    out->log = android_log;
    out->shutdown = android_shutdown;
    out->fps_cap = 30;
    return NJ_OK;
}

void nj_port_android_set_window(nj_port *port, void *native_window)
{
    android_state *s;
    ANativeWindow *old;

    if (!port || !port->ud) {
        return;
    }
    s = AS(port->ud);
    if (s->window == (ANativeWindow *)native_window) {
        return;
    }
    pthread_mutex_lock(&s->lock);
    old = s->window;
    s->window = (ANativeWindow *)native_window;
    if (s->window) {
        ANativeWindow_acquire(s->window);
        /*
         * Ask for the format the blit knows how to fill, so the bytes this
         * port writes are the bytes the compositor reads.  A SurfaceView that
         * comes up RGB_565 would otherwise take the other packing - handled,
         * but not what a phone with a 24-bit panel should be showing.
         * Impossible requests are ignored: display_present() reads the format
         * back from every lock and packs for whatever it finds.
         */
        (void)ANativeWindow_setBuffersGeometry(s->window, 0, 0,
                                               WINDOW_FORMAT_RGBA_8888);
        /* The WINDOW size, not the screen size.  Overwriting s->width here is
         * what kept this port handing the MIDlet a 640x1280 screen after
         * nj_port_android_ex() had set it to 176x208 - the window used to be
         * the screen, and this line is what was left of that. */
        s->win_w = ANativeWindow_getWidth(s->window);
        s->win_h = ANativeWindow_getHeight(s->window);
    }
    pthread_mutex_unlock(&s->lock);
    /* Outside the lock: the reference this port held is dropped, and a frame
     * that is presenting right now holds its own. */
    if (old) {
        ANativeWindow_release(old);
    }
}

void nj_port_android_set_paths(nj_port *port, const char *rms_dir,
                               const char *fs_dir)
{
    android_state *s;

    if (!port || !port->ud) {
        return;
    }
    s = AS(port->ud);
    if (rms_dir && rms_dir[0]) {
        snprintf(s->rms_dir, sizeof(s->rms_dir), "%s", rms_dir);
        port->rms_dir = s->rms_dir;
    }
    if (fs_dir && fs_dir[0]) {
        snprintf(s->fs_dir, sizeof(s->fs_dir), "%s", fs_dir);
        port->fs_dir = s->fs_dir;
    }
}

void nj_port_android_push_event(nj_port *port, const nj_event *ev)
{
    if (port && port->ud && ev) {
        android_push(port->ud, ev);
    }
}

/*
 * How the game's picture is placed in the window.
 *
 * `mode` is one of the NJ_VIEW_* values in this file and decides the base fit;
 * `zoom` is a multiplier on that fit (1.0 = the default size) and `dx`/`dy`
 * are a pan in window pixels.  Zoom and pan are the player's in every mode, not
 * just NJ_VIEW_CUSTOM: moving the picture must not cost the crisp integer
 * scaling.  Safe to call at any time from the UI thread: display_present()
 * reads all four under the same mutex it publishes the transform with, and
 * nj_port_android_view_limits() reports how far a pan may actually go.
 */
void nj_port_android_set_view(nj_port *port, int mode, float zoom, float dx,
                              float dy)
{
    android_state *s;

    if (!port || !port->ud) {
        return;
    }
    s = AS(port->ud);
    if (mode < 0 || mode >= NJ_VIEW_MODE_COUNT) {
        return;
    }
    pthread_mutex_lock(&s->lock);
    s->view_mode = mode;
    s->view_zoom = (zoom > 0.05f) ? zoom : 1.0f;
    s->view_dx = dx;
    s->view_dy = dy;
    pthread_mutex_unlock(&s->lock);
}

/*
 * How far the picture may be panned in the mode and zoom now in force, in
 * window pixels, as *max_x / *max_y.
 *
 * display_present() clamps the picture to the panel with exactly these
 * numbers; the UI clamps the pan it stores with them too, so that a long drag
 * cannot wind up a pan the player then has to unwind before the picture moves
 * the other way again.  Returns 0 until a frame has published them, so a pan
 * restored from the launcher is not clamped against limits nobody measured.
 */
int nj_port_android_view_limits(nj_port *port, int *max_x, int *max_y)
{
    android_state *s;

    if (!port || !port->ud) {
        return 0;
    }
    s = AS(port->ud);
    pthread_mutex_lock(&s->lock);
    if (!s->view_limits_valid) {
        pthread_mutex_unlock(&s->lock);
        return 0;
    }
    if (max_x) {
        *max_x = s->view_max_pan_x;
    }
    if (max_y) {
        *max_y = s->view_max_pan_y;
    }
    pthread_mutex_unlock(&s->lock);
    return 1;
}

/*
 * The area of the logical screen the MIDlet has painted, as
 * {x, y, w, h, frames}.  Returns 1 when the box is worth using.
 *
 * This is what the launcher shows as "detected resolution" and what 适配画面
 * crops to.  The last element is how many frames the measurement has seen: a
 * box that appeared after two frames is a game still starting up, not one that
 * only paints a corner, and the caller can wait for more before trusting it.
 */
int nj_port_android_detect(const nj_port *port, int out[5])
{
    android_state *s;

    if (!port || !port->ud) {
        return 0;
    }
    s = AS((void *)port->ud);
    if (!out) {
        return 0;
    }
    pthread_mutex_lock((pthread_mutex_t *)&s->lock);
    out[0] = s->det_x;
    out[1] = s->det_y;
    out[2] = s->det_w;
    out[3] = s->det_h;
    out[4] = s->det_frames;
    pthread_mutex_unlock((pthread_mutex_t *)&s->lock);
    return (s->det_w >= 16 && s->det_h >= 16) ? 1 : 0;
}

int nj_port_android_stats(const nj_port *port, char *buf, nj_size cap)
{
    android_state *s;
    android_audio *a;
    int n;

    if (!port || !port->ud || !buf || cap == 0) {
        return NJ_ERR;
    }
    s = AS((void *)port->ud);
    a = s->audio;
    n = snprintf(buf, cap,
                 "screen=%dx%d window=%dx%d scale=%.2f frames=%ld keys=%d "
                 "ptr=%d paused=%d view=%d zoom=%.2f det=%dx%d@%d,%d f=%d%s "
                 "speed=%d audio=%s",
                 s->width, s->height, s->win_w, s->win_h, (double)s->view_scale,
                 s->frames_presented, s->key_events, s->ptr_events,
                 s->paused, s->view_mode, (double)s->view_zoom,
                 s->det_w, s->det_h, s->det_x, s->det_y, s->det_frames,
                 s->det_done ? "" : "?",
                 s->speed_applied > 0 ? s->speed_applied : 100,
                 a ? (a->failed ? "failed"
                                : (a->stream_paused ? "paused" : "on")) : "off");
    if (n > 0 && n < (int)cap && a && a->stream) {
        n += snprintf(buf + n, (size_t)((int)cap - n),
                      " rate=%d rates=%lld tones=%lld pcm=%lld",
                      a->out_rate, a->frames_rendered, a->tones_played,
                      a->pcm_frames);
        if (n > (int)cap) {
            n = (int)cap;
        }
    }
    return n;
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
    int        joined;
} android_app;

static void *android_main_thread(void *arg)
{
    android_app *a = (android_app *)arg;
    android_state *s = AS(a->port.ud);

    a->opts.jar_path = a->jar_path;
    /*
     * Hand the runtime the platform's glyphs.  Set here rather than in
     * nativeCreate because the faces live in this run's state: two Activities
     * in one process must not share a glyph cache, and a finished run must not
     * leave a face pointing at a freed cache.
     *
     * Primary, not fallback: a fallback is only consulted for characters the
     * built-in cell cannot draw, so with the faces behind the slot every ASCII
     * character was still measured and inked by the bundled bitmap face - the
     * Latin the player sees has to come from the phone's typeface too, which
     * means replacing the slot.  The same faces are repeated as the fallback,
     * so a glyph the platform itself cannot draw still ends up here.
     */
    if (s && s->glyph_mid && s->face_ptrs[0]) {
        a->opts.primary_fonts = s->face_ptrs;
        a->opts.primary_count = NJ_FONTSET_SLOTS;
        a->opts.fallback_fonts = s->face_ptrs;
        a->opts.fallback_count = NJ_FONTSET_SLOTS;
    }
    /*
     * Resume instead of starting, when the player picked "恢复进度".  The
     * runner loads the blob right after it has created the VM and attached the
     * jar, and a successful load replaces the MIDlet start: the restored thread
     * resumes in the middle of a method.
     */
    if (s) {
        pthread_mutex_lock(&s->lock);
        if (s->restore && s->restore_path[0] != '\0') {
            a->opts.load_state_path = s->restore_path;
        }
        pthread_mutex_unlock(&s->lock);
    }
    (void)nj_run(&a->port, &a->opts);
    if (s && s->run_attached && s->jvm) {
        (*s->jvm)->DetachCurrentThread(s->jvm);
        s->run_attached = 0;
        s->run_env = NULL;
    }
    a->running = 0;
    return NULL;
}

/* Copy a jstring into a fixed buffer; returns 1 if anything was copied. */
static int android_jstr(JNIEnv *env, jstring js, char *dst, size_t cap)
{
    const char *p;

    if (!js || !dst || cap == 0) {
        return 0;
    }
    p = (*env)->GetStringUTFChars(env, js, NULL);
    if (!p) {
        return 0;
    }
    snprintf(dst, cap, "%s", p);
    (*env)->ReleaseStringUTFChars(env, js, p);
    return 1;
}

JNIEXPORT jlong JNICALL
Java_com_lucinhu_nj2me_GameActivity_nativeCreate(JNIEnv *env, jobject thiz,
                                                 jobject surface,
                                                 jstring jarPath,
                                                 jint screenW, jint screenH,
                                                 jint heapMB,
                                                 jstring rmsDir,
                                                 jstring fsDir)
{
    android_app *a;
    android_state *s;
    ANativeWindow *win;
    char rms[512], fsd[512];

    (void)thiz;
    a = (android_app *)calloc(1u, sizeof(*a));
    if (!a) {
        return 0;
    }
    android_jstr(env, jarPath, a->jar_path, sizeof(a->jar_path));

    if (nj_port_android_ex(&a->port, (int)screenW, (int)screenH) != NJ_OK) {
        free(a);
        return 0;
    }
    s = AS(a->port.ud);

    rms[0] = fsd[0] = '\0';
    android_jstr(env, rmsDir, rms, sizeof(rms));
    android_jstr(env, fsDir, fsd, sizeof(fsd));
    nj_port_android_set_paths(&a->port, rms[0] ? rms : NULL,
                              fsd[0] ? fsd : NULL);

    if (heapMB > 0) {
        a->opts.heap_bytes = (nj_size)heapMB * 1024u * 1024u;
    } else {
        a->opts.heap_bytes = 0;        /* 0 => NJ_MAX_HEAP */
    }

    /*
     * The vibrate callback needs the class, not the instance, and it needs it
     * from a thread that is not the UI thread.  A global reference is what
     * survives this call; nativeDestroy() releases it.
     */
    {
        jclass cls = (*env)->GetObjectClass(env, thiz);
        if (cls) {
            jmethodID mid = (*env)->GetStaticMethodID(env, cls, "vibrateDevice",
                                                      "(I)V");
            if ((*env)->ExceptionCheck(env)) {
                (*env)->ExceptionClear(env);
                mid = NULL;
            }
            if (mid) {
                s->activity_class = (jclass)(*env)->NewGlobalRef(env, cls);
                s->vibrate_mid = mid;
            }
            (*env)->DeleteLocalRef(env, cls);
        }
    }
    (*env)->GetJavaVM(env, &s->jvm);

    /*
     * The platform's text stack, for the characters the built-in bitmap faces
     * cannot draw.  Resolved here, on the UI thread, while a class loader is
     * unambiguously the app's; the run thread only calls the cached method.
     *
     * (int codePoint, int px, int bold) -> coverage rows.  The style is passed
     * rather than inferred because the runtime answers bold from a second
     * bitmap table of its own; a platform face that was not told would draw
     * the plain glyph twice.
     */
    {
        jclass gcls = (*env)->FindClass(env, "com/lucinhu/nj2me/GlyphSource");
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            gcls = NULL;
        }
        if (gcls) {
            jmethodID gmid = (*env)->GetStaticMethodID(env, gcls, "render",
                                                       "(III)[B");
            if ((*env)->ExceptionCheck(env)) {
                (*env)->ExceptionClear(env);
                gmid = NULL;
            }
            if (gmid) {
                s->glyph_class = (jclass)(*env)->NewGlobalRef(env, gcls);
                s->glyph_mid = gmid;
            }
            (*env)->DeleteLocalRef(env, gcls);
        }
        android_glyph_init(s);
    }

    /*
     * The platform's MIDI synthesizer, behind MidiPlayer.java.  Same reasoning
     * as the glyphs above: resolved here, on the UI thread, where the class
     * loader is unambiguously the app's, and cached as a global reference.
     *
     * A missing class is NOT an error: every hook checks its own method id and
     * declines, which leaves the runtime rendering MIDI the way it does on the
     * desktop.  That is what keeps this port buildable against an app that has
     * no MidiPlayer at all.
     */
    {
        jclass mcls = (*env)->FindClass(env, "com/lucinhu/nj2me/MidiPlayer");
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            mcls = NULL;
        }
        if (mcls) {
            jmethodID m = (*env)->GetStaticMethodID(env, mcls, "open", "([B)J");
            if ((*env)->ExceptionCheck(env)) {
                (*env)->ExceptionClear(env);
                m = NULL;
            }
            if (m) {
                s->midi_class = (jclass)(*env)->NewGlobalRef(env, mcls);
                s->midi_open_mid = m;
                s->midi_length_mid = (*env)->GetStaticMethodID(env, mcls,
                                                               "length", "(J)I");
                s->midi_start_mid = (*env)->GetStaticMethodID(env, mcls,
                                                              "start", "(JZ)Z");
                s->midi_stop_mid = (*env)->GetStaticMethodID(env, mcls,
                                                             "stop", "(J)V");
                s->midi_close_mid = (*env)->GetStaticMethodID(env, mcls,
                                                              "close", "(J)V");
                s->midi_seek_mid = (*env)->GetStaticMethodID(env, mcls,
                                                             "seek", "(JI)V");
                s->midi_time_mid = (*env)->GetStaticMethodID(env, mcls,
                                                             "time", "(J)I");
                s->midi_volume_mid = (*env)->GetStaticMethodID(env, mcls,
                                                               "volume", "(JI)V");
                s->midi_done_mid = (*env)->GetStaticMethodID(env, mcls,
                                                             "finished", "(J)Z");
                if ((*env)->ExceptionCheck(env)) {
                    (*env)->ExceptionClear(env);
                }
            }
            (*env)->DeleteLocalRef(env, mcls);
        }
    }

    /*
     * The platform's image and audio codecs, behind PlatformImage.java and
     * PlatformAudio.java.  Resolved here for the same reason as the two above,
     * and optional in the same way: a missing class means the provider is not
     * installed and the runtime decodes whatever it can by itself.
     */
    {
        jclass icls = (*env)->FindClass(env, "com/lucinhu/nj2me/PlatformImage");
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            icls = NULL;
        }
        if (icls) {
            jmethodID iinfo = (*env)->GetStaticMethodID(env, icls, "info",
                                                        "([B)[I");
            jmethodID idec = (*env)->GetStaticMethodID(env, icls, "decode",
                                                       "([BII)[B");
            if ((*env)->ExceptionCheck(env)) {
                (*env)->ExceptionClear(env);
                iinfo = NULL;
                idec = NULL;
            }
            if (iinfo && idec) {
                s->image_class = (jclass)(*env)->NewGlobalRef(env, icls);
                s->image_info_mid = iinfo;
                s->image_decode_mid = idec;
            }
            (*env)->DeleteLocalRef(env, icls);
        }
    }
    {
        jclass acls = (*env)->FindClass(env, "com/lucinhu/nj2me/PlatformAudio");
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            acls = NULL;
        }
        if (acls) {
            jmethodID ainfo = (*env)->GetStaticMethodID(env, acls, "info",
                                                        "([B)[I");
            jmethodID adec = (*env)->GetStaticMethodID(env, acls, "decode",
                                                       "([B)[B");
            if ((*env)->ExceptionCheck(env)) {
                (*env)->ExceptionClear(env);
                ainfo = NULL;
                adec = NULL;
            }
            if (ainfo && adec) {
                s->audio_class = (jclass)(*env)->NewGlobalRef(env, acls);
                s->audio_info_mid = ainfo;
                s->audio_decode_mid = adec;
            }
            (*env)->DeleteLocalRef(env, acls);
        }
    }
    {
        /*
         * PlatformHttp.java.  Unlike the two bridges above this one is a plain
         * class with no Context at all: HttpURLConnection needs no permission
         * object of its own, only the manifest's INTERNET permission, which is
         * checked by the framework when the socket is opened.  A missing class
         * leaves the hook answering NJ_ERR_UNSUPPORTED.
         */
        jclass hcls = (*env)->FindClass(env, "com/lucinhu/nj2me/PlatformHttp");
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            hcls = NULL;
        }
        if (hcls) {
            jmethodID hreq = (*env)->GetStaticMethodID(
                env, hcls, "request",
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;[B)J");
            jmethodID hstat = (*env)->GetStaticMethodID(env, hcls, "status",
                                                        "(J)I");
            jmethodID hhead = (*env)->GetStaticMethodID(env, hcls, "headers",
                                                        "(J)Ljava/lang/String;");
            jmethodID hbody = (*env)->GetStaticMethodID(env, hcls, "body",
                                                        "(J)[B");
            jmethodID hclose = (*env)->GetStaticMethodID(env, hcls, "close",
                                                         "(J)V");
            if ((*env)->ExceptionCheck(env)) {
                (*env)->ExceptionClear(env);
                hreq = NULL;
            }
            if (hreq && hstat && hhead && hbody && hclose) {
                s->http_class = (jclass)(*env)->NewGlobalRef(env, hcls);
                s->http_request_mid = hreq;
                s->http_status_mid = hstat;
                s->http_headers_mid = hhead;
                s->http_body_mid = hbody;
                s->http_close_mid = hclose;
            } else {
                /* Name what is missing: a silent NULL here reads as "this
                 * build has no HTTP", which is a different problem. */
                __android_log_print(ANDROID_LOG_WARN, "nj2me",
                                    "http: PlatformHttp is present but its "
                                    "request API is not; http:// stays "
                                    "unavailable for this run");
            }
            (*env)->DeleteLocalRef(env, hcls);
        }
    }

    {
        /*
         * PlatformSensor.java, for JSR 256.  It needs a Context only to ask
         * the SensorManager what the device has, so a missing class simply
         * leaves the hook answering "no sensors" - the same answer a device
         * without an accelerometer gives.
         */
        jclass scls = (*env)->FindClass(env, "com/lucinhu/nj2me/PlatformSensor");
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            scls = NULL;
        }
        if (scls) {
            jmethodID slist = (*env)->GetStaticMethodID(
                env, scls, "list", "()[Ljava/lang/String;");
            jmethodID sopen = (*env)->GetStaticMethodID(
                env, scls, "open", "(Ljava/lang/String;I)J");
            jmethodID sread = (*env)->GetStaticMethodID(env, scls, "latest",
                                                        "(J)[D");
            jmethodID sclose = (*env)->GetStaticMethodID(env, scls, "close",
                                                         "(J)V");
            if ((*env)->ExceptionCheck(env)) {
                (*env)->ExceptionClear(env);
                slist = NULL;
            }
            if (slist && sopen && sread && sclose) {
                s->sensor_class = (jclass)(*env)->NewGlobalRef(env, scls);
                s->sensor_list_mid = slist;
                s->sensor_open_mid = sopen;
                s->sensor_read_mid = sread;
                s->sensor_close_mid = sclose;
            } else {
                __android_log_print(ANDROID_LOG_WARN, "nj2me",
                                    "sensor: PlatformSensor is present but its "
                                    "list API is not; findSensors() will come "
                                    "back empty for this run");
            }
            (*env)->DeleteLocalRef(env, scls);
        }
    }

    {
        /*
         * PlatformBluetooth.java, for javax.bluetooth.  The bridge holds the
         * discovery broadcast receiver and the list of devices it has seen, so
         * it needs a Context; available() answers 0 without an adapter or
         * without BLUETOOTH_CONNECT, and the runtime turns that into
         * BluetoothStateException.  None of the ten methods is fatal to miss
         * except available(), which is the one that decides whether the hooks
         * answer at all.
         */
        jclass bcls = (*env)->FindClass(env,
                                        "com/lucinhu/nj2me/PlatformBluetooth");
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            bcls = NULL;
        }
        if (bcls) {
            jmethodID bavail = (*env)->GetStaticMethodID(env, bcls, "available",
                                                         "()I");
            jmethodID baddr = (*env)->GetStaticMethodID(
                env, bcls, "address", "()Ljava/lang/String;");
            jmethodID bname = (*env)->GetStaticMethodID(
                env, bcls, "name", "()Ljava/lang/String;");
            jmethodID bclass = (*env)->GetStaticMethodID(env, bcls, "deviceClass",
                                                         "()I");
            jmethodID bdisc = (*env)->GetStaticMethodID(env, bcls,
                                                        "setDiscoverable", "(I)I");
            jmethodID bstart = (*env)->GetStaticMethodID(env, bcls,
                                                         "inquiryStart", "(I)I");
            jmethodID bdevs = (*env)->GetStaticMethodID(
                env, bcls, "inquiryDevices", "()[Ljava/lang/String;");
            jmethodID bstop = (*env)->GetStaticMethodID(env, bcls, "inquiryStop",
                                                       "()V");
            jmethodID bsvcs = (*env)->GetStaticMethodID(
                env, bcls, "services",
                "(Ljava/lang/String;Ljava/lang/String;)[Ljava/lang/String;");
            /*
             * The connection half.  A missing one is not fatal - the runtime
             * treats a NULL hook as "this port cannot connect" and refuses the
             * URL by name - but the ids are looked up once here so the hooks
             * do not resolve them per call.
             */
            jmethodID blisten = (*env)->GetStaticMethodID(
                env, bcls, "listen", "(Ljava/lang/String;)J");
            jmethodID baccept = (*env)->GetStaticMethodID(env, bcls, "accept",
                                                          "(JI)J");
            jmethodID bconnect = (*env)->GetStaticMethodID(
                env, bcls, "connect", "(Ljava/lang/String;I)J");
            jmethodID bread = (*env)->GetStaticMethodID(env, bcls, "read",
                                                        "(J[BII)I");
            jmethodID bwrite = (*env)->GetStaticMethodID(env, bcls, "write",
                                                         "(J[BII)I");
            jmethodID bclose = (*env)->GetStaticMethodID(env, bcls, "close",
                                                         "(J)V");
            if ((*env)->ExceptionCheck(env)) {
                (*env)->ExceptionClear(env);
                bavail = NULL;
            }
            if (bavail) {
                s->bt_class = (jclass)(*env)->NewGlobalRef(env, bcls);
                s->bt_available_mid = bavail;
                s->bt_address_mid = baddr;
                s->bt_name_mid = bname;
                s->bt_class_mid = bclass;
                s->bt_discoverable_mid = bdisc;
                s->bt_inquiry_start_mid = bstart;
                s->bt_inquiry_devices_mid = bdevs;
                s->bt_inquiry_stop_mid = bstop;
                s->bt_services_mid = bsvcs;
                s->bt_listen_mid = blisten;
                s->bt_accept_mid = baccept;
                s->bt_connect_mid = bconnect;
                s->bt_read_mid = bread;
                s->bt_write_mid = bwrite;
                s->bt_close_mid = bclose;
            } else {
                __android_log_print(ANDROID_LOG_WARN, "nj2me",
                                    "bluetooth: PlatformBluetooth is present "
                                    "but its available() method is not; this "
                                    "run has no Bluetooth radio");
            }
            (*env)->DeleteLocalRef(env, bcls);
        }
    }

    {
        /*
         * PlatformSocket.java, for javax.microedition.io.SocketConnection.  It
         * needs no Context - a TCP socket is not a permission the app asks for
         * - so a missing class means Connector.open("socket://") refuses by
         * name, which is the same answer a build without a socket stack gives.
         */
        jclass kcls = (*env)->FindClass(env, "com/lucinhu/nj2me/PlatformSocket");
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            kcls = NULL;
        }
        if (kcls) {
            jmethodID kconn = (*env)->GetStaticMethodID(
                env, kcls, "connect", "(Ljava/lang/String;I)J");
            jmethodID kaddr = (*env)->GetStaticMethodID(
                env, kcls, "address", "(J)Ljava/lang/String;");
            jmethodID kport = (*env)->GetStaticMethodID(env, kcls, "port", "(J)I");
            jmethodID kladdr = (*env)->GetStaticMethodID(
                env, kcls, "localAddress", "(J)Ljava/lang/String;");
            jmethodID klport = (*env)->GetStaticMethodID(
                env, kcls, "localPort", "(J)I");
            jmethodID kread = (*env)->GetStaticMethodID(
                env, kcls, "read", "(J[BII)I");
            jmethodID kwrite = (*env)->GetStaticMethodID(
                env, kcls, "write", "(J[BI)I");
            jmethodID kclose = (*env)->GetStaticMethodID(env, kcls, "close", "(J)V");
            if ((*env)->ExceptionCheck(env)) {
                (*env)->ExceptionClear(env);
                kconn = NULL;
            }
            if (kconn && kaddr && kport && kladdr && klport && kread && kwrite
                && kclose) {
                s->socket_class = (jclass)(*env)->NewGlobalRef(env, kcls);
                s->socket_connect_mid = kconn;
                s->socket_address_mid = kaddr;
                s->socket_port_mid = kport;
                s->socket_local_mid = kladdr;
                s->socket_local_port_mid = klport;
                s->socket_read_mid = kread;
                s->socket_write_mid = kwrite;
                s->socket_close_mid = kclose;
            } else {
                __android_log_print(ANDROID_LOG_WARN, "nj2me",
                                    "socket: PlatformSocket is present but its "
                                    "connect API is not; socket:// stays "
                                    "unavailable for this run");
            }
            (*env)->DeleteLocalRef(env, kcls);
        }
    }

    win = ANativeWindow_fromSurface(env, surface);
    nj_port_android_set_window(&a->port, win);
    if (win) {
        ANativeWindow_release(win);   /* the port holds its own reference */
    }
    return (jlong)(intptr_t)a;
}

JNIEXPORT void JNICALL
Java_com_lucinhu_nj2me_GameActivity_nativeSetSurface(JNIEnv *env, jobject thiz,
                                                     jlong handle,
                                                     jobject surface)
{
    android_app *a = (android_app *)(intptr_t)handle;
    ANativeWindow *win;
    (void)thiz;
    if (!a) {
        return;
    }
    /* NULL is a real value here: the activity hands the surface back on stop,
     * and the port must stop drawing rather than lock a window whose surface is
     * gone. */
    win = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    nj_port_android_set_window(&a->port, win);
    if (win) {
        ANativeWindow_release(win);
    }
}

/*
 * Runs the MIDlet, and RETURNS WHEN THE MIDLET IS OVER.
 *
 * The blocking contract matters more than it looks.  Java calls this on its own
 * thread and only then tears the port down, so if this returned as soon as the
 * pthread was created, that thread would go straight to nativeDestroy() and
 * sit in pthread_join() for the whole game.  The failure mode is not subtle:
 * the caller holds a lock across the join, so every UI-thread call that needs
 * the same lock - the status tick, key delivery - blocks until the player
 * quits, and the system declares the app unresponsive 5 seconds in.  Joining
 * here also means the caller's own join in nativeDestroy() is instant.
 *
 * It deliberately does NOT hold the caller's lock while waiting: a surface that
 * goes away mid-frame can park the rasterizer inside the graphics driver, and
 * the UI thread must stay free to show the last frame and leave the screen.
 */
JNIEXPORT void JNICALL
Java_com_lucinhu_nj2me_GameActivity_nativeRun(JNIEnv *env, jobject thiz,
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
        return;
    }
    pthread_join(a->thread, NULL);
    a->joined = 1;
    a->running = 0;
}

JNIEXPORT void JNICALL
Java_com_lucinhu_nj2me_GameActivity_nativeKey(JNIEnv *env, jobject thiz,
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
Java_com_lucinhu_nj2me_GameActivity_nativeTouch(JNIEnv *env, jobject thiz,
                                                jlong handle, jint id,
                                                jint phase, jfloat x, jfloat y)
{
    android_app *a = (android_app *)(intptr_t)handle;
    (void)env; (void)thiz;
    if (a) {
        nj_port_android_touch(&a->port, (int)id, (int)phase, (float)x,
                              (float)y);
    }
}

JNIEXPORT void JNICALL
Java_com_lucinhu_nj2me_GameActivity_nativeSetActive(JNIEnv *env, jobject thiz,
                                                    jlong handle,
                                                    jboolean active)
{
    android_app *a = (android_app *)(intptr_t)handle;
    (void)env; (void)thiz;
    if (a) {
        nj_port_android_set_active(&a->port, active ? 1 : 0);
    }
}

JNIEXPORT jstring JNICALL
Java_com_lucinhu_nj2me_GameActivity_nativeStats(JNIEnv *env, jobject thiz,
                                                jlong handle)
{
    android_app *a = (android_app *)(intptr_t)handle;
    char buf[256];
    (void)thiz;
    if (!a) {
        return NULL;
    }
    buf[0] = '\0';
    (void)nj_port_android_stats(&a->port, buf, sizeof(buf));
    return (*env)->NewStringUTF(env, buf);
}

/*
 * The player's choice of how the picture sits in the window.  `mode` is one of
 * the NJ_VIEW_* values in include/nj2me/nj_port.h and decides the base fit;
 * zoom and the pan stay the player's in every mode.  Called from the UI thread
 * while the VM runs, which the port handles by taking its own lock.
 */
JNIEXPORT void JNICALL
Java_com_lucinhu_nj2me_GameActivity_nativeSetView(JNIEnv *env, jobject thiz,
                                                  jlong handle, jint mode,
                                                  jfloat zoom, jfloat dx,
                                                  jfloat dy)
{
    android_app *a = (android_app *)(intptr_t)handle;
    (void)env; (void)thiz;
    if (a) {
        nj_port_android_set_view(&a->port, (int)mode, (float)zoom, (float)dx,
                                 (float)dy);
    }
}

/*
 * Game speed: 100 is real time, 200 twice as fast.  Applied by the run thread
 * on its next frame, so this only leaves a number behind.
 */
JNIEXPORT void JNICALL
Java_com_lucinhu_nj2me_GameActivity_nativeSetSpeed(JNIEnv *env, jobject thiz,
                                                   jlong handle, jint percent)
{
    android_app *a = (android_app *)(intptr_t)handle;
    (void)env; (void)thiz;
    if (a) {
        android_state *s = AS(a->port.ud);
        if (s) {
            s->speed_percent = (int)percent;
        }
    }
}

/*
 * Picture anti-aliasing: 1 = mix the neighbours at a fractional scale, 0 =
 * nearest neighbour (what every port has always done).
 *
 * Written by the UI thread, read by the present loop.  No lock for the same
 * reason the speed word above has none: it is one aligned int, and the worst
 * case of a torn read is a single frame drawn with the other filter.
 */
JNIEXPORT void JNICALL
Java_com_lucinhu_nj2me_GameActivity_nativeSetFilter(JNIEnv *env, jobject thiz,
                                                    jlong handle, jint linear)
{
    android_app *a = (android_app *)(intptr_t)handle;
    (void)env; (void)thiz;
    if (a) {
        android_state *s = AS(a->port.ud);
        if (s) {
            s->view_filter = linear ? 1 : 0;
        }
    }
}

/*
 * Drop every glyph the port has cached, so the next paint uses the typeface
 * and anti-aliasing the settings now say.  Remembered here, carried out at the
 * top of the next frame (see android_display_present): the cache belongs to the
 * run thread.
 */
JNIEXPORT void JNICALL
Java_com_lucinhu_nj2me_GameActivity_nativeGlyphReset(JNIEnv *env, jobject thiz,
                                                     jlong handle)
{
    android_app *a = (android_app *)(intptr_t)handle;
    (void)env; (void)thiz;
    if (a) {
        android_state *s = AS(a->port.ud);
        if (s) {
            s->glyph_flush = 1;
        }
    }
}

/*
 * State snapshots, from the UI thread's side.
 *
 * Restore is only remembered here: the run thread reads it before the MIDlet
 * starts, because a restored VM resumes in the middle of a method - there is
 * nothing to start.
 */
JNIEXPORT void JNICALL
Java_com_lucinhu_nj2me_GameActivity_nativeSetRestore(JNIEnv *env, jobject thiz,
                                                     jlong handle, jstring path)
{
    android_app *a = (android_app *)(intptr_t)handle;
    android_state *s;
    (void)thiz;
    if (!a || !(s = AS(a->port.ud))) {
        return;
    }
    pthread_mutex_lock(&s->lock);
    s->restore = android_jstr(env, path, s->restore_path, sizeof(s->restore_path));
    pthread_mutex_unlock(&s->lock);
}

/*
 * Save leaves the path for the run thread and waits for its answer.  Bounded,
 * because reporting "saved" before the file exists is worse than reporting a
 * timeout: the player would quit and lose the game twice.
 */
JNIEXPORT jint JNICALL
Java_com_lucinhu_nj2me_GameActivity_nativeSaveState(JNIEnv *env, jobject thiz,
                                                    jlong handle, jstring path)
{
    android_app *a = (android_app *)(intptr_t)handle;
    android_state *s;
    char pathbuf[512];
    int waited = 0;
    int rc = NJ_ERR_STATE;
    (void)thiz;
    if (!a || !(s = AS(a->port.ud))) {
        return NJ_ERR_STATE;
    }
    if (!android_jstr(env, path, pathbuf, sizeof(pathbuf))) {
        return NJ_ERR_STATE;
    }
    pthread_mutex_lock(&s->lock);
    if (s->save_req || s->save_busy) {
        pthread_mutex_unlock(&s->lock);
        return NJ_ERR_STATE;      /* one snapshot at a time */
    }
    snprintf(s->save_path, sizeof(s->save_path), "%s", pathbuf);
    s->save_done = 0;
    s->save_req = 1;
    pthread_mutex_unlock(&s->lock);

    while (waited < 4000) {
        pthread_mutex_lock(&s->lock);
        if (s->save_done) {
            rc = s->save_rc;
            s->save_done = 0;
            pthread_mutex_unlock(&s->lock);
            return rc;
        }
        pthread_mutex_unlock(&s->lock);
        usleep(5000);
        waited += 5;
    }
    return NJ_ERR_STATE;
}

/*
 * How far the pan may reach in the mode and zoom now in force, as
 * {max_x, max_y}, or NULL before the first frame has been presented.
 *
 * The launcher and the game's own 画面 dialog clamp the pan they store with
 * these numbers, so what the dialog reports is what the player sees.
 */
JNIEXPORT jintArray JNICALL
Java_com_lucinhu_nj2me_GameActivity_nativeViewLimits(JNIEnv *env, jobject thiz,
                                                     jlong handle)
{
    android_app *a = (android_app *)(intptr_t)handle;
    jint vals[2];
    jintArray arr;

    (void)thiz;
    if (!a || !nj_port_android_view_limits(&a->port, (int *)&vals[0],
                                           (int *)&vals[1])) {
        return NULL;
    }
    arr = (*env)->NewIntArray(env, 2);
    if (!arr) {
        return NULL;
    }
    (*env)->SetIntArrayRegion(env, arr, 0, 2, vals);
    return arr;
}

/*
 * What the port has measured the MIDlet to paint, as
 * {x, y, w, h, framesMeasured}, or NULL when it has nothing to report yet.
 *
 * The launcher calls this once, a few seconds into the first run of a game, to
 * remember the resolution and offer a screen size that fits; the in-game menu
 * calls it for the "detected" line and the 适配画面 action.  Null, not zeros,
 * because "not measured yet" and "measured an empty screen" are different
 * answers and a caller that cannot tell them apart would save 0x0.
 */
JNIEXPORT jintArray JNICALL
Java_com_lucinhu_nj2me_GameActivity_nativeDetect(JNIEnv *env, jobject thiz,
                                                 jlong handle)
{
    android_app *a = (android_app *)(intptr_t)handle;
    int box[5];
    jintArray arr;
    jint vals[5];
    int i;

    (void)thiz;
    if (!a) {
        return NULL;
    }
    if (!nj_port_android_detect(&a->port, box)) {
        return NULL;
    }
    for (i = 0; i < 5; i++) {
        vals[i] = (jint)box[i];
    }
    arr = (*env)->NewIntArray(env, 5);
    if (!arr) {
        return NULL;
    }
    (*env)->SetIntArrayRegion(env, arr, 0, 5, vals);
    return arr;
}

/*
 * The key codes the keypad editor may offer, as "code:label" pairs.  Generated
 * from the same table push_key() translates with, so the editor cannot offer a
 * button the port would ignore.
 */
JNIEXPORT jobjectArray JNICALL
Java_com_lucinhu_nj2me_GameActivity_nativeKeyTable(JNIEnv *env, jclass cls)
{
    jobjectArray arr;
    jclass strcls;
    int i;

    (void)cls;
    strcls = (*env)->FindClass(env, "java/lang/String");
    if (!strcls) {
        return NULL;
    }
    arr = (*env)->NewObjectArray(env, KEYMAP_N, strcls, NULL);
    if (!arr) {
        return NULL;
    }
    for (i = 0; i < KEYMAP_N; i++) {
        char line[64];
        jstring js;
        snprintf(line, sizeof(line), "%d:%s", k_keymap[i].android,
                 k_keymap[i].label);
        js = (*env)->NewStringUTF(env, line);
        if (!js) {
            return arr;
        }
        (*env)->SetObjectArrayElement(env, arr, i, js);
        (*env)->DeleteLocalRef(env, js);
    }
    return arr;
}

JNIEXPORT void JNICALL
Java_com_lucinhu_nj2me_GameActivity_nativeQuit(JNIEnv *env, jobject thiz,
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
Java_com_lucinhu_nj2me_GameActivity_nativeDestroy(JNIEnv *env, jobject thiz,
                                                  jlong handle)
{
    android_app *a = (android_app *)(intptr_t)handle;
    android_state *s;
    (void)thiz;
    if (!a) {
        return;
    }
    s = AS(a->port.ud);
    /*
     * The frame loop exits when it sees the QUIT event; join it so the VM is
     * completely torn down before the surface goes away.  Without the join the
     * rasterizer can be mid-frame when Android destroys the window.
     */
    if (a->started && !a->joined) {
        /*
         * Only when nativeRun() did not already do it.  A pthread_t may be
         * joined exactly once: bionic answers the second attempt with
         * "invalid pthread_t ... passed to pthread_join" and abort()s, which
         * is how a game that ended cleanly took the whole app down with it.
         */
        pthread_join(a->thread, NULL);
        a->joined = 1;
    }
    /* The Java reference belongs to this layer, so it is released here rather
     * than in the port's shutdown.  Releasing it is what makes "back out of the
     * game, start another" not leak an Activity class per game. */
    if (s && s->activity_class) {
        (*env)->DeleteGlobalRef(env, s->activity_class);
        s->activity_class = NULL;
        s->vibrate_mid = NULL;
    }
    if (s && s->glyph_class) {
        (*env)->DeleteGlobalRef(env, s->glyph_class);
        s->glyph_class = NULL;
        s->glyph_mid = NULL;
    }
    if (s && s->midi_class) {
        /* Every MediaPlayer this run opened was already closed by the runtime
         * (Player.deallocate), so there is nothing to stop here - only the
         * class reference to let go of. */
        (*env)->DeleteGlobalRef(env, s->midi_class);
        s->midi_class = NULL;
        s->midi_open_mid = NULL;
        s->midi_length_mid = NULL;
        s->midi_start_mid = NULL;
        s->midi_stop_mid = NULL;
        s->midi_close_mid = NULL;
        s->midi_seek_mid = NULL;
        s->midi_time_mid = NULL;
        s->midi_volume_mid = NULL;
        s->midi_done_mid = NULL;
    }
    if (s && s->glyph_cache) {
        /* Only after the run thread is joined: it is the thread that fills it. */
        free(s->glyph_cache);
        s->glyph_cache = NULL;
    }
    if (s && s->image_class) {
        (*env)->DeleteGlobalRef(env, s->image_class);
        s->image_class = NULL;
        s->image_info_mid = NULL;
        s->image_decode_mid = NULL;
    }
    if (s && s->audio_class) {
        /* PlatformAudio keeps no decoder alive between calls: every decode
         * releases its codec and extractor before it returns. */
        (*env)->DeleteGlobalRef(env, s->audio_class);
        s->audio_class = NULL;
        s->audio_info_mid = NULL;
        s->audio_decode_mid = NULL;
    }
    if (s && s->http_class) {
        /* Every request closes its Java-side entry before the hook returns, so
         * there is nothing to drain here - just the class reference. */
        (*env)->DeleteGlobalRef(env, s->http_class);
        s->http_class = NULL;
        s->http_request_mid = NULL;
        s->http_status_mid = NULL;
        s->http_headers_mid = NULL;
        s->http_body_mid = NULL;
        s->http_close_mid = NULL;
    }
    if (s && s->sensor_class) {
        /* Handles are closed by nj_sensor_shutdown() when the MIDlet ends; a
         * sensor the MIDlet left open would keep the Java listener awake, so
         * the runtime - not this teardown - is responsible for closing them. */
        (*env)->DeleteGlobalRef(env, s->sensor_class);
        s->sensor_class = NULL;
        s->sensor_list_mid = NULL;
        s->sensor_open_mid = NULL;
        s->sensor_read_mid = NULL;
        s->sensor_close_mid = NULL;
    }
    if (s && s->socket_class) {
        /* A socket the MIDlet left open is closed by nj_socket_close() on the
         * object it holds; nothing here keeps a Java-side entry alive. */
        (*env)->DeleteGlobalRef(env, s->socket_class);
        s->socket_class = NULL;
        s->socket_connect_mid = NULL;
        s->socket_address_mid = NULL;
        s->socket_port_mid = NULL;
        s->socket_local_mid = NULL;
        s->socket_local_port_mid = NULL;
        s->socket_read_mid = NULL;
        s->socket_write_mid = NULL;
        s->socket_close_mid = NULL;
    }
    if (s && s->bt_class) {
        /* Whatever discovery was running stops with the VM: an inquiry left
         * going keeps the radio busy and drains the battery. */
        if (env && s->bt_discovering && s->bt_inquiry_stop_mid) {
            (*env)->CallStaticVoidMethod(env, s->bt_class, s->bt_inquiry_stop_mid);
            if ((*env)->ExceptionCheck(env)) {
                (*env)->ExceptionClear(env);
            }
        }
        (*env)->DeleteGlobalRef(env, s->bt_class);
        s->bt_class = NULL;
        s->bt_available_mid = NULL;
        s->bt_address_mid = NULL;
        s->bt_name_mid = NULL;
        s->bt_class_mid = NULL;
        s->bt_discoverable_mid = NULL;
        s->bt_inquiry_start_mid = NULL;
        s->bt_inquiry_devices_mid = NULL;
        s->bt_inquiry_stop_mid = NULL;
        s->bt_services_mid = NULL;
        s->bt_listen_mid = NULL;
        s->bt_accept_mid = NULL;
        s->bt_connect_mid = NULL;
        s->bt_read_mid = NULL;
        s->bt_write_mid = NULL;
        s->bt_close_mid = NULL;
        s->bt_discovering = 0;
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
/*
 * nj_port_android(): the default 240x320 MIDlet screen.
 * nj_port_android_ex(): pick the logical screen - a title that gates on the
 * screen size needs one it will accept, and the Surface is scaled to fit.
 */
nj_status nj_port_android_ex(nj_port *out, int screen_w, int screen_h)
{
    (void)out; (void)screen_w; (void)screen_h;
    return NJ_ERR_UNSUPPORTED;
}

void nj_port_android_set_window(nj_port *port, void *native_window)
{
    (void)port;
    (void)native_window;
}

void nj_port_android_set_paths(nj_port *port, const char *rms_dir,
                               const char *fs_dir)
{
    (void)port;
    (void)rms_dir;
    (void)fs_dir;
}

void nj_port_android_set_active(nj_port *port, int active)
{
    (void)port;
    (void)active;
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

void nj_port_android_touch(nj_port *port, int pointer_id, int phase,
                           float x, float y)
{
    (void)port;
    (void)pointer_id;
    (void)phase;
    (void)x;
    (void)y;
}

int nj_port_android_stats(const nj_port *port, char *buf, nj_size cap)
{
    (void)port;
    (void)buf;
    (void)cap;
    return NJ_ERR_UNSUPPORTED;
}

void nj_port_android_set_view(nj_port *port, int mode, float zoom, float dx,
                              float dy)
{
    (void)port;
    (void)mode;
    (void)zoom;
    (void)dx;
    (void)dy;
}

int nj_port_android_view_limits(nj_port *port, int *max_x, int *max_y)
{
    (void)port;
    (void)max_x;
    (void)max_y;
    return 0;
}

int nj_port_android_detect(const nj_port *port, int out[5])
{
    (void)port;
    (void)out;
    return 0;
}

#endif /* __ANDROID__ */
