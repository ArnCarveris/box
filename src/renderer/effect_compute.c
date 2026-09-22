#include "renderer_internal.h"

static WGPUComputePipeline box_effect_compute_pipeline(
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

void box_effect_compute_init(
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
    r->setup_compute_layout = wgpuDeviceCreateBindGroupLayout(r->device, &layout_desc);

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->setup_compute_layout
    };
    WGPUPipelineLayout layout =
        wgpuDeviceCreatePipelineLayout(r->device, &pipeline_layout_desc);
    wgpuPipelineLayoutRelease(layout);
}

static bool box_effect_compute_ready(
    box_renderer_t *r,
    const box_effect_t *fx)
{
    if (r->setup_compute_shader == fx->setup_shader && r->setup_compute_first) {
        return true;
    }
    if (r->setup_compute_first) wgpuComputePipelineRelease(r->setup_compute_first);
    if (r->setup_compute_second) wgpuComputePipelineRelease(r->setup_compute_second);
    r->setup_compute_first = NULL;
    r->setup_compute_second = NULL;
    r->setup_compute_shader = fx->setup_shader;
    WGPUShaderModule module = box_shader_module(r, fx->setup_shader);
    if (!module) {
        return false;
    }
    WGPUPipelineLayoutDescriptor desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &r->setup_compute_layout
    };
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(r->device, &desc);
    r->setup_compute_first = box_effect_compute_pipeline(r, module, layout, "cs_first");
    r->setup_compute_second = box_effect_compute_pipeline(r, module, layout, "cs_second");
    wgpuPipelineLayoutRelease(layout);
    wgpuShaderModuleRelease(module);
    return r->setup_compute_first && r->setup_compute_second;
}

void box_effect_compute_fini(
    box_renderer_t *r)
{
    if (r->setup_compute_first) wgpuComputePipelineRelease(r->setup_compute_first);
    if (r->setup_compute_second) wgpuComputePipelineRelease(r->setup_compute_second);
    if (r->setup_compute_layout) wgpuBindGroupLayoutRelease(r->setup_compute_layout);
    r->setup_compute_first = NULL;
    r->setup_compute_second = NULL;
    r->setup_compute_layout = NULL;
    r->setup_compute_shader = BOX_SHADER_NONE;
}

void box_effect_compute_view_fini(
    box_gpu_view_t *v)
{
    if (v->setup_compute_state) wgpuBufferRelease(v->setup_compute_state);
    if (v->setup_compute_work) wgpuBufferRelease(v->setup_compute_work);
    v->setup_compute_state = NULL;
    v->setup_compute_work = NULL;
    v->setup_compute_size[0] = 0;
    v->setup_compute_size[1] = 0;
}

WGPUBuffer box_effect_storage(
    box_renderer_t *r,
    const box_effect_t *fx)
{
    uint32_t size = fx && fx->setup_buffer_size[1] ? fx->setup_buffer_size[1] : 16;
    if (r->v->setup_compute_state && r->v->setup_compute_size[1] == size) {
        goto done;
    }

    if (r->v->setup_compute_state) wgpuBufferRelease(r->v->setup_compute_state);

    WGPUBufferDescriptor desc = {
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = size
    };
    r->v->setup_compute_state = wgpuDeviceCreateBuffer(r->device, &desc);
    r->v->setup_compute_size[1] = size;
    float initial[4] = {0};
    if (fx) memcpy(initial, fx->setup_initial, sizeof(initial));
    wgpuQueueWriteBuffer(r->gpu_queue, r->v->setup_compute_state, 0, initial,
        size < sizeof(initial) ? size : sizeof(initial));
done:
    return r->v->setup_compute_state;
}

static WGPUBuffer box_effect_compute_work(
    box_renderer_t *r,
    const box_effect_t *fx)
{
    uint32_t size = fx->setup_buffer_size[0];
    if (!size) {
        return NULL;
    }
    if (r->v->setup_compute_work && r->v->setup_compute_size[0] == size) {
        goto done;
    }

    if (r->v->setup_compute_work) wgpuBufferRelease(r->v->setup_compute_work);
    WGPUBufferDescriptor desc = {
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = size
    };
    r->v->setup_compute_work = wgpuDeviceCreateBuffer(r->device, &desc);
    r->v->setup_compute_size[0] = size;
done:
    return r->v->setup_compute_work;
}

void box_effect_compute_run(
    box_renderer_t *r,
    const box_effect_t *fx,
    uint32_t slot,
    box_gpu_pool_t *src,
    WGPUCommandEncoder encoder)
{
    if (!src || !box_effect_compute_ready(r, fx)) {
        goto done;
    }

    WGPUBuffer state = box_effect_storage(r, fx);
    WGPUBuffer work = box_effect_compute_work(r, fx);
    if (!state || !work) {
        goto done;
    }
    WGPUBindGroupEntry entries[4] = {
        { .binding = 0, .buffer = r->effect_uniform, .size = sizeof(box_effect_uniform_t) },
        { .binding = 1, .textureView = src->view },
        { .binding = 2, .buffer = work, .size = fx->setup_buffer_size[0] },
        { .binding = 3, .buffer = state, .size = fx->setup_buffer_size[1] }
    };
    WGPUBindGroupDescriptor group_desc = {
        .layout = r->setup_compute_layout,
        .entryCount = 4,
        .entries = entries
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(r->device, &group_desc);

    uint32_t offset = slot * BOX_EFFECT_UNIFORM_STRIDE;
    uint32_t group_size = fx->setup_workgroup_size ? fx->setup_workgroup_size : 1;
    uint32_t groups_x = (src->width + group_size - 1) / group_size;
    uint32_t groups_y = (src->height + group_size - 1) / group_size;
    WGPUComputePassEncoder pass = box_timing_compute(r, encoder, "effect.setup");
    wgpuComputePassEncoderSetPipeline(pass, r->setup_compute_first);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 1, &offset);
    wgpuComputePassEncoderDispatchWorkgroups(pass, groups_x, groups_y, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);

    WGPUComputePassEncoder adapt = box_timing_compute(r, encoder, "effect.setup");
    wgpuComputePassEncoderSetPipeline(adapt, r->setup_compute_second);
    wgpuComputePassEncoderSetBindGroup(adapt, 0, group, 1, &offset);
    wgpuComputePassEncoderDispatchWorkgroups(adapt, 1, 1, 1);
    wgpuComputePassEncoderEnd(adapt);
    wgpuComputePassEncoderRelease(adapt);
    wgpuBindGroupRelease(group);
done:
    return;
}
