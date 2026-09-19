/**
 * @file Mesh.c
 * @brief Tests for SplineMesh: the swept mesh, its layout, rebuilds and sharing.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/spline/spline_internal.h"

void Mesh_strip_without_a_profile(void)
{
    // splines/spline_roads
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 10);
    ecs_modified(w, curve, Spline);

    ecs_entity_t road = ecs_new(w);
    ecs_set(w, road, SplineMesh, {.spline = curve, .segments = 4, .width = 4, .height = 1});

    const Mesh3 *mesh = ecs_get(w, road, Mesh3);
    test_assert(mesh != NULL);
    test_int(ecs_vec_count(&mesh->vertices), 10);
    test_int(ecs_vec_count(&mesh->indices), 24);

    const vec3 *v = ecs_vec_first_t(&mesh->vertices, vec3);
    test_flt(v[0].x, -2);
    test_flt(v[0].y, 0);
    test_flt(v[0].z, 0);
    test_flt(v[1].x, 2);
    test_flt(v[1].z, 0);
    test_flt(v[8].x, -2);
    test_flt(v[8].z, 10);
    test_flt(v[9].x, 2);
    test_flt(v[9].z, 10);

    ecs_fini(w);
}

void Mesh_slices_follow_the_segment_count(void)
{
    // splines/spline_roads
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 3);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 4);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 8);
    ecs_modified(w, curve, Spline);

    ecs_entity_t road = ecs_new(w);
    ecs_set(w, road, SplineMesh, {.spline = curve, .segments = 5, .width = 2, .height = 1});

    const Mesh3 *mesh = ecs_get(w, road, Mesh3);
    test_assert(mesh != NULL);
    test_int(ecs_vec_count(&mesh->vertices), 12);
    test_int(ecs_vec_count(&mesh->indices), 30);

    ecs_fini(w);
}

void Mesh_default_slices_per_curve_segment(void)
{
    // splines/spline_roads
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 3);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 4);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 8);
    ecs_modified(w, curve, Spline);

    ecs_entity_t road = ecs_new(w);
    ecs_set(w, road, SplineMesh, {.spline = curve, .width = 2, .height = 1});

    const Mesh3 *mesh = ecs_get(w, road, Mesh3);
    test_assert(mesh != NULL);
    test_int(ecs_vec_count(&mesh->vertices), (2 * 8 + 1) * 2);
    test_int(ecs_vec_count(&mesh->indices), 2 * 8 * 6);

    ecs_fini(w);
}

void Mesh_profile_is_swept_across_the_curve(void)
{
    // splines/spline_roads
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 6);
    ecs_modified(w, curve, Spline);

    ecs_entity_t road = ecs_new(w);
    SplineMesh *sweep = ecs_ensure(w, road, SplineMesh);
    sweep->spline = curve;
    sweep->segments = 2;
    sweep->width = 1;
    sweep->height = 1;
    ecs_vec_init_t(NULL, &sweep->profile, vec2, 3);
    ecs_vec_append_t(NULL, &sweep->profile, vec2)[0] = (vec2){-3, 0};
    ecs_vec_append_t(NULL, &sweep->profile, vec2)[0] = (vec2){0, 1};
    ecs_vec_append_t(NULL, &sweep->profile, vec2)[0] = (vec2){3, 0};
    ecs_modified(w, road, SplineMesh);

    const Mesh3 *mesh = ecs_get(w, road, Mesh3);
    test_assert(mesh != NULL);
    test_int(ecs_vec_count(&mesh->vertices), 12);
    test_int(ecs_vec_count(&mesh->indices), 24);

    const vec3 *v = ecs_vec_first_t(&mesh->vertices, vec3);
    test_flt(v[0].x, -3);
    test_flt(v[0].y, 0);
    test_flt(v[1].x, 0);
    test_flt(v[1].y, 1);
    test_flt(v[2].x, 0);
    test_flt(v[2].y, 1);
    test_flt(v[3].x, 3);
    test_flt(v[3].y, 0);

    ecs_fini(w);
}

void Mesh_normals_face_up_for_a_flat_strip(void)
{
    // splines/spline_roads
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 8);
    ecs_modified(w, curve, Spline);

    ecs_entity_t road = ecs_new(w);
    ecs_set(w, road, SplineMesh, {.spline = curve, .segments = 2, .width = 3, .height = 1});

    const Mesh3 *mesh = ecs_get(w, road, Mesh3);
    test_assert(mesh != NULL);

    const vec3 *n = ecs_vec_first_t(&mesh->normals, vec3);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&mesh->normals); i ++) {
        test_flt(n[i].x, 0);
        test_flt(n[i].y, 1);
        test_flt(n[i].z, 0);
    }

    const uint32_t *index = ecs_vec_first_t(&mesh->indices, uint32_t);
    const vec3 *v = ecs_vec_first_t(&mesh->vertices, vec3);
    vec3 a = v[index[0]];
    vec3 face = vec3_cross(vec3_sub(v[index[1]], a), vec3_sub(v[index[2]], a));
    test_assert(face.y > 0);

    ecs_fini(w);
}

void Mesh_uvs_run_along_the_curve(void)
{
    // splines/spline_roads
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 12);
    ecs_modified(w, curve, Spline);

    ecs_entity_t road = ecs_new(w);
    ecs_set(w, road, SplineMesh, {.spline = curve, .segments = 3, .width = 5, .height = 1});

    const Mesh3 *mesh = ecs_get(w, road, Mesh3);
    test_assert(mesh != NULL);

    const vec2 *uv = ecs_vec_first_t(&mesh->uvs, vec2);
    test_flt(uv[0].x, 0);
    test_flt(uv[0].y, 0);
    test_flt(uv[1].x, 5);
    test_flt(uv[1].y, 0);
    test_flt(uv[2].y, 4);
    test_flt(uv[6].y, 12);
    test_flt(uv[7].x, 5);

    ecs_fini(w);
}

void Mesh_a_loop_closes_the_sweep(void)
{
    // splines/spline_roads
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    s->loop = true;
    ecs_vec_init_t(NULL, &s->points, vec3, 4);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(4, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(4, 0, 4);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 4);
    ecs_modified(w, curve, Spline);

    ecs_entity_t road = ecs_new(w);
    ecs_set(w, road, SplineMesh, {.spline = curve, .segments = 8, .width = 1, .height = 1});

    const Mesh3 *mesh = ecs_get(w, road, Mesh3);
    test_assert(mesh != NULL);
    test_int(ecs_vec_count(&mesh->vertices), 16);
    test_int(ecs_vec_count(&mesh->indices), 48);

    const uint32_t *index = ecs_vec_first_t(&mesh->indices, uint32_t);
    test_uint(index[42], 14);
    test_uint(index[43], 1);
    test_uint(index[44], 15);

    ecs_fini(w);
}

void Mesh_closed_profile_sweeps_a_ring(void)
{
    // splines/spline_roads
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 5);
    ecs_modified(w, curve, Spline);

    ecs_entity_t pipe = ecs_new(w);
    SplineMesh *sweep = ecs_ensure(w, pipe, SplineMesh);
    sweep->spline = curve;
    sweep->segments = 2;
    sweep->width = 1;
    sweep->height = 1;
    sweep->closed = true;
    ecs_vec_init_t(NULL, &sweep->profile, vec2, 4);
    ecs_vec_append_t(NULL, &sweep->profile, vec2)[0] = (vec2){-1, -1};
    ecs_vec_append_t(NULL, &sweep->profile, vec2)[0] = (vec2){-1, 1};
    ecs_vec_append_t(NULL, &sweep->profile, vec2)[0] = (vec2){1, 1};
    ecs_vec_append_t(NULL, &sweep->profile, vec2)[0] = (vec2){1, -1};
    ecs_modified(w, pipe, SplineMesh);

    const Mesh3 *mesh = ecs_get(w, pipe, Mesh3);
    test_assert(mesh != NULL);
    test_int(ecs_vec_count(&mesh->vertices), 24);
    test_int(ecs_vec_count(&mesh->indices), 48);

    const RenderMesh *render = ecs_get(w, pipe, RenderMesh);
    test_assert(render != NULL);
    test_flt(render->bounds_min.y, -1);
    test_flt(render->bounds_max.y, 1);

    ecs_fini(w);
}

void Mesh_smooth_shares_the_columns(void)
{
    // splines/spline_roads
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 5);
    ecs_modified(w, curve, Spline);

    ecs_entity_t cable = ecs_new(w);
    SplineMesh *sweep = ecs_ensure(w, cable, SplineMesh);
    sweep->spline = curve;
    sweep->segments = 2;
    sweep->width = 1;
    sweep->height = 1;
    sweep->closed = true;
    sweep->smooth = true;
    ecs_vec_init_t(NULL, &sweep->profile, vec2, 4);
    ecs_vec_append_t(NULL, &sweep->profile, vec2)[0] = (vec2){-1, -1};
    ecs_vec_append_t(NULL, &sweep->profile, vec2)[0] = (vec2){-1, 1};
    ecs_vec_append_t(NULL, &sweep->profile, vec2)[0] = (vec2){1, 1};
    ecs_vec_append_t(NULL, &sweep->profile, vec2)[0] = (vec2){1, -1};
    ecs_modified(w, cable, SplineMesh);

    const Mesh3 *mesh = ecs_get(w, cable, Mesh3);
    test_assert(mesh != NULL);
    test_int(ecs_vec_count(&mesh->vertices), 12);
    test_int(ecs_vec_count(&mesh->indices), 48);

    const vec3 *n = ecs_vec_first_t(&mesh->normals, vec3);
    test_flt(n[0].x, -(float)sqrt(0.5));
    test_flt(n[0].y, -(float)sqrt(0.5));

    ecs_fini(w);
}

void Mesh_the_mesh_follows_the_curve(void)
{
    // splines/spline_roads
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 4);
    ecs_modified(w, curve, Spline);

    ecs_entity_t road = ecs_new(w);
    ecs_set(w, road, SplineMesh, {.spline = curve, .segments = 2, .width = 2, .height = 1});

    const Mesh3 *mesh = ecs_get(w, road, Mesh3);
    test_assert(mesh != NULL);
    test_flt(ecs_vec_first_t(&mesh->vertices, vec3)[5].z, 4);

    Spline *longer = ecs_get_mut(w, curve, Spline);
    test_assert(longer != NULL);
    ecs_vec_first_t(&longer->points, vec3)[1].z = 9;
    ecs_modified(w, curve, Spline);

    mesh = ecs_get(w, road, Mesh3);
    test_assert(mesh != NULL);
    test_int(ecs_vec_count(&mesh->vertices), 6);
    test_flt(ecs_vec_first_t(&mesh->vertices, vec3)[5].z, 9);

    ecs_fini(w);
}

void Mesh_the_mesh_follows_the_profile(void)
{
    // splines/spline_roads
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 4);
    ecs_modified(w, curve, Spline);

    ecs_entity_t road = ecs_new(w);
    ecs_set(w, road, SplineMesh, {.spline = curve, .segments = 2, .width = 2, .height = 1});

    const RenderMesh *first = ecs_get(w, road, RenderMesh);
    test_assert(first != NULL);
    uint32_t handle = first->handle;

    SplineMesh *wider = ecs_get_mut(w, road, SplineMesh);
    test_assert(wider != NULL);
    wider->width = 8;
    ecs_modified(w, road, SplineMesh);

    const Mesh3 *mesh = ecs_get(w, road, Mesh3);
    test_assert(mesh != NULL);
    test_flt(ecs_vec_first_t(&mesh->vertices, vec3)[0].x, -4);

    const RenderMesh *second = ecs_get(w, road, RenderMesh);
    test_assert(second != NULL);
    test_assert(second->handle != handle);

    ecs_fini(w);
}

void Mesh_bounds_come_from_the_curve(void)
{
    // splines/spline_roads
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 1, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 1, 10);
    ecs_modified(w, curve, Spline);

    ecs_entity_t road = ecs_new(w);
    ecs_set(w, road, SplineMesh, {.spline = curve, .segments = 4, .width = 6, .height = 1});

    const RenderMesh *render = ecs_get(w, road, RenderMesh);
    test_assert(render != NULL);
    test_flt(render->bounds_min.x, -3);
    test_flt(render->bounds_min.y, 1);
    test_flt(render->bounds_min.z, 0);
    test_flt(render->bounds_max.x, 3);
    test_flt(render->bounds_max.y, 1);
    test_flt(render->bounds_max.z, 10);

    ecs_fini(w);
}

void Mesh_the_sweep_uses_the_curve_of_the_entity(void)
{
    // splines/spline_roads
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t road = ecs_new(w);
    ecs_set(w, road, SplineMesh, {.segments = 2, .width = 2, .height = 1});
    test_assert(ecs_get(w, road, Mesh3) == NULL);

    Spline *s = ecs_ensure(w, road, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 3);
    ecs_modified(w, road, Spline);

    const Mesh3 *mesh = ecs_get(w, road, Mesh3);
    test_assert(mesh != NULL);
    test_int(ecs_vec_count(&mesh->vertices), 6);
    test_flt(ecs_vec_first_t(&mesh->vertices, vec3)[5].z, 3);

    ecs_fini(w);
}

void Mesh_a_sweep_without_a_curve_builds_nothing(void)
{
    // splines/spline_roads
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t road = ecs_new(w);
    ecs_set(w, road, SplineMesh, {.spline = ecs_new(w), .width = 2, .height = 1});

    test_assert(ecs_get(w, road, Mesh3) == NULL);
    test_assert(ecs_get(w, road, RenderMesh) == NULL);

    ecs_fini(w);
}

void Mesh_removing_the_sweep_removes_the_mesh(void)
{
    // splines/spline_roads
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 4);
    ecs_modified(w, curve, Spline);

    ecs_entity_t road = ecs_new(w);
    ecs_set(w, road, SplineMesh, {.spline = curve, .segments = 2, .width = 2, .height = 1});
    test_assert(ecs_get(w, road, Mesh3) != NULL);

    ecs_remove(w, road, SplineMesh);
    test_assert(ecs_get(w, road, Mesh3) == NULL);
    test_assert(ecs_get(w, road, RenderMesh) == NULL);

    ecs_fini(w);
}

void Mesh_deleting_the_curve_removes_the_mesh(void)
{
    // splines/spline_roads
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 4);
    ecs_modified(w, curve, Spline);

    ecs_entity_t road = ecs_new(w);
    ecs_set(w, road, SplineMesh, {.spline = curve, .segments = 2, .width = 2, .height = 1});
    test_assert(ecs_get(w, road, Mesh3) != NULL);

    ecs_delete(w, curve);
    test_assert(ecs_get(w, road, Mesh3) == NULL);
    test_assert(ecs_get(w, road, RenderMesh) == NULL);
    test_assert(ecs_get(w, road, SplineMesh) != NULL);

    ecs_fini(w);
}

void Mesh_the_entity_stays_a_static_instance(void)
{
    // splines/spline_roads
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 4);
    ecs_modified(w, curve, Spline);

    ecs_entity_t road = ecs_new(w);
    ecs_set(w, road, SplineMesh, {.spline = curve, .segments = 2, .width = 2, .height = 1});

    Spline *moved = ecs_get_mut(w, curve, Spline);
    test_assert(moved != NULL);
    ecs_vec_first_t(&moved->points, vec3)[1].z = 6;
    ecs_modified(w, curve, Spline);

    test_assert(!ecs_has_id(w, road, DynamicTransform));
    test_assert(ecs_get(w, road, Mesh3) != NULL);

    ecs_fini(w);
}

void Mesh_two_sweeps_share_one_mesh(void)
{
    // splines/spline_roads
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    ecs_entity_t curve = ecs_new(w);
    Spline *s = ecs_ensure(w, curve, Spline);
    s->type = SplineLinear;
    ecs_vec_init_t(NULL, &s->points, vec3, 2);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 0);
    ecs_vec_append_t(NULL, &s->points, vec3)[0] = vec3_new(0, 0, 4);
    ecs_modified(w, curve, Spline);

    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, SplineMesh, {.spline = curve, .segments = 2, .width = 2, .height = 1});

    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, SplineMesh, {.spline = curve, .segments = 2, .width = 2, .height = 1});

    const RenderMesh *first = ecs_get(w, a, RenderMesh);
    const RenderMesh *second = ecs_get(w, b, RenderMesh);
    test_assert(first != NULL);
    test_assert(second != NULL);
    test_uint(first->handle, second->handle);

    ecs_fini(w);
}

void Mesh_a_sweep_from_a_script(void)
{
    // splines/spline_roads
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxSpline);

    test_int(ecs_script_run(w, "test",
        "using box.spline\n"
        "track {\n"
        "  Spline: {points: [{0, 0, 0}, {0, 0, 8}], type: SplineLinear}\n"
        "}\n"
        "road {\n"
        "  SplineMesh: {spline: track, segments: 4, width: 6, height: 1,\n"
        "    profile: [{-0.5, 0}, {0.5, 0}]}\n"
        "}\n", NULL), 0);

    ecs_entity_t road = ecs_lookup(w, "road");
    test_assert(road != 0);

    const Mesh3 *mesh = ecs_get(w, road, Mesh3);
    test_assert(mesh != NULL);
    test_int(ecs_vec_count(&mesh->vertices), 10);
    test_flt(ecs_vec_first_t(&mesh->vertices, vec3)[0].x, -3);
    test_flt(ecs_vec_first_t(&mesh->vertices, vec3)[9].z, 8);

    ecs_fini(w);
}
