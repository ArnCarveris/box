/**
 * @file Tube.c
 * @brief Tube mesh: keyed by segments, smooth, the wall to radius ratio and the arc.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "mesh_cache.h"

void Tube_vertex_count(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Tube,
        { .segments = 8, .smooth = true, .length = 2, .radius = 2, .thickness = 1 });
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);
    test_int(mesh->vertex_count, 72);
    test_int(mesh->index_count, 192);

    ecs_fini(world);
}

void Tube_flat_vertex_count(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Tube,
        { .segments = 8, .smooth = false, .length = 2, .radius = 2, .thickness = 1 });
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);
    test_int(mesh->vertex_count, 192);
    test_int(mesh->index_count, 192);

    ecs_fini(world);
}

void Tube_normals(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Tube,
        { .segments = 8, .smooth = true, .length = 2, .radius = 2, .thickness = 1 });
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        test_assert(fabsf(glm_vec3_norm((float*)mesh->vertices[i].normal) - 1.0f) < 1e-4f);
    }

    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *v = &mesh->vertices[i];
        float rho = sqrtf(v->position[0] * v->position[0] + v->position[2] * v->position[2]);
        float radial = (v->normal[0] * v->position[0] + v->normal[2] * v->position[2]) / rho;
        if (fabsf(v->normal[1]) > 0.99f) {
            test_assert(v->normal[1] * v->position[1] > 0.0f);
        } else if (rho > 0.99f) {
            test_assert(radial > 0.5f);
        } else {
            test_flt(rho, 0.5f);
            test_assert(radial < -0.5f);
        }
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

void Tube_flat_normals(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Tube,
        { .segments = 8, .smooth = false, .length = 2, .radius = 2, .thickness = 1 });
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        test_assert(fabsf(glm_vec3_norm((float*)mesh->vertices[i].normal) - 1.0f) < 1e-4f);
    }

    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        const box_vertex_t *v = &mesh->vertices[i];
        float rho = sqrtf(v->position[0] * v->position[0] + v->position[2] * v->position[2]);
        float radial = (v->normal[0] * v->position[0] + v->normal[2] * v->position[2]) / rho;
        if (fabsf(v->normal[1]) > 0.99f) {
            test_assert(v->normal[1] * v->position[1] > 0.0f);
        } else if (rho > 0.99f) {
            test_assert(radial > 0.5f);
        } else {
            test_flt(rho, 0.5f);
            test_assert(radial < -0.5f);
        }
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

void Tube_uvs(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Tube,
        { .segments = 8, .smooth = true, .length = 2, .radius = 2, .thickness = 1 });
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

void Tube_cache_shared(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);
    ecs_set(world, e1, Tube, { .segments = 8, .length = 1, .radius = 1, .thickness = 0.25f });
    ecs_set(world, e2, Tube, { .segments = 8, .length = 5, .radius = 4, .thickness = 1 });
    ecs_entity_t e3 = ecs_new(world);
    ecs_set(world, e3, Tube, { .segments = 8, .length = 1, .radius = 1, .thickness = 0.5f });

    const ShapeMesh *m1 = ecs_get(world, e1, ShapeMesh);
    const ShapeMesh *m2 = ecs_get(world, e2, ShapeMesh);
    test_assert(m1 != NULL);
    test_assert(m2 != NULL);
    test_assert(m1->mesh != 0);
    test_uint(m1->mesh, m2->mesh);
    test_int(box_mesh_cache_refs(cache, m1->mesh), 2);
    const ShapeMesh *m3 = ecs_get(world, e3, ShapeMesh);
    test_assert(m3 != NULL);
    test_assert(m3->mesh != 0);
    test_assert(m3->mesh != m1->mesh);
    test_int(box_mesh_cache_count(cache), 2);

    ecs_fini(world);
}

void Tube_scale(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Tube, { .segments = 8, .length = 4, .radius = 2, .thickness = 1 });
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    test_assert(box_mesh_cache_get(cache, sm->mesh) != NULL);
    test_flt(sm->scale[0], 2.0f);
    test_flt(sm->scale[1], 4.0f);
    test_flt(sm->scale[2], 2.0f);

    ecs_fini(world);
}

void Tube_partial_arc(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Tube,
        { .segments = 8, .smooth = true, .radius = 2, .thickness = 1, .arc = 3.14159265f });
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);
    test_int(mesh->vertex_count, 80);
    test_int(mesh->index_count, 204);

    ecs_fini(world);
}
