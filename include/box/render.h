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
 * Read only for applications. Every instance of the scene counts once: drawn, out of its
 * DrawRange, frustum (or size) culled, or hidden behind closer geometry.
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
    int32_t range_culled;   /**< Instances outside their DrawRange from the camera of the view,
                             *   in the frustum or not; not counted as frustum culled */
    int32_t batches_skipped; /**< Batches whose draws the view skipped by their bounds
                             *   (CullSettings.bounds), 0 without; their instances count as
                             *   frustum culled */
    int32_t casters_skipped; /**< Batches whose draws the shadow cascades of the view skipped by
                             *   their bounds, summed over the cascades */
});

/** Distances from the camera of a view between which a shape draws, set on a shape or on any
 * ancestor of shapes, a prefab or a template part; inherited by the instances of a prefab.
 *
 * The nearest DrawRange of a shape, its own or that of the closest ChildOf ancestor, applies to
 * it. The distance is measured from the camera of the view to the origin of the shape (the
 * translation of its world transform), so a near and a far version at the same place measure
 * the same distance. Each view measures from its own camera, and the shadow cascades of a view
 * from the camera of the view, so shadows fade with the shape.
 *
 * Beyond far + fade or nearer than near - fade the GPU cull pass drops the instance
 * (CullStatus.range_culled). Within fade of near or far it draws with a screen door dither in
 * the depth prepass, the color and the shadow passes: the share of pixels drawn goes from 0 to
 * 1 over [near - fade, near + fade] and from 1 to 0 over [far - fade, far + fade]. The dither
 * threshold of a fade in mirrors that of a fade out, so a shape with far F and another with near
 * F and the same fade draw exactly one of the two at each pixel.
 *
 * The shapes of one batch share their range: shapes with different ranges draw in different
 * batches. Planar reflections measure from their mirrored camera; bakes, captures and the shadow
 * tiles of spot and point lights ignore the range. */
ECS_STRUCT(DrawRange, {
    float near_;    /**< Distance in meters below which the shape does not draw, 0 for none */
    float far_;     /**< Distance in meters beyond which the shape does not draw, 0 for none */
    float fade;     /**< Half width in meters of the dithered crossfade around near and far, 0
                     *   for a hard cut */
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
