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
    out->shutdown = sdl_shutdown;
    out->fps_cap = 30;
    return NJ_OK;
}
