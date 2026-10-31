/**
 * @file render/gpu_copy.c
 * @brief Copies between buffers of the webgpu backend and the staging buffer they share.
 *
 * A copy between different buffers whose source no other copy of its packet writes runs as one
 * copyBufferToBuffer. The others go through a staging buffer, so every copy reads its source as
 * it was before the copies of the packet.
 *
 * The staging buffer is released, or shrunk, after BOX_RENDER_COPY_STAGING_FRAMES packets, see
 * box_gpu_t.copy_staging.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

#include <stdlib.h>

/** Bytes of the smallest staging buffer */
#define BOX_GPU_COPY_STAGING_MIN ((uint64_t)64 * 1024)

void box_gpu_copy_init(
    box_gpu_t *gpu)
{
    ecs_vec_init_t(NULL, &gpu->copy_writes, box_gpu_copy_write_t, 0);
    ecs_vec_init_t(NULL, &gpu->copy_modes, box_gpu_copy_mode_t, 0);
    gpu->copy_staging = NULL;
    gpu->copy_staging_size = 0;
    gpu->copy_staging_peak = 0;
    gpu->copy_staging_frames = 0;
    gpu->copy_staging_idle = 0;
}

void box_gpu_copy_fini(
    box_gpu_t *gpu)
{
    if (gpu->copy_staging) {
        wgpuBufferRelease(gpu->copy_staging);
        gpu->copy_staging = NULL;
    }

    gpu->copy_staging_size = 0;
    gpu->copy_staging_peak = 0;
    gpu->copy_staging_frames = 0;
    gpu->copy_staging_idle = 0;
    ecs_vec_fini_t(NULL, &gpu->copy_writes, box_gpu_copy_write_t);
    ecs_vec_fini_t(NULL, &gpu->copy_modes, box_gpu_copy_mode_t);
}

/** Get the size of a staging buffer that holds the staged bytes of a packet.
 *
 * @param size Bytes it must hold, at most BOX_RENDER_COPY_MAX
 * @return The smallest power of two from BOX_GPU_COPY_STAGING_MIN that holds them, at most
 *         BOX_RENDER_COPY_MAX
 */
static uint64_t box_gpu_copy_capacity(
    uint64_t size)
{
    uint64_t result = BOX_GPU_COPY_STAGING_MIN;
    while (result < size) {
        result *= 2;
    }

    return result > BOX_RENDER_COPY_MAX ? BOX_RENDER_COPY_MAX : result;
}

/** Create the staging buffer the copies of a packet go through again with a size.
 *
 * @param gpu The backend
 * @param capacity Bytes of the buffer, 0 to only release it
 */
static void box_gpu_copy_staging_resize(
    box_gpu_t *gpu,
    uint64_t capacity)
{
    if (gpu->copy_staging) {
        wgpuBufferRelease(gpu->copy_staging);
        gpu->copy_staging = NULL;
    }

    if (capacity) {
        gpu->copy_staging = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst,
            .size = capacity
        });
    }

    gpu->copy_staging_size = capacity;
}

/** Get the staging buffer the copies of a packet go through, holding at least a size.
 *
 * @param gpu The backend
 * @param size Bytes it must hold
 * @return The buffer
 */
static WGPUBuffer box_gpu_copy_staging(
    box_gpu_t *gpu,
    uint64_t size)
{
    if (gpu->copy_staging_size < size) {
        box_gpu_copy_staging_resize(gpu, box_gpu_copy_capacity(size));
    }

    return gpu->copy_staging;
}

/** Count a packet for the release of the staging buffer.
 *
 * It is released after BOX_RENDER_COPY_STAGING_FRAMES packets in a row that staged nothing.
 * Every BOX_RENDER_COPY_STAGING_FRAMES packets it is shrunk to what the largest staged bytes of
 * a packet among them need, when that is smaller.
 *
 * @param gpu The backend
 * @param staged Bytes the packet staged
 */
static void box_gpu_copy_staging_trim(
    box_gpu_t *gpu,
    uint64_t staged)
{
    uint64_t capacity;
    if (staged) {
        gpu->copy_staging_idle = 0;
    } else if (gpu->copy_staging_idle < BOX_RENDER_COPY_STAGING_FRAMES) {
        gpu->copy_staging_idle ++;
    }

    if (staged > gpu->copy_staging_peak) {
        gpu->copy_staging_peak = staged;
    }

    gpu->copy_staging_frames ++;
    if (gpu->copy_staging && gpu->copy_staging_idle >= BOX_RENDER_COPY_STAGING_FRAMES) {
        box_gpu_copy_staging_resize(gpu, 0);
    }

    if (gpu->copy_staging_frames < BOX_RENDER_COPY_STAGING_FRAMES) {
        goto done;
    }

    capacity = box_gpu_copy_capacity(gpu->copy_staging_peak);
    if (gpu->copy_staging && gpu->copy_staging_peak && capacity < gpu->copy_staging_size) {
        box_gpu_copy_staging_resize(gpu, capacity);
    }

    gpu->copy_staging_frames = 0;
    gpu->copy_staging_peak = 0;
done:
    return;
}

/** Find the buffers of a copy of a packet and check the copy against them.
 *
 * @param gpu The backend
 * @param c The copy
 * @param src Receives the source buffer
 * @param dst Receives the destination buffer
 * @return Whether the copy is valid
 */
static bool box_gpu_copy_buffers(
    box_gpu_t *gpu,
    const box_render_copy_t *c,
    box_gpu_buffer_t **src,
    box_gpu_buffer_t **dst)
{
    *src = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, c->src);
    *dst = ecs_map_get_deref(&gpu->buffers, box_gpu_buffer_t, c->dst);

    return *src && *dst && box_gpu_buffer_copyable(*src) &&
        !((c->src_offset | c->dst_offset | c->size) & (BOX_RENDER_COPY_ALIGN - 1)) &&
        (uint64_t)c->src_offset + c->size <= (*src)->size &&
        (uint64_t)c->dst_offset + c->size <= (*dst)->size;
}

/** Get the bytes a copy takes in the staging buffer.
 *
 * @param c The copy
 * @return Its size rounded up to BOX_RENDER_COPY_ALIGN
 */
static uint64_t box_gpu_copy_staged(
    const box_render_copy_t *c)
{
    return ((uint64_t)c->size + BOX_RENDER_COPY_ALIGN - 1) &
        ~(uint64_t)(BOX_RENDER_COPY_ALIGN - 1);
}

/** Order the ranges copies write by buffer, then by first byte.
 *
 * @param a The first range
 * @param b The second range
 * @return Negative, zero or positive as a sorts before, with or after b
 */
static int box_gpu_copy_write_compare(
    const void *a,
    const void *b)
{
    const box_gpu_copy_write_t *wa = a, *wb = b;
    int result = (wa->start > wb->start) - (wa->start < wb->start);
    if (wa->buffer != wb->buffer) {
        result = wa->buffer < wb->buffer ? -1 : 1;
    }

    return result;
}

/** Check whether a copy of the packet writes a range of a buffer.
 *
 * The last range of the buffer that starts before the end of the range is found by bisection:
 * the ranges up to it overlap the range when the largest end among them is past its start.
 *
 * @param writes The ranges the copies write, sorted, end holding the largest end so far
 *               within each buffer
 * @param count Ranges
 * @param buffer Handle of the buffer
 * @param start First byte of the range
 * @param end Byte past the last one of the range
 * @return Whether a written range overlaps it
 */
static bool box_gpu_copy_written(
    const box_gpu_copy_write_t *writes,
    int32_t count,
    uint32_t buffer,
    uint64_t start,
    uint64_t end)
{
    int32_t lo = 0, hi = count;
    while (lo < hi) {
        int32_t mid = lo + (hi - lo) / 2;
        const box_gpu_copy_write_t *w = &writes[mid];
        if (w->buffer < buffer || (w->buffer == buffer && w->start < end)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo > 0 && writes[lo - 1].buffer == buffer && writes[lo - 1].end > start;
}

/** Decide how each copy of a packet runs, into box_gpu_t.copy_modes.
 *
 * A copy runs directly when its source and destination are different buffers and no copy of
 * the packet writes a range of its source it reads, else through the staging buffer. An invalid
 * copy is reported and skipped.
 *
 * @param gpu The backend
 * @param packet The packet
 * @return Bytes the staged copies take in the staging buffer
 */
static uint64_t box_gpu_copy_modes(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i, count = ecs_vec_count(&packet->copies), writes;
    box_gpu_buffer_t *src, *dst;
    box_gpu_copy_write_t *w;
    uint64_t result = 0;
    ecs_vec_clear(&gpu->copy_writes);
    ecs_vec_set_count_t(NULL, &gpu->copy_modes, box_gpu_copy_mode_t, count);
    box_gpu_copy_mode_t *modes = ecs_vec_first_t(&gpu->copy_modes, box_gpu_copy_mode_t);
    for (i = 0; i < count; i ++) {
        const box_render_copy_t *c = ecs_vec_get_t(&packet->copies, box_render_copy_t, i);
        modes[i] = BOX_GPU_COPY_SKIP;
        if (!box_gpu_copy_buffers(gpu, c, &src, &dst)) {
            ecs_err("copy of %u bytes from %u in buffer %u to %u in buffer %u is invalid",
                c->size, c->src_offset, c->src, c->dst_offset, c->dst);
            box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        } else if (c->size) {
            modes[i] = BOX_GPU_COPY_DIRECT;
            w = ecs_vec_append_t(NULL, &gpu->copy_writes, box_gpu_copy_write_t);
            w->buffer = c->dst;
            w->start = c->dst_offset;
            w->end = (uint64_t)c->dst_offset + c->size;
        }
    }

    writes = ecs_vec_count(&gpu->copy_writes);
    w = ecs_vec_first_t(&gpu->copy_writes, box_gpu_copy_write_t);
    if (writes > 1) {
        qsort(w, (size_t)writes, sizeof(box_gpu_copy_write_t), box_gpu_copy_write_compare);
    }

    for (i = 1; i < writes; i ++) {
        if (w[i].buffer == w[i - 1].buffer && w[i - 1].end > w[i].end) {
            w[i].end = w[i - 1].end;
        }
    }

    for (i = 0; i < count; i ++) {
        const box_render_copy_t *c = ecs_vec_get_t(&packet->copies, box_render_copy_t, i);
        if (modes[i] != BOX_GPU_COPY_SKIP && (c->src == c->dst || box_gpu_copy_written(
            w, writes, c->src, c->src_offset, (uint64_t)c->src_offset + c->size)))
        {
            modes[i] = BOX_GPU_COPY_STAGED;
            result += box_gpu_copy_staged(c);
        }
    }

    return result;
}

/** Record the copies of a packet: the sources of the staged copies into the staging buffer
 * first, then every destination in packet order, a direct copy from its source buffer, which no
 * copy writes, and a staged copy from the staging buffer.
 *
 * @param gpu The backend
 * @param packet The packet
 * @param staging The staging buffer, NULL when no copy is staged
 */
static void box_gpu_copy_record(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    WGPUBuffer staging)
{
    int32_t i, count = ecs_vec_count(&packet->copies);
    const box_gpu_copy_mode_t *modes = ecs_vec_first_t(&gpu->copy_modes, box_gpu_copy_mode_t);
    box_gpu_buffer_t *src, *dst;
    uint64_t offset = 0;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    for (i = 0; i < count; i ++) {
        const box_render_copy_t *c = ecs_vec_get_t(&packet->copies, box_render_copy_t, i);
        if (modes[i] == BOX_GPU_COPY_STAGED && box_gpu_copy_buffers(gpu, c, &src, &dst)) {
            wgpuCommandEncoderCopyBufferToBuffer(encoder, src->buffer, c->src_offset,
                staging, offset, c->size);
            offset += box_gpu_copy_staged(c);
        }
    }

    offset = 0;
    for (i = 0; i < count; i ++) {
        const box_render_copy_t *c = ecs_vec_get_t(&packet->copies, box_render_copy_t, i);
        if (modes[i] == BOX_GPU_COPY_SKIP || !box_gpu_copy_buffers(gpu, c, &src, &dst)) {
            continue;
        }

        if (modes[i] == BOX_GPU_COPY_DIRECT) {
            wgpuCommandEncoderCopyBufferToBuffer(encoder, src->buffer, c->src_offset,
                dst->buffer, c->dst_offset, c->size);
            box_gpu_count(gpu, BOX_GPU_STAT_COPY_DIRECT);
        } else {
            wgpuCommandEncoderCopyBufferToBuffer(encoder, staging, offset,
                dst->buffer, c->dst_offset, c->size);
            offset += box_gpu_copy_staged(c);
            box_gpu_count(gpu, BOX_GPU_STAT_COPY_STAGED);
        }

        dst->version = ++ gpu->buffer_writes;
    }

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
}

void box_gpu_copy(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i, count = ecs_vec_count(&packet->copies);
    uint64_t total = 0, staged = 0;
    if (!count) {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        total += box_gpu_copy_staged(ecs_vec_get_t(&packet->copies, box_render_copy_t, i));
    }

    if (total > BOX_RENDER_COPY_MAX) {
        ecs_err("copies of %llu bytes exceed the %llu bytes of a packet",
            (unsigned long long)total, (unsigned long long)BOX_RENDER_COPY_MAX);
        box_gpu_count(gpu, BOX_GPU_STAT_ERRORS);
        goto done;
    }

    staged = box_gpu_copy_modes(gpu, packet);
    box_gpu_copy_record(gpu, packet, staged ? box_gpu_copy_staging(gpu, staged) : NULL);
done:
    box_gpu_copy_staging_trim(gpu, staged);

    return;
}

#endif
