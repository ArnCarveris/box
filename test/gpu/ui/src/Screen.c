/**
 * @file Screen.c
 * @brief Screen, world and texture UI: UI scenes and views, draw order and render to texture.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Screen_hud_over_3d_scene(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t world_view = ecs_new(world);
    ecs_set(world, world_view, View, {.camera = camera, .lighting.enabled = true,
        .clear = {.enabled = true, .color = {0, 0, 255, 255}}});
    ecs_entity_t box = ecs_new(world);
    ecs_set(world, box, Position, {0, 0, 0});
    ecs_set(world, box, Box, {1, 1, 1});
    ecs_set(world, box, Rgb, {255, 255, 255});

    ecs_entity_t hud = ecs_new(world);
    ecs_set(world, hud, Scene, {0});
    ecs_entity_t hud_camera = ecs_new_w_pair(world, EcsChildOf, hud);
    ecs_set(world, hud_camera, Position, {0, 0, 100});
    ecs_set(world, hud_camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t hud_view = ecs_new(world);
    ecs_set(world, hud_view, View, {.camera = hud_camera});
    ecs_entity_t rect = ecs_new_w_pair(world, EcsChildOf, hud);
    ecs_set(world, rect, Position, {-20, 20, 0});
    ecs_set(world, rect, Rect, {16, 16});
    ecs_set(world, rect, Rgb, {0, 200, 0});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "hud.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/hud.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *ui = &px[(12 * 64 + 12) * 4];
    test_assert(ui[0] < 3);
    test_assert(abs((int)ui[1] - 200) < 3);
    test_assert(ui[2] < 3);
    unsigned char *lit = &px[(32 * 64 + 32) * 4];
    test_assert(lit[0] > 40);
    test_assert(lit[1] > 40);
    unsigned char *clear = &px[(60 * 64 + 60) * 4];
    test_assert(clear[0] < 3);
    test_assert(clear[1] < 3);
    test_assert(clear[2] > 252);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_ortho_scale_counts_points(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t rect = ecs_new(world);
    ecs_set(world, rect, Position, {0, 0, 0});
    ecs_set(world, rect, Rect, {20, 10});
    ecs_set(world, rect, Rgb, {0, 255, 0});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "size.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/size.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, i, wide = 0, high = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 64);
    for (i = 0; i < 64; i ++) {
        wide += px[(32 * 64 + i) * 4 + 1] > 128;
        high += px[(i * 64 + 32) * 4 + 1] > 128;
    }

    test_int(wide, 20);
    test_int(high, 10);
    stbi_image_free(px);

    ecs_set(world, s, Surface, {.width = 128, .height = 96, .write_to_file = "size.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 128);
    test_int(h, 96);
    wide = 0;
    high = 0;
    for (i = 0; i < 128; i ++) {
        wide += px[(48 * 128 + i) * 4 + 1] > 128;
    }

    for (i = 0; i < 96; i ++) {
        high += px[(i * 128 + 64) * 4 + 1] > 128;
    }

    test_int(wide, 20);
    test_int(high, 10);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_resolution_scale_keeps_ui_size(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t rect = ecs_new(world);
    ecs_set(world, rect, Position, {0, 0, 0});
    ecs_set(world, rect, Rect, {20, 10});
    ecs_set(world, rect, Rgb, {0, 255, 0});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "scaled.png",
        .resolution_scale = 2});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    const SurfaceStatus *status = ecs_get(world, s, SurfaceStatus);
    test_assert(status != NULL);
    test_int(status->render_width, 32);
    char *path = flecs_asprintf("%s/scaled.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, i, wide = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 64);
    for (i = 0; i < 64; i ++) {
        wide += px[(32 * 64 + i) * 4 + 1] > 128;
    }

    test_assert(wide >= 18);
    test_assert(wide <= 24);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_pixel_ratio_scales_pixels(void) {
    // scenes/projection_points
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t rect = ecs_new(world);
    ecs_set(world, rect, Position, {0, 0, 0});
    ecs_set(world, rect, Rect, {20, 10});
    ecs_set(world, rect, Rgb, {0, 255, 0});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "ratio.png",
        .resolution_scale = 1, .pixel_ratio = 2});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    const SurfaceStatus *status = ecs_get(world, s, SurfaceStatus);
    test_assert(status != NULL);
    test_int(status->point_width, 64);
    test_int(status->render_width, 128);
    char *path = flecs_asprintf("%s/ratio.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, i, wide = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 128);
    for (i = 0; i < 128; i ++) {
        wide += px[(64 * 128 + i) * 4 + 1] > 128;
    }

    test_assert(wide >= 38);
    test_assert(wide <= 44);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_views_stack_in_view_order(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t world_view = ecs_new(world);
    ecs_set(world, world_view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {255, 0, 0, 255}}});

    ecs_entity_t hud = ecs_new(world);
    ecs_set(world, hud, Scene, {0});
    ecs_entity_t hud_camera = ecs_new_w_pair(world, EcsChildOf, hud);
    ecs_set(world, hud_camera, Position, {0, 0, 100});
    ecs_set(world, hud_camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t hud_rect = ecs_new_w_pair(world, EcsChildOf, hud);
    ecs_set(world, hud_rect, Position, {0, 0, 0});
    ecs_set(world, hud_rect, Rect, {30, 30});
    ecs_set(world, hud_rect, Rgb, {0, 255, 0});
    ecs_entity_t hud_view = ecs_new(world);
    ecs_set(world, hud_view, View, {.camera = hud_camera});

    ecs_entity_t menu = ecs_new(world);
    ecs_set(world, menu, Scene, {0});
    ecs_entity_t menu_camera = ecs_new_w_pair(world, EcsChildOf, menu);
    ecs_set(world, menu_camera, Position, {0, 0, 100});
    ecs_set(world, menu_camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t menu_rect = ecs_new_w_pair(world, EcsChildOf, menu);
    ecs_set(world, menu_rect, Position, {0, 0, 0});
    ecs_set(world, menu_rect, Rect, {10, 10});
    ecs_set(world, menu_rect, Rgb, {0, 0, 255});
    ecs_entity_t menu_view = ecs_new(world);
    ecs_set(world, menu_view, View, {.camera = menu_camera});

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "stack.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/stack.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *center = &px[(32 * 64 + 32) * 4];
    test_assert(center[0] < 3 && center[1] < 3 && center[2] > 252);
    unsigned char *ring = &px[(32 * 64 + 42) * 4];
    test_assert(ring[0] < 3 && ring[1] > 252 && ring[2] < 3);
    unsigned char *corner = &px[(2 * 64 + 2) * 4];
    test_assert(corner[0] > 252 && corner[1] < 3 && corner[2] < 3);
    stbi_image_free(px);

    ecs_delete(world, hud_view);
    hud_view = ecs_new(world);
    ecs_set(world, hud_view, View, {.camera = hud_camera});
    ecs_progress(world, 0);
    box_render_sync(world);

    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    center = &px[(32 * 64 + 32) * 4];
    test_assert(center[0] < 3 && center[1] > 252 && center[2] < 3);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_unlit_ui_view_draws_base_color(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t ui = ecs_new(world);
    ecs_set(world, ui, Scene, {0});
    ecs_entity_t light = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, light, Position, {0, 0, 30});
    ecs_set(world, light, PointLight, {.intensity = 50 * GLM_PIf, .range = 100});
    ecs_set(world, light, Rgb, {255, 0, 0});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t cube = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, cube, Position, {0, 0, 0});
    ecs_set(world, cube, Box, {20, 20, 20});
    ecs_set(world, cube, Rgb, {200, 100, 50});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "unlit.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/unlit.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(32 * 64 + 32) * 4];
    test_assert(abs((int)c[0] - 200) < 3);
    test_assert(abs((int)c[1] - 100) < 3);
    test_assert(abs((int)c[2] - 50) < 3);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_ui_view_skips_culling(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t inside = ecs_new(world);
    ecs_set(world, inside, Position, {0, 0, 0});
    ecs_set(world, inside, Rect, {10, 10});
    ecs_entity_t outside = ecs_new(world);
    ecs_set(world, outside, Position, {1000, 0, 0});
    ecs_set(world, outside, Rect, {10, 10});
    ecs_entity_t tiny = ecs_new(world);
    ecs_set(world, tiny, Position, {10, 10, 0});
    ecs_set(world, tiny, Circle, {0.05f});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .resolution_scale = 1});

    int64_t start = ecs_get_world_info(world)->frame_count_total + 3;
    const CullStatus *status = NULL;
    int32_t frames = 0;
    while ((!status || status->frame < start) && frames < 5000) {
        ecs_progress(world, 0);
        box_render_sync(world);
        status = ecs_get(world, view, CullStatus);
        frames ++;
    }

    test_assert(status != NULL);
    test_assert(status->frame >= start);
    test_int(status->instances, 3);
    test_int(status->visible, 3);
    test_int(status->frustum_culled, 0);
    test_int(status->occlusion_culled, 0);

    ecs_fini(world);
}

void Screen_effects_of_3d_view_skip_ui(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t world_view = ecs_new(world);
    ecs_set(world, world_view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    View *v = ecs_get_mut(world, world_view, View);
    *ecs_vec_append_t(NULL, &v->effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = tonemap };
    ecs_modified(world, world_view, View);
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, 0, 0});
    ecs_set(world, floor, Quad, {20, 20});
    ecs_set(world, floor, Rgb, {0, 0, 0});
    ecs_set(world, floor, Emissive, {.strength = 1.5f, .color = {255, 255, 255}});

    ecs_entity_t hud = ecs_new(world);
    ecs_set(world, hud, Scene, {0});
    ecs_entity_t hud_camera = ecs_new_w_pair(world, EcsChildOf, hud);
    ecs_set(world, hud_camera, Position, {0, 0, 100});
    ecs_set(world, hud_camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t hud_view = ecs_new(world);
    ecs_set(world, hud_view, View, {.camera = hud_camera});
    ecs_entity_t rect = ecs_new_w_pair(world, EcsChildOf, hud);
    ecs_set(world, rect, Position, {0, 0, 0});
    ecs_set(world, rect, Rect, {20, 20});
    ecs_set(world, rect, Rgb, {255, 255, 255});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "fx.png",
        .resolution_scale = 1});
    ecs_progress(world, 1.0f / 60);
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/fx.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *mapped = &px[(4 * 64 + 4) * 4];
    test_assert(mapped[0] < 252);
    unsigned char *ui = &px[(32 * 64 + 32) * 4];
    test_int(ui[0], 255);
    test_int(ui[1], 255);
    test_int(ui[2], 255);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_children_draw_over_parents(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t child = ecs_new(world);
    ecs_set(world, child, Position, {0, 0, 0});
    ecs_set(world, child, Rect, {10, 10});
    ecs_set(world, child, Rgb, {0, 255, 0});
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_set(world, parent, Rect, {30, 30});
    ecs_set(world, parent, Rgb, {255, 0, 0});
    ecs_add_pair(world, child, EcsChildOf, parent);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "child.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/child.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] < 3 && c[1] > 252);
    unsigned char *p = &px[(32 * 64 + 42) * 4];
    test_assert(p[0] > 252 && p[1] < 3);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_parent_storage_children_draw_over_parents(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t child = ecs_new(world);
    ecs_set(world, child, Position, {0, 0, 0});
    ecs_set(world, child, Rect, {10, 10});
    ecs_set(world, child, Rgb, {0, 255, 0});
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_set(world, parent, Rect, {30, 30});
    ecs_set(world, parent, Rgb, {255, 0, 0});
    ecs_set(world, child, EcsParent, {parent});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "parent.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/parent.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] < 3 && c[1] > 252);
    unsigned char *p = &px[(32 * 64 + 42) * 4];
    test_assert(p[0] > 252 && p[1] < 3);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_siblings_follow_child_order(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t parent = ecs_new(world);
    ecs_add_id(world, parent, EcsOrderedChildren);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, a, Position, {-5, 0, 0});
    ecs_set(world, a, Rect, {20, 20});
    ecs_set(world, a, Rgb, {255, 0, 0});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, b, Position, {5, 0, 0});
    ecs_set(world, b, Rect, {20, 20});
    ecs_set(world, b, Rgb, {0, 255, 0});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "order.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/order.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] < 3 && c[1] > 252);
    stbi_image_free(px);

    ecs_entity_t order[2] = {b, a};
    ecs_set_child_order(world, parent, order, 2);
    ecs_progress(world, 0);
    box_render_sync(world);

    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] > 252 && c[1] < 3);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_order_stable_after_removal(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    Rgb colors[4] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}};
    ecs_entity_t children[4];
    int32_t i;
    for (i = 0; i < 4; i ++) {
        children[i] = ecs_new_w_pair(world, EcsChildOf, parent);
        ecs_set(world, children[i], Position, {0, 0, 0});
        ecs_set(world, children[i], Rect, {20, 20});
        ecs_set_ptr(world, children[i], Rgb, &colors[i]);
    }

    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "stable.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/stable.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] > 252 && c[1] > 252 && c[2] > 252);
    stbi_image_free(px);

    ecs_delete(world, children[0]);
    ecs_progress(world, 0);
    box_render_sync(world);

    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] > 252 && c[1] > 252 && c[2] > 252);
    stbi_image_free(px);

    ecs_delete(world, children[3]);
    ecs_progress(world, 0);
    box_render_sync(world);

    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] < 3 && c[1] < 3 && c[2] > 252);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_text_draws_over_background(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t label = ecs_new(world);
    ecs_set(world, label, Position, {0, 0, 0});
    ecs_set(world, label, Text, {.value = "I", .size = 50,
        .font = ecs_lookup(world, "box.text.DefaultFont")});
    ecs_set(world, label, Rgb, {255, 128, 0});
    ecs_entity_t background = ecs_new(world);
    ecs_set(world, background, Position, {0, 0, 0});
    ecs_set(world, background, Rect, {60, 60});
    ecs_set(world, background, Rgb, {0, 0, 80});
    ecs_add_pair(world, label, EcsChildOf, background);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "text.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/text.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, x, ink = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (x = 0; x < 64; x ++) {
        unsigned char *c = &px[(32 * 64 + x) * 4];
        if (c[0] > 250 && c[1] > 120 && c[1] < 136 && c[2] < 5) {
            ink ++;
        }
    }

    test_assert(ink >= 3);
    unsigned char *bg = &px[(32 * 64 + 6) * 4];
    test_assert(bg[0] < 3 && bg[2] > 75);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_translucent_parent_under_children(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t child = ecs_new(world);
    ecs_set(world, child, Position, {0, 0, 0});
    ecs_set(world, child, Rect, {10, 10});
    ecs_set(world, child, Rgb, {0, 255, 0});
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_set(world, parent, Rect, {30, 30});
    ecs_set(world, parent, Rgb, {255, 0, 0});
    ecs_set(world, parent, Opacity, {0.5f});
    ecs_add_pair(world, child, EcsChildOf, parent);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "fade.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/fade.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(32 * 64 + 32) * 4];
    test_assert(c[1] > c[0] + 30);
    test_assert(c[0] > 100);
    unsigned char *p = &px[(32 * 64 + 42) * 4];
    test_assert(p[0] > 150 && p[1] < 3);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_explicit_z_orders_siblings(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 0, 0});
    ecs_entity_t glass = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, glass, Position, {0, 0, 1});
    ecs_set(world, glass, Rect, {20, 20});
    ecs_set(world, glass, Rgb, {255, 255, 255});
    ecs_set(world, glass, Opacity, {0.5f});
    ecs_entity_t below = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, below, Position, {0, 0, 0});
    ecs_set(world, below, Rect, {30, 30});
    ecs_set(world, below, Rgb, {255, 0, 0});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "z.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/z.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] > 252);
    test_assert(c[1] > 150);
    stbi_image_free(px);

    ecs_set(world, glass, Position, {0, 0, 0});
    ecs_progress(world, 0);
    box_render_sync(world);

    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] > 252);
    test_assert(c[1] < 3);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_layout_children_draw_over_background(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t first = ecs_new(world);
    ecs_set(world, first, Rect, {20, 10});
    ecs_set(world, first, Rgb, {0, 255, 0});
    ecs_entity_t second = ecs_new(world);
    ecs_set(world, second, Rect, {20, 10});
    ecs_set(world, second, Rgb, {0, 0, 255});
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Layout, {.direction = DirectionColumn, .spacing = 4,
        .padding = {5, 5, 5, 5}});
    ecs_set(world, panel, Rect, {1, 1});
    ecs_set(world, panel, Rgb, {255, 0, 0});
    ecs_add_pair(world, first, EcsChildOf, panel);
    ecs_add_pair(world, second, EcsChildOf, panel);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "layout.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_flt(ecs_get(world, first, Position)->z, 0);
    test_flt(ecs_get(world, second, Position)->z, 0);
    const LayoutBounds *bounds = ecs_get(world, panel, LayoutBounds);
    test_flt(bounds->width, 30);
    test_flt(bounds->height, 34);
    char *path = flecs_asprintf("%s/layout.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *top = &px[(25 * 64 + 32) * 4];
    test_assert(top[0] < 3 && top[1] > 252 && top[2] < 3);
    unsigned char *bottom = &px[(39 * 64 + 32) * 4];
    test_assert(bottom[0] < 3 && bottom[1] < 3 && bottom[2] > 252);
    unsigned char *gap = &px[(32 * 64 + 32) * 4];
    test_assert(gap[0] > 252 && gap[1] < 3 && gap[2] < 3);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_world_space_scale(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 4, .near_ = 0.1f,
        .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Position, {0, 0, 0});
    ecs_set(world, panel, Scale, {0.01f, 0.01f, 0.01f});
    ecs_set(world, panel, Rect, {200, 100});
    ecs_set(world, panel, Rgb, {0, 255, 0});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 100, .height = 100, .write_to_file = "world.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/world.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, i, wide = 0, high = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0; i < 100; i ++) {
        wide += px[(50 * 100 + i) * 4 + 1] > 128;
        high += px[(i * 100 + 50) * 4 + 1] > 128;
    }

    test_int(wide, 50);
    test_assert(high >= 24 && high <= 26);
    unsigned char *c = &px[(50 * 100 + 50) * 4];
    test_assert(c[0] < 3 && c[1] > 252 && c[2] < 3);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_world_space_rotated_children(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 2.5f});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t panel = ecs_new(world);
    ecs_set(world, panel, Position, {0, 0, 0});
    ecs_set(world, panel, Rotation, {0.3f, 0.9f, 0.2f});
    ecs_set(world, panel, Scale, {0.01f, 0.01f, 0.01f});
    ecs_set(world, panel, Rect, {200, 200});
    ecs_set(world, panel, Rgb, {255, 0, 0});
    ecs_entity_t content = ecs_new_w_pair(world, EcsChildOf, panel);
    ecs_set(world, content, Position, {0, 0, 0});
    ecs_set(world, content, Rect, {100, 100});
    ecs_set(world, content, Rgb, {0, 255, 0});
    ecs_entity_t label = ecs_new_w_pair(world, EcsChildOf, content);
    ecs_set(world, label, Position, {0, 0, 0});
    ecs_set(world, label, Rect, {40, 40});
    ecs_set(world, label, Rgb, {0, 0, 255});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 128, .height = 128, .write_to_file = "rotated.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/rotated.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, x, y, label_px = 0, content_px = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (y = 60; y < 68; y ++) {
        for (x = 60; x < 68; x ++) {
            unsigned char *c = &px[(y * 128 + x) * 4];
            label_px += c[0] < 3 && c[1] < 3 && c[2] > 252;
        }
    }

    test_int(label_px, 64);
    for (y = 0; y < 128; y ++) {
        for (x = 0; x < 128; x ++) {
            unsigned char *c = &px[(y * 128 + x) * 4];
            content_px += c[1] > 128;
        }
    }

    test_assert(content_px > 200);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_texture_surface_sampled_by_image(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t main_surface = ecs_new(world);
    ecs_set(world, main_surface, Surface, {.width = 64, .height = 64,
        .write_to_file = "main.png", .resolution_scale = 1});
    ecs_entity_t texture = ecs_new(world);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t image = ecs_new(world);
    ecs_set(world, image, Position, {0, 0, 0});
    ecs_set(world, image, Rect, {40, 40});
    ecs_set(world, image, Image, {.texture = texture, .u1 = 1, .v1 = 1});

    ecs_entity_t ui = ecs_new(world);
    ecs_set(world, ui, Scene, {0});
    ecs_entity_t ui_camera = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, ui_camera, Position, {0, 0, 100});
    ecs_set(world, ui_camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t ui_rect = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, ui_rect, Position, {0, 0, 0});
    ecs_set(world, ui_rect, Rect, {16, 16});
    ecs_set(world, ui_rect, Rgb, {0, 255, 0});
    ecs_entity_t root = ecs_new(world);
    ecs_entity_t ui_view = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, ui_view, View, {.camera = ui_camera,
        .clear = {.enabled = true, .color = {0, 0, 255, 255}}});
    ecs_entity_t screen = ecs_new(world);
    ecs_set(world, screen, Surface, {.width = 32, .height = 32, .texture = texture,
        .view_root = root, .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/main.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] < 3 && c[1] > 252 && c[2] < 3);
    unsigned char *edge = &px[(32 * 64 + 48) * 4];
    test_assert(edge[0] < 3 && edge[1] < 3 && edge[2] > 252);
    unsigned char *outside = &px[(2 * 64 + 2) * 4];
    test_assert(outside[0] < 3 && outside[1] < 3 && outside[2] < 3);
    stbi_image_free(px);

    ecs_set(world, ui_rect, Rgb, {255, 0, 0});
    ecs_progress(world, 0);
    box_render_sync(world);

    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] > 252 && c[1] < 3 && c[2] < 3);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_texture_surface_sampled_by_material(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t texture = ecs_new(world);
    ecs_entity_t ui = ecs_new(world);
    ecs_set(world, ui, Scene, {0});
    ecs_entity_t ui_camera = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, ui_camera, Position, {0, 0, 100});
    ecs_set(world, ui_camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t root = ecs_new(world);
    ecs_entity_t ui_view = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, ui_view, View, {.camera = ui_camera,
        .clear = {.enabled = true, .color = {255, 128, 0, 255}}});
    ecs_entity_t screen = ecs_new(world);
    ecs_set(world, screen, Surface, {.width = 16, .height = 16, .texture = texture,
        .view_root = root, .resolution_scale = 1});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t quad = ecs_new(world);
    ecs_set(world, quad, Position, {0, 0, 0});
    ecs_set(world, quad, Rotation, {0, 0.5f, 0});
    ecs_set(world, quad, Quad, {2, 2});
    ecs_set(world, quad, Rgb, {255, 255, 255});
    ecs_set(world, quad, PbrTextures, {.albedo = texture});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "pbr.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/pbr.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(32 * 64 + 32) * 4];
    test_assert(abs((int)c[0] - 255) < 3);
    test_assert(abs((int)c[1] - 128) < 3);
    test_assert(c[2] < 3);
    unsigned char *outside = &px[(2 * 64 + 2) * 4];
    test_assert(outside[0] < 3 && outside[1] < 3 && outside[2] < 3);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_texture_surface_removed(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t texture = ecs_new(world);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t image = ecs_new(world);
    ecs_set(world, image, Position, {0, 0, 0});
    ecs_set(world, image, Rect, {40, 40});
    ecs_set(world, image, Image, {.texture = texture, .u1 = 1, .v1 = 1});
    ecs_entity_t ui = ecs_new(world);
    ecs_set(world, ui, Scene, {0});
    ecs_entity_t ui_camera = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, ui_camera, Position, {0, 0, 100});
    ecs_set(world, ui_camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t root = ecs_new(world);
    ecs_entity_t ui_view = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, ui_view, View, {.camera = ui_camera,
        .clear = {.enabled = true, .color = {0, 0, 255, 255}}});
    ecs_entity_t screen = ecs_new(world);
    ecs_set(world, screen, Surface, {.width = 16, .height = 16, .texture = texture,
        .view_root = root, .resolution_scale = 1});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "removed.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/removed.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] < 3 && c[1] < 3 && c[2] > 252);
    stbi_image_free(px);

    ecs_delete(world, screen);
    ecs_delete(world, root);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] > 252 && c[1] > 252 && c[2] > 252);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_texture_surface_not_sampled_by_itself(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t texture = ecs_new(world);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t image = ecs_new(world);
    ecs_set(world, image, Position, {0, 0, 0});
    ecs_set(world, image, Rect, {32, 32});
    ecs_set(world, image, Image, {.texture = texture, .u1 = 1, .v1 = 1});
    ecs_entity_t ui = ecs_new(world);
    ecs_set(world, ui, Scene, {0});
    ecs_entity_t ui_camera = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, ui_camera, Position, {0, 0, 100});
    ecs_set(world, ui_camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t mirror = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, mirror, Position, {0, 0, 0});
    ecs_set(world, mirror, Rect, {16, 16});
    ecs_set(world, mirror, Image, {.texture = texture, .u1 = 1, .v1 = 1});
    ecs_entity_t root = ecs_new(world);
    ecs_entity_t ui_view = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, ui_view, View, {.camera = ui_camera,
        .clear = {.enabled = true, .color = {0, 0, 255, 255}}});
    ecs_entity_t screen = ecs_new(world);
    ecs_set(world, screen, Surface, {.width = 32, .height = 32, .texture = texture,
        .view_root = root, .resolution_scale = 1});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64, .write_to_file = "self.png",
        .resolution_scale = 1});
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);

    char *path = flecs_asprintf("%s/self.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] > 252 && c[1] > 252 && c[2] > 252);
    unsigned char *edge = &px[(32 * 64 + 45) * 4];
    test_assert(edge[0] < 3 && edge[1] < 3 && edge[2] > 252);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_view_root_views_skipped_by_other_surfaces(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {255, 0, 0, 255}}});
    ecs_entity_t root = ecs_new(world);
    ecs_entity_t claimed = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, claimed, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 255, 0, 255}}});
    ecs_entity_t all = ecs_new(world);
    ecs_set(world, all, Surface, {.width = 8, .height = 8, .write_to_file = "all.png",
        .resolution_scale = 1});
    ecs_entity_t rooted = ecs_new(world);
    ecs_set(world, rooted, Surface, {.width = 8, .height = 8, .write_to_file = "rooted.png",
        .view_root = root, .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *path = flecs_asprintf("%s/all.png", tmp);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(4 * 8 + 4) * 4] > 252);
    test_assert(px[(4 * 8 + 4) * 4 + 1] < 3);
    stbi_image_free(px);
    ecs_os_free(path);
    path = flecs_asprintf("%s/rooted.png", tmp);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(4 * 8 + 4) * 4] < 3);
    test_assert(px[(4 * 8 + 4) * 4 + 1] > 252);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Screen_script(void) {
    // ui/screen
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    const char *code =
        "using box.transform\n"
        "using box.geometry\n"
        "using box.material\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.surface\n"
        "camera {\n"
        "  Position: {0, 0, 5}\n"
        "  Camera: {}\n"
        "}\n"
        "world_view {\n"
        "  View: {camera: camera, lighting: {enabled: false},\n"
        "    clear: {enabled: true, color: {0, 0, 0, 255}}}\n"
        "}\n"
        "screen_texture {}\n"
        "panel {\n"
        "  Position: {0, 0, 0}\n"
        "  Quad: {4, 4}\n"
        "  Rgb: {0, 0, 0}\n"
        "  Emissive: {strength: 1, color: {255, 255, 255}}\n"
        "  PbrTextures: {emissive: screen_texture}\n"
        "}\n"
        "screen_ui {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  fill {\n"
        "    Position: {0, 0, 0}\n"
        "    Rect: {64, 64}\n"
        "    Rgb: {0, 255, 0}\n"
        "  }\n"
        "}\n"
        "screen_views {\n"
        "  view {\n"
        "    View: {camera: screen_ui.camera, lighting: {enabled: false},\n"
        "      culling: {enabled: false}}\n"
        "  }\n"
        "}\n"
        "screen_surface {\n"
        "  Surface: {width: 16, height: 16, texture: screen_texture, view_root: screen_views}\n"
        "}\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  badge {\n"
        "    Position: {-24, 24, 0}\n"
        "    Rect: {12, 12}\n"
        "    Rgb: {0, 0, 255}\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera, lighting: {enabled: false}, culling: {enabled: false}}\n"
        "}\n"
        "out { Surface: {width: 64, height: 64, write_to_file: \"script.png\"} }\n";
    test_assert(ecs_script_run(world, "ui", code, NULL) == 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/script.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 64);
    unsigned char *c = &px[(32 * 64 + 32) * 4];
    test_assert(c[0] < 3 && c[1] > 252 && c[2] < 3);
    unsigned char *badge = &px[(8 * 64 + 8) * 4];
    test_assert(badge[0] < 3 && badge[1] < 3 && badge[2] > 252);
    unsigned char *clear = &px[(60 * 64 + 60) * 4];
    test_assert(clear[0] < 3 && clear[1] < 3 && clear[2] < 3);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

/** Make a headless world with an orthographic UI camera of 1 point per pixel, a white view and
 * a layout panel with a shadow around a child of a size.
 *
 * @param file The file the 64x64 surface writes
 * @param width Width of the child
 * @param height Height of the child
 * @return The world
 */
static ecs_world_t* Screen_shadow_layout_world(
    const char *file,
    float width,
    float height)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {255, 255, 255, 255}}});
    ecs_entity_t panel = ecs_entity(world, { .name = "panel" });
    ecs_set(world, panel, Layout, {.direction = DirectionColumn,
        .padding = {4, 4, 4, 4}});
    ecs_set(world, panel, Rect, {1, 1});
    ecs_set(world, panel, Rgb, {0, 0, 255});
    ecs_set(world, panel, BoxShadow, {.offset_x = 3, .offset_y = -3, .blur = 4, .spread = 2,
        .color = {0, 0, 0, 255}});
    ecs_entity_t child = ecs_entity(world, { .name = "child", .parent = panel });
    ecs_set(world, child, Rect, {width, height});
    ecs_set(world, child, Rgb, {0, 255, 0});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});

    return world;
}

/** Render two frames and load the image a surface wrote.
 *
 * @param world The world
 * @param file The file of the surface
 * @return The pixels, RGBA, free with stbi_image_free
 */
static unsigned char* Screen_shadow_layout_render(
    ecs_world_t *world,
    const char *file)
{
    int w = 0, h = 0, n = 0;
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    ecs_os_free(path);
    test_assert(px != NULL);
    test_int(w, 64);
    test_int(h, 64);

    return px;
}

void Screen_layout_resize_moves_box_shadow(void) {
    // ui/box_shadow
    ecs_world_t *world = Screen_shadow_layout_world("shadow_layout.png", 30, 20);
    unsigned char *end = Screen_shadow_layout_render(world, "shadow_layout.png");
    const LayoutBounds *bounds = ecs_get(world, ecs_lookup(world, "panel"), LayoutBounds);
    test_flt(bounds->width, 38);
    test_flt(bounds->height, 28);
    ecs_fini(world);

    world = Screen_shadow_layout_world("shadow_layout.png", 16, 10);
    unsigned char *px = Screen_shadow_layout_render(world, "shadow_layout.png");
    int32_t i, count = 0;
    bool differ = false;
    for (i = 0; i < 64 * 64 * 4; i ++) {
        differ |= (i % 4) != 3 && abs((int)px[i] - (int)end[i]) > 40;
    }

    test_assert(differ);
    stbi_image_free(px);

    ecs_set(world, ecs_lookup(world, "panel.child"), Rect, {30, 20});
    px = Screen_shadow_layout_render(world, "shadow_layout.png");
    bounds = ecs_get(world, ecs_lookup(world, "panel"), LayoutBounds);
    test_flt(bounds->width, 38);
    test_flt(bounds->height, 28);
    for (i = 0; i < 64 * 64 * 4; i ++) {
        if ((i % 4) != 3 && abs((int)px[i] - (int)end[i]) > 2) {
            count ++;
        }
    }

    test_int(count, 0);
    stbi_image_free(px);
    stbi_image_free(end);
    ecs_fini(world);
}
