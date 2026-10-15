/**
 * @file Restart.c
 * @brief Tests for a scene that keeps rendering when the backend restarts on another thread.
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

void Restart_main_thread_toggle_keeps_scene(void) {
    // render_fixes/backend_restart
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/checker.png", tmp);
    uint8_t px[4 * 4 * 4];
    int32_t i;
    for (i = 0; i < 16; i ++) {
        px[i * 4 + 0] = (uint8_t)(i % 2 ? 230 : 20);
        px[i * 4 + 1] = 60;
        px[i * 4 + 2] = (uint8_t)(i % 2 ? 20 : 230);
        px[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 4, 4, 4, px, 4 * 4) != 0);

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
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "restart.png",
        .resolution_scale = 1});
    char *path = flecs_asprintf("%s/restart.png", tmp);
    unsigned char *first = NULL;
    int32_t run, lit = 0;
    for (run = 0; run < 3; run ++) {
        remove(path);
        RenderSettings *settings = ecs_singleton_get_mut(world, RenderSettings);
        settings->main_thread = run == 1;
        ecs_singleton_modified(world, RenderSettings);
        ecs_progress(world, 0);
        ecs_progress(world, 0);
        box_render_sync(world);

        int w = 0, h = 0, n = 0, wrong = 0;
        unsigned char *img = stbi_load(path, &w, &h, &n, 4);
        test_assert(img != NULL);
        test_int(w, 64);
        test_int(h, 64);
        if (!first) {
            first = img;
            for (i = 0; i < w * h; i ++) {
                lit += first[i * 4] > 8 || first[i * 4 + 1] > 8 || first[i * 4 + 2] > 8;
            }

            test_assert(lit > 64 * 64 / 8);
            continue;
        }

        for (i = 0; i < w * h * 4; i ++) {
            wrong += abs((int)img[i] - (int)first[i]) > 2;
        }

        test_int(wrong, 0);
        stbi_image_free(img);
    }

    stbi_image_free(first);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}
