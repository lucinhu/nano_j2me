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
} nj_headless_state;

#define HS(ud) ((nj_headless_state *)(ud))

/* ------------------------------------------------------------------ */
/* Time                                                                */
/* ------------------------------------------------------------------ */
static nj_u4 hl_millis(void *ud)
{
    (void)ud;
    {
        /* A monotonic-enough clock without <time.h> portability games: the
         * frame counter plus a real clock when the C library is available. */
#if defined(CLOCKS_PER_SEC)
        return (nj_u4)((double)clock() * 1000.0 / (double)CLOCKS_PER_SEC);
#else
        return 0u;
#endif
    }
}

static void hl_sleep_ms(void *ud, nj_u4 ms)
{
    (void)ud;
    (void)ms;
}

static void hl_idle(void *ud)
{
    (void)ud;
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
