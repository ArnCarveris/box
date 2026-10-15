/**
 * @file Pacing.c
 * @brief Tests for the sleep a headless process inserts when it exceeds its CPU budget.
 */

#include <box.h>
#include <app.h>
#include <bake_test.h>
#include <stdlib.h>
#include <time.h>

static void Busy(ecs_iter_t *it) {
    (void)it;
    clock_t start = clock();
    while ((double)(clock() - start) < (double)CLOCKS_PER_SEC * 0.02) {
    }
}

void Pacing_sleep_over_budget(void) {
    // engine_core/app
    test_flt(box_app_pace(0.02, 0.02, 50), 0.02);
    test_flt(box_app_pace(0.03, 0.01, 100), 0.02);
}

void Pacing_no_sleep_within_budget(void) {
    // engine_core/app
    test_flt(box_app_pace(0.01, 0.02, 50), 0);
    test_flt(box_app_pace(0.02, 0.02, 100), 0);
}

void Pacing_budget_above_one_core(void) {
    // engine_core/app
    test_flt(box_app_pace(0.04, 0.02, 200), 0);
    test_flt(box_app_pace(0.06, 0.02, 200), 0.01);
}

void Pacing_sleeps_when_over_budget(void) {
    // test_fixes/pacing_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_system(world, {
        .phase = EcsOnUpdate,
        .run = Busy
    });

    ecs_singleton_set(world, App, {
        .headless = true, .frames = 6, .cpu_budget = 50, .unguarded = true });

    ecs_progress(world, 0);

    clock_t cpu_start = clock();
    ecs_time_t start;
    ecs_os_get_time(&start);
    for (int i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
    }

    double wall = ecs_time_measure(&start);
    double cpu = (double)(clock() - cpu_start) / (double)CLOCKS_PER_SEC;
    const AppStatus *status = ecs_singleton_get(world, AppStatus);
    test_assert(cpu >= 0.1);
    test_assert(cpu * 100.0 / 50.0 <= wall * 1.02);
    test_assert(status->slept <= cpu * 100.0 / 50.0);

    ecs_fini(world);
}

void Pacing_unpaced_never_sleeps(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_system(world, {
        .phase = EcsOnUpdate,
        .run = Busy
    });

    ecs_singleton_set(world, App, {
        .headless = true, .frames = 6, .cpu_budget = 50, .unguarded = true,
        .unpaced = true });

    for (int i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
    }

    test_flt(ecs_singleton_get(world, AppStatus)->slept, 0);

    ecs_fini(world);
}

void Pacing_not_headless_never_sleeps(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_system(world, {
        .phase = EcsOnUpdate,
        .run = Busy
    });

    ecs_singleton_set(world, App, { .frames = 6, .cpu_budget = 50 });

    for (int i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
    }

    test_flt(ecs_singleton_get(world, AppStatus)->slept, 0);

    ecs_fini(world);
}

void Pacing_env_sets_unpaced(void) {
    // engine_core/app
    setenv("BOX_TEST", "1", 1);
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_bool(ecs_singleton_get(world, App)->unpaced, true);

    ecs_fini(world);

    setenv("BOX_TEST", "0", 1);
    world = ecs_init();
    ECS_IMPORT(world, Box);

    test_bool(ecs_singleton_get(world, App)->unpaced, false);

    ecs_fini(world);
}
