/**
 * @file Occlusion.c
 * @brief Tests for Hi-Z occlusion culling: CullStatus counts per view and captured pixels.
 *
 * Views enable culling with occlusion. The camera looks down -z from
 * the origin unless a test moves it; shapes are unlit, so a red box shows red. A change shows
 * in the capture of the frame it was made in, which is what keeps fast cameras free of holes.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Occlusion_wall_hides_object(void) {
    // culling/hiz
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
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -10});
    ecs_set(world, wall, Box, {30, 30, 1});
    ecs_set(world, wall, Rgb, {0, 255, 0});
    ecs_entity_t hidden = ecs_new(world);
    ecs_set(world, hidden, Position, {0, 0, -20});
    ecs_set(world, hidden, Box, {2, 2, 2});
    ecs_set(world, hidden, Rgb, {255, 0, 0});
    ecs_entity_t away = ecs_new(world);
    ecs_set(world, away, Position, {100, 0, -10});
    ecs_set(world, away, Box, {1, 1, 1});
    ecs_entity_t tiny = ecs_new(world);
    ecs_set(world, tiny, Position, {1, 0, -5});
    ecs_set(world, tiny, Box, {0.01f, 0.01f, 0.01f});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .resolution_scale = 1});

    int64_t start = ecs_get_world_info(world)->frame_count_total + 3;
    const CullStatus *status = NULL;
    while (!status || status->frame < start) {
        ecs_progress(world, 0);
        box_render_sync(world);
        status = ecs_get(world, view, CullStatus);
    }

    test_assert(status != NULL);
    test_assert(status->frame >= start);
    test_int(status->instances, 4);
    test_int(status->visible, 1);
    test_int(status->frustum_culled, 2);
    test_int(status->occlusion_culled, 1);

    ecs_fini(world);
}

void Occlusion_wall_moved_shows_object_same_frame(void) {
    // culling/hiz
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
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -10});
    ecs_set(world, wall, Box, {30, 30, 1});
    ecs_set(world, wall, Rgb, {0, 255, 0});
    ecs_entity_t hidden = ecs_new(world);
    ecs_set(world, hidden, Position, {0, 0, -20});
    ecs_set(world, hidden, Box, {2, 2, 2});
    ecs_set(world, hidden, Rgb, {255, 0, 0});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "moved.png",
        .resolution_scale = 1});

    int64_t start = ecs_get_world_info(world)->frame_count_total + 3;
    const CullStatus *status = NULL;
    while (!status || status->frame < start) {
        ecs_progress(world, 0);
        box_render_sync(world);
        status = ecs_get(world, view, CullStatus);
    }

    test_assert(status != NULL);
    test_int(status->occlusion_culled, 1);
    char *path = flecs_asprintf("%s/moved.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 32) * 4 + 1] > 200);
    stbi_image_free(px);

    ecs_set(world, wall, Position, {100, 0, -10});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 32) * 4] > 200);
    test_assert(px[(32 * 64 + 32) * 4 + 1] < 50);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Occlusion_camera_cut_shows_object_same_frame(void) {
    // culling/hiz
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Rotation, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -10});
    ecs_set(world, wall, Box, {30, 30, 1});
    ecs_set(world, wall, Rgb, {0, 255, 0});
    int32_t i;
    for (i = 0; i < 9; i ++) {
        ecs_entity_t hidden = ecs_new(world);
        ecs_set(world, hidden, Position, {(float)(i % 3) * 4 - 4, (float)(i / 3) * 4 - 4, -20});
        ecs_set(world, hidden, Box, {2, 2, 2});
        ecs_set(world, hidden, Rgb, {255, 0, 0});
    }

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "cut.png",
        .resolution_scale = 1});

    int64_t start = ecs_get_world_info(world)->frame_count_total + 3;
    const CullStatus *status = NULL;
    while (!status || status->frame < start) {
        ecs_progress(world, 0);
        box_render_sync(world);
        status = ecs_get(world, view, CullStatus);
    }

    test_assert(status != NULL);
    test_int(status->occlusion_culled, 9);

    ecs_set(world, camera, Position, {0, 0, -40});
    ecs_set(world, camera, Rotation, {0, (float)GLM_PI, 0});
    ecs_progress(world, 0);
    box_render_sync(world);
    char *path = flecs_asprintf("%s/cut.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    int32_t centers[3] = {20, 32, 44};
    for (i = 0; i < 9; i ++) {
        unsigned char *p = &px[(centers[i / 3] * 64 + centers[i % 3]) * 4];
        test_assert(p[0] > 200);
        test_assert(p[1] < 50);
    }

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Occlusion_slot_moves_keep_instances_drawn(void) {
    // culling/hiz
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
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 2.5f, 0});
    ecs_set(world, wall, Box, {10, 4, 0.5f});
    ecs_set(world, wall, Rgb, {0, 255, 0});
    ecs_entity_t hidden[4], shown[4];
    int32_t i;
    for (i = 0; i < 4; i ++) {
        hidden[i] = ecs_new(world);
        ecs_set(world, hidden[i], Position, {(float)i * 2 - 3, 2.5f, -5});
        ecs_set(world, hidden[i], Box, {1, 1, 1});
        ecs_set(world, hidden[i], Rgb, {255, 0, 0});
    }

    for (i = 0; i < 4; i ++) {
        shown[i] = ecs_new(world);
        ecs_set(world, shown[i], Position, {(float)i * 2 - 3, -2.5f, 0});
        ecs_set(world, shown[i], Box, {1, 1, 1});
        ecs_set(world, shown[i], Rgb, {255, 255, 255});
    }

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "slots.png",
        .resolution_scale = 1});

    int64_t start = ecs_get_world_info(world)->frame_count_total + 3;
    const CullStatus *status = NULL;
    while (!status || status->frame < start) {
        ecs_progress(world, 0);
        box_render_sync(world);
        status = ecs_get(world, view, CullStatus);
    }

    test_assert(status != NULL);
    test_int(status->visible, 5);
    test_int(status->occlusion_culled, 4);

    ecs_delete(world, hidden[1]);
    ecs_delete(world, hidden[2]);
    ecs_set(world, wall, Position, {100, 2.5f, 0});
    ecs_progress(world, 0);
    box_render_sync(world);
    char *path = flecs_asprintf("%s/slots.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    int32_t columns[4] = {13, 26, 38, 51};
    for (i = 0; i < 4; i ++) {
        unsigned char *bottom = &px[(48 * 64 + columns[i]) * 4];
        unsigned char *top = &px[(16 * 64 + columns[i]) * 4];
        test_assert(bottom[0] > 200 && bottom[1] > 200 && bottom[2] > 200);
        if (i == 0 || i == 3) {
            test_assert(top[0] > 200 && top[1] < 50);
        } else {
            test_assert(top[0] < 50 && top[1] < 50);
        }
    }

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Occlusion_batch_replaced_draws_at_once(void) {
    // culling/hiz
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
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, 0});
    ecs_set(world, wall, Box, {10, 10, 0.5f});
    ecs_set(world, wall, Rgb, {0, 255, 0});
    ecs_entity_t spheres[4];
    int32_t i;
    for (i = 0; i < 4; i ++) {
        spheres[i] = ecs_new(world);
        ecs_set(world, spheres[i], Position, {(float)i * 2 - 3, 0, -5});
        ecs_set(world, spheres[i], Sphere, {.segments = 24, .radius = 0.6f});
        ecs_set(world, spheres[i], Rgb, {255, 0, 0});
    }

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "batch.png",
        .resolution_scale = 1});

    int64_t start = ecs_get_world_info(world)->frame_count_total + 3;
    const CullStatus *status = NULL;
    while (!status || status->frame < start) {
        ecs_progress(world, 0);
        box_render_sync(world);
        status = ecs_get(world, view, CullStatus);
    }

    test_assert(status != NULL);
    test_int(status->occlusion_culled, 4);

    for (i = 0; i < 4; i ++) {
        ecs_delete(world, spheres[i]);
        ecs_entity_t c = ecs_new(world);
        ecs_set(world, c, Position, {(float)i * 2 - 3, 0, -5});
        ecs_set(world, c, Cylinder, {.segments = 24, .length = 1.2f, .radius = 0.6f});
        ecs_set(world, c, Rgb, {0, 0, 255});
    }

    ecs_set(world, wall, Position, {100, 0, 0});
    ecs_progress(world, 0);
    box_render_sync(world);
    char *path = flecs_asprintf("%s/batch.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    int32_t columns[4] = {13, 26, 38, 51};
    for (i = 0; i < 4; i ++) {
        unsigned char *p = &px[(32 * 64 + columns[i]) * 4];
        test_assert(p[2] > 200);
        test_assert(p[0] < 50 && p[1] < 50);
    }

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Occlusion_frustum_only_and_disabled(void) {
    // culling/hiz
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
        .culling = {.enabled = true, .min_dimension_px = 1}});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -10});
    ecs_set(world, wall, Box, {30, 30, 1});
    ecs_entity_t hidden = ecs_new(world);
    ecs_set(world, hidden, Position, {0, 0, -20});
    ecs_set(world, hidden, Box, {2, 2, 2});
    ecs_entity_t away = ecs_new(world);
    ecs_set(world, away, Position, {100, 0, -10});
    ecs_set(world, away, Box, {1, 1, 1});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .resolution_scale = 1});

    int64_t start = ecs_get_world_info(world)->frame_count_total + 3;
    const CullStatus *status = NULL;
    while (!status || status->frame < start) {
        ecs_progress(world, 0);
        box_render_sync(world);
        status = ecs_get(world, view, CullStatus);
    }

    test_assert(status != NULL);
    test_int(status->instances, 3);
    test_int(status->visible, 2);
    test_int(status->frustum_culled, 1);
    test_int(status->occlusion_culled, 0);

    ecs_set(world, view, View, {.camera = camera});
    start = ecs_get_world_info(world)->frame_count_total + 1;
    while (!status || status->frame < start) {
        ecs_progress(world, 0);
        box_render_sync(world);
        status = ecs_get(world, view, CullStatus);
    }

    test_assert(status != NULL);
    test_int(status->instances, 3);
    test_int(status->visible, 3);
    test_int(status->frustum_culled, 0);
    test_int(status->occlusion_culled, 0);

    ecs_fini(world);
}

void Occlusion_hidden_caster_casts_shadow(void) {
    // culling/hiz
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 5, 4});
    ecs_set(world, camera, Rotation, {-0.6f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .shadows = {.enabled = true, .cascades = 4, .resolution = 2048, .max_distance = 200,
            .split_lambda = 0.7f, .depth_bias = 2, .normal_offset = 1.5f, .filter_size = 3,
            .fade = 0.2f}});
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {40, 0.2f, 40});
    ecs_set(world, floor, Rgb, {255, 255, 255});
    ecs_set(world, floor, PbrMaterial, {.roughness = 1});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 4, -2});
    ecs_set(world, wall, Box, {8, 8, 0.2f});
    ecs_set(world, wall, Rgb, {255, 255, 255});
    ecs_add(world, wall, NoShadow);
    ecs_entity_t caster = ecs_new(world);
    ecs_set(world, caster, Position, {0, 3, -4});
    ecs_set(world, caster, Box, {3, 0.2f, 3});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_4, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "caster.png",
        .resolution_scale = 1});

    int64_t start = ecs_get_world_info(world)->frame_count_total + 3;
    const CullStatus *status = NULL;
    while (!status || status->frame < start) {
        ecs_progress(world, 0);
        box_render_sync(world);
        status = ecs_get(world, view, CullStatus);
    }

    test_assert(status != NULL);
    test_int(status->instances, 3);
    test_int(status->occlusion_culled, 1);

    char *path = flecs_asprintf("%s/caster.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *shadow = &px[(43 * 64 + 32) * 4];
    unsigned char *lit = &px[(43 * 64 + 4) * 4];
    test_assert(lit[0] > 100);
    test_assert(shadow[0] < lit[0] / 2);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Occlusion_camera_cuts_match_frustum_only(void) {
    // culling/hiz
    float poses[6][4] = {
        {0, 3, 30, 0}, {25, 3, 0, 1.57f}, {0, 40, 0, 0}, {-25, 3, 0, -1.57f},
        {0, 3, -30, 3.14f}, {3, 1.5f, 3, 0.8f}
    };
    unsigned char *frames[2][12] = {{0}};
    int32_t mode, f, i, x, z;
    for (mode = 0; mode < 2; mode ++) {
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
            .culling = {.enabled = true, .occlusion = mode != 1, .min_dimension_px = 1},
            .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
        ecs_entity_t floor = ecs_new(world);
        ecs_set(world, floor, Position, {0, -0.1f, 0});
        ecs_set(world, floor, Box, {60, 0.2f, 60});
        ecs_set(world, floor, Rgb, {90, 90, 90});
        for (x = 0; x < 10; x ++) {
            for (z = 0; z < 10; z ++) {
                ecs_entity_t b = ecs_new(world);
                ecs_set(world, b, Position, {(float)x * 4 - 18, 2, (float)z * 4 - 18});
                ecs_set(world, b, Box, {3, 4 + (float)((x * 7 + z * 3) % 5), 3});
                ecs_set(world, b, Rgb, {(uint8_t)(40 + x * 20), (uint8_t)(40 + z * 20), 160});
                ecs_entity_t s = ecs_new(world);
                ecs_set(world, s, Position, {(float)x * 4 - 16, 0.5f, (float)z * 4 - 16});
                ecs_set(world, s, Sphere, {.segments = 24, .radius = 0.5f});
                ecs_set(world, s, Rgb, {250, (uint8_t)(x * 25), (uint8_t)(z * 25)});
            }
        }

        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Surface, {.width = 128, .height = 72, .write_to_file = "cuts.png",
            .resolution_scale = 1});
        char *path = flecs_asprintf("%s/cuts.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
        for (f = 0; f < 12; f ++) {
            float *p = poses[f % 6];
            ecs_set(world, camera, Position, {p[0], p[1], p[2]});
            ecs_set(world, camera, Rotation, {p[1] > 20 ? -1.5f : -0.1f, p[3], 0});
            ecs_progress(world, 0);
            box_render_sync(world);
            int w = 0, h = 0, n = 0;
            frames[mode][f] = stbi_load(path, &w, &h, &n, 4);
            test_assert(frames[mode][f] != NULL);
            test_int(w * h, 128 * 72);
        }

        const CullStatus *status = ecs_get(world, view, CullStatus);
        test_assert(status != NULL);
        test_int(status->instances, 201);
        if (mode == 0) {
            test_assert(status->occlusion_culled > 0);
        } else {
            test_int(status->occlusion_culled, 0);
        }

        ecs_os_free(path);
        ecs_fini(world);
    }

    for (f = 0; f < 12; f ++) {
        int32_t off = 0;
        for (i = 0; i < 128 * 72 * 4; i ++) {
            if (abs((int)frames[0][f][i] - (int)frames[1][f][i]) > 16) {
                off ++;
            }
        }

        test_int(off, 0);
        stbi_image_free(frames[0][f]);
        stbi_image_free(frames[1][f]);
    }
}
