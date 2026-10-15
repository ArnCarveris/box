/**
 * @file Delete.c
 * @brief Benchmarks for deleting a whole transform hierarchy.
 *
 * Each iteration builds a hierarchy of BENCH_DELETE_COUNT entities outside the timed region,
 * then deletes its root and runs the frame that follows.
 *
 * In the target variants every node with children is the target of a relationship, so flecs
 * deletes their children one by one instead of through the path for nodes that only parent.
 */

#include "tree.h"
#include <bake_bench.h>

/** Entities in the deleted hierarchy */
#define BENCH_DELETE_COUNT (100000)

/** Children per node of the deleted hierarchy */
#define BENCH_DELETE_FANOUT (10)

/** Delete the root of a hierarchy every iteration.
 *
 * @param b The benchmark
 * @param parent_component Use the Parent component instead of a ChildOf pair
 * @param dynamic Add DynamicTransform to the root
 * @param target Make every node with children the target of a relationship
 */
static void bench_delete_tree(
    bench_t *b,
    bool parent_component,
    bool dynamic,
    bool target)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t rel = ecs_new(world);
    ecs_progress(world, 0);

    bench_set_items(b, BENCH_DELETE_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        ecs_entity_t *tree = bench_tree_new(
            world, BENCH_DELETE_COUNT, BENCH_DELETE_FANOUT, parent_component);
        ecs_entity_t root = tree[0];
        if (dynamic) {
            ecs_add(world, root, DynamicTransform);
        }

        for (int32_t i = 0; target && i < BENCH_DELETE_COUNT / BENCH_DELETE_FANOUT; i ++) {
            ecs_add_pair(world, tree[i], rel, tree[i]);
        }

        ecs_progress(world, 0);
        ecs_os_free(tree);
        bench_resume(b);

        ecs_delete(world, root);
        ecs_progress(world, 0);
    }

    ecs_fini(world);
}

void Delete_static_childof(
    bench_t *b)
{
    // core_fixes/delete_observers
    bench_delete_tree(b, false, false, false);
}

void Delete_static_parent(
    bench_t *b)
{
    // core_fixes/delete_observers
    bench_delete_tree(b, true, false, false);
}

void Delete_dynamic_childof(
    bench_t *b)
{
    // core_fixes/delete_observers
    bench_delete_tree(b, false, true, false);
}

void Delete_dynamic_parent(
    bench_t *b)
{
    // core_fixes/delete_observers
    bench_delete_tree(b, true, true, false);
}

void Delete_static_parent_target(
    bench_t *b)
{
    // core_fixes/delete_observers
    bench_delete_tree(b, true, false, true);
}

void Delete_dynamic_parent_target(
    bench_t *b)
{
    // core_fixes/delete_observers
    bench_delete_tree(b, true, true, true);
}
