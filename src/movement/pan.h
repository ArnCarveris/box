/**
 * @file movement/pan.h
 * @brief The system that pans the Rotation of Pan entities.
 */

#ifndef BOX_MOVEMENT_PAN_H
#define BOX_MOVEMENT_PAN_H

#include <box.h>

/** Register the Pan system of box.movement.
 *
 * @param world The world, inside the scope of the module
 */
void box_movement_pan_import(
    ecs_world_t *world);

#endif
