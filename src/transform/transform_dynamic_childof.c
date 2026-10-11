/**
 * @file transform/transform_dynamic_childof.c
 * @brief Transform dynamic entities with ChildOf relationship.
 */

#include "transform.h"

void transformDynamicChildOf(
    ecs_world_t *world,
    ecs_query_t *query,
    uint64_t depth)
{
    ecs_iter_t it = ecs_query_iter(world, query);
    ecs_iter_set_group(&it, depth);
    while (ecs_query_next(&it)) {
        WorldTransform *t = ecs_field(&it, WorldTransform, 0);
        const WorldTransform *t_parent = ecs_field(&it, WorldTransform, 4);
        const Position *p = ecs_field(&it, Position, 1);
        const Rotation *r = ecs_field(&it, Rotation, 2);
        const Scale *s = ecs_field(&it, Scale, 3);

        for (int i = 0; i < it.count; i ++) {
            transformSingle(
                t_parent, &t[i], &p[i], r ? &r[i] : NULL, s ? &s[i] : NULL);
        }
    }
}
