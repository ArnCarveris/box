/**
 * @file Pipelines.c
 * @brief Benchmark for the forward pipeline lookup of every draw with many states in use.
 *
 * 1800 rounded boxes of distinct proportions, each its own batch and one of the 18 blend and
 * back face states, render into two 32x32 surfaces (one through tonemap): 3600 draws over 36
 * pipelines per frame. The timed loop does not wait for the GPU; one readback follows it.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Shapes of the scene, each with its own mesh and so its own batch */
#define BENCH_PIPELINES_SHAPES (1800)

void Pipelines_state_combinations(
    bench_t *b)
{
    // render_fixes/pipeline_cache
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 200});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 0.2f, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t root_ldr = ecs_new(world);
    ecs_entity_t root_hdr = ecs_new(world);
    ecs_entity_t ldr = ecs_new_w_pair(world, EcsChildOf, root_ldr);
    ecs_set(world, ldr, View, {.camera = camera, .clear = {.enabled = true,
        .color = {128, 128, 128, 255}}});
    ecs_entity_t tonemap = ecs_new(world);
    ecs_add(world, tonemap, Tonemap);
    ecs_entity_t hdr = ecs_new_w_pair(world, EcsChildOf, root_hdr);
    View value = {.camera = camera, .clear = {.enabled = true, .color = {128, 128, 128, 255}}};
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, 1);
    *ecs_vec_append_t(NULL, &value.effects, ViewEffect) = (ViewEffect){
        .enabled = true, .effect = tonemap };
    ecs_set_ptr(world, hdr, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);

    int32_t i;
    for (i = 0; i < BENCH_PIPELINES_SHAPES; i ++) {
        ecs_entity_t e = ecs_new(world);
        BlendMode blend = (BlendMode)(i % 9);
        BackFaceMode back_face = (BackFaceMode)((i / 9) % 2);
        ecs_set(world, e, Position, {(float)(i % 45) * 3 - 66, (float)(i / 45) * 3 - 60, 0});
        ecs_set(world, e, RoundedBox, {1, 1 + (float)i / 1024.0f, 1, 0.1f, 2});
        ecs_set(world, e, Rgb, {255, 0, 0});
        ecs_set_ptr(world, e, BlendMode, &blend);
        ecs_set_ptr(world, e, BackFaceMode, &back_face);
    }

    ecs_entity_t s1 = ecs_new(world);
    ecs_set(world, s1, Surface, {.width = 32, .height = 32, .resolution_scale = 1,
        .view_root = root_ldr});
    ecs_entity_t s2 = ecs_new(world);
    ecs_set(world, s2, Surface, {.width = 32, .height = 32, .resolution_scale = 1,
        .view_root = root_hdr});
    uint32_t word = 0;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
    }

    bench_set_items(b, BENCH_PIPELINES_SHAPES * 2);
    while (bench_iter(b)) {
        ecs_progress(world, 1.0f / 60);
    }

    box_render_sync(world);
    box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
    bench_keep(word);
    ecs_fini(world);
}
