/**
 * @file Scatter.c
 * @brief Benchmarks for Scatter: the evaluation and bulk creation of the instances of a region.
 *
 * A 100 by 100 m region scatters two prefabs at one per square meter on the plane. An iteration
 * changes the seed, so the scatter deletes its 10000 instances and places them anew.
 */

#include <box.h>
#include <bake_bench.h>

/** Edge of the region in meters, at one instance per square meter */
#define BENCH_SCATTER_EDGE (100)

/** Create a world with a scatter over the region and its two prefabs.
 *
 * @param scatter Receives the scatter entity
 * @param config Receives its config
 * @return The world
 */
static ecs_world_t* bench_scatter_world(
    ecs_entity_t *scatter,
    Scatter *config)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t tree = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t bush = ecs_new_w_id(world, EcsPrefab);
    *scatter = ecs_new(world);
    ecs_set(world, *scatter, Position, {0, 0, 0});
    ecs_add(world, *scatter, Scatter);
    *config = *ecs_get(world, *scatter, Scatter);
    config->items[0] = (ScatterItem){ tree, 1 };
    config->items[1] = (ScatterItem){ bush, 3 };
    config->density = 100;
    config->spacing = 0.5f;
    config->extent[0] = BENCH_SCATTER_EDGE;
    config->extent[1] = BENCH_SCATTER_EDGE;
    config->scale_min = 0.8f;
    config->scale_max = 1.2f;

    return world;
}

void Scatter_place_10k(
    bench_t *b)
{
    // scenes/scatter
    ecs_entity_t scatter;
    Scatter config;
    ecs_world_t *world = bench_scatter_world(&scatter, &config);
    ecs_set_ptr(world, scatter, Scatter, &config);
    ecs_progress(world, 0);
    bench_set_items(b, BENCH_SCATTER_EDGE * BENCH_SCATTER_EDGE);
    while (bench_iter(b)) {
        config.seed ++;
        ecs_set_ptr(world, scatter, Scatter, &config);
        ecs_progress(world, 0);
    }

    ecs_fini(world);
}

void Scatter_place_10k_spatial_ignore(
    bench_t *b)
{
    // scenes/scatter
    ecs_entity_t scatter;
    Scatter config;
    ecs_world_t *world = bench_scatter_world(&scatter, &config);
    config.spatial_ignore = true;
    ecs_set_ptr(world, scatter, Scatter, &config);
    ecs_progress(world, 0);
    bench_set_items(b, BENCH_SCATTER_EDGE * BENCH_SCATTER_EDGE);
    while (bench_iter(b)) {
        config.seed ++;
        ecs_set_ptr(world, scatter, Scatter, &config);
        ecs_progress(world, 0);
    }

    ecs_fini(world);
}
