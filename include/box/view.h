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

/** Light settings for a view */
ECS_STRUCT(LightSettings, {
    bool enabled;           /**< Enable lighting for the view */
});

/** Shadow settings of a view */
ECS_STRUCT(ShadowSettings, {
    bool enabled;        /**< The view renders shadows */
    int32_t cascades;    /**< Cascades of the shadow map, default 4, at most 4 */
    int32_t resolution;  /**< Edge of one cascade in texels, default 2048 */
    float max_distance;  /**< View distance the shadows reach, default 200 */
    float split_lambda;  /**< 0 splits uniformly, 1 logarithmically, default 0.7 */
    float depth_bias;    /**< Depth offset of the comparison in texels, default 2 */
    float normal_offset; /**< Offset along the geometric normal in texels, default 1.5 */
    int32_t filter_size; /**< PCF taps per axis, default 3, at most 7 */
    float fade;          /**< Fraction of the last cascade that fades out, default 0.2 */
});

/** Culling settings of a view, all zero culls with occlusion and the defaults */
ECS_STRUCT(CullSettings, {
    bool enabled;           /**< Cull the instances of the view */
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
 * Allows for having different scenes in the same world. A view will only render
 * the entities that are in the same scene as the configured camera. 
 *
 * Entities in a subtree without a Scene component will be rendered by the 
 * default scene.
 */
ECS_STRUCT(Scene, {
    int32_t dummy_;
});

void BoxViewImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
