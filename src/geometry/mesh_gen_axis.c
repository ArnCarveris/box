/**
 * @file geometry/mesh_gen_axis.c
 * @brief Shapes of revolution around Y: cone, cylinder, capsule, tube, frustum and torus.
 */

#include "mesh_gen_internal.h"

/** Build one flat quad that closes an open end of a tube.
 *
 * @param b The builder
 * @param angle Sweep angle of this end, in radians
 * @param inner_r Inner radius
 * @param outer_r Outer radius
 * @param start True for the end at angle 0, false for the end at the sweep angle
 */
static void box_mesh_tube_end_cap(
    box_mesh_builder_t *b,
    float angle,
    float inner_r,
    float outer_r,
    bool start)
{
    float ct = cosf(angle);
    float st = sinf(angle);
    vec3 tangent = vec3_new(-st, 0.0f, ct);
    vec3 n = start ? vec3_scale(tangent, -1.0f) : tangent;
    vec3 p00 = vec3_new(inner_r * ct, -0.5f, inner_r * st);
    vec3 p10 = vec3_new(outer_r * ct, -0.5f, outer_r * st);
    vec3 p01 = vec3_new(inner_r * ct, 0.5f, inner_r * st);
    vec3 p11 = vec3_new(outer_r * ct, 0.5f, outer_r * st);
    uint32_t i00 = box_mesh_builder_vertex(b, p00, n, 0.0f, 0.0f);
    uint32_t i10 = box_mesh_builder_vertex(b, p10, n, 1.0f, 0.0f);
    uint32_t i01 = box_mesh_builder_vertex(b, p01, n, 0.0f, 1.0f);
    uint32_t i11 = box_mesh_builder_vertex(b, p11, n, 1.0f, 1.0f);

    if (start) {
        box_mesh_builder_triangle(b, i00, i11, i10);
        box_mesh_builder_triangle(b, i00, i01, i11);
    } else {
        box_mesh_builder_triangle(b, i00, i10, i11);
        box_mesh_builder_triangle(b, i00, i11, i01);
    }
}

void box_mesh_cone(
    box_mesh_data_t *out,
    int32_t segments,
    bool smooth)
{
    int32_t n = box_mesh_clamp_min_i32(segments, 3);
    uint32_t vertex_count = (uint32_t)(3 * n + 3);
    uint32_t index_count = (uint32_t)(6 * n);
    box_mesh_data_t shared;
    box_mesh_builder_t b;

    box_mesh_builder_init(&b, &shared, vertex_count, index_count);
    box_mesh_cone_wall(&b, 1.0f, 0.0f, -0.5f, 0.5f, n, true, BOX_MESH_TAU);
    box_mesh_disc_cap(&b, 1.0f, -0.5f, -1.0f, n);
    box_mesh_builder_finish(&b);

    if (smooth) {
        *out = shared;
    } else {
        box_mesh_facet(&shared, out);
        box_mesh_data_fini(&shared);
    }
}

void box_mesh_cylinder(
    box_mesh_data_t *out,
    int32_t segments,
    bool smooth)
{
    int32_t n = box_mesh_clamp_min_i32(segments, 3);
    uint32_t vertex_count = (uint32_t)(4 * n + 4);
    uint32_t index_count = (uint32_t)(12 * n);
    box_mesh_data_t shared;
    box_mesh_builder_t b;

    box_mesh_builder_init(&b, &shared, vertex_count, index_count);
    box_mesh_cone_wall(&b, 1.0f, 1.0f, -0.5f, 0.5f, n, true, BOX_MESH_TAU);
    box_mesh_disc_cap(&b, 1.0f, 0.5f, 1.0f, n);
    box_mesh_disc_cap(&b, 1.0f, -0.5f, -1.0f, n);
    box_mesh_builder_finish(&b);

    if (smooth) {
        *out = shared;
    } else {
        box_mesh_facet(&shared, out);
        box_mesh_data_fini(&shared);
    }
}

void box_mesh_frustum(
    box_mesh_data_t *out,
    int32_t segments,
    bool smooth,
    float radius_top)
{
    int32_t n = box_mesh_clamp_min_i32(segments, 3);
    float rt = radius_top < 1e-4f ? 1e-4f : radius_top;
    uint32_t vertex_count = (uint32_t)(4 * n + 4);
    uint32_t index_count = (uint32_t)(12 * n);
    box_mesh_data_t shared;
    box_mesh_builder_t b;

    box_mesh_builder_init(&b, &shared, vertex_count, index_count);
    box_mesh_cone_wall(&b, 1.0f, rt, -0.5f, 0.5f, n, true, BOX_MESH_TAU);
    box_mesh_disc_cap(&b, rt, 0.5f, 1.0f, n);
    box_mesh_disc_cap(&b, 1.0f, -0.5f, -1.0f, n);
    box_mesh_builder_finish(&b);

    if (smooth) {
        *out = shared;
    } else {
        box_mesh_facet(&shared, out);
        box_mesh_data_fini(&shared);
    }
}

void box_mesh_capsule(
    box_mesh_data_t *out,
    int32_t segments,
    bool smooth,
    float length)
{
    int32_t n = box_mesh_clamp_min_i32(segments, 3);
    float len = length < 0.0f ? 0.0f : length;
    float half_len = len * 0.5f;
    uint32_t vertex_count = (uint32_t)(2 * (n + 1) + 2 * (n + 1) * (n + 1));
    uint32_t index_count = (uint32_t)(12 * n * n);
    box_mesh_data_t shared;
    box_mesh_builder_t b;

    box_mesh_builder_init(&b, &shared, vertex_count, index_count);
    box_mesh_cone_wall(&b, 1.0f, 1.0f, -half_len, half_len, n, true, BOX_MESH_TAU);
    box_mesh_hemisphere_shell(&b, vec3_new(0.0f, half_len, 0.0f), 1.0f, n, true, 0.0f, 1.0f);
    box_mesh_hemisphere_shell(&b, vec3_new(0.0f, -half_len, 0.0f), 1.0f, n, false, 0.0f, 1.0f);
    box_mesh_builder_finish(&b);

    if (smooth) {
        *out = shared;
    } else {
        box_mesh_facet(&shared, out);
        box_mesh_data_fini(&shared);
    }
}

void box_mesh_tube(
    box_mesh_data_t *out,
    int32_t segments,
    bool smooth,
    float thickness,
    float arc)
{
    int32_t n = box_mesh_clamp_min_i32(segments, 3);
    float th = thickness < 1e-4f ? 1e-4f : (thickness > 0.999f ? 0.999f : thickness);
    float inner_r = 1.0f - th;
    bool closed = arc >= (BOX_MESH_TAU - 1e-4f);
    float a = closed ? BOX_MESH_TAU : arc;
    uint32_t vertex_count = (uint32_t)(8 * (n + 1) + (closed ? 0 : 8));
    uint32_t index_count = (uint32_t)(24 * n + (closed ? 0 : 12));
    box_mesh_data_t shared;
    box_mesh_builder_t b;

    box_mesh_builder_init(&b, &shared, vertex_count, index_count);
    box_mesh_cone_wall(&b, 1.0f, 1.0f, -0.5f, 0.5f, n, true, a);
    box_mesh_cone_wall(&b, inner_r, inner_r, -0.5f, 0.5f, n, false, a);
    box_mesh_annulus_cap(&b, inner_r, 1.0f, 0.5f, 1.0f, n, a);
    box_mesh_annulus_cap(&b, inner_r, 1.0f, -0.5f, -1.0f, n, a);
    if (!closed) {
        box_mesh_tube_end_cap(&b, 0.0f, inner_r, 1.0f, true);
        box_mesh_tube_end_cap(&b, a, inner_r, 1.0f, false);
    }

    box_mesh_builder_finish(&b);

    if (smooth) {
        *out = shared;
    } else {
        box_mesh_facet(&shared, out);
        box_mesh_data_fini(&shared);
    }
}

void box_mesh_torus(
    box_mesh_data_t *out,
    int32_t segments,
    int32_t rings,
    bool smooth,
    float tube)
{
    int32_t seg = box_mesh_clamp_min_i32(segments, 3);
    int32_t rng = box_mesh_clamp_min_i32(rings, 3);
    float tr = tube < 1e-4f ? 1e-4f : tube;
    uint32_t vertex_count = (uint32_t)((seg + 1) * (rng + 1));
    uint32_t index_count = (uint32_t)(6 * seg * rng);
    uint32_t *ids = ecs_os_malloc_n(uint32_t, vertex_count);
    box_mesh_data_t shared;
    box_mesh_builder_t b;
    int32_t i, j;

    box_mesh_builder_init(&b, &shared, vertex_count, index_count);
    for (i = 0; i <= seg; i ++) {
        float theta = (float)i / (float)seg * BOX_MESH_TAU;
        float ct = cosf(theta);
        float st = sinf(theta);

        for (j = 0; j <= rng; j ++) {
            float phi = (float)j / (float)rng * BOX_MESH_TAU;
            float cp = cosf(phi);
            float sp = sinf(phi);
            vec3 n = vec3_new(cp * ct, sp, cp * st);
            vec3 p = vec3_new((1.0f + tr * cp) * ct, tr * sp, (1.0f + tr * cp) * st);
            float u = (float)i / (float)seg;
            float v = (float)j / (float)rng;

            ids[i * (rng + 1) + j] = box_mesh_builder_vertex(&b, p, n, u, v);
        }
    }

    for (i = 0; i < seg; i ++) {
        for (j = 0; j < rng; j ++) {
            uint32_t a = ids[i * (rng + 1) + j];
            uint32_t bb = ids[(i + 1) * (rng + 1) + j];
            uint32_t cc = ids[i * (rng + 1) + j + 1];
            uint32_t dd = ids[(i + 1) * (rng + 1) + j + 1];

            box_mesh_builder_triangle(&b, a, cc, bb);
            box_mesh_builder_triangle(&b, bb, cc, dd);
        }
    }

    box_mesh_builder_finish(&b);
    ecs_os_free(ids);

    if (smooth) {
        *out = shared;
    } else {
        box_mesh_facet(&shared, out);
        box_mesh_data_fini(&shared);
    }
}
