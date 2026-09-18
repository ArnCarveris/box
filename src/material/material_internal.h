/**
 * @file material/material_internal.h
 * @brief Private helpers of the box.material module.
 */

#ifndef BOX_MATERIAL_INTERNAL_H
#define BOX_MATERIAL_INTERNAL_H

#include <box.h>

/** Register the observers that resolve Tint into ActualTint.
 *
 * @param world The world
 */
void box_tint_init(
    ecs_world_t *world);

/** Create the built in layer entities under box.material.layers.
 *
 * @param world The world
 */
void box_layers_init(
    ecs_world_t *world);

#endif
