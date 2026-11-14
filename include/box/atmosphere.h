/**
 * @file box/atmosphere.h
 * @brief Physically based atmosphere of a scene, after Hillaire 2020; distances are in km.
 *
 * In a scene with an Atmosphere the sun and moon DirectionalLight intensity and Rgb are top of
 * atmosphere values: the light extract attenuates them by the transmittance toward the light.
 */

#ifndef BOX_ATMOSPHERE_H
#define BOX_ATMOSPHERE_H

#include "types.h"

#ifdef BOX_ATMOSPHERE_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Rayleigh scattering of the air molecules, density falling off exponentially with altitude */
ECS_STRUCT(AtmosphereRayleigh, {
    float scale_height;     /**< Altitude in km over which the density falls by 1/e, default 8 */
    float scattering_scale; /**< Scale of the Earth scattering coefficients, default 1, 0 for
                             *   no Rayleigh scattering */
});

/** Mie scattering and absorption of the aerosols, density falling off exponentially */
ECS_STRUCT(AtmosphereMie, {
    float scale_height;     /**< Altitude in km over which the density falls by 1/e, default 1.2 */
    float anisotropy;       /**< Cornette-Shanks phase asymmetry g, -1 < g < 1, default 0.8 */
    float scattering_scale; /**< Scale of the Earth scattering and extinction coefficients,
                             *   default 1, 0 for no aerosols */
    rgb tint;               /**< Display encoded tint of the coefficients, default
                             *   {255, 255, 255} */
});

/** Atmosphere of a scene, placed on an entity of the scene: the Scene root, an entity under it,
 * or an entity outside any Scene for the default scene. Adding it adds AtmosphereStatus,
 * removing it removes it.
 *
 * The entity is the environment source of the sky of its scene (LightSettings.environment):
 * Atmosphere adds Environment {128, Box} unless the entity has one. The sky has no sun or moon
 * disk; the sun or moon moving ibl_update_angle or changing color by 2% renders it again.
 *
 * Values apply as specified; a planet_radius, thickness, units_per_km or scale height that is
 * not positive or another negative distance sets AtmosphereStatus.error and renders nothing.
 */
ECS_STRUCT(Atmosphere, {
    ecs_entity_t sun;       /**< DirectionalLight that is the sun, default 0 for no sun */
    ecs_entity_t moon;      /**< DirectionalLight that is the moon, default 0 for no moon; its
                             *   light is scaled by AtmosphereStatus.moon_illumination */
    float planet_radius;    /**< Radius of the planet in km, default 6360 */
    float thickness;        /**< Height of the atmosphere above the ground in km, default 100 */
    float ground_altitude;  /**< Altitude in km at sea_level, 0 to thickness, default 0.5 */
    float sea_level;        /**< World Y of the ground_altitude, default 0 */
    float units_per_km;     /**< World units in a km, default 1000 */
    AtmosphereRayleigh rayleigh; /**< Air molecules, default {8, 1} */
    AtmosphereMie mie;      /**< Aerosols, default {1.2, 0.8, 1, {255, 255, 255}} */
    float ozone_scale;      /**< Scale of the absorption of the ozone layer, a tent around 25
                             *   km, default 1, 0 for no ozone */
    rgb ground_albedo;      /**< Display encoded albedo of the planet, default {77, 77, 77} */
    rgb night_tint;         /**< Display encoded color of the night sky, default
                             *   {70, 110, 230} */
    float night_intensity;  /**< Scale of the night sky color, default 0.003 */
    float sun_disk_intensity; /**< Scale of the drawn sun disk, default 1, 0 draws none */
    float sun_radius;       /**< Angular radius of the sun disk in degrees, default 0.266 */
    float moon_disk_intensity; /**< Scale of the drawn moon disk, default 1, 0 draws none */
    float moon_radius;      /**< Angular radius of the moon disk in degrees, default 0.259 */
    float moon_albedo;      /**< Fraction of sunlight the moon reflects, default 0.12 */
    float earthshine;       /**< Fraction of the radiance of the fully lit moon disk that its
                             *   unlit part shows, default 0 */
    float aerial_distance;  /**< Far end of the aerial perspective volume in km, default 32 */
    float aerial_intensity; /**< Scale of the aerial perspective, default 1, 0 for none */
    float ibl_update_angle; /**< Degrees the sun or moon moves before the environment is
                             *   rendered again, default 0.5 */
});

/** State of the atmosphere of a scene, written by the engine next to Atmosphere.
 *
 * The transmittances are zero for a missing light and for a light below the planet's horizon,
 * every value but lut_version is zero while error is set.
 */
ECS_STRUCT(AtmosphereStatus, {
    vec3 sun_transmittance;  /**< Linear rgb transmittance from the ground altitude toward the
                              *   sun */
    vec3 moon_transmittance; /**< Linear rgb transmittance from the ground altitude toward the
                              *   moon */
    float moon_illumination; /**< Lit fraction of the moon disk seen from the ground, 0 to 1 */
    int32_t lut_version;     /**< Incremented each time the transmittance and multi scattering
                              *   lookup tables are built again */
    char *error;             /**< Why nothing renders, such as an invalid value, NULL for none */
});

/** Atmosphere of a planet seen from space, a shell around the ground sphere of the planet whose
 * center is the world position of the entity. Adding it adds PlanetAtmosphereStatus, removing it
 * removes it.
 *
 * Unlike Atmosphere it does not attenuate any DirectionalLight in the light extract, adds no
 * Environment and draws no sky, sun disk or clouds; it leaves the Atmosphere of its scene alone,
 * and both can be in one scene. A scene can have many.
 *
 * Every view of its scene that is lit and has a depth draws the shell after its opaque meshes,
 * its sky and the blended layers around its ground, and before its fog volumes, particles, 2D
 * shapes and other blended draws. A blended layer is a mesh of the entity or its descendants
 * whose bounds hold the ground sphere and lie within the top sphere, such as a cloud sphere: it
 * is hazed with the ground below it. Other transparent geometry, such as the ring of a gas giant
 * or a window near the camera, is not hazed. Transmissive meshes
 * draw before the shell and are hazed as opaque ones at their depth. Each pixel of the shell
 * marches the ray inside the atmosphere up to the ground sphere or the scene depth with single
 * and multi scattering of the sun, and blends as dst * transmittance + inscatter * intensity,
 * the transmittance the mean of its channels. Reflection captures, reflection probes and
 * environment cubes do not include the shell.
 *
 * Its transmittance and multi scattering LUTs are built again only when the medium changes.
 *
 * With Scene.planet_light the meshes of the entity and its descendants, such as the planet body
 * and its cloud sphere, opaque, blended or transmissive, get the light of the sun attenuated per
 * fragment, which reddens and darkens the terminator.
 *
 * At p = (world - center) * km per unit, r = max(|p|, planet_radius) and mu = dot(p / |p|, sun)
 * the sun is multiplied by the transmittance LUT at (r, mu) and the visible fraction of the sun
 * disk of sun_radius above the horizon of the planet at r, its penumbra. A mesh belongs to its
 * nearest PlanetAtmosphere along ChildOf.
 *
 * Only the DirectionalLight along the direction of the sun is attenuated; other lights, the
 * ambient, the image based lighting and the meshes outside any PlanetAtmosphere are lit as
 * without one. The meshes of at most 63 planets of a world are lit so, further ones are not.
 *
 * Values apply as specified; a radius, planet_radius, thickness or scale height that is not
 * positive, a negative scale or sun_radius or an anisotropy outside -1 to 1 sets
 * PlanetAtmosphereStatus.error and renders nothing.
 */
ECS_STRUCT(PlanetAtmosphere, {
    ecs_entity_t sun;       /**< DirectionalLight that lights it, default 0 for the first
                             *   DirectionalLight of the scene; its intensity and Rgb are top
                             *   of atmosphere values */
    float radius;           /**< Radius of the ground sphere in the local units of the
                             *   entity, default 1; it is multiplied by the uniform world
                             *   scale of the entity, the geometric mean of the axis scales
                             *   (non-uniform scale is not supported) */
    float planet_radius;    /**< The same radius in km, default 6360; with the scaled radius
                             *   it sets the km in a world unit */
    float thickness;        /**< Height of the atmosphere above the ground in km, default 100 */
    AtmosphereRayleigh rayleigh; /**< Air molecules, default {8, 1} */
    AtmosphereMie mie;      /**< Aerosols, default {1.2, 0.8, 1, {255, 255, 255}}; the tint
                             *   colors the haze */
    float ozone_scale;      /**< Scale of the absorption of the ozone layer, default 1 */
    rgb ground_albedo;      /**< Display encoded albedo of the planet, which the multi
                             *   scattering LUT bounces, default {77, 77, 77} */
    float intensity;        /**< Scale of the drawn in scattered light, default 1 */
    float sun_radius;       /**< Angular radius of the sun disk in degrees as seen from the
                             *   planet, the penumbra of the shadow of the planet on its
                             *   meshes, default 0.266 as Atmosphere.sun_radius */
});

/** State of the atmosphere of a planet, written by the engine next to PlanetAtmosphere */
ECS_STRUCT(PlanetAtmosphereStatus, {
    int32_t lut_version;    /**< Incremented each time the transmittance and multi scattering
                             *   lookup tables are built again */
    char *error;            /**< Why nothing renders, such as an invalid value, NULL for none */
});

/** Layers of clouds a scene draws at most, see CloudLayer */
#define BOX_CLOUD_LAYER_MAX (4)

/** A horizontal plane of clouds at an altitude, drawn from a tileable texture in the sky of the
 * Atmosphere of its scene. Place it on any entity of a scene with an Atmosphere, the entity of
 * the Atmosphere or another one, one layer per entity.
 *
 * The sky pass draws the layers where no geometry is, so clouds are behind every mesh. A ray
 * from the camera hits the plane y = altitude, ahead of the camera, at a point whose texture
 * value t becomes the coverage c = saturate((t - threshold) / (2 * softness) + 0.5) and the
 * opacity saturate(c * density). The cloud is lit by the sun and moon through the atmosphere,
 * with a forward glow toward them, and by the sky, then fades into the haze with distance; a
 * camera above the plane sees its top. Layers composite from far to near along each ray.
 *
 * A scene draws at most BOX_CLOUD_LAYER_MAX layers, the first ones by entity id; more are
 * ignored with a warning. Views that render the atmosphere (LightSettings.atmosphere) draw the
 * clouds, views with another environment as background do not.
 *
 * The environment cubemap of the Atmosphere includes the layers, seen from world X and Z 0 at
 * the sea_level, so image based lighting, the ambient and reflections show them, and so do the
 * reflection captures where they see the environment. Adding, removing or changing a layer
 * renders the cubemap again. A moving layer, by offset or velocity, renders it again only once
 * its offset, wrapped to size, moved more than 1/64 of size since the last render, the shortest
 * way around the wrap: wind renders it periodically, every size / (64 * |velocity|) seconds,
 * not every frame. Such a move does not capture reflection probes or probe volumes again; they
 * keep the clouds of their last capture.
 *
 * size, altitude, offset and velocity mean what size, height, offset and velocity of a
 * LightCookie with the Plane projection mean: a cookie on the sun with the same texture and
 * values casts the shadows of these clouds, moving with them.
 */
ECS_STRUCT(CloudLayer, {
    ecs_entity_t texture;   /**< Any texture entity, a TextureGen (GenHeight is ideal) or a
                             *   TexturePath, whose red channel is read as stored; default 0 for
                             *   none, no clouds until it is loaded */
    float size;             /**< World units the texture repeats over, default 4000; none at or
                             *   below 0 */
    float altitude;         /**< World Y of the layer, default 1500 */
    vec2 offset;            /**< World units the pattern is moved by along world X and Z, the
                             *   texture read at (x + offset) / size, default 0 */
    vec2 velocity;          /**< Wind: world units per second the pattern moves along X and Z,
                             *   default 0; offset plus velocity times the total time is
                             *   computed in double precision and wrapped to size */
    float threshold;        /**< Value at which the coverage is one half, default 0.55 */
    float softness;         /**< Half the range of values the coverage ramps over, default
                             *   0.15; 0 is a hard step at threshold */
    float density;          /**< Opacity of fully covered clouds, default 1, 0 for none */
    rgb tint;               /**< Display encoded color the lit cloud is multiplied by, default
                             *   {255, 255, 255} */
});

/** Time of day of a scene, placed on a Scene root or as a singleton for the default scene.
 * Adding it adds TimeOfDayStatus, removing it removes it.
 *
 * The sun and moon positions come from Paul Schlyter's orbital elements for the year 2000 at
 * Greenwich longitude. The engine writes the Rotation of their lights to travel along local +Z
 * toward the ground; a prefab's Rotation becomes owned, and a light needs no rotated parent.
 *
 * Without an Atmosphere in the scene the light extract attenuates the sun and moon by the air
 * mass, warms their color toward the horizon and fades them out below it; the authored
 * DirectionalLight and Rgb are left as they are.
 */
ECS_STRUCT(TimeOfDay, {
    float hour;             /**< Local apparent solar time, 0 to 24, 12 has the sun on the
                             *   meridian, default 12 */
    int32_t day_of_year;    /**< Day of the year, 1 to 365, default 80 (about the March
                             *   equinox) */
    float latitude;         /**< Latitude in degrees, -90 to 90, default 45 */
    float north;            /**< World yaw of north in degrees about +Y, as Rotation yaw turns
                             *   +Z; default 0 puts north on +Z and east on -X (right seen from
                             *   above, north up), 90 makes +X north and +Z east */
    float time_scale;       /**< Simulated hours per real second, default 0 for paused */
    float moon_offset;      /**< Hours added to the time of the moon's position, default 0 */
    ecs_entity_t sun;       /**< DirectionalLight that is the sun, default 0 for the sun of the
                             *   scene's Atmosphere, or the first DirectionalLight of the scene
                             *   without one */
    ecs_entity_t moon;      /**< DirectionalLight that is the moon, default 0 for the moon of
                             *   the scene's Atmosphere, none without one */
});

/** State of the time of day of a scene, written by the engine next to TimeOfDay.
 *
 * Setting TimeOfDay restarts the status at its hour and day; with a time_scale the hour then
 * advances by time_scale times the frame delta, wrapping into the next day and year.
 */
ECS_STRUCT(TimeOfDayStatus, {
    float hour;             /**< Current local apparent solar time, 0 to 24 */
    int32_t day_of_year;    /**< Current day of the year, 1 to 365 */
    float sun_altitude;     /**< Degrees of the sun above the horizon, -90 to 90 */
    float sun_azimuth;      /**< Degrees of the sun from north toward east, 0 to 360 */
    float moon_altitude;    /**< Degrees of the moon above the horizon, -90 to 90 */
    float moon_azimuth;     /**< Degrees of the moon from north toward east, 0 to 360 */
    float moon_illumination; /**< Lit fraction of the moon disk, 0 for a new moon to 1 for a
                              *   full moon */
    float daylight;         /**< Brightness of the sky, 0 at night to 1 in full daylight, with
                             *   a little moonlight */
});

/** Register the box.atmosphere module.
 *
 * Makes the light extract attenuate the sun and moon of every scene with a valid Atmosphere by
 * the transmittance toward them and a fade over the disk's angular diameter at the horizon,
 * and the sun and moon of a TimeOfDay without an Atmosphere by its fallback.
 *
 * @param world The world
 */
void BoxAtmosphereImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
