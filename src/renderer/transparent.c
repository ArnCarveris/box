/**
 * @file renderer/transparent.c
 * @brief Sorted pass: blend pipelines, the copy of the opaque target and its blur.
 *
 * The copy is blurred by a pyramid: a thirteen tap downsample down the levels
 * and an eight tap tent upsample mixed back up them, which translucent draws
 * read at the level their roughness asks for.
 *
 * A float copy is blurred by one compute pass, a dispatch per level, whose downsample
 * writes a scratch chain the upsample mixes from since a dispatch cannot read what it
 * stores. A copy in the swap chain format takes a render pass per level.
 */

#include "renderer_internal.h"
#include <stddef.h>

/** Blend state per blend mode, the opaque entry is never used */
static const WGPUBlendState box_blend_states[BOX_BLEND_COUNT] = {
    {
        .color = { WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_Zero },
        .alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_Zero }
    }, {
        .color = { WGPUBlendOperation_Add, WGPUBlendFactor_One,
            WGPUBlendFactor_OneMinusSrcAlpha },
        .alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_One,
            WGPUBlendFactor_OneMinusSrcAlpha }
    }, {
        .color = { WGPUBlendOperation_Add, WGPUBlendFactor_One,
            WGPUBlendFactor_OneMinusSrcAlpha },
        .alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_One,
            WGPUBlendFactor_OneMinusSrcAlpha }
    }, {
        .color = { WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_One },
        .alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One }
    }, {
        .color = { WGPUBlendOperation_Add, WGPUBlendFactor_Dst, WGPUBlendFactor_Zero },
        .alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_DstAlpha, WGPUBlendFactor_Zero }
    }, {
        .color = { WGPUBlendOperation_Add, WGPUBlendFactor_OneMinusDst, WGPUBlendFactor_One },
        .alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_OneMinusDstAlpha,
            WGPUBlendFactor_One }
    }, {
        .color = { WGPUBlendOperation_ReverseSubtract, WGPUBlendFactor_SrcAlpha,
            WGPUBlendFactor_One },
        .alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_Zero, WGPUBlendFactor_One }
    }, {
        .color = { WGPUBlendOperation_Min, WGPUBlendFactor_One, WGPUBlendFactor_One },
        .alpha = { WGPUBlendOperation_Min, WGPUBlendFactor_One, WGPUBlendFactor_One }
    }, {
        .color = { WGPUBlendOperation_Max, WGPUBlendFactor_One, WGPUBlendFactor_One },
        .alpha = { WGPUBlendOperation_Max, WGPUBlendFactor_One, WGPUBlendFactor_One }
    }
};

/** Blend of an upsample pass: half the tent of the level below, half what is there */
static const WGPUBlendState box_blur_mix = {
    .color = { WGPUBlendOperation_Add, WGPUBlendFactor_Constant,
        WGPUBlendFactor_OneMinusConstant },
    .alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_Zero }
};

void box_transparent_view_fini(
    box_gpu_view_t *v)
{
    uint32_t mip;
    for (mip = 0; mip < v->scene_mips_count; mip ++) {
        if (v->scene_mips[mip]) wgpuTextureViewRelease(v->scene_mips[mip]);
        if (v->scene_down[mip]) wgpuBindGroupRelease(v->scene_down[mip]);
        if (v->scene_up[mip]) wgpuBindGroupRelease(v->scene_up[mip]);
        v->scene_mips[mip] = NULL;
        v->scene_down[mip] = NULL;
        v->scene_up[mip] = NULL;
    }

    for (mip = 0; mip < BOX_BLUR_LEVELS_MAX; mip ++) {
        if (v->scene_scratch_mips[mip]) wgpuTextureViewRelease(v->scene_scratch_mips[mip]);
        v->scene_scratch_mips[mip] = NULL;
    }

    if (v->scene_scratch) wgpuTextureRelease(v->scene_scratch);
    if (v->scene_view) wgpuTextureViewRelease(v->scene_view);
    if (v->scene) wgpuTextureRelease(v->scene);
    v->scene_scratch = NULL;
    v->scene_view = NULL;
    v->scene = NULL;
    v->scene_mips_count = 0;
}

/** Create the bind group a blur pass or dispatch reads one level of the chain through.
 *
 * @param r The renderer
 * @param source The level the pass samples
 * @param base The level a dispatch mixes into what it writes, NULL for a render pass
 * @param target The level a dispatch writes, NULL for a render pass
 * @return The bind group
 */
static WGPUBindGroup box_transparent_level_group(
    box_renderer_t *r,
    WGPUTextureView source,
    WGPUTextureView base,
    WGPUTextureView target)
{
    WGPUBindGroupEntry entries[4] = {
        { .binding = 0, .textureView = source },
        { .binding = 1, .sampler = r->scene_sampler },
        { .binding = 2, .textureView = base },
        { .binding = 3, .textureView = target }
    };
    WGPUBindGroupDescriptor desc = {
        .layout = target ? r->blur_compute_layout : r->blur_layout,
        .entryCount = target ? 4 : 2,
        .entries = entries
    };

    return wgpuDeviceCreateBindGroup(r->device, &desc);
}

/** Create the views of the scratch chain the downsample of a float copy writes.
 *
 * Its level k holds the downsample of level k + 1 of the copy, for every level
 * but the coarsest, which the downsample writes to the copy itself.
 *
 * @param r The renderer
 * @param format Format of the copy
 */
static void box_transparent_scratch(
    box_renderer_t *r,
    WGPUTextureFormat format)
{
    uint32_t levels = r->v->scene_mips_count - 2;
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { box_blur_extent(r->v->width, 1), box_blur_extent(r->v->height, 1), 1 },
        .format = format,
        .mipLevelCount = levels,
        .sampleCount = 1
    };
    r->v->scene_scratch = wgpuDeviceCreateTexture(r->device, &desc);

    uint32_t mip;
    for (mip = 0; mip < levels; mip ++) {
        WGPUTextureViewDescriptor mip_desc = {
            .format = format,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = mip,
            .mipLevelCount = 1,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_All
        };
        r->v->scene_scratch_mips[mip] = wgpuTextureCreateView(r->v->scene_scratch, &mip_desc);
    }
}

/** Create the bind groups of the dispatches that blur a float copy.
 *
 * @param r The renderer
 */
static void box_transparent_compute_groups(
    box_renderer_t *r)
{
    box_gpu_view_t *v = r->v;
    uint32_t last = v->scene_mips_count - 1;
    uint32_t mip;
    for (mip = 1; mip <= last; mip ++) {
        WGPUTextureView source = mip == 1 ? v->scene_mips[0] : v->scene_scratch_mips[mip - 2];
        WGPUTextureView target = mip == last ? v->scene_mips[mip] : v->scene_scratch_mips[mip - 1];
        v->scene_down[mip] = box_transparent_level_group(r, source, source, target);
        if (mip < last) {
            v->scene_up[mip] = box_transparent_level_group(r, v->scene_mips[mip + 1],
                v->scene_scratch_mips[mip - 1], v->scene_mips[mip]);
        }
    }
}

void box_transparent_resize(
    box_renderer_t *r,
    WGPUTextureFormat format)
{
    box_transparent_view_fini(r->v);
    r->v->scene_mips_count = box_blur_levels(r->v->width, r->v->height);
    r->v->scene_format = format;
    bool compute = format == BOX_EFFECT_HDR_FORMAT;

    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
            (compute ? WGPUTextureUsage_StorageBinding : WGPUTextureUsage_RenderAttachment),
        .dimension = WGPUTextureDimension_2D,
        .size = { r->v->width, r->v->height, 1 },
        .format = format,
        .mipLevelCount = r->v->scene_mips_count,
        .sampleCount = 1
    };
    r->v->scene = wgpuDeviceCreateTexture(r->device, &desc);

    WGPUTextureViewDescriptor view_desc = {
        .format = format,
        .dimension = WGPUTextureViewDimension_2D,
        .mipLevelCount = r->v->scene_mips_count,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    };
    r->v->scene_view = wgpuTextureCreateView(r->v->scene, &view_desc);

    uint32_t mip;
    for (mip = 0; mip < r->v->scene_mips_count; mip ++) {
        WGPUTextureViewDescriptor mip_desc = {
            .format = format,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = mip,
            .mipLevelCount = 1,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_All
        };
        r->v->scene_mips[mip] = wgpuTextureCreateView(r->v->scene, &mip_desc);
    }

    if (compute && r->v->scene_mips_count > 2) {
        box_transparent_scratch(r, format);
    }

    if (compute && r->blur_compute_layout) {
        box_transparent_compute_groups(r);
    }

    for (mip = 1; !compute && r->blur_layout && mip < r->v->scene_mips_count; mip ++) {
        r->v->scene_down[mip] = box_transparent_level_group(r, r->v->scene_mips[mip - 1],
            NULL, NULL);
        if (mip + 1 < r->v->scene_mips_count) {
            r->v->scene_up[mip] = box_transparent_level_group(r, r->v->scene_mips[mip + 1],
                NULL, NULL);
        }
    }

    r->v->env_dirty = true;
}

/** Create one full screen pipeline of the blur chain of a copy in the swap chain format.
 *
 * @param r The renderer
 * @param module The module holding both blur entry points
 * @param layout The pipeline layout
 * @param entry Name of the fragment entry point
 * @param blend Blend state of the pass, NULL when the pass replaces its target
 * @return The pipeline
 */
static WGPURenderPipeline box_transparent_blur_pipeline(
    box_renderer_t *r,
    WGPUShaderModule module,
    WGPUPipelineLayout layout,
    const char *entry,
    const WGPUBlendState *blend)
{
    WGPUColorTargetState target = {
        .format = r->format,
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
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .cullMode = WGPUCullMode_None,
            .frontFace = WGPUFrontFace_CW
        },
        .multisample = { .count = 1, .mask = 0xFFFFFFFF }
    };

    return wgpuDeviceCreateRenderPipeline(r->device, &desc);
}

/** Create one dispatch pipeline of the blur chain of a float copy.
 *
 * @param r The renderer
 * @param module The module holding both blur entry points
 * @param layout The pipeline layout
 * @param entry Name of the compute entry point
 * @return The pipeline
 */
static WGPUComputePipeline box_transparent_blur_compute(
    box_renderer_t *r,
    WGPUShaderModule module,
    WGPUPipelineLayout layout,
    const char *entry)
{
    WGPUComputePipelineDescriptor desc = {
        .label = BOX_GPU_LABEL("glass blur"),
        .layout = layout,
        .compute = {
            .module = module,
            .entryPoint = { .data = entry, .length = WGPU_STRLEN }
        }
    };

    return wgpuDeviceCreateComputePipeline(r->device, &desc);
}

/** Create the layout and the pipelines of the dispatches that blur a float copy.
 *
 * @param r The renderer
 * @param module The module holding the blur entry points
 */
static void box_transparent_compute_init(
    box_renderer_t *r,
    WGPUShaderModule module)
{
    WGPUBindGroupLayoutEntry entries[4] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }, {
            .binding = 2,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 3,
            .visibility = WGPUShaderStage_Compute,
            .storageTexture = {
                .access = WGPUStorageTextureAccess_WriteOnly,
                .format = BOX_EFFECT_HDR_FORMAT,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }
    };
    WGPUBindGroupLayoutDescriptor layout_desc = { .entryCount = 4, .entries = entries };
    r->blur_compute_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->blur_compute_layout
    };
    WGPUPipelineLayout pipeline_layout =
        wgpuDeviceCreatePipelineLayout(r->device, &pipeline_layout_desc);
    r->blur_compute_down = box_transparent_blur_compute(r, module, pipeline_layout, "cs_down");
    r->blur_compute_up = box_transparent_blur_compute(r, module, pipeline_layout, "cs_up");
    wgpuPipelineLayoutRelease(pipeline_layout);
}

void box_transparent_init(
    box_renderer_t *r)
{
    WGPUSamplerDescriptor sampler_desc = {
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Linear,
        .lodMaxClamp = (float)BOX_BLUR_LEVELS_MAX,
        .maxAnisotropy = 1
    };
    r->scene_sampler = wgpuDeviceCreateSampler(r->device, &sampler_desc);

    WGPUBindGroupLayoutEntry entries[2] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }
    };
    WGPUBindGroupLayoutDescriptor layout_desc = { .entryCount = 2, .entries = entries };
    r->blur_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);

    WGPUShaderModule module = box_shader_module(r, BOX_SHADER_BLUR);

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->blur_layout
    };
    WGPUPipelineLayout pipeline_layout =
        wgpuDeviceCreatePipelineLayout(r->device, &pipeline_layout_desc);

    r->blur_down = box_transparent_blur_pipeline(r, module, pipeline_layout, "fs_down", NULL);
    r->blur_up = box_transparent_blur_pipeline(r, module, pipeline_layout, "fs_up",
        &box_blur_mix);
    wgpuPipelineLayoutRelease(pipeline_layout);
    box_transparent_compute_init(r, module);
    wgpuShaderModuleRelease(module);
}

void box_transparent_bind_layout(
    WGPUBindGroupLayoutEntry *out,
    uint32_t base)
{
    out[0] = (WGPUBindGroupLayoutEntry){
        .binding = base,
        .visibility = WGPUShaderStage_Fragment,
        .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D
        }
    };
    out[1] = (WGPUBindGroupLayoutEntry){
        .binding = base + 1,
        .visibility = WGPUShaderStage_Fragment,
        .sampler = { .type = WGPUSamplerBindingType_Filtering }
    };
}

void box_transparent_bind_group(
    box_renderer_t *r,
    WGPUBindGroupEntry *out,
    uint32_t base)
{
    out[0] = (WGPUBindGroupEntry){ .binding = base, .textureView = r->v->scene_view };
    out[1] = (WGPUBindGroupEntry){ .binding = base + 1, .sampler = r->scene_sampler };
}

void box_transparent_needed(
    box_renderer_t *r,
    const box_view_t *view)
{
    r->v->scene_needed = view->translucent_count != 0;
}

void box_transparent_upload(
    box_renderer_t *r,
    const box_packet_t *p)
{
    uint32_t count = p->transparent.count;
    if (!count) {
        goto done;
    }

    if (count > r->sorted_capacity) {
        uint32_t grown = r->sorted_capacity ? r->sorted_capacity * 2 : 256;
        if (grown < count) {
            grown = count;
        }

        if (r->sorted) wgpuBufferRelease(r->sorted);
        WGPUBufferDescriptor desc = {
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)grown * sizeof(box_transparent_t)
        };
        r->sorted = wgpuDeviceCreateBuffer(r->device, &desc);
        r->sorted_capacity = grown;
    }

    wgpuQueueWriteBuffer(r->gpu_queue, r->sorted, 0, p->transparent.data,
        (size_t)count * sizeof(box_transparent_t));
done:
    return;
}

/** Record the dispatch of the blur chain of a float copy that writes one level.
 *
 * @param r The renderer
 * @param pass The compute pass that blurs the copy
 * @param pipeline The pipeline of the dispatch
 * @param group The levels the dispatch reads and writes
 * @param level Level of the copy the size of the written level matches
 */
static void box_transparent_blur_dispatch(
    box_renderer_t *r,
    WGPUComputePassEncoder pass,
    WGPUComputePipeline pipeline,
    WGPUBindGroup group,
    uint32_t level)
{
    uint32_t width = box_blur_extent(r->v->width, level);
    uint32_t height = box_blur_extent(r->v->height, level);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, (width + BOX_MIP_GROUP - 1) / BOX_MIP_GROUP,
        (height + BOX_MIP_GROUP - 1) / BOX_MIP_GROUP, 1);
}

/** Blur a float copy in one compute pass, a dispatch per level.
 *
 * @param r The renderer
 * @param encoder The command encoder
 */
static void box_transparent_blur_compute_run(
    box_renderer_t *r,
    WGPUCommandEncoder encoder)
{
    WGPUComputePassEncoder pass = box_timing_compute(r, encoder, "glass");
    uint32_t level;
    for (level = 1; level < r->v->scene_mips_count; level ++) {
        box_transparent_blur_dispatch(r, pass, r->blur_compute_down, r->v->scene_down[level],
            level);
    }

    for (level = r->v->scene_mips_count - 1; level > 1; level --) {
        box_transparent_blur_dispatch(r, pass, r->blur_compute_up, r->v->scene_up[level - 1],
            level - 1);
    }

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
}

/** Record one pass of the blur chain over one level of the copy.
 *
 * @param r The renderer
 * @param encoder The command encoder
 * @param pipeline The pipeline of the pass
 * @param group The level the pass samples
 * @param level The level the pass writes
 * @param mix The pass mixes into the level instead of replacing it
 */
static void box_transparent_blur_pass(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    WGPURenderPipeline pipeline,
    WGPUBindGroup group,
    uint32_t level,
    bool mix)
{
    WGPURenderPassColorAttachment color = {
        .view = r->v->scene_mips[level],
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = mix ? WGPULoadOp_Load : WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store
    };
    WGPURenderPassDescriptor desc = { .colorAttachmentCount = 1, .colorAttachments = &color };
    WGPURenderPassEncoder pass = box_timing_render(r, encoder, &desc, "glass");
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    if (mix) {
        WGPUColor weight = { BOX_BLUR_MIX_WEIGHT, BOX_BLUR_MIX_WEIGHT, BOX_BLUR_MIX_WEIGHT,
            BOX_BLUR_MIX_WEIGHT };
        wgpuRenderPassEncoderSetBlendConstant(pass, &weight);
    }

    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

void box_transparent_prepare(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    WGPUTexture target)
{
    WGPUTexelCopyTextureInfo src = { .texture = target, .aspect = WGPUTextureAspect_All };
    WGPUTexelCopyTextureInfo dst = { .texture = r->v->scene, .aspect = WGPUTextureAspect_All };
    WGPUExtent3D extent = { r->v->width, r->v->height, 1 };
    wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &extent);

    if (r->v->scene_format == BOX_EFFECT_HDR_FORMAT) {
        if (r->v->scene_mips_count > 1) {
            box_transparent_blur_compute_run(r, encoder);
        }

        goto done;
    }

    uint32_t level;
    for (level = 1; level < r->v->scene_mips_count; level ++) {
        box_transparent_blur_pass(r, encoder, r->blur_down, r->v->scene_down[level],
            level, false);
    }

    for (level = r->v->scene_mips_count - 1; level > 1; level --) {
        box_transparent_blur_pass(r, encoder, r->blur_up, r->v->scene_up[level - 1],
            level - 1, true);
    }

done:
    return;
}

/** Pipeline of one blend and back face mode, created the first time it is drawn.
 *
 * @param r The renderer
 * @param blend One of the BOX_BLEND constants
 * @param back One of the BOX_BACK constants
 * @param hdr The pass writes the float scene target of a view with a chain
 * @param cover The pass draws into the normal attachment of a chain
 * @return The pipeline
 */
static WGPURenderPipeline box_transparent_pipeline(
    box_renderer_t *r,
    uint32_t blend,
    uint32_t back,
    bool hdr,
    bool cover)
{
    uint32_t cull = back == BOX_BACK_CULL;
    uint32_t slot = hdr ? 1 : 0;
    uint32_t mode = cover ? 1 : 0;
    uint32_t normals = BOX_NORMALS_NONE;
    if (cover) {
        normals = blend == BOX_BLEND_OPAQUE ? BOX_NORMALS_WRITE : BOX_NORMALS_COVER;
    }

    if (!r->sorted_pipelines[blend][cull][slot][mode]) {
        const WGPUBlendState *state = blend ? &box_blend_states[blend] : NULL;
        r->sorted_pipelines[blend][cull][slot][mode] = box_frame_pipeline(r,
            sizeof(box_transparent_t), offsetof(box_transparent_t, data),
            cull ? WGPUCullMode_Back : WGPUCullMode_None, blend == BOX_BLEND_OPAQUE, state,
            hdr, normals);
    }

    return r->sorted_pipelines[blend][cull][slot][mode];
}

/** Pipeline that composites a render target, created the first time it is drawn.
 *
 * @param r The renderer
 * @param hdr The pass writes the float scene target of a view with a chain
 * @param cover The pass draws into the normal attachment of a chain
 * @return The pipeline
 */
static WGPURenderPipeline box_transparent_group_pipeline(
    box_renderer_t *r,
    bool hdr,
    bool cover)
{
    uint32_t slot = hdr ? 1 : 0;
    uint32_t mode = cover ? 1 : 0;
    if (!r->group_pipelines[slot][mode]) {
        r->group_pipelines[slot][mode] = box_frame_pipeline(r,
            sizeof(box_transparent_t), offsetof(box_transparent_t, data),
            WGPUCullMode_None, false, &box_blend_states[BOX_BLEND_ALPHA], hdr,
            cover ? BOX_NORMALS_GROUP_COVER : BOX_NORMALS_GROUP);
    }

    return r->group_pipelines[slot][mode];
}

/** Mesh bind group of a target, which binds its color in the scene slot.
 *
 * The group is kept with the target and built again when the environment group
 * of a view was, because it holds the same environment and the same texture
 * pages with one binding swapped.
 *
 * @param r The renderer
 * @param handle The target handle
 * @return The bind group, NULL when the handle names no live target
 */
static WGPUBindGroup box_transparent_group_bind(
    box_renderer_t *r,
    uint32_t handle)
{
    WGPUBindGroup result = NULL;
    box_gpu_target_t *t = box_target_get(r, handle);
    if (!t || !t->view) {
        goto done;
    }

    if (!t->composite || t->composite_epoch != r->env_epoch) {
        if (t->composite) {
            wgpuBindGroupRelease(t->composite);
        }

        t->composite = box_environment_scene_group(r, t->view);
        t->composite_epoch = r->env_epoch;
    }

    result = t->composite;
done:
    return result;
}

/** What the sorted pass has bound, so a draw only sets what differs */
typedef struct box_transparent_bound_t {
    WGPURenderPipeline pipeline; /**< Pipeline of the pass, NULL before the first set */
    WGPUBindGroup group;         /**< Bind group in the mesh slot of the pass */
    uint32_t mesh;               /**< Mesh whose buffers are bound, UINT32_MAX for none */
} box_transparent_bound_t;

/** Bind the pipeline, mesh bind group and mesh buffers of a draw that are not bound yet.
 *
 * @param pass The render pass
 * @param bound What the pass has bound, updated
 * @param pipeline The pipeline of the draw
 * @param group The bind group of the mesh slot
 * @param handle The mesh handle
 * @param m The mesh
 */
static void box_transparent_bind(
    WGPURenderPassEncoder pass,
    box_transparent_bound_t *bound,
    WGPURenderPipeline pipeline,
    WGPUBindGroup group,
    uint32_t handle,
    const box_gpu_mesh_t *m)
{
    if (bound->pipeline != pipeline) {
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        bound->pipeline = pipeline;
    }

    if (bound->group != group) {
        wgpuRenderPassEncoderSetBindGroup(pass, 2, group, 0, NULL);
        bound->group = group;
    }

    if (bound->mesh != handle) {
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m->vertices, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetIndexBuffer(pass, m->indices, WGPUIndexFormat_Uint32, 0,
            WGPU_WHOLE_SIZE);
        bound->mesh = handle;
    }
}

/** Draw the composite of one group at its place in the sorted list.
 *
 * @param r The renderer
 * @param entry The sorted entry of the composite
 * @param index Index of that entry in the sorted instance buffer
 * @param pass The render pass
 * @param bound What the pass has bound, updated
 */
static void box_transparent_group_draw(
    box_renderer_t *r,
    const box_transparent_t *entry,
    uint32_t index,
    WGPURenderPassEncoder pass,
    box_transparent_bound_t *bound)
{
    box_gpu_mesh_t *m = box_vec_at(&r->meshes, entry->mesh);
    WGPUBindGroup bind = box_transparent_group_bind(r, entry->group);
    if (!m->alive || !bind) {
        goto done;
    }

    box_transparent_bind(pass, bound, box_transparent_group_pipeline(r,
        r->effect_scene != NULL, r->effect_normal != NULL), bind, entry->mesh, m);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, r->sorted,
        (uint64_t)index * sizeof(box_transparent_t), sizeof(box_transparent_t));
    wgpuRenderPassEncoderDrawIndexed(pass, m->index_count, 1, 0, 0, 0);
done:
    return;
}

void box_transparent_draw(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p,
    WGPURenderPassEncoder pass)
{
    bool hdr = r->effect_scene != NULL;
    bool cover = r->effect_normal != NULL;
    box_transparent_bound_t bound = {
        .pipeline = NULL, .group = r->v->env_group, .mesh = UINT32_MAX
    };
    uint32_t end = view->transparent_first + view->transparent_count;
    uint32_t first = view->transparent_first;
    while (first < end) {
        box_transparent_t *entry = box_vec_at(&p->transparent, first);
        if (entry->group) {
            box_transparent_group_draw(r, entry, first, pass, &bound);
            first ++;
            continue;
        }

        uint32_t last = first + 1;
        while (last < end) {
            box_transparent_t *next = box_vec_at(&p->transparent, last);
            if (next->mesh != entry->mesh || next->blend != entry->blend ||
                next->back != entry->back || next->group)
            {
                break;
            }

            last ++;
        }

        box_gpu_mesh_t *m = box_vec_at(&r->meshes, entry->mesh);
        if (m->alive) {
            box_transparent_bind(pass, &bound,
                box_transparent_pipeline(r, entry->blend, entry->back, hdr, cover),
                r->v->env_group, entry->mesh, m);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 1, r->sorted,
                (uint64_t)first * sizeof(box_transparent_t),
                (uint64_t)(last - first) * sizeof(box_transparent_t));
            wgpuRenderPassEncoderDrawIndexed(pass, m->index_count, last - first, 0, 0, 0);
        }

        first = last;
    }
}

void box_transparent_reset(
    box_renderer_t *r)
{
    uint32_t slot;
    for (slot = 0; slot < BOX_EFFECT_FORMATS; slot ++) {
        uint32_t mode;
        for (mode = 0; mode < BOX_COVER_MODES; mode ++) {
            if (r->group_pipelines[slot][mode]) {
                wgpuRenderPipelineRelease(r->group_pipelines[slot][mode]);
                r->group_pipelines[slot][mode] = NULL;
            }
        }
    }

    uint32_t blend;
    for (blend = 0; blend < BOX_BLEND_COUNT; blend ++) {
        uint32_t cull;
        for (cull = 0; cull < BOX_CULL_MODES; cull ++) {
            uint32_t slot;
            for (slot = 0; slot < BOX_EFFECT_FORMATS; slot ++) {
                uint32_t mode;
                for (mode = 0; mode < BOX_COVER_MODES; mode ++) {
                    WGPURenderPipeline p = r->sorted_pipelines[blend][cull][slot][mode];
                    if (p) {
                        wgpuRenderPipelineRelease(p);
                        r->sorted_pipelines[blend][cull][slot][mode] = NULL;
                    }
                }
            }
        }
    }
}

void box_transparent_fini(
    box_renderer_t *r)
{
    box_transparent_reset(r);

    if (r->sorted) wgpuBufferRelease(r->sorted);
    if (r->scene_sampler) wgpuSamplerRelease(r->scene_sampler);
    if (r->blur_layout) wgpuBindGroupLayoutRelease(r->blur_layout);
    if (r->blur_down) wgpuRenderPipelineRelease(r->blur_down);
    if (r->blur_up) wgpuRenderPipelineRelease(r->blur_up);
    if (r->blur_compute_layout) wgpuBindGroupLayoutRelease(r->blur_compute_layout);
    if (r->blur_compute_down) wgpuComputePipelineRelease(r->blur_compute_down);
    if (r->blur_compute_up) wgpuComputePipelineRelease(r->blur_compute_up);
    r->blur_down = NULL;
    r->blur_up = NULL;
    r->blur_compute_layout = NULL;
    r->blur_compute_down = NULL;
    r->blur_compute_up = NULL;
    r->sorted = NULL;
    r->sorted_capacity = 0;
    r->scene_sampler = NULL;
    r->blur_layout = NULL;
}
