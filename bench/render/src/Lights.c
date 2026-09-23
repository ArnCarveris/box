/**
 * @file Lights.c
 * @brief Benchmarks for extracting moving point lights into a frame packet.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

/** Lights in the extracted scene */
#define BENCH_LIGHT_COUNT (1000)

/** Lights per row of the grid */
#define BENCH_LIGHT_ROW (40)

void Lights_extract_1000_moving(
    bench_t *b)
{
    // lighting/clustered_forward
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 20, -60});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .enabled = true});
    ecs_entity_t *lights = ecs_os_malloc_n(ecs_entity_t, BENCH_LIGHT_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_LIGHT_COUNT; i ++) {
        lights[i] = ecs_new(world);
        ecs_add(world, lights[i], DynamicTransform);
        ecs_set(world, lights[i], Position,
            {(float)(i % BENCH_LIGHT_ROW) * 2 - 40, 2, (float)(i / BENCH_LIGHT_ROW) * 2});
        ecs_set(world, lights[i], PointLight, {.intensity = 5, .range = 8});
        ecs_set(world, lights[i], Rgb, {200, 120, 60});
    }

    ecs_progress(world, 0.016f);
    float offset = 0;

    bench_set_items(b, BENCH_LIGHT_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        packet.width = 1920;
        packet.height = 1080;
        offset += 0.05f;
        for (i = 0; i < BENCH_LIGHT_COUNT; i ++) {
            ecs_set(world, lights[i], Position,
                {(float)(i % BENCH_LIGHT_ROW) * 2 - 40 + offset, 2,
                 (float)(i / BENCH_LIGHT_ROW) * 2});
        }

        ecs_progress(world, 0.016f);
        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.lights.count);
    }

    box_packet_fini(&packet);
    ecs_os_free(lights);
    ecs_fini(world);
}

void Lights_extract_1000_culled(
    bench_t *b)
{
    // lighting/clustered_forward
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 20, -60});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .enabled = true});
    ecs_entity_t *lights = ecs_os_malloc_n(ecs_entity_t, BENCH_LIGHT_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_LIGHT_COUNT; i ++) {
        lights[i] = ecs_new(world);
        ecs_add(world, lights[i], DynamicTransform);
        ecs_set(world, lights[i], Position,
            {(float)(i % BENCH_LIGHT_ROW) * 2 - 40, 2, -400 - (float)(i / BENCH_LIGHT_ROW)});
        ecs_set(world, lights[i], PointLight, {.intensity = 5, .range = 8});
    }

    ecs_progress(world, 0.016f);

    bench_set_items(b, BENCH_LIGHT_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        packet.width = 1920;
        packet.height = 1080;
        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.lights.count);
    }

    box_packet_fini(&packet);
    ecs_os_free(lights);
    ecs_fini(world);
}
