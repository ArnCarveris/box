/**
 * @file Particle.c
 * @brief Tests for the particle passes: streams render where their particles are, scenes and
 * full pools are respected and a fixed seed renders the same image twice.
 *
 * Frames advance a fixed 1/60 s.
 * An unlit orthographic camera 10 units in front of the origin looks into a 32x32 capture of
 * 8 units, 4 pixels a unit: world (0, 0) lands on pixel (16, 16) and y grows toward row 0.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Edge of the capture in pixels */
#define SIZE (32)

/** Create a headless world with a camera, a view that clears to black and a surface.
 *
 * @param file Name of the PNG the surface writes every frame
 * @return The world
 */
static ecs_world_t* world_new(
    const char *file)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 8, .near_ = 0.1f,
        .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}, .lighting.enabled = false});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = SIZE, .height = SIZE, .resolution_scale = 1,
        .write_to_file = ECS_CONST_CAST(char*, file)});

    return world;
}

/** Create a white emitter at the origin that rises at 1 unit per second.
 *
 * @param world The world
 * @param parent Parent of the emitter, 0 for none
 * @param rate Particles per second
 * @param max Particles alive at once
 * @return The emitter
 */
static ecs_entity_t emitter_new(
    ecs_world_t *world,
    ecs_entity_t parent,
    float rate,
    int32_t max)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_add(world, e, ParticleEmitter);
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->rate = rate;
    p->max = max;
    p->lifetime = (ParticleRange){100, 100};
    p->size = (ParticleRange){1, 1};
    ecs_modified(world, e, ParticleEmitter);

    return e;
}

/** Run frames and load the image the last one wrote.
 *
 * @param world The world
 * @param file Name of the PNG the surface writes
 * @param frames Frames to run
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* run(
    ecs_world_t *world,
    const char *file,
    int32_t frames)
{
    int32_t i;
    for (i = 0; i < frames; i ++) {
        ecs_progress(world, 1.0f / 60.0f);
    }

    box_render_sync(world);
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    ecs_os_free(path);
    test_assert(px != NULL);
    test_int(w, SIZE);
    test_int(h, SIZE);

    return px;
}

/** Get the green channel of the pixel at a world position on the plane of the emitters.
 *
 * @param px The pixels
 * @param x World x
 * @param y World y
 * @return The channel
 */
static int32_t green_at(
    const unsigned char *px,
    float x,
    float y)
{
    int32_t col = (int32_t)(16.0f + x * 4.0f), row = (int32_t)(16.0f - y * 4.0f);

    return px[(row * SIZE + col) * 4 + 1];
}

void Particle_stream_renders_where_expected(void) {
    // particle/draw
    ecs_world_t *world = world_new("stream.png");
    emitter_new(world, 0, 400, 1000);
    unsigned char *px = run(world, "stream.png", 60);

    test_assert(green_at(px, 0, 0.5f) > 128);
    test_assert(green_at(px, 0, 0.25f) > 128);
    test_assert(green_at(px, 3, 0.5f) < 8);
    test_assert(green_at(px, -3, 0.5f) < 8);
    test_assert(green_at(px, 0, 3) < 8);
    test_assert(green_at(px, 0, -2) < 8);
    int32_t live = ecs_singleton_get(world, ParticleStats)->particles;
    test_assert(live > 380 && live <= 400);

    stbi_image_free(px);
    ecs_fini(world);
}

void Particle_other_scene_does_not_show(void) {
    // particle/scenes
    ecs_world_t *world = world_new("scene.png");
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    emitter_new(world, scene, 400, 1000);
    unsigned char *px = run(world, "scene.png", 30);

    int32_t i, lit = 0;
    for (i = 0; i < SIZE * SIZE; i ++) {
        lit += px[i * 4] > 8 || px[i * 4 + 1] > 8 || px[i * 4 + 2] > 8;
    }

    test_int(lit, 0);
    int32_t live = ecs_singleton_get(world, ParticleStats)->particles;
    test_assert(live > 180 && live <= 200);

    stbi_image_free(px);
    ecs_fini(world);
}

void Particle_full_pool_stops_spawning(void) {
    // particle/spawn
    ecs_world_t *world = world_new("full.png");
    emitter_new(world, 0, 60, 3);
    unsigned char *px = run(world, "full.png", 90);

    test_assert(green_at(px, 0, 1.5f) > 64);
    test_assert(green_at(px, 0, 0) < 8);
    test_assert(green_at(px, 0, 0.75f) < 8);
    const ParticleStats *stats = ecs_singleton_get(world, ParticleStats);
    test_int(stats->particles, 3);
    test_int(stats->spawned, 0);

    stbi_image_free(px);
    ecs_fini(world);
}

void Particle_fixed_seed_same_image(void) {
    // particle/determinism
    unsigned char *px[2];
    int32_t w;
    for (w = 0; w < 2; w ++) {
        ecs_world_t *world = world_new("seed.png");
        ecs_entity_t e = emitter_new(world, 0, 300, 1000);
        ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
        p->shape = EmitterSphere;
        p->radius = 0.5f;
        p->spread = 45;
        p->speed = (ParticleRange){0.5f, 2};
        p->size = (ParticleRange){0.2f, 0.8f};
        p->opacity = (ParticleRange){0.2f, 0.8f};
        p->acceleration[1] = -1;
        p->color = (rgb){255, 128, 0};
        p->seed = 5;
        ecs_modified(world, e, ParticleEmitter);
        px[w] = run(world, "seed.png", 45);
        ecs_fini(world);
    }

    int32_t i, lit = 0;
    for (i = 0; i < SIZE * SIZE; i ++) {
        lit += px[0][i * 4] > 8;
    }

    test_assert(lit > 20);
    test_assert(memcmp(px[0], px[1], SIZE * SIZE * 4) == 0);
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}
