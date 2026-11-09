/* Nokia DirectGraphics shapes: explicit ARGB, preserved Graphics state,
 * even-odd coverage, single source-over at joins, and bounded int geometry. */
#include "nj2me/nj.h"
#include "../src/midp/nj_midp.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 16
#define H 14
#define CHECK(c, msg) do { if (c) ++passed; else { ++failed; \
    fprintf(stderr, "FAIL %d: %s\n", __LINE__, msg); } } while (0)
static unsigned passed, failed;
static void *(*base_alloc)(void *, nj_size);
static nj_size last_alloc_size, fail_alloc_size;
static unsigned injected;

static void *fault_alloc(void *ud, nj_size size)
{
    last_alloc_size = size;
    if (size == fail_alloc_size) {
        fail_alloc_size = 0; ++injected; return NULL;
    }
    return base_alloc ? base_alloc(ud, size) : malloc(size);
}

typedef struct {
    nj_vm *vm;
    nj_ref image, g, output;
} canvas;

static void call(canvas *c, const char *name, const char *desc, nj_value *a)
{
    CHECK(nj_call(c->vm, c->g, NJ_C_GRAPHICS, name, desc, a, NULL) == NJ_OK,
          name);
}

static nj_u4 background(int i)
{
    return ((nj_u4)((i * 37 + 41) & 255) << 24) |
           ((nj_u4)(i * 194987 + 0x234671) & 0xffffffu);
}

/* Independent channel arithmetic, including rounding and destination alpha. */
static nj_u4 over(nj_u4 d, nj_u4 s)
{
    unsigned a = s >> 24, ia = 255 - a, shift;
    nj_u4 result = ((a * 255 + (d >> 24) * ia + 127) / 255) << 24;
    if (a == 0) return d;
    for (shift = 0; shift < 24; shift += 8)
        result |= ((((s >> shift) & 255) * a +
                    ((d >> shift) & 255) * ia + 127) / 255) << shift;
    return result;
}

static void reset(canvas *c, int tx, int ty, int cx, int cy, int cw, int ch,
                  int dotted)
{
    nj_value a[4];
    nj_u4 *pixels = nj_midp_image_pixels(c->vm, c->image);
    int i, oldx = nj_call_int(c->vm, c->g, NJ_C_GRAPHICS, "getTranslateX", "()I", NULL);
    int oldy = nj_call_int(c->vm, c->g, NJ_C_GRAPHICS, "getTranslateY", "()I", NULL);
    for (i = 0; i < W * H; ++i) pixels[i] = background(i);
    a[0].i = tx - oldx; a[1].i = ty - oldy;
    call(c, "translate", "(II)V", a);
    a[0].i = cx; a[1].i = cy; a[2].i = cw; a[3].i = ch;
    call(c, "setClip", "(IIII)V", a);
    a[0].i = dotted;
    call(c, "setStrokeStyle", "(I)V", a);
    a[0].i = (nj_s4)0x7f315a8cu;
    call(c, "setARGBColor", "(I)V", a);
}

static void read_pixels(canvas *c, nj_u4 *out)
{
    nj_value a[7];
    nj_u4 *data = (nj_u4 *)nj_array_data(c->output);
    data[0] = data[W * H + 1] = 0xbad5eedu;
    a[0].r = c->output; a[1].i = 1; a[2].i = W;
    a[3].i = a[4].i = 0; a[5].i = W; a[6].i = H;
    CHECK(nj_call(c->vm, c->image, NJ_C_IMAGE, "getRGB", "([IIIIIII)V", a, NULL)
          == NJ_OK, "public pixel readback");
    CHECK(data[0] == 0xbad5eedu && data[W * H + 1] == 0xbad5eedu,
          "readback sentinels unchanged");
    memcpy(out, data + 1, W * H * sizeof(*out));
}

static void state(canvas *c, int *out)
{
    static const char *names[] = {"getColor", "getAlphaComponent", "getTranslateX",
        "getTranslateY", "getClipX", "getClipY", "getClipWidth", "getClipHeight",
        "getStrokeStyle"};
    unsigned i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        out[i] = nj_call_int(c->vm, c->g, NJ_C_GRAPHICS, names[i], "()I", NULL);
}

/* Point/ray parity oracle, independent of scanline sorting and rounded spans.
 * Pixel coordinates sample integer lattice points; left/top edges included,
 * right/bottom edges excluded. Long double keeps int-extreme probes safe. */
static int inside(const int *xs, const int *ys, int n, int x, int y)
{
    int i, j, parity = 0;
    for (i = 0, j = n - 1; i < n; j = i++) {
        if ((ys[i] <= y && ys[j] > y) || (ys[j] <= y && ys[i] > y)) {
            long double cross = (long double)xs[i] +
                ((long double)y - ys[i]) * ((long double)xs[j] - xs[i]) /
                ((long double)ys[j] - ys[i]);
            if ((long double)x < cross) parity ^= 1;
        }
    }
    return parity;
}

/* Full-path Bresenham oracle for small outlines; OR the edges before blending. */
static void mark_line(unsigned char *mask, int x, int y, int x1, int y1,
                      int tx, int ty, int cx, int cy, int cw, int ch, int dotted)
{
    int dx = x1 > x ? x1 - x : x - x1, sx = x < x1 ? 1 : -1;
    int dy = y1 > y ? y1 - y : y - y1, sy = y < y1 ? 1 : -1;
    int err = dx - dy, k = 0;
    for (;;) {
        int px = x + tx, py = y + ty, e2;
        if (px >= 0 && py >= 0 && px < W && py < H &&
            x >= cx && y >= cy && x < cx + cw && y < cy + ch &&
            (!dotted || !(k & 1))) mask[py * W + px] = 1;
        if (x == x1 && y == y1) break;
        e2 = 2 * err;
        if (e2 >= -dy) { err -= dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
        ++k;
    }
}

static void shape(canvas *c, const int *xs, const int *ys, int n, int kind,
                  nj_u4 color, int tx, int ty, const int *clip, int dotted)
{
    nj_u4 mark = nj_root_mark(c->vm), actual[W * H], expected[W * H];
    unsigned char mask[W * H] = {0};
    nj_value a[7], font_before, font_after;
    nj_ref xa, ya;
    int before[9], after[9], i, x, y;
    reset(c, tx, ty, clip[0], clip[1], clip[2], clip[3], dotted);
    CHECK(nj_call(c->vm, c->g, NJ_C_GRAPHICS, "getFont",
          "()Ljavax/microedition/lcdui/Font;", NULL, &font_before) == NJ_OK,
          "obtain current font");
    state(c, before);
    for (y = 0; y < H; ++y) for (x = 0; x < W; ++x) {
        int ux = x - tx, uy = y - ty;
        if ((kind & 1) && ux >= clip[0] && uy >= clip[1] &&
            ux < clip[0] + clip[2] && uy < clip[1] + clip[3])
            mask[y * W + x] = (unsigned char)inside(xs, ys, n, ux, uy);
    }
    if (!(kind & 1)) for (i = 0; i < n; ++i)
        mark_line(mask, xs[i], ys[i], xs[(i + 1) % n], ys[(i + 1) % n],
                  tx, ty, clip[0], clip[1], clip[2], clip[3], dotted);
    if (kind < 2) {
        xa = nj_root(c->vm, nj_int_array_new(c->vm, (nj_u4)n + 3));
        ya = nj_root(c->vm, nj_int_array_new(c->vm, (nj_u4)n + 4));
        CHECK(xa && ya, "offset polygon arrays allocated");
        for (i = 0; i < n; ++i) {
            ((nj_s4 *)nj_array_data(xa))[i + 1] = xs[i];
            ((nj_s4 *)nj_array_data(ya))[i + 2] = ys[i];
        }
        a[0].r = xa; a[1].i = 1; a[2].r = ya; a[3].i = 2;
        a[4].i = n; a[5].i = (nj_s4)color;
        call(c, kind ? "fillPolygon" : "drawPolygon", "([II[IIII)V", a);
    } else {
        for (i = 0; i < 3; ++i) { a[2 * i].i = xs[i]; a[2 * i + 1].i = ys[i]; }
        a[6].i = (nj_s4)color;
        call(c, kind == 3 ? "fillTriangle" : "drawTriangle", "(IIIIIII)V", a);
    }
    state(c, after);
    CHECK(nj_call(c->vm, c->g, NJ_C_GRAPHICS, "getFont",
          "()Ljavax/microedition/lcdui/Font;", NULL, &font_after) == NJ_OK &&
          font_before.r == font_after.r, "explicit shape preserves current font");
    CHECK(memcmp(before, after, sizeof(before)) == 0,
          "explicit shape preserves color, alpha, clip, translation and stroke");
    read_pixels(c, actual);
    for (i = 0; i < W * H; ++i)
        expected[i] = mask[i] ? over(background(i), color) : background(i);
    CHECK(memcmp(expected, actual, sizeof(actual)) == 0,
          "shape coverage and exactly one source-over per covered pixel");
    nj_root_release(c->vm, mark);
}

static void test_shapes(canvas *c)
{
    static const struct { int n, xs[10], ys[10]; } polys[] = {
        {4, {1, 11, 11, 1}, {1, 1, 11, 11}},
        {8, {1, 12, 12, 9, 9, 4, 4, 1}, {1, 1, 12, 12, 5, 5, 12, 12}},
        {4, {1, 12, 1, 12}, {1, 12, 12, 1}},
        {8, {1, 11, 11, 1, 1, 11, 11, 1}, {1, 1, 11, 11, 1, 1, 11, 11}},
        {6, {1, 11, 11, 11, 1, 1}, {1, 1, 1, 11, 11, 1}},
        {3, {2, 12, 2}, {2, 2, 12}},
        {3, {1, 6, 11}, {2, 7, 12}},
        {2, {2, 12}, {2, 10}},
        {1, {5}, {6}}, {0, {0}, {0}}
    };
    static const nj_u4 colors[] = {0x00319ac4u, 0x01319ac4u, 0x7f319ac4u,
        0x80319ac4u, 0xfe319ac4u, 0xff319ac4u};
    static const int clips[][4] = {{0, 0, W, H}, {2, 3, 7, 5}, {0, 0, 0, 0},
        {5, 6, 1, 1}, {-3, -2, 10, 10}};
    unsigned p, a, cl;
    int kind, dotted;
    for (p = 0; p < sizeof(polys) / sizeof(polys[0]); ++p)
        for (a = 0; a < sizeof(colors) / sizeof(colors[0]); ++a)
            for (cl = 0; cl < sizeof(clips) / sizeof(clips[0]); ++cl)
                for (kind = 0; kind < (polys[p].n == 3 ? 4 : 2); ++kind)
                    for (dotted = 0; dotted < 2; ++dotted)
                        shape(c, polys[p].xs, polys[p].ys, polys[p].n, kind,
                              colors[a], cl & 1 ? 2 : 0, cl & 1 ? -1 : 0,
                              clips[cl], dotted);
    /* Deterministic non-axis-aligned and clipped cases exercise edge rounding
     * in every octant without copying production interpolation into the oracle. */
    {
        nj_u4 rng = 0x3819ab5du;
        int sample, xs[9], ys[9], i, n;
        for (sample = 0; sample < 96; ++sample) {
            n = 3 + sample % 7;
            for (i = 0; i < n; ++i) {
                rng = rng * 1664525u + 1013904223u; xs[i] = (int)(rng % 27u) - 5;
                rng = rng * 1664525u + 1013904223u; ys[i] = (int)(rng % 23u) - 5;
            }
            for (kind = 0; kind < 2; ++kind)
                shape(c, xs, ys, n, kind, 0x805a39c1u, 2, -1, clips[1], sample & 1);
        }
    }
    /* All alpha values through the shared Graphics current-color path. */
    for (a = 0; a < 256; ++a) {
        nj_value v[4];
        nj_u4 actual[W * H], color = ((nj_u4)a << 24) | 0x4917d3u;
        reset(c, 0, 0, 0, 0, W, H, 0);
        v[0].i = (nj_s4)color;
        call(c, "setARGBColor", "(I)V", v);
        CHECK(nj_call_int(c->vm, c->g, NJ_C_GRAPHICS, "getAlphaComponent", "()I", NULL)
              == (int)a, "setARGBColor retains every alpha value");
        v[0].i = v[1].i = 0; v[2].i = v[3].i = 1;
        call(c, "fillRect", "(IIII)V", v);
        read_pixels(c, actual);
        CHECK(actual[0] == over(background(0), color), "MIDP drawing uses true DG alpha");
        v[0].i = 0x123456;
        call(c, "setColor", "(I)V", v);
        CHECK(nj_call_int(c->vm, c->g, NJ_C_GRAPHICS, "getAlphaComponent", "()I", NULL)
              == 255, "MIDP setColor restores opacity");
        v[0].i = (nj_s4)color; call(c, "setARGBColor", "(I)V", v);
        v[0].i = 71; call(c, "setGrayScale", "(I)V", v);
        CHECK(nj_call_int(c->vm, c->g, NJ_C_GRAPHICS, "getAlphaComponent", "()I", NULL)
              == 255, "MIDP grayscale restores opacity");
    }
}

static void test_limits(canvas *c)
{
    static const int cases[][3] = {{INT_MAX, 0, 1}, {1, 0, INT_MAX},
        {0, INT_MAX, 1}, {0, 1, INT_MAX}, {-1, 0, 0}, {0, -1, 0},
        {0, 0, -1}, {5, 0, 0}, {0, 5, 0}};
    nj_u4 mark = nj_root_mark(c->vm);
    nj_ref x = nj_root(c->vm, nj_int_array_new(c->vm, 4));
    nj_ref y = nj_root(c->vm, nj_int_array_new(c->vm, 4));
    nj_value a[6];
    unsigned i;
    int filled;
    reset(c, 0, 0, 0, 0, 0, 0, 0); /* Invalid arrays still throw under empty clip. */
    for (filled = 0; filled < 2; ++filled) {
        const char *name = filled ? "fillPolygon" : "drawPolygon";
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]) + 2; ++i) {
            nj_status st;
            const char *exception;
            a[0].r = x; a[2].r = y; a[5].i = -1;
            a[1].i = a[3].i = a[4].i = 0;
            if (i < sizeof(cases) / sizeof(cases[0])) {
                a[1].i = cases[i][0]; a[3].i = cases[i][1]; a[4].i = cases[i][2];
            } else a[i == sizeof(cases) / sizeof(cases[0]) ? 0 : 2].r = NULL;
            st = nj_call(c->vm, c->g, NJ_C_GRAPHICS, name, "([II[IIII)V", a, NULL);
            exception = nj_ref_class_name(c->vm->exception);
            CHECK(st != NJ_OK && exception && strcmp(exception,
                  i < sizeof(cases) / sizeof(cases[0]) ?
                  "java/lang/ArrayIndexOutOfBoundsException" :
                  "java/lang/NullPointerException") == 0, "polygon argument bounds");
            nj_clear_exception(c->vm);
        }
    }
    nj_root_release(c->vm, mark);
}

static void test_extreme(canvas *c)
{
    static const int clip[] = {0, 0, W, H};
    static const int xs[] = {INT_MIN, INT_MAX, INT_MAX, INT_MIN};
    static const int ys[] = {-1, -1, 1, 1};
    static const int huge_y[] = {INT_MIN, INT_MIN, INT_MAX, INT_MAX};
    shape(c, xs, ys, 4, 1, 0x804b9e17u, 0, 0, clip, 1);
    shape(c, xs, huge_y, 4, 1, 0x804b9e17u, 0, 0, clip, 0);
    /* Near-full unsigned 64-bit interpolation product, visible short output. */
    {
        static const int tx[] = {INT_MIN, INT_MAX, INT_MIN};
        static const int ty[] = {INT_MIN, INT_MAX, INT_MAX};
        shape(c, tx, ty, 3, 3, 0x804b9e17u, 0, 0, clip, 0);
    }
}

static void test_outline_extreme(canvas *c)
{
    int direction, dotted;
    for (direction = 0; direction < 2; ++direction) for (dotted = 0; dotted < 2; ++dotted) {
        nj_value a[7];
        nj_u4 actual[W * H];
        int x, y, equal = 1;
        reset(c, 0, 0, 0, 0, W, H, dotted);
        a[0].i = a[1].i = direction ? INT_MAX : INT_MIN;
        a[2].i = a[3].i = direction ? INT_MIN : INT_MAX;
        a[4].i = INT_MIN; a[5].i = INT_MAX; a[6].i = (nj_s4)0x805813acu;
        call(c, "drawTriangle", "(IIIIIII)V", a);
        read_pixels(c, actual);
        for (y = 0; y < H; ++y) for (x = 0; x < W; ++x) {
            int covered = x == y && (!dotted || ((x & 1) == direction));
            nj_u4 expected = covered ? over(background(y * W + x), 0x805813acu)
                                     : background(y * W + x);
            if (actual[y * W + x] != expected) equal = 0;
        }
        CHECK(equal, "int-extreme outline only iterates visible major-axis steps");
    }
    /* int-min source coordinates translated back into the visible device. */
    {
        nj_value a[7];
        nj_u4 actual[W * H];
        int x, y, equal = 1, translation;
        /* Core Graphics clamps cumulative translate. Use its reported value,
         * then test DG coordinates around that actual origin. */
        reset(c, INT_MAX, INT_MAX, -INT_MAX, -INT_MAX, W, H, 0);
        translation = nj_call_int(c->vm, c->g, NJ_C_GRAPHICS,
                                  "getTranslateX", "()I", NULL);
        a[0].i = a[1].i = -translation; a[2].i = W; a[3].i = H;
        call(c, "setClip", "(IIII)V", a);
        a[0].i = a[1].i = -translation + 2;
        a[2].i = -translation + 10; a[3].i = -translation + 2;
        a[4].i = -translation + 2; a[5].i = -translation + 10;
        a[6].i = (nj_s4)0x804b9e17u;
        call(c, "fillTriangle", "(IIIIIII)V", a);
        read_pixels(c, actual);
        for (y = 0; y < H; ++y) for (x = 0; x < W; ++x) {
            int covered = x >= 2 && y >= 2 && x + y < 12;
            nj_u4 expected = covered ? over(background(y * W + x), 0x804b9e17u)
                                     : background(y * W + x);
            if (actual[y * W + x] != expected) equal = 0;
        }
        CHECK(equal, "extreme translation applies once without narrowing intermediates");
        reset(c, 0, 0, 0, 0, W, H, 0);
    }
}

static void test_allocation_failure(canvas *c)
{
    nj_u4 mark = nj_root_mark(c->vm);
    nj_ref xs = nj_root(c->vm, nj_int_array_new(c->vm, 4));
    nj_ref ys = nj_root(c->vm, nj_int_array_new(c->vm, 4));
    static const int xv[] = {1, 12, 12, 1}, yv[] = {1, 1, 12, 12};
    int kind, i, tiny;
    /* Resolve the exception class before measuring host allocation balance. */
    nj_throw_oom(c->vm); nj_clear_exception(c->vm);
    for (tiny = 0; tiny < 2; ++tiny) for (kind = 0; kind < 4; ++kind) {
        const char *names[] = {"drawPolygon", "fillPolygon", "drawTriangle", "fillTriangle"};
        nj_value a[7];
        nj_status status;
        nj_u4 actual[W * H], roots;
        nj_u4 host;
        int state_before[9], state_after[9], equal = 1;
        void *probe;
        nj_size bytes = kind & 1 ? (nj_size)(kind < 2 ? 4 : 3) * sizeof(nj_s8)
                                : (tiny ? (3 * 3 + 7) / 8 : (12 * 12 + 7) / 8);
        for (i = 0; i < 4; ++i) {
            nj_array_set_int(xs, (nj_u4)i, tiny && xv[i] == 12 ? 3 : xv[i]);
            nj_array_set_int(ys, (nj_u4)i, tiny && yv[i] == 12 ? 3 : yv[i]);
        }
        reset(c, 0, 0, 0, 0, W, H, 0);
        state(c, state_before);
        if (kind < 2) {
            a[0].r = xs; a[1].i = 0; a[2].r = ys; a[3].i = 0;
            a[4].i = 4; a[5].i = (nj_s4)0x804931a7u;
        } else {
            for (i = 0; i < 3; ++i) {
                a[2 * i].i = tiny && xv[i] == 12 ? 3 : xv[i];
                a[2 * i + 1].i = tiny && yv[i] == 12 ? 3 : yv[i];
            }
            a[6].i = (nj_s4)0x804931a7u;
        }
        probe = nj_host_alloc(c->vm, bytes);
        CHECK(probe != NULL, "measure shape scratch allocation including host header");
        nj_host_free(c->vm, probe);
        fail_alloc_size = last_alloc_size; injected = 0;
        roots = nj_root_mark(c->vm); host = nj_host_live_count(c->vm);
        status = nj_call(c->vm, c->g, NJ_C_GRAPHICS, names[kind],
                         kind < 2 ? "([II[IIII)V" : "(IIIIIII)V", a, NULL);
        fail_alloc_size = 0;
        CHECK(injected == 1, "shape scratch failure injection fired");
        CHECK(status != NJ_OK && c->vm->exception && strcmp(
              nj_ref_class_name(c->vm->exception), "java/lang/OutOfMemoryError") == 0,
              "shape scratch failure reports OutOfMemoryError");
        nj_clear_exception(c->vm);
        CHECK(nj_host_live_count(c->vm) == host && nj_root_mark(c->vm) == roots,
              "scratch failure leaves no host allocation or temporary root");
        state(c, state_after);
        CHECK(memcmp(state_before, state_after, sizeof(state_before)) == 0,
              "scratch failure preserves Graphics state");
        read_pixels(c, actual);
        for (i = 0; i < W * H; ++i) if (actual[i] != background(i)) equal = 0;
        CHECK(equal, "scratch failure paints no partial shape");
        call(c, names[kind], kind < 2 ? "([II[IIII)V" : "(IIIIIII)V", a);
        CHECK(nj_host_live_count(c->vm) == host, "successful shape frees its scratch storage");
    }
    nj_root_release(c->vm, mark);
}

int main(int argc, char **argv)
{
    nj_port port;
    nj_vm *vm;
    nj_value a[2], r;
    canvas c;
    memset(&port, 0, sizeof(port));
    if (nj_port_headless(&port) != NJ_OK) return 2;
    base_alloc = port.alloc; port.alloc = fault_alloc;
    vm = nj_vm_create(&port, 4u * 1024u * 1024u);
    if (!vm) { nj_port_shutdown(&port); return 2; }
    a[0].i = W; a[1].i = H;
    CHECK(nj_call_static(vm, NJ_C_IMAGE, "createImage",
          "(II)Ljavax/microedition/lcdui/Image;", a, &r) == NJ_OK, "create image");
    c.vm = vm; c.image = nj_root(vm, r.r);
    CHECK(nj_call(vm, c.image, NJ_C_IMAGE, "getGraphics",
          "()Ljavax/microedition/lcdui/Graphics;", NULL, &r) == NJ_OK, "create Graphics");
    c.g = nj_root(vm, r.r);
    c.output = nj_root(vm, nj_int_array_new(vm, W * H + 2));
    if (argc == 1 || strcmp(argv[1], "--shapes") == 0) test_shapes(&c);
    if (argc == 1 || strcmp(argv[1], "--limits") == 0) test_limits(&c);
    if (argc == 1 || strcmp(argv[1], "--extreme") == 0) test_extreme(&c);
    if (argc == 1 || strcmp(argv[1], "--outline-extreme") == 0) test_outline_extreme(&c);
    if (argc == 1 || strcmp(argv[1], "--oom") == 0) test_allocation_failure(&c);
    CHECK(!nj_has_exception(vm), "no pending exception");
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    printf("DirectGraphics shapes: %u passed, %u failed\n", passed, failed);
    return failed ? 1 : 0;
}
