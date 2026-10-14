/**
 * @file render/gpu_bake.c
 * @brief Irradiance probe volumes of the webgpu backend, bound as group 3 of the forward pass.
 *
 * A volume is a uniform with its grid, a storage buffer with the SH of every probe and one
 * with the validity mask. Views without a volume bind a 1x1x1 volume that keeps the hemisphere.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Bytes of the SH of one probe */
#define BOX_GPU_PROBE_SIZE (BOX_RENDER_SH_COEFFICIENTS * 4 * sizeof(float))

/** Create a buffer that is written from the CPU.
 *
 * @param gpu The backend
 * @param size Bytes of the buffer
 * @param usage Usage of the buffer
 * @return The buffer
 */
static WGPUBuffer box_gpu_bake_buffer(
    box_gpu_t *gpu,
    uint64_t size,
    WGPUBufferUsage usage)
{
    WGPUBufferDescriptor desc = {
        .usage = usage | WGPUBufferUsage_CopyDst,
        .size = (size + 3) & ~(uint64_t)3
    };

    return wgpuDeviceCreateBuffer(gpu->device, &desc);
}

/** Release the buffers and bind group of a volume and the volume.
 *
 * @param v The volume
 */
static void box_gpu_volume_free(
    box_gpu_volume_t *v)
{
    wgpuBindGroupRelease(v->group);
    wgpuBufferRelease(v->grid);
    wgpuBufferRelease(v->probes);
    wgpuBufferRelease(v->valid);
    ecs_os_free(v);
}

/** Create a volume with its buffers and bind group.
 *
 * @param gpu The backend
 * @param count Probes of the volume
 * @return The volume
 */
static box_gpu_volume_t* box_gpu_volume_new(
    box_gpu_t *gpu,
    uint32_t count)
{
    box_gpu_volume_t *v = ecs_os_calloc_t(box_gpu_volume_t);
    uint64_t probes = (uint64_t)count * BOX_GPU_PROBE_SIZE;
    uint64_t valid = (uint64_t)(count + 31) / 32 * sizeof(uint32_t);
    v->count = count;
    v->grid = box_gpu_bake_buffer(gpu, sizeof(box_gpu_volume_uniforms_t),
        WGPUBufferUsage_Uniform);
    v->probes = box_gpu_bake_buffer(gpu, probes,
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    v->valid = box_gpu_bake_buffer(gpu, valid, WGPUBufferUsage_Storage);
    WGPUBindGroupEntry entries[3] = {
        { .binding = 0, .buffer = v->grid, .size = sizeof(box_gpu_volume_uniforms_t) },
        { .binding = 1, .buffer = v->probes, .size = probes },
        { .binding = 2, .buffer = v->valid, .size = valid }
    };
    WGPUBindGroupDescriptor desc = {
        .layout = gpu->volume_layout,
        .entryCount = 3,
        .entries = entries
    };
    v->group = wgpuDeviceCreateBindGroup(gpu->device, &desc);

    return v;
}

/** Write the grid of a volume, fill every probe with one SH and write its validity mask.
 *
 * @param gpu The backend
 * @param v The volume
 * @param desc The grid and the SH, followed by the mask
 */
static void box_gpu_volume_write(
    box_gpu_t *gpu,
    box_gpu_volume_t *v,
    const box_render_volume_desc_t *desc)
{
    box_gpu_volume_uniforms_t u = {
        .origin = { desc->origin[0], desc->origin[1], desc->origin[2], desc->spacing },
        .size = { desc->size[0], desc->size[1], desc->size[2], 1 }
    };
    size_t size = (size_t)v->count * BOX_GPU_PROBE_SIZE;
    uint8_t *probes = ecs_os_malloc((ecs_size_t)size);
    uint32_t i;
    for (i = 0; i < v->count; i ++) {
        ecs_os_memcpy(probes + i * BOX_GPU_PROBE_SIZE, desc->sh, BOX_GPU_PROBE_SIZE);
    }

    wgpuQueueWriteBuffer(gpu->queue, v->grid, 0, &u, sizeof(u));
    wgpuQueueWriteBuffer(gpu->queue, v->probes, 0, probes, size);
    wgpuQueueWriteBuffer(gpu->queue, v->valid, 0, desc + 1,
        (size_t)(v->count + 31) / 32 * sizeof(uint32_t));
    ecs_os_free(probes);
}

void box_gpu_bake_init(
    box_gpu_t *gpu)
{
    WGPUBindGroupLayoutEntry entries[3] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(box_gpu_volume_uniforms_t)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }
    };
    WGPUBindGroupLayoutDescriptor layout = { .entryCount = 3, .entries = entries };
    box_gpu_volume_uniforms_t none = { .size = { 1, 1, 1, 0 } };
    gpu->volume_layout = wgpuDeviceCreateBindGroupLayout(gpu->device, &layout);
    ecs_map_init(&gpu->volumes, NULL);
    gpu->no_volume = box_gpu_volume_new(gpu, 1);
    wgpuQueueWriteBuffer(gpu->queue, gpu->no_volume->grid, 0, &none, sizeof(none));
}

void box_gpu_bake_fini(
    box_gpu_t *gpu)
{
    ecs_map_iter_t it = ecs_map_iter(&gpu->volumes);
    while (ecs_map_next(&it)) {
        box_gpu_volume_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(&gpu->volumes);
    if (gpu->no_volume) {
        box_gpu_volume_free(gpu->no_volume);
        gpu->no_volume = NULL;
    }

    if (gpu->volume_layout) {
        wgpuBindGroupLayoutRelease(gpu->volume_layout);
        gpu->volume_layout = NULL;
    }
}

void box_gpu_bake_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_volume_desc_t *desc,
    uint32_t size)
{
    box_gpu_volume_t *v = ecs_map_get_deref(&gpu->volumes, box_gpu_volume_t, handle);
    uint64_t words = desc ? ((uint64_t)desc->probes + 31) / 32 : 0;
    bool valid = desc && desc->probes &&
        (uint64_t)desc->size[0] * desc->size[1] * desc->size[2] == desc->probes &&
        size >= sizeof(box_render_volume_desc_t) + words * sizeof(uint32_t);
    if (v && (op == BOX_RENDER_DESTROY || !valid || op == BOX_RENDER_CREATE ||
        v->count != desc->probes))
    {
        ecs_map_remove(&gpu->volumes, handle);
        box_gpu_volume_free(v);
        v = NULL;
    }

    if (op == BOX_RENDER_DESTROY) {
        goto done;
    }

    if (!valid) {
        ecs_err("probe volume %u has an invalid description", handle);
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        goto done;
    }

    if (!v) {
        v = box_gpu_volume_new(gpu, desc->probes);
        ecs_map_insert_ptr(&gpu->volumes, handle, v);
    }

    box_gpu_volume_write(gpu, v, desc);
done:
    return;
}

WGPUBindGroup box_gpu_bake_group(
    box_gpu_t *gpu,
    const box_render_view_t *v)
{
    box_gpu_volume_t *volume = v->volume
        ? ecs_map_get_deref(&gpu->volumes, box_gpu_volume_t, v->volume)
        : NULL;

    return volume ? volume->group : gpu->no_volume->group;
}

#endif
