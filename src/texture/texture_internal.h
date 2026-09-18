/**
 * @file texture/texture_internal.h
 * @brief Private state of the box.texture module: loaded images and the reverse reference map.
 */

#ifndef BOX_TEXTURE_INTERNAL_H
#define BOX_TEXTURE_INTERNAL_H

#include <box.h>
#include "../render/render_internal.h"

/** An image scaled to a texture size class, holding its whole mip chain */
typedef struct box_image_t {
    uint8_t *pixels;       /**< RGBA8 mip chain, level 0 first, malloc'd */
    uint32_t size;         /**< Edge length of level 0, the size of its class */
    uint32_t mips;         /**< Mip levels in the chain */
    int32_t source_width;  /**< Width of the image file */
    int32_t source_height; /**< Height of the image file */
} box_image_t;

/** CPU side state of a loaded texture */
typedef struct TextureImpl {
    uint8_t *pixels;       /**< RGBA8 copy of level 0, NULL unless kept */
    int32_t source_width;  /**< Width of the image file */
    int32_t source_height; /**< Height of the image file */
} TextureImpl;

/** Materials that bind a texture entity in one of their PbrTextures slots */
typedef struct TextureRefs {
    ecs_map_t map;  /**< ecs_vec_t of ecs_entity_t, keyed by texture entity */
} TextureRefs;

extern ECS_COMPONENT_DECLARE(TextureImpl);
extern ECS_COMPONENT_DECLARE(TextureRefs);

/** Load an image file and scale it to the size class it belongs to.
 *
 * @param path The image file, relative to the working directory
 * @param request Requested edge length in pixels, 0 keeps the source size
 * @param cap Largest edge length the surface allows
 * @param out The image to fill, its pixels are owned by the caller
 * @return 0 on success, nonzero when the file could not be read
 */
int box_texture_load(
    const char *path,
    int32_t request,
    uint32_t cap,
    box_image_t *out);

/** Register the observers that keep the reverse reference map.
 *
 * @param world The world
 */
void box_texture_refs_init(
    ecs_world_t *world);

/** Queue every material that binds a texture for re-extraction.
 *
 * @param world The world
 * @param texture The texture entity
 */
void box_texture_refs_mark(
    ecs_world_t *world,
    ecs_entity_t texture);

/** Clear every slot that binds a texture and warn when there were any.
 *
 * @param world The world
 * @param texture The texture entity
 */
void box_texture_refs_unbind(
    ecs_world_t *world,
    ecs_entity_t texture);

#endif
