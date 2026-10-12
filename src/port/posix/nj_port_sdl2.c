/*
 * nano_j2me - nj_port_sdl2.c
 *
 * The desktop port: an SDL2 window showing the runtime's framebuffer.
 *
 * It is the reference implementation for a "real" display port - it shows how
 * to handle a texture whose pitch differs from width*bytes_per_pixel, how to
 * do integer-only upscaling, and how to translate a keyboard into MIDP keys.
 * A microcontroller port does the same three things against an SPI panel.
 */
#include "nj2me/nj_port.h"
#include "nj2me/nj_types.h"

#include <SDL.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Up, down, left, right, fire, soft-left, soft-right. */
#define NJ_SDL2_NBTN 7
enum {
    NJ_SDL2_BTN_UP = 0, NJ_SDL2_BTN_DOWN, NJ_SDL2_BTN_LEFT, NJ_SDL2_BTN_RIGHT,
    NJ_SDL2_BTN_FIRE, NJ_SDL2_BTN_SOFT_L, NJ_SDL2_BTN_SOFT_R
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
    nj_u1    btn_key[NJ_SDL2_NBTN];
    int      btn_n;
    int      btn_held;      /* index into btn[], or -1 */
} sdl_state;


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
            "Z/X/C/V=game A/B/C/D  mouse: click and drag"
            "  (NJ_SDL_GRAB=1 if the window cannot be focused)");

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
    int scale, bs, pad, cross_x, cross_y;

    if (s->window) {
        SDL_GetWindowSize(s->window, &win_w, &win_h);
    }
    if (win_w <= 0 || win_h <= 0 || s->width <= 0 || s->height <= 0) {
        s->game_dst.x = s->game_dst.y = 0;
        s->game_dst.w = s->width;
        s->game_dst.h = s->height;
        s->btn_n = 0;
        return;
    }

    scale = win_w / s->width;
    if (win_h / s->height < scale) { scale = win_h / s->height; }
    if (scale < 1) { scale = 1; }

    s->game_dst.w = s->width * scale;
    s->game_dst.h = s->height * scale;
    s->game_dst.x = (win_w - s->game_dst.w) / 2;
    s->game_dst.y = (win_h - s->game_dst.h) / 2;
    s->scale = scale;

    if (!s->keys_visible) {
        s->btn_n = 0;
        return;
    }

    /*
     * A 3x3 cross of `bs` cells bottom-left, a round fire bottom-right, and the
     * two soft keys along the top - the positions a phone had them in, so the
     * layout needs no label to be understood.
     */
    bs = win_h / 7;
    if (bs > 72) { bs = 72; }
    if (bs < 24) { bs = 24; }
    pad = bs / 3;

    cross_x = pad;
    cross_y = win_h - pad - 3 * bs;

    s->btn[NJ_SDL2_BTN_LEFT].x  = cross_x;
    s->btn[NJ_SDL2_BTN_LEFT].y  = cross_y + bs;
    s->btn[NJ_SDL2_BTN_RIGHT].x = cross_x + 2 * bs;
    s->btn[NJ_SDL2_BTN_RIGHT].y = cross_y + bs;
    s->btn[NJ_SDL2_BTN_UP].x    = cross_x + bs;
    s->btn[NJ_SDL2_BTN_UP].y    = cross_y;
    s->btn[NJ_SDL2_BTN_DOWN].x  = cross_x + bs;
    s->btn[NJ_SDL2_BTN_DOWN].y  = cross_y + 2 * bs;
    s->btn[NJ_SDL2_BTN_FIRE].x  = win_w - pad - 2 * bs;
    s->btn[NJ_SDL2_BTN_FIRE].y  = win_h - pad - 2 * bs;
    s->btn[NJ_SDL2_BTN_SOFT_L].x = pad;
    s->btn[NJ_SDL2_BTN_SOFT_L].y = pad;
    s->btn[NJ_SDL2_BTN_SOFT_R].x = win_w - pad - 2 * bs;
    s->btn[NJ_SDL2_BTN_SOFT_R].y = pad;

    s->btn[NJ_SDL2_BTN_UP].w     = bs;  s->btn[NJ_SDL2_BTN_UP].h     = bs;
    s->btn[NJ_SDL2_BTN_DOWN].w   = bs;  s->btn[NJ_SDL2_BTN_DOWN].h   = bs;
    s->btn[NJ_SDL2_BTN_LEFT].w   = bs;  s->btn[NJ_SDL2_BTN_LEFT].h   = bs;
    s->btn[NJ_SDL2_BTN_RIGHT].w  = bs;  s->btn[NJ_SDL2_BTN_RIGHT].h  = bs;
    s->btn[NJ_SDL2_BTN_FIRE].w   = 2 * bs;
    s->btn[NJ_SDL2_BTN_FIRE].h   = 2 * bs;
    s->btn[NJ_SDL2_BTN_SOFT_L].w = 2 * bs;
    s->btn[NJ_SDL2_BTN_SOFT_L].h = bs;
    s->btn[NJ_SDL2_BTN_SOFT_R].w = 2 * bs;
    s->btn[NJ_SDL2_BTN_SOFT_R].h = bs;
    s->btn_n = NJ_SDL2_NBTN;
}

/*
 * A filled triangle drawn as horizontal spans.  Spans rather than
 * SDL_RenderGeometry because this has to work on the software renderer too, and
 * a shape made of one-pixel lines always does.
 *
 * `dir` is SDL2_DIR_*, and it is NOT the button index: an arrow's direction and
 * a button's position in an enum are different things that happen to be numbers,
 * and passing the index here drew a cross whose arrows all pointed the wrong way
 * while every rect was in the right place.
 */
#define SDL2_DIR_UP    0
#define SDL2_DIR_DOWN  1
#define SDL2_DIR_LEFT  2
#define SDL2_DIR_RIGHT 3

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
    for (dy = -rad; dy <= rad; dy++) {
        int dx = rad;
        while (dx > 0 && dx * dx + dy * dy > rad * rad) { dx--; }
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
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
        SDL_Rect *b = &s->btn[i];
        int held = (s->btn_held == i);

        /* Translucent, because these sit on top of somebody's game. */
        SDL_SetRenderDrawColor(s->renderer, 250, 250, 250,
                               (Uint8)(held ? 190 : 90));
        if (i == NJ_SDL2_BTN_FIRE) {
            fill_round(s->renderer, b->x + b->w / 2, b->y + b->h / 2,
                       b->w / 2 - 2);
        } else {
            SDL_RenderFillRect(s->renderer, b);
        }

        SDL_SetRenderDrawColor(s->renderer, 20, 20, 20,
                               (Uint8)(held ? 255 : 150));
        if (i == NJ_SDL2_BTN_FIRE) {
            fill_round(s->renderer, b->x + b->w / 2, b->y + b->h / 2,
                       b->w / 2 - 2);
            SDL_SetRenderDrawColor(s->renderer, 250, 250, 250, 160);
            fill_round(s->renderer, b->x + b->w / 2, b->y + b->h / 2,
                       b->w / 2 - 5);
        } else {
            SDL_RenderDrawRect(s->renderer, b);
        }

        /* A glyph so the cross reads as directions rather than as four boxes. */
        SDL_SetRenderDrawColor(s->renderer, 30, 30, 30, 220);
        if (i < 4) {
            static const int arrow[NJ_SDL2_NBTN] = {
                SDL2_DIR_UP, SDL2_DIR_DOWN, SDL2_DIR_LEFT, SDL2_DIR_RIGHT,
                SDL2_DIR_UP, SDL2_DIR_UP, SDL2_DIR_UP
            };
            fill_triangle(s->renderer, b->x + b->w / 4, b->y + b->h / 4,
                          b->w / 2, b->h / 2, arrow[i]);
        } else if (i == NJ_SDL2_BTN_FIRE) {
            SDL_SetRenderDrawColor(s->renderer, 30, 30, 30, 200);
            fill_round(s->renderer, b->x + b->w / 2, b->y + b->h / 2,
                       b->w / 5);
        } else {
            /* Soft keys: a filled bar, left- or right-aligned in the pill. */
            SDL_Rect bar;
            bar.w = b->w / 3;
            bar.h = b->h / 4;
            bar.y = b->y + (b->h - bar.h) / 2;
            bar.x = (i == NJ_SDL2_BTN_SOFT_L) ? (b->x + b->w / 6)
                                              : (b->x + b->w / 2);
            SDL_RenderFillRect(s->renderer, &bar);
        }
    }
    SDL_SetRenderDrawBlendMode(s->renderer, SDL_BLENDMODE_NONE);
}

/* Which key is at this window point, or -1. */
static int sdl_key_at(sdl_state *s, int x, int y)
{
    int i;
    for (i = 0; i < s->btn_n; i++) {
        const SDL_Rect *b = &s->btn[i];
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) {
            return i;
        }
    }
    return -1;
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
                    if (!s->keys_visible && s->btn_held >= 0) {
                        /* Hiding them while one is held would leave the key
                         * stuck down, because the release would never come. */
                        sdl_push_key(s, s->btn_key[s->btn_held], 0);
                        s->btn_held = -1;
                    }
                    sdl_layout(s);
                }
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
                 * The on-screen keys come first.  A press that lands on one is
                 * a key press, not a pointer press - otherwise every tap on the
                 * D-pad would ALSO be reported to a MIDlet as a click at that
                 * spot, and a game with a pointer UI would react to both.
                 */
                if (ev.type == SDL_MOUSEBUTTONDOWN) {
                    int hit = sdl_key_at(s, ev.button.x, ev.button.y);
                    if (hit >= 0) {
                        sdl_push_key(s, s->btn_key[hit], 1);
                        s->btn_held = hit;
                        break;
                    }
                } else if (s->btn_held >= 0) {
                    sdl_push_key(s, s->btn_key[s->btn_held], 0);
                    s->btn_held = -1;
                    break;
                }
                {
                    int gx, gy;
                    sdl_to_game(s, ev.button.x, ev.button.y, &gx, &gy);
                    sdl_push_pointer(s, gx, gy,
                                     (ev.type == SDL_MOUSEBUTTONDOWN)
                                         ? NJ_EV_POINTER_DOWN
                                         : NJ_EV_POINTER_UP);
                }
            }
            break;
        case SDL_MOUSEMOTION:
            /*
             * Only while a button is held.  A desktop mouse sweeping across the
             * window is not a finger on a panel, and reporting it as a drag
             * would give every MIDlet a stream of phantom moves.
             *
             * And not at all while an on-screen key is held: the finger that is
             * pressing "right" is not dragging across the game.
             */
            if ((ev.motion.state & SDL_BUTTON_LMASK) != 0 && s->btn_held < 0) {
                int gx, gy;
                sdl_to_game(s, ev.motion.x, ev.motion.y, &gx, &gy);
                sdl_push_pointer(s, gx, gy, NJ_EV_POINTER_MOVE);
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

            if (ev.type == SDL_FINGERDOWN) {
                int hit = sdl_key_at(s, wx, wy);
                if (hit >= 0) {
                    sdl_push_key(s, s->btn_key[hit], 1);
                    s->btn_held = hit;
                    break;
                }
            } else if (s->btn_held >= 0) {
                if (ev.type == SDL_FINGERUP) {
                    sdl_push_key(s, s->btn_key[s->btn_held], 0);
                    s->btn_held = -1;
                }
                break;
            }
            sdl_to_game(s, wx, wy, &gx, &gy);
            sdl_push_pointer(s, gx, gy,
                             (ev.type == SDL_FINGERDOWN) ? NJ_EV_POINTER_DOWN :
                             (ev.type == SDL_FINGERUP)   ? NJ_EV_POINTER_UP :
                                                           NJ_EV_POINTER_MOVE);
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

static int sdl_input_poll(void *ud, nj_event *ev)
{
    sdl_state *s = SS(ud);

    if (s->qh == s->qt) {
        return 0;
    }
    *ev = s->queue[s->qh];
    s->qh = (s->qh + 1) % (int)(sizeof(s->queue) / sizeof(s->queue[0]));
    return 1;
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
    s->btn_key[NJ_SDL2_BTN_UP]     = NJ_KEY_UP;
    s->btn_key[NJ_SDL2_BTN_DOWN]   = NJ_KEY_DOWN;
    s->btn_key[NJ_SDL2_BTN_LEFT]   = NJ_KEY_LEFT;
    s->btn_key[NJ_SDL2_BTN_RIGHT]  = NJ_KEY_RIGHT;
    s->btn_key[NJ_SDL2_BTN_FIRE]   = NJ_KEY_FIRE;
    s->btn_key[NJ_SDL2_BTN_SOFT_L] = NJ_KEY_SOFT_LEFT;
    s->btn_key[NJ_SDL2_BTN_SOFT_R] = NJ_KEY_SOFT_RIGHT;

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
    out->shutdown = sdl_shutdown;
    out->fps_cap = 30;
    return NJ_OK;
}
