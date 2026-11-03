/**
 * @file bench_memory.h
 * @brief Reporting of the instance memory counters, shared by the bench/render suites.
 */

#ifndef BENCH_RENDER_MEMORY_H
#define BENCH_RENDER_MEMORY_H

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Report the material records, the stream upload counters and the allocated page buffers of the
 * last extract.
 *
 * Adds the materials, transform_bytes, shading_bytes, surface_bytes, page_slots,
 * page_transform_bytes, page_shading_bytes, page_surface_bytes and page_bytes counters.
 *
 * @param b The bench
 * @param stats Counters of the last extract
 */
void bench_render_memory(
    bench_t *b,
    const box_render_draws_stats_t *stats);

#endif
