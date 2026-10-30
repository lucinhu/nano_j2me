/*
 * nano_j2me - nj.h
 *
 * Umbrella header.  Include this and nothing else.
 *
 *   #include "nj2me/nj.h"
 *
 *   nj_port port;
 *   nj_port_sdl2(&port, "My MIDlet");
 *   nj_run_opts opts = {0};
 *   opts.jar_path = "game.jar";
 *   return nj_run(&port, &opts);
 */
#ifndef NJ_H
#define NJ_H

#include "nj_config.h"
#include "nj_types.h"
#include "nj_port.h"
#include "nj_vm.h"
#include "nj_jit.h"
#include "nj_app.h"
#include "nj_device.h"

#include "nj_gfx.h"
#include "nj_codec.h"
#include "nj_png.h"
#include "nj_inflate.h"
#include "nj_zip.h"

/*
 * Desktop client: the games library behind the SDL2 window, the settings file
 * it remembers, and the library UI model.  Hosted-only; a board includes nj.h
 * just as well and ignores these.
 */
#include "nj_library.h"
#include "nj_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Version of the runtime, reported by --version and in logs. */
#define NJ_VERSION_MAJOR 0
#define NJ_VERSION_MINOR 5
#define NJ_VERSION_PATCH 2
#define NJ_VERSION_STRING "0.5.2"

/*
 * Run an embedded JAR with a caller-provided port, using a MIDlet class name
 * from `opts` (or the manifest's MIDlet-1 when it is NULL).
 */
int nj_run(const nj_port *port, const nj_run_opts *opts);

/* Built-in ports (see nj_port.h for the full contract). */
nj_status nj_port_headless(nj_port *out);
nj_status nj_port_sdl2(nj_port *out, const char *title);

/* Release a port created by the functions above.  Safe to call twice. */
void nj_port_shutdown(nj_port *port);

/*
 * MIDP application loop.  nj_run() drives it; an embedder with its own event
 * loop (a game engine, an Android Activity, a FreeRTOS task) can call
 * nj_midp_run itself, or pump it one frame at a time with nj_midp_frame().
 */
nj_status nj_midp_run(nj_vm *vm);
/* The Displayable currently on screen, or NULL when the MIDlet never set
 * one.  A runner uses this to distinguish "the app is running" from "the app
 * has no UI and the frame loop would spin forever". */
nj_ref    nj_midp_current(nj_vm *vm);
nj_status nj_midp_frame(nj_vm *vm);
nj_status nj_midp_destroy_app(nj_vm *vm, int unconditional);

/* Convenience: start the headless port with an optional frame dump.
 * Pass dump_prefix == NULL to keep frames in memory only. */
nj_status nj_port_headless_ex(nj_port *out, int width, int height,
                              int dump_every, const char *dump_prefix);

/* Opt in to host HTTP/HTTPS/TCP in the headless port. Default is offline.
 * Returns NJ_ERR_UNSUPPORTED when the desktop network backend is disabled. */
nj_status nj_port_headless_enable_network(nj_port *port);

/* Change the frame dump of a headless port that is already running.  A
 * prefix that names an existing directory dumps inside it. */
void nj_port_headless_set_dump(void *ud, const char *prefix, int every);

/* Push a synthetic input event into a port that supports it (headless). */
void nj_port_push_event(nj_port *port, const nj_event *ev);

/*
 * What the headless port did with SOUND.
 *
 * It records audio rather than playing it, so a test can assert that a MIDlet
 * asked for the right notes in the right order, with the right durations,
 * without a sound card anywhere in the picture.  A port that played them and
 * said nothing would make every audio path untestable, which is the whole
 * reason these exist.  All of them return -1 for a NULL or foreign port.
 */
int  nj_port_headless_tone_calls(const nj_port *port);  /* tone_play calls */
int  nj_port_headless_tone_count(const nj_port *port);  /* tones offered, total */
int  nj_port_headless_tone_stops(const nj_port *port);
int  nj_port_headless_tone_first(const nj_port *port);  /* first freq, last seq */
int  nj_port_headless_tone_last(const nj_port *port, nj_audio_tone *out, int cap);
long nj_port_headless_pcm_frames(const nj_port *port);  /* frames accepted */
int  nj_port_headless_pcm_opens(const nj_port *port);
int  nj_port_headless_pcm_format(const nj_port *port, int *rate, int *channels);
int  nj_port_headless_pcm_closes(const nj_port *port);
/*
 * File activity, so a test can tell "saved the score" from "silently did
 * nothing".  RecordStore and FileConnection are otherwise indistinguishable
 * from a MIDlet that never touched them: both are the same sequence of frames.
 * -1 means the port is not a headless one, which is different from 0, which
 * means it is one and the MIDlet really did no file IO.
 */
int  nj_port_headless_file_opens(const nj_port *port);
int  nj_port_headless_file_closes(const nj_port *port);
int  nj_port_headless_file_reads(const nj_port *port);
int  nj_port_headless_file_writes(const nj_port *port);
long nj_port_headless_bytes_read(const nj_port *port);
long nj_port_headless_bytes_written(const nj_port *port);
const char *nj_port_headless_last_path(const nj_port *port);   /* NULL if none */
const char *nj_port_headless_last_mode(const nj_port *port);

#ifdef __cplusplus
}
#endif

#endif /* NJ_H */
