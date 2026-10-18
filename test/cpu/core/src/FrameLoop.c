/**
 * @file FrameLoop.c
 * @brief Tests for the frame loop of box_app_run: ecs_progress(world, App.delta_time), App.fps as
 * the target fps of flecs and the --delta-time option. Time runs on a fake OS clock.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

static double loop_clock = 100;
static double loop_work = 0;
static float loop_deltas[64];
static int32_t loop_frames = 0;
static int32_t loop_change_at = -1;
static float loop_change_delta = 0;
static float loop_change_fps = -1;
static ecs_os_api_now_t loop_saved_now = NULL;
static ecs_os_api_sleep_t loop_saved_sleep = NULL;

static uint64_t LoopNow(void) {
    return (uint64_t)(loop_clock * 1e9);
}

static void LoopSleep(int32_t sec, int32_t nanosec) {
    loop_clock += (double)sec + (double)nanosec * 1e-9;
}

/** Record the delta of the frame, spend loop_work fake seconds and change App when asked */
static void LoopFrame(ecs_iter_t *it) {
    if (loop_frames < 64) {
        loop_deltas[loop_frames] = ecs_get_world_info(it->world)->delta_time_raw;
    }

    loop_clock += loop_work;
    if (loop_frames == loop_change_at) {
        App *app = ecs_singleton_get_mut(it->world, App);
        if (loop_change_fps >= 0) {
            app->fps = loop_change_fps;
        } else {
            app->delta_time = loop_change_delta;
        }

        ecs_singleton_modified(it->world, App);
    }

    loop_frames ++;
}

/** Create a world on the fake clock with the system that records the deltas.
 *
 * @param work Fake seconds every frame takes
 * @return The world
 */
static ecs_world_t* loop_world(
    double work)
{
    loop_clock = 100;
    loop_work = work;
    loop_frames = 0;
    loop_change_at = -1;
    loop_change_delta = 0;
    loop_change_fps = -1;
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    loop_saved_now = ecs_os_api.now_;
    loop_saved_sleep = ecs_os_api.sleep_;
    ecs_os_api.now_ = LoopNow;
    ecs_os_api.sleep_ = LoopSleep;
    ecs_system(world, { .phase = EcsOnUpdate, .run = LoopFrame, .immediate = true });

    return world;
}

/** Delete a world of loop_world and restore the OS clock.
 *
 * @param world The world
 */
static void loop_fini(
    ecs_world_t *world)
{
    ecs_fini(world);
    ecs_os_api.now_ = loop_saved_now;
    ecs_os_api.sleep_ = loop_saved_sleep;
}

void FrameLoop_delta_time_default(void) {
    // engine/builtin_frame_pacing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    const App *app = ecs_singleton_get(world, App);
    test_flt(app->delta_time, 0);
    test_flt(app->fps, 0);
    test_flt(ecs_get_world_info(world)->target_fps, 0);

    ecs_fini(world);
}

void FrameLoop_fixed_delta_time(void) {
    // engine/builtin_frame_pacing
    ecs_world_t *world = loop_world(0.003);
    ecs_singleton_set(world, App, { .frames = 4, .delta_time = 0.025f });

    test_int(box_app_run(world), 0);
    test_int(loop_frames, 4);
    for (int i = 0; i < 4; i ++) {
        test_flt(loop_deltas[i], 0.025f);
    }

    test_flt(ecs_get_world_info(world)->delta_time, 0.025f);
    test_assert(fabs(ecs_get_world_info(world)->world_time_total - 0.1) < 1e-6);

    loop_fini(world);
}

void FrameLoop_measured_delta_time(void) {
    // engine/builtin_frame_pacing
    ecs_world_t *world = loop_world(0.010);
    ecs_singleton_set(world, App, { .frames = 6 });

    test_int(box_app_run(world), 0);
    test_int(loop_frames, 6);
    for (int i = 1; i < 6; i ++) {
        test_assert(fabsf(loop_deltas[i] - 0.010f) < 1e-5f);
    }

    loop_fini(world);
}

void FrameLoop_delta_time_change_applies_next_frame(void) {
    // engine/builtin_frame_pacing
    ecs_world_t *world = loop_world(0.010);
    loop_change_at = 2;
    loop_change_delta = 0.05f;
    ecs_singleton_set(world, App, { .frames = 6 });

    test_int(box_app_run(world), 0);
    test_int(loop_frames, 6);
    test_assert(fabsf(loop_deltas[1] - 0.010f) < 1e-5f);
    test_assert(fabsf(loop_deltas[2] - 0.010f) < 1e-5f);
    test_flt(loop_deltas[3], 0.05f);
    test_flt(loop_deltas[5], 0.05f);

    loop_fini(world);
}

void FrameLoop_fps_sets_target_fps(void) {
    // engine/builtin_frame_pacing
    ecs_world_t *world = loop_world(0.002);
    ecs_singleton_set(world, App, { .fps = 40 });
    test_flt(ecs_get_world_info(world)->target_fps, 40);

    ecs_singleton_set(world, App, { .fps = 0 });
    test_flt(ecs_get_world_info(world)->target_fps, 0);

    ecs_singleton_set(world, App, { .frames = 5, .fps = 40 });
    test_int(box_app_run(world), 0);
    test_flt(ecs_get_world_info(world)->target_fps, 40);
    for (int i = 1; i < 5; i ++) {
        test_assert(fabsf(loop_deltas[i] - 0.025f) < 1e-5f);
    }

    loop_fini(world);
}

void FrameLoop_fps_change_while_running(void) {
    // engine/builtin_frame_pacing
    ecs_world_t *world = loop_world(0.002);
    loop_change_at = 2;
    loop_change_fps = 20;
    ecs_singleton_set(world, App, { .frames = 7, .fps = 50 });

    test_int(box_app_run(world), 0);
    test_flt(ecs_get_world_info(world)->target_fps, 20);
    test_assert(fabsf(loop_deltas[2] - 0.020f) < 1e-5f);
    for (int i = 4; i < 7; i ++) {
        test_assert(fabsf(loop_deltas[i] - 0.050f) < 1e-5f);
    }

    loop_fini(world);
}

void FrameLoop_fps_caps_headless(void) {
    // engine/builtin_frame_pacing
    ecs_world_t *world = loop_world(0.002);
    ecs_singleton_set(world, App, {
        .headless = true, .frames = 5, .fps = 40, .cpu_budget = 100,
        .unguarded = true, .unpaced = true });

    test_int(box_app_run(world), 0);
    test_flt(ecs_get_world_info(world)->target_fps, 40);
    for (int i = 1; i < 5; i ++) {
        test_assert(fabsf(loop_deltas[i] - 0.025f) < 1e-5f);
    }

    loop_fini(world);
}

void FrameLoop_headless_measures_delta(void) {
    // engine/builtin_frame_pacing
    ecs_world_t *world = loop_world(0.007);
    char *argv[] = {"boxrun", "--headless", "--frames", "5", "--port", "0"};
    test_int(box_app_parse(world, 6, argv), 0);
    App *app = ecs_singleton_get_mut(world, App);
    app->unguarded = true;
    app->unpaced = true;
    test_flt(app->delta_time, 0);

    test_int(box_app_run(world), 0);
    test_int(loop_frames, 5);
    test_flt(ecs_get_world_info(world)->target_fps, 0);
    for (int i = 1; i < 5; i ++) {
        test_assert(fabsf(loop_deltas[i] - 0.007f) < 1e-5f);
    }

    loop_fini(world);
}

void FrameLoop_parse_delta_time(void) {
    // engine/builtin_frame_pacing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--delta-time", "0.016666667"};
    test_int(box_app_parse(world, 3, argv), 0);
    test_flt(ecs_singleton_get(world, App)->delta_time, 1.0f / 60.0f);

    char *zero[] = {"boxrun", "--delta-time", "0"};
    test_int(box_app_parse(world, 3, zero), 0);
    test_flt(ecs_singleton_get(world, App)->delta_time, 0);

    ecs_fini(world);
}

void FrameLoop_parse_delta_time_negative(void) {
    // engine/builtin_frame_pacing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--delta-time", "-0.01"};
    ecs_log_set_level(-4);
    test_assert(box_app_parse(world, 3, argv) != 0);
    test_flt(ecs_singleton_get(world, App)->delta_time, 0);

    ecs_fini(world);
}

void FrameLoop_parse_delta_time_not_a_number(void) {
    // engine/builtin_frame_pacing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_log_set_level(-4);
    char *text[] = {"boxrun", "--delta-time", "fast"};
    test_assert(box_app_parse(world, 3, text) != 0);
    char *suffix[] = {"boxrun", "--delta-time", "0.01s"};
    test_assert(box_app_parse(world, 3, suffix) != 0);
    char *missing[] = {"boxrun", "--delta-time"};
    test_assert(box_app_parse(world, 2, missing) != 0);
    test_flt(ecs_singleton_get(world, App)->delta_time, 0);

    ecs_fini(world);
}

void FrameLoop_parse_delta_time_not_finite(void) {
    // engine/builtin_frame_pacing
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_log_set_level(-4);
    const char *values[] = {"inf", "nan", "1e300"};
    for (int i = 0; i < 3; i ++) {
        char *argv[] = {"boxrun", "--delta-time", (char*)values[i]};
        test_assert(box_app_parse(world, 3, argv) != 0);
    }

    test_flt(ecs_singleton_get(world, App)->delta_time, 0);

    ecs_fini(world);
}
