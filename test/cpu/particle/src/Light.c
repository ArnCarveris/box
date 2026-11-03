/**
 * @file Light.c
 * @brief Tests for ParticleLight: the light follows play, pause and stop with its fade, the curve
 * once and looped, spot lights, the frame the lights see it and the lights of effect instances.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

/** Frame delta of the tests, a fixed clock of a whole number of microseconds */
#define DELTA (0.1f)

/** Tolerance of the float comparisons */
#define EPS (1e-4f)

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

/** Create a world that renders into a null backend.
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

    return world;
}

/** Create an emitter with a PointLight at intensity 1 and a ParticleLight.
 *
 * @param world The world
 * @param light The ParticleLight
 * @return The emitter
 */
static ecs_entity_t emitter_new(
    ecs_world_t *world,
    ParticleLight light)
{
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    ParticleEmitter *p = ecs_get_mut(world, e, ParticleEmitter);
    p->rate = 20;
    p->lifetime = (ParticleRange){1, 1};
    ecs_modified(world, e, ParticleEmitter);
    ecs_set(world, e, PointLight, { .intensity = 1, .range = 10 });
    ecs_set_ptr(world, e, ParticleLight, &light);

    return e;
}

/** Get a ParticleLight with a linear curve from 0 to 3 over a second, so the light is three
 * times the playback clock until it holds 3.
 *
 * @return The ParticleLight
 */
static ParticleLight ramp(void)
{
    static CurveKey keys[2] = { {0, 0}, {1, 3} };

    return (ParticleLight){
        .intensity = 1,
        .curve = { .keys = { .array = keys, .count = 2, .size = 2 } },
        .period = 1
    };
}

/** Set the state of the playback of an emitter, keeping its other members.
 *
 * @param world The world
 * @param e The emitter
 * @param state The state
 */
static void state_set(
    ecs_world_t *world,
    ecs_entity_t e,
    PlaybackState state)
{
    ParticlePlayback *p = ecs_ensure(world, e, ParticlePlayback);
    p->state = state;
    ecs_modified(world, e, ParticlePlayback);
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

/** Run a frame.
 *
 * @param world The world
 */
static void frame(
    ecs_world_t *world)
{
    ecs_progress(world, DELTA);
    box_render_sync(world);
}

/** Run frames.
 *
 * @param world The world
 * @param count Frames to run
 */
static void frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        frame(world);
    }
}

/** Get the lights the light extract collected in the last frame.
 *
 * @param world The world
 * @return The lights that light something
 */
static int32_t lights_lit(
    ecs_world_t *world)
{
    box_render_lights_stats_t stats;
    box_render_lights_stats(world, &stats);

    return stats.lights;
}

void Light_c_defaults(void) {
    // particle/light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleLight);
    const ParticleLight *l = ecs_get(world, e, ParticleLight);
    test_assert(l != NULL);
    test_flt(l->intensity, 1);
    test_int(l->curve.kind, CurveLinear);
    test_int(ecs_vec_count(&l->curve.keys), 0);
    test_flt(l->period, 0);
    test_bool(l->loop, false);
    test_flt(l->fade, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Light_script_values(void) {
    // particle/light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    test_assert(ecs_script_run(world, "light",
        "using box.curve\n"
        "using box.particle\n"
        "e {\n"
        "  ParticleLight: {intensity: 5, curve: {kind: CurveCatmullRom, keys: [{0, 0}, "
        "{0.33333334, 1}, {0.6666667, 0.5}, {1, 0}]}, period: 2, loop: true, fade: 0.5}\n"
        "}\n", NULL) == 0);
    ecs_entity_t e = ecs_lookup(world, "e");
    const ParticleLight *l = ecs_get(world, e, ParticleLight);
    test_assert(l != NULL);
    test_flt(l->intensity, 5);
    test_int(l->curve.kind, CurveCatmullRom);
    test_int(ecs_vec_count(&l->curve.keys), 4);
    const CurveKey *keys = ecs_vec_first_t(&l->curve.keys, CurveKey);
    test_flt(keys[0].value, 0);
    test_assert(keys[1].time == 1.0f / 3.0f);
    test_flt(keys[1].value, 1);
    test_assert(keys[2].time == 2.0f / 3.0f);
    test_flt(keys[2].value, 0.5);
    test_flt(keys[3].time, 1);
    test_flt(keys[3].value, 0);
    test_flt(l->period, 2);
    test_bool(l->loop, true);
    test_flt(l->fade, 0.5);

    ecs_fini(world);
    box_render_null_free(null);
}

void Light_follows_play(void) {
    // particle/light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, (ParticleLight){ .intensity = 4 });

    frame(world);
    near(intensity_of(world, e), 4);
    frame(world);
    near(intensity_of(world, e), 4);

    ecs_get_mut(world, e, ParticleLight)->intensity = 2;
    ecs_modified(world, e, ParticleLight);
    frame(world);
    near(intensity_of(world, e), 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void Light_holds_while_paused(void) {
    // particle/light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, ramp());

    frames(world, 2);
    near(intensity_of(world, e), 0.6f);

    state_set(world, e, ParticlePaused);
    frames(world, 3);
    near(intensity_of(world, e), 0.6f);

    state_set(world, e, ParticlePlaying);
    frame(world);
    near(intensity_of(world, e), 0.9f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Light_stop_goes_dark_at_once(void) {
    // particle/light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, (ParticleLight){ .intensity = 4 });

    frame(world);
    near(intensity_of(world, e), 4);

    state_set(world, e, ParticleStopped);
    frame(world);
    test_flt(intensity_of(world, e), 0);
    frames(world, 3);
    test_flt(intensity_of(world, e), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Light_stop_fades_over_seconds(void) {
    // particle/light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, (ParticleLight){ .intensity = 4, .fade = 0.4f });

    frame(world);
    near(intensity_of(world, e), 4);

    state_set(world, e, ParticleStopped);
    frame(world);
    near(intensity_of(world, e), 3);
    frame(world);
    near(intensity_of(world, e), 2);
    frame(world);
    near(intensity_of(world, e), 1);
    frame(world);
    test_flt(intensity_of(world, e), 0);
    frames(world, 2);
    test_flt(intensity_of(world, e), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Light_paused_fade_holds(void) {
    // particle/light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, (ParticleLight){ .intensity = 4, .fade = 0.4f });

    frame(world);
    state_set(world, e, ParticleStopped);
    frames(world, 2);
    near(intensity_of(world, e), 2);

    state_set(world, e, ParticlePaused);
    frames(world, 3);
    near(intensity_of(world, e), 2);

    state_set(world, e, ParticleStopped);
    frame(world);
    near(intensity_of(world, e), 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Light_play_after_stop_relights(void) {
    // particle/light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, ramp());

    frames(world, 3);
    near(intensity_of(world, e), 0.9f);

    state_set(world, e, ParticleStopped);
    frame(world);
    test_flt(intensity_of(world, e), 0);

    state_set(world, e, ParticlePlaying);
    frame(world);
    near(ecs_get(world, e, ParticlePlayback)->time, 0.1f);
    near(intensity_of(world, e), 0.3f);

    ecs_fini(world);
    box_render_null_free(null);
}

void Light_curve_once(void) {
    // particle/light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, (ParticleLight){
        .intensity = 2,
        .curve = { .keys = BOX_CURVE_KEYS({0, 1}, {1.0f / 3.0f, 2}, {2.0f / 3.0f, 4}, {1, 8}) },
        .period = 0.3f
    });

    frame(world);
    near(intensity_of(world, e), 4);
    frame(world);
    near(intensity_of(world, e), 8);
    frame(world);
    near(intensity_of(world, e), 16);
    frames(world, 5);
    near(intensity_of(world, e), 16);

    ecs_fini(world);
    box_render_null_free(null);
}

void Light_curve_loop(void) {
    // particle/light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, (ParticleLight){
        .intensity = 2,
        .curve = { .keys = BOX_CURVE_KEYS({0, 1}, {1.0f / 3.0f, 2}, {2.0f / 3.0f, 4}, {1, 8}) },
        .period = 0.3f,
        .loop = true
    });

    frame(world);
    near(intensity_of(world, e), 4);
    frame(world);
    near(intensity_of(world, e), 8);
    frame(world);
    near(intensity_of(world, e), 2);
    frame(world);
    near(intensity_of(world, e), 4);
    frames(world, 3);
    near(intensity_of(world, e), 4);

    ecs_fini(world);
    box_render_null_free(null);
}

void Light_spot_light(void) {
    // particle/light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, ParticleEmitter);
    ecs_set(world, e, SpotLight, { .intensity = 1, .range = 10, .outer_angle = 30 });
    ecs_set(world, e, ParticleLight, { .intensity = 6, .fade = 0.2f });

    frame(world);
    near(ecs_get(world, e, SpotLight)->intensity, 6);

    state_set(world, e, ParticleStopped);
    frame(world);
    near(ecs_get(world, e, SpotLight)->intensity, 3);
    frame(world);
    test_flt(ecs_get(world, e, SpotLight)->intensity, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Light_lights_same_frame(void) {
    // particle/light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, (ParticleLight){ .intensity = 4 });

    frame(world);
    test_int(lights_lit(world), 1);

    state_set(world, e, ParticleStopped);
    frame(world);
    test_int(lights_lit(world), 0);

    state_set(world, e, ParticlePlaying);
    frame(world);
    test_int(lights_lit(world), 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Light_prefab_instance_own_light(void) {
    // particle/light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t base = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, base, PointLight, { .intensity = 1, .range = 10 });
    ecs_set(world, base, ParticleLight, { .intensity = 3 });
    ecs_add(world, base, ParticleEmitter);
    ecs_entity_t e = ecs_new_w_pair(world, EcsIsA, base);
    test_bool(ecs_owns(world, e, PointLight), true);

    frame(world);
    near(intensity_of(world, e), 3);
    near(intensity_of(world, base), 1);

    state_set(world, e, ParticleStopped);
    frame(world);
    test_flt(intensity_of(world, e), 0);
    near(intensity_of(world, base), 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void Light_effect_layers_own_lights(void) {
    // particle/light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    test_assert(ecs_script_run(world, "templates",
        "using box.particle\n"
        "using box.light\n"
        "@tree ChildOf\n"
        "prefab Lamp {\n"
        "  ParticleEmitter: {lifetime: {0.3, 0.3}}\n"
        "  ParticleBurst: {count: 2}\n"
        "  PointLight: {intensity: 1, range: 5}\n"
        "  ParticleLight: {intensity: 2, fade: 0.2}\n"
        "  glow {\n"
        "    ParticleEmitter: {lifetime: {0.5, 0.5}}\n"
        "    ParticleBurst: {count: 2}\n"
        "    PointLight: {intensity: 1, range: 5}\n"
        "    ParticleLight: {intensity: 5}\n"
        "  }\n"
        "}\n", NULL) == 0);
    ecs_entity_t lamp = ecs_lookup(world, "Lamp");
    vec3 at = {0, 0, 0};
    ecs_entity_t root = box_particle_burst_at(world, lamp, at, 1);
    test_assert(root != 0);
    ecs_entity_t glow = ecs_lookup_child(world, root, "glow");
    test_assert(glow != 0);
    test_bool(ecs_owns(world, root, PointLight), true);
    test_bool(ecs_owns(world, glow, PointLight), true);

    frame(world);
    near(intensity_of(world, root), 2);
    near(intensity_of(world, glow), 5);
    test_int(lights_lit(world), 2);
    near(intensity_of(world, lamp), 1);
    near(intensity_of(world, ecs_lookup_child(world, lamp, "glow")), 1);

    frame(world);
    near(intensity_of(world, root), 1);
    test_flt(intensity_of(world, glow), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void Light_goes_out_with_entity(void) {
    // particle/light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, (ParticleLight){ .intensity = 4 });
    ecs_entity_t other = emitter_new(world, (ParticleLight){ .intensity = 2 });

    frame(world);
    test_int(lights_lit(world), 2);

    ecs_delete(world, e);
    frame(world);
    test_int(lights_lit(world), 1);
    near(intensity_of(world, other), 2);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Get a ParticleLight whose curve of a kind rises from 0 to 1 at half its period of 0.4
 * seconds and falls back to 0, its Bezier key at 0 leaving at a slope of 4.
 *
 * @param kind The kind
 * @param loop Whether it repeats
 * @param keys Receives the keys the light views, three
 * @return The ParticleLight
 */
static ParticleLight hump(
    CurveKind kind,
    bool loop,
    CurveKey *keys)
{
    keys[0] = (CurveKey){ .time = 0, .value = 0, .out = 4 };
    keys[1] = (CurveKey){ .time = 0.5f, .value = 1 };
    keys[2] = (CurveKey){ .time = 1, .value = 0 };

    return (ParticleLight){
        .intensity = 2,
        .curve = { .kind = kind, .keys = { .array = keys, .count = 3, .size = 3 } },
        .period = 0.4f,
        .loop = loop
    };
}

/** Value of the hump curve of a kind at a quarter of its period: linear 1/2, Catmull-Rom 9/16
 * with the slope 1 of the mirrored first key, and Bezier 3/4 with its slope of 4.
 *
 * @param kind The kind
 * @return The value
 */
static float hump_quarter(
    CurveKind kind)
{
    float result = 0.5f;
    if (kind == CurveCatmullRom) {
        result = 0.5625f;
    } else if (kind == CurveBezier) {
        result = 0.75f;
    }

    return result;
}

/** Value of the hump curve of a kind at three quarters of its period: linear and Bezier 1/2,
 * Catmull-Rom 9/16.
 *
 * @param kind The kind
 * @return The value
 */
static float hump_three_quarters(
    CurveKind kind)
{
    return kind == CurveCatmullRom ? 0.5625f : 0.5f;
}

void Light_curve_kinds_once(void) {
    // particle/light
    CurveKind kinds[3] = { CurveLinear, CurveCatmullRom, CurveBezier };
    int32_t k;
    for (k = 0; k < 3; k ++) {
        box_render_null_t *null;
        ecs_world_t *world = world_new(&null);
        CurveKey keys[3];
        ecs_entity_t e = emitter_new(world, hump(kinds[k], false, keys));

        frame(world);
        near(intensity_of(world, e), 2 * hump_quarter(kinds[k]));
        frame(world);
        near(intensity_of(world, e), 2);
        frame(world);
        near(intensity_of(world, e), 2 * hump_three_quarters(kinds[k]));
        frame(world);
        near(intensity_of(world, e), 0);
        frames(world, 3);
        near(intensity_of(world, e), 0);

        ecs_fini(world);
        box_render_null_free(null);
    }
}

void Light_curve_kinds_loop(void) {
    // particle/light
    CurveKind kinds[3] = { CurveLinear, CurveCatmullRom, CurveBezier };
    int32_t k;
    for (k = 0; k < 3; k ++) {
        box_render_null_t *null;
        ecs_world_t *world = world_new(&null);
        CurveKey keys[3];
        ecs_entity_t e = emitter_new(world, hump(kinds[k], true, keys));
        /* Looping, Catmull-Rom takes the slope 0 of the peak across the wrap at its ends */
        float quarter = kinds[k] == CurveCatmullRom ? 0.5f : hump_quarter(kinds[k]);
        float three_quarters = kinds[k] == CurveCatmullRom
            ? 0.5f : hump_three_quarters(kinds[k]);

        frame(world);
        near(intensity_of(world, e), 2 * quarter);
        frame(world);
        near(intensity_of(world, e), 2);
        frame(world);
        near(intensity_of(world, e), 2 * three_quarters);
        frame(world);
        near(intensity_of(world, e), 0);
        frame(world);
        near(intensity_of(world, e), 2 * quarter);
        frame(world);
        near(intensity_of(world, e), 2);

        ecs_fini(world);
        box_render_null_free(null);
    }
}

/** Get a looping ParticleLight at intensity 2 over a second with a Catmull-Rom curve of uneven
 * keys that ends on its start value.
 *
 * @param loop Whether it repeats
 * @return The ParticleLight
 */
static ParticleLight wrap_light(
    bool loop)
{
    static CurveKey keys[5] = {
        { 0.0f, 0.4f }, { 0.15f, 1.2f }, { 0.45f, 0.7f }, { 0.8f, 1.1f }, { 1.0f, 0.4f }
    };

    return (ParticleLight){
        .intensity = 2,
        .curve = { .kind = CurveCatmullRom, .keys = { .array = keys, .count = 5, .size = 5 } },
        .period = 1,
        .loop = loop
    };
}

/** Run frames of a delta from a new emitter and keep the intensity after each.
 *
 * @param light The ParticleLight
 * @param first Seconds of the first frame
 * @param delta Seconds of the next frames
 * @param count Frames to run
 * @param out Receives count intensities
 */
static void wrap_samples(
    ParticleLight light,
    float first,
    float delta,
    int32_t count,
    float *out)
{
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t e = emitter_new(world, light);
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, i ? delta : first);
        box_render_sync(world);
        out[i] = intensity_of(world, e);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void Light_curve_catmull_rom_loop_wrap(void) {
    // particle/light
    ParticleLight light = wrap_light(true);
    Curve view = { .kind = light.curve.kind, .keys = light.curve.keys };
    const CurveKey *keys = ecs_vec_first_t(&light.curve.keys, CurveKey);
    float h = 0.01f, v[5];
    wrap_samples(light, 1.0f - 2.0f * h, h, 5, v);

    /* The clock at 0.98, 0.99, 1 (the wrap), 1.01 and 1.02 seconds */
    near(v[0], 2 * box_curve_loop_value(&view, 1.0f, 1.0f - 2.0f * h, 1.0f));
    near(v[1], 2 * box_curve_loop_value(&view, 1.0f, 1.0f - h, 1.0f));
    near(v[2], 2 * keys[0].value);
    near(v[3], 2 * box_curve_loop_value(&view, 1.0f, h, 1.0f));
    near(v[4], 2 * box_curve_loop_value(&view, 1.0f, 2.0f * h, 1.0f));
    test_assert(fabsf(v[1] - v[3]) < 0.02f);

    /* One sided second order slopes meet the slope across the wrap */
    float slope = 2 * (keys[1].value - keys[3].value) / (keys[1].time - keys[3].time + 1.0f);
    float before = (3.0f * v[2] - 4.0f * v[1] + v[0]) / (2.0f * h);
    float after = (4.0f * v[3] - 3.0f * v[2] - v[4]) / (2.0f * h);
    test_assert(fabsf(before - slope) < 0.05f);
    test_assert(fabsf(after - slope) < 0.05f);
}

void Light_curve_catmull_rom_once_unchanged(void) {
    // particle/light
    ParticleLight light = wrap_light(false);
    Curve view = { .kind = light.curve.kind, .keys = light.curve.keys };
    float v[10];
    wrap_samples(light, DELTA, DELTA, 10, v);
    int32_t i;
    for (i = 0; i < 10; i ++) {
        near(v[i], 2 * box_curve_value(&view, (float)(i + 1) * DELTA, 1.0f));
    }
}

void Light_bad_curve_is_one(void) {
    // particle/light
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    int32_t level = ecs_log_set_level(-4);
    ecs_entity_t e = emitter_new(world, (ParticleLight){
        .intensity = 3,
        .curve = { .keys = BOX_CURVE_KEYS({0, 0}, {2, 5}) },
        .period = 0.4f
    });

    frame(world);
    ecs_log_set_level(level);
    near(intensity_of(world, e), 3);

    ecs_fini(world);
    box_render_null_free(null);
}
