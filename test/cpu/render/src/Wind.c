/**
 * @file Wind.c
 * @brief Tests for the Wind singleton, its WindStatus and the WindSway material component: their
 * defaults and reflection, the wrapped double precision integration, the material records, the
 * batches and bounds of swaying shapes, and that nothing changes for the shapes without one.
 *
 * The extract cases render with the null backend at a fixed delta and read the batch keys, the
 * material table and the packets it received.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"
#include "draw.h"

/** Seconds every frame advances the world, a power of two so the clock is exact */
#define WIND_DELTA (1.0f / 64.0f)

/** Create a world that renders into the null backend with a view.
 *
 * @param null Receives the backend
 * @param view Receives the view
 * @return The world
 */
static ecs_world_t* wind_world(
    box_render_null_t **null,
    ecs_entity_t *view)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 2, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    *view = ecs_new(world);
    ecs_add(world, *view, View);
    View *v = ecs_get_mut(world, *view, View);
    v->camera = camera;
    v->culling = (CullSettings){.enabled = true, .bounds = true};
    ecs_modified(world, *view, View);

    return world;
}

/** Run a frame at the fixed delta and wait until the backend has its packet.
 *
 * @param world The world
 * @param null The backend
 * @return The packet the backend received
 */
static const box_render_packet_t* wind_frame(
    ecs_world_t *world,
    box_render_null_t *null)
{
    ecs_progress(world, WIND_DELTA);
    box_render_sync(world);

    return &null->last;
}

/** Add a unit box.
 *
 * @param world The world
 * @param x Its x position
 * @param y Its y position
 * @return The box
 */
static ecs_entity_t wind_box(
    ecs_world_t *world,
    float x,
    float y)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, y, 0});
    ecs_set(world, e, Box, {1, 1, 1});

    return e;
}

/** Get the batch of a shape as the extract keeps it.
 *
 * @param world The world
 * @param e The shape
 * @return Its batch
 */
static box_draw_batch_t* wind_batch(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const DrawSlot *slot = ecs_get(world, e, DrawSlot);
    test_assert(slot != NULL && slot->batch >= 0);

    return box_draws_batch(box_draws_state(world), slot->batch);
}

/** Find the opaque draw of the batch of a shape in the last packet.
 *
 * @param world The world
 * @param null The backend
 * @param e The shape
 * @return The draw
 */
static const box_render_batch_t* wind_draw(
    ecs_world_t *world,
    const box_render_null_t *null,
    ecs_entity_t e)
{
    const box_draw_batch_t *b = wind_batch(world, e);
    const box_render_batch_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.batches); i ++) {
        const box_render_batch_t *draw = ecs_vec_get_t(
            &null->last.batches, box_render_batch_t, i);
        if (draw->mesh == b->key.mesh && draw->view == -1 && draw->first_instance == b->first &&
            draw->instances == box_draws_store_handle(box_draws_batch_store(
                box_draws_state(world), b), b->page, BOX_RENDER_STREAM_TRANSFORM))
        {
            result = draw;
        }
    }

    test_assert(result != NULL);

    return result;
}

/** Get the material record of a shape as the extract packed it.
 *
 * @param world The world
 * @param e The shape
 * @return The record
 */
static const box_render_material_t* wind_record(
    ecs_world_t *world,
    ecs_entity_t e)
{
    box_render_draws_t *d = box_draws_state(world);
    const MaterialId *id = ecs_get(world, e, MaterialId);
    test_assert(id != NULL);
    test_assert(id->value < (uint32_t)ecs_vec_count(&d->materials.records));

    return ecs_vec_get_t(&d->materials.records, box_render_material_t, (int32_t)id->value);
}

/** Distance between two phases on a circle of a period.
 *
 * @param a The first phase
 * @param b The second phase
 * @param period The period
 * @return b - a wrapped to -period / 2..period / 2
 */
static double wind_step(
    double a,
    double b,
    double period)
{
    double d = b - a;
    if (d > period * 0.5) {
        d -= period;
    } else if (d < -period * 0.5) {
        d += period;
    }

    return d;
}

void Wind_defaults(void) {
    // atmosphere/wind
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, Wind);
    const Wind *w = ecs_get(world, e, Wind);
    test_assert(w != NULL);
    test_flt(w->direction[0], 1);
    test_flt(w->direction[1], 0);
    test_flt(w->speed, BOX_WIND_SPEED);
    test_flt(w->speed, 0);
    test_flt(w->gust, BOX_WIND_GUST);
    test_flt(w->gust, 0.5f);
    test_flt(w->gust_scale, BOX_WIND_GUST_SCALE);
    test_flt(w->gust_scale, 20);

    /* Adding the Wind adds its status at rest */
    const WindStatus *s = ecs_get(world, e, WindStatus);
    test_assert(s != NULL);
    test_assert(s->offset_x == 0.0);
    test_assert(s->offset_z == 0.0);
    test_assert(s->sway == 0.0);
    test_assert(s->time == 0.0);
    test_assert(s->clock == 0.0);

    /* Every member is reflected */
    test_assert(ecs_script_run(world, "wind",
        "using box.atmosphere\n"
        "gale { Wind: {direction: {0, 2}, speed: 5, gust: 0.25, gust_scale: 8} }\n"
        "breeze { Wind: {speed: 1} }\n", NULL) == 0);
    w = ecs_get(world, ecs_lookup(world, "gale"), Wind);
    test_assert(w != NULL);
    test_flt(w->direction[0], 0);
    test_flt(w->direction[1], 2);
    test_flt(w->speed, 5);
    test_flt(w->gust, 0.25f);
    test_flt(w->gust_scale, 8);
    w = ecs_get(world, ecs_lookup(world, "breeze"), Wind);
    test_flt(w->direction[0], 1);
    test_flt(w->speed, 1);
    test_flt(w->gust, 0.5f);
    test_flt(w->gust_scale, 20);
    test_assert(ecs_has(world, ecs_lookup(world, "breeze"), WindStatus));

    test_assert(ecs_lookup(world, "box.atmosphere.Wind") == ecs_id(Wind));
    test_assert(ecs_lookup(world, "box.atmosphere.WindStatus") == ecs_id(WindStatus));
    test_assert(ecs_lookup(world, "box.material.WindSway") == ecs_id(WindSway));

    ecs_fini(world);
}

void Wind_sway_defaults(void) {
    // materials/wind_sway
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, WindSway);
    const WindSway *s = ecs_get(world, added, WindSway);
    test_assert(s != NULL);
    test_flt(s->bend, 0);
    test_flt(s->height, BOX_MATERIAL_SWAY_HEIGHT);
    test_flt(s->height, 1);
    test_flt(s->base, 0);
    test_flt(s->flutter, 0);
    test_flt(s->flutter_frequency, BOX_MATERIAL_SWAY_FLUTTER_FREQUENCY);
    test_flt(s->flutter_frequency, 2);
    test_flt(s->flutter_reach, BOX_MATERIAL_SWAY_FLUTTER_REACH);
    test_flt(s->flutter_reach, 0.3f);
    test_bool(s->pivots, false);
    test_flt(s->branch, 0);
    test_flt(s->branch_frequency, BOX_MATERIAL_SWAY_BRANCH_FREQUENCY);
    test_flt(s->branch_frequency, 0.5f);
    test_bool(ecs_owns(world, added, MaterialId), true);

    test_assert(ecs_script_run(world, "sway",
        "using box.material\n"
        "tree { WindSway: {bend: 0.05, height: 4, base: -1, flutter: 0.1, "
            "flutter_frequency: 3, flutter_reach: 0.5} }\n"
        "grass { WindSway: {bend: 0.02} }\n"
        "oak { WindSway: {bend: 0.03, pivots: true, branch: 0.2, branch_frequency: 0.7} }\n",
        NULL) == 0);
    s = ecs_get(world, ecs_lookup(world, "tree"), WindSway);
    test_assert(s != NULL);
    test_flt(s->bend, 0.05f);
    test_flt(s->height, 4);
    test_flt(s->base, -1);
    test_flt(s->flutter, 0.1f);
    test_flt(s->flutter_frequency, 3);
    test_flt(s->flutter_reach, 0.5f);
    s = ecs_get(world, ecs_lookup(world, "grass"), WindSway);
    test_flt(s->bend, 0.02f);
    test_flt(s->height, 1);
    test_flt(s->flutter_frequency, 2);
    test_flt(s->flutter_reach, 0.3f);
    test_bool(s->pivots, false);
    s = ecs_get(world, ecs_lookup(world, "oak"), WindSway);
    test_assert(s != NULL);
    test_bool(s->pivots, true);
    test_flt(s->branch, 0.2f);
    test_flt(s->branch_frequency, 0.7f);
    test_flt(s->height, 1);

    /* Inherited by instances */
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, WindSway, {.bend = 0.25f, .height = 2});
    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    test_bool(ecs_owns(world, instance, WindSway), false);
    test_flt(ecs_get(world, instance, WindSway)->bend, 0.25f);
    test_flt(ecs_get(world, instance, WindSway)->height, 2);

    ecs_fini(world);
}

void Wind_resolve(void) {
    // atmosphere/wind
    Wind out;
    box_render_wind_resolve(&(Wind){
        .direction = {3, 4}, .speed = 2, .gust = 0.25f, .gust_scale = 10 }, &out);
    test_flt(out.direction[0], 0.6f);
    test_flt(out.direction[1], 0.8f);
    test_flt(out.speed, 2);
    test_flt(out.gust, 0.25f);
    test_flt(out.gust_scale, 10);

    box_render_wind_resolve(&(Wind){
        .direction = {0, 0}, .speed = -2, .gust = 3, .gust_scale = 0 }, &out);
    test_flt(out.direction[0], 1);
    test_flt(out.direction[1], 0);
    test_flt(out.speed, 0);
    test_flt(out.gust, 1);
    test_flt(out.gust_scale, BOX_RENDER_WIND_GUST_SCALE_MIN);

    box_render_wind_resolve(&(Wind){
        .direction = {NAN, 1}, .speed = INFINITY, .gust = NAN, .gust_scale = NAN }, &out);
    test_flt(out.direction[0], 1);
    test_flt(out.direction[1], 0);
    test_flt(out.speed, 0);
    test_flt(out.gust, 0);
    test_flt(out.gust_scale, BOX_WIND_GUST_SCALE);

    /* The sway gets faster with the speed, up to its largest frequency */
    test_assert(box_render_wind_sway_frequency(&(Wind){ .speed = 0 }) ==
        BOX_RENDER_WIND_SWAY_BASE);
    test_assert(box_render_wind_sway_frequency(&(Wind){ .speed = 4 }) >
        box_render_wind_sway_frequency(&(Wind){ .speed = 2 }));
    test_assert(box_render_wind_sway_frequency(&(Wind){ .speed = 1000 }) ==
        BOX_RENDER_WIND_SWAY_MAX);
}

/** The offset of the gust field and the sway phase advance by the speed and the frequency times
 * the delta, wrap to their periods and keep their precision after hours of frames */
void Wind_advance_wraps(void) {
    // atmosphere/wind
    Wind wind = { .direction = {0.6f, -0.8f}, .speed = 7.3f, .gust = 0.5f, .gust_scale = 3 };
    WindStatus s = {0};
    double period = BOX_WIND_GUST_PERIOD * 3.0;
    double dt = 1.0 / 60.0;
    double frequency = box_render_wind_sway_frequency(&wind);
    Wind resolved;
    box_render_wind_resolve(&wind, &resolved);
    double vx = (double)resolved.direction[0] * (double)resolved.speed;
    double vz = (double)resolved.direction[1] * (double)resolved.speed;
    int32_t f;

    /* Two hours of frames */
    for (f = 0; f < 2 * 3600 * 60; f ++) {
        WindStatus was = s;
        box_render_wind_advance(&wind, dt, &s);
        test_assert(s.offset_x >= 0.0 && s.offset_x < period);
        test_assert(s.offset_z >= 0.0 && s.offset_z < period);
        test_assert(s.sway >= 0.0 && s.sway < 1.0);
        if (f % 997 == 0 || f > 2 * 3600 * 60 - 120) {
            test_assert(fabs(wind_step(was.offset_x, s.offset_x, period) - vx * dt) < 1e-9);
            test_assert(fabs(wind_step(was.offset_z, s.offset_z, period) - vz * dt) < 1e-9);
            test_assert(fabs(wind_step(was.sway, s.sway, 1.0) - frequency * dt) < 1e-9);
        }
    }

    test_assert(fabs(s.time - 2.0 * 3600.0) < 1e-6);

    /* A delta that is not positive and finite advances nothing */
    WindStatus kept = s;
    box_render_wind_advance(&wind, 0, &s);
    box_render_wind_advance(&wind, -1, &s);
    box_render_wind_advance(&wind, NAN, &s);
    test_assert(!memcmp(&kept, &s, sizeof(s)));

    /* A broken status starts over at 0 */
    s.offset_x = NAN;
    s.sway = INFINITY;
    box_render_wind_advance(&wind, dt, &s);
    test_assert(s.offset_x >= 0.0 && s.offset_x < period);
    test_assert(s.sway >= 0.0 && s.sway < 1.0);
}

/** A change of the speed changes how fast the phases advance, never where they are: the frame
 * after the change moves them by the new rates times the delta, where speed times time would
 * jump */
void Wind_speed_change_continuous(void) {
    // atmosphere/wind
    Wind wind = { .direction = {1, 0}, .speed = 2, .gust = 0.5f, .gust_scale = 20 };
    WindStatus s = {0};
    double period = BOX_WIND_GUST_PERIOD * 20.0;
    double dt = 1.0 / 64.0;
    int32_t f;
    for (f = 0; f < 64 * 500 + 37; f ++) {
        box_render_wind_advance(&wind, dt, &s);
    }

    WindStatus before = s;
    wind.speed = 9;
    box_render_wind_advance(&wind, dt, &s);
    test_assert(fabs(wind_step(before.offset_x, s.offset_x, period) - 9.0 * dt) < 1e-9);
    test_assert(fabs(wind_step(before.offset_z, s.offset_z, period)) < 1e-12);
    test_assert(fabs(wind_step(before.sway, s.sway, 1.0) -
        box_render_wind_sway_frequency(&wind) * dt) < 1e-9);

    /* The naive phase, the new frequency times the time, would have jumped */
    double naive = fmod(box_render_wind_sway_frequency(&wind) * s.time, 1.0);
    test_assert(fabs(wind_step(before.sway, naive, 1.0)) > 0.01);

    /* Calm keeps the gust field and the sway where they are, the flutter clock runs */
    wind.speed = 0;
    before = s;
    box_render_wind_advance(&wind, dt, &s);
    test_assert(s.offset_x == before.offset_x);
    test_assert(fabs(wind_step(before.sway, s.sway, 1.0) - BOX_RENDER_WIND_SWAY_BASE * dt) <
        1e-9);
    test_assert(s.time > before.time);
    test_assert(fabs((s.clock - before.clock) - BOX_RENDER_WIND_CLOCK_CALM * dt) < 1e-9);
}

/** The engine advances the status of the singleton by the frame delta, so a fixed delta gives
 * the same status every run */
void Wind_system_fixed_delta(void) {
    // atmosphere/wind
    WindStatus runs[2];
    int32_t r, f;
    for (r = 0; r < 2; r ++) {
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        ecs_singleton_set(world, Wind, {.direction = {1, 1}, .speed = 3, .gust = 0.5f,
            .gust_scale = 10});
        for (f = 0; f < 100; f ++) {
            ecs_progress(world, WIND_DELTA);
        }

        const WindStatus *s = ecs_get(world, ecs_id(Wind), WindStatus);
        test_assert(s != NULL);
        runs[r] = *s;

        WindStatus expect = {0};
        for (f = 0; f < 100; f ++) {
            box_render_wind_advance(&(Wind){.direction = {1, 1}, .speed = 3, .gust = 0.5f,
                .gust_scale = 10}, WIND_DELTA, &expect);
        }

        test_assert(fabs(s->offset_x - expect.offset_x) < 1e-9);
        test_assert(fabs(s->offset_z - expect.offset_z) < 1e-9);
        test_assert(fabs(s->sway - expect.sway) < 1e-9);
        test_assert(fabs(s->time - 100.0 * WIND_DELTA) < 1e-9);
        test_assert(fabs(s->clock - expect.clock) < 1e-9);

        /* The frame of the renderer reads the singleton */
        box_render_wind_t frame;
        box_render_wind_frame(world, &frame);
        test_flt(frame.direction[0], (float)(1.0 / sqrt(2.0)));
        test_flt(frame.direction[1], (float)(1.0 / sqrt(2.0)));
        test_flt(frame.speed, 3);
        test_flt(frame.gust, 0.5f);
        test_flt(frame.frequency, 0.1f);
        test_flt(frame.offset[0], (float)s->offset_x);
        test_flt(frame.sway, (float)s->sway);
        test_assert(frame.clock == s->clock);
        ecs_fini(world);
    }

    test_assert(!memcmp(&runs[0], &runs[1], sizeof(WindStatus)));

    /* Without a Wind the frame is calm */
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_wind_t frame;
    box_render_wind_frame(world, &frame);
    test_flt(frame.speed, 0);
    test_flt(frame.direction[0], 1);
    test_flt(frame.direction[1], 0);
    ecs_fini(world);
}

/** The record holds the wind and the sway; a material without WindSway packs the bytes it did
 * before, and a transition of another component encodes the sway again */
void Wind_pack(void) {
    // materials/wind_sway
    box_render_material_t record, plain;
    ecs_os_memset_t(&record, 0x55, box_render_material_t);
    box_draws_material_pack(&record, &(box_draw_material_t){ .fade = 1 });
    test_flt(record.wind_speed, 0);
    test_flt(record.wind_bend, 0);
    test_uint(record.flags, 0);
    test_flt(record.reveal_threshold, 0);
    plain = record;
    test_int(ECS_SIZEOF(box_render_material_t), 272);
    test_int(offsetof(box_render_material_t, wind_direction), 192);
    test_int(offsetof(box_render_material_t, flags), 248);
    test_int(offsetof(box_render_material_t, wind_branch), 256);
    test_int(offsetof(box_render_material_t, wind_branch_phase), 260);

    /* A wind without a WindSway changes nothing */
    box_render_wind_t wind = {
        .direction = {0, 1}, .offset = {12.5f, 3}, .speed = 6, .gust = 0.25f,
        .frequency = 0.05f, .sway = 0.375f, .clock = 1000.25
    };
    box_draws_material_pack(&record, &(box_draw_material_t){ .wind = wind, .fade = 1 });
    test_assert(!ecs_os_memcmp_t(&record, &plain, box_render_material_t));

    WindSway sway = { .bend = 0.05f, .height = 4, .base = -1, .flutter = 0.1f,
        .flutter_frequency = 3, .flutter_reach = 0.5f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .sway = &sway, .wind = wind, .fade = 1 });
    test_flt(record.wind_direction[0], 0);
    test_flt(record.wind_direction[1], 1);
    test_flt(record.wind_offset[0], 12.5f);
    test_flt(record.wind_offset[1], 3);
    test_flt(record.wind_speed, 6);
    test_flt(record.wind_gust, 0.25f);
    test_flt(record.wind_frequency, 0.05f);
    test_flt(record.wind_sway, 0.375f);
    test_flt(record.wind_bend, 0.05f);
    test_flt(record.wind_height, 4);
    test_flt(record.wind_base, -1);
    test_flt(record.wind_flutter, 0.1f);
    test_flt(record.wind_reach, 0.5f);
    test_flt(record.wind_flutter_phase, 0.75f);
    test_uint(record.flags, BOX_RENDER_WIND_PRESENT);

    /* The other fields are those of the plain record */
    box_render_material_t other = record;
    box_render_wind_pack(&other, NULL, &wind);
    test_assert(!ecs_os_memcmp_t(&other, &plain, box_render_material_t));

    /* Broken values are clamped */
    WindSway broken = { .bend = NAN, .height = 0, .base = INFINITY, .flutter = -1,
        .flutter_frequency = NAN, .flutter_reach = -2 };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .sway = &broken, .wind = wind, .fade = 1 });
    test_flt(record.wind_bend, 0);
    test_flt(record.wind_height, BOX_RENDER_WIND_MIN);
    test_flt(record.wind_base, 0);
    test_flt(record.wind_flutter, 0);
    test_flt(record.wind_reach, BOX_RENDER_WIND_MIN);
    test_flt(record.wind_flutter_phase, 0);

    /* A transition encodes the sway again */
    PbrMaterial pbr = { .metallic = 0.25f, .roughness = 0.125f };
    box_draws_material_pack(&record, &(box_draw_material_t){
        .sway = &sway, .wind = wind, .pbr = &pbr, .fade = 1 });
    box_draw_transition_material_t target;
    box_render_material_t encoded;
    box_draws_transition_target(&target, &(box_draw_material_t){
        .sway = &sway, .wind = wind, .pbr = &pbr, .fade = 1 }, BOX_DRAW_TRANSITION_TABLE);
    box_draws_transition_encode(&target, &encoded);
    test_assert(!ecs_os_memcmp_t(&encoded, &record, box_render_material_t));
}

/** The flutter phase is the flutter frequency times the wind clock, wrapped in double
 * precision, so it keeps its steps after hours */
void Wind_flutter_phase_wraps(void) {
    // materials/wind_sway
    box_render_material_t a, b;
    WindSway sway = { .bend = 0.1f, .height = 1, .flutter = 0.1f, .flutter_frequency = 2.3f,
        .flutter_reach = 0.3f };
    box_render_wind_t wind = { .direction = {1, 0}, .speed = 4, .frequency = 0.05f };
    int32_t f;
    for (f = 0; f < 600; f ++) {
        wind.clock = 4.0 * 3600.0 + (double)f / 60.0;
        box_render_wind_pack(&a, &sway, &wind);
        wind.clock += 1.0 / 60.0;
        box_render_wind_pack(&b, &sway, &wind);
        test_assert(a.wind_flutter_phase >= 0.0f && a.wind_flutter_phase < 1.0f);
        test_assert(fabs(wind_step(a.wind_flutter_phase, b.wind_flutter_phase, 1.0) -
            (double)2.3f / 60.0) < 1e-5);
    }
}

/** The wind clock runs at a sixth of its full rate in a calm, a third at 2 m/s and at full rate
 * from BOX_WIND_FULL_SPEED; a change of the speed changes how fast it and the flutter and branch
 * phases advance, never where they are, and they keep their steps after hours */
void Wind_clock_rate(void) {
    // atmosphere/wind
    test_assert(fabs(box_render_wind_clock_rate(&(Wind){ .speed = 0 }) - 1.0 / 6.0) < 1e-12);
    test_assert(fabs(box_render_wind_clock_rate(&(Wind){ .speed = 2 }) - 1.0 / 3.0) < 1e-6);
    test_assert(box_render_wind_clock_rate(&(Wind){ .speed = BOX_WIND_FULL_SPEED }) == 1.0);
    test_assert(box_render_wind_clock_rate(&(Wind){ .speed = 1000 }) == 1.0);
    test_assert(box_render_wind_clock_rate(&(Wind){ .speed = 6 }) >
        box_render_wind_clock_rate(&(Wind){ .speed = 4 }));
    test_assert(box_render_wind_clock_rate(&(Wind){ .speed = NAN }) == BOX_RENDER_WIND_CLOCK_CALM);
    test_assert(box_render_wind_clock_rate(&(Wind){ .speed = -3 }) == BOX_RENDER_WIND_CLOCK_CALM);

    /* An hour of a breeze advances the clock by a third of the time */
    Wind wind = { .direction = {1, 0}, .speed = 2, .gust = 0.5f, .gust_scale = 20 };
    WindStatus s = {0};
    double dt = 1.0 / 64.0;
    int32_t f;
    for (f = 0; f < 64 * 3600 + 13; f ++) {
        box_render_wind_advance(&wind, dt, &s);
    }

    test_assert(fabs(s.time - (3600.0 + 13.0 * dt)) < 1e-6);
    test_assert(fabs(s.clock - s.time * box_render_wind_clock_rate(&wind)) < 1e-6);

    /* A gale: the phases of the next frame move by the full rates times the delta */
    WindSway sway = { .bend = 0.05f, .height = 4, .flutter = 0.1f, .flutter_frequency = 2.5f,
        .flutter_reach = 0.5f, .pivots = true, .branch = 0.3f, .branch_frequency = 0.75f };
    box_render_wind_t frame = { .direction = {1, 0}, .speed = 2, .frequency = 0.05f,
        .time = s.time, .clock = s.clock };
    box_render_material_t a, b;
    box_render_wind_pack(&a, &sway, &frame);
    wind.speed = 12;
    box_render_wind_advance(&wind, dt, &s);
    frame.speed = 12;
    frame.time = s.time;
    frame.clock = s.clock;
    box_render_wind_pack(&b, &sway, &frame);
    test_assert(fabs(wind_step(a.wind_flutter_phase, b.wind_flutter_phase, 1.0) -
        2.5 * dt) < 1e-5);
    test_assert(fabs(wind_step(a.wind_branch_phase, b.wind_branch_phase, 1.0) -
        0.75 * dt) < 1e-5);

    /* The naive phase, the frequency times the time, would have jumped */
    float naive = (float)fmod(2.5 * s.time, 1.0);
    test_assert(fabs(wind_step(a.wind_flutter_phase, naive, 1.0)) > 0.01);

    /* Back to a breeze the phases move by a third of the rates */
    a = b;
    wind.speed = 2;
    box_render_wind_advance(&wind, dt, &s);
    frame.speed = 2;
    frame.clock = s.clock;
    box_render_wind_pack(&b, &sway, &frame);
    double rate = box_render_wind_clock_rate(&wind);
    test_assert(fabs(wind_step(a.wind_flutter_phase, b.wind_flutter_phase, 1.0) -
        2.5 * rate * dt) < 1e-5);
    test_assert(fabs(wind_step(a.wind_branch_phase, b.wind_branch_phase, 1.0) -
        0.75 * rate * dt) < 1e-5);

    /* A broken clock starts over */
    s.clock = NAN;
    box_render_wind_advance(&wind, dt, &s);
    test_assert(fabs(s.clock - rate * dt) < 1e-12);
}

/** The reach is 0 when calm, grows with the speed, the gust, the bend and the flutter */
void Wind_reach(void) {
    // materials/wind_sway
    WindSway sway = { .bend = 0.1f, .height = 2, .flutter = 0.2f, .flutter_frequency = 2,
        .flutter_reach = 0.3f };
    box_render_wind_t calm = { .direction = {1, 0}, .frequency = 0.05f };
    test_flt(box_render_wind_reach(&sway, &calm, 0), 0);
    test_flt(box_render_wind_reach(NULL, &calm, 0), 0);

    box_render_wind_t wind = calm;
    wind.speed = 5;
    wind.gust = 0.5f;
    float strength = 5 * 1.5f;
    float expect = 0.1f * strength * (1 + BOX_RENDER_WIND_SWAY + BOX_RENDER_WIND_CROSS) +
        1.15f * 0.2f * strength / BOX_WIND_FULL_SPEED;
    test_assert(fabsf(box_render_wind_reach(&sway, &wind, 0) - expect) < 1e-5f);

    WindSway rigid = { .height = 1, .flutter_reach = 0.3f };
    test_flt(box_render_wind_reach(&rigid, &wind, 0), 0);
    WindSway backwards = sway;
    backwards.bend = -0.1f;
    test_assert(fabsf(box_render_wind_reach(&backwards, &wind, 0) - expect) < 1e-5f);
    test_flt(box_render_wind_reach(NULL, &wind, 0), 0);
}

/** With pivots the record holds the flag, the branch and the branch phase, wrapped in double
 * precision; without pivots they are 0 whatever the branch */
void Wind_pack_pivots(void) {
    // materials/wind_sway
    box_render_material_t record;
    box_render_wind_t wind = {
        .direction = {1, 0}, .speed = 6, .frequency = 0.05f, .sway = 0.25f, .clock = 1000.25
    };
    WindSway sway = { .bend = 0.05f, .height = 4, .flutter = 0.1f, .flutter_frequency = 3,
        .flutter_reach = 0.5f, .branch = 0.3f, .branch_frequency = 0.5f };
    ecs_os_memset_t(&record, 0x55, box_render_material_t);
    box_render_wind_pack(&record, &sway, &wind);
    test_uint(record.flags, BOX_RENDER_WIND_PRESENT);
    test_flt(record.wind_branch, 0);
    test_flt(record.wind_branch_phase, 0);
    test_flt(record.occlusion_strength, 0);
    test_flt(record.wind_pad, 0);
    box_render_material_t plain = record;

    sway.pivots = true;
    box_render_wind_pack(&record, &sway, &wind);
    test_uint(record.flags, BOX_RENDER_WIND_PRESENT | BOX_RENDER_WIND_PIVOTS);
    test_flt(record.wind_branch, 0.3f);
    test_flt(record.wind_branch_phase, 0.125f);
    test_flt(record.occlusion_strength, 0);
    test_flt(record.wind_pad, 0);

    /* The pivots leave the other words as they are */
    record.flags = plain.flags;
    record.wind_branch = 0;
    record.wind_branch_phase = 0;
    test_assert(!ecs_os_memcmp_t(&record, &plain, box_render_material_t));

    /* The flag does not collide with the VertexReveal bits */
    test_uint(BOX_RENDER_WIND_PIVOTS & (BOX_RENDER_WIND_PRESENT | BOX_RENDER_REVEAL_PRESENT |
        BOX_RENDER_REVEAL_INVERT | (7u << BOX_RENDER_REVEAL_CHANNEL_SHIFT) |
        (0xFFFFu << BOX_RENDER_REVEAL_SOFTNESS_SHIFT)), 0);

    /* Broken values are 0 */
    sway.branch = NAN;
    sway.branch_frequency = INFINITY;
    box_render_wind_pack(&record, &sway, &wind);
    test_flt(record.wind_branch, 0);
    test_flt(record.wind_branch_phase, 0);

    /* The branch phase keeps its steps after hours */
    int32_t f;
    box_render_material_t a, b;
    sway.branch = 0.3f;
    sway.branch_frequency = 0.7f;
    for (f = 0; f < 600; f ++) {
        wind.clock = 4.0 * 3600.0 + (double)f / 60.0;
        box_render_wind_pack(&a, &sway, &wind);
        wind.clock += 1.0 / 60.0;
        box_render_wind_pack(&b, &sway, &wind);
        test_assert(a.wind_branch_phase >= 0.0f && a.wind_branch_phase < 1.0f);
        test_assert(fabs(wind_step(a.wind_branch_phase, b.wind_branch_phase, 1.0) -
            (double)0.7f / 60.0) < 1e-5);
    }
}

/** With pivots the reach grows by the chords of the largest turns of a branch and its parent
 * over the size; the size is ignored without pivots */
void Wind_reach_pivots(void) {
    // materials/wind_sway
    WindSway sway = { .bend = 0.1f, .height = 2, .flutter = 0.2f, .flutter_frequency = 2,
        .flutter_reach = 0.3f, .branch = 0.4f, .branch_frequency = 0.5f };
    box_render_wind_t wind = { .direction = {1, 0}, .speed = 5, .gust = 0.5f,
        .frequency = 0.05f };
    float plain = box_render_wind_reach(&sway, &wind, 0);
    test_flt(box_render_wind_reach(&sway, &wind, 3), plain);
    test_flt(box_render_wind_turn(&sway), 0);

    sway.pivots = true;
    float turn = 0.4f * sqrtf(1.0f + BOX_RENDER_WIND_BRANCH_CROSS *
        BOX_RENDER_WIND_BRANCH_CROSS);
    test_assert(fabsf(box_render_wind_turn(&sway) - turn) < 1e-6f);
    test_assert(fabsf(box_render_wind_reach(&sway, &wind, 0) - plain) < 1e-6f);
    float expect = plain + 4.0f * sinf(0.5f * turn) * 3.0f;
    test_assert(fabsf(box_render_wind_reach(&sway, &wind, 3) - expect) < 1e-5f);

    /* A backwards branch reaches as far, a huge one at most the diameter four times */
    sway.branch = -0.4f;
    test_assert(fabsf(box_render_wind_reach(&sway, &wind, 3) - expect) < 1e-5f);
    sway.branch = 100;
    test_flt(box_render_wind_turn(&sway), GLM_PIf);
    test_assert(box_render_wind_reach(&sway, &wind, 3) <= plain + 12.0f + 1e-5f);

    /* Calm or broken values reach nothing more */
    box_render_wind_t calm = wind;
    calm.speed = 0;
    test_flt(box_render_wind_reach(&sway, &calm, 3), 0);
    sway.branch = NAN;
    test_flt(box_render_wind_turn(&sway), 0);
    test_assert(fabsf(box_render_wind_reach(&sway, &wind, 3) - plain) < 1e-6f);
    sway.branch = 0.4f;
    test_assert(fabsf(box_render_wind_reach(&sway, &wind, NAN) - plain) < 1e-6f);
}

/** box_wind_sway_vertex packs the level with the flutter weight and the two stiffnesses so the
 * shaders unpack them, clamped */
void Wind_sway_vertex(void) {
    // materials/wind_sway
    vec4 data0, data1;
    box_wind_sway_vertex(data0, data1, (vec3){1, 2, 3}, 2, 0.4f, 1.0f, (vec3){4, 5, 6}, 0.8f);
    test_flt(data0[0], 1);
    test_flt(data0[1], 2);
    test_flt(data0[2], 3);
    test_flt(data0[3], 2.5f);
    test_flt(data1[0], 4);
    test_flt(data1[1], 5);
    test_flt(data1[2], 6);
    test_flt(data1[3], 102.0f + 256.0f * 204.0f);

    /* Unpacked as wind.wgsl does */
    float level = floorf(data0[3]);
    float weight = 2.0f * (data0[3] - level);
    float parent = floorf(data1[3] / 256.0f);
    test_flt(level, 2);
    test_flt(weight, 1);
    test_assert(fabsf((data1[3] - 256.0f * parent) / 255.0f - 0.4f) < 0.5f / 255.0f);
    test_assert(fabsf(parent / 255.0f - 0.8f) < 0.5f / 255.0f);

    /* Without a parent, out of range or broken values */
    box_wind_sway_vertex(data0, data1, (vec3){1, 2, 3}, 9, 2.0f, NAN, NULL, -1.0f);
    test_flt(data0[3], (float)BOX_MATERIAL_SWAY_LEVELS);
    test_flt(data1[0], 0);
    test_flt(data1[1], 0);
    test_flt(data1[2], 0);
    test_flt(data1[3], 255);
    box_wind_sway_vertex(data0, data1, (vec3){1, 2, 3}, -1, 0, 0.5f, NULL, 0);
    test_flt(data0[3], 0.25f);
    test_flt(data1[3], 0);
}

/** Scene.wind is a feature of the scene that turns on no override of the mesh shaders: the
 * pipelines of a scene with it are those of a scene without it, except the wind batches */
void Wind_scene_feature(void) {
    // atmosphere/wind
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_uint(box_render_scene_features(world, 0) & BOX_RENDER_FEATURE_WIND, 0);
    ecs_singleton_set(world, Scene, {.wind = true});
    test_uint(box_render_scene_features(world, 0), BOX_RENDER_FEATURE_WIND);

    uint32_t shaders[2] = { BOX_RENDER_SHADER_MESH, BOX_RENDER_SHADER_MESH_EXTENDED };
    int32_t s;
    for (s = 0; s < 2; s ++) {
        test_uint(box_render_mesh_overrides(shaders[s], BOX_RENDER_FEATURE_WIND, false, false),
            0);
        test_uint(box_render_mesh_overrides(shaders[s], BOX_RENDER_FEATURE_WIND |
            BOX_RENDER_FEATURE_SHEEN, true, false), box_render_mesh_overrides(shaders[s],
            BOX_RENDER_FEATURE_SHEEN, true, false));
    }

    ecs_fini(world);
}

/** In a scene with Scene.wind a shape with a WindSway gets a wind batch of its own, whose draw
 * moves its vertices and grows its bounds; a shape without one keeps the batch, the record and
 * the bounds it has in a scene without the wind */
void Wind_batches(void) {
    // materials/wind_sway
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = wind_world(&null, &view);
    ecs_singleton_set(world, Scene, {.wind = true});
    ecs_singleton_set(world, Wind, {.direction = {1, 0}, .speed = 8, .gust = 0.5f,
        .gust_scale = 20});
    ecs_entity_t plain = wind_box(world, 0, 0);
    ecs_entity_t tree = wind_box(world, 3, 0);
    ecs_set(world, tree, WindSway, {.bend = 0.05f, .height = 2, .flutter = 0.1f,
        .flutter_frequency = 2, .flutter_reach = 0.3f});
    wind_frame(world, null);
    wind_frame(world, null);

    box_draw_batch_t *pb = wind_batch(world, plain);
    box_draw_batch_t *tb = wind_batch(world, tree);
    test_assert(pb != tb);
    test_bool(pb->key.wind, false);
    test_bool(tb->key.wind, true);
    test_assert(pb->key.features & BOX_RENDER_FEATURE_WIND);
    test_int(pb->key.pipeline, BOX_RENDER_SHADER_MESH);
    test_int(tb->key.pipeline, BOX_RENDER_SHADER_MESH);

    const box_render_batch_t *pd = wind_draw(world, null, plain);
    test_bool(pd->wind, false);
    test_flt(pd->wind_reach, 0);
    float plain_min[3], plain_max[3];
    glm_vec3_copy(ECS_CONST_CAST(float*, pd->min), plain_min);
    glm_vec3_copy(ECS_CONST_CAST(float*, pd->max), plain_max);
    const box_render_batch_t *td = wind_draw(world, null, tree);
    test_bool(td->wind, true);
    WindSway sway = *ecs_get(world, tree, WindSway);
    box_render_wind_t frame;
    box_render_wind_frame(world, &frame);
    float reach = box_render_wind_reach(&sway, &frame, 0);
    test_assert(reach > 0.0f);
    test_flt(td->wind_reach, reach);

    /* The world bounds of the swaying box grow by the reach on every side */
    test_bool(td->bounds, true);
    test_assert(td->min[0] <= 2.5f - reach && td->min[0] > 2.5f - reach - 0.01f);
    test_assert(td->max[0] >= 3.5f + reach && td->max[0] < 3.5f + reach + 0.01f);
    test_assert(td->min[1] <= -0.5f - reach);
    test_assert(td->max[2] >= 0.5f + reach);

    /* The record of the swaying shape holds the wind, that of the plain one none */
    const box_render_material_t *r = wind_record(world, tree);
    test_uint(r->flags, BOX_RENDER_WIND_PRESENT);
    test_flt(r->wind_speed, 8);
    test_flt(r->wind_bend, 0.05f);
    const MaterialId *id = ecs_get(world, plain, MaterialId);
    if (id && id->value != UINT32_MAX) {
        r = wind_record(world, plain);
        test_uint(r->flags, 0);
        test_flt(r->wind_speed, 0);
    }

    /* The plain box has the bounds it has without the wind (in a world of its own, as without
     * Scene.wind it shares the batch of the box that no longer sways) */
    box_render_null_t *ref_null;
    ecs_entity_t ref_view;
    ecs_world_t *ref = wind_world(&ref_null, &ref_view);
    ecs_singleton_set(ref, Wind, {.direction = {1, 0}, .speed = 8, .gust = 0.5f,
        .gust_scale = 20});
    ecs_entity_t ref_plain = wind_box(ref, 0, 0);
    wind_frame(ref, ref_null);
    wind_frame(ref, ref_null);
    pd = wind_draw(ref, ref_null, ref_plain);
    test_bool(pd->wind, false);
    test_assert(!memcmp(pd->min, plain_min, sizeof(plain_min)));
    test_assert(!memcmp(pd->max, plain_max, sizeof(plain_max)));
    ecs_fini(ref);
    box_render_null_free(ref_null);

    ecs_singleton_set(world, Scene, {.wind = false});
    wind_frame(world, null);
    wind_frame(world, null);

    /* Without Scene.wind the swaying shape draws as without its WindSway */
    tb = wind_batch(world, tree);
    test_bool(tb->key.wind, false);
    td = wind_draw(world, null, tree);
    test_bool(td->wind, false);
    test_flt(td->wind_reach, 0);
    r = wind_record(world, tree);
    test_uint(r->flags, 0);
    test_flt(r->wind_speed, 0);
    test_assert(td->max[0] < 3.5f + 0.01f);

    ecs_fini(world);
    box_render_null_free(null);
}

/** A calm wind leaves the bounds of a swaying shape tight and its reach 0, and a removed
 * WindSway moves the shape back to the plain batch */
void Wind_calm_and_removed(void) {
    // materials/wind_sway
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = wind_world(&null, &view);
    ecs_singleton_set(world, Scene, {.wind = true});
    ecs_singleton_set(world, Wind, {.direction = {1, 0}, .speed = 0});
    ecs_entity_t plain = wind_box(world, 0, 0);
    ecs_entity_t tree = wind_box(world, 3, 0);
    ecs_set(world, tree, WindSway, {.bend = 0.05f, .height = 2});
    wind_frame(world, null);
    wind_frame(world, null);

    const box_render_batch_t *td = wind_draw(world, null, tree);
    test_bool(td->wind, true);
    test_flt(td->wind_reach, 0);
    test_assert(td->max[0] < 3.5f + 0.01f);
    test_flt(wind_record(world, tree)->wind_speed, 0);

    ecs_remove(world, tree, WindSway);
    wind_frame(world, null);
    test_bool(wind_batch(world, tree)->key.wind, false);
    test_assert(wind_batch(world, tree) == wind_batch(world, plain));

    ecs_fini(world);
    box_render_null_free(null);
}

/** Create a spot light shining straight down that casts shadows.
 *
 * @param world The world
 * @param position World position of the light
 * @return The light
 */
static ecs_entity_t wind_spot(
    ecs_world_t *world,
    vec3 position)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {position[0], position[1], position[2]});
    ecs_set(world, e, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_add(world, e, SpotLight);
    SpotLight *s = ecs_get_mut(world, e, SpotLight);
    s->shadows = true;
    s->bake = LightBakeModeMixed;
    ecs_modified(world, e, SpotLight);

    return e;
}

/** Run a frame and check whether the tile of a light renders.
 *
 * @param world The world
 * @param null The backend
 * @param light The light
 * @return Whether its tile renders
 */
static bool wind_tile_renders(
    ecs_world_t *world,
    box_render_null_t *null,
    ecs_entity_t light)
{
    const box_render_packet_t *p = wind_frame(world, null);
    const box_render_shadow_tile_t *tile = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&p->shadow_tiles); i ++) {
        const box_render_shadow_tile_t *t = ecs_vec_get_t(
            &p->shadow_tiles, box_render_shadow_tile_t, i);
        if (t->light == light) {
            tile = t;
        }
    }

    test_assert(tile != NULL);

    return tile->render;
}

/** Turn on the shadows of the view and Scene.punctual_shadows with Scene.wind.
 *
 * @param world The world
 * @param view The view
 */
static void wind_shadows(
    ecs_world_t *world,
    ecs_entity_t view)
{
    View *v = ecs_get_mut(world, view, View);
    v->shadows.enabled = true;
    ecs_modified(world, view, View);
    ecs_singleton_set(world, Scene, {.punctual_shadows = true, .wind = true});
}

/** A cached spot shadow tile whose casters do not sway is not rendered again while a strong
 * wind blows */
void Wind_spot_tile_kept_without_sway(void) {
    // atmosphere/wind
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = wind_world(&null, &view);
    wind_shadows(world, view);
    ecs_singleton_set(world, Wind, {.direction = {1, 0}, .speed = 20, .gust = 1,
        .gust_scale = 5});
    ecs_entity_t light = wind_spot(world, (vec3){0, 4, 0});
    ecs_entity_t caster = ecs_new(world);
    ecs_set(world, caster, Position, {0, 2, 0});
    ecs_set(world, caster, Box, {1, 0.1f, 1});

    /* A swaying shape far from the light does not reach its tile */
    ecs_entity_t far = ecs_new(world);
    ecs_set(world, far, Position, {0, 2, -60});
    ecs_set(world, far, Box, {1, 0.1f, 1});
    ecs_set(world, far, WindSway, {.bend = 0.01f, .height = 1});

    test_bool(wind_tile_renders(world, null, light), true);
    test_bool(wind_tile_renders(world, null, light), false);
    test_bool(wind_tile_renders(world, null, light), false);
    test_bool(wind_tile_renders(world, null, light), false);

    ecs_fini(world);
    box_render_null_free(null);
}

/** The tile of a light with a swaying caster renders every frame while the wind blows, once more
 * when it calms down, and is kept again after */
void Wind_spot_tile_renders_with_sway(void) {
    // atmosphere/wind
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = wind_world(&null, &view);
    wind_shadows(world, view);
    ecs_singleton_set(world, Wind, {.direction = {1, 0}, .speed = 5, .gust = 0.5f,
        .gust_scale = 20});
    ecs_entity_t light = wind_spot(world, (vec3){0, 4, 0});
    ecs_entity_t caster = ecs_new(world);
    ecs_set(world, caster, Position, {0, 2, 0});
    ecs_set(world, caster, Box, {0.2f, 1, 0.2f});
    ecs_set(world, caster, WindSway, {.bend = 0.05f, .height = 1, .base = 1.5f});

    test_bool(wind_tile_renders(world, null, light), true);
    test_bool(wind_tile_renders(world, null, light), true);
    test_bool(wind_tile_renders(world, null, light), true);

    ecs_singleton_set(world, Wind, {.direction = {1, 0}, .speed = 0, .gust = 0.5f,
        .gust_scale = 20});
    test_bool(wind_tile_renders(world, null, light), true);
    test_bool(wind_tile_renders(world, null, light), false);
    test_bool(wind_tile_renders(world, null, light), false);

    /* Without Scene.wind it does not sway, and the tile is kept */
    ecs_singleton_set(world, Wind, {.direction = {1, 0}, .speed = 5});
    ecs_singleton_set(world, Scene, {.punctual_shadows = true});
    wind_frame(world, null);
    test_bool(wind_tile_renders(world, null, light), false);
    test_bool(wind_tile_renders(world, null, light), false);

    ecs_fini(world);
    box_render_null_free(null);
}

/** Cascades with an interval are kept between their renders as before when no caster sways, and
 * all render every frame while swaying casters move in the scene */
void Wind_cascades_render_with_sway(void) {
    // atmosphere/wind
    box_render_null_t *null;
    ecs_entity_t view;
    ecs_world_t *world = wind_world(&null, &view);
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {(float)GLM_PI_2, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1});
    View *v = ecs_get_mut(world, view, View);
    v->shadows.enabled = true;
    v->shadows.interval[0] = 2;
    v->shadows.interval[1] = 2;
    v->shadows.interval[2] = 2;
    v->shadows.interval[3] = 2;
    ecs_modified(world, view, View);
    ecs_singleton_set(world, Scene, {.wind = true});
    ecs_singleton_set(world, Wind, {.direction = {1, 0}, .speed = 5});
    wind_box(world, 0, 0);
    int32_t f;
    wind_frame(world, null);
    for (f = 0; f < 4; f ++) {
        wind_frame(world, null);
        test_assert(box_render_shadow_rendered(world, view) != 0xFu);
    }

    ecs_entity_t tree = wind_box(world, 2, 0);
    ecs_set(world, tree, WindSway, {.bend = 0.05f, .height = 1});
    wind_frame(world, null);
    for (f = 0; f < 4; f ++) {
        wind_frame(world, null);
        test_uint(box_render_shadow_rendered(world, view), 0xFu);
    }

    ecs_singleton_set(world, Wind, {.direction = {1, 0}, .speed = 0});
    wind_frame(world, null);
    wind_frame(world, null);
    for (f = 0; f < 4; f ++) {
        wind_frame(world, null);
        test_assert(box_render_shadow_rendered(world, view) != 0xFu);
    }

    ecs_fini(world);
    box_render_null_free(null);
}
