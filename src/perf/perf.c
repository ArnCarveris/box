/**
 * @file perf/perf.c
 * @brief Registers the box.perf module: frame pacing, the orbit camera and the frame checks.
 */

#define BOX_PERF_IMPL
#include "perf.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/** Seconds before a deadline the pacer stops sleeping and spins */
#define BOX_PERF_SPIN (0.0002)

/** Fraction of the time left that one sleep of the pacer asks for.
 *
 * A process without a window gets up to 50% timer leeway on macOS, so a sleep of the whole
 * time left would overshoot the deadline by up to half of it.
 */
#define BOX_PERF_SLEEP (0.6)

/** Read the wall clock.
 *
 * @return Seconds since an arbitrary epoch
 */
static double box_perf_now(void)
{
    ecs_time_t t;
    ecs_os_get_time(&t);

    return (double)t.sec + (double)t.nanosec * 1e-9;
}

/** Wait until a deadline with ever shorter sleeps that cannot overshoot it, then spin.
 *
 * @param deadline Wall seconds to wait for
 * @return The wall clock after the wait
 */
static double box_perf_wait(
    double deadline)
{
    double now = box_perf_now();
    while (now < deadline) {
        double left = deadline - now;
        if (left > BOX_PERF_SPIN) {
            ecs_sleepf(left * BOX_PERF_SLEEP);
        }

        now = box_perf_now();
    }

    return now;
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

/** Make the frame rate of the App singleton the target of the run.
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
    perf->frames = run->frames > 0 ? run->frames : (int32_t)(fps * BOX_PERF_SECONDS);
    perf->warmup = run->warmup > 0 ? run->warmup : (int32_t)fps;
    perf->warmup = perf->warmup < BOX_PERF_MIN_WARMUP ? BOX_PERF_MIN_WARMUP : perf->warmup;
    perf->run = 0;
    perf->mark = 0;
    perf->bounds = false;
    ecs_vec_clear(&perf->intervals);
    ecs_vec_clear(&perf->work);
    if (!valid) {
        ecs_err("PerfRun.fps is %g, expected 30, 60, 90 or 120", (double)run->fps);
        if (run->quit) {
            ecs_quit(world);
        }
    } else {
        box_perf_app_fps(world, fps);
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
    float period = run->period > 0 ? run->period : BOX_PERF_PERIOD;
    float elevation = glm_rad(run->elevation != 0 ? run->elevation : BOX_PERF_ELEVATION);
    float angle = (float)(2.0 * GLM_PI * (box_perf_now() - perf->start) / period);
    float d = perf->distance;
    float ce = cosf(elevation);
    mat4 m = GLM_MAT4_IDENTITY_INIT;
    vec3 euler;
    glm_rotate_make(m, angle, GLM_YUP);
    glm_rotate_x(m, -elevation, m);
    glm_euler_angles(m, euler);
    ecs_set(world, camera, Position, {
        perf->center[0] + d * ce * sinf(angle),
        perf->center[1] + d * sinf(elevation),
        perf->center[2] + d * ce * cosf(angle)
    });
    ecs_set(world, camera, Rotation, { euler[0], euler[1], euler[2] });
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
    float fov = cam && cam->fov > 0 ? cam->fov : BOX_CAMERA_FOV;
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

    if (camera && perf->bounds) {
        box_perf_orbit(it->world, perf, run, camera);
    }

    box_perf_stall(run->stall);
done:
    return;
}

/** Start the measurement: remember the budget sleep and the stalls before it.
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

    status->errors = status->slow + status->fast;
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
    status->passed = status->errors == 0 && status->frame == perf->frames;
    status->state = PerfDone;
    perf->state = PerfDone;
    if (run->report && run->report[0]) {
        box_perf_report(world, run, status, &perf->intervals);
    }

    if (run->quit) {
        ecs_quit(world);
    }
}

/** Pace the frame to the target interval, measure it and advance the run.
 *
 * @param it The iterator of the system
 */
static void BoxPerfFrame(
    ecs_iter_t *it)
{
    box_perf_t *perf = it->ctx;
    const PerfRun *run = ecs_singleton_get(it->world, PerfRun);
    PerfStatus *status = ecs_singleton_get_mut(it->world, PerfStatus);
    double now;
    if (!run || !status || (perf->state != PerfWarmup && perf->state != PerfMeasure)) {
        goto done;
    }

    now = box_perf_now();
    if (perf->mark > 0) {
        double work = now - perf->mark;
        now = box_perf_wait(perf->mark + 1.0 / perf->fps);
        if (perf->state == PerfMeasure) {
            box_perf_count(perf, status, now - perf->mark, work);
        }
    }

    perf->mark = now;
    perf->run ++;
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

void BoxPerfImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxPerf);

    ECS_META_COMPONENT(world, PerfState);
    ECS_META_COMPONENT(world, PerfRun);
    ECS_META_COMPONENT(world, PerfStatus);
    box_owned_hooks(world, PerfRun, BOX_OWNED_STR(PerfRun, report));

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
        .entity = ecs_entity(world, { .name = "PerfFrame" }),
        .phase = EcsPostFrame,
        .run = BoxPerfFrame,
        .ctx = perf
    });
}
