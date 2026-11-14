/* Optional exact-coverage 3D fragment interface. Pure C; no graphics API types. */
#ifndef NJ_GFX_RASTER_H
#define NJ_GFX_RASTER_H
#include "nj_gfx.h"
#ifdef __cplusplus
extern "C" {
#endif

#define NJ_GFX_RASTER_VERSION 1u
#define NJ_GFX_RASTER_MAX_KEY 16777216

/* One opaque, constant-colour primitive, AFTER engine-specific coverage and
 * depth quantization. CPU raster setup remains authoritative. keys is a dense
 * rectangle: 0 means uncovered; 1..16777216 are reversed depth keys (nearer is
 * larger). For a depthless primitive use 1 for each covered pixel.
 * x/y are surface/device coordinates. All strides below count int32 entries,
 * NOT bytes. keys starts at rectangle (x,y); depth starts at surface (0,0).
 * Depth test is key >= old (equal passes). Test and write are independent.
 * A NULL depth is valid only with both depth controls zero.
 * Surface pixels, keys, depth, packet storage and the drawn result must not
 * overlap. keys/depth point to properly aligned int32 arrays.
 * Pointers are borrowed for this synchronous call only; do not invoke the VM.
 * The caller clips coverage to the rectangle and supplies valid buffer sizes.
 * This interface does NOT replace transforms, clipping, shading or sorting.
 */
typedef struct nj_gfx_raster {
    nj_u4 version;
    nj_surface *surface;
    int x, y, width, height;
    const nj_s4 *keys;
    int key_stride;
    nj_s4 *depth;
    int depth_stride;
    int depth_test, depth_write;
    nj_u4 color; /* opaque 0xFFRRGGBB */
} nj_gfx_raster;

/* DONE means every colour/depth write completed and *drawn is 0 or 1, reporting
 * whether any covered fragment passed. Any other return means NO writes to
 * colour, depth OR *drawn, allowing the original software path to execute.
 * Backend context lifetime and serialization belong to the port. */
typedef int (*nj_gfx_raster_fn)(void *ud, const nj_gfx_raster *packet,
                                nj_u4 *drawn);

/* Structural/value validation (no writes) and reference synchronous merge.
 * Software returns DONE or DECLINE with the same atomic contract. */
int nj_gfx_raster_validate(const nj_gfx_raster *packet);
int nj_gfx_raster_software(const nj_gfx_raster *packet, nj_u4 *drawn);
#ifdef __cplusplus
}
#endif
#endif
