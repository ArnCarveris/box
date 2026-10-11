/**
 * @file surface/surface.c
 * @brief Registers the components of the box.surface module.
 */

#define BOX_SURFACE_IMPL
#include <box.h>

void BoxSurfaceImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxSurface);

    ECS_META_COMPONENT(world, Anisotropy);
    ECS_META_COMPONENT(world, TextureQuality);
    ECS_META_COMPONENT(world, Surface);
}
