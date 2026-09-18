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

/** Anisotropic samples the texture samplers take when the surface asks for the default */
#define BOX_ANISOTROPY_DEFAULT (4)

/** Maximum anisotropic filtering samples of the texture samplers */
ECS_ENUM(Anisotropy, {
    AnisotropyDefault = 0, /**< Engine default, four samples */
    AnisotropyOff = 1,     /**< No anisotropic filtering */
    AnisotropyLow = 2,     /**< Two samples */
    AnisotropyMedium = 4,  /**< Four samples */
    AnisotropyHigh = 8,    /**< Eight samples */
    AnisotropyUltra = 16   /**< Sixteen samples */
});

/** Maximum texture dimension in pixels a loaded texture is scaled down to */
ECS_ENUM(TextureQuality, {
    TextureQualityDefault = 0,   /**< Engine default, the largest size class */
    TextureQualityLow = 512,     /**< Textures are capped at 512 pixels */
    TextureQualityMedium = 1024, /**< Textures are capped at 1024 pixels */
    TextureQualityHigh = 2048,   /**< Textures are capped at 2048 pixels */
    TextureQualityVeryHigh = 4096 /**< Above the largest size class, capped at 2048 */
});

/** GPU texture block compression, declared but not implemented yet */
ECS_ENUM(TextureCompression, {
    TextureCompressionDefault = 0, /**< Engine default, no compression */
    TextureCompressionOff = 1,     /**< Textures stay RGBA8 */
    TextureCompressionBc7 = 2      /**< Reserved for BC7 compression */
});

/** On disk cache for processed textures, declared but not implemented yet */
ECS_ENUM(TextureCacheMode, {
    TextureCacheDefault = 0, /**< Engine default, no cache */
    TextureCacheOff = 1,     /**< Textures are processed on every load */
    TextureCacheOn = 2       /**< Reserved for a processed texture cache */
});

/** Window or offscreen render target, actual_* and point_* are written by the engine */
ECS_STRUCT(Surface, {
    int32_t width;          /**< Requested width in points */
    int32_t height;         /**< Requested height in points */
    int32_t actual_width;   /**< Framebuffer width in pixels */
    int32_t actual_height;  /**< Framebuffer height in pixels */
    int32_t point_width;    /**< Window width in points */
    int32_t point_height;   /**< Window height in points */
    bool vsync;             /**< Present with vertical sync */
    bool keep_source_textures; /**< Keep the pixels of a loaded texture in memory */
    Anisotropy anisotropy;  /**< Anisotropic samples of the texture samplers */
    TextureQuality texture_quality;        /**< Largest dimension a texture is scaled to */
    TextureCompression texture_compression; /**< Reserved, no compression is applied */
    TextureCacheMode texture_cache;        /**< Reserved, processed textures are not cached */
    char *title;            /**< Window title */
    char *write_to_file;    /**< PNG path to write frames to, NULL for none */
    int32_t write_nth;      /**< Write every nth frame with a frame suffix, 0 writes every frame */
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
