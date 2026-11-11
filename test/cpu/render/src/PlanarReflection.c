/**
 * @file PlanarReflection.c
 * @brief Tests for the PlanarReflection component and the planar reflections the extract gives
 * the views: the reflection matrix, the oblique reversed Z projection, the planes a view picks
 * and the records of the packet.
 *
 * The cameras of the picking cases stand at (0, 1, 0) and look along -Z with a 60 degree field
 * of view and an aspect of 1; the worlds render into a null backend.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"
#include "draw.h"

/** Create a world that renders into a null backend, its default scene with
 * Scene.planar_reflections.
 *
 * @param null Receives the backend
 * @return The world
 */
static ecs_world_t* world_new(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    ecs_singleton_set(world, Scene, {.planar_reflections = true});

    return world;
}

/** Get the shader of the batch a shape draws in.
 *
 * @param world The world
 * @param e The shape
 * @return The pipeline of its batch key
 */
static uint32_t pipeline_of(
    ecs_world_t *world,
    ecs_entity_t e)
{
    box_render_draws_t *d = ecs_system_get(world,
        ecs_lookup(world, "box.render.RenderExtractDraws"))->ctx;
    const DrawSlot *slot = ecs_get(world, e, DrawSlot);
    test_assert(slot != NULL && slot->batch >= 0);

    return box_draws_batch(d, slot->batch)->key.pipeline;
}

/** Run a frame and wait until the backend rendered it.
 *
 * @param world The world
 */
static void frame(
    ecs_world_t *world)
{
    ecs_progress(world, 0);
    box_render_sync(world);
}

/** Create a lit view of a perspective camera at (0, 1, 10) that looks along -Z.
 *
 * @param world The world
 * @param parent Parent of the camera, its scene, 0 for the default scene
 * @param lit Whether the view is lit
 * @return The view
 */
static ecs_entity_t view_new(
    ecs_world_t *world,
    ecs_entity_t parent,
    bool lit)
{
    ecs_entity_t camera = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, camera, Position, {0, 1, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting = {.enabled = lit}});

    return view;
}

/** Find the view of the last packet that renders a view entity.
 *
 * @param null The backend
 * @param view The view entity
 * @return The view of the packet
 */
static const box_render_view_t* packet_view(
    box_render_null_t *null,
    ecs_entity_t view)
{
    const box_render_view_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.views); i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&null->last.views, box_render_view_t, i);
        if (v->view == view) {
            result = v;
        }
    }

    test_assert(result != NULL);

    return result;
}

/** Get a planar reflection record of a view of the last packet.
 *
 * @param null The backend
 * @param v The view of the packet
 * @param index Index of the record among those of the view
 * @return The record
 */
static const box_render_planar_t* record_of(
    box_render_null_t *null,
    const box_render_view_t *v,
    int32_t index)
{
    test_assert(index < v->planar_count);
    test_assert(v->planar_first + index < ecs_vec_count(&null->last.planar_reflections));

    return ecs_vec_get_t(&null->last.planar_reflections, box_render_planar_t,
        v->planar_first + index);
}

/** Make a packet view of a perspective camera at (0, 1, 0) that looks along -Z.
 *
 * @param far_ Far plane, 0 for none
 * @param out Receives the view
 */
static void pick_view(
    float far_,
    box_render_view_t *out)
{
    ecs_os_memset_t(out, 0, box_render_view_t);
    out->camera_data = (Camera){.fov = 60, .near_ = 0.1f, .far_ = far_, .aspect_ratio = 1};
    glm_mat4_identity(out->camera_transform);
    out->camera_transform[3][1] = 1.0f;
    glm_mat4_inv(out->camera_transform, out->view_matrix);
}

/** Make a plane from a normal and a point.
 *
 * @param nx Normal x
 * @param ny Normal y
 * @param nz Normal z
 * @param px Point x
 * @param py Point y
 * @param pz Point z
 * @return The plane
 */
static box_render_planar_plane_t plane_of(
    float nx,
    float ny,
    float nz,
    float px,
    float py,
    float pz)
{
    box_render_planar_plane_t result;
    result.plane[0] = nx;
    result.plane[1] = ny;
    result.plane[2] = nz;
    result.plane[3] = -(nx * px + ny * py + nz * pz);

    return result;
}

/** Project a view space point and get its normalized device coordinates.
 *
 * @param proj The projection
 * @param p The point, w 1
 * @param out Receives x, y and depth over w, and w
 */
static void project(
    mat4 proj,
    const float *p,
    vec4 out)
{
    vec4 in = {p[0], p[1], p[2], 1.0f};
    glm_mat4_mulv(proj, in, out);
    out[0] /= out[3];
    out[1] /= out[3];
    out[2] /= out[3];
}

void PlanarReflection_defaults(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "mirror",
        "using box.light\n"
        "mirror { PlanarReflection: {} }\n", NULL) == 0);
    const PlanarReflection *p = ecs_get(world, ecs_lookup(world, "mirror"), PlanarReflection);
    test_assert(p != NULL);
    test_flt(p->resolution, 0.5f);
    test_flt(p->clip_offset, 0.02f);
    test_flt(p->distortion, 0.02f);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, PlanarReflection);
    p = ecs_get(world, e, PlanarReflection);
    test_flt(p->resolution, 0.5f);
    test_flt(p->clip_offset, 0.02f);
    test_flt(p->distortion, 0.02f);

    test_assert(ecs_script_run(world, "partial",
        "using box.light\n"
        "partial { PlanarReflection: {resolution: 1} }\n", NULL) == 0);
    p = ecs_get(world, ecs_lookup(world, "partial"), PlanarReflection);
    test_flt(p->resolution, 1.0f);
    test_flt(p->clip_offset, 0.02f);
    test_flt(p->distortion, 0.02f);
    test_int(BOX_PLANAR_REFLECTION_MAX, 2);

    ecs_fini(world);
}

void PlanarReflection_matrix_mirrors_points(void) {
    box_render_planar_plane_t floor = plane_of(0, 1, 0, 0, 1, 0);
    mat4 m, twice;
    vec4 p = {2, 3, 4, 1}, q;
    box_render_planar_matrix(floor.plane, m);
    glm_mat4_mulv(m, p, q);
    test_flt(q[0], 2);
    test_flt(q[1], -1);
    test_flt(q[2], 4);
    test_flt(q[3], 1);

    vec4 on = {-5, 1, 7, 1};
    glm_mat4_mulv(m, on, q);
    test_flt(q[0], -5);
    test_flt(q[1], 1);
    test_flt(q[2], 7);

    test_flt(glm_mat4_det(m), -1);
    glm_mat4_mul(m, m, twice);
    int32_t i, j;
    for (i = 0; i < 4; i ++) {
        for (j = 0; j < 4; j ++) {
            test_assert(fabsf(twice[i][j] - (i == j ? 1.0f : 0.0f)) < 1e-5f);
        }
    }
}

void PlanarReflection_matrix_wall_mirror(void) {
    // A wall mirror at z -4 that faces +Z, as a Rotation of 90 degrees about X turns +Y
    box_render_planar_plane_t wall = plane_of(0, 0, 1, 0, 1.5f, -4);
    mat4 m;
    vec4 p = {1, 2, -1, 1}, q, dir = {0, 0, -1, 0};
    box_render_planar_matrix(wall.plane, m);
    glm_mat4_mulv(m, p, q);
    test_flt(q[0], 1);
    test_flt(q[1], 2);
    test_flt(q[2], -7);

    glm_mat4_mulv(m, dir, q);
    test_flt(q[0], 0);
    test_flt(q[1], 0);
    test_flt(q[2], 1);
    test_flt(q[3], 0);
}

/** Check the oblique projection of a mirrored camera at the origin looking along -Z, below the
 * plane y = 1 it sees through: a point above the plane is kept, a point below it is clipped, a
 * point on it is at depth 1, depth falls along a ray and the corners of the frustum above the
 * plane stay at or above 0.
 *
 * @param far_ Far plane, 0 for none
 */
static void oblique_check(
    float far_)
{
    Camera c = {.fov = 60, .near_ = 0.1f, .far_ = far_};
    mat4 proj, oblique;
    vec4 ndc;
    float plane[4] = {0, 1, 0, -1};
    box_camera_projection(&c, 100, 100, proj);
    test_bool(box_render_planar_oblique(proj, plane, oblique), true);

    // x, y and w rows are those of the projection
    int32_t j;
    for (j = 0; j < 4; j ++) {
        test_flt(oblique[j][0], proj[j][0]);
        test_flt(oblique[j][1], proj[j][1]);
        test_flt(oblique[j][3], proj[j][3]);
    }

    // Above the plane: kept, within 0..1
    project(oblique, (float[]){0, 2, -5}, ndc);
    test_assert(ndc[3] > 0);
    test_assert(ndc[2] >= 0.0f && ndc[2] <= 1.0f);
    test_assert(fabsf(ndc[1]) <= 1.0f);

    // Below the plane, inside the frustum of the camera: clipped by the near plane
    project(proj, (float[]){0, 0.5f, -5}, ndc);
    test_assert(ndc[2] >= 0.0f && ndc[2] <= 1.0f && fabsf(ndc[1]) <= 1.0f);
    project(oblique, (float[]){0, 0.5f, -5}, ndc);
    test_assert(ndc[2] > 1.0f);

    // On the plane: depth 1
    project(oblique, (float[]){0.5f, 1, -3}, ndc);
    test_assert(fabsf(ndc[2] - 1.0f) < 1e-4f);

    // Far above the plane, still in the frustum
    float far_point = far_ > 0 ? far_ * 0.9f : 1e5f;
    project(oblique, (float[]){0, far_point * 0.5f, -far_point}, ndc);
    test_assert(ndc[2] >= -1e-5f && ndc[2] <= 1.0f);

    // Reversed Z: along a ray, the nearer point is deeper
    vec4 near_ndc, far_ndc;
    project(oblique, (float[]){0, 2, -5}, near_ndc);
    project(oblique, (float[]){0, 4, -10}, far_ndc);
    test_assert(near_ndc[2] > far_ndc[2]);

    // Corners of the far end of the frustum above the plane stay at or above depth 0
    mat4 inv;
    glm_mat4_inv(proj, inv);
    int32_t i;
    for (i = 0; i < 4; i ++) {
        vec4 corner = {(i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, 0.0f, 1.0f}, q, clip;
        glm_mat4_mulv(inv, corner, q);
        if (plane[0] * q[0] + plane[1] * q[1] + plane[2] * q[2] + plane[3] * q[3] >= 0) {
            glm_mat4_mulv(oblique, q, clip);
            test_assert(clip[2] >= -1e-4f * fabsf(clip[3]) - 1e-6f);
        }
    }
}

void PlanarReflection_oblique_finite_far(void) {
    oblique_check(1000);
}

void PlanarReflection_oblique_infinite_far(void) {
    oblique_check(0);
}

void PlanarReflection_oblique_offset_keeps_below(void) {
    // A clip plane moved 0.1 below y = 1 keeps a point 0.05 below the plane
    Camera c = {.fov = 60, .near_ = 0.1f, .far_ = 0};
    mat4 proj, oblique;
    vec4 ndc;
    float exact[4] = {0, 1, 0, -1}, offset[4] = {0, 1, 0, -0.9f};
    box_camera_projection(&c, 100, 100, proj);
    test_bool(box_render_planar_oblique(proj, exact, oblique), true);
    project(oblique, (float[]){0, 0.95f, -5}, ndc);
    test_assert(ndc[2] > 1.0f);
    test_bool(box_render_planar_oblique(proj, offset, oblique), true);
    project(oblique, (float[]){0, 0.95f, -5}, ndc);
    test_assert(ndc[2] <= 1.0f && ndc[2] >= 0.0f);
}

void PlanarReflection_oblique_behind_camera(void) {
    // A plane that keeps only what is behind the camera leaves nothing, the projection stays
    Camera c = {.fov = 60, .near_ = 0.1f, .far_ = 100};
    mat4 proj, oblique;
    float plane[4] = {0, 0, 1, -1};
    box_camera_projection(&c, 100, 100, proj);
    test_bool(box_render_planar_oblique(proj, plane, oblique), false);
    int32_t i, j;
    for (i = 0; i < 4; i ++) {
        for (j = 0; j < 4; j ++) {
            test_flt(oblique[i][j], proj[i][j]);
        }
    }
}

void PlanarReflection_select_frustum(void) {
    box_render_view_t v;
    int32_t out[BOX_PLANAR_REFLECTION_MAX];
    pick_view(0, &v);

    // The floor and a ceiling cross the frustum, a wall behind the camera does not
    box_render_planar_plane_t floor = plane_of(0, 1, 0, 0, 0, 0);
    box_render_planar_plane_t ceiling = plane_of(0, -1, 0, 0, 5, 0);
    box_render_planar_plane_t behind = plane_of(0, 0, -1, 0, 0, 5);
    test_int(box_render_planar_select(&v, &floor, 1, out), 1);
    test_int(out[0], 0);
    test_int(box_render_planar_select(&v, &ceiling, 1, out), 1);
    test_int(box_render_planar_select(&v, &behind, 1, out), 0);

    // A camera that looks up does not see the floor
    pick_view(0, &v);
    glm_mat4_identity(v.camera_transform);
    glm_rotate_x(v.camera_transform, glm_rad(90), v.camera_transform);
    v.camera_transform[3][1] = 1.0f;
    test_int(box_render_planar_select(&v, &floor, 1, out), 0);
}

void PlanarReflection_select_far_plane(void) {
    box_render_view_t v;
    int32_t out[BOX_PLANAR_REFLECTION_MAX];
    box_render_planar_plane_t deep = plane_of(0, 1, 0, 0, -100, 0);

    // 101 units below a camera whose lowest ray drops 0.58 per unit, beyond a far plane of 50
    pick_view(50, &v);
    test_int(box_render_planar_select(&v, &deep, 1, out), 0);
    pick_view(0, &v);
    test_int(box_render_planar_select(&v, &deep, 1, out), 1);
    pick_view(1000, &v);
    test_int(box_render_planar_select(&v, &deep, 1, out), 1);
}

void PlanarReflection_select_back_side(void) {
    box_render_view_t v;
    int32_t out[BOX_PLANAR_REFLECTION_MAX];
    pick_view(0, &v);
    box_render_planar_plane_t under = plane_of(0, -1, 0, 0, 0, 0);
    box_render_planar_plane_t through = plane_of(0, 1, 0, 0, 1, 0);
    test_int(box_render_planar_select(&v, &under, 1, out), 0);
    test_int(box_render_planar_select(&v, &through, 1, out), 0);
}

void PlanarReflection_select_limit_nearest_first(void) {
    box_render_view_t v;
    int32_t out[BOX_PLANAR_REFLECTION_MAX];
    pick_view(0, &v);
    box_render_planar_plane_t planes[4] = {
        plane_of(0, 1, 0, 0, -3, 0),    // 4 below the camera
        plane_of(0, 1, 0, 0, 0, 0),     // 1 below
        plane_of(0, 0, -1, 0, 0, 5),    // behind the camera, not seen
        plane_of(0, 1, 0, 0, -1, 0)     // 2 below
    };
    test_int(box_render_planar_select(&v, planes, 4, out), 2);
    test_int(out[0], 1);
    test_int(out[1], 3);

    test_int(box_render_planar_select(&v, planes, 1, out), 1);
    test_int(out[0], 0);

    // Equal distances keep the order of the planes
    box_render_planar_plane_t same[3] = {
        plane_of(0, 1, 0, 0, 0, 0),
        plane_of(0, -1, 0, 0, 2, 0),
        plane_of(0, 1, 0, 0, -5, 0)
    };
    test_int(box_render_planar_select(&v, same, 3, out), 2);
    test_int(out[0], 0);
    test_int(out[1], 1);
}

void PlanarReflection_select_ortho_none(void) {
    box_render_view_t v;
    int32_t out[BOX_PLANAR_REFLECTION_MAX];
    pick_view(0, &v);
    v.camera_data = (Camera){.ortho = true, .ortho_height = 10, .near_ = 0.1f, .far_ = 100};
    box_render_planar_plane_t floor = plane_of(0, 1, 0, 0, 0, 0);
    test_int(box_render_planar_select(&v, &floor, 1, out), 0);
}

void PlanarReflection_extract_record(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0, true);
    ecs_entity_t mirror = ecs_new(world);
    ecs_set(world, mirror, Position, {0, -1, 0});
    ecs_set(world, mirror, Box, {20, 0.1f, 20});
    ecs_set(world, mirror, PlanarReflection, {.resolution = 4, .clip_offset = 0.25f,
        .distortion = 0.1f});
    frame(world);
    frame(world);

    const box_render_view_t *v = packet_view(null, view);
    test_int(v->planar_count, 1);
    const box_render_planar_t *r = record_of(null, v, 0);
    test_assert(r->entity == mirror);
    test_int(r->slot, 0);
    test_flt(r->plane[0], 0);
    test_flt(r->plane[1], 1);
    test_flt(r->plane[2], 0);
    test_flt(r->plane[3], 1);
    test_flt(r->clip[3], 1.25f);
    test_flt(r->resolution, 1.0f);
    test_flt(r->distortion, 0.1f);

    // The camera at (0, 1, 10) mirrors to (0, -3, 10)
    test_flt(r->camera[0], 0);
    test_flt(r->camera[1], -3);
    test_flt(r->camera[2], 10);

    // The view matrix is the one of the view times the reflection
    mat4 reflection, expect;
    box_render_planar_matrix(r->plane, reflection);
    glm_mat4_mul(ECS_CONST_CAST(vec4*, v->view_matrix), reflection, expect);
    int32_t i, j;
    for (i = 0; i < 4; i ++) {
        for (j = 0; j < 4; j ++) {
            test_assert(fabsf(r->view_matrix[i][j] - expect[i][j]) < 1e-5f);
        }
    }

    // The shape of the mirror is its instance
    uint32_t page = 0, slot = 0;
    test_bool(box_render_draws_instance(world, mirror, &page, &slot), true);
    test_assert(page != 0);
    test_uint(r->exclude_page, page);
    test_uint(r->exclude_slot, slot);

    // The record holds the transform of that instance as its page does, the box scaled by its
    // size and moved to the mirror
    vec4 rows[3];
    test_bool(box_render_draws_instance_transform(world, mirror, rows), true);
    for (i = 0; i < 3; i ++) {
        for (j = 0; j < 4; j ++) {
            test_flt(r->owner[i][j], rows[i][j]);
        }
    }

    test_assert(fabsf(r->owner[1][3] + 1.0f) < 1e-5f);

    ecs_fini(world);
    box_render_null_free(null);
}

void PlanarReflection_extract_rotated_wall(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0, true);
    ecs_entity_t mirror = ecs_new(world);
    ecs_set(world, mirror, Position, {0, 1.5f, -4});
    ecs_set(world, mirror, Rotation, {GLM_PI_2f, 0, 0});
    ecs_set(world, mirror, Scale, {3, 3, 3});
    ecs_add(world, mirror, PlanarReflection);
    frame(world);

    const box_render_view_t *v = packet_view(null, view);
    test_int(v->planar_count, 1);
    const box_render_planar_t *r = record_of(null, v, 0);
    test_assert(fabsf(r->plane[0]) < 1e-5f);
    test_assert(fabsf(r->plane[1]) < 1e-5f);
    test_assert(fabsf(r->plane[2] - 1.0f) < 1e-5f);
    test_assert(fabsf(r->plane[3] - 4.0f) < 1e-4f);
    test_flt(r->resolution, 0.5f);
    test_uint(r->exclude_page, 0);
    test_flt(r->distortion, 0.02f);
    test_flt(r->owner[0][0], 0);
    test_flt(r->owner[1][3], 0);

    // The camera at z 10 mirrors to z -18
    test_assert(fabsf(r->camera[2] + 18.0f) < 1e-4f);

    ecs_fini(world);
    box_render_null_free(null);
}

void PlanarReflection_extract_limit(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0, true);
    ecs_entity_t far_ = ecs_new(world);
    ecs_set(world, far_, Position, {0, -5, 0});
    ecs_add(world, far_, PlanarReflection);
    ecs_entity_t near_ = ecs_new(world);
    ecs_set(world, near_, Position, {0, 0, 0});
    ecs_add(world, near_, PlanarReflection);
    ecs_entity_t mid = ecs_new(world);
    ecs_set(world, mid, Position, {0, -2, 0});
    ecs_add(world, mid, PlanarReflection);
    ecs_entity_t back = ecs_new(world);
    ecs_set(world, back, Position, {0, 3, 0});
    ecs_add(world, back, PlanarReflection);
    frame(world);

    const box_render_view_t *v = packet_view(null, view);
    test_int(v->planar_count, 2);
    test_assert(record_of(null, v, 0)->entity == near_);
    test_int(record_of(null, v, 0)->slot, 0);
    test_assert(record_of(null, v, 1)->entity == mid);
    test_int(record_of(null, v, 1)->slot, 1);

    ecs_delete(world, near_);
    frame(world);
    v = packet_view(null, view);
    test_int(v->planar_count, 2);
    test_assert(record_of(null, v, 0)->entity == mid);
    test_assert(record_of(null, v, 1)->entity == far_);

    ecs_remove(world, mid, PlanarReflection);
    ecs_remove(world, far_, PlanarReflection);
    frame(world);
    v = packet_view(null, view);
    test_int(v->planar_count, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void PlanarReflection_extract_views_without(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t unlit = view_new(world, 0, false);
    ecs_entity_t scene = ecs_new(world);
    ecs_set(world, scene, Scene, {.planar_reflections = true});
    ecs_entity_t other = view_new(world, scene, true);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 1, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 100});
    ecs_entity_t ortho = ecs_new(world);
    ecs_set(world, ortho, View, {.camera = camera, .lighting = {.enabled = true}});
    ecs_entity_t mirror = ecs_new(world);
    ecs_set(world, mirror, Position, {0, 0, 0});
    ecs_add(world, mirror, PlanarReflection);
    frame(world);

    test_int(packet_view(null, unlit)->planar_count, 0);
    test_int(packet_view(null, other)->planar_count, 0);
    test_int(packet_view(null, ortho)->planar_count, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void PlanarReflection_extract_needs_scene_feature(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_singleton_set(world, Scene, {.planar_reflections = false});
    ecs_entity_t view = view_new(world, 0, true);
    ecs_entity_t mirror = ecs_new(world);
    ecs_set(world, mirror, Position, {0, 0, 0});
    ecs_add(world, mirror, PlanarReflection);
    frame(world);

    // Without Scene.planar_reflections the view renders no reflection
    const box_render_view_t *v = packet_view(null, view);
    test_int(v->planar_count, 0);
    test_int(ecs_vec_count(&null->last.planar_reflections), 0);
    test_assert(!(v->scene_features & BOX_RENDER_FEATURE_PLANAR));

    ecs_singleton_set(world, Scene, {.planar_reflections = true});
    frame(world);
    v = packet_view(null, view);
    test_assert(v->scene_features & BOX_RENDER_FEATURE_PLANAR);
    test_int(v->planar_count, 1);
    test_assert(record_of(null, v, 0)->entity == mirror);

    // A scene of its own without the feature renders none, whatever the default scene does
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_entity_t other = view_new(world, scene, true);
    ecs_entity_t inner = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, inner, Position, {0, 0, 0});
    ecs_add(world, inner, PlanarReflection);
    frame(world);
    test_int(packet_view(null, other)->planar_count, 0);
    test_int(packet_view(null, view)->planar_count, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void PlanarReflection_shape_draws_with_extended(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    view_new(world, 0, true);
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Position, {3, 0, 0});
    ecs_set(world, plain, Box, {1, 1, 1});
    ecs_entity_t mirror = ecs_new(world);
    ecs_set(world, mirror, Position, {0, 0, 0});
    ecs_set(world, mirror, Box, {4, 0.1f, 4});
    ecs_add(world, mirror, PlanarReflection);
    frame(world);

    test_int(pipeline_of(world, mirror), BOX_RENDER_SHADER_MESH_EXTENDED);
    test_int(pipeline_of(world, plain), BOX_RENDER_SHADER_MESH);

    // Removing the component moves the shape back to the mesh shader
    ecs_remove(world, mirror, PlanarReflection);
    frame(world);
    test_int(pipeline_of(world, mirror), BOX_RENDER_SHADER_MESH);

    ecs_add(world, mirror, PlanarReflection);
    frame(world);
    test_int(pipeline_of(world, mirror), BOX_RENDER_SHADER_MESH_EXTENDED);

    ecs_fini(world);
    box_render_null_free(null);
}

void PlanarReflection_feature_off_draws_with_mesh_shader(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_singleton_set(world, Scene, {.planar_reflections = false});
    view_new(world, 0, true);
    ecs_entity_t mirror = ecs_new(world);
    ecs_set(world, mirror, Position, {0, 0, 0});
    ecs_set(world, mirror, Box, {4, 0.1f, 4});
    ecs_add(world, mirror, PlanarReflection);
    frame(world);

    // A scene without Scene.planar_reflections draws the shape as without the component
    test_int(pipeline_of(world, mirror), BOX_RENDER_SHADER_MESH);

    // Turning the feature on moves it to the extended mesh shader
    ecs_singleton_set(world, Scene, {.planar_reflections = true});
    frame(world);
    test_int(pipeline_of(world, mirror), BOX_RENDER_SHADER_MESH_EXTENDED);

    ecs_fini(world);
    box_render_null_free(null);
}
