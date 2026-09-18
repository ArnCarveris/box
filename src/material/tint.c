/**
 * @file material/tint.c
 * @brief Resolves Tint into ActualTint on the entity and its untinted descendants.
 */

#include "material_internal.h"

/** Write a resolved tint to an entity and recurse into children without their own Tint.
 *
 * @param world The world
 * @param e The entity
 * @param tint The resolved tint, NULL to remove it
 */
static void box_tint_apply(
    ecs_world_t *world,
    ecs_entity_t e,
    const ActualTint *tint)
{
    if (tint) {
        ecs_set_ptr(world, e, ActualTint, tint);
    } else {
        ecs_remove(world, e, ActualTint);
    }

    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (!ecs_has(world, it.entities[i], Tint)) {
                box_tint_apply(world, it.entities[i], tint);
            }
        }
    }
}

/** Tint was set: resolve it for the subtree.
 *
 * @param it Iterator over the tinted entities
 */
static void box_tint_on_set(
    ecs_iter_t *it)
{
    Tint *t = ecs_field(it, Tint, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ActualTint actual = { t[i].r, t[i].g, t[i].b, t[i].a };
        box_tint_apply(it->world, it->entities[i], &actual);
    }
}

/** Tint was removed: fall back to the parent's resolved tint for the subtree.
 *
 * @param it Iterator over the entities that lost their tint
 */
static void box_tint_on_remove(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t parent = ecs_get_target(it->world, it->entities[i], EcsChildOf, 0);
        const ActualTint *inherited = parent ? ecs_get(it->world, parent, ActualTint) : NULL;
        box_tint_apply(it->world, it->entities[i], inherited);
    }
}

/** Entity was parented under a tinted ancestor: inherit its resolved tint.
 *
 * @param it Iterator over the reparented entities
 */
static void box_tint_on_parent(
    ecs_iter_t *it)
{
    ActualTint *parent_tint = ecs_field(it, ActualTint, 1);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_tint_apply(it->world, it->entities[i], parent_tint);
    }
}

void box_tint_init(
    ecs_world_t *world)
{
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Tint) }},
        .events = { EcsOnSet },
        .callback = box_tint_on_set
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Tint) }},
        .events = { EcsOnRemove },
        .callback = box_tint_on_remove
    });

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_pair(EcsChildOf, EcsWildcard) },
            { .id = ecs_id(ActualTint), .src.id = EcsUp, .inout = EcsIn },
            { .id = ecs_id(Tint), .oper = EcsNot }
        },
        .events = { EcsOnAdd },
        .callback = box_tint_on_parent
    });
}
