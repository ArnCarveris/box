/**
 * @file Movement.c
 * @brief Benchmark for a frame with 100k entities moving by Velocity.
 */

#include <box.h>
#include <bake_bench.h>

void Movement_moving_100k(
    bench_t *b)
{
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    for (int i = 0; i < 100000; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)i, 0, 0});
        ecs_set(world, e, Rotation, {0, 0, 0});
        ecs_set(world, e, Velocity, {1, 2, 3});
        ecs_set(world, e, AngularVelocity, {0.1f, 0.2f, 0.3f});
        ecs_add(world, e, DynamicTransform);
    }

    ecs_progress(world, 0.016f);

    while (bench_iter(b)) {
        ecs_progress(world, 0.016f);
    }

    ecs_fini(world);
}
