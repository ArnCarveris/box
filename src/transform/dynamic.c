/**
 * @file transform/dynamic.c
 * @brief System that recomputes the world transform of dynamic entities.
 *
 * Entities with DynamicTransform on themselves or an ancestor are updated every
 * frame in PostUpdate, parents before children, without emitting events.
 */

#include <box.h>
#include "transform_internal.h"

/** Recompute the world transform of dynamic entities.
 *
 * @param it Iterator over the dynamic entities
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

void box_transform_dynamic_init(
    ecs_world_t *world)
{
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
            { .id = DynamicTransform, .src.id = EcsSelf|EcsUp,
                .trav = EcsChildOf, .inout = EcsInOutNone },
            { .id = ManualTransform, .oper = EcsNot, .inout = EcsInOutNone }
        },
        .callback = BoxTransformDynamicUpdate
    });
}
