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

/** Resolve one shape value to a mesh and an instance scale, taking a reference to the mesh.
 *
 * @param world The world
 * @param shape The shape
 * @param value The shape value
 * @param resolved The resolved value
 */
static void box_shape_resolve_value(
    const ecs_world_t *world,
    const box_shape_t *shape,
    const void *value,
    ShapeMesh *resolved)
{
    *resolved = (ShapeMesh){0};
    resolved->mesh = box_mesh_cache_acquire(shape->cache, shape, value);
    glm_vec3_one(resolved->scale);
    if (shape->desc.surface_world) {
        shape->desc.surface_world(world, value, &resolved->surface,
            resolved->min, resolved->max);
    }

    if (shape->desc.surface) {
        shape->desc.surface(value, &resolved->surface, resolved->min, resolved->max);
        box_shape_key_t key = {0};
        if (shape->desc.key_world) {
            shape->desc.key_world(world, value, &key);
        } else {
            shape->desc.key(value, &key);
        }

        resolved->surface.p[3][3] = (float)key.i[2];
    }

    if (resolved->surface.assets) {
        ecs_os_free(resolved->surface.assets);
        const box_mesh_t *mesh = box_mesh_cache_get(shape->cache, resolved->mesh);
        resolved->surface.assets = mesh->surface_assets;
        resolved->surface.asset_count = mesh->surface_asset_count;
    }

    if (shape->desc.surface_world) {
        const box_mesh_t *mesh = box_mesh_cache_get(shape->cache, resolved->mesh);
        if (!mesh || !mesh->vertex_count || !mesh->index_count) {
            box_shape_mesh_release(shape->cache, resolved);
            *resolved = (ShapeMesh){0};
            glm_vec3_one(resolved->scale);
        }
    }

    if (shape->desc.scale) {
        shape->desc.scale(value, resolved->scale);
    }

    resolved->render_mesh = box_mesh_cache_resolve(shape->cache, resolved);
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
            box_shape_resolve_value(it->world, shape, value, &resolved[i]);
        }
    }
}

/** Write a resolved shape to the ShapeMesh of an entity, which takes over its reference.
 *
 * ShapeMesh comes with the shape through the With trait, so the value is written in place
 * without moving the entity, which also holds while the entity is being instantiated. An
 * inherited value that resolves to the ShapeMesh the instance already has is left alone.
 *
 * @param world The world
 * @param stage The stage for structural changes
 * @param shape The shape
 * @param e The entity
 * @param resolved The resolved value
 * @param inherited True when the shape value is inherited from a base
 */
static void box_shape_write(
    ecs_world_t *world,
    ecs_world_t *stage,
    const box_shape_t *shape,
    ecs_entity_t e,
    ShapeMesh *resolved,
    bool inherited)
{
    box_surface_materials_apply(world, e, resolved);
    ShapeMesh *dst = ecs_get_mut(world, e, ShapeMesh);
    bool same = dst && inherited && dst->mesh == resolved->mesh &&
        glm_vec3_eqv(dst->scale, resolved->scale) &&
        !ecs_os_memcmp_t(&dst->surface, &resolved->surface, box_surface_data_t);
    if (same) {
        box_shape_mesh_release(shape->cache, resolved);
    } else if (dst) {
        box_shape_mesh_release(shape->cache, dst);
        *dst = *resolved;
        ecs_modified(stage, e, ShapeMesh);
    } else {
        ecs_set_ptr(stage, e, ShapeMesh, resolved);
        box_shape_mesh_release(shape->cache, resolved);
    }
}

void box_shape_apply(
    ecs_world_t *world,
    ecs_world_t *stage,
    const box_shape_t *shape,
    ecs_entity_t e,
    const void *value)
{
    ShapeMesh resolved;
    box_shape_resolve_value(world, shape, value, &resolved);
    box_shape_write(world, stage, shape, e, &resolved, false);
}

/** Resolve set shape values to a cached mesh and an instance scale.
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
        box_shape_write(it->world, it->stage, shape, entities[i], &resolved[i], !self);
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
    ecs_observer_desc_t on_set = {
        .query.terms = {{ .id = desc->component }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnSet },
        .callback = box_shape_on_set,
        .ctx = shape
    };
    if (desc->exclude) {
        on_set.query.terms[1] = (ecs_term_t){ .id = desc->exclude, .oper = EcsNot };
    }

    ecs_observer_init(world, &on_set);
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

/** Resolve an entity with a Box and a Bevel: the bevel mesh, or the plain Box mesh when the
 * Bevel rounds no edge so that the entity shares the batches of plain boxes.
 *
 * @param world The world
 * @param stage The stage for structural changes
 * @param bevel The cached shape of the Bevel
 * @param e The entity
 * @param value The Box and the Bevel of the entity
 */
static void box_bevel_resolve(
    ecs_world_t *world,
    ecs_world_t *stage,
    const box_shape_t *bevel,
    ecs_entity_t e,
    const box_bevel_value_t *value)
{
    if (box_bevel_flat(&value->bevel)) {
        const box_shape_t *box = box_mesh_cache_shape(bevel->cache, ecs_id(Box));
        box_shape_apply(world, stage, box, e, &value->box);
    } else {
        box_shape_apply(world, stage, bevel, e, value);
    }
}

/** Resolve the entities with a Box and a Bevel whose Box or Bevel was set.
 *
 * The observer of Box skips entities with a Bevel, so this one owns their ShapeMesh.
 *
 * @param it The iterator
 */
static void box_bevel_on_set(
    ecs_iter_t *it)
{
    const box_shape_t *bevel = it->ctx;
    const Box *boxes = ecs_field(it, Box, 0);
    const Bevel *bevels = ecs_field(it, Bevel, 1);
    bool box_self = ecs_field_is_self(it, 0);
    bool bevel_self = ecs_field_is_self(it, 1);
    int32_t count = it->count;
    box_bevel_value_t *values = ecs_os_malloc_n(box_bevel_value_t, count);
    ecs_entity_t *entities = ecs_os_memdup_n(it->entities, ecs_entity_t, count);
    for (int32_t i = 0; i < count; i ++) {
        values[i].box = boxes[box_self ? i : 0];
        values[i].bevel = bevels[bevel_self ? i : 0];
    }

    for (int32_t i = 0; i < count; i ++) {
        box_bevel_resolve(it->world, it->stage, bevel, entities[i], &values[i]);
    }

    ecs_os_free(values);
    ecs_os_free(entities);
}

/** Resolve an entity that loses its Bevel and keeps its Box back to the plain Box.
 *
 * An entity that is deleted resolves nothing, and one that also loses its Box drops its
 * ShapeMesh in the remove observer of the Box.
 *
 * @param it The iterator
 */
static void box_bevel_on_remove(
    ecs_iter_t *it)
{
    const box_shape_t *bevel = it->ctx;
    const box_shape_t *box = box_mesh_cache_shape(bevel->cache, ecs_id(Box));
    bool live = it->other_table && ecs_table_get_type(it->other_table)->count &&
        ecs_table_has_id(it->world, it->other_table, ecs_id(Box));
    for (int32_t i = 0; live && i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        const Box *value = ecs_get(it->world, e, Box);
        if (value) {
            Box copy = *value;
            box_shape_apply(it->world, it->stage, box, e, &copy);
        }
    }
}

void box_geometry_bevel_init(
    ecs_world_t *world,
    box_shape_t *bevel)
{
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Box) }, { .id = ecs_id(Bevel) }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnSet },
        .callback = box_bevel_on_set,
        .ctx = bevel
    });
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Bevel) }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnRemove },
        .callback = box_bevel_on_remove,
        .ctx = bevel
    });
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
    ECS_META_COMPONENT(world, Bevel);
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
    ECS_META_COMPONENT(world, BoxShadow);
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
