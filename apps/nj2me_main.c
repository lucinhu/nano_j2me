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

#define NJ_CLI_HEAP_MB ((NJ_MAX_HEAP / (1024u * 1024u)) < 64u \
                        ? 64u : (NJ_MAX_HEAP / (1024u * 1024u)))

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
"  --font <file>        draw text with a TrueType face (default: built-in)\n"
"  --heap <mb>          Java heap size in megabytes (default: %u)\n"
"  --scale <n>          integer upscale for the window (sdl2, default: auto)\n"
"  --size <WxH>         screen size, e.g. 240x320 (default: %dx%d)\n"
"  --rms-dir <path>     where RecordStore files are kept\n"
"  --fps <n>            frame rate cap (default: 30; 0 = uncapped)\n"
"  --run-frames <n>     stop after n presented frames (for benchmarks)\n"
"  --save-state <file>  write a VM snapshot at a frame boundary and keep going\n"
"  --save-state-frame <n>  which frame boundary to snapshot (default: 1).\n"
"                        A title that never returns to a frame boundary\n"
"                        (Doom 3 joins its game thread in the MIDlet\n"
"                        constructor) is never snapshotted; the run says so\n"
"                        at the end.\n"
"  --load-state <file>  resume a snapshot instead of starting the MIDlet\n"
"  --keys <script>      replay key and pointer events at given frames\n"
        "  --fs-dir <path>      root for file:// (JSR 75); no filesystem by default\n"
"  --network            enable HTTP/HTTPS/TCP for the headless port\n"
"  --stats              print bytecode and frame counters at the end\n"
"  --device <name>      select handset properties and key convention\n"
"  --prop <key>=<value> answer one more property (repeatable)\n"
"  --list-devices       print the built-in handsets and exit\n"
"\n"
"the device a MIDlet thinks it is on\n"
"  Many titles ask microedition.platform which handset they are running on and\n"
"  take a code path that was never finished when the answer is not one they\n"
"  know - 1945空战英雄_6105 stops outright unless the platform starts with\n"
"  \"Nokia\".  --device names a built-in profile with a platform string, an IMEI,\n"
"  an IMSI and the vendor's own spellings of them; --list-devices prints them.\n"
"  --prop answers any key on its own, on top of a profile or instead of one:\n"
"      --prop com.nokia.keyboard.type=None\n"
"  Both are for compatibility work, not for hiding a bug in the runtime: run\n"
"  without them first, and treat a title that only starts with a profile as a\n"
"  title whose own code chose that path.\n"
"\n"
"fonts\n"
"  Text uses the built-in bitmap faces unless --font names a TrueType file.\n"
"  The face is fitted to the built-in line heights rather than the other way\n"
"  round, so MIDlet layout is unchanged, and any character the supplied font\n"
"  has no glyph for still comes from the built-in face.  A build made with\n"
"  -DNJ_ENABLE_TTF=0 has no rasteriser at all and ignores this option.\n"
"\n"
"on-screen keys (sdl2)\n"
"  The window draws every J2ME key: soft-left, soft-right, the D-pad, fire,\n"
"  the twelve-key phone pad, clear, A/B/C/D and volume/power.  Click or tap\n"
"  them, or press them on the keyboard.\n"
"    F12 or Tab         show / hide the on-screen keys\n"
"    NJ_SDL2_KEYS=0     start with them hidden\n"
"  --where              say where each thread stopped, frame by frame\n"
"  --dump-frames <prefix>  write a PPM per frame as <prefix>NNNNN.ppm\n"
"                          (headless; give a directory by ending it in /)\n"
"  --dump-every <n>     write every n-th frame (headless, default: 1)\n"
"  --version            print the version and exit\n"
"  --help               this text\n"
"\n"
"replay scripts (--keys)\n"
"  A script is a list of frame-stamped events; whitespace and newlines\n"
"  separate entries and '#' starts a comment.  A key name is the same\n"
"  vocabulary the keyboard uses: up down left right fire soft1 soft2 soft3,\n"
"  the twelve-key pad (0-9, star, pound), a b c d, clear.\n"
"  A bare \"frame:key\" is a tap: down at that frame, up the next one.  Add\n"
"  :down or :up to name one edge instead, which is how a script holds a\n"
"  direction for a title that reads getKeyStates() and moves while it is\n"
"  held.  Pointer entries are pressed@X,Y, dragged@X,Y and released@X,Y in\n"
"  device pixels, and their edges are explicit too:\n"
"      480:down:down,486:down:up,489:fire,520:up:down,700:up:up\n"
"      20:pressed@30,40 24:released@30,40\n"
"\n"
"examples:\n"
"  %s game.jar\n"
"  %s --port headless --dump-frames out/ game.jar\n"
"  %s --port sdl2 --scale 3 --heap 8 game.jar\n",
        argv0,
        (unsigned)NJ_CLI_HEAP_MB,
        NJ_SCREEN_WIDTH, NJ_SCREEN_HEIGHT,
        argv0, argv0, argv0);
}

typedef struct cli {
    const char *port_name;
    const char *jar;
    const char *midlet;
    const char *rms_dir;
    const char *fs_dir;
    const char *dump_dir;
    int         dump_every;
    int         scale;
    int         fps;
    int         width;
    int         height;
    unsigned    heap_mb;
    int         stats;
    int         network;
    int         where;
    int         run_frames;
    const char *keys;
    const char *font;
    const char *device_name;
    const char *save_state;
    const char *load_state;
    int         save_frame;
} cli;

/*
 * Room for a device profile plus the caller's own --prop entries.  The largest
 * built-in profile has eleven rows, so a profile and a handful of overrides fit
 * with room to spare; going over is a usage error rather than a silent drop.
 */
#define NJ_CLI_MAX_PROPS 32
#define NJ_CLI_PROP_LEN  96

static void list_devices(void)
{
    nj_u4 n = nj_device_count();
    nj_u4 i;

    printf("built-in devices:\n");
    for (i = 0; i < n; i++) {
        const nj_device *d = nj_device_at(i);
        printf("  %-20s %s\n", d->name, d->summary);
    }
    printf("\n  The default is \"%s\".  See nj2me/nj_device.h for what each\n"
           "  profile answers and why the identity numbers are synthetic.\n",
           nj_device_default()->name);
}

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
    /* The merged property table nj_run() installs: the device, then overrides. */
    static nj_device_prop props[NJ_CLI_MAX_PROPS];
    static char prop_key[NJ_CLI_MAX_PROPS][NJ_CLI_PROP_LEN];
    static char prop_val[NJ_CLI_MAX_PROPS][NJ_CLI_PROP_LEN];
    int nprops = 0;
    nj_size klen;

    memset(&c, 0, sizeof(c));
    c.dump_every = 1;
    c.fps = 30;
    c.width = NJ_SCREEN_WIDTH;
    c.height = NJ_SCREEN_HEIGHT;
    c.heap_mb = (unsigned)NJ_CLI_HEAP_MB;
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
        } else if (strcmp(a, "--font") == 0 && i + 1 < argc) {
            c.font = argv[++i];
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
        } else if (strcmp(a, "--fs-dir") == 0 && i + 1 < argc) {
            c.fs_dir = argv[++i];
        } else if (strcmp(a, "--keys") == 0 && i + 1 < argc) {
            c.keys = argv[++i];
        } else if (strcmp(a, "--list-devices") == 0) {
            list_devices();
            return 0;
        } else if (strcmp(a, "--device") == 0 && i + 1 < argc) {
            c.device_name = argv[++i];
        } else if (strcmp(a, "--prop") == 0 && i + 1 < argc) {
            const char *kv = argv[++i];
            const char *eq = strchr(kv, '=');
            nj_size k;
            if (eq == NULL || eq == kv) {
                fprintf(stderr, "--prop wants key=value, got: %s\n", kv);
                return 2;
            }
            if (nprops >= NJ_CLI_MAX_PROPS) {
                fprintf(stderr, "too many --prop options (the limit is %d)\n",
                        NJ_CLI_MAX_PROPS);
                return 2;
            }
            k = (nj_size)(eq - kv);
            if (k >= NJ_CLI_PROP_LEN ||
                strlen(eq + 1) >= (size_t)NJ_CLI_PROP_LEN) {
                fprintf(stderr, "--prop key and value must each be shorter "
                                "than %d characters\n", NJ_CLI_PROP_LEN);
                return 2;
            }
            memcpy(prop_key[nprops], kv, (size_t)k);
            prop_key[nprops][k] = '\0';
            memcpy(prop_val[nprops], eq + 1, strlen(eq + 1) + 1u);
            props[nprops].key = prop_key[nprops];
            props[nprops].value = prop_val[nprops];
            nprops++;
        } else if (strcmp(a, "--network") == 0) {
            c.network = 1;
        } else if (strcmp(a, "--stats") == 0) {
            c.stats = 1;
        } else if (strcmp(a, "--where") == 0) {
            c.where = 1;
        } else if (strcmp(a, "--run-frames") == 0 && i + 1 < argc) {
            c.run_frames = atoi(argv[++i]);
        } else if (strcmp(a, "--save-state") == 0 && i + 1 < argc) {
            c.save_state = argv[++i];
            c.save_frame = 1;
        } else if (strcmp(a, "--save-state-frame") == 0 && i + 1 < argc) {
            c.save_frame = atoi(argv[++i]);
            if (c.save_frame < 1) {
                c.save_frame = 1;
            }
        } else if (strcmp(a, "--load-state") == 0 && i + 1 < argc) {
            c.load_state = argv[++i];
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

    /*
     * Build the property table: the device profile first, the caller's own
     * --prop entries after it.  A later row wins, so an override lands on top
     * of the profile it names rather than being shadowed by it.
     */
    if (c.device_name != NULL) {
        const nj_device *d = nj_device_find(c.device_name);
        nj_u4 j;

        if (d == NULL) {
            fprintf(stderr, "unknown device: %s\n", c.device_name);
            list_devices();
            return 2;
        }
        if ((int)d->count + nprops > NJ_CLI_MAX_PROPS) {
            fprintf(stderr, "too many properties for one run (the limit is "
                            "%d)\n", NJ_CLI_MAX_PROPS);
            return 2;
        }
        for (j = (nj_u4)nprops; j > 0u; j--) {
            props[j - 1u + d->count] = props[j - 1u];
        }
        for (j = 0; j < d->count; j++) {
            props[j] = d->props[j];
        }
        nprops += (int)d->count;
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

    if (c.network && strcmp(c.port_name, "headless") == 0) {
        st = nj_port_headless_enable_network(&port);
        if (st != NJ_OK) {
            fprintf(stderr, "this build has no desktop network backend\n");
            nj_port_shutdown(&port);
            return 2;
        }
    }

    port.width = c.width;
    port.height = c.height;
    port.fps_cap = c.fps;
    port.rms_dir = c.rms_dir;
    /*
     * No filesystem unless asked for.  JSR 75 exposes the host's directories to
     * untrusted code, and a default that hands a MIDlet the working directory
     * is a default nobody thought about.  --fs-dir names the root a MIDlet is
     * confined to.
     */
    port.fs_dir = c.fs_dir;
    if (use_sdl2 && c.scale > 0) {
        port.scale = c.scale;
    }

    opts.jar_path = c.jar;
    opts.midlet_class = c.midlet;
    opts.heap_bytes = (nj_size)c.heap_mb * 1024u * 1024u;
    opts.argv = (const char *const *)argv;
    opts.argc = argc;
    opts.stats = c.stats;
    opts.where = c.where;
    opts.run_frames = (nj_u4)(c.run_frames > 0 ? c.run_frames : 0);
    opts.keys = c.keys;
    opts.font_path = c.font;
    opts.props = nprops > 0 ? props : NULL;
    opts.nprops = nprops;
    opts.device = c.device_name ? nj_device_find(c.device_name) : NULL;
    opts.load_state_path = c.load_state;
    opts.save_state_path = c.save_state;
    opts.save_state_frame = (nj_u4)(c.save_frame > 0 ? c.save_frame : 0);

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
