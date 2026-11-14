/* Optional native triangle batches. Pure C; no graphics API or VM types. */
#ifndef NJ_GFX_TRIANGLES_H
#define NJ_GFX_TRIANGLES_H
#include "nj_gfx.h"
#ifdef __cplusplus
extern "C" {
#endif

#define NJ_GFX_TRIANGLES_VERSION 1u
#define NJ_GFX_TRIANGLES_MAX_COUNT 65536u

/* Post-clipping device coordinates (origin top left), window depth [0,1],
 * positive reciprocal clip W, normalized UV, and screen-affine ARGB colour.
 * Texture UV is perspective-correct: interpolate (u*inv_w,v*inv_w,inv_w).
 * Depth offset has ALREADY been applied by the caller. The backend must NOT
 * apply another polygon offset. Finite depth outside [0,1] is clamped per pixel.
 * Face culling and two-sided colour selection have ALREADY happened. */
typedef struct nj_gfx_triangle_vertex {
    float x, y;
    double depth;
    float inv_w, u, v;
    nj_u4 color;
} nj_gfx_triangle_vertex;

typedef struct nj_gfx_triangle_cmd {
    nj_gfx_triangle_vertex vertices[3];
    /* Selected mip level, tightly packed RGB/RGBA bytes, top row first.
     * NULL means untextured. The no_alpha flag describes the ORIGINAL image
     * format, even when a resolved mip uses RGBA. */
    const nj_u1 *texture;
    int texture_width, texture_height, texture_bpp, texture_no_alpha;
    int wrap_s, wrap_t;       /* 240 clamp, 241 repeat (wrap then clamp taps). */
    int filter;               /* 208 nearest, 209 linear. */
    int texture_function;     /* 226 decal, 227 modulate, 228 replace. */
    int blend;                /* 64 alpha, 65 add, 66 modulate, 67 x2, 68 replace. */
    int alpha_threshold;      /* 0..255; discard source alpha < threshold. */
    int alpha_write, depth_test, depth_write; /* independent booleans */
    int fog;                  /* 0 disabled, 80 exponential, 81 linear. */
    nj_u4 fog_color;           /* RGB; source alpha is preserved. */
    float fog_near, fog_far, fog_density; /* distance = 1/interpolated inv_w. */
} nj_gfx_triangle_cmd;

/* A single synchronous transaction, in supplied triangle order. All pointers
 * are borrowed for this call only: no VM reentry, retention, or async access.
 * Depth starts at surface (0,0); depth_stride counts int32 entries, NOT bytes.
 * Existing CPU keys: 0 clear, 1 + round((1-windowDepth)*16777215).
 * Native D24 writes use q=round(clamp(windowDepth,0,1)*16777215),
 * key=16777216-q, with the same q used for the actual depth comparison.
 * Imported keys retain their exact rank; 0 and 1 share the far depth rank.
 * At half-LSB ties native rounding may differ from the CPU formula by one key.
 * Equal canonical ranks pass (LEQUAL); exact fragments retain their own API.
 * The clip is an already-intersected device rectangle, inside the surface.
 * Input storage (including textures), output colour, depth, packet, surface
 * descriptor and result must not overlap. Buffers must have declared lengths.
 * Textures may alias other read-only textures. Mutable images must be observed
 * on EVERY call; pointer identity alone never proves texture contents stable.
 *
 * This interface opts into native hardware coverage/interpolation/rounding.
 * It does NOT promise byte-identical output to a software triangle rasterizer.
 * Software remains independent, and exact fragments use nj_gfx_raster instead.
 * The colour/depth CPU buffers remain authoritative at every call boundary. */
typedef struct nj_gfx_triangles {
    nj_u4 version;
    nj_surface *surface;
    nj_s4 *depth;
    int depth_stride;
    int x, y, width, height;
    const nj_gfx_triangle_cmd *triangles;
    nj_u4 count;
} nj_gfx_triangles;

/* DONE: all CPU-visible colour/depth writes and *submitted committed atomically.
 * submitted counts accepted post-clipping/culling primitives, <= packet->count;
 * it is a work counter, not an occlusion query or a visible-triangle count.
 * Every other return: NO colour, depth or result writes; the caller may replay
 * its complete original software transaction. Context lifetime belongs to port. */
typedef int (*nj_gfx_triangles_fn)(void *ud, const nj_gfx_triangles *packet,
                                   nj_u4 *submitted);

/* Structural/value/alias validation only; no allocations or writes. */
int nj_gfx_triangles_validate(const nj_gfx_triangles *packet, const nj_u4 *submitted);

/* ------------------------------------------------------------------ */
/* Ordered lists: several packets committed as one transaction          */
/* ------------------------------------------------------------------ */
/*
 * A backend that implements lists can merge the fixed per-call cost (colour
 * and depth import, seeding, colour and depth readback) across many packets
 * that a caller already knows are consecutive draws into the same target.
 *
 * The transaction is exactly equivalent to calling the single-packet callback
 * for every packet in order, with the CPU colour/depth buffers authoritative
 * at the end of the LAST packet. Intermediate CPU states need not be written,
 * so a caller must not observe or mutate the target between the packets it
 * groups. Each packet keeps its own ROI and triangles; all packets must name
 * the same surface, depth buffer and depth stride. The caller keeps ownership
 * of every packet and of the packet pointer array; they are borrowed for this
 * call only. Textures are borrowed as in the single-packet interface.
 *
 * DONE: all colour/depth writes of all packets and *submitted are committed
 * atomically; *submitted is the sum of accepted primitives. Every other
 * return: no colour, depth or result writes at all, and the caller may replay
 * every packet in software in order. Ports without a list callback leave the
 * hook NULL; the caller then falls back to single-packet calls.
 */
#define NJ_GFX_TRIANGLES_LIST_VERSION 1u
#define NJ_GFX_TRIANGLES_LIST_MAX_PACKETS 256u
#define NJ_GFX_TRIANGLES_LIST_MAX_TOTAL 131072u
typedef struct nj_gfx_triangles_list {
    nj_u4 version;
    nj_surface *surface;
    nj_s4 *depth;
    int depth_stride;
    const nj_gfx_triangles *const *packets;
    nj_u4 count;
} nj_gfx_triangles_list;
typedef int (*nj_gfx_triangles_list_fn)(void *ud,
                                        const nj_gfx_triangles_list *list,
                                        nj_u4 *submitted);
int nj_gfx_triangles_list_validate(const nj_gfx_triangles_list *list,
                                   const nj_u4 *submitted);

/* ------------------------------------------------------------------ */
/* Persistent sessions: keep the GPU result resident across submissions */
/* ------------------------------------------------------------------ */
/*
 * A backend that implements sessions can keep one target rectangle resident on
 * the GPU between several triangle submissions and pay the CPU/GPU import,
 * seeding and readback only once. This is the optional architecture used by the
 * core journal for same-thread synchronous GL ports; ports without it keep the
 * single-packet and list contracts unchanged.
 *
 * begin() seeds the session rectangle from the current CPU colour/depth buffers
 * and returns DONE. While the session is open the backend owns the authoritative
 * result: the CPU colour/depth buffers are STALE, and the caller must not read
 * or write the target pixels (no 2D draw, read, present, save, target switch or
 * clear) until end() or abort() returns. Submissions during the session use the
 * single-packet or list callback and must stay inside the session rectangle;
 * their DONE only means "queued into the session" and must not be treated as a
 * CPU commit. A declined submission invalidates the session: the caller aborts
 * it and replays every submitted packet in software from the pre-session CPU
 * state.
 *
 * end() readbacks the session rectangle and commits CPU colour (and depth when
 * the session declared a depth buffer) atomically: DONE commits, any other
 * return writes nothing and the caller replays every packet in software.
 * abort() discards the session without touching CPU pixels. Both calls close
 * the session and leave the backend usable for the next transaction. The
 * caller must keep the surface, depth buffer and every submitted packet alive
 * until end()/abort() returns; packets are borrowed for the session.
 *
 * submitted on end() is the total accepted primitives for the whole session.
 * The rectangle must be inside the surface; x/y may be non-zero.
 */
#define NJ_GFX_TRIANGLES_SESSION_VERSION 1u
typedef struct nj_gfx_triangles_session {
    nj_u4 version;
    nj_surface *surface;
    nj_s4 *depth;
    int depth_stride;
    int x, y, width, height;
} nj_gfx_triangles_session;
typedef int (*nj_gfx_triangles_begin_fn)(void *ud,
                                         const nj_gfx_triangles_session *session);
typedef int (*nj_gfx_triangles_end_fn)(void *ud, nj_u4 *submitted);
typedef void (*nj_gfx_triangles_abort_fn)(void *ud);
#ifdef __cplusplus
}
#endif
#endif
