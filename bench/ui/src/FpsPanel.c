/**
 * @file FpsPanel.c
 * @brief Benchmarks for the cost of the fps panel in a frame.
 *
 * Every case runs a headless frame of an empty world with a full window of frame samples that
 * the engine does not add to (hold). The difference between the cases is the cost of the panel:
 * closed, open with a new sample every frame so the strip scrolls and one bar is rewritten, and
 * open with fixed samples.
 */

#include <box.h>
#include <bake_bench.h>

/** Create a headless world with a full window of fixed frame samples.
 *
 * @param open Whether the panel is open
 * @return The world
 */
static ecs_world_t* bench_fps_world(
    bool open)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    FrameSamples *samples = ecs_singleton_get_mut(world, FrameSamples);
    int32_t i;
    samples->hold = true;
    for (i = 0; i < BOX_FRAME_WINDOW; i ++) {
        box_frame_samples_add(samples, 10.0 + (double)(i % 31));
    }

    ecs_singleton_modified(world, FrameSamples);
    ecs_singleton_set(world, FpsPanel, { .open = open });
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    return world;
}

/** Run frames that each add a sample, so the strip of an open panel scrolls one slot and only
 * the bar of the new sample is rewritten. The sample is notified with ecs_singleton_modified, as
 * the engine does.
 *
 * @param b The bench
 * @param open Whether the panel is open
 */
static void bench_fps_shifting(
    bench_t *b,
    bool open)
{
    ecs_world_t *world = bench_fps_world(open);
    int32_t i = 0;
    while (bench_iter(b)) {
        FrameSamples *samples = ecs_singleton_get_mut(world, FrameSamples);
        box_frame_samples_add(samples, 10.0 + (double)(i ++ % 31));
        ecs_singleton_modified(world, FrameSamples);
        ecs_progress(world, 0);
    }

    ecs_fini(world);
}

void FpsPanel_closed(
    bench_t *b)
{
    // fps/panel
    bench_fps_shifting(b, false);
}

void FpsPanel_open_new_sample(
    bench_t *b)
{
    // fps/panel
    bench_fps_shifting(b, true);
}

void FpsPanel_open_fixed_samples(
    bench_t *b)
{
    // fps/panel
    ecs_world_t *world = bench_fps_world(true);
    while (bench_iter(b)) {
        ecs_progress(world, 0);
    }

    ecs_fini(world);
}
