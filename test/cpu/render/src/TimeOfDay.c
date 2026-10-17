/**
 * @file TimeOfDay.c
 * @brief Tests for TimeOfDay, its status, the sun and moon positions and the lights it points.
 *
 * Positions are observed through TimeOfDayStatus, the lights through their WorldTransform and
 * the views of the packets the null backend receives.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <string.h>
#include "render.h"

/** Check that two values are within a tolerance of each other.
 *
 * @param a The first value
 * @param b The second value
 * @param eps The tolerance
 */
static void test_near(
    float a,
    float b,
    float eps)
{
    if (fabsf(a - b) > eps) {
        printf("%f != %f\n", (double)a, (double)b);
    }

    test_assert(fabsf(a - b) <= eps);
}

/** Create a world with Box and the null backend.
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

/** Create a scene root with a TimeOfDay.
 *
 * @param world The world
 * @param t The time of day
 * @return The scene
 */
static ecs_entity_t scene_new(
    ecs_world_t *world,
    const TimeOfDay *t)
{
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    ecs_set_ptr(world, scene, TimeOfDay, t);

    return scene;
}

/** Create a white directional light in a scene, pointing straight down.
 *
 * @param world The world
 * @param scene The scene
 * @return The light
 */
static ecs_entity_t light_new(
    ecs_world_t *world,
    ecs_entity_t scene)
{
    ecs_entity_t light = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, light, DirectionalLight, {.intensity = 1});
    ecs_set(world, light, Rgb, {255, 255, 255});
    ecs_set(world, light, Rotation, {glm_rad(90.0f), 0, 0});

    return light;
}

/** Create a view whose camera is in a scene.
 *
 * @param world The world
 * @param scene The scene of the camera
 */
static void view_new(
    ecs_world_t *world,
    ecs_entity_t scene)
{
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Position, {0, 2, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true});
}

/** Run a frame and get the view of a scene in the packet the backend received.
 *
 * @param world The world
 * @param null The backend
 * @param scene The scene of the view
 * @return The view
 */
static const box_render_view_t* frame_view(
    ecs_world_t *world,
    box_render_null_t *null,
    ecs_entity_t scene)
{
    const box_render_view_t *result = NULL;
    int32_t i;
    ecs_progress(world, 0);
    box_render_sync(world);
    for (i = 0; i < ecs_vec_count(&null->last.views); i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&null->last.views, box_render_view_t, i);
        if (v->scene == scene) {
            result = v;
        }
    }

    test_assert(result != NULL);

    return result;
}

/** Get the default TimeOfDay values.
 *
 * @return The values
 */
static TimeOfDay tod_defaults(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, TimeOfDay);
    TimeOfDay result = *ecs_get(world, e, TimeOfDay);
    ecs_fini(world);

    return result;
}

/** Get the status of a time of day as it is set.
 *
 * @param t The time of day
 * @return The status
 */
static TimeOfDayStatus tod_status(
    const TimeOfDay *t)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t scene = scene_new(world, t);
    const TimeOfDayStatus *status = ecs_get(world, scene, TimeOfDayStatus);
    test_assert(status != NULL);
    TimeOfDayStatus result = *status;
    ecs_fini(world);

    return result;
}

/** Get the status of a default time of day at a day, hour and latitude.
 *
 * @param day The day of the year
 * @param hour The hour
 * @param latitude The latitude
 * @return The status
 */
static TimeOfDayStatus tod_at(
    int32_t day,
    float hour,
    float latitude)
{
    TimeOfDay t = tod_defaults();
    t.day_of_year = day;
    t.hour = hour;
    t.latitude = latitude;

    return tod_status(&t);
}

/** Run a world headless for a number of frames at 60 frames per second.
 *
 * @param world The world
 * @param frames The number of frames
 */
static void run_frames(
    ecs_world_t *world,
    int32_t frames)
{
    char count[16];
    ecs_os_snprintf(count, sizeof(count), "%d", frames);
    char *argv[] = {"boxrun", "--headless", "--frames", count, "--fps", "60"};
    test_int(box_app_parse(world, 6, argv), 0);
    App *app = ecs_singleton_get_mut(world, App);
    app->unguarded = true;
    app->unpaced = true;
    test_int(box_app_run(world), 0);
}

/** Get the status of a time of day after a number of headless frames.
 *
 * @param t The time of day
 * @param frames The number of frames
 * @return The status
 */
static TimeOfDayStatus tod_after(
    const TimeOfDay *t,
    int32_t frames)
{
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    ecs_entity_t scene = scene_new(world, t);
    run_frames(world, frames);
    TimeOfDayStatus result = *ecs_get(world, scene, TimeOfDayStatus);
    ecs_fini(world);
    box_render_null_free(null);

    return result;
}

/** Check that a light travels from a position in the sky along its local +Z.
 *
 * @param world The world
 * @param light The light
 * @param north World yaw of north in degrees
 * @param altitude Altitude of the position in degrees
 * @param azimuth Azimuth of the position in degrees
 */
static void test_points_from(
    ecs_world_t *world,
    ecs_entity_t light,
    float north,
    float altitude,
    float azimuth)
{
    const WorldTransform *wt = ecs_get(world, light, WorldTransform);
    test_assert(wt != NULL);
    float alt = glm_rad(altitude), yaw = glm_rad(north - azimuth);
    test_near(-wt->m[2][0], cosf(alt) * sinf(yaw), 1e-4f);
    test_near(-wt->m[2][1], sinf(alt), 1e-4f);
    test_near(-wt->m[2][2], cosf(alt) * cosf(yaw), 1e-4f);
}

/** Count the Rotation OnSet events.
 *
 * @param it The observer iterator
 */
static void count_rotation_set(
    ecs_iter_t *it)
{
    int32_t *count = it->ctx;
    *count += it->count;
}

/** Get the color of the only directional light of a scene without an Atmosphere, with the sun
 * at an hour of the equinox at the equator.
 *
 * @param hour The hour
 * @param out Receives the linear color the views get
 */
static void fallback_color(
    float hour,
    float *out)
{
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);
    TimeOfDay t = tod_defaults();
    t.hour = hour;
    t.latitude = 0;
    ecs_entity_t scene = scene_new(world, &t);
    light_new(world, scene);
    view_new(world, scene);
    const box_render_view_t *v = frame_view(world, null, scene);
    test_int(v->lights.directional, 1);
    glm_vec3_copy(ECS_CONST_CAST(float*, v->lights.sun_color), out);
    ecs_fini(world);
    box_render_null_free(null);
}

void TimeOfDay_script_defaults(void) {
    // atmosphere/time_of_day
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "defaults",
        "using box.view\n"
        "using box.atmosphere\n"
        "scene {\n Scene: {}\n TimeOfDay: {}\n}\n"
        "custom {\n Scene: {}\n TimeOfDay: {hour: 18, latitude: -30, time_scale: 2}\n}\n",
        NULL) == 0);

    ecs_entity_t scene = ecs_lookup(world, "scene");
    const TimeOfDay *t = ecs_get(world, scene, TimeOfDay);
    test_assert(t != NULL);
    test_flt(t->hour, 12);
    test_int(t->day_of_year, 80);
    test_flt(t->latitude, 45);
    test_flt(t->north, 0);
    test_flt(t->time_scale, 0);
    test_flt(t->moon_offset, 0);
    test_uint(t->sun, 0);
    test_uint(t->moon, 0);
    const TimeOfDayStatus *status = ecs_get(world, scene, TimeOfDayStatus);
    test_assert(status != NULL);
    test_flt(status->hour, 12);
    test_int(status->day_of_year, 80);

    ecs_entity_t custom = ecs_lookup(world, "custom");
    t = ecs_get(world, custom, TimeOfDay);
    test_flt(t->hour, 18);
    test_int(t->day_of_year, 80);
    test_flt(t->latitude, -30);
    test_flt(t->time_scale, 2);
    status = ecs_get(world, custom, TimeOfDayStatus);
    test_assert(status != NULL);
    test_flt(status->hour, 18);

    ecs_fini(world);
}

void TimeOfDay_add_defaults(void) {
    // atmosphere/time_of_day
    TimeOfDay t = tod_defaults();
    test_flt(t.hour, 12);
    test_int(t.day_of_year, 80);
    test_flt(t.latitude, 45);
    test_flt(t.north, 0);
    test_flt(t.time_scale, 0);
    test_flt(t.moon_offset, 0);
    test_uint(t.sun, 0);
    test_uint(t.moon, 0);
}

void TimeOfDay_status_follows_time_of_day(void) {
    // atmosphere/time_of_day
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    test_assert(!ecs_has(world, scene, TimeOfDayStatus));
    ecs_add(world, scene, TimeOfDay);
    const TimeOfDayStatus *status = ecs_get(world, scene, TimeOfDayStatus);
    test_assert(status != NULL);
    test_flt(status->hour, 12);
    test_int(status->day_of_year, 80);
    test_near(status->sun_altitude, 45, 1);
    test_assert(status->daylight > 0.9f);

    ecs_remove(world, scene, TimeOfDay);
    test_assert(!ecs_has(world, scene, TimeOfDayStatus));

    ecs_fini(world);
}

void TimeOfDay_singleton_has_status(void) {
    // atmosphere/time_of_day
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_singleton_set(world, TimeOfDay, {.hour = 0, .day_of_year = 80, .latitude = 45});
    test_assert(ecs_has(world, ecs_id(TimeOfDay), TimeOfDayStatus));
    ecs_progress(world, 0);
    const TimeOfDayStatus *status = ecs_get(world, ecs_id(TimeOfDay), TimeOfDayStatus);
    test_flt(status->hour, 0);
    test_near(status->sun_altitude, -45, 1);
    test_assert(status->daylight < 0.06f);

    ecs_singleton_remove(world, TimeOfDay);
    test_assert(!ecs_has(world, ecs_id(TimeOfDay), TimeOfDayStatus));

    ecs_fini(world);
    box_render_null_free(null);
}

void TimeOfDay_equator_equinox_noon_zenith(void) {
    // atmosphere/time_of_day
    TimeOfDayStatus s = tod_at(80, 12, 0);
    test_assert(s.sun_altitude > 89.0f);
    test_assert(s.sun_altitude <= 90.0f);
    test_assert(s.daylight > 0.95f);
}

void TimeOfDay_equator_equinox_sunrise_east_sunset_west(void) {
    // atmosphere/time_of_day
    TimeOfDayStatus s = tod_at(80, 6, 0);
    test_near(s.sun_altitude, 0, 1);
    test_near(s.sun_azimuth, 90, 1);

    s = tod_at(80, 18, 0);
    test_near(s.sun_altitude, 0, 1);
    test_near(s.sun_azimuth, 270, 1);

    s = tod_at(80, 7, 0);
    test_near(s.sun_altitude, 15, 1);
}

void TimeOfDay_latitude_52_noon_altitudes(void) {
    // atmosphere/time_of_day
    TimeOfDayStatus s = tod_at(80, 12, 52);
    test_near(s.sun_altitude, 38, 1);
    test_near(s.sun_azimuth, 180, 1);

    s = tod_at(172, 12, 52);
    test_near(s.sun_altitude, 61.4f, 0.5f);
    test_near(s.sun_azimuth, 180, 1);

    s = tod_at(355, 12, 52);
    test_near(s.sun_altitude, 14.6f, 0.5f);
}

void TimeOfDay_time_scale_advances_hour(void) {
    // atmosphere/time_of_day
    TimeOfDay t = tod_defaults();
    t.hour = 10;
    t.time_scale = 2;
    TimeOfDayStatus a = tod_after(&t, 90);
    test_near(a.hour, 13, 1e-3f);
    test_int(a.day_of_year, 80);
    test_assert(a.sun_altitude < tod_at(80, 12, 45).sun_altitude);
    test_assert(a.sun_azimuth > 180);

    TimeOfDayStatus b = tod_after(&t, 90);
    test_assert(!memcmp(&a, &b, sizeof(TimeOfDayStatus)));

    t.time_scale = -2;
    a = tod_after(&t, 90);
    test_near(a.hour, 7, 1e-3f);
}

void TimeOfDay_time_scale_wraps_day_and_year(void) {
    // atmosphere/time_of_day
    TimeOfDay t = tod_defaults();
    t.hour = 23.5f;
    t.day_of_year = 100;
    t.time_scale = 1;
    TimeOfDayStatus s = tod_after(&t, 60);
    test_near(s.hour, 0.5f, 1e-3f);
    test_int(s.day_of_year, 101);

    t.day_of_year = 365;
    s = tod_after(&t, 60);
    test_near(s.hour, 0.5f, 1e-3f);
    test_int(s.day_of_year, 1);

    t.hour = 0.5f;
    t.day_of_year = 1;
    t.time_scale = -1;
    s = tod_after(&t, 60);
    test_near(s.hour, 23.5f, 1e-3f);
    test_int(s.day_of_year, 365);
}

void TimeOfDay_set_restarts_status(void) {
    // atmosphere/time_of_day
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    TimeOfDay t = tod_defaults();
    t.time_scale = 1;
    ecs_entity_t scene = scene_new(world, &t);
    run_frames(world, 30);
    test_near(ecs_get(world, scene, TimeOfDayStatus)->hour, 12.5f, 1e-3f);

    t.hour = 30;
    t.time_scale = 0;
    ecs_set_ptr(world, scene, TimeOfDay, &t);
    const TimeOfDayStatus *status = ecs_get(world, scene, TimeOfDayStatus);
    test_flt(status->hour, 6);
    test_int(status->day_of_year, 81);
    ecs_progress(world, 1.0f);
    status = ecs_get(world, scene, TimeOfDayStatus);
    test_flt(status->hour, 6);
    test_int(status->day_of_year, 81);

    ecs_fini(world);
    box_render_null_free(null);
}

void TimeOfDay_full_moon_illumination(void) {
    // atmosphere/time_of_day
    TimeOfDayStatus s = tod_at(21, 4.67f, 45);
    test_assert(s.moon_illumination > 0.99f);

    TimeOfDay t = tod_defaults();
    t.day_of_year = 6;
    t.hour = 18.23f;
    t.moon_offset = 346.4f;
    s = tod_status(&t);
    test_assert(s.moon_illumination > 0.99f);
    test_assert(s.sun_altitude < 0);
}

void TimeOfDay_new_moon_illumination(void) {
    // atmosphere/time_of_day
    TimeOfDayStatus s = tod_at(6, 18.23f, 45);
    test_assert(s.moon_illumination < 0.01f);

    s = tod_at(14, 12, 45);
    test_assert(s.moon_illumination > 0.3f);
    test_assert(s.moon_illumination < 0.7f);
}

void TimeOfDay_paused_sets_no_rotation(void) {
    // atmosphere/time_of_day
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    TimeOfDay t = tod_defaults();
    ecs_entity_t scene = scene_new(world, &t);
    ecs_entity_t moon = light_new(world, scene);
    ecs_entity_t sun = light_new(world, scene);
    t.moon = moon;
    ecs_set_ptr(world, scene, TimeOfDay, &t);
    view_new(world, scene);
    ecs_progress(world, 0);
    TimeOfDayStatus first = *ecs_get(world, scene, TimeOfDayStatus);
    Rotation sun_r = *ecs_get(world, sun, Rotation);

    int32_t count = 0;
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Rotation) }},
        .events = { EcsOnSet },
        .callback = count_rotation_set,
        .ctx = &count
    });
    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
    }

    test_int(count, 0);
    test_assert(!memcmp(&first, ecs_get(world, scene, TimeOfDayStatus),
        sizeof(TimeOfDayStatus)));
    test_assert(!memcmp(&sun_r, ecs_get(world, sun, Rotation), sizeof(Rotation)));

    t.hour = 13;
    ecs_set_ptr(world, scene, TimeOfDay, &t);
    ecs_progress(world, 0);
    test_int(count, 2);

    ecs_fini(world);
    box_render_null_free(null);
}

void TimeOfDay_sun_rotation_follows_status(void) {
    // atmosphere/time_of_day
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    TimeOfDay t = tod_defaults();
    t.hour = 9;
    t.latitude = 30;
    t.time_scale = 1;
    ecs_entity_t scene = scene_new(world, &t);
    ecs_entity_t sun = light_new(world, scene);
    run_frames(world, 30);
    const TimeOfDayStatus *s = ecs_get(world, scene, TimeOfDayStatus);
    test_near(s->hour, 9.5f, 1e-3f);
    test_assert(s->sun_altitude > 20);
    test_assert(s->sun_azimuth > 90 && s->sun_azimuth < 180);
    test_points_from(world, sun, 0, s->sun_altitude, s->sun_azimuth);
    test_assert(ecs_get(world, sun, WorldTransform)->m[2][0] > 0);

    t.north = 90;
    t.time_scale = 0;
    ecs_set_ptr(world, scene, TimeOfDay, &t);
    ecs_progress(world, 0);
    s = ecs_get(world, scene, TimeOfDayStatus);
    test_flt(s->hour, 9);
    test_points_from(world, sun, 90, s->sun_altitude, s->sun_azimuth);
    test_assert(ecs_get(world, sun, WorldTransform)->m[2][2] < 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void TimeOfDay_moon_rotation_follows_status(void) {
    // atmosphere/time_of_day
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    TimeOfDay t = tod_defaults();
    t.day_of_year = 21;
    t.hour = 1;
    ecs_entity_t scene = scene_new(world, &t);
    ecs_entity_t moon = light_new(world, scene);
    ecs_entity_t sun = light_new(world, scene);
    t.moon = moon;
    ecs_set_ptr(world, scene, TimeOfDay, &t);
    ecs_progress(world, 0);
    const TimeOfDayStatus *s = ecs_get(world, scene, TimeOfDayStatus);
    test_assert(s->moon_altitude > 0);
    test_assert(s->sun_altitude < 0);
    test_points_from(world, moon, 0, s->moon_altitude, s->moon_azimuth);
    test_points_from(world, sun, 0, s->sun_altitude, s->sun_azimuth);

    t.moon_offset = 6;
    ecs_set_ptr(world, scene, TimeOfDay, &t);
    ecs_progress(world, 0);
    s = ecs_get(world, scene, TimeOfDayStatus);
    test_points_from(world, moon, 0, s->moon_altitude, s->moon_azimuth);
    test_assert(s->moon_azimuth > 180);

    ecs_fini(world);
    box_render_null_free(null);
}

void TimeOfDay_atmosphere_sun_and_moon(void) {
    // atmosphere/time_of_day
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    TimeOfDay t = tod_defaults();
    t.day_of_year = 21;
    t.hour = 6.5f;
    ecs_entity_t scene = scene_new(world, &t);
    ecs_entity_t other = light_new(world, scene);
    ecs_entity_t sun = light_new(world, scene);
    ecs_entity_t moon = light_new(world, scene);
    ecs_set(world, scene, Atmosphere, {.sun = sun, .moon = moon});
    ecs_progress(world, 0);
    const TimeOfDayStatus *s = ecs_get(world, scene, TimeOfDayStatus);
    test_points_from(world, sun, 0, s->sun_altitude, s->sun_azimuth);
    test_points_from(world, moon, 0, s->moon_altitude, s->moon_azimuth);
    test_points_from(world, other, 0, 90, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void TimeOfDay_prefab_instance_gets_owned_rotation(void) {
    // atmosphere/time_of_day
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, prefab, DirectionalLight, {.intensity = 1});
    ecs_set(world, prefab, Rgb, {255, 255, 255});

    TimeOfDay t = tod_defaults();
    ecs_entity_t scene = scene_new(world, &t);
    ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_add_pair(world, sun, EcsIsA, prefab);
    test_assert(!ecs_has(world, sun, Rotation));
    ecs_progress(world, 0);

    test_assert(ecs_owns(world, sun, Rotation));
    test_assert(!ecs_has(world, prefab, Rotation));
    const TimeOfDayStatus *s = ecs_get(world, scene, TimeOfDayStatus);
    test_points_from(world, sun, 0, s->sun_altitude, s->sun_azimuth);

    ecs_fini(world);
    box_render_null_free(null);
}

void TimeOfDay_fallback_red_sunset(void) {
    // atmosphere/time_of_day
    float noon[3], sunset[3];
    fallback_color(12, noon);
    fallback_color(17.75f, sunset);
    test_assert(noon[0] > 0.95f * GLM_PIf);
    test_assert(noon[2] > 0.9f * noon[0]);
    test_assert(sunset[0] > 0);
    test_assert(sunset[0] < noon[0]);
    test_assert(sunset[0] > sunset[1]);
    test_assert(sunset[1] > sunset[2]);
    test_assert(sunset[2] < 0.5f * sunset[0]);
}

void TimeOfDay_fallback_dark_night(void) {
    // atmosphere/time_of_day
    float night[3];
    fallback_color(0, night);
    test_flt(night[0], 0);
    test_flt(night[1], 0);
    test_flt(night[2], 0);
}

void TimeOfDay_fallback_leaves_authored_light(void) {
    // atmosphere/time_of_day
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    TimeOfDay t = tod_defaults();
    t.hour = 0;
    ecs_entity_t plain = scene_new(world, &t);
    ecs_entity_t sun = light_new(world, plain);
    view_new(world, plain);
    const box_render_view_t *v = frame_view(world, null, plain);
    test_flt(v->lights.sun_color[0], 0);
    test_flt(ecs_get(world, sun, DirectionalLight)->intensity, 1);
    test_int(ecs_get(world, sun, Rgb)->r, 255);
    test_int(ecs_get(world, sun, Rgb)->g, 255);
    test_int(ecs_get(world, sun, Rgb)->b, 255);

    ecs_remove(world, plain, TimeOfDay);
    v = frame_view(world, null, plain);
    test_near(v->lights.sun_color[0], GLM_PIf, 1e-5f);
    test_near(v->lights.sun_color[2], GLM_PIf, 1e-5f);

    ecs_fini(world);
    box_render_null_free(null);
}
