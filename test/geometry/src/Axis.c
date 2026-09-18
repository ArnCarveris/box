/**
 * @file Axis.c
 * @brief Tests for the axis mesh generators: cone, cylinder, frustum, capsule, tube and torus.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/geometry/mesh_gen.h"

void Axis_cone_smooth(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_cone(&mesh, 8, true);
    test_uint(mesh.vertex_count, 27);
    test_uint(mesh.index_count, 48);
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
    test_assert(fabsf(mesh.bounds_min.y - -0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_min.z - -1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.z - 1.0f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Axis_cone_flat(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_cone(&mesh, 8, false);
    test_uint(mesh.vertex_count, 48);
    test_uint(mesh.index_count, 48);
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
    test_assert(fabsf(mesh.bounds_min.y - -0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 0.5f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Axis_cylinder_smooth(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_cylinder(&mesh, 8, true);
    test_uint(mesh.vertex_count, 36);
    test_uint(mesh.index_count, 96);
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
    test_assert(fabsf(mesh.bounds_min.y - -0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_min.z - -1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.z - 1.0f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Axis_cylinder_flat(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_cylinder(&mesh, 8, false);
    test_uint(mesh.vertex_count, 96);
    test_uint(mesh.index_count, 96);
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
    test_assert(fabsf(mesh.bounds_min.y - -0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 0.5f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Axis_frustum_smooth(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_frustum(&mesh, 8, true, 0.5f);
    test_uint(mesh.vertex_count, 36);
    test_uint(mesh.index_count, 96);
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
    test_assert(fabsf(mesh.bounds_min.y - -0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_min.z - -1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.z - 1.0f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Axis_frustum_flat(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_frustum(&mesh, 8, false, 0.5f);
    test_uint(mesh.vertex_count, 96);
    test_uint(mesh.index_count, 96);
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
    test_assert(fabsf(mesh.bounds_min.y - -0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 0.5f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Axis_capsule_smooth(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_capsule(&mesh, 8, true, 1.0f);
    test_uint(mesh.vertex_count, 180);
    test_uint(mesh.index_count, 768);
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
    test_assert(fabsf(mesh.bounds_min.y - -1.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_min.z - -1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 1.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.z - 1.0f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Axis_capsule_flat(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_capsule(&mesh, 8, false, 1.0f);
    test_uint(mesh.vertex_count, 768);
    test_uint(mesh.index_count, 768);
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

    test_assert(fabsf(mesh.bounds_min.y - -1.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 1.5f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Axis_tube_smooth(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_tube(&mesh, 8, true, 0.3f, 6.28318530718f);
    test_uint(mesh.vertex_count, 72);
    test_uint(mesh.index_count, 192);
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
    test_assert(fabsf(mesh.bounds_min.y - -0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_min.z - -1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 0.5f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.z - 1.0f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Axis_tube_flat(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_tube(&mesh, 8, false, 0.3f, 6.28318530718f);
    test_uint(mesh.vertex_count, 192);
    test_uint(mesh.index_count, 192);
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

    box_mesh_data_fini(&mesh);
}

void Axis_tube_open_arc(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_tube(&mesh, 8, true, 0.3f, 3.14159265359f);
    test_uint(mesh.vertex_count, 80);
    test_uint(mesh.index_count, 204);
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
    test_assert(mesh.bounds_min.z > -1e-4f && mesh.bounds_min.z < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.z - 1.0f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Axis_torus_smooth(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_torus(&mesh, 8, 8, true, 0.3f);
    test_uint(mesh.vertex_count, 81);
    test_uint(mesh.index_count, 384);
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

    test_assert(fabsf(mesh.bounds_min.x - -1.3f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_min.y - -0.3f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_min.z - -1.3f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 1.3f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 0.3f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.z - 1.3f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Axis_torus_flat(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_torus(&mesh, 8, 8, false, 0.3f);
    test_uint(mesh.vertex_count, 384);
    test_uint(mesh.index_count, 384);
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

    box_mesh_data_fini(&mesh);
}
