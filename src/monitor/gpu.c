/**
 * @file monitor/gpu.c
 * @brief Publishes the per pass GPU times of the renderer on the GpuTimings component.
 *
 * Requests timestamp queries from the renderer while the component exists and
 * copies the last resolved frame into reflected fields. Queries are turned off
 * again when the component is removed.
 */

#include "monitor_internal.h"
#include "../renderer/renderer.h"

/** Copy the passes of the last resolved frame into the component.
 *
 * @param out The component
 * @param gpu The passes the renderer resolved
 * @return true when the component names another frame than before
 */
static bool box_gpu_write(
    GpuTimings *out,
    const box_renderer_gpu_t *gpu)
{
    int32_t count = (int32_t)gpu->count;
    if (count > BOX_MONITOR_PASSES) {
        count = BOX_MONITOR_PASSES;
    }

    bool result = out->supported != gpu->supported || out->frame != (int32_t)gpu->frame ||
        out->pass_count != count;

    out->supported = gpu->supported;
    out->frame = (int32_t)gpu->frame;
    out->pass_count = count;
    out->frame_ms = gpu->frame_ms;
    int32_t i;
    for (i = 0; i < BOX_MONITOR_PASSES; i ++) {
        box_monitor_name(&out->passes[i].name, i < count ? gpu->passes[i].name : NULL);
        out->passes[i].time_ms = i < count ? gpu->passes[i].time_ms : 0.0;
    }

    return result;
}

/** Ask the renderer for the timings and publish them on every component.
 *
 * A component that names a newly resolved frame is marked modified, which is
 * what the GPU panel redraws on.
 *
 * @param it Iterator over the GpuTimings components
 */
static void BoxPublishGpuTimings(
    ecs_iter_t *it)
{
    GpuTimings *timings = ecs_field(it, GpuTimings, 0);
    box_renderer_t *r = box_monitor_renderer(it->world);
    if (!r) {
        goto done;
    }

    box_renderer_set_gpu_timings(r, true);
    box_renderer_gpu_t gpu;
    box_renderer_gpu_timings(r, &gpu);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (box_gpu_write(&timings[i], &gpu)) {
            ecs_modified_id(it->world, it->entities[i], ecs_id(GpuTimings));
        }
    }

done:
    return;
}

/** Stop the timestamp queries when the component is removed.
 *
 * An entity that is being deleted lands in the empty root table, which is how
 * a world that is being torn down removes the component; the surface of such a
 * world may already have destroyed its renderer, so nothing is turned off.
 *
 * @param it Iterator over the removed GpuTimings components
 */
static void BoxGpuTimingsOnRemove(
    ecs_iter_t *it)
{
    if (it->other_table && !ecs_table_get_type(it->other_table)->count) {
        goto done;
    }

    box_renderer_t *r = box_monitor_renderer(it->world);
    if (r) {
        box_renderer_set_gpu_timings(r, false);
    }

done:
    return;
}

void box_gpu_monitor_init(
    ecs_world_t *world)
{
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxPublishGpuTimings" }),
        .phase = EcsPostFrame,
        .query.terms = {{ .id = ecs_id(GpuTimings) }},
        .callback = BoxPublishGpuTimings,
        .immediate = true
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "BoxGpuTimingsOnRemove" }),
        .query.terms = {{ .id = ecs_id(GpuTimings) }},
        .events = { EcsOnRemove },
        .callback = BoxGpuTimingsOnRemove
    });
}
