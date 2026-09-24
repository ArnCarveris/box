/**
 * @file renderer/renderer.h
 * @brief Public interface of the renderer, usable without WebGPU headers.
 *
 * The renderer owns the GPU device and renders packets from its queue.
 */

#ifndef BOX_RENDERER_H
#define BOX_RENDERER_H

#include "packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque renderer handle */
typedef struct box_renderer_t box_renderer_t;

#include "timing.h"
#include "counters.h"

/** Instance counts of the last frame the renderer drew.
 *
 * The counts of the compute culled batches come from a read back of the draw
 * arguments, so they trail the frame they belong to by one or two frames.
 */
typedef struct box_renderer_stats_t {
    uint32_t drawn;   /**< Instances the frame drew, which the first pass overshoots */
    uint32_t culled;  /**< Instances the frame skipped because no view could see them */
    uint32_t occluded; /**< Instances the depth pyramid of a view found hidden behind others */
    uint32_t shadow_rebuilds; /**< Cached static cascade layers the frame rendered again */
} box_renderer_stats_t;

/** Per pass GPU times of the last frame the renderer resolved.
 *
 * A name recorded by several views or cascades holds their sum. The numbers
 * trail the frame they belong to by one or two frames.
 */
typedef struct box_renderer_gpu_t {
    box_timing_pass_t passes[BOX_TIMING_PASS_MAX]; /**< Passes of the frame, in record order */
    uint32_t count;   /**< Passes the frame recorded, one entry per name */
    uint32_t frame;   /**< Frame the renderer measured them in */
    double frame_ms;  /**< Milliseconds the passes add up to */
    bool supported;   /**< The device can time its passes */
} box_renderer_gpu_t;

/** Parameters for creating a renderer */
typedef struct box_renderer_desc_t {
    void *metal_layer;            /**< CAMetalLayer of the window, NULL for offscreen */
    const char *canvas_selector;  /**< HTML canvas selector on web, NULL for offscreen */
    uint32_t width;               /**< Initial target width in pixels */
    uint32_t height;              /**< Initial target height in pixels */
    uint32_t anisotropy;          /**< Anisotropic samples of the texture samplers */
    bool vsync;                   /**< Present with vertical sync */
    bool threaded;                /**< Run on a dedicated thread */
} box_renderer_desc_t;

/** Create a renderer and start its thread when requested.
 *
 * @param desc Creation parameters
 * @return The renderer, or NULL when the device could not be created
 */
box_renderer_t* box_renderer_create(
    const box_renderer_desc_t *desc);

/** Stop the renderer thread and release GPU resources.
 *
 * @param r The renderer
 */
void box_renderer_destroy(
    box_renderer_t *r);

/** Take a free packet, blocking while too many frames are in flight.
 *
 * @param r The renderer
 * @return A packet to fill, or NULL when the renderer stopped
 */
box_packet_t* box_renderer_acquire(
    box_renderer_t *r);

/** Hand a filled packet to the renderer; renders immediately when not threaded.
 *
 * @param r The renderer
 * @param p The packet
 */
void box_renderer_submit(
    box_renderer_t *r,
    box_packet_t *p);

/** Check whether the GPU device is ready to render.
 *
 * @param r The renderer
 * @return true when frames can be rendered
 */
bool box_renderer_ready(
    const box_renderer_t *r);

/** Set whether the GPU device is ready to render.
 *
 * The web path calls this from the device callback, because the device request
 * completes long after the first packets were produced.
 *
 * @param r The renderer
 * @param ready Whether frames can be rendered
 */
void box_renderer_set_ready(
    box_renderer_t *r,
    bool ready);

/** Check whether the renderer holds GPU buffers for a mesh handle.
 *
 * @param r The renderer
 * @param mesh The mesh handle
 * @return true when the mesh was uploaded
 */
bool box_renderer_has_mesh(
    const box_renderer_t *r,
    uint32_t mesh);

/** Number of static instances the renderer draws for a mesh handle.
 *
 * @param r The renderer
 * @param mesh The mesh handle
 * @return The instance count, 0 when the mesh holds none
 */
uint32_t box_renderer_static_count(
    const box_renderer_t *r,
    uint32_t mesh);

/** Instance counts of the last frame the renderer drew.
 *
 * @param r The renderer
 * @param out The counts to fill
 */
void box_renderer_stats(
    const box_renderer_t *r,
    box_renderer_stats_t *out);

/** Record the GPU time of every pass of a frame, or stop recording it.
 *
 * Nothing is created and no pass is timed while this is off, and a device
 * without the timestamp query feature reports no passes either way.
 *
 * @param r The renderer
 * @param enable Time the passes of the frames that follow
 */
void box_renderer_set_gpu_timings(
    box_renderer_t *r,
    bool enable);

/** Per pass GPU times of the last frame the renderer resolved, copied whole.
 *
 * @param r The renderer
 * @param out The times to fill
 */
void box_renderer_gpu_timings(
    box_renderer_t *r,
    box_renderer_gpu_t *out);

/** Count the work of the frames that follow, or stop counting it.
 *
 * Nothing is counted while the flags are zero. A frame starts counting with the
 * flags it finds when it begins, so a frame is always counted whole. Starting
 * to count again forgets the frame published before counting stopped.
 *
 * @param r The renderer
 * @param flags BOX_COUNT_ flags of what to count, zero for nothing
 */
void box_renderer_set_counters(
    box_renderer_t *r,
    uint32_t flags);

/** Counts of the last frame the renderer finished counting.
 *
 * A frame is published whole, after the read back of its indirect draws, so
 * the counts trail the frame they belong to by one or two frames.
 *
 * @param r The renderer
 * @param out The counts to fill
 * @return true when a frame was published
 */
bool box_renderer_counters(
    box_renderer_t *r,
    box_counters_t *out);

/** Number of frames the renderer has presented.
 *
 * @param r The renderer
 * @return Frame count
 */
uint32_t box_renderer_frames(
    const box_renderer_t *r);

/** Number of device errors the renderer reported since it was created.
 *
 * @param r The renderer
 * @return Error count
 */
uint32_t box_renderer_errors(
    const box_renderer_t *r);

/** Wait until every capture the renderer took is written to its file.
 *
 * @param r The renderer
 */
void box_renderer_captures_wait(
    box_renderer_t *r);

#ifdef __cplusplus
}
#endif

#endif
