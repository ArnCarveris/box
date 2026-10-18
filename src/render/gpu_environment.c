/**
 * @file render/gpu_environment.c
 * @brief Environment cubemaps of the webgpu backend: the target their sources write and the
 * processing that runs after the sources of a packet wrote new content.
 *
 * The processing reuses the reflection probe passes of bake_sh.wgsl: reflect_mip box filters
 * the mip chain, reflect_prefilter writes the GGX prefiltered mips of a Ggx cubemap, and
 * environment_sh projects a box filtered mip onto the SH irradiance.
 */

#include "gpu_environment.h"
#include "gpu_bake.h"

#ifdef BOX_RENDER_WGPU

/** Cubemap faces, each a layer of the texture */
#define BOX_GPU_ENVIRONMENT_FACES (6)

/** Bytes of a texel of BOX_GPU_ENVIRONMENT_FORMAT */
#define BOX_GPU_ENVIRONMENT_TEXEL (8)

/** Face edge in texels of the mip level the SH is projected from at most */
#define BOX_GPU_ENVIRONMENT_SH_SIZE (16)

struct box_gpu_env_process_t {
    WGPUBindGroupLayout sh_layout;  /**< Binds a mip level and the SH buffer */
    WGPUPipelineLayout sh_pipeline_layout; /**< Pipeline layout of the SH projection */
    WGPUComputePipeline sh;         /**< SH projection, NULL until the bake shader exists */
    int32_t revision;               /**< Revision of the shader the pipeline was made with */
    ecs_map_t sources;              /**< Resolution to the box_gpu_reflect_t* source cube a Ggx
                                     *   cubemap of that resolution is box filtered into */
    ecs_vec_t due;                  /**< box_gpu_environment_t* processed by a packet */
};

/** Release a cubemap, its views and buffers.
 *
 * @param e The cubemap
 */
static void box_gpu_environment_free(
    box_gpu_environment_t *e)
{
    uint32_t i;
    for (i = 0; i < e->mips; i ++) {
        wgpuTextureViewRelease(e->levels[i]);
    }

    wgpuBufferRelease(e->params);
    wgpuBufferRelease(e->sh);
    wgpuTextureViewRelease(e->cube);
    wgpuTextureRelease(e->texture);
    ecs_os_free(e->levels);
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
    uint32_t i, res = desc->resolution;
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
    e->levels = ecs_os_calloc_n(WGPUTextureView, e->mips);
    for (i = 0; i < e->mips; i ++) {
        e->levels[i] = wgpuTextureCreateView(e->texture, &(WGPUTextureViewDescriptor){
            .format = BOX_GPU_ENVIRONMENT_FORMAT,
            .dimension = WGPUTextureViewDimension_2DArray,
            .baseMipLevel = i,
            .mipLevelCount = 1,
            .arrayLayerCount = BOX_GPU_ENVIRONMENT_FACES,
            .aspect = WGPUTextureAspect_All
        });
    }

    e->target = e->levels[0];
    e->params = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = sizeof(box_gpu_bake_params_t)
    });
    e->sh = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc,
        .size = BOX_GPU_PROBE_SIZE
    });
    box_gpu_bake_params_t *params = ecs_os_calloc_t(box_gpu_bake_params_t);
    params->size[0] = res;
    params->size[1] = 1;
    wgpuQueueWriteBuffer(gpu->queue, e->params, 0, params, sizeof(box_gpu_bake_params_t));
    ecs_os_free(params);

    return e;
}

void box_gpu_environment_init(
    box_gpu_t *gpu)
{
    ecs_map_init(&gpu->environments, NULL);
}

/** Release the SH pipeline and the source cubes of the processing.
 *
 * @param gpu The backend
 */
static void box_gpu_env_process_fini(
    box_gpu_t *gpu)
{
    box_gpu_env_process_t *s = gpu->environment_process;
    if (!s) {
        goto done;
    }

    ecs_map_iter_t it = ecs_map_iter(&s->sources);
    while (ecs_map_next(&it)) {
        box_gpu_reflect_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(&s->sources);
    ecs_vec_fini_t(NULL, &s->due, box_gpu_environment_t*);
    if (s->sh) {
        wgpuComputePipelineRelease(s->sh);
    }

    wgpuPipelineLayoutRelease(s->sh_pipeline_layout);
    wgpuBindGroupLayoutRelease(s->sh_layout);
    ecs_os_free(s);
    gpu->environment_process = NULL;
done:
    return;
}

void box_gpu_environment_fini(
    box_gpu_t *gpu)
{
    box_gpu_environment_image_fini(gpu);
    box_gpu_env_process_fini(gpu);
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

/** Get the state of the processing, creating the layouts of the SH projection on first use.
 *
 * @param gpu The backend
 * @return The state
 */
static box_gpu_env_process_t* box_gpu_env_process_get(
    box_gpu_t *gpu)
{
    box_gpu_env_process_t *s = gpu->environment_process;
    if (s) {
        goto done;
    }

    WGPUBindGroupLayoutEntry entries[2] = {
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        },
        {
            .binding = 11,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2DArray
            }
        }
    };
    s = ecs_os_calloc_t(box_gpu_env_process_t);
    s->sh_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 2, .entries = entries });
    s->sh_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &s->sh_layout
        });
    ecs_map_init(&s->sources, NULL);
    ecs_vec_init_t(NULL, &s->due, box_gpu_environment_t*, 0);
    gpu->environment_process = s;
done:
    return s;
}

bool box_gpu_environment_ready(
    box_gpu_t *gpu)
{
    box_gpu_shader_t *shader = ecs_map_get_deref(
        &gpu->shaders, box_gpu_shader_t, BOX_RENDER_SHADER_BAKE);
    bool result = shader && shader->module && box_gpu_bake_ready(gpu, box_gpu_bake_state(gpu));
    if (!result) {
        goto done;
    }

    box_gpu_env_process_t *s = box_gpu_env_process_get(gpu);
    if (!s->sh || s->revision != shader->revision) {
        if (s->sh) {
            wgpuComputePipelineRelease(s->sh);
        }

        s->sh = wgpuDeviceCreateComputePipeline(gpu->device, &(WGPUComputePipelineDescriptor){
            .layout = s->sh_pipeline_layout,
            .compute = {
                .module = shader->module,
                .entryPoint = { .data = "environment_sh", .length = WGPU_STRLEN }
            }
        });
        s->revision = shader->revision;
    }

done:
    return result;
}

/** Get the source cube Ggx cubemaps of a resolution are box filtered into, created on first
 * use: one cube with a full mip chain, which the prefilter samples.
 *
 * @param gpu The backend
 * @param s The state
 * @param resolution Face edge of mip 0 in texels
 * @return The source cube
 */
static box_gpu_reflect_t* box_gpu_env_source(
    box_gpu_t *gpu,
    box_gpu_env_process_t *s,
    uint32_t resolution)
{
    box_gpu_reflect_t *result = ecs_map_get_deref(&s->sources, box_gpu_reflect_t, resolution);
    if (!result) {
        result = box_gpu_reflect_new(gpu, resolution, 1, true);
        ecs_map_insert_ptr(&s->sources, resolution, result);
    }

    return result;
}

/** Record the processing of a cubemap: its box filtered or GGX prefiltered mips and its SH.
 *
 * A Ggx cubemap copies mip 0 into the source cube of its resolution first, so cubemaps of one
 * resolution share it in turn within the command buffer.
 *
 * @param gpu The backend
 * @param s The state
 * @param encoder The encoder of the packet
 * @param e The cubemap
 * @param writes Timestamps the pass writes, NULL for none
 */
static void box_gpu_env_record(
    box_gpu_t *gpu,
    box_gpu_env_process_t *s,
    WGPUCommandEncoder encoder,
    const box_gpu_environment_t *e,
    const WGPUPassTimestampWrites *writes)
{
    uint32_t level = 0, faces = BOX_GPU_ENVIRONMENT_FACES;
    const WGPUTextureView *levels = e->levels;
    uint32_t mips = e->mips;
    box_gpu_reflect_t *source = NULL;
    while ((e->resolution >> level) > BOX_GPU_ENVIRONMENT_SH_SIZE) {
        level ++;
    }

    if (e->filter == EnvironmentFilterGgx) {
        source = box_gpu_env_source(gpu, s, e->resolution);
        wgpuCommandEncoderCopyTextureToTexture(encoder,
            &(WGPUTexelCopyTextureInfo){
                .texture = e->texture, .aspect = WGPUTextureAspect_All
            },
            &(WGPUTexelCopyTextureInfo){
                .texture = source->texture, .aspect = WGPUTextureAspect_All
            },
            &(WGPUExtent3D){ e->resolution, e->resolution, faces });
        levels = source->levels;
        mips = source->mips;
    }

    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,
        &(WGPUComputePassDescriptor){ .timestampWrites = writes });
    box_gpu_reflect_mip_chain(gpu, pass, e->params, sizeof(box_gpu_bake_params_t), levels,
        e->resolution, mips, faces);
    if (source) {
        box_gpu_reflect_prefilter_chain(gpu, pass, e->params, sizeof(box_gpu_bake_params_t),
            source->cube, e->levels, e->resolution, e->mips, faces);
    }

    WGPUBindGroup group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = s->sh_layout,
        .entryCount = 2,
        .entries = (WGPUBindGroupEntry[2]){
            { .binding = 2, .buffer = e->sh, .size = BOX_GPU_PROBE_SIZE },
            { .binding = 11, .textureView = levels[level] }
        }
    });
    wgpuComputePassEncoderSetPipeline(pass, s->sh);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuBindGroupRelease(group);
}

/** Check whether a cubemap needs processing.
 *
 * @param e The cubemap
 * @return Whether its source wrote a version that was not processed or its filter changed
 */
static bool box_gpu_env_due(
    const box_gpu_environment_t *e)
{
    return e->written && (e->written != e->processed || e->filter != e->processed_filter);
}

void box_gpu_environment_process(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    uint64_t start = ecs_os_now();
    int32_t i, count;
    bool due = false;
    (void)packet;
    ecs_map_iter_t it = ecs_map_iter(&gpu->environments);
    while (!due && ecs_map_next(&it)) {
        due = box_gpu_env_due(ecs_map_ptr(&it));
    }

    if (!due || !box_gpu_environment_ready(gpu)) {
        goto done;
    }

    box_gpu_env_process_t *s = gpu->environment_process;
    ecs_vec_clear(&s->due);
    it = ecs_map_iter(&gpu->environments);
    while (ecs_map_next(&it)) {
        box_gpu_environment_t *e = ecs_map_ptr(&it);
        if (box_gpu_env_due(e)) {
            *ecs_vec_append_t(NULL, &s->due, box_gpu_environment_t*) = e;
        }
    }

    count = ecs_vec_count(&s->due);
    box_gpu_bake_t *b = gpu->bake;
    int32_t readback = box_gpu_bake_readback(b);
    uint32_t query = (uint32_t)readback * 2;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    for (i = 0; i < count; i ++) {
        box_gpu_environment_t *e = *ecs_vec_get_t(&s->due, box_gpu_environment_t*, i);
        WGPUPassTimestampWrites writes = {
            .querySet = b->queries,
            .beginningOfPassWriteIndex = i == 0 ? query : WGPU_QUERY_SET_INDEX_UNDEFINED,
            .endOfPassWriteIndex = i == count - 1 ? query + 1 : WGPU_QUERY_SET_INDEX_UNDEFINED
        };
        bool timed = readback != -1 && (i == 0 || i == count - 1);
        box_gpu_env_record(gpu, s, encoder, e, timed ? &writes : NULL);
        e->processed = e->written;
        e->processed_filter = e->filter;
        e->runs ++;
    }

    box_gpu_bake_submit(gpu, encoder);
    if (readback != -1) {
        b->readbacks[readback].probes = count;
        b->readbacks[readback].kind = BOX_GPU_BAKE_ENVIRONMENT;
        b->readbacks[readback].state = BOX_GPU_BAKE_RECORDED;
    } else if (!b->queries) {
        box_gpu_bake_done_measure(gpu, b, BOX_GPU_BAKE_ENVIRONMENT, start, count);
    }

done:
    return;
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

/** Get the cubemap of an environment for a read of the main thread.
 *
 * @param world The world
 * @param handle Handle of the cubemap
 * @param fn Name of the read, for the assert
 * @param gpu Receives the backend
 * @return The cubemap, NULL when the backend is not ready or has none
 */
static const box_gpu_environment_t* box_gpu_environment_read(
    ecs_world_t *world,
    uint32_t handle,
    const char *fn,
    box_gpu_t **gpu)
{
    const box_gpu_environment_t *result = NULL;
    box_render_gpu_stats_t stats;
    if (!box_render_gpu_stats(world, &stats) || !stats.ready) {
        goto done;
    }

    *gpu = box_render_get_backend(world)->ctx;
    bool renders = ecs_os_thread_self() == (*gpu)->thread;
    ecs_assert(renders, ECS_INVALID_OPERATION,
        "%s needs RenderSettings.main_thread and box_render_sync", fn);
    (void)fn;
    result = renders ? box_gpu_environment_get(*gpu, handle) : NULL;
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
    uint16_t *texels = NULL;
    box_gpu_t *gpu = NULL;
    const box_gpu_environment_t *e = face < BOX_GPU_ENVIRONMENT_FACES
        ? box_gpu_environment_read(world, handle, "box_render_gpu_read_environment", &gpu)
        : NULL;
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

bool box_render_gpu_environment_state(
    ecs_world_t *world,
    uint32_t handle,
    box_render_environment_state_t *out)
{
    box_gpu_t *gpu = NULL;
    const box_gpu_environment_t *e = box_gpu_environment_read(world, handle,
        "box_render_gpu_environment_state", &gpu);
    ecs_os_memset_t(out, 0, box_render_environment_state_t);
    if (e) {
        out->written = e->written;
        out->processed = e->processed;
        out->filter = e->processed_filter;
        out->runs = e->runs;
    }

    return e != NULL;
}

bool box_render_gpu_read_environment_sh(
    ecs_world_t *world,
    uint32_t handle,
    float out[BOX_RENDER_SH_COEFFICIENTS][4])
{
    box_gpu_t *gpu = NULL;
    const box_gpu_environment_t *e = box_gpu_environment_read(world, handle,
        "box_render_gpu_read_environment_sh", &gpu);

    return e && e->processed &&
        box_gpu_read(gpu, e->sh, 0, (uint32_t)BOX_GPU_PROBE_SIZE, out);
}

#else

bool box_render_gpu_environment_state(
    ecs_world_t *world,
    uint32_t handle,
    box_render_environment_state_t *out)
{
    (void)world;
    (void)handle;
    ecs_os_memset_t(out, 0, box_render_environment_state_t);

    return false;
}

bool box_render_gpu_read_environment_sh(
    ecs_world_t *world,
    uint32_t handle,
    float out[BOX_RENDER_SH_COEFFICIENTS][4])
{
    (void)world;
    (void)handle;
    (void)out;

    return false;
}

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
