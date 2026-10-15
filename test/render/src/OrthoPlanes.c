/**
 * @file OrthoPlanes.c
 * @brief Tests for cameras whose planes leave no depth range: far not beyond near, or a
 * perspective near plane at or behind the camera. They draw nothing and their projection
 * stays finite.
 * The captures look at a white cube 5 units in front of the camera.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void OrthoPlanes_inverted_planes_draw_nothing(void) {
    // render_fixes/render_minor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_log_set_level(-4);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 10, .far_ = 1});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}});
    ecs_entity_t cube = ecs_new(world);
    ecs_set(world, cube, Box, {8, 8, 1});
    ecs_set(world, cube, Rgb, {255, 255, 255});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 16, .height = 16, .resolution_scale = 1,
        .write_to_file = "inverted.png"});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_log_set_level(-1);

    char *path = flecs_asprintf("%s/inverted.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(8 * 16 + 8) * 4] < 10);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void OrthoPlanes_equal_planes_projection_is_finite(void) {
    // render_fixes/render_minor
    Camera camera = {.ortho = true, .ortho_height = 10, .near_ = 5, .far_ = 5};
    mat4 proj;
    vec4 point = {0, 0, -5, 1}, out;
    int32_t i, j;
    box_camera_projection(&camera, 100, 100, proj);
    for (i = 0; i < 4; i ++) {
        for (j = 0; j < 4; j ++) {
            test_assert(isfinite(proj[i][j]));
        }
    }

    glm_mat4_mulv(proj, point, out);
    test_assert(out[2] < 0 || out[2] > out[3]);
}

void OrthoPlanes_valid_planes_after_invalid_draw(void) {
    // render_fixes/render_minor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_log_set_level(-4);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 3, .far_ = 3});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}});
    ecs_entity_t cube = ecs_new(world);
    ecs_set(world, cube, Box, {8, 8, 1});
    ecs_set(world, cube, Rgb, {255, 255, 255});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 16, .height = 16, .resolution_scale = 1,
        .write_to_file = "valid.png"});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_log_set_level(-1);
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 1, .far_ = 10});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/valid.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(8 * 16 + 8) * 4] > 200);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void OrthoPlanes_perspective_bad_planes_draw_nothing(void) {
    // render_fixes/render_minor
    float planes[2][2] = {{10, 1}, {0, 100}};
    int32_t k;
    for (k = 0; k < 2; k ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);

        ecs_log_set_level(-4);
        ecs_entity_t camera = ecs_new(world);
        ecs_set(world, camera, Position, {0, 0, 5});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = planes[k][0],
            .far_ = planes[k][1]});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
            .color = {0, 0, 0, 255}}});
        ecs_entity_t cube = ecs_new(world);
        ecs_set(world, cube, Box, {2, 2, 2});
        ecs_set(world, cube, Rgb, {255, 255, 255});
        ecs_entity_t s = ecs_new(world);
        ecs_set(world, s, Surface, {.width = 16, .height = 16, .resolution_scale = 1,
            .write_to_file = "perspective.png"});
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        ecs_log_set_level(-1);

        char *path = flecs_asprintf("%s/perspective.png",
            ecs_singleton_get(world, AppStatus)->tmp_dir);
        int w = 0, h = 0, n = 0;
        unsigned char *px = stbi_load(path, &w, &h, &n, 4);
        test_assert(px != NULL);
        test_assert(px[(8 * 16 + 8) * 4] < 10);
        stbi_image_free(px);
        ecs_os_free(path);
        ecs_fini(world);
    }
}

void OrthoPlanes_empty_depth_view_draws_no_instances(void) {
    // render_fixes/render_minor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_log_set_level(-4);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 10, .far_ = 1});
    ecs_log_set_level(-1);
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}});
    ecs_entity_t cube = ecs_new(world);
    ecs_set(world, cube, Box, {8, 8, 1});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 16, .height = 16, .resolution_scale = 1});
    uint32_t word = 0;
    int32_t i;
    for (i = 0; i < 50 && !ecs_has(world, view, CullStatus); i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
    }

    const CullStatus *st = ecs_get(world, view, CullStatus);
    test_assert(st != NULL);
    test_int(st->visible, 0);
    test_int(st->instances, 0);

    ecs_fini(world);
}
