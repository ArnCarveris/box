/**
 * @file Views.c
 * @brief Tests for views: render order (View.order, then creation), the scene of the camera,
 * the camera transform and views without a camera, checked on PNG captures.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Views_creation_order(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ECS_TAG(world, Moved);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, View, {
        .camera = camera, .clear = {.enabled = true, .color = {255, 0, 0, 255}}});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 255, 0, 255}}});
    ecs_entity_t c = ecs_new(world);
    ecs_set(world, c, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 255, 255}}});
    ecs_add(world, a, Moved);
    ecs_add(world, b, Moved);
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 8, .height = 8, .write_to_file = "order.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/order.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, i, wrong = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0; i < w * h; i ++) {
        wrong += px[i * 4] || px[i * 4 + 1] || px[i * 4 + 2] != 255;
    }

    test_int(wrong, 0);
    stbi_image_free(px);

    View *v = ecs_get_mut(world, c, View);
    v->clear.enabled = false;
    ecs_modified(world, c, View);
    ecs_progress(world, 0);
    box_render_sync(world);

    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0, wrong = 0; i < w * h; i ++) {
        wrong += px[i * 4] || px[i * 4 + 1] != 255 || px[i * 4 + 2];
    }

    test_int(wrong, 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

/** Count the pixels of a capture of the temporary directory that are not a color.
 *
 * @param world The world
 * @param file Name of the capture
 * @param r Expected red
 * @param g Expected green
 * @param b Expected blue
 * @return Pixels of another color, -1 when the capture has none
 */
static int views_wrong(
    ecs_world_t *world,
    const char *file,
    unsigned char r,
    unsigned char g,
    unsigned char b)
{
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0, i, result = -1;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    if (px && w * h) {
        for (i = 0, result = 0; i < w * h; i ++) {
            result += px[i * 4] != r || px[i * 4 + 1] != g || px[i * 4 + 2] != b;
        }
    }

    stbi_image_free(px);
    ecs_os_free(path);

    return result;
}

/** Create a view of a camera that clears to a color.
 *
 * @param world The world
 * @param camera The camera
 * @param order Order of the view
 * @param r Red
 * @param g Green
 * @param b Blue
 * @return The view
 */
static ecs_entity_t views_clear(
    ecs_world_t *world,
    ecs_entity_t camera,
    int32_t order,
    unsigned char r,
    unsigned char g,
    unsigned char b)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, View, {
        .camera = camera, .clear = {.enabled = true, .color = {r, g, b, 255}},
        .order = order});

    return result;
}

void Views_order(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    views_clear(world, camera, 2, 255, 0, 0);
    views_clear(world, camera, 0, 0, 255, 0);
    views_clear(world, camera, 1, 0, 0, 255);
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 8, .height = 8, .write_to_file = "view_order.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(views_wrong(world, "view_order.png", 255, 0, 0), 0);

    ecs_fini(world);
}

void Views_order_ties(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    views_clear(world, camera, 1, 255, 0, 0);
    views_clear(world, camera, 1, 0, 255, 0);
    views_clear(world, camera, 0, 0, 0, 255);
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 8, .height = 8, .write_to_file = "view_ties.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(views_wrong(world, "view_ties.png", 0, 255, 0), 0);

    ecs_fini(world);
}

void Views_order_runtime_change(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t a = views_clear(world, camera, 0, 255, 0, 0);
    views_clear(world, camera, 0, 0, 255, 0);
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 8, .height = 8,
        .write_to_file = "view_runtime.png", .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(views_wrong(world, "view_runtime.png", 0, 255, 0), 0);

    ecs_get_mut(world, a, View)->order = 1;
    ecs_modified(world, a, View);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(views_wrong(world, "view_runtime.png", 255, 0, 0), 0);

    ecs_fini(world);
}

void Views_order_view_root(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t root = ecs_new(world);
    ecs_entity_t a = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, a, View, {
        .camera = camera, .clear = {.enabled = true, .color = {255, 0, 0, 255}},
        .order = 1});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, b, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 255, 255}}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 8, .height = 8, .view_root = root,
        .write_to_file = "view_root_order.png", .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(views_wrong(world, "view_root_order.png", 255, 0, 0), 0);

    ecs_get_mut(world, b, View)->order = 2;
    ecs_modified(world, b, View);
    ecs_progress(world, 0);
    box_render_sync(world);
    test_int(views_wrong(world, "view_root_order.png", 0, 0, 255), 0);

    ecs_fini(world);
}

void Views_camera_scene(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t outer = ecs_new(world);
    ecs_set(world, outer, Scene, {0});
    ecs_entity_t inner = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_set(world, inner, Scene, {0});
    ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, inner);
    ecs_entity_t red = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_set(world, red, Box, {2, 2, 2});
    ecs_set(world, red, Position, {0, 0, 0});
    ecs_set(world, red, Rgb, {0, 0, 0});
    ecs_set(world, red, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, red, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t green = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, green, Box, {2, 2, 2});
    ecs_set(world, green, Position, {0, 0, 0});
    ecs_set(world, green, Rgb, {0, 0, 0});
    ecs_set(world, green, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, green, Emissive, {.strength = 1, .color = {0, 255, 0}});
    ecs_entity_t blue = ecs_new(world);
    ecs_set(world, blue, Box, {2, 2, 2});
    ecs_set(world, blue, Position, {0, 0, 0});
    ecs_set(world, blue, Rgb, {0, 0, 0});
    ecs_set(world, blue, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, blue, Emissive, {.strength = 1, .color = {0, 0, 255}});
    ecs_entity_t c_outer = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_set(world, c_outer, Position, {0, 0, 5});
    ecs_set(world, c_outer, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t c_inner = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, c_inner, Position, {0, 0, 5});
    ecs_set(world, c_inner, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t c_default = ecs_new(world);
    ecs_set(world, c_default, Position, {0, 0, 5});
    ecs_set(world, c_default, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = c_outer, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 16, .height = 16, .write_to_file = "scene.png",
        .resolution_scale = 1});
    ecs_entity_t cameras[3] = {c_outer, c_inner, c_default};
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_set(world, view, View, {
            .camera = cameras[i], .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
        ecs_progress(world, 0);
        box_render_sync(world);

        char *path = flecs_asprintf("%s/scene.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
        int w = 0, h = 0, n = 0;
        unsigned char *px = stbi_load(path, &w, &h, &n, 4);
        test_assert(px != NULL);
        unsigned char *center = &px[(8 * 16 + 8) * 4];
        test_assert((center[0] > 200) == (i == 0));
        test_assert((center[1] > 200) == (i == 1));
        test_assert((center[2] > 200) == (i == 2));
        stbi_image_free(px);
        ecs_os_free(path);
    }

    ecs_fini(world);
}

void Views_view_matrix(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_set(world, camera, Position, {1, 2, 3});
    ecs_set(world, camera, Rotation, {0, GLM_PI_2f, 0});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t ahead = ecs_new(world);
    ecs_set(world, ahead, Position, {-3, 2, 3});
    ecs_set(world, ahead, Box, {1, 1, 1});
    ecs_set(world, ahead, Rgb, {0, 0, 0});
    ecs_set(world, ahead, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, ahead, Emissive, {.strength = 1, .color = {255, 0, 0}});
    ecs_entity_t behind = ecs_new(world);
    ecs_set(world, behind, Position, {5, 2, 3});
    ecs_set(world, behind, Box, {1, 1, 1});
    ecs_set(world, behind, Rgb, {0, 0, 0});
    ecs_set(world, behind, PbrMaterial, {.metallic = 1, .roughness = 1});
    ecs_set(world, behind, Emissive, {.strength = 1, .color = {0, 255, 0}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 16, .height = 16, .write_to_file = "look.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/look.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, i, green = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *center = &px[(8 * 16 + 8) * 4];
    test_assert(center[0] > 200);
    test_assert(center[1] < 40);
    test_int(px[0], 0);
    for (i = 0; i < w * h; i ++) {
        green += px[i * 4 + 1] > 40;
    }

    test_int(green, 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Views_no_camera_skipped(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t not_camera = ecs_new(world);
    ecs_entity_t dead = ecs_new(world);
    ecs_delete(world, dead);
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, View, {
        .camera = camera, .clear = {.enabled = true, .color = {255, 0, 0, 255}}});
    ecs_entity_t b1 = ecs_new(world);
    ecs_set(world, b1, View, {.clear = {.enabled = true, .color = {0, 255, 0, 255}}});
    ecs_entity_t b2 = ecs_new(world);
    ecs_set(world, b2, View, {
        .camera = not_camera, .clear = {.enabled = true, .color = {0, 255, 0, 255}}});
    ecs_entity_t b3 = ecs_new(world);
    ecs_set(world, b3, View, {
        .camera = dead, .clear = {.enabled = true, .color = {0, 255, 0, 255}}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 8, .height = 8, .write_to_file = "skip.png",
        .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/skip.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, i, wrong = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0; i < w * h; i ++) {
        wrong += px[i * 4] != 255 || px[i * 4 + 1] || px[i * 4 + 2];
    }

    test_int(wrong, 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}
