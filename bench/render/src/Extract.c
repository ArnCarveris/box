/**
 * @file Extract.c
 * @brief Benchmarks for building a frame packet from static and dynamic entities.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

/** Entities in the extracted scene */
#define BENCH_SCENE_COUNT (10000)

void Extract_static_1_changed(
    bench_t *b)
{
    // project_tool/bench
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;
    ecs_entity_t *scene = ecs_os_malloc_n(ecs_entity_t, BENCH_SCENE_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_SCENE_COUNT; i ++) {
        scene[i] = ecs_new(world);
        ecs_set(world, scene[i], Position, {(float)i, 0, 0});
        ecs_set(world, scene[i], Box, {1, 1, 1});
    }

    box_render_extract(world, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    float x = 0;

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        x += 1.0f;
        ecs_set(world, scene[0], Position, {x, 0, 0});
        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.static_updates.count);
    }

    box_packet_fini(&packet);
    ecs_os_free(scene);
    ecs_fini(world);
}

void Extract_static_100_changed(
    bench_t *b)
{
    // project_tool/bench
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;
    ecs_entity_t *scene = ecs_os_malloc_n(ecs_entity_t, BENCH_SCENE_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_SCENE_COUNT; i ++) {
        scene[i] = ecs_new(world);
        ecs_set(world, scene[i], Position, {(float)i, 0, 0});
        ecs_set(world, scene[i], Box, {1, 1, 1});
    }

    box_render_extract(world, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    float x = 0;

    bench_set_items(b, 100);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        x += 1.0f;
        for (i = 0; i < 100; i ++) {
            ecs_set(world, scene[i], Position, {x, 0, 0});
        }

        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.static_updates.count);
    }

    box_packet_fini(&packet);
    ecs_os_free(scene);
    ecs_fini(world);
}

void Extract_static_1000_changed(
    bench_t *b)
{
    // project_tool/bench
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;
    ecs_entity_t *scene = ecs_os_malloc_n(ecs_entity_t, BENCH_SCENE_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_SCENE_COUNT; i ++) {
        scene[i] = ecs_new(world);
        ecs_set(world, scene[i], Position, {(float)i, 0, 0});
        ecs_set(world, scene[i], Box, {1, 1, 1});
    }

    box_render_extract(world, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    float x = 0;

    bench_set_items(b, 1000);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        x += 1.0f;
        for (i = 0; i < 1000; i ++) {
            ecs_set(world, scene[i], Position, {x, 0, 0});
        }

        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.static_updates.count);
    }

    box_packet_fini(&packet);
    ecs_os_free(scene);
    ecs_fini(world);
}

void Extract_static_1000_changed_parent(
    bench_t *b)
{
    // scene_core/parent_hierarchy
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;
    ecs_entity_t *scene = ecs_os_malloc_n(ecs_entity_t, BENCH_SCENE_COUNT);
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {0, 0, 0});
    int32_t i;
    for (i = 0; i < BENCH_SCENE_COUNT; i ++) {
        scene[i] = ecs_new_w_parent(world, i < 100 ? root : scene[i / 100], NULL);
        ecs_set(world, scene[i], Position, {(float)i, 0, 0});
        ecs_set(world, scene[i], Box, {1, 1, 1});
    }

    box_render_extract(world, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    bench_counter(b, "tables", (double)ecs_get_world_info(world)->table_count);
    float x = 0;

    bench_set_items(b, 1000);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        x += 1.0f;
        for (i = BENCH_SCENE_COUNT - 1000; i < BENCH_SCENE_COUNT; i ++) {
            ecs_set(world, scene[i], Position, {x, 0, 0});
        }

        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.static_updates.count);
    }

    box_packet_fini(&packet);
    ecs_os_free(scene);
    ecs_fini(world);
}

void Extract_dynamic_10k(
    bench_t *b)
{
    // project_tool/bench
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;
    ecs_entity_t *scene = ecs_os_malloc_n(ecs_entity_t, BENCH_SCENE_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_SCENE_COUNT; i ++) {
        scene[i] = ecs_new(world);
        ecs_add(world, scene[i], DynamicTransform);
        ecs_set(world, scene[i], Position, {(float)i, 0, 0});
        ecs_set(world, scene[i], Box, {1, 1, 1});
    }

    ecs_progress(world, 0.016f);
    box_render_extract(world, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);

    bench_set_items(b, BENCH_SCENE_COUNT);

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
