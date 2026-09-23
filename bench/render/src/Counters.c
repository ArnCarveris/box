/**
 * @file Counters.c
 * @brief Benchmark for the cost of the per frame counts on the frames the renderer records.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/renderer/renderer.h"

/** Blended quads and boxes, alternating in depth so the sorted pass draws each on its own */
#define BENCH_COUNTED_SHAPES (2000)

/** Frames the world progresses before the measurement, which builds the meshes */
#define BENCH_COUNTED_WARMUP (60)

/** Edge of the offscreen target the benchmark renders into */
#define BENCH_COUNTED_SIZE (256)

/** Build a scene whose frames record a draw call per shape.
 *
 * @return The world
 */
static ecs_world_t* box_bench_counted_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, {0, 30, -30});
    ecs_set(world, cam, LookAt, {0, 0, 0});
    ecs_set(world, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = cam, .enabled = true});
    int32_t i;
    for (i = 0; i < BENCH_COUNTED_SHAPES; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)(i % 50) - 25.0f, 0, (float)i * 0.02f - 20.0f});
        if (i % 2) {
            ecs_set(world, e, Box, {0.8f, 0.8f, 0.8f});
        } else {
            ecs_set(world, e, Quad, {0.8f, 0.8f});
        }

        ecs_set(world, e, Opacity, {0.5f});
        ecs_set(world, e, BlendMode, {AlphaBlend});
    }

    return world;
}

/** Render frames of the scene while the renderer counts what the flags ask for.
 *
 * @param b The benchmark
 * @param flags BOX_COUNT_ flags, zero to count nothing
 */
static void box_bench_counted(
    bench_t *b,
    uint32_t flags)
{
    ecs_world_t *world = box_bench_counted_world();
    box_renderer_desc_t desc = { .width = BENCH_COUNTED_SIZE, .height = BENCH_COUNTED_SIZE };
    box_renderer_t *r = box_renderer_create(&desc);
    if (!r) {
        goto done;
    }

    int32_t i;
    for (i = 0; i < BENCH_COUNTED_WARMUP; i ++) {
        ecs_progress(world, 0.016f);
        box_packet_t *p = box_renderer_acquire(r);
        p->width = BENCH_COUNTED_SIZE;
        p->height = BENCH_COUNTED_SIZE;
        box_render_extract(world, p);
        box_renderer_submit(r, p);
    }

    box_renderer_set_counters(r, flags);
    bench_set_items(b, BENCH_COUNTED_SHAPES);
    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_t *p = box_renderer_acquire(r);
        p->width = BENCH_COUNTED_SIZE;
        p->height = BENCH_COUNTED_SIZE;
        box_render_extract(world, p);
        bench_resume(b);
        box_renderer_submit(r, p);
    }

    bench_keep(box_renderer_frames(r));
    box_renderer_destroy(r);
done:
    ecs_fini(world);
}

void Counters_record_2000_shapes_uncounted(
    bench_t *b)
{
    // monitoring/stats_command
    box_bench_counted(b, 0);
}

void Counters_record_2000_shapes_counted(
    bench_t *b)
{
    // monitoring/stats_command
    box_bench_counted(b, BOX_COUNT_FRAME);
}

void Counters_record_2000_shapes_all_details(
    bench_t *b)
{
    // monitoring/stats_details
    box_bench_counted(b, BOX_COUNT_FRAME | BOX_COUNT_PASSES | BOX_COUNT_PIPELINES |
        BOX_COUNT_BIND_GROUPS | BOX_COUNT_BUFFERS | BOX_COUNT_COMPUTE);
}
