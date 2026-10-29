/**
 * @file LightBake.c
 * @brief Tests for the baked lighting settings and the status the engine keeps next to them.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void LightBake_script_defaults(void) {
    // baked_lighting/bake_settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "defaults",
        "using box.light\n"
        "using box.view\n"
        "scene {\n Scene: {}\n LightBake: {}\n}\n"
        "custom {\n Scene: {}\n LightBake: {probe_spacing: 4, bounces: 3, capture_size: 32, "
        "budget_ms: 5, bounds_padding: 2, cache: \"other\"}\n}\n", NULL) == 0);

    const LightBake *bake = ecs_get(world, ecs_lookup(world, "scene"), LightBake);
    test_assert(bake != NULL);
    test_flt(bake->probe_spacing, 2);
    test_int(bake->bounces, 2);
    test_int(bake->capture_size, 16);
    test_flt(bake->budget_ms, 2);
    test_flt(bake->bounds_padding, 1);
    test_str(bake->cache, ".box/bake");

    bake = ecs_get(world, ecs_lookup(world, "custom"), LightBake);
    test_assert(bake != NULL);
    test_flt(bake->probe_spacing, 4);
    test_int(bake->bounces, 3);
    test_int(bake->capture_size, 32);
    test_flt(bake->budget_ms, 5);
    test_flt(bake->bounds_padding, 2);
    test_str(bake->cache, "other");

    ecs_fini(world);
}

void LightBake_c_defaults(void) {
    // baked_lighting/bake_settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, LightBake);
    const LightBake *bake = ecs_get(world, scene, LightBake);
    test_assert(bake != NULL);
    test_flt(bake->probe_spacing, 2);
    test_int(bake->bounces, 2);
    test_int(bake->capture_size, 16);
    test_flt(bake->budget_ms, 2);
    test_flt(bake->bounds_padding, 1);
    test_str(bake->cache, ".box/bake");

    ecs_fini(world);
}

void LightBake_zero_is_literal(void) {
    // baked_lighting/bake_settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "zero",
        "using box.light\n"
        "using box.view\n"
        "scene {\n Scene: {}\n LightBake: {bounces: 0, budget_ms: 0, probe_spacing: 0, "
        "capture_size: 0, bounds_padding: 0, cache: \"\"}\n}\n", NULL) == 0);

    const LightBake *bake = ecs_get(world, ecs_lookup(world, "scene"), LightBake);
    test_assert(bake != NULL);
    test_int(bake->bounces, 0);
    test_flt(bake->budget_ms, 0);
    test_flt(bake->probe_spacing, 0);
    test_int(bake->capture_size, 0);
    test_flt(bake->bounds_padding, 0);
    test_assert(bake->cache == NULL || bake->cache[0] == '\0');

    ecs_entity_t literal = ecs_new(world);
    ecs_set(world, literal, LightBake, {0});
    bake = ecs_get(world, literal, LightBake);
    test_int(bake->bounces, 0);
    test_flt(bake->budget_ms, 0);
    test_flt(bake->probe_spacing, 0);

    ecs_fini(world);
}

void LightBake_status_follows_bake(void) {
    // baked_lighting/bake_settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    test_assert(!ecs_has(world, scene, LightBakeStatus));

    ecs_add(world, scene, LightBake);
    test_assert(ecs_has(world, scene, LightBakeStatus));
    const LightBakeStatus *status = ecs_get(world, scene, LightBakeStatus);
    test_int(status->probes, 0);
    test_int(status->baked, 0);
    test_int(status->pass, 0);
    test_bool(status->converged, false);
    test_bool(status->cache_hit, false);

    ecs_remove(world, scene, LightBake);
    test_assert(!ecs_has(world, scene, LightBakeStatus));

    ecs_set(world, scene, LightBake, {.bounces = 1});
    test_assert(ecs_has(world, scene, LightBakeStatus));

    test_assert(ecs_script_run(world, "status",
        "using box.light\n"
        "using box.view\n"
        "other {\n Scene: {}\n LightBake: {}\n}\n", NULL) == 0);
    test_assert(ecs_has(world, ecs_lookup(world, "other"), LightBakeStatus));

    ecs_delete(world, scene);
    ecs_fini(world);
}

void LightBake_singleton_bake_has_status(void) {
    // baked_lighting/bake_settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, LightBake, {.probe_spacing = 3, .bounces = 1});
    test_assert(ecs_has(world, ecs_id(LightBake), LightBakeStatus));
    test_flt(ecs_singleton_get(world, LightBake)->probe_spacing, 3);

    ecs_singleton_remove(world, LightBake);
    test_assert(!ecs_has(world, ecs_id(LightBake), LightBakeStatus));

    ecs_fini(world);
}

void LightBake_no_bake_tag(void) {
    // baked_lighting/bake_settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t shape = ecs_new(world);
    ecs_add(world, shape, NoBake);
    test_assert(ecs_has(world, shape, NoBake));

    test_assert(ecs_script_run(world, "tag",
        "using box.light\n"
        "wall { NoBake }\n", NULL) == 0);
    test_assert(ecs_has(world, ecs_lookup(world, "wall"), NoBake));

    ecs_fini(world);
}

void LightBake_cache_is_owned(void) {
    // baked_lighting/bake_settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *path = ecs_os_strdup("cache/dir");
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, LightBake, {.cache = path});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, LightBake, {.cache = ecs_os_strdup("second")});
    ecs_set(world, b, LightBake, {.cache = ecs_os_strdup("third")});

    test_str(ecs_get(world, b, LightBake)->cache, "third");
    test_str(ecs_get(world, a, LightBake)->cache, "cache/dir");

    ecs_entity_t c = ecs_clone(world, 0, a, true);
    ecs_delete(world, a);
    test_str(ecs_get(world, c, LightBake)->cache, "cache/dir");

    ecs_fini(world);
}

void LightBake_view_baked_defaults(void) {
    // baked_lighting/bake_settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "view",
        "using box.view\n"
        "view { View: {} }\n"
        "off { View: {lighting: {enabled: true, baked: false}} }\n", NULL) == 0);

    const View *view = ecs_get(world, ecs_lookup(world, "view"), View);
    test_assert(view != NULL);
    test_bool(view->lighting.enabled, true);
    test_bool(view->lighting.baked, true);
    view = ecs_get(world, ecs_lookup(world, "off"), View);
    test_bool(view->lighting.enabled, true);
    test_bool(view->lighting.baked, false);

    ecs_entity_t literal = ecs_new(world);
    ecs_set(world, literal, View, {.lighting.enabled = true});
    test_bool(ecs_get(world, literal, View)->lighting.baked, false);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, View);
    test_bool(ecs_get(world, added, View)->lighting.baked, true);

    ecs_fini(world);
}

void LightBake_scene_renders_identically(void) {
    // baked_lighting/bake_settings
    unsigned char *images[2] = {NULL, NULL};
    int w[2] = {0, 0}, h[2] = {0, 0};
    int i;

    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);

        ecs_entity_t scene = ecs_new(world);
        ecs_add(world, scene, Scene);
        if (i == 1) {
            ecs_set(world, scene, LightBake, {.bounces = 1});
        }

        ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, camera, Position, {0, 1, 6});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
            .color = {10, 10, 20, 255}}, .lighting.enabled = true, .lighting.baked = true});
        ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, floor, Position, {0, -1, 0});
        ecs_set(world, floor, Box, {10, 0.2f, 10});
        ecs_set(world, floor, Rgb, {200, 200, 200});
        ecs_entity_t ball = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, ball, Position, {0, 0.5f, 0});
        ecs_set(world, ball, Rgb, {220, 60, 40});
        ecs_set(world, ball, Sphere, {.segments = 24, .smooth = true, .radius = 1});
        ecs_entity_t light = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, light, Position, {2, 3, 2});
        ecs_set(world, light, PointLight, {.intensity = 4 * GLM_PIf, .range = 12});
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Surface, {.width = 64, .height = 48, .write_to_file = "bake.png",
            .resolution_scale = 1});
        ecs_progress(world, 0);
        box_render_sync(world);

        char *path = flecs_asprintf("%s/bake.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
        int n = 0;
        images[i] = stbi_load(path, &w[i], &h[i], &n, 4);
        test_assert(images[i] != NULL);
        ecs_os_free(path);
        ecs_fini(world);
    }

    test_int(w[0], w[1]);
    test_int(h[0], h[1]);
    test_int(memcmp(images[0], images[1], (size_t)w[0] * (size_t)h[0] * 4), 0);

    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
}

void LightBake_projected_anchor_not_baked(void) {
    // core_fixes/projection_position
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

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

    ecs_entity_t anchor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, anchor, ProjectedAnchor, {
        .target = floor, .view = view, .offset = {0, 1.1f, 0}});
    ecs_set(world, anchor, Box, {0.5f, 0.5f, 0.5f});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, anchor);
    ecs_set(world, child, Position, {0.5f, 0, 0});
    ecs_set(world, child, Box, {0.3f, 0.3f, 0.3f});
    for (frame = 0; frame < 5; frame ++) {
        ecs_set(world, camera, Position, {0.01f * (float)frame, 1, 5});
        ecs_progress(world, 0);
        status = ecs_get(world, scene, LightBakeStatus);
        test_assert(status->converged);
        test_assert(fabsf(ecs_get(world, anchor, WorldTransform)->m[3][0]) < 1.0f);
    }

    ecs_entity_t plain = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, plain, Box, {0.5f, 0.5f, 0.5f});
    ecs_progress(world, 0);
    status = ecs_get(world, scene, LightBakeStatus);
    test_bool(status->converged, false);

    ecs_fini(world);
}
