#ifndef NJ_FSR2_VK_H
#define NJ_FSR2_VK_H

#include "nj_fsr2.h"
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Optional API for GPU producers. All handles are borrowed for ctx's lifetime.
 * Submit producer/consumer work on this queue and externally synchronize host
 * access to it. The producer must make its writes visible to compute shaders. */
typedef struct NjFsr2VkDevice {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family;
} NjFsr2VkDevice;

typedef struct NjFsr2VkImage {
    VkImage image;
    VkImageView view;
} NjFsr2VkImage;

typedef struct NjFsr2VkFrame {
    /* One mip/layer, optimal tiled, 2D, same dimensions as nj_fsr2_create.
     * Input color RGBA8_UNORM, depth R32_SFLOAT, motion R32G32_SFLOAT:
     * VK_IMAGE_USAGE_SAMPLED_BIT in SHADER_READ_ONLY_OPTIMAL layout.
     * Output RGBA8_UNORM: VK_IMAGE_USAGE_STORAGE_BIT in GENERAL layout.
     * Images remain in the indicated layouts after dispatch. */
    NjFsr2VkImage color, depth, motion, output;
} NjFsr2VkFrame;

int nj_fsr2_vk_get_device(NjFsr2 *ctx, NjFsr2VkDevice *device);
/* Synchronous. Only scalar fields of frame are used. No CPU transfers. */
int nj_fsr2_vk_dispatch(NjFsr2 *ctx, const NjFsr2Frame *frame,
                        const NjFsr2VkFrame *images, char *error, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
