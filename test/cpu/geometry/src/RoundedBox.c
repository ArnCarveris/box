/**
 * @file RoundedBox.c
 * @brief RoundedBox mesh: keyed by normalized size, radius and segments.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "mesh_cache.h"
#include "lod_keys.h"

void RoundedBox_size(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, RoundedBox, { .x = 2, .y = 1, .z = 1, .radius = 0.25f, .segments = 4 });
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);
    vec3 lo = { 1e9f, 1e9f, 1e9f };
    vec3 hi = { -1e9f, -1e9f, -1e9f };
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        glm_vec3_minv(lo, mesh->vertices[i].position, lo);
        glm_vec3_maxv(hi, mesh->vertices[i].position, hi);
    }

    test_assert(fabsf((hi[0] - lo[0]) * sm->scale[0] - 2.0f) < 1e-4f);
    test_assert(fabsf((hi[1] - lo[1]) * sm->scale[1] - 1.0f) < 1e-4f);
    test_assert(fabsf((hi[2] - lo[2]) * sm->scale[2] - 1.0f) < 1e-4f);
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        const float *p = mesh->vertices[i].position;
        bool corner = fabsf(p[0]) > hi[0] - 1e-4f && fabsf(p[1]) > hi[1] - 1e-4f &&
            fabsf(p[2]) > hi[2] - 1e-4f;
        test_assert(!corner);
    }

    ecs_fini(world);
}

void RoundedBox_normals(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, RoundedBox, { .x = 2, .y = 1, .z = 1, .radius = 0.25f, .segments = 4 });
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        test_assert(fabsf(glm_vec3_norm((float*)mesh->vertices[i].normal) - 1.0f) < 1e-4f);
    }

    vec3 center = {0};
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        glm_vec3_add(center, mesh->vertices[i].position, center);
    }

    glm_vec3_scale(center, 1.0f / (float)mesh->vertex_count, center);
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        vec3 d;
        glm_vec3_sub(mesh->vertices[i].position, center, d);
        test_assert(glm_vec3_dot((float*)mesh->vertices[i].normal, d) > 0.0f);
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

    ecs_fini(world);
}

void RoundedBox_uvs(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, RoundedBox, { .x = 2, .y = 1, .z = 1, .radius = 0.25f, .segments = 4 });
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);

    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        test_assert(mesh->vertices[i].uv[0] >= 0.0f && mesh->vertices[i].uv[0] <= 1.0f);
        test_assert(mesh->vertices[i].uv[1] >= 0.0f && mesh->vertices[i].uv[1] <= 1.0f);
    }

    ecs_fini(world);
}

void RoundedBox_cache_shared(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);
    ecs_set(world, e1, RoundedBox, { .x = 2, .y = 1, .z = 1, .radius = 0.25f, .segments = 4 });
    ecs_set(world, e2, RoundedBox, { .x = 8, .y = 4, .z = 4, .radius = 1, .segments = 4 });
    ecs_entity_t e3 = ecs_new(world);
    ecs_set(world, e3, RoundedBox, { .x = 2, .y = 2, .z = 1, .radius = 0.25f, .segments = 4 });

    const ShapeMesh *m1 = ecs_get(world, e1, ShapeMesh);
    const ShapeMesh *m2 = ecs_get(world, e2, ShapeMesh);
    test_assert(m1 != NULL);
    test_assert(m2 != NULL);
    test_assert(m1->mesh != 0);
    test_uint(m1->mesh, m2->mesh);
    const ShapeMesh *m3 = ecs_get(world, e3, ShapeMesh);
    test_assert(m3 != NULL);
    test_assert(m3->mesh != 0);
    test_assert(m3->mesh != m1->mesh);

    ecs_fini(world);
}

void RoundedBox_scale(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, RoundedBox, { .x = 2, .y = 3, .z = 1, .radius = 0.5f });
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    test_assert(box_mesh_cache_get(cache, sm->mesh) != NULL);
    test_flt(sm->scale[0], 3.0f);
    test_flt(sm->scale[1], 3.0f);
    test_flt(sm->scale[2], 3.0f);

    ecs_fini(world);
}

void RoundedBox_zero_radius(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, RoundedBox, { .x = 2, .y = 1, .z = 1, .segments = 4 });
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);
    vec3 lo = { 1e9f, 1e9f, 1e9f };
    vec3 hi = { -1e9f, -1e9f, -1e9f };
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        glm_vec3_minv(lo, mesh->vertices[i].position, lo);
        glm_vec3_maxv(hi, mesh->vertices[i].position, hi);
    }

    test_assert(fabsf((hi[0] - lo[0]) * sm->scale[0] - 2.0f) < 1e-4f);
    test_assert(fabsf((hi[1] - lo[1]) * sm->scale[1] - 1.0f) < 1e-4f);
    test_assert(fabsf((hi[2] - lo[2]) * sm->scale[2] - 1.0f) < 1e-4f);
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        const float *n = mesh->vertices[i].normal;
        float largest = fmaxf(fabsf(n[0]), fmaxf(fabsf(n[1]), fabsf(n[2])));
        test_assert(fabsf(largest - 1.0f) < 1e-4f);
    }

    ecs_fini(world);
}

void RoundedBox_zero_radius_normals(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, RoundedBox, { .x = 2, .y = 1, .z = 1, .segments = 4 });
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        test_assert(fabsf(glm_vec3_norm((float*)mesh->vertices[i].normal) - 1.0f) < 1e-4f);
    }

    vec3 center = {0};
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        glm_vec3_add(center, mesh->vertices[i].position, center);
    }

    glm_vec3_scale(center, 1.0f / (float)mesh->vertex_count, center);
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        vec3 d;
        glm_vec3_sub(mesh->vertices[i].position, center, d);
        test_assert(glm_vec3_dot((float*)mesh->vertices[i].normal, d) > 0.0f);
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

    ecs_fini(world);
}

void RoundedBox_lod_keys(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, RoundedBox, { .x = 1, .y = 1, .z = 1, .radius = 0.2f, .segments = 16 });
    test_int(lod_key(world, e, 1)->i[0], 8);
    test_int(lod_key(world, e, 2)->i[0], 4);
    test_int(lod_key(world, e, 3)->i[0], 2);
    lod_test_fewer_indices(world, e, 1);
    lod_test_fewer_indices(world, e, 3);

    /* Counts round up to even after halving: 6 halves to 3, which generates as 4 */
    ecs_entity_t six = ecs_new(world);
    ecs_set(world, six, RoundedBox, { .x = 1, .y = 1, .z = 1, .radius = 0.2f, .segments = 6 });
    test_int(lod_key(world, six, 1)->i[0], 4);
    test_int(lod_key(world, six, 2)->i[0], 2);
    test_uint(lod_mesh(world, six, 3), lod_mesh(world, six, 2));

    /* 3 generates as 4, so level 1 has 2 */
    ecs_entity_t odd = ecs_new(world);
    ecs_set(world, odd, RoundedBox, { .x = 1, .y = 1, .z = 1, .radius = 0.2f, .segments = 3 });
    test_int(lod_key(world, odd, 1)->i[0], 2);

    /* 1 and 2 generate as 2, the minimum, and have no levels */
    for (int32_t segments = 1; segments <= 2; segments ++) {
        ecs_entity_t least = ecs_new(world);
        ecs_set(world, least, RoundedBox, { .x = 1, .y = 2, .z = 1, .radius = 0.2f,
            .segments = segments });
        test_uint(lod_mesh(world, least, 1), lod_mesh(world, least, 0));
        test_uint(lod_mesh(world, least, 3), lod_mesh(world, least, 0));
    }

    ecs_fini(world);
}
