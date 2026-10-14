/**
 * @file Forward.c
 * @brief Tests for the forward pass of the webgpu backend, checked on PNG captures.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Forward_simple_scene(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {10, 10, 10, 255}}});
    ecs_entity_t box = ecs_new(world);
    ecs_set(world, box, Position, {-1.5f, 0, 0});
    ecs_set(world, box, Box, {1.5f, 1.5f, 1.5f});
    ecs_set(world, box, Rgb, {255, 0, 0});
    ecs_entity_t ball = ecs_new(world);
    ecs_set(world, ball, Position, {1.5f, 0, 0});
    ecs_set(world, ball, Sphere, {.segments = 24, .smooth = true, .radius = 0.8f});
    ecs_set(world, ball, Rgb, {0, 0, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 32, .write_to_file = "scene.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/scene.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 64);
    test_int(h, 32);
    unsigned char *corner = &px[0];
    unsigned char *red = &px[(16 * 64 + 25) * 4];
    unsigned char *blue = &px[(16 * 64 + 39) * 4];
    test_int(corner[0], 10);
    test_int(corner[1], 10);
    test_int(corner[2], 10);
    test_assert(red[0] > 100);
    test_assert(red[1] < 40);
    test_assert(red[2] < 40);
    test_assert(blue[2] > 100);
    test_assert(blue[0] < 40);
    test_assert(blue[1] < 40);

    box_render_gpu_stats_t stats;
    box_render_gpu_stats(world, &stats);
    test_int(stats.passes, 1);
    test_int(stats.draws, 2);
    test_int(stats.meshes, 2);
    test_int(stats.buffers, 2);
    test_int(stats.shaders, 2);
    test_int(stats.errors, 0);
    test_int(stats.shader_errors, 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Forward_depth_nearer_wins(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.5f, .far_ = 50});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t near = ecs_new(world);
    ecs_set(world, near, Position, {0, 0, 2});
    ecs_set(world, near, Box, {1, 1, 1});
    ecs_set(world, near, Rgb, {0, 255, 0});
    ecs_entity_t far = ecs_new(world);
    ecs_set(world, far, Position, {0, 0, 0});
    ecs_set(world, far, Box, {2, 2, 2});
    ecs_set(world, far, Rgb, {255, 0, 0});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 64, .height = 64, .write_to_file = "depth.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/depth.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *center = &px[(32 * 64 + 32) * 4];
    unsigned char *edge = &px[(32 * 64 + 41) * 4];
    unsigned char *corner = &px[(2 * 64 + 2) * 4];
    test_assert(center[1] > 100);
    test_assert(center[0] < 40);
    test_assert(edge[0] > 100);
    test_assert(edge[1] < 40);
    test_int(corner[0], 0);
    test_int(corner[1], 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Forward_view_order(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t c1 = ecs_new(world);
    ecs_set(world, c1, Position, {0, 0, 5});
    ecs_set(world, c1, Camera, {.fov = 60});
    ecs_entity_t c2 = ecs_new(world);
    ecs_set(world, c2, Position, {0, 0, 5});
    ecs_set(world, c2, Rotation, {0, GLM_PIf, 0});
    ecs_set(world, c2, Camera, {.fov = 60});
    ecs_entity_t v1 = ecs_new(world);
    ecs_set(world, v1, View, {
        .camera = c1, .clear = {.enabled = true, .color = {255, 0, 0, 255}}});
    ecs_entity_t v2 = ecs_new(world);
    ecs_set(world, v2, View, {.camera = c2, .clear = {.color = {0, 0, 255, 255}}});
    ecs_entity_t box = ecs_new(world);
    ecs_set(world, box, Position, {0, 0, 0});
    ecs_set(world, box, Box, {2, 2, 2});
    ecs_set(world, box, Rgb, {0, 255, 0});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 32, .height = 32, .write_to_file = "order.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/order.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, i, wrong = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *center = &px[(16 * 32 + 16) * 4];
    test_assert(center[1] > 100);
    test_assert(center[0] < 40);
    test_int(px[0], 255);
    test_int(px[1], 0);
    test_int(px[2], 0);
    stbi_image_free(px);

    View *v = ecs_get_mut(world, v2, View);
    v->clear.enabled = true;
    ecs_modified(world, v2, View);
    ecs_progress(world, 0);
    box_render_sync(world);

    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0; i < w * h; i ++) {
        wrong += px[i * 4] || px[i * 4 + 1] || px[i * 4 + 2] != 255;
    }

    test_int(wrong, 0);
    box_render_gpu_stats_t stats;
    box_render_gpu_stats(world, &stats);
    test_int(stats.passes, 4);
    test_int(stats.errors, 0);
    test_assert(v1 != 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Forward_scene_filter(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Scene, {0});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t box = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, box, Position, {0, 0, 0});
    ecs_set(world, box, Box, {2, 2, 2});
    ecs_set(world, box, Rgb, {255, 255, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 32, .height = 32, .write_to_file = "scene.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/scene.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, i, lit = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0; i < w * h; i ++) {
        lit += px[i * 4] || px[i * 4 + 1] || px[i * 4 + 2];
    }

    test_int(lit, 0);
    stbi_image_free(px);

    ecs_entity_t inner = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, inner, Position, {0, 0, 5});
    ecs_set(world, inner, Camera, {.fov = 60});
    ecs_entity_t overlay = ecs_new(world);
    ecs_set(world, overlay, View, {.camera = inner});
    ecs_progress(world, 0);
    box_render_sync(world);

    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *center = &px[(16 * 32 + 16) * 4];
    test_assert(center[0] > 100);
    test_int(px[0], 0);
    box_render_gpu_stats_t stats;
    box_render_gpu_stats(world, &stats);
    test_int(stats.draws, 1);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Forward_ortho_camera(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 4});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t box = ecs_new(world);
    ecs_set(world, box, Position, {0, 0, 0});
    ecs_set(world, box, Box, {2, 2, 2});
    ecs_set(world, box, Rgb, {255, 255, 255});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 32, .height = 32, .write_to_file = "ortho.png"});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/ortho.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, x, y, lit = 0, wrong = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            bool on = px[(y * w + x) * 4] > 0;
            bool inside = x >= 8 && x < 24 && y >= 8 && y < 24;
            lit += on;
            wrong += on != inside;
        }
    }

    test_int(lit, 256);
    test_int(wrong, 0);

    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Forward_mesh_shader_on_demand(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Surface, {.width = 16, .height = 16});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_gpu_stats_t stats;
    box_render_gpu_stats(world, &stats);
    test_int(stats.shaders, 1);
    test_int(stats.meshes, 0);

    ecs_entity_t box = ecs_new(world);
    ecs_set(world, box, Box, {1, 1, 1});
    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_gpu_stats(world, &stats);
    test_int(stats.shaders, 2);
    test_int(stats.meshes, 1);
    test_int(stats.draws, 1);
    test_int(stats.shader_errors, 0);
    ecs_entity_t mesh = ecs_lookup(world, "box.shader.builtin.mesh");
    test_bool(ecs_get(world, mesh, ShaderStatus)->ok, true);

    ecs_delete(world, box);
    ecs_progress(world, 0);
    box_render_sync(world);
    box_render_gpu_stats(world, &stats);
    test_int(stats.meshes, 0);
    test_int(stats.draws, 1);
    test_int(stats.errors, 0);

    ecs_fini(world);
}

void Forward_target_view_root(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    char *path = flecs_asprintf("%s/root.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    ecs_entity_t root = ecs_new(world);

    box_render_backend_t gpu = box_render_gpu_backend();
    test_int(gpu.init(gpu.ctx), 0);
    box_render_packet_t p;
    box_render_packet_init(&p);
    box_render_target_desc_t *desc = box_render_packet_resource(
        &p, BOX_RENDER_TARGET, BOX_RENDER_CREATE, 1, sizeof(box_render_target_desc_t));
    ecs_os_memset_t(desc, 0, box_render_target_desc_t);
    desc->width = 8;
    desc->height = 8;
    box_render_view_t *v = box_render_packet_view(&p);
    v->view = 1000;
    v->parent = root;
    v->camera_data.fov = 60;
    glm_mat4_identity(v->view_matrix);
    v->clear = (ClearSettings){.enabled = true, .color = {0, 255, 0, 255}};
    v = box_render_packet_view(&p);
    v->view = 1001;
    v->camera_data.fov = 60;
    glm_mat4_identity(v->view_matrix);
    v->clear = (ClearSettings){.enabled = true, .color = {255, 0, 0, 255}};
    box_render_target_t *t = box_render_packet_target(&p, 1, path);
    t->view_root = root;
    gpu.render(gpu.ctx, &p);

    int w = 0, h = 0, n = 0, i, wrong = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0; i < w * h; i ++) {
        wrong += px[i * 4] || px[i * 4 + 1] != 255 || px[i * 4 + 2];
    }

    test_int(wrong, 0);
    stbi_image_free(px);

    ecs_vec_clear(&p.resources);
    t = ecs_vec_first_t(&p.targets, box_render_target_t);
    t->view_root = 0;
    gpu.render(gpu.ctx, &p);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    for (i = 0, wrong = 0; i < w * h; i ++) {
        wrong += px[i * 4] != 255 || px[i * 4 + 1] || px[i * 4 + 2];
    }

    test_int(wrong, 0);
    stbi_image_free(px);

    gpu.fini(gpu.ctx);
    gpu.free(gpu.ctx);
    box_render_packet_fini(&p);
    ecs_os_free(path);
    ecs_fini(world);
}
