/*
 * nano_j2me - nj_client.h
 *
 * The desktop client: a games library window that runs a .jar when you pick
 * it (or drop it on the window), and a settings file next to it.  FreeJ2ME
 * ships a Swing launcher; this is the same idea with no toolkit - the frame,
 * the rows and the text are drawn by the runtime's own rasteriser
 * (nj_gfx + the built-in fonts), and the only thing the window system is
 * asked for is one ARGB8888 texture per frame.
 *
 * The split matters for testing and for reuse:
 *
 *   src/client/nj_library.c      the file/ZIP/PNG half   (nj_library.h)
 *   src/client/nj_client_ui.c    the model + drawing     (this header)
 *   src/client/nj_client_config.c the settings file      (this header)
 *   src/port/posix/nj_sdl2_client.c  the window + game loop
 *
 * nj_client_ui never touches SDL and never blocks: the embedder feeds it
 * resize/key/char/pointer/wheel/drop events, calls tick() with a millisecond
 * clock and draw() into a memory surface, and polls take() for "run this jar"
 * and "quit".  tests/test_client_ui.c drives exactly that, headless, and
 * dumps PPM frames to look at.
 */
#ifndef NJ_CLIENT_H
#define NJ_CLIENT_H

#include "nj_types.h"
#include "nj_gfx.h"
#include "nj_port.h"
#include "nj_library.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NJ_CLIENT_PATH_MAX 1024

/* ------------------------------------------------------------------ */
/* Settings                                                            */
/* ------------------------------------------------------------------ */
/*
 * Saved as `key = value` lines in <XDG_CONFIG_HOME or ~/.config>/
 * nano_j2me/client.conf.  Paths are UTF-8; unknown keys are ignored so an
 * older client tolerates a newer file, and the file is rewritten on every
 * change (it is a few hundred bytes, not a database).  `window = 900x700`
 * holds the library window's size so it comes back where it was left,
 * `recent = <path>` lines list the last jars that ran (newest first) and
 * `favorite = <path>` lines carry the starred ones.
 */
#define NJ_CLIENT_BIND_NAME_MAX 24
#define NJ_CLIENT_RECENT_MAX 10
#define NJ_CLIENT_FAVORITE_MAX 32

typedef struct nj_client_settings {
    char games_dir[NJ_CLIENT_PATH_MAX]; /* the library directory              */
    char last_dir[NJ_CLIENT_PATH_MAX];  /* file browser starts here          */
    int  game_scale;                    /* 0 = auto, 1..4 = integer scale    */
    int  keys_visible;                  /* on-screen keypad in games, 0/1    */
    int  fps_cap;                       /* 0 = uncapped, else 30/60/120      */
    int  filter_linear;                 /* scale filter 0=nearest 1=linear   */
    int  fullscreen;                    /* games start fullscreen, 0/1       */
    int  accel;                         /* 0=software renderer, 1=GPU+vsync  */
    int  heap_mb;                       /* 0 = runtime default, else MB      */
    int  mute;                          /* host audio output muted, 0/1      */
    int  network;                       /* games may use the network, 0/1    */
    int  gamepad;                       /* SDL game controllers drive the
                                           game keys, 0/1                     */
    int  launcher_w;                    /* library window, 0 = 720 x 540     */
    int  launcher_h;
    /*
     * The library remembers what ran and what was starred.  Recent entries
     * are newest first; the UI shows them in a "最近玩过" section above the
     * full list and resolves them through the library, so a jar that moved
     * away simply drops out of sight.  Favorites are keept in the order they
     * were starred and get a star on their row.
     */
    char recent[NJ_CLIENT_RECENT_MAX][NJ_CLIENT_PATH_MAX];
    int  recent_count;
    char favorites[NJ_CLIENT_FAVORITE_MAX][NJ_CLIENT_PATH_MAX];
    int  favorite_count;
    /*
     * Per-slot key bindings: SDL key names ("W", "Space", "Left Shift", the
     * same spelling SDL_GetKeyName() prints) indexed by nj_port_bind.  Empty
     * means "use the port's default for this slot"; the desktop client turns
     * a name into a keycode with SDL_GetKeyFromName() before starting a game.
     */
    char binds[NJ_PORT_BIND_COUNT][NJ_CLIENT_BIND_NAME_MAX];
} nj_client_settings;

/*
 * Defaults: games dir from nj_client_default_games_dir(), last_dir equal to
 * it, scale auto, on-screen keys on, fps uncapped, nearest-neighbour filter,
 * windowed, software renderer, runtime heap, sound on, network on, every key
 * slot on its default.
 */
void nj_client_settings_default(nj_client_settings *s);

/* Display name and default SDL key name for a bind slot ("上" and "Up", ...).
 * The display name may be NULL for an unknown slot; the key name likewise. */
const char *nj_client_bind_label(int bind);
const char *nj_client_bind_default_key(int bind);

/* The config file path, or 0 when there is no home directory to put it in. */
int nj_client_settings_path(char *out, int cap);

/*
 * Read a settings file, keeping the defaults for anything missing.
 * `path` NULL means the default path.  Returns 1 when the file was read (even
 * when it was empty), 0 otherwise; the caller does not have to care.
 */
int nj_client_settings_load(nj_client_settings *s, const char *path);

/* Write the settings file, creating parent directories.  `path` NULL means
 * the default path.  Returns 1 on success. */
int nj_client_settings_save(const nj_client_settings *s, const char *path);

/*
 * <XDG_DATA_HOME or ~/.local/share>/nano_j2me/games, created when missing.
 * Returns 1 and writes an absolute path when a games directory is available.
 */
int nj_client_default_games_dir(char *out, int cap);

/* ------------------------------------------------------------------ */
/* The model                                                           */
/* ------------------------------------------------------------------ */

/* Keys the model distinguishes.  Everything else arrives as a character
 * through nj_client_ui_char (o/r/s/q, digits). */
typedef enum nj_client_key {
    NJ_CLIENT_KEY_UP = 1,
    NJ_CLIENT_KEY_DOWN,
    NJ_CLIENT_KEY_LEFT,
    NJ_CLIENT_KEY_RIGHT,
    NJ_CLIENT_KEY_PAGE_UP,
    NJ_CLIENT_KEY_PAGE_DOWN,
    NJ_CLIENT_KEY_HOME,
    NJ_CLIENT_KEY_END,
    NJ_CLIENT_KEY_ENTER,
    NJ_CLIENT_KEY_ESCAPE,
    NJ_CLIENT_KEY_BACKSPACE,
    NJ_CLIENT_KEY_HELP,      /* F1: the shortcut sheet over the library */
    NJ_CLIENT_KEY_FAVORITE   /* F2: star or unstar the selected jar      */
} nj_client_key;

/* What the client loop should do after a frame. */
typedef enum nj_client_action {
    NJ_CLIENT_ACTION_NONE = 0,
    NJ_CLIENT_ACTION_RUN,    /* `jar` in nj_client_ui_take() is set */
    NJ_CLIENT_ACTION_QUIT    /* the user closed the client          */
} nj_client_action;

typedef enum nj_client_mode {
    NJ_CLIENT_MODE_LIBRARY = 0,
    NJ_CLIENT_MODE_BROWSER,
    NJ_CLIENT_MODE_SETTINGS,
    NJ_CLIENT_MODE_BINDS    /* the key-mapping page opened from SETTINGS */
} nj_client_mode;

typedef struct nj_client_ui nj_client_ui;

/*
 * `settings` and `library` are borrowed for the lifetime of the model: the
 * model edits the settings (and saves them when a row changes) and rebuilds
 * the library when the user drops a directory or picks one in settings.
 * The library is NOT scanned by the model; the embedder starts and steps the
 * scan, so a slow disk cannot stall an event.
 */
nj_client_ui *nj_client_ui_new(nj_client_settings *settings,
                               nj_library *library);
void          nj_client_ui_free(nj_client_ui *ui);

/* Size of the surface the next draw() will fill.  Call on window resize. */
void nj_client_ui_resize(nj_client_ui *ui, int width, int height);
/* Milliseconds, for the toast lifetime and the scanning spinner. */
void nj_client_ui_tick(nj_client_ui *ui, nj_u4 now_ms);

/* The model's window contents, into `surface` (NJ_PIXFMT_ARGB8888). */
void nj_client_ui_draw(nj_client_ui *ui, nj_surface *surface);

/* Input.  `down` 0 is a release; `repeat` marks an auto-repeat. */
void nj_client_ui_key(nj_client_ui *ui, int key, int down, int repeat);
void nj_client_ui_char(nj_client_ui *ui, nj_u2 ch);
void nj_client_ui_pointer(nj_client_ui *ui, int x, int y, int down);
void nj_client_ui_pointer_move(nj_client_ui *ui, int x, int y);
void nj_client_ui_wheel(nj_client_ui *ui, int dy);
/*
 * A path dropped on the window.  A runnable .jar runs (the action comes out
 * of take()), a directory becomes the library and is rescanned, anything else
 * shows a toast and is left alone.
 */
void nj_client_ui_drop(nj_client_ui *ui, const char *path);

/* Messages.  `note` is a toast; `note_game_exit` is the same with the status
 * of a finished game in it. */
void nj_client_ui_note(nj_client_ui *ui, const char *message);
void nj_client_ui_note_game_exit(nj_client_ui *ui, const char *jar, int status);

/*
 * Poll for a pending action.  Returns 1 and fills `action` (and `jar`, for
 * RUN) when there is one; 0 when there is nothing to do.  A RUN also records
 * the jar as the most recent one and saves the settings, so the "最近玩过"
 * section is right the next time the window opens.
 */
int nj_client_ui_take(nj_client_ui *ui, nj_client_action *action,
                      char *jar, int jar_cap);

/* True once the user asked to quit. */
int nj_client_ui_quit(const nj_client_ui *ui);

/*
 * Layout queries, for mouse paths and for tests that do not want to hard-code
 * pixel coordinates: the rectangle of visible library row `index` (0-based,
 * "visible" meaning scrolled into view).  Returns 1 and writes all four when
 * the row is on screen, 0 otherwise.
 */
int nj_client_ui_row_rect(const nj_client_ui *ui, int index,
                          int *x, int *y, int *w, int *h);
/* Selected library index, or -1 when the library is empty. */
int nj_client_ui_selected(const nj_client_ui *ui);
/* Selected entries / current mode, for tests and for log lines. */
int nj_client_ui_mode(const nj_client_ui *ui);
int nj_client_ui_browser_selected(const nj_client_ui *ui);
/* Selected slot on the key-mapping page, or -1 when that page is not open. */
int nj_client_ui_selected_bind(const nj_client_ui *ui);
/*
 * The library's search text: typing in the library filters the rows by name,
 * file name and MIDlet class (case-insensitively), Backspace edits, Escape
 * clears before it quits.  Empty when the library is not being searched.
 */
const char *nj_client_ui_filter(const nj_client_ui *ui);
/* True while the F1 shortcut sheet covers the library. */
int nj_client_ui_help_visible(const nj_client_ui *ui);
/* True when the selected row carries a star (0 when nothing is selected). */
int nj_client_ui_selected_favorite(const nj_client_ui *ui);

/*
 * Record `path` as the most recently played jar (moved to the front, capped,
 * deduplicated) and save the settings.  take() does this for a RUN; the SDL
 * client loop also does it for a jar it starts itself (--client <jar>, and
 * the jar dropped on a running game that becomes the next run), so every
 * launch shows up under 最近玩过.
 */
void nj_client_ui_record_run(nj_client_ui *ui, const char *path);

/* ------------------------------------------------------------------ */
/* Key capture                                                         */
/* ------------------------------------------------------------------ */
/*
 * The key-mapping page records one slot at a time.  While it waits, the
 * embedder must feed every key press here as an SDL key name instead of
 * calling nj_client_ui_key(): the model needs the raw key, not the navigation
 * meaning, and it is the model that decides whether the key is allowed.
 *
 * nj_client_ui_capture_key() accepts the name, stores the binding, saves the
 * settings, ends the wait and returns 1.  It refuses - returning 0 with the
 * capture still waiting and a toast - a reserved client key (F3/F5/F10/F11/
 * F12/Tab), a modifier pressed on its own, an unknown or over-long name, and a
 * key another slot already uses.  The name of a slot's default key clears the
 * override rather than storing it, and "Escape" cancels.
 */
int  nj_client_ui_capturing(const nj_client_ui *ui);
int  nj_client_ui_capture_key(nj_client_ui *ui, const char *sdl_key_name);
void nj_client_ui_capture_cancel(nj_client_ui *ui);

/* ------------------------------------------------------------------ */
/* The SDL2 client loop                                                */
/* ------------------------------------------------------------------ */

/*
 * Run the desktop client until the user quits, then return NJ_OK.
 *
 * Trailing fields may be zero: width/height default to the runtime's screen,
 * scale 0 is auto, fps_cap -1 leaves the port default, keys_visible -1 leaves
 * the port default, launcher sizes 0 default to the settings' saved size and
 * then to 720x540.
 *
 * `first_jar` runs before the library window is shown, so `nj2me game.jar`
 * with --client starts the game directly and falls back to the library when
 * it exits.  `game->run_frames > 0` ends the whole client after the first
 * game, which is the seam tools/check_sdl2_client.sh uses: a scripted run of
 * N frames does not leave a window waiting for a user.
 *
 * Every game is nj_run() on a copy of `game` with `jar_path` replaced; the
 * caller's options are left alone.  The client saves the settings before it
 * returns.
 */
typedef struct nj_client_run_options {
    const nj_run_opts  *game;
    nj_client_settings *settings;
    nj_library         *library;
    const char         *first_jar;   /* may be NULL */
    int width, height;               /* game screen, 0 = defaults */
    int scale;                       /* 0 = auto */
    int fps_cap;                     /* -1 = port default */
    int keys_visible;                /* -1 = port default */
    const char *rms_dir;             /* NULL = per-game default */
    const char *fs_dir;              /* NULL = no filesystem */
    int launcher_width, launcher_height; /* 0 = 720x540 */
} nj_client_run_options;

nj_status nj_client_run(const nj_client_run_options *options);

#ifdef NJ_HAVE_SDL2
/*
 * Push the settings a game run uses into a freshly created SDL2 port: the
 * renderer (software/GPU+vsync), the integer-scale filter, fullscreen, muting
 * and the network switch travel as the port's NJ_SDL2_* environment knobs, and
 * the key bindings are installed through nj_port_sdl2_bind().  Returns the
 * heap size to put in nj_run_opts.heap_bytes (0 = the runtime default).
 *
 * The port reads its renderer/audio/network knobs when it is constructed, so
 * `port` may be NULL for a first call that only sets the environment, done
 * right before nj_port_sdl2(); a second call with the created port installs
 * the key bindings.  The desktop loop and the one-shot CLI both do this, so
 * `nj2me game.jar` obeys the same settings file as the library window.
 */
nj_size nj_client_apply_game_settings(nj_port *port,
                                      const nj_client_settings *settings);
#endif

#ifdef __cplusplus
}
#endif

#endif /* NJ_CLIENT_H */
