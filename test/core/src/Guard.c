/**
 * @file Guard.c
 * @brief Tests for the oversubscription guard of a headless start.
 */

#include <box.h>
#include <app.h>
#include <bake_test.h>
#include <stdlib.h>

void Guard_load_above_cores(void) {
    // engine_core/app
    test_bool(box_app_oversubscribed(12.5, 12), true);
    test_bool(box_app_oversubscribed(2.0, 1), true);
}

void Guard_load_at_cores(void) {
    // engine_core/app
    test_bool(box_app_oversubscribed(12.0, 12), false);
}

void Guard_load_below_cores(void) {
    // engine_core/app
    test_bool(box_app_oversubscribed(0.5, 12), false);
    test_bool(box_app_oversubscribed(0, 1), false);
}

void Guard_env_sets_unguarded(void) {
    // engine_core/app
    setenv("BOX_TEST", "1", 1);
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_bool(ecs_singleton_get(world, App)->unguarded, true);

    ecs_fini(world);
}

void Guard_env_zero_keeps_guard(void) {
    // engine_core/app
    setenv("BOX_TEST", "0", 1);
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_bool(ecs_singleton_get(world, App)->unguarded, false);

    ecs_fini(world);
}

void Guard_env_unset_keeps_guard(void) {
    // engine_core/app
    unsetenv("BOX_TEST");
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_bool(ecs_singleton_get(world, App)->unguarded, false);

    ecs_fini(world);
}

void Guard_run_unguarded(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, App, {
        .headless = true, .frames = 2, .cpu_budget = 100, .unguarded = true,
        .unpaced = true });

    test_int(box_app_run(world), 0);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 2);

    ecs_fini(world);
}
