/**
 * @file box/texture.h
 * @brief Texture components: setting one loads or rasterizes an image and uploads it.
 *
 * The box.texture module turns a Texture or an Svg into a RenderTexture handle
 * that the PbrTextures slots of a material bind.
 */

#ifndef BOX_TEXTURE_H
#define BOX_TEXTURE_H

#include "types.h"

#ifdef BOX_TEXTURE_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Texture loaded from an image file, width and height 0 keep the source size */
ECS_STRUCT(Texture, {
    char *path;     /**< Image file, relative to the working directory */
    int32_t width;  /**< Requested width in pixels, 0 keeps the source width */
    int32_t height; /**< Requested height in pixels, 0 keeps the source height */
});

/** Texture rasterized from an SVG file or inline SVG data.
 *
 * A zero width or height is derived from the other one at the aspect ratio of
 * the SVG; both zero rasterize the SVG at its own size. The rasterized pixels
 * have straight alpha and go through the same upload path as a Texture, so an
 * Svg entity binds to a PbrTextures slot like any other texture. An entity with
 * both a Texture and an Svg is an error: the component that was set first owns
 * the texture.
 */
ECS_STRUCT(Svg, {
    char *file;     /**< SVG file, relative to the working directory */
    char *data;     /**< Inline SVG document, used when file is empty */
    int32_t width;  /**< Rasterization width in pixels, 0 follows the height */
    int32_t height; /**< Rasterization height in pixels, 0 follows the width */
});

/** Register the box.texture module.
 *
 * @param world The world
 */
void BoxTextureImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
