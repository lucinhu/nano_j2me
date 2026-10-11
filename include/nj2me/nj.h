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
#include "nj_app.h"

#include "nj_gfx.h"
#include "nj_codec.h"
#include "nj_png.h"
#include "nj_inflate.h"
#include "nj_zip.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Version of the runtime, reported by --version and in logs. */
#define NJ_VERSION_MAJOR 0
#define NJ_VERSION_MINOR 1
#define NJ_VERSION_PATCH 0
#define NJ_VERSION_STRING "0.1.0"

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

/* Push a synthetic input event into a port that supports it (headless). */
void nj_port_push_event(nj_port *port, const nj_event *ev);

#ifdef __cplusplus
}
#endif

#endif /* NJ_H */
