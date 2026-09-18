/**
 * @file Args.c
 * @brief Tests for command line parsing into the App singleton.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>

static ecs_world_t* args_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCore);

    return world;
}

void Args_no_args_defaults(void)
{
    // engine_core/app
    ecs_world_t *world = args_world();
    char *argv[] = { "box" };
    test_int(box_parse_args(world, 1, argv), 0);
    const App *app = ecs_singleton_get(world, App);
    test_assert(app != NULL);
    test_bool(app->headless, false);
    test_int(app->frames, 0);
    test_flt(app->timeout, 0);
    test_flt(app->cpu_budget, 1.0);
    test_flt(app->target_fps, 0);
    ecs_fini(world);
}

void Args_headless_requires_stop_condition(void)
{
    // engine_core/app
    ecs_world_t *world = args_world();
    ecs_log_set_level(-4);
    char *argv[] = { "box", "--headless" };
    test_int(box_parse_args(world, 2, argv), -1);
    ecs_log_set_level(-1);
    ecs_fini(world);
}

void Args_headless_with_frames(void)
{
    // engine_core/app
    ecs_world_t *world = args_world();
    char *argv[] = { "box", "--headless", "--frames", "12" };
    test_int(box_parse_args(world, 4, argv), 0);
    const App *app = ecs_singleton_get(world, App);
    test_bool(app->headless, true);
    test_int(app->frames, 12);
    ecs_fini(world);
}

void Args_timeout_seconds(void)
{
    // engine_core/app
    ecs_world_t *world = args_world();
    char *argv[] = { "box", "--headless", "--timeout", "1.5" };
    test_int(box_parse_args(world, 4, argv), 0);
    test_flt(ecs_singleton_get(world, App)->timeout, 1.5);
    ecs_fini(world);
}

void Args_timeout_infinite(void)
{
    // engine_core/app
    ecs_world_t *world = args_world();
    char *argv[] = { "box", "--headless", "--timeout", "infinite" };
    test_int(box_parse_args(world, 4, argv), 0);
    test_flt(ecs_singleton_get(world, App)->timeout, -1.0);
    ecs_fini(world);
}

void Args_cpu_budget(void)
{
    // engine_core/app
    ecs_world_t *world = args_world();
    char *argv[] = { "box", "--cpu-budget", "0.5" };
    test_int(box_parse_args(world, 3, argv), 0);
    test_flt(ecs_singleton_get(world, App)->cpu_budget, 0.5);
    ecs_fini(world);
}

void Args_fps(void)
{
    // engine_core/app
    ecs_world_t *world = args_world();
    char *argv[] = { "box", "--fps", "30" };
    test_int(box_parse_args(world, 3, argv), 0);
    test_flt(ecs_singleton_get(world, App)->target_fps, 30);
    ecs_fini(world);
}

void Args_unknown_option_fails(void)
{
    // engine_core/app
    ecs_world_t *world = args_world();
    ecs_log_set_level(-4);
    char *argv[] = { "box", "--bogus" };
    test_int(box_parse_args(world, 2, argv), -1);
    ecs_log_set_level(-1);
    ecs_fini(world);
}

void Args_scene_file_loads(void)
{
    // engine_core/app
    ecs_world_t *world = args_world();
    const char *path = "/tmp/box_args_scene.flecs";
    FILE *f = fopen(path, "w");
    test_assert(f != NULL);
    fputs("$ { box.core.App: {frames: 7} }\n", f);
    fclose(f);
    char *argv[] = { "box", (char*)path };
    test_int(box_parse_args(world, 2, argv), 0);
    test_int(ecs_singleton_get(world, App)->frames, 7);
    remove(path);
    ecs_fini(world);
}

void Args_missing_scene_fails(void)
{
    // engine_core/app
    ecs_world_t *world = args_world();
    ecs_log_set_level(-4);
    char *argv[] = { "box", "/tmp/box_does_not_exist.flecs" };
    test_int(box_parse_args(world, 2, argv), -1);
    ecs_log_set_level(-1);
    ecs_fini(world);
}
