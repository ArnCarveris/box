/**
 * @file geometry/mesh_gen_solid.c
 * @brief Generators of the icosphere and the rounded box, and the conversion of authored meshes.
 */

#include "mesh_cache.h"
#include "mesh_gen.h"
#include <math.h>

/** Corners of the icosahedron before normalization */
static const float box_ico_vertices[12][3] = {
    { -1.0f, 1.618034f, 0.0f }, { 1.0f, 1.618034f, 0.0f },
    { -1.0f, -1.618034f, 0.0f }, { 1.0f, -1.618034f, 0.0f },
    { 0.0f, -1.0f, 1.618034f }, { 0.0f, 1.0f, 1.618034f },
    { 0.0f, -1.0f, -1.618034f }, { 0.0f, 1.0f, -1.618034f },
    { 1.618034f, 0.0f, -1.0f }, { 1.618034f, 0.0f, 1.0f },
    { -1.618034f, 0.0f, -1.0f }, { -1.618034f, 0.0f, 1.0f }
};

/** Faces of the icosahedron */
static const uint32_t box_ico_faces[20][3] = {
    { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 },
    { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
    { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 },
    { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 }
};

/** Get or create the normalized midpoint of an edge.
 *
 * @param edges Map from edge to midpoint index
 * @param positions Vertex positions
 * @param count Number of vertices, incremented when a midpoint is added
 * @param a First vertex of the edge
 * @param b Second vertex of the edge
 * @return Index of the midpoint
 */
static uint32_t box_ico_midpoint(
    ecs_map_t *edges,
    vec3 *positions,
    uint32_t *count,
    uint32_t a,
    uint32_t b)
{
    uint64_t key = a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
    ecs_map_val_t *existing = ecs_map_get(edges, key);
    uint32_t result;
    if (existing) {
        result = (uint32_t)*existing;
    } else {
        result = (*count) ++;
        glm_vec3_add(positions[a], positions[b], positions[result]);
        glm_vec3_normalize(positions[result]);
        ecs_map_insert(edges, key, result);
    }

    return result;
}

void box_mesh_gen_icosphere(
    box_mesh_t *mesh,
    int32_t level,
    bool smooth)
{
    int32_t faces = 20 << (2 * level);
    int32_t verts = 10 * (1 << (2 * level)) + 2;
    vec3 *positions = ecs_os_malloc_n(vec3, verts);
    uint32_t *tris = ecs_os_malloc_n(uint32_t, faces * 3);
    uint32_t *next = ecs_os_malloc_n(uint32_t, faces * 3);
    uint32_t count = 12, tri_count = 20;
    for (int32_t i = 0; i < 12; i ++) {
        glm_vec3_normalize_to((float*)box_ico_vertices[i], positions[i]);
    }

    ecs_os_memcpy(tris, box_ico_faces, ECS_SIZEOF(box_ico_faces));
    ecs_map_t edges;
    ecs_map_init(&edges, NULL);
    for (int32_t l = 0; l < level; l ++) {
        for (uint32_t t = 0; t < tri_count; t ++) {
            uint32_t *f = &tris[t * 3];
            uint32_t ab = box_ico_midpoint(&edges, positions, &count, f[0], f[1]);
            uint32_t bc = box_ico_midpoint(&edges, positions, &count, f[1], f[2]);
            uint32_t ca = box_ico_midpoint(&edges, positions, &count, f[2], f[0]);
            uint32_t sub[12] = { f[0], ab, ca, f[1], bc, ab, f[2], ca, bc, ab, bc, ca };
            ecs_os_memcpy(&next[t * 12], sub, ECS_SIZEOF(sub));
        }

        tri_count *= 4;
        uint32_t *tmp = tris;
        tris = next;
        next = tmp;
    }

    for (uint32_t i = 0; i < count; i ++) {
        const float *p = positions[i];
        float y = glm_clamp(p[1], -1.0f, 1.0f);
        float u = 0.5f + atan2f(p[2], p[0]) / (2.0f * BOX_PI);
        box_mesh_vertex(mesh, p, p, glm_clamp(u, 0.0f, 1.0f), acosf(y) / BOX_PI);
    }

    for (uint32_t t = 0; t < tri_count; t ++) {
        box_mesh_triangle(mesh, (int32_t)tris[t * 3], (int32_t)tris[t * 3 + 1],
            (int32_t)tris[t * 3 + 2]);
    }

    ecs_map_fini(&edges);
    ecs_os_free(positions);
    ecs_os_free(tris);
    ecs_os_free(next);
    if (!smooth) {
        box_mesh_facet(mesh);
    }
}

/** Get the angle and the side of a grid line of a rounded box face.
 *
 * @param index The grid line, 0 to 2 * half + 1
 * @param half Grid lines per half face minus one
 * @param side Receives -1 for the lower half of the face and 1 for the upper half
 * @return Tangent of the angle between the line and the face normal
 */
static float box_rounded_line(
    int32_t index,
    int32_t half,
    float *side)
{
    float angle;
    if (index <= half) {
        *side = -1.0f;
        angle = -0.25f * BOX_PI * (float)(half - index) / (float)half;
    } else {
        *side = 1.0f;
        angle = 0.25f * BOX_PI * (float)(index - half - 1) / (float)half;
    }

    return tanf(angle);
}

/** Append one face of a rounded box.
 *
 * @param mesh The mesh
 * @param h Half sizes
 * @param r Corner radius
 * @param k Axis of the face normal
 * @param sign Direction of the face normal, 1 or -1
 * @param half Grid lines per half face minus one
 */
static void box_rounded_face(
    box_mesh_t *mesh,
    const vec3 h,
    float r,
    int32_t k,
    float sign,
    int32_t half)
{
    int32_t u = (k + 1) % 3, v = (k + 2) % 3;
    int32_t lines = 2 * half + 2;
    int32_t base = mesh->vertex_count;
    for (int32_t a = 0; a < lines; a ++) {
        for (int32_t b = 0; b < lines; b ++) {
            float side_a, side_b;
            vec3 dir, pos;
            dir[k] = sign;
            dir[u] = box_rounded_line(a, half, &side_a);
            dir[v] = box_rounded_line(b, half, &side_b);
            glm_vec3_normalize(dir);
            pos[k] = sign * (h[k] - r) + r * dir[k];
            pos[u] = side_a * (h[u] - r) + r * dir[u];
            pos[v] = side_b * (h[v] - r) + r * dir[v];
            float tu = glm_clamp(0.5f + 0.5f * pos[u] / h[u], 0.0f, 1.0f);
            float tv = glm_clamp(0.5f - 0.5f * pos[v] / h[v], 0.0f, 1.0f);
            box_mesh_vertex(mesh, pos, dir, tu, tv);
        }
    }

    bool flat_u = h[u] - r <= 1e-6f, flat_v = h[v] - r <= 1e-6f;
    for (int32_t a = 0; a < lines - 1; a ++) {
        for (int32_t b = 0; b < lines - 1; b ++) {
            if ((flat_u && a == half) || (flat_v && b == half)) {
                continue;
            }

            int32_t i0 = base + a * lines + b;
            int32_t i1 = i0 + lines;
            box_mesh_triangle(mesh, i0, i1, i1 + 1);
            box_mesh_triangle(mesh, i0, i1 + 1, i0 + 1);
        }
    }
}

void box_mesh_gen_rounded_box(
    box_mesh_t *mesh,
    const vec3 size,
    float radius,
    int32_t segments)
{
    if (radius <= 0.0f) {
        box_mesh_gen_box(mesh);
        for (int32_t i = 0; i < mesh->vertex_count; i ++) {
            glm_vec3_mul(mesh->vertices[i].position, (float*)size, mesh->vertices[i].position);
        }
    } else {
        int32_t half = (segments + 1) / 2 < 1 ? 1 : (segments + 1) / 2;
        vec3 h;
        glm_vec3_scale((float*)size, 0.5f, h);
        for (int32_t k = 0; k < 3; k ++) {
            box_rounded_face(mesh, h, radius, k, 1.0f, half);
            box_rounded_face(mesh, h, radius, k, -1.0f, half);
        }
    }
}

void box_mesh_gen_mesh3(
    box_mesh_t *mesh,
    const Mesh3 *src)
{
    int32_t count = ecs_vec_count(&src->vertices);
    const vec3 *positions = ecs_vec_first(&src->vertices);
    const vec3 *normals = ecs_vec_count(&src->normals) == count ?
        ecs_vec_first(&src->normals) : NULL;
    const vec2 *uvs = ecs_vec_count(&src->uvs) == count ? ecs_vec_first(&src->uvs) : NULL;
    const rgba *colors = ecs_vec_count(&src->colors) == count ?
        ecs_vec_first(&src->colors) : NULL;
    for (int32_t i = 0; i < count; i ++) {
        vec3 n = {0};
        if (normals && glm_vec3_norm2((float*)normals[i]) > 0.0f) {
            glm_vec3_normalize_to((float*)normals[i], n);
        }

        box_mesh_vertex(mesh, positions[i], n, uvs ? uvs[i][0] : 0.0f, uvs ? uvs[i][1] : 0.0f);
    }

    if (colors && count) {
        mesh->colors = ecs_os_memdup_n(colors, rgba, count);
    }

    int32_t src_indices = ecs_vec_count(&src->indices);
    const uint32_t *indices = ecs_vec_first(&src->indices);
    int32_t total = src_indices ? src_indices - src_indices % 3 : count - count % 3;
    mesh->indices = ecs_os_malloc_n(uint32_t, total ? total : 1);
    mesh->index_capacity = total ? total : 1;
    for (int32_t t = 0; t < total; t += 3) {
        uint32_t tri[3] = { (uint32_t)t, (uint32_t)t + 1, (uint32_t)t + 2 };
        if (src_indices) {
            ecs_os_memcpy(tri, &indices[t], ECS_SIZEOF(tri));
        }

        if (tri[0] < (uint32_t)count && tri[1] < (uint32_t)count && tri[2] < (uint32_t)count) {
            ecs_os_memcpy(&mesh->indices[mesh->index_count], tri, ECS_SIZEOF(tri));
            mesh->index_count += 3;
        }
    }

    if (!normals) {
        box_mesh_facet(mesh);
    }
}
