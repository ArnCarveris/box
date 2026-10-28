/**
 * @file box/light.h
 * @brief Light components; color comes from the entity's Rgb and direction from its transform.
 *
 * A light lights only its scene (nearest Scene ancestor, else the default scene); a scene
 * without light entities gets a default sun of intensity pi, View.lighting.enabled false draws
 * base color plus emission unlit. Intensity is irradiance, see DirectionalLight.
 *
 * The ambient of a scene sums its AmbientLight components, set on the Scene root (a singleton
 * for the default scene); a scene without one gets the default sky and ground.
 */

#ifndef BOX_LIGHT_H
#define BOX_LIGHT_H

#include "types.h"

#ifdef BOX_LIGHT_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** How a light takes part in the LightBake of its scene, as Unity's light modes.
 *
 * Mixed is the zero value and means what it says: the light lights directly in realtime and
 * its bounced light is baked, so a light written as a C literal stays in both. In a scene
 * without LightBake every mode lights directly in realtime.
 */
ECS_ENUM(LightBakeMode, {
    LightBakeModeMixed = 0,    /**< Lights directly in realtime and contributes to the bake */
    LightBakeModeRealtime = 1, /**< Lights directly in realtime and is left out of the bake */
    LightBakeModeBaked = 2     /**< Exists only in the probes of the bake, its direct and
                                *   bounced light alike: no realtime light, no per frame cost
                                *   and no view shadows */
});

/** Directional light travelling along the entity's local +Z.
 *
 * Intensity is the linear irradiance on a surface facing the light, Rgb times intensity, as in
 * flecs-engine: a Lambert surface reflects albedo / pi * intensity * NdotL.
 *
 * Intensity pi and a white Rgb light a facing surface at its base color. An Atmosphere computes
 * its sky from the irradiance of its sun and moon.
 */
ECS_STRUCT(DirectionalLight, {
    float intensity;    /**< Irradiance on a facing surface, default 1, 0 lights nothing */
    LightBakeMode bake; /**< Part of the light in the bake of its scene, default Mixed; a sun
                         *   moved by a time of day should be Mixed or Realtime, as a Baked
                         *   one keeps the light of its baked state within the thresholds */
});

/** Omnidirectional light. Intensity is the irradiance on a facing surface 1 unit away: at
 * distance d (from 0.01 up) it is intensity / d^2, windowed by (1 - (d / range)^4)^2 so the light
 * reaches zero at range, and a white diffuse surface reflects that over pi.
 */
ECS_STRUCT(PointLight, {
    float intensity;    /**< Irradiance on a facing surface 1 unit away, default 1, 0 none */
    float range;        /**< Distance at which the light reaches zero, default 10 */
    LightBakeMode bake; /**< Part of the light in the bake of its scene, default Mixed */
});

/** Cone light along the entity's local +Z, angles are half angles in degrees */
ECS_STRUCT(SpotLight, {
    float intensity;    /**< Irradiance on a facing surface 1 unit away, default 1, 0 none */
    float range;        /**< Distance at which the light reaches zero, default 10 */
    float inner_angle;  /**< Full intensity cone half angle, default 20 */
    float outer_angle;  /**< Zero intensity cone half angle, default 30 */
    LightBakeMode bake; /**< Part of the light in the bake of its scene, default Mixed */
});

/** Hemisphere ambient light of the scene the entity is in, blended by the surface normal.
 *
 * The colors are irradiance over pi, so a white surface reflects them at its base color: sky
 * times intensity is the ambient a surface facing up gets, as from a uniform sky of that color.
 */
ECS_STRUCT(AmbientLight, {
    rgb sky;         /**< Display encoded light from above, default {118, 130, 149} */
    rgb ground;      /**< Display encoded light from below, default {79, 74, 68} */
    float intensity; /**< Scale of both colors, default 1 */
});

/** Baked indirect lighting of a scene, placed on a Scene root or as a singleton for the default
 * scene. Adding it adds LightBakeStatus.
 *
 * A bake is bounces + 1 passes: the first captures direct light and emission, each later one
 * adds a bounce. A change rebakes the probes it can reach, which keep their values meanwhile
 * (static shapes, lights). Dynamic shapes rebake none.
 *
 * Values apply as specified: bounces 0 bakes direct emission and light only, budget_ms 0 bakes
 * nothing, a capture_size outside 1 to 256 or bounces outside 0 to 16 bakes nothing and sets
 * LightBakeStatus.error.
 */
ECS_STRUCT(LightBake, {
    float probe_spacing; /**< World units between probes, default 2 */
    int32_t bounces;     /**< Light bounces to bake, 0 to 16, default 2 */
    int32_t capture_size; /**< Face edge of a probe capture in texels, 1 to 256, default 16 */
    float budget_ms;     /**< Render time per frame spent baking in milliseconds, default 2 */
    float bounds_padding; /**< World units added around the static bounds, default 1 */
    char *cache;         /**< Cache folder path, default ".box/bake", empty for none */
    ecs_entity_t environment; /**< Environment source the captures see where no shape covers
                          *   them and the probes start as, default 0 for the hemisphere of
                          *   the AmbientLight */
    float light_threshold; /**< Degrees a directional light turns from the direction it was
                          *   baked with before it or an environment change rebakes, so a
                          *   time of day converges between rebakes, default 2, 0 for any */
    float color_threshold; /**< Change of the linear color of a directional light from the
                          *   baked one, over its brightest channel, before it or an
                          *   environment change rebakes, default 0.05, 0 for any */
});

/** Progress of the bake of a scene, written by the engine next to LightBake */
ECS_STRUCT(LightBakeStatus, {
    int32_t probes;      /**< Probes of the volume */
    int32_t baked;       /**< Valid probes not waiting to be (re)baked in the pass being baked */
    int32_t pass;        /**< Completed capture passes of the bake in progress: the first bakes
                          *   direct light and emission, each later one a bounce; bounces + 1
                          *   when converged */
    bool converged;      /**< All bounces + 1 passes are baked and no change is queued */
    bool cache_hit;      /**< The volume was loaded from the cache */
    char *error;         /**< Why nothing is baked, such as an invalid setting, NULL for none */
});

/** Keeps a static shape out of the bake captures */
extern ECS_TAG_DECLARE(NoBake);

/** Cubemap reflection probes of a scene, placed on a Scene root or as a singleton for the
 * default scene. Adding it adds ReflectionBakeStatus, removing it removes it.
 *
 * Values apply as specified: budget_ms 0 captures nothing, a resolution outside 8 to 512
 * captures nothing and sets ReflectionBakeStatus.error.
 */
ECS_STRUCT(ReflectionBake, {
    int32_t resolution;   /**< Face edge of a probe cubemap in texels, 8 to 512, default 128 */
    float budget_ms;      /**< GPU time per frame spent capturing and prefiltering in
                           *   milliseconds, default 1 */
    bool scene_probe;     /**< The engine adds a probe covering the whole scene, default true */
    float scene_probe_height; /**< Height of the scene probe above the bottom of the static
                               *   bounds of the scene, default 2 */
    ecs_entity_t environment; /**< Environment source the captures see where no shape covers
                               *   them, default 0 for the hemisphere of the AmbientLight */
    float light_threshold; /**< Degrees a directional light turns from the direction it was
                            *   captured with before it or an environment change recaptures,
                            *   default 2, 0 for any; probes show the old capture meanwhile */
    float color_threshold; /**< Change of the linear color of a directional light from the
                            *   captured one, over its brightest channel, before it or an
                            *   environment change recaptures, default 0.05, 0 for any */
});

/** Progress of the reflection probes of a scene, written by the engine next to ReflectionBake */
ECS_STRUCT(ReflectionBakeStatus, {
    int32_t probes;       /**< Probes of the scene, the scene probe included */
    int32_t resident;     /**< Probes that have a cubemap slot; the others did not fit */
    int32_t captured;     /**< Resident probes whose current content is captured and
                           *   prefiltered */
    bool converged;       /**< Every resident probe is captured and nothing is queued */
    char *error;          /**< Why nothing is captured, such as an invalid setting, NULL for
                           *   none */
});

/** Reflection probe of the scene with ReflectionBake the entity is in, placed by the world
 * transform of the entity (position and rotation, scale is ignored).
 *
 * Its boxes are centered on the entity and oriented by its rotation. Where probes overlap,
 * higher priority probes are blended first and cover lower ones; the scene probe is below all.
 */
ECS_STRUCT(ReflectionProbe, {
    vec3 extent;          /**< Half size of the influence box, default {5, 5, 5} */
    float blend;          /**< Distance inside the influence box over which the probe fades
                           *   out, default 1, 0 is a hard edge */
    int32_t priority;     /**< Blend order, higher first, default 0 */
    vec3 projection;      /**< Half size of the box reflections are parallax corrected with,
                           *   default {5, 5, 5}, {0, 0, 0} for an infinitely distant
                           *   environment */
    vec3 offset;          /**< Capture point relative to the entity, default {0, 0, 0} */
});

/** How the mip levels of an environment cubemap are prefiltered from its first level */
ECS_ENUM(EnvironmentFilter, {
    EnvironmentFilterGgx = 0, /**< GGX radiance of a rougher surface at each higher level */
    EnvironmentFilterBox = 1  /**< Mean of the two by two texels of the level below */
});

/** An environment source: an entity whose content (an image, a sky) the engine keeps in a
 * cubemap that views light their scene with through LightSettings.environment.
 *
 * Adding it adds EnvironmentStatus, removing it removes it. A resolution that is not a power
 * of two from 8 to 1024 creates no cubemap and sets EnvironmentStatus.error.
 */
ECS_STRUCT(Environment, {
    int32_t resolution;       /**< Face edge of the cubemap in texels, a power of two from 8
                               *   to 1024, default 256 */
    EnvironmentFilter filter; /**< How the mip levels are prefiltered, default Ggx */
});

/** Progress of an environment source, written by the engine next to Environment */
ECS_STRUCT(EnvironmentStatus, {
    int32_t version;      /**< Content sent to the cubemap so far, 0 for none yet; the source
                           *   increments it each time it sends new content */
    char *error;          /**< Why the source has no content, such as an invalid resolution or
                           *   a missing file, NULL for none */
});

/** An environment source that loads a Radiance .hdr equirectangular image; adding it adds
 * Environment.
 *
 * Column u of the image is the longitude, row v the latitude: the center column looks along
 * -Z, the camera forward, the image goes toward +X to its right and its edges look along +Z.
 *
 * The top row is +Y: direction d is at u = 0.5 + atan2(d.x, -d.z) / 2 pi, v = acos(d.y) / pi.
 */
ECS_STRUCT(EnvironmentImage, {
    char *path;           /**< Image file, relative to the working directory or an ancestor
                           *   of it; a file that cannot be read sets EnvironmentStatus.error.
                           *   A change of the file that FileWatch (box/app.h) sees loads it
                           *   again */
});

/** Register the box.light module.
 *
 * The first directional light of a scene casts the shadows of the views that enable them;
 * NoShadow shapes and blended shapes cast none.
 *
 * In a scene with LightBake the views are shadowed by the first directional light that is not
 * Baked and the probe captures by the first one they shade surfaces with (see LightBakeMode).
 *
 * @param world The world
 */
void BoxLightImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
