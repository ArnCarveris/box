/**
 * @file Shapes2D.c
 * @brief Tests for the 2D shapes: their outlines, borders, images, edges and batches.
 */

#include <box.h>
#include <bake_test.h>
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
