/*
 * nano_j2me - javax.microedition.io.file (JSR 75).
 *
 * Builds a known directory tree, points a MIDlet at it through nj_port.fs_dir,
 * and asserts on what the MIDlet saw.
 *
 * THE SANDBOX ASSERTIONS ARE THE IMPORTANT ONES.  A file API that can be talked
 * out of its root is worse than one that is missing, because the missing one
 * fails loudly and the escapable one works until someone notices.  Three
 * different ".." shapes are tried, and all three must be refused.
 *
 * Usage: test_fs <fsdemo.jar>
 */
#include "nj2me/nj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks;
static int fails;

static void ok(int cond, const char *what)
{
    checks++;
    if (!cond) {
        fails++;
        printf("  FAIL %s\n", what);
    }
}

typedef struct capture { char buf[8192]; size_t n; } capture;

static void cap_log(void *user, int level, const char *msg)
{
    capture *c = (capture *)user;
    size_t len = strlen(msg);
    (void)level;
    if (c->n + len + 2u >= sizeof(c->buf)) { return; }
    memcpy(c->buf + c->n, msg, len);
    c->n += len;
    c->buf[c->n++] = '\n';
    c->buf[c->n] = '\0';
}

/* The text after "FS <key>=" on its line, or NULL. */
static const char *field(const capture *c, const char *key, char *out, size_t cap)
{
    const char *p = c->buf;
    char k[64];
    size_t kl;
    snprintf(k, sizeof(k), "FS %s=", key);
    kl = strlen(k);
    while ((p = strstr(p, k)) != NULL) {
        const char *v = p + kl;
        size_t i = 0;
        while (v[i] && v[i] != '\n' && i + 1u < cap) { out[i] = v[i]; i++; }
        out[i] = '\0';
        return out;
    }
    return NULL;
}

static int contains(const capture *c, const char *s)
{
    return strstr(c->buf, s) != NULL;
}

/*
 * Compare a "a|b|c|" listing as a SET.
 *
 * readdir returns entries in whatever order the filesystem keeps them, so
 * asserting on the order asserts on the HOST, not on this runtime - it passed
 * by luck on one machine and would fail on another with a different directory
 * layout.  The property that matters is which entries came back, not the
 * sequence.
 */
static int same_set(const char *got, const char *want)
{
    /*
     * Compare "a|b|c|" as a SET, without strtok_r (not available under
     * -std=c99 without _POSIX_C_SOURCE, and this test does not need it).
     *
     * readdir returns entries in whatever order the filesystem keeps them, so
     * asserting on the order asserts on the HOST, not on this runtime - it
     * passed by luck here and would fail elsewhere.  Which entries came back is
     * the property that matters.
     */
    char g[600], w[600];
    int gt = 0, wt = 0;
    const char *p;

    /*
     * Both sides are normalised to "|a|b|c|" before counting: the runtime's
     * listing already ends with a separator and the expectation written in the
     * test does not, so without this the counts differ by one and the check
     * fails for a reason that has nothing to do with the listing.
     */
    {
        size_t gl = strlen(got);
        while (gl > 0u && got[gl - 1u] == '|') { gl--; }
        snprintf(g, sizeof(g), "|%.*s|", (int)gl, got);
        snprintf(w, sizeof(w), "|%s|", want);
    }

    for (p = g; *p; p++) { if (*p == '|') { gt++; } }
    for (p = w; *p; p++) { if (*p == '|') { wt++; } }
    if (gt != wt) {
        return 0;                             /* different number of entries */
    }
    /* Every expected token must appear in the result. */
    for (p = w; *p; ) {
        const char *e = p + 1;
        char pat[160];
        while (*e && *e != '|') { e++; }
        if (e > p + 1) {
            size_t len = (size_t)(e - p - 1);
            if (len + 3u > sizeof(pat)) { return 0; }
            pat[0] = '|';
            memcpy(pat + 1, p + 1, len);
            pat[len + 1] = '|';
            pat[len + 2] = '\0';
            if (!strstr(g, pat)) {
                return 0;
            }
        }
        /* Leave p ON the separator, not past it: the loop takes p+1 as the
         * token's first character, so advancing past the '|' would eat the
         * next token's first character and compare ".txt" instead of "a.txt". */
        p = e;
    }
    return 1;
}

static void mkfile(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (f) { fputs(text, f); fclose(f); }
}

int main(int argc, char **argv)
{
    nj_port port;
    nj_run_opts opts;
    capture cap;
    const char *jar;
    const char *root = "tests/out/fsroot";
    char v[512];
    int rc, round;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <fsdemo.jar>\n", argv[0]);
        return 2;
    }
    jar = argv[1];
    printf("== nano_j2me JSR 75 (javax.microedition.io.file) ==\n");

    /* A tree whose contents the test chose, so the expectations are not guesses. */
    mkdir("tests/out", 0777);
    mkdir(root, 0777);
    { char p[256]; snprintf(p, sizeof(p), "%s/sub", root); mkdir(p, 0777); }
    { char p[256]; snprintf(p, sizeof(p), "%s/a.txt", root); mkfile(p, "hello from a file\n"); }
    { char p[256]; snprintf(p, sizeof(p), "%s/game.nes", root); mkfile(p, "nes rom\n"); }
    { char p[256]; snprintf(p, sizeof(p), "%s/sub/inner.txt", root); mkfile(p, "inner\n"); }

    for (round = 1; round <= 2; round++) {
        memset(&cap, 0, sizeof(cap));
        if (nj_port_headless_ex(&port, 176, 208, 0, NULL) != NJ_OK) { return 2; }
        port.log = cap_log;
        port.user = &cap;
        port.fps_cap = 0;
        port.fs_dir = root;

        memset(&opts, 0, sizeof(opts));
        opts.jar_path = jar;
        opts.heap_bytes = 4u * 1024u * 1024u;
        opts.run_frames = 40u;
        rc = nj_run(&port, &opts);
        nj_port_shutdown(&port);

        printf("--- MIDlet report (round %d) ---\n%s", round, cap.buf);
        ok(!contains(&cap, "FS failed="), "the MIDlet ran without failing");

        /* ---- reading ---- */
        ok(field(&cap, "opened", v, sizeof(v)) &&
           strstr(v, "file:///root/a.txt exists=true dir=false name=a.txt path=/a.txt"),
           "a file opens with the right name and path");
        ok(field(&cap, "content", v, sizeof(v)) &&
           strcmp(v, "hello from a file") == 0,
           "openInputStream returns the file's bytes");

        /* getPath() used to return "///root/a.txt" and the root's name "root";
         * both are asserted above and in the rootname check below. */
        ok(field(&cap, "rootname", v, sizeof(v)) && strcmp(v, "[]") == 0,
           "the root has no name");

        /* ---- listing ---- */
        ok(field(&cap, "entries", v, sizeof(v)) &&
           same_set(v, "game.nes|a.txt|sub/"),
           "list() returns files and the subdirectory WITH a trailing slash");
        ok(field(&cap, "dirs", v, sizeof(v)) && strcmp(v, "1") == 0,
           "exactly one entry is a directory");
        ok(field(&cap, "files", v, sizeof(v)) && strcmp(v, "2") == 0,
           "exactly two entries are files");
        ok(field(&cap, "nes", v, sizeof(v)) &&
           strcmp(v, "game.nes|") == 0,
           "the \"*.nes\" filter matches one file and not the others");

        /* ---- the sandbox ---- */
        {
            const char *p = cap.buf;
            int refusals = 0;
            while ((p = strstr(p, "FS refused=file:///root/")) != NULL) {
                if (strstr(p, "ConnectionNotFoundException")) { refusals++; }
                p += 10;
            }
            /* "../etc/passwd", "sub/../../etc/passwd" and "a/../../etc/passwd" */
            ok(refusals >= 3,
               "every path that escapes the root is refused, by name");
        }
        ok(field(&cap, "scheme", v, sizeof(v)) &&
           strstr(v, "ConnectionNotFoundException") != NULL,
           "a non-file scheme is refused rather than faked");

        /* ---- a closed connection stays closed ---- */
        ok(field(&cap, "closed", v, sizeof(v)) &&
           strstr(v, "ConnectionClosedException") != NULL,
           "using a closed FileConnection raises ConnectionClosedException");

        if (fails) { break; }
    }

    printf("\n%d checks, %d failures\n%s\n", checks, fails,
           fails == 0 ? "PASS" : "FAIL");
    return fails == 0 ? 0 : 1;
}
