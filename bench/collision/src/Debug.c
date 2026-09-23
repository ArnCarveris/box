/**
 * @file Debug.c
 * @brief Benchmarks for the collider wireframes the extract of a frame emits.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/collision/collision_internal.h"

/** Entities with a collider in the scene of the benchmarks */
#define BENCH_DEBUG_COUNT (10000)

/** Fill a packet with one view whose frustum is the clip cube.
 *
 * @param p The packet
 */
static void bench_debug_view(
    box_packet_t *p)
{
    box_view_t *v = box_vec_push(&p->views);
    memset(v, 0, sizeof(box_view_t));
    int32_t i;
    for (i = 0; i < 4; i ++) {
        v->view[i * 5] = 1.0f;
        v->proj[i * 5] = 1.0f;
    }
}

/** Build a world of colliders inside the clip cube.
 *
 * @param debug What the debug drawing emits
 * @return The world
 */
static ecs_world_t* bench_debug_world(
    CollisionDebug debug)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, CollisionSettings, {
        .margin = 0.25f, .dynamic = true, .enabled = true, .debug = debug
    });

    int32_t i;
    for (i = 0; i < BENCH_DEBUG_COUNT; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {
            (float)(i % 100) * 0.02f - 1.0f,
            (float)(i / 100) * 0.02f - 1.0f,
            0.5f
        });
        ecs_set(world, e, BoxCollider, {{0, 0, 0}, {0.005f, 0.005f, 0.005f}});
    }

    ecs_progress(world, 0.016f);

    return world;
}

void Debug_extract_off_10k(
    bench_t *b)
{
    // collision/debug_draw
    ecs_world_t *world = bench_debug_world(DebugNone);
    box_packet_t packet;
    box_packet_init(&packet);
    bench_debug_view(&packet);
    bench_set_items(b, BENCH_DEBUG_COUNT);

    while (bench_iter(b)) {
        box_vec_clear(&packet.lines);
        box_collision_debug_extract(world, &packet);
        bench_keep(&packet.lines.count);
    }

    box_packet_fini(&packet);
    ecs_fini(world);
}

void Debug_extract_colliders_10k(
    bench_t *b)
{
    // collision/debug_draw
    ecs_world_t *world = bench_debug_world(DebugColliders);
    box_packet_t packet;
    box_packet_init(&packet);
    bench_debug_view(&packet);
    bench_set_items(b, BENCH_DEBUG_COUNT);

    while (bench_iter(b)) {
        box_vec_clear(&packet.lines);
        box_collision_debug_extract(world, &packet);
        bench_keep(&packet.lines.count);
    }

    box_packet_fini(&packet);
    ecs_fini(world);
}
