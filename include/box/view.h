/**
 * @file box/view.h
 * @brief View and render target components: what a frame draws and where.
 *
 * LodSettings follows it too and holds the screen space error thresholds the
 * LOD selection of the view measures against, with a more aggressive threshold
 * for its shadow passes.
 *
 * A layer is any entity an entity or a light joins with the pair (Layer, layer).
 * A view lists the layers it shows and the layers that cast its shadows; both
 * lists hold at most BOX_VIEW_LAYER_MAX entries, end at the first zero and show
 * every layer when they are empty. An entity in no layer is drawn by every view,
 * unless the view sets strict_layers and lists layers, which draws its members
 * only.
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

ECS_STRUCT(ShadowSettings, {
    bool enabled;
    int32_t cascades;    /**< Cascades of the shadow map, default 4, at most 4 */
    int32_t resolution;  /**< Edge of one cascade in texels, default 2048 */
    float max_distance;  /**< View distance the shadows reach, default 200 */
    float split_lambda;  /**< 0 splits uniformly, 1 logarithmically, default 0.7 */
    float depth_bias;    /**< Depth offset of the comparison in texels, default 2 */
    float normal_offset; /**< Offset along the geometric normal in texels, default 1.5 */
    int32_t filter_size; /**< PCF taps per axis, default 3, at most 7 */
    float fade;          /**< Fraction of the last cascade that fades out, default 0.2 */
});

ECS_STRUCT(BakeSettings, {
    bool enabled;
    int32_t budget;
    float probe_spacing;
    int32_t probe_max;
    int32_t x;
    int32_t y;
    int32_t z;
    int32_t steps;
});

ECS_STRUCT(ViewEffect, {
    ecs_entity_t effect;
    bool enabled;
});

ECS_STRUCT(ViewMaterialLayer, {
    ecs_entity_t layer;
    bool enabled;
});

ECS_STRUCT(CullSettings, {
    bool enabled;
    int32_t gpu_threshold;
    bool occlusion;
    int32_t hiz_levels;
    float min_dimension_px;
});

ECS_STRUCT(View, {
    ecs_entity_t camera;  /**< Camera the view renders, a view without one is skipped */
    ecs_entity_t target;  /**< RenderTarget entity, 0 renders into the surface */
    ecs_entity_t layers[8];        /**< Layers the view shows, empty shows every layer */
    ecs_entity_t shadow_layers[8]; /**< Layers that cast its shadows, empty casts from all */
    ShadowSettings shadows;
    BakeSettings bake;
    CullSettings culling;
    ecs_vec(ViewEffect) effects;
    ecs_vec(ViewMaterialLayer) material_layers;
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
 * A level of a node may not project an error larger than error_px, which
 * quality scales: a view of quality 2 allows half of it and a view of quality
 * 0.5 twice as much. Shadow passes measure against shadow_error_px, which is
 * larger because a coarser caster hardly shows in a shadow. hysteresis is the
 * fraction of the threshold the band around it is wide, so a node keeps its
 * level until its error leaves that band. A value of zero takes the default of
 * every field, and the flag is true through the constructor, so a view that
 * needs one field adds the component and writes that field.
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
