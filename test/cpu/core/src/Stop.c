/**
 * @file Stop.c
 * @brief Tests for the frame limit, the timeout and the run loop.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int32_t quit_after = 0;
static float last_delta = 0;

static void QuitAfter(ecs_iter_t *it) {
    const AppStatus *status = ecs_singleton_get(it->world, AppStatus);
    if (status->frame + 1 >= quit_after) {
        ecs_quit(it->world);
    }
}

static void RecordDelta(ecs_iter_t *it) {
    last_delta = it->delta_time;
}

void Stop_frames(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, App, { .frames = 3, .cpu_budget = 100 });

    int32_t count = 0;
    while (ecs_progress(world, 0) && count < 100) {
        count ++;
    }

    test_int(count, 2);
    test_bool(ecs_should_quit(world), true);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 3);

    ecs_fini(world);
}

void Stop_no_limit(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    for (int i = 0; i < 10; i ++) {
        test_bool(ecs_progress(world, 0), true);
    }

    test_bool(ecs_should_quit(world), false);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 10);

    ecs_fini(world);
}

void Stop_timeout_infinite(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, App, { .timeout = -1, .cpu_budget = 100 });

    for (int i = 0; i < 10; i ++) {
        test_bool(ecs_progress(world, 0), true);
    }

    test_bool(ecs_should_quit(world), false);

    ecs_fini(world);
}

void Stop_run_headless_frames(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, App, {
        .headless = true, .frames = 5, .cpu_budget = 100, .fps = 0,
        .unguarded = true, .unpaced = true });

    test_int(box_app_run(world), 0);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 5);
    test_bool(ecs_singleton_get(world, AppStatus)->running, false);

    ecs_fini(world);
}

void Stop_run_headless_requires_limit(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, App, {
        .headless = true, .cpu_budget = 100, .unguarded = true, .unpaced = true });

    ecs_log_set_level(-4);
    test_assert(box_app_run(world) != 0);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 0);

    ecs_fini(world);
}

void Stop_run_headless_timeout_infinite(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    quit_after = 3;
    ecs_system(world, {
        .phase = EcsOnUpdate,
        .run = QuitAfter
    });

    ecs_singleton_set(world, App, {
        .headless = true, .timeout = -1, .cpu_budget = 100, .fps = 0,
        .unguarded = true, .unpaced = true });

    test_int(box_app_run(world), 0);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 3);

    ecs_fini(world);
}

void Stop_run_headless_fixed_delta(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_system(world, {
        .phase = EcsOnUpdate,
        .run = RecordDelta
    });

    ecs_singleton_set(world, App, {
        .headless = true, .frames = 3, .cpu_budget = 100, .delta_time = 0.02f,
        .unguarded = true, .unpaced = true });

    test_int(box_app_run(world), 0);
    test_flt(last_delta, 0.02f);

    ecs_fini(world);
}

void Stop_run_loads_scene(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *path = flecs_asprintf("/tmp/box_core_scene_%d.flecs", (int)getpid());
    FILE *f = fopen(path, "w");
    test_assert(f != NULL);
    fprintf(f, "scene_root {}\n");
    fclose(f);

    ecs_singleton_set(world, App, {
        .headless = true, .frames = 1, .cpu_budget = 100, .fps = 0,
        .unguarded = true, .unpaced = true, .scene = path });

    test_int(box_app_run(world), 0);
    test_assert(ecs_lookup(world, "scene_root") != 0);

    unlink(path);
    ecs_os_free(path);
    ecs_fini(world);
}

/** Log output of the run while capturing */
static FILE *stop_log = NULL;

/** Log output stream before capturing */
static void *stop_log_prev = NULL;

/** Send the log output to a temporary file, so the expected errors stay out of the test output
 * and still pass the error count of box.app.
 */
static void stop_capture_begin(void) {
    stop_log = tmpfile();
    test_assert(stop_log != NULL);
    stop_log_prev = ecs_os_api.log_out_;
    ecs_os_api.log_out_ = stop_log;
}

/** Stop capturing and test whether the captured output contains a text.
 *
 * @param text The text, NULL for no check
 * @return Whether the output contains the text
 */
static bool stop_capture_end(
    const char *text)
{
    char buf[8192];
    size_t len;
    ecs_os_api.log_out_ = stop_log_prev;
    fflush(stop_log);
    rewind(stop_log);
    len = fread(buf, 1, sizeof(buf) - 1, stop_log);
    buf[len] = '\0';
    fclose(stop_log);
    stop_log = NULL;

    return !text || strstr(buf, text) != NULL;
}

/** Run a scene of test/assets/app headless for some frames.
 *
 * @param world The world
 * @param scene The scene from the repository root
 * @param frames The frames
 * @param fail_on_error Whether an error logged fails the run
 * @return The result of box_app_run
 */
static int stop_run_scene(
    ecs_world_t *world,
    const char *scene,
    int32_t frames,
    bool fail_on_error)
{
    ecs_singleton_set(world, App, {
        .headless = true, .frames = frames, .cpu_budget = 100, .fps = 0,
        .unguarded = true, .unpaced = true, .scene = ECS_CONST_CAST(char*, scene),
        .fail_on_error = fail_on_error });

    return box_app_run(world);
}

/** Async function that resolves at once, so the awaiting block resumes the next frame. */
static void CoreTick(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)ctx;
    (void)argc;
    (void)argv;
    bool value = true;
    ecs_script_future_resolve(future, &(ecs_value_t){
        .type = ecs_id(ecs_bool_t), .ptr = &value });
    ecs_script_future_release(future);
}

/** Create a world with Box and the core_tick async function of async_error.flecs.
 *
 * @return The world
 */
static ecs_world_t* stop_async_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_async_function(world, {
        .name = "core_tick",
        .return_type = ecs_id(ecs_bool_t),
        .callback = CoreTick
    });

    return world;
}

/** Define the CoreWaited component of waits_for_component.flecs after the first frame. */
static void DefineWaited(ecs_iter_t *it) {
    const AppStatus *status = ecs_singleton_get(it->world, AppStatus);
    if (status->frame == 1 && !ecs_lookup(it->world, "CoreWaited")) {
        ecs_struct(it->world, {
            .entity = ecs_entity(it->world, { .name = "CoreWaited" }),
            .members = {{ .name = "value", .type = ecs_id(ecs_i32_t) }}
        });
    }
}

void Stop_run_scene_syntax_error(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    stop_capture_begin();
    int result = stop_run_scene(world, "test/assets/app/syntax_error.flecs", 3, false);
    test_bool(stop_capture_end("not loaded"), true);
    test_assert(result != 0);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 3);
    test_assert(ecs_singleton_get(world, AppStatus)->errors > 0);

    ecs_fini(world);
}

void Stop_run_scene_missing(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    stop_capture_begin();
    int result = stop_run_scene(world, "test/assets/app/no_such_scene.flecs", 2, false);
    test_bool(stop_capture_end("not loaded"), true);
    test_assert(result != 0);

    ecs_fini(world);
}

void Stop_run_scene_valid(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_int(stop_run_scene(world, "test/assets/app/valid.flecs", 3, true), 0);
    test_assert(ecs_lookup(world, "valid_root") != 0);
    test_int(ecs_singleton_get(world, AppStatus)->errors, 0);

    ecs_fini(world);
}

void Stop_run_async_error(void) {
    // engine_core/app
    ecs_world_t *world = stop_async_world();

    stop_capture_begin();
    int result = stop_run_scene(world, "test/assets/app/async_error.flecs", 5, false);
    test_bool(stop_capture_end("division by zero"), true);
    test_int(result, 0);
    test_assert(ecs_lookup(world, "async_error_root") != 0);
    test_assert(ecs_singleton_get(world, AppStatus)->errors > 0);

    ecs_fini(world);
}

void Stop_run_async_error_fail_on_error(void) {
    // engine_core/app
    ecs_world_t *world = stop_async_world();

    stop_capture_begin();
    int result = stop_run_scene(world, "test/assets/app/async_error.flecs", 5, true);
    test_bool(stop_capture_end("--fail-on-error"), true);
    test_assert(result != 0);
    test_assert(ecs_singleton_get(world, AppStatus)->errors > 0);

    ecs_fini(world);
}

void Stop_run_errors_counted_from_start(void) {
    // engine_core/app
    stop_capture_begin();
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_err("an error before the run");
    int result = stop_run_scene(world, "test/assets/app/valid.flecs", 2, true);
    test_bool(stop_capture_end("an error before the run"), true);
    test_assert(result != 0);
    test_int(ecs_singleton_get(world, AppStatus)->errors, 1);

    ecs_fini(world);
}

void Stop_run_scene_loaded_later(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_system(world, {
        .phase = EcsOnUpdate,
        .run = DefineWaited,
        .immediate = true
    });

    stop_capture_begin();
    int result = stop_run_scene(world, "test/assets/app/waits_for_component.flecs", 4, false);
    test_bool(stop_capture_end("failed to load scene"), true);
    test_int(result, 0);
    ecs_entity_t root = ecs_lookup(world, "waiting_root");
    test_assert(root != 0);
    ecs_entity_t waited = ecs_lookup(world, "CoreWaited");
    test_assert(waited != 0);
    test_assert(ecs_has_id(world, root, waited));

    ecs_fini(world);
}
