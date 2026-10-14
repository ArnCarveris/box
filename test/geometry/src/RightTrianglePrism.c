/**
 * @file RightTrianglePrism.c
 * @brief RightTrianglePrism mesh: an extruded right triangle.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "mesh_cache.h"

void RightTrianglePrism_vertex_count(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, RightTrianglePrism, {2, 3, 4});
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);
    test_int(mesh->vertex_count, 18);
    test_int(mesh->index_count, 24);

    ecs_fini(world);
}

void RightTrianglePrism_normals(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, RightTrianglePrism, {2, 3, 4});
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

void RightTrianglePrism_uvs(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, RightTrianglePrism, {2, 3, 4});
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

void RightTrianglePrism_cache_shared(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);
    ecs_set(world, e1, RightTrianglePrism, {1, 1, 1});
    ecs_set(world, e2, RightTrianglePrism, {4, 5, 6});

    const ShapeMesh *m1 = ecs_get(world, e1, ShapeMesh);
    const ShapeMesh *m2 = ecs_get(world, e2, ShapeMesh);
    test_assert(m1 != NULL);
    test_assert(m2 != NULL);
    test_assert(m1->mesh != 0);
    test_uint(m1->mesh, m2->mesh);
    test_int(box_mesh_cache_refs(cache, m1->mesh), 2);
    test_int(box_mesh_cache_count(cache), 1);

    ecs_fini(world);
}

void RightTrianglePrism_scale(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, RightTrianglePrism, {2, 3, 4});
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    test_assert(box_mesh_cache_get(cache, sm->mesh) != NULL);
    test_flt(sm->scale[0], 2.0f);
    test_flt(sm->scale[1], 3.0f);
    test_flt(sm->scale[2], 4.0f);

    ecs_fini(world);
}
