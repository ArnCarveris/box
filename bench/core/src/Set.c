/**
 * @file Set.c
 * @brief Benchmarks for ecs_set and ecs_modified of a component without observers in a world
 * with every box module, which pay for every world wide OnSet observer.
 */

#include <box.h>
#include <bake_bench.h>

#define BENCH_SET_COUNT (100000)

/** A component without hooks or observers. */
typedef struct BenchValue {
    float x, y;
} BenchValue;

/** Create entities with BenchValue in a world with every box module.
 *
 * @param world The world
 * @param component Out: the BenchValue component
 * @return The entities, freed by the caller
 */
static ecs_entity_t* bench_set_entities(
    ecs_world_t *world,
    ecs_entity_t *component)
{
    ECS_IMPORT(world, Box);
    ECS_COMPONENT(world, BenchValue);
    ecs_entity_t *entities = ecs_os_malloc_n(ecs_entity_t, BENCH_SET_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_SET_COUNT; i ++) {
        entities[i] = ecs_new(world);
        ecs_set(world, entities[i], BenchValue, {0, 0});
    }

    *component = ecs_id(BenchValue);
    return entities;
}

void Set_set_100k(
    bench_t *b)
{
    // testing/bench
    ecs_world_t *world = ecs_init();
    ecs_entity_t component;
    ecs_entity_t *entities = bench_set_entities(world, &component);
    BenchValue value = {1, 2};
    int32_t i;
    bench_set_items(b, BENCH_SET_COUNT);

    while (bench_iter(b)) {
        for (i = 0; i < BENCH_SET_COUNT; i ++) {
            value.x = (float)i;
            ecs_set_id(world, entities[i], component, sizeof(BenchValue), &value);
        }
    }

    ecs_os_free(entities);
    ecs_fini(world);
}

void Set_modified_100k(
    bench_t *b)
{
    // testing/bench
    ecs_world_t *world = ecs_init();
    ecs_entity_t component;
    ecs_entity_t *entities = bench_set_entities(world, &component);
    int32_t i;
    bench_set_items(b, BENCH_SET_COUNT);

    while (bench_iter(b)) {
        for (i = 0; i < BENCH_SET_COUNT; i ++) {
            BenchValue *v = ecs_get_mut_id(world, entities[i], component);
            v->x += 1;
            ecs_modified_id(world, entities[i], component);
        }
    }

    ecs_os_free(entities);
    ecs_fini(world);
}
