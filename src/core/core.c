/**
 * @file core/core.c
 * @brief Registers the box.core module and the App singleton.
 */

#define BOX_CORE_IMPL
#include "core_internal.h"

void BoxCoreImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxCore);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, App);
    ecs_singleton_set(world, App, {
        .cpu_budget = 1.0,
        .port = BOX_PORT_DEFAULT
    });
    box_process_init(world);
}
