/*
 * nano_j2me - nj_web.h
 *
 * ============================================================================
 *  THE WEB (EMSCRIPTEN / WASM) EMBEDDING INTERFACE
 * ============================================================================
 *
 * nano_j2me is normally driven by nj_run(), which owns the loop and blocks
 * until the MIDlet exits.  A browser cannot afford that: the page's main
 * thread must return to the event loop after a few milliseconds, and no
 * JavaScript may run while the runtime is executing.  This header therefore
 * exposes the same engine as a *pumped* runner:
 *
 *     njw_start()          create the VM, open the JAR, construct the MIDlet
 *     njw_frame(dt_ms)     advance the emulator by one host frame (call from
 *                          requestAnimationFrame, never in a tight loop)
 *     njw_stop()           tear the VM down again
 *
 * Everything else is optional: input, audio, save states, and the app
 * metadata needed to build a game library.  The runner is single threaded:
 * no Asyncify, no pthreads, no SharedArrayBuffer.  Every entry point must be
 * called from the thread that created the module (the browser's main thread),
 * and none of them blocks.
 *
 * The header is C, but designed to be read from JavaScript: every exported
 * function takes and returns plain integers (pointers are 32-bit wasm
 * offsets, `double` is passed as f64).  Strings are NUL terminated UTF-8.
 *
 * ============================================================================
 *  A MINIMAL HOST LOOP
 * ============================================================================
 *
 *     njw_options_reset();
 *     njw_option_int(NJW_OPT_HEAP_BYTES, 16 * 1024 * 1024);
 *     njw_option_string(NJW_OPT_RMS_DIR, "/nano_j2me/saves/" + gameId);
 *     if (njw_start("/nano_j2me/jars/" + gameId + ".jar") != 0) { fail(njw_error()); }
 *
 *     function pump(now) {
 *         const dt = Math.min(now - last, 250);
 *         last = now;
 *         const flags = njw_frame(dt);
 *         if (flags < 0) { fail(njw_error()); return; }
 *         if (flags & NJW_FRAME_DREW) draw(njw_framebuffer(), njw_width(), njw_height());
 *         pumpAudio();                       // see njw_audio_read()
 *         if (flags & NJW_FRAME_RUNNING) requestAnimationFrame(pump);
 *         else gameOver(njw_stop_reason());
 *     }
 *
 * The framebuffer is RGBA8888.  Copy it (or putImageData) before the next
 * njw_frame() call; the pointer stays valid for the life of the VM, the
 * contents are overwritten in place.
 *
 * ============================================================================
 *  THE CLOCK (WHY dt IS VIRTUAL)
 * ============================================================================
 *
 * The runtime never calls the host wall clock directly; it reads
 * nj_port.millis, which this port derives from a virtual clock.  The host
 * owns that clock:
 *
 *   - Each njw_frame(dt_ms) advances it by dt_ms (clamped to 0..250).  During
 *     normal play dt_ms is simply the real time since the previous animation
 *     frame, so Java's System.currentTimeMillis()/Thread.sleep() behave
 *     normally.
 *   - While the MIDlet constructor runs (inside njw_start) the clock runs in
 *     a fast-forward "load" mode: a constructor that sleeps 5 seconds returns
 *     immediately instead of freezing the page.
 *   - Thread.sleep() during a frame may advance the clock a little further.
 *     This is what lets a blocking native call (paint, key handling) finish
 *     without deadlocking the browser; the amount is small and bounded, so
 *     game timing is preserved to within a few percent.
 *
 * A host that wants deterministic replay should pass a fixed dt (for example
 * 1000.0 / 60.0) instead of measured time.
 */
#ifndef NJ_WEB_H
#define NJ_WEB_H

#include "nj_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Version / build identity                                            */
/* ------------------------------------------------------------------ */

#define NJW_API_VERSION 1

/* Bits returned by njw_frame().  A non-negative result is a mask of these;
 * any negative value is a fatal engine error (see njw_error()). */
#define NJW_FRAME_RUNNING 0x1  /* the VM is alive; keep pumping                */
#define NJW_FRAME_DREW    0x2  /* display_present() ran; the pixels changed    */
#define NJW_FRAME_LOADING 0x4  /* startApp() has not produced a Displayable yet */

/* Engine version ("0.5.7"); static storage, never NULL. */
const char *njw_runtime_version(void);

/* ABI version of this header (NJW_API_VERSION); the shell refuses to run
 * against a module built from a different header. */
int njw_api_version(void);

/* ------------------------------------------------------------------ */
/* Options (set before njw_start; njw_options_reset restores defaults) */
/* ------------------------------------------------------------------ */

enum njw_option {
    /* int: framebuffer width in device pixels.  Default 240.
     * The runtime rasterizes at this size; the canvas is scaled with CSS.
     * Changing it between games is fine, changing it mid-game is not. */
    NJW_OPT_SCREEN_WIDTH = 1,
    /* int: framebuffer height in device pixels.  Default 320. */
    NJW_OPT_SCREEN_HEIGHT,

    /* int: Java heap in bytes.  Default NJ_MAX_HEAP (16 MB).  The runtime
     * may fail to start a large title with the default; the shell can retry
     * with 32 MB.  0 restores the default. */
    NJW_OPT_HEAP_BYTES,

    /* int: emulated handset speed in percent (10..800, default 100).
     * Same knob as nj_vm_set_speed(): values above 100 run the game faster
     * than real time on a slow host, or for fast-forward. */
    NJW_OPT_SPEED_PERCENT,

    /* string: Emscripten FS path for this game's RecordStore files.
     * Default "/nano_j2me/saves/<jar stem>".  The directory must exist. */
    NJW_OPT_RMS_DIR,
    /* string: Emscripten FS path exposed as file:///root/ (JSR 75).
     * Default "/nano_j2me/files/<jar stem>".  Must exist. */
    NJW_OPT_FS_DIR,

    /* string: device profile name handed to the runtime ("nokia-6230",
     * "generic", ...).  NULL lets nj_run-equivalent defaults apply. */
    NJW_OPT_DEVICE,
    /* string: override the MIDlet class from the JAR manifest. */
    NJW_OPT_MIDLET_CLASS,
    /* string: Emscripten FS path of a save state to load right after the
     * MIDlet is constructed (the VM resumes instead of calling startApp). */
    NJW_OPT_LOAD_STATE_PATH
};

/* Restore every option above to its default. */
void njw_options_reset(void);

/* Set an integer option.  Returns 0 on success, -1 on an unknown option. */
int njw_option_int(int option, int value);

/* Set a string option (copied into runner storage).  `utf8` may be NULL to
 * clear it.  Returns 0 on success, -1 on an unknown option. */
int njw_option_string(int option, const char *utf8);

/* ------------------------------------------------------------------ */
/* Life cycle                                                          */
/* ------------------------------------------------------------------ */

/*
 * Create the VM, read the JAR from the Emscripten filesystem, open it,
 * construct the MIDlet and queue MIDlet.startApp().  Returns 0 on success.
 *
 * On failure returns -1 and njw_error() describes why (bad path, not a JAR,
 * constructor threw, out of memory, ...).  The caller is expected to write
 * the JAR into the FS first (FS.writeFile), create the RMS/FS directories,
 * and to remove/rewrite those files after njw_stop().
 *
 * Cost: this runs the MIDlet constructor synchronously, so a title that does
 * real work in its constructor delays the page by a few dozen milliseconds
 * (sleeps are fast-forwarded).  Call it from a click handler, not on load.
 */
int njw_start(const char *jar_path);

/*
 * Advance the emulator by one host frame.  `dt_ms` is the wall time since the
 * previous call (clamped internally to 0..250).  dt_ms <= 0 means "use one
 * nominal 60 Hz tick".
 *
 * Returns a non-negative bit mask:
 *   NJW_FRAME_RUNNING   the VM is alive; keep pumping
 *   NJW_FRAME_DREW      display_present() ran during this call; repaint
 *   NJW_FRAME_LOADING   startApp() has not produced a Displayable yet
 * or a negative value on an engine error (details in njw_error()).
 *
 * Call at most once per animation frame, and never from inside a callout of
 * another njw_* function.
 */
int njw_frame(double dt_ms);

/* TRUE while a VM is alive (the MIDlet may still be starting). */
int njw_running(void);

/* TRUE once the MIDlet produced its first Displayable. */
int njw_displayable_ready(void);

/*
 * Tear the running VM down: destroyApp() with a bounded budget, close the
 * app, release all runner memory and reset the option defaults.  Safe to
 * call at any time, including twice, and after a fatal error.  The FS is
 * left untouched (the host owns the JAR/RMS files).
 */
void njw_stop(void);

/* Empty string when no error; otherwise a static UTF-8 description. */
const char *njw_error(void);

/* Why the MIDlet stopped ("the MIDlet exited normally", ...); "" if it is
 * still running or was never started. */
const char *njw_stop_reason(void);

/* Frames pumped since njw_start(). */
unsigned njw_frames(void);

/* ------------------------------------------------------------------ */
/* Framebuffer                                                         */
/* ------------------------------------------------------------------ */

/* RGBA8888, njw_width()*njw_height()*4 bytes, rows top to bottom.
 * NULL before the first frame has been presented. */
const unsigned char *njw_framebuffer(void);

int njw_width(void);
int njw_height(void);

/* Incremented by display_present().  When it did not change since the last
 * paint, the host may skip drawing (the pixels are still the previous ones). */
unsigned njw_frame_epoch(void);

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

/* Key codes are nj_key values from nj_port.h.  The numeric values are part
 * of the ABI and match the order of the enum:
 *
 *   0 NONE
 *   1..10  '0'..'9'
 *   11 STAR,      12 POUND
 *   13 UP, 14 DOWN, 15 LEFT, 16 RIGHT, 17 FIRE
 *   18 SOFT_LEFT, 19 SOFT_RIGHT, 20 CLEAR
 *   21 GAME_A, 22 GAME_B, 23 GAME_C, 24 GAME_D
 *   25 VOLUP, 26 VOLDOWN, 27 POWER
 *
 * `down` != 0 is a press, 0 a release.  The runtime handles the mapping to
 * MIDP key codes, including auto-repeat (call it once per DOM keydown, and
 * do not send repeats: the runtime's key handling already coalesces).
 */
void njw_key(int key, int down);

/* Pointer / touch.  Coordinates are framebuffer (device) pixels, origin top
 * left: canvas-pixel = (clientX - rect.left) * njw_width() / rect.width.
 * action: 0 = down, 1 = up, 2 = move. */
#define NJW_POINTER_DOWN 0
#define NJW_POINTER_UP   1
#define NJW_POINTER_MOVE 2
void njw_pointer(int action, int x, int y);

/* Committed UTF-8 text (max NJ_TEXT_INPUT_BYTES-1 bytes per event; the shell
 * must split longer strings on UTF-8 code point boundaries).  Only used by
 * MIDlets that read keystrokes directly; native editors use the calls
 * below. */
void njw_text_input(const char *utf8);

/* Editing command while a native editor is active (nj_text_edit values):
 * 1 BACKSPACE, 2 DELETE, 3 LEFT, 4 RIGHT, 5 HOME, 6 END. */
void njw_text_edit(int edit);

/*
 * Whole editor contents from a DOM input/textarea, plus the caret position
 * in UTF-16 code units (i.e. textarea.selectionStart).  This is the simple
 * way to support IMEs, paste and backspace: on every input event send the
 * complete value.  `cursor` may be -1 to keep the current caret.
 */
void njw_text_replace(const char *utf8, int cursor);

/* TRUE while a MIDlet text widget wants a native editor.  The shell should
 * focus a hidden <textarea> when this becomes true and blur it when false. */
int njw_text_active(void);

/*
 * Copy the editor contents as UTF-8 into `out` (NUL terminated).  Returns the
 * number of bytes needed; when that is >= cap the output was truncated.
 * Pass out=NULL/cap=0 to query the size.
 */
int njw_text_current(char *out, int cap);

/*
 * Copy the full nj_text_input_state a port normally receives.  Any pointer may
 * be NULL.  `length`/`cursor` are UTF-16 code units.
 */
void njw_text_state(int *active, int *multiline, unsigned *constraints,
                    int *max_length, int *length, int *cursor);

/* ------------------------------------------------------------------ */
/* Audio: the runtime pushes interleaved S16 frames; the host pulls      */
/* ------------------------------------------------------------------ */

/* Frames available right now; 0 when silent. */
int njw_audio_available(void);

/* Sample rate/channels of what is queued (defaults 22050 / 1).  Changes when
 * the MIDlet opens a different PCM/MIDI stream. */
int njw_audio_rate(void);
int njw_audio_channels(void);

/*
 * Pull up to `max_frames` interleaved S16 frames into `dst` (allocate
 * max_frames*channels*2 bytes).  Returns the number of frames written.
 * The host should drain this every animation frame and schedule the data on
 * an AudioContext; the buffer keeps a few hundred milliseconds.
 */
int njw_audio_read(short *dst, int max_frames);

/*
 * Tell the runtime how many pulled frames are scheduled in the audio context
 * but not audible yet, so getMediaTime()/Player time keep tracking the sound
 * the user hears.  Call after each AudioBufferSourceNode is started (and
 * again as it ends); monotonic bookkeeping is the host's job.
 */
void njw_audio_note_playing(int frames);

/* ------------------------------------------------------------------ */
/* App metadata / library probing                                      */
/* ------------------------------------------------------------------ */

/*
 * Open `jar_path` in a throwaway VM (small heap, no MIDlet) and read the
 * manifest so the host can build its game library without starting a game.
 * Fills the njw_app_* accessors below.  Returns 0 on success, -1 on failure.
 *
 * The temp VM is destroyed before this returns; the strings live in runner
 * storage and stay valid until the next probe/start/stop call.
 */
int njw_probe(const char *jar_path);

/* MIDlet-Name from the manifest, falling back to the JAR file stem.
 * Empty string when nothing was probed yet.  UTF-8. */
const char *njw_app_title(void);
/* MIDlet-Vendor / MIDlet-Version from the manifest; "" when absent. */
const char *njw_app_vendor(void);
const char *njw_app_version(void);

/* How many MIDlet-1..N entries the JAR declares (0 when unknown). */
int njw_midlet_count(void);
/* Class name of MIDlet `index` (0 based), "" when out of range. */
const char *njw_midlet_class_at(int index);

/*
 * The MIDlet's icon (PNG or JPEG bytes as stored in the JAR), or NULL when
 * the manifest names none.  `*len` receives the byte count.  The pointer is
 * owned by the runner and remains valid until the next probe/start/stop.
 */
const unsigned char *njw_app_icon(int *len);

/* ------------------------------------------------------------------ */
/* Save states                                                         */
/* ------------------------------------------------------------------ */

/* Cheap check: the VM can serialize right now (between frames, MIDlet
 * running or paused). */
int njw_can_save(void);

/*
 * Serialize the whole VM into a newly malloc'd blob.  `*blob` and `*len`
 * receive it; release it with njw_free().  Returns 0 on success, -1 when no
 * VM is running or the VM cannot save.  The blob is self-describing (it
 * records the JAR identity and endianness), so loading it into a different
 * game fails cleanly instead of corrupting the VM.
 */
int njw_save_state(void **blob, int *len);

/*
 * Restore a state produced by njw_save_state().  Call only between
 * njw_frame() calls (never inside one).  Returns 0 on success.  On failure
 * the VM is left unusable: the host must njw_stop() and njw_start() again.
 */
int njw_load_state(const void *blob, int len);

/* Allocate `bytes` from the wasm heap (16-byte aligned, for audio read
 * buffers, save-state blobs passed back to njw_load_state, ...).  Returns
 * NULL on failure.  Release with njw_free(). */
void *njw_alloc(int bytes);

/* Free a pointer returned through this header (save-state blobs, njw_alloc
 * buffers).  NULL is valid. */
void njw_free(void *ptr);

/* ------------------------------------------------------------------ */
/* Tuning                                                              */
/* ------------------------------------------------------------------ */

/* Change the emulated speed while a game is running (10..800 percent).
 * Ignored when no VM is alive.  Same units as NJW_OPT_SPEED_PERCENT. */
void njw_set_speed(int percent);

/* Current effective speed in percent. */
int njw_speed(void);

#ifdef __cplusplus
}
#endif

#endif /* NJ_WEB_H */
