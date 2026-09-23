/**
 * @file Monitor.c
 * @brief Benchmarks for the per frame cost of each monitor of box.monitor.
 */

#include <box.h>
#include <bake_bench.h>

/** Systems the benchmark world runs every frame */
#define BENCH_SYSTEM_COUNT (32)

/** Entities those systems iterate */
#define BENCH_ENTITY_COUNT (1000)

/** Move every entity of a table so a benchmark system has work to do.
 *
 * @param it Iterator over the entities with a position
 */
static void BoxBenchTouch(
    ecs_iter_t *it)
{
    Position *p = ecs_field(it, Position, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        p[i].x += 1.0f;
    }
}

/** Build a world with a fixed number of systems and entities.
 *
 * @return The world
 */
static ecs_world_t* box_bench_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    int32_t i;
    for (i = 0; i < BENCH_ENTITY_COUNT; i ++) {
        ecs_set(world, ecs_new(world), Position, {0, 0, 0});
    }

    for (i = 0; i < BENCH_SYSTEM_COUNT; i ++) {
        char name[32];
        ecs_os_snprintf(name, sizeof(name), "BoxBenchSystem%d", i);
        ecs_system(world, {
            .entity = ecs_entity(world, { .name = name }),
            .phase = EcsOnUpdate,
            .query.terms = {{ .id = ecs_id(Position) }},
            .callback = BoxBenchTouch
        });
    }

    return world;
}

void Monitor_no_monitor(
    bench_t *b)
{
    // monitoring/cpu_monitor
    ecs_world_t *world = box_bench_world();
    ecs_progress(world, 0.016f);
    bench_set_items(b, BENCH_SYSTEM_COUNT);

    while (bench_iter(b)) {
        ecs_progress(world, 0.016f);
    }

    ecs_fini(world);
}

void Monitor_cpu_monitor(
    bench_t *b)
{
    // monitoring/cpu_monitor
    ecs_world_t *world = box_bench_world();
    ecs_singleton_add(world, CpuMonitor);
    ecs_progress(world, 0.016f);
    bench_set_items(b, BENCH_SYSTEM_COUNT);

    while (bench_iter(b)) {
        ecs_progress(world, 0.016f);
    }

    ecs_fini(world);
}

void Monitor_ecs_stats(
    bench_t *b)
{
    // monitoring/ecs_stats
    ecs_world_t *world = box_bench_world();
    ecs_singleton_set(world, EcsStats, {.period_s = 0.001});
    ecs_progress(world, 0.016f);
    bench_set_items(b, BENCH_SYSTEM_COUNT);

    while (bench_iter(b)) {
        ecs_progress(world, 0.016f);
    }

    ecs_fini(world);
}

void Monitor_system_monitor(
    bench_t *b)
{
    // monitoring/system_monitor
    ecs_world_t *world = box_bench_world();
    ecs_singleton_set(world, SystemMonitor, {.enabled = true});
    ecs_progress(world, 0.016f);
    bench_set_items(b, BENCH_SYSTEM_COUNT);

    while (bench_iter(b)) {
        ecs_progress(world, 0.016f);
    }

    ecs_fini(world);
}

void Monitor_gpu_timings(
    bench_t *b)
{
    // monitoring/gpu_timings
    ecs_world_t *world = box_bench_world();
    ecs_singleton_add(world, GpuTimings);
    ecs_progress(world, 0.016f);
    bench_set_items(b, BENCH_SYSTEM_COUNT);

    while (bench_iter(b)) {
        ecs_progress(world, 0.016f);
    }

    ecs_fini(world);
}

void Monitor_sample_cost(
    bench_t *b)
{
    // monitoring/cpu_monitor
    ecs_world_t *world = box_bench_world();
    ecs_singleton_add(world, CpuMonitor);
    ecs_singleton_set(world, EcsStats, {.period_s = 0.001});
    ecs_singleton_set(world, SystemMonitor, {.enabled = true});
    ecs_progress(world, 0.016f);
    bench_set_items(b, BENCH_SYSTEM_COUNT);

    while (bench_iter(b)) {
        ecs_progress(world, 0.016f);
    }

    ecs_fini(world);
}
