/**
 * @file HiZ.c
 * @brief Benchmark for the screen bound projection and the occlusion test of the pyramid.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/renderer/hiz.h"

/** Bounding spheres the occlusion benchmark projects */
#define BENCH_SPHERE_COUNT (100000)

/** Spheres on one axis of the grid the benchmark spreads them over */
#define BENCH_SPHERE_SPAN (100)

/** Width of the pyramid the benchmark tests against */
#define BENCH_HIZ_WIDTH (1280)

/** Height of the pyramid the benchmark tests against */
#define BENCH_HIZ_HEIGHT (720)

void HiZ_project_100k(bench_t *b)
{
    // culling/hi_z
    mat4 view = mat4_look_at(vec3_new(0, 60, -160), vec3_new(0, 0, 0), vec3_new(0, 1, 0));
    mat4 proj = mat4_perspective(3.14159265f / 3.0f, 16.0f / 9.0f, 0.1f);
    float clip[16];
    box_frustum_mul(proj.v, view.v, clip);

    float *bounds = malloc(sizeof(float) * BENCH_SPHERE_COUNT * 4);
    int32_t i;
    for (i = 0; i < BENCH_SPHERE_COUNT; i ++) {
        bounds[i * 4 + 0] = (float)(i % BENCH_SPHERE_SPAN) * 4.0f - 200.0f;
        bounds[i * 4 + 1] = (float)((i / BENCH_SPHERE_SPAN) % 25) * 2.0f;
        bounds[i * 4 + 2] = (float)(i / (BENCH_SPHERE_SPAN * 25)) * 6.0f - 120.0f;
        bounds[i * 4 + 3] = 0.7f;
    }

    box_hiz_pyramid_t pyramid = {
        .width = BENCH_HIZ_WIDTH,
        .height = BENCH_HIZ_HEIGHT,
        .count = box_hiz_levels(BENCH_HIZ_WIDTH, BENCH_HIZ_HEIGHT, 0)
    };
    float *storage[BOX_HIZ_LEVELS_MAX];
    uint32_t level;
    for (level = 0; level < pyramid.count; level ++) {
        uint32_t texels = box_hiz_extent(BENCH_HIZ_WIDTH, level) *
            box_hiz_extent(BENCH_HIZ_HEIGHT, level);
        storage[level] = malloc(sizeof(float) * texels);
        uint32_t texel;
        for (texel = 0; texel < texels; texel ++) {
            storage[level][texel] = 0.02f;
        }

        pyramid.levels[level] = storage[level];
    }

    bench_set_items(b, BENCH_SPHERE_COUNT);

    while (bench_iter(b)) {
        uint32_t shown = 0;
        for (i = 0; i < BENCH_SPHERE_COUNT; i ++) {
            box_hiz_rect_t rect;
            box_hiz_project(clip, bounds + i * 4, bounds[i * 4 + 3], &rect);
            shown += box_hiz_visible(&pyramid, &rect) ? 1u : 0u;
        }

        bench_keep(shown);
    }

    for (level = 0; level < pyramid.count; level ++) {
        free(storage[level]);
    }

    free(bounds);
}
