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

/** Baked indirect lighting of a scene, placed on a Scene root or as a singleton for the default
 * scene. Adding it adds LightBakeStatus.
 *
 * A bake is bounces + 1 passes over every probe: the first captures direct light and emission,
 * every later pass adds a bounce by shading its captures with the probes of the pass before it.
 * Views show the last complete pass while the next one bakes. Changing bounces bakes again.
 *
 * Values apply as specified: bounces 0 bakes direct emission and light only, budget_ms 0 bakes
 * nothing, a capture_size outside 1 to 256 or bounces outside 0 to 16 bakes nothing and sets
 * LightBakeStatus.error.
 */
ECS_STRUCT(LightBake, {
    float probe_spacing; /**< World units between probes, default 2 */
    int32_t bounces;     /**< Light bounces to bake, 0 to 16, default 2 */
    int32_t capture_size; /**< Face edge of a probe capture in texels, 1 to 256, default 16 */
    float budget_ms;     /**< Render time per frame spent baking in milliseconds, default 2 */
    float bounds_padding; /**< World units added around the static bounds, default 1 */
    char *cache;         /**< Cache folder path, default ".box/bake", empty for none */
});

/** Progress of the bake of a scene, written by the engine next to LightBake */
ECS_STRUCT(LightBakeStatus, {
    int32_t probes;      /**< Probes of the volume */
    int32_t baked;       /**< Probes baked in the pass being baked, or in the last when converged */
    int32_t pass;        /**< Completed capture passes: the first bakes direct light and emission,
                          *   each later one a bounce; bounces + 1 when converged */
    bool converged;      /**< All bounces + 1 passes are baked, pass equals bounces + 1 */
    bool cache_hit;      /**< The volume was loaded from the cache */
    char *error;         /**< Why nothing is baked, such as an invalid setting, NULL for none */
});

/** Keeps a static shape out of the bake captures */
extern ECS_TAG_DECLARE(NoBake);

/** Register the box.light module.
 *
 * The first directional light of a scene casts the shadows of the views that enable them;
 * NoShadow shapes and blended shapes cast none.
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
