/**
 * @file layout/layout.c
 * @brief Registers the components of the box.layout module.
 */

#define BOX_LAYOUT_IMPL
#include <box.h>

void BoxLayoutImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxLayout);

    ECS_META_COMPONENT(world, Align);
    ECS_META_COMPONENT(world, VAlign);
    ECS_META_COMPONENT(world, Direction);
    ECS_META_COMPONENT(world, Padding);
    ECS_META_COMPONENT(world, Layout);
    ECS_META_COMPONENT(world, SizeMode);
    ECS_META_COMPONENT(world, Sizing);
    ECS_META_COMPONENT(world, LayoutItem);
    ECS_META_COMPONENT(world, LayoutBounds);

    ecs_add_pair(world, ecs_id(Layout), EcsWith, ecs_id(LayoutBounds));
    ecs_add_pair(world, ecs_id(LayoutItem), EcsWith, ecs_id(LayoutBounds));
}
