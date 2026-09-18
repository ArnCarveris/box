/**
 * @file InputCommands.c
 * @brief Benchmarks for the input commands of a terminal session.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/command/command_internal.h"

/** Key presses one iteration queues */
#define BENCH_INPUT_CMD_PRESSES (10)

void InputCommands_key_press_10(bench_t *b)
{
    // input/input_commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCommand);
    ecs_entity_t session = ecs_entity(world, { .name = "bench" });
    ecs_add(world, session, Terminal);
    ecs_progress(world, 0.016f);

    bench_set_items(b, BENCH_INPUT_CMD_PRESSES);

    while (bench_iter(b)) {
        int32_t i;
        for (i = 0; i < BENCH_INPUT_CMD_PRESSES; i ++) {
            char *out = box_command_run(world, session, "key.press W", NULL);
            bench_keep(out);
            ecs_os_free(out);
        }

        bench_pause(b);
        box_input_reset(world);
        bench_resume(b);
    }

    ecs_fini(world);
}

void InputCommands_action_list_100(bench_t *b)
{
    // input/input_commands
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCommand);
    ecs_entity_t session = ecs_entity(world, { .name = "bench" });
    ecs_add(world, session, Terminal);
    ecs_entity_t map = ecs_lookup(world, "box.input.map");

    int32_t i;
    for (i = 0; i < 100; i ++) {
        char name[32];
        ecs_os_snprintf(name, sizeof(name), "action_%d", i);
        ecs_entity_t action = ecs_entity(world, { .name = name });
        ecs_add(world, action, InputAction);
        ecs_entity_t bind = ecs_new_w_pair(world, EcsChildOf, map);
        ecs_set(world, bind, Binding, {
            .mask = (uint64_t)(BoxKeyA + (i % 26)), .action = action });
    }

    bench_set_items(b, 100);

    while (bench_iter(b)) {
        char *out = box_command_run(world, session, "action.list", NULL);
        bench_keep(out);
        ecs_os_free(out);
    }

    ecs_fini(world);
}
