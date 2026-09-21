#include "renderer_internal.h"

#define BOX_BLOOM_FORMAT (WGPUTextureFormat_RGBA16Float)

static const WGPUBlendState box_bloom_mix = {
    .color = { WGPUBlendOperation_Add, WGPUBlendFactor_Constant,
        WGPUBlendFactor_One },
    .alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_Zero, WGPUBlendFactor_One }
};

static WGPURenderPipeline box_bloom_pipeline(
    box_renderer_t *r,
    WGPUShaderModule module,
    WGPUPipelineLayout layout,
    const char *entry,
    const WGPUBlendState *blend)
{
    WGPUColorTargetState target = {
        .format = BOX_BLOOM_FORMAT,
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

void box_bloom_init(
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
    r->pyramid_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->pyramid_layout
    };
    WGPUPipelineLayout layout =
        wgpuDeviceCreatePipelineLayout(r->device, &pipeline_layout_desc);
    WGPUShaderModule module = box_shader_module(r, BOX_SHADER_PYRAMID);
    if (module) {
        r->pyramid_first = box_bloom_pipeline(r, module, layout, "fs_first", NULL);
        r->pyramid_down = box_bloom_pipeline(r, module, layout, "fs_down", NULL);
        r->pyramid_up = box_bloom_pipeline(r, module, layout, "fs_up", &box_bloom_mix);
        r->pyramid_history = box_bloom_pipeline(r, module, layout, "fs_history", NULL);
        wgpuShaderModuleRelease(module);
    }

    wgpuPipelineLayoutRelease(layout);
}

void box_bloom_fini(
    box_renderer_t *r)
{
    if (r->pyramid_first) wgpuRenderPipelineRelease(r->pyramid_first);
    if (r->pyramid_down) wgpuRenderPipelineRelease(r->pyramid_down);
    if (r->pyramid_up) wgpuRenderPipelineRelease(r->pyramid_up);
    if (r->pyramid_history) wgpuRenderPipelineRelease(r->pyramid_history);
    if (r->pyramid_layout) wgpuBindGroupLayoutRelease(r->pyramid_layout);
    r->pyramid_first = NULL;
    r->pyramid_down = NULL;
    r->pyramid_up = NULL;
    r->pyramid_layout = NULL;
    r->pyramid_history = NULL;
}

static void box_bloom_history_fini(box_gpu_view_t *v)
{
    for (uint32_t i = 0; i < 2; i ++) {
        if (v->bloom_history_view[i]) wgpuTextureViewRelease(v->bloom_history_view[i]);
        if (v->bloom_history[i]) wgpuTextureRelease(v->bloom_history[i]);
        v->bloom_history_view[i] = NULL;
        v->bloom_history[i] = NULL;
    }
    v->bloom_history_valid = false;
    v->bloom_history_index = 0;
}

void box_bloom_view_fini(
    box_gpu_view_t *v)
{
    box_bloom_history_fini(v);
    uint32_t level;
    for (level = 0; level < BOX_BLOOM_LEVELS_MAX; level ++) {
        if (v->bloom_mips[level]) wgpuTextureViewRelease(v->bloom_mips[level]);
        if (v->bloom_down[level]) wgpuBindGroupRelease(v->bloom_down[level]);
        if (v->bloom_up[level]) wgpuBindGroupRelease(v->bloom_up[level]);
        v->bloom_mips[level] = NULL;
        v->bloom_down[level] = NULL;
        v->bloom_up[level] = NULL;
    }

    if (v->bloom_view) wgpuTextureViewRelease(v->bloom_view);
    if (v->bloom) wgpuTextureRelease(v->bloom);
    v->bloom_view = NULL;
    v->bloom = NULL;
    v->bloom_uniform = NULL;
    v->bloom_levels = 0;
    v->bloom_width = 0;
    v->bloom_height = 0;
}

static WGPUBindGroup box_bloom_group(
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
        .layout = r->pyramid_layout,
        .entryCount = 4,
        .entries = entries
    };

    return wgpuDeviceCreateBindGroup(r->device, &desc);
}

static void box_bloom_resize(
    box_renderer_t *r,
    uint32_t width,
    uint32_t height,
    uint32_t levels)
{
    box_bloom_view_fini(r->v);
    r->v->bloom_width = width;
    r->v->bloom_height = height;
    r->v->bloom_levels = levels;
    r->v->bloom_uniform = r->effect_uniform;

    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { r->v->bloom_width, r->v->bloom_height, 1 },
        .format = BOX_BLOOM_FORMAT,
        .mipLevelCount = levels,
        .sampleCount = 1
    };
    r->v->bloom = wgpuDeviceCreateTexture(r->device, &desc);

    uint32_t level;
    for (level = 0; level < levels; level ++) {
        WGPUTextureViewDescriptor view_desc = {
            .format = BOX_BLOOM_FORMAT,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = level,
            .mipLevelCount = 1,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_All
        };
        r->v->bloom_mips[level] = wgpuTextureCreateView(r->v->bloom, &view_desc);
    }

    r->v->bloom_view = wgpuTextureCreateView(r->v->bloom, &(WGPUTextureViewDescriptor){
        .format = BOX_BLOOM_FORMAT,
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    });

    for (level = 1; level < levels; level ++) {
        r->v->bloom_down[level] = box_bloom_group(r, r->v->bloom_mips[level - 1], NULL);
        r->v->bloom_up[level - 1] = box_bloom_group(r, r->v->bloom_mips[level], NULL);
    }
}

static void box_bloom_pass(
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
    WGPURenderPassEncoder pass = box_timing_render(r, encoder, &desc, "bloom");
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

static WGPUTextureView box_bloom_history(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    uint32_t slot,
    uint32_t frame)
{
    box_gpu_view_t *v = r->v;
    if (!v->bloom_history[0]) {
        WGPUTextureDescriptor desc = {
            .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
            .dimension = WGPUTextureDimension_2D,
            .size = { v->bloom_width, v->bloom_height, 1 },
            .format = BOX_BLOOM_FORMAT,
            .mipLevelCount = 1,
            .sampleCount = 1
        };
        for (uint32_t i = 0; i < 2; i ++) {
            v->bloom_history[i] = wgpuDeviceCreateTexture(r->device, &desc);
            v->bloom_history_view[i] = wgpuTextureCreateView(v->bloom_history[i], NULL);
        }
    }
    bool valid = v->bloom_history_valid && v->bloom_history_slot == slot &&
        v->bloom_history_frame + 1 == frame;
    uint32_t next = 1 - v->bloom_history_index;
    WGPUBindGroup group = box_bloom_group(r, v->bloom_mips[0],
        valid ? v->bloom_history_view[v->bloom_history_index] : v->bloom_mips[0]);
    box_bloom_pass(r, encoder, r->pyramid_history, group,
        v->bloom_history_view[next], slot, false, 0);
    wgpuBindGroupRelease(group);
    v->bloom_history_index = next;
    v->bloom_history_frame = frame;
    v->bloom_history_slot = slot;
    v->bloom_history_valid = true;
    return v->bloom_history_view[next];
}

WGPUTextureView box_bloom_build(
    box_renderer_t *r,
    const box_effect_t *fx,
    uint32_t slot,
    uint32_t frame,
    box_gpu_pool_t *src,
    WGPUCommandEncoder encoder)
{
    if (!r->pyramid_first || !r->pyramid_down || !r->pyramid_up ||
        !r->pyramid_history || !src)
    {
        return NULL;
    }
    if (fx->params[0] <= 0) {
        box_bloom_history_fini(r->v);
        return NULL;
    }
    uint32_t width, height;
    box_bloom_size(src->width, src->height, fx->params[6], fx->params[10] > 0.5f,
        &width, &height);
    uint32_t levels = box_bloom_chain(width, height);
    if (r->v->bloom_levels != levels || r->v->bloom_width != width ||
        r->v->bloom_height != height || r->v->bloom_uniform != r->effect_uniform)
    {
        box_bloom_resize(r, width, height, levels);
    }
    float first_step[2] = { 0.5f / (float)width, 0.5f / (float)height };
    wgpuQueueWriteBuffer(r->gpu_queue, r->effect_uniform,
        (uint64_t)slot * BOX_EFFECT_UNIFORM_STRIDE + 12 * sizeof(float),
        first_step, sizeof(first_step));
    WGPUBindGroup first = box_bloom_group(r, src->view, NULL);
    box_bloom_pass(r, encoder, r->pyramid_first, first, r->v->bloom_mips[0],
        slot, false, 0);
    for (uint32_t level = 1; level < levels; level ++) {
        box_bloom_pass(r, encoder, r->pyramid_down, r->v->bloom_down[level],
            r->v->bloom_mips[level], slot, false, 0);
    }
    for (uint32_t level = levels - 1; level > 0; level --) {
        box_bloom_pass(r, encoder, r->pyramid_up, r->v->bloom_up[level - 1],
            r->v->bloom_mips[level - 1], slot, true,
            box_bloom_blend(fx->params, level, levels));
    }
    wgpuBindGroupRelease(first);
    if (fx->params[8] > 0) {
        return box_bloom_history(r, encoder, slot, frame);
    }
    box_bloom_history_fini(r->v);
    return r->v->bloom_view;
}
