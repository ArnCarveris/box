/**
 * @file controllers/controllers.c
 * @brief Registers the box.controllers module: the controller components, actions and systems.
 */

#define BOX_CONTROLLERS_IMPL
#include "controllers.h"

ECS_COMPONENT_DECLARE(CameraActionMap);

const box_controller_action_t box_camera_actions[BoxCameraActionCount] = {
    [BoxCameraForward] = { "Forward", "Move the camera forward", KeyW },
    [BoxCameraBack] = { "Back", "Move the camera back", KeyS },
    [BoxCameraLeft] = { "Left", "Move the camera left", KeyA },
    [BoxCameraRight] = { "Right", "Move the camera right", KeyD },
    [BoxCameraUp] = { "Up", "Move the camera up", KeyE },
    [BoxCameraDown] = { "Down", "Move the camera down", KeyQ },
    [BoxCameraFast] = { "Fast", "Move the camera faster while held", KeyShiftLeft },
    [BoxCameraLook] = { "Look", "Turn the camera with the cursor while held", KeyMouseRight }
};

/** Release the query of the action map.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_camera_action_map_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    CameraActionMap *map = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        if (map[i].controllers) {
            ecs_query_fini(map[i].controllers);
        }
    }
}

/** Create the default camera actions, disabled and each bound to its key.
 *
 * @param world The world
 * @param map The action map, written
 */
static void box_controllers_camera_actions(
    ecs_world_t *world,
    CameraActionMap *map)
{
    int32_t i;
    map->scope = ecs_entity(world, { .name = "camera" });
    for (i = 0; i < BoxCameraActionCount; i ++) {
        const box_controller_action_t *a = &box_camera_actions[i];
        ecs_entity_t action = ecs_entity(world, { .name = a->name, .parent = map->scope });
        ecs_set(world, action, Action, { .description = ECS_CONST_CAST(char*, a->description) });
        ecs_set(world, action, Binding, { .key = a->key });
        ecs_enable(world, action, false);
        map->actions[i] = action;
    }
}

void BoxControllersImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxControllers);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxInput);

    ECS_META_COMPONENT(world, CameraController);
    ECS_COMPONENT_DEFINE(world, CameraActionMap);
    ecs_set_hooks(world, CameraController, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, CameraActionMap, {
        .ctor = flecs_default_ctor,
        .dtor = box_camera_action_map_dtor,
        .flags = ECS_TYPE_HOOK_COPY_ILLEGAL | ECS_TYPE_HOOK_MOVE_ILLEGAL
    });
    ecs_add_pair(world, ecs_id(CameraController), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(CameraController), EcsWith, ecs_id(Rotation));

    CameraActionMap *map = ecs_singleton_ensure(world, CameraActionMap);
    box_controllers_camera_actions(world, map);
    map->controllers = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "CameraControllers" }),
        .terms = {{ .id = ecs_id(CameraController), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "CameraActionsActivate" }),
        .phase = EcsPreFrame,
        .run = BoxCameraActionsActivate,
        .immediate = true
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "CameraControllerUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(CameraController), .inout = EcsInOut },
            { .id = ecs_id(Position), .inout = EcsInOut },
            { .id = ecs_id(Rotation), .inout = EcsInOut }
        },
        .callback = BoxCameraControllerUpdate
    });
}
