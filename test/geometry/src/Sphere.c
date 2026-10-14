/**
 * @file Sphere.c
 * @brief Sphere mesh: a UV sphere keyed by segments and smooth.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "mesh_cache.h"

void Sphere_segments(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sphere, { .segments = 6, .smooth = true, .radius = 10 });
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

    test_int(count, 6);

    ecs_fini(world);
}

void Sphere_flat_faces(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sphere, { .segments = 6, .smooth = false, .radius = 10 });
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

void Sphere_normals(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sphere, { .segments = 6, .smooth = true, .radius = 10 });
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

void Sphere_flat_normals(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sphere, { .segments = 6, .smooth = false, .radius = 10 });
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

void Sphere_uvs(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sphere, { .segments = 6, .smooth = true, .radius = 10 });
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

void Sphere_cache_shared(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);
    ecs_set(world, e1, Sphere, { .segments = 6, .smooth = true, .radius = 1 });
    ecs_set(world, e2, Sphere, { .segments = 6, .smooth = true, .radius = 10 });
    ecs_entity_t e3 = ecs_new(world);
    ecs_set(world, e3, Sphere, { .segments = 8, .smooth = true, .radius = 1 });

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

void Sphere_scale(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sphere, { .segments = 6, .radius = 10 });
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    test_assert(box_mesh_cache_get(cache, sm->mesh) != NULL);
    test_flt(sm->scale[0], 10.0f);
    test_flt(sm->scale[1], 10.0f);
    test_flt(sm->scale[2], 10.0f);

    ecs_fini(world);
}

void Sphere_default_segments(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);
    ecs_add(world, e1, Sphere);
    ecs_get_mut(world, e1, Sphere)->smooth = true;
    ecs_get_mut(world, e1, Sphere)->radius = 1;
    ecs_modified(world, e1, Sphere);
    ecs_set(world, e2, Sphere, { .segments = 24, .smooth = true, .radius = 1 });
    const ShapeMesh *m1 = ecs_get(world, e1, ShapeMesh);
    const ShapeMesh *m2 = ecs_get(world, e2, ShapeMesh);
    test_assert(m1 != NULL);
    test_assert(m2 != NULL);
    test_assert(m1->mesh != 0);
    test_uint(m1->mesh, m2->mesh);

    ecs_fini(world);
}

void Sphere_default_segments_normals(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, Sphere);
    ecs_get_mut(world, e, Sphere)->smooth = true;
    ecs_get_mut(world, e, Sphere)->radius = 1;
    ecs_modified(world, e, Sphere);
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

void Sphere_lantern_ribs(void) {
    // renderer/sphere_gaps
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    for (int32_t smooth = 0; smooth < 2; smooth ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Sphere, { .segments = 8, .smooth = smooth, .radius = 0.25f });
        const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
        test_assert(sm != NULL);
        const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
        test_assert(mesh != NULL);
        int32_t outside = 0;
        for (int32_t rib = -1; rib <= 1; rib += 2) {
            for (int32_t a = 0; a < 64; a ++) {
                for (int32_t b = 0; b < 8; b ++) {
                    float around = 2.0f * GLM_PIf * (float)a / 64.0f;
                    float tube = 2.0f * GLM_PIf * (float)b / 8.0f;
                    float r = 0.185f + 0.014f * cosf(tube);
                    vec3 p = {
                        r * cosf(around) / 0.25f,
                        (0.12f * (float)rib + 0.014f * sinf(tube)) / 0.25f,
                        r * sinf(around) / 0.25f
                    };
                    bool inside = true;
                    for (int32_t t = 0; t < mesh->index_count; t += 3) {
                        const float *v0 = mesh->vertices[mesh->indices[t]].position;
                        const float *v1 = mesh->vertices[mesh->indices[t + 1]].position;
                        const float *v2 = mesh->vertices[mesh->indices[t + 2]].position;
                        vec3 e1, e2, face, d;
                        glm_vec3_sub((float*)v1, (float*)v0, e1);
                        glm_vec3_sub((float*)v2, (float*)v0, e2);
                        glm_vec3_cross(e1, e2, face);
                        glm_vec3_sub(p, (float*)v0, d);
                        if (glm_vec3_norm(face) > 1e-8f && glm_vec3_dot(face, d) >= 0.0f) {
                            inside = false;
                        }
                    }

                    outside += !inside;
                }
            }
        }

        test_int(outside, 0);
    }

    ecs_fini(world);
}
