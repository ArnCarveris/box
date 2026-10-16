/**
 * @file render/gpu_pool.c
 * @brief Instance pool of the cull pass of the webgpu backend.
 *
 * The cull pass reads the model matrices of the instances of every draw, and each batch has its
 * own instance buffer. The pool holds a copy of every instance buffer the cull reads at its own
 * range, so one dispatch culls all the draws of a table instead of one dispatch per draw.
 *
 * A range is copied again only when its buffer was written since, so a static scene copies its
 * buffers once. A buffer that does not fit the binding window keeps a dispatch on its own buffer.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

void box_gpu_cull_pool_reset(
    box_gpu_cull_t *c)
{
    ecs_map_iter_t it = ecs_map_iter(&c->pool_ranges);
    while (ecs_map_next(&it)) {
        ecs_os_free(ecs_map_ptr(&it));
    }

    ecs_map_clear(&c->pool_ranges);
    c->pool_used = 0;
    c->pool_waste = 0;
}

void box_gpu_cull_pool_purge(
    box_gpu_t *gpu)
{
    box_gpu_cull_t *c = &gpu->cull;
    ecs_map_iter_t it = ecs_map_iter(&c->pool_ranges);
    while (ecs_map_next(&it)) {
        box_gpu_cull_pool_range_t *r = ecs_map_ptr(&it);
        if (!ecs_map_get(&gpu->buffers, ecs_map_key(&it))) {
            c->pool_waste += r->size;
            ecs_os_free(r);
            ecs_map_remove(&c->pool_ranges, ecs_map_key(&it));
            it = ecs_map_iter(&c->pool_ranges);
        }
    }
}

uint32_t box_gpu_cull_pool_add(
    box_gpu_t *gpu,
    uint32_t handle)
{
    box_gpu_cull_t *c = &gpu->cull;
    box_gpu_buffer_t *b = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, handle);
    box_gpu_cull_pool_range_t *r = ecs_map_get_deref(
        &c->pool_ranges, box_gpu_cull_pool_range_t, handle);
    uint64_t stride = sizeof(box_render_instance_t);
    uint64_t limit = (uint64_t)c->window * sizeof(uint32_t);
    uint32_t result = UINT32_MAX;
    uint64_t size;
    if (!b) {
        goto done;
    }

    size = (b->size + stride - 1) / stride * stride;
    if (r && r->size >= b->size) {
        result = (uint32_t)(r->offset / stride);
        goto done;
    }

    if (c->pool_used + size > limit) {
        goto done;
    }

    if (r) {
        c->pool_waste += r->size;
    } else {
        r = ecs_os_calloc_t(box_gpu_cull_pool_range_t);
        ecs_map_insert_ptr(&c->pool_ranges, handle, r);
    }

    r->offset = c->pool_used;
    r->size = size;
    r->version = 0;
    c->pool_used += size;
    result = (uint32_t)(r->offset / stride);
done:
    return result;
}

bool box_gpu_cull_pooled(
    box_gpu_t *gpu,
    uint32_t handle)
{
    box_gpu_buffer_t *b = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, handle);
    box_gpu_cull_pool_range_t *r = ecs_map_get_deref(
        &gpu->cull.pool_ranges, box_gpu_cull_pool_range_t, handle);

    return b && r && r->size >= b->size;
}

/** Make the pool buffer hold the ranges, creating it again by doubling when it is too small.
 *
 * @param gpu The backend
 * @return Whether the buffer was created, so every range must be copied again
 */
static bool box_gpu_cull_pool_reserve(
    box_gpu_t *gpu)
{
    box_gpu_cull_t *c = &gpu->cull;
    uint64_t capacity = c->pool_size ? c->pool_size : BOX_GPU_POOL_MIN_SIZE;
    bool result = false;
    if (c->pool && c->pool_used <= c->pool_size) {
        goto done;
    }

    while (capacity < c->pool_used) {
        capacity *= 2;
    }

    if (c->pool) {
        wgpuBufferRelease(c->pool);
    }

    if (c->pool_group) {
        wgpuBindGroupRelease(c->pool_group);
        c->pool_group = NULL;
    }

    c->pool = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = capacity
    });
    c->pool_size = capacity;
    result = true;
done:
    return result;
}

void box_gpu_cull_pool_upload(
    box_gpu_t *gpu)
{
    box_gpu_cull_t *c = &gpu->cull;
    WGPUCommandEncoder encoder = NULL;
    uint64_t limit = (uint64_t)c->window * sizeof(uint32_t);
    bool created;
    ecs_map_iter_t it;
    if (!c->pool_used) {
        goto done;
    }

    created = box_gpu_cull_pool_reserve(gpu);
    it = ecs_map_iter(&c->pool_ranges);
    while (ecs_map_next(&it)) {
        box_gpu_cull_pool_range_t *r = ecs_map_ptr(&it);
        box_gpu_buffer_t *b = ecs_map_get_deref(
            &gpu->buffers, box_gpu_buffer_t, ecs_map_key(&it));
        if (!b || (!created && b->version == r->version) || b->size > r->size) {
            continue;
        }

        if (!encoder) {
            encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
        }

        wgpuCommandEncoderCopyBufferToBuffer(encoder, b->buffer, 0, c->pool, r->offset, b->size);
        r->version = b->version;
    }

    if (encoder) {
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuQueueSubmit(gpu->queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
    }

    if (!c->pool_group) {
        WGPUBindGroupEntry entry = {
            .binding = 0,
            .buffer = c->pool,
            .size = c->pool_size < limit ? c->pool_size : limit
        };
        c->pool_group = wgpuDeviceCreateBindGroup(gpu->device, &(WGPUBindGroupDescriptor){
            .layout = gpu->instance_layout,
            .entryCount = 1,
            .entries = &entry
        });
    }

done:
    return;
}

#endif
