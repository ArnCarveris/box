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

void PipelineCache_invalid_back_face_mode_keeps_states_apart(void) {
    // render_fixes/pipeline_cache
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 50});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {128, 128, 128, 255}}});
    BackFaceMode invalid = (BackFaceMode)16;
    ecs_entity_t opaque = ecs_new(world);
    ecs_set(world, opaque, Position, {-10, 0, 0});
    ecs_set(world, opaque, Box, {8, 8, 8});
    ecs_set(world, opaque, Rgb, {255, 0, 0});
    ecs_set_ptr(world, opaque, BackFaceMode, &invalid);
    ecs_entity_t blended = ecs_new(world);
    ecs_set(world, blended, Position, {10, 0, 0});
    ecs_set(world, blended, Box, {8, 8, 8});
    ecs_set(world, blended, Rgb, {0, 0, 255});
    ecs_set(world, blended, Opacity, {0.5f});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .resolution_scale = 1,
        .write_to_file = "back_face.png"});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/back_face.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    const unsigned char *a = &px[(32 * 64 + 22) * 4];
    const unsigned char *b = &px[(32 * 64 + 42) * 4];
    test_assert(a[0] > 200);
    test_assert(a[2] < 40);
    test_assert(b[0] > 60);
    test_assert(b[0] < 120);
    test_assert(b[2] > 170);
    test_assert(b[2] < 235);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void PipelineCache_shader_reload_redraws_every_state(void) {
    // render_fixes/pipeline_cache
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, ShaderSettings, {.poll_interval = -1});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 50});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {128, 128, 128, 255}}});
    BlendMode blends[3] = {Opaque, AlphaBlend, PremultipliedAlpha};
    int32_t i;
    for (i = 0; i < 12; i ++) {
        ecs_entity_t e = ecs_new(world);
        BackFaceMode back_face = (BackFaceMode)((i / 3) % 2);
        ecs_set(world, e, Position, {(float)(i % 4) * 14 - 21, 14 - (float)(i / 4) * 14, 0});
        if (i < 6) {
            ecs_set(world, e, Box, {8, 8, 8});
        } else {
            ecs_set(world, e, Rect, {8, 8});
        }

        ecs_set(world, e, Rgb, {255, 0, 0});
        ecs_set_ptr(world, e, BlendMode, &blends[i % 3]);
        ecs_set_ptr(world, e, BackFaceMode, &back_face);
    }

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .resolution_scale = 1,
        .write_to_file = "reload.png"});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *png = flecs_asprintf("%s/reload.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(png, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0; i < 12; i ++) {
        const unsigned char *p = &px[((18 + (i / 4) * 14) * 64 + 11 + (i % 4) * 14) * 4];
        test_assert(p[0] > 200);
        test_assert(p[2] < 40);
    }

    stbi_image_free(px);

    int32_t root = (int32_t)(strstr(__FILE__, "test/gpu/render/src/") - __FILE__);
    char *mesh = flecs_asprintf("%s/magenta_mesh.wgsl", tmp);
    char *shape = flecs_asprintf("%s/magenta_shape2d.wgsl", tmp);
    FILE *f = fopen(mesh, "w");
    fprintf(f, "#define fs fs_original\n#include \"%.*setc/shaders/mesh.wgsl\"\n#undef fs\n"
        "@fragment fn fs(f: VertexOut, @builtin(front_facing) front: bool) -> "
        "@location(0) vec4f { return vec4f(1.0, 0.0, 1.0, 1.0); }\n", root, __FILE__);
    fclose(f);
    f = fopen(shape, "w");
    fprintf(f, "#define fs fs_original\n#include \"%.*setc/shaders/shape2d.wgsl\"\n#undef fs\n"
        "@fragment fn fs(f: VertexOut) -> @location(0) vec4f "
        "{ return vec4f(1.0, 0.0, 1.0, 1.0); }\n", root, __FILE__);
    fclose(f);
    ecs_entity_t mesh_shader = ecs_lookup(world, "box.shader.builtin.mesh");
    ecs_entity_t shape_shader = ecs_lookup(world, "box.shader.builtin.shape2d");
    ecs_set(world, mesh_shader, Shader, {.path = mesh});
    ecs_set(world, shape_shader, Shader, {.path = shape});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    test_bool(ecs_get(world, mesh_shader, ShaderStatus)->ok, true);
    test_bool(ecs_get(world, shape_shader, ShaderStatus)->ok, true);
    px = stbi_load(png, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0; i < 12; i ++) {
        const unsigned char *p = &px[((18 + (i / 4) * 14) * 64 + 11 + (i % 4) * 14) * 4];
        test_assert(p[0] > 200);
        test_assert(p[1] < 40);
        test_assert(p[2] > 200);
    }

    stbi_image_free(px);
    ecs_os_free(mesh);
    ecs_os_free(shape);
    ecs_os_free(png);
    ecs_fini(world);
}

void PipelineCache_released_shader_draws_nothing(void) {
    // render_fixes/pipeline_cache
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 50});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {128, 128, 128, 255}}});
    ecs_entity_t cube = ecs_new(world);
    ecs_set(world, cube, Box, {8, 8, 8});
    ecs_set(world, cube, Rgb, {255, 0, 0});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 32, .height = 32, .resolution_scale = 1,
        .write_to_file = "released.png"});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *png = flecs_asprintf("%s/released.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(png, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 16) * 4] > 200);
    test_assert(px[(16 * 32 + 16) * 4 + 1] < 40);
    stbi_image_free(px);

    ecs_entity_t mesh_shader = ecs_lookup(world, "box.shader.builtin.mesh");
    ecs_remove(world, mesh_shader, Shader);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    px = stbi_load(png, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(16 * 32 + 16) * 4] > 118);
    test_assert(px[(16 * 32 + 16) * 4] < 138);
    test_assert(px[(16 * 32 + 16) * 4 + 1] > 118);
    stbi_image_free(px);

    ecs_os_free(png);
    ecs_fini(world);
}
