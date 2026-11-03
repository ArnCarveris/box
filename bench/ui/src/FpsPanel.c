/**
 * @file FpsPanel.c
 * @brief Benchmarks for the cost of the fps panel in a frame.
 *
 * Every case runs a headless frame of an empty world with a full window of held frame samples.
 * The cases differ by the cost of the panel: closed, open with fixed samples, and open with a new
 * sample every frame, which moves every bar one slot left.
 *
 * In open_new_sample the samples differ, so nearly every bar and the summary change; in
 * open_steady_sample they are all as long, so the script evaluates every bar and sets none.
 */

#include <box.h>
#include <bake_bench.h>

/** Create a headless world with a full window of fixed frame samples. It runs unpaced, so the
 * CPU budget of a headless run never sleeps in a measured frame.
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
    app->unpaced = true;
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

/** Run frames that each add a sample, so the bars of an open panel each show the sample of the
 * bar right of them. The sample is notified with ecs_singleton_modified, as the engine does.
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

void FpsPanel_open_steady_sample(
    bench_t *b)
{
    // fps/panel
    ecs_world_t *world = bench_fps_world(true);
    FrameSamples *samples = ecs_singleton_get_mut(world, FrameSamples);
    int32_t i;
    for (i = 0; i < BOX_FRAME_WINDOW; i ++) {
        box_frame_samples_add(samples, 1000.0 / 60.0);
    }

    ecs_singleton_modified(world, FrameSamples);
    ecs_progress(world, 0);
    while (bench_iter(b)) {
        samples = ecs_singleton_get_mut(world, FrameSamples);
        box_frame_samples_add(samples, 1000.0 / 60.0);
        ecs_singleton_modified(world, FrameSamples);
        ecs_progress(world, 0);
    }

    ecs_fini(world);
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
