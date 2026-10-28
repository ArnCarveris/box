/**
* @file box/view.h
* @brief View and render target components: what a frame draws and where.
*/

#ifndef BOX_VIEW_H
#define BOX_VIEW_H

#include "types.h"

#ifdef BOX_VIEW_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Clear settings for a view */
ECS_STRUCT(ClearSettings, {
    bool enabled;           /**< Clear view */
    rgba color;             /**< Clear color */
});

/** The environment a view lights its scene with: an entity with Environment, such as an
 * EnvironmentImage */
ECS_STRUCT(EnvironmentSettings, {
    ecs_entity_t source;    /**< Entity with Environment, 0 for no environment */
    float intensity;        /**< Scale of the environment light, View ctor sets 1 */
    float yaw;              /**< Rotation of the environment about +Y in radians, as
                             *   Rotation.y */
    bool background;        /**< Draw the environment behind the geometry in place of the
                             *   clear color */
});

/** Light settings for a view */
ECS_STRUCT(LightSettings, {
    bool enabled;           /**< Enable lighting for the view. A lit view whose camera has a
                             *   depth range draws its opaque and alpha tested 3D meshes
                             *   into the depth buffer first (a depth prepass, two for a
                             *   view that culls by occlusion), then shades them once per
                             *   pixel with an Equal depth test; an unlit view shades every
                             *   opaque fragment that passes the depth test as it draws */
    bool baked;             /**< Sample the baked lighting of the scene, View ctor sets true */
    bool reflections;       /**< Sample the reflection probes of the scene, View ctor sets
                             *   true */
    bool atmosphere;        /**< Render the Atmosphere of the scene: the sky behind the
                             *   geometry and the aerial perspective of the 3D meshes, View
                             *   ctor sets true */
    EnvironmentSettings environment; /**< Environment the view is lit with, used only when
                                      *   the view is lit */
    ecs_entity_t scene;
});

/** Shadow settings of a view */
ECS_STRUCT(ShadowSettings, {
    bool enabled;        /**< The view renders shadows */
    int32_t cascades;    /**< Cascades of the shadow map, default 4, at most 4 */
    int32_t resolution;  /**< Edge of one cascade in texels, default 2048 */
    float max_distance;  /**< View distance the shadows reach, default 200 */
    float split_lambda;  /**< 0 splits uniformly, 1 logarithmically, default 0.7 */
    float depth_bias;    /**< Depth offset of the comparison in texels of the cascade, default 2 */
    float normal_offset; /**< Offset along the geometric normal (before the normal map) in
                          *   texels of the cascade times 1 - N.L: none facing the light, all
                          *   of it at grazing light; default 2 */
    int32_t filter_size; /**< Tent filter width in texels: 1 is one bilinear comparison (2x2
                          *   PCF), 3 four, 5 nine, 7 sixteen; even values round up to the next
                          *   odd one; default 3, 1 to 7 */
    float fade;          /**< Fraction of the last cascade that fades out, default 0.2 */
    float blend;         /**< Fraction of the half extent of a cascade at the edge of its box
                          *   that blends into the next cascade, 0 for hard seams, the last one
                          *   fades instead; default 0.1, 0 to 0.5 */
});

/** Culling settings of a view, applied as specified: all zero draws every instance.
 *
 * Occlusion tests against what the view drew first in the same frame, so an instance that comes
 * into view is drawn at once however fast the camera moves. Cascades cull by frustum only.
 */
ECS_STRUCT(CullSettings, {
    bool enabled;           /**< Cull by frustum and size, false draws every instance */
    bool occlusion;         /**< Also cull instances hidden behind what the view drew */
    float min_dimension_px; /**< Projected size in pixels below which an instance is culled */
});

/** Element in view effects array. */
ECS_STRUCT(ViewEffect, {
    bool enabled;           /**< Whether effect is enabled. */
    ecs_entity_t effect;    /**< Entity with effect settings. */
});

/** What a frame draws: a camera, the target it draws into and its settings. */
ECS_STRUCT(View, {
    ecs_entity_t camera;            /**< Camera the view renders, a view without one is skipped */
    ClearSettings clear;            /**< Clear settings */
    LightSettings lighting;         /**< Lighting settings */
    ShadowSettings shadows;         /**< Shadow settings */
    CullSettings culling;           /**< Cull settings */
    ecs_vec(ViewEffect) effects;    /**< Post process effects */
});

/** Component added to scene root.
 * A view renders only the scene of its camera, lit by the lights of that scene.
 *
 * An entity belongs to its nearest ancestor with Scene (itself included), so
 * scenes nest; entities outside any Scene belong to the default scene.
 */
ECS_STRUCT(Scene, {
    int32_t dummy_; /**< Unused, gives the component a size */
});

/** Register the box.view module.
 *
 * View gets a constructor that enables lighting, baked lighting, reflections and the
 * atmosphere, sets the environment intensity to 1 and the ShadowSettings defaults with shadows
 * disabled and enables culling with occlusion and a smallest size of one pixel.
 *
 * A C literal View starts from zero instead: unlit, drawing every instance.
 *
 * @param world The world
 */
void BoxViewImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
