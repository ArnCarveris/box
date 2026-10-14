/**
 * @file light/light.c
 * @brief Registers the components of the box.light module and their defaults.
 */

#define BOX_LIGHT_IMPL
#include <box.h>

/** Construct DirectionalLight values at full intensity.
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
        values[i] = (DirectionalLight){ .intensity = 1.0f };
    }
}

/** Construct PointLight values at full intensity with the default range.
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
        values[i] = (PointLight){ .intensity = 1.0f, .range = 10.0f };
    }
}

/** Construct SpotLight values at full intensity with the default range and cone.
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
            .intensity = 1.0f, .range = 10.0f, .inner_angle = 20.0f, .outer_angle = 30.0f
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

/** Construct View values that are lit, with the documented shadow defaults.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_light_view_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    View *values = ptr;
    int32_t i;
    ecs_os_memset(ptr, 0, ti->size * count);
    for (i = 0; i < count; i ++) {
        values[i].lighting.enabled = true;
        values[i].shadows = (ShadowSettings){
            .cascades = 4,
            .resolution = 2048,
            .max_distance = 200.0f,
            .split_lambda = 0.7f,
            .depth_bias = 2.0f,
            .normal_offset = 1.5f,
            .filter_size = 3,
            .fade = 0.2f
        };
    }
}

void BoxLightImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxLight);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxView);

    ECS_META_COMPONENT(world, DirectionalLight);
    ECS_META_COMPONENT(world, PointLight);
    ECS_META_COMPONENT(world, SpotLight);
    ECS_META_COMPONENT(world, AmbientLight);
    ecs_set_hooks(world, DirectionalLight, { .ctor = box_directional_light_ctor });
    ecs_set_hooks(world, PointLight, { .ctor = box_point_light_ctor });
    ecs_set_hooks(world, SpotLight, { .ctor = box_spot_light_ctor });
    ecs_set_hooks(world, AmbientLight, { .ctor = box_ambient_light_ctor });
    ecs_set_hooks(world, View, { .ctor = box_light_view_ctor });
}
