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

/** Construct Ssao values with the documented defaults.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_ssao_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Ssao *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Ssao){ .radius = 0.5f, .bias = 0.025f, .intensity = 1.0f };
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
            .ambient_inscatter = 1.0f,
            .atmosphere = true
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

void BoxEffectsImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxEffects);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, Bloom);
    ECS_META_COMPONENT(world, Ssao);
    ECS_META_COMPONENT(world, HeightFog);
    ECS_META_COMPONENT(world, AutoExposure);
    ECS_META_COMPONENT(world, TonemapOperator);
    ECS_META_COMPONENT(world, Tonemap);

    ecs_set_hooks(world, Bloom, { .ctor = box_bloom_ctor });
    ecs_set_hooks(world, Ssao, { .ctor = box_ssao_ctor });
    ecs_set_hooks(world, HeightFog, { .ctor = box_fog_ctor });
    ecs_set_hooks(world, AutoExposure, { .ctor = box_exposure_ctor });
    ecs_set_hooks(world, Tonemap, { .ctor = flecs_default_ctor });
}
