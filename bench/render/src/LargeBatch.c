/**
 * @file LargeBatch.c
 * @brief Benchmark for frames of one key with as many instances as one batch holds, and more.
 *
 * Static unlit boxes of one key render into a 64x64 headless surface with the GPU waited for
 * every frame; instructions (macOS) are those the process retired over the measured frames,
 * frames the frames they ran.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"
#ifdef __APPLE__
#include <libproc.h>
#include <unistd.h>
#endif

/** Get the instructions the process retired so far.
 *
 * @return The instructions, 0 where the platform does not count them
 */
static uint64_t bench_large_instructions(void)
{
    uint64_t result = 0;
#ifdef __APPLE__
    struct rusage_info_v4 info;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t*)&info)) {
        result = info.ri_instructions;
    }
#endif

    return result;
}

/** Render frames of a number of static boxes of one key.
 *
 * @param b The benchmark
 * @param count Boxes of the scene
 */
static void bench_large(
    bench_t *b,
    int32_t count)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 1200});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 0.05f, .near_ = 0.1f,
        .far_ = 2000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {0, 0, 0, 255}}, .culling = {.enabled = true, .min_dimension_px = 1}});
    Position *pos = ecs_os_malloc_n(Position, count);
    Box *box = ecs_os_malloc_n(Box, count);
    Rgb *rgb = ecs_os_malloc_n(Rgb, count);
    int32_t i;
    for (i = 0; i < count; i ++) {
        pos[i] = (Position){(float)(i % 1024) - 512, (float)((i / 1024) % 1024) - 512,
            -(float)(i / (1024 * 1024))};
        box[i] = (Box){0.8f, 0.8f, 0.8f};
        rgb[i] = (Rgb){255, 255, 255};
    }

    void *data[] = {pos, box, rgb};
    ecs_bulk_init(world, &(ecs_bulk_desc_t){
        .count = count,
        .ids = {ecs_id(Position), ecs_id(Box), ecs_id(Rgb)},
        .data = data
    });
    ecs_os_free(pos);
    ecs_os_free(box);
    ecs_os_free(rgb);
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 64, .resolution_scale = 1});
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }

    uint32_t word = 0;
    double frames = 0;
    uint64_t start = bench_large_instructions();
    bench_set_items(b, count);
    while (bench_iter(b)) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
        bench_keep(word);
        frames ++;
    }

    bench_counter(b, "instructions", (double)(bench_large_instructions() - start));
    bench_counter(b, "frames", frames);
    ecs_fini(world);
}

void LargeBatch_instances_1m(
    bench_t *b)
{
    // render_fixes/unbounded_batches
    bench_large(b, 1 << 20);
}

void LargeBatch_instances_1m5(
    bench_t *b)
{
    // render_fixes/unbounded_batches
    bench_large(b, (1 << 20) + (1 << 19));
}
