#if defined(__SSE2__)
#include <emmintrin.h>
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#endif

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
static void upload_geometry_at(nj_vk_renderer *r, const nj_gfx_triangles *p, const packet_info *info,
                               vertex *out) {
    int j;
    nj_u4 i;
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
/* The colour round trip copies up to a whole physical frame per packet. Plain
 * stores would fetch every cache line before overwriting it, so copy large
 * aligned runs with non-temporal stores. Same bytes, only the store hint
 * differs. */
static void copy_words(uint8_t *restrict dst, const uint8_t *restrict src, size_t bytes) {
#if !defined(NJ_VK_NO_STREAM) && defined(__SSE2__)
    size_t i = 0;
    if ((((uintptr_t)dst | (uintptr_t)src) & 15u) == 0) {
        for (; i + 64 <= bytes; i += 64) {
            _mm_stream_si128((__m128i *)(dst + i), _mm_loadu_si128((const __m128i *)(src + i)));
            _mm_stream_si128((__m128i *)(dst + i + 16), _mm_loadu_si128((const __m128i *)(src + i + 16)));
            _mm_stream_si128((__m128i *)(dst + i + 32), _mm_loadu_si128((const __m128i *)(src + i + 32)));
            _mm_stream_si128((__m128i *)(dst + i + 48), _mm_loadu_si128((const __m128i *)(src + i + 48)));
        }
        for (; i + 16 <= bytes; i += 16)
            _mm_stream_si128((__m128i *)(dst + i), _mm_loadu_si128((const __m128i *)(src + i)));
        _mm_sfence();
    }
    if (i < bytes)
        memcpy(dst + i, src + i, bytes - i);
#else
    memcpy(dst, src, bytes);
#endif
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
/* Depth keys are integer positions on the 24-bit grid that the software
 * rasteriser interpolates; the GPU depth attachment is D32_SFLOAT. Converting
 * both directions per packet is a per-pixel scalar float round trip, and at
 * physical resolution it is one of the larger per-transaction costs. These two
 * helpers do the same arithmetic four lanes at a time (the SSE2 and NEON paths
 * round exactly like the scalar expressions, so queued and direct draws stay
 * bit-identical). */
static void depth_import(nj_s4 *keys, float *values, int width) {
    int x = 0;
#if defined(__SSE2__)
    const __m128i top = _mm_set1_epi32(16777216);
    const __m128 scale = _mm_set1_ps(1.0f / 16777216.0f);
    for (; x + 4 <= width; x += 4) {
        __m128i k = _mm_loadu_si128((const __m128i *)(keys + x));
        __m128 v = _mm_cvtepi32_ps(_mm_sub_epi32(top, k));
        _mm_storeu_ps((float *)(values + x), _mm_mul_ps(v, scale));
    }
#elif defined(__ARM_NEON)
    const int32x4_t top = vdupq_n_s32(16777216);
    const float32x4_t scale = vdupq_n_f32(1.0f / 16777216.0f);
    for (; x + 4 <= width; x += 4) {
        int32x4_t k = vld1q_s32(keys + x);
        float32x4_t v = vcvtq_f32_s32(vsubq_s32(top, k));
        vst1q_f32((float *)(values + x), vmulq_f32(v, scale));
    }
#endif
    for (; x < width; ++x)
        values[x] = (float)(16777216 - keys[x]) * (1.0f / 16777216.0f);
}

static void depth_export(nj_s4 *keys, const float *values, int width) {
    int x = 0;
#if defined(__SSE2__)
    const __m128 scale = _mm_set1_ps(16777216.0f);
    const __m128i top = _mm_set1_epi32(16777216);
    for (; x + 4 <= width; x += 4) {
        __m128 v = _mm_loadu_ps(values + x);
        __m128i k = _mm_cvttps_epi32(_mm_mul_ps(v, scale));
        _mm_storeu_si128((__m128i *)(keys + x), _mm_sub_epi32(top, k));
    }
#elif defined(__ARM_NEON)
    const float32x4_t scale = vdupq_n_f32(16777216.0f);
    const int32x4_t top = vdupq_n_s32(16777216);
    for (; x + 4 <= width; x += 4) {
        float32x4_t v = vld1q_f32(values + x);
        int32x4_t k = vcvtq_s32_f32(vmulq_f32(v, scale));
        vst1q_s32(keys + x, vsubq_s32(top, k));
    }
#endif
    for (; x < width; ++x)
        keys[x] = 16777216 - (nj_s4)(values[x] * 16777216.0f);
}

/* Copy one rectangle between the staged image and the caller's surface / depth
 * keys. The staged rectangle starts at `colour_offset` bytes (and its float
 * depth plane at `depth_offset`), which lets a retained session pack several
 * dirty rectangles into one staging buffer. */
static void transfer_rect_at(nj_vk_renderer *r, const nj_surface *s, nj_s4 *depth,
                             int depth_stride, int rx, int ry, int width, int height,
                             int want_depth, int output, size_t colour_offset,
                             size_t depth_offset) {
    int bpp = s->format == NJ_PIXFMT_ARGB8888 ? 4 : s->format == NJ_PIXFMT_RGB888 ? 3 : 2;
    uint8_t *bytes = (uint8_t *)r->stage.map + colour_offset;
    float *z = (float *)((uint8_t *)r->stage.map + depth_offset);
    int x, y;
    for (y = 0; y < height; ++y) {
        uint8_t *row = (uint8_t *)s->pixels + (size_t)(ry + y) * s->stride + (size_t)rx * bpp;
        uint8_t *bgra = bytes + (size_t)y * width * 4;
        /* Vulkan platforms supported by this backend use little-endian packed ARGB. */
        if (bpp == 4) {
            if (output)
                copy_words(row, bgra, (size_t)width * 4);
            else
                copy_words(bgra, row, (size_t)width * 4);
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
        if (want_depth) {
            nj_s4 *keys = depth + (size_t)(ry + y) * depth_stride + rx;
            float *values = z + (size_t)y * width;
            if (output)
                depth_export(keys, values, width);
            else
                depth_import(keys, values, width);
        }
    }
}
/* Framed copy: colour plane at the front of the staging buffer and the depth
 * plane right after it. */
static void transfer_rect(nj_vk_renderer *r, const nj_surface *s, nj_s4 *depth, int depth_stride,
                          int rx, int ry, int width, int height, int want_depth, int output) {
    size_t n = (size_t)width * height;
    transfer_rect_at(r, s, depth, depth_stride, rx, ry, width, height, want_depth, output, 0,
                     n * 4u);
}
static void transfer_cpu(nj_vk_renderer *r, const nj_gfx_triangles *p, int depth, int output) {
    transfer_rect(r, p->surface, p->depth, p->depth_stride, p->x, p->y, p->width, p->height, depth,
                  output);
}
static void draws_body(nj_vk_renderer *r, const nj_gfx_triangles *p, const packet_info *info,
                       nj_u4 vertex_base) {
    unsigned prev_index = PIPELINES;
    nj_u4 i;
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
        vkCmdDraw(r->cmd, (end - i) * 3, 1, vertex_base + i * 3, 0);
        i = end;
    }
}
/* Render one packet into the framebuffer-sized attachments (all packets share
 * the same staged import, so the pass extent is the packet rectangle). */
static void draws(nj_vk_renderer *r, const nj_gfx_triangles *p, const packet_info *info) {
    VkRenderPassBeginInfo begin = VK_INIT(RENDER_PASS_BEGIN_INFO);
    VkViewport vp = {0, 0, (float)p->width, (float)p->height, 0, 1};
    VkDeviceSize zero = 0;
    begin.renderPass = r->pass;
    begin.framebuffer = r->fb;
    begin.renderArea.extent = (VkExtent2D){(uint32_t)p->width, (uint32_t)p->height};
    vkCmdBeginRenderPass(r->cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(r->cmd, 0, 1, &vp);
    vkCmdSetScissor(r->cmd, 0, 1, &begin.renderArea);
    vkCmdBindVertexBuffers(r->cmd, 0, 1, &r->vertices.id, &zero);
    vkCmdBindDescriptorSets(r->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->layout, 0, 1, &r->set, 0, NULL);
    draws_body(r, p, info, 0);
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
    if (r->session_keep) {
        /* Park the resident attachments back in the transfer destination layout
         * so the next retained begin() can copy into them again. */
        for (i = 0; i < 2; ++i) {
            barriers[i].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barriers[i].dstAccessMask = i ? (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
                                          : (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
            barriers[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barriers[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }
        vkCmdPipelineBarrier(r->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, 0, NULL, 0, NULL, 2, barriers);
    }
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
/* ------------------------------------------------------------------------- */
/* Persistent sessions                                                       */
/*                                                                           */
/* A session keeps the colour and depth attachments resident in device       */
/* memory. begin() imports the framed rectangle once, every submission       */
/* renders its packets into the resident attachments without reading        */
/* anything back, and end() exports the rectangle exactly once. Each         */
/* submission is still submitted and waited on its own, so staging buffers   */
/* may be reallocated between calls; the contract only forbids observing the */
/* target while the session is open.                                         */
/* ------------------------------------------------------------------------- */
static int session_surface_matches(const nj_vk_renderer *r, const nj_gfx_triangles *p) {
    const nj_surface *s = p->surface;
    if (!s)
        return 0;
    return s->pixels == r->session_surface.pixels && s->width == r->session_surface.width &&
           s->height == r->session_surface.height && s->format == r->session_surface.format &&
           s->bpp == r->session_surface.bpp && s->stride == r->session_surface.stride &&
           p->depth == r->session_depth && p->depth_stride == r->session_depth_stride;
}
static int session_contains(const nj_vk_renderer *r, const nj_gfx_triangles *p) {
    return p->x >= r->session_x && p->y >= r->session_y &&
           p->x + p->width <= r->session_x + r->session_width &&
           p->y + p->height <= r->session_y + r->session_height;
}
/* Queue a batch of packets into the open session and submit it. Returns 0 on
 * failure (the caller must then abort the session), 1 with the triangle count
 * on success. */
static int session_record(nj_vk_renderer *r, const nj_gfx_triangles *const *packets, nj_u4 count,
                          nj_u4 *triangles) {
    packet_info *infos;
    VkDescriptorBufferInfo desc = {0};
    VkWriteDescriptorSet write = VK_INIT(WRITE_DESCRIPTOR_SET);
    VkCommandBufferBeginInfo cb = VK_INIT(COMMAND_BUFFER_BEGIN_INFO);
    VkMemoryBarrier sync = VK_INIT(MEMORY_BARRIER);
    VkSubmitInfo submit = VK_INIT(SUBMIT_INFO);
    nj_u4 i, texels = 0, vertices = 0, vertex_base = 0, total = 0;
    double t0 = vk_prof_now();
    if (r->session_failed || !count)
        return 0;
    infos = (packet_info *)r->session_infos;
    if (r->session_infos_cap < count) {
        packet_info *grown = (packet_info *)realloc(infos, (size_t)count * sizeof(*infos));
        if (!grown)
            return 0;
        r->session_infos = infos = grown;
        r->session_infos_cap = count;
    }
    for (i = 0; i < count; ++i) {
        const nj_gfx_triangles *p = packets[i];
        nj_u4 one = 0, j;
        memset(&infos[i], 0, sizeof infos[i]);
        if (!p || !session_surface_matches(r, p) || !session_contains(r, p) ||
            !nj_gfx_triangles_validate(p, &one) || !analyze(r, p, &infos[i])) {
            r->session_failed = 1;
            return 0;
        }
        for (j = 0; j < (nj_u4)infos[i].count; ++j)
            infos[i].views[j].offset += (int)texels;
        texels += (nj_u4)infos[i].texels;
        vertices += p->count * 3u;
        total += p->count;
    }
    if (!reserve_buffer(r, &r->vertices, (VkDeviceSize)vertices * sizeof(vertex),
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
        !reserve_buffer(r, &r->textures, texels ? (VkDeviceSize)texels * 4u : 4u,
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
        r->session_failed = 1;
        return 0;
    }
    desc.buffer = r->textures.id;
    desc.range = texels ? (VkDeviceSize)texels * 4u : 4u;
    if (desc.range > r->properties.limits.maxStorageBufferRange) {
        r->session_failed = 1;
        return 0;
    }
    if (!r->desc_valid || desc.buffer != r->desc_buffer || desc.range > r->desc_range) {
        write.dstSet = r->set;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &desc;
        vkUpdateDescriptorSets(r->dev, 1, &write, 0, NULL);
        r->desc_buffer = desc.buffer;
        r->desc_range = desc.range;
        r->desc_valid = 1;
    }
    for (i = 0; i < count; ++i) {
        upload_geometry_at(r, packets[i], &infos[i], (vertex *)r->vertices.map + vertex_base);
        vertex_base += packets[i]->count * 3u;
    }
    if (!sync_buffer(r, &r->vertices, 0) || !sync_buffer(r, &r->textures, 0))
        return 0;
    if (vkResetCommandPool(r->dev, r->pool, 0) != VK_SUCCESS)
        return 0;
    cb.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(r->cmd, &cb) != VK_SUCCESS)
        return 0;
    /* The previous submission (or the import) wrote colour, depth or staging
     * memory; make those writes visible to the incoming fragment work. */
    sync.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_TRANSFER_WRITE_BIT;
    sync.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(r->cmd,
                         VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, 1, &sync, 0, NULL, 0, NULL);
    {
        VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(r->cmd, 0, 1, &r->vertices.id, &zero);
        vkCmdBindDescriptorSets(r->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->layout, 0, 1, &r->set, 0,
                                NULL);
    }
    /* One render pass covers the whole batch: dynamic viewport and scissor
     * keep each packet in its own rectangle while the attachments are loaded
     * once and stored once for the whole session. */
    {
        VkRect2D full = {(VkOffset2D){0, 0},
                         (VkExtent2D){(uint32_t)r->session_width, (uint32_t)r->session_height}};
        VkRenderPassBeginInfo begin = VK_INIT(RENDER_PASS_BEGIN_INFO);
        begin.renderPass = r->session_pass;
        begin.framebuffer = r->session_fb;
        begin.renderArea = full;
        vkCmdBeginRenderPass(r->cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
        vertex_base = 0;
        for (i = 0; i < count; ++i) {
            const nj_gfx_triangles *p = packets[i];
            VkViewport vp = {(float)(p->x - r->session_x), (float)(p->y - r->session_y),
                             (float)p->width, (float)p->height, 0, 1};
            VkRect2D area = {(VkOffset2D){p->x - r->session_x, p->y - r->session_y},
                             (VkExtent2D){(uint32_t)p->width, (uint32_t)p->height}};
            vkCmdSetViewport(r->cmd, 0, 1, &vp);
            vkCmdSetScissor(r->cmd, 0, 1, &area);
            draws_body(r, p, &infos[i], vertex_base);
            if (infos[i].write_depth)
                r->session_depth_written = 1;
            vertex_base += p->count * 3u;
        }
        vkCmdEndRenderPass(r->cmd);
    }
    if (vkEndCommandBuffer(r->cmd) != VK_SUCCESS ||
        vkResetFences(r->dev, 1, &r->fence) != VK_SUCCESS)
        return 0;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &r->cmd;
    if (vkQueueSubmit(r->queue, 1, &submit, r->fence) != VK_SUCCESS ||
        vkWaitForFences(r->dev, 1, &r->fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        r->lost = 1;
        return 0;
    }
    r->prof_render += vk_prof_now() - t0;
    r->session_submitted += total;
    *triangles = total;
    return 1;
}
/* A retained begin() may import at most this many dirty rectangles; more
 * means the caller's dirty tracking is coarse and a full import is cheaper. */
#define SESSION_DIRTY_MAX 8u
/* True when the attachments still hold the exported result of exactly this
 * surface rectangle, so a retained begin() may skip the full import. */
static int session_matches(const nj_vk_renderer *r, const nj_gfx_triangles_session *s) {
    return r->session_retained && s->surface &&
           s->surface->pixels == r->session_surface.pixels &&
           s->surface->width == r->session_surface.width &&
           s->surface->height == r->session_surface.height &&
           s->surface->format == r->session_surface.format &&
           s->surface->bpp == r->session_surface.bpp &&
           s->surface->stride == r->session_surface.stride &&
           s->depth == r->session_depth && s->depth_stride == r->session_depth_stride &&
           s->x == r->session_x && s->y == r->session_y && s->width == r->session_width &&
           s->height == r->session_height;
}
int nj_vk_renderer_triangles_begin(void *ud, const nj_gfx_triangles_session *s) {
    nj_vk_renderer *r = ud;
    VkCommandBufferBeginInfo cb = VK_INIT(COMMAND_BUFFER_BEGIN_INFO);
    VkImageMemoryBarrier barriers[2] = {VK_INIT(IMAGE_MEMORY_BARRIER), VK_INIT(IMAGE_MEMORY_BARRIER)};
    VkBufferImageCopy copies[1 + SESSION_DIRTY_MAX] = {{0}};
    VkMemoryBarrier sync = VK_INIT(MEMORY_BARRIER);
    VkSubmitInfo submit = VK_INIT(SUBMIT_INFO);
    nj_u4 n, i, dirty = 0;
    size_t colour_bytes = 0;
    int depth, keep = 0, retained = 0;
    if (!r || r->lost || !s || !s->surface)
        return NJ_GFX_ACCEL_DECLINE;
    if (s->version != 1u && s->version != NJ_GFX_TRIANGLES_SESSION_VERSION)
        return NJ_GFX_ACCEL_DECLINE;
    if (s->width <= 0 || s->height <= 0 || s->x < 0 || s->y < 0)
        return NJ_GFX_ACCEL_DECLINE;
    if (s->width > s->surface->width - s->x || s->height > s->surface->height - s->y)
        return NJ_GFX_ACCEL_DECLINE;
    n = (nj_u4)s->width * (nj_u4)s->height;
    if (n > MAX_PIXELS || (unsigned)s->width > r->max_image_width ||
        (unsigned)s->height > r->max_image_height ||
        (unsigned)s->width > r->properties.limits.maxFramebufferWidth ||
        (unsigned)s->height > r->properties.limits.maxFramebufferHeight ||
        (unsigned)s->width > r->properties.limits.maxViewportDimensions[0] ||
        (unsigned)s->height > r->properties.limits.maxViewportDimensions[1])
        return NJ_GFX_ACCEL_DECLINE;
    depth = s->depth ? 1 : 0;
    if (depth && s->depth_stride < s->width)
        return NJ_GFX_ACCEL_DECLINE;
    if (s->version >= NJ_GFX_TRIANGLES_SESSION_VERSION && s->retain)
        keep = 1;
    /* Capture the retention hint before abort() clears the session flags; the
     * provisional flag lets target() drop it when it must recreate the
     * attachments. Importing only dirty colour rectangles also requires an
     * untouched CPU depth buffer, which `depth_retain` asserts. */
    retained = keep && r->session_retained && session_matches(r, s) && (!depth || s->depth_retain);
    nj_vk_renderer_triangles_abort(r);
    r->session_retained = retained;
    r->session_keep = keep;
    if (!target(r, s->width, s->height) ||
        !reserve_buffer(r, &r->stage, (VkDeviceSize)n * 4u * (VkDeviceSize)(1 + depth),
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)) {
        r->session_retained = 0;
        return NJ_GFX_ACCEL_DECLINE;
    }
    retained = retained && r->session_retained;
    {
    double t0 = vk_prof_now();
    if (retained && s->dirty_count) {
        if (!s->dirty || s->dirty_count > SESSION_DIRTY_MAX) {
            retained = 0;
        } else {
            for (i = 0; i < s->dirty_count; ++i) {
                const nj_gfx_session_rect *d = &s->dirty[i];
                if (d->width <= 0 || d->height <= 0 || d->x < s->x || d->y < s->y ||
                    d->x + d->width > s->x + s->width || d->y + d->height > s->y + s->height) {
                    retained = 0;
                    break;
                }
                dirty += (nj_u4)d->width * (nj_u4)d->height;
            }
            if ((VkDeviceSize)dirty * 4u > (VkDeviceSize)n * 4u)
                retained = 0;
        }
    }
    r->session_retained = retained;
    if (!retained) {
        transfer_rect(r, s->surface, s->depth, s->depth_stride, s->x, s->y, s->width, s->height,
                      depth, 0);
    } else {
        for (i = 0; i < s->dirty_count; ++i) {
            const nj_gfx_session_rect *d = &s->dirty[i];
            size_t bytes = (size_t)d->width * (size_t)d->height * 4u;
            transfer_rect_at(r, s->surface, NULL, 0, d->x, d->y, d->width, d->height, 0, 0,
                             colour_bytes, colour_bytes);
            colour_bytes += bytes;
        }
    }
    r->session_surface = *s->surface;
    r->session_depth = s->depth;
    r->session_depth_stride = s->depth_stride;
    r->session_x = s->x;
    r->session_y = s->y;
    r->session_width = s->width;
    r->session_height = s->height;
    r->session_depth_written = 0;
    r->session_submitted = 0;
    if (!sync_buffer(r, &r->stage, 0) || vkResetCommandPool(r->dev, r->pool, 0) != VK_SUCCESS) {
        r->session_retained = 0;
        return NJ_GFX_ACCEL_DECLINE;
    }
    cb.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(r->cmd, &cb) != VK_SUCCESS) {
        r->session_retained = 0;
        return NJ_GFX_ACCEL_DECLINE;
    }
    if (!retained) {
        for (i = 0; i < 2; ++i) {
            barriers[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barriers[i].srcQueueFamilyIndex = barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].image = r->attachments[i].id;
            barriers[i].subresourceRange = (VkImageSubresourceRange){aspects[i], 0, 1, 0, 1};
            copies[i].bufferOffset = (VkDeviceSize)i * n * 4u;
            copies[i].imageSubresource = (VkImageSubresourceLayers){aspects[i], 0, 0, 1};
            copies[i].imageExtent = (VkExtent3D){(uint32_t)s->width, (uint32_t)s->height, 1};
        }
        vkCmdPipelineBarrier(r->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 2, barriers);
        for (i = 0; i < 1 + depth; ++i)
            vkCmdCopyBufferToImage(r->cmd, r->stage.id, r->attachments[i].id,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copies[i]);
    } else if (s->dirty_count) {
        /* The attachments already hold everything except the caller's dirty
         * colour rectangles, so only those are copied back in. */
        barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[0].srcQueueFamilyIndex = barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].image = r->attachments[0].id;
        barriers[0].subresourceRange = (VkImageSubresourceRange){aspects[0], 0, 1, 0, 1};
        vkCmdPipelineBarrier(r->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, barriers);
        colour_bytes = 0;
        for (i = 0; i < s->dirty_count; ++i) {
            const nj_gfx_session_rect *d = &s->dirty[i];
            VkBufferImageCopy copy = {0};
            copy.bufferOffset = colour_bytes;
            copy.imageSubresource = (VkImageSubresourceLayers){aspects[0], 0, 0, 1};
            copy.imageOffset = (VkOffset3D){d->x - s->x, d->y - s->y, 0};
            copy.imageExtent = (VkExtent3D){(uint32_t)d->width, (uint32_t)d->height, 1};
            vkCmdCopyBufferToImage(r->cmd, r->stage.id, r->attachments[0].id,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            colour_bytes += (size_t)d->width * (size_t)d->height * 4u;
        }
    }
    sync.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    sync.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(r->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                         0, 1, &sync, 0, NULL, 0, NULL);
    if (vkEndCommandBuffer(r->cmd) != VK_SUCCESS ||
        vkResetFences(r->dev, 1, &r->fence) != VK_SUCCESS) {
        r->session_retained = 0;
        return NJ_GFX_ACCEL_DECLINE;
    }
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &r->cmd;
    if (vkQueueSubmit(r->queue, 1, &submit, r->fence) != VK_SUCCESS ||
        vkWaitForFences(r->dev, 1, &r->fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        r->lost = 1;
        r->session_retained = 0;
        return NJ_GFX_ACCEL_DECLINE;
    }
    r->prof_import += vk_prof_now() - t0;
    ++r->prof_sessions;
    }
    r->session_open = 1;
    r->session_failed = 0;
    return NJ_GFX_ACCEL_DONE;
}
int nj_vk_renderer_triangles_end(void *ud, nj_u4 *submitted) {
    nj_vk_renderer *r = ud;
    VkCommandBufferBeginInfo cb = VK_INIT(COMMAND_BUFFER_BEGIN_INFO);
    VkImageMemoryBarrier barriers[2] = {VK_INIT(IMAGE_MEMORY_BARRIER), VK_INIT(IMAGE_MEMORY_BARRIER)};
    VkBufferImageCopy copies[2] = {{0}};
    VkMemoryBarrier host = VK_INIT(MEMORY_BARRIER);
    VkSubmitInfo submit = VK_INIT(SUBMIT_INFO);
    int i, depth;
    nj_u4 total;
    double t0 = vk_prof_now();
    if (!r || !submitted || !r->session_open)
        return NJ_GFX_ACCEL_DECLINE;
    if (r->session_failed) {
        r->session_open = 0;
        r->session_retained = 0;
        return NJ_GFX_ACCEL_DECLINE;
    }
    total = r->session_submitted;
    if (!total) {
        /* Nothing was drawn, so the attachments still match the CPU import and
         * can be retained for the next begin(). */
        r->session_open = 0;
        r->session_retained = r->session_keep;
        *submitted = 0;
        return NJ_GFX_ACCEL_DONE;
    }
    depth = r->session_depth && r->session_depth_written;
    cb.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkResetCommandPool(r->dev, r->pool, 0) != VK_SUCCESS ||
        vkBeginCommandBuffer(r->cmd, &cb) != VK_SUCCESS) {
        r->lost = 1;
        r->session_open = 0;
        r->session_retained = 0;
        return NJ_GFX_ACCEL_DECLINE;
    }
    for (i = 0; i < 2; ++i) {
        barriers[i].srcAccessMask = i ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                                      : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barriers[i].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barriers[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[i].srcQueueFamilyIndex = barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[i].image = r->attachments[i].id;
        barriers[i].subresourceRange = (VkImageSubresourceRange){aspects[i], 0, 1, 0, 1};
        copies[i].bufferOffset =
            (VkDeviceSize)i * (unsigned)r->session_width * (unsigned)r->session_height * 4u;
        copies[i].imageSubresource = (VkImageSubresourceLayers){aspects[i], 0, 0, 1};
        copies[i].imageExtent =
            (VkExtent3D){(uint32_t)r->session_width, (uint32_t)r->session_height, 1};
    }
    vkCmdPipelineBarrier(r->cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 2, barriers);
    for (i = 0; i < 1 + depth; ++i)
        vkCmdCopyImageToBuffer(r->cmd, r->attachments[i].id, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               r->stage.id, 1, &copies[i]);
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(r->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                         &host, 0, NULL, 0, NULL);
    if (r->session_keep) {
        /* Park the resident attachments back in the transfer destination layout
         * so the next retained begin() can copy into them again. */
        for (i = 0; i < 2; ++i) {
            barriers[i].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barriers[i].dstAccessMask = i ? (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
                                          : (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
            barriers[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barriers[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }
        vkCmdPipelineBarrier(r->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, 0, NULL, 0, NULL, 2, barriers);
    }
    if (vkEndCommandBuffer(r->cmd) != VK_SUCCESS ||
        vkResetFences(r->dev, 1, &r->fence) != VK_SUCCESS) {
        r->lost = 1;
        r->session_open = 0;
        r->session_retained = 0;
        return NJ_GFX_ACCEL_DECLINE;
    }
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &r->cmd;
    if (vkQueueSubmit(r->queue, 1, &submit, r->fence) != VK_SUCCESS ||
        vkWaitForFences(r->dev, 1, &r->fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        r->lost = 1;
        r->session_open = 0;
        r->session_retained = 0;
        return NJ_GFX_ACCEL_DECLINE;
    }
    if (!sync_buffer(r, &r->stage, 1)) {
        r->lost = 1;
        r->session_open = 0;
        r->session_retained = 0;
        return NJ_GFX_ACCEL_DECLINE;
    }
    r->prof_export += vk_prof_now() - t0;
    r->session_open = 0;
    r->session_retained = r->session_keep;
    transfer_rect(r, &r->session_surface, r->session_depth, r->session_depth_stride, r->session_x,
                  r->session_y, r->session_width, r->session_height, depth, 1);
    *submitted = total;
    return NJ_GFX_ACCEL_DONE;
}
void nj_vk_renderer_triangles_abort(void *ud) {
    nj_vk_renderer *r = ud;
    if (!r)
        return;
    r->session_open = 0;
    r->session_failed = 0;
    /* A discarded session leaves attachments that do not match the CPU buffers
     * any more, so the next begin() must import again. */
    r->session_retained = 0;
}
int nj_vk_renderer_triangles(void *ud, const nj_gfx_triangles *p, nj_u4 *submitted) {
    nj_vk_renderer *r = ud;
    packet_info info = {0};
    VkDescriptorBufferInfo desc = {0};
    VkWriteDescriptorSet write = VK_INIT(WRITE_DESCRIPTOR_SET);
    uint64_t n;
    if (!r || r->lost || !p || p->width <= 0 || p->height <= 0)
        return NJ_GFX_ACCEL_DECLINE;
    if (r->session_open) {
        const nj_gfx_triangles *one = p;
        nj_u4 triangles = 0;
        if (!session_record(r, &one, 1u, &triangles)) {
            r->session_failed = 1;
            return NJ_GFX_ACCEL_DECLINE;
        }
        *submitted = triangles;
        return NJ_GFX_ACCEL_DONE;
    }
    /* A standalone packet owns the attachments for one frame only. */
    r->session_retained = 0;
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
    /* Only rewrite the texture descriptor when the pooled buffer changed or the
     * packet needs a bigger range; doing it for every packet is measurable
     * driver overhead once the physical target is large. */
    if (!r->desc_valid || desc.buffer != r->desc_buffer || desc.range > r->desc_range) {
        write.dstSet = r->set;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &desc;
        vkUpdateDescriptorSets(r->dev, 1, &write, 0, NULL);
        r->desc_buffer = desc.buffer;
        r->desc_range = desc.range;
        r->desc_valid = 1;
    }
    upload_geometry_at(r, p, &info, (vertex *)r->vertices.map);
    transfer_cpu(r, p, info.seed_depth, 0);
    if (!sync_buffer(r, &r->vertices, 0) || !sync_buffer(r, &r->textures, 0) ||
        !sync_buffer(r, &r->stage, 0) || !execute(r, p, &info))
        return NJ_GFX_ACCEL_DECLINE;
    transfer_cpu(r, p, info.write_depth, 1);
    *submitted = p->count;
    return NJ_GFX_ACCEL_DONE;
}
/* A list transaction may commit several packets of one surface. This backend
 * keeps the per-packet path as the reference implementation: each packet is
 * validated and executed exactly as a standalone submission, in order, and the
 * whole list declines if any packet declines. Correctness first; a batched
 * command stream can replace this once it is proven pixel-identical. */
static int triangles_list_common(void *ud, const nj_gfx_triangles_list *l, nj_u4 *submitted,
                                 int checked) {
    nj_vk_renderer *r = ud;
    nj_u4 total = 0, done = 0, i;
    int rc = NJ_GFX_ACCEL_DECLINE;
    if (!r || !l || !submitted || (checked && !nj_gfx_triangles_list_validate(l, submitted)))
        return NJ_GFX_ACCEL_DECLINE;
    if (r->session_open) {
        nj_u4 triangles = 0;
        if (!session_record(r, l->packets, l->count, &triangles)) {
            r->session_failed = 1;
            return NJ_GFX_ACCEL_DECLINE;
        }
        *submitted = triangles;
        return NJ_GFX_ACCEL_DONE;
    }
    r->session_retained = 0;
    for (i = 0; i < l->count; ++i) {
        nj_u4 one = 0;
        if (nj_vk_renderer_triangles(r, l->packets[i], &one) != NJ_GFX_ACCEL_DONE)
            return NJ_GFX_ACCEL_DECLINE; /* rare: the journal replays in software */
        total += one;
        done = i + 1;
    }
    (void)done;
    *submitted = total;
    rc = NJ_GFX_ACCEL_DONE;
    return rc;
}

int nj_vk_renderer_triangles_list(void *ud, const nj_gfx_triangles_list *l, nj_u4 *submitted) {
    return triangles_list_common(ud, l, submitted, 1);
}

/* Internal entry for the journal: every packet was validated when it was
 * queued, so re-validating the whole list only costs CPU time. */
static int triangles_list_trusted(void *ud, const nj_gfx_triangles_list *l, nj_u4 *submitted) {
    return triangles_list_common(ud, l, submitted, 0);
}

/* ------------------------------------------------------------------------- */
/* Resident mirror                                                           */
/*                                                                           */
/* A colour image that stays in device memory between calls, so the caller   */
/* can compose the whole 8x frame on the GPU and pay one export per present. */
/* This first cut supports solid clears, rectangle fills and colour export;  */
/* textured 2D still uses the CPU path.                                      */
struct nj_vk_mirror {
    nj_vk_renderer *r;
    int width, height, ready;
    int depth_ready; /* depth attachment already moved out of UNDEFINED */
    attachment image, depth;
    VkRenderPass clear_pass, load_pass;
    VkFramebuffer clear_fb, load_fb;
    buffer stage;
};

/* Mirrored after create_pass(): same attachment formats and subpass layout so
 * the existing triangle pipelines are render-pass compatible with it. */
static VkRenderPass mirror_render_pass(nj_vk_renderer *r, VkAttachmentLoadOp load) {
    VkAttachmentDescription att[2] = {{0}};
    VkAttachmentReference color = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub = {0};
    VkRenderPassCreateInfo info = VK_INIT(RENDER_PASS_CREATE_INFO);
    VkRenderPass pass = VK_NULL_HANDLE;
    int i;
    for (i = 0; i < 2; ++i) {
        att[i].format = formats[i];
        att[i].samples = VK_SAMPLE_COUNT_1_BIT;
        att[i].loadOp = load;
        att[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[i].initialLayout = i ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                 : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att[i].finalLayout = att[i].initialLayout;
    }
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &color;
    sub.pDepthStencilAttachment = &depth;
    info.attachmentCount = 2;
    info.pAttachments = att;
    info.subpassCount = 1;
    info.pSubpasses = &sub;
    if (vkCreateRenderPass(r->dev, &info, NULL, &pass) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return pass;
}

static int mirror_framebuffer(nj_vk_mirror *m, VkRenderPass pass, VkFramebuffer *out) {
    VkFramebufferCreateInfo info = VK_INIT(FRAMEBUFFER_CREATE_INFO);
    VkImageView views[2];
    views[0] = m->image.view;
    views[1] = m->depth.view;
    info.renderPass = pass;
    info.attachmentCount = 2;
    info.pAttachments = views;
    info.width = (uint32_t)m->width;
    info.height = (uint32_t)m->height;
    info.layers = 1;
    return vkCreateFramebuffer(m->r->dev, &info, NULL, out) == VK_SUCCESS;
}

static int mirror_begin(nj_vk_renderer *r) {
    VkCommandBufferBeginInfo cb = VK_INIT(COMMAND_BUFFER_BEGIN_INFO);
    if (vkResetCommandPool(r->dev, r->pool, 0) != VK_SUCCESS)
        return 0;
    cb.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    return vkBeginCommandBuffer(r->cmd, &cb) == VK_SUCCESS;
}

static int mirror_submit(nj_vk_renderer *r) {
    VkSubmitInfo submit = VK_INIT(SUBMIT_INFO);
    if (vkEndCommandBuffer(r->cmd) != VK_SUCCESS ||
        vkResetFences(r->dev, 1, &r->fence) != VK_SUCCESS)
        return 0;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &r->cmd;
    if (vkQueueSubmit(r->queue, 1, &submit, r->fence) != VK_SUCCESS ||
        vkWaitForFences(r->dev, 1, &r->fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        r->lost = 1;
        return 0;
    }
    return 1;
}

static VkClearValue mirror_clear_value(nj_u4 argb) {
    VkClearValue value;
    /* The attachment is B8G8R8A8, so the float order is B, G, R, A. */
    value.color.float32[0] = (float)((argb >> 16) & 0xffu) / 255.0f;
    value.color.float32[1] = (float)((argb >> 8) & 0xffu) / 255.0f;
    value.color.float32[2] = (float)(argb & 0xffu) / 255.0f;
    value.color.float32[3] = (float)((argb >> 24) & 0xffu) / 255.0f;
    return value;
}

static void mirror_image_release(nj_vk_renderer *r, attachment *a) {
    vkDestroyImageView(r->dev, a->view, NULL);
    vkDestroyImage(r->dev, a->id, NULL);
    vkFreeMemory(r->dev, a->memory, NULL);
    memset(a, 0, sizeof *a);
}

void nj_vk_renderer_mirror_destroy(nj_vk_mirror *m) {
    nj_vk_renderer *r;
    if (!m)
        return;
    r = m->r;
    vkDestroyFramebuffer(r->dev, m->clear_fb, NULL);
    vkDestroyFramebuffer(r->dev, m->load_fb, NULL);
    vkDestroyRenderPass(r->dev, m->clear_pass, NULL);
    vkDestroyRenderPass(r->dev, m->load_pass, NULL);
    mirror_image_release(r, &m->image);
    mirror_image_release(r, &m->depth);
    release_buffer(r, &m->stage);
    free(m);
}

nj_vk_mirror *nj_vk_renderer_mirror_create(void *ud, int width, int height) {
    nj_vk_renderer *r = (nj_vk_renderer *)ud;
    nj_vk_mirror *m;
    VkImageCreateInfo info = VK_INIT(IMAGE_CREATE_INFO);
    VkImageViewCreateInfo view = VK_INIT(IMAGE_VIEW_CREATE_INFO);
    VkMemoryAllocateInfo alloc = VK_INIT(MEMORY_ALLOCATE_INFO);
    VkMemoryRequirements req;
    int i;
    if (!r || width <= 0 || height <= 0 ||
        (uint64_t)width * (uint64_t)height > MAX_PIXELS)
        return NULL;
    m = (nj_vk_mirror *)calloc(1, sizeof *m);
    if (!m)
        return NULL;
    m->r = r;
    m->width = width;
    m->height = height;
    for (i = 0; i < 2; ++i) {
        attachment *a = i ? &m->depth : &m->image;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent = (VkExtent3D){(uint32_t)width, (uint32_t)height, 1};
        info.mipLevels = info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.format = formats[i];
        info.usage = usages[i];
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
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
    }
    m->clear_pass = mirror_render_pass(r, VK_ATTACHMENT_LOAD_OP_CLEAR);
    m->load_pass = mirror_render_pass(r, VK_ATTACHMENT_LOAD_OP_LOAD);
    if (m->clear_pass == VK_NULL_HANDLE || m->load_pass == VK_NULL_HANDLE)
        goto fail;
    if (!mirror_framebuffer(m, m->clear_pass, &m->clear_fb) ||
        !mirror_framebuffer(m, m->load_pass, &m->load_fb))
        goto fail;
    if (!reserve_buffer(r, &m->stage, (VkDeviceSize)width * (VkDeviceSize)height * 4u,
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT))
        goto fail;
    return m;
fail:
    nj_vk_renderer_mirror_destroy(m);
    return NULL;
}

int nj_vk_renderer_mirror_clear(nj_vk_mirror *m, nj_u4 argb) {
    nj_vk_renderer *r;
    VkRenderPassBeginInfo pass = VK_INIT(RENDER_PASS_BEGIN_INFO);
    VkClearValue values[2];
    int i;
    if (!m)
        return 0;
    r = m->r;
    values[1].depthStencil.depth = 1.0f;
    values[1].depthStencil.stencil = 0;
    values[0] = mirror_clear_value(argb);
    if (!mirror_begin(r))
        return 0;
    if (!m->ready) {
        VkImageMemoryBarrier barrier[2] = {VK_INIT(IMAGE_MEMORY_BARRIER),
                                           VK_INIT(IMAGE_MEMORY_BARRIER)};
        for (i = 0; i < 2; ++i) {
            barrier[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier[i].newLayout = i ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                     : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier[i].srcQueueFamilyIndex = barrier[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier[i].image = i ? m->depth.id : m->image.id;
            barrier[i].subresourceRange = (VkImageSubresourceRange){aspects[i], 0, 1, 0, 1};
            barrier[i].dstAccessMask = i ? (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
                                         : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        }
        vkCmdPipelineBarrier(r->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                             0, 0, NULL, 0, NULL, 2, barrier);
    }
    pass.renderPass = m->clear_pass;
    pass.framebuffer = m->clear_fb;
    pass.renderArea.extent = (VkExtent2D){(uint32_t)m->width, (uint32_t)m->height};
    pass.clearValueCount = 2;
    pass.pClearValues = values;
    vkCmdBeginRenderPass(r->cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(r->cmd);
    if (!mirror_submit(r))
        return 0;
    m->ready = 1;
    return 1;
}

int nj_vk_renderer_mirror_fill(nj_vk_mirror *m, int x, int y, int width, int height, nj_u4 argb) {
    nj_vk_renderer *r;
    VkRenderPassBeginInfo pass = VK_INIT(RENDER_PASS_BEGIN_INFO);
    VkClearAttachment clear;
    VkClearRect rect;
    VkClearValue value;
    if (!m || !m->ready || width <= 0 || height <= 0)
        return 0;
    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x + width > m->width)
        width = m->width - x;
    if (y + height > m->height)
        height = m->height - y;
    if (width <= 0 || height <= 0)
        return 1;
    r = m->r;
    value = mirror_clear_value(argb);
    clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    clear.colorAttachment = 0;
    clear.clearValue = value;
    rect.rect.offset = (VkOffset2D){x, y};
    rect.rect.extent = (VkExtent2D){(uint32_t)width, (uint32_t)height};
    rect.baseArrayLayer = 0;
    rect.layerCount = 1;
    if (!mirror_begin(r))
        return 0;
    pass.renderPass = m->load_pass;
    pass.framebuffer = m->load_fb;
    pass.renderArea.extent = (VkExtent2D){(uint32_t)m->width, (uint32_t)m->height};
    vkCmdBeginRenderPass(r->cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdClearAttachments(r->cmd, 1, &clear, 1, &rect);
    vkCmdEndRenderPass(r->cmd);
    return mirror_submit(r);
}

/* The mapped readback buffer that receives the mirror image on export.  A
 * caller that keeps this memory as its CPU surface can export without the
 * extra host copy. */
void *nj_vk_renderer_mirror_pixels(nj_vk_mirror *m) {
    return m && m->stage.map ? m->stage.map : NULL;
}

int nj_vk_renderer_mirror_export(nj_vk_mirror *m, const nj_surface *dst) {
    nj_vk_renderer *r;
    VkImageMemoryBarrier barriers[2] = {VK_INIT(IMAGE_MEMORY_BARRIER), VK_INIT(IMAGE_MEMORY_BARRIER)};
    VkBufferImageCopy copy = {0};
    VkMemoryBarrier host = VK_INIT(MEMORY_BARRIER);
    const nj_u1 *rows;
    nj_u8 row;
    if (!m || !m->ready || !dst || !dst->pixels || dst->bpp != 4 || dst->width != m->width ||
        dst->height != m->height)
        return 0;
    r = m->r;
    if (!mirror_begin(r))
        return 0;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    for (row = 0; row < 2; ++row) {
        barriers[row].srcQueueFamilyIndex = barriers[row].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[row].image = m->image.id;
        barriers[row].subresourceRange =
            (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(r->cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barriers[0]);
    copy.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = (VkExtent3D){(uint32_t)m->width, (uint32_t)m->height, 1};
    vkCmdCopyImageToBuffer(r->cmd, m->image.id, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           m->stage.id, 1, &copy);
    vkCmdPipelineBarrier(r->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1,
                         &barriers[1]);
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(r->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                         &host, 0, NULL, 0, NULL);
    if (!mirror_submit(r))
        return 0;
    {
        double sync_start = vk_prof_now();
        if (!sync_buffer(r, &m->stage, 1))
            return 0;
        r->resident_prof_sync += vk_prof_now() - sync_start;
    }
    if ((const void *)dst->pixels != (const void *)m->stage.map) {
        double copy_start = vk_prof_now();
        nj_u8 y;
        rows = (const nj_u1 *)m->stage.map;
        if ((nj_u8)dst->stride == (nj_u8)m->width * 4u) {
            memcpy((nj_u1 *)dst->pixels, rows,
                   (nj_u8)m->width * (nj_u8)m->height * 4u);
        } else {
            for (y = 0; y < (nj_u8)m->height; ++y)
                memcpy((nj_u1 *)dst->pixels + y * (nj_u8)dst->stride,
                       rows + y * (nj_u8)m->width * 4u, (nj_u8)m->width * 4u);
        }
        r->resident_prof_copy += vk_prof_now() - copy_start;
    }
    return 1;
}
/* Present the resident mirror into a swapchain image.  The destination is
 * cleared to black first (the letterbox bars), the mirror is blitted into the
 * caller's aspect-fit rectangle and the image is left in PRESENT_SRCKHR.  The
 * acquire semaphore is waited on before the transfer, so the caller can queue
 * the present as soon as this returns. */
int nj_vk_renderer_mirror_present(nj_vk_mirror *m, VkImage dst, uint32_t dst_width,
                                  uint32_t dst_height, int x, int y, int width, int height,
                                  int filter, VkSemaphore wait) {
    nj_vk_renderer *r;
    VkImageMemoryBarrier barrier[2];
    VkImageSubresourceRange range;
    VkClearColorValue black;
    VkImageBlit blit;
    VkSubmitInfo submit = VK_INIT(SUBMIT_INFO);
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (!m || !m->ready || dst == VK_NULL_HANDLE || dst_width == 0 || dst_height == 0 ||
        width <= 0 || height <= 0 || x < 0 || y < 0 ||
        (uint32_t)(x + width) > dst_width || (uint32_t)(y + height) > dst_height)
        return 0;
    r = m->r;
    if (!mirror_begin(r))
        return 0;
    memset(&range, 0, sizeof range);
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    memset(barrier, 0, sizeof barrier);
    barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier[0].image = m->image.id;
    barrier[0].subresourceRange = range;
    barrier[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier[1].srcAccessMask = 0;
    barrier[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier[1].image = dst;
    barrier[1].subresourceRange = range;
    vkCmdPipelineBarrier(r->cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 2, barrier);
    memset(&black, 0, sizeof black);
    black.float32[3] = 1.0f;
    vkCmdClearColorImage(r->cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
    memset(&blit, 0, sizeof blit);
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[1].x = m->width;
    blit.srcOffsets[1].y = m->height;
    blit.srcOffsets[1].z = 1;
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[0].x = x;
    blit.dstOffsets[0].y = y;
    blit.dstOffsets[1].x = x + width;
    blit.dstOffsets[1].y = y + height;
    blit.dstOffsets[1].z = 1;
    vkCmdBlitImage(r->cmd, m->image.id, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   filter ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
    memset(barrier, 0, sizeof barrier);
    barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier[0].image = m->image.id;
    barrier[0].subresourceRange = range;
    barrier[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier[1].dstAccessMask = 0;
    barrier[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier[1].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier[1].image = dst;
    barrier[1].subresourceRange = range;
    vkCmdPipelineBarrier(r->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 2, barrier);
    if (vkEndCommandBuffer(r->cmd) != VK_SUCCESS)
        return 0;
    if (vkResetFences(r->dev, 1, &r->fence) != VK_SUCCESS)
        return 0;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &r->cmd;
    if (wait != VK_NULL_HANDLE) {
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &wait;
        submit.pWaitDstStageMask = &stage;
    }
    if (vkQueueSubmit(r->queue, 1, &submit, r->fence) != VK_SUCCESS ||
        vkWaitForFences(r->dev, 1, &r->fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        r->lost = 1;
        return 0;
    }
    return 1;
}

/* Export a rectangle of the mirror back into a full-frame CPU surface.  The
 * frame's other pixels are left untouched (`dst` keeps whatever it held). */
int nj_vk_renderer_mirror_export_rect(nj_vk_mirror *m, const nj_surface *dst, int x, int y,
                                      int width, int height) {
    nj_vk_renderer *r;
    VkImageMemoryBarrier barriers[2] = {VK_INIT(IMAGE_MEMORY_BARRIER), VK_INIT(IMAGE_MEMORY_BARRIER)};
    VkBufferImageCopy copy = {0};
    VkMemoryBarrier host = VK_INIT(MEMORY_BARRIER);
    const nj_u1 *rows;
    int row;
    if (!m || !m->ready || !dst || !dst->pixels || dst->bpp != 4 || dst->width != m->width ||
        dst->height != m->height)
        return 0;
    if (x == 0 && y == 0 && width == m->width && height == m->height)
        return nj_vk_renderer_mirror_export(m, dst);
    if (x < 0 || y < 0 || width <= 0 || height <= 0 || x > m->width - width || y > m->height - height)
        return 0;
    if ((const void *)dst->pixels == (const void *)m->stage.map)
        return 0;
    r = m->r;
    if (!mirror_begin(r))
        return 0;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    for (row = 0; row < 2; ++row) {
        barriers[row].srcQueueFamilyIndex = barriers[row].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[row].image = m->image.id;
        barriers[row].subresourceRange =
            (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(r->cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barriers[0]);
    copy.bufferRowLength = 0;
    copy.bufferImageHeight = 0;
    copy.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageOffset = (VkOffset3D){(int32_t)x, (int32_t)y, 0};
    copy.imageExtent = (VkExtent3D){(uint32_t)width, (uint32_t)height, 1};
    vkCmdCopyImageToBuffer(r->cmd, m->image.id, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m->stage.id, 1,
                           &copy);
    vkCmdPipelineBarrier(r->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1,
                         &barriers[1]);
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(r->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                         &host, 0, NULL, 0, NULL);
    if (!mirror_submit(r))
        return 0;
    if (!sync_buffer(r, &m->stage, 1))
        return 0;
    rows = (const nj_u1 *)m->stage.map;
    for (row = 0; row < height; ++row)
        memcpy((nj_u1 *)dst->pixels + (size_t)(y + row) * (size_t)dst->stride + (size_t)x * 4u,
               rows + (size_t)row * (size_t)width * 4u, (size_t)width * 4u);
    return 1;
}

/* One source pixel as the RGBA word the triangle shader unpacks: byte 0 is R,
 * byte 1 G, byte 2 B, byte 3 A. Surfaces store ARGB8888 as B,G,R,A bytes (the
 * packed 0xAARRGGBB order), RGB888 as R,G,B and RGB565 as little-endian. */
static nj_u4 mirror_texel_at(const nj_u1 *p, int bpp) {
    if (bpp == 2) {
        unsigned v = (unsigned)p[0] | ((unsigned)p[1] << 8);
        unsigned red = (v >> 11) & 31u, green = (v >> 5) & 63u, blue = v & 31u;
        return ((red << 3) | (red >> 2)) | (((green << 2) | (green >> 4)) << 8) |
               (((blue << 3) | (blue >> 2)) << 16) | 0xff000000u;
    }
    if (bpp == 3)
        return (nj_u4)p[0] | ((nj_u4)p[1] << 8) | ((nj_u4)p[2] << 16) | 0xff000000u;
    return (nj_u4)p[2] | ((nj_u4)p[1] << 8) | ((nj_u4)p[0] << 16) | ((nj_u4)p[3] << 24);
}

/* Draw a (sub)rectangle of a CPU surface as a textured quad on the mirror.
 * `filter` is 208 nearest or any other value for filtered sampling, `blend` is
 * 68 replace or 64 alpha blending. */
int nj_vk_renderer_mirror_texture(nj_vk_mirror *m, const nj_surface *src, int sx, int sy, int sw,
                                  int sh, int dx, int dy, int dw, int dh, int filter, int blend) {
    nj_vk_renderer *r;
    nj_gfx_triangle_cmd style;
    packet_info info;
    VkDescriptorBufferInfo desc = {0};
    VkWriteDescriptorSet write = VK_INIT(WRITE_DESCRIPTOR_SET);
    VkRenderPassBeginInfo pass = VK_INIT(RENDER_PASS_BEGIN_INFO);
    VkViewport viewport;
    VkRect2D scissor;
    VkDeviceSize zero = 0;
    material mat;
    vertex *verts;
    nj_u4 *texels;
    unsigned index;
    int x, y, bpp;
    if (!m || !m->ready || !src || !src->pixels || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
        return 0;
    if (blend != 64 && blend != 68)
        return 0;
    if (sx < 0 || sy < 0 || sx + sw > src->width || sy + sh > src->height || dx < 0 || dy < 0 ||
        dx + dw > m->width || dy + dh > m->height)
        return 0;
    bpp = src->bpp;
    if (bpp != 2 && bpp != 3 && bpp != 4)
        return 0;
    r = m->r;
    memset(&style, 0, sizeof style);
    memset(&info, 0, sizeof info);
    style.blend = blend;
    style.alpha_write = 1;
    style.texture_function = 228;
    style.filter = (unsigned)filter;
    style.wrap_s = style.wrap_t = 240;
    style.texture_no_alpha = bpp == 3;
    if (!prepare_pipeline(r, &style))
        return 0;
    index = pipeline_index(&style);
    if (!reserve_buffer(r, &r->vertices, (VkDeviceSize)6 * sizeof(vertex),
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
        !reserve_buffer(r, &r->textures, (VkDeviceSize)sw * (VkDeviceSize)sh * 4u,
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
        return 0;
    desc.buffer = r->textures.id;
    desc.range = (VkDeviceSize)sw * (VkDeviceSize)sh * 4u;
    if (desc.range > r->properties.limits.maxStorageBufferRange)
        return 0;
    if (!r->desc_valid || desc.buffer != r->desc_buffer || desc.range > r->desc_range) {
        write.dstSet = r->set;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &desc;
        vkUpdateDescriptorSets(r->dev, 1, &write, 0, NULL);
        r->desc_buffer = desc.buffer;
        r->desc_range = desc.range;
        r->desc_valid = 1;
    }
    verts = (vertex *)r->vertices.map;
    {
        const float x0 = (float)dx * 2.0f / (float)m->width - 1.0f;
        const float x1 = (float)(dx + dw) * 2.0f / (float)m->width - 1.0f;
        const float y0 = (float)dy * 2.0f / (float)m->height - 1.0f;
        const float y1 = (float)(dy + dh) * 2.0f / (float)m->height - 1.0f;
        const float u0 = (float)sx / (float)src->width;
        const float u1 = (float)(sx + sw) / (float)src->width;
        const float v0 = (float)sy / (float)src->height;
        const float v1 = (float)(sy + sh) / (float)src->height;
        int k;
        for (k = 0; k < 6; ++k) {
            int corner = (int[]){0, 1, 2, 2, 1, 3}[k];
            verts[k].position[0] = (corner & 1) ? x1 : x0;
            verts[k].position[1] = (corner & 2) ? y1 : y0;
            verts[k].position[2] = 0.0f;
            verts[k].uvq[0] = (corner & 1) ? u1 : u0;
            verts[k].uvq[1] = (corner & 2) ? v1 : v0;
            verts[k].uvq[2] = 1.0f;
            verts[k].rgba[0] = verts[k].rgba[1] = verts[k].rgba[2] = verts[k].rgba[3] = 255;
        }
    }
    texels = (nj_u4 *)r->textures.map;
    for (y = 0; y < sh; ++y) {
        const nj_u1 *row = (const nj_u1 *)src->pixels + (nj_u8)(sy + y) * (nj_u8)src->stride +
                           (nj_u8)sx * (nj_u8)bpp;
        for (x = 0; x < sw; ++x)
            texels[(nj_u8)y * (nj_u8)sw + (nj_u8)x] = mirror_texel_at(row + (nj_u8)x * (nj_u8)bpp, bpp);
    }
    memset(&mat, 0, sizeof mat);
    mat.width = src->width;
    mat.height = src->height;
    mat.function = 228;
    mat.blend = blend;
    mat.flags = (filter != 208 ? 4 : 0) | (bpp == 3 ? 8 : 0) |
                (filter == NJ_GFX_FILTER_SHARP ? 16 : 0);
    if (!sync_buffer(r, &r->vertices, 0) || !sync_buffer(r, &r->textures, 0) || !mirror_begin(r))
        return 0;
    viewport = (VkViewport){0.0f, 0.0f, (float)m->width, (float)m->height, 0.0f, 1.0f};
    scissor = (VkRect2D){{0, 0}, {(uint32_t)m->width, (uint32_t)m->height}};
    pass.renderPass = m->load_pass;
    pass.framebuffer = m->load_fb;
    pass.renderArea.extent = (VkExtent2D){(uint32_t)m->width, (uint32_t)m->height};
    vkCmdBeginRenderPass(r->cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(r->cmd, 0, 1, &viewport);
    vkCmdSetScissor(r->cmd, 0, 1, &scissor);
    vkCmdBindPipeline(r->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines[index]);
    vkCmdBindVertexBuffers(r->cmd, 0, 1, &r->vertices.id, &zero);
    vkCmdBindDescriptorSets(r->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->layout, 0, 1, &r->set, 0,
                            NULL);
    vkCmdPushConstants(r->cmd, r->layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof mat, &mat);
    vkCmdDraw(r->cmd, 6, 1, 0, 0);
    vkCmdEndRenderPass(r->cmd);
    return mirror_submit(r);
}

/* Seed the resident colour (and optional depth) from the CPU mirror before a
 * frame of 3D packets. Leaves both images in the attachment layouts the mirror
 * render passes expect, so the next call can draw without another transition. */
static int mirror_import_rect(nj_vk_mirror *m, const nj_surface *surface, nj_s4 *depth,
                              int depth_stride, int x, int y, int width, int height, int mask) {
    nj_vk_renderer *r = m->r;
    VkCommandBufferBeginInfo cb = VK_INIT(COMMAND_BUFFER_BEGIN_INFO);
    VkImageMemoryBarrier barriers[2] = {VK_INIT(IMAGE_MEMORY_BARRIER), VK_INIT(IMAGE_MEMORY_BARRIER)};
    VkBufferImageCopy copies[2] = {{0}};
    VkSubmitInfo submit = VK_INIT(SUBMIT_INFO);
    size_t colour_bytes, depth_bytes;
    int i, pack_depth, want_depth, want_colour, stages = 0;
    int full = x == 0 && y == 0 && width == m->width && height == m->height;
    if (!surface || !surface->pixels || surface->width != m->width || surface->height != m->height ||
        surface->bpp != 4)
        return 0;
    /* A fresh mirror has undefined attachments, and one that never imported
     * its depth cannot have the unimported depth trusted either.  Both cases
     * must seed the whole frame before any pass can load it. */
    if (!m->ready || ((mask & NJ_VK_MIRROR_IMPORT_DEPTH) && depth && !m->depth_ready)) {
        x = 0;
        y = 0;
        width = m->width;
        height = m->height;
        mask = NJ_VK_MIRROR_IMPORT_COLOUR | NJ_VK_MIRROR_IMPORT_DEPTH;
        full = 1;
    }
    if (x < 0 || y < 0 || width <= 0 || height <= 0 || x > m->width - width || y > m->height - height)
        return 0;
    want_colour = (mask & NJ_VK_MIRROR_IMPORT_COLOUR) != 0;
    want_depth = depth && (mask & NJ_VK_MIRROR_IMPORT_DEPTH) != 0;
    if (!want_colour && !want_depth)
        return 1;
    if (want_depth && depth_stride < x + width)
        return 0;
    /* The resident zero-copy path aliases the mirror's own staging buffer, not
     * this one, so a full frame still has to be copied here.  Only skip the
     * copy when the caller really points at this staging buffer already. */
    if ((const void *)surface->pixels == (const void *)r->stage.map && !full)
        return 0;
    pack_depth = depth ? 1 : 0;
    colour_bytes = (size_t)width * (size_t)height * 4u;
    depth_bytes = pack_depth ? colour_bytes : 0;
    if (!reserve_buffer(r, &r->stage, (VkDeviceSize)(colour_bytes + depth_bytes),
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
        return 0;
    if ((const void *)surface->pixels != (const void *)r->stage.map)
        transfer_rect_at(r, surface, depth, depth_stride, x, y, width, height, pack_depth, 0, 0,
                         colour_bytes);
    if (!sync_buffer(r, &r->stage, 0) || vkResetCommandPool(r->dev, r->pool, 0) != VK_SUCCESS)
        return 0;
    cb.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(r->cmd, &cb) != VK_SUCCESS)
        return 0;
    for (i = 0; i < 2; ++i) {
        int want = i ? want_depth : want_colour;
        if (!want)
            continue;
        barriers[stages].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[stages].oldLayout =
            i == 0 ? (m->ready ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                               : VK_IMAGE_LAYOUT_UNDEFINED)
                   : (m->depth_ready ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                     : VK_IMAGE_LAYOUT_UNDEFINED);
        barriers[stages].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[stages].srcQueueFamilyIndex = barriers[stages].dstQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED;
        barriers[stages].image = i ? m->depth.id : m->image.id;
        barriers[stages].subresourceRange = (VkImageSubresourceRange){aspects[i], 0, 1, 0, 1};
        copies[stages].bufferOffset = i ? (VkDeviceSize)colour_bytes : 0;
        copies[stages].bufferRowLength = 0;
        copies[stages].bufferImageHeight = 0;
        copies[stages].imageSubresource = (VkImageSubresourceLayers){aspects[i], 0, 0, 1};
        copies[stages].imageOffset = (VkOffset3D){(int32_t)x, (int32_t)y, 0};
        copies[stages].imageExtent = (VkExtent3D){(uint32_t)width, (uint32_t)height, 1};
        ++stages;
    }
    vkCmdPipelineBarrier(r->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, NULL, 0, NULL, (uint32_t)stages, barriers);
    for (i = 0; i < stages; ++i)
        vkCmdCopyBufferToImage(r->cmd, r->stage.id, barriers[i].image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copies[i]);
    for (i = 0; i < stages; ++i) {
        barriers[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[i].dstAccessMask =
            barriers[i].image == m->depth.id
                ? (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
                : (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        barriers[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[i].newLayout = barriers[i].image == m->depth.id
                                    ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                    : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    vkCmdPipelineBarrier(r->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         0, 0, NULL, 0, NULL, (uint32_t)stages, barriers);
    if (vkEndCommandBuffer(r->cmd) != VK_SUCCESS ||
        vkResetFences(r->dev, 1, &r->fence) != VK_SUCCESS)
        return 0;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &r->cmd;
    if (vkQueueSubmit(r->queue, 1, &submit, r->fence) != VK_SUCCESS ||
        vkWaitForFences(r->dev, 1, &r->fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        r->lost = 1;
        return 0;
    }
    m->ready = 1;
    if (want_depth)
        m->depth_ready = 1;
    return 1;
}

static int mirror_import(nj_vk_mirror *m, const nj_surface *surface, nj_s4 *depth,
                         int depth_stride, int mask) {
    return mirror_import_rect(m, surface, depth, depth_stride, 0, 0, m->width, m->height, mask);
}

int nj_vk_renderer_mirror_import_rect(nj_vk_mirror *m, const nj_surface *surface, nj_s4 *depth,
                                      int depth_stride, int x, int y, int width, int height,
                                      int mask) {
    if (!m)
        return 0;
    return mirror_import_rect(m, surface, depth, depth_stride, x, y, width, height, mask);
}

/* Clear a rectangle of the resident mirror (colour and/or depth). Useful for
 * partial clears that a full VkAttachmentLoadOp cannot express. */
int nj_vk_renderer_mirror_clear_rect(nj_vk_mirror *m, nj_u4 argb, int x, int y, int width,
                                     int height, int clear_colour, int clear_depth) {
    nj_vk_renderer *r;
    VkRenderPassBeginInfo pass = VK_INIT(RENDER_PASS_BEGIN_INFO);
    VkClearAttachment att[2];
    VkClearRect rect;
    int n = 0;
    if (!m || !(r = m->r) || r->lost || width <= 0 || height <= 0 || x < 0 || y < 0 ||
        x + width > m->width || y + height > m->height || (!clear_colour && !clear_depth))
        return 0;
    if (!m->ready) {
        /* The LOAD render pass needs defined attachment contents first. */
        if (!nj_vk_renderer_mirror_clear(m, clear_colour ? argb : 0))
            return 0;
    }
    if (!mirror_begin(r))
        return 0;
    pass.renderPass = m->load_pass;
    pass.framebuffer = m->load_fb;
    pass.renderArea.extent = (VkExtent2D){(uint32_t)m->width, (uint32_t)m->height};
    vkCmdBeginRenderPass(r->cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
    if (clear_colour) {
        att[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        att[n].colorAttachment = 0;
        att[n].clearValue.color = (VkClearColorValue){{
            (float)((argb >> 16) & 0xffu) / 255.0f, (float)((argb >> 8) & 0xffu) / 255.0f,
            (float)(argb & 0xffu) / 255.0f, (float)((argb >> 24) & 0xffu) / 255.0f}};
        ++n;
    }
    if (clear_depth) {
        att[n].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        att[n].colorAttachment = 0;
        att[n].clearValue.depthStencil = (VkClearDepthStencilValue){1.0f, 0};
        ++n;
    }
    rect.rect = (VkRect2D){{x, y}, {(uint32_t)width, (uint32_t)height}};
    rect.baseArrayLayer = 0;
    rect.layerCount = 1;
    vkCmdClearAttachments(r->cmd, (uint32_t)n, att, 1, &rect);
    vkCmdEndRenderPass(r->cmd);
    if (!mirror_submit(r))
        return 0;
    if (clear_depth)
        m->depth_ready = 1;
    return 1;
}

/* Draw a whole packet list into the resident mirror. `import` is a mask of
 * NJ_VK_MIRROR_IMPORT_* bits that seeds the mirror from the CPU colour/depth
 * buffers first (0 keeps the GPU state); `export_colour` copies the colour back
 * afterwards. Between calls the mirror is authoritative. All packets must share
 * one surface/depth whose dimensions match the mirror (the HD mirror size). */
static int mirror_triangles_common(nj_vk_mirror *m, const nj_gfx_triangles_list *l, int import,
                                   int export_colour, nj_u4 *submitted, int checked) {
    nj_vk_renderer *r;
    packet_info *infos;
    VkDescriptorBufferInfo desc = {0};
    VkWriteDescriptorSet write = VK_INIT(WRITE_DESCRIPTOR_SET);
    VkCommandBufferBeginInfo cb = VK_INIT(COMMAND_BUFFER_BEGIN_INFO);
    VkRenderPassBeginInfo pass = VK_INIT(RENDER_PASS_BEGIN_INFO);
    VkSubmitInfo submit = VK_INIT(SUBMIT_INFO);
    VkViewport vp;
    VkRect2D scissor;
    VkDeviceSize zero = 0;
    nj_u4 i, one = 0, total = 0, vertices = 0, texels = 0, vertex_base = 0;
    if (!m || !m->r || !l || !submitted)
        return 0;
    r = m->r;
    if (r->lost || (checked && !nj_gfx_triangles_list_validate(l, &one)) || !l->surface ||
        l->surface->width != m->width || l->surface->height != m->height || l->surface->bpp != 4)
        return 0;
    infos = (packet_info *)r->session_infos;
    if (r->session_infos_cap < l->count) {
        packet_info *grown =
            (packet_info *)realloc(infos, (size_t)l->count * sizeof(*infos));
        if (!grown)
            return 0;
        r->session_infos = infos = grown;
        r->session_infos_cap = l->count;
    }
    for (i = 0; i < l->count; ++i) {
        const nj_gfx_triangles *p = l->packets[i];
        nj_u4 j;
        memset(&infos[i], 0, sizeof infos[i]);
        if (!p || !analyze(r, p, &infos[i]))
            return 0;
        for (j = 0; j < (nj_u4)infos[i].count; ++j)
            infos[i].views[j].offset += (int)texels;
        texels += (nj_u4)infos[i].texels;
        vertices += p->count * 3u;
        total += p->count;
    }
    if (!reserve_buffer(r, &r->vertices, (VkDeviceSize)vertices * sizeof(vertex),
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
        !reserve_buffer(r, &r->textures, texels ? (VkDeviceSize)texels * 4u : 4u,
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
        return 0;
    desc.buffer = r->textures.id;
    desc.range = texels ? (VkDeviceSize)texels * 4u : 4u;
    if (desc.range > r->properties.limits.maxStorageBufferRange)
        return 0;
    if (!r->desc_valid || desc.buffer != r->desc_buffer || desc.range > r->desc_range) {
        write.dstSet = r->set;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &desc;
        vkUpdateDescriptorSets(r->dev, 1, &write, 0, NULL);
        r->desc_buffer = desc.buffer;
        r->desc_range = desc.range;
        r->desc_valid = 1;
    }
    for (i = 0; i < l->count; ++i) {
        upload_geometry_at(r, l->packets[i], &infos[i], (vertex *)r->vertices.map + vertex_base);
        vertex_base += l->packets[i]->count * 3u;
    }
    if (!sync_buffer(r, &r->vertices, 0) || !sync_buffer(r, &r->textures, 0))
        return 0;
    /* A fresh mirror has no defined contents: import the CPU frame (or clear)
       before the first pass may load the attachments. */
    if (!m->ready) {
        int mask = import | NJ_VK_MIRROR_IMPORT_COLOUR;
        if (l->depth)
            mask |= NJ_VK_MIRROR_IMPORT_DEPTH;
        if (!mirror_import(m, l->surface, l->depth, l->depth_stride, mask))
            return 0;
    } else if (import && !mirror_import(m, l->surface, l->depth, l->depth_stride, import)) {
        return 0;
    }
    if (!mirror_begin(r))
        return 0;
    cb.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    (void)cb;
    pass.renderPass = m->load_pass;
    pass.framebuffer = m->load_fb;
    pass.renderArea.extent = (VkExtent2D){(uint32_t)m->width, (uint32_t)m->height};
    vkCmdBeginRenderPass(r->cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindVertexBuffers(r->cmd, 0, 1, &r->vertices.id, &zero);
    vkCmdBindDescriptorSets(r->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->layout, 0, 1, &r->set, 0,
                            NULL);
    vertex_base = 0;
    for (i = 0; i < l->count; ++i) {
        const nj_gfx_triangles *p = l->packets[i];
        vp = (VkViewport){(float)p->x, (float)p->y, (float)p->width, (float)p->height, 0.0f, 1.0f};
        scissor = (VkRect2D){{p->x, p->y}, {(uint32_t)p->width, (uint32_t)p->height}};
        vkCmdSetViewport(r->cmd, 0, 1, &vp);
        vkCmdSetScissor(r->cmd, 0, 1, &scissor);
        draws_body(r, p, &infos[i], vertex_base);
        vertex_base += p->count * 3u;
    }
    vkCmdEndRenderPass(r->cmd);
    if (!mirror_submit(r))
        return 0;
    if (export_colour && !nj_vk_renderer_mirror_export(m, l->surface))
        return 0;
    *submitted = total;
    return NJ_GFX_ACCEL_DONE;
}

int nj_vk_renderer_mirror_triangles(nj_vk_mirror *m, const nj_gfx_triangles_list *l, int import,
                                    int export_colour, nj_u4 *submitted) {
    return mirror_triangles_common(m, l, import, export_colour, submitted, 1);
}

static int mirror_triangles_trusted(nj_vk_mirror *m, const nj_gfx_triangles_list *l, int import,
                                    int export_colour, nj_u4 *submitted) {
    return mirror_triangles_common(m, l, import, export_colour, submitted, 0);
}
