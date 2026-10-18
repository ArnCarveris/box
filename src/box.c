/**
 * @file box.c
 * @brief Imports all modules of the box module.
 */

#include <box.h>
#include "jobs/jobs.h"

/** Make flecs allocate zeroed memory.
 *
 * flecs_script_for_slot_init leaves the entry cache of a for loop slot uninitialized, so a slot
 * in reused memory can match an entity of an earlier instance and write its freed entry.
 *
 * Zeroed slots start with an empty cache; drop this once flecs initializes it.
 */
static void box_os_zeroed_alloc(void)
{
    if (ecs_os_api.calloc_ && ecs_os_api.malloc_ != ecs_os_api.calloc_) {
        ecs_os_api.malloc_ = ecs_os_api.calloc_;
    }
}

void BoxImport(
    ecs_world_t *world)
{
    box_os_zeroed_alloc();
    ECS_IMPORT(world, FlecsScriptMath);
    ECS_IMPORT(world, FlecsStats);
    ecs_enable(world, ecs_id(FlecsStats), false);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxSurface);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxEffects);
    ECS_IMPORT(world, BoxMaterial);
    ECS_IMPORT(world, BoxLight);
    ECS_IMPORT(world, BoxMovement);
    ECS_IMPORT(world, BoxLayout);
    ECS_IMPORT(world, BoxInput);
    ECS_IMPORT(world, BoxControllers);
    ECS_IMPORT(world, BoxApp);
    ECS_IMPORT(world, BoxJobs);
    ECS_IMPORT(world, BoxTexture);
    ECS_IMPORT(world, BoxText);
    ECS_IMPORT(world, BoxRender);
    ECS_IMPORT(world, BoxShader);
    ECS_IMPORT(world, BoxCommand);
    ECS_IMPORT(world, BoxPerf);
    ECS_IMPORT(world, BoxProjection);
    ECS_IMPORT(world, BoxSpatial);
    ECS_IMPORT(world, BoxPicking);
    ECS_IMPORT(world, BoxUi);
    ECS_IMPORT(world, BoxConsole);
    ECS_IMPORT(world, BoxAtmosphere);
    ECS_IMPORT(world, BoxParticle);
}
