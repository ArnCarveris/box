/**
 * @file Shapes2D.c
 * @brief Tests for the 2D shapes: their outlines, borders, images, edges and batches.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"
#include "spatial.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Create a white 20x10 Rect at the origin the first time the system runs.
 *
 * @param it The iterator of the system
 */
static void LateRectCreate(
    ecs_iter_t *it)
{
    int32_t *created = it->ctx;
    if (!*created) {
        ecs_entity_t e = ecs_new(it->world);
        ecs_set(it->world, e, Position, {0, 0, 0});
        ecs_set(it->world, e, Rect, {20, 10});
        ecs_set(it->world, e, Rgb, {255, 255, 255});
        *created = 1;
    }
}

void Shapes2D_rect_size(void) {
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t rect = ecs_new(world);
    ecs_set(world, rect, Position, {0, 0, 0});
    ecs_set(world, rect, Rect, {20, 10});
    ecs_set(world, rect, Rgb, {255, 255, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "rect.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/rect.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 32) * 4] > 245);
    test_assert(px[(32 * 64 + 41) * 4] > 245);
    test_assert(px[(32 * 64 + 42) * 4] < 10);
    test_assert(px[(32 * 64 + 22) * 4] > 245);
    test_assert(px[(32 * 64 + 21) * 4] < 10);
    test_assert(px[(27 * 64 + 32) * 4] > 245);
    test_assert(px[(26 * 64 + 32) * 4] < 10);
    test_assert(px[(36 * 64 + 32) * 4] > 245);
    test_assert(px[(37 * 64 + 32) * 4] < 10);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shapes2D_circle_shape(void) {
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t circle = ecs_new(world);
    ecs_set(world, circle, Position, {0, 0, 0});
    ecs_set(world, circle, Circle, {12});
    ecs_set(world, circle, Rgb, {255, 255, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "circle.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/circle.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 32) * 4] > 245);
    test_assert(px[(32 * 64 + 42) * 4] > 245);
    test_assert(px[(32 * 64 + 45) * 4] < 10);
    test_assert(px[(22 * 64 + 32) * 4] > 245);
    test_assert(px[(18 * 64 + 32) * 4] < 10);
    test_assert(px[(23 * 64 + 41) * 4] < 10);
    test_assert(px[(41 * 64 + 23) * 4] < 10);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shapes2D_border_radius_rounds_corners(void) {
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t rect = ecs_new(world);
    ecs_set(world, rect, Position, {0, 0, 0});
    ecs_set(world, rect, Rect, {40, 40});
    ecs_set(world, rect, Rgb, {255, 255, 255});
    ecs_set(world, rect, Border, {.radius = 12});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "corner.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/corner.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 51) * 4] > 245);
    test_assert(px[(13 * 64 + 32) * 4] > 245);
    test_assert(px[(13 * 64 + 51) * 4] < 10);
    test_assert(px[(51 * 64 + 13) * 4] < 10);
    test_assert(px[(16 * 64 + 48) * 4] > 245);
    stbi_image_free(px);

    ecs_remove(world, rect, Border);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(13 * 64 + 51) * 4] > 245);
    test_assert(px[(51 * 64 + 13) * 4] > 245);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shapes2D_border_width_and_color(void) {
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t rect = ecs_new(world);
    ecs_set(world, rect, Position, {0, 0, 0});
    ecs_set(world, rect, Rect, {40, 40});
    ecs_set(world, rect, Rgb, {255, 255, 255});
    ecs_set(world, rect, Border, {.width = 4, .color = {255, 0, 0, 255}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "border.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/border.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] > 245 && c[1] > 245 && c[2] > 245);
    c = &px[(32 * 64 + 47) * 4];
    test_assert(c[0] > 245 && c[1] > 245 && c[2] > 245);
    c = &px[(32 * 64 + 50) * 4];
    test_assert(c[0] > 245 && c[1] < 10 && c[2] < 10);
    c = &px[(13 * 64 + 32) * 4];
    test_assert(c[0] > 245 && c[1] < 10 && c[2] < 10);
    c = &px[(32 * 64 + 53) * 4];
    test_assert(c[0] < 10 && c[1] < 10 && c[2] < 10);
    stbi_image_free(px);

    ecs_set(world, rect, Border, {.width = 10, .color = {0, 0, 255, 255}});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(32 * 64 + 44) * 4];
    test_assert(c[0] < 10 && c[1] < 10 && c[2] > 245);
    c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] > 245 && c[1] > 245 && c[2] > 245);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shapes2D_circle_border_ring(void) {
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t circle = ecs_new(world);
    ecs_set(world, circle, Position, {0, 0, 0});
    ecs_set(world, circle, Circle, {16});
    ecs_set(world, circle, Rgb, {0, 255, 0});
    ecs_set(world, circle, Border, {.width = 4, .radius = 2, .color = {0, 0, 255, 255}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "ring.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/ring.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(32 * 64 + 32) * 4];
    test_assert(c[1] > 245 && c[2] < 10);
    c = &px[(32 * 64 + 46) * 4];
    test_assert(c[1] < 10 && c[2] > 245);
    c = &px[(18 * 64 + 32) * 4];
    test_assert(c[1] < 10 && c[2] > 245);
    c = &px[(32 * 64 + 50) * 4];
    test_assert(c[1] < 10 && c[2] < 10);
    c = &px[(20 * 64 + 44) * 4];
    test_assert(c[1] < 10 && c[2] < 10);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shapes2D_line_segment(void) {
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t line = ecs_new(world);
    ecs_set(world, line, Position, {0, 0, 0});
    ecs_set(world, line, Line, {.x1 = -20, .y1 = -20, .x2 = 20, .y2 = 20, .width = 4});
    ecs_set(world, line, Rgb, {255, 255, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "line.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/line.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(31 * 64 + 32) * 4] > 245);
    test_assert(px[(21 * 64 + 42) * 4] > 245);
    test_assert(px[(41 * 64 + 22) * 4] > 245);
    test_assert(px[(41 * 64 + 42) * 4] < 10);
    test_assert(px[(21 * 64 + 22) * 4] < 10);
    test_assert(px[(7 * 64 + 56) * 4] < 10);
    test_assert(px[(57 * 64 + 7) * 4] < 10);
    test_assert(px[(31 * 64 + 36) * 4] < 10);
    stbi_image_free(px);

    ecs_set(world, line, Line, {.x1 = -20, .y1 = 0.5f, .x2 = 20, .y2 = 0.5f, .width = 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(31 * 64 + 20) * 4] > 245);
    test_assert(px[(31 * 64 + 44) * 4] > 245);
    test_assert(px[(30 * 64 + 32) * 4] < 10);
    test_assert(px[(32 * 64 + 32) * 4] < 10);
    test_assert(px[(31 * 64 + 8) * 4] < 10);
    test_assert(px[(31 * 64 + 56) * 4] < 10);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shapes2D_image_texture_area(void) {
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/quadrants.png", tmp);
    uint8_t texels[4 * 4 * 4];
    int32_t i;
    for (i = 0; i < 16; i ++) {
        bool right = (i % 4) >= 2;
        bool bottom = (i / 4) >= 2;
        texels[i * 4 + 0] = (uint8_t)(right == bottom ? 255 : 0);
        texels[i * 4 + 1] = (uint8_t)(right ? 255 : 0);
        texels[i * 4 + 2] = (uint8_t)(bottom ? 255 : 0);
        texels[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 4, 4, 4, texels, 4 * 4) != 0);

    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t image = ecs_new(world);
    ecs_set(world, image, Position, {0, 0, 0});
    ecs_set(world, image, Rect, {40, 40});
    ecs_set(world, image, Image, {.texture = tex, .u1 = 1, .v1 = 1});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "image.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/image.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(22 * 64 + 22) * 4];
    test_assert(c[0] > 230 && c[1] < 30 && c[2] < 30);
    c = &px[(22 * 64 + 42) * 4];
    test_assert(c[0] < 30 && c[1] > 230 && c[2] < 30);
    c = &px[(42 * 64 + 22) * 4];
    test_assert(c[0] < 30 && c[1] < 30 && c[2] > 230);
    c = &px[(42 * 64 + 42) * 4];
    test_assert(c[0] > 230 && c[1] > 230 && c[2] > 230);
    c = &px[(32 * 64 + 56) * 4];
    test_assert(c[0] < 10 && c[1] < 10 && c[2] < 10);
    stbi_image_free(px);

    ecs_set(world, image, Image, {.texture = tex, .u0 = 0.5f, .v0 = 0, .u1 = 1, .v1 = 0.5f});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] < 30 && c[1] > 230 && c[2] < 30);
    c = &px[(22 * 64 + 22) * 4];
    test_assert(c[0] < 30 && c[1] > 230 && c[2] < 30);
    c = &px[(36 * 64 + 36) * 4];
    test_assert(c[0] < 30 && c[1] > 230 && c[2] < 30);
    stbi_image_free(px);

    ecs_set(world, image, Image, {.texture = tex, .u0 = 0.5f, .v0 = 0.5f, .u1 = 1, .v1 = 1});
    ecs_set(world, image, Rgb, {255, 0, 255});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] > 230 && c[1] < 30 && c[2] > 230);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_os_free(file);
    ecs_fini(world);
}

void Shapes2D_opacity_blends(void) {
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t back = ecs_new(world);
    ecs_set(world, back, Position, {-8, 0, 0});
    ecs_set(world, back, Rect, {20, 40});
    ecs_set(world, back, Rgb, {255, 0, 0});
    ecs_entity_t front = ecs_new(world);
    ecs_set(world, front, Position, {0, 0, 1});
    ecs_set(world, front, Circle, {12});
    ecs_set(world, front, Rgb, {0, 0, 255});
    ecs_set(world, front, Opacity, {0.5f});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "opacity.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/opacity.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(32 * 64 + 28) * 4];
    test_assert(c[0] > 170 && c[0] < 205);
    test_assert(c[2] > 170 && c[2] < 205);
    c = &px[(32 * 64 + 38) * 4];
    test_assert(c[0] < 10);
    test_assert(c[2] > 170 && c[2] < 205);
    c = &px[(14 * 64 + 28) * 4];
    test_assert(c[0] > 245 && c[2] < 10);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shapes2D_depth_with_3d_shapes(void) {
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t front = ecs_new(world);
    ecs_set(world, front, Position, {-10, 0, 2});
    ecs_set(world, front, Rect, {10, 10});
    ecs_set(world, front, Rgb, {255, 255, 255});
    ecs_entity_t behind = ecs_new(world);
    ecs_set(world, behind, Position, {10, 0, -2});
    ecs_set(world, behind, Circle, {5});
    ecs_set(world, behind, Rgb, {255, 255, 255});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, 0});
    ecs_set(world, wall, Box, {40, 40, 1});
    ecs_set(world, wall, Rgb, {0, 0, 0});
    ecs_set(world, wall, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, wall, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "depth.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/depth.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(32 * 64 + 22) * 4];
    test_assert(c[0] > 245 && c[1] > 245 && c[2] > 245);
    c = &px[(32 * 64 + 26) * 4];
    test_assert(c[0] > 245 && c[1] > 245);
    c = &px[(32 * 64 + 27) * 4];
    test_assert(c[0] > 200 && c[1] < 30);
    c = &px[(32 * 64 + 42) * 4];
    test_assert(c[0] > 200 && c[1] < 30);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shapes2D_coplanar_draw_in_order(void) {
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Position, {0, 0, 0});
    ecs_set(world, panel, Rect, {40, 40});
    ecs_set(world, panel, Rgb, {255, 0, 0});
    ecs_entity_t badge = ecs_new(world);
    ecs_set(world, badge, Position, {0, 0, 0});
    ecs_set(world, badge, Circle, {8});
    ecs_set(world, badge, Rgb, {0, 255, 0});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "order.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/order.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] < 10 && c[1] > 245);
    c = &px[(32 * 64 + 46) * 4];
    test_assert(c[0] > 245 && c[1] < 10);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

/** What a turned panel holds in front of it, in the order its children are created */
typedef enum Shapes2D_siblings_t {
    Shapes2DFill,       /**< The green fill alone, the mask of its pixels */
    Shapes2DGroove,     /**< A red groove, then the smaller green fill over it */
    Shapes2DNested,     /**< A red groove with a yellow child, then the green fill over both */
    Shapes2DTextAlone,  /**< A white text alone, the mask of its pixels */
    Shapes2DText,       /**< A red rect, then a white text over it */
    Shapes2DFloatAlone, /**< The green fill floating (LayoutFloat) alone, the mask of its pixels */
    Shapes2DFloat       /**< The floating green fill, then a red groove that it stays over */
} Shapes2D_siblings_t;

/** Add a rect in the plane of its parent.
 *
 * @param world The world
 * @param parent The parent
 * @param x Its x position
 * @param w Its width
 * @param h Its height
 * @param rgb Its color as 0xRRGGBB
 * @return The rect
 */
static ecs_entity_t Shapes2D_child(
    ecs_world_t *world,
    ecs_entity_t parent,
    float x,
    float w,
    float h,
    uint32_t rgb)
{
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, e, Position, {x, 0, 0});
    ecs_set(world, e, Rect, {w, h});
    ecs_set(world, e, Rgb, {(uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb});

    return e;
}

/** Make a world with a perspective camera at the origin and a blue 300x120 panel turned toward
 * it like the panel of ui_world_widgets, scaled with its distance to cover the same part of the
 * image, holding coplanar overlapping children of different sizes like a Slider's groove and fill.
 *
 * @param dist The distance of the panel from the camera
 * @param mode The children of the panel
 * @return The world
 */
static ecs_world_t* Shapes2D_siblings_world(
    float dist,
    Shapes2D_siblings_t mode)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Position, {0, 0, -dist});
    ecs_set(world, panel, Rotation, {-0.35f, 0.95f, 0});
    ecs_set(world, panel, Scale, {0.003f * dist, 0.003f * dist, 0.003f * dist});
    ecs_set(world, panel, Rect, {300, 120});
    ecs_set(world, panel, Rgb, {0, 0, 255});
    bool floats = mode == Shapes2DFloatAlone || mode == Shapes2DFloat;
    if (floats) {
        ecs_entity_t fill = Shapes2D_child(world, panel, -40, 180, 40, 0x00FF00);
        ecs_set(world, fill, LayoutFloat, {0});
    }

    if (mode == Shapes2DGroove || mode == Shapes2DNested || mode == Shapes2DFloat) {
        ecs_entity_t g = Shapes2D_child(world, panel, 0, 260, 40, 0xFF0000);
        if (mode == Shapes2DNested) {
            Shapes2D_child(world, g, -20, 200, 30, 0xFFFF00);
        }
    }

    if (mode == Shapes2DText) {
        Shapes2D_child(world, panel, 0, 280, 110, 0xFF0000);
    }

    if (mode == Shapes2DText || mode == Shapes2DTextAlone) {
        ecs_entity_t label = ecs_new_w_pair(world, EcsChildOf, panel);
        ecs_set(world, label, Position, {0, 0, 0});
        ecs_set(world, label, Text, {.value = "HHH", .size = 60,
            .font = ecs_lookup(world, "box.text.DefaultFont")});
        ecs_set(world, label, Rgb, {255, 255, 255});
    } else if (!floats) {
        Shapes2D_child(world, panel, -40, 180, 40, 0x00FF00);
    }

    return world;
}

/** Render a world into a 160x160 capture and free the world.
 *
 * @param world The world
 * @param file The name of the capture
 * @return The pixels of the capture, freed with stbi_image_free
 */
static unsigned char* Shapes2D_siblings_capture(
    ecs_world_t *world,
    const char *file)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 160, .height = 160,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 160);
    test_int(h, 160);
    ecs_os_free(path);
    ecs_fini(world);

    return px;
}

/** Render a turned panel.
 *
 * @param dist The distance of the panel from the camera
 * @param mode The children of the panel
 * @param file The name of the capture
 * @return The pixels of the 160x160 capture, freed with stbi_image_free
 */
static unsigned char* Shapes2D_siblings_render(
    float dist,
    Shapes2D_siblings_t mode,
    const char *file)
{
    return Shapes2D_siblings_capture(Shapes2D_siblings_world(dist, mode), file);
}

/** Check that the pixels a later element fully covers when drawn alone keep its color when it is
 * drawn over coplanar elements, at a near, a middle and a far distance.
 *
 * @param mask_mode The children of the panel without the elements below
 * @param mode The children of the panel with them
 * @param color The color of the element drawn last
 * @param least The pixels it must cover at least
 */
static void Shapes2D_siblings_check(
    Shapes2D_siblings_t mask_mode,
    Shapes2D_siblings_t mode,
    const unsigned char *color,
    int32_t least)
{
    float distances[3] = { 2, 10, 40 };
    int32_t d, j;
    for (d = 0; d < 3; d ++) {
        unsigned char *mask = Shapes2D_siblings_render(distances[d], mask_mode, "mask.png");
        unsigned char *px = Shapes2D_siblings_render(distances[d], mode, "siblings.png");
        int32_t i, covered = 0, hidden = 0;
        for (i = 0; i < 160 * 160; i ++) {
            bool in = true, shown = true;
            for (j = 0; j < 3; j ++) {
                in &= abs((int)mask[i * 4 + j] - (int)color[j]) <= 10;
                shown &= abs((int)px[i * 4 + j] - (int)color[j]) <= 10;
            }

            covered += in;
            hidden += in && !shown;
        }

        test_assert(covered > least);
        test_int(hidden, 0);
        stbi_image_free(mask);
        stbi_image_free(px);
    }
}

void Shapes2D_coplanar_siblings_in_perspective(void) {
    // ui/shapes_2d
    const unsigned char green[3] = { 0, 255, 0 };
    Shapes2D_siblings_check(Shapes2DFill, Shapes2DGroove, green, 100);
}

void Shapes2D_coplanar_sibling_over_descendants(void) {
    // ui/shapes_2d
    const unsigned char green[3] = { 0, 255, 0 };
    Shapes2D_siblings_check(Shapes2DFill, Shapes2DNested, green, 100);
}

void Shapes2D_coplanar_glyphs_in_perspective(void) {
    // ui/shapes_2d
    const unsigned char white[3] = { 255, 255, 255 };
    Shapes2D_siblings_check(Shapes2DTextAlone, Shapes2DText, white, 10);
}

void Shapes2D_coplanar_float_in_perspective(void) {
    // ui/shapes_2d
    const unsigned char green[3] = { 0, 255, 0 };
    Shapes2D_siblings_check(Shapes2DFloatAlone, Shapes2DFloat, green, 100);
}

/** Render a turned panel at a distance of 2 with an emissive red box on the ray through the
 * center of the image, the center of the green fill.
 *
 * @param z The z of the box, the panel being at -2
 * @return The pixel at the center of the image
 */
static uint32_t Shapes2D_siblings_box(
    float z)
{
    ecs_world_t *world = Shapes2D_siblings_world(2, Shapes2DNested);
    ecs_entity_t box = ecs_new(world);
    ecs_set(world, box, Position, {0, 0, z});
    ecs_set(world, box, Box, {0.2f, 0.2f, 0.2f});
    ecs_set(world, box, Rgb, {0, 0, 0});
    ecs_set(world, box, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, box, Emissive, {.strength = 1, .color = {255, 0, 0}});
    unsigned char *px = Shapes2D_siblings_capture(world, "box.png");
    const unsigned char *c = &px[(80 * 160 + 80) * 4];
    uint32_t result = (uint32_t)c[0] | ((uint32_t)c[1] << 8) | ((uint32_t)c[2] << 16);
    stbi_image_free(px);

    return result;
}

void Shapes2D_coplanar_siblings_depth_with_3d_shapes(void) {
    // ui/shapes_2d
    uint32_t front = Shapes2D_siblings_box(-1.6f);
    test_assert((front & 0xFF) > 200);
    test_assert(((front >> 8) & 0xFF) < 30);

    uint32_t behind = Shapes2D_siblings_box(-2.6f);
    test_assert((behind & 0xFF) < 10);
    test_assert(((behind >> 8) & 0xFF) > 245);
    test_assert(((behind >> 16) & 0xFF) < 10);
}

void Shapes2D_edges_crisp_at_any_scale(void) {
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t circle = ecs_new(world);
    ecs_set(world, circle, Position, {0.3f, 0, 0});
    ecs_set(world, circle, Circle, {1});
    ecs_set(world, circle, Rgb, {255, 255, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 160, .height = 160, .write_to_file = "scale.png",
        .resolution_scale = 1});
    char *path = flecs_asprintf("%s/scale.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    float scales[4] = { 3, 12, 40, 75 };
    int32_t s;
    for (s = 0; s < 4; s ++) {
        ecs_set(world, circle, Scale, {scales[s], scales[s], 1});
        ecs_progress(world, 0);
        box_render_sync(world);

        int w = 0, h = 0, n = 0;
        unsigned char *px = stbi_load(path, &w, &h, &n, 4);
        test_assert(px != NULL);
        int32_t x, partial = 0, full = 0;
        for (x = 0; x < 160; x ++) {
            unsigned char v = px[(80 * 160 + x) * 4];
            if (v >= 235) {
                full ++;
            } else if (v > 20) {
                partial ++;
            }
        }

        test_assert(partial >= 1);
        test_assert(partial <= 4);
        test_assert(full >= (int32_t)(2 * scales[s]) - 3);
        test_assert(full <= (int32_t)(2 * scales[s]) + 1);
        stbi_image_free(px);
    }

    ecs_os_free(path);
    ecs_fini(world);
}

void Shapes2D_edges_crisp_in_perspective(void) {
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t rect = ecs_new(world);
    ecs_set(world, rect, Rotation, {0, 0.9f, 0});
    ecs_set(world, rect, Rect, {1, 1});
    ecs_set(world, rect, Border, {.radius = 0.1f});
    ecs_set(world, rect, Rgb, {255, 255, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 128, .height = 128, .write_to_file = "far.png",
        .resolution_scale = 1});
    char *path = flecs_asprintf("%s/far.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    float distances[3] = { 2, 20, 200 };
    int32_t d;
    for (d = 0; d < 3; d ++) {
        float dist = distances[d];
        ecs_set(world, rect, Position, {0.01f * dist, 0, -dist});
        ecs_set(world, rect, Scale, {0.8f * dist, 0.8f * dist, 1});
        ecs_progress(world, 0);
        box_render_sync(world);

        int w = 0, h = 0, n = 0;
        unsigned char *px = stbi_load(path, &w, &h, &n, 4);
        test_assert(px != NULL);
        int32_t x, partial = 0, full = 0;
        for (x = 0; x < 128; x ++) {
            unsigned char v = px[(64 * 128 + x) * 4];
            if (v >= 235) {
                full ++;
            } else if (v > 20) {
                partial ++;
            }
        }

        test_assert(full >= 30);
        test_assert(partial >= 1);
        test_assert(partial <= 4);
        stbi_image_free(px);
    }

    ecs_os_free(path);
    ecs_fini(world);
}

void Shapes2D_edges_antialiased_when_rotated(void) {
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t rect = ecs_new(world);
    ecs_set(world, rect, Position, {0, 0, 0});
    ecs_set(world, rect, Rotation, {0, 0, 0.3f});
    ecs_set(world, rect, Rect, {80, 40});
    ecs_set(world, rect, Rgb, {255, 255, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 128, .height = 128, .write_to_file = "rotated.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/rotated.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    int32_t x, y, smooth = 0;
    for (x = 44; x < 84; x ++) {
        int32_t partial = 0, full = 0;
        for (y = 0; y < 128; y ++) {
            unsigned char v = px[(y * 128 + x) * 4];
            if (v >= 235) {
                full ++;
            } else if (v > 20) {
                partial ++;
            }
        }

        test_assert(partial <= 4);
        test_assert(full >= 36);
        if (partial >= 2) {
            smooth ++;
        }
    }

    test_assert(smooth >= 20);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shapes2D_changes_update_render(void) {
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t shape = ecs_new(world);
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_set(world, shape, Rect, {10, 10});
    ecs_set(world, shape, Rgb, {255, 255, 255});
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, Position, {-24, 24, 0});
    ecs_set(world, other, Rect, {4, 4});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "change.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/change.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 32) * 4] > 245);
    test_assert(px[(32 * 64 + 44) * 4] < 10);
    stbi_image_free(px);

    ecs_set(world, shape, Rect, {30, 30});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 44) * 4] > 245);
    test_assert(px[(20 * 64 + 44) * 4] > 245);
    stbi_image_free(px);

    ecs_remove(world, shape, Rect);
    ecs_set(world, shape, Circle, {15});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 44) * 4] > 245);
    test_assert(px[(20 * 64 + 44) * 4] < 10);
    stbi_image_free(px);

    ecs_remove(world, shape, Circle);
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 32) * 4] < 10);
    test_assert(px[(8 * 64 + 8) * 4] > 150);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shapes2D_dynamic_shapes(void) {
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t moving = ecs_new(world);
    ecs_add(world, moving, DynamicTransform);
    ecs_set(world, moving, Position, {-16, 0, 0});
    ecs_set(world, moving, Circle, {12});
    ecs_set(world, moving, Rgb, {255, 255, 255});
    ecs_entity_t fading = ecs_new(world);
    ecs_add(world, fading, DynamicMaterial);
    ecs_set(world, fading, Position, {16, 0, 0});
    ecs_set(world, fading, Rect, {20, 20});
    ecs_set(world, fading, Border, {.radius = 10, .width = 3, .color = {0, 0, 255, 255}});
    ecs_set(world, fading, Rgb, {255, 255, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "dynamic.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/dynamic.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 16) * 4] > 245);
    test_assert(px[(23 * 64 + 25) * 4] < 10);
    test_assert(px[(32 * 64 + 48) * 4] > 245);
    test_assert(px[(32 * 64 + 56) * 4 + 2] > 245);
    test_assert(px[(32 * 64 + 56) * 4] < 10);
    test_assert(px[(23 * 64 + 39) * 4 + 2] < 10);
    stbi_image_free(px);

    ecs_set(world, moving, Position, {-16, 12, 0});
    ecs_set(world, fading, Rgb, {255, 0, 0});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(20 * 64 + 16) * 4] > 245);
    test_assert(px[(11 * 64 + 25) * 4] < 10);
    test_assert(px[(43 * 64 + 16) * 4] < 10);
    unsigned char *c = &px[(32 * 64 + 48) * 4];
    test_assert(c[0] > 245 && c[1] < 10);
    test_assert(px[(32 * 64 + 56) * 4 + 2] > 245);
    test_assert(px[(23 * 64 + 39) * 4] < 10);
    test_assert(px[(23 * 64 + 39) * 4 + 2] < 10);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shapes2D_prefab_instance(void) {
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Position, {0, 0, 0});
    ecs_set(world, prefab, Circle, {10});
    ecs_set(world, prefab, Border, {.width = 3, .color = {255, 0, 0, 255}});
    ecs_set(world, prefab, Rgb, {0, 255, 0});
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, inst, Position, {12, 0, 0});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "prefab.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/prefab.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(32 * 64 + 44) * 4];
    test_assert(c[0] < 10 && c[1] > 245);
    c = &px[(32 * 64 + 53) * 4];
    test_assert(c[0] > 245 && c[1] < 10);
    c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] < 10 && c[1] < 10);
    c = &px[(32 * 64 + 20) * 4];
    test_assert(c[0] < 10 && c[1] < 10);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shapes2D_one_batch_for_all_kinds(void) {
    // ui/shapes_2d
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
    for (i = 0; i < 30; i ++) {
        ecs_entity_t rect = ecs_new(world);
        ecs_set(world, rect, Position, {(float)i, 0, 0});
        ecs_set(world, rect, Rect, {1, 2});
        ecs_set(world, rect, Border, {.width = 0.1f, .radius = 0.2f});
        ecs_set(world, rect, Rgb, {(uint8_t)i, 0, 0});
        ecs_entity_t circle = ecs_new(world);
        ecs_set(world, circle, Position, {(float)i, 2, 0});
        ecs_set(world, circle, Circle, {0.5f + (float)i});
        ecs_entity_t line = ecs_new(world);
        ecs_set(world, line, Position, {(float)i, 4, 0});
        ecs_set(world, line, Line, {.x1 = 0, .y1 = 0, .x2 = 1, .y2 = (float)i, .width = 1});
    }

    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&null->last.batches), 1);
    const box_render_batch_t *b = ecs_vec_first(&null->last.batches);
    test_int(b->instance_count, 90);

    ecs_entity_t cube = ecs_new(world);
    ecs_set(world, cube, Position, {0, 0, 0});
    ecs_set(world, cube, Box, {1, 1, 1});
    ecs_entity_t tex = ecs_new(world);
    ecs_entity_t image = ecs_new(world);
    ecs_set(world, image, Position, {0, 0, 0});
    ecs_set(world, image, Rect, {1, 1});
    ecs_set(world, image, Image, {.texture = tex, .u1 = 1, .v1 = 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&null->last.batches), 3);
    int32_t total = 0;
    for (i = 0; i < 3; i ++) {
        b = ecs_vec_get_t(&null->last.batches, box_render_batch_t, i);
        total += (int32_t)b->instance_count;
    }

    test_int(total, 92);

    ecs_fini(world);
    box_render_null_free(null);
}

void Shapes2D_empty_shapes_not_drawn(void) {
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t rect = ecs_new(world);
    ecs_set(world, rect, Rect, {0, 2});
    ecs_entity_t circle = ecs_new(world);
    ecs_set(world, circle, Circle, {0});
    ecs_entity_t line = ecs_new(world);
    ecs_set(world, line, Line, {.x1 = 1, .y1 = 1, .x2 = 1, .y2 = 1, .width = 1});
    ecs_entity_t decorated = ecs_new(world);
    ecs_set(world, decorated, Border, {.width = 2});
    ecs_set(world, decorated, Image, {0});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&null->last.batches), 0);

    ecs_set(world, rect, Rect, {1, 2});
    ecs_set(world, decorated, Circle, {3});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(ecs_vec_count(&null->last.batches), 1);
    test_int(ecs_vec_get_t(&null->last.batches, box_render_batch_t, 0)->instance_count, 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void Shapes2D_bounds_hold_shape(void) {
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t rect = ecs_new(world);
    ecs_set(world, rect, Position, {10, 0, 0});
    ecs_set(world, rect, Rect, {4, 2});
    ecs_entity_t circle = ecs_new(world);
    ecs_set(world, circle, Position, {0, 10, 0});
    ecs_set(world, circle, Circle, {3});
    ecs_entity_t line = ecs_new(world);
    ecs_set(world, line, Position, {0, 0, 5});
    ecs_set(world, line, Line, {.x1 = 20, .y1 = 4, .x2 = 30, .y2 = -6, .width = 2});
    ecs_progress(world, 0);

    vec3 min, max;
    test_assert(box_spatial_bounds(world, rect, NULL, min, max));
    test_flt(min[0], 8);
    test_flt(max[0], 12);
    test_flt(min[1], -1);
    test_flt(max[1], 1);
    test_assert(box_spatial_bounds(world, circle, NULL, min, max));
    test_flt(min[0], -3);
    test_flt(max[0], 3);
    test_flt(min[1], 7);
    test_flt(max[1], 13);
    test_assert(box_spatial_bounds(world, line, NULL, min, max));
    test_assert(min[0] <= 19 && max[0] >= 31);
    test_assert(min[1] <= -7 && max[1] >= 5);
    test_assert(min[2] <= 5 && max[2] >= 5);

    ecs_set(world, line, Line, {.x1 = -50, .y1 = 0, .x2 = -40, .y2 = 0, .width = 2});
    ecs_progress(world, 0);
    test_assert(box_spatial_bounds(world, line, NULL, min, max));
    test_assert(min[0] <= -51 && max[0] >= -39);
    test_assert(min[1] <= -1 && max[1] >= 1);

    ecs_fini(world);
}

void Shapes2D_tint_applies(void) {
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_set(world, parent, Tint, {0, 0, 255, 255});
    ecs_entity_t rect = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, rect, Position, {-12, 0, 0});
    ecs_set(world, rect, Rect, {16, 16});
    ecs_set(world, rect, Rgb, {255, 255, 255});
    ecs_set(world, rect, Border, {.width = 3, .color = {255, 0, 0, 255}});
    ecs_entity_t circle = ecs_new(world);
    ecs_set(world, circle, Position, {12, 0, 0});
    ecs_set(world, circle, Circle, {8});
    ecs_set(world, circle, Rgb, {255, 255, 255});
    ecs_set(world, circle, Tint, {0, 255, 0, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "tint.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/tint.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(32 * 64 + 20) * 4];
    test_assert(c[0] < 10 && c[1] < 10 && c[2] > 245);
    c = &px[(32 * 64 + 44) * 4];
    test_assert(c[0] < 10 && c[1] > 245 && c[2] < 10);
    c = &px[(37 * 64 + 44) * 4];
    test_assert(c[0] < 10 && c[1] > 245 && c[2] < 10);
    c = &px[(32 * 64 + 13) * 4];
    test_assert(c[0] < 10 && c[1] < 10 && c[2] > 245);
    ecs_set(world, parent, Tint, {0, 0, 255, 0});
    stbi_image_free(px);

    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(32 * 64 + 20) * 4];
    test_assert(c[0] > 245 && c[1] > 245 && c[2] > 245);
    c = &px[(32 * 64 + 13) * 4];
    test_assert(c[0] > 245 && c[1] < 10 && c[2] < 10);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shapes2D_culled_by_view(void) {
    // ui/shapes_2d
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = true, .occlusion = true, .min_dimension_px = 1},
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t rect = ecs_new(world);
    ecs_set(world, rect, Position, {0, 10, 0});
    ecs_set(world, rect, Rect, {10, 4});
    ecs_set(world, rect, Rgb, {255, 255, 255});
    ecs_entity_t line = ecs_new(world);
    ecs_set(world, line, Position, {100, 0, 0});
    ecs_set(world, line, Line, {.x1 = -110, .y1 = -10, .x2 = -90, .y2 = -10, .width = 2});
    ecs_set(world, line, Rgb, {255, 255, 255});
    ecs_entity_t away = ecs_new(world);
    ecs_set(world, away, Position, {80, 0, 0});
    ecs_set(world, away, Circle, {5});
    ecs_entity_t far_line = ecs_new(world);
    ecs_set(world, far_line, Position, {60, 60, 0});
    ecs_set(world, far_line, Line, {.x1 = -2, .y1 = -2, .x2 = 2, .y2 = 2, .width = 1});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "culled.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 4);
    test_int(stats.visible, 2);

    char *path = flecs_asprintf("%s/culled.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(21 * 64 + 32) * 4] > 245);
    test_assert(px[(41 * 64 + 25) * 4] > 245);
    test_assert(px[(42 * 64 + 39) * 4] > 245);
    test_assert(px[(41 * 64 + 45) * 4] < 10);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shapes2D_created_in_post_update(void) {
    // renderer/late_shape
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "late_post_update.png",
        .resolution_scale = 1});
    int32_t created = 0;
    ecs_system(world, {
        .phase = EcsPostUpdate,
        .callback = LateRectCreate,
        .ctx = &created,
        .immediate = true
    });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(created, 1);

    char *path = flecs_asprintf("%s/late_post_update.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 32) * 4] > 245);
    test_assert(px[(32 * 64 + 41) * 4] > 245);
    test_assert(px[(32 * 64 + 42) * 4] < 10);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shapes2D_created_in_pre_store(void) {
    // renderer/late_shape
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "late_pre_store.png",
        .resolution_scale = 1});
    int32_t created = 0;
    ecs_system(world, {
        .phase = EcsPreStore,
        .callback = LateRectCreate,
        .ctx = &created,
        .immediate = true
    });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(created, 1);

    char *path = flecs_asprintf("%s/late_pre_store.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 32) * 4] > 245);
    test_assert(px[(32 * 64 + 41) * 4] > 245);
    test_assert(px[(32 * 64 + 42) * 4] < 10);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Shapes2D_created_in_on_store(void) {
    // renderer/late_shape
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "late_on_store.png",
        .resolution_scale = 1});
    int32_t created = 0;
    ecs_system(world, {
        .phase = EcsOnStore,
        .callback = LateRectCreate,
        .ctx = &created,
        .immediate = true
    });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(created, 1);

    char *path = flecs_asprintf("%s/late_on_store.png",
        ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(32 * 64 + 32) * 4] > 245);
    test_assert(px[(32 * 64 + 41) * 4] > 245);
    test_assert(px[(32 * 64 + 42) * 4] < 10);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

/** Make a world with an ortho camera of one unit per pixel, a white background and a 64x64
 * capture.
 *
 * @param file The name of the capture
 * @param culling Whether the view culls
 * @return The world
 */
static ecs_world_t* Shapes2D_shadow_world(
    const char *file,
    bool culling)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    if (culling) {
        ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    }

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set_name(world, view, "shadow_view");
    ecs_set(world, view, View, {.camera = camera,
        .culling = {.enabled = culling, .occlusion = culling, .min_dimension_px = 1},
        .clear = {.enabled = true, .color = {255, 255, 255, 255}}});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});

    return world;
}

/** Render a frame of a world made by Shapes2D_shadow_world and read its capture.
 *
 * @param world The world
 * @param file The name of the capture
 * @return The pixels, freed with stbi_image_free
 */
static unsigned char* Shapes2D_shadow_capture(
    ecs_world_t *world,
    const char *file)
{
    ecs_progress(world, 0);
    box_render_sync(world);
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 64);
    test_int(h, 64);
    ecs_os_free(path);

    return px;
}

/** Get a pixel of a 64x64 capture.
 *
 * @param px The pixels
 * @param x The column
 * @param y The row, 0 at the top
 * @return The RGBA of the pixel
 */
static const unsigned char* Shapes2D_at(
    const unsigned char *px,
    int32_t x,
    int32_t y)
{
    return &px[(y * 64 + x) * 4];
}

/** Add a white rect with a shadow.
 *
 * @param world The world
 * @param parent The parent, 0 for none
 * @param x Its x position
 * @param y Its y position
 * @param size Its width and height
 * @param shadow Its shadow
 * @return The rect
 */
static ecs_entity_t Shapes2D_shadowed(
    ecs_world_t *world,
    ecs_entity_t parent,
    float x,
    float y,
    float size,
    BoxShadow shadow)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Position, {x, y, 0});
    ecs_set(world, e, Rect, {size, size});
    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_set_ptr(world, e, BoxShadow, &shadow);

    return e;
}

/** Count the instances the last frame of a null backend drew.
 *
 * @param null The null backend
 * @return The sum of the instances of its draws
 */
static int32_t Shapes2D_drawn(
    const box_render_null_t *null)
{
    int32_t i, result = 0;
    for (i = 0; i < ecs_vec_count(&null->last.batches); i ++) {
        result += (int32_t)ecs_vec_get_t(
            &null->last.batches, box_render_batch_t, i)->instance_count;
    }

    return result;
}

void Shapes2D_box_shadow_darkens_and_falls_off(void) {
    // ui/box_shadow
    ecs_world_t *world = Shapes2D_shadow_world("shadow_falloff.png", false);
    Shapes2D_shadowed(world, 0, 0, 0, 20,
        (BoxShadow){.blur = 8, .color = {0, 0, 0, 255}});
    unsigned char *px = Shapes2D_shadow_capture(world, "shadow_falloff.png");
    const unsigned char *c = Shapes2D_at(px, 32, 32);
    test_assert(c[0] > 245 && c[1] > 245 && c[2] > 245);
    c = Shapes2D_at(px, 41, 32);
    test_assert(c[0] > 245);
    int32_t near_ = Shapes2D_at(px, 42, 32)[0];
    int32_t mid = Shapes2D_at(px, 45, 32)[0];
    int32_t far_ = Shapes2D_at(px, 48, 32)[0];
    test_assert(near_ < 220);
    test_assert(near_ < mid);
    test_assert(mid < far_);
    test_assert(Shapes2D_at(px, 59, 32)[0] > 250);
    test_assert(Shapes2D_at(px, 32, 59)[0] > 250);
    test_assert(Shapes2D_at(px, 21, 32)[0] < 220);
    test_assert(Shapes2D_at(px, 32, 21)[0] < 220);
    test_assert(Shapes2D_at(px, 32, 42)[0] < 220);
    test_assert(abs((int)Shapes2D_at(px, 21, 32)[0] - near_) <= 3);
    stbi_image_free(px);

    ecs_fini(world);
}

void Shapes2D_box_shadow_offset_spread_edges(void) {
    // ui/box_shadow
    ecs_world_t *world = Shapes2D_shadow_world("shadow_edges.png", false);
    ecs_entity_t e = Shapes2D_shadowed(world, 0, 0, 0, 20, (BoxShadow){
        .offset_x = 6, .offset_y = -6, .spread = 2, .color = {0, 0, 0, 255}});
    unsigned char *px = Shapes2D_shadow_capture(world, "shadow_edges.png");
    test_assert(Shapes2D_at(px, 32, 32)[0] > 245);
    test_assert(Shapes2D_at(px, 48, 45)[0] < 10);
    test_assert(Shapes2D_at(px, 51, 45)[0] > 250);
    test_assert(Shapes2D_at(px, 49, 45)[0] < Shapes2D_at(px, 50, 45)[0]);
    test_assert(Shapes2D_at(px, 45, 48)[0] < 10);
    test_assert(Shapes2D_at(px, 45, 51)[0] > 250);
    test_assert(Shapes2D_at(px, 27, 45)[0] < 10);
    test_assert(Shapes2D_at(px, 24, 45)[0] > 250);
    test_assert(Shapes2D_at(px, 45, 16)[0] > 250);
    test_assert(Shapes2D_at(px, 45, 27)[0] < 10);
    stbi_image_free(px);

    ecs_set(world, e, BoxShadow, {.spread = 6, .color = {0, 0, 0, 255}});
    px = Shapes2D_shadow_capture(world, "shadow_edges.png");
    test_assert(Shapes2D_at(px, 46, 17)[0] < 10);
    test_assert(Shapes2D_at(px, 17, 46)[0] < 10);
    test_assert(Shapes2D_at(px, 49, 32)[0] > 250);
    stbi_image_free(px);

    ecs_set(world, e, Border, {.radius = 8});
    px = Shapes2D_shadow_capture(world, "shadow_edges.png");
    test_assert(Shapes2D_at(px, 47, 16)[0] > 245);
    test_assert(Shapes2D_at(px, 16, 47)[0] > 245);
    test_assert(Shapes2D_at(px, 42, 21)[0] < 40);
    test_assert(Shapes2D_at(px, 46, 31)[0] < 10);
    stbi_image_free(px);

    ecs_set(world, e, BoxShadow, {.spread = -4, .color = {0, 0, 0, 255}});
    ecs_set(world, e, Rgb, {255, 0, 0});
    px = Shapes2D_shadow_capture(world, "shadow_edges.png");
    const unsigned char *c = Shapes2D_at(px, 32, 32);
    test_assert(c[0] > 245 && c[1] < 10);
    test_assert(Shapes2D_at(px, 43, 32)[1] > 250);
    stbi_image_free(px);

    ecs_set(world, e, BoxShadow, {.spread = -10, .color = {0, 0, 0, 255}});
    ecs_set(world, e, Opacity, {0.5f});
    px = Shapes2D_shadow_capture(world, "shadow_edges.png");
    c = Shapes2D_at(px, 32, 32);
    test_assert(c[0] > 245 && c[1] > 170 && c[1] < 205);
    stbi_image_free(px);

    ecs_fini(world);
}

void Shapes2D_box_shadow_under_element(void) {
    // ui/box_shadow
    ecs_world_t *world = Shapes2D_shadow_world("shadow_under.png", false);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rect, {21, 21});
    ecs_set(world, e, Rgb, {255, 0, 0});
    ecs_set(world, e, BoxShadow, {.blur = 4, .color = {0, 0, 0, 255}});
    unsigned char *px = Shapes2D_shadow_capture(world, "shadow_under.png");
    const unsigned char *c = Shapes2D_at(px, 41, 32);
    test_assert(c[0] > 245 && c[1] < 10 && c[2] < 10);
    c = Shapes2D_at(px, 32, 32);
    test_assert(c[0] > 245 && c[1] < 10 && c[2] < 10);
    c = Shapes2D_at(px, 42, 32);
    test_assert(c[0] > 210 && c[1] > 90 && c[1] < 180);
    test_assert(Shapes2D_at(px, 43, 32)[1] < 230);
    test_assert(Shapes2D_at(px, 43, 32)[1] > Shapes2D_at(px, 42, 32)[1]);
    stbi_image_free(px);

    ecs_set(world, e, BoxShadow, {.offset_x = 6, .color = {0, 0, 0, 255}});
    ecs_set(world, e, BlendMode, {Additive});
    px = Shapes2D_shadow_capture(world, "shadow_under.png");
    c = Shapes2D_at(px, 32, 32);
    test_assert(c[0] > 245 && c[1] > 245 && c[2] > 245);
    c = Shapes2D_at(px, 39, 25);
    test_assert(c[0] > 245 && c[1] > 245 && c[2] > 245);
    c = Shapes2D_at(px, 46, 32);
    test_assert(c[0] < 10 && c[1] < 10 && c[2] < 10);
    c = Shapes2D_at(px, 52, 32);
    test_assert(c[0] > 250 && c[1] > 250 && c[2] > 250);
    stbi_image_free(px);

    ecs_fini(world);
}

/** Test that two 64x64 captures are the same over an area.
 *
 * @param a The first capture
 * @param b The second capture
 * @param x0 First column
 * @param y0 First row
 * @param x1 Last column
 * @param y1 Last row
 */
static void Shapes2D_same(
    const unsigned char *a,
    const unsigned char *b,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1)
{
    int32_t x, y, i;
    for (y = y0; y <= y1; y ++) {
        for (x = x0; x <= x1; x ++) {
            for (i = 0; i < 3; i ++) {
                test_int(Shapes2D_at(a, x, y)[i], Shapes2D_at(b, x, y)[i]);
            }
        }
    }
}

void Shapes2D_box_shadow_cutout_translucent(void) {
    // ui/box_shadow
    ecs_world_t *world = Shapes2D_shadow_world("shadow_cutout.png", false);
    ecs_entity_t e = Shapes2D_shadowed(world, 0, 0, 0, 20,
        (BoxShadow){.blur = 6, .spread = 2, .color = {0, 0, 0, 255}});
    ecs_set(world, e, Rgb, {255, 0, 0});
    ecs_set(world, e, Opacity, {0.5f});
    unsigned char *shadowed = Shapes2D_shadow_capture(world, "shadow_cutout.png");
    ecs_remove(world, e, BoxShadow);
    unsigned char *plain = Shapes2D_shadow_capture(world, "shadow_cutout.png");
    const unsigned char *c = Shapes2D_at(plain, 32, 32);
    test_assert(c[0] > 245 && c[1] > 170 && c[1] < 205);
    Shapes2D_same(shadowed, plain, 23, 23, 40, 40);
    test_assert(Shapes2D_at(plain, 43, 32)[0] > 250);
    test_assert(Shapes2D_at(shadowed, 43, 32)[0] < 230);
    test_assert(Shapes2D_at(shadowed, 32, 43)[0] < 230);
    test_assert(Shapes2D_at(shadowed, 20, 32)[0] < 230);
    stbi_image_free(shadowed);
    stbi_image_free(plain);

    ecs_set(world, e, Rgb, {255, 255, 255});
    ecs_set(world, e, Opacity, {0.25f});
    ecs_remove(world, e, Rect);
    ecs_set(world, e, Circle, {10});
    ecs_set(world, e, BoxShadow, {.blur = 4, .color = {0, 0, 0, 255}});
    shadowed = Shapes2D_shadow_capture(world, "shadow_cutout.png");
    test_assert(Shapes2D_at(shadowed, 32, 32)[0] > 250);
    test_assert(Shapes2D_at(shadowed, 26, 32)[0] > 250);
    test_assert(Shapes2D_at(shadowed, 32, 38)[0] > 250);
    test_assert(Shapes2D_at(shadowed, 42, 32)[0] < 245);
    test_assert(Shapes2D_at(shadowed, 42, 32)[0] < Shapes2D_at(shadowed, 44, 32)[0]);
    stbi_image_free(shadowed);

    ecs_fini(world);
}

void Shapes2D_box_shadow_cutout_offset(void) {
    // ui/box_shadow
    ecs_world_t *world = Shapes2D_shadow_world("shadow_cutout_offset.png", false);
    ecs_entity_t e = Shapes2D_shadowed(world, 0, 0, 0, 20, (BoxShadow){
        .offset_x = 8, .offset_y = -8, .color = {0, 0, 0, 255}});
    ecs_set(world, e, Rgb, {0, 0, 255});
    ecs_set(world, e, Opacity, {0.5f});
    unsigned char *shadowed = Shapes2D_shadow_capture(world, "shadow_cutout_offset.png");
    ecs_remove(world, e, BoxShadow);
    unsigned char *plain = Shapes2D_shadow_capture(world, "shadow_cutout_offset.png");
    Shapes2D_same(shadowed, plain, 23, 23, 40, 40);
    Shapes2D_same(shadowed, plain, 0, 0, 63, 20);
    Shapes2D_same(shadowed, plain, 0, 0, 20, 63);
    const unsigned char *c = Shapes2D_at(shadowed, 46, 38);
    test_assert(c[0] > 175 && c[0] < 200 && c[1] == c[0] && c[2] == c[0]);
    test_assert(abs((int)Shapes2D_at(shadowed, 38, 46)[0] - c[0]) <= 2);
    test_assert(abs((int)Shapes2D_at(shadowed, 46, 46)[0] - c[0]) <= 2);
    test_assert(abs((int)Shapes2D_at(shadowed, 42, 31)[0] - c[0]) <= 2);
    test_assert(abs((int)Shapes2D_at(shadowed, 31, 42)[0] - c[0]) <= 2);
    c = Shapes2D_at(shadowed, 53, 40);
    test_assert(c[0] > 250 && c[1] > 250 && c[2] > 250);
    c = Shapes2D_at(shadowed, 40, 53);
    test_assert(c[0] > 250 && c[1] > 250 && c[2] > 250);
    stbi_image_free(shadowed);
    stbi_image_free(plain);

    ecs_set(world, e, BoxShadow, {.offset_x = 8, .offset_y = -8, .blur = 6,
        .color = {0, 0, 0, 255}});
    shadowed = Shapes2D_shadow_capture(world, "shadow_cutout_offset.png");
    ecs_remove(world, e, BoxShadow);
    plain = Shapes2D_shadow_capture(world, "shadow_cutout_offset.png");
    Shapes2D_same(shadowed, plain, 23, 23, 40, 40);
    test_assert(Shapes2D_at(shadowed, 46, 46)[0] < 225);
    test_assert(Shapes2D_at(plain, 46, 46)[0] > 250);
    test_assert(Shapes2D_at(shadowed, 46, 40)[0] < Shapes2D_at(shadowed, 49, 40)[0]);
    stbi_image_free(shadowed);
    stbi_image_free(plain);

    ecs_fini(world);
}

/** Test that a channel of the pixels of a capture from a point outward goes from the inside of a
 * translucent element to its shadow with no light or dark seam: every step toward the shadow, and
 * all within the inside and the shadow.
 *
 * @param px The capture
 * @param channel The channel
 * @param x Column of the first pixel, inside the element
 * @param y Row of the first pixel
 * @param dx Column step
 * @param dy Row step
 * @param count Pixels to test, the last one in the shadow outside the element
 */
static void Shapes2D_no_seam(
    const unsigned char *px,
    int32_t channel,
    int32_t x,
    int32_t y,
    int32_t dx,
    int32_t dy,
    int32_t count)
{
    int32_t inside = Shapes2D_at(px, x, y)[channel];
    int32_t outside = Shapes2D_at(px, x + dx * (count - 1), y + dy * (count - 1))[channel];
    int32_t dir = outside > inside ? 1 : -1;
    int32_t lo = glm_min(inside, outside), hi = glm_max(inside, outside);
    int32_t last = inside, i;
    test_assert(abs(inside - outside) > 10);
    for (i = 0; i < count; i ++) {
        int32_t v = Shapes2D_at(px, x + dx * i, y + dy * i)[channel];
        test_assert((v - last) * dir >= -2);
        test_assert(v >= lo - 2 && v <= hi + 2);
        last = v;
    }
}

void Shapes2D_box_shadow_cutout_no_seam(void) {
    // ui/box_shadow
    ecs_world_t *world = Shapes2D_shadow_world("shadow_seam.png", false);
    ecs_entity_t e = Shapes2D_shadowed(world, 0, 0, 0, 30,
        (BoxShadow){.spread = 6, .color = {0, 0, 0, 160}});
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, Opacity, {0.5f});
    ecs_set(world, e, Border, {.radius = 10});
    unsigned char *px = Shapes2D_shadow_capture(world, "shadow_seam.png");
    Shapes2D_no_seam(px, 0, 32, 32, 1, 0, 18);
    Shapes2D_no_seam(px, 0, 32, 32, -1, 0, 18);
    Shapes2D_no_seam(px, 0, 32, 32, 0, 1, 18);
    Shapes2D_no_seam(px, 0, 32, 32, 0, -1, 18);
    Shapes2D_no_seam(px, 0, 36, 28, 1, -1, 11);
    Shapes2D_no_seam(px, 0, 28, 36, -1, 1, 11);
    stbi_image_free(px);

    ecs_set(world, e, Opacity, {0.8f});
    ecs_set(world, e, Rgb, {255, 0, 0});
    ecs_set(world, e, BoxShadow, {.spread = 4, .blur = 2, .color = {0, 0, 0, 200}});
    px = Shapes2D_shadow_capture(world, "shadow_seam.png");
    Shapes2D_no_seam(px, 1, 36, 28, 1, -1, 10);
    Shapes2D_no_seam(px, 1, 32, 32, 1, 0, 16);
    Shapes2D_no_seam(px, 1, 28, 36, -1, 1, 10);
    stbi_image_free(px);

    ecs_set(world, e, Opacity, {1.0f});
    ecs_set(world, e, Rgb, {0, 0, 0});
    ecs_set(world, e, BoxShadow, {.spread = 4, .color = {0, 0, 0, 255}});
    px = Shapes2D_shadow_capture(world, "shadow_seam.png");
    int32_t i;
    for (i = 0; i < 10; i ++) {
        test_assert(Shapes2D_at(px, 36 + i, 28 - i)[0] < 6);
        test_assert(Shapes2D_at(px, 40 + i, 32)[0] < 6);
    }
    stbi_image_free(px);

    ecs_fini(world);
}

void Shapes2D_box_shadow_sibling_order(void) {
    // ui/box_shadow
    ecs_world_t *world = Shapes2D_shadow_world("shadow_siblings.png", false);
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Position, {0, 0, 0});
    ecs_entity_t early = ecs_new_w_pair(world, EcsChildOf, panel);
    ecs_set(world, early, Position, {-12, 0, 0});
    ecs_set(world, early, Rect, {16, 40});
    ecs_set(world, early, Rgb, {0, 255, 0});
    Shapes2D_shadowed(world, panel, 0, 0, 12,
        (BoxShadow){.spread = 10, .color = {0, 0, 0, 255}});
    ecs_entity_t late = ecs_new_w_pair(world, EcsChildOf, panel);
    ecs_set(world, late, Position, {12, 0, 0});
    ecs_set(world, late, Rect, {16, 40});
    ecs_set(world, late, Rgb, {255, 0, 0});
    unsigned char *px = Shapes2D_shadow_capture(world, "shadow_siblings.png");
    const unsigned char *c = Shapes2D_at(px, 17, 32);
    test_assert(c[1] < 10);
    c = Shapes2D_at(px, 17, 13);
    test_assert(c[0] < 10 && c[1] > 245);
    c = Shapes2D_at(px, 46, 32);
    test_assert(c[0] > 245 && c[1] < 10 && c[2] < 10);
    c = Shapes2D_at(px, 32, 32);
    test_assert(c[0] > 245 && c[1] > 245 && c[2] > 245);
    stbi_image_free(px);

    ecs_fini(world);
}

void Shapes2D_box_shadow_clipped(void) {
    // ui/box_shadow
    ecs_world_t *world = Shapes2D_shadow_world("shadow_clip.png", false);
    ecs_entity_t clip = ecs_new(world);
    ecs_set(world, clip, Position, {0, 0, 0});
    ecs_set(world, clip, Rect, {20, 20});
    ecs_set(world, clip, Rgb, {255, 255, 255});
    ecs_add_id(world, clip, ClipChildren);
    Shapes2D_shadowed(world, clip, 0, 0, 10,
        (BoxShadow){.spread = 10, .color = {0, 0, 0, 255}});
    unsigned char *px = Shapes2D_shadow_capture(world, "shadow_clip.png");
    test_assert(Shapes2D_at(px, 32, 32)[0] > 245);
    test_assert(Shapes2D_at(px, 40, 32)[0] < 10);
    test_assert(Shapes2D_at(px, 32, 23)[0] < 10);
    test_assert(Shapes2D_at(px, 44, 32)[0] > 250);
    test_assert(Shapes2D_at(px, 32, 44)[0] > 250);
    test_assert(Shapes2D_at(px, 19, 19)[0] > 250);
    stbi_image_free(px);

    ecs_fini(world);
}

void Shapes2D_box_shadow_past_view_edge(void) {
    // ui/box_shadow
    ecs_world_t *world = Shapes2D_shadow_world("shadow_edge.png", true);
    Shapes2D_shadowed(world, 0, 40, 0, 10,
        (BoxShadow){.offset_x = -20, .blur = 4, .color = {0, 0, 0, 255}});
    Shapes2D_shadowed(world, 0, -36, 0, 20,
        (BoxShadow){.blur = 16, .color = {0, 0, 0, 255}});
    unsigned char *px = Shapes2D_shadow_capture(world, "shadow_edge.png");
    ecs_entity_t view = ecs_lookup(world, "shadow_view");
    box_render_cull_stats_t stats;
    test_bool(box_render_gpu_cull_stats(world, view, &stats), true);
    test_int(stats.tested, 4);
    test_int(stats.visible, 3);
    test_assert(Shapes2D_at(px, 52, 32)[0] < 40);
    test_assert(Shapes2D_at(px, 62, 32)[0] > 250);
    test_assert(Shapes2D_at(px, 0, 32)[0] > 245);
    test_assert(Shapes2D_at(px, 8, 32)[0] < 220);
    test_assert(Shapes2D_at(px, 8, 32)[0] < Shapes2D_at(px, 14, 32)[0]);
    stbi_image_free(px);

    ecs_fini(world);
}

void Shapes2D_box_shadow_needs_rect_or_circle(void) {
    // ui/box_shadow
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t line = ecs_new(world);
    ecs_set(world, line, Position, {0, 0, -10});
    ecs_set(world, line, Line, {.x1 = 0, .y1 = 0, .x2 = 1, .y2 = 1, .width = 1});
    ecs_entity_t label = ecs_new(world);
    ecs_set(world, label, Position, {0, 0, -10});
    ecs_set(world, label, Text, {.value = "HI", .size = 1,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_entity_t bare = ecs_new(world);
    ecs_set(world, bare, Position, {0, 0, -10});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    int32_t plain = Shapes2D_drawn(null);
    test_assert(plain >= 3);

    ecs_set(world, line, BoxShadow, {.blur = 2, .spread = 1});
    ecs_set(world, label, BoxShadow, {.blur = 2, .spread = 1});
    ecs_set(world, bare, BoxShadow, {.blur = 2, .spread = 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(Shapes2D_drawn(null), plain);

    ecs_set(world, line, Circle, {1});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(Shapes2D_drawn(null), plain + 1);

    ecs_remove(world, line, Circle);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(Shapes2D_drawn(null), plain);

    ecs_fini(world);
    box_render_null_free(null);
}

void Shapes2D_box_shadow_slots_freed(void) {
    // ui/box_shadow
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {0, 0, -10});
    ecs_set(world, a, Rect, {1, 1});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, {2, 0, -10});
    ecs_set(world, b, Circle, {1});
    ecs_set(world, b, Opacity, {0.5f});
    ecs_entity_t c = ecs_new(world);
    ecs_set(world, c, Position, {4, 0, -10});
    ecs_set(world, c, Rect, {1, 2});
    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(stats.instances, 3);
    test_int(stats.batches, 2);

    ecs_set(world, a, BoxShadow, {.blur = 1});
    ecs_set(world, b, BoxShadow, {.blur = 1});
    ecs_set(world, c, BoxShadow, {.blur = 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_draws_stats(world, &stats);
    test_int(stats.instances, 6);
    test_int(stats.batches, 3); // the shadows have a batch of their own, apart from b
    test_int(Shapes2D_drawn(null), 6);

    ecs_set(world, a, BoxShadow, {.blur = 2, .spread = 1});
    ecs_set(world, c, BoxShadow, {.spread = -1});
    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_draws_stats(world, &stats);
    test_int(stats.instances, 5);

    ecs_set(world, c, BoxShadow, {.spread = 1});
    ecs_remove(world, a, BoxShadow);
    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_draws_stats(world, &stats);
    test_int(stats.instances, 5);

    ecs_set(world, a, BoxShadow, {.blur = 3});
    ecs_set(world, b, BlendMode, {Additive});
    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_draws_stats(world, &stats);
    test_int(stats.instances, 6);
    test_int(stats.batches, 3);

    ecs_set(world, b, Border, {.width = 0.1f});
    ecs_set(world, c, Rect, {3, 2});
    ecs_remove(world, a, Rect);
    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_draws_stats(world, &stats);
    test_int(stats.instances, 4);
    test_int(Shapes2D_drawn(null), 4);

    ecs_set(world, a, Rect, {1, 1});
    ecs_delete(world, b);
    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_draws_stats(world, &stats);
    test_int(stats.instances, 4);

    ecs_delete(world, a);
    ecs_remove(world, c, BoxShadow);
    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_draws_stats(world, &stats);
    test_int(stats.instances, 1);
    test_int(stats.batches, 1);
    test_int(Shapes2D_drawn(null), 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Shapes2D_box_shadow_own_batch(void) {
    // ui/box_shadow
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t translucent = ecs_new(world);
    ecs_set(world, translucent, Position, {0, 0, -10});
    ecs_set(world, translucent, Rect, {1, 1});
    ecs_set(world, translucent, Opacity, {0.5f});
    ecs_entity_t opaque = ecs_new(world);
    ecs_set(world, opaque, Position, {2, 0, -10});
    ecs_set(world, opaque, Rect, {1, 1});
    ecs_set(world, opaque, BoxShadow, {.blur = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    int32_t i, shadows = 0, blended = 0;
    for (i = 0; i < ecs_vec_count(&null->last.batches); i ++) {
        const box_render_batch_t *b = ecs_vec_get_t(&null->last.batches, box_render_batch_t, i);
        if (b->box_shadow) {
            test_int(b->blend, AlphaBlend);
            test_int(b->instance_count, 1);
            shadows ++;
        } else if (b->blend == AlphaBlend) {
            test_int(b->instance_count, 1);
            blended ++;
        }
    }

    test_int(shadows, 1);
    test_int(blended, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Set up the scene of a case of a shadow that follows its element, in the state before the
 * change or in the state after it.
 *
 * @param world The world
 * @param end Whether to set up the state after the change
 */
typedef void (*Shapes2D_follow_scene_t)(
    ecs_world_t *world,
    bool end);

/** Change the scene of a case of a shadow that follows its element.
 *
 * @param world The world
 * @param frame The frame of the change, from 0
 */
typedef void (*Shapes2D_follow_change_t)(
    ecs_world_t *world,
    int32_t frame);

/** Test that two 64x64 captures differ: a channel of a pixel by more than 40.
 *
 * @param a The first capture
 * @param b The second capture
 * @return Whether they differ
 */
static bool Shapes2D_differ(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t i;
    bool result = false;
    for (i = 0; i < 64 * 64 * 4 && !result; i ++) {
        result = (i % 4) != 3 && abs((int)a[i] - (int)b[i]) > 40;
    }

    return result;
}

/** Test that two 64x64 captures match: every channel of every pixel within 2.
 *
 * @param a The first capture
 * @param b The second capture
 */
static void Shapes2D_close(
    const unsigned char *a,
    const unsigned char *b)
{
    int32_t i, count = 0;
    for (i = 0; i < 64 * 64 * 4; i ++) {
        if ((i % 4) != 3 && abs((int)a[i] - (int)b[i]) > 2) {
            if (!count) {
                printf("first mismatch at %d, %d channel %d: %d != %d\n",
                    (i / 4) % 64, (i / 4) / 64, i % 4, a[i], b[i]);
            }

            count ++;
        }
    }

    test_int(count, 0);
}

/** Test that a shadow follows its element through a change: the pixels after the change match
 * a scene set up in the state after the change from the start, and differ from the pixels
 * before it.
 *
 * @param file The name of the capture
 * @param scene Sets up the scene
 * @param change Makes the change, once per frame
 * @param frames Frames to change and render
 */
static void Shapes2D_follow(
    const char *file,
    Shapes2D_follow_scene_t scene,
    Shapes2D_follow_change_t change,
    int32_t frames)
{
    int32_t f;
    ecs_world_t *world = Shapes2D_shadow_world(file, false);
    scene(world, true);
    unsigned char *end = Shapes2D_shadow_capture(world, file);
    ecs_fini(world);

    world = Shapes2D_shadow_world(file, false);
    scene(world, false);
    unsigned char *px = Shapes2D_shadow_capture(world, file);
    test_assert(Shapes2D_differ(px, end));
    for (f = 0; f < frames; f ++) {
        stbi_image_free(px);
        change(world, f);
        px = Shapes2D_shadow_capture(world, file);
    }

    Shapes2D_close(px, end);
    stbi_image_free(px);
    stbi_image_free(end);
    ecs_fini(world);
}

/** Look up an entity of a follow scene by name.
 *
 * @param world The world
 * @param name The name
 * @return The entity
 */
static ecs_entity_t Shapes2D_named(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t result = ecs_lookup(world, name);
    test_assert(result != 0);

    return result;
}

/** Add a named white rect with an offset soft shadow under a parent.
 *
 * @param world The world
 * @param parent The parent, 0 for none
 * @param name The name of the rect
 * @param x Its x position
 * @param y Its y position
 * @return The rect
 */
static ecs_entity_t Shapes2D_follow_box(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    float x,
    float y)
{
    ecs_entity_t e = Shapes2D_shadowed(world, parent, x, y, 16, (BoxShadow){
        .offset_x = 4, .offset_y = -4, .blur = 4, .spread = 2, .color = {0, 0, 0, 255}});
    ecs_set_name(world, e, name);

    return e;
}

/** Add a named plain parent at a position.
 *
 * @param world The world
 * @param parent Its parent, 0 for none
 * @param name The name
 * @param x Its x position
 * @param y Its y position
 * @return The entity
 */
static ecs_entity_t Shapes2D_follow_parent(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    float x,
    float y)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set_name(world, e, name);
    ecs_set(world, e, Position, {x, y, 0});

    return e;
}

static void Shapes2D_transform_scene(
    ecs_world_t *world,
    bool end)
{
    ecs_entity_t parent = Shapes2D_follow_parent(world, 0, "parent", 0, 0);
    ecs_entity_t box = Shapes2D_follow_box(world, parent, "box", 0, 0);
    if (end) {
        ecs_set(world, parent, Position, {6, -4, 0});
        ecs_set(world, parent, Rotation, {0, 0, 0.3f});
        ecs_set(world, parent, Scale, {1.25f, 1.25f, 1});
        ecs_set(world, box, Position, {2, 3, 0});
    }
}

static void Shapes2D_transform_change(
    ecs_world_t *world,
    int32_t frame)
{
    ecs_entity_t parent = Shapes2D_named(world, "parent");
    if (frame == 0) {
        ecs_set(world, parent, Position, {6, -4, 0});
        ecs_set(world, parent, Rotation, {0, 0, 0.3f});
        ecs_set(world, parent, Scale, {1.25f, 1.25f, 1});
    } else {
        ecs_set(world, Shapes2D_named(world, "parent.box"), Position, {2, 3, 0});
    }
}

void Shapes2D_box_shadow_follows_transform(void) {
    // ui/box_shadow
    Shapes2D_follow("shadow_follow_transform.png", Shapes2D_transform_scene,
        Shapes2D_transform_change, 2);
}

static void Shapes2D_dynamic_transform_scene(
    ecs_world_t *world,
    bool end)
{
    ecs_entity_t parent = Shapes2D_follow_parent(world, 0, "parent", 0, 0);
    ecs_entity_t box = Shapes2D_follow_box(world, parent, "box", 0, 0);
    ecs_add(world, box, DynamicTransform);
    if (end) {
        ecs_set(world, parent, Position, {-3, 2, 0});
        ecs_set(world, box, Position, {8, -5, 0});
        ecs_set(world, box, Rotation, {0, 0, 0.5f});
    }
}

static void Shapes2D_dynamic_transform_change(
    ecs_world_t *world,
    int32_t frame)
{
    ecs_entity_t box = Shapes2D_named(world, "parent.box");
    float x[3] = {-6, 3, 8}, y[3] = {4, -2, -5}, angle[3] = {0.2f, -0.4f, 0.5f};
    if (frame == 0) {
        /* A static parent that moves marks its subtree, so it moves first and the frames after
         * it move the element through the per frame transforms only */
        ecs_set(world, Shapes2D_named(world, "parent"), Position, {-3, 2, 0});
    }

    ecs_set(world, box, Position, {x[frame], y[frame], 0});
    ecs_set(world, box, Rotation, {0, 0, angle[frame]});
}

void Shapes2D_box_shadow_follows_dynamic_transform(void) {
    // ui/box_shadow
    Shapes2D_follow("shadow_follow_dynamic_transform.png", Shapes2D_dynamic_transform_scene,
        Shapes2D_dynamic_transform_change, 3);
}

static void Shapes2D_size_scene(
    ecs_world_t *world,
    bool end)
{
    ecs_entity_t box = Shapes2D_follow_box(world, 0, "box", 0, 0);
    if (end) {
        ecs_set(world, box, Rect, {26, 12});
    }
}

static void Shapes2D_size_change(
    ecs_world_t *world,
    int32_t frame)
{
    (void)frame;
    ecs_set(world, Shapes2D_named(world, "box"), Rect, {26, 12});
}

void Shapes2D_box_shadow_follows_size(void) {
    // ui/box_shadow
    Shapes2D_follow("shadow_follow_size.png", Shapes2D_size_scene, Shapes2D_size_change, 1);
}

/** A panel with a blue 24x24 target and a 16x16 shadowed element over it, which a uniform Fit
 * scales by 1.5 through its ShapeMesh.scale. The end state is a 24x24 element with its shadow
 * scaled by 1.5, which does not fit.
 *
 * @param world The world
 * @param end Whether to set up the state after the change
 */
static void Shapes2D_fit_scene(
    ecs_world_t *world,
    bool end)
{
    ecs_entity_t panel = Shapes2D_follow_parent(world, 0, "panel", 0, 0);
    ecs_entity_t target = Shapes2D_follow_parent(world, panel, "target", 0, 0);
    ecs_set(world, target, Rect, {24, 24});
    ecs_set(world, target, Rgb, {0, 0, 255});
    ecs_entity_t box = end ?
        Shapes2D_shadowed(world, panel, 0, 0, 24, (BoxShadow){
            .offset_x = 6, .offset_y = -6, .blur = 6, .spread = 3, .color = {0, 0, 0, 255}}) :
        Shapes2D_shadowed(world, panel, 0, 0, 16, (BoxShadow){
            .offset_x = 4, .offset_y = -4, .blur = 4, .spread = 2, .color = {0, 0, 0, 255}});
    ecs_set_name(world, box, "box");
}

static void Shapes2D_fit_change(
    ecs_world_t *world,
    int32_t frame)
{
    if (frame == 0) {
        ecs_set(world, Shapes2D_named(world, "panel.box"), Fit, {
            .target = Shapes2D_named(world, "panel.target"), .axes = SpatialXY});
    }
}

void Shapes2D_box_shadow_follows_fit(void) {
    // ui/box_shadow
    Shapes2D_follow("shadow_follow_fit.png", Shapes2D_fit_scene, Shapes2D_fit_change, 2);
}

static void Shapes2D_opacity_scene(
    ecs_world_t *world,
    bool end)
{
    ecs_entity_t parent = Shapes2D_follow_parent(world, 0, "parent", 0, 0);
    ecs_entity_t box = Shapes2D_follow_box(world, parent, "box", 0, 0);
    ecs_set(world, box, Rgb, {255, 0, 0});
    if (end) {
        ecs_set(world, box, Opacity, {0.5f});
        ecs_set(world, parent, Opacity, {0.6f});
    }
}

static void Shapes2D_opacity_change(
    ecs_world_t *world,
    int32_t frame)
{
    if (frame == 0) {
        ecs_set(world, Shapes2D_named(world, "parent.box"), Opacity, {0.5f});
    } else {
        ecs_set(world, Shapes2D_named(world, "parent"), Opacity, {0.6f});
    }
}

void Shapes2D_box_shadow_follows_opacity(void) {
    // ui/box_shadow
    Shapes2D_follow("shadow_follow_opacity.png", Shapes2D_opacity_scene,
        Shapes2D_opacity_change, 2);
}

static void Shapes2D_dynamic_material_scene(
    ecs_world_t *world,
    bool end)
{
    ecs_entity_t parent = Shapes2D_follow_parent(world, 0, "parent", -12, 0);
    ecs_entity_t box = Shapes2D_follow_box(world, 0, "box", 12, 0);
    ecs_entity_t child = Shapes2D_follow_box(world, parent, "child", 0, 0);
    ecs_add(world, box, DynamicMaterial);
    ecs_add(world, parent, DynamicMaterial);
    ecs_set(world, box, Rgb, {0, 0, 255});
    ecs_set(world, child, Rgb, {0, 255, 0});
    ecs_set(world, box, Opacity, {end ? 0.3f : 0.9f});
    ecs_set(world, parent, Opacity, {end ? 0.4f : 0.9f});
}

static void Shapes2D_dynamic_material_change(
    ecs_world_t *world,
    int32_t frame)
{
    /* Written in place as an animation does, which marks nothing: only the per frame shading
     * of the DynamicMaterial shapes reaches the shadows */
    ecs_get_mut(world, Shapes2D_named(world, "box"), Opacity)->value = frame ? 0.3f : 0.6f;
    ecs_get_mut(world, Shapes2D_named(world, "parent"), Opacity)->value = frame ? 0.4f : 0.7f;
}

void Shapes2D_box_shadow_follows_dynamic_material(void) {
    // ui/box_shadow
    Shapes2D_follow("shadow_follow_dynamic_material.png", Shapes2D_dynamic_material_scene,
        Shapes2D_dynamic_material_change, 2);
}

static void Shapes2D_tint_scene(
    ecs_world_t *world,
    bool end)
{
    ecs_entity_t parent = Shapes2D_follow_parent(world, 0, "parent", 0, 0);
    ecs_entity_t box = Shapes2D_follow_box(world, parent, "box", 0, 0);
    if (end) {
        ecs_set(world, parent, Tint, {255, 0, 0, 128});
        ecs_set(world, box, Tint, {0, 0, 255, 100});
    }
}

static void Shapes2D_tint_change(
    ecs_world_t *world,
    int32_t frame)
{
    if (frame == 0) {
        ecs_set(world, Shapes2D_named(world, "parent"), Tint, {255, 0, 0, 128});
    } else {
        ecs_set(world, Shapes2D_named(world, "parent.box"), Tint, {0, 0, 255, 100});
    }
}

void Shapes2D_box_shadow_follows_tint(void) {
    // ui/box_shadow
    Shapes2D_follow("shadow_follow_tint.png", Shapes2D_tint_scene, Shapes2D_tint_change, 2);

    /* The black shadow mixes toward the red tint of the parent and the blue one of the element,
     * where an untinted shadow is gray, next to the element */
    ecs_world_t *world = Shapes2D_shadow_world("shadow_follow_tint.png", false);
    Shapes2D_tint_scene(world, true);
    unsigned char *px = Shapes2D_shadow_capture(world, "shadow_follow_tint.png");
    const unsigned char *c = &px[(42 * 64 + 42) * 4];
    test_assert(c[0] > c[1] + 25);
    test_assert(c[2] > c[1] + 25);
    stbi_image_free(px);
    ecs_fini(world);
}

/** A white clip area with a child whose shadow reaches past it, clipped when on is set.
 *
 * @param world The world
 * @param on Whether the area has ClipChildren
 */
static void Shapes2D_clip_scene_of(
    ecs_world_t *world,
    bool on)
{
    ecs_entity_t clip = Shapes2D_follow_parent(world, 0, "clip", 0, 0);
    ecs_set(world, clip, Rect, {24, 24});
    ecs_set(world, clip, Rgb, {255, 255, 255});
    Shapes2D_shadowed(world, clip, 0, 0, 10, (BoxShadow){.spread = 10, .color = {0, 0, 0, 255}});
    if (on) {
        ecs_add_id(world, clip, ClipChildren);
    }
}

static void Shapes2D_clip_added_scene(
    ecs_world_t *world,
    bool end)
{
    Shapes2D_clip_scene_of(world, end);
}

static void Shapes2D_clip_added_change(
    ecs_world_t *world,
    int32_t frame)
{
    (void)frame;
    ecs_add_id(world, Shapes2D_named(world, "clip"), ClipChildren);
}

void Shapes2D_box_shadow_follows_clip_added(void) {
    // ui/box_shadow
    Shapes2D_follow("shadow_follow_clip_added.png", Shapes2D_clip_added_scene,
        Shapes2D_clip_added_change, 1);
}

static void Shapes2D_clip_removed_scene(
    ecs_world_t *world,
    bool end)
{
    Shapes2D_clip_scene_of(world, !end);
}

static void Shapes2D_clip_removed_change(
    ecs_world_t *world,
    int32_t frame)
{
    (void)frame;
    ecs_remove_id(world, Shapes2D_named(world, "clip"), ClipChildren);
}

void Shapes2D_box_shadow_follows_clip_removed(void) {
    // ui/box_shadow
    Shapes2D_follow("shadow_follow_clip_removed.png", Shapes2D_clip_removed_scene,
        Shapes2D_clip_removed_change, 1);
}

/** Add a named green rect left and a named red rect right of the origin to a panel, 16x40, so
 * the shadow of an element at the origin reaches both.
 *
 * @param world The world
 * @param panel The panel
 * @param left Whether to add the left rect
 * @param right Whether to add the right rect
 */
static void Shapes2D_order_sides(
    ecs_world_t *world,
    ecs_entity_t panel,
    bool left,
    bool right)
{
    if (left) {
        ecs_entity_t early = Shapes2D_follow_parent(world, panel, "early", -12, 0);
        ecs_set(world, early, Rect, {16, 40});
        ecs_set(world, early, Rgb, {0, 255, 0});
    }

    if (right) {
        ecs_entity_t late = Shapes2D_follow_parent(world, panel, "late", 12, 0);
        ecs_set(world, late, Rect, {16, 40});
        ecs_set(world, late, Rgb, {255, 0, 0});
    }
}

/** Add the element of the draw order cases: a 12x12 rect with a shadow over its neighbors.
 *
 * @param world The world
 * @param parent The parent
 * @return The element
 */
static ecs_entity_t Shapes2D_order_box(
    ecs_world_t *world,
    ecs_entity_t parent)
{
    ecs_entity_t e = Shapes2D_shadowed(world, parent, 0, 0, 12,
        (BoxShadow){.spread = 10, .color = {0, 0, 0, 255}});
    ecs_set_name(world, e, "box");

    return e;
}

static void Shapes2D_z_scene(
    ecs_world_t *world,
    bool end)
{
    ecs_entity_t panel = Shapes2D_follow_parent(world, 0, "panel", 0, 0);
    Shapes2D_order_sides(world, panel, true, false);
    ecs_entity_t box = Shapes2D_order_box(world, panel);
    Shapes2D_order_sides(world, panel, false, true);
    if (end) {
        ecs_set(world, box, Position, {0, 0, 1});
    }
}

static void Shapes2D_z_change(
    ecs_world_t *world,
    int32_t frame)
{
    (void)frame;
    ecs_set(world, Shapes2D_named(world, "panel.box"), Position, {0, 0, 1});
}

void Shapes2D_box_shadow_follows_z(void) {
    // ui/box_shadow
    Shapes2D_follow("shadow_follow_z.png", Shapes2D_z_scene, Shapes2D_z_change, 1);
}

static void Shapes2D_child_order_scene(
    ecs_world_t *world,
    bool end)
{
    ecs_entity_t panel = Shapes2D_follow_parent(world, 0, "panel", 0, 0);
    ecs_add_id(world, panel, EcsOrderedChildren);
    if (end) {
        Shapes2D_order_sides(world, panel, true, true);
        Shapes2D_order_box(world, panel);
    } else {
        Shapes2D_order_box(world, panel);
        Shapes2D_order_sides(world, panel, true, true);
    }
}

static void Shapes2D_child_order_change(
    ecs_world_t *world,
    int32_t frame)
{
    ecs_entity_t order[3] = {
        Shapes2D_named(world, "panel.early"),
        Shapes2D_named(world, "panel.late"),
        Shapes2D_named(world, "panel.box")
    };
    (void)frame;
    ecs_set_child_order(world, Shapes2D_named(world, "panel"), order, 3);
}

void Shapes2D_box_shadow_follows_child_order(void) {
    // ui/box_shadow
    Shapes2D_follow("shadow_follow_child_order.png", Shapes2D_child_order_scene,
        Shapes2D_child_order_change, 1);
}

static void Shapes2D_float_scene(
    ecs_world_t *world,
    bool end)
{
    ecs_entity_t panel = Shapes2D_follow_parent(world, 0, "panel", 0, 0);
    Shapes2D_order_sides(world, panel, true, false);
    ecs_entity_t box = Shapes2D_order_box(world, panel);
    Shapes2D_order_sides(world, panel, false, true);
    if (end) {
        ecs_set(world, box, LayoutFloat, {0});
    }
}

static void Shapes2D_float_change(
    ecs_world_t *world,
    int32_t frame)
{
    (void)frame;
    ecs_set(world, Shapes2D_named(world, "panel.box"), LayoutFloat, {0});
}

void Shapes2D_box_shadow_follows_float(void) {
    // ui/box_shadow
    Shapes2D_follow("shadow_follow_float.png", Shapes2D_float_scene, Shapes2D_float_change, 1);
}

static void Shapes2D_reparent_scene(
    ecs_world_t *world,
    bool end)
{
    ecs_entity_t panel = Shapes2D_follow_parent(world, 0, "panel", 0, 0);
    ecs_entity_t back = Shapes2D_follow_parent(world, panel, "back", 0, 0);
    Shapes2D_order_sides(world, panel, true, true);
    ecs_entity_t front = Shapes2D_follow_parent(world, panel, "front", 0, 0);
    Shapes2D_order_box(world, end ? front : back);
}

static void Shapes2D_reparent_change(
    ecs_world_t *world,
    int32_t frame)
{
    (void)frame;
    ecs_add_pair(world, Shapes2D_named(world, "panel.back.box"), EcsChildOf,
        Shapes2D_named(world, "panel.front"));
}

void Shapes2D_box_shadow_follows_reparent(void) {
    // ui/box_shadow
    Shapes2D_follow("shadow_follow_reparent.png", Shapes2D_reparent_scene,
        Shapes2D_reparent_change, 1);
}

static void Shapes2D_scene_scene(
    ecs_world_t *world,
    bool end)
{
    ecs_entity_t root = Shapes2D_follow_parent(world, 0, "root", 0, 0);
    Shapes2D_follow_box(world, root, "box", 0, 0);
    if (end) {
        ecs_add(world, root, Scene);
    }
}

static void Shapes2D_scene_change(
    ecs_world_t *world,
    int32_t frame)
{
    (void)frame;
    ecs_add(world, Shapes2D_named(world, "root"), Scene);
}

void Shapes2D_box_shadow_follows_scene(void) {
    // ui/box_shadow
    Shapes2D_follow("shadow_follow_scene.png", Shapes2D_scene_scene, Shapes2D_scene_change, 1);
}

static void Shapes2D_batch_scene(
    ecs_world_t *world,
    bool end)
{
    ecs_entity_t box = Shapes2D_follow_box(world, 0, "box", 0, 0);
    ecs_set(world, box, Rgb, {255, 0, 0});
    if (end) {
        ecs_set(world, box, BlendMode, {Additive});
    }
}

static void Shapes2D_batch_change(
    ecs_world_t *world,
    int32_t frame)
{
    (void)frame;
    ecs_set(world, Shapes2D_named(world, "box"), BlendMode, {Additive});
}

void Shapes2D_box_shadow_follows_batch(void) {
    // ui/box_shadow
    Shapes2D_follow("shadow_follow_batch.png", Shapes2D_batch_scene, Shapes2D_batch_change, 1);
}

/** A shadowed element next to one without a shadow, disabled when off is set.
 *
 * @param world The world
 * @param off Whether the element is disabled
 */
static void Shapes2D_disabled_scene_of(
    ecs_world_t *world,
    bool off)
{
    ecs_entity_t box = Shapes2D_follow_box(world, 0, "box", -10, 0);
    ecs_entity_t other = Shapes2D_follow_parent(world, 0, "other", 16, 0);
    ecs_set(world, other, Rect, {10, 10});
    ecs_set(world, other, Rgb, {0, 0, 255});
    if (off) {
        ecs_enable(world, box, false);
    }
}

static void Shapes2D_disabled_scene(
    ecs_world_t *world,
    bool end)
{
    Shapes2D_disabled_scene_of(world, end);
}

static void Shapes2D_disabled_change(
    ecs_world_t *world,
    int32_t frame)
{
    (void)frame;
    ecs_enable(world, Shapes2D_named(world, "box"), false);
}

void Shapes2D_box_shadow_follows_disabled(void) {
    // ui/box_shadow
    Shapes2D_follow("shadow_follow_disabled.png", Shapes2D_disabled_scene,
        Shapes2D_disabled_change, 1);
}

static void Shapes2D_enabled_scene(
    ecs_world_t *world,
    bool end)
{
    Shapes2D_disabled_scene_of(world, !end);
}

static void Shapes2D_enabled_change(
    ecs_world_t *world,
    int32_t frame)
{
    (void)frame;
    ecs_enable(world, Shapes2D_named(world, "box"), true);
}

void Shapes2D_box_shadow_follows_enabled(void) {
    // ui/box_shadow
    Shapes2D_follow("shadow_follow_enabled.png", Shapes2D_enabled_scene,
        Shapes2D_enabled_change, 1);
}
