/**
 * @file Bake.c
 * @brief Benchmarks for the probe invalidation of the baked lighting.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

/** Static entities in the invalidation scene */
#define BENCH_STATIC_COUNT (100000)

/** Static entities per row of the grid */
#define BENCH_STATIC_ROW (400)

/** Static point lights lighting the scene */
#define BENCH_STATIC_LIGHTS (4)

/** Range of the static lights, the radius a changed instance invalidates */
#define BENCH_LIGHT_RANGE (10.0f)

/** Static entities moved per frame by the churn case */
#define BENCH_CHURN_COUNT (1000)

void Bake_invalidate_one_of_100k(bench_t *b)
{
    // lighting/light_baking
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 60, -160});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .bake = {.enabled = true}, .enabled = true});

    int32_t i;
    for (i = 0; i < BENCH_STATIC_LIGHTS; i ++) {
        ecs_entity_t light = ecs_new(world);
        ecs_set(world, light, Position, {(float)i * 40 - 60, 10, 0});
        ecs_set(world, light, PointLight, {.intensity = 20, .range = BENCH_LIGHT_RANGE});
    }

    ecs_entity_t moved = 0;
    for (i = 0; i < BENCH_STATIC_COUNT; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)(i % BENCH_STATIC_ROW) - 200.0f,
            (float)((i / BENCH_STATIC_ROW) % 25) * 2, (float)(i / 10000) * 6 - 30});
        ecs_set(world, e, Box, {0.8f, 0.8f, 0.8f});
        moved = i == BENCH_STATIC_COUNT / 2 ? e : moved;
    }

    ecs_progress(world, 0.016f);
    box_render_extract(world, &packet);

    box_bake_state_t state;
    box_bake_state_init(&state);
    box_bake_state_apply(&state, &packet);
    box_bake_state_take(&state, BENCH_STATIC_COUNT);
    float offset = 0;

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        bench_pause(b);
        box_bake_state_take(&state, BENCH_STATIC_COUNT);
        box_packet_reset(&packet);
        packet.width = 1920;
        packet.height = 1080;
        offset += 0.01f;
        ecs_set(world, moved, Position, {offset, 0, 0});
        ecs_progress(world, 0.016f);
        box_render_extract(world, &packet);
        bench_resume(b);
        box_bake_state_apply(&state, &packet);
        bench_keep(state.dirty.count);
    }

    box_bake_state_fini(&state);
    box_packet_fini(&packet);
    ecs_fini(world);
}

void Bake_probe_count_100k(bench_t *b)
{
    // lighting/bake_resolution
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 60, -160});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .enabled = true});

    int32_t i;
    for (i = 0; i < BENCH_STATIC_LIGHTS; i ++) {
        ecs_entity_t light = ecs_new(world);
        ecs_set(world, light, Position, {(float)i * 40 - 60, 10, 0});
        ecs_set(world, light, PointLight, {.intensity = 20, .range = BENCH_LIGHT_RANGE});
    }

    for (i = 0; i < BENCH_STATIC_COUNT; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)(i % BENCH_STATIC_ROW) - 200.0f,
            (float)((i / BENCH_STATIC_ROW) % 25) * 2, (float)(i / 10000) * 6 - 30});
        ecs_set(world, e, Box, {0.8f, 0.8f, 0.8f});
    }

    ecs_progress(world, 0.016f);
    box_render_extract(world, &packet);

    box_bake_state_t state;
    box_bake_state_init(&state);
    box_bake_state_apply(&state, &packet);
    box_bake_state_take(&state, BENCH_STATIC_COUNT);
    bench_set_items(b, BENCH_STATIC_COUNT);

    while (bench_iter(b)) {
        box_bake_state_apply(&state, &packet);
        bench_counter(b, "probes", (double)state.probe_count);
        bench_keep(state.probe_count);
    }

    box_bake_state_fini(&state);
    box_packet_fini(&packet);
    ecs_fini(world);
}

void Bake_churn_1000_of_100k(bench_t *b)
{
    // lighting/bake_churn
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 60, -160});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .enabled = true});

    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Position, {0, 100, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 3});

    ecs_entity_t *moved = ecs_os_malloc_n(ecs_entity_t, BENCH_STATIC_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_STATIC_COUNT; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)(i % BENCH_STATIC_ROW) - 200.0f,
            (float)((i / BENCH_STATIC_ROW) % 25) * 2, (float)(i / 10000) * 6 - 30});
        ecs_set(world, e, Box, {0.8f, 0.8f, 0.8f});
        moved[i] = e;
    }

    ecs_progress(world, 0.016f);
    box_render_extract(world, &packet);

    box_bake_state_t state;
    box_bake_state_init(&state);
    box_bake_state_apply(&state, &packet);
    box_bake_state_take(&state, BENCH_STATIC_COUNT);
    float offset = 0;

    bench_set_items(b, BENCH_CHURN_COUNT);

    while (bench_iter(b)) {
        bench_pause(b);
        box_bake_state_take(&state, BENCH_STATIC_COUNT);
        box_packet_reset(&packet);
        packet.width = 1920;
        packet.height = 1080;
        offset += 0.001f;
        int32_t j;
        for (j = 0; j < BENCH_CHURN_COUNT; j ++) {
            ecs_entity_t e = moved[(j * 97) % BENCH_STATIC_COUNT];
            const Position *pos = ecs_get(world, e, Position);
            ecs_set(world, e, Position, {pos->x, pos->y + offset, pos->z});
        }

        ecs_progress(world, 0.016f);
        bench_resume(b);
        box_render_extract(world, &packet);
        box_bake_state_apply(&state, &packet);
        bench_keep(state.dirty.count);
    }

    box_bake_state_fini(&state);
    box_packet_fini(&packet);
    ecs_os_free(moved);
    ecs_fini(world);
}
