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
    bool seen[5] = {false, false, false, false, false};
    const LightBakeStatus *status = NULL;
    for (frame = 0; frame < 5000; frame ++) {
        ecs_progress(world, 0);
        status = ecs_get(world, scene, LightBakeStatus);
        test_assert(status->pass >= last);
        test_assert(status->pass <= 4);
        test_bool(status->converged, status->pass == 4);
        if (status->baked < previous) {
            test_assert(status->pass > last);
            drops ++;
        }

        last = status->pass;
        previous = status->baked;
        seen[status->pass] = true;
        if (status->converged) {
            break;
        }
    }

    test_assert(status->converged);
    test_int(status->pass, 4);
    test_assert(status->baked > 0);
    test_int(drops, 3);
    test_assert(seen[0] && seen[1] && seen[2] && seen[3] && seen[4]);

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
    for (frame = 0; frame < 5000; frame ++) {
        ecs_progress(world, 0);
        status = ecs_get(world, scene, LightBakeStatus);
        test_assert(status->baked >= previous);
        test_int(status->pass, status->converged ? 1 : 0);
        previous = status->baked;
        if (status->converged) {
            break;
        }
    }

    test_assert(status->converged);
    test_int(status->pass, 1);
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

        ecs_singleton_set(world, LightBake, {.probe_spacing = 2, .bounds_padding = 0.5f,
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

        ecs_singleton_set(world, LightBake, {.probe_spacing = 2, .bounds_padding = 0.5f,
            .capture_size = 8, .budget_ms = 20, .bounces = 16});
        for (frame = 0; frame < 5000; frame ++) {
            ecs_progress(world, 0);
            if (ecs_get(world, ecs_id(LightBake), LightBakeStatus)->converged) {
                break;
            }
        }

        status = ecs_get(world, ecs_id(LightBake), LightBakeStatus);
        test_bool(status->converged, true);
        test_int(status->pass, 17);
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
    for (frame = 0; frame < 5000; frame ++) {
        ecs_progress(world, 0);
        status = ecs_get(world, scene, LightBakeStatus);
        if (status->converged) {
            break;
        }
    }

    test_assert(status->converged);
    test_int(status->pass, 2);
    int32_t probes = status->baked;

    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0.5f,
        .capture_size = 8, .budget_ms = 0.001f, .bounces = 2});
    ecs_progress(world, 0);
    status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, false);
    test_int(status->pass, 0);
    test_assert(status->baked < probes);

    for (frame = 0; frame < 5000; frame ++) {
        ecs_progress(world, 0);
        status = ecs_get(world, scene, LightBakeStatus);
        if (status->converged) {
            break;
        }
    }

    test_assert(status->converged);
    test_int(status->pass, 3);
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
    for (frame = 0; frame < 5000; frame ++) {
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
    for (frame = 0; frame < 5000; frame ++) {
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

void Bounces_far_end_brightens_with_bounces(void) {
    // baked_lighting/bounces
    const int32_t bounces[3] = {0, 1, 3};
    const float parts[4][6] = {
        {0, -0.1f, 0, 6.4f, 0.2f, 2.8f}, {0, 1.2f, 1.3f, 6.4f, 2.4f, 0.2f},
        {0, 1.2f, -1.3f, 6.4f, 2.4f, 0.2f}, {3.1f, 1.2f, 0, 0.2f, 2.4f, 2.4f}
    };
    int32_t far[3] = {0, 0, 0}, lit[3] = {0, 0, 0};
    int32_t i, j, x, y;

    for (i = 0; i < 3; i ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);

        ecs_entity_t scene = ecs_new(world);
        ecs_add(world, scene, Scene);
        ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = -0.7f,
            .capture_size = 16, .budget_ms = 2, .bounces = bounces[i]});
        ecs_set(world, scene, AmbientLight, {.intensity = 0});
        ecs_entity_t dark = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, dark, DirectionalLight, {.intensity = 0});
        ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, camera, Position, {0, 8, 0});
        ecs_set(world, camera, Rotation, {-1.5708f, 0, 0});
        ecs_set(world, camera, Camera, {.fov = 50, .near_ = 0.1f});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
            .color = {0, 0, 0, 255}}, .lighting.enabled = true, .lighting.baked = true});
        ecs_entity_t ceiling = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, ceiling, Position, {0, 2.4f, 0});
        ecs_set(world, ceiling, Rotation, {1.5708f, 0, 0});
        ecs_set(world, ceiling, Quad, {6.4f, 2.8f});
        ecs_set(world, ceiling, Rgb, {250, 250, 250});
        for (j = 0; j < 4; j ++) {
            ecs_entity_t part = ecs_new_w_pair(world, EcsChildOf, scene);
            ecs_set(world, part, Position, {parts[j][0], parts[j][1], parts[j][2]});
            ecs_set(world, part, Box, {parts[j][3], parts[j][4], parts[j][5]});
            ecs_set(world, part, Rgb, {250, 250, 250});
        }

        ecs_entity_t panel = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, panel, Position, {-3.1f, 1.2f, 0});
        ecs_set(world, panel, Box, {0.2f, 2.4f, 2.4f});
        ecs_set(world, panel, Rgb, {0, 0, 0});
        ecs_set(world, panel, Emissive, {.strength = 0.8f, .color = {255, 236, 210}});
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Surface, {.width = 160, .height = 120, .write_to_file = "far.png",
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

        char *path = flecs_asprintf("%s/far.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
        int w = 0, h = 0, n = 0;
        unsigned char *image = stbi_load(path, &w, &h, &n, 4);
        test_assert(image != NULL);
        test_int(w, 160);
        test_int(h, 120);
        for (y = 56; y < 64; y ++) {
            for (x = 0; x < 8; x ++) {
                const unsigned char *l = &image[(y * w + 44 + x) * 4];
                const unsigned char *f = &image[(y * w + 108 + x) * 4];
                lit[i] += (l[0] + l[1] + l[2]) / 3;
                far[i] += (f[0] + f[1] + f[2]) / 3;
            }
        }

        lit[i] /= 64;
        far[i] /= 64;
        stbi_image_free(image);
        ecs_os_free(path);
        ecs_fini(world);
    }

    test_assert(far[1] > far[0] + 10);
    test_assert(far[2] > far[1] + 10);
    for (i = 0; i < 3; i ++) {
        test_assert(far[i] < lit[i]);
        test_assert(lit[i] < 255);
    }
}

void Bounces_colored_walls_bleed_with_bounces(void) {
    // baked_lighting/color_bleed
    const int32_t bounces[3] = {0, 1, 3};
    const float parts[5][6] = {
        {0, -0.1f, 0, 4.4f, 0.2f, 4.4f}, {0, 2, -2.1f, 4.4f, 4, 0.2f},
        {0, 2, 2.1f, 4.4f, 4, 0.2f}, {-2.1f, 2, 0, 0.2f, 4, 4}, {2.1f, 2, 0, 0.2f, 4, 4}
    };
    const uint8_t colors[5][3] = {
        {250, 250, 246}, {250, 250, 246}, {250, 250, 246}, {235, 30, 22}, {28, 215, 36}
    };
    float red[3] = {0, 0, 0}, green[3] = {0, 0, 0}, red_excess[3] = {0, 0, 0};
    float green_excess[3] = {0, 0, 0};
    int32_t i, j, x, y;

    for (i = 0; i < 3; i ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);

        ecs_entity_t scene = ecs_new(world);
        ecs_add(world, scene, Scene);
        ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = -0.7f,
            .capture_size = 16, .budget_ms = 2, .bounces = bounces[i]});
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
        ecs_entity_t ceiling = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, ceiling, Position, {0, 4, 0});
        ecs_set(world, ceiling, Rotation, {1.5708f, 0, 0});
        ecs_set(world, ceiling, Quad, {4.4f, 4.4f});
        ecs_set(world, ceiling, Rgb, {250, 250, 246});
        for (j = 0; j < 5; j ++) {
            ecs_entity_t part = ecs_new_w_pair(world, EcsChildOf, scene);
            ecs_set(world, part, Position, {parts[j][0], parts[j][1], parts[j][2]});
            ecs_set(world, part, Box, {parts[j][3], parts[j][4], parts[j][5]});
            ecs_set(world, part, Rgb, {colors[j][0], colors[j][1], colors[j][2]});
        }

        ecs_entity_t lamp = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, lamp, Position, {0, 3.9f, 0});
        ecs_set(world, lamp, Rotation, {1.5708f, 0, 0});
        ecs_set(world, lamp, SpotLight, {.intensity = 12 * GLM_PIf, .range = 10, .inner_angle = 15,
            .outer_angle = 25});
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Surface, {.width = 160, .height = 120, .write_to_file = "bleed.png",
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

        char *path = flecs_asprintf("%s/bleed.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
        int w = 0, h = 0, n = 0;
        unsigned char *image = stbi_load(path, &w, &h, &n, 4);
        test_assert(image != NULL);
        test_int(w, 160);
        test_int(h, 120);
        float sums[2][3] = {{0, 0, 0}, {0, 0, 0}};
        for (y = 66; y < 76; y ++) {
            for (x = 0; x < 6; x ++) {
                for (j = 0; j < 3; j ++) {
                    sums[0][j] += image[(y * w + 53 + x) * 4 + j];
                    sums[1][j] += image[(y * w + 101 + x) * 4 + j];
                }
            }
        }

        test_assert(sums[0][1] > 60 * 8 && sums[1][0] > 60 * 8);
        test_assert(sums[0][0] < 60 * 250 && sums[1][1] < 60 * 250);
        red[i] = sums[0][0] / sums[0][1];
        green[i] = sums[1][1] / sums[1][0];
        red_excess[i] = (sums[0][0] - sums[0][1]) / 60;
        green_excess[i] = (sums[1][1] - sums[1][0]) / 60;
        stbi_image_free(image);
        ecs_os_free(path);
        ecs_fini(world);
    }

    test_assert(red[0] > 0.98f && red[0] < 1.02f);
    test_assert(green[0] > 0.98f && green[0] < 1.02f);
    test_assert(red[1] > 1.15f && red[2] > 1.15f);
    test_assert(green[1] > 1.15f && green[2] > 1.15f);
    test_assert(red_excess[1] > red_excess[0] + 10);
    test_assert(red_excess[2] > red_excess[1] + 2);
    test_assert(green_excess[1] > green_excess[0] + 8);
    test_assert(green_excess[2] > green_excess[1] + 2);
}
