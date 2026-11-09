/* Shared desktop UI DPI policy. No SDL logical size/event filtering is used.
 * Scales describe physical renderer pixels per 96-DPI UI unit. A high-DPI
 * compositor already supplies drawable/window pixels; divide by that ratio
 * when choosing a window size, rather than applying desktop scale twice.
 * Priority: NJ2ME_UI_SCALE (factor, e.g. 1.5), saved ui_scale (percent), auto.
 */
#ifndef NJ_SDL2_DPI_H
#define NJ_SDL2_DPI_H

#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__) && defined(SDL_VIDEO_DRIVER_X11)
#include <SDL_syswm.h>
#endif

typedef struct nj_sdl2_dpi {
    double pixels;             /* renderer pixels per UI unit */
    double window_x, window_y;  /* window coordinates per UI unit */
    int window_w, window_h, output_w, output_h;
} nj_sdl2_dpi;

static inline double nj_sdl2_factor(const char *text)
{
    char *end;
    double value;
    if (!text || !*text) return 0.0;
    value = strtod(text, &end);
    while (*end == ' ' || *end == '\t') end++;
    /* Reject infinities/NaNs as well as malformed or absurd environment data. */
    if (*end || !(value >= 0.5 && value <= 8.0)) return 0.0;
    return value;
}

static inline double nj_sdl2_xft_scale(SDL_Window *window)
{
#if defined(__linux__) && defined(SDL_VIDEO_DRIVER_X11)
    SDL_SysWMinfo info;
    void *lib;
    const char *resources;
    char *(*resource_string)(Display *);
    double result = 0.0;
    SDL_VERSION(&info.version);
    if (!window || !SDL_GetWindowWMInfo(window, &info) ||
        info.subsystem != SDL_SYSWM_X11) return 0.0;
    lib = SDL_LoadObject("libX11.so.6");
    if (!lib) return 0.0;
    /* SDL owns the Display; never open another connection or require -lX11. */
    resource_string = (char *(*)(Display *))SDL_LoadFunction(lib, "XResourceManagerString");
    resources = resource_string ? resource_string(info.info.x11.display) : NULL;
    while (resources && *resources) {
        const char *line = resources;
        const char *next = strchr(line, '\n');
        char *end;
        double dpi;
        while (*line == ' ' || *line == '\t') line++;
        if (strncmp(line, "Xft.dpi", 7) == 0) {
            line += 7;
            while (*line == ' ' || *line == '\t') line++;
            if (*line == ':') {
                dpi = strtod(line + 1, &end);
                if (end != line + 1 && dpi >= 48.0 && dpi <= 768.0)
                    result = dpi / 96.0;
            }
        }
        resources = next ? next + 1 : NULL;
    }
    SDL_UnloadObject(lib);
    return result;
#else
    (void)window;
    return 0.0;
#endif
}

static inline nj_sdl2_dpi nj_sdl2_get_dpi(SDL_Window *window,
                                         SDL_Renderer *renderer, int percent)
{
    nj_sdl2_dpi d;
    double ratio_x, ratio_y, value, desktop = 1.0;
    float ddpi = 0.0f;
    int display = window ? SDL_GetWindowDisplayIndex(window) : 0;
    memset(&d, 0, sizeof(d));
    if (window) SDL_GetWindowSize(window, &d.window_w, &d.window_h);
    if (renderer) {
        /* The software renderer can retain its old backing surface after a
         * window resize (including SDL2-compat). A draw command refreshes it.
         * All callers query DPI before repainting the complete frame. */
        SDL_RenderClear(renderer);
        SDL_GetRendererOutputSize(renderer, &d.output_w, &d.output_h);
    }
    if (d.window_w <= 0) d.window_w = 1;
    if (d.window_h <= 0) d.window_h = 1;
    if (d.output_w <= 0) d.output_w = d.window_w;
    if (d.output_h <= 0) d.output_h = d.window_h;
    ratio_x = (double)d.output_w / d.window_w;
    ratio_y = (double)d.output_h / d.window_h;
    value = nj_sdl2_factor(SDL_getenv("NJ2ME_UI_SCALE"));
    if (value > 0.0) {
        d.pixels = value;
    } else if (percent >= 100 && percent <= 300) {
        d.pixels = percent / 100.0;
    } else {
        if (display >= 0 && SDL_GetDisplayDPI(display, &ddpi, NULL, NULL) == 0 &&
            ddpi >= 48.0f && ddpi <= 768.0f) desktop = ddpi / 96.0;
        value = nj_sdl2_xft_scale(window);
        if (value > 0.0) desktop = value;
        value = nj_sdl2_factor(SDL_getenv("QT_SCALE_FACTOR"));
        if (value > 0.0) desktop = value;
        value = nj_sdl2_factor(SDL_getenv("GDK_SCALE"));
        {
            double font = nj_sdl2_factor(SDL_getenv("GDK_DPI_SCALE"));
            if (value > 0.0 || font > 0.0)
                desktop = (value > 0.0 ? value : 1.0) * (font > 0.0 ? font : 1.0);
        }
        if (desktop < ratio_x) desktop = ratio_x;
        if (desktop < ratio_y) desktop = ratio_y;
        /* Round noisy physical DPI reports, while respecting explicit factors. */
        if (desktop < 1.05) desktop = 1.0;
        d.pixels = desktop;
    }
    if (d.pixels < 1.0) d.pixels = 1.0;
    if (d.pixels > 3.0) d.pixels = 3.0;
    d.window_x = d.pixels / ratio_x;
    d.window_y = d.pixels / ratio_y;
    return d;
}

/* Some backends update drawable size after delivering the resize event. Probe
 * cheaply each frame as well, before converting input with the cached ratio. */
static inline int nj_sdl2_dpi_size_changed(SDL_Window *window,
                                           SDL_Renderer *renderer,
                                           const nj_sdl2_dpi *dpi)
{
    int w = 0, h = 0, ow = 0, oh = 0;
    SDL_GetWindowSize(window, &w, &h);
    if (SDL_GetRendererOutputSize(renderer, &ow, &oh) != 0) return 0;
    return w != dpi->window_w || h != dpi->window_h ||
           ow != dpi->output_w || oh != dpi->output_h;
}

static inline int nj_sdl2_ui_extent(int logical, double factor)
{
    return (int)(logical * factor + 0.5);
}

static inline int nj_sdl2_coord(int point, int from, int to)
{
    return from > 0 ? (int)((Sint64)point * to / from) : point;
}

static inline void nj_sdl2_limit_window(SDL_Window *window, int *w, int *h)
{
    SDL_Rect bounds;
    int display = window ? SDL_GetWindowDisplayIndex(window) : 0;
    if (display >= 0 && SDL_GetDisplayUsableBounds(display, &bounds) == 0) {
        /* Leave room for decorations, even without a window manager in tests. */
        int max_w = bounds.w > 64 ? bounds.w - 32 : bounds.w;
        int max_h = bounds.h > 64 ? bounds.h - 48 : bounds.h;
        if (*w > max_w) *w = max_w;
        if (*h > max_h) *h = max_h;
    }
    if (*w < 1) *w = 1;
    if (*h < 1) *h = 1;
}
#endif
