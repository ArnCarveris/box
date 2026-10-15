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

/** Hz a refresh rate may be off and still count as a whole multiple of the frame rate cap:
 * displays report whole Hz, so a 59.94 Hz panel reads as 59 or 60 */
#define BOX_RENDER_REFRESH_SLACK (1.0)

/** How frames travel from the main thread to the renderer, applied before the next frame.
 *
 * display_rate simulates a vsync display for offscreen targets, like a FIFO swap chain of three
 * images: a frame shows for swap_interval refreshes from the first free refresh after its submit
 * and its GPU work, and the renderer blocks while a present is still queued.
 */
ECS_STRUCT(RenderSettings, {
    int32_t in_flight;  /**< Packets in flight, default 2, from 1 to 8 */
    bool main_thread;   /**< The main thread renders the packet, default on in the browser */
    float display_rate; /**< Refresh rate in Hz of the simulated vsync display, 0 for none */
});

/** Packet counters the engine writes after every frame, read only for applications */
ECS_STRUCT(RenderStatus, {
    int32_t submitted;  /**< Packets the main thread handed to the renderer */
    int32_t rendered;   /**< Packets the backend finished and the ring got back */
    int32_t in_flight;  /**< Packets submitted or being filled that are not yet rendered */
    int32_t stalls;     /**< Frames the main thread waited for a free packet */
    bool threaded;      /**< A render thread consumes the packets */
    int64_t upload_bytes; /**< Bytes of instance data the last frame uploaded, 0 when unchanged */
    bool vsync;         /**< Frames present with vsync: a window with vsync or display_rate */
    float refresh_rate; /**< Refresh rate in Hz of the vsync display, 0 when unknown or none */
    int32_t swap_interval; /**< Refreshes the last frame is shown for: refresh_rate / App.fps,
                            *   1 without a cap, alternating in a cadence, 0 when the refresh
                            *   rate is unknown and the cap sleeps */
    int32_t cadence;    /**< Frames after which the swap intervals repeat: 1 when every frame
                         *   is shown equally long, 3 for 90 FPS on 120 Hz (1, 1, 2 refreshes),
                         *   0 when they do not repeat within 240 frames or nothing paces */
    int32_t presented;  /**< Frames presented: shown on the simulated display or handed over */
    double present_ms;  /**< Interval between the last two presents */
    int32_t presents_late;  /**< Present intervals over 1.1 frame intervals: the swap interval of
                             *   the frame before on the simulated display, so a repeated
                             *   refresh (a late CPU or GPU) counts, else 1 / App.fps */
    int32_t presents_early; /**< Present intervals under 0.9 frame intervals */
    int32_t delta_mismatches; /**< Frames on the simulated display whose delta differs over
                               *   BOX_FRAME_TOLERANCE_MS from the time it was shown after the
                               *   frame before it, so the motion does not match the display */
});

/** How a view culled its instances in the last frame the GPU finished, set on the View entity.
 *
 * Written by the engine a frame or two late, read only for applications. Every instance of the
 * scene counts once: drawn, frustum (or size) culled, or hidden behind closer geometry.
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
