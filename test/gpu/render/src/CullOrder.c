/**
 * @file CullOrder.c
 * @brief Tests that the cull pass keeps the instance order of a draw, so frames are identical.
 *
 * Pairs of boxes of one mesh share a position with different colors, so every pixel of a box
 * is an exact depth tie that the first drawn instance of the pair wins.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void CullOrder_equal_depth_ties_render_identically(void) {
    // render_fixes/deterministic_cull
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 30, 40});
    ecs_set(world, camera, Rotation, {-0.6f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->shadows.enabled = true;
    ecs_modified(world, view, View);
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.5423041f, 2.3679806f, -0.4669112f});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f * GLM_PIf});
    int32_t i, k;
    for (k = 0; k < 2; k ++) {
        for (i = 0; i < 2048; i ++) {
            ecs_entity_t e = ecs_new(world);
            ecs_set(world, e, Position, {(float)(i % 64) - 32, 0.5f, -(float)(i / 64) - 2});
            ecs_set(world, e, Box, {0.9f, 0.9f, 0.9f});
            ecs_set(world, e, Rgb, {(uint8_t)(k ? 40 : 230), 120, (uint8_t)(k ? 230 : 40)});
        }
    }

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 96, .height = 64, .resolution_scale = 1,
        .write_to_file = "order.png"});
    char *path = flecs_asprintf("%s/order.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    unsigned char *first = NULL;
    int32_t frame, differ = 0;
    for (frame = 0; frame < 8; frame ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        if (frame < 2) {
            continue;
        }

        int w = 0, h = 0, n = 0;
        unsigned char *px = stbi_load(path, &w, &h, &n, 4);
        test_assert(px != NULL);
        if (!first) {
            first = px;
            continue;
        }

        if (memcmp(first, px, (size_t)(w * h * 4))) {
            differ ++;
        }

        stbi_image_free(px);
    }

    test_int(differ, 0);
    stbi_image_free(first);
    ecs_os_free(path);
    ecs_fini(world);
}
