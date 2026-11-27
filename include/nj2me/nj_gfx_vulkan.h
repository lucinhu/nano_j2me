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
#ifdef __cplusplus
}
#endif
#endif
