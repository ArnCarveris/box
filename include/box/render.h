/**
 * @file box/render.h
 * @brief Render settings and status: the packet ring between the main thread and the renderer.
 *
 * The box.render module extracts every frame into a render packet that the render thread, or
 * the main thread in a browser build, hands to the backend.
 */

#ifndef BOX_RENDER_H
#define BOX_RENDER_H

#include "types.h"

#ifdef BOX_RENDER_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Packets in flight of the RenderSettings the module sets */
#define BOX_RENDER_IN_FLIGHT (2)

/** Largest number of packets in flight */
#define BOX_RENDER_IN_FLIGHT_MAX (8)

/** Fraction of the frame interval a present interval may deviate before it counts as uneven */
#define BOX_RENDER_PRESENT_TOLERANCE (0.1)

/** How frames travel from the main thread to the renderer, applied before the next frame */
ECS_STRUCT(RenderSettings, {
    int32_t in_flight;  /**< Packets in flight, default 2, from 1 to 8 */
    bool main_thread;   /**< The main thread renders the packet, default on in the browser */
});

/** Packet counters the engine writes after every frame, read only for applications */
ECS_STRUCT(RenderStatus, {
    int32_t submitted;  /**< Packets the main thread handed to the renderer */
    int32_t rendered;   /**< Packets the backend finished and the ring got back */
    int32_t in_flight;  /**< Packets submitted or being filled that are not yet rendered */
    int32_t stalls;     /**< Frames whose first packet waited for a free one: every packet was
                         *   in flight, the backpressure of the renderer */
    bool threaded;      /**< A render thread consumes the packets */
    int64_t upload_bytes; /**< Bytes of instance data the last frame uploaded, 0 when unchanged:
                           *   transform_bytes + shading_bytes + surface_bytes of
                           *   box_render_draws_stats_t */
    bool vsync;         /**< Frames present to a window with vsync, FIFO */
    float refresh_rate; /**< Refresh rate in Hz the surface reports, 0 when unknown or none */
    int32_t presented;  /**< Frames the backend presented */
    double present_ms;  /**< Interval between the last two presents */
    int32_t presents_late;  /**< Present intervals over 1.1 / App.fps, counted while capped */
    int32_t presents_early; /**< Present intervals under 0.9 / App.fps, counted while capped */
    int32_t device_losses;  /**< Lost GPU devices the renderer recreated, 0 for none; a failed
                             *   recreation is not counted and drops later frames, and the
                             *   browser neither reports nor recovers a loss (its page does) */
    int32_t shadow_tiles;   /**< Shadow atlas tiles the last extract allocated, over every
                             *   ShadowAtlas */
    int32_t shadow_dropped; /**< Shadow casting lights that needed a tile in the last extract
                             *   and got none, so they light unshadowed */
    int32_t shadow_cascades; /**< Cascades the last extract scheduled to render, over every
                             *   view with shadows (ShadowSettings.interval) */
    int32_t shadow_cascades_kept; /**< Cascades of those views the last extract kept from an
                             *   earlier frame instead */
});

/** How a view culled its instances in the last frame the GPU finished, set on the View entity.
 *
 * Read only for applications. Every instance of the scene counts once: drawn, out of its
 * DrawRange, frustum (or size) culled, or hidden behind closer geometry.
 *
 * The counts are read back from the GPU without waiting and arrive a variable number of frames
 * late, many while the GPU is behind as headless rendering does not wait for it; wait for `frame`
 * to reach a frame instead of counting frames.
 */
ECS_STRUCT(CullStatus, {
    int64_t frame;          /**< Frame the counts are from */
    int32_t instances;      /**< Instances the view tested */
    int32_t visible;        /**< Instances the view drew */
    int32_t frustum_culled; /**< Instances outside the frustum or below the smallest size */
    int32_t occlusion_culled; /**< Instances in the frustum hidden behind closer geometry */
    int32_t range_culled;   /**< Instances outside their DrawRange from the camera of the view,
                             *   in the frustum or not; not counted as frustum culled */
    int32_t batches_skipped; /**< Batches whose draws the view skipped by their bounds
                             *   (CullSettings.bounds), 0 without; their instances count as
                             *   frustum culled */
    int32_t casters_skipped; /**< Batches whose draws the shadow cascades of the view skipped by
                             *   their bounds, summed over the cascades */
});

/** Distances from the camera of a view between which a shape draws, set on a shape or on any
 * ancestor of shapes, a prefab or a template part; inherited by the instances of a prefab.
 *
 * The nearest DrawRange of a shape, its own or that of the closest ChildOf ancestor, applies to
 * it. The distance is measured from the camera of the view to the origin of the shape (the
 * translation of its world transform), so a near and a far version at the same place measure
 * the same distance. Each view measures from its own camera, and the shadow cascades of a view
 * from the camera of the view, so shadows fade with the shape.
 *
 * Beyond far + fade or nearer than near - fade the GPU cull pass drops the instance
 * (CullStatus.range_culled). Within fade of near or far it draws with a screen door dither in
 * the depth prepass, the color and the shadow passes: the share of pixels drawn goes from 0 to
 * 1 over [near - fade, near + fade] and from 1 to 0 over [far - fade, far + fade]. The dither
 * threshold of a fade in mirrors that of a fade out, so a shape with far F and another with near
 * F and the same fade draw exactly one of the two at each pixel.
 *
 * The shapes of one batch share their range: shapes with different ranges draw in different
 * batches. Planar reflections measure from their mirrored camera; bakes, captures and the shadow
 * tiles of spot and point lights ignore the range. */
ECS_STRUCT(DrawRange, {
    float near_;    /**< Distance in meters below which the shape does not draw, 0 for none */
    float far_;     /**< Distance in meters beyond which the shape does not draw, 0 for none */
    float fade;     /**< Half width in meters of the dithered crossfade around near and far, 0
                     *   for a hard cut */
});

/** Views per axis of the octahedral grid of an Impostor at most */
#define BOX_IMPOSTOR_FRAMES_MAX (32)

/** Largest width and height of an impostor atlas in pixels, frames times resolution */
#define BOX_IMPOSTOR_ATLAS_MAX (8192)

/** Octahedral impostor of a prefab, or of a template instance used as one: the engine renders
 * the hierarchy under it from a grid of directions into three atlases, which ImpostorDraw draws
 * on a card in place of its far instances.
 *
 * The bake renders a private copy of the hierarchy in a scene of its own, with the transform of
 * the root left out, so the atlases are in the frame of the root: an instance turned or scaled
 * shows them turned or scaled. Its shapes draw as in the scene of the source, with their
 * materials, alpha cutout, texture blends and layers, but unlit, without emission and without
 * Wind.
 *
 * Each view is orthographic, along a direction toward the center of the bounds of the
 * hierarchy, framed by the sphere around those bounds with a margin of resolution / 16 pixels
 * (at least 1) on every side, at resolution by resolution pixels. View (x, y) of the frames by
 * frames grid looks from the direction box_impostor_frame_direction returns, with the up axis
 * of the image along world +Y, or -Z for a view straight down or up; it fills the cell whose
 * top left pixel is (x * resolution, y * resolution) of every atlas:
 *
 * - albedo (ImpostorAlbedo): the base color, display encoded, and in alpha its coverage
 * - normal (ImpostorNormal): the normal in the frame of the root as n * 0.5 + 0.5, coverage in
 *   alpha
 * - depth (ImpostorDepth): the depth across the bounding sphere in every color channel, 0 where
 *   the sphere is nearest the view and 1 where it is farthest, coverage in alpha
 *
 * The empty texels of a cell take the color of the nearest covered texels (dilation) with an
 * alpha of 0, so the mips and filtering of an atlas do not bleed the background into the edges.
 *
 * The atlases are baked over several frames, at most ImpostorBudget.frames views per frame over
 * every impostor, and shared by every instance. A change to the shapes, materials or
 * transforms of the hierarchy (the components of box.geometry and box.material and Position,
 * Rotation and Scale, on its entities and their prefabs, and the loads of the textures they
 * refer to), to the bounds of the hierarchy or to frames, resolution or hemisphere bakes them
 * again, keeping the previous atlases until the new ones are complete; box_impostor_rebake asks
 * for a bake for other changes. Removing the component frees the atlases and the copy.
 *
 * Instances do not inherit it. Adding it adds ImpostorStatus.
 */
ECS_STRUCT(Impostor, {
    int32_t frames;     /**< Views per axis of the octahedral grid, frames * frames views,
                         *   default 8, from 2 to BOX_IMPOSTOR_FRAMES_MAX */
    int32_t resolution; /**< Width and height of a view in pixels, default 128, from 16; an atlas
                         *   wider than BOX_IMPOSTOR_ATLAS_MAX lowers it */
    bool hemisphere;    /**< Views from above the horizon only, for things standing on the
                         *   ground, default true; else from the whole sphere */
    float distance;     /**< Distance in meters from the camera beyond which ImpostorDraw draws
                         *   the impostor, default 100 */
    float fade;         /**< Half width in meters of the crossfade of ImpostorDraw around
                         *   distance, default 10 */
});

/** Bake state of an Impostor, written by the engine and read only for applications */
ECS_STRUCT(ImpostorStatus, {
    bool ready;         /**< The atlases hold a complete bake */
    bool baking;        /**< A bake is in progress, the atlases hold the previous one */
    int32_t revision;   /**< Bakes completed, the first one is 1 */
    int32_t atlas_size; /**< Width and height in pixels of the atlases of the last bake */
    int32_t frames;     /**< Views per axis of the last bake */
    int32_t resolution; /**< Width and height of a view of the last bake */
    bool hemisphere;    /**< The last bake has views from above the horizon only */
    int32_t baked;      /**< Views of the bake in progress sent to the renderer */
    float bake_time;    /**< Seconds the last bake took, from its first views to its last */
    vec3 center;        /**< Center of the views of the last bake in the frame of the root */
    float radius;       /**< Radius of the sphere the views of the last bake frame */
});

/** Views of impostors the engine renders per frame at most, over every Impostor, set as a
 * singleton; each view is three passes (albedo, normal and depth) of resolution by resolution
 * pixels. Default 8. */
ECS_STRUCT(ImpostorBudget, {
    int32_t frames;     /**< Views per frame, at least 1 */
});

/** An atlas of an Impostor */
ECS_ENUM(ImpostorChannel, {
    ImpostorAlbedo = 0, /**< Display encoded base color, coverage in alpha */
    ImpostorNormal = 1, /**< Normal in the frame of the root as n * 0.5 + 0.5 */
    ImpostorDepth = 2   /**< Depth across the bounding sphere along the view */
});

/** Get the direction of a view of the octahedral grid of an impostor, from the center of the
 * object toward the viewer, in the frame of the root of the prefab.
 *
 * The cell (x, y) maps to e = 2 * (x, y) / (frames - 1) - 1, so the outer cells lie on the
 * border of the octahedron. A hemisphere grid unfolds the upper half: d = (t.x, 1 - |t.x| -
 * |t.y|, t.y) with t = ((e.x + e.y) / 2, (e.x - e.y) / 2), whose border is the horizon. A sphere
 * grid unfolds the whole octahedron: d = (e.x, 1 - |e.x| - |e.y|, e.y), with the lower half
 * folded out to the corners, (1 - |d.z|, 1 - |d.x|) with the signs of d.x and d.z.
 *
 * @param frames Views per axis
 * @param hemisphere Whether the grid covers the upper hemisphere only
 * @param x Column of the view
 * @param y Row of the view, 0 at the top of the atlas
 * @param out Receives the unit direction
 */
void box_impostor_frame_direction(
    int32_t frames,
    bool hemisphere,
    int32_t x,
    int32_t y,
    vec3 out);

/** Find the view of the octahedral grid of an impostor that looks from the direction nearest a
 * direction, the inverse of box_impostor_frame_direction.
 *
 * @param frames Views per axis
 * @param hemisphere Whether the grid covers the upper hemisphere only, which takes a direction
 *        below the horizon as on it
 * @param dir Direction from the center toward the viewer, of any length
 * @param x Receives the column
 * @param y Receives the row
 */
void box_impostor_frame_of(
    int32_t frames,
    bool hemisphere,
    const vec3 dir,
    int32_t *x,
    int32_t *y);

/** Bake the atlases of an impostor again, as a change of its hierarchy does.
 *
 * @param world The world
 * @param impostor Entity with Impostor
 */
void box_impostor_rebake(
    ecs_world_t *world,
    ecs_entity_t impostor);

/** Write an atlas of an impostor to a PNG file in the next frame, the RGBA8 pixels as they are
 * stored: the albedo display encoded, the normal and the depth as values. For tests and tools.
 *
 * @param world The world
 * @param impostor Entity with a ready Impostor (ImpostorStatus.ready)
 * @param channel The atlas
 * @param path The file, written by the renderer; box_render_sync waits for it
 * @return 0 when the write was queued, -1 when the impostor has no ready atlas
 */
int box_impostor_write(
    ecs_world_t *world,
    ecs_entity_t impostor,
    ImpostorChannel channel,
    const char *path);

/** Register the box.render module.
 *
 * @param world The world
 */
void BoxRenderImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
