/**
 * @file geometry/mesh_data.c
 * @brief Mesh lifecycle and the shared builders used by the mesh generators.
 */

#include "mesh_gen_internal.h"
#include <math.h>
#include <string.h>

void box_mesh_data_fini(
    box_mesh_data_t *mesh)
{
    ecs_os_free(mesh->positions);
    ecs_os_free(mesh->normals);
    ecs_os_free(mesh->uvs);
    ecs_os_free(mesh->indices);
    mesh->positions = NULL;
    mesh->normals = NULL;
    mesh->uvs = NULL;
    mesh->indices = NULL;
    mesh->vertex_count = 0;
    mesh->index_count = 0;
}

void box_mesh_builder_init(
    box_mesh_builder_t *builder,
    box_mesh_data_t *mesh,
    uint32_t vertex_count,
    uint32_t index_count)
{
    mesh->positions = ecs_os_malloc_n(float, vertex_count * 3);
    mesh->normals = ecs_os_malloc_n(float, vertex_count * 3);
    mesh->uvs = ecs_os_malloc_n(float, vertex_count * 2);
    mesh->indices = ecs_os_malloc_n(uint32_t, index_count);
    mesh->vertex_count = vertex_count;
    mesh->index_count = index_count;
    builder->mesh = mesh;
    builder->next_vertex = 0;
    builder->next_index = 0;
}

uint32_t box_mesh_builder_vertex(
    box_mesh_builder_t *builder,
    vec3 position,
    vec3 normal,
    float u,
    float v)
{
    uint32_t index = builder->next_vertex;
    box_mesh_data_t *mesh = builder->mesh;

    mesh->positions[index * 3 + 0] = position.x;
    mesh->positions[index * 3 + 1] = position.y;
    mesh->positions[index * 3 + 2] = position.z;
    mesh->normals[index * 3 + 0] = normal.x;
    mesh->normals[index * 3 + 1] = normal.y;
    mesh->normals[index * 3 + 2] = normal.z;
    mesh->uvs[index * 2 + 0] = u;
    mesh->uvs[index * 2 + 1] = v;
    builder->next_vertex = index + 1;

    return index;
}

void box_mesh_builder_triangle(
    box_mesh_builder_t *builder,
    uint32_t a,
    uint32_t b,
    uint32_t c)
{
    uint32_t index = builder->next_index;
    box_mesh_data_t *mesh = builder->mesh;

    mesh->indices[index + 0] = a;
    mesh->indices[index + 1] = b;
    mesh->indices[index + 2] = c;
    builder->next_index = index + 3;
}

vec3 box_mesh_builder_position(
    const box_mesh_builder_t *builder,
    uint32_t index)
{
    const float *p = &builder->mesh->positions[index * 3];

    return vec3_new(p[0], p[1], p[2]);
}

void box_mesh_builder_finish(
    box_mesh_builder_t *builder)
{
    box_mesh_data_t *mesh = builder->mesh;
    uint32_t i;

    if (mesh->vertex_count == 0) {
        mesh->bounds_min = vec3_new(0.0f, 0.0f, 0.0f);
        mesh->bounds_max = vec3_new(0.0f, 0.0f, 0.0f);
    } else {
        vec3 lo = vec3_new(mesh->positions[0], mesh->positions[1], mesh->positions[2]);
        vec3 hi = lo;

        for (i = 1; i < mesh->vertex_count; i ++) {
            vec3 p = vec3_new(
                mesh->positions[i * 3 + 0],
                mesh->positions[i * 3 + 1],
                mesh->positions[i * 3 + 2]);

            lo = vec3_min(lo, p);
            hi = vec3_max(hi, p);
        }

        mesh->bounds_min = lo;
        mesh->bounds_max = hi;
    }
}

/** Read one vec3 out of a flat position or normal array.
 *
 * @param a The array
 * @param index Element index
 * @return The vector at that index
 */
static vec3 box_mesh_read_vec3(
    const float *a,
    uint32_t index)
{
    return vec3_new(a[index * 3 + 0], a[index * 3 + 1], a[index * 3 + 2]);
}

void box_mesh_facet(
    const box_mesh_data_t *in,
    box_mesh_data_t *out)
{
    uint32_t triangle_count = in->index_count / 3;
    uint32_t vertex_count = triangle_count * 3;
    box_mesh_builder_t b;
    uint32_t t;

    box_mesh_builder_init(&b, out, vertex_count, in->index_count);
    for (t = 0; t < triangle_count; t ++) {
        uint32_t ia = in->indices[t * 3 + 0];
        uint32_t ib = in->indices[t * 3 + 1];
        uint32_t ic = in->indices[t * 3 + 2];
        vec3 pa = box_mesh_read_vec3(in->positions, ia);
        vec3 pb = box_mesh_read_vec3(in->positions, ib);
        vec3 pc = box_mesh_read_vec3(in->positions, ic);
        vec3 n = vec3_normalize(vec3_cross(vec3_sub(pb, pa), vec3_sub(pc, pa)));
        uint32_t a = box_mesh_builder_vertex(&b, pa, n, in->uvs[ia * 2], in->uvs[ia * 2 + 1]);
        uint32_t bb = box_mesh_builder_vertex(&b, pb, n, in->uvs[ib * 2], in->uvs[ib * 2 + 1]);
        uint32_t cc = box_mesh_builder_vertex(&b, pc, n, in->uvs[ic * 2], in->uvs[ic * 2 + 1]);

        box_mesh_builder_triangle(&b, a, bb, cc);
    }

    box_mesh_builder_finish(&b);
}

int32_t box_mesh_clamp_min_i32(
    int32_t value,
    int32_t min)
{
    int32_t result = value;

    if (result < min) {
        result = min;
    }

    return result;
}

void box_mesh_disc_cap(
    box_mesh_builder_t *builder,
    float radius,
    float y,
    float sign,
    int32_t segments)
{
    vec3 up = vec3_new(0.0f, sign, 0.0f);
    uint32_t center = box_mesh_builder_vertex(builder, vec3_new(0.0f, y, 0.0f), up, 0.5f, 0.5f);
    uint32_t *rim = ecs_os_malloc_n(uint32_t, segments);
    int32_t i;

    for (i = 0; i < segments; i ++) {
        float a = (float)i / (float)segments * BOX_MESH_TAU;
        float x = cosf(a);
        float z = sinf(a);
        vec3 p = vec3_new(x * radius, y, z * radius);

        rim[i] = box_mesh_builder_vertex(builder, p, up, x * 0.5f + 0.5f, z * 0.5f + 0.5f);
    }

    for (i = 0; i < segments; i ++) {
        int32_t j = (i + 1) % segments;

        if (sign > 0.0f) {
            box_mesh_builder_triangle(builder, center, rim[j], rim[i]);
        } else {
            box_mesh_builder_triangle(builder, center, rim[i], rim[j]);
        }
    }

    ecs_os_free(rim);
}

void box_mesh_annulus_cap(
    box_mesh_builder_t *builder,
    float inner_radius,
    float outer_radius,
    float y,
    float sign,
    int32_t segments,
    float arc)
{
    vec3 up = vec3_new(0.0f, sign, 0.0f);
    uint32_t *inner = ecs_os_malloc_n(uint32_t, segments + 1);
    uint32_t *outer = ecs_os_malloc_n(uint32_t, segments + 1);
    int32_t i;

    for (i = 0; i <= segments; i ++) {
        float a = (float)i / (float)segments * arc;
        float x = cosf(a);
        float z = sinf(a);
        vec3 pi = vec3_new(x * inner_radius, y, z * inner_radius);
        vec3 po = vec3_new(x * outer_radius, y, z * outer_radius);
        float u = (float)i / (float)segments;

        inner[i] = box_mesh_builder_vertex(builder, pi, up, u, 0.0f);
        outer[i] = box_mesh_builder_vertex(builder, po, up, u, 1.0f);
    }

    for (i = 0; i < segments; i ++) {
        if (sign > 0.0f) {
            box_mesh_builder_triangle(builder, inner[i], outer[i + 1], outer[i]);
            box_mesh_builder_triangle(builder, inner[i], inner[i + 1], outer[i + 1]);
        } else {
            box_mesh_builder_triangle(builder, inner[i], outer[i], outer[i + 1]);
            box_mesh_builder_triangle(builder, inner[i], outer[i + 1], inner[i + 1]);
        }
    }

    ecs_os_free(inner);
    ecs_os_free(outer);
}

void box_mesh_cone_wall(
    box_mesh_builder_t *builder,
    float radius_bottom,
    float radius_top,
    float y_bottom,
    float y_top,
    int32_t segments,
    bool outward,
    float arc)
{
    float dr = radius_top - radius_bottom;
    float dy = y_top - y_bottom;
    float len = sqrtf(dr * dr + dy * dy);
    float nr = len > 0.0f ? dy / len : 1.0f;
    float ny = len > 0.0f ? -dr / len : 0.0f;
    float dir = outward ? 1.0f : -1.0f;
    uint32_t *bottom = ecs_os_malloc_n(uint32_t, segments + 1);
    uint32_t *top = ecs_os_malloc_n(uint32_t, segments + 1);
    int32_t i;

    for (i = 0; i <= segments; i ++) {
        float a = (float)i / (float)segments * arc;
        float cx = cosf(a);
        float cz = sinf(a);
        vec3 pb = vec3_new(cx * radius_bottom, y_bottom, cz * radius_bottom);
        vec3 pt = vec3_new(cx * radius_top, y_top, cz * radius_top);
        vec3 nb = vec3_new(nr * cx * dir, ny * dir, nr * cz * dir);
        vec3 nt = vec3_new(nr * cx * dir, ny * dir, nr * cz * dir);
        float u = (float)i / (float)segments;

        bottom[i] = box_mesh_builder_vertex(builder, pb, nb, u, 0.0f);
        top[i] = box_mesh_builder_vertex(builder, pt, nt, u, 1.0f);
    }

    for (i = 0; i < segments; i ++) {
        bool skip_bottom = radius_bottom == 0.0f;
        bool skip_top = radius_top == 0.0f;

        if (outward) {
            if (!skip_bottom) {
                box_mesh_builder_triangle(builder, bottom[i], top[i + 1], bottom[i + 1]);
            }

            if (!skip_top) {
                box_mesh_builder_triangle(builder, bottom[i], top[i], top[i + 1]);
            }
        } else {
            if (!skip_bottom) {
                box_mesh_builder_triangle(builder, bottom[i], bottom[i + 1], top[i + 1]);
            }

            if (!skip_top) {
                box_mesh_builder_triangle(builder, bottom[i], top[i + 1], top[i]);
            }
        }
    }

    ecs_os_free(bottom);
    ecs_os_free(top);
}

/** Fill one latitude ring of a hemisphere shell.
 *
 * @param builder The builder
 * @param center Center of the sphere the hemisphere is cut from
 * @param radius The radius
 * @param lon Steps around the axis
 * @param y Height of the ring relative to the center
 * @param r Radius of the ring
 * @param v Texture V coordinate of the ring
 * @param ids Vertex indices of the ring, sized lon + 1
 */
static void box_mesh_hemisphere_ring(
    box_mesh_builder_t *builder,
    vec3 center,
    float radius,
    int32_t lon,
    float y,
    float r,
    float v,
    uint32_t *ids)
{
    int32_t j;

    for (j = 0; j <= lon; j ++) {
        float theta = (float)j / (float)lon * BOX_MESH_TAU;
        float cx = cosf(theta);
        float cz = sinf(theta);
        vec3 p = vec3_new(cx * r, y, cz * r);
        vec3 n = vec3_scale(p, 1.0f / radius);
        float u = (float)j / (float)lon;

        ids[j] = box_mesh_builder_vertex(builder, vec3_add(center, p), n, u, v);
    }
}

void box_mesh_hemisphere_shell(
    box_mesh_builder_t *builder,
    vec3 center,
    float radius,
    int32_t segments,
    bool upper,
    float v_min,
    float v_max)
{
    int32_t lat = segments;
    int32_t lon = segments;
    uint32_t *prev = ecs_os_malloc_n(uint32_t, lon + 1);
    uint32_t *curr = ecs_os_malloc_n(uint32_t, lon + 1);
    float sign = upper ? 1.0f : -1.0f;
    int32_t i, j;

    box_mesh_hemisphere_ring(builder, center, radius, lon, sign * radius, 0.0f, v_max, prev);
    for (i = 1; i <= lat; i ++) {
        float phi = (float)i / (float)lat * 0.5f * BOX_MESH_PI;
        float y = sign * cosf(phi) * radius;
        float r = sinf(phi) * radius;
        float v = v_max + ((float)i / (float)lat) * (v_min - v_max);

        box_mesh_hemisphere_ring(builder, center, radius, lon, y, r, v, curr);
        for (j = 0; j < lon; j ++) {
            if (upper) {
                box_mesh_builder_triangle(builder, prev[j], curr[j + 1], curr[j]);
            } else {
                box_mesh_builder_triangle(builder, prev[j], curr[j], curr[j + 1]);
            }

            if (i > 1) {
                if (upper) {
                    box_mesh_builder_triangle(builder, prev[j], prev[j + 1], curr[j + 1]);
                } else {
                    box_mesh_builder_triangle(builder, prev[j], curr[j + 1], prev[j + 1]);
                }
            }
        }

        memcpy(prev, curr, (size_t)(lon + 1) * sizeof(uint32_t));
    }

    ecs_os_free(prev);
    ecs_os_free(curr);
}
