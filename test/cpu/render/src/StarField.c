/**
 * @file StarField.c
 * @brief Tests for the StarField component and the star field records the light extract gives
 * the views: defaults, reflection, resolved records, the views that draw none and the stars the
 * shader generates, repeated on the CPU.
 *
 * The camera of the views stands at (0, 0, 10) and looks along -Z with a 60 degree field of
 * view; the worlds render into a null backend.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

/** Create a world that renders into a null backend.
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

    return world;
}

/** Create a lit view and its perspective camera.
 *
 * @param world The world
 * @param parent Parent of the camera, its scene, 0 for the default scene
 * @return The view
 */
static ecs_entity_t view_new(
    ecs_world_t *world,
    ecs_entity_t parent)
{
    ecs_entity_t camera = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    ecs_modified(world, view, View);

    return view;
}

/** Create a star field with the defaults.
 *
 * @param world The world
 * @param parent Parent of the field, 0 for none
 * @return The entity
 */
static ecs_entity_t field_new(
    ecs_world_t *world,
    ecs_entity_t parent)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_add(world, e, StarField);

    return e;
}

/** Run a frame and wait until the backend rendered it.
 *
 * @param world The world
 */
static void frame(
    ecs_world_t *world)
{
    ecs_progress(world, 0);
    box_render_sync(world);
}

/** Find the view of the last packet that renders a view entity.
 *
 * @param null The backend
 * @param view The view entity
 * @return The view of the packet
 */
static const box_render_view_t* packet_view(
    box_render_null_t *null,
    ecs_entity_t view)
{
    const box_render_view_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.views); i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&null->last.views, box_render_view_t, i);
        if (v->view == view) {
            result = v;
        }
    }

    test_assert(result != NULL);

    return result;
}

/** Get a star field record of a view of the last packet.
 *
 * @param null The backend
 * @param v The view of the packet
 * @param index Index of the record among those of the view
 * @return The record
 */
static const box_render_star_field_t* record_of(
    box_render_null_t *null,
    const box_render_view_t *v,
    int32_t index)
{
    test_assert(index < v->star_count);
    test_assert(v->star_first + index < ecs_vec_count(&null->last.star_fields));

    return ecs_vec_get_t(&null->last.star_fields, box_render_star_field_t,
        v->star_first + index);
}

/** Make a record of the defaults of a StarField at the identity rotation.
 *
 * @param out Receives the record
 */
static void default_record(
    box_render_star_field_t *out)
{
    StarField f = {
        .count = 1000, .seed = 1, .brightness = 6, .falloff = 4, .size = 1.5f,
        .temperature_min = 3000, .temperature_max = 12000, .saturation = 0.6f,
        .band = 0.4f, .band_width = 8
    };
    box_render_star_record(&f, NULL, 0, out);
}

void StarField_defaults(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "stars",
        "using box.light\n"
        "stars { StarField: {} }\n", NULL) == 0);
    const StarField *f = ecs_get(world, ecs_lookup(world, "stars"), StarField);
    test_assert(f != NULL);
    test_int(f->count, 20000);
    test_int(f->seed, 1);
    test_flt(f->brightness, 6);
    test_flt(f->falloff, 4);
    test_flt(f->size, 1.5f);
    test_flt(f->temperature_min, 3000);
    test_flt(f->temperature_max, 12000);
    test_flt(f->saturation, 0.6f);
    test_flt(f->band, 0.4f);
    test_flt(f->band_width, 8);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, StarField);
    f = ecs_get(world, added, StarField);
    test_int(f->count, 20000);
    test_int(f->seed, 1);
    test_flt(f->size, 1.5f);
    test_flt(f->band_width, 8);

    ecs_fini(world);
}

void StarField_zero_is_literal(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "stars",
        "using box.light\n"
        "stars { StarField: {count: 0, seed: 0, brightness: 0, band: 0, saturation: 0} }\n",
        NULL) == 0);
    const StarField *f = ecs_get(world, ecs_lookup(world, "stars"), StarField);
    test_int(f->count, 0);
    test_int(f->seed, 0);
    test_flt(f->brightness, 0);
    test_flt(f->band, 0);
    test_flt(f->saturation, 0);
    test_flt(f->falloff, 4);
    test_flt(f->size, 1.5f);
    test_flt(f->temperature_min, 3000);
    test_flt(f->temperature_max, 12000);
    test_flt(f->band_width, 8);

    ecs_entity_t literal = ecs_new(world);
    ecs_set(world, literal, StarField, {.count = 5});
    f = ecs_get(world, literal, StarField);
    test_int(f->count, 5);
    test_flt(f->brightness, 0);
    test_flt(f->size, 0);

    ecs_fini(world);
}

void StarField_reflection(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "stars",
        "using box.light\n"
        "stars { StarField: {count: 123, seed: 9, brightness: 2.5, falloff: 1.5, size: 3,\n"
        "  temperature_min: 4000, temperature_max: 9000, saturation: 0.25, band: 0.75,\n"
        "  band_width: 12} }\n", NULL) == 0);
    ecs_entity_t e = ecs_lookup(world, "stars");
    const StarField *f = ecs_get(world, e, StarField);
    test_int(f->count, 123);
    test_int(f->seed, 9);
    test_flt(f->brightness, 2.5f);
    test_flt(f->falloff, 1.5f);
    test_flt(f->size, 3);
    test_flt(f->temperature_min, 4000);
    test_flt(f->temperature_max, 9000);
    test_flt(f->saturation, 0.25f);
    test_flt(f->band, 0.75f);
    test_flt(f->band_width, 12);

    const EcsStruct *st = ecs_get(world, ecs_id(StarField), EcsStruct);
    test_assert(st != NULL);
    test_int(ecs_vec_count(&st->members), 10);
    char *json = ecs_ptr_to_json(world, ecs_id(StarField), f);
    test_assert(json != NULL);
    test_assert(strstr(json, "\"band_width\":12") != NULL);
    test_assert(strstr(json, "\"count\":123") != NULL);
    ecs_os_free(json);

    ecs_fini(world);
}

void StarField_record_in_packet(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    ecs_entity_t e = field_new(world, 0);
    StarField *f = ecs_get_mut(world, e, StarField);
    f->count = 3000;
    f->seed = 42;
    f->brightness = 8;
    f->falloff = 2;
    f->size = 2;
    f->temperature_min = 3500;
    f->temperature_max = 9000;
    f->saturation = 0.5f;
    f->band = 0.7f;
    f->band_width = 10;
    ecs_modified(world, e, StarField);
    frame(world);

    const box_render_view_t *v = packet_view(null, view);
    test_int(v->star_count, 1);
    const box_render_star_field_t *r = record_of(null, v, 0);
    test_int(r->entity, e);
    test_int(r->count, 3000);
    test_int(r->seed, 42);
    test_flt(r->brightness, 8);
    test_flt(r->falloff, 2);
    test_flt(r->size, 2);
    test_flt(r->temperature_min, 3500);
    test_flt(r->temperature_max, 9000);
    test_flt(r->saturation, 0.5f);
    test_flt(r->band, 0.7f);
    test_assert(fabsf(r->band_width - 10.0f * 3.14159265f / 180.0f) < 1e-5f);
    test_flt(r->rotation[0][0], 1);
    test_flt(r->rotation[1][1], 1);
    test_flt(r->rotation[2][2], 1);

    f = ecs_get_mut(world, e, StarField);
    f->seed = 43;
    ecs_modified(world, e, StarField);
    frame(world);
    v = packet_view(null, view);
    test_int(record_of(null, v, 0)->seed, 43);

    ecs_fini(world);
    box_render_null_free(null);
}

void StarField_rotated_record(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    ecs_entity_t e = field_new(world, 0);
    ecs_set(world, e, Rotation, {1.5707963f, 0, 0});
    ecs_set(world, e, Scale, {3, 3, 3});
    frame(world);

    const box_render_star_field_t *r = record_of(null, packet_view(null, view), 0);
    test_assert(fabsf(r->rotation[0][0] - 1) < 1e-4f);
    test_assert(fabsf(r->rotation[1][0]) < 1e-4f);
    test_assert(fabsf(r->rotation[1][1]) < 1e-4f);
    test_assert(fabsf(fabsf(r->rotation[1][2]) - 1) < 1e-4f);
    test_assert(fabsf(fabsf(r->rotation[2][1]) - 1) < 1e-4f);

    ecs_fini(world);
    box_render_null_free(null);
}

void StarField_clamped_values(void) {
    StarField f = {
        .count = BOX_STAR_FIELD_MAX + 10, .seed = 3, .brightness = -1, .falloff = -2,
        .size = -1, .temperature_min = 100, .temperature_max = 50, .saturation = 2,
        .band = -0.5f, .band_width = -4
    };
    box_render_star_field_t r;
    box_render_star_record(&f, NULL, 7, &r);
    test_int(r.count, BOX_STAR_FIELD_MAX);
    test_int(r.seed, 3);
    test_flt(r.brightness, 0);
    test_flt(r.falloff, 0);
    test_flt(r.size, 0);
    test_flt(r.temperature_min, 1000);
    test_flt(r.temperature_max, 1000);
    test_flt(r.saturation, 1);
    test_flt(r.band, 0);
    test_flt(r.band_width, 0);
    test_int(r.entity, 7);

    f.count = -5;
    f.temperature_min = 50000;
    f.band = 3;
    box_render_star_record(&f, NULL, 7, &r);
    test_int(r.count, 0);
    test_flt(r.temperature_min, 40000);
    test_flt(r.temperature_max, 40000);
    test_flt(r.band, 1);
}

void StarField_count_zero_none(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    ecs_entity_t e = field_new(world, 0);
    ecs_set(world, e, StarField, {.count = 0, .brightness = 5, .size = 2});
    frame(world);

    test_int(packet_view(null, view)->star_count, 0);
    test_int(ecs_vec_count(&null->last.star_fields), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void StarField_unlit_view_none(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    View *v = ecs_get_mut(world, view, View);
    v->lighting.enabled = false;
    ecs_modified(world, view, View);
    field_new(world, 0);
    frame(world);

    test_int(packet_view(null, view)->star_count, 0);
    test_int(ecs_vec_count(&null->last.star_fields), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void StarField_ortho_view_none(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    ecs_entity_t camera = ecs_get(world, view, View)->camera;
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_height = 10, .near_ = 0.1f,
        .far_ = 100});
    field_new(world, 0);
    frame(world);

    test_int(packet_view(null, view)->star_count, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void StarField_other_scene_none(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_entity_t view = view_new(world, 0);
    ecs_entity_t scene_view = view_new(world, scene);
    ecs_entity_t e = field_new(world, scene);
    frame(world);

    test_int(packet_view(null, view)->star_count, 0);
    const box_render_view_t *sv = packet_view(null, scene_view);
    test_int(sv->star_count, 1);
    test_int(record_of(null, sv, 0)->entity, e);

    ecs_fini(world);
    box_render_null_free(null);
}

void StarField_two_fields_by_entity(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t view = view_new(world, 0);
    ecs_entity_t a = field_new(world, 0);
    ecs_entity_t b = field_new(world, 0);
    ecs_set(world, b, Rotation, {0, 1, 0});
    frame(world);

    const box_render_view_t *v = packet_view(null, view);
    test_int(v->star_count, 2);
    test_int(record_of(null, v, 0)->entity, a < b ? a : b);
    test_int(record_of(null, v, 1)->entity, a < b ? b : a);

    ecs_delete(world, a);
    frame(world);
    v = packet_view(null, view);
    test_int(v->star_count, 1);
    test_int(record_of(null, v, 0)->entity, b);

    ecs_fini(world);
    box_render_null_free(null);
}

void StarField_records_follow_add(void) {
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    view_new(world, 0);
    frame(world);
    test_int(ecs_vec_count(&null->last.star_fields), 0);

    field_new(world, 0);
    frame(world);
    test_int(ecs_vec_count(&null->last.star_fields), 1);

    ecs_fini(world);
    box_render_null_free(null);
}

void StarField_sample_deterministic(void) {
    box_render_star_field_t r;
    box_render_star_t s1, s2;
    int32_t i, same = 0;
    default_record(&r);
    for (i = 0; i < 100; i ++) {
        box_render_star_sample(&r, (uint32_t)i, &s1);
        box_render_star_sample(&r, (uint32_t)i, &s2);
        test_assert(!memcmp(&s1, &s2, sizeof(s1)));
        test_assert(fabsf(glm_vec3_norm(s1.direction) - 1) < 1e-4f);
        test_assert(s1.relative > 0 && s1.relative <= 1);
        test_assert(s1.temperature >= 3000 && s1.temperature <= 12000);
    }

    box_render_star_field_t other = r;
    other.seed = 2;
    for (i = 0; i < 100; i ++) {
        box_render_star_sample(&r, (uint32_t)i, &s1);
        box_render_star_sample(&other, (uint32_t)i, &s2);
        same += glm_vec3_distance(s1.direction, s2.direction) < 1e-3f;
    }

    test_assert(same < 3);
}

void StarField_sample_band(void) {
    box_render_star_field_t r;
    box_render_star_t s;
    int32_t i, near_plane = 0;
    default_record(&r);
    r.band = 1;
    r.band_width = 2.0f * 3.14159265f / 180.0f;
    for (i = 0; i < 1000; i ++) {
        box_render_star_sample(&r, (uint32_t)i, &s);
        test_assert(fabsf(s.direction[1]) < sinf(10.0f * 3.14159265f / 180.0f));
    }

    r.band = 0;
    for (i = 0; i < 1000; i ++) {
        box_render_star_sample(&r, (uint32_t)i, &s);
        near_plane += fabsf(s.direction[1]) < 0.1f;
    }

    test_assert(near_plane > 60 && near_plane < 140);

    r.band = 1;
    r.rotation[1][0] = 0;
    r.rotation[1][1] = 0;
    r.rotation[1][2] = 1;
    r.rotation[2][0] = 0;
    r.rotation[2][1] = -1;
    r.rotation[2][2] = 0;
    for (i = 0; i < 1000; i ++) {
        box_render_star_sample(&r, (uint32_t)i, &s);
        test_assert(fabsf(s.direction[2]) < sinf(10.0f * 3.14159265f / 180.0f));
    }
}

void StarField_sample_falloff(void) {
    box_render_star_field_t r;
    box_render_star_t s;
    int32_t i, faint = 0;
    default_record(&r);
    r.falloff = 0;
    for (i = 0; i < 100; i ++) {
        box_render_star_sample(&r, (uint32_t)i, &s);
        test_flt(s.relative, 1);
        test_flt(s.radius, 1.5f);
    }

    r.falloff = 4;
    for (i = 0; i < 1000; i ++) {
        box_render_star_sample(&r, (uint32_t)i, &s);
        faint += s.relative < 0.1f;
    }

    test_assert(faint > 500);
}

void StarField_sample_energy(void) {
    box_render_star_field_t r;
    box_render_star_t s;
    int32_t i, points = 0;
    default_record(&r);
    r.size = 3;
    for (i = 0; i < 200; i ++) {
        box_render_star_sample(&r, (uint32_t)i, &s);
        test_assert(fabsf(s.radius - 3.0f * sqrtf(s.relative)) < 1e-4f);
        float energy;
        if (s.radius < BOX_RENDER_STAR_MIN_RADIUS) {
            test_flt(s.drawn, 0);
            energy = s.peak;
            points ++;
        } else {
            test_flt(s.drawn, s.radius);
            energy = s.peak * 3.14159265f * (s.drawn * s.drawn + 1.0f / 12.0f);
        }

        test_assert(fabsf(energy - r.brightness * 3.14159265f * s.radius * s.radius) <
            1e-4f * r.brightness * 30.0f);
    }

    test_assert(points > 0 && points < 200);

    r.size = 0;
    box_render_star_sample(&r, 0, &s);
    test_flt(s.peak, 0);
}

void StarField_blackbody(void) {
    float c[3];
    box_render_star_blackbody(6600, c);
    test_assert(c[0] > 254 && c[1] > 250 && c[2] > 254);
    box_render_star_blackbody(3000, c);
    test_flt(c[0], 255);
    test_assert(c[2] < c[1] && c[1] < c[0]);
    box_render_star_blackbody(20000, c);
    test_flt(c[2], 255);
    test_assert(c[0] < c[2]);

    box_render_star_field_t r;
    box_render_star_t s;
    default_record(&r);
    r.saturation = 0;
    box_render_star_sample(&r, 5, &s);
    test_flt(s.color[0], 1);
    test_flt(s.color[1], 1);
    test_flt(s.color[2], 1);
    r.saturation = 1;
    r.temperature_min = 2000;
    r.temperature_max = 2000;
    box_render_star_sample(&r, 5, &s);
    test_flt(s.color[0], 1);
    test_assert(s.color[2] < 0.1f);
}
