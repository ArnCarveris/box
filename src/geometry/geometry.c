/**
 * @file geometry/geometry.c
 * @brief Registers the shape components of box.geometry and resolves shapes to cached meshes.
 */

#define BOX_GEOMETRY_IMPL
#include "mesh_cache.h"

ECS_COMPONENT_DECLARE(ShapeMesh);

/** Construct ShapeMesh values without a mesh.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti The type info
 */
static void box_shape_mesh_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    ShapeMesh *values = ptr;
    for (int32_t i = 0; i < count; i ++) {
        values[i].mesh = 0;
        glm_vec3_one(values[i].scale);
    }
}

/** Destruct ShapeMesh values, dropping their mesh references.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti The type info
 */
static void box_shape_mesh_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ShapeMesh *values = ptr;
    for (int32_t i = 0; i < count; i ++) {
        box_mesh_cache_release(ti->hooks.ctx, values[i].mesh);
    }
}

/** Copy ShapeMesh values, taking a reference for every copy.
 *
 * @param dst_ptr The destination values
 * @param src_ptr The source values
 * @param count Number of values
 * @param ti The type info
 */
static void box_shape_mesh_copy(
    void *dst_ptr,
    const void *src_ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ShapeMesh *dst = dst_ptr;
    const ShapeMesh *src = src_ptr;
    for (int32_t i = 0; i < count; i ++) {
        box_mesh_cache_keep(ti->hooks.ctx, src[i].mesh);
        box_mesh_cache_release(ti->hooks.ctx, dst[i].mesh);
        dst[i] = src[i];
    }
}

/** Move ShapeMesh values, the source gives up its references.
 *
 * @param dst_ptr The destination values
 * @param src_ptr The source values
 * @param count Number of values
 * @param ti The type info
 */
static void box_shape_mesh_move(
    void *dst_ptr,
    void *src_ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ShapeMesh *dst = dst_ptr;
    ShapeMesh *src = src_ptr;
    for (int32_t i = 0; i < count; i ++) {
        if (&dst[i] != &src[i]) {
            box_mesh_cache_release(ti->hooks.ctx, dst[i].mesh);
            dst[i] = src[i];
            src[i].mesh = 0;
        }
    }
}

/** Resolve set shape values to a cached mesh and an instance scale.
 *
 * ShapeMesh comes with the shape through the With trait, so the value is written in place
 * without moving the entity, which also holds while the entity is being instantiated.
 *
 * @param it The iterator
 */
static void box_shape_on_set(
    ecs_iter_t *it)
{
    const box_shape_t *shape = it->ctx;
    const void *values = ecs_field_w_size(it, (size_t)shape->size, 0);
    bool self = ecs_field_is_self(it, 0);
    int32_t count = it->count;
    ShapeMesh *resolved = ecs_os_malloc_n(ShapeMesh, count);
    ecs_entity_t *entities = ecs_os_memdup_n(it->entities, ecs_entity_t, count);
    for (int32_t i = 0; i < count; i ++) {
        const void *value = ECS_ELEM(values, shape->size, self ? i : 0);
        resolved[i].mesh = box_mesh_cache_acquire(shape->cache, shape, value);
        glm_vec3_one(resolved[i].scale);
        if (shape->desc.scale) {
            shape->desc.scale(value, resolved[i].scale);
        }
    }

    for (int32_t i = 0; i < count; i ++) {
        ShapeMesh *dst = ecs_get_mut(it->world, entities[i], ShapeMesh);
        if (dst) {
            box_mesh_cache_release(shape->cache, dst->mesh);
            *dst = resolved[i];
            ecs_modified(it->stage, entities[i], ShapeMesh);
        } else {
            ecs_set_ptr(it->stage, entities[i], ShapeMesh, &resolved[i]);
            box_mesh_cache_release(shape->cache, resolved[i].mesh);
        }
    }

    ecs_os_free(resolved);
    ecs_os_free(entities);
}

/** Remove the resolved mesh when the shape is removed from a live entity.
 *
 * @param it The iterator
 */
static void box_shape_on_remove(
    ecs_iter_t *it)
{
    if (it->other_table && ecs_table_get_type(it->other_table)->count) {
        for (int32_t i = 0; i < it->count; i ++) {
            ecs_remove(it->stage, it->entities[i], ShapeMesh);
        }
    }
}

int box_shape_register(
    ecs_world_t *world,
    const box_shape_desc_t *desc)
{
    int result = -1;
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    const ecs_type_info_t *ti = NULL;
    box_shape_t *shape = NULL;
    if (!cache || !desc->component || !desc->key || !desc->generate) {
        ecs_err("box_shape_register: invalid shape registration");
        goto done;
    }

    ti = ecs_get_type_info(world, desc->component);
    if (!ti) {
        ecs_err("box_shape_register: shape is not a component");
        goto done;
    }

    if (ecs_id_in_use(world, desc->component)) {
        ecs_err("box_shape_register: shape is already in use, register it before using it");
        goto done;
    }

    shape = box_mesh_cache_add_shape(cache, desc);
    if (!shape) {
        ecs_err("box_shape_register: shape is already registered");
        goto done;
    }

    shape->size = ti->size;
    ecs_add_pair(world, desc->component, EcsWith, ecs_id(ShapeMesh));
    ecs_observer(world, {
        .query.terms = {{ .id = desc->component }},
        .events = { EcsOnSet },
        .callback = box_shape_on_set,
        .ctx = shape
    });
    ecs_observer(world, {
        .query.terms = {{ .id = desc->component }},
        .events = { EcsOnRemove },
        .callback = box_shape_on_remove,
        .ctx = shape
    });
    result = 0;
done:
    return result;
}

void BoxGeometryImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxGeometry);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, Mesh3);
    ECS_META_COMPONENT(world, Box);
    ECS_META_COMPONENT(world, Quad);
    ECS_META_COMPONENT(world, Triangle);
    ECS_META_COMPONENT(world, RightTriangle);
    ECS_META_COMPONENT(world, TrianglePrism);
    ECS_META_COMPONENT(world, RightTrianglePrism);
    ECS_META_COMPONENT(world, NGon);
    ECS_META_COMPONENT(world, Sphere);
    ECS_META_COMPONENT(world, HemiSphere);
    ECS_META_COMPONENT(world, IcoSphere);
    ECS_META_COMPONENT(world, Cone);
    ECS_META_COMPONENT(world, Cylinder);
    ECS_META_COMPONENT(world, Torus);
    ECS_META_COMPONENT(world, Capsule);
    ECS_META_COMPONENT(world, Tube);
    ECS_META_COMPONENT(world, Frustum);
    ECS_META_COMPONENT(world, RoundedBox);
    ECS_META_COMPONENT(world, Star);
    ECS_META_COMPONENT(world, Rect);
    ECS_META_COMPONENT(world, Circle);

    ECS_COMPONENT_DEFINE(world, ShapeMesh);
    ecs_set_hooks(world, ShapeMesh, {
        .ctor = box_shape_mesh_ctor,
        .dtor = box_shape_mesh_dtor,
        .copy = box_shape_mesh_copy,
        .move = box_shape_mesh_move,
        .ctx = box_mesh_cache_new(),
        .ctx_free = box_mesh_cache_free
    });

    box_geometry_shapes_init(world);
    box_geometry_star_init(world);
}
