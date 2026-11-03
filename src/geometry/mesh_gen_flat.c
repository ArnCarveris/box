/**
 * @file geometry/mesh_gen_flat.c
 * @brief Generators of the shapes made of flat faces: box, quad, triangles, prisms and ngon.
 */

#include "mesh_cache.h"
#include "mesh_gen.h"
#include <math.h>

/** Append a quad with four corners and one normal as two triangles.
 *
 * @param mesh The mesh
 * @param corners The corners in order around the quad
 * @param uvs The texture coordinates of the corners
 * @param normal The normal
 */
static void box_mesh_quad_face(
    box_mesh_t *mesh,
    vec3 corners[4],
    vec2 uvs[4],
    const vec3 normal)
{
    int32_t idx[4];
    for (int32_t i = 0; i < 4; i ++) {
        idx[i] = box_mesh_vertex(mesh, corners[i], normal, uvs[i][0], uvs[i][1]);
    }

    box_mesh_triangle(mesh, idx[0], idx[1], idx[2]);
    box_mesh_triangle(mesh, idx[0], idx[2], idx[3]);
}

void box_mesh_box_face_uv(
    int32_t k,
    float sign,
    const vec3 pos,
    const vec3 h,
    vec2 out)
{
    float tu, tv;
    if (k == 1) {
        tu = 0.5f + 0.5f * sign * pos[2] / h[2];
        tv = 0.5f - 0.5f * pos[0] / h[0];
    } else if (k == 0) {
        tu = 0.5f - 0.5f * sign * pos[2] / h[2];
        tv = 0.5f - 0.5f * pos[1] / h[1];
    } else {
        tu = 0.5f + 0.5f * sign * pos[0] / h[0];
        tv = 0.5f - 0.5f * pos[1] / h[1];
    }

    out[0] = glm_clamp(tu, 0.0f, 1.0f);
    out[1] = glm_clamp(tv, 0.0f, 1.0f);
}

void box_mesh_gen_box(
    box_mesh_t *mesh)
{
    static const float su[4] = { -0.5f, 0.5f, 0.5f, -0.5f };
    static const float sv[4] = { -0.5f, -0.5f, 0.5f, 0.5f };
    for (int32_t k = 0; k < 3; k ++) {
        int32_t u = (k + 1) % 3, v = (k + 2) % 3;
        for (int32_t sign = -1; sign <= 1; sign += 2) {
            vec3 normal = {0};
            normal[k] = (float)sign;
            vec3 corners[4];
            vec2 uvs[4];
            for (int32_t i = 0; i < 4; i ++) {
                corners[i][k] = 0.5f * (float)sign;
                corners[i][u] = su[i];
                corners[i][v] = sv[i];
            }

            for (int32_t i = 0; i < 4; i ++) {
                box_mesh_box_face_uv(k, (float)sign, corners[i],
                    (vec3){ 0.5f, 0.5f, 0.5f }, uvs[i]);
            }

            box_mesh_quad_face(mesh, corners, uvs, normal);
        }
    }
}

void box_mesh_gen_quad(
    box_mesh_t *mesh)
{
    vec3 corners[4] = {
        { -0.5f, -0.5f, 0.0f }, { 0.5f, -0.5f, 0.0f },
        { 0.5f, 0.5f, 0.0f }, { -0.5f, 0.5f, 0.0f }
    };
    vec2 uvs[4] = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } };
    box_mesh_quad_face(mesh, corners, uvs, (vec3){ 0.0f, 0.0f, 1.0f });
}

/** Get the corners of the unit triangle in the XY plane.
 *
 * @param right True for a right triangle
 * @param out The three corners
 */
static void box_mesh_triangle_corners(
    bool right,
    vec2 out[3])
{
    out[0][0] = -0.5f;
    out[0][1] = -0.5f;
    out[1][0] = 0.5f;
    out[1][1] = -0.5f;
    out[2][0] = right ? -0.5f : 0.0f;
    out[2][1] = 0.5f;
}

/** Append a flat triangle cap at a depth.
 *
 * @param mesh The mesh
 * @param pts The corners in the XY plane
 * @param z The depth
 * @param nz The Z component of the normal, 1 or -1
 */
static void box_mesh_triangle_cap(
    box_mesh_t *mesh,
    vec2 pts[3],
    float z,
    float nz)
{
    vec3 normal = { 0.0f, 0.0f, nz };
    int32_t idx[3];
    for (int32_t i = 0; i < 3; i ++) {
        vec3 pos = { pts[i][0], pts[i][1], z };
        idx[i] = box_mesh_vertex(mesh, pos, normal, pts[i][0] + 0.5f, 0.5f - pts[i][1]);
    }

    box_mesh_triangle(mesh, idx[0], idx[1], idx[2]);
}

void box_mesh_gen_triangle(
    box_mesh_t *mesh,
    bool right)
{
    vec2 pts[3];
    box_mesh_triangle_corners(right, pts);
    box_mesh_triangle_cap(mesh, pts, 0.0f, 1.0f);
}

void box_mesh_gen_triangle_prism(
    box_mesh_t *mesh,
    bool right)
{
    vec2 pts[3];
    box_mesh_triangle_corners(right, pts);
    box_mesh_triangle_cap(mesh, pts, 0.5f, 1.0f);
    box_mesh_triangle_cap(mesh, pts, -0.5f, -1.0f);

    vec2 center = {
        (pts[0][0] + pts[1][0] + pts[2][0]) / 3.0f,
        (pts[0][1] + pts[1][1] + pts[2][1]) / 3.0f
    };
    for (int32_t i = 0; i < 3; i ++) {
        const float *p0 = pts[i], *p1 = pts[(i + 1) % 3];
        vec3 normal = { p1[1] - p0[1], p0[0] - p1[0], 0.0f };
        float mx = 0.5f * (p0[0] + p1[0]) - center[0];
        float my = 0.5f * (p0[1] + p1[1]) - center[1];
        if (normal[0] * mx + normal[1] * my < 0.0f) {
            glm_vec3_negate(normal);
        }

        glm_vec3_normalize(normal);
        vec3 corners[4] = {
            { p0[0], p0[1], -0.5f }, { p1[0], p1[1], -0.5f },
            { p1[0], p1[1], 0.5f }, { p0[0], p0[1], 0.5f }
        };
        vec2 uvs[4] = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } };
        box_mesh_quad_face(mesh, corners, uvs, normal);
    }
}

void box_mesh_gen_ngon(
    box_mesh_t *mesh,
    int32_t sides)
{
    vec3 normal = { 0.0f, 0.0f, 1.0f };
    int32_t center = box_mesh_vertex(mesh, (vec3){0}, normal, 0.5f, 0.5f);
    for (int32_t i = 0; i < sides; i ++) {
        float angle = 0.5f * BOX_PI + 2.0f * BOX_PI * (float)i / (float)sides;
        vec3 pos = { cosf(angle), sinf(angle), 0.0f };
        box_mesh_vertex(mesh, pos, normal, 0.5f + 0.5f * pos[0], 0.5f - 0.5f * pos[1]);
    }

    for (int32_t i = 0; i < sides; i ++) {
        box_mesh_triangle(mesh, center, center + 1 + i, center + 1 + (i + 1) % sides);
    }
}
