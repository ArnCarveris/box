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

/** Environment light that lights the scene from every direction, a singleton
 *
 * The source is reserved for the cubemap texture or sky entity that later
 * epics render the environment from; it must be 0 while the environment is the
 * sky and ground gradient of the two colors.
 */
ECS_STRUCT(EnvironmentLight, {
    rgb sky_color;       /**< Color of the upper hemisphere */
    rgb ground_color;    /**< Color of the lower hemisphere */
    float intensity;     /**< Radiance scale of both colors, zero takes 1 */
    ecs_entity_t source; /**< Reserved for a cubemap or sky entity, 0 for now */
});

/** GPU bake of static lights and emissive surfaces into per instance probes, a singleton */
ECS_STRUCT(BakeSettings, {
    bool enabled;          /**< Static instances read baked probes instead of the static lights */
    int32_t budget;        /**< Probes rebaked per frame, zero takes the default */
    float probe_spacing;   /**< World units between two probes, zero takes the default */
    int32_t probe_max;     /**< Probes an instance gets on one axis, zero takes the default */
    int32_t x;             /**< Occupancy voxels across the scene, zero takes the default */
    int32_t y;             /**< Occupancy voxels up the scene, zero takes the default */
    int32_t z;             /**< Occupancy voxels through the scene, zero takes the default */
    int32_t steps;         /**< Ray march samples per visibility test, zero takes the default */
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

/** Default probes rebaked per frame */
#define BOX_BAKE_BUDGET (16384)

/** Default world units between two probes of an instance */
#define BOX_BAKE_SPACING (1.0f)

/** Default probes an instance gets on one axis */
#define BOX_BAKE_PROBE_MAX (32)

/** Default occupancy voxels across the scene */
#define BOX_BAKE_GRID_X (128)

/** Default occupancy voxels up the scene */
#define BOX_BAKE_GRID_Y (32)

/** Default occupancy voxels through the scene */
#define BOX_BAKE_GRID_Z (128)

/** Default ray march samples per visibility test */
#define BOX_BAKE_STEPS (32)

/** Sky color channel of the environment light a world without one gets */
#define BOX_ENV_SKY (255)

/** Ground color channel of the environment light a world without one gets */
#define BOX_ENV_GROUND (128)

/** Intensity of the environment light a world without one gets */
#define BOX_ENV_INTENSITY (0.25f)

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
