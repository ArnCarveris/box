/**
 * @file face_uvs.h
 * @brief Check of the texture orientation on the flat faces of a box mesh, shared by the Box and
 * Bevel suites.
 */

#ifndef TEST_GEOMETRY_FACE_UVS_H
#define TEST_GEOMETRY_FACE_UVS_H

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "mesh_cache.h"

/** Get the directions u and v increase along on a face of a box, seen from outside: on a side
 * face u along up x normal and v toward -y, on the top u along +z, on the bottom u along -z, and
 * v along -x on both.
 *
 * @param k Axis of the face normal
 * @param sign Side of the face, 1 or -1
 * @param u Receives the direction of u
 * @param v Receives the direction of v
 */
static void face_uvs_axes(
    int32_t k,
    float sign,
    vec3 u,
    vec3 v)
{
    glm_vec3_zero(u);
    glm_vec3_zero(v);
    if (k == 1) {
        u[2] = sign;
        v[0] = -1.0f;
    } else {
        vec3 n = {0}, up = { 0.0f, 1.0f, 0.0f };
        n[k] = sign;
        glm_vec3_cross(up, n, u);
        v[1] = -1.0f;
    }
}

/** Check that the texture coordinates of every triangle on a flat face of a box mesh, whose
 * three normals are the face normal, increase along the directions of face_uvs_axes.
 *
 * @param mesh The mesh
 * @return Bit 2 * k + (sign > 0) set for each face with a triangle checked
 */
static int32_t face_uvs_check(
    const box_mesh_t *mesh)
{
    int32_t count = mesh->index_count ? mesh->index_count : mesh->vertex_count;
    int32_t result = 0;
    for (int32_t i = 0; i + 2 < count; i += 3) {
        const box_vertex_t *t[3];
        for (int32_t j = 0; j < 3; j ++) {
            uint32_t index = mesh->index_count ? mesh->indices[i + j] : (uint32_t)(i + j);
            t[j] = &mesh->vertices[index];
        }

        int32_t face = -1;
        for (int32_t k = 0; k < 3 && face < 0; k ++) {
            bool flat = true;
            for (int32_t j = 0; j < 3; j ++) {
                flat = flat && fabsf(t[j]->normal[k]) > 0.99999f &&
                    t[j]->normal[k] * t[0]->normal[k] > 0.0f;
            }

            face = flat ? k : -1;
        }

        if (face < 0) {
            continue;
        }

        float sign = t[0]->normal[face] > 0.0f ? 1.0f : -1.0f;
        vec3 du, dv;
        face_uvs_axes(face, sign, du, dv);
        for (int32_t j = 0; j < 3; j ++) {
            const box_vertex_t *a = t[j], *b = t[(j + 1) % 3];
            vec3 d;
            glm_vec3_sub(ECS_CONST_CAST(float*, b->position),
                ECS_CONST_CAST(float*, a->position), d);
            float along_u = glm_vec3_dot(d, du), along_v = glm_vec3_dot(d, dv);
            float delta_u = b->uv[0] - a->uv[0], delta_v = b->uv[1] - a->uv[1];
            if (fabsf(along_u) > 1e-5f) {
                test_assert(delta_u * along_u > 0.0f);
            } else {
                test_assert(fabsf(delta_u) < 1e-4f);
            }

            if (fabsf(along_v) > 1e-5f) {
                test_assert(delta_v * along_v > 0.0f);
            } else {
                test_assert(fabsf(delta_v) < 1e-4f);
            }
        }

        result |= 1 << (2 * face + (sign > 0.0f ? 1 : 0));
    }

    return result;
}

#endif
