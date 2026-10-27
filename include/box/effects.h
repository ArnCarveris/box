/**
 * @file box/effects.h
 * @brief Post process effect settings, a view lists the effect entities it applies.
 *
 * An effect entity is added to the effects of a View, which renders into an HDR target and
 * runs its effects in order. Constructors set the defaults; values apply as specified, so a
 * C literal that leaves a field out sets it to zero.
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
    float intensity;                     /**< Blend into the image, default 0.3 */
    float low_frequency_boost;           /**< Extra weight of the wide mips, default 0.7 */
    float low_frequency_boost_curvature; /**< Falloff of the boost across mips, default 0.95 */
    float high_pass_frequency;           /**< Fraction of mips that add, default 1 */
    float threshold;                     /**< Luminance above which pixels bloom, default 1 */
    float threshold_softness;            /**< Width of the knee around the threshold, default 0 */
    float firefly_clamp;                 /**< Clamp against fireflies, default 12, 0 off */
    float temporal;                      /**< Blend with last frame, default 0, max 0.95 */
    int32_t max_mip_dimension;           /**< Longest edge of the first mip, default 512 */
});

/** Screen space ambient occlusion from the depth of the view, with normals derived from it */
ECS_STRUCT(Ssao, {
    float radius;    /**< Sample radius in world units, default 0.5 */
    float bias;      /**< Depth offset against self occlusion, default 0.025 */
    float intensity; /**< Strength of the occlusion, default 1 */
});

/** Exponential fog that thins out with height, lit by the sun and ambient light of the scene.
 *
 * With atmosphere set, a view that renders the Atmosphere of its scene takes the in scatter
 * color of the fog from its sky instead: the ambient is the horizon of the sky mixed a little
 * toward its zenith, the glow the horizon toward the main light (the sun by day, the moon by
 * night) with the Mie phase of the atmosphere. color is then ignored. The fog applies on top of
 * the aerial perspective.
 */
ECS_STRUCT(HeightFog, {
    float density;           /**< Fog density at the base height, default 0.1 */
    float falloff;           /**< Rate at which density drops with height, default 0.3 */
    float base_height;       /**< Height of full density, default 0 */
    float max_opacity;       /**< Upper bound of the fog opacity, default 1 */
    rgb color;               /**< Fog color, default 191 158 140, ignored when the color comes
                              *   from the atmosphere */
    float sun_inscatter;     /**< Scattered directional light, default 1 */
    float ambient_inscatter; /**< Scattered ambient light, default 1 */
    bool atmosphere;         /**< Take the in scatter color from the sky of the Atmosphere of
                              *   the scene when the view renders it, default true */
});

/** Exposure that adapts to the brightness of the image.
 *
 * A Tonemap refers to it to scale its input, or it is an effect of its own that scales the
 * image. The histogram leaves out pixels with alpha 0 and pixels darker than min_log_luma.
 *
 * The first frame of a view starts at the target exposure; later frames adapt toward it by
 * the frame delta, so a headless run with a fixed delta adapts the same way every time.
 */
ECS_STRUCT(AutoExposure, {
    float target_brightness;     /**< Brightness the exposure converges to, 0 uses the range */
    float min_brightness;        /**< Low end of the accepted brightness, default 0.1 */
    float max_brightness;        /**< High end of the accepted brightness, default 0.3 */
    float min_log_luma;          /**< Lowest log2 luminance of the histogram, darker pixels
                                  *   are left out, default -8 */
    float max_log_luma;          /**< Highest log2 luminance of the histogram, default 4 */
    float speed_up;              /**< Adaptation rate toward brighter, default 3 */
    float speed_down;            /**< Adaptation rate toward darker, default 1 */
    float low_percentile;        /**< Histogram fraction ignored at the dark end, default 0.5 */
    float high_percentile;       /**< Fraction below the bright end, default 0.95 */
    float exposure_compensation; /**< Exposure offset in stops, default 0 */
    float min_exposure;          /**< Lower clamp of the exposure, 0 is unbounded */
    float max_exposure;          /**< Upper clamp of the exposure, 0 is unbounded */
});

/** How a Tonemap maps HDR color to the display range */
ECS_ENUM(TonemapOperator, {
    TonemapOperatorPbrNeutral = 0,   /**< Khronos PBR Neutral: colors below a peak of about 0.76
                                      *   keep their value and saturation, brighter ones roll off
                                      *   to white */
    TonemapOperatorTonyMcMapface = 1 /**< Tony McMapface, a 48^3 LUT (etc/luts) that desaturates
                                      *   colors smoothly as they brighten */
});

/** Maps HDR color to the display range, after scaling it by an adapted exposure.
 *
 * The output is linear, the target encodes it. Tony McMapface adds a dither of up to half an
 * 8 bit step against banding.
 */
ECS_STRUCT(Tonemap, {
    ecs_entity_t auto_exposure; /**< Entity with AutoExposure that scales the input, 0 is none */
    TonemapOperator operator_;  /**< The mapping, default PbrNeutral */
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
