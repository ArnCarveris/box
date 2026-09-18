/**
 * @file box/light.h
 * @brief Light components; color comes from the entity's Rgb and direction from its transform.
 */

#ifndef BOX_LIGHT_H
#define BOX_LIGHT_H

#include "types.h"

#ifdef BOX_LIGHT_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Directional light travelling along the entity's local +Z */
ECS_STRUCT(DirectionalLight, {
    float intensity; /**< Radiance scale */
});

/** Omnidirectional light */
ECS_STRUCT(PointLight, {
    float intensity; /**< Radiance scale */
    float range;     /**< Distance at which the light reaches zero */
});

/** Cone light along the entity's local +Z, angles are half angles in degrees */
ECS_STRUCT(SpotLight, {
    float intensity;   /**< Radiance scale */
    float range;       /**< Distance at which the light reaches zero */
    float inner_angle; /**< Full intensity cone half angle */
    float outer_angle; /**< Zero intensity cone half angle */
});

/** Register the box.light module.
 *
 * @param world The world
 */
void BoxLightImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
