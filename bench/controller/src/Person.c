/**
 * @file Person.c
 * @brief Benchmarks for the OnUpdate system that walks the first person characters.
 */

#include <box.h>
#include <bake_bench.h>

/** Characters the benchmark walks every frame */
#define BENCH_PERSON_COUNT (100)

void Person_update_100_characters(
    bench_t *b)
{
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    int32_t i;
    for (i = 0; i < BENCH_PERSON_COUNT; i ++) {
        ecs_entity_t person = ecs_new(world);
        ecs_add(world, person, FirstPersonController);
        ecs_add(world, person, LookAt);
    }

    box_input_inject(world, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyW });
    box_input_inject(world, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyShiftLeft });
    ecs_progress(world, 0.016f);

    bench_set_items(b, BENCH_PERSON_COUNT);

    while (bench_iter(b)) {
        ecs_progress(world, 0.016f);
        bench_keep(ecs_singleton_get(world, Input)->keys[BoxKeyW].state);
    }

    ecs_fini(world);
}

/** Roots of the scene the character walks through */
#define BENCH_PERSON_ROOTS (50000)

/** Colliders below every root */
#define BENCH_PERSON_FANOUT (20)

/** Roots along one side of the grid the scene is laid out on */
#define BENCH_PERSON_SIDE (224)

/** Distance between two roots of the grid */
#define BENCH_PERSON_STEP (4.0f)

void Person_walk_through_1m_colliders(
    bench_t *b)
{
    // collision/controller_collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    int32_t i, j;
    for (i = 0; i < BENCH_PERSON_ROOTS; i ++) {
        float x = (float)(i % BENCH_PERSON_SIDE) * BENCH_PERSON_STEP;
        float z = (float)(i / BENCH_PERSON_SIDE) * BENCH_PERSON_STEP;
        ecs_entity_t root = ecs_new_w_pair(world, EcsIsA, prefab);
        ecs_set(world, root, Position, {x, 0, z});
        for (j = 0; j < BENCH_PERSON_FANOUT; j ++) {
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

    ecs_entity_t person = ecs_new(world);
    ecs_add(world, person, FirstPersonController);
    ecs_add(world, person, LookAt);
    ecs_set(world, person, Position, {2, 1.7f, 0});
    box_input_inject(world, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyW });
    ecs_progress(world, 0.016f);

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        ecs_progress(world, 0.016f);
        bench_keep(ecs_get(world, person, Position)->z);
    }

    ecs_fini(world);
}
