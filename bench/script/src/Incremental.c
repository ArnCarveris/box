/**
 * @file Incremental.c
 * @brief Benchmarks for the instantiation throughput and the ordering cost.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/script/script_internal.h"

/** Instances the throughput case expands per iteration */
#define BENCH_SCRIPT_INSTANCES (10000)

/** Instances the ordering case keeps pending */
#define BENCH_ORDER_INSTANCES (100000)

static const char *bench_script_code =
    "using box.transform\n"
    "template Cluster {\n"
    "  base {\n"
    "    Position: {0, 0.4, 0}\n"
    "    Rotation: {0, 0, 0}\n"
    "  }\n"
    "  top {\n"
    "    Position: {0, 1.2, 0}\n"
    "    Rotation: {0, 1, 0}\n"
    "  }\n"
    "}\n";

void Incremental_instantiate_10k(bench_t *b)
{
    // responsiveness/incremental_templates
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ScriptSettings *settings = ecs_singleton_ensure(world, ScriptSettings);
    settings->enabled = false;
    ecs_singleton_modified(world, ScriptSettings);
    ecs_script_run(world, "bench", bench_script_code, NULL);

    ecs_entity_t tmpl = ecs_lookup(world, "Cluster");
    const ScriptState *ss = ecs_get(world, ecs_id(ScriptState), ScriptState);
    ScriptStats *stats = ecs_singleton_ensure(world, ScriptStats);
    bench_set_items(b, BENCH_SCRIPT_INSTANCES);

    while (bench_iter(b)) {
        bench_pause(b);
        ecs_entity_t scope = ecs_new(world);
        int32_t i;
        for (i = 0; i < BENCH_SCRIPT_INSTANCES; i ++) {
            ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, scope);
            ecs_set(world, e, Position, {(float)i, 0, 0});
            ecs_add_id(world, e, tmpl);
        }

        bench_resume(b);
        box_script_drain(world, ss->state, settings, stats);
        bench_pause(b);
        ecs_delete(world, scope);
        bench_resume(b);
        bench_keep(stats->instantiated);
    }

    ecs_fini(world);
}

void Incremental_order_100k(bench_t *b)
{
    // responsiveness/incremental_templates
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ScriptSettings *settings = ecs_singleton_ensure(world, ScriptSettings);
    settings->templates_per_frame = 1;
    settings->budget_ms = 0.0001;
    ecs_singleton_modified(world, ScriptSettings);
    ecs_script_run(world, "bench", bench_script_code, NULL);

    ecs_entity_t tmpl = ecs_lookup(world, "Cluster");
    ecs_entity_t eye = ecs_new(world);
    ecs_set(world, eye, Position, {0, 0, 0});
    ecs_set(world, eye, Camera, {.fov = 60});

    int32_t i;
    for (i = 0; i < BENCH_ORDER_INSTANCES; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)(i % 320), 0, (float)(i / 320)});
        ecs_add_id(world, e, tmpl);
    }

    const ScriptState *ss = ecs_get(world, ecs_id(ScriptState), ScriptState);
    ScriptStats *stats = ecs_singleton_ensure(world, ScriptStats);
    bench_set_items(b, BENCH_ORDER_INSTANCES);

    while (bench_iter(b)) {
        bench_pause(b);
        ss->state->dirty = true;
        bench_resume(b);
        box_script_drain(world, ss->state, settings, stats);
        bench_keep(stats->pending);
    }

    ecs_fini(world);
}
