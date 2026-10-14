/**
 * @file view/view.c
 * @brief Registers the components of the box.view module.
 */

#define BOX_VIEW_IMPL
#include <box.h>

/** Construct View values that are lit.
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
    ECS_META_COMPONENT(world, View);
    box_owned_hooks(world, View, BOX_OWNED_VEC(View, effects, ViewEffect));
    ecs_set_hooks(world, View, { .ctor = box_view_ctor });
    ECS_META_COMPONENT(world, Scene);
}
