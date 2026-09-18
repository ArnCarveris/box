/**
 * @file camera/camera.c
 * @brief Registers the box.camera module.
 */

#define BOX_CAMERA_IMPL
#include "camera_internal.h"

ECS_TAG_DECLARE(Billboard);

void BoxCameraImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxCamera);
    ECS_IMPORT(world, BoxTransform);

    ECS_META_COMPONENT(world, Camera);
    ECS_META_COMPONENT(world, Capture);
    ECS_TAG_DEFINE(world, Billboard);

    ecs_add_pair(world, ecs_id(Camera), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(Camera), EcsWith, ecs_id(WorldTransform));
    ecs_add_pair(world, Billboard, EcsWith, ecs_id(Rotation));

    box_billboard_init(world);
}
