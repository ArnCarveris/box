/**
 * @file Args.c
 * @brief Tests for parsing the command line into the App singleton.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

void Args_defaults(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun"};
    test_int(box_app_parse(world, 1, argv), 0);

    const App *app = ecs_singleton_get(world, App);
    test_assert(app != NULL);
    test_bool(app->headless, false);
    test_bool(app->unguarded, false);
    test_int(app->frames, 0);
    test_flt(app->timeout, 0);
    test_flt(app->cpu_budget, 100);
    test_flt(app->fps, 0);
    test_flt(app->delta_time, 0);
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

void Args_no_audio(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_bool(ecs_singleton_get(world, App)->no_audio, false);
    char *argv[] = {"boxrun", "--no-audio"};
    test_int(box_app_parse(world, 2, argv), 0);

    const App *app = ecs_singleton_get(world, App);
    test_bool(app->no_audio, true);
    test_bool(app->headless, false);

    ecs_fini(world);
}

void Args_unguarded(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_bool(ecs_singleton_get(world, App)->unguarded, false);
    char *argv[] = {"boxrun", "--headless", "--unguarded"};
    test_int(box_app_parse(world, 3, argv), 0);

    const App *app = ecs_singleton_get(world, App);
    test_bool(app->unguarded, true);
    test_bool(app->headless, true);

    ecs_fini(world);
}

void Args_async_loads(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_bool(ecs_singleton_get(world, App)->async_loads, false);
    char *argv[] = {"boxrun", "--headless", "--async-loads"};
    test_int(box_app_parse(world, 3, argv), 0);

    const App *app = ecs_singleton_get(world, App);
    test_bool(app->async_loads, true);
    test_bool(app->headless, true);

    ecs_fini(world);
}

void Args_fail_on_error(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_bool(ecs_singleton_get(world, App)->fail_on_error, false);
    char *argv[] = {"boxrun", "--headless", "--fail-on-error"};
    test_int(box_app_parse(world, 3, argv), 0);

    const App *app = ecs_singleton_get(world, App);
    test_bool(app->fail_on_error, true);
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

void Args_delta_time_fraction(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--delta-time", "1/60"};
    test_int(box_app_parse(world, 3, argv), 0);
    test_flt(ecs_singleton_get(world, App)->delta_time, 1.0f / 60.0f);

    char *argv_decimal[] = {"boxrun", "--delta-time", "0.5"};
    test_int(box_app_parse(world, 3, argv_decimal), 0);
    test_flt(ecs_singleton_get(world, App)->delta_time, 0.5f);

    ecs_fini(world);
}

void Args_invalid_delta_time(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_log_set_level(-4);

    char *argv_zero[] = {"boxrun", "--delta-time", "1/0"};
    test_assert(box_app_parse(world, 3, argv_zero) != 0);

    char *argv_negative[] = {"boxrun", "--delta-time", "-1/60"};
    test_assert(box_app_parse(world, 3, argv_negative) != 0);

    char *argv_missing[] = {"boxrun", "--delta-time", "1/"};
    test_assert(box_app_parse(world, 3, argv_missing) != 0);

    char *argv_text[] = {"boxrun", "--delta-time", "abc"};
    test_assert(box_app_parse(world, 3, argv_text) != 0);

    test_flt(ecs_singleton_get(world, App)->delta_time, 0);

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

    char *argv[] = {"boxrun", "scenes/scene.flecs"};
    test_int(box_app_parse(world, 2, argv), 0);

    const App *app = ecs_singleton_get(world, App);
    test_str(app->scene, "scenes/scene.flecs");

    ecs_fini(world);
}

void Args_all_options(void) {
    // engine_core/app
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--headless", "--frames", "5", "--timeout", "30",
        "--cpu-budget", "50", "--fps", "30", "--delta-time", "0.025", "--port", "8000",
        "scene.flecs"};
    test_int(box_app_parse(world, 15, argv), 0);

    const App *app = ecs_singleton_get(world, App);
    test_bool(app->headless, true);
    test_int(app->frames, 5);
    test_flt(app->timeout, 30);
    test_flt(app->cpu_budget, 50);
    test_flt(app->fps, 30);
    test_flt(app->delta_time, 0.025f);
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

void Args_surface(void) {
    // test_fixes/surface_option
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *argv[] = {"boxrun", "--surface", "800x600"};
    test_int(box_app_parse(world, 3, argv), 0);

    const App *app = ecs_singleton_get(world, App);
    test_int(app->surface_width, 800);
    test_int(app->surface_height, 600);

    ecs_fini(world);
}

void Args_invalid_surface(void) {
    // test_fixes/surface_option
    const char *values[] = {"800", "800x", "x600", "0x600", "800x0", "-800x600", "800x-600",
        "800x600x2", "800X600", " 800x600", "8193x600", "600x8193", "abc", ""};
    int32_t i;
    ecs_log_set_level(-4);
    for (i = 0; i < (int32_t)(sizeof(values) / sizeof(values[0])); i ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);

        char *argv[] = {"boxrun", "--surface", ECS_CONST_CAST(char*, values[i])};
        test_assert(box_app_parse(world, 3, argv) != 0);

        const App *app = ecs_singleton_get(world, App);
        test_int(app->surface_width, 0);
        test_int(app->surface_height, 0);

        ecs_fini(world);
    }
}

void Args_surface_created_when_none(void) {
    // test_fixes/surface_option
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    char *argv[] = {"boxrun", "--headless", "--frames", "3", "--surface", "320x200"};
    test_int(box_app_parse(world, 6, argv), 0);
    App *app = ecs_singleton_get_mut(world, App);
    app->unguarded = true;
    app->unpaced = true;
    test_int(box_app_run(world), 0);

    test_int(ecs_count(world, Surface), 1);
    ecs_iter_t it = ecs_each(world, Surface);
    test_bool(ecs_each_next(&it), true);
    const Surface *s = ecs_field(&it, Surface, 0);
    test_int(s[0].width, 320);
    test_int(s[0].height, 200);
    test_flt(s[0].resolution_scale, 1);
    test_flt(s[0].pixel_ratio, 1);
    ecs_iter_fini(&it);

    ecs_fini(world);
    box_render_null_free(null);
}

void Args_surface_not_created_when_present(void) {
    // test_fixes/surface_option
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t own = ecs_entity(world, { .name = "own" });
    Surface *own_s = ecs_ensure(world, own, Surface);
    own_s->width = 64;
    own_s->height = 48;
    ecs_modified(world, own, Surface);

    char *argv[] = {"boxrun", "--headless", "--frames", "3", "--surface", "320x200"};
    test_int(box_app_parse(world, 6, argv), 0);
    App *app = ecs_singleton_get_mut(world, App);
    app->unguarded = true;
    app->unpaced = true;
    test_int(box_app_run(world), 0);

    test_int(ecs_count(world, Surface), 1);
    const Surface *s = ecs_get(world, own, Surface);
    test_int(s->width, 320);
    test_int(s->height, 200);

    ecs_fini(world);
    box_render_null_free(null);
}

void Args_surface_created_next_to_texture_surface(void) {
    // test_fixes/surface_option
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t tex = ecs_entity(world, { .name = "screen_texture" });
    ecs_entity_t screen = ecs_entity(world, { .name = "screen" });
    Surface *screen_s = ecs_ensure(world, screen, Surface);
    screen_s->width = 64;
    screen_s->height = 48;
    screen_s->texture = tex;
    ecs_modified(world, screen, Surface);

    char *argv[] = {"boxrun", "--headless", "--frames", "3", "--surface", "320x200"};
    test_int(box_app_parse(world, 6, argv), 0);
    App *app = ecs_singleton_get_mut(world, App);
    app->unguarded = true;
    app->unpaced = true;
    test_int(box_app_run(world), 0);

    test_int(ecs_count(world, Surface), 2);
    int32_t windows = 0, i;
    ecs_iter_t it = ecs_each(world, Surface);
    while (ecs_each_next(&it)) {
        const Surface *s = ecs_field(&it, Surface, 0);
        for (i = 0; i < it.count; i ++) {
            if (!s[i].texture) {
                windows ++;
                test_int(s[i].width, 320);
                test_int(s[i].height, 200);
            }
        }
    }

    test_int(windows, 1);
    test_int(ecs_get(world, screen, Surface)->width, 64);

    ecs_fini(world);
    box_render_null_free(null);
}

void Args_surface_first_frame(void) {
    // test_fixes/surface_option
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    char *argv[] = {"boxrun", "--headless", "--frames", "1", "--surface", "320x200"};
    test_int(box_app_parse(world, 6, argv), 0);
    App *app = ecs_singleton_get_mut(world, App);
    app->unguarded = true;
    app->unpaced = true;
    test_int(box_app_run(world), 0);

    test_int(ecs_count(world, Surface), 1);
    ecs_iter_t it = ecs_each(world, Surface);
    test_bool(ecs_each_next(&it), true);
    ecs_entity_t e = it.entities[0];
    ecs_iter_fini(&it);

    const SurfaceStatus *status = ecs_get(world, e, SurfaceStatus);
    test_assert(status != NULL);
    test_int(status->point_width, 320);
    test_int(status->point_height, 200);

    ecs_fini(world);
    box_render_null_free(null);
}

void Args_out(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *argv[] = {"boxrun", "--out", "model.png"};
    test_int(box_app_parse(world, 3, argv), 0);
    test_str(ecs_singleton_get(world, App)->out, "model.png");
    ecs_entity_t copy = ecs_new(world);
    ecs_set_ptr(world, copy, App, ecs_singleton_get(world, App));
    test_assert(ecs_get(world, copy, App)->out != ecs_singleton_get(world, App)->out);
    test_str(ecs_get(world, copy, App)->out, "model.png");
    char *repeat[] = {"boxrun", "--out", "other"};
    test_int(box_app_parse(world, 3, repeat), 0);
    test_str(ecs_singleton_get(world, App)->out, "other");
    test_str(ecs_get(world, copy, App)->out, "model.png");
    ecs_fini(world);
}

void Args_invalid_out(void) {
    char *args[][3] = {
        {"boxrun", "--out"},
        {"boxrun", "--out", ""},
        {"boxrun", "--out", "--design"}
    };
    ecs_log_set_level(-4);
    for (int i = 0; i < 3; i ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        test_assert(box_app_parse(world, i ? 3 : 2, args[i]) != 0);
        ecs_fini(world);
    }
}

void Args_out_capture(void) {
    for (int i = 0; i < 3; i ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        box_render_null_t *null = box_render_null_new();
        box_render_backend_t backend = box_render_null_backend(null);
        box_render_set_backend(world, &backend);
        if (i) {
            ecs_entity_t surface = ecs_new(world);
            ecs_set(world, surface, Surface, {
                .width = 64, .height = 48, .write_to_file = "old.png"
            });
        }
        char *argv[] = {"boxrun", "--headless", "--frames", "1", "--port", "0",
            "--out", i == 2 ? "/tmp/box-out-test.png" : "model"};
        test_int(box_app_parse(world, 8, argv), 0);
        App *app = ecs_singleton_get_mut(world, App);
        app->unguarded = true;
        app->unpaced = true;
        test_int(box_app_run(world), 0);
        test_int(ecs_count(world, Surface), 1);
        ecs_iter_t it = ecs_each(world, Surface);
        test_assert(ecs_each_next(&it));
        const Surface *surface = ecs_field(&it, Surface, 0);
        test_assert(surface->write_to_file[0] == '/');
        if (i == 2) {
            test_str(surface->write_to_file, "/tmp/box-out-test.png");
        } else {
            size_t length = strlen(surface->write_to_file);
            test_str(surface->write_to_file + length - 10, "/model.png");
        }
        ecs_iter_fini(&it);
        ecs_fini(world);
        box_render_null_free(null);
    }
}

static void surface_scene_run(
    const char *size,
    int32_t width,
    int32_t height)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    char *argv[] = {"boxrun", "--headless", "--frames", "1",
        "test/assets/app/surface_window.flecs", "--surface", ECS_CONST_CAST(char*, size)};
    int argc = size ? 7 : 5;

    test_int(box_app_parse(world, argc, argv), 0);
    App *app = ecs_singleton_get_mut(world, App);
    app->unguarded = true;
    app->unpaced = true;
    test_int(box_app_run(world), 0);

    ecs_entity_t window = ecs_lookup(world, "window");
    test_assert(window != 0);
    test_int(ecs_count(world, Surface), 1);
    const Surface *s = ecs_get(world, window, Surface);
    test_assert(s != NULL);
    test_int(s->width, width);
    test_int(s->height, height);

    /* A later write of the Surface, as a reload does, keeps the size of the command line */
    ecs_set(world, window, Surface, {.width = 800, .height = 600});
    ecs_progress(world, 0);
    s = ecs_get(world, window, Surface);
    test_int(s->width, size ? width : 800);
    test_int(s->height, size ? height : 600);

    ecs_fini(world);
    box_render_null_free(null);
}

void Args_surface_overrides_scene_window(void) {
    // test_fixes/surface_arg_override
    surface_scene_run("1280x720", 1280, 720);
}

void Args_surface_scene_window_without_option(void) {
    // test_fixes/surface_arg_override
    surface_scene_run(NULL, 640, 360);
}
