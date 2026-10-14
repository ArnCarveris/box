/**
 * @file geometry/mesh_gen_round.c
 * @brief Generators of the shapes revolved around the Y axis.
 */

#include "mesh_cache.h"
#include "mesh_gen.h"
#include <math.h>

/** Append a flat disc around the Y axis.
 *
 * @param mesh The mesh
 * @param inner Inner radius, 0 for a full disc
 * @param outer Outer radius
 * @param y Height of the disc
 * @param ny Vertical normal, 1 for up and -1 for down
 * @param slices Steps around the axis
 * @param arc Sweep angle in radians
 */
static void box_mesh_disc(
    box_mesh_t *mesh,
    float inner,
    float outer,
    float y,
    float ny,
    int32_t slices,
    float arc)
{
    box_lathe_point_t profile[2] = {
        { inner, y, 0.0f, ny, 0.0f },
        { outer, y, 0.0f, ny, 1.0f }
    };
    box_mesh_lathe(mesh, profile, 2, slices, arc, true);
}

void box_mesh_gen_sphere(
    box_mesh_t *mesh,
    int32_t segments,
    bool smooth)
{
    int32_t stacks = segments / 2 < 2 ? 2 : segments / 2;
    box_lathe_point_t *profile = ecs_os_malloc_n(box_lathe_point_t, stacks + 1);
    for (int32_t i = 0; i <= stacks; i ++) {
        float phi = BOX_PI * (float)i / (float)stacks;
        float r = (i == 0 || i == stacks) ? 0.0f : sinf(phi);
        float y = cosf(phi);
        profile[i] = (box_lathe_point_t){ r, y, r, y, (float)i / (float)stacks };
    }

    box_mesh_lathe(mesh, profile, stacks + 1, segments, 2.0f * BOX_PI, false);
    ecs_os_free(profile);
    if (!smooth) {
        box_mesh_facet(mesh);
    }
}

void box_mesh_gen_hemisphere(
    box_mesh_t *mesh,
    int32_t segments,
    bool smooth)
{
    int32_t stacks = segments / 4 < 1 ? 1 : segments / 4;
    box_lathe_point_t *profile = ecs_os_malloc_n(box_lathe_point_t, stacks + 1);
    for (int32_t i = 0; i <= stacks; i ++) {
        float phi = 0.5f * BOX_PI * (float)i / (float)stacks;
        float r = i == 0 ? 0.0f : sinf(phi);
        float y = i == stacks ? 0.0f : cosf(phi);
        profile[i] = (box_lathe_point_t){ r, y, r, y, (float)i / (float)stacks };
    }

    box_mesh_lathe(mesh, profile, stacks + 1, segments, 2.0f * BOX_PI, false);
    box_mesh_disc(mesh, 0.0f, 1.0f, 0.0f, -1.0f, segments, 2.0f * BOX_PI);
    ecs_os_free(profile);
    if (!smooth) {
        box_mesh_facet(mesh);
    }
}

void box_mesh_gen_frustum(
    box_mesh_t *mesh,
    int32_t segments,
    bool smooth,
    float bottom,
    float top)
{
    float tau = 2.0f * BOX_PI;
    if (bottom > 0.0f || top > 0.0f) {
        float dr = bottom - top;
        box_lathe_point_t side[2] = {
            { bottom, -0.5f, 1.0f, dr, 1.0f },
            { top, 0.5f, 1.0f, dr, 0.0f }
        };
        box_mesh_lathe(mesh, side, 2, segments, tau, false);
    }

    if (bottom > 0.0f) {
        box_mesh_disc(mesh, 0.0f, bottom, -0.5f, -1.0f, segments, tau);
    }

    if (top > 0.0f) {
        box_mesh_disc(mesh, 0.0f, top, 0.5f, 1.0f, segments, tau);
    }

    if (!smooth) {
        box_mesh_facet(mesh);
    }
}

void box_mesh_gen_torus(
    box_mesh_t *mesh,
    int32_t segments,
    int32_t rings,
    bool smooth,
    float tube)
{
    box_lathe_point_t *profile = ecs_os_malloc_n(box_lathe_point_t, rings + 1);
    for (int32_t j = 0; j <= rings; j ++) {
        float theta = 2.0f * BOX_PI * (float)j / (float)rings;
        float ct = cosf(theta), st = sinf(theta);
        profile[j] = (box_lathe_point_t){
            1.0f + tube * ct, tube * st, ct, st, (float)j / (float)rings };
    }

    box_mesh_lathe(mesh, profile, rings + 1, segments, 2.0f * BOX_PI, false);
    ecs_os_free(profile);
    if (!smooth) {
        box_mesh_facet(mesh);
    }
}

void box_mesh_gen_capsule(
    box_mesh_t *mesh,
    int32_t segments,
    bool smooth,
    float length)
{
    int32_t half = segments / 4 < 1 ? 1 : segments / 4;
    int32_t count = 0;
    float total = length + 2.0f;
    float h = 0.5f * length;
    box_lathe_point_t *profile = ecs_os_malloc_n(box_lathe_point_t, 2 * half + 2);
    for (int32_t i = 0; i <= half; i ++) {
        float phi = 0.5f * BOX_PI * (float)i / (float)half;
        float r = i == 0 ? 0.0f : sinf(phi);
        float c = i == half ? 0.0f : cosf(phi);
        profile[count ++] = (box_lathe_point_t){ r, c + h, r, c, (1.0f - c) / total };
    }

    for (int32_t i = length > 0.0f ? 0 : 1; i <= half; i ++) {
        float phi = 0.5f * BOX_PI * (float)i / (float)half;
        float r = i == half ? 0.0f : cosf(phi);
        float c = i == 0 ? 0.0f : -sinf(phi);
        profile[count ++] = (box_lathe_point_t){ r, c - h, r, c, (1.0f + length - c) / total };
    }

    box_mesh_lathe(mesh, profile, count, segments, 2.0f * BOX_PI, false);
    ecs_os_free(profile);
    if (!smooth) {
        box_mesh_facet(mesh);
    }
}

/** Append the flat end of a partial tube at an angle.
 *
 * @param mesh The mesh
 * @param inner Inner radius
 * @param angle Angle of the end
 * @param sign -1 for the start of the sweep, 1 for its end
 */
static void box_mesh_tube_end(
    box_mesh_t *mesh,
    float inner,
    float angle,
    float sign)
{
    float ca = cosf(angle), sa = sinf(angle);
    vec3 normal = { -sa * sign, 0.0f, ca * sign };
    float radii[2] = { inner, 1.0f };
    float width = 1.0f - inner;
    int32_t idx[4];
    for (int32_t i = 0; i < 4; i ++) {
        float r = radii[i == 1 || i == 2];
        float y = i < 2 ? -0.5f : 0.5f;
        vec3 pos = { r * ca, y, r * sa };
        float u = width > 0.0f ? (r - inner) / width : 0.0f;
        idx[i] = box_mesh_vertex(mesh, pos, normal, u, 0.5f - y);
    }

    box_mesh_triangle(mesh, idx[0], idx[1], idx[2]);
    box_mesh_triangle(mesh, idx[0], idx[2], idx[3]);
}

void box_mesh_gen_tube(
    box_mesh_t *mesh,
    int32_t segments,
    bool smooth,
    float inner,
    float arc)
{
    bool full = arc <= 0.0f || arc >= 2.0f * BOX_PI - 1e-4f;
    if (full) {
        arc = 2.0f * BOX_PI;
    }

    box_lathe_point_t outer[2] = {
        { 1.0f, -0.5f, 1.0f, 0.0f, 1.0f },
        { 1.0f, 0.5f, 1.0f, 0.0f, 0.0f }
    };
    box_mesh_lathe(mesh, outer, 2, segments, arc, false);
    if (inner > 0.0f) {
        box_lathe_point_t wall[2] = {
            { inner, 0.5f, -1.0f, 0.0f, 0.0f },
            { inner, -0.5f, -1.0f, 0.0f, 1.0f }
        };
        box_mesh_lathe(mesh, wall, 2, segments, arc, false);
    }

    box_mesh_disc(mesh, inner, 1.0f, 0.5f, 1.0f, segments, arc);
    box_mesh_disc(mesh, inner, 1.0f, -0.5f, -1.0f, segments, arc);
    if (!full) {
        box_mesh_tube_end(mesh, inner, 0.0f, -1.0f);
        box_mesh_tube_end(mesh, inner, arc, 1.0f);
    }

    if (!smooth) {
        box_mesh_facet(mesh);
    }
}
