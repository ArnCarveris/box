/**
 * @file box/draw.h
 * @brief Immediate mode drawing: an instance of a prefab or template drawn this frame.
 *
 * box_draw draws an asset at a transform for the frame it is called in. The
 * instance behind a key keeps its identity across frames; one not drawn again
 * is hidden and, after a while, deleted.
 *
 * An overlay draw goes to a layer a dedicated view draws unlit over the scene
 * with the camera of the primary view, faded where the scene hides it. An
 * overlay draw on top goes to a second layer whose view the scene never hides.
 */

#ifndef BOX_DRAW_H
#define BOX_DRAW_H

#include "types.h"

#ifdef BOX_DRAW_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** What an instance of the draw pool was made from, written by the engine */
ECS_STRUCT(DrawInstance, {
    ecs_entity_t asset; /**< Prefab or template the instance was made from */
    uint64_t key;       /**< Identity of the instance across frames */
    int64_t frame;      /**< Frame the instance was drawn in last */
    bool overlay;       /**< The instance is drawn over the scene */
    bool on_top;        /**< The scene never hides the overlay instance */
});

/** One draw call: an asset, its identity and the transform it is drawn at */
typedef struct box_draw_desc_t {
    ecs_entity_t asset; /**< Prefab or template to draw */
    uint64_t key;       /**< Identity across frames, 0 numbers the call in the frame */
    vec3 position;      /**< World position of the instance */
    vec3 rotation;      /**< Euler rotation of the instance in radians */
    vec3 scale;         /**< Scale of the instance, all zero draws at one */
    bool overlay;       /**< Draw over the scene, unlit and without depth against it */
    bool on_top;        /**< With overlay, draw in full where the scene hides the instance */
} box_draw_desc_t;

/** Frames an instance stays in the pool after its last draw before it is deleted */
#define BOX_DRAW_RETIRE_FRAMES (120)

/** Order in the frame the overlay view renders at, after the scene, before the UI */
#define BOX_DRAW_OVERLAY_PRIORITY (900)

/** Order in the frame the overlay view on top renders at, after the overlay, before the UI */
#define BOX_DRAW_TOP_PRIORITY (910)

/** Draw an instance of a prefab or template this frame.
 *
 * The same asset and key draw the same instance every frame, so a caller that
 * draws every frame gets a persistent entity it can set props on. The entity
 * lives under box.draw.instances.
 *
 * @param world The world
 * @param desc The draw call
 * @return The instance entity, 0 when the asset is not a prefab or a template
 */
ecs_entity_t box_draw(
    ecs_world_t *world,
    const box_draw_desc_t *desc);

/** Get the layer the overlay instances are drawn in.
 *
 * The layer is registered with the renderer on the first overlay draw.
 *
 * @param world The world
 * @return The layer entity
 */
ecs_entity_t box_draw_overlay_layer(
    ecs_world_t *world);

/** Get the layer the overlay instances on top are drawn in.
 *
 * The layer is registered with the renderer on the first overlay draw on top.
 *
 * @param world The world
 * @return The layer entity
 */
ecs_entity_t box_draw_top_layer(
    ecs_world_t *world);

/** Register the box.draw module.
 *
 * @param world The world
 */
void BoxDrawImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
