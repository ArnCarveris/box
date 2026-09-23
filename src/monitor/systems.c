/**
 * @file monitor/systems.c
 * @brief Attributes frame time to systems and phases on the SystemMonitor singleton.
 *
 * Flecs accumulates the time of every system in ecs_system_t when system time
 * measurement is on, so the monitor only has to difference that total per frame.
 */

#include "monitor_internal.h"

/** Add a system's time to the total of the phase it depends on.
 *
 * @param world The world
 * @param m The monitor
 * @param system The system that ran
 * @param ms Milliseconds it spent
 */
static void box_system_phase(
    const ecs_world_t *world,
    SystemMonitor *m,
    ecs_entity_t system,
    double ms)
{
    ecs_entity_t phase = ecs_get_target(world, system, EcsDependsOn, 0);
    int32_t i, slot = -1;
    for (i = 0; i < BOX_MONITOR_PHASES; i ++) {
        if (m->phases[i].time_ms != 0 && m->phases[i].phase == phase) {
            m->phases[i].time_ms += ms;
            goto done;
        }

        if (slot < 0 && m->phases[i].time_ms == 0 && !m->phases[i].phase) {
            slot = i;
        }
    }

    if (slot >= 0) {
        m->phases[slot].phase = phase;
        m->phases[slot].time_ms = ms;
    }
done:
    return;
}

/** Difference the accumulated time of one system and publish the result.
 *
 * @param world The world
 * @param ctx The per system accounting
 * @param m The monitor
 * @param system The system to attribute
 */
static void box_system_attribute(
    const ecs_world_t *world,
    box_system_monitor_t *ctx,
    SystemMonitor *m,
    ecs_entity_t system)
{
    const ecs_system_t *sys = ecs_system_get(world, system);
    if (!sys) {
        goto done;
    }

    uint64_t total = (uint64_t)((double)sys->time_spent * 1e9);
    ecs_map_val_t *slot = ecs_map_ensure(&ctx->last, system);
    uint64_t last = *slot;
    *slot = total;
    if (!last || total <= last) {
        goto done;
    }

    double ms = (double)(total - last) * 1e-6;
    m->frame_ms += ms;
    m->system_count ++;
    box_monitor_top(m->slowest, BOX_MONITOR_TOP, system, ms);
    box_system_phase(world, m, system, ms);
done:
    return;
}

/** Walk every system of the world and publish the frame breakdown.
 *
 * A frame that collected marks the component modified, which is what the
 * systems panel redraws on.
 *
 * @param it Iterator over the SystemMonitor singleton
 */
static void BoxCollectSystems(
    ecs_iter_t *it)
{
    SystemMonitor *m = ecs_field(it, SystemMonitor, 0);
    box_system_monitor_t *ctx = it->ctx;
    const ecs_world_t *world = ecs_get_world(it->world);

    ecs_os_memset_n(m->slowest, 0, SystemTime, BOX_MONITOR_TOP);
    ecs_os_memset_n(m->phases, 0, PhaseTime, BOX_MONITOR_PHASES);
    m->frame_ms = 0;
    m->system_count = 0;
    if (!m->enabled) {
        goto done;
    }

    ecs_iter_t sit = ecs_query_iter(it->world, ctx->systems);
    while (ecs_query_next(&sit)) {
        int32_t i;
        for (i = 0; i < sit.count; i ++) {
            box_system_attribute(world, ctx, m, sit.entities[i]);
        }
    }

    ecs_modified(it->world, ecs_id(SystemMonitor), SystemMonitor);
done:
    return;
}

/** Turn the flecs system time measurement on or off with the component.
 *
 * @param it Iterator over the SystemMonitor singleton
 */
static void box_system_monitor_on_set(
    ecs_iter_t *it)
{
    SystemMonitor *m = ecs_field(it, SystemMonitor, 0);
    ecs_world_t *world = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(it->world));
    ecs_measure_system_time(world, m->enabled);
}

/** Turn the measurement off again unless the stats addon still needs it.
 *
 * @param it Iterator over the SystemMonitor singleton
 */
static void box_system_monitor_on_remove(
    ecs_iter_t *it)
{
    ecs_world_t *world = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(it->world));
    if (!ecs_lookup(world, "flecs.stats")) {
        ecs_measure_system_time(world, false);
    }
}

/** Free the per system accounting and the query that feeds it.
 *
 * @param ptr The accounting
 */
static void box_system_monitor_free(
    void *ptr)
{
    box_system_monitor_t *ctx = ptr;
    ecs_map_fini(&ctx->last);
    ecs_query_fini(ctx->systems);
    ecs_os_free(ctx);
}

void box_system_monitor_init(
    ecs_world_t *world)
{
    box_system_monitor_t *ctx = ecs_os_calloc_t(box_system_monitor_t);
    ecs_map_init(&ctx->last, NULL);
    ctx->systems = ecs_query(world, {
        .terms = {{ .id = EcsSystem }},
        .cache_kind = EcsQueryCacheNone,
        .flags = EcsQueryMatchDisabled
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxCollectSystems" }),
        .phase = EcsPostFrame,
        .query.terms = {{ .id = ecs_id(SystemMonitor), .src.id = ecs_id(SystemMonitor) }},
        .callback = BoxCollectSystems,
        .ctx = ctx,
        .ctx_free = box_system_monitor_free,
        .immediate = true
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "BoxSystemMonitorOnSet" }),
        .query.terms = {{ .id = ecs_id(SystemMonitor) }},
        .events = { EcsOnSet },
        .callback = box_system_monitor_on_set
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "BoxSystemMonitorOnRemove" }),
        .query.terms = {{ .id = ecs_id(SystemMonitor) }},
        .events = { EcsOnRemove },
        .callback = box_system_monitor_on_remove
    });
}
