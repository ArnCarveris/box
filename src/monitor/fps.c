/**
 * @file monitor/fps.c
 * @brief The Fps singleton, the frame time ring it is filled from and the panel script.
 *
 * A system samples the frame time and publishes the frame rate, mean, worst and
 * recent frame times of the last second on the Fps singleton. The panel script
 * BOX_MONITOR_FPS_PANEL loads into box.monitor.fps_panel, hidden until visible.
 */

#include "monitor_internal.h"
#include "../shader/shader_internal.h"

void box_fps_ring_push(
    box_fps_ring_t *ring,
    float time_ms)
{
    if (time_ms < 0.0f) {
        goto done;
    }

    ring->times_ms[ring->next] = time_ms;
    ring->next = (ring->next + 1) % BOX_FPS_RING;
    if (ring->count < BOX_FPS_RING) {
        ring->count ++;
    }

done:
    return;
}

float box_fps_ring_get(
    const box_fps_ring_t *ring,
    int32_t back)
{
    float result = 0.0f;
    if (back >= 0 && back < ring->count) {
        int32_t slot = ring->next - 1 - back;
        while (slot < 0) {
            slot += BOX_FPS_RING;
        }

        result = ring->times_ms[slot];
    }

    return result;
}

int32_t box_fps_ring_window(
    const box_fps_ring_t *ring,
    float window_ms)
{
    int32_t result = 0;
    float total = 0.0f;
    while (result < ring->count && total < window_ms) {
        total += box_fps_ring_get(ring, result);
        result ++;
    }

    return result;
}

void box_fps_ring_stats(
    const box_fps_ring_t *ring,
    float window_ms,
    box_fps_stats_t *out)
{
    int32_t frames = box_fps_ring_window(ring, window_ms);
    float total = 0.0f;
    float worst = 0.0f;
    int32_t i;
    for (i = 0; i < frames; i ++) {
        float time_ms = box_fps_ring_get(ring, i);
        total += time_ms;
        if (time_ms > worst) {
            worst = time_ms;
        }
    }

    out->frames = frames;
    out->mean_ms = frames ? total / (float)frames : 0.0f;
    out->worst_ms = worst;
    out->fps = total > 0.0f ? (float)frames * 1000.0f / total : 0.0f;
}

/** Sample the frame and publish the window and the history on the Fps singleton.
 *
 * @param it Iterator over the Fps singleton
 */
static void BoxPublishFps(
    ecs_iter_t *it)
{
    Fps *fps = ecs_field(it, Fps, 0);
    box_fps_ring_t *ring = it->ctx;
    float time_ms = (float)(it->delta_time * 1000.0);
    box_fps_ring_push(ring, time_ms);

    box_fps_stats_t stats;
    box_fps_ring_stats(ring, BOX_FPS_WINDOW_MS, &stats);
    fps->fps = stats.fps;
    fps->mean_ms = stats.mean_ms;
    fps->worst_ms = stats.worst_ms;
    fps->frame_ms = time_ms;
    ecs_os_memmove(fps->history, &fps->history[1],
        (BOX_MONITOR_FPS_HISTORY - 1) * ECS_SIZEOF(float));
    fps->history[BOX_MONITOR_FPS_HISTORY - 1] = time_ms;
    if (fps->count < BOX_MONITOR_FPS_HISTORY) {
        fps->count ++;
    }

    ecs_modified(it->world, ecs_id(Fps), Fps);
}

/** Free the ring of the Fps monitor.
 *
 * @param ptr The ring
 */
static void box_fps_ring_free(
    void *ptr)
{
    ecs_os_free(ptr);
}

/** Load the frame rate panel script into box.monitor.
 *
 * @param world The world
 */
static void box_fps_panel_load(
    ecs_world_t *world)
{
    char resolved[1024];
    if (!box_locate(BOX_MONITOR_FPS_PANEL, resolved, sizeof(resolved))) {
        ecs_warn("box.monitor: no fps panel at '%s'", BOX_MONITOR_FPS_PANEL);
        goto done;
    }

    ecs_entity_t script = ecs_entity(world, {
        .name = "fps_script", .parent = ecs_lookup(world, "box.monitor") });
    if (!ecs_script(world, { .entity = script, .filename = resolved })) {
        ecs_err("box.monitor: failed to load '%s'", resolved);
    }

done:
    return;
}

void box_fps_monitor_init(
    ecs_world_t *world)
{
    ecs_set_hooks(world, Fps, { .ctor = flecs_default_ctor });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxPublishFps" }),
        .phase = EcsPostFrame,
        .query.terms = {{ .id = ecs_id(Fps), .src.id = ecs_id(Fps) }},
        .callback = BoxPublishFps,
        .ctx = ecs_os_calloc_t(box_fps_ring_t),
        .ctx_free = box_fps_ring_free,
        .immediate = true
    });

    ecs_singleton_add(world, Fps);
    box_fps_panel_load(world);
}
