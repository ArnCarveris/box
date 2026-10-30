/**
 * @file render/gpu_bake_time.c
 * @brief GPU time of the probe and reflection captures and the environment processing of the
 * webgpu backend and the results the main thread takes.
 *
 * Timestamps around the captures of each kind in a packet are read back without waiting, or
 * the CPU measures until the queue reports their work done when the device has no timestamps.
 */

#include "gpu_bake.h"
#include "environment.h"

#ifdef BOX_RENDER_WGPU

/** Create the timestamp queries and their readbacks when the device has the feature.
 *
 * @param gpu The backend
 * @param b The capture state
 */
void box_gpu_bake_queries_init(
    box_gpu_t *gpu,
    box_gpu_bake_t *b)
{
    int32_t i;
    if (!gpu->timestamps) {
        goto done;
    }

    b->queries = wgpuDeviceCreateQuerySet(gpu->device, &(WGPUQuerySetDescriptor){
        .type = WGPUQueryType_Timestamp,
        .count = BOX_GPU_BAKE_QUERIES * BOX_GPU_BAKE_READBACKS
    });
    b->resolve = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc,
        .size = 2 * BOX_GPU_BAKE_RESOLVE_ALIGN * BOX_GPU_BAKE_READBACKS
    });
    for (i = 0; i < BOX_GPU_BAKE_READBACKS; i ++) {
        b->readbacks[i].buffer = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
            .size = 2 * sizeof(uint64_t)
        });
    }

done:
    return;
}

/** Release the timestamp queries and their readbacks.
 *
 * @param b The capture state
 */
void box_gpu_bake_queries_fini(
    box_gpu_bake_t *b)
{
    int32_t i;
    for (i = 0; i < BOX_GPU_BAKE_READBACKS; i ++) {
        box_gpu_bake_readback_t *r = &b->readbacks[i];
        if (r->state == BOX_GPU_BAKE_MAPPED) {
            wgpuBufferUnmap(r->buffer);
        }

        if (r->buffer) {
            wgpuBufferRelease(r->buffer);
        }
    }

    if (b->queries) {
        wgpuQuerySetRelease(b->queries);
    }

    if (b->resolve) {
        wgpuBufferRelease(b->resolve);
    }
}

/** Record the outcome of the map of a timestamp readback.
 *
 * @param status Outcome of the request
 * @param message Error text
 * @param userdata1 The box_gpu_bake_readback_t
 * @param userdata2 Unused
 */
static void box_gpu_bake_on_map(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_bake_readback_t *r = userdata1;
    (void)message;
    (void)userdata2;
    if (status != WGPUMapAsyncStatus_CallbackCancelled) {
        r->state = status == WGPUMapAsyncStatus_Success
            ? BOX_GPU_BAKE_MAPPED
            : BOX_GPU_BAKE_FAILED;
    }
}

/** Add a measurement of the captures of a packet to the time the main thread takes.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param kind box_gpu_bake_kind_t of the captures
 * @param probes Probes the captures held
 * @param ms Milliseconds they took, dropped when not plausible
 */
static void box_gpu_bake_measured(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    int32_t kind,
    int32_t probes,
    double ms)
{
    if (probes > 0 && ms > 0 && ms < BOX_GPU_BAKE_MEASURE_MAX) {
        ecs_os_mutex_lock(gpu->lock);
        b->timing[kind].probes += probes;
        b->timing[kind].ms += ms;
        ecs_os_mutex_unlock(gpu->lock);
    }
}

/** Measure the captures of a packet when the queue finished their work.
 *
 * @param status Outcome of the request
 * @param message Error text
 * @param userdata1 The box_gpu_bake_done_t
 * @param userdata2 Unused
 */
static void box_gpu_bake_on_done(
    WGPUQueueWorkDoneStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_bake_done_t *d = userdata1;
    (void)message;
    (void)userdata2;
    if (status == WGPUQueueWorkDoneStatus_Success) {
        box_gpu_bake_measured(d->gpu, d->bake, d->kind, d->probes,
            (double)(ecs_os_now() - d->start) * 1e-6);
    }

    if (status != WGPUQueueWorkDoneStatus_CallbackCancelled) {
        d->probes = 0;
    }
}

/** Get the nanoseconds of one tick of the timestamps.
 *
 * @param gpu The backend
 * @return The period, 1 in the browser, which resolves timestamps in nanoseconds
 */
static double box_gpu_bake_period(
    box_gpu_t *gpu)
{
#ifdef __EMSCRIPTEN__
    (void)gpu;

    return 1.0;
#else
    return (double)wgpuQueueGetTimestampPeriod(gpu->queue);
#endif
}

/** Map the timestamp copies that were submitted and publish the ones that arrived.
 *
 * @param gpu The backend
 * @param b The capture state
 */
void box_gpu_bake_readback_poll(
    box_gpu_t *gpu,
    box_gpu_bake_t *b)
{
    int32_t i;
#ifndef __EMSCRIPTEN__
    for (i = 0; i < BOX_GPU_BAKE_READBACKS; i ++) {
        if (b->readbacks[i].state == BOX_GPU_BAKE_MAPPING || b->done[i].probes) {
            wgpuDevicePoll(gpu->device, false, NULL);
            break;
        }
    }
#endif

    for (i = 0; i < BOX_GPU_BAKE_READBACKS; i ++) {
        box_gpu_bake_readback_t *r = &b->readbacks[i];
        if (r->state == BOX_GPU_BAKE_MAPPED) {
            const uint64_t *t = wgpuBufferGetConstMappedRange(r->buffer, 0,
                2 * sizeof(uint64_t));
            if (t && t[1] > t[0]) {
                box_gpu_bake_measured(gpu, b, r->kind, r->probes,
                    (double)(t[1] - t[0]) * box_gpu_bake_period(gpu) * 1e-6);
            }

            wgpuBufferUnmap(r->buffer);
            r->state = BOX_GPU_BAKE_FREE;
        } else if (r->state == BOX_GPU_BAKE_FAILED) {
            r->state = BOX_GPU_BAKE_FREE;
        } else if (r->state == BOX_GPU_BAKE_COPIED) {
            r->state = BOX_GPU_BAKE_MAPPING;
            wgpuBufferMapAsync(r->buffer, WGPUMapMode_Read, 0, 2 * sizeof(uint64_t),
                (WGPUBufferMapCallbackInfo){
                    .mode = WGPUCallbackMode_AllowSpontaneous,
                    .callback = box_gpu_bake_on_map,
                    .userdata1 = r
                });
        }
    }
}

const WGPUPassTimestampWrites* box_gpu_bake_writes(
    const box_gpu_bake_t *b,
    int32_t readback,
    bool first,
    bool last,
    WGPUPassTimestampWrites *out)
{
    uint32_t query = (uint32_t)readback * BOX_GPU_BAKE_QUERIES;
    *out = (WGPUPassTimestampWrites){
        .querySet = b->queries,
        .beginningOfPassWriteIndex = first ? query : query + 2,
        .endOfPassWriteIndex = last ? query + 3 : query + 1
    };

    return out;
}

/** Find a free timestamp readback.
 *
 * @param b The capture state
 * @return Index of the readback, -1 without timestamps or when every one is in flight
 */
int32_t box_gpu_bake_readback(
    box_gpu_bake_t *b)
{
    int32_t i, result = -1;
    for (i = 0; b->queries && i < BOX_GPU_BAKE_READBACKS; i ++) {
        if (b->readbacks[i].state == BOX_GPU_BAKE_FREE) {
            result = i;
            break;
        }
    }

    return result;
}

/** Resolve the timestamps of the readbacks whose captures were submitted before the last
 * packet and copy them into their readbacks; the ones recorded earlier in this packet are
 * resolved by the next one.
 *
 * Metal writes the timestamps of a pass when the pass completed, which a resolve in the same
 * or the next command buffer does not wait for, so the resolve waits for one more packet.
 *
 * @param gpu The backend
 * @param b The capture state
 */
void box_gpu_bake_resolve(
    box_gpu_t *gpu,
    box_gpu_bake_t *b)
{
    WGPUCommandEncoder encoder = NULL;
    int32_t r;
    uint32_t i;
    for (r = 0; r < BOX_GPU_BAKE_READBACKS; r ++) {
        box_gpu_bake_readback_t *rb = &b->readbacks[r];
        uint64_t offset = (uint64_t)r * 2 * BOX_GPU_BAKE_RESOLVE_ALIGN;
        if (rb->state != BOX_GPU_BAKE_WRITTEN) {
            continue;
        }

        if (!encoder) {
            encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
        }

        for (i = 0; i < 2; i ++) {
            wgpuCommandEncoderResolveQuerySet(encoder, b->queries,
                (uint32_t)r * BOX_GPU_BAKE_QUERIES + i * (BOX_GPU_BAKE_QUERIES - 1), 1,
                b->resolve, offset + i * BOX_GPU_BAKE_RESOLVE_ALIGN);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, b->resolve,
                offset + i * BOX_GPU_BAKE_RESOLVE_ALIGN, rb->buffer,
                i * sizeof(uint64_t), sizeof(uint64_t));
        }

        rb->state = BOX_GPU_BAKE_COPIED;
    }

    for (r = 0; r < BOX_GPU_BAKE_READBACKS; r ++) {
        if (b->readbacks[r].state == BOX_GPU_BAKE_RECORDED) {
            b->readbacks[r].state = BOX_GPU_BAKE_WRITTEN;
        }
    }

    if (encoder) {
        box_gpu_bake_submit(gpu, encoder);
    }
}

/** Measure captures from the CPU until the queue reports their work done, the fallback of
 * devices without timestamp queries. The time includes queued work before the captures and,
 * natively, the wait for the next poll, so it errs on the expensive side.
 *
 * @param gpu The backend
 * @param b The capture state
 * @param kind box_gpu_bake_kind_t of the captures
 * @param start ecs_os_now when the captures started recording
 * @param probes Probes the captures held
 */
void box_gpu_bake_done_measure(
    box_gpu_t *gpu,
    box_gpu_bake_t *b,
    int32_t kind,
    uint64_t start,
    int32_t probes)
{
    int32_t i;
    for (i = 0; probes && i < BOX_GPU_BAKE_READBACKS; i ++) {
        box_gpu_bake_done_t *d = &b->done[i];
        if (!d->probes) {
            d->bake = b;
            d->gpu = gpu;
            d->start = start;
            d->probes = probes;
            d->kind = kind;
            wgpuQueueOnSubmittedWorkDone(gpu->queue, (WGPUQueueWorkDoneCallbackInfo){
                .mode = WGPUCallbackMode_AllowSpontaneous,
                .callback = box_gpu_bake_on_done,
                .userdata1 = d
            });
            break;
        }
    }
}

/** Take the results of one kind of captures the GPU backend of a world reported.
 *
 * @param world The world
 * @param kind box_gpu_bake_kind_t of the captures
 * @param out Receives box_render_bake_result_t
 * @return Whether the world renders with the GPU backend
 */
static bool box_gpu_bake_take(
    ecs_world_t *world,
    int32_t kind,
    ecs_vec_t *out)
{
    box_render_gpu_stats_t stats;
    bool result = box_render_gpu_stats(world, &stats);
    ecs_vec_clear(out);
    if (!result) {
        goto done;
    }

    box_gpu_t *gpu = box_render_get_backend(world)->ctx;
    ecs_os_mutex_lock(gpu->lock);
    ecs_vec_t *captured = gpu->bake ? &gpu->bake->captured[kind] : NULL;
    int32_t count = captured ? ecs_vec_count(captured) : 0;
    if (count) {
        ecs_vec_set_count_t(NULL, out, box_render_bake_result_t, count);
        ecs_os_memcpy_n(ecs_vec_first(out), ecs_vec_first(captured),
            box_render_bake_result_t, count);
        ecs_vec_clear(captured);
    }

    ecs_os_mutex_unlock(gpu->lock);
done:
    return result;
}

/** Take the GPU time of one kind of captures the GPU backend of a world measured.
 *
 * @param world The world
 * @param kind box_gpu_bake_kind_t of the captures
 * @param out Receives the time, zero when nothing arrived
 * @return Whether the world renders with the GPU backend
 */
static bool box_gpu_bake_time(
    ecs_world_t *world,
    int32_t kind,
    box_render_bake_timing_t *out)
{
    box_render_gpu_stats_t stats;
    bool result = box_render_gpu_stats(world, &stats);
    ecs_os_memset_t(out, 0, box_render_bake_timing_t);
    if (!result) {
        goto done;
    }

    box_gpu_t *gpu = box_render_get_backend(world)->ctx;
    ecs_os_mutex_lock(gpu->lock);
    out->measured = gpu->timestamps;
    if (gpu->bake) {
        out->probes = gpu->bake->timing[kind].probes;
        out->ms = gpu->bake->timing[kind].ms;
        gpu->bake->timing[kind].probes = 0;
        gpu->bake->timing[kind].ms = 0;
    }

    ecs_os_mutex_unlock(gpu->lock);
done:
    return result;
}

bool box_render_gpu_bake_captured(
    ecs_world_t *world,
    ecs_vec_t *out)
{
    return box_gpu_bake_take(world, BOX_GPU_BAKE_PROBES, out);
}

bool box_render_gpu_bake_timing(
    ecs_world_t *world,
    box_render_bake_timing_t *out)
{
    return box_gpu_bake_time(world, BOX_GPU_BAKE_PROBES, out);
}

bool box_render_gpu_reflect_captured(
    ecs_world_t *world,
    ecs_vec_t *out)
{
    return box_gpu_bake_take(world, BOX_GPU_BAKE_REFLECT, out);
}

bool box_render_gpu_reflect_timing(
    ecs_world_t *world,
    box_render_bake_timing_t *out)
{
    return box_gpu_bake_time(world, BOX_GPU_BAKE_REFLECT, out);
}

bool box_render_gpu_environment_timing(
    ecs_world_t *world,
    box_render_bake_timing_t *out)
{
    return box_gpu_bake_time(world, BOX_GPU_BAKE_ENVIRONMENT, out);
}
#else


bool box_render_gpu_bake_captured(
    ecs_world_t *world,
    ecs_vec_t *out)
{
    (void)world;
    ecs_vec_clear(out);

    return false;
}

bool box_render_gpu_bake_timing(
    ecs_world_t *world,
    box_render_bake_timing_t *out)
{
    (void)world;
    ecs_os_memset_t(out, 0, box_render_bake_timing_t);

    return false;
}


bool box_render_gpu_reflect_captured(
    ecs_world_t *world,
    ecs_vec_t *out)
{
    (void)world;
    ecs_vec_clear(out);

    return false;
}

bool box_render_gpu_reflect_timing(
    ecs_world_t *world,
    box_render_bake_timing_t *out)
{
    (void)world;
    ecs_os_memset_t(out, 0, box_render_bake_timing_t);

    return false;
}

bool box_render_gpu_environment_timing(
    ecs_world_t *world,
    box_render_bake_timing_t *out)
{
    (void)world;
    ecs_os_memset_t(out, 0, box_render_bake_timing_t);

    return false;
}

#endif
