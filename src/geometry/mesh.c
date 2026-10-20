/**
 * @file geometry/mesh.c
 * @brief Mesh building operations shared by the generators and the shape registry.
 */

#include "mesh_cache.h"
#include "mesh_gen.h"
#include <math.h>

int32_t box_mesh_vertex(
    box_mesh_t *mesh,
    const vec3 position,
    const vec3 normal,
    float u,
    float v)
{
    if (mesh->vertex_count == mesh->vertex_capacity) {
        mesh->vertex_capacity = mesh->vertex_capacity ? mesh->vertex_capacity * 2 : 64;
        mesh->vertices = ecs_os_realloc_n(
            mesh->vertices, box_vertex_t, mesh->vertex_capacity);
    }

    box_vertex_t *vert = &mesh->vertices[mesh->vertex_count];
    glm_vec3_copy((float*)position, vert->position);
    glm_vec3_copy((float*)normal, vert->normal);
    vert->uv[0] = u;
    vert->uv[1] = v;

    return mesh->vertex_count ++;
}

/** Append one index to a mesh.
 *
 * @param mesh The mesh
 * @param index The index
 */
static void box_mesh_index(
    box_mesh_t *mesh,
    int32_t index)
{
    if (mesh->index_count == mesh->index_capacity) {
        mesh->index_capacity = mesh->index_capacity ? mesh->index_capacity * 2 : 192;
        mesh->indices = ecs_os_realloc_n(mesh->indices, uint32_t, mesh->index_capacity);
    }

    mesh->indices[mesh->index_count ++] = (uint32_t)index;
}

/** Compute the unnormalized geometric normal of a triangle.
 *
 * @param mesh The mesh
 * @param a First vertex index
 * @param b Second vertex index
 * @param c Third vertex index
 * @param out The normal
 */
static void box_mesh_face_normal(
    const box_mesh_t *mesh,
    int32_t a,
    int32_t b,
    int32_t c,
    vec3 out)
{
    vec3 ab, ac;
    glm_vec3_sub(mesh->vertices[b].position, mesh->vertices[a].position, ab);
    glm_vec3_sub(mesh->vertices[c].position, mesh->vertices[a].position, ac);
    glm_vec3_cross(ab, ac, out);
}

void box_mesh_triangle(
    box_mesh_t *mesh,
    int32_t a,
    int32_t b,
    int32_t c)
{
    vec3 face, sum;
    box_mesh_face_normal(mesh, a, b, c, face);
    glm_vec3_add(mesh->vertices[a].normal, mesh->vertices[b].normal, sum);
    glm_vec3_add(mesh->vertices[c].normal, sum, sum);

    box_mesh_index(mesh, a);
    if (glm_vec3_dot(face, sum) < 0.0f) {
        box_mesh_index(mesh, c);
        box_mesh_index(mesh, b);
    } else {
        box_mesh_index(mesh, b);
        box_mesh_index(mesh, c);
    }
}

float box_shape_quantize(
    float value)
{
    return roundf(value * 4096.0f) / 4096.0f;
}

void box_mesh_fini(
    box_mesh_t *mesh)
{
    ecs_os_free(mesh->vertices);
    ecs_os_free(mesh->indices);
    ecs_os_free(mesh->colors);
    ecs_os_free(mesh->surface_assets);
    ecs_os_memset_t(mesh, 0, box_mesh_t);
}

void box_mesh_bounds(
    box_mesh_t *mesh)
{
    glm_vec3_zero(mesh->min);
    glm_vec3_zero(mesh->max);
    if (mesh->vertex_count) {
        glm_vec3_copy(mesh->vertices[0].position, mesh->min);
        glm_vec3_copy(mesh->vertices[0].position, mesh->max);
    }

    for (int32_t i = 1; i < mesh->vertex_count; i ++) {
        glm_vec3_minv(mesh->min, mesh->vertices[i].position, mesh->min);
        glm_vec3_maxv(mesh->max, mesh->vertices[i].position, mesh->max);
    }
}

void box_mesh_facet(
    box_mesh_t *mesh)
{
    box_mesh_t flat = {0};
    int32_t count = mesh->index_count - mesh->index_count % 3;
    if (mesh->colors) {
        flat.colors = ecs_os_malloc_n(rgba, count ? count : 1);
    }

    for (int32_t t = 0; t < count; t += 3) {
        int32_t idx[3] = {
            (int32_t)mesh->indices[t],
            (int32_t)mesh->indices[t + 1],
            (int32_t)mesh->indices[t + 2]
        };
        vec3 normal;
        box_mesh_face_normal(mesh, idx[0], idx[1], idx[2], normal);
        if (glm_vec3_norm2(normal) < 1e-20f) {
            glm_vec3_add(mesh->vertices[idx[0]].normal, mesh->vertices[idx[1]].normal, normal);
            glm_vec3_add(mesh->vertices[idx[2]].normal, normal, normal);
        }

        if (glm_vec3_norm2(normal) < 1e-20f) {
            glm_vec3_copy((vec3){0.0f, 1.0f, 0.0f}, normal);
        }

        glm_vec3_normalize(normal);
        for (int32_t k = 0; k < 3; k ++) {
            const box_vertex_t *src = &mesh->vertices[idx[k]];
            int32_t v = box_mesh_vertex(&flat, src->position, normal, src->uv[0], src->uv[1]);
            if (flat.colors) {
                flat.colors[v] = mesh->colors[idx[k]];
            }

            box_mesh_index(&flat, v);
        }
    }

    box_mesh_fini(mesh);
    *mesh = flat;
}

void box_mesh_lathe(
    box_mesh_t *mesh,
    const box_lathe_point_t *points,
    int32_t count,
    int32_t slices,
    float arc,
    bool planar)
{
    int32_t base = mesh->vertex_count;
    int32_t stride = slices + 1;
    for (int32_t i = 0; i < count; i ++) {
        const box_lathe_point_t *p = &points[i];
        for (int32_t s = 0; s <= slices; s ++) {
            float step = (p->r == 0.0f && s < slices) ? (float)s + 0.5f : (float)s;
            float angle = arc * step / (float)slices;
            float ca = cosf(angle), sa = sinf(angle);
            vec3 pos = { p->r * ca, p->y, p->r * sa };
            vec3 normal = { p->nr * ca, p->ny, p->nr * sa };
            glm_vec3_normalize(normal);
            if (planar) {
                box_mesh_vertex(mesh, pos, normal, 0.5f + 0.5f * pos[0], 0.5f + 0.5f * pos[2]);
            } else {
                box_mesh_vertex(mesh, pos, normal, step / (float)slices, p->v);
            }
        }
    }

    for (int32_t i = 0; i < count - 1; i ++) {
        bool pole_a = points[i].r == 0.0f;
        bool pole_b = points[i + 1].r == 0.0f;
        for (int32_t s = 0; s < slices; s ++) {
            int32_t a = base + i * stride + s;
            int32_t b = a + 1;
            int32_t c = a + stride;
            int32_t d = c + 1;
            if (!pole_a && !pole_b) {
                box_mesh_triangle(mesh, a, b, d);
                box_mesh_triangle(mesh, a, d, c);
            } else if (!pole_b) {
                box_mesh_triangle(mesh, a, d, c);
            } else if (!pole_a) {
                box_mesh_triangle(mesh, a, b, c);
            }
        }
    }
}
