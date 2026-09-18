/**
 * @file camera/camera_internal.h
 * @brief Private helpers of the box.camera module.
 */

#ifndef BOX_CAMERA_INTERNAL_H
#define BOX_CAMERA_INTERNAL_H

#include <box.h>

/** Register the billboard system.
 *
 * @param world The world
 */
void box_billboard_init(
    ecs_world_t *world);

#endif
