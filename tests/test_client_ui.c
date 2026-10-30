/*
 * nano_j2me - test_client_ui.c
 *
 * Headless checks for the portable desktop-client UI model (nj_client_ui):
 * drawing into a malloc'd ARGB8888 surface, keyboard/mouse/wheel/char input,
 * browsing a temporary directory tree, settings edits + persistence, drag and
 * drop, toasts and resizes.  No window, no SDL: exactly the seam the embedder
 * (nj_sdl2_client.c) uses.
 *
 * Frames are dumped as binary PPM (P6) to $NJ_TEST_OUT, default
 * /tmp/nj_client_ui_dump, so a human can look at what the model drew.
 *
 * The test only uses the public headers.  While src/client/nj_library.c and
 * src/client/nj_client_config.c are being written in parallel it is linked
 * against /tmp/stubs.c; it is expected to link and pass unchanged once the
 * real modules exist.
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "nj2me/nj_client.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

/* ------------------------------------------------------------------ */
/* Check bookkeeping (same shape as tests/test_detect.c)                */
/* ------------------------------------------------------------------ */

static int checks;
static int failures;

#define CHECK(cond, what)                                                  \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));        \
        }                                                                  \
    } while (0)

/* Palette facts the layout contract fixes; used to prove pixels landed. */
#define COL_BG      0xFF191C22u
#define COL_CHROME  0xFF232733u
#define COL_PATHBAR 0xFF20242Du
#define COL_SEL     0xFF2E6EA6u

#define TEST_W 720
#define TEST_H 540

/* ------------------------------------------------------------------ */
/* Small surface / PPM helpers                                         */
/* ------------------------------------------------------------------ */

static nj_surface *surface_new(int w, int h)
{
    nj_surface *s = (nj_surface *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->pixels = calloc((size_t)w * (size_t)h, 4u);
    if (!s->pixels) {
        free(s);
        return NULL;
    }
    s->width = w;
    s->height = h;
    s->stride = w * 4;
    s->format = NJ_PIXFMT_ARGB8888;
    s->bpp = 4;
    return s;
}

static void surface_free(nj_surface *s)
{
    if (!s) return;
    free(s->pixels);
    free(s);
}

static nj_u4 px(const nj_surface *s, int x, int y)
{
    const nj_u4 *row;
    if (!s || !s->pixels || x < 0 || y < 0 || x >= s->width || y >= s->height) {
        return 0;
    }
    row = (const nj_u4 *)((const unsigned char *)s->pixels +
                          (size_t)y * (size_t)s->stride);
    return row[x];
}

static int count_not(const nj_surface *s, nj_u4 argb)
{
    int x, y, n = 0;
    if (!s || !s->pixels) return 0;
    for (y = 0; y < s->height; y++) {
        const nj_u4 *row = (const nj_u4 *)((const unsigned char *)s->pixels +
                                           (size_t)y * (size_t)s->stride);
        for (x = 0; x < s->width; x++) {
            if (row[x] != argb) n++;
        }
    }
    return n;
}

static int count_region_not(const nj_surface *s, int x0, int y0, int x1,
                            int y1, nj_u4 argb)
{
    int x, y, n = 0;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->width) x1 = s->width;
    if (y1 > s->height) y1 = s->height;
    for (y = y0; y < y1; y++) {
        const nj_u4 *row = (const nj_u4 *)((const unsigned char *)s->pixels +
                                           (size_t)y * (size_t)s->stride);
        for (x = x0; x < x1; x++) {
            if (row[x] != argb) n++;
        }
    }
    return n;
}

static int diff_pixels(const nj_surface *a, const nj_surface *b)
{
    int x, y, n = 0;
    if (!a || !b || a->width != b->width || a->height != b->height) return -1;
    for (y = 0; y < a->height; y++) {
        const nj_u4 *ra = (const nj_u4 *)((const unsigned char *)a->pixels +
                                          (size_t)y * (size_t)a->stride);
        const nj_u4 *rb = (const nj_u4 *)((const unsigned char *)b->pixels +
                                          (size_t)y * (size_t)b->stride);
        for (x = 0; x < a->width; x++) {
            if (ra[x] != rb[x]) n++;
        }
    }
    return n;
}

static void dump_ppm(const char *dir, const char *name, const nj_surface *s)
{
    char path[1200];
    FILE *f;
    int x, y;

    if (!dir || !dir[0] || !s || !s->pixels) return;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (!f) {
        printf("  skip PPM %s (cannot write)\n", path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", s->width, s->height);
    for (y = 0; y < s->height; y++) {
        const nj_u4 *row = (const nj_u4 *)((const unsigned char *)s->pixels +
                                           (size_t)y * (size_t)s->stride);
        for (x = 0; x < s->width; x++) {
            nj_u4 p = row[x];
            fputc((int)((p >> 16) & 0xFFu), f);
            fputc((int)((p >> 8) & 0xFFu), f);
            fputc((int)(p & 0xFFu), f);
        }
    }
    fclose(f);
    printf("  wrote %s\n", path);
}

static void draw_dump(nj_client_ui *ui, nj_surface *s, const char *dir,
                      const char *name)
{
    nj_client_ui_draw(ui, s);
    dump_ppm(dir, name, s);
}

/* ------------------------------------------------------------------ */
/* Temporary environment: a fake HOME/XDG plus a games directory tree   */
/* ------------------------------------------------------------------ */

typedef struct test_env {
    int ok;            /* temp tree + fixture jar ready                     */
    int have_cjk;      /* the CJK fixture jar was copied                    */
    int have_broken;   /* the empty broken.jar was created                  */
    int expect_count;  /* entries the scan should append for games/         */
    char root[512];
    char games[700];
    char sub[760];
    char other[760];
    char home[700];
    char config[700];
    char data[700];
    char out[900];
    char jar[800];       /* games/alphademo.jar                            */
    char jar_drop[800];  /* outside the games dir, for drop tests          */
    char cjk_games[900]; /* games/1942<CJK>_10403.jar                      */
    char broken[800];    /* games/broken.jar                               */
    char txt[800];       /* root/readme.txt                                */
    char longdir[800];   /* root/longdir, holds a very long file name      */
    char longjar[1400];  /* longdir/<120 chars>.jar                        */
} test_env;

static int file_exists(const char *path)
{
    FILE *f;
    if (!path || !path[0]) return 0;
    f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int copy_file(const char *src, const char *dst)
{
    FILE *in;
    FILE *out;
    char buf[8192];
    size_t n;

    in = fopen(src, "rb");
    if (!in) return 0;
    out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) break;
    }
    fclose(in);
    if (fclose(out) != 0) return 0;
    return n == 0;
}

static void make_dir(const char *path)
{
    if (!path || !path[0]) return;
    (void)mkdir(path, 0755);
}

static int write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fputs(text, f);
    fclose(f);
    return 1;
}

static int env_init(test_env *e)
{
#if defined(_WIN32)
    (void)e;
    return 0;
#else
    char tmpl[] = "/tmp/njclientuiXXXXXX";
    char *root;
    const char *outdir;
    const char *src;

    memset(e, 0, sizeof(*e));
    root = mkdtemp(tmpl);
    if (!root) return 0;
    snprintf(e->root, sizeof(e->root), "%s", root);
    snprintf(e->games, sizeof(e->games), "%s/games", e->root);
    snprintf(e->sub, sizeof(e->sub), "%s/sub", e->games);
    snprintf(e->other, sizeof(e->other), "%s/other", e->root);
    snprintf(e->home, sizeof(e->home), "%s/home", e->root);
    snprintf(e->config, sizeof(e->config), "%s/config", e->root);
    snprintf(e->data, sizeof(e->data), "%s/data", e->root);
    snprintf(e->jar, sizeof(e->jar), "%s/alphademo.jar", e->games);
    snprintf(e->jar_drop, sizeof(e->jar_drop), "%s/dropped jar.jar", e->root);
    snprintf(e->cjk_games, sizeof(e->cjk_games), "%s/1942空战_10403.jar",
             e->games);
    snprintf(e->broken, sizeof(e->broken), "%s/broken.jar", e->games);
    snprintf(e->txt, sizeof(e->txt), "%s/readme.txt", e->root);

    make_dir(e->games);
    make_dir(e->sub);
    make_dir(e->other);
    make_dir(e->home);
    make_dir(e->config);
    make_dir(e->data);
    make_dir(e->root);

    outdir = getenv("NJ_TEST_OUT");
    if (!outdir || !outdir[0]) outdir = "/tmp/nj_client_ui_dump";
    snprintf(e->out, sizeof(e->out), "%s", outdir);
    if (mkdir(e->out, 0755) != 0 && errno != EEXIST) {
        snprintf(e->out, sizeof(e->out), "%s/dump", e->root);
        make_dir(e->out);
    }

    /* Environment before anything reads the config module. */
    setenv("HOME", e->home, 1);
    setenv("XDG_CONFIG_HOME", e->config, 1);
    setenv("XDG_DATA_HOME", e->data, 1);

    /* Fixture jars: the repo's real ones, copied into the games dir. */
    src = "tests/fixtures/alphademo.jar";
    if (!file_exists(src)) {
        const char *env = getenv("NJ_FIXTURE_JAR");
        if (env && env[0]) src = env;
    }
    if (file_exists(src) && copy_file(src, e->jar)) {
        if (copy_file(src, e->jar_drop)) e->ok = 1;
    }
    src = "tests/games/1942空战_10403.jar";
    if (file_exists(src) && copy_file(src, e->cjk_games)) e->have_cjk = 1;

    if (write_text(e->broken, "")) e->have_broken = 1;
    (void)write_text(e->txt, "not a jar\n");

    /* A jar whose 124-character basename needs truncation in every view.
     * (nj_library skips names >= NJ_LIB_FILE_MAX, so this stays below it.) */
    snprintf(e->longdir, sizeof(e->longdir), "%s/longdir", e->root);
    make_dir(e->longdir);
    {
        char base[300];
        int i;
        for (i = 0; i < 120; i++) base[i] = 'x';
        base[120] = '\0';
        snprintf(e->longjar, sizeof(e->longjar), "%s/%s.jar", e->longdir,
                 base);
        if (e->ok) (void)copy_file(e->jar, e->longjar);
    }

    e->expect_count = (e->ok ? 1 : 0) + e->have_broken + e->have_cjk;
    return e->ok;
#endif
}

/* ------------------------------------------------------------------ */
/* Model helpers                                                       */
/* ------------------------------------------------------------------ */

static void finish_scan(nj_library *lib)
{
    int guard = 4096;
    while (nj_lib_scan_busy(lib) && guard-- > 0) {
        if (nj_lib_scan_step(lib, 4) <= 0 && !nj_lib_scan_busy(lib)) break;
    }
}

static int scan_started(const nj_library *lib)
{
    if (!lib) return 0;
    return lib->scan_active || lib->pending_count > 0 || nj_lib_scan_busy(lib);
}

static void key(nj_client_ui *ui, int k)
{
    nj_client_ui_key(ui, k, 1, 0);
}

static void lib_select(nj_client_ui *ui, int index)
{
    int i;
    key(ui, NJ_CLIENT_KEY_HOME);
    for (i = 0; i < index; i++) key(ui, NJ_CLIENT_KEY_DOWN);
}

static int find_entry(const nj_library *lib, int want_valid, int skip)
{
    int i;
    if (!lib) return -1;
    for (i = 0; i < lib->count; i++) {
        if (i == skip) continue;
        if (want_valid < 0 || (lib->entries[i].valid ? 1 : 0) == want_valid) {
            return i;
        }
    }
    return -1;
}

static int find_entry_file(const nj_library *lib, const char *file)
{
    int i;
    if (!lib || !file) return -1;
    for (i = 0; i < lib->count; i++) {
        if (strcmp(lib->entries[i].file, file) == 0) return i;
    }
    return -1;
}

static int take_action(nj_client_ui *ui, nj_client_action *action, char *jar,
                       int cap)
{
    *action = NJ_CLIENT_ACTION_NONE;
    if (jar && cap > 0) jar[0] = '\0';
    return nj_client_ui_take(ui, action, jar, cap);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    test_env env;
    nj_client_settings settings;
    nj_library *lib;
    nj_client_ui *ui;
    nj_surface *s;
    nj_surface *snap_a = NULL;
    nj_surface *snap_b = NULL;
    nj_client_action action;
    char jar[1200];
    char dflt[1200];
    int rx = 0, ry = 0, rw = 0, rh = 0;

    printf("test_client_ui: portable launcher model\n");
    fflush(stdout);

    if (!env_init(&env)) {
        printf("  note: fixture jar or temp tree unavailable; skipping the\n"
               "        interactive model checks (the reduced checks below\n"
               "        only prove NULL safety and the quit latch)\n");
        memset(&settings, 0, sizeof(settings));
        nj_client_settings_default(&settings);
        ui = nj_client_ui_new(&settings, NULL);
        CHECK(ui != NULL, "model allocates without a library");
        if (ui) {
            s = surface_new(TEST_W, TEST_H);
            CHECK(s != NULL, "surface allocates");
            if (s) {
                nj_client_ui_resize(ui, TEST_W, TEST_H);
                nj_client_ui_tick(ui, 1000);
                nj_client_ui_draw(ui, s);
                CHECK(count_not(s, COL_BG) > 1000, "reduced: empty state draws");
                surface_free(s);
            }
            nj_client_ui_key(ui, NJ_CLIENT_KEY_ESCAPE, 1, 0);
            CHECK(nj_client_ui_quit(ui) == 1, "ESCAPE quits");
            nj_client_ui_free(ui);
        }
        printf("  %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }

    printf("  -- setup --\n");
    printf("  tree: %s\n", env.root);
    printf("  dump: %s\n", env.out);
    CHECK(env.ok, "temporary tree + fixture jar ready");
    CHECK(env.expect_count >= 2, "fixture tree has at least two jars");

    nj_client_settings_default(&settings);
    CHECK(settings.games_dir[0] != '\0', "settings default games dir set");
    CHECK(settings.last_dir[0] != '\0', "settings default last dir set");
    CHECK(settings.game_scale == 0, "settings default scale is auto");
    CHECK(settings.keys_visible == 1, "settings default keys visible");
    CHECK(settings.fps_cap == 0, "settings default fps uncapped");

    /* Point the library at our tree rather than the XDG default. */
    snprintf(settings.games_dir, sizeof(settings.games_dir), "%s", env.games);
    snprintf(settings.last_dir, sizeof(settings.last_dir), "%s", env.games);

    lib = nj_library_new();
    CHECK(lib != NULL, "library allocates");
    if (!lib) {
        printf("  %d checks, %d failures\n", checks, failures);
        return 1;
    }
    ui = nj_client_ui_new(&settings, lib);
    CHECK(ui != NULL, "model allocates");
    if (!ui) {
        nj_library_free(lib);
        printf("  %d checks, %d failures\n", checks, failures);
        return 1;
    }
    nj_client_ui_resize(ui, TEST_W, TEST_H);
    s = surface_new(TEST_W, TEST_H);
    snap_a = surface_new(TEST_W, TEST_H);
    snap_b = surface_new(TEST_W, TEST_H);
    CHECK(s && snap_a && snap_b, "surfaces allocate");
    if (!s || !snap_a || !snap_b) {
        printf("  %d checks, %d failures\n", checks, failures);
        return 1;
    }

    /* -------------------------------------------------------------- */
    /* Scanning state: library empty while begin() ran but step didn't */
    /* -------------------------------------------------------------- */
    printf("  -- scanning empty library --\n");
    CHECK(nj_lib_scan_begin(lib, env.games) == 1, "scan_begin accepts games dir");
    CHECK(nj_lib_scan_busy(lib) == 1, "scan is busy before stepping");
    CHECK(lib->count == 0, "scan_begin cleared the library");
    nj_client_ui_tick(ui, 1000);
    draw_dump(ui, s, env.out, "01_scanning.ppm");
    CHECK(count_not(s, COL_BG) > 2000, "scanning state draws");
    CHECK(px(s, 5, 5) == COL_CHROME, "header chrome drawn while scanning");
    CHECK(nj_client_ui_selected(ui) == -1, "no selection while library empty");

    /* Entries grow between two draws of the same scan. */
    CHECK(nj_lib_scan_step(lib, 1) >= 0, "scan step ran");
    CHECK(lib->count >= 1, "scan step appended an entry");
    draw_dump(ui, s, env.out, "01b_scanning_partial.ppm");
    CHECK(count_not(s, COL_BG) > 2000, "partially scanned library draws");
    CHECK(nj_client_ui_selected(ui) == 0, "first entry becomes selectable");

    finish_scan(lib);
    CHECK(nj_lib_scan_busy(lib) == 0, "scan finished");
    CHECK(lib->count == env.expect_count, "scan found every fixture jar");

    /* -------------------------------------------------------------- */
    /* Library frame + layout                                          */
    /* -------------------------------------------------------------- */
    printf("  -- library frame --\n");
    nj_client_ui_draw(ui, s);
    dump_ppm(env.out, "02_library.ppm", s);
    CHECK(count_not(s, COL_BG) > 5000, "library frame is not blank");
    CHECK(px(s, 5, 5) == COL_CHROME, "header background");
    CHECK(px(s, 5, TEST_H - 5) == COL_CHROME, "footer background");
    CHECK(count_region_not(s, 16, 18, 300, 40, COL_CHROME) > 100,
          "header title text visible");
    CHECK(count_region_not(s, 16, TEST_H - 32, 520, TEST_H - 8, COL_CHROME) >
              100,
          "footer hints visible");
    CHECK(nj_client_ui_selected(ui) == 0, "selection defaults to first row");
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_LIBRARY, "library mode");
    CHECK(nj_client_ui_row_rect(ui, 0, &rx, &ry, &rw, &rh) == 1,
          "row_rect(0) answers");

    /* re-fetch into named locals for the checks below */
    {
        int rx0 = 0, ry0 = 0, rw0 = 0, rh0 = 0;
        CHECK(nj_client_ui_row_rect(ui, 0, &rx0, &ry0, &rw0, &rh0) == 1,
              "row_rect(0) again");
        CHECK(rx0 == 8, "row starts 8 px from the left edge");
        CHECK(ry0 >= 56, "row starts below the 56 px header");
        CHECK(ry0 + rh0 <= TEST_H - 40, "row ends above the footer");
        CHECK(rw0 == TEST_W - 16, "row spans the window minus margins");
        CHECK(rh0 == 48, "row height 48");
        CHECK(px(s, 600, ry0 + 24) == COL_SEL, "selected row uses the blue");
        CHECK(count_region_not(s, 68, ry0 + 6, 600, ry0 + 30, COL_SEL) > 20,
              "row title text drawn over the selection");
    }
    CHECK(nj_client_ui_row_rect(ui, 1000, &rx, &ry, &rw, &rh) == 0,
          "row_rect rejects an out-of-range index");

    /* Broken entry stays in the list but is invalid. */
    {
        int b = find_entry_file(lib, "broken.jar");
        CHECK(b >= 0, "broken jar is listed");
        if (b >= 0) {
            CHECK(lib->entries[b].valid == 0, "broken jar is marked not runnable");
        }
    }

    /* CJK name path: the row must draw and the font must have the glyphs. */
    if (env.have_cjk) {
        int c = -1;
        int i;
        for (i = 0; i < lib->count; i++) {
            const unsigned char *p = (const unsigned char *)lib->entries[i].file;
            while (*p) {
                if (*p >= 0x80u) {
                    c = i;
                    break;
                }
                p++;
            }
            if (c >= 0) break;
        }
        CHECK(c >= 0, "CJK fixture named in the library");
        draw_dump(ui, s, env.out, "03_library_cjk.ppm");
    } else {
        printf("  skip: CJK fixture (tests/games/1942空战_10403.jar) missing\n");
    }

    /* -------------------------------------------------------------- */
    /* Keyboard navigation                                             */
    /* -------------------------------------------------------------- */
    printf("  -- keyboard --\n");
    key(ui, NJ_CLIENT_KEY_HOME);
    CHECK(nj_client_ui_selected(ui) == 0, "HOME selects first");
    key(ui, NJ_CLIENT_KEY_DOWN);
    CHECK(nj_client_ui_selected(ui) == 1, "DOWN moves down one");
    key(ui, NJ_CLIENT_KEY_UP);
    CHECK(nj_client_ui_selected(ui) == 0, "UP moves up one");
    key(ui, NJ_CLIENT_KEY_UP);
    CHECK(nj_client_ui_selected(ui) == 0, "UP clamps at the top");
    key(ui, NJ_CLIENT_KEY_END);
    CHECK(nj_client_ui_selected(ui) == lib->count - 1, "END selects last");
    key(ui, NJ_CLIENT_KEY_PAGE_DOWN);
    CHECK(nj_client_ui_selected(ui) == lib->count - 1, "PAGE_DOWN clamps last");
    key(ui, NJ_CLIENT_KEY_PAGE_UP);
    CHECK(nj_client_ui_selected(ui) == 0, "PAGE_UP returns to first");

    /* ENTER on an invalid row does nothing. */
    {
        int bad = find_entry(lib, 0, -1);
        if (bad >= 0) {
            lib_select(ui, bad);
            CHECK(nj_client_ui_selected(ui) == bad, "selected broken row");
            key(ui, NJ_CLIENT_KEY_ENTER);
            CHECK(take_action(ui, &action, jar, (int)sizeof(jar)) == 0,
                  "ENTER on a broken row emits no action");
            CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_LIBRARY,
                  "still in the library after a broken row");
        }
    }

    /* ENTER on a valid row emits RUN and keeps the selection. */
    {
        int good = find_entry(lib, 1, -1);
        CHECK(good >= 0, "valid row present");
        if (good >= 0) {
            lib_select(ui, good);
            key(ui, NJ_CLIENT_KEY_ENTER);
            CHECK(take_action(ui, &action, jar, (int)sizeof(jar)) == 1,
                  "ENTER on a valid row emits an action");
            CHECK(action == NJ_CLIENT_ACTION_RUN, "action is RUN");
            CHECK(strcmp(jar, lib->entries[good].path) == 0,
                  "RUN carries the jar path");
            CHECK(nj_client_ui_selected(ui) == good,
                  "selection kept after RUN");
            CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_LIBRARY,
                  "still in the library after RUN");
            CHECK(take_action(ui, &action, jar, (int)sizeof(jar)) == 0,
                  "the pending action is consumed once");
        }
    }

    /* release events are ignored */
    {
        int before = nj_client_ui_selected(ui);
        nj_client_ui_key(ui, NJ_CLIENT_KEY_DOWN, 0, 0);
        CHECK(nj_client_ui_selected(ui) == before, "key release ignored");
    }

    /* -------------------------------------------------------------- */
    /* Mouse: click selects, second click on the selection runs        */
    /* -------------------------------------------------------------- */
    printf("  -- pointer --\n");
    {
        int rx0 = 0, ry0 = 0, rw0 = 0, rh0 = 0;
        int sel = nj_client_ui_selected(ui);
        int target = (sel == 0) ? (lib->count - 1) : 0;
        CHECK(nj_client_ui_row_rect(ui, target, &rx0, &ry0, &rw0, &rh0) == 1,
              "target row visible");
        nj_client_ui_pointer(ui, rx0 + 30, ry0 + 20, 1);
        CHECK(nj_client_ui_selected(ui) == target,
              "click on a row selects it");
        CHECK(take_action(ui, &action, jar, (int)sizeof(jar)) == 0,
                  "first click does not run");
        /* second click on the now-selected valid row runs it */
        {
            int good = find_entry(lib, 1, -1);
            if (good >= 0) {
                CHECK(nj_client_ui_row_rect(ui, good, &rx0, &ry0, &rw0,
                                            &rh0) == 1,
                      "valid row visible for click");
                if (nj_client_ui_selected(ui) != good) {
                    nj_client_ui_pointer(ui, rx0 + 30, ry0 + 20, 1);
                    CHECK(nj_client_ui_selected(ui) == good,
                          "click selects the valid row");
                    CHECK(take_action(ui, &action, jar, (int)sizeof(jar)) == 0,
                          "selection click emits nothing");
                }
                nj_client_ui_pointer(ui, rx0 + 30, ry0 + 20, 1);
                CHECK(take_action(ui, &action, jar, (int)sizeof(jar)) == 1,
                      "second click on the selection runs");
                CHECK(action == NJ_CLIENT_ACTION_RUN, "click RUN action");
                CHECK(strcmp(jar, lib->entries[good].path) == 0,
                      "click RUN carries the jar path");
            }
        }
        /* down == 0 is a release and is ignored */
        {
            int before = nj_client_ui_selected(ui);
            int other = (before == 0) ? lib->count - 1 : 0;
            CHECK(nj_client_ui_row_rect(ui, other, &rx0, &ry0, &rw0, &rh0) ==
                      1,
                  "other row visible");
            nj_client_ui_pointer(ui, rx0 + 30, ry0 + 20, 0);
            CHECK(nj_client_ui_selected(ui) == before,
                  "pointer release does not select");
            CHECK(take_action(ui, &action, jar, (int)sizeof(jar)) == 0,
                  "pointer release emits nothing");
        }
    }

    /* Hover changes the frame but not the selection. */
    {
        int sel = nj_client_ui_selected(ui);
        int other = (sel == 0) ? lib->count - 1 : 0;
        int rx0 = 0, ry0 = 0, rw0 = 0, rh0 = 0;
        CHECK(nj_client_ui_row_rect(ui, other, &rx0, &ry0, &rw0, &rh0) == 1,
              "hover row visible");
        nj_client_ui_pointer_move(ui, -1, -1);
        nj_client_ui_draw(ui, s);
        memcpy(snap_a->pixels, s->pixels,
               (size_t)TEST_W * (size_t)TEST_H * 4u);
        nj_client_ui_pointer_move(ui, rx0 + 40, ry0 + 20);
        nj_client_ui_draw(ui, s);
        CHECK(diff_pixels(snap_a, s) > 500, "hover changes the row colour");
        CHECK(nj_client_ui_selected(ui) == sel, "hover does not move selection");
        nj_client_ui_pointer_move(ui, -1, -1);
        nj_client_ui_draw(ui, s);
        CHECK(diff_pixels(snap_a, s) == 0, "leaving the row restores the frame");
    }

    /* -------------------------------------------------------------- */
    /* Wheel: moves the selection, not just the view                   */
    /* -------------------------------------------------------------- */
    printf("  -- wheel --\n");
    nj_client_ui_pointer_move(ui, -1, -1);
    key(ui, NJ_CLIENT_KEY_HOME);
    nj_client_ui_wheel(ui, -1);
    CHECK(nj_client_ui_selected(ui) == 1, "wheel down moves selection down");
    nj_client_ui_wheel(ui, 1);
    CHECK(nj_client_ui_selected(ui) == 0, "wheel up moves selection up");
    nj_client_ui_wheel(ui, 1000);
    CHECK(nj_client_ui_selected(ui) == 0, "wheel up clamps at the top");
    nj_client_ui_wheel(ui, -1000);
    CHECK(nj_client_ui_selected(ui) == lib->count - 1,
          "wheel down clamps at the bottom");

    /* -------------------------------------------------------------- */
    /* Toast lifetime                                                  */
    /* -------------------------------------------------------------- */
    printf("  -- toast --\n");
    key(ui, NJ_CLIENT_KEY_HOME);
    nj_client_ui_tick(ui, 10000);   /* let any earlier toast expire first */
    nj_client_ui_draw(ui, s);
    memcpy(snap_a->pixels, s->pixels, (size_t)TEST_W * (size_t)TEST_H * 4u);
    nj_client_ui_note(ui, "测试提示：拖入游戏即可加入");
    nj_client_ui_draw(ui, s);
    CHECK(diff_pixels(snap_a, s) > 1000, "a toast changes the frame");
    nj_client_ui_tick(ui, 13000);
    nj_client_ui_draw(ui, s);
    CHECK(diff_pixels(snap_a, s) > 1000, "toast still visible before 3500 ms");
    nj_client_ui_tick(ui, 13500);
    nj_client_ui_draw(ui, s);
    CHECK(diff_pixels(snap_a, s) == 0, "toast expires after 3500 ms");

    nj_client_ui_tick(ui, 14000);
    nj_client_ui_note_game_exit(ui, "/games/旧游戏.jar", 3);
    nj_client_ui_draw(ui, s);
    CHECK(diff_pixels(snap_a, s) > 500,
          "game-exit toast appears with its status");
    nj_client_ui_tick(ui, 19000);
    nj_client_ui_draw(ui, s);
    CHECK(diff_pixels(snap_a, s) == 0, "game-exit toast expires too");

    /* -------------------------------------------------------------- */
    /* Browser                                                         */
    /* -------------------------------------------------------------- */
    printf("  -- browser --\n");
    nj_client_ui_char(ui, 'o');
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_BROWSER, "'o' opens browser");
    CHECK(nj_client_ui_browser_selected(ui) == 0, "browser starts at '..'");
    CHECK(nj_client_ui_row_rect(ui, 0, &rx, &ry, &rw, &rh) == 0,
          "row_rect only answers in library mode");
    nj_client_ui_tick(ui, 9100);
    draw_dump(ui, s, env.out, "04_browser.ppm");
    CHECK(px(s, 5, 56 + 10) == COL_PATHBAR, "browser path bar drawn");
    CHECK(count_not(s, COL_BG) > 3000, "browser frame is not blank");

    key(ui, NJ_CLIENT_KEY_DOWN);
    CHECK(nj_client_ui_browser_selected(ui) == 1, "browser DOWN moves to subdir");
    memcpy(snap_a->pixels, s->pixels, (size_t)TEST_W * (size_t)TEST_H * 4u);
    key(ui, NJ_CLIENT_KEY_ENTER);
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_BROWSER,
          "ENTER on a directory stays in the browser");
    CHECK(nj_client_ui_browser_selected(ui) == 0, "descend resets selection");
    nj_client_ui_draw(ui, s);
    CHECK(diff_pixels(snap_a, s) > 500, "descending changes the browser frame");
    key(ui, NJ_CLIENT_KEY_BACKSPACE);
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_BROWSER,
          "BACKSPACE stays in the browser");
    nj_client_ui_draw(ui, s);
    CHECK(nj_client_ui_browser_selected(ui) == 0, "back reload selects '..'");

    key(ui, NJ_CLIENT_KEY_ESCAPE);
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_LIBRARY,
          "ESCAPE leaves the browser");

    /* 'p' does nothing outside pick-dir mode */
    {
        char before[sizeof(settings.games_dir)];
        snprintf(before, sizeof(before), "%s", settings.games_dir);
        nj_client_ui_char(ui, 'o');
        nj_client_ui_char(ui, 'p');
        CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_BROWSER,
              "'p' ignored in a normal browser");
        CHECK(strcmp(settings.games_dir, before) == 0,
              "normal browser does not change the games dir");
        key(ui, NJ_CLIENT_KEY_ESCAPE);
    }

    /* -------------------------------------------------------------- */
    /* Settings: rows, persistence, pick-dir, reset                    */
    /* -------------------------------------------------------------- */
    printf("  -- settings --\n");
    snprintf(settings.last_dir, sizeof(settings.last_dir), "%s", env.games);
    nj_client_ui_char(ui, 's');
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_SETTINGS,
          "'s' opens settings");
    nj_client_ui_tick(ui, 9200);
    draw_dump(ui, s, env.out, "05_settings.ppm");
    CHECK(count_not(s, COL_BG) > 5000, "settings frame is not blank");
    CHECK(px(s, 5, 5) != COL_CHROME, "settings dims the library behind");

    /* 画面缩放: 0 -> 1 -> 2 -> 3 -> 4 -> 0, then LEFT back to 4, LEFT to 3 */
    key(ui, NJ_CLIENT_KEY_DOWN);
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.game_scale == 1, "RIGHT sets scale 1");
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.game_scale == 2, "RIGHT sets scale 2");
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.game_scale == 3, "RIGHT sets scale 3");
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.game_scale == 4, "RIGHT sets scale 4");
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.game_scale == 0, "RIGHT wraps scale back to auto");
    key(ui, NJ_CLIENT_KEY_LEFT);
    CHECK(settings.game_scale == 4, "LEFT wraps scale to 4");
    key(ui, NJ_CLIENT_KEY_LEFT);
    CHECK(settings.game_scale == 3, "LEFT steps scale to 3");

    /* 缩放滤镜 */
    key(ui, NJ_CLIENT_KEY_DOWN);
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.filter_linear == 1, "filter row switches to linear");
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.filter_linear == 0, "filter row wraps to nearest");

    /* 全屏 */
    key(ui, NJ_CLIENT_KEY_DOWN);
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.fullscreen == 1, "fullscreen row turns on");
    key(ui, NJ_CLIENT_KEY_LEFT);
    CHECK(settings.fullscreen == 0, "fullscreen row turns off");

    /* 渲染加速 */
    key(ui, NJ_CLIENT_KEY_DOWN);
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.accel == 1, "accel row turns on");
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.accel == 0, "accel row toggles off");

    /* 屏幕键盘 */
    key(ui, NJ_CLIENT_KEY_DOWN);
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.keys_visible == 0, "keys row toggles off");
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.keys_visible == 1, "keys row toggles on");

    /* 帧率上限: 0 -> 30 -> 60 -> 0, and back */
    key(ui, NJ_CLIENT_KEY_DOWN);
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.fps_cap == 30, "fps row sets 30");
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.fps_cap == 60, "fps row sets 60");
    key(ui, NJ_CLIENT_KEY_LEFT);
    CHECK(settings.fps_cap == 30, "fps row steps back to 30");
    key(ui, NJ_CLIENT_KEY_RIGHT);
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.fps_cap == 0, "fps row wraps back to uncapped");
    key(ui, NJ_CLIENT_KEY_LEFT);
    CHECK(settings.fps_cap == 60, "fps row wraps back to 60");

    /* -------------------------------------------------------------- */
    /* Key bindings page                                               */
    /* -------------------------------------------------------------- */
    key(ui, NJ_CLIENT_KEY_DOWN);          /* row 7: 按键映射 */
    key(ui, NJ_CLIENT_KEY_ENTER);
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_BINDS,
          "ENTER on 按键映射 opens the bind page");
    CHECK(nj_client_ui_selected_bind(ui) == 0,
          "bind page starts on the first slot");
    nj_client_ui_tick(ui, 9500);
    draw_dump(ui, s, env.out, "05b_binds.ppm");
    CHECK(count_not(s, COL_BG) > 5000, "bind page frame is not blank");

    {
        int i;
        CHECK(NJ_PORT_BIND_COUNT == 12, "twelve bind slots");
        for (i = 0; i < NJ_PORT_BIND_COUNT; i++) {
            CHECK(settings.binds[i][0] == '\0',
                  "bind slot starts without an override");
            CHECK(nj_client_bind_label(i) && nj_client_bind_label(i)[0],
                  "bind slot has a label");
            CHECK(nj_client_bind_default_key(i) &&
                      nj_client_bind_default_key(i)[0],
                  "bind slot has a default key");
        }
        CHECK(strcmp(nj_client_bind_label(0), "上") == 0,
              "first slot is labelled 上");
        CHECK(strcmp(nj_client_bind_default_key(0), "Up") == 0,
              "first slot defaults to Up");
        CHECK(strcmp(nj_client_bind_default_key(11), "V") == 0,
              "last slot defaults to V");
    }

    /* Navigation, paging and the wheel. */
    key(ui, NJ_CLIENT_KEY_DOWN);
    CHECK(nj_client_ui_selected_bind(ui) == 1, "bind DOWN moves one slot");
    key(ui, NJ_CLIENT_KEY_END);
    CHECK(nj_client_ui_selected_bind(ui) == NJ_PORT_BIND_COUNT - 1,
          "bind END selects the last slot");
    key(ui, NJ_CLIENT_KEY_HOME);
    CHECK(nj_client_ui_selected_bind(ui) == 0, "bind HOME selects the first");
    key(ui, NJ_CLIENT_KEY_PAGE_DOWN);
    CHECK(nj_client_ui_selected_bind(ui) > 0, "bind PAGE_DOWN moves a page");
    key(ui, NJ_CLIENT_KEY_HOME);
    nj_client_ui_wheel(ui, -3);
    CHECK(nj_client_ui_selected_bind(ui) == 3, "bind wheel moves the selection");
    nj_client_ui_wheel(ui, 3);
    CHECK(nj_client_ui_selected_bind(ui) == 0, "bind wheel moves back");

    /* Pointer taps select; a second tap on the selection starts capture.
     * The coordinates follow the centered 540x320 panel at 720x540: rows
     * begin at y=174 with a 40 px pitch and five rows are visible. */
    nj_client_ui_draw(ui, s);
    nj_client_ui_pointer(ui, 200, 174 + 40 + 20, 1);
    CHECK(nj_client_ui_selected_bind(ui) == 1, "bind tap selects a row");
    nj_client_ui_pointer(ui, 200, 174 + 40 + 20, 1);
    CHECK(nj_client_ui_capturing(ui) == 1, "tapping the selection captures");
    nj_client_ui_capture_cancel(ui);
    CHECK(nj_client_ui_capturing(ui) == 0, "capture_cancel clears capture");
    key(ui, NJ_CLIENT_KEY_HOME);

    /* ENTER waits for one key; capture_key() judges it. */
    key(ui, NJ_CLIENT_KEY_ENTER);
    CHECK(nj_client_ui_capturing(ui) == 1, "ENTER begins key capture");
    CHECK(nj_client_ui_selected_bind(ui) == 0, "capture keeps the slot");
    nj_client_ui_draw(ui, s);
    dump_ppm(env.out, "05c_capture.ppm", s);
    CHECK(count_not(s, COL_BG) > 5000, "capture overlay draws");

    /* While capturing, navigation must not leak through. */
    key(ui, NJ_CLIENT_KEY_DOWN);
    CHECK(nj_client_ui_selected_bind(ui) == 0, "capture ignores navigation");
    nj_client_ui_wheel(ui, 2);
    CHECK(nj_client_ui_selected_bind(ui) == 0, "capture ignores the wheel");
    CHECK(nj_client_ui_capturing(ui) == 1, "still capturing");

    /* Rejections keep the capture open and leave the slot empty. */
    CHECK(nj_client_ui_capture_key(ui, "F5") == 0, "reserved key rejected");
    CHECK(nj_client_ui_capturing(ui) == 1, "reserved key keeps capturing");
    CHECK(nj_client_ui_capture_key(ui, "Tab") == 0, "Tab rejected");
    CHECK(nj_client_ui_capturing(ui) == 1, "Tab keeps capturing");
    CHECK(nj_client_ui_capture_key(ui, "Left Shift") == 0,
          "modifier rejected");
    CHECK(nj_client_ui_capturing(ui) == 1, "modifier keeps capturing");
    CHECK(nj_client_ui_capture_key(ui, "") == 0, "empty key rejected");
    CHECK(nj_client_ui_capture_key(ui, "abcdefghijklmnopqrstuvwxyz") == 0,
          "overlong key rejected");
    CHECK(nj_client_ui_capturing(ui) == 1, "invalid keys keep capturing");
    CHECK(nj_client_ui_capture_key(ui, "Down") == 0,
          "default of another slot rejected");
    CHECK(nj_client_ui_capture_key(ui, "down") == 0,
          "duplicate check is case-insensitive");
    CHECK(nj_client_ui_capturing(ui) == 1, "duplicates keep capturing");
    CHECK(settings.binds[0][0] == '\0', "rejected keys left the slot empty");

    /* Accept a printable key and persist it. */
    CHECK(nj_client_ui_capture_key(ui, "w") == 1, "new key accepted");
    CHECK(nj_client_ui_capturing(ui) == 0, "acceptance ends capture");
    CHECK(strcmp(settings.binds[0], "w") == 0,
          "accepted key stored in the slot");
    {
        nj_client_settings loaded;
        memset(&loaded, 0, sizeof(loaded));
        CHECK(nj_client_settings_load(&loaded, NULL) == 1,
              "settings with a binding load back");
        CHECK(strcmp(loaded.binds[0], "w") == 0, "load kept the bound key");
    }

    /* Pressing the slot's own default clears the override. */
    key(ui, NJ_CLIENT_KEY_ENTER);
    CHECK(nj_client_ui_capturing(ui) == 1, "capture restarted");
    CHECK(nj_client_ui_capture_key(ui, "Up") == 1,
          "slot default restores the default");
    CHECK(nj_client_ui_capturing(ui) == 0, "restoring ends capture");
    CHECK(settings.binds[0][0] == '\0', "default name cleared the override");

    /* ESC cancels without touching the slot. */
    key(ui, NJ_CLIENT_KEY_ENTER);
    CHECK(nj_client_ui_capture_key(ui, "Escape") == 0, "Escape cancels");
    CHECK(nj_client_ui_capturing(ui) == 0, "Escape ends capture");
    CHECK(settings.binds[0][0] == '\0', "cancel left the slot empty");

    /* BACKSPACE clears an existing binding outside capture. */
    key(ui, NJ_CLIENT_KEY_ENTER);
    CHECK(nj_client_ui_capture_key(ui, "q") == 1, "bound q to the first slot");
    key(ui, NJ_CLIENT_KEY_BACKSPACE);
    CHECK(settings.binds[0][0] == '\0', "BACKSPACE clears the slot");
    CHECK(nj_client_ui_capturing(ui) == 0, "BACKSPACE does not start capture");

    /* ESC goes back to the settings row that opened the page. */
    key(ui, NJ_CLIENT_KEY_ESCAPE);
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_SETTINGS,
          "ESCAPE returns from binds to settings");
    CHECK(nj_client_ui_selected_bind(ui) == -1,
          "selected_bind is -1 outside the bind page");

    /* 堆大小: 自动 -> 8 -> 16 -> 32 -> 64 -> 自动, and back */
    key(ui, NJ_CLIENT_KEY_DOWN);
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.heap_mb == 8, "heap row steps to 8 MB");
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.heap_mb == 16, "heap row steps to 16 MB");
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.heap_mb == 32, "heap row steps to 32 MB");
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.heap_mb == 64, "heap row steps to 64 MB");
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.heap_mb == 0, "heap row wraps to automatic");
    key(ui, NJ_CLIENT_KEY_LEFT);
    CHECK(settings.heap_mb == 64, "heap LEFT wraps to 64 MB");

    /* 静音 */
    key(ui, NJ_CLIENT_KEY_DOWN);
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.mute == 1, "mute row turns on");
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.mute == 0, "mute row turns off");

    /* 网络 */
    key(ui, NJ_CLIENT_KEY_DOWN);
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.network == 0, "network row turns off");
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.network == 1, "network row turns on");

    /* Persistence: the same values must come back through save/load. */
    {
        nj_client_settings loaded;
        char path[1200];
        int saved = nj_client_settings_save(&settings, NULL);
        CHECK(saved == 1, "settings saved");
        memset(&loaded, 0, sizeof(loaded));
        CHECK(nj_client_settings_load(&loaded, NULL) == 1,
              "settings load reports success");
        CHECK(loaded.game_scale == settings.game_scale, "load kept scale");
        CHECK(loaded.keys_visible == settings.keys_visible,
              "load kept keys visibility");
        CHECK(loaded.fps_cap == settings.fps_cap, "load kept fps cap");
        CHECK(loaded.filter_linear == settings.filter_linear,
              "load kept the filter");
        CHECK(loaded.fullscreen == settings.fullscreen,
              "load kept fullscreen");
        CHECK(loaded.accel == settings.accel, "load kept accel");
        CHECK(loaded.heap_mb == settings.heap_mb, "load kept heap size");
        CHECK(loaded.mute == settings.mute, "load kept mute");
        CHECK(loaded.network == settings.network, "load kept network");
        CHECK(strcmp(loaded.games_dir, settings.games_dir) == 0,
              "load kept the games dir");
        CHECK(nj_client_settings_path(path, (int)sizeof(path)) == 1,
              "settings path available");
#if !defined(_WIN32)
        CHECK(strncmp(path, env.config, strlen(env.config)) == 0,
              "settings path lives under the test config dir");
#endif
    }

    /* Pick-dir flow: row 0 ENTER opens the browser in pick-dir mode. */
    snprintf(settings.last_dir, sizeof(settings.last_dir), "%s", env.games);
    snprintf(settings.games_dir, sizeof(settings.games_dir), "%s", env.other);
    key(ui, NJ_CLIENT_KEY_HOME);
    key(ui, NJ_CLIENT_KEY_ENTER);
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_BROWSER,
          "ENTER on 游戏目录 opens the browser");
    nj_client_ui_draw(ui, s);
    dump_ppm(env.out, "06_pick_dir_browser.ppm", s);
    key(ui, NJ_CLIENT_KEY_ESCAPE);
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_SETTINGS,
          "ESC from the pick-dir browser returns to settings");
    key(ui, NJ_CLIENT_KEY_ENTER);
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_BROWSER,
          "the pick-dir browser reopens");
    nj_client_ui_char(ui, 'p');
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_LIBRARY,
          "'p' returns to the library");
    CHECK(strcmp(settings.games_dir, env.games) == 0,
          "'p' adopted the browsed directory");
    CHECK(strcmp(settings.last_dir, env.games) == 0,
          "'p' remembered the browsed directory");
    CHECK(scan_started(lib), "'p' started a rescan");
    finish_scan(lib);
    CHECK(lib->count == env.expect_count, "'p' scan found the fixtures");

    /* Set a binding first, so the reset has an override to clear. */
    nj_client_ui_char(ui, 's');
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_SETTINGS,
          "'s' reopens settings");
    key(ui, NJ_CLIENT_KEY_HOME);
    {
        int i;
        for (i = 0; i < 7; i++) key(ui, NJ_CLIENT_KEY_DOWN);
    }
    key(ui, NJ_CLIENT_KEY_ENTER);
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_BINDS,
          "bind page reopened for the reset");
    key(ui, NJ_CLIENT_KEY_ENTER);
    CHECK(nj_client_ui_capture_key(ui, "n") == 1, "bound n before the reset");
    CHECK(settings.binds[0][0] != '\0', "binding stored before the reset");

    /* 恢复默认 */
    key(ui, NJ_CLIENT_KEY_ESCAPE);              /* binds -> settings */
    key(ui, NJ_CLIENT_KEY_END);
    key(ui, NJ_CLIENT_KEY_ENTER);
    CHECK(settings.game_scale == 0, "reset restores auto scale");
    CHECK(settings.keys_visible == 1, "reset restores on-screen keys");
    CHECK(settings.fps_cap == 0, "reset restores uncapped fps");
    CHECK(settings.filter_linear == 0 && settings.fullscreen == 0 &&
              settings.accel == 0 && settings.heap_mb == 0 &&
              settings.mute == 0 && settings.network == 1,
          "reset restores the new settings");
    CHECK(settings.binds[0][0] == '\0', "reset clears every binding");
    CHECK(nj_client_default_games_dir(dflt, (int)sizeof(dflt)) == 1,
          "default games dir available");
    CHECK(strcmp(settings.games_dir, dflt) == 0,
          "reset restores the default games dir");
    finish_scan(lib);
    CHECK(lib->count == 0, "reset rescan of the empty default dir");
    key(ui, NJ_CLIENT_KEY_ESCAPE);
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_LIBRARY,
          "ESCAPE leaves settings");

    /* Empty state, now that the library is empty again. */
    nj_client_ui_tick(ui, 13000);
    draw_dump(ui, s, env.out, "07_empty_library.ppm");
    CHECK(count_not(s, COL_BG) > 1000, "empty state draws");
    CHECK(nj_client_ui_selected(ui) == -1, "empty library has no selection");
    CHECK(nj_client_ui_row_rect(ui, 0, &rx, &ry, &rw, &rh) == 0,
          "no row rect in an empty library");

    /* -------------------------------------------------------------- */
    /* Drag and drop                                                   */
    /* -------------------------------------------------------------- */
    printf("  -- drop --\n");
    nj_client_ui_drop(ui, env.txt);
    CHECK(take_action(ui, &action, jar, (int)sizeof(jar)) == 0,
          "dropping a non-jar emits no action");
    CHECK(strcmp(settings.games_dir, dflt) == 0,
          "dropping a non-jar keeps the games dir");
    nj_client_ui_draw(ui, s);

    nj_client_ui_drop(ui, env.broken);
    CHECK(take_action(ui, &action, jar, (int)sizeof(jar)) == 0,
          "dropping a broken jar emits no action");
    CHECK(lib->count == 0, "dropping a broken jar does not add it");
    nj_client_ui_draw(ui, s);
    CHECK(count_not(s, COL_BG) > 1000, "drop toast frame draws");

    nj_client_ui_drop(ui, env.jar_drop);
    CHECK(take_action(ui, &action, jar, (int)sizeof(jar)) == 1,
          "dropping a valid jar emits an action");
    CHECK(action == NJ_CLIENT_ACTION_RUN, "dropped jar action is RUN");
    CHECK(strcmp(jar, env.jar_drop) == 0, "dropped jar path is carried");
    CHECK(lib->count == 1, "dropped jar was added to the library");
    if (lib->count == 1) {
        CHECK(strcmp(lib->entries[0].path, env.jar_drop) == 0,
              "added entry points at the dropped jar");
    }
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_LIBRARY,
          "drop returns to the library");

    /* Dropping the same jar twice must not duplicate it. */
    nj_client_ui_drop(ui, env.jar_drop);
    CHECK(take_action(ui, &action, jar, (int)sizeof(jar)) == 1,
          "second drop still runs the jar");
    CHECK(lib->count == 1, "duplicate drop does not add a second entry");

    /* Dropping a directory adopts it as the games dir and rescans. */
    nj_client_ui_drop(ui, env.games);
    CHECK(strcmp(settings.games_dir, env.games) == 0,
          "dropped directory becomes the games dir");
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_LIBRARY,
          "directory drop stays in the library");
    CHECK(scan_started(lib), "directory drop started a rescan");
    finish_scan(lib);
    CHECK(lib->count == env.expect_count, "directory drop scan found fixtures");

    /* 'r' rescans the same directory. */
    nj_client_ui_char(ui, 'r');
    CHECK(scan_started(lib), "'r' started a rescan");
    finish_scan(lib);
    CHECK(lib->count == env.expect_count, "'r' scan found the fixtures");

    /* -------------------------------------------------------------- */
    /* Very long names and paths                                       */
    /* -------------------------------------------------------------- */
    printf("  -- long names/paths --\n");
    {
        char saved_games[NJ_CLIENT_PATH_MAX];
        size_t slen;

        snprintf(saved_games, sizeof(saved_games), "%s", settings.games_dir);
        nj_client_ui_drop(ui, env.longdir);
        CHECK(strcmp(settings.games_dir, env.longdir) == 0,
              "long-directory drop adopts the directory");
        finish_scan(lib);
        CHECK(lib->count == 1, "long-name directory has one jar");
        draw_dump(ui, s, env.out, "09_long_name.ppm");
        CHECK(count_not(s, COL_BG) > 2000, "long-name row draws");
        CHECK(nj_client_ui_row_rect(ui, 0, &rx, &ry, &rw, &rh) == 1,
              "long-name row is clickable");

        /* A games dir longer than the header can show. */
        {
            size_t cap = sizeof(settings.games_dir) - 1u;
            size_t n = strlen(env.root);
            if (n > cap - 1u) n = cap - 1u;
            memcpy(settings.games_dir, env.root, n);
            settings.games_dir[n] = '/';
            memset(settings.games_dir + n + 1, 'a', cap - n - 1u);
            settings.games_dir[cap] = '\0';
        }
        nj_client_ui_draw(ui, s);
        CHECK(count_not(s, COL_BG) > 2000, "very long games dir draws");
        nj_client_ui_note(ui, "非常长的提示消息用于检查截断逻辑是否安全");
        nj_client_ui_draw(ui, s);
        CHECK(count_not(s, COL_BG) > 2000, "toast over a long path draws");

        /* Restore the games directory and its scan for later checks. */
        slen = strlen(saved_games);
        if (slen >= sizeof(settings.games_dir)) {
            slen = sizeof(settings.games_dir) - 1u;
        }
        memcpy(settings.games_dir, saved_games, slen);
        settings.games_dir[slen] = '\0';
        nj_client_ui_drop(ui, env.games);
        finish_scan(lib);
        CHECK(lib->count == env.expect_count,
              "library restored after long-name checks");
    }

    /* -------------------------------------------------------------- */
    /* Small windows                                                   */
    /* -------------------------------------------------------------- */
    printf("  -- small windows --\n");
    {
        nj_surface *small = surface_new(320, 240);
        nj_surface *tiny = surface_new(320, 200);
        CHECK(small && tiny, "small surfaces allocate");
        if (small && tiny) {
            int rx0 = 0, ry0 = 0, rw0 = 0, rh0 = 0;
            nj_client_ui_draw(ui, small);
            dump_ppm(env.out, "08_small_320x240.ppm", small);
            CHECK(count_not(small, COL_BG) > 1500, "320x240 frame draws");
            CHECK(nj_client_ui_row_rect(ui, 0, &rx0, &ry0, &rw0, &rh0) == 1,
                  "320x240 row_rect(0) answers");
            CHECK(ry0 >= 40, "320x240 row below the shrunken header");
            CHECK(ry0 + rh0 <= 240 - 30, "320x240 row above the footer");
            CHECK(px(small, 5, 5) == COL_CHROME, "320x240 header drawn");

            nj_client_ui_draw(ui, tiny);
            CHECK(count_not(tiny, COL_BG) > 1000, "320x200 frame draws");

            /* The bind page and its capture overlay must survive the
             * smallest windows too. */
            {
                int i;
                nj_client_ui_char(ui, 's');
                CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_SETTINGS,
                      "320x240: settings open");
                nj_client_ui_draw(ui, small);
                dump_ppm(env.out, "08c_small_settings.ppm", small);
                CHECK(count_not(small, COL_BG) > 1500,
                      "320x240 settings page draws");
                nj_client_ui_draw(ui, tiny);
                CHECK(count_not(tiny, COL_BG) > 1000,
                      "320x200 settings page draws");
                for (i = 0; i < 7; i++) key(ui, NJ_CLIENT_KEY_DOWN);
                key(ui, NJ_CLIENT_KEY_ENTER);
                CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_BINDS,
                      "320x240: bind page opens");
                key(ui, NJ_CLIENT_KEY_ENTER);
                CHECK(nj_client_ui_capturing(ui) == 1,
                      "320x240: capture overlay opens");
                nj_client_ui_draw(ui, small);
                dump_ppm(env.out, "08b_small_binds.ppm", small);
                CHECK(count_not(small, COL_BG) > 1500,
                      "320x240 bind page with capture draws");
                nj_client_ui_capture_cancel(ui);
                nj_client_ui_draw(ui, small);
                CHECK(count_not(small, COL_BG) > 1500,
                      "320x240 bind page draws");
                nj_client_ui_draw(ui, tiny);
                CHECK(count_not(tiny, COL_BG) > 1000,
                      "320x200 bind page draws");
                key(ui, NJ_CLIENT_KEY_ESCAPE);   /* binds -> settings */
                key(ui, NJ_CLIENT_KEY_ESCAPE);   /* settings -> library */
                CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_LIBRARY,
                      "small window: ESC returns to the library");
            }
            surface_free(tiny);
        }
        surface_free(small);
        nj_client_ui_resize(ui, TEST_W, TEST_H);
        nj_client_ui_draw(ui, s);
    }

    /* -------------------------------------------------------------- */
    /* NULL robustness                                                 */
    /* -------------------------------------------------------------- */
    printf("  -- NULL safety --\n");
    {
        nj_client_ui *nul = nj_client_ui_new(NULL, NULL);
        CHECK(nul != NULL, "model allocates without settings or library");
        if (nul) {
            nj_client_ui_resize(nul, TEST_W, TEST_H);
            nj_client_ui_tick(nul, 100);
            nj_client_ui_draw(nul, s);
            CHECK(count_not(s, COL_BG) > 1000, "empty model draws");
            nj_client_ui_draw(nul, NULL);
            nj_client_ui_key(nul, NJ_CLIENT_KEY_DOWN, 1, 0);
            nj_client_ui_wheel(nul, -3);
            nj_client_ui_pointer(nul, 10, 60, 1);
            nj_client_ui_pointer_move(nul, 20, 70);
            nj_client_ui_char(nul, 'o');
            CHECK(nj_client_ui_mode(nul) == NJ_CLIENT_MODE_BROWSER,
                  "browser opens without settings");
            nj_client_ui_draw(nul, s);
            key(nul, NJ_CLIENT_KEY_ESCAPE);
            nj_client_ui_char(nul, 's');
            CHECK(nj_client_ui_mode(nul) == NJ_CLIENT_MODE_SETTINGS,
                  "settings open without settings");
            nj_client_ui_draw(nul, s);
            key(nul, NJ_CLIENT_KEY_ESCAPE);
            CHECK(nj_client_ui_row_rect(nul, 0, NULL, NULL, NULL, NULL) == 0,
                  "row_rect on an empty model answers 0");
            CHECK(nj_client_ui_selected(nul) == -1, "empty model selection -1");
            CHECK(nj_client_ui_selected_bind(nul) == -1,
                  "no bind selection on an empty model");
            CHECK(nj_client_ui_capturing(nul) == 0,
                  "empty model is not capturing");
            CHECK(nj_client_ui_capture_key(nul, "w") == 0,
                  "capture_key is ignored without settings");
            nj_client_ui_capture_cancel(nul);
            nj_client_ui_note(nul, NULL);
            nj_client_ui_note_game_exit(nul, NULL, 7);
            nj_client_ui_drop(nul, env.jar_drop);
            CHECK(take_action(nul, &action, jar, (int)sizeof(jar)) == 1,
                  "jar drop runs even without a library");
            CHECK(action == NJ_CLIENT_ACTION_RUN, "NULL-library drop is RUN");
            nj_client_ui_drop(nul, env.games);
            CHECK(take_action(nul, &action, jar, (int)sizeof(jar)) == 0,
                  "directory drop without settings emits nothing");
            key(nul, NJ_CLIENT_KEY_ESCAPE);
            CHECK(nj_client_ui_quit(nul) == 1, "ESCAPE latches quit");
            nj_client_ui_free(nul);
        }
    }

    /* -------------------------------------------------------------- */
    /* Quit                                                            */
    /* -------------------------------------------------------------- */
    printf("  -- quit --\n");
    nj_client_ui_draw(ui, s);
    nj_client_ui_char(ui, 'q');
    CHECK(nj_client_ui_quit(ui) == 1, "'q' latches the quit flag");
    CHECK(take_action(ui, &action, jar, (int)sizeof(jar)) == 1,
          "quit emits an action");
    CHECK(action == NJ_CLIENT_ACTION_QUIT, "action is QUIT");
    CHECK(nj_client_ui_quit(ui) == 1, "quit stays latched");
    CHECK(take_action(ui, &action, jar, (int)sizeof(jar)) == 0,
          "quit action is consumed once");

    /* Fresh model: ESCAPE from the library quits and 'take' reports QUIT. */
    {
        nj_client_ui *fresh = nj_client_ui_new(&settings, lib);
        CHECK(fresh != NULL, "fresh model allocates");
        if (fresh) {
            key(fresh, NJ_CLIENT_KEY_ESCAPE);
            CHECK(nj_client_ui_quit(fresh) == 1, "ESCAPE quits a fresh model");
            CHECK(take_action(fresh, &action, jar, (int)sizeof(jar)) == 1,
                  "fresh model quit action available");
            CHECK(action == NJ_CLIENT_ACTION_QUIT, "fresh model QUIT action");
            nj_client_ui_free(fresh);
        }
    }

    /* -------------------------------------------------------------- */
    /* Teardown                                                        */
    /* -------------------------------------------------------------- */
    printf("  -- done --\n");
    surface_free(s);
    surface_free(snap_a);
    surface_free(snap_b);
    nj_client_ui_free(ui);
    nj_library_free(lib);

    printf("  %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
