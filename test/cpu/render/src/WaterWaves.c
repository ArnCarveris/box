/**
 * @file WaterWaves.c
 * @brief Tests for WaterWaves and WaterFlow: their reflection and defaults, the phases the main
 * thread integrates in double precision, the wave table and the moving record of a Water.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "draw.h"
#include <math.h>
#include <stddef.h>

/** 2 pi */
#define TAU (6.283185307179586)

/** Create a world that renders with a null backend and has a view.
 *
 * @param null Receives the backend
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
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
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

/** Get the state of the batches of a world.
 *
 * @param world The world
 * @return The state
 */
static box_render_draws_t* draws_of(
    ecs_world_t *world)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.extract.Draws");
    const ecs_system_t *s = ecs_system_get(world, system);
    test_assert(s != NULL && s->ctx != NULL);

    return s->ctx;
}

/** Get the record of a slot of the material table as the extract packed it.
 *
 * @param world The world
 * @param id The slot
 * @return The record
 */
static const box_render_material_t* record_of(
    ecs_world_t *world,
    uint32_t id)
{
    box_render_draws_t *d = draws_of(world);
    test_assert(id < (uint32_t)ecs_vec_count(&d->materials.records));

    return ecs_vec_get_t(&d->materials.records, box_render_material_t, (int32_t)id);
}

/** Get the MaterialId an entity owns or inherits.
 *
 * @param world The world
 * @param e The entity
 * @return The slot, UINT32_MAX without a MaterialId
 */
static uint32_t id_of(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const MaterialId *id = ecs_get(world, e, MaterialId);

    return id ? id->value : UINT32_MAX;
}

/** Get the set of the waves a record names.
 *
 * @param record The record
 * @return The set, 0 for none
 */
static uint32_t set_of(
    const box_render_material_t *record)
{
    return (record->water_flags >> BOX_RENDER_WATER_WAVES_SHIFT) & BOX_RENDER_WATER_WAVES_MASK;
}

/** Get the distance of two phases on the circle.
 *
 * @param a The first phase
 * @param b The second phase
 * @return The distance, 0..pi
 */
static double phase_distance(
    double a,
    double b)
{
    double d = fmod(fabs(a - b), TAU);

    return d > TAU / 2 ? TAU - d : d;
}

/** Create a box shape with a Water.
 *
 * @param world The world
 * @param x Its position on X
 * @return The shape
 */
static ecs_entity_t pond_new(
    ecs_world_t *world,
    float x)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_set(world, e, Position, {x, 0, 0});
    ecs_set(world, e, Rgb, {40, 80, 120});
    ecs_add(world, e, Water);

    return e;
}

/** The waves of a WaterWaves with one swell of a wavelength of 8 and an amplitude of 0.25.
 *
 * @return The waves
 */
static WaterWaves swell(void)
{
    WaterWaves result = { .detail = 1 };
    result.waves[0] = (WaterWave){
        .direction = {1, 0}, .wavelength = 8, .amplitude = 0.25f, .steepness = 0.5f };

    return result;
}

void WaterWaves_defaults(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "waves",
        "using box.material\n"
        "pond {\n"
        "  Water: {}\n"
        "  WaterWaves: {waves: [{direction: {0, 1}, wavelength: 12, amplitude: 0.3,"
        " steepness: 0.4, speed: 2}, {wavelength: 3, amplitude: 0.05}], detail: 0.5}\n"
        "  WaterFlow: {velocity: {0.5, -1}}\n"
        "}\n", NULL) == 0);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, WaterWaves);
    const WaterWaves *w = ecs_get(world, added, WaterWaves);
    test_assert(w != NULL);
    test_flt(w->detail, BOX_MATERIAL_WATER_WAVES_DETAIL);
    test_flt(w->detail, 1);
    int32_t i;
    for (i = 0; i < BOX_MATERIAL_WATER_WAVES; i ++) {
        test_flt(w->waves[i].direction[0], 0);
        test_flt(w->waves[i].direction[1], 0);
        test_flt(w->waves[i].wavelength, 0);
        test_flt(w->waves[i].amplitude, 0);
        test_flt(w->waves[i].steepness, 0);
        test_flt(w->waves[i].speed, 0);
    }

    ecs_add(world, added, WaterFlow);
    test_flt(ecs_get(world, added, WaterFlow)->velocity[0], 0);
    test_flt(ecs_get(world, added, WaterFlow)->velocity[1], 0);

    ecs_entity_t pond = ecs_lookup(world, "pond");
    w = ecs_get(world, pond, WaterWaves);
    test_assert(w != NULL);
    test_flt(w->detail, 0.5f);
    test_flt(w->waves[0].direction[0], 0);
    test_flt(w->waves[0].direction[1], 1);
    test_flt(w->waves[0].wavelength, 12);
    test_flt(w->waves[0].amplitude, 0.3f);
    test_flt(w->waves[0].steepness, 0.4f);
    test_flt(w->waves[0].speed, 2);
    test_flt(w->waves[1].wavelength, 3);
    test_flt(w->waves[1].amplitude, 0.05f);
    test_flt(w->waves[2].wavelength, 0);
    test_flt(ecs_get(world, pond, WaterFlow)->velocity[0], 0.5f);
    test_flt(ecs_get(world, pond, WaterFlow)->velocity[1], -1);

    /* Reflection knows the members */
    const EcsStruct *s = ecs_get(world, ecs_id(WaterWave), EcsStruct);
    test_assert(s != NULL);
    test_int(ecs_vec_count(&s->members), 5);
    test_str(ecs_vec_get_t(&s->members, ecs_member_t, 0)->name, "direction");
    test_str(ecs_vec_get_t(&s->members, ecs_member_t, 1)->name, "wavelength");
    test_str(ecs_vec_get_t(&s->members, ecs_member_t, 2)->name, "amplitude");
    test_str(ecs_vec_get_t(&s->members, ecs_member_t, 3)->name, "steepness");
    test_str(ecs_vec_get_t(&s->members, ecs_member_t, 4)->name, "speed");
    s = ecs_get(world, ecs_id(WaterWaves), EcsStruct);
    test_assert(s != NULL);
    test_int(ecs_vec_count(&s->members), 2);
    test_str(ecs_vec_get_t(&s->members, ecs_member_t, 0)->name, "waves");
    test_int(ecs_vec_get_t(&s->members, ecs_member_t, 0)->count, BOX_MATERIAL_WATER_WAVES);
    test_uint(ecs_vec_get_t(&s->members, ecs_member_t, 0)->type, ecs_id(WaterWave));
    test_str(ecs_vec_get_t(&s->members, ecs_member_t, 1)->name, "detail");
    s = ecs_get(world, ecs_id(WaterFlow), EcsStruct);
    test_assert(s != NULL);
    test_int(ecs_vec_count(&s->members), 1);
    test_str(ecs_vec_get_t(&s->members, ecs_member_t, 0)->name, "velocity");
    test_assert(ecs_lookup(world, "box.material.WaterWaves") == ecs_id(WaterWaves));
    test_assert(ecs_lookup(world, "box.material.WaterFlow") == ecs_id(WaterFlow));

    /* Instances inherit both like Water */
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_add(world, prefab, Water);
    WaterWaves waves = swell();
    ecs_set_ptr(world, prefab, WaterWaves, &waves);
    ecs_set(world, prefab, WaterFlow, {{1, 2}});
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    test_bool(ecs_owns(world, instance, WaterWaves), false);
    test_bool(ecs_owns(world, instance, WaterFlow), false);
    test_flt(ecs_get(world, instance, WaterWaves)->waves[0].wavelength, 8);
    test_flt(ecs_get(world, instance, WaterFlow)->velocity[1], 2);

    ecs_fini(world);
}

void WaterWaves_resolve(void) {
    float dir[2];
    double k, omega;

    /* The direction is normalized, the deep water dispersion gives omega = sqrt(g k) */
    WaterWave w = { .direction = {3, 4}, .wavelength = 10, .amplitude = 1 };
    test_bool(box_draws_water_wave_resolve(&w, dir, &k, &omega), true);
    test_flt(dir[0], 0.6f);
    test_flt(dir[1], 0.8f);
    test_assert(fabs(k - TAU / 10) < 1e-12);
    test_assert(fabs(omega - sqrt(BOX_MATERIAL_WATER_GRAVITY * TAU / 10)) < 1e-6);

    /* The phase speed of the dispersion is sqrt(g L / 2 pi) */
    test_assert(fabs(omega / k - sqrt(BOX_MATERIAL_WATER_GRAVITY * 10 / TAU)) < 1e-6);

    /* A speed sets the phase speed */
    w.speed = 2;
    test_bool(box_draws_water_wave_resolve(&w, NULL, &k, &omega), true);
    test_assert(fabs(omega - 2 * TAU / 10) < 1e-9);

    /* A zero or broken direction is +X */
    w.direction[0] = 0;
    w.direction[1] = 0;
    box_draws_water_wave_resolve(&w, dir, NULL, NULL);
    test_flt(dir[0], 1);
    test_flt(dir[1], 0);
    w.direction[0] = NAN;
    box_draws_water_wave_resolve(&w, dir, NULL, NULL);
    test_flt(dir[0], 1);
    test_flt(dir[1], 0);

    /* A wavelength of at most 0 disables the wave */
    w.wavelength = 0;
    test_bool(box_draws_water_wave_resolve(&w, NULL, &k, &omega), false);
    test_assert(k == 0 && omega == 0);
    w.wavelength = -3;
    test_bool(box_draws_water_wave_resolve(&w, NULL, NULL, NULL), false);
    w.wavelength = INFINITY;
    test_bool(box_draws_water_wave_resolve(&w, NULL, NULL, NULL), false);
    test_assert(box_draws_water_wave_rate(&w, NULL) == 0);

    /* The flow adds k times the velocity along the direction */
    WaterWave east = { .direction = {1, 0}, .wavelength = 4, .speed = 1 };
    WaterFlow flow = { .velocity = {0.5f, 3} };
    test_assert(fabs(box_draws_water_wave_rate(&east, NULL) - TAU / 4) < 1e-9);
    test_assert(fabs(box_draws_water_wave_rate(&east, &flow) - 1.5 * TAU / 4) < 1e-9);
    flow.velocity[0] = -1;
    test_assert(fabs(box_draws_water_wave_rate(&east, &flow)) < 1e-9);
}

void WaterWaves_phase_wrapped(void) {
    test_assert(box_draws_water_phase_wrap(0) == 0);
    test_assert(fabs(box_draws_water_phase_wrap(TAU + 1) - 1) < 1e-12);
    test_assert(fabs(box_draws_water_phase_wrap(-1) - (TAU - 1)) < 1e-12);
    test_assert(box_draws_water_phase_wrap(NAN) == 0);
    test_assert(box_draws_water_phase_wrap(INFINITY) == 0);

    /* After hours the phases stay in 0..2 pi and exact in double precision */
    WaterWaves waves = swell();
    waves.waves[1] = (WaterWave){ .direction = {0, 1}, .wavelength = 0.7f, .amplitude = 0.01f };
    WaterFlow flow = { .velocity = {3, -2} };
    double phases[4] = {0};
    double time = 6 * 3600.0;
    box_draws_water_waves_step(phases, &waves, &flow, time);
    int32_t i;
    for (i = 0; i < BOX_MATERIAL_WATER_WAVES; i ++) {
        test_assert(phases[i] >= 0 && phases[i] < TAU);
    }

    test_assert(phase_distance(phases[0],
        box_draws_water_wave_rate(&waves.waves[0], &flow) * time) < 1e-6);
    test_assert(phase_distance(phases[1],
        box_draws_water_wave_rate(&waves.waves[1], &flow) * time) < 1e-6);
    test_assert(phases[2] == 0 && phases[3] == 0);

    /* The packed phases are f32 below 2 pi */
    box_render_water_waves_t set;
    box_draws_water_waves_pack(&set, &waves, phases);
    for (i = 0; i < BOX_MATERIAL_WATER_WAVES; i ++) {
        test_assert(set.waves[i].phase >= 0 && set.waves[i].phase < (float)TAU);
    }

    /* A step that is not finite or negative does nothing */
    double kept[4] = { phases[0], phases[1], phases[2], phases[3] };
    box_draws_water_waves_step(phases, &waves, &flow, NAN);
    box_draws_water_waves_step(phases, &waves, &flow, -1);
    for (i = 0; i < BOX_MATERIAL_WATER_WAVES; i ++) {
        test_assert(phases[i] == kept[i]);
    }
}

void WaterWaves_phase_frame_split(void) {
    WaterWaves waves = swell();
    waves.waves[1] = (WaterWave){ .direction = {1, 1}, .wavelength = 2.5f, .amplitude = 0.05f,
        .speed = 1.3f };
    waves.waves[2] = (WaterWave){ .direction = {-1, 0.2f}, .wavelength = 0.4f,
        .amplitude = 0.01f };
    WaterFlow flow = { .velocity = {0.7f, -0.3f} };
    double whole[4] = {0}, frames[4] = {0}, uneven[4] = {0};
    int32_t i;

    /* 10 seconds at once, in 600 frames of 1/60 and in uneven frames give the same phases */
    box_draws_water_waves_step(whole, &waves, &flow, 10.0);
    for (i = 0; i < 600; i ++) {
        box_draws_water_waves_step(frames, &waves, &flow, 1.0 / 60.0);
    }

    double left = 10.0;
    for (i = 0; left > 0; i ++) {
        double dt = 0.001 + 0.05 * (double)(i % 7);
        dt = dt < left ? dt : left;
        box_draws_water_waves_step(uneven, &waves, &flow, dt);
        left -= dt;
    }

    for (i = 0; i < BOX_MATERIAL_WATER_WAVES; i ++) {
        test_assert(phase_distance(whole[i], frames[i]) < 1e-6);
        test_assert(phase_distance(whole[i], uneven[i]) < 1e-6);
    }
}

void WaterWaves_phase_continuous_velocity(void) {
    WaterWaves waves = swell();
    WaterFlow flow = { .velocity = {0, 0} };
    double phases[4] = {0};
    double dt = 1.0 / 60.0;
    double k = TAU / 8;
    int32_t i;

    /* A velocity that changes every frame (a paddling boat) moves the phase on by its rate
     * times the frame, never by a jump of k times the change of the offset */
    for (i = 0; i < 600; i ++) {
        double was = phases[0];
        flow.velocity[0] = 4.0f * (float)sin((double)i * 0.05);
        box_draws_water_waves_step(phases, &waves, &flow, dt);
        double rate = box_draws_water_wave_rate(&waves.waves[0], &flow);
        test_assert(phase_distance(phases[0], was + rate * dt) < 1e-9);
        test_assert(phase_distance(phases[0], was) <= (sqrt(BOX_MATERIAL_WATER_GRAVITY * k) +
            k * 4.0) * dt + 1e-9);
    }

    /* The flow moves the pattern by velocity times time: the phase of the flow is k v t */
    WaterWaves still = swell();
    still.waves[0].speed = 1e-30f;
    double moved[4] = {0}, fixed[4] = {0};
    flow.velocity[0] = 2;
    flow.velocity[1] = 5;
    box_draws_water_waves_step(moved, &still, &flow, 3.0);
    box_draws_water_waves_step(fixed, &still, NULL, 3.0);
    test_assert(phase_distance(moved[0] - fixed[0], k * 2 * 3.0) < 1e-6);
}

void WaterWaves_pack(void) {
    WaterWaves waves = swell();
    waves.waves[0].direction[0] = 3;
    waves.waves[0].direction[1] = 4;
    waves.waves[1] = (WaterWave){ .wavelength = 2, .amplitude = -0.1f, .steepness = 3 };
    waves.waves[2] = (WaterWave){ .wavelength = 0, .amplitude = 1, .steepness = 1 };
    waves.waves[3] = (WaterWave){ .wavelength = 1, .amplitude = NAN, .steepness = NAN };
    waves.detail = 0.25f;
    double phases[4] = { 1, 2, 3, TAU };
    box_render_water_waves_t set;
    box_draws_water_waves_pack(&set, &waves, phases);
    test_int(ECS_SIZEOF(box_render_water_wave_t), 32);
    test_int(ECS_SIZEOF(box_render_water_waves_t), 144);
    test_int(offsetof(box_render_water_waves_t, detail), 128);

    test_flt(set.waves[0].direction[0], 0.6f);
    test_flt(set.waves[0].direction[1], 0.8f);
    test_flt(set.waves[0].k, (float)(TAU / 8));
    test_flt(set.waves[0].phase, 1);
    test_flt(set.waves[0].slope, (float)(0.25 * TAU / 8));
    test_flt(set.waves[0].steepness, 0.5f);

    /* A zero direction is +X, the amplitude its size, the steepness clamped */
    test_flt(set.waves[1].direction[0], 1);
    test_flt(set.waves[1].direction[1], 0);
    test_flt(set.waves[1].slope, (float)(0.1 * TAU / 2));
    test_flt(set.waves[1].steepness, 1);
    test_flt(set.waves[1].phase, 2);

    /* A disabled wave is zero */
    test_flt(set.waves[2].k, 0);
    test_flt(set.waves[2].slope, 0);
    test_flt(set.waves[2].steepness, 0);
    test_flt(set.waves[2].phase, 0);
    test_flt(set.waves[2].direction[0], 0);

    /* Broken floats are 0, a phase of 2 pi wraps to 0 */
    test_flt(set.waves[3].slope, 0);
    test_flt(set.waves[3].steepness, 0);
    test_flt(set.waves[3].phase, 0);
    test_uint(set.count, 3);
    test_flt(set.detail, 0.25f);

    waves.detail = -1;
    box_draws_water_waves_pack(&set, &waves, phases);
    test_flt(set.detail, 0);
    waves.detail = NAN;
    box_draws_water_waves_pack(&set, &waves, phases);
    test_flt(set.detail, 0);

    /* The record names the set in the bits above the foam flag, below the absorption */
    box_render_material_t record;
    box_draws_tables_init();
    Water water = {
        .shallow_color = {60, 170, 160}, .deep_color = {8, 40, 64}, .depth = 3,
        .visibility = 2, .foam_texture = 77, .absorption = {255, 255, 255}
    };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &water, .water_waves = 5, .fade = 1 });
    test_uint(set_of(&record), 5);
    test_uint(record.water_flags & BOX_RENDER_WATER_FOAM_TEXTURE, BOX_RENDER_WATER_FOAM_TEXTURE);
    test_uint(record.water_flags >> BOX_RENDER_WATER_ABSORPTION_SHIFT, 0xFFFFFFu);
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &water, .water_waves = BOX_RENDER_WATER_WAVE_SETS - 1, .fade = 1 });
    test_uint(set_of(&record), BOX_RENDER_WATER_WAVE_SETS - 1);
    test_uint(record.water_flags >> BOX_RENDER_WATER_ABSORPTION_SHIFT, 0xFFFFFFu);

    /* Without waves, or without Water, the record is what it was */
    box_render_material_t plain;
    box_draws_material_pack(&plain, &(box_draw_material_t){ .water = &water, .fade = 1 });
    test_uint(set_of(&plain), 0);
    box_draws_material_pack(&record, &(box_draw_material_t){ .water_waves = 5, .fade = 1 });
    test_uint(record.water_flags, 0);

    /* A transition of another component encodes the record with its set again */
    box_draw_transition_material_t target;
    box_draws_transition_target(&target, &(box_draw_material_t){
        .water = &water, .water_waves = 9, .fade = 1 }, BOX_DRAW_TRANSITION_TABLE);
    box_draws_transition_encode(&target, &record);
    test_uint(set_of(&record), 9);
}

void WaterWaves_moving_record(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_singleton_set(world, Scene, {.water = true});
    ecs_entity_t a = pond_new(world, 0);
    ecs_entity_t b = pond_new(world, 2);
    frame_dt(world, 1.0f / 60.0f);
    frame_dt(world, 1.0f / 60.0f);

    /* Water without WaterWaves shares its slot and names no set */
    uint32_t shared = id_of(world, a);
    test_assert(shared != 0 && shared != UINT32_MAX);
    test_uint(id_of(world, b), shared);
    test_uint(set_of(record_of(world, shared)), 0);
    box_render_draws_t *d = draws_of(world);
    test_uint(box_draws_water_waves_find(&d->materials, a), 0);

    /* WaterWaves give the shape a private slot with a set of the wave table */
    WaterWaves waves = swell();
    ecs_set_ptr(world, a, WaterWaves, &waves);
    frame_dt(world, 1.0f / 60.0f);
    frame_dt(world, 1.0f / 60.0f);
    uint32_t own = id_of(world, a);
    test_assert(own != shared && own != 0 && own != UINT32_MAX);
    test_uint(id_of(world, b), shared);
    uint32_t set = set_of(record_of(world, own));
    test_assert(set != 0);
    test_uint(box_draws_water_waves_find(&d->materials, a), set);
    test_uint(box_draws_water_waves_find(&d->materials, b), 0);
    test_assert(d->materials.waves != NULL);
    test_bool(d->materials.waves->created, true);

    /* The phase of the set moves every frame, by the rate times the delta */
    double rate = box_draws_water_wave_rate(&waves.waves[0], NULL);
    int32_t i;
    for (i = 0; i < 5; i ++) {
        double was = d->materials.waves->states[set].phases[0];
        frame_dt(world, 0.1f);
        double now = d->materials.waves->states[set].phases[0];
        test_assert(phase_distance(now, was + rate * 0.1) < 1e-5);
        test_flt(d->materials.waves->table[set].waves[0].phase, (float)now);
        test_flt(d->materials.waves->table[set].waves[0].k, (float)(TAU / 8));
        test_uint(id_of(world, a), own);
        test_uint(set_of(record_of(world, own)), set);
    }

    /* The phase is that of the total time, as if integrated from 0 */
    double time = d->materials.time;
    test_assert(phase_distance(d->materials.waves->states[set].phases[0], rate * time) < 1e-5);

    /* A WaterFlow speeds it up by k times the velocity along the wave */
    ecs_set(world, a, WaterFlow, {{1.5f, 0}});
    frame_dt(world, 0.1f);
    double was = d->materials.waves->states[set].phases[0];
    frame_dt(world, 0.1f);
    WaterFlow flow = { .velocity = {1.5f, 0} };
    double flowing = box_draws_water_wave_rate(&waves.waves[0], &flow);
    test_assert(flowing > rate);
    test_assert(phase_distance(d->materials.waves->states[set].phases[0],
        was + flowing * 0.1) < 1e-5);

    /* Without WaterWaves the shape shares the slot of b again and frees its set */
    ecs_remove(world, a, WaterWaves);
    frame_dt(world, 1.0f / 60.0f);
    frame_dt(world, 1.0f / 60.0f);
    test_uint(id_of(world, a), id_of(world, b));
    test_uint(set_of(record_of(world, id_of(world, a))), 0);
    test_uint(box_draws_water_waves_find(&d->materials, a), 0);
    test_assert(d->materials.waves->states[set].owner == 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterWaves_without_water(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_singleton_set(world, Scene, {.water = true});
    WaterWaves waves = swell();

    /* WaterWaves and WaterFlow on a shape without Water do nothing */
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Rgb, {40, 80, 120});
    ecs_set_ptr(world, a, WaterWaves, &waves);
    ecs_set(world, a, WaterFlow, {{1, 0}});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_set(world, b, Position, {2, 0, 0});
    ecs_set(world, b, Rgb, {40, 80, 120});
    frame_dt(world, 1.0f / 60.0f);
    frame_dt(world, 1.0f / 60.0f);
    test_uint(id_of(world, a), id_of(world, b));
    box_render_draws_t *d = draws_of(world);
    test_uint(box_draws_water_waves_find(&d->materials, a), 0);
    test_assert(d->materials.waves == NULL);

    /* A WaterFlow alone keeps a Water still and shared */
    ecs_entity_t c = pond_new(world, 4);
    ecs_entity_t e = pond_new(world, 6);
    ecs_set(world, c, WaterFlow, {{1, 0}});
    frame_dt(world, 1.0f / 60.0f);
    frame_dt(world, 1.0f / 60.0f);
    test_uint(id_of(world, c), id_of(world, e));
    test_uint(set_of(record_of(world, id_of(world, c))), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterWaves_water_off(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t a = pond_new(world, 0);
    ecs_entity_t b = pond_new(world, 2);
    WaterWaves waves = swell();
    ecs_set_ptr(world, a, WaterWaves, &waves);
    frame_dt(world, 1.0f / 60.0f);
    frame_dt(world, 1.0f / 60.0f);

    /* A scene without Scene.water packs neither the Water nor its waves */
    test_uint(id_of(world, a), id_of(world, b));
    test_flt(record_of(world, id_of(world, a))->water_depth, 0);
    test_uint(record_of(world, id_of(world, a))->water_flags, 0);
    box_render_draws_t *d = draws_of(world);
    test_uint(box_draws_water_waves_find(&d->materials, a), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterWaves_prefab_instances(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_singleton_set(world, Scene, {.water = true});
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_add(world, prefab, Water);
    WaterWaves waves = swell();
    ecs_set_ptr(world, prefab, WaterWaves, &waves);
    ecs_entity_t i1 = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, i1, Position, {0, 0, 0});
    ecs_entity_t i2 = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, i2, Position, {2, 0, 0});
    frame_dt(world, 1.0f / 60.0f);
    frame_dt(world, 1.0f / 60.0f);

    /* The instances share the record of the prefab and its set */
    uint32_t id = id_of(world, prefab);
    test_assert(id != 0 && id != UINT32_MAX);
    test_uint(id_of(world, i1), id);
    test_uint(id_of(world, i2), id);
    uint32_t set = set_of(record_of(world, id));
    test_assert(set != 0);
    box_render_draws_t *d = draws_of(world);
    test_uint(box_draws_water_waves_find(&d->materials, prefab), set);

    /* Its phases move */
    double was = d->materials.waves->states[set].phases[0];
    frame_dt(world, 0.1f);
    test_assert(phase_distance(d->materials.waves->states[set].phases[0], was) > 1e-3);

    ecs_fini(world);
    box_render_null_free(null);
}
