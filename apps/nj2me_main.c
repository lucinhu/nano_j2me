/* NJ_PROF_FEATURE: the dev sampler needs POSIX/GNU signal types even in
 * strict -std=c99 builds; define this before any system header. */
#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
#define _GNU_SOURCE 1
#endif
/*
 * nano_j2me - apps/nj2me_main.c
 *
 * The command-line front end.  A platform's real main() is usually three lines
 * around nj_run(); everything here is argument handling and port selection.
 */
#include "nj2me/nj.h"
#if NJ_ENABLE_VULKAN
#include "nj2me/nj_gfx_vulkan.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#if defined(_WIN32)
#include <direct.h>
#define NJ_CLI_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define NJ_CLI_MKDIR(path) mkdir((path), 0777)
#endif

/*
 * Dev-only self-sampling profiler: NJ_SAMPLE_PROF=1 arms a 1 kHz SIGPROF
 * timer that records the interrupted program counter; at exit the addresses
 * are printed as "PROF <hex>" lines on stderr so an offline symbolizer can
 * map them. It never touches the VM, so a profiled run measures the same
 * code as a normal run.
 */
#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
#define NJ_PROF 1
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>
#define NJ_PROF_MAX 131072u
static volatile sig_atomic_t nj_prof_on;
static unsigned nj_prof_n;
static unsigned long nj_prof_pc[NJ_PROF_MAX];
static unsigned long nj_prof_base;

static void nj_prof_handler(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = (ucontext_t *)ucv;
    unsigned long pc;
    (void)sig;
    (void)si;
#if defined(__x86_64__)
    pc = (unsigned long)uc->uc_mcontext.gregs[REG_RIP];
#else
    pc = (unsigned long)uc->uc_mcontext.pc;
#endif
    if (nj_prof_n < NJ_PROF_MAX) {
        nj_prof_pc[nj_prof_n++] = pc;
    }
}

static void nj_prof_start(void)
{
    struct sigaction sa;
    struct itimerval it;
    FILE *f = fopen("/proc/self/maps", "r");
    char line[1024];
    char self[512];
    ssize_t slen;
    if (!f) {
        return;
    }
    slen = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (slen <= 0) {
        fclose(f);
        return;
    }
    self[slen] = 0;
    /* The whole executable mapping: min start .. max end for this path. */
    while (fgets(line, (int)sizeof(line), f)) {
        unsigned long lo = 0, hi = 0;
        char perms[8], path[600];
        int got;
        path[0] = 0;
        got = sscanf(line, "%lx-%lx %7s %*s %*s %*s %599s", &lo, &hi, perms,
                     path);
        if (got >= 4 && strcmp(path, self) == 0) {
            if (!nj_prof_base || lo < nj_prof_base) {
                nj_prof_base = lo;
            }
        }
    }
    fclose(f);
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = nj_prof_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPROF, &sa, NULL) != 0) {
        return;
    }
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 1000;
    it.it_value = it.it_interval;
    if (setitimer(ITIMER_PROF, &it, NULL) != 0) {
        return;
    }
    nj_prof_on = 1;
}

static void nj_prof_dump(void)
{
    unsigned i;
    struct itimerval zero;
    if (!nj_prof_on) {
        return;
    }
    memset(&zero, 0, sizeof(zero));
    setitimer(ITIMER_PROF, &zero, NULL);
    {
        FILE *f = fopen("/proc/self/maps", "r");
        char line[1024];
        if (f) {
            while (fgets(line, (int)sizeof(line), f)) {
                unsigned long lo = 0, hi = 0, off = 0;
                char perms[8], path[600];
                int got;
                path[0] = 0;
                got = sscanf(line, "%lx-%lx %7s %lx %*s %*s %599s", &lo, &hi,
                             perms, &off, path);
                if (got >= 4 && perms[2] == 'x') {
                    fprintf(stderr, "MAP %lx %lx %lx %s\n", lo, hi, off,
                            got >= 5 ? path : "");
                }
            }
            fclose(f);
        }
    }
    for (i = 0; i < nj_prof_n; ++i) {
        fprintf(stderr, "PROF %lx\n", nj_prof_pc[i]);
    }
    fprintf(stderr, "PROF samples=%u\n", nj_prof_n);
}
#else
/* Function-like macros cannot be passed to atexit(); use real no-ops so the
   CLI also builds on architectures the profiler does not support (armv7 etc). */
static void nj_prof_start(void) {}
static void nj_prof_dump(void) {}
#endif

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>     /* CommandLineToArgvW */
#endif

#define NJ_CLI_HEAP_MB (NJ_MAX_HEAP / (1024u * 1024u))

static void usage(const char *argv0)
{
    printf(
"nano_j2me " NJ_VERSION_STRING " - a J2ME (CLDC 1.1 / MIDP 2.0) runtime in C\n"
"\n"
"usage: %s [options] <game.jar>\n"
"\n"
"options:\n"
"  --port <name>        headless | sdl2        (default: sdl2 if built in)\n"
#if NJ_HAVE_SDL2
"  --graphics <mode>    software | gles2 | vulkan (M3G drawing; default: software)\n"
"                       gles2 is experimental and requires the optional backend;\n"
"                       unavailable builds reject it. Unsupported drawing falls\n"
"                       back to software; acceleration is not guaranteed.\n"
"                       Applies to direct games and every game from --client.\n"
"                       Without this flag, legacy NJ_SDL2_GFX=gles2 is honored;\n"
"                       explicit software overrides it. SDL presentation is separate.\n"
#else
"  --graphics <mode>    software | vulkan (optional headless M3G drawing)\n"
#endif
"  --midlet <class>     run a specific MIDlet class instead of MIDlet-1\n"
"  --font <file>        draw text with a TrueType face (default: built-in)\n"
"  --heap <mb>          initial Java heap MB (saved setting; runtime default: %u)\n"
"  --heap-grow          expand after GC when needed, up to 128 MB or --heap\n"
"  --no-heap-grow       keep the Java heap fixed (overrides the saved switch)\n"
"  --render-scale <1..%d>  rasterize geometry and outlines at this output scale\n"
"  --ssaa <1|2>           samples per axis before resolving the physical frame\n"
"  --subdivide            split M3G triangles during HD drawing\n"
"  --asset-filter <mode>  HD artwork: nearest (default), smooth, pixel-art\n"
"                        or fsr1 (cubic plus clamped sharpening)\n"
"  --smooth-assets        alias for --asset-filter smooth\n"
"  --fsr2                 request temporal M3G rendering (Vulkan build)\n"
"  --scale <n>          integer upscale for the window (sdl2, default: auto)\n"
"  --size <WxH>         screen size, e.g. 240x320 (default: %dx%d)\n"
"  --rms-dir <path>     where RecordStore files are kept\n"
"  --fps <n>            frame rate cap (default: the port's own; 0 = uncapped)\n"
"  --run-frames <n>     stop after n presented frames (for benchmarks)\n"
"  --deterministic      replay with a virtual clock and seeded Random: the\n"
"                       same script stops at the same game state, so dumps\n"
"                       can be compared byte for byte (headless port)\n"
"  --save-state <file>  write a VM snapshot at a frame boundary and keep going\n"
"  --save-state-frame <n>  presented frame to snapshot at, or the first\n"
"                        boundary after it (default: 1).\n"
"                        With --save-state the MIDlet is started as a VM\n"
"                        thread, so titles that run their frame loop inside\n"
"                        startApp() can be snapshotted.  A title that never\n"
"                        returns to a frame boundary at all (Doom 3 joins its\n"
"                        game thread in the MIDlet constructor) is never\n"
"                        snapshotted; the run says so at the end.\n"
"  --load-state <file>  resume a snapshot instead of starting the MIDlet\n"
"  --state-dir <dir>    slot directory (default: <jar dir>/states)\n"
"  --save-slot <n>      write slot n of this game (0-999)\n"
"  --load-slot <n>      resume slot n of this game\n"
"  --keys <script>      replay key and pointer events at given frames\n"
        "  --cheat <code>       arena-offset cheat code re-applied every frame\n"
        "                       (format \"<offset>:<hex bytes>\", repeatable)\n"
        "  --cheat-file <file>  read cheat codes, one per line (\"#\" comments)\n"
        "  --fs-dir <path>      root for file:// (JSR 75); no filesystem by default\n"
"  --network            enable HTTP/HTTPS/TCP for the headless port\n"
"  --stats              print bytecode and frame counters at the end\n"
"  --stats-every <n>    also print the counters every <n> frames (leak\n"
"                       hunting: watch heap= over a long run)\n"
"  --verbose <1-3>      log level: 2 is the default, 3 adds decoder and\n"
"                       media decisions (for diagnosing silent audio/video)\n"
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
"  --upscale <mode>     present through the GLES2 shader pass: nearest|sharp|scanline|lcd|crt|fsr|bicubic\n"
"  --upscale-scale <n|auto>  output scale for --upscale (1-8, auto = display)\n"
"                          (default: auto for FSR, 2 for other filters)\n"
"  --version            print the version and exit\n"
"  --help               this text\n"
"  --client             open the games library instead of a bare game\n"
"                       (what no jar argument means in an SDL2 build)\n"
"  --games <dir>        the directory the client uses as its library\n"
"  --print-library      list the games library and exit\n"
"\n"
"the desktop client (sdl2)\n"
"  With no jar argument the SDL2 build opens a library window listing the\n"
"  .jar files in its games directory ($XDG_DATA_HOME/nano_j2me/games, or\n"
"  --games, or NJ2ME_GAMES).  Enter or a click runs one; O picks a jar\n"
"  anywhere; R rescans; S opens the settings; Q or Esc quits.  Dropping a\n"
"  .jar on the window runs it - on the library window and on a running game\n"
"  alike - and dropping a directory makes it the library.  Typing filters the\n"
"  library by name, file or MIDlet class, F1 shows the shortcut sheet and F2\n"
"  stars the selected jar; jars that have already run sit in a 最近玩过 section\n"
"  above the list.\n"
"  Closing a game window, or F10 while it runs, returns to the library.  The\n"
"  library window remembers the size it was left at.\n"
"  --client <jar> runs the jar first and shows the library when it ends;\n"
"  --client with --run-frames N runs N frames and exits, which is how the\n"
"  window path is tested without a user.\n"
"\n"
"  The S page edits the settings file: game scale, scale filter, fullscreen,\n"
"  renderer, gamepad, on-screen keypad, fps cap, key bindings, heap size, mute\n"
"  and the network switch.  They live in $XDG_CONFIG_HOME/nano_j2me/client.conf\n"
"  (default ~/.config/nano_j2me/client.conf), and a direct run reads the same\n"
"  file, so its heap and display choices apply to `nj2me game.jar` too.\n"
"  F11 toggles fullscreen in a game.  The key bindings page maps the twelve\n"
"  game keys and refuses the port's own F3/F5/F10/F11/F12/Tab controls.\n"
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
"  Add a finger id after pressed/dragged/released to report several\n"
"  pointers at once; the Nokia MultipointTouch listener sees each id and\n"
"  the MIDP pointer stays owned by the first finger.\n"
"      30:pressed#1@40,50 40:pressed#2@120,90 60:dragged#1@45,55\n"
"\n"
"examples:\n"
"  %s game.jar\n"
"  %s --port headless --dump-frames out/ game.jar\n"
"  %s --port sdl2 --scale 3 --heap 8 game.jar\n",
        argv0,
        (unsigned)NJ_CLI_HEAP_MB, NJ_RENDER_SCALE_MAX,
        NJ_SCREEN_WIDTH, NJ_SCREEN_HEIGHT,
        argv0, argv0, argv0);
}

typedef struct cli {
    const char *port_name;
    int graphics_mode;           /* -1 legacy env, 0 software, 1 GLES2, 2 Vulkan */
    const char *jar;
    const char *midlet;
    const char *rms_dir;
    const char *fs_dir;
    const char *dump_dir;
    int         dump_every;
    int         scale;
    int         render_scale, ssaa, subdivide, fsr2, smooth_assets;
    int         fps;
    int         width;
    int         height;
    unsigned    heap_mb;
    int         heap_given;     /* --heap wins over client.conf's heap_mb */
    int         heap_grow;
    int         grow_given;     /* explicit on/off wins over saved settings */
    int         stats;
    int         stats_every;
    int         network;
    int         where;
    int         run_frames;
    int         deterministic;   /* --deterministic: replay clock and RNGs   */
    int         verbose;
    const char *keys;
    const char *cheat_file;         /* --cheat-file: one code per line      */
#define NJ_CLI_MAX_CHEATS 16
    const char *cheat_codes[NJ_CLI_MAX_CHEATS];
    int         cheat_count;        /* --cheat entries collected so far     */
    const char *font;
    const char *device_name;
    int         upscale;      /* --upscale: -1 unset, 0..6 shader pass mode */
    int         upscale_scale;/* --upscale-scale: 0 port default */
    int         upscale_auto;/* --upscale-scale auto: 1 */
    float       upscale_sharp;/* --upscale-sharp: RCAS 0..1, -1 default */
    const char *save_state;
    const char *load_state;
    int         save_frame;
    const char *state_dir;      /* --state-dir: directory for slot files   */
    int         save_slot;      /* --save-slot: -1 unset                   */
    int         load_slot;      /* --load-slot: -1 unset                   */
    char        slot_save_path[1024];
    char        slot_load_path[1024];
    int         client;         /* --client: the desktop client, not one run */
    const char *games_dir;      /* --games: client library directory */
    int         print_library;  /* --print-library: list and exit */
} cli;

/* ---- save-state slots ---------------------------------------------------- */
#define NJ_CLI_SLOT_MAX 999

static int cli_parse_slot(const char *text, int *out)
{
    char *end = NULL;
    long v;
    if (text == NULL || *text == '\0') return -1;
    errno = 0;
    v = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || v < 0 || v > NJ_CLI_SLOT_MAX) {
        return -1;
    }
    *out = (int)v;
    return 0;
}

/*
 * Resolve "<dir>/<stem>-slot<N>.state".  The directory defaults to
 * "<jar dir>/states" and is created when missing; an explicit --state-dir has
 * to exist already so a typo cannot silently write somewhere unexpected.
 */
static int cli_slot_paths(cli *c)
{
    char dir[1024];
    char stem[256];
    const char *jar = c->jar;
    const char *base;
    const char *dot = NULL;
    const char *s;
    size_t n;
    size_t dlen;

    if (jar == NULL) return -1;
    base = jar;
    for (s = jar; *s != '\0'; s++) {
        if (*s == '/' || *s == '\\') base = s + 1;
    }
    n = strlen(base);
    if (n == 0) return -1;
    for (s = base; *s != '\0'; s++) {
        if (*s == '.') dot = s;
    }
    if (dot != NULL && dot > base) n = (size_t)(dot - base);
    if (n >= sizeof stem) return -1;
    memcpy(stem, base, n);
    stem[n] = '\0';

    if (c->state_dir != NULL) {
        if (strlen(c->state_dir) + 1u > sizeof dir) return -1;
        strcpy(dir, c->state_dir);
    } else {
        dlen = (size_t)(base - jar);
        if (dlen + 8u > sizeof dir) return -1;
        memcpy(dir, jar, dlen);
        if (dlen > 0u && dir[dlen - 1u] != '/' && dir[dlen - 1u] != '\\') {
            dir[dlen++] = '/';
        }
        strcpy(dir + dlen, "states");
        (void)NJ_CLI_MKDIR(dir);
    }
    if (c->save_slot >= 0) {
        snprintf(c->slot_save_path, sizeof c->slot_save_path,
                 "%s/%s-slot%d.state", dir, stem, c->save_slot);
    }
    if (c->load_slot >= 0) {
        snprintf(c->slot_load_path, sizeof c->slot_load_path,
                 "%s/%s-slot%d.state", dir, stem, c->load_slot);
    }
    return 0;
}

#if defined(_WIN32)
#define NJ_CLI_SETENV(name, value) _putenv_s((name), (value))
#else
#define NJ_CLI_SETENV(name, value) setenv((name), (value), 1)
#endif

/*
 * Direct SDL2 runs get save slots for free: the port's F5/F6/F7/F8/F9 keys need
 * a directory and a game prefix, and this is the only place that knows both the
 * JAR path and that the SDL2 port was chosen.  An environment variable set by
 * the caller always wins.
 */
static void cli_slot_env(const cli *c)
{
    char dir[1024];
    char stem[256];
    const char *jar = c->jar;
    const char *base;
    const char *dot = NULL;
    const char *s;
    size_t n;
    size_t dlen;

    if (jar == NULL || getenv("NJ_SDL2_STATE_DIR") != NULL ||
        (getenv("NJ_SDL2_STATE") != NULL && getenv("NJ_SDL2_STATE")[0] != '\0')) {
        return;
    }
    base = jar;
    for (s = jar; *s != '\0'; s++) {
        if (*s == '/' || *s == '\\') base = s + 1;
    }
    n = strlen(base);
    if (n == 0) return;
    for (s = base; *s != '\0'; s++) {
        if (*s == '.') dot = s;
    }
    if (dot != NULL && dot > base) n = (size_t)(dot - base);
    if (n >= sizeof stem) return;
    memcpy(stem, base, n);
    stem[n] = '\0';

    dlen = (size_t)(base - jar);
    if (dlen + 8u > sizeof dir) return;
    memcpy(dir, jar, dlen);
    if (dlen > 0u && dir[dlen - 1u] != '/' && dir[dlen - 1u] != '\\') {
        dir[dlen++] = '/';
    }
    strcpy(dir + dlen, "states");
    (void)NJ_CLI_MKDIR(dir);
    (void)NJ_CLI_SETENV("NJ_SDL2_STATE_DIR", dir);
    (void)NJ_CLI_SETENV("NJ_SDL2_STATE_STEM", stem);
}

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

/*
 * The library directory, in the order a user expects: --games wins, then the
 * environment (handy for a shell alias or a test), then client.conf, then the
 * XDG default the settings module creates.
 */
static int resolve_games_dir(const cli *c, char *out, int cap)
{
    const char *env;
    nj_client_settings s;
    const char *src = NULL;

    if (c->games_dir != NULL && c->games_dir[0] != '\0') {
        src = c->games_dir;
    } else {
        env = getenv("NJ2ME_GAMES");
        if (env != NULL && env[0] != '\0') {
            src = env;
        }
    }
    if (src == NULL) {
        nj_client_settings_load(&s, NULL);
        src = s.games_dir;
    }
    if (src == NULL || src[0] == '\0') {
        return 0;
    }
    return snprintf(out, (size_t)cap, "%s", src) < cap;
}

static int print_library(const cli *c)
{
    char dir[NJ_CLIENT_PATH_MAX];
    nj_library *lib;
    int n;
    int i;

    if (!resolve_games_dir(c, dir, (int)sizeof(dir))) {
        fprintf(stderr, "no games directory; name one with --games <dir>\n");
        return 2;
    }
    lib = nj_library_new();
    if (lib == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    n = nj_lib_scan(lib, dir);
    printf("library: %s\n", dir);
    for (i = 0; i < n; i++) {
        const nj_lib_entry *e = &lib->entries[i];
        if (e->valid) {
            printf("  ok   %-40s %s\n", e->name, e->file);
        } else {
            printf("  bad  %-40s %s\n", e->name, e->note);
        }
    }
    printf("%d games, %d runnable\n", n, lib->valid_count);
    nj_library_free(lib);
    return 0;
}

/* Create the port for a direct run and apply the CLI's display settings. */
static int setup_port(const cli *c, nj_port *port,
                      const nj_client_settings *settings)
{
    nj_status st;
    int use_sdl2 = 0;

    memset(port, 0, sizeof(*port));
    if (strcmp(c->port_name, "headless") == 0) {
        st = nj_port_headless_ex(port, c->width, c->height,
                                 c->dump_dir ? c->dump_every : 0, c->dump_dir);
        if (st != NJ_OK) {
            fprintf(stderr, "cannot create the headless port\n");
            return 1;
        }
#if NJ_ENABLE_VULKAN
        if (c->graphics_mode == 2 && !nj_vk_renderer_attach(port))
            fprintf(stderr, "Vulkan initialization failed, using software drawing\n");
#endif
    } else if (strcmp(c->port_name, "sdl2") == 0) {
#if NJ_HAVE_SDL2
        use_sdl2 = 1;
        /*
         * The port reads its renderer/audio/network knobs from the environment
         * at construction time, so the settings file has to be applied before
         * the window exists.  This is the same helper the desktop client uses,
         * so `nj2me game.jar` obeys the same settings.
         */
        if (settings != NULL) {
            (void)nj_client_apply_game_settings(NULL, settings);
        }
        st = nj_port_sdl2_ex(port, "nano_j2me", c->graphics_mode);
        if (st == NJ_ERR_UNSUPPORTED) {
            fprintf(stderr, "requested drawing backend is unavailable; use --graphics software\n");
            return 2;
        }
        if (st != NJ_OK) {
            fprintf(stderr, "cannot create the SDL2 port (is DISPLAY set?)\n");
            return 1;
        }
#else
        fprintf(stderr, "this build has no SDL2 port; use --port headless\n");
        return 2;
#endif
    } else {
        fprintf(stderr, "unknown port: %s\n", c->port_name);
        return 2;
    }

    if (c->network && strcmp(c->port_name, "headless") == 0) {
        st = nj_port_headless_enable_network(port);
        if (st != NJ_OK) {
            fprintf(stderr, "this build has no desktop network backend\n");
            nj_port_shutdown(port);
            return 2;
        }
    }

    port->render_scale = c->fsr2 && c->render_scale < 2 ? 2 : c->render_scale;
    port->render_ssaa = c->ssaa;
    port->render_subdivide = c->subdivide;
    port->render_fsr2 = c->fsr2;
    port->render_smooth_assets = c->smooth_assets;
    port->width = c->width;
    port->height = c->height;
    if (c->fps >= 0) {
        port->fps_cap = c->fps;
    } else if (settings != NULL) {
        port->fps_cap = settings->fps_cap;
    }
    port->rms_dir = c->rms_dir;
    /*
     * No filesystem unless asked for.  JSR 75 exposes the host's directories to
     * untrusted code, and a default that hands a MIDlet the working directory
     * is a default nobody thought about.  --fs-dir names the root a MIDlet is
     * confined to.
     */
    port->fs_dir = c->fs_dir;
    if (use_sdl2) {
        if (c->scale > 0) {
            port->scale = c->scale;
        } else if (settings != NULL && settings->game_scale > 0) {
            port->scale = settings->game_scale;
        }
#if NJ_HAVE_SDL2
        if (settings != NULL) {
            (void)nj_client_apply_game_settings(port, settings);
        }
#endif
    }
    return 0;
}

#if NJ_HAVE_SDL2
static int run_client_mode(const cli *c, const nj_run_opts *opts)
{
    nj_client_settings settings;
    nj_client_run_options co;
    const char *env;
    nj_library *lib;
    nj_status st;

    if (strcmp(c->port_name, "headless") == 0) {
        fprintf(stderr, "the desktop client needs the SDL2 port; run a jar "
                        "directly for --port headless\n");
        return 2;
    }
    nj_client_settings_load(&settings, NULL);
    env = getenv("NJ2ME_GAMES");
    if (c->games_dir != NULL && c->games_dir[0] != '\0') {
        if (snprintf(settings.games_dir, sizeof(settings.games_dir), "%s",
                     c->games_dir) >= (int)sizeof(settings.games_dir)) {
            fprintf(stderr, "--games path is too long\n");
            return 2;
        }
    } else if (env != NULL && env[0] != '\0') {
        if (snprintf(settings.games_dir, sizeof(settings.games_dir), "%s",
                     env) >= (int)sizeof(settings.games_dir)) {
            fprintf(stderr, "NJ2ME_GAMES path is too long\n");
            return 2;
        }
    }
    lib = nj_library_new();
    if (lib == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    memset(&co, 0, sizeof(co));
    co.game = opts;
    co.settings = &settings;
    co.library = lib;
    co.first_jar = c->jar;
    co.width = c->width;
    co.height = c->height;
    co.scale = c->scale;
    co.fps_cap = c->fps;
    co.keys_visible = -1; /* the port's own default */
    co.rms_dir = c->rms_dir;
    co.fs_dir = c->fs_dir;
    co.heap_grow_override = c->grow_given ? (c->heap_grow ? 1 : -1) : 0;
    co.graphics_mode = c->graphics_mode + 1;
    co.render_scale = c->fsr2 && c->render_scale < 2 ? 2 : c->render_scale;
    co.render_ssaa = c->ssaa;
    co.render_subdivide = c->subdivide;
    co.render_fsr2 = c->fsr2;
    co.render_smooth_assets = c->smooth_assets;
    st = nj_client_run(&co);
    if (c->stats) {
        fprintf(stderr, "nano_j2me: exit status %d\n", (int)st);
    }
    nj_library_free(lib);
    return st == NJ_OK ? 0 : 1;
}
#endif

#if defined(_WIN32)
/*
 * The C runtime converts a Windows command line to narrow argv through the
 * ANSI code page.  A jar called 1942空战_10403.jar therefore arrives as GBK
 * (CP936) bytes on a Chinese system, or as "?" characters on an English one -
 * not the UTF-8 the runtime reads paths in, and sometimes destroyed before
 * main() even sees it.  Rebuild argv from the wide command line instead; the
 * allocation lives for the rest of the process.
 */
static char **win32_utf8_argv(int *out_argc)
{
    wchar_t **wargv;
    char **argv;
    int argc = 0;
    int i;

    if (!out_argc) {
        return NULL;
    }
    *out_argc = 0;
    wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wargv || argc <= 0) {
        if (wargv) {
            LocalFree(wargv);
        }
        return NULL;
    }
    argv = (char **)calloc((size_t)argc + 1u, sizeof(*argv));
    if (!argv) {
        LocalFree(wargv);
        return NULL;
    }
    for (i = 0; i < argc; i++) {
        int need = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0,
                                       NULL, NULL);
        if (need <= 0) {
            break;
        }
        argv[i] = (char *)malloc((size_t)need);
        if (!argv[i] ||
            !WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, argv[i], need,
                                 NULL, NULL)) {
            break;
        }
    }
    LocalFree(wargv);
    if (i != argc) {
        while (i > 0) {
            free(argv[--i]);
        }
        free(argv);
        return NULL;
    }
    argv[argc] = NULL;
    *out_argc = argc;
    return argv;
}
#endif

int main(int argc, char **argv)
{
    cli c;
    const char *nj_prof_env = getenv("NJ_SAMPLE_PROF");
    if (nj_prof_env && nj_prof_env[0] == '1') {
        nj_prof_start();
        atexit(nj_prof_dump);
    }
    nj_port port;
    nj_run_opts opts;
    nj_client_settings settings;
    nj_status st;
    int i;
    /* The merged property table nj_run() installs: the device, then overrides. */
    static nj_device_prop props[NJ_CLI_MAX_PROPS];
    static char prop_key[NJ_CLI_MAX_PROPS][NJ_CLI_PROP_LEN];
    static char prop_val[NJ_CLI_MAX_PROPS][NJ_CLI_PROP_LEN];
    int nprops = 0;

#if defined(_WIN32)
    {
        int wide_argc = 0;
        char **wide_argv = win32_utf8_argv(&wide_argc);
        if (wide_argv) {
            argc = wide_argc;
            argv = wide_argv;
        }
    }
#endif

    memset(&c, 0, sizeof(c));
    c.graphics_mode = -1; /* Software by default, with legacy env opt-in. */
    c.dump_every = 1;
    c.fps = -1;              /* -1: leave the port's own cap alone */
    c.width = NJ_SCREEN_WIDTH;
    c.height = NJ_SCREEN_HEIGHT;
    c.heap_mb = (unsigned)NJ_CLI_HEAP_MB;
    c.upscale = -1;
    c.upscale_sharp = -1.0f;
    c.save_slot = -1;
    c.load_slot = -1;
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
        } else if (strcmp(a, "--dump-builtin-api") == 0 && i + 1 < argc) {
            if (!nj_dump_builtin_api(argv[++i])) {
                fprintf(stderr, "cannot write builtin API dump\n");
                return 2;
            }
            return 0;
        } else if (strcmp(a, "--port") == 0 && i + 1 < argc) {
            c.port_name = argv[++i];
        } else if (strcmp(a, "--graphics") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--graphics requires software, gles2 or vulkan\n");
                return 2;
            }
            a = argv[++i];
            if (strcmp(a, "software") == 0) c.graphics_mode = 0;
            else if (strcmp(a, "gles2") == 0) c.graphics_mode = 1;
            else if (strcmp(a, "vulkan") == 0) c.graphics_mode = 2;
            else {
                fprintf(stderr, "bad --graphics: %s (expected software, gles2 or vulkan)\n", a);
                return 2;
            }
        } else if (strcmp(a, "--midlet") == 0 && i + 1 < argc) {
            c.midlet = argv[++i];
        } else if (strcmp(a, "--font") == 0 && i + 1 < argc) {
            c.font = argv[++i];
        } else if (strcmp(a, "--heap") == 0 && i + 1 < argc) {
            char *end;
            const char *value = argv[++i];
            unsigned long mb;
            errno = 0;
            mb = strtoul(value, &end, 10);
            if (value[0] == '-' || end == value || *end != '\0' ||
                errno == ERANGE || mb > UINT_MAX ||
                mb > (unsigned long)((nj_size)-1) /
                                           (1024u * 1024u)) {
                fprintf(stderr, "bad --heap (expected nonnegative MB)\n");
                return 2;
            }
            c.heap_mb = (unsigned)mb;
            c.heap_given = 1;
        } else if (strcmp(a, "--heap-grow") == 0) {
            c.heap_grow = 1;
            c.grow_given = 1;
        } else if (strcmp(a, "--no-heap-grow") == 0) {
            c.heap_grow = 0;
            c.grow_given = 1;
        } else if (strcmp(a, "--render-scale") == 0 || strcmp(a, "--ssaa") == 0) {
            int samples = strcmp(a, "--ssaa") == 0;
            int maximum = samples ? 2 : NJ_RENDER_SCALE_MAX;
            if (++i >= argc || argv[i][0] < '1' || argv[i][0] > '0' + maximum ||
                argv[i][1] != '\0') {
                fprintf(stderr, "%s expects an integer from 1 to %d\n", a, maximum);
                return 2;
            }
            if (samples) c.ssaa = argv[i][0] - '0';
            else c.render_scale = argv[i][0] - '0';
        } else if (strcmp(a, "--subdivide") == 0) {
            c.subdivide = 1;
        } else if (strcmp(a, "--asset-filter") == 0) {
            const char *mode = ++i < argc ? argv[i] : "";
            if (strcmp(mode, "nearest") == 0) c.smooth_assets = NJ_ASSET_NEAREST;
            else if (strcmp(mode, "smooth") == 0) c.smooth_assets = NJ_ASSET_SMOOTH;
            else if (strcmp(mode, "pixel-art") == 0) c.smooth_assets = NJ_ASSET_PIXEL_ART;
            else if (strcmp(mode, "fsr1") == 0) c.smooth_assets = NJ_ASSET_FSR1;
            else {
                fprintf(stderr, "--asset-filter expects nearest, smooth, pixel-art or fsr1\n");
                return 2;
            }
        } else if (strcmp(a, "--smooth-assets") == 0) {
            c.smooth_assets = NJ_ASSET_SMOOTH;
        } else if (strcmp(a, "--fsr2") == 0) {
#if NJ_ENABLE_FSR2
            c.fsr2 = 1;
#else
            fprintf(stderr, "--fsr2 requires a build with NJ_ENABLE_FSR2=ON\n");
            return 2;
#endif
        } else if (strcmp(a, "--scale") == 0 && i + 1 < argc) {
            c.scale = atoi(argv[++i]);
        } else if (strcmp(a, "--upscale") == 0 && i + 1 < argc) {
            const char *m = argv[++i];
            if (strcmp(m, "nearest") == 0) c.upscale = 0;
            else if (strcmp(m, "sharp") == 0) c.upscale = 1;
            else if (strcmp(m, "scanline") == 0) c.upscale = 2;
            else if (strcmp(m, "lcd") == 0) c.upscale = 3;
            else if (strcmp(m, "crt") == 0) c.upscale = 4;
            else if (strcmp(m, "fsr") == 0) c.upscale = 5;
            else if (strcmp(m, "bicubic") == 0) c.upscale = 6;
            else if (m[0] >= '0' && m[0] <= '6' && m[1] == '\0') c.upscale = m[0] - '0';
            else {
                fprintf(stderr, "--upscale: bad mode %s (nearest|sharp|scanline|lcd|crt|fsr|bicubic|0..6)\n", m);
                return 2;
            }
        } else if (strcmp(a, "--upscale-sharp") == 0 && i + 1 < argc) {
            const char *sv = argv[++i];
            float fv = (float)atof(sv);
            if (fv <= 0.0f || fv > 1.0f) {
                fprintf(stderr, "--upscale-sharp: expected 0..1, got %s\n", sv);
                return 2;
            }
            c.upscale_sharp = fv;
        } else if (strcmp(a, "--upscale-scale") == 0 && i + 1 < argc) {
            const char *sc = argv[++i];
            if (strcmp(sc, "auto") == 0) {
                c.upscale_auto = 1;
            } else {
                c.upscale_scale = atoi(sc);
            }
            if (!c.upscale_auto && (c.upscale_scale < 1 || c.upscale_scale > 8)) {
                fprintf(stderr, "--upscale-scale: expected 1..8\n");
                return 2;
            }
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
        } else if (strcmp(a, "--cheat") == 0 && i + 1 < argc) {
            if (c.cheat_count >= NJ_CLI_MAX_CHEATS) {
                fprintf(stderr, "too many --cheat entries (limit %d)\n",
                        NJ_CLI_MAX_CHEATS);
                return 2;
            }
            c.cheat_codes[c.cheat_count++] = argv[++i];
        } else if (strcmp(a, "--cheat-file") == 0 && i + 1 < argc) {
            c.cheat_file = argv[++i];
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
        } else if (strcmp(a, "--client") == 0) {
            c.client = 1;
        } else if (strcmp(a, "--games") == 0 && i + 1 < argc) {
            c.games_dir = argv[++i];
        } else if (strcmp(a, "--print-library") == 0) {
            c.print_library = 1;
        } else if (strcmp(a, "--stats") == 0) {
            c.stats = 1;
        } else if (strcmp(a, "--stats-every") == 0 && i + 1 < argc) {
            c.stats_every = atoi(argv[++i]);
            if (c.stats_every < 0) {
                c.stats_every = 0;
            }
        } else if (strcmp(a, "--where") == 0) {
            c.where = 1;
        } else if (strcmp(a, "--run-frames") == 0 && i + 1 < argc) {
            c.run_frames = atoi(argv[++i]);
        } else if (strcmp(a, "--deterministic") == 0) {
            c.deterministic = 1;
        } else if (strcmp(a, "--verbose") == 0 && i + 1 < argc) {
            c.verbose = atoi(argv[++i]);
            if (c.verbose < 1) {
                c.verbose = 1;
            }
            if (c.verbose > 3) {
                c.verbose = 3;
            }
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
        } else if (strcmp(a, "--state-dir") == 0 && i + 1 < argc) {
            c.state_dir = argv[++i];
        } else if (strcmp(a, "--save-slot") == 0 && i + 1 < argc) {
            if (cli_parse_slot(argv[++i], &c.save_slot) != 0) {
                fprintf(stderr, "--save-slot needs an integer in 0..%d\n",
                        NJ_CLI_SLOT_MAX);
                return 2;
            }
        } else if (strcmp(a, "--load-slot") == 0 && i + 1 < argc) {
            if (cli_parse_slot(argv[++i], &c.load_slot) != 0) {
                fprintf(stderr, "--load-slot needs an integer in 0..%d\n",
                        NJ_CLI_SLOT_MAX);
                return 2;
            }
        } else if (a[0] == '-') {
            fprintf(stderr, "unknown option: %s\n", a);
            usage(argv[0]);
            return 2;
        } else {
            c.jar = a;
        }
    }

    if (c.graphics_mode == 1 && strcmp(c.port_name, "sdl2") != 0) {
        fprintf(stderr, "--graphics gles2 requires --port sdl2\n");
        return 2;
    }
#if !NJ_ENABLE_VULKAN
    if (c.graphics_mode == 2) {
        fprintf(stderr, "--graphics vulkan requires NJ_ENABLE_VULKAN=ON\n");
        return 2;
    }
#endif

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

    memset(&opts, 0, sizeof(opts));
    opts.jar_path = c.jar;
    opts.midlet_class = c.midlet;
    opts.heap_bytes = (nj_size)c.heap_mb * 1024u * 1024u;
    opts.argv = (const char *const *)argv;
    opts.argc = argc;
    opts.stats = c.stats;
    opts.stats_every = (nj_u4)c.stats_every;
    opts.where = c.where;
    opts.run_frames = (nj_u4)(c.run_frames > 0 ? c.run_frames : 0);
    opts.deterministic = c.deterministic;
    opts.verbose = c.verbose;
    opts.keys = c.keys;
    opts.cheat_file = c.cheat_file;
    opts.cheat_codes = c.cheat_count > 0 ? c.cheat_codes : NULL;
    opts.cheat_count = c.cheat_count;
    opts.font_path = c.font;
    opts.props = nprops > 0 ? props : NULL;
    opts.nprops = nprops;
    opts.device = c.device_name ? nj_device_find(c.device_name) : NULL;
    opts.load_state_path = c.load_state;
    opts.save_state_path = c.save_state;
    opts.save_state_frame = (nj_u4)(c.save_frame > 0 ? c.save_frame : 0);

    /*
     * The settings file also feeds a direct run: heap size unless --heap was
     * named, and the port/display knobs setup_port applies.  A headless run
     * has no use for them, but loading is cheap and keeps one code path.
     */
    nj_client_settings_load(&settings, NULL);
    if (!c.heap_given) {
        opts.heap_bytes = (nj_size)settings.heap_mb * 1024u * 1024u;
    }
    opts.heap_limit = nj_client_heap_limit(&settings,
                         c.grow_given ? (c.heap_grow ? 1 : -1) : 0,
                         opts.heap_bytes);

    if (c.print_library) {
        return print_library(&c);
    }

#if NJ_HAVE_SDL2
    /*
     * Bare `nj2me` in an SDL2 build is the desktop client; naming a jar runs
     * that game first.  A headless build with no jar keeps the old usage error
     * below, because a serial console has nothing to draw a library on.
     */
    if (c.client || (c.jar == NULL && strcmp(c.port_name, "headless") != 0)) {
        return run_client_mode(&c, &opts);
    }
#endif
    if (c.jar == NULL) {
        usage(argv[0]);
        return 2;
    }

    if (c.save_slot >= 0 || c.load_slot >= 0) {
        if (c.save_slot >= 0 && c.save_state != NULL) {
            fprintf(stderr, "--save-slot and --save-state are mutually exclusive\n");
            return 2;
        }
        if (c.load_slot >= 0 && c.load_state != NULL) {
            fprintf(stderr, "--load-slot and --load-state are mutually exclusive\n");
            return 2;
        }
        if (cli_slot_paths(&c) != 0) {
            fprintf(stderr, "cannot build a save-slot path from %s\n", c.jar);
            return 2;
        }
        if (c.save_slot >= 0) {
            opts.save_state_path = c.slot_save_path;
            if (opts.save_state_frame == 0u) {
                opts.save_state_frame = 1u;
            }
            printf("state: slot %d -> %s\n", c.save_slot, c.slot_save_path);
        }
        if (c.load_slot >= 0) {
            opts.load_state_path = c.slot_load_path;
            printf("state: resuming slot %d <- %s\n",
                   c.load_slot, c.slot_load_path);
        }
    }

    if (strcmp(c.port_name, "sdl2") == 0) {
        cli_slot_env(&c);
    }

    if (c.upscale >= 0) {
        char v[8];
        if (c.graphics_mode == 0 || c.graphics_mode == 2) {
            fprintf(stderr, "--upscale needs a GLES2 path: use --graphics gles2\n");
        } else {
            if (c.graphics_mode < 0) c.graphics_mode = 1;
            snprintf(v, sizeof v, "%d", c.upscale);
#if defined(_WIN32)
            _putenv_s("NJ_GL_PRESENT", v);
            if (c.upscale_scale > 0 || c.upscale_auto) {
                snprintf(v, sizeof v, "%d", c.upscale_auto ? 0 : c.upscale_scale);
                _putenv_s("NJ_GL_PRESENT_SCALE", v);
            }
            if (c.upscale_sharp > 0.0f) {
                snprintf(v, sizeof v, "%.3f", (double)c.upscale_sharp);
                _putenv_s("NJ_GL_PRESENT_SHARP", v);
            }
#else
            setenv("NJ_GL_PRESENT", v, 1);
            if (c.upscale_scale > 0 || c.upscale_auto) {
                snprintf(v, sizeof v, "%d", c.upscale_auto ? 0 : c.upscale_scale);
                setenv("NJ_GL_PRESENT_SCALE", v, 1);
            }
            if (c.upscale_sharp > 0.0f) {
                snprintf(v, sizeof v, "%.3f", (double)c.upscale_sharp);
                setenv("NJ_GL_PRESENT_SHARP", v, 1);
            }
#endif
        }
    }

    {
        int prc = setup_port(&c, &port, &settings);
        if (prc != 0) {
            return prc;
        }
        if (c.deterministic && strcmp(c.port_name, "headless") == 0) {
            /*
             * The VM half of the replay contract is nj_run_opts.deterministic;
             * the clock half belongs to the port, and only the headless one
             * has a virtual clock to offer.  A real window keeps wall time.
             */
            nj_port_headless_set_virtual_clock(port.ud, 1);
        }
    }

#if NJ_HAVE_SDL2
    /*
     * Direct runs honor jar drops too: dropping a game on a running game
     * starts it instead of doing nothing.  The run ended with the drop, so the
     * window is rebuilt for the next game the same way a fresh run builds it.
     */
    {
        char chain_jar[NJ_CLIENT_PATH_MAX];

        for (;;) {
            st = (nj_status)nj_run(&port, &opts);
            if (strcmp(c.port_name, "sdl2") != 0 ||
                nj_port_sdl2_take_dropped(&port, chain_jar,
                                          (nj_u4)sizeof(chain_jar)) != 1) {
                break;
            }
            nj_port_shutdown(&port);
            {
                int prc = setup_port(&c, &port, &settings);
                if (prc != 0) {
                    return prc;
                }
            }
            opts.jar_path = chain_jar;
        }
    }
#else
    st = (nj_status)nj_run(&port, &opts);
#endif

    /* nj_run() does not own the port, so shutting it down is the caller's job. */
    nj_port_shutdown(&port);

    if (c.stats) {
        /* nj_run() printed the counters before tearing the VM down; say what
         * the host is being told so a script can check it without parsing. */
        fprintf(stderr, "nano_j2me: exit status %d\n", (int)st);
    }
    return st == NJ_OK ? 0 : 1;
}
