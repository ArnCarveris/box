/**
 * @file ProbeVolume.c
 * @brief Tests for the irradiance probe volume of scenes with LightBake and the views that use it.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void ProbeVolume_probe_count_of_layout(void) {
    // baked_lighting/probe_volume
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, LightBake, {.probe_spacing = 2, .bounds_padding = 1});
    ecs_entity_t box = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, box, Position, {0, 0, 0});
    ecs_set(world, box, Box, {4, 2, 2});
    ecs_progress(world, 0);

    const LightBakeStatus *status = ecs_get(world, scene, LightBakeStatus);
    test_assert(status != NULL);
    test_int(status->probes, 4 * 2 * 2);

    box_render_probe_t probe;
    vec3 corner = {-10, -10, -10};
    test_assert(box_render_bake_probe(world, scene, corner, &probe));
    test_flt(probe.position[0], -3);
    test_flt(probe.position[1], -1);
    test_flt(probe.position[2], -1);

    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0});
    ecs_progress(world, 0);
    test_int(ecs_get(world, scene, LightBakeStatus)->probes, 4 * 2 * 2);
    test_assert(box_render_bake_probe(world, scene, corner, &probe));
    test_flt(probe.position[0], -1.5f);
    test_flt(probe.position[1], -0.5f);
    test_flt(probe.position[2], -0.5f);

    ecs_fini(world);
}

void ProbeVolume_no_static_shapes_no_probes(void) {
    // baked_lighting/probe_volume
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, LightBake, {.probe_spacing = 1, .bounds_padding = 1});
    ecs_entity_t mover = ecs_new(world);
    ecs_add(world, mover, DynamicTransform);
    ecs_set(world, mover, Position, {0, 0, 0});
    ecs_set(world, mover, Box, {2, 2, 2});
    ecs_progress(world, 0);

    test_int(ecs_get(world, ecs_id(LightBake), LightBakeStatus)->probes, 0);
    box_render_probe_t probe;
    test_assert(!box_render_bake_probe(world, 0, (vec3){0, 0, 0}, &probe));

    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, 0, 0});
    ecs_set(world, floor, Box, {2, 2, 2});
    ecs_progress(world, 0);
    test_int(ecs_get(world, ecs_id(LightBake), LightBakeStatus)->probes, 4 * 4 * 4);

    ecs_set(world, mover, Position, {50, 0, 0});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(ecs_get(world, ecs_id(LightBake), LightBakeStatus)->probes, 4 * 4 * 4);

    ecs_fini(world);
}

void ProbeVolume_invalid_inside_box(void) {
    // baked_lighting/probe_volume
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 1});
    ecs_entity_t box = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, box, Position, {0, 0, 0});
    ecs_set(world, box, Box, {3, 3, 3});
    ecs_entity_t hidden = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_add(world, hidden, NoBake);
    ecs_set(world, hidden, Position, {5, 0, 0});
    ecs_set(world, hidden, Box, {3, 3, 3});
    ecs_entity_t mover = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_add(world, mover, DynamicTransform);
    ecs_set(world, mover, Position, {0, 2, 0});
    ecs_set(world, mover, Box, {3, 3, 3});
    ecs_progress(world, 0);

    test_int(ecs_get(world, scene, LightBakeStatus)->probes, 11 * 6 * 6);

    box_render_probe_t probe;
    test_assert(box_render_bake_probe(world, scene, (vec3){0.5f, 0.5f, -0.5f}, &probe));
    test_flt(probe.position[0], 0.5f);
    test_flt(probe.position[1], 0.5f);
    test_flt(probe.position[2], -0.5f);
    test_bool(probe.valid, false);

    test_assert(box_render_bake_probe(world, scene, (vec3){-1.5f, -1.5f, 1.5f}, &probe));
    test_bool(probe.valid, true);

    test_assert(box_render_bake_probe(world, scene, (vec3){-2.5f, 0.5f, 0.5f}, &probe));
    test_bool(probe.valid, true);

    test_assert(box_render_bake_probe(world, scene, (vec3){5.5f, 0.5f, 0.5f}, &probe));
    test_flt(probe.position[0], 5.5f);
    test_bool(probe.valid, true);

    test_assert(box_render_bake_probe(world, scene, (vec3){0.5f, 2.5f, 0.5f}, &probe));
    test_flt(probe.position[1], 2.5f);
    test_bool(probe.valid, true);

    ecs_fini(world);
}

void ProbeVolume_invalid_inside_rotated_box(void) {
    // baked_lighting/probe_volume
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, LightBake, {.probe_spacing = 1, .bounds_padding = 0});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, 0});
    ecs_set(world, wall, Rotation, {0, (float)GLM_PI_4, 0});
    ecs_set(world, wall, Box, {8, 2, 0.5f});
    ecs_progress(world, 0);

    const WorldTransform *t = ecs_get(world, wall, WorldTransform);
    test_assert(t != NULL);
    float along = t->m[0][0] * t->m[0][2] < 0 ? -0.5f : 0.5f;
    box_render_probe_t probe;
    test_assert(box_render_bake_probe(world, 0, (vec3){0.5f, 0.5f, along}, &probe));
    test_flt(probe.position[0], 0.5f);
    test_flt(probe.position[1], 0.5f);
    test_flt(probe.position[2], along);
    test_bool(probe.valid, false);

    test_assert(box_render_bake_probe(world, 0, (vec3){0.5f, 0.5f, -along}, &probe));
    test_flt(probe.position[0], 0.5f);
    test_flt(probe.position[2], -along);
    test_bool(probe.valid, true);

    ecs_fini(world);
}

void ProbeVolume_valid_outside_sphere_inside_bounds(void) {
    // baked_lighting/probe_volume
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, LightBake, {.probe_spacing = 1, .bounds_padding = 0});
    ecs_entity_t ball = ecs_new(world);
    ecs_set(world, ball, Position, {0, 0, 0});
    ecs_set(world, ball, Sphere, {.segments = 24, .radius = 2});
    ecs_progress(world, 0);

    test_int(ecs_get(world, ecs_id(LightBake), LightBakeStatus)->probes, 4 * 4 * 4);

    box_render_probe_t probe;
    test_assert(box_render_bake_probe(world, 0, (vec3){0.5f, 0.5f, 0.5f}, &probe));
    test_flt(probe.position[0], 0.5f);
    test_flt(probe.position[1], 0.5f);
    test_flt(probe.position[2], 0.5f);
    test_bool(probe.valid, false);

    test_assert(box_render_bake_probe(world, 0, (vec3){1.5f, -0.5f, 0.5f}, &probe));
    test_flt(probe.position[0], 1.5f);
    test_flt(probe.position[1], -0.5f);
    test_bool(probe.valid, false);

    test_assert(box_render_bake_probe(world, 0, (vec3){1.5f, 1.5f, -1.5f}, &probe));
    test_flt(probe.position[0], 1.5f);
    test_flt(probe.position[1], 1.5f);
    test_flt(probe.position[2], -1.5f);
    test_bool(probe.valid, true);

    test_assert(box_render_bake_probe(world, 0, (vec3){-1.5f, 1.5f, 0.5f}, &probe));
    test_flt(probe.position[0], -1.5f);
    test_flt(probe.position[2], 0.5f);
    test_bool(probe.valid, true);

    ecs_fini(world);
}

void ProbeVolume_valid_inside_torus_hole(void) {
    // baked_lighting/probe_volume
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, LightBake, {.probe_spacing = 0.5f, .bounds_padding = 0});
    ecs_entity_t ring = ecs_new(world);
    ecs_set(world, ring, Position, {0, 0, 0});
    ecs_set(world, ring, Torus, {.segments = 24, .rings = 12, .radius = 2, .tube = 0.5f});
    ecs_progress(world, 0);

    test_int(ecs_get(world, ecs_id(LightBake), LightBakeStatus)->probes, 10 * 2 * 10);

    box_render_probe_t probe;
    test_assert(box_render_bake_probe(world, 0, (vec3){0.25f, 0.25f, -0.25f}, &probe));
    test_flt(probe.position[0], 0.25f);
    test_flt(probe.position[1], 0.25f);
    test_flt(probe.position[2], -0.25f);
    test_bool(probe.valid, true);

    test_assert(box_render_bake_probe(world, 0, (vec3){2.25f, -0.25f, 0.25f}, &probe));
    test_flt(probe.position[0], 2.25f);
    test_flt(probe.position[1], -0.25f);
    test_flt(probe.position[2], 0.25f);
    test_bool(probe.valid, false);

    test_assert(box_render_bake_probe(world, 0, (vec3){2.25f, 0.25f, 2.25f}, &probe));
    test_flt(probe.position[0], 2.25f);
    test_flt(probe.position[2], 2.25f);
    test_bool(probe.valid, true);

    ecs_fini(world);
}

void ProbeVolume_grid_follows_static_bounds(void) {
    // baked_lighting/probe_volume
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0});
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Box, {2, 2, 2});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, b, Position, {0, 0, 0});
    ecs_set(world, b, Box, {2, 2, 2});
    ecs_progress(world, 0);
    test_int(ecs_get(world, scene, LightBakeStatus)->probes, 2 * 2 * 2);

    ecs_set(world, b, Position, {4, 0, 0});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(ecs_get(world, scene, LightBakeStatus)->probes, 6 * 2 * 2);

    box_render_probe_t probe;
    test_assert(box_render_bake_probe(world, scene, (vec3){10, 0, 0}, &probe));
    test_flt(probe.position[0], 4.5f);

    ecs_set(world, b, Position, {0, 0, 0});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(ecs_get(world, scene, LightBakeStatus)->probes, 2 * 2 * 2);

    ecs_delete(world, a);
    ecs_delete(world, b);
    ecs_progress(world, 0);
    test_int(ecs_get(world, scene, LightBakeStatus)->probes, 0);

    ecs_fini(world);
}

void ProbeVolume_unbaked_view_renders_identically(void) {
    // baked_lighting/probe_volume
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
            ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 1});
        }

        ecs_set(world, scene, AmbientLight, {.sky = {120, 160, 255}, .ground = {200, 90, 40},
            .intensity = 2});
        ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, camera, Position, {0, 1, 6});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
            .color = {10, 10, 20, 255}}, .lighting.enabled = true, .lighting.baked = i == 0});
        ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, floor, Position, {0, -1, 0});
        ecs_set(world, floor, Box, {10, 0.2f, 10});
        ecs_set(world, floor, Rgb, {200, 200, 200});
        ecs_entity_t ball = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, ball, Position, {0, 0.5f, 0});
        ecs_set(world, ball, Rgb, {220, 220, 220});
        ecs_set(world, ball, Sphere, {.segments = 24, .smooth = true, .radius = 1});
        ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, sun, Rotation, {glm_rad(60), 0, 0});
        ecs_set(world, sun, DirectionalLight, {.intensity = 0.2f * GLM_PIf});
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Surface, {.width = 64, .height = 48, .write_to_file = "unbaked.png",
            .resolution_scale = 1});
        ecs_progress(world, 0);
        box_render_sync(world);

        char *path = flecs_asprintf("%s/unbaked.png",
            ecs_singleton_get(world, AppStatus)->tmp_dir);
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

void ProbeVolume_ambient_probes_match_hemisphere(void) {
    // baked_lighting/probe_volume
    unsigned char *images[2] = {NULL, NULL};
    int w[2] = {0, 0}, h[2] = {0, 0};
    int i, lit = 0, largest = 0;

    for (i = 0; i < 2; i ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);

        if (i == 1) {
            ecs_singleton_set(world, LightBake, {.probe_spacing = 0.75f, .bounds_padding = 1});
        }

        ecs_singleton_set(world, AmbientLight, {.sky = {120, 160, 255},
            .ground = {200, 90, 40}, .intensity = 2});
        ecs_entity_t camera = ecs_new(world);
        ecs_set(world, camera, Position, {0, 1, 6});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
            .color = {10, 10, 20, 255}}, .lighting.enabled = true, .lighting.baked = true});
        ecs_entity_t floor = ecs_new(world);
        ecs_set(world, floor, Position, {0, -1, 0});
        ecs_set(world, floor, Box, {10, 0.2f, 10});
        ecs_set(world, floor, Rgb, {200, 200, 200});
        ecs_entity_t ball = ecs_new(world);
        ecs_set(world, ball, Position, {-1, 0.5f, 0});
        ecs_set(world, ball, Rgb, {220, 220, 220});
        ecs_set(world, ball, Sphere, {.segments = 24, .smooth = true, .radius = 1});
        ecs_entity_t metal = ecs_new(world);
        ecs_set(world, metal, Position, {1.5f, 0, 0});
        ecs_set(world, metal, Rotation, {0, glm_rad(30), 0});
        ecs_set(world, metal, Box, {1, 2, 1});
        ecs_set(world, metal, PbrMaterial, {.metallic = 1, .roughness = 0.3f});
        ecs_entity_t mover = ecs_new(world);
        ecs_add(world, mover, DynamicTransform);
        ecs_set(world, mover, Position, {0, 3, -20});
        ecs_set(world, mover, Sphere, {.segments = 16, .radius = 2});
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Surface, {.width = 64, .height = 48, .write_to_file = "ambient.png",
            .resolution_scale = 1});
        ecs_progress(world, 0);
        box_render_sync(world);

        char *path = flecs_asprintf("%s/ambient.png",
            ecs_singleton_get(world, AppStatus)->tmp_dir);
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
        lit += (i % 4) != 3 && images[0][i] > 40;
    }

    test_assert(lit > 100);
    test_assert(largest <= 2);

    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
}
