/**
 * @file box/light.h
 * @brief Light components; color comes from the entity's Rgb and direction from its transform.
 *
 * A light lights only its scene (nearest Scene ancestor, else the default scene); a scene
 * without lights gets a default sun of intensity pi unless it has a valid Atmosphere. Intensity
 * is irradiance, see DirectionalLight; View.lighting.enabled false draws base color and emission.
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

/** Cluster lookups of the point and spot lights a FogVolume ray makes at most, and the lights
 * of each lookup it marches at most; FOG_LIGHT_SEGMENTS in etc/shaders/fog_volume.wgsl */
#define BOX_FOG_VOLUME_LIGHT_STEPS (8)

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

/** How a LightCookie maps world positions onto its texture */
ECS_ENUM(LightCookieProjection, {
    LightCookieProjectionPlane = 0, /**< Along the light onto the horizontal plane at height:
                                     *   the pattern stays fixed in the world and its shadows
                                     *   move over the ground as the light turns, as clouds do */
    LightCookieProjectionLight = 1  /**< In the plane of the local X and Y axes of the light,
                                     *   so the pattern follows its rotation */
});

/** A tileable single channel texture whose value scales the direct light (diffuse, specular
 * and shadows) of the DirectionalLight it is set on, in the forward pass and for lit particles:
 * noise for cloud shadows, a mask for a classic cookie. Ambient and captures are not affected.
 *
 * Only the first directional light of a scene, the one with the cascaded shadows, uses its
 * cookie; on another light it has no effect yet. Neither has a size at or below 0 or strength 0.
 */
ECS_STRUCT(LightCookie, {
    ecs_entity_t texture; /**< TexturePath with single_channel, 0 for none, read as stored;
                           *   another texture reads its stored red channel (no sRGB decoding);
                           *   no effect until it is loaded */
    LightCookieProjection projection; /**< Plane: p moves along the direction l toward the
                           *   light to q = p + l * (height - p.y) / l.y, read at q.xz; Light:
                           *   read at p along the local X and Y of the light. Default Plane */
    float size;           /**< World units the texture repeats over, default 4000 */
    float height;         /**< World Y of the plane, Plane only, default 1500: positions at or
                           *   above it and a light at or below the horizon are not affected */
    vec2 offset;          /**< World units the pattern is moved by, along world X and Z (Plane)
                           *   or the local X and Y of the light (Light), default 0 */
    vec2 velocity;        /**< Wind: world units per second the pattern moves along the same
                           *   axes, default 0; offset plus velocity times the total time is
                           *   computed in double precision and wrapped to size */
    float threshold;      /**< The value t read is remapped to lit = saturate((t - threshold) /
                           *   (2 * softness) + 0.5), default 0.5 */
    float softness;       /**< Default 0.5, which with threshold 0.5 passes t through; 0 is a
                           *   hard step at threshold */
    float strength;       /**< The cookie is mix(1, lit, strength), default 1 */
});

/** Omnidirectional light. Intensity is the irradiance on a facing surface 1 unit away: at
 * distance d (from 0.01 up) it is intensity / d^2, windowed by (1 - (d / range)^4)^2 so the light
 * reaches zero at range, and a white diffuse surface reflects that over pi.
 *
 * A light with shadows gets a square tile of the ShadowAtlas of its scene while a view of the
 * scene that renders shadows (ShadowSettings.enabled) sees its range, as a spot light does.
 *
 * The tile holds the whole sphere around the light as an octahedron unfolded into the square, 8
 * perspective octants from 0.005 range (at least 0.001) to range; casters beyond it cast none.
 */
ECS_STRUCT(PointLight, {
    float intensity;    /**< Irradiance on a facing surface 1 unit away, default 1, 0 none */
    float range;        /**< Distance at which the light reaches zero, default 10 */
    LightBakeMode bake; /**< Part of the light in the bake of its scene, default Mixed */
    bool shadows;       /**< The light casts shadows in the views of its scene that render
                         *   them, default false; a Baked light has no realtime light and casts
                         *   none */
    int32_t shadow_resolution; /**< Edge of its tile in texels, rounded down to a power of two
                         *   and clamped to ShadowAtlas min_tile and resolution, default 512;
                         *   a light shrinks below it when the atlas is full */
});

/** Cone light along the entity's local +Z, angles are half angles in degrees.
 *
 * A light with shadows gets a tile of the ShadowAtlas of its scene while a view of the scene
 * that renders shadows (ShadowSettings.enabled) sees its range: a reversed Z perspective along
 * +Z from 0.005 range (at least 0.001) to range.
 *
 * The tile covers the outer angle plus the reach of the shadow filter, at most 80 degrees; a
 * wider cone is unshadowed beyond that.
 */
ECS_STRUCT(SpotLight, {
    float intensity;    /**< Irradiance on a facing surface 1 unit away, default 1, 0 none */
    float range;        /**< Distance at which the light reaches zero, default 10 */
    float inner_angle;  /**< Full intensity cone half angle, default 20 */
    float outer_angle;  /**< Zero intensity cone half angle, default 30 */
    LightBakeMode bake; /**< Part of the light in the bake of its scene, default Mixed */
    bool shadows;       /**< The light casts shadows in the views of its scene that render
                         *   them, default false; a Baked light has no realtime light and casts
                         *   none */
    int32_t shadow_resolution; /**< Edge of its tile in texels, rounded down to a power of two
                         *   and clamped to ShadowAtlas min_tile and resolution, default 512;
                         *   a light shrinks below it when the atlas is full */
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

/** The depth atlas the shadow casting spot and point lights of a scene render their tiles into,
 * one tile per light, placed on a Scene root or as a singleton for the default scene; a scene
 * without one uses the defaults.
 *
 * Lights get power of two tiles nearest to the cameras first: the largest that fits in the area
 * still free up to its shadow_resolution, at least min_tile, else none. At most 64 tiles, placed
 * by size and entity; one renders again only when its light, place or a caster in range changed.
 *
 * Values that are not powers of two round down to one; resolution clamps to 256 to 8192 and
 * min_tile to 1 to resolution.
 */
ECS_STRUCT(ShadowAtlas, {
    int32_t resolution;  /**< Edge of the atlas in texels, default 2048 */
    int32_t min_tile;    /**< Smallest tile edge a light is shrunk to before it gets none,
                          *   default 64 */
});

/** Baked indirect lighting of a scene, placed on a Scene root or as a singleton for the default
 * scene. Adding it adds LightBakeStatus.
 *
 * A bake is bounces + 1 passes: direct light and emission, then a pass per bounce. A change of
 * static shapes or lights rebakes the probes it can reach; dynamic shapes rebake none. Probes
 * no static surface blends are not baked: a dynamic shape in open air shows the ambient.
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

/** A box of fog the lit views of its scene with LightSettings.fog_volumes raymarch, placed by the
 * position and rotation of the entity as a ReflectionProbe is (scale ignored). Probe and
 * reflection captures and bakes do not see it.
 *
 * It draws in the forward pass after the opaque meshes and the sky, back to front, at most 32 in
 * the frustum per view, the nearest kept. Glass shows it through its snapshot; blended shapes and
 * particles in or behind it are not fogged, and a view without clear keeps its alpha.
 *
 * It is lit by the ambient of the view (environment or hemisphere, not the probe volume), the
 * first directional light through its cascades and LightCookie, and the point and spot lights of
 * the clusters of the view through their shadow tiles, dimmed by the aerial perspective of the
 * view; further directional lights are not marched. To bound the cost, a ray looks up its
 * clusters at most BOX_FOG_VOLUME_LIGHT_STEPS times: the march is split into that many
 * segments, each lit by the first BOX_FOG_VOLUME_LIGHT_STEPS lights of the cluster at its middle,
 * so a dense cluster or a segment that crosses several clusters may lose lights. Their shadows
 * are one depth comparison per step, without the filter of the meshes.
 */
ECS_STRUCT(FogVolume, {
    vec3 extent;             /**< Half size of the box, default {5, 5, 5} */
    float density;           /**< Extinction per world unit, default 0.1 */
    rgb color;               /**< Color of the scattered light, display encoded, default
                              *   white */
    float absorption;        /**< Share of the extinction that is absorbed, 0 to 1, default 0 */
    float asymmetry;         /**< Henyey-Greenstein g, -0.9 to 0.9, default 0.3: positive
                              *   scatters forward, so the fog is brighter looking toward the
                              *   sun */
    float edge_fade;         /**< Distance inside the box over which density fades to 0 at its
                              *   faces with a smoothstep, default 1, 0 for hard faces */
    float sun_inscatter;     /**< Scale of the sun, default 1 */
    float ambient_inscatter; /**< Scale of the ambient light, default 1 */
    float light_inscatter;   /**< Scale of the point and spot lights, default 1, 0 skips their
                              *   lookup */
    int32_t steps;           /**< March steps across a volume, default 24, clamped to 4..128;
                              *   a ray that crosses part of the box takes its share of them,
                              *   at least 4, or 8 with noise */
    float noise_strength;    /**< 0 constant density, 1 the density goes from 0 to twice its
                              *   value with a tileable 3D noise, clamped to 0..1, default 0 */
    float noise_scale;       /**< World size of the largest billows, default 4. The noise is in
                              *   world space, not in the box: a volume that moves or turns
                              *   does not drag its billows along */
    vec3 wind;               /**< World units per second the noise moves, times the world time,
                              *   so a fixed --delta-time renders the same frames every run,
                              *   default {0, 0, 0} */
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
    char *path;           /**< Image file, relative to the working directory or an ancestor of
                           *   it; an unreadable file sets EnvironmentStatus.error. A change
                           *   that FileWatch (box/app.h) sees loads it again */
});

/** Register the box.light module.
 *
 * The first directional light of a scene and its spot and point lights with shadows (the tiles
 * of its ShadowAtlas) shadow the views that enable them. NoShadow and blended shapes cast none,
 * LightFixture shapes cast into all but the tile of a light inside their world bounds.
 *
 * In a scene with LightBake the views are shadowed by the first directional light that is not
 * Baked and the probe captures by the first one they shade surfaces with (see LightBakeMode).
 * The LightCookie of the first directional light of the views scales its direct light.
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
