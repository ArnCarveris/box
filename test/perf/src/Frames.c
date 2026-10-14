/**
 * @file Frames.c
 * @brief Tests for the frame pacing and the frame time checks of a performance run.
 */

#include <box.h>
#include <bake_test.h>

void Frames_trivial_passes(void) {
    // testing/perf
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_singleton_set(world, PerfRun, { .fps = 60, .warmup = 2, .frames = 20 });

    int32_t frames = 0;
    while (ecs_singleton_get(world, PerfStatus)->state != PerfDone && frames < 100) {
        ecs_progress(world, 0);
        frames ++;
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(frames, 22);
    test_int(status->state, PerfDone);
    test_int(status->frame, 20);
    test_int(status->errors, 0);
    test_int(status->slow, 0);
    test_int(status->fast, 0);
    test_bool(status->passed, true);
    test_assert(status->target_ms > 16.66 && status->target_ms < 16.67);
    test_assert(status->max_ms < status->target_ms * 1.1);
    test_assert(status->mean_ms >= status->min_ms && status->mean_ms <= status->max_ms);
    test_assert(status->p50_ms >= status->min_ms && status->p99_ms <= status->max_ms);
    test_assert(status->work_mean_ms < status->target_ms);
    test_assert(status->work_max_ms >= status->work_p99_ms);

    ecs_fini(world);
}

void Frames_early_frames_sleep(void) {
    // testing/perf
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, PerfRun, { .fps = 30, .warmup = 2, .frames = 10 });

    ecs_time_t start;
    ecs_os_get_time(&start);
    for (int i = 0; i < 13; i ++) {
        ecs_progress(world, 0);
    }

    double wall = ecs_time_measure(&start);
    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_assert(status->min_ms >= status->target_ms);
    test_int(status->fast, 0);
    test_assert(wall >= 11.0 / 30.0);

    ecs_fini(world);
}

void Frames_slow_frames_fail(void) {
    // testing/perf
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, PerfRun, {
        .fps = 60, .warmup = 2, .frames = 10, .stall = 25 });
    for (int i = 0; i < 13; i ++) {
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_int(status->frame, 10);
    test_int(status->slow, 10);
    test_int(status->fast, 0);
    test_int(status->errors, 10);
    test_bool(status->passed, false);
    test_assert(status->min_ms > 25);
    test_assert(status->work_mean_ms > 25);

    ecs_fini(world);
}

void Frames_warmup_not_measured(void) {
    // testing/perf
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, PerfRun, { .fps = 120, .warmup = 5, .frames = 4 });
    for (int i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
        const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
        test_int(status->state, i < 4 ? PerfWarmup : PerfMeasure);
        test_int(status->frame, 0);
    }

    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, PerfStatus)->frame, 1);

    ecs_fini(world);
}

void Frames_invalid_fps(void) {
    // testing/perf
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_log_set_level(-4);
    ecs_singleton_set(world, PerfRun, { .fps = 50, .frames = 2 });
    for (int i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfInvalid);
    test_int(status->frame, 0);
    test_bool(status->passed, false);
    test_int(ecs_singleton_get(world, App)->fps, 60);

    ecs_fini(world);
}

void Frames_sets_app_fps(void) {
    // testing/perf
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, PerfRun, { .fps = 120, .frames = 2 });
    test_int(ecs_singleton_get(world, App)->fps, 120);

    ecs_singleton_set(world, PerfRun, { .fps = 30, .frames = 2 });
    test_int(ecs_singleton_get(world, App)->fps, 30);

    ecs_fini(world);
}

void Frames_restart_on_set(void) {
    // testing/perf
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, PerfRun, { .fps = 120, .warmup = 2, .frames = 3 });
    for (int i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
    }

    test_int(ecs_singleton_get(world, PerfStatus)->state, PerfDone);
    test_int(ecs_singleton_get(world, PerfStatus)->frame, 3);

    ecs_singleton_set(world, PerfRun, { .fps = 120, .warmup = 2, .frames = 5 });
    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfWarmup);
    test_int(status->frame, 0);

    for (int i = 0; i < 8; i ++) {
        ecs_progress(world, 0);
    }

    status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_int(status->frame, 5);

    ecs_fini(world);
}

void Frames_quit_when_done(void) {
    // testing/perf
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, PerfRun, {
        .fps = 120, .warmup = 2, .frames = 3, .quit = true });
    int32_t frames = 0;
    while (ecs_progress(world, 0) && frames < 100) {
        frames ++;
    }

    test_int(frames, 4);
    test_int(ecs_singleton_get(world, PerfStatus)->state, PerfDone);

    ecs_fini(world);
}

void Frames_budget_sleep_reported(void) {
    // testing/perf
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, App, {
        .headless = true, .frames = 100, .cpu_budget = 10, .unguarded = true,
        .unpaced = false });
    ecs_singleton_set(world, PerfRun, {
        .fps = 30, .warmup = 2, .frames = 6, .stall = 8 });
    for (int i = 0; i < 9; i ++) {
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_assert(status->slept_ms > 0);

    ecs_fini(world);
}
