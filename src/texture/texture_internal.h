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
    ecs_entity_t source;   /**< Component that owns the texture, Texture or Svg */
    bool conflict;         /**< The rejected second source component was logged */
} TextureImpl;

/** Materials that bind a texture entity in one of their PbrTextures slots */
typedef struct TextureRefs {
    ecs_map_t map;  /**< ecs_vec_t of ecs_entity_t, keyed by texture entity */
} TextureRefs;

extern ECS_COMPONENT_DECLARE(TextureImpl);
extern ECS_COMPONENT_DECLARE(TextureRefs);

/** Scale an RGBA8 image into the mip chain of the size class it belongs to.
 *
 * @param src RGBA8 source pixels
 * @param width Width of the source image
 * @param height Height of the source image
 * @param request Requested edge length in pixels, 0 keeps the source size
 * @param cap Largest edge length the surface allows
 * @param out The image to fill, its pixels are owned by the caller
 */
void box_texture_image(
    const uint8_t *src,
    int32_t width,
    int32_t height,
    int32_t request,
    uint32_t cap,
    box_image_t *out);

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

/** Read the texture settings of the surface, or the engine defaults.
 *
 * @param world The world
 * @param cap Receives the largest edge length a texture is scaled to
 * @param keep Receives whether the loaded pixels stay in memory
 */
void box_texture_settings(
    ecs_world_t *world,
    uint32_t *cap,
    bool *keep);

/** Hand an image to the renderer and point the RenderTexture of an entity at it.
 *
 * The pixels of the image move to the renderer. A handle of the same size class
 * is replaced in place, one of another class is destroyed first.
 *
 * @param world The world
 * @param e The texture entity
 * @param img The image, its pixels are consumed
 * @param keep Keep a copy of level 0 in the TextureImpl of the entity
 */
void box_texture_publish(
    ecs_world_t *world,
    ecs_entity_t e,
    const box_image_t *img,
    bool keep);

/** Claim the texture of an entity for one source component.
 *
 * The first of Texture and Svg to be set on an entity owns its texture; the
 * second one is rejected and logged once.
 *
 * @param world The world
 * @param e The texture entity
 * @param id The component id of the source
 * @return true when the source owns the texture of the entity
 */
bool box_texture_claim(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t id);

/** Release the texture of an entity whose source component was removed.
 *
 * A source that never owned the texture leaves the entity alone, so removing a
 * rejected component does not unload the texture of the winning one.
 *
 * @param world The world
 * @param stage The stage of the observer, structural changes go through it
 * @param e The texture entity
 * @param id The component id of the source
 * @param deleting The entity is being deleted, its components go anyway
 */
void box_texture_release(
    ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t e,
    ecs_entity_t id,
    bool deleting);

/** Register the observers that rasterize an Svg component.
 *
 * @param world The world
 */
void box_svg_init(
    ecs_world_t *world);

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
