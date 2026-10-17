/**
 * @file Registry.c
 * @brief Shape registry: a project shape is one registration of key, generator and scale.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "mesh_cache.h"

/** Project shape used to exercise the registry */
typedef struct Pyramid {
    int32_t sides; /**< Sides of the base */
    float size;    /**< Base radius, the height is twice the size */
} Pyramid;

static void PyramidKey(const void *value, box_shape_key_t *key) {
    key->i[0] = ((const Pyramid*)value)->sides;
}

static void PyramidGenerate(
    const void *value, const box_shape_key_t *key, box_mesh_t *mesh)
{
    (void)value;
    int32_t sides = key->i[0];
    for (int32_t i = 0; i < sides; i ++) {
        float a0 = 6.2831853f * (float)i / (float)sides;
        float a1 = 6.2831853f * (float)(i + 1) / (float)sides;
        vec3 p0 = { cosf(a0), 0.0f, sinf(a0) };
        vec3 p1 = { cosf(a1), 0.0f, sinf(a1) };
        vec3 top = { 0.0f, 1.0f, 0.0f };
        vec3 n;
        vec3 e0, e1;
        glm_vec3_sub(p1, p0, e0);
        glm_vec3_sub(top, p0, e1);
        glm_vec3_cross(e0, e1, n);
        glm_vec3_normalize(n);
        if (n[0] * (p0[0] + p1[0]) + n[2] * (p0[2] + p1[2]) < 0.0f) {
            glm_vec3_negate(n);
        }

        int32_t a = box_mesh_vertex(mesh, p0, n, 0.0f, 1.0f);
        int32_t b = box_mesh_vertex(mesh, p1, n, 1.0f, 1.0f);
        int32_t c = box_mesh_vertex(mesh, top, n, 0.5f, 0.0f);
        box_mesh_triangle(mesh, a, b, c);
    }
}

static void PyramidScale(const void *value, vec3 scale) {
    float size = ((const Pyramid*)value)->size;
    scale[0] = size;
    scale[1] = size * 2.0f;
    scale[2] = size;
}

void Registry_register_custom(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    ECS_COMPONENT(world, Pyramid);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    test_int(box_shape_register(world, &(box_shape_desc_t){
        .component = ecs_id(Pyramid),
        .key = PyramidKey,
        .generate = PyramidGenerate,
        .scale = PyramidScale
    }), 0);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);
    ecs_entity_t e3 = ecs_new(world);
    ecs_set(world, e1, Pyramid, { .sides = 4, .size = 1 });
    ecs_set(world, e2, Pyramid, { .sides = 4, .size = 3 });
    ecs_set(world, e3, Pyramid, { .sides = 5, .size = 1 });

    const ShapeMesh *m1 = ecs_get(world, e1, ShapeMesh);
    const ShapeMesh *m2 = ecs_get(world, e2, ShapeMesh);
    const ShapeMesh *m3 = ecs_get(world, e3, ShapeMesh);
    test_assert(m1 != NULL);
    test_assert(m2 != NULL);
    test_assert(m3 != NULL);
    test_uint(m1->mesh, m2->mesh);
    test_assert(m1->mesh != m3->mesh);
    test_flt(m2->scale[0], 3.0f);
    test_flt(m2->scale[1], 6.0f);
    test_flt(m2->scale[2], 3.0f);

    const box_mesh_t *mesh = box_mesh_cache_get(cache, m1->mesh);
    test_assert(mesh != NULL);
    test_int(mesh->vertex_count, 12);
    test_int(mesh->index_count, 12);
    test_flt(mesh->max[1], 1.0f);

    ecs_fini(world);
}

void Registry_register_in_use(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    ECS_COMPONENT(world, Pyramid);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Pyramid, { .sides = 3, .size = 2 });

    ecs_log_set_level(-4);
    test_assert(box_shape_register(world, &(box_shape_desc_t){
        .component = ecs_id(Pyramid),
        .key = PyramidKey,
        .generate = PyramidGenerate,
        .scale = PyramidScale
    }) != 0);
    test_assert(!ecs_has(world, e, ShapeMesh));

    ecs_fini(world);
}

void Registry_register_without_scale(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    ECS_COMPONENT(world, Pyramid);

    test_int(box_shape_register(world, &(box_shape_desc_t){
        .component = ecs_id(Pyramid),
        .key = PyramidKey,
        .generate = PyramidGenerate
    }), 0);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Pyramid, { .sides = 3, .size = 2 });
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    test_flt(sm->scale[0], 1.0f);
    test_flt(sm->scale[1], 1.0f);
    test_flt(sm->scale[2], 1.0f);

    ecs_fini(world);
}

void Registry_register_twice(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    ECS_COMPONENT(world, Pyramid);

    box_shape_desc_t desc = {
        .component = ecs_id(Pyramid),
        .key = PyramidKey,
        .generate = PyramidGenerate
    };
    test_int(box_shape_register(world, &desc), 0);
    ecs_log_set_level(-4);
    test_assert(box_shape_register(world, &desc) != 0);
    test_assert(box_shape_register(world, &(box_shape_desc_t){
        .component = ecs_id(Sphere),
        .key = PyramidKey,
        .generate = PyramidGenerate
    }) != 0);

    ecs_fini(world);
}

void Registry_register_invalid(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    ECS_COMPONENT(world, Pyramid);
    ecs_entity_t tag = ecs_new(world);

    ecs_log_set_level(-4);
    test_assert(box_shape_register(world, &(box_shape_desc_t){
        .component = ecs_id(Pyramid),
        .key = PyramidKey
    }) != 0);
    test_assert(box_shape_register(world, &(box_shape_desc_t){
        .component = ecs_id(Pyramid),
        .generate = PyramidGenerate
    }) != 0);
    test_assert(box_shape_register(world, &(box_shape_desc_t){
        .key = PyramidKey,
        .generate = PyramidGenerate
    }) != 0);
    test_assert(box_shape_register(world, &(box_shape_desc_t){
        .component = tag,
        .key = PyramidKey,
        .generate = PyramidGenerate
    }) != 0);

    ecs_fini(world);
}

void Registry_register_without_module(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_COMPONENT(world, Pyramid);

    ecs_log_set_level(-4);
    test_assert(box_shape_register(world, &(box_shape_desc_t){
        .component = ecs_id(Pyramid),
        .key = PyramidKey,
        .generate = PyramidGenerate
    }) != 0);

    ecs_fini(world);
}

void Registry_star(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_mesh_cache_t *cache = box_mesh_cache_of(world);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Star, { .points = 5, .inner = 0.5f, .radius = 3 });
    const ShapeMesh *sm = ecs_get(world, e, ShapeMesh);
    test_assert(sm != NULL);
    test_flt(sm->scale[0], 3.0f);
    test_flt(sm->scale[1], 3.0f);
    test_flt(sm->scale[2], 1.0f);

    const box_mesh_t *mesh = box_mesh_cache_get(cache, sm->mesh);
    test_assert(mesh != NULL);
    test_assert(mesh->index_count > 0);
    for (int32_t i = 0; i < mesh->vertex_count; i ++) {
        test_flt(mesh->vertices[i].normal[2], 1.0f);
        test_assert(mesh->vertices[i].uv[0] >= 0.0f && mesh->vertices[i].uv[0] <= 1.0f);
        test_assert(mesh->vertices[i].uv[1] >= 0.0f && mesh->vertices[i].uv[1] <= 1.0f);
    }

    for (int32_t t = 0; t < mesh->index_count; t += 3) {
        const box_vertex_t *a = &mesh->vertices[mesh->indices[t]];
        const box_vertex_t *b = &mesh->vertices[mesh->indices[t + 1]];
        const box_vertex_t *c = &mesh->vertices[mesh->indices[t + 2]];
        vec3 ab, ac, face;
        glm_vec3_sub((float*)b->position, (float*)a->position, ab);
        glm_vec3_sub((float*)c->position, (float*)a->position, ac);
        glm_vec3_cross(ab, ac, face);
        test_assert(face[2] > 0.0f);
    }

    ecs_fini(world);
}

void Registry_star_cache_shared(void) {
    // renderer/meshes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);

    ecs_entity_t e1 = ecs_new(world);
    ecs_entity_t e2 = ecs_new(world);
    ecs_entity_t e3 = ecs_new(world);
    ecs_entity_t e4 = ecs_new(world);
    ecs_add(world, e1, Star);
    ecs_get_mut(world, e1, Star)->points = 5;
    ecs_get_mut(world, e1, Star)->radius = 1;
    ecs_modified(world, e1, Star);
    ecs_set(world, e2, Star, { .points = 5, .inner = 0.45f, .radius = 4 });
    ecs_set(world, e3, Star, { .points = 6, .inner = 0.45f, .radius = 1 });
    ecs_set(world, e4, Star, { .points = 5, .inner = 0.3f, .radius = 1 });

    uint32_t m1 = ecs_get(world, e1, ShapeMesh)->mesh;
    test_uint(ecs_get(world, e2, ShapeMesh)->mesh, m1);
    test_assert(ecs_get(world, e3, ShapeMesh)->mesh != m1);
    test_assert(ecs_get(world, e4, ShapeMesh)->mesh != m1);

    ecs_fini(world);
}
