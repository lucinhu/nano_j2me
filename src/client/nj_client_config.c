/*
 * nano_j2me - src/client/nj_client_config.c
 *
 * The desktop client's settings file and the two XDG directories behind it.
 *
 * A launcher that forgets its game folder on every start is not a launcher, so
 * this is deliberately small and boring: one `key = value` per line, UTF-8
 * paths, unknown keys ignored.  The file is a few hundred bytes and is
 * rewritten whenever a settings row changes; atomically, through a .tmp file,
 * because a client that crashes mid-write must not come back with half a path.
 *
 * XDG says an empty or relative XDG_CONFIG_HOME/XDG_DATA_HOME means "not set";
 * this follows that, because some login scripts export an empty value and
 * writing to ".config" relative to the working directory is never what anyone
 * wanted.
 */
#include "nj2me/nj_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include "nj_posix_win32.h"
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <errno.h>
#endif

#define NJ_CONFIG_NAME "client.conf"
#define NJ_APP_DIR     "nano_j2me"

/* ---------------------------------------------------------------- */
/* Small path helpers                                                */
/* ---------------------------------------------------------------- */

static int cfg_copy(char *out, int cap, const char *src)
{
    size_t n;

    if (!out || cap <= 0) return 0;
    if (!src) src = "";
    n = strlen(src);
    if (n >= (size_t)cap) return 0;
    memcpy(out, src, n + 1u);
    return 1;
}

/* An XDG variable is only a value when it is an absolute path. */
static const char *cfg_env_dir(const char *name)
{
    const char *v = getenv(name);

    return (v && v[0] == '/') ? v : NULL;
}

static const char *cfg_home(char *out, int cap)
{
    const char *home = getenv("HOME");

#if defined(_WIN32)
    if (!home || home[0] == '\0') home = getenv("USERPROFILE");
#endif
    if (!home || home[0] == '\0') return NULL;
    return cfg_copy(out, cap, home) ? out : NULL;
}

static int cfg_join(char *out, int cap, const char *a, const char *b)
{
    int n;

    if (!out || cap <= 0 || !a || !b) return 0;
#if defined(_WIN32)
    n = snprintf(out, (size_t)cap, "%s\\%s", a, b);
#else
    n = snprintf(out, (size_t)cap, "%s/%s", a, b);
#endif
    return n > 0 && n < cap;
}

#if defined(_WIN32)
/* The same two operations through the wide API: the default settings path
 * lives under %APPDATA%, which is not always an ANSI path. */
static int cfg_mkdir_p(const char *path)
{
    return nj_win32_mkdir_p(path);
}

static int cfg_is_dir(const char *path)
{
    return nj_win32_is_dir(path);
}
#else
/* mkdir -p for the directories we own.  Existing components are fine. */
static int cfg_mkdir_p(const char *path)
{
    char tmp[NJ_CLIENT_PATH_MAX];
    size_t i;
    size_t n;

    if (!cfg_copy(tmp, (int)sizeof(tmp), path)) return 0;
    n = strlen(tmp);
    while (n > 1u && tmp[n - 1u] == '/') tmp[--n] = '\0';
    for (i = 1u; i < n; i++) {
        if (tmp[i] != '/') continue;
        tmp[i] = '\0';
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return 0;
        tmp[i] = '/';
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return 0;
    return 1;
}

static int cfg_is_dir(const char *path)
{
    struct stat st;

    return path && path[0] != '\0' && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}
#endif

/* ---------------------------------------------------------------- */
/* Public: paths and defaults                                        */
/* ---------------------------------------------------------------- */

int nj_client_settings_path(char *out, int cap)
{
    const char *xdg = cfg_env_dir("XDG_CONFIG_HOME");
    char home[NJ_CLIENT_PATH_MAX];
    char base[NJ_CLIENT_PATH_MAX];
    char dir[NJ_CLIENT_PATH_MAX];

    if (!out || cap <= 0) return 0;
#if defined(_WIN32)
    /* The Windows convention, and the only location that is stable when the
     * process is started from a shortcut with a different HOME: roaming
     * application data, not $HOME/.config.  Keep the old fallback below when
     * APPDATA is somehow absent. */
    {
        char appdata[NJ_CLIENT_PATH_MAX];
        if (nj_win32_getenv_utf8("APPDATA", appdata, (int)sizeof(appdata)) &&
            appdata[0] != '\0') {
            if (!cfg_join(base, (int)sizeof(base), appdata, NJ_APP_DIR)) return 0;
            return cfg_join(out, cap, base, NJ_CONFIG_NAME);
        }
    }
#endif
    if (xdg) {
        if (!cfg_copy(base, (int)sizeof(base), xdg)) return 0;
    } else if (cfg_home(home, (int)sizeof(home))) {
        if (!cfg_join(base, (int)sizeof(base), home, ".config")) return 0;
    } else {
        return 0;
    }
    if (!cfg_join(dir, (int)sizeof(dir), base, NJ_APP_DIR)) return 0;
    return cfg_join(out, cap, dir, NJ_CONFIG_NAME);
}

int nj_client_default_games_dir(char *out, int cap)
{
    const char *xdg = cfg_env_dir("XDG_DATA_HOME");
    char home[NJ_CLIENT_PATH_MAX];
    char base[NJ_CLIENT_PATH_MAX];
    char dir[NJ_CLIENT_PATH_MAX];
    char games[NJ_CLIENT_PATH_MAX];

    if (!out || cap <= 0) return 0;
#if defined(_WIN32)
    /* Windows convention: %APPDATA%\nano_j2me\games, the same root the
     * settings file uses.  The old HOME rule stays reachable when the
     * environment is missing. */
    {
        char appdata[NJ_CLIENT_PATH_MAX];
        if (nj_win32_getenv_utf8("APPDATA", appdata, (int)sizeof(appdata)) &&
            appdata[0] != '\0') {
            if (!cfg_join(dir, (int)sizeof(dir), appdata, NJ_APP_DIR)) return 0;
        } else
#endif
    {
        if (xdg) {
            if (!cfg_copy(base, (int)sizeof(base), xdg)) return 0;
        } else if (cfg_home(home, (int)sizeof(home))) {
            if (!cfg_join(base, (int)sizeof(base), home, ".local/share")) return 0;
        } else {
            return 0;
        }
        if (!cfg_join(dir, (int)sizeof(dir), base, NJ_APP_DIR)) return 0;
    }
#if defined(_WIN32)
    }
#endif
    if (!cfg_join(games, (int)sizeof(games), dir, "games")) return 0;
    if (!cfg_mkdir_p(games) && !cfg_is_dir(games)) return 0;
    return cfg_copy(out, cap, games);
}

void nj_client_settings_default(nj_client_settings *s)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    if (!nj_client_default_games_dir(s->games_dir, NJ_CLIENT_PATH_MAX)) {
        s->games_dir[0] = '\0';
    }
    memcpy(s->last_dir, s->games_dir, sizeof(s->last_dir));
    s->game_scale = 0;   /* fit the window */
    s->keys_visible = 1; /* the SDL2 port shows them by default */
    s->fps_cap = 0;      /* uncapped: the VM paces itself by the MIDP timer */
    s->filter_linear = 0; /* nearest: pixels stay pixels */
    s->fullscreen = 0;    /* a window, first */
    s->accel = 0;         /* software renderer: the one that always works */
    s->heap_mb = 0;       /* runtime default */
    s->mute = 0;          /* sound on */
    s->network = 1;       /* network on, as before this setting existed */
    s->gamepad = 1;       /* pads work without being asked, like the keys */
    s->launcher_w = 0;    /* the library window remembers its size once it has
                             been resized; 0 means "default 720 x 540"       */
    s->launcher_h = 0;
}

/* The bind table, in NJ_PORT_BIND_* order.  The labels are what the key page
 * shows; the key names are SDL's spelling of the port's defaults, and are
 * also what "restore the default" produces. */
static const struct {
    const char *label;
    const char *default_key;
} cfg_bind_table[NJ_PORT_BIND_COUNT] = {
    { "上",       "Up" },
    { "下",       "Down" },
    { "左",       "Left" },
    { "右",       "Right" },
    { "开火",     "Return" },
    { "左软键",   "F1" },
    { "右软键",   "Escape" },
    { "清除键",   "Backspace" },
    { "游戏键 A", "Z" },
    { "游戏键 B", "X" },
    { "游戏键 C", "C" },
    { "游戏键 D", "V" }
};

const char *nj_client_bind_label(int bind)
{
    if (bind < 0 || bind >= NJ_PORT_BIND_COUNT) return NULL;
    return cfg_bind_table[bind].label;
}

const char *nj_client_bind_default_key(int bind)
{
    if (bind < 0 || bind >= NJ_PORT_BIND_COUNT) return NULL;
    return cfg_bind_table[bind].default_key;
}

/* ---------------------------------------------------------------- */
/* Parsing and saving                                                */
/* ---------------------------------------------------------------- */

static char *cfg_trim(char *s)
{
    char *end;

    while (*s == ' ' || *s == '\t') s++;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' ||
                       end[-1] == '\n')) {
        end--;
    }
    *end = '\0';
    return s;
}

static int cfg_int(const char *s, int fallback)
{
    char *end = NULL;
    long v;

    if (!s || s[0] == '\0') return fallback;
    v = strtol(s, &end, 10);
    if (!end || *end != '\0' || v < -1000000L || v > 1000000L) return fallback;
    return (int)v;
}

/* The config file's key for a bind slot ("map_up", ...), in slot order. */
static const char *const cfg_bind_keys[NJ_PORT_BIND_COUNT] = {
    "map_up",    "map_down",       "map_left",     "map_right",
    "map_fire",  "map_soft_left",  "map_soft_right", "map_clear",
    "map_game_a", "map_game_b",    "map_game_c",   "map_game_d"
};

/* A settings switch: 0 or 1 in the file, anything else keeps the default. */
static int cfg_bool(const char *s, int fallback)
{
    int v = cfg_int(s, -1000000);
    if (v == 0) return 0;
    if (v == 1) return 1;
    return fallback;
}

static int cfg_bind_slot(const char *key)
{
    int i;
    for (i = 0; i < NJ_PORT_BIND_COUNT; i++) {
        if (strcmp(key, cfg_bind_keys[i]) == 0) return i;
    }
    return -1;
}

/* A key name this file is willing to keep: SDL prints printable ASCII names,
 * and a name that is not that is a corrupt line, not a binding. */
static int cfg_ok_key_name(const char *s)
{
    size_t n;
    if (!s) return 0;
    n = strlen(s);
    if (n == 0 || n >= NJ_CLIENT_BIND_NAME_MAX) return 0;
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c < 0x20u || c > 0x7eu || c == '=') return 0;
    }
    return 1;
}

/*
 * "900x700" -> the library window's size.  A missing or silly value keeps the
 * defaults (0 = 720x540) rather than opening a 4-pixel window.
 */
static void cfg_window(const char *val, int *w, int *h)
{
    int a = 0;
    int b = 0;
    if (!val || sscanf(val, "%dx%d", &a, &b) != 2) return;
    if (a < 320 || a > 7680 || b < 240 || b > 4320) return;
    *w = a;
    *h = b;
}

int nj_client_settings_load(nj_client_settings *s, const char *path)
{
    char file[NJ_CLIENT_PATH_MAX];
    char line[2048];
    FILE *f;
    int loaded = 0;

    if (!s) return 0;
    nj_client_settings_default(s);
    if (!path) {
        if (!nj_client_settings_path(file, (int)sizeof(file))) return 0;
        path = file;
    }
#if defined(_WIN32)
    f = nj_win32_fopen(path, "rb");
#else
    f = fopen(path, "rb");
#endif
    if (!f) return 0;
    loaded = 1;
    while (fgets(line, (int)sizeof(line), f)) {
        char *eq;
        char *key;
        char *val;

        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n') continue;
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        key = cfg_trim(line);
        val = cfg_trim(eq + 1);
        if (strcmp(key, "games_dir") == 0) {
            if (val[0] != '\0') cfg_copy(s->games_dir, NJ_CLIENT_PATH_MAX, val);
        } else if (strcmp(key, "last_dir") == 0) {
            if (val[0] != '\0') cfg_copy(s->last_dir, NJ_CLIENT_PATH_MAX, val);
        } else if (strcmp(key, "game_scale") == 0) {
            int v = cfg_int(val, s->game_scale);
            if (v >= 0 && v <= 4) s->game_scale = v;
        } else if (strcmp(key, "keys_visible") == 0) {
            s->keys_visible = cfg_int(val, s->keys_visible) ? 1 : 0;
        } else if (strcmp(key, "fps_cap") == 0) {
            int v = cfg_int(val, s->fps_cap);
            if (v == 0 || v == 30 || v == 60 || v == 120) s->fps_cap = v;
        } else if (strcmp(key, "filter_linear") == 0) {
            s->filter_linear = cfg_bool(val, s->filter_linear);
        } else if (strcmp(key, "fullscreen") == 0) {
            s->fullscreen = cfg_bool(val, s->fullscreen);
        } else if (strcmp(key, "accel") == 0) {
            s->accel = cfg_bool(val, s->accel);
        } else if (strcmp(key, "mute") == 0) {
            s->mute = cfg_bool(val, s->mute);
        } else if (strcmp(key, "network") == 0) {
            s->network = cfg_bool(val, s->network);
        } else if (strcmp(key, "gamepad") == 0) {
            s->gamepad = cfg_bool(val, s->gamepad);
        } else if (strcmp(key, "window") == 0) {
            cfg_window(val, &s->launcher_w, &s->launcher_h);
        } else if (strcmp(key, "heap_mb") == 0) {
            int v = cfg_int(val, s->heap_mb);
            if (v < 0) v = 0;
            if (v > 64) v = 64;
            s->heap_mb = v;
        } else {
            int slot = cfg_bind_slot(key);
            if (slot >= 0) {
                if (val[0] == '\0') {
                    s->binds[slot][0] = '\0';
                } else if (cfg_ok_key_name(val)) {
                    memcpy(s->binds[slot], val, strlen(val) + 1);
                }
            }
        }
        /* Unknown keys are ignored on purpose: an older client opening a file
         * written by a newer one must not lose the settings it does know. */
    }
    fclose(f);
    /*
     * A saved games_dir that no longer exists is a folder the user deleted or
     * a removable disk that went away.  Keep it only when it is a directory;
     * otherwise start from the default one instead of an empty window that
     * looks like the game was lost.
     */
    if (s->games_dir[0] == '\0' || !cfg_is_dir(s->games_dir)) {
        char fallback[NJ_CLIENT_PATH_MAX];
        if (nj_client_default_games_dir(fallback, (int)sizeof(fallback))) {
            if (s->games_dir[0] == '\0' ||
                strcmp(s->games_dir, fallback) != 0) {
                memcpy(s->games_dir, fallback, sizeof(s->games_dir));
            }
        }
    }
    return loaded;
}

int nj_client_settings_save(const nj_client_settings *s, const char *path)
{
    char file[NJ_CLIENT_PATH_MAX];
    char tmp[NJ_CLIENT_PATH_MAX + 8];
    char dir[NJ_CLIENT_PATH_MAX];
    FILE *f;
    int n;

    if (!s) return 0;
    if (!path) {
        if (!nj_client_settings_path(file, (int)sizeof(file))) return 0;
        path = file;
    }
    if (!cfg_copy(tmp, (int)sizeof(tmp), path)) return 0;
    if (!cfg_copy(dir, (int)sizeof(dir), path)) return 0;
    n = (int)strlen(dir);
#if defined(_WIN32)
    while (n > 0 && dir[n - 1] != '/' && dir[n - 1] != '\\') n--;
#else
    while (n > 0 && dir[n - 1] != '/') n--;
#endif
    if (n > 0) dir[n - 1] = '\0';
    if (dir[0] != '\0') (void)cfg_mkdir_p(dir);
    n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (n <= 0 || n >= (int)sizeof(tmp)) return 0;
#if defined(_WIN32)
    f = nj_win32_fopen(tmp, "wb");
#else
    f = fopen(tmp, "wb");
#endif
    if (!f) return 0;
    fprintf(f, "# nano_j2me desktop client settings\n");
    fprintf(f, "games_dir = %s\n", s->games_dir);
    fprintf(f, "last_dir = %s\n", s->last_dir);
    fprintf(f, "game_scale = %d\n", s->game_scale);
    fprintf(f, "keys_visible = %d\n", s->keys_visible ? 1 : 0);
    fprintf(f, "fps_cap = %d\n", s->fps_cap);
    fprintf(f, "filter_linear = %d\n", s->filter_linear ? 1 : 0);
    fprintf(f, "fullscreen = %d\n", s->fullscreen ? 1 : 0);
    fprintf(f, "accel = %d\n", s->accel ? 1 : 0);
    fprintf(f, "heap_mb = %d\n", s->heap_mb);
    fprintf(f, "mute = %d\n", s->mute ? 1 : 0);
    fprintf(f, "network = %d\n", s->network ? 1 : 0);
    fprintf(f, "gamepad = %d\n", s->gamepad ? 1 : 0);
    /* Only a window the user actually resized carries a size. */
    if (s->launcher_w > 0 && s->launcher_h > 0) {
        fprintf(f, "window = %dx%d\n", s->launcher_w, s->launcher_h);
    }
    {
        int i;
        /*
         * Only the slots the player changed are written; an absent map_ line
         * means "the port's default", which is also what an older client that
         * does not know these keys reads.
         */
        for (i = 0; i < NJ_PORT_BIND_COUNT; i++) {
            if (s->binds[i][0] != '\0') {
                fprintf(f, "%s = %s\n", cfg_bind_keys[i], s->binds[i]);
            }
        }
    }
    if (fclose(f) != 0) {
#if defined(_WIN32)
        (void)nj_win32_remove(tmp);
#else
        (void)remove(tmp);
#endif
        return 0;
    }
#if defined(_WIN32)
    /* A second save must replace the file written by the first one; CRT
     * rename() refuses to overwrite, so this goes through MoveFileExW. */
    if (nj_win32_replace(tmp, path) != 0) {
        (void)nj_win32_remove(tmp);
        return 0;
    }
#else
    if (rename(tmp, path) != 0) {
        (void)remove(tmp);
        return 0;
    }
#endif
    return 1;
}
