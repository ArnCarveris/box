/**
 * @file Hierarchy.c
 * @brief Benchmarks that compare the ChildOf pair with the Parent component.
 */

#include <box.h>
#include <bake_bench.h>

/** Entities in the compared tree */
#define BENCH_HIERARCHY_COUNT (100000)

/** Children per node of the compared tree */
#define BENCH_HIERARCHY_FANOUT (50)

/** Entities in the dynamic subtree */
#define BENCH_HIERARCHY_SUBTREE (1000)

/** Build a tree of the configured size.
 *
 * @param world The world
 * @param tree Array that receives the entities
 * @param count Entities to create
 * @param parent_component Use the Parent component instead of a ChildOf pair
 */
static void bench_hierarchy_build(
    ecs_world_t *world,
    ecs_entity_t *tree,
    int32_t count,
    bool parent_component)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        if (!i) {
            tree[i] = ecs_new(world);
        } else if (parent_component) {
            tree[i] = ecs_new_w_parent(
                world, tree[(i - 1) / BENCH_HIERARCHY_FANOUT], NULL);
        } else {
            tree[i] = ecs_new_w_pair(world, EcsChildOf,
                tree[(i - 1) / BENCH_HIERARCHY_FANOUT]);
        }

        ecs_set(world, tree[i], Position, {0.1f, 0.2f, 0.3f});
    }
}

/** Measure the time to build a tree and report the tables it created.
 *
 * @param b The benchmark
 * @param parent_component Use the Parent component instead of a ChildOf pair
 */
static void bench_hierarchy_build_case(
    bench_t *b,
    bool parent_component)
{
    double tables = 0;

    bench_set_items(b, BENCH_HIERARCHY_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        ecs_entity_t *tree = ecs_os_malloc_n(ecs_entity_t, BENCH_HIERARCHY_COUNT);
        bench_resume(b);

        bench_hierarchy_build(world, tree, BENCH_HIERARCHY_COUNT, parent_component);

        bench_pause(b);
        tables = (double)ecs_get_world_info(world)->table_count;
        bench_keep(tree[BENCH_HIERARCHY_COUNT - 1]);
        ecs_os_free(tree);
        ecs_fini(world);
        bench_resume(b);
    }

    bench_counter(b, "tables", tables);
}

/** Measure the propagation of a root move through a tree.
 *
 * @param b The benchmark
 * @param parent_component Use the Parent component instead of a ChildOf pair
 */
static void bench_hierarchy_propagate_case(
    bench_t *b,
    bool parent_component)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t *tree = ecs_os_malloc_n(ecs_entity_t, BENCH_HIERARCHY_COUNT);
    bench_hierarchy_build(world, tree, BENCH_HIERARCHY_COUNT, parent_component);
    double tables = (double)ecs_get_world_info(world)->table_count;

    float x = 0;

    bench_set_items(b, BENCH_HIERARCHY_COUNT);

    while (bench_iter(b)) {
        x += 1.0f;
        ecs_set(world, tree[0], Position, {x, 0.2f, 0.3f});
        const WorldTransform *wt = ecs_get(world,
            tree[BENCH_HIERARCHY_COUNT - 1], WorldTransform);
        bench_keep(wt);
    }

    bench_counter(b, "tables", tables);
    ecs_os_free(tree);
    ecs_fini(world);
}

void Hierarchy_build_100k_childof(bench_t *b)
{
    // scene_core/parent_hierarchy
    bench_hierarchy_build_case(b, false);
}

void Hierarchy_build_100k_parent(bench_t *b)
{
    // scene_core/parent_hierarchy
    bench_hierarchy_build_case(b, true);
}

void Hierarchy_propagate_root_childof(bench_t *b)
{
    // scene_core/parent_hierarchy
    bench_hierarchy_propagate_case(b, false);
}

void Hierarchy_propagate_root_parent(bench_t *b)
{
    // scene_core/parent_hierarchy
    bench_hierarchy_propagate_case(b, true);
}

void Hierarchy_dynamic_subtree_parent(bench_t *b)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t *tree = ecs_os_malloc_n(ecs_entity_t, BENCH_HIERARCHY_SUBTREE);
    bench_hierarchy_build(world, tree, BENCH_HIERARCHY_SUBTREE, true);

    int32_t i;
    for (i = 0; i < BENCH_HIERARCHY_SUBTREE; i ++) {
        ecs_set(world, tree[i], Rotation, {0.1f, 0.2f, 0.3f});
    }

    ecs_add(world, tree[0], DynamicTransform);
    ecs_progress(world, 0.016f);
    double tables = (double)ecs_get_world_info(world)->table_count;

    bench_set_items(b, BENCH_HIERARCHY_SUBTREE);

    while (bench_iter(b)) {
        ecs_progress(world, 0.016f);
        const WorldTransform *wt = ecs_get(world,
            tree[BENCH_HIERARCHY_SUBTREE - 1], WorldTransform);
        bench_keep(wt);
    }

    bench_counter(b, "tables", tables);
    ecs_os_free(tree);
    ecs_fini(world);
}
