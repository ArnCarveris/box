/**
 * @file Stop.c
 * @brief Tests for the frame limit, the timeout and the run loop.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include "../../../../src/posix.h"

static int32_t quit_after = 0;
static float last_delta = 0;

static void QuitAfter(ecs_iter_t *it) {
    const AppStatus *status = ecs_singleton_get(it->world, AppStatus);
    if (status->frame + 1 >= quit_after) {
        ecs_quit(it->world);
    }
}

static void RecordDelta(ecs_iter_t *it) {
    last_delta = it->delta_time;
}

void Stop_frames(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, App, { .frames = 3, .cpu_budget = 100 });

    int32_t count = 0;
    while (ecs_progress(world, 0) && count < 100) {
        count ++;
    }

    test_int(count, 2);
    test_bool(ecs_should_quit(world), true);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 3);

    ecs_fini(world);
}

void Stop_no_limit(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    for (int i = 0; i < 10; i ++) {
        test_bool(ecs_progress(world, 0), true);
    }

    test_bool(ecs_should_quit(world), false);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 10);

    ecs_fini(world);
}

void Stop_timeout_infinite(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, App, { .timeout = -1, .cpu_budget = 100 });

    for (int i = 0; i < 10; i ++) {
        test_bool(ecs_progress(world, 0), true);
    }

    test_bool(ecs_should_quit(world), false);

    ecs_fini(world);
}

void Stop_run_headless_frames(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, App, {
        .headless = true, .frames = 5, .cpu_budget = 100, .fps = 0,
        .unguarded = true, .unpaced = true });

    test_int(box_app_run(world), 0);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 5);
    test_bool(ecs_singleton_get(world, AppStatus)->running, false);

    ecs_fini(world);
}

void Stop_run_headless_requires_limit(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, App, {
        .headless = true, .cpu_budget = 100, .unguarded = true, .unpaced = true });

    ecs_log_set_level(-4);
    test_assert(box_app_run(world) != 0);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 0);

    ecs_fini(world);
}

void Stop_run_headless_timeout_infinite(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    quit_after = 3;
    ecs_system(world, {
        .phase = EcsOnUpdate,
        .run = QuitAfter
    });

    ecs_singleton_set(world, App, {
        .headless = true, .timeout = -1, .cpu_budget = 100, .fps = 0,
        .unguarded = true, .unpaced = true });

    test_int(box_app_run(world), 0);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 3);

    ecs_fini(world);
}

void Stop_run_headless_fixed_delta(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_system(world, {
        .phase = EcsOnUpdate,
        .run = RecordDelta
    });

    ecs_singleton_set(world, App, {
        .headless = true, .frames = 3, .cpu_budget = 100, .delta_time = 0.02f,
        .unguarded = true, .unpaced = true });

    test_int(box_app_run(world), 0);
    test_flt(last_delta, 0.02f);

    ecs_fini(world);
}

void Stop_run_loads_scene(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *path = flecs_asprintf("/tmp/box_core_scene_%d.flecs", (int)getpid());
    FILE *f = fopen(path, "w");
    test_assert(f != NULL);
    fprintf(f, "scene_root {}\n");
    fclose(f);

    ecs_singleton_set(world, App, {
        .headless = true, .frames = 1, .cpu_budget = 100, .fps = 0,
        .unguarded = true, .unpaced = true, .scene = path });

    test_int(box_app_run(world), 0);
    test_assert(ecs_lookup(world, "scene_root") != 0);

    unlink(path);
    ecs_os_free(path);
    ecs_fini(world);
}
