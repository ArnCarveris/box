/**
 * @file view/view.c
 * @brief Registers the components of the box.view module.
 */

#define BOX_VIEW_IMPL
#include <box.h>

/** Construct View values that are lit, sample the baked lighting and the reflection probes of
 * their scene, render its atmosphere, light with an environment at intensity 1, have the
 * documented shadow defaults and cull with occlusion and a smallest size of one pixel.
 *
 * The shadow lookup moves along the geometric normal by 2 texels times 1 - N.L: Esoterica's
 * offset scale, and the smallest offset that keeps a normal mapped floor under a 10 degree sun
 * free of acne.
 *
 * It compares 2 texels closer to the light, which covers the depth slope across the 3 texel
 * tent where the light is too steep for the offset to help (N.L above 0.5). A 0.1 band at the
 * edge of a cascade blends into the next one.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_view_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    View *values = ptr;
    int32_t i;
    ecs_os_memset(ptr, 0, ti->size * count);
    for (i = 0; i < count; i ++) {
        values[i].lighting.enabled = true;
        values[i].lighting.baked = true;
        values[i].lighting.reflections = true;
        values[i].lighting.atmosphere = true;
        values[i].lighting.environment.intensity = 1.0f;
        values[i].shadows = (ShadowSettings){
            .cascades = 4,
            .resolution = 2048,
            .max_distance = 200.0f,
            .split_lambda = 0.7f,
            .depth_bias = 2.0f,
            .normal_offset = 2.0f,
            .filter_size = 3,
            .fade = 0.2f,
            .blend = 0.1f
        };
        values[i].culling = (CullSettings){
            .enabled = true,
            .occlusion = true,
            .min_dimension_px = 1.0f
        };
    }
}

/** Construct ViewEffect values that are enabled, so a script entry that names only its effect
 * runs it.
 *
 * @param ptr The elements
 * @param count Number of elements
 * @param ti Type info
 */
static void box_view_effect_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ViewEffect *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (ViewEffect){ .enabled = true };
    }
}

int box_view_compare(
    int32_t order_a,
    ecs_entity_t a,
    int32_t order_b,
    ecs_entity_t b)
{
    int result = (order_a > order_b) - (order_a < order_b);
    if (!result) {
        result = (a > b) - (a < b);
    }

    return result;
}

void BoxViewImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxView);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, ClearSettings);
    ECS_META_COMPONENT(world, EnvironmentSettings);
    ECS_META_COMPONENT(world, LightSettings);
    ECS_META_COMPONENT(world, ShadowSettings);
    ECS_META_COMPONENT(world, CullSettings);
    ECS_META_COMPONENT(world, ViewEffect);
    ecs_set_hooks(world, ViewEffect, { .ctor = box_view_effect_ctor });
    ECS_META_COMPONENT(world, View);
    box_owned_hooks(world, View, BOX_OWNED_VEC(View, effects, ViewEffect));
    ecs_set_hooks(world, View, { .ctor = box_view_ctor });
    ECS_META_COMPONENT(world, Scene);
}
