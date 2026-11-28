/* Optional headless Vulkan 1.0 triangle backend. One synchronous caller per renderer. */
#ifndef NJ_GFX_VULKAN_H
#define NJ_GFX_VULKAN_H
#include "nj_port.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Install an owned renderer on an initialized port before the VM starts. */
int nj_vk_renderer_attach(nj_port *port);
typedef struct nj_vk_renderer nj_vk_renderer;
nj_vk_renderer *nj_vk_renderer_create(void);
void nj_vk_renderer_destroy(nj_vk_renderer *);
int nj_vk_renderer_triangles(void *ud, const nj_gfx_triangles *packet, nj_u4 *submitted);
int nj_vk_renderer_triangles_list(void *ud, const nj_gfx_triangles_list *list, nj_u4 *submitted);
/* Optional persistent session: keeps the attachments resident across packets
 * and pays the colour/depth import and export once per session. */
int nj_vk_renderer_triangles_begin(void *ud, const nj_gfx_triangles_session *session);
int nj_vk_renderer_triangles_end(void *ud, nj_u4 *submitted);
void nj_vk_renderer_triangles_abort(void *ud);
/* Resident colour mirror: compose on the GPU and export once per present.
 * The mirror is a synchronous resource like the renderer: do not interleave it
 * with triangle sessions, and destroy every mirror before the renderer. */
typedef struct nj_vk_mirror nj_vk_mirror;
/* nj_vk_renderer_mirror_triangles() import bits: seed the mirror from the CPU
 * colour/depth buffers before drawing. A fresh mirror always imports both. */
#define NJ_VK_MIRROR_IMPORT_COLOUR 1
#define NJ_VK_MIRROR_IMPORT_DEPTH 2
nj_vk_mirror *nj_vk_renderer_mirror_create(void *ud, int width, int height);
void nj_vk_renderer_mirror_destroy(nj_vk_mirror *mirror);
int nj_vk_renderer_mirror_clear(nj_vk_mirror *mirror, nj_u4 argb);
/* Clear one rectangle of the resident mirror on the GPU. `clear_colour` and
 * `clear_depth` select which attachments the rectangle clears. Returns
 * nonzero when the rectangle was committed. */
int nj_vk_renderer_mirror_clear_rect(nj_vk_mirror *mirror, nj_u4 argb, int x, int y, int width,
                                     int height, int clear_colour, int clear_depth);
int nj_vk_renderer_mirror_fill(nj_vk_mirror *mirror, int x, int y, int width, int height, nj_u4 argb);
int nj_vk_renderer_mirror_export(nj_vk_mirror *mirror, const nj_surface *dst);
/* Seed and read back part of the mirror.  A fresh mirror (or one whose depth
 * has never been imported) falls back to seeding the whole frame; sub-rectangle
 * imports are otherwise packed and copied as a rectangle. */
int nj_vk_renderer_mirror_import_rect(nj_vk_mirror *mirror, const nj_surface *surface,
                                      nj_s4 *depth, int depth_stride, int x, int y, int width,
                                      int height, int mask);
int nj_vk_renderer_mirror_export_rect(nj_vk_mirror *mirror, const nj_surface *dst, int x, int y,
                                      int width, int height);
void *nj_vk_renderer_mirror_pixels(nj_vk_mirror *mirror);
int nj_vk_renderer_mirror_texture(nj_vk_mirror *mirror, const nj_surface *src, int sx, int sy,
                                  int sw, int sh, int dx, int dy, int dw, int dh, int filter,
                                  int blend);
int nj_vk_renderer_mirror_triangles(nj_vk_mirror *mirror, const nj_gfx_triangles_list *list,
                                    int import, int export_colour, nj_u4 *submitted);
#ifdef __cplusplus
}
#endif
#endif
