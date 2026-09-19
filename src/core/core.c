/**
 * @file core/core.c
 * @brief Registers the box.core module, the App singleton and the option type.
 */

#define BOX_CORE_IMPL
#include "core_internal.h"

void BoxCoreImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxCore);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, App);
    ECS_META_COMPONENT(world, AppOption);
    ecs_singleton_set(world, App, {
        .cpu_budget = 1.0,
        .port = BOX_PORT_DEFAULT
    });
    box_process_init(world);
}
