/**
 * @file Render.c
 * @brief Tests for the capture and render commands, checked on the images they write.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Render_capture_active_camera(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t box = ecs_new(world);
    ecs_set(world, box, Box, {2, 2, 2});
    ecs_set(world, box, Rgb, {0, 0, 0});
    ecs_set(world, box, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, box, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 32, .height = 32});
    ecs_entity_t s = box_terminal_new(world);
    ecs_progress(world, 0);

    bool error = true;
    char *out = box_terminal_run(world, s, "capture", &error);
    test_bool(error, false);
    test_str(out, "capture_0.png (pending)");
    ecs_os_free(out);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/resources/capture_0.png",
        ecs_get(world, s, TerminalStatus)->dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 32);
    test_int(h, 32);
    test_assert(px[(16 * 32 + 16) * 4] > 200);
    test_int(px[0], 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Render_capture_named_camera(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t main_camera = ecs_entity(world, { .name = "main_camera" });
    ecs_set(world, main_camera, Position, {0, 0, 5});
    ecs_set(world, main_camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = main_camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t blue = ecs_new(world);
    ecs_set(world, blue, Box, {2, 2, 2});
    ecs_set(world, blue, Rgb, {0, 0, 0});
    ecs_set(world, blue, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, blue, Emissive, {.strength = 1, .color = {0, 0, 255}});
    ecs_entity_t scene = ecs_entity(world, { .name = "level" });
    ecs_set(world, scene, Scene, {0});
    ecs_entity_t red = ecs_entity(world, { .name = "level.crate" });
    ecs_set(world, red, Box, {2, 2, 2});
    ecs_set(world, red, Rgb, {0, 0, 0});
    ecs_set(world, red, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, red, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t other = ecs_entity(world, { .name = "level.other" });
    ecs_set(world, other, Camera, {.fov = 60});
    ecs_set(world, other, Position, {0, 0, 5});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 16, .height = 16});
    ecs_entity_t s = box_terminal_new(world);
    ecs_progress(world, 0);

    char *out = box_terminal_run(world, s, "capture level.other", NULL);
    test_str(out, "capture_0.png (pending)");
    ecs_os_free(out);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/resources/capture_0.png",
        ecs_get(world, s, TerminalStatus)->dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(8 * 16 + 8) * 4];
    test_assert(c[0] > 200);
    test_assert(c[2] < 40);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Render_capture_batch(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_set(world, a, Position, {-10, 0, 5});
    ecs_set(world, a, Camera, {.fov = 60});
    ecs_entity_t b = ecs_entity(world, { .name = "b" });
    ecs_set(world, b, Position, {10, 0, 5});
    ecs_set(world, b, Camera, {.fov = 60});
    ecs_entity_t red = ecs_new(world);
    ecs_set(world, red, Position, {-10, 0, 0});
    ecs_set(world, red, Box, {2, 2, 2});
    ecs_set(world, red, Rgb, {0, 0, 0});
    ecs_set(world, red, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, red, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t green = ecs_new(world);
    ecs_set(world, green, Position, {10, 0, 0});
    ecs_set(world, green, Box, {2, 2, 2});
    ecs_set(world, green, Rgb, {0, 0, 0});
    ecs_set(world, green, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, green, Emissive, {.strength = 1, .color = {0, 255, 0}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 16, .height = 16});
    ecs_entity_t s = box_terminal_new(world);
    ecs_progress(world, 0);

    char *out = box_terminal_run(world, s, "capture a, b", NULL);
    test_str(out, "capture_0.png (pending)\ncapture_1.png (pending)");
    ecs_os_free(out);
    ecs_progress(world, 0);
    box_render_sync(world);

    const char *dir = ecs_get(world, s, TerminalStatus)->dir;
    char *path = flecs_asprintf("%s/resources/capture_0.png", dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(8 * 16 + 8) * 4] > 200);
    test_assert(px[(8 * 16 + 8) * 4 + 1] < 40);
    stbi_image_free(px);
    ecs_os_free(path);

    path = flecs_asprintf("%s/resources/capture_1.png", dir);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(8 * 16 + 8) * 4] < 40);
    test_assert(px[(8 * 16 + 8) * 4 + 1] > 200);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Render_capture_bracket_list(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_set(world, a, Position, {-10, 0, 5});
    ecs_set(world, a, Camera, {.fov = 60});
    ecs_entity_t b = ecs_entity(world, { .name = "b" });
    ecs_set(world, b, Position, {10, 0, 5});
    ecs_set(world, b, Camera, {.fov = 60});
    ecs_entity_t red = ecs_new(world);
    ecs_set(world, red, Position, {-10, 0, 0});
    ecs_set(world, red, Box, {2, 2, 2});
    ecs_set(world, red, Rgb, {0, 0, 0});
    ecs_set(world, red, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, red, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t green = ecs_new(world);
    ecs_set(world, green, Position, {10, 0, 0});
    ecs_set(world, green, Box, {2, 2, 2});
    ecs_set(world, green, Rgb, {0, 0, 0});
    ecs_set(world, green, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, green, Emissive, {.strength = 1, .color = {0, 255, 0}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 16, .height = 16});
    ecs_entity_t s = box_terminal_new(world);
    ecs_progress(world, 0);

    char *out = box_terminal_run(world, s, "capture [b, a, b]", NULL);
    test_str(out,
        "capture_0.png (pending)\ncapture_1.png (pending)\ncapture_2.png (pending)");
    ecs_os_free(out);
    ecs_progress(world, 0);
    box_render_sync(world);

    const char *dir = ecs_get(world, s, TerminalStatus)->dir;
    bool green_expected[3] = {true, false, true};
    int32_t i;
    for (i = 0; i < 3; i ++) {
        char *path = flecs_asprintf("%s/resources/capture_%d.png", dir, i);
        int w = 0, h = 0, n = 0;
        unsigned char *px = stbi_load(path, &w, &h, &n, 4);
        test_assert(px != NULL);
        unsigned char *c = &px[(8 * 16 + 8) * 4];
        test_bool(c[1] > 200, green_expected[i]);
        test_bool(c[0] > 200, !green_expected[i]);
        stbi_image_free(px);
        ecs_os_free(path);
    }

    ecs_fini(world);
}

void Render_capture_size_of_surface(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 320, .height = 200});
    ecs_entity_t s = box_terminal_new(world);
    ecs_progress(world, 0);

    ecs_os_free(box_terminal_run(world, s, "capture", NULL));
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/resources/capture_0.png",
        ecs_get(world, s, TerminalStatus)->dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 320);
    test_int(h, 200);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Render_capture_clear_of_view(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_set(world, a, Camera, {.fov = 60});
    ecs_entity_t b = ecs_entity(world, { .name = "b" });
    ecs_set(world, b, Camera, {.fov = 60});
    ecs_entity_t view_a = ecs_new(world);
    ecs_set(world, view_a, View, {
        .camera = a, .clear = {.enabled = true, .color = {255, 0, 0, 255}}});
    ecs_entity_t view_b = ecs_new(world);
    ecs_set(world, view_b, View, {.camera = b});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 8, .height = 8});
    ecs_entity_t s = box_terminal_new(world);

    ecs_os_free(box_terminal_run(world, s, "capture b", NULL));
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/resources/capture_0.png",
        ecs_get(world, s, TerminalStatus)->dir);
    int w = 0, h = 0, n = 0, i, wrong = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0; i < w * h; i ++) {
        wrong += px[i * 4] != 255 || px[i * 4 + 1] || px[i * 4 + 2];
    }

    test_int(wrong, 0);
    test_assert(view_b != 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Render_capture_not_a_camera(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_set(world, a, Camera, {.fov = 60});
    ecs_entity(world, { .name = "thing" });
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 8, .height = 8});
    ecs_entity_t s = box_terminal_new(world);

    bool error = false;
    char *out = box_terminal_run(world, s, "capture a, thing", &error);
    test_bool(error, true);
    test_str(out, "error: 'thing' is not a camera");
    ecs_os_free(out);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/resources/capture_0.png",
        ecs_get(world, s, TerminalStatus)->dir);
    test_assert(fopen(path, "rb") == NULL);

    ecs_os_free(path);
    ecs_fini(world);
}

void Render_capture_unknown_camera(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "capture nope", &error);
    test_bool(error, true);
    test_str(out, "error: no entity 'nope'");
    ecs_os_free(out);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_capture_no_active_camera(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "capture", &error);
    test_bool(error, true);
    test_str(out, "error: no active camera");
    ecs_os_free(out);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_capture_view_root_of_surface(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {-10, 0, 5});
    ecs_set(world, a, Camera, {.fov = 60});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, {10, 0, 5});
    ecs_set(world, b, Camera, {.fov = 60});
    ecs_entity_t view_a = ecs_new(world);
    ecs_set(world, view_a, View, {.camera = a});
    ecs_entity_t root = ecs_new(world);
    ecs_entity_t view_b = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, view_b, View, {.camera = b});
    ecs_entity_t red = ecs_new(world);
    ecs_set(world, red, Position, {-10, 0, 0});
    ecs_set(world, red, Box, {2, 2, 2});
    ecs_set(world, red, Rgb, {0, 0, 0});
    ecs_set(world, red, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, red, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t green = ecs_new(world);
    ecs_set(world, green, Position, {10, 0, 0});
    ecs_set(world, green, Box, {2, 2, 2});
    ecs_set(world, green, Rgb, {0, 0, 0});
    ecs_set(world, green, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, green, Emissive, {.strength = 1, .color = {0, 255, 0}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 16, .height = 16, .view_root = root});
    ecs_entity_t s = box_terminal_new(world);
    ecs_progress(world, 0);

    ecs_os_free(box_terminal_run(world, s, "capture", NULL));
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/resources/capture_0.png",
        ecs_get(world, s, TerminalStatus)->dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(8 * 16 + 8) * 4] < 40);
    test_assert(px[(8 * 16 + 8) * 4 + 1] > 200);
    test_assert(view_a != 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Render_render_prefab_private_scene(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t prefab = ecs_entity(world, { .name = "Crate" });
    ecs_add_id(world, prefab, EcsPrefab);
    ecs_set(world, prefab, Position, {0, 0, 0});
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_set(world, prefab, Rgb, {0, 0, 0});
    ecs_set(world, prefab, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, prefab, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t lid = ecs_entity(world, { .name = "Crate.lid" });
    ecs_set(world, lid, Position, {0, 1, 0});
    ecs_set(world, lid, Sphere, {.radius = 0.5f});
    ecs_set(world, lid, Rgb, {0, 0, 0});
    ecs_set(world, lid, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, lid, Emissive, {.strength = 1, .color = {0, 255, 0}});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, 0, 0});
    ecs_set(world, floor, Box, {10, 1, 10});
    ecs_set(world, floor, Rgb, {0, 0, 0});
    ecs_set(world, floor, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, floor, Emissive, {.strength = 1, .color = {0, 0, 255}});
    ecs_entity_t s = box_terminal_new(world);

    bool error = true;
    char *out = box_terminal_run(world, s, "render Crate", &error);
    test_bool(error, false);
    test_str(out, "render_0.png (pending)");
    ecs_os_free(out);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/resources/render_0.png",
        ecs_get(world, s, TerminalStatus)->dir);
    int w = 0, h = 0, n = 0, i, red = 0, green = 0, blue = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0; i < w * h; i ++) {
        const unsigned char *c = &px[i * 4];
        red += c[0] > 200 && c[1] < 40 && c[2] < 40;
        green += c[1] > 200 && c[0] < 40 && c[2] < 40;
        blue += c[2] > 200 && c[0] < 40 && c[1] < 40;
    }

    test_assert(red > 0);
    test_assert(green > 0);
    test_int(blue, 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Render_render_scene_deleted_next_frame(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t prefab = ecs_entity(world, { .name = "Crate" });
    ecs_add_id(world, prefab, EcsPrefab);
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_entity_t s = box_terminal_new(world);

    ecs_os_free(box_terminal_run(world, s, "render Crate", NULL));
    ecs_progress(world, 0);
    test_int(ecs_count(world, Scene), 1);

    ecs_progress(world, 0);
    test_int(ecs_count(world, Scene), 0);
    test_assert(ecs_is_alive(world, prefab));
    test_assert(ecs_has(world, prefab, Box));

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_render_template(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    test_assert(ecs_script(world, {
        .code =
            "using box.transform\n"
            "using box.geometry\n"
            "using box.material\n"
            "template Pillar {\n"
            "  prop height: f32 = 4\n"
            "  base {\n"
            "    Position: {0, height / 2, 0}\n"
            "    Box: {1, height, 1}\n"
            "    Rgb: {0, 0, 0}\n"
            "    PbrMaterial: {metallic: 1, roughness: 1}\n"
            "    Emissive: {strength: 1, color: {255, 0, 0}}\n"
            "  }\n"
            "  cap {\n"
            "    Position: {0, height, 0}\n"
            "    Sphere: {radius: 0.5}\n"
            "    Rgb: {0, 0, 0}\n"
            "    PbrMaterial: {metallic: 1, roughness: 1}\n"
            "    Emissive: {strength: 1, color: {0, 255, 0}}\n"
            "  }\n"
            "  top {\n"
            "    Position: {0, height + 1, 0}\n"
            "    Sphere: {radius: 0.5}\n"
            "    Rgb: {0, 0, 0}\n"
            "    PbrMaterial: {metallic: 1, roughness: 1}\n"
            "    Emissive: {strength: 1, color: {0, 0, 255}}\n"
            "  }\n"
            "}\n"
    }) != 0);
    ecs_entity_t s = box_terminal_new(world);

    bool error = true;
    char *out = box_terminal_run(world, s, "render Pillar", &error);
    test_bool(error, false);
    test_str(out, "render_0.png (pending)");
    ecs_os_free(out);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/resources/render_0.png",
        ecs_get(world, s, TerminalStatus)->dir);
    int w = 0, h = 0, n = 0, i, red = 0, green = 0, blue = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0; i < w * h; i ++) {
        const unsigned char *c = &px[i * 4];
        red += c[0] > 200 && c[1] < 40 && c[2] < 40;
        green += c[1] > 200 && c[0] < 40 && c[2] < 40;
        blue += c[2] > 200 && c[0] < 40 && c[1] < 40;
    }

    test_assert(red > 0);
    test_assert(green > 0);
    test_assert(blue > 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Render_render_entity_copy(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t car = ecs_entity(world, { .name = "car" });
    ecs_set(world, car, Position, {5, 0, 0});
    ecs_set(world, car, Box, {2, 1, 1});
    ecs_set(world, car, Rgb, {10, 20, 30});
    ecs_entity_t wheel = ecs_entity(world, { .name = "car.wheel" });
    ecs_set(world, wheel, Position, {1, -0.5f, 0});
    ecs_set(world, wheel, Sphere, {.radius = 0.3f});
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, Box, {1, 1, 1});
    ecs_entity_t s = box_terminal_new(world);
    ecs_progress(world, 0);

    char *out = box_terminal_run(world, s, "render car", NULL);
    test_str(out, "render_0.png (pending)");
    ecs_os_free(out);
    ecs_progress(world, 0);

    ecs_entity_t scene = 0;
    ecs_iter_t sit = ecs_each(world, Scene);
    while (ecs_each_next(&sit)) {
        test_int(sit.count, 1);
        scene = sit.entities[0];
    }

    test_assert(scene != 0);
    ecs_entity_t copy = 0;
    ecs_iter_t it = ecs_children(world, scene);
    while (ecs_children_next(&it)) {
        test_int(it.count, 1);
        copy = it.entities[0];
    }

    test_assert(copy != 0);
    test_assert(copy != car);
    test_assert(ecs_get_name(world, copy) == NULL);
    test_flt(ecs_get(world, copy, Position)->x, 5);
    test_flt(ecs_get(world, copy, Box)->x, 2);
    test_int(ecs_get(world, copy, Rgb)->g, 20);
    test_int(ecs_count_id(world, ecs_childof(copy)), 1);
    test_assert(ecs_is_alive(world, wheel));
    test_assert(ecs_has_pair(world, wheel, EcsChildOf, car));
    test_flt(ecs_get(world, car, Position)->x, 5);
    test_assert(ecs_is_alive(world, other));

    ecs_progress(world, 0);
    test_assert(!ecs_is_alive(world, copy));
    test_assert(ecs_is_alive(world, car));
    test_assert(ecs_is_alive(world, wheel));

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_render_fits_bounds(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t e = ecs_entity(world, { .name = "cube" });
    ecs_set(world, e, Position, {10, 0, 0});
    ecs_set(world, e, Box, {2, 2, 2});
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, e, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t s = box_terminal_new(world);
    ecs_progress(world, 0);

    ecs_os_free(box_terminal_run(world, s, "render cube", NULL));
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/resources/render_0.png",
        ecs_get(world, s, TerminalStatus)->dir);
    int w = 0, h = 0, n = 0, x, y, red = 0, edge = 0;
    double cx = 0, cy = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            const unsigned char *c = &px[(y * w + x) * 4];
            if (c[0] > 200 && c[1] < 40 && c[2] < 40) {
                red ++;
                cx += x;
                cy += y;
                edge += x == 0 || y == 0 || x == w - 1 || y == h - 1;
            }
        }
    }

    test_assert(red > w * h / 10);
    test_int(edge, 0);
    cx /= red;
    cy /= red;
    test_assert(fabs(cx - w / 2.0) < w / 10.0);
    test_assert(fabs(cy - h / 2.0) < h / 10.0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Render_render_unknown(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "render nope", &error);
    test_bool(error, true);
    test_str(out, "error: no entity 'nope'");
    ecs_os_free(out);

    ecs_progress(world, 0);
    test_int(ecs_count(world, Scene), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_render_builtin(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "render /flecs.core", &error);
    test_bool(error, true);
    test_str(out, "error: '/flecs.core' is builtin");
    ecs_os_free(out);
    test_int(ecs_count(world, Scene), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_render_session_closed(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t prefab = ecs_entity(world, { .name = "Crate" });
    ecs_add_id(world, prefab, EcsPrefab);
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_entity_t s = box_terminal_new(world);

    ecs_os_free(box_terminal_run(world, s, "render Crate", NULL));
    ecs_delete(world, s);
    ecs_progress(world, 0);
    test_int(ecs_count(world, Scene), 0);

    ecs_progress(world, 0);
    test_assert(ecs_is_alive(world, prefab));
    test_assert(ecs_has(world, prefab, Box));

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_names_unique(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_entity(world, { .name = "cam" });
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t prefab = ecs_entity(world, { .name = "Crate" });
    ecs_add_id(world, prefab, EcsPrefab);
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_entity_t s = box_terminal_new(world);

    char *out = box_terminal_run(world, s, "capture cam", NULL);
    test_str(out, "capture_0.png (pending)");
    ecs_os_free(out);
    out = box_terminal_run(world, s, "capture cam", NULL);
    test_str(out, "capture_1.png (pending)");
    ecs_os_free(out);
    out = box_terminal_run(world, s, "render Crate", NULL);
    test_str(out, "render_0.png (pending)");
    ecs_os_free(out);
    out = box_terminal_run(world, s, "render Crate", NULL);
    test_str(out, "render_1.png (pending)");
    ecs_os_free(out);

    ecs_fini(world);
    box_render_null_free(null);
}

void Render_gpu_capture_png(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {255, 128, 0, 255}}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 32});
    ecs_entity_t s = box_terminal_new(world);
    ecs_progress(world, 0);

    ecs_os_free(box_terminal_run(world, s, "capture", NULL));
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/resources/capture_0.png",
        ecs_get(world, s, TerminalStatus)->dir);
    int w = 0, h = 0, n = 0, i, wrong = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 64);
    test_int(h, 32);
    for (i = 0; i < w * h; i ++) {
        wrong += px[i * 4] != 255 || px[i * 4 + 1] != 128 || px[i * 4 + 2] != 0 ||
            px[i * 4 + 3] != 255;
    }

    test_int(wrong, 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Render_gpu_capture_first_frame(void) {
    // command/capture_first_frame
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 64});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Box, {2, 2, 2});
    ecs_set(world, shape, Rgb, {0, 0, 0});
    ecs_set(world, shape, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, shape, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t s = box_terminal_new(world);

    ecs_os_free(box_terminal_run(world, s, "capture", NULL));
    char *path = flecs_asprintf("%s/resources/capture_0.png",
        ecs_get(world, s, TerminalStatus)->dir);
    int i;
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 64);
    test_int(h, 64);
    int32_t c = (h / 2 * w + w / 2) * 4;
    test_assert(px[c] > 200);
    test_assert(px[c + 1] < 40);
    test_assert(px[c + 2] < 40);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Render_gpu_render_isolated(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t prefab = ecs_entity(world, { .name = "Ball" });
    ecs_add_id(world, prefab, EcsPrefab);
    ecs_set(world, prefab, Sphere, {.radius = 1, .segments = 24, .smooth = true});
    ecs_set(world, prefab, Rgb, {0, 0, 255});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, 0});
    ecs_set(world, wall, Box, {50, 50, 50});
    ecs_set(world, wall, Rgb, {255, 0, 0});
    ecs_entity_t s = box_terminal_new(world);

    char *out = box_terminal_run(world, s, "render Ball", NULL);
    test_str(out, "render_0.png (pending)");
    ecs_os_free(out);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/resources/render_0.png",
        ecs_get(world, s, TerminalStatus)->dir);
    int w = 0, h = 0, n = 0, i, red = 0, blue = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0; i < w * h; i ++) {
        red += px[i * 4] > 100 && px[i * 4 + 2] < 50;
        blue += px[i * 4 + 2] > 100 && px[i * 4] < 50;
    }

    test_int(red, 0);
    test_assert(blue > w * h / 10);
    int32_t c = (h / 2 * w + w / 2) * 4;
    test_assert(px[c + 2] > 100);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Render_http_resource(void) {
    // command/render
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t prefab = ecs_entity(world, { .name = "Ball" });
    ecs_add_id(world, prefab, EcsPrefab);
    ecs_set(world, prefab, Sphere, {.radius = 1});
    ecs_entity_t s = box_terminal_new(world);

    char *path = flecs_asprintf("/terminal/exec?session=%s&cmd=render%%20Ball",
        ecs_get_name(world, s));
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", path, NULL, &reply), 200);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "render_0.png (pending)");
    ecs_os_free(body);
    ecs_os_free(path);

    ecs_progress(world, 0);
    box_render_sync(world);

    path = flecs_asprintf("/resource/render_0.png?session=%s", ecs_get_name(world, s));
    ecs_http_reply_t res = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", path, NULL, &res), 200);
    int32_t size = ecs_strbuf_written(&res.body);
    body = ecs_strbuf_get(&res.body);
    test_assert(size > 8);
    test_assert(!memcmp(body, "\x89PNG", 4));
    ecs_os_free(body);
    ecs_os_free(path);

    ecs_fini(world);
}
