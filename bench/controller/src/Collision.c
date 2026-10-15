/**
 * @file Collision.c
 * @brief Benchmarks for controller collision in a scene with one million static shapes.
 *
 * Boxes, spheres, cylinders and cones stand on a 1000 x 1000 grid with a spacing of 1.5 units.
 * Move cases move first person sized capsules over the grid (the fast case 100 units in one 0.5
 * second frame), the frame case walks one through it.
 */

#include <box.h>
#include <bake_bench.h>
#include "controllers.h"
#include <math.h>

/** Shapes per row of the grid */
#define BENCH_ROW (1000)

/** Static shapes of the scene */
#define BENCH_STATIC (BENCH_ROW * BENCH_ROW)

/** Distance between grid points */
#define BENCH_SPACING (1.5f)

/** Moves per iteration of a move case */
#define BENCH_MOVES (1000)

/** Seconds of a frame */
#define BENCH_DT (1.0f / 60.0f)

/** Moves per iteration of the fast move case */
#define BENCH_FAST_MOVES (10)

/** Units per second of the fast move case, a boosted camera */
#define BENCH_FAST_SPEED (200.0f)

/** Seconds of a frame of the fast move case */
#define BENCH_FAST_DT (0.5f)

/** Get a random number between 0 and 1.
 *
 * @param seed The state of the generator
 * @return The number
 */
static float bench_random(
    uint32_t *seed)
{
    *seed = *seed * 1664525u + 1013904223u;

    return (float)(*seed >> 8) / 16777216.0f;
}

/** Create a world with the shapes of the grid standing on the ground and index them.
 *
 * @return The world
 */
static ecs_world_t* bench_collision_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    uint32_t seed = 1;
    int32_t i;
    for (i = 0; i < BENCH_STATIC; i ++) {
        ecs_entity_t e = ecs_new(world);
        float s = 0.3f + bench_random(&seed) * 0.6f;
        float h = s * (1.0f + bench_random(&seed) * 2.0f);
        ecs_set(world, e, Position, {(float)(i % BENCH_ROW) * BENCH_SPACING, h * 0.5f,
            (float)(i / BENCH_ROW) * BENCH_SPACING});
        if (i % 4 == 0) {
            ecs_set(world, e, Box, {s, h, s});
        } else if (i % 4 == 1) {
            ecs_set(world, e, Sphere, { .segments = 24, .radius = s * 0.5f });
        } else if (i % 4 == 2) {
            ecs_set(world, e, Cylinder, { .segments = 24, .radius = s * 0.5f, .length = h });
        } else {
            ecs_set(world, e, Cone, { .segments = 24, .radius = s * 0.5f, .length = h });
        }
    }

    ecs_progress(world, 0);

    return world;
}

void Collision_move_1m(
    bench_t *b)
{
    // controllers/collision
    ecs_world_t *world = bench_collision_world();
    box_controller_body_t body = { .radius = 0.3f, .height = 1.85f, .step = 0.3f, .probe = 0.3f };
    Position *feet = ecs_os_malloc_n(Position, BENCH_MOVES);
    vec3 *dir = ecs_os_malloc_n(vec3, BENCH_MOVES);
    float extent = BENCH_ROW * BENCH_SPACING;
    uint32_t seed = 7;
    int32_t i, blocked = 0;
    for (i = 0; i < BENCH_MOVES; i ++) {
        float a = bench_random(&seed) * 6.2832f;
        feet[i] = (Position){ bench_random(&seed) * extent, 0, bench_random(&seed) * extent };
        dir[i][0] = 4.0f * cosf(a);
        dir[i][1] = 0;
        dir[i][2] = 4.0f * sinf(a);
    }

    bench_set_items(b, BENCH_MOVES);
    while (bench_iter(b)) {
        blocked = 0;
        for (i = 0; i < BENCH_MOVES; i ++) {
            Position p = feet[i];
            vec3 v = { dir[i][0], dir[i][1], dir[i][2] };
            box_controller_hit_t hit;
            box_controller_move(world, &body, &p, v, BENCH_DT, &hit);
            blocked += hit.blocked;
        }

        bench_keep(blocked);
    }

    bench_counter(b, "blocked_pct", 100.0 * blocked / BENCH_MOVES);
    ecs_os_free(dir);
    ecs_os_free(feet);
    ecs_fini(world);
}

void Collision_move_blocked_1m(
    bench_t *b)
{
    // controllers/collision
    ecs_world_t *world = bench_collision_world();
    box_controller_body_t body = { .radius = 0.3f, .height = 1.85f, .step = 0.3f, .probe = 0.3f };
    Position *feet = ecs_os_malloc_n(Position, BENCH_MOVES);
    uint32_t seed = 9;
    int32_t i, blocked = 0;
    for (i = 0; i < BENCH_MOVES; i ++) {
        int32_t cell = (int32_t)(bench_random(&seed) * (BENCH_STATIC - 1));
        feet[i] = (Position){ (float)(cell % BENCH_ROW) * BENCH_SPACING - 0.75f, 0,
            (float)(cell / BENCH_ROW) * BENCH_SPACING };
    }

    bench_set_items(b, BENCH_MOVES);
    while (bench_iter(b)) {
        blocked = 0;
        for (i = 0; i < BENCH_MOVES; i ++) {
            Position p = feet[i];
            vec3 v = { 4, 0, 0 };
            box_controller_hit_t hit;
            box_controller_move(world, &body, &p, v, 0.1f, &hit);
            blocked += hit.blocked;
        }

        bench_keep(blocked);
    }

    bench_counter(b, "blocked_pct", 100.0 * blocked / BENCH_MOVES);
    ecs_os_free(feet);
    ecs_fini(world);
}

void Collision_frame_1m(
    bench_t *b)
{
    // controllers/collision
    ecs_world_t *world = bench_collision_world();
    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Position, { BENCH_ROW * BENCH_SPACING * 0.5f + 0.75f, 0,
        BENCH_ROW * BENCH_SPACING * 0.5f + 0.75f });
    ecs_add(world, fp, FirstPersonController);
    box_input_key(world, KeyW, true);
    ecs_progress(world, BENCH_DT);
    float yaw = 0;
    while (bench_iter(b)) {
        yaw += 0.01f;
        ecs_set(world, fp, Rotation, { 0, yaw, 0 });
        ecs_progress(world, BENCH_DT);
    }

    const Position *p = ecs_get(world, fp, Position);
    bench_counter(b, "x", p->x);
    bench_counter(b, "z", p->z);
    ecs_fini(world);
}

void Collision_fast_move_1m(
    bench_t *b)
{
    // controller_fixes/substep_colliders
    ecs_world_t *world = bench_collision_world();
    box_controller_body_t body = {
        .radius = 0.3f, .height = 1.85f, .step = 0.3f, .probe = 0.3f, .upright = true
    };
    Position *feet = ecs_os_malloc_n(Position, BENCH_FAST_MOVES);
    vec3 *dir = ecs_os_malloc_n(vec3, BENCH_FAST_MOVES);
    float extent = BENCH_ROW * BENCH_SPACING;
    uint32_t seed = 11;
    int32_t i;
    float travelled = 0;
    for (i = 0; i < BENCH_FAST_MOVES; i ++) {
        float a = bench_random(&seed) * 6.2832f;
        feet[i] = (Position){ 100 + bench_random(&seed) * (extent - 200), 0,
            100 + bench_random(&seed) * (extent - 200) };
        dir[i][0] = BENCH_FAST_SPEED * cosf(a);
        dir[i][1] = 0;
        dir[i][2] = BENCH_FAST_SPEED * sinf(a);
    }

    bench_set_items(b, BENCH_FAST_MOVES);
    while (bench_iter(b)) {
        travelled = 0;
        for (i = 0; i < BENCH_FAST_MOVES; i ++) {
            Position p = feet[i];
            vec3 v = { dir[i][0], dir[i][1], dir[i][2] };
            box_controller_move(world, &body, &p, v, BENCH_FAST_DT, NULL);
            travelled += fabsf(p.x - feet[i].x) + fabsf(p.z - feet[i].z);
        }

        bench_keep(travelled);
    }

    bench_counter(b, "travelled", travelled / BENCH_FAST_MOVES);
    ecs_os_free(dir);
    ecs_os_free(feet);
    ecs_fini(world);
}
