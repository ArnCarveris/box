/**
 * @file PlanetSurfaceLight.c
 * @brief Benchmarks for the cost of Scene.planet_light in scenes without a PlanetAtmosphere.
 *
 * A headless world renders a grid of 4096 lit boxes into a 1920x1080 offscreen surface on the
 * main thread and waits for the GPU after every frame, so a case measures a whole frame: the
 * extract, which keys the changed shapes, and the forward pass of the mesh shader. The bare
 * cases have the feature off, the others on; without a PlanetAtmosphere the two should cost the
 * same. The changed cases recolor a tenth of the boxes every frame, so their keys are computed
 * again; the planet case puts the grid under a PlanetAtmosphere for comparison.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Boxes along each side of the grid */
#define BENCH_SURFACE_SIDE (64)

/** Render frames of the grid of boxes.
 *
 * @param b The benchmark
 * @param planet_light Scene.planet_light
 * @param changed Whether a tenth of the boxes change their color every frame
 * @param planet Whether the boxes are children of a PlanetAtmosphere
 */
static void bench_surface(
    bench_t *b,
    bool planet_light,
    bool changed,
    bool planet)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    ecs_singleton_set(world, Scene, {.planet_light = planet_light});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 80});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    ecs_modified(world, view, View);
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {-0.4f, -2.0f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 4});

    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Position, {0, 0, 0});
    if (planet) {
        ecs_add(world, root, PlanetAtmosphere);
        PlanetAtmosphere *a = ecs_get_mut(world, root, PlanetAtmosphere);
        a->radius = 40;
        a->intensity = 0;
        ecs_modified(world, root, PlanetAtmosphere);
    }

    ecs_entity_t *boxes = ecs_os_malloc_n(ecs_entity_t,
        BENCH_SURFACE_SIDE * BENCH_SURFACE_SIDE);
    int32_t x, y, i = 0;
    for (y = 0; y < BENCH_SURFACE_SIDE; y ++) {
        for (x = 0; x < BENCH_SURFACE_SIDE; x ++) {
            ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, root);
            ecs_set(world, e, Position, {(float)x - 31.5f, (float)y - 31.5f, 0});
            ecs_set(world, e, Box, {0.8f, 0.8f, 0.8f});
            ecs_set(world, e, Rgb, {90, 110, 80});
            boxes[i ++] = e;
        }
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 1920, .height = 1080, .resolution_scale = 1});
    uint32_t word = 0;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }

    int32_t frame = 0, count = BENCH_SURFACE_SIDE * BENCH_SURFACE_SIDE;
    bench_set_items(b, 1);
    while (bench_iter(b)) {
        if (changed) {
            for (i = frame % 10; i < count; i += 10) {
                ecs_set(world, boxes[i], Rgb, {(uint8_t)(90 + frame % 2), 110, 80});
            }
        }

        frame ++;
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
        bench_keep(word);
    }

    ecs_os_free(boxes);
    ecs_fini(world);
}

void PlanetSurfaceLight_bare(
    bench_t *b)
{
    // lighting/planet_surface_light
    bench_surface(b, false, false, false);
}

void PlanetSurfaceLight_feature(
    bench_t *b)
{
    // lighting/planet_surface_light
    bench_surface(b, true, false, false);
}

void PlanetSurfaceLight_changed_bare(
    bench_t *b)
{
    // lighting/planet_surface_light
    bench_surface(b, false, true, false);
}

void PlanetSurfaceLight_changed_feature(
    bench_t *b)
{
    // lighting/planet_surface_light
    bench_surface(b, true, true, false);
}

void PlanetSurfaceLight_planet(
    bench_t *b)
{
    // lighting/planet_surface_light
    bench_surface(b, true, false, true);
}
