/**
 * @file Follow.c
 * @brief Benchmarks for the per frame cost of moving followers along a curve.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/spline/spline_internal.h"

/** Control points of the curve the followers travel */
#define BENCH_FOLLOW_POINTS (33)

/** Followers the benchmark moves every frame */
#define BENCH_FOLLOWERS (10000)

/** Fixed step the benchmark advances the world with */
#define BENCH_STEP (1.0f / 60.0f)

/** Build a curve and a crowd of followers on it.
 *
 * @param world The world
 * @param orient true to turn every follower with the tangent
 * @return The curve entity
 */
static ecs_entity_t box_bench_follow_scene(
    ecs_world_t *world,
    bool orient)
{
    ecs_entity_t curve = ecs_new(world);
    Spline *s = ecs_ensure(world, curve, Spline);
    int32_t i;

    s->loop = true;
    ecs_vec_init_t(NULL, &s->points, vec3, BENCH_FOLLOW_POINTS);
    for (i = 0; i < BENCH_FOLLOW_POINTS; i ++) {
        float a = (float)i * 0.19f;

        ecs_vec_append_t(NULL, &s->points, vec3)[0] =
            vec3_new(a * 3.0f, (float)(i % 4), a * a * 0.1f);
    }

    ecs_modified(world, curve, Spline);

    float length = box_spline_length(world, curve);
    for (i = 0; i < BENCH_FOLLOWERS; i ++) {
        ecs_entity_t e = ecs_new(world);

        ecs_set(world, e, FollowPath, {
            .spline = curve,
            .speed = 4,
            .offset = length * (float)i / (float)BENCH_FOLLOWERS,
            .orient = orient
        });
    }

    return curve;
}

void Follow_followers_10k(bench_t *b)
{
    // splines/spline_paths
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t curve = box_bench_follow_scene(world, false);
    bench_set_items(b, BENCH_FOLLOWERS);

    while (bench_iter(b)) {
        ecs_progress(world, BENCH_STEP);
        bench_keep(box_spline_length(world, curve));
    }

    ecs_fini(world);
}

void Follow_followers_10k_oriented(bench_t *b)
{
    // splines/spline_paths
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t curve = box_bench_follow_scene(world, true);
    bench_set_items(b, BENCH_FOLLOWERS);

    while (bench_iter(b)) {
        ecs_progress(world, BENCH_STEP);
        bench_keep(box_spline_length(world, curve));
    }

    ecs_fini(world);
}
