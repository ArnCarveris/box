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

/** Froxel grid the renderer assigns point and spot lights to, a singleton */
ECS_STRUCT(ClusterSettings, {
    int32_t x;          /**< Clusters across the view, default 16 */
    int32_t y;          /**< Clusters down the view, default 9 */
    int32_t z;          /**< Depth slices, default 24 */
    int32_t max_lights; /**< Lights stored per cluster, default 64 */
});

/** Default clusters across the view */
#define BOX_CLUSTER_X (16)

/** Default clusters down the view */
#define BOX_CLUSTER_Y (9)

/** Default depth slices of the cluster grid */
#define BOX_CLUSTER_Z (24)

/** Default lights stored per cluster */
#define BOX_CLUSTER_MAX_LIGHTS (64)

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
