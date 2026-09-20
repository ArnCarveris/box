/**
 * @file Events.c
 * @brief Benchmarks for the script tasks that await an action.
 */

#include <box.h>
#include <bake_bench.h>

/** Script tasks that await the same action while the benchmark runs */
#define BENCH_EVENT_WAITERS (100)

void Events_resume_100_waiters(bench_t *b)
{
    // input/script_events
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_script(world, { .code =
        "using flecs.script\n"
        "using box.input\n"
        "actions { Forward { InputAction } }\n"
        "template Waiter {\n"
        "  mut hits = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.pressed(actions.Forward)\n"
        "      hits = hits + 1\n"
        "    }\n"
        "  }\n"
        "}\n" });

    ecs_entity_t forward = ecs_lookup(world, "actions.Forward");
    ecs_entity_t waiter = ecs_lookup(world, "Waiter");
    ecs_set(world, ecs_new(world), Binding, {
        .mask = BoxKeyW, .action = forward });

    int32_t i;
    for (i = 0; i < BENCH_EVENT_WAITERS; i ++) {
        ecs_add_id(world, ecs_new(world), waiter);
    }

    ecs_progress(world, 0.016f);
    bench_set_items(b, BENCH_EVENT_WAITERS);

    while (bench_iter(b)) {
        bench_pause(b);
        box_input_inject(world, &(InputEvent){
            .kind = InputKeyDown, .code = BoxKeyW, .duration = 1 });
        bench_resume(b);
        ecs_progress(world, 0.016f);
        bench_keep(ecs_get(world, forward, InputAction)->held);
        bench_pause(b);
        ecs_progress(world, 0.016f);
        bench_resume(b);
    }

    ecs_fini(world);
}
