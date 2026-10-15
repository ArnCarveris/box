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
#ifdef __APPLE__
#include <libproc.h>
#include <unistd.h>
#endif

/** Entities in the deleted hierarchy */
#define BENCH_DELETE_COUNT (100000)

/** Children per node of the deleted hierarchy */
#define BENCH_DELETE_FANOUT (10)

/** Get the instructions the process retired so far.
 *
 * @return The instructions, 0 where the platform does not count them
 */
static uint64_t bench_instructions(void)
{
    uint64_t result = 0;
#ifdef __APPLE__
    struct rusage_info_v4 info;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t*)&info)) {
        result = info.ri_instructions;
    }
#endif

    return result;
}

/** Delete the root of a hierarchy every iteration.
 *
 * On macOS the counters delete_minstr and frame_minstr report the instructions retired by the
 * delete and by the frame after it, in millions per iteration (warm up runs included in the
 * average), which load on the machine does not change.
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
    uint64_t deleted = 0, framed = 0, runs = 0;

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

        uint64_t start = bench_instructions();
        ecs_delete(world, root);
        uint64_t mid = bench_instructions();
        ecs_progress(world, 0);
        framed += bench_instructions() - mid;
        deleted += mid - start;
        runs ++;
    }

#ifdef __APPLE__
    double scale = runs ? (double)bench_iterations(b) / (double)runs / 1e6 : 0;
    bench_counter(b, "delete_minstr", (double)deleted * scale);
    bench_counter(b, "frame_minstr", (double)framed * scale);
#else
    (void)deleted;
    (void)framed;
    (void)runs;
#endif

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
