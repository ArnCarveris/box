/**
 * @file box/view.h
 * @brief View and render target components: what a frame draws and where.
 *
 * LodSettings follows it too and holds the screen space error thresholds the
 * LOD selection of the view measures against, with a more aggressive threshold
 * for its shadow passes.
 *
 * Entities join a layer with (Layer, layer). A view lists up to
 * BOX_VIEW_LAYER_MAX shown and shadow layers, zero terminated, empty for all.
 * Entities in no layer are drawn by every view without strict_layers.
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

/** Offscreen color and depth texture a view can render into */
ECS_STRUCT(RenderTarget, {
    int32_t width;   /**< Width in pixels */
    int32_t height;  /**< Height in pixels */
});

/** Cascaded shadow map settings of a view, a member that is zero takes its default */
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

/** Baked lighting settings, read from the primary view, zero takes the default */
ECS_STRUCT(BakeSettings, {
    bool enabled;            /**< Static instances read baked probes, not the static lights */
    int32_t budget;          /**< Probes rebaked per frame, default BOX_BAKE_BUDGET */
    float probe_spacing;     /**< World units between two probes of an instance, default 1 */
    float occluder_min_size; /**< Side two axes of a shape need to occlude, default 0.5 */
    int32_t probe_max;       /**< Probes an instance gets on one axis, default 32 */
    int32_t x;               /**< Occupancy voxels across the scene, default 128 */
    int32_t y;               /**< Occupancy voxels up the scene, default 32 */
    int32_t z;               /**< Occupancy voxels through the scene, default 128 */
    int32_t steps;           /**< Ray march samples per visibility test, default 32 */
});

/** Post process effect in the chain of a view */
ECS_STRUCT(ViewEffect, {
    ecs_entity_t effect; /**< Effect entity, 0 is skipped */
    bool enabled;        /**< The effect runs in this view */
});

/** Material layer a view draws */
ECS_STRUCT(ViewMaterialLayer, {
    ecs_entity_t layer; /**< MaterialLayer entity, 0 is skipped */
    bool enabled;       /**< The layer is drawn in this view */
});

/** Culling settings of a view, all zero culls with occlusion and the defaults */
ECS_STRUCT(CullSettings, {
    bool enabled;           /**< Cull the instances of the view */
    int32_t gpu_threshold;  /**< Static instances past which a batch culls on the GPU */
    bool occlusion;         /**< Cull instances hidden behind the depth pyramid */
    int32_t hiz_levels;     /**< Levels of the depth pyramid, 0 for the whole chain */
    float min_dimension_px; /**< Projected size in pixels below which an instance is culled */
});

/** What a frame draws: a camera, the target it draws into and its settings.
 *
 * A view with an occluder starts from the depth that view left, so its
 * instances sit in the scene of the occluder, and what that depth hides is
 * drawn first at half opacity. The occluder renders before it, at its size.
 */
ECS_STRUCT(View, {
    ecs_entity_t camera;  /**< Camera the view renders, a view without one is skipped */
    ecs_entity_t target;  /**< RenderTarget entity, 0 renders into the surface */
    ecs_entity_t occluder; /**< View whose depth hides this one, what it hides shows faded */
    ecs_entity_t layers[8];        /**< Layers the view shows, empty shows every layer */
    ecs_entity_t shadow_layers[8]; /**< Layers that cast its shadows, empty casts from all */
    ShadowSettings shadows; /**< Shadow settings of the view */
    BakeSettings bake;      /**< Baked lighting, used when this is the primary view */
    CullSettings culling;   /**< Culling settings of the view */
    ecs_vec(ViewEffect) effects;                /**< Post process chain, in order */
    ecs_vec(ViewMaterialLayer) material_layers; /**< Material layers the view draws */
    rgb clear;            /**< Color the target is cleared to */
    float clear_alpha;    /**< Alpha the target is cleared to */
    int32_t priority;     /**< Order in the frame, lower renders first */
    bool strict_layers;   /**< A view with layers draws only members, default false */
    bool enabled;         /**< The view is rendered, default true */
});

#define BOX_VIEW_LAYER_MAX (8)
extern ECS_TAG_DECLARE(Layer);

/** Cascades a view splits its shadow map into by default */
#define BOX_SHADOW_CASCADES (4)

/** Edge of one cascade of a shadow map in texels */
#define BOX_SHADOW_RESOLUTION (2048)

/** View distance the shadows of a view reach */
#define BOX_SHADOW_DISTANCE (200.0f)

/** Blend of the logarithmic and the uniform cascade split */
#define BOX_SHADOW_LAMBDA (0.7f)

/** Depth offset of the shadow comparison in texels of the cascade */
#define BOX_SHADOW_BIAS (2.0f)

/** Offset of the shadow lookup along the geometric normal, in texels */
#define BOX_SHADOW_NORMAL (1.5f)

/** PCF taps the shadow comparison takes per axis */
#define BOX_SHADOW_FILTER (3)

/** PCF taps per axis a view can ask for */
#define BOX_SHADOW_FILTER_MAX (7)

/** Fraction of the last cascade the shadows fade out over */
#define BOX_SHADOW_FADE (0.2f)

/** Static instances a batch needs before the renderer culls it in a compute pass */
#define BOX_CULL_GPU_THRESHOLD (2048)

/** Levels of the depth pyramid of a view, zero being the whole chain down to one texel */
#define BOX_CULL_HIZ_LEVELS (0)

/** Screen space error settings of a view, with the singleton as its default.
 *
 * A level may not project an error above error_px divided by quality; shadow
 * passes use shadow_error_px. A node keeps its level until its error leaves the
 * hysteresis band. Zero fields take their default; enabled defaults to true.
 */
ECS_STRUCT(LodSettings, {
    bool enabled;          /**< The view selects levels by projected error, default true */
    float quality;         /**< Scales the thresholds of the view, default 1 */
    float error_px;        /**< Projected error in pixels a level may not exceed, default 2 */
    float shadow_error_px; /**< The same threshold for its shadow passes, default 6 */
    float hysteresis;      /**< Band around the threshold as a fraction, default 0.25 */
});

/** Quality a view scales its LOD error thresholds with */
#define BOX_VIEW_LOD_QUALITY (1.0f)

/** Projected error in pixels a level of a node may not exceed */
#define BOX_VIEW_LOD_ERROR (2.0f)

/** Projected error in pixels a level may not exceed in a shadow pass */
#define BOX_VIEW_LOD_SHADOW_ERROR (6.0f)

/** Fraction of the threshold the band a level switch has to cross is wide */
#define BOX_VIEW_LOD_HYSTERESIS (0.25f)

/** Red channel of the clear color a view starts out with */
#define BOX_VIEW_CLEAR_R (39)

/** Green channel of the clear color a view starts out with */
#define BOX_VIEW_CLEAR_G (39)

/** Blue channel of the clear color a view starts out with */
#define BOX_VIEW_CLEAR_B (48)

/** Register the box.view module.
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
