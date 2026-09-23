/**
 * @file monitor/stats.c
 * @brief Mirrors the flecs stats addon into the fields of the EcsStats singleton.
 *
 * Setting the component imports the addon, which is what collects the world and
 * system statistics; this file reduces them into reflected fields at the period
 * and disables the addon again when the component goes.
 */

#include "monitor_internal.h"

/** Whether the system monitor still needs the system time measurement.
 *
 * @param world The world
 * @return true when a SystemMonitor singleton collects
 */
static bool box_stats_monitor_needed(
    const ecs_world_t *world)
{
    const SystemMonitor *m = ecs_singleton_get(world, SystemMonitor);

    return m && m->enabled;
}

/** Import the flecs stats addon, or enable it again after this mirror disabled it.
 *
 * @param it Iterator over the EcsStats singleton
 */
static void box_stats_on_set(
    ecs_iter_t *it)
{
    ecs_world_t *world = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(it->world));
    box_stats_monitor_t *ctx = it->ctx;
    if (ecs_stage_is_readonly(world)) {
        goto done;
    }

    ecs_entity_t module = ecs_lookup(world, "flecs.stats");
    if (!module) {
        ECS_IMPORT(world, FlecsStats);
        ctx->imported = true;
        ctx->frames = 0;
        goto done;
    }

    if (ctx->imported && ecs_has_id(world, module, EcsDisabled)) {
        ecs_enable(world, module, true);
        ecs_measure_frame_time(world, true);
        ecs_measure_system_time(world, true);
        ctx->frames = 0;
    }

done:
    return;
}

/** Disable the monitors of the stats addon when this mirror imported them.
 *
 * A world that is being torn down removes the singleton by deleting it, which
 * lands it in the empty root table; nothing is changed then. The system time
 * measurement stays on while a SystemMonitor still reads it.
 *
 * @param it Iterator over the removed EcsStats singleton
 */
static void box_stats_on_remove(
    ecs_iter_t *it)
{
    if (it->other_table && !ecs_table_get_type(it->other_table)->count) {
        goto done;
    }

    ecs_world_t *world = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(it->world));
    box_stats_monitor_t *ctx = it->ctx;
    ecs_entity_t module = ecs_lookup(world, "flecs.stats");
    if (!ctx->imported || !module || ecs_stage_is_readonly(world)) {
        goto done;
    }

    ecs_enable(world, module, false);
    if (!box_stats_monitor_needed(world)) {
        ecs_measure_system_time(world, false);
    }

done:
    return;
}

/** Reduce the last frames of the samples of a system into one entry.
 *
 * The addon keeps one sample per frame; the frames since the last collection
 * are reduced, at most the window the addon holds.
 *
 * @param system The system
 * @param s Its samples
 * @param frames Frames since the last collection
 * @param out Receives the average, minimum and maximum milliseconds per frame
 */
static void box_stats_reduce(
    ecs_entity_t system,
    const ecs_system_stats_t *s,
    int32_t frames,
    SystemTime *out)
{
    int32_t count = frames < ECS_STAT_WINDOW ? frames : ECS_STAT_WINDOW;
    count = count > 0 ? count : 1;
    double total = 0.0;
    double lo = 0.0;
    double hi = 0.0;
    int32_t i;
    for (i = 0; i < count; i ++) {
        int32_t t = (s->query.t - i + ECS_STAT_WINDOW) % ECS_STAT_WINDOW;
        double avg = (double)s->time_spent.gauge.avg[t] * 1000.0;
        double min = (double)s->time_spent.gauge.min[t] * 1000.0;
        double max = (double)s->time_spent.gauge.max[t] * 1000.0;
        total += avg;
        lo = i == 0 || min < lo ? min : lo;
        hi = max > hi ? max : hi;
    }

    out->system = system;
    out->time_ms = total / (double)count;
    out->min_ms = lo;
    out->max_ms = hi;
}

/** Whether a system belongs to a module, at any depth.
 *
 * @param world The world
 * @param system The system
 * @param module The module
 * @return true when the module is an ancestor of the system
 */
static bool box_stats_in_module(
    const ecs_world_t *world,
    ecs_entity_t system,
    ecs_entity_t module)
{
    ecs_entity_t parent = ecs_get_parent(world, system);
    while (parent && parent != module) {
        parent = ecs_get_parent(world, parent);
    }

    return parent == module;
}

/** Fill a list with the slowest systems of the last frames by their average time.
 *
 * The monitors of the stats addon only run because the component exists, so
 * they are left out of the list.
 *
 * @param world The world
 * @param frames Frames since the last collection
 * @param top Receives the slowest systems, slowest first
 */
static void box_stats_slowest(
    const ecs_world_t *world,
    int32_t frames,
    SystemTime *top)
{
    ecs_os_memset_n(top, 0, SystemTime, BOX_MONITOR_TOP);
    const EcsSystemStats *stats = ecs_get_pair(
        world, EcsWorld, EcsSystemStats, EcsPeriod1s);
    if (!stats) {
        goto done;
    }

    ecs_entity_t addon = ecs_lookup(world, "flecs.stats");
    ecs_map_iter_t mit = ecs_map_iter(&stats->stats);
    while (ecs_map_next(&mit)) {
        ecs_entity_t system = (ecs_entity_t)ecs_map_key(&mit);
        if (box_stats_in_module(world, system, addon)) {
            continue;
        }

        SystemTime entry;
        box_stats_reduce(system, ecs_map_ptr(&mit), frames, &entry);
        if (entry.max_ms > 0.0) {
            box_monitor_top(top, BOX_MONITOR_TOP, &entry);
        }
    }
done:
    return;
}

/** Add a frame time to the retained window and return its average.
 *
 * @param ctx The collection state
 * @param history Frame times the window keeps
 * @param frame_ms The frame time to add
 * @return Average frame time of the window in milliseconds
 */
static double box_stats_window(
    box_stats_monitor_t *ctx,
    int32_t history,
    double frame_ms)
{
    ctx->frame_ms[ctx->next] = frame_ms;
    ctx->next = (ctx->next + 1) % history;
    if (ctx->count < history) {
        ctx->count ++;
    }

    double total = 0;
    int32_t i;
    for (i = 0; i < ctx->count; i ++) {
        total += ctx->frame_ms[i];
    }

    return total / (double)ctx->count;
}

/** Copy the world summary and the slowest systems into the EcsStats singleton.
 *
 * A collection marks the component modified, which is what the systems panel
 * redraws on.
 *
 * @param it Iterator over the EcsStats singleton
 */
static void BoxCollectStats(
    ecs_iter_t *it)
{
    EcsStats *s = ecs_field(it, EcsStats, 0);
    box_stats_monitor_t *ctx = it->ctx;
    ecs_world_t *world = it->world;

    double period = s->period_s > 0 ? s->period_s : BOX_MONITOR_PERIOD;
    ctx->elapsed += (double)it->delta_time;
    ctx->frames ++;
    if (ctx->elapsed < period) {
        goto done;
    }

    ctx->elapsed = 0;
    if (!box_monitor_stats_active(world)) {
        goto done;
    }

    const EcsWorldSummary *w = ecs_get(world, EcsWorld, EcsWorldSummary);
    if (!w) {
        goto done;
    }

    int32_t history = s->history > 0 ? s->history : BOX_MONITOR_HISTORY;
    if (history > BOX_MONITOR_HISTORY_MAX) {
        history = BOX_MONITOR_HISTORY_MAX;
    }

    if (ctx->count > history) {
        ctx->count = history;
        ctx->next = 0;
    }

    s->entity_count = (int32_t)w->entity_count;
    s->table_count = (int32_t)w->table_count;
    s->systems_ran = (int32_t)w->systems_ran_frame;
    s->frame_time_ms = w->frame_time_frame * 1000.0;
    s->system_time_ms = w->system_time_frame * 1000.0;
    s->merge_time_ms = w->merge_time_frame * 1000.0;
    s->fps = w->fps;
    s->frame_time_avg_ms = box_stats_window(ctx, history, s->frame_time_ms);
    s->collections ++;
    box_stats_slowest(world, ctx->frames, s->slowest);
    ctx->frames = 0;
    ecs_modified(world, ecs_id(EcsStats), EcsStats);
done:
    return;
}

/** Free the collection state of the statistics mirror.
 *
 * @param ptr The state
 */
static void box_stats_monitor_free(
    void *ptr)
{
    ecs_os_free(ptr);
}

void box_stats_monitor_init(
    ecs_world_t *world)
{
    box_stats_monitor_t *ctx = ecs_os_calloc_t(box_stats_monitor_t);
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxCollectStats" }),
        .phase = EcsPostFrame,
        .query.terms = {{ .id = ecs_id(EcsStats), .src.id = ecs_id(EcsStats) }},
        .callback = BoxCollectStats,
        .ctx = ctx,
        .ctx_free = box_stats_monitor_free,
        .immediate = true
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "BoxStatsOnSet" }),
        .query.terms = {{ .id = ecs_id(EcsStats) }},
        .events = { EcsOnSet },
        .callback = box_stats_on_set,
        .ctx = ctx
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "BoxStatsOnRemove" }),
        .query.terms = {{ .id = ecs_id(EcsStats) }},
        .events = { EcsOnRemove },
        .callback = box_stats_on_remove,
        .ctx = ctx
    });
}
