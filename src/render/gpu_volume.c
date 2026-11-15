/**
 * @file render/gpu_volume.c
 * @brief Irradiance probe volumes of the webgpu backend.
 *
 * A volume is a uniform with its grid, two SH buffers, the validity mask and the visibility
 * maps, bound as group 3 of the forward pass with the probe buffer the view shows. Views
 * without a volume bind a 1x1x1 volume that keeps the hemisphere.
 */

#include "gpu_environment.h"

/** The zonal band 2 pattern y_m = sqrt(4 pi / 5) Y_2m(a) of an axis, m in the order of the
 * band 2 coefficients of mesh.wgsl.
 *
 * @param a The axis
 * @param out Receives y_m
 */
static void box_render_zh3_pattern(
    const float a[3],
    float out[5])
{
    out[0] = 1.7320508076f * a[0] * a[1];
    out[1] = 1.7320508076f * a[1] * a[2];
    out[2] = 0.5f * (3.0f * a[2] * a[2] - 1.0f);
    out[3] = 1.7320508076f * a[0] * a[2];
    out[4] = 0.8660254038f * (a[0] * a[0] - a[1] * a[1]);
}

void box_render_zh3_axis(
    const float band1[][4],
    float out[3])
{
    static const float w[3] = { 0.2126f, 0.7152f, 0.0722f };
    float a[3] = { 0 }, len;
    int32_t c;
    for (c = 0; c < 3; c ++) {
        a[0] += band1[2][c] * w[c];
        a[1] += band1[0][c] * w[c];
        a[2] += band1[1][c] * w[c];
    }

    len = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (len > BOX_RENDER_ZH3_AXIS_EPSILON) {
        out[0] = a[0] / len;
        out[1] = a[1] / len;
        out[2] = a[2] / len;
    } else {
        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = 1.0f;
    }
}

void box_render_sh_to_zh3(
    const float sh[BOX_RENDER_SH_COEFFICIENTS][4],
    float out[BOX_RENDER_ZH3_VECTORS][4])
{
    float a[3], y[5];
    int32_t k, c;
    box_render_zh3_axis(&sh[1], a);
    box_render_zh3_pattern(a, y);
    for (k = 0; k < BOX_RENDER_ZH3_VECTORS; k ++) {
        for (c = 0; c < 3; c ++) {
            out[k][c] = sh[k][c];
        }

        out[k][3] = 0.0f;
    }

    for (c = 0; c < 3; c ++) {
        float z = 0.0f;
        for (k = 0; k < 5; k ++) {
            z += sh[4 + k][c] * y[k];
        }

        out[c][3] = z;
    }
}

void box_render_zh3_to_sh(
    const float zh3[BOX_RENDER_ZH3_VECTORS][4],
    float out[BOX_RENDER_SH_COEFFICIENTS][4])
{
    float a[3], y[5];
    int32_t k, c;
    box_render_zh3_axis(&zh3[1], a);
    box_render_zh3_pattern(a, y);
    for (k = 0; k < BOX_RENDER_ZH3_VECTORS; k ++) {
        for (c = 0; c < 3; c ++) {
            out[k][c] = zh3[k][c];
        }

        out[k][3] = 0.0f;
    }

    for (k = 0; k < 5; k ++) {
        for (c = 0; c < 3; c ++) {
            out[4 + k][c] = zh3[c][3] * y[k];
        }

        out[4 + k][3] = 0.0f;
    }
}

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
    wgpuBufferRelease(v->visibility);
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
    uint64_t probes = (uint64_t)count * BOX_GPU_VOLUME_PROBE_SIZE;
    uint64_t valid = ((uint64_t)count + 3) & ~(uint64_t)3;
    uint64_t visibility = (uint64_t)count * BOX_GPU_VISIBILITY_SIZE;
    int32_t i;
    v->count = count;
    v->grid = box_gpu_volume_buffer(gpu, sizeof(box_gpu_volume_uniforms_t),
        WGPUBufferUsage_Uniform);
    v->valid = box_gpu_volume_buffer(gpu, valid, WGPUBufferUsage_Storage);
    v->visibility = box_gpu_volume_buffer(gpu, visibility,
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    for (i = 0; i < 2; i ++) {
        v->probes[i] = box_gpu_volume_buffer(gpu, probes,
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
        WGPUBindGroupEntry entries[4] = {
            { .binding = 0, .buffer = v->grid, .size = sizeof(box_gpu_volume_uniforms_t) },
            { .binding = 1, .buffer = v->probes[i], .size = probes },
            { .binding = 2, .buffer = v->valid, .size = valid },
            { .binding = 3, .buffer = v->visibility, .size = visibility }
        };
        WGPUBindGroupDescriptor desc = {
            .layout = gpu->volume_layout,
            .entryCount = 4,
            .entries = entries
        };
        v->group[i] = wgpuDeviceCreateBindGroup(gpu->device, &desc);
    }

    return v;
}

/** Write the validity of the cells of a volume, so the forward pass reads that of the eight
 * probes it blends with one load: a byte per cell, whose bit x + 2y + 4z is set when that corner
 * of the cell is a valid probe.
 *
 * A cell is a probe with a neighbour after it on every axis with more than one probe. The mask
 * of the probes follows the description, one bit per probe.
 *
 * @param gpu The backend
 * @param v The volume
 * @param desc The grid, followed by the mask of its probes
 */
static void box_gpu_volume_valid(
    box_gpu_t *gpu,
    box_gpu_volume_t *v,
    const box_render_volume_desc_t *desc)
{
    const uint32_t *bits = (const uint32_t*)(desc + 1);
    const uint32_t *n = desc->size;
    uint32_t c[3], x, y, z, k;
    size_t size = ((size_t)v->count + 3) & ~(size_t)3;
    uint8_t *cells = ecs_os_calloc((ecs_size_t)size);
    uint8_t *cell = cells;
    for (k = 0; k < 3; k ++) {
        c[k] = n[k] > 1 ? n[k] - 1 : 1;
    }

    for (z = 0; z < c[2]; z ++) {
        for (y = 0; y < c[1]; y ++) {
            for (x = 0; x < c[0]; x ++) {
                for (k = 0; k < 8; k ++) {
                    uint32_t px = n[0] > 1 ? x + (k & 1) : 0;
                    uint32_t py = n[1] > 1 ? y + ((k >> 1) & 1) : 0;
                    uint32_t pz = n[2] > 1 ? z + ((k >> 2) & 1) : 0;
                    uint32_t index = px + (py + pz * n[1]) * n[0];
                    *cell |= (uint8_t)(((bits[index >> 5] >> (index & 31)) & 1) << k);
                }

                cell ++;
            }
        }
    }

    wgpuQueueWriteBuffer(gpu->queue, v->valid, 0, cells, size);
    ecs_os_free(cells);
}

/** Fill every probe of both buffers of a volume with the SH of a processed environment, which
 * replaces the SH the probes were written with.
 *
 * The ZH3 the processing writes after the L2 SH of the environment is copied into the first
 * probe of each buffer, then each buffer copies the probes the
 * other one has filled after them, doubling the filled probes with every pair of copies.
 *
 * @param gpu The backend
 * @param v The volume, its buffers written
 * @param handle Handle of the environment, 0 for none
 */
static void box_gpu_volume_environment(
    box_gpu_t *gpu,
    box_gpu_volume_t *v,
    uint32_t handle)
{
    const box_gpu_environment_t *env = handle ? box_gpu_environment_get(gpu, handle) : NULL;
    uint64_t filled = 1;
    int32_t i;
    if (!env || !env->processed || !v->count) {
        goto done;
    }

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    for (i = 0; i < 2; i ++) {
        wgpuCommandEncoderCopyBufferToBuffer(encoder, env->sh, BOX_GPU_PROBE_SIZE,
            v->probes[i], 0, BOX_GPU_VOLUME_PROBE_SIZE);
    }

    while (filled < v->count) {
        uint64_t n = filled < v->count - filled ? filled : v->count - filled;
        for (i = 0; i < 2; i ++) {
            wgpuCommandEncoderCopyBufferToBuffer(encoder, v->probes[i], 0, v->probes[1 - i],
                filled * BOX_GPU_VOLUME_PROBE_SIZE, n * BOX_GPU_VOLUME_PROBE_SIZE);
        }

        filled += n;
    }

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
done:
    return;
}

/** Write the grid of a volume, fill every probe of both buffers with one SH, open its
 * visibility maps and write its validity mask.
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
    size_t size = (size_t)v->count * BOX_GPU_VOLUME_PROBE_SIZE;
    size_t texels = (size_t)v->count * BOX_GPU_VISIBILITY_TEXELS;
    uint8_t *probes = ecs_os_malloc((ecs_size_t)size);
    uint32_t *open = ecs_os_malloc_n(uint32_t, (ecs_size_t)texels);
    float zh3[BOX_RENDER_ZH3_VECTORS][4];
    uint32_t i;
    box_render_sh_to_zh3(desc->sh, zh3);
    for (i = 0; i < v->count; i ++) {
        ecs_os_memcpy(probes + i * BOX_GPU_VOLUME_PROBE_SIZE, zh3, BOX_GPU_VOLUME_PROBE_SIZE);
    }

    for (i = 0; i < texels; i ++) {
        open[i] = BOX_GPU_VISIBILITY_OPEN;
    }

    v->spacing = desc->spacing;

    wgpuQueueWriteBuffer(gpu->queue, v->grid, 0, &u, sizeof(u));
    wgpuQueueWriteBuffer(gpu->queue, v->probes[0], 0, probes, size);
    wgpuQueueWriteBuffer(gpu->queue, v->probes[1], 0, probes, size);
    wgpuQueueWriteBuffer(gpu->queue, v->visibility, 0, open, texels * sizeof(uint32_t));
    box_gpu_volume_environment(gpu, v, desc->environment);
    box_gpu_volume_valid(gpu, v, desc);
    ecs_os_free(open);
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
            v->probes[1 - desc->from], 0, (uint64_t)v->count * BOX_GPU_VOLUME_PROBE_SIZE);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuQueueSubmit(gpu->queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
    }

    box_gpu_volume_valid(gpu, v, desc);
}

/** Copy the probes of both buffers and the visibility maps of a volume that a resized grid
 * keeps into the volume of that grid, one run of x per row the two grids share.
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
                    src * BOX_GPU_VOLUME_PROBE_SIZE, to->probes[i],
                    dst * BOX_GPU_VOLUME_PROBE_SIZE,
                    (uint64_t)(x1 - x0) * BOX_GPU_VOLUME_PROBE_SIZE);
            }

            wgpuCommandEncoderCopyBufferToBuffer(encoder, from->visibility,
                src * BOX_GPU_VISIBILITY_SIZE, to->visibility, dst * BOX_GPU_VISIBILITY_SIZE,
                (uint64_t)(x1 - x0) * BOX_GPU_VISIBILITY_SIZE);
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
    WGPUBindGroupLayoutEntry entries[4] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(box_gpu_volume_uniforms_t)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = BOX_GPU_VOLUME_PROBE_SIZE
            }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        },
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }
        }
    };
    WGPUBindGroupLayoutDescriptor layout = { .entryCount = 4, .entries = entries };
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

bool box_render_gpu_read_volume_sh(
    ecs_world_t *world,
    uint32_t volume,
    uint32_t buffer,
    uint32_t probe,
    float out[BOX_RENDER_SH_COEFFICIENTS][4])
{
    bool result = false;
    float zh3[BOX_RENDER_ZH3_VECTORS][4];
    box_render_gpu_stats_t stats;
    if (!box_render_gpu_stats(world, &stats) || !stats.ready || buffer > 1) {
        goto done;
    }

    box_gpu_t *gpu = box_render_get_backend(world)->ctx;
    bool renders = ecs_os_thread_self() == gpu->thread;
    ecs_assert(renders, ECS_INVALID_OPERATION,
        "box_render_gpu_read_volume_sh needs RenderSettings.main_thread and box_render_sync");
    const box_gpu_volume_t *v = renders
        ? ecs_map_get_deref(&gpu->volumes, box_gpu_volume_t, volume)
        : NULL;
    if (!v || probe >= v->count) {
        goto done;
    }

    result = box_gpu_read(gpu, v->probes[buffer], probe * (uint32_t)BOX_GPU_VOLUME_PROBE_SIZE,
        (uint32_t)BOX_GPU_VOLUME_PROBE_SIZE, zh3);
    if (result) {
        box_render_zh3_to_sh(zh3, out);
    }

done:
    return result;
}

bool box_render_gpu_volume_bytes(
    ecs_world_t *world,
    uint32_t volume,
    uint64_t *out)
{
    bool result = false;
    box_render_gpu_stats_t stats;
    if (!box_render_gpu_stats(world, &stats) || !stats.ready) {
        goto done;
    }

    box_gpu_t *gpu = box_render_get_backend(world)->ctx;
    bool renders = ecs_os_thread_self() == gpu->thread;
    ecs_assert(renders, ECS_INVALID_OPERATION,
        "box_render_gpu_volume_bytes needs RenderSettings.main_thread and box_render_sync");
    const box_gpu_volume_t *v = renders
        ? ecs_map_get_deref(&gpu->volumes, box_gpu_volume_t, volume)
        : NULL;
    if (!v) {
        goto done;
    }

    *out = wgpuBufferGetSize(v->probes[0]);
    result = true;
done:
    return result;
}

#else

bool box_render_gpu_volume_bytes(
    ecs_world_t *world,
    uint32_t volume,
    uint64_t *out)
{
    (void)world;
    (void)volume;
    (void)out;

    return false;
}

bool box_render_gpu_read_volume_sh(
    ecs_world_t *world,
    uint32_t volume,
    uint32_t buffer,
    uint32_t probe,
    float out[BOX_RENDER_SH_COEFFICIENTS][4])
{
    (void)world;
    (void)volume;
    (void)buffer;
    (void)probe;
    (void)out;

    return false;
}

#endif
