/**
 * @file effects/effects.c
 * @brief Registers the components of the box.effects module with their documented defaults.
 */

#define BOX_EFFECTS_IMPL
#include <box.h>

/** Construct Bloom values with the documented defaults.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_bloom_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Bloom *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Bloom){
            .intensity = 0.3f,
            .low_frequency_boost = 0.7f,
            .low_frequency_boost_curvature = 0.95f,
            .high_pass_frequency = 1.0f,
            .threshold = 1.0f,
            .firefly_clamp = 12.0f,
            .max_mip_dimension = 512
        };
    }
}

/** Construct Gtao values with the documented defaults.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_gtao_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Gtao *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Gtao){
            .radius = 0.5f,
            .falloff_range = 0.615f,
            .final_value_power = 2.2f,
            .quality = GtaoHigh,
            .denoise_passes = 1,
            .half_resolution = true
        };
    }
}

/** Construct HeightFog values with the documented defaults.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_fog_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    HeightFog *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (HeightFog){
            .density = 0.1f,
            .falloff = 0.3f,
            .max_opacity = 1.0f,
            .color = {191, 158, 140},
            .sun_inscatter = 1.0f,
            .ambient_inscatter = 1.0f
        };
    }
}

/** Construct AutoExposure values with the documented defaults.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_exposure_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AutoExposure *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (AutoExposure){
            .min_brightness = 0.1f,
            .max_brightness = 0.3f,
            .min_log_luma = -8.0f,
            .max_log_luma = 4.0f,
            .speed_up = 3.0f,
            .speed_down = 1.0f,
            .low_percentile = 0.5f,
            .high_percentile = 0.95f
        };
    }
}

/** Construct ColorGrading values with the documented defaults, which leave the image as is.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_grading_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ColorGrading *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (ColorGrading){
            .contrast = 1.0f,
            .saturation = 1.0f,
            .gamma = {1.0f, 1.0f, 1.0f},
            .gain = {1.0f, 1.0f, 1.0f},
            .lut_strength = 1.0f
        };
    }
}

/** Construct Smaa values with the documented defaults.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_smaa_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Smaa *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Smaa){
            .threshold = 0.1f,
            .max_search_steps = 16,
            .max_search_steps_diag = 8,
            .corner_rounding = 25
        };
    }
}

void BoxEffectsImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxEffects);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, Bloom);
    ECS_META_COMPONENT(world, GtaoQuality);
    ECS_META_COMPONENT(world, Gtao);
    ECS_META_COMPONENT(world, HeightFog);
    ECS_META_COMPONENT(world, AutoExposure);
    ECS_META_COMPONENT(world, TonemapOperator);
    ECS_META_COMPONENT(world, Tonemap);
    ECS_META_COMPONENT(world, ColorGrading);
    ECS_META_COMPONENT(world, Smaa);

    ecs_set_hooks(world, Bloom, { .ctor = box_bloom_ctor });
    ecs_set_hooks(world, Gtao, { .ctor = box_gtao_ctor });
    ecs_set_hooks(world, HeightFog, { .ctor = box_fog_ctor });
    ecs_set_hooks(world, AutoExposure, { .ctor = box_exposure_ctor });
    ecs_set_hooks(world, Tonemap, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, ColorGrading, { .ctor = box_grading_ctor });
    ecs_set_hooks(world, Smaa, { .ctor = box_smaa_ctor });
}
