/**
 * @file Particle.c
 * @brief Benchmarks for a pool of one million live particles: the frame with the integrate and
 * spawn passes alone, with three active forces, and with the draw of the pool in a view; the
 * fill rate of a screen covering smoke; and a thousand plays a second of a layered effect.
 *
 * The GPU cases render on the main thread and read a word of the particle page back each frame.
 * The effect case extracts into a null backend and counts the allocations of a frame.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Particles the pool keeps alive */
#define BENCH_PARTICLES (1000000)

/** Frame delta, a fixed clock */
#define BENCH_PARTICLE_DELTA (1.0f / 60.0f)

/** Run frames of an emitter that keeps one million particles alive.
 *
 * @param b The benchmark
 * @param draw Whether a view draws the pool
 * @param forces Whether gravity, drag toward a wind and turbulence act on the particles
 */
static void bench_particles(
    bench_t *b,
    bool draw,
    bool forces)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    if (draw) {
        ecs_entity_t camera = ecs_new(world);
        ecs_set(world, camera, Position, {0, 5, 30});
        ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
        ecs_entity_t view = ecs_new(world);
        ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
            .color = {0, 0, 0, 255}}});
    }

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->max = BENCH_PARTICLES;
    p->rate = 1200000;
    p->shape = EmitterSphere;
    p->radius = 2;
    p->spread = 30;
    p->speed = (ParticleRange){2, 6};
    p->lifetime = (ParticleRange){1, 1.2f};
    p->size = (ParticleRange){0.02f, 0.05f};
    p->acceleration[1] = -2;
    ecs_modified(world, e, ParticleEmitter);
    if (forces) {
        ecs_set(world, e, ParticleForces, {
            .gravity = {0, -9.81f, 0}, .wind = {3, 0, 1}, .drag = 0.8f, .turbulence = 4,
            .turbulence_scale = 1.5f
        });
    }

    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 1280, .height = 720, .resolution_scale = 1});

    int32_t i;
    for (i = 0; i < 90; i ++) {
        ecs_progress(world, BENCH_PARTICLE_DELTA);
    }

    box_render_sync(world);
    uint32_t word = 0;
    double frames = 0;
    bench_set_items(b, BENCH_PARTICLES);
    while (bench_iter(b)) {
        ecs_progress(world, BENCH_PARTICLE_DELTA);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_PARTICLE_FIRST, 0,
            sizeof(word), &word);
        bench_keep(word);
        frames ++;
    }

    const ParticleStats *stats = ecs_singleton_get(world, ParticleStats);
    bench_counter(b, "frames", frames);
    bench_counter(b, "particles", (double)stats->particles);
    bench_counter(b, "spawned", (double)stats->spawned);
    bench_counter(b, "bytes", (double)stats->bytes);
    ecs_fini(world);
}

void Particle_simulate_1m(
    bench_t *b)
{
    // particle/bench
    bench_particles(b, false, false);
}

void Particle_simulate_1m_forces(
    bench_t *b)
{
    // particle/bench
    bench_particles(b, false, true);
}

void Particle_draw_1m(
    bench_t *b)
{
    // particle/bench
    bench_particles(b, true, false);
}

/** Particles of the screen filling smoke */
#define BENCH_SMOKE (4000)

void Particle_smoke_fill(
    bench_t *b)
{
    // particle/bench
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    app->unpaced = true;
    ecs_singleton_modified(world, App);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 6});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .clear = {.enabled = true,
        .color = {40, 50, 70, 255}}});
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, -2});
    ecs_set(world, wall, Box, {40, 40, 0.2f});

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->max = BENCH_SMOKE;
    p->rate = BENCH_SMOKE;
    p->shape = EmitterBox;
    p->extents[0] = 3;
    p->extents[1] = 2;
    p->extents[2] = 1;
    p->spread = 180;
    p->speed = (ParticleRange){0.1f, 0.3f};
    p->lifetime = (ParticleRange){0.9f, 1};
    p->size = (ParticleRange){3, 5};
    p->rotation = (ParticleRange){0, 360};
    p->opacity = (ParticleRange){0.05f, 0.15f};
    ecs_modified(world, e, ParticleEmitter);
    ecs_set(world, e, ParticleCurves, {
        .size = {0.5f, 1, 1.3f, 1.5f},
        .opacity = {0, 1, 1, 0},
        .rotation = {0, 0, 0, 0},
        .color = {{230, 230, 230}, {200, 200, 200}, {160, 160, 160}, {120, 120, 120}},
        .spin = {1, 1, 1, 1}
    });
    ecs_set(world, e, ParticleAppearance, {.softness = 0.5f, .lit = 0.5f});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 1280, .height = 720, .resolution_scale = 1});

    int32_t i;
    for (i = 0; i < 90; i ++) {
        ecs_progress(world, BENCH_PARTICLE_DELTA);
    }

    box_render_sync(world);
    uint32_t word = 0;
    double frames = 0;
    bench_set_items(b, BENCH_SMOKE);
    while (bench_iter(b)) {
        ecs_progress(world, BENCH_PARTICLE_DELTA);
        box_render_sync(world);
        box_render_gpu_read_buffer(world, BOX_RENDER_BUFFER_PARTICLE_FIRST, 0,
            sizeof(word), &word);
        bench_keep(word);
        frames ++;
    }

    bench_counter(b, "frames", frames);
    bench_counter(b, "particles", (double)ecs_singleton_get(world, ParticleStats)->particles);
    ecs_fini(world);
}

/** Plays per second of the effect benchmark */
#define BENCH_EFFECT_PLAYS (1000)

void Particle_effect_plays_1k(
    bench_t *b)
{
    // particle/effect
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    if (ecs_script_run(world, "hit",
        "using box.particle\n"
        "using box.transform\n"
        "prefab Hit {\n"
        "  flash {\n"
        "    ParticleEmitter: {max: 16, lifetime: {0.1, 0.2}, size: {0.5, 1}}\n"
        "    ParticleBurst: {count: 8}\n"
        "  }\n"
        "  smoke {\n"
        "    Position: {0, 0.5, 0}\n"
        "    ParticleEmitter: {max: 64, rate: 60, lifetime: {0.4, 0.6}, shape: EmitterSphere, "
        "radius: 0.5}\n"
        "    ParticleBurst: {count: 12, at: 0.1}\n"
        "  }\n"
        "  debris {\n"
        "    ParticleEmitter: {max: 32, spread: 60, speed: {2, 4}, lifetime: {0.3, 0.5}}\n"
        "    ParticleBurst: {count: 6, at: 0.05, repeat: 2}\n"
        "  }\n"
        "}\n", NULL))
    {
        goto done;
    }

    ecs_entity_t hit = ecs_lookup(world, "Hit");
    double owed = 0, plays = 0, frames = 0;
    int64_t mallocs = 0;
    uint32_t n = 0;
    int32_t i;
    for (i = 0; i < 240; i ++) {
        owed += BENCH_EFFECT_PLAYS * BENCH_PARTICLE_DELTA;
        for (; owed >= 1; owed -= 1, n ++) {
            vec3 at = { (float)(n % 32), 0, (float)((n / 32) % 32) };
            box_particle_burst_at(world, hit, at, 1.0f + (float)(n % 3) * 0.5f);
        }

        ecs_progress(world, BENCH_PARTICLE_DELTA);
        box_render_sync(world);
    }

    bench_set_items(b, (int64_t)(BENCH_EFFECT_PLAYS * BENCH_PARTICLE_DELTA + 0.5f));
    mallocs = ecs_os_api_malloc_count;
    while (bench_iter(b)) {
        owed += BENCH_EFFECT_PLAYS * BENCH_PARTICLE_DELTA;
        for (; owed >= 1; owed -= 1, n ++) {
            vec3 at = { (float)(n % 32), 0, (float)((n / 32) % 32) };
            bench_keep(box_particle_burst_at(world, hit, at, 1.0f + (float)(n % 3) * 0.5f));
            plays ++;
        }

        ecs_progress(world, BENCH_PARTICLE_DELTA);
        box_render_sync(world);
        frames ++;
    }

    mallocs = ecs_os_api_malloc_count - mallocs;
    const ParticleStats *stats = ecs_singleton_get(world, ParticleStats);
    bench_counter(b, "frames", frames);
    bench_counter(b, "plays", plays);
    bench_counter(b, "effects", (double)stats->effects);
    bench_counter(b, "particles", (double)stats->particles);
    bench_counter(b, "allocations_per_frame", frames > 0 ? (double)mallocs / frames : 0);
done:
    ecs_fini(world);
    box_render_null_free(null);
}
