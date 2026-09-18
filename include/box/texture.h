/**
 * @file box/texture.h
 * @brief Texture component: setting it loads an image file and uploads it to the renderer.
 *
 * The box.texture module turns a Texture into a RenderTexture handle that the
 * PbrTextures slots of a material bind.
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
