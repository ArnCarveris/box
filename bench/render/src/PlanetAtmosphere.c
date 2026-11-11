/**
 * @file PlanetAtmosphere.c
 * @brief Benchmarks for the cost of the atmosphere shells of PlanetAtmosphere on the GPU.
 *
 * A headless world renders planets lit by a sun into a 1920x1080 offscreen surface on the main
 * thread and waits for the GPU after every frame, so a case measures a whole frame; the cost of
 * the shells is the difference to the same planets without them. One planet fills the image,
 * or 18 small planets in a 6x3 grid cover a part of it.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Columns of the grid of the small planets */
#define BENCH_PLANET_COLUMNS (6)

/** Rows of the grid of the small planets */
#define BENCH_PLANET_ROWS (3)

/** Render frames of planets, with or without atmosphere shells.
 *
 * @param b The benchmark
 * @param many Whether 18 small planets render, else one that fills the image
 * @param atmosphere Whether the planets have a PlanetAtmosphere
 */
static void bench_planets(
    bench_t *b,
    bool many,
    bool atmosphere)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, many ? 12.0f : 1.3f});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.01f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    ecs_modified(world, view, View);
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {-0.2f, -2.0f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 6});
    int32_t x, y, i = 0;
    for (y = 0; y < (many ? BENCH_PLANET_ROWS : 1); y ++) {
        for (x = 0; x < (many ? BENCH_PLANET_COLUMNS : 1); x ++) {
            ecs_entity_t e = ecs_new(world);
            float px = many ? (float)x * 3 - 7.5f : 0;
            float py = many ? (float)y * 3 - 3 : 0;
            ecs_set(world, e, Position, {px, py, 0});
            ecs_set(world, e, Sphere, {.segments = 64, .smooth = true, .radius = 1});
            ecs_set(world, e, Rgb, {90, 110, 80});
            if (atmosphere) {
                ecs_add(world, e, PlanetAtmosphere);
                PlanetAtmosphere *a = ecs_get_mut(world, e, PlanetAtmosphere);
                a->planet_radius = 2000;
                a->rayleigh.scattering_scale = 1 + (float)(i % 3);
                ecs_modified(world, e, PlanetAtmosphere);
            }

            i ++;
        }
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 1920, .height = 1080, .resolution_scale = 1});
    uint32_t word = 0;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }

    bench_set_items(b, 1);
    while (bench_iter(b)) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
        bench_keep(word);
    }

    ecs_fini(world);
}

void PlanetAtmosphere_one_bare(
    bench_t *b)
{
    // lighting/planet_atmosphere
    bench_planets(b, false, false);
}

void PlanetAtmosphere_one(
    bench_t *b)
{
    // lighting/planet_atmosphere
    bench_planets(b, false, true);
}

void PlanetAtmosphere_many_bare(
    bench_t *b)
{
    // lighting/planet_atmosphere
    bench_planets(b, true, false);
}

void PlanetAtmosphere_many(
    bench_t *b)
{
    // lighting/planet_atmosphere
    bench_planets(b, true, true);
}
