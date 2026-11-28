/* Android hardware-buffer interop probe.
 *
 * Present currently reads the resident HD mirror back as pixels and uploads it
 * through GLES.  Sharing an Android hardware buffer between GL and Vulkan would
 * remove both transfers.  Before writing any of that, find out whether this
 * device really lets us allocate a GPU buffer, describe it to Vulkan, import it
 * as an image, bind memory to it and actually use it from a command buffer.
 *
 * This is a one-shot diagnostic, compiled only into Android Vulkan builds and
 * enabled with the debug property `debug.nj2me.ahb_probe` (or NJ_AHB_PROBE=1).
 * It creates its own instance and device so it cannot perturb the renderer.
 *
 * WARNING: the gfxstream emulator advertises the extension but dies inside the
 * driver once the probe imports the buffer (see
 * .local-tools/debug-runs/avd-ahb-20261128/).  Only enable this on real
 * hardware.
 */
#if defined(__ANDROID__)

#include <android/hardware_buffer.h>
#include <android/log.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <vulkan/vulkan.h>

#define AHB_LOG(...) __android_log_print(ANDROID_LOG_INFO, "nj2me", __VA_ARGS__)

typedef struct nj_ahb_api {
    int (*allocate)(const AHardwareBuffer_Desc *desc, AHardwareBuffer **out);
    void (*release)(AHardwareBuffer *buffer);
} nj_ahb_api;

static int nj_ahb_api_load(nj_ahb_api *api)
{
    void *lib = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (!lib)
        return 0;
    *(void **)(&api->allocate) = dlsym(lib, "AHardwareBuffer_allocate");
    *(void **)(&api->release) = dlsym(lib, "AHardwareBuffer_release");
    return api->allocate && api->release;
}

static int nj_ahb_instance_extension(const char *name)
{
    uint32_t count = 0, i;
    VkExtensionProperties *exts;
    if (vkEnumerateInstanceExtensionProperties(NULL, &count, NULL) != VK_SUCCESS || !count)
        return 0;
    exts = (VkExtensionProperties *)malloc((size_t)count * sizeof *exts);
    if (!exts)
        return 0;
    if (vkEnumerateInstanceExtensionProperties(NULL, &count, exts) != VK_SUCCESS) {
        free(exts);
        return 0;
    }
    for (i = 0; i < count; ++i) {
        if (strcmp(exts[i].extensionName, name) == 0) {
            free(exts);
            return 1;
        }
    }
    free(exts);
    return 0;
}

static int nj_ahb_device_extension(VkPhysicalDevice gpu, const char *name)
{
    uint32_t count = 0, i;
    VkExtensionProperties *exts;
    if (vkEnumerateDeviceExtensionProperties(gpu, NULL, &count, NULL) != VK_SUCCESS || !count)
        return 0;
    exts = (VkExtensionProperties *)malloc((size_t)count * sizeof *exts);
    if (!exts)
        return 0;
    if (vkEnumerateDeviceExtensionProperties(gpu, NULL, &count, exts) != VK_SUCCESS) {
        free(exts);
        return 0;
    }
    for (i = 0; i < count; ++i) {
        if (strcmp(exts[i].extensionName, name) == 0) {
            free(exts);
            return 1;
        }
    }
    free(exts);
    return 0;
}

static int nj_ahb_queue_family(VkPhysicalDevice gpu, uint32_t *family)
{
    VkQueueFamilyProperties props[8];
    uint32_t count = 8, i;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, props);
    for (i = 0; i < count; ++i) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            *family = i;
            return 1;
        }
    }
    return 0;
}

static double nj_ahb_now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec * 1e-6;
}

/* Returns 1 when the whole chain (allocate, describe, import, bind, use)
 * succeeded, 0 otherwise; every step logs its own result. */
void nj_vk_ahb_probe(void)
{
    nj_ahb_api ahb = {0};
    AHardwareBuffer *buffer = NULL;
    AHardwareBuffer_Desc desc = {0};
    VkApplicationInfo app = {0};
    VkInstanceCreateInfo instance_info = {0};
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory image_memory = VK_NULL_HANDLE;
    VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory = VK_NULL_HANDLE;
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID get_ahb_props = NULL;
    const uint32_t extent = 64;
    const VkDeviceSize bytes = (VkDeviceSize)extent * extent * 4u;
    const char *instance_ext = "VK_KHR_external_memory_capabilities";
    const char *instance_exts[1];
    const char *device_exts[3];
    uint32_t instance_ext_count = 0;
    uint32_t device_ext_count = 0;
    uint32_t family = 0, i;
    VkAndroidHardwareBufferPropertiesANDROID ahb_props = {0};
    VkAndroidHardwareBufferFormatPropertiesANDROID format_props = {0};
    VkExternalFormatANDROID external_format = {0};
    VkExternalMemoryImageCreateInfo external_memory = {0};
    VkImageCreateInfo image_info = {0};
    VkMemoryRequirements requirements = {0};
    VkPhysicalDeviceMemoryProperties memory = {0};
    VkMemoryDedicatedAllocateInfo dedicated = {0};
    VkImportAndroidHardwareBufferInfoANDROID import = {0};
    VkMemoryAllocateInfo allocate = {0};
    VkBufferCreateInfo buffer_info = {0};
    VkCommandPoolCreateInfo pool_info = {0};
    VkCommandBufferAllocateInfo cmd_info = {0};
    VkFenceCreateInfo fence_info = {0};
    VkCommandBufferBeginInfo begin_info = {0};
    VkImageMemoryBarrier barriers[2] = {{0}, {0}};
    VkBufferImageCopy copy = {0};
    VkBufferMemoryBarrier host_barrier = {0};
    VkSubmitInfo submit = {0};
    VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkClearColorValue clear = {{0.25f, 0.5f, 0.75f, 1.0f}};
    VkDeviceSize offset = 0;
    VkMemoryAllocateInfo readback_allocate = {0};
    void *mapped = NULL;
    const unsigned char *pixels;
    VkResult result;
    double start;
    int ok = 0;

    if (!nj_ahb_api_load(&ahb)) {
        AHB_LOG("ahb probe: no AHardwareBuffer API in libandroid.so");
        return;
    }
    desc.width = extent;
    desc.height = extent;
    desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    desc.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
                 AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
    if (ahb.allocate(&desc, &buffer) != 0) {
        AHB_LOG("ahb probe: AHardwareBuffer_allocate failed");
        return;
    }
    AHB_LOG("ahb probe: allocated %ux%u rgba", extent, extent);

    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.apiVersion = VK_API_VERSION_1_0;
    app.pApplicationName = "nano_j2me";
    app.pEngineName = "nano_j2me";
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app;
    if (nj_ahb_instance_extension(instance_ext)) {
        instance_exts[instance_ext_count++] = instance_ext;
        instance_info.enabledExtensionCount = instance_ext_count;
        instance_info.ppEnabledExtensionNames = instance_exts;
    }
    result = vkCreateInstance(&instance_info, NULL, &instance);
    if (result != VK_SUCCESS) {
        AHB_LOG("ahb probe: vkCreateInstance failed (%d)", (int)result);
        goto done;
    }
    {
        VkPhysicalDevice gpus[4];
        uint32_t count = 4, best = 0;
        int have = 0;
        if (vkEnumeratePhysicalDevices(instance, &count, gpus) != VK_SUCCESS || !count) {
            AHB_LOG("ahb probe: no physical device");
            goto done;
        }
        if (count > 4)
            count = 4;
        for (i = 0; i < count; ++i) {
            int external_memory =
                nj_ahb_device_extension(gpus[i], "VK_KHR_external_memory");
            int ahb_extension = nj_ahb_device_extension(
                gpus[i], "VK_ANDROID_external_memory_android_hardware_buffer");
            AHB_LOG("ahb probe: gpu %u external_memory=%d ahb_ext=%d", i, external_memory,
                    ahb_extension);
            if (external_memory && ahb_extension) {
                gpu = gpus[i];
                best = i;
                have = 1;
                break;
            }
        }
        (void)best;
        if (!have) {
            AHB_LOG("ahb probe: no device with the hardware-buffer extensions");
            goto done;
        }
    }
    if (!nj_ahb_queue_family(gpu, &family)) {
        AHB_LOG("ahb probe: no graphics queue");
        goto done;
    }
    {
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info = {0};
        VkDeviceCreateInfo device_info = {0};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;
        device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;
        if (nj_ahb_device_extension(gpu, "VK_KHR_dedicated_allocation"))
            device_exts[device_ext_count++] = "VK_KHR_dedicated_allocation";
        device_exts[device_ext_count++] = "VK_KHR_external_memory";
        device_exts[device_ext_count++] =
            "VK_ANDROID_external_memory_android_hardware_buffer";
        device_info.enabledExtensionCount = device_ext_count;
        device_info.ppEnabledExtensionNames = device_exts;
        result = vkCreateDevice(gpu, &device_info, NULL, &dev);
        if (result != VK_SUCCESS) {
            AHB_LOG("ahb probe: vkCreateDevice failed (%d)", (int)result);
            goto done;
        }
    }
    vkGetDeviceQueue(dev, family, 0, &queue);
    *(void **)(&get_ahb_props) = (void *)vkGetDeviceProcAddr(
        dev, "vkGetAndroidHardwareBufferPropertiesANDROID");
    if (!get_ahb_props) {
        AHB_LOG("ahb probe: vkGetAndroidHardwareBufferPropertiesANDROID missing");
        goto done;
    }

    AHB_LOG("ahb probe: querying hardware buffer properties");
    format_props.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID;
    ahb_props.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
    ahb_props.pNext = &format_props;
    result = get_ahb_props(dev, buffer, &ahb_props);
    if (result != VK_SUCCESS) {
        AHB_LOG("ahb probe: hardware buffer properties failed (%d)", (int)result);
        goto done;
    }
    AHB_LOG("ahb probe: props size=%llu bits=0x%x format=%d external=0x%llx",
            (unsigned long long)ahb_props.allocationSize, ahb_props.memoryTypeBits,
            (int)format_props.format, (unsigned long long)format_props.externalFormat);

    external_memory.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external_memory.handleTypes =
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = &external_memory;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent.width = extent;
    image_info.extent.height = extent;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (format_props.externalFormat) {
        external_format.sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID;
        external_format.externalFormat = format_props.externalFormat;
        external_format.pNext = image_info.pNext;
        image_info.pNext = &external_format;
        image_info.format = VK_FORMAT_UNDEFINED;
        AHB_LOG("ahb probe: driver wants undecorated external format");
    }
    AHB_LOG("ahb probe: creating importable image");
    result = vkCreateImage(dev, &image_info, NULL, &image);
    if (result != VK_SUCCESS) {
        AHB_LOG("ahb probe: vkCreateImage failed (%d)", (int)result);
        goto done;
    }
    vkGetImageMemoryRequirements(dev, image, &requirements);
    vkGetPhysicalDeviceMemoryProperties(gpu, &memory);
    {
        int index = -1;
        for (i = 0; i < memory.memoryTypeCount; ++i) {
            if ((requirements.memoryTypeBits & (1u << i)) &&
                (ahb_props.memoryTypeBits & (1u << i))) {
                index = (int)i;
                break;
            }
        }
        if (index < 0) {
            AHB_LOG("ahb probe: no memory type for the imported image");
            goto done;
        }
        dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicated.image = image;
        import.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
        import.pNext = &dedicated;
        import.buffer = buffer;
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.pNext = &import;
        allocate.allocationSize = requirements.size > ahb_props.allocationSize
                                       ? requirements.size
                                       : ahb_props.allocationSize;
        allocate.memoryTypeIndex = (uint32_t)index;
        AHB_LOG("ahb probe: allocating and importing memory");
        result = vkAllocateMemory(dev, &allocate, NULL, &image_memory);
        if (result != VK_SUCCESS) {
            AHB_LOG("ahb probe: vkAllocateMemory(import) failed (%d)", (int)result);
            goto done;
        }
        result = vkBindImageMemory(dev, image, image_memory, 0);
        if (result != VK_SUCCESS) {
            AHB_LOG("ahb probe: vkBindImageMemory failed (%d)", (int)result);
            goto done;
        }
    }
    AHB_LOG("ahb probe: imported image bound (%llu bytes)",
            (unsigned long long)requirements.size);

    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = bytes;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    result = vkCreateBuffer(dev, &buffer_info, NULL, &readback);
    if (result != VK_SUCCESS) {
        AHB_LOG("ahb probe: vkCreateBuffer failed (%d)", (int)result);
        goto done;
    }
    vkGetBufferMemoryRequirements(dev, readback, &requirements);
    {
        int index = -1;
        for (i = 0; i < memory.memoryTypeCount; ++i) {
            if ((requirements.memoryTypeBits & (1u << i)) &&
                (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                index = (int)i;
                break;
            }
        }
        if (index < 0) {
            AHB_LOG("ahb probe: no host-visible memory type");
            goto done;
        }
        readback_allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        readback_allocate.allocationSize = requirements.size;
        readback_allocate.memoryTypeIndex = (uint32_t)index;
        result = vkAllocateMemory(dev, &readback_allocate, NULL, &readback_memory);
        if (result != VK_SUCCESS) {
            AHB_LOG("ahb probe: readback memory failed (%d)", (int)result);
            goto done;
        }
        vkBindBufferMemory(dev, readback, readback_memory, 0);
    }
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = family;
    if (vkCreateCommandPool(dev, &pool_info, NULL, &pool) != VK_SUCCESS) {
        AHB_LOG("ahb probe: command pool failed");
        goto done;
    }
    cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_info.commandPool = pool;
    cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_info.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(dev, &cmd_info, &cmd) != VK_SUCCESS) {
        AHB_LOG("ahb probe: command buffer failed");
        goto done;
    }
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(dev, &fence_info, NULL, &fence) != VK_SUCCESS) {
        AHB_LOG("ahb probe: fence failed");
        goto done;
    }
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
        AHB_LOG("ahb probe: begin failed");
        goto done;
    }
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = image;
    barriers[0].subresourceRange = range;
    barriers[0].srcAccessMask = 0;
    barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1] = barriers[0];
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                         &barriers[0]);
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                         &barriers[1]);
    copy.bufferOffset = 0;
    copy.bufferRowLength = 0;
    copy.bufferImageHeight = 0;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = extent;
    copy.imageExtent.height = extent;
    copy.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
    host_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    host_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host_barrier.buffer = readback;
    host_barrier.offset = 0;
    host_barrier.size = bytes;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 0, NULL, 1,
                         &host_barrier);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        AHB_LOG("ahb probe: end failed");
        goto done;
    }
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    start = nj_ahb_now();
    result = vkQueueSubmit(queue, 1, &submit, fence);
    if (result == VK_SUCCESS)
        result = vkWaitForFences(dev, 1, &fence, VK_TRUE, 1000000000ull);
    if (result != VK_SUCCESS) {
        AHB_LOG("ahb probe: submit/wait failed (%d)", (int)result);
        goto done;
    }
    result = vkMapMemory(dev, readback_memory, 0, bytes, 0, &mapped);
    if (result != VK_SUCCESS) {
        AHB_LOG("ahb probe: map failed (%d)", (int)result);
        goto done;
    }
    pixels = (const unsigned char *)mapped;
    AHB_LOG("ahb probe: readback r=%u g=%u b=%u a=%u", pixels[0], pixels[1], pixels[2],
            pixels[3]);
    AHB_LOG("ahb probe: PASS clearing an imported hardware buffer in %.2f ms",
            (nj_ahb_now() - start) * 1000.0);
    vkUnmapMemory(dev, readback_memory);
    ok = 1;

done:
    if (dev != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(dev);
        if (fence)
            vkDestroyFence(dev, fence, NULL);
        if (pool)
            vkDestroyCommandPool(dev, pool, NULL);
        if (readback_memory)
            vkFreeMemory(dev, readback_memory, NULL);
        if (readback)
            vkDestroyBuffer(dev, readback, NULL);
        if (image_memory)
            vkFreeMemory(dev, image_memory, NULL);
        if (image)
            vkDestroyImage(dev, image, NULL);
        vkDestroyDevice(dev, NULL);
    }
    if (instance != VK_NULL_HANDLE)
        vkDestroyInstance(instance, NULL);
    if (buffer)
        ahb.release(buffer);
    if (!ok)
        AHB_LOG("ahb probe: chain incomplete (see the last line above)");
}

#else

/* Every port source compiles on every platform; outside Android the probe has
 * nothing to talk to. */
void nj_vk_ahb_probe(void) {}

#endif
