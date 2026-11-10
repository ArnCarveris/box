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
    ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
        ? ecs_singleton_get(world, Scene) : &(Scene){0});
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 1,
        .capture_size = 8, .budget_ms = 2, .bounces = 0, .compute = true});
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
    ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
        ? ecs_singleton_get(world, Scene) : &(Scene){0});
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 1,
        .capture_size = 8, .budget_ms = 2, .bounces = 0, .compute = true});
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
    ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
        ? ecs_singleton_get(world, Scene) : &(Scene){0});
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 1,
        .capture_size = 8, .budget_ms = 2, .bounces = 0, .compute = true});
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
    ecs_set(world, lamp, PointLight, {.intensity = 2 * GLM_PIf, .range = 3});
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

    ecs_set(world, lamp, PointLight, {.intensity = 4 * GLM_PIf, .range = 3});
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
    ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
        ? ecs_singleton_get(world, Scene) : &(Scene){0});
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 1,
        .capture_size = 8, .budget_ms = 2, .bounces = 0, .compute = true});
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
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
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

    ecs_set(world, sun, DirectionalLight, {.intensity = 2 * GLM_PIf});
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
    ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
        ? ecs_singleton_get(world, Scene) : &(Scene){0});
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 1,
        .capture_size = 8, .budget_ms = 2, .bounces = 0, .compute = true});
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
        ecs_singleton_set(world, Scene, {.baked = true, .reflections = true,
            .punctual_shadows = true, .cookies = true, .environment = true,
            .aerial_perspective = true, .alpha_cutout = true, .blend_modes = true,
            .transmission = true, .texture_blend = true, .triplanar = true, .sheen = true,
            .clear_coat = true, .anisotropy = true, .diffuse_transmission = true});
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);

        ecs_entity_t scene = ecs_new(world);
        ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
            ? ecs_singleton_get(world, Scene) : &(Scene){0});
        ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
            .capture_size = 8, .budget_ms = 2, .bounces = 1, .compute = true});
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
    ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
        ? ecs_singleton_get(world, Scene) : &(Scene){0});
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 1,
        .capture_size = 8, .budget_ms = 1000000, .bounces = 1, .compute = true});
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
    ecs_set(world, e, Surface, {.texture = e, .width = 64, .height = 48, .write_to_file = path,
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
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 1,
        .capture_size = 8, .budget_ms = 0.001f, .bounces = 1, .compute = true});
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

void Invalidation_edge_changes_keep_the_volume(void) {
    // baked_lighting/invalidation
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
    char *path = flecs_asprintf("%s/edge.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    app = ecs_singleton_get_mut(world, App);
    app->headless = false;
    ecs_singleton_modified(world, App);

    ecs_entity_t scene = ecs_new(world);
    ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
        ? ecs_singleton_get(world, Scene) : &(Scene){0});
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 1,
        .capture_size = 8, .budget_ms = 1000000, .bounces = 0, .compute = true});
    ecs_set(world, scene, AmbientLight, {.intensity = 0});
    ecs_entity_t dark = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, dark, DirectionalLight, {.intensity = 0});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-1.5708f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true, .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {12, 0.2f, 4});
    ecs_set(world, floor, Rgb, {230, 230, 230});
    ecs_entity_t glow = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, glow, Position, {-3, 0.3f, 0});
    ecs_set(world, glow, Box, {0.6f, 0.6f, 0.6f});
    ecs_set(world, glow, Rgb, {0, 0, 0});
    ecs_set(world, glow, Emissive, {.strength = 4, .color = {255, 255, 255}});
    ecs_entity_t panel = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, panel, Position, {0, 2.5f, 0});
    ecs_set(world, panel, Rotation, {1.5708f, 0, 0});
    ecs_set(world, panel, Quad, {10, 3});
    ecs_set(world, panel, Rgb, {0, 0, 0});
    ecs_set(world, panel, Emissive, {.strength = 1, .color = {255, 255, 255}});
    ecs_entity_t tall = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, tall, Position, {3, 2.5f, 0});
    ecs_set(world, tall, Box, {0.6f, 5, 0.6f});
    ecs_set(world, tall, Rgb, {0, 0, 0});
    ecs_entity_t edge = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, edge, Position, {5.5f, 0.5f, 0});
    ecs_set(world, edge, Box, {1, 1, 1});
    ecs_set(world, edge, Rgb, {0, 0, 0});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.texture = e, .width = 64, .height = 48, .write_to_file = path,
        .resolution_scale = 1});

    int32_t frames = 0, change, n = 0, iw = 0, ih = 0, x, y;
    while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 1,
        .capture_size = 8, .budget_ms = 0.001f, .bounces = 0, .compute = true});
    ecs_progress(world, 0);
    box_render_sync(world);

    for (change = 0; change < 3; change ++) {
        const LightBakeStatus *status = ecs_get(world, scene, LightBakeStatus);
        test_bool(status->converged, true);
        int32_t probes = status->probes, baked = status->baked;
        unsigned char *image = stbi_load(path, &iw, &ih, &n, 4);
        test_assert(image != NULL);
        int32_t before = 0;
        for (y = 20; y < 28; y ++) {
            for (x = 24; x < 36; x ++) {
                before += image[(y * iw + x) * 4 + 1];
            }
        }

        stbi_image_free(image);
        test_assert(before > 8 * 12 * 40);
        if (change == 0) {
            ecs_set(world, edge, Position, {7.5f, 0.5f, 0});
        } else if (change == 1) {
            ecs_set(world, floor, Position, {0, -0.12f, 0});
        } else {
            ecs_delete(world, tall);
        }

        int32_t lowest = before, partial = 0;
        frames = 0;
        do {
            ecs_progress(world, 0);
            box_render_sync(world);
            status = ecs_get(world, scene, LightBakeStatus);
            if (!frames && change != 1) {
                test_assert((baked - status->baked) * 2 < probes);
            }

            image = stbi_load(path, &iw, &ih, &n, 4);
            test_assert(image != NULL);
            int32_t sum = 0;
            for (y = 20; y < 28; y ++) {
                for (x = 24; x < 36; x ++) {
                    sum += image[(y * iw + x) * 4 + 1];
                }
            }

            stbi_image_free(image);
            lowest = sum < lowest ? sum : lowest;
            partial += !status->converged;
            frames ++;
        } while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000);

        test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
        test_assert(partial > 3);
        test_assert(lowest >= before - 8 * 12 * 3);
    }

    ecs_os_free(path);
    ecs_fini(world);
}

void Invalidation_deleting_uncaptured_shapes_rebakes_nothing(void) {
    // baked_lighting/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t scene = ecs_new(world);
    ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
        ? ecs_singleton_get(world, Scene) : &(Scene){0});
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 1,
        .capture_size = 8, .budget_ms = 2, .bounces = 0, .compute = true});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 4, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {16, 0.2f, 4});
    ecs_entity_t mover = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_add(world, mover, DynamicTransform);
    ecs_set(world, mover, Position, {-4, 0.5f, 0});
    ecs_set(world, mover, Box, {1, 1, 1});
    ecs_entity_t hidden = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_add(world, hidden, NoBake);
    ecs_set(world, hidden, Position, {0, 0.5f, 0});
    ecs_set(world, hidden, Box, {1, 1, 1});
    ecs_entity_t manual = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_add(world, manual, ManualTransform);
    ecs_set(world, manual, Box, {1, 1, 1});
    WorldTransform placed;
    glm_translate_make(placed.m, (vec3){4, 0.5f, 0});
    ecs_set_ptr(world, manual, WorldTransform, &placed);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 32, .height = 24, .resolution_scale = 1});

    int32_t frames = 0, i;
    while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    const LightBakeStatus *status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, true);
    int32_t baked = status->baked;
    ecs_entity_t gone[3] = {mover, hidden, manual};
    for (i = 0; i < 3; i ++) {
        ecs_delete(world, gone[i]);
        ecs_progress(world, 0);
        box_render_sync(world);
        status = ecs_get(world, scene, LightBakeStatus);
        test_bool(status->converged, true);
        test_int(status->baked, baked);
    }

    ecs_fini(world);
}

void Invalidation_parent_opacity_and_prefab_textures_rebake(void) {
    // baked_lighting/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t scene = ecs_new(world);
    ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
        ? ecs_singleton_get(world, Scene) : &(Scene){0});
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 1,
        .capture_size = 8, .budget_ms = 2, .bounces = 0, .compute = true});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 4, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {48, 0.2f, 4});
    ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, group, Position, {-12, 0, 0});
    ecs_entity_t block = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, block, Position, {0, 0.5f, 0});
    ecs_set(world, block, Box, {1, 1, 1});
    ecs_set(world, block, Emissive, {.strength = 2, .color = {255, 255, 255}});
    ecs_entity_t texture = ecs_new(world);
    ecs_entity_t other = ecs_new(world);
    ecs_entity_t prefab = ecs_new(world);
    ecs_add_id(world, prefab, EcsPrefab);
    ecs_set(world, prefab, PbrTextures, {.albedo = texture});
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_add_pair(world, instance, EcsChildOf, scene);
    ecs_set(world, instance, Position, {12, 0.5f, 0});
    ecs_set(world, instance, Box, {1, 1, 1});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 32, .height = 24, .resolution_scale = 1});

    int32_t frames = 0, i;
    while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    const LightBakeStatus *status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, true);
    int32_t probes = status->probes, baked = status->baked;
    for (i = 0; i < 2; i ++) {
        if (i == 0) {
            ecs_set(world, group, Opacity, {.value = 0.5f});
        } else {
            ecs_set(world, prefab, PbrTextures, {.albedo = other});
        }

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
    }

    ecs_fini(world);
}

void Invalidation_newly_valid_probe_hidden_until_baked(void) {
    // baked_lighting/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    char *path = flecs_asprintf("%s/valid.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    app = ecs_singleton_get_mut(world, App);
    app->headless = false;
    ecs_singleton_modified(world, App);

    ecs_entity_t scene = ecs_new(world);
    ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
        ? ecs_singleton_get(world, Scene) : &(Scene){0});
    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 1,
        .capture_size = 8, .budget_ms = 1000000, .bounces = 0, .compute = true});
    ecs_set(world, scene, AmbientLight, {.intensity = 0});
    ecs_entity_t dark = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, dark, DirectionalLight, {.intensity = 0});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 9, 0});
    ecs_set(world, camera, Rotation, {-1.5708f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true, .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {12, 0.2f, 4});
    ecs_set(world, floor, Rgb, {230, 230, 230});
    ecs_entity_t post = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, post, Position, {-5.7f, 1.5f, -1.7f});
    ecs_set(world, post, Box, {0.2f, 3, 0.2f});
    ecs_entity_t glow = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, glow, Position, {-3, 0.3f, 0});
    ecs_set(world, glow, Box, {0.6f, 0.6f, 0.6f});
    ecs_set(world, glow, Rgb, {0, 0, 0});
    ecs_set(world, glow, Emissive, {.strength = 4, .color = {255, 255, 255}});
    ecs_entity_t lamp = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, lamp, Position, {1.2f, 0.3f, 1.5f});
    ecs_set(world, lamp, Box, {0.4f, 0.4f, 0.4f});
    ecs_set(world, lamp, Rgb, {0, 0, 0});
    ecs_set(world, lamp, Emissive, {.strength = 4, .color = {255, 255, 255}});
    ecs_entity_t block = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, block, Position, {2.5f, 0.8f, 0});
    ecs_set(world, block, Box, {1.6f, 1.6f, 1.6f});
    ecs_set(world, block, Rgb, {0, 0, 0});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.texture = e, .width = 64, .height = 48, .write_to_file = path,
        .resolution_scale = 1});

    int32_t frames = 0, n = 0, iw = 0, ih = 0, x, y;
    while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 1,
        .capture_size = 8, .budget_ms = 0.001f, .bounces = 0, .compute = true});
    ecs_set(world, glow, Emissive, {.strength = 5, .color = {255, 255, 255}});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_bool(ecs_get(world, scene, LightBakeStatus)->converged, false);
    ecs_delete(world, block);

    int32_t first = -1, lowest = 0, last = 0;
    frames = 0;
    do {
        ecs_progress(world, 0);
        box_render_sync(world);
        unsigned char *image = stbi_load(path, &iw, &ih, &n, 4);
        test_assert(image != NULL);
        int32_t sum = 0;
        for (y = 21; y < 27; y ++) {
            for (x = 40; x < 44; x ++) {
                sum += image[(y * iw + x) * 4 + 1];
            }
        }

        stbi_image_free(image);
        if (first == -1) {
            first = lowest = sum;
        }

        lowest = sum < lowest ? sum : lowest;
        last = sum;
        frames ++;
    } while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000);

    test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
    test_assert(frames > 10);
    test_assert(lowest >= (first < last ? first : last) - 24);

    ecs_os_free(path);
    ecs_fini(world);
}

void Invalidation_full_rebakes_keep_values(void) {
    // baked_lighting/invalidation
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);
        char *path = flecs_asprintf("%s/full.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
        app = ecs_singleton_get_mut(world, App);
        app->headless = false;
        ecs_singleton_modified(world, App);

        ecs_entity_t scene = ecs_new(world);
        ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
            ? ecs_singleton_get(world, Scene) : &(Scene){0});
        ecs_set(world, scene, LightBake, {.probe_spacing = 3, .bounds_padding = 0,
            .capture_size = 8, .budget_ms = 1000000, .bounces = 1, .compute = true});
        ecs_set(world, scene, AmbientLight, {.sky = {40, 40, 40}, .ground = {40, 40, 40},
            .intensity = 1});
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
        ecs_set(world, e, Surface, {.texture = e, .width = 64, .height = 48, .write_to_file = path,
            .resolution_scale = 1});

        int32_t frames = 0, n = 0, iw = 0, ih = 0, x, y;
        while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
            ecs_progress(world, 0);
            box_render_sync(world);
            frames ++;
        }

        test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
        ecs_set(world, scene, LightBake, {.probe_spacing = 3, .bounds_padding = 0,
            .capture_size = 8, .budget_ms = 0.001f, .bounces = 1, .compute = true});
        ecs_progress(world, 0);
        box_render_sync(world);
        test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
        unsigned char *image = stbi_load(path, &iw, &ih, &n, 4);
        test_assert(image != NULL);
        int32_t before = 0;
        for (y = 20; y < 28; y ++) {
            for (x = 36; x < 44; x ++) {
                before += image[(y * iw + x) * 4 + 1];
            }
        }

        stbi_image_free(image);
        if (i == 0) {
            ecs_set(world, scene, AmbientLight, {.sky = {50, 50, 50}, .ground = {40, 40, 40},
                .intensity = 1});
        } else if (i == 1) {
            ecs_set(world, scene, LightBake, {.probe_spacing = 3, .bounds_padding = 0,
                .capture_size = 12, .budget_ms = 0.001f, .bounces = 1, .compute = true});
        } else {
            ecs_set(world, scene, LightBake, {.probe_spacing = 3, .bounds_padding = 0,
                .capture_size = 8, .budget_ms = 0.001f, .bounces = 2, .compute = true});
        }

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
        test_assert(partial > 3);
        test_assert(lowest >= before - 64 * 2);
        ecs_os_free(path);
        ecs_fini(world);
    }
}

void Invalidation_tint_rebakes_nothing(void) {
    // baked_lighting/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t scene = ecs_new(world);
    ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
        ? ecs_singleton_get(world, Scene) : &(Scene){0});
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 1,
        .capture_size = 8, .budget_ms = 2, .bounces = 0, .compute = true});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 4, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {48, 0.2f, 4});
    ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, group, Position, {-12, 0, 0});
    ecs_entity_t block = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, block, Position, {0, 0.5f, 0});
    ecs_set(world, block, Box, {1, 1, 1});
    ecs_entity_t faded = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, faded, Position, {12, 0.5f, 0});
    ecs_set(world, faded, Box, {1, 1, 1});
    ecs_set(world, faded, Tint, {.r = 255, .g = 0, .b = 0, .a = 0});
    box_transition_set(world, faded, ecs_id(Tint), &(TransitionSpec){ .duration = 1.0f });
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 32, .height = 24, .resolution_scale = 1});

    int32_t frames = 0, i, j;
    while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    const LightBakeStatus *status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, true);
    int32_t baked = status->baked;
    for (i = 0; i < 4; i ++) {
        if (i == 0) {
            ecs_set(world, group, Tint, {.r = 255, .g = 0, .b = 0, .a = 128});
        } else if (i == 1) {
            ecs_set(world, block, Tint, {.r = 0, .g = 255, .b = 0, .a = 128});
        } else if (i == 2) {
            ecs_remove(world, group, Tint);
        } else {
            ecs_set(world, faded, Tint, {.r = 255, .g = 0, .b = 0, .a = 128});
        }

        for (j = 0; j < 3; j ++) {
            ecs_progress(world, 0);
            box_render_sync(world);
            status = ecs_get(world, scene, LightBakeStatus);
            test_bool(status->converged, true);
            test_int(status->baked, baked);
        }
    }

    ecs_fini(world);
}

void Invalidation_bake_ignores_tint(void) {
    // baked_lighting/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    char *path = flecs_asprintf("%s/bake_tint.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    app = ecs_singleton_get_mut(world, App);
    app->headless = false;
    ecs_singleton_modified(world, App);

    ecs_entity_t scene = ecs_new(world);
    ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
        ? ecs_singleton_get(world, Scene) : &(Scene){0});
    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 1,
        .capture_size = 16, .budget_ms = 1000000, .bounces = 0, .compute = true});
    ecs_set(world, scene, AmbientLight, {.intensity = 0});
    ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, sun, DirectionalLight, {.intensity = 4});
    ecs_set(world, sun, Rotation, {0, -1.5708f, 0});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 7, 0});
    ecs_set(world, camera, Rotation, {-1.5708f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 50, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true, .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {8, 0.2f, 6});
    ecs_set(world, floor, Rgb, {230, 230, 230});
    ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, wall, Position, {-3, 2, 0});
    ecs_set(world, wall, Box, {0.4f, 4, 6});
    ecs_set(world, wall, Rgb, {255, 255, 255});
    ecs_set(world, wall, Tint, {.r = 255, .g = 0, .b = 0, .a = 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.texture = e, .width = 64, .height = 48, .write_to_file = path,
        .resolution_scale = 1});

    int32_t frames = 0, n = 0, iw = 0, ih = 0, x, y;
    while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
    ecs_progress(world, 0);
    box_render_sync(world);
    unsigned char *image = stbi_load(path, &iw, &ih, &n, 4);
    test_assert(image != NULL);
    int32_t red = 0, green = 0;
    for (y = 20; y < 28; y ++) {
        for (x = 28; x < 36; x ++) {
            red += image[(y * iw + x) * 4];
            green += image[(y * iw + x) * 4 + 1];
        }
    }

    test_assert(green > 64);
    test_assert(abs(red - green) * 10 < green);

    stbi_image_free(image);
    ecs_os_free(path);
    ecs_fini(world);
}

void Invalidation_same_transform_rebakes_nothing(void) {
    // baked_lighting/invalidation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t scene = ecs_new(world);
    ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
        ? ecs_singleton_get(world, Scene) : &(Scene){0});
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 1,
        .capture_size = 8, .budget_ms = 2, .bounces = 0, .compute = true});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 4, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {48, 0.2f, 4});
    ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, group, Position, {-12, 0, 0});
    ecs_set(world, group, Rotation, {0, 0.5f, 0});
    ecs_entity_t block = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, block, Position, {0, 0.5f, 0});
    ecs_set(world, block, Box, {1, 1, 1});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 32, .height = 24, .resolution_scale = 1});

    int32_t frames = 0, j;
    while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        frames ++;
    }

    const LightBakeStatus *status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, true);
    int32_t baked = status->baked;
    ecs_set(world, group, Position, {-12, 0, 0});
    ecs_set(world, group, Rotation, {0, 0.5f, 0});
    for (j = 0; j < 3; j ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
        status = ecs_get(world, scene, LightBakeStatus);
        test_bool(status->converged, true);
        test_int(status->baked, baked);
    }

    ecs_set(world, group, Position, {-11, 0, 0});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_bool(ecs_get(world, scene, LightBakeStatus)->converged, false);

    ecs_fini(world);
}

void Invalidation_moved_caster_matches_fresh_bake(void) {
    // baked_lighting/invalidation
    unsigned char *images[2] = {NULL, NULL};
    int w[2] = {0, 0}, h[2] = {0, 0}, n = 0, i, largest = 0;
    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);

        ecs_entity_t scene = ecs_new(world);
        ecs_set_ptr(world, scene, Scene, ecs_singleton_get(world, Scene)
            ? ecs_singleton_get(world, Scene) : &(Scene){0});
        ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 1,
            .capture_size = 16, .budget_ms = 2, .bounces = 1, .compute = true});
        ecs_set(world, scene, AmbientLight, {.intensity = 0});
        ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, camera, Position, {4, 1.5f, 0});
        ecs_set(world, camera, Rotation, {0, 1.5707963f, 0});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
            .lighting.baked = true, .shadows.enabled = true});
        ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, sun, Rotation, {1.5707963f, 0, 0});
        ecs_set(world, sun, DirectionalLight, {.intensity = 12});
        ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, floor, Position, {0, -0.1f, 0});
        ecs_set(world, floor, Box, {10, 0.2f, 10});
        ecs_set(world, floor, Rgb, {255, 255, 255});
        ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, wall, Position, {-5, 2, 0});
        ecs_set(world, wall, Box, {0.2f, 4, 10});
        ecs_set(world, wall, Rgb, {255, 255, 255});
        ecs_entity_t caster = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, caster, Position, {i ? -2.5f : 2.5f, 3, 0});
        ecs_set(world, caster, Box, {5, 0.2f, 10});
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Surface, {.width = 64, .height = 48, .write_to_file = "caster.png",
            .resolution_scale = 1});
        char *path = flecs_asprintf("%s/caster.png",
            ecs_singleton_get(world, AppStatus)->tmp_dir);

        int32_t frames = 0;
        while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
            ecs_progress(world, 0);
            box_render_sync(world);
            frames ++;
        }

        test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
        if (i) {
            ecs_set(world, caster, Position, {2.5f, 3, 0});
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
        }

        ecs_progress(world, 0);
        box_render_sync(world);
        images[i] = stbi_load(path, &w[i], &h[i], &n, 4);
        test_assert(images[i] != NULL);
        ecs_os_free(path);
        ecs_fini(world);
    }

    test_int(w[0], w[1]);
    test_int(h[0], h[1]);
    for (i = 0; i < w[0] * h[0] * 4; i ++) {
        int d = abs((int)images[0][i] - (int)images[1][i]);
        largest = d > largest ? d : largest;
    }

    test_assert(largest <= 2);
    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
}
