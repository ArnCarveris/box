/**
 * @file render/render.h
 * @brief Render packets, the backend interface and the packet ring of the box.render sources.
 *
 * Main thread modules append to the packet of the frame during the RenderExtract phase; the
 * backend reads a submitted packet on the render thread, or on the main thread in a browser.
 */

#ifndef BOX_RENDER_INTERNAL_H
#define BOX_RENDER_INTERNAL_H

#include <box.h>

#include "render_packet.h"
#include "render_backend.h"

/** Counters of the instanced batches of a world, written by the draw extract */
typedef struct box_render_draws_stats_t {
    int32_t batches;            /**< Batches alive */
    int32_t instances;          /**< Static instances in batches */
    int32_t dynamic;            /**< Dynamic instances in batches */
    int32_t processed;          /**< Changed entities processed by the last extract */
    int32_t scattered;          /**< Instances the last extract sent through the scatter pass */
    int32_t scatters;           /**< Scatters the last extract sent, one per instance page with
                                 *   static changes */
    int32_t pages;              /**< Instance pages alive */
    int64_t page_slots;         /**< Slots the buffers of the instance pages alive hold, of every
                                 *   store: their capacity, not the live instances */
    int64_t page_transform_bytes; /**< Bytes of the transform buffers of the instance pages
                                 *   alive, of every store */
    int64_t page_shading_bytes; /**< Bytes of the shading buffers of the instance pages alive,
                                 *   of every store */
    int64_t page_surface_bytes; /**< Bytes of the surface buffers of the instance pages alive,
                                 *   which only the pages of the meshes with a parametric
                                 *   surface have */
    int64_t page_bytes;         /**< Bytes of every buffer of the instance pages alive, the sum
                                 *   of the bytes of the streams */
    int64_t upload_bytes;       /**< Bytes the last extract uploaded into instance buffers, the
                                 *   sum of the bytes of the streams */
    int32_t copies;             /**< GPU copies the last extract sent, one per stream of a batch
                                 *   whose run moved, which copy its instances from its old run */
    int64_t copy_bytes;         /**< Bytes those copies move on the GPU, not uploaded */
    int32_t kept_pages;         /**< Page buffers the last extract created again larger, which
                                 *   keep their instances through a GPU copy */
    int64_t kept_bytes;         /**< Bytes of instances those buffers keep, not uploaded */
    int64_t transform_bytes;    /**< Bytes the last extract uploaded into the transform streams:
                                 *   the runs of batches whose transforms upload (every frame for
                                 *   DynamicTransform) and the scatter segments of moved slots */
    int64_t shading_bytes;      /**< Bytes the last extract uploaded into the shading streams:
                                 *   the runs of batches whose shading uploads (every frame
                                 *   for DynamicMaterial) and scatter segments of materials */
    int64_t surface_bytes;      /**< Bytes the last extract uploaded into the surface streams
                                 *   of the mesh pages with parametric surfaces, runs and
                                 *   scatter segments, only when a ShapeMesh changed; plain mesh
                                 *   and 2D pages have no surface stream */
    int32_t clips;              /**< ClipChildren elements in the clip buffer */
    int64_t clip_bytes;         /**< Bytes the last extract uploaded into the clip buffer */
    int32_t materials;          /**< Slots of the material table alive, slot 0 included */
    int64_t material_bytes;     /**< Bytes of the material table the last extract uploaded */
} box_render_draws_stats_t;

/** Register the observers and the system that keep the shapes of the world in batches.
 *
 * Shapes are instanced in batches keyed by scene, mesh, pipeline and whether their transform
 * and their material are dynamic, which persist between frames.
 *
 * A mesh instance page holds a transform, a shading and a surface stream, a 2D page no surface
 * stream. The transforms of DynamicTransform shapes and the shading of DynamicMaterial shapes
 * upload every frame; the other streams upload the slots that changed through the scatter pass.
 *
 * @param world The world
 */
void box_render_draws_init(
    ecs_world_t *world);

/** Read the counters of the instanced batches of a world.
 *
 * @param world The world
 * @param stats Receives the counters
 */
void box_render_draws_stats(
    const ecs_world_t *world,
    box_render_draws_stats_t *stats);

/** Get the clip area of a ClipChildren element: its LayoutBounds, else its Rect, with the
 * corners rounded by its Border.radius.
 *
 * @param world The world
 * @param e The element
 * @param half Receives half its width and half its height, 0 when it has neither
 * @return The corner radius, clamped to half the smaller side
 */
float box_render_clip_area(
    const ecs_world_t *world,
    ecs_entity_t e,
    float half[2]);

/** Check whether a world position is inside the clip areas of the ClipChildren ancestors of an
 * entity, in the local XY plane of each, as the 2D shader clips it. A clip with an empty area
 * contains no position. The ancestors of a floating element (LayoutFloat) do not clip it.
 *
 * @param world The world
 * @param e The entity, whose own ClipChildren does not clip it
 * @param point The world position
 * @return Whether no ancestor clips the position away
 */
bool box_render_clip_contains(
    const ecs_world_t *world,
    ecs_entity_t e,
    const float point[3]);

/** Check whether the ClipChildren ancestors of an entity clip all of it away.
 *
 * Its bounds (its LayoutBounds, else its Rect, else its origin) have no part of positive size
 * inside the area of one of them. A clip with an empty area hides everything below it.
 *
 * A clip that scrolls (with a LayoutScroll) is skipped so Tab can scroll its content into view,
 * and the ancestors of a floating element (LayoutFloat) do not clip it.
 *
 * @param world The world
 * @param e The entity
 * @return Whether no part of it is drawn
 */
bool box_render_clip_hides(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Set the instances one batch holds before its shapes continue in another, for tests.
 *
 * A batch is one run of slots of an instance page, so the default of each store is its chunk,
 * the slots that keep every buffer of its pages within the 128 MiB storage binding every WebGPU
 * device supports.
 *
 * The size applies to every store, capped by its chunk; shapes added after a change follow it.
 *
 * @param world The world
 * @param instances Instances per batch, at least 1
 */
void box_render_draws_chunk(
    ecs_world_t *world,
    int32_t instances);

/** Set the slots of an instance page, for tests.
 *
 * The default of each store is its chunk, 128 MiB over its largest stride, the storage binding
 * every WebGPU device supports; the size applies to every store, capped by its chunk.
 *
 * A batch never spans pages, so a key also continues in another batch once a batch holds this
 * many shapes. Pages created after the change hold at most this many slots.
 *
 * @param world The world
 * @param slots Slots per page, at least 1
 */
void box_render_draws_page(
    ecs_world_t *world,
    int32_t slots);

/** Forget the texture entities surfaces render into, before the surfaces register them again.
 *
 * @param world The world
 */
void box_render_texture_targets_clear(
    ecs_world_t *world);

/** Register the texture entity a surface renders into, so materials that sample the entity
 * sample the image of the target of the surface.
 *
 * @param world The world
 * @param texture The texture entity
 * @param target Handle of the target
 */
void box_render_texture_target(
    ecs_world_t *world,
    ecs_entity_t texture,
    uint32_t target);

/** Get the texture handle of the image of the target that renders into a texture entity.
 *
 * @param world The world
 * @param texture The texture entity
 * @return BOX_RENDER_TEXTURE_TARGET with the target handle, 0 when no surface renders into it
 */
uint32_t box_render_texture_target_handle(
    const ecs_world_t *world,
    ecs_entity_t texture);

/** Register the system that sends loaded texture images to the backend.
 *
 * It also writes the anisotropy of the samplers into the packet, the largest one any surface
 * asks for, and loads every texture again when the backend changes.
 *
 * @param world The world
 */
void box_render_textures_init(
    ecs_world_t *world);

/** Counters of the light extract of a world */
typedef struct box_render_lights_stats_t {
    int32_t scenes;             /**< Scenes that have lights */
    int32_t lights;             /**< Lights that light something, of every scene */
    int64_t upload_bytes;       /**< Bytes the last extract uploaded into light buffers */
    int32_t shadow_tiles;       /**< Shadow atlas tiles of every scene */
    int32_t shadow_dropped;     /**< Shadow casting lights a view sees that got no tile */
} box_render_lights_stats_t;

/** Register the system that sends the lights of every scene and the lights of every view.
 *
 * Lights are collected per scene into a light buffer that is uploaded when it changed; every
 * view of the packet gets the buffer and the ambient light of the scene of its camera.
 *
 * @param world The world
 */
void box_render_lights_init(
    ecs_world_t *world);

/** Read the counters of the light extract of a world.
 *
 * @param world The world
 * @param stats Receives the counters
 */
void box_render_lights_stats(
    const ecs_world_t *world,
    box_render_lights_stats_t *stats);

/** Get the state of the light extract of a world, valid as long as the world.
 *
 * @param world The world
 * @return The state, NULL without the light extract
 */
const void* box_render_lights_state(
    const ecs_world_t *world);

/** Get the offset a LightCookie moves its pattern by at a time: its offset plus its velocity
 * times the time, computed in double precision and wrapped to [0, size) on each axis.
 *
 * @param cookie The cookie, whose size is above 0
 * @param time Seconds the world has advanced in total
 * @param out Receives the two components in world units
 */
void box_render_cookie_offset(
    const LightCookie *cookie,
    double time,
    float *out);

/** Resolve the LightCookie of the first directional light of a view for the backend.
 *
 * @param world The world
 * @param light The entity of the light
 * @param time Seconds the world has advanced in total, in double precision
 * @param out Receives the cookie, texture 0 when the light has none or it has no effect: no
 *            texture, a texture without an image yet, a size at or below 0 or a strength of 0
 */
void box_render_cookie_resolve(
    const ecs_world_t *world,
    ecs_entity_t light,
    double time,
    box_render_view_cookie_t *out);

/** Make the light extract attenuate the sun and moon of the scenes with an Atmosphere and
 * write their AtmosphereStatus, called by the import of box.atmosphere.
 *
 * @param world The world
 */
void box_render_lights_atmosphere(
    ecs_world_t *world);

/** Get the sun of the valid atmosphere of a scene as the light extract of this frame found it.
 *
 * @param state The state of the light extract, see box_render_lights_state
 * @param scene The scene, 0 for the default scene
 * @param direction Receives the normalized direction toward the sun
 * @param color Receives its linear color times intensity, before the transmittance
 * @return Whether the scene has a valid atmosphere and a sun
 */
bool box_render_lights_atmos_sun(
    const void *state,
    ecs_entity_t scene,
    float *direction,
    float *color);

/** Get the moon of the valid atmosphere of a scene as the light extract of this frame found it.
 *
 * @param state The state of the light extract, see box_render_lights_state
 * @param scene The scene, 0 for the default scene
 * @param direction Receives the normalized direction toward the moon
 * @param color Receives its linear color times intensity, before the transmittance
 * @param illumination Receives its lit fraction, 0 without a sun
 * @return Whether the scene has a valid atmosphere and a moon
 */
bool box_render_lights_atmos_moon(
    const void *state,
    ecs_entity_t scene,
    float *direction,
    float *color,
    float *illumination);

/** Get the main light of the valid atmosphere of a scene as the light extract of this frame
 * attenuated it: the sun or the moon, whichever is brighter after the transmittance and the
 * horizon fade, the sun by day and the moon by night.
 *
 * @param state The state of the light extract, see box_render_lights_state
 * @param scene The scene, 0 for the default scene
 * @param direction Receives the normalized direction toward the light
 * @param color Receives its linear color times intensity, times the transmittance and
 * the horizon fade, and for the moon its illumination
 * @param fade Receives the fade of its disk at the horizon
 * @param moon Receives whether the light is the moon
 * @return Whether the scene has a valid atmosphere with a sun or a moon
 */
bool box_render_lights_atmos_main(
    const void *state,
    ecs_entity_t scene,
    float *direction,
    float *color,
    float *fade,
    bool *moon);

/** Register the system that keeps the transmittance and multi scattering LUTs of every scene
 * with a valid Atmosphere, a BOX_RENDER_ATMOSPHERE per scene.
 *
 * Runs before the light extract, which writes the lut_version of each build into the
 * AtmosphereStatus. The LUTs are built again only when the medium changes: radii, scale
 * heights, coefficients, tint and ground albedo, not the sun, the colors or the cameras.
 *
 * @param world The world
 */
void box_render_atmosphere_init(
    ecs_world_t *world);

/** Register the system that gives every view of a scene with atmosphere LUTs its sky view and
 * aerial perspective inputs, after the light extract found the sun of the scene.
 *
 * @param world The world
 */
void box_render_atmosphere_views_init(
    ecs_world_t *world);

/** Register the system that makes the entity with the Atmosphere of every scene with LUTs an
 * environment source, which renders its sky into the cubemap of its valid Environment as a
 * BOX_RENDER_ATMOSPHERE_ENVIRONMENT.
 *
 * It renders when the cubemap is empty, the medium changed, the sun or moon moved more than
 * Atmosphere.ibl_update_angle or a channel of their color changed by more than
 * BOX_RENDER_ATMOS_ENV_COLOR, and never otherwise. Runs after the light extract.
 *
 * @param world The world
 */
void box_render_atmosphere_environment_init(
    ecs_world_t *world);

/** Relative change of the color of the sun or moon, against its brightest channel, above
 * which the environment of an atmosphere is rendered again */
#define BOX_RENDER_ATMOS_ENV_COLOR (0.02f)

/** Make the atmosphere extract follow the Atmosphere components, called by the import of
 * box.atmosphere.
 *
 * @param world The world
 */
void box_render_atmosphere_import(
    ecs_world_t *world);

/** Get the lut_version an atmosphere has after the extract of this frame.
 *
 * @param world The world
 * @param scene The scene of the atmosphere
 * @param entity The entity with the Atmosphere
 * @param current Its lut_version so far
 * @return The version of the last build of the LUTs of the scene when they were built from the
 *         entity, else current
 */
int32_t box_render_atmosphere_version(
    const ecs_world_t *world,
    ecs_entity_t scene,
    ecs_entity_t entity,
    int32_t current);

/** Get the handle of the atmosphere LUTs of a scene.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @return Handle of its BOX_RENDER_ATMOSPHERE, 0 when the scene has no valid Atmosphere
 */
uint32_t box_render_atmosphere_handle(
    const ecs_world_t *world,
    ecs_entity_t scene);

/** Check whether a packet runs the atmosphere shader.
 *
 * @param p The packet
 * @return Whether it builds atmosphere LUTs or a view has an atmosphere
 */
bool box_render_atmosphere_use(
    const box_render_packet_t *p);

/** Get where the transmittance LUT holds a radius and a direction, as atmosphere.wgsl maps them.
 *
 * @param bottom Radius of the ground in km
 * @param top Radius of the top of the atmosphere in km
 * @param r Radius of the start of the ray in km
 * @param mu Cosine of the angle between the ray and the up direction
 * @param uv Receives the texture coordinates, 0 to 1, v 0 at the first row
 */
void box_render_atmos_transmittance_uv(
    float bottom,
    float top,
    float r,
    float mu,
    float *uv);

/** Copy the transmittance or the multi scattering LUT of an atmosphere into memory, waiting for
 * the copy.
 *
 * Only call this when the main thread renders, after box_render_sync, as
 * box_render_gpu_read_buffer.
 *
 * @param world The world
 * @param handle Handle of the BOX_RENDER_ATMOSPHERE
 * @param lut BOX_RENDER_ATMOS_TRANSMITTANCE or BOX_RENDER_ATMOS_MULTI
 * @param out Receives the linear RGBA texels row by row, four floats each
 * @param count Floats out holds
 * @return Whether the LUTs exist, were built and the texels were copied
 */
bool box_render_gpu_read_atmosphere(
    ecs_world_t *world,
    uint32_t handle,
    box_render_atmos_pass_t lut,
    float *out,
    int32_t count);

/** Make the light extract attenuate the sun and moon of the scenes with a TimeOfDay and no
 * Atmosphere by the fallback of the time of day, called by the import of box.atmosphere.
 *
 * @param world The world
 */
void box_render_lights_time_of_day(
    ecs_world_t *world);

/** Register the cached query that knows the scene of every entity in a scene.
 *
 * @param world The world
 */
void box_render_scenes_init(
    ecs_world_t *world);

/** Get the scene an entity belongs to from the cached scene membership query.
 *
 * @param world The world
 * @param e The entity
 * @return The entity when it has Scene, else its nearest ancestor with Scene, 0 for the
 * default scene
 */
ecs_entity_t box_render_scene_of(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Get the scene of an entity of a query result with an optional Scene(self|up) term.
 *
 * @param it The iterator
 * @param field Index of the Scene field
 * @param row Row of the entity in the result
 * @return The entity when it has Scene, else the source of the field, 0 for the default scene
 */
ecs_entity_t box_render_scene_field(
    const ecs_iter_t *it,
    int8_t field,
    int32_t row);

/** Register the system that resolves the shadow settings of every view of the packet.
 *
 * Runs after the light extract, because a view only renders cascades when its scene has a
 * directional light and samples the spot light tiles the light extract allocated in the shadow
 * atlas of its scene.
 *
 * @param world The world
 */
void box_render_shadows_init(
    ecs_world_t *world);

/** Resolve shadow settings: every field is clamped to its valid range and an even
 * filter_size rounds up to the next odd one, other values apply as set.
 *
 * @param settings The settings of a view
 * @param out Receives the resolved settings
 */
void box_render_shadow_settings(
    const ShadowSettings *settings,
    ShadowSettings *out);

/** Split the view depths between near and distance into cascades.
 *
 * A split mixes the uniform split near + (distance - near) * i / n with the logarithmic split
 * near * (distance / near) ^ (i / n) by lambda: 0 is uniform, 1 logarithmic.
 *
 * @param near_ View depth the first cascade starts at
 * @param distance View depth the last cascade ends at
 * @param lambda Weight of the logarithmic split
 * @param cascades Number of cascades
 * @param out Receives the view depth each cascade ends at, the last one is distance
 */
void box_render_shadow_splits(
    float near_,
    float distance,
    float lambda,
    int32_t cascades,
    float *out);

/** A cascade fitted to a slice of the view frustum */
typedef struct box_render_cascade_t {
    mat4 matrix;            /**< World to shadow clip space, reversed Z: 1 nearest the light */
    float texel;            /**< World size of a texel of the cascade */
    float depth_range;      /**< World distance the depth of the cascade spans */
} box_render_cascade_t;

/** Fit a cascade around the bounding sphere of a slice of the view frustum.
 *
 * The sphere only depends on the projection and the slice depths, so its size is the same in
 * every frame; its center is snapped to whole texels in light space, so a moving camera does
 * not make the shadow edges shimmer.
 *
 * The depth range reaches back toward the light by distance, which keeps the casters between
 * the light and the slice.
 *
 * @param camera World transform of the camera
 * @param proj Projection of the view
 * @param near_ View depth the slice starts at
 * @param far_ View depth the slice ends at
 * @param direction Direction toward the light
 * @param resolution Edge of the cascade in texels
 * @param distance Distance the casters may be in front of the slice toward the light
 * @param out Receives the cascade
 */
void box_render_shadow_cascade(
    mat4 camera,
    mat4 proj,
    float near_,
    float far_,
    const float *direction,
    int32_t resolution,
    float distance,
    box_render_cascade_t *out);

/** Resolve the settings of a shadow atlas: resolution rounds down to a power of two within
 * BOX_RENDER_SHADOW_ATLAS_MIN and BOX_RENDER_SHADOW_ATLAS_MAX, min_tile to a power of two
 * within 1 and the resolution.
 *
 * @param in The settings of a scene, NULL for the defaults
 * @param out Receives the resolved settings
 */
void box_render_shadow_atlas_settings(
    const ShadowAtlas *in,
    ShadowAtlas *out);

/** Get the tile edge a light asks for: its shadow_resolution rounded down to a power of two
 * within the min_tile and the resolution of an atlas.
 *
 * @param resolution SpotLight.shadow_resolution
 * @param atlas Resolved settings of the atlas
 * @return The edge in texels
 */
int32_t box_render_shadow_request_size(
    int32_t resolution,
    const ShadowAtlas *atlas);

/** A light that asks for a tile of a shadow atlas */
typedef struct box_render_shadow_request_t {
    ecs_entity_t light;     /**< Entity of the light, which breaks ties */
    int32_t size;           /**< Tile edge it asks for, a power of two */
    float distance;         /**< Distance from the nearest camera to its range sphere, 0 inside */
} box_render_shadow_request_t;

/** Where a light got its tile */
typedef struct box_render_shadow_place_t {
    int32_t size;           /**< Edge of the tile in texels, 0 for no tile */
    int32_t x;              /**< Left column of the tile in the atlas */
    int32_t y;              /**< Top row of the tile in the atlas */
} box_render_shadow_place_t;

/** Allocate the tiles of the lights of a shadow atlas.
 *
 * Lights are taken nearest first, then larger request, then lower entity; at most
 * BOX_RENDER_SHADOW_TILES of them. Each gets the largest power of two up to its request that
 * fits in the area still free, at least min_tile, else no tile.
 *
 * Tiles are then placed by size (largest first), then entity, in quadtree (Morton) order, which
 * fits whenever their area does and depends only on the set of lights and their sizes.
 *
 * @param requests The lights, in any order
 * @param count Number of lights
 * @param atlas Resolved settings of the atlas
 * @param out Receives where each light got its tile, in the order of requests
 * @param dropped Receives the lights that got no tile, NULL for none
 * @return Tiles allocated
 */
int32_t box_render_shadow_allocate(
    const box_render_shadow_request_t *requests,
    int32_t count,
    const ShadowAtlas *atlas,
    box_render_shadow_place_t *out,
    int32_t *dropped);

/** Write the tile record of a spot light: the reversed Z perspective along its axis from
 * near max(0.005 range, 0.001) to far max(range, 2 near), over its outer angle widened by the
 * reach of the widest shadow filter, at most BOX_RENDER_SHADOW_SPOT_ANGLE_MAX.
 *
 * @param position World position of the light
 * @param direction Normalized direction it shines in
 * @param range Range of the light
 * @param outer Outer half angle in degrees
 * @param place The tile, with a size
 * @param resolution Edge of the atlas in texels
 * @param out Receives the record
 */
void box_render_shadow_spot(
    const float *position,
    const float *direction,
    float range,
    float outer,
    const box_render_shadow_place_t *place,
    int32_t resolution,
    box_render_shadow_record_t *out);

/** Get the near plane of the shadow tile of a spot or a point light.
 *
 * @param range Range of the light
 * @return max(BOX_RENDER_SHADOW_NEAR_FRACTION range, BOX_RENDER_SHADOW_NEAR_MIN)
 */
float box_render_shadow_near(
    float range);

/** Get the far plane of the shadow tile of a spot or a point light.
 *
 * @param range Range of the light
 * @return max(range, 2 near)
 */
float box_render_shadow_far(
    float range);

/** Write the tile record of a point light: the octahedral projection of the sphere around it,
 * from near to far of its range (box_render_shadow_near, box_render_shadow_far). The world size
 * of a texel per unit of distance along an octant axis is 2 sqrt(6) over the edge of the tile.
 *
 * @param position World position of the light
 * @param range Range of the light
 * @param place The tile, with a size
 * @param resolution Edge of the atlas in texels
 * @param out Receives the record
 */
void box_render_shadow_point(
    const float *position,
    float range,
    const box_render_shadow_place_t *place,
    int32_t resolution,
    box_render_shadow_record_t *out);

/** Write the 6 world planes of the box around the sphere a point light tile culls its casters
 * by, inside where non negative, normalized.
 *
 * @param tile The tile, octahedral
 * @param out Receives the planes
 */
void box_render_shadow_point_planes(
    const box_render_shadow_tile_t *tile,
    vec4 *out);

/** Get the world bounds of what a shadow atlas tile holds: the box around the sphere of the far
 * plane of a point light tile, or for a spot light tile the box around its frustum (the cone
 * through its corners) within that sphere.
 *
 * @param tile The tile
 * @param min Receives the minimum corner
 * @param max Receives the maximum corner
 */
void box_render_shadow_tile_bounds(
    const box_render_shadow_tile_t *tile,
    float *min,
    float *max);

/** Check whether a box may reach into what a shadow atlas tile holds: the sphere of the far
 * plane of a point light tile, the bounds of the frustum of a spot light tile.
 *
 * @param tile The tile
 * @param min Minimum corner of the box
 * @param max Maximum corner of the box
 * @return Whether a caster in the box may change the tile
 */
bool box_render_shadow_tile_touches(
    const box_render_shadow_tile_t *tile,
    const float *min,
    const float *max);

/** A region a shadow caster covered or covers after a change: it moved, was added or removed,
 * changed mesh, scale or surface, started or stopped casting, or, alpha tested, changed what its
 * alpha test reads: its alpha, the cutoff or UV transform of its material or its albedo image */
typedef struct box_render_shadow_region_t {
    ecs_entity_t scene;     /**< Scene root of the caster, 0 for the default scene */
    float min[3];           /**< Minimum corner of its world bounds */
    float max[3];           /**< Maximum corner of its world bounds */
} box_render_shadow_region_t;

/** Render the tiles of an atlas that a shadow caster changed within: those a region of their
 * scene reaches into (box_render_shadow_tile_touches).
 *
 * @param tiles The tiles, whose render flag is set for the touched ones
 * @param count Tiles
 * @param scene Scene of the atlas
 * @param regions The regions of every scene
 * @param region_count Regions
 */
void box_render_shadow_touched(
    box_render_shadow_tile_t *tiles,
    int32_t count,
    ecs_entity_t scene,
    const box_render_shadow_region_t *regions,
    int32_t region_count);

/** Get the regions the shadow casters of a world covered before and after their changes since
 * the light extract last took them, recorded on the main thread by the draw extract.
 *
 * The caller clears the vector once it tested the regions against the tiles.
 *
 * @param world The world
 * @return box_render_shadow_region_t, NULL without the draw extract
 */
ecs_vec_t* box_render_draws_shadow_regions(
    const ecs_world_t *world);

/** Set whether the draw extract of a world records the regions of changed shadow casters: while
 * a scene has a shadow atlas tile to keep. A tile that appears renders anyway.
 *
 * @param world The world
 * @param record Whether to record them
 */
void box_render_draws_shadow_record(
    const ecs_world_t *world,
    bool record);

/** Make the light extract of a world render every shadow atlas tile every frame instead of only
 * the tiles that changed, for tests that compare both. Applies from the next extract.
 *
 * @param world The world
 * @param enabled Whether tiles that did not change are kept
 * @return Whether the world has the light extract
 */
bool box_render_shadow_cache(
    ecs_world_t *world,
    bool enabled);

/** Get the shadow atlas of a scene as the light extract of this frame left it.
 *
 * @param state The state of the light extract, see box_render_lights_state
 * @param scene The scene, 0 for the default scene
 * @param handle Receives the handle of the atlas and its tile buffer, 0 for none
 * @return Tile records of the atlas, 0 when the scene has none
 */
uint32_t box_render_lights_atlas(
    const void *state,
    ecs_entity_t scene,
    uint32_t *handle);

/** Register the system that keeps the irradiance probe volume of every scene with LightBake.
 *
 * Runs after the light extract, because the probes start out as the ambient light of the views
 * of their scene. Views with lighting.baked sample the volume of their scene.
 *
 * A volume is a grid over the static bounds of its scene plus bounds_padding, probe_spacing
 * apart; it is sent again when the grid changes and refilled when the ambient changes.
 *
 * @param world The world
 */
void box_render_bake_init(
    ecs_world_t *world);

/** A probe of the volume of a scene */
typedef struct box_render_probe_t {
    vec3 position;          /**< World position of the probe */
    bool valid;             /**< The probe is outside static geometry, so views sample it */
} box_render_probe_t;

/** Find the probe of the volume of a scene nearest to a position.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param position The position
 * @param out Receives the probe
 * @return false when the scene has no probes
 */
bool box_render_bake_probe(
    const ecs_world_t *world,
    ecs_entity_t scene,
    const vec3 position,
    box_render_probe_t *out);

/** Get the probes left to bake in the volumes whose bake can progress and has not settled.
 *
 * A volume can progress when its settings are valid, its budget_ms is positive, a view of its
 * scene was in the packet of the last frame and the GPU backend renders; a volume that cannot
 * progress counts nothing.
 *
 * A volume settles when it first converges after it was filled, or when a bake completes
 * after a light, ambient or environment change queued every probe, as a time of day does.
 *
 * @param world The world
 * @return Probe captures of every pass not reported yet, those of the probes a change queued
 *         included, 0 when every bake that can progress settled
 */
int32_t box_render_bake_pending(
    const ecs_world_t *world);

/** Texture array layers the reflection probe slots assume while no device reports its limit,
 * the WebGPU default of maxTextureArrayLayers */
#define BOX_RENDER_REFLECT_LAYERS (256)

/** A resident reflection probe as the reflection probe buffer of its scene holds it.
 *
 * The buffer lists the resident probes of a scene in blend order: higher priority first, then
 * the smaller influence box, the scene probe last. The boxes are centered on center and
 * oriented by axes.
 */
typedef struct box_render_reflection_probe_t {
    float center[3];        /**< World center of the influence and projection boxes */
    float blend;            /**< Distance inside the influence box over which it fades out */
    float axes[3][4];       /**< World directions of the local x, y and z axes, unit length, w
                             *   unused */
    float extent[3];        /**< Half size of the influence box */
    int32_t priority;       /**< ReflectionProbe.priority, INT32_MIN for the scene probe */
    float projection[3];    /**< Half size of the projection box, zero for no box projection */
    uint32_t slot;          /**< Cubemap slot of the probe */
    float capture[3];       /**< World capture point */
    uint32_t scene;         /**< 1 for the scene probe, 0 for a ReflectionProbe */
    uint32_t ready;         /**< 1 when views can sample the slot: its capture was reported or
                             *   runs before the targets of this packet */
    uint32_t pad[3];        /**< Unused */
} box_render_reflection_probe_t;

/** Register the system that keeps the reflection probes of every scene with ReflectionBake.
 *
 * Runs after the bake extract, whose change records queue the probes a change reaches for a new
 * capture. Probes keep their cubemap slot while they stay resident; the resident probes of a
 * scene are uploaded to its reflection probe buffer when they change.
 *
 * @param world The world
 */
void box_render_reflect_init(
    ecs_world_t *world);

/** Get the resident reflection probes of a scene.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param count Receives the number of probes
 * @return The probes in blend order, valid until the next extract, NULL when the scene has
 *         no ReflectionBake
 */
const box_render_reflection_probe_t* box_render_reflect_probes(
    const ecs_world_t *world,
    ecs_entity_t scene,
    int32_t *count);

/** Get the reflection probe cubemap array of a scene.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @return Handle of its BOX_RENDER_REFLECTION, 0 when the scene has none
 */
uint32_t box_render_reflect_texture(
    const ecs_world_t *world,
    ecs_entity_t scene);

/** Get the reflection probes left to capture in the scenes whose captures can progress and
 * have not settled.
 *
 * Captures can progress when the resolution is valid, budget_ms is positive, a view of the
 * scene was in the packet of the last frame and the GPU backend renders.
 *
 * Once the light bake of the scene settled, a scene settles when it first converges, or when
 * every resident probe has a capture after a light, ambient, environment or bake change
 * queued every probe.
 *
 * @param world The world
 * @return Resident probes without a capture of what they show now, 0 when every scene that
 *         can progress settled
 */
int32_t box_render_reflect_pending(
    const ecs_world_t *world);

/** Get the resident reflection probes of a scene that wait for a capture, in the order they are
 * captured: the scene probe first, then nearer to the cameras of the scene.
 *
 * A probe waits until its first capture is in a packet, and again from a change of what it
 * shows until the capture after that change is in a packet; until then it shows its previous
 * capture.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 * @param out Receives the entity of each probe, 0 for the scene probe, NULL for none
 * @param max Entities out holds
 * @return Probes that wait, which may be more than max
 */
int32_t box_render_reflect_queue(
    const ecs_world_t *world,
    ecs_entity_t scene,
    ecs_entity_t *out,
    int32_t max);

/** Mark the resident reflection probes of a scene captured with nothing queued, as if the
 * backend reported their captures, for tests that run without the GPU backend. The
 * ReflectionBakeStatus follows in the next frame.
 *
 * @param world The world
 * @param scene The scene, 0 for the default scene
 */
void box_render_reflect_settle(
    ecs_world_t *world,
    ecs_entity_t scene);

/** Register the system that adds the effect passes of every view to the packet.
 *
 * Runs after the light extract, because fog is lit by the lights of the scene of the view.
 *
 * @param world The world
 */
void box_render_effects_init(
    ecs_world_t *world);

/** Resolve settings of effects: clamp every field to its valid range, values apply as set.
 *
 * @param in The settings of an effect entity
 * @param out Receives the resolved settings
 */
void box_render_bloom_settings(
    const Bloom *in,
    Bloom *out);

/** Resolve the settings of the ambient occlusion of a view.
 *
 * @param in The settings of an effect entity
 * @param out Receives the resolved settings
 */
void box_render_gtao_settings(
    const Gtao *in,
    Gtao *out);

/** Resolve the settings of a fog pass.
 *
 * @param in The settings of an effect entity
 * @param out Receives the resolved settings
 */
void box_render_fog_settings(
    const HeightFog *in,
    HeightFog *out);

/** Resolve the settings of an adapted exposure.
 *
 * @param in The settings of an effect entity
 * @param out Receives the resolved settings
 */
void box_render_exposure_settings(
    const AutoExposure *in,
    AutoExposure *out);

/** Resolve the settings of an anti aliasing pass.
 *
 * @param in The settings of an effect entity
 * @param out Receives the resolved settings
 */
void box_render_smaa_settings(
    const Smaa *in,
    Smaa *out);

/** Texels along each edge of the Tony McMapface LUT */
#define BOX_RENDER_TONY_SIZE (48)

/** Texels of the Tony McMapface LUT */
#define BOX_RENDER_TONY_TEXELS (BOX_RENDER_TONY_SIZE * BOX_RENDER_TONY_SIZE * BOX_RENDER_TONY_SIZE)

/** File of the Tony McMapface LUT, found like a shader file */
#define BOX_RENDER_TONY_PATH "etc/luts/tony_mc_mapface.dds"

/** Load the Tony McMapface LUT from its DDS file: 48^3 RGB9E5 texels, x fastest, then y, then z.
 *
 * @param path The file, relative paths are found like shader files
 * @param error Receives why the file cannot be used, NULL when it loads
 * @return The texels, freed with ecs_os_free, NULL when the file cannot be used
 */
uint32_t* box_render_tony_load(
    const char *path,
    char **error);

/** Decode a texel of the shared exponent format RGB9E5.
 *
 * @param texel The texel: 9 bit mantissas of r, g and b from the low bits, 5 bit exponent on top
 * @param out Receives the linear color
 */
void box_render_rgb9e5_decode(
    uint32_t texel,
    float *out);

/** Map a linear color with the Khronos PBR Neutral curve, as etc/shaders/tonemap.wgsl does.
 *
 * @param in The color, after the exposure
 * @param out Receives the mapped color
 */
void box_render_tonemap_pbr_neutral(
    const float *in,
    float *out);

/** Map a linear color with Tony McMapface, as etc/shaders/tonemap.wgsl does without its dither:
 * the LUT sampled with trilinear filtering at c / (c + 1), with half texel insets.
 *
 * @param lut The texels of the LUT, as box_render_tony_load returns them
 * @param in The color, after the exposure
 * @param out Receives the mapped color
 */
void box_render_tonemap_tony(
    const uint32_t *lut,
    const float *in,
    float *out);

/** Check whether the effect passes of a packet run an engine shader.
 *
 * @param p The packet
 * @param handle Handle of the shader
 * @return Whether a pass of a view uses the shader, or a view applies Gtao
 */
bool box_render_effects_use(
    const box_render_packet_t *p,
    uint32_t handle);

/** A one-off render of a camera into an offscreen image that is written as a PNG file.
 *
 * The camera is an entity read when the frame is extracted, or, when camera is 0, the
 * settings, transform and scene in the description.
 *
 * A camera that a view renders is captured as its first view renders it, effects included
 * (exposure and temporal bloom start fresh). Other cameras render lit, without shadows or
 * effects, with the default culling.
 */
typedef struct box_render_capture_desc_t {
    ecs_entity_t camera;        /**< Entity with Camera, 0 to use the fields below */
    Camera camera_data;         /**< Settings of the camera when camera is 0 */
    mat4 transform;             /**< World transform of the camera when camera is 0 */
    ecs_entity_t scene;         /**< Scene root drawn when camera is 0, 0 for the default scene */
    ClearSettings clear;        /**< Clear of the image, opaque black when not enabled */
    int32_t width;              /**< Width of the image in pixels */
    int32_t height;             /**< Height of the image in pixels */
    const char *path;           /**< PNG file, copied */
} box_render_capture_desc_t;

/** Register the capture queue and the system that adds the captures to the packet.
 *
 * @param world The world
 */
void box_render_captures_init(
    ecs_world_t *world);

/** Ask for a capture in the next extracted frame.
 *
 * The image is written on the thread that renders the packet of that frame, through a
 * temporary offscreen target; the world switches to the GPU backend unless it was given one.
 *
 * @param world The world
 * @param desc The capture
 * @return 0 when the capture was queued, -1 when the description is invalid
 */
int box_render_capture(
    ecs_world_t *world,
    const box_render_capture_desc_t *desc);

/** Get the number of captures that were requested and not yet added to a packet.
 *
 * @param world The world
 * @return The number of queued captures
 */
int32_t box_render_captures_pending(
    const ecs_world_t *world);

/** Initialize the vectors of a packet.
 *
 * @param p The packet
 */
void box_render_packet_init(
    box_render_packet_t *p);

/** Free the storage of a packet.
 *
 * @param p The packet
 */
void box_render_packet_fini(
    box_render_packet_t *p);

/** Empty a packet and keep its storage, except a data block above BOX_RENDER_DATA_KEEP that
 * the frames used little of for BOX_RENDER_DATA_TRIM_RESETS resets in a row, which shrinks to
 * the largest of those frames.
 *
 * @param p The packet
 */
void box_render_packet_reset(
    box_render_packet_t *p);

/** Copy the contents of a packet into another one, reusing its storage.
 *
 * @param dst The destination packet
 * @param src The source packet
 */
void box_render_packet_copy(
    box_render_packet_t *dst,
    const box_render_packet_t *src);

/** Append a transition record to a packet.
 *
 * @param p The packet
 * @param op What the record does
 * @param node The node id
 * @return The record, zeroed but for op and node
 */
box_render_transition_record_t* box_render_packet_transition(
    box_render_packet_t *p,
    box_render_transition_op_t op,
    uint32_t node);

/** Get the packet of the current frame, acquiring it on first use in the frame.
 *
 * Only call this from the main thread during the RenderExtract phase. It waits when every
 * packet is in flight.
 *
 * @param world The world
 * @return The packet
 */
box_render_packet_t* box_render_packet(
    ecs_world_t *world);

/** Append a view to a packet.
 *
 * @param p The packet
 * @return The zeroed view
 */
box_render_view_t* box_render_packet_view(
    box_render_packet_t *p);

/** Append a batch to a packet.
 *
 * @param p The packet
 * @return The zeroed batch
 */
box_render_batch_t* box_render_packet_batch(
    box_render_packet_t *p);

/** Append a target to a packet.
 *
 * @param p The packet
 * @param handle Handle of the target
 * @param capture PNG file the frame of the target is written to, NULL for none
 * @return The target, with a zero clear color
 */
box_render_target_t* box_render_packet_target(
    box_render_packet_t *p,
    uint32_t handle,
    const char *capture);

/** Append an upload to a packet.
 *
 * @param p The packet
 * @param buffer Handle of the destination buffer
 * @param offset Byte offset in the destination buffer
 * @param size Bytes to write
 * @return Where the caller writes the bytes, valid until the next append to the packet
 */
void* box_render_packet_upload(
    box_render_packet_t *p,
    uint32_t buffer,
    uint32_t offset,
    uint32_t size);

/** Append a scatter to a packet.
 *
 * @param p The packet
 * @return The zeroed scatter
 */
box_render_scatter_t* box_render_packet_scatter(
    box_render_packet_t *p);

/** Append a GPU copy between buffers to a packet.
 *
 * @param p The packet
 * @return The zeroed copy
 */
box_render_copy_t* box_render_packet_buffer_copy(
    box_render_packet_t *p);

/** Append a resource change to a packet.
 *
 * @param p The packet
 * @param kind Kind of object
 * @param op What happens to it
 * @param handle Handle of the object
 * @param size Bytes of the description, 0 for none
 * @return Where the caller writes the description, valid until the next append, or NULL
 */
void* box_render_packet_resource(
    box_render_packet_t *p,
    box_render_resource_kind_t kind,
    box_render_resource_op_t op,
    uint32_t handle,
    uint32_t size);

/** Reserve an aligned block of bytes in the data of a packet.
 *
 * The block moves when more data is reserved; keep the offset, not the pointer.
 *
 * @param p The packet
 * @param size Bytes to reserve
 * @param offset_out Receives the data offset of the block
 * @return The block, NULL when size is 0
 */
void* box_render_packet_reserve(
    box_render_packet_t *p,
    uint32_t size,
    uint32_t *offset_out);

/** Get the bytes an upload or a resource change points to.
 *
 * @param p The packet
 * @param offset The data offset of the upload or the resource change
 * @return The bytes
 */
const void* box_render_packet_data(
    const box_render_packet_t *p,
    uint32_t offset);

/** Replace the backend, after the packets in flight rendered with the current one.
 *
 * The current backend runs its fini and is freed; the new one runs init before its first
 * packet. Renderer modules call this when they are imported.
 *
 * @param world The world
 * @param backend The backend, copied
 */
void box_render_set_backend(
    ecs_world_t *world,
    const box_render_backend_t *backend);

/** Hold or release the packets on their way to the backend.
 *
 * A held ring still accepts packets until all of them are in flight. Shutting down renders
 * the packets in flight even while they are held.
 *
 * @param world The world
 * @param paused Whether the backend stops receiving packets
 */
void box_render_pause(
    ecs_world_t *world,
    bool paused);

/** Wait until the backend rendered every submitted packet, also while the ring is held, and
 * flushed it, so every capture of those packets is written, and bring RenderStatus up to date.
 *
 * @param world The world
 */
void box_render_sync(
    ecs_world_t *world);

/** Create a packet ring with the null backend.
 *
 * @return The ring, freed by box_render_ring_free
 */
box_render_ring_t* box_render_ring_new(void);

/** Stop the consumer after it rendered every packet in flight and free the ring.
 *
 * @param ring The ring
 */
void box_render_ring_free(
    box_render_ring_t *ring);

/** Change the settings of a ring, applied before the next packet is acquired.
 *
 * @param ring The ring
 * @param settings The settings
 */
void box_render_ring_configure(
    box_render_ring_t *ring,
    const RenderSettings *settings);

/** Take the next free packet, waiting while every packet is in flight.
 *
 * @param ring The ring
 * @return The empty packet
 */
box_render_packet_t* box_render_ring_acquire(
    box_render_ring_t *ring);

/** Get the packet that was acquired and not yet submitted.
 *
 * @param ring The ring
 * @return The packet, or NULL when none is acquired
 */
box_render_packet_t* box_render_ring_current(
    box_render_ring_t *ring);

/** Hand the acquired packet to the consumer, or render it when the main thread renders.
 *
 * @param ring The ring
 */
void box_render_ring_submit(
    box_render_ring_t *ring);

/** Replace the backend of a ring.
 *
 * @param ring The ring
 * @param backend The backend, copied
 */
void box_render_ring_set_backend(
    box_render_ring_t *ring,
    const box_render_backend_t *backend);

/** Get the backend of a ring.
 *
 * @param ring The ring
 * @return The backend
 */
const box_render_backend_t* box_render_ring_backend(
    const box_render_ring_t *ring);

/** Check whether a ring still has the null backend it was created with.
 *
 * @param ring The ring
 * @return Whether no backend was set
 */
bool box_render_ring_is_default(
    const box_render_ring_t *ring);

/** Hold or release the packets of a ring.
 *
 * @param ring The ring
 * @param paused Whether the consumer stops taking packets
 */
void box_render_ring_pause(
    box_render_ring_t *ring,
    bool paused);

/** Wait until every submitted packet of a ring was rendered.
 *
 * @param ring The ring
 */
void box_render_ring_sync(
    box_render_ring_t *ring);

/** Read the counters of a ring.
 *
 * @param ring The ring
 * @param status The counters
 */
void box_render_ring_status(
    box_render_ring_t *ring,
    RenderStatus *status);

/** Kind of a field of a transitionable component */
typedef enum box_render_transition_kind_t {
    BOX_RENDER_TRANSITION_F32 = 0,  /**< float, interpolated linearly */
    BOX_RENDER_TRANSITION_U8        /**< uint8_t, interpolated in float and rounded */
} box_render_transition_kind_t;

/** A field of a transitionable component */
typedef struct box_render_transition_field_t {
    uint16_t offset;                /**< Byte offset in the component */
    uint16_t kind;                  /**< box_render_transition_kind_t */
} box_render_transition_field_t;

/** The fields of a transitionable component, the only description the resolver has of it */
typedef struct box_render_transition_layout_t {
    int32_t size;                   /**< Size of the component, at most
                                     *   BOX_RENDER_TRANSITION_VALUE */
    int32_t count;                  /**< Fields */
    box_render_transition_field_t fields[BOX_RENDER_TRANSITION_FIELDS]; /**< The fields */
} box_render_transition_layout_t;

/** Transition of one component of a node on the render thread */
typedef struct box_render_transition_value_t {
    bool present;                   /**< A record set the component */
    bool active;                    /**< The transition runs */
    TimingFunction timing_function; /**< Curve */
    float delay;                    /**< Seconds the value holds still after the start */
    float duration;                 /**< Seconds from the end of the delay to the target */
    double start;                   /**< World time of the start */
    uint8_t from[BOX_RENDER_TRANSITION_VALUE];    /**< Value at the start */
    uint8_t current[BOX_RENDER_TRANSITION_VALUE]; /**< Value at the time of the last packet */
    uint8_t target[BOX_RENDER_TRANSITION_VALUE];  /**< Value at the end */
} box_render_transition_value_t;

/** A transition node on the render thread, one per entity with Transition */
typedef struct box_render_transition_node_t {
    bool alive;                     /**< A create record made it and no destroy dropped it */
    bool listed;                    /**< The node is in the active list */
    int32_t running;                /**< Values whose transition runs */
    int64_t posed;                  /**< Frame of the packet whose pose record set world, so
                                     *   a slot or child of a node without one is skipped */
    mat4 world;                     /**< Rendered world matrix of the node in that packet */
    int64_t faded;                  /**< Frame of the packet whose fade record set the tints
                                     *   and alphas below, so a slot or child of a node without
                                     *   one is skipped */
    float above_add[3];             /**< Composed tints of the ancestors of the node, rendered:
                                     *   the color c becomes c * above_keep + above_add */
    float above_keep;               /**< See above_add */
    float above_alpha;              /**< Product of the rendered opacities of the ancestors */
    float fade_add[3];              /**< Composed tints of the node and its ancestors, rendered,
                                     *   what its descendants inherit */
    float fade_keep;                /**< See fade_add */
    float fade_alpha;               /**< Product of the rendered opacities of the node and its
                                     *   ancestors */
    box_render_transition_value_t values[BOX_RENDER_TRANSITION_COMPONENTS]; /**< Values by
                                     *   box_render_transition_component_t */
} box_render_transition_node_t;

/** Transition resolver state of the consumer of the packets, not backend state, so it survives
 * a device loss and a new backend */
typedef struct box_render_transition_table_t {
    ecs_vec_t nodes;                /**< box_render_transition_node_t by node id, 0 unused */
    ecs_vec_t active;               /**< uint32_t ids of the nodes with a running transition,
                                     *   its size at least the count of nodes */
} box_render_transition_table_t;

/** Get the field layout of a transitionable component.
 *
 * @param component The component
 * @return The layout
 */
const box_render_transition_layout_t* box_render_transition_layout(
    box_render_transition_component_t component);

/** Copy the fields of a component value into a value of a record or the table, zeroing the
 * padding, so values compare and copy as bytes.
 *
 * @param component The component
 * @param src The component value
 * @param dst Receives BOX_RENDER_TRANSITION_VALUE bytes
 */
void box_render_transition_pack(
    box_render_transition_component_t component,
    const void *src,
    void *dst);

/** Check whether two values of a component have the same bytes in every field.
 *
 * @param component The component
 * @param a A value
 * @param b A value
 * @return Whether no field differs
 */
bool box_render_transition_equal(
    box_render_transition_component_t component,
    const void *a,
    const void *b);

/** Interpolate two values of a component per field: floats linearly, uint8_t fields in float,
 * rounded. 0 and 1 give the exact end values.
 *
 * @param component The component
 * @param from The value at 0
 * @param to The value at 1
 * @param t Eased progress
 * @param out Receives the value, BOX_RENDER_TRANSITION_VALUE bytes
 */
void box_render_transition_mix(
    box_render_transition_component_t component,
    const void *from,
    const void *to,
    float t,
    void *out);

/** Linear progress of a transition at a time: 0 until the delay passed, then up to 1 over the
 * duration. A duration of 0 jumps from 0 to 1 at the end of the delay.
 *
 * @param start World time of the start
 * @param delay Seconds of delay
 * @param duration Seconds of the transition
 * @param time World time
 * @return The progress, 0 to 1
 */
float box_render_transition_progress(
    double start,
    float delay,
    float duration,
    double time);

/** Check whether a transition runs at a time, its progress under 1. The game thread and the
 * resolver both decide with this function, so they agree on the last active packet.
 *
 * @param start World time of the start
 * @param delay Seconds of delay
 * @param duration Seconds of the transition
 * @param time World time, the time of a packet
 * @return Whether the rendered value is not yet the target
 */
bool box_render_transition_running(
    double start,
    float delay,
    float duration,
    double time);

/** Create an empty transition table.
 *
 * @param t The table
 */
void box_render_transition_table_init(
    box_render_transition_table_t *t);

/** Free a transition table.
 *
 * @param t The table
 */
void box_render_transition_table_fini(
    box_render_transition_table_t *t);

/** Apply the transition records of a packet, evaluate every running transition at its time
 * and patch the transforms of its pose and slot records, parent nodes first, on the thread
 * that consumes the packets, before the backend renders it.
 *
 * Returns at once when the packet has no records and no transition runs. Allocates only when
 * a record creates a node past the end of the table.
 *
 * @param t The table
 * @param p The packet
 */
void box_render_transition_resolve(
    box_render_transition_table_t *t,
    box_render_packet_t *p);

/** Get the value of a component of a node at the time of the last resolved packet.
 *
 * @param t The table
 * @param node The node id
 * @param component The component
 * @param out Receives the value, the size of the component
 * @return Whether the node exists and has a value of the component
 */
bool box_render_transition_table_value(
    const box_render_transition_table_t *t,
    uint32_t node,
    box_render_transition_component_t component,
    void *out);

/** Get the number of nodes with a running transition.
 *
 * @param t The table
 * @return The nodes
 */
int32_t box_render_transition_table_active(
    const box_render_transition_table_t *t);

/** Game thread state of the transition extract */
typedef struct box_render_transitions_t box_render_transitions_t;

/** Register the system that detects the changes of transitioned components and sends the
 * transition records, a system of RenderExtract.
 *
 * @param world The world
 */
void box_render_transitions_init(
    ecs_world_t *world);

/** Register the system that sends the pose and slot records of the active transform
 * transitions, a system of RenderExtract that runs after the batches were sent and before the
 * shadow tiles take the regions of the casters.
 *
 * @param world The world
 */
void box_render_transitions_slots_init(
    ecs_world_t *world);

/** Private tag of the entities a transform transition moves in this frame: an entity with an
 * active transition of Position, Rotation or Scale and its drawn descendants. It keys their
 * slots as dynamic_transform and out of the bakes; the transition extract adds and removes it.
 */
extern ECS_TAG_DECLARE(TransitionTransform);

/** Private tag of the drawn entities whose shading an active transition patches in this
 * frame: the 2D shapes and texts of a material transition, and every drawn shape or text at or
 * below an entity with an active Tint or Opacity transition.
 *
 * It keys their slots as dynamic_material and out of the bakes; the transition extract adds and
 * removes it. A mesh shape of a material transition alone reads the patched material table. */
extern ECS_TAG_DECLARE(TransitionMaterial);

/** Private tag of the drawn entities at or below an entity with an active Opacity transition.
 *
 * Their key is computed as if their alpha were below 1 (box_draws_blend), so they draw in a
 * blended batch and cast no shadow while the transition runs; the transition extract adds and
 * removes it, which rekeys them to the batch of their target at the end. */
extern ECS_TAG_DECLARE(TransitionOpacity);

/** Singleton that holds the state of the transition extract, set by
 * box_render_transitions_init, so box_render_transitions_get reads it without a lookup */
typedef struct RenderTransitionState {
    box_render_transitions_t *state; /**< The state, owned by RenderExtractTransitions */
} RenderTransitionState;

/** Component id of RenderTransitionState */
extern ECS_COMPONENT_DECLARE(RenderTransitionState);

/** Get the state of the transition extract of a world, from the RenderTransitionState
 * singleton, without a name lookup.
 *
 * @param world The world
 * @return The state, NULL without the extract
 */
box_render_transitions_t* box_render_transitions_get(
    const ecs_world_t *world);

/** Get the transitionable component of a component id.
 *
 * @param component The component id
 * @param out Receives the transitionable component
 * @return Whether the id is transitionable
 */
bool box_render_transition_component(
    ecs_entity_t component,
    box_render_transition_component_t *out);

/** Get the node id of an entity.
 *
 * @param s The state of the transition extract
 * @param entity The entity
 * @return The node id, 0 when the entity has no node
 */
uint32_t box_render_transitions_node(
    const box_render_transitions_t *s,
    ecs_entity_t entity);

/** Check whether the transition of a component of a node runs at a time, as the resolver
 * decides it: from the same start time, delay and duration, with box_render_transition_running.
 *
 * @param s The state of the transition extract
 * @param node The node id
 * @param component The component
 * @param time World time, the time of the packet
 * @return Whether the resolver renders a value other than the target at that time
 */
bool box_render_transitions_active(
    const box_render_transitions_t *s,
    uint32_t node,
    box_render_transition_component_t component,
    double time);

/** Check whether any component of a node has a running transition at a time.
 *
 * @param s The state of the transition extract
 * @param node The node id
 * @param time World time, the time of the packet
 * @return Whether one of its components is active
 */
bool box_render_transitions_node_active(
    const box_render_transitions_t *s,
    uint32_t node,
    double time);

/** Count the entities the material extract visited in the last frame: the entities of the
 * nodes with an active material transition and those of their IsA instances that own a slot of
 * the material table, have 2D or text shading or are IsA bases themselves, 0 in a frame without
 * one. Mesh instances that inherit their material are skipped per table and not counted.
 *
 * @param s The state of the transition extract
 * @return The entities
 */
int32_t box_render_transitions_material_visited(
    const box_render_transitions_t *s);

/** Get the transition resolver table of a ring, used by the consumer of its packets.
 *
 * @param ring The ring
 * @return The table
 */
box_render_transition_table_t* box_render_ring_transitions(
    box_render_ring_t *ring);

#include "particles.h"

#endif
