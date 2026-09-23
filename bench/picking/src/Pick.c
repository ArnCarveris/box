/**
 * @file Pick.c
 * @brief Benchmarks for a pick in a scene of a million nested colliders.
 */

#include <box.h>
#include <bake_bench.h>

/** Roots of the large scene */
#define BENCH_PICK_ROOTS (50000)

/** Colliders below every root */
#define BENCH_PICK_FANOUT (20)

/** Roots along one side of the grid the scene is laid out on */
#define BENCH_PICK_SIDE (224)

/** Distance between two roots of the grid */
#define BENCH_PICK_STEP (4.0f)

/** Cursor positions the sweep of a benchmark walks through */
#define BENCH_PICK_CURSORS (64)

/** Build a scene of nested roots with a collider on every leaf and a camera.
 *
 * @param world The world
 * @param roots Number of roots to create
 */
static void bench_pick_build(
    ecs_world_t *world,
    int32_t roots)
{
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    int32_t i, j;
    for (i = 0; i < roots; i ++) {
        float x = (float)(i % BENCH_PICK_SIDE) * BENCH_PICK_STEP;
        float z = (float)(i / BENCH_PICK_SIDE) * BENCH_PICK_STEP;
        ecs_entity_t root = ecs_new_w_pair(world, EcsIsA, prefab);
        ecs_set(world, root, Position, {x, 0, z});
        for (j = 0; j < BENCH_PICK_FANOUT; j ++) {
            float dx = (float)(j % 5) * 0.6f - 1.2f;
            float dy = (float)(j / 5) * 0.6f;
            ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, root);
            ecs_set(world, child, Position, {dx, dy, 0});
            ecs_set(world, child, SphereCollider, {{0, 0, 0}, 0.25f});
            ecs_set(world, child, AABB, {
                {x + dx - 0.25f, dy - 0.25f, z - 0.25f},
                {x + dx + 0.25f, dy + 0.25f, z + 0.25f}});
        }
    }

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0.2f, -20});
    ecs_set(world, camera, Camera, {.fov = 60, .aspect_ratio = 1});
    ecs_progress(world, 0);
}

void Pick_ray_from_a_camera(
    bench_t *b)
{
    // picking/picking
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, -10});
    ecs_set(world, camera, Camera, {.fov = 60, .aspect_ratio = 1});
    ecs_progress(world, 0);

    bench_set_items(b, 1);
    int32_t step = 0;

    while (bench_iter(b)) {
        float x = (float)(step ++ % BENCH_PICK_CURSORS) / BENCH_PICK_CURSORS;
        vec3 origin, dir;
        box_picking_ray(world, 0, (vec2){x, x}, &origin, &dir);
        bench_keep(dir.x);
    }

    ecs_fini(world);
}

void Pick_pick_1m(
    bench_t *b)
{
    // picking/picking
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    bench_pick_build(world, BENCH_PICK_ROOTS);

    bench_set_items(b, 1);
    int32_t step = 0;

    while (bench_iter(b)) {
        float x = (float)(step ++ % BENCH_PICK_CURSORS) / BENCH_PICK_CURSORS;
        PickHit hit;
        box_picking_pick(world, 0, (vec2){x, 0}, &hit);
        bench_keep(hit.entity);
    }

    ecs_fini(world);
}

void Pick_pick_miss_1m(
    bench_t *b)
{
    // picking/picking
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    bench_pick_build(world, BENCH_PICK_ROOTS);

    bench_set_items(b, 1);
    int32_t step = 0;

    while (bench_iter(b)) {
        float x = (float)(step ++ % BENCH_PICK_CURSORS) / BENCH_PICK_CURSORS;
        PickHit hit;
        box_picking_pick(world, 0, (vec2){x, 1}, &hit);
        bench_keep(hit.entity);
    }

    ecs_fini(world);
}

void Pick_target_of_a_nested_hit(
    bench_t *b)
{
    // picking/picking
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {0, 0, 0});
    ecs_add(world, root, RootBounds);
    ecs_entity_t leaf = root;
    int32_t i;
    for (i = 0; i < 8; i ++) {
        leaf = ecs_new_w_pair(world, EcsChildOf, leaf);
        ecs_set(world, leaf, Position, {0, 0, 0});
    }

    ecs_set(world, leaf, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(world, 0);

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        bench_keep(box_picking_target(world, leaf));
    }

    ecs_fini(world);
}

void Pick_raise_to_an_empty_chain(
    bench_t *b)
{
    // picking/picking
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    int32_t i;
    for (i = 0; i < 8; i ++) {
        e = ecs_new_w_pair(world, EcsChildOf, e);
    }

    PickHit hit = { .entity = e };
    bench_set_items(b, 1);

    while (bench_iter(b)) {
        bench_keep(box_picking_raise(world, PickClick, &hit));
    }

    ecs_fini(world);
}
