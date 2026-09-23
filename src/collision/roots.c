/**
 * @file collision/roots.c
 * @brief Recognises prefab and template roots and aggregates their bounds.
 *
 * An instance is remembered when it is created and turned into a root by the
 * next flush, which is late enough for its subtree to exist. From then on the
 * observers on AABB and on the two parent storages mark the root a change
 * belongs to, and a flush walks only the marked roots.
 *
 * A Hidden switch is a change like any other: the walk of a root skips the
 * subtree it sits on and a hidden root itself comes out empty, which takes both
 * out of the index and out of every query that reads it.
 */

#include "collision_internal.h"

/** Running aggregate of the bounds and the colliders of a subtree */
typedef struct box_root_agg_t {
    box_collision_state_t *state; /**< The state, to update nested roots first */
    ecs_vec_t *list;              /**< Colliders the root covers */
    vec3 min;                     /**< Minimum corner so far */
    vec3 max;                     /**< Maximum corner so far */
    int32_t count;                /**< Entities with bounds so far */
    bool dynamic;                 /**< The subtree moves every frame */
} box_root_agg_t;

static void box_root_update(
    ecs_world_t *world,
    box_collision_state_t *state,
    ecs_entity_t root);

/** Test whether a box covers nothing.
 *
 * @param min Minimum corner
 * @param max Maximum corner
 * @return true when the box is a point
 */
static bool box_root_empty(
    vec3 min,
    vec3 max)
{
    return min.x == max.x && min.y == max.y && min.z == max.z;
}

/** Add a box to a running aggregate.
 *
 * @param agg The aggregate
 * @param min Minimum corner
 * @param max Maximum corner
 * @param count Entities the box stands for
 */
static void box_root_add(
    box_root_agg_t *agg,
    vec3 min,
    vec3 max,
    int32_t count)
{
    if (!count || box_root_empty(min, max)) {
        goto done;
    }

    agg->min = agg->count ? vec3_min(agg->min, min) : min;
    agg->max = agg->count ? vec3_max(agg->max, max) : max;
    agg->count += count;
done:
    return;
}

/** Add the world bounds of the collider of an entity whose AABB covers nothing.
 *
 * A template root or a child that carries only a collider, like a picking
 * target, has an empty AABB or none, so its collider is what tells the
 * aggregate where it is.
 *
 * @param world The world
 * @param e The entity
 * @param agg The aggregate
 */
static void box_root_add_collider(
    ecs_world_t *world,
    ecs_entity_t e,
    box_root_agg_t *agg)
{
    Collider c = {0};
    vec3 min, max;
    if (box_collider_resolve(world, e, &c)) {
        box_collider_bounds(&c, &min, &max);
        box_root_add(agg, min, max, 1);
    }
}

/** Visit one entity below a root.
 *
 * @param world The world
 * @param e The entity
 * @param ctx The aggregate
 */
static void box_root_visit(
    ecs_world_t *world,
    ecs_entity_t e,
    void *ctx)
{
    box_root_agg_t *agg = ctx;
    if (ecs_has_id(world, e, EcsPrefab)) {
        goto done;
    }

    if (agg->state->hide_used && ecs_has_id(world, e, Hidden)) {
        goto done;
    }

    const AABB *aabb = ecs_get(world, e, AABB);
    if (aabb && !box_root_empty(aabb->min, aabb->max)) {
        box_root_add(agg, aabb->min, aabb->max, 1);
    } else {
        box_root_add_collider(world, e, agg);
    }

    if (ecs_has(world, e, RootBounds)) {
        box_root_update(world, agg->state, e);
        const RootBounds *rb = ecs_get(world, e, RootBounds);
        box_root_add(agg, rb->min, rb->max, rb->count);
        goto done;
    }

    bool dynamic = agg->dynamic;
    agg->dynamic = dynamic || ecs_has_id(world, e, DynamicTransform);
    if (!agg->dynamic && ecs_has(world, e, Collider)) {
        ecs_vec_append_t(NULL, agg->list, ecs_entity_t)[0] = e;
        box_index_unset(&agg->state->index, e);
    }

    box_hierarchy_children(world, e, box_root_visit, agg);
    agg->dynamic = dynamic;
done:
    return;
}

/** Transform the eight corners of a box and take the bounds of the result.
 *
 * @param m The matrix
 * @param min Minimum corner, receives the new minimum
 * @param max Maximum corner, receives the new maximum
 */
static void box_root_transform(
    const mat4 *m,
    vec3 *min,
    vec3 *max)
{
    vec3 lo = min[0];
    vec3 hi = max[0];
    int32_t corner;
    for (corner = 0; corner < 8; corner ++) {
        vec3 c = { corner & 1 ? hi.x : lo.x, corner & 2 ? hi.y : lo.y,
            corner & 4 ? hi.z : lo.z };
        vec3 w = mat4_transform_point(m, c);
        min[0] = corner ? vec3_min(min[0], w) : w;
        max[0] = corner ? vec3_max(max[0], w) : w;
    }
}

/** Get the world matrix of an entity.
 *
 * @param world The world
 * @param e The entity
 * @return The matrix, identity when the entity has no world transform
 */
static mat4 box_root_matrix(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);

    return wt ? wt->m : mat4_identity();
}

/** Walk the subtree of a root and write the bounds it covers.
 *
 * @param world The world
 * @param state The state
 * @param root The root
 * @param out Receives the bounds
 */
static void box_root_aggregate(
    ecs_world_t *world,
    box_collision_state_t *state,
    ecs_entity_t root,
    RootBounds *out)
{
    box_root_agg_t agg = { .state = state };
    agg.list = box_index_list(&state->index, root);
    agg.dynamic = box_hierarchy_ancestor_has(world, root, DynamicTransform);
    ecs_vec_clear(agg.list);

    const AABB *aabb = ecs_get(world, root, AABB);
    if (aabb && !box_root_empty(aabb->min, aabb->max)) {
        box_root_add(&agg, aabb->min, aabb->max, 1);
    } else {
        box_root_add_collider(world, root, &agg);
    }

    if (!agg.dynamic && ecs_has(world, root, Collider)) {
        ecs_vec_append_t(NULL, agg.list, ecs_entity_t)[0] = root;
    }


    box_hierarchy_children(world, root, box_root_visit, &agg);

    out->count = agg.count;
    out->min = agg.count ? agg.min : vec3_new(0, 0, 0);
    out->max = agg.count ? agg.max : vec3_new(0, 0, 0);
    out->local_min = out->min;
    out->local_max = out->max;
    if (!agg.count) {
        goto done;
    }

    mat4 m = box_root_matrix(world, root);
    mat4 inv = mat4_inverse(&m);
    box_root_transform(&inv, &out->local_min, &out->local_max);
done:
    return;
}

/** Give a hidden root empty bounds and drop the colliders it used to cover.
 *
 * @param state The state
 * @param root The root
 * @param out The bounds, emptied in place
 */
static void box_root_clear(
    box_collision_state_t *state,
    ecs_entity_t root,
    RootBounds *out)
{
    ecs_vec_clear(box_index_list(&state->index, root));
    out->count = 0;
    out->min = vec3_new(0, 0, 0);
    out->max = out->min;
    out->local_min = out->min;
    out->local_max = out->min;
}

/** Give a root the world bounds its cached local bounds have now.
 *
 * @param world The world
 * @param root The root
 * @param out The bounds, updated in place
 */
static void box_root_move(
    ecs_world_t *world,
    ecs_entity_t root,
    RootBounds *out)
{
    out->min = out->local_min;
    out->max = out->local_max;
    mat4 m = box_root_matrix(world, root);
    box_root_transform(&m, &out->min, &out->max);
}

/** Do the work a root was marked for, when it is still marked.
 *
 * @param world The world
 * @param state The state
 * @param root The root
 */
static void box_root_update(
    ecs_world_t *world,
    box_collision_state_t *state,
    ecs_entity_t root)
{
    ecs_map_val_t *slot = ecs_map_get(&state->dirty, root);
    if (!slot) {
        goto done;
    }

    int32_t flags = (int32_t)slot[0];
    ecs_map_remove(&state->dirty, root);
    if (!ecs_is_alive(world, root)) {
        goto done;
    }

    const RootBounds *cur = ecs_get(world, root, RootBounds);
    if (!cur) {
        goto done;
    }

    RootBounds next = cur[0];
    bool rigid = ecs_has_id(world, root, RigidSubtree);
    bool hidden = state->hide_used &&
        box_hierarchy_ancestor_has(world, root, Hidden);
    if (hidden) {
        box_root_clear(state, root, &next);
    } else if (next.count && rigid && (flags & BOX_ROOT_MOVED)) {
        box_root_move(world, root, &next);
    } else {
        box_root_aggregate(world, state, root, &next);
    }

    ecs_set_ptr(world, root, RootBounds, &next);
    if (next.count) {
        box_index_set(&state->index, root, next.min, next.max);
    } else {
        box_index_unset(&state->index, root);
    }

done:
    return;
}

void box_collision_mark(
    box_collision_state_t *state,
    ecs_entity_t root,
    int32_t flags)
{
    ecs_map_val_t *slot = ecs_map_ensure(&state->dirty, root);
    slot[0] |= (ecs_map_val_t)flags;
}

ecs_entity_t box_collision_root_of(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = 0;
    if (!ecs_count_id(world, ecs_id(RootBounds))) {
        return result;
    }

    ecs_entity_t cur = e;
    while (cur && ecs_is_alive(world, cur)) {
        if (ecs_has(world, cur, RootBounds)) {
            result = cur;
            break;
        }

        cur = box_hierarchy_parent(world, cur);
    }

    return result;
}

/** Mark the root an entity belongs to for a new aggregate.
 *
 * @param world The world
 * @param state The state
 * @param e The entity that changed
 */
static void box_collision_mark_of(
    const ecs_world_t *world,
    box_collision_state_t *state,
    ecs_entity_t e)
{
    ecs_entity_t root = box_collision_root_of(world, e);
    if (root) {
        box_collision_mark(state, root, BOX_ROOT_FULL);
    }
}

/** Mark every root below an entity and queue every other entity for a collider.
 *
 * A collider that is not below a root has a leaf of its own, so the switch
 * reaches it through the collider pass instead of through the aggregate of a
 * root; a nested root keeps its own leaf and is marked so its own pass empties
 * it.
 *
 * @param world The world
 * @param e The entity
 * @param ctx The state
 */
static void box_collision_hide_below(
    ecs_world_t *world,
    ecs_entity_t e,
    void *ctx)
{
    box_collision_state_t *state = ctx;
    if (ecs_has(world, e, RootBounds)) {
        box_collision_mark(state, e, BOX_ROOT_FULL);
    } else {
        box_collision_touch(state, e);
    }

    box_hierarchy_children(world, e, box_collision_hide_below, state);
}

/** A Hidden tag appeared or went away: redo the roots and colliders below it.
 *
 * @param it Iterator over the entities
 */
static void BoxCollisionOnHidden(
    ecs_iter_t *it)
{
    box_collision_state_t *state = it->ctx;
    ecs_world_t *world = it->world;
    state->hide_used = true;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_collision_mark_of(world, state, it->entities[i]);
        box_collision_hide_below(world, it->entities[i], state);
    }
}

/** Remember an instance so the next flush can turn it into a root.
 *
 * @param it Iterator over the new instances
 */
static void BoxCollisionOnInstance(
    ecs_iter_t *it)
{
    box_collision_state_t *state = it->ctx;
    ecs_world_t *world = it->world;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (ecs_has_id(world, e, EcsPrefab)) {
            continue;
        }

        ecs_vec_append_t(NULL, &state->instances, ecs_entity_t)[0] = e;
    }
}

/** Mark a root that was added or removed, by the flush or by hand.
 *
 * @param it Iterator over the roots
 */
static void BoxCollisionOnRoot(
    ecs_iter_t *it)
{
    box_collision_state_t *state = it->ctx;
    ecs_world_t *world = it->world;
    bool removed = it->event == EcsOnRemove;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (!removed) {
            box_collision_mark(state, e, BOX_ROOT_FULL);
            continue;
        }

        ecs_map_remove(&state->dirty, e);
        box_index_unset(&state->index, e);
        box_collision_mark_of(world, state, box_hierarchy_parent(world, e));
    }
}

/** Mark the root of every entity whose bounds appeared, changed or went away.
 *
 * @param it Iterator over the entities
 */
static void BoxCollisionOnBounds(
    ecs_iter_t *it)
{
    box_collision_state_t *state = it->ctx;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_collision_mark_of(it->world, state, it->entities[i]);
    }
}

/** Mark the roots on both sides of a change of parent.
 *
 * @param it Iterator over the entities
 */
static void BoxCollisionOnParent(
    ecs_iter_t *it)
{
    box_collision_state_t *state = it->ctx;
    ecs_world_t *world = it->world;
    ecs_entity_t parent = ecs_pair_second(world, it->event_id);

    box_collision_mark_of(world, state, parent);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_collision_mark_of(world, state, it->entities[i]);
    }
}

/** Mark a root whose own transform changed.
 *
 * @param it Iterator over the moved roots
 */
static void BoxCollisionOnTransform(
    ecs_iter_t *it)
{
    box_collision_state_t *state = it->ctx;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_collision_mark(state, it->entities[i], BOX_ROOT_MOVED);
    }
}

void box_collision_roots_promote(
    ecs_world_t *world,
    box_collision_state_t *state)
{
    int32_t i, count = ecs_vec_count(&state->instances);
    ecs_entity_t *instances = ecs_vec_first_t(&state->instances, ecs_entity_t);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = instances[i];
        if (ecs_is_alive(world, e) && !ecs_has(world, e, RootBounds)) {
            ecs_add(world, e, RootBounds);
        }
    }

    ecs_vec_clear(&state->instances);
}

void box_collision_roots_flush(
    ecs_world_t *world,
    box_collision_state_t *state)
{
    int32_t i, count;
    box_collision_roots_promote(world, state);
    ecs_vec_clear(&state->pass);

    ecs_map_iter_t mit = ecs_map_iter(&state->dirty);
    while (ecs_map_next(&mit)) {
        ecs_vec_append_t(NULL, &state->pass, ecs_entity_t)[0] =
            (ecs_entity_t)ecs_map_key(&mit);
    }

    count = ecs_vec_count(&state->pass);
    ecs_entity_t *pass = ecs_vec_first_t(&state->pass, ecs_entity_t);
    for (i = 0; i < count; i ++) {
        box_root_update(world, state, pass[i]);
    }
}

void box_collision_hidden_init(
    ecs_world_t *world,
    box_collision_state_t *state)
{
    if (state->hide_wired || !Hidden) {
        goto done;
    }

    ecs_observer(world, {
        .query.terms = {{ .id = Hidden }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = BoxCollisionOnHidden,
        .ctx = state,
        .yield_existing = true
    });

    state->hide_wired = true;
done:
    return;
}

void box_collision_roots_init(
    ecs_world_t *world,
    box_collision_state_t *state)
{
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_pair(EcsIsA, EcsWildcard) }},
        .events = { EcsOnAdd },
        .callback = BoxCollisionOnInstance,
        .ctx = state
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_pair(EcsScriptTemplatePending, EcsWildcard) }},
        .events = { EcsOnAdd },
        .callback = BoxCollisionOnInstance,
        .ctx = state
    });

    ecs_entity_t root = ecs_lookup(world, "flecs.script.ScriptTemplateRoot");
    if (root) {
        ecs_observer(world, {
            .query.terms = {{ .id = ecs_pair(root, EcsWildcard) }},
            .events = { EcsOnAdd },
            .callback = BoxCollisionOnInstance,
            .ctx = state
        });
    }

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(RootBounds) }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = BoxCollisionOnRoot,
        .ctx = state
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(AABB) }},
        .events = { EcsOnAdd, EcsOnSet, EcsOnRemove },
        .callback = BoxCollisionOnBounds,
        .ctx = state
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_pair(EcsChildOf, EcsWildcard) }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = BoxCollisionOnParent,
        .ctx = state
    });

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(WorldTransform) },
            { .id = ecs_id(RootBounds), .inout = EcsInOutFilter }
        },
        .events = { EcsOnSet },
        .callback = BoxCollisionOnTransform,
        .ctx = state
    });
}
