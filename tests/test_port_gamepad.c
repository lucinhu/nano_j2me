/*
 * nano_j2me - tests/test_port_gamepad.c
 *
 * Game controller support in the SDL2 port.
 *
 * The port's promise is simple: a pad plugged into the desktop makes the game
 * playable without the keyboard, and the mapping is fixed so a game that has
 * only ever seen MIDP keys cannot tell whether the player used a pad or the
 * arrow keys.  What that promise hides is a lot of state - which controller is
 * open, whether a held button has already produced its press, and how the
 * d-pad and the left stick are merged into one set of directions - so this
 * test drives all of it through real SDL controller events:
 *
 *   - a virtual game controller is attached after the port was built, so the
 *     hotplug path (SDL_CONTROLLERDEVICEADDED) is what opens it;
 *   - A fires once and only once while held, and the release arrives;
 *   - B/X/Y, LB/RB, Back and Start reach the game A/B/C, soft-left/right and
 *     clear keys, and d-pad left is left;
 *   - the left stick's extremes are directions, half deflection is dead-zone
 *     jitter, and the right stick is not a direction at all;
 *   - a d-pad direction and the stick holding the same direction make one key
 *     press, and it ends only when both let go;
 *   - a controller that vanishes mid-press releases the key it was holding;
 *   - a plain joystick (a wheel, an untyped virtual device) never produces
 *     MIDP keys;
 *   - a pad attached before the port exists is opened by the constructor, not
 *     only by hotplug;
 *   - NJ_SDL2_GAMEPAD=0 leaves the controller subsystem untouched and ignores
 *     the pad completely.
 *
 * The virtual joystick is SDL's own: no hardware, no desktop and no VM.  The
 * port is created with nj_port_sdl2() and its frame API (display_init,
 * display_present, input_poll) is called directly, which is exactly what the
 * client's main loop does.  Under the dummy video/audio drivers, so the test
 * runs headless; if SDL or virtual joysticks are unavailable it prints SKIP and
 * succeeds, like the other port tests.
 *
 * Usage: test_port_gamepad    (from the repository root; no fixtures needed)
 */
#define _POSIX_C_SOURCE 200809L

#include "nj2me/nj.h"          /* nj_port_sdl2 and the nj_event queue */

#include <SDL.h>

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

/*
 * The raw button/axis indexes of SDL's virtual game controller.
 *
 * SDL_JoystickSetVirtualButton() and SDL_JoystickSetVirtualAxis() speak raw
 * joystick numbers, and for the virtual game controller those numbers are the
 * SDL_CONTROLLER_BUTTON_* / SDL_CONTROLLER_AXIS_* values (that is SDL's
 * documented virtual-device layout).  Spelled out here so the test can also
 * use buttons the mapping does not consume (the share/guide button is not
 * mapped on purpose).
 */
enum {
    VBTN_A = 0,
    VBTN_B = 1,
    VBTN_X = 2,
    VBTN_Y = 3,
    VBTN_BACK = 4,
    VBTN_START = 6,
    VBTN_LEFTSHOULDER = 9,
    VBTN_RIGHTSHOULDER = 10,
    VBTN_DPAD_UP = 11,
    VBTN_DPAD_DOWN = 12,
    VBTN_DPAD_LEFT = 13,
    VBTN_DPAD_RIGHT = 14
};

enum {
    VAXIS_LEFTX = 0,
    VAXIS_LEFTY = 1,
    VAXIS_RIGHTX = 2,
    VAXIS_RIGHTY = 3
};

/* Pop the next KEY event from the port's queue, if there is one.  Non-key
 * events are drained on the way; the tests below only ever put keys there. */
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

static void expect_key_at(nj_port *port, int type, int key, const char *what,
                          int line)
{
    nj_event ev;

    checks++;
    if (!next_key(port, &ev)) {
        failures++;
        printf("  FAIL %s:%d  %s (no key event)\n", __FILE__, line, what);
        return;
    }
    if (ev.type != type || ev.key != key) {
        failures++;
        printf("  FAIL %s:%d  %s (got type %d, key %d)\n",
               __FILE__, line, what, (int)ev.type, ev.key);
    }
}

static void expect_none_at(nj_port *port, const char *what, int line)
{
    nj_event ev;

    checks++;
    if (next_key(port, &ev)) {
        failures++;
        printf("  FAIL %s:%d  %s (got type %d, key %d)\n",
               __FILE__, line, what, (int)ev.type, ev.key);
    }
}

#define EXPECT_KEY(port, type, key, what) \
    expect_key_at((port), (type), (key), (what), __LINE__)
#define EXPECT_NONE(port, what) \
    expect_none_at((port), (what), __LINE__)

/* Build the port and its window the way the client does.  Returns 0 when SDL
 * could not give us a window under the dummy driver (the caller skips). */
static int open_test_port(nj_port *port, const char *title, int *w, int *h)
{
    int stride = 0;
    void *pixels = NULL;

    if (nj_port_sdl2(port, title) != NJ_OK) {
        return 0;
    }
    if (port->display_init(port->ud, w, h, &stride, &pixels) < 0 ||
        !pixels || *w <= 0 || *h <= 0) {
        nj_port_shutdown(port);
        return 0;
    }
    port->display_present(port->ud, 0, 0, *w, *h);
    return 1;
}

/* Let the port see everything SDL has queued.  SDL_PumpEvents() turns the
 * virtual device's new button/axis state into controller events and
 * display_present() drains them through the same switch the frame loop runs. */
static void pump(nj_port *port, int w, int h)
{
    SDL_PumpEvents();
    port->display_present(port->ud, 0, 0, w, h);
}

/* Drop whatever the port queued, so the next assertion starts from nothing. */
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

static void pad_button(SDL_Joystick *js, int button, int down)
{
    (void)SDL_JoystickSetVirtualButton(js, button, down ? 1 : 0);
}

static void pad_axis(SDL_Joystick *js, int axis, int value)
{
    (void)SDL_JoystickSetVirtualAxis(js, axis, value);
}

/*
 * The hotplug path, and the mapping it feeds: a controller attached to a
 * running port is opened when SDL announces it.
 *
 * Returns 0 when the checks ran, -1 when SDL gave us no window, -2 when the
 * virtual controller cannot be attached (the caller prints SKIP).
 */
static int phase_hotplug(void)
{
    nj_port port;
    int w = 0, h = 0;
    int idx;
    SDL_Joystick *js;

    if (!open_test_port(&port, "port gamepad test", &w, &h)) {
        return -1;
    }

    idx = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 16, 0);
    if (idx < 0) {
        nj_port_shutdown(&port);
        return -2;
    }
    js = SDL_JoystickOpen(idx);
    if (!js) {
        SDL_JoystickDetachVirtual(idx);
        nj_port_shutdown(&port);
        return -2;
    }

    CHECK(SDL_IsGameController(idx) == SDL_TRUE,
          "the virtual device is a game controller");
    /* The port was built before the pad appeared, so this present() is what
     * opens it: SDL_CONTROLLERDEVICEADDED is in the queue now. */
    pump(&port, w, h);
    drain(&port);

    /* Fire is a held button, not a stream of presses. */
    pad_button(js, VBTN_A, 1);
    pump(&port, w, h);
    EXPECT_KEY(&port, NJ_EV_KEY_DOWN, NJ_KEY_FIRE, "A presses fire");
    pump(&port, w, h);
    EXPECT_NONE(&port, "holding A does not repeat fire");
    pad_button(js, VBTN_A, 0);
    pump(&port, w, h);
    EXPECT_KEY(&port, NJ_EV_KEY_UP, NJ_KEY_FIRE, "releasing A releases fire");

    /* The fixed button map: physical buttons, not the keyboard bindings. */
    {
        static const struct {
            int button;
            int key;
            const char *what;
        } pad_map[] = {
            { VBTN_B, NJ_KEY_GAME_A, "B is game A" },
            { VBTN_X, NJ_KEY_GAME_B, "X is game B" },
            { VBTN_Y, NJ_KEY_GAME_C, "Y is game C" },
            { VBTN_LEFTSHOULDER, NJ_KEY_SOFT_LEFT, "LB is soft-left" },
            { VBTN_RIGHTSHOULDER, NJ_KEY_SOFT_RIGHT, "RB is soft-right" },
            { VBTN_BACK, NJ_KEY_CLEAR, "Back is clear" },
            { VBTN_START, NJ_KEY_GAME_D, "Start is game D" },
            { VBTN_DPAD_LEFT, NJ_KEY_LEFT, "d-pad left is left" }
        };
        int i;

        for (i = 0; i < (int)(sizeof(pad_map) / sizeof(pad_map[0])); i++) {
            pad_button(js, pad_map[i].button, 1);
            pump(&port, w, h);
            EXPECT_KEY(&port, NJ_EV_KEY_DOWN, pad_map[i].key, pad_map[i].what);
            pad_button(js, pad_map[i].button, 0);
            pump(&port, w, h);
            EXPECT_KEY(&port, NJ_EV_KEY_UP, pad_map[i].key, pad_map[i].what);
        }
    }

    /* The left stick.  A real stick never rests at 0, so 8000 is jitter, and a
     * held stick is still one key press. */
    pad_axis(js, VAXIS_LEFTX, 8000);
    pump(&port, w, h);
    EXPECT_NONE(&port, "a half-deflected stick is inside the dead zone");
    pad_axis(js, VAXIS_LEFTX, 0);
    pump(&port, w, h);

    pad_axis(js, VAXIS_LEFTX, -32768);
    pump(&port, w, h);
    EXPECT_KEY(&port, NJ_EV_KEY_DOWN, NJ_KEY_LEFT, "pushing the stick left is left");
    pump(&port, w, h);
    EXPECT_NONE(&port, "holding the stick left does not repeat");
    pad_axis(js, VAXIS_LEFTX, 0);
    pump(&port, w, h);
    EXPECT_KEY(&port, NJ_EV_KEY_UP, NJ_KEY_LEFT, "centring the stick releases left");

    pad_axis(js, VAXIS_LEFTY, -32768);
    pump(&port, w, h);
    EXPECT_KEY(&port, NJ_EV_KEY_DOWN, NJ_KEY_UP, "pushing the stick up is up");
    pad_axis(js, VAXIS_LEFTY, 0);
    pump(&port, w, h);
    EXPECT_KEY(&port, NJ_EV_KEY_UP, NJ_KEY_UP, "centring the stick releases up");

    /* The right stick is not part of the mapping. */
    pad_axis(js, VAXIS_RIGHTX, -32768);
    pump(&port, w, h);
    EXPECT_NONE(&port, "the right stick is not a direction");
    pad_axis(js, VAXIS_RIGHTX, 0);
    pump(&port, w, h);

    /* The d-pad and the stick merge: same "up", one press, released when both
     * are done. */
    pad_button(js, VBTN_DPAD_UP, 1);
    pump(&port, w, h);
    EXPECT_KEY(&port, NJ_EV_KEY_DOWN, NJ_KEY_UP, "d-pad up is up");
    pad_axis(js, VAXIS_LEFTY, -32768);
    pump(&port, w, h);
    EXPECT_NONE(&port, "the stick joining a held d-pad is not a second up");
    pad_button(js, VBTN_DPAD_UP, 0);
    pump(&port, w, h);
    EXPECT_NONE(&port, "releasing the d-pad keeps the stick's up");
    pad_axis(js, VAXIS_LEFTY, 0);
    pump(&port, w, h);
    EXPECT_KEY(&port, NJ_EV_KEY_UP, NJ_KEY_UP, "releasing the stick ends up");

    /* A pad that vanishes mid-press must not leave a key stuck down. */
    pad_button(js, VBTN_A, 1);
    pump(&port, w, h);
    EXPECT_KEY(&port, NJ_EV_KEY_DOWN, NJ_KEY_FIRE, "A is down before the pad is removed");
    SDL_JoystickClose(js);
    js = NULL;
    CHECK(SDL_JoystickDetachVirtual(idx) == 0, "the virtual controller detaches");
    pump(&port, w, h);
    EXPECT_KEY(&port, NJ_EV_KEY_UP, NJ_KEY_FIRE,
               "removing the pad releases the held fire");

    /* A plain joystick is not a game controller and must not become keys. */
    idx = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_UNKNOWN, 6, 16, 0);
    if (idx < 0) {
        printf("  SKIP could not attach a plain virtual joystick\n");
    } else {
        CHECK(SDL_IsGameController(idx) == SDL_FALSE,
              "a plain joystick is not a game controller");
        js = SDL_JoystickOpen(idx);
        CHECK(js != NULL, "the plain virtual joystick opens");
        /* The port sees SDL_JOYDEVICEADDED for it and leaves it alone. */
        pump(&port, w, h);
        drain(&port);
        if (js) {
            pad_button(js, 0, 1);
            pump(&port, w, h);
            EXPECT_NONE(&port, "a plain joystick's button is not a game key");
            pad_axis(js, VAXIS_LEFTX, -32768);
            pump(&port, w, h);
            EXPECT_NONE(&port, "a plain joystick's axis is not a game key");
            pad_button(js, 0, 0);
            pad_axis(js, VAXIS_LEFTX, 0);
            pump(&port, w, h);
            SDL_JoystickClose(js);
            js = NULL;
        }
        SDL_JoystickDetachVirtual(idx);
        pump(&port, w, h);
    }

    nj_port_shutdown(&port);
    return 0;
}

/*
 * A pad that is already attached when the port is built is opened by the
 * constructor, not only by the hotplug event.
 */
static void phase_constructor(void)
{
    nj_port port;
    int w = 0, h = 0;
    int idx;
    SDL_Joystick *js;

    if (SDL_Init(SDL_INIT_JOYSTICK) != 0) {
        printf("  SKIP could not initialise the joystick subsystem: %s\n",
               SDL_GetError());
        return;
    }
    idx = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 16, 0);
    if (idx < 0) {
        printf("  SKIP could not attach a virtual controller: %s\n",
               SDL_GetError());
        SDL_Quit();
        return;
    }
    if (!open_test_port(&port, "port gamepad init test", &w, &h)) {
        SDL_JoystickDetachVirtual(idx);
        SDL_Quit();
        printf("  SKIP no SDL2 window under the dummy driver\n");
        return;
    }
    js = SDL_JoystickOpen(idx);
    CHECK(js != NULL, "the pad attached before the port opens as a joystick");
    pump(&port, w, h);
    drain(&port);
    if (js) {
        pad_button(js, VBTN_A, 1);
        pump(&port, w, h);
        EXPECT_KEY(&port, NJ_EV_KEY_DOWN, NJ_KEY_FIRE,
                   "the constructor opens the pad that was already attached");
        pad_button(js, VBTN_A, 0);
        pump(&port, w, h);
        EXPECT_KEY(&port, NJ_EV_KEY_UP, NJ_KEY_FIRE,
                   "and its release reaches the game");
        SDL_JoystickClose(js);
    }
    SDL_JoystickDetachVirtual(idx);
    pump(&port, w, h);
    nj_port_shutdown(&port);
}

/*
 * NJ_SDL2_GAMEPAD=0: the port must not even initialise controller support, and
 * a pad that the rest of the process can still see must produce nothing.
 */
static void phase_disabled(void)
{
    nj_port port;
    int w = 0, h = 0;
    int idx;
    SDL_GameController *pad;

    setenv("NJ_SDL2_GAMEPAD", "0", 1);
    if (!open_test_port(&port, "port gamepad disabled test", &w, &h)) {
        unsetenv("NJ_SDL2_GAMEPAD");
        printf("  SKIP no SDL2 window under the dummy driver\n");
        return;
    }
    CHECK(SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0,
          "NJ_SDL2_GAMEPAD=0 leaves controller support uninitialised");

    /* The port did not initialise it, so the test does: without a controller
     * of our own SDL would not deliver the events the port has to ignore. */
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        printf("  SKIP could not initialise controller support: %s\n",
               SDL_GetError());
        nj_port_shutdown(&port);
        unsetenv("NJ_SDL2_GAMEPAD");
        return;
    }
    idx = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 16, 0);
    if (idx < 0) {
        printf("  SKIP could not attach a virtual controller: %s\n",
               SDL_GetError());
    } else {
        pad = SDL_GameControllerOpen(idx);
        CHECK(pad != NULL,
              "the test can open a controller while the port's support is off");
        if (pad) {
            SDL_Joystick *js = SDL_GameControllerGetJoystick(pad);

            /* The ADDED event is in the queue and the port has to ignore it. */
            pump(&port, w, h);
            drain(&port);
            pad_button(js, VBTN_A, 1);
            pump(&port, w, h);
            EXPECT_NONE(&port, "NJ_SDL2_GAMEPAD=0: a pad button queues no game key");
            pad_axis(js, VAXIS_LEFTX, -32768);
            pump(&port, w, h);
            EXPECT_NONE(&port, "NJ_SDL2_GAMEPAD=0: a pad axis queues no game key");
            pad_button(js, VBTN_A, 0);
            pad_axis(js, VAXIS_LEFTX, 0);
            pump(&port, w, h);
            SDL_GameControllerClose(pad);
        }
        SDL_JoystickDetachVirtual(idx);
        pump(&port, w, h);
    }
    nj_port_shutdown(&port);
    unsetenv("NJ_SDL2_GAMEPAD");
}

int main(void)
{
    int rc;

    /* Dummy drivers: a desktop is not required, and the window/renderer path
     * is identical to a real one from the port's point of view. */
    setenv("SDL_VIDEODRIVER", "dummy", 0);
    setenv("SDL_AUDIODRIVER", "dummy", 0);
    unsetenv("NJ_SDL2_GAMEPAD");

    printf("== nj_port_sdl2: game controller ==\n");

    rc = phase_hotplug();
    if (rc != 0) {
        if (rc == -1) {
            printf("  SKIP no SDL2 window under the dummy driver\n");
        } else {
            printf("  SKIP virtual game controllers are unavailable here\n");
        }
        printf("  0 checks, 0 failures\n");
        return 0;
    }
    phase_constructor();
    phase_disabled();

    printf("  %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
