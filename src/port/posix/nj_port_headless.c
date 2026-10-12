/*
 * clock_gettime/nanosleep need the POSIX feature macro under -std=c99, which
 * defines __STRICT_ANSI__ and hides them otherwise.  It has to come before any
 * system header.
 */
/*
 * Unconditional: NJ_USE_LIBC is defined by nj_config.h, which is included
 * further down, so guarding on it here would always be false and the macro
 * would never take effect.
 */
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

/*
 * nano_j2me - nj_port_headless.c
 *
 * The reference port, and the one the test suite uses.
 *
 * It renders into a plain memory framebuffer (so the rasterizer can be
 * exercised exactly as on a real panel), takes input from a scripted event
 * list or stdin, and can dump frames as PPM/ANSI for eyeballing or regression
 * comparison.
 *
 * It is also the template for a new port: about 90 lines implement the whole
 * contract.
 */
#include "nj2me/nj_port.h"
#include "nj2me/nj_types.h"
#include "nj2me/nj.h"

#include <sys/mman.h>
#include <dirent.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

typedef struct nj_headless_state {
    int    width;
    int    height;
    int    stride;
    void  *pixels;

    nj_event queue[128];
    int    qh;
    int    qt;

    int    frame;
    int    dump_every;      /* 0 = never */
    const char *dump_prefix;
    int    use_ansi;
    int    quit;
    int    key_state[NJ_KEY_COUNT];

    int    auto_quit_frames; /* 0 = run until the app exits */

    /*
     * Audio, recorded rather than played.
     *
     * The headless port is the one the test suite drives, so what it has to do
     * with sound is make it OBSERVABLE: a test can then assert that a MIDlet
     * asked for the right notes, in the right order, for the right durations,
     * without a sound card anywhere in the picture.  A port that played the
     * tones and said nothing would make every audio path untestable.
     *
     * The counters live in the caller's state, not in the C file, so two
     * headless ports in one process do not share a note count.
     */
    int    tone_calls;       /* how many times tone_play was called */
    int    tone_count;       /* tones accepted, total */
    int    tone_last_freq;   /* last call's arguments, for a quick assertion */
    int    tone_last_ms;
    int    tone_last_first;  /* first frequency of the last sequence */
    int    tone_stops;
    nj_audio_tone last_tones[8];
    int    last_tones_n;

    int    pcm_opens;
    int    pcm_closes;
    long   pcm_frames;       /* frames accepted, total */
    int    pcm_rate;
    int    pcm_channels;

    /*
     * Files, for the same reason as the audio counters above.
     *
     * RecordStore and FileConnection both go through the port's file hooks
     * (see src/midp/nj_lib_rms.c), and both are otherwise invisible from
     * outside: a MIDlet that saves a high score and one that silently fails to
     * are the same sequence of frames.  A test can only tell them apart by
     * asserting on what the port was asked to write, so the port has to expose
     * it.
     */
    int    file_opens;
    int    file_closes;
    int    file_reads;
    int    file_writes;
    long   bytes_read;
    long   bytes_written;
    char   file_last_path[256];   /* last path opened, for a direct assertion */
    char   file_last_mode[8];
} nj_headless_state;

#define HS(ud) ((nj_headless_state *)(ud))

/* ------------------------------------------------------------------ */
/* Time                                                                */
/* ------------------------------------------------------------------ */
static nj_u4 hl_millis(void *ud)
{
    (void)ud;
    /*
     * WALL time, not CPU time.
     *
     * This used clock(), which measures processor time consumed by this
     * process.  That is the wrong clock for an emulated device in a way that
     * breaks everything downstream: a guest thread inside Thread.sleep()
     * consumes no CPU time, so the guest's clock barely advanced while it
     * slept, and System.currentTimeMillis(), the frame pacer and every timeout
     * in the runtime were all measured against a clock that stops when the
     * emulator is idle.  A game's own countdown ran at whatever rate the host
     * happened to be busy.
     */
#if defined(NJ_USE_LIBC) && NJ_USE_LIBC
    {
        struct timespec ts;
        if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
            return (nj_u4)((nj_u8)ts.tv_sec * 1000u
                           + (nj_u8)(ts.tv_nsec / 1000000));
        }
        return (nj_u4)((double)clock() * 1000.0 / (double)CLOCKS_PER_SEC);
    }
#elif defined(CLOCKS_PER_SEC)
    return (nj_u4)((double)clock() * 1000.0 / (double)CLOCKS_PER_SEC);
#else
    return 0u;
#endif
}

static void hl_sleep_ms(void *ud, nj_u4 ms)
{
    (void)ud;
    /*
     * Actually sleep.  This was a no-op, which quietly disabled the frame
     * limiter on the reference port: --fps was accepted, stored, and then
     * ignored, so a "60 fps" run presented 1750 frames a second.  The visible
     * symptom was a game whose background thread never got scheduled - it
     * sleeps 50 emulated milliseconds between updates, and the wall clock
     * reached the end of the run first - so the picture never changed while
     * every counter said the game was running.
     */
#if defined(NJ_USE_LIBC) && NJ_USE_LIBC
    if (ms > 0u) {
        struct timespec ts;
        ts.tv_sec = (time_t)(ms / 1000u);
        ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
        nanosleep(&ts, NULL);
    }
#else
    (void)ms;
#endif
}

static void hl_idle(void *ud)
{
    (void)ud;
}

static int hl_input_push(void *ud, const nj_event *ev);

/* ------------------------------------------------------------------ */
/* Executable memory                                                   */
/* ------------------------------------------------------------------ */
/*
 * The reference implementation of the code_alloc/code_commit contract, and it
 * is worth having one: an API with no implementation is an API that has never
 * been checked, and the three platform details it hides are easy to get wrong.
 *
 *   - mmap gives writable pages that are NOT executable.  Handing those back
 *     and calling it done is the classic mistake: the JIT writes code and the
 *     first jump faults.
 *   - so code_commit flips them to read+execute, which is also what a W^X
 *     policy requires - the pages are writable while being written and never
 *     executable at the same time.
 *   - and the instruction cache has to be synchronised afterwards.  x86 does
 *     this in hardware and __builtin___clear_cache is a no-op there; on ARM,
 *     Xtensa and RISC-V it is not, and skipping it produces code that runs the
 *     first time and executes stale bytes afterwards.
 *
 * We record the length in a header word so code_free can munmap the same range
 * without the caller having to remember it.
 */
typedef struct hl_code_block {
    nj_size len;                /* total, header included */
} hl_code_block;

static void *hl_code_alloc(void *ud, nj_size n)
{
    void *p;
    nj_size total;
    (void)ud;

    if (n == 0u) {
        return NULL;
    }
    total = n + sizeof(hl_code_block);
    p = mmap(NULL, (size_t)total, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        return NULL;
    }
    ((hl_code_block *)p)->len = total;
    return (unsigned char *)p + sizeof(hl_code_block);
}

static int hl_code_commit(void *ud, void *p, nj_size n)
{
    unsigned char *base;
    nj_size total;
    (void)ud;

    if (!p) {
        return -1;
    }
    base = (unsigned char *)p - sizeof(hl_code_block);
    total = ((hl_code_block *)base)->len;
    if (n + sizeof(hl_code_block) > total) {
        return -1;              /* the caller is describing someone else's block */
    }
    __builtin___clear_cache((char *)p, (char *)p + n);
    if (mprotect(base, (size_t)total, PROT_READ | PROT_EXEC) != 0) {
        return -1;
    }
    return 0;
}

static void hl_code_free(void *ud, void *p, nj_size n)
{
    nj_size total;
    (void)ud;
    (void)n;
    if (!p) {
        return;
    }
    total = ((hl_code_block *)((unsigned char *)p - sizeof(hl_code_block)))->len;
    (void)munmap((unsigned char *)p - sizeof(hl_code_block), (size_t)total);
}

/*
 * Device properties.  These are facts about the MACHINE, which is why they live
 * in the port and not in the runtime.
 *
 * com.nokia.keyboard.type deserves the comment: a game built from source here
 * probed it and called .equals() on the result without checking for null, which
 * is a bug in the game - but a real Nokia device always answers, so the game
 * never met it.  A desktop has no keypad, and "None" is exactly what that
 * means: the game then hides its on-screen keypad command, which is correct
 * behaviour for a device that has no keypad to hide.
 */
static const char *hl_device_property(void *ud, const char *key)
{
    (void)ud;
    if (!key) {
        return NULL;
    }
    if (strcmp(key, "com.nokia.keyboard.type") == 0) {
        return "None";              /* there is no keypad on a desktop */
    }
    if (strcmp(key, "com.nokia.mid.ui.version") == 0) {
        return "1.1";
    }
    if (strcmp(key, "microedition.platform") == 0) {
        return "nano_j2me/headless";
    }
    return NULL;                    /* honestly unknown */
}


/* ------------------------------------------------------------------ */
/* Directory iteration                                                 */
/* ------------------------------------------------------------------ */
/*
 * POSIX opendir/readdir, with the handle wrapped so the runtime never sees a
 * DIR*.  "." and ".." are skipped: a MIDlet listing a directory wants its
 * contents, and every implementation of JSR 75 that returned them would put
 * two entries in a file picker that cannot be opened.
 */
/*
 * POSIX opendir/readdir, with the handle wrapped so the runtime never sees a
 * DIR* and the directory's own path travels with the iteration - stat() needs
 * the full path to answer isDirectory(), and a dirent does not carry one.
 *
 * "." and ".." are skipped: a MIDlet listing a directory wants its CONTENTS,
 * and returning them would put two entries in a file picker that cannot be
 * opened.
 */
typedef struct hl_dir {
    DIR  *d;
    char  path[1024];
} hl_dir;

static int hl_dir_open(void *ud, const char *path, void **out_handle)
{
    hl_dir *h;
    nj_size n = 0;
    (void)ud;

    if (!path || !out_handle) {
        return -1;
    }
    h = (hl_dir *)malloc(sizeof(*h));
    if (!h) {
        return -1;
    }
    h->d = opendir(path);
    if (!h->d) {
        free(h);
        return -1;
    }
    while (path[n] && n + 1u < sizeof(h->path)) {
        h->path[n] = path[n];
        n++;
    }
    h->path[n] = '\0';
    *out_handle = h;
    return 0;
}

static int hl_dir_next(void *ud, void *handle, char *name, nj_size cap,
                       int *is_dir)
{
    hl_dir *h = (hl_dir *)handle;
    struct dirent *e;
    (void)ud;

    if (!h || !name || cap == 0u) {
        return -1;
    }
    while ((e = readdir(h->d)) != NULL) {
        nj_size n = 0;
        char full[2048];
        struct stat st;

        if (e->d_name[0] == '.' &&
            (e->d_name[1] == '\0' ||
             (e->d_name[1] == '.' && e->d_name[2] == '\0'))) {
            continue;
        }
        while (e->d_name[n] && n + 1u < cap) {
            name[n] = e->d_name[n];
            n++;
        }
        name[n] = '\0';
        if (is_dir) {
            /*
             * stat() rather than the dirent's d_type, which is not populated on
             * every filesystem.  A wrong isDirectory() makes a file picker
             * offer files as folders, and the failure only shows when the user
             * taps one.
             */
            *is_dir = 0;
            if (snprintf(full, sizeof(full), "%s/%s", h->path, name) > 0 &&
                stat(full, &st) == 0) {
                *is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
            }
        }
        return 1;
    }
    return 0;
}

static void hl_dir_close(void *ud, void *handle)
{
    hl_dir *h = (hl_dir *)handle;
    (void)ud;
    if (h) {
        closedir(h->d);
        free(h);
    }
}

/* ------------------------------------------------------------------ */
/* Files                                                               */
/* ------------------------------------------------------------------ */
/*
 * stdio, with the FILE* hidden behind nj_file.handle.
 *
 * The headless port left these hooks NULL for a long time, which is not the
 * harmless omission it looks like: RecordStore and FileConnection both check
 * `port->file_open` before doing anything (src/midp/nj_lib_rms.c), so on this
 * port a MIDlet's saves and its file reads were silently skipped rather than
 * failing.  The suite did not catch it because tests/test_fs.c brings its own
 * port; every headless corpus run since has been running without a filesystem.
 *
 * Paths arrive already resolved against the port's fs_dir by the runtime, so
 * this layer adds no sandbox of its own - the sandbox that matters is the one
 * that knows which paths a MIDlet is allowed to name, and that lives in
 * src/midp/nj_lib_fc.c.
 */
static void hl_note_path(nj_headless_state *s, const char *name, const char *mode)
{
    nj_size n = 0;

    if (!s) {
        return;
    }
    if (name) {
        while (name[n] && n + 1u < sizeof(s->file_last_path)) {
            s->file_last_path[n] = name[n];
            n++;
        }
    }
    s->file_last_path[n] = '\0';

    n = 0;
    if (mode) {
        while (mode[n] && n + 1u < sizeof(s->file_last_mode)) {
            s->file_last_mode[n] = mode[n];
            n++;
        }
    }
    s->file_last_mode[n] = '\0';
}

static int hl_file_open(void *ud, const char *name, const char *mode,
                        nj_file *out)
{
    nj_headless_state *s = HS(ud);
    FILE *fp;

    if (!name || !out) {
        return -1;
    }
    fp = fopen(name, mode ? mode : "rb");
    if (!fp) {
        return -1;
    }
    out->handle = (void *)fp;
    out->pos = 0u;
    out->size = 0u;
    {
        struct stat st;
        if (stat(name, &st) == 0 && st.st_size > 0) {
            out->size = (nj_u4)st.st_size;
        }
    }
    s->file_opens++;
    hl_note_path(s, name, mode);
    return 0;
}

static nj_s4 hl_file_read(void *ud, nj_file *f, void *buf, nj_u4 n)
{
    nj_headless_state *s = HS(ud);
    size_t got;

    if (!f || !f->handle || !buf || n == 0u) {
        return 0;
    }
    got = fread(buf, 1u, (size_t)n, (FILE *)f->handle);
    f->pos += (nj_u4)got;
    s->file_reads++;
    s->bytes_read += (long)got;
    return (nj_s4)got;
}

static nj_s4 hl_file_write(void *ud, nj_file *f, const void *buf, nj_u4 n)
{
    nj_headless_state *s = HS(ud);
    size_t put;

    if (!f || !f->handle || !buf || n == 0u) {
        return 0;
    }
    put = fwrite(buf, 1u, (size_t)n, (FILE *)f->handle);
    f->pos += (nj_u4)put;
    if (f->pos > f->size) {
        f->size = f->pos;
    }
    s->file_writes++;
    s->bytes_written += (long)put;
    return (nj_s4)put;
}

static void hl_file_close(void *ud, nj_file *f)
{
    nj_headless_state *s = HS(ud);

    if (!f || !f->handle) {
        return;
    }
    fclose((FILE *)f->handle);
    f->handle = NULL;
    s->file_closes++;
}

static int hl_file_seek(void *ud, nj_file *f, nj_u4 pos)
{
    (void)ud;
    if (!f || !f->handle) {
        return -1;
    }
    if (fseek((FILE *)f->handle, (long)pos, SEEK_SET) != 0) {
        return -1;
    }
    f->pos = pos;
    return 0;
}

static int hl_file_remove(void *ud, const char *name)
{
    (void)ud;
    if (!name) {
        return -1;
    }
    return remove(name) == 0 ? 0 : -1;
}

static int hl_file_exists(void *ud, const char *name)
{
    struct stat st;
    (void)ud;
    if (!name) {
        return 0;
    }
    return stat(name, &st) == 0 ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Audio: recorded, not played                                         */
/* ------------------------------------------------------------------ */
static int hl_tone_play(void *ud, const nj_audio_tone *tones, int count)
{
    nj_headless_state *s = HS(ud);
    int i;

    if (!s || !tones || count <= 0) {
        return -1;
    }
    s->tone_calls++;
    s->tone_count += count;
    s->tone_last_freq = (int)tones[count - 1].freq_hz;
    s->tone_last_ms = (int)tones[count - 1].duration_ms;
    s->tone_last_first = (int)tones[0].freq_hz;
    s->last_tones_n = (count < 8) ? count : 8;
    for (i = 0; i < s->last_tones_n; i++) {
        s->last_tones[i] = tones[i];
    }
    return 0;
}

static void hl_tone_stop(void *ud)
{
    nj_headless_state *s = HS(ud);
    if (s) {
        s->tone_stops++;
    }
}

static int hl_pcm_open(void *ud, int sample_rate, int channels)
{
    nj_headless_state *s = HS(ud);
    if (!s) {
        return -1;
    }
    s->pcm_opens++;
    s->pcm_rate = sample_rate;
    s->pcm_channels = channels;
    return 0;
}

static int hl_pcm_write(void *ud, const nj_s2 *samples, int frames)
{
    nj_headless_state *s = HS(ud);
    if (!s || !samples || frames <= 0) {
        return -1;
    }
    s->pcm_frames += frames;
    return frames;          /* always accept: the counter is the point */
}

static void hl_pcm_close(void *ud)
{
    nj_headless_state *s = HS(ud);
    if (s) {
        s->pcm_closes++;
    }
}

int nj_port_headless_tone_calls(const nj_port *p)
{
    return (p && p->ud) ? HS(p->ud)->tone_calls : -1;
}

int nj_port_headless_tone_count(const nj_port *p)
{
    return (p && p->ud) ? HS(p->ud)->tone_count : -1;
}

int nj_port_headless_tone_stops(const nj_port *p)
{
    return (p && p->ud) ? HS(p->ud)->tone_stops : -1;
}

int nj_port_headless_tone_first(const nj_port *p)
{
    return (p && p->ud) ? HS(p->ud)->tone_last_first : -1;
}

long nj_port_headless_pcm_frames(const nj_port *p)
{
    return (p && p->ud) ? HS(p->ud)->pcm_frames : -1;
}

int nj_port_headless_pcm_opens(const nj_port *p)
{
    return (p && p->ud) ? HS(p->ud)->pcm_opens : -1;
}

int nj_port_headless_pcm_closes(const nj_port *p)
{
    return (p && p->ud) ? HS(p->ud)->pcm_closes : -1;
}

int nj_port_headless_pcm_format(const nj_port *p, int *rate, int *channels)
{
    if (!p || !p->ud) {
        return -1;
    }
    if (rate) { *rate = HS(p->ud)->pcm_rate; }
    if (channels) { *channels = HS(p->ud)->pcm_channels; }
    return 0;
}

int nj_port_headless_file_opens(const nj_port *p)
{
    return (p && p->ud) ? HS(p->ud)->file_opens : -1;
}

int nj_port_headless_file_closes(const nj_port *p)
{
    return (p && p->ud) ? HS(p->ud)->file_closes : -1;
}

int nj_port_headless_file_reads(const nj_port *p)
{
    return (p && p->ud) ? HS(p->ud)->file_reads : -1;
}

int nj_port_headless_file_writes(const nj_port *p)
{
    return (p && p->ud) ? HS(p->ud)->file_writes : -1;
}

long nj_port_headless_bytes_read(const nj_port *p)
{
    return (p && p->ud) ? HS(p->ud)->bytes_read : -1;
}

long nj_port_headless_bytes_written(const nj_port *p)
{
    return (p && p->ud) ? HS(p->ud)->bytes_written : -1;
}

const char *nj_port_headless_last_path(const nj_port *p)
{
    return (p && p->ud) ? HS(p->ud)->file_last_path : NULL;
}

const char *nj_port_headless_last_mode(const nj_port *p)
{
    return (p && p->ud) ? HS(p->ud)->file_last_mode : NULL;
}

static void hl_log(void *ud, int level, const char *msg)
{
    (void)ud;
    (void)level;
    fputs(msg, stderr);
    fputc('\n', stderr);
}

/* ------------------------------------------------------------------ */
/* Display                                                             */
/* ------------------------------------------------------------------ */
static int hl_display_init(void *ud, int *w, int *h, int *stride, void **pixels)
{
    nj_headless_state *s = HS(ud);

    if (s->width <= 0) {
        s->width = NJ_SCREEN_WIDTH;
    }
    if (s->height <= 0) {
        s->height = NJ_SCREEN_HEIGHT;
    }
#if NJ_PIXFMT == NJ_PIXFMT_ARGB8888
    s->stride = s->width * 4;
#elif NJ_PIXFMT == NJ_PIXFMT_RGB888
    s->stride = s->width * 3;
#else
    s->stride = s->width * 2;
#endif
    s->pixels = calloc(1u, (size_t)s->stride * (size_t)s->height);
    if (!s->pixels) {
        return -1;
    }
    *w = s->width;
    *h = s->height;
    *stride = s->stride;
    *pixels = s->pixels;
    return NJ_PIXFMT;
}

static void ppm_dump(nj_headless_state *s, const char *path)
{
    FILE *f = fopen(path, "wb");
    int x, y;

    if (!f) {
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", s->width, s->height);
    for (y = 0; y < s->height; y++) {
        const unsigned char *row = (const unsigned char *)s->pixels +
                                   (size_t)y * (size_t)s->stride;
        for (x = 0; x < s->width; x++) {
            unsigned char rgb[3];
#if NJ_PIXFMT == NJ_PIXFMT_ARGB8888
            const unsigned char *p = row + x * 4;
            /* Stored B,G,R,A on little endian ARGB8888 words. */
            rgb[0] = p[2]; rgb[1] = p[1]; rgb[2] = p[0];
#elif NJ_PIXFMT == NJ_PIXFMT_RGB888
            const unsigned char *p = row + x * 3;
            rgb[0] = p[2]; rgb[1] = p[1]; rgb[2] = p[0];
#else
            unsigned v = (unsigned)row[x * 2] | ((unsigned)row[x * 2 + 1] << 8);
            rgb[0] = (unsigned char)(((v >> 11) & 0x1Fu) << 3);
            rgb[1] = (unsigned char)(((v >> 5) & 0x3Fu) << 2);
            rgb[2] = (unsigned char)((v & 0x1Fu) << 3);
#endif
            fwrite(rgb, 1u, 3u, f);
        }
    }
    fclose(f);
}

static void hl_display_present(void *ud, int x, int y, int w, int h)
{
    nj_headless_state *s = HS(ud);
    char path[256];

    (void)x; (void)y; (void)w; (void)h;
    s->frame++;
    if (s->dump_every > 0 && (s->frame % s->dump_every) == 0 && s->dump_prefix) {
        snprintf(path, sizeof(path), "%s%05d.ppm", s->dump_prefix, s->frame);
        ppm_dump(s, path);
    }
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */
static int hl_input_poll(void *ud, nj_event *ev)
{
    nj_headless_state *s = HS(ud);

    if (s->qh == s->qt) {
        return 0;
    }
    *ev = s->queue[s->qh];
    s->qh = (s->qh + 1) % (int)(sizeof(s->queue) / sizeof(s->queue[0]));
    return 1;
}

static void hl_shutdown(void *ud)
{
    nj_headless_state *s = HS(ud);

    if (!s) {
        return;
    }
    if (s->pixels) {
        free(s->pixels);
        s->pixels = NULL;
    }
    free(s);
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */
nj_status nj_port_headless(nj_port *out)
{
    return nj_port_headless_ex(out, 0, 0, 0, NULL);
}

void nj_port_headless_free(nj_port *p)
{
    if (p && p->ud) {
        free(p->ud);
        p->ud = NULL;
    }
}

nj_status nj_port_headless_ex(nj_port *out, int width, int height,
                              int dump_every, const char *dump_prefix)
{
    nj_headless_state *s;

    if (!out) {
        return NJ_ERR;
    }
    s = (nj_headless_state *)calloc(1u, sizeof(*s));
    if (!s) {
        return NJ_ERR_NOMEM;
    }
    s->width = width;         /* 0 lets the runtime apply NJ_SCREEN_* */
    s->height = height;
    s->dump_every = dump_every;
    s->dump_prefix = dump_prefix;
    s->auto_quit_frames = 0;

    memset(out, 0, sizeof(*out));
    out->name = "headless";
    out->ud = s;
    out->millis = hl_millis;
    out->sleep_ms = hl_sleep_ms;
    out->idle = hl_idle;
    out->device_property = hl_device_property;
    out->code_alloc = hl_code_alloc;
    out->code_commit = hl_code_commit;
    out->code_free = hl_code_free;
    out->dir_open = hl_dir_open;
    out->dir_next = hl_dir_next;
    out->dir_close = hl_dir_close;
    out->file_open = hl_file_open;
    out->file_read = hl_file_read;
    out->file_write = hl_file_write;
    out->file_seek = hl_file_seek;
    out->file_close = hl_file_close;
    out->file_remove = hl_file_remove;
    out->file_exists = hl_file_exists;
    out->input_push = hl_input_push;
    out->tone_play = hl_tone_play;
    out->tone_stop = hl_tone_stop;
    out->pcm_open = hl_pcm_open;
    out->pcm_write = hl_pcm_write;
    out->pcm_close = hl_pcm_close;
    out->log = hl_log;
    out->display_init = hl_display_init;
    out->display_present = hl_display_present;
    out->input_poll = hl_input_poll;
    out->shutdown = hl_shutdown;
    out->scale = 1;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Helpers used by the test harness and by embedders with their own loop */
/* ------------------------------------------------------------------ */
void nj_port_headless_set_dump(void *ud, const char *prefix, int every)
{
    nj_headless_state *s = HS(ud);
    if (s) {
        s->dump_prefix = prefix;
        s->dump_every = every;
    }
}

void nj_port_headless_push(void *ud, const nj_event *ev)
{
    nj_headless_state *s = HS(ud);
    int next;

    if (!s || !ev) {
        return;
    }
    next = (s->qt + 1) % (int)(sizeof(s->queue) / sizeof(s->queue[0]));
    if (next == s->qh) {
        return;   /* full */
    }
    s->queue[s->qt] = *ev;
    s->qt = next;
}

/* Public wrapper: append events to whichever port supports it. */
static int hl_input_push(void *ud, const nj_event *ev)
{
    if (!ud || !ev) {
        return 0;
    }
    nj_port_headless_push(ud, ev);
    return 1;
}

void nj_port_push_event(nj_port *port, const nj_event *ev)
{
    if (!port || !ev) {
        return;
    }
    if (port->name && strcmp(port->name, "headless") == 0) {
        nj_port_headless_push(port->ud, ev);
    }
}

int nj_port_headless_frames(void *ud)
{
    nj_headless_state *s = HS(ud);
    return s ? s->frame : 0;
}
