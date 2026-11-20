/**
 * @file Mesh3.c
 * @brief Mesh3: authored meshes are converted once per distinct content and keep a scale of one.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "mesh_cache.h"

void Mesh3_with_normals(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    Mesh3 m = {0};
    ecs_vec_init_t(NULL, &m.vertices, vec3, 4);
    ecs_vec_init_t(NULL, &m.normals, vec3, 4);
    ecs_vec_init_t(NULL, &m.uvs, vec2, 4);
    ecs_vec_init_t(NULL, &m.indices, uint32_t, 6);
    ecs_vec_set_count_t(NULL, &m.vertices, vec3, 4);
    ecs_vec_set_count_t(NULL, &m.normals, vec3, 4);
    ecs_vec_set_count_t(NULL, &m.uvs, vec2, 4);
    ecs_vec_set_count_t(NULL, &m.indices, uint32_t, 6);
    vec3 *p = ecs_vec_first_t(&m.vertices, vec3);
    vec3 *n = ecs_vec_first_t(&m.normals, vec3);
    vec2 *uv = ecs_vec_first_t(&m.uvs, vec2);
    uint32_t *idx = ecs_vec_first_t(&m.indices, uint32_t);
    const float corners[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    for (int i = 0; i < 4; i ++) {
        glm_vec3_copy((vec3){ corners[i][0], corners[i][1], 0.0f }, p[i]);
        glm_vec3_copy((vec3){ 0.0f, 0.0f, 2.0f }, n[i]);
        uv[i][0] = corners[i][0];
        uv[i][1] = 1.0f - corners[i][1];
    }

    const uint32_t tris[6] = { 0, 1, 2, 0, 2, 3 };
    ecs_os_memcpy(idx, tris, sizeof(tris));

    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Mesh3, &m);
    ecs_vec_fini_t(NULL, &m.vertices, vec3);
    ecs_vec_fini_t(NULL, &m.normals, vec3);
    ecs_vec_fini_t(NULL, &m.uvs, vec2);
    ecs_vec_fini_t(NULL, &m.indices, uint32_t);

    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);
    test_int(mesh->vertex_count, 4);
    test_int(mesh->index_count, 6);
    test_assert(mesh->colors == NULL);
    for (int i = 0; i < 6; i ++) {
        test_uint(mesh->indices[i], tris[i]);
    }

    for (int i = 0; i < 4; i ++) {
        test_flt(mesh->vertices[i].position[0], corners[i][0]);
        test_flt(mesh->vertices[i].position[1], corners[i][1]);
        test_flt(mesh->vertices[i].normal[2], 1.0f);
        test_flt(mesh->vertices[i].uv[0], corners[i][0]);
        test_flt(mesh->vertices[i].uv[1], 1.0f - corners[i][1]);
    }

    test_flt(mesh->min[0], 0.0f);
    test_flt(mesh->max[0], 1.0f);
    test_flt(mesh->max[1], 1.0f);

    ecs_fini(world);
}

void Mesh3_derive_normals(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    Mesh3 m = {0};
    ecs_vec_init_t(NULL, &m.vertices, vec3, 4);
    ecs_vec_init_t(NULL, &m.indices, uint32_t, 6);
    ecs_vec_set_count_t(NULL, &m.vertices, vec3, 4);
    ecs_vec_set_count_t(NULL, &m.indices, uint32_t, 6);
    vec3 *p = ecs_vec_first_t(&m.vertices, vec3);
    glm_vec3_copy((vec3){ 0, 0, 0 }, p[0]);
    glm_vec3_copy((vec3){ 1, 0, 0 }, p[1]);
    glm_vec3_copy((vec3){ 1, 1, 0 }, p[2]);
    glm_vec3_copy((vec3){ 0, 1, 0 }, p[3]);
    const uint32_t tris[6] = { 0, 1, 2, 0, 3, 2 };
    ecs_os_memcpy(ecs_vec_first(&m.indices), tris, sizeof(tris));

    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Mesh3, &m);
    ecs_vec_fini_t(NULL, &m.vertices, vec3);
    ecs_vec_fini_t(NULL, &m.indices, uint32_t);

    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);
    test_int(mesh->index_count, 6);
    for (int i = 0; i < 6; i ++) {
        const box_vertex_t *v = &mesh->vertices[mesh->indices[i]];
        test_flt(v->normal[2], i < 3 ? 1.0f : -1.0f);
        test_flt(v->uv[0], 0.0f);
    }

    ecs_fini(world);
}

void Mesh3_no_indices(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    Mesh3 m = {0};
    ecs_vec_init_t(NULL, &m.vertices, vec3, 7);
    ecs_vec_set_count_t(NULL, &m.vertices, vec3, 7);
    vec3 *p = ecs_vec_first_t(&m.vertices, vec3);
    for (int i = 0; i < 7; i ++) {
        glm_vec3_copy((vec3){ (float)(i % 3 == 1), (float)(i % 3 == 2), (float)(i / 3) }, p[i]);
    }

    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Mesh3, &m);
    ecs_vec_fini_t(NULL, &m.vertices, vec3);

    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);
    test_int(mesh->vertex_count, 6);
    test_int(mesh->index_count, 6);
    for (int i = 0; i < 6; i ++) {
        test_flt(mesh->vertices[i].position[2], (float)(i / 3));
        test_flt(mesh->vertices[i].normal[2], 1.0f);
    }

    ecs_fini(world);
}

void Mesh3_invalid_index_skipped(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    Mesh3 m = {0};
    ecs_vec_init_t(NULL, &m.vertices, vec3, 3);
    ecs_vec_init_t(NULL, &m.normals, vec3, 3);
    ecs_vec_init_t(NULL, &m.indices, uint32_t, 7);
    ecs_vec_set_count_t(NULL, &m.vertices, vec3, 3);
    ecs_vec_set_count_t(NULL, &m.normals, vec3, 3);
    ecs_vec_set_count_t(NULL, &m.indices, uint32_t, 7);
    vec3 *p = ecs_vec_first_t(&m.vertices, vec3);
    vec3 *n = ecs_vec_first_t(&m.normals, vec3);
    for (int i = 0; i < 3; i ++) {
        glm_vec3_copy((vec3){ (float)(i == 1), (float)(i == 2), 0.0f }, p[i]);
        glm_vec3_copy((vec3){ 0.0f, 0.0f, 1.0f }, n[i]);
    }

    const uint32_t tris[7] = { 0, 1, 2, 0, 2, 9, 1 };
    ecs_os_memcpy(ecs_vec_first(&m.indices), tris, sizeof(tris));

    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Mesh3, &m);
    ecs_vec_fini_t(NULL, &m.vertices, vec3);
    ecs_vec_fini_t(NULL, &m.normals, vec3);
    ecs_vec_fini_t(NULL, &m.indices, uint32_t);

    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);
    test_int(mesh->vertex_count, 3);
    test_int(mesh->index_count, 3);

    ecs_fini(world);
}

void Mesh3_colors(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    Mesh3 m = {0};
    ecs_vec_init_t(NULL, &m.vertices, vec3, 3);
    ecs_vec_init_t(NULL, &m.colors, rgba, 3);
    ecs_vec_set_count_t(NULL, &m.vertices, vec3, 3);
    ecs_vec_set_count_t(NULL, &m.colors, rgba, 3);
    vec3 *p = ecs_vec_first_t(&m.vertices, vec3);
    rgba *c = ecs_vec_first_t(&m.colors, rgba);
    for (int i = 0; i < 3; i ++) {
        glm_vec3_copy((vec3){ (float)(i == 1), (float)(i == 2), 0.0f }, p[i]);
        c[i] = (rgba){ (uint8_t)(10 * i), 20, 30, 255 };
    }

    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Mesh3, &m);
    ecs_vec_fini_t(NULL, &m.vertices, vec3);
    ecs_vec_fini_t(NULL, &m.colors, rgba);

    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);
    test_int(mesh->vertex_count, 3);
    test_assert(mesh->colors != NULL);
    for (int i = 0; i < 3; i ++) {
        test_int(mesh->colors[i].r, 10 * i);
        test_int(mesh->colors[i].g, 20);
        test_int(mesh->colors[i].a, 255);
    }

    ecs_fini(world);
}

void Mesh3_cache_shared(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);

    Mesh3 m = {0};
    ecs_vec_init_t(NULL, &m.vertices, vec3, 3);
    ecs_vec_set_count_t(NULL, &m.vertices, vec3, 3);
    vec3 *p = ecs_vec_first_t(&m.vertices, vec3);
    for (int i = 0; i < 3; i ++) {
        glm_vec3_copy((vec3){ (float)(i == 1), (float)(i == 2), 0.0f }, p[i]);
    }

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);
    ecs_entity_t e3 = ecs_new(world);
    ecs_set_ptr(world, e1, Mesh3, &m);
    ecs_set_ptr(world, e2, Mesh3, &m);
    p[2][2] = 1.0f;
    ecs_set_ptr(world, e3, Mesh3, &m);
    ecs_vec_fini_t(NULL, &m.vertices, vec3);

    const ShapeMesh *m1 = ecs_get(world, e1, ShapeMesh);
    const ShapeMesh *m2 = ecs_get(world, e2, ShapeMesh);
    const ShapeMesh *m3 = ecs_get(world, e3, ShapeMesh);
    test_assert(m1 != NULL);
    test_assert(m2 != NULL);
    test_assert(m3 != NULL);
    test_uint(m1->mesh, m2->mesh);
    test_assert(m1->mesh != m3->mesh);

    ecs_fini(world);
}

void Mesh3_update_content(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    Mesh3 m = {0};
    ecs_vec_init_t(NULL, &m.vertices, vec3, 3);
    ecs_vec_set_count_t(NULL, &m.vertices, vec3, 3);
    vec3 *p = ecs_vec_first_t(&m.vertices, vec3);
    for (int i = 0; i < 3; i ++) {
        glm_vec3_copy((vec3){ (float)(i == 1), (float)(i == 2), 0.0f }, p[i]);
    }

    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Mesh3, &m);
    uint32_t first = ecs_get(world, e, ShapeMesh)->mesh;
    test_assert(first != 0);
    test_flt(box_mesh_cache_get(cache, first)->max[2], 0.0f);

    p[2][2] = 2.0f;
    ecs_set_ptr(world, e, Mesh3, &m);
    ecs_vec_fini_t(NULL, &m.vertices, vec3);

    uint32_t second = ecs_get(world, e, ShapeMesh)->mesh;
    test_assert(second != 0);
    test_flt(box_mesh_cache_get(cache, second)->max[2], 2.0f);

    ecs_delete(world, e);
    test_int(box_mesh_cache_count(cache), 0);

    ecs_fini(world);
}

void Mesh3_scale(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Mesh3, {0});
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    test_flt(sm->scale[0], 1.0f);
    test_flt(sm->scale[1], 1.0f);
    test_flt(sm->scale[2], 1.0f);

    ecs_fini(world);
}

void Mesh3_ensure_modified_leak(void) {
    // renderer/mesh3_leak
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);

    ecs_entity_t e = ecs_new(world);
    Mesh3 *m = ecs_ensure(world, e, Mesh3);
    test_assert(m != NULL);
    ecs_vec_init_t(NULL, &m->vertices, vec3, 3);
    ecs_vec_set_count_t(NULL, &m->vertices, vec3, 3);
    vec3 *p = ecs_vec_first_t(&m->vertices, vec3);
    for (int i = 0; i < 3; i ++) {
        glm_vec3_copy((vec3){ (float)(i == 1), (float)(i == 2), 0.0f }, p[i]);
    }

    ecs_modified(world, e, Mesh3);
    test_assert(ecs_get(world, e, ShapeMesh) != NULL);

    ecs_fini(world);
}

void Mesh3_prefab_instances_share(void) {
    // core_fixes/mesh3_inherit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    Mesh3 m = {0};
    ecs_vec_init_t(NULL, &m.vertices, vec3, 4);
    ecs_vec_init_t(NULL, &m.indices, uint32_t, 6);
    ecs_vec_set_count_t(NULL, &m.vertices, vec3, 4);
    ecs_vec_set_count_t(NULL, &m.indices, uint32_t, 6);
    vec3 *p = ecs_vec_first_t(&m.vertices, vec3);
    glm_vec3_copy((vec3){ -1, -1, 0 }, p[0]);
    glm_vec3_copy((vec3){ 1, -1, 0 }, p[1]);
    glm_vec3_copy((vec3){ 1, 1, 0 }, p[2]);
    glm_vec3_copy((vec3){ -1, 1, 0 }, p[3]);
    const uint32_t tris[6] = { 0, 1, 2, 0, 2, 3 };
    ecs_os_memcpy(ecs_vec_first(&m.indices), tris, sizeof(tris));

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set_ptr(world, prefab, Mesh3, &m);
    ecs_vec_fini_t(NULL, &m.vertices, vec3);
    ecs_vec_fini_t(NULL, &m.indices, uint32_t);
    const Mesh3 *shared = ecs_get(world, prefab, Mesh3);
    uint32_t mesh = ecs_get(world, prefab, ShapeMesh)->mesh;
    test_assert(mesh != 0);

    ecs_entity_t *instances = ecs_os_malloc_n(ecs_entity_t, 1000);
    for (int i = 0; i < 1000; i ++) {
        instances[i] = ecs_new_w_pair(world, EcsIsA, prefab);
    }

    for (int i = 0; i < 1000; i ++) {
        test_assert(!ecs_owns(world, instances[i], Mesh3));
        test_assert(ecs_get(world, instances[i], Mesh3) == shared);
        const ShapeMesh *sm = ecs_get(world, instances[i], ShapeMesh);
        test_assert(sm != NULL);
        test_uint(sm->mesh, mesh);
        test_flt(sm->scale[0], 1.0f);
    }

    const box_mesh_t *resolved = box_mesh_cache_get(cache, mesh);
    test_assert(resolved != NULL);
    test_int(resolved->index_count, 6);
    test_flt(resolved->min[0], -1.0f);
    test_flt(resolved->max[1], 1.0f);

    for (int i = 0; i < 1000; i ++) {
        ecs_delete(world, instances[i]);
    }

    test_assert(box_mesh_cache_get(cache, mesh) != NULL);
    ecs_delete(world, prefab);
    test_int(box_mesh_cache_count(cache), 0);

    ecs_os_free(instances);
    ecs_fini(world);
}

void Mesh3_prefab_instance_override(void) {
    // core_fixes/mesh3_inherit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    Mesh3 m = {0};
    ecs_vec_init_t(NULL, &m.vertices, vec3, 3);
    ecs_vec_set_count_t(NULL, &m.vertices, vec3, 3);
    vec3 *p = ecs_vec_first_t(&m.vertices, vec3);
    for (int i = 0; i < 3; i ++) {
        glm_vec3_copy((vec3){ (float)(i == 1), (float)(i == 2), 0.0f }, p[i]);
    }

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set_ptr(world, prefab, Mesh3, &m);
    ecs_entity_t a = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t b = ecs_new_w_pair(world, EcsIsA, prefab);
    uint32_t shared = ecs_get(world, prefab, ShapeMesh)->mesh;

    p[2][2] = 3.0f;
    ecs_set_ptr(world, a, Mesh3, &m);
    ecs_vec_fini_t(NULL, &m.vertices, vec3);
    test_assert(ecs_owns(world, a, Mesh3));
    uint32_t own = ecs_get(world, a, ShapeMesh)->mesh;
    test_assert(own != shared);
    test_flt(box_mesh_cache_get(cache, own)->max[2], 3.0f);
    test_uint(ecs_get(world, b, ShapeMesh)->mesh, shared);
    test_uint(ecs_get(world, prefab, ShapeMesh)->mesh, shared);
    test_flt(box_mesh_cache_get(cache, shared)->max[2], 0.0f);
    test_int(ecs_vec_count(&ecs_get(world, prefab, Mesh3)->vertices), 3);

    ecs_remove(world, a, Mesh3);
    test_assert(!ecs_owns(world, a, Mesh3));
    test_assert(ecs_has(world, a, Mesh3));
    const ShapeMesh *sm = ecs_get(world, a, ShapeMesh);
    test_assert(sm != NULL);
    test_uint(sm->mesh, shared);
    test_assert(box_mesh_cache_get(cache, own) == NULL);

    ecs_delete(world, a);
    ecs_delete(world, b);
    ecs_delete(world, prefab);
    test_int(box_mesh_cache_count(cache), 0);

    ecs_fini(world);
}

void Mesh3_prefab_update(void) {
    // core_fixes/mesh3_inherit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    Mesh3 m = {0};
    ecs_vec_init_t(NULL, &m.vertices, vec3, 3);
    ecs_vec_set_count_t(NULL, &m.vertices, vec3, 3);
    vec3 *p = ecs_vec_first_t(&m.vertices, vec3);
    for (int i = 0; i < 3; i ++) {
        glm_vec3_copy((vec3){ (float)(i == 1), (float)(i == 2), 0.0f }, p[i]);
    }

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set_ptr(world, prefab, Mesh3, &m);
    ecs_entity_t instances[10];
    for (int i = 0; i < 10; i ++) {
        instances[i] = ecs_new_w_pair(world, EcsIsA, prefab);
    }

    p[2][2] = 5.0f;
    ecs_set_ptr(world, instances[0], Mesh3, &m);
    uint32_t own = ecs_get(world, instances[0], ShapeMesh)->mesh;

    p[2][2] = 2.0f;
    ecs_set_ptr(world, prefab, Mesh3, &m);
    ecs_vec_fini_t(NULL, &m.vertices, vec3);
    uint32_t updated = ecs_get(world, prefab, ShapeMesh)->mesh;
    test_flt(box_mesh_cache_get(cache, updated)->max[2], 2.0f);
    test_uint(ecs_get(world, instances[0], ShapeMesh)->mesh, own);
    test_flt(box_mesh_cache_get(cache, own)->max[2], 5.0f);
    for (int i = 1; i < 10; i ++) {
        test_uint(ecs_get(world, instances[i], ShapeMesh)->mesh, updated);
    }

    Mesh3 *edit = ecs_get_mut(world, prefab, Mesh3);
    ecs_vec_get_t(&edit->vertices, vec3, 2)[0][2] = 4.0f;
    ecs_modified(world, prefab, Mesh3);
    uint32_t edited = ecs_get(world, prefab, ShapeMesh)->mesh;
    test_flt(box_mesh_cache_get(cache, edited)->max[2], 4.0f);
    for (int i = 1; i < 10; i ++) {
        test_uint(ecs_get(world, instances[i], ShapeMesh)->mesh, edited);
    }

    test_int(box_mesh_cache_count(cache), 2);
    for (int i = 0; i < 10; i ++) {
        ecs_delete(world, instances[i]);
    }

    ecs_delete(world, prefab);
    test_int(box_mesh_cache_count(cache), 0);

    ecs_fini(world);
}

void Mesh3_prefab_delete(void) {
    // core_fixes/mesh3_inherit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    ECS_TAG(world, Marker);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    Mesh3 m = {0};
    ecs_vec_init_t(NULL, &m.vertices, vec3, 3);
    ecs_vec_set_count_t(NULL, &m.vertices, vec3, 3);
    vec3 *p = ecs_vec_first_t(&m.vertices, vec3);
    for (int i = 0; i < 3; i ++) {
        glm_vec3_copy((vec3){ (float)(i == 1), (float)(i == 2), 0.0f }, p[i]);
    }

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set_ptr(world, prefab, Mesh3, &m);
    ecs_vec_fini_t(NULL, &m.vertices, vec3);
    ecs_entity_t instances[10];
    for (int i = 0; i < 10; i ++) {
        instances[i] = ecs_new_w_pair(world, EcsIsA, prefab);
        ecs_add(world, instances[i], Marker);
    }

    ecs_delete(world, prefab);
    for (int i = 0; i < 10; i ++) {
        test_assert(ecs_is_alive(world, instances[i]));
        test_assert(!ecs_has(world, instances[i], Mesh3));
        test_assert(!ecs_has(world, instances[i], ShapeMesh));
        test_assert(ecs_has(world, instances[i], Marker));
    }

    test_int(box_mesh_cache_count(cache), 0);

    ecs_fini(world);
}

void Mesh3_prefab_remove(void) {
    // core_fixes/mesh3_inherit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    Mesh3 m = {0};
    ecs_vec_init_t(NULL, &m.vertices, vec3, 3);
    ecs_vec_set_count_t(NULL, &m.vertices, vec3, 3);
    vec3 *p = ecs_vec_first_t(&m.vertices, vec3);
    for (int i = 0; i < 3; i ++) {
        glm_vec3_copy((vec3){ (float)(i == 1), (float)(i == 2), 0.0f }, p[i]);
    }

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set_ptr(world, prefab, Mesh3, &m);
    ecs_entity_t instances[10];
    for (int i = 0; i < 10; i ++) {
        instances[i] = ecs_new_w_pair(world, EcsIsA, prefab);
    }

    uint32_t shared = ecs_get(world, instances[1], ShapeMesh)->mesh;
    p[2][2] = 3.0f;
    ecs_set_ptr(world, instances[0], Mesh3, &m);
    uint32_t own = ecs_get(world, instances[0], ShapeMesh)->mesh;

    ecs_remove(world, prefab, Mesh3);
    test_assert(!ecs_has(world, prefab, Mesh3));
    test_assert(ecs_has(world, instances[0], Mesh3));
    test_uint(ecs_get(world, instances[0], ShapeMesh)->mesh, own);
    for (int i = 1; i < 10; i ++) {
        test_assert(!ecs_has(world, instances[i], Mesh3));
        test_assert(!ecs_has(world, instances[i], ShapeMesh));
    }

    test_assert(box_mesh_cache_get(cache, shared) == NULL);

    p[2][2] = 2.0f;
    ecs_set_ptr(world, prefab, Mesh3, &m);
    ecs_vec_fini_t(NULL, &m.vertices, vec3);
    uint32_t again = ecs_get(world, prefab, ShapeMesh)->mesh;
    test_flt(box_mesh_cache_get(cache, again)->max[2], 2.0f);
    test_uint(ecs_get(world, instances[0], ShapeMesh)->mesh, own);
    for (int i = 1; i < 10; i ++) {
        const ShapeMesh *sm = ecs_get(world, instances[i], ShapeMesh);
        test_assert(sm != NULL);
        test_uint(sm->mesh, again);
    }

    for (int i = 0; i < 10; i ++) {
        ecs_delete(world, instances[i]);
    }

    ecs_delete(world, prefab);
    test_int(box_mesh_cache_count(cache), 0);

    ecs_fini(world);
}

/** Make a Mesh3 of one triangle with per vertex data streams.
 *
 * @param m The mesh to fill, freed with Mesh3_fini
 * @param data0 Values of data0, 0 for none
 * @param data1 Values of data1, 0 for none
 * @param normals Whether the triangle has normals, else they are derived per face
 */
static void Mesh3_data_triangle(
    Mesh3 *m,
    int32_t data0,
    int32_t data1,
    bool normals)
{
    *m = (Mesh3){0};
    ecs_vec_init_t(NULL, &m->vertices, vec3, 3);
    ecs_vec_set_count_t(NULL, &m->vertices, vec3, 3);
    vec3 *p = ecs_vec_first_t(&m->vertices, vec3);
    for (int i = 0; i < 3; i ++) {
        glm_vec3_copy((vec3){ (float)(i == 1), (float)(i == 2), 0.0f }, p[i]);
    }

    if (normals) {
        ecs_vec_init_t(NULL, &m->normals, vec3, 3);
        ecs_vec_set_count_t(NULL, &m->normals, vec3, 3);
        vec3 *n = ecs_vec_first_t(&m->normals, vec3);
        for (int i = 0; i < 3; i ++) {
            glm_vec3_copy((vec3){ 0.0f, 0.0f, 1.0f }, n[i]);
        }
    }

    if (data0) {
        ecs_vec_init_t(NULL, &m->data0, vec4, data0);
        ecs_vec_set_count_t(NULL, &m->data0, vec4, data0);
        vec4 *d = ecs_vec_first_t(&m->data0, vec4);
        for (int i = 0; i < data0; i ++) {
            glm_vec4_copy((vec4){ (float)i, 10.0f + (float)i, 20.0f, 30.0f }, d[i]);
        }
    }

    if (data1) {
        ecs_vec_init_t(NULL, &m->data1, vec4, data1);
        ecs_vec_set_count_t(NULL, &m->data1, vec4, data1);
        vec4 *d = ecs_vec_first_t(&m->data1, vec4);
        for (int i = 0; i < data1; i ++) {
            glm_vec4_copy((vec4){ 40.0f, 50.0f, 60.0f, 70.0f + (float)i }, d[i]);
        }
    }
}

/** Free the vectors of a Mesh3 made by Mesh3_data_triangle.
 *
 * @param m The mesh
 */
static void Mesh3_fini(
    Mesh3 *m)
{
    ecs_vec_fini_t(NULL, &m->vertices, vec3);
    ecs_vec_fini_t(NULL, &m->normals, vec3);
    ecs_vec_fini_t(NULL, &m->data0, vec4);
    ecs_vec_fini_t(NULL, &m->data1, vec4);
    ecs_vec_fini_t(NULL, &m->data2, vec4);
    ecs_vec_fini_t(NULL, &m->data3, vec4);
}

/** Get the cached mesh of the Mesh3 of an entity.
 *
 * @param world The world
 * @param e The entity
 * @return The mesh
 */
static const box_mesh_t* Mesh3_cached(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    const box_mesh_t *mesh = box_mesh_cache_get(box_mesh_cache_of(world), sm->mesh);
    test_assert(mesh != NULL);

    return mesh;
}

void Mesh3_data(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    Mesh3 m;
    Mesh3_data_triangle(&m, 3, 3, true);
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Mesh3, &m);
    Mesh3_fini(&m);

    const box_mesh_t *mesh = Mesh3_cached(world, e);
    test_int(mesh->vertex_count, 3);
    test_assert(mesh->data != NULL);
    for (int i = 0; i < 3; i ++) {
        test_flt(mesh->data[i * 2][0], (float)i);
        test_flt(mesh->data[i * 2][1], 10.0f + (float)i);
        test_flt(mesh->data[i * 2][3], 30.0f);
        test_flt(mesh->data[i * 2 + 1][0], 40.0f);
        test_flt(mesh->data[i * 2 + 1][3], 70.0f + (float)i);
    }

    ecs_fini(world);
}

/** The data2 and data3 of a Mesh3 are the upper data of its mesh; a mesh with only upper data
 * has zero data0 and data1 */
void Mesh3_data_upper(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    Mesh3 m;
    Mesh3_data_triangle(&m, 3, 0, true);
    ecs_vec_init_t(NULL, &m.data2, vec4, 3);
    ecs_vec_set_count_t(NULL, &m.data2, vec4, 3);
    ecs_vec_init_t(NULL, &m.data3, vec4, 3);
    ecs_vec_set_count_t(NULL, &m.data3, vec4, 3);
    for (int i = 0; i < 3; i ++) {
        glm_vec4_copy((vec4){ 80.0f + (float)i, 90.0f, 100.0f, 110.0f },
            ecs_vec_get_t(&m.data2, vec4, i)[0]);
        glm_vec4_copy((vec4){ 120.0f, 130.0f, 140.0f, 150.0f + (float)i },
            ecs_vec_get_t(&m.data3, vec4, i)[0]);
    }

    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Mesh3, &m);
    Mesh3_fini(&m);

    const box_mesh_t *mesh = Mesh3_cached(world, e);
    test_int(mesh->vertex_count, 3);
    test_assert(mesh->data != NULL);
    test_assert(mesh->data_upper != NULL);
    for (int i = 0; i < 3; i ++) {
        test_flt(mesh->data[i * 2][0], (float)i);
        test_flt(mesh->data[i * 2 + 1][3], 0.0f);
        test_flt(mesh->data_upper[i * 2][0], 80.0f + (float)i);
        test_flt(mesh->data_upper[i * 2][3], 110.0f);
        test_flt(mesh->data_upper[i * 2 + 1][0], 120.0f);
        test_flt(mesh->data_upper[i * 2 + 1][3], 150.0f + (float)i);
    }

    /* Without data2 and data3 the mesh has no upper data */
    Mesh3_data_triangle(&m, 3, 3, true);
    ecs_set_ptr(world, e, Mesh3, &m);
    Mesh3_fini(&m);
    mesh = Mesh3_cached(world, e);
    test_assert(mesh->data != NULL);
    test_assert(mesh->data_upper == NULL);

    ecs_fini(world);
}

void Mesh3_data_one_stream(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    Mesh3 m;
    Mesh3_data_triangle(&m, 0, 3, true);
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Mesh3, &m);
    Mesh3_fini(&m);

    const box_mesh_t *mesh = Mesh3_cached(world, e);
    test_assert(mesh->data != NULL);
    for (int i = 0; i < 3; i ++) {
        for (int c = 0; c < 4; c ++) {
            test_flt(mesh->data[i * 2][c], 0.0f);
        }

        test_flt(mesh->data[i * 2 + 1][3], 70.0f + (float)i);
    }

    ecs_fini(world);
}

void Mesh3_no_data(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    Mesh3 m;
    Mesh3_data_triangle(&m, 0, 0, true);
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Mesh3, &m);
    Mesh3_fini(&m);

    test_assert(Mesh3_cached(world, e)->data == NULL);
    ecs_fini(world);
}

void Mesh3_data_count_mismatch(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    Mesh3 m;
    Mesh3_data_triangle(&m, 2, 0, true);
    ecs_entity_t e = ecs_new(world);
    int32_t level = ecs_log_set_level(-4);
    ecs_set_ptr(world, e, Mesh3, &m);
    ecs_log_set_level(level);
    Mesh3_fini(&m);

    const box_mesh_t *mesh = Mesh3_cached(world, e);
    test_int(mesh->vertex_count, 3);
    test_assert(mesh->data == NULL);
    ecs_fini(world);
}

void Mesh3_data_derived_normals(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    Mesh3 m;
    Mesh3_data_triangle(&m, 3, 3, false);
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Mesh3, &m);
    Mesh3_fini(&m);

    const box_mesh_t *mesh = Mesh3_cached(world, e);
    test_int(mesh->vertex_count, 3);
    test_assert(mesh->data != NULL);
    for (int i = 0; i < 3; i ++) {
        uint32_t v = mesh->indices[i];
        test_flt(mesh->data[v * 2][0], (float)i);
        test_flt(mesh->data[v * 2 + 1][3], 70.0f + (float)i);
        test_flt(mesh->vertices[v].normal[2], 1.0f);
    }

    ecs_fini(world);
}

void Mesh3_data_cache_key(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    Mesh3 plain, data, other;
    Mesh3_data_triangle(&plain, 0, 0, true);
    Mesh3_data_triangle(&data, 3, 0, true);
    Mesh3_data_triangle(&other, 3, 0, true);
    ecs_vec_first_t(&other.data0, vec4)[1][2] = 99.0f;
    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);
    ecs_entity_t e3 = ecs_new(world);
    ecs_entity_t e4 = ecs_new(world);
    ecs_set_ptr(world, e1, Mesh3, &plain);
    ecs_set_ptr(world, e2, Mesh3, &data);
    ecs_set_ptr(world, e3, Mesh3, &data);
    ecs_set_ptr(world, e4, Mesh3, &other);
    Mesh3_fini(&plain);
    Mesh3_fini(&data);
    Mesh3_fini(&other);

    uint32_t m1 = ecs_get(world, e1, ShapeMesh)->mesh;
    uint32_t m2 = ecs_get(world, e2, ShapeMesh)->mesh;
    uint32_t m3 = ecs_get(world, e3, ShapeMesh)->mesh;
    uint32_t m4 = ecs_get(world, e4, ShapeMesh)->mesh;
    test_assert(m1 != m2);
    test_uint(m2, m3);
    test_assert(m2 != m4);
    ecs_fini(world);
}

void Mesh3_data_prefab_instances_share(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    Mesh3 m;
    Mesh3_data_triangle(&m, 3, 3, true);
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set_ptr(world, prefab, Mesh3, &m);
    Mesh3_fini(&m);
    ecs_entity_t i1 = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t i2 = ecs_new_w_pair(world, EcsIsA, prefab);

    test_uint(ecs_get(world, i1, ShapeMesh)->mesh, ecs_get(world, i2, ShapeMesh)->mesh);
    test_assert(Mesh3_cached(world, i1)->data != NULL);
    test_flt(Mesh3_cached(world, i1)->data[2][1], 11.0f);
    ecs_fini(world);
}
