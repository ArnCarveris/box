/**
 * @file Tiling.c
 * @brief Benchmarks for the extract cost of instances with a texture transform and tiling.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

/** Instances in the extracted scene */
#define BENCH_TILING_COUNT (10000)

/** Tiling shapes the instances cycle through */
#define BENCH_TILING_SHAPES (4)

void Tiling_extract_10k(bench_t *b)
{
    // textures/tiling
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;

    TilingShape shapes[BENCH_TILING_SHAPES] = {
        TilingBox, TilingCylinder, TilingRadial, TilingMeters };
    ecs_entity_t *scene = ecs_os_malloc_n(ecs_entity_t, BENCH_TILING_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_TILING_COUNT; i ++) {
        scene[i] = ecs_new(world);
        ecs_add(world, scene[i], DynamicTransform);
        ecs_set(world, scene[i], Position, {(float)i, 0, 0});
        ecs_set(world, scene[i], Box, {1 + (float)(i % 4), 1, 2});
        ecs_set(world, scene[i], TextureTransform, {1.0f, 1.0f, 0.25f, 0.5f});
        ecs_set(world, scene[i], TextureTiling, { .meters_x = 1, .meters_y = 1,
            .shape = shapes[i % BENCH_TILING_SHAPES] });
    }

    ecs_progress(world, 0.016f);
    box_render_extract(world, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);

    bench_set_items(b, BENCH_TILING_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.dynamic_instances.count);
    }

    box_packet_fini(&packet);
    ecs_os_free(scene);
    ecs_fini(world);
}
