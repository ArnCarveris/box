/**
 * @file box/box.c
 * @brief The box module: imports every module of the framework in one order.
 *
 * Component ids live in process globals that every world forces, so two worlds
 * that register the modules in different orders collide. An application or a
 * test imports this module and nothing else, which gives every world the same
 * registration order and the same ids.
 */

#define BOX_IMPL
#include <box.h>

void BoxImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, Box);
    ECS_IMPORT(world, FlecsScriptMath);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxCore);
    ECS_IMPORT(world, BoxJob);
    ECS_IMPORT(world, BoxShader);
    ECS_IMPORT(world, BoxSurface);
    ECS_IMPORT(world, BoxInput);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxLight);
    ECS_IMPORT(world, BoxMaterial);
    ECS_IMPORT(world, BoxTransition);
    ECS_IMPORT(world, BoxLod);
    ECS_IMPORT(world, BoxEffects);
    ECS_IMPORT(world, BoxSky);
    ECS_IMPORT(world, BoxRender);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxTexture);
    ECS_IMPORT(world, BoxGltf);
    ECS_IMPORT(world, BoxMovement);
    ECS_IMPORT(world, BoxCollision);
    ECS_IMPORT(world, BoxPicking);
    ECS_IMPORT(world, BoxSpline);
    ECS_IMPORT(world, BoxParticle);
    ECS_IMPORT(world, BoxController);
    ECS_IMPORT(world, BoxUi);
    ECS_IMPORT(world, BoxMonitor);
    ECS_IMPORT(world, BoxScript);
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxCommand);
}
