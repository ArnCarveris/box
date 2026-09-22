#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

#define BENCH_LAYER_DATA_COUNT (10000)

void LayerData_extract_10k_with_layer_data(bench_t *b)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;

    ecs_entity_t shader = ecs_new(world);
    ecs_set(world, shader, Shader, {.path = "bench/render/etc/layer.wgsl"});
    ecs_entity_t layer = ecs_new(world);
    ecs_set(world, layer, MaterialLayer, {.shader = shader, .order = 0});
    ecs_set(world, layer, LayerParams, {.a = {1, 1, 1, 1}});
    ecs_entity_t *scene = ecs_os_malloc_n(ecs_entity_t, BENCH_LAYER_DATA_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_LAYER_DATA_COUNT; i ++) {
        float x = (float)(i % 100) - 50.0f;
        float z = (float)(i / 100) - 50.0f;
        scene[i] = ecs_new(world);
        ecs_set(world, scene[i], Position, {x, 0, z});
        ecs_set(world, scene[i], Box, {1, 1, 1});
        ecs_add_id(world, scene[i], DynamicTransform);
        ecs_add_pair(world, scene[i], MaterialLayerOn, layer);
        ecs_set(world, scene[i], MaterialLayerData, {
            .amount0 = (float)(i % 8) / 8.0f, .amount1 = 1, .amount2 = 1,
            .amount3 = 1, .amount4 = 1, .amount5 = 1,
            .seed = (float)(i % 16) / 16.0f, .scale = 1 });
    }

    box_render_extract(world, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    uint32_t shader_index;
    for (shader_index = 0; shader_index < packet.shader_create.count; shader_index ++) {
        free(((box_shader_cmd_t*)box_vec_at(
            &packet.shader_create, shader_index))->source);
    }

    bench_set_items(b, BENCH_LAYER_DATA_COUNT);

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
