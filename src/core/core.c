/**
 * @file core/core.c
 * @brief Registers the box.core module and the App singleton.
 */

#define BOX_CORE_IMPL
#include <box.h>

void box_coreImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, box_core);
    ECS_IMPORT(world, box_types);

    ECS_META_COMPONENT(world, App);
    ecs_singleton_set(world, App, { .cpu_budget = 1.0, .target_fps = 60.0 });
}
