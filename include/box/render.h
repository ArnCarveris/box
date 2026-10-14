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

/** Packets in flight when RenderSettings.in_flight is 0 */
#define BOX_RENDER_IN_FLIGHT (2)

/** Largest number of packets in flight */
#define BOX_RENDER_IN_FLIGHT_MAX (8)

/** How frames travel from the main thread to the renderer, applied before the next frame */
ECS_STRUCT(RenderSettings, {
    int32_t in_flight;  /**< Packets in flight, default 2, at most 8, 0 for the default */
    bool main_thread;   /**< The main thread renders the packet, default on in the browser */
});

/** Packet counters the engine writes after every frame, read only for applications */
ECS_STRUCT(RenderStatus, {
    int32_t submitted;  /**< Packets the main thread handed to the renderer */
    int32_t rendered;   /**< Packets the backend finished and the ring got back */
    int32_t in_flight;  /**< Packets submitted or being filled that are not yet rendered */
    int32_t stalls;     /**< Frames the main thread waited for a free packet */
    bool threaded;      /**< A render thread consumes the packets */
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
