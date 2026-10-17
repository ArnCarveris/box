/**
 * @file Cache.c
 * @brief Mesh cache: meshes are shared by key, reference counted and freed with the last user.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "mesh_cache.h"

static void SetSphere(ecs_iter_t *it) {
    ecs_entity_t *e = it->ctx;
    ecs_set(it->stage, *e, Sphere, { .segments = 6, .radius = 4 });
}

void Cache_release_on_remove(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sphere, { .segments = 6, .radius = 1 });
    test_assert(ecs_has(world, e, ShapeMesh));
    uint32_t mesh = ecs_get(world, e, ShapeMesh)->mesh;
    test_assert(box_mesh_cache_get(cache, mesh) != NULL);

    ecs_remove(world, e, Sphere);
    test_assert(!ecs_has(world, e, ShapeMesh));
    test_assert(box_mesh_cache_get(cache, mesh) == NULL);
    test_int(box_mesh_cache_count(cache), 0);

    ecs_fini(world);
}

void Cache_release_on_delete(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sphere, { .segments = 6, .radius = 1 });
    uint32_t mesh = ecs_get(world, e, ShapeMesh)->mesh;
    test_assert(box_mesh_cache_get(cache, mesh) != NULL);

    ecs_delete(world, e);
    test_assert(box_mesh_cache_get(cache, mesh) == NULL);
    test_int(box_mesh_cache_count(cache), 0);

    ecs_fini(world);
}

void Cache_shared_release(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);
    ecs_set(world, e1, Sphere, { .segments = 6, .radius = 1 });
    ecs_set(world, e2, Sphere, { .segments = 6, .radius = 10 });
    uint32_t mesh = ecs_get(world, e1, ShapeMesh)->mesh;
    test_uint(ecs_get(world, e2, ShapeMesh)->mesh, mesh);

    ecs_delete(world, e1);
    test_uint(ecs_get(world, e2, ShapeMesh)->mesh, mesh);
    test_assert(box_mesh_cache_get(cache, mesh) != NULL);

    ecs_delete(world, e2);
    test_assert(box_mesh_cache_get(cache, mesh) == NULL);
    test_int(box_mesh_cache_count(cache), 0);

    ecs_fini(world);
}

void Cache_change_topology(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, e, Sphere, { .segments = 6, .radius = 1 });
    ecs_set(world, other, Sphere, { .segments = 8, .radius = 1 });
    uint32_t six = ecs_get(world, e, ShapeMesh)->mesh;
    uint32_t eight = ecs_get(world, other, ShapeMesh)->mesh;
    test_assert(six != eight);

    ecs_set(world, e, Sphere, { .segments = 8, .radius = 1 });
    test_uint(ecs_get(world, e, ShapeMesh)->mesh, eight);

    ecs_delete(world, e);
    ecs_delete(world, other);
    test_int(box_mesh_cache_count(cache), 0);

    ecs_fini(world);
}

void Cache_change_size(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sphere, { .segments = 6, .radius = 1 });
    uint32_t mesh = ecs_get(world, e, ShapeMesh)->mesh;

    ecs_set(world, e, Sphere, { .segments = 6, .radius = 5 });
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_uint(sm->mesh, mesh);
    test_flt(sm->scale[0], 5.0f);

    ecs_delete(world, e);
    test_int(box_mesh_cache_count(cache), 0);

    ecs_fini(world);
}

void Cache_different_shapes(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);
    ecs_set(world, e1, Cone, { .segments = 8, .length = 1, .radius = 1 });
    ecs_set(world, e2, Cylinder, { .segments = 8, .length = 1, .radius = 1 });
    test_assert(ecs_get(world, e1, ShapeMesh)->mesh != ecs_get(world, e2, ShapeMesh)->mesh);

    ecs_fini(world);
}

void Cache_quantized_proportions(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);
    ecs_set(world, e1, Capsule, { .segments = 8, .length = 2, .radius = 1 });
    ecs_set(world, e2, Capsule, { .segments = 8, .length = 2.00001f, .radius = 1 });
    test_uint(ecs_get(world, e1, ShapeMesh)->mesh, ecs_get(world, e2, ShapeMesh)->mesh);

    ecs_fini(world);
}

void Cache_bounds(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Capsule, { .segments = 8, .smooth = true, .length = 6, .radius = 3 });
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);
    vec3 lo = { 1e9f, 1e9f, 1e9f };
    vec3 hi = { -1e9f, -1e9f, -1e9f };
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        glm_vec3_minv(lo, mesh->vertices[i].position, lo);
        glm_vec3_maxv(hi, mesh->vertices[i].position, hi);
    }

    for (int32_t i = 0; i < 3; i ++) {
        test_assert(fabsf(mesh->min[i] - lo[i]) < 1e-4f);
        test_assert(fabsf(mesh->max[i] - hi[i]) < 1e-4f);
    }

    test_assert(fabsf(mesh->min[0] * sm->scale[0] + 3.0f) < 1e-4f);
    test_assert(fabsf(mesh->max[0] * sm->scale[0] - 3.0f) < 1e-4f);
    test_assert(fabsf(mesh->min[1] * sm->scale[1] + 6.0f) < 1e-4f);
    test_assert(fabsf(mesh->max[1] * sm->scale[1] - 6.0f) < 1e-4f);

    ecs_fini(world);
}

void Cache_prefab_instances(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t p = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, p, Sphere, { .segments = 6, .radius = 2 });
    ecs_entity_t i1 = ecs_new_w_pair(world, EcsIsA, p);
    ecs_entity_t i2 = ecs_new_w_pair(world, EcsIsA, p);

    const ShapeMesh *m1 = ecs_get(world, i1, ShapeMesh);
    const ShapeMesh *m2 = ecs_get(world, i2, ShapeMesh);
    test_assert(m1 != NULL);
    test_assert(m2 != NULL);
    test_uint(m1->mesh, m2->mesh);
    test_flt(m1->scale[0], 2.0f);

    ecs_delete(world, i1);
    ecs_delete(world, i2);
    ecs_delete(world, p);
    test_int(box_mesh_cache_count(cache), 0);

    ecs_fini(world);
}

void Cache_clone(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Sphere, { .segments = 6, .radius = 1 });
    uint32_t mesh = ecs_get(world, e, ShapeMesh)->mesh;

    ecs_entity_t c = ecs_clone(world, 0, e, true);
    test_uint(ecs_get(world, c, ShapeMesh)->mesh, mesh);

    ecs_delete(world, c);
    test_assert(box_mesh_cache_get(cache, mesh) != NULL);
    test_uint(ecs_get(world, e, ShapeMesh)->mesh, mesh);

    ecs_delete(world, e);
    test_assert(box_mesh_cache_get(cache, mesh) == NULL);
    test_int(box_mesh_cache_count(cache), 0);

    ecs_fini(world);
}

void Cache_set_in_system(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_system(world, {
        .phase = EcsOnUpdate,
        .callback = SetSphere,
        .ctx = &e
    });

    ecs_progress(world, 0);

    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    test_flt(sm->scale[0], 4.0f);
    test_assert(box_mesh_cache_get(cache, sm->mesh) != NULL);

    ecs_delete(world, e);
    test_int(box_mesh_cache_count(cache), 0);

    ecs_fini(world);
}

void Cache_prefab_override_removed(void) {
    // core_fixes/mesh3_inherit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t p = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, p, Box, {1, 1, 1});
    ecs_entity_t i1 = ecs_new_w_pair(world, EcsIsA, p);
    ecs_entity_t i2 = ecs_new_w_pair(world, EcsIsA, p);
    ecs_set(world, i1, Box, {3, 3, 3});
    test_flt(ecs_get(world, i1, ShapeMesh)->scale[0], 3.0f);

    ecs_remove(world, i1, Box);
    test_assert(!ecs_has(world, i1, Box));
    test_assert(!ecs_has(world, i1, ShapeMesh));
    const ShapeMesh *m2 = ecs_get(world, i2, ShapeMesh);
    test_assert(m2 != NULL);
    test_flt(m2->scale[0], 1.0f);

    ecs_delete(world, i1);
    ecs_delete(world, i2);
    ecs_delete(world, p);
    test_int(box_mesh_cache_count(cache), 0);

    ecs_fini(world);
}
