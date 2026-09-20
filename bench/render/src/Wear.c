/**
 * @file Wear.c
 * @brief Benchmark for extracting instances that carry per entity layer amounts.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

/** Dynamic entities with per entity layer amounts in the extracted scene */
#define BENCH_WEAR_COUNT (10000)

void Wear_extract_10k_with_wear(bench_t *b)
{
    // ore_engine/instance_wear
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;

    ecs_entity_t dust = ecs_lookup(world, "box.material.layers.dust");
    ecs_entity_t *scene = ecs_os_malloc_n(ecs_entity_t, BENCH_WEAR_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_WEAR_COUNT; i ++) {
        float x = (float)(i % 100) - 50.0f;
        float z = (float)(i / 100) - 50.0f;
        scene[i] = ecs_new(world);
        ecs_set(world, scene[i], Position, {x, 0, z});
        ecs_set(world, scene[i], Box, {1, 1, 1});
        ecs_add_id(world, scene[i], DynamicTransform);
        ecs_add_pair(world, scene[i], MaterialLayerOn, dust);
        ecs_set(world, scene[i], Wear, {
            .dust = (float)(i % 8) / 8.0f, .grime = 1, .edge = 1, .wet = 1 });
        ecs_set(world, scene[i], WearDetail, {
            .burn = 1, .scratch = 1, .seed = (float)(i % 16) / 16.0f, .scale = 1 });
    }

    box_render_extract(world, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    uint32_t layer;
    for (layer = 0; layer < packet.shader_create.count; layer ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, layer))->source);
    }

    bench_set_items(b, BENCH_WEAR_COUNT);

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
