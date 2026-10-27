/**
 * @file Light.c
 * @brief Tests for directional, point, spot and ambient lights and their scenes.
 *
 * Most cases look at a white wall that fills a 64x32 capture, 6.95 units in front of the
 * camera: world x = -3 lands on pixel column 20, x = 3 on column 44.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <math.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Light_point_light_reaches_its_range(void) {
    // lighting/lights
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {255, 255, 255});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {-3, 0, 0});
    ecs_set(world, light, PointLight, {.intensity = 4 * GLM_PIf, .range = 3});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "point.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/point.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *near_ = &px[(16 * 64 + 20) * 4];
    unsigned char *far_ = &px[(16 * 64 + 44) * 4];
    test_assert(near_[0] > 200);
    test_assert(near_[1] > 200);
    test_assert(near_[2] > 200);
    test_assert(far_[0] < 8);
    test_assert(far_[1] < 8);
    test_assert(far_[2] < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Light_point_light_color(void) {
    // lighting/lights
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {255, 255, 255});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});
    ecs_entity_t red = ecs_new(world);
    ecs_set(world, red, Position, {-3, 0, 0});
    ecs_set(world, red, PointLight, {.intensity = 2 * GLM_PIf, .range = 3});
    ecs_set(world, red, Rgb, {255, 0, 0});
    ecs_entity_t blue = ecs_new(world);
    ecs_set(world, blue, Position, {3, 0, 0});
    ecs_set(world, blue, PointLight, {.intensity = 2 * GLM_PIf, .range = 3});
    ecs_set(world, blue, Rgb, {0, 0, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "color.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/color.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *left = &px[(16 * 64 + 20) * 4];
    unsigned char *right = &px[(16 * 64 + 44) * 4];
    test_assert(left[0] > 150);
    test_assert(left[1] < 10);
    test_assert(left[2] < 10);
    test_assert(right[2] > 150);
    test_assert(right[0] < 10);
    test_assert(right[1] < 10);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Light_spot_light_cone(void) {
    // lighting/lights
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {255, 255, 255});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});
    ecs_entity_t spot = ecs_new(world);
    ecs_set(world, spot, Position, {0, 0, 2});
    ecs_set(world, spot, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, spot, SpotLight, {
        .intensity = 20 * GLM_PIf, .range = 10, .inner_angle = 10, .outer_angle = 15});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "spot.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/spot.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *center = &px[(16 * 64 + 32) * 4];
    unsigned char *outside = &px[(16 * 64 + 40) * 4];
    unsigned char *above = &px[(4 * 64 + 32) * 4];
    test_assert(center[0] > 200);
    test_assert(outside[0] < 8);
    test_assert(above[0] < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Light_directional_light_direction(void) {
    // lighting/lights
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {128, 128, 128});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0, (float)GLM_PI, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "sun.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/sun.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(16 * 64 + 32) * 4];
    test_assert(c[0] > 110 && c[0] < 150);
    test_assert(abs((int)c[0] - (int)c[1]) < 4);
    test_assert(abs((int)c[0] - (int)c[2]) < 4);
    stbi_image_free(px);

    ecs_set(world, sun, Rotation, {0, 0, 0});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(16 * 64 + 32) * 4];
    test_assert(c[0] < 8);
    test_assert(c[1] < 8);
    test_assert(c[2] < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Light_lights_replace_default_sun(void) {
    // lighting/lights
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {255, 255, 255});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "default.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/default.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(16 * 64 + 32) * 4];
    test_assert(c[0] > 100);
    stbi_image_free(px);

    ecs_entity_t away = ecs_new(world);
    ecs_set(world, away, DirectionalLight, {.intensity = GLM_PIf});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(16 * 64 + 32) * 4];
    test_assert(c[0] < 8);
    test_assert(c[1] < 8);
    test_assert(c[2] < 8);
    stbi_image_free(px);

    ecs_delete(world, away);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(16 * 64 + 32) * 4];
    test_assert(c[0] > 100);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Light_light_scoped_to_scene(void) {
    // lighting/lights
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t scene = ecs_new(world);
    ecs_set(world, scene, Scene, {0});
    ecs_set(world, scene, AmbientLight, {.intensity = 0});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {255, 255, 255});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});
    ecs_entity_t own = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, own, Position, {-3, 0, 0});
    ecs_set(world, own, PointLight, {.intensity = 4 * GLM_PIf, .range = 3});
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, Position, {3, 0, 0});
    ecs_set(world, other, PointLight, {.intensity = 4 * GLM_PIf, .range = 3});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "scene.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/scene.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *lit = &px[(16 * 64 + 20) * 4];
    unsigned char *dark = &px[(16 * 64 + 44) * 4];
    test_assert(lit[0] > 200);
    test_assert(dark[0] < 8);
    test_assert(dark[1] < 8);
    test_assert(dark[2] < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Light_nested_scene_keeps_its_lights(void) {
    // lighting/lights
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t outer = ecs_new(world);
    ecs_set(world, outer, Scene, {0});
    ecs_set(world, outer, AmbientLight, {.intensity = 0});
    ecs_entity_t inner = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_set(world, inner, Scene, {0});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {255, 255, 255});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});
    ecs_entity_t own = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_set(world, own, Position, {-3, 0, 0});
    ecs_set(world, own, PointLight, {.intensity = 4 * GLM_PIf, .range = 3});
    ecs_entity_t nested = ecs_new_w_pair(world, EcsChildOf, inner);
    ecs_set(world, nested, Position, {3, 0, 0});
    ecs_set(world, nested, PointLight, {.intensity = 4 * GLM_PIf, .range = 3});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "nested.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/nested.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *lit = &px[(16 * 64 + 20) * 4];
    unsigned char *dark = &px[(16 * 64 + 44) * 4];
    test_assert(lit[0] > 200);
    test_assert(dark[0] < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Light_lighting_disabled_is_unlit(void) {
    // lighting/lights
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {200, 100, 50});
    ecs_entity_t glow = ecs_new(world);
    ecs_set(world, glow, Position, {3, 0, 0});
    ecs_set(world, glow, Box, {1, 1, 0.1f});
    ecs_set(world, glow, Rgb, {0, 0, 0});
    ecs_set(world, glow, Emissive, {.strength = 1, .color = {0, 255, 0}});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {-3, 0, 0});
    ecs_set(world, light, PointLight, {.intensity = 4 * GLM_PIf, .range = 3});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "unlit.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/unlit.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *near_ = &px[(16 * 64 + 20) * 4];
    unsigned char *far_ = &px[(4 * 64 + 56) * 4];
    unsigned char *glowing = &px[(16 * 64 + 45) * 4];
    test_assert(abs((int)near_[0] - 200) <= 2);
    test_assert(abs((int)near_[1] - 100) <= 2);
    test_assert(abs((int)near_[2] - 50) <= 2);
    test_assert(abs((int)far_[0] - 200) <= 2);
    test_assert(abs((int)far_[1] - 100) <= 2);
    test_assert(abs((int)far_[2] - 50) <= 2);
    test_assert(glowing[0] < 4);
    test_assert(glowing[1] > 250);
    test_assert(glowing[2] < 4);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Light_ambient_light_of_scene(void) {
    // lighting/lights
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t scene = ecs_new(world);
    ecs_set(world, scene, Scene, {0});
    ecs_set(world, scene, AmbientLight, {
        .sky = {255, 255, 255}, .ground = {255, 255, 255}, .intensity = 1});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {128, 128, 128});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});
    ecs_entity_t away = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, away, DirectionalLight, {.intensity = GLM_PIf});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "ambient.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/ambient.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(16 * 64 + 32) * 4];
    test_assert(c[0] > 110);
    int bright = c[0];
    stbi_image_free(px);

    ecs_set(world, scene, AmbientLight, {
        .sky = {255, 255, 255}, .ground = {255, 255, 255}, .intensity = 0.25f});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(16 * 64 + 32) * 4];
    test_assert(c[0] > 8);
    test_assert(c[0] < bright - 30);
    stbi_image_free(px);

    ecs_set(world, scene, AmbientLight, {.intensity = 0});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(16 * 64 + 32) * 4];
    test_assert(c[0] < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Light_intensity_zero_lights_nothing(void) {
    // lighting/lights
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {255, 255, 255});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {-3, 0, 0});
    ecs_set(world, light, PointLight, {.intensity = 0, .range = 3});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "off.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/off.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *near_ = &px[(16 * 64 + 20) * 4];
    unsigned char *center = &px[(16 * 64 + 32) * 4];
    test_assert(near_[0] < 8);
    test_assert(center[0] < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Light_moved_light_follows_transform(void) {
    // lighting/lights
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {255, 255, 255});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {-3, 0, 0});
    ecs_entity_t light = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, light, Position, {0, 0, 0});
    ecs_set(world, light, PointLight, {.intensity = 4 * GLM_PIf, .range = 3});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "moved.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/moved.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 64 + 20) * 4] > 200);
    test_assert(px[(16 * 64 + 44) * 4] < 8);
    stbi_image_free(px);

    ecs_set(world, parent, Position, {3, 0, 0});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 64 + 20) * 4] < 8);
    test_assert(px[(16 * 64 + 44) * 4] > 200);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Light_many_point_lights(void) {
    // lighting/lights
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {255, 255, 255});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});
    int32_t x, y;
    for (x = 0; x < 40; x ++) {
        for (y = 0; y < 25; y ++) {
            ecs_entity_t light = ecs_new(world);
            ecs_set(world, light, Position, {-8 + x * 0.41f, -4.2f + y * 0.35f, 0});
            ecs_set(world, light, PointLight, {.intensity = 2 * GLM_PIf, .range = 1.2f});
        }
    }

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "many.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_lights_stats_t stats;
    box_render_lights_stats(world, &stats);
    test_int(stats.lights, 1000);
    char *path = flecs_asprintf("%s/many.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 64 + 2) * 4] > 60);
    test_assert(px[(16 * 64 + 61) * 4] > 60);
    test_assert(px[(2 * 64 + 32) * 4] > 60);
    test_assert(px[(29 * 64 + 32) * 4] > 60);
    test_assert(px[(16 * 64 + 32) * 4] > 60);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Light_unchanged_lights_upload_nothing(void) {
    // lighting/lights
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t scene = ecs_new(world);
    ecs_set(world, scene, Scene, {0});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {1, 2, 3});
    ecs_set(world, light, PointLight, {.intensity = GLM_PIf, .range = 5});
    ecs_entity_t other = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, other, SpotLight, {.intensity = GLM_PIf, .range = 5, .outer_angle = 30});

    box_render_lights_stats_t stats;
    ecs_progress(world, 0);
    box_render_lights_stats(world, &stats);
    test_int(stats.scenes, 2);
    test_int(stats.lights, 2);
    test_assert(stats.upload_bytes > 0);

    ecs_progress(world, 0);
    box_render_lights_stats(world, &stats);
    test_int(stats.upload_bytes, 0);

    ecs_set(world, light, Position, {4, 5, 6});
    ecs_progress(world, 0);
    box_render_lights_stats(world, &stats);
    test_assert(stats.upload_bytes > 0);

    ecs_progress(world, 0);
    box_render_lights_stats(world, &stats);
    test_int(stats.upload_bytes, 0);

    ecs_delete(world, other);
    ecs_progress(world, 0);
    box_render_lights_stats(world, &stats);
    test_int(stats.scenes, 1);
    test_int(stats.lights, 1);

    box_render_sync(world);
    ecs_fini(world);
}

void Light_component_defaults(void) {
    // lighting/lights
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "defaults",
        "using box.light\n"
        "using box.view\n"
        "point { PointLight: {range: 5} }\n"
        "spot { SpotLight: {intensity: 2} }\n"
        "sun { DirectionalLight: {} }\n"
        "ambient { AmbientLight: {intensity: 0.5} }\n"
        "view { View: {} }\n", NULL) == 0);

    const PointLight *point = ecs_get(world, ecs_lookup(world, "point"), PointLight);
    test_assert(point != NULL);
    test_flt(point->intensity, 1);
    test_flt(point->range, 5);
    const SpotLight *spot = ecs_get(world, ecs_lookup(world, "spot"), SpotLight);
    test_assert(spot != NULL);
    test_flt(spot->intensity, 2);
    test_flt(spot->range, 10);
    test_flt(spot->inner_angle, 20);
    test_flt(spot->outer_angle, 30);
    const DirectionalLight *sun = ecs_get(world, ecs_lookup(world, "sun"), DirectionalLight);
    test_assert(sun != NULL);
    test_flt(sun->intensity, 1);
    const AmbientLight *ambient = ecs_get(world, ecs_lookup(world, "ambient"), AmbientLight);
    test_assert(ambient != NULL);
    test_flt(ambient->intensity, 0.5f);
    test_assert(ambient->sky.b > ambient->sky.r);
    const View *view = ecs_get(world, ecs_lookup(world, "view"), View);
    test_assert(view != NULL);
    test_bool(view->lighting.enabled, true);

    ecs_fini(world);
}

void Light_cluster_lists_every_light(void) {
    // lighting_fixes/cluster_overflow
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {255, 255, 255});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});
    int32_t i;
    for (i = 0; i < 200; i ++) {
        ecs_entity_t light = ecs_new(world);
        ecs_set(world, light, Position, {-3, 0, 0});
        ecs_set(world, light, PointLight, {.intensity = 0.005f * GLM_PIf, .range = 2});
    }

    ecs_entity_t single = ecs_new(world);
    ecs_set(world, single, Position, {3, 0, 0});
    ecs_set(world, single, PointLight, {.intensity = GLM_PIf, .range = 2});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "crowd.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/crowd.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *crowd = &px[(16 * 64 + 20) * 4];
    unsigned char *reference = &px[(16 * 64 + 44) * 4];
    test_assert(reference[0] > 40);
    test_assert(reference[0] < 250);
    test_assert(abs(crowd[0] - reference[0]) < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Light_spot_light_lights_its_whole_cone(void) {
    // lighting_fixes/cluster_overflow
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {255, 255, 255});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});
    ecs_entity_t spot = ecs_new(world);
    ecs_set(world, spot, Position, {-1, 0, 2});
    ecs_set(world, spot, Rotation, {0, (float)GLM_PI - 0.5f, 0});
    ecs_set(world, spot, SpotLight, {
        .intensity = 40 * GLM_PIf, .range = 12, .inner_angle = 40, .outer_angle = 45});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "cone.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/cone.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *outside = &px[(16 * 64 + 21) * 4];
    unsigned char *edge = &px[(16 * 64 + 27) * 4];
    unsigned char *center = &px[(16 * 64 + 40) * 4];
    unsigned char *far = &px[(16 * 64 + 55) * 4];
    test_assert(outside[0] < 8);
    test_assert(edge[0] > 200);
    test_assert(center[0] > 200);
    test_assert(far[0] > 100);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Light_point_light_inverse_square(void) {
    // lighting_fixes/point_falloff
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {255, 255, 255});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, PointLight, {.intensity = 0.15f * GLM_PIf, .range = 40});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "falloff.png",
        .resolution_scale = 1});
    char *path = flecs_asprintf("%s/falloff.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    const float distances[3] = {0.5f, 1, 2};
    double radiance[3];
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_set(world, light, Position, {0.125f, -0.125f, -0.95f + distances[i]});
        ecs_progress(world, 0);
        box_render_sync(world);
        int w = 0, h = 0, n = 0;
        unsigned char *px = stbi_load(path, &w, &h, &n, 4);
        test_assert(px != NULL);
        double v = (double)px[(16 * 64 + 32) * 4 + 1] / 255.0;
        radiance[i] = v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
        stbi_image_free(px);
    }

    test_assert(radiance[0] < 0.95);
    test_assert(radiance[2] > 0.01);
    test_assert(fabs(radiance[1] - 0.15) < 0.015);
    test_assert(fabs(radiance[1] / radiance[0] - 0.25) < 0.03);
    test_assert(fabs(radiance[2] / radiance[1] - 0.25) < 0.03);

    ecs_os_free(path);
    ecs_fini(world);
}

/** Light a surface reflects toward the eye from one light, as brdf() of etc/shaders/mesh.wgsl:
 * Lambert diffuse albedo / pi times 1 - F, GGX specular, times the light color and NdotL.
 *
 * @param albedo Linear base color, dielectric
 * @param roughness Perceptual roughness
 * @param ndl Cosine between the normal and the direction toward the light, the eye along the
 *        normal
 * @param color Light color times intensity
 * @return The linear reflected light
 */
static float light_brdf(
    float albedo,
    float roughness,
    float ndl,
    float color)
{
    float a = roughness * roughness, a2 = a * a;
    float sin_l = sqrtf(1.0f - ndl * ndl);
    float len = sqrtf(sin_l * sin_l + (ndl + 1.0f) * (ndl + 1.0f));
    float ndh = (ndl + 1.0f) / len, ldh = ndh;
    float x = 1.0f - ldh;
    float f = 0.04f + 0.96f * x * x * x * x * x;
    float d = ndh * ndh * (a2 - 1.0f) + 1.0f;
    float dist = a2 / (GLM_PIf * d * d);
    float gv = ndl;
    float gl = sqrtf((ndl - a2 * ndl) * ndl + a2);
    float vis = 0.5f / (gv + gl);
    float diffuse = (1.0f - f) * albedo / GLM_PIf;

    return (diffuse + dist * vis * f) * color * ndl;
}

/* A sun of intensity 2 travels 60 degrees off the normal of a wall of albedo 128 (0.2158
 * linear): the wall reflects about 0.2158 / pi * 2 * 0.5 = 0.069 plus a little GGX specular,
 * where the old radiance of intensity times pi made it 0.22. */
void Light_directional_light_irradiance_unit(void) {
    // lighting/lights
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {128, 128, 128});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});

    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0, 2.0f * (float)GLM_PI / 3.0f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 2});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "unit.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/unit.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(16 * 64 + 32) * 4];

    float albedo = powf((128.0f / 255.0f + 0.055f) / 1.055f, 2.4f);
    float linear = light_brdf(albedo, 1, 0.5f, 2);
    float encoded = 1.055f * powf(linear, 1.0f / 2.4f) - 0.055f;
    int expect = (int)(encoded * 255.0f + 0.5f);
    test_assert(linear > 0.068f && linear < 0.072f);
    test_assert(abs((int)c[0] - expect) <= 2);
    test_assert(abs((int)c[1] - expect) <= 2);
    test_assert(abs((int)c[2] - expect) <= 2);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Light_point_light_fades_smoothly_to_range(void) {
    // lighting_fixes/point_falloff
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {255, 255, 255});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {0, 0, 1});
    ecs_set(world, light, PointLight, {.intensity = 2 * GLM_PIf, .range = 3});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "fade.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/fade.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    int32_t x;
    double steepest = 0, prev = 1;
    for (x = 32; x < 64; x ++) {
        double v = (double)px[(16 * 64 + x) * 4 + 1] / 255.0;
        v = v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
        test_assert(v <= prev + 0.005);
        steepest = prev - v > steepest && x > 32 ? prev - v : steepest;
        prev = v;
    }

    test_assert(px[(16 * 64 + 32) * 4 + 1] > 100);
    test_assert(px[(16 * 64 + 43) * 4 + 1] < 2);
    test_assert(steepest < 0.08);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Light_cluster_list_grows_to_every_light(void) {
    // lighting_fixes/cluster_overflow
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {255, 255, 255});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});
    int32_t i;
    for (i = 0; i < 100; i ++) {
        ecs_entity_t light = ecs_new(world);
        ecs_set(world, light, Position, {-3, 0, 0});
        ecs_set(world, light, PointLight, {.intensity = 0.004f * GLM_PIf, .range = 1000});
    }

    ecs_entity_t single = ecs_new(world);
    ecs_set(world, single, Position, {3, 0, 0});
    ecs_set(world, single, PointLight, {.intensity = 0.4f * GLM_PIf, .range = 1000});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "everywhere.png",
        .resolution_scale = 1});
    for (i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    char *path = flecs_asprintf("%s/everywhere.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *crowd = &px[(16 * 64 + 20) * 4];
    unsigned char *reference = &px[(16 * 64 + 44) * 4];
    test_assert(reference[0] > 40);
    test_assert(reference[0] < 250);
    test_assert(abs(crowd[0] - reference[0]) < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Light_cluster_list_kept_per_target(void) {
    // lighting_fixes/cluster_overflow
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {255, 255, 255});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});
    int32_t i;
    for (i = 0; i < 100; i ++) {
        ecs_entity_t light = ecs_new(world);
        ecs_set(world, light, Position, {-3, 0, 0});
        ecs_set(world, light, PointLight, {.intensity = 0.004f * GLM_PIf, .range = 1000});
    }

    ecs_entity_t single = ecs_new(world);
    ecs_set(world, single, Position, {3, 0, 0});
    ecs_set(world, single, PointLight, {.intensity = 0.4f * GLM_PIf, .range = 1000});

    ecs_entity_t scene = ecs_new(world);
    ecs_set(world, scene, Scene, {0});
    ecs_entity_t ui_camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, ui_camera, Position, {0, 0, 10});
    ecs_set(world, ui_camera, Camera, {
        .ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 100});
    ecs_entity_t root = ecs_new(world);
    ecs_entity_t ui_view = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, ui_view, View, {.camera = ui_camera, .lighting.enabled = true});
    ecs_entity_t texture = ecs_new(world);
    ecs_entity_t ui = ecs_new(world);
    ecs_set(world, ui, Surface, {.width = 16, .height = 16, .texture = texture,
        .view_root = root});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "targets.png",
        .resolution_scale = 1});
    for (i = 0; i < 12; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    char *path = flecs_asprintf("%s/targets.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *crowd = &px[(16 * 64 + 20) * 4];
    unsigned char *reference = &px[(16 * 64 + 44) * 4];
    test_assert(reference[0] > 40);
    test_assert(reference[0] < 250);
    test_assert(abs(crowd[0] - reference[0]) < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}
