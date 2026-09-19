/**
 * @file Impostor.c
 * @brief Benchmarks what the billboards of the impostors cost one view of a frame.
 *
 * The capture of an atlas happens once per subtree and runs off the frame, so
 * the only impostor work a view repeats is picking the tile the eye stands at
 * and filling the sorted draw that shows it.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/render/lod_cache.h"

/** Billboards the benchmark frame draws */
#define BENCH_IMPOSTOR_COUNT (10000)

/** World units between two billboards */
#define BENCH_IMPOSTOR_SPACING (40.0f)

/** Billboards along one row of the field */
#define BENCH_IMPOSTOR_ROW (100)

void Impostor_billboard_10k(bench_t *b)
{
    // lod/hlod_proxies/impostors
    box_lod_billboard_t *billboards;
    box_transparent_t *draws;
    vec3 eye = vec3_new(0, 40, -600);
    int32_t i;

    ecs_os_init();
    billboards = ecs_os_malloc_n(box_lod_billboard_t, BENCH_IMPOSTOR_COUNT);
    draws = ecs_os_malloc_n(box_transparent_t, BENCH_IMPOSTOR_COUNT);

    for (i = 0; i < BENCH_IMPOSTOR_COUNT; i ++) {
        float x = (float)(i % BENCH_IMPOSTOR_ROW) * BENCH_IMPOSTOR_SPACING;
        float z = (float)(i / BENCH_IMPOSTOR_ROW) * BENCH_IMPOSTOR_SPACING;

        billboards[i] = (box_lod_billboard_t){
            .center = vec3_new(x - 2000.0f, 13, z),
            .axis = vec3_new(0, 0, -1), .radius = 22.8f,
            .size = { 38.9f, 27.0f }, .atlas = (uint32_t)(i % 64) + 1,
            .color = { 1, 1, 1, 1 } };
    }

    bench_set_items(b, BENCH_IMPOSTOR_COUNT);

    while (bench_iter(b)) {
        int32_t sum = 0;
        for (i = 0; i < BENCH_IMPOSTOR_COUNT; i ++) {
            int32_t tile = box_lod_impostor_pick(&billboards[i], eye);

            box_lod_impostor_billboard(&billboards[i], 1, tile, eye, &draws[i]);
            sum += tile;
        }

        bench_counter(b, "billboards", (double)BENCH_IMPOSTOR_COUNT);
        bench_keep(sum);
    }

    ecs_os_free(billboards);
    ecs_os_free(draws);
}
