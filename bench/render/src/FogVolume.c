/**
 * @file FogVolume.c
 * @brief Benchmarks for the cost of the fog volumes a view lights in its froxel grid on the GPU.
 *
 * A headless world renders 1000 boxes lit by a shadow casting sun into a 1280x720 offscreen
 * surface on the main thread and waits for the GPU after every frame, so a case measures a whole
 * frame; the volumes cover the whole image, the camera stands inside each of them. The light
 * cases add 16 point and spot lights over the boxes, 4 of them shadowed, to one volume. The
 * screen case renders 1920x1080 inside one volume with noise, as the light shafts of a forest.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Lights of the light cases */
#define BENCH_FOG_LIGHTS (16)

/** Render frames of 1000 lit boxes inside fog volumes that cover the image.
 *
 * @param b The benchmark
 * @param volumes Fog volumes, all around the camera
 * @param lights Point and spot lights over the boxes, every other one a spot
 * @param light_inscatter FogVolume.light_inscatter of the volumes
 * @param width Width of the surface
 * @param height Height of the surface
 * @param noise FogVolume.noise_strength of the volumes
 */
static void bench_fog_volumes(
    bench_t *b,
    int32_t volumes,
    int32_t lights,
    float light_inscatter,
    int32_t width,
    int32_t height,
    float noise)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 8, 30});
    ecs_set(world, camera, Rotation, {-0.3f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 50, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {110, 150, 210, 255}};
    v->shadows.enabled = true;
    ecs_modified(world, view, View);
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.5423041f, 2.3679806f, -0.4669112f});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1.5f * GLM_PIf});
    ecs_entity_t floor = ecs_new(world);
    ecs_set(world, floor, Position, {0, -0.1f, 0});
    ecs_set(world, floor, Box, {60, 0.2f, 60});
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

    for (i = 0; i < volumes; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {0, 8, (float)i * 0.5f});
        ecs_add(world, e, FogVolume);
        FogVolume *f = ecs_get_mut(world, e, FogVolume);
        f->extent[0] = 40;
        f->extent[1] = 20;
        f->extent[2] = 40;
        f->density = 0.02f;
        f->light_inscatter = light_inscatter;
        f->noise_strength = noise;
        f->noise_scale = 6;
        ecs_modified(world, e, FogVolume);
    }

    for (i = 0; i < lights; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)(i % 4) * 12 - 18, 5, (float)(i / 4) * 10 - 18});
        ecs_set(world, e, Rgb, {255, 210, 160});
        if (i % 2) {
            ecs_set(world, e, Rotation, {(float)GLM_PI_2, 0, 0});
            ecs_set(world, e, SpotLight, {.intensity = 20 * GLM_PIf, .range = 10,
                .inner_angle = 20, .outer_angle = 30, .shadows = i % 4 == 1});
        } else {
            ecs_set(world, e, PointLight, {.intensity = 10 * GLM_PIf, .range = 8,
                .shadows = i % 8 == 0});
        }
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = width, .height = height,
        .resolution_scale = 1});
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

void FogVolume_none(
    bench_t *b)
{
    // lighting/fog_volume
    bench_fog_volumes(b, 0, 0, 1, 1280, 720, 0);
}

void FogVolume_one(
    bench_t *b)
{
    // lighting/fog_volume
    bench_fog_volumes(b, 1, 0, 1, 1280, 720, 0);
}

void FogVolume_sixteen(
    bench_t *b)
{
    // lighting/fog_volume
    bench_fog_volumes(b, 16, 0, 1, 1280, 720, 0);
}

void FogVolume_lights_off(
    bench_t *b)
{
    // lighting/fog_volume
    bench_fog_volumes(b, 1, BENCH_FOG_LIGHTS, 0, 1280, 720, 0);
}

void FogVolume_lights(
    bench_t *b)
{
    // lighting/fog_volume
    bench_fog_volumes(b, 1, BENCH_FOG_LIGHTS, 1, 1280, 720, 0);
}

void FogVolume_screen(
    bench_t *b)
{
    // lighting/fog_volume
    bench_fog_volumes(b, 1, 0, 1, 1920, 1080, 0.45f);
}
