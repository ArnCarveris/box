/**
 * @file material/tint.c
 * @brief Resolves Tint into ActualTint on the entity and its untinted descendants.
 */

#include "material_internal.h"
#include "../transform/transform_internal.h"

/** Queue a resolved tint for an entity and recurse into children without their own Tint.
 *
 * @param world The world, used for reading
 * @param stage The stage that queues the writes
 * @param e The entity
 * @param tint The resolved tint, NULL to remove it
 */
static void box_tint_apply(
    ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t e,
    const ActualTint *tint)
{
    if (tint) {
        ecs_set_ptr(stage, e, ActualTint, tint);
    } else {
        ecs_remove(stage, e, ActualTint);
    }

    ecs_vec_t children;
    ecs_vec_init_t(NULL, &children, ecs_entity_t, 0);
    box_hierarchy_collect(world, e, &children);

    int32_t i;
    for (i = 0; i < ecs_vec_count(&children); i ++) {
        ecs_entity_t child = ecs_vec_get_t(&children, ecs_entity_t, i)[0];
        if (ecs_has(world, child, Tint)) {
            continue;
        }

        box_tint_apply(world, stage, child, tint);
    }

    ecs_vec_fini_t(NULL, &children, ecs_entity_t);
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
        box_tint_apply(it->world, it->stage, it->entities[i], &actual);
    }

}

/** Tint was removed: fall back to the parent's resolved tint for the subtree.
 *
 * @param it Iterator over the entities that lost their tint
 */
static void box_tint_on_remove(
    ecs_iter_t *it)
{
    bool deleting = !it->other_table || !ecs_table_get_type(it->other_table)->count;
    int32_t i;
    for (i = 0; i < it->count && !deleting; i ++) {
        ecs_entity_t parent = box_hierarchy_parent(it->world, it->entities[i]);
        const ActualTint *inherited = parent ? ecs_get(it->world, parent, ActualTint) : NULL;
        box_tint_apply(it->world, it->stage, it->entities[i], inherited);
    }

}

/** Entity was parented under a tinted ancestor in either storage: inherit its tint.
 *
 * @param it Iterator over the reparented entities
 */
static void box_tint_on_parent(
    ecs_iter_t *it)
{
    ActualTint *parent_tint = ecs_field(it, ActualTint, 1);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_tint_apply(it->world, it->stage, it->entities[i], parent_tint);
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
