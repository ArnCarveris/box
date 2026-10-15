/**
 * @file FrameClock.c
 * @brief Tests for the frame loop: delta time, the frame cap, vsync pacing and FrameTiming.
 */

#include <box.h>
#include <bake_test.h>
#include <app.h>
#include "render.h"
#include <string.h>
#include <sys/resource.h>

static double fake_clock = 100;
static double fake_work = 0;
static int32_t fake_frame = 0;
static int32_t fake_slow_frame = -1;
static double fake_jitter = 0;
static uint32_t fake_seed = 1;
static int64_t fake_mark_frame = -1;
static int32_t fake_mark_mismatches = 0;
static int32_t fake_mark_late = 0;

/** A GPU on the fake clock that finishes its frames in order */
typedef struct {
    uint64_t first;     /**< Fence before the first frame of this GPU */
    uint64_t issued;    /**< Frames rendered */
    double done[64];    /**< Fake time each recent frame finishes */
    double gpu;         /**< Seconds of GPU work per frame */
    double slow;        /**< Seconds of GPU work of the slow frame */
    int64_t slow_frame; /**< Frame whose GPU work is slow, -1 for none */
    int32_t foreign;    /**< Waits for a fence this GPU never issued */
} fake_gpu_t;

static uint64_t FakeNow(void) {
    return (uint64_t)(fake_clock * 1e9);
}

static void FakeSleep(int32_t sec, int32_t nanosec) {
    fake_clock += (double)sec + (double)nanosec * 1e-9;
    if (fake_jitter > 0) {
        fake_seed = fake_seed * 1664525u + 1013904223u;
        fake_clock += fake_jitter * (double)(fake_seed >> 8) / 16777216.0;
    }
}

static void FakeMark(ecs_iter_t *it) {
    const RenderStatus *r = ecs_singleton_get(it->world, RenderStatus);
    if (r && (int64_t)ecs_get_world_info(it->world)->frame_count_total == fake_mark_frame) {
        fake_mark_mismatches = r->delta_mismatches;
        fake_mark_late = r->presents_late;
    }
}

static int32_t fake_switches = 0;
static FrameTiming fake_switch_timing[2];

static void FakeSwitch(ecs_iter_t *it) {
    int64_t frame = (int64_t)ecs_get_world_info(it->world)->frame_count_total;
    if (frame == 1800 || frame == 2600) {
        fake_switch_timing[fake_switches ++] = *ecs_singleton_get(it->world, FrameTiming);
    }

    if (frame == 1800) {
        ecs_singleton_set(it->world, RenderSettings, {
            .in_flight = 2, .main_thread = true, .display_rate = 60 });
    } else if (frame == 2600) {
        App *app = ecs_singleton_get_mut(it->world, App);
        app->fps = 20;
    }
}

static void FakeGpuRender(void *ctx, const box_render_packet_t *p) {
    fake_gpu_t *g = ctx;
    double start = g->issued ? g->done[g->issued % 64] : 0;
    start = start > fake_clock ? start : fake_clock;
    g->issued ++;
    g->done[g->issued % 64] = start + (p->frame == g->slow_frame ? g->slow : g->gpu);
}

static uint64_t FakeGpuFence(void *ctx) {
    fake_gpu_t *g = ctx;

    return g->first + g->issued;
}

static void FakeGpuWait(void *ctx, uint64_t fence) {
    fake_gpu_t *g = ctx;
    if (fence <= g->first || fence > g->first + g->issued) {
        g->foreign ++;
    } else {
        double done = g->done[(fence - g->first) % 64];
        fake_clock = done > fake_clock ? done : fake_clock;
    }
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
    fake_jitter = 0;
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });
    ecs_singleton_set(world, RenderSettings, {
        .in_flight = 2, .main_thread = true, .display_rate = 120 });
    ecs_singleton_set(world, App, { .frames = 800, .fps = 60 });

    test_int(box_app_run(world), 0);

    const FrameTiming *t = ecs_singleton_get(world, FrameTiming);
    test_bool(t->smoothing, true);
    test_assert(t->refresh_rate > 119.99f && t->refresh_rate < 120.01f);
    test_assert(t->delta.max - t->delta.min < 0.001);
    test_assert(t->delta.min > 16.665 && t->delta.max < 16.668);
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
    fake_jitter = 0;
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });
    ecs_singleton_set(world, RenderSettings, {
        .in_flight = 2, .main_thread = true, .display_rate = 120 });
    float fps[] = { 30, 60, 120, 0, 240 };
    int32_t expect[] = { 4, 2, 1, 1, 1 };
    for (int i = 0; i < 5; i ++) {
        ecs_singleton_set(world, App, { .fps = fps[i] });
        ecs_progress(world, 0.01f);
        test_int(ecs_singleton_get(world, RenderStatus)->swap_interval, expect[i]);
        test_int(ecs_singleton_get(world, RenderStatus)->cadence, 1);
    }

    ecs_singleton_set(world, App, { .fps = 90 });
    int32_t refreshes = 0;
    for (int i = 0; i < 6; i ++) {
        ecs_progress(world, 0.01f);
        const RenderStatus *r = ecs_singleton_get(world, RenderStatus);
        test_int(r->cadence, 3);
        test_assert(r->swap_interval == 1 || r->swap_interval == 2);
        refreshes += r->swap_interval;
    }

    test_int(refreshes, 8);

    ecs_singleton_set(world, RenderSettings, {
        .in_flight = 2, .main_thread = true, .display_rate = 59 });
    ecs_singleton_set(world, App, { .fps = 30 });
    ecs_progress(world, 0.01f);
    ecs_progress(world, 0.01f);
    test_int(ecs_singleton_get(world, RenderStatus)->swap_interval, 2);
    test_int(ecs_singleton_get(world, RenderStatus)->cadence, 1);

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

void FrameClock_cadence_delta_matches_display(void) {
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
    fake_jitter = 0.0003;
    fake_mark_frame = 90;
    fake_mark_mismatches = -1;
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeMark });
    ecs_singleton_set(world, RenderSettings, {
        .in_flight = 2, .main_thread = true, .display_rate = 120 });
    ecs_singleton_set(world, App, { .frames = 5400, .fps = 90 });

    test_int(box_app_run(world), 0);

    const RenderStatus *r = ecs_singleton_get(world, RenderStatus);
    const FrameTiming *t = ecs_singleton_get(world, FrameTiming);
    test_int(r->cadence, 3);
    test_assert(fake_mark_mismatches >= 0);
    test_int(r->delta_mismatches, fake_mark_mismatches);
    test_int(r->presents_late, 0);
    test_int(r->presents_early, 0);
    test_bool(t->smoothing, true);
    test_assert(t->present.min > 8.33 && t->present.min < 8.34);
    test_assert(t->present.max > 16.66 && t->present.max < 16.67);
    test_assert(t->delta.min > 8.2 && t->delta.min < 8.5);
    test_assert(t->delta.max > 16.5 && t->delta.max < 16.8);
    test_assert(t->interval.mean > 11.0 && t->interval.mean < 11.2);

    ecs_fini(world);
    ecs_os_api.now_ = now;
    ecs_os_api.sleep_ = sleep;
    fake_jitter = 0;
    fake_mark_frame = -1;
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

void FrameClock_long_run_59_94_hz_no_drift(void) {
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
    fake_jitter = 0.0003;
    fake_mark_frame = 600;
    fake_mark_mismatches = -1;
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeMark });
    ecs_singleton_set(world, RenderSettings, {
        .in_flight = 2, .main_thread = true, .display_rate = 59.94f });
    ecs_singleton_set(world, App, { .frames = 36000, .fps = 60 });

    test_int(box_app_run(world), 0);

    const RenderStatus *r = ecs_singleton_get(world, RenderStatus);
    const FrameTiming *t = ecs_singleton_get(world, FrameTiming);
    test_int(r->swap_interval, 1);
    test_assert(fake_mark_mismatches >= 0);
    test_int(r->delta_mismatches, fake_mark_mismatches);
    test_int(r->presents_late, 0);
    test_bool(t->smoothing, true);
    test_assert(t->refresh_rate > 59.93f && t->refresh_rate < 59.95f);
    test_assert(t->delta.max < 16.70);
    test_assert(t->total > 35990);

    ecs_fini(world);
    ecs_os_api.now_ = now;
    ecs_os_api.sleep_ = sleep;
    fake_jitter = 0;
    fake_mark_frame = -1;
}

void FrameClock_long_run_143_98_hz_no_drift(void) {
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
    fake_jitter = 0.0003;
    fake_mark_frame = 1440;
    fake_mark_mismatches = -1;
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeMark });
    ecs_singleton_set(world, RenderSettings, {
        .in_flight = 2, .main_thread = true, .display_rate = 143.98f });
    ecs_singleton_set(world, App, { .frames = 86400, .fps = 144 });

    test_int(box_app_run(world), 0);

    const RenderStatus *r = ecs_singleton_get(world, RenderStatus);
    const FrameTiming *t = ecs_singleton_get(world, FrameTiming);
    test_int(r->swap_interval, 1);
    test_assert(fake_mark_mismatches >= 0);
    test_int(r->delta_mismatches, fake_mark_mismatches);
    test_int(r->presents_late, 0);
    test_bool(t->smoothing, true);
    test_assert(t->refresh_rate > 143.97f && t->refresh_rate < 143.99f);
    test_assert(t->delta.max < 6.96);
    test_assert(t->total > 86390);

    ecs_fini(world);
    ecs_os_api.now_ = now;
    ecs_os_api.sleep_ = sleep;
    fake_jitter = 0;
    fake_mark_frame = -1;
}

void FrameClock_late_gpu_repeats_frame(void) {
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
    fake_jitter = 0;
    fake_mark_frame = 400;
    fake_mark_late = -1;
    static fake_gpu_t gpu;
    gpu = (fake_gpu_t){ .gpu = 0.005, .slow = 0.045, .slow_frame = 450 };
    box_render_set_backend(world, &(box_render_backend_t){
        .ctx = &gpu, .render = FakeGpuRender, .fence = FakeGpuFence, .wait = FakeGpuWait });
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeMark });
    ecs_singleton_set(world, RenderSettings, {
        .in_flight = 2, .main_thread = true, .display_rate = 120 });
    ecs_singleton_set(world, App, { .frames = 600, .fps = 60 });

    test_int(box_app_run(world), 0);

    const RenderStatus *r = ecs_singleton_get(world, RenderStatus);
    test_int(fake_mark_late, 0);
    test_assert(r->presents_late > 0);
    test_assert(r->delta_mismatches > fake_mark_mismatches);
    test_int(gpu.foreign, 0);

    ecs_fini(world);
    ecs_os_api.now_ = now;
    ecs_os_api.sleep_ = sleep;
    fake_mark_frame = -1;
}

void FrameClock_fences_end_with_backend(void) {
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
    fake_jitter = 0;
    static fake_gpu_t first, second, third;
    first = (fake_gpu_t){ .first = 1000, .gpu = 0.003, .slow_frame = -1 };
    second = (fake_gpu_t){ .first = 0, .gpu = 0.003, .slow_frame = -1 };
    third = (fake_gpu_t){ .first = 0, .gpu = 0.003, .slow_frame = -1 };
    box_render_set_backend(world, &(box_render_backend_t){
        .ctx = &first, .render = FakeGpuRender, .fence = FakeGpuFence, .wait = FakeGpuWait });
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });
    ecs_singleton_set(world, RenderSettings, {
        .in_flight = 2, .main_thread = true, .display_rate = 120 });
    ecs_singleton_set(world, App, { .fps = 60 });
    for (int i = 0; i < 20; i ++) {
        ecs_progress(world, 0);
    }

    box_render_set_backend(world, &(box_render_backend_t){
        .ctx = &second, .render = FakeGpuRender, .fence = FakeGpuFence, .wait = FakeGpuWait });
    for (int i = 0; i < 20; i ++) {
        ecs_progress(world, 0);
    }

    ecs_singleton_set(world, RenderSettings, {
        .in_flight = 2, .main_thread = false, .display_rate = 120 });
    for (int i = 0; i < 20; i ++) {
        ecs_progress(world, 0);
    }

    box_render_set_backend(world, &(box_render_backend_t){
        .ctx = &third, .render = FakeGpuRender, .fence = FakeGpuFence, .wait = FakeGpuWait });
    for (int i = 0; i < 20; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    test_assert(first.issued > 0);
    test_assert(second.issued > 0);
    test_assert(third.issued > 0);
    test_int(first.foreign, 0);
    test_int(second.foreign, 0);
    test_int(third.foreign, 0);

    ecs_fini(world);
    ecs_os_api.now_ = now;
    ecs_os_api.sleep_ = sleep;
}

void FrameClock_smoothing_follows_display_change(void) {
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
    fake_jitter = 0;
    fake_switches = 0;
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeSwitch });
    ecs_singleton_set(world, RenderSettings, {
        .in_flight = 2, .main_thread = true, .display_rate = 120 });
    ecs_singleton_set(world, App, { .frames = 3400, .fps = 0 });

    test_int(box_app_run(world), 0);

    test_int(fake_switches, 2);
    test_bool(fake_switch_timing[0].smoothing, true);
    test_assert(fake_switch_timing[0].refresh_rate > 119.99f);
    test_assert(fake_switch_timing[0].refresh_rate < 120.01f);
    test_assert(fake_switch_timing[0].delta.min > 8.33);
    test_assert(fake_switch_timing[0].delta.max < 8.34);
    test_bool(fake_switch_timing[1].smoothing, true);
    test_assert(fake_switch_timing[1].refresh_rate > 59.99f);
    test_assert(fake_switch_timing[1].refresh_rate < 60.01f);
    test_assert(fake_switch_timing[1].delta.min > 16.66);
    test_assert(fake_switch_timing[1].delta.max < 16.67);

    const FrameTiming *t = ecs_singleton_get(world, FrameTiming);
    const RenderStatus *r = ecs_singleton_get(world, RenderStatus);
    test_int(r->swap_interval, 3);
    test_bool(t->smoothing, true);
    test_assert(t->delta.min > 49.99 && t->delta.max < 50.01);

    ecs_fini(world);
    ecs_os_api.now_ = now;
    ecs_os_api.sleep_ = sleep;
}

void FrameClock_cap_guard_never_binds_paced(void) {
    // testing/town_stutter
    test_flt(box_app_cap_interval(60, 120, 0), 1.0 / 60.0);
    test_flt(box_app_cap_interval(60, 0, 2), 1.0 / 60.0);
    test_flt(box_app_cap_interval(0, 120, 1), 0);
    test_flt(box_app_cap_interval(60, 120, 2), BOX_APP_PACED_GUARD * 2.0 / 120.0);
    test_flt(box_app_cap_interval(30, 59, 2), BOX_APP_PACED_GUARD * 2.0 / 59.0);
    test_flt(box_app_cap_interval(90, 120, 2), BOX_APP_PACED_GUARD / 120.0);
    test_flt(box_app_cap_interval(90, 120, 1), BOX_APP_PACED_GUARD / 120.0);
    test_assert(box_app_cap_interval(60, 120, 2) < 2.0 / 120.0);
    test_assert(box_app_cap_interval(90, 120, 1) < 1.0 / 120.0);
}

void FrameClock_cap_guard_bounds_runaway(void) {
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
    fake_jitter = 0;
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });
    ecs_singleton_set(world, App, { .frames = 200, .fps = 60 });
    ecs_singleton_set(world, RenderStatus, {
        .vsync = true, .refresh_rate = 120, .swap_interval = 2, .cadence = 1 });
    ecs_entity_t render = ecs_lookup(world, "box.render.RenderSubmitPacket");
    test_assert(render != 0);
    ecs_enable(world, render, false);

    test_int(box_app_run(world), 0);

    const FrameTiming *t = ecs_singleton_get(world, FrameTiming);
    test_assert(t->interval.min > 12.49);
    test_assert(t->interval.mean > 12.49 && t->interval.mean < 12.6);

    ecs_fini(world);
    ecs_os_api.now_ = now;
    ecs_os_api.sleep_ = sleep;
}

void FrameClock_cadence_repeats(void) {
    // testing/town_stutter
    int32_t expect_90[] = { 1, 1, 2, 1, 1, 2 };
    int32_t expect_60_on_144[] = { 2, 2, 3, 2, 3 };
    double phase = 0;
    for (int i = 0; i < 6; i ++) {
        test_int(box_render_cadence_next(&phase, 120, 90), expect_90[i]);
    }

    phase = 0;
    for (int i = 0; i < 5; i ++) {
        test_int(box_render_cadence_next(&phase, 144, 60), expect_60_on_144[i]);
    }

    phase = 0;
    test_int(box_render_cadence_next(&phase, 120, 60), 2);
    test_int(box_render_cadence_next(&phase, 120, 0), 1);
    test_int(box_render_cadence_next(&phase, 0, 60), 0);
    test_int(box_render_cadence_cycle(120, 90), 3);
    test_int(box_render_cadence_cycle(144, 60), 5);
    test_int(box_render_cadence_cycle(120, 60), 1);
    test_int(box_render_cadence_cycle(0, 60), 0);
    test_int(box_render_swap_interval(59, 30), 2);
    test_int(box_render_swap_interval(59.94f, 30), 2);
    test_int(box_render_swap_interval(61, 30), 2);
    test_int(box_render_swap_interval(62, 30), 0);
    test_int(box_render_swap_interval(143.98f, 72), 2);
    test_int(box_render_swap_interval(59, 60), 1);
    test_int(box_render_swap_interval(120, 90), 0);
}

void FrameClock_window_presents_follow_cadence(void) {
    // testing/town_stutter
    test_int(box_render_window_presents(true, true, 2), 2);
    test_int(box_render_window_presents(true, true, 1), 1);
    test_int(box_render_window_presents(true, true, 0), 1);
    test_int(box_render_window_presents(false, true, 2), 1);
    test_int(box_render_window_presents(true, false, 2), 1);

    box_render_target_desc_t desc = {
        .width = 2560, .height = 1440, .render_width = 2560, .render_height = 1440,
        .vsync = true, .refresh = 120 };
    test_bool(box_render_target_scene(&desc, true, 60), true);
    test_bool(box_render_target_scene(&desc, true, 90), true);
    test_bool(box_render_target_scene(&desc, true, 120), false);
    test_bool(box_render_target_scene(&desc, true, 0), false);
    test_bool(box_render_target_scene(&desc, false, 60), false);
    desc.vsync = false;
    test_bool(box_render_target_scene(&desc, true, 60), false);
    desc.refresh = 0;
    desc.vsync = true;
    test_bool(box_render_target_scene(&desc, true, 60), false);
    desc.render_width = 1280;
    desc.render_height = 720;
    test_bool(box_render_target_scene(&desc, false, 120), true);
}
