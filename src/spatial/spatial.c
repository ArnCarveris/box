/**
 * @file spatial/spatial.c
 * @brief Registers box.spatial and keeps the index of every scene up to date.
 *
 * Observers collect changed shapes and subtrees, so an update only bounds what changed. Moved
 * leaves, movers (mover.c) and dynamic UI elements (posed by SpatialPose) are placed by the
 * first query of a frame, or by box.spatial.Update when the last frame queried.
 *
 * A shape with DynamicTransform is indexed under its mover, the entity that owns the tag; a
 * local transform set under a mover bounds its shapes again. SpatialIgnore subtrees are not
 * indexed; adding or removing the tag or DynamicTransform walks the subtree.
 */

#include "index.h"
#include "layout.h"
#include "../render/render.h"
#include "../geometry/mesh_cache.h"

ECS_COMPONENT_DECLARE(SpatialProxy);
ECS_COMPONENT_DECLARE(SpatialPose);
ECS_COMPONENT_DECLARE(SpatialIndex);
ECS_TAG_DECLARE(SpatialIgnore);

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
        proxies[i] = (SpatialProxy){ .item = -1, .mover = -1, .member = -1 };
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

/** Take an entity out of the index and out of its mover.
 *
 * @param index The index
 * @param p The proxy of the entity
 */
static void box_spatial_forget(
    box_spatial_t *index,
    SpatialProxy *p)
{
    box_spatial_unindex(index, p);
    if (p->mover >= 0) {
        box_spatial_mover_leave(index, p);
    }
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

/** Bound the shapes of a mover again when a local transform under it changed: the place of the
 * entity in the frame of its mover moved. A mover that moves itself only places its leaf.
 *
 * @param it The iterator of the observer
 */
static void BoxSpatialOnLocal(
    ecs_iter_t *it)
{
    box_spatial_t *index = it->ctx;
    int32_t i;
    if (!ecs_is_fini(it->world) && ecs_map_count(&index->mover_of)) {
        for (i = 0; i < it->count; i ++) {
            if (!ecs_owns_id(it->world, it->entities[i], DynamicTransform)) {
                box_spatial_mover_touch(it->world, index, it->entities[i]);
            }
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
                box_spatial_forget(it->ctx, p);
            }
        }
    }
}

/** Take entities that lose their shape out of the index, and mark the ones that stay so the
 * next update drops their SpatialPose or indexes them as a UI element.
 *
 * @param it The iterator of the observer
 */
static void BoxSpatialOnShapeRemove(
    ecs_iter_t *it)
{
    int32_t i;
    BoxSpatialOnRemove(it);
    bool stays = it->other_table && ecs_table_get_type(it->other_table)->count;
    if (!ecs_is_fini(it->world) && stays) {
        for (i = 0; i < it->count; i ++) {
            box_spatial_mark(it->world, it->ctx, it->entities[i], false);
        }
    }
}

/** Flag the items of entities that are disabled or enabled, so queries skip them or see them
 * again right away, without waiting for the next update.
 *
 * @param it The iterator of the observer
 */
static void BoxSpatialOnDisabled(
    ecs_iter_t *it)
{
    box_spatial_t *index = it->ctx;
    bool disabled = it->event == EcsOnAdd;
    int32_t i;
    if (!ecs_is_fini(it->world)) {
        for (i = 0; i < it->count; i ++) {
            const SpatialProxy *p = ecs_get(it->world, it->entities[i], SpatialProxy);
            box_spatial_scene_t *s = p && p->item >= 0 ?
                box_spatial_scene(index, p->scene) : NULL;
            if (s) {
                box_spatial_item(s, p->item)->disabled = disabled;
            }

            if (p && p->mover >= 0) {
                box_spatial_mover_disable(index, p, disabled);
            }
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
    const box_mesh_t *result = cache && shape->mesh ?
        box_mesh_cache_get(cache, shape->mesh) : NULL;

    return result && result->vertex_count ? result : NULL;
}

/** Get the local bounds of a shape: the unit mesh bounds times the instance scale.
 *
 * @param mesh The unit mesh, NULL when the shape has none
 * @param shape The shape
 * @param min Receives the minimum corner
 * @param max Receives the maximum corner
 * @return false when the shape has no mesh
 */
static bool box_spatial_shape_local(
    const box_mesh_t *mesh,
    const ShapeMesh *shape,
    vec3 min,
    vec3 max)
{
    int32_t i;
    for (i = 0; mesh && i < 3; i ++) {
        float lo = shape->surface.p[0][0] ? shape->min[i] : mesh->min[i];
        float hi = shape->surface.p[0][0] ? shape->max[i] : mesh->max[i];
        float a = lo * shape->scale[i], b = hi * shape->scale[i];
        min[i] = a < b ? a : b;
        max[i] = a < b ? b : a;
    }

    return mesh != NULL;
}

/** Get the local bounds of an entity: the unit mesh bounds of its shape times the instance
 * scale, else the area of a UI element without a shape (LayoutBounds, else TextBounds) in the
 * XY plane around its origin, which picks it only with a WorldTransform.
 *
 * @param world The world
 * @param cache The mesh cache
 * @param e The entity
 * @param min Receives the minimum corner
 * @param max Receives the maximum corner
 * @param ui Receives whether the entity is a UI element
 * @return false when the entity has nothing to index
 */
static bool box_spatial_local(
    const ecs_world_t *world,
    const box_mesh_cache_t *cache,
    ecs_entity_t e,
    vec3 min,
    vec3 max,
    bool *ui)
{
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    const LayoutBounds *lb = shape ? NULL : ecs_get(world, e, LayoutBounds);
    const TextBounds *tb = shape || lb ? NULL : ecs_get(world, e, TextBounds);
    bool result = false;
    *ui = false;
    if (shape) {
        result = box_spatial_shape_local(box_spatial_mesh(cache, shape), shape, min, max);
    } else if ((lb || tb) && ecs_has(world, e, WorldTransform)) {
        float w = lb ? lb->width : tb->width, h = lb ? lb->height : tb->height;
        glm_vec3_copy((vec3){ -w * 0.5f, -h * 0.5f, 0 }, min);
        glm_vec3_copy((vec3){ w * 0.5f, h * 0.5f, 0 }, max);
        *ui = true;
        result = true;
    }

    return result;
}

/** Compute the world bounds of local bounds placed by a transform.
 *
 * @param l_min Minimum corner of the local bounds
 * @param l_max Maximum corner of the local bounds
 * @param t World transform of the entity
 * @param min Receives the minimum corner
 * @param max Receives the maximum corner
 */
static void box_spatial_world_bounds(
    const vec3 l_min,
    const vec3 l_max,
    const WorldTransform *t,
    vec3 min,
    vec3 max)
{
    const WorldTransform *m = t;
    vec3 c, e;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        c[i] = (l_min[i] + l_max[i]) * 0.5f;
        e[i] = (l_max[i] - l_min[i]) * 0.5f;
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
 * @param t World transform its bounds were computed from, read when it is dynamic
 */
static void box_spatial_pose(
    ecs_world_t *world,
    ecs_entity_t e,
    bool dynamic,
    const WorldTransform *t)
{
    if (dynamic) {
        SpatialPose pose;
        ecs_os_memcpy(pose.m, t->m, sizeof(mat4));
        ecs_set_ptr(world, e, SpatialPose, &pose);
    } else if (ecs_has(world, e, SpatialPose)) {
        ecs_remove(world, e, SpatialPose);
    }
}

/** Check whether an entity or one of its ancestors has SpatialIgnore.
 *
 * @param world The world
 * @param e The entity
 * @return Whether the entity stays out of the index
 */
static bool box_spatial_ignored(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    bool result = false;
    for (; e && !result; e = ecs_get_parent(world, e)) {
        result = ecs_has_id(world, e, SpatialIgnore);
    }

    return result;
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
    WorldTransform t_buf;
    const WorldTransform *t = box_transform_world(world, e, &t_buf);
    vec3 l_min, l_max;
    bool ui = false;
    bool indexed = t && box_spatial_local(world, cache, e, l_min, l_max, &ui) &&
        !ecs_has_id(world, e, EcsPrefab) && !box_spatial_ignored(world, e);
    if (indexed) {
        box_spatial_world_bounds(l_min, l_max, t, min, max);
        scene = rescope ? box_render_scene_of(world, e) : p->scene;
        dynamic = ecs_has_id(world, e, DynamicTransform);
    }

    ecs_entity_t mover = indexed && dynamic && !ui ? box_spatial_mover_of(world, e) : 0;
    if (p->mover >= 0 && box_spatial_mover(index, p->mover)->entity != mover) {
        box_spatial_mover_leave(index, p);
    }

    if (p->item >= 0 && (!indexed || scene != p->scene || mover)) {
        box_spatial_unindex(index, p);
    }

    if (mover) {
        box_spatial_mover_join(index, p, e, mover, l_min, l_max,
            ecs_has_id(world, e, EcsDisabled));
        index->stats.updated ++;
        box_spatial_pose(world, e, false, t);
        goto done;
    }

    int32_t tree = ui ? BOX_SPATIAL_UI : dynamic ? BOX_SPATIAL_DYNAMIC : BOX_SPATIAL_STATIC;
    if (indexed && p->item < 0) {
        box_spatial_scene_t *s = box_spatial_scene_ensure(index, scene);
        p->scene = scene;
        p->item = box_spatial_item_add(s, e, tree, min, max, index->frame);
        box_spatial_item(s, p->item)->disabled = ecs_has_id(world, e, EcsDisabled);
    } else if (indexed) {
        box_spatial_item_set(
            box_spatial_scene(index, scene), p->item, tree, min, max, index->frame);
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
        const WorldTransform *t = ecs_field(&it, WorldTransform, 2);
        const ShapeMesh *shape = ecs_field(&it, ShapeMesh, 3);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            vec3 l_min, l_max, min, max;
            bool ui = false;
            WorldTransform buf;
            const WorldTransform *m = box_transform_world_field(world, &it, t, i, &buf);
            if (p[i].item < 0 || p[i].dirty || !ecs_os_memcmp(pose[i].m, m->m, sizeof(mat4))) {
                continue;
            }

            ecs_os_memcpy(pose[i].m, m->m, sizeof(mat4));
            index->stats.rebound ++;
            if (!s || scene != p[i].scene) {
                scene = p[i].scene;
                s = box_spatial_scene(index, scene);
            }

            if (shape && (!mesh || handle != shape[i].mesh)) {
                handle = shape[i].mesh;
                mesh = box_spatial_mesh(cache, &shape[i]);
            }

            const box_spatial_item_t *item = s ? box_spatial_item(s, p[i].item) : NULL;
            bool local = shape ? box_spatial_shape_local(mesh, &shape[i], l_min, l_max) :
                box_spatial_local(world, cache, it.entities[i], l_min, l_max, &ui);
            if (!item || item->tree == BOX_SPATIAL_STATIC || !local) {
                continue;
            }

            box_spatial_world_bounds(l_min, l_max, m, min, max);
            if (glm_vec3_eqv(min, ECS_CONST_CAST(float*, item->min)) &&
                glm_vec3_eqv(max, ECS_CONST_CAST(float*, item->max)))
            {
                continue;
            }

            index->stats.moved += box_spatial_item_set(
                s, p[i].item, item->tree, min, max, index->frame);
        }
    }
}

/** Place the dynamic entities and move the static and UI leaves of the last update, once per
 * update.
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
        box_spatial_dynamic(world, index, box_mesh_cache_of(world));
        index->stats.rebound += box_spatial_movers_place(world, index);
        box_spatial_move(index);
        index->stats.moved += box_spatial_settle(index);
    }
}

box_spatial_t* box_spatial_of(
    const ecs_world_t *world)
{
    box_spatial_t *index = box_spatial_get(world);
    if (index) {
        index->queried = true;
        box_spatial_sync(world, index);
    }

    return index;
}

/** Compute the bounds of the changed entities, commit the new static ones and mark the
 * leaves stale.
 *
 * When the index was queried since the last update the leaves are placed at once, as the
 * next frame most likely queries again: its first query then finds them in place.
 *
 * @param world The world
 * @param index The index
 */
static void box_spatial_process(
    ecs_world_t *world,
    box_spatial_t *index)
{
    const box_mesh_cache_t *cache = box_mesh_cache_of(world);
    bool queried = index->queried;
    int32_t i;
    index->queried = false;
    index->stats.updated = 0;
    index->stats.rebound = 0;
    index->stats.moved = 0;
    index->frame ++;
    box_spatial_walk(world, index, &index->rescoped, true);
    for (i = 0; i < ecs_vec_count(&index->dirty); i ++) {
        box_spatial_refresh(world, index, cache, *ecs_vec_get_t(&index->dirty, ecs_entity_t, i));
    }

    ecs_vec_clear(&index->dirty);
    box_spatial_movers_refit(world, index);
    index->stale = true;
    box_spatial_commit(index);
    if (queried) {
        box_spatial_sync(world, index);
    }
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

    int32_t i;
    for (i = 0; i < ecs_vec_count(&index->movers); i ++) {
        const box_spatial_mover_t *m = box_spatial_mover(index, i);
        if (m->entity && m->item >= 0) {
            stats->movers ++;
            stats->entities += m->live - 1;
        }
    }

done:
    return;
}

/** Register an observer of the index, which also matches disabled entities.
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
    desc.query.flags |= EcsQueryMatchDisabled;
    index->refs ++;
    ecs_observer_init(world, &desc);
}

/** Follow static transforms: the entities whose WorldTransform was set, added or removed, and
 * the entities whose static transform was computed through box_transform_on_moved. An entity
 * without a WorldTransform has no place in the world and is not indexed, as it is not drawn.
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
    box_spatial_observer(world, index, "SpatialOnWorldTransformAdd", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(WorldTransform) }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxSpatialOnChange);
    index->refs ++;
    box_transform_on_moved(world, box_spatial_on_moved, index, box_spatial_release);
}

/** Follow the local transforms under movers, which move shapes in the frame of their mover.
 *
 * @param world The world
 * @param index The index
 */
static void box_spatial_local_observers(
    ecs_world_t *world,
    box_spatial_t *index)
{
    ecs_id_t ids[] = { ecs_id(Position), ecs_id(Rotation), ecs_id(Scale) };
    const char *names[] = { "SpatialOnPosition", "SpatialOnRotation", "SpatialOnScale" };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        box_spatial_observer(world, index, names[i], (ecs_observer_desc_t){
            .query.terms = {
                { .id = ids[i] },
                { .id = DynamicTransform, .inout = EcsInOutNone }
            },
            .events = { EcsOnSet } }, BoxSpatialOnLocal);
    }
}

/** Register the observers that collect changed entities and changed subtrees.
 *
 * A disabled entity stays in the index, with its item flagged so no query or ray sees it until
 * it is enabled again. The observers match disabled entities, so deleting one, removing its
 * shape or changing its bounds or scene still updates the index.
 *
 * @param world The world
 * @param index The index
 */
static void box_spatial_observers(
    ecs_world_t *world,
    box_spatial_t *index)
{
    box_spatial_observer(world, index, "SpatialOnPrefab", (ecs_observer_desc_t){
        .query.terms = {{ .id = EcsPrefab }},
        .query.flags = EcsQueryMatchPrefab,
        .events = { EcsOnAdd, EcsOnRemove } }, BoxSpatialOnChange);
    box_spatial_observer(world, index, "SpatialOnDisabled", (ecs_observer_desc_t){
        .query.terms = {{ .id = EcsDisabled }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxSpatialOnDisabled);
    box_spatial_observer(world, index, "SpatialOnShape", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(ShapeMesh) }},
        .events = { EcsOnSet } }, BoxSpatialOnChange);
    box_spatial_observer(world, index, "SpatialOnShapeRemove", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(ShapeMesh) }},
        .events = { EcsOnRemove } }, BoxSpatialOnShapeRemove);
    box_spatial_observer(world, index, "SpatialOnElement", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(LayoutBounds) }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxSpatialOnChange);
    box_spatial_observer(world, index, "SpatialOnText", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(TextBounds) }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxSpatialOnChange);
    box_spatial_observer(world, index, "SpatialOnProxyRemove", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(SpatialProxy) }},
        .events = { EcsOnRemove } }, BoxSpatialOnRemove);
    index->refs ++;
    box_transform_on_dynamic(world, BoxSpatialOnChange, index, box_spatial_release);
    box_spatial_observer(world, index, "SpatialOnScene", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(Scene) }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxSpatialOnRescope);
    box_spatial_observer(world, index, "SpatialOnChildOf", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_pair(EcsChildOf, EcsWildcard) }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxSpatialOnRescope);
    box_spatial_observer(world, index, "SpatialOnParent", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(EcsParent) }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxSpatialOnRescope);
    box_spatial_observer(world, index, "SpatialOnMover", (ecs_observer_desc_t){
        .query.terms = {{ .id = DynamicTransform, .src.id = EcsSelf }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxSpatialOnRescope);
    box_spatial_observer(world, index, "SpatialOnIgnore", (ecs_observer_desc_t){
        .query.terms = {{ .id = SpatialIgnore }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxSpatialOnRescope);
    box_spatial_transform_observers(world, index);
    box_spatial_local_observers(world, index);
}

void BoxSpatialImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxSpatial);
    ECS_IMPORT(world, BoxRender);

    ecs_set_name_prefix(world, "Spatial");

    box_spatial_layout_init(world);

    box_spatial_t *index = box_spatial_new();
    ECS_COMPONENT_DEFINE(world, SpatialProxy);
    ecs_set_hooks(world, SpatialProxy, {
        .ctor = box_spatial_proxy_ctor,
        .copy = box_spatial_proxy_copy,
        .move = box_spatial_proxy_move
    });
    ecs_add_pair(world, ecs_id(ShapeMesh), EcsWith, ecs_id(SpatialProxy));
    ecs_add_pair(world, ecs_id(LayoutBounds), EcsWith, ecs_id(SpatialProxy));
    ecs_add_pair(world, ecs_id(TextBounds), EcsWith, ecs_id(SpatialProxy));
    ECS_COMPONENT_DEFINE(world, SpatialPose);
    ecs_set_hooks(world, SpatialPose, { .ctor = flecs_default_ctor });
    ECS_TAG_DEFINE(world, SpatialIgnore);
    ECS_COMPONENT_DEFINE(world, SpatialIndex);
    ecs_set_hooks(world, SpatialIndex, { .ctor = flecs_default_ctor });
    ecs_singleton_set(world, SpatialIndex, { .index = index });

    index->refs = 1;
    index->dynamic = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "Dynamic" }),
        .terms = {
            { .id = ecs_id(SpatialProxy), .inout = EcsIn },
            { .id = ecs_id(SpatialPose), .inout = EcsInOut },
            { .id = ecs_id(WorldTransform), .inout = EcsIn },
            { .id = ecs_id(ShapeMesh), .inout = EcsIn, .oper = EcsOptional }
        },
        .cache_kind = EcsQueryCacheAuto
    });
    box_spatial_observers(world, index);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Update" }),
        .phase = EcsOnStore,
        .run = BoxSpatialUpdate,
        .ctx = index,
        .ctx_free = box_spatial_release
    });

    ecs_set_name_prefix(world, NULL);
}
