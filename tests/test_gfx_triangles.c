/* The borrowed native transaction must be rejected before any CPU writes. */
#include "nj2me/nj_gfx_triangles.h"
#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    nj_u4 pixels[128], texture[64], submitted;
    nj_s4 depth[80];
    nj_surface surface;
    nj_gfx_triangle_cmd triangle;
    nj_gfx_triangles packet;
} fixture;
static unsigned checks;
static void init(fixture *f)
{
    int i;
    memset(f, 0, sizeof *f);
    memset(f->pixels, 0xa5, sizeof f->pixels);
    memset(f->texture, 0x87, sizeof f->texture);
    f->submitted = 0x12345678u;
    f->surface.pixels = f->pixels;
    f->surface.width = 8; f->surface.height = 6;
    f->surface.stride = 36; f->surface.format = NJ_PIXFMT_ARGB8888;
    f->triangle.blend = 68; f->triangle.alpha_write = 1;
    f->triangle.depth_test = f->triangle.depth_write = 1;
    for (i=0; i<3; ++i) {
        f->triangle.vertices[i].x = i==1 ? 7.0f : 1.0f;
        f->triangle.vertices[i].y = i==2 ? 5.0f : 1.0f;
        f->triangle.vertices[i].depth = 0.5;
        f->triangle.vertices[i].inv_w = 1.0f;
        f->triangle.vertices[i].color = 0xff123456u;
    }
    f->packet.version = NJ_GFX_TRIANGLES_VERSION;
    f->packet.surface = &f->surface;
    f->packet.depth = f->depth; f->packet.depth_stride = 10;
    f->packet.x = f->packet.y = 1;
    f->packet.width = 6; f->packet.height = 4;
    f->packet.triangles = &f->triangle; f->packet.count = 1;
}
static void verify(fixture *f, int expected)
{
    fixture before = *f;
    int actual = nj_gfx_triangles_validate(&f->packet, &f->submitted);
    assert(actual == expected);
    assert(memcmp(f, &before, sizeof *f)==0);
    checks += 2;
}
#define CASE(expected, changes) do { fixture f; init(&f); changes; verify(&f, expected); } while (0)
#define BAD(changes) CASE(0, changes)
#define GOOD(changes) CASE(1, changes)
int main(void)
{
    fixture f;
    init(&f);
    assert(!nj_gfx_triangles_validate(NULL, &f.submitted));
    assert(!nj_gfx_triangles_validate(&f.packet, NULL));
    checks += 2;
    GOOD((void)0);
    GOOD(f.surface.format=NJ_PIXFMT_RGB565; f.surface.stride=19);
    GOOD(f.surface.format=NJ_PIXFMT_RGB888; f.surface.stride=27);
    GOOD(f.triangle.vertices[0].depth=-1.0; f.triangle.vertices[1].depth=2.0);
    GOOD(f.triangle.depth_test=f.triangle.depth_write=0; f.packet.depth=NULL);
    GOOD(f.depth[0]=-1); /* Outside the clip is not consumed. */
    GOOD(f.depth[11]=16777216);
    BAD(f.packet.version++);
    BAD(f.packet.surface=NULL);
    BAD(f.surface.pixels=NULL);
    BAD(f.surface.width=0);
    BAD(f.surface.height=-1);
    BAD(f.surface.format=-1);
    BAD(f.surface.stride=31);
    BAD(f.packet.x=-1);
    BAD(f.packet.y=INT_MAX);
    BAD(f.packet.width=INT_MAX);
    BAD(f.packet.height=0);
    BAD(f.packet.count=0);
    BAD(f.packet.count=NJ_GFX_TRIANGLES_MAX_COUNT+1u);
    BAD(f.packet.triangles=NULL);
    BAD(f.packet.depth=NULL);
    BAD(f.packet.depth_stride=7);
    BAD(f.packet.depth=(nj_s4 *)((unsigned char *)f.depth+1));
    BAD(f.depth[11]=-1);
    BAD(f.depth[11]=16777217);
    BAD(f.triangle.blend=63);
    BAD(f.triangle.blend=69);
    BAD(f.triangle.alpha_threshold=256);
    BAD(f.triangle.alpha_write=2);
    BAD(f.triangle.depth_test=-1);
    BAD(f.triangle.texture_no_alpha=2);
    BAD(f.triangle.fog=82);
    BAD(f.triangle.fog=80; f.triangle.fog_density=-1.0f);
    BAD(f.triangle.fog=81; f.triangle.fog_far=NAN);
    BAD(f.triangle.vertices[1].x=INFINITY);
    BAD(f.triangle.vertices[2].depth=NAN);
    BAD(f.triangle.vertices[0].inv_w=0);
    BAD(f.triangle.vertices[0].u=FLT_MAX; f.triangle.vertices[0].inv_w=FLT_MAX);
    BAD(f.surface.pixels=f.depth); /* Writable buffers overlap. */
    BAD(f.surface.pixels=&f.triangle);
    BAD(f.packet.depth=(nj_s4 *)&f.packet);
    BAD(f.surface.pixels=(void *)(uintptr_t)(UINTPTR_MAX-1u));
    init(&f);
    assert(!nj_gfx_triangles_validate(&f.packet, f.pixels));
    assert(!nj_gfx_triangles_validate(&f.packet, (nj_u4 *)&f.triangle));
    assert(!nj_gfx_triangles_validate(&f.packet, (nj_u4 *)&f.packet));
    checks += 3;
    /* All texture metadata and alias checks precede any reads of texels. */
    f.triangle.texture=(const nj_u1 *)f.texture;
    f.triangle.texture_width=4; f.triangle.texture_height=3;
    f.triangle.texture_bpp=4; f.triangle.wrap_s=f.triangle.wrap_t=240;
    f.triangle.filter=208; f.triangle.texture_function=227;
    verify(&f, 1);
    f.triangle.texture_width=0; verify(&f, 0); f.triangle.texture_width=4;
    f.triangle.texture_bpp=2; verify(&f, 0); f.triangle.texture_bpp=4;
    f.triangle.wrap_s=239; verify(&f, 0); f.triangle.wrap_s=241;
    f.triangle.filter=210; verify(&f, 0); f.triangle.filter=209;
    f.triangle.texture_function=225; verify(&f, 0); f.triangle.texture_function=226;
    f.triangle.texture=(const nj_u1 *)f.depth; verify(&f, 0);
    f.triangle.texture=(const nj_u1 *)&f.triangle; verify(&f, 0);
    f.triangle.texture=(const nj_u1 *)&f.submitted; verify(&f, 0);
    f.triangle.texture=(const nj_u1 *)(uintptr_t)(UINTPTR_MAX-1u); verify(&f, 0);
    printf("test_gfx_triangles: %u validation/atomicity checks passed\n", checks);
    return 0;
}
