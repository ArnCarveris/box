/**
 * @file Bevel.c
 * @brief Bevel on a Box: per edge radii, the RoundedBox form of one radius, the mesh cache and
 * how the mesh resolves with the order of Box and Bevel, removal and prefabs.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stddef.h>
#include "mesh_cache.h"

/** Edge of a box by its Bevel member and the two axes and sides it lies on */
typedef struct bevel_edge_t {
    size_t offset;  /**< Offset of the radius in Bevel */
    int32_t b;      /**< First axis across the edge */
    int32_t c;      /**< Second axis across the edge */
    float sb;       /**< Side of the edge along b */
    float sc;       /**< Side of the edge along c */
} bevel_edge_t;

/** The 12 edges with the sides documented on Bevel: +X right, +Y top, +Z front */
static const bevel_edge_t bevel_edges[12] = {
    { offsetof(Bevel, top_front), 1, 2, 1, 1 },
    { offsetof(Bevel, top_back), 1, 2, 1, -1 },
    { offsetof(Bevel, top_left), 1, 0, 1, -1 },
    { offsetof(Bevel, top_right), 1, 0, 1, 1 },
    { offsetof(Bevel, bottom_front), 1, 2, -1, 1 },
    { offsetof(Bevel, bottom_back), 1, 2, -1, -1 },
    { offsetof(Bevel, bottom_left), 1, 0, -1, -1 },
    { offsetof(Bevel, bottom_right), 1, 0, -1, 1 },
    { offsetof(Bevel, front_left), 2, 0, 1, -1 },
    { offsetof(Bevel, front_right), 2, 0, 1, 1 },
    { offsetof(Bevel, back_left), 2, 0, -1, -1 },
    { offsetof(Bevel, back_right), 2, 0, -1, 1 }
};

/** Get a pointer to the radius of an edge.
 *
 * @param bevel The bevel
 * @param edge Index in bevel_edges
 * @return The radius
 */
static float* bevel_radius(
    Bevel *bevel,
    int32_t edge)
{
    return (float*)ECS_OFFSET(bevel, bevel_edges[edge].offset);
}

/** Create a world with box.geometry.
 *
 * @return The world
 */
static ecs_world_t* bevel_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);

    return world;
}

/** Create an entity with a Box and a Bevel.
 *
 * @param world The world
 * @param box The box
 * @param bevel The bevel
 * @return The entity
 */
static ecs_entity_t bevel_entity(
    ecs_world_t *world,
    Box box,
    Bevel bevel)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Box, &box);
    ecs_set_ptr(world, e, Bevel, &bevel);

    return e;
}

/** Get the resolved shape of an entity, asserting it has a mesh.
 *
 * @param world The world
 * @param e The entity
 * @return The resolved shape
 */
static const ShapeMesh* bevel_shape(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    test_assert(sm->mesh != 0);
    test_assert(box_mesh_cache_get(box_mesh_cache_of(world), sm->mesh) != NULL);

    return sm;
}

/** Get the mesh of an entity.
 *
 * @param world The world
 * @param e The entity
 * @return The mesh
 */
static const box_mesh_t* bevel_mesh(
    ecs_world_t *world,
    ecs_entity_t e)
{
    return box_mesh_cache_get(box_mesh_cache_of(world), bevel_shape(world, e)->mesh);
}

/** Get how far the shape reaches toward an edge, in the units of the Box.
 *
 * @param world The world
 * @param e The entity
 * @param edge Index in bevel_edges
 * @return The largest sb * b + sc * c over the vertices
 */
static float bevel_reach(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t edge)
{
    const ShapeMesh *sm = bevel_shape(world, e);
    const box_mesh_t *mesh = bevel_mesh(world, e);
    const bevel_edge_t *info = &bevel_edges[edge];
    float result = -1e9f;
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        const float *p = mesh->vertices[i].position;
        float reach = info->sb * p[info->b] * sm->scale[info->b] +
            info->sc * p[info->c] * sm->scale[info->c];
        result = fmaxf(result, reach);
    }

    return result;
}

/** Assert that two entities resolve to meshes with the same vertices.
 *
 * @param world The world
 * @param a The first entity
 * @param b The second entity
 */
static void bevel_same_outline(
    ecs_world_t *world,
    ecs_entity_t a,
    ecs_entity_t b)
{
    const ShapeMesh *sa = bevel_shape(world, a);
    const ShapeMesh *sb = bevel_shape(world, b);
    const box_mesh_t *ma = bevel_mesh(world, a);
    const box_mesh_t *mb = bevel_mesh(world, b);
    test_int(ma->vertex_count, mb->vertex_count);
    test_int(ma->index_count, mb->index_count);
    for (int32_t k = 0; k < 3; k ++) {
        test_flt(sa->scale[k], sb->scale[k]);
    }

    for (int32_t i = 0; i < ma->vertex_count; i ++) {
        test_assert(glm_vec3_distance(ma->vertices[i].position, mb->vertices[i].position) < 1e-6f);
    }

    for (int32_t i = 0; i < ma->index_count; i ++) {
        test_uint(ma->indices[i], mb->indices[i]);
    }
}

void Bevel_size(void) {
    ecs_world_t *world = bevel_world();
    ecs_entity_t e = bevel_entity(world, (Box){ 2, 1, 1.5f },
        (Bevel){ .top_front = 0.3f, .front_right = 0.2f, .bottom_left = 0.1f, .segments = 4 });
    const ShapeMesh *sm = bevel_shape(world, e);
    const box_mesh_t *mesh = bevel_mesh(world, e);
    vec3 lo = { 1e9f, 1e9f, 1e9f };
    vec3 hi = { -1e9f, -1e9f, -1e9f };
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        glm_vec3_minv(lo, mesh->vertices[i].position, lo);
        glm_vec3_maxv(hi, mesh->vertices[i].position, hi);
    }

    test_flt(sm->scale[0], 2.0f);
    test_flt(sm->scale[1], 2.0f);
    test_flt(sm->scale[2], 2.0f);
    test_assert(fabsf((hi[0] - lo[0]) * sm->scale[0] - 2.0f) < 1e-3f);
    test_assert(fabsf((hi[1] - lo[1]) * sm->scale[1] - 1.0f) < 1e-3f);
    test_assert(fabsf((hi[2] - lo[2]) * sm->scale[2] - 1.5f) < 1e-3f);
    for (int32_t k = 0; k < 3; k ++) {
        test_assert(fabsf(lo[k] + hi[k]) < 1e-3f);
        test_assert(fabsf(mesh->min[k] - lo[k]) < 1e-6f);
        test_assert(fabsf(mesh->max[k] - hi[k]) < 1e-6f);
    }

    ecs_fini(world);
}

void Bevel_normals(void) {
    ecs_world_t *world = bevel_world();
    ecs_entity_t e = bevel_entity(world, (Box){ 2, 1, 1.5f },
        (Bevel){ .top_front = 0.3f, .front_right = 0.2f, .bottom_left = 0.1f, .segments = 4 });
    const box_mesh_t *mesh = bevel_mesh(world, e);
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *v = &mesh->vertices[i];
        test_assert(fabsf(glm_vec3_norm((float*)v->normal) - 1.0f) < 1e-4f);
        test_assert(glm_vec3_dot((float*)v->normal, (float*)v->position) > 0.0f);
    }

    for (int32_t t = 0; t < mesh->index_count; t += 3) {
        const box_vertex_t *a = &mesh->vertices[mesh->indices[t]];
        const box_vertex_t *b = &mesh->vertices[mesh->indices[t + 1]];
        const box_vertex_t *c = &mesh->vertices[mesh->indices[t + 2]];
        vec3 ab, ac, face, sum;
        glm_vec3_sub((float*)b->position, (float*)a->position, ab);
        glm_vec3_sub((float*)c->position, (float*)a->position, ac);
        glm_vec3_cross(ab, ac, face);
        glm_vec3_add((float*)a->normal, (float*)b->normal, sum);
        glm_vec3_add((float*)c->normal, sum, sum);
        test_assert(glm_vec3_dot(face, sum) > 0.0f);
    }

    const ShapeMesh *sm = bevel_shape(world, e);
    vec3 corner = { 1.0f / sm->scale[0], -0.5f / sm->scale[1], -0.75f / sm->scale[2] };
    bool faces[3] = { false, false, false };
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *v = &mesh->vertices[i];
        if (glm_vec3_distance((float*)v->position, corner) < 1e-4f) {
            faces[0] |= fabsf(v->normal[0] - 1.0f) < 1e-4f;
            faces[1] |= fabsf(v->normal[1] + 1.0f) < 1e-4f;
            faces[2] |= fabsf(v->normal[2] + 1.0f) < 1e-4f;
        }
    }

    test_assert(faces[0] && faces[1] && faces[2]);
    ecs_fini(world);
}

void Bevel_continuous_normals(void) {
    ecs_world_t *world = bevel_world();
    Bevel bevel = { .segments = 4 };
    for (int32_t edge = 0; edge < 12; edge ++) {
        *bevel_radius(&bevel, edge) = 0.05f + 0.02f * (float)edge;
    }

    ecs_entity_t e = bevel_entity(world, (Box){ 2, 1.6f, 1.8f }, bevel);
    const box_mesh_t *mesh = bevel_mesh(world, e);
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *a = &mesh->vertices[i];
        for (int32_t j = i + 1; j < mesh->vertex_count; j ++) {
            const box_vertex_t *b = &mesh->vertices[j];
            if (glm_vec3_distance((float*)a->position, (float*)b->position) < 1e-5f) {
                test_assert(glm_vec3_dot((float*)a->normal, (float*)b->normal) > 0.999f);
            }
        }
    }

    ecs_fini(world);
}

void Bevel_uvs(void) {
    ecs_world_t *world = bevel_world();
    ecs_entity_t e = bevel_entity(world, (Box){ 2, 1, 1.5f },
        (Bevel){ .top_front = 0.3f, .front_right = 0.2f, .bottom_left = 0.1f, .segments = 4 });
    const box_mesh_t *mesh = bevel_mesh(world, e);
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        test_assert(mesh->vertices[i].uv[0] >= 0.0f && mesh->vertices[i].uv[0] <= 1.0f);
        test_assert(mesh->vertices[i].uv[1] >= 0.0f && mesh->vertices[i].uv[1] <= 1.0f);
    }

    int32_t top = 0;
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *v = &mesh->vertices[i];
        if (v->normal[1] > 0.9999f && fabsf(v->position[1] - 0.25f) < 1e-5f) {
            test_assert(fabsf(v->uv[0] - (0.5f + 0.5f * v->position[2] / 0.375f)) < 1e-4f);
            test_assert(fabsf(v->uv[1] - (0.5f - 0.5f * v->position[0] / 0.5f)) < 1e-4f);
            top ++;
        }
    }

    test_assert(top > 4);
    ecs_fini(world);
}

void Bevel_edge_names(void) {
    ecs_world_t *world = bevel_world();
    Box box = { 2, 1.6f, 1.2f };
    vec3 half = { 1.0f, 0.8f, 0.6f };
    for (int32_t edge = 0; edge < 12; edge ++) {
        Bevel bevel = { .segments = 4 };
        *bevel_radius(&bevel, edge) = 0.3f;
        ecs_entity_t e = bevel_entity(world, box, bevel);
        for (int32_t other = 0; other < 12; other ++) {
            const bevel_edge_t *info = &bevel_edges[other];
            float sharp = half[info->b] + half[info->c];
            float reach = bevel_reach(world, e, other);
            if (other == edge) {
                test_assert(fabsf(reach - (sharp - 0.3f * (2.0f - sqrtf(2.0f)))) < 0.01f);
            } else {
                test_assert(fabsf(reach - sharp) < 1e-3f);
            }
        }

        ecs_delete(world, e);
    }

    ecs_fini(world);
}

void Bevel_scaled_radii(void) {
    ecs_world_t *world = bevel_world();
    ecs_entity_t e = bevel_entity(world, (Box){ 1, 1, 1 },
        (Bevel){ .top_front = 0.8f, .top_back = 0.8f, .segments = 4 });
    const ShapeMesh *sm = bevel_shape(world, e);
    const box_mesh_t *mesh = bevel_mesh(world, e);
    vec3 lo = { 1e9f, 1e9f, 1e9f };
    vec3 hi = { -1e9f, -1e9f, -1e9f };
    float front_top = -1e9f;
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *v = &mesh->vertices[i];
        glm_vec3_minv(lo, (float*)v->position, lo);
        glm_vec3_maxv(hi, (float*)v->position, hi);
        if (v->normal[2] > 0.9999f && fabsf(v->position[2] - 0.5f) < 1e-5f) {
            front_top = fmaxf(front_top, v->position[1]);
        }

        if (v->normal[1] > 0.9999f) {
            test_assert(fabsf(v->position[2]) < 0.01f);
        }
    }

    test_flt(sm->scale[0], 1.0f);
    for (int32_t k = 0; k < 3; k ++) {
        test_assert(fabsf(lo[k] + 0.5f) < 1e-3f);
        test_assert(fabsf(hi[k] - 0.5f) < 1e-3f);
    }

    test_assert(fabsf(front_top) < 0.01f);
    test_assert(fabsf(bevel_reach(world, e, 0) - (1.0f - 0.5f * (2.0f - sqrtf(2.0f)))) < 0.01f);
    ecs_fini(world);
}

void Bevel_thin_box(void) {
    ecs_world_t *world = bevel_world();
    Box box = { 2, 0.1f, 1 };
    float sharp = 1.0f + 0.5f, rounded = sharp - 0.3f * (2.0f - sqrtf(2.0f));
    ecs_entity_t all = bevel_entity(world, box, (Bevel){ .front_left = 0.3f,
        .front_right = 0.3f, .back_left = 0.3f, .back_right = 0.3f, .segments = 4 });
    ecs_entity_t one = bevel_entity(world, box, (Bevel){ .front_right = 0.3f, .segments = 4 });
    for (int32_t edge = 8; edge < 12; edge ++) {
        test_assert(fabsf(bevel_reach(world, all, edge) - rounded) < 1e-3f);
        test_assert(fabsf(bevel_reach(world, one, edge) - (edge == 9 ? rounded : sharp)) < 1e-3f);
    }

    ecs_entity_t across = bevel_entity(world, box, (Bevel){ .top_front = 0.3f, .segments = 4 });
    float fitted = 0.1f - 0.0005f * 2.0f;
    float limit = 0.05f + 0.5f - fitted * (2.0f - sqrtf(2.0f));
    test_assert(fabsf(bevel_reach(world, across, 0) - limit) < 1e-3f);
    ecs_fini(world);
}

void Bevel_radius_overrides(void) {
    ecs_world_t *world = bevel_world();
    ecs_entity_t bevel = bevel_entity(world, (Box){ 2, 1, 1.5f },
        (Bevel){ .radius = 0.25f, .top_front = 0.4f, .back_left = 0.1f, .segments = 4 });
    ecs_entity_t rounded = ecs_new(world);
    ecs_set(world, rounded, RoundedBox, { 2, 1, 1.5f, .radius = 0.25f, .segments = 4 });
    bevel_same_outline(world, bevel, rounded);

    ecs_set(world, bevel, Bevel, { .radius = 3, .segments = 6 });
    ecs_set(world, rounded, RoundedBox, { 2, 1, 1.5f, .radius = 3, .segments = 6 });
    bevel_same_outline(world, bevel, rounded);
    ecs_fini(world);
}

void Bevel_equal_edges(void) {
    ecs_world_t *world = bevel_world();
    Bevel value = { .segments = 4 };
    for (int32_t edge = 0; edge < 12; edge ++) {
        *bevel_radius(&value, edge) = 0.25f;
    }

    ecs_entity_t bevel = bevel_entity(world, (Box){ 2, 1, 1.5f }, value);
    ecs_entity_t rounded = ecs_new(world);
    ecs_set(world, rounded, RoundedBox, { 2, 1, 1.5f, .radius = 0.25f, .segments = 4 });
    bevel_same_outline(world, bevel, rounded);

    ecs_entity_t uniform = bevel_entity(world, (Box){ 2, 1, 1.5f },
        (Bevel){ .radius = 0.25f, .segments = 4 });
    test_uint(bevel_shape(world, bevel)->mesh, bevel_shape(world, uniform)->mesh);
    ecs_fini(world);
}

void Bevel_cache_shared(void) {
    ecs_world_t *world = bevel_world();
    Bevel value = { .top_front = 0.2f, .front_left = 0.1f, .segments = 4 };
    ecs_entity_t e1 = bevel_entity(world, (Box){ 2, 1, 1.5f }, value);
    ecs_entity_t e2 = bevel_entity(world, (Box){ 2, 1, 1.5f }, value);
    ecs_entity_t e3 = bevel_entity(world, (Box){ 4, 2, 3 },
        (Bevel){ .top_front = 0.4f, .front_left = 0.2f, .segments = 4 });
    const ShapeMesh *m1 = bevel_shape(world, e1);
    const ShapeMesh *m2 = bevel_shape(world, e2);
    const ShapeMesh *m3 = bevel_shape(world, e3);
    test_uint(m1->mesh, m2->mesh);
    test_uint(m1->mesh, m3->mesh);
    test_flt(m3->scale[0], 4.0f);

    ecs_entity_t e4 = bevel_entity(world, (Box){ 2, 1, 1.5f },
        (Bevel){ .top_front = 0.2f + 1e-6f, .front_left = 0.1f, .segments = 4 });
    test_uint(bevel_shape(world, e4)->mesh, m1->mesh);

    ecs_entity_t e5 = bevel_entity(world, (Box){ 2, 1, 1.5f },
        (Bevel){ .top_front = 0.2f, .front_left = 0.1f, .segments = 8 });
    test_assert(bevel_shape(world, e5)->mesh != m1->mesh);
    ecs_fini(world);
}

void Bevel_cache_single_edge(void) {
    ecs_world_t *world = bevel_world();
    Bevel base = { .segments = 4 };
    for (int32_t edge = 0; edge < 12; edge ++) {
        *bevel_radius(&base, edge) = edge % 3 ? 0.1f : 0.0f;
    }

    uint32_t meshes[13];
    meshes[0] = bevel_shape(world, bevel_entity(world, (Box){ 2, 1, 1.5f }, base))->mesh;
    for (int32_t edge = 0; edge < 12; edge ++) {
        Bevel changed = base;
        *bevel_radius(&changed, edge) += 0.05f;
        ecs_entity_t e = bevel_entity(world, (Box){ 2, 1, 1.5f }, changed);
        meshes[edge + 1] = bevel_shape(world, e)->mesh;
    }

    for (int32_t i = 0; i < 13; i ++) {
        for (int32_t j = i + 1; j < 13; j ++) {
            test_assert(meshes[i] != meshes[j]);
        }
    }

    ecs_fini(world);
}

void Bevel_resolve_order(void) {
    ecs_world_t *world = bevel_world();
    Box box = { 2, 1, 1.5f };
    Bevel bevel = { .top_front = 0.3f, .front_right = 0.2f, .segments = 4 };
    ecs_entity_t box_first = ecs_new(world);
    ecs_set_ptr(world, box_first, Box, &box);
    ecs_set_ptr(world, box_first, Bevel, &bevel);

    ecs_entity_t bevel_first = ecs_new(world);
    ecs_set_ptr(world, bevel_first, Bevel, &bevel);
    test_assert(!ecs_has(world, bevel_first, ShapeMesh));
    ecs_set_ptr(world, bevel_first, Box, &box);

    ecs_entity_t deferred = ecs_new(world);
    ecs_entity_t deferred_bevel_first = ecs_new(world);
    ecs_readonly_begin(world, false);
    ecs_world_t *stage = ecs_get_stage(world, 0);
    ecs_set_ptr(stage, deferred, Box, &box);
    ecs_set_ptr(stage, deferred, Bevel, &bevel);
    ecs_set_ptr(stage, deferred_bevel_first, Bevel, &bevel);
    ecs_set_ptr(stage, deferred_bevel_first, Box, &box);
    test_assert(!ecs_has(world, deferred, Box));
    ecs_readonly_end(world);

    ecs_entity_t plain = ecs_new(world);
    ecs_set_ptr(world, plain, Box, &box);

    const ShapeMesh *expected = bevel_shape(world, box_first);
    test_assert(expected->mesh != bevel_shape(world, plain)->mesh);
    ecs_entity_t others[3] = { bevel_first, deferred, deferred_bevel_first };
    for (int32_t i = 0; i < 3; i ++) {
        const ShapeMesh *sm = bevel_shape(world, others[i]);
        test_uint(sm->mesh, expected->mesh);
        for (int32_t k = 0; k < 3; k ++) {
            test_flt(sm->scale[k], expected->scale[k]);
        }
    }

    ecs_fini(world);
}

void Bevel_resolve_script(void) {
    ecs_world_t *world = bevel_world();
    test_int(ecs_script_run(world, "bevels",
        "using box.geometry\n"
        "box_first { Box: {2, 1, 1.5}\n Bevel: {top_front: 0.3, front_right: 0.2} }\n"
        "bevel_first { Bevel: {top_front: 0.3, front_right: 0.2}\n Box: {2, 1, 1.5} }\n"
        "prefab Counter { Bevel: {top_front: 0.3, front_right: 0.2}\n Box: {2, 1, 1.5} }\n"
        "instance : Counter {}\n", NULL), 0);
    const ShapeMesh *expected = bevel_shape(world, ecs_lookup(world, "box_first"));
    test_uint(bevel_shape(world, ecs_lookup(world, "bevel_first"))->mesh, expected->mesh);
    test_uint(bevel_shape(world, ecs_lookup(world, "instance"))->mesh, expected->mesh);
    test_flt(bevel_shape(world, ecs_lookup(world, "instance"))->scale[0], 2.0f);
    ecs_fini(world);
}

void Bevel_zero_radii(void) {
    ecs_world_t *world = bevel_world();
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Box, { 2, 1, 1.5f });
    ecs_entity_t e = bevel_entity(world, (Box){ 2, 1, 1.5f }, (Bevel){ .segments = 4 });
    const ShapeMesh *sm = bevel_shape(world, e);
    test_uint(sm->mesh, bevel_shape(world, plain)->mesh);
    test_flt(sm->scale[0], 2.0f);
    test_flt(sm->scale[1], 1.0f);
    test_flt(sm->scale[2], 1.5f);

    ecs_set(world, e, Bevel, { .radius = -1, .segments = 4 });
    test_uint(bevel_shape(world, e)->mesh, bevel_shape(world, plain)->mesh);
    ecs_set(world, e, Bevel, { .top_front = -1, .segments = 4 });
    test_uint(bevel_shape(world, e)->mesh, bevel_shape(world, plain)->mesh);
    ecs_fini(world);
}

void Bevel_remove_bevel(void) {
    ecs_world_t *world = bevel_world();
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Box, { 2, 1, 1.5f });
    ecs_entity_t e = bevel_entity(world, (Box){ 2, 1, 1.5f },
        (Bevel){ .top_front = 0.3f, .segments = 4 });
    uint32_t beveled = bevel_shape(world, e)->mesh;
    test_assert(beveled != bevel_shape(world, plain)->mesh);
    ecs_remove(world, e, Bevel);
    const ShapeMesh *sm = bevel_shape(world, e);
    test_uint(sm->mesh, bevel_shape(world, plain)->mesh);
    test_flt(sm->scale[0], 2.0f);
    test_flt(sm->scale[1], 1.0f);
    test_flt(sm->scale[2], 1.5f);
    test_assert(box_mesh_cache_get(cache, beveled) == NULL);
    ecs_fini(world);
}

void Bevel_update_size(void) {
    ecs_world_t *world = bevel_world();
    ecs_entity_t e = bevel_entity(world, (Box){ 2, 1, 1.5f },
        (Bevel){ .top_front = 0.3f, .segments = 4 });
    uint32_t mesh = bevel_shape(world, e)->mesh;

    ecs_set(world, e, Box, { 4, 2, 3 });
    test_assert(bevel_shape(world, e)->mesh != mesh);
    test_flt(bevel_shape(world, e)->scale[0], 4.0f);
    const box_mesh_t *m = bevel_mesh(world, e);
    test_assert(fabsf((m->max[1] - m->min[1]) * 4.0f - 2.0f) < 1e-3f);
    float sharp = 1.0f + 1.5f;
    test_assert(fabsf(bevel_reach(world, e, 0) - (sharp - 0.3f * (2.0f - sqrtf(2.0f)))) < 0.01f);
    ecs_fini(world);
}

void Bevel_update_radius(void) {
    ecs_world_t *world = bevel_world();
    ecs_entity_t e = bevel_entity(world, (Box){ 2, 1, 1.5f },
        (Bevel){ .top_front = 0.3f, .segments = 4 });
    uint32_t mesh = bevel_shape(world, e)->mesh;
    ecs_set(world, e, Bevel, { .top_front = 0.2f, .segments = 4 });
    test_assert(bevel_shape(world, e)->mesh != mesh);
    float sharp = 0.5f + 0.75f;
    test_assert(fabsf(bevel_reach(world, e, 0) - (sharp - 0.2f * (2.0f - sqrtf(2.0f)))) < 0.01f);
    ecs_fini(world);
}

void Bevel_remove_box(void) {
    ecs_world_t *world = bevel_world();
    Bevel bevel = { .top_front = 0.3f, .back_left = 0.1f, .segments = 4 };
    ecs_entity_t reference = bevel_entity(world, (Box){ 2, 1, 1.5f }, bevel);
    ecs_entity_t e = bevel_entity(world, (Box){ 2, 1, 1.5f }, bevel);
    test_uint(bevel_shape(world, e)->mesh, bevel_shape(world, reference)->mesh);
    ecs_remove(world, e, Box);
    test_assert(!ecs_has(world, e, ShapeMesh));
    test_assert(ecs_has(world, e, Bevel));
    ecs_set(world, e, Box, { 2, 1, 1.5f });
    test_uint(bevel_shape(world, e)->mesh, bevel_shape(world, reference)->mesh);
    test_flt(bevel_shape(world, e)->scale[0], 2.0f);
    ecs_fini(world);
}

void Bevel_delete(void) {
    ecs_world_t *world = bevel_world();
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    ecs_entity_t e = bevel_entity(world, (Box){ 2, 1, 1.5f },
        (Bevel){ .top_front = 0.3f, .segments = 4 });
    uint32_t mesh = bevel_shape(world, e)->mesh;
    test_int(box_mesh_cache_users(cache, mesh), 1);
    test_int(box_mesh_cache_count(cache), 1);
    ecs_delete(world, e);
    test_assert(box_mesh_cache_get(cache, mesh) == NULL);
    test_int(box_mesh_cache_count(cache), 0);
    ecs_fini(world);
}

void Bevel_prefab_instance(void) {
    ecs_world_t *world = bevel_world();
    Bevel bevel = { .top_front = 0.3f, .front_right = 0.2f, .segments = 4 };
    ecs_entity_t reference = bevel_entity(world, (Box){ 2, 1, 1.5f }, bevel);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Box, { 2, 1, 1.5f });
    ecs_set_ptr(world, prefab, Bevel, &bevel);
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    const ShapeMesh *sm = bevel_shape(world, instance);
    test_uint(sm->mesh, bevel_shape(world, reference)->mesh);
    test_flt(sm->scale[0], 2.0f);
    ecs_fini(world);
}

void Bevel_prefab_override(void) {
    ecs_world_t *world = bevel_world();
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Box, { 2, 1, 1.5f });
    ecs_set(world, prefab, Bevel, { .top_front = 0.3f, .segments = 4 });
    ecs_entity_t inherited = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t bevel_override = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t box_override = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, bevel_override, Bevel, { .back_left = 0.2f, .segments = 4 });
    ecs_set(world, box_override, Box, { 1, 1, 1 });

    ecs_entity_t bevel_reference = bevel_entity(world, (Box){ 2, 1, 1.5f },
        (Bevel){ .back_left = 0.2f, .segments = 4 });
    ecs_entity_t box_reference = bevel_entity(world, (Box){ 1, 1, 1 },
        (Bevel){ .top_front = 0.3f, .segments = 4 });
    uint32_t prefab_mesh = bevel_shape(world, inherited)->mesh;
    test_uint(bevel_shape(world, bevel_override)->mesh, bevel_shape(world, bevel_reference)->mesh);
    test_uint(bevel_shape(world, box_override)->mesh, bevel_shape(world, box_reference)->mesh);
    test_assert(bevel_shape(world, bevel_override)->mesh != prefab_mesh);
    test_assert(bevel_shape(world, box_override)->mesh != prefab_mesh);
    test_flt(bevel_shape(world, box_override)->scale[0], 1.0f);

    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, Box, { 2, 1, 1.5f });
    ecs_remove(world, bevel_override, Bevel);
    test_uint(bevel_shape(world, bevel_override)->mesh, bevel_shape(world, plain)->mesh);
    ecs_fini(world);
}
