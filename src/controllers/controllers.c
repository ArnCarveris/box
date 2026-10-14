/**
 * @file controllers/controllers.c
 * @brief Registers the box.controllers module: the controller components, actions and systems.
 */

#define BOX_CONTROLLERS_IMPL
#include "controllers.h"

ECS_COMPONENT_DECLARE(CameraActionMap);
ECS_COMPONENT_DECLARE(FirstPersonActionMap);

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

/** Free the default action scope a controller constructor reads.
 *
 * @param ctx The scope
 */
static void box_controllers_scope_free(
    void *ctx)
{
    ecs_os_free(ctx);
}

/** Make the constructor of a controller use a scope as its default actions.
 *
 * @param world The world
 * @param component The controller component
 * @param ctor The constructor
 * @param scope The scope of the default actions
 */
static void box_controllers_ctor(
    ecs_world_t *world,
    ecs_entity_t component,
    ecs_xtor_t ctor,
    ecs_entity_t scope)
{
    ecs_entity_t *ctx = ecs_os_malloc_t(ecs_entity_t);
    *ctx = scope;
    ecs_set_hooks_id(world, component, &(ecs_type_hooks_t){
        .ctor = ctor,
        .ctx = ctx,
        .ctx_free = box_controllers_scope_free
    });
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
    ecs_set_hooks(world, CameraActionMap, {
        .ctor = flecs_default_ctor,
        .flags = ECS_TYPE_HOOK_COPY_ILLEGAL | ECS_TYPE_HOOK_MOVE_ILLEGAL
    });
    ecs_add_pair(world, ecs_id(CameraController), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(CameraController), EcsWith, ecs_id(Rotation));

    ECS_META_COMPONENT(world, FirstPersonController);
    ECS_COMPONENT_DEFINE(world, FirstPersonActionMap);
    ecs_set_hooks(world, FirstPersonActionMap, { .ctor = flecs_default_ctor });
    ecs_add_pair(world, ecs_id(FirstPersonController), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(FirstPersonController), EcsWith, ecs_id(Rotation));

    CameraActionMap *map = ecs_singleton_ensure(world, CameraActionMap);
    box_controllers_camera_actions(world, map);
    box_controllers_ctor(world, ecs_id(CameraController), box_camera_controller_ctor, map->scope);

    FirstPersonActionMap *fp_map = ecs_singleton_ensure(world, FirstPersonActionMap);
    box_first_person_actions_create(world, fp_map);
    box_controllers_ctor(world, ecs_id(FirstPersonController), box_first_person_controller_ctor,
        fp_map->scope);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "CameraActionsActivate" }),
        .phase = EcsPreFrame,
        .query.terms = {{ .id = ecs_id(CameraController), .inout = EcsIn }},
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

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "FirstPersonActionsActivate" }),
        .phase = EcsPreFrame,
        .query.terms = {{ .id = ecs_id(FirstPersonController), .inout = EcsIn }},
        .run = BoxFirstPersonActionsActivate,
        .immediate = true
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "FirstPersonControllerUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(FirstPersonController), .inout = EcsInOut },
            { .id = ecs_id(Position), .inout = EcsInOut },
            { .id = ecs_id(Rotation), .inout = EcsInOut }
        },
        .callback = BoxFirstPersonControllerUpdate
    });
}
