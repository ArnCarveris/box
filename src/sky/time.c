/**
 * @file sky/time.c
 * @brief The simulated clock that drives the sun, the moon and the star field.
 *
 * The clock turns the hour, the date and the latitude of a world into the
 * direction of the two bodies and the orientation of the celestial sphere, and
 * writes them as rotations. Everything is derived from the component every
 * frame, so writing the hour from a script or a terminal jumps the sky in the
 * same frame instead of on the next one.
 */

#include "sky_internal.h"
#include <math.h>

/** Radians in one degree */
#define BOX_TIME_RADIANS (3.14159265f / 180.0f)

/** Half a turn in radians, the ecliptic a moon twelve hours behind stands at */
#define BOX_TIME_PI (3.14159265f)

/** Hours in one day of the clock */
#define BOX_TIME_HOURS (24.0f)

/** Day of the year the ecliptic longitude of the sun is measured from */
#define BOX_TIME_EQUINOX (284.0f)

/** Hours the moon trails the sun over a full turn of its phase */
#define BOX_TIME_MOON_TURN (12.0f)

/** Hermite interpolation between two edges, the smoothstep of the shaders.
 *
 * @param low The edge the result leaves zero at
 * @param high The edge the result reaches one at
 * @param x The value
 * @return The interpolant
 */
static float box_time_smooth(
    float low,
    float high,
    float x)
{
    float t = (x - low) / (high - low);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);

    return t * t * (3.0f - 2.0f * t);
}

/** Ecliptic longitude of the sun on a day of the year, in radians.
 *
 * @param day The day of the year
 * @return The longitude, zero at the March equinox
 */
static float box_time_longitude(
    float day)
{
    return 2.0f * BOX_TIME_PI * (BOX_TIME_EQUINOX + day) / BOX_TIME_YEAR;
}

/** Rotation that carries the equatorial frame of a world into its own axes.
 *
 * The pole of the frame stands at the latitude over the northern horizon and
 * the compass of the world is turned by the offset, so one matrix places every
 * body and the stars alike.
 *
 * @param latitude Latitude in degrees
 * @param north Degrees the compass is turned about the world Y axis
 * @return The rotation
 */
static mat4 box_time_frame(
    float latitude,
    float north)
{
    mat4 tilt = mat4_rotation_x((90.0f - latitude) * BOX_TIME_RADIANS);
    mat4 compass = mat4_rotation_y(north * BOX_TIME_RADIANS);

    return mat4_mul(&compass, &tilt);
}

/** Direction of a body from its hour angle and declination.
 *
 * @param frame The equatorial frame of the world
 * @param hour_angle Hours the body stands east or west of the meridian
 * @param declination Declination of the body in degrees
 * @return The direction from the camera toward the body
 */
static vec3 box_time_direction(
    const mat4 *frame,
    float hour_angle,
    float declination)
{
    float h = hour_angle * BOX_TIME_HOUR_ANGLE * BOX_TIME_RADIANS;
    float d = declination * BOX_TIME_RADIANS;
    vec3 local = vec3_new(-sinf(h) * cosf(d), sinf(d), -cosf(h) * cosf(d));

    return vec3_normalize(mat4_transform_dir(frame, local));
}

/** Point an entity so the light it casts travels away from a body.
 *
 * The rotation is written through the stage, because a light that was
 * authored without one is given a Rotation by the clock, which moves it to
 * another table while the world is read only.
 *
 * @param stage The stage the clock writes through
 * @param e The entity, 0 for none
 * @param dir The direction from the camera toward the body
 */
static void box_time_face(
    ecs_world_t *stage,
    ecs_entity_t e,
    vec3 dir)
{
    if (!e) {
        goto done;
    }

    vec3 travel = vec3_scale(dir, -1.0f);
    vec3 up = fabsf(travel.y) > 0.999f ? vec3_new(0, 0, 1) : vec3_new(0, 1, 0);
    mat4 facing = mat4_facing(travel, up);
    vec3 euler = mat4_to_euler_xyz(&facing);
    ecs_set(stage, e, Rotation, { euler.x, euler.y, euler.z });
done:
    return;
}

/** Turn the celestial sphere to the hour and the date of the clock.
 *
 * @param stage The stage the clock writes through
 * @param e The entity holding the Stars component, 0 for none
 * @param frame The equatorial frame of the world
 * @param t The clock
 */
static void box_time_stars(
    ecs_world_t *stage,
    ecs_entity_t e,
    const mat4 *frame,
    const TimeOfDay *t)
{
    if (!e) {
        goto done;
    }

    float day = box_sky_or(t->day_of_year, BOX_TIME_DAY_OF_YEAR);
    float spin = (t->hour - 12.0f) * BOX_TIME_HOUR_ANGLE * BOX_TIME_RADIANS +
        2.0f * BOX_TIME_PI * day / BOX_TIME_YEAR;
    mat4 turn = mat4_rotation_y(spin);
    mat4 basis = mat4_mul(frame, &turn);
    vec3 euler = mat4_to_euler_xyz(&basis);
    ecs_set(stage, e, Rotation, { euler.x, euler.y, euler.z });
done:
    return;
}

/** Find the entity of a body, falling back to the atmosphere of the clock.
 *
 * @param world The world
 * @param named The entity the clock names, 0 for none
 * @param a The atmosphere, NULL when the world has none
 * @param field Offset of the field of the atmosphere that names it
 * @return The entity, 0 when neither names one
 */
static ecs_entity_t box_time_body(
    ecs_world_t *world,
    ecs_entity_t named,
    const Atmosphere *a,
    size_t field)
{
    ecs_entity_t result = named;
    if (!result && a) {
        result = *(const ecs_entity_t*)((const char*)a + field);
    }

    return result && ecs_is_alive(world, result) ? result : 0;
}

/** Advance one clock and write the sky it describes.
 *
 * @param world The world the clock reads
 * @param stage The stage the clock writes the rotations through
 * @param t The clock
 * @param delta Seconds the frame took
 */
static void box_time_step(
    ecs_world_t *world,
    ecs_world_t *stage,
    TimeOfDay *t,
    float delta)
{
    t->hour += t->time_scale * delta;
    float day = box_sky_or(t->day_of_year, BOX_TIME_DAY_OF_YEAR);
    while (t->hour >= BOX_TIME_HOURS) {
        t->hour -= BOX_TIME_HOURS;
        day += 1.0f;
    }

    while (t->hour < 0.0f) {
        t->hour += BOX_TIME_HOURS;
        day -= 1.0f;
    }

    while (day > BOX_TIME_YEAR) {
        day -= BOX_TIME_YEAR;
    }

    while (day < 1.0f) {
        day += BOX_TIME_YEAR;
    }

    t->day_of_year = day;

    ecs_entity_t atmosphere = t->atmosphere && ecs_is_alive(world, t->atmosphere) ?
        t->atmosphere : box_sky_entity(world);
    const Atmosphere *a = atmosphere ? ecs_get(world, atmosphere, Atmosphere) : NULL;
    mat4 frame = box_time_frame(t->latitude, t->north_offset);
    float longitude = box_time_longitude(day);
    float phase = t->moon_position_offset * BOX_TIME_PI / BOX_TIME_MOON_TURN;
    float sun_dec = BOX_TIME_TILT * sinf(longitude);
    float moon_dec = BOX_TIME_TILT * sinf(longitude + phase);
    vec3 sun = box_time_direction(&frame, t->hour - 12.0f, sun_dec);
    vec3 moon = box_time_direction(&frame,
        t->hour - 12.0f - t->moon_position_offset, moon_dec);

    box_time_face(stage, box_time_body(world, t->light, a,
        offsetof(Atmosphere, sun)), sun);
    box_time_face(stage, box_time_body(world, t->moon_light, a,
        offsetof(Atmosphere, moon)), moon);
    box_time_stars(stage, box_time_body(world, t->stars, a,
        offsetof(Atmosphere, stars)), &frame, t);
    t->daylight = box_time_smooth(BOX_TIME_DAYLIGHT_LOW, BOX_TIME_DAYLIGHT_HIGH, sun.y);
}

/** Advance every clock of the world and write the sky it describes.
 *
 * @param it The system iterator
 */
static void BoxSkyTimeUpdate(
    ecs_iter_t *it)
{
    TimeOfDay *t = ecs_field(it, TimeOfDay, 0);

    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_time_step(it->world, it->stage, &t[i], it->delta_time);
    }
}

void box_sky_time_init(
    ecs_world_t *world)
{
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxSkyTimeUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {{ .id = ecs_id(TimeOfDay), .inout = EcsInOut }},
        .callback = BoxSkyTimeUpdate
    });
}
