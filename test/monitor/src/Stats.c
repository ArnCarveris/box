/**
 * @file Stats.c
 * @brief Tests for the EcsStats singleton that mirrors the flecs stats addon.
 */

#include <box.h>
#include <bake_test.h>

static void BoxStatsBurn(
    ecs_iter_t *it)
{
    (void)it;
    ecs_time_t start;
    ecs_os_get_time(&start);
    volatile double sink = 0;
    double elapsed = 0;
    while (elapsed < 0.004) {
        int32_t i;
        for (i = 0; i < 4096; i ++) {
            sink += (double)i;
        }

        ecs_time_t now;
        ecs_os_get_time(&now);
        elapsed = (double)(now.sec - start.sec) +
            ((double)now.nanosec - (double)start.nanosec) * 1e-9;
    }
}

void Stats_setting_the_component_imports_the_addon(void)
{
    // monitoring/ecs_stats
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_lookup(world, "flecs.stats") == 0);
    ecs_singleton_set(world, EcsStats, {.period_s = 0.001});
    test_assert(ecs_lookup(world, "flecs.stats") != 0);
    const EcsStats *s = ecs_singleton_get(world, EcsStats);
    test_int(s->collections, 0);
    ecs_fini(world);
}

void Stats_collects_after_the_period(void)
{
    // monitoring/ecs_stats
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, EcsStats, {.period_s = 0.001});
    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0.01f);
    }

    const EcsStats *s = ecs_singleton_get(world, EcsStats);
    test_assert(s->collections >= 4);
    test_assert(s->entity_count > 0);
    test_assert(s->table_count > 0);
    test_assert(s->systems_ran > 0);
    test_assert(s->frame_time_ms >= 0.0);
    test_assert(s->merge_time_ms >= 0.0);
    ecs_fini(world);
}

void Stats_period_holds_back_the_collection(void)
{
    // monitoring/ecs_stats
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, EcsStats, {.period_s = 100.0});
    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0.01f);
    }

    const EcsStats *s = ecs_singleton_get(world, EcsStats);
    test_int(s->collections, 0);
    test_int(s->entity_count, 0);
    ecs_fini(world);
}

void Stats_entity_count_follows_the_world(void)
{
    // monitoring/ecs_stats
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, EcsStats, {.period_s = 0.001});
    ecs_progress(world, 0.01f);
    ecs_progress(world, 0.01f);
    int32_t before = ecs_singleton_get(world, EcsStats)->entity_count;
    int32_t i;
    for (i = 0; i < 500; i ++) {
        ecs_new(world);
    }

    ecs_progress(world, 0.01f);
    ecs_progress(world, 0.01f);
    int32_t after = ecs_singleton_get(world, EcsStats)->entity_count;
    test_assert(after >= before + 500);
    ecs_fini(world);
}

void Stats_frame_time_window_is_averaged(void)
{
    // monitoring/ecs_stats
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, EcsStats, {.period_s = 0.001, .history = 1});
    ECS_SYSTEM(world, BoxStatsBurn, EcsOnUpdate, 0);
    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0.01f);
    }

    const EcsStats *s = ecs_singleton_get(world, EcsStats);
    test_int(s->history, 1);
    test_assert(s->frame_time_ms > 0.0);
    test_flt(s->frame_time_avg_ms, s->frame_time_ms);
    ecs_fini(world);
}

void Stats_slowest_systems_are_listed(void)
{
    // monitoring/ecs_stats
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, EcsStats, {.period_s = 0.001});
    ECS_SYSTEM(world, BoxStatsBurn, EcsOnUpdate, 0);
    int32_t i;
    for (i = 0; i < 20; i ++) {
        ecs_progress(world, 0.01f);
    }

    const EcsStats *s = ecs_singleton_get(world, EcsStats);
    bool found = false;
    for (i = 0; i < 8; i ++) {
        if (s->slowest[i].system == BoxStatsBurn && s->slowest[i].time_ms > 0.0) {
            found = true;
        }
    }

    test_assert(found);
    test_assert(s->slowest[0].time_ms >= s->slowest[1].time_ms);
    ecs_fini(world);
}

void Stats_removing_the_component_stops_collection(void)
{
    // monitoring/ecs_stats
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, EcsStats, {.period_s = 0.001});
    ecs_progress(world, 0.01f);
    ecs_progress(world, 0.01f);
    test_assert(ecs_singleton_get(world, EcsStats)->collections >= 1);
    ecs_singleton_remove(world, EcsStats);
    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0.01f);
    }

    test_assert(ecs_singleton_get(world, EcsStats) == NULL);
    ecs_singleton_set(world, EcsStats, {.period_s = 100.0});
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0.01f);
    }

    const EcsStats *s = ecs_singleton_get(world, EcsStats);
    test_int(s->collections, 0);
    test_int(s->entity_count, 0);
    ecs_fini(world);
}

void Stats_slowest_covers_min_avg_max(void)
{
    // monitoring/stats_systems
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, EcsStats, {.period_s = 0.05});
    ECS_SYSTEM(world, BoxStatsBurn, EcsOnUpdate, 0);
    int32_t i;
    for (i = 0; i < 12; i ++) {
        ecs_progress(world, 0.01f);
    }

    const EcsStats *s = ecs_singleton_get(world, EcsStats);
    const SystemTime *burn = NULL;
    for (i = 0; i < BOX_MONITOR_TOP; i ++) {
        if (s->slowest[i].system == BoxStatsBurn) {
            burn = &s->slowest[i];
        }
    }

    test_assert(burn != NULL);
    test_assert(burn->max_ms > 0.0);
    test_assert(burn->min_ms <= burn->time_ms);
    test_assert(burn->time_ms <= burn->max_ms);
    test_assert(burn->min_ms > 0.0);
    ecs_fini(world);
}

static int32_t stats_sets;

static void BoxStatsCountSet(
    ecs_iter_t *it)
{
    (void)it;
    stats_sets ++;
}

void Stats_collection_marks_the_component_modified(void)
{
    // monitoring/stats_systems
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(EcsStats) }},
        .events = { EcsOnSet },
        .callback = BoxStatsCountSet
    });
    stats_sets = 0;
    ecs_singleton_set(world, EcsStats, {.period_s = 1.0});
    test_int(stats_sets, 1);
    ecs_progress(world, 0.5f);
    test_int(stats_sets, 1);
    ecs_progress(world, 0.5f);
    test_int(stats_sets, 2);
    ecs_progress(world, 0.5f);
    test_int(stats_sets, 2);
    ecs_progress(world, 0.5f);
    test_int(stats_sets, 3);
    ecs_fini(world);
}

void Stats_removing_the_component_disables_the_addon(void)
{
    // monitoring/stats_systems
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, EcsStats, {.period_s = 0.01});
    ECS_SYSTEM(world, BoxStatsBurn, EcsOnUpdate, 0);
    ecs_entity_t module = ecs_lookup(world, "flecs.stats");
    test_assert(module != 0);
    ecs_progress(world, 0.02f);
    ecs_progress(world, 0.02f);
    const EcsWorldSummary *summary = ecs_get(world, EcsWorld, EcsWorldSummary);
    test_assert(summary != NULL);
    int64_t frames = summary->frame_count;
    test_assert(frames > 0);
    test_assert(ecs_system_get(world, BoxStatsBurn)->time_spent > 0);

    ecs_singleton_remove(world, EcsStats);
    test_bool(ecs_has_id(world, module, EcsDisabled), true);
    ecs_ftime_t spent = ecs_system_get(world, BoxStatsBurn)->time_spent;
    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0.02f);
    }

    test_int(ecs_get(world, EcsWorld, EcsWorldSummary)->frame_count, frames);
    test_flt(ecs_system_get(world, BoxStatsBurn)->time_spent, spent);
    ecs_fini(world);
}

void Stats_setting_it_again_enables_the_addon(void)
{
    // monitoring/stats_systems
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, EcsStats, {.period_s = 0.01});
    ecs_entity_t module = ecs_lookup(world, "flecs.stats");
    ecs_progress(world, 0.02f);
    ecs_singleton_remove(world, EcsStats);
    test_bool(ecs_has_id(world, module, EcsDisabled), true);
    int64_t frames = ecs_get(world, EcsWorld, EcsWorldSummary)->frame_count;

    ecs_singleton_set(world, EcsStats, {.period_s = 0.01});
    test_bool(ecs_has_id(world, module, EcsDisabled), false);
    ecs_progress(world, 0.02f);
    ecs_progress(world, 0.02f);
    test_assert(ecs_get(world, EcsWorld, EcsWorldSummary)->frame_count > frames);
    test_assert(ecs_singleton_get(world, EcsStats)->collections >= 1);
    ecs_fini(world);
}

void Stats_addon_monitors_are_not_listed(void)
{
    // monitoring/stats_systems
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, EcsStats, {.period_s = 0.05});
    int32_t i;
    for (i = 0; i < 12; i ++) {
        ecs_progress(world, 0.01f);
    }

    const EcsStats *s = ecs_singleton_get(world, EcsStats);
    ecs_entity_t addon = ecs_lookup(world, "flecs.stats");
    test_assert(s->slowest[0].system != 0);
    for (i = 0; i < BOX_MONITOR_TOP; i ++) {
        ecs_entity_t parent = s->slowest[i].system;
        while (parent) {
            test_assert(parent != addon);
            parent = ecs_get_parent(world, parent);
        }
    }

    ecs_fini(world);
}
