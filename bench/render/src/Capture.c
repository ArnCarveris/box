/**
 * @file Capture.c
 * @brief Benchmark for the time a frame that captures its target spends on the render thread.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdio.h>
#include <unistd.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/renderer/renderer.h"

/** Boxes in the captured scene */
#define BENCH_CAPTURE_SHAPES (400)

/** Frames the world progresses before the measurement, which builds the meshes */
#define BENCH_CAPTURE_WARMUP (30)

/** Width of the offscreen target, a 1080p frame */
#define BENCH_CAPTURE_WIDTH (1920)

/** Height of the offscreen target, a 1080p frame */
#define BENCH_CAPTURE_HEIGHT (1080)

/** File the captures of the benchmark overwrite */
#define BENCH_CAPTURE_PATH "/tmp/box_bench_capture.png"

/** Build a lit scene of boxes in front of a camera.
 *
 * @return The world
 */
static ecs_world_t* box_bench_capture_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, {0, 20, -30});
    ecs_set(world, cam, LookAt, {0, 0, 0});
    ecs_set(world, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = cam, .enabled = true});
    int32_t i;
    for (i = 0; i < BENCH_CAPTURE_SHAPES; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)(i % 20) - 10.0f, 0, (float)(i / 20) - 10.0f});
        ecs_set(world, e, Box, {0.8f, 0.8f, 0.8f});
        ecs_set(world, e, Rgb, {(float)(i % 7) / 7.0f, 0.5f, (float)(i % 3) / 3.0f});
    }

    return world;
}

/** Acquire and fill the packet of the next frame.
 *
 * @param world The world
 * @param r The renderer
 * @return The packet
 */
static box_packet_t* box_bench_capture_packet(
    ecs_world_t *world,
    box_renderer_t *r)
{
    ecs_progress(world, 0.016f);
    box_packet_t *p = box_renderer_acquire(r);
    p->width = BENCH_CAPTURE_WIDTH;
    p->height = BENCH_CAPTURE_HEIGHT;
    box_render_extract(world, p);

    return p;
}

/** Time the frames of the scene, with or without a capture of the target.
 *
 * Every measured frame starts with an idle renderer, so a capture pays for its
 * own readback and nothing that is left of the previous one.
 *
 * @param b The benchmark
 * @param capture Request a capture on every measured frame
 */
static void box_bench_capture(
    bench_t *b,
    bool capture)
{
    ecs_world_t *world = box_bench_capture_world();
    box_renderer_desc_t desc = {
        .width = BENCH_CAPTURE_WIDTH, .height = BENCH_CAPTURE_HEIGHT
    };
    box_renderer_t *r = box_renderer_create(&desc);
    if (!r) {
        goto done;
    }

    int32_t i;
    for (i = 0; i < BENCH_CAPTURE_WARMUP; i ++) {
        box_renderer_submit(r, box_bench_capture_packet(world, r));
    }

    while (bench_iter(b)) {
        bench_pause(b);
        box_renderer_captures_wait(r);
        box_packet_t *p = box_bench_capture_packet(world, r);
        if (capture) {
            snprintf(p->capture_path, BOX_CAPTURE_PATH_MAX, "%s", BENCH_CAPTURE_PATH);
        }

        bench_resume(b);
        box_renderer_submit(r, p);
    }

    box_renderer_captures_wait(r);
    bench_keep(box_renderer_frames(r));
    box_renderer_destroy(r);
    unlink(BENCH_CAPTURE_PATH);
done:
    ecs_fini(world);
}

void Capture_frame_1080p_without_capture(
    bench_t *b)
{
    // command/capture
    box_bench_capture(b, false);
}

void Capture_frame_1080p_with_capture(
    bench_t *b)
{
    // command/capture
    box_bench_capture(b, true);
}
