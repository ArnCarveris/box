/**
 * @file renderer/effect_mip.c
 * @brief Mip chain setup pass that a post process pass can run before it draws.
 *
 * The setup shader fills the finest level of a per view pyramid from the source
 * image, downsamples it level by level and adds every coarser level back into the
 * finer one, optionally blending the result with the pyramid of the previous frame.
 *
 * A storage format pyramid is filled by one compute pass, a dispatch per level, whose
 * upsample writes a second chain since a dispatch cannot read what it stores. Other
 * formats take a render pass per level that blends the upsample into the level.
 */

#include "renderer_internal.h"

/** Bytes between the blend weights of two levels, the uniform offset alignment */
#define BOX_EFFECT_MIP_WEIGHT_STRIDE (256)

/** Blend state of the upsample pass, which adds the coarser level at a constant weight */
static const WGPUBlendState box_effect_mip_mix = {
    .color = { WGPUBlendOperation_Add, WGPUBlendFactor_Constant,
        WGPUBlendFactor_One },
    .alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_Zero, WGPUBlendFactor_One }
};

/** Entry points of the setup shader, in the order of the pipeline arrays */
static const char *box_effect_mip_entries[2][4] = {
    { "fs_first", "fs_down", "fs_up", "fs_history" },
    { "cs_first", "cs_down", "cs_up", "cs_history" }
};

/** Whether a pyramid format can be written by the compute passes.
 *
 * @param format Format of the pyramid
 * @return Whether the format is a storage format
 */
static bool box_effect_mip_storage(
    WGPUTextureFormat format)
{
    return format == WGPUTextureFormat_RGBA16Float;
}

/** Edge of a level of the pyramid, which halves every level and never drops below one.
 *
 * @param size Edge of the finest level in pixels
 * @param level The level
 * @return The edge of the level in pixels
 */
static uint32_t box_effect_mip_extent(
    uint32_t size,
    uint32_t level)
{
    uint32_t result = size >> level;
    if (!result) {
        result = 1;
    }

    return result;
}

/** Create a fullscreen pipeline from a fragment entry point of a setup shader.
 *
 * @param r The renderer
 * @param module The shader module
 * @param layout The pipeline layout
 * @param entry Name of the fragment entry point
 * @param blend Blend state of the color target, NULL for no blending
 * @param format Format of the pyramid the pipeline writes
 * @return The pipeline
 */
static WGPURenderPipeline box_effect_mip_pipeline(
    box_renderer_t *r,
    WGPUShaderModule module,
    WGPUPipelineLayout layout,
    const char *entry,
    const WGPUBlendState *blend,
    WGPUTextureFormat format)
{
    WGPUColorTargetState target = {
        .format = format,
        .blend = blend,
        .writeMask = WGPUColorWriteMask_All
    };
    WGPUFragmentState fragment = {
        .module = module,
        .entryPoint = { .data = entry, .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = layout,
        .vertex = {
            .module = module,
            .entryPoint = { .data = "vs_main", .length = WGPU_STRLEN }
        },
        .fragment = &fragment,
        .primitive = { .topology = WGPUPrimitiveTopology_TriangleList },
        .multisample = { .count = 1, .mask = 0xFFFFFFFF }
    };

    return wgpuDeviceCreateRenderPipeline(r->device, &desc);
}

/** Create a compute pipeline from an entry point of a setup shader.
 *
 * @param r The renderer
 * @param module The shader module
 * @param layout The pipeline layout
 * @param entry Name of the compute entry point
 * @return The pipeline
 */
static WGPUComputePipeline box_effect_mip_compute_pipeline(
    box_renderer_t *r,
    WGPUShaderModule module,
    WGPUPipelineLayout layout,
    const char *entry)
{
    WGPUComputePipelineDescriptor desc = {
        .label = BOX_GPU_LABEL("setup mip"),
        .layout = layout,
        .compute = {
            .module = module,
            .entryPoint = { .data = entry, .length = WGPU_STRLEN }
        }
    };

    return wgpuDeviceCreateComputePipeline(r->device, &desc);
}

void box_effect_mip_init(
    box_renderer_t *r)
{
    WGPUBindGroupLayoutEntry entries[6] = {
        {
            .binding = 0,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_effect_uniform_t)
            }
        }, {
            .binding = 1,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 2,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }, {
            .binding = 3,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 4,
            .storageTexture = {
                .access = WGPUStorageTextureAccess_WriteOnly,
                .format = WGPUTextureFormat_RGBA16Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 5,
            .buffer = { .type = WGPUBufferBindingType_Uniform, .minBindingSize = 16 }
        }
    };

    uint32_t i;
    for (i = 0; i < 6; i ++) {
        entries[i].visibility = WGPUShaderStage_Fragment;
    }

    WGPUBindGroupLayoutDescriptor layout_desc = { .entryCount = 4, .entries = entries };
    r->setup_mip_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);

    for (i = 0; i < 6; i ++) {
        entries[i].visibility = WGPUShaderStage_Compute;
    }

    layout_desc.entryCount = 6;
    r->setup_mip_compute_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);
}

void box_effect_mip_release(
    box_renderer_t *r)
{
    uint32_t i;
    for (i = 0; i < 4; i ++) {
        if (r->setup_mip_pipeline[i]) wgpuRenderPipelineRelease(r->setup_mip_pipeline[i]);
        if (r->setup_mip_compute[i]) wgpuComputePipelineRelease(r->setup_mip_compute[i]);
        r->setup_mip_pipeline[i] = NULL;
        r->setup_mip_compute[i] = NULL;
    }

    r->setup_mip_shader = BOX_SHADER_NONE;
}

void box_effect_mip_fini(
    box_renderer_t *r)
{
    box_effect_mip_release(r);
    if (r->setup_mip_layout) wgpuBindGroupLayoutRelease(r->setup_mip_layout);
    if (r->setup_mip_compute_layout) wgpuBindGroupLayoutRelease(r->setup_mip_compute_layout);
    r->setup_mip_layout = NULL;
    r->setup_mip_compute_layout = NULL;
}

/** Build the four pipelines of the setup shader of a pass, unless they already exist.
 *
 * @param r The renderer
 * @param fx The pass whose setup shader and format the pipelines are built from
 * @param compute Build the compute pipelines instead of the render pipelines
 * @return Whether every pipeline exists
 */
static bool box_effect_mip_ready(
    box_renderer_t *r,
    const box_effect_t *fx,
    bool compute)
{
    bool result = true;
    if (r->setup_mip_shader != fx->setup_shader) {
        box_effect_mip_release(r);
        r->setup_mip_shader = fx->setup_shader;
    }

    if (compute ? r->setup_mip_compute[0] != NULL : r->setup_mip_pipeline[0] != NULL) {
        goto done;
    }

    WGPUShaderModule module = box_shader_module(r, fx->setup_shader);
    if (!module) {
        result = false;
        goto done;
    }

    WGPUPipelineLayoutDescriptor desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = compute ? &r->setup_mip_compute_layout : &r->setup_mip_layout
    };
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(r->device, &desc);
    WGPUTextureFormat format = fx->setup_format == BOX_EFFECT_TEXTURE_RG16 ?
        WGPUTextureFormat_RG16Float : WGPUTextureFormat_RGBA16Float;
    uint32_t i;
    for (i = 0; i < 4; i ++) {
        const char *entry = box_effect_mip_entries[compute][i];
        if (compute) {
            r->setup_mip_compute[i] = box_effect_mip_compute_pipeline(r, module, layout, entry);
        } else {
            r->setup_mip_pipeline[i] = box_effect_mip_pipeline(r, module, layout, entry,
                i == 2 ? &box_effect_mip_mix : NULL, format);
        }

        result = result && (compute ? r->setup_mip_compute[i] != NULL :
            r->setup_mip_pipeline[i] != NULL);
    }

    wgpuPipelineLayoutRelease(layout);
    wgpuShaderModuleRelease(module);
done:
    return result;
}

/** Release the history textures of the pyramid of a view and forget the history.
 *
 * @param v The view
 */
static void box_effect_mip_history_fini(
    box_gpu_view_t *v)
{
    for (uint32_t i = 0; i < 2; i ++) {
        if (v->setup_mip_history_view[i]) wgpuTextureViewRelease(v->setup_mip_history_view[i]);
        if (v->setup_mip_history[i]) wgpuTextureRelease(v->setup_mip_history[i]);
        v->setup_mip_history_view[i] = NULL;
        v->setup_mip_history[i] = NULL;
    }

    v->setup_mip_history_valid = false;
    v->setup_mip_history_index = 0;
}

void box_effect_mip_view_fini(
    box_gpu_view_t *v)
{
    box_effect_mip_history_fini(v);
    uint32_t level;
    for (level = 0; level < BOX_EFFECT_MIP_LEVELS_MAX; level ++) {
        if (v->setup_mip_mips[level]) wgpuTextureViewRelease(v->setup_mip_mips[level]);
        if (v->setup_mip_sum_mips[level]) wgpuTextureViewRelease(v->setup_mip_sum_mips[level]);
        if (v->setup_mip_down[level]) wgpuBindGroupRelease(v->setup_mip_down[level]);
        if (v->setup_mip_up[level]) wgpuBindGroupRelease(v->setup_mip_up[level]);
        v->setup_mip_mips[level] = NULL;
        v->setup_mip_sum_mips[level] = NULL;
        v->setup_mip_down[level] = NULL;
        v->setup_mip_up[level] = NULL;
    }

    if (v->setup_mip_view) wgpuTextureViewRelease(v->setup_mip_view);
    if (v->setup_mip) wgpuTextureRelease(v->setup_mip);
    if (v->setup_mip_sum) wgpuTextureRelease(v->setup_mip_sum);
    if (v->setup_mip_weights) wgpuBufferRelease(v->setup_mip_weights);
    v->setup_mip_view = NULL;
    v->setup_mip = NULL;
    v->setup_mip_sum = NULL;
    v->setup_mip_weights = NULL;
    v->setup_mip_uniform = NULL;
    v->setup_mip_levels = 0;
    v->setup_mip_width = 0;
    v->setup_mip_height = 0;
}

/** Create a bind group of a pyramid pass or dispatch.
 *
 * @param r The renderer
 * @param source The image the pass samples
 * @param history The image the pass blends with or adds to, NULL binds the source again
 * @param target The level a dispatch writes, NULL for a render pass
 * @param level Level whose blend weight a dispatch reads
 * @return The bind group
 */
static WGPUBindGroup box_effect_mip_group(
    box_renderer_t *r,
    WGPUTextureView source,
    WGPUTextureView history,
    WGPUTextureView target,
    uint32_t level)
{
    WGPUBindGroupEntry entries[6] = {
        { .binding = 0, .buffer = r->effect_uniform, .size = sizeof(box_effect_uniform_t) },
        { .binding = 1, .textureView = source },
        { .binding = 2, .sampler = r->effect_sampler },
        { .binding = 3, .textureView = history ? history : source },
        { .binding = 4, .textureView = target },
        {
            .binding = 5,
            .buffer = r->v->setup_mip_weights,
            .offset = (uint64_t)level * BOX_EFFECT_MIP_WEIGHT_STRIDE,
            .size = 16
        }
    };
    WGPUBindGroupDescriptor desc = {
        .layout = target ? r->setup_mip_compute_layout : r->setup_mip_layout,
        .entryCount = target ? 6 : 4,
        .entries = entries
    };

    return wgpuDeviceCreateBindGroup(r->device, &desc);
}

/** Create a view of one level of a pyramid texture.
 *
 * @param texture The texture
 * @param format Format of the texture
 * @param level The level
 * @return The view
 */
static WGPUTextureView box_effect_mip_level_view(
    WGPUTexture texture,
    WGPUTextureFormat format,
    uint32_t level)
{
    WGPUTextureViewDescriptor view_desc = {
        .format = format,
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = level,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    };

    return wgpuTextureCreateView(texture, &view_desc);
}

/** Create the chain the upsample dispatches write, the weight buffer and the dispatch groups.
 *
 * @param r The renderer
 */
static void box_effect_mip_compute_resize(
    box_renderer_t *r)
{
    box_gpu_view_t *v = r->v;
    uint32_t levels = v->setup_mip_levels;
    WGPUBufferDescriptor weights_desc = {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)levels * BOX_EFFECT_MIP_WEIGHT_STRIDE
    };
    v->setup_mip_weights = wgpuDeviceCreateBuffer(r->device, &weights_desc);
    if (levels > 1) {
        WGPUTextureDescriptor desc = {
            .usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding,
            .dimension = WGPUTextureDimension_2D,
            .size = { v->setup_mip_width, v->setup_mip_height, 1 },
            .format = v->setup_mip_format,
            .mipLevelCount = levels - 1,
            .sampleCount = 1
        };
        v->setup_mip_sum = wgpuDeviceCreateTexture(r->device, &desc);
    }

    uint32_t level;
    for (level = 0; level + 1 < levels; level ++) {
        v->setup_mip_sum_mips[level] =
            box_effect_mip_level_view(v->setup_mip_sum, v->setup_mip_format, level);
    }

    for (level = 1; level < levels; level ++) {
        v->setup_mip_down[level] = box_effect_mip_group(r, v->setup_mip_mips[level - 1], NULL,
            v->setup_mip_mips[level], 0);
        WGPUTextureView coarser = level + 1 == levels ?
            v->setup_mip_mips[level] : v->setup_mip_sum_mips[level];
        v->setup_mip_up[level - 1] = box_effect_mip_group(r, coarser,
            v->setup_mip_mips[level - 1], v->setup_mip_sum_mips[level - 1], level);
    }
}

/** Recreate the pyramid of the current view, its level views and its bind groups.
 *
 * @param r The renderer
 * @param width Width of the finest level in pixels
 * @param height Height of the finest level in pixels
 * @param levels Levels of the pyramid
 * @param format Format of the pyramid
 */
static void box_effect_mip_resize(
    box_renderer_t *r,
    uint32_t width,
    uint32_t height,
    uint32_t levels,
    WGPUTextureFormat format)
{
    box_effect_mip_view_fini(r->v);
    bool compute = box_effect_mip_storage(format);
    r->v->setup_mip_width = width;
    r->v->setup_mip_height = height;
    r->v->setup_mip_levels = levels;
    r->v->setup_mip_format = format;
    r->v->setup_mip_uniform = r->effect_uniform;

    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding |
            (compute ? WGPUTextureUsage_StorageBinding : WGPUTextureUsage_RenderAttachment),
        .dimension = WGPUTextureDimension_2D,
        .size = { r->v->setup_mip_width, r->v->setup_mip_height, 1 },
        .format = format,
        .mipLevelCount = levels,
        .sampleCount = 1
    };
    r->v->setup_mip = wgpuDeviceCreateTexture(r->device, &desc);

    uint32_t level;
    for (level = 0; level < levels; level ++) {
        r->v->setup_mip_mips[level] = box_effect_mip_level_view(r->v->setup_mip, format, level);
    }

    if (compute) {
        box_effect_mip_compute_resize(r);
    } else {
        for (level = 1; level < levels; level ++) {
            r->v->setup_mip_down[level] =
                box_effect_mip_group(r, r->v->setup_mip_mips[level - 1], NULL, NULL, 0);
            r->v->setup_mip_up[level - 1] =
                box_effect_mip_group(r, r->v->setup_mip_mips[level], NULL, NULL, 0);
        }
    }

    WGPUTexture result = r->v->setup_mip_sum ? r->v->setup_mip_sum : r->v->setup_mip;
    r->v->setup_mip_view = box_effect_mip_level_view(result, format, 0);
}

/** Record one fullscreen pass of the pyramid.
 *
 * @param r The renderer
 * @param encoder The command encoder
 * @param pipeline The pipeline of the pass
 * @param group The bind group of the pass
 * @param target The level view the pass writes
 * @param slot Index of the pass in the packet, which is its uniform slot
 * @param mix The pass blends into the target instead of clearing it
 * @param blend Weight of the blend constant when the pass mixes
 */
static void box_effect_mip_pass(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    WGPURenderPipeline pipeline,
    WGPUBindGroup group,
    WGPUTextureView target,
    uint32_t slot,
    bool mix,
    float blend)
{
    WGPURenderPassColorAttachment color = {
        .view = target,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = mix ? WGPULoadOp_Load : WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store
    };
    WGPURenderPassDescriptor desc = { .colorAttachmentCount = 1, .colorAttachments = &color };
    WGPURenderPassEncoder pass = box_timing_render(r, encoder, &desc, "setup_mip");
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    if (mix) {
        WGPUColor weight = { blend, blend, blend, blend };
        wgpuRenderPassEncoderSetBlendConstant(pass, &weight);
    }

    uint32_t offset = slot * BOX_EFFECT_UNIFORM_STRIDE;
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &offset);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

/** Record the render passes of a pyramid in a format that cannot be a storage texture.
 *
 * @param r The renderer
 * @param encoder The command encoder
 * @param first The bind group of the pass that reads the source image
 * @param blend The bind group of the history blend, NULL for none
 * @param history The history texture the blend writes
 * @param slot Index of the pass in the packet, which is its uniform slot
 * @param weight Weight of the chain
 * @param curve Low frequency boost, its curvature and the high pass frequency, NULL for none
 */
static void box_effect_mip_passes(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    WGPUBindGroup first,
    WGPUBindGroup blend,
    WGPUTextureView history,
    uint32_t slot,
    float weight,
    const float *curve)
{
    uint32_t levels = r->v->setup_mip_levels;
    box_effect_mip_pass(r, encoder, r->setup_mip_pipeline[0], first, r->v->setup_mip_mips[0],
        slot, false, 0);
    for (uint32_t level = 1; level < levels; level ++) {
        box_effect_mip_pass(r, encoder, r->setup_mip_pipeline[1], r->v->setup_mip_down[level],
            r->v->setup_mip_mips[level], slot, false, 0);
    }

    for (uint32_t level = levels - 1; level > 0; level --) {
        box_effect_mip_pass(r, encoder, r->setup_mip_pipeline[2], r->v->setup_mip_up[level - 1],
            r->v->setup_mip_mips[level - 1], slot, true,
            box_effect_mip_blend(weight, curve, level, levels));
    }

    if (blend) {
        box_effect_mip_pass(r, encoder, r->setup_mip_pipeline[3], blend, history, slot,
            false, 0);
    }
}

/** Record the dispatch of the compute pass of a pyramid that fills one level.
 *
 * @param r The renderer
 * @param pass The compute pass
 * @param pipeline The pipeline of the dispatch
 * @param group The bind group of the dispatch
 * @param slot Index of the pass in the packet, which is its uniform slot
 * @param level The level whose size the dispatch covers
 */
static void box_effect_mip_dispatch(
    box_renderer_t *r,
    WGPUComputePassEncoder pass,
    WGPUComputePipeline pipeline,
    WGPUBindGroup group,
    uint32_t slot,
    uint32_t level)
{
    uint32_t width = box_effect_mip_extent(r->v->setup_mip_width, level);
    uint32_t height = box_effect_mip_extent(r->v->setup_mip_height, level);
    uint32_t offset = slot * BOX_EFFECT_UNIFORM_STRIDE;
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 1, &offset);
    wgpuComputePassEncoderDispatchWorkgroups(pass, (width + BOX_MIP_GROUP - 1) / BOX_MIP_GROUP,
        (height + BOX_MIP_GROUP - 1) / BOX_MIP_GROUP, 1);
}

/** Record the compute pass of a pyramid in a storage format, a dispatch per level.
 *
 * @param r The renderer
 * @param encoder The command encoder
 * @param first The bind group of the dispatch that reads the source image
 * @param blend The bind group of the history blend, NULL for none
 * @param slot Index of the pass in the packet, which is its uniform slot
 * @param weight Weight of the chain
 * @param curve Low frequency boost, its curvature and the high pass frequency, NULL for none
 */
static void box_effect_mip_dispatches(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    WGPUBindGroup first,
    WGPUBindGroup blend,
    uint32_t slot,
    float weight,
    const float *curve)
{
    uint32_t levels = r->v->setup_mip_levels;
    float weights[BOX_EFFECT_MIP_LEVELS_MAX][BOX_EFFECT_MIP_WEIGHT_STRIDE / sizeof(float)] = {0};
    for (uint32_t level = 1; level < levels; level ++) {
        weights[level][0] = box_effect_mip_blend(weight, curve, level, levels);
    }

    wgpuQueueWriteBuffer(r->gpu_queue, r->v->setup_mip_weights, 0, weights,
        (size_t)levels * BOX_EFFECT_MIP_WEIGHT_STRIDE);

    WGPUComputePassEncoder pass = box_timing_compute(r, encoder, "setup_mip");
    box_effect_mip_dispatch(r, pass, r->setup_mip_compute[0], first, slot, 0);
    for (uint32_t level = 1; level < levels; level ++) {
        box_effect_mip_dispatch(r, pass, r->setup_mip_compute[1], r->v->setup_mip_down[level],
            slot, level);
    }

    for (uint32_t level = levels - 1; level > 0; level --) {
        box_effect_mip_dispatch(r, pass, r->setup_mip_compute[2], r->v->setup_mip_up[level - 1],
            slot, level - 1);
    }

    if (blend) {
        box_effect_mip_dispatch(r, pass, r->setup_mip_compute[3], blend, slot, 0);
    }

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
}

/** Pick the history texture this frame writes, creating both on first use.
 *
 * @param r The renderer
 * @param slot Index of the pass in the packet, which is its uniform slot
 * @param frame Frame number, the history only counts when it holds the previous frame
 * @param out_previous Receives the image the blend reads as the previous frame
 * @return The view of the history texture this frame writes
 */
static WGPUTextureView box_effect_mip_history(
    box_renderer_t *r,
    uint32_t slot,
    uint32_t frame,
    WGPUTextureView *out_previous)
{
    box_gpu_view_t *v = r->v;
    if (!v->setup_mip_history[0]) {
        WGPUTextureDescriptor desc = {
            .usage = WGPUTextureUsage_TextureBinding |
                (box_effect_mip_storage(v->setup_mip_format) ?
                    WGPUTextureUsage_StorageBinding : WGPUTextureUsage_RenderAttachment),
            .dimension = WGPUTextureDimension_2D,
            .size = { v->setup_mip_width, v->setup_mip_height, 1 },
            .format = v->setup_mip_format,
            .mipLevelCount = 1,
            .sampleCount = 1
        };
        for (uint32_t i = 0; i < 2; i ++) {
            v->setup_mip_history[i] = wgpuDeviceCreateTexture(r->device, &desc);
            v->setup_mip_history_view[i] = wgpuTextureCreateView(v->setup_mip_history[i], NULL);
        }
    }

    bool valid = v->setup_mip_history_valid && v->setup_mip_history_slot == slot &&
        v->setup_mip_history_frame + 1 == frame;
    uint32_t next = 1 - v->setup_mip_history_index;
    *out_previous = valid ? v->setup_mip_history_view[v->setup_mip_history_index] :
        v->setup_mip_view;
    v->setup_mip_history_index = next;
    v->setup_mip_history_frame = frame;
    v->setup_mip_history_slot = slot;
    v->setup_mip_history_valid = true;

    return v->setup_mip_history_view[next];
}

WGPUTextureView box_effect_mip_run(
    box_renderer_t *r,
    const box_effect_t *fx,
    uint32_t slot,
    uint32_t frame,
    box_gpu_pool_t *src,
    WGPUCommandEncoder encoder)
{
    WGPUTextureView result = NULL;
    WGPUTextureFormat format = fx->setup_format == BOX_EFFECT_TEXTURE_RG16 ?
        WGPUTextureFormat_RG16Float : WGPUTextureFormat_RGBA16Float;
    bool compute = box_effect_mip_storage(format);
    if (!src || !box_effect_mip_ready(r, fx, compute)) {
        goto done;
    }

    int32_t weight_param = fx->setup_parameter[0];
    int32_t size_param = fx->setup_parameter[1];
    int32_t history_param = fx->setup_parameter[2];
    int32_t option_param = fx->setup_parameter[3];
    int32_t curve_param = fx->setup_parameter[4];
    const float *curve = curve_param >= 0 && curve_param + 2 < BOX_EFFECT_PARAM_MAX ?
        &fx->params[curve_param] : NULL;
    float weight = weight_param >= 0 && weight_param < BOX_EFFECT_PARAM_MAX ?
        fx->params[weight_param] : 1.0f;
    if (weight <= 0) {
        box_effect_mip_history_fini(r->v);
        goto done;
    }

    uint32_t width, height;
    float max_dimension = size_param >= 0 && size_param < BOX_EFFECT_PARAM_MAX ?
        fx->params[size_param] : 65536.0f;
    bool full_size = option_param >= 0 && option_param < BOX_EFFECT_PARAM_MAX &&
        fx->params[option_param] > 0.5f;
    box_effect_mip_size(src->width, src->height, max_dimension, full_size,
        &width, &height);
    uint32_t levels = box_effect_mip_count(width, height);
    if (r->v->setup_mip_levels != levels || r->v->setup_mip_width != width ||
        r->v->setup_mip_height != height || r->v->setup_mip_format != format ||
        r->v->setup_mip_uniform != r->effect_uniform)
    {
        box_effect_mip_resize(r, width, height, levels, format);
    }

    float first_step[2] = { 0.5f / (float)width, 0.5f / (float)height };
    wgpuQueueWriteBuffer(r->gpu_queue, r->effect_uniform,
        (uint64_t)slot * BOX_EFFECT_UNIFORM_STRIDE + 12 * sizeof(float),
        first_step, sizeof(first_step));

    WGPUTextureView history = NULL;
    WGPUTextureView previous = NULL;
    if (history_param >= 0 && history_param < BOX_EFFECT_PARAM_MAX &&
        fx->params[history_param] > 0)
    {
        history = box_effect_mip_history(r, slot, frame, &previous);
    } else {
        box_effect_mip_history_fini(r->v);
    }

    WGPUBindGroup first = box_effect_mip_group(r, src->view, NULL,
        compute ? r->v->setup_mip_mips[0] : NULL, 0);
    WGPUBindGroup blend = history ? box_effect_mip_group(r, r->v->setup_mip_view, previous,
        compute ? history : NULL, 0) : NULL;
    if (compute) {
        box_effect_mip_dispatches(r, encoder, first, blend, slot, weight, curve);
    } else {
        box_effect_mip_passes(r, encoder, first, blend, history, slot, weight, curve);
    }

    wgpuBindGroupRelease(first);
    if (blend) {
        wgpuBindGroupRelease(blend);
    }

    result = history ? history : r->v->setup_mip_view;
done:
    return result;
}
