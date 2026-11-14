/* Optional port-side GLES 2.0 implementation. No GL types enter the core. */
#ifndef NJ_GFX_GLES2_H
#define NJ_GFX_GLES2_H

#include "nj_gfx.h"
#include "nj_gfx_raster.h"
#include "nj_gfx_triangles.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nj_gles2 nj_gles2;
typedef struct nj_gles2_config {
    /* Required: nonzero only while this instance's dedicated GLES2 context
     * is current on this thread and usable. Must not invoke the VM. */
    int (*is_current)(void *ud);
    void *ud;
    /* Per source / destination / raster rectangle limit. 0 selects 1M pixels.
     * 2D uses at most two CPU RGBA buffers and two GL RGBA textures. Raster
     * lazily adds one CPU RGBA readback buffer and one GL RGBA depth texture.
     * Each buffer/texture is individually bounded by 4*max_pixels bytes. */
    size_t max_pixels;
    /* Optional copied per-instance GL dispatch. NULL uses linked GLES2.
     * Zero-initialize the complete config before setting individual fields. */
    const struct nj_gl_api *gl;
    int desktop_gl; /* desktop GLSL 1.20 rather than GLES GLSL ES 1.00. */
} nj_gles2_config;

typedef struct nj_gles2_stats {
    nj_u8 fills, blits, declines, rasters;
    nj_u8 triangles; /* Submitted native primitives (not an occlusion count). */
    nj_u8 batches;   /* Successfully committed native triangle transactions. */
} nj_gles2_stats;

/* Requires NJ_ENABLE_GFX_ACCEL=1 and a dedicated current GLES2 context with
 * fragment highp. NULL on allocation/shader/GL failure; keep software fallback.
 * This backend changes GL state; do not share its context with other renderers.
 * The caller owns context creation, thread affinity and serialization. */
nj_gles2 *nj_gles2_create(const nj_gles2_config *config);
const nj_gfx_accel *nj_gles2_backend(void);
/* Exact-coverage opaque 3D fragment merge in the same dedicated context.
 * Keys and old depth use RGBA8 bytewise comparison, without float key rounding.
 * GPU colour/depth results are both read and checked before any CPU commit.
 * DECLINE leaves surface, depth and *drawn unchanged. Resources are lazy: a
 * raster shader/allocation failure does not disable the existing 2D backend. */
int nj_gles2_raster(void *ud, const nj_gfx_raster *packet, nj_u4 *drawn);
/* Native triangles/textures/fog with atomic CPU colour + depth synchronization.
 * A context lacking verified 24-bit depth/fragment-depth support declines.
 * Coverage/rounding are native hardware semantics; exact packets stay separate. */
int nj_gles2_triangles(void *ud, const nj_gfx_triangles *packet, nj_u4 *submitted);
/* The same backend, merging an ordered list into one import/draw/readback
 * transaction. Atomic as a whole; DECLINE writes nothing for any packet. */
int nj_gles2_triangles_list(void *ud, const nj_gfx_triangles_list *list,
                            nj_u4 *submitted);
/* Persistent session: seed the rectangle once, submit lists into the resident
 * framebuffer without CPU commits, then read back and commit once. While the
 * session is open the CPU colour/depth buffers are stale (see the session
 * contract in nj_gfx_triangles.h). All three return NJ_GFX_ACCEL_DONE on
 * success; end/begin DECLINE writes nothing. abort is void and never writes
 * CPU pixels. A context that fails the triangle gate declines begin(). */
int nj_gles2_triangles_begin(void *ud,
                             const nj_gfx_triangles_session *session);
int nj_gles2_triangles_end(void *ud, nj_u4 *submitted);
void nj_gles2_triangles_abort(void *ud);
void nj_gles2_get_stats(const nj_gles2 *state, nj_gles2_stats *out);

/* Detach from all ports/contexts and finish drawing first. Returns 0 without
 * freeing anything if the owning context isn't current; otherwise frees GL
 * and CPU resources and returns 1. NULL is a successful no-op. */
int nj_gles2_destroy(nj_gles2 *state);
/* After actual context loss/destruction ONLY: frees CPU state without GL calls.
 * GL resources must already be reclaimed by the destroyed context. */
void nj_gles2_abandon(nj_gles2 *state);

#ifdef __cplusplus
}
#endif
#endif
