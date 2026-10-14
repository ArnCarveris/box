/**
 * @file box/texture.h
 * @brief Texture components: images loaded from files on the job system.
 *
 * A TexturePath loads a raster image (stb_image) or an SVG (resvg) on a worker thread; the
 * engine then writes Texture and TextureStatus, and PbrTextures (box/material.h) samples it.
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

/** Size of a loaded texture, written by the engine when a load succeeds */
ECS_STRUCT(Texture, {
    int32_t width;  /**< Width in pixels */
    int32_t height; /**< Height in pixels */
});

/** Texture loaded from an image file, which writes Texture and TextureStatus.
 *
 * Raster formats load with stb_image, a file ending in .svg is rendered by resvg at the
 * requested size; with one requested dimension the other keeps the aspect ratio.
 *
 * The largest dimension is capped by Surface.texture_quality. Changing a member loads the
 * image again; the previous image stays in use until the new one is ready.
 */
ECS_STRUCT(TexturePath, {
    char *path;     /**< Image file, relative to the working directory or an ancestor of it */
    int32_t width;  /**< Requested width in pixels, 0 for source width. */
    int32_t height; /**< Requested height in pixels, 0 for source height. */
});

/** Progress of the load of a TexturePath */
ECS_ENUM(TextureState, {
    TextureLoading = 0, /**< A load is in flight */
    TextureReady = 1,   /**< The last load succeeded and Texture holds its size */
    TextureFailed = 2   /**< The last load failed, error says why */
});

/** Outcome of the loads of a TexturePath, written by the engine and read only for applications */
ECS_STRUCT(TextureStatus, {
    TextureState state; /**< Progress of the last requested load */
    int32_t revision;   /**< Loads that succeeded, the image changed when it increments */
    char *error;        /**< Why the last load failed, NULL unless state is TextureFailed */
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
