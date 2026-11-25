/**
 * @file render/gpu_profile.c
 * @brief GPU time of the passes of the webgpu backend, measured with timestamp queries while
 * box_render_gpu_timing turned it on or the BOX_GPU_PROFILE environment variable is set.
 *
 * The passes of a tiled GPU overlap: a pass begins while the fragments of the one before it
 * still run. A pass counts the time from where the passes before it ended to where it ends, so
 * the times of a frame add up to the time the GPU worked on it.
 *
 * The passes of a frame write their timestamps into the queries of a slot of a ring, or into
 * queries of their own, which the slot borrows. At the end of the frame the slot is resolved,
 * copied into its readback and mapped; BOX_GPU_PROFILE also logs means per label.
 */

#include "gpu.h"
#include <stdlib.h>
#include <string.h>

#ifdef BOX_RENDER_WGPU

/** Slots of the ring, frames whose timestamps are in flight at most */
#define BOX_GPU_PROFILE_SLOTS (8)

/** Passes a frame measures at most */
#define BOX_GPU_PROFILE_PASSES (128)

/** Labels the profile tells apart at most */
#define BOX_GPU_PROFILE_LABELS (BOX_RENDER_GPU_TIMING_LABELS)

/** Color attachments of a render pass at most */
#define BOX_GPU_PROFILE_ATTACHMENTS (8)

/** Frames each logged mean covers */
#define BOX_GPU_PROFILE_FRAMES (120)

/** Alignment of the destination offset of a query resolve */
#define BOX_GPU_PROFILE_ALIGN (256)

/** Bytes of the timestamps of the passes of a slot */
#define BOX_GPU_PROFILE_SLOT_BYTES (2 * BOX_GPU_PROFILE_PASSES * sizeof(uint64_t))

/** State of a slot of the ring */
typedef enum box_gpu_profile_state_t {
    BOX_GPU_PROFILE_FREE = 0,   /**< Not in use */
    BOX_GPU_PROFILE_RECORDING,  /**< Passes of the frame being rendered write into it */
    BOX_GPU_PROFILE_MAPPING,    /**< Resolved, copied and being mapped */
    BOX_GPU_PROFILE_MAPPED,     /**< Mapped, to read */
    BOX_GPU_PROFILE_FAILED      /**< The map failed */
} box_gpu_profile_state_t;

/** A slot of the ring: the passes of one frame */
typedef struct box_gpu_profile_slot_t {
    box_gpu_profile_state_t state; /**< Its state */
    WGPUBuffer readback;        /**< Copy of its resolved timestamps to map */
    int64_t frame;              /**< Frame of the packet the passes rendered */
    int32_t passes;             /**< Passes that wrote timestamps */
    const char *labels[BOX_GPU_PROFILE_PASSES]; /**< Label of each pass */
    WGPUQuerySet borrowed[BOX_GPU_PROFILE_PASSES]; /**< Query set of a pass that writes its own
                                 *   timestamps, NULL for a pass that writes into the slot */
    uint32_t begin[BOX_GPU_PROFILE_PASSES]; /**< Query of the start of a borrowed pass */
    uint32_t end[BOX_GPU_PROFILE_PASSES]; /**< Query of the end of a borrowed pass */
} box_gpu_profile_slot_t;

/** A timed pass of a mapped slot */
typedef struct box_gpu_profile_pass_t {
    uint64_t begin;             /**< Timestamp of its start */
    uint64_t end;               /**< Timestamp of its end */
    const char *label;          /**< Its label */
} box_gpu_profile_pass_t;

/** The profile of a backend */
typedef struct box_gpu_profile_t {
    WGPUQuerySet queries;       /**< Two queries per pass per slot */
    WGPUBuffer resolve;         /**< Resolved timestamps of every slot */
    WGPUBuffer borrowed;        /**< Resolved borrowed timestamps of a frame, one per
                                 *   BOX_GPU_PROFILE_ALIGN bytes */
    box_gpu_profile_slot_t slots[BOX_GPU_PROFILE_SLOTS]; /**< The ring */
    int32_t current;            /**< Slot the frame being rendered records into, -1 for none */
    bool on;                    /**< Passes of the frame being rendered are timed */
    bool log;                   /**< BOX_GPU_PROFILE is set, the means are logged */
    const char *labels[BOX_GPU_PROFILE_LABELS]; /**< Labels logged so far */
    double sums[BOX_GPU_PROFILE_LABELS]; /**< Milliseconds of each label since the last log */
    int32_t counts[BOX_GPU_PROFILE_LABELS]; /**< Passes of each label since the last log */
    double busy_ms;             /**< Milliseconds the GPU worked in the frames since the last log */
    double span_ms;             /**< Milliseconds from the first to the last timestamp of the
                                 *   frames since the last log */
    int32_t frames;             /**< Frames read since the last log */
} box_gpu_profile_t;

/** Create the profile of a backend when the timing or BOX_GPU_PROFILE needs it and the device
 * has timestamp queries.
 *
 * @param gpu The backend
 * @param on The timing is on
 * @return The profile, NULL when there is none
 */
static box_gpu_profile_t* box_gpu_profile_get(
    box_gpu_t *gpu,
    bool on)
{
    box_gpu_profile_t *p = gpu->profile;
    bool log = getenv("BOX_GPU_PROFILE") != NULL;
    int32_t i;
    if (p || (!on && !log) || !gpu->timestamps || !gpu->device) {
        goto done;
    }

    p = ecs_os_calloc_t(box_gpu_profile_t);
    p->queries = wgpuDeviceCreateQuerySet(gpu->device, &(WGPUQuerySetDescriptor){
        .type = WGPUQueryType_Timestamp,
        .count = 2 * BOX_GPU_PROFILE_PASSES * BOX_GPU_PROFILE_SLOTS
    });
    p->resolve = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc,
        .size = BOX_GPU_PROFILE_SLOT_BYTES * BOX_GPU_PROFILE_SLOTS
    });
    p->borrowed = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc,
        .size = 2 * BOX_GPU_PROFILE_PASSES * BOX_GPU_PROFILE_ALIGN
    });
    for (i = 0; i < BOX_GPU_PROFILE_SLOTS; i ++) {
        p->slots[i].readback = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
            .size = BOX_GPU_PROFILE_SLOT_BYTES
        });
    }

    p->current = -1;
    p->log = log;
    gpu->profile = p;
    if (log) {
        ecs_trace("gpu profile: measuring the passes, logged every %d frames",
            BOX_GPU_PROFILE_FRAMES);
    }

done:
    return p;
}

/** Get the slot the passes of the frame being rendered record into, taking a free one for the
 * first pass of the frame.
 *
 * @param p The profile
 * @return The slot, NULL when the profile is off, every slot is in use or the slot is full
 */
static box_gpu_profile_slot_t* box_gpu_profile_slot(
    box_gpu_profile_t *p)
{
    box_gpu_profile_slot_t *result = NULL;
    int32_t i;
    if (!p || !p->on) {
        goto done;
    }

    for (i = 0; p->current < 0 && i < BOX_GPU_PROFILE_SLOTS; i ++) {
        if (p->slots[i].state == BOX_GPU_PROFILE_FREE) {
            p->current = i;
            p->slots[i].state = BOX_GPU_PROFILE_RECORDING;
            p->slots[i].passes = 0;
        }
    }

    if (p->current >= 0 && p->slots[p->current].passes < BOX_GPU_PROFILE_PASSES) {
        result = &p->slots[p->current];
    }

done:
    return result;
}

const WGPUPassTimestampWrites* box_gpu_profile_writes(
    box_gpu_t *gpu,
    const char *label,
    WGPUPassTimestampWrites *out)
{
    box_gpu_profile_t *p = gpu->profile;
    box_gpu_profile_slot_t *s = box_gpu_profile_slot(p);
    const WGPUPassTimestampWrites *result = NULL;
    if (!s) {
        goto done;
    }

    uint32_t query = (uint32_t)(p->current * BOX_GPU_PROFILE_PASSES + s->passes) * 2u;
    s->borrowed[s->passes] = NULL;
    s->labels[s->passes ++] = label ? label : "other";
    *out = (WGPUPassTimestampWrites){
        .querySet = p->queries,
        .beginningOfPassWriteIndex = query,
        .endOfPassWriteIndex = query + 1u
    };
    result = out;
done:
    return result;
}

void box_gpu_profile_borrow(
    box_gpu_t *gpu,
    const char *label,
    const WGPUPassTimestampWrites *writes)
{
    box_gpu_profile_slot_t *s = box_gpu_profile_slot(gpu->profile);
    if (!s || !writes || !writes->querySet ||
        writes->beginningOfPassWriteIndex == WGPU_QUERY_SET_INDEX_UNDEFINED ||
        writes->endOfPassWriteIndex == WGPU_QUERY_SET_INDEX_UNDEFINED)
    {
        goto done;
    }

    s->borrowed[s->passes] = writes->querySet;
    s->begin[s->passes] = writes->beginningOfPassWriteIndex;
    s->end[s->passes] = writes->endOfPassWriteIndex;
    s->labels[s->passes ++] = label ? label : "other";
done:
    return;
}

WGPURenderPassEncoder box_gpu_profile_render_pass(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const WGPURenderPassDescriptor *desc,
    const char *label)
{
    WGPURenderPassDescriptor d = *desc;
    WGPURenderPassColorAttachment colors[BOX_GPU_PROFILE_ATTACHMENTS];
    WGPUPassTimestampWrites writes;
    size_t i;
    for (i = 0; gpu->clear_view && i < d.colorAttachmentCount; i ++) {
        if (d.colorAttachments[i].view != gpu->clear_view) {
            continue;
        }

        if (d.colorAttachments[i].loadOp == WGPULoadOp_Load &&
            d.colorAttachmentCount <= BOX_GPU_PROFILE_ATTACHMENTS)
        {
            ecs_os_memcpy_n(colors, d.colorAttachments, WGPURenderPassColorAttachment,
                (int32_t)d.colorAttachmentCount);
            colors[i].loadOp = WGPULoadOp_Clear;
            colors[i].clearValue = gpu->clear_color;
            d.colorAttachments = colors;
        }

        gpu->clear_view = NULL;
    }

    if (d.timestampWrites) {
        box_gpu_profile_borrow(gpu, label, d.timestampWrites);
    } else {
        d.timestampWrites = box_gpu_profile_writes(gpu, label, &writes);
    }

    return wgpuCommandEncoderBeginRenderPass(encoder, &d);
}

WGPUComputePassEncoder box_gpu_profile_compute_pass(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const char *label)
{
    WGPUPassTimestampWrites writes;

    return wgpuCommandEncoderBeginComputePass(encoder, &(WGPUComputePassDescriptor){
        .timestampWrites = box_gpu_profile_writes(gpu, label, &writes)
    });
}

/** Mark a slot whose readback was mapped.
 *
 * @param status Outcome of the map
 * @param message Error text
 * @param userdata1 The slot
 * @param userdata2 Unused
 */
static void box_gpu_profile_on_map(
    WGPUMapAsyncStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_profile_slot_t *s = userdata1;
    (void)message;
    (void)userdata2;
    if (status != WGPUMapAsyncStatus_CallbackCancelled) {
        s->state = status == WGPUMapAsyncStatus_Success
            ? BOX_GPU_PROFILE_MAPPED : BOX_GPU_PROFILE_FAILED;
    }
}

/** Compare two timed passes by their start, for qsort.
 *
 * @param a The first pass
 * @param b The second pass
 * @return Negative when a starts first, positive when b does, 0 when they start together
 */
static int box_gpu_profile_cmp(
    const void *a,
    const void *b)
{
    uint64_t x = ((const box_gpu_profile_pass_t*)a)->begin;
    uint64_t y = ((const box_gpu_profile_pass_t*)b)->begin;

    return (x > y) - (x < y);
}

/** Add the milliseconds of a pass to the label of a timing.
 *
 * @param t The timing
 * @param label The label of the pass
 * @param ms The milliseconds the pass added
 */
static void box_gpu_profile_label(
    box_render_gpu_timing_t *t,
    const char *label,
    double ms)
{
    int32_t l;
    for (l = 0; l < t->labels && strcmp(t->label[l], label); l ++) { }
    if (l == t->labels && l < BOX_RENDER_GPU_TIMING_LABELS) {
        t->label[l] = label;
        t->labels ++;
    }

    if (l < t->labels) {
        t->label_ms[l] += ms;
    }
}

/** Compute the timing of a mapped slot: its passes in the order they started, each counting
 * from where the passes before it ended.
 *
 * @param gpu The backend
 * @param s The slot
 * @param t Receives the timing
 */
static void box_gpu_profile_timing(
    box_gpu_t *gpu,
    const box_gpu_profile_slot_t *s,
    box_render_gpu_timing_t *t)
{
    const uint64_t *stamps = wgpuBufferGetConstMappedRange(s->readback, 0,
        BOX_GPU_PROFILE_SLOT_BYTES);
    double period = (double)wgpuQueueGetTimestampPeriod(gpu->queue) * 1e-6;
    box_gpu_profile_pass_t passes[BOX_GPU_PROFILE_PASSES];
    uint64_t first = UINT64_MAX, last = 0;
    int32_t i, count = 0;
    ecs_os_memset_t(t, 0, box_render_gpu_timing_t);
    t->frame = s->frame;
    for (i = 0; stamps && i < s->passes; i ++) {
        if (stamps[2 * i] && stamps[2 * i + 1] >= stamps[2 * i]) {
            passes[count ++] = (box_gpu_profile_pass_t){
                stamps[2 * i], stamps[2 * i + 1], s->labels[i] };
        }
    }

    qsort(passes, (size_t)count, sizeof(box_gpu_profile_pass_t), box_gpu_profile_cmp);
    for (i = 0; i < count; i ++) {
        uint64_t begin = passes[i].begin > last ? passes[i].begin : last;
        double ms = passes[i].end > begin ? (double)(passes[i].end - begin) * period : 0;
        first = passes[i].begin < first ? passes[i].begin : first;
        last = passes[i].end > last ? passes[i].end : last;
        t->busy_ms += ms;
        box_gpu_profile_label(t, passes[i].label, ms);
    }

    t->passes = count;
    t->span_ms = last > first ? (double)(last - first) * period : 0;
}

/** Add a timing to the sums of the log, and log the means every BOX_GPU_PROFILE_FRAMES frames.
 *
 * @param p The profile
 * @param t The timing
 */
static void box_gpu_profile_log(
    box_gpu_profile_t *p,
    const box_render_gpu_timing_t *t)
{
    int32_t i, l;
    for (i = 0; i < t->labels; i ++) {
        for (l = 0; l < BOX_GPU_PROFILE_LABELS && p->labels[l] &&
            strcmp(p->labels[l], t->label[i]); l ++) { }
        if (l < BOX_GPU_PROFILE_LABELS) {
            p->labels[l] = t->label[i];
            p->sums[l] += t->label_ms[i];
            p->counts[l] ++;
        }
    }

    p->busy_ms += t->busy_ms;
    p->span_ms += t->span_ms;
    p->frames ++;
    if (p->frames < BOX_GPU_PROFILE_FRAMES) {
        goto done;
    }

    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_strbuf_append(&buf, "gpu profile over %d frames: frame span %.2f ms, busy %.2f ms",
        p->frames, p->span_ms / p->frames, p->busy_ms / p->frames);
    for (l = 0; l < BOX_GPU_PROFILE_LABELS && p->labels[l]; l ++) {
        ecs_strbuf_append(&buf, "\n  %-24s %6.3f ms in %d frames",
            p->labels[l], p->sums[l] / p->frames, p->counts[l]);
        p->sums[l] = 0;
        p->counts[l] = 0;
    }

    char *str = ecs_strbuf_get(&buf);
    ecs_trace("%s", str);
    ecs_os_free(str);
    p->busy_ms = 0;
    p->span_ms = 0;
    p->frames = 0;
done:
    return;
}

/** Read the mapped slots: publish their timings while the timing is on and log them with
 * BOX_GPU_PROFILE, then free them.
 *
 * @param gpu The backend
 * @param p The profile
 * @param on The timing is on
 */
static void box_gpu_profile_read(
    box_gpu_t *gpu,
    box_gpu_profile_t *p,
    bool on)
{
    box_render_gpu_timing_t t;
    int32_t i;
    for (i = 0; i < BOX_GPU_PROFILE_SLOTS; i ++) {
        box_gpu_profile_slot_t *s = &p->slots[i];
        if (s->state == BOX_GPU_PROFILE_FAILED) {
            s->state = BOX_GPU_PROFILE_FREE;
        }

        if (s->state != BOX_GPU_PROFILE_MAPPED) {
            continue;
        }

        box_gpu_profile_timing(gpu, s, &t);
        wgpuBufferUnmap(s->readback);
        s->state = BOX_GPU_PROFILE_FREE;
        if (p->log) {
            box_gpu_profile_log(p, &t);
        }

        ecs_os_mutex_lock(gpu->lock);
        if (on && ecs_vec_count(&gpu->timings) < BOX_RENDER_GPU_TIMING_KEEP) {
            ecs_vec_append_t(NULL, &gpu->timings, box_render_gpu_timing_t)[0] = t;
        }

        ecs_os_mutex_unlock(gpu->lock);
    }
}

/** Resolve the timestamps of the slot of the frame into its readback and map it.
 *
 * The borrowed timestamps are resolved now, before their passes write them again in a later
 * frame, one per BOX_GPU_PROFILE_ALIGN bytes, and copied over the slot's own after it.
 *
 * @param gpu The backend
 * @param p The profile
 * @param s The slot
 */
static void box_gpu_profile_submit(
    box_gpu_t *gpu,
    box_gpu_profile_t *p,
    box_gpu_profile_slot_t *s)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
    uint64_t offset = (uint64_t)p->current * BOX_GPU_PROFILE_SLOT_BYTES;
    uint64_t pair = 2 * sizeof(uint64_t);
    int32_t i, q;
    wgpuCommandEncoderResolveQuerySet(encoder, p->queries,
        (uint32_t)(p->current * BOX_GPU_PROFILE_PASSES * 2), (uint32_t)s->passes * 2u,
        p->resolve, offset);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, p->resolve, offset, s->readback, 0,
        (uint64_t)s->passes * pair);
    for (i = 0; i < s->passes; i ++) {
        uint32_t queries[2] = { s->begin[i], s->end[i] };
        for (q = 0; s->borrowed[i] && q < 2; q ++) {
            uint64_t at = (uint64_t)(2 * i + q) * BOX_GPU_PROFILE_ALIGN;
            wgpuCommandEncoderResolveQuerySet(encoder, s->borrowed[i], queries[q], 1,
                p->borrowed, at);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, p->borrowed, at, s->readback,
                (uint64_t)i * pair + (uint64_t)q * sizeof(uint64_t), sizeof(uint64_t));
        }
    }

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(gpu->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    s->state = BOX_GPU_PROFILE_MAPPING;
    wgpuBufferMapAsync(s->readback, WGPUMapMode_Read, 0, BOX_GPU_PROFILE_SLOT_BYTES,
        (WGPUBufferMapCallbackInfo){
            .mode = WGPUCallbackMode_AllowSpontaneous,
            .callback = box_gpu_profile_on_map,
            .userdata1 = s
        });
}

void box_gpu_profile_frame(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    box_gpu_profile_t *p;
    bool on;
    ecs_os_mutex_lock(gpu->lock);
    on = gpu->timing;
    ecs_os_mutex_unlock(gpu->lock);
    p = box_gpu_profile_get(gpu, on);
    if (!p) {
        goto done;
    }

    if (p->current >= 0) {
        box_gpu_profile_slot_t *s = &p->slots[p->current];
        s->frame = packet->frame;
        if (s->passes) {
            box_gpu_profile_submit(gpu, p, s);
        } else {
            s->state = BOX_GPU_PROFILE_FREE;
        }

        p->current = -1;
    }

    wgpuDevicePoll(gpu->device, false, NULL);
    box_gpu_profile_read(gpu, p, on);
    p->on = on || p->log;
done:
    return;
}

bool box_gpu_profile_timed(
    box_gpu_t *gpu)
{
    return gpu->cull.detail || (gpu->profile && gpu->profile->on);
}

void box_gpu_profile_fini(
    box_gpu_t *gpu)
{
    box_gpu_profile_t *p = gpu->profile;
    int32_t i;
    if (!p) {
        goto done;
    }

    for (i = 0; i < BOX_GPU_PROFILE_SLOTS; i ++) {
        wgpuBufferRelease(p->slots[i].readback);
    }

    wgpuBufferRelease(p->borrowed);
    wgpuBufferRelease(p->resolve);
    wgpuQuerySetRelease(p->queries);
    ecs_os_free(p);
    gpu->profile = NULL;
done:
    return;
}

#endif
