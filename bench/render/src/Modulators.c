/**
 * @file Modulators.c
 * @brief Benchmark for the sun term of a scene with and without a light modulator.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

/** Dynamic entities the modulated sun lights in the extracted scene */
#define BENCH_MODULATOR_COUNT (10000)

void Modulators_extract_10k_plain_sun(bench_t *b)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;

    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {1.0f, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 3});

    ecs_entity_t *scene = ecs_os_malloc_n(ecs_entity_t, BENCH_MODULATOR_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_MODULATOR_COUNT; i ++) {
        float x = (float)(i % 100) - 50.0f;
        float z = (float)(i / 100) - 50.0f;
        scene[i] = ecs_new(world);
        ecs_set(world, scene[i], Position, {x, 0, z});
        ecs_set(world, scene[i], Box, {1, 1, 1});
        ecs_add_id(world, scene[i], DynamicTransform);
    }

    box_render_extract(world, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    uint32_t shader;
    for (shader = 0; shader < packet.shader_create.count; shader ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, shader))->source);
    }

    bench_set_items(b, BENCH_MODULATOR_COUNT);

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

void Modulators_extract_10k_modulated_sun(bench_t *b)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;

    ecs_entity_t plugin = ecs_new(world);
    ecs_set(world, plugin, Shader, {.path = "bench/render/etc/modulator.wgsl"});
    ecs_entity_t params = ecs_id(LayerParams);
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {1.0f, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 3});
    ecs_set(world, sun, LightModulator, {
        .shader = plugin, .params = params, .enabled = true});
    ecs_set(world, sun, LayerParams, {.a = {24, 2, 0.6f, 0.4f}});

    ecs_entity_t *scene = ecs_os_malloc_n(ecs_entity_t, BENCH_MODULATOR_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_MODULATOR_COUNT; i ++) {
        float x = (float)(i % 100) - 50.0f;
        float z = (float)(i / 100) - 50.0f;
        scene[i] = ecs_new(world);
        ecs_set(world, scene[i], Position, {x, 0, z});
        ecs_set(world, scene[i], Box, {1, 1, 1});
        ecs_add_id(world, scene[i], DynamicTransform);
    }

    box_render_extract(world, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    uint32_t shader;
    for (shader = 0; shader < packet.shader_create.count; shader ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, shader))->source);
    }

    bench_set_items(b, BENCH_MODULATOR_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.modulator_count + packet.dynamic_instances.count);
    }

    box_packet_fini(&packet);
    ecs_os_free(scene);
    ecs_fini(world);
}
