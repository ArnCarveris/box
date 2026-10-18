/**
 * @file FramePacing.c
 * @brief Tests for frame pacing: the default cap, the cap interval, the delta smoother fed with
 * simulated refreshes and presents, windows on a simulated display and the frame start wait.
 */

#include <box.h>
#include <bake_test.h>
#include <app.h>
#include "render.h"
#include <math.h>
#include <string.h>

/** Frames a simulated display runs before its deltas are checked: the estimate locks by then */
#define PACING_WARMUP (1500)

/** Frames whose deltas are checked */
#define PACING_FRAMES (2000)

/** Seconds a snapped delta may differ from the refreshes it stands for: the bleed of the time
 * left over by the jitter */
#define PACING_EXACT (0.00005)

/** Seconds a delta may differ from its refreshes while a late frame is handed back */
#define PACING_BLEED (0.0002)

static double fake_clock = 100;
static double fake_work = 0;
static double fake_jitter = 0;
static uint32_t fake_seed = 1;

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

static void FakeWork(ecs_iter_t *it) {
    (void)it;
    fake_clock += fake_work;
}

static float fake_delta = 0;

static void RecordDelta(ecs_iter_t *it) {
    fake_delta = it->delta_time;
}

/** A display the smoother is fed from: frames start when the display shows the frame before,
 * plus the wake up jitter of the main thread */
typedef struct {
    box_app_smoother_t smoother; /**< The smoother under test */
    double hz;          /**< Refresh rate the display runs at */
    float reported;     /**< Refresh rate the display reports */
    double jitter;      /**< Seconds the main thread wakes late at most */
    double display;     /**< Seconds of the refresh the last frame was shown at */
    double start;       /**< Seconds the last frame started */
    uint32_t seed;      /**< State of the jitter */
} pacing_sim_t;

/** Deltas of a range of frames */
typedef struct {
    double min;         /**< Shortest delta in seconds */
    double max;         /**< Longest delta */
    double handed;      /**< Sum of the deltas */
    double measured;    /**< Sum of the measured deltas */
    double error;       /**< Largest difference of a delta and the refreshes it stands for */
    int32_t smoothed;   /**< Deltas snapped to refreshes */
    int32_t frames;     /**< Frames in the range */
} pacing_range_t;

static pacing_sim_t pacing_sim(double hz, float reported, double jitter) {
    pacing_sim_t result = { .hz = hz, .reported = reported, .jitter = jitter, .seed = 7 };
    box_app_smoother_reset(&result.smoother);

    return result;
}

static double pacing_random(pacing_sim_t *sim) {
    sim->seed = sim->seed * 1664525u + 1013904223u;

    return (double)(sim->seed >> 8) / 16777216.0;
}

/** Feed the smoother one measured delta and add the result to a range.
 *
 * @param sim The display
 * @param raw Measured seconds since the previous frame
 * @param presents Refreshes the frame before is shown for, as the ring reports them
 * @param cycle Frames after which the present counts repeat
 * @param expect Seconds the delta should be, 0 to not check it
 * @param r The range, NULL to not record the delta
 * @return The delta in seconds
 */
static double pacing_feed(
    pacing_sim_t *sim,
    double raw,
    int32_t presents,
    int32_t cycle,
    double expect,
    pacing_range_t *r)
{
    int64_t ns = box_app_smooth_delta(&sim->smoother, (int64_t)llround(raw * 1e9), presents,
        cycle, sim->reported);
    double delta = (double)ns * 1e-9;
    if (r) {
        r->min = !r->frames || delta < r->min ? delta : r->min;
        r->max = !r->frames || delta > r->max ? delta : r->max;
        r->handed += delta;
        r->measured += raw;
        r->smoothed += sim->smoother.smoothed;
        if (expect > 0 && fabs(delta - expect) > r->error) {
            r->error = fabs(delta - expect);
        }

        r->frames ++;
    }

    return delta;
}

/** Run one frame on the display: the frame before stays for some refreshes, and the frame starts
 * when it is replaced, late by the jitter and by a delay of the present.
 *
 * @param sim The display
 * @param refreshes Refreshes the frame before actually stays on screen
 * @param presents Refreshes the frame before is planned to stay, as the ring reports them
 * @param cycle Frames after which the present counts repeat
 * @param late Seconds the present that starts the frame returns late
 * @param r The range, NULL to not record the delta
 * @return The delta in seconds
 */
static double pacing_frame(
    pacing_sim_t *sim,
    int32_t refreshes,
    int32_t presents,
    int32_t cycle,
    double late,
    pacing_range_t *r)
{
    double start, raw;
    sim->display += (double)refreshes / sim->hz;
    start = sim->display + sim->jitter * pacing_random(sim) + late;
    raw = sim->start > 0 ? start - sim->start : (double)refreshes / sim->hz;
    sim->start = start;

    return pacing_feed(sim, raw, presents, cycle, (double)refreshes / sim->hz, r);
}

/** Check that a display paced at a whole number of refreshes per frame gets deltas of exactly
 * those refreshes at the refresh rate it measured, and that they add up to the wall time.
 *
 * @param hz Refresh rate the display runs at
 * @param reported Refresh rate the display reports
 * @param presents Refreshes each frame is shown for
 */
static void pacing_steady(
    double hz,
    float reported,
    int32_t presents)
{
    pacing_sim_t sim = pacing_sim(hz, reported, 0.0005);
    pacing_range_t all = {0}, r = {0};
    double shown = (double)presents / hz;
    int32_t i;
    for (i = 0; i < PACING_WARMUP; i ++) {
        pacing_frame(&sim, presents, presents, 1, 0, &all);
    }

    for (i = 0; i < PACING_FRAMES; i ++) {
        pacing_frame(&sim, presents, presents, 1, 0, &r);
    }

    test_int(r.smoothed, r.frames);
    test_assert(fabs(r.min - shown) < PACING_EXACT);
    test_assert(fabs(r.max - shown) < PACING_EXACT);
    test_assert(fabs(r.handed - r.measured) < 1.0 / hz);
    test_assert(fabs(all.handed + r.handed - all.measured - r.measured) < 1.0 / hz);
    test_bool(sim.smoother.complete, true);
    test_bool(sim.smoother.snap_measured, true);
    test_assert(fabs(1e9 / sim.smoother.snap_period - hz) < 0.05);
}

/** Clock functions of the OS API before pacing_window replaced them */
static ecs_os_api_now_t pacing_now = NULL;
static ecs_os_api_sleep_t pacing_sleep = NULL;

/** Run a world on the simulated display of the null backend with the fake clock, which stays
 * installed until pacing_window_fini.
 *
 * @param display Refresh rate the simulated display reports
 * @param actual Rate it really runs at, 0 for the reported one
 * @param fps App.fps, 0 for the default of a window
 * @param frames Frames to run
 * @return The world, deleted with pacing_window_fini
 */
static ecs_world_t* pacing_window(
    float display,
    float actual,
    float fps,
    int32_t frames)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    pacing_now = ecs_os_api.now_;
    pacing_sleep = ecs_os_api.sleep_;
    ecs_os_api.now_ = FakeNow;
    ecs_os_api.sleep_ = FakeSleep;
    fake_clock = 100;
    fake_work = 0.002;
    fake_jitter = 0.0003;
    fake_seed = 1;
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });
    ecs_singleton_set(world, RenderSettings, {
        .in_flight = 2, .main_thread = true, .display_rate = display, .display_actual = actual });
    ecs_singleton_set(world, App, { .frames = frames, .fps = fps });
    test_int(box_app_run(world), 0);

    return world;
}

/** Delete a world of pacing_window and put the clock back.
 *
 * @param world The world
 */
static void pacing_window_fini(
    ecs_world_t *world)
{
    ecs_fini(world);
    ecs_os_api.now_ = pacing_now;
    ecs_os_api.sleep_ = pacing_sleep;
    fake_jitter = 0;
}

/** Check that a window on the simulated display shows every frame for the same refreshes and
 * advances each frame by exactly the time it is shown.
 *
 * @param display Refresh rate of the simulated display
 * @param fps App.fps, 0 for the default of a window
 * @param presents Refreshes each frame should be shown for
 */
static void pacing_window_check(
    float display,
    float fps,
    int32_t presents)
{
    ecs_world_t *world = pacing_window(display, 0, fps, 1800);
    const RenderStatus *r = ecs_singleton_get(world, RenderStatus);
    const FrameTiming *t = ecs_singleton_get(world, FrameTiming);
    double shown = 1000.0 * (double)presents / (double)display;
    test_bool(r->vsync, true);
    test_int(r->swap_interval, presents);
    test_int(r->cadence, 1);
    test_int(r->presents_late, 0);
    test_int(r->presents_early, 0);
    test_bool(t->smoothing, true);
    test_bool(t->refresh_measured, true);
    test_assert(fabs(t->refresh_rate - display) < 0.05f);
    test_assert(fabs(t->delta.min - shown) < 0.05 && fabs(t->delta.max - shown) < 0.05);
    test_assert(fabs(t->present.min - shown) < 0.01 && fabs(t->present.max - shown) < 0.01);
    test_assert(fabs(t->interval.mean - shown) < 0.01);
    test_assert(fabs(t->frame_ms - shown) < 0.01);
    test_int(t->dropped, 0);

    pacing_window_fini(world);
}

void FramePacing_default_window_uncapped(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun"};
    test_int(box_app_parse(world, 1, argv), 0);
    const App *app = ecs_singleton_get(world, App);
    test_flt(app->fps, 0);
    test_flt(box_app_fps(app), 0);

    char *capped[] = {"boxrun", "--fps", "60"};
    test_int(box_app_parse(world, 3, capped), 0);
    test_flt(box_app_fps(ecs_singleton_get(world, App)), 60);

    test_flt(box_app_fps(&(App){ .headless = true }), BOX_APP_FPS);
    test_flt(box_app_fps(&(App){ .headless = true, .fps = 20 }), 20);
    test_flt(box_app_fps(&(App){ .headless = true, .realtime = true }), 0);
    test_flt(box_app_fps(&(App){ .headless = true, .realtime = true, .fps = 90 }), 90);
    test_flt(box_app_fps(NULL), 0);

    ecs_fini(world);
}

void FramePacing_headless_default_fixed_step(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_os_api_now_t now = ecs_os_api.now_;
    ecs_os_api_sleep_t sleep = ecs_os_api.sleep_;
    ecs_os_api.now_ = FakeNow;
    ecs_os_api.sleep_ = FakeSleep;
    fake_clock = 100;
    fake_work = 0.001;
    fake_jitter = 0;
    fake_delta = 0;
    ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });
    ecs_system(world, { .phase = EcsOnUpdate, .run = RecordDelta });
    ecs_singleton_set(world, App, {
        .headless = true, .frames = 20, .unguarded = true, .unpaced = true });

    test_int(box_app_run(world), 0);

    const FrameTiming *t = ecs_singleton_get(world, FrameTiming);
    test_bool(t->fixed, true);
    test_flt(fake_delta, 1.0f / (float)BOX_APP_FPS);
    test_assert(fabs(t->target_ms - 1000.0 / BOX_APP_FPS) < 0.001);
    test_assert(fabs(t->delta.min - 1000.0 / BOX_APP_FPS) < 0.001);
    test_assert(fabs(t->delta.max - 1000.0 / BOX_APP_FPS) < 0.001);
    test_assert(fabs(t->interval.mean - 1000.0 / BOX_APP_FPS) < 0.01);

    ecs_fini(world);
    ecs_os_api.now_ = now;
    ecs_os_api.sleep_ = sleep;
}

void FramePacing_cap_interval(void) {
    // testing/town_stutter
    test_flt(box_app_cap_interval(60, 120, 2), 2.0 / 120.0);
    test_flt(box_app_cap_interval(72, 144, 2), 2.0 / 144.0);
    test_flt(box_app_cap_interval(30, 59.94f, 2), 2.0 / (double)59.94f);
    test_flt(box_app_cap_interval(90, 120, 1), 1.0 / 120.0);
    test_flt(box_app_cap_interval(90, 120, 2), 2.0 / 120.0);
    test_flt(box_app_cap_interval(60, 144, 3), 3.0 / 144.0);
    test_flt(box_app_cap_interval(0, 144, 1), BOX_APP_PACED_GUARD / 144.0);
    test_flt(box_app_cap_interval(0, 60, 1), BOX_APP_PACED_GUARD / 60.0);
    test_flt(box_app_cap_interval(144, 144, 1), BOX_APP_PACED_GUARD / 144.0);
    test_flt(box_app_cap_interval(60, 0, 0), 1.0 / 60.0);
    test_flt(box_app_cap_interval(0, 0, 0), 0);

    double phase = 0;
    int32_t refreshes = 0;
    for (int i = 0; i < 5; i ++) {
        refreshes += box_render_cadence_next(&phase, 144, 60);
    }

    test_int(refreshes, 12);
    test_int(box_render_swap_interval(144, 72), 2);
    test_int(box_render_swap_interval(144, 0), 1);
    test_int(box_render_swap_interval(120, 60), 2);
    test_int(box_render_swap_interval(60, 60), 1);
}

void FramePacing_smoother_60_hz(void) {
    // testing/town_stutter
    pacing_steady(60, 60, 1);
}

void FramePacing_smoother_120_hz(void) {
    // testing/town_stutter
    pacing_steady(120, 120, 1);
}

void FramePacing_smoother_144_hz(void) {
    // testing/town_stutter
    pacing_steady(144, 144, 1);
}

void FramePacing_smoother_cap_60_on_120_hz(void) {
    // testing/town_stutter
    pacing_steady(120, 120, 2);
}

void FramePacing_smoother_cap_72_on_144_hz(void) {
    // testing/town_stutter
    pacing_steady(144, 144, 2);
}

void FramePacing_smoother_cadence_90_on_120_hz(void) {
    // testing/town_stutter
    pacing_sim_t sim = pacing_sim(120, 120, 0.0005);
    pacing_range_t r = {0};
    double phase = 0;
    int32_t before = box_render_cadence_next(&phase, 120, 90);
    int32_t i;
    for (i = 0; i < PACING_WARMUP + PACING_FRAMES; i ++) {
        int32_t next = box_render_cadence_next(&phase, 120, 90);
        pacing_frame(&sim, before, before, 3, 0, i >= PACING_WARMUP ? &r : NULL);
        before = next;
    }

    test_int(r.smoothed, r.frames);
    test_assert(r.error < PACING_EXACT);
    test_assert(fabs(r.min - 1.0 / 120.0) < PACING_EXACT);
    test_assert(fabs(r.max - 2.0 / 120.0) < PACING_EXACT);
    test_assert(fabs(r.handed - r.measured) < 1.0 / 120.0);
    test_bool(sim.smoother.snap_measured, true);
    test_assert(fabs(1e9 / sim.smoother.snap_period - 120) < 0.05);
}

void FramePacing_estimate_ignores_missed_refreshes(void) {
    // testing/town_stutter
    pacing_sim_t sim = pacing_sim(120, 120, 0.0003);
    pacing_range_t r = {0};
    double missed = 0;
    int32_t i;
    for (i = 0; i < PACING_WARMUP + PACING_FRAMES; i ++) {
        int32_t refreshes = i % 30 == 29 ? 2 : 1;
        double delta = pacing_frame(&sim, refreshes, 1, 1, 0, i >= PACING_WARMUP ? &r : NULL);
        if (i >= PACING_WARMUP && refreshes == 2) {
            missed = fabs(delta - 2.0 / 120.0) > missed ? fabs(delta - 2.0 / 120.0) : missed;
        }
    }

    test_bool(sim.smoother.complete, true);
    test_bool(sim.smoother.snap_measured, true);
    test_assert(fabs(1e9 / sim.smoother.snap_period - 120) < 0.05);
    test_int(r.smoothed, r.frames);
    test_assert(missed < PACING_BLEED);
    test_assert(r.error < PACING_BLEED);
    test_assert(fabs(r.handed - r.measured) < 1.0 / 120.0);
}

void FramePacing_estimate_reported_rate_slightly_off(void) {
    // testing/town_stutter
    double hz[] = { 119.88, 59.94, 143.98, 120.4 };
    float reported[] = { 120, 60, 144, 120 };
    for (int k = 0; k < 4; k ++) {
        pacing_sim_t sim = pacing_sim(hz[k], reported[k], 0.0005);
        pacing_range_t all = {0}, r = {0};
        int32_t i;
        for (i = 0; i < PACING_WARMUP; i ++) {
            pacing_frame(&sim, 1, 1, 1, 0, &all);
        }

        for (i = 0; i < PACING_FRAMES * 4; i ++) {
            pacing_frame(&sim, 1, 1, 1, 0, &r);
        }

        test_bool(sim.smoother.complete, true);
        test_bool(sim.smoother.snap_measured, true);
        test_assert(fabs(1e9 / sim.smoother.snap_period - hz[k]) < 0.05);
        test_int(r.smoothed, r.frames);
        test_assert(r.error < PACING_EXACT);
        test_assert(fabs(r.handed - r.measured) < 1.0 / hz[k]);
        test_assert(fabs(all.handed - all.measured) < 1.0 / hz[k]);
    }
}

void FramePacing_estimate_rejects_reading_far_from_reported(void) {
    // testing/town_stutter
    pacing_sim_t sim = pacing_sim(100, 120, 0.0003);
    pacing_range_t r = {0};
    int32_t i;
    for (i = 0; i < PACING_WARMUP; i ++) {
        pacing_frame(&sim, 1, 1, 1, 0, NULL);
    }

    for (i = 0; i < PACING_FRAMES; i ++) {
        pacing_frame(&sim, 1, 1, 1, 0, &r);
    }

    test_bool(sim.smoother.complete, false);
    test_int(sim.smoother.estimated_fps, 0);
    test_int(r.smoothed, 0);
    test_assert(fabs(r.handed - r.measured) < 1e-5);
}

void FramePacing_single_late_present(void) {
    // testing/town_stutter
    double late[] = { 0.002, 0.004, 0.0055 };
    for (int k = 0; k < 3; k ++) {
        pacing_sim_t sim = pacing_sim(120, 120, 0.0003);
        pacing_range_t r = {0};
        int32_t i;
        for (i = 0; i < PACING_WARMUP; i ++) {
            pacing_frame(&sim, 1, 1, 1, 0, NULL);
        }

        for (i = 0; i < 600; i ++) {
            pacing_frame(&sim, 1, 1, 1, i == 100 ? late[k] : 0, &r);
        }

        test_int(r.smoothed, r.frames);
        test_assert(r.error < PACING_BLEED);
        test_assert(r.max < 1.0 / 120.0 + PACING_BLEED);
        test_assert(r.min > 1.0 / 120.0 - PACING_BLEED);
        test_assert(fabs(r.handed - r.measured) < 1.0 / 120.0);
    }
}

void FramePacing_single_missed_refresh(void) {
    // testing/town_stutter
    int32_t presents[] = { 1, 2 };
    double hz[] = { 120, 144 };
    for (int k = 0; k < 2; k ++) {
        pacing_sim_t sim = pacing_sim(hz[k], (float)hz[k], 0.0003);
        pacing_range_t r = {0};
        double repeated = 0;
        int32_t i;
        for (i = 0; i < PACING_WARMUP; i ++) {
            pacing_frame(&sim, presents[k], presents[k], 1, 0, NULL);
        }

        for (i = 0; i < 600; i ++) {
            int32_t shown = presents[k] + (i == 100);
            double delta = pacing_frame(&sim, shown, presents[k], 1, 0, &r);
            if (i == 100) {
                repeated = delta;
            }
        }

        test_assert(fabs(repeated - (double)(presents[k] + 1) / hz[k]) < PACING_EXACT);
        test_assert(r.error < PACING_EXACT);
        test_assert(fabs(r.handed - r.measured) < 1.0 / hz[k]);
    }
}

void FramePacing_presents_return_immediately(void) {
    // testing/town_stutter
    pacing_sim_t sim = pacing_sim(120, 120, 0.0003);
    pacing_range_t hidden = {0}, after = {0};
    int32_t i;
    for (i = 0; i < PACING_WARMUP; i ++) {
        pacing_frame(&sim, 1, 1, 1, 0, NULL);
    }

    for (i = 0; i < 300; i ++) {
        pacing_feed(&sim, 0.00005, 1, 1, 0, &hidden);
    }

    test_assert(hidden.handed < hidden.measured + 1.0 / 120.0 + PACING_EXACT);
    test_assert(hidden.max < 1.0 / 120.0 + PACING_BLEED);

    sim.start += hidden.measured;
    sim.display = sim.start;
    for (i = 0; i < 600; i ++) {
        pacing_frame(&sim, 1, 1, 1, 0, i >= 300 ? &after : NULL);
    }

    test_int(after.smoothed, after.frames);
    test_assert(after.error < PACING_EXACT);
    test_assert(fabs(after.handed - after.measured) < 1.0 / 120.0);
}

void FramePacing_late_present_then_immediate(void) {
    // testing/town_stutter
    pacing_sim_t sim = pacing_sim(120, 120, 0);
    pacing_range_t burst = {0}, after = {0};
    double period = 1.0 / 120.0;
    int32_t i;
    for (i = 0; i < PACING_WARMUP; i ++) {
        pacing_frame(&sim, 1, 1, 1, 0, NULL);
    }

    pacing_feed(&sim, period + 0.012, 1, 1, 0, &burst);
    pacing_feed(&sim, 0.0001, 1, 1, 0, &burst);
    sim.start += burst.measured;
    sim.display += 2.0 * period;

    test_assert(burst.max < 2.0 * period + PACING_BLEED);
    test_assert(burst.min > period - PACING_BLEED);
    test_assert(burst.handed < burst.measured + period + PACING_EXACT);

    for (i = 0; i < 300; i ++) {
        pacing_frame(&sim, 1, 1, 1, 0, i >= 100 ? &after : &burst);
    }

    test_assert(after.error < PACING_BLEED);
    test_assert(fabs(burst.handed + after.handed - burst.measured - after.measured) < period);
}

void FramePacing_window_uncapped_120_hz(void) {
    // testing/town_stutter
    pacing_window_check(120, 0, 1);
}

void FramePacing_window_uncapped_144_hz(void) {
    // testing/town_stutter
    pacing_window_check(144, 0, 1);
}

void FramePacing_window_cap_60_on_120_hz(void) {
    // testing/town_stutter
    pacing_window_check(120, 60, 2);
}

void FramePacing_window_cap_72_on_144_hz(void) {
    // testing/town_stutter
    pacing_window_check(144, 72, 2);
}

void FramePacing_window_cap_display_off_rate(void) {
    // testing/town_stutter
    // A cap paces the loop open loop against the reported rate, so a display 0.1% off (119.88
    // against 120) slides the phase across a refresh boundary every few seconds. This shows
    // what that does: the frames the display shows keep to 2 refreshes except for a bounded
    // number of 1 and 3 refresh frames, and the deltas still add up to the time on screen.
    ecs_world_t *world = pacing_window(120, 119.88f, 60, 3600);
    const RenderStatus *r = ecs_singleton_get(world, RenderStatus);
    const FrameTiming *t = ecs_singleton_get(world, FrameTiming);
    double shown = 2000.0 / 119.88;
    test_int(r->swap_interval, 2);
    test_assert(t->interval.mean > shown * 0.99 && t->interval.mean < shown * 1.01);
    test_assert(t->delta.min > shown * 0.45 && t->delta.max < shown * 1.55);
    test_assert(fabs(t->delta.mean - t->interval.mean) < 0.5);
    test_assert(t->dropped < 10);

    pacing_window_fini(world);
}

void FramePacing_window_cap_paced_by_clock(void) {
    // testing/town_stutter
    float fps[] = { 60, 0 };
    int32_t swap[] = { 2, 1 };
    double interval[] = { 2000.0 / 120.0, 1000.0 * BOX_APP_PACED_GUARD / 120.0 };
    for (int k = 0; k < 2; k ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        ecs_os_api_now_t now = ecs_os_api.now_;
        ecs_os_api_sleep_t sleep = ecs_os_api.sleep_;
        ecs_os_api.now_ = FakeNow;
        ecs_os_api.sleep_ = FakeSleep;
        fake_clock = 100;
        fake_work = 0.001;
        fake_jitter = 0;
        ecs_system(world, { .phase = EcsOnUpdate, .run = FakeWork });
        ecs_singleton_set(world, App, { .frames = 200, .fps = fps[k] });
        ecs_singleton_set(world, RenderStatus, {
            .vsync = true, .refresh_rate = 120, .swap_interval = swap[k], .cadence = 1 });
        ecs_entity_t render = ecs_lookup(world, "box.render.RenderSubmitPacket");
        test_assert(render != 0);
        ecs_enable(world, render, false);

        test_int(box_app_run(world), 0);

        const FrameTiming *t = ecs_singleton_get(world, FrameTiming);
        test_assert(fabs(t->interval.min - interval[k]) < 0.01);
        test_assert(fabs(t->interval.max - interval[k]) < 0.01);

        ecs_fini(world);
        ecs_os_api.now_ = now;
        ecs_os_api.sleep_ = sleep;
    }
}

void FramePacing_frames_report_snap_rate(void) {
    // testing/town_stutter
    int32_t frames[] = { 120, 900 };
    const char *source[] = { "(reported)", "(measured)" };
    for (int k = 0; k < 2; k ++) {
        ecs_world_t *world = pacing_window(120, 0, 0, frames[k]);
        ecs_entity_t s = box_terminal_new(world);
        bool error = false;
        char *out = box_terminal_run(world, s, "frames", &error);
        test_bool(error, false);
        test_assert(out != NULL);
        test_assert(strstr(out, "delta snapped to refreshes of 120.00 Hz") != NULL);
        test_assert(strstr(out, source[k]) != NULL);
        test_assert(strstr(out, "of 0.00 Hz") == NULL);
        ecs_os_free(out);
        pacing_window_fini(world);
    }
}

void FramePacing_frames_report_cap_paces(void) {
    // testing/town_stutter
    float fps[] = { 60, 0 };
    const char *expect[] = {
        "cap 60, the cap paces: 2 refresh(es) of 120 Hz per frame",
        "no cap, vsync paces: 1 refresh(es) of 120 Hz per frame" };
    for (int k = 0; k < 2; k ++) {
        ecs_world_t *world = pacing_window(120, 0, fps[k], 300);
        ecs_entity_t s = box_terminal_new(world);
        bool error = false;
        char *out = box_terminal_run(world, s, "frames", &error);
        test_bool(error, false);
        test_assert(out != NULL);
        test_assert(strstr(out, expect[k]) != NULL);
        test_assert(strstr(out, "0 dropped") != NULL);
        ecs_os_free(out);
        pacing_window_fini(world);
    }
}

void FramePacing_frame_start_waits_for_packet(void) {
    // testing/town_stutter
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, RenderSettings, {
        .in_flight = 2, .main_thread = false, .display_rate = 120 });
    ecs_singleton_set(world, App, { .frames = 40 });

    test_int(box_app_run(world), 0);
    box_render_sync(world);

    const RenderStatus *r = ecs_singleton_get(world, RenderStatus);
    test_bool(r->threaded, true);
    test_assert(r->waits > 0);
    test_int(r->stalls, 0);

    ecs_fini(world);
}
