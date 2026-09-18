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

/** Instance counts of the last frame the renderer drew.
 *
 * The counts of the compute culled batches come from a read back of the draw
 * arguments, so they trail the frame they belong to by one or two frames.
 */
typedef struct box_renderer_stats_t {
    uint32_t drawn;   /**< Instances the frame drew */
    uint32_t culled;  /**< Instances the frame skipped because no view could see them */
} box_renderer_stats_t;

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

/** Number of frames the renderer has presented.
 *
 * @param r The renderer
 * @return Frame count
 */
uint32_t box_renderer_frames(
    const box_renderer_t *r);

#ifdef __cplusplus
}
#endif

#endif
