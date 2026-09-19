/**
 * @file box/sky.h
 * @brief Physically based atmosphere drawn behind the scene, with aerial perspective.
 *
 * An Atmosphere entity describes a planet and the medium around it: the
 * Rayleigh and Mie layers, the ozone and aerosol absorption, the ground albedo
 * and the sun the sky is lit by. The renderer precomputes a transmittance and a
 * multiple scattering table from those parameters, renders a sky view table for
 * the sun direction of the frame and draws the result where the depth target of
 * a view is empty, so the sky is the background of every view of the world.
 *
 * The same tables shade the geometry of the view through an aerial perspective
 * table, which is why distance haze and the sky cannot drift apart, and an
 * `EnvironmentLight` whose `source` names the atmosphere entity prefilters its
 * image based lighting from the sky instead of from its two colors.
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
 * The field is procedural: the direction of a pixel is turned into star space
 * by the rotation of the entity, which is what makes the sky turn over the
 * night, and the cell it lands in decides whether a star sits there. The
 * `CelestialLight` of the same entity gives the field its radiance and color,
 * and daylight fades it out between the two sun elevations.
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
 * The sun, the moon and the stars are entities: the two lights carry a
 * `DirectionalLight` for their direction and a `CelestialLight` for the
 * radiance they have over the medium, and the stars carry a `Stars` with a
 * rotation. The sky attenuates all three with the transmittance of the air.
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
