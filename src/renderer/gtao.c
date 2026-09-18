/**
 * @file renderer/gtao.c
 * @brief Ambient occlusion of a view: the horizon trace, its denoise and history.
 *
 * The occlusion input of a pass is work the frame does for it. A trace pass
 * walks the horizon of every pixel of a half resolution copy of the view and
 * writes the visibility it found next to the view distance of the pixel; a
 * resolve pass denoises that with a depth aware three by three kernel and mixes
 * it with the occlusion of the previous frame, reprojected with the motion
 * vectors and rejected where the distance of a pixel no longer matches.
 *
 * Both textures of the history belong to the view, so the passes of one view
 * never disturb another, and the pass that asked for the input reads the one
 * this frame wrote through a depth aware upsample of its own.
 */

#include "renderer_internal.h"

/** Bindings the occlusion passes take */
#define BOX_GTAO_BINDINGS (7)

/** Create one full screen pipeline of the occlusion passes.
 *
 * @param r The renderer
 * @param module The module holding both entry points
 * @param layout The pipeline layout
 * @param entry Name of the fragment entry point
 * @return The pipeline
 */
static WGPURenderPipeline box_gtao_pipeline(
    box_renderer_t *r,
    WGPUShaderModule module,
    WGPUPipelineLayout layout,
    const char *entry)
{
    WGPUColorTargetState target = { .format = BOX_GTAO_FORMAT,
        .writeMask = WGPUColorWriteMask_All };
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

void box_gtao_init(
    box_renderer_t *r)
{
    WGPUBindGroupLayoutEntry entries[BOX_GTAO_BINDINGS] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_effect_uniform_t)
            }
        }, {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }, {
            .binding = 4,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 5,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 6,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }
    };
    WGPUBindGroupLayoutDescriptor layout_desc = {
        .entryCount = BOX_GTAO_BINDINGS,
        .entries = entries
    };
    r->gtao_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->gtao_layout
    };
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(r->device,
        &pipeline_layout_desc);
    WGPUShaderModule module = box_shader_module(r, BOX_SHADER_GTAO);
    if (module) {
        r->gtao_trace = box_gtao_pipeline(r, module, layout, "fs_trace");
        r->gtao_resolve = box_gtao_pipeline(r, module, layout, "fs_resolve");
        wgpuShaderModuleRelease(module);
    }

    wgpuPipelineLayoutRelease(layout);
}

void box_gtao_view_fini(
    box_gpu_view_t *v)
{
    uint32_t i;
    for (i = 0; i < 2; i ++) {
        if (v->gtao_hist_view[i]) wgpuTextureViewRelease(v->gtao_hist_view[i]);
        if (v->gtao_hist[i]) wgpuTextureRelease(v->gtao_hist[i]);
        v->gtao_hist_view[i] = NULL;
        v->gtao_hist[i] = NULL;
    }

    if (v->gtao_raw_view) wgpuTextureViewRelease(v->gtao_raw_view);
    if (v->gtao_raw) wgpuTextureRelease(v->gtao_raw);
    v->gtao_raw_view = NULL;
    v->gtao_raw = NULL;
    v->gtao_width = 0;
    v->gtao_height = 0;
    v->gtao_valid = false;
}

/** Create the occlusion textures of the current view for a size.
 *
 * @param r The renderer
 * @param width Width of the trace in texels
 * @param height Height of the trace in texels
 */
static void box_gtao_resize(
    box_renderer_t *r,
    uint32_t width,
    uint32_t height)
{
    box_gtao_view_fini(r->v);
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { width, height, 1 },
        .format = BOX_GTAO_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    r->v->gtao_raw = wgpuDeviceCreateTexture(r->device, &desc);
    r->v->gtao_raw_view = wgpuTextureCreateView(r->v->gtao_raw, NULL);

    uint32_t i;
    for (i = 0; i < 2; i ++) {
        r->v->gtao_hist[i] = wgpuDeviceCreateTexture(r->device, &desc);
        r->v->gtao_hist_view[i] = wgpuTextureCreateView(r->v->gtao_hist[i], NULL);
    }

    r->v->gtao_width = width;
    r->v->gtao_height = height;
    r->v->gtao_slot = 0;
}

/** Bind group of one occlusion pass.
 *
 * @param r The renderer
 * @param raw Occlusion the trace pass wrote, NULL for the trace itself
 * @param history Occlusion of the previous frame, NULL for the trace itself
 * @return The bind group
 */
static WGPUBindGroup box_gtao_group(
    box_renderer_t *r,
    WGPUTextureView raw,
    WGPUTextureView history)
{
    WGPUTextureView normal = r->effect_normal ? r->effect_normal->view :
        r->effect_dummy_view;
    WGPUTextureView motion = r->effect_motion ? r->effect_motion->view :
        r->effect_dummy_view;
    WGPUBindGroupEntry entries[BOX_GTAO_BINDINGS] = {
        { .binding = 0, .buffer = r->effect_uniform, .size = sizeof(box_effect_uniform_t) },
        { .binding = 1, .textureView = r->v->depth_sample },
        { .binding = 2, .textureView = normal },
        { .binding = 3, .sampler = r->effect_sampler },
        { .binding = 4, .textureView = motion },
        { .binding = 5, .textureView = raw ? raw : r->effect_dummy_view },
        { .binding = 6, .textureView = history ? history : r->effect_dummy_view }
    };
    WGPUBindGroupDescriptor desc = {
        .layout = r->gtao_layout,
        .entryCount = BOX_GTAO_BINDINGS,
        .entries = entries
    };

    return wgpuDeviceCreateBindGroup(r->device, &desc);
}

/** Record one occlusion pass over one target.
 *
 * @param r The renderer
 * @param encoder The command encoder
 * @param pipeline The pipeline of the pass
 * @param group The bind group of the pass
 * @param target The attachment the pass writes
 * @param slot Index of the effect pass, which is its uniform slot
 */
static void box_gtao_pass(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    WGPURenderPipeline pipeline,
    WGPUBindGroup group,
    WGPUTextureView target,
    uint32_t slot)
{
    WGPURenderPassColorAttachment color = {
        .view = target,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store
    };
    WGPURenderPassDescriptor desc = { .colorAttachmentCount = 1, .colorAttachments = &color };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    uint32_t offset = slot * BOX_EFFECT_UNIFORM_STRIDE;
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &offset);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(group);
}

WGPUTextureView box_gtao_build(
    box_renderer_t *r,
    const box_effect_t *fx,
    uint32_t slot,
    WGPUCommandEncoder encoder)
{
    WGPUTextureView result = NULL;
    if (!r->gtao_trace || !r->gtao_resolve) {
        goto done;
    }

    bool half = fx->params[5] > 0.5f;
    uint32_t width = box_gtao_size(r->v->width, half);
    uint32_t height = box_gtao_size(r->v->height, half);
    if (r->v->gtao_width != width || r->v->gtao_height != height) {
        box_gtao_resize(r, width, height);
    }

    uint32_t dst = r->v->gtao_slot;
    box_gtao_pass(r, encoder, r->gtao_trace, box_gtao_group(r, NULL, NULL),
        r->v->gtao_raw_view, slot);
    box_gtao_pass(r, encoder, r->gtao_resolve,
        box_gtao_group(r, r->v->gtao_raw_view, r->v->gtao_hist_view[dst ^ 1]),
        r->v->gtao_hist_view[dst], slot);
    r->v->gtao_slot = dst ^ 1;
    r->v->gtao_valid = true;
    result = r->v->gtao_hist_view[dst];
done:
    return result;
}

void box_gtao_fini(
    box_renderer_t *r)
{
    if (r->gtao_trace) wgpuRenderPipelineRelease(r->gtao_trace);
    if (r->gtao_resolve) wgpuRenderPipelineRelease(r->gtao_resolve);
    if (r->gtao_layout) wgpuBindGroupLayoutRelease(r->gtao_layout);
    r->gtao_trace = NULL;
    r->gtao_resolve = NULL;
    r->gtao_layout = NULL;
}
