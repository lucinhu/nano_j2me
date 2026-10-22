/*
 * Mascot Capsule Micro3D v3: indexed BMP texture decode + triangle raster.
 *
 * Table-rotation port of the reference Java rasterizer the DMC / Micro3D
 * model games use:
 *
 *   FreeJ2ME-Plus src/com/mascotcapsule/micro3d/v3/Rasterizer.java
 *   FreeJ2ME-Plus src/com/mascotcapsule/micro3d/v3/Texture.java
 *   (MIT License, Copyright (c) 2026 Roman Lahin and Yury Kharchenko)
 *
 * Pure C99, allocation free: the caller owns every buffer.  No depth buffer;
 * triangles are drawn in the order the caller queues them (the reference
 * sorts its queue far-to-near and flushes).
 *
 * VERTEX UNITS
 * ------------
 * A vertex carries exactly what the reference keeps in its polygon queue, so
 * the caller needs no conversion:
 *
 *   x, y     screen pixels, plain integers (queue stores signed 16 bit; the
 *            reference clamps to -32768..32767)
 *   u, v     texture coordinates, 8.8 fixed (original 8-bit texel << 8)
 *   light    per-vertex light intensity, 0..4095 (processLighting() output).
 *            4095 is full brightness (NJ_MR_LIGHT_FULL); pass 0 for an
 *            unshaded texture or full brightness for an unlit colour poly.
 *   eu, ev   environment-map coordinates, 4.4 fixed (processLighting() 0..1039)
 *
 * Internally u<<4 becomes the reference's 12.12 texel coordinate,
 * (light>>2)<<7 its 12.12 shade and eu<<8 its 12.12 env coordinate.
 *
 * FLAT vs SMOOTH
 * --------------
 *   NJ_MR_LIGHTING set     colour poly: CL/CLE (shade interpolated);
 *                          textured: TL/TLE (shade interpolated per texel)
 *   NJ_MR_LIGHTING clear   colour poly: C/CE (flat shade from vertex 0);
 *                          textured: T/TE (flat shade from vertex 0 when
 *                          NJ_MR_SHADED is set)
 *
 * A flat caller puts the same light/env values in all three vertices and
 * leaves NJ_MR_LIGHTING clear.  The reference's flat and smooth colour
 * formulas differ, hence the flag.
 *
 * PIXEL FORMATS
 * -------------
 * Blending happens in ARGB8888.  RGB565/RGB888 destinations convert to
 * ARGB8888 (565 channels expanded by bit replication, like nj_surface_get)
 * and write back truncated.  Pixel-exact comparison with the Java reference
 * needs an ARGB8888 surface.
 */
#ifndef NJ_MASCOT_RASTER_H
#define NJ_MASCOT_RASTER_H

#include "nj2me/nj.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Blend modes (reference blendPixel / fillTriangle switch)            */
/* ------------------------------------------------------------------ */
#define NJ_MR_BLEND_REPLACE 0   /* framebuffer = source                      */
#define NJ_MR_BLEND_HALF    1   /* (source + destination) / 2, floor         */
#define NJ_MR_BLEND_ADD     2   /* saturating add per channel                */
#define NJ_MR_BLEND_SUB     3   /* saturating subtract per channel           */

/* ------------------------------------------------------------------ */
/* Triangle flags                                                      */
/* ------------------------------------------------------------------ */
#define NJ_MR_TEXTURED 0x0001u  /* sample the model texture                 */
#define NJ_MR_COLORKEY 0x0002u  /* texture index 0 is transparent           */
#define NJ_MR_SHADED   0x0004u  /* apply light to textured texels           */
#define NJ_MR_LIGHTING 0x0008u  /* interpolate light/env from the vertices  */
#define NJ_MR_ENVMAP   0x0010u  /* add the environment-map colour           */
#define NJ_MR_TOON     0x0020u  /* toon shading: light passes through toon  */

/*
 * Toon shading (Effect3D.TOON_SHADING).
 *
 * The reference Geometry stage splits a triangle at the threshold so every
 * sub-triangle is flat; its rasterizer then paints the whole sub-triangle
 * with either the low or the high toon colour converted to a light level:
 *
 *     lightData = toonColour << 2;   shade = lightData << 7
 *     channel   = channel * ((toonColour >> 3) + 1) >> 5
 *     textured  = palette[(toonColour >> 3) << 8 | index]
 *
 * Doing the split per pixel gives the same picture: for every pixel the
 * interpolated light is compared as the reference compares it
 * ((light >> 4) <= threshold ? low : high) and that toon colour is used.
 * `toon` packs the parameters exactly as the queue plans to carry them:
 *
 *     bit  0      enabled
 *     bits 8..15  threshold, 0..255
 *     bits 16..23 high colour, 0..255
 *     bits 24..31 low colour, 0..255
 *
 * NJ_MR_TOON must be set and the pack's bit 0 must be set; with it clear the
 * triangle is drawn normally.
 */
#define NJ_MR_TOON_PACK(enabled, threshold, high, low)                     \
    ((((nj_u4)(enabled)) & 0x1u) |                                        \
     ((((nj_u4)(threshold)) & 0xFFu) << 8) |                              \
     ((((nj_u4)(high)) & 0xFFu) << 16) |                                  \
     ((((nj_u4)(low)) & 0xFFu) << 24))

/* ------------------------------------------------------------------ */
/* Vertex (exactly the reference queue vertex)                         */
/* ------------------------------------------------------------------ */
typedef struct nj_mascot_vertex {
    nj_s4 x, y;    /* screen position, integer pixels                        */
    nj_s4 u, v;    /* texture coordinates, 8.8 fixed                         */
    nj_s4 light;   /* light intensity, 0..4095                               */
    nj_s4 eu, ev;  /* environment-map coordinates, 4.4 fixed                 */
} nj_mascot_vertex;

/* Full brightness for `light`; also the unlit-colour default. */
#define NJ_MR_LIGHT_FULL 4095

/* ------------------------------------------------------------------ */
/* Errors                                                              */
/* ------------------------------------------------------------------ */
#define NJ_MR_OK           0
#define NJ_MR_ERR_ARG    (-1)   /* null/negative/out-of-range argument       */
#define NJ_MR_ERR_FORMAT (-2)   /* not an 8-bit CORE/INFO40 BMP              */
#define NJ_MR_ERR_SIZE   (-3)   /* buffer too small / BMP too large          */

/* ------------------------------------------------------------------ */
/* Texture blob                                                        */
/* ------------------------------------------------------------------ */
/* The decoded texture is an immutable byte blob the caller keeps alive
 * (e.g. in a Java byte[] rooted from the Texture object).  Little endian,
 * blob should be 4-byte aligned:
 *
 *   0   u32 magic      NJ_MR_BLOB_MAGIC
 *   4   u16 width      original BMP width
 *   6   u16 height     original BMP height
 *   8   u8  width_bit  log2 of padded width
 *   9   u8  height_bit log2 of padded height
 *   10  u8  flags      NJ_MR_TEX_*
 *   11  u8  reserved   0
 *   12  u16 padded_w   1 << width_bit, at most 32768
 *   14  u16 padded_h   1 << height_bit, at most 32768
 *   16  u32 raster_off byte offset of the raster from blob start
 *   20  u32 raster_len index count (model) / 4096 (environment map)
 *   24  256 u32 ARGB palette (raw BMP palette; zero filled)
 *   1048 raster       model: padded index bytes (padded with index 0)
 *                      environment: 4096 u32 0x00RRGGBB
 *
 * The model bitmap is padded to a power of two in both dimensions and
 * sampled with the reference's `(((v >> 12) << width_bit) | (u >> 12)) &
 * len_mask`; that is why the padded shape is part of the blob.  Environment
 * maps are always 64x64 (width_bit = height_bit = 6), as in Micro3D v3.
 *
 * Limits: width_bit/height_bit <= 15 (a padded side is stored in a u16, so
 * 65536 cannot be represented) and the padded area is at most 16 MiB texels
 * (16777216), which caps a model blob at 1048 + 16777216 = 16778264 bytes
 * (4096x4096).  The decoder rejects anything larger; view() rejects crafted
 * blobs that violate the same limits.
 */
#define NJ_MR_BLOB_MAGIC 0x4D335458u /* 'M3TX' */

#define NJ_MR_TEX_MODEL       0x01u
#define NJ_MR_TEX_ENVMAP      0x02u
#define NJ_MR_TEX_FIRST_BLACK 0x04u /* palette[0] == 0xFF000000              */

#define NJ_MR_BLOB_HEADER_SIZE   24u
#define NJ_MR_BLOB_PALETTE_OFF   24u
#define NJ_MR_BLOB_RASTER_OFF    1048u
#define NJ_MR_ENV_SIZE           4096u

/* View over a decoded blob; filling it copies nothing. */
typedef struct nj_mascot_texture {
    const nj_u1 *palette;  /* 256 ARGB8888 entries                             */
    const nj_u1 *raster;   /* padded indices, or 4096 0x00RRGGBB env pixels    */
    nj_s4 width_bit;
    nj_s4 height_bit;
    nj_s4 len_mask;        /* (1 << (width_bit + height_bit)) - 1, or 4095     */
    nj_s4 flags;           /* NJ_MR_TEX_*                                      */
} nj_mascot_texture;

/* Bytes the blob needs (written to *out_size), or a negative NJ_MR_ERR_*.
 * is_for_model = 1 decodes the 8-bit index bitmap (padded to power-of-two
 * dimensions); 0 decodes the 64x64 environment map (indices below 1 are
 * ignored). */
nj_s4 nj_mascot_texture_query(const void *bmp, nj_s4 bmp_len,
                              nj_s4 is_for_model, nj_s4 *out_size);

/* Decode bmp into blob; NJ_MR_OK or negative error. */
nj_s4 nj_mascot_texture_fill(const void *bmp, nj_s4 bmp_len,
                             nj_s4 is_for_model, void *blob, nj_s4 blob_len);

/* Validate a decoded blob and fill out; NJ_MR_OK or negative error. */
nj_s4 nj_mascot_texture_view(const void *blob, nj_s4 blob_len,
                             nj_mascot_texture *out);

/* ------------------------------------------------------------------ */
/* Material and one-triangle draw                                      */
/* ------------------------------------------------------------------ */
typedef struct nj_mascot_material {
    nj_u4 color;           /* colour-poly RGB, 0x00RRGGBB (channels only)     */
    nj_s4 flags;           /* NJ_MR_*                                        */
    nj_s4 blend;           /* NJ_MR_BLEND_*, 0 = REPLACE                      */
    nj_u4 toon;            /* NJ_MR_TOON_PACK(...), used with NJ_MR_TOON      */
    const void *texture;   /* decoded model blob when NJ_MR_TEXTURED          */
    nj_s4 texture_len;
    const void *env;       /* decoded environment blob when NJ_MR_ENVMAP      */
    nj_s4 env_len;
} nj_mascot_material;

/*
 * Draw one triangle the way the reference flush does.
 *
 * clip is a half-open rectangle in surface pixels, [x1,x2) x [y1,y2);
 * pass (0, 0, dst->width, dst->height) for the whole surface.
 * Vertices are copied before sorting, so any order is fine.
 * Degenerate triangles (cy == ay), vertices outside the clip rectangle and
 * extreme (reference-clamped) coordinates are safe.
 */
nj_s4 nj_mascot_raster_draw(nj_surface *dst,
                            nj_s4 clip_x1, nj_s4 clip_y1,
                            nj_s4 clip_x2, nj_s4 clip_y2,
                            const nj_mascot_vertex verts[3],
                            const nj_mascot_material *mat);

/* Low-level variant taking already-validated texture views; same clipping
 * and vertex units as nj_mascot_raster_draw.  Does not validate the views.
 * `toon` is the NJ_MR_TOON_PACK(...) word (ignored without NJ_MR_TOON). */
void nj_mascot_raster_triangle(nj_surface *dst,
                               nj_s4 clip_x1, nj_s4 clip_y1,
                               nj_s4 clip_x2, nj_s4 clip_y2,
                               const nj_mascot_vertex verts[3],
                               nj_u4 color,
                               const nj_mascot_texture *tex,
                               const nj_mascot_texture *env,
                               nj_u4 flags, nj_s4 blend, nj_u4 toon);

/* ------------------------------------------------------------------ */
/* Points and lines                                                    */
/* ------------------------------------------------------------------ */
/* Same blendPixel() as the polygons, so ADD/SUB behave identically.
 * argb is 0xAARRGGBB; REPLACE drops alpha in the packer.  The line walks
 * the reference's Bresenham exactly (needed for pixel-exact diff tests).
 * Clip rectangle as above. */
void nj_mascot_raster_pixel(nj_surface *dst, nj_s4 x, nj_s4 y,
                            nj_u4 argb, nj_s4 blend);

void nj_mascot_raster_line(nj_surface *dst,
                           nj_s4 clip_x1, nj_s4 clip_y1,
                           nj_s4 clip_x2, nj_s4 clip_y2,
                           nj_s4 x0, nj_s4 y0, nj_s4 x1, nj_s4 y1,
                           nj_u4 argb, nj_s4 blend);

/* Reference per-pixel blend of a source colour over a destination colour
 * (both 0xAARRGGBB). */
nj_u4 nj_mascot_blend_pixel(nj_u4 src, nj_u4 dst, nj_s4 blend);

#ifdef __cplusplus
}
#endif

#endif /* NJ_MASCOT_RASTER_H */
