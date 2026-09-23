/**
 * @file Input.c
 * @brief Benchmarks for the OnLoad system that applies the queued input events.
 */

#include <box.h>
#include <bake_bench.h>

/** Events the queue holds when the update system runs */
#define BENCH_INPUT_EVENTS (1000)

void Input_update_1000_events(
    bench_t *b)
{
    // input/device_layer
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    Surface *surface = ecs_singleton_ensure(world, Surface);
    surface->width = 1920;
    surface->height = 1080;
    ecs_progress(world, 0.016f);

    bench_set_items(b, BENCH_INPUT_EVENTS);

    while (bench_iter(b)) {
        bench_pause(b);
        int32_t i;
        for (i = 0; i < BENCH_INPUT_EVENTS; i += 4) {
            box_input_inject(world, &(InputEvent){
                .kind = InputKeyDown, .code = BoxKeyA + (i % 26) });
            box_input_inject(world, &(InputEvent){
                .kind = InputKeyUp, .code = BoxKeyA + (i % 26) });
            box_input_inject(world, &(InputEvent){
                .kind = InputMouseMove, .value = {(float)i, (float)(i * 2)} });
            box_input_inject(world, &(InputEvent){
                .kind = InputScroll, .value = {0, 1.0f} });
        }

        bench_resume(b);
        ecs_progress(world, 0.016f);
        bench_keep(ecs_singleton_get(world, Input)->mouse.window.x);
    }

    ecs_fini(world);
}
