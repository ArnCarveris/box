/**
 * @file renderer/timing.c
 * @brief Records the timestamps of every pass of a frame and resolves them later.
 *
 * The collector keeps the names of the passes of the frame in flight. The
 * encoder of the next frame resolves their timestamps into a mappable buffer,
 * because the counters of a pass are only complete once its command buffer is,
 * and the map of that buffer publishes the milliseconds per pass name. Without
 * the device feature nothing is created and every pass is recorded plainly.
 */

#include "renderer_internal.h"
#include <string.h>

/** Copy a pass name into a fixed buffer, truncating what does not fit.
 *
 * @param dst The buffer, BOX_TIMING_NAME_MAX characters
 * @param name The name to copy
 */
static void box_timing_name(
    char *dst,
    const char *name)
{
    uint32_t i = 0;
    while (i + 1 < BOX_TIMING_NAME_MAX && name[i]) {
        dst[i] = name[i];
        i ++;
    }

    dst[i] = '\0';
}

/** Add the time of a pass to the published entry of its name.
 *
 * Several passes of a frame share a name when a frame has more than one view
 * or a shadow map more than one cascade, and the entry of that name is what
 * they cost together.
 *
 * @param t The collector
 * @param name Name of the pass
 * @param time_ms Milliseconds the pass spent
 */
static void box_timing_merge(
    box_timing_t *t,
    const char *name,
    double time_ms)
{
    uint32_t i;
    for (i = 0; i < t->count; i ++) {
        if (strcmp(t->passes[i].name, name)) {
            continue;
        }

        t->passes[i].time_ms += time_ms;
        goto done;
    }

    if (t->count < BOX_TIMING_PASS_MAX) {
        box_timing_name(t->passes[t->count].name, name);
        t->passes[t->count].time_ms = time_ms;
        t->count ++;
    }

done:
    return;
}

bool box_timing_begin(
    box_timing_t *t)
{
    bool result = !t->pending;
    if (result) {
        t->recorded_count = 0;
        t->recorded_frame = t->frame;
        t->recording = true;
        t->frame ++;
    }

    return result;
}

int32_t box_timing_add(
    box_timing_t *t,
    const char *name)
{
    int32_t result = -1;
    if (!t->recording) {
        goto done;
    }

    if (t->recorded_count >= BOX_TIMING_PASS_MAX) {
        t->dropped ++;
        goto done;
    }

    box_timing_name(t->recorded[t->recorded_count].name, name);
    t->recorded[t->recorded_count].time_ms = 0.0;
    result = (int32_t)(t->recorded_count * 2);
    t->recorded_count ++;
done:
    return result;
}

bool box_timing_end(
    box_timing_t *t)
{
    bool result = false;
    if (!t->recording) {
        goto done;
    }

    t->recording = false;
    result = t->recorded_count != 0;
    t->pending = result;
done:
    return result;
}

void box_timing_resolve(
    box_timing_t *t,
    const uint64_t *stamps,
    uint32_t count)
{
    uint32_t passes = count / 2;
    if (passes > t->recorded_count) {
        passes = t->recorded_count;
    }

    t->count = 0;
    t->frame_ms = 0.0;
    uint32_t i;
    for (i = 0; i < passes; i ++) {
        uint64_t begin = stamps[i * 2];
        uint64_t end = stamps[i * 2 + 1];
        double time_ms = end > begin ? (double)(end - begin) / BOX_TIMING_NS_MS : 0.0;
        box_timing_merge(t, t->recorded[i].name, time_ms);
        t->frame_ms += time_ms;
    }

    t->resolved = t->recorded_frame;
    t->pending = false;
}

void box_timing_drop(
    box_timing_t *t)
{
    t->pending = false;
}

double box_timing_get(
    const box_timing_t *t,
    const char *name)
{
    double result = 0.0;
    uint32_t i;
    for (i = 0; i < t->count; i ++) {
        if (strcmp(t->passes[i].name, name)) {
            continue;
        }

        result = t->passes[i].time_ms;
        goto done;
    }

done:
    return result;
}

#ifndef BOX_PLATFORM_WEB
/** Bytes the timestamps of a full frame take */
#define BOX_TIMING_BYTES (BOX_TIMING_PASS_MAX * 2 * (uint64_t)sizeof(uint64_t))

/** Publish the timestamps the readback buffer holds.
 *
 * @param status Outcome of the map
 * @param message Error text
 * @param userdata1 The renderer
 * @param userdata2 Unused
 */
static void box_timing_mapped(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    (void)message;
    (void)userdata2;
    box_renderer_t *r = userdata1;
    uint32_t count = r->timing.recorded_count * 2;
    if (status != WGPUMapAsyncStatus_Success) {
        box_timing_drop(&r->timing);
        goto done;
    }

    const uint64_t *stamps = wgpuBufferGetConstMappedRange(r->timing_readback, 0,
        (size_t)count * sizeof(uint64_t));
    if (stamps) {
        box_timing_resolve(&r->timing, stamps, count);
    } else {
        box_timing_drop(&r->timing);
    }

    wgpuBufferUnmap(r->timing_readback);
done:
    r->timing_encoded = false;
    r->timing_busy = false;
}

/** Release the query set and the buffers of the collector.
 *
 * @param r The renderer
 */
static void box_timing_release(
    box_renderer_t *r)
{
    if (r->timing_set) wgpuQuerySetRelease(r->timing_set);
    if (r->timing_resolve) wgpuBufferRelease(r->timing_resolve);
    if (r->timing_readback) wgpuBufferRelease(r->timing_readback);
    r->timing_set = NULL;
    r->timing_resolve = NULL;
    r->timing_readback = NULL;
}

/** Create the query set and the buffers the timestamps of a frame take.
 *
 * @param r The renderer
 */
static void box_timing_create(
    box_renderer_t *r)
{
    WGPUQuerySetDescriptor set_desc = {
        .type = WGPUQueryType_Timestamp,
        .count = BOX_TIMING_PASS_MAX * 2
    };
    r->timing_set = wgpuDeviceCreateQuerySet(r->device, &set_desc);

    WGPUBufferDescriptor resolve_desc = {
        .usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc,
        .size = BOX_TIMING_BYTES
    };
    r->timing_resolve = wgpuDeviceCreateBuffer(r->device, &resolve_desc);

    WGPUBufferDescriptor read_desc = {
        .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
        .size = BOX_TIMING_BYTES
    };
    r->timing_readback = wgpuDeviceCreateBuffer(r->device, &read_desc);
}
#endif

void box_timing_frame(
    box_renderer_t *r)
{
#ifndef BOX_PLATFORM_WEB
    if (!r->timing_enabled || !r->timing_supported) {
        if (r->timing_set && !r->timing_busy) {
            box_timing_release(r);
            box_timing_drop(&r->timing);
            r->timing_encoded = false;
        }

        goto done;
    }

    if (!r->timing_set) {
        box_timing_create(r);
    }

    if (r->timing_set && r->timing_resolve && r->timing_readback) {
        box_timing_begin(&r->timing);
    }

done:
    return;
#else
    (void)r;
#endif
}

/** Take the two timestamps of a pass of the frame being recorded.
 *
 * @param r The renderer
 * @param name Name the pass is published under
 * @param writes The timestamp writes to fill
 * @return The filled writes, NULL when the pass is not timed
 */
static const WGPUPassTimestampWrites* box_timing_writes(
    box_renderer_t *r,
    const char *name,
    WGPUPassTimestampWrites *writes)
{
    const WGPUPassTimestampWrites *result = NULL;
    if (!r->timing.recording) {
        goto done;
    }

    int32_t first = box_timing_add(&r->timing, name);
    if (first < 0) {
        goto done;
    }

    writes->nextInChain = NULL;
    writes->querySet = r->timing_set;
    writes->beginningOfPassWriteIndex = (uint32_t)first;
    writes->endOfPassWriteIndex = (uint32_t)first + 1;
    result = writes;
done:
    return result;
}

WGPURenderPassEncoder box_timing_render(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    const WGPURenderPassDescriptor *desc,
    const char *name)
{
    WGPURenderPassEncoder result;
    WGPUPassTimestampWrites writes;
    const WGPUPassTimestampWrites *timed = box_timing_writes(r, name, &writes);
    if (!timed) {
        result = wgpuCommandEncoderBeginRenderPass(encoder, desc);
        goto done;
    }

    WGPURenderPassDescriptor timed_desc = *desc;
    timed_desc.timestampWrites = timed;
    result = wgpuCommandEncoderBeginRenderPass(encoder, &timed_desc);
done:
    return result;
}

WGPUComputePassEncoder box_timing_compute(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    const char *name)
{
    WGPUComputePassEncoder result;
    WGPUPassTimestampWrites writes;
    const WGPUPassTimestampWrites *timed = box_timing_writes(r, name, &writes);
    if (!timed) {
        result = wgpuCommandEncoderBeginComputePass(encoder, NULL);
        goto done;
    }

    WGPUComputePassDescriptor desc = { .timestampWrites = timed };
    result = wgpuCommandEncoderBeginComputePass(encoder, &desc);
done:
    return result;
}

void box_timing_record(
    box_renderer_t *r,
    WGPUCommandEncoder encoder)
{
#ifndef BOX_PLATFORM_WEB
    if (!r->timing.pending || r->timing_encoded || !r->timing_set) {
        goto done;
    }

    uint32_t count = r->timing.recorded_count * 2;
    wgpuCommandEncoderResolveQuerySet(encoder, r->timing_set, 0, count, r->timing_resolve, 0);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, r->timing_resolve, 0, r->timing_readback, 0,
        (uint64_t)count * sizeof(uint64_t));
    r->timing_encoded = true;
done:
    return;
#else
    (void)r;
    (void)encoder;
#endif
}

void box_timing_close(
    box_renderer_t *r)
{
    box_timing_end(&r->timing);
}

#ifndef BOX_PLATFORM_WEB
/** Take the timestamps of the frame that is waiting for its map.
 *
 * A poll that does not wait leaves the map of a frame the GPU is still working
 * on alone, and the buffer of a frame that is BOX_TIMING_DELAY frames old is
 * waited for, which the GPU has long finished by then.
 *
 * @param r The renderer
 */
static void box_timing_collect(
    box_renderer_t *r)
{
    wgpuDevicePoll(r->device, false, NULL);
    wgpuInstanceProcessEvents(r->instance);
    if (!r->timing_busy) {
        goto done;
    }

    r->timing_wait ++;
    if (r->timing_wait < BOX_TIMING_DELAY) {
        goto done;
    }

    wgpuDevicePoll(r->device, true, NULL);
    wgpuInstanceProcessEvents(r->instance);
done:
    return;
}
#endif

void box_timing_publish(
    box_renderer_t *r)
{
#ifndef BOX_PLATFORM_WEB
    if (r->timing_busy) {
        box_timing_collect(r);
        goto done;
    }

    if (!r->timing.pending || !r->timing_encoded || !r->timing_readback) {
        goto done;
    }

    WGPUBufferMapCallbackInfo info = {
        .mode = WGPUCallbackMode_AllowProcessEvents,
        .callback = box_timing_mapped,
        .userdata1 = r
    };
    r->timing_busy = true;
    r->timing_wait = 0;
    wgpuBufferMapAsync(r->timing_readback, WGPUMapMode_Read, 0,
        (size_t)r->timing.recorded_count * 2 * sizeof(uint64_t), info);
done:
    return;
#else
    (void)r;
#endif
}

void box_timing_fini(
    box_renderer_t *r)
{
#ifndef BOX_PLATFORM_WEB
    while (r->timing_busy) {
        wgpuDevicePoll(r->device, true, NULL);
        wgpuInstanceProcessEvents(r->instance);
    }

    box_timing_release(r);
#else
    (void)r;
#endif
}
