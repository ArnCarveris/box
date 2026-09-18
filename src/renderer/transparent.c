/**
 * @file renderer/transparent.c
 * @brief Sorted pass: blend pipelines, the copy of the opaque target and its mips.
 *
 * The copy is what alpha blended draws read to blur what is behind them, which
 * their roughness turns into a mip level and a weight.
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

/** Mip levels a copy of the target holds, down to a single texel.
 *
 * @param width Target width in pixels
 * @param height Target height in pixels
 * @return The number of mip levels
 */
static uint32_t box_transparent_mips(
    uint32_t width,
    uint32_t height)
{
    uint32_t size = width > height ? width : height;
    uint32_t result = 1;
    while (size > 1 && result < BOX_SCENE_MIPS_MAX) {
        size >>= 1;
        result ++;
    }

    return result;
}

/** Release the copy of the opaque target, its views and its bind groups.
 *
 * @param r The renderer
 */
static void box_transparent_release_scene(
    box_renderer_t *r)
{
    uint32_t mip;
    for (mip = 0; mip < r->scene_mips_count; mip ++) {
        if (r->scene_mips[mip]) wgpuTextureViewRelease(r->scene_mips[mip]);
        if (r->scene_groups[mip]) wgpuBindGroupRelease(r->scene_groups[mip]);
        r->scene_mips[mip] = NULL;
        r->scene_groups[mip] = NULL;
    }

    if (r->scene_view) wgpuTextureViewRelease(r->scene_view);
    if (r->scene) wgpuTextureRelease(r->scene);
    r->scene_view = NULL;
    r->scene = NULL;
    r->scene_mips_count = 0;
}

/** Create the bind group a downsample pass reads the previous mip through.
 *
 * @param r The renderer
 * @param mip The mip the pass writes
 */
static void box_transparent_scene_group(
    box_renderer_t *r,
    uint32_t mip)
{
    WGPUBindGroupEntry entries[2] = {
        { .binding = 0, .textureView = r->scene_mips[mip - 1] },
        { .binding = 1, .sampler = r->scene_sampler }
    };
    WGPUBindGroupDescriptor desc = {
        .layout = r->downsample_layout,
        .entryCount = 2,
        .entries = entries
    };
    r->scene_groups[mip] = wgpuDeviceCreateBindGroup(r->device, &desc);
}

void box_transparent_resize(
    box_renderer_t *r)
{
    box_transparent_release_scene(r);
    r->scene_mips_count = box_transparent_mips(r->width, r->height);

    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
            WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = { r->width, r->height, 1 },
        .format = r->format,
        .mipLevelCount = r->scene_mips_count,
        .sampleCount = 1
    };
    r->scene = wgpuDeviceCreateTexture(r->device, &desc);

    WGPUTextureViewDescriptor view_desc = {
        .format = r->format,
        .dimension = WGPUTextureViewDimension_2D,
        .mipLevelCount = r->scene_mips_count,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    };
    r->scene_view = wgpuTextureCreateView(r->scene, &view_desc);

    uint32_t mip;
    for (mip = 0; mip < r->scene_mips_count; mip ++) {
        WGPUTextureViewDescriptor mip_desc = {
            .format = r->format,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = mip,
            .mipLevelCount = 1,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_All
        };
        r->scene_mips[mip] = wgpuTextureCreateView(r->scene, &mip_desc);
    }

    for (mip = 1; r->downsample_layout && mip < r->scene_mips_count; mip ++) {
        box_transparent_scene_group(r, mip);
    }

    if (r->env_layout) {
        box_environment_rebuild(r);
    }
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
        .lodMaxClamp = (float)BOX_SCENE_MIPS_MAX,
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
    r->downsample_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);

    WGPUShaderSourceWGSL wgsl = {
        .chain = { .sType = WGPUSType_ShaderSourceWGSL },
        .code = { .data = box_downsample_wgsl, .length = WGPU_STRLEN }
    };
    WGPUShaderModuleDescriptor module_desc = { .nextInChain = (WGPUChainedStruct*)&wgsl };
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(r->device, &module_desc);

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->downsample_layout
    };
    WGPUPipelineLayout pipeline_layout =
        wgpuDeviceCreatePipelineLayout(r->device, &pipeline_layout_desc);

    WGPUColorTargetState target = { .format = r->format, .writeMask = WGPUColorWriteMask_All };
    WGPUFragmentState fragment = {
        .module = module,
        .entryPoint = { .data = "fs_main", .length = WGPU_STRLEN },
        .targetCount = 1,
        .targets = &target
    };
    WGPURenderPipelineDescriptor desc = {
        .layout = pipeline_layout,
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
    r->downsample_pipeline = wgpuDeviceCreateRenderPipeline(r->device, &desc);
    wgpuPipelineLayoutRelease(pipeline_layout);
    wgpuShaderModuleRelease(module);

    box_transparent_resize(r);
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
    out[0] = (WGPUBindGroupEntry){ .binding = base, .textureView = r->scene_view };
    out[1] = (WGPUBindGroupEntry){ .binding = base + 1, .sampler = r->scene_sampler };
}

void box_transparent_upload(
    box_renderer_t *r,
    const box_packet_t *p)
{
    uint32_t count = p->transparent.count;
    r->scene_needed = false;
    if (!count) {
        goto done;
    }

    uint32_t i;
    for (i = 0; i < count && !r->scene_needed; i ++) {
        uint32_t blend = ((box_transparent_t*)box_vec_at(&p->transparent, i))->blend;
        r->scene_needed = blend == BOX_BLEND_ALPHA || blend == BOX_BLEND_PREMULTIPLIED;
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

void box_transparent_prepare(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    WGPUTexture target)
{
    WGPUTexelCopyTextureInfo src = { .texture = target, .aspect = WGPUTextureAspect_All };
    WGPUTexelCopyTextureInfo dst = { .texture = r->scene, .aspect = WGPUTextureAspect_All };
    WGPUExtent3D extent = { r->width, r->height, 1 };
    wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &extent);

    uint32_t mip;
    for (mip = 1; mip < r->scene_mips_count; mip ++) {
        WGPURenderPassColorAttachment color = {
            .view = r->scene_mips[mip],
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = WGPULoadOp_Clear,
            .storeOp = WGPUStoreOp_Store
        };
        WGPURenderPassDescriptor desc = { .colorAttachmentCount = 1, .colorAttachments = &color };
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
        wgpuRenderPassEncoderSetPipeline(pass, r->downsample_pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, r->scene_groups[mip], 0, NULL);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
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
    const box_packet_t *p,
    WGPURenderPassEncoder pass)
{
    uint32_t first = 0;
    while (first < p->transparent.count) {
        box_transparent_t *entry = box_vec_at(&p->transparent, first);
        uint32_t last = first + 1;
        while (last < p->transparent.count) {
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

void box_transparent_fini(
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

    box_transparent_release_scene(r);
    if (r->sorted) wgpuBufferRelease(r->sorted);
    if (r->scene_sampler) wgpuSamplerRelease(r->scene_sampler);
    if (r->downsample_layout) wgpuBindGroupLayoutRelease(r->downsample_layout);
    if (r->downsample_pipeline) wgpuRenderPipelineRelease(r->downsample_pipeline);
    r->sorted = NULL;
    r->sorted_capacity = 0;
    r->scene_sampler = NULL;
    r->downsample_layout = NULL;
    r->downsample_pipeline = NULL;
}
