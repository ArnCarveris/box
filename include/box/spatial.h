/**
 * @file box/spatial.h
 * @brief Spatial index per scene over the world bounds of shapes.
 *
 * Static shapes are placed in a bounding volume hierarchy once and again on change, shapes
 * with DynamicTransform every frame; ray casts and overlap queries are scoped to one scene.
 */

#ifndef BOX_SPATIAL_H
#define BOX_SPATIAL_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Register the box.spatial module.
 *
 * @param world The world
 */
void BoxSpatialImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#endif
