/**
 * @file portal/portal.c
 * @brief Registers the components of a portal-room world, the index of its areas and portals
 * and the system of its sensors.
 */

#define BOX_PORTAL_IMPL
#include "portal.h"

/** Construct VisPortal values: open, like an opening nothing covers.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti Type info of VisPortal
 */
static void box_vis_portal_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    VisPortal *portals = ptr;
    int32_t i;
    (void)ti;

    ecs_os_memset_n(portals, 0, VisPortal, count);

    for (i = 0; i < count; i ++) {
        portals[i].open = 1.0f;
    }
}

void BoxPortalImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxPortal);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxView);

    ECS_META_COMPONENT(world, VisAreaMaterials);
    ECS_META_COMPONENT(world, VisArea);
    box_owned_hooks(world, VisArea, BOX_OWNED_VEC(VisArea, shape, vec2));
    ECS_META_COMPONENT(world, PortalKind);
    ECS_META_COMPONENT(world, PortalSlide);
    ECS_META_COMPONENT(world, VisPortal);
    ecs_set_hooks(world, VisPortal, { .ctor = box_vis_portal_ctor });
    ECS_META_COMPONENT(world, VisOccluder);
    ECS_META_COMPONENT(world, PortalSensor);
    ECS_META_COMPONENT(world, PortalStatus);

    box_portal_index_import(world);
    box_portal_sensors_import(world);
}
