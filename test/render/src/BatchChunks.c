/**
 * @file BatchChunks.c
 * @brief Tests for batches with more instances than one instance buffer binding holds.
 *
 * The first case renders 2^20 + 16 unlit boxes of one key (most behind the camera) into a
 * 32x32 capture; the second lowers the instances per batch and checks a 4x4 grid of boxes.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void BatchChunks_instances_past_binding_limit_render(void) {
    // render_fixes/unbounded_batches
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 50});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}});

    int32_t count = (1 << 20) + 16, i;
    Position *pos = ecs_os_malloc_n(Position, count);
    Box *box = ecs_os_malloc_n(Box, count);
    Rgb *rgb = ecs_os_malloc_n(Rgb, count);
    for (i = 0; i < count; i ++) {
        pos[i] = (Position){0, 0, 200};
        box[i] = (Box){4, 4, 4};
        rgb[i] = (Rgb){255, 255, 255};
    }

    pos[0] = (Position){-8, 0, 0};
    pos[count - 1] = (Position){8, 0, 0};
    void *data[] = {pos, box, rgb};
    ecs_bulk_init(world, &(ecs_bulk_desc_t){
        .count = count,
        .ids = {ecs_id(Position), ecs_id(Box), ecs_id(Rgb)},
        .data = data
    });
    ecs_os_free(pos);
    ecs_os_free(box);
    ecs_os_free(rgb);

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 32, .height = 32, .resolution_scale = 1,
        .write_to_file = "chunks.png"});
    ecs_log_set_level(-4);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_log_set_level(-1);

    char *path = flecs_asprintf("%s/chunks.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 8) * 4] > 200);
    test_assert(px[(16 * 32 + 24) * 4] > 200);
    test_assert(px[(16 * 32 + 16) * 4] < 20);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void BatchChunks_shapes_across_batches_stay_drawn(void) {
    // render_fixes/unbounded_batches
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    box_render_draws_chunk(world, 3);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 50});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}});
    ecs_entity_t shapes[16];
    int32_t i;
    for (i = 0; i < 16; i ++) {
        shapes[i] = ecs_new(world);
        ecs_set(world, shapes[i], Position, {(float)(i % 4) * 8 - 12, 12 - (float)(i / 4) * 8, 0});
        ecs_set(world, shapes[i], Box, {4, 4, 4});
        ecs_set(world, shapes[i], Rgb, {255, 255, 255});
        if (i % 5 == 4) {
            ecs_set(world, shapes[i], Opacity, {0.5f});
        }
    }

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 32, .height = 32, .resolution_scale = 1,
        .write_to_file = "grid.png"});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/grid.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0; i < 16; i ++) {
        const unsigned char *p = &px[((4 + (i / 4) * 8) * 32 + 4 + (i % 4) * 8) * 4];
        test_assert(p[1] > (i % 5 == 4 ? 100 : 200));
    }

    stbi_image_free(px);

    ecs_delete(world, shapes[0]);
    ecs_delete(world, shapes[1]);
    shapes[0] = ecs_new(world);
    ecs_set(world, shapes[0], Position, {-12, 12, 0});
    ecs_set(world, shapes[0], Box, {4, 4, 4});
    ecs_set(world, shapes[0], Rgb, {255, 255, 255});
    ecs_set(world, shapes[15], Rgb, {255, 0, 0});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0; i < 16; i ++) {
        const unsigned char *p = &px[((4 + (i / 4) * 8) * 32 + 4 + (i % 4) * 8) * 4];
        if (i == 1) {
            test_assert(p[0] < 20);
        } else if (i == 15) {
            test_assert(p[0] > 200);
            test_assert(p[1] < 20);
        } else {
            test_assert(p[1] > (i % 5 == 4 ? 100 : 200));
        }
    }

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}
