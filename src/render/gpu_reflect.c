/**
 * @file render/gpu_reflect.c
 * @brief Reflection probe cubemap arrays of the webgpu backend and the passes that fill them.
 *
 * Each scene with resident reflection probes has a cube array, 6 layers per slot, and the L2 SH
 * irradiance of each slot, which its views sample; views without probes bind one black slot.
 * Captures store their faces in mip 0 and in source cubes with a box filtered full mip chain.
 *
 * The prefilter fills each higher mip level of the slots with the GGX prefiltered radiance from
 * the source cubes, in the command buffer of the capture.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Texels per side of a workgroup of the store and mip passes, as in bake_sh.wgsl */
#define BOX_GPU_REFLECT_GROUP (8)

/** Cubemap faces of a slot, each a layer of the array */
#define BOX_GPU_REFLECT_FACES (6)

/** Bytes of a texel of BOX_GPU_REFLECT_FORMAT */
#define BOX_GPU_REFLECT_TEXEL (8)

void box_gpu_reflect_free(
    box_gpu_reflect_t *c)
{
    uint32_t i;
    for (i = 0; i < c->mips; i ++) {
        wgpuTextureViewRelease(c->levels[i]);
    }

    if (c->sh) {
        wgpuBufferRelease(c->sh);
        wgpuBufferRelease(c->visibility);
    }

    wgpuTextureViewRelease(c->cube);
    wgpuTextureRelease(c->texture);
    ecs_os_free(c->levels);
    ecs_os_free(c);
}

box_gpu_reflect_t* box_gpu_reflect_new(
    box_gpu_t *gpu,
    uint32_t resolution,
    uint32_t cubes,
    bool source)
{
    box_gpu_reflect_t *c = ecs_os_calloc_t(box_gpu_reflect_t);
    uint32_t i, layers = cubes * BOX_GPU_REFLECT_FACES;
    uint32_t smallest = source ? 1 : BOX_GPU_REFLECT_MIN_MIP;
    c->resolution = resolution;
    c->cubes = cubes;
    for (c->mips = 1; (resolution >> c->mips) >= smallest; c->mips ++) { }
    if (!source) {
        c->sh = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc |
                WGPUBufferUsage_CopyDst,
            .size = (uint64_t)cubes * BOX_GPU_PROBE_SIZE
        });
        c->visibility = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Storage,
            .size = (uint64_t)cubes * BOX_GPU_VISIBILITY_SIZE
        });
    }

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

/** Copy every mip level and the SH of the slots two cube arrays of the same resolution share.
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

    wgpuCommandEncoderCopyBufferToBuffer(encoder, from->sh, 0, to->sh, 0,
        (uint64_t)cubes * BOX_GPU_PROBE_SIZE);
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
    WGPUBindGroupLayoutEntry store[6] = {
        params,
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        { .binding = 10, .visibility = WGPUShaderStage_Compute, .storageTexture = storage },
        { .binding = 13, .visibility = WGPUShaderStage_Compute, .storageTexture = storage },
        {
            .binding = 15,
            .visibility = WGPUShaderStage_Compute,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        },
        {
            .binding = 17,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_Cube
            }
        }
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
    WGPUBindGroupLayoutEntry prefilter[4] = {
        params,
        {
            .binding = 14,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_CubeArray
            }
        },
        {
            .binding = 15,
            .visibility = WGPUShaderStage_Compute,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        },
        { .binding = 16, .visibility = WGPUShaderStage_Compute, .storageTexture = storage }
    };
    ecs_map_init(&gpu->reflections, NULL);
    gpu->reflect_store_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 6, .entries = store });
    gpu->reflect_mip_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 3, .entries = mip });
    gpu->reflect_prefilter_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 4, .entries = prefilter });
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
    gpu->reflect_prefilter_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &gpu->reflect_prefilter_layout
        });
    gpu->reflect_sampler = wgpuDeviceCreateSampler(gpu->device, &(WGPUSamplerDescriptor){
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Linear,
        .lodMaxClamp = 32.0f,
        .maxAnisotropy = 1
    });
    gpu->no_reflection = box_gpu_reflect_new(gpu, 1, 1, false);
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

    if (gpu->reflect_source) {
        box_gpu_reflect_free(gpu->reflect_source);
    }

    if (gpu->no_reflection) {
        box_gpu_reflect_free(gpu->no_reflection);
    }

    if (gpu->reflect_sampler) {
        wgpuSamplerRelease(gpu->reflect_sampler);
    }

    WGPUComputePipeline pipelines[3] = {
        gpu->reflect_store, gpu->reflect_mip, gpu->reflect_prefilter
    };
    WGPUPipelineLayout layouts[3] = {
        gpu->reflect_store_pipeline_layout, gpu->reflect_mip_pipeline_layout,
        gpu->reflect_prefilter_pipeline_layout
    };
    WGPUBindGroupLayout groups[3] = {
        gpu->reflect_store_layout, gpu->reflect_mip_layout, gpu->reflect_prefilter_layout
    };
    int32_t i;
    for (i = 0; i < 3; i ++) {
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

    gpu->reflect_source = NULL;
    gpu->no_reflection = NULL;
    gpu->reflect_sampler = NULL;
    gpu->reflect_store = gpu->reflect_mip = gpu->reflect_prefilter = NULL;
    gpu->reflect_store_pipeline_layout = gpu->reflect_mip_pipeline_layout = NULL;
    gpu->reflect_prefilter_pipeline_layout = NULL;
    gpu->reflect_store_layout = gpu->reflect_mip_layout = gpu->reflect_prefilter_layout = NULL;
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
        box_gpu_reflect_t *c = box_gpu_reflect_new(gpu, desc->resolution, desc->cubes, false);
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

const box_gpu_reflect_t* box_gpu_reflect_of(
    box_gpu_t *gpu,
    const box_render_view_t *v)
{
    bool listed = v->reflections && v->lights.enabled &&
        ecs_map_get(&gpu->buffers, v->reflections);

    return listed
        ? ecs_map_get_deref(&gpu->reflections, box_gpu_reflect_t, v->reflections)
        : NULL;
}

/** Request a compute pipeline of an entry point of the bake shader.
 *
 * @param gpu The backend
 * @param layout The pipeline layout
 * @param module The shader module
 * @param entry The entry point
 * @return The pipeline, NULL while it is created
 */
static WGPUComputePipeline box_gpu_reflect_compute(
    box_gpu_t *gpu,
    WGPUPipelineLayout layout,
    WGPUShaderModule module,
    const char *entry)
{
    return box_gpu_compute_pipeline(gpu, &(WGPUComputePipelineDescriptor){
        .layout = layout,
        .compute = {
            .module = module,
            .entryPoint = { .data = entry, .length = WGPU_STRLEN }
        }
    });
}

void box_gpu_reflect_pipelines(
    box_gpu_t *gpu,
    WGPUShaderModule module,
    WGPUComputePipeline *next,
    WGPUComputePipeline **current)
{
    next[0] = box_gpu_reflect_compute(gpu, gpu->reflect_store_pipeline_layout, module,
        "reflect_store");
    next[1] = box_gpu_reflect_compute(gpu, gpu->reflect_mip_pipeline_layout, module,
        "reflect_mip");
    next[2] = box_gpu_reflect_compute(gpu, gpu->reflect_prefilter_pipeline_layout, module,
        "reflect_prefilter");
    current[0] = &gpu->reflect_store;
    current[1] = &gpu->reflect_mip;
    current[2] = &gpu->reflect_prefilter;
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

/** Get source cubes for the probes of a chunk at a resolution, created again when the ones
 * there are smaller or of another resolution.
 *
 * @param gpu The backend
 * @param resolution Face edge of mip 0 in texels
 * @param count Probes of the chunk
 * @return The source cubes
 */
static box_gpu_reflect_t* box_gpu_reflect_source(
    box_gpu_t *gpu,
    uint32_t resolution,
    uint32_t count)
{
    box_gpu_reflect_t *c = gpu->reflect_source;
    if (c && (c->resolution != resolution || c->cubes < count)) {
        box_gpu_reflect_free(c);
        c = NULL;
    }

    if (!c) {
        c = box_gpu_reflect_new(gpu, resolution, count, true);
        gpu->reflect_source = c;
    }

    return c;
}

/** Record a dispatch of a pass over the faces of the probes of a chunk at one mip level.
 *
 * @param gpu The backend
 * @param pass The compute pass
 * @param layout Bind group layout of the pipeline the pass has set
 * @param entries Entries of the bind group
 * @param count Entries
 * @param size Face edge of the level in texels
 * @param faces Faces of the chunk
 */
static void box_gpu_reflect_dispatch(
    box_gpu_t *gpu,
    WGPUComputePassEncoder pass,
    WGPUBindGroupLayout layout,
    const WGPUBindGroupEntry *entries,
    uint32_t count,
    uint32_t size,
    uint32_t faces)
{
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = layout,
        .entryCount = count,
        .entries = entries
    });
    uint32_t groups = box_gpu_reflect_groups(size);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, groups, groups, faces);
    wgpuBindGroupRelease(group);
}

bool box_gpu_reflect_ready(
    const box_gpu_t *gpu)
{
    return gpu->reflect_store && gpu->reflect_mip && gpu->reflect_prefilter;
}

void box_gpu_reflect_mip_chain(
    box_gpu_t *gpu,
    WGPUComputePassEncoder pass,
    WGPUBuffer params,
    uint64_t params_size,
    const WGPUTextureView *levels,
    uint32_t resolution,
    uint32_t mips,
    uint32_t faces)
{
    uint32_t i;
    if (mips > 1) {
        wgpuComputePassEncoderSetPipeline(pass, gpu->reflect_mip);
    }

    for (i = 1; i < mips; i ++) {
        box_gpu_reflect_dispatch(gpu, pass, gpu->reflect_mip_layout, (WGPUBindGroupEntry[3]){
            { .binding = 0, .buffer = params, .size = params_size },
            { .binding = 11, .textureView = levels[i - 1] },
            { .binding = 12, .textureView = levels[i] }
        }, 3, resolution >> i, faces);
    }
}

void box_gpu_reflect_prefilter_chain(
    box_gpu_t *gpu,
    WGPUComputePassEncoder pass,
    WGPUBuffer params,
    uint64_t params_size,
    WGPUTextureView source,
    const WGPUTextureView *levels,
    uint32_t resolution,
    uint32_t mips,
    uint32_t faces)
{
    uint32_t i;
    if (mips > 1) {
        wgpuComputePassEncoderSetPipeline(pass, gpu->reflect_prefilter);
    }

    for (i = 1; i < mips; i ++) {
        box_gpu_reflect_dispatch(gpu, pass, gpu->reflect_prefilter_layout,
            (WGPUBindGroupEntry[4]){
                { .binding = 0, .buffer = params, .size = params_size },
                { .binding = 14, .textureView = source },
                { .binding = 15, .sampler = gpu->reflect_sampler },
                { .binding = 16, .textureView = levels[i] }
            }, 4, resolution >> i, faces);
    }
}

bool box_gpu_reflect_store(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const box_gpu_reflect_t *cube,
    WGPUBuffer params,
    uint64_t params_size,
    WGPUTextureView atlas,
    WGPUTextureView environment,
    int32_t count,
    const WGPUPassTimestampWrites *writes)
{
    uint32_t faces = (uint32_t)count * BOX_GPU_REFLECT_FACES;
    bool result = box_gpu_reflect_ready(gpu) && count > 0;
    if (!result) {
        goto done;
    }

    box_gpu_reflect_t *source = box_gpu_reflect_source(gpu, cube->resolution, (uint32_t)count);
    WGPUComputePassEncoder pass = box_gpu_compute_begin(encoder, writes);
    wgpuComputePassEncoderSetPipeline(pass, gpu->reflect_store);
    box_gpu_reflect_dispatch(gpu, pass, gpu->reflect_store_layout, (WGPUBindGroupEntry[6]){
        { .binding = 0, .buffer = params, .size = params_size },
        { .binding = 1, .textureView = atlas },
        { .binding = 10, .textureView = cube->levels[0] },
        { .binding = 13, .textureView = source->levels[0] },
        { .binding = 15, .sampler = gpu->reflect_sampler },
        { .binding = 17, .textureView = environment ? environment : gpu->no_environment }
    }, 6, cube->resolution, faces);
    box_gpu_reflect_mip_chain(gpu, pass, params, params_size, source->levels,
        cube->resolution, source->mips, faces);
    box_gpu_reflect_prefilter_chain(gpu, pass, params, params_size, source->cube,
        cube->levels, cube->resolution, cube->mips, faces);
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

bool box_render_gpu_read_reflection_sh(
    ecs_world_t *world,
    uint32_t texture,
    uint32_t slot,
    float out[BOX_RENDER_SH_COEFFICIENTS][4])
{
    bool result = false;
    box_render_gpu_stats_t stats;
    if (!box_render_gpu_stats(world, &stats) || !stats.ready) {
        goto done;
    }

    box_gpu_t *gpu = box_render_get_backend(world)->ctx;
    bool renders = ecs_os_thread_self() == gpu->thread;
    ecs_assert(renders, ECS_INVALID_OPERATION,
        "box_render_gpu_read_reflection_sh needs RenderSettings.main_thread and box_render_sync");
    const box_gpu_reflect_t *c = renders
        ? ecs_map_get_deref(&gpu->reflections, box_gpu_reflect_t, texture)
        : NULL;
    if (!c || slot >= c->cubes) {
        goto done;
    }

    result = box_gpu_read(gpu, c->sh, slot * (uint32_t)BOX_GPU_PROBE_SIZE,
        (uint32_t)BOX_GPU_PROBE_SIZE, out);
done:
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

bool box_render_gpu_read_reflection_sh(
    ecs_world_t *world,
    uint32_t texture,
    uint32_t slot,
    float out[BOX_RENDER_SH_COEFFICIENTS][4])
{
    (void)world;
    (void)texture;
    (void)slot;
    (void)out;

    return false;
}

#endif
