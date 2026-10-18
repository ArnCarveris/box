/**
 * @file Stop.c
 * @brief Tests for the frame limit, the timeout and the run loop.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/** Error messages logged since the collector was installed */
static ecs_strbuf_t stop_errors = ECS_STRBUF_INIT;

/** Log function the collector passes messages on to */
static ecs_os_api_log_t stop_log = NULL;

/** Collect logged errors and pass every message on.
 *
 * @param level Level of the message
 * @param file File that logged it
 * @param line Line that logged it
 * @param msg The message
 */
static void stop_collect_errors(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    if (level == -3) {
        ecs_strbuf_appendstr(&stop_errors, msg);
        ecs_strbuf_appendch(&stop_errors, '\n');
    }

    if (stop_log) {
        stop_log(level, file, line, msg);
    }
}

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

void Stop_timeout(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, App, { .timeout = 0.1, .cpu_budget = 100 });

    ecs_time_t start;
    ecs_os_get_time(&start);
    int32_t count = 0;
    while (ecs_progress(world, 0) && count < 1000) {
        ecs_sleepf(0.005);
        count ++;
    }

    test_assert(count < 1000);
    test_bool(ecs_should_quit(world), true);
    test_assert(ecs_time_measure(&start) >= 0.1);
    test_assert(ecs_singleton_get(world, AppStatus)->elapsed >= 0.1);

    ecs_fini(world);
}

void Stop_frames_before_timeout(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, App, { .frames = 2, .timeout = 60, .cpu_budget = 100 });

    int32_t count = 0;
    while (ecs_progress(world, 0) && count < 100) {
        count ++;
    }

    test_int(ecs_singleton_get(world, AppStatus)->frame, 2);
    test_assert(ecs_singleton_get(world, AppStatus)->elapsed < 60);

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

void Stop_run_missing_scene(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, App, {
        .headless = true, .frames = 1, .cpu_budget = 100, .fps = 0,
        .unguarded = true, .unpaced = true, .scene = "/tmp/box_core_missing.flecs" });

    ecs_log_set_level(-4);
    test_assert(box_app_run(world) != 0);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 0);

    ecs_fini(world);
}

void Stop_run_invalid_scene_headless(void) {
    // core_fixes/scene_load_failure
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *path = flecs_asprintf("/tmp/box_core_invalid_%d.flecs", (int)getpid());
    FILE *f = fopen(path, "w");
    test_assert(f != NULL);
    fputs("Foo {}\nusing flecs.meta\n", f);
    fclose(f);
    ecs_singleton_set(world, App, {
        .headless = true, .frames = 2, .cpu_budget = 100, .fps = 0,
        .unguarded = true, .unpaced = true, .scene = path });

    ecs_strbuf_reset(&stop_errors);
    stop_log = ecs_os_api.log_;
    ecs_os_api.log_ = stop_collect_errors;
    int result = box_app_run(world);
    ecs_os_api.log_ = stop_log;
    char *errors = ecs_strbuf_get(&stop_errors);
    test_assert(result != 0);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 0);
    test_assert(errors != NULL);
    test_assert(strstr(errors, "must be declared before other statements") != NULL);
    test_assert(strstr(errors, "failed to load scene") != NULL);

    ecs_os_free(errors);
    unlink(path);
    ecs_os_free(path);
    ecs_fini(world);
}

void Stop_run_invalid_scene_windowed(void) {
    // core_fixes/scene_load_failure
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *path = flecs_asprintf("/tmp/box_core_invalid_%d.flecs", (int)getpid());
    FILE *f = fopen(path, "w");
    test_assert(f != NULL);
    fputs("Foo {\n", f);
    fclose(f);
    ecs_singleton_set(world, App, {
        .frames = 2, .fps = 0, .unpaced = true, .scene = path });

    ecs_strbuf_reset(&stop_errors);
    stop_log = ecs_os_api.log_;
    ecs_os_api.log_ = stop_collect_errors;
    int result = box_app_run(world);
    ecs_os_api.log_ = stop_log;
    char *errors = ecs_strbuf_get(&stop_errors);
    test_assert(result != 0);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 0);
    test_assert(errors != NULL);
    test_assert(strstr(errors, "unexpected end of script") != NULL);
    test_assert(strstr(errors, "failed to load scene") != NULL);

    ecs_os_free(errors);
    unlink(path);
    ecs_os_free(path);
    ecs_fini(world);
}

void Stop_run_scene_evaluation_error(void) {
    // core_fixes/scene_load_failure
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *path = flecs_asprintf("/tmp/box_core_invalid_%d.flecs", (int)getpid());
    FILE *f = fopen(path, "w");
    test_assert(f != NULL);
    fputs("e { NoSuchComponent }\n", f);
    fclose(f);
    ecs_singleton_set(world, App, {
        .frames = 2, .fps = 0, .unpaced = true, .scene = path });

    ecs_strbuf_reset(&stop_errors);
    stop_log = ecs_os_api.log_;
    ecs_os_api.log_ = stop_collect_errors;
    int result = box_app_run(world);
    ecs_os_api.log_ = stop_log;
    char *errors = ecs_strbuf_get(&stop_errors);
    test_assert(result != 0);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 0);
    test_assert(errors != NULL);
    test_assert(strstr(errors, "NoSuchComponent") != NULL);
    test_assert(strstr(errors, "failed to load scene") != NULL);

    ecs_os_free(errors);
    unlink(path);
    ecs_os_free(path);
    ecs_fini(world);
}
