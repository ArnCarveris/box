/**
 * @file renderer/effect_temporal.c
 * @brief Temporal setup pass that a post process pass can run before it draws.
 *
 * The setup shader renders a raw image of the view into a work target and then
 * resolves it against the history of the previous frame into one of two history
 * targets, which the post process pass samples and the next frame reads back.
 */

#include "renderer_internal.h"

/** Bindings of the bind group of a temporal pass */
#define BOX_EFFECT_TEMPORAL_BINDINGS (7)

/** Create a fullscreen pipeline from a fragment entry point of a setup shader.
 *
 * @param r The renderer
 * @param module The shader module
 * @param layout The pipeline layout
 * @param entry Name of the fragment entry point
 * @param format Format of the target the pipeline writes
 * @return The pipeline
 */
static WGPURenderPipeline box_effect_temporal_pipeline(
    box_renderer_t *r,
    WGPUShaderModule module,
    WGPUPipelineLayout layout,
    const char *entry,
    WGPUTextureFormat format)
{
    WGPUColorTargetState target = { .format = format,
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

void box_effect_temporal_init(
    box_renderer_t *r)
{
    WGPUBindGroupLayoutEntry entries[BOX_EFFECT_TEMPORAL_BINDINGS] = {
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
        .entryCount = BOX_EFFECT_TEMPORAL_BINDINGS,
        .entries = entries
    };
    r->setup_temporal_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->setup_temporal_layout
    };
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(r->device,
        &pipeline_layout_desc);
    wgpuPipelineLayoutRelease(layout);
}

/** Build the two pipelines of the setup shader of a pass, unless they already exist.
 *
 * @param r The renderer
 * @param fx The pass whose setup shader and format the pipelines are built from
 * @return Whether both pipelines exist
 */
static bool box_effect_temporal_ready(
    box_renderer_t *r,
    const box_effect_t *fx)
{
    bool result = true;
    if (r->setup_temporal_shader == fx->setup_shader && r->setup_temporal_first) {
        goto done;
    }

    if (r->setup_temporal_first) wgpuRenderPipelineRelease(r->setup_temporal_first);
    if (r->setup_temporal_second) wgpuRenderPipelineRelease(r->setup_temporal_second);
    r->setup_temporal_first = NULL;
    r->setup_temporal_second = NULL;
    r->setup_temporal_shader = fx->setup_shader;
    WGPUShaderModule module = box_shader_module(r, fx->setup_shader);
    if (!module) {
        result = false;
        goto done;
    }

    WGPUPipelineLayoutDescriptor desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->setup_temporal_layout
    };
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(r->device, &desc);
    WGPUTextureFormat format = fx->setup_format == BOX_EFFECT_TEXTURE_RGBA16 ?
        WGPUTextureFormat_RGBA16Float : WGPUTextureFormat_RG16Float;
    r->setup_temporal_first = box_effect_temporal_pipeline(r, module, layout,
        "fs_first", format);
    r->setup_temporal_second = box_effect_temporal_pipeline(r, module, layout,
        "fs_second", format);
    wgpuPipelineLayoutRelease(layout);
    wgpuShaderModuleRelease(module);

    result = r->setup_temporal_first && r->setup_temporal_second;
done:
    return result;
}

void box_effect_temporal_view_fini(
    box_gpu_view_t *v)
{
    uint32_t i;
    for (i = 0; i < 2; i ++) {
        if (v->setup_temporal_hist_view[i]) {
            wgpuTextureViewRelease(v->setup_temporal_hist_view[i]);
        }

        if (v->setup_temporal_hist[i]) wgpuTextureRelease(v->setup_temporal_hist[i]);
        v->setup_temporal_hist_view[i] = NULL;
        v->setup_temporal_hist[i] = NULL;
    }

    if (v->setup_temporal_work_view) wgpuTextureViewRelease(v->setup_temporal_work_view);
    if (v->setup_temporal_work) wgpuTextureRelease(v->setup_temporal_work);
    v->setup_temporal_work_view = NULL;
    v->setup_temporal_work = NULL;
    v->setup_temporal_width = 0;
    v->setup_temporal_height = 0;
    v->setup_temporal_valid = false;
}

/** Recreate the work target and the two history targets of the current view.
 *
 * @param r The renderer
 * @param width Width of the targets in pixels
 * @param height Height of the targets in pixels
 * @param format Format of the targets
 */
static void box_effect_temporal_resize(
    box_renderer_t *r,
    uint32_t width,
    uint32_t height,
    WGPUTextureFormat format)
{
    box_effect_temporal_view_fini(r->v);
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { width, height, 1 },
        .format = format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    r->v->setup_temporal_work = wgpuDeviceCreateTexture(r->device, &desc);
    r->v->setup_temporal_work_view = wgpuTextureCreateView(r->v->setup_temporal_work, NULL);

    uint32_t i;
    for (i = 0; i < 2; i ++) {
        r->v->setup_temporal_hist[i] = wgpuDeviceCreateTexture(r->device, &desc);
        r->v->setup_temporal_hist_view[i] =
            wgpuTextureCreateView(r->v->setup_temporal_hist[i], NULL);
    }

    r->v->setup_temporal_width = width;
    r->v->setup_temporal_height = height;
    r->v->setup_temporal_format = format;
    r->v->setup_temporal_slot = 0;
}

/** Create a bind group of a temporal pass.
 *
 * @param r The renderer
 * @param raw The raw image the resolve reads, NULL binds the dummy texture
 * @param history The history of the previous frame, NULL binds the dummy texture
 * @return The bind group
 */
static WGPUBindGroup box_effect_temporal_group(
    box_renderer_t *r,
    WGPUTextureView raw,
    WGPUTextureView history)
{
    WGPUTextureView normal = r->effect_normal ? r->effect_normal->view :
        r->effect_dummy_view;
    WGPUTextureView motion = r->effect_motion ? r->effect_motion->view :
        r->effect_dummy_view;
    WGPUBindGroupEntry entries[BOX_EFFECT_TEMPORAL_BINDINGS] = {
        { .binding = 0, .buffer = r->effect_uniform, .size = sizeof(box_effect_uniform_t) },
        { .binding = 1, .textureView = r->v->depth_sample },
        { .binding = 2, .textureView = normal },
        { .binding = 3, .sampler = r->effect_sampler },
        { .binding = 4, .textureView = motion },
        { .binding = 5, .textureView = raw ? raw : r->effect_dummy_view },
        { .binding = 6, .textureView = history ? history : r->effect_dummy_view }
    };
    WGPUBindGroupDescriptor desc = {
        .layout = r->setup_temporal_layout,
        .entryCount = BOX_EFFECT_TEMPORAL_BINDINGS,
        .entries = entries
    };

    return wgpuDeviceCreateBindGroup(r->device, &desc);
}

/** Record one fullscreen pass and release its bind group.
 *
 * @param r The renderer
 * @param encoder The command encoder
 * @param pipeline The pipeline of the pass
 * @param group The bind group of the pass, released after recording
 * @param target The view the pass writes
 * @param slot Index of the pass in the packet, which is its uniform slot
 */
static void box_effect_temporal_pass(
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
    WGPURenderPassEncoder pass = box_timing_render(r, encoder, &desc, "effect.setup");
    uint32_t offset = slot * BOX_EFFECT_UNIFORM_STRIDE;
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &offset);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(group);
}

WGPUTextureView box_effect_temporal_run(
    box_renderer_t *r,
    const box_effect_t *fx,
    uint32_t slot,
    WGPUCommandEncoder encoder)
{
    WGPUTextureView result = NULL;
    if (!box_effect_temporal_ready(r, fx)) {
        goto done;
    }

    int32_t scale_param = fx->setup_parameter[0];
    bool reduced = scale_param >= 0 && scale_param < BOX_EFFECT_PARAM_MAX &&
        fx->params[scale_param] > 0.5f;
    uint32_t width = box_effect_temporal_size(r->v->width, reduced);
    uint32_t height = box_effect_temporal_size(r->v->height, reduced);
    WGPUTextureFormat format = fx->setup_format == BOX_EFFECT_TEXTURE_RGBA16 ?
        WGPUTextureFormat_RGBA16Float : WGPUTextureFormat_RG16Float;
    if (r->v->setup_temporal_width != width || r->v->setup_temporal_height != height ||
        r->v->setup_temporal_format != format)
    {
        box_effect_temporal_resize(r, width, height, format);
    }

    uint32_t dst = r->v->setup_temporal_slot;
    box_effect_temporal_pass(r, encoder, r->setup_temporal_first,
        box_effect_temporal_group(r, NULL, r->v->setup_temporal_hist_view[dst ^ 1]),
        r->v->setup_temporal_work_view, slot);
    WGPUBindGroup resolve = box_effect_temporal_group(r, r->v->setup_temporal_work_view,
        r->v->setup_temporal_hist_view[dst ^ 1]);
    box_effect_temporal_pass(r, encoder, r->setup_temporal_second, resolve,
        r->v->setup_temporal_hist_view[dst], slot);
    r->v->setup_temporal_slot = dst ^ 1;
    r->v->setup_temporal_valid = true;
    result = r->v->setup_temporal_hist_view[dst];
done:
    return result;
}

void box_effect_temporal_fini(
    box_renderer_t *r)
{
    if (r->setup_temporal_first) wgpuRenderPipelineRelease(r->setup_temporal_first);
    if (r->setup_temporal_second) wgpuRenderPipelineRelease(r->setup_temporal_second);
    if (r->setup_temporal_layout) wgpuBindGroupLayoutRelease(r->setup_temporal_layout);
    r->setup_temporal_first = NULL;
    r->setup_temporal_second = NULL;
    r->setup_temporal_layout = NULL;
    r->setup_temporal_shader = BOX_SHADER_NONE;
}
