/**
 * @file FrameClock.c
 * @brief Tests for the frame loop: delta time, the frame cap, vsync pacing and FrameTiming.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include <sys/resource.h>

static double fake_clock = 100;
static double fake_work = 0;
static int32_t fake_frame = 0;
static int32_t fake_slow_frame = -1;

static uint64_t FakeNow(void) {
    return (uint64_t)(fake_clock * 1e9);
}

static void FakeSleep(int32_t sec, int32_t nanosec) {
    fake_clock += (double)sec + (double)nanosec * 1e-9;
}

static void FakeWork(ecs_iter_t *it) {
    (void)it;
    fake_clock += fake_frame == fake_slow_frame ? 0.040 : fake_work;
    fake_frame ++;
}

void FrameClock_timing_measures_delta_and_interval(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_os_api_now_t now = ecs_os_api.now_;
    ecs_os_api.now_ = FakeNow;
    fake_clock = 100;
    fake_work = 0.010;
    fake_frame = 0;
    fake_slow_frame = -1;
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });

    for (int i = 0; i < 30; i ++) {
        ecs_progress(world, 0);
    }

    const FrameTiming *t = ecs_singleton_get(world, FrameTiming);
    test_int(t->frames, 29);
    test_assert(t->interval.min > 9.99 && t->interval.max < 10.01);
    test_assert(t->delta.min > 9.99 && t->delta.max < 10.01);
    test_assert(t->error.max < 0.01);
    test_int(t->late, 0);
    test_int(t->dropped, 0);
    test_assert(t->total == 29);
    test_bool(t->fixed, false);

    ecs_fini(world);
    ecs_os_api.now_ = now;
}

void FrameClock_late_delta_counted(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_os_api_now_t now = ecs_os_api.now_;
    ecs_os_api.now_ = FakeNow;
    fake_clock = 100;
    fake_work = 0.010;
    fake_frame = 0;
    fake_slow_frame = -1;
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });

    for (int i = 0; i < 11; i ++) {
        ecs_progress(world, i < 6 ? 0.0105f : 0.0115f);
    }

    const FrameTiming *t = ecs_singleton_get(world, FrameTiming);
    test_int(t->frames, 10);
    test_int(t->late, 5);
    test_int(t->total_late, 5);
    test_assert(t->error.max > 1.4 && t->error.max < 1.6);
    test_assert(t->delta_ms > 11.49 && t->delta_ms < 11.51);

    ecs_fini(world);
    ecs_os_api.now_ = now;
}

void FrameClock_dropped_frame_counted(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_os_api_now_t now = ecs_os_api.now_;
    ecs_os_api.now_ = FakeNow;
    fake_clock = 100;
    fake_work = 0.020;
    fake_frame = 0;
    fake_slow_frame = 5;
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });
    ecs_singleton_set(world, App, { .fps = 50 });

    for (int i = 0; i < 12; i ++) {
        ecs_progress(world, 0);
    }

    const FrameTiming *t = ecs_singleton_get(world, FrameTiming);
    test_int(t->dropped, 1);
    test_int(t->total_dropped, 1);
    test_assert(t->target_ms > 19.99 && t->target_ms < 20.01);
    test_assert(t->interval.max > 39.9);

    ecs_fini(world);
    ecs_os_api.now_ = now;
}

void FrameClock_headless_steps_fixed_delta(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_os_api_now_t now = ecs_os_api.now_;
    ecs_os_api_sleep_t sleep = ecs_os_api.sleep_;
    ecs_os_api.now_ = FakeNow;
    ecs_os_api.sleep_ = FakeSleep;
    fake_clock = 100;
    fake_work = 0.003;
    fake_frame = 0;
    fake_slow_frame = -1;
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });
    ecs_singleton_set(world, App, {
        .headless = true, .frames = 20, .fps = 50, .unguarded = true, .unpaced = true });

    test_int(box_app_run(world), 0);

    const FrameTiming *t = ecs_singleton_get(world, FrameTiming);
    test_bool(t->fixed, true);
    test_assert(t->delta.min > 19.999 && t->delta.max < 20.001);
    test_assert(t->interval.min > 19.99 && t->interval.max < 20.1);
    test_int(t->late, 0);

    ecs_fini(world);
    ecs_os_api.now_ = now;
    ecs_os_api.sleep_ = sleep;
}

void FrameClock_cap_sleeps_rest_of_frame(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_os_api_now_t now = ecs_os_api.now_;
    ecs_os_api_sleep_t sleep = ecs_os_api.sleep_;
    ecs_os_api.now_ = FakeNow;
    ecs_os_api.sleep_ = FakeSleep;
    fake_clock = 100;
    fake_work = 0.005;
    fake_frame = 0;
    fake_slow_frame = -1;
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });
    ecs_singleton_set(world, App, { .frames = 30, .fps = 50 });

    test_int(box_app_run(world), 0);

    const FrameTiming *t = ecs_singleton_get(world, FrameTiming);
    test_bool(t->fixed, false);
    test_assert(t->interval.mean > 19.9 && t->interval.mean < 20.1);
    test_assert(t->interval.max < 20.1);
    test_assert(t->error.max < BOX_FRAME_TOLERANCE_MS);
    test_int(t->late, 0);

    ecs_fini(world);
    ecs_os_api.now_ = now;
    ecs_os_api.sleep_ = sleep;
}

void FrameClock_vsync_paces_without_cap(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_os_api_now_t now = ecs_os_api.now_;
    ecs_os_api_sleep_t sleep = ecs_os_api.sleep_;
    ecs_os_api.now_ = FakeNow;
    ecs_os_api.sleep_ = FakeSleep;
    fake_clock = 100;
    fake_work = 0.003;
    fake_frame = 0;
    fake_slow_frame = -1;
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });
    ecs_singleton_set(world, RenderSettings, {
        .in_flight = 2, .main_thread = true, .display_rate = 120 });
    ecs_singleton_set(world, App, { .frames = 60, .fps = 60 });

    test_int(box_app_run(world), 0);

    const RenderStatus *r = ecs_singleton_get(world, RenderStatus);
    const FrameTiming *t = ecs_singleton_get(world, FrameTiming);
    test_bool(r->vsync, true);
    test_int(r->swap_interval, 2);
    test_assert(r->refresh_rate == 120);
    test_int(r->presents_late, 0);
    test_int(r->presents_early, 0);
    test_assert(r->present_ms > 16.66 && r->present_ms < 16.67);
    test_assert(t->present.min > 16.66 && t->present.max < 16.67);
    test_assert(t->interval_ms > 16.66 && t->interval_ms < 16.67);
    test_assert(t->interval.max < 16.67);
    test_assert(t->error.max < BOX_FRAME_TOLERANCE_MS);

    ecs_fini(world);
    ecs_os_api.now_ = now;
    ecs_os_api.sleep_ = sleep;
}

void FrameClock_vsync_delta_smoothed(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_os_api_now_t now = ecs_os_api.now_;
    ecs_os_api_sleep_t sleep = ecs_os_api.sleep_;
    ecs_os_api.now_ = FakeNow;
    ecs_os_api.sleep_ = FakeSleep;
    fake_clock = 100;
    fake_work = 0.002;
    fake_frame = 0;
    fake_slow_frame = -1;
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });
    ecs_singleton_set(world, RenderSettings, {
        .in_flight = 2, .main_thread = true, .display_rate = 120 });
    ecs_singleton_set(world, App, { .frames = 800, .fps = 60 });

    test_int(box_app_run(world), 0);

    const FrameTiming *t = ecs_singleton_get(world, FrameTiming);
    test_bool(t->smoothing, true);
    test_assert(t->refresh_rate == 60);
    test_assert(t->delta.min == t->delta.max);
    test_assert(t->delta.min > 16.665 && t->delta.min < 16.667);
    test_assert(t->error.max < 0.01);
    test_int(t->late, 0);

    ecs_fini(world);
    ecs_os_api.now_ = now;
    ecs_os_api.sleep_ = sleep;
}

void FrameClock_swap_interval_follows_cap(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_os_api_now_t now = ecs_os_api.now_;
    ecs_os_api_sleep_t sleep = ecs_os_api.sleep_;
    ecs_os_api.now_ = FakeNow;
    ecs_os_api.sleep_ = FakeSleep;
    fake_clock = 100;
    fake_work = 0.001;
    fake_frame = 0;
    fake_slow_frame = -1;
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });
    ecs_singleton_set(world, RenderSettings, {
        .in_flight = 2, .main_thread = true, .display_rate = 120 });
    float fps[] = { 30, 60, 120, 0, 90, 240 };
    int32_t expect[] = { 4, 2, 1, 1, 0, 1 };
    for (int i = 0; i < 6; i ++) {
        ecs_singleton_set(world, App, { .fps = fps[i] });
        ecs_progress(world, 0.01f);
        test_int(ecs_singleton_get(world, RenderStatus)->swap_interval, expect[i]);
    }

    ecs_singleton_set(world, RenderSettings, {
        .in_flight = 2, .main_thread = true, .display_rate = 90 });
    ecs_singleton_set(world, App, { .fps = 90 });
    ecs_progress(world, 0.01f);
    ecs_progress(world, 0.01f);
    test_int(ecs_singleton_get(world, RenderStatus)->swap_interval, 1);

    ecs_fini(world);
    ecs_os_api.now_ = now;
    ecs_os_api.sleep_ = sleep;
}

void FrameClock_uneven_cap_presents_uneven(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_os_api_now_t now = ecs_os_api.now_;
    ecs_os_api_sleep_t sleep = ecs_os_api.sleep_;
    ecs_os_api.now_ = FakeNow;
    ecs_os_api.sleep_ = FakeSleep;
    fake_clock = 100;
    fake_work = 0.002;
    fake_frame = 0;
    fake_slow_frame = -1;
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });
    ecs_singleton_set(world, RenderSettings, {
        .in_flight = 2, .main_thread = true, .display_rate = 120 });
    ecs_singleton_set(world, App, { .frames = 90, .fps = 90 });

    test_int(box_app_run(world), 0);

    const RenderStatus *r = ecs_singleton_get(world, RenderStatus);
    const FrameTiming *t = ecs_singleton_get(world, FrameTiming);
    test_int(r->swap_interval, 0);
    test_assert(t->interval.mean > 11.0 && t->interval.mean < 11.2);
    test_assert(r->presents_late > 0);
    test_assert(r->presents_early > 0);
    test_assert(t->present.min > 8.3 && t->present.min < 8.4);
    test_assert(t->present.max > 16.6 && t->present.max < 16.7);

    ecs_fini(world);
    ecs_os_api.now_ = now;
    ecs_os_api.sleep_ = sleep;
}

void FrameClock_cap_does_not_spin(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, App, { .frames = 30, .fps = 60 });
    ecs_time_t start;
    ecs_os_get_time(&start);
    struct rusage before, after;
    getrusage(RUSAGE_SELF, &before);

    test_int(box_app_run(world), 0);

    double wall = ecs_time_measure(&start);
    getrusage(RUSAGE_SELF, &after);
    double cpu = (double)(after.ru_utime.tv_sec - before.ru_utime.tv_sec) +
        (double)(after.ru_utime.tv_usec - before.ru_utime.tv_usec) * 1e-6 +
        (double)(after.ru_stime.tv_sec - before.ru_stime.tv_sec) +
        (double)(after.ru_stime.tv_usec - before.ru_stime.tv_usec) * 1e-6;
    test_assert(wall > 0.45);
    test_assert(cpu < wall * 0.25);

    ecs_fini(world);
}

void FrameClock_wait_does_not_spin(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ecs_time_t start;
    ecs_os_get_time(&start);
    double begin = (double)start.sec + (double)start.nanosec * 1e-9;
    struct rusage before, after;
    getrusage(RUSAGE_SELF, &before);

    double end = box_wait_until(begin + 0.2);

    getrusage(RUSAGE_SELF, &after);
    double cpu = (double)(after.ru_utime.tv_sec - before.ru_utime.tv_sec) +
        (double)(after.ru_utime.tv_usec - before.ru_utime.tv_usec) * 1e-6 +
        (double)(after.ru_stime.tv_sec - before.ru_stime.tv_sec) +
        (double)(after.ru_stime.tv_usec - before.ru_stime.tv_usec) * 1e-6;
    test_assert(end >= begin + 0.2);
    test_assert(end < begin + 0.25);
    test_assert(cpu < 0.02);

    ecs_fini(world);
}

void FrameClock_fps_command(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_os_api_now_t now = ecs_os_api.now_;
    ecs_os_api.now_ = FakeNow;
    fake_clock = 100;
    fake_work = 0.010;
    fake_frame = 0;
    fake_slow_frame = -1;
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "fps", &error);
    test_bool(error, true);
    ecs_os_free(out);

    for (int i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
    }

    const char *names[] = { "fps", "frames" };
    for (int i = 0; i < 2; i ++) {
        out = box_terminal_run(world, s, names[i], &error);
        test_bool(error, false);
        test_assert(out != NULL);
        test_assert(strstr(out, "100.0 fps") != NULL);
        test_assert(strstr(out, "delta") != NULL);
        test_assert(strstr(out, "interval") != NULL);
        test_assert(strstr(out, "0 late") != NULL);
        ecs_os_free(out);
    }

    ecs_fini(world);
    ecs_os_api.now_ = now;
}
