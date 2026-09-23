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
#include "../surface/surface_internal.h"

/** Path of the component that holds the renderer of a surface */
#define BOX_GPU_SURFACE_PATH "box.surface.SurfaceImpl"

/** Renderer of the first surface of a world.
 *
 * @param world The world
 * @return The renderer, or NULL when the world renders nothing
 */
static box_renderer_t* box_gpu_renderer(
    ecs_world_t *world)
{
    box_renderer_t *result = NULL;
    ecs_entity_t comp = ecs_lookup(world, BOX_GPU_SURFACE_PATH);
    if (!comp) {
        goto done;
    }

    ecs_iter_t it = ecs_each_id(world, comp);
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const SurfaceImpl *impl = ecs_get_id(world, it.entities[i], comp);
            if (impl && impl->renderer && !result) {
                result = impl->renderer;
            }
        }
    }

done:
    return result;
}

/** Keep a pass name in a reflected string field, reusing it while it is the same.
 *
 * @param slot The field
 * @param name The name the pass has now, NULL for an unused entry
 */
static void box_gpu_name(
    char **slot,
    const char *name)
{
    if (*slot && name && !ecs_os_strcmp(*slot, name)) {
        goto done;
    }

    ecs_os_free(*slot);
    *slot = name ? ecs_os_strdup(name) : NULL;
done:
    return;
}

/** Copy the passes of the last resolved frame into the component.
 *
 * @param out The component
 * @param gpu The passes the renderer resolved
 */
static void box_gpu_write(
    GpuTimings *out,
    const box_renderer_gpu_t *gpu)
{
    int32_t count = (int32_t)gpu->count;
    if (count > BOX_MONITOR_PASSES) {
        count = BOX_MONITOR_PASSES;
    }

    out->supported = gpu->supported;
    out->frame = (int32_t)gpu->frame;
    out->pass_count = count;
    out->frame_ms = gpu->frame_ms;
    int32_t i;
    for (i = 0; i < BOX_MONITOR_PASSES; i ++) {
        box_gpu_name(&out->passes[i].name, i < count ? gpu->passes[i].name : NULL);
        out->passes[i].time_ms = i < count ? gpu->passes[i].time_ms : 0.0;
    }
}

/** Ask the renderer for the timings and publish them on every component.
 *
 * @param it Iterator over the GpuTimings components
 */
static void BoxPublishGpuTimings(
    ecs_iter_t *it)
{
    GpuTimings *timings = ecs_field(it, GpuTimings, 0);
    box_renderer_t *r = box_gpu_renderer(it->world);
    if (!r) {
        goto done;
    }

    box_renderer_set_gpu_timings(r, true);
    box_renderer_gpu_t gpu;
    box_renderer_gpu_timings(r, &gpu);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_gpu_write(&timings[i], &gpu);
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

    box_renderer_t *r = box_gpu_renderer(it->world);
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
        .callback = BoxPublishGpuTimings
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "BoxGpuTimingsOnRemove" }),
        .query.terms = {{ .id = ecs_id(GpuTimings) }},
        .events = { EcsOnRemove },
        .callback = BoxGpuTimingsOnRemove
    });
}
