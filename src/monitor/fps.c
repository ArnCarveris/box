/**
 * @file monitor/fps.c
 * @brief Defaults of the FpsOverlay component and the window it publishes.
 *
 * The panel itself is drawn by the renderer from the intervals between the
 * packets it processes. This file keeps the same window on the game side, so
 * the frame rate, the mean and the worst frame time of the last second are
 * reflected fields that the terminal, the REST API and the status page read
 * without an endpoint of their own. Nothing is sampled while no entity and no
 * singleton carries the component.
 */

#include "monitor_internal.h"
#include "../renderer/overlay.h"

/** Frame times the published numbers of every overlay are taken from */
typedef struct box_fps_monitor_t {
    box_fps_ring_t ring; /**< Frame times of the most recent frames */
    int64_t frame;       /**< World frame the last sample was taken in */
} box_fps_monitor_t;

/** FpsOverlay constructor: enabled, in the top right corner and mostly opaque.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_fps_overlay_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    ecs_os_memset(ptr, 0, (ecs_size_t)count * (ecs_size_t)sizeof(FpsOverlay));
    FpsOverlay *o = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        o[i].enabled = true;
        o[i].corner = FpsTopRight;
        o[i].scale = 1.0f;
        o[i].opacity = 0.7f;
    }
}

/** Sample the frame and publish the window on every overlay of the world.
 *
 * @param it Iterator over the FpsOverlay components
 */
static void BoxPublishFps(
    ecs_iter_t *it)
{
    FpsOverlay *overlay = ecs_field(it, FpsOverlay, 0);
    box_fps_monitor_t *ctx = it->ctx;
    const ecs_world_info_t *info = ecs_get_world_info(it->world);
    if (ctx->frame != info->frame_count_total) {
        ctx->frame = info->frame_count_total;
        box_fps_ring_push(&ctx->ring, (float)(info->delta_time * 1000.0));
    }

    box_fps_stats_t stats;
    box_fps_ring_stats(&ctx->ring, BOX_FPS_WINDOW_MS, &stats);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        overlay[i].fps = stats.fps;
        overlay[i].mean_ms = stats.mean_ms;
        overlay[i].worst_ms = stats.worst_ms;
    }
}

/** Free the window of the overlay monitor.
 *
 * @param ptr The window
 */
static void box_fps_monitor_free(
    void *ptr)
{
    ecs_os_free(ptr);
}

void box_fps_overlay_init(
    ecs_world_t *world)
{
    ecs_set_hooks(world, FpsOverlay, { .ctor = box_fps_overlay_ctor });

    box_fps_monitor_t *ctx = ecs_os_calloc_t(box_fps_monitor_t);
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxPublishFps" }),
        .phase = EcsPostFrame,
        .query.terms = {{ .id = ecs_id(FpsOverlay) }},
        .callback = BoxPublishFps,
        .ctx = ctx,
        .ctx_free = box_fps_monitor_free
    });
}
