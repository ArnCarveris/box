/**
 * @file LightModes.c
 * @brief Tests for the bake modes of lights: Mixed, Realtime and Baked.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void LightModes_defaults_to_mixed(void) {
    // baked_lighting/light_modes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "modes",
        "using box.light\n"
        "sun { DirectionalLight: {} }\n"
        "lamp { PointLight: {} }\n"
        "spot { SpotLight: {} }\n"
        "realtime { PointLight: {bake: Realtime} }\n"
        "baked { SpotLight: {bake: Baked} }\n"
        "mixed { DirectionalLight: {bake: Mixed} }\n", NULL) == 0);

    test_int(ecs_get(world, ecs_lookup(world, "sun"), DirectionalLight)->bake,
        LightBakeModeMixed);
    test_int(ecs_get(world, ecs_lookup(world, "lamp"), PointLight)->bake, LightBakeModeMixed);
    test_int(ecs_get(world, ecs_lookup(world, "spot"), SpotLight)->bake, LightBakeModeMixed);
    test_int(ecs_get(world, ecs_lookup(world, "realtime"), PointLight)->bake,
        LightBakeModeRealtime);
    test_int(ecs_get(world, ecs_lookup(world, "baked"), SpotLight)->bake, LightBakeModeBaked);
    test_int(ecs_get(world, ecs_lookup(world, "mixed"), DirectionalLight)->bake,
        LightBakeModeMixed);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, PointLight, {.intensity = 2, .range = 5});
    test_int(ecs_get(world, e, PointLight)->bake, LightBakeModeMixed);
    ecs_add(world, e, SpotLight);
    test_int(ecs_get(world, e, SpotLight)->bake, LightBakeModeMixed);

    ecs_fini(world);
}

void LightModes_realtime_light_left_out_of_bake(void) {
    // baked_lighting/light_modes
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
        if (i) {
            ecs_set(world, scene, LightBake, {.probe_spacing = 1.4f, .bounds_padding = -0.4f,
                .capture_size = 16, .budget_ms = 2, .bounces = 0});
        }

        ecs_set(world, scene, AmbientLight, {.intensity = 0});
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
        ecs_set(world, lamp, Position, {1.5f, 2.5f, -1.5f});
        ecs_set(world, lamp, PointLight, {.intensity = 3, .range = 6,
            .bake = i == 2 ? LightBakeModeMixed : LightBakeModeRealtime});

        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Surface, {.width = 64, .height = 48, .write_to_file = "realtime.png",
            .resolution_scale = 1});
        int32_t frames = 0;
        while (i && !ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
            ecs_progress(world, 0);
            box_render_sync(world);
            frames ++;
        }

        test_bool(!i || ecs_get(world, scene, LightBakeStatus)->converged, true);
        ecs_progress(world, 0);
        box_render_sync(world);

        char *path = flecs_asprintf("%s/realtime.png",
            ecs_singleton_get(world, AppStatus)->tmp_dir);
        int n = 0;
        images[i] = stbi_load(path, &w[i], &h[i], &n, 4);
        test_assert(images[i] != NULL);
        ecs_os_free(path);

        if (i == 1) {
            const LightBakeStatus *status = ecs_get(world, scene, LightBakeStatus);
            int32_t baked = status->baked;
            ecs_set(world, lamp, PointLight, {.intensity = 6, .range = 4,
                .bake = LightBakeModeRealtime});
            ecs_set(world, lamp, Position, {-1.5f, 2.5f, -1.5f});
            ecs_progress(world, 0);
            box_render_sync(world);
            status = ecs_get(world, scene, LightBakeStatus);
            test_bool(status->converged, true);
            test_int(status->baked, baked);
        }

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
        int d = abs((int)images[0][j] - (int)images[1][j]);
        largest = d > largest ? d : largest;
    }

    test_assert(largest <= 1);
    test_assert(sums[0] > w[0] * h[0] * 10);
    test_assert(sums[2] > sums[1] + w[0] * h[0] * 10);

    for (i = 0; i < 3; i ++) {
        stbi_image_free(images[i]);
    }
}

void LightModes_baked_light_only_in_probes(void) {
    // baked_lighting/light_modes
    unsigned char *images[4] = {NULL, NULL, NULL, NULL};
    int w[4] = {0, 0, 0, 0}, h[4] = {0, 0, 0, 0};
    const float walls[6][6] = {
        {0, -0.1f, 0, 6.4f, 0.2f, 6.4f}, {0, 3.1f, 0, 6.4f, 0.2f, 6.4f},
        {-3.1f, 1.5f, 0, 0.2f, 3, 6.4f}, {3.1f, 1.5f, 0, 0.2f, 3, 6.4f},
        {0, 1.5f, -3.1f, 6.4f, 3, 0.2f}, {0, 1.5f, 3.1f, 6.4f, 3, 0.2f}
    };
    int i, j, largest = 0;
    long sums[4] = {0, 0, 0, 0};

    for (i = 0; i < 4; i ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);

        ecs_entity_t scene = ecs_new(world);
        ecs_add(world, scene, Scene);
        ecs_set(world, scene, LightBake, {.probe_spacing = 1.4f, .bounds_padding = -0.4f,
            .capture_size = 16, .budget_ms = 2, .bounces = 0});
        ecs_set(world, scene, AmbientLight, {.intensity = 0});
        ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, camera, Position, {0, 1.5f, 2.4f});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
            .color = {0, 0, 0, 255}}, .lighting.enabled = true, .lighting.baked = i >= 2});
        for (j = 0; j < 6; j ++) {
            ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, scene);
            ecs_set(world, wall, Position, {walls[j][0], walls[j][1], walls[j][2]});
            ecs_set(world, wall, Box, {walls[j][3], walls[j][4], walls[j][5]});
            ecs_set(world, wall, Rgb, {220, 220, 220});
        }

        ecs_entity_t dim = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, dim, Position, {-2, 1.5f, 0});
        ecs_set(world, dim, PointLight, {.intensity = 1, .range = 3});
        if (!(i % 2)) {
            ecs_entity_t lamp = ecs_new_w_pair(world, EcsChildOf, scene);
            ecs_set(world, lamp, Position, {1.5f, 2.5f, -1.5f});
            ecs_set(world, lamp, PointLight, {.intensity = 4, .range = 6,
                .bake = LightBakeModeBaked});
        }

        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Surface, {.width = 64, .height = 48, .write_to_file = "baked.png",
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

        char *path = flecs_asprintf("%s/baked.png",
            ecs_singleton_get(world, AppStatus)->tmp_dir);
        int n = 0;
        images[i] = stbi_load(path, &w[i], &h[i], &n, 4);
        test_assert(images[i] != NULL);
        ecs_os_free(path);
        ecs_fini(world);
    }

    for (i = 0; i < 4; i ++) {
        for (j = 0; j < w[i] * h[i] * 4; j ++) {
            sums[i] += (j % 4) != 3 ? images[i][j] : 0;
        }
    }

    test_int(w[0], w[1]);
    test_int(h[0], h[1]);
    for (j = 0; j < w[0] * h[0] * 4; j ++) {
        int d = abs((int)images[0][j] - (int)images[1][j]);
        largest = d > largest ? d : largest;
    }

    test_assert(largest <= 1);
    test_assert(sums[1] > w[0] * h[0] * 2);
    test_assert(sums[2] > sums[3] + w[0] * h[0] * 10);

    for (i = 0; i < 4; i ++) {
        stbi_image_free(images[i]);
    }
}

void LightModes_modes_identical_without_bake(void) {
    // baked_lighting/light_modes
    unsigned char *images[3] = {NULL, NULL, NULL};
    int w[3] = {0, 0, 0}, h[3] = {0, 0, 0};
    int i, j, largest = 0;
    long sum = 0;

    for (i = 0; i < 3; i ++) {
        LightBakeMode mode = (LightBakeMode)i;
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);

        ecs_entity_t camera = ecs_new(world);
        ecs_set(world, camera, Position, {0, 10, 0});
        ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
            .color = {0, 0, 0, 255}}, .lighting.enabled = true, .lighting.baked = true,
            .shadows = {.enabled = true, .depth_bias = 2, .normal_offset = 1.5f, .fade = 0.2f,
                .cascades = 4, .resolution = 2048, .max_distance = 200, .filter_size = 3}});
        ecs_singleton_set(world, AmbientLight, {.intensity = 0});
        ecs_entity_t floor = ecs_new(world);
        ecs_set(world, floor, Position, {0, -0.1f, 0});
        ecs_set(world, floor, Box, {20, 0.2f, 20});
        ecs_set(world, floor, Rgb, {255, 255, 255});
        ecs_set(world, floor, PbrMaterial, {.roughness = 1});
        ecs_entity_t caster = ecs_new(world);
        ecs_set(world, caster, Position, {-3, 5, 0});
        ecs_set(world, caster, Box, {1.5f, 0.2f, 1.5f});
        ecs_entity_t sun = ecs_new(world);
        ecs_set(world, sun, Rotation, {(float)GLM_PI_2, 0, 0});
        ecs_set(world, sun, DirectionalLight, {.intensity = 0.5f, .bake = mode});
        ecs_entity_t lamp = ecs_new(world);
        ecs_set(world, lamp, Position, {3, 1, 2});
        ecs_set(world, lamp, PointLight, {.intensity = 2, .range = 5, .bake = mode});
        ecs_entity_t spot = ecs_new(world);
        ecs_set(world, spot, Position, {3, 3, -3});
        ecs_set(world, spot, Rotation, {(float)GLM_PI_2, 0, 0});
        ecs_set(world, spot, SpotLight, {.intensity = 3, .range = 6, .inner_angle = 20,
            .outer_angle = 30, .bake = mode});
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "modes.png",
            .resolution_scale = 1});
        ecs_progress(world, 0);
        box_render_sync(world);

        char *path = flecs_asprintf("%s/modes.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
        int n = 0;
        images[i] = stbi_load(path, &w[i], &h[i], &n, 4);
        test_assert(images[i] != NULL);
        ecs_os_free(path);
        ecs_fini(world);
    }

    for (i = 1; i < 3; i ++) {
        test_int(w[i], w[0]);
        test_int(h[i], h[0]);
        for (j = 0; j < w[0] * h[0] * 4; j ++) {
            int d = abs((int)images[0][j] - (int)images[i][j]);
            largest = d > largest ? d : largest;
        }
    }

    for (j = 0; j < w[0] * h[0] * 4; j ++) {
        sum += (j % 4) != 3 ? images[0][j] : 0;
    }

    test_int(largest, 0);
    test_assert(sum > w[0] * h[0] * 30);
    test_assert(images[0][(32 * 64 + 15) * 4] < 30);
    test_assert(images[0][(32 * 64 + 48) * 4] > 60);

    for (i = 0; i < 3; i ++) {
        stbi_image_free(images[i]);
    }
}

void LightModes_mode_change_rebakes_reach(void) {
    // baked_lighting/light_modes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 1,
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

    LightBakeMode modes[3] = {LightBakeModeRealtime, LightBakeModeBaked, LightBakeModeMixed};
    int32_t m;
    for (m = 0; m < 3; m ++) {
        int32_t frames = 0;
        while (!ecs_get(world, scene, LightBakeStatus)->converged && frames < 5000) {
            ecs_progress(world, 0);
            box_render_sync(world);
            frames ++;
        }

        const LightBakeStatus *status = ecs_get(world, scene, LightBakeStatus);
        test_bool(status->converged, true);
        int32_t probes = status->probes, baked = status->baked;

        ecs_set(world, lamp, PointLight, {.intensity = 2, .range = 3, .bake = modes[m]});
        ecs_progress(world, 0);
        box_render_sync(world);
        status = ecs_get(world, scene, LightBakeStatus);
        if (modes[m] == LightBakeModeMixed) {
            test_bool(status->converged, true);
            test_int(status->baked, baked);
        } else {
            test_bool(status->converged, false);
            test_assert(status->baked < baked);
            test_assert((baked - status->baked) * 8 < probes);
        }
    }

    ecs_fini(world);
}

void LightModes_baked_sun_leaves_shadows_to_next(void) {
    // baked_lighting/light_modes
    unsigned char *images[2] = {NULL, NULL};
    int w[2] = {0, 0}, h[2] = {0, 0};
    int i, j, largest = 0;

    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);

        ecs_singleton_set(world, LightBake, {.probe_spacing = 10, .bounds_padding = 0,
            .capture_size = 8, .budget_ms = 0, .bounces = 0});
        ecs_entity_t camera = ecs_new(world);
        ecs_set(world, camera, Position, {0, 10, 0});
        ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
            .color = {0, 0, 0, 255}}, .lighting.enabled = true, .lighting.baked = false,
            .shadows = {.enabled = true, .depth_bias = 2, .normal_offset = 1.5f, .fade = 0.2f,
                .cascades = 4, .resolution = 2048, .max_distance = 200, .filter_size = 3}});
        ecs_singleton_set(world, AmbientLight, {.intensity = 0});
        ecs_entity_t floor = ecs_new(world);
        ecs_set(world, floor, Position, {0, -0.1f, 0});
        ecs_set(world, floor, Box, {20, 0.2f, 20});
        ecs_set(world, floor, Rgb, {255, 255, 255});
        ecs_set(world, floor, PbrMaterial, {.roughness = 1});
        ecs_entity_t caster = ecs_new(world);
        ecs_set(world, caster, Position, {-3, 5, 0});
        ecs_set(world, caster, Box, {1.5f, 0.2f, 1.5f});
        if (!i) {
            ecs_entity_t baked = ecs_new(world);
            ecs_set(world, baked, Rotation, {(float)GLM_PI_2, 0, 0.7f});
            ecs_set(world, baked, DirectionalLight, {.intensity = 2,
                .bake = LightBakeModeBaked});
        }

        ecs_entity_t sun = ecs_new(world);
        ecs_set(world, sun, Rotation, {(float)GLM_PI_2, 0, 0});
        ecs_set(world, sun, DirectionalLight, {.intensity = 1});
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "suns.png",
            .resolution_scale = 1});
        ecs_progress(world, 0);
        box_render_sync(world);

        char *path = flecs_asprintf("%s/suns.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
        int n = 0;
        images[i] = stbi_load(path, &w[i], &h[i], &n, 4);
        test_assert(images[i] != NULL);
        ecs_os_free(path);
        ecs_fini(world);
    }

    test_int(w[0], w[1]);
    test_int(h[0], h[1]);
    for (j = 0; j < w[0] * h[0] * 4; j ++) {
        int d = abs((int)images[0][j] - (int)images[1][j]);
        largest = d > largest ? d : largest;
    }

    test_assert(largest <= 1);
    test_assert(images[0][(32 * 64 + 48) * 4] > 200);
    test_assert(images[0][(32 * 64 + 15) * 4] < 30);

    for (i = 0; i < 2; i ++) {
        stbi_image_free(images[i]);
    }
}
