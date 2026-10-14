/**
 * @file app/app.c
 * @brief Registers the box.app module, its stop conditions, the CPU budget and the run loop.
 */

#define BOX_APP_IMPL
#include "app.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/** Clock of the frame system, which measures the interval between two frames */
typedef struct box_app_clock_t {
    double start;   /**< Wall seconds when the module was imported */
    double cpu;     /**< Process CPU seconds at the end of the previous frame */
    double wall;    /**< Wall seconds at the end of the previous frame */
} box_app_clock_t;

/** Check whether the environment asks for test worlds.
 *
 * @return Whether BOX_TEST is set to anything but 0
 */
static bool box_app_test_env(void)
{
    const char *value = getenv(BOX_TEST_ENV);

    return value && value[0] && strcmp(value, "0") != 0;
}

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

/** Make a new App true: a headless process gets its temporary folder and a running
 * world its frame rate.
 *
 * @param it The iterator of the observer
 */
static void BoxAppOnSet(
    ecs_iter_t *it)
{
    ecs_world_t *world = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(it->world));
    const App *app = ecs_field(it, App, 0);
    AppStatus *status = ecs_singleton_get_mut(world, AppStatus);
    if (!status) {
        goto done;
    }

    if (app->headless && !status->tmp_dir) {
        status->tmp_dir = box_app_tmp_create(world);
    }

    if (status->running) {
        ecs_set_target_fps(world, app->fps);
    }

done:
    return;
}

/** Count the frame, start the clock on the first frame, stop the world when App says so and
 * sleep when a headless process used more than its CPU budget since the previous frame.
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
        double sleep = box_app_pace(cpu - clock->cpu, wall - clock->wall, app->cpu_budget);
        if (sleep > 0) {
            ecs_sleepf(sleep);
            status->slept += sleep;
            wall = box_app_wall_time();
        }
    }

done:
    clock->cpu = cpu;
    clock->wall = wall;
}

/** Compute the delta time of the next frame.
 *
 * @param app The App singleton
 * @return A fixed step of one frame when headless, 0 to measure it otherwise
 */
static float box_app_delta(
    const App *app)
{
    float result = 0;
    if (app->headless) {
        result = 1.0f / (app->fps > 0 ? app->fps : (float)BOX_APP_FPS);
    }

    return result;
}

/** Serve the terminal and REST endpoints on a port, only REST without box.command.
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
    ecs_entity_t server = ecs_lookup(world, "box.command.TerminalServer");
    if (server) {
        TerminalServer value = { .port = port };
        ecs_set_id(world, server, server, sizeof(TerminalServer), &value);
    } else {
        ecs_singleton_set(world, EcsRest, { .port = (uint16_t)port });
    }
#endif
}

/** Find a scene file: the path as given, or a relative path in the working directory or one
 * of its ancestors, so a run from inside the repository finds `etc/scenes/...`.
 *
 * @param path The scene path from the command line
 * @return The path to load, owned by the caller
 */
static char* box_app_scene_path(
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

/** Check the settings of a run and load its scene.
 *
 * @param world The world
 * @return 0 when the world may run, nonzero when it may not
 */
static int box_app_start(
    ecs_world_t *world)
{
    const App *app = ecs_singleton_get(world, App);
    int result = -1;
    if (!app) {
        ecs_err("box.app is not imported");
        goto done;
    }

    if (app->headless && app->frames <= 0 && app->timeout == 0) {
        ecs_err("headless mode requires --frames or --timeout");
        goto done;
    }

    if (app->headless && !app->unguarded && box_app_guard()) {
        goto done;
    }

    if (app->scene && app->scene[0]) {
        char *scene = box_app_scene_path(app->scene);
        bool loaded = ecs_script(world, { .filename = scene }) != 0;
        ecs_os_free(scene);
        if (!loaded) {
            ecs_err("failed to load scene '%s'", app->scene);
            goto done;
        }
    }

    app = ecs_singleton_get(world, App);
    if (app->port > 0) {
        box_app_serve(world, app->port);
    }

    ecs_set_target_fps(world, app->fps);
    result = 0;
done:
    return result;
}

#ifdef __EMSCRIPTEN__
/** Run a frame from the animation frame callback of the browser and end the loop when the
 * world quits. The browser paces the frames, so flecs must not sleep to reach a frame rate.
 *
 * @param arg The world
 */
static void box_app_web_frame(
    void *arg)
{
    ecs_world_t *world = arg;
    if (ecs_get_world_info(world)->target_fps != 0) {
        ecs_set_target_fps(world, 0);
    }

    if (!ecs_progress(world, box_app_delta(ecs_singleton_get(world, App)))) {
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
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(box_app_web_frame, world, 0, true);
#else
    while (ecs_progress(world, box_app_delta(ecs_singleton_get(world, App)))) {
    }
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
    box_owned_hooks(world, App, BOX_OWNED_STR(App, scene));
    box_owned_hooks(world, AppStatus, BOX_OWNED_STR(AppStatus, tmp_dir));

    ecs_singleton_set(world, AppStatus, {0});

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "AppOnSet" }),
        .query.terms = {{ .id = ecs_id(App) }},
        .events = { EcsOnSet },
        .callback = BoxAppOnSet
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "AppFrame" }),
        .phase = EcsPostFrame,
        .run = BoxAppFrame,
        .ctx = box_app_clock_new(),
        .ctx_free = box_app_clock_free
    });

    bool test = box_app_test_env();
    ecs_singleton_set(world, App, {
        .cpu_budget = 100,
        .fps = BOX_APP_FPS,
        .port = BOX_APP_PORT,
        .unguarded = test,
        .unpaced = test
    });
}
