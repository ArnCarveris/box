/**
 * @file sky/extract.c
 * @brief Copies the atmosphere of a world into the packet and decides what to render.
 *
 * The parameter block leaves this file in the units the shaders integrate in,
 * so the renderer never converts. The two expensive tables depend on the medium
 * alone and are marked dirty when a coefficient changes; the sky view table
 * also follows the sun and the camera altitude and is marked dirty when either
 * moves further than its threshold.
 */

#include "sky_internal.h"
#include <math.h>

/** Rayleigh scattering of air at sea level per kilometer, per channel */
static const float box_sky_rayleigh[3] = { 0.005802f, 0.013558f, 0.033100f };

/** Mie scattering of the aerosol layer at sea level per kilometer */
#define BOX_SKY_MIE_SCATTER (0.003996f)

/** Mie extinction of the aerosol layer at sea level per kilometer */
#define BOX_SKY_MIE_EXTINCT (0.004400f)

/** Ozone absorption of the layer at 25 kilometers, per channel */
static const float box_sky_ozone[3] = { 0.000650f, 0.001881f, 0.000085f };

/** Stratospheric aerosol absorption of the layer at 20 kilometers, per channel */
static const float box_sky_strat[3] = { 0.001950f, 0.005643f, 0.000255f };

/** Grey haze absorption of the aerosol layer per kilometer at scale one */
#define BOX_SKY_HAZE (0.005f)

float box_sky_or(
    float value,
    float fallback)
{
    return value > 0.0f ? value : fallback;
}

void box_sky_color(
    rgb color,
    uint8_t fallback,
    float *out)
{
    rgb used = color;
    if (!used.r && !used.g && !used.b) {
        used.r = fallback;
        used.g = fallback;
        used.b = fallback;
    }

    out[0] = box_srgb_to_linear(used.r);
    out[1] = box_srgb_to_linear(used.g);
    out[2] = box_srgb_to_linear(used.b);
}

void box_sky_medium(
    const Atmosphere *a,
    box_sky_t *sky)
{
    float planet = box_sky_or(a->planet_radius_km, BOX_SKY_PLANET_KM);
    sky->bounds[0] = planet;
    sky->bounds[1] = planet + box_sky_or(a->atmosphere_thickness_km, BOX_SKY_THICKNESS_KM);
    sky->bounds[2] = box_sky_or(a->rayleigh.scale_height_km, BOX_SKY_RAYLEIGH_KM);
    sky->bounds[3] = box_sky_or(a->mie.scale_height_km, BOX_SKY_MIE_KM);

    float rayleigh = box_sky_or(a->rayleigh.scattering_scale, 1.0f);
    float mie = box_sky_or(a->mie.scattering_scale, 1.0f);
    float ozone = a->ozone_scale < 0.0f ? 0.0f : box_sky_or(a->ozone_scale, 1.0f);
    float strat = a->stratospheric_aerosol_scale > 0.0f ? a->stratospheric_aerosol_scale : 0.0f;
    float haze = a->haze_absorption > 0.0f ? a->haze_absorption : 0.0f;
    float tint[3];
    box_sky_color(a->mie.tint, 255, tint);

    int32_t i;
    for (i = 0; i < 3; i ++) {
        sky->scat_rayleigh[i] = box_sky_rayleigh[i] * rayleigh;
        sky->scat_mie[i] = BOX_SKY_MIE_SCATTER * mie * tint[i];
        sky->ext_mie[i] = BOX_SKY_MIE_EXTINCT * mie * tint[i];
        sky->abs_ozone[i] = box_sky_ozone[i] * ozone;
        sky->abs_strat[i] = box_sky_strat[i] * strat;
        sky->abs_haze[i] = BOX_SKY_HAZE * haze;
    }

    box_sky_color(a->ground_albedo, BOX_SKY_GROUND_ALBEDO, sky->ground_albedo);
}

/** Write the sun direction and radiance the sky is lit by.
 *
 * The celestial pass that lit the views already resolved the body and its
 * radiance over the medium, which is the radiance the sky scatters; a packet
 * without views falls back to a sun straight overhead.
 *
 * @param world The world
 * @param sky The parameter block to fill
 */
static void box_sky_sun(
    ecs_world_t *world,
    box_sky_t *sky)
{
    const SkyState *state = ecs_singleton_get(world, SkyState);
    vec3 dir = vec3_new(0, 1, 0);
    vec3 color = vec3_new(1, 1, 1);
    if (state && state->lit) {
        dir = vec3_new(state->sun.dir[0], state->sun.dir[1], state->sun.dir[2]);
        color = vec3_new(state->sun.toa[0], state->sun.toa[1], state->sun.toa[2]);
    }

    sky->sun_dir[0] = dir.x;
    sky->sun_dir[1] = dir.y;
    sky->sun_dir[2] = dir.z;
    sky->sun_color[0] = color.x;
    sky->sun_color[1] = color.y;
    sky->sun_color[2] = color.z;
}

/** Write what belongs to the view rather than to the medium.
 *
 * @param a The atmosphere
 * @param p The packet
 * @param sky The parameter block to fill
 */
static void box_sky_look(
    const Atmosphere *a,
    const box_packet_t *p,
    box_sky_t *sky)
{
    float aerial = a->aerial_perspective_intensity < 0.0f ? 0.0f :
        box_sky_or(a->aerial_perspective_intensity, 1.0f);
    float disk = a->sun_disk_intensity < 0.0f ? 0.0f :
        box_sky_or(a->sun_disk_intensity, 1.0f);
    float radius = box_sky_or(a->sun_disk_angular_radius, BOX_SKY_DISK_RADIUS);

    sky->sun_color[3] = aerial;
    sky->mie_params[0] = a->mie.anisotropy;
    sky->mie_params[1] = box_sky_or(a->aerial_perspective_distance_km, BOX_SKY_AERIAL_KM);
    sky->mie_params[2] = cosf(radius);
    sky->mie_params[3] = disk;

    float units = box_sky_or(a->world_units_per_km, BOX_SKY_UNITS_PER_KM);
    sky->world_scale[0] = units;
    sky->world_scale[1] = 1.0f / units;
    sky->world_scale[2] = a->sea_level_y;
    sky->world_scale[3] = a->ground_altitude_km;

    float night[3];
    box_sky_color(a->night_tint, 0, night);
    float intensity = a->night_intensity > 0.0f ? a->night_intensity : 0.0f;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        sky->night[i] = night[i] * intensity;
    }

    sky->turbulence[0] = a->turbulence > 0.0f ? a->turbulence : 0.0f;
    sky->turbulence[1] = box_sky_or(a->turbulence_scale, BOX_SKY_TURBULENCE_SCALE);
    sky->turbulence[2] = box_sky_or(a->turbulence_speed, 1.0f);
    sky->turbulence[3] = (float)p->time;
}

/** Write the overcast of the weather into the spare slot of the night color.
 *
 * The sky pass and the environment face of a view blend their radiance toward
 * a flat grey with it, which is how a world gets a cloud deck without the
 * atmosphere it is drawn from holding one: the medium, the sun and both
 * expensive tables stay the clear sky they were.
 *
 * @param world The world
 * @param sky The parameter block to fill
 */
static void box_sky_overcast(
    ecs_world_t *world,
    box_sky_t *sky)
{
    WeatherAmounts weather;
    box_sky_weather(world, &weather);
    sky->night[3] = weather.overcast;
}

/** Altitude of the camera of a frame over the planet, in kilometers.
 *
 * @param p The packet
 * @param sky The parameter block
 * @return The altitude
 */
static float box_sky_altitude(
    const box_packet_t *p,
    const box_sky_t *sky)
{
    float result = sky->world_scale[3];
    if (p->views.count) {
        const box_view_t *v = box_vec_at(&p->views, 0);
        result += (v->eye[1] - sky->world_scale[2]) * sky->world_scale[1];
    }

    return result;
}

/** Check whether the medium of two parameter blocks is the same.
 *
 * @param a The first block
 * @param b The second block
 * @return true when every coefficient the two tables are built from matches
 */
static bool box_sky_medium_equal(
    const box_sky_t *a,
    const box_sky_t *b)
{
    size_t bytes = sizeof(a->bounds);

    return memcmp(a->bounds, b->bounds, bytes) == 0 &&
        memcmp(a->ground_albedo, b->ground_albedo, bytes) == 0 &&
        memcmp(a->scat_rayleigh, b->scat_rayleigh, bytes) == 0 &&
        memcmp(a->scat_mie, b->scat_mie, bytes) == 0 &&
        memcmp(a->ext_mie, b->ext_mie, bytes) == 0 &&
        memcmp(a->abs_ozone, b->abs_ozone, bytes) == 0 &&
        memcmp(a->abs_strat, b->abs_strat, bytes) == 0 &&
        memcmp(a->abs_haze, b->abs_haze, bytes) == 0;
}

/** Check whether the sun and the sky of two parameter blocks are the same.
 *
 * @param a The first block
 * @param b The second block
 * @param epsilon Radians the sun may have moved and overcast it may have gained
 * @return true when the sky of the first block is the sky of the second
 */
static bool box_sky_view_equal(
    const box_sky_t *a,
    const box_sky_t *b,
    float epsilon)
{
    vec3 moved = vec3_sub(vec3_new(a->sun_dir[0], a->sun_dir[1], a->sun_dir[2]),
        vec3_new(b->sun_dir[0], b->sun_dir[1], b->sun_dir[2]));

    return vec3_length(moved) <= epsilon &&
        fabsf(a->night[3] - b->night[3]) <= epsilon &&
        memcmp(a->sun_color, b->sun_color, sizeof(float) * 3) == 0 &&
        memcmp(a->night, b->night, sizeof(float) * 3) == 0 &&
        a->mie_params[0] == b->mie_params[0];
}

/** Mix the environment generation of the sky into the views that use it.
 *
 * That generation is its own: the sky view table follows the sun within a
 * fraction of a degree because the sky is looked at directly, while filtering
 * the environment again costs a cubemap and a prefilter, which a tenth of a
 * degree of a sun does not change.
 *
 * @param p The packet
 * @param generation The generation of the environment of the atmosphere
 */
static void box_sky_environments(
    box_packet_t *p,
    uint32_t generation)
{
    uint32_t i;
    for (i = 0; i < p->views.count; i ++) {
        box_view_t *v = box_vec_at(&p->views, i);
        if (v->sky_env) {
            v->env_generation = v->env_generation * 2654435761u + generation;
        }
    }

    if (p->views.count) {
        p->view = *(box_view_t*)box_vec_at(&p->views, 0);
    }
}

/** Copy the handle a shader entity was given into the packet.
 *
 * @param world The world
 * @param e The shader entity
 * @return The handle, BOX_SHADER_NONE while the shader did not compile
 */
static uint32_t box_sky_handle(
    ecs_world_t *world,
    ecs_entity_t e)
{
    uint32_t result = BOX_SHADER_NONE;
    const ShaderImpl *impl = e ? ecs_get(world, e, ShaderImpl) : NULL;
    if (impl && impl->assigned) {
        result = impl->handle;
    }

    return result;
}

void box_sky_extract(
    ecs_world_t *world,
    box_packet_t *p)
{
    SkyState *state = ecs_singleton_ensure(world, SkyState);
    ecs_entity_t e = box_sky_entity(world);
    const Atmosphere *a = e ? ecs_get(world, e, Atmosphere) : NULL;
    if (!a) {
        state->valid = false;
        goto done;
    }

    box_sky_t *sky = &p->sky;
    box_sky_medium(a, sky);
    box_sky_sun(world, sky);
    box_sky_look(a, p, sky);
    box_sky_overcast(world, sky);
    box_sky_bodies(world, a, p, sky);
    sky->active = 1;

    float altitude = box_sky_altitude(p, sky);
    bool lut = !state->valid || !box_sky_medium_equal(sky, &state->last);
    bool view = lut || !box_sky_view_equal(sky, &state->last, BOX_SKY_SUN_EPSILON) ||
        fabsf(altitude - state->altitude) > BOX_SKY_ALTITUDE_EPSILON;
    if (view) {
        state->generation ++;
        state->altitude = altitude;
        state->last = *sky;
    }

    bool env = !state->valid || !box_sky_medium_equal(sky, &state->env_last) ||
        !box_sky_view_equal(sky, &state->env_last, BOX_SKY_ENV_EPSILON) ||
        fabsf(altitude - state->env_altitude) > BOX_SKY_ENV_ALTITUDE;
    if (env) {
        state->env_generation ++;
        state->env_altitude = altitude;
        state->env_last = *sky;
    }

    state->valid = true;
    sky->lut_dirty = lut ? 1u : 0u;
    sky->view_dirty = view ? 1u : 0u;
    sky->generation = state->generation;
    p->sky_lut_shader = box_sky_handle(world, state->lut_shader);
    p->sky_shader = box_sky_handle(world, state->sky_shader);
    box_sky_environments(p, state->env_generation);
done:
    return;
}
