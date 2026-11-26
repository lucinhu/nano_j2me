#include "nj_fsr2_vk.h"
#include <ffx_fsr2.h>
#include <vk/ffx_fsr2_vk.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

namespace {
struct Error {
    char *text;
    size_t cap;
    bool fail(const char *what, int code = 0) const {
        if (text && cap) std::snprintf(text, cap, "%s (%d)", what, code);
        return false;
    }
};
#define CHECK_RESULT(expr) do { auto r_ = (expr); if (r_ != 0) return err.fail(#expr, (int)r_); } while (0)

struct Image {
    NjFsr2VkImage handles;
    VkDeviceMemory memory;
};
const VkFormat formats[] = { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R32_SFLOAT,
                            VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM };
const VkImageSubresourceRange colorRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
}

struct NjFsr2 {
    alignas(16) FfxFsr2Context fsr;
    NjFsr2VkDevice vk;
    FfxDimensions2D input, output;
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;
    void *scratch;
    bool created, failed, has_history;
    /* Lazily allocated CPU bridge (GPU-only callers need none of this). */
    Image images[4];
    VkBuffer buffer;
    VkDeviceMemory host_memory;
    void *mapped;
    VkDeviceSize offset[4];
    bool coherent;
};

namespace {
bool choose_device(NjFsr2 &c, VkPhysicalDevice physical) {
    VkPhysicalDeviceSubgroupProperties subgroup = {};
    subgroup.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
    VkPhysicalDeviceProperties2 props = {};
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props.pNext = &subgroup;
    vkGetPhysicalDeviceProperties2(physical, &props);
    if (props.properties.apiVersion < VK_API_VERSION_1_2 ||
        c.output.width > props.properties.limits.maxImageDimension2D ||
        c.output.height > props.properties.limits.maxImageDimension2D ||
        !(subgroup.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT)) return false;
    VkSubgroupFeatureFlags needed = VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_QUAD_BIT;
    if ((subgroup.supportedOperations & needed) != needed ||
        props.properties.limits.maxComputeWorkGroupInvocations < 256 ||
        props.properties.limits.maxComputeWorkGroupSize[0] < 256) return false;
    uint32_t num_extensions = 0;
    if (vkEnumerateDeviceExtensionProperties(physical, nullptr, &num_extensions, nullptr) != VK_SUCCESS) return false;
    std::unique_ptr<VkExtensionProperties[]> ext(new(std::nothrow) VkExtensionProperties[num_extensions]);
    if (!ext || vkEnumerateDeviceExtensionProperties(physical, nullptr, &num_extensions, ext.get()) != VK_SUCCESS) return false;
    bool size_control = false;
    const char *extensions[2];
    uint32_t extension_count = 0;
    for (uint32_t i = 0; i < num_extensions; i++) {
        const char *name = ext[i].extensionName;
        if (!std::strcmp(name, VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME)) {
            size_control = true;
            extensions[extension_count++] = name;
        } else if (!std::strcmp(name, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME)) {
            extensions[extension_count++] = name;
        }
    }
    VkPhysicalDeviceSubgroupSizeControlFeaturesEXT sizes = {};
    sizes.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT;
    VkPhysicalDeviceVulkan12Features v12 = {};
    v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    v12.pNext = size_control ? &sizes : nullptr;
    VkPhysicalDeviceFeatures2 features = {};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &v12;
    vkGetPhysicalDeviceFeatures2(physical, &features);
    if (!features.features.shaderStorageImageExtendedFormats || !features.features.shaderStorageImageWriteWithoutFormat) return false;
    VkPhysicalDeviceVulkan12Features enable = {};
    enable.sType = v12.sType;
    enable.shaderFloat16 = v12.shaderFloat16;
    enable.shaderSubgroupExtendedTypes = v12.shaderSubgroupExtendedTypes;
    enable.pNext = v12.pNext;
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, nullptr);
    std::unique_ptr<VkQueueFamilyProperties[]> families(new(std::nothrow) VkQueueFamilyProperties[n]);
    if (!families) return false;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, families.get());
    for (uint32_t i = 0; i < n; i++) {
        if (!(families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) continue;
        VkPhysicalDeviceFeatures required = {};
        required.shaderStorageImageExtendedFormats = VK_TRUE;
        required.shaderStorageImageWriteWithoutFormat = VK_TRUE;
        float priority = 1;
        VkDeviceQueueCreateInfo qi = {};
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = i;
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        VkDeviceCreateInfo di = {};
        di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        di.queueCreateInfoCount = 1;
        di.pQueueCreateInfos = &qi;
        di.pEnabledFeatures = &required;
        di.pNext = &enable;
        di.enabledExtensionCount = extension_count;
        di.ppEnabledExtensionNames = extensions;
        required.shaderInt16 = features.features.shaderInt16;
        if (vkCreateDevice(physical, &di, nullptr, &c.vk.device) != VK_SUCCESS) continue;
        c.vk.physical = physical;
        c.vk.queue_family = i;
        vkGetDeviceQueue(c.vk.device, i, 0, &c.vk.queue);
        return true;
    }
    return false;
}

bool init(NjFsr2 &c, Error err) {
    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "nano_j2me FSR2";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    CHECK_RESULT(vkCreateInstance(&info, nullptr, &c.vk.instance));
    uint32_t n = 0;
    CHECK_RESULT(vkEnumeratePhysicalDevices(c.vk.instance, &n, nullptr));
    std::unique_ptr<VkPhysicalDevice[]> devices(new(std::nothrow) VkPhysicalDevice[n]);
    if (!devices) return err.fail("No memory for device enumeration");
    CHECK_RESULT(vkEnumeratePhysicalDevices(c.vk.instance, &n, devices.get()));
    for (uint32_t i = 0; i < n && !c.vk.device; i++) choose_device(c, devices[i]);
    if (!c.vk.device) return err.fail("No compatible Vulkan 1.2 compute device");
    VkCommandPoolCreateInfo pi = {};
    pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = c.vk.queue_family;
    CHECK_RESULT(vkCreateCommandPool(c.vk.device, &pi, nullptr, &c.pool));
    VkCommandBufferAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = c.pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    CHECK_RESULT(vkAllocateCommandBuffers(c.vk.device, &ai, &c.cmd));
    VkFenceCreateInfo fi = {};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    CHECK_RESULT(vkCreateFence(c.vk.device, &fi, nullptr, &c.fence));
    auto size = ffxFsr2GetScratchMemorySizeVK(c.vk.physical);
    c.scratch = std::calloc(1, size);
    if (!c.scratch) return err.fail("No memory for FSR2 scratch");
    FfxFsr2ContextDescription desc = {};
    desc.maxRenderSize = c.input;
    desc.displaySize = c.output;
    desc.device = ffxGetDeviceVK(c.vk.device);
    desc.flags = FFX_FSR2_ENABLE_AUTO_EXPOSURE;
    CHECK_RESULT(ffxFsr2GetInterfaceVK(&desc.callbacks, c.scratch, size,
                                       c.vk.physical, vkGetDeviceProcAddr));
    CHECK_RESULT(ffxFsr2ContextCreate(&c.fsr, &desc));
    c.created = true;
    return true;
}

bool allocate(NjFsr2 &c, VkMemoryRequirements req, VkMemoryPropertyFlags flags,
              VkDeviceMemory &memory, Error err) {
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(c.vk.physical, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        auto actual = props.memoryTypes[i].propertyFlags;
        if (!(req.memoryTypeBits & (1u << i)) || (actual & flags) != flags) continue;
        VkMemoryAllocateInfo ai = {};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.memoryTypeIndex = i;
        ai.allocationSize = req.size;
        VkResult r = vkAllocateMemory(c.vk.device, &ai, nullptr, &memory);
        if (r != VK_SUCCESS) continue;
        if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            c.coherent = (actual & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        return true;
    }
    return err.fail("No suitable Vulkan memory allocation");
}

bool create_cpu_io(NjFsr2 &c, Error err) {
    VkDeviceSize bytes = 0;
    for (int i = 0; i < 4; i++) {
        bool output = i == 3;
        auto dims = output ? c.output : c.input;
        Image &im = c.images[i];
        VkImageCreateInfo ii = {};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = formats[i];
        ii.extent = {dims.width, dims.height, 1};
        ii.mipLevels = ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.usage = output ? VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                          : VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        CHECK_RESULT(vkCreateImage(c.vk.device, &ii, nullptr, &im.handles.image));
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(c.vk.device, im.handles.image, &req);
        if (!allocate(c, req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, im.memory, err)) return false;
        CHECK_RESULT(vkBindImageMemory(c.vk.device, im.handles.image, im.memory, 0));
        VkImageViewCreateInfo vi = {};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = im.handles.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = ii.format;
        vi.subresourceRange = colorRange;
        CHECK_RESULT(vkCreateImageView(c.vk.device, &vi, nullptr, &im.handles.view));
        bytes = (bytes + 7) & ~VkDeviceSize(7); // texel block size for RG32F
        c.offset[i] = bytes;
        bytes += VkDeviceSize(dims.width) * dims.height * (i == 2 ? 8 : 4);
    }
    if (bytes > SIZE_MAX) return err.fail("CPU frame exceeds addressable memory");
    VkBufferCreateInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = bytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    CHECK_RESULT(vkCreateBuffer(c.vk.device, &bi, nullptr, &c.buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(c.vk.device, c.buffer, &req);
    if (!allocate(c, req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, c.host_memory, err)) return false;
    CHECK_RESULT(vkBindBufferMemory(c.vk.device, c.buffer, c.host_memory, 0));
    CHECK_RESULT(vkMapMemory(c.vk.device, c.host_memory, 0, VK_WHOLE_SIZE, 0, &c.mapped));
    return true;
}

bool valid_frame(NjFsr2 *c, const NjFsr2Frame *f, Error err) {
    if (!c || c->failed) return err.fail("FSR2 context unavailable");
    if (!f || !std::isfinite(f->jitter_x) || !std::isfinite(f->jitter_y) ||
        !std::isfinite(f->delta_ms) || f->delta_ms <= 0 ||
        !(f->camera_near > 0 && f->camera_far > f->camera_near) ||
        !std::isfinite(f->camera_far) || !(f->fov_y > 0 && f->fov_y < 3.141593f))
        return err.fail("Invalid FSR2 frame parameters");
    return true;
}

bool begin(NjFsr2 &c, Error err) {
    CHECK_RESULT(vkResetCommandPool(c.vk.device, c.pool, 0));
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CHECK_RESULT(vkBeginCommandBuffer(c.cmd, &bi));
    return true;
}

bool record(NjFsr2 &c, const NjFsr2Frame &f, const NjFsr2VkFrame &images, Error err) {
    auto resource = [&](NjFsr2VkImage image, int index, const wchar_t *name) {
        bool output = index == 3;
        auto dims = output ? c.output : c.input;
        return ffxGetTextureResourceVK(&c.fsr, image.image, image.view, dims.width, dims.height,
            formats[index], name, output ? FFX_RESOURCE_STATE_UNORDERED_ACCESS : FFX_RESOURCE_STATE_COMPUTE_READ);
    };
    FfxFsr2DispatchDescription d = {};
    d.commandList = ffxGetCommandListVK(c.cmd);
    d.color = resource(images.color, 0, L"M3G color");
    d.depth = resource(images.depth, 1, L"M3G depth");
    d.motionVectors = resource(images.motion, 2, L"M3G motion");
    d.output = resource(images.output, 3, L"FSR2 output");
    d.jitterOffset = {f.jitter_x, f.jitter_y};
    d.motionVectorScale = {1, 1};
    d.renderSize = c.input;
    d.frameTimeDelta = f.delta_ms;
    d.preExposure = 1;
    d.reset = !c.has_history || f.reset;
    d.cameraNear = f.camera_near;
    d.cameraFar = f.camera_far;
    d.cameraFovAngleVertical = f.fov_y;
    d.viewSpaceToMetersFactor = 1;
    CHECK_RESULT(ffxFsr2ContextDispatch(&c.fsr, &d));
    return true;
}

bool finish(NjFsr2 &c, Error err) {
    CHECK_RESULT(vkEndCommandBuffer(c.cmd));
    CHECK_RESULT(vkResetFences(c.vk.device, 1, &c.fence));
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &c.cmd;
    CHECK_RESULT(vkQueueSubmit(c.vk.queue, 1, &si, c.fence));
    CHECK_RESULT(vkWaitForFences(c.vk.device, 1, &c.fence, VK_TRUE, UINT64_MAX));
    c.has_history = true;
    return true;
}

void barrier(NjFsr2 &c, int index, VkImageLayout from, VkImageLayout to,
             VkPipelineStageFlags src, VkPipelineStageFlags dst,
             VkAccessFlags previous, VkAccessFlags next) {
    VkImageMemoryBarrier b = {};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = previous;
    b.dstAccessMask = next;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = c.images[index].handles.image;
    b.subresourceRange = colorRange;
    vkCmdPipelineBarrier(c.cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
}

VkBufferImageCopy copy_region(const NjFsr2 &c, int i) {
    auto dims = i == 3 ? c.output : c.input;
    VkBufferImageCopy reg = {};
    reg.bufferOffset = c.offset[i];
    reg.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    reg.imageSubresource.layerCount = 1;
    reg.imageExtent = {dims.width, dims.height, 1};
    return reg;
}

bool dispatch_cpu(NjFsr2 &c, const NjFsr2Frame &f, uint32_t *out, Error err) {
    if (!c.mapped && !create_cpu_io(c, err)) return false;
    auto *bytes = static_cast<uint8_t *>(c.mapped);
    size_t pixels = size_t(c.input.width) * c.input.height;
    for (size_t i = 0; i < pixels; i++) {
        uint32_t v = f.color_argb[i];
        bytes[4*i] = uint8_t(v >> 16);
        bytes[4*i+1] = uint8_t(v >> 8);
        bytes[4*i+2] = uint8_t(v);
        bytes[4*i+3] = 255;
    }
    std::memcpy(bytes + c.offset[1], f.depth, pixels * sizeof(float));
    std::memcpy(bytes + c.offset[2], f.motion_xy, pixels * sizeof(float) * 2);
    VkMappedMemoryRange range = {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = c.host_memory;
    range.size = VK_WHOLE_SIZE;
    if (!c.coherent) CHECK_RESULT(vkFlushMappedMemoryRanges(c.vk.device, 1, &range));
    if (!begin(c, err)) return false;
    for (int i = 0; i < 3; i++) {
        // A fence completed the last use, and the full input is overwritten.
        barrier(c, i, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        auto reg = copy_region(c, i);
        vkCmdCopyBufferToImage(c.cmd, c.buffer, c.images[i].handles.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &reg);
        barrier(c, i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }
    barrier(c, 3, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_ACCESS_SHADER_WRITE_BIT);
    NjFsr2VkFrame images = {c.images[0].handles, c.images[1].handles, c.images[2].handles, c.images[3].handles};
    if (!record(c, f, images, err)) return false;
    barrier(c, 3, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    auto reg = copy_region(c, 3);
    vkCmdCopyImageToBuffer(c.cmd, c.images[3].handles.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, c.buffer, 1, &reg);
    VkMemoryBarrier host = {};
    host.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(c.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &host, 0, nullptr, 0, nullptr);
    if (!finish(c, err)) return false;
    if (!c.coherent) CHECK_RESULT(vkInvalidateMappedMemoryRanges(c.vk.device, 1, &range));
    const uint8_t *p = bytes + c.offset[3];
    for (size_t i = 0; i < size_t(c.output.width) * c.output.height; i++, p += 4)
        out[i] = 0xff000000u | (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
    return true;
}
} // namespace

NjFsr2 *nj_fsr2_create(int rw, int rh, int ow, int oh, char *error, size_t cap) {
    Error err = {error, cap};
    // The SPD luminance pyramid uses fixed levels and at most 12 mips.
    // Bound reconstruction to AMD's Ultra Performance envelope (3x per axis).
    if (rw < 32 || rh < 32 || (rw < 64 && rh < 64) || rw > 4096 || rh > 4096 || ow < rw || oh < rh ||
        ow > 3 * rw || oh > 3 * rh) {
        char why[160];
        std::snprintf(why, sizeof(why),
                      "Unsupported FSR2 input/output dimensions %dx%d -> %dx%d; "
                      "at most 3x per axis",
                      rw, rh, ow, oh);
        err.fail(why);
        return nullptr;
    }
    NjFsr2 *c = new(std::nothrow) NjFsr2{};
    if (!c) { err.fail("No memory for FSR2 context"); return nullptr; }
    c->input = {uint32_t(rw), uint32_t(rh)};
    c->output = {uint32_t(ow), uint32_t(oh)};
    if (!init(*c, err)) { nj_fsr2_destroy(c); return nullptr; }
    return c;
}

void nj_fsr2_destroy(NjFsr2 *c) {
    if (!c) return;
    auto d = c->vk.device;
    if (d) {
        vkDeviceWaitIdle(d);
        if (c->created) ffxFsr2ContextDestroy(&c->fsr);
        if (c->mapped) vkUnmapMemory(d, c->host_memory);
        vkDestroyBuffer(d, c->buffer, nullptr);
        vkFreeMemory(d, c->host_memory, nullptr);
        for (const auto &im : c->images) {
            vkDestroyImageView(d, im.handles.view, nullptr);
            vkDestroyImage(d, im.handles.image, nullptr);
            vkFreeMemory(d, im.memory, nullptr);
        }
        vkDestroyFence(d, c->fence, nullptr);
        vkDestroyCommandPool(d, c->pool, nullptr);
        vkDestroyDevice(d, nullptr);
    }
    if (c->vk.instance) vkDestroyInstance(c->vk.instance, nullptr);
    std::free(c->scratch);
    delete c;
}

void nj_fsr2_jitter(NjFsr2 *c, unsigned frame, float *x, float *y) {
    float jx = 0, jy = 0;
    if (c) {
        int n = ffxFsr2GetJitterPhaseCount(c->input.width, c->output.width);
        ffxFsr2GetJitterOffset(&jx, &jy, int(frame % unsigned(n)), n);
    }
    if (x) *x = jx;
    if (y) *y = jy;
}

int nj_fsr2_dispatch(NjFsr2 *c, const NjFsr2Frame *f, uint32_t *out, char *error, size_t cap) {
    Error err = {error, cap};
    if (!valid_frame(c, f, err)) return 0;
    if (!out || !f->depth || !f->motion_xy || !f->color_argb) return err.fail("Missing FSR2 CPU frame buffers");
    c->failed = !dispatch_cpu(*c, *f, out, err);
    return !c->failed;
}

int nj_fsr2_vk_get_device(NjFsr2 *c, NjFsr2VkDevice *vk) {
    if (!c || !vk || c->failed) return 0;
    *vk = c->vk;
    return 1;
}

int nj_fsr2_vk_dispatch(NjFsr2 *c, const NjFsr2Frame *f, const NjFsr2VkFrame *im, char *error, size_t cap) {
    Error err = {error, cap};
    if (!valid_frame(c, f, err)) return 0;
    if (!im || !im->color.image || !im->color.view || !im->depth.image || !im->depth.view ||
        !im->motion.image || !im->motion.view || !im->output.image || !im->output.view)
        return err.fail("Missing FSR2 Vulkan frame images");
    c->failed = !(begin(*c, err) && record(*c, *f, *im, err) && finish(*c, err));
    return !c->failed;
}
