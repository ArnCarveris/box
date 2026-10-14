/**
 * @file Frames.c
 * @brief Tests for the frame pacing and the frame time checks of a performance run.
 */

#include <box.h>
#include <bake_test.h>
#include "fake.h"
#include <math.h>
#include <sys/resource.h>
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#endif

void Frames_trivial_passes(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;

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
    test_assert(status->work_mean_ms > 1.9 && status->work_mean_ms < 2.1);
    test_assert(status->work_max_ms >= status->work_p99_ms);

    ecs_fini(world);
    box_perf_clock = NULL;
    box_perf_sleep = NULL;
}

void Frames_early_frames_sleep(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;

    ecs_singleton_set(world, PerfRun, { .fps = 30, .warmup = 2, .frames = 10 });

    double start = fake_time;
    for (int i = 0; i < 13; i ++) {
        ecs_progress(world, 0);
    }

    double wall = fake_time - start;
    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_assert(status->min_ms >= status->target_ms);
    test_int(status->fast, 0);
    test_assert(wall >= 11.0 / 30.0);

    ecs_fini(world);
    box_perf_clock = NULL;
    box_perf_sleep = NULL;
}

void Frames_slow_frames_fail(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;

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
    box_perf_clock = NULL;
    box_perf_sleep = NULL;
}

void Frames_warmup_not_measured(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;

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
    box_perf_sleep = NULL;
}

void Frames_invalid_fps(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;

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
    box_perf_clock = NULL;
    box_perf_sleep = NULL;
}

void Frames_sets_app_fps(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;

    ecs_singleton_set(world, PerfRun, { .fps = 120, .frames = 2 });
    test_int(ecs_singleton_get(world, App)->fps, 120);

    ecs_singleton_set(world, PerfRun, { .fps = 30, .frames = 2 });
    test_int(ecs_singleton_get(world, App)->fps, 30);

    ecs_fini(world);
    box_perf_clock = NULL;
    box_perf_sleep = NULL;
}

void Frames_restart_on_set(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;

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
    box_perf_sleep = NULL;
}

void Frames_quit_when_done(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;

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
    box_perf_sleep = NULL;
}

void Frames_budget_sleep_reported(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 2;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;

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
    box_perf_sleep = NULL;
}

void Frames_only_slow_frames_counted(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;

    ecs_system(world, { .entity = ecs_entity(world, { .name = "FakeWork" }),
        .phase = EcsOnUpdate, .run = fake_work });
    ecs_singleton_set(world, PerfRun, { .fps = 60, .warmup = 2, .frames = 10 });
    fake_work_ms = 1;
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    for (int i = 0; i < 10; i ++) {
        fake_work_ms = (i % 4 == 0) ? 25 : 17.5;
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_int(status->frame, 10);
    test_int(status->slow, 3);
    test_int(status->fast, 0);
    test_int(status->errors, 3);
    test_bool(status->passed, false);

    ecs_fini(world);
    box_perf_clock = NULL;
    box_perf_sleep = NULL;
}

void Frames_tolerance_edges(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;

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
    box_perf_sleep = NULL;
}

void Frames_statistics(void) {
    // testing/perf_flake
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;

    ecs_system(world, { .entity = ecs_entity(world, { .name = "FakeWork" }),
        .phase = EcsOnUpdate, .run = fake_work });
    ecs_singleton_set(world, PerfRun, { .fps = 30, .warmup = 2, .frames = 4 });
    fake_work_ms = 1;
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    for (int i = 0; i < 4; i ++) {
        fake_work_ms = 40 + 10 * i;
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_assert(status->min_ms > 39.9 && status->min_ms < 40.1);
    test_assert(status->max_ms > 69.9 && status->max_ms < 70.1);
    test_assert(status->mean_ms > 54.9 && status->mean_ms < 55.1);
    test_assert(status->p50_ms >= 49.9 && status->p50_ms <= 60.1);
    test_assert(status->p99_ms > 69.9 && status->p99_ms < 70.1);
    test_assert(status->work_mean_ms > 54.9 && status->work_mean_ms < 55.1);
    test_assert(status->work_max_ms > 69.9 && status->work_max_ms < 70.1);
    test_int(status->slow, 4);

    ecs_fini(world);
    box_perf_clock = NULL;
    box_perf_sleep = NULL;
}

void Frames_late_wakeups_absorbed(void) {
    // testing/perf_orbit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep_late;

    ecs_system(world, { .entity = ecs_entity(world, { .name = "FakeWork" }),
        .phase = EcsOnUpdate, .run = fake_work });
    ecs_singleton_set(world, PerfRun, { .fps = 120, .warmup = 2, .frames = 60 });
    for (int i = 0; i < 62; i ++) {
        fake_work_ms = (i % 7) * 0.9;
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_int(status->frame, 60);
    test_int(status->errors, 0);
    test_bool(status->passed, true);
    test_assert(status->min_ms >= status->target_ms);
    test_assert(status->max_ms < status->target_ms + 0.1);

    ecs_fini(world);
    box_perf_clock = NULL;
    box_perf_sleep = NULL;
}

void Frames_realtime_during_run(void) {
    // testing/pacer_priority
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;

    ecs_system(world, { .entity = ecs_entity(world, { .name = "FakeWork" }),
        .phase = EcsOnUpdate, .run = fake_work });
    fake_work_ms = 3;
    ecs_singleton_set(world, PerfRun, { .fps = 120, .warmup = 4, .frames = 10 });
#ifdef __APPLE__
    mach_port_t thread = pthread_mach_thread_np(pthread_self());
    thread_time_constraint_policy_data_t policy;
    mach_msg_type_number_t count;
    boolean_t standard;
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    double interval = 1e9 / 120.0 * (double)tb.denom / (double)tb.numer;
    double work = 3e6 * (double)tb.denom / (double)tb.numer;

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    count = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
    standard = FALSE;
    thread_policy_get(thread, THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&policy,
        &count, &standard);
    test_bool(standard, false);
    test_assert(fabs((double)policy.period - interval) < interval * 0.01);
    test_assert(fabs((double)policy.constraint - interval) < interval * 0.01);
    test_assert(policy.computation <= policy.constraint);

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, PerfStatus)->state, PerfMeasure);
    count = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
    standard = FALSE;
    thread_policy_get(thread, THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&policy,
        &count, &standard);
    test_bool(standard, false);
    test_assert((double)policy.computation >= work);
    test_assert(policy.computation <= policy.constraint);

    for (int i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
    }

    test_int(ecs_singleton_get(world, PerfStatus)->state, PerfDone);
    count = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
    standard = FALSE;
    thread_policy_get(thread, THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&policy,
        &count, &standard);
    test_bool(standard, true);
#endif

    ecs_fini(world);
    box_perf_clock = NULL;
    box_perf_sleep = NULL;
}

void Frames_no_realtime_without_run(void) {
    // testing/pacer_priority
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;
#ifdef __APPLE__
    mach_port_t thread = pthread_mach_thread_np(pthread_self());
    thread_time_constraint_policy_data_t policy;
    mach_msg_type_number_t count;
    boolean_t standard;

    for (int i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
    }

    count = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
    standard = FALSE;
    thread_policy_get(thread, THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&policy,
        &count, &standard);
    test_bool(standard, true);

    ecs_singleton_set(world, PerfRun, { .fps = 50, .warmup = 2, .frames = 10 });
    for (int i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
    }

    test_int(ecs_singleton_get(world, PerfStatus)->state, PerfInvalid);
    count = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
    standard = FALSE;
    thread_policy_get(thread, THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&policy,
        &count, &standard);
    test_bool(standard, true);
#endif

    ecs_fini(world);
    box_perf_clock = NULL;
    box_perf_sleep = NULL;
}

void Frames_realtime_reset_on_remove(void) {
    // testing/pacer_priority
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;
    ecs_singleton_set(world, PerfRun, { .fps = 60, .warmup = 2, .frames = 100 });
#ifdef __APPLE__
    mach_port_t thread = pthread_mach_thread_np(pthread_self());
    thread_time_constraint_policy_data_t policy;
    mach_msg_type_number_t count;
    boolean_t standard;

    for (int i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
    }

    count = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
    standard = FALSE;
    thread_policy_get(thread, THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&policy,
        &count, &standard);
    test_bool(standard, false);

    ecs_singleton_remove(world, PerfRun);
    ecs_progress(world, 0);
    count = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
    standard = FALSE;
    thread_policy_get(thread, THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&policy,
        &count, &standard);
    test_bool(standard, true);
#endif

    ecs_fini(world);
    box_perf_clock = NULL;
    box_perf_sleep = NULL;
}

void Frames_realtime_reset_on_fini(void) {
    // testing/pacer_priority
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    fake_time = 100;
    fake_work_ms = 0;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;
    ecs_singleton_set(world, PerfRun, { .fps = 90, .warmup = 2, .frames = 100 });
#ifdef __APPLE__
    mach_port_t thread = pthread_mach_thread_np(pthread_self());
    thread_time_constraint_policy_data_t policy;
    mach_msg_type_number_t count;
    boolean_t standard;

    for (int i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
    }

    count = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
    standard = FALSE;
    thread_policy_get(thread, THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&policy,
        &count, &standard);
    test_bool(standard, false);

    ecs_fini(world);
    count = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
    standard = FALSE;
    thread_policy_get(thread, THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&policy,
        &count, &standard);
    test_bool(standard, true);
#else
    ecs_fini(world);
#endif

    box_perf_clock = NULL;
    box_perf_sleep = NULL;
}

void Frames_delta_matches_wall_time(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_os_api_now_t now = ecs_os_api.now_;
    ecs_os_api.now_ = fake_os_now;
    fake_time = 100;
    fake_work_ms = 2;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;
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
    test_bool(ecs_singleton_get(world, App)->realtime, false);

    ecs_fini(world);
    ecs_os_api.now_ = now;
    box_perf_clock = NULL;
    box_perf_sleep = NULL;
}

void Frames_delta_mismatch_fails(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_os_api_now_t now = ecs_os_api.now_;
    ecs_os_api.now_ = fake_os_now;
    fake_time = 100;
    fake_work_ms = 2;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;
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
    ecs_os_api.now_ = now;
    box_perf_clock = NULL;
    box_perf_sleep = NULL;
}

void Frames_delta_tolerance_edges(void) {
    // testing/town_stutter
    float off[] = { 0.0009f, 0.0011f, -0.0009f, -0.0011f };
    int32_t errors[] = { 0, 10, 0, 10 };
    for (int c = 0; c < 4; c ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        ecs_os_api_now_t now = ecs_os_api.now_;
        ecs_os_api.now_ = fake_os_now;
        fake_time = 100;
        fake_work_ms = 2;
        fake_slept_ms = 0;
        box_perf_clock = fake_now;
        box_perf_sleep = fake_sleep;
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
        ecs_os_api.now_ = now;
        box_perf_clock = NULL;
        box_perf_sleep = NULL;
    }
}

void Frames_pacer_does_not_spin(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, PerfRun, { .fps = 60, .warmup = 2, .frames = 30 });
    struct rusage before, after;
    ecs_time_t start;
    ecs_os_get_time(&start);
    getrusage(RUSAGE_SELF, &before);
    for (int i = 0; i < 32; i ++) {
        ecs_progress(world, 0);
    }

    double wall = ecs_time_measure(&start);
    getrusage(RUSAGE_SELF, &after);
    double cpu = (double)(after.ru_utime.tv_sec - before.ru_utime.tv_sec) +
        (double)(after.ru_utime.tv_usec - before.ru_utime.tv_usec) * 1e-6 +
        (double)(after.ru_stime.tv_sec - before.ru_stime.tv_sec) +
        (double)(after.ru_stime.tv_usec - before.ru_stime.tv_usec) * 1e-6;
    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_assert(wall > 0.45);
    test_assert(cpu < wall * 0.25);
    test_assert(status->checks.cpu_percent > 0 && status->checks.cpu_percent < 25);
    test_assert(status->checks.cpu_ms > 0 && status->checks.cpu_ms < 4);

    ecs_fini(world);
}

void Frames_display_paces_run(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_os_api_now_t now = ecs_os_api.now_;
    ecs_os_api_sleep_t sleep = ecs_os_api.sleep_;
    ecs_os_api.now_ = fake_os_now;
    ecs_os_api.sleep_ = fake_os_sleep;
    fake_time = 100;
    fake_work_ms = 3;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;
    ecs_system(world, { .entity = ecs_entity(world, { .name = "FakeWork" }),
        .phase = EcsOnUpdate, .run = fake_work });
    ecs_singleton_set(world, RenderSettings, { .in_flight = 2, .main_thread = true });
    ecs_singleton_set(world, PerfRun, {
        .fps = 60, .warmup = 4, .frames = 30, .refresh = 120 });
    for (int i = 0; i < 34; i ++) {
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    const RenderStatus *render = ecs_singleton_get(world, RenderStatus);
    test_int(status->state, PerfDone);
    test_int(render->swap_interval, 2);
    test_assert(status->checks.refresh == 120);
    test_int(status->checks.present_errors, 0);
    test_int(status->checks.display_delta_errors, 0);
    test_int(status->checks.delta_errors, 0);
    test_assert(status->checks.present_mean_ms > 16.6 && status->checks.present_mean_ms < 16.7);
    test_assert(status->min_ms > 16.6 && status->max_ms < 16.7);
    test_int(status->errors, 0);
    test_bool(status->passed, true);
    test_assert(ecs_singleton_get(world, RenderSettings)->display_rate == 0);

    ecs_fini(world);
    ecs_os_api.now_ = now;
    ecs_os_api.sleep_ = sleep;
    box_perf_clock = NULL;
    box_perf_sleep = NULL;
}

void Frames_uneven_display_fails(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_os_api_now_t now = ecs_os_api.now_;
    ecs_os_api_sleep_t sleep = ecs_os_api.sleep_;
    ecs_os_api.now_ = fake_os_now;
    ecs_os_api.sleep_ = fake_os_sleep;
    fake_time = 100;
    fake_work_ms = 3;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;
    ecs_system(world, { .entity = ecs_entity(world, { .name = "FakeWork" }),
        .phase = EcsOnUpdate, .run = fake_work });
    ecs_singleton_set(world, RenderSettings, { .in_flight = 2, .main_thread = true });
    ecs_singleton_set(world, PerfRun, {
        .fps = 90, .warmup = 4, .frames = 30, .refresh = 120 });
    for (int i = 0; i < 34; i ++) {
        ecs_progress(world, 0);
    }

    const PerfStatus *status = ecs_singleton_get(world, PerfStatus);
    test_int(status->state, PerfDone);
    test_assert(status->checks.present_errors > 0);
    test_assert(status->errors > 0);
    test_bool(status->passed, false);

    ecs_fini(world);
    ecs_os_api.now_ = now;
    ecs_os_api.sleep_ = sleep;
    box_perf_clock = NULL;
    box_perf_sleep = NULL;
}

void Frames_fly_moves_camera(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_os_api_now_t now = ecs_os_api.now_;
    ecs_os_api.now_ = fake_os_now;
    fake_time = 100;
    fake_work_ms = 2;
    fake_slept_ms = 0;
    box_perf_clock = fake_now;
    box_perf_sleep = fake_sleep;
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
    ecs_os_api.now_ = now;
    box_perf_clock = NULL;
    box_perf_sleep = NULL;
}
