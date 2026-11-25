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

void WaterCaustics_without_water(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_singleton_set(world, Scene, {.shader_features.Extended.water = true});

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

