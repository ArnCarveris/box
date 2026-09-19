/**
 * @file renderer/timing.h
 * @brief Per pass GPU timestamps of a frame, their names and their delayed resolve.
 *
 * The renderer writes two timestamps around every pass it records, resolves
 * them in the encoder of the next frame and maps that buffer a frame or two
 * later, so the numbers of a frame arrive while a later frame is recorded. The
 * bookkeeping lives here because the monitor publishes the same passes and
 * because
 * neither side of the frame packet may include the WebGPU headers. The
 * recording functions at the end are declared for the renderer sources only,
 * which is what including gpu.h before this file selects.
 */

#ifndef BOX_RENDERER_TIMING_H
#define BOX_RENDERER_TIMING_H

#include <stdint.h>
#include <stdbool.h>

/** Passes one frame can time, which is half the timestamps of the query set */
#define BOX_TIMING_PASS_MAX (64)

/** Characters of a pass name, its terminator included */
#define BOX_TIMING_NAME_MAX (24)

/** Frames the read back of a resolve is given before the renderer waits for it */
#define BOX_TIMING_DELAY (2)

/** Nanoseconds in a millisecond, the unit the resolved timestamps are in */
#define BOX_TIMING_NS_MS (1000000.0)

/** Name of a recorded pass and the milliseconds its timestamps spanned */
typedef struct box_timing_pass_t {
    char name[BOX_TIMING_NAME_MAX]; /**< Name the pass was recorded under */
    double time_ms;                 /**< Milliseconds the GPU spent in the pass */
} box_timing_pass_t;

/** Passes of the frame in flight and the passes of the last resolved frame.
 *
 * A frame is recorded into `recorded` and closed when its resolve is encoded,
 * which makes it pending; the collector records no other frame until the
 * timestamps of that one arrive or are dropped, so one query set is enough.
 */
typedef struct box_timing_t {
    box_timing_pass_t recorded[BOX_TIMING_PASS_MAX]; /**< Passes of the frame in flight */
    uint32_t recorded_count;   /**< Passes that frame recorded */
    uint32_t recorded_frame;   /**< Frame those passes belong to */
    bool recording;            /**< A frame is being recorded */
    bool pending;              /**< The recorded frame waits for its timestamps */
    box_timing_pass_t passes[BOX_TIMING_PASS_MAX]; /**< Passes of the resolved frame */
    uint32_t count;            /**< Passes of that frame, one entry per name */
    double frame_ms;           /**< Milliseconds those passes add up to */
    uint32_t frame;            /**< Frames the collector started */
    uint32_t resolved;         /**< Frame the published passes were measured in */
    uint32_t dropped;          /**< Passes that found the query set full */
} box_timing_t;

/** Start recording the passes of a frame.
 *
 * @param t The collector
 * @return true when the frame is recorded, false while an earlier one is pending
 */
bool box_timing_begin(
    box_timing_t *t);

/** Register a pass of the frame being recorded and take its two timestamps.
 *
 * @param t The collector
 * @param name Name of the pass, truncated to BOX_TIMING_NAME_MAX
 * @return Index of the timestamp the pass begins at, -1 when it is not timed
 */
int32_t box_timing_add(
    box_timing_t *t,
    const char *name);

/** Close the frame being recorded, which makes it wait for its timestamps.
 *
 * @param t The collector
 * @return true when the frame recorded a pass and has to be resolved
 */
bool box_timing_end(
    box_timing_t *t);

/** Publish the timestamps of the pending frame, summing the passes per name.
 *
 * @param t The collector
 * @param stamps Resolved timestamps in nanoseconds, two per recorded pass
 * @param count Timestamps the buffer holds
 */
void box_timing_resolve(
    box_timing_t *t,
    const uint64_t *stamps,
    uint32_t count);

/** Give up on the timestamps of the pending frame and record the next one.
 *
 * @param t The collector
 */
void box_timing_drop(
    box_timing_t *t);

/** Milliseconds a pass of the last resolved frame spent on the GPU.
 *
 * @param t The collector
 * @param name Name of the pass
 * @return The milliseconds, 0 when the resolved frame has no such pass
 */
double box_timing_get(
    const box_timing_t *t,
    const char *name);

#ifdef BOX_RENDERER_GPU_H
/** Start the timings of a frame, creating or releasing what they need.
 *
 * @param r The renderer
 */
void box_timing_frame(
    box_renderer_t *r);

/** Begin a render pass, timing it when the frame is being recorded.
 *
 * @param r The renderer
 * @param encoder The command encoder
 * @param desc The pass descriptor
 * @param name Name the pass is published under
 * @return The pass encoder
 */
WGPURenderPassEncoder box_timing_render(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    const WGPURenderPassDescriptor *desc,
    const char *name);

/** Begin a compute pass, timing it when the frame is being recorded.
 *
 * @param r The renderer
 * @param encoder The command encoder
 * @param name Name the pass is published under
 * @return The pass encoder
 */
WGPUComputePassEncoder box_timing_compute(
    box_renderer_t *r,
    WGPUCommandEncoder encoder,
    const char *name);

/** Encode the resolve of the timestamps of the frame that is waiting for them.
 *
 * @param r The renderer
 * @param encoder The command encoder of the frame that follows it
 */
void box_timing_record(
    box_renderer_t *r,
    WGPUCommandEncoder encoder);

/** Close the frame being recorded, which makes it wait for its resolve.
 *
 * @param r The renderer
 */
void box_timing_close(
    box_renderer_t *r);

/** Map the resolved timestamps of the frame that is waiting for them.
 *
 * @param r The renderer
 */
void box_timing_publish(
    box_renderer_t *r);

/** Wait for a pending map and release the query set and the buffers.
 *
 * @param r The renderer
 */
void box_timing_fini(
    box_renderer_t *r);
#endif

#endif
