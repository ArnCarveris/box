/**
 * @file Views.c
 * @brief Tests for the views of a packet: render order, parent, scene, clear and matrices.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

void Views_creation_order(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ECS_TAG(world, Moved);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, View, {.camera = camera});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, View, {.camera = camera});
    ecs_entity_t c = ecs_new(world);
    ecs_set(world, c, View, {.camera = camera});
    ecs_add(world, a, Moved);
    ecs_add(world, b, Moved);
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.views), 3);
    box_render_view_t *v = ecs_vec_first_t(&null->last.views, box_render_view_t);
    test_uint(v[0].view, a);
    test_uint(v[1].view, b);
    test_uint(v[2].view, c);

    ecs_fini(world);
    box_render_null_free(null);
}

void Views_parent(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t top = ecs_new(world);
    ecs_set(world, top, View, {.camera = camera});
    ecs_entity_t root = ecs_new(world);
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, child, View, {.camera = camera});
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.views), 2);
    box_render_view_t *v = ecs_vec_first_t(&null->last.views, box_render_view_t);
    test_uint(v[0].view, top);
    test_uint(v[0].parent, 0);
    test_uint(v[1].view, child);
    test_uint(v[1].parent, root);

    ecs_fini(world);
    box_render_null_free(null);
}

void Views_camera_scene(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t outer = ecs_new(world);
    ecs_set(world, outer, Scene, {0});
    ecs_entity_t inner = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_set(world, inner, Scene, {0});
    ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, inner);
    ecs_entity_t c1 = ecs_new(world);
    ecs_set(world, c1, Camera, {.fov = 60});
    ecs_entity_t c2 = ecs_new_w_pair(world, EcsChildOf, outer);
    ecs_set(world, c2, Camera, {.fov = 60});
    ecs_entity_t c3 = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, c3, Camera, {.fov = 60});
    ecs_entity_t v1 = ecs_new(world);
    ecs_set(world, v1, View, {.camera = c1});
    ecs_entity_t v2 = ecs_new(world);
    ecs_set(world, v2, View, {.camera = c2});
    ecs_entity_t v3 = ecs_new(world);
    ecs_set(world, v3, View, {.camera = c3});
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.views), 3);
    box_render_view_t *v = ecs_vec_first_t(&null->last.views, box_render_view_t);
    test_uint(v[0].view, v1);
    test_uint(v[0].scene, 0);
    test_uint(v[1].view, v2);
    test_uint(v[1].scene, outer);
    test_uint(v[2].view, v3);
    test_uint(v[2].scene, inner);

    ecs_fini(world);
    box_render_null_free(null);
}

void Views_clear_settings(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, View, {
        .camera = camera, .clear = {.enabled = true, .color = {1, 2, 3, 255}}});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, View, {.camera = camera, .clear = {.color = {4, 5, 6, 255}}});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_view_t *v = ecs_vec_first_t(&null->last.views, box_render_view_t);
    test_bool(v[0].clear.enabled, true);
    test_int(v[0].clear.color.b, 3);
    test_bool(v[1].clear.enabled, false);
    test_int(v[1].clear.color.b, 6);

    View *view = ecs_get_mut(world, b, View);
    view->clear.enabled = true;
    ecs_modified(world, b, View);
    ecs_progress(world, 0);
    box_render_sync(world);
    v = ecs_vec_first_t(&null->last.views, box_render_view_t);
    test_bool(v[1].clear.enabled, true);

    ecs_fini(world);
    box_render_null_free(null);
}

void Views_view_matrix(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_set(world, camera, Position, {1, 2, 3});
    ecs_set(world, camera, Rotation, {0, GLM_PI_2f, 0});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_progress(world, 0);
    box_render_sync(world);

    box_render_view_t *v = ecs_vec_first_t(&null->last.views, box_render_view_t);
    vec4 eye = {1, 2, 3, 1}, ahead = {0, 2, 3, 1}, out;
    glm_mat4_mulv(v->view_matrix, eye, out);
    test_assert(glm_vec3_norm(out) < 1e-5f);
    glm_mat4_mulv(v->view_matrix, ahead, out);
    test_assert(fabsf(out[0]) < 1e-5f);
    test_assert(fabsf(out[1]) < 1e-5f);
    test_flt(out[2], -1.0f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Views_no_camera_skipped(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, View, {.camera = camera});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, View, {0});
    ecs_entity_t c = ecs_new(world);
    ecs_set(world, c, View, {.camera = camera});
    ecs_progress(world, 0);
    box_render_sync(world);

    test_int(ecs_vec_count(&null->last.views), 2);
    box_render_view_t *v = ecs_vec_first_t(&null->last.views, box_render_view_t);
    test_uint(v[0].view, a);
    test_uint(v[1].view, c);
    test_assert(b != 0);

    ecs_fini(world);
    box_render_null_free(null);
}
