/**
 * @file Packet.c
 * @brief Benchmark for a frame that keeps appending more packet data than a packet always keeps.
 *
 * Every frame uploads 20 MB, above BOX_RENDER_DATA_KEEP, to a backend that drops the packets,
 * touching one byte per page the way an extract writes its data.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Bytes a frame uploads */
#define BENCH_PACKET_BYTES (20 * 1024 * 1024)

/** Accept a packet without rendering it.
 *
 * @param ctx Unused
 * @param packet The packet
 */
static void bench_packet_drop(
    void *ctx,
    const box_render_packet_t *packet)
{
    (void)ctx;
    (void)packet;
}

/** Upload BENCH_PACKET_BYTES and write one byte per page.
 *
 * @param it The iterator of the system
 */
static void BenchPacketExtract(
    ecs_iter_t *it)
{
    box_render_packet_t *p = box_render_packet(it->world);
    uint8_t *bytes = box_render_packet_upload(p, 1, 0, BENCH_PACKET_BYTES);
    uint32_t i;
    for (i = 0; i < BENCH_PACKET_BYTES; i += 4096) {
        bytes[i] = (uint8_t)i;
    }
}

void Packet_steady_20mb(
    bench_t *b)
{
    // render_fixes/packet_data_trim
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_backend_t backend = { .render = bench_packet_drop };
    box_render_set_backend(world, &backend);
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BenchPacketExtract" }),
        .phase = RenderExtract,
        .callback = BenchPacketExtract
    });

    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
    }

    int64_t before = ecs_os_api_malloc_count + ecs_os_api_calloc_count +
        ecs_os_api_realloc_count;
    int64_t frames = 0;
    bench_set_items(b, 1);
    while (bench_iter(b)) {
        ecs_progress(world, 0);
        frames ++;
    }

    box_render_sync(world);
    int64_t after = ecs_os_api_malloc_count + ecs_os_api_calloc_count +
        ecs_os_api_realloc_count;
    bench_counter(b, "allocs_per_frame", frames ? (double)(after - before) / (double)frames : 0);
    ecs_fini(world);
}
