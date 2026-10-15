/**
 * @file BlendedDraws.c
 * @brief Tests for the blended draws: many instances of interleaved batches blend back to front.
 *
 * Eight columns of eight half transparent quads at shuffled depths render unlit through an
 * orthographic camera into a 64x16 capture; a column is 6 pixels wide, 8 pixels apart.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void BlendedDraws_interleaved_batches_blend_back_to_front(void) {
    // render_fixes/blended_draws
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 20});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}});

    float expect[8][3] = {{0}};
    int32_t col, k, layer;
    for (col = 0; col < 8; col ++) {
        for (k = 0; k < 8; k ++) {
            layer = (k * 5 + col * 3) % 8;
            bool blue = ((col * 7 + layer * 5) % 3) == 0;
            bool thick = ((col + layer * 3) % 2) == 0;
            ecs_entity_t e = ecs_new(world);
            ecs_set(world, e, Position, {(float)(col * 8 - 28), 0, -(float)layer});
            ecs_set(world, e, Box, {6, 12, thick ? 0.2f : 0.1f});
            ecs_set(world, e, Rgb, {0, blue ? 0 : 255, blue ? 255 : 0});
            ecs_set(world, e, Opacity, {0.5f});
        }

        for (layer = 7; layer >= 0; layer --) {
            bool blue = ((col * 7 + layer * 5) % 3) == 0;
            expect[col][1] = 0.5f * (blue ? 0.0f : 1.0f) + 0.5f * expect[col][1];
            expect[col][2] = 0.5f * (blue ? 1.0f : 0.0f) + 0.5f * expect[col][2];
        }
    }

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 16, .resolution_scale = 1,
        .write_to_file = "blended.png"});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/blended.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (col = 0; col < 8; col ++) {
        const unsigned char *p = &px[(8 * 64 + 32 + col * 8 - 28) * 4];
        int32_t c;
        for (c = 1; c < 3; c ++) {
            float lin = expect[col][c];
            float srgb = lin <= 0.0031308f ? lin * 12.92f
                : 1.055f * powf(lin, 1.0f / 2.4f) - 0.055f;
            int32_t want = (int32_t)(srgb * 255.0f + 0.5f);
            if (abs((int32_t)p[c] - want) > 8) {
                printf("column %d channel %d: got %d, want %d\n", col, c, p[c], want);
            }

            test_assert(abs((int32_t)p[c] - want) <= 8);
        }

        test_assert(p[0] < 8);
    }

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}
