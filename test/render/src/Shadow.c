/**
 * @file Shadow.c
 * @brief Tests for cascaded shadow maps: cascade splits, cascade fitting and casters.
 *
 * The capture cases look straight down from 10 units above a white floor lit by a sun that
 * shines straight down, into a 64x64 image: world x = -3 lands on pixel column 15, x = 3 on
 * column 48. A small slab 5 units above x = -3 shadows the floor below it.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Shadow_splits_uniform(void) {
    // lighting/shadows
    float splits[4];
    box_render_shadow_splits(1, 101, 0, 4, splits);
    test_flt(splits[0], 26);
    test_flt(splits[1], 51);
    test_flt(splits[2], 76);
    test_flt(splits[3], 101);
}

void Shadow_splits_logarithmic(void) {
    // lighting/shadows
    float splits[3];
    box_render_shadow_splits(1, 1000, 1, 3, splits);
    test_assert(fabsf(splits[0] - 10) < 1e-3f);
    test_assert(fabsf(splits[1] - 100) < 1e-2f);
    test_flt(splits[2], 1000);
}

void Shadow_splits_mix_by_lambda(void) {
    // lighting/shadows
    float splits[2];
    box_render_shadow_splits(1, 100, 0.5f, 2, splits);
    test_assert(fabsf(splits[0] - (0.5f * 10 + 0.5f * 50.5f)) < 1e-3f);
    test_flt(splits[1], 100);

    box_render_shadow_splits(1, 100, 0.7f, 2, splits);
    test_assert(fabsf(splits[0] - (0.7f * 10 + 0.3f * 50.5f)) < 1e-3f);
}

void Shadow_splits_end_at_max_distance(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.5f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .shadows = {.enabled = true, .cascades = 3, .max_distance = 50, .split_lambda = 0.5f}});
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.views), 1);
    const box_render_view_t *v = ecs_vec_first(&null->last.views);
    float expect[3];
    box_render_shadow_splits(0.5f, 50, 0.5f, 3, expect);
    test_bool(v->shadows.enabled, true);
    test_int(v->shadows.cascades, 3);
    test_flt(v->shadows.near_, 0.5f);
    test_flt(v->shadows.splits[0], expect[0]);
    test_flt(v->shadows.splits[1], expect[1]);
    test_flt(v->shadows.splits[2], 50);
    test_assert(v->shadows.splits[0] > 0.5f);
    test_assert(v->shadows.splits[1] > v->shadows.splits[0]);
    test_assert(v->shadows.splits[2] > v->shadows.splits[1]);

    ecs_fini(world);
    box_render_null_free(null);
}

void Shadow_splits_end_at_camera_far(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .far_ = 30});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .shadows = {.enabled = true, .cascades = 2, .max_distance = 200}});
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.views), 1);
    const box_render_view_t *v = ecs_vec_first(&null->last.views);
    test_bool(v->shadows.enabled, true);
    test_int(v->shadows.cascades, 2);
    test_flt(v->shadows.splits[1], 30);

    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_progress(world, 0);
    box_render_sync(world);
    v = ecs_vec_first(&null->last.views);
    test_bool(v->shadows.enabled, false);

    ecs_fini(world);
    box_render_null_free(null);
}

void Shadow_cascade_contains_slice(void) {
    // lighting/shadows
    Camera c = {.fov = 60};
    mat4 camera, proj;
    float dir[3] = {0.3f, 0.8f, 0.5f};
    glm_vec3_normalize(dir);
    glm_mat4_identity(camera);
    glm_translate(camera, (vec3){5, 2, 3});
    glm_rotate_y(camera, 0.7f, camera);
    glm_rotate_x(camera, -0.3f, camera);
    box_camera_projection(&c, 160, 90, proj);
    box_render_cascade_t cascade;
    box_render_shadow_cascade(camera, proj, 5, 20, dir, 1024, 50, &cascade);
    test_assert(cascade.texel > 0);

    int32_t i;
    for (i = 0; i < 8; i ++) {
        float depth = i < 4 ? 5.0f : 20.0f;
        vec4 local = {
            (i & 1 ? 1.0f : -1.0f) * depth / proj[0][0],
            (i & 2 ? 1.0f : -1.0f) * depth / proj[1][1],
            -depth, 1
        };
        vec4 world, clip;
        glm_mat4_mulv(camera, local, world);
        glm_mat4_mulv(cascade.matrix, world, clip);
        test_assert(clip[0] > -1 && clip[0] < 1);
        test_assert(clip[1] > -1 && clip[1] < 1);
        test_assert(clip[2] > 0 && clip[2] < 1);
    }
}

void Shadow_cascade_snapped_to_texels(void) {
    // lighting/shadows
    Camera c = {.fov = 60};
    mat4 a, b, proj;
    float dir[3] = {-0.4f, 0.7f, 0.2f};
    int32_t res = 2048;
    glm_vec3_normalize(dir);
    box_camera_projection(&c, 160, 90, proj);
    glm_mat4_identity(a);
    glm_translate(a, (vec3){1, 2, 3});
    glm_mat4_identity(b);
    glm_translate(b, (vec3){1.37f, 2.11f, 2.47f});
    glm_rotate_y(b, 0.25f, b);
    box_render_cascade_t ca, cb;
    box_render_shadow_cascade(a, proj, 1, 10, dir, res, 50, &ca);
    box_render_shadow_cascade(b, proj, 1, 10, dir, res, 50, &cb);
    test_flt(ca.texel, cb.texel);

    vec4 p = {2, 0, -3, 1}, pa, pb;
    glm_mat4_mulv(ca.matrix, p, pa);
    glm_mat4_mulv(cb.matrix, p, pb);
    test_assert(pa[0] != pb[0] || pa[1] != pb[1]);
    float dx = (pa[0] - pb[0]) * 0.5f * (float)res;
    float dy = (pa[1] - pb[1]) * 0.5f * (float)res;
    test_assert(fabsf(dx - roundf(dx)) < 0.02f);
    test_assert(fabsf(dy - roundf(dy)) < 0.02f);
}

void Shadow_cascade_reversed_z(void) {
    // lighting/shadows
    Camera c = {.fov = 60};
    mat4 camera, proj;
    float dir[3] = {0, 1, 0};
    glm_mat4_identity(camera);
    box_camera_projection(&c, 100, 100, proj);
    box_render_cascade_t cascade;
    box_render_shadow_cascade(camera, proj, 1, 10, dir, 512, 50, &cascade);

    vec4 low = {0, 0, -5, 1}, high = {0, 2, -5, 1}, cl, ch;
    glm_mat4_mulv(cascade.matrix, low, cl);
    glm_mat4_mulv(cascade.matrix, high, ch);
    test_assert(ch[2] > cl[2]);
    test_assert(cl[2] > 0);
    test_assert(ch[2] < 1);
}

void Shadow_box_casts_shadow(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .shadows = {.enabled = true, .depth_bias = 2, .normal_offset = 1.5f, .fade = 0.2f}});
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
    ecs_set(world, sun, DirectionalLight, {.intensity = 1});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "cast.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/cast.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *under = &px[(32 * 64 + 15) * 4];
    unsigned char *open = &px[(32 * 64 + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(under[0] < 30);
    test_assert(under[1] < 30);
    test_assert(under[2] < 30);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shadow_disabled_casts_no_shadow(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .shadows = {.enabled = false, .depth_bias = 2}});
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
    ecs_set(world, sun, DirectionalLight, {.intensity = 1});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "disabled.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/disabled.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *under = &px[(32 * 64 + 15) * 4];
    unsigned char *open = &px[(32 * 64 + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(abs((int)under[0] - (int)open[0]) < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shadow_no_shadow_casts_none(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .shadows = {.enabled = true, .depth_bias = 2, .normal_offset = 1.5f, .fade = 0.2f}});
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
    ecs_set(world, sun, DirectionalLight, {.intensity = 1});
    ecs_add(world, caster, NoShadow);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "ghost.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/ghost.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *under = &px[(32 * 64 + 15) * 4];
    unsigned char *open = &px[(32 * 64 + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(abs((int)under[0] - (int)open[0]) < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shadow_no_shadow_parent_casts_none(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .shadows = {.enabled = true, .depth_bias = 2, .normal_offset = 1.5f, .fade = 0.2f}});
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
    ecs_set(world, sun, DirectionalLight, {.intensity = 1});
    ecs_entity_t group = ecs_new(world);
    ecs_add(world, group, NoShadow);
    ecs_add_pair(world, caster, EcsChildOf, group);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "group.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/group.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *under = &px[(32 * 64 + 15) * 4];
    unsigned char *open = &px[(32 * 64 + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(abs((int)under[0] - (int)open[0]) < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shadow_no_shadow_removed_casts_again(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .shadows = {.enabled = true, .depth_bias = 2, .normal_offset = 1.5f, .fade = 0.2f}});
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
    ecs_set(world, sun, DirectionalLight, {.intensity = 1});
    ecs_add(world, caster, NoShadow);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "removed.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/removed.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *under = &px[(32 * 64 + 15) * 4];
    unsigned char *open = &px[(32 * 64 + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(abs((int)under[0] - (int)open[0]) < 8);
    stbi_image_free(px);

    ecs_remove(world, caster, NoShadow);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    under = &px[(32 * 64 + 15) * 4];
    open = &px[(32 * 64 + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(under[0] < 30);
    test_assert(under[1] < 30);
    test_assert(under[2] < 30);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shadow_beyond_max_distance_unshadowed(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .shadows = {.enabled = true, .depth_bias = 2, .normal_offset = 1.5f, .fade = 0.2f,
            .max_distance = 5}});
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
    ecs_set(world, sun, DirectionalLight, {.intensity = 1});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "beyond.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/beyond.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *under = &px[(32 * 64 + 15) * 4];
    unsigned char *open = &px[(32 * 64 + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(abs((int)under[0] - (int)open[0]) < 8);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shadow_fade_softens_last_cascade(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Rotation, {-(float)GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .lighting.enabled = true,
        .shadows = {.enabled = true, .cascades = 1, .max_distance = 20, .fade = 1,
            .depth_bias = 2, .normal_offset = 1.5f}});
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
    ecs_set(world, sun, DirectionalLight, {.intensity = 1});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "fade.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/fade.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *under = &px[(32 * 64 + 15) * 4];
    unsigned char *open = &px[(32 * 64 + 48) * 4];
    test_assert(open[0] > 200);
    test_assert(under[0] > 60);
    test_assert(under[0] < (int)open[0] - 30);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shadow_default_settings(void) {
    // lighting/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "defaults",
        "using box.view\n"
        "view { View: {} }\n"
        "shadowed { View: {shadows: {enabled: true}} }\n", NULL) == 0);

    const View *view = ecs_get(world, ecs_lookup(world, "view"), View);
    test_assert(view != NULL);
    test_bool(view->shadows.enabled, false);
    test_int(view->shadows.cascades, 4);
    test_int(view->shadows.resolution, 2048);
    test_flt(view->shadows.max_distance, 200);
    test_flt(view->shadows.split_lambda, 0.7f);
    test_flt(view->shadows.depth_bias, 2);
    test_flt(view->shadows.normal_offset, 1.5f);
    test_int(view->shadows.filter_size, 3);
    test_flt(view->shadows.fade, 0.2f);
    const View *shadowed = ecs_get(world, ecs_lookup(world, "shadowed"), View);
    test_assert(shadowed != NULL);
    test_bool(shadowed->shadows.enabled, true);
    test_int(shadowed->shadows.cascades, 4);
    test_flt(shadowed->shadows.split_lambda, 0.7f);

    ecs_fini(world);
}
