/**
 * @file box/effects.h
 * @brief Post process effect settings, a view lists the effect entities it applies.
 *
 * An effect entity holds one of the settings components below and is added to
 * the effects of a View. Zero fields take engine defaults.
 */

#ifndef BOX_EFFECTS_H
#define BOX_EFFECTS_H

#include "types.h"

#ifdef BOX_EFFECTS_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Glow around pixels brighter than the threshold */
ECS_STRUCT(Bloom, {
    float intensity;                     /**< Blend of the bloom into the image, default 0.3 */
    float low_frequency_boost;           /**< Extra weight of the wide mips, default 0.7 */
    float low_frequency_boost_curvature; /**< Falloff of the boost across mips, default 0.95 */
    float high_pass_frequency;           /**< Fraction of the mips that contribute, default 1 */
    float threshold;                     /**< Luminance above which pixels bloom, default 1 */
    float threshold_softness;            /**< Width of the knee around the threshold, default 0 */
    float firefly_clamp;                 /**< Luminance clamp against fireflies, default 12 */
    float temporal;                      /**< Blend with the previous frame, default 0 */
    int32_t max_mip_dimension;           /**< Edge of the first mip in pixels, default 512 */
});

/** Screen space ambient occlusion */
ECS_STRUCT(Ssao, {
    float radius;    /**< Sample radius in world units, default 0.5 */
    float bias;      /**< Depth offset against self occlusion, default 0.025 */
    float intensity; /**< Strength of the occlusion, default 1 */
});

/** Exponential fog that thins out with height */
ECS_STRUCT(HeightFog, {
    float density;           /**< Fog density at the base height, default 0.1 */
    float falloff;           /**< Rate at which density drops with height, default 0.3 */
    float base_height;       /**< Height of full density, default 0 */
    float max_opacity;       /**< Upper bound of the fog opacity, default 1 */
    rgb color;               /**< Fog color, black uses 191 158 140 */
    float sun_inscatter;     /**< Scattered directional light, default 1 */
    float ambient_inscatter; /**< Scattered ambient light, default 1 */
});

/** Exposure that adapts to the brightness of the image */
ECS_STRUCT(AutoExposure, {
    float target_brightness;     /**< Brightness the exposure converges to, 0 uses the range */
    float min_brightness;        /**< Lower end of the accepted brightness, default 0.1 */
    float max_brightness;        /**< Upper end of the accepted brightness, default 0.3 */
    float min_log_luma;          /**< Lowest log2 luminance of the histogram, default -8 */
    float max_log_luma;          /**< Highest log2 luminance of the histogram, default 4 */
    float speed_up;              /**< Adaptation speed toward brighter, default 3 */
    float speed_down;            /**< Adaptation speed toward darker, default 1 */
    float low_percentile;        /**< Histogram fraction ignored at the dark end, default 0.5 */
    float high_percentile;       /**< Histogram fraction kept below the bright end, default 0.95 */
    float exposure_compensation; /**< Exposure offset in stops, default 0 */
    float min_exposure;          /**< Lower clamp of the exposure, 0 is unbounded */
    float max_exposure;          /**< Upper clamp of the exposure, 0 is unbounded */
});

/** Maps HDR color to the display range */
ECS_STRUCT(Tonemap, {
    ecs_entity_t auto_exposure; /**< Entity with AutoExposure that scales the input, 0 is none */
});

/** Register the box.effects module.
 *
 * @param world The world
 */
void BoxEffectsImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
