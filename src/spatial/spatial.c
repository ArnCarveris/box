/**
 * @file spatial/spatial.c
 * @brief Registers box.spatial and keeps the index of every scene up to date.
 *
 * Observers collect changed shapes and the roots of changed subtrees, so an update only
 * computes the bounds of what changed. Moved static leaves and DynamicTransform shapes are
 * placed by the first query after a frame, so a frame without queries does not move leaves.
 *
 * A dynamic shape keeps the world transform of its bounds in SpatialPose, and only the shapes
 * whose transform differs from it get new bounds.
 */

#include "index.h"
#include "../render/render.h"
#include "../geometry/mesh_cache.h"

ECS_COMPONENT_DECLARE(SpatialProxy);
ECS_COMPONENT_DECLARE(SpatialPose);
ECS_COMPONENT_DECLARE(SpatialIndex);

/** World transform of a shape without one */
static const WorldTransform box_spatial_identity = { .m = GLM_MAT4_IDENTITY_INIT };

/** Construct SpatialProxy values that are not indexed.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_spatial_proxy_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    SpatialProxy *proxies = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        proxies[i] = (SpatialProxy){ .item = -1 };
    }
}

/** Copy SpatialProxy values: a copy is a new entity that is not indexed yet.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_spatial_proxy_copy(
    void *dst,
    const void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)src;
    box_spatial_proxy_ctor(dst, count, ti);
}

/** Move SpatialProxy values with the entity they belong to.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_spatial_proxy_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ecs_os_memcpy(dst, src, ti->size * count);
}

/** Get the index of a world without placing the dynamic entities.
 *
 * @param world The world
 * @return The index, NULL when box.spatial was not imported
 */
static box_spatial_t* box_spatial_get(
    const ecs_world_t *world)
{
    const SpatialIndex *s = ecs_singleton_get(world, SpatialIndex);

    return s ? s->index : NULL;
}

/** Drop a reference to the index, freeing it with the last one.
 *
 * @param ctx The index
 */
static void box_spatial_release(
    void *ctx)
{
    box_spatial_t *index = ctx;
    if (!-- index->refs) {
        box_spatial_free(index);
    }
}

/** Add an entity with a proxy to the changed entities unless it is in the list.
 *
 * @param world The world
 * @param index The index
 * @param e The entity
 * @param rescope The scene of the entity may have changed
 */
static void box_spatial_mark(
    ecs_world_t *world,
    box_spatial_t *index,
    ecs_entity_t e,
    bool rescope)
{
    SpatialProxy *p = ecs_get_mut(world, e, SpatialProxy);
    if (p) {
        p->rescope |= rescope;
    }

    if (p && !p->dirty) {
        p->dirty = true;
        *ecs_vec_append_t(NULL, &index->dirty, ecs_entity_t) = e;
    }
}

/** Take an entity out of the index.
 *
 * @param index The index
 * @param p The proxy of the entity
 */
static void box_spatial_unindex(
    box_spatial_t *index,
    SpatialProxy *p)
{
    box_spatial_scene_t *s = p->item >= 0 ? box_spatial_scene(index, p->scene) : NULL;
    if (s) {
        box_spatial_item_remove(index, s, p->item);
    }

    p->item = -1;
    p->scene = 0;
}

/** Mark the entities of an observer event as changed.
 *
 * @param it The iterator of the observer
 */
static void BoxSpatialOnChange(
    ecs_iter_t *it)
{
    int32_t i;
    if (!ecs_is_fini(it->world)) {
        for (i = 0; i < it->count; i ++) {
            box_spatial_mark(it->world, it->ctx, it->entities[i], false);
        }
    }
}

/** Mark the static entities whose WorldTransform was set; the descendants of a ManualTransform
 * entity arrive through box_transform_on_moved once their static transform is computed.
 *
 * @param it The iterator of the observer
 */
static void BoxSpatialOnWorld(
    ecs_iter_t *it)
{
    if (!box_transform_dynamic(it->world, it->table)) {
        BoxSpatialOnChange(it);
    }
}

/** Mark the entities whose static transform was computed.
 *
 * @param world The world
 * @param entities The entities
 * @param count Number of entities
 * @param ctx The index
 */
static void box_spatial_on_moved(
    ecs_world_t *world,
    const ecs_entity_t *entities,
    int32_t count,
    void *ctx)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        box_spatial_mark(world, ctx, entities[i], false);
    }
}

/** Remember the entities of an observer event as roots of subtrees that may change scene.
 *
 * @param it The iterator of the observer
 */
static void BoxSpatialOnRescope(
    ecs_iter_t *it)
{
    box_spatial_t *index = it->ctx;
    int32_t i;
    if (!ecs_is_fini(it->world)) {
        for (i = 0; i < it->count; i ++) {
            *ecs_vec_append_t(NULL, &index->rescoped, ecs_entity_t) = it->entities[i];
        }
    }
}

/** Take entities that lose their shape out of the index.
 *
 * @param it The iterator of the observer
 */
static void BoxSpatialOnRemove(
    ecs_iter_t *it)
{
    int32_t i;
    if (!ecs_is_fini(it->world)) {
        for (i = 0; i < it->count; i ++) {
            SpatialProxy *p = ecs_get_mut(it->world, it->entities[i], SpatialProxy);
            if (p) {
                box_spatial_unindex(it->ctx, p);
            }
        }
    }
}

/** Take entities that lose their shape out of the index, and mark the dynamic ones that stay
 * so the next update drops their SpatialPose.
 *
 * @param it The iterator of the observer
 */
static void BoxSpatialOnShapeRemove(
    ecs_iter_t *it)
{
    int32_t i;
    BoxSpatialOnRemove(it);
    bool stays = it->other_table && ecs_table_get_type(it->other_table)->count;
    if (!ecs_is_fini(it->world) && stays &&
        ecs_table_has_id(it->world, it->table, ecs_id(SpatialPose)))
    {
        for (i = 0; i < it->count; i ++) {
            box_spatial_mark(it->world, it->ctx, it->entities[i], false);
        }
    }
}

/** Get the unit mesh of a shape.
 *
 * @param cache The mesh cache
 * @param shape The shape
 * @return The mesh, NULL when the shape has no mesh with vertices
 */
static const box_mesh_t* box_spatial_mesh(
    const box_mesh_cache_t *cache,
    const ShapeMesh *shape)
{
    const box_mesh_t *result = cache && shape && shape->mesh ?
        box_mesh_cache_get(cache, shape->mesh) : NULL;

    return result && result->vertex_count ? result : NULL;
}

/** Compute the world bounds of a shape from its unit mesh, instance scale and transform.
 *
 * @param mesh The unit mesh of the shape
 * @param shape The shape
 * @param t World transform of the entity, NULL for the identity
 * @param min Receives the minimum corner
 * @param max Receives the maximum corner
 */
static void box_spatial_world_bounds(
    const box_mesh_t *mesh,
    const ShapeMesh *shape,
    const WorldTransform *t,
    vec3 min,
    vec3 max)
{
    const WorldTransform *m = t ? t : &box_spatial_identity;
    vec3 c, e;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        c[i] = (mesh->min[i] + mesh->max[i]) * 0.5f * shape->scale[i];
        e[i] = (mesh->max[i] - mesh->min[i]) * 0.5f * fabsf(shape->scale[i]);
    }

    for (i = 0; i < 3; i ++) {
        float wc = m->m[3][i] + m->m[0][i] * c[0] + m->m[1][i] * c[1] + m->m[2][i] * c[2];
        float we = fabsf(m->m[0][i]) * e[0] + fabsf(m->m[1][i]) * e[1] +
            fabsf(m->m[2][i]) * e[2];
        min[i] = wc - we;
        max[i] = wc + we;
    }
}

/** Keep the world transform of a dynamic shape with it, or drop it from a shape that is no
 * longer an indexed dynamic shape.
 *
 * @param world The world
 * @param e The entity
 * @param dynamic The entity is an indexed dynamic shape
 * @param t World transform its bounds were computed from, NULL for the identity
 */
static void box_spatial_pose(
    ecs_world_t *world,
    ecs_entity_t e,
    bool dynamic,
    const WorldTransform *t)
{
    if (dynamic) {
        SpatialPose pose;
        ecs_os_memcpy(pose.m, (t ? t : &box_spatial_identity)->m, sizeof(mat4));
        ecs_set_ptr(world, e, SpatialPose, &pose);
    } else if (ecs_has(world, e, SpatialPose)) {
        ecs_remove(world, e, SpatialPose);
    }
}

/** Place a changed entity in the index of its scene, or take it out.
 *
 * @param world The world
 * @param index The index
 * @param cache The mesh cache
 * @param e The entity
 */
static void box_spatial_refresh(
    ecs_world_t *world,
    box_spatial_t *index,
    const box_mesh_cache_t *cache,
    ecs_entity_t e)
{
    vec3 min, max;
    ecs_entity_t scene = 0;
    bool dynamic = false;
    SpatialProxy *p = ecs_is_alive(world, e) ? ecs_get_mut(world, e, SpatialProxy) : NULL;
    if (!p) {
        goto done;
    }

    bool rescope = p->rescope || p->item < 0;
    p->dirty = false;
    p->rescope = false;
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    const box_mesh_t *mesh = box_spatial_mesh(cache, shape);
    bool indexed = mesh && !ecs_has_id(world, e, EcsPrefab);
    if (indexed) {
        box_spatial_world_bounds(mesh, shape, t, min, max);
        scene = rescope ? box_render_scene_of(world, e) : p->scene;
        dynamic = ecs_has_id(world, e, DynamicTransform);
    }

    if (p->item >= 0 && (!indexed || scene != p->scene)) {
        box_spatial_unindex(index, p);
    }

    int32_t tree = dynamic ? BOX_SPATIAL_DYNAMIC : BOX_SPATIAL_STATIC;
    if (indexed && p->item < 0) {
        box_spatial_scene_t *s = box_spatial_scene_ensure(index, scene);
        p->scene = scene;
        p->item = box_spatial_item_add(s, e, tree, min, max);
    } else if (indexed) {
        box_spatial_item_set(box_spatial_scene(index, scene), p->item, tree, min, max);
    }

    index->stats.updated += indexed;
    box_spatial_pose(world, e, dynamic, t);
done:
    return;
}

/** Mark every entity of changed subtrees, the roots included.
 *
 * @param world The world
 * @param index The index
 * @param roots The roots of the subtrees
 * @param rescope The scene of the subtrees may have changed
 */
static void box_spatial_walk(
    ecs_world_t *world,
    box_spatial_t *index,
    ecs_vec_t *roots,
    bool rescope)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(roots); i ++) {
        ecs_entity_t root = *ecs_vec_get_t(roots, ecs_entity_t, i);
        if (!ecs_is_alive(world, root)) {
            continue;
        }

        *ecs_vec_append_t(NULL, &index->walk, ecs_entity_t) = root;
        while (ecs_vec_count(&index->walk)) {
            ecs_entity_t e = *ecs_vec_last_t(&index->walk, ecs_entity_t);
            ecs_vec_remove_last(&index->walk);
            box_spatial_mark(world, index, e, rescope);
            ecs_iter_t it = ecs_children(world, e);
            while (ecs_children_next(&it)) {
                int32_t c;
                for (c = 0; c < it.count; c ++) {
                    *ecs_vec_append_t(NULL, &index->walk, ecs_entity_t) = it.entities[c];
                }
            }
        }
    }

    ecs_vec_clear(roots);
}

/** Give the dynamic entities whose world transform changed the bounds of this frame.
 *
 * @param world The world
 * @param index The index
 * @param cache The mesh cache
 */
static void box_spatial_dynamic(
    const ecs_world_t *world,
    box_spatial_t *index,
    const box_mesh_cache_t *cache)
{
    ecs_iter_t it = ecs_query_iter(world, index->dynamic);
    box_spatial_scene_t *s = NULL;
    const box_mesh_t *mesh = NULL;
    ecs_entity_t scene = 0;
    uint32_t handle = 0;
    while (ecs_query_next(&it)) {
        const SpatialProxy *p = ecs_field(&it, SpatialProxy, 0);
        SpatialPose *pose = ecs_field(&it, SpatialPose, 1);
        const ShapeMesh *shape = ecs_field(&it, ShapeMesh, 2);
        const WorldTransform *t = ecs_field(&it, WorldTransform, 3);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            vec3 min, max;
            const WorldTransform *m = t ? &t[i] : &box_spatial_identity;
            if (p[i].item < 0 || p[i].dirty || !ecs_os_memcmp(pose[i].m, m->m, sizeof(mat4))) {
                continue;
            }

            ecs_os_memcpy(pose[i].m, m->m, sizeof(mat4));
            index->stats.rebound ++;
            if (!s || scene != p[i].scene) {
                scene = p[i].scene;
                s = box_spatial_scene(index, scene);
            }

            if (!mesh || handle != shape[i].mesh) {
                handle = shape[i].mesh;
                mesh = box_spatial_mesh(cache, &shape[i]);
            }

            const box_spatial_item_t *item = s ? box_spatial_item(s, p[i].item) : NULL;
            if (!mesh || !item || item->tree != BOX_SPATIAL_DYNAMIC) {
                continue;
            }

            box_spatial_world_bounds(mesh, &shape[i], m, min, max);
            if (glm_vec3_eqv(min, ECS_CONST_CAST(float*, item->min)) &&
                glm_vec3_eqv(max, ECS_CONST_CAST(float*, item->max)))
            {
                continue;
            }

            index->stats.moved += box_spatial_item_set(
                s, p[i].item, BOX_SPATIAL_DYNAMIC, min, max);
        }
    }
}

/** Move the static leaves of the last update and place the dynamic entities, once per update.
 *
 * @param world The world
 * @param index The index
 */
static void box_spatial_sync(
    const ecs_world_t *world,
    box_spatial_t *index)
{
    if (index->stale) {
        index->stale = false;
        box_spatial_move(index);
        box_spatial_dynamic(world, index, box_mesh_cache_of(world));
    }
}

box_spatial_t* box_spatial_of(
    const ecs_world_t *world)
{
    box_spatial_t *index = box_spatial_get(world);
    if (index) {
        box_spatial_sync(world, index);
    }

    return index;
}

/** Compute the bounds of the changed entities, commit the new static ones and mark the
 * leaves stale.
 *
 * @param world The world
 * @param index The index
 */
static void box_spatial_process(
    ecs_world_t *world,
    box_spatial_t *index)
{
    const box_mesh_cache_t *cache = box_mesh_cache_of(world);
    int32_t i;
    index->stats.updated = 0;
    index->stats.rebound = 0;
    index->stats.moved = 0;
    box_spatial_walk(world, index, &index->rescoped, true);
    for (i = 0; i < ecs_vec_count(&index->dirty); i ++) {
        box_spatial_refresh(world, index, cache, *ecs_vec_get_t(&index->dirty, ecs_entity_t, i));
    }

    ecs_vec_clear(&index->dirty);
    index->stale = true;
    box_spatial_commit(index);
}

/** Update the index once the transforms of the frame are known.
 *
 * @param it The iterator of the system
 */
static void BoxSpatialUpdate(
    ecs_iter_t *it)
{
    box_spatial_process(it->stage, it->ctx);
}

void box_spatial_update(
    ecs_world_t *world)
{
    box_spatial_t *index = box_spatial_get(world);
    if (index) {
        box_spatial_process(world, index);
        box_spatial_sync(world, index);
    }
}

void box_spatial_rebuild(
    ecs_world_t *world)
{
    box_spatial_t *index = box_spatial_get(world);
    if (!index) {
        goto done;
    }

    box_spatial_process(world, index);
    box_spatial_sync(world, index);
    box_spatial_scene_rebuild(index, index->root);
    ecs_map_iter_t it = ecs_map_iter(&index->scenes);
    while (ecs_map_next(&it)) {
        box_spatial_scene_rebuild(index, ecs_map_ptr(&it));
    }

done:
    return;
}

void box_spatial_stats(
    const ecs_world_t *world,
    box_spatial_stats_t *stats)
{
    const box_spatial_t *index = box_spatial_get(world);
    ecs_os_memset_t(stats, 0, box_spatial_stats_t);
    if (!index) {
        goto done;
    }

    *stats = index->stats;
    stats->scenes = index->root->count ? 1 : 0;
    stats->entities = index->root->count;
    ecs_map_iter_t it = ecs_map_iter(&index->scenes);
    while (ecs_map_next(&it)) {
        const box_spatial_scene_t *s = ecs_map_ptr(&it);
        stats->scenes += s->count ? 1 : 0;
        stats->entities += s->count;
    }

done:
    return;
}

/** Register an observer of the index.
 *
 * @param world The world
 * @param index The index
 * @param name Name of the observer
 * @param desc Terms and events of the observer
 * @param callback What the observer does
 */
static void box_spatial_observer(
    ecs_world_t *world,
    box_spatial_t *index,
    const char *name,
    ecs_observer_desc_t desc,
    ecs_iter_action_t callback)
{
    desc.entity = ecs_entity(world, { .name = name });
    desc.callback = callback;
    desc.ctx = index;
    desc.ctx_free = box_spatial_release;
    index->refs ++;
    ecs_observer_init(world, &desc);
}

/** Follow static transforms: the entities whose WorldTransform was set, and the entities whose
 * static transform was computed through box_transform_on_moved.
 *
 * @param world The world
 * @param index The index
 */
static void box_spatial_transform_observers(
    ecs_world_t *world,
    box_spatial_t *index)
{
    box_spatial_observer(world, index, "SpatialOnWorldTransform", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(WorldTransform) }},
        .events = { EcsOnSet } }, BoxSpatialOnWorld);
    index->refs ++;
    box_transform_on_moved(world, box_spatial_on_moved, index, box_spatial_release);
}

/** Register the observers that collect changed entities and changed subtrees.
 *
 * @param world The world
 * @param index The index
 */
static void box_spatial_observers(
    ecs_world_t *world,
    box_spatial_t *index)
{
    box_spatial_observer(world, index, "SpatialOnShape", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(ShapeMesh) }},
        .events = { EcsOnSet } }, BoxSpatialOnChange);
    box_spatial_observer(world, index, "SpatialOnShapeRemove", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(ShapeMesh) }},
        .events = { EcsOnRemove } }, BoxSpatialOnShapeRemove);
    box_spatial_observer(world, index, "SpatialOnProxyRemove", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(SpatialProxy) }},
        .events = { EcsOnRemove } }, BoxSpatialOnRemove);
    box_spatial_observer(world, index, "SpatialOnDynamic", (ecs_observer_desc_t){
        .query.terms = {{ .id = DynamicTransform }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxSpatialOnChange);
    box_spatial_observer(world, index, "SpatialOnScene", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(Scene) }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxSpatialOnRescope);
    box_spatial_observer(world, index, "SpatialOnChildOf", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_pair(EcsChildOf, EcsWildcard) }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxSpatialOnRescope);
    box_spatial_observer(world, index, "SpatialOnParent", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(EcsParent) }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxSpatialOnRescope);
    box_spatial_transform_observers(world, index);
}

void BoxSpatialImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxSpatial);
    ECS_IMPORT(world, BoxRender);

    box_spatial_t *index = box_spatial_new();
    ECS_COMPONENT_DEFINE(world, SpatialProxy);
    ecs_set_hooks(world, SpatialProxy, {
        .ctor = box_spatial_proxy_ctor,
        .copy = box_spatial_proxy_copy,
        .move = box_spatial_proxy_move
    });
    ecs_add_pair(world, ecs_id(ShapeMesh), EcsWith, ecs_id(SpatialProxy));
    ECS_COMPONENT_DEFINE(world, SpatialPose);
    ecs_set_hooks(world, SpatialPose, { .ctor = flecs_default_ctor });
    ECS_COMPONENT_DEFINE(world, SpatialIndex);
    ecs_set_hooks(world, SpatialIndex, { .ctor = flecs_default_ctor });
    ecs_singleton_set(world, SpatialIndex, { .index = index });

    index->refs = 1;
    index->dynamic = ecs_query(world, {
        .terms = {
            { .id = ecs_id(SpatialProxy), .inout = EcsIn },
            { .id = ecs_id(SpatialPose), .inout = EcsInOut },
            { .id = ecs_id(ShapeMesh), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn, .oper = EcsOptional }
        },
        .cache_kind = EcsQueryCacheAuto
    });
    box_spatial_observers(world, index);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "SpatialUpdate" }),
        .phase = EcsOnStore,
        .run = BoxSpatialUpdate,
        .ctx = index,
        .ctx_free = box_spatial_release
    });
}
