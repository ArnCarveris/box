/**
 * @file tree.c
 * @brief Builds the hierarchies of the transform benchmarks.
 */

#include "tree.h"

ecs_entity_t* bench_tree_new(
    ecs_world_t *world,
    int32_t count,
    int32_t fanout,
    bool parent_component)
{
    ecs_entity_t *tree = ecs_os_malloc_n(ecs_entity_t, count);
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_entity_t parent = i ? tree[(i - 1) / fanout] : 0;
        if (!parent) {
            tree[i] = ecs_new(world);
        } else if (parent_component) {
            tree[i] = ecs_new_w_parent(world, parent, NULL);
        } else {
            tree[i] = ecs_new_w_pair(world, EcsChildOf, parent);
        }

        ecs_set(world, tree[i], Position, {0.1f, 0.2f, 0.3f});
        ecs_set(world, tree[i], Rotation, {0.0f, 0.1f, 0.0f});
    }

    return tree;
}
