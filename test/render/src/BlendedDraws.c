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

void BlendedDraws_one_batch_is_few_draws(void) {
    // render_fixes/blended_draws
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    int32_t i;
    for (i = 0; i < 200; i ++) {
        int32_t at = (i * 7919) % 200;
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)(at % 10), 0, -(float)((at * 7) % 50) - 1});
        ecs_set(world, e, Box, {1, 1, 1});
        ecs_set(world, e, Opacity, {0.5f});
    }

    ecs_progress(world, 0);
    box_render_sync(world);
    int32_t draws = 0, instances = 0;
    for (i = 0; i < ecs_vec_count(&null->last.batches); i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&null->last.batches, box_render_batch_t, i);
        if (b->view == 0) {
            draws ++;
            instances += (int32_t)b->instance_count;
        }
    }

    test_int(instances, 200);
    test_assert(draws <= 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void BlendedDraws_interleaved_batches_one_draw_per_run(void) {
    // render_fixes/blended_draws
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    int32_t i;
    for (i = 0; i < 200; i ++) {
        int32_t at = (i * 7919) % 200;
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)(at % 10), 0, -(float)at - 1});
        if ((at / 50) % 2) {
            ecs_set(world, e, Sphere, {.segments = 8, .radius = 0.5f});
        } else {
            ecs_set(world, e, Box, {1, 1, 1});
        }

        ecs_set(world, e, Opacity, {0.5f});
    }

    ecs_progress(world, 0);
    box_render_sync(world);
    int32_t draws = 0, instances = 0;
    for (i = 0; i < ecs_vec_count(&null->last.batches); i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&null->last.batches, box_render_batch_t, i);
        if (b->view == 0) {
            draws ++;
            instances += (int32_t)b->instance_count;
        }
    }

    test_int(instances, 200);
    test_int(draws, 4);

    ecs_fini(world);
    box_render_null_free(null);
}

void BlendedDraws_long_run_blends_back_to_front(void) {
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

    float expect[4][3] = {{0}};
    int32_t col, k, layer;
    for (col = 0; col < 4; col ++) {
        for (k = 0; k < 160; k ++) {
            layer = (k * 37 + col * 11) % 160;
            bool blue = ((col * 7 + layer * 5) % 3) == 0;
            ecs_entity_t e = ecs_new(world);
            ecs_set(world, e, Position, {(float)(col * 8 - 12), 0, -(float)layer * 0.05f});
            ecs_set(world, e, Box, {6, 12, 0.01f});
            ecs_set(world, e, Rgb, {0, blue ? 0 : 255, blue ? 255 : 0});
            ecs_set(world, e, Opacity, {0.5f});
        }

        for (layer = 159; layer >= 0; layer --) {
            bool blue = ((col * 7 + layer * 5) % 3) == 0;
            expect[col][1] = 0.5f * (blue ? 0.0f : 1.0f) + 0.5f * expect[col][1];
            expect[col][2] = 0.5f * (blue ? 1.0f : 0.0f) + 0.5f * expect[col][2];
        }
    }

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 32, .height = 16, .resolution_scale = 1,
        .write_to_file = "long_run.png"});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/long_run.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (col = 0; col < 4; col ++) {
        const unsigned char *p = &px[(8 * 32 + 16 + col * 8 - 12) * 4];
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
    }

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}
