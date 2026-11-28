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
nj_vk_mirror *nj_vk_renderer_mirror_create(void *ud, int width, int height);
void nj_vk_renderer_mirror_destroy(nj_vk_mirror *mirror);
int nj_vk_renderer_mirror_clear(nj_vk_mirror *mirror, nj_u4 argb);
int nj_vk_renderer_mirror_fill(nj_vk_mirror *mirror, int x, int y, int width, int height, nj_u4 argb);
int nj_vk_renderer_mirror_export(nj_vk_mirror *mirror, const nj_surface *dst);
int nj_vk_renderer_mirror_texture(nj_vk_mirror *mirror, const nj_surface *src, int sx, int sy,
                                  int sw, int sh, int dx, int dy, int dw, int dh, int filter,
                                  int blend);
#ifdef __cplusplus
}
#endif
#endif
