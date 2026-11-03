/**
 * @file Particle.c
 * @brief Benchmarks for a pool of one million live particles: the frame with the integrate and
 * spawn passes alone, with three active forces, and with the draw of the pool in a view; the
 * fill rate of a screen covering smoke; and a thousand plays a second of a layered effect.
 *
 * The cost of curves: one million particles drawn with constant curves, with keyed curves at
 * their values, which differ only by the reads of their rows, and with keyed and max_keys
 * curves and ParticleMotion; and the plays of an effect with keyed curves.
 *
 * The extract of thousands of emitters without, with constant and with keyed curves, also with
 * one curve changing per frame, into a null backend; the GPU cases render on the main thread.
 */

#include <box.h>
#include <bake_bench.h>
#include "render.h"

/** Particles the pool keeps alive */
#define BENCH_PARTICLES (1000000)

/** Frame delta, a fixed clock */
#define BENCH_PARTICLE_DELTA (1.0f / 60.0f)

/** The curves of the emitters of a benchmark */
typedef enum bench_curves_t {
    BENCH_CURVES_NONE,      /**< No ParticleCurves */
    BENCH_CURVES_CONSTANT,  /**< ParticleCurves of one key each, which read no row */
    BENCH_CURVES_FLAT,      /**< The constant curves with two keys at almost their values
                             *   but for the color, so that size, opacity, rotation and spin
                             *   read their rows: the cost of the reads alone */
    BENCH_CURVES_KEYED,     /**< Keyed size, opacity, rotation, spin and color curves */
    BENCH_CURVES_KEYED_MAX  /**< The keyed curves, each with max_keys */
} bench_curves_t;

/** Give an emitter curves.
 *
 * @param world The world
 * @param e The emitter
 * @param curves The curves
 * @param end The end of the size curve, to make keyed curves of emitters differ
 */
static void bench_curves_set(
    ecs_world_t *world,
    ecs_entity_t e,
    bench_curves_t curves,
    float end)
{
    if (curves == BENCH_CURVES_CONSTANT) {
        ecs_set(world, e, ParticleCurves, {
            .size = { .keys = BOX_CURVE_KEYS({0, 0.8f}) },
            .opacity = { .keys = BOX_CURVE_KEYS({0, 0.9f}) },
            .rotation = { .keys = BOX_CURVE_KEYS({0, 0.1f}) },
            .spin = { .keys = BOX_CURVE_KEYS({0, 0.5f}) },
            .color = { .keys = BOX_VEC_VIEW(ParticleGradientKey, {0, {200, 180, 160}}) }
        });
    } else if (curves == BENCH_CURVES_FLAT) {
        ecs_set(world, e, ParticleCurves, {
            .size = { .keys = BOX_CURVE_KEYS({0, 0.8f}, {1, 0.801f}) },
            .opacity = { .keys = BOX_CURVE_KEYS({0, 0.9f}, {1, 0.901f}) },
            .rotation = { .keys = BOX_CURVE_KEYS({0, 0.1f}, {1, 0.101f}) },
            .spin = { .keys = BOX_CURVE_KEYS({0, 0.5f}, {1, 0.501f}) },
            .color = { .keys = BOX_VEC_VIEW(ParticleGradientKey, {0, {200, 180, 160}}) }
        });
    } else if (curves == BENCH_CURVES_KEYED) {
        ecs_set(world, e, ParticleCurves, {
            .size = { .kind = CurveCatmullRom,
                .keys = BOX_CURVE_KEYS({0, 0.3f}, {0.4f, 1.8f}, {1, end}) },
            .opacity = { .keys = BOX_CURVE_KEYS({0, 0}, {0.1f, 1}, {1, 0}) },
            .rotation = { .keys = BOX_CURVE_KEYS({0, 0}, {1, 0.25f}) },
            .spin = { .kind = CurveBezier, .keys = BOX_CURVE_KEYS(
                {.time = 0, .value = 1, .out = -2}, {.time = 1, .value = 0}) },
            .color = { .keys = BOX_VEC_VIEW(ParticleGradientKey,
                {0, {255, 230, 160}}, {0.5f, {255, 120, 40}}, {1, {60, 60, 60}}) }
        });
    } else if (curves == BENCH_CURVES_KEYED_MAX) {
        ecs_set(world, e, ParticleCurves, {
            .size = { .kind = CurveCatmullRom,
                .keys = BOX_CURVE_KEYS({0, 0.3f}, {0.4f, 1.8f}, {1, end}),
                .max_keys = BOX_CURVE_KEYS({0, 0.5f}, {0.5f, 2.2f}, {1, end + 0.5f}) },
            .opacity = { .keys = BOX_CURVE_KEYS({0, 0}, {0.1f, 1}, {1, 0}),
                .max_keys = BOX_CURVE_KEYS({0, 0}, {0.3f, 0.8f}, {1, 0}) },
            .rotation = { .keys = BOX_CURVE_KEYS({0, 0}, {1, 0.25f}),
                .max_keys = BOX_CURVE_KEYS({0, 0}, {1, -0.25f}) },
            .spin = { .kind = CurveBezier, .keys = BOX_CURVE_KEYS(
                {.time = 0, .value = 1, .out = -2}, {.time = 1, .value = 0}),
                .max_keys = BOX_CURVE_KEYS(
                {.time = 0, .value = -1, .out = 2}, {.time = 1, .value = 0}) },
            .color = { .keys = BOX_VEC_VIEW(ParticleGradientKey,
                {0, {255, 230, 160}}, {0.5f, {255, 120, 40}}, {1, {60, 60, 60}}),
                .max_keys = BOX_VEC_VIEW(ParticleGradientKey,
                {0, {160, 230, 255}}, {0.5f, {40, 120, 255}}, {1, {60, 60, 60}}) }
        });
    }
}

/** Give an emitter keyed ParticleMotion curves: a velocity that rises, a force that turns and
 * a drag that comes on late.
 *
 * @param world The world
 * @param e The emitter
 * @param max Whether the curves have max_keys
 */
static void bench_motion_set(
    ecs_world_t *world,
    ecs_entity_t e,
    bool max)
{
    if (max) {
        ecs_set(world, e, ParticleMotion, {
            .velocity_y = { .keys = BOX_CURVE_KEYS({0, 0}, {0.6f, 0}, {1, 3}),
                .max_keys = BOX_CURVE_KEYS({0, 0}, {0.4f, 0}, {1, 5}) },
            .force_x = { .kind = CurveCatmullRom,
                .keys = BOX_CURVE_KEYS({0, -2}, {0.5f, 2}, {1, -2}),
                .max_keys = BOX_CURVE_KEYS({0, 2}, {0.5f, -2}, {1, 2}) },
            .drag = { .keys = BOX_CURVE_KEYS({0, 0}, {0.5f, 0}, {0.7f, 1}),
                .max_keys = BOX_CURVE_KEYS({0, 0}, {0.3f, 0}, {0.5f, 1}) }
        });
    } else {
        ecs_set(world, e, ParticleMotion, {
            .velocity_y = { .keys = BOX_CURVE_KEYS({0, 0}, {0.6f, 0}, {1, 3}) },
            .force_x = { .kind = CurveCatmullRom,
                .keys = BOX_CURVE_KEYS({0, -2}, {0.5f, 2}, {1, -2}) },
            .drag = { .keys = BOX_CURVE_KEYS({0, 0}, {0.5f, 0}, {0.7f, 1}) }
        });
    }
}

/** Run frames of an emitter that keeps one million particles alive.
 *
 * @param b The benchmark
 * @param draw Whether a view draws the pool
 * @param forces Whether gravity, drag toward a wind and turbulence act on the particles
 * @param curves The curves of the emitter
 * @param motion Whether it has keyed ParticleMotion curves, with max_keys for
 *        BENCH_CURVES_KEYED_MAX
 */
static void bench_particles(
    bench_t *b,
    bool draw,
    bool forces,
    bench_curves_t curves,
    bool motion)
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

    bench_curves_set(world, e, curves, 2.2f);
    if (motion) {
        bench_motion_set(world, e, curves == BENCH_CURVES_KEYED_MAX);
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
    bench_particles(b, false, false, BENCH_CURVES_NONE, false);
}

void Particle_simulate_1m_forces(
    bench_t *b)
{
    // particle/bench
    bench_particles(b, false, true, BENCH_CURVES_NONE, false);
}

void Particle_draw_1m(
    bench_t *b)
{
    // particle/bench
    bench_particles(b, true, false, BENCH_CURVES_NONE, false);
}

void Particle_simulate_1m_motion(
    bench_t *b)
{
    // particle/curve_cost
    bench_particles(b, false, false, BENCH_CURVES_NONE, true);
}

void Particle_draw_1m_constant_curves(
    bench_t *b)
{
    // particle/curve_cost
    bench_particles(b, true, false, BENCH_CURVES_CONSTANT, false);
}

void Particle_draw_1m_flat_curves(
    bench_t *b)
{
    // particle/curve_cost
    bench_particles(b, true, false, BENCH_CURVES_FLAT, false);
}

void Particle_draw_1m_curves(
    bench_t *b)
{
    // particle/curve_cost
    bench_particles(b, true, false, BENCH_CURVES_KEYED, false);
}

void Particle_draw_1m_curves_max(
    bench_t *b)
{
    // particle/curve_cost
    bench_particles(b, true, false, BENCH_CURVES_KEYED_MAX, false);
}

void Particle_draw_1m_curves_motion(
    bench_t *b)
{
    // particle/curve_cost
    bench_particles(b, true, false, BENCH_CURVES_KEYED, true);
}

void Particle_draw_1m_curves_max_motion(
    bench_t *b)
{
    // particle/curve_cost
    bench_particles(b, true, false, BENCH_CURVES_KEYED_MAX, true);
}

/** Emitters of the extract benchmarks */
#define BENCH_EMITTERS (4096)

/** Run frames of thousands of small emitters into a null backend, measuring the extract.
 *
 * @param b The benchmark
 * @param curves The curves of the emitters; keyed size curves differ per emitter
 * @param change Whether one curve changes in each frame, a different emitter each time, to a
 *        value it never had
 */
static void bench_extract(
    bench_t *b,
    bench_curves_t curves,
    bool change)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t *emitters = ecs_os_malloc_n(ecs_entity_t, BENCH_EMITTERS);
    int32_t i;
    for (i = 0; i < BENCH_EMITTERS; i ++) {
        ecs_entity_t e = emitters[i] = ecs_new(world);
        ecs_set(world, e, Position, {(float)(i % 64), 0, (float)(i / 64)});
        ecs_add(world, e, ParticleEmitter);
        ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
        p->max = 8;
        p->rate = 4;
        p->lifetime = (ParticleRange){1, 1.5f};
        ecs_modified(world, e, ParticleEmitter);
        bench_curves_set(world, e, curves, 2.0f + (float)i * 1e-3f);
    }

    for (i = 0; i < 60; i ++) {
        ecs_progress(world, BENCH_PARTICLE_DELTA);
        box_render_sync(world);
    }

    box_render_particle_curve_stats_t before, after;
    box_render_particles_curve_stats(box_render_particles_get(world), &before);
    int64_t mallocs = ecs_os_api_malloc_count;
    double frames = 0;
    uint32_t n = 0;
    bench_set_items(b, BENCH_EMITTERS);
    while (bench_iter(b)) {
        if (change) {
            ecs_entity_t e = emitters[n % BENCH_EMITTERS];
            ParticleCurves *c = ecs_get_mut(world, e, ParticleCurves);
            ecs_vec_get_t(&c->size.keys, CurveKey, 1)->value = 1.8f + (float)(n + 1) * 1e-4f;
            ecs_modified(world, e, ParticleCurves);
            n ++;
        }

        ecs_progress(world, BENCH_PARTICLE_DELTA);
        box_render_sync(world);
        frames ++;
    }

    mallocs = ecs_os_api_malloc_count - mallocs;
    box_render_particles_curve_stats(box_render_particles_get(world), &after);
    const ParticleStats *stats = ecs_singleton_get(world, ParticleStats);
    bench_counter(b, "frames", frames);
    bench_counter(b, "emitters", (double)stats->emitters);
    bench_counter(b, "particles", (double)stats->particles);
    bench_counter(b, "rows", (double)after.rows);
    bench_counter(b, "entries", (double)after.entries);
    bench_counter(b, "bakes_per_frame",
        frames > 0 ? (double)(after.bakes - before.bakes) / frames : 0);
    bench_counter(b, "samplings_per_frame",
        frames > 0 ? (double)(after.samplings - before.samplings) / frames : 0);
    bench_counter(b, "allocations_per_frame", frames > 0 ? (double)mallocs / frames : 0);
    ecs_os_free(emitters);
    ecs_fini(world);
    box_render_null_free(null);
}

void Particle_extract_4k_no_curves(
    bench_t *b)
{
    // particle/curve_cost
    bench_extract(b, BENCH_CURVES_NONE, false);
}

void Particle_extract_4k_constant_curves(
    bench_t *b)
{
    // particle/curve_cost
    bench_extract(b, BENCH_CURVES_CONSTANT, false);
}

void Particle_extract_4k_curves(
    bench_t *b)
{
    // particle/curve_cost
    bench_extract(b, BENCH_CURVES_KEYED, false);
}

void Particle_extract_4k_curves_max(
    bench_t *b)
{
    // particle/curve_cost
    bench_extract(b, BENCH_CURVES_KEYED_MAX, false);
}

void Particle_extract_4k_curves_change_one(
    bench_t *b)
{
    // particle/curve_cost
    bench_extract(b, BENCH_CURVES_KEYED, true);
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
        .size = { .keys = BOX_CURVE_KEYS(
            {0, 0.5f}, {1.0f / 3.0f, 1}, {2.0f / 3.0f, 1.3f}, {1, 1.5f}) },
        .opacity = { .keys = BOX_CURVE_KEYS({0, 0}, {1.0f / 3.0f, 1}, {2.0f / 3.0f, 1}, {1, 0}) },
        .color = { .keys = BOX_VEC_VIEW(ParticleGradientKey,
            {0, {230, 230, 230}}, {1.0f / 3.0f, {200, 200, 200}},
            {2.0f / 3.0f, {160, 160, 160}}, {1, {120, 120, 120}}) }
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

/** The layered effect of the effect benchmarks */
static const char *bench_effect_hit =
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
    "}\n";

/** The layered effect with keyed curves on every layer and ParticleMotion on its debris */
static const char *bench_effect_hit_curves =
    "using box.curve\n"
    "using box.particle\n"
    "using box.transform\n"
    "prefab Hit {\n"
    "  flash {\n"
    "    ParticleEmitter: {max: 16, lifetime: {0.1, 0.2}, size: {0.5, 1}}\n"
    "    ParticleBurst: {count: 8}\n"
    "    ParticleCurves: {\n"
    "      size: {kind: CurveCatmullRom, keys: [{0, 0.3}, {0.4, 1.8}, {1, 2.2}]}\n"
    "      opacity: {keys: [{0, 1}, {1, 0}]}\n"
    "    }\n"
    "  }\n"
    "  smoke {\n"
    "    Position: {0, 0.5, 0}\n"
    "    ParticleEmitter: {max: 64, rate: 60, lifetime: {0.4, 0.6}, shape: EmitterSphere, "
    "radius: 0.5}\n"
    "    ParticleBurst: {count: 12, at: 0.1}\n"
    "    ParticleCurves: {\n"
    "      opacity: {keys: [{0, 0}, {0.1, 1}, {1, 0}], max_keys: [{0, 0}, {0.3, 0.8}, {1, 0}]}\n"
    "      size: {kind: CurveCatmullRom, keys: [{0, 0.3}, {0.4, 1.8}, {1, 2.2}]}\n"
    "      spin: {kind: CurveBezier, keys: [{time: 0, value: 1, out: -2}, "
    "{time: 1, value: 0, in: 0}]}\n"
    "      color: {keys: [{0, {200, 200, 200}}, {1, {90, 90, 90}}]}\n"
    "    }\n"
    "  }\n"
    "  debris {\n"
    "    ParticleEmitter: {max: 32, spread: 60, speed: {2, 4}, lifetime: {0.3, 0.5}}\n"
    "    ParticleBurst: {count: 6, at: 0.05, repeat: 2}\n"
    "    ParticleMotion: {\n"
    "      drag: {keys: [{0, 0}, {0.5, 0}, {0.7, 1}]}\n"
    "      velocity_y: {keys: [{0, 0}, {0.6, 0}, {1, 3}]}\n"
    "    }\n"
    "  }\n"
    "}\n";

/** Play the layered effect a thousand times a second into a null backend.
 *
 * @param b The benchmark
 * @param script The script of the effect, a prefab Hit
 */
static void bench_effect_plays(
    bench_t *b,
    const char *script)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    box_render_particle_curve_stats_t before, after;
    if (ecs_script_run(world, "hit", script, NULL)) {
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
    box_render_particles_curve_stats(box_render_particles_get(world), &before);
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
    box_render_particles_curve_stats(box_render_particles_get(world), &after);
    const ParticleStats *stats = ecs_singleton_get(world, ParticleStats);
    bench_counter(b, "frames", frames);
    bench_counter(b, "plays", plays);
    bench_counter(b, "effects", (double)stats->effects);
    bench_counter(b, "particles", (double)stats->particles);
    bench_counter(b, "allocations_per_frame", frames > 0 ? (double)mallocs / frames : 0);
    bench_counter(b, "entries", (double)after.entries);
    bench_counter(b, "bakes", (double)(after.bakes - before.bakes));
    bench_counter(b, "samplings", (double)(after.samplings - before.samplings));
done:
    ecs_fini(world);
    box_render_null_free(null);
}

void Particle_effect_plays_1k(
    bench_t *b)
{
    // particle/effect
    bench_effect_plays(b, bench_effect_hit);
}

void Particle_effect_plays_1k_curves(
    bench_t *b)
{
    // particle/curve_cost
    bench_effect_plays(b, bench_effect_hit_curves);
}
