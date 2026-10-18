/**
 * @file Particle.c
 * @brief Tests for the particle passes: streams render where their particles are, scenes and
 * full pools are respected, a fixed seed renders the same image twice, additive pools brighten,
 * soft particles fade near geometry and sprites show the frame of their age.
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

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

/** Edge of the capture in pixels */
#define SIZE (32)

/** Create a headless world with a camera, a view that clears to a gray and a surface.
 *
 * @param file Name of the PNG the surface writes every frame
 * @param gray Channels of the clear color
 * @return The world
 */
static ecs_world_t* world_new_clear(
    const char *file,
    uint8_t gray)
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
        .clear = {.enabled = true, .color = {gray, gray, gray, 255}},
        .lighting.enabled = false});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = SIZE, .height = SIZE, .resolution_scale = 1,
        .write_to_file = ECS_CONST_CAST(char*, file)});

    return world;
}

/** Create a headless world with a camera, a view that clears to black and a surface.
 *
 * @param file Name of the PNG the surface writes every frame
 * @return The world
 */
static ecs_world_t* world_new(
    const char *file)
{
    return world_new_clear(file, 0);
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

/** Create an emitter of one white particle that stays where it spawns.
 *
 * @param world The world
 * @param x World x of the emitter
 * @param size Diameter of the particle
 * @return The emitter
 */
static ecs_entity_t single_new(
    ecs_world_t *world,
    float x,
    float size)
{
    ecs_entity_t e = emitter_new(world, 0, 60, 1);
    ecs_set(world, e, Position, {x, 0, 0});
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->direction[1] = 0;
    p->size = (ParticleRange){size, size};
    ecs_modified(world, e, ParticleEmitter);

    return e;
}

/** Get a pixel at a world position on the plane of the emitters.
 *
 * @param px The pixels
 * @param x World x
 * @param y World y
 * @return The RGBA channels of the pixel
 */
static const unsigned char* pixel_at(
    const unsigned char *px,
    float x,
    float y)
{
    int32_t col = (int32_t)(16.0f + x * 4.0f), row = (int32_t)(16.0f - y * 4.0f);

    return &px[(row * SIZE + col) * 4];
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

void Particle_additive_never_darkens(void) {
    // particle/blend
    unsigned char *px[2];
    int32_t w, i;
    for (w = 0; w < 2; w ++) {
        ecs_world_t *world = world_new_clear("blend.png", 128);
        ecs_entity_t e = emitter_new(world, 0, 400, 1000);
        ecs_get_mut(world, e, ParticleEmitter)->color = (rgb){64, 64, 64};
        ecs_modified(world, e, ParticleEmitter);
        ecs_set(world, e, ParticleAppearance, {
            .blend = w ? ParticleAdditive : ParticleAlpha});
        px[w] = run(world, "blend.png", 60);
        ecs_fini(world);
    }

    const unsigned char *alpha = pixel_at(px[0], 0, 0.5f);
    const unsigned char *additive = pixel_at(px[1], 0, 0.5f);
    test_assert(alpha[1] < 110);
    test_assert(additive[1] > 150);
    for (i = 0; i < SIZE * SIZE * 4; i ++) {
        if ((i % 4) != 3) {
            test_assert(px[1][i] >= 126);
        }
    }

    test_assert(pixel_at(px[1], 3, 3)[1] <= 130);
    stbi_image_free(px[0]);
    stbi_image_free(px[1]);
}

void Particle_soft_fades_near_wall(void) {
    // particle/soft
    ecs_world_t *world = world_new("soft.png");
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {-1, 0, -0.3f});
    ecs_set(world, wall, Box, {6, 8, 0.1f});
    ecs_set(world, wall, Rgb, {0, 0, 0});
    ecs_entity_t near_soft = single_new(world, -2.5f, 1.5f);
    ecs_entity_t near_hard = single_new(world, 0, 1.5f);
    ecs_entity_t open_soft = single_new(world, 3, 1.5f);
    ecs_set(world, near_soft, ParticleAppearance, {.softness = 2});
    ecs_set(world, near_hard, ParticleAppearance, {.softness = 0});
    ecs_set(world, open_soft, ParticleAppearance, {.softness = 2});
    unsigned char *px = run(world, "soft.png", 10);

    int32_t faded = pixel_at(px, -2.5f, 0)[1];
    int32_t hard = pixel_at(px, 0, 0)[1];
    int32_t open = pixel_at(px, 3, 0)[1];
    test_assert(hard > 200);
    test_assert(open > 200);
    test_assert(faded < 160);
    test_assert(faded > 4);
    test_assert(pixel_at(px, -2.5f, 2.5f)[1] < 8);
    test_int(ecs_singleton_get(world, ParticleStats)->particles, 3);

    stbi_image_free(px);
    ecs_fini(world);
}

void Particle_sprite_frame_texel(void) {
    // particle/sprite
    ecs_world_t *world = world_new("sprite.png");
    const char *tmp = ecs_singleton_get(world, AppStatus)->tmp_dir;
    char *file = flecs_asprintf("%s/sheet.png", tmp);
    uint8_t texels[8 * 2 * 4];
    int32_t i;
    for (i = 0; i < 16; i ++) {
        bool right = (i % 8) >= 4;
        texels[i * 4 + 0] = (uint8_t)(right ? 0 : 255);
        texels[i * 4 + 1] = (uint8_t)(right ? 255 : 0);
        texels[i * 4 + 2] = 0;
        texels[i * 4 + 3] = 255;
    }

    test_assert(stbi_write_png(file, 8, 2, 4, texels, 8 * 4) != 0);
    ecs_entity_t tex = ecs_new(world);
    ecs_set(world, tex, TexturePath, {.path = file});
    ecs_entity_t e = single_new(world, 0, 4);
    ecs_set(world, e, ParticleAppearance, {
        .shape = ParticleSprite, .texture = tex, .columns = 2, .rows = 1, .fps = 1});

    unsigned char *px = run(world, "sprite.png", 20);
    const unsigned char *c = pixel_at(px, 0, 0);
    test_assert(c[0] > 200 && c[1] < 50 && c[2] < 50);
    stbi_image_free(px);

    px = run(world, "sprite.png", 70);
    c = pixel_at(px, 0, 0);
    test_assert(c[0] < 50 && c[1] > 200 && c[2] < 50);
    test_assert(pixel_at(px, 3, 3)[1] < 8);
    stbi_image_free(px);

    ecs_os_free(file);
    ecs_fini(world);
}
