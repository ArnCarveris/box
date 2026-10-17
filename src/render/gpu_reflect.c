/**
 * @file render/gpu_reflect.c
 * @brief Reflection probe cubemap arrays of the webgpu backend and the passes that fill them.
 *
 * Each scene with resident reflection probes has a cube array with a full mip chain, 6 layers
 * per slot. The captures of gpu_bake.c store their faces in mip 0 and box filter the others.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Texels per side of a workgroup of the store and mip passes, as in bake_sh.wgsl */
#define BOX_GPU_REFLECT_GROUP (8)

/** Cubemap faces of a slot, each a layer of the array */
#define BOX_GPU_REFLECT_FACES (6)

/** Bytes of a texel of BOX_GPU_REFLECT_FORMAT */
#define BOX_GPU_REFLECT_TEXEL (8)

/** Release a cube array and its views.
 *
 * @param c The cube array
 */
static void box_gpu_reflect_free(
    box_gpu_reflect_t *c)
{
    uint32_t i;
    for (i = 0; i < c->mips; i ++) {
        wgpuTextureViewRelease(c->levels[i]);
    }

    wgpuTextureViewRelease(c->cube);
    wgpuTextureRelease(c->texture);
    ecs_os_free(c->levels);
    ecs_os_free(c);
}

/** Create a cube array with a full mip chain and its views.
 *
 * @param gpu The backend
 * @param resolution Face edge of mip 0 in texels
 * @param cubes Cubemap slots
 * @return The cube array
 */
static box_gpu_reflect_t* box_gpu_reflect_new(
    box_gpu_t *gpu,
    uint32_t resolution,
    uint32_t cubes)
{
    box_gpu_reflect_t *c = ecs_os_calloc_t(box_gpu_reflect_t);
    uint32_t i, layers = cubes * BOX_GPU_REFLECT_FACES;
    c->resolution = resolution;
    c->cubes = cubes;
    for (c->mips = 1; (resolution >> c->mips) > 0; c->mips ++) { }
    c->texture = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding |
            WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = { resolution, resolution, layers },
        .format = BOX_GPU_REFLECT_FORMAT,
        .mipLevelCount = c->mips,
        .sampleCount = 1
    });
    c->cube = wgpuTextureCreateView(c->texture, &(WGPUTextureViewDescriptor){
        .format = BOX_GPU_REFLECT_FORMAT,
        .dimension = WGPUTextureViewDimension_CubeArray,
        .mipLevelCount = c->mips,
        .arrayLayerCount = layers,
        .aspect = WGPUTextureAspect_All
    });
    c->levels = ecs_os_calloc_n(WGPUTextureView, c->mips);
    for (i = 0; i < c->mips; i ++) {
        c->levels[i] = wgpuTextureCreateView(c->texture, &(WGPUTextureViewDescriptor){
            .format = BOX_GPU_REFLECT_FORMAT,
            .dimension = WGPUTextureViewDimension_2DArray,
            .baseMipLevel = i,
            .mipLevelCount = 1,
            .arrayLayerCount = layers,
            .aspect = WGPUTextureAspect_All
        });
    }

    return c;
}

/** Copy every mip level of the slots two cube arrays of the same resolution share.
 *
 * @param gpu The backend
 * @param from The old cube array
 * @param to The new cube array
 */
static void box_gpu_reflect_carry(
    box_gpu_t *gpu,
    const box_gpu_reflect_t *from,
    box_gpu_reflect_t *to)
{
    uint32_t i, cubes = from->cubes < to->cubes ? from->cubes : to->cubes;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    for (i = 0; i < to->mips; i ++) {
        uint32_t size = to->resolution >> i;
        wgpuCommandEncoderCopyTextureToTexture(encoder,
            &(WGPUTexelCopyTextureInfo){
                .texture = from->texture, .mipLevel = i, .aspect = WGPUTextureAspect_All
            },
            &(WGPUTexelCopyTextureInfo){
                .texture = to->texture, .mipLevel = i, .aspect = WGPUTextureAspect_All
            },
            &(WGPUExtent3D){ size, size, cubes * BOX_GPU_REFLECT_FACES });
    }

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
}

void box_gpu_reflect_init(
    box_gpu_t *gpu)
{
    WGPUBindGroupLayoutEntry params = {
        .binding = 0,
        .visibility = WGPUShaderStage_Compute,
        .buffer = { .type = WGPUBufferBindingType_Uniform }
    };
    WGPUStorageTextureBindingLayout storage = {
        .access = WGPUStorageTextureAccess_WriteOnly,
        .format = BOX_GPU_REFLECT_FORMAT,
        .viewDimension = WGPUTextureViewDimension_2DArray
    };
    WGPUBindGroupLayoutEntry store[3] = {
        params,
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        { .binding = 10, .visibility = WGPUShaderStage_Compute, .storageTexture = storage }
    };
    WGPUBindGroupLayoutEntry mip[3] = {
        params,
        {
            .binding = 11,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2DArray
            }
        },
        { .binding = 12, .visibility = WGPUShaderStage_Compute, .storageTexture = storage }
    };
    ecs_map_init(&gpu->reflections, NULL);
    gpu->reflect_store_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 3, .entries = store });
    gpu->reflect_mip_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 3, .entries = mip });
    gpu->reflect_store_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &gpu->reflect_store_layout
        });
    gpu->reflect_mip_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &gpu->reflect_mip_layout
        });
}

void box_gpu_reflect_fini(
    box_gpu_t *gpu)
{
    if (ecs_map_is_init(&gpu->reflections)) {
        ecs_map_iter_t it = ecs_map_iter(&gpu->reflections);
        while (ecs_map_next(&it)) {
            box_gpu_reflect_free(ecs_map_ptr(&it));
        }

        ecs_map_fini(&gpu->reflections);
    }

    WGPUComputePipeline pipelines[2] = { gpu->reflect_store, gpu->reflect_mip };
    WGPUPipelineLayout layouts[2] = {
        gpu->reflect_store_pipeline_layout, gpu->reflect_mip_pipeline_layout
    };
    WGPUBindGroupLayout groups[2] = { gpu->reflect_store_layout, gpu->reflect_mip_layout };
    int32_t i;
    for (i = 0; i < 2; i ++) {
        if (pipelines[i]) {
            wgpuComputePipelineRelease(pipelines[i]);
        }

        if (layouts[i]) {
            wgpuPipelineLayoutRelease(layouts[i]);
        }

        if (groups[i]) {
            wgpuBindGroupLayoutRelease(groups[i]);
        }
    }

    gpu->reflect_store = gpu->reflect_mip = NULL;
    gpu->reflect_store_pipeline_layout = gpu->reflect_mip_pipeline_layout = NULL;
    gpu->reflect_store_layout = gpu->reflect_mip_layout = NULL;
}

void box_gpu_reflect_apply(
    box_gpu_t *gpu,
    box_render_resource_op_t op,
    uint32_t handle,
    const box_render_reflection_desc_t *desc)
{
    box_gpu_reflect_t *old = ecs_map_get_deref(&gpu->reflections, box_gpu_reflect_t, handle);
    bool valid = desc && desc->resolution && desc->cubes &&
        (uint64_t)desc->cubes * BOX_GPU_REFLECT_FACES <= UINT32_MAX;
    if (op == BOX_RENDER_DESTROY || (op == BOX_RENDER_CREATE && valid)) {
        if (old) {
            ecs_map_remove(&gpu->reflections, handle);
        }
    } else {
        goto done;
    }

    if (op == BOX_RENDER_CREATE) {
        box_gpu_reflect_t *c = box_gpu_reflect_new(gpu, desc->resolution, desc->cubes);
        if (old && old->resolution == c->resolution) {
            box_gpu_reflect_carry(gpu, old, c);
        }

        ecs_map_insert_ptr(&gpu->reflections, handle, c);
    }

    if (old) {
        box_gpu_reflect_free(old);
    }

done:
    return;
}

/** Create a compute pipeline of an entry point of the bake shader, releasing the one it
 * replaces.
 *
 * @param gpu The backend
 * @param pipeline The pipeline, replaced
 * @param layout The pipeline layout
 * @param module The shader module
 * @param entry The entry point
 */
static void box_gpu_reflect_compute(
    box_gpu_t *gpu,
    WGPUComputePipeline *pipeline,
    WGPUPipelineLayout layout,
    WGPUShaderModule module,
    const char *entry)
{
    if (*pipeline) {
        wgpuComputePipelineRelease(*pipeline);
    }

    *pipeline = wgpuDeviceCreateComputePipeline(gpu->device, &(WGPUComputePipelineDescriptor){
        .layout = layout,
        .compute = {
            .module = module,
            .entryPoint = { .data = entry, .length = WGPU_STRLEN }
        }
    });
}

void box_gpu_reflect_pipelines(
    box_gpu_t *gpu,
    WGPUShaderModule module)
{
    box_gpu_reflect_compute(gpu, &gpu->reflect_store, gpu->reflect_store_pipeline_layout,
        module, "reflect_store");
    box_gpu_reflect_compute(gpu, &gpu->reflect_mip, gpu->reflect_mip_pipeline_layout,
        module, "reflect_mip");
}

/** Get the workgroups along an edge of a face of the store and mip passes.
 *
 * @param size Face edge in texels
 * @return Workgroups that cover it
 */
static uint32_t box_gpu_reflect_groups(
    uint32_t size)
{
    return (size + BOX_GPU_REFLECT_GROUP - 1) / BOX_GPU_REFLECT_GROUP;
}

bool box_gpu_reflect_store(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_gpu_reflect_t *cube,
    WGPUBuffer params,
    uint64_t params_size,
    WGPUTextureView atlas,
    int32_t count,
    const WGPUPassTimestampWrites *writes)
{
    uint32_t i, faces = (uint32_t)count * BOX_GPU_REFLECT_FACES;
    bool result = gpu->reflect_store && gpu->reflect_mip && count > 0;
    if (!result) {
        goto done;
    }

    WGPUBindGroupEntry entries[3] = {
        { .binding = 0, .buffer = params, .size = params_size },
        { .binding = 1, .textureView = atlas },
        { .binding = 10, .textureView = cube->levels[0] }
    };
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = gpu->reflect_store_layout,
        .entryCount = 3,
        .entries = entries
    });
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,
        &(WGPUComputePassDescriptor){ .timestampWrites = writes });
    uint32_t groups = box_gpu_reflect_groups(cube->resolution);
    wgpuComputePassEncoderSetPipeline(pass, gpu->reflect_store);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, groups, groups, faces);
    wgpuBindGroupRelease(group);
    if (cube->mips > 1) {
        wgpuComputePassEncoderSetPipeline(pass, gpu->reflect_mip);
    }

    for (i = 1; i < cube->mips; i ++) {
        WGPUBindGroupEntry levels[3] = {
            { .binding = 0, .buffer = params, .size = params_size },
            { .binding = 11, .textureView = cube->levels[i - 1] },
            { .binding = 12, .textureView = cube->levels[i] }
        };
        group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
            .layout = gpu->reflect_mip_layout,
            .entryCount = 3,
            .entries = levels
        });
        groups = box_gpu_reflect_groups(cube->resolution >> i);
        wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
        wgpuComputePassEncoderDispatchWorkgroups(pass, groups, groups, faces);
        wgpuBindGroupRelease(group);
    }

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
done:
    return result;
}

/** Convert a half float to a float.
 *
 * @param h The bits of the half
 * @return The value
 */
static float box_gpu_reflect_half(
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

bool box_render_gpu_read_reflection(
    ecs_world_t *world,
    uint32_t texture,
    uint32_t slot,
    uint32_t face,
    float *out,
    int32_t count)
{
    bool result = false;
    box_render_gpu_stats_t stats;
    uint16_t *texels = NULL;
    if (!box_render_gpu_stats(world, &stats) || !stats.ready || face >= BOX_GPU_REFLECT_FACES) {
        goto done;
    }

    box_gpu_t *gpu = box_render_get_backend(world)->ctx;
    bool renders = ecs_os_thread_self() == gpu->thread;
    ecs_assert(renders, ECS_INVALID_OPERATION,
        "box_render_gpu_read_reflection needs RenderSettings.main_thread and box_render_sync");
    const box_gpu_reflect_t *c = renders
        ? ecs_map_get_deref(&gpu->reflections, box_gpu_reflect_t, texture)
        : NULL;
    if (!c || slot >= c->cubes || (int64_t)c->resolution * c->resolution * 4 > count) {
        goto done;
    }

    uint32_t x, y, size = c->resolution, row = size * BOX_GPU_REFLECT_TEXEL;
    uint32_t stride = (row + BOX_GPU_ROW_ALIGN - 1) / BOX_GPU_ROW_ALIGN * BOX_GPU_ROW_ALIGN;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)stride * size
    });
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    wgpuCommandEncoderCopyTextureToBuffer(encoder,
        &(WGPUTexelCopyTextureInfo){
            .texture = c->texture,
            .origin = { 0, 0, slot * BOX_GPU_REFLECT_FACES + face },
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
            out[(size_t)y * size * 4 + x] = box_gpu_reflect_half(line[x]);
        }
    }

done:
    ecs_os_free(texels);

    return result;
}

#else

bool box_render_gpu_read_reflection(
    ecs_world_t *world,
    uint32_t texture,
    uint32_t slot,
    uint32_t face,
    float *out,
    int32_t count)
{
    (void)world;
    (void)texture;
    (void)slot;
    (void)face;
    (void)out;
    (void)count;

    return false;
}

#endif
