/**
 * @file Culling.c
 * @brief Tests for frustum and size culling: draw counts read back per view, and captures.
 *
 * The orthographic cases look down -z from z = 10 with ortho_height 10 into a 64x64 image, so
 * world x and y from -5 to 5 fill the image and a unit is 6.4 pixels. The shapes are unlit, so
 * a white box shows white.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Culling_offscreen_not_drawn(void) {
    // culling/frustum
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    float inside[4][2] = {{-2, -2}, {2, -2}, {-2, 2}, {2, 2}};
    float outside[12][3] = {
        {-20, 0, 0}, {20, 0, 0}, {0, -20, 0}, {0, 20, 0}, {0, 0, 50}, {-9, 9, 0},
        {9, -9, 0}, {-40, -40, 0}, {40, 40, 0}, {6, 0, 0}, {0, -6, 0}, {2, 2, 30}
    };
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {inside[i][0], inside[i][1], 0});
        ecs_set(world, e, Box, {1, 1, 1});
        ecs_set(world, e, Rgb, {255, 255, 255});
    }

    for (i = 0; i < 12; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {outside[i][0], outside[i][1], outside[i][2]});
        ecs_set(world, e, Box, {1, 1, 1});
        ecs_set(world, e, Rgb, {255, 255, 255});
    }

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "offscreen.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 16);
    test_int(stats.visible, 4);

    char *path = flecs_asprintf("%s/offscreen.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(19 * 64 + 19) * 4] > 200);
    test_assert(px[(19 * 64 + 44) * 4] > 200);
    test_assert(px[(44 * 64 + 19) * 4] > 200);
    test_assert(px[(44 * 64 + 44) * 4] > 200);
    test_assert(px[(32 * 64 + 32) * 4] < 20);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Culling_edge_objects_drawn(void) {
    // culling/frustum
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    float edges[4][2] = {{6.5f, 0}, {-6.5f, 0}, {0, 6.5f}, {0, -6.5f}};
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {edges[i][0], edges[i][1], -10});
        ecs_set(world, e, Box, {2, 2, 2});
        ecs_set(world, e, Rgb, {255, 255, 255});
    }

    ecs_entity_t corner = ecs_new(world);
    ecs_set(world, corner, Position, {6.5f, 6.5f, -10});
    ecs_set(world, corner, Box, {2, 2, 2});
    ecs_set(world, corner, Rgb, {255, 255, 255});
    ecs_entity_t beyond = ecs_new(world);
    ecs_set(world, beyond, Position, {9, 0, -10});
    ecs_set(world, beyond, Box, {2, 2, 2});
    ecs_set(world, beyond, Rgb, {255, 255, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "edges.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 6);
    test_int(stats.visible, 5);

    char *path = flecs_asprintf("%s/edges.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 63) * 4] > 200);
    test_assert(px[(32 * 64 + 0) * 4] > 200);
    test_assert(px[(0 * 64 + 32) * 4] > 200);
    test_assert(px[(63 * 64 + 32) * 4] > 200);
    test_assert(px[(0 * 64 + 63) * 4] > 200);
    test_assert(px[(32 * 64 + 32) * 4] < 20);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Culling_bounds_follow_scale_and_rotation(void) {
    // culling/frustum
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 18, 0});
    ecs_set(world, parent, Rotation, {0, 0, (float)GLM_PI_2});
    ecs_entity_t bar = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, bar, Scale, {10, 1, 1});
    ecs_set(world, bar, Box, {3, 0.5f, 0.5f});
    ecs_set(world, bar, Rgb, {255, 255, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "bar.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 1);
    test_int(stats.visible, 1);

    char *path = flecs_asprintf("%s/bar.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(2 * 64 + 32) * 4] > 200);
    test_assert(px[(2 * 64 + 10) * 4] < 20);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Culling_disabled_draws_all(void) {
    // culling/frustum
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)i * 4 - 2, 0, 0});
        ecs_set(world, e, Box, {1, 1, 1});
    }

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 10);
    test_int(stats.visible, 10);

    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 10);
    test_int(stats.visible, 2);

    ecs_fini(world);
}

void Culling_constructed_view_culls(void) {
    // engine_core/zero_values
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    ecs_get_mut(world, view, View)->camera = camera;
    ecs_modified(world, view, View);
    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)i * 4 - 2, 0, 0});
        ecs_set(world, e, Box, {1, 1, 1});
    }

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 10);
    test_int(stats.visible, 2);

    const View *v = ecs_get(world, view, View);
    test_bool(v->culling.enabled, true);
    test_bool(v->culling.occlusion, true);
    test_flt(v->culling.min_dimension_px, 1);

    ecs_fini(world);
}

void Culling_min_dimension(void) {
    // culling/frustum
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1},
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 8},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t large = ecs_new(world);
    ecs_set(world, large, Position, {-1, 0, -5});
    ecs_set(world, large, Box, {1, 1, 1});
    ecs_set(world, large, Rgb, {255, 255, 255});
    ecs_entity_t small = ecs_new(world);
    ecs_set(world, small, Position, {4, 0, -20});
    ecs_set(world, small, Box, {1, 1, 1});
    ecs_set(world, small, Rgb, {255, 255, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "small.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 2);
    test_int(stats.visible, 1);

    char *path = flecs_asprintf("%s/small.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 21) * 4] > 200);
    test_assert(px[(32 * 64 + 43) * 4] < 20);
    stbi_image_free(px);

    ecs_set(world, view, View, {.camera = camera, .culling = {.enabled = true, .occlusion = true},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 2);
    test_int(stats.visible, 2);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 43) * 4] > 200);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Culling_offscreen_caster_shadows_view(void) {
    // culling/frustum
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .shadows = {.enabled = true, .depth_bias = 2, .normal_offset = 1.5f, .fade = 0.2f,
            .cascades = 4, .resolution = 2048, .max_distance = 200, .filter_size = 3}});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {20, 0.2f, 20});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    ecs_entity_t caster = ecs_new(world);
    ecs_set(world, caster, Position, {0, 5, -8});
    ecs_set(world, caster, Box, {1.5f, 0.2f, 1.5f});
    ecs_entity_t far = ecs_new(world);
    ecs_set(world, far, Position, {500, 5, 0});
    ecs_set(world, far, Box, {1.5f, 0.2f, 1.5f});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_4, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "caster.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 3);
    test_int(stats.visible, 1);
    test_assert(stats.casters_visible > 0);
    test_assert(stats.casters_visible < stats.casters_tested);

    char *path = flecs_asprintf("%s/caster.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *under = &px[(15 * 64 + 32) * 4];
    unsigned char *open = &px[(48 * 64 + 32) * 4];
    test_assert(open[0] > 100);
    test_assert(under[0] < 30);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Culling_blended_order_kept(void) {
    // culling/frustum
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
        .punctual_shadows = true, .cookies = true, .environment = true,
        .aerial_perspective = true, .alpha_cutout = true, .blend_modes = true,
        .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
        .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t back = ecs_new(world);
    ecs_set(world, back, Position, {0, 0, -1});
    ecs_set(world, back, Box, {4, 4, 0.1f});
    ecs_set(world, back, Rgb, {255, 0, 0});
    ecs_set(world, back, Opacity, {0.5f});
    ecs_entity_t away = ecs_new(world);
    ecs_set(world, away, Position, {50, 0, 0});
    ecs_set(world, away, Box, {4, 4, 0.1f});
    ecs_set(world, away, Rgb, {0, 255, 0});
    ecs_set(world, away, Opacity, {0.5f});
    ecs_entity_t front = ecs_new(world);
    ecs_set(world, front, Position, {0, 0, 1});
    ecs_set(world, front, Box, {4, 4, 0.1f});
    ecs_set(world, front, Rgb, {0, 0, 255});
    ecs_set(world, front, Opacity, {0.5f});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "blend.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 3);
    test_int(stats.visible, 2);

    char *path = flecs_asprintf("%s/blend.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *mixed = &px[(32 * 64 + 32) * 4];
    test_assert(mixed[0] > 60);
    test_assert(mixed[1] < 20);
    test_assert(mixed[2] > mixed[0] + 20);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Culling_views_cull_separately(void) {
    // culling/frustum
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t left_camera = ecs_new(world);
    ecs_set(world, left_camera, Position, {-20, 0, 10});
    ecs_set(world, left_camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t right_camera = ecs_new(world);
    ecs_set(world, right_camera, Position, {20, 0, 10});
    ecs_set(world, right_camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t left = ecs_new(world);
    ecs_set(world, left, View, {.camera = left_camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});
    ecs_entity_t right = ecs_new(world);
    ecs_set(world, right, View, {.camera = right_camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1}});
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {-20 + (float)i * 2 - 2, 0, 0});
        ecs_set(world, e, Box, {1, 1, 1});
    }

    for (i = 0; i < 5; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {20 + (float)i * 2 - 4, 0, 0});
        ecs_set(world, e, Box, {1, 1, 1});
    }

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, left, &stats), true);
    test_int(stats.tested, 8);
    test_int(stats.visible, 3);
    test_bool(box_render_gpu_cull_stats(world, right, &stats), true);
    test_int(stats.tested, 8);
    test_int(stats.visible, 5);

    ecs_fini(world);
}

void Culling_dynamic_moves_out(void) {
    // culling/frustum
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t mover = ecs_new(world);
    ecs_add(world, mover, DynamicTransform);
    ecs_set(world, mover, Position, {0, 0, 0});
    ecs_set(world, mover, Box, {2, 2, 2});
    ecs_set(world, mover, Rgb, {255, 255, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "mover.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 1);
    test_int(stats.visible, 1);

    ecs_set(world, mover, Position, {30, 0, 0});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 1);
    test_int(stats.visible, 0);

    char *path = flecs_asprintf("%s/mover.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 32) * 4] < 20);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}
