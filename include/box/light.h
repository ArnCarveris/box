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

/** Point and spot lights of its cluster a cell of the froxel grid of the FogVolumes is lit by
 * at most; FOG_LIGHTS in etc/shaders/fog_volume.wgsl */
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
 * Range and intensity are in the local units of the light: under a uniform world scale s (the
 * cube root of the absolute determinant of its WorldTransform) it reaches range * s and lights
 * with intensity * s^2, so a lit model looks the same at any scale, only larger.
 *
 * A light with shadows gets a square tile of the ShadowAtlas of its scene while a view of the
 * scene that renders shadows (ShadowSettings.enabled) sees its range, as a spot light does.
 *
 * The tile holds the whole sphere around the light as an octahedron unfolded into the square, 8
 * perspective octants from 0.005 range (at least 0.001) to range; casters beyond it cast none.
 */
ECS_STRUCT(PointLight, {
    float intensity;    /**< Irradiance on a facing surface 1 local unit away, default 1, 0
                         *   none */
    float range;        /**< Local distance at which the light reaches zero, default 10 */
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
 * Range and intensity are in the local units of the light and follow its uniform world scale
 * as those of a PointLight do.
 *
 * A light with shadows gets a tile of the ShadowAtlas of its scene while a view of the scene
 * that renders shadows (ShadowSettings.enabled) sees its range: a reversed Z perspective along
 * +Z from 0.005 range (at least 0.001) to range.
 *
 * The tile covers the outer angle plus the reach of the shadow filter, at most 80 degrees; a
 * wider cone is unshadowed beyond that.
 */
ECS_STRUCT(SpotLight, {
    float intensity;    /**< Irradiance on a facing surface 1 local unit away, default 1, 0
                         *   none */
    float range;        /**< Local distance at which the light reaches zero, default 10 */
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
 * A bake is bounces + 1 passes. A change of static shapes or lights rebakes the probes it can
 * reach; dynamic shapes rebake none. A capture_size outside 1 to 256 or bounces outside 0 to 16
 * bakes nothing and sets LightBakeStatus.error.
 *
 * compute decides whether the engine schedules captures, View.lighting.baked whether a view
 * samples the probes. A volume that does not compute keeps its grid and records changes, and
 * catches up on them once compute is true again.
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
    bool compute;        /**< The engine schedules new captures for the volume, default true;
                          *   false holds the bake where it is without hiding it from views
                          *   with View.lighting.baked */
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
    bool held;           /**< Work is left but LightBake.compute is false, so nothing new is
                          *   captured until it is true again */
    char *error;         /**< Why nothing is baked, such as an invalid setting, NULL for none */
});

/** Keeps a static shape out of the bake captures */
extern ECS_TAG_DECLARE(NoBake);

/** Cubemap reflection probes of a scene, placed on a Scene root or as a singleton for the
 * default scene. Adding it adds ReflectionBakeStatus, removing it removes it.
 *
 * Values apply as specified: budget_ms 0 captures nothing, a resolution outside 8 to 512
 * captures nothing and sets ReflectionBakeStatus.error.
 *
 * With follow_focus the scene probe follows the first WorldFocus of the scene (box_world_focus)
 * for streamed worlds; without one it stays over the static bounds.
 */
ECS_STRUCT(ReflectionBake, {
    int32_t resolution;   /**< Face edge of a probe cubemap in texels, 8 to 512, default 128 */
    float budget_ms;      /**< GPU time per frame spent capturing and prefiltering in
                           *   milliseconds, default 1 */
    bool scene_probe;     /**< The engine adds a probe covering the whole scene, default true */
    float scene_probe_height; /**< Height of the scene probe above the bottom of the static
                               *   bounds of the scene, or above the focus with follow_focus,
                               *   default 2 */
    bool follow_focus;    /**< The scene probe captures above the focus of the scene, again once
                           *   the focus moved more than follow_distance, blending to the new
                           *   capture over follow_blend, default false */
    float follow_distance; /**< Distance the focus moves before the scene probe captures again,
                            *   default 16 */
    float follow_radius;  /**< Changes of static shapes or lights farther than this from a scene
                           *   probe that follows the focus do not recapture it, default 64 */
    float follow_blend;   /**< Seconds over which the scene probe blends from its old capture to
                           *   the one at its new place, default 0.5, 0 switches at once */
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

/** Reflection planes a view renders a mirrored image of at most: the PlanarReflections of its
 * scene nearest to its camera among those it sees from their front side */
#define BOX_PLANAR_REFLECTION_MAX (2)

/** An exact mirror image of the scene, set on the entity of the mirroring shape: a Box, a plane,
 * the water surface. The plane passes through the world position of the entity and faces its
 * local +Y in world space; scale is ignored.
 *
 * Each lit view with a perspective camera renders the scene mirrored about the plane into an
 * HDR image before its forward pass, aligned with its screen and lit like it, when the plane
 * crosses its frustum and has the camera on its front side.
 *
 * Only the BOX_PLANAR_REFLECTION_MAX nearest such planes of a view render; a plane is unbounded
 * for these tests. Probe, reflection and bake captures and orthographic views render none.
 *
 * Only a scene with Scene.planar_reflections renders the images; without it the shape draws as
 * if it did not have the component and no reflection pass runs.
 *
 * The 3D mesh of the entity, a Water surface too, shows the image as its indirect specular
 * light, in place of the reflection probes and the environment: sampled at the screen point of
 * a fragment moved by distortion times the tilt of its normal map, blurred by its roughness and
 * fading to the probes above a roughness of 0.6; Fresnel, specular occlusion and ClearCoat apply
 * as to the probes. Where the image shows nothing, and in a view that renders no image of the
 * plane (over the limit, behind it), the probes reflect as before. 2D shapes and text ignore it.
 *
 * @par Limits:
 * - It draws the opaque, alpha tested and blended meshes, the sky and the particles, not the
 *   shape of the entity itself, transmissive shapes or Water shapes (no reflections of
 *   reflections: water never reflects itself or other water), fog volumes, 2D shapes, text,
 *   debug drawing or the effects of the view.
 * - Shadows and point and spot lights come from the shadow maps and light clusters of the view,
 *   so what the view itself does not see may miss them; it has no ambient occlusion and no
 *   aerial perspective.
 * - It culls with the mirrored frustum but not by occlusion; particles face the mirrored camera
 *   and are not soft; blended meshes keep the back to front order of the view.
 *
 * @par A wall mirror facing +Z, its local +Y turned toward +Z by a pitch of 90 degrees:
 * @code
 * mirror {
 *   Position: {0, 1.5, -4}
 *   Rotation: {1.5708, 0, 0}
 *   Box: {2, 0.05, 3}
 *   PlanarReflection: {}
 * }
 * @endcode
 */
ECS_STRUCT(PlanarReflection, {
    float resolution;     /**< Size of the reflection image relative to the view, default 0.5,
                           *   clamped to 0.125..1 */
    float clip_offset;    /**< World units the clip plane is moved along the normal, default
                           *   0.02: positive keeps geometry slightly below the plane (shore
                           *   lines, objects that touch the surface), negative removes it */
    float distortion;     /**< Screen space offset of the reflection lookup by the normal mapped
                           *   normal, as a fraction of the image height, default 0.02; 0 is a
                           *   perfect mirror */
});

/** Shape of the fog of a FogVolume in its box */
ECS_ENUM(FogVolumeShape, {
    FogVolumeBox = 0,       /**< The box: edge_fade fades the density by the distance to its
                             *   nearest face */
    FogVolumeEllipsoid = 1, /**< The ellipsoid inscribed in the box, with semi-axes extent:
                             *   edge_fade fades the density by an approximate distance inside
                             *   its surface, so the outline is round */
    FogVolumeUnbounded = 2  /**< All space of its scene, out to the fog_distance of each view,
                             *   wherever the camera is: extent, edge_fade and edge_noise are
                             *   ignored and the volume is never culled */
});

/** A box of fog the lit views of its scene with LightSettings.fog_volumes render, placed by the
 * position and rotation of the entity. Probe and reflection captures and bakes do not see it.
 *
 * The fog fills the box or the ellipsoid inscribed in it (shape) and fades to 0 over edge_fade
 * inside its surface; height_falloff thins it out toward the top of the box, and edge_noise lets
 * the noise erode the faded border into billows instead of an outline of the shape.
 *
 * A FogVolumeUnbounded volume has no box: it fills all space of its scene, out to the
 * fog_distance of each lit view, wherever the camera is, and is in every lit view of its scene.
 * Its height_falloff is measured from the position of the entity along its local +Y, as the
 * base_height of a HeightFog, so the same density and falloff give fog like a HeightFog, with
 * shadows, shafts, noise and local lights. In the fog the sun's shadow fades out over the last
 * part of the shadow distance, so the shafts end without a ring.
 *
 * The unbounded volumes and the nearest bounded ones a view sees, 32 in all, render through a
 * froxel grid of the view: a grid aligned with its frustum, one cell per tile of about 8x8
 * pixels (larger tiles above 1920 pixels, so a grid has at most 240 cells along its larger
 * edge) by 64 depth slices from the near plane to LightSettings.fog_distance, thickening
 * exponentially with the distance (evenly for an orthographic camera). Each cell sums the density of every volume at its center and is lit
 * once, so the cost follows the grid, not the image size, the screen coverage or the number of
 * overlapping volumes. The grid is applied after the opaque meshes and the sky, one lookup per
 * pixel at its depth; fog beyond fog_distance is not drawn, and the sky takes the fog up to it.
 * Detail smaller than a cell, such as a hard face of a box or a thin shaft, is softened over a
 * cell. Particles and blended shapes in or behind it are fogged at their own depth by one
 * lookup in the grid, alpha blended ones taking on the fog in front of them and additive ones
 * dimmed by it; 2D shapes and text are not fogged. It is lit by the ambient of the view, the
 * first directional light through its shadow cascades and cookie, and the point and spot lights
 * of the clusters of the view.
 *
 * A shape with Transmission in or behind it is fogged as an opaque surface at its depth: the fog
 * behind the nearest glass of a pixel is applied before the snapshot the glass shows, the fog in
 * front of it after the last transmissive draw, from a depth only pass of the transmissive shapes.
 * Blended shapes and particles drawn before the last glass are not fogged at their own depth:
 * over glass they are fogged as the glass, elsewhere not at all; the fog between two layers of
 * glass is lost.
 *
 * A cell is lit by the first BOX_FOG_VOLUME_LIGHT_STEPS lights of its cluster, so dense clusters
 * may lose lights. Shadows are one depth comparison per cell.
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
    float noise_strength;    /**< 0 constant density, 1 the density goes from 0 to twice its
                              *   value with a tileable 3D noise, clamped to 0..1, default 0 */
    float noise_scale;       /**< World size of the largest billows, default 4. The noise is in
                              *   world space, not in the box: a volume that moves or turns
                              *   does not drag its billows along */
    vec3 wind;               /**< World units per second the noise moves, times the world time,
                              *   so a fixed --delta-time renders the same frames every run,
                              *   default {0, 0, 0} */
    FogVolumeShape shape;    /**< Shape of the fog in the box, default FogVolumeBox. The box
                              *   bounds every shape and is what views cull it by, except
                              *   FogVolumeUnbounded, which has no box and is never culled */
    float height_falloff;    /**< The density times exp(-height_falloff * h), h the height
                              *   above the bottom of the box along its local +Y, as
                              *   HeightFog.falloff, so ground mist thins out toward its top;
                              *   for FogVolumeUnbounded h is the height above the entity,
                              *   denser below it; at least 0, default 0 for uniform density */
    float edge_noise;        /**< How much the noise erodes the faded border, 0 to 1, default 0:
                              *   at 0 the fade of edge_fade scales the density, at 1 it is a
                              *   threshold on the noise, so the fog reaches further out where
                              *   the noise is high and the outline follows its billows. Applies
                              *   with noise_strength and edge_fade above 0 */
});

/** Stars a StarField draws at most; a larger count draws this many */
#define BOX_STAR_FIELD_MAX (1000000)

/** A procedural field of stars on the background, points of light at infinite distance that
 * every lit 3D view of its scene (a view with lighting and a perspective camera with a depth
 * range) draws behind all geometry. It is background only: environment captures, image based
 * lighting, reflection probes, reflection captures and bakes do not see it.
 *
 * Star i of count is generated on the GPU from a hash of i and the seed: a direction, uniform on
 * the sphere or, with probability band, along the galactic plane, a relative brightness
 * r = u^falloff for u uniform in 0..1, and a temperature uniform from temperature_min to
 * temperature_max. The galactic plane is the local XZ plane of the entity, so the field turns
 * with the rotation of its world transform; it does not move with the camera or the entity.
 *
 * A star is a disc of radius size * sqrt(r) pixels that emits brightness times its color,
 * anti-aliased over one pixel, so its energy is proportional to r; a star whose radius is below
 * half a pixel is a point that keeps that energy (dimmer, not smaller), spread over the at most
 * two by two pixels nearest its center, so it does not flicker or crawl as the camera turns.
 * The color is the blackbody color of the temperature (the blackbody() of the planet scene),
 * decoded to linear and mixed toward white by 1 - saturation.
 *
 * Stars draw after the opaque meshes where no geometry is, after the sky and the environment
 * background and before the planet atmosphere shells, fog volumes, particles and blended draws,
 * into the HDR image of a view with effects, so bloom and the tonemapper apply. They add to the
 * background and are not hazed by aerial perspective; a height fog effect fogs them as it fogs
 * the rest of the background.
 *
 * 0 always means zero: count 0 draws nothing, brightness 0 draws black stars, band 0 is uniform.
 *
 * @par Stars along a band tilted 60 degrees:
 * @code
 * stars {
 *   Rotation: {1.047, 0, 0}
 *   StarField: {count: 30000, seed: 7, band: 0.6}
 * }
 * @endcode
 */
ECS_STRUCT(StarField, {
    int32_t count;          /**< Number of stars, default 20000, clamped to 0..
                             *   BOX_STAR_FIELD_MAX */
    uint32_t seed;          /**< Seed of the hash, default 1 */
    float brightness;       /**< Emission of the brightest stars in the units of
                             *   Emissive.strength, default 6 */
    float falloff;          /**< Slope of the brightness distribution, r = u^falloff, default
                             *   4: larger makes more faint stars, 0 makes all stars as bright
                             *   as the brightest */
    float size;             /**< Radius of the brightest star in pixels, default 1.5 */
    float temperature_min;  /**< Coolest star in kelvin, default 3000 */
    float temperature_max;  /**< Hottest star in kelvin, default 12000 */
    float saturation;       /**< Saturation of the blackbody colors, 0 white to 1 full,
                             *   default 0.6 */
    float band;             /**< Share of the stars concentrated along the galactic plane, 0
                             *   uniform to 1 all of them, default 0.4 */
    float band_width;       /**< Standard deviation in degrees of the latitude of the stars of
                             *   the band, default 8 */
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
