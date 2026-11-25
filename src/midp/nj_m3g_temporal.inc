/* Included after the M3G software rasterizer. A private jittered pass uses the
 * same coverage, alpha test, shading and depth quantization as the HD pass. */
#include "../port/fsr2/nj_fsr2.h"

typedef struct {
  nj_ref node, vb, ib;
  unsigned draw, index[3];
  float xy[6], w[3];
} temporal_face;

typedef struct {
  temporal_face *face;
  size_t count, capacity;
} temporal_history;

typedef struct {
  NjFsr2 *ctx;
  const void *target; /* Logical pixel storage, shared by temporary Graphics
                         wrappers. */
  nj_surface scratch;
  nj_s4 *z, *scratch_z;
  uint32_t *color;
  float *depth, *motion;
  temporal_history prev, curr;
  int w, h, ow, oh, active, valid, cleared, reset;
  unsigned frame, draw;
  nj_u8 serial;
  nj_u4 gc;
  uint64_t background;
  nj_s8 time;
  float jx, jy, near_z, far_z, fovy;
} temporal_state;

static temporal_state *temporal_get(nj_vm *vm) {
  m3g_hd_state *s = vm->m3g_hd_state;
  return s ? s->temporal : NULL;
}

static void m3g_temporal_dispose(nj_vm *vm) {
  temporal_state *s = temporal_get(vm);
  if (!s)
    return;
  if (s->ctx)
    nj_fsr2_destroy(s->ctx);
  free(s->scratch.pixels);
  free(s->scratch_z);
  free(s->z);
  free(s->color);
  free(s->depth);
  free(s->motion);
  free(s->prev.face);
  free(s->curr.face);
  free(s);
  *m3g_temporal_slot(vm) = NULL;
}

static void m3g_temporal_bind(nj_vm *vm, const void *target, int w, int h,
                              int ow, int oh, nj_u8 serial) {
  temporal_state *s = temporal_get(vm);
  if (w <= 0 || h <= 0 || ow <= 0 || oh <= 0 || (uint64_t)w * h > 4194304 ||
      (uint64_t)ow * oh > 16777216) {
    m3g_temporal_dispose(vm);
    return;
  }
  if (s && (s->w != w || s->h != h || s->ow != ow || s->oh != oh)) {
    m3g_temporal_dispose(vm);
    s = NULL;
  }
  if (!s) {
    void **slot = m3g_temporal_slot(vm);
    size_t n = (size_t)w * h;
    char error[256];
    if (!slot || !(s = calloc(1, sizeof(*s))))
      return;
    *slot = s;
    s->w = w;
    s->h = h;
    s->ow = ow;
    s->oh = oh;
    s->ctx = nj_fsr2_create(w, h, ow, oh, error, sizeof(error));
    if (!s->ctx && getenv("NJ_FSR2_TRACE"))
      fprintf(stderr, "FSR2 context: %s\n", error);
    if (!s->ctx)
      return; /* Memoize unsupported dimensions/device. */
    s->scratch.width = w;
    s->scratch.height = h;
    s->scratch.stride = w * 4;
    s->scratch.bpp = 4;
    s->scratch.format = NJ_PIXFMT_ARGB8888;
    s->scratch.pixels = malloc(n * 4);
    s->scratch_z = malloc(n * sizeof(*s->scratch_z));
    s->z = malloc(n * sizeof(*s->z));
    s->color = malloc(n * 4);
    s->depth = malloc(n * sizeof(float));
    s->motion = malloc(n * 2 * sizeof(float));
    if (!s->scratch.pixels || !s->scratch_z || !s->z || !s->color ||
        !s->depth || !s->motion) {
      m3g_temporal_dispose(vm);
      return;
    }
  }
  if (!s->ctx)
    return;
  s->reset = s->active || !s->prev.count || target != s->target ||
             serial != s->serial + 1 || s->gc != vm->heap.gc_count;
  s->active = 1;
  s->valid = 1;
  s->cleared = 0;
  s->curr.count = 0;
  s->draw = 0;
  s->serial = serial;
  s->target = target;
  s->gc = vm->heap.gc_count;
  nj_fsr2_jitter(s->ctx, s->frame, &s->jx, &s->jy);
}

static void m3g_temporal_invalidate(nj_vm *vm) {
  temporal_state *s = temporal_get(vm);
  if (s && s->active)
    s->valid = 0;
}

static void m3g_temporal_clear(nj_vm *vm, const nj_surface *logical, int full) {
  temporal_state *s = temporal_get(vm);
  if (!s || !s->active)
    return;
  if (logical && logical->pixels == s->target && full)
    s->valid =
        1; /* This clear replaces every earlier fragment in the binding. */
  if (getenv("NJ_FSR2_TRACE") && !s->frame)
    fprintf(stderr, "FSR2 clear full=%d valid=%d target_matches=%d\n", full,
            s->valid, logical && logical->pixels == s->target);
  if (!s->valid)
    return;
  if (!full || !logical || logical->pixels != s->target ||
      logical->width != s->w || logical->height != s->h) {
    s->valid = 0;
    return;
  }
  size_t n = (size_t)s->w * s->h;
  /* A screen-space background has zero velocity when its pixels are unchanged.
   * Detect animated images, crop offsets and clear colour changes. */
  uint64_t signature = UINT64_C(14695981039346656037);
  for (int y = 0; y < s->h; y++)
    for (int x = 0; x < s->w; x++) {
      uint32_t color = nj_surface_get(logical, x, y);
      s->color[(size_t)y * s->w + x] = color;
      signature = (signature ^ color) * UINT64_C(1099511628211);
    }
  if (s->background != signature)
    s->reset = 1;
  s->background = signature;
  memset(s->z, 0, n * sizeof(*s->z));
  memset(s->motion, 0, 2 * n * sizeof(float));
  s->cleared = 1;
  s->curr.count = 0;
  s->draw = 0;
}

static void m3g_temporal_mesh(m3g_jr *r) {
  temporal_state *s = temporal_get(r->vm);
  if (!s || !s->active || !s->valid)
    return;
  const float *p = r->proj;
  /* Conventional finite, centered perspective. FSR2 requires physical
   * camera planes and vertical field of view for the input depth buffer. */
  if (!s->cleared || p[14] != -1.f || p[15] != 0 || p[5] <= 0 ||
      p[10] >= -1.f || p[11] >= 0 || r->sw.depth_near != 0 ||
      r->sw.depth_far != 1 || !r->sw.dwrite || !r->sw.dtest ||
      (r->sw.blend != 64 && r->sw.blend != 68) || r->sw.depth_units ||
      r->sw.depth_factor) {
    if (getenv("NJ_FSR2_TRACE"))
      fprintf(stderr,
              "FSR2 mesh clear=%d p14=%g p5=%g p10=%g p11=%g depth=%g,%g "
              "test/write=%d,%d blend=%d\n",
              s->cleared, p[14], p[5], p[10], p[11], r->sw.depth_near,
              r->sw.depth_far, r->sw.dtest, r->sw.dwrite, r->sw.blend);
    s->valid = 0;
    return;
  }
  float near_z = p[11] / (p[10] - 1), far_z = p[11] / (p[10] + 1),
        fovy = 2 * atanf(1 / p[5]);
  if (!isfinite(near_z) || !isfinite(far_z) || !isfinite(fovy) ||
      !(far_z > near_z && near_z > 0)) {
    s->valid = 0;
    return;
  }
  if (s->near_z != near_z || s->far_z != far_z || s->fovy != fovy) {
    if (s->draw) {
      s->valid = 0;
      return;
    }
    s->reset = 1;
  }
  s->near_z = near_z;
  s->far_z = far_z;
  s->fovy = fovy;
  ++s->draw;
}

static temporal_face *temporal_append(temporal_history *h) {
  if (h->count == h->capacity) {
    if (h->capacity >= 262144)
      return NULL;
    size_t cap = h->capacity ? 2 * h->capacity : 128;
    temporal_face *ptr = realloc(h->face, cap * sizeof(*ptr));
    if (!ptr)
      return NULL;
    h->face = ptr;
    h->capacity = cap;
  }
  return &h->face[h->count++];
}

static float temporal_edge(float ax, float ay, float bx, float by, float x,
                           float y) {
  return (bx - ax) * (y - ay) - (by - ay) * (x - ax);
}

static void m3g_temporal_triangle(m3g_jr *r, const jr_vtx *a, const jr_vtx *b,
                                  const jr_vtx *c, const float xy[6],
                                  const float inv[3], const double depth[3],
                                  const float st[6], const nj_u4 rgb[3],
                                  const nj_u4 rgb2[3]) {
  temporal_state *s = temporal_get(r->vm);
  if (!s || !s->active || !s->valid)
    return;
  size_t fi = s->curr.count;
  temporal_face *face = temporal_append(&s->curr);
  if (!face) {
    s->valid = 0;
    return;
  }
  const jr_vtx *v[3] = {a, b, c};
  float jit[6];
  face->node = r->hd_node;
  face->vb = r->hd_vb;
  face->ib = r->hd_ib;
  face->draw = s->draw;
  memcpy(face->xy, xy, sizeof(face->xy));
  for (int i = 0; i < 3; i++) {
    if (!(inv[i] > 0) || !isfinite(inv[i]) || !isfinite(xy[2 * i]) ||
        !isfinite(xy[2 * i + 1])) {
      s->valid = 0;
      return;
    }
    face->index[i] = v[i]->source_index;
    face->w[i] = 1 / inv[i];
    if (face->index[i] == UINT32_MAX)
      s->reset = 1;
    /* FSR2 recovers the unjittered sample position as pixelCenter - jitter. */
    jit[2 * i] = xy[2 * i] + s->jx;
    jit[2 * i + 1] = xy[2 * i + 1] + s->jy;
  }
  const temporal_face *prev = fi < s->prev.count ? &s->prev.face[fi] : NULL;
  if (!prev || prev->node != face->node || prev->vb != face->vb ||
      prev->ib != face->ib || prev->draw != face->draw ||
      memcmp(prev->index, face->index, sizeof(face->index)))
    s->reset = 1;
  m3g_sw sw = r->sw;
  sw.surf = &s->scratch;
  sw.z = s->scratch_z;
  /* Capture the source alpha before blending. A partially transparent
   * contributor needs a reactive mask and multiple surface velocities. */
  sw.blend = 68;
  sw.awrite = 1;
  sw.vx /= r->hd_scale;
  sw.vy /= r->hd_scale;
  sw.vw /= r->hd_scale;
  sw.vh /= r->hd_scale;
#if NJ_ENABLE_GFX_ACCEL
  sw.collect = NULL;
#endif
  float xmin = fminf(jit[0], fminf(jit[2], jit[4])),
        xmax = fmaxf(jit[0], fmaxf(jit[2], jit[4]));
  float ymin = fminf(jit[1], fminf(jit[3], jit[5])),
        ymax = fmaxf(jit[1], fmaxf(jit[3], jit[5]));
  int x0 = (int)fmaxf(0, fminf(s->w, floorf(xmin) - 1)),
      x1 = (int)fminf(s->w, fmaxf(0, ceilf(xmax) + 1));
  int y0 = (int)fmaxf(0, fminf(s->h, floorf(ymin) - 1)),
      y1 = (int)fminf(s->h, fmaxf(0, ceilf(ymax) + 1));
  if (x0 >= x1 || y0 >= y1)
    return;
  for (int y = y0; y < y1; y++)
    memset(s->scratch_z + (size_t)y * s->w + x0, 0,
           (size_t)(x1 - x0) * sizeof(nj_s4));
  if (!sw_tri_band(&sw, jit, inv, depth, st, sw.tex && (r->tpos || sw.sprite),
                   rgb, rgb2, r->twosided, -1))
    return;
  float area = temporal_edge(xy[0], xy[1], xy[2], xy[3], xy[4], xy[5]);
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) {
      size_t k = (size_t)y * s->w + x;
      nj_s4 key = s->scratch_z[k];
      if (!key || key < s->z[k])
        continue;
      uint32_t color = ((uint32_t *)s->scratch.pixels)[k];
      if ((color >> 24) != 255) {
        s->valid = 0;
        return;
      }
      s->z[k] = key;
      s->color[k] = color;
      if (s->reset || !prev || area == 0)
        continue;
      float px = x + .5f - s->jx, py = y + .5f - s->jy;
      float l0 = temporal_edge(xy[2], xy[3], xy[4], xy[5], px, py) / area;
      float l1 = temporal_edge(xy[4], xy[5], xy[0], xy[1], px, py) / area;
      float weights[3] = {l0 * inv[0] * prev->w[0], l1 * inv[1] * prev->w[1],
                          (1 - l0 - l1) * inv[2] * prev->w[2]};
      float den = weights[0] + weights[1] + weights[2];
      if (!(den > 0) || !isfinite(den)) {
        s->reset = 1;
        continue;
      }
      for (int axis = 0; axis < 2; axis++)
        s->motion[2 * k + axis] =
            (weights[0] * prev->xy[axis] + weights[1] * prev->xy[2 + axis] +
             weights[2] * prev->xy[4 + axis]) /
                den -
            (axis ? py : px);
    }
}

static int m3g_temporal_release(nj_vm *vm, const void *target, nj_surface *hd) {
  temporal_state *s = temporal_get(vm);
  if (!s || !s->active)
    return 0;
  s->active = 0;
  if (!s->valid || !s->cleared || !s->curr.count || target != s->target ||
      !hd || !hd->pixels || hd->format != NJ_PIXFMT_ARGB8888 ||
      hd->width != s->ow || hd->height != s->oh || hd->stride != s->ow * 4) {
    if (getenv("NJ_FSR2_TRACE"))
      fprintf(stderr,
              "FSR2 fallback valid=%d clear=%d draws=%u triangles=%zu\n",
              s->valid, s->cleared, s->draw, s->curr.count);
    s->prev.count = 0;
    return 0;
  }
  nj_s8 now = nj_now_ms(vm);
  float dt = (float)(now - s->time);
  NjFsr2Frame f = {0};
  f.reset = s->reset || s->curr.count != s->prev.count || dt > 250 || dt < 0 ||
            vm->heap.gc_count != s->gc;
  size_t n = (size_t)s->w * s->h;
  for (size_t i = 0; i < n; i++)
    s->depth[i] = s->z[i] ? 1.f - (s->z[i] - 1) / 16777215.f : 1.f;
  if (f.reset)
    memset(s->motion, 0, 2 * n * sizeof(float));
  f.color_argb = s->color;
  f.motion_xy = s->motion;
  f.depth = s->depth;
  f.jitter_x = s->jx;
  f.jitter_y = s->jy;
  f.delta_ms = (dt > 0 && dt <= 250) ? dt : 16.667f;
  f.camera_near = s->near_z;
  f.camera_far = s->far_z;
  f.fov_y = s->fovy;
  char error[256];
  /* The SDK bridge finishes GPU work before publishing CPU pixels. On
   * failure the spatial rendering in this surface is still usable. */
  if (!nj_fsr2_dispatch(s->ctx, &f, (uint32_t *)hd->pixels, error, sizeof(error))) {
    m3g_temporal_dispose(vm);
    return 0;
  }
  if (getenv("NJ_FSR2_TRACE"))
    fprintf(stderr,
            "FSR2 dispatched %dx%d -> %dx%d triangles=%zu reset=%d phase=%u\n",
            s->w, s->h, s->ow, s->oh, s->curr.count, f.reset, s->frame);
  temporal_history tmp = s->prev;
  s->prev = s->curr;
  s->curr = tmp;
  s->time = now;
  s->frame++;
  return 1;
}
