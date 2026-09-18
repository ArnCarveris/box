/**
 * @file Flat.c
 * @brief Tests for the flat mesh generators: quad, triangle, right triangle and ngon.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/geometry/mesh_gen.h"

void Flat_quad(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_quad(&mesh);
    test_uint(mesh.vertex_count, 4);
    test_uint(mesh.index_count, 6);
    pos = mesh.positions;
    nrm = mesh.normals;
    uv = mesh.uvs;
    idx = mesh.indices;
    for (i = 0; i < mesh.index_count; i ++) {
        test_assert(idx[i] < mesh.vertex_count);
    }

    for (i = 0; i < mesh.vertex_count; i ++) {
        float len = sqrtf(nrm[i * 3] * nrm[i * 3] + nrm[i * 3 + 1] * nrm[i * 3 + 1]
            + nrm[i * 3 + 2] * nrm[i * 3 + 2]);

        test_assert(fabsf(len - 1.0f) < 1e-4f);
        test_assert(uv[i * 2] >= 0.0f && uv[i * 2] <= 1.0f);
        test_assert(uv[i * 2 + 1] >= 0.0f && uv[i * 2 + 1] <= 1.0f);
    }

    for (i = 0; i < mesh.index_count / 3; i ++) {
        uint32_t ia = idx[i * 3], ib = idx[i * 3 + 1], ic = idx[i * 3 + 2];
        vec3 pa = vec3_new(pos[ia * 3], pos[ia * 3 + 1], pos[ia * 3 + 2]);
        vec3 pb = vec3_new(pos[ib * 3], pos[ib * 3 + 1], pos[ib * 3 + 2]);
        vec3 pc = vec3_new(pos[ic * 3], pos[ic * 3 + 1], pos[ic * 3 + 2]);
        vec3 na = vec3_new(nrm[ia * 3], nrm[ia * 3 + 1], nrm[ia * 3 + 2]);
        vec3 nb = vec3_new(nrm[ib * 3], nrm[ib * 3 + 1], nrm[ib * 3 + 2]);
        vec3 nc = vec3_new(nrm[ic * 3], nrm[ic * 3 + 1], nrm[ic * 3 + 2]);
        vec3 face_n = vec3_cross(vec3_sub(pb, pa), vec3_sub(pc, pa));
        vec3 avg_n = vec3_scale(vec3_add(na, vec3_add(nb, nc)), 1.0f / 3.0f);

        test_assert(vec3_dot(face_n, avg_n) > 0.0f);
    }

    test_assert(fabsf(mesh.bounds_min.x - -0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_min.y - -0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_min.z - 0.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.z - 0.0f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Flat_triangle(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_triangle(&mesh);
    test_uint(mesh.vertex_count, 3);
    test_uint(mesh.index_count, 3);
    pos = mesh.positions;
    nrm = mesh.normals;
    uv = mesh.uvs;
    idx = mesh.indices;
    for (i = 0; i < mesh.index_count; i ++) {
        test_assert(idx[i] < mesh.vertex_count);
    }

    for (i = 0; i < mesh.vertex_count; i ++) {
        float len = sqrtf(nrm[i * 3] * nrm[i * 3] + nrm[i * 3 + 1] * nrm[i * 3 + 1]
            + nrm[i * 3 + 2] * nrm[i * 3 + 2]);

        test_assert(fabsf(len - 1.0f) < 1e-4f);
        test_assert(uv[i * 2] >= 0.0f && uv[i * 2] <= 1.0f);
        test_assert(uv[i * 2 + 1] >= 0.0f && uv[i * 2 + 1] <= 1.0f);
    }

    for (i = 0; i < mesh.index_count / 3; i ++) {
        uint32_t ia = idx[i * 3], ib = idx[i * 3 + 1], ic = idx[i * 3 + 2];
        vec3 pa = vec3_new(pos[ia * 3], pos[ia * 3 + 1], pos[ia * 3 + 2]);
        vec3 pb = vec3_new(pos[ib * 3], pos[ib * 3 + 1], pos[ib * 3 + 2]);
        vec3 pc = vec3_new(pos[ic * 3], pos[ic * 3 + 1], pos[ic * 3 + 2]);
        vec3 na = vec3_new(nrm[ia * 3], nrm[ia * 3 + 1], nrm[ia * 3 + 2]);
        vec3 nb = vec3_new(nrm[ib * 3], nrm[ib * 3 + 1], nrm[ib * 3 + 2]);
        vec3 nc = vec3_new(nrm[ic * 3], nrm[ic * 3 + 1], nrm[ic * 3 + 2]);
        vec3 face_n = vec3_cross(vec3_sub(pb, pa), vec3_sub(pc, pa));
        vec3 avg_n = vec3_scale(vec3_add(na, vec3_add(nb, nc)), 1.0f / 3.0f);

        test_assert(vec3_dot(face_n, avg_n) > 0.0f);
    }

    test_assert(fabsf(mesh.bounds_min.x - -0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_min.y - -0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 0.5f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Flat_right_triangle(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_right_triangle(&mesh);
    test_uint(mesh.vertex_count, 3);
    test_uint(mesh.index_count, 3);
    pos = mesh.positions;
    nrm = mesh.normals;
    uv = mesh.uvs;
    idx = mesh.indices;
    for (i = 0; i < mesh.index_count; i ++) {
        test_assert(idx[i] < mesh.vertex_count);
    }

    for (i = 0; i < mesh.vertex_count; i ++) {
        float len = sqrtf(nrm[i * 3] * nrm[i * 3] + nrm[i * 3 + 1] * nrm[i * 3 + 1]
            + nrm[i * 3 + 2] * nrm[i * 3 + 2]);

        test_assert(fabsf(len - 1.0f) < 1e-4f);
        test_assert(uv[i * 2] >= 0.0f && uv[i * 2] <= 1.0f);
        test_assert(uv[i * 2 + 1] >= 0.0f && uv[i * 2 + 1] <= 1.0f);
    }

    for (i = 0; i < mesh.index_count / 3; i ++) {
        uint32_t ia = idx[i * 3], ib = idx[i * 3 + 1], ic = idx[i * 3 + 2];
        vec3 pa = vec3_new(pos[ia * 3], pos[ia * 3 + 1], pos[ia * 3 + 2]);
        vec3 pb = vec3_new(pos[ib * 3], pos[ib * 3 + 1], pos[ib * 3 + 2]);
        vec3 pc = vec3_new(pos[ic * 3], pos[ic * 3 + 1], pos[ic * 3 + 2]);
        vec3 na = vec3_new(nrm[ia * 3], nrm[ia * 3 + 1], nrm[ia * 3 + 2]);
        vec3 nb = vec3_new(nrm[ib * 3], nrm[ib * 3 + 1], nrm[ib * 3 + 2]);
        vec3 nc = vec3_new(nrm[ic * 3], nrm[ic * 3 + 1], nrm[ic * 3 + 2]);
        vec3 face_n = vec3_cross(vec3_sub(pb, pa), vec3_sub(pc, pa));
        vec3 avg_n = vec3_scale(vec3_add(na, vec3_add(nb, nc)), 1.0f / 3.0f);

        test_assert(vec3_dot(face_n, avg_n) > 0.0f);
    }

    test_assert(fabsf(mesh.bounds_min.x - 0.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_min.y - 0.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 1.0f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Flat_ngon_matches_triangle_at_three_sides(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t triangle = {0};
    box_mesh_data_t ngon = {0};

    ecs_os_init();
    box_mesh_triangle(&triangle);
    box_mesh_ngon(&ngon, 3);
    test_uint(ngon.vertex_count, triangle.vertex_count);
    test_uint(ngon.index_count, triangle.index_count);
    box_mesh_data_fini(&triangle);
    box_mesh_data_fini(&ngon);
}

void Flat_ngon_clamps_minimum(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t below_one = {0};
    box_mesh_data_t below_two = {0};

    ecs_os_init();
    box_mesh_ngon(&below_one, 1);
    test_uint(below_one.vertex_count, 3);
    test_uint(below_one.index_count, 3);
    box_mesh_data_fini(&below_one);

    box_mesh_ngon(&below_two, 2);
    test_uint(below_two.vertex_count, 3);
    test_uint(below_two.index_count, 3);
    box_mesh_data_fini(&below_two);
}

void Flat_ngon(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_ngon(&mesh, 8);
    test_uint(mesh.vertex_count, 8);
    test_uint(mesh.index_count, 18);
    pos = mesh.positions;
    nrm = mesh.normals;
    uv = mesh.uvs;
    idx = mesh.indices;
    for (i = 0; i < mesh.index_count; i ++) {
        test_assert(idx[i] < mesh.vertex_count);
    }

    for (i = 0; i < mesh.vertex_count; i ++) {
        float len = sqrtf(nrm[i * 3] * nrm[i * 3] + nrm[i * 3 + 1] * nrm[i * 3 + 1]
            + nrm[i * 3 + 2] * nrm[i * 3 + 2]);

        test_assert(fabsf(len - 1.0f) < 1e-4f);
        test_assert(uv[i * 2] >= 0.0f && uv[i * 2] <= 1.0f);
        test_assert(uv[i * 2 + 1] >= 0.0f && uv[i * 2 + 1] <= 1.0f);
    }

    for (i = 0; i < mesh.index_count / 3; i ++) {
        uint32_t ia = idx[i * 3], ib = idx[i * 3 + 1], ic = idx[i * 3 + 2];
        vec3 pa = vec3_new(pos[ia * 3], pos[ia * 3 + 1], pos[ia * 3 + 2]);
        vec3 pb = vec3_new(pos[ib * 3], pos[ib * 3 + 1], pos[ib * 3 + 2]);
        vec3 pc = vec3_new(pos[ic * 3], pos[ic * 3 + 1], pos[ic * 3 + 2]);
        vec3 na = vec3_new(nrm[ia * 3], nrm[ia * 3 + 1], nrm[ia * 3 + 2]);
        vec3 nb = vec3_new(nrm[ib * 3], nrm[ib * 3 + 1], nrm[ib * 3 + 2]);
        vec3 nc = vec3_new(nrm[ic * 3], nrm[ic * 3 + 1], nrm[ic * 3 + 2]);
        vec3 face_n = vec3_cross(vec3_sub(pb, pa), vec3_sub(pc, pa));
        vec3 avg_n = vec3_scale(vec3_add(na, vec3_add(nb, nc)), 1.0f / 3.0f);

        test_assert(vec3_dot(face_n, avg_n) > 0.0f);
    }

    test_assert(fabsf(mesh.bounds_min.x - -1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_min.y - -1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 1.0f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}
