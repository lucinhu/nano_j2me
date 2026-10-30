/*
 * nano_j2me - tests/test_library.c
 *
 * The on-disk games library must find games, not lose them: a runnable jar
 * gets the name its manifest declares, a broken one stays in the list with a
 * reason a player can read, a scan is interruptible, and an icon is decoded
 * once.  This pins those rules against the real jars in tests/fixtures and
 * tests/games plus a few temporary directories.
 *
 * Usage: test_library
 */
#if !defined(_POSIX_C_SOURCE) && !defined(_GNU_SOURCE)
#  define _POSIX_C_SOURCE 200809L   /* mkdtemp, symlink, getcwd under -std=c99 */
#endif

#include "nj2me/nj_library.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#  include <sys/stat.h>
#  include <unistd.h>
#endif

static int checks;
static int failures;
static int area_failures;

#define CHECK(cond, what)                                                    \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, (what));        \
        }                                                                    \
    } while (0)

#define JAR_ALPHA   "tests/fixtures/alphademo.jar"
#define JAR_1942    "tests/games/1942空战_10403.jar"
#define JAR_3D      "tests/games/3D版田园物语_6744.jar"
#define JAR_BOGUS   "tests/fixtures/bogus_count.jar"

static void area_begin(const char *name)
{
    printf("-- %s\n", name);
    area_failures = failures;
}

static void area_end(const char *name)
{
    printf("  %s %s\n", (failures == area_failures) ? "PASS" : "FAIL", name);
}

static void skip(const char *what)
{
    printf("  SKIP %s\n", what);
}

static int file_readable(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

#if !defined(_WIN32)
static int path_join(char *out, size_t cap, const char *dir, const char *name)
{
    int n = snprintf(out, cap, "%s/%s", dir, name);

    return (n >= 0 && (size_t)n < cap) ? 1 : 0;
}

static char *dup_string(const char *s)
{
    size_t n = strlen(s) + 1u;
    char *copy = (char *)malloc(n);

    if (copy != NULL) {
        memcpy(copy, s, n);
    }
    return copy;
}

static char *make_temp_dir(void)
{
    char pattern[] = "/tmp/nj_library_XXXXXX";
    char *made = mkdtemp(pattern);

    return (made != NULL) ? dup_string(made) : NULL;
}

/* Absolute path of a file relative to the test's cwd (repo root). */
static char *absolute_path(const char *rel)
{
    char cwd[NJ_LIB_PATH_MAX];
    char joined[NJ_LIB_PATH_MAX];

    if (getcwd(cwd, sizeof cwd) == NULL) {
        return NULL;
    }
    if (!path_join(joined, sizeof joined, cwd, rel)) {
        return NULL;
    }
    return dup_string(joined);
}

static int write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    size_t n = strlen(text);

    if (f == NULL) {
        return 0;
    }
    if (n > 0 && fwrite(text, 1, n, f) != n) {
        fclose(f);
        return 0;
    }
    return fclose(f) == 0;
}

static int copy_file(const char *src, const char *dst)
{
    char buf[4096];
    FILE *in;
    FILE *out;
    size_t n;
    int ok = 1;

    in = fopen(src, "rb");
    if (in == NULL) {
        return 0;
    }
    out = fopen(dst, "wb");
    if (out == NULL) {
        fclose(in);
        return 0;
    }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = 0;
            break;
        }
    }
    if (ferror(in)) {
        ok = 0;
    }
    fclose(in);
    if (fclose(out) != 0) {
        ok = 0;
    }
    return ok;
}

/* Copy just the first `limit` bytes: a jar with no central directory. */
static int copy_prefix(const char *src, const char *dst, size_t limit)
{
    char buf[256];
    FILE *in;
    FILE *out;
    size_t want = (limit < sizeof buf) ? limit : sizeof buf;
    size_t n;
    int ok;

    in = fopen(src, "rb");
    if (in == NULL) {
        return 0;
    }
    out = fopen(dst, "wb");
    if (out == NULL) {
        fclose(in);
        return 0;
    }
    n = fread(buf, 1, want, in);
    ok = (n == want) && (fwrite(buf, 1, n, out) == n);
    fclose(in);
    if (fclose(out) != 0) {
        ok = 0;
    }
    return ok;
}

static int make_dir(const char *path)
{
    return mkdir(path, 0700) == 0;
}

static void unlink_if_present(const char *path)
{
    (void)unlink(path);
}

static void rmdir_if_present(const char *path)
{
    (void)rmdir(path);
}

static int utf8_valid(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;

    while (*p != 0u) {
        if (*p < 0x80u) {
            p += 1;
        } else if ((*p & 0xE0u) == 0xC0u) {
            if ((p[1] & 0xC0u) != 0x80u) {
                return 0;
            }
            p += 2;
        } else if ((*p & 0xF0u) == 0xE0u) {
            if ((p[1] & 0xC0u) != 0x80u || (p[2] & 0xC0u) != 0x80u) {
                return 0;
            }
            p += 3;
        } else if ((*p & 0xF8u) == 0xF0u) {
            if ((p[1] & 0xC0u) != 0x80u || (p[2] & 0xC0u) != 0x80u ||
                (p[3] & 0xC0u) != 0x80u) {
                return 0;
            }
            p += 4;
        } else {
            return 0;
        }
    }
    return 1;
}
#endif /* !_WIN32 */

/* ------------------------------------------------------------------ */
/* name rules                                                         */
/* ------------------------------------------------------------------ */

static void test_jar_names(void)
{
    area_begin("nj_lib_is_jar_name");

    CHECK(nj_lib_is_jar_name("game.jar") == 1, "game.jar is a jar name");
    CHECK(nj_lib_is_jar_name("GAME.JAR") == 1, ".JAR is case insensitive");
    CHECK(nj_lib_is_jar_name("Mixed.Jar") == 1, "mixed case is accepted");
    CHECK(nj_lib_is_jar_name("dir/game.jar") == 1, "a path is judged by its basename");
    CHECK(nj_lib_is_jar_name("游戏.jar") == 1, "a UTF-8 basename is accepted");
    CHECK(nj_lib_is_jar_name(".jar") == 1, ".jar alone is a suffix, not an empty basename");
    CHECK(nj_lib_is_jar_name("game.jarr") == 0, "jarr is not jar");
    CHECK(nj_lib_is_jar_name("game.jarx") == 0, "jarx is not jar");
    CHECK(nj_lib_is_jar_name("game") == 0, "no suffix is not a jar");
    CHECK(nj_lib_is_jar_name("game.jar/") == 0, "a trailing slash leaves no basename");
    CHECK(nj_lib_is_jar_name("") == 0, "the empty name is not a jar");
    CHECK(nj_lib_is_jar_name(NULL) == 0, "NULL is not a jar");

    area_end("nj_lib_is_jar_name");
}

/* ------------------------------------------------------------------ */
/* probing real jars                                                  */
/* ------------------------------------------------------------------ */

static void test_probe_real_jars(void)
{
    nj_lib_entry e;

    area_begin("probe: real jars");

    if (!file_readable(JAR_ALPHA)) {
        skip("tests/fixtures/alphademo.jar missing");
    } else {
        memset(&e, 0xAA, sizeof e);        /* prove every field is written */
        CHECK(nj_lib_probe(JAR_ALPHA, &e) == 1, "alphademo.jar is runnable");
        CHECK(e.valid == 1, "valid is set");
        CHECK(strcmp(e.file, "alphademo.jar") == 0, "file is the basename");
        CHECK(strcmp(e.name, "AlphaDemo") == 0, "the MIDlet-1 display name wins");
        CHECK(strcmp(e.midlet, "AlphaDemo") == 0, "the MIDlet-1 class is kept");
        CHECK(e.icon[0] == '\0', "alphademo declares no icon");
        CHECK(e.size > 0, "the file size is recorded");
        CHECK(e.note[0] == '\0', "a valid jar carries no note");
        CHECK(e.path[0] == '/', "the probe stores an absolute path");
        CHECK(strstr(e.path, "alphademo.jar") != NULL, "and it looks like the input");
        CHECK(nj_lib_probe(JAR_ALPHA, NULL) == 1, "a NULL out is allowed");
    }

    if (!file_readable(JAR_1942)) {
        skip("tests/games/1942空战_10403.jar missing");
    } else {
        CHECK(nj_lib_probe(JAR_1942, &e) == 1, "1942 is runnable");
        CHECK(e.valid == 1, "valid is set");
        CHECK(strcmp(e.name, "1942AirCombat") == 0, "name comes from MIDlet-1");
        CHECK(strcmp(e.midlet, "com.lemonquest.aircombat.GameMIDlet") == 0,
              "class comes from MIDlet-1");
        CHECK(strcmp(e.icon, "icon.png") == 0,
              "the icon field names the zip entry, without the leading slash");
        CHECK(e.size > 0, "the file size is recorded");
        CHECK(strcmp(e.file, "1942空战_10403.jar") == 0,
              "the UTF-8 basename is kept as-is");
    }

    if (!file_readable(JAR_3D)) {
        skip("tests/games/3D版田园物语_6744.jar missing");
    } else {
        CHECK(nj_lib_probe(JAR_3D, &e) == 1, "the 3D game is runnable");
        CHECK(strcmp(e.name, "3D版田园物语") == 0,
              "a CJK MIDlet-Name is kept as UTF-8");
        CHECK(strlen(e.name) == 17u, "two ASCII bytes plus five CJK characters");
        CHECK(utf8_valid(e.name), "the display name stays valid UTF-8");
        CHECK(strcmp(e.midlet, "current.RPGMIDlet") == 0, "the class is kept");
        CHECK(strcmp(e.icon, "icon.png") == 0, "the icon entry exists in the jar");
    }

    area_end("probe: real jars");
}

static void test_probe_broken(void)
{
    static const char *bogus[] = {
        "tests/fixtures/bogus_cd_offset.jar",
        "tests/fixtures/bogus_cd_sig.jar",
        "tests/fixtures/bogus_count.jar"
    };
    nj_lib_entry e;
    unsigned i;

    area_begin("probe: broken files");

    for (i = 0; i < sizeof bogus / sizeof bogus[0]; i++) {
        if (!file_readable(bogus[i])) {
            skip(bogus[i]);
            continue;
        }
        memset(&e, 0xAA, sizeof e);
        CHECK(nj_lib_probe(bogus[i], &e) == 0, "a bad central directory is not runnable");
        CHECK(e.valid == 0, "valid is cleared");
        CHECK(e.note[0] != '\0', "there is a reason to show");
        CHECK(e.name[0] != '\0', "even a broken file gets a display name");
        CHECK(e.file[0] != '\0', "and a file name");
        CHECK(e.path[0] == '/', "and an absolute path");
        CHECK(e.midlet[0] == '\0', "no class is invented");
        CHECK(e.icon[0] == '\0', "no icon is invented");
        CHECK(e.size > 0, "the size still comes from the file");
    }

    if (file_readable("tests/fixtures/empty.jar")) {
        CHECK(nj_lib_probe("tests/fixtures/empty.jar", &e) == 0,
              "an empty zip has no manifest");
        CHECK(e.valid == 0 && e.note[0] != '\0', "and says why");
        CHECK(strcmp(e.name, "empty") == 0, "the fallback name drops .jar");
    } else {
        skip("tests/fixtures/empty.jar missing");
    }

    if (file_readable("tests/fixtures/empty_raw.bin")) {
        CHECK(nj_lib_probe("tests/fixtures/empty_raw.bin", &e) == 0,
              "a two-byte file is not a zip");
        CHECK(e.size == 2, "its size is still recorded");
        CHECK(e.note[0] != '\0', "and there is a reason");
    } else {
        skip("tests/fixtures/empty_raw.bin missing");
    }

    memset(&e, 0xAA, sizeof e);
    CHECK(nj_lib_probe("no/such/game.jar", &e) == 0, "a missing file is not runnable");
    CHECK(e.valid == 0 && e.note[0] != '\0', "and gets a note");
    CHECK(e.size == 0, "with no size");
    CHECK(e.name[0] != '\0', "but still a name");

    memset(&e, 0xAA, sizeof e);
    CHECK(nj_lib_probe(NULL, &e) == 0, "NULL path is refused, not crashed on");
    CHECK(e.name[0] != '\0' && e.note[0] != '\0', "NULL path still fills name and note");
    CHECK(nj_lib_probe(NULL, NULL) == 0, "NULL path and NULL out are both safe");

    area_end("probe: broken files");
}

/* ------------------------------------------------------------------ */
/* display name cleaning                                              */
/* ------------------------------------------------------------------ */

#if !defined(_WIN32)
static void test_name_cleaning(void)
{
    char *base;
    char path[1200];
    nj_lib_entry e;

    area_begin("display name cleaning");

    base = make_temp_dir();
    if (base == NULL) {
        skip("cannot create a temporary directory");
        area_end("display name cleaning");
        return;
    }

    /* Control bytes become spaces, runs collapse, ends are trimmed. */
    if (path_join(path, sizeof path, base, "  spaced   name .jar")) {
        write_text(path, "not a zip\n");
        CHECK(nj_lib_probe(path, &e) == 0, "a text file named .jar is not runnable");
        CHECK(strcmp(e.name, "spaced name") == 0,
              "spaces are trimmed and collapsed, .jar is dropped");
        CHECK(strcmp(e.file, "  spaced   name .jar") == 0,
              "the raw basename is kept in file");
    }

    if (path_join(path, sizeof path, base, "a\tb.jar")) {
        write_text(path, "x");
        CHECK(nj_lib_probe(path, &e) == 0, "a tab in the name does not break probing");
        CHECK(strcmp(e.name, "a b") == 0, "a control byte becomes a space");
    }

    /* A 70-character CJK name is 210 bytes: the cap must not split a char. */
    {
        char long_name[NJ_LIB_FILE_MAX * 2];
        size_t pos = 0;
        int i;

        for (i = 0; i < 70; i++) {
            memcpy(long_name + pos, "\xE6\xB5\x8B", 3u); /* 测 */
            pos += 3u;
        }
        memcpy(long_name + pos, ".jar", 4u);
        pos += 4u;
        long_name[pos] = '\0';
        if (path_join(path, sizeof path, base, long_name)) {
            write_text(path, "x");
            CHECK(nj_lib_probe(path, &e) == 0, "a very long name is still probed");
            CHECK(strlen(e.name) == 189u,
                  "the name is capped at 63 whole CJK characters");
            CHECK(utf8_valid(e.name), "and the truncation keeps valid UTF-8");
            CHECK(strlen(e.name) <= NJ_LIB_NAME_MAX - 1u,
                  "and stays inside the public field");
            CHECK(memcmp(e.name, long_name, 189u) == 0, "it is a prefix of the file name");
        }
    }

    /* Empty and truncated files must not crash the probe. */
    if (path_join(path, sizeof path, base, "empty_file.jar")) {
        write_text(path, "");
        CHECK(nj_lib_probe(path, &e) == 0, "an empty file is not runnable");
        CHECK(e.size == 0, "its size is 0");
        CHECK(e.note[0] != '\0', "and the reason is filled");
        unlink_if_present(path);
    }
    if (file_readable(JAR_ALPHA) && path_join(path, sizeof path, base, "truncated.jar")) {
        if (copy_prefix(JAR_ALPHA, path, 100u)) {
            CHECK(nj_lib_probe(path, &e) == 0, "a truncated jar is not runnable");
            CHECK(e.note[0] != '\0', "and says why");
            CHECK(e.name[0] != '\0', "and still gets a name");
        }
        unlink_if_present(path);
    }

    if (path_join(path, sizeof path, base, "plain.txt")) {
        write_text(path, "hello\n");
        CHECK(nj_lib_probe(path, &e) == 0, "a plain text file is not runnable");
        CHECK(strcmp(e.name, "plain.txt") == 0, "the fallback name keeps a non-jar suffix");
        CHECK(e.note[0] != '\0', "and the reason is filled");
    }

    /* A runnable jar keeps its manifest name even when the file is ugly. */
    if (file_readable(JAR_ALPHA) && path_join(path, sizeof path, base, "  ugly   name .jar")) {
        if (copy_file(JAR_ALPHA, path)) {
            CHECK(nj_lib_probe(path, &e) == 1, "a jar with a messy file name still runs");
            CHECK(strcmp(e.name, "AlphaDemo") == 0,
                  "the manifest name wins over the file name");
            CHECK(strcmp(e.file, "  ugly   name .jar") == 0,
                  "the file field keeps the real basename");
        }
    } else {
        skip(JAR_ALPHA " missing: cannot test a messy-name jar");
    }

    unlink_if_present(path);
    {
        char p[1200];
        if (path_join(p, sizeof p, base, "  spaced   name .jar")) {
            unlink_if_present(p);
        }
        if (path_join(p, sizeof p, base, "a\tb.jar")) {
            unlink_if_present(p);
        }
        if (path_join(p, sizeof p, base, "plain.txt")) {
            unlink_if_present(p);
        }
        if (path_join(p, sizeof p, base, "  ugly   name .jar")) {
            unlink_if_present(p);
        }
    }
    /* Remove the long CJK file by rebuilding its name. */
    {
        char long_name[NJ_LIB_FILE_MAX * 2];
        size_t pos = 0;
        int i;

        for (i = 0; i < 70; i++) {
            memcpy(long_name + pos, "\xE6\xB5\x8B", 3u);
            pos += 3u;
        }
        memcpy(long_name + pos, ".jar", 4u);
        pos += 4u;
        long_name[pos] = '\0';
        if (path_join(path, sizeof path, base, long_name)) {
            unlink_if_present(path);
        }
    }
    rmdir_if_present(base);
    free(base);

    area_end("display name cleaning");
}
#else
static void test_name_cleaning(void)
{
    area_begin("display name cleaning");
    skip("temporary directories are POSIX-only");
    area_end("display name cleaning");
}
#endif

/* ------------------------------------------------------------------ */
/* adding entries, icons                                              */
/* ------------------------------------------------------------------ */

static void test_add_and_icons(void)
{
    nj_library *lib;
    const nj_image *image;
    const nj_image *again;
    int index;
    char saved_dir[NJ_LIB_PATH_MAX];

    area_begin("nj_lib_add and nj_lib_icon");

    if (!file_readable(JAR_ALPHA)) {
        skip(JAR_ALPHA " missing: cannot test adding");
        area_end("nj_lib_add and nj_lib_icon");
        return;
    }

    lib = nj_library_new();
    CHECK(lib != NULL, "nj_library_new returns an object");
    if (lib == NULL) {
        area_end("nj_lib_add and nj_lib_icon");
        return;
    }
    CHECK(lib->count == 0 && lib->cap == 0 && lib->valid_count == 0,
          "a new library is empty");
    CHECK(lib->dir[0] == '\0' && lib->done == 0 && lib->scan_active == 0,
          "and has no scan state");

    index = nj_lib_add(lib, JAR_ALPHA);
    CHECK(index == 0, "the first add returns index 0");
    CHECK(lib->count == 1 && lib->valid_count == 1, "the library grew by one valid game");
    CHECK(lib->cap >= 16, "the entry array starts at the public initial capacity");
    CHECK(lib->icons != NULL && lib->icons[0].image == NULL &&
          lib->icons[0].attempted == 0,
          "the icon slot starts untouched");
    CHECK(strcmp(lib->entries[0].name, "AlphaDemo") == 0, "the probe result was stored");
    CHECK(nj_lib_add(lib, JAR_ALPHA) == index, "a duplicate path returns the existing index");
    CHECK(lib->count == 1 && lib->valid_count == 1, "and does not add a row");
    CHECK(nj_lib_add(lib, NULL) == -1, "a NULL path is refused");
    CHECK(nj_lib_add(NULL, JAR_ALPHA) == -1, "a NULL library is refused");

    image = nj_lib_icon(lib, 0);
    CHECK(image == NULL, "alphademo has no icon to decode");
    CHECK(lib->icons[0].attempted == 1, "a failed icon load is remembered");
    again = nj_lib_icon(lib, 0);
    CHECK(again == NULL && lib->icons[0].attempted == 1, "the failure stays cached");
    CHECK(nj_lib_icon(lib, -1) == NULL, "a negative index has no icon");
    CHECK(nj_lib_icon(lib, 1) == NULL, "an out-of-range index has no icon");
    CHECK(nj_lib_icon(NULL, 0) == NULL, "a NULL library has no icon");

    if (file_readable(JAR_1942)) {
        index = nj_lib_add(lib, JAR_1942);
        CHECK(index == 1, "the second add returns index 1");
        image = nj_lib_icon(lib, index);
        CHECK(image != NULL, "the 1942 icon decodes");
        if (image != NULL) {
            CHECK(image->width == 80 && image->height == 59,
                  "and has the PNG's 80x59 size");
            CHECK(image->width != 1 || image->height != 1,
                  "and is not a placeholder 1x1 image");
            CHECK(image->pixels != NULL, "and has pixels");
        }
        again = nj_lib_icon(lib, index);
        CHECK(again == image, "the decoded icon is cached, not decoded again");
        CHECK(lib->icons[index].attempted == 1, "and marked attempted");
    } else {
        skip(JAR_1942 " missing: cannot test icon decoding");
    }

    /* Clear must release entries and every decoded icon. */
    memcpy(saved_dir, lib->dir, sizeof saved_dir);
    nj_library_clear(lib);
    CHECK(lib->count == 0 && lib->cap == 0 && lib->valid_count == 0,
          "clear drops the entries");
    CHECK(lib->entries == NULL && lib->icons == NULL, "and frees both arrays");
    CHECK(lib->scan_active == 0 && lib->done == 0, "and forgets the scan");
    CHECK(memcmp(lib->dir, saved_dir, sizeof saved_dir) == 0,
          "but keeps dir for a rescan");
    CHECK(nj_lib_icon(lib, 0) == NULL, "after clear there is no icon");

    /* The library remains usable after clear. */
    CHECK(nj_lib_add(lib, JAR_ALPHA) == 0, "entries can be added again");
    CHECK(lib->count == 1 && lib->valid_count == 1, "and counted again");

    nj_library_clear(lib);
    CHECK(lib->entries == NULL && lib->icons == NULL, "clear is idempotent");
    nj_library_free(lib);

    area_end("nj_lib_add and nj_lib_icon");
}

/* ------------------------------------------------------------------ */
/* progressive scan                                                   */
/* ------------------------------------------------------------------ */

#if !defined(_WIN32)
typedef struct expected_jar {
    char name[NJ_LIB_FILE_MAX];   /* basename on disk */
    int  valid;
} expected_jar;

static const nj_lib_entry *find_entry(const nj_library *lib, const char *file)
{
    int i;

    for (i = 0; i < lib->count; i++) {
        if (strcmp(lib->entries[i].file, file) == 0) {
            return &lib->entries[i];
        }
    }
    return NULL;
}

static void test_progressive_scan(void)
{
    nj_library *lib;
    char *base;
    char *alpha;
    char path[1200];
    char target[1200];
    expected_jar expected[8];
    int expected_count = 0;
    int expected_valid = 0;
    int i;
    int guard;
    int total;
    static const char *const candidates[] = {
        "a_upper.JAR", "b_alpha.jar", "c_3d.jar", "d_bogus.jar",
        "  spaced   name .jar", "zz_broken.jar", "readme.txt"
    };

    area_begin("progressive scan");

    base = make_temp_dir();
    if (base == NULL) {
        skip("cannot create a temporary directory");
        area_end("progressive scan");
        return;
    }
    alpha = file_readable(JAR_ALPHA) ? absolute_path(JAR_ALPHA) : NULL;
    if (alpha == NULL) {
        skip(JAR_ALPHA " missing: nothing runnable to scan");
        rmdir_if_present(base);
        free(base);
        area_end("progressive scan");
        return;
    }

    /* A subdirectory: scanning must not descend into it. */
    if (path_join(path, sizeof path, base, "subdir")) {
        make_dir(path);
        if (path_join(target, sizeof target, base, "subdir/inside.jar")) {
            (void)symlink(alpha, target);
        }
    }
    /* A directory that happens to end in .jar is still a directory. */
    if (path_join(path, sizeof path, base, "dir.jar")) {
        make_dir(path);
    }
    /* Two runnable games with the same manifest name (one via .JAR case). */
    if (path_join(target, sizeof target, base, "a_upper.JAR")) {
        if (symlink(alpha, target) == 0) {
            snprintf(expected[expected_count].name, NJ_LIB_FILE_MAX, "%s", "a_upper.JAR");
            expected[expected_count].valid = 1;
            expected_count++;
            expected_valid++;
        }
    }
    if (path_join(target, sizeof target, base, "b_alpha.jar")) {
        if (symlink(alpha, target) == 0) {
            snprintf(expected[expected_count].name, NJ_LIB_FILE_MAX, "%s", "b_alpha.jar");
            expected[expected_count].valid = 1;
            expected_count++;
            expected_valid++;
        }
    }
    /* A third, differently named runnable game, when the corpus has it. */
    {
        char *three_d = file_readable(JAR_3D) ? absolute_path(JAR_3D) : NULL;
        if (three_d != NULL &&
            path_join(target, sizeof target, base, "c_3d.jar") &&
            symlink(three_d, target) == 0) {
            snprintf(expected[expected_count].name, NJ_LIB_FILE_MAX, "%s", "c_3d.jar");
            expected[expected_count].valid = 1;
            expected_count++;
            expected_valid++;
        }
        free(three_d);
    }
    /* A broken jar, when the corpus has one; otherwise a plain text .jar. */
    {
        char *bogus = file_readable(JAR_BOGUS) ? absolute_path(JAR_BOGUS) : NULL;
        if (bogus != NULL &&
            path_join(target, sizeof target, base, "d_bogus.jar") &&
            symlink(bogus, target) == 0) {
            snprintf(expected[expected_count].name, NJ_LIB_FILE_MAX, "%s", "d_bogus.jar");
            expected[expected_count].valid = 0;
            expected_count++;
        }
        free(bogus);
    }
    /* Always present: two files that are not runnable jars. */
    if (path_join(path, sizeof path, base, "  spaced   name .jar")) {
        write_text(path, "not a zip\n");
        snprintf(expected[expected_count].name, NJ_LIB_FILE_MAX, "%s", "  spaced   name .jar");
        expected[expected_count].valid = 0;
        expected_count++;
    }
    if (path_join(path, sizeof path, base, "zz_broken.jar")) {
        write_text(path, "certainly not a zip\n");
        snprintf(expected[expected_count].name, NJ_LIB_FILE_MAX, "%s", "zz_broken.jar");
        expected[expected_count].valid = 0;
        expected_count++;
    }
    if (path_join(path, sizeof path, base, "readme.txt")) {
        write_text(path, "read me\n");
    }
    if (path_join(path, sizeof path, base, "emptydir")) {
        make_dir(path);
    }

    lib = nj_library_new();
    CHECK(lib != NULL, "a library object is available");
    if (lib == NULL) {
        free(alpha);
        rmdir_if_present(base);
        free(base);
        area_end("progressive scan");
        return;
    }

    CHECK(nj_lib_scan_begin(lib, base) == 1, "scan_begin opens the directory");
    CHECK(nj_lib_scan_busy(lib) == 1, "the scan is busy");
    CHECK(lib->done == 0, "and not done");
    CHECK(lib->pending_count == expected_count,
          "every .jar file was queued, and nothing else");
    CHECK(lib->pending_index == 0, "nothing has been probed yet");
    CHECK(strcmp(lib->dir, base) == 0, "dir stores the absolute directory");
    CHECK(lib->count == 0, "the entry list starts empty");

    CHECK(nj_lib_scan_step(lib, 0) == 0, "a zero budget probes nothing");
    CHECK(lib->count == 0 && nj_lib_scan_busy(lib) == 1, "and leaves the scan running");

    CHECK(nj_lib_scan_step(lib, 1) == 1, "one unit of budget probes one jar");
    CHECK(lib->count == 1, "and appends one entry");
    CHECK(nj_lib_scan_busy(lib) == 1, "the scan is still running");

    total = lib->count;
    guard = expected_count + 4;
    while (nj_lib_scan_busy(lib) && guard-- > 0) {
        int before = lib->count;
        int added = nj_lib_scan_step(lib, 1);

        CHECK(added >= 0 && added <= 1, "budget 1 appends at most one entry");
        CHECK(lib->count == before + added, "the entry count grows by what step returns");
        total += added;
        if (guard < 0) {
            break;
        }
    }
    CHECK(guard >= 0, "the scan finishes in a bounded number of steps");
    CHECK(nj_lib_scan_busy(lib) == 0, "the scan is no longer busy");
    CHECK(lib->done == 1, "and is marked done");
    CHECK(total == expected_count, "every queued jar became an entry");
    CHECK(lib->count == expected_count, "the library holds them all");
    CHECK(lib->valid_count == expected_valid, "valid_count counts the runnable ones");
    CHECK(lib->pending == NULL && lib->pending_count == 0,
          "the pending queue is released at the end");
    CHECK(nj_lib_scan_step(lib, 8) == 0, "stepping a finished scan is harmless");

    /* Sort rule: runnable games first, then by display name. */
    for (i = 0; i + 1 < lib->count; i++) {
        if (lib->entries[i].valid == 0) {
            CHECK(lib->entries[i + 1].valid == 0,
                  "no runnable game sorts after a broken one");
        } else if (lib->entries[i + 1].valid != 0) {
            CHECK(strcmp(lib->entries[i].name, lib->entries[i + 1].name) <= 0,
                  "runnable games are sorted by display name");
        }
    }
    for (i = 0; i < lib->count; i++) {
        CHECK(lib->entries[i].path[0] == '/', "every entry has an absolute path");
        CHECK(strncmp(lib->entries[i].path, base, strlen(base)) == 0,
              "and the path is inside the scanned directory");
        CHECK(lib->entries[i].file[0] != '\0', "every entry has a file name");
        CHECK(lib->entries[i].name[0] != '\0', "every entry has a display name");
        if (!lib->entries[i].valid) {
            CHECK(lib->entries[i].note[0] != '\0', "a broken entry explains itself");
        } else {
            CHECK(lib->entries[i].note[0] == '\0', "a runnable entry has no note");
        }
    }
    for (i = 0; i < (int)(sizeof candidates / sizeof candidates[0]); i++) {
        if (strcmp(candidates[i], "readme.txt") == 0) {
            CHECK(find_entry(lib, candidates[i]) == NULL,
                  "a non-jar file is not listed");
        }
    }

    /* Subdirectories are not followed. */
    CHECK(find_entry(lib, "inside.jar") == NULL,
          "a jar inside a subdirectory is not scanned");

    /* The .JAR spelling and the no-icon runnable jar both probed cleanly. */
    {
        const nj_lib_entry *upper = find_entry(lib, "a_upper.JAR");
        const nj_lib_entry *spaced = find_entry(lib, "  spaced   name .jar");

        CHECK(upper != NULL, "a symlink to a jar is listed");
        if (upper != NULL) {
            CHECK(upper->valid == 1, "and is runnable");
            CHECK(strcmp(upper->name, "AlphaDemo") == 0, "with its manifest name");
        }
        CHECK(spaced != NULL, "the space-ridden .jar is listed");
        if (spaced != NULL) {
            CHECK(spaced->valid == 0, "but is broken");
            CHECK(strcmp(spaced->name, "spaced name") == 0,
                  "and gets a cleaned display name");
        }
        CHECK(find_entry(lib, "dir.jar") == NULL,
              "a directory named .jar is not listed as a game");
    }

    /* A blocking rescan rebuilds the same list. */
    CHECK(nj_lib_rescan(lib) == expected_count, "rescan returns the entry count");
    CHECK(lib->count == expected_count && lib->valid_count == expected_valid,
          "and rebuilds the same library");
    CHECK(nj_lib_scan_busy(lib) == 0 && lib->done == 1, "a rescan completes");

    /* An empty directory scans to an empty list, not an error. */
    if (path_join(path, sizeof path, base, "emptydir")) {
        CHECK(nj_lib_scan(lib, path) == 0, "an empty directory yields no entries");
        CHECK(lib->count == 0 && lib->valid_count == 0, "and the library is empty");
        CHECK(lib->done == 1 && nj_lib_scan_busy(lib) == 0, "but the scan is done");
    }

    /* A directory that cannot be opened fails cleanly. */
    CHECK(nj_lib_scan_begin(lib, "no/such/directory") == 0,
          "an unopenable directory fails");
    CHECK(lib->count == 0 && lib->done == 1 && nj_lib_scan_busy(lib) == 0,
          "and leaves an empty, consistent library");
    CHECK(nj_lib_scan_begin(lib, NULL) == 0, "a NULL directory fails");
    CHECK(nj_lib_scan(lib, NULL) == 0, "a NULL blocking scan fails");

    nj_library_free(lib);

    /* Clean up the temporary tree. */
    for (i = 0; i < (int)(sizeof candidates / sizeof candidates[0]); i++) {
        if (path_join(path, sizeof path, base, candidates[i])) {
            unlink_if_present(path);
        }
    }
    if (path_join(path, sizeof path, base, "subdir/inside.jar")) {
        unlink_if_present(path);
    }
    if (path_join(path, sizeof path, base, "subdir")) {
        rmdir_if_present(path);
    }
    if (path_join(path, sizeof path, base, "dir.jar")) {
        rmdir_if_present(path);
    }
    if (path_join(path, sizeof path, base, "emptydir")) {
        rmdir_if_present(path);
    }
    rmdir_if_present(base);
    free(alpha);
    free(base);

    area_end("progressive scan");
}
#else
static void test_progressive_scan(void)
{
    area_begin("progressive scan");
    skip("temporary directories are POSIX-only");
    area_end("progressive scan");
}
#endif

/* ------------------------------------------------------------------ */
/* directory listing                                                  */
/* ------------------------------------------------------------------ */

#if !defined(_WIN32)
static void test_directory_listing(void)
{
    char *base;
    char path[1200];
    nj_lib_dir_entry *rows = NULL;
    int count = -1;
    int n;

    area_begin("nj_lib_list_dir");

    base = make_temp_dir();
    if (base == NULL) {
        skip("cannot create a temporary directory");
        area_end("nj_lib_list_dir");
        return;
    }
    if (path_join(path, sizeof path, base, "adir")) {
        make_dir(path);
    }
    if (path_join(path, sizeof path, base, "zdir")) {
        make_dir(path);
    }
    /* A symlinked directory is a directory. */
    if (path_join(path, sizeof path, base, "bdir")) {
        (void)symlink("zdir", path);
    }
    if (path_join(path, sizeof path, base, "A.jar")) {
        write_text(path, "x");
    }
    if (path_join(path, sizeof path, base, "z.jar")) {
        write_text(path, "yz");
    }
    /* A symlinked file is a file, with the target's size. */
    if (path_join(path, sizeof path, base, "link.jar")) {
        (void)symlink("A.jar", path);
    }
    if (path_join(path, sizeof path, base, "notes.txt")) {
        write_text(path, "not a game\n");
    }

    n = nj_lib_list_dir(base, &rows, &count);
    CHECK(n == 6, "five rows plus the symlinked file are listed");
    CHECK(count == n, "count matches the return value");
    CHECK(rows != NULL, "the row array is allocated");
    if (rows != NULL && count >= 6) {
        int expected_dirs = 3;
        int i;

        for (i = 0; i < expected_dirs; i++) {
            CHECK(rows[i].is_dir == 1, "directories come first");
        }
        CHECK(strcmp(rows[0].name, "adir") == 0, "directories are sorted by name");
        CHECK(strcmp(rows[1].name, "bdir") == 0, "a symlinked directory is a directory");
        CHECK(strcmp(rows[2].name, "zdir") == 0, "and sorts by its own name");
        for (i = expected_dirs; i < count; i++) {
            CHECK(rows[i].is_dir == 0, "the .jar files follow the directories");
        }
        CHECK(strcmp(rows[3].name, "A.jar") == 0, "jar files are sorted by name");
        CHECK(strcmp(rows[4].name, "link.jar") == 0, "a symlinked jar is listed");
        CHECK(strcmp(rows[5].name, "z.jar") == 0, "with the rest in order");
        CHECK(rows[3].size == 1, "a regular jar reports its size");
        CHECK(rows[4].size == 1, "a symlinked jar reports the target's size");
    }
    nj_lib_free_list(rows);
    rows = NULL;

    /* NULL outputs are allowed; the count is still returnable. */
    CHECK(nj_lib_list_dir(base, NULL, NULL) == 6, "NULL out and NULL count are allowed");
    CHECK(nj_lib_list_dir(NULL, &rows, &count) == 0 && rows == NULL && count == 0,
          "a NULL directory lists nothing");
    if (path_join(path, sizeof path, base, "no/such")) {
        CHECK(nj_lib_list_dir(path, &rows, &count) == 0, "a missing directory lists nothing");
        CHECK(rows == NULL && count == 0, "and clears the outputs");
    }
    if (path_join(path, sizeof path, base, "A.jar")) {
        CHECK(nj_lib_list_dir(path, &rows, &count) == 0, "a file path lists nothing");
    }
    nj_lib_free_list(NULL);

    /* Clean up. */
    if (path_join(path, sizeof path, base, "bdir")) {
        unlink_if_present(path);
    }
    if (path_join(path, sizeof path, base, "link.jar")) {
        unlink_if_present(path);
    }
    if (path_join(path, sizeof path, base, "A.jar")) {
        unlink_if_present(path);
    }
    if (path_join(path, sizeof path, base, "z.jar")) {
        unlink_if_present(path);
    }
    if (path_join(path, sizeof path, base, "notes.txt")) {
        unlink_if_present(path);
    }
    if (path_join(path, sizeof path, base, "adir")) {
        rmdir_if_present(path);
    }
    if (path_join(path, sizeof path, base, "zdir")) {
        rmdir_if_present(path);
    }
    rmdir_if_present(base);
    free(base);

    area_end("nj_lib_list_dir");
}
#else
static void test_directory_listing(void)
{
    area_begin("nj_lib_list_dir");
    skip("temporary directories are POSIX-only");
    area_end("nj_lib_list_dir");
}
#endif

/* ------------------------------------------------------------------ */
/* NULL and empty arguments                                           */
/* ------------------------------------------------------------------ */

static void test_null_arguments(void)
{
    nj_library *lib;

    area_begin("NULL and empty arguments");

    nj_library_clear(NULL);
    nj_library_free(NULL);
    CHECK(nj_lib_scan_busy(NULL) == 0, "a NULL library is not scanning");
    CHECK(nj_lib_scan(NULL, ".") == 0, "a NULL blocking scan returns 0");
    CHECK(nj_lib_scan_begin(NULL, ".") == 0, "a NULL library cannot begin a scan");
    CHECK(nj_lib_scan_step(NULL, 4) == 0, "a NULL library cannot step a scan");
    CHECK(nj_lib_rescan(NULL) == 0, "a NULL library cannot rescan");
    CHECK(nj_lib_icon(NULL, 0) == NULL, "a NULL library has no icon");
    CHECK(nj_lib_add(NULL, NULL) == -1, "a NULL library cannot add");
    CHECK(nj_lib_probe(NULL, NULL) == 0, "NULL probe arguments are refused");
    CHECK(nj_lib_is_jar_name(NULL) == 0, "NULL is not a jar name");
    {
        nj_lib_dir_entry *rows = (nj_lib_dir_entry *)&lib;
        int count = -1;

        CHECK(nj_lib_list_dir(NULL, &rows, &count) == 0,
              "a NULL directory lists nothing");
        CHECK(rows == NULL && count == 0, "and clears the outputs");
    }
    nj_lib_free_list(NULL);

    lib = nj_library_new();
    CHECK(lib != NULL, "a library object is available");
    if (lib != NULL) {
        CHECK(nj_lib_rescan(lib) == 0, "rescanning a never-scanned library returns 0");
        CHECK(nj_lib_scan_begin(lib, NULL) == 0, "scan_begin(NULL dir) fails");
        CHECK(lib->count == 0 && lib->done == 1, "leaving an empty, done library");
        nj_library_free(lib);
    }

    area_end("NULL and empty arguments");
}

/* ------------------------------------------------------------------ */

int main(void)
{
    printf("== nj_library: a games folder becomes a list of runnable jars ==\n");
    test_jar_names();
    test_probe_real_jars();
    test_probe_broken();
    test_name_cleaning();
    test_add_and_icons();
    test_progressive_scan();
    test_directory_listing();
    test_null_arguments();
    printf("  %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
