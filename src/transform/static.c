/**
 * @file transform/static.c
 * @brief Observers that keep the world transform of static entities up to date.
 *
 * A static entity has no DynamicTransform on itself or an ancestor. Its world
 * transform is written when its pose changes and when it is reparented.
 */

#include <box.h>
#include "transform_internal.h"

/** Test whether an ancestor of an entity has DynamicTransform.
 *
 * @param world The world
 * @param e The entity
 * @return true when the entity is part of a dynamic subtree
 */
static bool box_transform_dynamic_parent(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    bool result = false;
    ecs_entity_t parent = ecs_get_parent(world, e);
    while (parent) {
        if (ecs_has(world, parent, DynamicTransform)) {
            result = true;
            break;
        }

        parent = ecs_get_parent(world, parent);
    }

    return result;
}

/** Write the world transform of an entity and of its static descendants.
 *
 * @param world The world
 * @param e The entity
 * @param parent The world matrix of the parent
 */
static void box_transform_write(
    ecs_world_t *world,
    ecs_entity_t e,
    const mat4 *parent)
{
    mat4 m;
    if (ecs_has(world, e, ManualTransform)) {
        const WorldTransform *wt = ecs_get(world, e, WorldTransform);
        m = wt->m;
    } else {
        mat4 local = box_transform_compose(
            ecs_get(world, e, Position),
            ecs_get(world, e, Rotation),
            ecs_get(world, e, Scale),
            ecs_get(world, e, Orientation));
        m = mat4_mul(parent, &local);
        ecs_set(world, e, WorldTransform, { .m = m });
    }

    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t child = it.entities[i];
            if (ecs_has(world, child, DynamicTransform)) {
                continue;
            }

            if (!ecs_has(world, child, WorldTransform)) {
                continue;
            }

            box_transform_write(world, child, &m);
        }
    }
}

/** Recompute the world transform of a static entity and of its subtree.
 *
 * @param world The world
 * @param e The entity
 * @param unparented true when the entity is losing its parent
 */
static void box_transform_recompute(
    ecs_world_t *world,
    ecs_entity_t e,
    bool unparented)
{
    mat4 parent = mat4_identity();
    if (!unparented) {
        ecs_entity_t p = ecs_get_parent(world, e);
        if (p) {
            const WorldTransform *wt = ecs_get(world, p, WorldTransform);
            if (wt) {
                parent = wt->m;
            }
        }
    }

    box_transform_write(world, e, &parent);
}

/** Test whether the engine updates an entity through the static path.
 *
 * @param world The world
 * @param e The entity
 * @param dynamic_self true when the entity counts as dynamic itself
 * @return true when the entity must be recomputed now
 */
static bool box_transform_is_static(
    const ecs_world_t *world,
    ecs_entity_t e,
    bool dynamic_self)
{
    bool result = false;
    if (dynamic_self && ecs_has(world, e, DynamicTransform)) {
        goto done;
    }

    if (box_transform_dynamic_parent(world, e)) {
        goto done;
    }

    result = ecs_has(world, e, WorldTransform);
done:
    return result;
}

/** OnSet observer for the pose components.
 *
 * @param it Iterator over the changed entities
 */
static void BoxTransformOnPose(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (!box_transform_is_static(world, e, true)) {
            continue;
        }

        box_transform_recompute(world, e, false);
    }
}

/** Observer for entities that get or lose a parent.
 *
 * @param it Iterator over the reparented entities
 */
static void BoxTransformOnReparent(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    bool unparented = it->event == EcsOnRemove;
    if (unparented) {
        if (!it->other_table) {
            goto done;
        }

        if (ecs_search(world, it->other_table,
            ecs_pair(EcsChildOf, EcsWildcard), NULL) != -1)
        {
            goto done;
        }
    }

    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (!box_transform_is_static(world, e, true)) {
            continue;
        }

        box_transform_recompute(world, e, unparented);
    }
done:
    return;
}

/** OnRemove observer for DynamicTransform: the subtree becomes static again.
 *
 * @param it Iterator over the entities that lose the tag
 */
static void BoxTransformOnStatic(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (!box_transform_is_static(world, e, false)) {
            continue;
        }

        box_transform_recompute(world, e, false);
    }
}

void box_transform_static_init(
    ecs_world_t *world)
{
    ecs_id_t pose[] = {
        ecs_id(Position), ecs_id(Rotation), ecs_id(Scale), ecs_id(Orientation)
    };

    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_observer(world, {
            .query.terms = {{ .id = pose[i] }},
            .events = { EcsOnSet },
            .callback = BoxTransformOnPose
        });
    }

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_pair(EcsChildOf, EcsWildcard) },
            { .id = ecs_id(WorldTransform), .inout = EcsInOutFilter }
        },
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = BoxTransformOnReparent
    });

    ecs_observer(world, {
        .query.terms = {
            { .id = DynamicTransform },
            { .id = ecs_id(WorldTransform), .inout = EcsInOutFilter }
        },
        .events = { EcsOnRemove },
        .callback = BoxTransformOnStatic
    });
}
