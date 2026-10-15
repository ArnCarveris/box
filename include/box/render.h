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
    int32_t stalls;     /**< Frames the main thread waited for a free packet */
    bool threaded;      /**< A render thread consumes the packets */
    int64_t upload_bytes; /**< Bytes of instance data the last frame uploaded, 0 when unchanged */
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
