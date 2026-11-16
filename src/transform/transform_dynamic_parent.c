/**
 * @file transform/transform_dynamic_parent.c
 * @brief Transform dynamic entities with Parent component.
 *
 * Only the movers are transformed: the entities whose table box_transform_spread rejects. A
 * mover under another mover finds the world transform of its parent through
 * box_transform_world, computed from the movers above it.
 */

#include "transform.h"

void transformDynamicParent(
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
        const Position *p = ecs_field(&it, Position, 1);
        const Rotation *r = ecs_field(&it, Rotation, 2);
        const Scale *s = ecs_field(&it, Scale, 3);
        const EcsParent *parents = ecs_field(&it, EcsParent, 4);
        ecs_entity_t last = 0;
        const WorldTransform *t_parent = NULL;

        for (int i = 0; i < it.count; i ++) {
            ecs_entity_t parent = parents[i].value;
            if (!i || parent != last) {
                t_parent = transformGetParentWorldTransform(world, parent, &buf);
                last = parent;
            }

            transformSingle(t_parent, &t[i], &p[i], r ? &r[i] : NULL, s ? &s[i] : NULL);
        }
    }
}
