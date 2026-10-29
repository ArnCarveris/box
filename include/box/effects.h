/**
 * @file box/effects.h
 * @brief Post process effect settings, a view lists the effect entities it applies.
 *
 * A View renders into an HDR target and runs its effects in order, on its own image only, so a
 * later UI view keeps its text crisp under Smaa. Constructors set the defaults; values apply as
 * specified, so a C literal that leaves a field out sets it to zero.
 *
 * Gtao is listed like the others but is no pass of the chain: it runs between the depth prepass
 * and the forward pass wherever it is listed, only the first enabled one counts, and a view whose
 * only effect is a Gtao renders without an HDR image, like a view without effects.
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

/** Slices and steps per slice of the horizon search of Gtao, the quality levels of XeGTAO */
ECS_ENUM(GtaoQuality, {
    GtaoLow = 0,    /**< 1 slice of 2 steps */
    GtaoMedium = 1, /**< 2 slices of 2 steps */
    GtaoHigh = 2,   /**< 3 slices of 3 steps */
    GtaoUltra = 3   /**< 9 slices of 3 steps */
});

/** Ground truth ambient occlusion (XeGTAO) of the indirect light of a view.
 *
 * Computed from the depth prepass before the forward pass, which scales the indirect diffuse
 * light of opaque and alpha tested meshes by the smaller of it and their ORM occlusion and the
 * indirect specular by its specular occlusion; direct light, emission and blended draws are not.
 *
 * No pass of the chain: it applies wherever View.effects lists it, the first enabled one counts.
 * Lit views with a perspective camera apply it; unlit views, orthographic cameras and the probe
 * and reflection captures ignore it, and 2D shapes and particles are never occluded.
 */
ECS_STRUCT(Gtao, {
    float radius;            /**< World radius of the occlusion sphere, default 0.5, at least 0 */
    float falloff_range;     /**< Fraction of the radius over which occluders fade out, default
                              *   0.615, clamped to 0..1 */
    float final_value_power; /**< Power applied to the visibility, the strength, default 2.2, at
                              *   least 0; 0 is no occlusion */
    GtaoQuality quality;     /**< Slices and steps of the search, default GtaoHigh */
    int32_t denoise_passes;  /**< Edge aware denoise passes, default 1, clamped to 0..3 */
    bool half_resolution;    /**< Compute at half the render size and upsample with a depth aware
                              *   filter, default true */
});

/** Exponential fog that thins out with height, lit by the sun and ambient light of the scene.
 * The fog applies on top of the aerial perspective.
 *
 * With atmosphere set, a view rendering the Atmosphere of its scene takes the in scatter color
 * from its sky, ignoring color: the ambient is the sky horizon mixed a little toward its zenith,
 * the glow the horizon toward the main light (sun by day, moon by night) with the Mie phase.
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

/** SMAA 1x anti aliasing: color edge detection with diagonal and corner detection.
 *
 * Belongs after the Tonemap: edges are found on the input clamped to 0..1 and sRGB encoded, while
 * the blend reads it as is (linear light, all four channels), keeping values above 1 but finding
 * no edges between them. Placed before the Tonemap it runs on the clamped HDR values.
 *
 * It only touches the image of its own view: text and UI of a later view stay crisp, while 2D
 * shapes and text inside the same 3D view (world space UI) are anti aliased with it. A view
 * without clear composites anti aliased coverage.
 */
ECS_STRUCT(Smaa, {
    float threshold;               /**< Color edge detection threshold on display encoded
                                    *   values, default 0.1, clamped to 0.01..0.5 */
    int32_t max_search_steps;      /**< Horizontal and vertical search steps, default 16,
                                    *   clamped to 1..112 */
    int32_t max_search_steps_diag; /**< Diagonal search steps, default 8, clamped to 0..20,
                                    *   0 turns the diagonal detection off */
    int32_t corner_rounding;       /**< Percentage of the corners that is rounded, default 25,
                                    *   clamped to 0..100 */
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
