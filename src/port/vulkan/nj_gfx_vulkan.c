/* Headless Vulkan 1.0. The command buffer only references private, reusable
 * storage. The sole caller-visible commit follows fence wait and invalidation. */
#include "nj2me/nj_gfx_vulkan.h"
#include <vulkan/vulkan.h>
#if defined(__ANDROID__)
#include <android/log.h>
#define VK_LOG(...) __android_log_print(ANDROID_LOG_INFO, "nj2me", __VA_ARGS__)
#else
#define VK_LOG(...) ((void)0)
#endif
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "tri_spv.inc"

#define MAX_PIXELS (8u * 1024u * 1024u)
#define MAX_TEXELS (4u * 1024u * 1024u)
#define MAX_VIEWS 64
#define PIPELINES 40 /* 5 blends x 8 combinations of alpha write/depth test/write */
#define VK_INIT(type) {.sType = VK_STRUCTURE_TYPE_##type}

typedef struct buffer {
    VkBuffer id;
    VkDeviceMemory memory;
    void *map;
    VkDeviceSize capacity;
    int coherent;
} buffer;
typedef struct attachment {
    VkImage id;
    VkImageView view;
    VkDeviceMemory memory;
} attachment;
typedef struct vertex {
    float position[3], uvq[3];
    uint8_t rgba[4];
} vertex;
typedef struct material {
    int32_t offset, width, height, flags, function, threshold, blend, fog;
    uint32_t fog_color;
    float near_dist, far_dist, density;
} material;
struct nj_vk_renderer {
    VkInstance instance;
    VkPhysicalDevice gpu;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory;
    VkDevice dev;
    uint32_t family;
    VkQueue queue;
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;
    VkShaderModule vs, fs;
    VkDescriptorSetLayout set_layout;
    VkPipelineLayout layout;
    VkRenderPass pass;
    VkRenderPass session_pass; /* keeps the attachments in TRANSFER_DST between packets */
    VkDescriptorPool descriptors;
    VkDescriptorSet set;
    VkPipeline pipelines[PIPELINES];
    attachment attachments[2];
    VkFramebuffer fb;
    VkFramebuffer session_fb;
    uint32_t width, height;
    uint32_t max_image_width, max_image_height;
    buffer stage, vertices, textures;
    /* What the pooled texture storage descriptor currently points at. */
    VkBuffer desc_buffer;
    VkDeviceSize desc_range;
    int desc_valid;
    int lost;
    /* Optional persistent triangle session: while open the attachments own the
     * authoritative rectangle and the CPU colour/depth buffers are stale until
     * end() exports them. All state lives here, never in a global. */
    nj_surface session_surface; /* copy: the caller's nj_surface may be a stack copy */
    nj_s4 *session_depth;
    int session_depth_stride;
    int session_x, session_y, session_width, session_height;
    int session_open, session_failed;
    int session_depth_written;
    nj_u4 session_submitted;
    void *session_infos; /* packet_info scratch, typed inside tri_draw.inc */
    unsigned session_infos_cap;
};
static const VkFormat formats[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_D32_SFLOAT};
static const VkImageUsageFlags usages[] = {
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT};
static const VkImageAspectFlags aspects[] = {VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_DEPTH_BIT};

static uint32_t memory_type(nj_vk_renderer *r, uint32_t bits, VkMemoryPropertyFlags require,
                            VkMemoryPropertyFlags prefer) {
    uint32_t i, fallback = UINT32_MAX;
    for (i = 0; i < r->memory.memoryTypeCount; ++i) {
        VkMemoryPropertyFlags f = r->memory.memoryTypes[i].propertyFlags;
        if (!(bits & (1u << i)) || (f & require) != require)
            continue;
        if ((f & prefer) == prefer)
            return i;
        if (fallback == UINT32_MAX)
            fallback = i;
    }
    return fallback;
}
static void release_buffer(nj_vk_renderer *r, buffer *b) {
    if (b->map)
        vkUnmapMemory(r->dev, b->memory);
    vkDestroyBuffer(r->dev, b->id, NULL);
    vkFreeMemory(r->dev, b->memory, NULL);
    memset(b, 0, sizeof *b);
}
static int reserve_buffer(nj_vk_renderer *r, buffer *b, VkDeviceSize bytes, VkBufferUsageFlags usage) {
    VkBufferCreateInfo info = VK_INIT(BUFFER_CREATE_INFO);
    VkMemoryAllocateInfo alloc = VK_INIT(MEMORY_ALLOCATE_INFO);
    VkMemoryRequirements req;
    if (b->capacity >= bytes)
        return 1;
    release_buffer(r, b);
    info.size = 4096;
    while (info.size < bytes)
        info.size *= 2;
    info.usage = usage;
    if (vkCreateBuffer(r->dev, &info, NULL, &b->id) != VK_SUCCESS)
        goto fail;
    vkGetBufferMemoryRequirements(r->dev, b->id, &req);
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex =
        memory_type(r, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                    (usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT
                                                               : VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (alloc.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(r->dev, &alloc, NULL, &b->memory) != VK_SUCCESS ||
        vkBindBufferMemory(r->dev, b->id, b->memory, 0) != VK_SUCCESS ||
        vkMapMemory(r->dev, b->memory, 0, VK_WHOLE_SIZE, 0, &b->map) != VK_SUCCESS)
        goto fail;
    b->coherent = (r->memory.memoryTypes[alloc.memoryTypeIndex].propertyFlags &
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    b->capacity = info.size;
    return 1;
fail:
    release_buffer(r, b);
    return 0;
}
static int sync_buffer(nj_vk_renderer *r, const buffer *b, int readback) {
    VkMappedMemoryRange range = VK_INIT(MAPPED_MEMORY_RANGE);
    if (b->coherent)
        return 1;
    range.memory = b->memory;
    range.size = VK_WHOLE_SIZE;
    return (readback ? vkInvalidateMappedMemoryRanges(r->dev, 1, &range)
                     : vkFlushMappedMemoryRanges(r->dev, 1, &range)) == VK_SUCCESS;
}
static void release_target(nj_vk_renderer *r) {
    int i;
    vkDestroyFramebuffer(r->dev, r->fb, NULL);
    vkDestroyFramebuffer(r->dev, r->session_fb, NULL);
    r->fb = r->session_fb = VK_NULL_HANDLE;
    r->session_open = 0;
    r->session_failed = 0;
    for (i = 0; i < 2; ++i) {
        attachment *a = &r->attachments[i];
        vkDestroyImageView(r->dev, a->view, NULL);
        vkDestroyImage(r->dev, a->id, NULL);
        vkFreeMemory(r->dev, a->memory, NULL);
        memset(a, 0, sizeof *a);
    }
    r->width = r->height = 0;
}
static int target(nj_vk_renderer *r, uint32_t w, uint32_t h) {
    VkFramebufferCreateInfo fb = VK_INIT(FRAMEBUFFER_CREATE_INFO);
    VkImageView views[2];
    uint32_t grow_w = w > r->width ? w : r->width, grow_h = h > r->height ? h : r->height;
    int i;
    if (r->width >= w && r->height >= h)
        return 1;
    if ((uint64_t)grow_w * grow_h <= MAX_PIXELS) {
        w = grow_w;
        h = grow_h;
    }
    release_target(r);
    for (i = 0; i < 2; ++i) {
        VkImageCreateInfo info = VK_INIT(IMAGE_CREATE_INFO);
        VkImageViewCreateInfo view = VK_INIT(IMAGE_VIEW_CREATE_INFO);
        VkMemoryAllocateInfo alloc = VK_INIT(MEMORY_ALLOCATE_INFO);
        VkMemoryRequirements req;
        attachment *a = &r->attachments[i];
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent = (VkExtent3D){w, h, 1};
        info.mipLevels = info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.usage = usages[i];
        info.format = formats[i];
        if (vkCreateImage(r->dev, &info, NULL, &a->id) != VK_SUCCESS)
            goto fail;
        vkGetImageMemoryRequirements(r->dev, a->id, &req);
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex =
            memory_type(r, req.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (alloc.memoryTypeIndex == UINT32_MAX ||
            vkAllocateMemory(r->dev, &alloc, NULL, &a->memory) != VK_SUCCESS ||
            vkBindImageMemory(r->dev, a->id, a->memory, 0) != VK_SUCCESS)
            goto fail;
        view.image = a->id;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = formats[i];
        view.subresourceRange = (VkImageSubresourceRange){aspects[i], 0, 1, 0, 1};
        if (vkCreateImageView(r->dev, &view, NULL, &a->view) != VK_SUCCESS)
            goto fail;
        views[i] = a->view;
    }
    fb.renderPass = r->pass;
    fb.attachmentCount = 2;
    fb.pAttachments = views;
    fb.width = w;
    fb.height = h;
    fb.layers = 1;
    if (vkCreateFramebuffer(r->dev, &fb, NULL, &r->fb) != VK_SUCCESS)
        goto fail;
    fb.renderPass = r->session_pass;
    if (vkCreateFramebuffer(r->dev, &fb, NULL, &r->session_fb) != VK_SUCCESS)
        goto fail;
    r->width = w;
    r->height = h;
    return 1;
fail:
    release_target(r);
    return 0;
}
static int choose_gpu(nj_vk_renderer *r) {
    VkPhysicalDevice devices[16];
    uint32_t n = 16, i;
    VkResult result = vkEnumeratePhysicalDevices(r->instance, &n, devices);
    VK_LOG("vulkan: enumerate result=%d count=%u", (int)result, n);
    if (result != VK_SUCCESS && result != VK_INCOMPLETE)
        return 0;
    for (i = 0; i < n; ++i) {
        VkQueueFamilyProperties families[32];
        VkFormatProperties format;
        uint32_t f = 32, j;
        vkGetPhysicalDeviceProperties(devices[i], &r->properties);
        VK_LOG("vulkan: device %u %s type=%d api=%u.%u", i, r->properties.deviceName,
               (int)r->properties.deviceType, r->properties.apiVersion >> 22,
               (r->properties.apiVersion >> 12) & 0x3ffu);
        if (r->properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
            VK_LOG("vulkan: device %u skipped, cpu", i);
            continue;
        }
        vkGetPhysicalDeviceFormatProperties(devices[i], formats[0], &format);
        if (!(format.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)) {
            VK_LOG("vulkan: device %u skipped, no blend on bgra8", i);
            continue;
        }
        r->max_image_width = r->max_image_height = UINT32_MAX;
        for (j = 0; j < 2; ++j) {
            VkImageFormatProperties props;
            if (vkGetPhysicalDeviceImageFormatProperties(devices[i], formats[j], VK_IMAGE_TYPE_2D,
                                                         VK_IMAGE_TILING_OPTIMAL, usages[j], 0,
                                                         &props) != VK_SUCCESS)
                break;
            if (props.maxExtent.width < r->max_image_width)
                r->max_image_width = props.maxExtent.width;
            if (props.maxExtent.height < r->max_image_height)
                r->max_image_height = props.maxExtent.height;
        }
        if (j != 2) {
            VK_LOG("vulkan: device %u skipped, formats or usages unsupported", i);
            continue;
        }
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &f, families);
        for (j = 0; j < f; ++j)
            if (families[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                r->gpu = devices[i];
                r->family = j;
                vkGetPhysicalDeviceMemoryProperties(r->gpu, &r->memory);
                return 1;
            }
        VK_LOG("vulkan: device %u skipped, no graphics queue", i);
    }
    return 0;
}
static int create_pass(nj_vk_renderer *r, VkImageLayout final_layout, VkRenderPass *out) {
    VkAttachmentDescription attachments[2] = {{0}};
    VkAttachmentReference color = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub = {0};
    VkSubpassDependency deps[2] = {{0}};
    VkRenderPassCreateInfo info = VK_INIT(RENDER_PASS_CREATE_INFO);
    const VkPipelineStageFlags stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkAccessFlags writes =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    int i;
    for (i = 0; i < 2; ++i) {
        attachments[i].format = formats[i];
        attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[i].initialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        attachments[i].finalLayout = final_layout;
    }
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &color;
    sub.pDepthStencilAttachment = &depth;
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[0].dstStageMask = stages;
    deps[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    deps[0].dstAccessMask =
        writes | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = stages;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[1].srcAccessMask = writes;
    deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    info.attachmentCount = 2;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &sub;
    info.dependencyCount = 2;
    info.pDependencies = deps;
    return vkCreateRenderPass(r->dev, &info, NULL, out) == VK_SUCCESS;
}
nj_vk_renderer *nj_vk_renderer_create(void) {
    nj_vk_renderer *r = calloc(1, sizeof *r);
    VkApplicationInfo app_info = VK_INIT(APPLICATION_INFO);
    VkInstanceCreateInfo instance = VK_INIT(INSTANCE_CREATE_INFO);
    VkDeviceQueueCreateInfo queue = VK_INIT(DEVICE_QUEUE_CREATE_INFO);
    VkDeviceCreateInfo dev = VK_INIT(DEVICE_CREATE_INFO);
    VkCommandPoolCreateInfo pool = VK_INIT(COMMAND_POOL_CREATE_INFO);
    VkCommandBufferAllocateInfo cmd = VK_INIT(COMMAND_BUFFER_ALLOCATE_INFO);
    VkFenceCreateInfo fence = VK_INIT(FENCE_CREATE_INFO);
    VkShaderModuleCreateInfo shader = VK_INIT(SHADER_MODULE_CREATE_INFO);
    VkDescriptorSetLayoutBinding binding = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                            VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    VkDescriptorSetLayoutCreateInfo set_layout = VK_INIT(DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    VkPipelineLayoutCreateInfo layout = VK_INIT(PIPELINE_LAYOUT_CREATE_INFO);
    VkPushConstantRange range = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(material)};
    VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo descriptors = VK_INIT(DESCRIPTOR_POOL_CREATE_INFO);
    VkDescriptorSetAllocateInfo set = VK_INIT(DESCRIPTOR_SET_ALLOCATE_INFO);
    float priority = 1;
    VkResult stage = VK_SUCCESS;
    const char *stage_name = "instance";
    if (!r)
        return NULL;
    /* A NULL pApplicationInfo leaves apiVersion 0; some Android Vulkan HALs (the
       emulator's gfxstream one) then enumerate no physical devices at all. */
    app_info.apiVersion = VK_API_VERSION_1_0;
    app_info.pApplicationName = "nano_j2me";
    app_info.applicationVersion = 1;
    app_info.pEngineName = "nano_j2me";
    app_info.engineVersion = 1;
    instance.pApplicationInfo = &app_info;
    stage = vkCreateInstance(&instance, NULL, &r->instance);
    if (stage != VK_SUCCESS) {
        VK_LOG("vulkan: vkCreateInstance failed (%d)", (int)stage);
        goto fail;
    }
    stage_name = "physical device";
    if (!choose_gpu(r))
        goto fail;
    queue.queueFamilyIndex = r->family;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    dev.queueCreateInfoCount = 1;
    dev.pQueueCreateInfos = &queue;
    stage_name = "device";
    if ((stage = vkCreateDevice(r->gpu, &dev, NULL, &r->dev)) != VK_SUCCESS)
        goto fail;
    vkGetDeviceQueue(r->dev, r->family, 0, &r->queue);
    pool.queueFamilyIndex = r->family;
    stage_name = "command pool";
    if ((stage = vkCreateCommandPool(r->dev, &pool, NULL, &r->pool)) != VK_SUCCESS)
        goto fail;
    cmd.commandPool = r->pool;
    cmd.commandBufferCount = 1;
    stage_name = "command buffer";
    stage = vkAllocateCommandBuffers(r->dev, &cmd, &r->cmd);
    if (stage != VK_SUCCESS)
        goto fail;
    stage_name = "fence";
    if ((stage = vkCreateFence(r->dev, &fence, NULL, &r->fence)) != VK_SUCCESS)
        goto fail;
    stage_name = "render pass";
    if (!create_pass(r, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, &r->pass) ||
        !create_pass(r, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &r->session_pass)) {
        stage = VK_ERROR_INITIALIZATION_FAILED;
        goto fail;
    }
    shader.codeSize = sizeof tri_vert;
    shader.pCode = tri_vert;
    stage_name = "vertex module";
    if ((stage = vkCreateShaderModule(r->dev, &shader, NULL, &r->vs)) != VK_SUCCESS)
        goto fail;
    shader.codeSize = sizeof tri_frag;
    shader.pCode = tri_frag;
    stage_name = "fragment module";
    if ((stage = vkCreateShaderModule(r->dev, &shader, NULL, &r->fs)) != VK_SUCCESS)
        goto fail;
    set_layout.bindingCount = 1;
    set_layout.pBindings = &binding;
    stage_name = "descriptor layout";
    if ((stage = vkCreateDescriptorSetLayout(r->dev, &set_layout, NULL, &r->set_layout)) != VK_SUCCESS)
        goto fail;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &r->set_layout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &range;
    stage_name = "pipeline layout";
    if ((stage = vkCreatePipelineLayout(r->dev, &layout, NULL, &r->layout)) != VK_SUCCESS)
        goto fail;
    descriptors.maxSets = descriptors.poolSizeCount = 1;
    descriptors.pPoolSizes = &size;
    stage_name = "descriptor pool";
    if ((stage = vkCreateDescriptorPool(r->dev, &descriptors, NULL, &r->descriptors)) != VK_SUCCESS)
        goto fail;
    set.descriptorPool = r->descriptors;
    set.descriptorSetCount = 1;
    set.pSetLayouts = &r->set_layout;
    stage_name = "descriptor set";
    if ((stage = vkAllocateDescriptorSets(r->dev, &set, &r->set)) != VK_SUCCESS)
        goto fail;
    VK_LOG("vulkan: renderer ready on %s", r->properties.deviceName);
    return r;
fail:
    VK_LOG("vulkan: %s setup failed (%d)", stage_name, (int)stage);
    nj_vk_renderer_destroy(r);
    return NULL;
}
void nj_vk_renderer_destroy(nj_vk_renderer *r) {
    if (!r)
        return;
    if (r->dev) {
        int i;
        vkDeviceWaitIdle(r->dev);
        vkDestroyCommandPool(r->dev, r->pool, NULL);
        vkDestroyDescriptorPool(r->dev, r->descriptors, NULL);
        release_target(r);
        release_buffer(r, &r->stage);
        release_buffer(r, &r->textures);
        release_buffer(r, &r->vertices);
        for (i = 0; i < PIPELINES; ++i)
            vkDestroyPipeline(r->dev, r->pipelines[i], NULL);
        vkDestroyPipelineLayout(r->dev, r->layout, NULL);
        vkDestroyDescriptorSetLayout(r->dev, r->set_layout, NULL);
        vkDestroyShaderModule(r->dev, r->vs, NULL);
        vkDestroyShaderModule(r->dev, r->fs, NULL);
        vkDestroyRenderPass(r->dev, r->session_pass, NULL);
        vkDestroyRenderPass(r->dev, r->pass, NULL);
        vkDestroyFence(r->dev, r->fence, NULL);
        vkDestroyDevice(r->dev, NULL);
    }
    free(r->session_infos);
    if (r->instance)
        vkDestroyInstance(r->instance, NULL);
    free(r);
}
#include "tri_draw.inc"

#if NJ_ENABLE_GFX_ACCEL
static void destroy_owned_renderer(void *ud) { nj_vk_renderer_destroy(ud); }
#endif
int nj_vk_renderer_attach(nj_port *port) {
#if NJ_ENABLE_GFX_ACCEL
    nj_vk_renderer *r;
    if (!port || port->gfx_hd_destroy)
        return 0;
    r = nj_vk_renderer_create();
    if (!r)
        return 0;
    port->gfx_hd_triangles = port->gfx_triangles = nj_vk_renderer_triangles;
    port->gfx_hd_triangles_ud = port->gfx_triangles_ud = r;
    /* The default is the exact single-packet path with the journal off.  A
     * multi-packet list transaction produced unreproducible pixels at 1x and
     * 8x even when the recorded packet stream and the imported colour/depth
     * were bit-identical, and it was slower than the single-packet path.
     *
     * The persistent session path is the remaining lever on the 8x colour and
     * depth round trip: it keeps the attachments resident across submissions
     * and pays import/export once per session instead of once per packet. It
     * is still opt-in (NJ_VK_SESSION=1) until the deterministic gates and the
     * timing comparison clear it. */
    port->gfx_triangles_list = NULL;
    port->gfx_triangles_list_ud = NULL;
    port->gfx_hd_journal = 0;
    port->gfx_triangles_begin = NULL;
    port->gfx_triangles_end = NULL;
    port->gfx_triangles_abort = NULL;
    port->gfx_triangles_session_ud = NULL;
    port->gfx_triangles_auto = 0;
    {
        const char *session = getenv("NJ_VK_SESSION");
        if (session && session[0] == '1') {
            port->gfx_triangles_list = nj_vk_renderer_triangles_list;
            port->gfx_triangles_list_ud = r;
            port->gfx_triangles_begin = nj_vk_renderer_triangles_begin;
            port->gfx_triangles_end = nj_vk_renderer_triangles_end;
            port->gfx_triangles_abort = nj_vk_renderer_triangles_abort;
            port->gfx_triangles_session_ud = r;
            port->gfx_hd_journal = 1;
        }
    }
    port->gfx_hd_destroy = destroy_owned_renderer;
    return 1;
#else
    (void)port;
    return 0;
#endif
}
