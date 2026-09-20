/**
 * @file Blending.c
 * @brief Benchmark for extracting and sorting the transparent draw list.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

/** Transparent entities in the extracted scene */
#define BENCH_TRANSPARENT_COUNT (10000)

void Blending_transparent_10k(bench_t *b)
{
    // materials/blending
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;
    ecs_entity_t *scene = ecs_os_malloc_n(ecs_entity_t, BENCH_TRANSPARENT_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_TRANSPARENT_COUNT; i ++) {
        float x = (float)(i % 100) - 50.0f;
        float z = (float)((i * 37) % 997) * 0.1f;
        scene[i] = ecs_new(world);
        ecs_set(world, scene[i], Position, {x, 0, z});
        ecs_set(world, scene[i], Quad, {1, 1});
        ecs_set(world, scene[i], Opacity, {0.5f});
        ecs_set(world, scene[i], BlendMode, {AlphaBlend});
    }

    box_render_extract(world, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);

    bench_set_items(b, BENCH_TRANSPARENT_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.transparent.count);
    }

    box_packet_fini(&packet);
    ecs_os_free(scene);
    ecs_fini(world);
}
