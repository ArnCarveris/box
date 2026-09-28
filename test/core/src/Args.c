/**
 * @file Args.c
 * @brief Tests for command line parsing into the App singleton.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <string.h>

/** Settings of the stress test an application option writes */
typedef struct Stress {
    bool enabled;   /**< The stress test runs */
    int32_t seconds; /**< Seconds the stress test lasts */
    char *mode;     /**< Name of the mode it runs in */
} Stress;

static ecs_world_t* args_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

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

void Args_a_flag_option_sets_its_bool_member(void)
{
    // ore_engine/app_args
    ecs_world_t *world = args_world();
    ECS_COMPONENT(world, Stress);
    ecs_struct(world, {
        .entity = ecs_id(Stress),
        .members = {
            { .name = "enabled", .type = ecs_id(ecs_bool_t) },
            { .name = "seconds", .type = ecs_id(ecs_i32_t) },
            { .name = "mode", .type = ecs_id(ecs_string_t) }
        }
    });

    ecs_entity_t option = ecs_entity(world, { .name = "options.stresstest" });
    AppOption *o = ecs_ensure(world, option, AppOption);
    o->member = ecs_lookup(world, "Stress.enabled");
    o->description = ecs_os_strdup("run the stress test");
    test_assert(o->member != 0);
    ecs_modified(world, option, AppOption);

    char *argv[] = { "box", "--stresstest" };
    test_int(box_parse_args(world, 2, argv), 0);
    test_bool(ecs_singleton_get(world, Stress)->enabled, true);
    ecs_fini(world);
}

void Args_an_option_takes_the_value_behind_its_equals_sign(void)
{
    // ore_engine/app_args
    ecs_world_t *world = args_world();
    ECS_COMPONENT(world, Stress);
    ecs_struct(world, {
        .entity = ecs_id(Stress),
        .members = {
            { .name = "enabled", .type = ecs_id(ecs_bool_t) },
            { .name = "seconds", .type = ecs_id(ecs_i32_t) },
            { .name = "mode", .type = ecs_id(ecs_string_t) }
        }
    });

    ecs_entity_t option = ecs_entity(world, { .name = "options.stresstest" });
    AppOption *o = ecs_ensure(world, option, AppOption);
    o->member = ecs_lookup(world, "Stress.seconds");
    o->fallback = ecs_os_strdup("30");
    ecs_modified(world, option, AppOption);

    char *argv[] = { "box", "--stresstest=45" };
    test_int(box_parse_args(world, 2, argv), 0);
    test_int(ecs_singleton_get(world, Stress)->seconds, 45);
    ecs_fini(world);
}

void Args_an_option_without_a_value_takes_its_fallback(void)
{
    // ore_engine/app_args
    ecs_world_t *world = args_world();
    ECS_COMPONENT(world, Stress);
    ecs_struct(world, {
        .entity = ecs_id(Stress),
        .members = {
            { .name = "enabled", .type = ecs_id(ecs_bool_t) },
            { .name = "seconds", .type = ecs_id(ecs_i32_t) },
            { .name = "mode", .type = ecs_id(ecs_string_t) }
        }
    });

    ecs_entity_t option = ecs_entity(world, { .name = "options.stresstest" });
    AppOption *o = ecs_ensure(world, option, AppOption);
    o->member = ecs_lookup(world, "Stress.seconds");
    o->fallback = ecs_os_strdup("30");
    ecs_modified(world, option, AppOption);

    char *argv[] = { "box", "--stresstest" };
    test_int(box_parse_args(world, 2, argv), 0);
    test_int(ecs_singleton_get(world, Stress)->seconds, 30);
    ecs_fini(world);
}

void Args_an_option_that_needs_a_value_takes_the_next_argument(void)
{
    // ore_engine/app_args
    ecs_world_t *world = args_world();
    ECS_COMPONENT(world, Stress);
    ecs_struct(world, {
        .entity = ecs_id(Stress),
        .members = {
            { .name = "enabled", .type = ecs_id(ecs_bool_t) },
            { .name = "seconds", .type = ecs_id(ecs_i32_t) },
            { .name = "mode", .type = ecs_id(ecs_string_t) }
        }
    });

    ecs_entity_t option = ecs_entity(world, { .name = "options.mode" });
    AppOption *o = ecs_ensure(world, option, AppOption);
    o->member = ecs_lookup(world, "Stress.mode");
    ecs_modified(world, option, AppOption);

    char *argv[] = { "box", "--mode", "mining", "--fps", "30" };
    test_int(box_parse_args(world, 5, argv), 0);
    test_str(ecs_singleton_get(world, Stress)->mode, "mining");
    test_flt(ecs_singleton_get(world, App)->target_fps, 30);
    ecs_fini(world);
}

void Args_an_option_that_needs_a_value_fails_without_one(void)
{
    // ore_engine/app_args
    ecs_world_t *world = args_world();
    ECS_COMPONENT(world, Stress);
    ecs_struct(world, {
        .entity = ecs_id(Stress),
        .members = {
            { .name = "enabled", .type = ecs_id(ecs_bool_t) },
            { .name = "seconds", .type = ecs_id(ecs_i32_t) },
            { .name = "mode", .type = ecs_id(ecs_string_t) }
        }
    });

    ecs_entity_t option = ecs_entity(world, { .name = "options.mode" });
    AppOption *o = ecs_ensure(world, option, AppOption);
    o->member = ecs_lookup(world, "Stress.mode");
    ecs_modified(world, option, AppOption);

    ecs_log_set_level(-4);
    char *argv[] = { "box", "--mode" };
    test_int(box_parse_args(world, 2, argv), -1);
    ecs_log_set_level(-1);
    ecs_fini(world);
}

void Args_an_unclaimed_option_still_fails(void)
{
    // ore_engine/app_args
    ecs_world_t *world = args_world();
    ECS_COMPONENT(world, Stress);
    ecs_struct(world, {
        .entity = ecs_id(Stress),
        .members = {
            { .name = "enabled", .type = ecs_id(ecs_bool_t) },
            { .name = "seconds", .type = ecs_id(ecs_i32_t) },
            { .name = "mode", .type = ecs_id(ecs_string_t) }
        }
    });

    ecs_entity_t option = ecs_entity(world, { .name = "options.stresstest" });
    AppOption *o = ecs_ensure(world, option, AppOption);
    o->member = ecs_lookup(world, "Stress.enabled");
    ecs_modified(world, option, AppOption);

    ecs_log_set_level(-4);
    char *argv[] = { "box", "--bogus" };
    test_int(box_parse_args(world, 2, argv), -1);
    ecs_log_set_level(-1);
    ecs_fini(world);
}

void Args_the_usage_text_lists_the_options_of_the_world(void)
{
    // ore_engine/app_args
    ecs_world_t *world = args_world();
    ECS_COMPONENT(world, Stress);
    ecs_struct(world, {
        .entity = ecs_id(Stress),
        .members = {
            { .name = "enabled", .type = ecs_id(ecs_bool_t) },
            { .name = "seconds", .type = ecs_id(ecs_i32_t) },
            { .name = "mode", .type = ecs_id(ecs_string_t) }
        }
    });

    ecs_entity_t option = ecs_entity(world, { .name = "options.stresstest" });
    AppOption *o = ecs_ensure(world, option, AppOption);
    o->member = ecs_lookup(world, "Stress.seconds");
    o->fallback = ecs_os_strdup("30");
    o->description = ecs_os_strdup("run the stress test");
    ecs_modified(world, option, AppOption);

    char *usage = box_args_usage(world, "box");
    test_assert(strstr(usage, "--headless") != NULL);
    test_assert(strstr(usage, "--stresstest[=<value>]") != NULL);
    test_assert(strstr(usage, "run the stress test") != NULL);
    ecs_os_free(usage);
    ecs_fini(world);
}

void Args_a_scene_still_loads_next_to_an_option(void)
{
    // ore_engine/app_args
    ecs_world_t *world = args_world();
    ECS_COMPONENT(world, Stress);
    ecs_struct(world, {
        .entity = ecs_id(Stress),
        .members = {
            { .name = "enabled", .type = ecs_id(ecs_bool_t) },
            { .name = "seconds", .type = ecs_id(ecs_i32_t) },
            { .name = "mode", .type = ecs_id(ecs_string_t) }
        }
    });

    ecs_entity_t option = ecs_entity(world, { .name = "options.stresstest" });
    AppOption *o = ecs_ensure(world, option, AppOption);
    o->member = ecs_lookup(world, "Stress.seconds");
    o->fallback = ecs_os_strdup("30");
    ecs_modified(world, option, AppOption);

    const char *path = "/tmp/box_args_option_scene.flecs";
    FILE *f = fopen(path, "w");
    test_assert(f != NULL);
    fputs("$ { box.core.App: {frames: 7} }\n", f);
    fclose(f);
    char *argv[] = { "box", "--stresstest=45", (char*)path };
    test_int(box_parse_args(world, 3, argv), 0);
    test_int(ecs_singleton_get(world, App)->frames, 7);
    test_int(ecs_singleton_get(world, Stress)->seconds, 45);
    remove(path);
    ecs_fini(world);
}

void Args_an_option_with_a_bad_value_fails(void)
{
    // ore_engine/app_args
    ecs_world_t *world = args_world();
    ECS_COMPONENT(world, Stress);
    ecs_struct(world, {
        .entity = ecs_id(Stress),
        .members = {
            { .name = "enabled", .type = ecs_id(ecs_bool_t) },
            { .name = "seconds", .type = ecs_id(ecs_i32_t) },
            { .name = "mode", .type = ecs_id(ecs_string_t) }
        }
    });

    ecs_entity_t option = ecs_entity(world, { .name = "options.stresstest" });
    AppOption *o = ecs_ensure(world, option, AppOption);
    o->member = ecs_lookup(world, "Stress.enabled");
    ecs_modified(world, option, AppOption);

    ecs_log_set_level(-4);
    char *argv[] = { "box", "--stresstest=maybe" };
    test_int(box_parse_args(world, 2, argv), -1);
    ecs_log_set_level(-1);
    ecs_fini(world);
}

void Args_a_scene_loads_under_the_scenes_scope(void)
{
    // ore_engine/scene_path_entities
    ecs_world_t *world = args_world();
    const char *path = "/tmp/box_args_scoped.flecs";
    FILE *f = fopen(path, "w");
    test_assert(f != NULL);
    fputs("crate {}\n", f);
    fclose(f);
    char *argv[] = { "box", (char*)path };
    test_int(box_parse_args(world, 2, argv), 0);

    ecs_entity_t scene = ecs_lookup(world, "scenes.box_args_scoped");
    test_assert(scene != 0);
    test_assert(ecs_has(world, scene, EcsScript));
    test_assert(ecs_lookup(world, "crate") != 0);

    ecs_iter_t it = ecs_children(world, 0);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const char *name = ecs_get_name(world, it.entities[i]);
            test_assert(!name || name[0] != '\0');
        }
    }

    remove(path);
    ecs_fini(world);
}

void Args_gpu_names_the_gpu_to_render_on(void)
{
    // launcher/gpu_selection
    ecs_world_t *world = args_world();
    test_assert(ecs_singleton_get(world, App)->gpu == NULL);
    char *argv[] = { "box", "--gpu", "low-power", "--gpu", "nvidia" };
    test_int(box_parse_args(world, 5, argv), 0);
    test_str(ecs_singleton_get(world, App)->gpu, "nvidia");

    char *usage = box_args_usage(world, "box");
    test_assert(strstr(usage, "--gpu <gpu>") != NULL);
    ecs_os_free(usage);
    ecs_fini(world);
}
