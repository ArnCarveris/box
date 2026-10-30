/**
 * @file bench_memory.c
 * @brief Reporting of the instance memory counters, shared by the bench/render suites.
 */

#include "bench_memory.h"

void bench_render_memory(
    bench_t *b,
    const box_render_draws_stats_t *stats)
{
    bench_counter(b, "transform_bytes", (double)stats->transform_bytes);
    bench_counter(b, "shading_bytes", (double)stats->shading_bytes);
    bench_counter(b, "surface_bytes", (double)stats->surface_bytes);
    bench_counter(b, "page_slots", (double)stats->page_slots);
    bench_counter(b, "page_transform_bytes", (double)stats->page_transform_bytes);
    bench_counter(b, "page_shading_bytes", (double)stats->page_shading_bytes);
    bench_counter(b, "page_surface_bytes", (double)stats->page_surface_bytes);
    bench_counter(b, "page_bytes", (double)stats->page_bytes);
}
