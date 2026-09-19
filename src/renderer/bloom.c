/**
 * @file renderer/bloom.c
 * @brief Progressive pyramid of the image a post process pass reads.
 *
 * The pyramid is the filter pair of the blurred background copy: a thirteen tap
 * downsample walks the levels and an eight tap tent upsample mixes them back
 * up, which is how a kernel that spans the image costs a handful of small
 * passes. The first downsample decodes the image, weights its taps by the Karis
 * average and clamps a firefly, so the levels hold linear light that a single
 * bright texel cannot take over.
 */

#include "renderer_internal.h"

/** Format every level of the pyramid holds, which keeps light past white */
#define BOX_BLOOM_FORMAT (WGPUTextureFormat_RGBA16Float)

/** Blend of an upsample pass: half the tent of the level below, half what is there */
static const WGPUBlendState box_bloom_mix = {
    .color = { WGPUBlendOperation_Add, WGPUBlendFactor_Constant,
        WGPUBlendFactor_OneMinusConstant },
    .alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_Zero }
};

/** Weight an upsample pass gives the tent of the level below it */
#define BOX_BLOOM_MIX_WEIGHT (0.5)

/** Create one full screen pipeline of the pyramid.
 *
 * @param r The renderer
 * @param module The module holding every entry point
 * @param layout The pipeline layout
 * @param entry Name of the fragment entry point
 * @param blend Blend state of the pass, NULL when it replaces its target
 * @return The pipeline
 */
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
    WGPUBindGroupLayoutEntry entries[3] = {
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
        }
    };
    WGPUBindGroupLayoutDescriptor layout_desc = { .entryCount = 3, .entries = entries };
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
    if (r->pyramid_layout) wgpuBindGroupLayoutRelease(r->pyramid_layout);
    r->pyramid_first = NULL;
    r->pyramid_down = NULL;
    r->pyramid_up = NULL;
    r->pyramid_layout = NULL;
}

void box_bloom_view_fini(
    box_gpu_view_t *v)
{
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
    v->bloom_levels = 0;
    v->bloom_width = 0;
    v->bloom_height = 0;
}

/** Create the bind group a pass of the pyramid reads a texture through.
 *
 * @param r The renderer
 * @param source The texture the pass samples
 * @return The bind group
 */
static WGPUBindGroup box_bloom_group(
    box_renderer_t *r,
    WGPUTextureView source)
{
    WGPUBindGroupEntry entries[3] = {
        { .binding = 0, .buffer = r->effect_uniform, .size = sizeof(box_effect_uniform_t) },
        { .binding = 1, .textureView = source },
        { .binding = 2, .sampler = r->effect_sampler }
    };
    WGPUBindGroupDescriptor desc = {
        .layout = r->pyramid_layout,
        .entryCount = 3,
        .entries = entries
    };

    return wgpuDeviceCreateBindGroup(r->device, &desc);
}

/** Create the pyramid of the current view for a size and a level count.
 *
 * @param r The renderer
 * @param width Width of the image the pyramid is built from
 * @param height Height of that image
 * @param levels Levels the pyramid holds
 */
static void box_bloom_resize(
    box_renderer_t *r,
    uint32_t width,
    uint32_t height,
    uint32_t levels)
{
    box_bloom_view_fini(r->v);
    r->v->bloom_width = width / 2 ? width / 2 : 1;
    r->v->bloom_height = height / 2 ? height / 2 : 1;
    r->v->bloom_levels = levels;

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
        r->v->bloom_down[level] = box_bloom_group(r, r->v->bloom_mips[level - 1]);
        r->v->bloom_up[level - 1] = box_bloom_group(r, r->v->bloom_mips[level]);
    }
}

/** Record one pass of the pyramid over one of its levels.
 *
 * @param r The renderer
 * @param encoder The command encoder
 * @param pipeline The pipeline of the pass
 * @param group The texture the pass samples
 * @param level The level the pass writes
 * @param slot Index of the effect pass, which is its uniform slot
 * @param mix The pass mixes into the level instead of replacing it
 */
static void box_bloom_pass(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    WGPURenderPipeline pipeline,
    WGPUBindGroup group,
    uint32_t level,
    uint32_t slot,
    bool mix)
{
    WGPURenderPassColorAttachment color = {
        .view = r->v->bloom_mips[level],
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = mix ? WGPULoadOp_Load : WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store
    };
    WGPURenderPassDescriptor desc = { .colorAttachmentCount = 1, .colorAttachments = &color };
    WGPURenderPassEncoder pass = box_timing_render(r, encoder, &desc, "bloom");
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    if (mix) {
        WGPUColor weight = { BOX_BLOOM_MIX_WEIGHT, BOX_BLOOM_MIX_WEIGHT, BOX_BLOOM_MIX_WEIGHT,
            BOX_BLOOM_MIX_WEIGHT };
        wgpuRenderPassEncoderSetBlendConstant(pass, &weight);
    }

    uint32_t offset = slot * BOX_EFFECT_UNIFORM_STRIDE;
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &offset);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

WGPUTextureView box_bloom_build(
    box_renderer_t *r,
    const box_effect_t *fx,
    uint32_t slot,
    box_gpu_pool_t *src,
    WGPUCommandEncoder encoder)
{
    WGPUTextureView result = NULL;
    if (!r->pyramid_first || !r->pyramid_down || !r->pyramid_up || !src) {
        goto done;
    }

    uint32_t levels = box_bloom_levels(src->width, src->height, fx->params[1]);
    uint32_t width = src->width / 2 ? src->width / 2 : 1;
    uint32_t height = src->height / 2 ? src->height / 2 : 1;
    if (r->v->bloom_levels != levels || r->v->bloom_width != width ||
        r->v->bloom_height != height)
    {
        box_bloom_resize(r, src->width, src->height, levels);
    }

    WGPUBindGroup first = box_bloom_group(r, src->view);
    box_bloom_pass(r, encoder, r->pyramid_first, first, 0, slot, false);

    uint32_t level;
    for (level = 1; level < levels; level ++) {
        box_bloom_pass(r, encoder, r->pyramid_down, r->v->bloom_down[level], level, slot,
            false);
    }

    for (level = levels - 1; level > 0; level --) {
        box_bloom_pass(r, encoder, r->pyramid_up, r->v->bloom_up[level - 1], level - 1, slot,
            true);
    }

    wgpuBindGroupRelease(first);
    result = r->v->bloom_view;
done:
    return result;
}
