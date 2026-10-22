/*
 * A complete nj_port for a bare-metal 32-bit MCU, plus the nine C library
 * functions include/nj2me/nj_libc.h asks for.
 *
 * This exists to be LINKED, not described.  Compiling the runtime for a cross
 * target proves it parses; linking it into a firmware image with -nostdlib
 * proves the dependency claim in docs/FOOTPRINT.md is true - that nothing in
 * src/ reaches for a hosted libc, and that the runtime's own .data and .bss are
 * empty on a real 32-bit target.
 *
 * The port state lives in a struct owned by main(), like every other port here,
 * so this file adds no mutable globals of its own.
 */
#include "nj2me/nj.h"
#include <stdint.h>
#include "nj2me/nj_port.h"

static void bare_puts(const char *s);   /* the allocator reports through it */

/* ------------------------------------------------------------------ */
/* The nine functions.  Deliberately naive: the point is that the       */
/* runtime compiles and links against a minimal set, not that these are */
/* fast.  A real port uses the vendor SDK's versions.                   */
/*                                                                      */
/* ONLY WHEN THERE IS NO C LIBRARY.  Under ESP-IDF there is one, and    */
/* defining memcpy here as well is a multiple-definition error against  */
/* esp_libc - which is what the RISC-V link failed on.  The Xtensa      */
/* build happened to tolerate it; the fix is not to rely on that.       */
/* ------------------------------------------------------------------ */
#if !NJ_USE_LIBC

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) { *d++ = *s++; }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        while (n--) { *d++ = *s++; }
    } else {
        d += n; s += n;
        while (n--) { *--d = *--s; }
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--) { *d++ = (unsigned char)c; }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    while (n--) {
        if (*x != *y) { return (int)*x - (int)*y; }
        x++; y++;
    }
    return 0;
}

size_t strlen(const char *s) { const char *p = s; while (*p) { p++; } return (size_t)(p - s); }

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (!n) { return 0; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) { return (char *)s; }
        if (!*s) { return 0; }
    }
}

int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

/*
 * fmod is the one libm function that cannot be switched off: the interpreter
 * needs it for the frem/drem bytecodes.  Truncate toward zero and subtract;
 * slow and exact, and a port with a libm links the vendor's instead.
 */
double fmod(double x, double y)
{
    double q;
    if (y == 0.0) { return x; }
    q = x / y;
    {
        /* Truncate q toward zero without <math.h>. */
        long t = (long)q;
        q = (double)t;
    }
    return x - q * y;
}

double floor(double x)
{
    long t = (long)x;                 /* truncates toward zero */
    if ((double)t > x) { t -= 1; }    /* and floor goes the other way for negatives */
    return (double)t;
}

double ceil(double x)
{
    long t = (long)x;
    if ((double)t < x) { t += 1; }
    return (double)t;
}

/* Round half to even, the way IEEE 754 and java.lang.Math.rint require. */
double rint(double x)
{
    double f = floor(x);
    double d = x - f;
    if (d > 0.5) { return f + 1.0; }
    if (d < 0.5) { return f; }
    {
        long fi = (long)f;
        return ((fi & 1L) != 0L) ? f + 1.0 : f;
    }
}

/*
 * The java.lang.Math transcendentals.
 *
 * These are stubs, and that is deliberate: it makes the link succeed and makes
 * the dependency VISIBLE in one place rather than hiding it.  NJ_ENABLE_FLOAT=0
 * does NOT remove them - the switch is defined in nj_config.h and no source
 * honours it, which docs/FOOTPRINT.md records as a known gap.  So the honest
 * statement is that this runtime needs the full libm set, and a port without a
 * libm supplies it (or accepts that Math.sqrt and friends return NaN).
 *
 * A real port links the vendor's libm here; this file exists to prove that
 * NOTHING ELSE in the runtime needs a C library.
 */
static double bare_unimplemented(double x) { return x; }

double sqrt(double x) { return bare_unimplemented(x); }
double sin(double x)  { return bare_unimplemented(x); }
double cos(double x)  { return bare_unimplemented(x); }
double tan(double x)  { return bare_unimplemented(x); }
double asin(double x) { return bare_unimplemented(x); }
double acos(double x) { return bare_unimplemented(x); }
double atan(double x) { return bare_unimplemented(x); }
double atan2(double y, double x) { (void)y; return bare_unimplemented(x); }
double exp(double x)  { return bare_unimplemented(x); }
double log(double x)  { return bare_unimplemented(x); }
double log10(double x){ return bare_unimplemented(x); }
double pow(double x, double y) { (void)y; return bare_unimplemented(x); }
double cbrt(double x) { return bare_unimplemented(x); }

#endif /* !NJ_USE_LIBC */

/* ------------------------------------------------------------------ */
/* Port state, owned by the caller                                      */
/* ------------------------------------------------------------------ */
/*
 * Framebuffer size, overridable at compile time.
 *
 * 240x320x4 is 300 KB, which does not fit in an ESP32-S3's internal SRAM next
 * to a Java heap and the IDF runtime - the real port (nj_port_esp32.c) draws
 * into a panel and keeps the Java heap in PSRAM for exactly this reason.  The
 * QEMU image shrinks both so it fits in internal RAM, because what it is there
 * to prove is that the RUNTIME executes, not that a 240x320 framebuffer fits.
 */
#ifndef BARE_W
#  define BARE_W 64
#endif
#ifndef BARE_H
#  define BARE_H 64
#endif

typedef struct bare_state {
    nj_u4  fb[BARE_W * BARE_H];   /* ARGB words; a panel driver DMAs this out */
    nj_u4  now_ms;
    int    frames;
    int    stop_after;
    nj_u1 *heap;                  /* the arena, owned by the harness */
    nj_size heap_bytes;
    nj_size heap_used;            /* allocator state belongs to the PORT, not
                                   * to a file-scope variable - the same rule
                                   * the runtime follows */
} bare_state;

/* ---- clock ---- */
static nj_u4 bare_millis(void *ud)
{
    /* A real port reads a hardware timer.  This one advances by a fixed step
     * per frame so the image is deterministic and does not need a timer at
     * all - which is also what makes it usable as a link test. */
    return ((bare_state *)ud)->now_ms;
}

static void bare_sleep(void *ud, nj_u4 ms) { ((bare_state *)ud)->now_ms += ms; }
static void bare_idle(void *ud) { (void)ud; }

/* ---- memory: one arena the linker places, no malloc ---- */
static void *bare_alloc(void *ud, nj_size n)
{
    bare_state *s = (bare_state *)ud;
    nj_size aligned = (n + (NJ_OBJ_ALIGN - 1u)) & ~(nj_size)(NJ_OBJ_ALIGN - 1u);
    uintptr_t base;
    uintptr_t at;
    nj_size pos;

    /*
     * Align the POINTER, not the offset.
     *
     * The Java arena must be 16-byte aligned because it holds long and double
     * fields, and nj_heap_init() can only REJECT an unaligned one - so a port
     * that gets this wrong fails with "nj_vm_create: heap failed" and no hint
     * as to why, which is exactly how this was found.  Rounding the offset is
     * not enough: the base is whatever the linker chose, and a static array is
     * not guaranteed to be 16-byte aligned in every linker script.
     */
    if (!s->heap) {
        return 0;
    }
    base = (uintptr_t)s->heap;
    at = (base + (uintptr_t)s->heap_used + (uintptr_t)(NJ_OBJ_ALIGN - 1u))
         & ~(uintptr_t)(NJ_OBJ_ALIGN - 1u);
    pos = (nj_size)(at - base);

    if (pos + aligned > s->heap_bytes) {
        /* Say what was asked for and what is left.  A port that goes quiet on
         * allocation failure turns a sizing mistake into a mystery. */
        char b[96];
        unsigned k = 0;
        const char *q = "nj2me: out of port memory: asked ";
        while (*q) { b[k++] = *q++; }
        {
            char t[16]; int m = 0;
            unsigned long v = (unsigned long)aligned;
            while (v) { t[m++] = (char)('0' + v % 10u); v /= 10u; }
            if (!m) { t[m++] = '0'; }
            while (m) { b[k++] = t[--m]; }
        }
        q = " used "; while (*q) { b[k++] = *q++; }
        {
            char t[16]; int m = 0;
            unsigned long v = (unsigned long)s->heap_used;
            while (v) { t[m++] = (char)('0' + v % 10u); v /= 10u; }
            if (!m) { t[m++] = '0'; }
            while (m) { b[k++] = t[--m]; }
        }
        q = " of "; while (*q) { b[k++] = *q++; }
        {
            char t[16]; int m = 0;
            unsigned long v = (unsigned long)s->heap_bytes;
            while (v) { t[m++] = (char)('0' + v % 10u); v /= 10u; }
            if (!m) { t[m++] = '0'; }
            while (m) { b[k++] = t[--m]; }
        }
        b[k++] = '\n'; b[k] = 0;
        bare_puts(b);
        return 0;
    }
    s->heap_used = pos + aligned;
    return (void *)at;
}

static void bare_free(void *ud, void *p) { (void)ud; (void)p; }

/* ---- display ---- */
static int bare_display_init(void *ud, int *w, int *h, int *stride, void **px)
{
    bare_state *s = (bare_state *)ud;
    *w = BARE_W; *h = BARE_H;
    *stride = BARE_W * 4;
    *px = s->fb;
    return NJ_PIXFMT_ARGB8888;
}

static void bare_present(void *ud, int x, int y, int w, int h)
{
    bare_state *s = (bare_state *)ud;
    (void)x; (void)y; (void)w; (void)h;
    s->frames++;
    s->now_ms += 16u;         /* a 60 Hz panel */
}

/*
 * UART output, so the image can be RUN and not merely linked.
 *
 * Both ESP32-S3 (Xtensa) and ESP32-C3 (RISC-V) put UART0 at 0x60000000 with the
 * transmit FIFO in the first word, which is all this needs - no driver, no
 * header, no SDK.  Define NJ_BARE_UART=0 to compile it out for a target whose
 * console is somewhere else (SWO, semihosting, a memory ring the debugger
 * reads).
 *
 * QEMU's esp32s3 and esp32c3 machines model this FIFO, so the firmware's own
 * output appears on the emulated serial line - which is what turns "it links
 * for the target" into "it runs on the target".
 */
/*
 * On by default ONLY where the address means something.  0x60000000 is UART0 on
 * an ESP32, and on a host it is unmapped - so a build that runs this port's
 * code on a desktop (tools/make_bare_firmware.sh does, to exercise the same
 * main.c) must set NJ_BARE_UART=0 or the first log line segfaults.
 */
#ifndef NJ_BARE_UART
#  if defined(__XTENSA__) || defined(__riscv)
#    define NJ_BARE_UART 1
#  else
#    define NJ_BARE_UART 0
#  endif
#endif

#if NJ_BARE_UART
#  define BARE_UART0_FIFO (*(volatile unsigned int *)0x60000000u)
static void bare_putc(char c)
{
    int spin = 0;
    BARE_UART0_FIFO = (unsigned int)(unsigned char)c;
    while (spin++ < 1000) { }        /* let the FIFO drain a little */
}
#elif defined(NJ_USE_LIBC) && NJ_USE_LIBC
/* No UART, but there is a C library: this is the desktop build that runs the
 * same main.c so the firmware's logic is exercised.  Without this the port goes
 * silent exactly when it has something to say about why it failed. */
#include <stdio.h>
static void bare_putc(char c) { fputc(c, stderr); }
#else
static void bare_putc(char c) { (void)c; }
#endif

static void bare_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') { bare_putc('\r'); }
        bare_putc(*s++);
    }
}

static void bare_log(void *ud, int level, const char *msg)
{
    (void)ud;
    if (level <= 1) { bare_puts("nj2me: "); }
    bare_puts(msg);
    bare_putc('\n');
}

/* Frames presented so far.  main() cannot see bare_state, and an embedder
 * reading the port's own counters is the normal shape anyway. */
int nj_port_bare_frames(const void *ud) { return ((const bare_state *)ud)->frames; }

/*
 * One line saying what happened, on the console.  A firmware test with no
 * output is indistinguishable from a firmware that hung, and "did it finish"
 * is the only question QEMU can answer for us.
 */
void nj_port_bare_report(int rc, int frames)
{
    char buf[64];
    int n = 0;
    const char *p = "NJ2ME DONE rc=";
    const char *q;
    while (*p) { buf[n++] = *p++; }
    buf[n++] = (char)('0' + (rc < 0 ? 0 : rc % 10));
    p = " frames=";
    while (*p) { buf[n++] = *p++; }
    {
        char tmp[12];
        int k = 0;
        if (frames == 0) { tmp[k++] = '0'; }
        while (frames > 0) { tmp[k++] = (char)('0' + frames % 10); frames /= 10; }
        while (k > 0) { buf[n++] = tmp[--k]; }
    }
    q = "\n";
    while (*q) { buf[n++] = *q++; }
    bare_puts(buf);
    fflush(stderr);
    /* Emulators stop the guest the instant the marker appears, which can cut
     * the line above mid-write (fputc goes through a buffered stream under
     * ESP-IDF).  Flush, then print a second marker the signal is about: an
     * exit-on string has to be something that is only reachable once the
     * result line is complete. */
    bare_puts("NJ2ME END\n");
    fflush(stderr);
}

nj_status nj_port_bare(nj_port *out, bare_state *st, void *heap, nj_size heap_bytes)
{
    if (!out || !st) { return NJ_ERR; }
    st->now_ms = 0u;
    st->frames = 0;
    st->stop_after = 0;
    st->heap = (nj_u1 *)heap;
    st->heap_bytes = heap_bytes;
    st->heap_used = 0u;
    memset(out, 0, sizeof(*out));
    out->name = "bare";
    out->ud = st;
    out->millis = bare_millis;
    out->sleep_ms = bare_sleep;
    out->idle = bare_idle;
    out->alloc = bare_alloc;
    out->free = bare_free;
    out->display_init = bare_display_init;
    out->display_present = bare_present;
    out->log = bare_log;
    return NJ_OK;
}
