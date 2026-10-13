/*
 * nano_j2me - nj_port_sdl2.c
 *
 * The desktop port: an SDL2 window showing the runtime's framebuffer.
 *
 * It is the reference implementation for a "real" display port - it shows how
 * to handle a texture whose pitch differs from width*bytes_per_pixel, how to
 * do integer-only upscaling, and how to translate a keyboard into MIDP keys.
 * A microcontroller port does the same three things against an SPI panel.
 *
 * The window also carries an overlay: the on-screen keypad (see the key table
 * below) and one control that is not a key - a small rounded badge, top
 * centre, showing the game speed as "1.0x".  Clicking the badge or pressing F3
 * cycles 1.0x -> 1.5x -> 2.0x -> 3.0x -> 0.5x, and F5 takes a save-state
 * snapshot.  Both use the same rule: the overlay only writes a number, and the
 * next frame applies it (sdl_input_poll), because nothing outside the VM's own
 * thread may touch the VM.  sdl_install_codecs() is where the VM pointer comes
 * from.
 *
 * The keypad is pressed the way a finger presses a phone pad: hold the mouse
 * button (or put a finger down) on it and slide.  While a press is in
 * progress, moving from one cap to the next releases the first and presses the
 * second, moving off onto empty space releases it, and letting go of the button
 * ends it (sdl_pointer_to()).  A mouse moving with NO button held does nothing
 * at all - it is not a finger, and neither the keypad nor the game hears from
 * it.  A press that started inside the game area and slides onto a cap presses
 * that cap too, and sliding back off releases it, which is the same rule from
 * the other side.
 */
#include "nj2me/nj_port.h"
#include "nj2me/nj_vm.h"       /* speed + save-state, applied on the frame */

#include "nj_sdl2_font.h"
#include "nj2me/nj_types.h"

#include <SDL.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * EVERY key the runtime has, not just the four directions and fire.
 *
 * A J2ME keypad is 27 keys, and games use all of them: the digits as a menu
 * shortcut, * and # as extra fire buttons, the soft keys for the command bar,
 * and A/B/C/D for whatever the game likes.  An overlay with only a D-pad would
 * run a platformer and nothing else.
 *
 * The keys are not the whole overlay.  Above them sits the speed badge, drawn
 * with the same fill_round()/draw_label() helpers, and it is hit-tested before
 * the keypad and before the game: a click on the badge is neither a keypress
 * nor a pointer press inside the MIDlet, and a click anywhere else is neither.
 *
 * The keys are pressed by HOLDING a button down and pointing at them, or by a
 * finger - a press has to be in progress, exactly as on the Android pad, so a
 * mouse merely moving across the window presses nothing at all.  The keyboard
 * mapping below is the same set of keys on physical buttons and is unchanged by
 * any of this.
 */
enum {
    NJ_SDL2_BTN_UP = 0, NJ_SDL2_BTN_DOWN, NJ_SDL2_BTN_LEFT, NJ_SDL2_BTN_RIGHT,
    NJ_SDL2_BTN_FIRE,
    NJ_SDL2_BTN_1, NJ_SDL2_BTN_2, NJ_SDL2_BTN_3,
    NJ_SDL2_BTN_4, NJ_SDL2_BTN_5, NJ_SDL2_BTN_6,
    NJ_SDL2_BTN_7, NJ_SDL2_BTN_8, NJ_SDL2_BTN_9,
    NJ_SDL2_BTN_STAR, NJ_SDL2_BTN_0, NJ_SDL2_BTN_POUND,
    NJ_SDL2_BTN_SOFT_L, NJ_SDL2_BTN_SOFT_R, NJ_SDL2_BTN_CLEAR,
    NJ_SDL2_BTN_A, NJ_SDL2_BTN_B, NJ_SDL2_BTN_C, NJ_SDL2_BTN_D,
    NJ_SDL2_BTN_VOLUP, NJ_SDL2_BTN_VOLDOWN, NJ_SDL2_BTN_POWER,
    NJ_SDL2_NBTN
};

/* What to draw on a button. */
enum { NJ_SDL2_SHAPE_RECT = 0, NJ_SDL2_SHAPE_TRI, NJ_SDL2_SHAPE_DOT };

/* Which way an arrow points.  NOT the button index: a direction and a position
 * in an enum are different things that happen to be numbers, and passing the
 * index drew a cross whose arrows all pointed the wrong way while every rect was
 * in the right place. */
#define SDL2_DIR_UP    0
#define SDL2_DIR_DOWN  1
#define SDL2_DIR_LEFT  2
#define SDL2_DIR_RIGHT 3

typedef struct sdl2_key_def {
    nj_u1       key;      /* the nj_key this button sends */
    nj_u1       shape;
    nj_u1       arrow;    /* SDL2_DIR_*, for NJ_SDL2_SHAPE_TRI */
    const char *label;    /* drawn with nj_sdl2_font.h; NULL for none */
} sdl2_key_def;

/*
 * The buttons, in the enum's order.  `event` at the bottom of the file is the
 * only thing that has to stay in step with the enum, and it is one line per
 * entry.
 */
static const sdl2_key_def sdl2_keys[NJ_SDL2_NBTN] = {
    { NJ_KEY_UP,         NJ_SDL2_SHAPE_TRI,  SDL2_DIR_UP,    0 },
    { NJ_KEY_DOWN,       NJ_SDL2_SHAPE_TRI,  SDL2_DIR_DOWN,  0 },
    { NJ_KEY_LEFT,       NJ_SDL2_SHAPE_TRI,  SDL2_DIR_LEFT,  0 },
    { NJ_KEY_RIGHT,      NJ_SDL2_SHAPE_TRI,  SDL2_DIR_RIGHT, 0 },
    { NJ_KEY_FIRE,       NJ_SDL2_SHAPE_DOT,  SDL2_DIR_UP,    0 },
    { NJ_KEY_1,          NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "1" },
    { NJ_KEY_2,          NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "2" },
    { NJ_KEY_3,          NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "3" },
    { NJ_KEY_4,          NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "4" },
    { NJ_KEY_5,          NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "5" },
    { NJ_KEY_6,          NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "6" },
    { NJ_KEY_7,          NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "7" },
    { NJ_KEY_8,          NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "8" },
    { NJ_KEY_9,          NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "9" },
    { NJ_KEY_STAR,       NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "*" },
    { NJ_KEY_0,          NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "0" },
    { NJ_KEY_POUND,      NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "#" },
    { NJ_KEY_SOFT_LEFT,  NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "L" },
    { NJ_KEY_SOFT_RIGHT, NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "R" },
    { NJ_KEY_CLEAR,      NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "CLR" },
    { NJ_KEY_GAME_A,     NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "A" },
    { NJ_KEY_GAME_B,     NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "B" },
    { NJ_KEY_GAME_C,     NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "C" },
    { NJ_KEY_GAME_D,     NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "D" },
    { NJ_KEY_VOLUP,      NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "+" },
    { NJ_KEY_VOLDOWN,    NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "-" },
    { NJ_KEY_POWER,      NJ_SDL2_SHAPE_RECT, SDL2_DIR_UP,    "P" }
};

typedef struct sdl_state {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;

    int  width;
    int  height;
    int  stride;
    int  scale;
    int  format;          /* NJ_PIXFMT_* */
    void *pixels;         /* our own buffer; the texture is a separate copy */

    nj_event queue[256];
    int  qh, qt;

    int  quit;
    int  have_scale_arg;
    const nj_port *port;   /* the caller's struct: we read width/height from it */
    /*
     * Audio, the real kind: an SDL device opened on demand and fed with
     * SDL_QueueAudio.  Opened lazily because a MIDlet that never plays a sound
     * should not hold an audio device open - on a phone that is a battery
     * conversation, and on a desktop it is a "why is this app in my mixer"
     * conversation.
     */
    SDL_AudioDeviceID audio;
    int  audio_rate;
    int  audio_chans;

    const char *capture;   /* NJ_SDL2_CAPTURE: proof-of-presentation dump */
    int  capture_n;
    int  capture_every;

    /*
     * On-screen keys.
     *
     * A desktop emulator has a keyboard, so these are for the other two cases:
     * a touchscreen, and a user who would rather click than memorise that
     * Escape is the soft-right key.  They are drawn OVER the game, in WINDOW
     * coordinates - which is why this port computes its own letterbox instead
     * of letting SDL_RenderSetLogicalSize do it: with a logical size set, every
     * drawing call is in game pixels and a button would have to be drawn inside
     * the 176x208 the game owns, covering the thing you are trying to play.
     *
     * Default on.  F12 or Tab hides them; NJ_SDL2_KEYS=0 starts hidden.
     */
    int  keys_visible;
    SDL_Rect game_dst;      /* the letterboxed game area, in window pixels */
    SDL_Rect btn[NJ_SDL2_NBTN];
    int      btn_n;
    int      btn_held;      /* the cap being held down, or -1 */
    /*
     * True between a press that landed in the GAME area and its release.
     *
     * A press is what makes the pointer mean anything: with the mouse button up
     * (or no finger down) the pointer is just a pointer, and this flag is the
     * other half of that rule - it remembers that the press went DOWN on the
     * game, so only then do moves and the release become pointer events for the
     * MIDlet.  A press that began on a cap never does.
     */
    int      game_drag;

    /*
     * The speed badge.
     *
     * `speed_percent` is what the badge says and the only thing the UI writes;
     * `speed_applied` is what the VM has been told.  They are separate because
     * the VM may only be touched from its own thread, at the top of a frame -
     * the click that changes the speed happens inside SDL_PollEvent, during
     * presentation, so the value has to wait for the next frame.  That is also
     * why `vm` is captured in install_codecs(): a port never creates the VM, it
     * is handed to it once.
     */
    nj_vm *vm;
    int    speed_percent;
    int    speed_applied;   /* 0 until the VM has been told; then == speed */
    int    speed_held;      /* the button / finger went down on the badge */
    SDL_Rect speed_btn;     /* the badge, in window pixels; w==0: off screen */

    /*
     * Save state.  The port cannot see the JAR path (nj_port has no such
     * field), so F5 saves to NJ_SDL2_STATE and says so when it is unset.
     * Loading is deliberately not offered: a snapshot has to be restored
     * before the MIDlet is constructed, which only the runner can do
     * (--load-state), never a hotkey in a running VM.
     */
    const char *state_path; /* NJ_SDL2_STATE, or NULL */
    int         save_req;   /* F5 was pressed; served on the next frame */
} sdl_state;

/* The speeds the badge cycles through, in percent. */
static const int sdl2_speeds[] = { 100, 150, 200, 300, 50 };
#define SDL2_NSPEEDS ((int)(sizeof(sdl2_speeds) / sizeof(sdl2_speeds[0])))


#define SS(ud) ((sdl_state *)(ud))

/* ------------------------------------------------------------------ */
/* Time / log / idle                                                   */
/* ------------------------------------------------------------------ */
static nj_u4 sdl_millis(void *ud)
{
    (void)ud;
    return (nj_u4)SDL_GetTicks();
}

static void sdl_sleep_ms(void *ud, nj_u4 ms)
{
    (void)ud;
    SDL_Delay(ms);
}


/*
 * Device properties.  These are facts about the MACHINE, which is why they live
 * in the port and not in the runtime.
 *
 * com.nokia.keyboard.type deserves the comment: a game built from source here
 * probed it and called .equals() on the result without checking for null, which
 * is a bug in the game - but a real Nokia device always answers, so the game
 * never met it.  A desktop has no keypad, and "None" is exactly what that
 * means: the game then hides its on-screen keypad command, which is correct
 * behaviour for a device that has no keypad to hide.
 */
static const char *sdl_device_property(void *ud, const char *key)
{
    (void)ud;
    if (!key) {
        return NULL;
    }
    if (strcmp(key, "com.nokia.keyboard.type") == 0) {
        return "None";              /* there is no keypad on a desktop */
    }
    if (strcmp(key, "com.nokia.mid.ui.version") == 0) {
        return "1.1";
    }
    if (strcmp(key, "microedition.platform") == 0) {
        return "nano_j2me/sdl2";
    }
    return NULL;                    /* honestly unknown */
}

static void sdl_idle(void *ud)
{
    (void)ud;
    SDL_Delay(1);
}

static void sdl_log(void *ud, int level, const char *msg)
{
    (void)ud;
    if (level <= 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", msg);
    } else if (level == 1) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", msg);
    } else {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", msg);
    }
}

/* ------------------------------------------------------------------ */
/* Display                                                             */
/* ------------------------------------------------------------------ */
static Uint32 sdl_pixel_format(int nj_format)
{
    switch (nj_format) {
#if NJ_PIXFMT == NJ_PIXFMT_RGB888
    case NJ_PIXFMT_RGB888:   return SDL_PIXELFORMAT_RGB24;
#endif
#if NJ_PIXFMT == NJ_PIXFMT_ARGB8888
    case NJ_PIXFMT_ARGB8888: return SDL_PIXELFORMAT_ARGB8888;
#endif
    default:                 return SDL_PIXELFORMAT_RGB565;
    }
}

static int sdl_bytes_per_pixel(int nj_format)
{
    switch (nj_format) {
    case NJ_PIXFMT_RGB565:   return 2;
    case NJ_PIXFMT_RGB888:   return 3;
    default:                 return 4;
    }
}

static int sdl_display_init(void *ud, int *w, int *h, int *stride, void **px)
{
    sdl_state *s = SS(ud);
    int bpp = sdl_bytes_per_pixel(NJ_PIXFMT);

    /*
     * Take the size from the caller's nj_port, which is where --size lands.
     *
     * The port used to ignore it entirely and always use the compile-time
     * default, so the same command line produced a 176x208 screen headless and
     * a 240x320 one here - and a title that gates itself on the screen
     * dimensions ("Screen too small") then behaved differently on the two
     * ports for no reason the user could see.
     */
    if (s->port && s->port->width > 0) {
        s->width = s->port->width;
    }
    if (s->port && s->port->height > 0) {
        s->height = s->port->height;
    }
    if (s->width <= 0) {
        s->width = NJ_SCREEN_WIDTH;
    }
    if (s->height <= 0) {
        s->height = NJ_SCREEN_HEIGHT;
    }
    s->format = NJ_PIXFMT;
    s->stride = s->width * bpp;

    /*
     * Keep our own buffer rather than locking the texture every frame: it lets
     * the rasterizer write with a stable pointer and lets us update the whole
     * texture with a single SDL_UpdateTexture call.
     */
    s->pixels = calloc(1u, (size_t)s->stride * (size_t)s->height);
    if (!s->pixels) {
        return -1;
    }

    if (s->scale <= 0) {
        /* Pick the largest integer scale that leaves a margin. */
        SDL_DisplayMode dm;
        int sw = 1280, sh = 800;
        if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
            sw = dm.w;
            sh = dm.h;
        }
        s->scale = 1;
        while ((s->scale + 1) * s->width + 64 <= sw &&
               (s->scale + 1) * s->height + 64 <= sh) {
            s->scale++;
        }
    }
    (void)s->have_scale_arg;

    s->window = SDL_CreateWindow("nano_j2me",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 s->width * s->scale, s->height * s->scale,
                                 SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!s->window) {
        return -1;
    }
    /*
     * Prefer the SOFTWARE renderer, and make the accelerated one opt-in.
     *
     * This looks backwards and is not.  Asking SDL for an accelerated renderer
     * on a display without usable GL does not return NULL - the X server
     * raises BadValue on the GLX request and SDL's X11 error handler makes it
     * fatal, so the process dies before the fallback below can ever run.  That
     * is a headless X server, a remote X session and anything without DRI,
     * which is a large fraction of the places a console emulator gets run.
     *
     * A blit of a 240x320 texture is not a workload that needs a GPU, and the
     * software path behaves identically everywhere and is deterministic.
     * NJ_SDL2_ACCEL=1 asks for the GPU for anyone who wants vsync.
     */
    {
        const char *want_accel = SDL_getenv("NJ_SDL2_ACCEL");
        Uint32 flags = SDL_RENDERER_SOFTWARE;

        if (want_accel && *want_accel && *want_accel != '0') {
            flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;
        }
        s->renderer = SDL_CreateRenderer(s->window, -1, flags);
        if (!s->renderer && flags != SDL_RENDERER_SOFTWARE) {
            s->renderer = SDL_CreateRenderer(s->window, -1,
                                             SDL_RENDERER_SOFTWARE);
        }
        if (!s->renderer) {
            return -1;
        }
    }
    /*
     * NO SDL_RenderSetLogicalSize.  It would letterbox the game for us, and it
     * would also put every subsequent drawing call in GAME pixels - so the
     * on-screen keys would have to fit inside the 176x208 the game owns and
     * would cover the thing being played.  sdl_layout() does the same
     * arithmetic and leaves the renderer in window coordinates.
     */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    s->texture = SDL_CreateTexture(s->renderer, sdl_pixel_format(NJ_PIXFMT),
                                   SDL_TEXTUREACCESS_STREAMING,
                                   s->width, s->height);
    if (!s->texture) {
        return -1;
    }

    /*
     * Take keyboard focus, and say which key does what.
     *
     * The most common "the game is deaf" report on a desktop is a window that
     * was mapped but never focused: the key events go to whatever window the
     * window manager left focused, while the mouse keeps working because
     * button and motion events are delivered to the window under the pointer.
     * Raising and explicitly focusing the window removes the guesswork, and
     * printing the mapping lets a player tell "no key arrived" from "that key
     * is not mapped to anything".
     */
    SDL_RaiseWindow(s->window);
    SDL_SetWindowInputFocus(s->window);
    /*
     * Raising and focusing is not always enough: an Xwayland window that the
     * window manager will not activate still receives nothing, and SDL hands
     * the key to whatever surface the compositor focused.  A keyboard grab
     * bypasses activation entirely - measured here: without it a synthetic
     * Return leaves the picture byte-identical, with it the MIDlet repaints.
     * Opt-in, because SDL documents a keyboard grab as a VM-frontend tool
     * that also captures Alt+Tab and the Meta key, which is not what a player
     * wants from a windowed emulator by default.
     */
    {
        const char *grab = getenv("NJ_SDL_GRAB");

        if (grab != NULL && grab[0] != '\0' && grab[0] != '0') {
            SDL_SetWindowKeyboardGrab(s->window, SDL_TRUE);
            sdl_log(NULL, 2, "keys: keyboard grabbed (NJ_SDL_GRAB)");
        }
    }
    sdl_log(NULL, 2,
            "keys: arrows=up/down/left/right  Enter/Space=fire  F1=soft-left"
            "  Esc/F2=soft-right  Backspace=clear  0-9 * #  "
            "Z/X/C/V=game A/B/C/D  mouse: hold the button and slide onto a key"
            " to press it (a bare move presses nothing)  F12/Tab=keypad"
            "  click the speed badge / F3=game speed  F5=save state"
            "  (NJ_SDL_GRAB=1 if the window cannot be focused)");
    if (s->state_path && s->state_path[0]) {
        char msg[256];
        SDL_snprintf(msg, sizeof(msg), "state: F5 writes %s", s->state_path);
        sdl_log(NULL, 2, msg);
    } else {
        sdl_log(NULL, 2, "state: F5 needs NJ_SDL2_STATE=<file> - the port is "
                         "not told the JAR path, so it cannot pick one");
    }

    *w = s->width;
    *h = s->height;
    *stride = s->stride;
    *px = s->pixels;
    return NJ_PIXFMT;
}

/*
 * Queue a pointer event, in DEVICE pixels.
 *
 * Mouse coordinates arrive in window pixels while the runtime's contract is
 * device pixels, so the caller scales them down first; a tap that skipped that
 * would land at `scale` times its intended position and miss everything below
 * the top-left corner of the screen.
 */
static void sdl_push_pointer(sdl_state *s, int x, int y, unsigned char type)
{
    nj_event e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.x = (nj_s2)x;
    e.y = (nj_s2)y;
    s->queue[s->qt] = e;
    s->qt = (s->qt + 1) % (int)(sizeof(s->queue) / sizeof(s->queue[0]));
}

static void sdl_push_key(sdl_state *s, int key, int down)
{
    nj_event e;
    memset(&e, 0, sizeof(e));
    e.type = (nj_u1)(down ? NJ_EV_KEY_DOWN : NJ_EV_KEY_UP);
    e.key = (nj_u1)key;
    s->queue[s->qt] = e;
    s->qt = (s->qt + 1) % (int)(sizeof(s->queue) / sizeof(s->queue[0]));
}

/* Window pixels to device pixels.  The window is the screen at an integer
 * scale, so this is exact and needs no rounding. */
/* Window point -> game point, through the letterbox.  A click in the bars maps
 * outside the screen, which the runtime clips for us. */
static void sdl_to_game(sdl_state *s, int wx, int wy, int *gx, int *gy)
{
    int scale = (s->scale > 0) ? s->scale : 1;
    *gx = (wx - s->game_dst.x) / scale;
    *gy = (wy - s->game_dst.y) / scale;
}

/*
 * Where the game goes, and where the keys go.
 *
 * Recomputed whenever the window size is read rather than at setup, because the
 * window is resizable and a layout computed once would leave the buttons
 * floating over the wrong place after a resize.
 *
 * The game is scaled by the largest INTEGER factor that fits and centred, with
 * the leftover as bars: J2ME art is pixel art and a fractional scale turns
 * every sprite edge into a blur.
 */
static void sdl_layout(sdl_state *s)
{
    int win_w = 0, win_h = 0;
    int scale, bs, ks, gs, pad, gap;
    int dx, dy, fx, fy, kx, ky, gy, vy, cy;

    if (s->window) {
        SDL_GetWindowSize(s->window, &win_w, &win_h);
    }
    if (win_w <= 0 || win_h <= 0 || s->width <= 0 || s->height <= 0) {
        s->game_dst.x = s->game_dst.y = 0;
        s->game_dst.w = s->width;
        s->game_dst.h = s->height;
        s->btn_n = 0;
        s->speed_btn.x = s->speed_btn.y = s->speed_btn.w = s->speed_btn.h = 0;
        return;
    }

    /*
     * The game keeps the middle: the largest INTEGER scale that fits, centred,
     * with the leftover as bars.  Integer because J2ME art is pixel art and a
     * fractional scale turns every sprite edge into a blur.
     */
    scale = win_w / s->width;
    if (win_h / s->height < scale) { scale = win_h / s->height; }
    if (scale < 1) { scale = 1; }
    s->game_dst.w = s->width * scale;
    s->game_dst.h = s->height * scale;
    s->game_dst.x = (win_w - s->game_dst.w) / 2;
    s->game_dst.y = (win_h - s->game_dst.h) / 2;
    s->scale = scale;

    /*
     * Sizes for everything in the overlay, computed before the keypad's own
     * early return because the speed badge is not part of the keypad and stays
     * on screen when the keys are hidden.  Clamped rather than proportional
     * alone, so the keys stay tappable in a small window and do not turn into
     * billboards in a large one.
     */
    bs = win_h / 11;   if (bs > 56) { bs = 56; }   if (bs < 16) { bs = 16; }
    ks = bs * 9 / 10;  if (ks > 48) { ks = 48; }   if (ks < 14) { ks = 14; }
    gs = bs * 4 / 5;   if (gs > 44) { gs = 44; }   if (gs < 14) { gs = 14; }
    pad = bs / 4;      if (pad < 4) { pad = 4; }
    gap = pad;

    /*
     * The speed badge: a rounded plate at the top, between the two soft keys
     * (the corners and the middle of the top edge are taken), centred on the
     * window and vertically centred on the soft keys.  Small - it shows two or
     * three characters - but never smaller than a fingertip-sized 14px, and it
     * sits on the letterbox bar, not on the game.
     *
     * In a window too narrow for the middle the clamp below parks it over a
     * soft key, which is the least bad option: it is hit-tested first, so the
     * soft key loses those pixels rather than the badge being unreachable.
     */
    {
        int bh = bs * 2 / 3;
        if (bh > 36) { bh = 36; }
        if (bh < 14) { bh = 14; }

        s->speed_btn.w = bh * 2;
        s->speed_btn.h = bh;
        s->speed_btn.x = (win_w - s->speed_btn.w) / 2;
        s->speed_btn.y = pad + (bs - bh) / 2;
        if (s->speed_btn.x < 0) { s->speed_btn.x = 0; }
        if (s->speed_btn.y < 0) { s->speed_btn.y = 0; }
    }

    if (!s->keys_visible) {
        s->btn_n = 0;
        return;
    }

    /*
     * Three groups, laid out from the window size so they scale with it:
     *
     *   left   the D-pad cross, and the four game keys under it
     *   right  the twelve-key phone pad, CLEAR under it, volume/power above it
     *   top    the two soft keys
     */
    /* ---- left: the cross ---- */
    dx = pad;
    dy = win_h - pad - 3 * bs - gap - 2 * gs;
    s->btn[NJ_SDL2_BTN_UP].x    = dx + bs;  s->btn[NJ_SDL2_BTN_UP].y    = dy;
    s->btn[NJ_SDL2_BTN_LEFT].x  = dx;       s->btn[NJ_SDL2_BTN_LEFT].y  = dy + bs;
    s->btn[NJ_SDL2_BTN_RIGHT].x = dx + 2 * bs;
    s->btn[NJ_SDL2_BTN_RIGHT].y = dy + bs;
    s->btn[NJ_SDL2_BTN_DOWN].x  = dx + bs;  s->btn[NJ_SDL2_BTN_DOWN].y  = dy + 2 * bs;
    s->btn[NJ_SDL2_BTN_UP].w    = bs;  s->btn[NJ_SDL2_BTN_UP].h    = bs;
    s->btn[NJ_SDL2_BTN_DOWN].w  = bs;  s->btn[NJ_SDL2_BTN_DOWN].h  = bs;
    s->btn[NJ_SDL2_BTN_LEFT].w  = bs;  s->btn[NJ_SDL2_BTN_LEFT].h  = bs;
    s->btn[NJ_SDL2_BTN_RIGHT].w = bs;  s->btn[NJ_SDL2_BTN_RIGHT].h = bs;

    /*
     * FIRE goes in the space between the cross and the game, sized to FIT that
     * space rather than to a constant.
     *
     * The game is centred and takes the whole height, so the only room is the
     * side bars, and how wide they are depends on the window: a fixed 2*bs put
     * the button on top of the game at this window size and would put it in the
     * middle of nowhere at another.  Below `bs` it stops shrinking and starts
     * overlapping instead - a fire button you cannot hit is worse than one that
     * covers a few pixels of grass.
     */
    {
        int free_w = s->game_dst.x - (dx + 3 * bs) - gap;
        int fbs = free_w - gap;
        if (fbs > 2 * bs) { fbs = 2 * bs; }
        if (fbs < bs) { fbs = bs; }
        fx = dx + 3 * bs + gap + ((free_w - fbs) > 0 ? (free_w - fbs) / 2 : 0);
        fy = dy + (3 * bs - fbs) / 2;
        if (fy < dy) { fy = dy; }
        s->btn[NJ_SDL2_BTN_FIRE].x = fx;
        s->btn[NJ_SDL2_BTN_FIRE].y = fy;
        s->btn[NJ_SDL2_BTN_FIRE].w = fbs;
        s->btn[NJ_SDL2_BTN_FIRE].h = fbs;
    }

    /* ---- left, below the cross: A/B/C/D in a 2x2 ---- */
    gy = dy + 3 * bs + gap;
    s->btn[NJ_SDL2_BTN_A].x = dx;          s->btn[NJ_SDL2_BTN_A].y = gy;
    s->btn[NJ_SDL2_BTN_B].x = dx + gs;     s->btn[NJ_SDL2_BTN_B].y = gy;
    s->btn[NJ_SDL2_BTN_C].x = dx;          s->btn[NJ_SDL2_BTN_C].y = gy + gs;
    s->btn[NJ_SDL2_BTN_D].x = dx + gs;     s->btn[NJ_SDL2_BTN_D].y = gy + gs;
    s->btn[NJ_SDL2_BTN_A].w = gs; s->btn[NJ_SDL2_BTN_A].h = gs;
    s->btn[NJ_SDL2_BTN_B].w = gs; s->btn[NJ_SDL2_BTN_B].h = gs;
    s->btn[NJ_SDL2_BTN_C].w = gs; s->btn[NJ_SDL2_BTN_C].h = gs;
    s->btn[NJ_SDL2_BTN_D].w = gs; s->btn[NJ_SDL2_BTN_D].h = gs;

    /* ---- right: the twelve-key pad, 1-9 then *0# ---- */
    kx = win_w - pad - 3 * ks;
    cy = win_h - pad - gs;                 /* CLEAR sits on the bottom */
    ky = cy - gap - 4 * ks;                /* the pad above it */
    {
        static const int pad_btn[12] = {
            NJ_SDL2_BTN_1, NJ_SDL2_BTN_2, NJ_SDL2_BTN_3,
            NJ_SDL2_BTN_4, NJ_SDL2_BTN_5, NJ_SDL2_BTN_6,
            NJ_SDL2_BTN_7, NJ_SDL2_BTN_8, NJ_SDL2_BTN_9,
            NJ_SDL2_BTN_STAR, NJ_SDL2_BTN_0, NJ_SDL2_BTN_POUND
        };
        int n;
        for (n = 0; n < 12; n++) {
            s->btn[pad_btn[n]].x = kx + (n % 3) * ks;
            s->btn[pad_btn[n]].y = ky + (n / 3) * ks;
            s->btn[pad_btn[n]].w = ks;
            s->btn[pad_btn[n]].h = ks;
        }
    }
    s->btn[NJ_SDL2_BTN_CLEAR].x = kx;
    s->btn[NJ_SDL2_BTN_CLEAR].y = cy;
    s->btn[NJ_SDL2_BTN_CLEAR].w = 3 * ks;
    s->btn[NJ_SDL2_BTN_CLEAR].h = gs;

    /* Volume and power above the pad, out of the way of the digits. */
    vy = ky - gap - gs;
    s->btn[NJ_SDL2_BTN_VOLUP].x   = kx;
    s->btn[NJ_SDL2_BTN_VOLUP].y   = vy;
    s->btn[NJ_SDL2_BTN_VOLDOWN].x = kx + ks;
    s->btn[NJ_SDL2_BTN_VOLDOWN].y = vy;
    s->btn[NJ_SDL2_BTN_POWER].x   = kx + 2 * ks;
    s->btn[NJ_SDL2_BTN_POWER].y   = vy;
    s->btn[NJ_SDL2_BTN_VOLUP].w   = ks;
    s->btn[NJ_SDL2_BTN_VOLUP].h   = gs;
    s->btn[NJ_SDL2_BTN_VOLDOWN].w = ks;
    s->btn[NJ_SDL2_BTN_VOLDOWN].h = gs;
    s->btn[NJ_SDL2_BTN_POWER].w   = ks;
    s->btn[NJ_SDL2_BTN_POWER].h   = gs;

    /* ---- top: the soft keys, where a phone had them ---- */
    s->btn[NJ_SDL2_BTN_SOFT_L].x = pad;
    s->btn[NJ_SDL2_BTN_SOFT_L].y = pad;
    s->btn[NJ_SDL2_BTN_SOFT_R].x = win_w - pad - 2 * bs;
    s->btn[NJ_SDL2_BTN_SOFT_R].y = pad;
    s->btn[NJ_SDL2_BTN_SOFT_L].w = 2 * bs;
    s->btn[NJ_SDL2_BTN_SOFT_L].h = bs;
    s->btn[NJ_SDL2_BTN_SOFT_R].w = 2 * bs;
    s->btn[NJ_SDL2_BTN_SOFT_R].h = bs;

    s->btn_n = NJ_SDL2_NBTN;
}

/* A filled triangle drawn as horizontal spans.  Spans rather than
 * SDL_RenderGeometry because this has to work on the software renderer too, and
 * a shape made of one-pixel lines always does. */
static void fill_triangle(SDL_Renderer *r, int x, int y, int w, int h, int dir)
{
    int i;
    if (w <= 0 || h <= 0) {
        return;
    }
    if (dir == SDL2_DIR_UP || dir == SDL2_DIR_DOWN) {
        for (i = 0; i < h; i++) {
            int half = (w / 2) * i / (h > 1 ? h - 1 : 1);
            int yy = (dir == SDL2_DIR_UP) ? (y + i) : (y + h - 1 - i);
            SDL_RenderDrawLine(r, x + w / 2 - half, yy, x + w / 2 + half, yy);
        }
    } else {
        for (i = 0; i < w; i++) {
            int half = (h / 2) * i / (w > 1 ? w - 1 : 1);
            int xx = (dir == SDL2_DIR_LEFT) ? (x + i) : (x + w - 1 - i);
            SDL_RenderDrawLine(r, xx, y + h / 2 - half, xx, y + h / 2 + half);
        }
    }
}

static void fill_round(SDL_Renderer *r, int cx, int cy, int rad)
{
    int dy;
    if (rad < 1) {
        return;
    }
    for (dy = -rad; dy <= rad; dy++) {
        int dx = rad;
        while (dx > 0 && dx * dx + dy * dy > rad * rad) { dx--; }
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

/*
 * Two glyphs the shared font does not carry, kept here rather than added to
 * nj_sdl2_font.h because only this port's speed badge needs them: the decimal
 * point and the lowercase x of "1.5x".  Same 5x7 column-major encoding, bit 0
 * is the top row.
 */
static const sdl2_glyph sdl2_extra_font[] = {
    { '.', { 0x00, 0x00, 0x60, 0x60, 0x00 } },
    { 'x', { 0x44, 0x28, 0x10, 0x28, 0x44 } }
};
#define SDL2_EXTRA_FONT_N \
    ((int)(sizeof(sdl2_extra_font) / sizeof(sdl2_extra_font[0])))

/* The glyph for a character, or NULL when the font has none. */
static const sdl2_glyph *sdl_glyph(char ch)
{
    int k;
    for (k = 0; k < SDL2_FONT_N; k++) {
        if (sdl2_font[k].ch == ch) { return &sdl2_font[k]; }
    }
    for (k = 0; k < SDL2_EXTRA_FONT_N; k++) {
        if (sdl2_extra_font[k].ch == ch) { return &sdl2_extra_font[k]; }
    }
    return NULL;
}

/*
 * A label, centred, at the largest integer scale that fits the button.
 *
 * Integer for the same reason the game is: a 5x7 font scaled by 7.3 is a
 * smudge.  A label that cannot fit even at 1:1 is dropped rather than drawn
 * over the edge - a truncated "CLR" is worse than no label, because the key
 * beside it is then a guess.
 */
static void draw_label(SDL_Renderer *r, const SDL_Rect *b, const char *text)
{
    int len = 0;
    int sc, tw, th, ox, oy, i;
    const char *q;

    for (q = text; *q; q++) { len++; }
    if (len <= 0) {
        return;
    }
    sc = (b->w - 6) / (SDL2_GLYPH_W * len + (len - 1));
    if ((b->h - 6) / SDL2_GLYPH_H < sc) { sc = (b->h - 6) / SDL2_GLYPH_H; }
    if (sc < 1) {
        return;
    }
    tw = (SDL2_GLYPH_W * len + (len - 1)) * sc;
    th = SDL2_GLYPH_H * sc;
    ox = b->x + (b->w - tw) / 2;
    oy = b->y + (b->h - th) / 2;

    for (i = 0; i < len; i++) {
        const sdl2_glyph *g = sdl_glyph(text[i]);
        if (g) {
            int col, row;
            for (col = 0; col < SDL2_GLYPH_W; col++) {
                for (row = 0; row < SDL2_GLYPH_H; row++) {
                    if ((g->col[col] & (1u << row)) != 0u) {
                        SDL_Rect p;
                        p.x = ox + (i * (SDL2_GLYPH_W + 1) + col) * sc;
                        p.y = oy + row * sc;
                        p.w = sc;
                        p.h = sc;
                        SDL_RenderFillRect(r, &p);
                    }
                }
            }
        }
    }
}

static void sdl_draw_keys(sdl_state *s)
{
    int i;

    if (s->btn_n <= 0) {
        return;
    }
    SDL_SetRenderDrawBlendMode(s->renderer, SDL_BLENDMODE_BLEND);

    for (i = 0; i < s->btn_n; i++) {
        const SDL_Rect *b = &s->btn[i];
        const sdl2_key_def *k = &sdl2_keys[i];
        int held = (s->btn_held == i);

        /* Translucent, because these sit on top of somebody's game. */
        SDL_SetRenderDrawColor(s->renderer, 250, 250, 250,
                               (Uint8)(held ? 200 : 96));
        if (k->shape == NJ_SDL2_SHAPE_DOT) {
            fill_round(s->renderer, b->x + b->w / 2, b->y + b->h / 2,
                       b->w / 2 - 2);
        } else {
            SDL_RenderFillRect(s->renderer, b);
        }

        SDL_SetRenderDrawColor(s->renderer, 25, 25, 25,
                               (Uint8)(held ? 255 : 170));
        if (k->shape == NJ_SDL2_SHAPE_DOT) {
            fill_round(s->renderer, b->x + b->w / 2, b->y + b->h / 2,
                       b->w / 2 - 3);
            SDL_SetRenderDrawColor(s->renderer, 250, 250, 250, 180);
            fill_round(s->renderer, b->x + b->w / 2, b->y + b->h / 2,
                       b->w / 2 - 7);
        } else {
            SDL_RenderDrawRect(s->renderer, b);
        }

        SDL_SetRenderDrawColor(s->renderer, 20, 20, 20, 235);
        if (k->shape == NJ_SDL2_SHAPE_TRI) {
            fill_triangle(s->renderer, b->x + b->w / 4, b->y + b->h / 4,
                          b->w / 2, b->h / 2, k->arrow);
        } else if (k->shape == NJ_SDL2_SHAPE_DOT) {
            fill_round(s->renderer, b->x + b->w / 2, b->y + b->h / 2,
                       b->w / 5);
        } else if (k->label) {
            draw_label(s->renderer, b, k->label);
        }
    }
    SDL_SetRenderDrawBlendMode(s->renderer, SDL_BLENDMODE_NONE);
}

/* Which button is at this window point, or -1. */
static int sdl_key_at(sdl_state *s, int x, int y)
{
    int i;
    for (i = 0; i < s->btn_n; i++) {
        const SDL_Rect *b = &s->btn[i];
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) {
            /* The fire button is round; the corners of its box are not it. */
            if (sdl2_keys[i].shape == NJ_SDL2_SHAPE_DOT) {
                int cx = b->x + b->w / 2 - x;
                int cy = b->y + b->h / 2 - y;
                int rad = b->w / 2 - 2;
                if (cx * cx + cy * cy > rad * rad) {
                    continue;
                }
            }
            return i;
        }
    }
    return -1;
}

/*
 * The vertical half-height of a rounded rect at column x: full radius along the
 * straight middle, shrinking over the two caps.  Integer maths, the same trick
 * fill_round() uses, so no fpu or libm is needed.
 */
static int round_half(int x, int lc, int rc, int rad)
{
    int dx = 0;
    int dy = rad;

    if (x < lc) { dx = lc - x; }
    else if (x > rc) { dx = x - rc; }
    while (dy > 0 && dx * dx + dy * dy > rad * rad) { dy--; }
    return dy;
}

/*
 * A rounded rect, filled.  Drawn one pixel column at a time rather than as a
 * middle rect plus two circles: the plate is translucent, and overlapping
 * shapes would blend twice and show seams through the badge.
 */
static void fill_rounded(SDL_Renderer *r, const SDL_Rect *b, int rad)
{
    SDL_Rect col;
    int i, cy, lc, rc;

    if (rad < 1 || b->w < 4 || b->h < 4) {
        SDL_RenderFillRect(r, b);
        return;
    }
    if (rad > b->w / 2) { rad = b->w / 2; }
    if (rad > b->h / 2) { rad = b->h / 2; }

    cy = b->y + b->h / 2;
    lc = b->x + rad;                    /* centre column of the left cap  */
    rc = b->x + b->w - 1 - rad;         /* centre column of the right cap */

    for (i = 0; i < b->w; i++) {
        int dy = round_half(b->x + i, lc, rc, rad);
        col.x = b->x + i;
        col.y = cy - dy;
        col.w = 1;
        col.h = dy * 2 + 1;
        SDL_RenderFillRect(r, &col);
    }
}

/* The outline of the same shape: the same spans, top and bottom pixel only. */
static void draw_round_rect(SDL_Renderer *r, const SDL_Rect *b, int rad)
{
    int i, cy, lc, rc;

    if (rad < 1 || b->w < 4 || b->h < 4) {
        SDL_RenderDrawRect(r, b);
        return;
    }
    if (rad > b->w / 2) { rad = b->w / 2; }
    if (rad > b->h / 2) { rad = b->h / 2; }

    cy = b->y + b->h / 2;
    lc = b->x + rad;
    rc = b->x + b->w - 1 - rad;

    for (i = 0; i < b->w; i++) {
        int dy = round_half(b->x + i, lc, rc, rad);
        SDL_RenderDrawPoint(r, b->x + i, cy - dy);
        SDL_RenderDrawPoint(r, b->x + i, cy + dy);
    }
    if (rad < b->h / 2) {               /* corners smaller than half the height */
        SDL_RenderDrawLine(r, b->x, cy - rad, b->x, cy + rad);
        SDL_RenderDrawLine(r, b->x + b->w - 1, cy - rad, b->x + b->w - 1, cy + rad);
    }
}

/* "1.0x", "1.5x", ... from a percentage. */
static void sdl_speed_label(int percent, char *out, int out_n)
{
    if (percent < 0) { percent = 0; }
    SDL_snprintf(out, (size_t)out_n, "%d.%dx",
                 percent / 100, (percent % 100) / 10);
}

/*
 * The speed badge.  Drawn exactly like a keycap - translucent plate, dark
 * outline, dark label - because it is one more thing lying on somebody's game,
 * and it is the one overlay control that stays when the keypad is hidden.
 * Disabled (greyed) until the VM has been captured, which only happens if a
 * MIDlet is actually running.
 */
static void sdl_draw_speed(sdl_state *s)
{
    const SDL_Rect *b = &s->speed_btn;
    char label[16];
    int held = s->speed_held;
    int rad;
    Uint8 alpha;

    if (b->w <= 0 || b->h <= 0) {
        return;
    }
    rad = b->h / 2;
    alpha = (Uint8)(s->vm ? (held ? 200 : 96) : 48);

    SDL_SetRenderDrawBlendMode(s->renderer, SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(s->renderer, 250, 250, 250, alpha);
    fill_rounded(s->renderer, b, rad);

    SDL_SetRenderDrawColor(s->renderer, 25, 25, 25,
                           (Uint8)(s->vm ? (held ? 255 : 170) : 120));
    draw_round_rect(s->renderer, b, rad);

    sdl_speed_label(s->speed_percent, label, (int)sizeof(label));
    SDL_SetRenderDrawColor(s->renderer, 20, 20, 20,
                           (Uint8)(s->vm ? 235 : 140));
    draw_label(s->renderer, b, label);

    SDL_SetRenderDrawBlendMode(s->renderer, SDL_BLENDMODE_NONE);
}

/*
 * Is this window point on the badge?  Tested before the keypad and before the
 * game area: a click on the badge is neither a keypress nor a pointer event,
 * and a click anywhere else is not the badge.
 */
static int sdl_speed_at(sdl_state *s, int x, int y)
{
    const SDL_Rect *b = &s->speed_btn;
    return b->w > 0 && b->h > 0 &&
           x >= b->x && x < b->x + b->w &&
           y >= b->y && y < b->y + b->h;
}

/* 1.0x -> 1.5x -> 2.0x -> 3.0x -> 0.5x -> 1.0x.  Only sets state: the frame
 * applies it (see sdl_input_poll). */
static void sdl_speed_cycle(sdl_state *s)
{
    int i, next = 0;

    for (i = 0; i < SDL2_NSPEEDS; i++) {
        if (sdl2_speeds[i] == s->speed_percent) {
            next = (i + 1) % SDL2_NSPEEDS;
            break;
        }
    }
    s->speed_percent = sdl2_speeds[next];
}

/* Let go of the cap the pointer is holding, if it is holding one. */
static void sdl_release_held(sdl_state *s)
{
    if (s->btn_held >= 0) {
        sdl_push_key(s, sdl2_keys[s->btn_held].key, 0);
        s->btn_held = -1;
    }
}

/*
 * Move the press that is in progress to wherever the pointer is now.
 *
 * Called ONLY while a press is in progress - the mouse button is down, or a
 * finger is on the panel - because a press is what makes the pointer mean
 * something.  A mouse sweeping across the window with no button held is not a
 * finger and must press nothing; that is the whole difference between this and
 * a hover.
 *
 * While a press is in progress the position decides which cap is down: sliding
 * from one cap to the next releases the first and presses the second, moving
 * off onto empty space releases it, and the speed badge takes priority over
 * whatever cap is under it.  The game is not consulted here at all - only caps
 * are hit-tested, so a drag crossing the picture stays silent unless it is the
 * game's own drag (see `game_drag`).
 */
static void sdl_pointer_to(sdl_state *s, int x, int y)
{
    int want = sdl_speed_at(s, x, y) ? -1 : sdl_key_at(s, x, y);

    if (want == s->btn_held) {
        return;             /* already pressed, or already nothing: no events */
    }
    sdl_release_held(s);
    if (want >= 0) {
        sdl_push_key(s, sdl2_keys[want].key, 1);
        s->btn_held = want;
    }
}

static void sdl_display_present(void *ud, int x, int y, int w, int h)
{
    sdl_state *s = SS(ud);
    SDL_Rect r;
    SDL_Event ev;

    /* Pump the window system here: present() is called exactly once per frame
     * and is the natural place to do it, whatever the port. */
    while (SDL_PollEvent(&ev)) {
        nj_event e;
        memset(&e, 0, sizeof(e));
        if (getenv("NJ_SDL2_EVDBG")) {
            if (ev.type == SDL_MOUSEMOTION) {
                SDL_Log("EV motion state=%d xy=%d,%d", (int)ev.motion.state,
                        ev.motion.x, ev.motion.y);
            } else if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP) {
                SDL_Log("EV button type=%u xy=%d,%d", (unsigned)ev.type,
                        ev.button.x, ev.button.y);
            }
        }
        switch (ev.type) {
        case SDL_QUIT:
            e.type = NJ_EV_QUIT;
            s->queue[s->qt] = e;
            s->qt = (s->qt + 1) % (int)(sizeof(s->queue) / sizeof(s->queue[0]));
            s->quit = 1;
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            int k = -1;

            /*
             * F12 or Tab shows and hides the on-screen keys.  Not F1: that is
             * already the soft-left key, and a control that both does something
             * and undoes it is worse than no control.
             */
            if ((ev.key.keysym.sym == SDLK_F12 || ev.key.keysym.sym == SDLK_TAB)) {
                if (ev.type == SDL_KEYDOWN && ev.key.repeat == 0) {
                    s->keys_visible = !s->keys_visible;
                    if (!s->keys_visible) {
                        /* Hiding them while one is held would leave the key
                         * stuck down, because the release would never come. */
                        sdl_release_held(s);
                    }
                    sdl_layout(s);
                }
                break;
            }
            /*
             * F3 cycles the speed badge.  Not F2: that is the soft-right key,
             * and an overlay hotkey that eats a key the game wants is a bug.
             */
            if (ev.type == SDL_KEYDOWN && ev.key.repeat == 0 &&
                ev.key.keysym.sym == SDLK_F3) {
                sdl_speed_cycle(s);
                break;
            }
            /*
             * F5 asks for a save-state snapshot.  It only sets a flag: the
             * snapshot itself is taken by input_poll, at the top of a frame, on
             * the VM's own thread.
             */
            if (ev.type == SDL_KEYDOWN && ev.key.repeat == 0 &&
                ev.key.keysym.sym == SDLK_F5) {
                s->save_req = 1;
                break;
            }
            switch (ev.key.keysym.sym) {
            case SDLK_UP:     k = NJ_KEY_UP;     break;
            case SDLK_DOWN:   k = NJ_KEY_DOWN;   break;
            case SDLK_LEFT:   k = NJ_KEY_LEFT;   break;
            case SDLK_RIGHT:  k = NJ_KEY_RIGHT;  break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
            case SDLK_SPACE:  k = NJ_KEY_FIRE;   break;
            case SDLK_ESCAPE: k = NJ_KEY_SOFT_RIGHT; break;
            case SDLK_F1:     k = NJ_KEY_SOFT_LEFT;  break;
            case SDLK_F2:     k = NJ_KEY_SOFT_RIGHT; break;
            case SDLK_BACKSPACE: k = NJ_KEY_CLEAR;   break;
            case SDLK_ASTERISK:  k = NJ_KEY_STAR;    break;
            case SDLK_HASH:      k = NJ_KEY_POUND;   break;
            case SDLK_z:      k = NJ_KEY_GAME_A; break;
            case SDLK_x:      k = NJ_KEY_GAME_B; break;
            case SDLK_c:      k = NJ_KEY_GAME_C; break;
            case SDLK_v:      k = NJ_KEY_GAME_D; break;
            case SDLK_0: k = NJ_KEY_0; break;
            case SDLK_1: k = NJ_KEY_1; break;
            case SDLK_2: k = NJ_KEY_2; break;
            case SDLK_3: k = NJ_KEY_3; break;
            case SDLK_4: k = NJ_KEY_4; break;
            case SDLK_5: k = NJ_KEY_5; break;
            case SDLK_6: k = NJ_KEY_6; break;
            case SDLK_7: k = NJ_KEY_7; break;
            case SDLK_8: k = NJ_KEY_8; break;
            case SDLK_9: k = NJ_KEY_9; break;
            default: break;
            }
            if (k >= 0) {
                e.type = (ev.type == SDL_KEYDOWN) ? NJ_EV_KEY_DOWN
                                                  : NJ_EV_KEY_UP;
                e.key = (nj_u1)k;
                s->queue[s->qt] = e;
                s->qt = (s->qt + 1) %
                        (int)(sizeof(s->queue) / sizeof(s->queue[0]));
            }
            break;
        }
        /*
         * Pointer input.
         *
         * There was none: the port could push an event into its own queue but
         * nothing ever came out of the window, so on a desktop build a MIDlet
         * with a pointer-driven UI was simply dead to the mouse, and on a
         * touchscreen the only input that worked was the keyboard mapping.
         *
         * Both sources are accepted.  A laptop has a mouse, a phone has a
         * touchscreen, and SDL reports them as different event types that mean
         * the same thing to a MIDlet.
         */
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (ev.button.button == SDL_BUTTON_LEFT) {
                /*
                 * A press is what makes the pointer mean anything, so the button
                 * going down is where the keypad wakes up: the badge first (it
                 * is a control, not a key and not a click), then the cap under
                 * the press, and only then the game.
                 *
                 * While the button stays down, motion carries the press from cap
                 * to cap (sdl_pointer_to).  Letting go releases whatever cap is
                 * still held and ends the MIDlet's drag if it had one; a press
                 * that landed on a cap was never a pointer press, so the MIDlet
                 * never hears about it at all.
                 */
                if (ev.type == SDL_MOUSEBUTTONDOWN) {
                    if (sdl_speed_at(s, ev.button.x, ev.button.y)) {
                        sdl_speed_cycle(s);
                        s->speed_held = 1;
                        break;
                    }
                    sdl_pointer_to(s, ev.button.x, ev.button.y);
                    if (s->btn_held >= 0) {
                        /* A press that lands on a cap is a key press, not a
                         * pointer press - otherwise every tap on the D-pad
                         * would ALSO be reported to a MIDlet as a click at that
                         * spot, and a game with a pointer UI would react to
                         * both. */
                        break;
                    }
                    s->game_drag = 1;
                    {
                        int gx, gy;
                        sdl_to_game(s, ev.button.x, ev.button.y, &gx, &gy);
                        sdl_push_pointer(s, gx, gy, NJ_EV_POINTER_DOWN);
                    }
                } else {
                    s->speed_held = 0;
                    sdl_release_held(s);
                    if (!s->game_drag) {
                        break;      /* the press never reached the game */
                    }
                    s->game_drag = 0;
                    {
                        int gx, gy;
                        sdl_to_game(s, ev.button.x, ev.button.y, &gx, &gy);
                        sdl_push_pointer(s, gx, gy, NJ_EV_POINTER_UP);
                    }
                }
            }
            break;
        case SDL_MOUSEMOTION:
            /*
             * Nothing at all while the button is up.  A mouse crossing the
             * window is not a finger on a panel: pressing caps nobody touched
             * would be bad enough, and reporting the moves would give every
             * MIDlet a stream of phantom drags.
             *
             * With the button down the press follows the pointer - onto a cap,
             * off a cap, from one cap to the next, and back into the game area
             * for the drag the press started there.
             */
            if ((ev.motion.state & SDL_BUTTON_LMASK) == 0) {
                break;
            }
            if (s->speed_held) {
                if (sdl_speed_at(s, ev.motion.x, ev.motion.y)) {
                    break;                  /* still on the badge */
                }
                s->speed_held = 0;          /* dragged off it: press over */
            }
            sdl_pointer_to(s, ev.motion.x, ev.motion.y);
            if (s->btn_held >= 0) {
                break;              /* this press belongs to the keypad */
            }
            if (s->game_drag) {
                int gx, gy;
                sdl_to_game(s, ev.motion.x, ev.motion.y, &gx, &gy);
                sdl_push_pointer(s, gx, gy, NJ_EV_POINTER_MOVE);
            }
            break;
        case SDL_WINDOWEVENT:
            /*
             * The pointer left the window, so nothing is under it any more and
             * nothing may stay pressed: without this a cap held down by a drag
             * that wandered out of the window would stay down until the pointer
             * came back and crossed it again, which is a stuck key.
             */
            if (ev.window.event == SDL_WINDOWEVENT_LEAVE) {
                sdl_release_held(s);
                s->speed_held = 0;
            }
            break;
        /*
         * Touch.  tfinger coordinates are normalised to 0..1 over the window,
         * so multiplying by the screen size gives device pixels directly - no
         * scale factor, because the normalisation already removed it.
         */
        case SDL_FINGERDOWN:
        case SDL_FINGERUP:
        case SDL_FINGERMOTION: {
            /*
             * tfinger coordinates are normalised 0..1 over the WINDOW, so they
             * have to come back to window pixels before the on-screen keys can
             * be hit-tested, and then through the letterbox to reach the game.
             */
            int ww = 0, wh = 0;
            int wx, wy, gx, gy;
            SDL_GetWindowSize(s->window, &ww, &wh);
            wx = (int)(ev.tfinger.x * (float)ww);
            wy = (int)(ev.tfinger.y * (float)wh);

            /*
             * A finger is lifted, so it is holding nothing any more: this is
             * where touch differs from the mouse, which keeps its cap pressed
             * while the pointer stays on it.  The MIDlet is only told about a
             * release if it was told about the press (game_drag).
             */
            if (ev.type == SDL_FINGERUP) {
                s->speed_held = 0;
                sdl_release_held(s);
                if (!s->game_drag) {
                    break;
                }
                s->game_drag = 0;
                sdl_to_game(s, wx, wy, &gx, &gy);
                sdl_push_pointer(s, gx, gy, NJ_EV_POINTER_UP);
                break;
            }

            if (ev.type == SDL_FINGERDOWN && sdl_speed_at(s, wx, wy)) {
                sdl_speed_cycle(s);
                s->speed_held = 1;
                break;
            }
            if (s->speed_held) {
                if (sdl_speed_at(s, wx, wy)) {
                    break;                  /* still on the badge */
                }
                s->speed_held = 0;          /* slid off it: press over */
            }

            /* A finger that is down is a press in progress, so the position
             * decides which cap it holds - exactly as for the mouse. */
            sdl_pointer_to(s, wx, wy);
            if (s->btn_held >= 0) {
                /* Sliding from cap to cap presses and releases as it goes; a
                 * finger resting on one is not on the game. */
                break;
            }
            sdl_to_game(s, wx, wy, &gx, &gy);
            if (ev.type == SDL_FINGERDOWN) {
                s->game_drag = 1;
                sdl_push_pointer(s, gx, gy, NJ_EV_POINTER_DOWN);
            } else if (s->game_drag) {
                sdl_push_pointer(s, gx, gy, NJ_EV_POINTER_MOVE);
            }
            break;
        }
        default:
            break;
        }
    }

    if (s->quit) {
        return;
    }

    /* The runtime may report a sub-rectangle; updating only that region keeps
     * the bus quiet on a real panel and costs nothing here. */
    r.x = x; r.y = y; r.w = w; r.h = h;
    if (r.w <= 0 || r.h <= 0) {
        r.x = 0; r.y = 0; r.w = s->width; r.h = s->height;
    }
    SDL_UpdateTexture(s->texture, &r,
                      (const nj_u1 *)s->pixels + (size_t)r.y * (size_t)s->stride
                          + (size_t)r.x * (size_t)sdl_bytes_per_pixel(s->format),
                      s->stride);
    sdl_layout(s);
    SDL_RenderClear(s->renderer);
    SDL_RenderCopy(s->renderer, s->texture, NULL, &s->game_dst);
    sdl_draw_keys(s);
    sdl_draw_speed(s);

    /*
     * Optional proof-of-presentation capture.
     *
     * NJ_SDL2_CAPTURE=<prefix> reads the renderer's own backbuffer back after
     * the copy and writes <prefix>N.ppm.  This is the ONLY way to verify the
     * window path without a person looking at it: it covers the whole real
     * chain - texture upload, renderer, backbuffer - rather than the software
     * framebuffer the runtime already knows about, and it works headless, on
     * Wayland, over SSH and inside CI, where grabbing the screen does not.
     *
     * It is a debugging and test facility, so it costs one getenv at startup
     * and nothing at all when unset.
     */
    if (s->capture && s->capture[0]) {
        int cw = 0, ch = 0;
        SDL_GetRendererOutputSize(s->renderer, &cw, &ch);
        if (cw > 0 && ch > 0) {
            nj_u1 *shot = (nj_u1 *)SDL_malloc((size_t)cw * (size_t)ch * 3u);
            if (shot) {
                if (SDL_RenderReadPixels(s->renderer, NULL, SDL_PIXELFORMAT_RGB24,
                                         shot, cw * 3) == 0) {
                    char path[512];
                    char name[600];
                    int n = 0;
                    const char *q = s->capture;
                    while (*q && n < (int)sizeof(name) - 12) { name[n++] = *q++; }
                    name[n] = '\0';
                    SDL_snprintf(path, sizeof(path), "%s%05d.ppm", name,
                                 s->capture_n++);
                    {
                        SDL_RWops *f = SDL_RWFromFile(path, "wb");
                        if (f) {
                            char hdr[64];
                            int hn = SDL_snprintf(hdr, sizeof(hdr),
                                                  "P6\n%d %d\n255\n", cw, ch);
                            SDL_RWwrite(f, hdr, 1, (size_t)hn);
                            SDL_RWwrite(f, shot, 1,
                                        (size_t)cw * (size_t)ch * 3u);
                            SDL_RWclose(f);
                        }
                    }
                }
                SDL_free(shot);
            }
        }
        s->capture_every--;
        if (s->capture_every <= 0) {
            s->capture = NULL;   /* one frame is the proof; stop paying for it */
        }
    }

    SDL_RenderPresent(s->renderer);
}

/* ------------------------------------------------------------------ */
/* Audio                                                               */
/* ------------------------------------------------------------------ */
/*
 * Tones are SYNTHESISED here rather than handed to a MIDI device, because a
 * J2ME tone sequence is a set of square-wave beeps and that is exactly what a
 * 2004 phone did with it.  A square wave is what the format sounds like; a sine
 * would be prettier and wrong.
 *
 * The output is queued, never blocking: SDL_QueueAudio takes a copy and returns,
 * so a tone sequence that lasts a second does not stop the frame loop for a
 * second.  pcm_free() reports the queue depth so the runtime can throttle a
 * long WAV instead of growing an unbounded buffer.
 */
#define SDL_TONE_AMP 7000

static int sdl_audio_ensure(sdl_state *s, int rate, int chans)
{
    SDL_AudioSpec want;
    SDL_AudioSpec have;

    if (s->audio && s->audio_rate == rate && s->audio_chans == chans) {
        return 1;
    }
    if (s->audio) {
        SDL_CloseAudioDevice(s->audio);
        s->audio = 0;
    }
    SDL_zero(want);
    want.freq = rate;
    want.format = AUDIO_S16SYS;
    want.channels = (Uint8)chans;
    want.samples = 1024;
    want.callback = NULL;              /* queued, not pulled */
    s->audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!s->audio) {
        return 0;
    }
    s->audio_rate = (int)have.freq;
    s->audio_chans = (int)have.channels;
    SDL_PauseAudioDevice(s->audio, 0);

    /*
     * Say so, ONCE per device.  "The game is silent" and "the audio device
     * never opened" are indistinguishable from outside, and the difference is
     * the whole of this section - so a port that opens a device announces it,
     * with the format it actually got rather than the one it asked for.
     */
    {
        char b[160];
        SDL_snprintf(b, sizeof(b), "audio: %s opened, %d Hz, %d channel(s)",
                     SDL_GetCurrentAudioDriver(), (int)have.freq,
                     (int)have.channels);
        if (s->port && s->port->log) {
            s->port->log(s->port->user, 2, b);
        }
    }
    return 1;
}

static int sdl_tone_play(void *ud, const nj_audio_tone *tones, int count)
{
    sdl_state *s = SS(ud);
    int rate = 22050;
    int i;
    nj_u4 total = 0u;
    nj_s2 *buf;
    nj_u4 at = 0u;

    if (!s || !tones || count <= 0) {
        return -1;
    }
    if (!sdl_audio_ensure(s, rate, 1)) {
        return -1;                     /* no device: not an error for the game */
    }
    for (i = 0; i < count; i++) {
        total += (nj_u4)tones[i].duration_ms * (nj_u4)rate / 1000u;
    }
    if (total == 0u) {
        return 0;
    }
    buf = (nj_s2 *)SDL_malloc((size_t)total * sizeof(nj_s2));
    if (!buf) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        nj_u4 n = (nj_u4)tones[i].duration_ms * (nj_u4)rate / 1000u;
        nj_u4 k;
        if (tones[i].freq_hz == 0u) {
            for (k = 0; k < n; k++) {
                buf[at++] = 0;         /* a rest is silence, not a low note */
            }
            continue;
        }
        for (k = 0; k < n; k++) {
            /*
             * Square, with the period in samples as a running phase rather than
             * a float: a part with no FPU must be able to play a jingle, and an
             * integer phase accumulator costs nothing to reason about.
             */
            nj_u4 half = (nj_u4)rate / ((nj_u4)tones[i].freq_hz * 2u);
            if (half == 0u) {
                half = 1u;
            }
            buf[at++] = (((k / half) & 1u) != 0u) ? SDL_TONE_AMP : -SDL_TONE_AMP;
        }
    }
    SDL_QueueAudio(s->audio, buf, total * (nj_u4)sizeof(nj_s2));
    SDL_free(buf);
    return 0;
}

static void sdl_tone_stop(void *ud)
{
    sdl_state *s = SS(ud);
    if (s && s->audio) {
        SDL_ClearQueuedAudio(s->audio);
    }
}

static int sdl_pcm_open(void *ud, int sample_rate, int channels)
{
    sdl_state *s = SS(ud);
    if (!s || sample_rate <= 0 || channels <= 0) {
        return -1;
    }
    return sdl_audio_ensure(s, sample_rate, channels) ? 0 : -1;
}

static int sdl_pcm_write(void *ud, const nj_s2 *samples, int frames)
{
    sdl_state *s = SS(ud);
    if (!s || !s->audio || !samples || frames <= 0) {
        return -1;
    }
    SDL_QueueAudio(s->audio, samples,
                   (Uint32)frames * (Uint32)s->audio_chans
                       * (Uint32)sizeof(nj_s2));
    return frames;
}

static void sdl_pcm_close(void *ud)
{
    sdl_state *s = SS(ud);
    if (s && s->audio) {
        SDL_CloseAudioDevice(s->audio);
        s->audio = 0;
    }
}

/*
 * Frames the device will take right now.  A quarter of a second of queue is
 * plenty: less and the device underruns on a slow frame, more and a stop()
 * leaves a second of sound playing after the game asked it to stop.
 */
static int sdl_pcm_free(void *ud)
{
    sdl_state *s = SS(ud);
    nj_u4 queued;
    nj_u4 cap;
    if (!s || !s->audio || s->audio_chans <= 0) {
        return 0;
    }
    queued = SDL_GetQueuedAudioSize(s->audio);
    cap = (nj_u4)s->audio_rate * (nj_u4)s->audio_chans
              * (nj_u4)sizeof(nj_s2) / 4u;
    if (queued >= cap) {
        return 0;
    }
    return (int)((cap - queued) / ((nj_u4)s->audio_chans * sizeof(nj_s2)));
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */
/*
 * Inject a synthetic event, so --keys works here as it does headless.
 *
 * The queue and the polling side already existed; only the way IN was missing,
 * which meant a scripted run against this port silently ignored every key.  A
 * port that draws a window is the one a person actually watches, so that is
 * exactly where "the keys did nothing" is hardest to diagnose.
 */
static int sdl_input_push(void *ud, const nj_event *ev)
{
    sdl_state *s = SS(ud);
    int next;

    if (!s || !ev) {
        return 0;
    }
    next = (s->qt + 1) % (int)(sizeof(s->queue) / sizeof(s->queue[0]));
    if (next == s->qh) {
        return 0;               /* full: drop rather than overwrite a real key */
    }
    s->queue[s->qt] = *ev;
    s->qt = next;
    return 1;
}

/*
 * Take the snapshot the F5 key asked for.
 *
 * Called from sdl_input_poll - the VM's own thread, at the top of a frame, with
 * the object arena, every Java stack and the scheduler in a state the blob can
 * record and no native call in flight.  Loading is deliberately not offered:
 * a snapshot has to be restored BEFORE the MIDlet is constructed, which only
 * the runner can do (--load-state), never a hotkey in a running VM.
 */
static void sdl_save_state(sdl_state *s)
{
    char msg[256];
    int rc;

    if (!s->vm) {
        sdl_log(s, 1, "state: no VM yet, nothing to save");
        return;
    }
    if (!s->state_path || !s->state_path[0]) {
        sdl_log(s, 1, "state: set NJ_SDL2_STATE=<file> to use the F5 snapshot "
                      "(the port is not told the JAR path, so it cannot choose "
                      "one for you)");
        return;
    }
    rc = nj_vm_can_save(s->vm) ? nj_vm_save_file(s->vm, s->state_path)
                               : NJ_ERR_STATE;
    SDL_snprintf(msg, sizeof(msg), "state: save to %s -> %d%s",
                 s->state_path, rc,
                 rc == NJ_OK ? "  (resume it with --load-state)" : "");
    sdl_log(s, rc == NJ_OK ? 2 : 1, msg);
}

static int sdl_input_poll(void *ud, nj_event *ev)
{
    sdl_state *s = SS(ud);

    /*
     * Port settings the overlay asked for, applied HERE and nowhere else.
     *
     * input_poll is the one hook that runs at the top of every frame, on the
     * VM's own thread; the click or the keypress that requested the change came
     * out of SDL_PollEvent during presentation, which is a different moment in
     * the frame and (on a port with a UI thread) a different thread.  So the UI
     * writes a number, and this function is the only place the VM is touched.
     */
    if (s->vm && s->speed_percent > 0 && s->speed_percent != s->speed_applied) {
        nj_vm_set_speed(s->vm, s->speed_percent);
        s->speed_applied = s->speed_percent;
        /*
         * Logged because the multiplier is invisible in a screenshot: the frame
         * counter is what the port can draw, not what the MIDlet's clock says.
         * This line is the evidence that the game really sped up.
         */
        {
            char msg[96];
            SDL_snprintf(msg, sizeof(msg), "speed: MIDlet time runs at %d%%",
                         s->speed_percent);
            sdl_log(s, 2, msg);
        }
    }
    if (s->save_req) {
        s->save_req = 0;
        sdl_save_state(s);
    }

    if (s->qh == s->qt) {
        return 0;
    }
    *ev = s->queue[s->qh];
    s->qh = (s->qh + 1) % (int)(sizeof(s->queue) / sizeof(s->queue[0]));
    return 1;
}

/*
 * install_codecs(): the one hook that is handed the VM once it exists.
 *
 * This port installs no codecs of its own - the SDL2 build has no platform
 * image or audio decoder to register - but the hook is still the only chance to
 * remember the VM, and both the speed badge and the F5 snapshot need it.
 * speed_applied is cleared so the first frame pushes the speed this port was
 * configured with instead of assuming the VM's default.
 */
static void sdl_install_codecs(void *ud, nj_vm *vm)
{
    sdl_state *s = SS(ud);

    if (!s || !vm) {
        return;
    }
    s->vm = vm;
    s->speed_applied = 0;
}

static void sdl_shutdown(void *ud)
{
    sdl_state *s = SS(ud);

    if (!s) {
        return;
    }
    if (s->texture) {
        SDL_DestroyTexture(s->texture);
    }
    if (s->renderer) {
        SDL_DestroyRenderer(s->renderer);
    }
    if (s->window) {
        SDL_DestroyWindow(s->window);
    }
    free(s->pixels);
    free(s);
    SDL_Quit();
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */
nj_status nj_port_sdl2(nj_port *out, const char *title)
{
    sdl_state *s;

    if (!out) {
        return NJ_ERR;
    }
    (void)title;
    /* SDL_INIT_AUDIO is requested but not required: a machine with no sound
     * card must still get a window, and the port reports the failure by leaving
     * the audio hooks to return -1. */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0
        && SDL_Init(SDL_INIT_VIDEO) != 0) {
        return NJ_ERR;
    }
    s = (sdl_state *)calloc(1u, sizeof(*s));
    if (!s) {
        SDL_Quit();
        return NJ_ERR_NOMEM;
    }
    s->scale = 0;                 /* auto: largest integer fit */
    /*
     * On-screen keys: on by default, because the point of them is the user who
     * has not memorised the keyboard map.  NJ_SDL2_KEYS=0 starts them hidden,
     * and F12 or Tab toggles at run time.
     */
    {
        const char *kv = SDL_getenv("NJ_SDL2_KEYS");
        s->keys_visible = 1;
        if (kv && kv[0] == '0') {
            s->keys_visible = 0;
        }
    }
    s->btn_held = -1;

    /*
     * The speed badge.  There is no VM yet - install_codecs() is where that
     * arrives - so this only records what to ask for; the first frame applies
     * it.  NJ_SDL2_SPEED=<percent> starts at a chosen speed, clamped the same
     * way nj_vm_set_speed() clamps it.
     */
    {
        const char *sv = SDL_getenv("NJ_SDL2_SPEED");
        s->speed_percent = 100;
        if (sv && *sv) {
            int p = SDL_atoi(sv);
            if (p < 10) { p = 10; }
            if (p > 800) { p = 800; }
            s->speed_percent = p;
        }
    }
    /* F5 saves here; unset means F5 explains itself and saves nothing. */
    s->state_path = SDL_getenv("NJ_SDL2_STATE");

    s->capture = SDL_getenv("NJ_SDL2_CAPTURE");
    s->capture_n = 0;
    s->capture_every = 1;
    if (s->capture && *s->capture) {
        const char *every = SDL_getenv("NJ_SDL2_CAPTURE_AT");
        if (every && *every) {
            s->capture_every = SDL_atoi(every);
            if (s->capture_every < 1) {
                s->capture_every = 1;
            }
        }
    }

    memset(out, 0, sizeof(*out));
    s->port = out;
    out->name = "sdl2";
    out->ud = s;
    out->millis = sdl_millis;
    out->sleep_ms = sdl_sleep_ms;
    out->idle = sdl_idle;
    out->device_property = sdl_device_property;
    out->tone_play = sdl_tone_play;
    out->tone_stop = sdl_tone_stop;
    out->pcm_open = sdl_pcm_open;
    out->pcm_write = sdl_pcm_write;
    out->pcm_close = sdl_pcm_close;
    out->pcm_free = sdl_pcm_free;
    out->log = sdl_log;
    out->display_init = sdl_display_init;
    out->display_present = sdl_display_present;
    out->input_poll = sdl_input_poll;
    out->input_push = sdl_input_push;
    /* No codecs to install, but this is how the port gets the VM it must not
     * touch except from input_poll (the speed badge and the F5 snapshot). */
    out->install_codecs = sdl_install_codecs;
    out->shutdown = sdl_shutdown;
    out->fps_cap = 30;
    return NJ_OK;
}
