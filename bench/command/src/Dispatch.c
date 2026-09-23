/**
 * @file Dispatch.c
 * @brief Benchmarks for running command lines on a terminal session.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/command/command_internal.h"

/** Children of the scope that ls lists */
#define BENCH_SCOPE_COUNT (1000)

void Dispatch_help(
    bench_t *b)
{
    // project_tool/bench
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t session = ecs_entity(world, { .name = "bench" });
    ecs_add(world, session, Terminal);

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        char *out = box_command_run(world, session, "help", NULL);
        bench_keep(out);
        ecs_os_free(out);
    }

    ecs_fini(world);
}

void Dispatch_ls_scope(
    bench_t *b)
{
    // project_tool/bench
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t scope = ecs_entity(world, { .name = "scene" });
    int32_t i;
    for (i = 0; i < BENCH_SCOPE_COUNT; i ++) {
        char name[32];
        ecs_os_snprintf(name, sizeof(name), "item_%d", i);
        ecs_entity_t child = ecs_entity(world, { .name = name });
        ecs_add_pair(world, child, EcsChildOf, scope);
        ecs_set(world, child, Position, {(float)i, 0, 0});
    }

    ecs_entity_t session = ecs_entity(world, { .name = "bench" });
    ecs_add(world, session, Terminal);

    bench_set_items(b, BENCH_SCOPE_COUNT);

    while (bench_iter(b)) {
        char *out = box_command_run(world, session, "ls scene", NULL);
        bench_keep(out);
        ecs_os_free(out);
    }

    ecs_fini(world);
}

void Dispatch_path_lookup(
    bench_t *b)
{
    // project_tool/bench
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity(world, { .name = "one.two.three.four.five" });
    ecs_entity_t session = ecs_entity(world, { .name = "bench" });
    ecs_add(world, session, Terminal);

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        char *out = box_command_run(
            world, session, "ls one.two.three.four.five", NULL);
        bench_keep(out);
        ecs_os_free(out);
    }

    ecs_fini(world);
}

void Dispatch_set_expression(
    bench_t *b)
{
    // project_tool/bench
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t thing = ecs_entity(world, { .name = "thing" });
    ecs_add(world, thing, Position);
    ecs_entity_t session = ecs_entity(world, { .name = "bench" });
    ecs_add(world, session, Terminal);

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        char *out = box_command_run(
            world, session, "set thing Position {1,2,3}", NULL);
        bench_keep(out);
        ecs_os_free(out);
    }

    ecs_fini(world);
}
