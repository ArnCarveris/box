/**
 * @file box/render.h
 * @brief Render settings and status: the packet ring between the main thread and the renderer.
 *
 * The box.render module extracts every frame into a render packet that the render thread, or
 * the main thread in a browser build, hands to the backend.
 */

#ifndef BOX_RENDER_H
#define BOX_RENDER_H

#include "types.h"

#ifdef BOX_RENDER_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Packets in flight of the RenderSettings the module sets */
#define BOX_RENDER_IN_FLIGHT (2)

/** Largest number of packets in flight */
#define BOX_RENDER_IN_FLIGHT_MAX (8)

/** Fraction of the frame interval a present interval may deviate before it counts as uneven */
#define BOX_RENDER_PRESENT_TOLERANCE (0.1)

/** How frames travel from the main thread to the renderer, applied before the next frame */
ECS_STRUCT(RenderSettings, {
    int32_t in_flight;  /**< Packets in flight, default 2, from 1 to 8 */
    bool main_thread;   /**< The main thread renders the packet, default on in the browser */
});

/** Packet counters the engine writes after every frame, read only for applications */
ECS_STRUCT(RenderStatus, {
    int32_t submitted;  /**< Packets the main thread handed to the renderer */
    int32_t rendered;   /**< Packets the backend finished and the ring got back */
    int32_t in_flight;  /**< Packets submitted or being filled that are not yet rendered */
    int32_t stalls;     /**< Frames whose first packet waited for a free one: every packet was
                         *   in flight, the backpressure of the renderer */
    bool threaded;      /**< A render thread consumes the packets */
    int64_t upload_bytes; /**< Bytes of instance data the last frame uploaded, 0 when unchanged:
                           *   transform_bytes + shading_bytes + surface_bytes of
                           *   box_render_draws_stats_t */
    bool vsync;         /**< Frames present to a window with vsync, FIFO */
    float refresh_rate; /**< Refresh rate in Hz the surface reports, 0 when unknown or none */
    int32_t presented;  /**< Frames the backend presented */
    double present_ms;  /**< Interval between the last two presents */
    int32_t presents_late;  /**< Present intervals over 1.1 / App.fps, counted while capped */
    int32_t presents_early; /**< Present intervals under 0.9 / App.fps, counted while capped */
    int32_t device_losses;  /**< Lost GPU devices the renderer recreated, 0 for none; a failed
                             *   recreation is not counted and drops later frames, and the
                             *   browser neither reports nor recovers a loss (its page does) */
    int32_t shadow_tiles;   /**< Shadow atlas tiles the last extract allocated, over every
                             *   ShadowAtlas */
    int32_t shadow_dropped; /**< Shadow casting lights that needed a tile in the last extract
                             *   and got none, so they light unshadowed */
    int32_t shadow_cascades; /**< Cascades the last extract scheduled to render, over every
                             *   view with shadows (ShadowSettings.interval) */
    int32_t shadow_cascades_kept; /**< Cascades of those views the last extract kept from an
                             *   earlier frame instead */
});

/** How a view culled its instances in the last frame the GPU finished, set on the View entity.
 *
 * Read only for applications. Every instance of the scene counts once: drawn, frustum (or size)
 * culled, or hidden behind closer geometry.
 *
 * The counts are read back from the GPU without waiting and arrive a variable number of frames
 * late, many while the GPU is behind as headless rendering does not wait for it; wait for `frame`
 * to reach a frame instead of counting frames.
 */
ECS_STRUCT(CullStatus, {
    int64_t frame;          /**< Frame the counts are from */
    int32_t instances;      /**< Instances the view tested */
    int32_t visible;        /**< Instances the view drew */
    int32_t frustum_culled; /**< Instances outside the frustum or below the smallest size */
    int32_t occlusion_culled; /**< Instances in the frustum hidden behind closer geometry */
});

/** Register the box.render module.
 *
 * @param world The world
 */
void BoxRenderImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
