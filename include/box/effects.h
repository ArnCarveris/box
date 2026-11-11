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

/** Glow around pixels brighter than the threshold.
 *
 * Every pixel is clamped against fireflies, weighted down by its brightness (Karis) and
 * thresholded before the image is halved, so the glow of a small or thin bright detail stays
 * the same as it moves across the pixels, without temporal, which adds ghosting.
 *
 * The first mip is half the image or, past twice max_mip_dimension, the image halved until a
 * B-spline filter brings it to that size: with the default 512, one extra pass at 1280x720 and
 * two at 2560x1440 and 3840x2160. A cubic B-spline of the first mip is added to the image.
 */
ECS_STRUCT(Bloom, {
    float intensity;                     /**< Blend into the image, default 0.3 */
    float low_frequency_boost;           /**< Extra weight of the wide mips, default 0.7 */
    float low_frequency_boost_curvature; /**< Falloff of the boost across mips, default 0.95 */
    float high_pass_frequency;           /**< Fraction of mips that add, default 1 */
    float threshold;                     /**< Luminance above which pixels bloom, default 1 */
    float threshold_softness;            /**< Width of the knee around the threshold, default 0 */
    float firefly_clamp;                 /**< Clamp against fireflies, default 12, 0 off */
    float temporal;                      /**< Blend with last frame, default 0, max 0.95 */
    int32_t max_mip_dimension;           /**< Longest edge of the first mip, at most half the
                                          *   image, default 512, at least 8 */
});

/** What decides the blur of a pixel of a DepthOfField */
ECS_ENUM(DepthOfFieldMode, {
    DofCamera = 0,   /**< Blur by view depth around the focus distance, like a camera lens */
    DofTiltShift = 1 /**< Blur by screen position: a sharp band across the image with the blur
                      *   growing above and below it, independent of depth, the miniature look */
});

/** Bokeh blur by view depth, the distance along the forward axis of the camera, not the ray.
 *
 * Sharp within focus_range around focus_distance, the blur ramps up to max_radius over the near
 * and far transitions. Depth 0 (the sky, and every pixel of a view of only 2D shapes) is far at
 * full blur. Perspective and orthographic cameras, lit or unlit, work the same way.
 *
 * DofTiltShift blurs by screen position instead: sharp within tilt_band of the line through
 * tilt_center turned by tilt_angle, the blur ramps up to max_radius over tilt_transition, in
 * units of the image height. focus_distance, focus_range, near_transition, far_transition and
 * near_blur are ignored. {mode: DofTiltShift} alone gives a miniature look; a max_radius of 0.015
 * to 0.02 is typical for it, and a ColorGrading with a saturation of about 1.3 and a contrast of
 * about 1.1 strengthens it.
 *
 * Belongs before the Tonemap, in HDR, so bright highlights make bokeh discs, and before Bloom.
 * An entity with several effects runs HeightFog, DepthOfField, Bloom, AutoExposure, Tonemap,
 * ColorGrading, ChromaticAberration, Vignette and Smaa.
 */
ECS_STRUCT(DepthOfField, {
    float focus_distance;  /**< View depth of the focus plane, default 10, at least 0 */
    float focus_range;     /**< Depth around focus_distance that stays fully sharp, default 2,
                            *   at least 0 */
    float near_transition; /**< Depth over which the blur ramps from 0 to max in front of the
                            *   sharp range, default 4, at least 0; 0 is a hard step */
    float far_transition;  /**< Depth over which the blur ramps from 0 to max behind the sharp
                            *   range, default 20, at least 0; 0 is a hard step */
    float max_radius;      /**< Largest blur radius as a fraction of the image height, default
                            *   0.01, clamped to 0..0.05; 0 leaves the image as is */
    bool near_blur;        /**< Blur in front of the focus, default true; false only blurs the
                            *   background */
    DepthOfFieldMode mode; /**< Blur by depth or by screen position, default DofCamera */
    float tilt_center;     /**< Normalized image Y of the center of the sharp band, 0 at the top,
                            *   default 0.55 */
    float tilt_band;       /**< Half height of the fully sharp band as a fraction of the image
                            *   height, default 0.08, at least 0 */
    float tilt_transition; /**< Distance from the band edge, as a fraction of the image height,
                            *   over which the blur ramps to max, default 0.3, at least 0; 0 is
                            *   a hard step */
    float tilt_angle;      /**< Counterclockwise rotation of the band in radians, default 0 */
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
 * With atmosphere set to the entity with the Atmosphere that a view renders, the in scatter
 * color comes from its sky instead of color: the ambient from the horizon toward the zenith, the
 * glow from the horizon toward the main light with the Mie phase.
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
    ecs_entity_t atmosphere; /**< Entity with the Atmosphere whose sky gives the in scatter
                              *   color in a view that renders it, default 0 for color */
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

/** Shapes the look of the image: white balance, exposure, contrast, saturation, vibrance,
 * lift/gamma/gain and an optional strip LUT, applied in that order. Every default is a no-op.
 *
 * Belongs after the Tonemap: its LUT expects display range values. Placed before the Tonemap the
 * parametric grading works on HDR values, contrast around a middle grey of 0.18 in log space,
 * and the LUT clamps them to 0..1. After a Tonemap of the view contrast works around 0.5.
 *
 * The LUT is a 2D strip, the "unwrapped cube": N slices of NxN side by side, e.g. 1024x32 for
 * 32^3 or 256x16 for 16^3. Red increases to the right within a slice, green downward and blue
 * from slice to slice. N is the image height and the width must be N*N; an image of any other
 * shape logs an error once and is ignored. It is applied last, on the color clamped to 0..1 and
 * sRGB encoded, with trilinear filtering, and its result is decoded back to linear. It does
 * nothing until the texture is loaded.
 */
ECS_STRUCT(ColorGrading, {
    float temperature;  /**< White balance from blue (-1) to warm (+1), a chromatic adaptation
                         *   in LMS space, default 0 */
    float tint;         /**< White balance from green (-1) to magenta (+1), default 0 */
    float exposure;     /**< Multiplier in stops, separate from AutoExposure, default 0 */
    float contrast;     /**< Contrast around middle grey, default 1, at least 0 */
    float saturation;   /**< 0 is grey (luminance), default 1, at least 0 */
    float vibrance;     /**< Saturates the less saturated colors more, default 0, at least -1 */
    vec3 lift;          /**< Added per channel after the gain, default 0 */
    vec3 gamma;         /**< Per channel: pow(max(c * gain + lift, 0), 1 / gamma), default 1,
                         *   at least 0.01 */
    vec3 gain;          /**< Multiplier per channel, default 1 */
    ecs_entity_t lut;   /**< Texture entity (TexturePath) with a strip LUT, default 0 for none */
    float lut_strength; /**< Blend of the LUT result, default 1, clamped to 0..1 */
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

/** Lateral color fringing that grows toward the edges of the image, as in a cheap lens.
 *
 * The input is sampled along the line from the pixel toward the center, from -offset to +offset,
 * where offset = intensity * (d / d_corner)^falloff and d is the distance from the center. Each
 * tap is weighted by a red, green, blue spectral ramp normalized per channel, so a white image
 * stays white. The center pixel is never shifted, the edges clamp and alpha is taken from the
 * center tap.
 *
 * Works before or after the Tonemap. Before it, bright HDR fringes bloom stronger.
 */
ECS_STRUCT(ChromaticAberration, {
    float intensity; /**< Largest offset at the image corners as a fraction of the image width,
                      *   default 0.005, at least 0 */
    int32_t samples; /**< Taps along the offset, default 8, clamped to 3..16 */
    vec2 center;     /**< Center in normalized image coordinates, 0, 0 the top left, default
                      *   0.5, 0.5 */
    float falloff;   /**< Exponent of the offset over the distance from the center, default 2,
                      *   1 is linear, at least 0 */
});

/** Darkens or tints the edges of the image toward the corners, as a lens does.
 *
 * Belongs after the Tonemap: it multiplies the color by mix(1, color, v * intensity), where
 * v = smoothstep(radius, radius + smoothness, d) and d is the distance from the center in half
 * diagonals, so a black vignette darkens and a colored one tints. Alpha passes through.
 */
ECS_STRUCT(Vignette, {
    float intensity;  /**< Strength, 0 none to 1 full, default 0.3, clamped to 0..1 */
    float radius;     /**< Distance from the center in half diagonals where the falloff starts,
                       *   default 0.75, at least 0 */
    float smoothness; /**< Width of the falloff, default 0.45, at least 0; 0 is a hard edge */
    float roundness;  /**< 1 is a circle regardless of the aspect ratio, 0 follows the
                       *   rectangle of the image, default 1, clamped to 0..1 */
    vec2 center;      /**< Center in normalized image coordinates, 0, 0 the top left, default
                       *   0.5, 0.5 */
    rgb color;        /**< Color of the vignette, display encoded, default 0 0 0 */
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
