/**
 * @file box/surface.h
 * @brief Surface component: setting it brings up a window or an offscreen target.
 *
 * The box.surface module connects the Surface component to the platform and renderer.
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

/** Window or offscreen render target. */
ECS_STRUCT(Surface, {
    int32_t width;                   /**< Requested width in points */
    int32_t height;                  /**< Requested height in points */
    float resolution_scale;          /**< Divides the pixels the scene is rendered at, 2 is half */
    bool vsync;                      /**< Present with vertical sync */
    Anisotropy anisotropy;           /**< Anisotropic samples of the texture samplers */
    TextureQuality texture_quality;  /**< Largest dimension a texture is scaled to */
    ecs_entity_t texture;            /**< For offscreen rendering to texture (window if 0) */
    ecs_entity_t view_root;          /**< Parent of the views to render in order, 0 for all views */
});

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
