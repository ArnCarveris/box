/**
 * @file view/view.c
 * @brief Registers the components of the box.view module.
 */

#define BOX_VIEW_IMPL
#include <box.h>

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
    ECS_META_COMPONENT(world, Scene);
}
