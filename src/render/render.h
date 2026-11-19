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
    int64_t page_frame_bytes;   /**< Bytes of the frame buffers of the instance pages alive,
                                 *   which only the pages of the MappingFrame materials have */
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
                                 *   scatter segments, only when a ShapeMesh changed */
    int64_t frame_bytes;        /**< Bytes the last extract uploaded into the frame streams of
                                 *   the pages of the MappingFrame materials, runs and scatter
                                 *   segments, when a transform or a frame changed */
    int32_t clips;              /**< ClipChildren elements in the clip buffer */
    int32_t runs;               /**< Runs of consecutive 2D slots of the order, the draws of
                                 *   the ordered 2D shapes before a view splits them: a shadow
                                 *   in another batch than its element breaks a run */
    int32_t box_shadows;        /**< Shadows of 2D shapes (BoxShadow) drawn */
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

/** Get the clip area of a ClipChildren element: its LayoutBounds, else its Rect, shrunk by its
 * Border.width on each side and with the corners rounded by its Border.radius less the width,
 * the inner edge of the border as the 2D shader draws it.
 *
 * Reads the target Border on the game thread: while a Border transition runs, the clipped
 * children keep the target width and corner radius, not the ones the border renders with.
 *
 * @param world The world
 * @param e The element
 * @param half Receives half the width and half the height of the area, 0 when it has neither
 * @return The corner radius, clamped to half the smaller side
 */
float box_render_clip_area(
    const ecs_world_t *world,
    ecs_entity_t e,
    float half[2]);

/** Check whether a world position is inside the clip areas of the ClipChildren ancestors of an
 * entity, in the local XY plane of each, as the 2D shader clips it; an empty area contains none.
 *
 * The ancestors of a floating element (LayoutFloat without clip) do not clip it, and nothing
 * inside a float that hides (box_render_float_hidden) contains the position.
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

/** Check whether a floating element hides while its anchor is clipped out (LayoutFloat with
 * hide_clipped and without clip).
 *
 * The anchor is clipped out when its ClipChildren ancestors, scroll views included, clip all of
 * it away (box_render_clip_hides without skipping scrolls), or it is inside a hiding float.
 *
 * @param world The world
 * @param e The entity
 * @return Whether it is such a float and its anchor is clipped out
 */
bool box_render_float_hidden(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Check whether the ClipChildren ancestors of an entity clip all of it away.
 *
 * Its bounds (its LayoutBounds, else its Rect, else its origin) have no part of positive size
 * inside the area of one of them. A clip with an empty area hides everything below it.
 *
 * A clip that scrolls (LayoutScroll) is skipped so Tab can scroll its content into view, and the
 * ancestors of a LayoutFloat that is not a clipped overlay do not clip it. Everything inside a
 * float that hides (box_render_float_hidden) is hidden.
 *
 * @param world The world
 * @param e The entity
 * @return Whether no part of it is drawn
 */
bool box_render_clip_hides(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Find the texture frame of an entity, whose space MappingFrame projects in: the entity itself
 * when it has TextureFrame, else the nearest ancestor with it.
 *
 * @param world The world
 * @param e The entity
 * @return The frame, 0 for none
 */
ecs_entity_t box_render_texture_frame(
    const ecs_world_t *world,
    ecs_entity_t e);

/** Set the instances one batch holds before its shapes continue in another, for tests.
 *
 * A batch is one run of slots of an instance page, so the default of each store is its chunk,
 * the slots that keep every buffer of its pages within the 128 MiB storage binding every WebGPU
 * device supports.
 *
 * The size applies to every store, capped by the slots its layout allows (128 MiB over its
 * largest stride); shapes added after a change follow it.
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
 * every WebGPU device supports; the size applies to every store, capped by the slots its layout
 * allows.
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

/** Take the texture graph readbacks the GPU backend delivered since the last call, with their
 * texture and node entities, without the ones the previews kept for
 * box_render_texture_graph_reads.
 *
 * @param world The world
 * @param out Receives box_render_texture_read_t
 * @return Whether the world renders with the GPU backend
 */
bool box_render_texture_graph_reads_take(
    ecs_world_t *world,
    ecs_vec_t *out);

/** Move the readbacks of other callers that the previews took along with theirs to the end of
 * a vector.
 *
 * @param world The world
 * @param out Receives box_render_texture_read_t
 */
void box_render_texture_previews_kept(
    ecs_world_t *world,
    ecs_vec_t *out);

/** Move the readbacks of the previews among the ones a caller of
 * box_render_texture_graph_reads took to the previews, which encode them in their next frame.
 *
 * @param world The world
 * @param out The readbacks, compacted
 * @param first Index of the first readback the caller took
 */
void box_render_texture_previews_filter(
    ecs_world_t *world,
    ecs_vec_t *out,
    int32_t first);

/** Register the system that reads back the nodes with a TextureNodePreview and writes their
 * TextureNodePreviewStatus, and TextureGraphStats. It runs after the texture extract.
 *
 * @param world The world
 */
void box_render_texture_previews_init(
    ecs_world_t *world);

/** Counters of the light extract of a world */
typedef struct box_render_lights_stats_t {
    int32_t scenes;             /**< Scenes that have lights */
    int32_t lights;             /**< Lights that light something, of every scene */
    int32_t culled;             /**< Point and spot lights no view of their scene sees */
    int64_t upload_bytes;       /**< Bytes the last extract uploaded into light buffers */
    int32_t shadow_tiles;       /**< Shadow atlas tiles of every scene */
    int32_t shadow_dropped;     /**< Shadow casting lights a view sees that got no tile */
    int32_t fog_volumes;        /**< Fog volume records of every view */
    int32_t fog_dropped;        /**< Fog volumes views saw beyond BOX_RENDER_FOG_VOLUMES_MAX */
} box_render_lights_stats_t;

/** Fog volumes the light extract gives the views, see box_render_fog_views */
typedef struct box_render_fog_t box_render_fog_t;

/** Create the state of the fog volumes of the views and its query.
 *
 * @param world The world
 * @return The state
 */
box_render_fog_t* box_render_fog_new(
    ecs_world_t *world);

/** Free the state of the fog volumes of the views.
 *
 * @param fog The state, NULL for none
 */
void box_render_fog_free(
    box_render_fog_t *fog);

/** Texels along each edge of the fog volume noise texture */
#define BOX_RENDER_FOG_NOISE_SIZE (64)

/** Generate the texels of the fog volume noise texture, the same on every call and platform.
 *
 * Four octaves of value noise that tile over the texture, of 2, 4, 8 and 16 lattice cells across
 * with the amplitudes 1, 1/2, 1/4 and 1/8, from an integer hash of a fixed seed.
 *
 * They are remapped as tools/noise_texture.py does, piecewise linearly from the 1st percentile
 * to 0, the median to 0.5 and the 99th percentile to 1, clamped: half of the texels are below
 * 0.5 and the texels fill 0..255, with 1% clipped to either end.
 *
 * @param out Receives BOX_RENDER_FOG_NOISE_SIZE^3 texels, x fastest, then y, then z
 */
void box_render_fog_noise(
    uint8_t *out);

/** Give every lit view with LightSettings.fog_volumes the FogVolumes of the scene of its camera
 * that may be in its frustum, back to front by the distance of their centers from the camera.
 *
 * A view that sees more than BOX_RENDER_FOG_VOLUMES_MAX keeps the nearest and warns once.
 * Views without a view entity, unlit views and cameras without a depth range get none. The
 * records go after those of the views before, with the noise offsets at the time of the packet.
 *
 * @param world The world
 * @param fog The state
 * @param p The packet, whose views are lit
 * @param stats Counters of the light extract, which count the records and dropped volumes
 */
void box_render_fog_views(
    ecs_world_t *world,
    box_render_fog_t *fog,
    box_render_packet_t *p,
    box_render_lights_stats_t *stats);

/** Give every view with a tree on in BvhDebug the boxes of those trees of the spatial index of
 * the scene of its camera, after the boxes of the views before it.
 *
 * @param world The world
 * @param p The packet
 */
void box_render_debug_bvh(
    const ecs_world_t *world,
    box_render_packet_t *p);

/** Give every view that culls by portals (CullSettings.portals, a perspective camera, a scene
 * with areas) the visibility block of what its camera sees, write its PortalStatus and
 * PortalTree, and the lines of its portal world (PortalDebug). A view that only draws lines or
 * keeps a frozen traversal gets no block.
 *
 * @param world The world
 * @param stage The stage of the extract system, which the statuses are set through
 * @param p The packet
 */
void box_render_portals(
    ecs_world_t *world,
    ecs_world_t *stage,
    box_render_packet_t *p);

/** Get the offset the wind of a FogVolume moves its noise by at a time: its wind times the time
 * on each axis, computed in double precision and wrapped to [0, BOX_RENDER_FOG_NOISE_PERIOD
 * times its noise_scale), the distance over which both noise samples of the shader repeat.
 *
 * @param fog The FogVolume
 * @param time Seconds the world has advanced in total
 * @param out Receives the three components in world units
 */
void box_render_fog_offset(
    const FogVolume *fog,
    double time,
    float *out);

/** Star fields the light extract gives the views, see box_render_star_views */
typedef struct box_render_stars_t box_render_stars_t;

/** Radius in pixels below which a star is drawn as a point, its energy spread with a tent filter
 * over the at most two by two pixels nearest its center */
#define BOX_RENDER_STAR_MIN_RADIUS (0.5f)

/** Create the state of the star fields of the views and its query.
 *
 * @param world The world
 * @return The state
 */
box_render_stars_t* box_render_stars_new(
    ecs_world_t *world);

/** Free the state of the star fields of the views.
 *
 * @param stars The state, NULL for none
 */
void box_render_stars_free(
    box_render_stars_t *stars);

/** Write the record of a StarField: its values clamped to their ranges and the unit axes of the
 * rotation of a world transform, the identity without one.
 *
 * @param field The StarField
 * @param transform World transform of the entity, NULL for none
 * @param entity The entity
 * @param out Receives the record
 */
void box_render_star_record(
    const StarField *field,
    const WorldTransform *transform,
    ecs_entity_t entity,
    box_render_star_field_t *out);

/** Give every lit view with a perspective camera with a depth range the StarFields of the scene
 * of its camera, by entity id, after the records of the views before. A StarField with count 0
 * is left out. Views without a view entity and captures get none.
 *
 * @param world The world
 * @param stars The state
 * @param p The packet, whose views are lit
 */
void box_render_star_views(
    ecs_world_t *world,
    box_render_stars_t *stars,
    box_render_packet_t *p);

/** A star of a star field as etc/shaders/star_field.wgsl generates it */
typedef struct box_render_star_t {
    float direction[3];     /**< Unit world direction toward the star */
    float relative;         /**< Relative brightness, u^falloff, 0 to 1 */
    float temperature;      /**< Temperature in kelvin */
    float radius;           /**< Radius of its disc in pixels, size * sqrt(relative) */
    float drawn;            /**< Radius of the disc it is drawn with, radius, or 0 for a point
                             *   when radius is below BOX_RENDER_STAR_MIN_RADIUS */
    float peak;             /**< Emission at the center of a disc of radius r, brightness * r^2
                             *   / (r^2 + 1/12): with its pixel ramp it keeps brightness * pi *
                             *   r^2; of a point, that energy in the pixel it is centered on */
    float color[3];         /**< Linear color, the brightest channel 1 */
} box_render_star_t;

/** Hash a 32 bit integer, the hash of etc/shaders/star_field.wgsl.
 *
 * @param x The integer
 * @return The hash
 */
uint32_t box_render_star_hash(
    uint32_t x);

/** Get the display encoded blackbody color of a temperature, the fit of Tanner Helland that the
 * planet scene uses: the brightest channel is 255.
 *
 * @param kelvin The temperature, clamped to 1000..40000
 * @param out Receives r, g and b from 0 to 255
 */
void box_render_star_blackbody(
    float kelvin,
    float *out);

/** Generate a star of a star field as the shader does.
 *
 * @param field The record of the star field
 * @param index Index of the star, 0 to count - 1
 * @param out Receives the star
 */
void box_render_star_sample(
    const box_render_star_field_t *field,
    uint32_t index,
    box_render_star_t *out);

/** Check whether a sphere may reach into the frustum of a view.
 *
 * A camera that follows the aspect of its surface is tested with a wide aspect of 4, an
 * orthographic one sized by its surface only by its depth range, so a sphere the view may see
 * is never left out.
 *
 * @param v The view
 * @param center World center of the sphere
 * @param radius Radius of the sphere
 * @return Whether the sphere is not entirely outside a plane of the frustum
 */
bool box_render_view_sees(
    const box_render_view_t *v,
    const float *center,
    float radius);

/** A reflection plane a view may render, for box_render_planar_select */
typedef struct box_render_planar_plane_t {
    float plane[4];         /**< Unit normal and minus its dot with a point of the plane,
                             *   positive on the front side */
} box_render_planar_plane_t;

/** Write the matrix that mirrors points about a plane.
 *
 * It maps p to p - 2 (n . p + d) n for the unit normal n and the offset d, so its determinant
 * is -1 and it is its own inverse.
 *
 * @param plane Unit normal and minus its dot with a point of the plane
 * @param out Receives the matrix
 */
void box_render_planar_matrix(
    const float *plane,
    mat4 out);

/** Make the near plane of a reversed Z perspective projection the clip plane of a reflection
 * (Lengyel, "Oblique View Frustum Depth Projection and Clipping").
 *
 * Its z row becomes w - a * plane: a point on the plane gets depth 1, a point on its negative
 * side a depth above 1, which is clipped. The x, y and w rows do not change, so the image keeps
 * the screen of the projection.
 *
 * The far plane passes through the corner of the frustum farthest along the plane, so depth
 * stays at or above 0 within the frustum, for a finite and an infinite far plane alike.
 *
 * @param proj The projection, as box_camera_projection makes it
 * @param plane The clip plane in view space, keeping its positive side
 * @param out Receives the projection, which may be proj
 * @return Whether the plane crosses the frustum; when not, out is proj
 */
bool box_render_planar_oblique(
    mat4 proj,
    const float *plane,
    mat4 out);

/** Pick the planes a view renders reflections of: those that cross its frustum and have its
 * camera on their front side, at most BOX_PLANAR_REFLECTION_MAX, nearest to the camera first,
 * then by index.
 *
 * The frustum is the one box_render_view_sees tests, a camera that follows the aspect of its
 * surface taken at an aspect of 4; the planes are unbounded.
 *
 * @param v The view, with a perspective camera
 * @param planes The planes
 * @param count Planes
 * @param out Receives the indices of the planes picked, BOX_PLANAR_REFLECTION_MAX of room
 * @return Planes picked
 */
int32_t box_render_planar_select(
    const box_render_view_t *v,
    const box_render_planar_plane_t *planes,
    int32_t count,
    int32_t *out);

/** Register the system that gives every lit perspective view of a scene with
 * Scene.planar_reflections the PlanarReflections of the
 * scene of its camera it renders (box_render_planar_select), after the light extract.
 *
 * @param world The world
 */
void box_render_planar_init(
    ecs_world_t *world);

/** Get the instance of the shape of an entity in the instance pages.
 *
 * @param world The world
 * @param e The entity
 * @param page Receives the handle of the transform stream of its page
 * @param slot Receives its slot in the page
 * @return Whether the entity is drawn in a batch
 */
bool box_render_draws_instance(
    const ecs_world_t *world,
    ecs_entity_t e,
    uint32_t *page,
    uint32_t *slot);

/** Get the transform of the instance of the shape of an entity as its instance page holds it.
 *
 * @param world The world
 * @param e The entity
 * @param rows Receives the three rows of its model matrix (box_render_transform_t)
 * @return Whether the entity is drawn in a batch
 */
bool box_render_draws_instance_transform(
    const ecs_world_t *world,
    ecs_entity_t e,
    vec4 *rows);

/** Register the system that sends the lights of every scene and the lights of every view.
 *
 * Lights are collected per scene into a light buffer that is uploaded when it changed; every
 * view of the packet gets the buffer and the ambient light of the scene of its camera, and a
 * lit view with LightSettings.fog_volumes the FogVolumes of that scene in its frustum.
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

/** Write a light of a light buffer again from the values a transition renders, as the light
 * extract does: its color from Rgb and intensity times the record scale, the range of a point or
 * spot light, the cone of a spot light. Position, direction and shadow tile stay; resolver only.
 *
 * @param target The target of the light record
 * @param values The rendered intensity, range, inner and outer angle
 * @param rgb The rendered Rgb, white when the target has none
 * @param light The light, patched
 */
void box_render_lights_patch(
    const box_render_transition_light_t *target,
    const float *values,
    const Rgb *rgb,
    box_render_light_t *light);

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

/** Get the sun of a PlanetAtmosphere as the light extract of this frame found it: a
 * DirectionalLight of a scene with its top of atmosphere values, which no atmosphere attenuated.
 *
 * @param state The state of the light extract, see box_render_lights_state
 * @param scene The scene, 0 for the default scene
 * @param light The DirectionalLight entity, 0 for the first one of the scene
 * @param direction Receives the normalized direction toward the light
 * @param color Receives its linear color times intensity
 * @return Whether the scene has the light
 */
bool box_render_lights_planet_sun(
    const void *state,
    ecs_entity_t scene,
    ecs_entity_t light,
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

/** Smallest ramp of the coverage remap of a CloudLayer, which makes a softness of 0 a step at
 * the threshold, COOKIE_MIN_RAMP of the cookie remap */
#define BOX_RENDER_CLOUD_MIN_RAMP (1e-5f)

/** Get the coverage of a CloudLayer from a texture value, as atmosphere.wgsl remaps it:
 * saturate((t - threshold) / (2 * softness) + 0.5), with a ramp of at least
 * BOX_RENDER_CLOUD_MIN_RAMP.
 *
 * @param t The texture value
 * @param threshold Value at which the coverage is one half
 * @param softness Half the range of values the coverage ramps over
 * @return The coverage, 0 to 1
 */
float box_render_cloud_coverage(
    float t,
    float threshold,
    float softness);

/** Get the opacity of a CloudLayer from its coverage: saturate(coverage * density).
 *
 * @param coverage The coverage
 * @param density The density
 * @return The opacity, 0 to 1
 */
float box_render_cloud_alpha(
    float coverage,
    float density);

/** Fraction of the size of a CloudLayer its offset moves before the environment of its
 * Atmosphere is rendered again */
#define BOX_RENDER_CLOUD_ENV_MOVE (1.0f / 64.0f)

/** Check whether the offset of a CloudLayer moved far enough since the last render of the
 * environment of its Atmosphere to render it again: by more than BOX_RENDER_CLOUD_ENV_MOVE of
 * its size, the shortest way around the wrap of the offsets to [0, size).
 *
 * @param was Offset along X and Z at the last render
 * @param now Offset along X and Z now
 * @param size World units the texture repeats over
 * @return Whether it moved far enough, false for a size at or below 0
 */
bool box_render_cloud_moved(
    const float *was,
    const float *now,
    float size);

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

/** Get the cascades of a view the last shadow extract scheduled to render.
 *
 * @param world The world
 * @param view The view entity
 * @return Mask of the cascades, bit i for cascade i, 0 for a view without cascades
 */
uint32_t box_render_shadow_rendered(
    const ecs_world_t *world,
    ecs_entity_t view);

/** Resolve shadow settings: every field is clamped to its valid range and an even
 * filter_size rounds up to the next odd one, other values apply as set.
 *
 * @param settings The settings of a view
 * @param out Receives the resolved settings
 */
void box_render_shadow_settings(
    const ShadowSettings *settings,
    ShadowSettings *out);

/** Resolve the levels of detail of a view: levels is clamped to BOX_LOD_LEVELS_MAX, a single
 * level is none (levels 0), distances[0] and negative or non-finite distances are 0, each is at
 * least the one before it and the distances past the levels are 0.
 *
 * @param settings The settings of a view
 * @param out Receives the resolved settings
 */
void box_render_lod_settings(
    const LodSettings *settings,
    LodSettings *out);

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

/** Write the 6 world planes of the box of one octant of a point light tile: the part of the box
 * of box_render_shadow_point_planes on the side of the light the octant faces, bit 0 of the
 * octant negating x, bit 1 y and bit 2 z, inside where non negative, normalized.
 *
 * @param tile The tile, octahedral
 * @param octant The octant, 0 to 7
 * @param out Receives the planes
 */
void box_render_shadow_octant_planes(
    const box_render_shadow_tile_t *tile,
    int32_t octant,
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

/** Make the draw extract follow PlanetAtmosphere: a mesh shape whose nearest PlanetAtmosphere
 * at or above it changes, by an added or removed PlanetAtmosphere or by a new parent, takes the
 * planet light slot of the new one in its batch key. Called by box.atmosphere.
 *
 * @param world The world
 */
void box_render_planet_observers(
    ecs_world_t *world);

/** Get the planet light slot the mesh shapes at or below a PlanetAtmosphere entity reference in
 * their shading, which the planet light of the entity in the packet carries.
 *
 * A slot is taken by the first mesh shape the draw extract keys under the entity in a scene
 * with Scene.planet_light, the lowest free one, and given back when the entity loses its
 * PlanetAtmosphere.
 *
 * @param world The world
 * @param planet The entity with the PlanetAtmosphere
 * @return The slot, 1 to BOX_RENDER_PLANET_LIGHT_MAX - 1, 0 for none
 */
uint32_t box_render_planet_light_slot(
    const ecs_world_t *world,
    ecs_entity_t planet);

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
    bool valid;             /**< A static surface can use the probe, so views sample it */
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
 * A volume can progress when its settings are valid, its budget_ms is positive, its
 * LightBake.compute is true, a view of its scene was in the packet of the last frame and the
 * GPU backend renders; a volume that cannot progress counts nothing.
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
 * Once its light bake settled or stopped computing, a scene settles when it first converges, or
 * when every resident probe has a capture after a light, ambient, environment or bake change
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

/** Resolve the settings of a depth of field pass.
 *
 * @param in The settings of an effect entity
 * @param out Receives the resolved settings
 */
void box_render_depth_of_field_settings(
    const DepthOfField *in,
    DepthOfField *out);

/** Get the constants that turn a reversed Z depth of a camera into view depth, as
 * etc/shaders/depth_of_field.wgsl does: z = (k[0] + k[1] * d) / (k[2] * d + k[3]). Depth is 1
 * at the near plane and 0 at the far plane, or at infinity for a perspective camera without one.
 *
 * @param camera The camera
 * @param out Receives the four constants
 */
void box_render_depth_of_field_linearize(
    const Camera *camera,
    float *out);

/** Get the view depth of a reversed Z depth with the constants of
 * box_render_depth_of_field_linearize.
 *
 * @param k The constants
 * @param depth The depth, above 0
 * @return The distance from the camera along its forward axis
 */
float box_render_depth_of_field_view_depth(
    const float *k,
    float depth);

/** Get the signed circle of confusion of a view depth as etc/shaders/depth_of_field.wgsl
 * computes it: -1 in front of the focus at the largest blur, 0 sharp, +1 behind it at the
 * largest blur. The near side is 0 without near_blur.
 *
 * @param dof The resolved settings
 * @param z The view depth, INFINITY for a pixel without geometry
 * @return The circle of confusion
 */
float box_render_depth_of_field_coc(
    const DepthOfField *dof,
    float z);

/** Get the signed circle of confusion of a pixel from its reversed Z depth; depth 0 is the sky.
 *
 * @param dof The resolved settings
 * @param k The constants of box_render_depth_of_field_linearize
 * @param depth The depth of the pixel
 * @return The circle of confusion
 */
float box_render_depth_of_field_coc_depth(
    const DepthOfField *dof,
    const float *k,
    float depth);

/** Get the circle of confusion of a pixel of a tilt shift as etc/shaders/depth_of_field.wgsl
 * does: 0 within tilt_band of the line through tilt_center turned by tilt_angle, up to 1 at
 * tilt_transition past the band edge, positive on both sides, distances in image heights.
 *
 * @param dof The resolved settings
 * @param x Normalized image X of the pixel, 0 at the left
 * @param y Normalized image Y of the pixel, 0 at the top
 * @param aspect Width of the image over its height
 * @return The circle of confusion
 */
float box_render_depth_of_field_tilt_coc(
    const DepthOfField *dof,
    float x,
    float y,
    float aspect);

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

/** Resolve the settings of a color grading pass.
 *
 * @param in The settings of an effect entity
 * @param out Receives the resolved settings
 */
void box_render_grading_settings(
    const ColorGrading *in,
    ColorGrading *out);

/** Resolve the settings of a vignette pass.
 *
 * @param in The settings of an effect entity
 * @param out Receives the resolved settings
 */
void box_render_vignette_settings(
    const Vignette *in,
    Vignette *out);

/** Resolve the settings of a chromatic aberration pass.
 *
 * @param in The settings of an effect entity
 * @param out Receives the resolved settings
 */
void box_render_chromatic_aberration_settings(
    const ChromaticAberration *in,
    ChromaticAberration *out);

/** Get the distance from the center to its farthest image corner in pixels, the distance at
 * which the offset of a chromatic aberration reaches its intensity.
 *
 * @param ca The resolved settings
 * @param width Width of the image in pixels
 * @param height Height of the image in pixels
 * @return The distance, at least 1
 */
float box_render_chromatic_aberration_corner(
    const ChromaticAberration *ca,
    float width,
    float height);

/** Get the offset of a chromatic aberration at a pixel as
 * etc/shaders/chromatic_aberration.wgsl computes it: intensity * width * (d / d_corner)^falloff,
 * 0 at the center.
 *
 * @param ca The resolved settings
 * @param width Width of the image in pixels
 * @param height Height of the image in pixels
 * @param x Horizontal position in pixels, 0 left
 * @param y Vertical position in pixels, 0 top
 * @return The offset in pixels
 */
float box_render_chromatic_aberration_offset(
    const ChromaticAberration *ca,
    float width,
    float height,
    float x,
    float y);

/** Get the spectral weights of a tap of a chromatic aberration as
 * etc/shaders/chromatic_aberration.wgsl computes them: a red, green, blue ramp over the taps from
 * -offset (red) to +offset (blue), normalized so that the weights of each channel sum to 1.
 *
 * @param samples Taps, 3..16
 * @param i Tap, 0 at -offset
 * @param out Receives the red, green and blue weights
 */
void box_render_chromatic_aberration_weights(
    int32_t samples,
    int32_t i,
    float *out);

/** Get the vignette factor at a point of the image as etc/shaders/vignette.wgsl computes it:
 * smoothstep(radius, radius + smoothness, d), a hard step when smoothness is 0, where d is the
 * distance from the center in half diagonals with the aspect corrected by roundness.
 *
 * @param v The resolved settings
 * @param aspect Width over height of the image
 * @param u Horizontal normalized image coordinate, 0 left
 * @param w Vertical normalized image coordinate, 0 top
 * @return The factor, 0 inside the radius to 1 past the falloff, before the intensity
 */
float box_render_vignette_factor(
    const Vignette *v,
    float aspect,
    float u,
    float w);

/** Apply a vignette to a linear color as etc/shaders/vignette.wgsl does:
 * mix(c, c * color, factor * intensity) with the color decoded to linear.
 *
 * @param v The resolved settings
 * @param aspect Width over height of the image
 * @param u Horizontal normalized image coordinate, 0 left
 * @param w Vertical normalized image coordinate, 0 top
 * @param in The linear color
 * @param out Receives the color
 */
void box_render_vignette_apply(
    const Vignette *v,
    float aspect,
    float u,
    float w,
    const float *in,
    float *out);

/** Get the matrix of the white balance and exposure of a color grading: a chromatic adaptation
 * in LMS space as in Unity and Bevy, times the exposure; the identity times the exposure when
 * temperature and tint are 0.
 *
 * @param g The resolved settings
 * @param out Receives the rows of the matrix, which multiplies a linear color
 */
void box_render_grading_balance(
    const ColorGrading *g,
    float out[3][3]);

/** Grade a linear color as etc/shaders/grading.wgsl does.
 *
 * @param g The resolved settings
 * @param display The input is in display range, after a Tonemap
 * @param lut RGBA8 rows of a strip LUT of lut_size slices as stored, sRGB encoded, NULL for none
 * @param lut_size Slices of the LUT, its height
 * @param in The color
 * @param out Receives the graded color
 */
void box_render_grading_apply(
    const ColorGrading *g,
    bool display,
    const uint8_t *lut,
    int32_t lut_size,
    const float *in,
    float *out);

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

/** Get the BOX_RENDER_FEATURE_* features of a scene from its Scene.
 *
 * @param world The world
 * @param scene The scene root, 0 for the default scene, whose features are the Scene singleton
 * @return The features, 0 without a Scene
 */
uint32_t box_render_scene_features(
    const ecs_world_t *world,
    ecs_entity_t scene);

/** Get the features of a view: those of its scene without the lighting ones its LightSettings
 * turn off (baked, reflections, atmosphere), and the depth prepass.
 *
 * @param world The world
 * @param scene The scene the view draws
 * @param lighting The LightSettings of the view, NULL for a capture without a view
 * @return The features
 */
uint32_t box_render_view_features(
    const ecs_world_t *world,
    ecs_entity_t scene,
    const LightSettings *lighting);

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

/** Append a placement of a mover page to a packet.
 *
 * @param p The packet
 * @return The zeroed placement
 */
box_render_compose_t* box_render_packet_compose(
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

/** Read the seconds the consumer of a ring rendered and, within that, waited on the GPU: for
 * the window image, for the oldest frame in flight and for reads of GPU results.
 *
 * @param ring The ring
 * @param busy Receives the seconds rendering that were not a wait on the GPU
 * @param wait Receives the seconds waiting on the GPU
 */
void box_render_ring_load(
    box_render_ring_t *ring,
    double *busy,
    double *wait);

/** Read the seconds the renderer of a world rendered and waited on the GPU, as
 * box_render_ring_load does for its ring.
 *
 * @param world The world
 * @param busy Receives the seconds rendering that were not a wait on the GPU, 0 without a ring
 * @param wait Receives the seconds waiting on the GPU, 0 without a ring
 * @return Whether the world has a ring
 */
bool box_render_load(
    const ecs_world_t *world,
    double *busy,
    double *wait);

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
    TransitionBezier bezier;        /**< Handles of a CubicBezier */
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
 * padding, so values compare and copy as bytes. A light also keeps the members that switch at
 * once (bake, shadows and shadow_resolution), which are neither compared nor mixed.
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
 * rounded and clamped to 0..255. 0 and 1 give the exact end values; a t outside 0..1, from a
 * CubicBezier whose y overshoots, extrapolates past them.
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

/** Get the neutral value of a component for a target: the value that renders like not having
 * the component, with the fields that have no neutral of their own taken from the target.
 *
 * An added component animates from it (on_add) and a removed one to it (on_remove), so a Tint
 * fades in its own color.
 *
 * Pure, with no world access. A component without a neutral value (Position, Rgb, AlphaCutoff,
 * Transmission) gives the target.
 *
 * @param component The component
 * @param target The target, packed (box_render_transition_pack)
 * @param out Receives the neutral value, packed, BOX_RENDER_TRANSITION_VALUE bytes; may be target
 * @return Whether the component has a neutral value
 */
bool box_render_transition_neutral(
    box_render_transition_component_t component,
    const void *target,
    void *out);

/** Check whether the extract keeps the per field range of the targets of a component: a
 * transform component, a BoxShadow, a PointLight or a SpotLight.
 *
 * @param component The component
 * @return Whether it is one of them
 */
bool box_render_transition_ranged(
    box_render_transition_component_t component);

/** Widen the per field range of the targets of a transform component, a BoxShadow or a light
 * by what a CubicBezier can overshoot: from + (to - from) * e, e in the hull of 0, y1, y2 and 1,
 * is at most max(y1, y2) - 1 or -min(y1, y2) widths beyond it. Presets do not overshoot.
 *
 * @param component The component, nothing for one box_render_transition_ranged rejects
 * @param spec The transition of the start
 * @param lo The per field minimum, widened: three floats, four (offset_x, offset_y, blur and
 * spread) for a BoxShadow, two (intensity and range) for a PointLight and four (intensity,
 * range, inner and outer angle) for a SpotLight
 * @param hi The per field maximum, widened
 */
void box_render_transition_widen(
    box_render_transition_component_t component,
    const TransitionSpec *spec,
    float *lo,
    float *hi);

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

/** Private tag of the drawn entities whose BoxShadow has an active transition in this frame.
 *
 * It keys the slot of their shadow as dynamic_transform and dynamic_material, and keeps it while
 * the target shadow box has no area; the transition extract adds and removes it, which rekeys
 * the shadow to its static batch at the end. The slot of the shape keeps its key. */
extern ECS_TAG_DECLARE(TransitionBoxShadow);

/** Singleton that holds the state of the transition extract, set by
 * box_render_transitions_init, so box_render_transitions_get reads it without a lookup */
typedef struct RenderTransitionState {
    box_render_transitions_t *state; /**< The state, owned by extract.Transitions */
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

/** Check whether an entity renders a component it no longer has, owned or inherited, on its
 * way to the neutral value of an on_remove (box_transition_slot_t leaving), as the last extract
 * saw it.
 *
 * @param world The world
 * @param s The state of the transition extract, NULL for none
 * @param entity The entity
 * @param component The component
 * @return Whether the component of the entity leaves
 */
bool box_render_transitions_leaving(
    const ecs_world_t *world,
    const box_render_transitions_t *s,
    ecs_entity_t entity,
    box_render_transition_component_t component);

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

/** A light the transition extract found in this frame whose PointLight or SpotLight runs or
 * leaves, or whose Rgb runs: the light extract keeps it lit and sends a light record for it */
typedef struct box_render_light_transition_t {
    ecs_entity_t entity;    /**< The light entity */
    int32_t kind;           /**< 0 for a directional, 1 for a point and 2 for a spot light */
    bool leaving;           /**< The entity no longer has its light, which renders its way to
                             *   the neutral value of on_remove */
    uint32_t node;          /**< Node whose PointLight or SpotLight the light renders, 0 when
                             *   it renders its target */
    uint32_t rgb_node;      /**< Node whose Rgb the light renders, 0 for its target */
    float range;            /**< Largest range the light can render in this frame: its target
                             *   and the range of the targets since its node was last idle */
    float outer;            /**< Largest outer angle of a spot light, the same way */
    uint8_t value[BOX_RENDER_TRANSITION_VALUE]; /**< Target light, packed: the PointLight or
                             *   SpotLight, of a leaving light the neutral value it renders its
                             *   way to, the DirectionalLight intensity in the first float */
    Rgb rgb;                /**< Target Rgb, white without one */
    bool has_rgb;           /**< The light has an Rgb, owned or inherited */
} box_render_light_transition_t;

/** Get the transition of a light in this frame.
 *
 * Returns at once in a frame without a light transition, so lights of a world that does not
 * transition them cost a compare.
 *
 * @param s The state of the transition extract, NULL for none
 * @param entity The light entity
 * @return The transition, NULL for none
 */
const box_render_light_transition_t* box_render_transitions_light(
    const box_render_transitions_t *s,
    ecs_entity_t entity);

/** Get every light transition of this frame.
 *
 * @param s The state of the transition extract, NULL for none
 * @param out Receives the transitions
 * @return Their number
 */
int32_t box_render_transitions_lights(
    const box_render_transitions_t *s,
    const box_render_light_transition_t **out);

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

/** Count the entities the material extract visited in the last frame, 0 in a frame without an
 * active material transition.
 *
 * Those are the nodes of the transitions and their IsA instances that own a material slot, have
 * 2D or text shading or are IsA bases; mesh instances that inherit their material are skipped.
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
