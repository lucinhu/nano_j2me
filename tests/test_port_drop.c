/*
 * nano_j2me - tests/test_port_drop.c
 *
 * Dropping a .jar on the game window asks for that game instead of the one
 * being played; F10 ends the run.  Both are window features the port owns -
 * SDL_DROPFILE and the F10 key never reach the VM otherwise - and the contract
 * is deliberately narrow:
 *
 *   - a drop the port believes can start queues the path and ends the run the
 *     same way the close button does (NJ_EV_QUIT), so the caller that owns the
 *     run can start the new jar once nj_run() returns;
 *   - a drop that cannot start queues nothing and must leave the running game
 *     alone - a slip of the mouse is not a reason to kill a game;
 *   - F10 is the keyboard's close button and is not a MIDP key.
 *
 * These checks drive the real SDL2 port under the dummy video/audio drivers, so
 * no desktop and no VM are involved: display_present() is called directly, the
 * way the frame loop calls it.  The dummy window never has keyboard focus, and
 * a drop is still taken - the correct behaviour, because a drop IS the
 * interaction and SDL delivers it to the window it landed on.  The port is also
 * built and shut down twice, because creating the second one is what exercises
 * a fresh SDL_Init after the first port's SDL_Quit.
 *
 * Usage: test_port_drop    (from the repository root; needs the fixtures)
 */
#define _POSIX_C_SOURCE 200809L

#include "nj2me/nj.h"          /* nj_port_sdl2 + nj_port_shutdown */

#include <SDL.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
static int failures;

#define CHECK(cond, what)                                                    \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, (what));        \
        }                                                                    \
    } while (0)

/* The fixture the drop tests use: a tiny but real MIDlet suite. */
static const char *const k_jar = "tests/fixtures/alphademo.jar";

/*
 * How a pushed drop event has to be built for this test to work everywhere.
 *
 * On real SDL2, SDL_PushEvent hands the event loop exactly the SDL_DROPFILE an
 * OS drag would, T file pointer included.  The test also has to run against
 * sdl2-compat (the SDL2-on-SDL3 shim that some distributions ship as "SDL2"),
 * whose SDL_PushEvent copies the SDL2 event body into SDL3's larger event
 * union without translating the drop fields: SDL3 reads drop.data 24 bytes
 * into that copied body, which is byte 32 of the SDL2 event - inside
 * edit.text.  Left zeroed, that is a NULL file name, and the shim's own
 * conversion crashes on it (SDL3_strdup(NULL) inside SDL_PollEvent, before
 * this port ever sees the event).  Copying the same path pointer there too
 * makes the shim deliver the drop it was handed; on real SDL2 those bytes are
 * the union's text[] area and nothing ever reads them.  Real drops come from
 * SDL3 with drop.data already set and are unaffected either way.
 */
typedef char sdl2_drop_data_offset_check[
    (offsetof(SDL_Event, edit.text) + 20) == 32 ? 1 : -1];

static int jar_exists(void)
{
    FILE *f = fopen(k_jar, "rb");

    if (!f) {
        return 0;
    }
    fclose(f);
    return 1;
}

/* Hand the event loop a drop exactly as SDL would deliver one: the path is
 * SDL-owned memory and the port frees it with SDL_free, so the test must not
 * free it and must not stack-allocate it.  Returns the allocation so the
 * sdl2-compat case below can release what that shim duplicates. */
static char *push_drop(const char *path)
{
    SDL_Event ev;
    char *file = SDL_strdup(path);

    CHECK(file != NULL, "the dropped path can be handed to the event loop");
    if (!file) {
        return NULL;
    }
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_DROPFILE;
    ev.drop.file = file;
    /* See sdl2_drop_data_offset_check: only sdl2-compat reads this. */
    memcpy(&ev.edit.text[20], &file, sizeof(file));
    CHECK(SDL_PushEvent(&ev) == 1, "SDL_PushEvent delivers a file drop");
    return file;
}

/*
 * Real SDL2 hands the event loop the pointer it was given, and the port frees
 * that pointer - the test must not.  sdl2-compat instead copies the drop inside
 * SDL_PushEvent, so the port frees the shim's copy and the allocation above is
 * still ours after the event has been consumed: free it, or a sanitizer run
 * reports it (correctly) as a leak.  Called after display_present() has drained
 * the queue, before the port is shut down.
 */
static void drop_release(char *file)
{
    if (file && SDL_GetHint("SDL2_COMPAT") != NULL) {
        SDL_free(file);
    }
}

static void push_keydown(SDL_Keycode sym, SDL_Scancode scan)
{
    SDL_Event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_KEYDOWN;
    ev.key.repeat = 0;
    ev.key.keysym.sym = sym;
    ev.key.keysym.scancode = scan;
    CHECK(SDL_PushEvent(&ev) == 1, "SDL_PushEvent delivers a keypress");
}

/* Drain the event queue, reporting whether an end-of-run was in it. */
static int drain_quit(nj_port *port)
{
    nj_event ev;
    int quit = 0;
    int i;

    for (i = 0; i < 256; i++) {
        if (!port->input_poll(port->ud, &ev)) {
            break;
        }
        if (ev.type == NJ_EV_QUIT) {
            quit = 1;
        }
    }
    return quit;
}

/*
 * Create the port and get its window up.  Returns 0 when the dummy driver
 * cannot provide a window - the caller then reports SKIP, not a failure.
 */
static int open_port(nj_port *port, int *w, int *h, int *stride)
{
    void *pixels = NULL;

    if (nj_port_sdl2(port, "port drop test") != NJ_OK) {
        return 0;
    }
    if (!port->display_init || !port->display_present || !port->input_poll ||
        !port->shutdown) {
        port->shutdown(port->ud);
        return 0;
    }
    /* The return is the pixel format, and NJ_PIXFMT_RGB565 is 0 - only a
     * negative value is the failure. */
    if (port->display_init(port->ud, w, h, stride, &pixels) < 0 ||
        pixels == NULL || *w <= 0 || *h <= 0) {
        port->shutdown(port->ud);
        return 0;
    }
    port->display_present(port->ud, 0, 0, *w, *h);   /* the window is up */
    return 1;
}

/*
 * A dropped text file is a mistake, not an instruction.  The game must keep
 * running: nothing queued, no NJ_EV_QUIT anywhere in the queue.
 */
static void test_refused_drop(nj_port *port, int w, int h)
{
    char txt[128];
    char out[NJ_LIB_PATH_MAX];
    FILE *f;

    SDL_snprintf(txt, sizeof(txt), "/tmp/nj2me_port_drop_%lu.txt",
                 (unsigned long)SDL_GetTicks());
    f = fopen(txt, "wb");
    CHECK(f != NULL, "the not-a-game drop can be created");
    if (f) {
        fputs("this is not a MIDlet\n", f);
        fclose(f);
    }
    {
        char *dropped = push_drop(txt);

        SDL_PumpEvents();
        port->display_present(port->ud, 0, 0, w, h);
        drop_release(dropped);
    }
    CHECK(nj_port_sdl2_take_dropped(port, out, (nj_u4)sizeof(out)) == 0,
          "a .txt drop is ignored instead of queued");
    CHECK(drain_quit(port) == 0,
          "an ignored drop does not end the running game");
    remove(txt);
}

/*
 * A real .jar: queued, and the run ends with the same NJ_EV_QUIT the close
 * button sends.  The caller takes the path after nj_run() returns.
 */
static void test_accepted_drop(nj_port *port, int w, int h)
{
    char out[NJ_LIB_PATH_MAX];
    char tiny[4];

    {
        char *dropped = push_drop(k_jar);

        SDL_PumpEvents();
        port->display_present(port->ud, 0, 0, w, h);
        drop_release(dropped);
    }
    CHECK(nj_port_sdl2_take_dropped(port, tiny, (nj_u4)sizeof(tiny)) == 0,
          "a buffer too small refuses the path instead of truncating it");
    CHECK(nj_port_sdl2_take_dropped(port, out, (nj_u4)sizeof(out)) == 1,
          "a runnable .jar drop is handed back once the run ends");
    CHECK(strcmp(out, k_jar) == 0, "and the path is the one that was dropped");
    CHECK(nj_port_sdl2_take_dropped(port, out, (nj_u4)sizeof(out)) == 0,
          "taking it clears it - there is only one drop to take");
    CHECK(drain_quit(port) == 1, "the drop ended the run with NJ_EV_QUIT");
}

/*
 * F10 is the keyboard's close button.  Drain first so the QUIT found below is
 * F10's and not a leftover from a drop.
 */
static void test_f10(nj_port *port, int w, int h)
{
    nj_event ev;
    int saw_quit = 0;
    int saw_key = 0;
    int i;

    (void)drain_quit(port);
    push_keydown(SDLK_F10, SDL_SCANCODE_F10);
    SDL_PumpEvents();
    port->display_present(port->ud, 0, 0, w, h);
    for (i = 0; i < 16; i++) {
        if (!port->input_poll(port->ud, &ev)) {
            break;
        }
        if (ev.type == NJ_EV_QUIT) {
            saw_quit = 1;
            break;
        }
        if (ev.type == NJ_EV_KEY_DOWN || ev.type == NJ_EV_KEY_UP) {
            saw_key = 1;
        }
    }
    CHECK(saw_quit == 1,
          "F10 ends the run with NJ_EV_QUIT, like the close button");
    CHECK(saw_key == 0, "F10 is not delivered to the MIDlet as a key");
}

int main(void)
{
    nj_port port;
    int w = 0, h = 0, stride = 0;
    int w2 = 0, h2 = 0, stride2 = 0;

    /* Before SDL_Init: the port must run the whole test without a desktop. */
    setenv("SDL_VIDEODRIVER", "dummy", 0);
    setenv("SDL_AUDIODRIVER", "dummy", 0);

    printf("== nj_port_sdl2: a dropped .jar switches runs, F10 ends one ==\n");

    if (!open_port(&port, &w, &h, &stride)) {
        printf("  SKIP no SDL2 window under the dummy driver\n");
        printf("  0 checks, 0 failures\n");
        return 0;
    }

    test_refused_drop(&port, w, h);
    if (jar_exists()) {
        test_accepted_drop(&port, w, h);
    } else {
        printf("  SKIP %s is missing; the accepted-drop checks are skipped\n",
               k_jar);
    }
    test_f10(&port, w, h);

    /* Close the window for real, then build a new port in the same process: a
     * second SDL_Init after a shutdown is where stale SDL state would show, and
     * the refused drop below is checked with no previous run left behind. */
    {
        SDL_Event q;

        memset(&q, 0, sizeof(q));
        q.type = SDL_QUIT;
        SDL_PushEvent(&q);
        port.display_present(port.ud, 0, 0, w, h);
    }
    nj_port_shutdown(&port);

    if (!open_port(&port, &w2, &h2, &stride2)) {
        CHECK(0, "a second port can be built after the first was shut down");
    } else {
        test_refused_drop(&port, w2, h2);
        nj_port_shutdown(&port);
    }

    printf("  %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
