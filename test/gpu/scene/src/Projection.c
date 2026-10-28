/**
 * @file Projection.c
 * @brief Cross scene projection: an entity placed at the projection of a target through a view.
 *
 * Without a surface a view projects for the default 1280x720 surface; the 3D camera sits 10
 * units in front of the origin looking down -Z.
 */

#include <box.h>
#include <bake_test.h>
#include <float.h>
#include <math.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Entity a counter watches and the number of times it was seen */
typedef struct projection_count_t {
    ecs_entity_t entity; /**< The watched entity */
    int32_t count;       /**< Times the entity was seen */
} projection_count_t;

/** Count the watched entity among the entities whose static transform was computed.
 *
 * @param world The world
 * @param entities The entities
 * @param count Number of entities
 * @param ctx The counter
 */
static void ProjectionCountMoved(
    ecs_world_t *world,
    const ecs_entity_t *entities,
    int32_t count,
    void *ctx)
{
    projection_count_t *c = ctx;
    (void)world;
    for (int32_t i = 0; i < count; i ++) {
        c->count += entities[i] == c->entity;
    }
}

/** Count the watched entity among the entities whose WorldTransform was set.
 *
 * @param it The iterator
 */
static void ProjectionCountSet(
    ecs_iter_t *it)
{
    projection_count_t *c = it->ctx;
    for (int32_t i = 0; i < it->count; i ++) {
        c->count += it->entities[i] == c->entity;
    }
}

/** Project a box the way a ProjectedBox should, for a camera without rotation.
 *
 * Every corner of the box must be in front of the camera. The surface is the default 1280x720.
 *
 * @param world The world
 * @param camera The camera
 * @param center Position of the target
 * @param min Smallest corner of the box from the target
 * @param max Largest corner of the box from the target
 * @param out Receives left, right, top and bottom in pixels, origin at the center, y up
 */
static void projection_box_expected(
    const ecs_world_t *world,
    ecs_entity_t camera,
    const vec3 center,
    const vec3 min,
    const vec3 max,
    float out[4])
{
    const Camera *c = ecs_get(world, camera, Camera);
    const Position *eye = ecs_get(world, camera, Position);
    mat4 proj, view, vp;
    box_camera_projection(c, 1280, 720, proj);
    glm_translate_make(view, (vec3){-eye->x, -eye->y, -eye->z});
    glm_mat4_mul(proj, view, vp);
    out[0] = out[3] = FLT_MAX;
    out[1] = out[2] = -FLT_MAX;
    for (int32_t i = 0; i < 8; i ++) {
        vec4 point = {
            center[0] + ((i & 1) ? max[0] : min[0]),
            center[1] + ((i & 2) ? max[1] : min[1]),
            center[2] + ((i & 4) ? max[2] : min[2]),
            1.0f
        };
        vec4 clip;
        glm_mat4_mulv(vp, point, clip);
        float x = clip[0] / clip[3] * 640.0f;
        float y = clip[1] / clip[3] * 360.0f;
        out[0] = glm_min(out[0], x);
        out[1] = glm_max(out[1], x);
        out[2] = glm_max(out[2], y);
        out[3] = glm_min(out[3], y);
    }
}

/** Test whether the ProjectedBounds of an entity match left, right, top and bottom.
 *
 * @param world The world
 * @param e The entity
 * @param expect The expected rectangle
 * @return Whether every edge is within 0.05 pixels
 */
static bool projection_bounds_match(
    const ecs_world_t *world,
    ecs_entity_t e,
    const float expect[4])
{
    const ProjectedBounds *b = ecs_get(world, e, ProjectedBounds);

    return b && fabsf(b->left - expect[0]) < 0.05f && fabsf(b->right - expect[1]) < 0.05f &&
        fabsf(b->top - expect[2]) < 0.05f && fabsf(b->bottom - expect[3]) < 0.05f;
}

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

void Projection_surface_points(void) {
    // scenes/projection_points
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 400, .height = 200, .resolution_scale = 1,
        .pixel_ratio = 2});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {150, -20, 0});
    ecs_entity_t label = ecs_new(world);
    ecs_set(world, label, ProjectedAnchor, {.target = target, .view = view});
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    const SurfaceStatus *status = ecs_get(world, surface, SurfaceStatus);
    test_assert(status != NULL);
    test_int(status->width, 800);
    test_int(status->point_width, 400);
    const Position *p = ecs_get(world, label, Position);
    test_assert(fabsf(p->x - 150.0f) < 0.01f);
    test_assert(fabsf(p->y + 20.0f) < 0.01f);
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

void Projection_transformed_once_per_frame(void) {
    // core_fixes/projection_position
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
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, label);
    ecs_set(world, child, Position, {0, 10, 0});
    ecs_progress(world, 0);

    projection_count_t moved = { .entity = label };
    projection_count_t set = { .entity = label };
    projection_count_t child_set = { .entity = child };
    box_transform_on_moved(world, ProjectionCountMoved, &moved, NULL);
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(WorldTransform) }},
        .events = { EcsOnSet },
        .callback = ProjectionCountSet,
        .ctx = &set
    });
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(WorldTransform) }},
        .events = { EcsOnSet },
        .callback = ProjectionCountSet,
        .ctx = &child_set
    });

    for (int32_t frame = 1; frame <= 5; frame ++) {
        ecs_set(world, target, Position, {0.5f * (float)frame, 0, 0});
        ecs_progress(world, 0);
        const WorldTransform *t = ecs_get(world, label, WorldTransform);
        test_assert(fabsf(t->m[3][0] - 36.0f * (float)frame) < 0.01f);
        t = ecs_get(world, child, WorldTransform);
        test_assert(fabsf(t->m[3][0] - 36.0f * (float)frame) < 0.01f);
        test_assert(fabsf(t->m[3][1] - 10.0f) < 0.01f);
    }

    test_int(set.count, 5);
    test_int(child_set.count, 5);

    for (int32_t frame = 0; frame < 3; frame ++) {
        ecs_progress(world, 0);
    }

    test_int(set.count, 5);
    test_int(child_set.count, 5);
    test_int(moved.count, 0);
    test_flt(ecs_get(world, label, WorldTransform)->m[3][0], 180.0f);

    ecs_fini(world);
}

void Projection_box_perspective(void) {
    // engine/projected_bounds
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {1, 0, 0});
    ecs_entity_t bar = ecs_new(world);
    ecs_set(world, bar, Position, {0, 0, 2});
    ecs_set(world, bar, ProjectedAnchor, {.target = target, .view = view});
    ecs_set(world, bar, ProjectedBox, {
        .min = {-0.5f, -0.5f, -0.5f}, .max = {0.5f, 0.5f, 0.5f}, .offset = {0, 6}});
    ecs_progress(world, 0);

    float expect[4];
    projection_box_expected(world, camera, (vec3){1, 0, 0},
        (vec3){-0.5f, -0.5f, -0.5f}, (vec3){0.5f, 0.5f, 0.5f}, expect);
    test_assert(!ecs_has(world, bar, ProjectedHidden));
    test_assert(projection_bounds_match(world, bar, expect));

    float focal = 1.0f / tanf(glm_rad(30.0f));
    float aspect = 1280.0f / 720.0f;
    const ProjectedBounds *b = ecs_get(world, bar, ProjectedBounds);
    test_assert(fabsf(b->right - focal / aspect * 1.5f / 9.5f * 640.0f) < 0.05f);
    test_assert(fabsf(b->left - focal / aspect * 0.5f / 10.5f * 640.0f) < 0.05f);
    test_assert(fabsf(b->top - focal * 0.5f / 9.5f * 360.0f) < 0.05f);
    test_assert(fabsf(b->bottom + focal * 0.5f / 9.5f * 360.0f) < 0.05f);

    const Position *p = ecs_get(world, bar, Position);
    test_assert(fabsf(p->x - (expect[0] + expect[1]) * 0.5f) < 0.05f);
    test_assert(fabsf(p->y - (expect[2] + 6.0f)) < 0.05f);
    test_assert(p->z == 2);
    const WorldTransform *t = ecs_get(world, bar, WorldTransform);
    test_assert(fabsf(t->m[3][0] - p->x) < 0.01f);
    test_assert(fabsf(t->m[3][1] - p->y) < 0.01f);

    ecs_fini(world);
}

void Projection_box_camera_moves(void) {
    // engine/projected_bounds
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {0, 0, 0});
    ecs_entity_t bar = ecs_new(world);
    ecs_set(world, bar, ProjectedAnchor, {.target = target, .view = view});
    ecs_set(world, bar, ProjectedBox, {
        .min = {-0.5f, -0.5f, -0.5f}, .max = {0.5f, 0.5f, 0.5f}});
    ecs_progress(world, 0);

    vec3 min = {-0.5f, -0.5f, -0.5f}, max = {0.5f, 0.5f, 0.5f};
    float far[4], near[4], side[4];
    projection_box_expected(world, camera, (vec3){0, 0, 0}, min, max, far);
    test_assert(projection_bounds_match(world, bar, far));
    test_assert(fabsf(ecs_get(world, bar, Position)->x) < 0.05f);

    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_progress(world, 0);
    projection_box_expected(world, camera, (vec3){0, 0, 0}, min, max, near);
    test_assert(projection_bounds_match(world, bar, near));
    test_assert(near[1] - near[0] > (far[1] - far[0]) * 1.5f);
    test_assert(near[2] - near[3] > (far[2] - far[3]) * 1.5f);

    ecs_set(world, camera, Position, {2, 0, 5});
    ecs_progress(world, 0);
    projection_box_expected(world, camera, (vec3){0, 0, 0}, min, max, side);
    test_assert(projection_bounds_match(world, bar, side));
    test_assert(side[1] < near[1] - 100.0f);
    test_assert(side[0] < near[0] - 100.0f);
    const Position *p = ecs_get(world, bar, Position);
    test_assert(fabsf(p->x - (side[0] + side[1]) * 0.5f) < 0.05f);
    test_assert(p->x < -100.0f);

    ecs_fini(world);
}

void Projection_box_fit_width(void) {
    // engine/projected_bounds
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
    ecs_entity_t bar = ecs_new(world);
    ecs_set(world, bar, ProjectedAnchor, {.target = target, .view = view});
    ecs_set(world, bar, ProjectedBox, {
        .min = {-1, 0, -1}, .max = {1, 2, 1}, .offset = {0, 10}, .fit_width = true});
    ecs_set(world, bar, Layout, {.direction = DirectionRow});
    ecs_set(world, bar, LayoutItem, {
        .width = {.mode = SizeModeFixed, .value = 1},
        .height = {.mode = SizeModeFixed, .value = 8}});
    ecs_set(world, bar, Rect, {1, 8});
    ecs_entity_t fill = ecs_new_w_pair(world, EcsChildOf, bar);
    ecs_set(world, fill, Position, {0, 0, 1});
    ecs_set(world, fill, LayoutItem, {
        .width = {.mode = SizeModeGrow, .value = 1},
        .height = {.mode = SizeModeGrow, .value = 1}});
    ecs_set(world, fill, Rect, {0, 0});
    ecs_entity_t rest = ecs_new_w_pair(world, EcsChildOf, bar);
    ecs_set(world, rest, Position, {0, 0, 1});
    ecs_set(world, rest, LayoutItem, {
        .width = {.mode = SizeModeGrow, .value = 3},
        .height = {.mode = SizeModeGrow, .value = 1}});
    ecs_set(world, rest, Rect, {0, 0});
    ecs_progress(world, 0);

    const ProjectedBounds *b = ecs_get(world, bar, ProjectedBounds);
    test_assert(fabsf(b->left + 72.0f) < 0.01f);
    test_assert(fabsf(b->right - 72.0f) < 0.01f);
    test_assert(fabsf(b->top - 144.0f) < 0.01f);
    test_assert(fabsf(b->bottom) < 0.01f);
    const LayoutItem *item = ecs_get(world, bar, LayoutItem);
    test_int(item->width.mode, SizeModeFixed);
    test_assert(fabsf(item->width.value - 144.0f) < 0.01f);
    test_int(item->height.mode, SizeModeFixed);
    test_assert(fabsf(item->height.value - 8.0f) < 0.01f);
    test_assert(fabsf(ecs_get(world, bar, Rect)->width - 144.0f) < 0.01f);
    test_assert(fabsf(ecs_get(world, bar, LayoutBounds)->width - 144.0f) < 0.01f);
    const Position *p = ecs_get(world, bar, Position);
    test_assert(fabsf(p->x) < 0.01f);
    test_assert(fabsf(p->y - 154.0f) < 0.01f);
    test_assert(fabsf(ecs_get(world, fill, Rect)->width - 36.0f) < 0.01f);
    test_assert(fabsf(ecs_get(world, rest, Rect)->width - 108.0f) < 0.01f);
    test_assert(fabsf(ecs_get(world, fill, Rect)->height - 8.0f) < 0.01f);
    const WorldTransform *t = ecs_get(world, fill, WorldTransform);
    test_assert(fabsf(t->m[3][0] + 54.0f) < 0.01f);
    test_assert(fabsf(t->m[3][1] - 154.0f) < 0.01f);

    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 5, .near_ = 0.1f,
        .far_ = 1000});
    ecs_progress(world, 0);
    test_assert(fabsf(ecs_get(world, bar, Rect)->width - 288.0f) < 0.01f);
    test_assert(fabsf(ecs_get(world, fill, Rect)->width - 72.0f) < 0.01f);
    test_assert(fabsf(ecs_get(world, rest, Rect)->width - 216.0f) < 0.01f);
    t = ecs_get(world, bar, WorldTransform);
    test_assert(fabsf(t->m[3][1] - 298.0f) < 0.01f);
    t = ecs_get(world, fill, WorldTransform);
    test_assert(fabsf(t->m[3][0] + 108.0f) < 0.01f);
    test_assert(fabsf(t->m[3][1] - 298.0f) < 0.01f);
    t = ecs_get(world, rest, WorldTransform);
    test_assert(fabsf(t->m[3][0] - 36.0f) < 0.01f);

    ecs_progress(world, 0);
    test_assert(fabsf(ecs_get(world, fill, Rect)->width - 72.0f) < 0.01f);
    t = ecs_get(world, fill, WorldTransform);
    test_assert(fabsf(t->m[3][0] + 108.0f) < 0.01f);
    test_assert(fabsf(t->m[3][1] - 298.0f) < 0.01f);

    ecs_fini(world);
}

void Projection_box_fit_height(void) {
    // engine/projected_bounds
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {1, 0, 0});
    ecs_entity_t frame = ecs_new(world);
    ecs_set(world, frame, ProjectedAnchor, {.target = target, .view = view});
    ecs_set(world, frame, ProjectedBox, {
        .min = {-0.5f, 0, -0.5f}, .max = {0.5f, 1, 0.5f}, .offset = {0, -36},
        .fit_width = true, .fit_height = true});
    ecs_set(world, frame, LayoutItem, {0});
    ecs_set(world, frame, Rect, {1, 1});
    ecs_progress(world, 0);

    const Rect *r = ecs_get(world, frame, Rect);
    test_assert(fabsf(r->width - 72.0f) < 0.01f);
    test_assert(fabsf(r->height - 72.0f) < 0.01f);
    const Position *p = ecs_get(world, frame, Position);
    test_assert(fabsf(p->x - 72.0f) < 0.01f);
    test_assert(fabsf(p->y - 36.0f) < 0.01f);

    ecs_fini(world);
}

void Projection_box_behind_camera(void) {
    // engine/projected_bounds
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {0, 0, 0});
    ecs_entity_t bar = ecs_new(world);
    ecs_set(world, bar, ProjectedAnchor, {.target = target, .view = view});
    ecs_set(world, bar, ProjectedBox, {
        .min = {-0.5f, -0.5f, -0.5f}, .max = {0.5f, 0.5f, 0.5f}});
    ecs_progress(world, 0);
    test_assert(!ecs_has(world, bar, ProjectedHidden));
    ProjectedBounds before = *ecs_get(world, bar, ProjectedBounds);

    ecs_set(world, target, Position, {0, 0, 20});
    ecs_progress(world, 0);
    test_assert(ecs_has(world, bar, ProjectedHidden));
    const ProjectedBounds *b = ecs_get(world, bar, ProjectedBounds);
    test_assert(b->left == before.left && b->right == before.right);
    test_assert(b->top == before.top && b->bottom == before.bottom);
    const WorldTransform *t = ecs_get(world, bar, WorldTransform);
    test_assert(glm_vec3_norm(ECS_CONST_CAST(float*, t->m[0])) < 1e-6f);

    ecs_set(world, target, Position, {0, 0, 10});
    ecs_set(world, bar, ProjectedBox, {.min = {-1, -1, -1}, .max = {1, 1, 1}});
    ecs_progress(world, 0);
    test_assert(!ecs_has(world, bar, ProjectedHidden));
    b = ecs_get(world, bar, ProjectedBounds);
    test_assert(b->left < -640.0f && b->right > 640.0f);
    test_assert(b->bottom < -360.0f && b->top > 360.0f);
    test_assert(isfinite(b->left) && isfinite(b->right));
    test_assert(isfinite(b->top) && isfinite(b->bottom));

    ecs_set(world, target, Position, {3, 0, 10});
    ecs_set(world, bar, ProjectedBox, {
        .min = {-0.5f, -0.5f, -0.5f}, .max = {0.5f, 0.5f, 0.5f}});
    ecs_progress(world, 0);
    test_assert(ecs_has(world, bar, ProjectedHidden));

    ecs_set(world, target, Position, {0, 0, 0});
    ecs_progress(world, 0);
    test_assert(!ecs_has(world, bar, ProjectedHidden));
    t = ecs_get(world, bar, WorldTransform);
    test_assert(fabsf(t->m[0][0] - 1.0f) < 1e-6f);

    ecs_fini(world);
}

void Projection_box_off_screen(void) {
    // engine/projected_bounds
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .far_ = 20, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, Position, {9.5f, 0, 0});
    ecs_entity_t bar = ecs_new(world);
    ecs_set(world, bar, ProjectedAnchor, {.target = target, .view = view});
    ecs_set(world, bar, ProjectedBox, {.min = {-1, -1, -1}, .max = {1, 1, 1}});
    ecs_entity_t point = ecs_new(world);
    ecs_set(world, point, ProjectedAnchor, {.target = target, .view = view});
    ecs_progress(world, 0);

    test_assert(ecs_has(world, point, ProjectedHidden));
    test_assert(!ecs_has(world, bar, ProjectedHidden));
    const ProjectedBounds *b = ecs_get(world, bar, ProjectedBounds);
    test_assert(fabsf(b->left - 612.0f) < 0.01f);
    test_assert(fabsf(b->right - 756.0f) < 0.01f);

    ecs_set(world, target, Position, {11, 0, 0});
    ecs_progress(world, 0);
    test_assert(ecs_has(world, bar, ProjectedHidden));

    ecs_set(world, target, Position, {0, -7, 0});
    ecs_progress(world, 0);
    test_assert(ecs_has(world, bar, ProjectedHidden));

    ecs_set(world, target, Position, {0, 0, -15});
    ecs_progress(world, 0);
    test_assert(ecs_has(world, bar, ProjectedHidden));

    ecs_set(world, target, Position, {0, 0, -9.5f});
    ecs_progress(world, 0);
    test_assert(!ecs_has(world, bar, ProjectedHidden));

    ecs_fini(world);
}

void Projection_box_removed(void) {
    // engine/projected_bounds
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
    ecs_entity_t point = ecs_new(world);
    ecs_set(world, point, ProjectedAnchor, {.target = target, .view = view});
    ecs_entity_t bar = ecs_new(world);
    ecs_set(world, bar, ProjectedAnchor, {.target = target, .view = view});
    ecs_set(world, bar, ProjectedBox, {
        .min = {-1, 0, -1}, .max = {1, 1, 1}, .offset = {0, 4}, .fit_width = true});
    ecs_set(world, bar, Rect, {10, 4});
    ecs_progress(world, 0);

    test_assert(!ecs_has(world, point, ProjectedBounds));
    test_assert(!ecs_has(world, bar, LayoutItem));
    test_assert(ecs_get(world, bar, Rect)->width == 10);
    const Position *p = ecs_get(world, bar, Position);
    test_assert(fabsf(p->x - 144.0f) < 0.01f);
    test_assert(fabsf(p->y - 148.0f) < 0.01f);
    p = ecs_get(world, point, Position);
    test_assert(fabsf(p->x - 144.0f) < 0.01f);
    test_assert(fabsf(p->y - 72.0f) < 0.01f);

    ecs_remove(world, bar, ProjectedBox);
    ecs_progress(world, 0);
    p = ecs_get(world, bar, Position);
    test_assert(fabsf(p->x - 144.0f) < 0.01f);
    test_assert(fabsf(p->y - 72.0f) < 0.01f);

    ecs_fini(world);
}

void Projection_box_from_script(void) {
    // engine/projected_bounds
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "projected_box",
        "using box.transform\n"
        "using box.geometry\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.layout\n"
        "using box.projection\n"
        "cam {\n"
        "  Position: {0, 0, 10}\n"
        "  Camera: {ortho: true, ortho_height: 10}\n"
        "}\n"
        "main_view {\n"
        "  View: {camera: cam}\n"
        "}\n"
        "tower {\n"
        "  Position: {0, 0, 0}\n"
        "}\n"
        "ui {\n"
        "  Scene: {}\n"
        "  health {\n"
        "    ProjectedAnchor: {target: tower, view: main_view}\n"
        "    ProjectedBox: {min: {-1, 0, -1}, max: {1, 2, 1}, offset: {0, 8}, fit_width: true}\n"
        "    Layout: {direction: Row}\n"
        "    LayoutItem: {width: {mode: Fixed}, height: {mode: Fixed, value: 4}}\n"
        "    Rect: {1, 4}\n"
        "    fill {\n"
        "      LayoutItem: {width: {mode: Grow, value: 1}, height: {mode: Grow}}\n"
        "      Rect: {0, 0}\n"
        "    }\n"
        "    empty {\n"
        "      LayoutItem: {width: {mode: Grow, value: 1}, height: {mode: Grow}}\n"
        "    }\n"
        "  }\n"
        "}\n", NULL) == 0);
    ecs_progress(world, 0);

    ecs_entity_t bar = ecs_lookup(world, "ui.health");
    ecs_entity_t fill = ecs_lookup(world, "ui.health.fill");
    test_assert(bar != 0 && fill != 0);
    test_assert(!ecs_has(world, bar, ProjectedHidden));
    const Position *p = ecs_get(world, bar, Position);
    test_assert(fabsf(p->x) < 0.01f);
    test_assert(fabsf(p->y - 152.0f) < 0.01f);
    test_assert(fabsf(ecs_get(world, bar, Rect)->width - 144.0f) < 0.01f);
    test_assert(fabsf(ecs_get(world, fill, Rect)->width - 72.0f) < 0.01f);
    test_assert(fabsf(ecs_get(world, fill, Rect)->height - 4.0f) < 0.01f);
    const WorldTransform *t = ecs_get(world, fill, WorldTransform);
    test_assert(fabsf(t->m[3][0] + 36.0f) < 0.01f);
    test_assert(fabsf(t->m[3][1] - 152.0f) < 0.01f);

    ecs_fini(world);
}
