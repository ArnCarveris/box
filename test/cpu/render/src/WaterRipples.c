/**
 * @file WaterRipples.c
 * @brief Tests for WaterRipples and RippleSource on the main thread: their reflection and
 * defaults, the box.material.ripple_at and foam_at script functions, the foam terms, the fields
 * the extract appends to the packet (window, steps, sources, foam) and the velocity of the sources relative to the water.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include <math.h>

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
    ecs_singleton_set(world, Scene, {.water = true});
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

/** Create a pond: a Water with WaterRipples of 32 world units and 256 cells at the origin.
 *
 * @param world The world
 * @return The pond
 */
static ecs_entity_t pond_new(
    ecs_world_t *world)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_add(world, e, Water);
    ecs_set(world, e, WaterRipples, {.size = 32, .resolution = 256, .wave_speed = 2,
        .damping = 0.5f, .strength = 1});

    return e;
}

/** Get the field of a Water in the last packet.
 *
 * @param null The backend
 * @param water The Water entity
 * @return The record, NULL when the packet has none
 */
static const box_render_water_ripples_t* field_of(
    const box_render_null_t *null,
    ecs_entity_t water)
{
    const box_render_water_ripples_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.water_ripples) && !result; i ++) {
        const box_render_water_ripples_t *r = ecs_vec_get_t(
            &null->last.water_ripples, box_render_water_ripples_t, i);
        if (r->water == water) {
            result = r;
        }
    }

    return result;
}

/** Get a source of a field in the last packet.
 *
 * @param null The backend
 * @param rec The field
 * @param index Index of the source in the field
 * @return The source
 */
static const box_render_ripple_source_t* source_of(
    const box_render_null_t *null,
    const box_render_water_ripples_t *rec,
    int32_t index)
{
    test_assert(index < rec->source_count);

    return ecs_vec_get_t(&null->last.ripple_sources, box_render_ripple_source_t,
        rec->source_first + index);
}

void WaterRipples_defaults(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, WaterRipples);
    ecs_add(world, e, RippleSource);
    const WaterRipples *r = ecs_get(world, e, WaterRipples);
    test_assert(r != NULL);
    test_flt(r->size, BOX_MATERIAL_RIPPLES_SIZE);
    test_flt(r->size, 32);
    test_int(r->resolution, BOX_MATERIAL_RIPPLES_RESOLUTION);
    test_int(r->resolution, 256);
    test_uint(r->follow, 0);
    test_flt(r->wave_speed, 2);
    test_flt(r->damping, 0.5f);
    test_flt(r->strength, 1);
    test_flt(r->foam_decay, BOX_MATERIAL_RIPPLES_FOAM_DECAY);
    test_flt(r->foam_decay, 0.4f);
    test_flt(r->foam_spread, BOX_MATERIAL_RIPPLES_FOAM_SPREAD);
    test_flt(r->foam_spread, 0.02f);
    test_flt(r->crest_foam, 0);

    const RippleSource *s = ecs_get(world, e, RippleSource);
    test_assert(s != NULL);
    test_flt(s->extents[0], 0.5f);
    test_flt(s->extents[1], 0.5f);
    test_flt(s->strength, 1);
    test_flt(s->foam, 0);

    ecs_fini(world);
}

void WaterRipples_reflection(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "ripples",
        "using box.material\n"
        "boat {}\n"
        "pond {\n"
        "  Water: {}\n"
        "  WaterRipples: {size: 12, resolution: 128, follow: boat, wave_speed: 1.5,"
        " damping: 0.25, strength: 2, foam_decay: 0.6, foam_spread: 0.05, crest_foam: 1.5}\n"
        "}\n"
        "boat {\n"
        "  RippleSource: {extents: {0.4, 1.2}, strength: 3, foam: 0.8}\n"
        "}\n"
        "rock {\n"
        "  RippleSource: {}\n"
        "}\n", NULL) == 0);

    ecs_entity_t pond = ecs_lookup(world, "pond");
    ecs_entity_t boat = ecs_lookup(world, "boat");
    const WaterRipples *r = ecs_get(world, pond, WaterRipples);
    test_assert(r != NULL);
    test_flt(r->size, 12);
    test_int(r->resolution, 128);
    test_uint(r->follow, boat);
    test_flt(r->wave_speed, 1.5f);
    test_flt(r->damping, 0.25f);
    test_flt(r->strength, 2);
    test_flt(r->foam_decay, 0.6f);
    test_flt(r->foam_spread, 0.05f);
    test_flt(r->crest_foam, 1.5f);

    const RippleSource *s = ecs_get(world, boat, RippleSource);
    test_assert(s != NULL);
    test_flt(s->extents[0], 0.4f);
    test_flt(s->extents[1], 1.2f);
    test_flt(s->strength, 3);
    test_flt(s->foam, 0.8f);

    s = ecs_get(world, ecs_lookup(world, "rock"), RippleSource);
    test_assert(s != NULL);
    test_flt(s->extents[0], 0.5f);
    test_flt(s->extents[1], 0.5f);
    test_flt(s->strength, 1);
    test_flt(s->foam, 0);

    const EcsStruct *st = ecs_get(world, ecs_id(WaterRipples), EcsStruct);
    test_assert(st != NULL);
    test_int(ecs_vec_count(&st->members), 9);
    st = ecs_get(world, ecs_id(RippleSource), EcsStruct);
    test_assert(st != NULL);
    test_int(ecs_vec_count(&st->members), 3);

    ecs_fini(world);
}

void WaterRipples_function_registered(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fn = ecs_lookup(world, "box.material.ripple_at");
    test_assert(fn != 0);
    test_assert(ecs_has(world, fn, EcsScriptFunction));

    test_assert(ecs_script_run(world, "ripple",
        "using box.material\n"
        "pond {\n"
        "  Water: {}\n"
        "  WaterRipples: {}\n"
        "}\n"
        "const ok: bool = box.material.ripple_at(pond, {1, 0.5, -2}, 0.75, 0.2)\n", NULL) == 0);

    ecs_vec_t *queue = box_material_ripples_queue(world);
    test_assert(queue != NULL);
    test_int(ecs_vec_count(queue), 1);
    const box_material_ripple_t *q = ecs_vec_first_t(queue, box_material_ripple_t);
    test_uint(q->water, ecs_lookup(world, "pond"));
    test_flt(q->position[0], 1);
    test_flt(q->position[1], 0.5f);
    test_flt(q->position[2], -2);
    test_flt(q->radius, 0.75f);
    test_flt(q->strength, 0.2f);
    test_flt(q->foam, 0);

    ecs_fini(world);
}

void WaterRipples_function_checks_arguments(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t pond = ecs_new(world);
    ecs_add(world, pond, Water);
    ecs_add(world, pond, WaterRipples);
    ecs_entity_t plain = ecs_new(world);
    ecs_add(world, plain, Water);
    ecs_entity_t dry = ecs_new(world);
    ecs_add(world, dry, WaterRipples);
    float at[3] = {0, 0, 0};
    float nan_at[3] = {NAN, 0, 0};

    int level = ecs_log_set_level(-4);
    test_bool(box_material_ripple_at(world, 0, at, 1, 0.1f), false);
    test_bool(box_material_ripple_at(world, plain, at, 1, 0.1f), false);
    test_bool(box_material_ripple_at(world, dry, at, 1, 0.1f), false);
    test_bool(box_material_ripple_at(world, pond, nan_at, 1, 0.1f), false);
    test_bool(box_material_ripple_at(world, pond, at, 0, 0.1f), false);
    test_bool(box_material_ripple_at(world, pond, at, -1, 0.1f), false);
    test_bool(box_material_ripple_at(world, pond, at, INFINITY, 0.1f), false);
    test_bool(box_material_ripple_at(world, pond, at, 1, NAN), false);
    ecs_log_set_level(level);
    test_int(ecs_vec_count(box_material_ripples_queue(world)), 0);

    test_bool(box_material_ripple_at(world, pond, at, 1, 0.1f), true);
    test_bool(box_material_ripple_at(world, pond, at, 1, -0.1f), true);
    test_int(ecs_vec_count(box_material_ripples_queue(world)), 2);

    ecs_fini(world);
}

void WaterRipples_resolution_rounded(void) {
    test_int(box_render_ripples_resolution(256), 256);
    test_int(box_render_ripples_resolution(64), 64);
    test_int(box_render_ripples_resolution(1024), 1024);
    test_int(box_render_ripples_resolution(100), 128);
    test_int(box_render_ripples_resolution(95), 64);
    test_int(box_render_ripples_resolution(96), 128);
    test_int(box_render_ripples_resolution(300), 256);
    test_int(box_render_ripples_resolution(400), 512);
    test_int(box_render_ripples_resolution(0), 64);
    test_int(box_render_ripples_resolution(-5), 64);
    test_int(box_render_ripples_resolution(1 << 20), 1024);
}

void WaterRipples_relative_velocity(void) {
    WaterFlow flow = {.velocity = {1.5f, -0.5f}};
    float still[2] = {0, 0}, along[2] = {1.5f, -0.5f}, across[2] = {0, 2}, out[2];

    /* A source held still on flowing water moves through it */
    float speed = box_render_ripples_relative(still, &flow, out);
    test_assert(speed > 0);
    test_flt(out[0], -1.5f);
    test_flt(out[1], 0.5f);
    test_flt(speed, sqrtf(1.5f * 1.5f + 0.5f * 0.5f));

    /* A source that flows with the water does not */
    speed = box_render_ripples_relative(along, &flow, out);
    test_flt(speed, 0);

    speed = box_render_ripples_relative(across, &flow, out);
    test_flt(out[0], -1.5f);
    test_flt(out[1], 2.5f);

    /* Still water */
    speed = box_render_ripples_relative(across, NULL, out);
    test_flt(speed, 2);
    speed = box_render_ripples_relative(still, NULL, out);
    test_flt(speed, 0);
}

void WaterRipples_source_relative_to_flow(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t pond = pond_new(world);
    ecs_set(world, pond, WaterFlow, {.velocity = {1, 0}});

    /* A rock held still and a log that flows with the water */
    ecs_entity_t rock = ecs_new(world);
    ecs_set(world, rock, Position, {2, 0, 0});
    ecs_set(world, rock, RippleSource, {.extents = {0.5f, 0.5f}, .strength = 1});
    ecs_entity_t log = ecs_new(world);
    ecs_set(world, log, Position, {-2, 0, 0});
    ecs_set(world, log, RippleSource, {.extents = {0.5f, 0.5f}, .strength = 1});

    float x = -2;
    int32_t i;
    for (i = 0; i < 4; i ++) {
        frame_dt(world, 1.0f / 60);
        x += 1.0f / 60;
        ecs_set(world, log, Position, {x, 0, 0});
    }

    float velocity[3];
    test_bool(box_render_ripples_velocity(world, rock, velocity), true);
    test_flt(velocity[0], 0);
    test_flt(velocity[2], 0);
    test_bool(box_render_ripples_velocity(world, log, velocity), true);
    test_assert(fabsf(velocity[0] - 1) < 1e-3f);
    test_assert(fabsf(velocity[2]) < 1e-3f);

    /* Only the rock pushes the water, by its speed of 1 relative to it */
    const box_render_water_ripples_t *rec = field_of(null, pond);
    test_assert(rec != NULL);
    test_int(rec->steps, 2);
    test_int(rec->source_count, 1);
    const box_render_ripple_source_t *s = source_of(null, rec, 0);
    test_uint(s->once, 0);
    test_assert(s->amount < 0);
    test_flt(s->amount, -BOX_RENDER_RIPPLE_PUSH * (float)BOX_RENDER_RIPPLE_STEP);

    /* It sits at x = 2 in the world, which drifts through the water frame */
    float cell = rec->cell;
    test_flt(cell, 0.125f);
    test_assert(fabsf(rec->corner[0] + s->to[0] * cell - 2) < 1e-3f);
    test_assert(s->to[0] < s->from[0]);
    test_assert(fabsf((s->from[0] - s->to[0]) * cell - 1.0f / 60) < 1e-4f);
    test_assert(fabsf(s->extents[0] - 4) < 1e-4f);

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterRipples_window_whole_cells(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t pond = pond_new(world);
    ecs_entity_t boat = ecs_new(world);
    ecs_set(world, boat, Position, {0, 0, 0});
    WaterRipples *r = ecs_ensure(world, pond, WaterRipples);
    r->follow = boat;
    ecs_modified(world, pond, WaterRipples);

    frame_dt(world, 1.0f / 60);
    const box_render_water_ripples_t *rec = field_of(null, pond);
    test_assert(rec != NULL);
    test_bool(rec->reset, true);
    test_int(rec->resolution, 256);
    test_flt(rec->cell, 0.125f);
    test_flt(rec->corner[0], -16);
    test_flt(rec->corner[1], -16);
    test_int(rec->origin[0], 128);
    test_int(rec->origin[1], 128);

    /* Less than a cell keeps the window */
    ecs_set(world, boat, Position, {0.1f, 0, 0.05f});
    frame_dt(world, 1.0f / 60);
    rec = field_of(null, pond);
    test_bool(rec->reset, false);
    test_int(rec->shift[0], 0);
    test_int(rec->shift[1], 0);
    test_flt(rec->corner[0], -16);
    test_flt(rec->corner[1], -16);

    /* 2.4 cells along X and -1.2 along Z move it by whole cells */
    ecs_set(world, boat, Position, {0.3f, 0, -0.15f});
    frame_dt(world, 1.0f / 60);
    rec = field_of(null, pond);
    test_bool(rec->reset, false);
    test_int(rec->shift[0], 2);
    test_int(rec->shift[1], -2);
    test_flt(rec->corner[0], -15.75f);
    test_flt(rec->corner[1], -16.25f);
    test_int(rec->origin[0], 130);
    test_int(rec->origin[1], 126);

    /* A jump beyond the window clears all of it */
    ecs_set(world, boat, Position, {100, 0, 0});
    frame_dt(world, 1.0f / 60);
    rec = field_of(null, pond);
    test_int(rec->shift[0], 256);
    test_flt(rec->corner[0], 100 - 16);

    int64_t origin[2];
    box_render_ripples_window(0.3, -0.15, 0.125, 256, origin);
    test_int(origin[0], 2 - 128);
    test_int(origin[1], -2 - 128);

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterRipples_window_in_water_frame(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t pond = pond_new(world);
    ecs_set(world, pond, WaterFlow, {.velocity = {0.5f, 0}});

    /* The window stays on the pond while the water frame drifts: its corner moves by whole cells
     * relative to the frame, so it stays within a cell of -16 in the world */
    int32_t i;
    for (i = 0; i < 120; i ++) {
        frame_dt(world, 1.0f / 60);
        const box_render_water_ripples_t *rec = field_of(null, pond);
        test_assert(rec != NULL);
        test_assert(rec->corner[0] <= -16 + 1e-4f);
        test_assert(rec->corner[0] > -16 - 0.125f - 1e-4f);
        test_assert(rec->shift[0] == 0 || rec->shift[0] == -1 || i == 0);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterRipples_fixed_steps(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t pond = pond_new(world);

    frame_dt(world, 1.0f / 60);
    test_int(field_of(null, pond)->steps, 2);
    frame_dt(world, 1.0f / 120);
    test_int(field_of(null, pond)->steps, 1);
    frame_dt(world, 1.0f / 240);
    test_int(field_of(null, pond)->steps, 0);
    frame_dt(world, 1.0f / 240);
    test_int(field_of(null, pond)->steps, 1);

    /* A long frame takes the most substeps and drops the rest of its time */
    frame_dt(world, 1);
    test_int(field_of(null, pond)->steps, BOX_RENDER_RIPPLE_SUBSTEPS);
    frame_dt(world, 1.0f / 120);
    test_int(field_of(null, pond)->steps, 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterRipples_wave_speed_clamped(void) {
    WaterRipples r = {.size = 32, .resolution = 256, .wave_speed = 2, .damping = 0.5f,
        .strength = 1};
    float courant2, keep;
    float speed = box_render_ripples_wave(&r, 0.125f, &courant2, &keep);
    test_flt(speed, 2);
    float c = 2 * (float)BOX_RENDER_RIPPLE_STEP / 0.125f;
    test_flt(courant2, c * c);
    test_assert(fabsf(keep - (float)pow(0.5, BOX_RENDER_RIPPLE_STEP)) < 1e-6f);

    /* Too fast for small cells: clamped to the Courant limit */
    r.wave_speed = 100;
    speed = box_render_ripples_wave(&r, 1.0f / 32, &courant2, &keep);
    test_assert(fabsf(speed - BOX_RENDER_RIPPLE_COURANT / 32.0f /
        (float)BOX_RENDER_RIPPLE_STEP) < 1e-3f);
    test_assert(courant2 <= BOX_RENDER_RIPPLE_COURANT * BOX_RENDER_RIPPLE_COURANT + 1e-6f);

    /* Undamped, fully damped, invalid */
    r.wave_speed = 2;
    r.damping = 0;
    box_render_ripples_wave(&r, 0.125f, &courant2, &keep);
    test_flt(keep, 1);
    r.damping = 1;
    box_render_ripples_wave(&r, 0.125f, &courant2, &keep);
    test_flt(keep, 0);
    r.damping = NAN;
    r.wave_speed = -1;
    speed = box_render_ripples_wave(&r, 0.125f, &courant2, &keep);
    test_flt(speed, 0);
    test_flt(courant2, 0);
    test_flt(keep, 1);
}

void WaterRipples_ripple_at_next_step(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t pond = pond_new(world);
    frame_dt(world, 1.0f / 60);

    float at[3] = {2, 0, -1};
    test_bool(box_material_ripple_at(world, pond, at, 0.5f, 0.1f), true);

    /* A frame too short for a step keeps it */
    frame_dt(world, 1.0f / 480);
    const box_render_water_ripples_t *rec = field_of(null, pond);
    test_int(rec->steps, 0);
    test_int(rec->source_count, 0);
    test_int(ecs_vec_count(box_material_ripples_queue(world)), 1);

    frame_dt(world, 1.0f / 60);
    rec = field_of(null, pond);
    test_assert(rec->steps > 0);
    test_int(rec->source_count, 1);
    const box_render_ripple_source_t *s = source_of(null, rec, 0);
    test_uint(s->once, 1);
    test_flt(s->amount, -0.1f);
    test_flt(s->extents[0], 4);
    test_flt(s->extents[1], 4);
    test_assert(fabsf(rec->corner[0] + s->to[0] * rec->cell - 2) < 1e-4f);
    test_assert(fabsf(rec->corner[1] + s->to[1] * rec->cell + 1) < 1e-4f);
    test_int(ecs_vec_count(box_material_ripples_queue(world)), 0);

    frame_dt(world, 1.0f / 60);
    test_int(field_of(null, pond)->source_count, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterRipples_no_field(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t zero = pond_new(world);
    WaterRipples *r = ecs_ensure(world, zero, WaterRipples);
    r->size = 0;
    ecs_modified(world, zero, WaterRipples);

    ecs_entity_t disabled = pond_new(world);
    ecs_enable(world, disabled, false);

    ecs_entity_t dry = ecs_new(world);
    ecs_set(world, dry, Position, {0, 0, 0});
    ecs_add(world, dry, WaterRipples);

    frame_dt(world, 1.0f / 60);
    test_int(ecs_vec_count(&null->last.water_ripples), 0);
    test_int(ecs_vec_count(&null->last.ripple_sources), 0);

    /* Without Scene.water the Water does nothing */
    ecs_enable(world, disabled, true);
    ecs_singleton_set(world, Scene, {.water = false});
    frame_dt(world, 1.0f / 60);
    test_int(ecs_vec_count(&null->last.water_ripples), 0);

    ecs_singleton_set(world, Scene, {.water = true});
    frame_dt(world, 1.0f / 60);
    test_int(ecs_vec_count(&null->last.water_ripples), 1);
    test_assert(field_of(null, disabled) != NULL);

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterRipples_one_field_per_scene(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t a = pond_new(world);
    ecs_entity_t b = pond_new(world);

    int level = ecs_log_set_level(-4);
    frame_dt(world, 1.0f / 60);
    frame_dt(world, 1.0f / 60);
    ecs_log_set_level(level);
    test_int(ecs_vec_count(&null->last.water_ripples), 1);
    test_assert(field_of(null, a) != NULL || field_of(null, b) != NULL);

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterRipples_foam_function_registered(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t fn = ecs_lookup(world, "box.material.foam_at");
    test_assert(fn != 0);
    test_assert(ecs_has(world, fn, EcsScriptFunction));

    test_assert(ecs_script_run(world, "foam",
        "using box.material\n"
        "pond {\n"
        "  Water: {}\n"
        "  WaterRipples: {}\n"
        "}\n"
        "const ok: bool = box.material.foam_at(pond, {-1, 0, 3}, 1.5, 0.6)\n", NULL) == 0);

    ecs_vec_t *queue = box_material_ripples_queue(world);
    test_assert(queue != NULL);
    test_int(ecs_vec_count(queue), 1);
    const box_material_ripple_t *q = ecs_vec_first_t(queue, box_material_ripple_t);
    test_uint(q->water, ecs_lookup(world, "pond"));
    test_flt(q->position[0], -1);
    test_flt(q->position[1], 0);
    test_flt(q->position[2], 3);
    test_flt(q->radius, 1.5f);
    test_flt(q->strength, 0);
    test_flt(q->foam, 0.6f);

    ecs_fini(world);
}

void WaterRipples_foam_function_checks_arguments(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t pond = ecs_new(world);
    ecs_add(world, pond, Water);
    ecs_add(world, pond, WaterRipples);
    ecs_entity_t plain = ecs_new(world);
    ecs_add(world, plain, Water);
    float at[3] = {0, 0, 0};
    float nan_at[3] = {0, NAN, 0};

    int level = ecs_log_set_level(-4);
    test_bool(box_material_foam_at(world, 0, at, 1, 0.5f), false);
    test_bool(box_material_foam_at(world, plain, at, 1, 0.5f), false);
    test_bool(box_material_foam_at(world, pond, nan_at, 1, 0.5f), false);
    test_bool(box_material_foam_at(world, pond, at, 0, 0.5f), false);
    test_bool(box_material_foam_at(world, pond, at, 1, NAN), false);
    test_bool(box_material_foam_at(world, pond, at, 1, INFINITY), false);
    ecs_log_set_level(level);
    test_int(ecs_vec_count(box_material_ripples_queue(world)), 0);

    test_bool(box_material_foam_at(world, pond, at, 1, 0.5f), true);
    test_bool(box_material_foam_at(world, pond, at, 1, -0.5f), true);
    test_int(ecs_vec_count(box_material_ripples_queue(world)), 2);

    ecs_fini(world);
}

void WaterRipples_foam_terms(void) {
    WaterRipples r = {.size = 32, .resolution = 256, .wave_speed = 2, .damping = 0.5f,
        .strength = 1, .foam_decay = 0.5f, .foam_spread = 0.05f, .crest_foam = 3};
    float spread, keep, crest;
    float dt = (float)BOX_RENDER_RIPPLE_STEP;
    box_render_ripples_foam(&r, 0.125f, &spread, &keep, &crest);
    test_assert(fabsf(spread - 0.05f * dt / (0.125f * 0.125f)) < 1e-6f);
    test_assert(fabsf(keep - (float)pow(0.5, BOX_RENDER_RIPPLE_STEP)) < 1e-6f);
    test_assert(fabsf(crest - 3 * dt) < 1e-6f);

    /* Too much spread for small cells: clamped so the steps stay stable */
    r.foam_spread = 100;
    box_render_ripples_foam(&r, 1.0f / 32, &spread, &keep, &crest);
    test_flt(spread, BOX_RENDER_RIPPLE_FOAM_SPREAD_MAX);

    /* No decay, full decay, invalid values */
    r.foam_decay = 0;
    box_render_ripples_foam(&r, 0.125f, &spread, &keep, &crest);
    test_flt(keep, 1);
    r.foam_decay = 1;
    box_render_ripples_foam(&r, 0.125f, &spread, &keep, &crest);
    test_flt(keep, 0);
    r.foam_decay = NAN;
    r.foam_spread = -1;
    r.crest_foam = INFINITY;
    box_render_ripples_foam(&r, 0.125f, &spread, &keep, &crest);
    test_flt(spread, 0);
    test_flt(keep, 1);
    test_flt(crest, 0);
    r.foam_spread = 0.5f;
    r.crest_foam = -2;
    box_render_ripples_foam(&r, 0, &spread, &keep, &crest);
    test_flt(spread, 0);
    test_flt(crest, 0);
}

void WaterRipples_foam_in_record(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t pond = pond_new(world);
    WaterRipples *r = ecs_ensure(world, pond, WaterRipples);
    r->foam_decay = 0.25f;
    r->foam_spread = 0.04f;
    r->crest_foam = 2;
    ecs_modified(world, pond, WaterRipples);
    frame_dt(world, 1.0f / 60);

    const box_render_water_ripples_t *rec = field_of(null, pond);
    test_assert(rec != NULL);
    float spread, keep, crest;
    box_render_ripples_foam(ecs_get(world, pond, WaterRipples), rec->cell, &spread, &keep,
        &crest);
    test_flt(rec->foam_spread, spread);
    test_flt(rec->foam_keep, keep);
    test_flt(rec->crest, crest);
    test_assert(rec->foam_spread > 0);
    test_assert(rec->foam_keep > 0 && rec->foam_keep < 1);
    test_assert(rec->crest > 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterRipples_source_foam(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t pond = pond_new(world);
    ecs_set(world, pond, WaterFlow, {.velocity = {1, 0}});

    /* Three rocks held still on the flowing water: one that only foams, one that pushes and
     * foams, one that does neither */
    ecs_entity_t foamy = ecs_new(world);
    ecs_set(world, foamy, Position, {2, 0, 0});
    ecs_set(world, foamy, RippleSource, {.extents = {0.5f, 0.5f}, .strength = 0, .foam = 0.5f});
    ecs_entity_t both = ecs_new(world);
    ecs_set(world, both, Position, {-2, 0, 0});
    ecs_set(world, both, RippleSource, {.extents = {0.5f, 0.5f}, .strength = 1, .foam = 1});
    ecs_entity_t idle = ecs_new(world);
    ecs_set(world, idle, Position, {0, 0, 2});
    ecs_set(world, idle, RippleSource, {.extents = {0.5f, 0.5f}, .strength = 0, .foam = 0});

    int32_t i;
    for (i = 0; i < 3; i ++) {
        frame_dt(world, 1.0f / 60);
    }

    const box_render_water_ripples_t *rec = field_of(null, pond);
    test_assert(rec != NULL);
    test_int(rec->steps, 2);
    test_int(rec->source_count, 2);

    /* The foam grows by its speed of 1 through the water */
    float dt = (float)BOX_RENDER_RIPPLE_STEP;
    const box_render_ripple_source_t *a = source_of(null, rec, 0);
    const box_render_ripple_source_t *b = source_of(null, rec, 1);
    if (fabsf(rec->corner[0] + a->to[0] * rec->cell - 2) > 1e-3f) {
        const box_render_ripple_source_t *t = a;
        a = b;
        b = t;
    }

    test_assert(fabsf(rec->corner[0] + a->to[0] * rec->cell - 2) < 1e-3f);
    test_uint(a->once, 0);
    test_flt(a->amount, 0);
    test_assert(fabsf(a->foam - 0.5f * BOX_RENDER_RIPPLE_FOAM_PUSH * dt) < 1e-6f);
    test_assert(b->amount < 0);
    test_assert(fabsf(b->foam - BOX_RENDER_RIPPLE_FOAM_PUSH * dt) < 1e-6f);

    /* A source without foam lays none down */
    ecs_set(world, both, RippleSource, {.extents = {0.5f, 0.5f}, .strength = 1});
    frame_dt(world, 1.0f / 60);
    rec = field_of(null, pond);
    test_int(rec->source_count, 2);
    for (i = 0; i < rec->source_count; i ++) {
        const box_render_ripple_source_t *s = source_of(null, rec, i);
        if (s->amount < 0) {
            test_flt(s->foam, 0);
        } else {
            test_assert(s->foam > 0);
        }
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void WaterRipples_foam_at_next_step(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t pond = pond_new(world);
    frame_dt(world, 1.0f / 60);

    float at[3] = {-3, 0, 1};
    test_bool(box_material_foam_at(world, pond, at, 0.75f, 0.6f), true);
    frame_dt(world, 1.0f / 60);
    const box_render_water_ripples_t *rec = field_of(null, pond);
    test_assert(rec->steps > 0);
    test_int(rec->source_count, 1);
    const box_render_ripple_source_t *s = source_of(null, rec, 0);
    test_uint(s->once, 1);
    test_flt(s->amount, 0);
    test_flt(s->foam, 0.6f);
    test_flt(s->extents[0], 6);
    test_assert(fabsf(rec->corner[0] + s->to[0] * rec->cell + 3) < 1e-4f);
    test_assert(fabsf(rec->corner[1] + s->to[1] * rec->cell - 1) < 1e-4f);
    test_int(ecs_vec_count(box_material_ripples_queue(world)), 0);

    /* A ripple_at carries no foam */
    test_bool(box_material_ripple_at(world, pond, at, 0.75f, 0.1f), true);
    frame_dt(world, 1.0f / 60);
    rec = field_of(null, pond);
    test_int(rec->source_count, 1);
    s = source_of(null, rec, 0);
    test_flt(s->amount, -0.1f);
    test_flt(s->foam, 0);

    ecs_fini(world);
    box_render_null_free(null);
}
