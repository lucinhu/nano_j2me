/* Headless Vulkan 1.0. The command buffer only references private, reusable
 * storage. The sole caller-visible commit follows fence wait and invalidation. */
#include "nj2me/nj_gfx_vulkan.h"
#include <vulkan/vulkan.h>
#if defined(__ANDROID__)
#include <android/log.h>
#include <sys/system_properties.h>
#include <android/native_window.h>
#include <vulkan/vulkan_android.h>
#define VK_LOG(...) __android_log_print(ANDROID_LOG_INFO, "nj2me", __VA_ARGS__)
#else
#define VK_LOG(...) ((void)0)
#endif
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "tri_spv.inc"

static double vk_prof_now(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec * 1e-6;
}

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
/* A physical HD target that stays on the GPU between CPU observations. It is
 * matched to the CPU buffer it exports to; the VM calls the mirror sync hook
 * before it draws on that buffer and before it presents it. */
typedef struct nj_vk_resident {
    nj_vk_mirror *mirror;
    void *pixels;      /* the CPU-visible pointer, i.e. the mirror stage map */
    nj_surface *surface; /* whose pixels were redirected, to restore on release */
    void *cpu_pixels;  /* the surface's original allocation */
    int cpu_stride;
    int width, height;
} nj_vk_resident;

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
    /* Retained sessions (contract version 2): after a successful end() with
     * `retain` the attachments still hold the exported result for
     * `session_surface` and sit in TRANSFER_DST layout, so the next begin()
     * may import only the caller's dirty rectangles. */
    int session_keep;     /* end() must keep the result and relayout back */
    int session_retained; /* attachments currently hold a reusable result */
    nj_u4 session_submitted;
    void *session_infos; /* packet_info scratch, typed inside tri_draw.inc */
    unsigned session_infos_cap;
    /* NJ_VK_PROFILE=1 reports where the session time goes. */
    double prof_import, prof_export, prof_render;
    unsigned prof_sessions;
    /* GPU-resident HD mirror (opt-in): the physical target accumulates M3G
     * draws and GPU 2D until the CPU needs the pixels. */
    nj_vk_resident *resident;
    int resident_dirty;       /* GPU colour differs from the CPU copy */
    int resident_cpu_touched; /* the CPU may have written since the last import */
    int resident_depth_dirty; /* GPU depth holds draws the CPU keys do not */
    int resident_repaint;     /* the GPU fully repainted the mirror since the last export */
    /* Resident-path profile timers (NJ_VK_PROFILE): where the frame time goes. */
    double resident_prof_export, resident_prof_draw, resident_prof_clear, resident_prof_image;
    double resident_prof_copy, resident_prof_sync;
#if defined(__ANDROID__)
    /* Native-window presentation: the mirror is blitted into a swapchain image
     * and handed to the compositor, so the CPU never sees the HD frame. */
    int present_instance_ext;
    int present_device_ext;
    void *present_window;
    PFN_vkCreateAndroidSurfaceKHR present_create_surface;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR present_surface_support;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR present_surface_caps;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR present_surface_formats;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR present_surface_modes;
    PFN_vkCreateSwapchainKHR present_create_swapchain;
    PFN_vkDestroySwapchainKHR present_destroy_swapchain;
    PFN_vkGetSwapchainImagesKHR present_get_images;
    PFN_vkAcquireNextImageKHR present_acquire;
    PFN_vkQueuePresentKHR present_queue;
    VkSurfaceKHR present_surface;
    VkSwapchainKHR present_swapchain;
    VkImage *present_images;
    uint32_t present_count;
    VkFormat present_format;
    VkColorSpaceKHR present_color_space;
    VkExtent2D present_extent;
    VkSemaphore present_semaphore;
    int present_ready;
    int present_stats;
    double present_prof_acquire, present_prof_import, present_prof_blit, present_prof_queue;
    unsigned present_prof_frames;
    unsigned present_attempts;
    /* CPU-drawn frames are uploaded at the size they are shown, not at the
     * full HD mirror resolution: the nearest-neighbour shrink keeps the same
     * sampling as the GPU blit did, and takes a quarter of the bandwidth at
     * 2x. Set NJ_VK_PRESENT_SMALL=0 (debug.nj2me.vk_present_small=0) to send
     * the full frame instead. */
    int present_small_enabled;
    nj_vk_mirror *present_small;
    int present_small_w;
    int present_small_h;
    nj_u1 *present_scratch;
    unsigned present_small_frames;
#endif
    unsigned resident_prof_exports, resident_prof_draws;
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
    r->session_keep = 0;
    r->session_retained = 0;
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
    {
        /* Presentation is experimental on Android; debug.nj2me.vk_present=0
         * turns it off at runtime so the old present path can be compared. */
        int want_present = 1;
        const char *present_env = getenv("NJ_VK_PRESENT");
        if (present_env && present_env[0] == '0')
            want_present = 0;
#if defined(__ANDROID__)
        {
            char prop[PROP_VALUE_MAX];
            if (__system_property_get("debug.nj2me.vk_present", prop) > 0 && prop[0] == '0')
                want_present = 0;
        }
#endif
#if defined(__ANDROID__)
        r->present_instance_ext = want_present ? -1 : 0; /* -1: allowed, decide below */
#else
        (void)want_present;
#endif
    }
    /* A NULL pApplicationInfo leaves apiVersion 0; some Android Vulkan HALs (the
       emulator's gfxstream one) then enumerate no physical devices at all. */
    app_info.apiVersion = VK_API_VERSION_1_0;
    app_info.pApplicationName = "nano_j2me";
    app_info.applicationVersion = 1;
    app_info.pEngineName = "nano_j2me";
    app_info.engineVersion = 1;
    instance.pApplicationInfo = &app_info;
#if defined(__ANDROID__)
    /* These arrays hold the extension name pointers handed to vkCreateInstance,
       so they must live in this scope, not in the lookup block below. */
    const char *want[2];
    const char *have[2];
    if (r->present_instance_ext < 0) {
        /* Presentation needs VK_KHR_surface plus the Android surface extension;
         * the old GL path presented through EGL and needed neither. */
        uint32_t total = 0, i, j, found = 0;
        want[0] = VK_KHR_SURFACE_EXTENSION_NAME;
        want[1] = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
        if (vkEnumerateInstanceExtensionProperties(NULL, &total, NULL) == VK_SUCCESS && total) {
            VkExtensionProperties *list = (VkExtensionProperties *)malloc(total * sizeof *list);
            if (list &&
                vkEnumerateInstanceExtensionProperties(NULL, &total, list) == VK_SUCCESS) {
                for (j = 0; j < 2; ++j)
                    for (i = 0; i < total; ++i)
                        if (strcmp(list[i].extensionName, want[j]) == 0) {
                            have[found++] = want[j];
                            break;
                        }
            }
            free(list);
        }
        if (found == 2) {
            instance.enabledExtensionCount = found;
            instance.ppEnabledExtensionNames = have;
            r->present_instance_ext = 1;
        } else {
            r->present_instance_ext = 0;
        }
        VK_LOG("vulkan: surface extensions=%u", found);
    } else {
        r->present_instance_ext = 0;
    }
#endif
    stage = vkCreateInstance(&instance, NULL, &r->instance);
    if (stage != VK_SUCCESS) {
        VK_LOG("vulkan: vkCreateInstance failed (%d)", (int)stage);
        goto fail;
    }
    stage_name = "physical device";
#if defined(__ANDROID__)
    if (r->present_instance_ext) {
        r->present_create_surface = (PFN_vkCreateAndroidSurfaceKHR)vkGetInstanceProcAddr(
            r->instance, "vkCreateAndroidSurfaceKHR");
        r->present_surface_support = (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)vkGetInstanceProcAddr(
            r->instance, "vkGetPhysicalDeviceSurfaceSupportKHR");
        r->present_surface_caps =
            (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)vkGetInstanceProcAddr(
                r->instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        r->present_surface_formats =
            (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)vkGetInstanceProcAddr(
                r->instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
        r->present_surface_modes =
            (PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)vkGetInstanceProcAddr(
                r->instance, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    }
#endif
    if (!choose_gpu(r))
        goto fail;
    queue.queueFamilyIndex = r->family;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    dev.queueCreateInfoCount = 1;
    dev.pQueueCreateInfos = &queue;
    stage_name = "device";
#if defined(__ANDROID__)
    /* The pointer handed to vkCreateDevice below must stay alive until then. */
    const char *const swap_ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    {
        uint32_t total = 0, i;
        int found = 0;
        if (r->present_instance_ext &&
            vkEnumerateDeviceExtensionProperties(r->gpu, NULL, &total, NULL) == VK_SUCCESS &&
            total) {
            VkExtensionProperties *list = (VkExtensionProperties *)malloc(total * sizeof *list);
            if (list &&
                vkEnumerateDeviceExtensionProperties(r->gpu, NULL, &total, list) == VK_SUCCESS) {
                for (i = 0; i < total; ++i)
                    if (strcmp(list[i].extensionName, swap_ext) == 0) {
                        found = 1;
                        break;
                    }
            }
            free(list);
        }
        VK_LOG("vulkan: swapchain extension found=%d total=%u", found, total);
        if (found) {
            dev.enabledExtensionCount = 1;
            dev.ppEnabledExtensionNames = &swap_ext;
            r->present_device_ext = 1;
        }
    }
#endif
    if ((stage = vkCreateDevice(r->gpu, &dev, NULL, &r->dev)) != VK_SUCCESS)
        goto fail;
    vkGetDeviceQueue(r->dev, r->family, 0, &r->queue);
#if defined(__ANDROID__)
    if (r->present_device_ext) {
        r->present_acquire =
            (PFN_vkAcquireNextImageKHR)vkGetDeviceProcAddr(r->dev, "vkAcquireNextImageKHR");
        r->present_queue =
            (PFN_vkQueuePresentKHR)vkGetDeviceProcAddr(r->dev, "vkQueuePresentKHR");
        r->present_create_swapchain =
            (PFN_vkCreateSwapchainKHR)vkGetDeviceProcAddr(r->dev, "vkCreateSwapchainKHR");
        r->present_destroy_swapchain =
            (PFN_vkDestroySwapchainKHR)vkGetDeviceProcAddr(r->dev, "vkDestroySwapchainKHR");
        r->present_get_images =
            (PFN_vkGetSwapchainImagesKHR)vkGetDeviceProcAddr(r->dev, "vkGetSwapchainImagesKHR");
    }
#endif
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
    {
        /* Probe for the presentation-interop path: a shared Android hardware
         * buffer would let present consume the resident mirror without the
         * GPU-to-host readback. */
        uint32_t count = 0, i;
        int ahb = 0, extmem = 0;
        VkExtensionProperties *exts = NULL;
        if (vkEnumerateDeviceExtensionProperties(r->gpu, NULL, &count, NULL) == VK_SUCCESS &&
            count) {
            exts = (VkExtensionProperties *)malloc((size_t)count * sizeof *exts);
            if (exts && vkEnumerateDeviceExtensionProperties(r->gpu, NULL, &count, exts) ==
                            VK_SUCCESS) {
                for (i = 0; i < count; ++i) {
                    if (strcmp(exts[i].extensionName,
                               "VK_ANDROID_external_memory_android_hardware_buffer") == 0)
                        ahb = 1;
                    if (strcmp(exts[i].extensionName, "VK_KHR_external_memory") == 0)
                        extmem = 1;
                }
            }
            free(exts);
        }
        VK_LOG("vulkan: device extensions=%u ahb=%d extmem=%d", count, ahb, extmem);
    }
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
        if (r->resident) {
            if (r->resident->mirror)
                nj_vk_renderer_mirror_destroy(r->resident->mirror);
            free(r->resident);
            r->resident = NULL;
        }
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
#if defined(__ANDROID__)
        if (r->present_swapchain != VK_NULL_HANDLE && r->present_destroy_swapchain)
            r->present_destroy_swapchain(r->dev, r->present_swapchain, NULL);
        r->present_swapchain = VK_NULL_HANDLE;
        free(r->present_images);
        r->present_images = NULL;
        if (r->present_semaphore != VK_NULL_HANDLE)
            vkDestroySemaphore(r->dev, r->present_semaphore, NULL);
        r->present_semaphore = VK_NULL_HANDLE;
#endif
        vkDestroyFence(r->dev, r->fence, NULL);
        vkDestroyDevice(r->dev, NULL);
    }
    if (getenv("NJ_VK_PROFILE")) {
        fprintf(stderr, "[vkprof] sessions=%u import=%.3f export=%.3f render=%.3f\n",
                r->prof_sessions, r->prof_import, r->prof_export, r->prof_render);
        fprintf(stderr,
                "[vkprof] resident exports=%u export=%.3f (copy=%.3f sync=%.3f) draws=%u "
                "draw=%.3f clear=%.3f image=%.3f\n",
                r->resident_prof_exports, r->resident_prof_export, r->resident_prof_copy,
                r->resident_prof_sync, r->resident_prof_draws, r->resident_prof_draw,
                r->resident_prof_clear, r->resident_prof_image);
    }
    free(r->session_infos);
#if defined(__ANDROID__)
    if (r->present_surface != VK_NULL_HANDLE && r->instance)
        vkDestroySurfaceKHR(r->instance, r->present_surface, NULL);
#endif
    if (r->instance)
        vkDestroyInstance(r->instance, NULL);
    free(r);
}
#include "tri_draw.inc"

/* --- GPU-resident HD mirror ------------------------------------------------ */
static void resident_release(nj_vk_renderer *r) {
    if (!r || !r->resident) return;
    /* Restore the real allocation only while the surface still points at our
     * readback buffer; a released HD target may already have been reused. */
    if (r->resident->surface && r->resident->cpu_pixels &&
        r->resident->surface->pixels == r->resident->pixels) {
        r->resident->surface->pixels = r->resident->cpu_pixels;
        r->resident->surface->stride = r->resident->cpu_stride;
    }
    if (r->resident->mirror) nj_vk_renderer_mirror_destroy(r->resident->mirror);
    free(r->resident);
    r->resident = NULL;
    r->resident_dirty = 0;
    r->resident_cpu_touched = 0;
    r->resident_depth_dirty = 0;
    r->resident_repaint = 0;
}
static nj_vk_resident *resident_ensure(nj_vk_renderer *r, nj_surface *surface) {
    nj_vk_resident *res = r->resident;
    if (!r || !surface || !surface->pixels) return NULL;
    if (res && res->pixels == surface->pixels && res->width == surface->width &&
        res->height == surface->height)
        return res;
    resident_release(r);
    if (surface->format != NJ_PIXFMT_ARGB8888 || surface->bpp != 4 || surface->width < 16 ||
        surface->height < 16)
        return NULL;
    res = (nj_vk_resident *)calloc(1, sizeof *res);
    if (!res) return NULL;
    res->mirror = nj_vk_renderer_mirror_create(r, surface->width, surface->height);
    if (!res->mirror) {
        free(res);
        return NULL;
    }
    /* Keep the CPU copy and the readback buffer as one allocation: exports
     * then skip the host copy, and 2D writes land where the next import
     * reads.  Only packed ARGB rows can be aliased. */
    {
        void *stage = nj_vk_renderer_mirror_pixels(res->mirror);
        if (stage && surface->stride == surface->width * 4) {
            res->surface = surface;
            res->cpu_pixels = surface->pixels;
            res->cpu_stride = surface->stride;
            surface->pixels = stage;
        }
    }
    res->pixels = surface->pixels;
    res->width = surface->width;
    res->height = surface->height;
    r->resident = res;
    return res;
}
static void resident_drop(void *ud, nj_surface *target) {
    nj_vk_renderer *r = (nj_vk_renderer *)ud;
    if (r && r->resident && target && r->resident->surface == target)
        resident_release(r);
}
static int resident_sync(void *ud, nj_surface *target) {
    nj_vk_renderer *r = (nj_vk_renderer *)ud;
    nj_vk_resident *res;
    if (!r) return 0;
    res = r->resident;
    if (!res || !target || res->pixels != target->pixels) return 1;
    if (r->resident_dirty) {
        double t0 = vk_prof_now();
        if (!nj_vk_renderer_mirror_export(res->mirror, target)) {
            r->lost = 1;
            return 0;
        }
        r->resident_prof_export += vk_prof_now() - t0;
        r->resident_prof_exports++;
        r->resident_dirty = 0;
    }
    r->resident_repaint = 0;
    r->resident_cpu_touched = 1;
    return 1;
}
static int resident_clear(void *ud, nj_surface *target, nj_u4 argb, int x, int y, int width,
                          int height, int clear_colour, int clear_depth) {
    nj_vk_renderer *r = (nj_vk_renderer *)ud;
    nj_vk_resident *res;
    int full;
    if (!r || !target) return 0;
    res = resident_ensure(r, target);
    if (!res) return 0;
    full = x == 0 && y == 0 && width == res->width && height == res->height;
    if (!res->mirror->ready) {
        /* Define both attachments, then seed the colour from the CPU frame so a
           partial clear keeps everything outside its rectangle. */
        if (!nj_vk_renderer_mirror_clear(res->mirror, 0)) {
            r->lost = 1;
            return 0;
        }
        if (!(full && clear_colour && clear_depth) &&
            !mirror_import(res->mirror, target, NULL, 0, NJ_VK_MIRROR_IMPORT_COLOUR)) {
            r->lost = 1;
            return 0;
        }
    }
    {
        double t0 = vk_prof_now();
        int ok = full && clear_colour && clear_depth
                     ? nj_vk_renderer_mirror_clear(res->mirror, argb)
                     : nj_vk_renderer_mirror_clear_rect(res->mirror, argb, x, y, width, height,
                                                        clear_colour, clear_depth);
        r->resident_prof_clear += vk_prof_now() - t0;
        if (!ok) {
            r->lost = 1;
            return 0;
        }
    }
    r->resident_dirty = 1;
    r->resident_cpu_touched = 0;
    if (full && clear_colour) r->resident_repaint = 1;
    if (clear_depth) r->resident_depth_dirty = 0;
    return 1;
}
static int resident_image(void *ud, nj_surface *target, const void *pixels, int width, int height,
                          int bpp, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh) {
    nj_vk_renderer *r = (nj_vk_renderer *)ud;
    nj_vk_resident *res;
    nj_surface src;
    if (!r || !target || !pixels || width <= 0 || height <= 0 || bpp < 2 || bpp > 4 || sw <= 0 ||
        sh <= 0 || dw <= 0 || dh <= 0)
        return 0;
    res = resident_ensure(r, target);
    if (!res) return 0;
    src.pixels = (void *)pixels;
    src.width = width;
    src.height = height;
    src.stride = width * bpp;
    src.format = bpp == 4 ? NJ_PIXFMT_ARGB8888 : bpp == 3 ? NJ_PIXFMT_RGB888 : NJ_PIXFMT_RGB565;
    src.bpp = bpp;
    if (!res->mirror->ready) {
        if (!nj_vk_renderer_mirror_clear(res->mirror, 0) ||
            !mirror_import(res->mirror, target, NULL, 0, NJ_VK_MIRROR_IMPORT_COLOUR)) {
            r->lost = 1;
            return 0;
        }
    }
    {
        double t0 = vk_prof_now();
        int ok = nj_vk_renderer_mirror_texture(res->mirror, &src, sx, sy, sw, sh, dx, dy, dw, dh,
                                               208, 68);
        r->resident_prof_image += vk_prof_now() - t0;
        if (!ok) {
            r->lost = 1;
            return 0;
        }
    }
    r->resident_dirty = 1;
    r->resident_cpu_touched = 0;
    /* A replace quad that covers the whole mirror makes the CPU copy stale by
       construction, so the next triangle batch need not import it. */
    if (dx <= 0 && dy <= 0 && dx + dw >= res->width && dy + dh >= res->height)
        r->resident_repaint = 1;
    return 1;
}
static int resident_list_common(void *ud, const nj_gfx_triangles_list *l, nj_u4 *submitted,
                                int checked) {
    nj_vk_renderer *r = (nj_vk_renderer *)ud;
    nj_vk_resident *res;
    nj_u4 import = 0;
    if (!r || !l || !l->surface || !submitted) return NJ_GFX_ACCEL_DECLINE;
    if (l->surface->format != NJ_PIXFMT_ARGB8888 || l->surface->bpp != 4)
        return checked ? nj_vk_renderer_triangles_list(r, l, submitted)
                       : triangles_list_trusted(r, l, submitted);
    res = resident_ensure(r, l->surface);
    if (!res)
        return checked ? nj_vk_renderer_triangles_list(r, l, submitted)
                       : triangles_list_trusted(r, l, submitted);
    if (r->resident_cpu_touched) {
        /* A full GPU clear/background replace already overwrote the stale CPU
           copy, so importing it back would just restore the previous frame. */
        if (!r->resident_repaint) import = NJ_VK_MIRROR_IMPORT_COLOUR;
        if (!r->resident_depth_dirty) import |= NJ_VK_MIRROR_IMPORT_DEPTH;
    }
    {
        double t0 = vk_prof_now();
        int ok = checked ? nj_vk_renderer_mirror_triangles(res->mirror, l, (int)import, 0, submitted)
                         : mirror_triangles_trusted(res->mirror, l, (int)import, 0, submitted);
        r->resident_prof_draw += vk_prof_now() - t0;
        r->resident_prof_draws++;
        if (!ok) {
            r->lost = 1;
            return NJ_GFX_ACCEL_DECLINE;
        }
    }
    r->resident_dirty = 1;
    r->resident_cpu_touched = 0;
    r->resident_depth_dirty = 1;
    return NJ_GFX_ACCEL_DONE;
}
static int resident_list(void *ud, const nj_gfx_triangles_list *l, nj_u4 *submitted) {
    return resident_list_common(ud, l, submitted, 1);
}
static int resident_list_trusted(void *ud, const nj_gfx_triangles_list *l, nj_u4 *submitted) {
    return resident_list_common(ud, l, submitted, 0);
}

#if NJ_ENABLE_GFX_ACCEL
static void destroy_owned_renderer(void *ud) { nj_vk_renderer_destroy(ud); }
#endif
/* --- Native-window presentation ----------------------------------------- */
/* The resident mirror goes straight into a swapchain image: no readback, no
 * CPU scaling, no GL upload.  A device without the surface/swapchain
 * extensions, or a surface whose formats cannot represent the mirror, leaves
 * the hooks returning zero and the old present path keeps working. */
static void present_release_swapchain(nj_vk_renderer *r) {
#if defined(__ANDROID__)
    if (!r)
        return;
    if (r->present_swapchain != VK_NULL_HANDLE && r->present_destroy_swapchain)
        r->present_destroy_swapchain(r->dev, r->present_swapchain, NULL);
    r->present_swapchain = VK_NULL_HANDLE;
    free(r->present_images);
    r->present_images = NULL;
    r->present_count = 0;
    r->present_ready = 0;
#else
    (void)r;
#endif
}

void nj_vk_renderer_present_detach(void *renderer) {
    nj_vk_renderer *r = (nj_vk_renderer *)renderer;
    if (!r)
        return;
    present_release_swapchain(r);
#if defined(__ANDROID__)
    if (r->present_surface != VK_NULL_HANDLE && r->instance) {
        vkDestroySurfaceKHR(r->instance, r->present_surface, NULL);
        r->present_surface = VK_NULL_HANDLE;
    }
    r->present_window = NULL;
#endif
}

#if defined(__ANDROID__)
static int present_build_swapchain(nj_vk_renderer *r, int width, int height) {
    VkSurfaceCapabilitiesKHR caps;
    VkSurfaceFormatKHR *list = NULL;
    VkPresentModeKHR *modes = NULL;
    VkSwapchainCreateInfoKHR info = VK_INIT(SWAPCHAIN_CREATE_INFO_KHR);
    VkExtent2D extent;
    VkFormat want = formats[0];
    uint32_t count = 0, mode_count = 0, i, mode = VK_PRESENT_MODE_FIFO_KHR;
    VkResult rc;
    if (!r->present_surface_caps || !r->present_surface_formats || !r->present_create_swapchain ||
        !r->present_get_images || !r->present_acquire || !r->present_queue)
        return 0;
    if (r->present_surface_caps(r->gpu, r->present_surface, &caps) != VK_SUCCESS)
        return 0;
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return 0;
    if (r->present_surface_formats(r->gpu, r->present_surface, &count, NULL) != VK_SUCCESS ||
        !count)
        return 0;
    list = (VkSurfaceFormatKHR *)malloc((size_t)count * sizeof *list);
    if (!list)
        return 0;
    if (r->present_surface_formats(r->gpu, r->present_surface, &count, list) != VK_SUCCESS)
        count = 0;
    /* The mirror renders into the platform's preferred 8-bit format; surfaces
       sometimes only offer the byte-swapped twin. vkCmdBlitImage maps channels
       by component, so both are safe blit destinations when the format
       supports it. */
    {
        const VkFormat candidates[2] = {want, VK_FORMAT_R8G8B8A8_UNORM};
        int k;
        r->present_format = VK_FORMAT_UNDEFINED;
        for (k = 0; k < 2 && r->present_format == VK_FORMAT_UNDEFINED; ++k) {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(r->gpu, candidates[k], &props);
            if (!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
                continue;
            for (i = 0; i < count; ++i)
                if (list[i].format == candidates[k]) {
                    r->present_format = list[i].format;
                    r->present_color_space = list[i].colorSpace;
                    break;
                }
        }
    }
    free(list);
    if (r->present_format == VK_FORMAT_UNDEFINED) {
        VK_LOG("vulkan: swapchain offers no blittable 8-bit format (%u formats); CPU present stays",
               count);
        return 0;
    }
    if (r->present_surface_modes &&
        r->present_surface_modes(r->gpu, r->present_surface, &mode_count, NULL) == VK_SUCCESS &&
        mode_count) {
        modes = (VkPresentModeKHR *)malloc((size_t)mode_count * sizeof *modes);
        if (modes &&
            r->present_surface_modes(r->gpu, r->present_surface, &mode_count, modes) == VK_SUCCESS) {
            for (i = 0; i < mode_count; ++i)
                if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
                    mode = VK_PRESENT_MODE_MAILBOX_KHR;
                    break;
                }
        }
        free(modes);
    }
    extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = (uint32_t)(width > 0 ? width : 1);
        extent.height = (uint32_t)(height > 0 ? height : 1);
    }
    if (extent.width < caps.minImageExtent.width)
        extent.width = caps.minImageExtent.width;
    if (extent.height < caps.minImageExtent.height)
        extent.height = caps.minImageExtent.height;
    if (extent.width > caps.maxImageExtent.width)
        extent.width = caps.maxImageExtent.width;
    if (extent.height > caps.maxImageExtent.height)
        extent.height = caps.maxImageExtent.height;
    info.surface = r->present_surface;
    info.minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount && info.minImageCount > caps.maxImageCount)
        info.minImageCount = caps.maxImageCount;
    info.imageFormat = r->present_format;
    info.imageColorSpace = r->present_color_space;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = (VkPresentModeKHR)mode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = VK_NULL_HANDLE;
    rc = r->present_create_swapchain(r->dev, &info, NULL, &r->present_swapchain);
    if (rc != VK_SUCCESS) {
        VK_LOG("vulkan: swapchain create failed (%d)", (int)rc);
        return 0;
    }
    if (r->present_get_images(r->dev, r->present_swapchain, &r->present_count, NULL) != VK_SUCCESS ||
        !r->present_count)
        return 0;
    r->present_images = (VkImage *)malloc((size_t)r->present_count * sizeof *r->present_images);
    if (!r->present_images ||
        r->present_get_images(r->dev, r->present_swapchain, &r->present_count, r->present_images) !=
            VK_SUCCESS)
        return 0;
    r->present_extent = extent;
    r->present_ready = 1;
    VK_LOG("vulkan: present swapchain %ux%u format=%d mode=%d", extent.width, extent.height,
           (int)r->present_format, (int)mode);
    return 1;
}
#endif

int nj_vk_renderer_present_attach(void *renderer, void *native_window, int width, int height) {
    nj_vk_renderer *r = (nj_vk_renderer *)renderer;
#if defined(__ANDROID__)
    {
        const char *env = getenv("NJ_VK_PRESENT_STATS");
        char prop[PROP_VALUE_MAX];
        r->present_stats = env && env[0] == '1';
        if (__system_property_get("debug.nj2me.vk_present_stats", prop) > 0 && prop[0] == '1')
            r->present_stats = 1;
        if (r->present_stats) VK_LOG("vulkan: present stats enabled");
    }
    {
        const char *senv = getenv("NJ_VK_PRESENT_SMALL");
        char sprop[PROP_VALUE_MAX];
        int small = 1;
        if (senv && senv[0] == '0') small = 0;
        if (__system_property_get("debug.nj2me.vk_present_small", sprop) > 0 && sprop[0] == '0')
            small = 0;
        r->present_small_enabled = small;
        if (small) VK_LOG("vulkan: CPU frames are uploaded at the displayed size");
    }
#endif
#if defined(__ANDROID__)
    VkAndroidSurfaceCreateInfoKHR info = VK_INIT(ANDROID_SURFACE_CREATE_INFO_KHR);
    VkBool32 supported = VK_FALSE;
    if (!r || !r->instance || !r->dev || !native_window || !r->present_instance_ext ||
        !r->present_device_ext || !r->present_create_surface)
        return 0;
    nj_vk_renderer_present_detach(renderer);
    info.window = (ANativeWindow *)native_window;
    if (r->present_create_surface(r->instance, &info, NULL, &r->present_surface) != VK_SUCCESS) {
        VK_LOG("vulkan: android surface failed");
        return 0;
    }
    if (r->present_surface_support &&
        (r->present_surface_support(r->gpu, r->family, r->present_surface, &supported) !=
             VK_SUCCESS ||
         !supported)) {
        VK_LOG("vulkan: surface is not presentable on the graphics queue");
        nj_vk_renderer_present_detach(renderer);
        return 0;
    }
    if (!present_build_swapchain(r, width, height)) {
        nj_vk_renderer_present_detach(renderer);
        return 0;
    }
    if (r->present_semaphore == VK_NULL_HANDLE) {
        VkSemaphoreCreateInfo sem = VK_INIT(SEMAPHORE_CREATE_INFO);
        if (vkCreateSemaphore(r->dev, &sem, NULL, &r->present_semaphore) != VK_SUCCESS) {
            nj_vk_renderer_present_detach(renderer);
            return 0;
        }
    }
    r->present_window = native_window;
    return 1;
#else
    (void)r;
    (void)native_window;
    (void)width;
    (void)height;
    return 0;
#endif
}

#if defined(__ANDROID__)
static void present_small_release(nj_vk_renderer *r) {
    if (!r) return;
    nj_vk_renderer_mirror_destroy(r->present_small);
    r->present_small = NULL;
    free(r->present_scratch);
    r->present_scratch = NULL;
    r->present_small_w = 0;
    r->present_small_h = 0;
}

/* Shrink a CPU frame to the size the swapchain will show and upload that.
 * Returns 1 when the small mirror is ready to blit 1:1. */
static int present_upload_small(nj_vk_renderer *r, const nj_surface *frame, int w, int h) {
    nj_surface view;
    int i, j;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return 0;
    if (!r->present_small || r->present_small_w != w || r->present_small_h != h) {
        present_small_release(r);
        r->present_small = nj_vk_renderer_mirror_create(r, w, h);
        if (!r->present_small) return 0;
        r->present_scratch = (nj_u1 *)malloc((size_t)w * (size_t)h * 4u);
        if (!r->present_scratch) {
            present_small_release(r);
            return 0;
        }
        r->present_small_w = w;
        r->present_small_h = h;
    }
    for (i = 0; i < h; i++) {
        int sy = (int)(((long long)(2 * i + 1) * frame->height) / (2LL * h));
        const nj_u1 *src = (const nj_u1 *)frame->pixels + (size_t)sy * (size_t)frame->stride;
        nj_u1 *dst = r->present_scratch + (size_t)i * (size_t)w * 4u;
        for (j = 0; j < w; j++) {
            int sx = (int)(((long long)(2 * j + 1) * frame->width) / (2LL * w));
            *(nj_u4 *)(dst + (size_t)j * 4u) = *(const nj_u4 *)(src + (size_t)sx * 4u);
        }
    }
    view.pixels = r->present_scratch;
    view.width = w;
    view.height = h;
    view.stride = w * 4;
    view.format = frame->format;
    view.bpp = 4;
    if (!nj_vk_renderer_mirror_import_rect(r->present_small, &view, NULL, 0, 0, 0, w, h,
                                           NJ_VK_MIRROR_IMPORT_COLOUR))
        return 0;
    r->present_small_frames++;
    return 1;
}

#endif

int nj_vk_renderer_present_frame(void *renderer, const nj_surface *frame) {
    nj_vk_renderer *r = (nj_vk_renderer *)renderer;
#if defined(__ANDROID__)
    VkPresentInfoKHR present = VK_INIT(PRESENT_INFO_KHR);
    uint32_t index = 0;
    VkResult rc;
    int x, y, w, h;
    double aspect, target;
    if (!r) return 0;
    r->present_attempts++;
    if (!r->present_ready || !frame || !frame->pixels || frame->width <= 0 ||
        frame->height <= 0) {
        if (r->present_attempts <= 8)
            VK_LOG("vulkan: present skip ready=%d frame=%p pixels=%d %dx%d resident=%d mirror=%d",
                   r->present_ready, (const void *)frame, frame ? (frame->pixels != NULL) : 0,
                   frame ? frame->width : 0, frame ? frame->height : 0, r->resident != NULL,
                   r->resident ? (r->resident->mirror != NULL) : 0);
        return 0;
    }
    double now = r->present_stats ? vk_prof_now() : 0.0;
    double t_import = 0.0, t_acquire = 0.0, t_blit = 0.0;
    nj_vk_mirror *blit_mirror = NULL;
    int used_small = 0;
    /* The display rectangle first: CPU frames can be uploaded straight at it. */
    aspect = (double)frame->width / (double)frame->height;
    target = (double)r->present_extent.width / (double)r->present_extent.height;
    if (aspect >= target) {
        w = (int)r->present_extent.width;
        h = (int)((double)w / aspect);
    } else {
        h = (int)r->present_extent.height;
        w = (int)((double)h * aspect);
    }
    if (w <= 0 || h <= 0) {
        w = (int)r->present_extent.width;
        h = (int)r->present_extent.height;
    }
    x = ((int)r->present_extent.width - w) / 2;
    y = ((int)r->present_extent.height - h) / 2;
    if (r->present_small_enabled && (r->resident == NULL || r->resident_cpu_touched)) {
        if (present_upload_small(r, frame, w, h)) {
            used_small = 1;
            r->resident_cpu_touched = 0;
        }
    }
    if (!used_small && (!r->resident || !r->resident->mirror)) {
        /* Frames can arrive before the game first draws through the resident
         * mirror (loading screens, 2D-only titles). Adopt the CPU frame into a
         * mirror instead of falling back to the slow CPU present path: the
         * zero-copy adoption leaves the pixels in the staging buffer, so the
         * import is just one copyBufferToImage. */
        nj_vk_resident *res = resident_ensure(r, (nj_surface *)frame);
        if (!res || !res->mirror ||
            !nj_vk_renderer_mirror_import_rect(res->mirror, frame, NULL, 0, 0, 0, frame->width,
                                               frame->height, NJ_VK_MIRROR_IMPORT_COLOUR)) {
            if (r->present_attempts <= 8)
                VK_LOG("vulkan: present could not adopt the CPU frame");
            return 0;
        }
    }
    /* 2D-only frames draw on the CPU copy after the last GPU export. Upload
     * them before presenting, otherwise the swapchain keeps showing the stale
     * image the mirror had when the first frame was adopted. */
    if (!used_small && r->resident && r->resident_cpu_touched) {
        if (!nj_vk_renderer_mirror_import_rect(r->resident->mirror, frame, NULL, 0, 0, 0,
                                               frame->width, frame->height,
                                               NJ_VK_MIRROR_IMPORT_COLOUR)) {
            if (r->present_attempts <= 8)
                VK_LOG("vulkan: present could not upload the touched CPU frame");
            return 0;
        }
        r->resident_cpu_touched = 0;
    }
    blit_mirror = used_small ? r->present_small : (r->resident ? r->resident->mirror : NULL);
    if (!blit_mirror) {
        if (r->present_attempts <= 8) VK_LOG("vulkan: present has no mirror to blit");
        return 0;
    }
    if (r->present_stats) { t_import = vk_prof_now() - now; now = vk_prof_now(); }
    rc = r->present_acquire(r->dev, r->present_swapchain, UINT64_MAX, r->present_semaphore,
                            VK_NULL_HANDLE, &index);
    if (rc != VK_SUCCESS) {
        if (r->present_attempts <= 8)
            VK_LOG("vulkan: present acquire failed (%d)", (int)rc);
        if (rc == VK_ERROR_OUT_OF_DATE_KHR || rc == VK_SUBOPTIMAL_KHR)
            r->present_ready = 0;
        return 0;
    }
    if (r->present_stats) { t_acquire = vk_prof_now() - now; now = vk_prof_now(); }
    /* The mirror is 8x the logical frame, so presenting it needs a proper
     * downsample: a nearest tap of the supersampled image shimmers. Linear
     * filtering is the cheapest filter that actually averages the source. */
    if (!nj_vk_renderer_mirror_present(blit_mirror, r->present_images[index],
                                       r->present_extent.width, r->present_extent.height, x, y, w,
                                       h, 1, r->present_semaphore)) {
        if (r->present_attempts <= 8)
            VK_LOG("vulkan: present blit failed (%dx%d from %dx%d)", w, h, frame->width,
                   frame->height);
        return 0;
    }
    if (r->present_stats) { t_blit = vk_prof_now() - now; now = vk_prof_now(); }
    present.swapchainCount = 1;
    present.pSwapchains = &r->present_swapchain;
    present.pImageIndices = &index;
    rc = r->present_queue(r->queue, &present);
    if (rc != VK_SUCCESS && r->present_attempts <= 8)
        VK_LOG("vulkan: present queue failed (%d)", (int)rc);
    if (rc == VK_SUCCESS && r->present_attempts <= 8)
        VK_LOG("vulkan: present ok #%u", r->present_attempts);
    if (r->present_stats) {
        r->present_prof_import += t_import;
        r->present_prof_acquire += t_acquire;
        r->present_prof_blit += t_blit;
        r->present_prof_queue += vk_prof_now() - now;
        r->present_prof_frames++;
        if ((r->present_prof_frames % 60u) == 0u) {
            double n = (double)r->present_prof_frames;
            VK_LOG("vulkan: present stats frames=%u import=%.2f blit=%.2f acquire=%.2f queue=%.2f ms small=%u",
                   r->present_prof_frames, 1000.0 * r->present_prof_import / n,
                   1000.0 * r->present_prof_blit / n, 1000.0 * r->present_prof_acquire / n,
                   1000.0 * r->present_prof_queue / n, r->present_small_frames);
            r->present_prof_import = r->present_prof_acquire = 0.0;
            r->present_prof_blit = r->present_prof_queue = 0.0;
            r->present_prof_frames = 0u;
        }
    }
    if (rc != VK_SUCCESS) {
        if (rc == VK_ERROR_OUT_OF_DATE_KHR || rc == VK_SUBOPTIMAL_KHR)
            r->present_ready = 0;
        return 0;
    }
    return 1;
#else
    (void)r;
    (void)frame;
    return 0;
#endif
}
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
        int want_session = 0;
        const char *session = getenv("NJ_VK_SESSION");
        if (session && session[0] == '1')
            want_session = 1;
#if defined(__ANDROID__)
        if (!want_session) {
            /* The emulator has no way to hand the app an environment
             * variable, so the same opt-in is read from a debug property:
             * `adb shell setprop debug.nj2me.vk_session 1`. */
            char prop[PROP_VALUE_MAX];
            if (__system_property_get("debug.nj2me.vk_session", prop) > 0 &&
                prop[0] == '1')
                want_session = 1;
        }
#endif
        if (want_session) {
            port->gfx_triangles_list = nj_vk_renderer_triangles_list;
            port->gfx_triangles_list_ud = r;
            port->gfx_triangles_list_trusted = triangles_list_trusted;
            port->gfx_triangles_list_trusted_ud = r;
            port->gfx_triangles_begin = nj_vk_renderer_triangles_begin;
            port->gfx_triangles_end = nj_vk_renderer_triangles_end;
            port->gfx_triangles_abort = nj_vk_renderer_triangles_abort;
            port->gfx_triangles_session_ud = r;
            port->gfx_hd_journal = 1;
        }
    }
    {
        /* GPU-resident HD mirror: M3G draws, clears and background images stay
         * on the GPU; the CPU copy is exported only when the VM reads it. */
        int want_resident = 0;
        const char *resident = getenv("NJ_VK_RESIDENT");
        if (resident && resident[0] == '1')
            want_resident = 1;
#if defined(__ANDROID__)
        /* The swapchain present path consumes the resident mirror, and CPU
         * frames are uploaded at their displayed size, so both are on by
         * default here. debug.nj2me.vk_resident=0 (or NJ_VK_RESIDENT=0) keeps
         * the established GL/CPU present path. */
        want_resident = 1;
        if (resident && resident[0] == '0')
            want_resident = 0;
        {
            char prop[PROP_VALUE_MAX];
            if (__system_property_get("debug.nj2me.vk_resident", prop) > 0 && prop[0] == '0')
                want_resident = 0;
        }
#endif
        if (want_resident) {
            port->gfx_triangles_list = resident_list;
            port->gfx_triangles_list_ud = r;
            port->gfx_triangles_list_trusted = resident_list_trusted;
            port->gfx_triangles_list_trusted_ud = r;
            port->gfx_hd_journal = 1;
            port->hd_mirror_sync = resident_sync;
            port->hd_mirror_sync_ud = r;
            port->hd_mirror_clear = resident_clear;
            port->hd_mirror_clear_ud = r;
            port->hd_mirror_image = resident_image;
            port->hd_mirror_image_ud = r;
            port->hd_mirror_drop = resident_drop;
            port->hd_mirror_drop_ud = r;
            /* The resident mirror only exists for the HD target; keep the 1x
             * path synchronous. */
            port->gfx_triangles_hd_only = 1;
            VK_LOG("vulkan: resident HD mirror enabled");
        }
    }
    {
        /* One-shot diagnostic: can an Android hardware buffer be imported and
         * used as a Vulkan image on this device?  Present interop depends on
         * it; see src/port/vulkan/nj_vk_ahb_probe.c. */
        int want_probe = 0;
        const char *probe = getenv("NJ_AHB_PROBE");
        if (probe && probe[0] == '1')
            want_probe = 1;
#if defined(__ANDROID__)
        if (!want_probe) {
            char prop[PROP_VALUE_MAX];
            if (__system_property_get("debug.nj2me.ahb_probe", prop) > 0 && prop[0] == '1')
                want_probe = 1;
        }
#endif
        if (want_probe) {
            extern void nj_vk_ahb_probe(void);
            nj_vk_ahb_probe();
        }
    }
#if defined(__ANDROID__)
    /* The swapchain present path consumes the resident mirror; when the mirror
     * is off the port keeps its established GL/CPU present path. */
    if (port->gfx_triangles_list == resident_list) {
        port->hd_present_attach = nj_vk_renderer_present_attach;
        port->hd_present_detach = nj_vk_renderer_present_detach;
        port->hd_present_frame = nj_vk_renderer_present_frame;
        port->hd_present_ud = r;
    }
#endif
    port->gfx_hd_destroy = destroy_owned_renderer;
    return 1;
#else
    (void)port;
    return 0;
#endif
}
