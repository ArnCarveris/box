/**
 * @file lod/lod.c
 * @brief Registers the box.lod module.
 *
 * The module holds the proxy generator, the screen space metric and the node
 * hierarchy the traversal walks, which are pure code, plus the two components
 * the traversal reads and writes: LodPin and LodStats.
 */

#define BOX_LOD_IMPL
#include "lod_internal.h"

ECS_TAG_DECLARE(LodPin);

void BoxLodImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxLod);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, LodStats);
    ECS_TAG_DEFINE(world, LodPin);
    ecs_add_pair(world, ecs_id(LodStats), EcsOnInstantiate, EcsDontInherit);
}
