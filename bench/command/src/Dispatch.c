/**
 * @file Dispatch.c
 * @brief Benchmarks for running a command line and for frames with open shell sessions.
 */

#include <box.h>
#include <bake_bench.h>

void Dispatch_run_i32_arg(
    bench_t *b)
{
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);

    while (bench_iter(b)) {
        ecs_os_free(box_command_run(world, s, "logs error 1", NULL));
    }

    ecs_fini(world);
}

void Dispatch_frame_no_sessions(
    bench_t *b)
{
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_progress(world, 0);

    while (bench_iter(b)) {
        ecs_progress(world, 0);
    }

    ecs_fini(world);
}

void Dispatch_frame_16_sessions(
    bench_t *b)
{
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    int32_t i;
    for (i = 0; i < 16; i ++) {
        ecs_shell_session_init(world, NULL);
    }

    ecs_progress(world, 0);

    while (bench_iter(b)) {
        ecs_progress(world, 0);
    }

    ecs_fini(world);
}
