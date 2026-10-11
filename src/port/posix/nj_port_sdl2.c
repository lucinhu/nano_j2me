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
    s->renderer = SDL_CreateRenderer(s->window, -1,
                                     SDL_RENDERER_ACCELERATED |
                                     SDL_RENDERER_PRESENTVSYNC);
    if (!s->renderer) {
        s->renderer = SDL_CreateRenderer(s->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!s->renderer) {
        return -1;
    }
    SDL_RenderSetLogicalSize(s->renderer, s->width, s->height);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    s->texture = SDL_CreateTexture(s->renderer, sdl_pixel_format(NJ_PIXFMT),
                                   SDL_TEXTUREACCESS_STREAMING,
                                   s->width, s->height);
    if (!s->texture) {
        return -1;
    }

    *w = s->width;
    *h = s->height;
    *stride = s->stride;
    *px = s->pixels;
    return NJ_PIXFMT;
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
    SDL_RenderClear(s->renderer);
    SDL_RenderCopy(s->renderer, s->texture, NULL, NULL);
    SDL_RenderPresent(s->renderer);
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */
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
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        return NJ_ERR;
    }
    s = (sdl_state *)calloc(1u, sizeof(*s));
    if (!s) {
        SDL_Quit();
        return NJ_ERR_NOMEM;
    }
    s->scale = 0;                 /* auto: largest integer fit */

    memset(out, 0, sizeof(*out));
    out->name = "sdl2";
    out->ud = s;
    out->millis = sdl_millis;
    out->sleep_ms = sdl_sleep_ms;
    out->idle = sdl_idle;
    out->log = sdl_log;
    out->display_init = sdl_display_init;
    out->display_present = sdl_display_present;
    out->input_poll = sdl_input_poll;
    out->shutdown = sdl_shutdown;
    out->fps_cap = 30;
    return NJ_OK;
}
