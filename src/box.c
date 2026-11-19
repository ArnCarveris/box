/**
 * @file box.c
 * @brief Imports all modules of the box module.
 */

#include <box.h>
#include "jobs/jobs.h"

/** Allocate zeroed memory for ecs_os_malloc.
 *
 * flecs script leaves the cache of the for loop slots of a template state uninitialized when it
 * allocates them, so a template that runs again can find the entity of the previous run in a
 * reused block and write through its freed entry. Zeroed memory makes the cache start empty.
 *
 * @param size Bytes to allocate
 * @return The memory, NULL when out of memory
 */
static void* box_os_malloc(
    ecs_size_t size)
{
    return ecs_os_api.calloc_(size);
}

void BoxImport(
    ecs_world_t *world)
{
    ecs_os_api.malloc_ = box_os_malloc;
    ECS_IMPORT(world, FlecsScriptMath);
    ECS_IMPORT(world, FlecsStats);
    ecs_enable(world, ecs_id(FlecsStats), false);
    ECS_IMPORT(world, BoxTask);
    ECS_IMPORT(world, BoxCurve);
    ECS_IMPORT(world, BoxAnimation);
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
    ECS_IMPORT(world, BoxAudio);
    ECS_IMPORT(world, BoxJobs);
    ECS_IMPORT(world, BoxTexture);
    ECS_IMPORT(world, BoxTerrain);
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
    ECS_IMPORT(world, BoxEditor);
    ECS_IMPORT(world, BoxFps);
    ECS_IMPORT(world, BoxStartup);
    ECS_IMPORT(world, BoxAtmosphere);
    ECS_IMPORT(world, BoxParticle);
    ECS_IMPORT(world, BoxTransition);
    ECS_IMPORT(world, BoxWorld);
}
