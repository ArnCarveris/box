/**
 * @file PipelineCache.c
 * @brief Tests for the forward pipelines: every combination of state in use draws.
 *
 * Shapes on a 6x6 grid 14 units apart cover every blend mode, back face mode and shader of
 * the forward pass; two 96x96 surfaces render them, one directly and one through an HDR effect.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void PipelineCache_every_state_combination_draws(void) {
    // render_fixes/pipeline_cache
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 50});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t root_ldr = ecs_new(world);
    ecs_entity_t root_hdr = ecs_new(world);
    ecs_entity_t ldr = ecs_new_w_pair(world, EcsChildOf, root_ldr);
    ecs_set(world, ldr, View, {.camera = camera, .clear = {.enabled = true,
        .color = {128, 128, 128, 255}}});
    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    ecs_entity_t hdr = ecs_new_w_pair(world, EcsChildOf, root_hdr);
    View value = {.camera = camera, .clear = {.enabled = true, .color = {128, 128, 128, 255}}};
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, 1);
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = tonemap };
    ecs_set_ptr(world, hdr, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);

    int32_t i;
    for (i = 0; i < 36; i ++) {
        ecs_entity_t e = ecs_new(world);
        BlendMode blend = (BlendMode)(i % 9);
        BackFaceMode back_face = (BackFaceMode)((i / 9) % 2);
        ecs_set(world, e, Position, {(float)(i % 6) * 14 - 35, 35 - (float)(i / 6) * 14, 0});
        if (i < 18) {
            ecs_set(world, e, Box, {8, 8, 8});
        } else {
            ecs_set(world, e, Rect, {8, 8});
        }

        ecs_set(world, e, Rgb, {255, 0, 0});
        ecs_set_ptr(world, e, BlendMode, &blend);
        ecs_set_ptr(world, e, BackFaceMode, &back_face);
    }

    ecs_entity_t s1 = ecs_new(world);
    ecs_set(world, s1, Surface, {.width = 96, .height = 96, .resolution_scale = 1,
        .view_root = root_ldr, .write_to_file = "pipelines_ldr.png"});
    ecs_entity_t s2 = ecs_new(world);
    ecs_set(world, s2, Surface, {.width = 96, .height = 96, .resolution_scale = 1,
        .view_root = root_hdr, .write_to_file = "pipelines_hdr.png"});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    const char *files[2] = {"pipelines_ldr.png", "pipelines_hdr.png"};
    int32_t f;
    for (f = 0; f < 2; f ++) {
        char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir,
            files[f]);
        int w = 0, h = 0, n = 0;
        unsigned char *px = stbi_load(path, &w, &h, &n, 4);
        test_assert(px != NULL);
        test_int(w, 96);
        const unsigned char *clear = &px[(2 * 96 + 2) * 4];
        for (i = 0; i < 36; i ++) {
            int32_t x = 48 + (i % 6) * 14 - 35;
            int32_t y = 48 - (35 - (i / 6) * 14);
            const unsigned char *p = &px[(y * 96 + x) * 4];
            int32_t c, diff = 0;
            for (c = 0; c < 3; c ++) {
                int32_t d = abs((int32_t)p[c] - (int32_t)clear[c]);
                diff = d > diff ? d : diff;
            }

            if (diff <= 24) {
                printf("%s: shape %d (blend %d, back face %d) not drawn\n", files[f], i,
                    i % 9, (i / 9) % 2);
            }

            test_assert(diff > 24);
        }

        stbi_image_free(px);
        ecs_os_free(path);
    }

    ecs_fini(world);
}
