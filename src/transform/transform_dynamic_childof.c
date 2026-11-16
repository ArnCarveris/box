/**
 * @file transform/transform_dynamic_childof.c
 * @brief Transform dynamic entities with ChildOf relationship.
 *
 * Only the movers are transformed, as with Parent. The world transform of a parent whose own is
 * not kept comes from box_transform_world.
 */

#include "transform.h"

void transformDynamicChildOf(
    ecs_world_t *world,
    ecs_query_t *query,
    uint64_t depth)
{
    WorldTransform buf;
    ecs_iter_t it = ecs_query_iter(world, query);
    ecs_iter_set_group(&it, depth);
    while (ecs_query_next(&it)) {
        if (box_transform_spread(world, it.table)) {
            continue;
        }

        WorldTransform *t = ecs_field(&it, WorldTransform, 0);
        const WorldTransform *t_parent = ecs_field(&it, WorldTransform, 4);
        const Position *p = ecs_field(&it, Position, 1);
        const Rotation *r = ecs_field(&it, Rotation, 2);
        const Scale *s = ecs_field(&it, Scale, 3);
        ecs_entity_t src = t_parent ? ecs_field_src(&it, 4) : 0;
        if (src && box_transform_spread(world, ecs_get_table(world, src))) {
            t_parent = box_transform_world(world, src, &buf);
        }

        for (int i = 0; i < it.count; i ++) {
            transformSingle(
                t_parent, &t[i], &p[i], r ? &r[i] : NULL, s ? &s[i] : NULL);
        }
    }
}
