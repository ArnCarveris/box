/**
 * @file render/gpu_volume.c
 * @brief Irradiance probe volumes of the webgpu backend.
 *
 * A volume is a uniform with its grid, two storage buffers with the SH of every probe and one
 * with the validity mask, bound as group 3 of the forward pass with the probe buffer the view
 * shows. Views without a volume bind a 1x1x1 volume that keeps the hemisphere.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Create a buffer that is written from the CPU.
 *
 * @param gpu The backend
 * @param size Bytes of the buffer
 * @param usage Usage of the buffer
 * @return The buffer
 */
static WGPUBuffer box_gpu_volume_buffer(
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

void box_gpu_volume_free(
    box_gpu_volume_t *v)
{
    int32_t i;
    for (i = 0; i < 2; i ++) {
        wgpuBindGroupRelease(v->group[i]);
        wgpuBufferRelease(v->probes[i]);
    }

    wgpuBufferRelease(v->grid);
    wgpuBufferRelease(v->valid);
    ecs_os_free(v);
}

/** Create a volume with its buffers and a bind group per probe buffer.
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
    int32_t i;
    v->count = count;
    v->grid = box_gpu_volume_buffer(gpu, sizeof(box_gpu_volume_uniforms_t),
        WGPUBufferUsage_Uniform);
    v->valid = box_gpu_volume_buffer(gpu, valid, WGPUBufferUsage_Storage);
    for (i = 0; i < 2; i ++) {
        v->probes[i] = box_gpu_volume_buffer(gpu, probes,
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
        WGPUBindGroupEntry entries[3] = {
            { .binding = 0, .buffer = v->grid, .size = sizeof(box_gpu_volume_uniforms_t) },
            { .binding = 1, .buffer = v->probes[i], .size = probes },
            { .binding = 2, .buffer = v->valid, .size = valid }
        };
        WGPUBindGroupDescriptor desc = {
            .layout = gpu->volume_layout,
            .entryCount = 3,
            .entries = entries
        };
        v->group[i] = wgpuDeviceCreateBindGroup(gpu->device, &desc);
    }

    return v;
}

/** Write the grid of a volume, fill every probe of both buffers with one SH and write its
 * validity mask.
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
    wgpuQueueWriteBuffer(gpu->queue, v->probes[0], 0, probes, size);
    wgpuQueueWriteBuffer(gpu->queue, v->probes[1], 0, probes, size);
    wgpuQueueWriteBuffer(gpu->queue, v->valid, 0, desc + 1,
        (size_t)(v->count + 31) / 32 * sizeof(uint32_t));
    ecs_os_free(probes);
}

/** Write the validity mask of a volume, after copying one probe buffer into the other for a
 * BOX_RENDER_VOLUME_COPY; the probes keep their values otherwise.
 *
 * @param gpu The backend
 * @param v The volume
 * @param desc The op, followed by the mask
 */
static void box_gpu_volume_keep(
    box_gpu_t *gpu,
    box_gpu_volume_t *v,
    const box_render_volume_desc_t *desc)
{
    if (desc->op == BOX_RENDER_VOLUME_COPY && desc->from < 2) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, v->probes[desc->from], 0,
            v->probes[1 - desc->from], 0, (uint64_t)v->count * BOX_GPU_PROBE_SIZE);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuQueueSubmit(gpu->queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
    }

    wgpuQueueWriteBuffer(gpu->queue, v->valid, 0, desc + 1,
        (size_t)(v->count + 31) / 32 * sizeof(uint32_t));
}

/** Copy the probes of both buffers of a volume that a resized grid keeps into the volume of
 * that grid, one run of x per row the two grids share.
 *
 * @param gpu The backend
 * @param from The volume of the old grid
 * @param to The volume of the new grid, already filled
 * @param desc The resize: new size, old size and the shift from new to old indices
 */
static void box_gpu_volume_carry(
    box_gpu_t *gpu,
    const box_gpu_volume_t *from,
    box_gpu_volume_t *to,
    const box_render_volume_desc_t *desc)
{
    const uint32_t *n = desc->size, *o = desc->old_size;
    const int32_t *d = desc->shift;
    int32_t x0 = glm_max(0, -d[0]), x1 = glm_min((int32_t)n[0], (int32_t)o[0] - d[0]);
    int32_t y, z, i;
    uint64_t probes = (uint64_t)o[0] * o[1] * o[2];
    if (x1 <= x0 || probes != from->count) {
        goto done;
    }

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    for (z = 0; z < (int32_t)n[2]; z ++) {
        for (y = 0; y < (int32_t)n[1]; y ++) {
            int32_t oy = y + d[1], oz = z + d[2];
            if (oy < 0 || oz < 0 || oy >= (int32_t)o[1] || oz >= (int32_t)o[2]) {
                continue;
            }

            uint64_t src = (uint64_t)(x0 + d[0]) + ((uint64_t)oy + (uint64_t)oz * o[1]) * o[0];
            uint64_t dst = (uint64_t)x0 + ((uint64_t)y + (uint64_t)z * n[1]) * n[0];
            for (i = 0; i < 2; i ++) {
                wgpuCommandEncoderCopyBufferToBuffer(encoder, from->probes[i],
                    src * BOX_GPU_PROBE_SIZE, to->probes[i], dst * BOX_GPU_PROBE_SIZE,
                    (uint64_t)(x1 - x0) * BOX_GPU_PROBE_SIZE);
            }
        }
    }

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
done:
    return;
}

void box_gpu_volume_init(
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

void box_gpu_volume_fini(
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

void box_gpu_volume_apply(
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
    bool resize = valid && v && op == BOX_RENDER_UPDATE && desc->op == BOX_RENDER_VOLUME_RESIZE;
    if (resize) {
        box_gpu_volume_t *grown = box_gpu_volume_new(gpu, desc->probes);
        box_gpu_volume_write(gpu, grown, desc);
        box_gpu_volume_carry(gpu, v, grown, desc);
        box_gpu_volume_free(v);
        ecs_map_remove(&gpu->volumes, handle);
        ecs_map_insert_ptr(&gpu->volumes, handle, grown);
        goto done;
    }

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
        box_gpu_volume_write(gpu, v, desc);
    } else if (op == BOX_RENDER_CREATE || desc->op == BOX_RENDER_VOLUME_FILL) {
        box_gpu_volume_write(gpu, v, desc);
    } else {
        box_gpu_volume_keep(gpu, v, desc);
    }

done:
    return;
}

WGPUBindGroup box_gpu_volume_group(
    box_gpu_t *gpu,
    const box_render_view_t *v)
{
    box_gpu_volume_t *volume = v->volume
        ? ecs_map_get_deref(&gpu->volumes, box_gpu_volume_t, v->volume)
        : NULL;

    return volume ? volume->group[v->volume_buffer & 1u] : gpu->no_volume->group[0];
}

#endif
