/**
 * @file box/sky.h
 * @brief Sky components: atmosphere, stars, time of day, weather and rain.
 *
 * The atmosphere and the star field are what the sky of a world is rendered
 * from, the clock places the sun and the moon in it, and the weather eases the
 * overcast and the rain the sky and the rain column are drawn with.
 */

#ifndef BOX_SKY_H
#define BOX_SKY_H

#include "types.h"

#ifdef BOX_SKY_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Air molecules, which scatter short wavelengths and make the sky blue */
ECS_STRUCT(AtmosphereRayleigh, {
    float scale_height_km;  /**< Altitude the density falls by e over, 0 takes 8 */
    float scattering_scale; /**< Multiplier of the scattering coefficients, 0 takes 1 */
});

/** Aerosols, which scatter every wavelength forward and make the sun hazy */
ECS_STRUCT(AtmosphereMie, {
    float scale_height_km;  /**< Altitude the density falls by e over, 0 takes 1.2 */
    float anisotropy;       /**< Forward scattering of the phase function, 0 is uniform */
    float scattering_scale; /**< Multiplier of the scattering coefficients, 0 takes 1 */
    rgb tint;               /**< Color of the aerosols, black takes white */
});

/** Star field drawn behind the sky, on an entity with a rotation
 *
 * The procedural field turns with the rotation of the entity. The
 * `CelestialLight` of the same entity gives its radiance and color, and
 * daylight fades it out between the two sun elevations.
 */
ECS_STRUCT(Stars, {
    float density;    /**< Cells the field is cut into per radian, 0 takes 140 */
    float size;       /**< Angular radius of a star in radians, 0 takes 0.0016 */
    float twinkle;    /**< Amount the field flickers over time, 0 is steady */
    float fade_start; /**< Sine of the sun elevation the fade starts at, 0 takes -0.18 */
    float fade_end;   /**< Sine of the sun elevation the field is gone at, 0 takes 0.02 */
});

/** Planet and medium the sky of a world is rendered from
 *
 * The sun and moon are entities with a `DirectionalLight` and a `CelestialLight`,
 * the stars an entity with `Stars` and a rotation. The sky attenuates all three
 * with the transmittance of the air.
 */
ECS_STRUCT(Atmosphere, {
    ecs_entity_t sun;   /**< Directional light the sky is lit by, 0 takes the first one */
    ecs_entity_t moon;  /**< Light of the moon disk, 0 draws no moon */
    ecs_entity_t stars; /**< Entity holding the Stars field, 0 draws no stars */

    float sun_disk_intensity;      /**< Radiance of the disk itself, 0 draws no disk */
    float sun_disk_angular_radius; /**< Half angle of the disk in radians, 0 takes 0.00465 */

    float moon_angular_radius; /**< Half angle of the moon disk in radians, 0 takes 0.00452 */

    float aerial_perspective_distance_km; /**< Distance the haze table reaches, 0 takes 32 */
    float aerial_perspective_intensity;   /**< Density of that haze, 0 takes 1, below 0 is none */

    float sea_level_y;        /**< World height the ground altitude is measured from */
    float world_units_per_km; /**< World units in one kilometer, 0 takes 1000 */
    float ground_altitude_km; /**< Altitude of the sea level plane over the planet */

    float planet_radius_km;        /**< Radius of the planet, 0 takes 6360 */
    float atmosphere_thickness_km; /**< Height of the medium over it, 0 takes 100 */

    AtmosphereRayleigh rayleigh; /**< The molecular layer */
    AtmosphereMie mie;           /**< The aerosol layer */

    float ozone_scale;                 /**< Ozone absorption, 0 takes 1, below 0 is none */
    float stratospheric_aerosol_scale; /**< High altitude aerosol absorption, 0 is none */
    float haze_absorption;             /**< Grey absorption in the aerosol layer, 0 is none */

    rgb ground_albedo; /**< Color the planet reflects back into the sky */

    rgb night_tint;        /**< Color the sky keeps after the sun has set */
    float night_intensity; /**< Radiance of that color, 0 leaves the night black */

    float turbulence;       /**< Warp of the view ray, 0 leaves the sky steady */
    float turbulence_scale; /**< Spatial frequency of that warp, 0 takes 40 */
    float turbulence_speed; /**< Speed of that warp, 0 takes 1 */
});

/** Simulated clock that drives the sun, the moon and the stars of a world
 *
 * The clock turns an hour, a date and a latitude into the `Rotation` of the sun
 * and moon entities; a write takes effect in the same frame.
 *
 * The moon trails the sun by `moon_position_offset` hours: twelve is a full
 * moon opposite the sun, zero a new moon. The drawn phase follows from both.
 */
ECS_STRUCT(TimeOfDay, {
    ecs_entity_t light;      /**< Sun light, 0 takes the sun of the atmosphere */
    ecs_entity_t moon_light; /**< Moon light, 0 takes the moon of the atmosphere */
    ecs_entity_t atmosphere; /**< Atmosphere to read those from, 0 takes the one of the world */
    ecs_entity_t stars;      /**< Star field, 0 takes the one of the atmosphere */

    float hour;        /**< Local solar time from 0 to 24, wraps into the next day */
    float day_of_year; /**< Day of the year from 1 to 365, 0 takes 172 */
    float latitude;    /**< Latitude in degrees, north positive */
    float north_offset; /**< Degrees the compass of the world is turned about its Y axis */

    float time_scale;           /**< Simulated hours per real second, 0 holds the clock */
    float moon_position_offset; /**< Hours the moon trails the sun, 12 is a full moon */

    float daylight; /**< Written every frame: 0 in the dark, 1 with the sun up */
});

/** Overcast and rain of a weather, each from 0 to 1 */
ECS_STRUCT(WeatherAmounts, {
    float overcast; /**< Part of the sky the cloud deck covers */
    float rain;     /**< Part of the rain that falls on the world */
});

/** Weather of a world, which eases toward the amounts it names over time */
ECS_STRUCT(Weather, {
    float overcast;   /**< Cloud deck the sky is blended toward, 0 to 1 */
    float rain;       /**< Rain that falls on the world, 0 to 1 */
    float transition; /**< Seconds an amount eases over, 0 takes four */

    WeatherAmounts current; /**< Written every frame: the amounts of this frame */
});

/** Rain column the weather of a world drives, as a singleton
 *
 * The amount is `Weather.rain`; this is the column it falls in. The emitter is
 * created when a world first rains and follows the camera of the first view.
 *
 * `Weather.rain` scales the rate, drop size and wind. A world without the
 * component rains with the defaults below, which fall straight down.
 */
ECS_STRUCT(Rain, {
    int32_t max;   /**< Drops alive at once, 0 takes twenty thousand */
    float rate;    /**< Drops spawned per second at a full rain, 0 takes eight thousand */
    float area;    /**< Half extent of the column around the camera, 0 takes 24 */
    float height;  /**< World units the column reaches around the camera, 0 takes 24 */
    float ground;  /**< Height in world units a drop dies at */
    float speed;   /**< Speed a drop falls at once it is up to speed, 0 takes 14 */
    float size;    /**< World units across a drop is at a full rain, 0 takes 0.04 */
    float stretch; /**< World units a drop stretches per unit of its speed, 0 takes 0.03 */
    float drag;    /**< Fraction of the difference with the wind a drop loses per second */
    float opacity; /**< Opacity a drop is drawn with, 0 takes 0.35 */
    vec3 wind;     /**< Wind at a full rain, in world units per second */
    rgb color;     /**< Color of a drop, black takes a pale grey */
});

/** Day of the year a clock without one runs on, the summer solstice */
#define BOX_TIME_DAY_OF_YEAR (172.0f)

/** Axial tilt of the planet in degrees, which is what makes the seasons */
#define BOX_TIME_TILT (23.44f)

/** Days in the year the clock divides the ecliptic over */
#define BOX_TIME_YEAR (365.0f)

/** Degrees the planet turns in one hour */
#define BOX_TIME_HOUR_ANGLE (15.0f)

/** Sine of the sun elevation the daylight factor reaches one at */
#define BOX_TIME_DAYLIGHT_HIGH (0.1045f)

/** Sine of the sun elevation the daylight factor leaves zero at */
#define BOX_TIME_DAYLIGHT_LOW (-0.1045f)

/** Seconds a weather amount eases over when the component names none */
#define BOX_WEATHER_TRANSITION (4.0f)

/** Distance from its target an easing amount is snapped onto it at */
#define BOX_WEATHER_EPSILON (0.0005f)

/** Part of the direct light a full overcast leaves on the scene */
#define BOX_WEATHER_OVERCAST_LIGHT (0.45f)

/** Overcast a full rain adds to the sky of a world on top of its own */
#define BOX_WEATHER_RAIN_OVERCAST (0.6f)

/** Rain under which a world keeps no emitter at all */
#define BOX_RAIN_EPSILON (0.001f)

/** Drops a full rain spawns per second when the Rain names no rate */
#define BOX_RAIN_RATE (8000.0f)

/** Drops the pool of a rain holds when the Rain names no maximum */
#define BOX_RAIN_MAX (20000)

/** Half extent of the column around the camera when the Rain names none */
#define BOX_RAIN_AREA (24.0f)

/** World units the column reaches around the camera when the Rain names none */
#define BOX_RAIN_HEIGHT (24.0f)

/** Part of the height of the column that sits over the camera */
#define BOX_RAIN_LIFT (0.25f)

/** Speed in world units per second a drop falls at by default */
#define BOX_RAIN_SPEED (14.0f)

/** World units across a drop is at a full rain by default */
#define BOX_RAIN_SIZE (0.04f)

/** World units a drop stretches per unit of the speed it falls at, by default */
#define BOX_RAIN_STRETCH (0.03f)

/** Fraction of the difference with the wind a drop loses per second by default */
#define BOX_RAIN_DRAG (0.6f)

/** Opacity a drop is drawn with by default */
#define BOX_RAIN_OPACITY (0.35f)

/** Part of the size of a drop that is left where the rain is at its lightest */
#define BOX_RAIN_SIZE_LOW (0.55f)

/** Part of its fall a drop is given on top of it before its life runs out */
#define BOX_RAIN_LIFE_MARGIN (1.5f)

/** Channel a drop of a rain that names no color is drawn with */
#define BOX_RAIN_COLOR (210)

/** Seed the drops of a rain are drawn with, which keeps a capture reproducible */
#define BOX_RAIN_SEED (17)

/** Default half angle of the sun disk in radians */
#define BOX_SKY_DISK_RADIUS (0.00465f)

/** Default half angle of the moon disk in radians */
#define BOX_SKY_MOON_RADIUS (0.00452f)

/** Default cells the star field is cut into per radian */
#define BOX_SKY_STARS_DENSITY (140.0f)

/** Default angular radius of a star in radians */
#define BOX_SKY_STARS_SIZE (0.0016f)

/** Default sine of the sun elevation the star field starts to fade at */
#define BOX_SKY_STARS_FADE_START (-0.18f)

/** Default sine of the sun elevation the star field has faded out at */
#define BOX_SKY_STARS_FADE_END (0.02f)

/** Default kilometers the aerial perspective table reaches */
#define BOX_SKY_AERIAL_KM (32.0f)

/** Default world units in one kilometer */
#define BOX_SKY_UNITS_PER_KM (1000.0f)

/** Default altitude of the sea level plane over the planet, in kilometers */
#define BOX_SKY_GROUND_KM (0.5f)

/** Default radius of the planet in kilometers */
#define BOX_SKY_PLANET_KM (6360.0f)

/** Default height of the medium over the planet, in kilometers */
#define BOX_SKY_THICKNESS_KM (100.0f)

/** Default altitude the molecular density falls by e over, in kilometers */
#define BOX_SKY_RAYLEIGH_KM (8.0f)

/** Default altitude the aerosol density falls by e over, in kilometers */
#define BOX_SKY_MIE_KM (1.2f)

/** Default forward scattering of the aerosol phase function */
#define BOX_SKY_MIE_G (0.8f)

/** Default spatial frequency of the turbulence warp */
#define BOX_SKY_TURBULENCE_SCALE (40.0f)

/** Ground albedo channel of an atmosphere that names none */
#define BOX_SKY_GROUND_ALBEDO (77)

/** Radians the sun may move before the environment of a view is filtered again */
#define BOX_SKY_ENV_EPSILON (0.015f)

/** Kilometers the camera may rise before that environment is filtered again */
#define BOX_SKY_ENV_ALTITUDE (0.2f)

/** Radians the sun may move before the sky view table is rendered again */
#define BOX_SKY_SUN_EPSILON (0.002f)

/** Kilometers the camera may rise before the sky view table is rendered again */
#define BOX_SKY_ALTITUDE_EPSILON (0.01f)

/** Register the box.sky module.
 *
 * @param world The world
 */
void BoxSkyImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
