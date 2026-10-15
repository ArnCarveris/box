/**
 * @file Invalidation.c
 * @brief Tests for the local rebake of the probes a change of a baked scene can affect.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Invalidation_static_move_rebakes_nearby_probes(void) {
    // baked_lighting/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 2, .bounces = 0});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 4, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {48, 0.2f, 4});
    ecs_entity_t box = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, box, Position, {-6, 0.5f, 0});
    ecs_set(world, box, Box, {1, 1, 1});
    ecs_set(world, box, Emissive, {.strength = 2, .color = {255, 200, 100}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 48, .write_to_file = "static.png",
        .resolution_scale = 1});
    char *path = flecs_asprintf("%s/static.png", ecs_singleton_get(world, AppStatus)->tmp_dir);

    int32_t frames = 0;
    while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    const LightBakeStatus *status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, true);
    int32_t probes = status->probes, baked = status->baked;
    test_assert(probes > 200);
    ecs_progress(world, 0);
    box_render_sync(world);
    int w[2] = {0, 0}, h[2] = {0, 0}, n = 0, x, y, largest = 0;
    unsigned char *images[2] = {NULL, NULL};
    images[0] = stbi_load(path, &w[0], &h[0], &n, 4);
    test_assert(images[0] != NULL);

    ecs_set(world, box, Position, {-3, 0.5f, 0});
    ecs_progress(world, 0);
    box_render_sync(world);
    status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, false);
    test_int(status->probes, probes);
    test_assert(status->baked < baked);
    test_assert((baked - status->baked) * 4 < probes);

    frames = 0;
    while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, true);
    test_int(status->baked, baked);
    ecs_progress(world, 0);
    box_render_sync(world);
    images[1] = stbi_load(path, &w[1], &h[1], &n, 4);
    test_assert(images[1] != NULL);
    test_int(w[1], w[0]);
    test_int(h[1], h[0]);
    for (y = 0; y < h[0]; y ++) {
        for (x = w[0] * 3 / 4; x < w[0]; x ++) {
            for (n = 0; n < 3; n ++) {
                int32_t o = (y * w[0] + x) * 4 + n;
                int d = abs((int)images[0][o] - (int)images[1][o]);
                largest = d > largest ? d : largest;
            }
        }
    }

    test_assert(largest <= 1);
    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
    ecs_os_free(path);
    ecs_fini(world);
}

void Invalidation_dynamic_move_rebakes_nothing(void) {
    // baked_lighting/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 2, .bounces = 0});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 4, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {48, 0.2f, 4});
    ecs_entity_t mover = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_add(world, mover, DynamicTransform);
    ecs_set(world, mover, Position, {-6, 0.5f, 0});
    ecs_set(world, mover, Box, {1, 1, 1});
    ecs_set(world, mover, Emissive, {.strength = 2, .color = {255, 200, 100}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 32, .height = 24, .resolution_scale = 1});

    int32_t frames = 0;
    while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    const LightBakeStatus *status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, true);
    int32_t baked = status->baked;

    for (frames = 0; frames < 10; frames ++) {
        ecs_set(world, mover, Position, {-6 + (float)frames, 0.5f, 0});
        ecs_set(world, mover, Emissive, {.strength = 2 + (float)frames, .color = {255, 200, 100}});
        ecs_progress(world, 0);
        box_render_sync(world);
        status = ecs_get(world, scene, LightBakeStatus);
        test_bool(status->converged, true);
        test_int(status->baked, baked);
    }

    ecs_fini(world);
}

void Invalidation_point_light_change_limited_to_range(void) {
    // baked_lighting/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 2, .bounces = 0});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 4, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {48, 0.2f, 4});
    ecs_entity_t lamp = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, lamp, Position, {16, 1, 0});
    ecs_set(world, lamp, PointLight, {.intensity = 2, .range = 3});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 32, .height = 24, .resolution_scale = 1});

    int32_t frames = 0;
    while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    const LightBakeStatus *status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, true);
    int32_t probes = status->probes, baked = status->baked;

    ecs_set(world, lamp, PointLight, {.intensity = 4, .range = 3});
    ecs_progress(world, 0);
    box_render_sync(world);
    status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, false);
    test_assert(status->baked < baked);
    test_assert((baked - status->baked) * 8 < probes);

    frames = 0;
    while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);

    ecs_set(world, lamp, Position, {-16, 1, 0});
    ecs_progress(world, 0);
    box_render_sync(world);
    status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, false);
    test_assert(status->baked < baked);
    test_assert((baked - status->baked) * 4 < probes);

    ecs_fini(world);
}

void Invalidation_directional_light_change_rebakes_scene(void) {
    // baked_lighting/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 2, .bounces = 0});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 4, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {16, 0.2f, 4});
    ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, sun, Rotation, {1, 0.5f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 32, .height = 24, .resolution_scale = 1});

    int32_t frames = 0;
    while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    const LightBakeStatus *status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, true);
    int32_t baked = status->baked;
    test_assert(baked > 0);

    ecs_set(world, sun, DirectionalLight, {.intensity = 2});
    ecs_progress(world, 0);
    box_render_sync(world);
    status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, false);
    test_int(status->baked, 0);

    frames = 0;
    while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, true);
    test_int(status->baked, baked);

    ecs_fini(world);
}

void Invalidation_removed_and_recolored_shapes_rebake(void) {
    // baked_lighting/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 2, .bounces = 0});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 4, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {48, 0.2f, 4});
    ecs_entity_t glow = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, glow, Position, {-12, 0.5f, 0});
    ecs_set(world, glow, Box, {1, 1, 1});
    ecs_set(world, glow, Emissive, {.strength = 2, .color = {255, 200, 100}});
    ecs_entity_t block = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, block, Position, {12, 0.5f, 0});
    ecs_set(world, block, Box, {1, 1, 1});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 32, .height = 24, .resolution_scale = 1});

    int32_t frames = 0;
    while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    const LightBakeStatus *status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, true);
    int32_t probes = status->probes, baked = status->baked;

    ecs_set(world, glow, Emissive, {.strength = 4, .color = {100, 200, 255}});
    ecs_progress(world, 0);
    box_render_sync(world);
    status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, false);
    test_assert(status->baked < baked);
    test_assert((baked - status->baked) * 4 < probes);

    frames = 0;
    while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);

    ecs_delete(world, block);
    ecs_progress(world, 0);
    box_render_sync(world);
    status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, false);
    test_assert((baked - status->baked) * 4 < probes);

    frames = 0;
    while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, true);
    test_assert(status->baked >= baked);

    ecs_fini(world);
}

void Invalidation_moved_emissive_leaves_no_light_behind(void) {
    // baked_lighting/invalidation
    unsigned char *images[3] = {NULL, NULL, NULL};
    int w[3] = {0, 0, 0}, h[3] = {0, 0, 0};
    int32_t i, j;

    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);

        ecs_entity_t scene = ecs_new(world);
        ecs_add(world, scene, Scene);
        ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
            .capture_size = 8, .budget_ms = 2, .bounces = 1});
        ecs_set(world, scene, AmbientLight, {.intensity = 0});
        ecs_entity_t dark = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, dark, DirectionalLight, {.intensity = 0});
        ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, camera, Position, {0, 9, 0});
        ecs_set(world, camera, Rotation, {-1.5708f, 0, 0});
        ecs_set(world, camera, Camera, {.fov = 50, .near_ = 0.1f});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
            .color = {0, 0, 0, 255}}, .lighting.enabled = true, .lighting.baked = true});
        ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, floor, Position, {0, -0.1f, 0});
        ecs_set(world, floor, Box, {10, 0.2f, 6});
        ecs_set(world, floor, Rgb, {230, 230, 230});
        ecs_entity_t glow = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, glow, Position, {i == 0 ? -3.0f : 3.0f, 0.4f, 0});
        ecs_set(world, glow, Box, {0.8f, 0.8f, 0.8f});
        ecs_set(world, glow, Rgb, {0, 0, 0});
        ecs_set(world, glow, Emissive, {.strength = 4, .color = {255, 255, 255}});
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Surface, {.width = 160, .height = 120, .write_to_file = "moved.png",
            .resolution_scale = 1});
        char *path = flecs_asprintf("%s/moved.png", ecs_singleton_get(world, AppStatus)->tmp_dir);

        int32_t frames = 0;
        while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
            ecs_progress(world, 0);
            box_render_sync(world);
            frames ++;
        }

        test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
        ecs_progress(world, 0);
        box_render_sync(world);
        int n = 0;
        if (i == 0) {
            images[2] = stbi_load(path, &w[2], &h[2], &n, 4);
            test_assert(images[2] != NULL);
            ecs_set(world, glow, Position, {3, 0.4f, 0});
            ecs_progress(world, 0);
            box_render_sync(world);
            test_bool(ecs_get(world, scene, LightBakeStatus)->converged, false);
            frames = 0;
            while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
                ecs_progress(world, 0);
                box_render_sync(world);
                frames ++;
            }

            test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
            ecs_progress(world, 0);
            box_render_sync(world);
        }

        images[i] = stbi_load(path, &w[i], &h[i], &n, 4);
        test_assert(images[i] != NULL);
        ecs_os_free(path);
        ecs_fini(world);
    }

    for (i = 1; i < 3; i ++) {
        test_int(w[i], w[0]);
        test_int(h[i], h[0]);
    }

    int32_t before = 0, moved = 0, fresh = 0, moved_mid = 0, fresh_mid = 0;
    for (j = 0; j < 8; j ++) {
        int32_t o = (54 * w[0] + 52 + j) * 4, m = (54 * w[0] + 76 + j) * 4;
        before += images[2][o] + images[2][o + 1] + images[2][o + 2];
        moved += images[0][o] + images[0][o + 1] + images[0][o + 2];
        fresh += images[1][o] + images[1][o + 1] + images[1][o + 2];
        moved_mid += images[0][m] + images[0][m + 1] + images[0][m + 2];
        fresh_mid += images[1][m] + images[1][m + 1] + images[1][m + 2];
    }

    test_assert(before > moved + 8 * 3 * 20);
    test_assert(abs(moved - fresh) <= 8 * 3 * 2);
    test_assert(fresh_mid > 8 * 3 * 20);
    test_assert(abs(moved_mid - fresh_mid) <= 8 * 3 * 4);

    for (i = 0; i < 3; i ++) {
        stbi_image_free(images[i]);
    }
}

void Invalidation_requeued_probes_keep_values(void) {
    // baked_lighting/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    char *path = flecs_asprintf("%s/keep.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    app = ecs_singleton_get_mut(world, App);
    app->headless = false;
    ecs_singleton_modified(world, App);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 1000000, .bounces = 1});
    ecs_set(world, scene, AmbientLight, {.intensity = 0});
    ecs_entity_t dark = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, dark, DirectionalLight, {.intensity = 0});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 7, 0});
    ecs_set(world, camera, Rotation, {-1.5708f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 50, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true, .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {6, 0.2f, 4});
    ecs_set(world, floor, Rgb, {230, 230, 230});
    ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, wall, Position, {0, 1, -2.1f});
    ecs_set(world, wall, Box, {6, 2, 0.2f});
    ecs_set(world, wall, Rgb, {230, 230, 230});
    ecs_entity_t glow = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, glow, Position, {-2, 0.3f, 0});
    ecs_set(world, glow, Box, {0.6f, 0.6f, 0.6f});
    ecs_set(world, glow, Rgb, {0, 0, 0});
    ecs_set(world, glow, Emissive, {.strength = 4, .color = {255, 255, 255}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 48, .write_to_file = path,
        .resolution_scale = 1});

    int32_t frames = 0;
    while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
    ecs_progress(world, 0);
    box_render_sync(world);

    int n = 0, iw = 0, ih = 0, x, y;
    unsigned char *image = stbi_load(path, &iw, &ih, &n, 4);
    test_assert(image != NULL);
    int32_t before = 0;
    for (y = 20; y < 28; y ++) {
        for (x = 36; x < 44; x ++) {
            before += image[(y * iw + x) * 4 + 1];
        }
    }

    stbi_image_free(image);
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 0.001f, .bounces = 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
    ecs_set(world, glow, Emissive, {.strength = 4, .color = {255, 255, 255}});
    ecs_set(world, glow, Rgb, {10, 10, 10});

    int32_t lowest = before, partial = 0;
    frames = 0;
    do {
        ecs_progress(world, 0);
        box_render_sync(world);
        image = stbi_load(path, &iw, &ih, &n, 4);
        test_assert(image != NULL);
        int32_t sum = 0;
        for (y = 20; y < 28; y ++) {
            for (x = 36; x < 44; x ++) {
                sum += image[(y * iw + x) * 4 + 1];
            }
        }

        stbi_image_free(image);
        lowest = sum < lowest ? sum : lowest;
        partial += !ecs_get(world, scene, LightBakeStatus)->converged;
        frames ++;
    } while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000);

    test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
    test_assert(partial > 10);
    test_assert(lowest >= before - 64 * 2);

    ecs_os_free(path);
    ecs_fini(world);
}
