/**
 * @file Extract.c
 * @brief Benchmarks for extracting a scene whose values a transition moves.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

/** Entities in the extracted scene */
#define BENCH_SCENE_COUNT (10000)

/** Two floats a benchmark transitions to reach ten values per entity */
ECS_STRUCT(BenchPair, {
    float a; /**< First value */
    float b; /**< Second value */
});

/** Four floats a benchmark transitions to reach ten values per entity */
ECS_STRUCT(BenchQuad, {
    float x; /**< First value */
    float y; /**< Second value */
    float z; /**< Third value */
    float w; /**< Fourth value */
});

void Extract_transitions_10k_x_10(bench_t *b)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ECS_META_COMPONENT(world, BenchPair);
    ECS_META_COMPONENT(world, BenchQuad);
    ecs_add_id(world, ecs_id(PbrMaterial), Transitionable);
    ecs_add_id(world, ecs_id(TextureTransform), Transitionable);
    ecs_add_id(world, ecs_id(BenchPair), Transitionable);
    ecs_add_id(world, ecs_id(BenchQuad), Transitionable);

    ecs_entity_t keys[10] = {
        ecs_id(Position), ecs_id(Rotation), ecs_id(Scale), ecs_id(Rgb),
        ecs_id(Emissive), ecs_id(Tint), ecs_id(PbrMaterial),
        ecs_id(TextureTransform), ecs_id(BenchPair), ecs_id(BenchQuad)
    };

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    Transition *map = ecs_ensure(world, prefab, Transition);
    ecs_map_init_if(map, NULL);
    int32_t i;
    for (i = 0; i < 10; i ++) {
        TransitionParams *params = ecs_os_calloc_t(TransitionParams);

        params->duration = 1000.0f;
        params->timing_function = Linear;
        *ecs_map_ensure(map, keys[i]) = (ecs_map_val_t)(uintptr_t)params;
    }

    ecs_modified(world, prefab, Transition);

    ecs_entity_t *scene = ecs_os_malloc_n(ecs_entity_t, BENCH_SCENE_COUNT);
    for (i = 0; i < BENCH_SCENE_COUNT; i ++) {
        scene[i] = ecs_new_w_pair(world, EcsIsA, prefab);
        ecs_set(world, scene[i], Box, {1, 1, 1});
        ecs_set(world, scene[i], Position, {(float)i, 0, 0});
        ecs_set(world, scene[i], Rotation, {0, 0, 0});
        ecs_set(world, scene[i], Scale, {1, 1, 1});
        ecs_set(world, scene[i], Rgb, {10, 10, 10});
        ecs_set(world, scene[i], Emissive, {0, {10, 10, 10}});
        ecs_set(world, scene[i], Tint, {10, 10, 10, 10});
        ecs_set(world, scene[i], PbrMaterial, {0, 0.5f});
        ecs_set(world, scene[i], TextureTransform, {1, 1, 0, 0});
        ecs_set(world, scene[i], BenchPair, {0, 0});
        ecs_set(world, scene[i], BenchQuad, {0, 0, 0, 0});
    }

    for (i = 0; i < BENCH_SCENE_COUNT; i ++) {
        ecs_set(world, scene[i], Position, {(float)i + 10.0f, 5, 0});
        ecs_set(world, scene[i], Rotation, {1, 1, 1});
        ecs_set(world, scene[i], Scale, {2, 2, 2});
        ecs_set(world, scene[i], Rgb, {200, 200, 200});
        ecs_set(world, scene[i], Emissive, {5, {200, 200, 200}});
        ecs_set(world, scene[i], Tint, {200, 200, 200, 200});
        ecs_set(world, scene[i], PbrMaterial, {1, 0.1f});
        ecs_set(world, scene[i], TextureTransform, {2, 2, 1, 1});
        ecs_set(world, scene[i], BenchPair, {1, 1});
        ecs_set(world, scene[i], BenchQuad, {1, 1, 1, 1});
    }

    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;
    box_render_extract(world, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);

    bench_set_items(b, BENCH_SCENE_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        ecs_frame_begin(world, 1.0f / 60.0f);
        ecs_frame_end(world);
        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.static_updates.count);
    }

    box_packet_fini(&packet);
    ecs_os_free(scene);
    ecs_fini(world);
}

void Extract_no_transitions_10k(bench_t *b)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ECS_META_COMPONENT(world, BenchPair);
    ECS_META_COMPONENT(world, BenchQuad);

    ecs_entity_t *scene = ecs_os_malloc_n(ecs_entity_t, BENCH_SCENE_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_SCENE_COUNT; i ++) {
        scene[i] = ecs_new(world);
        ecs_set(world, scene[i], Box, {1, 1, 1});
        ecs_set(world, scene[i], Position, {(float)i, 0, 0});
        ecs_set(world, scene[i], Rotation, {0, 0, 0});
        ecs_set(world, scene[i], Scale, {1, 1, 1});
        ecs_set(world, scene[i], Rgb, {10, 10, 10});
        ecs_set(world, scene[i], Emissive, {0, {10, 10, 10}});
        ecs_set(world, scene[i], Tint, {10, 10, 10, 10});
        ecs_set(world, scene[i], PbrMaterial, {0, 0.5f});
        ecs_set(world, scene[i], TextureTransform, {1, 1, 0, 0});
        ecs_set(world, scene[i], BenchPair, {0, 0});
        ecs_set(world, scene[i], BenchQuad, {0, 0, 0, 0});
    }

    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;
    box_render_extract(world, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);

    bench_set_items(b, BENCH_SCENE_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        ecs_frame_begin(world, 1.0f / 60.0f);
        ecs_frame_end(world);
        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.static_updates.count);
    }

    box_packet_fini(&packet);
    ecs_os_free(scene);
    ecs_fini(world);
}

void Extract_start_10k_transitions(bench_t *b)
{
    // transitions/transition_params
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    Transition *map = ecs_ensure(world, prefab, Transition);
    ecs_map_init_if(map, NULL);
    TransitionParams *params = ecs_os_calloc_t(TransitionParams);
    params->duration = 1000.0f;
    params->timing_function = Linear;
    *ecs_map_ensure(map, ecs_id(Position)) = (ecs_map_val_t)(uintptr_t)params;
    ecs_modified(world, prefab, Transition);

    ecs_entity_t *scene = ecs_os_malloc_n(ecs_entity_t, BENCH_SCENE_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_SCENE_COUNT; i ++) {
        scene[i] = ecs_new_w_pair(world, EcsIsA, prefab);
        ecs_set(world, scene[i], Box, {1, 1, 1});
        ecs_set(world, scene[i], Position, {(float)i, 0, 0});
    }

    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;
    box_render_extract(world, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);

    float x = 0;

    bench_set_items(b, BENCH_SCENE_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        x += 1.0f;
        bench_resume(b);
        for (i = 0; i < BENCH_SCENE_COUNT; i ++) {
            ecs_set(world, scene[i], Position, {x, 0, 0});
        }

        bench_keep((int32_t)x);
    }

    box_packet_fini(&packet);
    ecs_os_free(scene);
    ecs_fini(world);
}
