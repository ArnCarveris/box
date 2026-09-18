/**
 * @file monitor/stats.c
 * @brief Mirrors the flecs stats addon into the fields of the EcsStats singleton.
 *
 * Setting the component imports the addon, which is what collects the world and
 * system statistics; this file copies them into reflected fields at the period.
 */

#include "monitor_internal.h"

/** Import the flecs stats addon when the world does not have it yet.
 *
 * @param it Iterator over the EcsStats singleton
 */
static void box_stats_on_set(
    ecs_iter_t *it)
{
    ecs_world_t *world = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(it->world));
    if (ecs_lookup(world, "flecs.stats") || ecs_stage_is_readonly(world)) {
        goto done;
    }

    ECS_IMPORT(world, FlecsStats);
done:
    return;
}

/** Fill a list with the slowest systems the stats addon measured.
 *
 * @param world The world
 * @param top Receives the slowest systems, slowest first
 */
static void box_stats_slowest(
    const ecs_world_t *world,
    SystemTime *top)
{
    ecs_os_memset_n(top, 0, SystemTime, BOX_MONITOR_TOP);
    const EcsSystemStats *stats = ecs_get_pair(
        world, EcsWorld, EcsSystemStats, EcsPeriod1s);
    if (!stats) {
        goto done;
    }

    ecs_map_iter_t mit = ecs_map_iter(&stats->stats);
    while (ecs_map_next(&mit)) {
        ecs_system_stats_t *s = ecs_map_ptr(&mit);
        double ms = (double)s->time_spent.gauge.avg[s->query.t] * 1000.0;
        box_monitor_top(top, BOX_MONITOR_TOP,
            (ecs_entity_t)ecs_map_key(&mit), ms);
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
    if (ctx->elapsed < period) {
        goto done;
    }

    ctx->elapsed = 0;
    if (!ecs_lookup(world, "flecs.stats")) {
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
    box_stats_slowest(world, s->slowest);
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
        .ctx_free = box_stats_monitor_free
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "BoxStatsOnSet" }),
        .query.terms = {{ .id = ecs_id(EcsStats) }},
        .events = { EcsOnSet },
        .callback = box_stats_on_set
    });
}
