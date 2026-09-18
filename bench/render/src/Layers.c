/**
 * @file Layers.c
 * @brief Benchmark for extracting instances that enable material layers.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

/** Dynamic entities with material layers in the extracted scene */
#define BENCH_LAYER_COUNT (10000)

void Layers_extract_10k_with_layers(bench_t *b)
{
    // materials/material_layers
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;

    ecs_entity_t dust = ecs_lookup(world, "box.material.layers.dust");
    ecs_entity_t grime = ecs_lookup(world, "box.material.layers.grime");
    ecs_entity_t *scene = ecs_os_malloc_n(ecs_entity_t, BENCH_LAYER_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_LAYER_COUNT; i ++) {
        float x = (float)(i % 100) - 50.0f;
        float z = (float)(i / 100) - 50.0f;
        scene[i] = ecs_new(world);
        ecs_set(world, scene[i], Position, {x, 0, z});
        ecs_set(world, scene[i], Box, {1, 1, 1});
        ecs_add_id(world, scene[i], DynamicTransform);
        ecs_add_pair(world, scene[i], MaterialLayerOn, dust);
        if (i % 2) {
            ecs_add_pair(world, scene[i], MaterialLayerOn, grime);
        }
    }

    box_render_extract(world, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    uint32_t layer;
    for (layer = 0; layer < packet.shader_create.count; layer ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, layer))->source);
    }

    bench_set_items(b, BENCH_LAYER_COUNT);

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

void Layers_extract_10k_two_views(bench_t *b)
{
    // views/layers
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;

    ecs_entity_t red = ecs_new(world);
    ecs_entity_t blue = ecs_new(world);
    ecs_entity_t *scene = ecs_os_malloc_n(ecs_entity_t, BENCH_LAYER_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_LAYER_COUNT; i ++) {
        float x = (float)(i % 100) - 50.0f;
        float z = (float)(i / 100) - 50.0f;
        scene[i] = ecs_new(world);
        ecs_set(world, scene[i], Position, {x, 0, z});
        ecs_set(world, scene[i], Box, {1, 1, 1});
        ecs_add_id(world, scene[i], DynamicTransform);
        ecs_add_pair(world, scene[i], Layer, i % 2 ? red : blue);
    }

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 20, -60});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t target = ecs_new(world);
    ecs_set(world, target, RenderTarget, {512, 512});

    ecs_entity_t main_view = ecs_new(world);
    ecs_add(world, main_view, View);
    View *main_settings = ecs_get_mut(world, main_view, View);
    main_settings->camera = camera;
    main_settings->layers[0] = red;
    ecs_modified(world, main_view, View);

    ecs_entity_t side_view = ecs_new(world);
    ecs_add(world, side_view, View);
    View *side_settings = ecs_get_mut(world, side_view, View);
    side_settings->camera = camera;
    side_settings->target = target;
    side_settings->priority = 1;
    side_settings->layers[0] = blue;
    ecs_modified(world, side_view, View);

    box_render_extract(world, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);

    bench_set_items(b, BENCH_LAYER_COUNT);

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
