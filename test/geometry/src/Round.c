/**
 * @file Round.c
 * @brief Tests for the round mesh generators: sphere, hemisphere and icosphere.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/geometry/mesh_gen.h"

void Round_sphere_smooth(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_sphere(&mesh, 8, true);
    test_uint(mesh.vertex_count, 162);
    test_uint(mesh.index_count, 720);
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

void Round_sphere_flat(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_sphere(&mesh, 8, false);
    test_uint(mesh.vertex_count, 720);
    test_uint(mesh.index_count, 720);
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

void Round_hemisphere_smooth(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_hemisphere(&mesh, 8, true);
    test_uint(mesh.vertex_count, 90);
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

    test_assert(fabsf(mesh.bounds_min.x - -1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_min.y - 0.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_min.z - -1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.z - 1.0f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Round_hemisphere_flat(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_hemisphere(&mesh, 8, false);
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

    test_assert(fabsf(mesh.bounds_min.x - -1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_min.y - 0.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_min.z - -1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - 1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - 1.0f) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.z - 1.0f) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Round_icosphere_base_smooth(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;
    float m = 0.85065081f;

    ecs_os_init();
    box_mesh_icosphere(&mesh, 0, true);
    test_uint(mesh.vertex_count, 12);
    test_uint(mesh.index_count, 60);
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

    test_assert(fabsf(mesh.bounds_min.x - -m) < 1e-4f);
    test_assert(fabsf(mesh.bounds_min.y - -m) < 1e-4f);
    test_assert(fabsf(mesh.bounds_min.z - -m) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.x - m) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.y - m) < 1e-4f);
    test_assert(fabsf(mesh.bounds_max.z - m) < 1e-4f);
    box_mesh_data_fini(&mesh);
}

void Round_icosphere_base_flat(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t mesh = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_icosphere(&mesh, 0, false);
    test_uint(mesh.vertex_count, 60);
    test_uint(mesh.index_count, 60);
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

void Round_icosphere_subdivided(void)
{
    // scene_core/mesh_gen
    box_mesh_data_t smooth = {0};
    box_mesh_data_t flat = {0};
    float *pos, *nrm, *uv;
    uint32_t *idx;
    uint32_t i;

    ecs_os_init();
    box_mesh_icosphere(&smooth, 2, true);
    test_uint(smooth.vertex_count, 162);
    test_uint(smooth.index_count, 960);
    box_mesh_icosphere(&flat, 2, false);
    test_uint(flat.vertex_count, 960);
    test_uint(flat.index_count, 960);
    pos = smooth.positions;
    nrm = smooth.normals;
    uv = smooth.uvs;
    idx = smooth.indices;
    for (i = 0; i < smooth.index_count; i ++) {
        test_assert(idx[i] < smooth.vertex_count);
    }

    for (i = 0; i < smooth.vertex_count; i ++) {
        float len = sqrtf(nrm[i * 3] * nrm[i * 3] + nrm[i * 3 + 1] * nrm[i * 3 + 1]
            + nrm[i * 3 + 2] * nrm[i * 3 + 2]);
        vec3 p = vec3_new(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]);
        vec3 n = vec3_new(nrm[i * 3], nrm[i * 3 + 1], nrm[i * 3 + 2]);

        test_assert(fabsf(len - 1.0f) < 1e-4f);
        test_assert(uv[i * 2] >= 0.0f && uv[i * 2] <= 1.0f);
        test_assert(uv[i * 2 + 1] >= 0.0f && uv[i * 2 + 1] <= 1.0f);
        test_assert(vec3_dot(n, p) > 0.0f);
    }

    test_assert(fabsf(smooth.bounds_min.x - -smooth.bounds_max.x) < 1e-4f);
    test_assert(fabsf(smooth.bounds_min.y - -smooth.bounds_max.y) < 1e-4f);
    test_assert(fabsf(smooth.bounds_min.z - -smooth.bounds_max.z) < 1e-4f);
    test_assert(smooth.bounds_max.x > 0.5f && smooth.bounds_max.x <= 1.0001f);
    test_assert(smooth.bounds_max.y > 0.5f && smooth.bounds_max.y <= 1.0001f);
    test_assert(smooth.bounds_max.z > 0.5f && smooth.bounds_max.z <= 1.0001f);
    box_mesh_data_fini(&smooth);
    box_mesh_data_fini(&flat);
}
