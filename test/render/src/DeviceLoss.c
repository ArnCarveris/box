/**
 * @file DeviceLoss.c
 * @brief Tests for a pipeline that fails to build and for a GPU device that is lost.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void DeviceLoss_pipeline_error_keeps_previous(void) {
    // render_fixes/device_loss
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, ShaderSettings, {.poll_interval = -1});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}});
    ecs_entity_t cube = ecs_new(world);
    ecs_set(world, cube, Box, {1, 1, 1});
    ecs_set(world, cube, Rgb, {255, 0, 0});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "pipeline.png",
        .resolution_scale = 1});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *png = flecs_asprintf("%s/pipeline.png", tmp);
    char *bad = flecs_asprintf("%s/unbound_mesh.wgsl", tmp);
    FILE *f = fopen(bad, "w");
    fputs("override blend_mode: u32 = 0u;\n"
        "@group(0) @binding(99) var<uniform> extra: vec4f;\n"
        "@vertex fn vs(@location(0) p: vec3f) -> @builtin(position) vec4f {\n"
        "    return vec4f(p, 1.0);\n"
        "}\n"
        "@fragment fn fs() -> @location(0) vec4f {\n"
        "    return extra + vec4f(f32(blend_mode));\n"
        "}\n", f);
    fclose(f);
    ecs_entity_t mesh = ecs_lookup(world, "box.shader.builtin.mesh");
    test_assert(mesh != 0);
    ecs_set(world, mesh, Shader, {.path = bad});
    ecs_log_set_level(-4);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_log_set_level(-1);

    const ShaderStatus *st = ecs_get(world, mesh, ShaderStatus);
    test_assert(st != NULL);
    test_bool(st->ok, false);
    test_assert(st->error != NULL);
    test_assert(strstr(st->error, "unbound_mesh.wgsl: ") != NULL);
    test_assert(strstr(st->error, "99") != NULL);

    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(png, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 32) * 4] > 200);
    test_assert(px[(32 * 64 + 32) * 4 + 1] < 40);
    stbi_image_free(px);

    ecs_os_free(bad);
    ecs_os_free(png);
    ecs_fini(world);
}

void DeviceLoss_lost_device_recovers(void) {
    // render_fixes/device_loss
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting.enabled = true});
    ecs_entity_t box = ecs_new(world);
    ecs_set(world, box, Position, {1, 0, 0});
    ecs_set(world, box, Box, {2, 2, 2});
    ecs_set(world, box, Rgb, {200, 200, 200});
    ecs_set(world, box, PbrMaterial, {.roughness = 1});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {1, 0, 2});
    ecs_set(world, light, PointLight, {.intensity = 4, .range = 4});
    ecs_entity_t label = ecs_new(world);
    ecs_set(world, label, Position, {-2, 1, 0});
    ecs_set(world, label, Text, {.value = "Box", .size = 1,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "lost.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_singleton_get(world, RenderStatus)->device_losses, 0);

    char *path = flecs_asprintf("%s/lost.png", tmp);
    int w = 0, h = 0, n = 0, i, lit = 0, wrong = 0;
    unsigned char *before = stbi_load(path, &w, &h, &n, 4);
    test_assert(before != NULL);
    for (i = 0; i < w * h; i ++) {
        lit += before[i * 4] > 8 || before[i * 4 + 1] > 8 || before[i * 4 + 2] > 8;
    }

    test_assert(lit > 64 * 64 / 8);

    ecs_log_set_level(-4);
    box_render_gpu_lose_device(world);
    for (i = 0; i < 8 && !ecs_singleton_get(world, RenderStatus)->device_losses; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    ecs_log_set_level(-1);
    test_int(ecs_singleton_get(world, RenderStatus)->device_losses, 1);

    remove(path);
    ecs_progress(world, 0);
    box_render_sync(world);
    unsigned char *after = stbi_load(path, &w, &h, &n, 4);
    test_assert(after != NULL);
    for (i = 0; i < w * h * 4; i ++) {
        wrong += abs((int)after[i] - (int)before[i]) > 2;
    }

    test_int(wrong, 0);
    test_int(ecs_singleton_get(world, RenderStatus)->device_losses, 1);

    stbi_image_free(after);
    stbi_image_free(before);
    ecs_os_free(path);
    ecs_fini(world);
}
