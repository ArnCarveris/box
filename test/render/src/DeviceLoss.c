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

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Errors logged since the counter was installed */
static int device_loss_errors = 0;

/** Log function the counter passes messages on to */
static ecs_os_api_log_t device_loss_log = NULL;

/** Count logged errors and pass every message on.
 *
 * @param level Level of the message
 * @param file File that logged it
 * @param line Line that logged it
 * @param msg The message
 */
static void device_loss_count_errors(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    if (level == -3) {
        device_loss_errors ++;
    }

    if (device_loss_log) {
        device_loss_log(level, file, line, msg);
    }
}

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
    ecs_singleton_set(world, RenderSettings, {.in_flight = 3});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/stripes.png", tmp);
    uint8_t pixels[4 * 4 * 4];
    int i;
    for (i = 0; i < 16; i ++) {
        pixels[i * 4 + 0] = (uint8_t)(i % 2 ? 230 : 20);
        pixels[i * 4 + 1] = 60;
        pixels[i * 4 + 2] = (uint8_t)(i % 2 ? 20 : 230);
        pixels[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 4, 4, 4, pixels, 4 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting.enabled = true});
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Position, {-2, 0, 0});
    ecs_set(world, quad, Quad, {3, 3});
    ecs_set(world, quad, Rgb, {255, 255, 255});
    ecs_set(world, quad, PbrMaterial, {.roughness = 1});
    ecs_set(world, quad, PbrTextures, {.albedo = tex});
    ecs_entity_t box = ecs_new(world);
    ecs_set(world, box, Position, {2, 0, 0});
    ecs_set(world, box, Box, {2, 2, 2});
    ecs_set(world, box, Rgb, {200, 200, 200});
    ecs_set(world, box, PbrMaterial, {.roughness = 1});
    ecs_entity_t light = ecs_new(world);
    ecs_set(world, light, Position, {2, 0, 2});
    ecs_set(world, light, PointLight, {.intensity = 4, .range = 4});
    ecs_entity_t label = ecs_new(world);
    ecs_set(world, label, Position, {0, 2, 0});
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
    int w = 0, h = 0, n = 0, lit = 0, wrong = 0;
    unsigned char *before = stbi_load(path, &w, &h, &n, 4);
    test_assert(before != NULL);
    for (i = 0; i < w * h; i ++) {
        lit += before[i * 4] > 8 || before[i * 4 + 1] > 8 || before[i * 4 + 2] > 8;
    }

    test_assert(lit > 64 * 64 / 8);
    unsigned char *texel = &before[(32 * 64 + 14) * 4];
    test_assert(texel[0] > 40 || texel[2] > 40);

    remove(path);
    device_loss_errors = 0;
    device_loss_log = ecs_os_api.log_;
    ecs_os_api.log_ = device_loss_count_errors;
    box_render_gpu_lose_device(world);
    ecs_entity_t extra = 0;
    for (i = 0; i < 4; i ++) {
        if (i == 0) {
            extra = ecs_new(world);
            ecs_set(world, extra, Position, {0, -1, 1});
            ecs_set(world, extra, Box, {1, 1, 1});
            ecs_set(world, extra, Rgb, {0, 255, 0});
        } else if (i == 2) {
            ecs_delete(world, extra);
        }

        ecs_progress(world, 0);
    }

    box_render_sync(world);
    ecs_os_api.log_ = device_loss_log;
    test_int(device_loss_errors, 0);
    test_int(ecs_singleton_get(world, RenderStatus)->device_losses, 1);

    unsigned char *after = stbi_load(path, &w, &h, &n, 4);
    test_assert(after != NULL);
    for (i = 0; i < w * h * 4; i ++) {
        wrong += abs((int)after[i] - (int)before[i]) > 2;
    }

    test_int(wrong, 0);

    stbi_image_free(after);
    stbi_image_free(before);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}
