/**
 * @file sky/celestial.c
 * @brief Resolves the sun, the moon and the stars and lights the views with them.
 *
 * The sun and the moon are entities with a direction and a radiance over the
 * atmosphere. This file marches the medium once per view to find what is left
 * of that radiance at the camera, hands the brighter of the two to the view as
 * its directional light and copies the moon and the star field into the packet
 * the sky pass draws them from.
 */

#include "sky_internal.h"
#include <math.h>

/** Samples the transmittance march takes, the step count of the table pass */
#define BOX_SKY_TRANS_STEPS (40)

/** Altitude in kilometers the ozone layer peaks at */
#define BOX_SKY_OZONE_PEAK (25.0f)

/** Half width in kilometers of the ozone layer */
#define BOX_SKY_OZONE_SPAN (15.0f)

/** Altitude in kilometers the stratospheric aerosol layer peaks at */
#define BOX_SKY_STRAT_PEAK (20.0f)

/** Half width in kilometers of that layer */
#define BOX_SKY_STRAT_SPAN (5.0f)

/** Cosine band the planet takes to cover a body at the horizon */
#define BOX_SKY_HORIZON_BAND (0.02f)

/** Weights the luminance of a radiance is compared with */
static const float box_sky_luma_weights[3] = { 0.2126f, 0.7152f, 0.0722f };

/** Take a value or its default when it is zero, keeping a negative one.
 *
 * @param value The value
 * @param fallback The default
 * @return The value, or the default
 */
static float box_sky_set(
    float value,
    float fallback)
{
    return value != 0.0f ? value : fallback;
}

/** Hermite interpolation between two edges, the smoothstep of the shaders.
 *
 * @param low The edge the result leaves zero at
 * @param high The edge the result reaches one at
 * @param x The value
 * @return The interpolant
 */
static float box_sky_smooth(
    float low,
    float high,
    float x)
{
    float t = (x - low) / (high - low > 1e-6f ? high - low : 1e-6f);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);

    return t * t * (3.0f - 2.0f * t);
}

/** Luminance of a radiance, which decides the body a view is lit by.
 *
 * @param color The three channels
 * @return The luminance
 */
static float box_sky_luma(
    const float *color)
{
    return color[0] * box_sky_luma_weights[0] + color[1] * box_sky_luma_weights[1] +
        color[2] * box_sky_luma_weights[2];
}

/** Extinction of the medium at an altitude, per channel.
 *
 * @param sky The parameter block holding the medium
 * @param alt The altitude in kilometers
 * @param out The three channels to fill
 */
static void box_sky_extinction(
    const box_sky_t *sky,
    float alt,
    float *out)
{
    float rayleigh = expf(-alt / sky->bounds[2]);
    float mie = expf(-alt / sky->bounds[3]);
    float ozone = fmaxf(0.0f,
        1.0f - fabsf(alt - BOX_SKY_OZONE_PEAK) / BOX_SKY_OZONE_SPAN);
    float strat = fmaxf(0.0f,
        1.0f - fabsf(alt - BOX_SKY_STRAT_PEAK) / BOX_SKY_STRAT_SPAN);

    int32_t i;
    for (i = 0; i < 3; i ++) {
        out[i] = sky->scat_rayleigh[i] * rayleigh + sky->ext_mie[i] * mie +
            sky->abs_ozone[i] * ozone + sky->abs_strat[i] * strat +
            sky->abs_haze[i] * mie;
    }
}

void box_sky_transmittance(
    const box_sky_t *sky,
    float r,
    float mu,
    float *out)
{
    float bottom = sky->bounds[0];
    float top = sky->bounds[1];
    float horizon = -sqrtf(fmaxf(0.0f,
        1.0f - (bottom * bottom) / fmaxf(r * r, 1e-6f)));
    float visible = box_sky_smooth(horizon - BOX_SKY_HORIZON_BAND,
        horizon + BOX_SKY_HORIZON_BAND, mu);
    float disc = r * r * (mu * mu - 1.0f) + top * top;
    float length = -r * mu + sqrtf(fmaxf(disc, 0.0f));
    float depth[3] = { 0.0f, 0.0f, 0.0f };
    float step = fmaxf(length, 0.0f) / (float)BOX_SKY_TRANS_STEPS;

    int32_t i;
    for (i = 0; i < BOX_SKY_TRANS_STEPS; i ++) {
        float t = ((float)i + 0.5f) * step;
        float alt = sqrtf(fmaxf(r * r + t * t + 2.0f * r * t * mu, 0.0f)) - bottom;
        float extinct[3];
        box_sky_extinction(sky, fmaxf(alt, 0.0f), extinct);
        int32_t c;
        for (c = 0; c < 3; c ++) {
            depth[c] += extinct[c] * step;
        }
    }

    for (i = 0; i < 3; i ++) {
        out[i] = expf(-depth[i]) * visible;
    }
}

/** Read the radiance an entity has over the medium.
 *
 * A CelestialLight gives it outright, a DirectionalLight and an Rgb give it
 * for a light that was authored without one, and an entity with neither is
 * white at one.
 *
 * @param world The world
 * @param e The entity
 * @param out The three channels to fill
 */
static void box_sky_radiance(
    ecs_world_t *world,
    ecs_entity_t e,
    float *out)
{
    const DirectionalLight *light = ecs_get(world, e, DirectionalLight);
    const CelestialLight *celestial = ecs_get(world, e, CelestialLight);
    const Rgb *tint = ecs_get(world, e, Rgb);
    float intensity = light ? box_sky_or(light->intensity, 1.0f) : 1.0f;
    float color[3] = { 1.0f, 1.0f, 1.0f };
    if (tint) {
        rgb value = { tint->r, tint->g, tint->b };
        box_sky_color(value, 255, color);
    }

    if (celestial) {
        intensity = box_sky_or(celestial->toa_intensity, intensity);
        box_sky_color(celestial->toa_color, 255, color);
    }

    int32_t i;
    for (i = 0; i < 3; i ++) {
        out[i] = color[i] * intensity;
    }
}

/** Read the direction and the radiance over the medium of one body.
 *
 * @param world The world
 * @param e The entity of the body, 0 for none
 * @param out The body to fill
 */
static void box_sky_body(
    ecs_world_t *world,
    ecs_entity_t e,
    box_body_t *out)
{
    memset(out, 0, sizeof(box_body_t));
    const WorldTransform *transform = e && ecs_is_alive(world, e) ?
        ecs_get(world, e, WorldTransform) : NULL;
    if (!transform) {
        goto done;
    }

    vec3 dir = vec3_normalize(mat4_transform_dir(&transform->m, vec3_new(0, 0, 1)));
    out->dir[0] = -dir.x;
    out->dir[1] = -dir.y;
    out->dir[2] = -dir.z;

    box_sky_radiance(world, e, out->toa);
    out->present = true;
done:
    return;
}

/** Take the light a view already found as the sun of the atmosphere.
 *
 * @param v The view
 * @param out The body to fill
 */
static void box_sky_view_body(
    const box_view_t *v,
    box_body_t *out)
{
    memset(out, 0, sizeof(box_body_t));

    int32_t i;
    for (i = 0; i < 3; i ++) {
        out->dir[i] = -v->sun_dir[i];
        out->toa[i] = v->sun_color[i] * v->sun_intensity;
    }

    out->present = true;
}

/** Write what the medium leaves of the radiance of a body at the camera.
 *
 * @param sky The parameter block holding the medium
 * @param r Radius of the camera in kilometers from the planet center
 * @param scale Part of the disk of the body that is lit
 * @param body The body to fill in
 */
static void box_sky_absorb(
    const box_sky_t *sky,
    float r,
    float scale,
    box_body_t *body)
{
    float transmittance[3] = { 0.0f, 0.0f, 0.0f };
    if (body->present) {
        box_sky_transmittance(sky, r, body->dir[1], transmittance);
    }

    int32_t i;
    for (i = 0; i < 3; i ++) {
        body->light[i] = body->toa[i] * transmittance[i] * scale;
    }
}

/** Illuminated fraction of the moon, which is the geometry of the two bodies.
 *
 * A moon opposite the sun shows a full disk and a moon in front of it shows
 * none, so the cosine between the two directions is the whole phase.
 *
 * @param sun The sun
 * @param moon The moon
 * @return The lit fraction, zero when the sky holds no moon
 */
static float box_sky_phase(
    const box_body_t *sun,
    const box_body_t *moon)
{
    float result = 0.0f;
    if (!moon->present) {
        goto done;
    }

    float cosine = sun->dir[0] * moon->dir[0] + sun->dir[1] * moon->dir[1] +
        sun->dir[2] * moon->dir[2];
    cosine = cosine < -1.0f ? -1.0f : (cosine > 1.0f ? 1.0f : cosine);
    result = (1.0f - cosine) * 0.5f;
done:
    return result;
}

/** Part of the star field the daylight of the frame leaves standing.
 *
 * @param world The world
 * @param a The atmosphere
 * @param sun The sun of the frame
 * @return One in the dark, zero once the sun has risen over the fade
 */
static float box_sky_fade(
    ecs_world_t *world,
    const Atmosphere *a,
    const box_body_t *sun)
{
    const Stars *stars = a->stars && ecs_is_alive(world, a->stars) ?
        ecs_get(world, a->stars, Stars) : NULL;
    float low = BOX_SKY_STARS_FADE_START;
    float high = BOX_SKY_STARS_FADE_END;
    if (stars) {
        low = box_sky_set(stars->fade_start, low);
        high = box_sky_set(stars->fade_end, high);
    }

    return 1.0f - box_sky_smooth(low, high, sun->dir[1]);
}

/** Hand the view the body that reaches the camera brightest.
 *
 * @param state The registry holding the two bodies
 * @param v The view to light
 */
static void box_sky_shine(
    const SkyState *state,
    box_view_t *v)
{
    const box_body_t *body = &state->sun;
    if (state->moon.present && box_sky_luma(state->moon.light) >
        box_sky_luma(state->sun.light))
    {
        body = &state->moon;
    }

    int32_t i;
    for (i = 0; i < 3; i ++) {
        v->sun_dir[i] = -body->dir[i];
        v->sun_color[i] = body->light[i];
    }

    v->sun_intensity = 1.0f;
}

float box_sky_altitude_of(
    const Atmosphere *a,
    float y)
{
    float units = box_sky_or(a->world_units_per_km, BOX_SKY_UNITS_PER_KM);

    return a->ground_altitude_km + (y - a->sea_level_y) / units;
}

bool box_sky_celestial(
    ecs_world_t *world,
    box_view_t *v)
{
    bool result = false;
    ecs_entity_t e = box_sky_entity(world);
    const Atmosphere *a = e ? ecs_get(world, e, Atmosphere) : NULL;
    if (!a) {
        goto done;
    }

    SkyState *state = ecs_singleton_ensure(world, SkyState);
    box_sky_t sky;
    box_sky_medium(a, &sky);
    box_sky_body(world, a->sun, &state->sun);
    if (!state->sun.present) {
        box_sky_view_body(v, &state->sun);
    }

    box_sky_body(world, a->moon, &state->moon);
    state->phase = box_sky_phase(&state->sun, &state->moon);
    state->fade = box_sky_fade(world, a, &state->sun);
    state->lit = true;

    float altitude = box_sky_altitude_of(a, v->eye[1]);
    float r = sky.bounds[0] + fmaxf(altitude, BOX_SKY_LIFT);
    box_sky_absorb(&sky, r, 1.0f, &state->sun);
    box_sky_absorb(&sky, r, state->phase, &state->moon);
    box_sky_shine(state, v);
    result = true;
done:
    return result;
}

/** Write the basis of the star field in world space into the packet.
 *
 * The shader turns a view ray into star space with it, so the field follows
 * the rotation of the entity and the sky turns over the night.
 *
 * @param world The world
 * @param e The entity holding the Stars component
 * @param sky The parameter block to fill
 */
static void box_sky_stars_basis(
    ecs_world_t *world,
    ecs_entity_t e,
    box_sky_t *sky)
{
    const WorldTransform *transform = ecs_get(world, e, WorldTransform);
    vec3 axis[3] = { vec3_new(1, 0, 0), vec3_new(0, 1, 0), vec3_new(0, 0, 1) };

    int32_t i;
    for (i = 0; i < 3; i ++) {
        vec3 dir = axis[i];
        vec3 turned = transform ? mat4_transform_dir(&transform->m, axis[i]) : dir;
        if (vec3_length(turned) > 1e-5f) {
            dir = vec3_normalize(turned);
        }

        sky->stars_rot[i * 4 + 0] = dir.x;
        sky->stars_rot[i * 4 + 1] = dir.y;
        sky->stars_rot[i * 4 + 2] = dir.z;
        sky->stars_rot[i * 4 + 3] = 0.0f;
    }
}

/** Write the star field of the atmosphere into the packet.
 *
 * @param world The world
 * @param a The atmosphere
 * @param p The packet
 * @param sky The parameter block to fill
 */
static void box_sky_stars(
    ecs_world_t *world,
    const Atmosphere *a,
    const box_packet_t *p,
    box_sky_t *sky)
{
    const SkyState *state = ecs_singleton_get(world, SkyState);
    ecs_entity_t e = a->stars && ecs_is_alive(world, a->stars) ? a->stars : 0;
    const Stars *stars = e ? ecs_get(world, e, Stars) : NULL;
    if (!stars) {
        goto done;
    }

    box_sky_radiance(world, e, sky->stars_color);
    sky->stars_color[3] = state ? state->fade : 1.0f;
    sky->stars[0] = box_sky_or(stars->density, BOX_SKY_STARS_DENSITY);
    sky->stars[1] = box_sky_or(stars->size, BOX_SKY_STARS_SIZE);
    sky->stars[2] = stars->twinkle > 0.0f ? stars->twinkle : 0.0f;
    sky->stars[3] = (float)p->time;
    box_sky_stars_basis(world, e, sky);
done:
    return;
}

void box_sky_bodies(
    ecs_world_t *world,
    const Atmosphere *a,
    const box_packet_t *p,
    box_sky_t *sky)
{
    const SkyState *state = ecs_singleton_get(world, SkyState);
    memset(sky->moon_dir, 0, sizeof(sky->moon_dir));
    memset(sky->moon_color, 0, sizeof(sky->moon_color));
    memset(sky->stars_color, 0, sizeof(sky->stars_color));
    memset(sky->stars, 0, sizeof(sky->stars));
    memset(sky->stars_rot, 0, sizeof(sky->stars_rot));
    sky->moon_dir[3] = 1.0f;
    if (state && state->lit && state->moon.present) {
        float radius = box_sky_or(a->moon_angular_radius, BOX_SKY_MOON_RADIUS);
        int32_t i;
        for (i = 0; i < 3; i ++) {
            sky->moon_dir[i] = state->moon.dir[i];
            sky->moon_color[i] = state->moon.toa[i];
        }

        sky->moon_dir[3] = cosf(radius);
        sky->moon_color[3] = state->phase;
    }

    box_sky_stars(world, a, p, sky);
}
