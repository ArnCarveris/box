/**
 * @file transform/dynamic.c
 * @brief The per frame world transform system and the observers that spread its tag.
 *
 * DynamicTransform is kept on every descendant of the entity it was added to:
 * it spreads to children and to entities placed under a tagged parent, and is
 * removed from children unless their parent still has it.
 */

#include <box.h>
#include "transform_internal.h"

/** Compose the world transform of every dynamic entity, parents first.
 *
 * @param it Iterator over the dynamic entities of one table
 */
static void BoxTransformDynamicUpdate(
    ecs_iter_t *it)
{
    WorldTransform *w = ecs_field(it, WorldTransform, 0);
    const Position *p = ecs_field(it, Position, 1);
    const Rotation *r = ecs_field(it, Rotation, 2);
    const Scale *s = ecs_field(it, Scale, 3);
    const Orientation *o = ecs_field(it, Orientation, 4);
    const WorldTransform *parent = ecs_field(it, WorldTransform, 5);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        mat4 local = box_transform_compose(p ? &p[i] : NULL, r ? &r[i] : NULL,
            s ? &s[i] : NULL, o ? &o[i] : NULL);
        if (parent) {
            w[i].m = mat4_mul(&parent->m, &local);
        } else {
            w[i].m = local;
        }
    }
}

/** Hand the tag to one child of an entity that has it.
 *
 * @param world The world
 * @param child The child
 * @param ctx The stage the tag is added through
 */
static void box_transform_tag_child(
    ecs_world_t *world,
    ecs_entity_t child,
    void *ctx)
{
    (void)world;
    ecs_add(ctx, child, DynamicTransform);
}

/** Take the tag from one child of an entity that lost it.
 *
 * @param world The world
 * @param child The child
 * @param ctx The stage the tag is removed through
 */
static void box_transform_untag_child(
    ecs_world_t *world,
    ecs_entity_t child,
    void *ctx)
{
    (void)world;
    ecs_remove(ctx, child, DynamicTransform);
}

/** Test whether the entities of an observer are on their way out of the world.
 *
 * @param it The observer iterator
 * @return true when the destination table is the empty root table
 */
static bool box_transform_leaving(
    const ecs_iter_t *it)
{
    return it->other_table && !ecs_table_get_type(it->other_table)->count;
}

/** OnAdd observer for DynamicTransform: the children get the tag too.
 *
 * @param it Iterator over the entities that got the tag
 */
static void BoxTransformOnDynamicAdd(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_hierarchy_children(it->world, it->entities[i], box_transform_tag_child,
            it->stage);
    }
}

/** OnRemove observer for DynamicTransform: the children lose the tag too.
 *
 * An entity whose parent still has the tag is a descendant of a dynamic
 * entity, so the tag comes back instead. An entity that is being deleted
 * takes its children with it and does nothing.
 *
 * @param it Iterator over the entities that lose the tag
 */
static void BoxTransformOnDynamicRemove(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    if (box_transform_leaving(it)) {
        goto done;
    }

    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        ecs_entity_t parent = box_hierarchy_parent(world, e);
        if (parent && ecs_has(world, parent, DynamicTransform)) {
            ecs_add(it->stage, e, DynamicTransform);
            continue;
        }

        box_hierarchy_children(world, e, box_transform_untag_child, it->stage);
    }

done:
    return;
}

/** OnAdd observer for a parent in either storage: a dynamic parent hands down its tag.
 *
 * @param it Iterator over the entities that got a parent
 */
static void BoxTransformOnDynamicParent(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        ecs_entity_t parent = box_hierarchy_parent(world, e);
        if (!parent || !ecs_has(world, parent, DynamicTransform)) {
            continue;
        }

        if (ecs_has(world, e, DynamicTransform)) {
            continue;
        }

        ecs_add(it->stage, e, DynamicTransform);
    }
}

void box_transform_dynamic_init(
    ecs_world_t *world)
{
    ecs_observer(world, {
        .query.terms = {{ .id = DynamicTransform }},
        .events = { EcsOnAdd },
        .callback = BoxTransformOnDynamicAdd
    });

    ecs_observer(world, {
        .query.terms = {{ .id = DynamicTransform }},
        .events = { EcsOnRemove },
        .callback = BoxTransformOnDynamicRemove
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_pair(EcsChildOf, EcsWildcard) }},
        .events = { EcsOnAdd },
        .callback = BoxTransformOnDynamicParent
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxTransformDynamicUpdate" }),
        .phase = EcsPostUpdate,
        .query.terms = {
            { .id = ecs_id(WorldTransform) },
            { .id = ecs_id(Position), .oper = EcsOptional, .inout = EcsIn },
            { .id = ecs_id(Rotation), .oper = EcsOptional, .inout = EcsIn },
            { .id = ecs_id(Scale), .oper = EcsOptional, .inout = EcsIn },
            { .id = ecs_id(Orientation), .oper = EcsOptional, .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .oper = EcsOptional, .inout = EcsIn,
                .src.id = EcsUp|EcsCascade, .trav = EcsChildOf },
            { .id = DynamicTransform, .inout = EcsInOutNone },
            { .id = ManualTransform, .oper = EcsNot, .inout = EcsInOutNone }
        },
        .callback = BoxTransformDynamicUpdate
    });
}
