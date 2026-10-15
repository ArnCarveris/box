/**
 * @file ShaderCompile.c
 * @brief Tests for the outcome of a GPU compile: a module that fails keeps the previous one.
 *
 * The case renders an unlit red cube from 5 units in front of the origin into a 64x64
 * capture; the cube covers the middle pixel.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void ShaderCompile_failed_recompile_keeps_module(void) {
    // render_fixes/shader_compile_web
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
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "compile.png",
        .resolution_scale = 1});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *png = flecs_asprintf("%s/compile.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(png, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 32) * 4] > 200);
    test_assert(px[(32 * 64 + 32) * 4 + 1] < 40);
    stbi_image_free(px);

    ecs_entity_t mesh = ecs_lookup(world, "box.shader.builtin.mesh");
    test_assert(mesh != 0);
    char *orig = ecs_os_strdup(ecs_get(world, mesh, Shader)->path);
    char *bad = flecs_asprintf("%s/bad_mesh.wgsl", tmp);
    FILE *f = fopen(bad, "w");
    fputs("fn f() -> f32 { return undefined_name; }\n", f);
    fclose(f);
    ecs_set(world, mesh, Shader, {.path = bad});
    ecs_log_set_level(-4);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_log_set_level(-1);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    const ShaderStatus *st = ecs_get(world, mesh, ShaderStatus);
    test_assert(st != NULL);
    test_bool(st->ok, false);
    test_assert(st->error != NULL);
    test_assert(strstr(st->error, "bad_mesh.wgsl:1: ") != NULL);
    test_assert(strstr(st->error, "undefined_name") != NULL);
    px = stbi_load(png, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 32) * 4] > 200);
    test_assert(px[(32 * 64 + 32) * 4 + 1] < 40);
    stbi_image_free(px);

    ecs_set(world, mesh, Shader, {.path = orig});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    st = ecs_get(world, mesh, ShaderStatus);
    test_bool(st->ok, true);
    test_assert(st->error == NULL);
    px = stbi_load(png, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 32) * 4] > 200);
    stbi_image_free(px);

    ecs_os_free(orig);
    ecs_os_free(bad);
    ecs_os_free(png);
    ecs_fini(world);
}
