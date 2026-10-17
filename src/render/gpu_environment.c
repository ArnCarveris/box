/**
 * @file render/gpu_environment.c
 * @brief Environment cubemaps of the webgpu backend: the target their sources write and the
 * processing that runs after the sources of a packet wrote new content.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Cubemap faces, each a layer of the texture */
#define BOX_GPU_ENVIRONMENT_FACES (6)

/** Bytes of a texel of BOX_GPU_ENVIRONMENT_FORMAT */
#define BOX_GPU_ENVIRONMENT_TEXEL (8)

/** Release a cubemap and its views.
 *
 * @param e The cubemap
 */
static void box_gpu_environment_free(
    box_gpu_environment_t *e)
{
    wgpuTextureViewRelease(e->target);
    wgpuTextureViewRelease(e->cube);
    wgpuTextureRelease(e->texture);
    ecs_os_free(e);
}

/** Create an empty cubemap with its views.
 *
 * @param gpu The backend
 * @param desc The resolution and filter
 * @return The cubemap
 */
static box_gpu_environment_t* box_gpu_environment_new(
    box_gpu_t *gpu,
    const box_render_environment_desc_t *desc)
{
    box_gpu_environment_t *e = ecs_os_calloc_t(box_gpu_environment_t);
    uint32_t res = desc->resolution;
    e->resolution = res;
    e->filter = desc->filter;
    for (e->mips = 1; (res >> e->mips) >= BOX_GPU_ENVIRONMENT_MIN_MIP; e->mips ++) { }
    e->texture = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding |
            WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = { res, res, BOX_GPU_ENVIRONMENT_FACES },
        .format = BOX_GPU_ENVIRONMENT_FORMAT,
        .mipLevelCount = e->mips,
        .sampleCount = 1
    });
    e->cube = wgpuTextureCreateView(e->texture, &(WGPUTextureViewDescriptor){
        .format = BOX_GPU_ENVIRONMENT_FORMAT,
        .dimension = WGPUTextureViewDimension_Cube,
        .mipLevelCount = e->mips,
        .arrayLayerCount = BOX_GPU_ENVIRONMENT_FACES,
        .aspect = WGPUTextureAspect_All
    });
    e->target = wgpuTextureCreateView(e->texture, &(WGPUTextureViewDescriptor){
        .format = BOX_GPU_ENVIRONMENT_FORMAT,
        .dimension = WGPUTextureViewDimension_2DArray,
        .mipLevelCount = 1,
        .arrayLayerCount = BOX_GPU_ENVIRONMENT_FACES,
        .aspect = WGPUTextureAspect_All
    });

    return e;
}

void box_gpu_environment_init(
    box_gpu_t *gpu)
{
    ecs_map_init(&gpu->environments, NULL);
}

void box_gpu_environment_fini(
    box_gpu_t *gpu)
{
    box_gpu_environment_image_fini(gpu);
    if (ecs_map_is_init(&gpu->environments)) {
        ecs_map_iter_t it = ecs_map_iter(&gpu->environments);
        while (ecs_map_next(&it)) {
            box_gpu_environment_free(ecs_map_ptr(&it));
        }

        ecs_map_fini(&gpu->environments);
    }
}

void box_gpu_environment_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_environment_desc_t *desc)
{
    box_gpu_environment_t *old = ecs_map_get_deref(
        &gpu->environments, box_gpu_environment_t, handle);
    bool valid = desc && desc->resolution >= 1;
    if (op == BOX_RENDER_UPDATE) {
        if (old && desc) {
            old->filter = desc->filter;
            old->processed = 0;
        }

        goto done;
    }

    if (op == BOX_RENDER_CREATE && !valid) {
        goto done;
    }

    if (old) {
        ecs_map_remove(&gpu->environments, handle);
        box_gpu_environment_free(old);
    }

    if (op == BOX_RENDER_CREATE) {
        ecs_map_insert_ptr(&gpu->environments, handle, box_gpu_environment_new(gpu, desc));
    }

done:
    return;
}

const box_gpu_environment_t* box_gpu_environment_get(
    box_gpu_t *gpu,
    uint32_t handle)
{
    return ecs_map_get_deref(&gpu->environments, box_gpu_environment_t, handle);
}

WGPUTextureView box_gpu_environment_target(
    box_gpu_t *gpu,
    uint32_t handle,
    uint32_t *res)
{
    const box_gpu_environment_t *e = box_gpu_environment_get(gpu, handle);
    *res = e ? e->resolution : 0;

    return e ? e->target : NULL;
}

void box_gpu_environment_written(
    box_gpu_t *gpu,
    uint32_t handle,
    int32_t version)
{
    box_gpu_environment_t *e = ecs_map_get_deref(
        &gpu->environments, box_gpu_environment_t, handle);
    if (e) {
        e->written = version;
    }
}

void box_gpu_environment_process(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    (void)packet;
    ecs_map_iter_t it = ecs_map_iter(&gpu->environments);
    while (ecs_map_next(&it)) {
        box_gpu_environment_t *e = ecs_map_ptr(&it);
        if (e->written && e->written != e->processed) {
            e->processed = e->written;
        }
    }
}

/** Convert a half float to a float.
 *
 * @param h The bits of the half
 * @return The value
 */
static float box_gpu_environment_half(
    uint16_t h)
{
    uint32_t sign = (uint32_t)(h >> 15) << 31, exponent = (h >> 10) & 0x1f, mantissa = h & 0x3ff;
    uint32_t bits;
    float result;
    if (!exponent) {
        result = (float)mantissa * (1.0f / 16777216.0f);
        result = sign ? -result : result;
        goto done;
    }

    bits = exponent == 0x1f
        ? sign | 0x7f800000u | (mantissa << 13)
        : sign | ((exponent + 112) << 23) | (mantissa << 13);
    ecs_os_memcpy(&result, &bits, ECS_SIZEOF(float));
done:
    return result;
}

bool box_render_gpu_read_environment(
    ecs_world_t *world,
    uint32_t handle,
    uint32_t face,
    float *out,
    int32_t count)
{
    bool result = false;
    box_render_gpu_stats_t stats;
    uint16_t *texels = NULL;
    if (!box_render_gpu_stats(world, &stats) || !stats.ready ||
        face >= BOX_GPU_ENVIRONMENT_FACES)
    {
        goto done;
    }

    box_gpu_t *gpu = box_render_get_backend(world)->ctx;
    bool renders = ecs_os_thread_self() == gpu->thread;
    ecs_assert(renders, ECS_INVALID_OPERATION,
        "box_render_gpu_read_environment needs RenderSettings.main_thread and box_render_sync");
    const box_gpu_environment_t *e = renders ? box_gpu_environment_get(gpu, handle) : NULL;
    if (!e || !e->written || (int64_t)e->resolution * e->resolution * 4 > count) {
        goto done;
    }

    uint32_t x, y, size = e->resolution, row = size * BOX_GPU_ENVIRONMENT_TEXEL;
    uint32_t stride = (row + BOX_GPU_ROW_ALIGN - 1) / BOX_GPU_ROW_ALIGN * BOX_GPU_ROW_ALIGN;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)stride * size
    });
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    wgpuCommandEncoderCopyTextureToBuffer(encoder,
        &(WGPUTexelCopyTextureInfo){
            .texture = e->texture,
            .origin = { 0, 0, face },
            .aspect = WGPUTextureAspect_All
        },
        &(WGPUTexelCopyBufferInfo){
            .layout = { .bytesPerRow = stride, .rowsPerImage = size },
            .buffer = buffer
        },
        &(WGPUExtent3D){ size, size, 1 });
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    texels = ecs_os_malloc((ecs_size_t)(stride * size));
    result = box_gpu_read(gpu, buffer, 0, stride * size, texels);
    wgpuBufferRelease(buffer);
    for (y = 0; result && y < size; y ++) {
        const uint16_t *line = texels + (size_t)y * (stride / 2);
        for (x = 0; x < size * 4; x ++) {
            out[(size_t)y * size * 4 + x] = box_gpu_environment_half(line[x]);
        }
    }

done:
    ecs_os_free(texels);

    return result;
}

#else

bool box_render_gpu_read_environment(
    ecs_world_t *world,
    uint32_t handle,
    uint32_t face,
    float *out,
    int32_t count)
{
    (void)world;
    (void)handle;
    (void)face;
    (void)out;
    (void)count;

    return false;
}

#endif
