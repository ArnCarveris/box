/**
 * @file Box.c
 * @brief Tests for the box mesh generators: box, rounded box and the two prisms.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/geometry/mesh_gen.h"

void Box_box(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_box(&mesh);
    test_uint(mesh.vertex_count, 24);
    test_uint(mesh.index_count, 36);
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
        vec3 p = vec3_new(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]);
        vec3 n = vec3_new(nrm[i * 3], nrm[i * 3 + 1], nrm[i * 3 + 2]);

        test_assert(fabsf(len - 1.0f) < 1e-4f);
        test_assert(uv[i * 2] >= 0.0f && uv[i * 2] <= 1.0f);
        test_assert(uv[i * 2 + 1] >= 0.0f && uv[i * 2 + 1] <= 1.0f);
        test_assert(vec3_dot(n, p) > 0.0f);
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
    test_assert(fabsf(mesh.bounds_min.z - -0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.z - 0.5f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Box_rounded_box(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_rounded_box(&mesh, 2.0f, 2.0f, 2.0f, 0.3f, 3);
    test_uint(mesh.vertex_count, 96);
    test_uint(mesh.index_count, 324);
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
        vec3 p = vec3_new(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]);
        vec3 n = vec3_new(nrm[i * 3], nrm[i * 3 + 1], nrm[i * 3 + 2]);

        test_assert(fabsf(len - 1.0f) < 1e-4f);
        test_assert(uv[i * 2] >= 0.0f && uv[i * 2] <= 1.0f);
        test_assert(uv[i * 2 + 1] >= 0.0f && uv[i * 2 + 1] <= 1.0f);
        test_assert(vec3_dot(n, p) > 0.0f);
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
    test_assert(fabsf(mesh.bounds_min.z - -1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.z - 1.0f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Box_rounded_box_clamps_radius(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};

    ecs_os_init();
    box_mesh_rounded_box(&mesh, 1.0f, 1.0f, 1.0f, 10.0f, 2);
    test_uint(mesh.vertex_count, 54);
    test_uint(mesh.index_count, 144);
    test_assert(fabsf(mesh.bounds_min.x - -0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_min.y - -0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_min.z - -0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.z - 0.5f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Box_rounded_box_clamps_segments(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};

    ecs_os_init();
    box_mesh_rounded_box(&mesh, 2.0f, 2.0f, 2.0f, 0.3f, 0);
    test_uint(mesh.vertex_count, 24);
    test_uint(mesh.index_count, 36);
    box_mesh_data_fini(&mesh);
}

void Box_triangle_prism(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_triangle_prism(&mesh);
    test_uint(mesh.vertex_count, 18);
    test_uint(mesh.index_count, 24);
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
    test_assert(fabsf(mesh.bounds_min.z - -0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.z - 0.5f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Box_right_triangle_prism(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_right_triangle_prism(&mesh);
    test_uint(mesh.vertex_count, 18);
    test_uint(mesh.index_count, 24);
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
    test_assert(fabsf(mesh.bounds_min.z - -0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.z - 0.5f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}
