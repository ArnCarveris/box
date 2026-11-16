/**
 * @file portal/portal.c
 * @brief Registers the authoring components of a portal-room world.
 */

#define BOX_PORTAL_IMPL
#include <box.h>

void BoxPortalImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxPortal);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, VisAreaMaterials);
    ECS_META_COMPONENT(world, VisArea);
    box_owned_hooks(world, VisArea, BOX_OWNED_VEC(VisArea, shape, vec2));
    ECS_META_COMPONENT(world, PortalKind);
    ECS_META_COMPONENT(world, VisPortal);
    ECS_META_COMPONENT(world, VisOccluder);
    ECS_META_COMPONENT(world, PortalDoorStyle);
    ECS_META_COMPONENT(world, PortalDoorSlide);
    ECS_META_COMPONENT(world, PortalDoor);
}
