/**
 * @file render/gpu_compose.c
 * @brief Compose pass of the webgpu backend.
 *
 * The slots of a mover page hold their transforms relative to their movers. Every frame the
 * engine compute shader etc/shaders/compose.wgsl multiplies them with the mover table into a
 * buffer of world transforms, which every pass binds in place of the transform stream.
 *
 * The frame records of the slots that move with their mover are placed the same way, so a
 * moving mover uploads one transform and not one per slot.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Slots one workgroup of the compose shader places */
#define BOX_GPU_COMPOSE_GROUP (64)

/** Largest workgroup count of one dispatch dimension */
#define BOX_GPU_COMPOSE_GROUPS_MAX (65535)

/** Create a storage buffer the compose pass writes.
 *
 * @param gpu The backend
 * @param size Bytes of the buffer
 * @return The buffer
 */
static WGPUBuffer box_gpu_compose_buffer(
    box_gpu_t *gpu,
    uint64_t size)
{
    return wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Storage,
        .size = size
    });
}

void box_gpu_compose_init(
    box_gpu_t *gpu)
{
    WGPUBufferBindingType types[6] = {
        WGPUBufferBindingType_ReadOnlyStorage, WGPUBufferBindingType_ReadOnlyStorage,
        WGPUBufferBindingType_ReadOnlyStorage, WGPUBufferBindingType_Storage,
        WGPUBufferBindingType_ReadOnlyStorage, WGPUBufferBindingType_Storage
    };
    WGPUBindGroupLayoutEntry entries[6];
    int32_t i;
    for (i = 0; i < 6; i ++) {
        entries[i] = (WGPUBindGroupLayoutEntry){
            .binding = (uint32_t)i,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = types[i] }
        };
    }

    gpu->compose_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 6, .entries = entries });
    gpu->compose_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &gpu->compose_layout
        });
    gpu->compose_sink = box_gpu_compose_buffer(gpu, sizeof(box_render_transform_t));
}

void box_gpu_compose_fini(
    box_gpu_t *gpu)
{
    if (gpu->compose) {
        wgpuComputePipelineRelease(gpu->compose);
        gpu->compose = NULL;
    }

    if (gpu->compose_pipeline_layout) {
        wgpuPipelineLayoutRelease(gpu->compose_pipeline_layout);
        gpu->compose_pipeline_layout = NULL;
    }

    if (gpu->compose_layout) {
        wgpuBindGroupLayoutRelease(gpu->compose_layout);
        gpu->compose_layout = NULL;
    }

    if (gpu->compose_sink) {
        wgpuBufferRelease(gpu->compose_sink);
        gpu->compose_sink = NULL;
    }
}

/** Get the compose pipeline, created again when the compose shader was recompiled; the previous
 * one stays until the new one exists.
 *
 * @param gpu The backend
 * @return The pipeline, NULL while the shader has no module or the first one is created
 */
static WGPUComputePipeline box_gpu_compose_pipeline(
    box_gpu_t *gpu)
{
    box_gpu_shader_t *s = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_COMPOSE);
    if (!s || !s->module) {
        goto done;
    }

    if (gpu->compose && gpu->compose_revision == s->revision) {
        goto done;
    }

    WGPUComputePipelineDescriptor desc = {
        .layout = gpu->compose_pipeline_layout,
        .compute = {
            .module = s->module,
            .entryPoint = { .data = "main", .length = WGPU_STRLEN }
        }
    };
    WGPUComputePipeline *current = &gpu->compose;
    WGPUComputePipeline next = box_gpu_compute_pipeline(gpu, &desc);
    if (box_gpu_compute_pipelines_swap(&current, &next, 1)) {
        gpu->compose_revision = s->revision;
    }

done:
    return s && s->module ? gpu->compose : NULL;
}

/** Make the buffer of the world records of a stream of a mover page, the size of the stream.
 *
 * @param gpu The backend
 * @param stream The stream
 * @param page The transform stream of the page, whose bind groups bind the world records
 */
static void box_gpu_compose_target(
    box_gpu_t *gpu,
    box_gpu_buffer_t *stream,
    box_gpu_buffer_t *page)
{
    if (stream->composed) {
        goto done;
    }

    stream->composed = box_gpu_compose_buffer(gpu, stream->size);
    box_gpu_instance_groups_release(page);
done:
    return;
}

/** Record the dispatch of one compose.
 *
 * @param gpu The backend
 * @param pass The compute pass
 * @param movers The mover table
 * @param c The compose
 */
static void box_gpu_compose_one(
    box_gpu_t *gpu,
    WGPUComputePassEncoder pass,
    const box_gpu_buffer_t *movers,
    const box_render_compose_t *c)
{
    uint32_t kind = box_render_page_kind(c->page);
    box_gpu_buffer_t *locals = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, c->page);
    box_gpu_buffer_t *ids = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t,
        c->page + BOX_RENDER_STREAM_MOVER);
    box_gpu_buffer_t *frames = (kind & BOX_RENDER_PAGE_FRAME) ? ecs_map_get_deref(
        &gpu->buffers, box_gpu_buffer_t, c->page + BOX_RENDER_STREAM_FRAME) : NULL;
    uint32_t groups = (c->count + BOX_GPU_COMPOSE_GROUP - 1) / BOX_GPU_COMPOSE_GROUP;
    if (!(kind & BOX_RENDER_PAGE_MOVER) || !locals || !ids || !c->count ||
        ((kind & BOX_RENDER_PAGE_FRAME) && !frames) || groups > BOX_GPU_COMPOSE_GROUPS_MAX)
    {
        ecs_err("compose of %u slots of page %u is invalid", c->count, c->page);
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        goto done;
    }

    box_gpu_compose_target(gpu, locals, locals);
    if (frames) {
        box_gpu_compose_target(gpu, frames, locals);
    }

    WGPUBindGroupEntry entries[6] = {
        { .binding = 0, .buffer = movers->buffer, .size = movers->size },
        { .binding = 1, .buffer = ids->buffer, .size = ids->size },
        { .binding = 2, .buffer = locals->buffer, .size = locals->size },
        { .binding = 3, .buffer = locals->composed, .size = locals->size },
        {
            .binding = 4,
            .buffer = frames ? frames->buffer : gpu->no_frames,
            .size = frames ? frames->size : sizeof(box_render_transform_t)
        },
        {
            .binding = 5,
            .buffer = frames ? frames->composed : gpu->compose_sink,
            .size = frames ? frames->size : sizeof(box_render_transform_t)
        }
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = gpu->compose_layout,
        .entryCount = 6,
        .entries = entries
    });
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, groups, 1, 1);
    wgpuBindGroupRelease(group);
done:
    return;
}

void box_gpu_compose(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i, count = ecs_vec_count(&packet->composes);
    const box_gpu_buffer_t *movers = ecs_map_get_deref(
        &gpu->buffers, box_gpu_buffer_t, BOX_RENDER_BUFFER_MOVERS);
    WGPUComputePipeline pipeline;
    if (!count || !movers) {
        goto done;
    }

    pipeline = box_gpu_compose_pipeline(gpu);
    if (!pipeline) {
        goto done;
    }

    WGPUCommandEncoder encoder = box_gpu_frame_encoder(gpu);
    WGPUComputePassEncoder pass = box_gpu_profile_compute_pass(gpu, encoder, "compose");
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    for (i = 0; i < count; i ++) {
        box_gpu_compose_one(gpu, pass, movers,
            ecs_vec_get_t(&packet->composes, box_render_compose_t, i));
    }

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
done:
    return;
}

#endif
