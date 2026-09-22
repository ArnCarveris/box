/**
 * @file Effects.c
 * @brief Benchmark for building the post process chain of a view.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

/** Passes the benchmarked chain orders and copies parameters for */
#define BENCH_EFFECT_COUNT (8)

void Effects_extract_chain(bench_t *b)
{
    // effects/shader_graph
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;

    ecs_entity_t shader = ecs_new(world);
    ecs_set(world, shader, Shader, {.path = "etc/shaders/overlay.wgsl"});
    ecs_entity_t params = ecs_id(LayerParams);
    int32_t i;
    for (i = 0; i < BENCH_EFFECT_COUNT; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Effect, { .shader = shader, .params = params,
            .order = BENCH_EFFECT_COUNT - i, .enabled = true, .color = true,
            .depth = i % 2 == 0, .scale = 1 });
        ecs_set(world, e, LayerParams, {
            .a = {(float)i, 1, 0, 0}, .b = {0, 0, 0, 0} });
    }

    box_render_extract(world, &packet);
    uint32_t cmd;
    for (cmd = 0; cmd < packet.shader_create.count; cmd ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, cmd))->source);
    }

    bench_set_items(b, BENCH_EFFECT_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.effects.count);
    }

    box_packet_fini(&packet);
    ecs_fini(world);
}
