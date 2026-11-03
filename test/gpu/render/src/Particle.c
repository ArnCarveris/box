/**
 * @file Particle.c
 * @brief Tests for the particle passes: streams and effect bursts render where their particles
 * are, scenes and full pools are respected, a fixed seed renders the same image twice, additive
 * pools brighten, soft particles fade near geometry and sprites show the frame of their age.
 * An effect flash lights a wall while it plays, a deleted lingering emitter still draws and the
 * drift of the forces carries particles at rest across the image, a radial speed expands
 * a ring in the rotated plane of its emitter and by the scale of its effect, and the velocity
 * and force curves of a ParticleMotion move particles at rest by their age: 2 units in their
 * first second.
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

/** The unlit view draws one forward pass, which the soft pool splits: the particles draw last,
 * in one more pass that reads the depth. Without a soft pool there is no split. */
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

    box_render_gpu_stats_t before, after;
    test_assert(box_render_gpu_stats(world, &before));
    ecs_progress(world, 1.0f / 60.0f);
    box_render_sync(world);
    test_assert(box_render_gpu_stats(world, &after));
    test_int(after.forward_passes - before.forward_passes, 2);

    ecs_set(world, near_soft, ParticleAppearance, {.softness = 0});
    ecs_set(world, open_soft, ParticleAppearance, {.softness = 0});
    ecs_progress(world, 1.0f / 60.0f);
    ecs_progress(world, 1.0f / 60.0f);
    box_render_sync(world);
    test_assert(box_render_gpu_stats(world, &before));
    ecs_progress(world, 1.0f / 60.0f);
    box_render_sync(world);
    test_assert(box_render_gpu_stats(world, &after));
    test_int(after.forward_passes - before.forward_passes, 1);
    test_int(after.errors, before.errors);

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

/** Create a prefab template of one white particle of size 1 that stays where it spawns.
 *
 * @param world The world
 * @return The template
 */
static ecs_entity_t dot_template_new(
    ecs_world_t *world)
{
    test_assert(ecs_script_run(world, "dot",
        "using box.particle\n"
        "prefab Dot {\n"
        "  ParticleEmitter: {max: 1, direction: {0, 0, 0}, lifetime: {100, 100}, "
        "size: {1, 1}}\n"
        "  ParticleBurst: {count: 1}\n"
        "}\n", NULL) == 0);
    ecs_entity_t result = ecs_lookup(world, "Dot");
    test_assert(result != 0);

    return result;
}

void Particle_burst_renders_at_point(void) {
    // particle/effect
    ecs_world_t *world = world_new("burst.png");
    ecs_entity_t dot = dot_template_new(world);
    vec3 at = {2, 1, 0};
    test_assert(box_particle_burst_at(world, dot, at, 1) != 0);
    unsigned char *px = run(world, "burst.png", 3);

    test_assert(green_at(px, 2, 1) > 128);
    test_assert(green_at(px, 0, 0) < 8);
    test_assert(green_at(px, -2, 1) < 8);
    test_int(ecs_singleton_get(world, ParticleStats)->effects, 1);

    stbi_image_free(px);
    ecs_fini(world);
}

void Particle_two_plays_one_frame(void) {
    // particle/effect
    ecs_world_t *world = world_new("plays.png");
    ecs_entity_t dot = dot_template_new(world);
    vec3 left = {-2, -1, 0}, right = {2, 1, 0};
    ecs_entity_t a = box_particle_burst_at(world, dot, left, 1);
    ecs_entity_t b = box_particle_burst_at(world, dot, right, 1);
    test_assert(a != 0);
    test_assert(b != 0);
    test_assert(a != b);
    unsigned char *px = run(world, "plays.png", 3);

    test_assert(green_at(px, -2, -1) > 128);
    test_assert(green_at(px, 2, 1) > 128);
    test_assert(green_at(px, 0, 0) < 8);
    test_int(ecs_singleton_get(world, ParticleStats)->effects, 2);

    stbi_image_free(px);
    ecs_fini(world);
}

/** Light the views of a world, which the worlds of the tests draw unlit.
 *
 * @param world The world
 */
static void lighting_enable(
    ecs_world_t *world)
{
    ecs_entity_t views[4];
    int32_t i, count = 0;
    ecs_iter_t it = ecs_each(world, View);
    while (ecs_each_next(&it)) {
        for (i = 0; i < it.count && count < 4; i ++) {
            views[count ++] = it.entities[i];
        }
    }

    for (i = 0; i < count; i ++) {
        ecs_get_mut(world, views[i], View)->lighting.enabled = true;
        ecs_modified(world, views[i], View);
    }
}

void Particle_flash_lights_wall(void) {
    // particle/flash
    ecs_world_t *world = world_new("flash.png");
    lighting_enable(world);
    ecs_singleton_set(world, AmbientLight, {.intensity = 0});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -1});
    ecs_set(world, wall, Box, {20, 20, 0.1f});
    ecs_set(world, wall, Rgb, {255, 255, 255});
    ecs_set(world, wall, PbrMaterial, {.roughness = 1});
    test_assert(ecs_script_run(world, "flash",
        "using box.particle\n"
        "using box.light\n"
        "prefab Flash {\n"
        "  ParticleEmitter: {max: 1, direction: {0, 0, 0}, lifetime: {1, 1}, "
        "size: {1, 1}}\n"
        "  ParticleBurst: {count: 1}\n"
        "  light {\n"
        "    PointLight: {intensity: flecs.script.math.PI, range: 4}\n"
        "    EffectFlash: {intensity: 4 * flecs.script.math.PI, seconds: 0.05}\n"
        "  }\n"
        "}\n", NULL) == 0);
    ecs_entity_t flash = ecs_lookup(world, "Flash");
    vec3 at = {0, 0, 0};
    test_assert(box_particle_burst_at(world, flash, at, 1) != 0);

    unsigned char *px = run(world, "flash.png", 1);
    test_assert(green_at(px, 1.5f, 0) > 64);
    test_assert(green_at(px, -1.5f, 0) > 64);
    test_assert(green_at(px, 0, 1.5f) > 64);
    stbi_image_free(px);

    px = run(world, "flash.png", 4);
    test_int(ecs_singleton_get(world, ParticleStats)->effects, 1);
    test_assert(green_at(px, 1.5f, 0) < 8);
    test_assert(green_at(px, -1.5f, 0) < 8);
    test_assert(green_at(px, 0, 1.5f) < 8);
    test_assert(green_at(px, 0, 0) > 128);

    stbi_image_free(px);
    ecs_fini(world);
}

void Particle_lingering_renders_after_delete(void) {
    // particle/linger
    ecs_world_t *world = world_new("linger.png");
    ecs_entity_t lingering = single_new(world, -2, 1);
    ecs_entity_t plain = single_new(world, 2, 1);
    ecs_get_mut(world, lingering, ParticleEmitter)->linger = true;
    ecs_modified(world, lingering, ParticleEmitter);
    unsigned char *px = run(world, "linger.png", 3);
    test_assert(green_at(px, -2, 0) > 128);
    test_assert(green_at(px, 2, 0) > 128);
    stbi_image_free(px);

    ecs_delete(world, lingering);
    ecs_delete(world, plain);
    px = run(world, "linger.png", 1);
    test_assert(green_at(px, -2, 0) > 128);
    test_assert(green_at(px, 2, 0) < 8);
    test_int(ecs_singleton_get(world, ParticleStats)->lingering, 1);
    test_int(ecs_singleton_get(world, ParticleStats)->emitters, 0);

    stbi_image_free(px);
    ecs_fini(world);
}

void Particle_drift_moves_particles(void) {
    // particle/drift
    ecs_world_t *world = world_new("drift.png");
    ecs_entity_t e = single_new(world, -2, 1);
    ecs_set(world, e, ParticleForces, {.drift = {2, 0, 0}});
    unsigned char *px = run(world, "drift.png", 61);

    test_assert(green_at(px, 0, 0) > 128);
    test_assert(green_at(px, -2, 0) < 8);
    test_assert(green_at(px, 2, 0) < 8);
    test_int(ecs_singleton_get(world, ParticleStats)->particles, 1);
    stbi_image_free(px);

    ecs_set(world, e, ParticleForces, {.drift = {0, 2, 0}});
    px = run(world, "drift.png", 60);
    test_assert(green_at(px, 0, 2) > 128);
    test_assert(green_at(px, 0, 0) < 8);

    stbi_image_free(px);
    ecs_fini(world);
}

/** Create a template that bursts a ring of particles over a disc in its XZ plane, outward at a
 * radial speed of 2 units per second.
 *
 * @param world The world
 * @return The template
 */
static ecs_entity_t ring_template_new(
    ecs_world_t *world)
{
    test_assert(ecs_script_run(world, "ring",
        "using box.particle\n"
        "prefab Ring {\n"
        "  ParticleEmitter: {max: 400, shape: EmitterDisc, radius: 0.25, "
        "direction: {0, 0, 0}, speed: {0, 0}, radial: {2, 2}, lifetime: {100, 100}, "
        "size: {0.25, 0.25}, seed: 5}\n"
        "  ParticleBurst: {count: 400}\n"
        "}\n", NULL) == 0);
    ecs_entity_t result = ecs_lookup(world, "Ring");
    test_assert(result != 0);

    return result;
}

void Particle_radial_ring_in_plane(void) {
    // particle/radial
    ecs_world_t *world = world_new("radial.png");
    ecs_entity_t e = emitter_new(world, 0, 0, 400);
    ecs_set(world, e, Rotation, {1.5707964f, 0, 0});
    ecs_set(world, e, ParticleBurst, {.count = 400});
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->shape = EmitterDisc;
    p->radius = 0.25f;
    p->speed = (ParticleRange){0, 0};
    p->radial = (ParticleRange){4, 4};
    p->size = (ParticleRange){0.5f, 0.5f};
    ecs_modified(world, e, ParticleEmitter);
    unsigned char *px = run(world, "radial.png", 31);

    test_assert(green_at(px, 2.1f, 0) > 128);
    test_assert(green_at(px, -2.1f, 0) > 128);
    test_assert(green_at(px, 0, 2.1f) > 128);
    test_assert(green_at(px, 0, -2.1f) > 128);
    test_assert(green_at(px, 0, 0) < 8);
    test_assert(green_at(px, 1, 1) < 8);
    test_assert(green_at(px, 3.5f, 0) < 8);
    test_int(ecs_singleton_get(world, ParticleStats)->particles, 400);

    stbi_image_free(px);
    ecs_fini(world);
}

void Particle_radial_burst_scaled(void) {
    // particle/radial
    ecs_world_t *world = world_new("radial_burst.png");
    ecs_entity_t ring = ring_template_new(world);
    vec3 at = {0, 0, 0};
    test_assert(box_particle_burst_at(world, ring, at, 2) != 0);
    unsigned char *px = run(world, "radial_burst.png", 31);

    test_assert(green_at(px, 1.5f, 0) > 128);
    test_assert(green_at(px, -1.5f, 0) > 128);
    test_assert(green_at(px, 0, 1.5f) < 8);
    test_assert(green_at(px, 0, -1.5f) < 8);
    test_assert(green_at(px, 3.25f, 0) < 8);
    test_int(ecs_singleton_get(world, ParticleStats)->effects, 1);
    test_int(ecs_singleton_get(world, ParticleStats)->particles, 400);

    stbi_image_free(px);
    ecs_fini(world);
}

/** Create an emitter of one particle at rest with a lifetime of 2 seconds.
 *
 * @param world The world
 * @param x World x of the emitter
 * @param y World y of the emitter
 * @return The emitter
 */
static ecs_entity_t motion_single_new(
    ecs_world_t *world,
    float x,
    float y)
{
    ecs_entity_t e = single_new(world, x, 1);
    ecs_set(world, e, Position, {x, y, 0});
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->lifetime = (ParticleRange){2, 2};
    ecs_modified(world, e, ParticleEmitter);

    return e;
}

void Particle_motion_velocity_curve_moves(void) {
    // particle/motion
    ecs_world_t *world = world_new("motion_velocity.png");
    ecs_entity_t e = motion_single_new(world, -2, 0);
    ecs_set(world, e, ParticleMotion, {
        .velocity_x = { CurveLinear, BOX_CURVE_KEYS({0, 0}, {1, 8}) }
    });
    unsigned char *px = run(world, "motion_velocity.png", 61);

    test_assert(green_at(px, 0, 0) > 128);
    test_assert(green_at(px, -2, 0) < 8);
    test_assert(green_at(px, 2, 0) < 8);
    test_int(ecs_singleton_get(world, ParticleStats)->particles, 1);

    stbi_image_free(px);
    ecs_fini(world);
}

void Particle_motion_force_curve_moves(void) {
    // particle/motion
    ecs_world_t *world = world_new("motion_force.png");
    ecs_entity_t e = motion_single_new(world, 0, -1);
    ecs_set(world, e, ParticleMotion, {
        .force_y = { CurveLinear, BOX_CURVE_KEYS({0, 0}, {1, 24}) },
        .drag = { CurveLinear, BOX_CURVE_KEYS({0, 0}, {1, 0}) }
    });
    unsigned char *px = run(world, "motion_force.png", 61);

    test_assert(green_at(px, 0, 1) > 128);
    test_assert(green_at(px, 0, -1) < 8);
    test_assert(green_at(px, 0, 2.5f) < 8);

    stbi_image_free(px);
    ecs_fini(world);
}
