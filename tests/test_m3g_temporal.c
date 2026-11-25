/* Exercise the public M3G-to-Image path, a new Graphics wrapper each frame,
 * actual AMD dispatch, then the 2D HUD and the offscreen-to-screen blit. */
#define main m3g_reference_main
#include "test_m3g_targets.c"
#undef main
#include "../src/port/fsr2/nj_fsr2.h"
#include <math.h>

enum { TW = 96, TH = 64, SCALE = 2 };
static int dispatches, current;
static nj_u4 ticks;
static nj_u4 test_clock(void *ud) {
  (void)ud;
  return ticks;
}
static void display(void *ud, const nj_surface *s) {
  (void)ud;
  (void)s;
}
extern int __real_nj_fsr2_dispatch(NjFsr2 *, const NjFsr2Frame *, uint32_t *,
                                   char *, size_t);
int __wrap_nj_fsr2_dispatch(NjFsr2 *ctx, const NjFsr2Frame *f, uint32_t *out,
                            char *error, size_t cap) {
  const int k = TH / 2 * TW + TW / 2;
  CHECK(f->depth[k] > 0 && f->depth[k] < 1);
  CHECK(f->depth[0] == 1 && f->motion_xy[0] == 0 && f->motion_xy[1] == 0);
  CHECK(f->color_argb[0] == 0xff182337u);
  CHECK(f->reset == (current == 0 || current == 3));
  /* One position unit at scale .5 and distance 3 moves 8 input pixels. */
  CHECK(fabsf(f->motion_xy[2 * k] - (current == 2 ? -8.f : 0.f)) < .01f);
  CHECK(fabsf(f->motion_xy[2 * k + 1]) < .01f);
  CHECK(fabsf(f->camera_near - 1.f) < .001f &&
        fabsf(f->camera_far - 10.f) < .001f);
  CHECK(fabsf(f->fov_y - 1.5707963f) < .001f);
  ++dispatches;
  int ok = __real_nj_fsr2_dispatch(ctx, f, out, error, cap);
  if (!ok)
    fprintf(stderr, "AMD dispatch failed: %s\n", error);
  return ok;
}
int main(void) {
  char error[256];
  NjFsr2 *preflight =
      nj_fsr2_create(TW, TH, TW * SCALE, TH * SCALE, error, sizeof(error));
  if (!preflight) {
    fprintf(stderr, "Skipping Vulkan test: %s\n", error);
    return 77;
  }
  nj_fsr2_destroy(preflight);
  target t;
  init(&t, NJ_PIXFMT_ARGB8888);
  t.port.display_hd = display;
  t.port.millis = test_clock;
  t.vm->render_quality.scale = SCALE;
  t.vm->render_quality.ssaa = 1;
  t.vm->render_quality.fsr2 = 1;
  nj_ref image = nj_root(t.vm, nj_midp_image_new(t.vm, TW, TH, 1));
  integer(&t, t.bg, C("Background"), "setColor", "(I)V", 0xff182337u);
  for (current = 0; current < 4; current++) {
    ticks += 16;
    t.vm->frames++;
    if (current == 3)
      image = nj_root(t.vm, nj_midp_image_new(t.vm, TW, TH, 1));
    nj_value g, a[6];
    CHECK(nj_call(t.vm, image, "javax/microedition/lcdui/Image", "getGraphics",
                  "()L" G ";", NULL, &g) == NJ_OK);
    nj_root(t.vm, g.r);
    a[0] = g;
    call(&t, t.g3d, C("Graphics3D"), "bindTarget", "(Ljava/lang/Object;)V", a);
    clear(&t);
    nj_s2 coords[12] = {-2, -2, -6, 2, -2, -6, -2, 2, -6, 2, 2, -6};
    if (current >= 2)
      for (int i = 0; i < 4; i++)
        coords[i * 3]++;
    memcpy(nj_array_data(t.positions), coords, sizeof(coords));
    render(&t);
    call(&t, t.g3d, C("Graphics3D"), "releaseTarget", "()V", NULL);
    CHECK(dispatches == current + 1);
    nj_surface surface;
    CHECK(nj_midp_surface(t.vm, image, &surface));
    nj_render_mirror *layer = nj_midp_hd_target(t.vm, &surface);
    CHECK(layer);
    /* Draw HUD after release via actual LCDUI methods. */
    integer(&t, g.r, G, "setColor", "(I)V", 0xff33aau);
    a[0].i = 2;
    a[1].i = 2;
    a[2].i = 4;
    a[3].i = 3;
    call(&t, g.r, G, "fillRect", "(IIII)V", a);
    CHECK(nj_surface_get(layer->target, 5, 5) == 0xffff33aau);
    a[0].r = image;
    a[1].i = 0;
    a[2].i = 0;
    a[3].i = 20;
    call(&t, t.g, G, "drawImage", "(Ljavax/microedition/lcdui/Image;III)V", a);
    nj_render_mirror *screen = nj_midp_hd_target(t.vm, &t.vm->screen);
    for (int y = 0; y < TH * SCALE; y++)
      for (int x = 0; x < TW * SCALE; x++)
        CHECK(nj_surface_get(screen->target, x, y) ==
              nj_surface_get(layer->target, x, y));
  }
  destroy(&t);
  puts("M3G offscreen FSR2: four AMD dispatches, static/moving/reset history "
       "and HD HUD blit passed");
  return 0;
}
