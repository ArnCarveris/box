/**
 * @file AtmosphereClouds.c
 * @brief Benchmarks for the GPU cost of the CloudLayers of the sky pass.
 *
 * A headless world renders the Atmosphere sky into a 1920x1080 offscreen surface on the main
 * thread and waits for the GPU after every frame, so a case measures a whole frame. The camera
 * looks up from the ground, so the layers cover every pixel and every pixel samples each of
 * them; the cost of the layers is the difference with the case without one.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Side of the noise texture of the layers in pixels */
#define BENCH_CLOUDS_TEXTURE (512)

/** Render frames of the sky with layers of a noise texture.
 *
 * @param b The benchmark
 * @param layers Cloud layers, 0 to BOX_CLOUD_LAYER_MAX
 */
static void bench_clouds(
    bench_t *b,
    int32_t layers)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    char *file = flecs_asprintf("%s/bench.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    uint8_t *px = ecs_os_malloc(BENCH_CLOUDS_TEXTURE * BENCH_CLOUDS_TEXTURE);
    int32_t i;
    uint32_t seed = 1;
    for (i = 0; i < BENCH_CLOUDS_TEXTURE * BENCH_CLOUDS_TEXTURE; i ++) {
        seed = seed * 1664525u + 1013904223u;
        px[i] = (uint8_t)(seed >> 24);
    }

    stbi_write_png(file, BENCH_CLOUDS_TEXTURE, BENCH_CLOUDS_TEXTURE, 1, px,
        BENCH_CLOUDS_TEXTURE);
    ecs_os_free(px);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file, .single_channel = true});

    ecs_entity_t scene = ecs_new(world);
    ecs_set(world, scene, Scene, {0});
    ecs_add(world, scene, Atmosphere);
    ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, sun, DirectionalLight, {.intensity = GLM_PIf});
    ecs_set(world, sun, Rotation, {glm_rad(40), 0.5f, 0});
    Atmosphere *a = ecs_get_mut(world, scene, Atmosphere);
    a->sun = sun;
    ecs_modified(world, scene, Atmosphere);

    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 2, 0});
    ecs_set(world, camera, Rotation, {glm_rad(30), 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->clear = (ClearSettings){.enabled = true, .color = {0, 0, 0, 255}};
    v->lighting.enabled = true;
    v->lighting.atmosphere = true;
    ecs_modified(world, view, View);

    for (i = 0; i < layers; i ++) {
        ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, scene);
        ecs_add(world, e, CloudLayer);
        CloudLayer *c = ecs_get_mut(world, e, CloudLayer);
        c->texture = tex;
        c->altitude = 1000.0f + (float)i * 1500.0f;
        c->size = 3000.0f + (float)i * 1000.0f;
        c->threshold = 0.3f;
        c->density = 0.5f;
        ecs_modified(world, e, CloudLayer);
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 1920, .height = 1080, .resolution_scale = 1});
    for (i = 0; i < 100; i ++) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        const TextureStatus *status = ecs_get(world, tex, TextureStatus);
        if (i >= 3 && status && status->state != TextureLoading) {
            break;
        }
    }

    uint32_t word = 0;
    bench_set_items(b, 1);
    while (bench_iter(b)) {
        ecs_progress(world, 1.0f / 60);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_FIRST, 0, sizeof(word), &word);
        bench_keep(word);
    }

    ecs_fini(world);
    ecs_os_free(file);
}

void AtmosphereClouds_sky(
    bench_t *b)
{
    // clouds/cloud_layer
    bench_clouds(b, 0);
}

void AtmosphereClouds_layers_1(
    bench_t *b)
{
    // clouds/cloud_layer
    bench_clouds(b, 1);
}

void AtmosphereClouds_layers_4(
    bench_t *b)
{
    // clouds/cloud_layer
    bench_clouds(b, BOX_CLOUD_LAYER_MAX);
}
