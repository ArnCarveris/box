/**
 * @file WaterCaustics.c
 * @brief Tests for WaterCaustics: its reflection and defaults, the record of a Water with it, the
 * water frame the main thread integrates from the WaterFlow in double precision and its wrap.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "draw.h"
#include <math.h>
#include <stddef.h>

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

/** Get the distance of two offsets of the water frame on the circle of its period.
 *
 * @param a The first offset
 * @param b The second offset
 * @return The distance, 0..half the period
 */
static double frame_distance(
    double a,
    double b)
{
    double d = fmod(fabs(a - b), BOX_RENDER_CAUSTICS_PERIOD);

    return d > BOX_RENDER_CAUSTICS_PERIOD / 2 ? BOX_RENDER_CAUSTICS_PERIOD - d : d;
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

void WaterCaustics_defaults(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "caustics",
        "using box.material\n"
        "pool {\n"
        "  Water: {}\n"
        "  WaterCaustics: {strength: 0.6, scale: 1.5, speed: 0.25, depth: 3}\n"
        "}\n"
        "partial {\n"
        "  Water: {}\n"
        "  WaterCaustics: {scale: 2}\n"
        "}\n", NULL) == 0);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, WaterCaustics);
    const WaterCaustics *c = ecs_get(world, added, WaterCaustics);
    test_assert(c != NULL);
    test_flt(c->strength, BOX_MATERIAL_WATER_CAUSTICS_STRENGTH);
    test_flt(c->strength, 1);
    test_flt(c->scale, BOX_MATERIAL_WATER_CAUSTICS_SCALE);
    test_flt(c->scale, 1);
    test_flt(c->speed, BOX_MATERIAL_WATER_CAUSTICS_SPEED);
    test_flt(c->speed, 0.5f);
    test_flt(c->depth, BOX_MATERIAL_WATER_CAUSTICS_DEPTH);
    test_flt(c->depth, 2);

    c = ecs_get(world, ecs_lookup(world, "pool"), WaterCaustics);
    test_assert(c != NULL);
    test_flt(c->strength, 0.6f);
    test_flt(c->scale, 1.5f);
    test_flt(c->speed, 0.25f);
    test_flt(c->depth, 3);

    /* A field a scene does not set keeps its default */
    c = ecs_get(world, ecs_lookup(world, "partial"), WaterCaustics);
    test_assert(c != NULL);
    test_flt(c->strength, 1);
    test_flt(c->scale, 2);
    test_flt(c->speed, 0.5f);
    test_flt(c->depth, 2);

    /* Reflection knows the members */
    const EcsStruct *s = ecs_get(world, ecs_id(WaterCaustics), EcsStruct);
    test_assert(s != NULL);
    test_int(ecs_vec_count(&s->members), 4);
    test_str(ecs_vec_get_t(&s->members, ecs_member_t, 0)->name, "strength");
    test_str(ecs_vec_get_t(&s->members, ecs_member_t, 1)->name, "scale");
    test_str(ecs_vec_get_t(&s->members, ecs_member_t, 2)->name, "speed");
    test_str(ecs_vec_get_t(&s->members, ecs_member_t, 3)->name, "depth");
    test_assert(ecs_lookup(world, "box.material.WaterCaustics") == ecs_id(WaterCaustics));

    /* Instances inherit it like Water */
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_add(world, prefab, Water);
    ecs_set(world, prefab, WaterCaustics, {.strength = 2, .scale = 3, .speed = 0, .depth = 4});
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    test_bool(ecs_owns(world, instance, WaterCaustics), false);
    test_flt(ecs_get(world, instance, WaterCaustics)->strength, 2);
    test_flt(ecs_get(world, instance, WaterCaustics)->depth, 4);

    ecs_fini(world);
}

void WaterCaustics_frame_wrapped(void) {
    double P = BOX_RENDER_CAUSTICS_PERIOD;
    test_assert(box_draws_water_caustics_wrap(0) == 0);
    test_assert(fabs(box_draws_water_caustics_wrap(P + 1) - 1) < 1e-12);
    test_assert(fabs(box_draws_water_caustics_wrap(-1) - (P - 1)) < 1e-12);
    test_assert(box_draws_water_caustics_wrap(P) == 0);
    test_assert(box_draws_water_caustics_wrap(NAN) == 0);
    test_assert(box_draws_water_caustics_wrap(INFINITY) == 0);

    /* The frequency is 1 over the scale, a broken scale the default or the smallest */
    WaterCaustics c = { .strength = 1, .scale = 2, .speed = 0.5f, .depth = 2 };
    test_flt(box_draws_water_caustics_frequency(&c), 0.5f);
    c.scale = 0;
    test_flt(box_draws_water_caustics_frequency(&c), 1.0f / BOX_RENDER_CAUSTICS_MIN);
    c.scale = -4;
    test_flt(box_draws_water_caustics_frequency(&c), 1.0f / BOX_RENDER_CAUSTICS_MIN);
    c.scale = NAN;
    test_flt(box_draws_water_caustics_frequency(&c), 1.0f / BOX_MATERIAL_WATER_CAUSTICS_SCALE);
    c.scale = 2;

    /* The frame moves by the velocity over the scale, in cells, and wraps at the period */
    double frame[2] = { 0, 0 };
    WaterFlow flow = { .velocity = {1.5f, -1} };
    box_draws_water_caustics_step(frame, &c, &flow, 2);
    test_assert(fabs(frame[0] - 1.5) < 1e-12);
    test_assert(fabs(frame[1] - (P - 1)) < 1e-12);
    frame[0] = P - 0.25;
    box_draws_water_caustics_step(frame, &c, &flow, 1);
    test_assert(fabs(frame[0] - 0.5) < 1e-12);

    /* No flow, a negative or broken step does nothing */
    double was[2] = { frame[0], frame[1] };
    box_draws_water_caustics_step(frame, &c, NULL, 1);
    box_draws_water_caustics_step(frame, &c, &flow, -1);
    box_draws_water_caustics_step(frame, &c, &flow, NAN);
    test_assert(frame[0] == was[0] && frame[1] == was[1]);

    /* A broken velocity stays still on its axis */
    flow.velocity[1] = NAN;
    box_draws_water_caustics_step(frame, &c, &flow, 1);
    test_assert(fabs(frame[0] - (was[0] + 0.75)) < 1e-12);
    test_assert(frame[1] == was[1]);

    /* After hours of small steps the frame stays exact in double precision */
    flow.velocity[0] = 0.37f;
    flow.velocity[1] = 0;
    frame[0] = 0;
    double total = 0;
    int32_t i;
    for (i = 0; i < 216000; i ++) {
        box_draws_water_caustics_step(frame, &c, &flow, 1.0 / 60.0);
        total += 1.0 / 60.0;
    }
    test_assert(frame[0] >= 0 && frame[0] < P);
    test_assert(frame_distance(frame[0], (double)0.37f * 0.5 * total) < 1e-6);

    /* The phase is speed times time over the loop, wrapped to 0..1 */
    float L = (float)BOX_RENDER_CAUSTICS_LOOP;
    test_flt(box_draws_water_caustics_phase(0.5f, 4), 2.0f / L);
    test_flt(box_draws_water_caustics_phase(1, L * 3 + 1), 1.0f / L);
    test_flt(box_draws_water_caustics_phase(-1, 1), 1.0f - 1.0f / L);
    test_flt(box_draws_water_caustics_phase(0, 100), 0);
    test_flt(box_draws_water_caustics_phase(NAN, 1), 0);
    test_flt(box_draws_water_caustics_phase(1, INFINITY), 0);
    float late = box_draws_water_caustics_phase(0.5f, 36000.25);
    test_assert(late >= 0 && late < 1);
    test_flt(late, (float)fmod(0.5 * 36000.25 / BOX_RENDER_CAUSTICS_LOOP, 1.0));
}

void WaterCaustics_pack(void) {
    box_draws_tables_init();
    Water water = {
        .shallow_color = {60, 170, 160}, .deep_color = {8, 40, 64}, .depth = 3,
        .visibility = 2, .absorption = {255, 255, 255}
    };
    WaterCaustics caustics = { .strength = 0.75f, .scale = 2, .speed = 0.5f, .depth = 1.5f };
    /* The caustics words of a Water are the Fresnel words, which a Water ignores */
    test_int(ECS_SIZEOF(box_render_material_t), 272);
    test_int(offsetof(box_render_material_t, caustics_offset),
        offsetof(box_render_material_t, fresnel_color));
    test_int(offsetof(box_render_material_t, caustics_strength),
        offsetof(box_render_material_t, fresnel_strength));
    test_int(offsetof(box_render_material_t, caustics_phase),
        offsetof(box_render_material_t, fresnel_opacity));

    /* Without caustics the caustics words are 0 and the record is that of a plain Water */
    box_render_material_t plain, record;
    ecs_os_memset_t(&plain, 0x55, box_render_material_t);
    box_draws_material_pack(&plain, &(box_draw_material_t){
        .water = &water, .fade = 1, .time = 3 });
    test_flt(plain.caustics_strength, 0);
    test_flt(plain.caustics_frequency, 0);
    test_flt(plain.caustics_depth, 0);
    test_flt(plain.caustics_phase, 0);
    test_flt(plain.caustics_offset[0], 0);
    test_flt(plain.caustics_offset[1], 0);

    /* With them the record holds strength, frequency, depth, frame and phase */
    ecs_os_memset_t(&record, 0x55, box_render_material_t);
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &water, .caustics = &caustics, .caustics_frame = {3.5f, 60},
        .fade = 1, .time = 4 });
    test_flt(record.caustics_strength, 0.75f);
    test_flt(record.caustics_frequency, 0.5f);
    test_flt(record.caustics_depth, 1.5f);
    test_flt(record.caustics_offset[0], 3.5f);
    test_flt(record.caustics_offset[1], 60);
    test_flt(record.caustics_phase, box_draws_water_caustics_phase(0.5f, 4));

    /* The other words are those of the plain Water */
    size_t start = offsetof(box_render_material_t, caustics_offset);
    size_t end = offsetof(box_render_material_t, caustics_phase) + sizeof(float);
    test_int(ecs_os_memcmp(&record, &plain, (ecs_size_t)start), 0);
    test_int(ecs_os_memcmp((char*)&record + end, (char*)&plain + end,
        (ecs_size_t)(sizeof(box_render_material_t) - end)), 0);

    /* A Water ignores a Fresnel: the words hold its caustics */
    Fresnel fresnel = { .color = {255, 255, 255}, .strength = 2, .power = 3, .bias = 0.5f,
        .opacity = 1 };
    box_render_material_t with_fresnel;
    box_draws_material_pack(&with_fresnel, &(box_draw_material_t){
        .water = &water, .caustics = &caustics, .fresnel = &fresnel,
        .caustics_frame = {3.5f, 60}, .fade = 1, .time = 4 });
    test_int(ecs_os_memcmp_t(&with_fresnel, &record, box_render_material_t), 0);
    box_draws_material_pack(&with_fresnel, &(box_draw_material_t){
        .water = &water, .fresnel = &fresnel, .fade = 1, .time = 3 });
    test_int(ecs_os_memcmp_t(&with_fresnel, &plain, box_render_material_t), 0);

    /* A depth below the smallest is the smallest */
    caustics.depth = -1;
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &water, .caustics = &caustics, .fade = 1 });
    test_flt(record.caustics_depth, BOX_RENDER_CAUSTICS_MIN);
    caustics.depth = NAN;
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &water, .caustics = &caustics, .fade = 1 });
    test_flt(record.caustics_depth, BOX_RENDER_CAUSTICS_MIN);
    caustics.depth = 1.5f;

    /* A strength of 0, negative or broken packs as without them */
    float strengths[3] = { 0, -1, NAN };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        caustics.strength = strengths[i];
        box_draws_material_pack(&record, &(box_draw_material_t){
            .water = &water, .caustics = &caustics, .caustics_frame = {3.5f, 60},
            .fade = 1, .time = 3 });
        test_int(ecs_os_memcmp_t(&record, &plain, box_render_material_t), 0);
    }
    caustics.strength = 0.75f;

    /* Without Water they do nothing */
    box_render_material_t none;
    box_draws_material_pack(&none, &(box_draw_material_t){ .fade = 1 });
    box_draws_material_pack(&record, &(box_draw_material_t){
        .caustics = &caustics, .caustics_frame = {1, 2}, .fade = 1, .time = 4 });
    test_int(ecs_os_memcmp_t(&record, &none, box_render_material_t), 0);

    /* A transition of another component encodes the record with its caustics again */
    box_draw_transition_material_t target;
    box_render_material_t encoded;
    box_draws_material_pack(&record, &(box_draw_material_t){
        .water = &water, .caustics = &caustics, .caustics_frame = {3.5f, 60},
        .fade = 1, .time = 4 });
    box_draws_transition_target(&target, &(box_draw_material_t){
        .water = &water, .caustics = &caustics, .caustics_frame = {3.5f, 60},
        .fade = 1, .time = 4 }, BOX_DRAW_TRANSITION_TABLE);
    box_draws_transition_encode(&target, &encoded);
    test_int(ecs_os_memcmp_t(&encoded, &record, box_render_material_t), 0);
}

void WaterCaustics_record_flag(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_singleton_set(world, Scene, {.water = true});
    ecs_entity_t a = pond_new(world, 0);
    ecs_entity_t b = pond_new(world, 2);
    ecs_entity_t c = pond_new(world, 4);
    frame_dt(world, 1.0f / 60.0f);
    frame_dt(world, 1.0f / 60.0f);

    /* Water without WaterCaustics shares its slot and has no strength */
    uint32_t shared = id_of(world, a);
    test_assert(shared != 0 && shared != UINT32_MAX);
    test_uint(id_of(world, b), shared);
    test_flt(record_of(world, shared)->caustics_strength, 0);

    /* Still caustics (speed 0, no flow) share a slot of their own record */
    ecs_set(world, a, WaterCaustics, {.strength = 1, .scale = 1, .speed = 0, .depth = 2});
    ecs_set(world, b, WaterCaustics, {.strength = 1, .scale = 1, .speed = 0, .depth = 2});
    frame_dt(world, 1.0f / 60.0f);
    frame_dt(world, 1.0f / 60.0f);
    uint32_t still = id_of(world, a);
    test_assert(still != shared && still != 0 && still != UINT32_MAX);
    test_uint(id_of(world, b), still);
    test_uint(id_of(world, c), shared);
    test_flt(record_of(world, still)->caustics_strength, 1);
    test_flt(record_of(world, still)->caustics_frequency, 1);
    test_flt(record_of(world, still)->caustics_depth, 2);
    test_flt(record_of(world, still)->caustics_phase, 0);
    test_flt(record_of(world, shared)->caustics_strength, 0);

    /* A speed gives a private slot whose phase moves every frame */
    ecs_set(world, a, WaterCaustics, {.strength = 1, .scale = 1, .speed = 0.5f, .depth = 2});
    frame_dt(world, 1.0f / 60.0f);
    frame_dt(world, 1.0f / 60.0f);
    uint32_t own = id_of(world, a);
    test_assert(own != still && own != shared && own != 0 && own != UINT32_MAX);
    test_uint(id_of(world, b), still);
    box_render_draws_t *d = draws_of(world);
    float was = record_of(world, own)->caustics_phase;
    frame_dt(world, 0.1f);
    test_uint(id_of(world, a), own);
    float now = record_of(world, own)->caustics_phase;
    test_assert(fabsf(now - was) > 1e-4f);
    test_flt(now, box_draws_water_caustics_phase(0.5f, d->materials.time));

    /* A strength of 0 draws as without them: the record of a plain Water */
    ecs_set(world, b, WaterCaustics, {.strength = 0, .scale = 1, .speed = 0, .depth = 2});
    frame_dt(world, 1.0f / 60.0f);
    frame_dt(world, 1.0f / 60.0f);
    test_uint(id_of(world, b), id_of(world, c));

    /* Removing them shares the slot of the plain Water again */
    ecs_remove(world, a, WaterCaustics);
    frame_dt(world, 1.0f / 60.0f);
    frame_dt(world, 1.0f / 60.0f);
    test_uint(id_of(world, a), id_of(world, c));
    test_flt(record_of(world, id_of(world, a))->caustics_strength, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterCaustics_frame_follows_flow(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_singleton_set(world, Scene, {.water = true});
    ecs_entity_t a = pond_new(world, 0);
    ecs_entity_t b = pond_new(world, 2);
    ecs_set(world, a, WaterCaustics, {.strength = 1, .scale = 2, .speed = 0, .depth = 2});
    ecs_set(world, b, WaterCaustics, {.strength = 1, .scale = 2, .speed = 0, .depth = 2});
    frame_dt(world, 1.0f / 60.0f);
    frame_dt(world, 1.0f / 60.0f);
    test_uint(id_of(world, a), id_of(world, b));
    box_render_draws_t *d = draws_of(world);
    float out[2];
    test_bool(box_draws_water_caustics_find(&d->materials, a, out), false);
    test_flt(out[0], 0);

    /* A WaterFlow gives a private slot whose frame follows the flow in cells */
    ecs_set(world, a, WaterFlow, {{1.5f, -0.5f}});
    frame_dt(world, 1.0f / 60.0f);
    frame_dt(world, 1.0f / 60.0f);
    uint32_t own = id_of(world, a);
    test_assert(own != id_of(world, b) && own != 0 && own != UINT32_MAX);
    test_bool(box_draws_water_caustics_find(&d->materials, a, out), true);
    test_bool(box_draws_water_caustics_find(&d->materials, b, out), false);

    int32_t i;
    for (i = 0; i < 5; i ++) {
        float before[2];
        box_draws_water_caustics_find(&d->materials, a, before);
        frame_dt(world, 0.1f);
        box_draws_water_caustics_find(&d->materials, a, out);
        double dt = 0.1;
        test_assert(frame_distance(out[0], before[0] + 0.75 * dt) < 1e-4);
        test_assert(frame_distance(out[1], before[1] - 0.25 * dt) < 1e-4);
        test_flt(record_of(world, own)->caustics_offset[0], out[0]);
        test_flt(record_of(world, own)->caustics_offset[1], out[1]);
        test_assert(out[0] >= 0 && out[0] < (float)BOX_RENDER_CAUSTICS_PERIOD);
        test_assert(out[1] >= 0 && out[1] < (float)BOX_RENDER_CAUSTICS_PERIOD);
    }

    /* A faster flow speeds the frame up from where it is, without a jump */
    ecs_set(world, a, WaterFlow, {{-3, 0}});
    frame_dt(world, 0.1f);
    float before[2];
    box_draws_water_caustics_find(&d->materials, a, before);
    frame_dt(world, 0.1f);
    box_draws_water_caustics_find(&d->materials, a, out);
    test_assert(frame_distance(out[0], before[0] - 1.5 * 0.1) < 1e-4);
    test_assert(frame_distance(out[1], before[1]) < 1e-4);

    /* The frame wraps: a long frame moves it past the period and back into 0..period */
    ecs_set(world, a, WaterFlow, {{64, 0}});
    frame_dt(world, 0.1f);
    box_draws_water_caustics_find(&d->materials, a, before);
    frame_dt(world, 2.5f);
    box_draws_water_caustics_find(&d->materials, a, out);
    test_assert(out[0] >= 0 && out[0] < (float)BOX_RENDER_CAUSTICS_PERIOD);
    test_assert(frame_distance(out[0], before[0] + 32 * 2.5) < 1e-3);

    /* Without the flow the frame is forgotten and the shape shares the still record of b */
    ecs_remove(world, a, WaterFlow);
    frame_dt(world, 1.0f / 60.0f);
    frame_dt(world, 1.0f / 60.0f);
    test_bool(box_draws_water_caustics_find(&d->materials, a, out), false);
    test_uint(id_of(world, a), id_of(world, b));
    test_flt(record_of(world, id_of(world, a))->caustics_offset[0], 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterCaustics_without_water(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_singleton_set(world, Scene, {.water = true});

    /* WaterCaustics and WaterFlow on a shape without Water do nothing */
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Box, {1, 1, 1});
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Rgb, {40, 80, 120});
    ecs_add(world, a, WaterCaustics);
    ecs_set(world, a, WaterFlow, {{1, 0}});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_set(world, b, Position, {2, 0, 0});
    ecs_set(world, b, Rgb, {40, 80, 120});
    frame_dt(world, 1.0f / 60.0f);
    frame_dt(world, 1.0f / 60.0f);
    test_uint(id_of(world, a), id_of(world, b));
    test_flt(record_of(world, id_of(world, a))->caustics_strength, 0);
    box_render_draws_t *d = draws_of(world);
    float out[2];
    test_bool(box_draws_water_caustics_find(&d->materials, a, out), false);

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterCaustics_water_off(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t a = pond_new(world, 0);
    ecs_entity_t b = pond_new(world, 2);
    ecs_add(world, a, WaterCaustics);
    ecs_set(world, a, WaterFlow, {{1, 0}});
    frame_dt(world, 1.0f / 60.0f);
    frame_dt(world, 1.0f / 60.0f);

    /* A scene without Scene.water packs neither the Water nor its caustics */
    test_uint(id_of(world, a), id_of(world, b));
    test_flt(record_of(world, id_of(world, a))->water_depth, 0);
    test_flt(record_of(world, id_of(world, a))->caustics_strength, 0);
    box_render_draws_t *d = draws_of(world);
    float out[2];
    test_bool(box_draws_water_caustics_find(&d->materials, a, out), false);

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterCaustics_prefab_instances(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_singleton_set(world, Scene, {.water = true});
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, Box, {1, 1, 1});
    ecs_add(world, prefab, Water);
    ecs_set(world, prefab, WaterCaustics, {.strength = 0.5f, .scale = 1, .speed = 0, .depth = 3});
    ecs_set(world, prefab, WaterFlow, {{0, 2}});
    ecs_entity_t i1 = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, i1, Position, {0, 0, 0});
    ecs_entity_t i2 = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, i2, Position, {2, 0, 0});
    frame_dt(world, 1.0f / 60.0f);
    frame_dt(world, 1.0f / 60.0f);

    /* The instances share the record of the prefab and its frame */
    uint32_t id = id_of(world, prefab);
    test_assert(id != 0 && id != UINT32_MAX);
    test_uint(id_of(world, i1), id);
    test_uint(id_of(world, i2), id);
    test_flt(record_of(world, id)->caustics_strength, 0.5f);
    test_flt(record_of(world, id)->caustics_depth, 3);
    box_render_draws_t *d = draws_of(world);
    float before[2], out[2];
    test_bool(box_draws_water_caustics_find(&d->materials, prefab, before), true);

    /* Its frame moves with the flow */
    frame_dt(world, 0.1f);
    box_draws_water_caustics_find(&d->materials, prefab, out);
    test_assert(frame_distance(out[1], before[1] + 0.2) < 1e-4);
    test_flt(record_of(world, id)->caustics_offset[1], out[1]);

    ecs_fini(world);
    box_render_null_free(null);
}
