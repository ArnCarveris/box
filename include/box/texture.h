/**
 * @file box/texture.h
 * @brief Texture components.
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

/** Environment variable that names the folder of the encoded texture cache */
#define BOX_TEXTURE_CACHE_ENV "BOX_TEXTURE_CACHE"

/** Texture */
ECS_STRUCT(Texture, {
    int32_t width;  /**< Width in pixels */
    int32_t height; /**< Height in pixels */
});

/** Texture loaded from an image file (populates Texture) */
ECS_STRUCT(TexturePath, {
    char *path;     /**< Image file, relative to the working directory */
    int32_t width;  /**< Requested width in pixels, 0 for source width. */
    int32_t height; /**< Requested height in pixels, 0 for source height. */
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
