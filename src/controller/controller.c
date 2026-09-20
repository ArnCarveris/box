/**
 * @file controller/controller.c
 * @brief Registers the box.controller module: the controllers and their actions.
 */

#define BOX_CONTROLLER_IMPL
#include "controller_internal.h"

ECS_COMPONENT_DECLARE(ControllerActions);

void BoxControllerImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxController);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxSurface);
    ECS_IMPORT(world, BoxInput);
    ECS_IMPORT(world, BoxCollision);

    ECS_META_COMPONENT(world, CameraController);
    ECS_META_COMPONENT(world, FirstPersonController);
    ECS_META_COMPONENT(world, FollowLevel);
    ECS_META_COMPONENT(world, FollowCamera);
    ECS_COMPONENT_DEFINE(world, ControllerActions);

    ecs_set_hooks(world, CameraController, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, FirstPersonController, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, ControllerActions, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, FollowCamera, { .ctor = flecs_default_ctor });

    ecs_add_pair(world, ecs_id(CameraController), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(CameraController), EcsWith, ecs_id(Orientation));
    ecs_add_pair(world, ecs_id(CameraController), EcsWith, DynamicTransform);
    ecs_add_pair(world, ecs_id(FirstPersonController), EcsWith, ecs_id(Position));
    ecs_add_pair(
        world, ecs_id(FirstPersonController), EcsWith, ecs_id(Orientation));
    ecs_add_pair(world, ecs_id(FirstPersonController), EcsWith, DynamicTransform);
    ecs_add_pair(world, ecs_id(FollowCamera), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(FollowCamera), EcsWith, ecs_id(LookAt));
    ecs_add_pair(world, ecs_id(FollowCamera), EcsWith, DynamicTransform);

    box_controller_actions_init(world);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxCameraControllerUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(CameraController) },
            { .id = ecs_id(Position) },
            { .id = ecs_id(Orientation) },
            { .id = ecs_id(LookAt), .oper = EcsOptional }
        },
        .callback = BoxCameraControllerUpdate
    });

    ecs_observer(world, {
        .query.terms = { { .id = ecs_id(CameraController), .inout = EcsInOut } },
        .events = { EcsOnAdd },
        .callback = BoxCameraControllerOnAdd
    });

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(CameraController), .inout = EcsInOut },
            { .id = ecs_id(Position), .inout = EcsIn },
            { .id = ecs_id(LookAt), .inout = EcsIn }
        },
        .events = { EcsOnSet },
        .callback = BoxCameraControllerOnPlace
    });

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(CameraController), .inout = EcsInOut },
            { .id = ecs_id(Rotation), .inout = EcsIn },
            { .id = ecs_id(LookAt), .oper = EcsNot }
        },
        .events = { EcsOnSet },
        .callback = BoxCameraControllerOnPlace
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxFirstPersonUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(FirstPersonController) },
            { .id = ecs_id(Position) },
            { .id = ecs_id(Orientation) },
            { .id = ecs_id(LookAt), .oper = EcsOptional }
        },
        .callback = BoxFirstPersonUpdate
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxFollowCameraUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(FollowCamera) },
            { .id = ecs_id(Position) },
            { .id = ecs_id(LookAt) }
        },
        .callback = BoxFollowCameraUpdate
    });
}
