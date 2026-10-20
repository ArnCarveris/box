/**
 * @file geometry/geometry.c
 * @brief Registers the shape components of box.geometry and resolves shapes to cached meshes.
 */

#define BOX_GEOMETRY_IMPL
#include "shape2d.h"
#include "paths.h"

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
        values[i] = (ShapeMesh){0};
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
        box_shape_mesh_release(ti->hooks.ctx, &values[i]);
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
        box_mesh_cache_keep(ti->hooks.ctx, src[i].render_mesh);
        box_shape_mesh_release(ti->hooks.ctx, &dst[i]);
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
            box_shape_mesh_release(ti->hooks.ctx, &dst[i]);
            dst[i] = src[i];
            src[i].mesh = 0;
            src[i].render_mesh = 0;
        }
    }
}

/** Resolve the shape values of an event to meshes, taking a reference to each.
 *
 * Entities that inherit the shape take the mesh their source resolved. The source's own OnSet
 * runs before the event propagates to its instances, so an inherited shape such as a Mesh3 is
 * keyed once per prefab instead of once per instance.
 *
 * @param it The iterator
 * @param shape The shape
 * @param resolved The resolved values, one per entity
 */
static void box_shape_resolve(
    ecs_iter_t *it,
    const box_shape_t *shape,
    ShapeMesh *resolved)
{
    const void *values = ecs_field_w_size(it, (size_t)shape->size, 0);
    bool self = ecs_field_is_self(it, 0);
    const ShapeMesh *shared = NULL;
    if (!self) {
        shared = ecs_get(it->world, ecs_field_src(it, 0), ShapeMesh);
    }

    for (int32_t i = 0; i < it->count; i ++) {
        if (shared && shared->mesh) {
            box_mesh_cache_keep(shape->cache, shared->mesh);
            box_mesh_cache_keep(shape->cache, shared->render_mesh);
            resolved[i] = *shared;
        } else {
            const void *value = ECS_ELEM(values, shape->size, self ? i : 0);
            resolved[i] = (ShapeMesh){0};
            resolved[i].mesh = box_mesh_cache_acquire(shape->cache, shape, value);
            glm_vec3_one(resolved[i].scale);
            if (shape->desc.surface_world) {
                shape->desc.surface_world(it->world, value, &resolved[i].surface,
                    resolved[i].min, resolved[i].max);
            }
            if (shape->desc.surface) {
                shape->desc.surface(value, &resolved[i].surface,
                    resolved[i].min, resolved[i].max);
                box_shape_key_t key = {0};
                if (shape->desc.key_world) {
                    shape->desc.key_world(it->world, value, &key);
                } else {
                    shape->desc.key(value, &key);
                }
                resolved[i].surface.p[3][3] = (float)key.i[2];
            }
            if (resolved[i].surface.assets) {
                ecs_os_free(resolved[i].surface.assets);
                const box_mesh_t *mesh = box_mesh_cache_get(shape->cache, resolved[i].mesh);
                resolved[i].surface.assets = mesh->surface_assets;
                resolved[i].surface.asset_count = mesh->surface_asset_count;
            }
            if (shape->desc.surface_world) {
                const box_mesh_t *mesh = box_mesh_cache_get(shape->cache, resolved[i].mesh);
                if (!mesh || !mesh->vertex_count || !mesh->index_count) {
                    box_shape_mesh_release(shape->cache, &resolved[i]);
                    resolved[i] = (ShapeMesh){0};
                    glm_vec3_one(resolved[i].scale);
                }
            }
            if (shape->desc.scale) {
                shape->desc.scale(value, resolved[i].scale);
            }
            resolved[i].render_mesh = box_mesh_cache_resolve(shape->cache, &resolved[i]);
        }
    }
}

/** Resolve set shape values to a cached mesh and an instance scale.
 *
 * ShapeMesh comes with the shape through the With trait, so the value is written in place
 * without moving the entity, which also holds while the entity is being instantiated. An
 * inherited value that resolves to the ShapeMesh the instance already has is left alone.
 *
 * @param it The iterator
 */
static void box_shape_on_set(
    ecs_iter_t *it)
{
    const box_shape_t *shape = it->ctx;
    bool self = ecs_field_is_self(it, 0);
    int32_t count = it->count;
    ShapeMesh *resolved = ecs_os_malloc_n(ShapeMesh, count);
    ecs_entity_t *entities = ecs_os_memdup_n(it->entities, ecs_entity_t, count);
    box_shape_resolve(it, shape, resolved);
    for (int32_t i = 0; i < count; i ++) {
        box_surface_materials_apply(it->world, entities[i], &resolved[i]);
        ShapeMesh *dst = ecs_get_mut(it->world, entities[i], ShapeMesh);
        bool same = dst && !self && dst->mesh == resolved[i].mesh &&
            glm_vec3_eqv(dst->scale, resolved[i].scale) &&
            !ecs_os_memcmp_t(&dst->surface, &resolved[i].surface, box_surface_data_t);
        if (same) {
            box_shape_mesh_release(shape->cache, &resolved[i]);
        } else if (dst) {
            box_shape_mesh_release(shape->cache, dst);
            *dst = resolved[i];
            ecs_modified(it->stage, entities[i], ShapeMesh);
        } else {
            ecs_set_ptr(it->stage, entities[i], ShapeMesh, &resolved[i]);
            box_shape_mesh_release(shape->cache, &resolved[i]);
        }
    }

    ecs_os_free(resolved);
    ecs_os_free(entities);
}

/** Drop the resolved mesh of an entity.
 *
 * Flecs forbids adding or removing a copied component such as ShapeMesh on a prefab once it has
 * instances, so a prefab keeps ShapeMesh without a mesh; other entities lose ShapeMesh.
 *
 * @param world The world
 * @param stage The stage for structural changes
 * @param shape The shape
 * @param table The table of the entity
 * @param e The entity
 */
static void box_shape_drop(
    ecs_world_t *world,
    ecs_world_t *stage,
    const box_shape_t *shape,
    const ecs_table_t *table,
    ecs_entity_t e)
{
    ShapeMesh *dst = NULL;
    if (ecs_table_has_id(world, table, EcsPrefab)) {
        dst = ecs_get_mut(world, e, ShapeMesh);
    } else {
        ecs_remove(stage, e, ShapeMesh);
    }

    if (dst) {
        box_shape_mesh_release(shape->cache, dst);
        dst->mesh = 0;
        dst->render_mesh = 0;
        glm_vec3_one(dst->scale);
    }
}

/** Drop the resolved mesh of the instances that inherit a shape their base no longer has.
 *
 * Flecs reports the removal to these instances without a destination table, the same way it
 * reports the deletion of an instance, so the base walks its instances instead.
 *
 * @param world The world
 * @param stage The stage for structural changes
 * @param shape The shape
 * @param base The base that lost the shape
 */
static void box_shape_drop_instances(
    ecs_world_t *world,
    ecs_world_t *stage,
    const box_shape_t *shape,
    ecs_entity_t base)
{
    ecs_iter_t it = ecs_each_id(world, ecs_pair(EcsIsA, base));
    while (ecs_each_next(&it)) {
        bool owned = ecs_table_has_id(world, it.table, shape->desc.component);
        for (int32_t i = 0; !owned && i < it.count; i ++) {
            box_shape_drop(world, stage, shape, it.table, it.entities[i]);
            box_shape_drop_instances(world, stage, shape, it.entities[i]);
        }
    }
}

/** Remove the resolved mesh when a live entity loses the shape.
 *
 * Only an inherited shape (Mesh3) stays reachable through IsA: an instance that removes its own
 * value keeps the prefab's, which the OnSet flecs then emits resolves. A copied shape is gone.
 *
 * An entity that loses an inherited shape also drops it from the instances that inherit it.
 *
 * @param it The iterator
 */
static void box_shape_on_remove(
    ecs_iter_t *it)
{
    const box_shape_t *shape = it->ctx;
    ecs_entity_t component = shape->desc.component;
    bool live = it->other_table && ecs_table_get_type(it->other_table)->count;
    bool inherited = ecs_has_pair(it->world, component, EcsOnInstantiate, EcsInherit);
    bool kept = live && inherited && ecs_search_relation(it->world, it->other_table, 0,
        component, EcsIsA, EcsSelf | EcsUp, NULL, NULL, NULL) != -1;
    bool self = ecs_field_is_self(it, 0);
    for (int32_t i = 0; live && !kept && i < it->count; i ++) {
        box_shape_drop(it->world, it->stage, shape, it->table, it->entities[i]);
        if (inherited && self) {
            box_shape_drop_instances(it->world, it->stage, shape, it->entities[i]);
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
    if (!cache || !desc || !desc->component || (!desc->key && !desc->key_world) ||
        (!desc->generate && !desc->generate_world))
    {
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
    shape->world = world;
    ecs_add_pair(world, desc->component, EcsWith, ecs_id(ShapeMesh));
    ecs_observer(world, {
        .query.terms = {{ .id = desc->component }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnSet },
        .callback = box_shape_on_set,
        .ctx = shape
    });
    ecs_observer(world, {
        .query.terms = {{ .id = desc->component }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
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
    ecs_add_pair(world, ecs_id(Mesh3), EcsOnInstantiate, EcsInherit);
    box_owned_hooks(world, Mesh3,
        BOX_OWNED_VEC(Mesh3, vertices, vec3),
        BOX_OWNED_VEC(Mesh3, normals, vec3),
        BOX_OWNED_VEC(Mesh3, uvs, vec2),
        BOX_OWNED_VEC(Mesh3, indices, uint32_t),
        BOX_OWNED_VEC(Mesh3, colors, rgba));
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
    ECS_META_COMPONENT(world, SurfaceRegion);
    ECS_COMPONENT_DEFINE(world, PathSegmentKind);
    ecs_enum(world, {.entity = ecs_id(PathSegmentKind), .constants = {
        {.name = "Line", .value = PathLine}, {.name = "CubicBezier", .value = PathCubicBezier}}});
    ECS_META_COMPONENT(world, PathSegment2D);
    ECS_META_COMPONENT(world, PathSegment3D);
    ECS_META_COMPONENT(world, Path2D);
    ECS_META_COMPONENT(world, Path3D);
    ECS_META_COMPONENT(world, Sweep);
    ECS_META_COMPONENT(world, LoftSection);
    ECS_META_COMPONENT(world, Loft);
    ECS_META_COMPONENT(world, Lathe);
    ECS_META_COMPONENT(world, EllipsoidShell);
    ECS_META_COMPONENT(world, TorusShell);
    ECS_META_COMPONENT(world, BeveledPrism);
    ECS_META_COMPONENT(world, Profile2D);
    ECS_META_COMPONENT(world, ProfileExtrusion);
    ecs_entity_t surfaces[] = {ecs_id(Sweep), ecs_id(Loft), ecs_id(Lathe),
        ecs_id(EllipsoidShell), ecs_id(TorusShell), ecs_id(BeveledPrism), ecs_id(ProfileExtrusion)};
    for (size_t i = 0; i < sizeof(surfaces) / sizeof(surfaces[0]); i ++) {
        ecs_add_pair(world, surfaces[i], EcsOnInstantiate, EcsInherit);
    }

    ECS_META_COMPONENT(world, SurfaceAttachment);
    ECS_META_COMPONENT(world, SurfaceMaterial);
    ECS_META_COMPONENT(world, SurfaceMaterials);
    ECS_META_COMPONENT(world, Rect);
    ECS_META_COMPONENT(world, Circle);
    ECS_META_COMPONENT(world, Line);
    ECS_META_COMPONENT(world, Border);
    ECS_META_COMPONENT(world, Image);

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
    box_surface_materials_init(world);
    box_geometry_paths_init(world);
    box_geometry_surfaces_init(world);
    box_geometry_profiles_init(world);
    box_geometry_attachments_init(world);
    box_geometry_star_init(world);
    box_geometry_shapes2d_init(world);
    box_geometry_validation_init(world);
}
