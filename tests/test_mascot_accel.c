/* Exact Mascot DDA coverage -> optional fragment merge. The accepting backend
 * here is an independent CPU implementation of the packet protocol: these
 * tests do not claim that a GPU was exercised. */
#include "nj2me/nj.h"
#include "nj_mascot_raster.h"
#ifndef NJ_MASCOT_ACCEL_STANDALONE
#include "nj_internal.h"
#include "nj_midp.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, cases;
#define CHECK(c) do { ++checks; if (!(c)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); \
} } while (0)
#define ACTIVE (NJ_ENABLE_GFX_ACCEL && NJ_USE_LIBC)
#define CAP 640

typedef struct probe {
    unsigned calls, accepted;
    int result, check_color, check_untouched;
    nj_u4 color;
    const void *pixels;
    const unsigned char *before;
    size_t size;
} probe;

static void put_pixel(nj_surface *s, int x, int y, nj_u4 color)
{
    unsigned char *p = (unsigned char *)s->pixels + y*s->stride + x*s->bpp;
    if (s->format == NJ_PIXFMT_RGB565) {
        unsigned v = ((color >> 8) & 0xf800u) | ((color >> 5) & 0x7e0u)
                   | ((color >> 3) & 31u);
        p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    } else if (s->format == NJ_PIXFMT_RGB888) {
        p[0] = (unsigned char)(color >> 16); p[1] = (unsigned char)(color >> 8);
        p[2] = (unsigned char)color;
    } else {
        p[0] = (unsigned char)color; p[1] = (unsigned char)(color >> 8);
        p[2] = (unsigned char)(color >> 16); p[3] = (unsigned char)(color >> 24);
    }
}

static int merge(void *ud, const nj_gfx_raster *r, nj_u4 *drawn)
{
    probe *p = (probe *)ud;
    int x, y;
    CHECK(p != NULL && r != NULL && drawn != NULL);
    ++p->calls;
    CHECK(r->version == NJ_GFX_RASTER_VERSION);
    CHECK(r->surface && r->surface->pixels == p->pixels);
    CHECK(r->depth == NULL && r->depth_stride == 0);
    CHECK(!r->depth_test && !r->depth_write);
    CHECK(r->x >= 0 && r->y >= 0 && r->width > 0 && r->height > 0);
    CHECK(r->x + r->width <= r->surface->width);
    CHECK(r->y + r->height <= r->surface->height);
    CHECK(r->keys && r->key_stride == r->width);
    CHECK((r->color >> 24) == 255);
    if (p->check_color) CHECK(r->color == p->color);
    if (p->check_untouched)
        CHECK(memcmp(r->surface->pixels, p->before, p->size) == 0);
    for (y = 0; y < r->height; ++y) for (x = 0; x < r->width; ++x)
        CHECK(r->keys[y*r->key_stride + x] == 0 || r->keys[y*r->key_stride + x] == 1);
    if (p->result != NJ_GFX_ACCEL_DONE) return p->result;
    *drawn = 0;
    for (y = 0; y < r->height; ++y) for (x = 0; x < r->width; ++x) {
        if (r->keys[y*r->key_stride + x]) {
            put_pixel(r->surface, r->x+x, r->y+y, r->color);
            *drawn = 1;
        }
    }
    CHECK(*drawn == 1); /* Empty coverage must never reach the backend. */
    ++p->accepted;
    return NJ_GFX_ACCEL_DONE;
}

static nj_u4 flat_color(nj_u4 rgb, int light)
{
    unsigned factor, result = 0xff000000u;
    int shift;
    if (light < 0) light = 0;
    if (light > 4095) light = 4095;
    factor = (unsigned)(light >> 7) + 1;
    for (shift = 0; shift <= 16; shift += 8)
        result |= ((((rgb >> shift) & 255u) * factor) / 32u) << shift;
    return result;
}

static void direct_matrix(void)
{
    static const int geometry[][6] = {
        {1,1, 1,5, 4,5}, {-9,-5, 18,2, 2,18}, {9,1, 2,7, 8,8},
        {2,1, 9,1, 4,8}, {1,7, 9,7, 6,0}, {3,1, 3,5, 3,8},
        {-8,-6, -2,-3, -1,-1}, {1,4, 5,4, 9,4},
        {-32768,-32768, 32767,2, 2,32767}
    };
    static const int order[6][3] = {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    static const int light[] = {-20,0,3,2048,4095,5000};
    static const int result[] = {NJ_GFX_ACCEL_DONE,NJ_GFX_ACCEL_DECLINE,-5,2};
    static const int format[] = {NJ_PIXFMT_RGB565,NJ_PIXFMT_RGB888,NJ_PIXFMT_ARGB8888};
    unsigned a, b, c, d, e, f, clip;
    unsigned char want[CAP], got[CAP], before[CAP];
    unsigned observed = 0;
    for (a = 0; a < 3; ++a) for (b = 0; b < sizeof geometry/sizeof geometry[0]; ++b)
    for (c = 0; c < 6; ++c) for (d = 0; d < 6; ++d) for (e = 0; e < 2; ++e)
    for (f = 0; f < 4; ++f) for (clip = 0; clip < 2; ++clip) {
        nj_surface sw, hw;
        nj_mascot_vertex v[3], saved[3];
        nj_mascot_material m;
        probe p;
        int k, stride = 11*(int)(a+2)+7;
        memset(&sw, 0, sizeof sw); sw.pixels = want+13;
        sw.width = 11; sw.height = 9; sw.stride = stride;
        sw.format = format[a]; sw.bpp = (int)a+2;
        hw = sw; hw.pixels = got+13;
        for (k = 0; k < CAP; ++k) before[k] = (unsigned char)(k*17+31);
        memcpy(want, before, CAP); memcpy(got, before, CAP);
        memset(v, 0, sizeof v); memset(&m, 0, sizeof m); memset(&p, 0, sizeof p);
        for (k = 0; k < 3; ++k) {
            int at = order[c][k];
            v[k].x = geometry[b][at*2]; v[k].y = geometry[b][at*2+1];
            v[k].light = k ? 17+k : light[d];
        }
        memcpy(saved, v, sizeof v);
        m.color = 0x3919e3a7u; m.flags = e ? NJ_MR_SHADED : 0;
        p.result = result[f]; p.check_color = p.check_untouched = 1;
        p.color = flat_color(m.color, light[d]); p.pixels = hw.pixels;
        p.before = before+13; p.size = (size_t)stride*9;
        CHECK(nj_mascot_raster_draw(&sw, clip?2:0, clip?1:0, clip?9:11, clip?7:9, v, &m) == NJ_MR_OK);
        CHECK(nj_mascot_raster_draw_accel(&hw, clip?2:0, clip?1:0, clip?9:11, clip?7:9,
                                         v, &m, merge, &p) == NJ_MR_OK);
        CHECK(memcmp(want, got, CAP) == 0);
        CHECK(memcmp(v, saved, sizeof v) == 0);
        CHECK(p.calls <= (unsigned)ACTIVE);
        observed += p.calls;
        ++cases;
    }
    CHECK(ACTIVE ? observed > 1000 : observed == 0);
}

static void counterexample_and_fallback(void)
{
    nj_u4 pixels[36], expected[36];
    nj_surface s;
    nj_mascot_vertex v[3];
    nj_mascot_material m;
    probe p;
    unsigned k, mode;
    static const int flags[] = {NJ_MR_LIGHTING,NJ_MR_ENVMAP,NJ_MR_TEXTURED,NJ_MR_SHADED};
    memset(&s, 0, sizeof s); s.pixels = pixels; s.width = s.height = 6;
    s.stride = 24; s.format = NJ_PIXFMT_ARGB8888; s.bpp = 4;
    memset(v, 0, sizeof v); v[0].x = v[1].x = 1; v[0].y = 1;
    v[1].y = v[2].y = 5; v[2].x = 4; v[0].light = 4095;
    memset(&m, 0, sizeof m); m.color = 0xabc345;
    memset(&p, 0, sizeof p); p.pixels = pixels; p.result = NJ_GFX_ACCEL_DONE;
    for (k = 0; k < 36; ++k) expected[k] = pixels[k] = 0xff102030u;
    expected[3*6+1] = expected[4*6+1] = expected[4*6+2] = 0xffabc345u;
    CHECK(nj_mascot_raster_draw_accel(&s,0,0,6,6,v,&m,merge,&p) == NJ_MR_OK);
    CHECK(memcmp(pixels, expected, sizeof pixels) == 0);
    CHECK(p.calls == (unsigned)ACTIVE); /* (1,2) is strictly inside a GL triangle but uncovered here. */
    for (mode = 0; mode < 4; ++mode) {
        nj_s4 status;
        nj_surface sw = s;
        memset(pixels, 0x51, sizeof pixels); memcpy(expected, pixels, sizeof pixels);
        sw.pixels = expected; p.calls = 0;
        m.flags = flags[mode]; m.toon = mode == 3 ? NJ_MR_TOON_PACK(1,50,200,40) : 0;
        status = nj_mascot_raster_draw(&sw,0,0,6,6,v,&m);
        CHECK(nj_mascot_raster_draw_accel(&s,0,0,6,6,v,&m,merge,&p) == status);
        CHECK(p.calls == 0 && memcmp(pixels, expected, sizeof pixels) == 0);
    }
    m.flags = 0; m.toon = 1; /* Toon word without lighting/shading is ignored. */
    for (mode = 1; mode < 4; ++mode) {
        nj_surface sw = s; sw.pixels = expected; m.blend = (int)mode; p.calls = 0;
        memset(pixels, 0x51, sizeof pixels); memcpy(expected, pixels, sizeof pixels);
        CHECK(nj_mascot_raster_draw(&sw,0,0,6,6,v,&m) == NJ_MR_OK);
        CHECK(nj_mascot_raster_draw_accel(&s,0,0,6,6,v,&m,merge,&p) == NJ_MR_OK);
        CHECK(p.calls == 0 && memcmp(pixels, expected, sizeof pixels) == 0);
    }
    m.blend = 0; p.calls = 0;
    CHECK(nj_mascot_raster_draw_accel(&s,0,0,0,6,v,&m,merge,&p) == NJ_MR_OK);
    CHECK(p.calls == 0);
    CHECK(nj_mascot_raster_draw_accel(NULL,0,0,6,6,v,&m,merge,&p) == NJ_MR_ERR_ARG);
    CHECK(nj_mascot_raster_draw_accel(&s,0,0,6,6,NULL,&m,merge,&p) == NJ_MR_ERR_ARG);
    CHECK(nj_mascot_raster_draw_accel(&s,0,0,6,6,v,NULL,merge,&p) == NJ_MR_ERR_ARG);
    CHECK(nj_mascot_raster_draw_accel(&s,0,0,6,6,v,&m,NULL,NULL) == NJ_MR_OK);
}

static void store_u32(unsigned char *p, nj_u4 v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static void texture_and_scratch_fallback(void)
{
    unsigned char blob[1052], saved[1052];
    nj_u4 got[36], expected[36];
    nj_surface s, sw;
    nj_mascot_vertex v[3];
    nj_mascot_material m;
    probe p;
    int mode, i;
    /* An immutable, validated 2x2 indexed texture; colorkey is INDEX zero,
     * even though its palette colour is green rather than black. */
    memset(blob,0,sizeof blob); store_u32(blob,NJ_MR_BLOB_MAGIC);
    blob[4] = blob[6] = blob[12] = blob[14] = 2;
    blob[8] = blob[9] = 1; blob[10] = NJ_MR_TEX_MODEL;
    store_u32(blob+16,1048); store_u32(blob+20,4);
    store_u32(blob+24,0xff28ce59u); store_u32(blob+28,0xffea4c72u);
    blob[1049] = blob[1050] = 1; memcpy(saved,blob,sizeof blob);
    memset(&s,0,sizeof s); s.pixels = got; s.width = s.height = 6;
    s.stride = 24; s.format = NJ_PIXFMT_ARGB8888; s.bpp = 4; sw = s; sw.pixels = expected;
    memset(v,0,sizeof v); v[0].x = v[1].x = 1; v[0].y = 1;
    v[1].y = v[2].y = 5; v[2].x = 5;
    v[1].v = v[2].u = 512;
    memset(&m,0,sizeof m); m.texture = blob; m.texture_len = sizeof blob;
    memset(&p,0,sizeof p); p.pixels = got; p.result = NJ_GFX_ACCEL_DONE;
    for (mode = 0; mode < 4; ++mode) {
        m.flags = NJ_MR_TEXTURED | ((mode&1) ? NJ_MR_COLORKEY : 0);
        m.blend = mode>>1;
        for (i = 0; i < 36; ++i) got[i] = expected[i] = 0xff234567u;
        CHECK(nj_mascot_raster_draw(&sw,0,0,6,6,v,&m) == NJ_MR_OK);
        CHECK(nj_mascot_raster_draw_accel(&s,0,0,6,6,v,&m,merge,&p) == NJ_MR_OK);
        CHECK(memcmp(got,expected,sizeof got) == 0 && p.calls == 0);
        CHECK(memcmp(blob,saved,sizeof blob) == 0);
    }
    /* A nonempty triangle exceeding the 1M-pixel scratch cap must be software,
     * with no callback and no loss of pixels at the cap boundary. */
    {
        size_t bytes = (size_t)1050*1000*4;
        unsigned char *large = (unsigned char *)malloc(bytes);
        unsigned char *reference_pixels = (unsigned char *)malloc(bytes);
        CHECK(large && reference_pixels);
        memset(large,0x35,bytes); memset(reference_pixels,0x35,bytes);
        s.pixels = large; s.width = 1050; s.height = 1000; s.stride = 4200;
        sw = s; sw.pixels = reference_pixels;
        v[0].x = v[0].y = v[1].x = v[2].y = 0;
        v[1].y = 1000; v[2].x = 1050; v[0].light = 4095;
        memset(&m,0,sizeof m); m.color = 0x28b75e;
        p.pixels = large; p.calls = 0;
        CHECK(nj_mascot_raster_draw(&sw,0,0,1050,1000,v,&m) == NJ_MR_OK);
        CHECK(nj_mascot_raster_draw_accel(&s,0,0,1050,1000,v,&m,merge,&p) == NJ_MR_OK);
        CHECK(p.calls == 0 && memcmp(large,reference_pixels,bytes) == 0);
        free(reference_pixels); free(large);
    }
}

#ifndef NJ_MASCOT_ACCEL_STANDALONE
#if NJ_ENABLE_MASCOT
#define G3D "com/mascotcapsule/micro3d/v3/Graphics3D"
#define FL "com/mascotcapsule/micro3d/v3/FigureLayout"
#define FX "com/mascotcapsule/micro3d/v3/Effect3D"
static nj_value integer(int n) { nj_value v; v.bits = 0; v.i = n; return v; }
static nj_value reference(nj_ref r) { nj_value v; v.bits = 0; v.r = r; return v; }
static void invoke(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                   const char *desc, nj_value *a)
{ CHECK(nj_call(vm,o,cls,name,desc,a,NULL) == NJ_OK); }
static nj_ref make(nj_vm *vm, const char *cls)
{
    nj_ref r = nj_root(vm,nj_object_new(vm,cls)); CHECK(r != NULL);
    CHECK(nj_call_special(vm,r,cls,"<init>","()V",NULL,NULL) == NJ_OK); return r;
}
static nj_ref ints(nj_vm *vm, const nj_s4 *data, unsigned n)
{
    nj_ref r = nj_root(vm,nj_int_array_new(vm,n)); CHECK(r != NULL);
    memcpy(nj_array_data(r),data,n*sizeof(*data)); return r;
}
static void readback(nj_vm *vm, nj_ref img, nj_ref array, const nj_u4 expected[36])
{
    nj_value a[7]; unsigned i;
    a[0] = reference(array); a[1] = integer(0); a[2] = integer(6);
    a[3] = a[4] = integer(0); a[5] = a[6] = integer(6);
    invoke(vm,img,NJ_C_IMAGE,"getRGB","([IIIIIII)V",a);
    for (i = 0; i < 36; ++i) CHECK((nj_u4)nj_array_get_int(array,i) == expected[i]);
}
static void submit(nj_vm *vm, nj_ref g3, nj_ref fl, nj_ref fx, nj_ref verts,
                   nj_ref colors, int command)
{
    nj_value a[11]; memset(a,0,sizeof a);
    a[3] = reference(fl); a[4] = reference(fx); a[5] = integer(command);
    a[6] = integer(1); a[7] = reference(verts); a[10] = reference(colors);
    invoke(vm,g3,G3D,"renderPrimitives","(Lcom/mascotcapsule/micro3d/v3/Texture;IIL"
           FL ";L" FX ";II[I[I[I[I)V",a);
}
static void vm_calls(nj_vm *vm, probe *p)
{
    static const nj_s4 tv[9] = {1,1,0, 1,5,0, 4,5,0};
    static const nj_s4 lv[6] = {1,4,0, 3,4,0};
    const nj_u4 red = 0xffe42c56u, green = 0xff228e36u, blue = 0xff1a4ed2u;
    nj_ref img, gfx, g3, fl, fx, verts, line, colors, array;
    nj_value a[4], out;
    nj_surface scratch, *surface;
    nj_u4 expected[36]; unsigned i, pass, start;
    a[0] = a[1] = integer(6); out.bits = 0;
    CHECK(nj_call_static(vm,NJ_C_IMAGE,"createImage","(II)L" NJ_C_IMAGE ";",a,&out) == NJ_OK);
    img = nj_root(vm,out.r); CHECK(img != NULL);
    CHECK(nj_call(vm,img,NJ_C_IMAGE,"getGraphics","()L" NJ_C_GRAPHICS ";",NULL,&out) == NJ_OK);
    gfx = nj_root(vm,out.r); CHECK(gfx != NULL);
    g3 = make(vm,G3D); fl = make(vm,FL); fx = make(vm,FX);
    verts = ints(vm,tv,9); line = ints(vm,lv,6);
    { const nj_s4 c[1] = {(nj_s4)red}; colors = ints(vm,c,1); }
    array = nj_root(vm,nj_int_array_new(vm,36)); CHECK(array != NULL);
    a[0] = a[1] = integer(4096); invoke(vm,fl,FL,"setScale","(II)V",a);
    surface = nj_midp_surface(vm,img,&scratch); CHECK(surface != NULL); p->pixels = surface->pixels;
    for (pass = 0; pass < 2; ++pass) {
        p->result = pass ? NJ_GFX_ACCEL_DECLINE : NJ_GFX_ACCEL_DONE;
        a[0] = integer(0xffffff); invoke(vm,gfx,NJ_C_GRAPHICS,"setColor","(I)V",a);
        a[0] = a[1] = integer(0); a[2] = a[3] = integer(6);
        invoke(vm,gfx,NJ_C_GRAPHICS,"fillRect","(IIII)V",a);
        for (i = 0; i < 36; ++i) expected[i] = 0xffffffffu;
        a[0] = reference(gfx); invoke(vm,g3,G3D,"bind","(Ljavax/microedition/lcdui/Graphics;)V",a);
        /* Changing Graphics after bind must not alter the captured 3D clip. */
        a[0] = a[1] = integer(0); a[2] = a[3] = integer(1);
        invoke(vm,gfx,NJ_C_GRAPHICS,"setClip","(IIII)V",a);
        start = p->calls;
        nj_array_set_int(colors,0,(nj_s4)red);
        submit(vm,g3,fl,fx,verts,colors,0x03000400);
        readback(vm,img,array,expected); CHECK(p->calls == start);
        invoke(vm,g3,G3D,"flush","()V",NULL);
        expected[19] = expected[25] = expected[26] = red;
        readback(vm,img,array,expected); CHECK(p->calls == start+(unsigned)ACTIVE);
        /* Same-depth submissions must stay in order across CPU-only blending
         * and lines between two eligible constant-colour triangles. */
        nj_array_set_int(colors,0,(nj_s4)green);
        submit(vm,g3,fl,fx,verts,colors,0x03000420); /* HALF */
        nj_array_set_int(colors,0,(nj_s4)blue);
        submit(vm,g3,fl,fx,line,colors,0x02000400);
        nj_array_set_int(colors,0,(nj_s4)green);
        submit(vm,g3,fl,fx,verts,colors,0x03000400);
        invoke(vm,g3,G3D,"flush","()V",NULL);
        expected[19] = expected[25] = expected[26] = green; expected[27] = blue;
        readback(vm,img,array,expected); CHECK(p->calls == start+2u*(unsigned)ACTIVE);
        a[0] = reference(gfx); invoke(vm,g3,G3D,"release","(Ljavax/microedition/lcdui/Graphics;)V",a);
        a[0] = a[1] = integer(0); a[2] = a[3] = integer(6);
        invoke(vm,gfx,NJ_C_GRAPHICS,"setClip","(IIII)V",a);
        a[0] = integer(0x123456); invoke(vm,gfx,NJ_C_GRAPHICS,"setColor","(I)V",a);
        a[0] = a[1] = integer(0); a[2] = a[3] = integer(1);
        invoke(vm,gfx,NJ_C_GRAPHICS,"fillRect","(IIII)V",a); expected[0] = 0xff123456u;
        readback(vm,img,array,expected);
    }
}
static void vm_test(void)
{
    nj_port ports[2]; nj_vm *vm[2]; probe p[2]; int i;
    memset(p,0,sizeof p);
    for (i = 0; i < 2; ++i) {
        nj_port_headless(&ports[i]);
#if NJ_ENABLE_GFX_ACCEL
        ports[i].gfx_raster = merge; ports[i].gfx_raster_ud = &p[i];
#endif
        vm[i] = nj_vm_create(&ports[i],4u*1024u*1024u); CHECK(vm[i] != NULL);
    }
    for (i = 0; i < 2; ++i) vm_calls(vm[i],&p[i]);
    CHECK(p[0].calls == 4u*(unsigned)ACTIVE && p[1].calls == p[0].calls);
    CHECK(p[0].accepted == 2u*(unsigned)ACTIVE && p[1].accepted == p[0].accepted);
    for (i = 0; i < 2; ++i) CHECK(nj_vm_destroy(vm[i]) > 0);
}
#endif
#endif

int main(void)
{
    direct_matrix(); counterexample_and_fallback();
#ifndef NJ_MASCOT_ACCEL_STANDALONE
#if NJ_ENABLE_MASCOT
    vm_test();
#else
    puts("Mascot VM integration SKIPPED: NJ_ENABLE_MASCOT=0");
#endif
#endif
    printf("Mascot accel=%d: %u matrix cases, %u checks PASS (CPU protocol backend; no GPU claim)\n",
           NJ_ENABLE_GFX_ACCEL,cases,checks);
    return 0;
}
