/**
 * @file FrameClock.c
 * @brief Tests for the frame timing: delta time against the interval, late and dropped frames,
 * the presents against the cap and the fps and frames commands.
 */

#include <box.h>
#include <bake_test.h>
#include <app.h>
#include "render.h"
#include <string.h>

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
    ecs_os_api_sleep_t sleep = ecs_os_api.sleep_;
    ecs_os_api.now_ = FakeNow;
    ecs_os_api.sleep_ = FakeSleep;
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
    ecs_os_api.sleep_ = sleep;
}

/** Present a packet, the one of frame 6 30 ms late: its present interval is late and the next
 * frame, which then starts at once, presents early */
static void FakePresentRender(void *ctx, const box_render_packet_t *p) {
    (void)ctx;
    fake_clock += p->frame == 6 ? 0.030 : 0;
}

void FrameClock_presents_late_against_cap(void) {
    // engine/builtin_frame_pacing
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
    box_render_set_backend(world, &(box_render_backend_t){ .render = FakePresentRender });
    ecs_singleton_set(world, RenderSettings, { .in_flight = 2, .main_thread = true });
    ecs_singleton_set(world, App, { .fps = 50 });

    for (int i = 0; i < 12; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    const RenderStatus *r = ecs_singleton_get(world, RenderStatus);
    test_int(r->presented, 12);
    test_int(r->presents_late, 1);
    test_int(r->presents_early, 1);
    test_bool(r->vsync, false);

    ecs_fini(world);
    ecs_os_api.now_ = now;
    ecs_os_api.sleep_ = sleep;
}

void FrameClock_fps_command(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_os_api_now_t now = ecs_os_api.now_;
    ecs_os_api_sleep_t sleep = ecs_os_api.sleep_;
    ecs_os_api.now_ = FakeNow;
    ecs_os_api.sleep_ = FakeSleep;
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
        test_assert(strstr(out, "no cap") != NULL);
        test_assert(strstr(out, "refresh") == NULL);
        ecs_os_free(out);
    }

    out = box_terminal_run(world, s, "fps 50", &error);
    test_bool(error, false);
    test_assert(out != NULL && strstr(out, "cap 50 fps") != NULL);
    ecs_os_free(out);
    test_flt(ecs_singleton_get(world, App)->fps, 50);
    test_flt(ecs_get_world_info(world)->target_fps, 50);

    for (int i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
    }

    out = box_terminal_run(world, s, "frames", &error);
    test_bool(error, false);
    test_assert(out != NULL && strstr(out, "cap 50") != NULL);
    ecs_os_free(out);

    out = box_terminal_run(world, s, "fps 0", &error);
    test_bool(error, false);
    ecs_os_free(out);
    test_flt(ecs_singleton_get(world, App)->fps, 0);
    test_flt(ecs_get_world_info(world)->target_fps, 0);

    const char *invalid[] = { "fps -1", "fps fast", "fps inf" };
    for (int i = 0; i < 3; i ++) {
        out = box_terminal_run(world, s, invalid[i], &error);
        test_bool(error, true);
        ecs_os_free(out);
    }

    test_flt(ecs_singleton_get(world, App)->fps, 0);

    ecs_fini(world);
    ecs_os_api.now_ = now;
    ecs_os_api.sleep_ = sleep;
}
