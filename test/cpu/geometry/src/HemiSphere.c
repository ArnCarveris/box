/**
 * @file HemiSphere.c
 * @brief HemiSphere mesh: the upper half of a sphere with a flat base.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "mesh_cache.h"
#include "lod_keys.h"

void HemiSphere_segments(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, HemiSphere, { .segments = 8, .smooth = true, .radius = 2 });
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);
    float angles[64];
    int32_t count = 0;
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        const float *p = mesh->vertices[i].position;
        if (sqrtf(p[0] * p[0] + p[2] * p[2]) < 1e-4f) {
            continue;
        }

        float angle = atan2f(p[2], p[0]);
        bool known = false;
        for (int32_t j = 0; j < count; j ++) {
            float d = fabsf(angle - angles[j]);
            known |= d < 1e-3f || fabsf(d - 2.0f * GLM_PIf) < 1e-3f;
        }

        if (!known && count < 64) {
            angles[count ++] = angle;
        }
    }

    test_int(count, 8);

    ecs_fini(world);
}

void HemiSphere_flat_faces(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, HemiSphere, { .segments = 8, .smooth = false, .radius = 2 });
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);
    int32_t faces = 0;
    for (int32_t t = 0; t < mesh->index_count; t += 3) {
        const box_vertex_t *a = &mesh->vertices[mesh->indices[t]];
        const box_vertex_t *b = &mesh->vertices[mesh->indices[t + 1]];
        const box_vertex_t *c = &mesh->vertices[mesh->indices[t + 2]];
        vec3 ab, ac, face;
        glm_vec3_sub((float*)b->position, (float*)a->position, ab);
        glm_vec3_sub((float*)c->position, (float*)a->position, ac);
        glm_vec3_cross(ab, ac, face);
        if (glm_vec3_norm(face) < 1e-8f) {
            continue;
        }

        glm_vec3_normalize(face);
        test_assert(glm_vec3_dot(face, (float*)a->normal) > 0.999f);
        test_assert(glm_vec3_dot(face, (float*)b->normal) > 0.999f);
        test_assert(glm_vec3_dot(face, (float*)c->normal) > 0.999f);
        faces ++;
    }

    test_assert(faces > 0);

    ecs_fini(world);
}

void HemiSphere_normals(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, HemiSphere, { .segments = 8, .smooth = true, .radius = 2 });
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

void HemiSphere_flat_normals(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, HemiSphere, { .segments = 8, .smooth = false, .radius = 2 });
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

void HemiSphere_uvs(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, HemiSphere, { .segments = 8, .smooth = true, .radius = 2 });
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

void HemiSphere_cache_shared(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);
    ecs_set(world, e1, HemiSphere, { .segments = 8, .radius = 1 });
    ecs_set(world, e2, HemiSphere, { .segments = 8, .radius = 5 });
    ecs_entity_t e3 = ecs_new(world);
    ecs_set(world, e3, HemiSphere, { .segments = 12, .radius = 1 });

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

void HemiSphere_scale(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, HemiSphere, { .segments = 8, .radius = 3 });
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    test_assert(box_mesh_cache_get(cache, sm->mesh) != NULL);
    test_flt(sm->scale[0], 3.0f);
    test_flt(sm->scale[1], 3.0f);
    test_flt(sm->scale[2], 3.0f);

    ecs_fini(world);
}

void HemiSphere_lod_keys(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, HemiSphere, { .segments = 24, .smooth = true, .radius = 1 });
    test_int(lod_key(world, e, 1)->i[0], 12);
    test_int(lod_key(world, e, 2)->i[0], 6);
    test_int(lod_key(world, e, 3)->i[0], 3);
    lod_test_fewer_indices(world, e, 2);

    /* A HemiSphere does not share the meshes of a Sphere with the same key */
    ecs_entity_t sphere = ecs_new(world);
    ecs_set(world, sphere, Sphere, { .segments = 12, .smooth = true, .radius = 1 });
    test_assert(lod_mesh(world, e, 1) != lod_mesh(world, sphere, 0));

    ecs_entity_t flat = ecs_new(world);
    ecs_set(world, flat, HemiSphere, { .segments = 24, .smooth = false, .radius = 1 });
    test_uint(lod_mesh(world, flat, 2), lod_mesh(world, flat, 0));
    ecs_fini(world);
}
