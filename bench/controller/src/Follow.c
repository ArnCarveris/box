/**
 * @file Follow.c
 * @brief Benchmarks for the OnUpdate system that follows targets with cameras.
 */

#include <box.h>
#include <bake_bench.h>

/** Follow cameras and targets the benchmark moves every frame */
#define BENCH_FOLLOW_COUNT (100)

void Follow_update_100_followers(
    bench_t *b)
{
    // ore_engine/follow_camera
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    int32_t i;
    for (i = 0; i < BENCH_FOLLOW_COUNT; i ++) {
        ecs_entity_t target = ecs_new(world);
        ecs_set(world, target, Position, {(float)i, 0, (float)i});
        ecs_entity_t camera = ecs_new(world);
        ecs_set(world, camera, FollowCamera, {
            .target = target,
            .glide = 6,
            .no_collision = true,
            .levels = {
                { .offset = {0, 4, -6}, .look_offset = {0, 1, 0} },
                { .offset = {0, 8, -12}, .look_offset = {0, 2, 0} },
                { .offset = {0, 12, -18}, .look_offset = {0, 3, 0} }
            }
        });
    }

    ecs_progress(world, 0.016f);

    bench_set_items(b, BENCH_FOLLOW_COUNT);

    while (bench_iter(b)) {
        ecs_progress(world, 0.016f);
        bench_keep(ecs_singleton_get(world, Input)->focused);
    }

    ecs_fini(world);
}
