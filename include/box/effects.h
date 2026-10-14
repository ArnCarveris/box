/**
 * @file box/effects.h
 * @brief Post process effect settings, a view lists the effect entities it applies.
 *
 * An effect entity is added to the effects of a View, which renders into an HDR target and
 * runs its effects in order. Constructors set the defaults; "0 too" marks a field that also
 * takes its default when zero, so a C literal that leaves it out still works.
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
    float intensity;                     /**< Blend into the image, default 0.3, 0 too */
    float low_frequency_boost;           /**< Extra weight of the wide mips, default 0.7 */
    float low_frequency_boost_curvature; /**< Falloff of the boost across mips, default 0.95 */
    float high_pass_frequency;           /**< Fraction of mips that add, default 1, 0 too */
    float threshold;                     /**< Luminance above which pixels bloom, default 1 */
    float threshold_softness;            /**< Width of the knee around the threshold, default 0 */
    float firefly_clamp;                 /**< Clamp against fireflies, default 12, 0 off */
    float temporal;                      /**< Blend with last frame, default 0, max 0.95 */
    int32_t max_mip_dimension;           /**< Longest edge of the first mip, default 512, 0 too */
});

/** Screen space ambient occlusion from the depth of the view, with normals derived from it */
ECS_STRUCT(Ssao, {
    float radius;    /**< Sample radius in world units, default 0.5, 0 too */
    float bias;      /**< Depth offset against self occlusion, default 0.025 */
    float intensity; /**< Strength of the occlusion, default 1, 0 too */
});

/** Exponential fog that thins out with height, lit by the sun and ambient light of the scene */
ECS_STRUCT(HeightFog, {
    float density;           /**< Fog density at the base height, default 0.1, 0 too */
    float falloff;           /**< Rate at which density drops with height, default 0.3 */
    float base_height;       /**< Height of full density, default 0 */
    float max_opacity;       /**< Upper bound of the fog opacity, default 1, 0 too */
    rgb color;               /**< Fog color, black uses 191 158 140 */
    float sun_inscatter;     /**< Scattered directional light, default 1 */
    float ambient_inscatter; /**< Scattered ambient light, default 1 */
});

/** Exposure that adapts to the brightness of the image.
 *
 * A Tonemap refers to it to scale its input, or it is an effect of its own that scales the
 * image. The exposure starts at 0 stops and adapts every frame by the frame delta, so a
 * headless run with a fixed delta adapts the same way every time.
 */
ECS_STRUCT(AutoExposure, {
    float target_brightness;     /**< Brightness the exposure converges to, 0 uses the range */
    float min_brightness;        /**< Low end of the accepted brightness, default 0.1, 0 too */
    float max_brightness;        /**< High end of the accepted brightness, default 0.3, 0 too */
    float min_log_luma;          /**< Lowest log2 luminance of the histogram, default -8 */
    float max_log_luma;          /**< Highest log2 luminance, default 4 (-8..4 if <= min) */
    float speed_up;              /**< Adaptation rate toward brighter, default 3, 0 too */
    float speed_down;            /**< Adaptation rate toward darker, default 1, 0 too */
    float low_percentile;        /**< Histogram fraction ignored at the dark end, default 0.5 */
    float high_percentile;       /**< Fraction below the bright end, default 0.95, 0 too */
    float exposure_compensation; /**< Exposure offset in stops, default 0 */
    float min_exposure;          /**< Lower clamp of the exposure, 0 is unbounded */
    float max_exposure;          /**< Upper clamp of the exposure, 0 is unbounded */
});

/** Maps HDR color to the display range with the Khronos PBR Neutral curve */
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
