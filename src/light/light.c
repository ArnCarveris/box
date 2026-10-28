/**
 * @file light/light.c
 * @brief Registers the components of the box.light module and their defaults.
 */

#define BOX_LIGHT_IMPL
#include <box.h>

/** Construct DirectionalLight values at full intensity, Mixed.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_directional_light_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    DirectionalLight *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (DirectionalLight){ .intensity = 1.0f, .bake = LightBakeModeMixed };
    }
}

/** Construct PointLight values at full intensity with the default range, Mixed.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_point_light_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    PointLight *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (PointLight){
            .intensity = 1.0f, .range = 10.0f, .bake = LightBakeModeMixed
        };
    }
}

/** Construct SpotLight values at full intensity with the default range and cone, Mixed, without
 * shadows.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_spot_light_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    SpotLight *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (SpotLight){
            .intensity = 1.0f, .range = 10.0f, .inner_angle = 20.0f, .outer_angle = 30.0f,
            .bake = LightBakeModeMixed, .shadows = false, .shadow_resolution = 512
        };
    }
}

/** Construct AmbientLight values with the default sky and ground.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_ambient_light_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    AmbientLight *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (AmbientLight){
            .sky = {118, 130, 149}, .ground = {79, 74, 68}, .intensity = 1.0f
        };
    }
}

/** Construct ShadowAtlas values with the default resolution and smallest tile.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_shadow_atlas_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ShadowAtlas *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (ShadowAtlas){ .resolution = 2048, .min_tile = 64 };
    }
}

ECS_TAG_DECLARE(NoBake);

/** Construct LightBake values with the default probe grid and cache folder.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_light_bake_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    LightBake *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (LightBake){
            .probe_spacing = 2.0f,
            .bounces = 2,
            .capture_size = 16,
            .budget_ms = 2.0f,
            .bounds_padding = 1.0f,
            .cache = ecs_os_strdup(".box/bake"),
            .light_threshold = 2.0f,
            .color_threshold = 0.05f
        };
    }
}

/** Add LightBakeStatus to an entity that got LightBake.
 *
 * @param it The observer iterator
 */
static void box_light_bake_add(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_set(it->stage, it->entities[i], LightBakeStatus, {0});
    }
}

/** Remove LightBakeStatus from an entity that lost LightBake.
 *
 * @param it The observer iterator
 */
static void box_light_bake_remove(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_remove(it->stage, it->entities[i], LightBakeStatus);
    }
}

/** Construct ReflectionBake values with the default resolution, budget and scene probe.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_reflection_bake_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ReflectionBake *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (ReflectionBake){
            .resolution = 128,
            .budget_ms = 1.0f,
            .scene_probe = true,
            .scene_probe_height = 2.0f,
            .light_threshold = 2.0f,
            .color_threshold = 0.05f
        };
    }
}

/** Construct ReflectionProbe values with the default boxes and blend.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_reflection_probe_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ReflectionProbe *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (ReflectionProbe){
            .extent = {5.0f, 5.0f, 5.0f},
            .blend = 1.0f,
            .projection = {5.0f, 5.0f, 5.0f}
        };
    }
}

/** Add ReflectionBakeStatus to an entity that got ReflectionBake.
 *
 * @param it The observer iterator
 */
static void box_reflection_bake_add(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_set(it->stage, it->entities[i], ReflectionBakeStatus, {0});
    }
}

/** Remove ReflectionBakeStatus from an entity that lost ReflectionBake.
 *
 * @param it The observer iterator
 */
static void box_reflection_bake_remove(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_remove(it->stage, it->entities[i], ReflectionBakeStatus);
    }
}

/** Construct Environment values with the default resolution and the GGX filter.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_environment_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Environment *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Environment){ .resolution = 256, .filter = EnvironmentFilterGgx };
    }
}

/** Add EnvironmentStatus to an entity that got Environment.
 *
 * @param it The observer iterator
 */
static void box_environment_add(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_set(it->stage, it->entities[i], EnvironmentStatus, {0});
    }
}

/** Remove EnvironmentStatus from an entity that lost Environment.
 *
 * @param it The observer iterator
 */
static void box_environment_remove(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_remove(it->stage, it->entities[i], EnvironmentStatus);
    }
}

void BoxLightImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxLight);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, LightBakeMode);
    ECS_META_COMPONENT(world, DirectionalLight);
    ECS_META_COMPONENT(world, PointLight);
    ECS_META_COMPONENT(world, SpotLight);
    ECS_META_COMPONENT(world, AmbientLight);
    ecs_set_hooks(world, DirectionalLight, { .ctor = box_directional_light_ctor });
    ecs_set_hooks(world, PointLight, { .ctor = box_point_light_ctor });
    ecs_set_hooks(world, SpotLight, { .ctor = box_spot_light_ctor });
    ecs_set_hooks(world, AmbientLight, { .ctor = box_ambient_light_ctor });
    ECS_META_COMPONENT(world, ShadowAtlas);
    ecs_set_hooks(world, ShadowAtlas, { .ctor = box_shadow_atlas_ctor });
    ECS_META_COMPONENT(world, LightBake);
    box_owned_hooks(world, LightBake, BOX_OWNED_STR(LightBake, cache));
    ecs_set_hooks(world, LightBake, { .ctor = box_light_bake_ctor });
    ECS_META_COMPONENT(world, LightBakeStatus);
    box_owned_hooks(world, LightBakeStatus, BOX_OWNED_STR(LightBakeStatus, error));
    ECS_TAG_DEFINE(world, NoBake);
    ECS_META_COMPONENT(world, ReflectionBake);
    ecs_set_hooks(world, ReflectionBake, { .ctor = box_reflection_bake_ctor });
    ECS_META_COMPONENT(world, ReflectionBakeStatus);
    box_owned_hooks(world, ReflectionBakeStatus, BOX_OWNED_STR(ReflectionBakeStatus, error));
    ECS_META_COMPONENT(world, ReflectionProbe);
    ecs_set_hooks(world, ReflectionProbe, { .ctor = box_reflection_probe_ctor });
    ECS_META_COMPONENT(world, EnvironmentFilter);
    ECS_META_COMPONENT(world, Environment);
    ecs_set_hooks(world, Environment, { .ctor = box_environment_ctor });
    ECS_META_COMPONENT(world, EnvironmentStatus);
    box_owned_hooks(world, EnvironmentStatus, BOX_OWNED_STR(EnvironmentStatus, error));
    ECS_META_COMPONENT(world, EnvironmentImage);
    box_owned_hooks(world, EnvironmentImage, BOX_OWNED_STR(EnvironmentImage, path));
    ecs_add_pair(world, ecs_id(EnvironmentImage), EcsWith, ecs_id(Environment));

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(LightBake) }},
        .events = { EcsOnAdd },
        .callback = box_light_bake_add
    });
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(LightBake) }},
        .events = { EcsOnRemove },
        .callback = box_light_bake_remove
    });
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(ReflectionBake) }},
        .events = { EcsOnAdd },
        .callback = box_reflection_bake_add
    });
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(ReflectionBake) }},
        .events = { EcsOnRemove },
        .callback = box_reflection_bake_remove
    });
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Environment) }},
        .events = { EcsOnAdd },
        .callback = box_environment_add
    });
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Environment) }},
        .events = { EcsOnRemove },
        .callback = box_environment_remove
    });
}
