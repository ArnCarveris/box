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

/** Sun or moon light, the radiance it has before the atmosphere absorbs it
 *
 * A directional light with this component is a body over the medium: the sky attenuates its
 * color and intensity by the air transmittance to the camera, so a low sun lights the scene red.
 * The moon and the star field carry it too.
 */
ECS_STRUCT(CelestialLight, {
    float toa_intensity; /**< Radiance over the atmosphere, 0 takes the DirectionalLight */
    rgb toa_color;       /**< Color over the atmosphere, black takes white */
});

/** Overrides whether an instance blocks the light of the bake, inherited by instances
 *
 * Without it a Box shape with two sides of at least the occluder_min_size of the
 * bake occludes and any other mesh does not.
 */
ECS_STRUCT(BakeOccluder, {
    bool enabled; /**< The instance occludes the bake, false keeps it out of it */
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

/** Plugin that scales the direct light of a light at every shaded point
 *
 * The shader is a `Shader` entity whose WGSL file defines `fn box_light_modulate(p: vec3<f32>,
 * n: vec3<f32>, l: vec3<f32>, time: f32, params: LightModulatorParams) -> vec3<f32>`, a color
 * multiplier applied to the direct light after its shadow, never to indirect light.
 *
 * The modulator sits on the light or a child of it; several modulators multiply. Its parameters
 * are a component next to it registered with box_light_modulator_params. A light without one
 * compiles the plain shader.
 */
ECS_STRUCT(LightModulator, {
    ecs_entity_t shader; /**< Shader entity of the plugin, without one it is skipped */
    ecs_entity_t params; /**< Parameter component, 0 finds the registered one it carries */
    bool enabled;        /**< The modulator runs, default true */
});

#define BOX_LIGHT_MODULATORS_BUILTINS "etc/box/light_modulators.flecs"

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

/** Added to a component entity to let it fill the parameter block of a modulator.
 *
 * The engine reads the reflection of the component next to a LightModulator and writes its
 * floats, in member order, into the sixteen float block the modulate function receives.
 */
extern ECS_TAG_DECLARE(LightModulatorParams);

/** Register a component as the parameter block of a light modulator plugin.
 *
 * @param world The world
 * @param component The component entity
 */
void box_light_modulator_params(
    ecs_world_t *world,
    ecs_entity_t component);

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
