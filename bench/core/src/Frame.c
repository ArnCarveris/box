/**
 * @file Frame.c
 * @brief Benchmark for the fixed cost of a frame of a world with every box module.
 */

#include <box.h>
#include <bake_bench.h>

void Frame_idle(
    bench_t *b)
{
    // testing/bench
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_progress(world, 0);

    while (bench_iter(b)) {
        ecs_progress(world, 0);
    }

    ecs_fini(world);
}
