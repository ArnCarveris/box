/**
 * @file render/gpu_hiz.c
 * @brief Depth pyramids and visibility histories of the occlusion culling of the webgpu backend.
 *
 * A view that culls by occlusion draws what it drew the previous frame (its history), reduces
 * that depth into a pyramid of the farthest (smallest, reversed Z) depths and tests the rest
 * against it, see etc/shaders/cull.wgsl. Levels are powers of two up to the render size.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Invocations along each axis of a workgroup of the pyramid shaders */
#define BOX_GPU_HIZ_GROUP (8)

/** Cull passes after which an unused history or pyramid is released */
#define BOX_GPU_HIZ_IDLE (240)

/** Smallest history buffer in words */
#define BOX_GPU_HIZ_MIN_WORDS (1024)

void box_gpu_hiz_init(
    box_gpu_t *gpu)
{
    box_gpu_cull_t *c = &gpu->cull;
    WGPUBindGroupLayoutEntry first[2] = {
        {
            .binding = 8,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 10,
            .visibility = WGPUShaderStage_Compute,
            .storageTexture = {
                .access = WGPUStorageTextureAccess_WriteOnly,
                .format = WGPUTextureFormat_R32Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }
    };
    WGPUBindGroupLayoutEntry reduce[2] = {
        {
            .binding = 9,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        first[1]
    };
    WGPUBindGroupLayoutEntry history[2] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = { .type = WGPUBufferBindingType_Storage }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Compute,
            .texture = {
                .sampleType = WGPUTextureSampleType_UnfilterableFloat,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        }
    };
    c->hiz_first_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 2, .entries = first });
    c->hiz_reduce_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 2, .entries = reduce });
    c->history_layout = wgpuDeviceCreateBindGroupLayout(gpu->device,
        &(WGPUBindGroupLayoutDescriptor){ .entryCount = 2, .entries = history });
    c->hiz_first_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &c->hiz_first_layout
        });
    c->hiz_reduce_pipeline_layout = wgpuDeviceCreatePipelineLayout(gpu->device,
        &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &c->hiz_reduce_layout
        });
    c->empty_history = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Storage,
        .size = 16
    });
    c->empty_hiz = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { 1, 1, 1 },
        .format = WGPUTextureFormat_R32Float,
        .mipLevelCount = 1,
        .sampleCount = 1
    });
    c->empty_hiz_view = wgpuTextureCreateView(c->empty_hiz, NULL);
    WGPUBindGroupEntry entries[2] = {
        { .binding = 0, .buffer = c->empty_history, .size = 16 },
        { .binding = 1, .textureView = c->empty_hiz_view }
    };
    c->empty_group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = c->history_layout,
        .entryCount = 2,
        .entries = entries
    });
    ecs_map_init(&c->histories, NULL);
    ecs_map_init(&c->pyramids, NULL);
}

/** Release a pyramid and its views and bind groups.
 *
 * @param hiz The pyramid, freed
 */
static void box_gpu_hiz_free(
    box_gpu_hiz_t *hiz)
{
    uint32_t i;
    for (i = 0; i < hiz->levels; i ++) {
        if (hiz->reduce[i]) {
            wgpuBindGroupRelease(hiz->reduce[i]);
        }

        wgpuTextureViewRelease(hiz->mips[i]);
    }

    wgpuTextureViewRelease(hiz->view);
    wgpuTextureRelease(hiz->texture);
    ecs_os_free(hiz);
}

/** Release a history, its buffer and its bind group.
 *
 * @param h The history, freed
 */
static void box_gpu_hiz_history_free(
    box_gpu_cull_history_t *h)
{
    if (h->group) {
        wgpuBindGroupRelease(h->group);
    }

    if (h->buffer) {
        wgpuBufferRelease(h->buffer);
    }

    ecs_map_fini(&h->ranges);
    ecs_os_free(h);
}

/** Release a compute pipeline when it exists.
 *
 * @param pipeline The pipeline, set to NULL
 */
static void box_gpu_hiz_pipeline_release(
    WGPUComputePipeline *pipeline)
{
    if (*pipeline) {
        wgpuComputePipelineRelease(*pipeline);
        *pipeline = NULL;
    }
}

void box_gpu_hiz_fini(
    box_gpu_t *gpu)
{
    box_gpu_cull_t *c = &gpu->cull;
    ecs_map_iter_t it = ecs_map_iter(&c->pyramids);
    while (ecs_map_next(&it)) {
        box_gpu_hiz_free(ecs_map_ptr(&it));
    }

    it = ecs_map_iter(&c->histories);
    while (ecs_map_next(&it)) {
        box_gpu_hiz_history_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(&c->pyramids);
    ecs_map_fini(&c->histories);
    box_gpu_hiz_pipeline_release(&c->hiz_first);
    box_gpu_hiz_pipeline_release(&c->hiz_reduce);
    wgpuBindGroupRelease(c->empty_group);
    wgpuTextureViewRelease(c->empty_hiz_view);
    wgpuTextureRelease(c->empty_hiz);
    wgpuBufferRelease(c->empty_history);
    wgpuPipelineLayoutRelease(c->hiz_first_pipeline_layout);
    wgpuPipelineLayoutRelease(c->hiz_reduce_pipeline_layout);
    wgpuBindGroupLayoutRelease(c->hiz_first_layout);
    wgpuBindGroupLayoutRelease(c->hiz_reduce_layout);
    wgpuBindGroupLayoutRelease(c->history_layout);
    c->empty_group = NULL;
    c->history_layout = NULL;
}

void box_gpu_hiz_pipelines(
    box_gpu_t *gpu,
    WGPUShaderModule module)
{
    box_gpu_cull_t *c = &gpu->cull;
    box_gpu_hiz_pipeline_release(&c->hiz_first);
    box_gpu_hiz_pipeline_release(&c->hiz_reduce);
    c->hiz_first = wgpuDeviceCreateComputePipeline(gpu->device,
        &(WGPUComputePipelineDescriptor){
            .layout = c->hiz_first_pipeline_layout,
            .compute = {
                .module = module,
                .entryPoint = { .data = "hiz_first", .length = WGPU_STRLEN }
            }
        });
    c->hiz_reduce = wgpuDeviceCreateComputePipeline(gpu->device,
        &(WGPUComputePipelineDescriptor){
            .layout = c->hiz_reduce_pipeline_layout,
            .compute = {
                .module = module,
                .entryPoint = { .data = "hiz_reduce", .length = WGPU_STRLEN }
            }
        });
}

/** Get the largest power of two that is not larger than a size.
 *
 * @param size The size, at least 1
 * @return The power of two
 */
static uint32_t box_gpu_hiz_floor_pow2(
    uint32_t size)
{
    uint32_t result = 1;
    while (result * 2 <= size) {
        result *= 2;
    }

    return result;
}

/** Create a pyramid for a first level size.
 *
 * @param gpu The backend
 * @param width Width of the first level, a power of two
 * @param height Height of the first level, a power of two
 * @return The pyramid
 */
static box_gpu_hiz_t* box_gpu_hiz_create(
    box_gpu_t *gpu,
    uint32_t width,
    uint32_t height)
{
    box_gpu_cull_t *c = &gpu->cull;
    box_gpu_hiz_t *hiz = ecs_os_calloc_t(box_gpu_hiz_t);
    uint32_t i, size = width > height ? width : height;
    hiz->width = width;
    hiz->height = height;
    hiz->levels = 1;
    while (size > 1 && hiz->levels < BOX_GPU_HIZ_LEVELS) {
        size /= 2;
        hiz->levels ++;
    }

    hiz->id = ++ c->next_hiz;
    hiz->texture = wgpuDeviceCreateTexture(gpu->device, &(WGPUTextureDescriptor){
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { width, height, 1 },
        .format = WGPUTextureFormat_R32Float,
        .mipLevelCount = hiz->levels,
        .sampleCount = 1
    });
    hiz->view = wgpuTextureCreateView(hiz->texture, NULL);
    for (i = 0; i < hiz->levels; i ++) {
        hiz->mips[i] = wgpuTextureCreateView(hiz->texture, &(WGPUTextureViewDescriptor){
            .format = WGPUTextureFormat_R32Float,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = i,
            .mipLevelCount = 1,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_All
        });
    }

    for (i = 1; i < hiz->levels; i ++) {
        WGPUBindGroupEntry entries[2] = {
            { .binding = 9, .textureView = hiz->mips[i - 1] },
            { .binding = 10, .textureView = hiz->mips[i] }
        };
        hiz->reduce[i] = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
            .layout = c->hiz_reduce_layout,
            .entryCount = 2,
            .entries = entries
        });
    }

    return hiz;
}

box_gpu_hiz_t* box_gpu_hiz_get(
    box_gpu_t *gpu,
    int32_t width,
    int32_t height)
{
    box_gpu_cull_t *c = &gpu->cull;
    uint32_t w = box_gpu_hiz_floor_pow2(width > 0 ? (uint32_t)width : 1);
    uint32_t h = box_gpu_hiz_floor_pow2(height > 0 ? (uint32_t)height : 1);
    uint64_t key = ((uint64_t)w << 32) | h;
    box_gpu_hiz_t *result = ecs_map_get_deref(&c->pyramids, box_gpu_hiz_t, key);
    if (!result) {
        result = box_gpu_hiz_create(gpu, w, h);
        ecs_map_insert_ptr(&c->pyramids, key, result);
    }

    result->stamp = c->stamp;

    return result;
}

box_gpu_cull_history_t* box_gpu_hiz_history(
    box_gpu_t *gpu,
    ecs_entity_t view,
    uint32_t target)
{
    box_gpu_cull_t *c = &gpu->cull;
    uint64_t key = (uint64_t)view ^ ((uint64_t)target * 0x9E3779B97F4A7C15ull);
    box_gpu_cull_history_t *result = ecs_map_get_deref(
        &c->histories, box_gpu_cull_history_t, key);
    if (!result) {
        result = ecs_os_calloc_t(box_gpu_cull_history_t);
        ecs_map_init(&result->ranges, NULL);
        ecs_map_insert_ptr(&c->histories, key, result);
    }

    result->stamp = c->stamp;

    return result;
}

/** Get the history words an instance buffer of a batch needs, one per instance it holds.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param index Index of the view in the packet
 * @param b The batch
 * @return The words, 0 when the view does not draw the batch with a history
 */
static uint32_t box_gpu_hiz_words(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    int32_t index,
    const box_render_batch_t *b)
{
    const box_render_view_t *v = ecs_vec_get_t(&packet->views, box_render_view_t, index);
    uint32_t result = 0;
    if (b->view != -1 || b->scene != v->scene || b->pipeline != BOX_RENDER_SHADER_MESH) {
        goto done;
    }

    box_gpu_buffer_t *inst = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, b->instances);
    if (inst && (inst->usage & BOX_RENDER_BUFFER_STORAGE)) {
        result = inst->size / (uint32_t)sizeof(box_render_instance_t);
    }

done:
    return result;
}

/** Check whether a history range holds the words of an instance buffer.
 *
 * @param h The history
 * @param buffer Handle of the instance buffer
 * @param words Words it needs
 * @return Whether the buffer has a range that large
 */
static bool box_gpu_hiz_fits(
    const box_gpu_cull_history_t *h,
    uint32_t buffer,
    uint32_t words)
{
    const ecs_map_val_t *range = ecs_map_get(&h->ranges, buffer);

    return range && (uint32_t)(*range & 0xFFFFFFFF) >= words;
}

/** Start a history over in a buffer with room for a number of words.
 *
 * @param gpu The backend
 * @param h The history
 * @param words Words the ranges of this frame need
 */
static void box_gpu_hiz_restart(
    box_gpu_t *gpu,
    box_gpu_cull_history_t *h,
    uint32_t words)
{
    uint32_t capacity = h->capacity ? h->capacity : BOX_GPU_HIZ_MIN_WORDS;
    while (capacity < words * 2) {
        capacity *= 2;
    }

    if (h->group) {
        wgpuBindGroupRelease(h->group);
        h->group = NULL;
    }

    if (h->buffer) {
        wgpuBufferRelease(h->buffer);
    }

    h->buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Storage,
        .size = (uint64_t)capacity * sizeof(uint32_t)
    });
    h->capacity = capacity;
    h->used = 0;
    ecs_map_clear(&h->ranges);
}

void box_gpu_hiz_ranges(
    box_gpu_t *gpu,
    box_gpu_cull_history_t *h,
    const box_render_packet_t *packet,
    int32_t index)
{
    int32_t i, count = ecs_vec_count(&packet->batches);
    uint64_t missing = 0, total = 0;
    for (i = 0; i < count; i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&packet->batches, box_render_batch_t, i);
        uint32_t words = box_gpu_hiz_words(gpu, packet, index, b);
        total += words;
        if (words && !box_gpu_hiz_fits(h, b->instances, words)) {
            missing += words;
        }
    }

    if (!h->buffer || h->used + missing > h->capacity) {
        box_gpu_hiz_restart(gpu, h, (uint32_t)total);
    }

    for (i = 0; i < count; i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&packet->batches, box_render_batch_t, i);
        uint32_t words = box_gpu_hiz_words(gpu, packet, index, b);
        if (!words || box_gpu_hiz_fits(h, b->instances, words)) {
            continue;
        }

        ecs_map_ensure(&h->ranges, b->instances)[0] = ((uint64_t)h->used << 32) | words;
        h->used += words;
    }
}

uint32_t box_gpu_hiz_offset(
    const box_gpu_cull_history_t *h,
    uint32_t buffer)
{
    const ecs_map_val_t *range = ecs_map_get(&h->ranges, buffer);

    return range ? (uint32_t)(*range >> 32) : UINT32_MAX;
}

WGPUBindGroup box_gpu_hiz_group(
    box_gpu_t *gpu,
    box_gpu_cull_history_t *h,
    box_gpu_hiz_t *hiz)
{
    if (h->group && h->group_hiz == hiz->id) {
        goto done;
    }

    if (h->group) {
        wgpuBindGroupRelease(h->group);
    }

    WGPUBindGroupEntry entries[2] = {
        {
            .binding = 0, .buffer = h->buffer,
            .size = (uint64_t)h->capacity * sizeof(uint32_t)
        },
        { .binding = 1, .textureView = hiz->view }
    };
    h->group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = gpu->cull.history_layout,
        .entryCount = 2,
        .entries = entries
    });
    h->group_hiz = hiz->id;
done:
    return h->group;
}

/** Record one dispatch of a pyramid shader over the texels of a level.
 *
 * @param pass The compute pass
 * @param pipeline The pipeline
 * @param group Its bind group
 * @param hiz The pyramid
 * @param level The level it writes
 */
static void box_gpu_hiz_dispatch(
    WGPUComputePassEncoder pass,
    WGPUComputePipeline pipeline,
    WGPUBindGroup group,
    const box_gpu_hiz_t *hiz,
    uint32_t level)
{
    uint32_t w = hiz->width >> level, h = hiz->height >> level;
    w = w ? w : 1;
    h = h ? h : 1;
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, (w + BOX_GPU_HIZ_GROUP - 1) / BOX_GPU_HIZ_GROUP,
        (h + BOX_GPU_HIZ_GROUP - 1) / BOX_GPU_HIZ_GROUP, 1);
}

void box_gpu_hiz_build(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    box_gpu_hiz_t *hiz,
    WGPUTextureView depth)
{
    box_gpu_cull_t *c = &gpu->cull;
    uint32_t i;
    if (!c->hiz_first || !c->hiz_reduce) {
        goto done;
    }

    WGPUBindGroupEntry entries[2] = {
        { .binding = 8, .textureView = depth },
        { .binding = 10, .textureView = hiz->mips[0] }
    };
    WGPUBindGroup first = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
        .layout = c->hiz_first_layout,
        .entryCount = 2,
        .entries = entries
    });
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    box_gpu_hiz_dispatch(pass, c->hiz_first, first, hiz, 0);
    for (i = 1; i < hiz->levels; i ++) {
        box_gpu_hiz_dispatch(pass, c->hiz_reduce, hiz->reduce[i], hiz, i);
    }

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuBindGroupRelease(first);
done:
    return;
}

void box_gpu_hiz_purge(
    box_gpu_t *gpu)
{
    box_gpu_cull_t *c = &gpu->cull;
    ecs_map_iter_t it = ecs_map_iter(&c->pyramids);
    while (ecs_map_next(&it)) {
        box_gpu_hiz_t *hiz = ecs_map_ptr(&it);
        if (c->stamp - hiz->stamp > BOX_GPU_HIZ_IDLE) {
            box_gpu_hiz_free(hiz);
            ecs_map_remove(&c->pyramids, ecs_map_key(&it));
            it = ecs_map_iter(&c->pyramids);
        }
    }

    it = ecs_map_iter(&c->histories);
    while (ecs_map_next(&it)) {
        box_gpu_cull_history_t *h = ecs_map_ptr(&it);
        if (c->stamp - h->stamp > BOX_GPU_HIZ_IDLE) {
            box_gpu_hiz_history_free(h);
            ecs_map_remove(&c->histories, ecs_map_key(&it));
            it = ecs_map_iter(&c->histories);
        }
    }
}

#endif
