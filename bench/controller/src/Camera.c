/**
 * @file Camera.c
 * @brief Benchmarks for the OnUpdate system that moves the camera controllers.
 */

#include <box.h>
#include <bake_bench.h>

/** Controllers the benchmark moves every frame */
#define BENCH_CONTROLLER_COUNT (100)

void Camera_update_100_controllers(
    bench_t *b)
{
    // controllers/camera_controller
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    int32_t i;
    for (i = 0; i < BENCH_CONTROLLER_COUNT; i ++) {
        ecs_entity_t camera = ecs_new(world);
        ecs_add(world, camera, CameraController);
        ecs_add(world, camera, LookAt);
    }

    box_input_inject(world, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyW });
    box_input_inject(world, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyShiftLeft });
    ecs_progress(world, 0.016f);

    bench_set_items(b, BENCH_CONTROLLER_COUNT);

    while (bench_iter(b)) {
        ecs_progress(world, 0.016f);
        bench_keep(ecs_singleton_get(world, Input)->keys[BoxKeyW].state);
    }

    ecs_fini(world);
}
