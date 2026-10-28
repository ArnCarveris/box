/**
 * @file Flash.c
 * @brief Tests for EffectFlash: the light of an effect fades over its seconds on each play, its
 * defaults, a flash at the root of a prefab, the retire of an instance and dark lights.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

/** Frame delta of the tests, a fixed clock of a whole number of microseconds */
#define DELTA (0.1f)

/** Tolerance of the float comparisons */
#define EPS (1e-4f)

/** Templates of the tests: Boom bursts particles living 0.3 seconds and flashes a child light
 * from 6 over 0.6 seconds, Pop flashes its root light at its own intensity over the default
 * seconds and Glow keeps playing for 5 seconds after its flash of 0.1 seconds */
#define TEMPLATES \
    "using box.particle\n" \
    "using box.light\n" \
    "@tree ChildOf\n" \
    "prefab Boom {\n" \
    "  ParticleEmitter: {lifetime: {0.3, 0.3}}\n" \
    "  ParticleBurst: {count: 2}\n" \
    "  flash {\n" \
    "    PointLight: {intensity: 1, range: 5}\n" \
    "    EffectFlash: {intensity: 6, seconds: 0.6}\n" \
    "  }\n" \
    "}\n" \
    "@tree ChildOf\n" \
    "prefab Pop {\n" \
    "  ParticleEmitter: {lifetime: {0.1, 0.1}}\n" \
    "  ParticleBurst: {count: 1}\n" \
    "  PointLight: {intensity: 3, range: 5}\n" \
    "  EffectFlash: {}\n" \
    "}\n" \
    "@tree ChildOf\n" \
    "prefab Glow {\n" \
    "  ParticleEmitter: {lifetime: {5, 5}}\n" \
    "  ParticleBurst: {count: 1}\n" \
    "  PointLight: {intensity: 2, range: 5}\n" \
    "  EffectFlash: {seconds: 0.1}\n" \
    "}\n"

/** Check that two floats are within EPS of each other, relative to their size.
 *
 * @param a The value
 * @param b The expected value
 */
static void near(
    float a,
    float b)
{
    test_assert(fabsf(a - b) <= EPS * fmaxf(1.0f, fabsf(b)));
}

/** Create a world that renders into a null backend, with the templates of the tests.
 *
 * @param null Receives the backend, freed by the caller after the world
 * @return The world
 */
static ecs_world_t* world_new(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    test_assert(ecs_script_run(world, "templates", TEMPLATES, NULL) == 0);

    return world;
}

/** Play a template at a point.
 *
 * @param world The world
 * @param name Name of the template
 * @param x X of the point
 * @return The root of the instance
 */
static ecs_entity_t play(
    ecs_world_t *world,
    const char *name,
    float x)
{
    vec3 position = { x, 0, 0 };
    ecs_entity_t t = ecs_lookup(world, name);
    test_assert(t != 0);
    ecs_entity_t result = box_particle_burst_at(world, t, position, 1);
    test_assert(result != 0);

    return result;
}

/** Get the intensity of the PointLight of an entity.
 *
 * @param world The world
 * @param e The entity
 * @return The intensity
 */
static float intensity_of(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const PointLight *l = ecs_get(world, e, PointLight);
    test_assert(l != NULL);

    return l->intensity;
}

/** Look up the flash light of an instance of Boom.
 *
 * @param world The world
 * @param root The root of the instance
 * @return The light
 */
static ecs_entity_t flash_of(
    ecs_world_t *world,
    ecs_entity_t root)
{
    ecs_entity_t result = ecs_lookup_child(world, root, "flash");
    test_assert(result != 0);

    return result;
}

/** Run a frame and get the stats it wrote.
 *
 * @param world The world
 * @return The stats
 */
static ParticleStats frame(
    ecs_world_t *world)
{
    ecs_progress(world, DELTA);
    box_render_sync(world);
    const ParticleStats *stats = ecs_singleton_get(world, ParticleStats);
    test_assert(stats != NULL);

    return *stats;
}

/** Run frames.
 *
 * @param world The world
 * @param count Frames to run
 * @return The stats of the last one
 */
static ParticleStats frames(
    ecs_world_t *world,
    int32_t count)
{
    ParticleStats result = {0};
    int32_t i;
    for (i = 0; i < count; i ++) {
        result = frame(world);
    }

    return result;
}

/** Get the counters of the light extract of the last frame.
 *
 * @param world The world
 * @return The counters
 */
static box_render_lights_stats_t lights_of(
    ecs_world_t *world)
{
    box_render_lights_stats_t result;
    box_render_lights_stats(world, &result);

    return result;
}

void Flash_c_defaults(void) {
    // particle/flash
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, EffectFlash);
    const EffectFlash *f = ecs_get(world, e, EffectFlash);
    test_assert(f != NULL);
    test_flt(f->intensity, 0);
    test_flt(f->seconds, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Flash_fades_over_seconds(void) {
    // particle/flash
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t root = play(world, "Boom", 0);
    ecs_entity_t light = flash_of(world, root);
    near(intensity_of(world, light), 6);

    frame(world);
    test_int(lights_of(world).lights, 1);
    near(intensity_of(world, light), 5);
    near(ecs_get(world, root, ParticleEffect)->time, 0.1f);

    frame(world);
    near(intensity_of(world, light), 4);
    frames(world, 3);
    near(intensity_of(world, light), 1);
    test_int(lights_of(world).lights, 1);

    frame(world);
    test_flt(intensity_of(world, light), 0);
    frame(world);
    test_int(lights_of(world).lights, 0);
    test_flt(intensity_of(world, light), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Flash_default_seconds(void) {
    // particle/flash
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t root = play(world, "Pop", 0);
    near(intensity_of(world, root), 3);

    frame(world);
    near(intensity_of(world, root), 1.5f);
    frame(world);
    test_flt(intensity_of(world, root), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Flash_own_intensity(void) {
    // particle/flash
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t first = play(world, "Pop", 0);
    near(intensity_of(world, first), 3);
    frames(world, 3);
    test_flt(intensity_of(world, first), 0);

    ecs_entity_t second = play(world, "Pop", 1);
    test_uint(second, first);
    near(intensity_of(world, second), 3);
    frame(world);
    near(intensity_of(world, second), 1.5f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Flash_each_play(void) {
    // particle/flash
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t root = play(world, "Boom", 0);
    ecs_entity_t light = flash_of(world, root);
    ParticleStats stats = frames(world, 7);
    test_int(stats.effects, 0);
    test_bool(ecs_has_id(world, light, EcsDisabled), true);
    test_flt(intensity_of(world, light), 0);

    test_uint(play(world, "Boom", 2), root);
    near(intensity_of(world, light), 6);
    frame(world);
    test_int(lights_of(world).lights, 1);
    near(intensity_of(world, light), 5);
    frames(world, 2);
    near(intensity_of(world, light), 3);

    ecs_fini(world);
    box_render_null_free(null);
}

void Flash_root_of_prefab(void) {
    // particle/flash
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t pop = ecs_lookup(world, "Pop");
    ecs_entity_t root = play(world, "Pop", 0);
    test_bool(ecs_owns(world, root, PointLight), true);

    frame(world);
    near(intensity_of(world, root), 1.5f);
    near(intensity_of(world, pop), 3);
    frame(world);
    test_flt(intensity_of(world, root), 0);
    near(intensity_of(world, pop), 3);

    ecs_fini(world);
    box_render_null_free(null);
}

void Flash_holds_instance(void) {
    // particle/flash
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t root = play(world, "Boom", 0);
    near(ecs_get(world, root, ParticleEffect)->retire, 0.6f);

    ParticleStats stats = frames(world, 4);
    test_int(stats.particles, 0);
    test_int(stats.effects, 1);
    test_bool(ecs_has_id(world, root, EcsDisabled), false);

    stats = frame(world);
    test_int(stats.effects, 1);
    stats = frame(world);
    test_int(stats.effects, 0);
    test_bool(ecs_has_id(world, root, EcsDisabled), true);

    ecs_fini(world);
    box_render_null_free(null);
}

void Flash_dark_lights_cost_nothing(void) {
    // particle/flash
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .shadows.enabled = true});
    int32_t i;
    for (i = 0; i < 50; i ++) {
        play(world, "Glow", (float)i);
    }

    frame(world);
    test_int(lights_of(world).lights, 50);

    ParticleStats stats = frames(world, 3);
    test_int(stats.effects, 50);
    box_render_lights_stats_t lights = lights_of(world);
    test_int(lights.lights, 0);
    test_int(lights.upload_bytes, 0);
    test_int(ecs_vec_count(&null->last.views), 1);
    const box_render_view_t *v = ecs_vec_first_t(&null->last.views, box_render_view_t);
    test_uint(v->lights.count, 0);
    test_uint(v->lights.directional, 0);
    test_bool(v->shadows.enabled, false);

    frame(world);
    test_int(lights_of(world).upload_bytes, 0);

    ecs_fini(world);
    box_render_null_free(null);
}
