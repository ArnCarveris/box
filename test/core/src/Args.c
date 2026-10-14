/**
 * @file Args.c
 * @brief Tests for parsing the command line into the App singleton.
 */

#include <box.h>
#include <bake_test.h>

void Args_defaults(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun"};
    test_int(box_app_parse(world, 1, argv), 0);

    const App *app = ecs_singleton_get(world, App);
    test_assert(app != NULL);
    test_bool(app->headless, false);
    test_int(app->frames, 0);
    test_flt(app->timeout, 0);
    test_flt(app->cpu_budget, 100);
    test_flt(app->fps, 60);
    test_int(app->port, 27750);
    test_assert(app->scene == NULL);

    ecs_fini(world);
}

void Args_headless(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--headless"};
    test_int(box_app_parse(world, 2, argv), 0);

    const App *app = ecs_singleton_get(world, App);
    test_bool(app->headless, true);

    ecs_fini(world);
}

void Args_frames(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--frames", "10"};
    test_int(box_app_parse(world, 3, argv), 0);

    const App *app = ecs_singleton_get(world, App);
    test_int(app->frames, 10);
    test_bool(app->headless, false);

    ecs_fini(world);
}

void Args_timeout_seconds(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--timeout", "2.5"};
    test_int(box_app_parse(world, 3, argv), 0);

    const App *app = ecs_singleton_get(world, App);
    test_flt(app->timeout, 2.5);

    ecs_fini(world);
}

void Args_timeout_infinite(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--timeout", "infinite"};
    test_int(box_app_parse(world, 3, argv), 0);

    const App *app = ecs_singleton_get(world, App);
    test_assert(app->timeout < 0);

    ecs_fini(world);
}

void Args_cpu_budget(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--cpu-budget", "25"};
    test_int(box_app_parse(world, 3, argv), 0);

    const App *app = ecs_singleton_get(world, App);
    test_flt(app->cpu_budget, 25);

    ecs_fini(world);
}

void Args_cpu_budget_percent_sign(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--cpu-budget", "150%"};
    test_int(box_app_parse(world, 3, argv), 0);

    const App *app = ecs_singleton_get(world, App);
    test_flt(app->cpu_budget, 150);

    ecs_fini(world);
}

void Args_fps(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--fps", "120"};
    test_int(box_app_parse(world, 3, argv), 0);

    const App *app = ecs_singleton_get(world, App);
    test_flt(app->fps, 120);

    ecs_fini(world);
}

void Args_port(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--port", "0"};
    test_int(box_app_parse(world, 3, argv), 0);

    const App *app = ecs_singleton_get(world, App);
    test_int(app->port, 0);

    ecs_fini(world);
}

void Args_scene(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "etc/scenes/default.flecs"};
    test_int(box_app_parse(world, 2, argv), 0);

    const App *app = ecs_singleton_get(world, App);
    test_str(app->scene, "etc/scenes/default.flecs");

    ecs_fini(world);
}

void Args_all_options(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--headless", "--frames", "5", "--timeout", "30",
        "--cpu-budget", "50", "--fps", "30", "--port", "8000", "scene.flecs"};
    test_int(box_app_parse(world, 13, argv), 0);

    const App *app = ecs_singleton_get(world, App);
    test_bool(app->headless, true);
    test_int(app->frames, 5);
    test_flt(app->timeout, 30);
    test_flt(app->cpu_budget, 50);
    test_flt(app->fps, 30);
    test_int(app->port, 8000);
    test_str(app->scene, "scene.flecs");

    ecs_fini(world);
}

void Args_unknown_option(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--fast"};
    ecs_log_set_level(-4);
    test_assert(box_app_parse(world, 2, argv) != 0);

    ecs_fini(world);
}

void Args_missing_value(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--frames"};
    ecs_log_set_level(-4);
    test_assert(box_app_parse(world, 2, argv) != 0);

    ecs_fini(world);
}

void Args_invalid_frames(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--frames", "ten"};
    ecs_log_set_level(-4);
    test_assert(box_app_parse(world, 3, argv) != 0);

    char *argv_zero[] = {"boxrun", "--frames", "0"};
    test_assert(box_app_parse(world, 3, argv_zero) != 0);

    char *argv_fraction[] = {"boxrun", "--frames", "1.5"};
    test_assert(box_app_parse(world, 3, argv_fraction) != 0);

    ecs_fini(world);
}

void Args_invalid_timeout(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--timeout", "forever"};
    ecs_log_set_level(-4);
    test_assert(box_app_parse(world, 3, argv) != 0);

    char *argv_zero[] = {"boxrun", "--timeout", "0"};
    test_assert(box_app_parse(world, 3, argv_zero) != 0);

    ecs_fini(world);
}

void Args_invalid_cpu_budget(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--cpu-budget", "0"};
    ecs_log_set_level(-4);
    test_assert(box_app_parse(world, 3, argv) != 0);

    char *argv_text[] = {"boxrun", "--cpu-budget", "half"};
    test_assert(box_app_parse(world, 3, argv_text) != 0);

    ecs_fini(world);
}

void Args_invalid_port(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--port", "70000"};
    ecs_log_set_level(-4);
    test_assert(box_app_parse(world, 3, argv) != 0);

    ecs_fini(world);
}

void Args_two_scenes(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "a.flecs", "b.flecs"};
    ecs_log_set_level(-4);
    test_assert(box_app_parse(world, 3, argv) != 0);

    ecs_fini(world);
}
