/**
 * @file Person.c
 * @brief Benchmarks for the OnUpdate system that walks the first person characters.
 */

#include <box.h>
#include <bake_bench.h>

/** Characters the benchmark walks every frame */
#define BENCH_PERSON_COUNT (100)

void Person_update_100_characters(bench_t *b)
{
    // controllers/first_person
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxController);

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
