/**
 * @file ShaderCompile.c
 * @brief Tests for the outcome of a GPU compile: a module that fails keeps the previous one.
 *
 * The first case renders an unlit red cube. The others delay compile outcomes as the browser
 * does (box_render_gpu_compile_delivery) and swap the blit, which scales the half resolution
 * scene of a 16x16 surface into its capture, for one that fills it with a color.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Blit shader that fills the image with magenta */
static const char *shader_compile_magenta =
    "struct V { @builtin(position) pos: vec4f, @location(0) uv: vec2f, };\n"
    "@vertex fn vs(@builtin(vertex_index) i: u32) -> V {\n"
    "    let p = vec2f(f32((i << 1u) & 2u), f32(i & 2u));\n"
    "    return V(vec4f(p * 2.0 - 1.0, 0.0, 1.0), vec2f(p.x, 1.0 - p.y));\n"
    "}\n"
    "@group(0) @binding(0) var scene: texture_2d<f32>;\n"
    "@group(0) @binding(1) var scene_sampler: sampler;\n"
    "@fragment fn fs(v: V) -> @location(0) vec4f {\n"
    "    return textureSample(scene, scene_sampler, v.uv) * 0.0 + vec4f(1.0, 0.0, 1.0, 1.0);\n"
    "}\n";

/** Blit shader that fills the image with blue */
static const char *shader_compile_blue =
    "struct V { @builtin(position) pos: vec4f, @location(0) uv: vec2f, };\n"
    "@vertex fn vs(@builtin(vertex_index) i: u32) -> V {\n"
    "    let p = vec2f(f32((i << 1u) & 2u), f32(i & 2u));\n"
    "    return V(vec4f(p * 2.0 - 1.0, 0.0, 1.0), vec2f(p.x, 1.0 - p.y));\n"
    "}\n"
    "@group(0) @binding(0) var scene: texture_2d<f32>;\n"
    "@group(0) @binding(1) var scene_sampler: sampler;\n"
    "@fragment fn fs(v: V) -> @location(0) vec4f {\n"
    "    return textureSample(scene, scene_sampler, v.uv) * 0.0 + vec4f(0.0, 0.0, 1.0, 1.0);\n"
    "}\n";

/** Shader with an error on its second line */
static const char *shader_compile_broken =
    "const A = 1.0;\n"
    "fn f() -> f32 { return undefined_name; }\n";

void ShaderCompile_failed_recompile_keeps_module(void) {
    // render_fixes/shader_compile_web
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, FileWatch, {.interval = -1});
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

void ShaderCompile_recompile_in_flight_keeps_old_module(void) {
    // render_fixes/shader_compile_web
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, FileWatch, {.interval = -1});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 255, 0, 255}}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 16, .height = 16, .write_to_file = "async.png",
        .resolution_scale = 2});
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *png = flecs_asprintf("%s/async.png", tmp);
    char *magenta = flecs_asprintf("%s/magenta.wgsl", tmp);
    FILE *f = fopen(magenta, "w");
    fputs(shader_compile_magenta, f);
    fclose(f);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_entity_t blit = ecs_lookup(world, "box.shader.builtin.blit");
    test_assert(blit != 0);
    test_bool(box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_NEXT_PACKET), true);

    ecs_set(world, blit, Shader, {.path = magenta});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(png, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(8 * 16 + 8) * 4] < 20);
    test_assert(px[(8 * 16 + 8) * 4 + 1] > 230);
    stbi_image_free(px);

    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_progress(world, 1.0f / 60);
    px = stbi_load(png, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(8 * 16 + 8) * 4] > 230);
    test_assert(px[(8 * 16 + 8) * 4 + 1] < 20);
    test_assert(px[(8 * 16 + 8) * 4 + 2] > 230);
    stbi_image_free(px);
    const ShaderStatus *st = ecs_get(world, blit, ShaderStatus);
    test_bool(st->ok, true);
    test_assert(st->error == NULL);

    ecs_os_free(magenta);
    ecs_os_free(png);
    ecs_fini(world);
}

void ShaderCompile_broken_recompile_in_flight_reports_error(void) {
    // render_fixes/shader_compile_web
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, FileWatch, {.interval = -1});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 255, 0, 255}}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 16, .height = 16, .write_to_file = "async.png",
        .resolution_scale = 2});
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *png = flecs_asprintf("%s/async.png", tmp);
    char *magenta = flecs_asprintf("%s/magenta.wgsl", tmp);
    char *broken = flecs_asprintf("%s/broken.wgsl", tmp);
    FILE *f = fopen(magenta, "w");
    fputs(shader_compile_magenta, f);
    fclose(f);
    f = fopen(broken, "w");
    fputs(shader_compile_broken, f);
    fclose(f);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_entity_t blit = ecs_lookup(world, "box.shader.builtin.blit");
    ecs_set(world, blit, Shader, {.path = magenta});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_NEXT_PACKET);

    ecs_set(world, blit, Shader, {.path = broken});
    ecs_log_set_level(-4);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    const ShaderStatus *st = ecs_get(world, blit, ShaderStatus);
    test_bool(st->ok, true);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_log_set_level(-1);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    st = ecs_get(world, blit, ShaderStatus);
    test_bool(st->ok, false);
    test_assert(st->error != NULL);
    test_assert(strstr(st->error, "broken.wgsl:2: ") != NULL);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(png, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(8 * 16 + 8) * 4] > 230);
    test_assert(px[(8 * 16 + 8) * 4 + 1] < 20);
    stbi_image_free(px);

    ecs_os_free(broken);
    ecs_os_free(magenta);
    ecs_os_free(png);
    ecs_fini(world);
}

void ShaderCompile_error_stays_until_outcome(void) {
    // render_fixes/shader_compile_web
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, FileWatch, {.interval = -1});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 255, 0, 255}}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 16, .height = 16, .write_to_file = "async.png",
        .resolution_scale = 2});
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *png = flecs_asprintf("%s/async.png", tmp);
    char *magenta = flecs_asprintf("%s/magenta.wgsl", tmp);
    char *broken = flecs_asprintf("%s/broken.wgsl", tmp);
    FILE *f = fopen(magenta, "w");
    fputs(shader_compile_magenta, f);
    fclose(f);
    f = fopen(broken, "w");
    fputs(shader_compile_broken, f);
    fclose(f);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_entity_t blit = ecs_lookup(world, "box.shader.builtin.blit");
    ecs_set(world, blit, Shader, {.path = broken});
    ecs_log_set_level(-4);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_log_set_level(-1);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    test_bool(ecs_get(world, blit, ShaderStatus)->ok, false);
    box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_HOLD);

    ecs_set(world, blit, Shader, {.path = magenta});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    const ShaderStatus *st = ecs_get(world, blit, ShaderStatus);
    test_bool(st->ok, false);
    test_assert(st->error != NULL);
    test_assert(strstr(st->error, "broken.wgsl:2: ") != NULL);

    box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_NEXT_PACKET);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    st = ecs_get(world, blit, ShaderStatus);
    test_bool(st->ok, true);
    test_assert(st->error == NULL);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(png, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(8 * 16 + 8) * 4] > 230);
    test_assert(px[(8 * 16 + 8) * 4 + 1] < 20);
    stbi_image_free(px);

    ecs_os_free(broken);
    ecs_os_free(magenta);
    ecs_os_free(png);
    ecs_fini(world);
}

void ShaderCompile_two_recompiles_in_flight_end_with_newest(void) {
    // render_fixes/shader_compile_web
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, FileWatch, {.interval = -1});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 255, 0, 255}}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 16, .height = 16, .write_to_file = "async.png",
        .resolution_scale = 2});
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *png = flecs_asprintf("%s/async.png", tmp);
    char *magenta = flecs_asprintf("%s/magenta.wgsl", tmp);
    char *blue = flecs_asprintf("%s/blue.wgsl", tmp);
    FILE *f = fopen(magenta, "w");
    fputs(shader_compile_magenta, f);
    fclose(f);
    f = fopen(blue, "w");
    fputs(shader_compile_blue, f);
    fclose(f);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_entity_t blit = ecs_lookup(world, "box.shader.builtin.blit");
    box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_HOLD);

    ecs_set(world, blit, Shader, {.path = magenta});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_set(world, blit, Shader, {.path = blue});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(png, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(8 * 16 + 8) * 4 + 1] > 230);
    stbi_image_free(px);

    box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_NEXT_PACKET);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    px = stbi_load(png, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(8 * 16 + 8) * 4] < 20);
    test_assert(px[(8 * 16 + 8) * 4 + 1] < 20);
    test_assert(px[(8 * 16 + 8) * 4 + 2] > 230);
    stbi_image_free(px);
    test_bool(ecs_get(world, blit, ShaderStatus)->ok, true);

    ecs_os_free(blue);
    ecs_os_free(magenta);
    ecs_os_free(png);
    ecs_fini(world);
}

void ShaderCompile_late_older_outcome_is_ignored(void) {
    // render_fixes/shader_compile_web
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, FileWatch, {.interval = -1});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 255, 0, 255}}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 16, .height = 16, .write_to_file = "async.png",
        .resolution_scale = 2});
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *png = flecs_asprintf("%s/async.png", tmp);
    char *magenta = flecs_asprintf("%s/magenta.wgsl", tmp);
    char *blue = flecs_asprintf("%s/blue.wgsl", tmp);
    FILE *f = fopen(magenta, "w");
    fputs(shader_compile_magenta, f);
    fclose(f);
    f = fopen(blue, "w");
    fputs(shader_compile_blue, f);
    fclose(f);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_entity_t blit = ecs_lookup(world, "box.shader.builtin.blit");
    box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_HOLD);

    ecs_set(world, blit, Shader, {.path = magenta});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_set(world, blit, Shader, {.path = blue});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_NEWEST_FIRST);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(png, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(8 * 16 + 8) * 4] < 20);
    test_assert(px[(8 * 16 + 8) * 4 + 1] < 20);
    test_assert(px[(8 * 16 + 8) * 4 + 2] > 230);
    stbi_image_free(px);
    test_bool(ecs_get(world, blit, ShaderStatus)->ok, true);

    ecs_os_free(blue);
    ecs_os_free(magenta);
    ecs_os_free(png);
    ecs_fini(world);
}

void ShaderCompile_new_shader_before_first_outcome(void) {
    // render_fixes/shader_compile_web
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, FileWatch, {.interval = -1});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 255, 0, 255}}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 16, .height = 16, .write_to_file = "async.png",
        .resolution_scale = 2});
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *png = flecs_asprintf("%s/async.png", tmp);
    char *broken = flecs_asprintf("%s/broken.wgsl", tmp);
    FILE *f = fopen(broken, "w");
    fputs(shader_compile_broken, f);
    fclose(f);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_HOLD);

    ecs_entity_t shader = ecs_new(world);
    ecs_set(world, shader, Shader, {.path = broken});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    const ShaderStatus *st = ecs_get(world, shader, ShaderStatus);
    test_assert(st != NULL);
    test_int(st->revision, 1);
    test_bool(st->ok, true);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(png, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(8 * 16 + 8) * 4 + 1] > 230);
    stbi_image_free(px);

    box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_NEXT_PACKET);
    ecs_log_set_level(-4);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_log_set_level(-1);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    st = ecs_get(world, shader, ShaderStatus);
    test_bool(st->ok, false);
    test_assert(st->error != NULL);
    test_assert(strstr(st->error, "broken.wgsl:2: ") != NULL);

    ecs_os_free(broken);
    ecs_os_free(png);
    ecs_fini(world);
}

void ShaderCompile_delete_shader_mid_compile(void) {
    // render_fixes/shader_compile_web
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, FileWatch, {.interval = -1});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 255, 0, 255}}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 16, .height = 16, .write_to_file = "async.png",
        .resolution_scale = 2});
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *png = flecs_asprintf("%s/async.png", tmp);
    char *magenta = flecs_asprintf("%s/magenta.wgsl", tmp);
    FILE *f = fopen(magenta, "w");
    fputs(shader_compile_magenta, f);
    fclose(f);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_HOLD);

    ecs_entity_t shader = ecs_new(world);
    ecs_set(world, shader, Shader, {.path = magenta});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_delete(world, shader);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_NEXT_PACKET);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    test_assert(!ecs_is_alive(world, shader));
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(png, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(8 * 16 + 8) * 4] < 20);
    test_assert(px[(8 * 16 + 8) * 4 + 1] > 230);
    stbi_image_free(px);

    ecs_os_free(magenta);
    ecs_os_free(png);
    ecs_fini(world);
}

void ShaderCompile_fini_mid_compile(void) {
    // render_fixes/shader_compile_web
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, FileWatch, {.interval = -1});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 255, 0, 255}}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 16, .height = 16, .resolution_scale = 1});
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *magenta = flecs_asprintf("%s/magenta.wgsl", tmp);
    FILE *f = fopen(magenta, "w");
    fputs(shader_compile_magenta, f);
    fclose(f);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    box_render_gpu_compile_delivery(world, BOX_RENDER_COMPILE_HOLD);

    ecs_entity_t blit = ecs_lookup(world, "box.shader.builtin.blit");
    ecs_set(world, blit, Shader, {.path = magenta});
    ecs_entity_t shader = ecs_new(world);
    ecs_set(world, shader, Shader, {.path = magenta});
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
    test_bool(ecs_get(world, shader, ShaderStatus)->ok, true);

    ecs_os_free(magenta);
    ecs_fini(world);
    test_assert(true);
}
