/**
 * @file transform/hierarchy.c
 * @brief The single place where the engine walks a parent hierarchy.
 *
 * Flecs stores a hierarchy either as a fragmenting (ChildOf, parent) pair or as
 * a non fragmenting Parent component. Both storages answer the same operations,
 * so every module reads a tree through these helpers instead of one relationship.
 */

#include <box.h>
#include "transform_internal.h"

/** Append one entity to a vector.
 *
 * @param world The world
 * @param e The entity
 * @param ctx The vector that receives the entity
 */
static void box_hierarchy_append(
    ecs_world_t *world,
    ecs_entity_t e,
    void *ctx)
{
    (void)world;

    ecs_vec_append_t(NULL, ctx, ecs_entity_t)[0] = e;
}

ecs_entity_t box_hierarchy_parent(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_get_parent(world, e);
}

void box_hierarchy_children(
    ecs_world_t *world,
    ecs_entity_t e,
    box_hierarchy_visit_t visit,
    void *ctx)
{
    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            visit(world, it.entities[i], ctx);
        }
    }
}

void box_hierarchy_collect(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_vec_t *out)
{
    box_hierarchy_children(world, e, box_hierarchy_append, out);
}

bool box_hierarchy_ancestor_has(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_id_t id)
{
    bool result = false;
    ecs_entity_t cur = e;
    while (cur) {
        if (ecs_has_id(world, cur, id)) {
            result = true;
            break;
        }

        cur = box_hierarchy_parent(world, cur);
    }

    return result;
}

bool box_hierarchy_is_child(
    const ecs_world_t *world,
    const ecs_table_t *table)
{
    bool result = false;
    if (!table) {
        goto done;
    }

    if (ecs_search(world, table, ecs_pair(EcsChildOf, EcsWildcard), NULL) != -1) {
        result = true;
        goto done;
    }

    result = ecs_search(world, table, ecs_id(EcsParent), NULL) != -1;
done:
    return result;
}

void box_hierarchy_set_parent(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t parent)
{
    bool non_fragmenting = ecs_has(world, e, EcsParent);
    if (!parent) {
        if (non_fragmenting) {
            ecs_remove(world, e, EcsParent);
        } else {
            ecs_remove_pair(world, e, EcsChildOf, EcsWildcard);
        }

        goto done;
    }

    if (non_fragmenting) {
        ecs_set(world, e, EcsParent, { parent });
    } else {
        ecs_add_pair(world, e, EcsChildOf, parent);
    }

done:
    return;
}
