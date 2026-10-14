/**
 * @file ProbeCapture.c
 * @brief Tests for the incremental probe captures that bake the probe volume of a scene.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void ProbeCapture_converges_in_bounded_frames(void) {
    // baked_lighting/probe_capture
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 2, .bounces = 1});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 1, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {4, 0.2f, 4});
    ecs_entity_t block = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, block, Position, {0, 0.5f, 0});
    ecs_set(world, block, Box, {1, 1, 1});
    ecs_set(world, block, Emissive, {.strength = 2, .color = {255, 200, 100}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 48, .resolution_scale = 1});

    int32_t frame;
    const LightBakeStatus *status = NULL;
    for (frame = 0; frame < 400; frame ++) {
        ecs_progress(world, 0);
        status = ecs_get(world, scene, LightBakeStatus);
        if (status->converged) {
            break;
        }
    }

    test_assert(status->converged);
    test_assert(frame < 400);
    test_assert(status->probes > 0);
    test_assert(status->baked > 0);
    test_assert(status->baked <= status->probes);
    test_int(status->pass, 1);

    ecs_fini(world);
}

void ProbeCapture_zero_budget_never_converges(void) {
    // baked_lighting/probe_capture
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 0, .bounces = 1});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 1, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {4, 0.2f, 4});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 48, .resolution_scale = 1});

    int32_t frame;
    for (frame = 0; frame < 100; frame ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    ecs_progress(world, 0);
    const LightBakeStatus *status = ecs_get(world, scene, LightBakeStatus);
    test_assert(status->probes > 0);
    test_int(status->baked, 0);
    test_bool(status->converged, false);

    ecs_fini(world);
}

void ProbeCapture_dynamic_shape_lit_not_captured(void) {
    // baked_lighting/probe_capture
    unsigned char *images[3] = {NULL, NULL, NULL};
    int w[3] = {0, 0, 0}, h[3] = {0, 0, 0};
    const float walls[6][6] = {
        {0, -0.1f, 0, 6.4f, 0.2f, 6.4f}, {0, 3.1f, 0, 6.4f, 0.2f, 6.4f},
        {-3.1f, 1.5f, 0, 0.2f, 3, 6.4f}, {3.1f, 1.5f, 0, 0.2f, 3, 6.4f},
        {0, 1.5f, -3.1f, 6.4f, 3, 0.2f}, {0, 1.5f, 3.1f, 6.4f, 3, 0.2f}
    };
    int i, j, largest = 0;

    for (i = 0; i < 3; i ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);

        ecs_entity_t scene = ecs_new(world);
        ecs_add(world, scene, Scene);
        ecs_set(world, scene, LightBake, {.probe_spacing = 0.75f, .bounds_padding = 0,
            .capture_size = 16, .budget_ms = 2, .bounces = 1});
        ecs_set(world, scene, AmbientLight, {.intensity = 0});
        ecs_entity_t dark = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, dark, DirectionalLight, {.intensity = 0});
        ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, camera, Position, {0, 1.5f, 2.4f});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
            .color = {0, 0, 0, 255}}, .lighting.enabled = true, .lighting.baked = i != 2});
        for (j = 0; j < 6; j ++) {
            ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, scene);
            ecs_set(world, wall, Position, {walls[j][0], walls[j][1], walls[j][2]});
            ecs_set(world, wall, Box, {walls[j][3], walls[j][4], walls[j][5]});
            ecs_set(world, wall, Rgb, {220, 220, 220});
        }

        ecs_entity_t lamp = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, lamp, Position, {0, 2.9f, -2.9f});
        ecs_set(world, lamp, Box, {1.2f, 0.2f, 0.6f});
        ecs_set(world, lamp, Emissive, {.strength = 60, .color = {255, 255, 255}});
        ecs_entity_t mover = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_add(world, mover, DynamicTransform);
        ecs_set(world, mover, Position, {0, 1.2f, -0.5f});
        ecs_set(world, mover, Box, {0.8f, 0.8f, 0.8f});
        ecs_set(world, mover, Rgb, {255, 255, 255});
        if (i == 1) {
            ecs_entity_t glow = ecs_new_w_pair(world, EcsChildOf, scene);
            ecs_add(world, glow, DynamicTransform);
            ecs_set(world, glow, Position, {0, 1.5f, 2.8f});
            ecs_set(world, glow, Box, {0.3f, 0.3f, 0.3f});
            ecs_set(world, glow, Emissive, {.strength = 20, .color = {40, 255, 40}});
        }

        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Surface, {.width = 64, .height = 48, .write_to_file = "dyn.png",
            .resolution_scale = 1});
        for (j = 0; j < 3; j ++) {
            ecs_progress(world, 0);
        }

        box_render_sync(world);
        ecs_progress(world, 0);
        box_render_sync(world);
        test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);

        char *path = flecs_asprintf("%s/dyn.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
        int n = 0;
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

    test_assert(largest <= 1);

    int center = ((h[0] / 2) * w[0] + w[0] / 2) * 4;
    int lit = images[0][center] + images[0][center + 1] + images[0][center + 2];
    int unbaked = images[2][center] + images[2][center + 1] + images[2][center + 2];
    test_assert(lit > 60);
    test_assert(unbaked < 6);

    for (i = 0; i < 3; i ++) {
        stbi_image_free(images[i]);
    }
}

void ProbeCapture_no_bake_removes_contribution(void) {
    // baked_lighting/probe_capture
    unsigned char *images[3] = {NULL, NULL, NULL};
    int w[3] = {0, 0, 0}, h[3] = {0, 0, 0};
    const float walls[6][6] = {
        {0, -0.1f, 0, 6.4f, 0.2f, 6.4f}, {0, 3.1f, 0, 6.4f, 0.2f, 6.4f},
        {-3.1f, 1.5f, 0, 0.2f, 3, 6.4f}, {3.1f, 1.5f, 0, 0.2f, 3, 6.4f},
        {0, 1.5f, -3.1f, 6.4f, 3, 0.2f}, {0, 1.5f, 3.1f, 6.4f, 3, 0.2f}
    };
    int i, j, largest = 0;
    long sums[3] = {0, 0, 0};

    for (i = 0; i < 3; i ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);

        ecs_entity_t scene = ecs_new(world);
        ecs_add(world, scene, Scene);
        ecs_set(world, scene, LightBake, {.probe_spacing = 0.75f, .bounds_padding = 0,
            .capture_size = 16, .budget_ms = 2, .bounces = 1});
        ecs_set(world, scene, AmbientLight, {.intensity = 0});
        ecs_entity_t dark = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, dark, DirectionalLight, {.intensity = 0});
        ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, camera, Position, {0, 1.5f, 2.4f});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
            .color = {0, 0, 0, 255}}, .lighting.enabled = true, .lighting.baked = true});
        for (j = 0; j < 6; j ++) {
            ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, scene);
            ecs_set(world, wall, Position, {walls[j][0], walls[j][1], walls[j][2]});
            ecs_set(world, wall, Box, {walls[j][3], walls[j][4], walls[j][5]});
            ecs_set(world, wall, Rgb, {220, 220, 220});
        }

        ecs_entity_t lamp = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, lamp, Position, {0, 1.5f, -2.9f});
        ecs_set(world, lamp, Box, {0.6f, 0.6f, 0.2f});
        ecs_set(world, lamp, Rgb, {0, 0, 0});
        ecs_set(world, lamp, Emissive, {.strength = 6, .color = {255, 255, 255}});
        if (i == 0) {
            ecs_add(world, lamp, NoBake);
        } else if (i == 1) {
            ecs_set(world, lamp, Emissive, {.strength = 0, .color = {0, 0, 0}});
        }

        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Surface, {.width = 64, .height = 48, .write_to_file = "nobake.png",
            .resolution_scale = 1});
        for (j = 0; j < 3; j ++) {
            ecs_progress(world, 0);
        }

        box_render_sync(world);
        ecs_progress(world, 0);
        box_render_sync(world);
        test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);

        char *path = flecs_asprintf("%s/nobake.png",
            ecs_singleton_get(world, AppStatus)->tmp_dir);
        int n = 0;
        images[i] = stbi_load(path, &w[i], &h[i], &n, 4);
        test_assert(images[i] != NULL);
        ecs_os_free(path);
        ecs_fini(world);
    }

    for (i = 0; i < 3; i ++) {
        for (j = 0; j < w[i] * h[i] * 4; j ++) {
            sums[i] += (j % 4) != 3 ? images[i][j] : 0;
        }
    }

    test_int(w[0], w[1]);
    test_int(h[0], h[1]);
    for (j = 0; j < w[0] * h[0] * 4; j ++) {
        int x = (j / 4) % w[0], y = (j / 4) / w[0];
        bool lamp = x > w[0] / 2 - 6 && x < w[0] / 2 + 6 && y > h[0] / 2 - 6 && y < h[0] / 2 + 6;
        int d = abs((int)images[0][j] - (int)images[1][j]);
        largest = !lamp && d > largest ? d : largest;
    }

    test_assert(largest <= 1);
    test_assert(sums[2] > sums[0] * 2 + w[0] * h[0] * 10);

    for (i = 0; i < 3; i ++) {
        stbi_image_free(images[i]);
    }
}

void ProbeCapture_perf_run_waits_for_convergence(void) {
    // baked_lighting/probe_capture
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 0.001f, .bounces = 1});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 1, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.baked = true});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {3, 0.2f, 3});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 48, .resolution_scale = 1});
    ecs_singleton_set(world, PerfRun, {.fps = 120, .frames = 4, .warmup = 2});

    int32_t frame;
    for (frame = 0; frame < 12; frame ++) {
        ecs_progress(world, 0);
    }

    test_bool(ecs_get(world, ecs_id(LightBake), LightBakeStatus)->converged, false);
    test_int(ecs_singleton_get(world, PerfStatus)->state, PerfWarmup);

    bool converged = false;
    for (frame = 0; frame < 400; frame ++) {
        ecs_progress(world, 0);
        const LightBakeStatus *status = ecs_get(world, ecs_id(LightBake), LightBakeStatus);
        if (!converged && status->converged) {
            converged = true;
            test_int(ecs_singleton_get(world, PerfStatus)->state, PerfWarmup);
        }

        if (ecs_singleton_get(world, PerfStatus)->state == PerfDone) {
            break;
        }
    }

    test_assert(converged);
    test_int(ecs_singleton_get(world, PerfStatus)->state, PerfDone);

    ecs_fini(world);
}
