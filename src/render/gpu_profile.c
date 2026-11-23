/**
 * @file render/gpu_profile.c
 * @brief GPU time of the passes of the webgpu backend, measured with timestamp queries when the
 * BOX_GPU_PROFILE environment variable is set, and logged as the mean milliseconds of each pass
 * label over BOX_GPU_PROFILE_FRAMES frames.
 *
 * The passes of a tiled GPU overlap: a pass begins while the fragments of the one before it
 * still run. A pass counts the time from where the passes before it ended to where it ends, so
 * the times of a frame add up to its span and tell what each pass adds to it.
 *
 * The passes of a frame write their timestamps into the queries of a slot of a ring. The slot
 * is resolved once the queue finished its work, as Metal writes a timestamp when its pass
 * completes, then copied into its readback and mapped without waiting.
 */

#include "gpu.h"
#include <stdlib.h>
#include <string.h>

#ifdef BOX_RENDER_WGPU

/** Slots of the ring, frames whose timestamps are in flight at most */
#define BOX_GPU_PROFILE_SLOTS (4)

/** Passes a frame measures at most */
#define BOX_GPU_PROFILE_PASSES (64)

/** Labels the profile tells apart at most */
#define BOX_GPU_PROFILE_LABELS (48)

/** Frames each logged mean covers */
#define BOX_GPU_PROFILE_FRAMES (120)

/** State of a slot of the ring */
typedef enum box_gpu_profile_state_t {
    BOX_GPU_PROFILE_FREE = 0,   /**< Not in use */
    BOX_GPU_PROFILE_RECORDING,  /**< Passes of the frame being rendered write into it */
    BOX_GPU_PROFILE_SUBMITTED,  /**< Waiting for the queue to finish the frame */
    BOX_GPU_PROFILE_FINISHED,   /**< The queue finished the frame, to resolve */
    BOX_GPU_PROFILE_COPIED,     /**< Resolved and copied, to map */
    BOX_GPU_PROFILE_MAPPING,    /**< Being mapped */
    BOX_GPU_PROFILE_MAPPED,     /**< Mapped, to read */
    BOX_GPU_PROFILE_FAILED      /**< The map failed */
} box_gpu_profile_state_t;

/** A slot of the ring: the passes of one frame */
typedef struct box_gpu_profile_slot_t {
    box_gpu_profile_state_t state; /**< Its state */
    WGPUBuffer readback;        /**< Copy of its resolved timestamps to map */
    int32_t passes;             /**< Passes that wrote timestamps */
    const char *labels[BOX_GPU_PROFILE_PASSES]; /**< Label of each pass */
} box_gpu_profile_slot_t;

/** The profile of a backend */
typedef struct box_gpu_profile_t {
    WGPUQuerySet queries;       /**< Two queries per pass per slot */
    WGPUBuffer resolve;         /**< Resolved timestamps of every slot */
    box_gpu_profile_slot_t slots[BOX_GPU_PROFILE_SLOTS]; /**< The ring */
    int32_t current;            /**< Slot the frame being rendered records into, -1 for none */
    const char *labels[BOX_GPU_PROFILE_LABELS]; /**< Labels measured so far */
    double sums[BOX_GPU_PROFILE_LABELS]; /**< Milliseconds of each label since the last log */
    int32_t counts[BOX_GPU_PROFILE_LABELS]; /**< Passes of each label since the last log */
    double idles[BOX_GPU_PROFILE_LABELS]; /**< Milliseconds the GPU was idle before the passes
                                 *   of each label since the last log */
    double frame_ms;            /**< Milliseconds from the first to the last timestamp of the
                                 *   frames since the last log */
    int32_t frames;             /**< Frames read since the last log */
} box_gpu_profile_t;

/** The profile, one per process: the tool measures the one backend of an app */
static box_gpu_profile_t *box_gpu_profile;

/** Get the profile, made on first use when BOX_GPU_PROFILE is set and the device has
 * timestamp queries.
 *
 * @param gpu The backend
 * @return The profile, NULL when it is off
 */
static box_gpu_profile_t* box_gpu_profile_get(
    box_gpu_t *gpu)
{
    static bool checked;
    box_gpu_profile_t *p = box_gpu_profile;
    int32_t i;
    if (p || checked) {
        goto done;
    }

    checked = true;
    if (!getenv("BOX_GPU_PROFILE") || !gpu->timestamps || !gpu->device) {
        goto done;
    }

    p = ecs_os_calloc_t(box_gpu_profile_t);
    p->queries = wgpuDeviceCreateQuerySet(gpu->device, &(WGPUQuerySetDescriptor){
        .type = WGPUQueryType_Timestamp,
        .count = 2 * BOX_GPU_PROFILE_PASSES * BOX_GPU_PROFILE_SLOTS
    });
    p->resolve = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc,
        .size = 2 * BOX_GPU_PROFILE_PASSES * BOX_GPU_PROFILE_SLOTS * sizeof(uint64_t)
    });
    for (i = 0; i < BOX_GPU_PROFILE_SLOTS; i ++) {
        p->slots[i].readback = wgpuDeviceCreateBuffer(gpu->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
            .size = 2 * BOX_GPU_PROFILE_PASSES * sizeof(uint64_t)
        });
    }

    p->current = -1;
    box_gpu_profile = p;
    ecs_trace("gpu profile: measuring the passes, logged every %d frames",
        BOX_GPU_PROFILE_FRAMES);
done:
    return p;
}

const WGPUPassTimestampWrites* box_gpu_profile_writes(
    box_gpu_t *gpu,
    const char *label,
    WGPUPassTimestampWrites *out)
{
    const WGPUPassTimestampWrites *result = NULL;
    box_gpu_profile_t *p = box_gpu_profile_get(gpu);
    int32_t i;
    if (!p) {
        goto done;
    }

    if (p->current < 0) {
        for (i = 0; i < BOX_GPU_PROFILE_SLOTS; i ++) {
            if (p->slots[i].state == BOX_GPU_PROFILE_FREE) {
                p->current = i;
                p->slots[i].state = BOX_GPU_PROFILE_RECORDING;
                p->slots[i].passes = 0;
                break;
            }
        }
    }

    if (p->current < 0) {
        goto done;
    }

    box_gpu_profile_slot_t *s = &p->slots[p->current];
    if (s->passes >= BOX_GPU_PROFILE_PASSES) {
        goto done;
    }

    uint32_t query = (uint32_t)(p->current * BOX_GPU_PROFILE_PASSES + s->passes) * 2u;
    s->labels[s->passes ++] = label;
    *out = (WGPUPassTimestampWrites){
        .querySet = p->queries,
        .beginningOfPassWriteIndex = query,
        .endOfPassWriteIndex = query + 1u
    };
    result = out;
done:
    return result;
}

WGPURenderPassEncoder box_gpu_profile_render_pass(
    box_gpu_t *gpu,
    WGPUCommandEncoder encoder,
    const WGPURenderPassDescriptor *desc,
    const char *label)
{
    WGPURenderPassDescriptor d = *desc;
    WGPUPassTimestampWrites writes;
    if (!d.timestampWrites && label) {
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

/** Mark a slot whose frame the queue finished.
 *
 * @param status Outcome of the request
 * @param message Error text
 * @param userdata1 The slot
 * @param userdata2 Unused
 */
static void box_gpu_profile_on_done(
    WGPUQueueWorkDoneStatus status,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    box_gpu_profile_slot_t *s = userdata1;
    (void)message;
    (void)userdata2;
    if (status == WGPUQueueWorkDoneStatus_Success) {
        s->state = BOX_GPU_PROFILE_FINISHED;
    } else if (status != WGPUQueueWorkDoneStatus_CallbackCancelled) {
        s->state = BOX_GPU_PROFILE_FREE;
    }
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

/** Add the passes of a mapped slot to the sums, and log the means every
 * BOX_GPU_PROFILE_FRAMES frames.
 *
 * @param gpu The backend
 * @param p The profile
 * @param s The slot
 */
static void box_gpu_profile_read(
    box_gpu_t *gpu,
    box_gpu_profile_t *p,
    box_gpu_profile_slot_t *s)
{
    const uint64_t *t = wgpuBufferGetConstMappedRange(s->readback, 0,
        2 * BOX_GPU_PROFILE_PASSES * sizeof(uint64_t));
    double period = (double)wgpuQueueGetTimestampPeriod(gpu->queue) * 1e-6;
    uint64_t first = UINT64_MAX, last = 0;
    int32_t i, l;
    for (i = 0; t && i < s->passes; i ++) {
        uint64_t begin = t[2 * i], end = t[2 * i + 1];
        double idle = 0;
        if (!begin || end < begin) {
            continue;
        }

        if (last && begin > last) {
            idle = (double)(begin - last) * period;
        }

        if (last && begin < last) {
            begin = end > last ? last : end;
        }

        first = begin < first ? begin : first;
        last = end > last ? end : last;
        for (l = 0; l < BOX_GPU_PROFILE_LABELS && p->labels[l] &&
            strcmp(p->labels[l], s->labels[i]); l ++) { }
        if (l == BOX_GPU_PROFILE_LABELS) {
            continue;
        }

        p->labels[l] = s->labels[i];
        p->sums[l] += (double)(end - begin) * period;
        p->idles[l] += idle;
        p->counts[l] ++;
    }

    if (last > first) {
        p->frame_ms += (double)(last - first) * period;
        p->frames ++;
    }

    if (p->frames < BOX_GPU_PROFILE_FRAMES) {
        goto done;
    }

    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    double total = 0;
    for (l = 0; l < BOX_GPU_PROFILE_LABELS && p->labels[l]; l ++) {
        total += p->sums[l];
    }

    ecs_strbuf_append(&buf, "gpu profile over %d frames: frame span %.2f ms, passes %.2f ms",
        p->frames, p->frame_ms / p->frames, total / p->frames);
    for (l = 0; l < BOX_GPU_PROFILE_LABELS && p->labels[l]; l ++) {
        ecs_strbuf_append(&buf, "\n  %-24s %6.3f ms (%.1f passes), idle before %.3f ms",
            p->labels[l], p->sums[l] / p->frames, (double)p->counts[l] / p->frames,
            p->idles[l] / p->frames);
        p->sums[l] = 0;
        p->idles[l] = 0;
        p->counts[l] = 0;
    }

    char *str = ecs_strbuf_get(&buf);
    ecs_trace("%s", str);
    ecs_os_free(str);
    p->frame_ms = 0;
    p->frames = 0;
done:
    return;
}

void box_gpu_profile_frame(
    box_gpu_t *gpu)
{
    box_gpu_profile_t *p = box_gpu_profile;
    WGPUCommandEncoder encoder = NULL;
    int32_t i;
    if (!p) {
        goto done;
    }

    wgpuDevicePoll(gpu->device, false, NULL);
    for (i = 0; i < BOX_GPU_PROFILE_SLOTS; i ++) {
        box_gpu_profile_slot_t *s = &p->slots[i];
        uint64_t offset = (uint64_t)i * 2 * BOX_GPU_PROFILE_PASSES * sizeof(uint64_t);
        if (s->state == BOX_GPU_PROFILE_MAPPED) {
            box_gpu_profile_read(gpu, p, s);
            wgpuBufferUnmap(s->readback);
            s->state = BOX_GPU_PROFILE_FREE;
        } else if (s->state == BOX_GPU_PROFILE_FAILED) {
            s->state = BOX_GPU_PROFILE_FREE;
        } else if (s->state == BOX_GPU_PROFILE_FINISHED && s->passes) {
            encoder = encoder ? encoder : wgpuDeviceCreateCommandEncoder(gpu->device, NULL);
            wgpuCommandEncoderResolveQuerySet(encoder, p->queries,
                (uint32_t)(i * BOX_GPU_PROFILE_PASSES * 2), (uint32_t)s->passes * 2u,
                p->resolve, offset);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, p->resolve, offset, s->readback, 0,
                (uint64_t)s->passes * 2 * sizeof(uint64_t));
            s->state = BOX_GPU_PROFILE_COPIED;
        } else if (s->state == BOX_GPU_PROFILE_FINISHED) {
            s->state = BOX_GPU_PROFILE_FREE;
        }
    }

    if (encoder) {
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuQueueSubmit(gpu->queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
        for (i = 0; i < BOX_GPU_PROFILE_SLOTS; i ++) {
            box_gpu_profile_slot_t *s = &p->slots[i];
            if (s->state == BOX_GPU_PROFILE_COPIED) {
                s->state = BOX_GPU_PROFILE_MAPPING;
                wgpuBufferMapAsync(s->readback, WGPUMapMode_Read, 0,
                    2 * BOX_GPU_PROFILE_PASSES * sizeof(uint64_t),
                    (WGPUBufferMapCallbackInfo){
                        .mode = WGPUCallbackMode_AllowSpontaneous,
                        .callback = box_gpu_profile_on_map,
                        .userdata1 = s
                    });
            }
        }
    }

    if (p->current >= 0) {
        box_gpu_profile_slot_t *s = &p->slots[p->current];
        s->state = BOX_GPU_PROFILE_SUBMITTED;
        wgpuQueueOnSubmittedWorkDone(gpu->queue, (WGPUQueueWorkDoneCallbackInfo){
            .mode = WGPUCallbackMode_AllowSpontaneous,
            .callback = box_gpu_profile_on_done,
            .userdata1 = s
        });
        p->current = -1;
    }

done:
    return;
}

#endif
