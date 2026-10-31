/*
 * nano_j2me - tests/test_client_config.c
 *
 * The desktop client remembers which folder its games live in and how the last
 * game was displayed.  That is a small promise, but a client that forgets it
 * is annoying in a way no error message fixes, so the file format and the XDG
 * lookups are tested rather than assumed:
 *
 *   - defaults come from <XDG_DATA_HOME>/nano_j2me/games and are created;
 *   - a round trip through client.conf gives back every field, UTF-8 paths
 *     included;
 *   - a missing file is not an error, a half-written one keeps the defaults,
 *     unknown keys are ignored and nonsense values are dropped;
 *   - a games_dir that no longer exists falls back to the default instead of
 *     showing an empty library forever.
 *
 * Usage: test_client_config
 */
#if !defined(_POSIX_C_SOURCE) && !defined(_GNU_SOURCE)
#  define _POSIX_C_SOURCE 200809L   /* mkdtemp, setenv, unsetenv under -std=c99 */
#endif
#include "nj2me/nj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static int is_dir(const char *path)
{
    struct stat st;

    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (!f) return 0;
    fclose(f);
    return 1;
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");

    if (!f) return;
    fputs(text, f);
    fclose(f);
}

static void test_defaults(const char *data_home)
{
    nj_client_settings s;
    char expect[1024];
    int n;

    printf("-- defaults and XDG lookups --\n");
    nj_client_settings_default(&s);
    n = snprintf(expect, sizeof(expect), "%s/nano_j2me/games", data_home);
    CHECK(n > 0 && (size_t)n < sizeof(expect), "the expected path fits");
    CHECK(strcmp(s.games_dir, expect) == 0, "games_dir is the XDG data folder");
    CHECK(is_dir(s.games_dir), "the games directory was created");
    CHECK(strcmp(s.last_dir, s.games_dir) == 0,
          "the browser starts in the games directory");
    CHECK(s.game_scale == 0, "scale defaults to fit-the-window");
    CHECK(s.keys_visible == 1, "the on-screen keypad starts visible");
    CHECK(s.fps_cap == 0, "frames default to uncapped");
    CHECK(s.filter_linear == 0, "the filter defaults to nearest-neighbour");
    CHECK(s.fullscreen == 0, "games start in a window");
    CHECK(s.accel == 0, "the software renderer is the default");
    CHECK(s.heap_mb == 0, "the heap defaults to the runtime size");
    CHECK(s.mute == 0, "sound starts on");
    CHECK(s.network == 1, "the network starts on");
    CHECK(s.gamepad == 1, "game controllers start enabled");
    CHECK(s.launcher_w == 0 && s.launcher_h == 0,
          "the library window starts at its default size");
    {
        int i;
        int empty = 1;
        for (i = 0; i < NJ_PORT_BIND_COUNT; i++) {
            if (s.binds[i][0] != '\0') empty = 0;
        }
        CHECK(empty, "every key slot starts on its default");
    }
}

static void test_paths(const char *config_home, const char *home)
{
    char path[1024];
    char expect[1024];
    int n;

    printf("-- the bind table --\n");
    {
        int i;
        int labelled = 1;
        int defaults = 1;
        for (i = 0; i < NJ_PORT_BIND_COUNT; i++) {
            if (nj_client_bind_label(i) == NULL) labelled = 0;
            if (nj_client_bind_default_key(i) == NULL) defaults = 0;
        }
        CHECK(labelled, "every slot has a label");
        CHECK(defaults, "every slot has a default key name");
        CHECK(strcmp(nj_client_bind_label(NJ_PORT_BIND_UP), "上") == 0,
              "the first slot is up");
        CHECK(strcmp(nj_client_bind_default_key(NJ_PORT_BIND_FIRE), "Return") == 0,
              "fire defaults to Return");
        CHECK(strcmp(nj_client_bind_default_key(NJ_PORT_BIND_SOFT_RIGHT),
                     "Escape") == 0, "soft-right defaults to Escape");
        CHECK(nj_client_bind_label(NJ_PORT_BIND_COUNT) == NULL,
              "an out-of-range slot has no label");
        CHECK(nj_client_bind_default_key(-1) == NULL,
              "a negative slot has no default");
    }

    printf("-- config paths --\n");
    CHECK(nj_client_settings_path(path, (int)sizeof(path)) == 1,
          "a config path exists");
    n = snprintf(expect, sizeof(expect), "%s/nano_j2me/client.conf", config_home);
    CHECK(n > 0 && strcmp(path, expect) == 0,
          "XDG_CONFIG_HOME wins over HOME");
    unsetenv("XDG_CONFIG_HOME");
    setenv("HOME", home, 1);
    CHECK(nj_client_settings_path(path, (int)sizeof(path)) == 1,
          "the path falls back to HOME");
    n = snprintf(expect, sizeof(expect), "%s/.config/nano_j2me/client.conf", home);
    CHECK(n > 0 && strcmp(path, expect) == 0, "and it is ~/.config");
    setenv("XDG_CONFIG_HOME", config_home, 1);
}

static void test_round_trip(const char *config_home)
{
    nj_client_settings a;
    nj_client_settings b;
    char games[1024];
    char path[1024];
    int n;

    printf("-- a round trip through client.conf --\n");
    nj_client_settings_default(&a);
    n = snprintf(games, sizeof(games), "%s/游戏/收藏", config_home);
    CHECK(n > 0 && (size_t)n < sizeof(games), "the UTF-8 test path fits");
    {
        /* mkdir -p: load() only keeps a games_dir that is a real directory. */
        char parent[1024];
        int pn = snprintf(parent, sizeof(parent), "%s/游戏", config_home);
        CHECK(pn > 0 && (size_t)pn < sizeof(parent), "the parent path fits");
        (void)mkdir(parent, 0755);
        (void)mkdir(games, 0755);
    }
    strcpy(a.games_dir, games);
    strcpy(a.last_dir, config_home);
    a.game_scale = 3;
    a.keys_visible = 0;
    a.fps_cap = 60;
    a.filter_linear = 1;
    a.fullscreen = 1;
    a.accel = 1;
    a.heap_mb = 32;
    a.mute = 1;
    a.network = 0;
    a.gamepad = 0;
    a.launcher_w = 900;
    a.launcher_h = 700;
    strcpy(a.binds[NJ_PORT_BIND_UP], "W");
    strcpy(a.binds[NJ_PORT_BIND_CLEAR], "Delete");
    strcpy(a.binds[NJ_PORT_BIND_GAME_A], "J");
    CHECK(nj_client_settings_save(&a, NULL) == 1, "settings save");
    n = snprintf(path, sizeof(path), "%s/nano_j2me/client.conf", config_home);
    CHECK(file_exists(path), "the file landed at the XDG path");

    memset(&b, 0, sizeof(b));
    CHECK(nj_client_settings_load(&b, NULL) == 1, "settings load");
    CHECK(strcmp(b.games_dir, games) == 0, "the UTF-8 games dir survives");
    CHECK(strcmp(b.last_dir, config_home) == 0, "last_dir survives");
    CHECK(b.game_scale == 3, "scale survives");
    CHECK(b.keys_visible == 0, "keys_visible survives");
    CHECK(b.fps_cap == 60, "fps cap survives");
    CHECK(b.filter_linear == 1, "the linear filter survives");
    CHECK(b.fullscreen == 1, "fullscreen survives");
    CHECK(b.accel == 1, "the accelerated renderer survives");
    CHECK(b.heap_mb == 32, "the heap size survives");
    CHECK(b.mute == 1, "mute survives");
    CHECK(b.network == 0, "the network switch survives");
    CHECK(b.gamepad == 0, "a disabled gamepad survives");
    CHECK(b.launcher_w == 900 && b.launcher_h == 700,
          "the library window size survives");
    CHECK(strcmp(b.binds[NJ_PORT_BIND_UP], "W") == 0, "a rebind survives");
    CHECK(strcmp(b.binds[NJ_PORT_BIND_CLEAR], "Delete") == 0,
          "a long key name survives");
    CHECK(strcmp(b.binds[NJ_PORT_BIND_GAME_A], "J") == 0,
          "a game-key rebind survives");
    CHECK(b.binds[NJ_PORT_BIND_DOWN][0] == '\0',
          "an absent slot stays on its default");
    {
        /* The file keeps only what changed, so an older client sees the
         * defaults it understands and nothing else. */
        char text[4096];
        FILE *f = fopen(path, "rb");
        size_t got = 0;
        if (f) {
            got = fread(text, 1, sizeof(text) - 1, f);
            text[got] = '\0';
            fclose(f);
        }
        CHECK(got > 0, "the file can be read back");
        CHECK(strstr(text, "map_up = W") != NULL, "the up rebind is in the file");
        CHECK(strstr(text, "map_clear = Delete") != NULL,
              "the clear rebind is in the file");
        CHECK(strstr(text, "map_down") == NULL,
              "a default slot is not written");
        CHECK(strstr(text, "filter_linear = 1") != NULL,
              "the filter switch is in the file");
        CHECK(strstr(text, "gamepad = 0") != NULL,
              "the gamepad switch is in the file");
        CHECK(strstr(text, "window = 900x700") != NULL,
              "the library window size is in the file");
    }

    /* A missing file is not an error: defaults are the answer. */
    {
        char missing[1024];
        nj_client_settings c;
        n = snprintf(missing, sizeof(missing), "%s/none/none.conf", config_home);
        CHECK(nj_client_settings_load(&c, missing) == 0, "missing file loads as 0");
        CHECK(c.game_scale == 0 && c.keys_visible == 1 && c.fps_cap == 0,
              "missing file keeps the defaults");
        CHECK(c.filter_linear == 0 && c.fullscreen == 0 && c.network == 1 &&
              c.binds[NJ_PORT_BIND_UP][0] == '\0',
              "missing file keeps the new defaults too");
        CHECK(c.gamepad == 1 && c.launcher_w == 0 && c.launcher_h == 0,
              "missing file keeps the gamepad and window defaults");
    }
}

static void test_bad_files(const char *config_home)
{
    char path[1024];
    int n;
    nj_client_settings s;

    printf("-- half-written and hostile files --\n");
    n = snprintf(path, sizeof(path), "%s/nano_j2me/client.conf", config_home);
    CHECK(n > 0, "the temp path fits");
    write_file(path,
               "# a file from the future\n"
               "games_dir = /tmp/definitely-not-here-93841\n"
               "game_scale = 9\n"
               "keys_visible = 3\n"
               "fps_cap = 55\n"
               "unknown_key = whatever\n"
               "last_dir = /tmp\n"
               "filter_linear = 7\n"
               "gamepad = 7\n"
               "window = 100x100\n"
               "heap_mb = 999\n"
               "mute = -2\n"
               "map_up = W\n"
               "map_fire = ThisNameIsWayTooLongForASlot\n"
               "map_left = Left Mouse Button\n");
    memset(&s, 0, sizeof(s));
    CHECK(nj_client_settings_load(&s, path) == 1, "the file is read");
    CHECK(s.game_scale == 0, "an out-of-range scale is ignored");
    CHECK(s.keys_visible == 1, "keys_visible = 3 means on");
    CHECK(s.fps_cap == 0, "an unsupported fps cap is ignored");
    CHECK(strcmp(s.last_dir, "/tmp") == 0, "known keys after unknown ones stick");
    CHECK(strstr(s.games_dir, "definitely-not-here") == NULL,
          "a games dir that does not exist falls back to the default");
    CHECK(s.filter_linear == 0, "a switch value other than 0/1 is ignored");
    CHECK(s.heap_mb == 64, "an oversized heap request clamps to 64 MB");
    CHECK(s.mute == 0, "a negative switch is ignored");
    CHECK(s.gamepad == 1, "a switch value other than 0/1 leaves it on");
    CHECK(s.launcher_w == 0 && s.launcher_h == 0,
          "a too-small window size is ignored");
    CHECK(strcmp(s.binds[NJ_PORT_BIND_UP], "W") == 0,
          "a bind line is read");
    CHECK(s.binds[NJ_PORT_BIND_FIRE][0] == '\0',
          "an over-long key name is dropped");
    CHECK(strcmp(s.binds[NJ_PORT_BIND_LEFT], "Left Mouse Button") == 0,
          "a multi-word key name is read (SDL can name keys that way)");

    /* A directory that DOES exist is kept. */
    n = snprintf(path, sizeof(path), "%s/nano_j2me/client.conf", config_home);
    CHECK(n > 0, "the temp path still fits");
    write_file(path, "games_dir = /tmp\nlast_dir = /tmp\n"
                      "window = 1920x1080\ngamepad = 0\n");
    memset(&s, 0, sizeof(s));
    CHECK(nj_client_settings_load(&s, path) == 1, "the second file is read");
    CHECK(strcmp(s.games_dir, "/tmp") == 0, "an existing games dir is kept");
    CHECK(s.launcher_w == 1920 && s.launcher_h == 1080,
          "a window size that parses is kept");
    CHECK(s.gamepad == 0, "gamepad = 0 is read");
}


static int read_file(const char *path, char *out, int cap)
{
    FILE *f = fopen(path, "rb");
    int n;
    if (!f) return 0;
    n = (int)fread(out, 1, (size_t)(cap - 1), f);
    fclose(f);
    if (n < 0) return 0;
    out[n] = '\0';
    return 1;
}

/* The library remembers what ran and what was starred; the file is the only
 * copy, so duplicates, relative paths and over-long lists must not survive a
 * round trip. */
static void test_recent_and_favorites(const char *config_home)
{
    char path[1024];
    char text[8192];
    char buf[8192];
    nj_client_settings s;
    int n;

    n = snprintf(path, sizeof(path), "%s/nano_j2me/client.conf", config_home);
    CHECK(n > 0 && (size_t)n < sizeof(path), "the recent/favorite path fits");

    /* Newest first, one duplicate, one relative path that must be dropped. */
    write_file(path,
               "games_dir = /tmp\n"
               "recent = /games/c.jar\n"
               "recent = /games/b.jar\n"
               "recent = /games/c.jar\n"
               "recent = relative/x.jar\n"
               "favorite = /games/a.jar\n"
               "favorite = /games/a.jar\n"
               "favorite = /games/space name.jar\n");
    memset(&s, 0, sizeof(s));
    CHECK(nj_client_settings_load(&s, path) == 1, "the recent file is read");
    CHECK(s.recent_count == 2, "a repeated recent path is kept once");
    CHECK(strcmp(s.recent[0], "/games/c.jar") == 0,
          "the first recent line is the newest");
    CHECK(strcmp(s.recent[1], "/games/b.jar") == 0,
          "the second recent line follows");
    CHECK(s.favorite_count == 2, "a repeated favorite is kept once");
    CHECK(strcmp(s.favorites[0], "/games/a.jar") == 0, "the first favorite is read");
    CHECK(strcmp(s.favorites[1], "/games/space name.jar") == 0,
          "a favorite with a space keeps its name");

    /* More recent lines than the list holds: the newest ten are kept. */
    {
        int i;
        size_t used = 0;
        n = snprintf(text, sizeof(text), "games_dir = /tmp\n");
        CHECK(n > 0 && (size_t)n < sizeof(text), "the cap fixture starts");
        used = (size_t)n;
        for (i = 0; i < NJ_CLIENT_RECENT_MAX + 2; i++) {
            n = snprintf(text + used, sizeof(text) - used, "recent = /r%d.jar\n", i);
            CHECK(n > 0 && used + (size_t)n < sizeof(text), "the cap fixture fits");
            used += (size_t)n;
        }
    }
    write_file(path, text);
    memset(&s, 0, sizeof(s));
    CHECK(nj_client_settings_load(&s, path) == 1, "the capped file is read");
    CHECK(s.recent_count == NJ_CLIENT_RECENT_MAX,
          "recent lines stop at the list size");
    CHECK(strcmp(s.recent[0], "/r0.jar") == 0, "the first kept recent entry is r0");
    CHECK(strcmp(s.recent[NJ_CLIENT_RECENT_MAX - 1], "/r9.jar") == 0,
          "the last kept recent entry is r9");

    /* Save and reload: order survives, and only non-empty lines are written. */
    nj_client_settings_default(&s);
    memcpy(s.recent[0], "/games/one.jar", 14);
    memcpy(s.recent[1], "/games/two.jar", 14);
    s.recent_count = 2;
    memcpy(s.favorites[0], "/games/star.jar", 15);
    s.favorite_count = 1;
    CHECK(nj_client_settings_save(&s, path) == 1, "the settings with recent saved");
    CHECK(read_file(path, buf, sizeof(buf)) == 1, "the saved file can be read");
    CHECK(strstr(buf, "recent = /games/one.jar") != NULL,
          "a recent line is written");
    CHECK(strstr(buf, "favorite = /games/star.jar") != NULL,
          "a favorite line is written");
    memset(&s, 0, sizeof(s));
    CHECK(nj_client_settings_load(&s, path) == 1, "the saved file is read back");
    CHECK(s.recent_count == 2, "the recent list round-trips");
    CHECK(strcmp(s.recent[0], "/games/one.jar") == 0, "recent order round-trips");
    CHECK(s.favorite_count == 1, "the favorite list round-trips");
    CHECK(strcmp(s.favorites[0], "/games/star.jar") == 0,
          "the favorite entry round-trips");

    /* A fresh profile writes neither line: they are state, not settings. */
    nj_client_settings_default(&s);
    CHECK(nj_client_settings_save(&s, path) == 1, "the defaults are saved");
    CHECK(read_file(path, buf, sizeof(buf)) == 1, "the default file can be read");
    CHECK(strstr(buf, "recent =") == NULL, "an empty recent list is not written");
    CHECK(strstr(buf, "favorite =") == NULL, "an empty favorite list is not written");
}

int main(void)
{
    char root[] = "/tmp/njclient-cfg-XXXXXX";
    char config_home[1024];
    char data_home[1024];
    char home[1024];
    int n;

    printf("== nj_client_settings: the settings file a launcher must not lose ==\n");
    if (!mkdtemp(root)) {
        printf("  SKIP: cannot create a temp directory\n");
        return 0;
    }
    n = snprintf(config_home, sizeof(config_home), "%s/config", root);
    CHECK(n > 0 && (size_t)n < sizeof(config_home), "config home fits");
    n = snprintf(data_home, sizeof(data_home), "%s/data", root);
    CHECK(n > 0 && (size_t)n < sizeof(data_home), "data home fits");
    n = snprintf(home, sizeof(home), "%s/home", root);
    CHECK(n > 0 && (size_t)n < sizeof(home), "home fits");
    (void)mkdir(config_home, 0755);
    (void)mkdir(data_home, 0755);
    (void)mkdir(home, 0755);
    setenv("XDG_CONFIG_HOME", config_home, 1);
    setenv("XDG_DATA_HOME", data_home, 1);
    setenv("HOME", home, 1);

    test_defaults(data_home);
    test_paths(config_home, home);
    test_round_trip(config_home);
    test_bad_files(config_home);
    test_recent_and_favorites(config_home);

    printf("  %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
