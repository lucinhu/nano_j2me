/*
 * nano_j2me - tests/test_port_binds.c
 *
 * Remappable game keys on the SDL2 port.
 *
 * The contract in nj_port.h is small but it has edges a settings page can cut
 * itself on, so this test drives them all through the public API:
 *
 *   - the defaults are exactly the mapping the port has always had (arrows,
 *     Return/KP_Enter/Space for fire, F1/Esc/F2 for the soft keys, Backspace
 *     for clear, Z/X/C/V for A-D), and the number pad - 0-9, * and # - is
 *     fixed: it is the phone's keypad, not a player preference;
 *   - binding a slot takes the key out of the game path for its defaults: a
 *     player who rebinds "up" to W expects the arrow to stop fighting;
 *   - one key means one action: binding a key another slot uses moves it, and
 *     the slot it left has no key until it is reset with 0;
 *   - the port's own controls (F3 speed, F5 save, F10 quit, F11 fullscreen,
 *     F12/Tab keypad) can never be bound, nor can an unknown slot;
 *   - and the mapping the API reports is the mapping the event path uses:
 *     real SDL_KEYDOWN/KEYUP events are pushed through display_present() and
 *     the queued nj_events are checked, so a remap that only applied to
 *     key_for() but not to the game would fail here.
 *
 * Under the dummy video/audio drivers, so no desktop is needed and no VM is
 * involved: display_init()/display_present() are called directly, the way the
 * frame loop calls them.  The second half builds a port with every client
 * setting switched on (fullscreen, linear filter, mute, no network) and checks
 * that it initialises, goes silent without erroring, and shuts down cleanly.
 *
 * Usage: test_port_binds    (from the repository root; no fixtures needed)
 */
#define _POSIX_C_SOURCE 200809L

#include "nj2me/nj.h"          /* nj_port_sdl2 + the bind/key_for API */

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

/* Hand the event loop one key exactly as a window would deliver it.  repeat is
 * 0: SDL sets it only for autorepeat, and the port treats the first press and
 * a repeat differently. */
static void push_key(SDL_EventType type, SDL_Keycode sym, SDL_Scancode scan)
{
    SDL_Event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.key.repeat = 0;
    ev.key.keysym.sym = sym;
    ev.key.keysym.scancode = scan;
    CHECK(SDL_PushEvent(&ev) == 1, "SDL_PushEvent delivers the key event");
}

/* Pop the next KEY event from the port's queue, if there is one.  Non-key
 * events are drained on the way; the game queue only ever carries keys,
 * pointer presses and QUIT, and the tests below push nothing else. */
static int next_key(nj_port *port, nj_event *out)
{
    nj_event ev;
    int i;

    for (i = 0; i < 64; i++) {
        if (!port->input_poll(port->ud, &ev)) {
            return 0;
        }
        if (ev.type == NJ_EV_KEY_DOWN || ev.type == NJ_EV_KEY_UP ||
            ev.type == NJ_EV_KEY_REPEAT) {
            *out = ev;
            return 1;
        }
    }
    return 0;
}

static void drain(nj_port *port)
{
    nj_event ev;
    int i;

    for (i = 0; i < 256; i++) {
        if (!port->input_poll(port->ud, &ev)) {
            break;
        }
    }
}

/*
 * The defaults.  Every line here is a promise to games that already exist:
 * these are the keys the port has always understood, and none of them changed
 * when the mapping became remappable.
 */
static void test_defaults(nj_port *port)
{
    int i;

    CHECK(nj_port_sdl2_key_for(port, SDLK_UP) == NJ_KEY_UP,
          "Up is up by default");
    CHECK(nj_port_sdl2_key_for(port, SDLK_DOWN) == NJ_KEY_DOWN,
          "Down is down by default");
    CHECK(nj_port_sdl2_key_for(port, SDLK_LEFT) == NJ_KEY_LEFT,
          "Left is left by default");
    CHECK(nj_port_sdl2_key_for(port, SDLK_RIGHT) == NJ_KEY_RIGHT,
          "Right is right by default");
    CHECK(nj_port_sdl2_key_for(port, SDLK_RETURN) == NJ_KEY_FIRE,
          "Return is fire by default");
    CHECK(nj_port_sdl2_key_for(port, SDLK_KP_ENTER) == NJ_KEY_FIRE,
          "keypad Enter is fire too");
    CHECK(nj_port_sdl2_key_for(port, SDLK_SPACE) == NJ_KEY_FIRE,
          "Space is fire too");
    CHECK(nj_port_sdl2_key_for(port, SDLK_ESCAPE) == NJ_KEY_SOFT_RIGHT,
          "Escape is the soft-right key");
    CHECK(nj_port_sdl2_key_for(port, SDLK_F2) == NJ_KEY_SOFT_RIGHT,
          "so is F2");
    CHECK(nj_port_sdl2_key_for(port, SDLK_F1) == NJ_KEY_SOFT_LEFT,
          "F1 is the soft-left key");
    CHECK(nj_port_sdl2_key_for(port, SDLK_BACKSPACE) == NJ_KEY_CLEAR,
          "Backspace is clear");
    CHECK(nj_port_sdl2_key_for(port, SDLK_z) == NJ_KEY_GAME_A,
          "Z is game key A");
    CHECK(nj_port_sdl2_key_for(port, SDLK_x) == NJ_KEY_GAME_B,
          "X is game key B");
    CHECK(nj_port_sdl2_key_for(port, SDLK_c) == NJ_KEY_GAME_C,
          "C is game key C");
    CHECK(nj_port_sdl2_key_for(port, SDLK_v) == NJ_KEY_GAME_D,
          "V is game key D");
    CHECK(nj_port_sdl2_key_for(port, SDLK_0) == NJ_KEY_0,
          "0 is the 0 key");
    CHECK(nj_port_sdl2_key_for(port, SDLK_1) == NJ_KEY_1,
          "1 is the 1 key");
    CHECK(nj_port_sdl2_key_for(port, SDLK_5) == NJ_KEY_5,
          "5 is the 5 key");
    CHECK(nj_port_sdl2_key_for(port, SDLK_9) == NJ_KEY_9,
          "9 is the 9 key");
    CHECK(nj_port_sdl2_key_for(port, SDLK_ASTERISK) == NJ_KEY_STAR,
          "* is the star key");
    CHECK(nj_port_sdl2_key_for(port, SDLK_HASH) == NJ_KEY_POUND,
          "# is the pound key");
    CHECK(nj_port_sdl2_key_for(port, SDLK_w) == -1,
          "an unbound letter is not a game key");
    CHECK(nj_port_sdl2_key_for(port, SDLK_F3) == -1,
          "F3 (speed) is not a game key");
    CHECK(nj_port_sdl2_key_for(port, SDLK_F5) == -1,
          "F5 (save) is not a game key");
    CHECK(nj_port_sdl2_key_for(port, SDLK_F10) == -1,
          "F10 (quit) is not a game key");
    CHECK(nj_port_sdl2_key_for(port, SDLK_F11) == -1,
          "F11 (fullscreen) is not a game key");
    CHECK(nj_port_sdl2_key_for(port, SDLK_F12) == -1,
          "F12 (keypad) is not a game key");
    CHECK(nj_port_sdl2_key_for(port, SDLK_TAB) == -1,
          "Tab (keypad) is not a game key");
    for (i = 0; i < NJ_PORT_BIND_COUNT; i++) {
        CHECK(nj_port_sdl2_bound(port, i) == 0,
              "a fresh port has every slot on its defaults");
    }
}

/* Rebind one slot and put it back: the old key must not keep working while
 * the binding is in place, or the player would have two fire buttons and one
 * of them would be the one they tried to free. */
static void test_remap_reset(nj_port *port)
{
    CHECK(nj_port_sdl2_bind(port, NJ_PORT_BIND_UP, SDLK_w) == 1,
          "up can be bound to W");
    CHECK(nj_port_sdl2_bound(port, NJ_PORT_BIND_UP) == SDLK_w,
          "and the slot reports W");
    CHECK(nj_port_sdl2_key_for(port, SDLK_w) == NJ_KEY_UP,
          "W now means up");
    CHECK(nj_port_sdl2_key_for(port, SDLK_UP) == -1,
          "the arrow no longer means up while W is bound");
    CHECK(nj_port_sdl2_key_for(port, SDLK_DOWN) == NJ_KEY_DOWN,
          "the other slots are untouched");
    CHECK(nj_port_sdl2_bind(port, NJ_PORT_BIND_UP, 0) == 1,
          "key 0 resets the slot");
    CHECK(nj_port_sdl2_bound(port, NJ_PORT_BIND_UP) == 0,
          "a reset slot is back on its defaults");
    CHECK(nj_port_sdl2_key_for(port, SDLK_UP) == NJ_KEY_UP,
          "and the arrow means up again");
    CHECK(nj_port_sdl2_key_for(port, SDLK_w) == -1,
          "and W is nobody's key once more");
}

/* The port's own controls and the phone's keypad are refused, and a refusal
 * must change nothing: silently keeping half a binding is worse than saying
 * no, because the settings page would then show a key the port ignores. */
static void test_reserved(nj_port *port)
{
    CHECK(nj_port_sdl2_bind(port, NJ_PORT_BIND_UP, SDLK_F3) == 0,
          "F3 (speed) cannot be bound");
    CHECK(nj_port_sdl2_bind(port, NJ_PORT_BIND_UP, SDLK_F5) == 0,
          "F5 (save) cannot be bound");
    CHECK(nj_port_sdl2_bind(port, NJ_PORT_BIND_UP, SDLK_F10) == 0,
          "F10 (quit) cannot be bound");
    CHECK(nj_port_sdl2_bind(port, NJ_PORT_BIND_UP, SDLK_F11) == 0,
          "F11 (fullscreen) cannot be bound");
    CHECK(nj_port_sdl2_bind(port, NJ_PORT_BIND_UP, SDLK_F12) == 0,
          "F12 (keypad) cannot be bound");
    CHECK(nj_port_sdl2_bind(port, NJ_PORT_BIND_FIRE, SDLK_TAB) == 0,
          "Tab (keypad) cannot be bound");
    CHECK(nj_port_sdl2_bind(port, -1, SDLK_w) == 0,
          "an unknown slot is refused");
    CHECK(nj_port_sdl2_bind(port, NJ_PORT_BIND_COUNT, SDLK_w) == 0,
          "a slot past the end is refused");
    CHECK(nj_port_sdl2_bind(port, NJ_PORT_BIND_UP, SDLK_1) == 0,
          "the number pad is fixed, so 1 cannot become up");
    CHECK(nj_port_sdl2_bind(port, NJ_PORT_BIND_UP, SDLK_ASTERISK) == 0,
          "* is fixed too");
    CHECK(nj_port_sdl2_bound(port, NJ_PORT_BIND_UP) == 0,
          "a refused binding leaves the slot on its defaults");
    CHECK(nj_port_sdl2_key_for(port, SDLK_UP) == NJ_KEY_UP,
          "and the arrow still means up");
    CHECK(nj_port_sdl2_key_for(port, SDLK_F10) == -1,
          "F10 still belongs to the port, not to a game key");
}

/*
 * One key, one action.
 *
 * Binding fire to the key up was using takes it: W is fire, and up has no key
 * at all (not "the arrow again" - the player did not ask for that, and a
 * hidden second key is what this rule exists to prevent).  Resetting either
 * slot is how the state is undone.
 */
static void test_move(nj_port *port)
{
    CHECK(nj_port_sdl2_bind(port, NJ_PORT_BIND_UP, SDLK_w) == 1,
          "up is on W");
    CHECK(nj_port_sdl2_bind(port, NJ_PORT_BIND_FIRE, SDLK_w) == 1,
          "binding fire to W takes it from up");
    CHECK(nj_port_sdl2_key_for(port, SDLK_w) == NJ_KEY_FIRE,
          "W means fire now");
    CHECK(nj_port_sdl2_key_for(port, SDLK_UP) == -1,
          "up lost W and has no key");
    CHECK(nj_port_sdl2_bound(port, NJ_PORT_BIND_UP) == -1,
          "the slot whose key moved reports no key");
    CHECK(nj_port_sdl2_key_for(port, SDLK_RETURN) == -1,
          "fire has a binding, so its old keys are released too");
    CHECK(nj_port_sdl2_bind(port, NJ_PORT_BIND_UP, 0) == 1,
          "resetting the loser restores it");
    CHECK(nj_port_sdl2_key_for(port, SDLK_UP) == NJ_KEY_UP,
          "and up means up again");
    CHECK(nj_port_sdl2_key_for(port, SDLK_w) == NJ_KEY_FIRE,
          "while W still belongs to fire");
    CHECK(nj_port_sdl2_bind(port, NJ_PORT_BIND_FIRE, 0) == 1,
          "resetting fire restores its defaults");
    CHECK(nj_port_sdl2_key_for(port, SDLK_RETURN) == NJ_KEY_FIRE,
          "Return is fire again");
    CHECK(nj_port_sdl2_key_for(port, SDLK_w) == -1,
          "and W is free again");
}

/*
 * The event path.
 *
 * The API above is only worth testing if it is the same mapping the game
 * receives, so this pushes real events through the window's event loop
 * (display_present) and checks the nj_events the frame loop would get.  A
 * KEYUP for a remapped key matters as much as the KEYDOWN: if only one of the
 * two used the new mapping the key would arrive pressed and stay pressed.
 */
static void test_events(nj_port *port, int w, int h)
{
    nj_event ev;

    drain(port);
    CHECK(nj_port_sdl2_bind(port, NJ_PORT_BIND_UP, SDLK_w) == 1,
          "up is remapped to W for the event path");
    push_key(SDL_KEYDOWN, SDLK_w, SDL_SCANCODE_W);
    SDL_PumpEvents();
    port->display_present(port->ud, 0, 0, w, h);
    CHECK(next_key(port, &ev) == 1 && ev.type == NJ_EV_KEY_DOWN &&
          ev.key == NJ_KEY_UP,
          "a pressed W queues NJ_KEY_DOWN of up");
    push_key(SDL_KEYUP, SDLK_w, SDL_SCANCODE_W);
    SDL_PumpEvents();
    port->display_present(port->ud, 0, 0, w, h);
    CHECK(next_key(port, &ev) == 1 && ev.type == NJ_EV_KEY_UP &&
          ev.key == NJ_KEY_UP,
          "and the release queues NJ_KEY_UP of up, not of W");
    /* The arrow it replaced is dead: it must not queue a game key (it would
     * have to be a printable character to become anything else, and it is
     * not). */
    push_key(SDL_KEYDOWN, SDLK_UP, SDL_SCANCODE_UP);
    SDL_PumpEvents();
    port->display_present(port->ud, 0, 0, w, h);
    CHECK(next_key(port, &ev) == 0,
          "the old arrow queues no game key once the slot is remapped");

    /* F11 is a window operation, not a MIDP key: no key may fall out of it.
     * Toggled twice so the port is back in a window for the rest of the
     * test - and to check that the second toggle is as quiet as the first. */
    drain(port);
    push_key(SDL_KEYDOWN, SDLK_F11, SDL_SCANCODE_F11);
    SDL_PumpEvents();
    port->display_present(port->ud, 0, 0, w, h);
    CHECK(next_key(port, &ev) == 0, "F11 queues no game key");
    push_key(SDL_KEYDOWN, SDLK_F11, SDL_SCANCODE_F11);
    SDL_PumpEvents();
    port->display_present(port->ud, 0, 0, w, h);
    CHECK(next_key(port, &ev) == 0, "toggling back out of fullscreen is silent");
    CHECK(nj_port_sdl2_bind(port, NJ_PORT_BIND_UP, 0) == 1,
          "up can be reset after the event-path checks");
    CHECK(nj_port_sdl2_key_for(port, SDLK_UP) == NJ_KEY_UP,
          "and the arrow is back");
}

/* The client's other switches, all at once: fullscreen, the linear filter,
 * muted audio and no network.  The point is that none of them breaks port
 * creation or display setup, and that mute is a silence, not an error - a
 * Player told the write failed would report a user preference as a bug. */
static void test_env_knobs(void)
{
    nj_port port;
    nj_event ev;
    int w = 0, h = 0, stride = 0;
    void *pixels = NULL;
    nj_audio_tone tone;
    nj_s2 samples[16];

    memset(&port, 0, sizeof(port));
    /* Real setenv, because that is what the client does before building the
     * port; the port reads them with SDL_getenv, which sees the same table. */
    setenv("NJ_SDL2_FULLSCREEN", "1", 1);
    setenv("NJ_SDL2_FILTER", "linear", 1);
    setenv("NJ_SDL2_MUTE", "1", 1);
    setenv("NJ_SDL2_NO_NETWORK", "1", 1);

    CHECK(nj_port_sdl2(&port, "port binds env test") == NJ_OK,
          "a port with every client switch on can be built");
    if (port.display_init == NULL) {
        CHECK(0, "the port has a display_init");
        return;
    }
    /* NJ_PIXFMT_RGB565 is 0, so only a negative return is the failure. */
    CHECK(port.display_init(port.ud, &w, &h, &stride, &pixels) >= 0 &&
          pixels != NULL && w > 0 && h > 0,
          "fullscreen + linear init under the dummy driver");
    port.display_present(port.ud, 0, 0, w, h);
    drain(&port);

    memset(&tone, 0, sizeof(tone));
    tone.freq_hz = 440;
    tone.duration_ms = 20;
    CHECK(port.tone_play(port.ud, &tone, 1) == 0,
          "a muted tone is accepted instead of failing the game");
    memset(samples, 0, sizeof(samples));
    CHECK(port.pcm_write(port.ud, samples, (int)(sizeof(samples) /
                                                sizeof(samples[0]))) ==
          (int)(sizeof(samples) / sizeof(samples[0])),
          "a muted pcm_write still reports the frames it took");
    CHECK(port.pcm_queued(port.ud) == 0,
          "and nothing reached the audio queue");
#if defined(NJ_HAVE_POSIX_NETWORK)
    CHECK(port.http_request == NULL,
          "NJ_SDL2_NO_NETWORK leaves the network hooks NULL");
#endif
    /* The mapping is per port: the switch port starts on the defaults. */
    CHECK(nj_port_sdl2_key_for(&port, SDLK_UP) == NJ_KEY_UP,
          "the switch port's keys are the defaults");
    CHECK(next_key(&port, &ev) == 0, "and it queued no key while starting");
    nj_port_shutdown(&port);

    unsetenv("NJ_SDL2_FULLSCREEN");
    unsetenv("NJ_SDL2_FILTER");
    unsetenv("NJ_SDL2_MUTE");
    unsetenv("NJ_SDL2_NO_NETWORK");
}

int main(void)
{
    nj_port port;
    int w = 0, h = 0, stride = 0;
    void *pixels = NULL;
    char dropped[NJ_LIB_PATH_MAX];

    /* Before SDL_Init: the port must run the whole test without a desktop. */
    setenv("SDL_VIDEODRIVER", "dummy", 0);
    setenv("SDL_AUDIODRIVER", "dummy", 0);
    /* Start from the defaults whatever the environment a script ran us in
     * has to say; test_env_knobs() sets these on purpose later. */
    unsetenv("NJ_SDL2_FULLSCREEN");
    unsetenv("NJ_SDL2_FILTER");
    unsetenv("NJ_SDL2_MUTE");
    unsetenv("NJ_SDL2_NO_NETWORK");

    printf("== nj_port_sdl2: remappable game keys ==\n");

    if (nj_port_sdl2(&port, "port binds test") != NJ_OK) {
        printf("  SKIP no SDL2 port under the dummy driver\n");
        printf("  0 checks, 0 failures\n");
        return 0;
    }

    /* The mapping contract needs no window: the client installs bindings
     * between building the port and starting the MIDlet, and key_for() is
     * what the game page shows. */
    test_defaults(&port);
    test_remap_reset(&port);
    test_reserved(&port);
    test_move(&port);
    CHECK(nj_port_sdl2_take_dropped(&port, dropped, (nj_u4)sizeof(dropped)) ==
          0,
          "a fresh port has no dropped game to hand back");

    /* The event path needs a window (display_init creates it); without one
     * the API checks above are still worth reporting, so skip only these. */
    if (port.display_init(port.ud, &w, &h, &stride, &pixels) < 0 ||
        pixels == NULL || w <= 0 || h <= 0) {
        printf("  SKIP no SDL2 window under the dummy driver; "
               "the event-path checks are skipped\n");
    } else {
        port.display_present(port.ud, 0, 0, w, h);   /* the window is up */
        test_events(&port, w, h);
    }
    nj_port_shutdown(&port);

    /* A second port, this time with every client switch on.  Building it after
     * the first was shut down also re-exercises SDL_Init after SDL_Quit. */
    test_env_knobs();

    printf("  %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
