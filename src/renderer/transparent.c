/**
 * @file renderer/transparent.c
 * @brief Sorted pass: blend pipelines, the copy of the opaque target and its blur.
 *
 * The copy is blurred by a pyramid: a thirteen tap downsample down the levels
 * and an eight tap tent upsample mixed back up them, which translucent draws
 * read at the level their roughness asks for.
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

/** Weight an upsample pass gives the tent of the level below it */
#define BOX_BLUR_MIX_WEIGHT (0.5)

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

    if (v->scene_view) wgpuTextureViewRelease(v->scene_view);
    if (v->scene) wgpuTextureRelease(v->scene);
    v->scene_view = NULL;
    v->scene = NULL;
    v->scene_mips_count = 0;
}

/** Create the bind group a blur pass reads one level of the chain through.
 *
 * @param r The renderer
 * @param source The level the pass samples
 * @return The bind group
 */
static WGPUBindGroup box_transparent_level_group(
    box_renderer_t *r,
    uint32_t source)
{
    WGPUBindGroupEntry entries[2] = {
        { .binding = 0, .textureView = r->v->scene_mips[source] },
        { .binding = 1, .sampler = r->scene_sampler }
    };
    WGPUBindGroupDescriptor desc = {
        .layout = r->blur_layout,
        .entryCount = 2,
        .entries = entries
    };

    return wgpuDeviceCreateBindGroup(r->device, &desc);
}

void box_transparent_resize(
    box_renderer_t *r)
{
    box_transparent_view_fini(r->v);
    r->v->scene_mips_count = box_blur_levels(r->v->width, r->v->height);

    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
            WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = { r->v->width, r->v->height, 1 },
        .format = r->format,
        .mipLevelCount = r->v->scene_mips_count,
        .sampleCount = 1
    };
    r->v->scene = wgpuDeviceCreateTexture(r->device, &desc);

    WGPUTextureViewDescriptor view_desc = {
        .format = r->format,
        .dimension = WGPUTextureViewDimension_2D,
        .mipLevelCount = r->v->scene_mips_count,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    };
    r->v->scene_view = wgpuTextureCreateView(r->v->scene, &view_desc);

    uint32_t mip;
    for (mip = 0; mip < r->v->scene_mips_count; mip ++) {
        WGPUTextureViewDescriptor mip_desc = {
            .format = r->format,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = mip,
            .mipLevelCount = 1,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_All
        };
        r->v->scene_mips[mip] = wgpuTextureCreateView(r->v->scene, &mip_desc);
    }

    for (mip = 1; r->blur_layout && mip < r->v->scene_mips_count; mip ++) {
        r->v->scene_down[mip] = box_transparent_level_group(r, mip - 1);
        if (mip + 1 < r->v->scene_mips_count) {
            r->v->scene_up[mip] = box_transparent_level_group(r, mip + 1);
        }
    }

    r->v->env_dirty = true;
}

/** Create one full screen pipeline of the blur chain.
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

    WGPUShaderSourceWGSL wgsl = {
        .chain = { .sType = WGPUSType_ShaderSourceWGSL },
        .code = { .data = box_blur_wgsl, .length = WGPU_STRLEN }
    };
    WGPUShaderModuleDescriptor module_desc = { .nextInChain = (WGPUChainedStruct*)&wgsl };
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(r->device, &module_desc);

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
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
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

    uint32_t level;
    for (level = 1; level < r->v->scene_mips_count; level ++) {
        box_transparent_blur_pass(r, encoder, r->blur_down, r->v->scene_down[level], level,
            false);
    }

    for (level = r->v->scene_mips_count - 1; level > 1; level --) {
        box_transparent_blur_pass(r, encoder, r->blur_up, r->v->scene_up[level - 1], level - 1,
            true);
    }
}

/** Pipeline of one blend and back face mode, created the first time it is drawn.
 *
 * @param r The renderer
 * @param blend One of the BOX_BLEND constants
 * @param back One of the BOX_BACK constants
 * @return The pipeline
 */
static WGPURenderPipeline box_transparent_pipeline(
    box_renderer_t *r,
    uint32_t blend,
    uint32_t back)
{
    uint32_t cull = back == BOX_BACK_CULL;
    if (!r->sorted_pipelines[blend][cull]) {
        const WGPUBlendState *state = blend ? &box_blend_states[blend] : NULL;
        r->sorted_pipelines[blend][cull] = box_frame_pipeline(r, sizeof(box_transparent_t),
            offsetof(box_transparent_t, data),
            cull ? WGPUCullMode_Back : WGPUCullMode_None, blend == BOX_BLEND_OPAQUE, state);
    }

    return r->sorted_pipelines[blend][cull];
}

void box_transparent_draw(
    box_renderer_t *r,
    const box_view_t *view,
    const box_packet_t *p,
    WGPURenderPassEncoder pass)
{
    uint32_t end = view->transparent_first + view->transparent_count;
    uint32_t first = view->transparent_first;
    while (first < end) {
        box_transparent_t *entry = box_vec_at(&p->transparent, first);
        uint32_t last = first + 1;
        while (last < end) {
            box_transparent_t *next = box_vec_at(&p->transparent, last);
            if (next->mesh != entry->mesh || next->blend != entry->blend ||
                next->back != entry->back)
            {
                break;
            }

            last ++;
        }

        box_gpu_mesh_t *m = box_vec_at(&r->meshes, entry->mesh);
        if (m->alive) {
            wgpuRenderPassEncoderSetPipeline(pass,
                box_transparent_pipeline(r, entry->blend, entry->back));
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m->vertices, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 1, r->sorted,
                (uint64_t)first * sizeof(box_transparent_t),
                (uint64_t)(last - first) * sizeof(box_transparent_t));
            wgpuRenderPassEncoderSetIndexBuffer(pass, m->indices, WGPUIndexFormat_Uint32, 0,
                WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderDrawIndexed(pass, m->index_count, last - first, 0, 0, 0);
        }

        first = last;
    }
}

void box_transparent_reset(
    box_renderer_t *r)
{
    uint32_t blend;
    for (blend = 0; blend < BOX_BLEND_COUNT; blend ++) {
        uint32_t cull;
        for (cull = 0; cull < BOX_CULL_MODES; cull ++) {
            if (r->sorted_pipelines[blend][cull]) {
                wgpuRenderPipelineRelease(r->sorted_pipelines[blend][cull]);
                r->sorted_pipelines[blend][cull] = NULL;
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
    r->sorted = NULL;
    r->sorted_capacity = 0;
    r->scene_sampler = NULL;
    r->blur_layout = NULL;
    r->blur_down = NULL;
    r->blur_up = NULL;
}
