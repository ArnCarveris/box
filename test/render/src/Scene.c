/**
 * @file Scene.c
 * @brief Scene isolation: membership, nested scenes, reparenting and the lights of a scene.
 *
 * Membership cases look through an unlit orthographic camera 10 units in front of the origin
 * into a 32x32 capture of 8 units: world x = -2 lands on pixel column 8, x = 0 on 16 and
 * x = 2 on 24. Light cases look at a white wall that fills a 64x32 capture.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Scene_view_renders_scene_of_camera(void) {
    // scenes/isolation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Scene, {0});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Scene, {0});
    ecs_entity_t cam_a = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, cam_a, Position, {0, 0, 10});
    ecs_set(world, cam_a, Camera, {.ortho = true, .ortho_height = 8});
    ecs_entity_t cam_b = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, cam_b, Position, {0, 0, 10});
    ecs_set(world, cam_b, Camera, {.ortho = true, .ortho_height = 8});
    ecs_entity_t cam_default = ecs_new(world);
    ecs_set(world, cam_default, Position, {0, 0, 10});
    ecs_set(world, cam_default, Camera, {.ortho = true, .ortho_height = 8});
    ecs_entity_t red = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, red, Position, {-2, 0, 0});
    ecs_set(world, red, Box, {1, 1, 1});
    ecs_set(world, red, Rgb, {255, 0, 0});
    ecs_entity_t green = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, green, Position, {2, 0, 0});
    ecs_set(world, green, Box, {1, 1, 1});
    ecs_set(world, green, Rgb, {0, 255, 0});
    ecs_entity_t blue = ecs_new(world);
    ecs_set(world, blue, Position, {0, 0, 0});
    ecs_set(world, blue, Box, {1, 1, 1});
    ecs_set(world, blue, Rgb, {0, 0, 255});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = cam_a,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 32, .write_to_file = "scene.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/scene.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] > 200);
    test_assert(px[(16 * 32 + 16) * 4 + 2] < 40);
    test_assert(px[(16 * 32 + 24) * 4 + 1] < 40);
    stbi_image_free(px);

    ecs_set(world, view, View, {.camera = cam_b,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] < 40);
    test_assert(px[(16 * 32 + 16) * 4 + 2] < 40);
    test_assert(px[(16 * 32 + 24) * 4 + 1] > 200);
    stbi_image_free(px);

    ecs_set(world, view, View, {.camera = cam_default,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] < 40);
    test_assert(px[(16 * 32 + 16) * 4 + 2] > 200);
    test_assert(px[(16 * 32 + 24) * 4 + 1] < 40);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Scene_two_views_two_scenes(void) {
    // scenes/isolation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Scene, {0});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Scene, {0});
    ecs_entity_t cam_a = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, cam_a, Position, {0, 0, 10});
    ecs_set(world, cam_a, Camera, {.ortho = true, .ortho_height = 8});
    ecs_entity_t cam_b = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, cam_b, Position, {4, 0, 10});
    ecs_set(world, cam_b, Camera, {.ortho = true, .ortho_height = 8});
    ecs_entity_t red = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, red, Position, {2, 0, 1});
    ecs_set(world, red, Box, {1, 1, 1});
    ecs_set(world, red, Rgb, {255, 0, 0});
    ecs_entity_t green = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, green, Position, {2, 0, 0});
    ecs_set(world, green, Box, {1, 1, 1});
    ecs_set(world, green, Rgb, {0, 255, 0});
    ecs_entity_t center = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, center, Position, {0, 0, 0});
    ecs_set(world, center, Box, {1, 1, 1});
    ecs_set(world, center, Rgb, {0, 255, 0});
    ecs_entity_t view_a = ecs_new(world);
    ecs_set(world, view_a, View, {.camera = cam_a,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t view_b = ecs_new(world);
    ecs_set(world, view_b, View, {.camera = cam_b, .lighting.enabled = false});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 32, .write_to_file = "scene.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/scene.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *left = &px[(16 * 32 + 8) * 4];
    unsigned char *middle = &px[(16 * 32 + 16) * 4];
    unsigned char *right = &px[(16 * 32 + 24) * 4];
    test_assert(left[0] < 40);
    test_assert(left[1] > 200);
    test_assert(middle[0] < 40);
    test_assert(middle[1] < 40);
    test_assert(right[0] > 200);
    test_assert(right[1] < 40);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Scene_nested_scene_nearest_ancestor(void) {
    // scenes/isolation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t outer = ecs_new(world);
    ecs_set(world, outer, Scene, {0});
    ecs_entity_t inner = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_set(world, inner, Scene, {0});
    ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, inner);
    ecs_entity_t cam_outer = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_set(world, cam_outer, Position, {0, 0, 10});
    ecs_set(world, cam_outer, Camera, {.ortho = true, .ortho_height = 8});
    ecs_entity_t cam_inner = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, cam_inner, Position, {0, 0, 10});
    ecs_set(world, cam_inner, Camera, {.ortho = true, .ortho_height = 8});
    ecs_entity_t red = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_set(world, red, Position, {-2, 0, 0});
    ecs_set(world, red, Box, {1, 1, 1});
    ecs_set(world, red, Rgb, {255, 0, 0});
    ecs_entity_t green = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, green, Position, {2, 0, 0});
    ecs_set(world, green, Box, {1, 1, 1});
    ecs_set(world, green, Rgb, {0, 255, 0});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = cam_outer,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 32, .write_to_file = "scene.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/scene.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] > 200);
    test_assert(px[(16 * 32 + 24) * 4 + 1] < 40);
    stbi_image_free(px);

    ecs_set(world, view, View, {.camera = cam_inner,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] < 40);
    test_assert(px[(16 * 32 + 24) * 4 + 1] > 200);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Scene_reparent_moves_subtree(void) {
    // scenes/isolation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Scene, {0});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Scene, {0});
    ecs_entity_t cam_a = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, cam_a, Position, {0, 0, 10});
    ecs_set(world, cam_a, Camera, {.ortho = true, .ortho_height = 8});
    ecs_entity_t cam_b = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, cam_b, Position, {0, 0, 10});
    ecs_set(world, cam_b, Camera, {.ortho = true, .ortho_height = 8});
    ecs_entity_t cam_default = ecs_new(world);
    ecs_set(world, cam_default, Position, {0, 0, 10});
    ecs_set(world, cam_default, Camera, {.ortho = true, .ortho_height = 8});
    ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, group, Position, {0, 0, 0});
    ecs_entity_t red = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, red, Position, {-2, 0, 0});
    ecs_set(world, red, Box, {1, 1, 1});
    ecs_set(world, red, Rgb, {255, 0, 0});
    ecs_entity_t green = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_add_id(world, green, DynamicTransform);
    ecs_set(world, green, Position, {2, 0, 0});
    ecs_set(world, green, Box, {1, 1, 1});
    ecs_set(world, green, Rgb, {0, 255, 0});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = cam_a,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 32, .write_to_file = "scene.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/scene.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] > 200);
    test_assert(px[(16 * 32 + 24) * 4 + 1] > 200);
    stbi_image_free(px);

    ecs_add_pair(world, group, EcsChildOf, b);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] < 40);
    test_assert(px[(16 * 32 + 24) * 4 + 1] < 40);
    stbi_image_free(px);

    ecs_set(world, view, View, {.camera = cam_b,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] > 200);
    test_assert(px[(16 * 32 + 24) * 4 + 1] > 200);
    stbi_image_free(px);

    ecs_remove_pair(world, group, EcsChildOf, EcsWildcard);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] < 40);
    test_assert(px[(16 * 32 + 24) * 4 + 1] < 40);
    stbi_image_free(px);

    ecs_set(world, view, View, {.camera = cam_default,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] > 200);
    test_assert(px[(16 * 32 + 24) * 4 + 1] > 200);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Scene_reparent_parent_storage(void) {
    // scenes/isolation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Scene, {0});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Scene, {0});
    ecs_entity_t cam_a = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, cam_a, Position, {0, 0, 10});
    ecs_set(world, cam_a, Camera, {.ortho = true, .ortho_height = 8});
    ecs_entity_t cam_b = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, cam_b, Position, {0, 0, 10});
    ecs_set(world, cam_b, Camera, {.ortho = true, .ortho_height = 8});
    ecs_entity_t group = ecs_new_w_parent(world, a, NULL);
    ecs_set(world, group, Position, {0, 0, 0});
    ecs_entity_t red = ecs_new_w_parent(world, group, NULL);
    ecs_set(world, red, Position, {-2, 0, 0});
    ecs_set(world, red, Box, {1, 1, 1});
    ecs_set(world, red, Rgb, {255, 0, 0});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = cam_a,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 32, .write_to_file = "scene.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/scene.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] > 200);
    stbi_image_free(px);

    ecs_set(world, group, EcsParent, {b});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] < 40);
    stbi_image_free(px);

    ecs_set(world, view, View, {.camera = cam_b,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] > 200);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Scene_scene_added_and_removed(void) {
    // scenes/isolation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t group = ecs_new(world);
    ecs_entity_t cam_group = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, cam_group, Position, {0, 0, 10});
    ecs_set(world, cam_group, Camera, {.ortho = true, .ortho_height = 8});
    ecs_entity_t cam_default = ecs_new(world);
    ecs_set(world, cam_default, Position, {0, 0, 10});
    ecs_set(world, cam_default, Camera, {.ortho = true, .ortho_height = 8});
    ecs_entity_t red = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, red, Position, {-2, 0, 0});
    ecs_set(world, red, Box, {1, 1, 1});
    ecs_set(world, red, Rgb, {255, 0, 0});
    ecs_entity_t green = ecs_new(world);
    ecs_set(world, green, Position, {2, 0, 0});
    ecs_set(world, green, Box, {1, 1, 1});
    ecs_set(world, green, Rgb, {0, 255, 0});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = cam_default,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 32, .write_to_file = "scene.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/scene.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] > 200);
    test_assert(px[(16 * 32 + 24) * 4 + 1] > 200);
    stbi_image_free(px);

    ecs_set(world, group, Scene, {0});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] < 40);
    test_assert(px[(16 * 32 + 24) * 4 + 1] > 200);
    stbi_image_free(px);

    ecs_set(world, view, View, {.camera = cam_group,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] > 200);
    test_assert(px[(16 * 32 + 24) * 4 + 1] < 40);
    stbi_image_free(px);

    ecs_remove(world, group, Scene);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] > 200);
    test_assert(px[(16 * 32 + 24) * 4 + 1] > 200);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Scene_camera_reparented_switches_scene(void) {
    // scenes/isolation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Scene, {0});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Scene, {0});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 8});
    ecs_entity_t red = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, red, Position, {-2, 0, 0});
    ecs_set(world, red, Box, {1, 1, 1});
    ecs_set(world, red, Rgb, {255, 0, 0});
    ecs_entity_t green = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, green, Position, {2, 0, 0});
    ecs_set(world, green, Box, {1, 1, 1});
    ecs_set(world, green, Rgb, {0, 255, 0});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 32, .write_to_file = "scene.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/scene.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] > 200);
    test_assert(px[(16 * 32 + 24) * 4 + 1] < 40);
    stbi_image_free(px);

    ecs_add_pair(world, camera, EcsChildOf, b);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] < 40);
    test_assert(px[(16 * 32 + 24) * 4 + 1] > 200);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Scene_lights_isolated_in_overlapping_scenes(void) {
    // scenes/isolation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Scene, {0});
    ecs_set(world, a, AmbientLight, {.intensity = 0});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Scene, {0});
    ecs_set(world, b, AmbientLight, {.intensity = 0});
    ecs_entity_t cam_a = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, cam_a, Position, {0, 0, 6});
    ecs_set(world, cam_a, Camera, {.fov = 60});
    ecs_entity_t cam_b = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, cam_b, Position, {0, 0, 6});
    ecs_set(world, cam_b, Camera, {.fov = 60});
    ecs_entity_t wall_a = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, wall_a, Position, {0, 0, -1});
    ecs_set(world, wall_a, Box, {20, 20, 0.1f});
    ecs_set(world, wall_a, Rgb, {255, 255, 255});
    ecs_set(world, wall_a, PbrMaterial, {.roughness = 1});
    ecs_entity_t wall_b = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, wall_b, Position, {0, 0, -1});
    ecs_set(world, wall_b, Box, {20, 20, 0.1f});
    ecs_set(world, wall_b, Rgb, {255, 255, 255});
    ecs_set(world, wall_b, PbrMaterial, {.roughness = 1});
    ecs_entity_t light_a = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, light_a, Position, {0, 0, 0});
    ecs_set(world, light_a, Rgb, {255, 0, 0});
    ecs_set(world, light_a, PointLight, {.intensity = 4, .range = 3});
    ecs_entity_t light_b = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, light_b, Position, {0, 0, 0});
    ecs_set(world, light_b, Rgb, {0, 0, 255});
    ecs_set(world, light_b, PointLight, {.intensity = 4, .range = 3});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = cam_a,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 32, .write_to_file = "lit.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/lit.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(16 * 64 + 32) * 4];
    test_assert(c[0] > 100);
    test_assert(c[1] < 8);
    test_assert(c[2] < 8);
    stbi_image_free(px);

    ecs_set(world, view, View, {.camera = cam_b,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(16 * 64 + 32) * 4];
    test_assert(c[0] < 8);
    test_assert(c[1] < 8);
    test_assert(c[2] > 100);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Scene_light_follows_reparent(void) {
    // scenes/isolation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Scene, {0});
    ecs_set(world, a, AmbientLight, {.intensity = 0});
    ecs_set(world, a, DirectionalLight, {.intensity = 0});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Scene, {0});
    ecs_set(world, b, AmbientLight, {.intensity = 0});
    ecs_set(world, b, DirectionalLight, {.intensity = 0});
    ecs_entity_t cam_a = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, cam_a, Position, {0, 0, 6});
    ecs_set(world, cam_a, Camera, {.fov = 60});
    ecs_entity_t cam_b = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, cam_b, Position, {0, 0, 6});
    ecs_set(world, cam_b, Camera, {.fov = 60});
    ecs_entity_t wall_a = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, wall_a, Position, {0, 0, -1});
    ecs_set(world, wall_a, Box, {20, 20, 0.1f});
    ecs_set(world, wall_a, Rgb, {255, 255, 255});
    ecs_set(world, wall_a, PbrMaterial, {.roughness = 1});
    ecs_entity_t wall_b = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, wall_b, Position, {0, 0, -1});
    ecs_set(world, wall_b, Box, {20, 20, 0.1f});
    ecs_set(world, wall_b, Rgb, {255, 255, 255});
    ecs_set(world, wall_b, PbrMaterial, {.roughness = 1});
    ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_set(world, group, Position, {0, 0, 0});
    ecs_entity_t light = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, light, Position, {0, 0, 0});
    ecs_set(world, light, PointLight, {.intensity = 4, .range = 3});
    ecs_entity_t view_a = ecs_new(world);
    ecs_set(world, view_a, View, {.camera = cam_a,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 32, .write_to_file = "lit.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/lit.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 64 + 32) * 4] > 100);
    stbi_image_free(px);

    ecs_add_pair(world, group, EcsChildOf, b);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 64 + 32) * 4] < 8);
    stbi_image_free(px);

    ecs_set(world, view_a, View, {.camera = cam_b,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 64 + 32) * 4] > 100);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Scene_light_moves_with_scene_added(void) {
    // scenes/isolation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t dark = ecs_new(world);
    ecs_set(world, dark, DirectionalLight, {.intensity = 0});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {255, 255, 255});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});
    ecs_entity_t group = ecs_new(world);
    ecs_entity_t mid = ecs_new_w_parent(world, group, NULL);
    ecs_entity_t light = ecs_new_w_parent(world, mid, NULL);
    ecs_set(world, light, Position, {0, 0, 0});
    ecs_set(world, light, PointLight, {.intensity = 4, .range = 3});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 32, .write_to_file = "lit.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/lit.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 64 + 32) * 4] > 100);
    stbi_image_free(px);

    ecs_set(world, group, Scene, {0});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 64 + 32) * 4] < 8);
    stbi_image_free(px);

    ecs_remove(world, group, Scene);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 64 + 32) * 4] > 100);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}
