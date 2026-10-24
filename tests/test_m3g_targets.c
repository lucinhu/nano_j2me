/* Exercise the public M3G calls against port-owned, padded framebuffers.
 * ARGB8888 is the reference for opaque fragments; blended RGB565 fragments
 * use an already quantized destination, then quantize the reference result. */
#include "nj2me/nj.h"
#include "nj2me/nj_gfx.h"
#include "nj_internal.h"
#include "nj_midp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define C(x) "javax/microedition/m3g/" x
#define G "javax/microedition/lcdui/Graphics"
enum { W = 240, H = 320, GUARD = 16, PAD = 16 };
static unsigned checks;
/* Optional cross-build capture: color plus depth, excluding row padding. */
static FILE *capture;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
typedef struct target {
    nj_port port;
    nj_vm *vm;
    nj_ref g, g3d, vb, ib, app, bg, cm, image, positions, lengths, tex;
    nj_u1 *allocation;
    nj_surface surface;
    void *(*alloc)(void *, nj_size);
    void (*free)(void *, void *);
    unsigned outstanding;
} target;
static target *active[4];
static nj_s4 *depth(target *t);
static target *owner(void *ud)
{
    unsigned i;
    for (i = 0; i < 4; i++)
        if (active[i] && active[i]->port.ud == ud)
            return active[i];
    CHECK(0);
    return NULL;
}
static void *count_alloc(void *ud, nj_size n)
{
    target *t = owner(ud);
    void *p = t->alloc ? t->alloc(ud, n) : malloc(n);
    if (p)
        ++t->outstanding;
    return p;
}
static void count_free(void *ud, void *p)
{
    target *t = owner(ud);
    if (p) {
        CHECK(t->outstanding > 0);
        --t->outstanding;
    }
    if (t->free)
        t->free(ud, p);
    else
        free(p);
}
static void call(target *t, nj_ref o, const char *c, const char *n, const char *d, nj_value *a)
{
    CHECK(nj_call(t->vm, o, c, n, d, a, NULL) == NJ_OK);
}
static nj_ref make(target *t, const char *c)
{
    nj_ref o = nj_object_new(t->vm, c);
    CHECK(o);
    nj_root(t->vm, o);
    CHECK(nj_call_special(t->vm, o, c, "<init>", "()V", NULL, NULL) == NJ_OK);
    return o;
}
static void integer(target *t, nj_ref o, const char *c, const char *n, const char *d, int v)
{
    nj_value a;
    a.i = v;
    call(t, o, c, n, d, &a);
}
static nj_u2 pack(nj_u4 c)
{
    return (nj_u2)((((c >> 16) & 255u) >> 3) << 11 | ((((c >> 8) & 255u) >> 2) << 5) |
                   ((c & 255u) >> 3));
}
/* Match the runtime's bit-replication conversion, independently of packing. */
static nj_u4 replica(nj_u2 v)
{
    nj_u4 r = v >> 11, g = (v >> 5) & 63u, b = v & 31u;
    return 0xff000000u | ((r * 8u + r / 4u) << 16) | ((g * 4u + g / 16u) << 8) | (b * 8u + b / 4u);
}
static nj_u4 pixel(target *t, int x, int y)
{
    nj_u1 *p = (nj_u1 *)t->surface.pixels + y * t->surface.stride + x * t->surface.bpp;
    if (t->surface.format == NJ_PIXFMT_RGB565) {
        nj_u2 v;
        memcpy(&v, p, 2);
        return replica(v);
    }
    if (t->surface.format == NJ_PIXFMT_RGB888)
        return 0xff000000u | ((nj_u4)p[0] << 16) | ((nj_u4)p[1] << 8) | p[2];
    {
        nj_u4 v;
        memcpy(&v, p, 4);
        return v;
    }
}
static void fill(target *t, nj_u4 colour)
{
    int x, y;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            nj_u1 *p = (nj_u1 *)t->surface.pixels + y * t->surface.stride + x * t->surface.bpp;
            if (t->surface.format == NJ_PIXFMT_RGB565) {
                nj_u2 v = pack(colour);
                memcpy(p, &v, 2);
            } else if (t->surface.format == NJ_PIXFMT_RGB888) {
                p[0] = (nj_u1)(colour >> 16);
                p[1] = (nj_u1)(colour >> 8);
                p[2] = (nj_u1)colour;
            } else
                memcpy(p, &colour, 4);
        }
}
static void guards(target *t)
{
    int i, y;
    size_t size = (size_t)t->surface.stride * H;
    for (i = 0; i < GUARD; i++) {
        CHECK(t->allocation[i] == 0xa5);
        CHECK(t->allocation[GUARD + size + i] == 0xa5);
    }
    for (y = 0; y < H; y++)
        for (i = W * t->surface.bpp; i < t->surface.stride; i++)
            CHECK(((nj_u1 *)t->surface.pixels)[y * t->surface.stride + i] == 0xa5);
}
static void compare(target *t, target *ref)
{
    int x, y;
    unsigned bad = 0;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            nj_u4 want = pixel(ref, x, y), got = pixel(t, x, y);
            if (t->surface.format == NJ_PIXFMT_RGB565)
                want = replica(pack(want));
            else if (t->surface.format == NJ_PIXFMT_RGB888)
                want |= 0xff000000u;
            if (got != want) {
                if (!bad)
                    fprintf(stderr, "pixel %d,%d got=%08x want=%08x\n", x, y, got, want);
                ++bad;
            }
        }
    CHECK(bad == 0);
    if (capture) {
        target *both[2] = {t, ref};
        int i;
        for (i = 0; i < 2; i++) {
            target *c = both[i];
            for (y = 0; y < H; y++)
                CHECK(fwrite((nj_u1 *)c->surface.pixels + y * c->surface.stride,
                             c->surface.bpp, W, capture) == W);
            CHECK(fwrite(depth(c), sizeof(nj_s4), W * H, capture) == W * H);
        }
    }
    guards(t);
    guards(ref);
}
static void viewport(target *t, int x, int y, int w, int h)
{
    nj_value a[4];
    a[0].i = x;
    a[1].i = y;
    a[2].i = w;
    a[3].i = h;
    call(t, t->g3d, C("Graphics3D"), "setViewport", "(IIII)V", a);
}
static void bind(target *t)
{
    nj_value a;
    a.r = t->g;
    call(t, t->g3d, C("Graphics3D"), "bindTarget", "(Ljava/lang/Object;)V", &a);
}
static void clear(target *t)
{
    nj_value a;
    a.r = t->bg;
    call(t, t->g3d, C("Graphics3D"), "clear", "(L" C("Background") ";)V", &a);
}
static void geometry(target *t, int z, int length)
{
    nj_s2 p[12] = {-4, -4, 0, 4, -4, 0, -4, 4, 0, 4, 4, 0};
    int k;
    for (k = 0; k < 4; k++)
        p[k * 3 + 2] = (nj_s2)z;
    memcpy(nj_array_data(t->positions), p, sizeof(p));
    ((nj_s4 *)nj_array_data(t->lengths))[0] = length;
}
static void texture(target *t, nj_u4 left, nj_u4 right)
{
    nj_value a[5];
    nj_ref data = nj_array_new(t->vm, "B", 8);
    nj_u1 *p = (nj_u1 *)nj_array_data(data);
    int k;
    CHECK(data);
    for (k = 0; k < 2; k++) {
        nj_u4 c = k ? right : left;
        p[k * 4] = (nj_u1)(c >> 16);
        p[k * 4 + 1] = (nj_u1)(c >> 8);
        p[k * 4 + 2] = (nj_u1)c;
        p[k * 4 + 3] = (nj_u1)(c >> 24);
    }
    a[0].i = 0;
    a[1].i = 0;
    a[2].i = 2;
    a[3].i = 1;
    a[4].r = data;
    call(t, t->image, C("Image2D"), "set", "(IIII[B)V", a);
}
static void render(target *t)
{
    nj_value a[5];
    nj_u8 before = t->vm->gfx_calls;
    a[0].r = t->vb;
    a[1].r = t->ib;
    a[2].r = t->app;
    a[3].r = NULL;
    a[4].i = 0;
    call(t, t->g3d, C("Graphics3D"), "render",
         "(L" C("VertexBuffer") ";L" C("IndexBuffer") ";L" C("Appearance") ";L" C(
             "Transform") ";I)V",
         a);
    CHECK(t->vm->gfx_calls == before); /* no per-triangle Java Graphics fallback */
}
static nj_s4 *depth(target *t)
{
    nj_ref z = nj_rget(t->vm, t->g3d, nj_field_lookup(t->vm, C("Graphics3D"), "_nj_zbuf", "[I", 0));
    CHECK(z && nj_array_length(z) == W * H);
    return (nj_s4 *)nj_array_data(z);
}
static void init(target *t, int format)
{
    nj_value a[6];
    nj_ref va, tc, tcdata, poly, camera, tex;
    int k;
    memset(t, 0, sizeof(*t));
    CHECK(nj_port_headless_ex(&t->port, W, H, 0, NULL) == NJ_OK);
    t->alloc = t->port.alloc;
    t->free = t->port.free;
    for (k = 0; k < 4; k++)
        if (!active[k]) {
            active[k] = t;
            break;
        }
    CHECK(k < 4);
    t->port.alloc = count_alloc;
    t->port.free = count_free;
    t->vm = nj_vm_create(&t->port, 8 * 1024 * 1024);
    CHECK(t->vm);
    CHECK(nj_lib_late_init(t->vm) == NJ_OK);
    t->surface.format = format;
    t->surface.width = W;
    t->surface.height = H;
    t->surface.bpp = format == NJ_PIXFMT_RGB565 ? 2 : format == NJ_PIXFMT_RGB888 ? 3 : 4;
    t->surface.stride = W * t->surface.bpp + PAD;
    t->allocation = malloc(GUARD + (size_t)t->surface.stride * H + GUARD);
    CHECK(t->allocation);
    memset(t->allocation, 0xa5, GUARD + (size_t)t->surface.stride * H + GUARD);
    t->surface.pixels = t->allocation + GUARD;
    t->vm->screen = t->surface;
    t->vm->screen_w = W;
    t->vm->screen_h = H;
    t->g = nj_object_new(t->vm, G);
    CHECK(t->g);
    nj_root(t->vm, t->g);
    nj_iput(t->vm, t->g, nj_field_lookup(t->vm, G, "gClipW", "I", 0), W);
    nj_iput(t->vm, t->g, nj_field_lookup(t->vm, G, "gClipH", "I", 0), H);
    t->g3d = make(t, C("Graphics3D"));
    t->vb = make(t, C("VertexBuffer"));
    t->app = make(t, C("Appearance"));
    t->bg = make(t, C("Background"));
    t->cm = make(t, C("CompositingMode"));
    poly = make(t, C("PolygonMode"));
    camera = make(t, C("Camera"));
    integer(t, t->bg, C("Background"), "setColorClearEnable", "(Z)V", 1);
    integer(t, t->bg, C("Background"), "setDepthClearEnable", "(Z)V", 1);
    a[0].r = t->cm;
    call(t, t->app, C("Appearance"), "setCompositingMode", "(L" C("CompositingMode") ";)V", a);
    integer(t, poly, C("PolygonMode"), "setCulling", "(I)V", 162);
    a[0].r = poly;
    call(t, t->app, C("Appearance"), "setPolygonMode", "(L" C("PolygonMode") ";)V", a);
    t->positions = nj_array_new(t->vm, "S", 12);
    CHECK(t->positions);
    nj_root(t->vm, t->positions);
    va = nj_object_new(t->vm, C("VertexArray"));
    CHECK(va);
    a[0].i = 4;
    a[1].i = 3;
    a[2].i = 2;
    CHECK(nj_call_special(t->vm, va, C("VertexArray"), "<init>", "(III)V", a, NULL) == NJ_OK);
    a[0].i = 0;
    a[1].i = 4;
    a[2].r = t->positions;
    call(t, va, C("VertexArray"), "set", "(II[S)V", a);
    t->positions =
        nj_rget(t->vm, va, nj_field_lookup(t->vm, C("VertexArray"), "_nj_data", "[S", 0));
    a[0].r = va;
    a[1].f = .5f;
    a[2].r = NULL;
    call(t, t->vb, C("VertexBuffer"), "setPositions", "(L" C("VertexArray") ";F[F)V", a);
    tc = nj_object_new(t->vm, C("VertexArray"));
    CHECK(tc);
    a[0].i = 4;
    a[1].i = 2;
    a[2].i = 2;
    CHECK(nj_call_special(t->vm, tc, C("VertexArray"), "<init>", "(III)V", a, NULL) == NJ_OK);
    tcdata = nj_array_new(t->vm, "S", 8);
    CHECK(tcdata);
    for (k = 0; k < 4; k++) {
        ((nj_s2 *)nj_array_data(tcdata))[k * 2] = (nj_s2)((k & 1) * 64);
        ((nj_s2 *)nj_array_data(tcdata))[k * 2 + 1] = (nj_s2)((k >> 1) * 64);
    }
    a[0].i = 0;
    a[1].i = 4;
    a[2].r = tcdata;
    call(t, tc, C("VertexArray"), "set", "(II[S)V", a);
    a[0].i = 0;
    a[1].r = tc;
    a[2].f = 1.f / 64;
    a[3].r = NULL;
    call(t, t->vb, C("VertexBuffer"), "setTexCoords", "(IL" C("VertexArray") ";F[F)V", a);
    t->lengths = nj_array_new(t->vm, "I", 1);
    CHECK(t->lengths);
    ((nj_s4 *)nj_array_data(t->lengths))[0] = 4;
    t->ib = nj_object_new(t->vm, C("TriangleStripArray"));
    CHECK(t->ib);
    a[0].i = 0;
    a[1].r = t->lengths;
    CHECK(nj_call_special(t->vm, t->ib, C("TriangleStripArray"), "<init>", "(I[I)V", a, NULL) ==
          NJ_OK);
    t->lengths = nj_rget(t->vm, t->ib,
                         nj_field_lookup(t->vm, C("TriangleStripArray"), "_nj_lengths", "[I", 0));
    t->image = nj_object_new(t->vm, C("Image2D"));
    CHECK(t->image);
    a[0].i = 100;
    a[1].i = 2;
    a[2].i = 1;
    CHECK(nj_call_special(t->vm, t->image, C("Image2D"), "<init>", "(III)V", a, NULL) == NJ_OK);
    tex = nj_object_new(t->vm, C("Texture2D"));
    CHECK(tex);
    t->tex = tex;
    a[0].r = t->image;
    CHECK(nj_call_special(t->vm, tex, C("Texture2D"), "<init>", "(L" C("Image2D") ";)V", a, NULL) ==
          NJ_OK);
    integer(t, tex, C("Texture2D"), "setBlending", "(I)V", 228);
    a[0].i = 240;
    a[1].i = 240;
    call(t, tex, C("Texture2D"), "setWrapping", "(II)V", a);
    a[0].i = 208;
    a[1].i = 208;
    call(t, tex, C("Texture2D"), "setFiltering", "(II)V", a);
    a[0].i = 0;
    a[1].r = tex;
    call(t, t->app, C("Appearance"), "setTexture", "(IL" C("Texture2D") ";)V", a);
    a[0].f = 4;
    a[1].f = 1;
    a[2].f = 1;
    a[3].f = 10;
    call(t, camera, C("Camera"), "setParallel", "(FFFF)V", a);
    a[0].r = camera;
    a[1].r = NULL;
    call(t, t->g3d, C("Graphics3D"), "setCamera", "(L" C("Camera") ";L" C("Transform") ";)V", a);
    bind(t);
    geometry(t, -4, 4);
    texture(t, 0xffff0000u, 0xff00ff00u);
}
static void destroy(target *t)
{
    unsigned i;
    (void)nj_vm_destroy(t->vm);
    CHECK(t->outstanding == 0);
    nj_port_shutdown(&t->port);
    free(t->allocation);
    for (i = 0; i < 4; i++)
        if (active[i] == t)
            active[i] = NULL;
}
static void cases(int format)
{
    target t, r;
    int mode, x, y;
    nj_u4 background = replica(pack(0xff355b83u));
    init(&t, format);
    init(&r, NJ_PIXFMT_ARGB8888);
    fill(&t, background);
    fill(&r, background);
    clear(&t);
    clear(&r);
    render(&t);
    render(&r);
    CHECK((pixel(&t, 60, 160) & 0xffffffu) == 0xff0000u);
    CHECK((pixel(&t, 180, 160) & 0xffffffu) == 0x00ff00u);
    compare(&t, &r);
    geometry(&t, -6, 4);
    geometry(&r, -6, 4);
    texture(&t, 0xff0000ffu, 0xffffff00u);
    texture(&r, 0xff0000ffu, 0xffffff00u);
    render(&t);
    render(&r);
    CHECK((pixel(&t, 60, 160) & 0xffffffu) == 0xff0000u);
    compare(&t, &r);
    for (mode = 64; mode <= 68; mode++) {
        fill(&t, background);
        fill(&r, background);
        geometry(&t, -4, 3);
        geometry(&r, -4, 3);
        texture(&t, 0x75c15329u, 0x75c15329u);
        texture(&r, 0x75c15329u, 0x75c15329u);
        integer(&t, t.cm, C("CompositingMode"), "setBlending", "(I)V", mode);
        integer(&r, r.cm, C("CompositingMode"), "setBlending", "(I)V", mode);
        integer(&t, t.bg, C("Background"), "setColorClearEnable", "(Z)V", 0);
        integer(&r, r.bg, C("Background"), "setColorClearEnable", "(Z)V", 0);
        clear(&t);
        clear(&r);
        render(&t);
        render(&r);
        compare(&t, &r);
    }
    {
        nj_value a;
        a.f = .9f;
        call(&t, t.cm, C("CompositingMode"), "setAlphaThreshold", "(F)V", &a);
        call(&r, r.cm, C("CompositingMode"), "setAlphaThreshold", "(F)V", &a);
        fill(&t, background);
        fill(&r, background);
        clear(&t);
        clear(&r);
        render(&t);
        render(&r);
        compare(&t, &r);
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++)
                CHECK(depth(&t)[y * W + x] == 0);
        a.f = 0;
        call(&t, t.cm, C("CompositingMode"), "setAlphaThreshold", "(F)V", &a);
        call(&r, r.cm, C("CompositingMode"), "setAlphaThreshold", "(F)V", &a);
    }
    /* Flat nearest spans also handle tint, replace/decal, mixed wrap axes,
     * alpha rejection, destination alpha and all compositing modes. Use one
     * triangle over a quantized destination so RGB565 rounding is independent
     * of overdraw. Captures can be compared with NJ_M3G_FAST_PATHS=0. */
    for (mode = 0; mode < 20; mode++) {
        target *both[2] = {&t, &r};
        int i;
        for (i = 0; i < 2; i++) {
            target *c = both[i];
            nj_value a[2];
            fill(c, background);
            geometry(c, -4, 3);
            texture(c, mode >= 10 ? 0xffc15329u : 0x75c15329u,
                       mode >= 10 ? 0xff348159u : 0xe7348159u);
            integer(c, c->vb, C("VertexBuffer"), "setDefaultColor", "(I)V", 0xff7f9bcd);
            integer(c, c->tex, C("Texture2D"), "setBlending", "(I)V", 225 + mode / 5);
            integer(c, c->cm, C("CompositingMode"), "setBlending", "(I)V", 64 + mode % 5);
            integer(c, c->cm, C("CompositingMode"), "setAlphaWriteEnable", "(Z)V", mode & 1);
            a[0].i = 240 + (mode & 1); a[1].i = 240 + ((mode >> 1) & 1);
            call(c, c->tex, C("Texture2D"), "setWrapping", "(II)V", a);
            a[0].f = mode & 2 ? .5f : 0.f;
            call(c, c->cm, C("CompositingMode"), "setAlphaThreshold", "(F)V", a);
            clear(c);
            render(c);
        }
        compare(&t, &r);
        CHECK(memcmp(depth(&t), depth(&r), W * H * sizeof(nj_s4)) == 0);
    }
    {
        target *both[2] = {&t, &r};
        int i;
        for (i = 0; i < 2; i++) {
            target *c = both[i]; nj_value a;
            a.f = 0;
            call(c, c->cm, C("CompositingMode"), "setAlphaThreshold", "(F)V", &a);
            integer(c, c->cm, C("CompositingMode"), "setAlphaWriteEnable", "(Z)V", 1);
            integer(c, c->cm, C("CompositingMode"), "setBlending", "(I)V", 68);
            integer(c, c->tex, C("Texture2D"), "setBlending", "(I)V", 228);
        }
    }
    integer(&t, t.cm, C("CompositingMode"), "setDepthWriteEnable", "(Z)V", 0);
    integer(&r, r.cm, C("CompositingMode"), "setDepthWriteEnable", "(Z)V", 0);
    clear(&t);
    clear(&r);
    render(&t);
    render(&r);
    CHECK(depth(&t)[160 * W + 60] == 0);
    compare(&t, &r);
    integer(&t, t.cm, C("CompositingMode"), "setDepthWriteEnable", "(Z)V", 1);
    integer(&r, r.cm, C("CompositingMode"), "setDepthWriteEnable", "(Z)V", 1);
    {
        nj_value a[4];
        target *both[2] = {&t, &r};
        int i;
        for (i = 0; i < 2; i++) {
            target *c = both[i];
            fill(c, background);
            geometry(c, -4, 4);
            texture(c, 0xffff0000u, 0xff00ff00u);
            a[0].i = 13;
            a[1].i = 17;
            call(c, c->g, G, "translate", "(II)V", a);
            a[0].i = 9;
            a[1].i = 11;
            a[2].i = 130;
            a[3].i = 180;
            call(c, c->g, G, "setClip", "(IIII)V", a);
            bind(c);
            viewport(c, -13, -17, W, H);
            clear(c);
            render(c);
        }
        compare(&t, &r);
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++)
                if (x < 22 || x >= 152 || y < 28 || y >= 208)
                    CHECK(pixel(&t, x, y) == background);
        viewport(&t, -40, -30, 280, 350);
        viewport(&r, -40, -30, 280, 350);
        clear(&t);
        clear(&r);
        render(&t);
        render(&r);
        compare(&t, &r);
        for (i = 0; i < 2; i++) {
            target *c = both[i];
            a[0].i = 0;
            a[1].i = 0;
            a[2].i = 0;
            a[3].i = 0;
            call(c, c->g, G, "setClip", "(IIII)V", a);
            bind(c);
            viewport(c, 0, 0, W, H);
            fill(c, background);
            clear(c);
            render(c);
        }
        compare(&t, &r);
        CHECK(pixel(&t, 60, 160) == background);
    }
    /* Clear one viewport without destroying depth belonging to its neighbour. */
    {
        target *both[2] = {&t, &r};
        int i;
        for (i = 0; i < 2; i++) {
            target *c = both[i];
            nj_value a[4];
            a[0].i = -13;
            a[1].i = -17;
            call(c, c->g, G, "translate", "(II)V", a);
            a[0].i = 0;
            a[1].i = 0;
            a[2].i = W;
            a[3].i = H;
            call(c, c->g, G, "setClip", "(IIII)V", a);
            bind(c);
            viewport(c, 0, 0, W, H);
            geometry(c, -4, 4);
            fill(c, background);
            clear(c);
            render(c);
            CHECK(depth(c)[160 * W + 180] > 0);
            viewport(c, 0, 0, W / 2, H);
            clear(c);
            CHECK(depth(c)[160 * W + 60] == 0);
            CHECK(depth(c)[160 * W + 180] > 0);
            geometry(c, -6, 4);
            render(c);
            CHECK(depth(c)[160 * W + 60] > 0);
        }
        compare(&t, &r);
    }
    /* Background images use the same format, row stride and clipped viewport. */
    {
        target *both[2] = {&t, &r};
        int i;
        for (i = 0; i < 2; i++) {
            target *c = both[i];
            nj_value a[4];
            viewport(c, 0, 0, W, H);
            texture(c, 0xffff0000u, 0xff00ff00u);
            a[0].r = c->image;
            call(c, c->bg, C("Background"), "setImage", "(L" C("Image2D") ";)V", a);
            a[0].i = 0;
            a[1].i = 0;
            a[2].i = 2;
            a[3].i = 1;
            call(c, c->bg, C("Background"), "setCrop", "(IIII)V", a);
            integer(c, c->bg, C("Background"), "setColorClearEnable", "(Z)V", 1);
            fill(c, background);
            clear(c);
            CHECK((pixel(c, 60, 160) & 0xffffffu) == 0xff0000u);
            CHECK((pixel(c, 180, 160) & 0xffffffu) == 0x00ff00u);
        }
        compare(&t, &r);
    }
    guards(&t);
    guards(&r);
    destroy(&t);
    destroy(&r);
    printf("PASS native M3G target format=%d: texture, depth, blend modes, alpha test, "
           "translated/empty clip, viewport and padded stride\n",
           format);
}
int main(int argc, char **argv)
{
    CHECK(argc <= 2);
    if (argc == 2) { capture = fopen(argv[1], "wb"); CHECK(capture); }
    cases(NJ_PIXFMT_RGB565);
    cases(NJ_PIXFMT_RGB888);
    cases(NJ_PIXFMT_ARGB8888);
    if (capture) CHECK(fclose(capture) == 0);
    printf("PASS M3G native target checks=%u\n", checks);
    return 0;
}
