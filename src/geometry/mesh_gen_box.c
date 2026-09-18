/**
 * @file geometry/mesh_gen_box.c
 * @brief Box shapes: box, rounded box, triangle prism and right triangle prism.
 */

#include "mesh_gen_internal.h"

/** Build one grid subdivided face of a box, projected onto a rounded corner when radius is set.
 *
 * @param b The builder
 * @param half Half extents of the box
 * @param radius Corner radius, 0 for a sharp box
 * @param axis The face normal axis, 0 for X, 1 for Y, 2 for Z
 * @param sign +1.0 for the positive face, -1.0 for the negative face
 * @param segments Grid subdivisions along each edge of the face
 */
static void box_mesh_box_face(
    box_mesh_builder_t *b,
    vec3 half,
    float radius,
    int32_t axis,
    float sign,
    int32_t segments)
{
    int32_t t0 = (axis + 1) % 3;
    int32_t t1 = (axis + 2) % 3;
    float h[3] = { half.x, half.y, half.z };
    float inner[3] = { half.x - radius, half.y - radius, half.z - radius };
    uint32_t base = b->next_vertex;
    int32_t n = segments;
    int32_t i, j;

    for (i = 0; i <= n; i ++) {
        float u = -1.0f + 2.0f * (float)i / (float)n;

        for (j = 0; j <= n; j ++) {
            float v = -1.0f + 2.0f * (float)j / (float)n;
            float p[3], c[3], d[3], dl;
            vec3 pos, nrm;
            int32_t k;

            p[axis] = sign * h[axis];
            p[t0] = u * h[t0];
            p[t1] = v * h[t1];
            for (k = 0; k < 3; k ++) {
                float lim = inner[k] > 0.0f ? inner[k] : 0.0f;

                c[k] = p[k] < -lim ? -lim : (p[k] > lim ? lim : p[k]);
                d[k] = p[k] - c[k];
            }

            dl = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (radius > 1e-6f && dl > 1e-6f) {
                pos = vec3_new(
                    c[0] + d[0] / dl * radius,
                    c[1] + d[1] / dl * radius,
                    c[2] + d[2] / dl * radius);
                nrm = vec3_new(d[0] / dl, d[1] / dl, d[2] / dl);
            } else {
                float nn[3] = { 0.0f, 0.0f, 0.0f };

                nn[axis] = sign;
                pos = vec3_new(p[0], p[1], p[2]);
                nrm = vec3_new(nn[0], nn[1], nn[2]);
            }

            box_mesh_builder_vertex(b, pos, nrm, u * 0.5f + 0.5f, v * 0.5f + 0.5f);
        }
    }

    for (i = 0; i < n; i ++) {
        for (j = 0; j < n; j ++) {
            uint32_t a = base + (uint32_t)(i * (n + 1) + j);
            uint32_t bb = base + (uint32_t)((i + 1) * (n + 1) + j);
            uint32_t cc = base + (uint32_t)(i * (n + 1) + j + 1);
            uint32_t dd = base + (uint32_t)((i + 1) * (n + 1) + j + 1);

            if (sign > 0.0f) {
                box_mesh_builder_triangle(b, a, bb, cc);
                box_mesh_builder_triangle(b, bb, dd, cc);
            } else {
                box_mesh_builder_triangle(b, a, cc, bb);
                box_mesh_builder_triangle(b, bb, cc, dd);
            }
        }
    }
}

void box_mesh_box(
    box_mesh_data_t *out)
{
    vec3 half = vec3_new(0.5f, 0.5f, 0.5f);
    box_mesh_builder_t b;
    int32_t axis;

    box_mesh_builder_init(&b, out, 24, 36);
    for (axis = 0; axis < 3; axis ++) {
        box_mesh_box_face(&b, half, 0.0f, axis, 1.0f, 1);
        box_mesh_box_face(&b, half, 0.0f, axis, -1.0f, 1);
    }

    box_mesh_builder_finish(&b);
}

void box_mesh_rounded_box(
    box_mesh_data_t *out,
    float x,
    float y,
    float z,
    float radius,
    int32_t segments)
{
    vec3 half = vec3_new(fmaxf(x, 1e-4f) * 0.5f, fmaxf(y, 1e-4f) * 0.5f, fmaxf(z, 1e-4f) * 0.5f);
    float min_half = fminf(half.x, fminf(half.y, half.z));
    float r = radius < 0.0f ? 0.0f : radius;
    int32_t n = box_mesh_clamp_min_i32(segments, 1);
    box_mesh_builder_t b;
    int32_t axis;

    r = r > min_half ? min_half : r;
    box_mesh_builder_init(&b, out, (uint32_t)(6 * (n + 1) * (n + 1)), (uint32_t)(36 * n * n));
    for (axis = 0; axis < 3; axis ++) {
        box_mesh_box_face(&b, half, r, axis, 1.0f, n);
        box_mesh_box_face(&b, half, r, axis, -1.0f, n);
    }

    box_mesh_builder_finish(&b);
}

/** Extrude a convex, CCW wound polygon along Z from -0.5 to 0.5 with flat caps and sides.
 *
 * @param out The mesh to fill
 * @param profile The polygon vertices, wound counter clockwise, Z ignored
 * @param count Number of profile vertices, at least 3
 */
static void box_mesh_prism_build(
    box_mesh_data_t *out,
    const vec3 *profile,
    int32_t count)
{
    vec3 lo = profile[0];
    vec3 hi = profile[0];
    uint32_t vertex_count = (uint32_t)(count * 6);
    uint32_t index_count = (uint32_t)(count * 12 - 12);
    uint32_t *front = ecs_os_malloc_n(uint32_t, count);
    uint32_t *back = ecs_os_malloc_n(uint32_t, count);
    box_mesh_builder_t b;
    int32_t i;

    for (i = 1; i < count; i ++) {
        lo = vec3_min(lo, profile[i]);
        hi = vec3_max(hi, profile[i]);
    }

    box_mesh_builder_init(&b, out, vertex_count, index_count);
    for (i = 0; i < count; i ++) {
        float u = (hi.x > lo.x) ? (profile[i].x - lo.x) / (hi.x - lo.x) : 0.5f;
        float v = (hi.y > lo.y) ? (profile[i].y - lo.y) / (hi.y - lo.y) : 0.5f;
        vec3 pf = vec3_new(profile[i].x, profile[i].y, -0.5f);
        vec3 pb = vec3_new(profile[i].x, profile[i].y, 0.5f);

        front[i] = box_mesh_builder_vertex(&b, pf, vec3_new(0.0f, 0.0f, -1.0f), u, v);
        back[i] = box_mesh_builder_vertex(&b, pb, vec3_new(0.0f, 0.0f, 1.0f), u, v);
    }

    for (i = 1; i < count - 1; i ++) {
        box_mesh_builder_triangle(&b, front[0], front[i + 1], front[i]);
        box_mesh_builder_triangle(&b, back[0], back[i], back[i + 1]);
    }

    for (i = 0; i < count; i ++) {
        int32_t j = (i + 1) % count;
        vec3 p = profile[i];
        vec3 q = profile[j];
        vec3 edge_n = vec3_normalize(vec3_new(q.y - p.y, -(q.x - p.x), 0.0f));
        vec3 pf = vec3_new(p.x, p.y, -0.5f);
        vec3 qf = vec3_new(q.x, q.y, -0.5f);
        vec3 qb = vec3_new(q.x, q.y, 0.5f);
        vec3 pb = vec3_new(p.x, p.y, 0.5f);
        uint32_t a = box_mesh_builder_vertex(&b, pf, edge_n, 0.0f, 0.0f);
        uint32_t bb = box_mesh_builder_vertex(&b, qf, edge_n, 1.0f, 0.0f);
        uint32_t cc = box_mesh_builder_vertex(&b, qb, edge_n, 1.0f, 1.0f);
        uint32_t dd = box_mesh_builder_vertex(&b, pb, edge_n, 0.0f, 1.0f);

        box_mesh_builder_triangle(&b, a, bb, cc);
        box_mesh_builder_triangle(&b, a, cc, dd);
    }

    box_mesh_builder_finish(&b);
    ecs_os_free(front);
    ecs_os_free(back);
}

void box_mesh_triangle_prism(
    box_mesh_data_t *out)
{
    vec3 profile[3] = {
        vec3_new(-0.5f, -0.5f, 0.0f),
        vec3_new(0.5f, -0.5f, 0.0f),
        vec3_new(0.0f, 0.5f, 0.0f)
    };

    box_mesh_prism_build(out, profile, 3);
}

void box_mesh_right_triangle_prism(
    box_mesh_data_t *out)
{
    vec3 profile[3] = {
        vec3_new(0.0f, 0.0f, 0.0f),
        vec3_new(1.0f, 0.0f, 0.0f),
        vec3_new(0.0f, 1.0f, 0.0f)
    };

    box_mesh_prism_build(out, profile, 3);
}
