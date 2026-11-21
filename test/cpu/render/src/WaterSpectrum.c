/**
 * @file WaterSpectrum.c
 * @brief Tests for WaterSpectrum on the main thread: its reflection and defaults, the validation
 * of its parameters, the spectrum and its variance, the revision of the initial spectrum, the time
 * wrapped to the period of the waves, the water frame and the records the extract appends to the
 * packet, and the flag of the material record of its Water.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "draw.h"
#include <math.h>

/** 2 pi */
#define TAU (6.283185307179586)

/** Create a world with Scene.water that renders with a null backend and has a view.
 *
 * @param null Receives the backend
 * @return The world
 */
static ecs_world_t* world_new(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, Scene, {.shader_features.Extended.water = true});
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 10, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});

    return world;
}

/** Run a frame of a fixed delta and wait until the backend has its packet.
 *
 * @param world The world
 * @param delta Seconds the frame advances the world
 */
static void frame_dt(
    ecs_world_t *world,
    float delta)
{
    ecs_progress(world, delta);
    box_render_sync(world);
}

/** Create a sea: a Water box with a default WaterSpectrum at the origin.
 *
 * @param world The world
 * @return The sea
 */
static ecs_entity_t sea_new(
    ecs_world_t *world)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rgb, {40, 80, 120});
    ecs_add(world, e, Water);
    ecs_add(world, e, WaterSpectrum);

    return e;
}

/** Get the spectrum of the WaterSpectrum of an entity in the last packet.
 *
 * @param null The backend
 * @param water The entity that owns the WaterSpectrum
 * @return The record, NULL when the packet has none
 */
static const box_render_water_spectrum_t* spectrum_of(
    const box_render_null_t *null,
    ecs_entity_t water)
{
    const box_render_water_spectrum_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.water_spectra) && !result; i ++) {
        const box_render_water_spectrum_t *r = ecs_vec_get_t(
            &null->last.water_spectra, box_render_water_spectrum_t, i);
        if (r->water == water) {
            result = r;
        }
    }

    return result;
}

/** Get the record of the slot of the material table of an entity as the extract packed it.
 *
 * @param world The world
 * @param e The entity
 * @return The record
 */
static const box_render_material_t* record_of(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.extract.Draws");
    const ecs_system_t *s = ecs_system_get(world, system);
    test_assert(s != NULL && s->ctx != NULL);
    box_render_draws_t *d = s->ctx;
    const MaterialId *id = ecs_get(world, e, MaterialId);
    test_assert(id != NULL);
    test_assert(id->value < (uint32_t)ecs_vec_count(&d->materials.records));

    return ecs_vec_get_t(&d->materials.records, box_render_material_t, (int32_t)id->value);
}

/** Get the defaults of a WaterSpectrum as its parameters.
 *
 * @param world A world with box imported
 * @param out Receives the record
 */
static void defaults_of(
    ecs_world_t *world,
    box_render_water_spectrum_t *out)
{
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, WaterSpectrum);
    ecs_os_memset_t(out, 0, box_render_water_spectrum_t);
    box_render_spectrum_params(ecs_get(world, e, WaterSpectrum), out);
    ecs_delete(world, e);
}

void WaterSpectrum_defaults(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, WaterSpectrum);
    const WaterSpectrum *s = ecs_get(world, e, WaterSpectrum);
    test_assert(s != NULL);
    test_flt(s->wind_speed, BOX_MATERIAL_SPECTRUM_WIND_SPEED);
    test_flt(s->wind_speed, 4);
    test_flt(s->wind_direction[0], 1);
    test_flt(s->wind_direction[1], 0);
    test_flt(s->size, BOX_MATERIAL_SPECTRUM_SIZE);
    test_flt(s->size, 32);
    test_int(s->cascades, BOX_MATERIAL_SPECTRUM_CASCADES);
    test_int(s->cascades, 2);
    test_int(s->resolution, BOX_MATERIAL_SPECTRUM_RESOLUTION);
    test_int(s->resolution, 128);
    test_flt(s->directionality, BOX_MATERIAL_SPECTRUM_DIRECTIONALITY);
    test_flt(s->amplitude, 1);
    test_uint(s->seed, 0);
    test_flt(s->detail, BOX_MATERIAL_SPECTRUM_DETAIL);

    ecs_fini(world);
}

void WaterSpectrum_reflection(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "spectrum",
        "using box.material\n"
        "sea {\n"
        "  Water: {}\n"
        "  WaterSpectrum: {wind_speed: 7, wind_direction: {0, 1}, size: 64, cascades: 3,"
        " resolution: 256, directionality: 0.5, amplitude: 2, seed: 9, detail: 0.5}\n"
        "}\n", NULL) == 0);

    const WaterSpectrum *s = ecs_get(world, ecs_lookup(world, "sea"), WaterSpectrum);
    test_assert(s != NULL);
    test_flt(s->wind_speed, 7);
    test_flt(s->wind_direction[0], 0);
    test_flt(s->wind_direction[1], 1);
    test_flt(s->size, 64);
    test_int(s->cascades, 3);
    test_int(s->resolution, 256);
    test_flt(s->directionality, 0.5f);
    test_flt(s->amplitude, 2);
    test_uint(s->seed, 9);
    test_flt(s->detail, 0.5f);

    ecs_fini(world);
}

void WaterSpectrum_inherited(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t base = ecs_new_w_id(world, EcsPrefab);
    ecs_add(world, base, Water);
    ecs_set(world, base, WaterSpectrum, {.wind_speed = 6, .size = 20, .cascades = 1,
        .resolution = 64, .amplitude = 1});
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, base);
    test_assert(!ecs_owns(world, inst, WaterSpectrum));
    const WaterSpectrum *s = ecs_get(world, inst, WaterSpectrum);
    test_assert(s != NULL);
    test_flt(s->wind_speed, 6);
    test_flt(s->size, 20);

    ecs_fini(world);
}

void WaterSpectrum_resolution_rounded(void) {
    test_int(box_render_spectrum_resolution(128), 128);
    test_int(box_render_spectrum_resolution(64), 64);
    test_int(box_render_spectrum_resolution(256), 256);
    test_int(box_render_spectrum_resolution(100), 128);
    test_int(box_render_spectrum_resolution(95), 64);
    test_int(box_render_spectrum_resolution(96), 128);
    test_int(box_render_spectrum_resolution(200), 256);
    test_int(box_render_spectrum_resolution(1000), 256);
    test_int(box_render_spectrum_resolution(1), 64);
    test_int(box_render_spectrum_resolution(0), 64);
    test_int(box_render_spectrum_resolution(-5), 64);
}

void WaterSpectrum_cascades_clamped(void) {
    test_int(box_render_spectrum_cascades(1), 1);
    test_int(box_render_spectrum_cascades(2), 2);
    test_int(box_render_spectrum_cascades(3), 3);
    test_int(box_render_spectrum_cascades(4), 3);
    test_int(box_render_spectrum_cascades(0), 1);
    test_int(box_render_spectrum_cascades(-2), 1);
}

void WaterSpectrum_params_validated(void) {
    WaterSpectrum s = {
        .wind_speed = 5, .wind_direction = {0, 2}, .size = 50, .cascades = 3,
        .resolution = 100, .directionality = 2, .amplitude = 1.5f, .seed = 4, .detail = 0.5f
    };
    box_render_water_spectrum_t r = {0};
    box_render_spectrum_params(&s, &r);
    test_uint(r.resolution, 128);
    test_uint(r.cascades, 3);
    test_flt(r.sizes[0], 50);
    test_flt(r.sizes[1], 10);
    test_flt(r.sizes[2], 2);
    test_flt(r.cuts[0], 0);
    test_flt(r.cuts[1], (float)(TAU * BOX_RENDER_SPECTRUM_CUT / 10.0));
    test_flt(r.cuts[2], (float)(TAU * BOX_RENDER_SPECTRUM_CUT / 2.0));
    test_flt(r.cuts[3], (float)(GLM_PI * BOX_RENDER_SPECTRUM_NYQUIST * 128 / 2.0));
    test_flt(r.wind[0], 0);
    test_flt(r.wind[1], 1);
    test_flt(r.wind_speed, 5);
    test_flt(r.directionality, 1);
    test_flt(r.amplitude, 1.5f);
    test_flt(r.detail, 0.5f);
    test_uint(r.seed, 4);

    /* The cut between two cascades lies below the shortest wave the larger tile resolves */
    test_assert(r.cuts[1] < (float)(GLM_PI * 128 / 50.0));

    /* Two cascades, the second one fifth of the first */
    s.cascades = 2;
    box_render_spectrum_params(&s, &r);
    test_uint(r.cascades, 2);
    test_flt(r.sizes[2], 0);
    test_flt(r.cuts[2], (float)(GLM_PI * BOX_RENDER_SPECTRUM_NYQUIST * 128 / 10.0));
    test_flt(r.cuts[3], 0);

    /* What is not valid falls back */
    s = (WaterSpectrum){
        .wind_speed = NAN, .wind_direction = {0, 0}, .size = -1, .cascades = 0,
        .resolution = 4, .directionality = -1, .amplitude = -2, .detail = NAN
    };
    box_render_spectrum_params(&s, &r);
    test_uint(r.resolution, 64);
    test_uint(r.cascades, 1);
    test_flt(r.sizes[0], BOX_MATERIAL_SPECTRUM_SIZE);
    test_flt(r.sizes[1], 0);
    test_flt(r.cuts[1], (float)(GLM_PI * BOX_RENDER_SPECTRUM_NYQUIST * 64 /
        BOX_MATERIAL_SPECTRUM_SIZE));
    test_flt(r.wind[0], 1);
    test_flt(r.wind[1], 0);
    test_flt(r.wind_speed, 0);
    test_flt(r.directionality, 0);
    test_flt(r.amplitude, 0);
    test_flt(r.detail, 0);
}

void WaterSpectrum_omega_quantized(void) {
    double step = TAU / BOX_RENDER_SPECTRUM_PERIOD;
    double k;
    for (k = 0.1; k < 600.0; k *= 1.37) {
        double w = box_render_spectrum_omega(k);
        double exact = sqrt((double)BOX_MATERIAL_WATER_GRAVITY * k);
        double n = w / step;
        test_assert(fabs(n - round(n)) < 1e-6);
        test_assert(w <= exact + 1e-9);
        test_assert(exact - w < step);
    }

    test_flt(box_render_spectrum_omega(0), 0);
}

void WaterSpectrum_density(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_water_spectrum_t r;
    defaults_of(world, &r);

    /* Nothing at the zero wave vector, more along the wind than across or against it */
    test_flt(box_render_spectrum_density(&r, 0, 0), 0);
    double along = box_render_spectrum_density(&r, 0.5, 0);
    double across = box_render_spectrum_density(&r, 0, 0.5);
    double against = box_render_spectrum_density(&r, -0.5, 0);
    test_assert(along > 0);
    test_assert(along > across);
    test_assert(across > 0);
    test_flt(across, against);

    /* Fully directional: nothing against the wind */
    r.directionality = 1;
    test_flt(box_render_spectrum_density(&r, -0.5, 0), 0);
    test_assert(box_render_spectrum_density(&r, 0.5, 0) > 0);

    /* No wind or no amplitude, no waves; the amplitude scales the heights */
    r.directionality = 0.8f;
    double one = box_render_spectrum_density(&r, 0.5, 0);
    r.amplitude = 2;
    test_assert(fabs(box_render_spectrum_density(&r, 0.5, 0) - 4 * one) < 1e-9 * one + 1e-30);
    r.amplitude = 0;
    test_flt(box_render_spectrum_density(&r, 0.5, 0), 0);
    r.amplitude = 1;
    r.wind_speed = 0;
    test_flt(box_render_spectrum_density(&r, 0.5, 0), 0);

    ecs_fini(world);
}

void WaterSpectrum_variance_grows_with_wind(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_water_spectrum_t r;
    defaults_of(world, &r);

    double h4 = 0, s4 = 0, h8 = 0, s8 = 0;
    int32_t c;
    for (c = 0; c < (int32_t)r.cascades; c ++) {
        h4 += box_render_spectrum_variance(&r, c, false);
        s4 += box_render_spectrum_variance(&r, c, true);
    }

    r.wind_speed = 8;
    for (c = 0; c < (int32_t)r.cascades; c ++) {
        h8 += box_render_spectrum_variance(&r, c, false);
        s8 += box_render_spectrum_variance(&r, c, true);
    }

    test_assert(h4 > 0);
    test_assert(s4 > 0);
    test_assert(h8 > h4);
    test_assert(s8 > s4);

    /* A light breeze: a standard deviation of the height of centimeters to decimeters, gentle
     * slopes */
    test_assert(sqrt(h4) > 0.01);
    test_assert(sqrt(h4) < 0.3);
    test_assert(sqrt(s4) < 0.5);

    /* A cascade it lacks has none */
    test_flt(box_render_spectrum_variance(&r, 2, false), 0);
    test_flt(box_render_spectrum_variance(&r, -1, false), 0);

    ecs_fini(world);
}

void WaterSpectrum_time_wraps(void) {
    double period = BOX_RENDER_SPECTRUM_PERIOD;
    test_flt(box_render_spectrum_time(0), 0);
    test_flt(box_render_spectrum_time(10.5), 10.5);
    test_assert(fabs(box_render_spectrum_time(period + 3.25) - 3.25) < 1e-9);
    test_assert(fabs(box_render_spectrum_time(1000 * period + 7.5) - 7.5) < 1e-6);
    test_assert(fabs(box_render_spectrum_time(-1.0) - (period - 1.0)) < 1e-9);
    test_flt(box_render_spectrum_time(period), 0);
    test_flt(box_render_spectrum_time(NAN), 0);
    test_flt(box_render_spectrum_time(INFINITY), 0);

    /* The record of a frame carries the render time wrapped */
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t sea = sea_new(world);
    frame_dt(world, 100);
    frame_dt(world, 100);
    frame_dt(world, 100);
    const box_render_water_spectrum_t *r = spectrum_of(null, sea);
    test_assert(r != NULL);
    double expect = box_render_spectrum_time(null->last.time);
    test_assert(null->last.time > period);
    test_assert(fabs(r->time - expect) < 1e-3);
    test_assert(r->time >= 0 && r->time < period);

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterSpectrum_record(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t sea = sea_new(world);
    frame_dt(world, 1.0f / 60);

    test_int(ecs_vec_count(&null->last.water_spectra), 1);
    const box_render_water_spectrum_t *r = spectrum_of(null, sea);
    test_assert(r != NULL);
    test_uint(r->scene, 0);
    test_uint(r->resolution, 128);
    test_uint(r->cascades, 2);
    test_flt(r->sizes[0], 32);
    test_flt(r->sizes[1], 32 / BOX_RENDER_SPECTRUM_RATIO);
    test_flt(r->wind_speed, 4);
    test_flt(r->detail, BOX_MATERIAL_SPECTRUM_DETAIL);
    test_assert(r->height_scale > 0);
    test_assert(r->revision != 0);
    test_assert(r->offset[0] >= 0 && r->offset[0] < 32);
    test_assert(r->offset[1] >= 0 && r->offset[1] < 32);

    /* The height scale is one over twice the standard deviation of the height */
    double variance = box_render_spectrum_variance(r, 0, false) +
        box_render_spectrum_variance(r, 1, false);
    test_assert(fabs(r->height_scale - 0.5 / sqrt(variance)) < 1e-3 * r->height_scale);

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterSpectrum_regenerates_on_change(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t sea = sea_new(world);
    frame_dt(world, 1.0f / 60);
    uint32_t first = spectrum_of(null, sea)->revision, revision;
    test_assert(box_render_spectrum_revision(world, sea, &revision));
    test_uint(revision, first);

    /* Frames that change nothing keep the initial spectrum */
    frame_dt(world, 1.0f / 60);
    frame_dt(world, 1.0f / 60);
    test_uint(spectrum_of(null, sea)->revision, first);

    /* The detail and the flow do not change it */
    WaterSpectrum *s = ecs_ensure(world, sea, WaterSpectrum);
    s->detail = 0.75f;
    ecs_modified(world, sea, WaterSpectrum);
    ecs_set(world, sea, WaterFlow, {.velocity = {1, 0}});
    frame_dt(world, 1.0f / 60);
    test_uint(spectrum_of(null, sea)->revision, first);
    test_flt(spectrum_of(null, sea)->detail, 0.75f);

    /* Setting the same values does not either */
    s = ecs_ensure(world, sea, WaterSpectrum);
    s->wind_speed = 4;
    ecs_modified(world, sea, WaterSpectrum);
    frame_dt(world, 1.0f / 60);
    test_uint(spectrum_of(null, sea)->revision, first);

    /* Each parameter of the spectrum does */
    uint32_t last = first;
    int32_t i;
    for (i = 0; i < 8; i ++) {
        s = ecs_ensure(world, sea, WaterSpectrum);
        switch (i) {
        case 0: s->wind_speed = 6; break;
        case 1: s->wind_direction[0] = 0; s->wind_direction[1] = 1; break;
        case 2: s->size = 40; break;
        case 3: s->cascades = 3; break;
        case 4: s->resolution = 64; break;
        case 5: s->directionality = 0.3f; break;
        case 6: s->amplitude = 0.5f; break;
        case 7: s->seed = 12; break;
        }

        ecs_modified(world, sea, WaterSpectrum);
        frame_dt(world, 1.0f / 60);
        uint32_t now = spectrum_of(null, sea)->revision;
        test_assert(now != last);
        last = now;
        frame_dt(world, 1.0f / 60);
        test_uint(spectrum_of(null, sea)->revision, last);
    }

    /* A resolution that rounds to the same one does not */
    s = ecs_ensure(world, sea, WaterSpectrum);
    s->resolution = 70;
    ecs_modified(world, sea, WaterSpectrum);
    frame_dt(world, 1.0f / 60);
    test_uint(spectrum_of(null, sea)->revision, last);

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterSpectrum_frame_follows_flow(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t sea = sea_new(world);
    frame_dt(world, 0.5f);
    float still[2] = {
        spectrum_of(null, sea)->offset[0], spectrum_of(null, sea)->offset[1] };
    frame_dt(world, 0.5f);
    test_flt(spectrum_of(null, sea)->offset[0], still[0]);
    test_flt(spectrum_of(null, sea)->offset[1], still[1]);

    /* The tiles move with the water: the offset the fragments add moves back by the velocity */
    ecs_set(world, sea, WaterFlow, {.velocity = {2, -1}});
    frame_dt(world, 0.5f);
    float moved[2] = {
        spectrum_of(null, sea)->offset[0], spectrum_of(null, sea)->offset[1] };
    double dx = fmod((double)still[0] - moved[0] + 32.0, 32.0);
    double dz = fmod((double)moved[1] - still[1] + 32.0, 32.0);
    test_assert(fabs(dx - 1.0) < 1e-4);
    test_assert(fabs(dz - 0.5) < 1e-4);

    /* It stays wrapped to the largest tile */
    int32_t i;
    for (i = 0; i < 60; i ++) {
        frame_dt(world, 0.5f);
        test_assert(spectrum_of(null, sea)->offset[0] >= 0);
        test_assert(spectrum_of(null, sea)->offset[0] < 32);
        test_assert(spectrum_of(null, sea)->offset[1] >= 0);
        test_assert(spectrum_of(null, sea)->offset[1] < 32);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterSpectrum_no_spectrum(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t disabled = sea_new(world);
    ecs_enable(world, disabled, false);

    ecs_entity_t dry = ecs_new(world);
    ecs_set(world, dry, Position, {0, 0, 0});
    ecs_add(world, dry, WaterSpectrum);

    frame_dt(world, 1.0f / 60);
    test_int(ecs_vec_count(&null->last.water_spectra), 0);

    /* Without Scene.water the Water does nothing */
    ecs_enable(world, disabled, true);
    ecs_singleton_set(world, Scene, {.shader_features.Extended.water = false});
    frame_dt(world, 1.0f / 60);
    test_int(ecs_vec_count(&null->last.water_spectra), 0);

    ecs_singleton_set(world, Scene, {.shader_features.Extended.water = true});
    frame_dt(world, 1.0f / 60);
    test_int(ecs_vec_count(&null->last.water_spectra), 1);
    test_assert(spectrum_of(null, disabled) != NULL);

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterSpectrum_one_spectrum_per_scene(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t a = sea_new(world);
    ecs_entity_t b = sea_new(world);

    int level = ecs_log_set_level(-4);
    frame_dt(world, 1.0f / 60);
    frame_dt(world, 1.0f / 60);
    ecs_log_set_level(level);
    test_int(ecs_vec_count(&null->last.water_spectra), 1);
    test_assert(spectrum_of(null, a) != NULL || spectrum_of(null, b) != NULL);

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterSpectrum_instances_share(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t base = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, base, Box, {1, 1, 1});
    ecs_add(world, base, Water);
    ecs_add(world, base, WaterSpectrum);
    ecs_entity_t a = ecs_new_w_pair(world, EcsIsA, base);
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_entity_t b = ecs_new_w_pair(world, EcsIsA, base);
    ecs_set(world, b, Position, {4, 0, 0});

    /* Shapes that inherit one WaterSpectrum share its spectrum without an error */
    frame_dt(world, 1.0f / 60);
    frame_dt(world, 1.0f / 60);
    test_int(ecs_vec_count(&null->last.water_spectra), 1);
    test_assert(spectrum_of(null, base) != NULL);

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterSpectrum_removed(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t sea = sea_new(world);
    frame_dt(world, 1.0f / 60);
    test_int(ecs_vec_count(&null->last.water_spectra), 1);
    uint32_t revision;
    test_assert(box_render_spectrum_revision(world, sea, &revision));

    ecs_remove(world, sea, WaterSpectrum);
    frame_dt(world, 1.0f / 60);
    test_int(ecs_vec_count(&null->last.water_spectra), 0);
    test_assert(!box_render_spectrum_revision(world, sea, &revision));

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterSpectrum_material_flag(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t sea = sea_new(world);
    ecs_entity_t pond = ecs_new(world);
    ecs_set(world, pond, Box, {1, 1, 1});
    ecs_set(world, pond, Position, {3, 0, 0});
    ecs_set(world, pond, Rgb, {40, 80, 120});
    ecs_add(world, pond, Water);
    frame_dt(world, 1.0f / 60);
    frame_dt(world, 1.0f / 60);

    test_uint(record_of(world, sea)->water_flow_flags & BOX_RENDER_WATER_FLOW_SPECTRUM,
        BOX_RENDER_WATER_FLOW_SPECTRUM);
    test_uint(record_of(world, pond)->water_flow_flags & BOX_RENDER_WATER_FLOW_SPECTRUM, 0);

    /* Adding and removing the component packs the record again */
    ecs_add(world, pond, WaterSpectrum);
    ecs_remove(world, sea, WaterSpectrum);
    int level = ecs_log_set_level(-4);
    frame_dt(world, 1.0f / 60);
    frame_dt(world, 1.0f / 60);
    ecs_log_set_level(level);
    test_uint(record_of(world, sea)->water_flow_flags & BOX_RENDER_WATER_FLOW_SPECTRUM, 0);
    test_uint(record_of(world, pond)->water_flow_flags & BOX_RENDER_WATER_FLOW_SPECTRUM,
        BOX_RENDER_WATER_FLOW_SPECTRUM);

    ecs_fini(world);
    box_render_null_free(null);
}
