/**
 * @file view/view.c
 * @brief Registers the components of the box.view module.
 */

#define BOX_VIEW_IMPL
#include <box.h>

/** Construct View values that are lit, with the documented shadow defaults.
 *
 * The CullSettings stay zero, which culls with occlusion and the default smallest size.
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

void BoxViewImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxView);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, ClearSettings);
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
