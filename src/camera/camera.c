/**
 * @file camera/camera.c
 * @brief Registers the components of the box.camera module.
 */

#define BOX_CAMERA_IMPL
#include <box.h>

void BoxCameraImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxCamera);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, Camera);
}
