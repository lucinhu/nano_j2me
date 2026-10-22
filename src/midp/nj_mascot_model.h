/* Checked, allocation-free Mascot Capsule MBAC decoding. */
#ifndef NJ_MASCOT_MODEL_H
#define NJ_MASCOT_MODEL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Material bits in the decoded polygon tuples. */
enum {
    NJ_MASCOT_MAT_COLORKEY = 0x001,
    NJ_MASCOT_MAT_BLEND_HALF = 0x002,
    NJ_MASCOT_MAT_BLEND_ADD = 0x004,
    NJ_MASCOT_MAT_BLEND_SUB = 0x006,
    NJ_MASCOT_MAT_DOUBLE_FACE = 0x010,
    NJ_MASCOT_MAT_LIGHTING = 0x020,
    NJ_MASCOT_MAT_SPECULAR = 0x040,
    NJ_MASCOT_MAT_FLAT_NORMAL = 0x080,
    NJ_MASCOT_MAT_ZSORT_NEAR = 0x100,
    NJ_MASCOT_MAT_ZSORT_FAR = 0x200,
    NJ_MASCOT_TRI_C_STRIDE = 5,
    NJ_MASCOT_QUAD_C_STRIDE = 6,
    NJ_MASCOT_TRI_T_STRIDE = 7,
    NJ_MASCOT_QUAD_T_STRIDE = 9
};

typedef enum nj_mascot_model_result {
    NJ_MASCOT_MODEL_OK = 0,
    NJ_MASCOT_MODEL_ARGUMENT,
    NJ_MASCOT_MODEL_TRUNCATED,
    NJ_MASCOT_MODEL_INVALID,
    NJ_MASCOT_MODEL_UNSUPPORTED,
    NJ_MASCOT_MODEL_OVERFLOW,
    NJ_MASCOT_MODEL_STORAGE
} nj_mascot_model_result;

typedef struct nj_mascot_bone {
    int32_t parent;              /* -1, or an earlier bone index. */
    uint16_t start_vertex;
    uint16_t num_vertices;
    int32_t matrix[12];          /* Row-major 3x4; rotation uses 4096 = 1.
                                   Translation (indices 3,7,11) is unscaled. */
} nj_mascot_bone;

typedef struct nj_mascot_pattern_range {
    uint32_t first_tri, first_quad;
    uint32_t num_tri, num_quad;
} nj_mascot_pattern_range;

/* Native-endian, relocatable blob header. Every *_offset is a byte offset
 * from this header, zero for an absent array. Arrays have natural alignment.
 * There are no pointers to either the source or the allocation in this blob.
 * Access arrays with (const TYPE *)((const unsigned char *)model + offset).
 * This is an in-process layout, not a portable serialized file format.
 */
typedef struct nj_mascot_model {
    uint32_t total_bytes;
    uint32_t source_bytes;       /* Structured bytes consumed; suffix ignored. */
    uint32_t vertices_offset;    /* int16_t[num_vertices * 3], interleaved xyz. */
    uint32_t normals_offset;     /* int16_t[num_vertices * 3], 4096 = unit. */
    uint32_t poly_t3_offset;     /* uint16_t[][7]: material, a,b,c, uv0,uv1,uv2. */
    uint32_t poly_t4_offset;     /* uint16_t[][9]: material, a,b,c,d, uv0..uv3. */
    uint32_t poly_c3_offset;     /* uint16_t[][5]: material, color, a,b,c. */
    uint32_t poly_c4_offset;     /* uint16_t[][6]: material, color, a,b,c,d. */
    uint32_t colors_offset;      /* uint32_t[num_colors], opaque 0xAARRGGBB. */
    uint32_t bones_offset;       /* nj_mascot_bone[num_bones]. */
    uint32_t patterns_offset;    /* nj_mascot_pattern_range[num_patterns *
                                   (num_textures + 1)], pattern-major.
                                   Slot 0 uses C3/C4; slot t+1 uses T3/T4. */
    uint16_t num_vertices, num_bones;
    uint16_t num_poly_t3, num_poly_t4, num_poly_c3, num_poly_c4;
    uint16_t num_textures, num_patterns, num_colors;
    uint16_t materials_and, materials_or;
    uint8_t version, vertex_format, normal_format, polygon_format;
    uint16_t reserved;
} nj_mascot_model;

/* UV tuple halves: u = uv >> 8, v = uv & 255. Vertices and polygon indices
 * retain file order; quad triangulation, pose, and pattern selection belong
 * to the renderer. Pattern 0 is the common group. The renderer defines the
 * pattern-mask policy; the reference uses bit p for group p, not bit p-1.
 *
 * Call with storage == NULL and capacity == 0 to validate the entire MBAC
 * and query exact output_bytes. Then call again with caller-owned storage
 * aligned for nj_mascot_model and at least output_bytes bytes. Source and
 * storage must not overlap; source/output_bytes must remain valid and
 * unchanged during a call. output_bytes must not alias source or storage.
 * No allocation, retained pointers, or writable globals are used.
 *
 * Supports MBAC versions 2..5, vertex formats 1/2, normal formats 0/1/2,
 * polygon formats 1/2/3, and bone format 1. Before v5, only one pattern and
 * at most one texture have an unambiguous range layout and are accepted.
 * Trailing vendor data is allowed (including the observed 20-byte suffix).
 * On success, output_bytes is the blob size. On STORAGE it is the required
 * size. On other failures it is zero; storage contents are unspecified and
 * must be discarded. A successful fill starts with nj_mascot_model.
 */
nj_mascot_model_result nj_mascot_model_decode(const void *source,
    size_t source_bytes, void *storage, size_t capacity, size_t *output_bytes);

#ifdef __cplusplus
}
#endif
#endif
