/*
 * nano_j2me - apps/nj2me_main.c
 *
 * The command-line front end.  A platform's real main() is usually three lines
 * around nj_run(); everything here is argument handling and port selection.
 */
#include "nj2me/nj.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void usage(const char *argv0)
{
    printf(
"nano_j2me " NJ_VERSION_STRING " - a J2ME (CLDC 1.1 / MIDP 2.0) runtime in C\n"
"\n"
"usage: %s [options] <game.jar>\n"
"\n"
"options:\n"
"  --port <name>        headless | sdl2        (default: sdl2 if built in)\n"
"  --midlet <class>     run a specific MIDlet class instead of MIDlet-1\n"
"  --heap <mb>          Java heap size in megabytes (default: %u)\n"
"  --scale <n>          integer upscale for the window (sdl2, default: auto)\n"
"  --size <WxH>         screen size, e.g. 240x320 (default: %dx%d)\n"
"  --rms-dir <path>     where RecordStore files are kept\n"
"  --fps <n>            frame rate cap (default: 30; 0 = uncapped)\n"
"  --run-frames <n>     stop after n presented frames (for benchmarks)\n"
"  --stats              print bytecode and frame counters at the end\n"
"  --dump-frames <dir>  write a PPM per frame (headless)\n"
"  --dump-every <n>     write every n-th frame (headless, default: 1)\n"
"  --stats              print VM statistics on exit\n"
"  --version            print the version and exit\n"
"  --help               this text\n"
"\n"
"examples:\n"
"  %s game.jar\n"
"  %s --port headless --dump-frames out/ game.jar\n"
"  %s --port sdl2 --scale 3 --heap 8 game.jar\n",
        argv0,
        (unsigned)(NJ_MAX_HEAP / (1024u * 1024u)),
        NJ_SCREEN_WIDTH, NJ_SCREEN_HEIGHT,
        argv0, argv0, argv0);
}

typedef struct cli {
    const char *port_name;
    const char *jar;
    const char *midlet;
    const char *rms_dir;
    const char *dump_dir;
    int         dump_every;
    int         scale;
    int         fps;
    int         width;
    int         height;
    unsigned    heap_mb;
    int         stats;
    int         run_frames;
} cli;

static int parse_size(const char *s, int *w, int *h)
{
    int a = 0, b = 0;
    if (!s || sscanf(s, "%dx%d", &a, &b) != 2 || a <= 0 || b <= 0) {
        return -1;
    }
    *w = a;
    *h = b;
    return 0;
}

int main(int argc, char **argv)
{
    cli c;
    nj_port port;
    nj_run_opts opts;
    nj_status st;
    int i;
    int use_sdl2 = 0;

    memset(&c, 0, sizeof(c));
    c.dump_every = 1;
    c.fps = 30;
    c.width = NJ_SCREEN_WIDTH;
    c.height = NJ_SCREEN_HEIGHT;
    c.heap_mb = (unsigned)(NJ_MAX_HEAP / (1024u * 1024u));
#if NJ_HAVE_SDL2
    c.port_name = "sdl2";
#else
    c.port_name = "headless";
#endif

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(a, "--version") == 0) {
            printf("nano_j2me %s\n", NJ_VERSION_STRING);
            return 0;
        } else if (strcmp(a, "--port") == 0 && i + 1 < argc) {
            c.port_name = argv[++i];
        } else if (strcmp(a, "--midlet") == 0 && i + 1 < argc) {
            c.midlet = argv[++i];
        } else if (strcmp(a, "--heap") == 0 && i + 1 < argc) {
            c.heap_mb = (unsigned)atoi(argv[++i]);
        } else if (strcmp(a, "--scale") == 0 && i + 1 < argc) {
            c.scale = atoi(argv[++i]);
        } else if (strcmp(a, "--fps") == 0 && i + 1 < argc) {
            c.fps = atoi(argv[++i]);
        } else if (strcmp(a, "--size") == 0 && i + 1 < argc) {
            if (parse_size(argv[++i], &c.width, &c.height) != 0) {
                fprintf(stderr, "bad --size (expected WxH)\n");
                return 2;
            }
        } else if (strcmp(a, "--rms-dir") == 0 && i + 1 < argc) {
            c.rms_dir = argv[++i];
        } else if (strcmp(a, "--dump-frames") == 0 && i + 1 < argc) {
            c.dump_dir = argv[++i];
        } else if (strcmp(a, "--dump-every") == 0 && i + 1 < argc) {
            c.dump_every = atoi(argv[++i]);
            if (c.dump_every < 1) {
                c.dump_every = 1;
            }
        } else if (strcmp(a, "--stats") == 0) {
            c.stats = 1;
        } else if (strcmp(a, "--run-frames") == 0 && i + 1 < argc) {
            c.run_frames = atoi(argv[++i]);
        } else if (a[0] == '-') {
            fprintf(stderr, "unknown option: %s\n", a);
            usage(argv[0]);
            return 2;
        } else {
            c.jar = a;
        }
    }

    if (!c.jar) {
        usage(argv[0]);
        return 2;
    }

    memset(&port, 0, sizeof(port));
    memset(&opts, 0, sizeof(opts));

    if (strcmp(c.port_name, "headless") == 0) {
        st = nj_port_headless_ex(&port, c.width, c.height,
                                 c.dump_dir ? c.dump_every : 0, c.dump_dir);
        if (st != NJ_OK) {
            fprintf(stderr, "cannot create the headless port\n");
            return 1;
        }
    } else if (strcmp(c.port_name, "sdl2") == 0) {
#if NJ_HAVE_SDL2
        use_sdl2 = 1;
        st = nj_port_sdl2(&port, "nano_j2me");
        if (st != NJ_OK) {
            fprintf(stderr, "cannot create the SDL2 port (is DISPLAY set?)\n");
            return 1;
        }
#else
        fprintf(stderr, "this build has no SDL2 port; use --port headless\n");
        return 2;
#endif
    } else {
        fprintf(stderr, "unknown port: %s\n", c.port_name);
        return 2;
    }

    port.width = c.width;
    port.height = c.height;
    port.fps_cap = c.fps;
    port.rms_dir = c.rms_dir;
    if (use_sdl2 && c.scale > 0) {
        port.scale = c.scale;
    }

    opts.jar_path = c.jar;
    opts.midlet_class = c.midlet;
    opts.heap_bytes = (nj_size)c.heap_mb * 1024u * 1024u;
    opts.argv = (const char *const *)argv;
    opts.argc = argc;
    opts.stats = c.stats;
    opts.run_frames = (nj_u4)(c.run_frames > 0 ? c.run_frames : 0);

    st = (nj_status)nj_run(&port, &opts);

    /* nj_run() does not own the port, so shutting it down is the caller's job. */
    nj_port_shutdown(&port);

    if (c.stats) {
        /* nj_run() printed the counters before tearing the VM down; say what
         * the host is being told so a script can check it without parsing. */
        fprintf(stderr, "nano_j2me: exit status %d\n", (int)st);
    }
    return st == NJ_OK ? 0 : 1;
}
