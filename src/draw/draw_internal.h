/**
 * @file draw/draw_internal.h
 * @brief Private state of the box.draw module: the pool of instances and the overlay.
 */

#ifndef BOX_DRAW_INTERNAL_H
#define BOX_DRAW_INTERNAL_H

#include <box.h>

/** The pool of the immediate mode draws of a world */
typedef struct DrawState {
    ecs_map_t instances;      /**< Instance entity by the hash of asset and key */
    ecs_entity_t scope;       /**< box.draw.instances, parent of every instance */
    ecs_entity_t overlay;     /**< Layer of the overlay draws */
    ecs_entity_t view;        /**< View that draws the overlay layer, 0 until needed */
    int64_t frame;            /**< Frame the last draw call was made in */
    int32_t calls;            /**< Draw calls without a key made in that frame */
} DrawState;

extern ECS_COMPONENT_DECLARE(DrawState);

/** Get the state of the module.
 *
 * @param world The world
 * @return The state, NULL when box.draw is not imported
 */
DrawState* box_draw_state(
    ecs_world_t *world);

#endif
