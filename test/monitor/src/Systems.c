/**
 * @file Systems.c
 * @brief Tests for the SystemMonitor singleton and its per system attribution.
 */

#include <box.h>
#include <bake_test.h>

static void BoxSlowSystem(
    ecs_iter_t *it)
{
    (void)it;
    ecs_time_t start;
    ecs_os_get_time(&start);
    volatile double sink = 0;
    double elapsed = 0;
    while (elapsed < 0.005) {
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

static void BoxLateSystem(
    ecs_iter_t *it)
{
    (void)it;
    ecs_time_t start;
    ecs_os_get_time(&start);
    volatile double sink = 0;
    double elapsed = 0;
    while (elapsed < 0.002) {
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

void Systems_off_by_default(void)
{
    // monitoring/system_monitor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxMonitor);
    ecs_singleton_add(world, SystemMonitor);
    const SystemMonitor *m = ecs_singleton_get(world, SystemMonitor);
    test_bool(m->enabled, false);
    ecs_progress(world, 0.01f);
    ecs_progress(world, 0.01f);
    test_flt(m->frame_ms, 0.0);
    test_int(m->system_count, 0);
    ecs_fini(world);
}

void Systems_disabled_monitor_collects_nothing(void)
{
    // monitoring/system_monitor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxMonitor);
    ecs_singleton_set(world, SystemMonitor, {.enabled = false});
    ECS_SYSTEM(world, BoxSlowSystem, EcsOnUpdate, 0);
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0.01f);
    }

    const SystemMonitor *m = ecs_singleton_get(world, SystemMonitor);
    test_flt(m->frame_ms, 0.0);
    test_uint(m->slowest[0].system, 0);
    ecs_fini(world);
}

void Systems_slow_system_is_attributed_to_itself(void)
{
    // monitoring/system_monitor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxMonitor);
    ecs_singleton_set(world, SystemMonitor, {.enabled = true});
    ECS_SYSTEM(world, BoxSlowSystem, EcsOnUpdate, 0);
    int32_t i;
    for (i = 0; i < 6; i ++) {
        ecs_progress(world, 0.01f);
    }

    const SystemMonitor *m = ecs_singleton_get(world, SystemMonitor);
    test_uint(m->slowest[0].system, BoxSlowSystem);
    test_assert(m->slowest[0].time_ms > 3.0);
    test_assert(m->slowest[0].time_ms < 25.0);
    ecs_fini(world);
}

void Systems_frame_total_covers_the_systems(void)
{
    // monitoring/system_monitor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxMonitor);
    ecs_singleton_set(world, SystemMonitor, {.enabled = true});
    ECS_SYSTEM(world, BoxSlowSystem, EcsOnUpdate, 0);
    ECS_SYSTEM(world, BoxLateSystem, EcsPostUpdate, 0);
    int32_t i;
    for (i = 0; i < 6; i ++) {
        ecs_progress(world, 0.01f);
    }

    const SystemMonitor *m = ecs_singleton_get(world, SystemMonitor);
    test_assert(m->system_count >= 2);
    test_assert(m->frame_ms >= m->slowest[0].time_ms);
    test_assert(m->frame_ms > 6.0);
    ecs_fini(world);
}

void Systems_phase_totals_are_kept_per_phase(void)
{
    // monitoring/system_monitor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxMonitor);
    ecs_singleton_set(world, SystemMonitor, {.enabled = true});
    ECS_SYSTEM(world, BoxSlowSystem, EcsOnUpdate, 0);
    ECS_SYSTEM(world, BoxLateSystem, EcsPostUpdate, 0);
    int32_t i;
    for (i = 0; i < 6; i ++) {
        ecs_progress(world, 0.01f);
    }

    const SystemMonitor *m = ecs_singleton_get(world, SystemMonitor);
    double on_update = 0, post_update = 0;
    for (i = 0; i < 8; i ++) {
        if (m->phases[i].phase == EcsOnUpdate) {
            on_update = m->phases[i].time_ms;
        }

        if (m->phases[i].phase == EcsPostUpdate) {
            post_update = m->phases[i].time_ms;
        }
    }

    test_assert(on_update > 3.0);
    test_assert(post_update > 1.0);
    test_assert(on_update > post_update);
    ecs_fini(world);
}

void Systems_disabling_stops_collection(void)
{
    // monitoring/system_monitor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxMonitor);
    ecs_singleton_set(world, SystemMonitor, {.enabled = true});
    ECS_SYSTEM(world, BoxSlowSystem, EcsOnUpdate, 0);
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0.01f);
    }

    test_assert(ecs_singleton_get(world, SystemMonitor)->frame_ms > 3.0);
    ecs_singleton_set(world, SystemMonitor, {.enabled = false});
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0.01f);
    }

    const SystemMonitor *m = ecs_singleton_get(world, SystemMonitor);
    test_flt(m->frame_ms, 0.0);
    test_uint(m->slowest[0].system, 0);
    ecs_fini(world);
}

void Systems_removing_the_monitor_stops_measurement(void)
{
    // monitoring/system_monitor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxMonitor);
    ecs_singleton_set(world, SystemMonitor, {.enabled = true});
    ECS_SYSTEM(world, BoxSlowSystem, EcsOnUpdate, 0);
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0.01f);
    }

    test_assert(ecs_system_get(world, BoxSlowSystem)->time_spent > 0);
    ecs_singleton_remove(world, SystemMonitor);
    double measured = (double)ecs_system_get(world, BoxSlowSystem)->time_spent;
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0.01f);
    }

    test_flt((double)ecs_system_get(world, BoxSlowSystem)->time_spent, measured);
    ecs_fini(world);
}
