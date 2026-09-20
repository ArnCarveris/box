/**
 * @file Environment.c
 * @brief Benchmark for extracting a changing environment light into a frame packet.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/render/render_internal.h"

void Environment_extract(bench_t *b)
{
    // lighting/ibl
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 2, -10});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_singleton_set(world, EnvironmentLight, {
        .sky_color = {110, 160, 235},
        .ground_color = {150, 110, 70},
        .intensity = 1
    });

    float intensity = 1;
    bench_set_items(b, 1);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        packet.width = 1920;
        packet.height = 1080;
        intensity += 0.01f;
        ecs_singleton_set(world, EnvironmentLight, {
            .sky_color = {110, 160, 235},
            .ground_color = {150, 110, 70},
            .intensity = intensity
        });
        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.view.env_generation);
    }

    box_packet_fini(&packet);
    ecs_fini(world);
}
