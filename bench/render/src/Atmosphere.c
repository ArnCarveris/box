/**
 * @file Atmosphere.c
 * @brief Benchmarks for the main thread cost of a scene with an Atmosphere: its extract and the
 * packet it fills, with one and four views of the scene.
 *
 * Packets are dropped, so a case measures the frame on the main thread; the sun turns every
 * frame, which reaches the views but builds no LUTs. The case without an atmosphere is the base.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Bytes of packet data of the last packet the backend dropped */
static int64_t bench_atmos_bytes;

/** Remember the data size of a packet and drop it.
 *
 * @param ctx Unused
 * @param packet The packet
 */
static void bench_atmos_drop(
    void *ctx,
    const box_render_packet_t *packet)
{
    (void)ctx;
    bench_atmos_bytes = ecs_vec_count(&packet->data);
}

/** Run frames of a scene with views of cameras around it and a turning sun.
 *
 * @param b The benchmark
 * @param views Views of the scene
 * @param atmosphere Whether the scene has an Atmosphere
 */
static void bench_atmosphere(
    bench_t *b,
    int32_t views,
    bool atmosphere)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_backend_t backend = { .render = bench_atmos_drop };
    box_render_set_backend(world, &backend);
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    if (atmosphere) {
        ecs_add(world, scene, Atmosphere);
    }

    ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_set(world, sun, Rotation, {0.5f, 0, 0});
    if (atmosphere) {
        ecs_get_mut(world, scene, Atmosphere)->sun = sun;
        ecs_modified(world, scene, Atmosphere);
    }

    int32_t i;
    for (i = 0; i < views; i ++) {
        ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, camera, Position, {(float)i * 10, 2 + (float)i * 50, 10});
        ecs_set(world, camera, Rotation, {-0.2f, (float)i * 1.5f, 0});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
        ecs_entity_t view = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
    }

    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    float angle = 0.5f;
    bench_set_items(b, 1);
    while (bench_iter(b)) {
        angle += 0.001f;
        ecs_set(world, sun, Rotation, {angle, 0.3f, 0});
        ecs_progress(world, 0);
        bench_keep(bench_atmos_bytes);
    }

    box_render_sync(world);
    bench_counter(b, "packet_bytes", (double)bench_atmos_bytes);
    if (atmosphere) {
        bench_counter(b, "lut_version",
            (double)ecs_get(world, scene, AtmosphereStatus)->lut_version);
    }

    ecs_fini(world);
}

void Atmosphere_one_view(
    bench_t *b)
{
    // atmosphere/luts
    bench_atmosphere(b, 1, true);
}

void Atmosphere_four_views(
    bench_t *b)
{
    // atmosphere/luts
    bench_atmosphere(b, 4, true);
}

void Atmosphere_four_views_no_atmosphere(
    bench_t *b)
{
    // atmosphere/luts
    bench_atmosphere(b, 4, false);
}
