/**
 * @file sky/fog.c
 * @brief Derives the fog of a view from the medium the sky is rendered from.
 *
 * The fog pass reads a color, a density, a falloff and a height. An atmosphere
 * fills all four from its own coefficients at the altitude of the camera and
 * from the body that lights the view, so the haze in front of a hill, the
 * aerial perspective of the sky pass and the sky behind it are the same air
 * and cannot drift apart over a day. The fog of a Weather thickens that air
 * without touching its color, so a foggy morning is the morning it was, and
 * the overcast of the same weather flattens that color with the sky.
 */

#include "sky_internal.h"
#include <math.h>

/** Index of the red channel of the fog color in the parameter block */
#define BOX_SKY_FOG_COLOR (0)

/** Index of the density of the fog in that block */
#define BOX_SKY_FOG_DENSITY (3)

/** Index of the falloff of the fog in that block */
#define BOX_SKY_FOG_FALLOFF (4)

/** Index of the height the density of the fog is measured at */
#define BOX_SKY_FOG_HEIGHT (7)

/** Altitude in kilometers the ozone layer peaks at */
#define BOX_SKY_FOG_OZONE_PEAK (25.0f)

/** Half width in kilometers of the ozone layer */
#define BOX_SKY_FOG_OZONE_SPAN (15.0f)

/** Weights the luminance of a fog color is measured with */
static const float box_sky_fog_luma[3] = { 0.2126f, 0.7152f, 0.0722f };

/** Scattering and extinction of the medium at one altitude, per channel */
typedef struct box_sky_air_t {
    float rayleigh[3]; /**< Molecular scattering per kilometer */
    float mie[3];      /**< Aerosol scattering per kilometer */
    float extinct[3];  /**< Everything the air takes out of a ray per kilometer */
} box_sky_air_t;

/** Sample the medium of a parameter block at an altitude.
 *
 * @param sky The parameter block holding the medium
 * @param alt The altitude in kilometers
 * @param out The sample to fill
 */
static void box_sky_air(
    const box_sky_t *sky,
    float alt,
    box_sky_air_t *out)
{
    float rayleigh = expf(-alt / sky->bounds[2]);
    float mie = expf(-alt / sky->bounds[3]);
    float ozone = fmaxf(0.0f,
        1.0f - fabsf(alt - BOX_SKY_FOG_OZONE_PEAK) / BOX_SKY_FOG_OZONE_SPAN);

    int32_t i;
    for (i = 0; i < 3; i ++) {
        out->rayleigh[i] = sky->scat_rayleigh[i] * rayleigh;
        out->mie[i] = sky->scat_mie[i] * mie;
        out->extinct[i] = out->rayleigh[i] + sky->ext_mie[i] * mie +
            sky->abs_ozone[i] * ozone + sky->abs_haze[i] * mie;
    }
}

/** Radiance the air in front of the camera scatters toward it.
 *
 * @param air The medium at the camera
 * @param light Radiance of the body that lights the view
 * @param night Radiance the sky keeps after the body has set
 * @param out The three channels to fill
 */
static void box_sky_fog_color(
    const box_sky_air_t *air,
    const float *light,
    const float *night,
    float *out)
{
    int32_t i;
    for (i = 0; i < 3; i ++) {
        float scattered = air->rayleigh[i] * BOX_SKY_FOG_RAYLEIGH_PHASE +
            air->mie[i] * BOX_SKY_FOG_MIE_PHASE;
        float albedo = scattered / fmaxf(air->extinct[i], 1e-9f);
        out[i] = albedo * BOX_SKY_FOG_GAIN * light[i] + night[i];
    }
}

/** Blend the color of the fog toward the grey of its own luminance.
 *
 * The air of an overcast world is lit by a cloud deck instead of by the sun
 * and the blue sky around it, so the haze in front of a hill loses its color
 * with the sky behind it and the two stay the same air.
 *
 * @param overcast Part of the sky the cloud deck of the weather covers
 * @param color The three channels to flatten
 */
static void box_sky_fog_flatten(
    float overcast,
    float *color)
{
    float luma = color[0] * box_sky_fog_luma[0] + color[1] * box_sky_fog_luma[1] +
        color[2] * box_sky_fog_luma[2];

    int32_t i;
    for (i = 0; i < 3; i ++) {
        color[i] += (luma - color[i]) * overcast;
    }
}

bool box_sky_fog(
    ecs_world_t *world,
    const box_view_t *v,
    float *out)
{
    bool result = false;
    ecs_entity_t e = box_sky_entity(world);
    const Atmosphere *a = e ? ecs_get(world, e, Atmosphere) : NULL;
    if (!a || a->fog_intensity < 0.0f) {
        goto done;
    }

    box_sky_t sky;
    box_sky_medium(a, &sky);
    float units = box_sky_or(a->world_units_per_km, BOX_SKY_UNITS_PER_KM);
    float altitude = box_sky_altitude_of(a, v->eye[1]);
    box_sky_air_t air;
    box_sky_air(&sky, fmaxf(altitude, 0.0f), &air);

    float light[3];
    float night[3];
    float intensity = a->night_intensity > 0.0f ? a->night_intensity : 0.0f;
    box_sky_color(a->night_tint, 0, night);

    int32_t i;
    for (i = 0; i < 3; i ++) {
        light[i] = v->sun_color[i] * v->sun_intensity;
        night[i] = night[i] * intensity;
    }

    box_sky_fog_color(&air, light, night, &out[BOX_SKY_FOG_COLOR]);
    WeatherAmounts weather;
    box_sky_weather(world, &weather);
    box_sky_fog_flatten(weather.overcast, &out[BOX_SKY_FOG_COLOR]);
    float gain = 1.0f + weather.fog * (BOX_WEATHER_FOG_GAIN - 1.0f);
    float extinct = (air.extinct[0] + air.extinct[1] + air.extinct[2]) / 3.0f;
    out[BOX_SKY_FOG_DENSITY] = extinct * box_sky_or(a->fog_intensity, 1.0f) * gain / units;
    out[BOX_SKY_FOG_FALLOFF] = 1.0f / (sky.bounds[3] * units);
    out[BOX_SKY_FOG_HEIGHT] = a->sea_level_y;
    result = true;
done:
    return result;
}
