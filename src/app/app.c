/**
 * @file app/app.c
 * @brief Registers the box.app module, its stop conditions, the CPU budget and the run loop.
 */

#define BOX_APP_IMPL
#include "app.h"
#include "watch.h"
#include "../render/render.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/** Samples of one series of FrameTiming, the most recent BOX_FRAME_WINDOW frames */
typedef struct box_app_series_t {
    double values[BOX_FRAME_WINDOW];    /**< Samples in milliseconds, a ring */
    int32_t count;                      /**< Samples stored, at most BOX_FRAME_WINDOW */
    int32_t next;                       /**< Slot the next sample goes to */
} box_app_series_t;

/** Clock of the frame systems: the CPU budget and the frame timing */
typedef struct box_app_clock_t {
    double start;               /**< Wall seconds when the module was imported */
    double cpu;                 /**< Process CPU seconds at the end of the previous frame */
    double wall;                /**< Wall seconds at the end of the previous frame */
    double pace;                /**< Deadline the CPU budget waited to at the end of the
                                 *   previous frame, its wall time when it did not wait */
    ecs_world_t *world;         /**< World of the frame loop, for the browser's frame callback */
    double begin;               /**< Wall seconds at the start of the previous frame, 0 none */
    int32_t presented;          /**< RenderStatus.presented at the previous frame */
    box_app_series_t delta;     /**< Deltas of the recent frames */
    box_app_series_t interval;  /**< Intervals of the recent frames */
    box_app_series_t error;     /**< Differences of delta and interval of the recent frames */
    box_app_series_t present;   /**< Present intervals of the recent frames */
    box_app_series_t late;      /**< 1 for a recent frame with a late delta, else 0 */
    box_app_series_t dropped;   /**< 1 for a recent dropped frame, else 0 */
} box_app_clock_t;

/** Name of the system whose context is the clock of the frame loop */
#define BOX_APP_TIMING_SYSTEM "box.app.FrameTimingUpdate"

/** Create the clock of the frame system.
 *
 * @return The clock, freed by box_app_clock_free
 */
static box_app_clock_t* box_app_clock_new(void)
{
    box_app_clock_t *result = ecs_os_calloc_t(box_app_clock_t);
    result->start = box_app_wall_time();

    return result;
}

/** Free the clock of the frame system.
 *
 * @param ctx The clock
 */
static void box_app_clock_free(
    void *ctx)
{
    ecs_os_free(ctx);
}

/** Make App.fps the target fps of flecs, which caps the frames; 0 for no cap. The browser keeps
 * no cap: the animation frames pace it and a flecs sleep would block its main thread.
 *
 * @param world The world
 * @param app The App singleton
 */
static void box_app_target_fps(
    ecs_world_t *world,
    const App *app)
{
#ifdef __EMSCRIPTEN__
    float fps = 0;
    (void)app;
#else
    float fps = app->fps > 0 ? app->fps : 0;
#endif
    ecs_set_target_fps(world, fps);
}

/** Read the delta time the next frame advances from the App singleton, so a change through the
 * command port or a script applies on the next frame.
 *
 * @param world The world
 * @return App.delta_time, 0 when flecs measures the delta
 */
static float box_app_frame_delta(
    const ecs_world_t *world)
{
    const App *app = ecs_singleton_get(world, App);

    return app ? app->delta_time : 0;
}

/** Make a new App true: flecs caps the frames at App.fps and a headless process gets its
 * temporary folder.
 *
 * @param it The iterator of the observer
 */
static void BoxAppOnSet(
    ecs_iter_t *it)
{
    ecs_world_t *world = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(it->world));
    const App *app = ecs_field(it, App, 0);
    AppStatus *status = ecs_singleton_get_mut(world, AppStatus);
    box_app_target_fps(world, app);
    if (!status) {
        goto done;
    }

    if (app->headless && !status->tmp_dir) {
        status->tmp_dir = box_app_tmp_create(world);
    }

done:
    return;
}

/** Count the frame, start the clock on the first frame, stop the world when App says so and
 * sleep when a headless process used more than its CPU budget since the previous frame.
 *
 * The sleep waits to a deadline that advances from the previous one, as the virtual
 * scheduling form of a leaky bucket (GCRA) does: a wait that wakes late shortens the next one,
 * so timer leeway does not lower the CPU share below the budget.
 *
 * A frame under its budget starts the next interval at its own wall time: idle earns no credit.
 *
 * @param it The iterator of the system
 */
static void BoxAppFrame(
    ecs_iter_t *it)
{
    box_app_clock_t *clock = it->ctx;
    const App *app = ecs_singleton_get(it->world, App);
    AppStatus *status = ecs_singleton_get_mut(it->world, AppStatus);
    double cpu = box_app_cpu_time();
    double wall = box_app_wall_time();
    double pace = wall;
    if (!app || !status) {
        goto done;
    }

    if (clock->wall == 0) {
        clock->start = wall;
    }

    status->frame ++;
    status->elapsed = wall - clock->start;
    if (app->frames > 0 && status->frame >= app->frames) {
        ecs_quit(it->world);
    }

    if (app->timeout > 0 && status->elapsed >= app->timeout) {
        ecs_quit(it->world);
    }

    if (app->headless && !app->unpaced && clock->wall > 0) {
        double sleep = box_app_pace(cpu - clock->cpu, wall - clock->pace, app->cpu_budget);
        if (sleep > 0) {
            double now = box_wait_until(wall + sleep);
            status->slept += now - wall;
            pace = wall + sleep;
            wall = now;
        }
    }

done:
    clock->cpu = cpu;
    clock->wall = wall;
    clock->pace = pace;
}

/** Add a sample to a series of FrameTiming.
 *
 * @param series The series
 * @param value The sample
 */
static void box_app_series_add(
    box_app_series_t *series,
    double value)
{
    series->values[series->next] = value;
    series->next = (series->next + 1) % BOX_FRAME_WINDOW;
    if (series->count < BOX_FRAME_WINDOW) {
        series->count ++;
    }
}

/** Compute the range of a series.
 *
 * @param series The series
 * @param range The range to fill, zero without samples
 * @return The sum of the samples
 */
static double box_app_series_range(
    const box_app_series_t *series,
    FrameRange *range)
{
    double sum = 0;
    int32_t i;
    *range = (FrameRange){0};
    for (i = 0; i < series->count; i ++) {
        double v = series->values[i];
        range->min = !i || v < range->min ? v : range->min;
        range->max = !i || v > range->max ? v : range->max;
        sum += v;
    }

    range->mean = series->count ? sum / (double)series->count : 0;

    return sum;
}

/** Get the clock of the frame loop of a world.
 *
 * @param world The world
 * @return The clock, NULL when box.app is not imported
 */
static box_app_clock_t* box_app_clock(
    const ecs_world_t *world)
{
    ecs_entity_t e = ecs_lookup(world, BOX_APP_TIMING_SYSTEM);
    const ecs_system_t *system = e ? ecs_system_get(world, e) : NULL;

    return system ? system->ctx : NULL;
}

/** Record the timing of the frame that starts: its delta, the wall time since the previous one
 * and the presents, and publish the ranges of the recent frames in FrameTiming.
 *
 * @param it The iterator of the system
 */
static void BoxAppTiming(
    ecs_iter_t *it)
{
    box_app_clock_t *clock = it->ctx;
    const App *app = ecs_singleton_get(it->world, App);
    const RenderStatus *render = ecs_id(RenderStatus)
        ? ecs_singleton_get(it->world, RenderStatus)
        : NULL;
    FrameTiming *timing = ecs_singleton_get_mut(it->world, FrameTiming);
    FrameRange counts;
    double now = box_app_wall_time();
    double delta = (double)ecs_get_world_info(it->world)->delta_time_raw * 1000.0;
    double interval = clock->begin > 0 ? (now - clock->begin) * 1000.0 : 0;
    double target = app && app->fps > 0 ? 1000.0 / (double)app->fps : 0;
    bool late, dropped;
    clock->begin = now;
    if (!timing || interval <= 0) {
        goto done;
    }

    late = fabs(delta - interval) > BOX_FRAME_TOLERANCE_MS;
    dropped = target > 0 && interval > target * BOX_FRAME_DROPPED;
    box_app_series_add(&clock->delta, delta);
    box_app_series_add(&clock->interval, interval);
    box_app_series_add(&clock->error, fabs(delta - interval));
    box_app_series_add(&clock->late, late);
    box_app_series_add(&clock->dropped, dropped);
    if (render && clock->presented > 0 && render->presented != clock->presented) {
        box_app_series_add(&clock->present, render->present_ms);
    }

    clock->presented = render ? render->presented : 0;
    timing->frames = clock->delta.count;
    timing->target_ms = target;
    timing->vsync = render && render->vsync;
    timing->delta_ms = delta;
    timing->interval_ms = interval;
    box_app_series_range(&clock->delta, &timing->delta);
    box_app_series_range(&clock->interval, &timing->interval);
    box_app_series_range(&clock->error, &timing->error);
    box_app_series_range(&clock->present, &timing->present);
    timing->late = (int32_t)box_app_series_range(&clock->late, &counts);
    timing->dropped = (int32_t)box_app_series_range(&clock->dropped, &counts);
    timing->total ++;
    timing->total_late += late;
    timing->total_dropped += dropped;
    ecs_singleton_modified(it->world, FrameTiming);
done:
    return;
}

/** Serve the REST API, which includes the shell endpoints, on a port.
 *
 * The browser cannot listen on a port and has no threads for the server, so the web build
 * serves nothing.
 *
 * @param world The world
 * @param port The port
 */
static void box_app_serve(
    ecs_world_t *world,
    int32_t port)
{
#ifdef __EMSCRIPTEN__
    (void)world;
    (void)port;
#else
    ECS_IMPORT(world, FlecsStats);
    ecs_enable(world, ecs_id(FlecsStats), true);
    ecs_singleton_set(world, EcsRest, { .port = (uint16_t)port });
#endif
}

char* box_app_locate(
    const char *path)
{
    char dir[4096];
    char *result = NULL;
    if (path[0] == '/' || !access(path, R_OK) || !getcwd(dir, sizeof(dir))) {
        goto done;
    }

    char *slash = NULL;
    do {
        result = flecs_asprintf("%s/%s", dir, path);
        if (!access(result, R_OK)) {
            goto done;
        }

        ecs_os_free(result);
        result = NULL;
        slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
        }
    } while (slash && dir[0]);

done:
    return result ? result : ecs_os_strdup(path);
}

/** Check the settings of a run and load its scene. ecs_script returns the entity of a managed
 * script also when it fails to parse or evaluate; both leave EcsScript without a script and
 * set its error, which tell whether the scene loaded.
 *
 * @param world The world
 * @return 0 when the world may run, nonzero when it may not
 */
static int box_app_start(
    ecs_world_t *world)
{
    const App *app = ecs_singleton_get(world, App);
    const AppStatus *status = ecs_singleton_get(world, AppStatus);
    int result = -1;
    if (!app || !status) {
        ecs_err("box.app is not imported");
        goto done;
    }

    if (app->headless && app->frames <= 0 && app->timeout == 0) {
        ecs_err("headless mode requires --frames or --timeout");
        goto done;
    }

    if (app->headless && !status->tmp_dir) {
        ecs_err("headless mode requires its temporary folder in %s", BOX_TMP_ROOT);
        goto done;
    }

    if (app->headless && !app->unguarded && box_app_guard(app->cpu_budget)) {
        goto done;
    }

    if (app->scene && app->scene[0]) {
        char *scene = box_app_locate(app->scene);
        ecs_entity_t script = ecs_script(world, { .filename = scene });
        const EcsScript *loaded = script ? ecs_get(world, script, EcsScript) : NULL;
        ecs_os_free(scene);
        if (!loaded || !loaded->script || loaded->error) {
            ecs_err("failed to load scene '%s'", app->scene);
            goto done;
        }
    }

    if (box_app_views(world)) {
        goto done;
    }

    app = ecs_singleton_get(world, App);
    if (app->port > 0) {
        box_app_serve(world, app->port);
    }

    box_app_target_fps(world, app);
    result = 0;
done:
    return result;
}

#ifdef __EMSCRIPTEN__
/** Run a frame from the animation frame callback of the browser and end the loop when the
 * world quits. The browser paces the frames, so the target fps of flecs stays 0.
 *
 * @param arg The clock of the frame loop
 */
static void box_app_web_frame(
    void *arg)
{
    box_app_clock_t *clock = arg;
    ecs_world_t *world = clock->world;
    const App *app = ecs_singleton_get(world, App);
    if (!ecs_progress(world, app ? app->delta_time : 0)) {
        ecs_singleton_get_mut(world, AppStatus)->running = false;
        emscripten_cancel_main_loop();
    }
}
#endif

int box_app_run(
    ecs_world_t *world)
{
    AppStatus *status;
    int result = box_app_start(world);
    if (result) {
        goto done;
    }

    status = ecs_singleton_get_mut(world, AppStatus);
    status->running = true;
    box_app_clock_t *clock = box_app_clock(world);
    clock->world = world;
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(box_app_web_frame, clock, 0, true);
#else
    while (ecs_progress(world, box_app_frame_delta(world))) { }
#endif

    status = ecs_singleton_get_mut(world, AppStatus);
    status->running = false;
done:
    return result;
}

void BoxAppImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxApp);

    ECS_META_COMPONENT(world, App);
    ECS_META_COMPONENT(world, AppStatus);
    ECS_META_COMPONENT(world, FrameRange);
    ECS_META_COMPONENT(world, FrameTiming);
    box_owned_hooks(world, App, BOX_OWNED_STR(App, scene), BOX_OWNED_STR(App, camera),
        BOX_OWNED_STR(App, out));
    box_owned_hooks(world, AppStatus, BOX_OWNED_STR(AppStatus, tmp_dir));
    ECS_META_COMPONENT(world, FileWatch);
    ECS_META_COMPONENT(world, FileWatchStatus);
    box_owned_hooks(world, FileWatchStatus, BOX_OWNED_STR(FileWatchStatus, last));

    ecs_singleton_set(world, AppStatus, {0});
    ecs_singleton_set(world, FrameTiming, {0});
    box_file_watch_init(world);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "AppOnSet" }),
        .query.terms = {{ .id = ecs_id(App) }},
        .events = { EcsOnSet },
        .callback = BoxAppOnSet
    });

    box_app_clock_t *clock = box_app_clock_new();
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "FrameTimingUpdate" }),
        .phase = EcsPreFrame,
        .run = BoxAppTiming,
        .ctx = clock,
        .ctx_free = box_app_clock_free
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "AppFrame" }),
        .phase = EcsPostFrame,
        .run = BoxAppFrame,
        .ctx = clock
    });

    ecs_singleton_set(world, App, {
        .cpu_budget = 100,
        .port = BOX_APP_PORT
    });
}
