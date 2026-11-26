/* Pipeline variants and recording of one atomic transaction. */
static unsigned pipeline_index(const nj_gfx_triangle_cmd *t) {
    return (unsigned)((t->blend - 64) * 8 + t->alpha_write * 4 + t->depth_test * 2 + t->depth_write);
}
static int prepare_pipeline(nj_vk_renderer *r, const nj_gfx_triangle_cmd *t) {
    unsigned index = pipeline_index(t);
    VkGraphicsPipelineCreateInfo info = VK_INIT(GRAPHICS_PIPELINE_CREATE_INFO);
    VkPipelineShaderStageCreateInfo shaders[2] = {VK_INIT(PIPELINE_SHADER_STAGE_CREATE_INFO),
                                                  VK_INIT(PIPELINE_SHADER_STAGE_CREATE_INFO)};
    VkPipelineVertexInputStateCreateInfo input = VK_INIT(PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    VkVertexInputBindingDescription binding = {0, sizeof(vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex, uvq)},
        {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(vertex, rgba)}};
    VkPipelineInputAssemblyStateCreateInfo assembly =
        VK_INIT(PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    VkPipelineViewportStateCreateInfo viewport = VK_INIT(PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    VkPipelineRasterizationStateCreateInfo raster = VK_INIT(PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    VkPipelineMultisampleStateCreateInfo ms = VK_INIT(PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    VkPipelineDepthStencilStateCreateInfo depth = VK_INIT(PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    VkPipelineColorBlendStateCreateInfo blend = VK_INIT(PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    VkPipelineColorBlendAttachmentState color = {0};
    VkDynamicState dynamic[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn = VK_INIT(PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    if (r->pipelines[index])
        return 1;
    shaders[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaders[0].module = r->vs;
    shaders[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaders[1].module = r->fs;
    shaders[0].pName = shaders[1].pName = "main";
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = 3;
    input.pVertexAttributeDescriptions = attrs;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    viewport.viewportCount = viewport.scissorCount = 1;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    depth.depthTestEnable = t->depth_test || t->depth_write;
    depth.depthWriteEnable = t->depth_write;
    depth.depthCompareOp = t->depth_test ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_ALWAYS;
    color.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    if (t->alpha_write)
        color.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
    color.blendEnable = t->blend != 68;
    color.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color.dstAlphaBlendFactor =
        t->blend == 64 ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ZERO;
    switch (t->blend) {
    case 64:
        color.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        color.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case 65:
        color.srcColorBlendFactor = color.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        break;
    case 66:
        color.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
        break;
    case 67:
        color.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
        color.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
        break;
    }
    blend.attachmentCount = 1;
    blend.pAttachments = &color;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynamic;
    info.stageCount = 2;
    info.pStages = shaders;
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dyn;
    info.layout = r->layout;
    info.renderPass = r->pass;
    return vkCreateGraphicsPipelines(r->dev, VK_NULL_HANDLE, 1, &info, NULL, &r->pipelines[index]) ==
           VK_SUCCESS;
}

typedef struct texture_view {
    const uint8_t *pixels;
    int width, height, bpp, offset;
} texture_view;
typedef struct packet_info {
    texture_view views[MAX_VIEWS];
    int count, texels, seed_depth, write_depth;
} packet_info;
static int texture_index(const packet_info *p, const nj_gfx_triangle_cmd *t) {
    int i;
    for (i = 0; i < p->count; ++i) {
        const texture_view *v = &p->views[i];
        if (v->pixels == t->texture && v->width == t->texture_width && v->height == t->texture_height &&
            v->bpp == t->texture_bpp)
            break;
    }
    return i;
}
static int analyze(nj_vk_renderer *r, const nj_gfx_triangles *p, packet_info *info) {
    nj_u4 i;
    for (i = 0; i < p->count; ++i) {
        const nj_gfx_triangle_cmd *t = &p->triangles[i];
        int j;
        /* RGB565 intermediate blending must quantize each ordered draw. Our
         * 8-bit attachment guarantees that property for replace-only packets. */
        if (p->surface->format == NJ_PIXFMT_RGB565 && t->blend != 68)
            return 0;
        for (j = 0; j < 3; ++j) {
            const nj_gfx_triangle_vertex *v = &t->vertices[j];
            /* Decline extreme intermediates before narrowing doubles or doing
             * interpolator arithmetic on devices which flush denormals. */
            if (fabs(v->depth) > 1e12 || fabsf(v->x) > 1e9f || fabsf(v->y) > 1e9f ||
                v->inv_w < 1e-12f || v->inv_w > 1e12f || fabsf(v->u) > 1e12f || fabsf(v->v) > 1e12f)
                return 0;
        }
        if (t->texture && texture_index(info, t) == info->count) {
            uint64_t n = (uint64_t)t->texture_width * (unsigned)t->texture_height;
            if (info->count == MAX_VIEWS || n > MAX_TEXELS - (unsigned)info->texels)
                return 0;
            info->views[info->count++] = (texture_view){t->texture, t->texture_width, t->texture_height,
                                                        t->texture_bpp, info->texels};
            info->texels += (int)n;
        }
        info->seed_depth |= t->depth_test || t->depth_write;
        info->write_depth |= t->depth_write;
        if (!prepare_pipeline(r, t))
            return 0;
    }
    return 1;
}
static material get_material(const nj_gfx_triangle_cmd *t, const packet_info *p) {
    material m = {0};
    m.threshold = t->alpha_threshold;
    m.blend = t->blend;
    m.fog = t->fog;
    if (t->fog) {
        m.fog_color = t->fog_color;
        m.near_dist = t->fog_near;
        m.far_dist = t->fog_far;
        m.density = t->fog_density;
    }
    if (t->texture) {
        const texture_view *v = &p->views[texture_index(p, t)];
        m.offset = v->offset;
        m.width = v->width;
        m.height = v->height;
        m.function = t->texture_function;
        m.flags = (t->wrap_s == 241) | ((t->wrap_t == 241) << 1) | ((t->filter != 208) << 2) |
                  ((t->texture_no_alpha || v->bpp == 3) << 3) | ((t->filter == NJ_GFX_FILTER_SHARP) << 4);
    }
    return m;
}
static void upload_geometry(nj_vk_renderer *r, const nj_gfx_triangles *p, const packet_info *info) {
    int j;
    nj_u4 i;
    vertex *out = r->vertices.map;
    for (i = 0; i < p->count; ++i)
        for (j = 0; j < 3; ++j, ++out) {
            const nj_gfx_triangle_vertex *v = &p->triangles[i].vertices[j];
            out->position[0] = (float)((v->x - (double)p->x) * 2.0 / p->width - 1.0);
            out->position[1] = (float)((v->y - (double)p->y) * 2.0 / p->height - 1.0);
            out->position[2] = (float)v->depth;
            out->uvq[0] = v->u * v->inv_w;
            out->uvq[1] = v->v * v->inv_w;
            out->uvq[2] = v->inv_w;
            out->rgba[0] = (uint8_t)(v->color >> 16);
            out->rgba[1] = (uint8_t)(v->color >> 8);
            out->rgba[2] = (uint8_t)v->color;
            out->rgba[3] = (uint8_t)(v->color >> 24);
        }
    for (j = 0; j < info->count; ++j) {
        const texture_view *v = &info->views[j];
        uint32_t *texels = (uint32_t *)r->textures.map + v->offset;
        const uint8_t *src = v->pixels;
        const uint32_t one = 1;
        size_t count = (size_t)v->width * v->height;
        /* RGBA byte order already matches the shader's packed uint on LE.
         * Always copy the current contents, even when the pointer repeats. */
        if (v->bpp == 4 && *(const uint8_t *)&one) {
            memcpy(texels, src, count * sizeof *texels);
            continue;
        }
        for (size_t k = 0; k < count; ++k, src += v->bpp)
            texels[k] = src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
                        ((v->bpp == 4 ? (uint32_t)src[3] : 255u) << 24);
    }
}
/* Mapped staging and caller storage are disjoint. Fixed-size memcpy permits
 * odd surface addresses and strides, and a fixed width leaves each color loop
 * independent of writes through caller-provided byte pointers. */
static void import_rgb565(uint8_t *restrict bgra, const uint8_t *restrict row, int width) {
    for (int x = 0; x < width; ++x) {
        uint16_t rgb;
        memcpy(&rgb, row + 2 * (size_t)x, sizeof rgb);
        unsigned red = rgb >> 11, green = (rgb >> 5) & 63, blue = rgb & 31;
        bgra[4 * x + 0] = (uint8_t)((blue << 3) | (blue >> 2));
        bgra[4 * x + 1] = (uint8_t)((green << 2) | (green >> 4));
        bgra[4 * x + 2] = (uint8_t)((red << 3) | (red >> 2));
        bgra[4 * x + 3] = 255;
    }
}
static void export_rgb565(uint8_t *restrict row, const uint8_t *restrict bgra, int width) {
    for (int x = 0; x < width; ++x) {
        uint16_t rgb = (uint16_t)(((bgra[4 * x + 2] >> 3) << 11) | ((bgra[4 * x + 1] >> 2) << 5) |
                                  (bgra[4 * x] >> 3));
        memcpy(row + 2 * (size_t)x, &rgb, sizeof rgb);
    }
}
static void transfer_cpu(nj_vk_renderer *r, const nj_gfx_triangles *p, int depth, int output) {
    const nj_surface *s = p->surface;
    int bpp = s->format == NJ_PIXFMT_ARGB8888 ? 4 : s->format == NJ_PIXFMT_RGB888 ? 3 : 2;
    size_t n = (size_t)p->width * p->height;
    uint8_t *bytes = r->stage.map;
    float *z = (float *)(bytes + n * 4);
    int x, y;
    const int width = p->width;
    for (y = 0; y < p->height; ++y) {
        uint8_t *row = (uint8_t *)s->pixels + (size_t)(p->y + y) * s->stride + (size_t)p->x * bpp;
        uint8_t *bgra = bytes + (size_t)y * p->width * 4;
        /* Vulkan platforms supported by this backend use little-endian packed ARGB. */
        if (bpp == 4) {
            if (output)
                memcpy(row, bgra, (size_t)p->width * 4);
            else
                memcpy(bgra, row, (size_t)p->width * 4);
        } else if (bpp == 2) {
            if (output)
                export_rgb565(row, bgra, width);
            else
                import_rgb565(bgra, row, width);
        } else {
            for (x = 0; x < width; ++x, row += 3, bgra += 4) {
                if (output) {
                    row[0] = bgra[2];
                    row[1] = bgra[1];
                    row[2] = bgra[0];
                } else {
                    bgra[2] = row[0];
                    bgra[1] = row[1];
                    bgra[0] = row[2];
                    bgra[3] = 255;
                }
            }
        }
        if (depth) {
            nj_s4 *keys = p->depth + (size_t)(p->y + y) * p->depth_stride + p->x;
            float *values = z + (size_t)y * p->width;
            if (output)
                for (x = 0; x < width; ++x)
                    keys[x] = 16777216 - (nj_s4)(values[x] * 16777216.0f);
            else
                for (x = 0; x < width; ++x)
                    values[x] = (float)(16777216 - keys[x]) * (1.0f / 16777216.0f);
        }
    }
}
static void draws(nj_vk_renderer *r, const nj_gfx_triangles *p, const packet_info *info) {
    VkRenderPassBeginInfo begin = VK_INIT(RENDER_PASS_BEGIN_INFO);
    VkViewport vp = {0, 0, (float)p->width, (float)p->height, 0, 1};
    VkDeviceSize zero = 0;
    nj_u4 i;
    unsigned prev_index = PIPELINES;
    begin.renderPass = r->pass;
    begin.framebuffer = r->fb;
    begin.renderArea.extent = (VkExtent2D){(uint32_t)p->width, (uint32_t)p->height};
    vkCmdBeginRenderPass(r->cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(r->cmd, 0, 1, &vp);
    vkCmdSetScissor(r->cmd, 0, 1, &begin.renderArea);
    vkCmdBindVertexBuffers(r->cmd, 0, 1, &r->vertices.id, &zero);
    vkCmdBindDescriptorSets(r->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->layout, 0, 1, &r->set, 0, NULL);
    for (i = 0; i < p->count;) {
        const nj_gfx_triangle_cmd *t = &p->triangles[i];
        unsigned index = pipeline_index(t);
        material m = get_material(t, info);
        nj_u4 end = i + 1;
        /* Preserve submission order while coalescing consecutive material runs. */
        while (end < p->count && pipeline_index(&p->triangles[end]) == index) {
            material next = get_material(&p->triangles[end], info);
            if (memcmp(&m, &next, sizeof m))
                break;
            ++end;
        }
        if (index != prev_index) {
            vkCmdBindPipeline(r->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines[index]);
            prev_index = index;
        }
        vkCmdPushConstants(r->cmd, r->layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof m, &m);
        vkCmdDraw(r->cmd, (end - i) * 3, 1, i * 3, 0);
        i = end;
    }
    vkCmdEndRenderPass(r->cmd);
}
static int execute(nj_vk_renderer *r, const nj_gfx_triangles *p, const packet_info *info) {
    VkCommandBufferBeginInfo cb = VK_INIT(COMMAND_BUFFER_BEGIN_INFO);
    VkImageMemoryBarrier barriers[2] = {VK_INIT(IMAGE_MEMORY_BARRIER), VK_INIT(IMAGE_MEMORY_BARRIER)};
    VkBufferImageCopy copies[2] = {{0}};
    VkMemoryBarrier host = VK_INIT(MEMORY_BARRIER);
    VkSubmitInfo submit = VK_INIT(SUBMIT_INFO);
    int i;
    cb.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(r->cmd, &cb) != VK_SUCCESS)
        return 0;
    for (i = 0; i < 2; ++i) {
        barriers[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[i].srcQueueFamilyIndex = barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[i].image = r->attachments[i].id;
        barriers[i].subresourceRange = (VkImageSubresourceRange){aspects[i], 0, 1, 0, 1};
        copies[i].bufferOffset = (VkDeviceSize)i * (unsigned)p->width * (unsigned)p->height * 4;
        copies[i].imageSubresource = (VkImageSubresourceLayers){aspects[i], 0, 0, 1};
        copies[i].imageExtent = (VkExtent3D){(uint32_t)p->width, (uint32_t)p->height, 1};
    }
    vkCmdPipelineBarrier(r->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, NULL, 0, NULL, 2, barriers);
    for (i = 0; i < 1 + info->seed_depth; ++i)
        vkCmdCopyBufferToImage(r->cmd, r->stage.id, r->attachments[i].id,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copies[i]);
    draws(r, p, info);
    for (i = 0; i < 1 + info->write_depth; ++i)
        vkCmdCopyImageToBuffer(r->cmd, r->attachments[i].id, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               r->stage.id, 1, &copies[i]);
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(r->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                         &host, 0, NULL, 0, NULL);
    if (vkEndCommandBuffer(r->cmd) != VK_SUCCESS || vkResetFences(r->dev, 1, &r->fence) != VK_SUCCESS)
        return 0;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &r->cmd;
    if (vkQueueSubmit(r->queue, 1, &submit, r->fence) != VK_SUCCESS ||
        vkWaitForFences(r->dev, 1, &r->fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        /* No future packet may reuse potentially in-flight storage. */
        r->lost = 1;
        return 0;
    }
    return sync_buffer(r, &r->stage, 1);
}
int nj_vk_renderer_triangles(void *ud, const nj_gfx_triangles *p, nj_u4 *submitted) {
    nj_vk_renderer *r = ud;
    packet_info info = {0};
    VkDescriptorBufferInfo desc = {0};
    VkWriteDescriptorSet write = VK_INIT(WRITE_DESCRIPTOR_SET);
    uint64_t n;
    if (!r || r->lost || !p || p->width <= 0 || p->height <= 0)
        return NJ_GFX_ACCEL_DECLINE;
    n = (uint64_t)p->width * (unsigned)p->height;
    if (n > MAX_PIXELS || (unsigned)p->width > r->max_image_width ||
        (unsigned)p->height > r->max_image_height ||
        (unsigned)p->width > r->properties.limits.maxFramebufferWidth ||
        (unsigned)p->height > r->properties.limits.maxFramebufferHeight ||
        (unsigned)p->width > r->properties.limits.maxViewportDimensions[0] ||
        (unsigned)p->height > r->properties.limits.maxViewportDimensions[1] ||
        !nj_gfx_triangles_validate(p, submitted) || !analyze(r, p, &info))
        return NJ_GFX_ACCEL_DECLINE;
    if (vkResetCommandPool(r->dev, r->pool, 0) != VK_SUCCESS || !target(r, p->width, p->height) ||
        !reserve_buffer(r, &r->stage, n * 4 * (1 + info.seed_depth),
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT) ||
        !reserve_buffer(r, &r->vertices, p->count * 3 * sizeof(vertex),
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
        !reserve_buffer(r, &r->textures, info.texels ? (unsigned)info.texels * 4u : 4u,
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
        return NJ_GFX_ACCEL_DECLINE;
    desc.buffer = r->textures.id;
    desc.range = info.texels ? (unsigned)info.texels * 4u : 4u;
    if (desc.range > r->properties.limits.maxStorageBufferRange)
        return NJ_GFX_ACCEL_DECLINE;
    write.dstSet = r->set;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &desc;
    vkUpdateDescriptorSets(r->dev, 1, &write, 0, NULL);
    upload_geometry(r, p, &info);
    transfer_cpu(r, p, info.seed_depth, 0);
    if (!sync_buffer(r, &r->vertices, 0) || !sync_buffer(r, &r->textures, 0) ||
        !sync_buffer(r, &r->stage, 0) || !execute(r, p, &info))
        return NJ_GFX_ACCEL_DECLINE;
    transfer_cpu(r, p, info.write_depth, 1);
    *submitted = p->count;
    return NJ_GFX_ACCEL_DONE;
}
