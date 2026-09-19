/**
 * @file lod/lod.c
 * @brief Registers the box.lod module.
 *
 * The module holds the proxy generator, which is a pure function over the
 * primitives of a subtree: it needs no components of its own yet, only the
 * shape components its proxies are expressed in.
 */

#include "lod_internal.h"

void BoxLodImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxLod);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxGeometry);
}
