/**
 * @file StarField.c
 * @brief Benchmarks for the cost of the stars of a StarField a view draws on the GPU.
 *
 * A headless world renders 1000 lit boxes over a floor that covers the lower half of a 1280x720
 * offscreen surface, with bloom and the tone map, on the main thread and waits for the GPU after
 * every frame, so a case measures a whole frame; the stars show on the upper half.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Render frames of 1000 lit boxes under a star field.
 *
 * @param b The benchmark
 * @param stars Stars of the field, 0 for no field
 */
static void bench_star_field(
    bench_t *b,
    int32_t stars)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 4, 30});
    ecs_set(world, camera, Rotation, {0.05f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 50, .near_ = 0.1f});
    ecs_entity_t bloom = ecs_new(world);
    ecs_set(world, bloom, Bloom, {.intensity = 0.3f, .threshold = 1,
        .threshold_softness = 0.5f});
    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    View value = {.camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}},
        .lighting.enabled = true};
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, 2);
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){.effect = bloom,
        .enabled = true};
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){.effect = tonemap,
        .enabled = true};
    ecs_entity_t view = ecs_new(world);
    ecs_set_ptr(world, view, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.5423041f, 2.3679806f, -0.4669112f});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f * GLM_PIf});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {400, 0.2f, 400});
    ecs_set(world, floor, Rgb, {90, 96, 104});
    int32_t x, z, i;
    for (x = 0; x < 40; x ++) {
        for (z = 0; z < 25; z ++) {
            ecs_entity_t e = ecs_new(world);
            ecs_set(world, e, Position, {(float)x * 1.2f - 24, 0.5f, (float)z * 1.2f - 20});
            ecs_set(world, e, Box, {0.6f, 1.0f + (float)(x % 3) * 0.5f, 0.6f});
            ecs_set(world, e, Rgb, {200, 120, 80});
        }
    }

    if (stars) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Rotation, {0, 0, 0.6f});
        ecs_add(world, e, StarField);
        StarField *f = ecs_get_mut(world, e, StarField);
        f->count = stars;
        ecs_modified(world, e, StarField);
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 1280, .height = 720, .resolution_scale = 1});
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

void StarField_none(
    bench_t *b)
{
    bench_star_field(b, 0);
}

void StarField_stars_50000(
    bench_t *b)
{
    bench_star_field(b, 50000);
}
