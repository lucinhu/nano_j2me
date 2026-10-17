/*
 * RecordStore and FileConnection are invisible from the outside.
 *
 * A MIDlet that saves a high score and one that silently fails to are the same
 * sequence of frames, so the corpus runner - which can only look at pixels -
 * cannot tell them apart.  The headless port is what makes the difference
 * observable: it records what it was asked to open, read and write.
 *
 * This test asserts on BOTH sides of that.  The counters are only worth
 * anything if they are tied to reality, so the number of bytes the port says it
 * wrote is compared against the size of the file that is actually on disk.
 * A counter that is merely non-zero would pass even if it counted the wrong
 * thing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "nj2me/nj.h"

static int failures = 0;
static int checks = 0;

static void ok(int cond, const char *what)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL  %s\n", what);
    } else {
        printf("  ok    %s\n", what);
    }
}

#define RMS_DIR "/tmp/nj_headless_fs_test"

/* Does `path` end in `suffix`? */
static int ends_with(const char *path, const char *suffix)
{
    size_t a, b;

    if (!path || !suffix) {
        return 0;
    }
    a = strlen(path);
    b = strlen(suffix);
    return a >= b && strcmp(path + a - b, suffix) == 0;
}

static long file_size(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) ? (long)st.st_size : -1;
}

int main(int argc, char **argv)
{
    nj_port port;
    nj_run_opts opts;
    char rms_file[256];
    long written;
    long on_disk;
    int opens, closes, reads, writes, rc;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <gamedemo.jar>\n", argv[0]);
        return 2;
    }

    printf("== nano_j2me headless file observation ==\n");

    /*
     * Start from an empty directory.  A record store left behind by an earlier
     * run would make "did it write?" unanswerable, which is exactly the sort of
     * stale-state control failure this project has been caught by before.
     */
    mkdir(RMS_DIR, 0700);
    snprintf(rms_file, sizeof(rms_file), "%s/demo.rms", RMS_DIR);
    remove(rms_file);
    ok(file_size(rms_file) < 0, "the record store does not exist before the run");

    if (nj_port_headless_ex(&port, 176, 208, 0, NULL) != NJ_OK) {
        fprintf(stderr, "cannot create the headless port\n");
        return 2;
    }
    port.fps_cap = 0;
    port.rms_dir = RMS_DIR;

    /* Reusing the port for another VM must not replace or leak its display. */
    {
        int w1 = 0, h1 = 0, stride1 = 0, w2 = 0, h2 = 0, stride2 = 0;
        void *pixels1 = NULL, *pixels2 = NULL;
        int fmt1 = port.display_init(port.ud, &w1, &h1, &stride1, &pixels1);
        int fmt2;
        ok(fmt1 >= 0 && pixels1 != NULL, "display initializes successfully");
        if (pixels1) { ((unsigned char *)pixels1)[0] = 0x5a; }
        fmt2 = port.display_init(port.ud, &w2, &h2, &stride2, &pixels2);
        ok(fmt2 == fmt1 && pixels2 == pixels1 && pixels2 != NULL,
           "repeated display initialization keeps the owned buffer");
        ok(w1 == 176 && h1 == 208 && w2 == w1 && h2 == h1 && stride2 == stride1,
           "repeated display initialization keeps its geometry");
        ok(pixels2 && ((unsigned char *)pixels2)[0] == 0x5a,
           "repeated display initialization preserves live pixels");
        if (pixels2) { ((unsigned char *)pixels2)[0] = 0; }
    }

    memset(&opts, 0, sizeof(opts));
    opts.jar_path = argv[1];
    opts.heap_bytes = 4u * 1024u * 1024u;

    rc = nj_run(&port, &opts);
    ok(rc == NJ_OK, "the run itself succeeded");

    opens = nj_port_headless_file_opens(&port);
    closes = nj_port_headless_file_closes(&port);
    reads = nj_port_headless_file_reads(&port);
    writes = nj_port_headless_file_writes(&port);
    written = nj_port_headless_bytes_written(&port);

    printf("--- port saw: opens=%d closes=%d reads=%d writes=%d bytes=%ld\n",
           opens, closes, reads, writes, written);

    ok(opens > 0, "the port was asked to open something at all");
    ok(writes > 0, "the MIDlet wrote records through the port");
    ok(written > 0, "and those writes carried a non-zero number of bytes");
    ok(reads > 0, "records were read back through the port as well");

    ok(ends_with(nj_port_headless_last_path(&port), ".rms"),
       "the last path opened is a record store, not some unrelated file");

    /*
     * The number the port reports is checked against the file on disk.  Without
     * this the counter could be counting calls rather than bytes and every
     * assertion above would still pass.
     *
     * It is >= and not ==, and the first version of this test got that wrong:
     * a record store is rewritten as a whole every time a record changes (the
     * writer opens it "wb"), so the port's running total counts every rewrite
     * while the file only holds the last one.  73 writes of 151 bytes leaving a
     * 17-byte file is correct, not a discrepancy.  The bound still has teeth -
     * a counter stuck at 0, or one counting calls instead of bytes, fails.
     */
    on_disk = file_size(rms_file);
    printf("--- on disk: %s = %ld bytes (port wrote %ld in total)\n",
           rms_file, on_disk, written);
    ok(on_disk > 0, "the record store really is on disk");
    ok(written >= on_disk,
       "the bytes the port reported writing cover the file that exists");

    /*
     * The project's promise is that a run leaves nothing behind.  A port that
     * opened files and never closed them would pass every check above and still
     * leak a descriptor per record store.
     */
    ok(opens == closes, "every file the port opened was closed again");

    /*
     * -1 is "this is not a headless port", which has to be distinguishable from
     * 0, "it is one and the MIDlet did no file IO" - a MIDlet that saves
     * nothing must not read like a port that cannot see.
     */
    {
        nj_port bare;
        memset(&bare, 0, sizeof(bare));
        ok(nj_port_headless_file_opens(&bare) == -1,
           "a port that is not headless reports -1, not 0");
    }

    /*
     * --rms-dir used to be taken on trust.  A MIDlet cannot create a
     * directory, so a save directory that is not there yet is a store that
     * cannot be opened - and 三国争霸之赤壁大战_11681 does not merely fail to
     * save, it stops drawing: 150 painted frames, one colour, black.
     *
     * The computed default always made its own directory.  A directory the
     * caller named has to be made too, or the option is a trap.
     */
    {
        char fresh[256];
        struct stat st;
        nj_port p2;
        nj_run_opts o2;

        /*
         * A path of its own, so the precondition is a fact rather than a
         * hope: an earlier run of this test leaves a record store behind, and
         * a directory with a file in it cannot be removed again.
         */
        snprintf(fresh, sizeof(fresh), "%s/fresh-%ld/deep", RMS_DIR,
                 (long)getpid());
        ok(stat(fresh, &st) != 0, "the save directory does not exist yet");

        if (nj_port_headless_ex(&p2, 176, 208, 0, NULL) != NJ_OK) {
            fprintf(stderr, "cannot create the second headless port\n");
            return 2;
        }
        p2.fps_cap = 0;
        p2.rms_dir = fresh;

        memset(&o2, 0, sizeof(o2));
        o2.jar_path = argv[1];
        o2.heap_bytes = 4u * 1024u * 1024u;

        rc = nj_run(&p2, &o2);
        ok(rc == NJ_OK, "a run pointed at a missing save directory succeeds");
        ok(stat(fresh, &st) == 0 && S_ISDIR(st.st_mode),
           "and the run created the directory it was pointed at");
        nj_port_shutdown(&p2);
    }

    nj_port_shutdown(&port);

    printf("\n%d checks, %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}
