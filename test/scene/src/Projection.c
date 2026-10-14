/**
 * @file Projection.c
 * @brief Cross scene projection: an entity placed at the projection of a target through a view.
 *
 * Without a surface a view projects for the default 1280x720 surface; the 3D camera sits 10
 * units in front of the origin looking down -Z.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Projection_perspective(void) {
    // scenes/projection
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {2, 1, 0});
    ecs_entity_t label = ecs_new(world);
    ecs_set(world, label, Position, {0, 0, 3});
    ecs_set(world, label, ProjectedAnchor, {.target = target, .view = view});
    ecs_progress(world, 0);

    float focal = 1.0f / tanf(glm_rad(30.0f));
    float aspect = 1280.0f / 720.0f;
    const Position *p = ecs_get(world, label, Position);
    test_assert(fabsf(p->x - focal / aspect * 0.2f * 640.0f) < 0.01f);
    test_assert(fabsf(p->y - focal * 0.1f * 360.0f) < 0.01f);
    test_assert(p->z == 3);
    test_assert(!ecs_has(world, label, ProjectedHidden));

    ecs_fini(world);
}

void Projection_offset(void) {
    // scenes/projection
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {1, 0, 0});
    ecs_entity_t target = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, target, Position, {1, -1, 0});
    ecs_entity_t label = ecs_new(world);
    ecs_set(world, label, ProjectedAnchor, {
        .target = target, .view = view, .offset = {0, 2, 0}});
    ecs_progress(world, 0);

    float focal = 1.0f / tanf(glm_rad(30.0f));
    float aspect = 1280.0f / 720.0f;
    const Position *p = ecs_get(world, label, Position);
    test_assert(fabsf(p->x - focal / aspect * 0.2f * 640.0f) < 0.01f);
    test_assert(fabsf(p->y - focal * 0.1f * 360.0f) < 0.01f);

    ecs_fini(world);
}

void Projection_orthographic(void) {
    // scenes/projection
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {2, 1, -5});
    ecs_entity_t label = ecs_new(world);
    ecs_set(world, label, ProjectedAnchor, {.target = target, .view = view});
    ecs_progress(world, 0);

    const Position *p = ecs_get(world, label, Position);
    test_assert(fabsf(p->x - 144.0f) < 0.01f);
    test_assert(fabsf(p->y - 72.0f) < 0.01f);

    ecs_fini(world);
}

void Projection_surface_size(void) {
    // scenes/projection
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 400, .height = 200, .resolution_scale = 1});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {50, -20, 0});
    ecs_entity_t label = ecs_new(world);
    ecs_set(world, label, ProjectedAnchor, {.target = target, .view = view});
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    const Position *p = ecs_get(world, label, Position);
    test_assert(fabsf(p->x - 50.0f) < 0.01f);
    test_assert(fabsf(p->y + 20.0f) < 0.01f);

    ecs_set(world, target, Position, {150, 0, 0});
    ecs_progress(world, 0);
    test_assert(fabsf(ecs_get(world, label, Position)->x - 150.0f) < 0.01f);
    test_assert(!ecs_has(world, label, ProjectedHidden));

    ecs_set(world, target, Position, {250, 0, 0});
    ecs_progress(world, 0);
    test_assert(ecs_has(world, label, ProjectedHidden));

    box_render_sync(world);
    ecs_fini(world);
}

void Projection_anchor_in_ui_scene(void) {
    // scenes/projection
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {-2, -1, 0});
    ecs_entity_t ui = ecs_new(world);
    ecs_set(world, ui, Scene, {0});
    ecs_entity_t ui_camera = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, ui_camera, Position, {0, 0, 100});
    ecs_set(world, ui_camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t ui_view = ecs_new(world);
    ecs_set(world, ui_view, View, {.camera = ui_camera});
    ecs_entity_t label = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, label, Position, {0, 0, 1});
    ecs_set(world, label, ProjectedAnchor, {.target = target, .view = view});
    ecs_progress(world, 0);

    const Position *p = ecs_get(world, label, Position);
    test_assert(fabsf(p->x + 144.0f) < 0.01f);
    test_assert(fabsf(p->y + 72.0f) < 0.01f);
    const WorldTransform *t = ecs_get(world, label, WorldTransform);
    test_assert(fabsf(t->m[3][0] + 144.0f) < 0.01f);
    test_assert(fabsf(t->m[3][1] + 72.0f) < 0.01f);
    test_assert(fabsf(t->m[3][2] - 1.0f) < 0.01f);
    const Position *tp = ecs_get(world, target, Position);
    test_assert(tp->x == -2 && tp->y == -1 && tp->z == 0);

    ecs_fini(world);
}

void Projection_follows_target_same_frame(void) {
    // scenes/projection
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {0, 0, 0});
    ecs_entity_t label = ecs_new(world);
    ecs_set(world, label, ProjectedAnchor, {.target = target, .view = view});
    ecs_progress(world, 0);

    const WorldTransform *t = ecs_get(world, label, WorldTransform);
    test_assert(fabsf(t->m[3][0]) < 0.01f);
    test_assert(fabsf(t->m[3][1]) < 0.01f);

    ecs_set(world, target, Position, {2, 1, 0});
    ecs_progress(world, 0);
    const Position *p = ecs_get(world, label, Position);
    test_assert(fabsf(p->x - 144.0f) < 0.01f);
    test_assert(fabsf(p->y - 72.0f) < 0.01f);
    t = ecs_get(world, label, WorldTransform);
    test_assert(fabsf(t->m[3][0] - 144.0f) < 0.01f);
    test_assert(fabsf(t->m[3][1] - 72.0f) < 0.01f);

    ecs_add(world, target, DynamicTransform);
    ecs_set(world, target, Position, {-2, 1, 0});
    ecs_progress(world, 0);
    t = ecs_get(world, label, WorldTransform);
    test_assert(fabsf(t->m[3][0] + 144.0f) < 0.01f);
    test_assert(fabsf(t->m[3][1] - 72.0f) < 0.01f);

    ecs_fini(world);
}

void Projection_follows_camera(void) {
    // scenes/projection
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t rig = ecs_new(world);
    ecs_set(world, rig, Position, {0, 0, 0});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, rig);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {0, 0, 0});
    ecs_entity_t label = ecs_new(world);
    ecs_set(world, label, ProjectedAnchor, {.target = target, .view = view});
    ecs_progress(world, 0);
    test_assert(fabsf(ecs_get(world, label, Position)->x) < 0.01f);

    ecs_set(world, rig, Position, {-2, -1, 0});
    ecs_progress(world, 0);
    const Position *p = ecs_get(world, label, Position);
    test_assert(fabsf(p->x - 144.0f) < 0.01f);
    test_assert(fabsf(p->y - 72.0f) < 0.01f);

    ecs_set(world, rig, Rotation, {0, (float)GLM_PI, 0});
    ecs_progress(world, 0);
    p = ecs_get(world, label, Position);
    test_assert(fabsf(p->x + 144.0f) < 0.01f);
    test_assert(fabsf(p->y - 72.0f) < 0.01f);

    ecs_fini(world);
}

void Projection_subtree_same_frame(void) {
    // scenes/projection
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {0, 0, 0});
    ecs_entity_t ui = ecs_new(world);
    ecs_set(world, ui, Scene, {0});
    ecs_entity_t label = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, label, ProjectedAnchor, {.target = target, .view = view});
    ecs_set(world, label, Scale, {2, 2, 2});
    ecs_entity_t text = ecs_new_w_pair(world, EcsChildOf, label);
    ecs_set(world, text, Position, {5, 0, 0});
    ecs_entity_t mark = ecs_new_w_pair(world, EcsChildOf, text);
    ecs_set(world, mark, Position, {0, 3, 0});
    ecs_progress(world, 0);

    ecs_set(world, target, Position, {2, 1, 0});
    ecs_progress(world, 0);
    const WorldTransform *t = ecs_get(world, text, WorldTransform);
    test_assert(fabsf(t->m[3][0] - 154.0f) < 0.01f);
    test_assert(fabsf(t->m[3][1] - 72.0f) < 0.01f);
    t = ecs_get(world, mark, WorldTransform);
    test_assert(fabsf(t->m[3][0] - 154.0f) < 0.01f);
    test_assert(fabsf(t->m[3][1] - 78.0f) < 0.01f);

    ecs_fini(world);
}

void Projection_dynamic_anchor(void) {
    // scenes/projection
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {0, 0, 0});
    ecs_entity_t label = ecs_new(world);
    ecs_add(world, label, DynamicTransform);
    ecs_set(world, label, ProjectedAnchor, {.target = target, .view = view});
    ecs_entity_t text = ecs_new_w_pair(world, EcsChildOf, label);
    ecs_set(world, text, Position, {5, 0, 0});
    ecs_progress(world, 0);

    ecs_set(world, target, Position, {2, 1, 0});
    ecs_progress(world, 0);
    const WorldTransform *t = ecs_get(world, label, WorldTransform);
    test_assert(fabsf(t->m[3][0] - 144.0f) < 0.01f);
    test_assert(fabsf(t->m[3][1] - 72.0f) < 0.01f);
    t = ecs_get(world, text, WorldTransform);
    test_assert(fabsf(t->m[3][0] - 149.0f) < 0.01f);
    test_assert(fabsf(t->m[3][1] - 72.0f) < 0.01f);

    ecs_fini(world);
}

void Projection_behind_camera_hidden(void) {
    // scenes/projection
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {0, 0, 0});
    ecs_entity_t label = ecs_new(world);
    ecs_set(world, label, ProjectedAnchor, {.target = target, .view = view});
    ecs_entity_t text = ecs_new_w_pair(world, EcsChildOf, label);
    ecs_set(world, text, Position, {5, 0, 0});
    ecs_set(world, text, Box, {1, 1, 1});
    ecs_progress(world, 0);
    test_assert(!ecs_has(world, label, ProjectedHidden));

    ecs_set(world, target, Position, {0, 0, 20});
    ecs_progress(world, 0);
    test_assert(ecs_has(world, label, ProjectedHidden));
    const Position *p = ecs_get(world, label, Position);
    test_assert(fabsf(p->x) < 0.01f && fabsf(p->y) < 0.01f);
    const WorldTransform *t = ecs_get(world, label, WorldTransform);
    test_assert(glm_vec3_norm(ECS_CONST_CAST(float*, t->m[0])) < 1e-6f);
    test_assert(glm_vec3_norm(ECS_CONST_CAST(float*, t->m[1])) < 1e-6f);
    test_assert(glm_vec3_norm(ECS_CONST_CAST(float*, t->m[2])) < 1e-6f);
    t = ecs_get(world, text, WorldTransform);
    test_assert(glm_vec3_norm(ECS_CONST_CAST(float*, t->m[0])) < 1e-6f);
    test_assert(fabsf(t->m[3][0]) < 0.01f);

    ecs_progress(world, 0);
    test_assert(ecs_has(world, label, ProjectedHidden));
    t = ecs_get(world, text, WorldTransform);
    test_assert(glm_vec3_norm(ECS_CONST_CAST(float*, t->m[0])) < 1e-6f);

    ecs_fini(world);
}

void Projection_off_screen_hidden(void) {
    // scenes/projection
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .far_ = 20, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {9.5f, 0, 0});
    ecs_entity_t label = ecs_new(world);
    ecs_set(world, label, ProjectedAnchor, {.target = target, .view = view});
    ecs_progress(world, 0);
    test_assert(ecs_has(world, label, ProjectedHidden));

    ecs_set(world, target, Position, {0, 5.5f, 0});
    ecs_progress(world, 0);
    test_assert(ecs_has(world, label, ProjectedHidden));

    ecs_set(world, target, Position, {0, 0, -15});
    ecs_progress(world, 0);
    test_assert(ecs_has(world, label, ProjectedHidden));

    ecs_set(world, target, Position, {8.5f, -4.5f, -5});
    ecs_progress(world, 0);
    test_assert(!ecs_has(world, label, ProjectedHidden));

    ecs_fini(world);
}

void Projection_shown_again(void) {
    // scenes/projection
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {0, 0, 20});
    ecs_entity_t label = ecs_new(world);
    ecs_set(world, label, ProjectedAnchor, {.target = target, .view = view});
    ecs_set(world, label, Scale, {2, 2, 2});
    ecs_entity_t text = ecs_new_w_pair(world, EcsChildOf, label);
    ecs_set(world, text, Position, {5, 0, 0});
    ecs_progress(world, 0);
    test_assert(ecs_has(world, label, ProjectedHidden));

    ecs_set(world, target, Position, {2, 1, 0});
    ecs_progress(world, 0);
    test_assert(!ecs_has(world, label, ProjectedHidden));
    const WorldTransform *t = ecs_get(world, label, WorldTransform);
    test_assert(fabsf(t->m[0][0] - 2.0f) < 1e-6f);
    test_assert(fabsf(t->m[1][1] - 2.0f) < 1e-6f);
    test_assert(fabsf(t->m[3][0] - 144.0f) < 0.01f);
    t = ecs_get(world, text, WorldTransform);
    test_assert(fabsf(t->m[0][0] - 2.0f) < 1e-6f);
    test_assert(fabsf(t->m[3][0] - 154.0f) < 0.01f);
    test_assert(fabsf(t->m[3][1] - 72.0f) < 0.01f);

    ecs_fini(world);
}

void Projection_missing_view_hidden(void) {
    // scenes/projection
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {0, 0, 0});
    ecs_entity_t label = ecs_new(world);
    ecs_set(world, label, ProjectedAnchor, {.target = target});
    ecs_progress(world, 0);
    test_assert(ecs_has(world, label, ProjectedHidden));

    ecs_set(world, label, ProjectedAnchor, {.target = target, .view = view});
    ecs_progress(world, 0);
    test_assert(!ecs_has(world, label, ProjectedHidden));

    ecs_set(world, view, View, {.lighting.enabled = true});
    ecs_progress(world, 0);
    test_assert(ecs_has(world, label, ProjectedHidden));

    ecs_fini(world);
}

void Projection_deleted_target_hidden(void) {
    // scenes/projection
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {2, 1, 0});
    ecs_entity_t label = ecs_new(world);
    ecs_set(world, label, ProjectedAnchor, {.target = target, .view = view});
    ecs_progress(world, 0);
    test_assert(!ecs_has(world, label, ProjectedHidden));

    ecs_delete(world, target);
    ecs_progress(world, 0);
    test_assert(ecs_is_alive(world, label));
    test_assert(ecs_has(world, label, ProjectedHidden));
    const Position *p = ecs_get(world, label, Position);
    test_assert(fabsf(p->x - 144.0f) < 0.01f);

    ecs_fini(world);
}

void Projection_from_script(void) {
    // scenes/projection
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "projection",
        "using box.transform\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.projection\n"
        "cam {\n"
        "  Position: {0, 0, 10}\n"
        "  Camera: {ortho: true, ortho_height: 10}\n"
        "}\n"
        "main_view {\n"
        "  View: {camera: cam}\n"
        "}\n"
        "hero {\n"
        "  Position: {2, 0, 0}\n"
        "}\n"
        "ui {\n"
        "  Scene: {}\n"
        "  nameplate {\n"
        "    ProjectedAnchor: {target: hero, view: main_view, offset: {0, 1, 0}}\n"
        "  }\n"
        "}\n", NULL) == 0);
    ecs_progress(world, 0);

    ecs_entity_t label = ecs_lookup(world, "ui.nameplate");
    test_assert(label != 0);
    const Position *p = ecs_get(world, label, Position);
    test_assert(p != NULL);
    test_assert(fabsf(p->x - 144.0f) < 0.01f);
    test_assert(fabsf(p->y - 72.0f) < 0.01f);

    ecs_fini(world);
}

void Projection_drawn_at_projection(void) {
    // scenes/projection
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 8, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {2, 1, 0});
    ecs_entity_t ui = ecs_new(world);
    ecs_set(world, ui, Scene, {0});
    ecs_entity_t ui_camera = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, ui_camera, Position, {0, 0, 10});
    ecs_set(world, ui_camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t ui_view = ecs_new(world);
    ecs_set(world, ui_view, View, {.camera = ui_camera, .lighting.enabled = false});
    ecs_entity_t label = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, label, ProjectedAnchor, {.target = target, .view = view});
    ecs_entity_t plate = ecs_new_w_pair(world, EcsChildOf, label);
    ecs_set(world, plate, Position, {0, 0, 0});
    ecs_set(world, plate, Quad, {4, 4});
    ecs_set(world, plate, Rgb, {255, 0, 0});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {
        .width = 64, .height = 32, .write_to_file = "projection.png", .resolution_scale = 1});
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf(
        "%s/projection.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_int(w, 64);
    test_assert(px[(12 * 64 + 40) * 4] > 200);
    test_assert(px[(16 * 64 + 32) * 4] < 40);
    stbi_image_free(px);

    ecs_set(world, target, Position, {2, 1, 20});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(ecs_has(world, label, ProjectedHidden));
    test_assert(px[(12 * 64 + 40) * 4] < 40);
    stbi_image_free(px);

    ecs_set(world, target, Position, {-2, -1, 0});
    ecs_progress(world, 0);
    box_render_sync(world);
    px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    test_assert(px[(20 * 64 + 24) * 4] > 200);
    test_assert(px[(12 * 64 + 40) * 4] < 40);
    stbi_image_free(px);

    ecs_os_free(path);
    ecs_fini(world);
}
