/**
 * @file Bounces.c
 * @brief Tests for the passes of a light bake, one more than its bounces.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Bounces_pass_reaches_bounces(void) {
    // baked_lighting/bounces
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 0.001f, .bounces = 3});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 1, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {2, 0.2f, 2});
    ecs_entity_t block = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, block, Position, {0, 0.5f, 0});
    ecs_set(world, block, Box, {0.5f, 0.5f, 0.5f});
    ecs_set(world, block, Emissive, {.strength = 2, .color = {255, 200, 100}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 32, .height = 24, .resolution_scale = 1});

    int32_t frame, last = 0, drops = 0, previous = 0;
    bool seen[4] = {false, false, false, false};
    const LightBakeStatus *status = NULL;
    for (frame = 0; frame < 2000; frame ++) {
        ecs_progress(world, 0);
        status = ecs_get(world, scene, LightBakeStatus);
        test_assert(status->pass >= last);
        test_assert(status->pass <= 3);
        drops += status->baked < previous;
        last = status->pass;
        previous = status->baked;
        seen[status->pass] = true;
        if (status->converged) {
            break;
        }
    }

    test_assert(status->converged);
    test_int(status->pass, 3);
    test_assert(status->baked > 0);
    test_int(drops, 3);
    test_assert(seen[0] && seen[1] && seen[2] && seen[3]);

    ecs_fini(world);
}

void Bounces_zero_bounces_bakes_one_pass(void) {
    // baked_lighting/bounces
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 0.001f, .bounces = 0});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 1, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {2, 0.2f, 2});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 32, .height = 24, .resolution_scale = 1});

    int32_t frame, previous = 0;
    const LightBakeStatus *status = NULL;
    for (frame = 0; frame < 1000; frame ++) {
        ecs_progress(world, 0);
        status = ecs_get(world, scene, LightBakeStatus);
        test_assert(status->baked >= previous);
        test_int(status->pass, 0);
        previous = status->baked;
        if (status->converged) {
            break;
        }
    }

    test_assert(status->converged);
    test_int(status->pass, 0);
    test_assert(status->baked > 0);
    test_assert(status->error == NULL);

    ecs_fini(world);
}

void Bounces_out_of_range_bakes_nothing(void) {
    // baked_lighting/bounces
    int32_t values[2] = {-1, 17};
    int32_t i, frame;

    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);

        ecs_singleton_set(world, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
            .capture_size = 8, .budget_ms = 20, .bounces = values[i]});
        ecs_entity_t camera = ecs_new(world);
        ecs_set(world, camera, Position, {0, 1, 5});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
            .lighting.baked = true});
        ecs_entity_t floor = ecs_new(world);
        ecs_set(world, floor, Position, {0, -0.1f, 0});
        ecs_set(world, floor, Box, {2, 0.2f, 2});
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Surface, {.width = 32, .height = 24, .resolution_scale = 1});

        for (frame = 0; frame < 30; frame ++) {
            ecs_progress(world, 0);
        }

        const LightBakeStatus *status = ecs_get(world, ecs_id(LightBake), LightBakeStatus);
        test_assert(status->probes > 0);
        test_int(status->baked, 0);
        test_bool(status->converged, false);
        test_assert(status->error != NULL);

        ecs_singleton_set(world, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
            .capture_size = 8, .budget_ms = 20, .bounces = 16});
        for (frame = 0; frame < 2000; frame ++) {
            ecs_progress(world, 0);
            if (ecs_get(world, ecs_id(LightBake), LightBakeStatus)->converged) {
                break;
            }
        }

        status = ecs_get(world, ecs_id(LightBake), LightBakeStatus);
        test_bool(status->converged, true);
        test_int(status->pass, 16);
        test_assert(status->error == NULL);

        ecs_fini(world);
    }
}

void Bounces_changing_bounces_restarts(void) {
    // baked_lighting/bounces
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 0.001f, .bounces = 1});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 1, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {2, 0.2f, 2});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 32, .height = 24, .resolution_scale = 1});

    int32_t frame;
    const LightBakeStatus *status = NULL;
    for (frame = 0; frame < 2000; frame ++) {
        ecs_progress(world, 0);
        status = ecs_get(world, scene, LightBakeStatus);
        if (status->converged) {
            break;
        }
    }

    test_assert(status->converged);
    test_int(status->pass, 1);
    int32_t probes = status->baked;

    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 0.001f, .bounces = 2});
    ecs_progress(world, 0);
    status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, false);
    test_int(status->pass, 0);
    test_assert(status->baked < probes);

    for (frame = 0; frame < 2000; frame ++) {
        ecs_progress(world, 0);
        status = ecs_get(world, scene, LightBakeStatus);
        if (status->converged) {
            break;
        }
    }

    test_assert(status->converged);
    test_int(status->pass, 2);
    test_int(status->baked, probes);

    ecs_fini(world);
}

void Bounces_image_stable_once_converged(void) {
    // baked_lighting/bounces
    unsigned char *images[2] = {NULL, NULL};
    int w[2] = {0, 0}, h[2] = {0, 0};
    int32_t i, frame, largest = 0;

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 0.05f, .bounces = 2});
    ecs_set(world, scene, AmbientLight, {.intensity = 0});
    ecs_entity_t dark = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, dark, DirectionalLight, {.intensity = 0});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 1, 4});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true, .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {4, 0.2f, 4});
    ecs_set(world, floor, Rgb, {220, 220, 220});
    ecs_entity_t back = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, back, Position, {0, 1, -2.1f});
    ecs_set(world, back, Box, {4, 2, 0.2f});
    ecs_set(world, back, Rgb, {220, 220, 220});
    ecs_entity_t lamp = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, lamp, Position, {-1.2f, 0.4f, -1.2f});
    ecs_set(world, lamp, Box, {0.6f, 0.6f, 0.6f});
    ecs_set(world, lamp, Emissive, {.strength = 8, .color = {255, 255, 255}});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    char *path = flecs_asprintf("%s/stable.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    app = ecs_singleton_get_mut(world, App);
    app->headless = false;
    ecs_singleton_modified(world, App);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 48, .write_to_file = path,
        .resolution_scale = 1});

    const LightBakeStatus *status = NULL;
    for (frame = 0; frame < 2000; frame ++) {
        ecs_progress(world, 0);
        status = ecs_get(world, scene, LightBakeStatus);
        if (status->converged) {
            break;
        }
    }

    test_assert(status->converged);
    test_assert(frame > 10);
    for (i = 0; i < 2; i ++) {
        box_render_sync(world);
        int n = 0;
        images[i] = stbi_load(path, &w[i], &h[i], &n, 4);
        test_assert(images[i] != NULL);
        for (frame = 0; frame < 120; frame ++) {
            ecs_progress(world, 0);
        }

        test_bool(ecs_get(world, scene, LightBakeStatus)->converged, true);
    }

    test_int(w[0], w[1]);
    test_int(h[0], h[1]);
    long sum = 0;
    for (i = 0; i < w[0] * h[0] * 4; i ++) {
        int d = abs((int)images[0][i] - (int)images[1][i]);
        largest = d > largest ? d : largest;
        sum += (i % 4) != 3 ? images[0][i] : 0;
    }

    test_int(largest, 0);
    test_assert(sum > w[0] * h[0] * 10);
    ecs_os_free(path);
    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
    ecs_fini(world);
}

void Bounces_view_keeps_last_complete_pass(void) {
    // baked_lighting/bounces
    unsigned char *shown = NULL;
    int w = 0, h = 0;
    int32_t i, frame, changes = 0, partial = 0, previous = 0;
    bool second = false;

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 0.001f, .bounces = 1});
    ecs_set(world, scene, AmbientLight, {.intensity = 0});
    ecs_entity_t dark = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, dark, DirectionalLight, {.intensity = 0});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 1, 4});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true, .lighting.baked = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {4, 0.2f, 4});
    ecs_set(world, floor, Rgb, {220, 220, 220});
    ecs_entity_t back = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, back, Position, {0, 1, -2.1f});
    ecs_set(world, back, Box, {4, 2, 0.2f});
    ecs_set(world, back, Rgb, {220, 220, 220});
    ecs_entity_t lamp = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, lamp, Position, {-1.2f, 0.4f, -1.2f});
    ecs_set(world, lamp, Box, {0.6f, 0.6f, 0.6f});
    ecs_set(world, lamp, Emissive, {.strength = 8, .color = {255, 255, 255}});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    char *path = flecs_asprintf("%s/passes.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    app = ecs_singleton_get_mut(world, App);
    app->headless = false;
    ecs_singleton_modified(world, App);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 48, .write_to_file = path,
        .resolution_scale = 1});

    const LightBakeStatus *status = NULL;
    for (frame = 0; frame < 2000; frame ++) {
        ecs_progress(world, 0);
        status = ecs_get(world, scene, LightBakeStatus);
        second |= status->baked < previous;
        previous = status->baked;
        if (!second) {
            continue;
        }

        box_render_sync(world);
        int n = 0, iw = 0, ih = 0;
        unsigned char *image = stbi_load(path, &iw, &ih, &n, 4);
        test_assert(image != NULL);
        if (shown) {
            test_int(iw, w);
            test_int(ih, h);
            changes += memcmp(shown, image, (size_t)(w * h * 4)) != 0;
            stbi_image_free(shown);
        }

        shown = image;
        w = iw;
        h = ih;
        partial += !status->converged;
        if (status->converged) {
            break;
        }
    }

    test_assert(status->converged);
    test_assert(partial > 20);
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
        int n = 0, iw = 0, ih = 0;
        unsigned char *image = stbi_load(path, &iw, &ih, &n, 4);
        test_assert(image != NULL);
        changes += memcmp(shown, image, (size_t)(w * h * 4)) != 0;
        stbi_image_free(shown);
        shown = image;
    }

    test_int(changes, 1);
    stbi_image_free(shown);
    ecs_os_free(path);
    ecs_fini(world);
}
