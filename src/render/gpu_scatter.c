/**
 * @file render/gpu_scatter.c
 * @brief Scatter pass of the webgpu backend and buffer readback.
 *
 * A scatter copies the changed instances of a segment of the change buffer into their slots
 * of an instance buffer with the engine compute shader etc/shaders/scatter.wgsl, so a frame
 * only uploads the instances that changed.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Instances one workgroup of the scatter shader copies */
#define BOX_GPU_SCATTER_GROUP (64)

/** Largest workgroup count of one dispatch dimension */
#define BOX_GPU_SCATTER_GROUPS_MAX (65535)

/** Outcome of mapping a readback buffer */
typedef struct box_gpu_read_t {
    bool done;                  /**< The map callback ran */
    WGPUMapAsyncStatus status;  /**< Status the callback reported */
} box_gpu_read_t;

void box_gpu_scatter_init(
    box_gpu_t *gpu)
{
    WGPUBindGroupLayoutEntry entries[2] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        }
    };
    WGPUBindGroupLayoutDescriptor layout = { .entryCount = 2, .entries = entries };
    gpu->scatter_layout = wgpuDeviceCreateBindGroupLayout(gpu->device, &layout);
    gpu->scatter_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &gpu->scatter_layout
        });
}

void box_gpu_scatter_fini(
    box_gpu_t *gpu)
{
    if (gpu->scatter) {
        wgpuComputePipelineRelease(gpu->scatter);
        gpu->scatter = NULL;
    }

    if (gpu->scatter_pipeline_layout) {
        wgpuPipelineLayoutRelease(gpu->scatter_pipeline_layout);
        gpu->scatter_pipeline_layout = NULL;
    }

    if (gpu->scatter_layout) {
        wgpuBindGroupLayoutRelease(gpu->scatter_layout);
        gpu->scatter_layout = NULL;
    }
}

/** Get the scatter pipeline, created again when the scatter shader was recompiled.
 *
 * @param gpu The backend
 * @return The pipeline, NULL while the shader has no module
 */
static WGPUComputePipeline box_gpu_scatter_pipeline(
    box_gpu_t *gpu)
{
    box_gpu_shader_t *s = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_SCATTER);
    if (!s || !s->module) {
        goto done;
    }

    if (gpu->scatter && gpu->scatter_revision == s->revision) {
        goto done;
    }

    if (gpu->scatter) {
        wgpuComputePipelineRelease(gpu->scatter);
    }

    WGPUComputePipelineDescriptor desc = {
        .layout = gpu->scatter_pipeline_layout,
        .compute = {
            .module = s->module,
            .entryPoint = { .data = "main", .length = WGPU_STRLEN }
        }
    };
    gpu->scatter = wgpuDeviceCreateComputePipeline(gpu->device, &desc);
    gpu->scatter_revision = s->revision;
done:
    return s && s->module ? gpu->scatter : NULL;
}

/** Record the dispatch of one scatter.
 *
 * @param gpu The backend
 * @param pass The compute pass
 * @param sc The scatter
 * @return Instances the dispatch copies, 0 when the scatter was skipped
 */
static uint32_t box_gpu_scatter_one(
    box_gpu_t *gpu,
    WGPUComputePassEncoder pass,
    const box_render_scatter_t *sc)
{
    uint32_t result = 0;
    box_gpu_buffer_t *src = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, sc->changes);
    box_gpu_buffer_t *dst = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, sc->buffer);
    uint32_t groups = (sc->count + BOX_GPU_SCATTER_GROUP - 1) / BOX_GPU_SCATTER_GROUP;
    if (!src || !dst || !sc->count || (uint64_t)sc->offset + sc->size > src->size ||
        groups > BOX_GPU_SCATTER_GROUPS_MAX)
    {
        ecs_err("scatter of %u instances into buffer %u is invalid", sc->count, sc->buffer);
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        goto done;
    }

    WGPUBindGroupEntry entries[2] = {
        { .binding = 0, .buffer = src->buffer, .offset = sc->offset, .size = sc->size },
        { .binding = 1, .buffer = dst->buffer, .offset = 0, .size = dst->size }
    };
    WGPUBindGroupDescriptor desc = {
        .layout = gpu->scatter_layout,
        .entryCount = 2,
        .entries = entries
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &desc);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, groups, 1, 1);
    wgpuBindGroupRelease(group);
    dst->version = ++ gpu->buffer_writes;
    box_gpu_count(gpu, BOX_GPU_STAT_SCATTER);
    result = sc->count;
done:
    return result;
}

void box_gpu_scatter(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i, count = ecs_vec_count(&packet->scatters);
    uint32_t copied = 0;
    WGPUComputePipeline pipeline;
    if (!count) {
        goto done;
    }

    pipeline = box_gpu_scatter_pipeline(gpu);
    if (!pipeline) {
        ecs_err("scatter shader is not available, %d scatters skipped", count);
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        goto done;
    }

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    for (i = 0; i < count; i ++) {
        copied += box_gpu_scatter_one(gpu, pass,
            ecs_vec_get_t(&packet->scatters, box_render_scatter_t, i));
    }

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    gpu->pending.scattered += (int32_t)copied;
done:
    return;
}

/** Record the outcome of a map request.
 *
 * @param status Outcome of the request
 * @param message Error text
 * @param userdata1 The box_gpu_read_t to fill
 * @param userdata2 Unused
 */
static void box_gpu_on_read(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_read_t *read = userdata1;
    (void)message;
    (void)userdata2;
    read->status = status;
    read->done = true;
}

bool box_gpu_read(
    box_gpu_t *gpu,
    WGPUBuffer buffer,
    uint32_t offset,
    uint32_t size,
    void *out)
{
    box_gpu_read_t read = {0};
    WGPUBufferDescriptor desc = {
        .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
        .size = size
    };
    WGPUBuffer staging = wgpuDeviceCreateBuffer(gpu->device, &desc);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, buffer, offset, staging, 0, size);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuBufferMapAsync(staging, WGPUMapMode_Read, 0, size, (WGPUBufferMapCallbackInfo){
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = box_gpu_on_read,
        .userdata1 = &read
    });
    while (!read.done) {
        wgpuDevicePoll(gpu->device, true, NULL);
    }

    if (read.status == WGPUMapAsyncStatus_Success) {
        ecs_os_memcpy(out, wgpuBufferGetConstMappedRange(staging, 0, size), (ecs_size_t)size);
        wgpuBufferUnmap(staging);
    }

    wgpuBufferRelease(staging);

    return read.status == WGPUMapAsyncStatus_Success;
}

bool box_render_gpu_read_buffer(
    ecs_world_t *world,
    uint32_t handle,
    uint32_t offset,
    uint32_t size,
    void *out)
{
    bool result = false;
    box_render_gpu_stats_t stats;
    if (!box_render_gpu_stats(world, &stats) || !stats.ready || !size || (offset | size) & 3) {
        goto done;
    }

    box_gpu_t *gpu = box_render_get_backend(world)->ctx;
    bool renders = ecs_os_thread_self() == gpu->thread;
    ecs_assert(renders, ECS_INVALID_OPERATION,
        "box_render_gpu_read_buffer needs RenderSettings.main_thread and box_render_sync");
    if (!renders) {
        goto done;
    }

    box_gpu_buffer_t *b = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, handle);
    if (!b || (uint64_t)offset + size > b->size) {
        goto done;
    }

    result = box_gpu_read(gpu, b->buffer, offset, size, out);
done:
    return result;
}

#else

bool box_render_gpu_read_buffer(
    ecs_world_t *world,
    uint32_t handle,
    uint32_t offset,
    uint32_t size,
    void *out)
{
    (void)world;
    (void)handle;
    (void)offset;
    (void)size;
    (void)out;

    return false;
}

#endif
