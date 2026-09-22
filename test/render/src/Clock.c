/**
 * @file Clock.c
 * @brief Tests for the simulated clock that drives the sun, moon and stars.
 */

#include <box.h>
#include <box/sky.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static ecs_entity_t sun;
static ecs_entity_t moon;
static ecs_entity_t stars;
static ecs_entity_t atmosphere;
static ecs_entity_t clock_entity;

void Clock_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {0, 0, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 3});
    moon = ecs_new(w);
    ecs_set(w, moon, Rotation, {0, 0, 0});
    ecs_set(w, moon, DirectionalLight, {.intensity = 1});
    stars = ecs_new(w);
    ecs_set(w, stars, Rotation, {0, 0, 0});
    ecs_set(w, stars, Stars, {0});
    atmosphere = ecs_new(w);
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .moon = moon, .stars = stars});
    clock_entity = ecs_new(w);
    ecs_set(w, clock_entity, TimeOfDay, {.day_of_year = 81, .hour = 12});
}

void Clock_teardown(void)
{
    ecs_fini(w);
}

void Clock_the_sun_stands_overhead_at_noon_on_the_equator(void)
{
    // sky/time_of_day
    ecs_progress(w, 0);
    const WorldTransform *t = ecs_get(w, sun, WorldTransform);
    vec3 dir = vec3_scale(vec3_normalize(
        mat4_transform_dir(&t->m, vec3_new(0, 0, 1))), -1.0f);

    test_assert(fabsf(dir.y - 1.0f) < 1e-3f);
}

void Clock_the_sun_stands_at_the_latitude_at_noon(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {.day_of_year = 81, .hour = 12, .latitude = 45});
    ecs_progress(w, 0);
    const WorldTransform *t = ecs_get(w, sun, WorldTransform);
    vec3 dir = vec3_scale(vec3_normalize(
        mat4_transform_dir(&t->m, vec3_new(0, 0, 1))), -1.0f);

    test_assert(fabsf(dir.y - sinf(45.0f * 3.14159265f / 180.0f)) < 1e-3f);
    test_assert(fabsf(dir.x) < 1e-3f);
    test_assert(dir.z < 0.0f);
}

void Clock_the_sun_rises_in_the_east(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {.day_of_year = 81, .hour = 6, .latitude = 20});
    ecs_progress(w, 0);
    const WorldTransform *t = ecs_get(w, sun, WorldTransform);
    vec3 dir = vec3_scale(vec3_normalize(
        mat4_transform_dir(&t->m, vec3_new(0, 0, 1))), -1.0f);

    test_assert(fabsf(dir.y) < 1e-3f);
    test_assert(fabsf(dir.x - 1.0f) < 1e-3f);
}

void Clock_the_sun_sets_in_the_west(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {.day_of_year = 81, .hour = 18, .latitude = 20});
    ecs_progress(w, 0);
    const WorldTransform *t = ecs_get(w, sun, WorldTransform);
    vec3 dir = vec3_scale(vec3_normalize(
        mat4_transform_dir(&t->m, vec3_new(0, 0, 1))), -1.0f);

    test_assert(fabsf(dir.y) < 1e-3f);
    test_assert(fabsf(dir.x + 1.0f) < 1e-3f);
}

void Clock_the_sun_stands_under_the_horizon_at_midnight(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {.day_of_year = 81, .hour = 0, .latitude = 40});
    ecs_progress(w, 0);
    const WorldTransform *t = ecs_get(w, sun, WorldTransform);
    vec3 dir = vec3_scale(vec3_normalize(
        mat4_transform_dir(&t->m, vec3_new(0, 0, 1))), -1.0f);

    test_assert(fabsf(dir.y + sinf(50.0f * 3.14159265f / 180.0f)) < 1e-3f);
}

void Clock_the_solstice_tilts_the_sun_by_the_axis(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {.day_of_year = 172, .hour = 12});
    ecs_progress(w, 0);
    const WorldTransform *t = ecs_get(w, sun, WorldTransform);
    vec3 dir = vec3_scale(vec3_normalize(
        mat4_transform_dir(&t->m, vec3_new(0, 0, 1))), -1.0f);
    float tilt = BOX_TIME_TILT * 3.14159265f / 180.0f;

    test_assert(fabsf(dir.y - cosf(tilt)) < 2e-3f);
    test_assert(fabsf(dir.z - sinf(tilt)) < 2e-3f);
}

void Clock_the_winter_solstice_tilts_the_sun_the_other_way(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {.day_of_year = 355, .hour = 12});
    ecs_progress(w, 0);
    const WorldTransform *t = ecs_get(w, sun, WorldTransform);
    vec3 dir = vec3_scale(vec3_normalize(
        mat4_transform_dir(&t->m, vec3_new(0, 0, 1))), -1.0f);
    float tilt = BOX_TIME_TILT * 3.14159265f / 180.0f;

    test_assert(fabsf(dir.y - cosf(tilt)) < 2e-3f);
    test_assert(fabsf(dir.z + sinf(tilt)) < 2e-3f);
}

void Clock_the_north_offset_turns_the_compass(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {.day_of_year = 81, .hour = 6, .north_offset = 90});
    ecs_progress(w, 0);
    const WorldTransform *t = ecs_get(w, sun, WorldTransform);
    vec3 dir = vec3_scale(vec3_normalize(
        mat4_transform_dir(&t->m, vec3_new(0, 0, 1))), -1.0f);

    test_assert(fabsf(dir.x) < 1e-3f);
    test_assert(fabsf(dir.z + 1.0f) < 1e-3f);
}

void Clock_a_day_without_a_date_is_the_solstice(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {.hour = 12});
    ecs_progress(w, 0);

    test_flt(ecs_get(w, clock_entity, TimeOfDay)->day_of_year, BOX_TIME_DAY_OF_YEAR);
}

void Clock_time_scale_advances_the_hour(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {.hour = 6, .time_scale = 2});
    ecs_progress(w, 0.5f);

    test_assert(fabsf(ecs_get(w, clock_entity, TimeOfDay)->hour - 7.0f) < 1e-4f);
}

void Clock_a_clock_without_a_scale_holds_the_hour(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {.hour = 6});
    ecs_progress(w, 10.0f);
    ecs_progress(w, 10.0f);

    test_flt(ecs_get(w, clock_entity, TimeOfDay)->hour, 6.0f);
}

void Clock_the_hour_wraps_into_the_next_day(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {.hour = 23.5f, .day_of_year = 81, .time_scale = 1});
    ecs_progress(w, 1.0f);
    const TimeOfDay *t = ecs_get(w, clock_entity, TimeOfDay);

    test_assert(fabsf(t->hour - 0.5f) < 1e-4f);
    test_flt(t->day_of_year, 82.0f);
}

void Clock_the_day_wraps_at_the_end_of_the_year(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {.hour = 23.5f, .day_of_year = 365, .time_scale = 1});
    ecs_progress(w, 1.0f);

    test_flt(ecs_get(w, clock_entity, TimeOfDay)->day_of_year, 1.0f);
}

void Clock_a_negative_scale_runs_the_clock_backwards(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {.hour = 0.5f, .day_of_year = 81, .time_scale = -1});
    ecs_progress(w, 1.0f);
    const TimeOfDay *t = ecs_get(w, clock_entity, TimeOfDay);

    test_assert(fabsf(t->hour - 23.5f) < 1e-4f);
    test_flt(t->day_of_year, 80.0f);
}

void Clock_a_written_hour_jumps_the_sky_in_the_same_frame(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {.day_of_year = 81, .hour = 6});
    ecs_progress(w, 0);
    const WorldTransform *t = ecs_get(w, sun, WorldTransform);
    vec3 east = vec3_scale(vec3_normalize(
        mat4_transform_dir(&t->m, vec3_new(0, 0, 1))), -1.0f);

    ecs_set(w, clock_entity, TimeOfDay, {.day_of_year = 81, .hour = 18});
    ecs_progress(w, 0);
    t = ecs_get(w, sun, WorldTransform);
    vec3 west = vec3_scale(vec3_normalize(
        mat4_transform_dir(&t->m, vec3_new(0, 0, 1))), -1.0f);

    test_assert(east.x > 0.99f);
    test_assert(west.x < -0.99f);
}

void Clock_daylight_is_one_by_day(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {.day_of_year = 81, .hour = 12});
    ecs_progress(w, 0);

    test_flt(ecs_get(w, clock_entity, TimeOfDay)->daylight, 1.0f);
}

void Clock_daylight_is_zero_at_night(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {.day_of_year = 81, .hour = 0});
    ecs_progress(w, 0);

    test_flt(ecs_get(w, clock_entity, TimeOfDay)->daylight, 0.0f);
}

void Clock_daylight_is_half_at_the_horizon(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {.day_of_year = 81, .hour = 6, .latitude = 30});
    ecs_progress(w, 0);

    test_assert(fabsf(ecs_get(w, clock_entity, TimeOfDay)->daylight - 0.5f) < 1e-3f);
}

void Clock_a_full_moon_stands_opposite_the_sun(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {
        .day_of_year = 172, .hour = 0, .latitude = 40, .moon_position_offset = 12});
    ecs_progress(w, 0);
    vec3 s = vec3_normalize(mat4_transform_dir(
        &ecs_get(w, sun, WorldTransform)->m, vec3_new(0, 0, 1)));
    vec3 m = vec3_normalize(mat4_transform_dir(
        &ecs_get(w, moon, WorldTransform)->m, vec3_new(0, 0, 1)));

    test_assert(s.x * m.x + s.y * m.y + s.z * m.z < -0.999f);
}

void Clock_a_new_moon_stands_at_the_sun(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {
        .day_of_year = 172, .hour = 9, .latitude = 40, .moon_position_offset = 0});
    ecs_progress(w, 0);
    vec3 s = vec3_normalize(mat4_transform_dir(
        &ecs_get(w, sun, WorldTransform)->m, vec3_new(0, 0, 1)));
    vec3 m = vec3_normalize(mat4_transform_dir(
        &ecs_get(w, moon, WorldTransform)->m, vec3_new(0, 0, 1)));

    test_assert(s.x * m.x + s.y * m.y + s.z * m.z > 0.999f);
}

void Clock_a_quarter_moon_stands_at_a_right_angle(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {
        .day_of_year = 81, .hour = 9, .latitude = 40, .moon_position_offset = 6});
    ecs_progress(w, 0);
    vec3 s = vec3_normalize(mat4_transform_dir(
        &ecs_get(w, sun, WorldTransform)->m, vec3_new(0, 0, 1)));
    vec3 m = vec3_normalize(mat4_transform_dir(
        &ecs_get(w, moon, WorldTransform)->m, vec3_new(0, 0, 1)));

    test_assert(fabsf(s.x * m.x + s.y * m.y + s.z * m.z) < 1e-3f);
}

void Clock_the_moon_phase_of_the_sky_follows_the_clock(void)
{
    // sky/time_of_day
    ECS_IMPORT(w, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_entity_t camera = ecs_new(w);
    ecs_set(w, camera, Position, {0, 0, 0});
    ecs_set(w, camera, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = camera, .enabled = true});
    ecs_set(w, clock_entity, TimeOfDay, {
        .day_of_year = 81, .hour = 0, .latitude = 40, .moon_position_offset = 12});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);

    test_assert(packet.sky.moon_color[3] > 0.999f);
    box_packet_fini(&packet);
}

void Clock_the_star_field_keeps_its_pole_over_the_hours(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {.day_of_year = 81, .hour = 3, .latitude = 40});
    ecs_progress(w, 0);
    vec3 first = vec3_normalize(mat4_transform_dir(
        &ecs_get(w, stars, WorldTransform)->m, vec3_new(0, 1, 0)));

    ecs_set(w, clock_entity, TimeOfDay, {.day_of_year = 81, .hour = 21, .latitude = 40});
    ecs_progress(w, 0);
    vec3 second = vec3_normalize(mat4_transform_dir(
        &ecs_get(w, stars, WorldTransform)->m, vec3_new(0, 1, 0)));
    float radians = 40.0f * 3.14159265f / 180.0f;

    test_assert(fabsf(first.y - sinf(radians)) < 1e-3f);
    test_assert(fabsf(first.z - cosf(radians)) < 1e-3f);
    test_assert(fabsf(first.x - second.x) < 1e-3f);
    test_assert(fabsf(first.y - second.y) < 1e-3f);
    test_assert(fabsf(first.z - second.z) < 1e-3f);
}

void Clock_the_star_field_turns_with_the_hour(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {.day_of_year = 81, .hour = 0, .latitude = 40});
    ecs_progress(w, 0);
    vec3 first = vec3_normalize(mat4_transform_dir(
        &ecs_get(w, stars, WorldTransform)->m, vec3_new(1, 0, 0)));

    ecs_set(w, clock_entity, TimeOfDay, {.day_of_year = 81, .hour = 6, .latitude = 40});
    ecs_progress(w, 0);
    vec3 second = vec3_normalize(mat4_transform_dir(
        &ecs_get(w, stars, WorldTransform)->m, vec3_new(1, 0, 0)));

    test_assert(fabsf(first.x * second.x + first.y * second.y +
        first.z * second.z) < 1e-3f);
}

void Clock_a_clock_without_lights_of_its_own_takes_them_from_the_sky(void)
{
    // sky/time_of_day
    ecs_set(w, clock_entity, TimeOfDay, {.day_of_year = 81, .hour = 12});
    ecs_progress(w, 0);
    const WorldTransform *t = ecs_get(w, sun, WorldTransform);
    vec3 dir = vec3_scale(vec3_normalize(
        mat4_transform_dir(&t->m, vec3_new(0, 0, 1))), -1.0f);

    test_assert(fabsf(dir.y - 1.0f) < 1e-3f);
}

void Clock_a_clock_that_names_its_own_lights_drives_those(void)
{
    // sky/time_of_day
    ecs_entity_t other = ecs_new(w);
    ecs_set(w, other, Rotation, {0, 0, 0});
    ecs_set(w, other, DirectionalLight, {.intensity = 1});
    ecs_set(w, clock_entity, TimeOfDay, {
        .light = other, .day_of_year = 81, .hour = 12});
    ecs_progress(w, 0);
    vec3 dir = vec3_scale(vec3_normalize(mat4_transform_dir(
        &ecs_get(w, other, WorldTransform)->m, vec3_new(0, 0, 1))), -1.0f);
    vec3 idle = vec3_normalize(mat4_transform_dir(
        &ecs_get(w, sun, WorldTransform)->m, vec3_new(0, 0, 1)));

    test_assert(fabsf(dir.y - 1.0f) < 1e-3f);
    test_assert(fabsf(idle.z - 1.0f) < 1e-3f);
}

void Clock_a_light_that_was_authored_without_a_rotation_gets_one(void)
{
    // sky/time_of_day
    ecs_entity_t bare = ecs_new(w);
    ecs_set(w, bare, DirectionalLight, {.intensity = 1});
    ecs_set(w, clock_entity, TimeOfDay, {
        .light = bare, .day_of_year = 81, .hour = 12});
    ecs_progress(w, 0);

    test_assert(ecs_get(w, bare, Rotation) != NULL);
    vec3 dir = vec3_scale(vec3_normalize(mat4_transform_dir(
        &ecs_get(w, bare, WorldTransform)->m, vec3_new(0, 0, 1))), -1.0f);
    test_assert(fabsf(dir.y - 1.0f) < 1e-3f);
}

void Clock_a_world_without_an_atmosphere_still_runs_its_clock(void)
{
    // sky/time_of_day
    ecs_delete(w, atmosphere);
    ecs_set(w, clock_entity, TimeOfDay, {
        .light = sun, .day_of_year = 81, .hour = 12, .time_scale = 1});
    ecs_progress(w, 1.0f);
    const TimeOfDay *t = ecs_get(w, clock_entity, TimeOfDay);

    test_assert(fabsf(t->hour - 13.0f) < 1e-4f);
    test_flt(t->daylight, 1.0f);
}
