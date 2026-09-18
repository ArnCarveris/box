/**
 * @file Frustum.c
 * @brief Benchmark for the CPU frustum test the renderer culls small batches with.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

/** Bounding spheres the culling benchmark tests */
#define BENCH_SPHERE_COUNT (100000)

/** Spheres on one axis of the grid the benchmark spreads them over */
#define BENCH_SPHERE_SPAN (100)

void Frustum_cull_100k_cpu(bench_t *b)
{
    // culling/frustum_culling
    mat4 view = mat4_look_at(vec3_new(0, 60, -160), vec3_new(0, 0, 0), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(3.14159265f / 3.0f, 16.0f / 9.0f, 0.1f);
    float *bounds = malloc(sizeof(float) * BENCH_SPHERE_COUNT * 4);
    int32_t i;
    for (i = 0; i < BENCH_SPHERE_COUNT; i ++) {
        bounds[i * 4 + 0] = (float)(i % BENCH_SPHERE_SPAN) * 4.0f - 200.0f;
        bounds[i * 4 + 1] = (float)((i / BENCH_SPHERE_SPAN) % 25) * 2.0f;
        bounds[i * 4 + 2] = (float)(i / (BENCH_SPHERE_SPAN * 25)) * 6.0f - 120.0f;
        bounds[i * 4 + 3] = 0.7f;
    }

    bench_set_items(b, BENCH_SPHERE_COUNT);

    while (bench_iter(b)) {
        box_frustum_t frustum;
        box_frustum_build(view.v, proj.v, &frustum);
        uint32_t visible = 0;
        for (i = 0; i < BENCH_SPHERE_COUNT; i ++) {
            visible += box_frustum_sphere(&frustum, bounds + i * 4, bounds[i * 4 + 3]) ? 1u : 0u;
        }

        bench_keep(visible);
    }

    free(bounds);
}
