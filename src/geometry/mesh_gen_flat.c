/**
 * @file geometry/mesh_gen_flat.c
 * @brief Flat shapes in the XY plane: quad, triangle, line, image and ngon.
 */

#include "mesh_gen_internal.h"

/** Build a convex, CCW wound polygon in the XY plane as a fan from vertex 0.
 *
 * @param out The mesh to fill
 * @param points The polygon vertices, wound counter clockwise, Z ignored
 * @param count Number of vertices, at least 3
 */
static void box_mesh_flat_build(
    box_mesh_data_t *out,
    const vec3 *points,
    int32_t count)
{
    vec3 lo = points[0];
    vec3 hi = points[0];
    uint32_t vertex_count = (uint32_t)count;
    uint32_t index_count = (uint32_t)((count - 2) * 3);
    box_mesh_builder_t b;
    int32_t i;

    for (i = 1; i < count; i ++) {
        lo = vec3_min(lo, points[i]);
        hi = vec3_max(hi, points[i]);
    }

    box_mesh_builder_init(&b, out, vertex_count, index_count);
    for (i = 0; i < count; i ++) {
        float u = (hi.x > lo.x) ? (points[i].x - lo.x) / (hi.x - lo.x) : 0.5f;
        float v = (hi.y > lo.y) ? (points[i].y - lo.y) / (hi.y - lo.y) : 0.5f;

        box_mesh_builder_vertex(&b, points[i], vec3_new(0.0f, 0.0f, -1.0f), u, v);
    }

    for (i = 1; i < count - 1; i ++) {
        box_mesh_builder_triangle(&b, 0, (uint32_t)(i + 1), (uint32_t)i);
    }

    box_mesh_builder_finish(&b);
}

void box_mesh_quad(
    box_mesh_data_t *out)
{
    vec3 points[4] = {
        vec3_new(-0.5f, -0.5f, 0.0f),
        vec3_new(0.5f, -0.5f, 0.0f),
        vec3_new(0.5f, 0.5f, 0.0f),
        vec3_new(-0.5f, 0.5f, 0.0f)
    };

    box_mesh_flat_build(out, points, 4);
}

void box_mesh_triangle(
    box_mesh_data_t *out)
{
    vec3 points[3] = {
        vec3_new(-0.5f, -0.5f, 0.0f),
        vec3_new(0.5f, -0.5f, 0.0f),
        vec3_new(0.0f, 0.5f, 0.0f)
    };

    box_mesh_flat_build(out, points, 3);
}

void box_mesh_right_triangle(
    box_mesh_data_t *out)
{
    vec3 points[3] = {
        vec3_new(0.0f, 0.0f, 0.0f),
        vec3_new(1.0f, 0.0f, 0.0f),
        vec3_new(0.0f, 1.0f, 0.0f)
    };

    box_mesh_flat_build(out, points, 3);
}

void box_mesh_ngon(
    box_mesh_data_t *out,
    int32_t sides)
{
    int32_t count = box_mesh_clamp_min_i32(sides, 3);
    vec3 *points = ecs_os_malloc_n(vec3, count);
    int32_t i;

    for (i = 0; i < count; i ++) {
        float a = (float)i / (float)count * BOX_MESH_TAU;

        points[i] = vec3_new(cosf(a), sinf(a), 0.0f);
    }

    box_mesh_flat_build(out, points, count);
    ecs_os_free(points);
}

/** Build a four sided strip in the XY plane from corners and explicit uvs.
 *
 * @param out The mesh to fill
 * @param points Four corners wound like the unit quad
 * @param uvs Eight floats, a U and a V per corner
 */
static void box_mesh_flat_uv_build(
    box_mesh_data_t *out,
    const vec3 *points,
    const float *uvs)
{
    box_mesh_builder_t b;
    int32_t i;

    box_mesh_builder_init(&b, out, 4, 6);
    for (i = 0; i < 4; i ++) {
        box_mesh_builder_vertex(&b, points[i], vec3_new(0.0f, 0.0f, -1.0f),
            uvs[i * 2], uvs[i * 2 + 1]);
    }

    box_mesh_builder_triangle(&b, 0, 2, 1);
    box_mesh_builder_triangle(&b, 0, 3, 2);
    box_mesh_builder_finish(&b);
}

void box_mesh_line(
    box_mesh_data_t *out,
    float x1,
    float y1,
    float x2,
    float y2,
    float thickness)
{
    float dx = x2 - x1;
    float dy = y2 - y1;
    float length = sqrtf(dx * dx + dy * dy);
    float half = (thickness > 0.0f ? thickness : length * 0.01f) * 0.5f;

    if (length > 1e-6f) {
        dx /= length;
        dy /= length;
    } else {
        dx = 1.0f;
        dy = 0.0f;
    }

    float nx = -dy * half;
    float ny = dx * half;
    vec3 points[4] = {
        vec3_new(x1 - nx, y1 - ny, 0.0f),
        vec3_new(x2 - nx, y2 - ny, 0.0f),
        vec3_new(x2 + nx, y2 + ny, 0.0f),
        vec3_new(x1 + nx, y1 + ny, 0.0f)
    };
    float uvs[8] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };

    box_mesh_flat_uv_build(out, points, uvs);
}

void box_mesh_image(
    box_mesh_data_t *out,
    float u0,
    float v0,
    float u1,
    float v1,
    float rotation)
{
    float angle = rotation * BOX_MESH_PI / 180.0f;
    float c = cosf(angle);
    float s = sinf(angle);
    float cx = (u0 + u1) * 0.5f;
    float cy = (v0 + v1) * 0.5f;
    float sx = u1 - u0;
    float sy = v1 - v0;
    vec3 points[4] = {
        vec3_new(-0.5f, -0.5f, 0.0f),
        vec3_new(0.5f, -0.5f, 0.0f),
        vec3_new(0.5f, 0.5f, 0.0f),
        vec3_new(-0.5f, 0.5f, 0.0f)
    };
    float uvs[8];
    int32_t i;

    for (i = 0; i < 4; i ++) {
        float px = points[i].x;
        float py = points[i].y;

        uvs[i * 2] = cx + (px * c - py * s) * sx;
        uvs[i * 2 + 1] = cy + (px * s + py * c) * sy;
    }

    box_mesh_flat_uv_build(out, points, uvs);
}
