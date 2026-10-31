/**
 * @file box/surface.h
 * @brief Surface component: setting it brings up a window or an offscreen target.
 *
 * The box.surface module connects the Surface component to the platform and renderer: a
 * surface is a window, or an offscreen texture in a headless run or when it has a texture.
 */

#ifndef BOX_SURFACE_H
#define BOX_SURFACE_H

#include <flecs.h>

#ifdef BOX_SURFACE_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Width in points of a constructed surface */
#define BOX_SURFACE_WIDTH (1280)

/** Height in points of a constructed surface */
#define BOX_SURFACE_HEIGHT (720)

/** Maximum anisotropic filtering samples of the texture samplers */
ECS_ENUM(Anisotropy, {
    AnisotropyDefault = 0, /**< Engine default, four samples */
    AnisotropyOff = 1,     /**< No anisotropic filtering */
    AnisotropyLow = 2,     /**< Two samples */
    AnisotropyMedium = 4,  /**< Four samples */
    AnisotropyHigh = 8,    /**< Eight samples */
    AnisotropyUltra = 16   /**< Sixteen samples */
});

/** Largest size class a loaded texture is scaled down to, changing it loads textures again */
ECS_ENUM(TextureQuality, {
    TextureQualityDefault = 0,    /**< Engine default, the largest size class of 4096 pixels */
    TextureQualityLow = 512,      /**< Textures are capped at 512 pixels */
    TextureQualityMedium = 1024,  /**< Textures are capped at 1024 pixels */
    TextureQualityHigh = 2048,    /**< Textures are capped at 2048 pixels */
    TextureQualityVeryHigh = 4096 /**< Textures are capped at 4096 pixels */
});

/** Window or offscreen render target.
 *
 * Headless or with a texture a surface is offscreen, where a point is a pixel.
 * Views render by View.order, then creation: those under view_root, or without one those
 * not under the view_root of another surface. A change reconfigures it before the next frame.
 *
 * A surface with a texture renders before the others; a material that samples the texture
 * entity (PbrTextures, Image) samples its image of the same frame, never inside itself.
 */
ECS_STRUCT(Surface, {
    int32_t width;                   /**< Requested width in points, default 1280 */
    int32_t height;                  /**< Requested height in points, default 720 */
    float resolution_scale;          /**< Divides the render pixels, 2 is half, default 1 */
    float pixel_ratio;               /**< Offscreen pixels per point, default 1, 0 or less is 1 */
    bool vsync;                      /**< Present with vertical sync */
    Anisotropy anisotropy;           /**< Anisotropic samples of the texture samplers */
    TextureQuality texture_quality;  /**< Largest dimension a texture is scaled to */
    ecs_entity_t texture;            /**< Texture entity materials sample it as, 0 for none */
    ecs_entity_t view_root;          /**< Parent of the views it renders, 0 for unclaimed views */
    char *write_to_file;             /**< PNG every frame is written to, NULL for none */
});

/** Size and kind of a surface, written by the engine and read only for applications.
 *
 * A relative Surface.write_to_file is a file in AppStatus.tmp_dir, or in the working
 * directory of a process without one.
 */
ECS_STRUCT(SurfaceStatus, {
    int32_t width;          /**< Width of the presented image in pixels */
    int32_t height;         /**< Height of the presented image in pixels */
    int32_t point_width;    /**< Width in points, the size cameras and anchors count in */
    int32_t point_height;   /**< Height in points */
    int32_t render_width;   /**< Width the views render at, after resolution_scale */
    int32_t render_height;  /**< Height the views render at */
    bool window;            /**< The surface presents to a window, not to an offscreen texture */
});

/** Get the size of a surface in points.
 *
 * For a window the size in SurfaceStatus when it is set (both point sizes above 0): a window
 * resize only updates SurfaceStatus, not the requested size. Else the requested Surface size,
 * clamped to 1 to BOX_SURFACE_MAX_SIZE like the target that renders: offscreen the status is a
 * copy of the request written at render extract, so it is a frame behind a change of the
 * request. Without either the size is 0.
 *
 * @param surface The surface, NULL when it has none
 * @param status The status of the surface, NULL when it has none yet
 * @param width Out: the width in points
 * @param height Out: the height in points
 */
void box_surface_point_size(
    const Surface *surface,
    const SurfaceStatus *status,
    float *width,
    float *height);

/** Find the surface a view renders to.
 *
 * The first surface whose view_root is 0 or the parent of the view counts, in the order of the
 * query.
 *
 * @param world The world
 * @param surfaces Query for Surface and an optional SurfaceStatus, NULL to make one for the call
 * @param view The view
 * @return The surface entity, 0 when no surface renders the view
 */
ecs_entity_t box_surface_of_view(
    ecs_world_t *world,
    ecs_query_t *surfaces,
    ecs_entity_t view);

/** Get the size in points of the surface a view renders to.
 *
 * The surface comes from box_surface_of_view, its size from box_surface_point_size. Without one
 * the size is the default surface size, BOX_SURFACE_WIDTH by BOX_SURFACE_HEIGHT.
 *
 * @param world The world
 * @param surfaces Query for Surface and an optional SurfaceStatus, NULL to make one for the call
 * @param view The view
 * @param width Out: the width in points
 * @param height Out: the height in points
 * @return Whether a surface renders the view
 */
bool box_surface_view_size(
    ecs_world_t *world,
    ecs_query_t *surfaces,
    ecs_entity_t view,
    float *width,
    float *height);

/** Register the box.surface module.
 *
 * @param world The world
 */
void BoxSurfaceImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
