/**
 * @file Hidden.c
 * @brief Benchmarks for toggling the Hidden switch on a pool of entities.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

/** Entities in the extracted scene */
#define BENCH_HIDE_SCENE (10000)

/** Entities the switch is flipped on every frame */
#define BENCH_HIDE_COUNT (1000)

void Hidden_toggle_1000_of_10k(
    bench_t *b)
{
    // ore_engine/visibility
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;
    ecs_entity_t *scene = ecs_os_malloc_n(ecs_entity_t, BENCH_HIDE_SCENE);
    int32_t i;
    for (i = 0; i < BENCH_HIDE_SCENE; i ++) {
        scene[i] = ecs_new(world);
        ecs_set(world, scene[i], Position, {(float)i, 0, 0});
        ecs_set(world, scene[i], Box, {1, 1, 1});
    }

    box_render_extract(world, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    bool hidden = false;

    bench_set_items(b, BENCH_HIDE_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        hidden = !hidden;
        bench_resume(b);
        for (i = 0; i < BENCH_HIDE_COUNT; i ++) {
            if (hidden) {
                ecs_add_id(world, scene[i], Hidden);
            } else {
                ecs_remove_id(world, scene[i], Hidden);
            }
        }

        box_render_extract(world, &packet);
        bench_keep(packet.static_updates.count);
    }

    box_packet_fini(&packet);
    ecs_os_free(scene);
    ecs_fini(world);
}

void Hidden_extract_10k_with_a_hidden_tenth(
    bench_t *b)
{
    // ore_engine/visibility
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;
    ecs_entity_t *scene = ecs_os_malloc_n(ecs_entity_t, BENCH_HIDE_SCENE);
    int32_t i;
    for (i = 0; i < BENCH_HIDE_SCENE; i ++) {
        scene[i] = ecs_new(world);
        ecs_set(world, scene[i], Position, {(float)i, 0, 0});
        ecs_set(world, scene[i], Box, {1, 1, 1});
        ecs_add_id(world, scene[i], DynamicTransform);
        if (i < BENCH_HIDE_COUNT) {
            ecs_add_id(world, scene[i], Hidden);
        }
    }

    box_render_extract(world, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);

    bench_set_items(b, BENCH_HIDE_SCENE);

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
