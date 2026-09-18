/**
 * @file transform/transform_internal.h
 * @brief Private helpers of the box.transform module.
 */

#ifndef BOX_TRANSFORM_INTERNAL_H
#define BOX_TRANSFORM_INTERNAL_H

#include <box.h>

/** Compose a local matrix from a pose.
 *
 * @param p Position, NULL for the origin
 * @param r Rotation, NULL and no orientation for no rotation
 * @param s Scale, NULL for one
 * @param o Orientation, wins over the rotation when set
 * @return The local transform
 */
mat4 box_transform_compose(
    const Position *p,
    const Rotation *r,
    const Scale *s,
    const Orientation *o);

/** Register the observers that update static world transforms.
 *
 * @param world The world
 */
void box_transform_static_init(
    ecs_world_t *world);

/** Register the system that turns LookAt into a Rotation.
 *
 * @param world The world
 */
void box_transform_lookat_init(
    ecs_world_t *world);

/** Register the system that updates dynamic world transforms.
 *
 * @param world The world
 */
void box_transform_dynamic_init(
    ecs_world_t *world);

#endif
