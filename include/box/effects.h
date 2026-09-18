/**
 * @file box/effects.h
 * @brief Post process effects: entities with a shader, an order and declared inputs.
 *
 * An effect is an entity with an Effect component that names a Shader entity and
 * declares which inputs its pass reads. The passes of a view run after the
 * transparent pass and before the capture, each reading what the pass before it
 * wrote and the last one writing the image of the view.
 *
 * A View that lists effects in `effect_list` runs those, a View that lists none
 * runs every enabled effect of the world in `order`, and `View.effects` turns the
 * whole chain off. Enabling or disabling an effect entity, changing its order or
 * changing the list of a view reconfigures the chain on the next frame.
 *
 * The parameters of an effect come from the first other reflected struct on its
 * entity, copied into the uniform block the shader reads as `fx.params`, so a
 * project adds a parameter component without touching the renderer.
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

/** One post process pass of the graph, with the inputs it reads */
ECS_STRUCT(Effect, {
    ecs_entity_t shader; /**< Shader entity of the pass, without one it is skipped */
    int32_t order;       /**< Order in the chain, the lowest runs first */
    float scale;         /**< Resolution scale of its output, 0 and 1 are the view size */
    bool enabled;        /**< The pass runs, default true */
    bool color;          /**< The pass reads the image so far, default true */
    bool depth;          /**< The pass reads the depth target of the view */
    bool normals;        /**< The pass reads the world normals of the view */
    bool motion;         /**< The pass reads the motion vectors of the view */
    bool pyramid;        /**< The frame blurs the image the pass reads into a pyramid */
    bool exposure;       /**< The frame adapts the exposure of the view from its parameters */
});

/** Exposure and saturation of the trivial grading pass */
ECS_STRUCT(ColorGrade, {
    float exposure;   /**< Stops the image is scaled by, 0 leaves it */
    float saturation; /**< 0 is grey, 1 leaves it, above 1 saturates */
});

/** Histogram driven exposure the frame adapts over the frames of a scene.
 *
 * The exposure is measured in stops: the image is multiplied by two to the
 * power of the adapted value plus the compensation, which the tone map pass
 * then rolls off.
 */
ECS_STRUCT(AutoExposure, {
    float speed_up;     /**< Stops per second the exposure rises, which brightens the image */
    float speed_down;   /**< Stops per second the exposure falls, which darkens the image */
    float min_ev;       /**< Lowest exposure in stops */
    float max_ev;       /**< Highest exposure in stops */
    float target;       /**< Average luminance the adapted image holds, 0 takes 0.18 */
    float compensation; /**< Stops added to the adapted exposure */
    bool pinned;        /**< The exposure keeps its value instead of adapting */
});

/** Curve and dithering of the tone map pass, the last pass of the chain */
ECS_STRUCT(ToneMapping, {
    float white;  /**< Linear value that reaches display white, 0 takes 1 */
    bool dither;  /**< Ordered 8 by 8 dither of one output step */
});

/** Wide kernel bloom gathered from a mip pyramid of the image */
ECS_STRUCT(Bloom, {
    float intensity;     /**< Fraction of the pyramid mixed into the image */
    float radius;        /**< 0 to 1, levels of the pyramid that feed back */
    float knee;          /**< Soft knee luminance, 0 lets every texel bloom */
    float firefly_clamp; /**< Luminance a texel is clamped to on the first downsample */
});

/** Height based fog integrated along the view ray */
ECS_STRUCT(Fog, {
    vec3 color;        /**< Linear color of the fog */
    float density;     /**< Extinction per meter at the fog height */
    float falloff;     /**< Density e-folds per meter above the height, 0 is uniform */
    float start;       /**< View distance the fog starts at */
    float max_opacity; /**< Highest fraction of the image the fog covers */
    float height;      /**< World height the density is measured at */
});

/** Effects a view can chain, which the effect_list of a View spells out */
#define BOX_EFFECTS_MAX (8)

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
