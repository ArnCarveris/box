/**
 * @file renderer/exposure.c
 * @brief Log luminance histogram of a view and the exposure it adapts to.
 *
 * One compute pass bins every texel of the image a pass reads into a histogram
 * and a second one takes its weighted average and moves the exposure of the
 * view towards it, at most as fast as the speeds of the component allow. The
 * exposure lives in a buffer of the view, so it carries over between frames and
 * the passes of the chain read it without the CPU ever waiting for it.
 */

#include "renderer_internal.h"

/** Create the compute pipeline of one entry point of the exposure shader.
 *
 * @param r The renderer
 * @param module The module holding both entry points
 * @param layout The pipeline layout
 * @param entry Name of the entry point
 * @return The pipeline
 */
static WGPUComputePipeline box_exposure_pipeline(
    box_renderer_t *r,
    WGPUShaderModule module,
    WGPUPipelineLayout layout,
    const char *entry)
{
    WGPUComputePipelineDescriptor desc = {
        .layout = layout,
        .compute = {
            .module = module,
            .entryPoint = { .data = entry, .length = WGPU_STRLEN }
        }
    };

    return wgpuDeviceCreateComputePipeline(r->device, &desc);
}

void box_exposure_init(
    box_renderer_t *r)
{
    WGPUBindGroupLayoutEntry entries[4] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = true,
                .minBindingSize = sizeof(box_effect_uniform_t)
            }
        }, {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }, {
            .binding = 2,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        }, {
            .binding = 3,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        }
    };
    WGPUBindGroupLayoutDescriptor layout_desc = { .entryCount = 4, .entries = entries };
    r->exposure_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->exposure_layout
    };
    WGPUPipelineLayout layout =
        wgpuDeviceCreatePipelineLayout(r->device, &pipeline_layout_desc);
    WGPUShaderModule module = box_shader_module(r, BOX_SHADER_EXPOSURE);
    if (module) {
        r->exposure_histogram = box_exposure_pipeline(r, module, layout, "cs_histogram");
        r->exposure_average = box_exposure_pipeline(r, module, layout, "cs_average");
        wgpuShaderModuleRelease(module);
    }

    wgpuPipelineLayoutRelease(layout);
}

void box_exposure_fini(
    box_renderer_t *r)
{
    if (r->exposure_histogram) wgpuComputePipelineRelease(r->exposure_histogram);
    if (r->exposure_average) wgpuComputePipelineRelease(r->exposure_average);
    if (r->exposure_layout) wgpuBindGroupLayoutRelease(r->exposure_layout);
    r->exposure_histogram = NULL;
    r->exposure_average = NULL;
    r->exposure_layout = NULL;
}

void box_exposure_view_fini(
    box_gpu_view_t *v)
{
    if (v->exposure_state) wgpuBufferRelease(v->exposure_state);
    if (v->exposure_hist) wgpuBufferRelease(v->exposure_hist);
    v->exposure_state = NULL;
    v->exposure_hist = NULL;
}

WGPUBuffer box_exposure_buffer(
    box_renderer_t *r)
{
    if (r->v->exposure_state) {
        goto done;
    }

    WGPUBufferDescriptor desc = {
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = sizeof(box_exposure_state_t)
    };
    r->v->exposure_state = wgpuDeviceCreateBuffer(r->device, &desc);

    box_exposure_state_t state = { .ev = 0.0f, .scale = 1.0f, .average = 0.0f, .pad = 0.0f };
    wgpuQueueWriteBuffer(r->gpu_queue, r->v->exposure_state, 0, &state, sizeof(state));
done:
    return r->v->exposure_state;
}

/** Buffer of the histogram of the current view, created and zeroed on demand.
 *
 * @param r The renderer
 * @return The buffer
 */
static WGPUBuffer box_exposure_histogram(
    box_renderer_t *r)
{
    if (r->v->exposure_hist) {
        goto done;
    }

    uint32_t zero[BOX_EXPOSURE_BINS] = {0};
    WGPUBufferDescriptor desc = {
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = sizeof(zero)
    };
    r->v->exposure_hist = wgpuDeviceCreateBuffer(r->device, &desc);
    wgpuQueueWriteBuffer(r->gpu_queue, r->v->exposure_hist, 0, zero, sizeof(zero));
done:
    return r->v->exposure_hist;
}

void box_exposure_dispatch(
    box_renderer_t *r,
    uint32_t slot,
    box_gpu_pool_t *src,
    WGPUCommandEncoder encoder)
{
    if (!r->exposure_histogram || !r->exposure_average || !src) {
        goto done;
    }

    WGPUBuffer state = box_exposure_buffer(r);
    WGPUBuffer histogram = box_exposure_histogram(r);
    WGPUBindGroupEntry entries[4] = {
        { .binding = 0, .buffer = r->effect_uniform, .size = sizeof(box_effect_uniform_t) },
        { .binding = 1, .textureView = src->view },
        { .binding = 2, .buffer = histogram, .size = BOX_EXPOSURE_BINS * sizeof(uint32_t) },
        { .binding = 3, .buffer = state, .size = sizeof(box_exposure_state_t) }
    };
    WGPUBindGroupDescriptor group_desc = {
        .layout = r->exposure_layout,
        .entryCount = 4,
        .entries = entries
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(r->device, &group_desc);

    uint32_t offset = slot * BOX_EFFECT_UNIFORM_STRIDE;
    uint32_t groups_x = (src->width + BOX_EXPOSURE_GROUP - 1) / BOX_EXPOSURE_GROUP;
    uint32_t groups_y = (src->height + BOX_EXPOSURE_GROUP - 1) / BOX_EXPOSURE_GROUP;
    WGPUComputePassEncoder pass = box_timing_compute(r, encoder, "exposure");
    wgpuComputePassEncoderSetPipeline(pass, r->exposure_histogram);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 1, &offset);
    wgpuComputePassEncoderDispatchWorkgroups(pass, groups_x, groups_y, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);

    WGPUComputePassEncoder adapt = box_timing_compute(r, encoder, "exposure");
    wgpuComputePassEncoderSetPipeline(adapt, r->exposure_average);
    wgpuComputePassEncoderSetBindGroup(adapt, 0, group, 1, &offset);
    wgpuComputePassEncoderDispatchWorkgroups(adapt, 1, 1, 1);
    wgpuComputePassEncoderEnd(adapt);
    wgpuComputePassEncoderRelease(adapt);
    wgpuBindGroupRelease(group);
done:
    return;
}
