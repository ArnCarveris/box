/**
 * @file LongMove.c
 * @brief Benchmarks for controller moves of 100 units in one 0.5 second frame.
 *
 * A first person sized capsule stands on a 400 x 400 slab next to a 400 unit long wall and
 * moves at 200 units per second, the speed of a boosted camera, into the wall at an angle (it
 * slides along it) or along the slab.
 */

#include <box.h>
#include <bake_bench.h>
#include "controllers.h"
#include <math.h>

/** Moves per iteration of a case */
#define BENCH_LONG_MOVES (100)

/** Seconds of a frame */
#define BENCH_LONG_DT (0.5f)

/** Units per second of a move */
#define BENCH_LONG_SPEED (200.0f)

/** Create a world with a slab to stand on and a wall along X at z = -2, and index them.
 *
 * @return The world
 */
static ecs_world_t* bench_long_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t slab = ecs_new(world);
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, slab, Position, { 0, -0.5f, 0 });
    ecs_set(world, slab, Box, { 400, 1, 400 });
    ecs_set(world, wall, Position, { 0, 1, -2 });
    ecs_set(world, wall, Box, { 400, 2, 0.2f });
    ecs_progress(world, 0);

    return world;
}

/** Move a walking body from the origin by a velocity over a frame, many times.
 *
 * @param b The bench
 * @param velocity The velocity of every move
 */
static void bench_long_run(
    bench_t *b,
    const vec3 velocity)
{
    ecs_world_t *world = bench_long_world();
    box_controller_body_t body = {
        .radius = 0.3f, .height = 1.85f, .step = 0.3f, .probe = 0.3f, .upright = true
    };
    Position p = { 0 };
    int32_t i;
    double run = 0;
    bench_set_items(b, BENCH_LONG_MOVES);
    while (bench_iter(b)) {
        for (i = 0; i < BENCH_LONG_MOVES; i ++) {
            vec3 v = { velocity[0], velocity[1], velocity[2] };
            p = (Position){ 0, 0, 0 };
            box_controller_move(world, &body, &p, v, BENCH_LONG_DT, NULL);
            run ++;
        }

        bench_keep(p.x);
    }

    bench_counter(b, "x", p.x);
    bench_counter(b, "z", p.z);
    bench_counter(b, "moves_run", run);
    ecs_fini(world);
}

void LongMove_wall(
    bench_t *b)
{
    // controller_fixes/tunneling
    vec3 v = { BENCH_LONG_SPEED * 0.5f, 0, -BENCH_LONG_SPEED * 0.866f };
    bench_long_run(b, v);
}

void LongMove_ground(
    bench_t *b)
{
    // controller_fixes/tunneling
    vec3 v = { BENCH_LONG_SPEED, 0, 0 };
    bench_long_run(b, v);
}
