/**
 * @file renderer/effect_mip.c
 * @brief Mip chain setup pass that a post process pass can run before it draws.
 *
 * The setup shader fills the finest level of a per view pyramid from the source
 * image, downsamples it level by level and adds every coarser level back into the
 * finer one, optionally blending the result with the pyramid of the previous frame.
 */

#include "renderer_internal.h"

/** Blend state of the upsample pass, which adds the coarser level at a constant weight */
static const WGPUBlendState box_effect_mip_mix = {
    .color = { WGPUBlendOperation_Add, WGPUBlendFactor_Constant,
        WGPUBlendFactor_One },
    .alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_Zero, WGPUBlendFactor_One }
};

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

void box_effect_mip_init(
    box_renderer_t *r)
{
    WGPUBindGroupLayoutEntry entries[4] = {
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
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }, {
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }
    };
    WGPUBindGroupLayoutDescriptor layout_desc = { .entryCount = 4, .entries = entries };
    r->setup_mip_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->setup_mip_layout
    };
    WGPUPipelineLayout layout =
        wgpuDeviceCreatePipelineLayout(r->device, &pipeline_layout_desc);
    wgpuPipelineLayoutRelease(layout);
}

void box_effect_mip_fini(
    box_renderer_t *r)
{
    if (r->setup_mip_pipeline[0]) wgpuRenderPipelineRelease(r->setup_mip_pipeline[0]);
    if (r->setup_mip_pipeline[1]) wgpuRenderPipelineRelease(r->setup_mip_pipeline[1]);
    if (r->setup_mip_pipeline[2]) wgpuRenderPipelineRelease(r->setup_mip_pipeline[2]);
    if (r->setup_mip_pipeline[3]) wgpuRenderPipelineRelease(r->setup_mip_pipeline[3]);
    if (r->setup_mip_layout) wgpuBindGroupLayoutRelease(r->setup_mip_layout);
    r->setup_mip_pipeline[0] = NULL;
    r->setup_mip_pipeline[1] = NULL;
    r->setup_mip_pipeline[2] = NULL;
    r->setup_mip_layout = NULL;
    r->setup_mip_pipeline[3] = NULL;
    r->setup_mip_shader = BOX_SHADER_NONE;
}

/** Build the four pipelines of the setup shader of a pass, unless they already exist.
 *
 * @param r The renderer
 * @param fx The pass whose setup shader and format the pipelines are built from
 * @return Whether every pipeline exists
 */
static bool box_effect_mip_ready(
    box_renderer_t *r,
    const box_effect_t *fx)
{
    bool result = true;
    if (r->setup_mip_shader == fx->setup_shader && r->setup_mip_pipeline[0]) {
        goto done;
    }

    for (uint32_t i = 0; i < 4; i ++) {
        if (r->setup_mip_pipeline[i]) wgpuRenderPipelineRelease(r->setup_mip_pipeline[i]);
        r->setup_mip_pipeline[i] = NULL;
    }

    r->setup_mip_shader = fx->setup_shader;
    WGPUShaderModule module = box_shader_module(r, fx->setup_shader);
    if (!module) {
        result = false;
        goto done;
    }

    WGPUPipelineLayoutDescriptor desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->setup_mip_layout
    };
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(r->device, &desc);
    WGPUTextureFormat format = fx->setup_format == BOX_EFFECT_TEXTURE_RG16 ?
        WGPUTextureFormat_RG16Float : WGPUTextureFormat_RGBA16Float;
    r->setup_mip_pipeline[0] = box_effect_mip_pipeline(r, module, layout,
        "fs_first", NULL, format);
    r->setup_mip_pipeline[1] = box_effect_mip_pipeline(r, module, layout,
        "fs_down", NULL, format);
    r->setup_mip_pipeline[2] = box_effect_mip_pipeline(r, module, layout,
        "fs_up", &box_effect_mip_mix, format);
    r->setup_mip_pipeline[3] = box_effect_mip_pipeline(r, module, layout,
        "fs_history", NULL, format);
    wgpuPipelineLayoutRelease(layout);
    wgpuShaderModuleRelease(module);

    result = r->setup_mip_pipeline[0] && r->setup_mip_pipeline[1] &&
        r->setup_mip_pipeline[2] && r->setup_mip_pipeline[3];
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
        if (v->setup_mip_down[level]) wgpuBindGroupRelease(v->setup_mip_down[level]);
        if (v->setup_mip_up[level]) wgpuBindGroupRelease(v->setup_mip_up[level]);
        v->setup_mip_mips[level] = NULL;
        v->setup_mip_down[level] = NULL;
        v->setup_mip_up[level] = NULL;
    }

    if (v->setup_mip_view) wgpuTextureViewRelease(v->setup_mip_view);
    if (v->setup_mip) wgpuTextureRelease(v->setup_mip);
    v->setup_mip_view = NULL;
    v->setup_mip = NULL;
    v->setup_mip_uniform = NULL;
    v->setup_mip_levels = 0;
    v->setup_mip_width = 0;
    v->setup_mip_height = 0;
}

/** Create a bind group of a pyramid pass.
 *
 * @param r The renderer
 * @param source The image the pass samples
 * @param history The history the pass blends with, NULL binds the source again
 * @return The bind group
 */
static WGPUBindGroup box_effect_mip_group(
    box_renderer_t *r,
    WGPUTextureView source,
    WGPUTextureView history)
{
    WGPUBindGroupEntry entries[4] = {
        { .binding = 0, .buffer = r->effect_uniform, .size = sizeof(box_effect_uniform_t) },
        { .binding = 1, .textureView = source },
        { .binding = 2, .sampler = r->effect_sampler },
        { .binding = 3, .textureView = history ? history : source }
    };
    WGPUBindGroupDescriptor desc = {
        .layout = r->setup_mip_layout,
        .entryCount = 4,
        .entries = entries
    };

    return wgpuDeviceCreateBindGroup(r->device, &desc);
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
    r->v->setup_mip_width = width;
    r->v->setup_mip_height = height;
    r->v->setup_mip_levels = levels;
    r->v->setup_mip_format = format;
    r->v->setup_mip_uniform = r->effect_uniform;

    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { r->v->setup_mip_width, r->v->setup_mip_height, 1 },
        .format = format,
        .mipLevelCount = levels,
        .sampleCount = 1
    };
    r->v->setup_mip = wgpuDeviceCreateTexture(r->device, &desc);

    uint32_t level;
    for (level = 0; level < levels; level ++) {
        WGPUTextureViewDescriptor view_desc = {
            .format = format,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = level,
            .mipLevelCount = 1,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_All
        };
        r->v->setup_mip_mips[level] = wgpuTextureCreateView(r->v->setup_mip, &view_desc);
    }

    r->v->setup_mip_view = wgpuTextureCreateView(r->v->setup_mip, &(WGPUTextureViewDescriptor){
        .format = format,
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    });

    for (level = 1; level < levels; level ++) {
        r->v->setup_mip_down[level] =
            box_effect_mip_group(r, r->v->setup_mip_mips[level - 1], NULL);
        r->v->setup_mip_up[level - 1] =
            box_effect_mip_group(r, r->v->setup_mip_mips[level], NULL);
    }
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

/** Blend the finest level with the history and write it to the other history texture.
 *
 * @param r The renderer
 * @param encoder The command encoder
 * @param slot Index of the pass in the packet, which is its uniform slot
 * @param frame Frame number, the history only counts when it holds the previous frame
 * @return The view of the history texture this frame wrote
 */
static WGPUTextureView box_effect_mip_history(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    uint32_t slot,
    uint32_t frame)
{
    box_gpu_view_t *v = r->v;
    if (!v->setup_mip_history[0]) {
        WGPUTextureDescriptor desc = {
            .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
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
    WGPUBindGroup group = box_effect_mip_group(r, v->setup_mip_mips[0],
        valid ? v->setup_mip_history_view[v->setup_mip_history_index] : v->setup_mip_mips[0]);
    box_effect_mip_pass(r, encoder, r->setup_mip_pipeline[3], group,
        v->setup_mip_history_view[next], slot, false, 0);
    wgpuBindGroupRelease(group);
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
    if (!src || !box_effect_mip_ready(r, fx)) {
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
    WGPUTextureFormat format = fx->setup_format == BOX_EFFECT_TEXTURE_RG16 ?
        WGPUTextureFormat_RG16Float : WGPUTextureFormat_RGBA16Float;
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
    WGPUBindGroup first = box_effect_mip_group(r, src->view, NULL);
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

    wgpuBindGroupRelease(first);
    if (history_param >= 0 && history_param < BOX_EFFECT_PARAM_MAX &&
        fx->params[history_param] > 0)
    {
        result = box_effect_mip_history(r, encoder, slot, frame);
    } else {
        box_effect_mip_history_fini(r->v);
        result = r->v->setup_mip_view;
    }

done:
    return result;
}
