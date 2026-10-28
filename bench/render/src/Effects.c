/**
 * @file Effects.c
 * @brief Benchmarks for the cost of the effect passes of a view on the GPU.
 *
 * A headless world renders 1000 boxes lit by a sun into a 1280x720 offscreen surface on the
 * main thread and waits for the GPU after every frame, so a case measures a whole frame; the
 * cost of an effect is the difference with the case without effects.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Effect a case runs */
typedef enum bench_effect_t {
    BENCH_EFFECT_NONE,      /**< No effects */
    BENCH_EFFECT_DISABLED,  /**< Every effect listed and disabled */
    BENCH_EFFECT_TONEMAP,   /**< Tonemap */
    BENCH_EFFECT_EXPOSURE,  /**< Tonemap with AutoExposure */
    BENCH_EFFECT_BLOOM,     /**< Bloom */
    BENCH_EFFECT_GTAO,      /**< Gtao, before the forward pass and outside the chain */
    BENCH_EFFECT_FOG,       /**< HeightFog */
    BENCH_EFFECT_SMAA,      /**< Tonemap then Smaa, its cost is the difference with tonemap */
    BENCH_EFFECT_ALL        /**< Gtao, fog, bloom and tonemap with exposure */
} bench_effect_t;

/** Add an effect entry to a view.
 *
 * @param v The view
 * @param effect The effect entity
 * @param enabled Whether the entry runs
 */
static void bench_effects_add(
    View *v,
    ecs_entity_t effect,
    bool enabled)
{
    *ecs_vec_append_t(NULL, &v->effects, ViewEffect) = (ViewEffect){
        .enabled = enabled, .effect = effect };
}

/** Render frames of 1000 lit boxes with an effect chain.
 *
 * @param b The benchmark
 * @param kind The effects of the view
 */
static void bench_effects(
    bench_t *b,
    bench_effect_t kind)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t exposure = ecs_new(world);
    ecs_add(world, exposure, AutoExposure);
    ecs_entity_t tonemap = ecs_new(world);
    ecs_set(world, tonemap, Tonemap, {.auto_exposure =
        kind == BENCH_EFFECT_TONEMAP || kind == BENCH_EFFECT_SMAA ? 0 : exposure});
    ecs_entity_t bloom = ecs_new(world);
    ecs_add(world, bloom, Bloom);
    ecs_entity_t gtao = ecs_new(world);
    ecs_add(world, gtao, Gtao);
    ecs_entity_t fog = ecs_new(world);
    ecs_add(world, fog, HeightFog);
    ecs_entity_t smaa = ecs_new(world);
    ecs_add(world, smaa, Smaa);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 8, 30});
    ecs_set(world, camera, Rotation, {-0.3f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 50, .near_ = 0.1f});
    View value = {.camera = camera, .clear = {.enabled = true, .color = {110, 150, 210, 255}},
        .lighting.enabled = true};
    ecs_vec_init_t(NULL, &value.effects, ViewEffect, 4);
    bool disabled = kind == BENCH_EFFECT_DISABLED;
    if (kind == BENCH_EFFECT_GTAO || kind == BENCH_EFFECT_ALL || disabled) {
        bench_effects_add(&value, gtao, !disabled);
    }

    if (kind == BENCH_EFFECT_FOG || kind == BENCH_EFFECT_ALL || disabled) {
        bench_effects_add(&value, fog, !disabled);
    }

    if (kind == BENCH_EFFECT_BLOOM || kind == BENCH_EFFECT_ALL || disabled) {
        bench_effects_add(&value, bloom, !disabled);
    }

    if (kind == BENCH_EFFECT_TONEMAP || kind == BENCH_EFFECT_EXPOSURE ||
        kind == BENCH_EFFECT_SMAA || kind == BENCH_EFFECT_ALL || disabled)
    {
        bench_effects_add(&value, tonemap, !disabled);
    }

    if (kind == BENCH_EFFECT_SMAA) {
        bench_effects_add(&value, smaa, true);
    }

    ecs_entity_t view = ecs_new(world);
    ecs_set_ptr(world, view, View, &value);
    ecs_vec_fini_t(NULL, &value.effects, ViewEffect);
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.5423041f, 2.3679806f, -0.4669112f});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f * GLM_PIf});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {60, 0.2f, 60});
    ecs_set(world, floor, Rgb, {90, 96, 104});
    int32_t x, z;
    for (x = 0; x < 40; x ++) {
        for (z = 0; z < 25; z ++) {
            ecs_entity_t e = ecs_new(world);
            ecs_set(world, e, Position, {(float)x * 1.2f - 24, 0.5f, (float)z * 1.2f - 20});
            ecs_set(world, e, Box, {0.6f, 1.0f + (float)(x % 3) * 0.5f, 0.6f});
            ecs_set(world, e, Rgb, {200, 120, 80});
        }
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 1280, .height = 720, .resolution_scale = 1});
    uint32_t word = 0;
    int32_t i;
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

void Effects_none(
    bench_t *b)
{
    // lighting/effects
    bench_effects(b, BENCH_EFFECT_NONE);
}

void Effects_disabled(
    bench_t *b)
{
    // lighting/effects
    bench_effects(b, BENCH_EFFECT_DISABLED);
}

void Effects_tonemap(
    bench_t *b)
{
    // lighting/effects
    bench_effects(b, BENCH_EFFECT_TONEMAP);
}

void Effects_auto_exposure(
    bench_t *b)
{
    // lighting/effects
    bench_effects(b, BENCH_EFFECT_EXPOSURE);
}

void Effects_bloom(
    bench_t *b)
{
    // lighting/effects
    bench_effects(b, BENCH_EFFECT_BLOOM);
}

void Effects_smaa(
    bench_t *b)
{
    // lighting/effects
    bench_effects(b, BENCH_EFFECT_SMAA);
}

void Effects_gtao(
    bench_t *b)
{
    // lighting/effects
    bench_effects(b, BENCH_EFFECT_GTAO);
}

void Effects_fog(
    bench_t *b)
{
    // lighting/effects
    bench_effects(b, BENCH_EFFECT_FOG);
}

void Effects_all(
    bench_t *b)
{
    // lighting/effects
    bench_effects(b, BENCH_EFFECT_ALL);
}
