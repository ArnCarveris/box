/**
 * @file perf/perf.c
 * @brief Registers the box.perf module: frame pacing, the orbit camera and the frame checks.
 */

#define BOX_PERF_IMPL
#include "perf.h"
#include "../render/render.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>

/** Seconds of a coarse sleep below which the pacer sleeps the rest in one go */
#define BOX_PERF_STEP (0.0001)

/** Seconds before a deadline at which the pacer counts it as reached, below the sleep resolution */
#define BOX_PERF_EPSILON (1e-6)

/** Seconds of CPU time the real-time schedule adds to the work for the wake ups of the wait */
#define BOX_PERF_WAKE (0.001)

/** Factor a sleep may take at most: a process without a window gets up to 50% timer leeway on
 * macOS, so a coarse sleep asks for the time left divided by it and cannot overshoot */
#define BOX_PERF_LEEWAY (1.5)

/** Seconds of wake up latency the pacer learns at most */
#define BOX_PERF_WAKE_MAX (0.002)

/** Seconds of wake up latency the pacer leaves to the deadline: waking this much early would
 * end a wait before its deadline, so the learned latency stops short of it */
#define BOX_PERF_WAKE_FLOOR (0.00001)

/** Weight of a new latency sample in the learned latency once a few samples were taken */
#define BOX_PERF_WAKE_GAIN (0.25)

/** Warmup frames a light bake may go without progress before a run measures without it */
#define BOX_PERF_BAKE_STALL (300)

/** Factor on the median work of the warmup frames that the real-time schedule asks for */
#define BOX_PERF_WORK_MARGIN (1.5)

double (*box_perf_clock)(void) = NULL;

void (*box_perf_sleep)(double seconds) = NULL;

/** Read the wall clock, or the clock a test installed.
 *
 * @return Seconds since an arbitrary epoch
 */
static double box_perf_now(void)
{
    ecs_time_t t;
    double result;
    if (box_perf_clock) {
        result = box_perf_clock();
    } else {
        ecs_os_get_time(&t);
        result = (double)t.sec + (double)t.nanosec * 1e-9;
    }

    return result;
}

/** Sleep on the wall clock, or with the sleep a test installed.
 *
 * @param seconds Seconds to sleep
 */
static void box_perf_sleep_for(
    double seconds)
{
    if (box_perf_sleep) {
        box_perf_sleep(seconds);
    } else {
        ecs_sleepf(seconds);
    }
}

/** Learn the wake up latency of the pacing thread from a sleep that woke later than planned.
 *
 * @param perf The state of the module
 * @param late Seconds the sleep woke after the time it asked for
 * @param jitter Whether the sample also updates the spread of the latency
 */
static void box_perf_learn(
    box_perf_t *perf,
    double late,
    bool jitter)
{
    double gain = 1.0 / (double)(perf->wake_samples + 1);
    gain = gain > BOX_PERF_WAKE_GAIN ? gain : BOX_PERF_WAKE_GAIN;
    late = late > BOX_PERF_WAKE_FLOOR ? late - BOX_PERF_WAKE_FLOOR : 0;
    if (jitter) {
        perf->wake_jitter += gain * (fabs(late - perf->wake) - perf->wake_jitter);
        perf->wake += gain * (late - perf->wake);
    } else {
        perf->wake += gain * late;
    }

    perf->wake = perf->wake < 0 ? 0 : perf->wake;
    perf->wake = perf->wake > BOX_PERF_WAKE_MAX ? BOX_PERF_WAKE_MAX : perf->wake;
    perf->wake_samples ++;
}

/** Seconds a wait may end before its deadline: the spread of the wake up latency times this,
 * since a sleep to make up for less would overshoot by a whole latency */
#define BOX_PERF_EARLY (3.0)

/** Wait until a deadline with OS sleeps, never busy waiting, waking early by the latency the
 * thread is learned to wake up with, so a late scheduler on any platform still lands on time.
 *
 * Coarse sleeps ask for the time left before the latency and its spread, divided by the timer
 * leeway, so none overshoots; a final sleep then ends a latency before the deadline. A wait
 * that still ends early by more than the spread of the latency sleeps again.
 *
 * @param perf The state of the module
 * @param deadline Wall seconds to wait for
 * @return The wall clock after the wait
 */
static double box_perf_wait(
    box_perf_t *perf,
    double deadline)
{
    double now = box_perf_now();
    double left, coarse, sleep, before, early;
    bool final;
    early = perf->wake_jitter * BOX_PERF_EARLY;
    while (deadline - now > (early > BOX_PERF_EPSILON ? early : BOX_PERF_EPSILON)) {
        left = deadline - now;
        coarse = (left - 2.0 * perf->wake - 2.0 * perf->wake_jitter) / BOX_PERF_LEEWAY;
        final = coarse <= BOX_PERF_STEP;
        sleep = left - perf->wake;
        sleep = final ? (sleep > BOX_PERF_EPSILON ? sleep : BOX_PERF_EPSILON) : coarse;
        before = now;
        box_perf_sleep_for(sleep);
        now = box_perf_now();
        if (final) {
            box_perf_learn(perf, now - before - sleep, true);
        } else if (now > deadline - perf->wake) {
            box_perf_learn(perf, now - (deadline - perf->wake), false);
        }

        early = perf->wake_jitter * BOX_PERF_EARLY;
    }

    return now;
}

/** Read the OS clock that flecs and the frame loop measure delta_time with.
 *
 * @return Seconds on the clock of ecs_os_get_time
 */
static double box_perf_os_now(void)
{
    ecs_time_t t;
    ecs_os_get_time(&t);

    return (double)t.sec + (double)t.nanosec * 1e-9;
}

/** Read the CPU time the process used on all of its threads.
 *
 * @return User plus system seconds, 0 where the platform does not report it
 */
static double box_perf_cpu(void)
{
    double result = 0;
#ifndef __EMSCRIPTEN__
    struct rusage usage;
    if (!getrusage(RUSAGE_SELF, &usage)) {
        result = (double)usage.ru_utime.tv_sec + (double)usage.ru_utime.tv_usec * 1e-6 +
            (double)usage.ru_stime.tv_sec + (double)usage.ru_stime.tv_usec * 1e-6;
    }
#endif

    return result;
}

/** Create the state of the module.
 *
 * @return The state, freed by box_perf_free
 */
static box_perf_t* box_perf_new(void)
{
    box_perf_t *result = ecs_os_calloc_t(box_perf_t);
    ecs_vec_init_t(NULL, &result->intervals, double, 0);
    ecs_vec_init_t(NULL, &result->work, double, 0);

    return result;
}

/** Free the state of the module.
 *
 * @param ctx The state
 */
static void box_perf_free(
    void *ctx)
{
    box_perf_t *perf = ctx;
    box_perf_priority_reset(&perf->priority);
    ecs_vec_fini_t(NULL, &perf->intervals, double);
    ecs_vec_fini_t(NULL, &perf->work, double);
    ecs_os_free(perf);
}

/** Check whether a frame rate is one a run can target.
 *
 * @param fps The frame rate
 * @return Whether it is 30, 60, 90 or 120
 */
static bool box_perf_valid_fps(
    float fps)
{
    return fps == 30 || fps == 60 || fps == 90 || fps == 120;
}

/** Make the frame rate of the App singleton the target of the run and measure delta_time.
 *
 * @param world The world
 * @param fps The target frame rate, 0 when a run ends
 */
static void box_perf_app_fps(
    ecs_world_t *world,
    float fps)
{
    App *app = ecs_singleton_get_mut(world, App);
    bool realtime = fps > 0;
    if (app && ((fps > 0 && app->fps != fps) || app->realtime != realtime)) {
        app->fps = fps > 0 ? fps : app->fps;
        app->realtime = realtime;
        ecs_singleton_modified(world, App);
    }
}

/** Present to a simulated vsync display for the run, or stop simulating one.
 *
 * @param world The world
 * @param refresh Refresh rate in Hz, 0 for none
 */
static void box_perf_display(
    ecs_world_t *world,
    float refresh)
{
    RenderSettings *settings = ecs_singleton_get_mut(world, RenderSettings);
    if (settings && settings->display_rate != refresh) {
        settings->display_rate = refresh;
        ecs_singleton_modified(world, RenderSettings);
    }
}

/** Hold the keys of a fly run, forward and turning left, or release every key.
 *
 * @param world The world
 * @param perf The state of the module
 * @param fly Whether the run flies
 */
static void box_perf_fly_keys(
    ecs_world_t *world,
    box_perf_t *perf,
    bool fly)
{
    box_input_reset(world);
    perf->boost = false;
    perf->boost_time = box_perf_now();
    if (fly) {
        box_input_key(world, KeyW, true);
        box_input_key(world, KeyLeft, true);
    }
}

/** Decide whether a simulated display shows the frames of a run in a cadence: the target does
 * not divide the refresh rate, so frames alternate between refresh counts.
 *
 * @param refresh Refresh rate of the simulated display, 0 for none
 * @param fps Target frame rate
 * @return Whether the frames alternate
 */
static bool box_perf_cadence(
    float refresh,
    float fps)
{
    bool result = false;
    double whole;
    if (refresh > 0 && fps > 0 && (double)fps < (double)refresh - BOX_RENDER_REFRESH_SLACK) {
        whole = floor((double)refresh / (double)fps + 0.5);
        result = fabs(whole * (double)fps - (double)refresh) > BOX_RENDER_REFRESH_SLACK;
    }

    return result;
}

/** Sum the errors of a run. In a cadence the frame intervals and the deltas against them are
 * uneven by design and only reported: what counts is that every delta matches the time the
 * display showed the frame before it, and that no frame missed its refresh.
 *
 * @param status The status to update
 */
static void box_perf_errors(
    PerfStatus *status)
{
    const PerfChecks *checks = &status->checks;
    status->errors = checks->display_delta_errors + checks->present_errors;
    if (!checks->cadence) {
        status->errors += status->slow + status->fast + checks->delta_errors;
    }
}

/** Start a run from new settings, or mark it invalid.
 *
 * @param it The iterator of the observer
 */
static void BoxPerfOnSet(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_perf_t *perf = it->ctx;
    const PerfRun *run = ecs_field(it, PerfRun, 0);
    bool valid = box_perf_valid_fps(run->fps);
    float fps = valid ? run->fps : 0;
    PerfStatus status = {
        .state = valid ? PerfWarmup : PerfInvalid,
        .target_ms = valid ? 1000.0 / fps : 0
    };

    perf->state = status.state;
    perf->fps = fps;
    perf->frames = run->frames > 0 ? run->frames : 0;
    perf->warmup = run->warmup < BOX_PERF_MIN_WARMUP ? BOX_PERF_MIN_WARMUP : run->warmup;
    status.warmup = perf->warmup;
    status.period = run->period;
    perf->run = 0;
    perf->mark = 0;
    perf->begin = 0;
    perf->os_begin = 0;
    perf->has_last = false;
    perf->wake = 0;
    perf->wake_jitter = 0;
    perf->wake_samples = 0;
    status.checks.fly = run->fly;
    status.checks.refresh = run->refresh;
    status.checks.cadence = box_perf_cadence(run->refresh, fps);
    status.checks.delta_tolerance_ms = BOX_FRAME_TOLERANCE_MS;
    perf->bake_left = 0;
    perf->bake_stalled = 0;
    perf->priority.refused = false;
    box_perf_priority_reset(&perf->priority);
    perf->bounds = false;
    perf->cull_frame = -1;
    ecs_os_memset(perf->cull_last, 0, ECS_SIZEOF(perf->cull_last));
    ecs_os_memset(perf->cull_sum, 0, ECS_SIZEOF(perf->cull_sum));
    ecs_vec_clear(&perf->intervals);
    ecs_vec_clear(&perf->work);
    if (!valid) {
        ecs_err("PerfRun.fps is %g, expected 30, 60, 90 or 120", (double)run->fps);
        box_perf_app_fps(world, 0);
        box_perf_display(world, 0);
        if (run->quit) {
            ecs_quit(world);
        }
    } else {
        box_perf_app_fps(world, fps);
        box_perf_display(world, run->refresh);
        if (run->fly) {
            box_perf_fly_keys(world, perf, true);
        }
    }

    ecs_singleton_set_ptr(world, PerfStatus, &status);
}

/** Find the camera a run orbits.
 *
 * @param world The world
 * @param run The run settings
 * @return The camera, 0 when there is none
 */
static ecs_entity_t box_perf_camera(
    ecs_world_t *world,
    const PerfRun *run)
{
    ecs_entity_t result = run->camera;
    if (!result) {
        ecs_iter_t it = ecs_each(world, Camera);
        if (ecs_each_next(&it)) {
            result = it.entities[0];
            ecs_iter_fini(&it);
        }
    }

    return result;
}

/** Busy wait, to make a frame artificially slow.
 *
 * @param ms Milliseconds to wait
 */
static void box_perf_stall(
    float ms)
{
    double end = box_perf_now() + (double)ms * 0.001;
    while (ms > 0 && box_perf_now() < end) {
    }
}

/** Place the camera on its orbit around the bounds, looking at the center.
 *
 * @param world The world
 * @param perf The state of the module
 * @param run The run settings
 * @param camera The camera
 */
static void box_perf_orbit(
    ecs_world_t *world,
    const box_perf_t *perf,
    const PerfRun *run,
    ecs_entity_t camera)
{
    float period = run->period;
    float elevation = glm_rad(run->elevation);
    float angle = period > 0
        ? (float)(2.0 * GLM_PI * (box_perf_now() - perf->start) / period)
        : 0.0f;
    float d = perf->distance;
    float ce = cosf(elevation);
    ecs_set(world, camera, Position, {
        perf->center[0] + d * ce * sinf(angle),
        perf->center[1] + d * sinf(elevation),
        perf->center[2] + d * ce * cosf(angle)
    });
    ecs_set(world, camera, Rotation, { -elevation, angle, 0 });
}

/** Measure the bounds of every shape and the camera distance that fits them.
 *
 * @param world The world
 * @param perf The state of the module
 * @param camera The camera whose field of view the distance fits, 0 for none
 */
static void box_perf_measure_bounds(
    ecs_world_t *world,
    box_perf_t *perf,
    ecs_entity_t camera)
{
    const Camera *cam = camera ? ecs_get(world, camera, Camera) : NULL;
    float fov = cam ? cam->fov : BOX_CAMERA_FOV;
    vec3 min, max;
    int32_t shapes = box_perf_bounds(world, min, max);
    PerfStatus *status;
    if (!shapes) {
        glm_vec3_zero(min);
        glm_vec3_zero(max);
    }

    glm_vec3_center(min, max, perf->center);
    perf->radius = glm_vec3_distance(min, max) * 0.5f;
    perf->radius = perf->radius > 0 ? perf->radius : 1.0f;
    if (cam && cam->ortho) {
        perf->distance = perf->radius * 2.0f;
    } else {
        perf->distance = perf->radius / sinf(glm_rad(fov) * 0.5f);
    }

    perf->bounds = true;
    perf->start = box_perf_now();
    status = ecs_singleton_get_mut(world, PerfStatus);
    if (status) {
        status->shapes = shapes;
        glm_vec3_copy(perf->center, status->center);
        status->radius = perf->radius;
        status->distance = perf->distance;
    }
}

/** Measure the bounds once the first frame placed the shapes, then orbit the camera and add
 * the artificial frame cost.
 *
 * @param it The iterator of the system
 */
static void BoxPerfOrbit(
    ecs_iter_t *it)
{
    box_perf_t *perf = it->ctx;
    const PerfRun *run = ecs_singleton_get(it->world, PerfRun);
    ecs_entity_t camera;
    if (!run || (perf->state != PerfWarmup && perf->state != PerfMeasure)) {
        goto done;
    }

    camera = box_perf_camera(it->world, run);
    if (!perf->bounds && perf->run > 0) {
        box_perf_measure_bounds(it->world, perf, camera);
    }

    if (camera && perf->bounds && !run->fly) {
        box_perf_orbit(it->world, perf, run, camera);
    }

    if (run->fly && box_perf_now() - perf->boost_time >= BOX_PERF_BOOST_PERIOD) {
        perf->boost = !perf->boost;
        perf->boost_time = box_perf_now();
        box_input_key(it->world, KeyShiftLeft, perf->boost);
    }

    box_perf_stall(run->stall);
done:
    return;
}

/** Give the pacing thread a real-time schedule of one frame per target interval, unless the
 * platform refused it before in this run.
 *
 * @param perf The state of the module
 * @param computation Seconds of CPU time a frame needs
 */
static void box_perf_schedule(
    box_perf_t *perf,
    double computation)
{
    if (!perf->priority.refused) {
        perf->priority.refused = !box_perf_priority_set(
            &perf->priority, 1.0 / perf->fps, computation);
    }
}

/** Start the measurement: fit the schedule to the work of the warmup frames and remember the
 * budget sleep and the stalls before it.
 *
 * @param world The world
 * @param perf The state of the module
 */
static void box_perf_begin(
    ecs_world_t *world,
    box_perf_t *perf)
{
    const AppStatus *app = ecs_singleton_get(world, AppStatus);
    const RenderStatus *render = ecs_singleton_get(world, RenderStatus);
    box_perf_stats_t warmup;
    box_perf_stats(&perf->work, &warmup);
    ecs_vec_clear(&perf->work);
    if (warmup.p50 > 0) {
        box_perf_schedule(perf, warmup.p50 * 0.001 * BOX_PERF_WORK_MARGIN + BOX_PERF_WAKE);
    }

    perf->slept = app ? app->slept : 0;
    perf->stalls = render ? render->stalls : 0;
    perf->presented = render ? render->presented : 0;
    perf->presents_bad = render ? render->presents_late + render->presents_early : 0;
    perf->mismatches = render ? render->delta_mismatches : 0;
    perf->cpu = box_perf_cpu();
    perf->wall = box_perf_os_now();
    perf->measure = perf->mark;
    perf->state = PerfMeasure;
}

/** Count one measured frame interval.
 *
 * @param perf The state of the module
 * @param status The status to update
 * @param interval Seconds since the previous frame
 * @param work Seconds the frame worked before the pacer waited
 */
static void box_perf_count(
    box_perf_t *perf,
    PerfStatus *status,
    double interval,
    double work)
{
    double ms = interval * 1000.0;
    double target = status->target_ms;
    ecs_vec_append_t(NULL, &perf->intervals, double)[0] = ms;
    ecs_vec_append_t(NULL, &perf->work, double)[0] = work * 1000.0;
    status->frame ++;
    if (ms > target * (1.0 + BOX_PERF_TOLERANCE)) {
        status->slow ++;
    } else if (ms < target * (1.0 - BOX_PERF_TOLERANCE)) {
        status->fast ++;
    }

    box_perf_errors(status);
}

/** Add the distance the camera moved since the previous measured frame.
 *
 * @param world The world
 * @param perf The state of the module
 * @param status The status to update
 * @param camera The camera
 */
static void box_perf_travel(
    ecs_world_t *world,
    box_perf_t *perf,
    PerfStatus *status,
    ecs_entity_t camera)
{
    const Position *p = camera ? ecs_get(world, camera, Position) : NULL;
    vec3 pos;
    if (!p) {
        goto done;
    }

    pos[0] = p->x;
    pos[1] = p->y;
    pos[2] = p->z;
    if (perf->has_last) {
        status->checks.travelled += glm_vec3_distance(pos, perf->last);
    }

    glm_vec3_copy(pos, perf->last);
    perf->has_last = true;
done:
    return;
}

/** Count the uneven presents of the simulated display since the measurement started.
 *
 * @param world The world
 * @param perf The state of the module
 * @param status The status to update
 */
static void box_perf_presents(
    ecs_world_t *world,
    const box_perf_t *perf,
    PerfStatus *status)
{
    const RenderStatus *render = ecs_singleton_get(world, RenderStatus);
    if (render && status->checks.refresh > 0) {
        status->checks.present_errors = render->presents_late + render->presents_early -
            perf->presents_bad;
        status->checks.display_delta_errors = render->delta_mismatches - perf->mismatches;
    }
}

/** Check whether a camera is the orbiting camera or one of its descendants.
 *
 * @param world The world
 * @param e The camera of a view
 * @param camera The orbiting camera
 * @return Whether the view sees through the orbit
 */
static bool box_perf_follows(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t camera)
{
    while (e && e != camera && ecs_is_alive(world, e)) {
        e = ecs_get_parent(world, e);
    }

    return camera != 0 && e == camera;
}

/** Add one culling sample to a range.
 *
 * @param range The range to update
 * @param value The sampled count
 * @param sum The sum of every sample including this one
 * @param samples The number of samples including this one
 */
static void box_perf_range(
    PerfRange *range,
    int32_t value,
    double sum,
    int32_t samples)
{
    range->min = samples == 1 || value < range->min ? value : range->min;
    range->max = samples == 1 || value > range->max ? value : range->max;
    range->mean = sum / (double)samples;
}

/** Count one culling sample of the orbiting views.
 *
 * @param perf The state of the module
 * @param status The status whose ranges are updated
 * @param counts The visible, frustum culled and occlusion culled instances
 */
static void box_perf_cull_count(
    box_perf_t *perf,
    PerfStatus *status,
    const int32_t counts[3])
{
    PerfRange *ranges[3] = {
        &status->visible, &status->frustum_culled, &status->occlusion_culled
    };
    bool changed = false;
    int32_t i;
    status->cull_samples ++;
    for (i = 0; i < 3; i ++) {
        changed |= status->cull_samples > 1 && counts[i] != perf->cull_last[i];
        perf->cull_last[i] = counts[i];
        perf->cull_sum[i] += counts[i];
        box_perf_range(ranges[i], counts[i], perf->cull_sum[i], status->cull_samples);
    }

    status->cull_changes += changed;
}

/** Count the views that render the orbit and sample their culling when the GPU published a
 * new result.
 *
 * @param world The world
 * @param perf The state of the module
 * @param status The status to update
 * @param camera The orbiting camera
 */
static void box_perf_sample_cull(
    ecs_world_t *world,
    box_perf_t *perf,
    PerfStatus *status,
    ecs_entity_t camera)
{
    int32_t counts[3] = {0, 0, 0};
    int64_t frame = -1;
    int32_t views = 0;
    ecs_iter_t it = ecs_each(world, View);
    while (ecs_each_next(&it)) {
        const View *v = ecs_field(&it, View, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const CullStatus *cull;
            if (!box_perf_follows(world, v[i].camera, camera)) {
                continue;
            }

            views ++;
            cull = ecs_get(world, it.entities[i], CullStatus);
            if (cull) {
                counts[0] += cull->visible;
                counts[1] += cull->frustum_culled;
                counts[2] += cull->occlusion_culled;
                frame = cull->frame > frame ? cull->frame : frame;
            }
        }
    }

    status->views = views;
    if (frame >= 0 && frame != perf->cull_frame) {
        perf->cull_frame = frame;
        box_perf_cull_count(perf, status, counts);
    }
}

/** Complete the CPU usage and the presents of a run.
 *
 * @param world The world
 * @param perf The state of the module
 * @param status The status to complete
 */
static void box_perf_end_usage(
    ecs_world_t *world,
    const box_perf_t *perf,
    PerfStatus *status)
{
    const RenderStatus *render = ecs_singleton_get(world, RenderStatus);
    double wall = box_perf_os_now() - perf->wall;
    double cpu = box_perf_cpu() - perf->cpu;
    int32_t presents = render ? render->presented - perf->presented : 0;
    status->checks.cpu_percent = wall > 0 ? cpu / wall * 100.0 : 0;
    status->checks.cpu_ms = status->frame > 0 ? cpu * 1000.0 / (double)status->frame : 0;
    status->checks.present_mean_ms = presents > 0 ? wall * 1000.0 / (double)presents : 0;
    box_perf_presents(world, perf, status);
    box_perf_errors(status);
}

/** Complete the run: write the statistics and the report and quit when asked.
 *
 * @param world The world
 * @param perf The state of the module
 * @param run The run settings
 * @param status The status to complete
 */
static void box_perf_end(
    ecs_world_t *world,
    box_perf_t *perf,
    const PerfRun *run,
    PerfStatus *status)
{
    const AppStatus *app = ecs_singleton_get(world, AppStatus);
    const RenderStatus *render = ecs_singleton_get(world, RenderStatus);
    box_perf_stats_t frames, work;
    box_perf_stats(&perf->intervals, &frames);
    box_perf_stats(&perf->work, &work);
    status->min_ms = frames.min;
    status->mean_ms = frames.mean;
    status->p50_ms = frames.p50;
    status->p99_ms = frames.p99;
    status->max_ms = frames.max;
    status->work_mean_ms = work.mean;
    status->work_p99_ms = work.p99;
    status->work_max_ms = work.max;
    status->slept_ms = app ? (app->slept - perf->slept) * 1000.0 : 0;
    status->stalls = render ? render->stalls - perf->stalls : 0;
    status->revolutions = run->period > 0 && !run->fly
        ? (float)((perf->mark - perf->measure) / run->period)
        : 0.0f;
    box_perf_end_usage(world, perf, status);
    status->passed = status->errors == 0 && status->frame == perf->frames;
    status->state = PerfDone;
    perf->state = PerfDone;
    box_perf_priority_reset(&perf->priority);
    box_perf_app_fps(world, 0);
    box_perf_display(world, 0);
    if (run->fly) {
        box_perf_fly_keys(world, perf, false);
    }

    if (run->report && run->report[0]) {
        box_perf_report(world, run, status, &perf->intervals);
    }

    if (run->quit) {
        ecs_quit(world);
    }
}

/** Check the delta_time of a measured frame against the wall time since the previous frame
 * started, both on the OS clock the frame loop measures with.
 *
 * @param it The iterator of the system
 */
static void BoxPerfBegin(
    ecs_iter_t *it)
{
    box_perf_t *perf = it->ctx;
    PerfStatus *status = ecs_singleton_get_mut(it->world, PerfStatus);
    double os = box_perf_os_now();
    double interval, delta, diff;
    PerfChecks *checks;
    if (!status || (perf->state != PerfWarmup && perf->state != PerfMeasure)) {
        perf->os_begin = 0;
        goto done;
    }

    perf->begin = box_perf_now();
    if (perf->state == PerfMeasure && perf->os_begin > 0) {
        interval = (os - perf->os_begin) * 1000.0;
        delta = (double)ecs_get_world_info(it->world)->delta_time_raw * 1000.0;
        diff = fabs(delta - interval);
        checks = &status->checks;
        checks->delta_max_ms = diff > checks->delta_max_ms ? diff : checks->delta_max_ms;
        checks->delta_errors += diff > BOX_FRAME_TOLERANCE_MS;
    }

    perf->os_begin = os;
done:
    return;
}

/** Measure a frame of the run: count its interval and its culling and the presents.
 *
 * @param world The world
 * @param perf The state of the module
 * @param run The run settings
 * @param status The status to update
 * @param now The clock at the end of the frame
 * @param work Seconds the frame worked
 */
static void box_perf_measure(
    ecs_world_t *world,
    box_perf_t *perf,
    const PerfRun *run,
    PerfStatus *status,
    double now,
    double work)
{
    ecs_entity_t camera = box_perf_camera(world, run);
    box_perf_presents(world, perf, status);
    box_perf_count(perf, status, now - perf->mark, work);
    box_perf_sample_cull(world, perf, status, camera);
    box_perf_travel(world, perf, status, camera);
}

/** Check whether a light bake that can progress is still baking, which a run waits for.
 *
 * A bake that leaves the same probes for BOX_PERF_BAKE_STALL frames is given up on, with an
 * error, and the run measures without it.
 *
 * @param world The world
 * @param perf The state of the module
 * @return Whether the warmup starts again
 */
static bool box_perf_baking(
    ecs_world_t *world,
    box_perf_t *perf)
{
    int32_t left = box_render_bake_pending(world);
    bool result = false;
    if (!left || perf->bake_stalled >= BOX_PERF_BAKE_STALL) {
        goto done;
    }

    if (left != perf->bake_left) {
        perf->bake_left = left;
        perf->bake_stalled = 0;
    } else if (++ perf->bake_stalled == BOX_PERF_BAKE_STALL) {
        ecs_err("PerfRun: the light bake left %d probes for %d frames, measuring without it",
            left, BOX_PERF_BAKE_STALL);
        goto done;
    }

    result = true;
done:
    return result;
}

/** Pace the frame to the target interval, unless a simulated display paces it like a window,
 * measure it and advance the run.
 *
 * @param it The iterator of the system
 */
static void BoxPerfFrame(
    ecs_iter_t *it)
{
    box_perf_t *perf = it->ctx;
    const PerfRun *run = ecs_singleton_get(it->world, PerfRun);
    PerfStatus *status = ecs_singleton_get_mut(it->world, PerfStatus);
    double now, work;
    if (!run || !status || (perf->state != PerfWarmup && perf->state != PerfMeasure)) {
        box_perf_priority_reset(&perf->priority);
        goto done;
    }

    now = box_perf_now();
    if (perf->mark > 0) {
        work = now - (perf->begin > perf->mark ? perf->begin : perf->mark);
        if (!perf->priority.active) {
            box_perf_schedule(perf, 0.5 / perf->fps);
        }

        if (run->refresh <= 0) {
            now = box_perf_wait(perf, perf->mark + 1.0 / perf->fps);
        }

        if (perf->state == PerfMeasure) {
            box_perf_measure(it->world, perf, run, status, now, work);
        } else {
            ecs_vec_append_t(NULL, &perf->work, double)[0] = work * 1000.0;
        }
    }

    perf->mark = now;
    perf->run ++;
    if (perf->state == PerfWarmup && box_perf_baking(it->world, perf)) {
        perf->run = 0;
        ecs_vec_clear(&perf->work);
    }

    if (perf->state == PerfWarmup && perf->run >= perf->warmup) {
        box_perf_begin(it->world, perf);
        status->state = PerfMeasure;
    } else if (perf->state == PerfMeasure && status->frame >= perf->frames) {
        box_perf_end(it->world, perf, run, status);
    }

    ecs_singleton_modified(it->world, PerfStatus);
done:
    return;
}

/** Construct PerfRun values with the documented defaults.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_perf_run_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    PerfRun *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (PerfRun){
            .fps = BOX_PERF_FPS,
            .frames = (int32_t)(BOX_PERF_FPS * BOX_PERF_SECONDS),
            .warmup = (int32_t)BOX_PERF_FPS,
            .period = BOX_PERF_PERIOD,
            .elevation = BOX_PERF_ELEVATION
        };
    }
}

void BoxPerfImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxPerf);

    ECS_META_COMPONENT(world, PerfState);
    ECS_META_COMPONENT(world, PerfRange);
    ECS_META_COMPONENT(world, PerfChecks);
    ECS_META_COMPONENT(world, PerfRun);
    ECS_META_COMPONENT(world, PerfStatus);
    box_owned_hooks(world, PerfRun, BOX_OWNED_STR(PerfRun, report));
    ecs_set_hooks(world, PerfRun, { .ctor = box_perf_run_ctor });

    ecs_singleton_set(world, PerfStatus, { .state = PerfIdle });

    box_perf_t *perf = box_perf_new();

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "PerfOnSet" }),
        .query.terms = {{ .id = ecs_id(PerfRun) }},
        .events = { EcsOnSet },
        .callback = BoxPerfOnSet,
        .ctx = perf,
        .ctx_free = box_perf_free
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "PerfOrbit" }),
        .phase = EcsOnUpdate,
        .run = BoxPerfOrbit,
        .ctx = perf,
        .immediate = true
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "PerfBegin" }),
        .phase = EcsPreFrame,
        .run = BoxPerfBegin,
        .ctx = perf
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "PerfFrame" }),
        .phase = EcsPostFrame,
        .run = BoxPerfFrame,
        .ctx = perf
    });
}
