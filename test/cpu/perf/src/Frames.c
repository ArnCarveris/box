/**
 * @file Frames.c
 * @brief Tests for the frame rate cap and the frame time checks of a performance run.
 */

#include <box.h>
#include <bake_test.h>
#include "fake.h"
#include <math.h>

void Frames_trivial_passes(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_system(world, { .entity = ecs_entity(world, { .name = "FakeWork" }),
        .phase = EcsOnUpdate, .run = fake_work });
    fake_work_ms = 2;
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
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

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Frames_cap_paces_frames(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_singleton_set(world, PerfRun, { .fps = 30, .warmup = 2, .frames = 10 });

    double start = fake_time;
    for (int i = 0; i < 13; i ++) {
        ecs_progress(world, 0);
    }

    double wall = fake_time - start;
    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_assert(status->min_ms > status->target_ms * 0.999);
    test_int(status->fast, 0);
    test_assert(wall >= 11.0 / 30.0);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Frames_slow_frames_fail(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

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

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Frames_warmup_not_measured(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

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
    box_perf_clock = NULL;
    fake_os_end();
}

void Frames_invalid_fps(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;

    ecs_log_set_level(-4);
    ecs_singleton_set(world, PerfRun, { .fps = 50, .frames = 2 });
    for (int i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfInvalid);
    test_int(status->frame, 0);
    test_bool(status->passed, false);
    test_int(ecs_singleton_get(world, App)->fps, 0);

    ecs_fini(world);
    box_perf_clock = NULL;
}

void Frames_sets_app_fps(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_singleton_set(world, PerfRun, { .fps = 120, .frames = 2 });
    test_int(ecs_singleton_get(world, App)->fps, 120);

    ecs_singleton_set(world, PerfRun, { .fps = 30, .frames = 2 });
    test_int(ecs_singleton_get(world, App)->fps, 30);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Frames_sets_target_fps(void) {
    // engine/builtin_frame_pacing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_singleton_set(world, PerfRun, { .fps = 90, .warmup = 2, .frames = 3 });
    test_flt(ecs_get_world_info(world)->target_fps, 90);

    ecs_singleton_set(world, PerfRun, { .fps = 30, .warmup = 2, .frames = 3 });
    test_flt(ecs_get_world_info(world)->target_fps, 30);

    double start = fake_time;
    for (int i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
    }

    test_int(ecs_singleton_get(world, PerfStatus)->state, PerfDone);
    test_assert(fake_time - start > 5.0 / 30.0 * 0.999);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Frames_restart_on_set(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

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
    box_perf_clock = NULL;
    fake_os_end();
}

void Frames_quit_when_done(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_singleton_set(world, PerfRun, {
        .fps = 120, .warmup = 2, .frames = 3, .quit = true });
    int32_t frames = 0;
    while (ecs_progress(world, 0) && frames < 100) {
        frames ++;
    }

    test_int(frames, 4);
    test_int(ecs_singleton_get(world, PerfStatus)->state, PerfDone);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Frames_budget_sleep_reported(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 2;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_system(world, { .entity = ecs_entity(world, { .name = "FakeWork" }),
        .phase = EcsOnUpdate, .run = fake_work });
    ecs_singleton_set(world, PerfRun, {
        .fps = 30, .warmup = 2, .frames = 6 });
    for (int i = 0; i < 9; i ++) {
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_assert(status->slept_ms > 11.9 && status->slept_ms < 12.1);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Frames_only_slow_frames_counted(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_system(world, { .entity = ecs_entity(world, { .name = "FakeWork" }),
        .phase = EcsOnUpdate, .run = fake_work });
    ecs_singleton_set(world, PerfRun, { .fps = 60, .warmup = 2, .frames = 10 });
    fake_work_ms = 1;
    ecs_progress(world, 0);
    for (int i = 0; i < 10; i ++) {
        fake_work_ms = (i % 4 == 0) ? 25 : 17.5;
        ecs_progress(world, 0);
    }

    fake_work_ms = 1;
    ecs_progress(world, 0);

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_int(status->frame, 10);
    test_int(status->slow, 3);
    test_int(status->fast, 0);
    test_int(status->errors, 3);
    test_bool(status->passed, false);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Frames_tolerance_edges(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_system(world, { .entity = ecs_entity(world, { .name = "FakeWork" }),
        .phase = EcsOnUpdate, .run = fake_work });
    fake_work_ms = 18.2;
    ecs_singleton_set(world, PerfRun, { .fps = 60, .warmup = 2, .frames = 5 });
    for (int i = 0; i < 7; i ++) {
        ecs_progress(world, 0);
    }

    test_int(ecs_singleton_get(world, PerfStatus)->errors, 0);
    test_bool(ecs_singleton_get(world, PerfStatus)->passed, true);

    fake_work_ms = 18.6;
    ecs_singleton_set(world, PerfRun, { .fps = 60, .warmup = 2, .frames = 5 });
    for (int i = 0; i < 7; i ++) {
        ecs_progress(world, 0);
    }

    test_int(ecs_singleton_get(world, PerfStatus)->slow, 5);
    test_bool(ecs_singleton_get(world, PerfStatus)->passed, false);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Frames_statistics(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    box_perf_clock = fake_now;
    fake_os_begin();

    ecs_system(world, { .entity = ecs_entity(world, { .name = "FakeWork" }),
        .phase = EcsOnUpdate, .run = fake_work });
    ecs_singleton_set(world, PerfRun, { .fps = 30, .warmup = 2, .frames = 4 });
    fake_work_ms = 1;
    ecs_progress(world, 0);
    for (int i = 0; i < 4; i ++) {
        fake_work_ms = 40 + 10 * i;
        ecs_progress(world, 0);
    }

    fake_work_ms = 1;
    ecs_progress(world, 0);

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_assert(status->min_ms > 39.9 && status->min_ms < 40.1);
    test_assert(status->max_ms > 69.9 && status->max_ms < 70.1);
    test_assert(status->mean_ms > 54.9 && status->mean_ms < 55.1);
    test_assert(status->p50_ms >= 49.9 && status->p50_ms <= 60.1);
    test_assert(status->p99_ms > 69.9 && status->p99_ms < 70.1);
    test_int(status->slow, 4);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Frames_delta_matches_wall_time(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 2;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();
    ecs_system(world, { .entity = ecs_entity(world, { .name = "FakeWork" }),
        .phase = EcsOnUpdate, .run = fake_work });
    ecs_singleton_set(world, PerfRun, { .fps = 60, .warmup = 2, .frames = 20 });
    for (int i = 0; i < 22; i ++) {
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_int(status->checks.delta_errors, 0);
    test_assert(status->checks.delta_max_ms < 0.01);
    test_assert(status->checks.delta_tolerance_ms == BOX_FRAME_TOLERANCE_MS);
    test_int(status->errors, 0);
    test_bool(status->passed, true);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Frames_delta_mismatch_fails(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 2;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();
    ecs_system(world, { .entity = ecs_entity(world, { .name = "FakeWork" }),
        .phase = EcsOnUpdate, .run = fake_work });
    ecs_singleton_set(world, PerfRun, { .fps = 60, .warmup = 2, .frames = 20 });
    for (int i = 0; i < 22; i ++) {
        ecs_progress(world, 1.0f / 30.0f);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_int(status->slow, 0);
    test_int(status->fast, 0);
    test_int(status->checks.delta_errors, 20);
    test_int(status->errors, 20);
    test_assert(status->checks.delta_max_ms > 16.6 && status->checks.delta_max_ms < 16.8);
    test_bool(status->passed, false);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}

void Frames_delta_tolerance_edges(void) {
    // testing/town_stutter
    float off[] = { 0.0009f, 0.0011f, -0.0009f, -0.0011f };
    int32_t errors[] = { 0, 10, 0, 10 };
    for (int c = 0; c < 4; c ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        fake_time = 100;
        fake_work_ms = 2;
        fake_slept_ms = 0;
        box_perf_clock = fake_now;
        fake_os_begin();
        ecs_system(world, { .entity = ecs_entity(world, { .name = "FakeWork" }),
            .phase = EcsOnUpdate, .run = fake_work });
        ecs_singleton_set(world, PerfRun, { .fps = 60, .warmup = 2, .frames = 10 });
        for (int i = 0; i < 12; i ++) {
            ecs_progress(world, 1.0f / 60.0f + off[c]);
        }

        const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
        test_int(status->state, PerfDone);
        test_int(status->checks.delta_errors, errors[c]);
        test_bool(status->passed, errors[c] == 0);

        ecs_fini(world);
            box_perf_clock = NULL;
        fake_os_end();
    }
}

void Frames_fly_moves_camera(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 2;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    fake_os_begin();
    ecs_system(world, { .entity = ecs_entity(world, { .name = "FakeWork" }),
        .phase = EcsOnUpdate, .run = fake_work });
    ecs_entity_t box = ecs_new(world);
    ecs_set(world, box, Box, { .x = 1, .y = 1, .z = 1 });
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, { 0, 2, 50 });
    ecs_set(world, camera, Rotation, { 0, 0, 0 });
    ecs_set(world, camera, Camera, { .fov = 60, .near_ = 0.1f });
    ecs_add(world, camera, CameraController);
    ecs_singleton_set(world, PerfRun, {
        .fps = 60, .warmup = 2, .frames = 120, .fly = true, .camera = camera });
    for (int i = 0; i < 122; i ++) {
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    const Position *p = ecs_get(world, camera, Position);
    test_int(status->state, PerfDone);
    test_bool(status->checks.fly, true);
    test_assert(status->checks.travelled > 5);
    test_assert(status->revolutions == 0);
    test_assert(p->z < 50 || p->x != 0);
    test_assert(p->y > 1.99f && p->y < 2.01f);
    test_int(status->checks.delta_errors, 0);
    test_bool(status->passed, true);
    ecs_progress(world, 0);
    test_int(box_input_pending(world), 0);

    ecs_fini(world);
    box_perf_clock = NULL;
    fake_os_end();
}
