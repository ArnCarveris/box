#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "../../../geometry_assets.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

static ecs_world_t *parametric_scene(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 8, 0});
    ecs_set(world, camera, Rotation, {-GLM_PI_2, 0, 0});
    ecs_set(world, camera, Camera, {.near_ = 0.1f, .far_ = 100,
        .ortho = true, .ortho_height = 6});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 64,
        .resolution_scale = 1, .write_to_file = "parametric.png"});
    return world;
}

static unsigned char *parametric_capture(ecs_world_t *world) {
    ecs_progress(world, 0);
    box_render_sync(world);
    char *path = flecs_asprintf("%s/parametric.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *pixels = stbi_load(path, &w, &h, &n, 4);
    test_not_null(pixels);
    test_int(w, 64);
    test_int(h, 64);
    ecs_os_free(path);
    return pixels;
}

static ecs_entity_t parametric_profile(ecs_world_t *world, const vec2 *points,
    int count, const int32_t *holes, int hole_count)
{
    ecs_entity_t asset = test_profile(world, points, count, holes, hole_count);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, ProfileExtrusion, {.profile = asset, .depth = 2,
        .cap_start = true, .cap_end = true});
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, Emissive, {.color = {0, 255, 0}, .strength = 1});
    return e;
}

void Parametric_profile_hole(void) {
    ecs_world_t *world = parametric_scene();
    const vec2 points[8] = {{-2, -2}, {2, -2}, {2, 2}, {-2, 2},
        {-1, -1}, {-1, 1}, {1, 1}, {1, -1}};
    const int32_t holes[1] = {4};
    parametric_profile(world, points, 8, holes, 1);
    unsigned char *pixels = parametric_capture(world);
    test_assert(pixels[(32 * 64 + 32) * 4 + 1] < 10);
    test_assert(pixels[(32 * 64 + 48) * 4 + 1] > 150);
    test_assert(pixels[(32 * 64 + 16) * 4 + 1] > 150);
    test_assert(pixels[(16 * 64 + 32) * 4 + 1] > 150);
    test_assert(pixels[(48 * 64 + 32) * 4 + 1] > 150);
    stbi_image_free(pixels);
    ecs_fini(world);
}

void Parametric_profile_concave(void) {
    ecs_world_t *world = parametric_scene();
    const vec2 points[6] = {{-2, -2}, {2, -2}, {2, 0}, {0, 0}, {0, 2}, {-2, 2}};
    parametric_profile(world, points, 6, NULL, 0);
    unsigned char *pixels = parametric_capture(world);
    test_assert(pixels[(48 * 64 + 48) * 4 + 1] < 10);
    test_assert(pixels[(16 * 64 + 48) * 4 + 1] > 150);
    test_assert(pixels[(48 * 64 + 16) * 4 + 1] > 150);
    stbi_image_free(pixels);
    ecs_fini(world);
}

void Parametric_sweep(void) {
    ecs_world_t *world = parametric_scene();
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    Sweep sweep = test_sweep(world);
    sweep.path = test_route(world, (vec3){-2,0,0}, (vec3){-1,0,0}, (vec3){1,0,0}, (vec3){2,0,0});
    sweep.scale[0] = sweep.scale[1] = 0.5f;
    ecs_set_ptr(world, e, Sweep, &sweep);
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, Emissive, {.color = {0, 255, 0}, .strength = 1});
    unsigned char *pixels = parametric_capture(world);
    test_assert(pixels[(32 * 64 + 32) * 4 + 1] > 150);
    test_assert(pixels[(20 * 64 + 32) * 4 + 1] < 10);
    stbi_image_free(pixels);
    ecs_fini(world);
}

void Parametric_lathe_cavity(void) {
    ecs_world_t *world = parametric_scene();
    Lathe lathe = {.cap_start = true, .cap_end = true};
    vec2 outer[2] = {{2, -1}, {2, 1}}, inner[2] = {{1, -1}, {1, 1}};
    lathe.profile = test_path2(world, outer, 2, false);
    lathe.inner = test_path2(world, inner, 2, false);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set_ptr(world, e, Lathe, &lathe);
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, Emissive, {.color = {0, 255, 0}, .strength = 1});
    unsigned char *pixels = parametric_capture(world);
    test_assert(pixels[(32 * 64 + 32) * 4 + 1] < 10);
    test_assert(pixels[(32 * 64 + 48) * 4 + 1] > 150);
    stbi_image_free(pixels);
    ecs_fini(world);
}

void Parametric_profile_multiple_holes(void) {
    ecs_world_t *world = parametric_scene();
    const vec2 points[12] = {{-2, -2}, {2, -2}, {2, 2}, {-2, 2},
        {-1.5f, -0.5f}, {-1.5f, 0.5f}, {-0.5f, 0.5f}, {-0.5f, -0.5f},
        {0.5f, -0.5f}, {0.5f, 0.5f}, {1.5f, 0.5f}, {1.5f, -0.5f}};
    const int32_t holes[2] = {4, 8};
    parametric_profile(world, points, 12, holes, 2);
    unsigned char *pixels = parametric_capture(world);
    test_assert(pixels[(32 * 64 + 21) * 4 + 1] < 10);
    test_assert(pixels[(32 * 64 + 43) * 4 + 1] < 10);
    test_assert(pixels[(32 * 64 + 32) * 4 + 1] > 150);
    test_assert(pixels[(16 * 64 + 32) * 4 + 1] > 150);
    stbi_image_free(pixels);
    ecs_fini(world);
}

void Parametric_bake_hollow(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set(world, scene, LightBake, {.probe_spacing = 0.5f, .bounds_padding = 0.5f});
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, e, Position, {0, 0, 0});
    Sweep value = test_sweep(world);
    value.path = test_route(world, (vec3){0,-1,0}, (vec3){0,-0.333333f,0},
        (vec3){0,0.333333f,0}, (vec3){0,1,0});
    value.profile = test_circle(world, 0.5f);
    ecs_set_ptr(world, e, Sweep, &value);
    ecs_progress(world, 0);
    box_render_probe_t probe;
    test_assert(box_render_bake_probe(world, scene, (vec3){0.25f, 0.25f, 0.25f}, &probe));
    test_bool(probe.valid, true);
    test_assert(box_render_bake_probe(world, scene, (vec3){0.75f, 0.25f, 0.25f}, &probe));
    test_bool(probe.valid, false);
    Sweep *sweep = ecs_get_mut(world, e, Sweep);
    sweep->profile = test_circle(world, 0.9f);
    ecs_modified(world, e, Sweep);
    ecs_progress(world, 0);
    test_assert(box_render_bake_probe(world, scene, (vec3){0.75f, 0.25f, 0.25f}, &probe));
    test_bool(probe.valid, true);
    ecs_fini(world);
}

void Parametric_profile_many_holes(void) {
    ecs_world_t *world = parametric_scene();
    vec2 points[28] = {{-2,-2},{2,-2},{2,2},{-2,2}};
    int32_t holes[6];
    for (int i = 0; i < 6; i ++) {
        float x = (float)(i % 3 - 1) * 1.25f;
        float z = i < 3 ? -0.8f : 0.8f;
        int first = holes[i] = 4 + i * 4;
        glm_vec2_copy((vec2){x - 0.25f, z - 0.3f}, points[first]);
        glm_vec2_copy((vec2){x + 0.25f, z - 0.3f}, points[first + 1]);
        glm_vec2_copy((vec2){x + 0.25f, z + 0.3f}, points[first + 2]);
        glm_vec2_copy((vec2){x - 0.25f, z + 0.3f}, points[first + 3]);
    }
    parametric_profile(world, points, 28, holes, 6);
    unsigned char *pixels = parametric_capture(world);
    for (int i = 0; i < 6; i ++) {
        float x = (float)(i % 3 - 1) * 1.25f;
        float z = i < 3 ? -0.8f : 0.8f;
        int px = (int)(32 + x * 64 / 6), py = (int)(32 + z * 64 / 6);
        test_assert(pixels[(py * 64 + px) * 4 + 1] < 10);
    }
    test_assert(pixels[(32 * 64 + 32) * 4 + 1] > 150);
    stbi_image_free(pixels);
    ecs_fini(world);
}

void Parametric_loft_transform(void) {
    ecs_world_t *world = parametric_scene();
    ecs_entity_t profile = test_profile(world, (vec2[]){{-1,-1},{1,-1},{1,1},{-1,1}}, 4, NULL, 0);
    Loft loft = {.cap_start = true, .cap_end = true};
    *ecs_vec_append_t(NULL, &loft.sections, LoftSection) =
        (LoftSection){.profile = profile, .scale = {0.3f, 0.3f}};
    *ecs_vec_append_t(NULL, &loft.sections, LoftSection) =
        (LoftSection){.profile = profile, .offset = {1, 1, 0},
            .rotation = {0, GLM_PI_4, 0}, .scale = {0.8f, 0.4f}};
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set_ptr(world, e, Loft, &loft);
    ecs_vec_fini_t(NULL, &loft.sections, LoftSection);
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, Emissive, {.color = {0, 255, 0}, .strength = 1});
    unsigned char *pixels = parametric_capture(world);
    test_assert(pixels[(32 * 64 + 42) * 4 + 1] > 150);
    test_assert(pixels[(32 * 64 + 16) * 4 + 1] < 10);
    stbi_image_free(pixels);
    ecs_fini(world);
}
