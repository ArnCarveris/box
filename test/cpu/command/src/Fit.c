/**
 * @file Fit.c
 * @brief Tests for the fit command: a camera moves to a tight fit of the meshes of its scene.
 */

#include <box.h>
#include <bake_test.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "mesh_cache.h"
#include "render.h"

/** Margin in world units of the frustum checks */
#define FIT_EPSILON (1e-3f)

/** Margin of a comparison with a value the output prints with 2 decimals */
#define FIT_ROUNDED (0.006f)

/** The output of a fit: the number of entities, the center and the distance */
typedef struct fit_result_t {
    int32_t count;      /**< Number of entities with a mesh */
    vec3 center;        /**< Center of the content */
    float distance;     /**< Distance of the camera from the center */
} fit_result_t;

/** Create a world with box imported.
 *
 * @return The world
 */
static ecs_world_t* fit_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    return world;
}

/** Create a camera.
 *
 * @param world The world
 * @param name Name of the camera
 * @param pos Position
 * @param rot Rotation
 * @param cam The camera
 * @param view Whether a view renders the camera
 * @return The camera
 */
static ecs_entity_t fit_camera(
    ecs_world_t *world,
    const char *name,
    Position pos,
    Rotation rot,
    Camera cam,
    bool view)
{
    ecs_entity_t result = ecs_entity(world, { .name = name });
    ecs_set_ptr(world, result, Position, &pos);
    ecs_set_ptr(world, result, Rotation, &rot);
    ecs_set_ptr(world, result, Camera, &cam);
    if (view) {
        ecs_entity_t v = ecs_new(world);
        ecs_set(world, v, View, { .camera = result });
    }

    return result;
}

/** Create a box shape.
 *
 * @param world The world
 * @param parent The parent, 0 for none
 * @param pos Position
 * @param size Size of the box
 * @return The shape
 */
static ecs_entity_t fit_box(
    ecs_world_t *world,
    ecs_entity_t parent,
    Position pos,
    Box size)
{
    ecs_entity_t result = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set_ptr(world, result, Position, &pos);
    ecs_set_ptr(world, result, Box, &size);

    return result;
}

/** Get the 8 world space corners of the mesh of a shape.
 *
 * @param world The world
 * @param e The shape
 * @param out Receives the corners
 */
static void fit_corners(
    ecs_world_t *world,
    ecs_entity_t e,
    vec3 *out)
{
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    test_assert(shape != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(box_mesh_cache_of(world), shape->mesh);
    test_assert(mesh != NULL);
    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    test_assert(t != NULL);
    for (int32_t i = 0; i < 8; i ++) {
        vec3 local = {
            ((i & 1) ? mesh->max[0] : mesh->min[0]) * shape->scale[0],
            ((i & 2) ? mesh->max[1] : mesh->min[1]) * shape->scale[1],
            ((i & 4) ? mesh->max[2] : mesh->min[2]) * shape->scale[2]
        };

        glm_mat4_mulv3((vec4*)t->m, local, 1.0f, out[i]);
    }
}

/** Run a fit and parse its output.
 *
 * @param world The world
 * @param s The session
 * @param line The command line
 * @param camera Path of the camera the output names
 * @param r Receives the result
 */
static void fit_run(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *line,
    const char *camera,
    fit_result_t *r)
{
    bool error = true;
    char *out = box_command_run(world, s, line, &error);
    char path[256] = {0};
    test_bool(error, false);
    test_int(sscanf(out, "fit %255s to %d entities, center (%f, %f, %f), distance %f",
        path, &r->count, &r->center[0], &r->center[1], &r->center[2], &r->distance), 6);
    test_str(path, camera);
    ecs_os_free(out);
}

/** Run a command line that fails and check its error.
 *
 * @param world The world
 * @param s The session
 * @param line The command line
 * @param expect The error it must print
 */
static void fit_run_error(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *line,
    const char *expect)
{
    bool error = false;
    char *out = box_command_run(world, s, line, &error);
    test_bool(error, true);
    test_str(out, expect);
    ecs_os_free(out);
}

/** Check whether corners lie inside the frustum of a perspective camera.
 *
 * @param m World transform of the camera, the axes of its view
 * @param eye Position of the camera
 * @param fov Vertical field of view in degrees
 * @param aspect Width over height
 * @param near_ Near plane distance
 * @param corners The corners
 * @param count Number of corners
 * @return Whether every corner is inside
 */
static bool fit_inside(
    const WorldTransform *m,
    const vec3 eye,
    float fov,
    float aspect,
    float near_,
    vec3 *corners,
    int32_t count)
{
    vec3 right, up, back;
    bool result = true;
    float tan_y = tanf(glm_rad(fov) * 0.5f), tan_x = tan_y * aspect;
    glm_vec3_normalize_to((float*)m->m[0], right);
    glm_vec3_normalize_to((float*)m->m[1], up);
    glm_vec3_normalize_to((float*)m->m[2], back);
    for (int32_t i = 0; result && i < count; i ++) {
        vec3 d;
        glm_vec3_sub(corners[i], (float*)eye, d);
        float x = glm_vec3_dot(d, right), y = glm_vec3_dot(d, up);
        float depth = -glm_vec3_dot(d, back);
        result = depth >= near_ - FIT_EPSILON && fabsf(x) <= depth * tan_x + FIT_EPSILON &&
            fabsf(y) <= depth * tan_y + FIT_EPSILON;
    }

    return result;
}

/** Get the world position of an entity.
 *
 * @param world The world
 * @param e The entity
 * @param out Receives the position
 */
static void fit_eye(
    ecs_world_t *world,
    ecs_entity_t e,
    vec3 out)
{
    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    test_assert(t != NULL);
    glm_vec3_copy((float*)t->m[3], out);
}

/** Check that the corners are inside the frustum of a camera and that they are not when the
 * camera is 10% closer to the center.
 *
 * @param world The world
 * @param camera The camera
 * @param r The result of the fit
 * @param corners The corners
 * @param count Number of corners
 */
static void fit_check_tight(
    ecs_world_t *world,
    ecs_entity_t camera,
    const fit_result_t *r,
    vec3 *corners,
    int32_t count)
{
    const Camera *cam = ecs_get(world, camera, Camera);
    const WorldTransform *t = ecs_get(world, camera, WorldTransform);
    vec3 eye, closer;
    fit_eye(world, camera, eye);
    test_assert(fabsf(glm_vec3_distance(eye, (float*)r->center) - r->distance) < FIT_ROUNDED);
    test_bool(fit_inside(t, eye, cam->fov, cam->aspect_ratio, cam->near_, corners, count), true);

    glm_vec3_sub(eye, (float*)r->center, closer);
    glm_vec3_scale(closer, 0.9f, closer);
    glm_vec3_add(closer, (float*)r->center, closer);
    test_bool(fit_inside(t, closer, cam->fov, cam->aspect_ratio, cam->near_, corners, count),
        false);
}

void Fit_frames_scene(void) {
    // command/fit
    ecs_world_t *world = fit_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t camera = fit_camera(world, "camera", (Position){0, 2, 20}, (Rotation){0},
        (Camera){ .fov = 50, .near_ = 0.1f, .far_ = 1000, .aspect_ratio = 1.5f }, true);
    ecs_entity_t boxes[3] = {
        fit_box(world, 0, (Position){-3, 0, 0}, (Box){1, 1, 1}),
        fit_box(world, 0, (Position){4, 1, -2}, (Box){2, 3, 1}),
        fit_box(world, 0, (Position){0, -1, 3}, (Box){1, 1, 2})
    };
    ecs_progress(world, 0);

    vec3 corners[24];
    for (int32_t i = 0; i < 3; i ++) {
        fit_corners(world, boxes[i], &corners[i * 8]);
    }

    fit_result_t r;
    fit_run(world, s, "fit", "camera", &r);
    test_int(r.count, 3);
    test_flt(r.center[0], 0.75f);
    test_flt(r.center[1], 0.5f);
    test_flt(r.center[2], 0.75f);
    ecs_progress(world, 0);

    // The camera looks along -Z at zero rotation, so it backs off along +Z
    const Position *p = ecs_get(world, camera, Position);
    test_assert(p->z > r.center[2]);
    fit_check_tight(world, camera, &r, corners, 24);

    ecs_fini(world);
}

void Fit_keeps_direction(void) {
    // command/fit
    ecs_world_t *world = fit_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t camera = fit_camera(world, "camera", (Position){5, 8, 5},
        (Rotation){-0.4f, 0.8f, 0},
        (Camera){ .fov = 60, .near_ = 0.1f, .far_ = 1000, .aspect_ratio = 1.0f }, true);
    ecs_entity_t boxes[2] = {
        fit_box(world, 0, (Position){0, 0, 0}, (Box){2, 2, 2}),
        fit_box(world, 0, (Position){3, 0, -1}, (Box){1, 4, 1})
    };
    ecs_progress(world, 0);

    vec3 corners[16];
    fit_corners(world, boxes[0], &corners[0]);
    fit_corners(world, boxes[1], &corners[8]);

    fit_result_t r;
    fit_run(world, s, "fit", "camera", &r);
    test_int(r.count, 2);
    ecs_progress(world, 0);

    const Rotation *rot = ecs_get(world, camera, Rotation);
    test_flt(rot->x, -0.4f);
    test_flt(rot->y, 0.8f);
    test_flt(rot->z, 0);
    fit_check_tight(world, camera, &r, corners, 16);

    // The camera looks at the center along its view
    const WorldTransform *t = ecs_get(world, camera, WorldTransform);
    vec3 eye, forward, to_center;
    fit_eye(world, camera, eye);
    glm_vec3_negate_to((float*)t->m[2], forward);
    glm_vec3_normalize(forward);
    glm_vec3_sub(r.center, eye, to_center);
    glm_vec3_normalize(to_center);
    test_assert(glm_vec3_dot(forward, to_center) > 0.9999f);

    ecs_fini(world);
}

void Fit_named_camera(void) {
    // command/fit
    ecs_world_t *world = fit_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t active = fit_camera(world, "active", (Position){0, 0, 50}, (Rotation){0},
        (Camera){ .fov = 60, .near_ = 0.1f, .far_ = 1000, .aspect_ratio = 1.0f }, true);
    ecs_entity_t other = fit_camera(world, "other", (Position){0, 0, 80}, (Rotation){0},
        (Camera){ .fov = 60, .near_ = 0.1f, .far_ = 1000, .aspect_ratio = 1.0f }, false);
    fit_box(world, 0, (Position){0, 0, 0}, (Box){2, 2, 2});
    ecs_progress(world, 0);

    fit_result_t r;
    fit_run(world, s, "fit other", "other", &r);
    test_int(r.count, 1);
    ecs_progress(world, 0);

    const Position *p = ecs_get(world, active, Position);
    test_flt(p->x, 0);
    test_flt(p->y, 0);
    test_flt(p->z, 50);
    p = ecs_get(world, other, Position);
    test_flt(p->x, 0);
    test_flt(p->y, 0);
    test_assert(fabsf(p->z - r.distance) < FIT_ROUNDED);
    test_assert(p->z < 80);

    ecs_fini(world);
}

void Fit_skips_other_scene(void) {
    // command/fit
    ecs_world_t *world = fit_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    fit_camera(world, "camera", (Position){0, 0, 10}, (Rotation){0},
        (Camera){ .fov = 60, .near_ = 0.1f, .far_ = 1000, .aspect_ratio = 1.0f }, true);
    fit_box(world, 0, (Position){0, 0, 0}, (Box){1, 1, 1});
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Scene, {0});
    fit_box(world, root, (Position){100, 0, 0}, (Box){1, 1, 1});
    ecs_progress(world, 0);

    fit_result_t r;
    fit_run(world, s, "fit", "camera", &r);
    test_int(r.count, 1);
    test_flt(r.center[0], 0);
    test_flt(r.center[1], 0);
    test_flt(r.center[2], 0);

    ecs_fini(world);
}

void Fit_skips_camera_children(void) {
    // command/fit
    ecs_world_t *world = fit_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t camera = fit_camera(world, "camera", (Position){0, 0, 10}, (Rotation){0},
        (Camera){ .fov = 60, .near_ = 0.1f, .far_ = 1000, .aspect_ratio = 1.0f }, true);
    fit_box(world, 0, (Position){0, 0, 0}, (Box){1, 1, 1});
    ecs_entity_t held = fit_box(world, camera, (Position){0.5f, -0.5f, -1}, (Box){0.2f, 0.2f, 1});
    fit_box(world, held, (Position){0, 0, -1}, (Box){0.1f, 0.1f, 0.1f});
    ecs_progress(world, 0);

    fit_result_t r;
    fit_run(world, s, "fit", "camera", &r);
    test_int(r.count, 1);
    test_flt(r.center[0], 0);
    test_flt(r.center[1], 0);
    test_flt(r.center[2], 0);

    ecs_fini(world);
}

void Fit_skips_prefab_and_disabled(void) {
    // command/fit
    ecs_world_t *world = fit_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    fit_camera(world, "camera", (Position){0, 0, 10}, (Rotation){0},
        (Camera){ .fov = 60, .near_ = 0.1f, .far_ = 1000, .aspect_ratio = 1.0f }, true);
    fit_box(world, 0, (Position){0, 0, 0}, (Box){1, 1, 1});
    ecs_entity_t prefab = fit_box(world, 0, (Position){100, 0, 0}, (Box){1, 1, 1});
    ecs_add_id(world, prefab, EcsPrefab);
    fit_box(world, prefab, (Position){0, 10, 0}, (Box){1, 1, 1});
    ecs_entity_t disabled = fit_box(world, 0, (Position){-100, 0, 0}, (Box){1, 1, 1});
    ecs_enable(world, disabled, false);
    ecs_progress(world, 0);
    test_assert(ecs_has(world, prefab, ShapeMesh));
    test_assert(ecs_has(world, disabled, ShapeMesh));

    fit_result_t r;
    fit_run(world, s, "fit", "camera", &r);
    test_int(r.count, 1);
    test_flt(r.center[0], 0);
    test_flt(r.center[1], 0);
    test_flt(r.center[2], 0);

    ecs_fini(world);
}

void Fit_skips_console_and_editor(void) {
    // command/fit
    ecs_world_t *world = fit_world();
    char *widgets = box_app_locate("test/assets/ui/widgets.flecs");
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "box.ui.widgets" }),
        .filename = widgets
    }) != 0);
    ecs_os_free(widgets);

    fit_camera(world, "camera", (Position){0, 0, 10}, (Rotation){0},
        (Camera){ .fov = 60, .near_ = 0.1f, .far_ = 1000, .aspect_ratio = 1.0f }, true);
    fit_box(world, 0, (Position){0, 0, 0}, (Box){1, 1, 1});

    ecs_singleton_get_mut(world, Console)->open = true;
    ecs_singleton_modified(world, Console);
    ecs_singleton_get_mut(world, Editor)->open = true;
    ecs_singleton_modified(world, Editor);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, BOX_CONSOLE_VIEW) != 0);
    test_assert(ecs_lookup(world, BOX_EDITOR_VIEW) != 0);

    // The console and the editor draw meshes of their own
    const box_mesh_cache_t *cache = box_mesh_cache_of(world);
    int32_t meshes = 0;
    ecs_iter_t it = ecs_each(world, ShapeMesh);
    while (ecs_each_next(&it)) {
        const ShapeMesh *shapes = ecs_field(&it, ShapeMesh, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            meshes += box_mesh_cache_get(cache, shapes[i].mesh) != NULL;
        }
    }

    test_assert(meshes > 1);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    fit_result_t r;
    fit_run(world, s, "fit", "camera", &r);
    test_int(r.count, 1);
    test_flt(r.center[0], 0);
    test_flt(r.center[1], 0);
    test_flt(r.center[2], 0);

    ecs_fini(world);
}

void Fit_orthographic(void) {
    // command/fit
    ecs_world_t *world = fit_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t camera = fit_camera(world, "camera", (Position){0, 0, 50}, (Rotation){0},
        (Camera){ .near_ = 0.1f, .far_ = 1000, .aspect_ratio = 2.0f, .ortho = true,
            .ortho_height = 10, .ortho_scale = 1 }, true);
    fit_box(world, 0, (Position){0, 0, -10}, (Box){6, 2, 2});
    ecs_progress(world, 0);

    fit_result_t r;
    fit_run(world, s, "fit", "camera", &r);
    test_int(r.count, 1);
    test_flt(r.distance, 1.1f);

    // max(1, 3 / 2) * 2 / 0.9
    const Camera *cam = ecs_get(world, camera, Camera);
    test_bool(cam->ortho, true);
    test_flt(cam->ortho_scale, 0);
    test_flt(cam->ortho_height, 3.0f / 0.9f);
    const Position *p = ecs_get(world, camera, Position);
    test_flt(p->x, 0);
    test_flt(p->y, 0);
    test_flt(p->z, -8.9f);

    ecs_fini(world);
}

void Fit_camera_with_parent(void) {
    // command/fit
    ecs_world_t *world = fit_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t rig = ecs_entity(world, { .name = "rig" });
    ecs_set(world, rig, Position, {10, 2, 5});
    ecs_set(world, rig, Rotation, {0, 0.6f, 0});
    ecs_set(world, rig, Scale, {2, 2, 2});
    ecs_entity_t camera = ecs_entity(world, { .name = "camera", .parent = rig });
    ecs_set(world, camera, Position, {1, 0, 3});
    ecs_set(world, camera, Rotation, {-0.2f, 0, 0});
    ecs_set(world, camera, Camera, {
        .fov = 60, .near_ = 0.1f, .far_ = 1000, .aspect_ratio = 1.0f });
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, { .camera = camera });
    ecs_entity_t boxes[2] = {
        fit_box(world, 0, (Position){0, 0, 0}, (Box){2, 2, 2}),
        fit_box(world, 0, (Position){-4, 1, 2}, (Box){1, 1, 1})
    };
    ecs_progress(world, 0);

    vec3 corners[16];
    fit_corners(world, boxes[0], &corners[0]);
    fit_corners(world, boxes[1], &corners[8]);

    fit_result_t r;
    fit_run(world, s, "fit", "rig.camera", &r);
    test_int(r.count, 2);
    ecs_progress(world, 0);

    // The world position is the center minus the view direction times the distance
    const WorldTransform *t = ecs_get(world, camera, WorldTransform);
    vec3 eye, forward, expect;
    fit_eye(world, camera, eye);
    glm_vec3_negate_to((float*)t->m[2], forward);
    glm_vec3_normalize(forward);
    glm_vec3_copy(r.center, expect);
    glm_vec3_muladds(forward, -r.distance, expect);
    test_assert(glm_vec3_distance(eye, expect) < 0.01f);
    test_bool(fit_inside(t, eye, 60, 1.0f, 0.1f, corners, 16), true);

    ecs_fini(world);
}

void Fit_camera_controller(void) {
    // command/fit
    ecs_world_t *world = fit_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t camera = fit_camera(world, "camera", (Position){0, 3, 30},
        (Rotation){-0.1f, 0.2f, 0},
        (Camera){ .fov = 60, .near_ = 0.1f, .far_ = 1000, .aspect_ratio = 1.0f }, true);
    ecs_add(world, camera, CameraController);
    fit_box(world, 0, (Position){0, 0, 0}, (Box){2, 2, 2});
    ecs_progress(world, 1.0f / 60.0f);

    CameraController *cc = ecs_get_mut(world, camera, CameraController);
    cc->velocity[0] = 5;
    cc->velocity[2] = -5;
    cc->angular_velocity[0] = 1;
    cc->angular_velocity[1] = 1;
    ecs_modified(world, camera, CameraController);

    fit_result_t r;
    fit_run(world, s, "fit", "camera", &r);
    test_int(r.count, 1);
    const Position *p = ecs_get(world, camera, Position);
    Position fitted = *p;
    cc = ecs_get_mut(world, camera, CameraController);
    test_flt(cc->velocity[0], 0);
    test_flt(cc->velocity[1], 0);
    test_flt(cc->velocity[2], 0);
    test_flt(cc->angular_velocity[0], 0);
    test_flt(cc->angular_velocity[1], 0);

    for (int32_t i = 0; i < 10; i ++) {
        ecs_progress(world, 1.0f / 60.0f);
    }

    p = ecs_get(world, camera, Position);
    test_flt(p->x, fitted.x);
    test_flt(p->y, fitted.y);
    test_flt(p->z, fitted.z);
    const Rotation *rot = ecs_get(world, camera, Rotation);
    test_flt(rot->x, -0.1f);
    test_flt(rot->y, 0.2f);

    ecs_fini(world);
}

void Fit_rotated_mesh(void) {
    // command/fit
    ecs_world_t *world = fit_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t camera = fit_camera(world, "camera", (Position){0, 0, 50}, (Rotation){0},
        (Camera){ .fov = 60, .near_ = 0.1f, .far_ = 1000, .aspect_ratio = 1.0f }, true);
    ecs_entity_t bar = fit_box(world, 0, (Position){0, 0, 0}, (Box){10, 1, 1});
    ecs_set(world, bar, Rotation, {0, (float)GLM_PI_2, 0});
    ecs_progress(world, 0);

    vec3 corners[8];
    fit_corners(world, bar, corners);

    // Rotated, the bar extends 0.5 to the sides and 5 along the view:
    // 0.5 / (tan(30deg) * 0.9) + 5, not 5 / (tan(30deg) * 0.9) + 0.5
    fit_result_t r;
    fit_run(world, s, "fit", "camera", &r);
    test_int(r.count, 1);
    test_assert(fabsf(r.distance - 5.96f) < 0.011f);
    ecs_progress(world, 0);
    fit_check_tight(world, camera, &r, corners, 8);

    ecs_fini(world);
}

void Fit_small_mesh(void) {
    // command/fit
    ecs_world_t *world = fit_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t camera = fit_camera(world, "camera", (Position){0, 0, 50}, (Rotation){0},
        (Camera){ .fov = 60, .near_ = 0.1f, .far_ = 1000, .aspect_ratio = 1.0f }, true);
    fit_box(world, 0, (Position){0, 0, 0}, (Box){0.01f, 0.01f, 0.01f});
    ecs_progress(world, 0);

    // The minimum extent of 0.5: 0.5 / (tan(30deg) * 0.9) + 0.5
    fit_result_t r;
    fit_run(world, s, "fit", "camera", &r);
    test_int(r.count, 1);
    test_assert(fabsf(r.distance - 1.46f) < 0.011f);
    const Position *p = ecs_get(world, camera, Position);
    test_assert(p->z > 1.0f);

    ecs_fini(world);
}

void Fit_no_content(void) {
    // command/fit
    ecs_world_t *world = fit_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t camera = fit_camera(world, "camera", (Position){1, 2, 3}, (Rotation){0},
        (Camera){ .fov = 60, .near_ = 0.1f, .far_ = 1000, .aspect_ratio = 1.0f }, true);
    ecs_progress(world, 0);

    fit_run_error(world, s, "fit", BOX_COMMAND_ERROR "no content to fit");
    const Position *p = ecs_get(world, camera, Position);
    test_flt(p->x, 1);
    test_flt(p->y, 2);
    test_flt(p->z, 3);

    ecs_fini(world);
}

void Fit_no_active_camera(void) {
    // command/fit
    ecs_world_t *world = fit_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    fit_box(world, 0, (Position){0, 0, 0}, (Box){1, 1, 1});
    ecs_progress(world, 0);

    fit_run_error(world, s, "fit", BOX_COMMAND_ERROR "no active camera");

    ecs_fini(world);
}

void Fit_not_a_camera(void) {
    // command/fit
    ecs_world_t *world = fit_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity(world, { .name = "thing" });
    fit_box(world, 0, (Position){0, 0, 0}, (Box){1, 1, 1});
    ecs_progress(world, 0);

    fit_run_error(world, s, "fit thing", BOX_COMMAND_ERROR "'thing' is not a camera");

    ecs_fini(world);
}

void Fit_help_lists_fit(void) {
    // command/fit
    ecs_world_t *world = fit_world();
    ecs_entity_t f = ecs_lookup(world, "box.command.commands.fit");
    test_assert(f != 0);
    test_assert(ecs_has(world, f, EcsScriptFunction));

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    char *out = box_command_run(world, s, "help", NULL);
    test_assert(!strncmp(out, "fit ", 4) || strstr(out, "\nfit ") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "help fit", NULL);
    test_assert(strstr(out, "fit [camera]") != NULL);
    ecs_os_free(out);

    ecs_fini(world);
}

/** Get the largest depth of corners seen from a camera.
 *
 * @param world The world
 * @param camera The camera
 * @param corners The corners
 * @param count Number of corners
 * @return The largest distance along the view direction
 */
static float fit_depth(
    ecs_world_t *world,
    ecs_entity_t camera,
    vec3 *corners,
    int32_t count)
{
    const WorldTransform *t = ecs_get(world, camera, WorldTransform);
    test_assert(t != NULL);
    vec3 eye, back;
    float result = -FLT_MAX;
    fit_eye(world, camera, eye);
    glm_vec3_normalize_to((float*)t->m[2], back);
    for (int32_t i = 0; i < count; i ++) {
        vec3 d;
        glm_vec3_sub(corners[i], eye, d);
        result = glm_max(result, -glm_vec3_dot(d, back));
    }

    return result;
}

void Fit_far_plane_raised(void) {
    // command/fit
    ecs_world_t *world = fit_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t camera = fit_camera(world, "camera", (Position){0, 0, 5}, (Rotation){0},
        (Camera){ .fov = 60, .near_ = 0.1f, .far_ = 10, .aspect_ratio = 1.0f }, true);
    ecs_entity_t box = fit_box(world, 0, (Position){0, 0, 0}, (Box){40, 40, 40});
    ecs_progress(world, 0);

    vec3 corners[8];
    fit_corners(world, box, corners);

    fit_result_t r;
    fit_run(world, s, "fit", "camera", &r);
    test_int(r.count, 1);
    ecs_progress(world, 0);

    const Camera *cam = ecs_get(world, camera, Camera);
    test_assert(cam->far_ > 10);
    test_assert(fit_depth(world, camera, corners, 8) <= cam->far_);
    fit_check_tight(world, camera, &r, corners, 8);

    ecs_fini(world);
}

void Fit_far_plane_kept(void) {
    // command/fit
    ecs_world_t *world = fit_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t covered = fit_camera(world, "covered", (Position){0, 0, 50}, (Rotation){0},
        (Camera){ .fov = 60, .near_ = 0.1f, .far_ = 500, .aspect_ratio = 1.0f }, true);
    ecs_entity_t infinite = fit_camera(world, "infinite", (Position){0, 0, 50}, (Rotation){0},
        (Camera){ .fov = 60, .near_ = 0.1f, .far_ = 0, .aspect_ratio = 1.0f }, false);
    ecs_entity_t box = fit_box(world, 0, (Position){0, 0, 0}, (Box){40, 40, 40});
    ecs_progress(world, 0);

    vec3 corners[8];
    fit_corners(world, box, corners);

    fit_result_t r;
    fit_run(world, s, "fit covered", "covered", &r);
    test_int(r.count, 1);
    fit_run(world, s, "fit infinite", "infinite", &r);
    test_int(r.count, 1);
    ecs_progress(world, 0);

    test_assert(fit_depth(world, covered, corners, 8) <= 500);
    test_flt(ecs_get(world, covered, Camera)->far_, 500);
    test_flt(ecs_get(world, infinite, Camera)->far_, 0);

    ecs_fini(world);
}

/** Create an offscreen surface with a view root and a view under it.
 *
 * @param world The world
 * @param width Width in points
 * @param height Height in points
 * @param camera Camera of the view
 * @return The surface
 */
static ecs_entity_t fit_surface(
    ecs_world_t *world,
    int32_t width,
    int32_t height,
    ecs_entity_t camera)
{
    ecs_entity_t root = ecs_new(world);
    ecs_entity_t view = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, view, View, { .camera = camera });
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, Surface, {
        .width = width, .height = height, .texture = ecs_new(world), .view_root = root });

    return result;
}

void Fit_aspect_of_view_surface(void) {
    // command/fit
    ecs_world_t *world = fit_world();
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t other = fit_camera(world, "other", (Position){0, 0, 50}, (Rotation){0},
        (Camera){ .fov = 60, .near_ = 0.1f, .far_ = 1000 }, false);
    ecs_entity_t camera = fit_camera(world, "camera", (Position){0, 0, 50}, (Rotation){0},
        (Camera){ .fov = 60, .near_ = 0.1f, .far_ = 1000 }, false);
    fit_surface(world, 1000, 500, other);
    fit_surface(world, 500, 1000, camera);
    ecs_entity_t box = fit_box(world, 0, (Position){0, 0, 0}, (Box){4, 4, 1});
    ecs_progress(world, 0);

    vec3 corners[8];
    fit_corners(world, box, corners);

    fit_result_t r;
    fit_run(world, s, "fit camera", "camera", &r);
    test_int(r.count, 1);
    ecs_progress(world, 0);

    // The second surface is 500x1000: the box fills the width of the view
    const WorldTransform *t = ecs_get(world, camera, WorldTransform);
    vec3 eye, closer;
    fit_eye(world, camera, eye);
    test_assert(fabsf(glm_vec3_distance(eye, r.center) - r.distance) < FIT_ROUNDED);
    test_bool(fit_inside(t, eye, 60, 0.5f, 0.1f, corners, 8), true);
    glm_vec3_sub(eye, r.center, closer);
    glm_vec3_scale(closer, 0.9f, closer);
    glm_vec3_add(closer, r.center, closer);
    test_bool(fit_inside(t, closer, 60, 0.5f, 0.1f, corners, 8), false);

    ecs_fini(world);
    box_render_null_free(null);
}
