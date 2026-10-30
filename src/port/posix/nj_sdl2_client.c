/*
 * nano_j2me - src/port/posix/nj_sdl2_client.c
 *
 * The desktop client's window loop: the half of the launcher that talks to
 * SDL.  It owns no decisions - the library window is a plain ARGB8888 surface
 * that src/client/nj_client_ui.c paints, and this file only moves that
 * surface to a texture, feeds events in and starts games.
 *
 * One window at a time is the whole design.  The launcher window and the game
 * window are never alive together: picking a game closes the launcher, creates
 * the ordinary nj_port_sdl2 and runs nj_run(); when the game ends (MIDlet exit,
 * the X button, F10, or a jar dropped on the game) the port is shut down and
 * the launcher is opened again.  That is what makes "back to the library"
 * cheap and what keeps the game path exactly the path a CLI run takes.
 *
 * The game path is the CLI path: a copy of the caller's nj_run_opts with only
 * jar_path replaced, the same port constructor, and nj_run() doing the rest.
 * Anything the client could do differently would be behaviour nobody tests
 * from a terminal.
 */
#include "nj2me/nj.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NJ_CLIENT_LAUNCHER_W 720
#define NJ_CLIENT_LAUNCHER_H 540
#define NJ_CLIENT_MIN_W      320
#define NJ_CLIENT_MIN_H      200
#define NJ_CLIENT_FPS        30

/* One open launcher window.  Rebuilt after every game. */
typedef struct nj_sdl_client {
    SDL_Window    *window;
    SDL_Renderer  *renderer;
    SDL_Texture   *texture;
    nj_surface     surface;
    unsigned char *pixels;

    int width;
    int height;
    int frames;
    int frames_limit;      /* NJ2ME_CLIENT_FRAMES: 0 = no limit */
    const char *shot_path; /* NJ2ME_CLIENT_SCREENSHOT: PPM or NULL */
} nj_sdl_client;

/* ---------------------------------------------------------------- */
/* Surface / window plumbing                                         */
/* ---------------------------------------------------------------- */

static int client_resize(nj_sdl_client *c, int width, int height)
{
    SDL_Texture *texture;
    unsigned char *pixels;

    if (width < NJ_CLIENT_MIN_W) width = NJ_CLIENT_MIN_W;
    if (height < NJ_CLIENT_MIN_H) height = NJ_CLIENT_MIN_H;
    if (width == c->width && height == c->height && c->pixels != NULL) {
        return 1;
    }
    pixels = (unsigned char *)realloc(c->pixels, (size_t)width * (size_t)height * 4u);
    if (pixels == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "client: out of memory");
        return 0;
    }
    c->pixels = pixels;
    texture = SDL_CreateTexture(c->renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, width, height);
    if (texture == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "client: %s",
                     SDL_GetError());
        return 0;
    }
    if (c->texture != NULL) {
        SDL_DestroyTexture(c->texture);
    }
    c->texture = texture;
    c->width = width;
    c->height = height;
    c->surface.pixels = pixels;
    c->surface.width = width;
    c->surface.height = height;
    c->surface.stride = width * 4;
    c->surface.format = NJ_PIXFMT_ARGB8888;
    c->surface.bpp = 4;
    return 1;
}

static int client_launcher_open(nj_sdl_client *c, int width, int height,
                                nj_client_ui *ui)
{
    memset(c, 0, sizeof(*c));
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "client: cannot start SDL2 video: %s", SDL_GetError());
        return 0;
    }
    c->window = SDL_CreateWindow("nano_j2me",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 width, height,
                                 SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (c->window == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "client: cannot open the launcher window: %s",
                     SDL_GetError());
        SDL_Quit();
        return 0;
    }
    c->renderer = SDL_CreateRenderer(c->window, -1, SDL_RENDERER_SOFTWARE);
    if (c->renderer == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "client: cannot create a renderer: %s", SDL_GetError());
        SDL_DestroyWindow(c->window);
        c->window = NULL;
        SDL_Quit();
        return 0;
    }
    if (!client_resize(c, width, height)) {
        SDL_DestroyRenderer(c->renderer);
        SDL_DestroyWindow(c->window);
        c->renderer = NULL;
        c->window = NULL;
        SDL_Quit();
        return 0;
    }
    nj_client_ui_resize(ui, width, height);
    {
        const char *limit = getenv("NJ2ME_CLIENT_FRAMES");
        c->frames_limit = (limit != NULL && limit[0] != '\0') ? atoi(limit) : 0;
    }
    c->shot_path = getenv("NJ2ME_CLIENT_SCREENSHOT");
    return 1;
}

static void client_launcher_close(nj_sdl_client *c)
{
    if (c->texture != NULL) SDL_DestroyTexture(c->texture);
    if (c->renderer != NULL) SDL_DestroyRenderer(c->renderer);
    if (c->window != NULL) SDL_DestroyWindow(c->window);
    free(c->pixels);
    c->texture = NULL;
    c->renderer = NULL;
    c->window = NULL;
    c->pixels = NULL;
    SDL_Quit();
}

/* P6 PPM, the format the project's other screenshots use: no encoder needed. */
static void client_dump_ppm(const nj_sdl_client *c, const char *path)
{
    FILE *f;
    int y;
    int x;

    if (path == NULL || path[0] == '\0' || c->pixels == NULL) {
        return;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "client: cannot write %s", path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", c->width, c->height);
    for (y = 0; y < c->height; y++) {
        const unsigned char *row =
            c->pixels + (size_t)y * (size_t)c->surface.stride;
        for (x = 0; x < c->width; x++) {
            const unsigned char *p = row + (size_t)x * 4u;
            /* ARGB8888 in memory on a little-endian host is B,G,R,A. */
            unsigned char rgb[3];
            rgb[0] = p[2];
            rgb[1] = p[1];
            rgb[2] = p[0];
            fwrite(rgb, 1u, 3u, f);
        }
    }
    fclose(f);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "client: wrote %s", path);
}

static void client_draw(nj_sdl_client *c, nj_client_ui *ui)
{
    SDL_Rect dst;

    nj_client_ui_draw(ui, &c->surface);
    SDL_UpdateTexture(c->texture, NULL, c->pixels, c->surface.stride);
    dst.x = 0;
    dst.y = 0;
    dst.w = c->width;
    dst.h = c->height;
    SDL_RenderClear(c->renderer);
    SDL_RenderCopy(c->renderer, c->texture, NULL, &dst);
    SDL_RenderPresent(c->renderer);
}

/* ---------------------------------------------------------------- */
/* Input                                                             */
/* ---------------------------------------------------------------- */

static int client_key_for_sym(SDL_Keycode sym)
{
    switch (sym) {
    case SDLK_UP:        return NJ_CLIENT_KEY_UP;
    case SDLK_DOWN:      return NJ_CLIENT_KEY_DOWN;
    case SDLK_LEFT:      return NJ_CLIENT_KEY_LEFT;
    case SDLK_RIGHT:     return NJ_CLIENT_KEY_RIGHT;
    case SDLK_PAGEUP:    return NJ_CLIENT_KEY_PAGE_UP;
    case SDLK_PAGEDOWN:  return NJ_CLIENT_KEY_PAGE_DOWN;
    case SDLK_HOME:      return NJ_CLIENT_KEY_HOME;
    case SDLK_END:       return NJ_CLIENT_KEY_END;
    case SDLK_RETURN:    return NJ_CLIENT_KEY_ENTER;
    case SDLK_KP_ENTER:  return NJ_CLIENT_KEY_ENTER;
    case SDLK_ESCAPE:    return NJ_CLIENT_KEY_ESCAPE;
    case SDLK_BACKSPACE: return NJ_CLIENT_KEY_BACKSPACE;
    default:             return 0;
    }
}

static void client_key_char(nj_client_ui *ui, SDL_Keycode sym)
{
    if (sym >= SDLK_a && sym <= SDLK_z) {
        nj_client_ui_char(ui, (nj_u2)('a' + (sym - SDLK_a)));
    } else if (sym >= SDLK_0 && sym <= SDLK_9) {
        nj_client_ui_char(ui, (nj_u2)('0' + (sym - SDLK_0)));
    }
}

/*
 * Pump the window system into the model.  Returns 1 when the user wants the
 * launcher closed; `run_jar` is filled when the model asked to start a game
 * (a click, Enter, or a dropped jar).
 */
static int client_launcher_events(nj_sdl_client *c, nj_client_ui *ui,
                                  char *run_jar, int run_cap)
{
    SDL_Event ev;
    nj_client_action action;
    int close = 0;

    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            close = 1;
            break;
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                int w = 0;
                int h = 0;
                SDL_GetWindowSize(c->window, &w, &h);
                if (w > 0 && h > 0 && client_resize(c, w, h)) {
                    nj_client_ui_resize(ui, w, h);
                }
            }
            break;
        case SDL_KEYDOWN: {
            int key;

            /*
             * While the key page waits for a key, the press *is* the data:
             * hand it over as SDL's name and keep the model's own navigation
             * and text handling out of it.  Escape cancels inside the model.
             */
            if (nj_client_ui_capturing(ui)) {
                if (ev.key.repeat == 0) {
                    nj_client_ui_capture_key(
                        ui, SDL_GetKeyName(ev.key.keysym.sym));
                }
                break;
            }
            key = client_key_for_sym(ev.key.keysym.sym);
            if (key != 0) {
                nj_client_ui_key(ui, key, 1, ev.key.repeat ? 1 : 0);
            } else if (ev.key.repeat == 0) {
                client_key_char(ui, ev.key.keysym.sym);
            }
            break;
        }
        case SDL_KEYUP: {
            int key;
            if (nj_client_ui_capturing(ui)) break;
            key = client_key_for_sym(ev.key.keysym.sym);
            if (key != 0) {
                nj_client_ui_key(ui, key, 0, 0);
            }
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
            if (ev.button.button == SDL_BUTTON_LEFT) {
                nj_client_ui_pointer(ui, ev.button.x, ev.button.y, 1);
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (ev.button.button == SDL_BUTTON_LEFT) {
                nj_client_ui_pointer(ui, ev.button.x, ev.button.y, 0);
            }
            break;
        case SDL_MOUSEMOTION:
            nj_client_ui_pointer_move(ui, ev.motion.x, ev.motion.y);
            break;
        case SDL_MOUSEWHEEL:
            nj_client_ui_wheel(ui, ev.wheel.y);
            break;
        case SDL_DROPFILE:
            if (ev.drop.file != NULL) {
                nj_client_ui_drop(ui, ev.drop.file);
                SDL_free(ev.drop.file);
            }
            break;
        default:
            break;
        }
        if (close) break;
    }
    if (nj_client_ui_quit(ui)) {
        return 1;
    }
    if (nj_client_ui_take(ui, &action, run_jar, run_cap)) {
        if (action == NJ_CLIENT_ACTION_QUIT) {
            return 1;
        }
        if (action == NJ_CLIENT_ACTION_RUN) {
            return 1;
        }
    }
    return close;
}

/* ---------------------------------------------------------------- */
/* The library window                                                */
/* ---------------------------------------------------------------- */

/*
 * Run the launcher until it quits or asks for a game.
 * Returns 1 when `run_jar` names the game to start, 0 when the client should
 * exit.  `quit` says why the launcher ended: 1 = user closed it.
 */
static int client_launcher_run(nj_sdl_client *c, nj_client_ui *ui,
                               nj_library *lib, nj_client_settings *settings,
                               int width, int height, char *run_jar,
                               int run_cap, int *quit)
{
    int scan_started = 0;
    int running = 1;
    int take_game = 0;

    if (!client_launcher_open(c, width, height, ui)) {
        *quit = 1;
        return 0;
    }
    run_jar[0] = '\0';
    while (running) {
        unsigned start = SDL_GetTicks();
        int closed;

        closed = client_launcher_events(c, ui, run_jar, run_cap);
        if (closed) {
            if (run_jar[0] != '\0') {
                take_game = 1;
                running = 0;
                break;
            }
            *quit = 1;
            break;
        }

        /* One scan per launcher session: a failed opendir must not be retried
         * thirty times a second, but a directory dropped on the window starts
         * its own scan from the model. */
        if (!scan_started && !nj_lib_scan_busy(lib) && !lib->done &&
            settings->games_dir[0] != '\0') {
            nj_lib_scan_begin(lib, settings->games_dir);
            scan_started = 1;
        }
        if (nj_lib_scan_busy(lib)) {
            nj_lib_scan_step(lib, 2);
        }

        nj_client_ui_tick(ui, SDL_GetTicks());
        client_draw(c, ui);
        c->frames++;

        if (c->frames_limit > 0 && c->frames >= c->frames_limit) {
            /* Let a screenshot script see a finished library rather than a
             * half-scanned one; the budget is bounded so a huge folder cannot
             * hang the run. */
            int guard = 0;
            while (nj_lib_scan_busy(lib) && guard++ < 10000) {
                nj_lib_scan_step(lib, 64);
            }
            nj_client_ui_tick(ui, SDL_GetTicks());
            client_draw(c, ui);
            client_dump_ppm(c, c->shot_path);
            *quit = 1;
            break;
        }

        /* ~30 fps is plenty for a list; a full redraw at 240x320 would be
         * cheap too, but the launcher is usually the bigger window. */
        {
            unsigned spent = SDL_GetTicks() - start;
            const unsigned target = 1000u / NJ_CLIENT_FPS;
            if (spent < target) {
                SDL_Delay(target - spent);
            }
        }
    }
    client_launcher_close(c);
    if (take_game) {
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- */
/* Settings that reach the game port                                 */
/* ---------------------------------------------------------------- */

nj_size nj_client_apply_game_settings(nj_port *port,
                                      const nj_client_settings *settings)
{
    int i;

    if (!settings) return 0;
    /*
     * These five knobs are how the port already exposes itself (the game
     * window has no setters), and it reads them when it is created, so a
     * NULL port means "just set the environment before nj_port_sdl2()".
     * The filter and fullscreen are read later, at display_init(), but the
     * same call sets them early for both.
     */
    SDL_setenv("NJ_SDL2_KEYS", settings->keys_visible ? "1" : "0", 1);
    SDL_setenv("NJ_SDL2_ACCEL", settings->accel ? "1" : "0", 1);
    SDL_setenv("NJ_SDL2_FILTER", settings->filter_linear ? "linear" : "nearest",
               1);
    SDL_setenv("NJ_SDL2_FULLSCREEN", settings->fullscreen ? "1" : "0", 1);
    SDL_setenv("NJ_SDL2_MUTE", settings->mute ? "1" : "0", 1);
    SDL_setenv("NJ_SDL2_NO_NETWORK", settings->network ? "0" : "1", 1);

    if (port != NULL) {
        for (i = 0; i < NJ_PORT_BIND_COUNT; i++) {
            int key = 0;
            if (settings->binds[i][0] != '\0') {
                key = (int)SDL_GetKeyFromName(settings->binds[i]);
                if (key == SDLK_UNKNOWN) key = 0;
            }
            nj_port_sdl2_bind(port, i, key);
        }
    }
    return (nj_size)settings->heap_mb * 1024u * 1024u;
}

/* ---------------------------------------------------------------- */
/* One game                                                          */
/* ---------------------------------------------------------------- */

static nj_status client_run_game(const nj_client_run_options *options,
                                 const char *jar, char *next_jar,
                                 int next_cap)
{
    nj_client_settings fallback;
    const nj_client_settings *settings = options->settings;
    nj_port port;
    nj_run_opts opts;
    nj_status st;
    nj_size heap;
    int width;
    int height;

    if (settings == NULL) {
        nj_client_settings_default(&fallback);
        settings = &fallback;
    }
    memset(&port, 0, sizeof(port));
    memset(&opts, 0, sizeof(opts));
    opts = *options->game;
    opts.jar_path = jar;
    opts.jar_data = NULL;
    opts.jar_size = 0;
    width = options->width > 0 ? options->width : NJ_SCREEN_WIDTH;
    height = options->height > 0 ? options->height : NJ_SCREEN_HEIGHT;

    /*
     * The port reads the keypad preference and the renderer/audio/network
     * knobs from the environment when it is created (there are no setters),
     * so the settings have to be in place before nj_port_sdl2(); the key
     * bindings need the port, so they are installed right after it exists.
     */
    heap = nj_client_apply_game_settings(NULL, settings);
    if (options->keys_visible >= 0) {
        SDL_setenv("NJ_SDL2_KEYS", options->keys_visible ? "1" : "0", 1);
    }
    st = nj_port_sdl2(&port, "nano_j2me");
    if (st != NJ_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "client: cannot create the game window: %s",
                     SDL_GetError());
        return NJ_ERR;
    }
    (void)nj_client_apply_game_settings(&port, settings);
    port.width = width;
    port.height = height;
    if (options->scale > 0) {
        port.scale = options->scale;
    } else if (settings->game_scale > 0) {
        port.scale = settings->game_scale;
    }
    if (options->fps_cap >= 0) {
        port.fps_cap = options->fps_cap;
    } else {
        port.fps_cap = settings->fps_cap;
    }
    if (opts.heap_bytes == 0 && heap > 0) {
        opts.heap_bytes = heap;
    }
    if (options->rms_dir != NULL) {
        port.rms_dir = options->rms_dir;
    }
    if (options->fs_dir != NULL) {
        port.fs_dir = options->fs_dir;
    }

    st = (nj_status)nj_run(&port, &opts);

    /* A jar dropped on the game window is the next game, not a trip through
     * the library - ask before the port (and its queue) is gone. */
    next_jar[0] = '\0';
    if (nj_port_sdl2_take_dropped(&port, next_jar, (nj_u4)next_cap) != 1 ||
        next_jar[0] == '\0') {
        next_jar[0] = '\0';
    }
    nj_port_shutdown(&port);
    return st;
}

/* ---------------------------------------------------------------- */
/* Entry point                                                       */
/* ---------------------------------------------------------------- */

nj_status nj_client_run(const nj_client_run_options *options)
{
    nj_client_settings local_settings;
    nj_client_settings *settings;
    nj_library *local_library = NULL;
    nj_library *library;
    nj_client_ui *ui;
    char jar[NJ_CLIENT_PATH_MAX];
    int launcher_w;
    int launcher_h;
    int quit = 0;

    if (options == NULL || options->game == NULL) {
        return NJ_ERR;
    }
    if (options->settings != NULL) {
        settings = options->settings;
    } else {
        nj_client_settings_default(&local_settings);
        settings = &local_settings;
    }
    if (options->library != NULL) {
        library = options->library;
    } else {
        local_library = nj_library_new();
        if (local_library == NULL) {
            return NJ_ERR;
        }
        library = local_library;
    }
    ui = nj_client_ui_new(settings, library);
    if (ui == NULL) {
        nj_library_free(local_library);
        return NJ_ERR;
    }
    nj_client_ui_resize(ui, options->launcher_width > 0
                                ? options->launcher_width
                                : NJ_CLIENT_LAUNCHER_W,
                        options->launcher_height > 0
                                ? options->launcher_height
                                : NJ_CLIENT_LAUNCHER_H);
    launcher_w = options->launcher_width > 0 ? options->launcher_width
                                             : NJ_CLIENT_LAUNCHER_W;
    launcher_h = options->launcher_height > 0 ? options->launcher_height
                                              : NJ_CLIENT_LAUNCHER_H;

    jar[0] = '\0';
    if (options->first_jar != NULL && options->first_jar[0] != '\0') {
        size_t n = strlen(options->first_jar);
        if (n < sizeof(jar)) {
            memcpy(jar, options->first_jar, n + 1u);
        }
    }

    for (;;) {
        nj_sdl_client client;

        if (jar[0] != '\0') {
            char next[NJ_CLIENT_PATH_MAX];
            nj_status st = client_run_game(options, jar, next, (int)sizeof(next));

            nj_client_ui_note_game_exit(ui, jar, (int)st);
            jar[0] = '\0';
            if (next[0] != '\0') {
                memcpy(jar, next, strlen(next) + 1u);
                continue;
            }
            /* A scripted run (-run-frames) must end the client, not sit in a
             * library window nothing will ever close. */
            if (options->game->run_frames > 0) {
                break;
            }
            if (nj_client_ui_quit(ui)) {
                break;
            }
        }

        if (client_launcher_run(&client, ui, library, settings, launcher_w,
                                launcher_h, jar, (int)sizeof(jar),
                                &quit) == 1) {
            continue; /* run_jar is now in `jar` */
        }
        if (quit) {
            break;
        }
    }

    nj_client_settings_save(settings, NULL);
    nj_client_ui_free(ui);
    nj_library_free(local_library);
    return NJ_OK;
}
