/**
 * @file BatchChunks.c
 * @brief Tests for batches with more instances than one instance buffer binding holds.
 *
 * The first case renders 2^20 + 16 unlit boxes of one key (most behind the camera) into a
 * 32x32 capture; the others lower the instances per batch, the cull page size and the slots of
 * an instance page.
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
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    box_render_gpu_stats_t stats;
    test_bool(box_render_gpu_stats(world, &stats), true);
    test_int(stats.errors, 0);

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

void BatchChunks_lowered_limits_render_like_defaults(void) {
    // render_fixes/unbounded_batches
    unsigned char *images[2] = {NULL, NULL};
    int32_t run, i;
    for (run = 0; run < 2; run ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);
        if (run) {
            box_render_draws_chunk(world, 2);
        }

        ecs_entity_t camera = ecs_new(world);
        ecs_set(world, camera, Position, {0, 4, 14});
        ecs_set(world, camera, Rotation, {-0.25f, 0, 0});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
        ecs_entity_t view = ecs_new(world);
        ecs_add(world, view, View);
        View *v = ecs_get_mut(world, view, View);
        v->camera = camera;
        v->clear = (ClearSettings){.enabled = true, .color = {20, 20, 30, 255}};
        v->shadows.enabled = true;
        ecs_modified(world, view, View);
        ecs_entity_t sun = ecs_new(world);
        ecs_set(world, sun, Rotation, {0.5423041f, 2.3679806f, -0.4669112f});
        ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f * GLM_PIf});
        ecs_entity_t floor = ecs_new(world);
        ecs_set(world, floor, Position, {0, -0.1f, 0});
        ecs_set(world, floor, Box, {30, 0.2f, 30});
        ecs_set(world, floor, Rgb, {120, 120, 120});
        ecs_entity_t wall = ecs_new(world);
        ecs_set(world, wall, Position, {-3, 1.5f, 4});
        ecs_set(world, wall, Box, {6, 3, 0.2f});
        ecs_set(world, wall, Rgb, {200, 40, 40});
        for (i = 0; i < 12; i ++) {
            ecs_entity_t e = ecs_new(world);
            if (i % 3 == 0) {
                ecs_add(world, e, DynamicTransform);
            }

            ecs_set(world, e, Position, {(float)(i % 4) * 2.5f - 4, 0.5f, -(float)(i / 4) * 2.5f});
            ecs_set(world, e, Box, {1, 1, 1});
            ecs_set(world, e, Rgb, {40, (uint8_t)(100 + i * 10), 200});
        }

        for (i = 0; i < 5; i ++) {
            int32_t layer = (i * 3) % 5;
            ecs_entity_t e = ecs_new(world);
            ecs_set(world, e, Position, {3, 1.5f, 6 - (float)layer * 0.5f});
            ecs_set(world, e, Box, {2, 2, 0.05f});
            ecs_set(world, e, Rgb, {(uint8_t)(i % 2 ? 255 : 0), 200, (uint8_t)(i % 2 ? 0 : 255)});
            ecs_set(world, e, Opacity, {0.5f});
        }

        ecs_entity_t s = ecs_new(world);
        ecs_set(world, s, Surface, {.width = 64, .height = 64, .resolution_scale = 1,
            .write_to_file = run ? "lowered.png" : "defaults.png"});
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        if (run) {
            test_bool(box_render_gpu_cull_window(world, 4), true);
        }

        for (i = 0; i < 4; i ++) {
            ecs_progress(world, 1.0f / 60);
            box_render_sync(world);
        }

        box_render_gpu_stats_t stats;
        test_bool(box_render_gpu_stats(world, &stats), true);
        test_int(stats.errors, 0);
        char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir,
            run ? "lowered.png" : "defaults.png");
        int w = 0, h = 0, n = 0;
        images[run] = stbi_load(path, &w, &h, &n, 4);
        test_assert(images[run] != NULL);
        ecs_os_free(path);
        ecs_fini(world);
    }

    int32_t off = 0;
    for (i = 0; i < 64 * 64 * 4; i ++) {
        if (abs((int32_t)images[0][i] - (int32_t)images[1][i]) > 8) {
            off ++;
        }
    }

    test_int(off, 0);
    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
}

void BatchChunks_lowered_pages_render_like_defaults(void) {
    // gpu_instance_data/instance_store
    unsigned char *images[2] = {NULL, NULL};
    int32_t run, i;
    for (run = 0; run < 2; run ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);
        if (run) {
            box_render_draws_page(world, 4);
        }

        ecs_entity_t camera = ecs_new(world);
        ecs_set(world, camera, Position, {0, 4, 14});
        ecs_set(world, camera, Rotation, {-0.25f, 0, 0});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
        ecs_entity_t view = ecs_new(world);
        ecs_add(world, view, View);
        View *v = ecs_get_mut(world, view, View);
        v->camera = camera;
        v->clear = (ClearSettings){.enabled = true, .color = {20, 20, 30, 255}};
        v->shadows.enabled = true;
        v->culling = (CullSettings){.enabled = true, .occlusion = true};
        ecs_modified(world, view, View);
        ecs_entity_t sun = ecs_new(world);
        ecs_set(world, sun, Rotation, {0.5423041f, 2.3679806f, -0.4669112f});
        ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f * GLM_PIf});
        ecs_entity_t floor = ecs_new(world);
        ecs_set(world, floor, Position, {0, -0.1f, 0});
        ecs_set(world, floor, Box, {30, 0.2f, 30});
        ecs_set(world, floor, Rgb, {120, 120, 120});
        ecs_entity_t wall = ecs_new(world);
        ecs_set(world, wall, Position, {-3, 1.5f, 4});
        ecs_set(world, wall, Box, {6, 3, 0.2f});
        ecs_set(world, wall, Rgb, {200, 40, 40});
        for (i = 0; i < 12; i ++) {
            ecs_entity_t e = ecs_new(world);
            if (i % 3 == 0) {
                ecs_add(world, e, DynamicTransform);
            }

            ecs_set(world, e, Position, {(float)(i % 4) * 2.5f - 4, 0.5f, -(float)(i / 4) * 2.5f});
            ecs_set(world, e, Box, {1, 1, 1});
            ecs_set(world, e, Rgb, {40, (uint8_t)(100 + i * 10), 200});
        }

        for (i = 0; i < 5; i ++) {
            int32_t layer = (i * 3) % 5;
            ecs_entity_t e = ecs_new(world);
            ecs_set(world, e, Position, {3, 1.5f, 6 - (float)layer * 0.5f});
            ecs_set(world, e, Box, {2, 2, 0.05f});
            ecs_set(world, e, Rgb, {(uint8_t)(i % 2 ? 255 : 0), 200, (uint8_t)(i % 2 ? 0 : 255)});
            ecs_set(world, e, Opacity, {0.5f});
        }

        ecs_entity_t s = ecs_new(world);
        ecs_set(world, s, Surface, {.width = 64, .height = 64, .resolution_scale = 1,
            .write_to_file = run ? "paged.png" : "unpaged.png"});
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        box_render_draws_stats_t draws;
        box_render_draws_stats(world, &draws);
        test_assert(run ? draws.pages > 4 : draws.pages == 1);

        for (i = 0; i < 4; i ++) {
            ecs_progress(world, 1.0f / 60);
            box_render_sync(world);
        }

        box_render_gpu_stats_t stats;
        test_bool(box_render_gpu_stats(world, &stats), true);
        test_int(stats.errors, 0);
        char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir,
            run ? "paged.png" : "unpaged.png");
        int w = 0, h = 0, n = 0;
        images[run] = stbi_load(path, &w, &h, &n, 4);
        test_assert(images[run] != NULL);
        ecs_os_free(path);
        ecs_fini(world);
    }

    int32_t off = 0;
    for (i = 0; i < 64 * 64 * 4; i ++) {
        if (abs((int32_t)images[0][i] - (int32_t)images[1][i]) > 8) {
            off ++;
        }
    }

    test_int(off, 0);
    stbi_image_free(images[0]);
    stbi_image_free(images[1]);
}
