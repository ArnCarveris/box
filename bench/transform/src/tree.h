/**
 * @file tree.h
 * @brief Hierarchy shapes the transform benchmarks propagate through.
 */

#ifndef BENCH_TRANSFORM_TREE_H
#define BENCH_TRANSFORM_TREE_H

#include <box.h>

/** Entities in the deep hierarchy, a chain with one child per level, kept below the
 * FLECS_DAG_DEPTH_MAX of 128 that a dynamic Parent chain is limited to */
#define BENCH_DEEP_COUNT (100)

/** Entities in the wide hierarchy, one root with every other entity as its child */
#define BENCH_WIDE_COUNT (10000)

/** Create a hierarchy where entity i is a child of entity (i - 1) / fanout.
 *
 * Every entity gets a Position and a Rotation. A fanout of 1 builds a chain, a fanout
 * of count builds a root with count - 1 children.
 *
 * @param world The world
 * @param count Entities to create
 * @param fanout Children per node
 * @param parent_component Use the Parent component instead of a ChildOf pair
 * @return Array of the entities in creation order, freed with ecs_os_free
 */
ecs_entity_t* bench_tree_new(
    ecs_world_t *world,
    int32_t count,
    int32_t fanout,
    bool parent_component);

#endif
