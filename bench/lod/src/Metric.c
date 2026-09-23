/**
 * @file Metric.c
 * @brief Benchmarks for the screen space error of a hundred thousand nodes.
 */

#include <box.h>
#include <bake_bench.h>

/** Nodes the metric is evaluated for in one iteration */
#define BENCH_NODE_COUNT (100000)

/** Nodes along one axis of the grid the benchmark walks */
#define BENCH_NODE_SIDE (50)

/** Levels of every node of the selection benchmark */
#define BENCH_LEVEL_COUNT (4)

void Metric_error_100k_perspective(
    bench_t *b)
{
    // lod/screen_space_error
    box_lod_bounds_t *bounds;
    box_lod_camera_t camera = { .position = vec3_new(0, 40, -120), .fov = 60 };
    int32_t i;

    ecs_os_init();
    bounds = ecs_os_malloc_n(box_lod_bounds_t, BENCH_NODE_COUNT);

    for (i = 0; i < BENCH_NODE_COUNT; i ++) {
        float x = (float)(i % BENCH_NODE_SIDE) * 4.0f;
        float y = (float)((i / BENCH_NODE_SIDE) % BENCH_NODE_SIDE) * 3.0f;
        float z = (float)(i / (BENCH_NODE_SIDE * BENCH_NODE_SIDE)) * 4.0f;

        bounds[i].min = vec3_new(x - 1.5f, y - 1.5f, z - 1.5f);
        bounds[i].max = vec3_new(x + 1.5f, y + 1.5f, z + 1.5f);
    }

    bench_set_items(b, BENCH_NODE_COUNT);

    while (bench_iter(b)) {
        float total = 0;

        for (i = 0; i < BENCH_NODE_COUNT; i ++) {
            total += box_lod_error_px(&bounds[i], 0.2f, &camera, 1080.0f);
        }

        bench_keep(total);
    }

    ecs_os_free(bounds);
}

void Metric_error_100k_orthographic(
    bench_t *b)
{
    // lod/screen_space_error
    box_lod_bounds_t *bounds;
    box_lod_camera_t camera = { .position = vec3_new(0, 40, -120),
        .ortho_height = 80, .orthographic = true };
    int32_t i;

    ecs_os_init();
    bounds = ecs_os_malloc_n(box_lod_bounds_t, BENCH_NODE_COUNT);

    for (i = 0; i < BENCH_NODE_COUNT; i ++) {
        float x = (float)(i % BENCH_NODE_SIDE) * 4.0f;
        float y = (float)((i / BENCH_NODE_SIDE) % BENCH_NODE_SIDE) * 3.0f;
        float z = (float)(i / (BENCH_NODE_SIDE * BENCH_NODE_SIDE)) * 4.0f;

        bounds[i].min = vec3_new(x - 1.5f, y - 1.5f, z - 1.5f);
        bounds[i].max = vec3_new(x + 1.5f, y + 1.5f, z + 1.5f);
    }

    bench_set_items(b, BENCH_NODE_COUNT);

    while (bench_iter(b)) {
        float total = 0;

        for (i = 0; i < BENCH_NODE_COUNT; i ++) {
            total += box_lod_error_px(&bounds[i], 0.2f, &camera, 1080.0f);
        }

        bench_keep(total);
    }

    ecs_os_free(bounds);
}

void Metric_select_100k(
    bench_t *b)
{
    // lod/screen_space_error
    box_lod_bounds_t *bounds;
    int32_t *levels;
    float errors[BENCH_LEVEL_COUNT] = {0.0f, 0.05f, 0.2f, 0.8f};
    box_lod_camera_t camera = { .position = vec3_new(0, 40, -120), .fov = 60 };
    int32_t i;

    ecs_os_init();
    bounds = ecs_os_malloc_n(box_lod_bounds_t, BENCH_NODE_COUNT);
    levels = ecs_os_calloc_n(int32_t, BENCH_NODE_COUNT);

    for (i = 0; i < BENCH_NODE_COUNT; i ++) {
        float x = (float)(i % BENCH_NODE_SIDE) * 4.0f;
        float y = (float)((i / BENCH_NODE_SIDE) % BENCH_NODE_SIDE) * 3.0f;
        float z = (float)(i / (BENCH_NODE_SIDE * BENCH_NODE_SIDE)) * 4.0f;

        bounds[i].min = vec3_new(x - 1.5f, y - 1.5f, z - 1.5f);
        bounds[i].max = vec3_new(x + 1.5f, y + 1.5f, z + 1.5f);
    }

    bench_set_items(b, BENCH_NODE_COUNT);

    while (bench_iter(b)) {
        for (i = 0; i < BENCH_NODE_COUNT; i ++) {
            float scale = box_lod_error_scale(&bounds[i], &camera, 1080.0f);
            float px[BENCH_LEVEL_COUNT];
            box_lod_select_desc_t desc;
            int32_t j;

            for (j = 0; j < BENCH_LEVEL_COUNT; j ++) {
                px[j] = errors[j] * scale;
            }

            desc = (box_lod_select_desc_t){ .error_px = px, .count = BENCH_LEVEL_COUNT,
                .current = levels[i], .threshold = 2.0f, .hysteresis = 0.25f };
            levels[i] = box_lod_select(&desc);
        }

        bench_keep(levels[0]);
    }

    ecs_os_free(levels);
    ecs_os_free(bounds);
}
