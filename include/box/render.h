/**
 * @file box/render.h
 * @brief Render module: mesh handles and the Extract system that fills frame packets.
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

/** Mesh and local scale that render an entity, written by shape modules */
ECS_STRUCT(RenderMesh, {
    uint32_t handle;  /**< Renderer mesh handle */
    vec3 scale;       /**< Scale applied before the world transform */
    vec3 bounds_min;  /**< Local bounds minimum of the unscaled mesh */
    vec3 bounds_max;  /**< Local bounds maximum of the unscaled mesh */
});

/** Texture handle an image entity resolved to, written by texture modules */
ECS_STRUCT(RenderTexture, {
    uint32_t handle;  /**< Renderer texture handle, its size class in the high byte */
    int32_t width;    /**< Width in pixels of the uploaded texture */
    int32_t height;   /**< Height in pixels of the uploaded texture */
});

/** Takes an entity and everything below it out of every view, pick and query.
 *
 * The tag is a switch, not a change of the scene: the entity keeps its
 * components, its place in the hierarchy and its children, and only stops
 * being drawn, picked and collided with. The subtree of a hidden entity is
 * hidden with it, and a hidden prefab hands the tag to its instances.
 */
extern ECS_TAG_DECLARE(Hidden);

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
