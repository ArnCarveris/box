/**
 * @file Guard.c
 * @brief Tests for the oversubscription guard of a headless start.
 */

#include <box.h>
#include <app.h>
#include <bake_test.h>
#include <unistd.h>

void Guard_load_above_cores(void) {
    // engine_core/app
    test_bool(box_app_oversubscribed(12.5, 12, 100), true);
    test_bool(box_app_oversubscribed(2.0, 1, 100), true);
}

void Guard_load_at_cores(void) {
    // engine_core/app
    test_bool(box_app_oversubscribed(11.0, 12, 100), false);
    test_bool(box_app_oversubscribed(0, 1, 100), false);
}

void Guard_load_below_cores(void) {
    // engine_core/app
    test_bool(box_app_oversubscribed(0.5, 12, 100), false);
    test_bool(box_app_oversubscribed(0, 2, 100), false);
}

void Guard_budget_adds_to_load(void) {
    // core_fixes/cpu_guard_budget
    test_bool(box_app_oversubscribed(11.5, 12, 100), true);
    test_bool(box_app_oversubscribed(11.5, 12, 50), false);
    test_bool(box_app_oversubscribed(11.6, 12, 50), true);
    test_bool(box_app_oversubscribed(9.5, 12, 200), false);
    test_bool(box_app_oversubscribed(10.5, 12, 200), true);
    test_bool(box_app_oversubscribed(0, 4, 400), false);
    test_bool(box_app_oversubscribed(0, 4, 500), true);
}

void Guard_no_limit_claims_one_core(void) {
    // core_fixes/cpu_guard_budget
    test_bool(box_app_oversubscribed(11.0, 12, 0), false);
    test_bool(box_app_oversubscribed(11.5, 12, 0), true);
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

void Guard_run_refuses_budget_above_cores(void) {
    // core_fixes/cpu_guard_budget
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    float cores = (float)sysconf(_SC_NPROCESSORS_ONLN);
    ecs_singleton_set(world, App, {
        .headless = true, .frames = 2, .cpu_budget = 100 * (cores + 1),
        .unguarded = false, .unpaced = true });

    ecs_log_set_level(-4);
    test_assert(box_app_run(world) != 0);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 0);

    ecs_fini(world);
}

void Guard_run_unguarded_ignores_budget(void) {
    // core_fixes/cpu_guard_budget
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    float cores = (float)sysconf(_SC_NPROCESSORS_ONLN);
    ecs_singleton_set(world, App, {
        .headless = true, .frames = 2, .cpu_budget = 100 * (cores + 1),
        .unguarded = true, .unpaced = true });

    test_int(box_app_run(world), 0);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 2);

    ecs_fini(world);
}
