/**
 * @file perf/perf.c
 * @brief Registers the box.perf module: the frame rate cap, the orbit camera and the frame checks.
 */

#define BOX_PERF_IMPL
#include "perf.h"
#include "../render/render.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>

/** Warmup frames a light bake may go without progress before a run measures without it */
#define BOX_PERF_BAKE_STALL (300)

double (*box_perf_clock)(void) = NULL;

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
    ecs_vec_fini_t(NULL, &perf->intervals, double);
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

/** Make the target of the run the frame rate of the App singleton, so flecs caps the frames.
 *
 * @param world The world
 * @param fps The target frame rate
 */
static void box_perf_app_fps(
    ecs_world_t *world,
    float fps)
{
    App *app = ecs_singleton_get_mut(world, App);
    if (app && app->fps != fps) {
        app->fps = fps;
        ecs_singleton_modified(world, App);
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

/** Sum the errors of a run: the slow and fast frame intervals and the deltas that differ from
 * the wall time.
 *
 * @param status The status to update
 */
static void box_perf_errors(
    PerfStatus *status)
{
    status->errors = status->slow + status->fast + status->checks.delta_errors;
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
    status.checks.fly = run->fly;
    status.checks.delta_tolerance_ms = BOX_FRAME_TOLERANCE_MS;
    perf->bake_left = 0;
    perf->bake_stalled = 0;
    perf->bounds = false;
    perf->cull_frame = -1;
    ecs_os_memset(perf->cull_last, 0, ECS_SIZEOF(perf->cull_last));
    ecs_os_memset(perf->cull_sum, 0, ECS_SIZEOF(perf->cull_sum));
    ecs_vec_clear(&perf->intervals);
    if (!valid) {
        ecs_err("PerfRun.fps is %g, expected 30, 60, 90 or 120", (double)run->fps);
        if (run->quit) {
            ecs_quit(world);
        }
    } else {
        box_perf_app_fps(world, fps);
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

/** Start the measurement: remember the budget sleep, the stalls and the presents before it.
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
    perf->slept = app ? app->slept : 0;
    perf->stalls = render ? render->stalls : 0;
    perf->presented = render ? render->presented : 0;
    perf->cpu = box_perf_cpu();
    perf->wall = box_perf_os_now();
    perf->measure = perf->mark;
    perf->state = PerfMeasure;
}

/** Count one measured frame interval.
 *
 * @param perf The state of the module
 * @param status The status to update
 * @param interval Seconds since the previous frame started
 */
static void box_perf_count(
    box_perf_t *perf,
    PerfStatus *status,
    double interval)
{
    double ms = interval * 1000.0;
    double target = status->target_ms;
    ecs_vec_append_t(NULL, &perf->intervals, double)[0] = ms;
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
    box_perf_stats_t frames;
    box_perf_stats(&perf->intervals, &frames);
    status->min_ms = frames.min;
    status->mean_ms = frames.mean;
    status->p50_ms = frames.p50;
    status->p99_ms = frames.p99;
    status->max_ms = frames.max;
    status->slept_ms = app ? (app->slept - perf->slept) * 1000.0 : 0;
    status->stalls = render ? render->stalls - perf->stalls : 0;
    status->revolutions = run->period > 0 && !run->fly
        ? (float)((perf->mark - perf->measure) / run->period)
        : 0.0f;
    box_perf_end_usage(world, perf, status);
    status->passed = status->errors == 0 && status->frame == perf->frames;
    status->state = PerfDone;
    perf->state = PerfDone;
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

/** Measure a frame of the run: count its interval and its culling.
 *
 * @param world The world
 * @param perf The state of the module
 * @param run The run settings
 * @param status The status to update
 * @param now The clock at the start of the frame
 */
static void box_perf_measure(
    ecs_world_t *world,
    box_perf_t *perf,
    const PerfRun *run,
    PerfStatus *status,
    double now)
{
    ecs_entity_t camera = box_perf_camera(world, run);
    box_perf_count(perf, status, now - perf->mark);
    box_perf_sample_cull(world, perf, status, camera);
    box_perf_travel(world, perf, status, camera);
}

/** Check whether a light or reflection bake that can progress is still baking, which a run
 * waits for.
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
    int32_t left = box_render_bake_pending(world) + box_render_reflect_pending(world);
    bool result = false;
    if (!left || perf->bake_stalled >= BOX_PERF_BAKE_STALL) {
        goto done;
    }

    if (left != perf->bake_left) {
        perf->bake_left = left;
        perf->bake_stalled = 0;
    } else if (++ perf->bake_stalled == BOX_PERF_BAKE_STALL) {
        ecs_err("PerfRun: the bakes left %d probes for %d frames, measuring without them",
            left, BOX_PERF_BAKE_STALL);
        goto done;
    }

    result = true;
done:
    return result;
}

/** Measure the frame, the interval between its start and the start of the previous frame, which
 * flecs caps at App.fps, and advance the run.
 *
 * @param it The iterator of the system
 */
static void BoxPerfFrame(
    ecs_iter_t *it)
{
    box_perf_t *perf = it->ctx;
    const PerfRun *run = ecs_singleton_get(it->world, PerfRun);
    PerfStatus *status = ecs_singleton_get_mut(it->world, PerfStatus);
    double now = perf->begin;
    if (!run || !status || (perf->state != PerfWarmup && perf->state != PerfMeasure)) {
        goto done;
    }

    if (perf->mark > 0 && now > 0 && perf->state == PerfMeasure) {
        box_perf_measure(it->world, perf, run, status, now);
    }

    perf->mark = now;
    perf->run ++;
    if (perf->state == PerfWarmup && box_perf_baking(it->world, perf)) {
        perf->run = 0;
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
