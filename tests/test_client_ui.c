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
#define COL_PANEL   0xFF252B36u

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

/* Pixels that differ outside the rectangle [x0,x1) x [y0,y1). */
static int diff_outside(const nj_surface *a, const nj_surface *b, int x0,
                        int y0, int x1, int y1)
{
    int x, y, n = 0;
    if (!a || !b || a->width != b->width || a->height != b->height) return -1;
    for (y = 0; y < a->height; y++) {
        const nj_u4 *ra = (const nj_u4 *)((const unsigned char *)a->pixels +
                                          (size_t)y * (size_t)a->stride);
        const nj_u4 *rb = (const nj_u4 *)((const unsigned char *)b->pixels +
                                          (size_t)y * (size_t)b->stride);
        for (x = 0; x < a->width; x++) {
            if (x >= x0 && x < x1 && y >= y0 && y < y1) continue;
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
    CHECK(settings.gamepad == 1, "settings default gamepad on");
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
    /* Library search filter                                           */
    /* -------------------------------------------------------------- */
    printf("  -- library search --\n");
    {
        int alpha = find_entry_file(lib, "alphademo.jar");
        int cjk = env.have_cjk ? find_entry_file(lib, "1942空战_10403.jar")
                               : -1;
        int i;

        CHECK(alpha >= 0, "search: alphademo row present");
        CHECK(cjk >= 0, "search: CJK row present");
        CHECK(strcmp(nj_client_ui_filter(ui), "") == 0,
              "search: filter starts empty");
        nj_client_ui_draw(ui, s);
        CHECK(px(s, 5, 56 + 8) == COL_PATHBAR,
              "search bar drawn under the header");
        CHECK(count_region_not(s, 16, 56, 400, 56 + 32, COL_PATHBAR) > 50,
              "search hint text visible in the bar");
        CHECK(count_region_not(s, TEST_W - 140, 56, TEST_W - 16, 56 + 32,
                               COL_PATHBAR) > 20,
              "search count label visible in the bar");

        /* ASCII name, one key at a time. */
        nj_client_ui_char(ui, 'a');
        nj_client_ui_char(ui, 'l');
        nj_client_ui_char(ui, 'p');
        nj_client_ui_char(ui, 'h');
        nj_client_ui_char(ui, 'a');
        CHECK(strcmp(nj_client_ui_filter(ui), "alpha") == 0,
              "filter getter returns the typed text");
        CHECK(nj_client_ui_selected(ui) == alpha,
              "filter selected the matching game");
        CHECK(nj_client_ui_row_rect(ui, alpha, &rx, &ry, &rw, &rh) == 1,
              "filtered match keeps its row rect");
        CHECK(nj_client_ui_row_rect(ui, cjk, &rx, &ry, &rw, &rh) == 0,
              "filtered-out row has no row rect");
        nj_client_ui_draw(ui, s);
        CHECK(count_region_not(s, 16, 56, 300, 56 + 32, COL_PATHBAR) > 50,
              "filter text drawn in the bar");
        dump_ppm(env.out, "03b_search.ppm", s);

        /* ESC clears the filter instead of quitting. */
        key(ui, NJ_CLIENT_KEY_ESCAPE);
        CHECK(strcmp(nj_client_ui_filter(ui), "") == 0,
              "ESCAPE clears the filter");
        CHECK(nj_client_ui_quit(ui) == 0, "clearing the filter does not quit");
        CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_LIBRARY,
              "clearing keeps the library mode");

        /* ASCII case folding. */
        nj_client_ui_char(ui, 'A');
        nj_client_ui_char(ui, 'L');
        nj_client_ui_char(ui, 'P');
        nj_client_ui_char(ui, 'H');
        nj_client_ui_char(ui, 'A');
        CHECK(strcmp(nj_client_ui_filter(ui), "ALPHA") == 0,
              "upper-case filter stored as typed");
        CHECK(nj_client_ui_selected(ui) == alpha,
              "upper-case filter matches case-insensitively");
        key(ui, NJ_CLIENT_KEY_ESCAPE);

        /* File name matching: the CJK jar's file name holds "10403". */
        nj_client_ui_char(ui, '1');
        nj_client_ui_char(ui, '0');
        nj_client_ui_char(ui, '4');
        nj_client_ui_char(ui, '0');
        nj_client_ui_char(ui, '3');
        CHECK(strcmp(nj_client_ui_filter(ui), "10403") == 0,
              "digit filter stored");
        CHECK(nj_client_ui_selected(ui) == cjk,
              "file name digits match the CJK jar");
        key(ui, NJ_CLIENT_KEY_ESCAPE);

        /* Chinese substring of the file name, and UTF-8 backspace. */
        nj_client_ui_char(ui, (nj_u2)0x7A7Au);          /* 空 */
        nj_client_ui_char(ui, (nj_u2)0x6218u);          /* 战 */
        CHECK(strcmp(nj_client_ui_filter(ui), "空战") == 0,
              "Chinese filter stored as UTF-8");
        CHECK(strlen(nj_client_ui_filter(ui)) == 6,
              "two CJK code points take six bytes");
        CHECK(nj_client_ui_selected(ui) == cjk,
              "Chinese substring matches the file name");
        key(ui, NJ_CLIENT_KEY_BACKSPACE);
        CHECK(strcmp(nj_client_ui_filter(ui), "空") == 0,
              "backspace removed one whole code point");
        key(ui, NJ_CLIENT_KEY_BACKSPACE);
        CHECK(strcmp(nj_client_ui_filter(ui), "") == 0,
              "backspace emptied the filter");
        CHECK(nj_client_ui_selected(ui) >= 0,
              "selection survives clearing the filter");

        /* MIDlet class matching (case-insensitive). */
        for (i = 0; "gamemidlet"[i] != '\0'; i++) {
            nj_client_ui_char(
                ui, (nj_u2)(unsigned char)"gamemidlet"[i]);
        }
        CHECK(strcmp(nj_client_ui_filter(ui), "gamemidlet") == 0,
              "class filter typed");
        CHECK(nj_client_ui_selected(ui) == cjk,
              "MIDlet class matches case-insensitively");
        key(ui, NJ_CLIENT_KEY_ESCAPE);

        /* Editing the filter resets the selection to the first match. */
        nj_client_ui_char(ui, 'a');
        CHECK(nj_client_ui_selected(ui) == cjk,
              "single letter selects the first matching row");
        key(ui, NJ_CLIENT_KEY_DOWN);
        CHECK(nj_client_ui_selected(ui) == alpha,
              "DOWN walks the filtered rows");
        nj_client_ui_char(ui, 'i');       /* "ai" only matches 1942AirCombat */
        CHECK(strcmp(nj_client_ui_filter(ui), "ai") == 0,
              "filter extended while a row was selected");
        CHECK(nj_client_ui_selected(ui) == cjk,
              "editing the filter resets the selection to the first match");
        key(ui, NJ_CLIENT_KEY_ESCAPE);

        /* Navigation inside the filtered list.  "m" matches exactly the
         * CJK entry (class GameMIDlet / com...) and AlphaDemo. */
        nj_client_ui_char(ui, 'm');
        key(ui, NJ_CLIENT_KEY_HOME);
        CHECK(nj_client_ui_selected(ui) == cjk, "HOME goes to the first match");
        key(ui, NJ_CLIENT_KEY_DOWN);
        CHECK(nj_client_ui_selected(ui) == alpha,
              "DOWN goes to the second match");
        key(ui, NJ_CLIENT_KEY_DOWN);
        CHECK(nj_client_ui_selected(ui) == alpha,
              "DOWN clamps at the last match");
        key(ui, NJ_CLIENT_KEY_END);
        CHECK(nj_client_ui_selected(ui) == alpha, "END goes to the last match");
        key(ui, NJ_CLIENT_KEY_UP);
        CHECK(nj_client_ui_selected(ui) == cjk, "UP goes back one match");
        key(ui, NJ_CLIENT_KEY_PAGE_DOWN);
        CHECK(nj_client_ui_selected(ui) == alpha,
              "PAGE_DOWN clamps to the last match");
        key(ui, NJ_CLIENT_KEY_PAGE_UP);
        CHECK(nj_client_ui_selected(ui) == cjk,
              "PAGE_UP returns to the first match");
        key(ui, NJ_CLIENT_KEY_ESCAPE);

        /* O/S/digit shortcuts become search text while filtering. */
        nj_client_ui_char(ui, 'a');
        nj_client_ui_char(ui, 's');
        CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_LIBRARY,
              "'s' types into the filter instead of opening settings");
        CHECK(strcmp(nj_client_ui_filter(ui), "as") == 0,
              "'s' appended to the filter");
        nj_client_ui_char(ui, 'o');
        CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_LIBRARY,
              "'o' types into the filter instead of opening files");
        nj_client_ui_char(ui, '7');
        CHECK(strcmp(nj_client_ui_filter(ui), "aso7") == 0,
              "digits append to the filter too");
        key(ui, NJ_CLIENT_KEY_ESCAPE);

        /* R rescans without dropping the filter. */
        nj_client_ui_char(ui, 'a');
        nj_client_ui_char(ui, 'r');
        CHECK(scan_started(lib), "R rescans while filtering");
        CHECK(strcmp(nj_client_ui_filter(ui), "a") == 0,
              "R keeps the filter text");
        finish_scan(lib);
        CHECK(lib->count == env.expect_count, "R rescan found the fixtures");
        nj_client_ui_draw(ui, s);
        CHECK(nj_client_ui_selected(ui) == cjk,
              "rescan keeps the filtered selection");

        /* ENTER runs the filtered row. */
        key(ui, NJ_CLIENT_KEY_ESCAPE);
        for (i = 0; "alphademo"[i] != '\0'; i++) {
            nj_client_ui_char(ui, (nj_u2)(unsigned char)"alphademo"[i]);
        }
        CHECK(nj_client_ui_selected(ui) == alpha,
              "alphademo filter selected the valid row");
        key(ui, NJ_CLIENT_KEY_ENTER);
        CHECK(take_action(ui, &action, jar, (int)sizeof(jar)) == 1,
              "ENTER on a filtered row emits an action");
        CHECK(action == NJ_CLIENT_ACTION_RUN, "filtered action is RUN");
        CHECK(strcmp(jar, lib->entries[alpha].path) == 0,
              "filtered RUN carries the jar path");
        key(ui, NJ_CLIENT_KEY_ESCAPE);

        /* Pointer clicks use the filtered mapping. */
        nj_client_ui_char(ui, 'a');
        nj_client_ui_draw(ui, s);
        CHECK(nj_client_ui_row_rect(ui, alpha, &rx, &ry, &rw, &rh) == 1,
              "filtered second row visible");
        nj_client_ui_pointer(ui, rx + 30, ry + 20, 1);
        CHECK(nj_client_ui_selected(ui) == alpha,
              "pointer click maps through the filter");
        CHECK(take_action(ui, &action, jar, (int)sizeof(jar)) == 0,
              "filtered selection click emits nothing");
        key(ui, NJ_CLIENT_KEY_ESCAPE);

        /* A non-matching filter is the no-match state. */
        nj_client_ui_char(ui, 'z');
        nj_client_ui_char(ui, 'z');
        nj_client_ui_char(ui, 'z');
        CHECK(strcmp(nj_client_ui_filter(ui), "zzz") == 0,
              "no-match filter stored");
        CHECK(nj_client_ui_selected(ui) == -1,
              "no-match state has no selection");
        CHECK(nj_client_ui_row_rect(ui, 0, &rx, &ry, &rw, &rh) == 0,
              "no-match state has no row rects");
        key(ui, NJ_CLIENT_KEY_ENTER);
        CHECK(take_action(ui, &action, jar, (int)sizeof(jar)) == 0,
              "ENTER in the no-match state does nothing");
        nj_client_ui_draw(ui, s);
        CHECK(count_not(s, COL_BG) > 2000, "no-match state still draws");
        key(ui, NJ_CLIENT_KEY_BACKSPACE);
        CHECK(strcmp(nj_client_ui_filter(ui), "zz") == 0,
              "backspace edits the no-match filter");
        key(ui, NJ_CLIENT_KEY_ESCAPE);
        CHECK(strcmp(nj_client_ui_filter(ui), "") == 0,
              "ESCAPE cleared the no-match filter");
        CHECK(nj_client_ui_selected(ui) >= 0,
              "clearing the search restores a selection");

        /* The byte cap never splits a UTF-8 sequence. */
        for (i = 0; i < 40; i++) nj_client_ui_char(ui, (nj_u2)0x7A7Au);
        nj_client_ui_char(ui, 'x');       /* dropped: the buffer is full */
        CHECK(strlen(nj_client_ui_filter(ui)) == 120,
              "filter stops at the 120 byte cap");
        CHECK((unsigned char)nj_client_ui_filter(ui)[119] >= 0x80u,
              "filter cap did not split the last code point");
        key(ui, NJ_CLIENT_KEY_ESCAPE);

        /* The filter belongs to the library; other pages ignore it. */
        nj_client_ui_char(ui, 'a');
        nj_client_ui_draw(ui, s);
        nj_client_ui_pointer(ui, TEST_W - 16 - 76 - 8 - 38, TEST_H - 20, 1);
        CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_SETTINGS,
              "settings button still opens the settings page");
        CHECK(strcmp(nj_client_ui_filter(ui), "a") == 0,
              "the filter survives a mode switch");
        nj_client_ui_char(ui, 'z');
        CHECK(strcmp(nj_client_ui_filter(ui), "a") == 0,
              "typing in settings does not touch the filter");
        nj_client_ui_draw(ui, s);
        CHECK(count_not(s, COL_BG) > 5000, "settings draws with a filter set");
        key(ui, NJ_CLIENT_KEY_ESCAPE);
        CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_LIBRARY,
              "back in the library with the filter still set");
        key(ui, NJ_CLIENT_KEY_ESCAPE);
        CHECK(strcmp(nj_client_ui_filter(ui), "") == 0,
              "library search cleared for the next section");
    }

    /* -------------------------------------------------------------- */
    /* F1 shortcut sheet                                               */
    /* -------------------------------------------------------------- */
    printf("  -- help sheet --\n");
    CHECK(nj_client_ui_help_visible(ui) == 0, "help sheet starts closed");
    key(ui, NJ_CLIENT_KEY_HELP);
    CHECK(nj_client_ui_help_visible(ui) == 1, "F1 opens the help sheet");
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_LIBRARY,
          "the help sheet keeps the library mode");
    {
        int sel = nj_client_ui_selected(ui);
        int count = lib->count;
        char before[160];

        snprintf(before, sizeof(before), "%s", nj_client_ui_filter(ui));
        nj_client_ui_draw(ui, s);
        memcpy(snap_b->pixels, s->pixels,
               (size_t)TEST_W * (size_t)TEST_H * 4u);
        CHECK(px(s, 80, TEST_H / 2 + 160) == COL_PANEL,
              "help panel background drawn");
        CHECK(px(s, 2, 2) != COL_CHROME,
              "the help sheet dims the header behind it");
        CHECK(count_region_not(s, TEST_W / 2 - 200, TEST_H / 2 - 120,
                               TEST_W / 2 + 200, TEST_H / 2 + 120,
                               COL_PANEL) > 200,
              "help text is drawn inside the panel");
        dump_ppm(env.out, "03c_help_sheet.ppm", s);

        key(ui, NJ_CLIENT_KEY_DOWN);
        key(ui, NJ_CLIENT_KEY_UP);
        nj_client_ui_char(ui, 'z');
        nj_client_ui_wheel(ui, -2);
        nj_client_ui_pointer(ui, 40, 200, 1);
        nj_client_ui_drop(ui, env.jar_drop);
        CHECK(nj_client_ui_selected(ui) == sel,
              "navigation is ignored while the sheet is open");
        CHECK(strcmp(nj_client_ui_filter(ui), before) == 0,
              "typing is ignored while the sheet is open");
        CHECK(lib->count == count,
              "drops are ignored while the sheet is open");
        CHECK(take_action(ui, &action, jar, (int)sizeof(jar)) == 0,
              "ignored drops emit nothing");
        CHECK(nj_client_ui_help_visible(ui) == 1,
              "the help sheet is still open");

        key(ui, NJ_CLIENT_KEY_ESCAPE);
        CHECK(nj_client_ui_help_visible(ui) == 0, "ESCAPE closes the sheet");
        CHECK(nj_client_ui_quit(ui) == 0, "closing the sheet does not quit");
        CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_LIBRARY,
              "closing the sheet stays in the library");
        nj_client_ui_draw(ui, s);
        CHECK(diff_pixels(snap_b, s) > 1000,
              "closing the sheet repaints the library");
    }

    key(ui, NJ_CLIENT_KEY_HELP);
    CHECK(nj_client_ui_help_visible(ui) == 1, "F1 reopens the sheet");
    key(ui, NJ_CLIENT_KEY_HELP);
    CHECK(nj_client_ui_help_visible(ui) == 0, "F1 toggles the sheet closed");
    key(ui, NJ_CLIENT_KEY_HELP);
    key(ui, NJ_CLIENT_KEY_ENTER);
    CHECK(nj_client_ui_help_visible(ui) == 0, "ENTER closes the sheet");

    /* F1 is a library-only shortcut. */
    nj_client_ui_char(ui, 's');
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_SETTINGS,
          "settings opened before the F1-ignored checks");
    key(ui, NJ_CLIENT_KEY_HELP);
    CHECK(nj_client_ui_help_visible(ui) == 0, "F1 is ignored in settings");
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_SETTINGS,
          "F1 does not leave the settings page");
    key(ui, NJ_CLIENT_KEY_ESCAPE);
    nj_client_ui_char(ui, 'o');
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_BROWSER,
          "browser opened for the F1-ignored checks");
    key(ui, NJ_CLIENT_KEY_HELP);
    CHECK(nj_client_ui_help_visible(ui) == 0,
          "F1 is ignored in the browser");
    key(ui, NJ_CLIENT_KEY_ESCAPE);
    CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_LIBRARY,
          "back in the library after the help sheet checks");

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

    /* 手柄: defaults on, LEFT/RIGHT toggles it and saves immediately. */
    key(ui, NJ_CLIENT_KEY_DOWN);
    CHECK(settings.gamepad == 1, "gamepad row starts on");
    draw_dump(ui, s, env.out, "05a_settings_gamepad.ppm");
    CHECK(settings.keys_visible == 1,
          "gamepad row did not land on the on-screen keypad row");
    key(ui, NJ_CLIENT_KEY_LEFT);
    CHECK(settings.gamepad == 0, "gamepad row turns off");
    {
        nj_client_settings saved;
        memset(&saved, 0, sizeof(saved));
        CHECK(nj_client_settings_load(&saved, NULL) == 1,
              "gamepad setting loads back");
        CHECK(saved.gamepad == 0, "gamepad save round-trips off");
    }
    key(ui, NJ_CLIENT_KEY_RIGHT);
    CHECK(settings.gamepad == 1, "gamepad row turns back on");

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
    key(ui, NJ_CLIENT_KEY_DOWN);          /* row 8: 按键映射 */
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
        CHECK(loaded.gamepad == settings.gamepad, "load kept gamepad");
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
        for (i = 0; i < 8; i++) key(ui, NJ_CLIENT_KEY_DOWN);
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
    CHECK(settings.gamepad == 1, "reset restores the gamepad default");
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
                for (i = 0; i < 8; i++) key(ui, NJ_CLIENT_KEY_DOWN);
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
    /* Scaled library icons                                            */
    /* -------------------------------------------------------------- */
    printf("  -- scaled icons --\n");
    {
        static nj_u4 big_pixels[80 * 59];
        static nj_u4 tiny_pixels[3 * 3 + 96];
        nj_lib_entry entries[1];
        nj_lib_icon_entry icons[1];
        nj_library fake_lib;
        nj_image img;
        nj_client_ui *icon_ui;
        nj_surface *a = surface_new(TEST_W, TEST_H);
        nj_surface *b = surface_new(TEST_W, TEST_H);
        int icon_x = 24, icon_y = 0, i;

        CHECK(a != NULL && b != NULL, "icon test surfaces allocate");
        memset(&fake_lib, 0, sizeof(fake_lib));
        memset(entries, 0, sizeof(entries));
        memset(icons, 0, sizeof(icons));
        memset(&img, 0, sizeof(img));
        snprintf(entries[0].name, sizeof(entries[0].name), "IconTest");
        snprintf(entries[0].file, sizeof(entries[0].file), "icontest.jar");
        snprintf(entries[0].path, sizeof(entries[0].path), "/tmp/icontest.jar");
        entries[0].valid = 1;
        entries[0].size = 4096;
        fake_lib.entries = entries;
        fake_lib.icons = icons;
        fake_lib.count = 1;
        fake_lib.cap = 1;
        fake_lib.valid_count = 1;
        icons[0].attempted = 1;
        icons[0].image = &img;

        icon_ui = nj_client_ui_new(&settings, &fake_lib);
        CHECK(icon_ui != NULL, "icon model allocates");
        if (icon_ui && a && b) {
            nj_client_ui_resize(icon_ui, TEST_W, TEST_H);
            CHECK(nj_client_ui_row_rect(icon_ui, 0, NULL, &icon_y, NULL, NULL) ==
                      1,
                  "icon row rect answers");
            icon_y += 8;                 /* the icon sits 8 px into the row */

            for (i = 0; i < 80 * 59; i++) big_pixels[i] = 0xFFFF0000u;
            for (i = 0; i < 3 * 3; i++) tiny_pixels[i] = 0xFF00FF00u;
            for (i = 3 * 3; i < 3 * 3 + 96; i++) {
                tiny_pixels[i] = 0xFFFF0000u;   /* tail must never be read */
            }

            /* 80x59 source: area sampling fills the whole 32x32 slot. */
            img.width = 80;
            img.height = 59;
            img.format = NJ_PIXFMT_ARGB8888;
            img.pixels = big_pixels;
            img.has_alpha = 1;
            nj_client_ui_draw(icon_ui, b);
            CHECK(count_region_not(b, icon_x, icon_y, icon_x + 32,
                                   icon_y + 32, 0xFFFF0000u) == 0,
                  "large icon is box filtered across the whole slot");
            CHECK(px(b, icon_x - 1, icon_y + 16) != 0xFFFF0000u,
                  "icon leaves the pixel left of the slot alone");
            CHECK(px(b, icon_x + 32, icon_y + 16) != 0xFFFF0000u,
                  "icon leaves the pixel right of the slot alone");
            CHECK(px(b, icon_x - 1, icon_y + 16) == COL_SEL,
                  "outside pixel keeps the selected row background");

            /* A tiny 3x3 source: nearest upscale, never reads its red tail. */
            img.width = 3;
            img.height = 3;
            img.pixels = tiny_pixels;
            nj_client_ui_draw(icon_ui, b);
            CHECK(count_region_not(b, icon_x, icon_y, icon_x + 32,
                                   icon_y + 32, 0xFF00FF00u) == 0,
                  "tiny icon upscales without reading past its pixels");

            /* A non-ARGB image is skipped instead of being misread. */
            img.width = 80;
            img.height = 59;
            img.pixels = big_pixels;
            img.format = NJ_PIXFMT_RGB565;
            nj_client_ui_draw(icon_ui, b);
            CHECK(px(b, icon_x + 16, icon_y + 16) != 0xFFFF0000u,
                  "non-ARGB icon is skipped");
            CHECK(count_region_not(b, icon_x, icon_y, icon_x + 32,
                                   icon_y + 32, 0xFFFF0000u) == 32 * 32,
                  "skipped icon leaves the whole slot to the background");

            /* The same frame with and without the icon: only the slot
             * changes, which proves the scaled blit stays in bounds. */
            img.format = NJ_PIXFMT_ARGB8888;
            nj_client_ui_draw(icon_ui, a);
            nj_client_ui_draw(icon_ui, b);
            CHECK(diff_pixels(a, b) == 0, "two identical draws agree");
            img.format = NJ_PIXFMT_RGB888;    /* skipped next frame */
            nj_client_ui_draw(icon_ui, b);
            CHECK(diff_outside(a, b, icon_x, icon_y, icon_x + 32,
                               icon_y + 32) == 0,
                  "skipping the icon changes nothing outside the 32x32 slot");
            CHECK(diff_pixels(a, b) > 0,
                  "skipping the icon changes pixels inside the slot");

            nj_client_ui_free(icon_ui);
        }
        surface_free(a);
        surface_free(b);
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
