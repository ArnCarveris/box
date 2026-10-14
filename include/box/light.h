/**
 * @file box/light.h
 * @brief Light components; color comes from the entity's Rgb and direction from its transform.
 *
 * A light lights only its scene (nearest Scene ancestor, else the default scene); a scene
 * without light entities gets a default sun. Intensity 1 and a white Rgb light a facing surface
 * at its base color, View.lighting.enabled false draws base color plus emission unlit.
 *
 * The ambient of a scene sums its AmbientLight components, set on the Scene root (a singleton
 * for the default scene); a scene without one gets the default sky and ground.
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
    float intensity; /**< Radiance scale, default 1, 0 lights nothing */
});

/** Omnidirectional light */
ECS_STRUCT(PointLight, {
    float intensity; /**< Radiance scale, default 1, 0 lights nothing */
    float range;     /**< Distance at which the light reaches zero, default 10 */
});

/** Cone light along the entity's local +Z, angles are half angles in degrees */
ECS_STRUCT(SpotLight, {
    float intensity;   /**< Radiance scale, default 1, 0 lights nothing */
    float range;       /**< Distance at which the light reaches zero, default 10 */
    float inner_angle; /**< Full intensity cone half angle, default 20 */
    float outer_angle; /**< Zero intensity cone half angle, default 30 */
});

/** Hemisphere ambient light of the scene the entity is in, blended by the surface normal */
ECS_STRUCT(AmbientLight, {
    rgb sky;         /**< Display encoded light from above, default {118, 130, 149} */
    rgb ground;      /**< Display encoded light from below, default {79, 74, 68} */
    float intensity; /**< Scale of both colors, default 1 */
});

/** Register the box.light module.
 *
 * The module gives View a constructor that enables lighting, so a view is lit unless it
 * sets lighting.enabled to false.
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
