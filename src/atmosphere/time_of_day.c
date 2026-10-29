/**
 * @file atmosphere/time_of_day.c
 * @brief Advances the TimeOfDay of every scene and points its sun and moon lights.
 *
 * The sun and moon positions come from Paul Schlyter's orbital elements ("How to compute
 * planetary positions"), for the year 2000 at Greenwich longitude.
 */

#include "atmosphere.h"
#include "../render/render.h"
#include <math.h>

/** Days in the year of the time of day */
#define BOX_TOD_DAYS (365)

/** Angular radius of the sun disk in radians, over which the fallback fades the light */
#define BOX_TOD_DISK_RADIUS (0.00465f)

/** Degrees to radians, in double precision */
#define BOX_TOD_RAD (GLM_PI / 180.0)

/** Position of a body in the local sky, in radians */
typedef struct box_tod_body_t {
    double altitude;        /**< Angle above the horizon */
    double azimuth;         /**< Angle from north toward east */
} box_tod_body_t;

/** Geocentric position of a body at a time */
typedef struct box_tod_ecliptic_t {
    double ecliptic[3];     /**< Unit direction in ecliptic coordinates */
    double ra;              /**< Right ascension in radians */
    double dec;             /**< Declination in radians */
} box_tod_ecliptic_t;

/** State of the time of day system */
typedef struct box_tod_state_t {
    ecs_query_t *lights;    /**< Entities with DirectionalLight and the scene they are in */
    ecs_query_t *atmospheres; /**< Entities with Atmosphere and the scene they are in */
} box_tod_state_t;

/** Wrap an angle in degrees into 0 to 360.
 *
 * @param d The angle
 * @return The wrapped angle
 */
static double box_tod_norm_deg(
    double d)
{
    d = fmod(d, 360.0);

    return d < 0 ? d + 360.0 : d;
}

/** Smoothstep from 0 to 1 of a value clamped to 0 to 1.
 *
 * @param t The value
 * @return The smoothed value
 */
static float box_tod_smoothstep(
    float t)
{
    t = glm_clamp(t, 0.0f, 1.0f);

    return t * t * (3.0f - 2.0f * t);
}

/** Set a body from its ecliptic longitude and latitude, rotated to the equator by the
 * obliquity of the ecliptic.
 *
 * @param lon Ecliptic longitude in radians
 * @param lat Ecliptic latitude in radians
 * @param obliquity Obliquity of the ecliptic in radians
 * @param out Receives the body
 */
static void box_tod_equatorial(
    double lon,
    double lat,
    double obliquity,
    box_tod_ecliptic_t *out)
{
    double x = cos(lat) * cos(lon), y = cos(lat) * sin(lon), z = sin(lat);
    double ye = y * cos(obliquity) - z * sin(obliquity);
    double ze = y * sin(obliquity) + z * cos(obliquity);
    out->ecliptic[0] = x;
    out->ecliptic[1] = y;
    out->ecliptic[2] = z;
    out->ra = atan2(ye, x);
    out->dec = atan2(ze, sqrt(x * x + ye * ye));
}

/** Solve Kepler's equation for the eccentric anomaly.
 *
 * @param m Mean anomaly in radians
 * @param e Eccentricity
 * @return Eccentric anomaly in radians
 */
static double box_tod_kepler(
    double m,
    double e)
{
    double ea = m + e * sin(m) * (1.0 + e * cos(m));
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ea -= (ea - e * sin(ea) - m) / (1.0 - e * cos(ea));
    }

    return ea;
}

/** Compute the geocentric sun and moon at a time, the moon with its largest perturbations in
 * longitude and latitude.
 *
 * @param d Days since 2000 Jan 0.0 UT, Schlyter's day number
 * @param sun Receives the sun
 * @param moon Receives the moon
 */
static void box_tod_bodies(
    double d,
    box_tod_ecliptic_t *sun,
    box_tod_ecliptic_t *moon)
{
    double obliquity = (23.4393 - 3.563e-7 * d) * BOX_TOD_RAD;

    double ws = box_tod_norm_deg(282.9404 + 4.70935e-5 * d);
    double es = 0.016709 - 1.151e-9 * d;
    double ms = box_tod_norm_deg(356.0470 + 0.9856002585 * d);
    double eas = box_tod_kepler(ms * BOX_TOD_RAD, es);
    double vs = atan2(sqrt(1.0 - es * es) * sin(eas), cos(eas) - es);
    double sun_lon = vs + ws * BOX_TOD_RAD;
    box_tod_equatorial(sun_lon, 0, obliquity, sun);

    double n = box_tod_norm_deg(125.1228 - 0.0529538083 * d) * BOX_TOD_RAD;
    double inc = 5.1454 * BOX_TOD_RAD;
    double w = box_tod_norm_deg(318.0634 + 0.1643573223 * d) * BOX_TOD_RAD;
    double e = 0.054900;
    double mm = box_tod_norm_deg(115.3654 + 13.0649929509 * d) * BOX_TOD_RAD;
    double ea = box_tod_kepler(mm, e);
    double v = atan2(sqrt(1.0 - e * e) * sin(ea), cos(ea) - e);
    double xh = cos(n) * cos(v + w) - sin(n) * sin(v + w) * cos(inc);
    double yh = sin(n) * cos(v + w) + cos(n) * sin(v + w) * cos(inc);
    double zh = sin(v + w) * sin(inc);
    double lon = atan2(yh, xh);
    double lat = atan2(zh, sqrt(xh * xh + yh * yh));

    double msr = ms * BOX_TOD_RAD;
    double ls = msr + ws * BOX_TOD_RAD;
    double lm = n + w + mm;
    double dm = lm - ls;
    double f = lm - n;
    lon += (-1.274 * sin(mm - 2 * dm) + 0.658 * sin(2 * dm) - 0.186 * sin(msr) -
        0.059 * sin(2 * mm - 2 * dm) - 0.057 * sin(mm - 2 * dm + msr) +
        0.053 * sin(mm + 2 * dm) + 0.046 * sin(2 * dm - msr) + 0.041 * sin(mm - msr) -
        0.035 * sin(dm) - 0.031 * sin(mm + msr) - 0.015 * sin(2 * f - 2 * dm) +
        0.011 * sin(mm - 4 * dm)) * BOX_TOD_RAD;
    lat += (-0.173 * sin(f - 2 * dm) - 0.055 * sin(mm - f - 2 * dm) -
        0.046 * sin(mm + f - 2 * dm) + 0.033 * sin(f + 2 * dm) +
        0.017 * sin(2 * mm + f)) * BOX_TOD_RAD;
    box_tod_equatorial(lon, lat, obliquity, moon);
}

/** Compute the altitude and azimuth of a body from its hour angle.
 *
 * @param body The body
 * @param hour_angle Hour angle in radians, positive west of the meridian
 * @param latitude Latitude in radians
 * @param out Receives the position
 */
static void box_tod_horizontal(
    const box_tod_ecliptic_t *body,
    double hour_angle,
    double latitude,
    box_tod_body_t *out)
{
    double sin_alt = sin(latitude) * sin(body->dec) +
        cos(latitude) * cos(body->dec) * cos(hour_angle);
    out->altitude = asin(fmax(-1.0, fmin(1.0, sin_alt)));
    out->azimuth = atan2(-cos(body->dec) * sin(hour_angle),
        sin(body->dec) * cos(latitude) - cos(body->dec) * sin(latitude) * cos(hour_angle));
}

/** Air mass transmittance of the fallback toward a light above the horizon.
 *
 * @param altitude Altitude of the light in radians
 * @return The transmittance, 0 at or below the horizon
 */
static float box_tod_transmittance(
    float altitude)
{
    float result = 0.0f;
    if (altitude > 0) {
        float altitude_deg = glm_deg(altitude);
        float ky = 0.50572f * powf(glm_max(altitude_deg + 6.07995f, 0.01f), -1.6364f);
        float air_mass = 1.0f / glm_max(sinf(altitude) + ky, 1e-4f);
        result = glm_clamp(expf(-0.1f * (air_mass - 1.0f)), 0.0f, 1.0f);
    }

    return result;
}

/** Compute the sky of a time of day and its daylight into its status.
 *
 * The hour is apparent solar time, so the sun's hour angle is 15 degrees per hour from noon;
 * the local sidereal time follows from the sun's right ascension and positions the moon. The
 * moon is computed moon_offset hours later, the sun with it for its sidereal time and phase.
 *
 * @param t The time of day
 * @param out The status, its hour and day_of_year in, the sky out
 */
static void box_tod_sky(
    const TimeOfDay *t,
    TimeOfDayStatus *out)
{
    double latitude = glm_clamp(t->latitude, -90.0f, 90.0f) * BOX_TOD_RAD;
    double hour = out->hour, moon_hour = hour + t->moon_offset;
    box_tod_ecliptic_t sun, moon, moon_sun;
    box_tod_body_t sun_pos, moon_pos;

    box_tod_bodies(out->day_of_year + hour / 24.0, &sun, &moon);
    box_tod_horizontal(&sun, (hour - 12.0) * 15.0 * BOX_TOD_RAD, latitude, &sun_pos);

    box_tod_bodies(out->day_of_year + moon_hour / 24.0, &moon_sun, &moon);
    double lst = moon_sun.ra + (moon_hour - 12.0) * 15.0 * BOX_TOD_RAD;
    box_tod_horizontal(&moon, lst - moon.ra, latitude, &moon_pos);
    double cos_elongation = moon.ecliptic[0] * moon_sun.ecliptic[0] +
        moon.ecliptic[1] * moon_sun.ecliptic[1] + moon.ecliptic[2] * moon_sun.ecliptic[2];

    out->sun_altitude = (float)(sun_pos.altitude / BOX_TOD_RAD);
    out->sun_azimuth = (float)box_tod_norm_deg(sun_pos.azimuth / BOX_TOD_RAD);
    out->moon_altitude = (float)(moon_pos.altitude / BOX_TOD_RAD);
    out->moon_azimuth = (float)box_tod_norm_deg(moon_pos.azimuth / BOX_TOD_RAD);
    out->moon_illumination = glm_clamp((float)(0.5 * (1.0 - cos_elongation)), 0.0f, 1.0f);
    if (out->sun_azimuth >= 360.0f) {
        out->sun_azimuth = 0.0f;
    }

    if (out->moon_azimuth >= 360.0f) {
        out->moon_azimuth = 0.0f;
    }

    float sun_alt = (float)sun_pos.altitude;
    float trans = box_tod_transmittance(glm_max(sun_alt, BOX_TOD_DISK_RADIUS));
    float sun_daylight = box_tod_smoothstep((out->sun_altitude + 6.0f) / 12.0f) *
        (0.55f + 0.45f * trans);
    float moon_daylight = 0.05f * out->moon_illumination *
        box_tod_smoothstep((out->moon_altitude + 2.0f) / 8.0f);
    out->daylight = glm_clamp(sun_daylight + moon_daylight, 0.0f, 1.0f);
}

void box_tod_fallback(
    float sin_altitude,
    float *color)
{
    static const float low[3] = { 1.00f, 0.55f, 0.28f };
    static const float high[3] = { 1.00f, 0.97f, 0.92f };
    float altitude = asinf(glm_clamp(sin_altitude, -1.0f, 1.0f));
    float t = box_tod_smoothstep((glm_deg(altitude) - 2.0f) / 18.0f);
    float fade = box_tod_smoothstep((altitude + BOX_TOD_DISK_RADIUS) /
        (2.0f * BOX_TOD_DISK_RADIUS));
    float trans = box_tod_transmittance(glm_max(altitude, BOX_TOD_DISK_RADIUS));
    int32_t c;
    for (c = 0; c < 3; c ++) {
        color[c] *= (low[c] * (1.0f - t) + high[c] * t) * trans * fade;
    }
}

/** Wrap a time into an hour of 0 to 24 and a day of 1 to 365, carrying whole days.
 *
 * @param hour The hour, any value
 * @param day The day, any value
 * @param out Receives the wrapped hour and day_of_year
 */
static void box_tod_wrap(
    double hour,
    int64_t day,
    TimeOfDayStatus *out)
{
    double days = floor(hour / 24.0);
    float wrapped = (float)(hour - days * 24.0);
    day += (int64_t)days;
    if (wrapped >= 24.0f) {
        wrapped = 0.0f;
        day ++;
    } else if (wrapped < 0.0f) {
        wrapped = 0.0f;
    }

    out->hour = wrapped;
    out->day_of_year = (int32_t)(((day - 1) % BOX_TOD_DAYS + BOX_TOD_DAYS) % BOX_TOD_DAYS + 1);
}

/** Construct TimeOfDay values with noon at the March equinox at latitude 45.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_tod_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    TimeOfDay *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (TimeOfDay){
            .hour = 12.0f,
            .day_of_year = 80,
            .latitude = 45.0f
        };
    }
}

/** Restart the TimeOfDayStatus of an entity whose TimeOfDay was added or set.
 *
 * @param it The observer iterator
 */
static void box_tod_restart(
    ecs_iter_t *it)
{
    const TimeOfDay *t = ecs_field(it, TimeOfDay, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        TimeOfDayStatus status = {0};
        box_tod_wrap(t[i].hour, t[i].day_of_year, &status);
        box_tod_sky(&t[i], &status);
        ecs_set_ptr(it->stage, it->entities[i], TimeOfDayStatus, &status);
    }
}

/** Remove TimeOfDayStatus from an entity that lost TimeOfDay.
 *
 * @param it The observer iterator
 */
static void box_tod_remove(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_remove(it->stage, it->entities[i], TimeOfDayStatus);
    }
}

/** Find the first entity of a query in a scene.
 *
 * @param world The world
 * @param q The query, its Scene(self|up) field at index 1
 * @param scene The scene
 * @param skip An entity that does not count, 0 for none
 * @param out Receives a copy of the first field of the entity, NULL when not needed
 * @param size Size of the first field
 * @return The entity, 0 when the scene has none
 */
static ecs_entity_t box_tod_first(
    const ecs_world_t *world,
    ecs_query_t *q,
    ecs_entity_t scene,
    ecs_entity_t skip,
    void *out,
    ecs_size_t size)
{
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_query_iter(world, q);
    while (!result && ecs_query_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count && !result; i ++) {
            if (it.entities[i] != skip && box_render_scene_field(&it, 1, i) == scene) {
                result = it.entities[i];
                if (out) {
                    ecs_os_memcpy(out, ECS_OFFSET(ecs_field_w_size(&it, size, 0), size * i),
                        size);
                }
            }
        }
    }

    if (result) {
        ecs_iter_fini(&it);
    }

    return result;
}

/** Find the sun and moon lights of a time of day: its own, else those of the Atmosphere of
 * its scene as the light extract picks them, else the first DirectionalLight of the scene.
 *
 * @param world The world
 * @param state The state of the system
 * @param e The entity with TimeOfDay
 * @param t The time of day
 * @param sun Receives the sun, 0 for none
 * @param moon Receives the moon, 0 for none
 */
static void box_tod_lights(
    const ecs_world_t *world,
    box_tod_state_t *state,
    ecs_entity_t e,
    const TimeOfDay *t,
    ecs_entity_t *sun,
    ecs_entity_t *moon)
{
    ecs_entity_t scene = box_render_scene_of(world, e);
    Atmosphere atmos = {0};
    *sun = t->sun;
    *moon = t->moon;
    if ((!*sun || !*moon) && box_tod_first(world, state->atmospheres, scene, 0, &atmos,
        ECS_SIZEOF(Atmosphere)))
    {
        *sun = *sun ? *sun : atmos.sun;
        *moon = *moon ? *moon : atmos.moon;
    }

    if (!*sun) {
        *sun = box_tod_first(world, state->lights, scene, *moon, NULL, 0);
    }
}

/** Point a light along its local +Z from a position in the sky toward the ground, unless its
 * owned Rotation already does.
 *
 * @param world The stage the Rotation is set on
 * @param light The light, 0 for none
 * @param t The time of day, for north
 * @param altitude Altitude of the body in degrees
 * @param azimuth Azimuth of the body in degrees, from north toward east
 */
static void box_tod_point(
    ecs_world_t *world,
    ecs_entity_t light,
    const TimeOfDay *t,
    float altitude,
    float azimuth)
{
    if (!light || !ecs_is_alive(world, light)) {
        goto done;
    }

    float yaw = glm_rad(t->north - azimuth) + GLM_PIf;
    yaw = (float)(yaw - 2.0 * GLM_PI * floor((yaw + GLM_PI) / (2.0 * GLM_PI)));
    Rotation r = { glm_rad(altitude), yaw, 0.0f };
    const Rotation *current = ecs_get(world, light, Rotation);
    if (!current || !ecs_owns(world, light, Rotation) ||
        ecs_os_memcmp(current, &r, ECS_SIZEOF(Rotation)))
    {
        ecs_set_ptr(world, light, Rotation, &r);
    }

done:
    return;
}

/** Advance the time of day of every scene, write its status and point its sun and moon when
 * they changed.
 *
 * @param it The iterator of the system
 */
static void BoxTimeOfDayUpdate(
    ecs_iter_t *it)
{
    box_tod_state_t *state = it->ctx;
    const TimeOfDay *t = ecs_field(it, TimeOfDay, 0);
    const TimeOfDayStatus *s = ecs_field(it, TimeOfDayStatus, 1);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i], sun, moon;
        TimeOfDayStatus status = s[i];
        if (t[i].time_scale != 0) {
            box_tod_wrap((double)status.hour + (double)t[i].time_scale * it->delta_time,
                status.day_of_year, &status);
        }

        box_tod_sky(&t[i], &status);
        if (ecs_os_memcmp(&status, &s[i], ECS_SIZEOF(TimeOfDayStatus))) {
            ecs_set_ptr(it->stage, e, TimeOfDayStatus, &status);
        }

        box_tod_lights(it->world, state, e, &t[i], &sun, &moon);
        box_tod_point(it->stage, sun, &t[i], status.sun_altitude, status.sun_azimuth);
        if (moon != sun) {
            box_tod_point(it->stage, moon, &t[i], status.moon_altitude, status.moon_azimuth);
        }
    }
}

/** Free the state of the time of day system.
 *
 * @param ctx The state
 */
static void box_tod_free(
    void *ctx)
{
    ecs_os_free(ctx);
}

/** Create a query of a component and the scene of its entity.
 *
 * @param world The world
 * @param component The component
 * @param name The name of the query entity
 * @return The query, with the component and Scene(self|up)
 */
static ecs_query_t* box_tod_scene_query(
    ecs_world_t *world,
    ecs_entity_t component,
    const char *name)
{
    return ecs_query(world, {
        .entity = ecs_entity(world, { .name = name }),
        .terms = {
            { .id = component, .inout = EcsIn },
            {
                .id = ecs_id(Scene),
                .src.id = EcsSelf | EcsUp,
                .trav = EcsChildOf,
                .inout = EcsInOutNone,
                .oper = EcsOptional
            }
        },
        .cache_kind = EcsQueryCacheAuto
    });
}

void box_tod_init(
    ecs_world_t *world)
{
    box_tod_state_t *state = ecs_os_calloc_t(box_tod_state_t);
    state->lights = box_tod_scene_query(world, ecs_id(DirectionalLight), "TimeOfDayLights");
    state->atmospheres = box_tod_scene_query(world, ecs_id(Atmosphere), "TimeOfDayAtmospheres");

    ecs_set_hooks(world, TimeOfDay, { .ctor = box_tod_ctor });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(TimeOfDay) }},
        .events = { EcsOnAdd, EcsOnSet },
        .callback = box_tod_restart
    });
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(TimeOfDay) }},
        .events = { EcsOnRemove },
        .callback = box_tod_remove
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "TimeOfDayUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(TimeOfDay), .inout = EcsIn },
            { .id = ecs_id(TimeOfDayStatus), .inout = EcsIn }
        },
        .callback = BoxTimeOfDayUpdate,
        .ctx = state,
        .ctx_free = box_tod_free
    });
}
