/**
 * @file controllers/controllers.c
 * @brief Registers the box.controllers module: the controller components, actions and systems.
 */

#define BOX_CONTROLLERS_IMPL
#include "controllers.h"

ECS_COMPONENT_DECLARE(CameraActionMap);
ECS_COMPONENT_DECLARE(FirstPersonActionMap);
ECS_TAG_DECLARE(Climbable);
ECS_TAG_DECLARE(TriangleCollider);

const box_controller_action_t box_camera_actions[BoxCameraActionCount] = {
    [BoxCameraForward] = { "Forward", "Move the camera forward", KeyW, KeyNone },
    [BoxCameraBack] = { "Back", "Move the camera back", KeyS, KeyNone },
    [BoxCameraLeft] = { "Left", "Move the camera left", KeyA, KeyNone },
    [BoxCameraRight] = { "Right", "Move the camera right", KeyD, KeyNone },
    [BoxCameraUp] = { "Up", "Move the camera up", KeyE, KeyNone },
    [BoxCameraDown] = { "Down", "Move the camera down", KeyQ, KeyNone },
    [BoxCameraFast] = { "Fast", "Move the camera faster while held", KeyShiftLeft, KeyShiftRight },
    [BoxCameraLook] = { "Look", "Turn the camera with the cursor while held", KeyMouseRight,
        KeyNone },
    [BoxCameraTurnLeft] = { "TurnLeft", "Turn the camera left while held", KeyLeft, KeyNone },
    [BoxCameraTurnRight] = { "TurnRight", "Turn the camera right while held", KeyRight, KeyNone },
    [BoxCameraTurnUp] = { "TurnUp", "Turn the camera up while held", KeyUp, KeyNone },
    [BoxCameraTurnDown] = { "TurnDown", "Turn the camera down while held", KeyDown, KeyNone }
};

ecs_entity_t box_controller_actions_create(
    ecs_world_t *world,
    const char *name,
    const box_controller_action_t *table,
    int32_t count,
    ecs_entity_t *actions,
    ecs_entity_t *alts)
{
    ecs_entity_t scope = ecs_entity(world, { .name = name });
    int32_t i;
    for (i = 0; i < count; i ++) {
        const box_controller_action_t *a = &table[i];
        ecs_entity_t action = ecs_entity(world, { .name = a->name, .parent = scope });
        ecs_entity_t alt = 0;
        ecs_set(world, action, Action, { .description = ECS_CONST_CAST(char*, a->description) });
        ecs_set(world, action, Binding, { .key = a->key });
        if (a->alt != KeyNone) {
            alt = ecs_entity(world, { .name = box_input_key_name(a->alt), .parent = action });
            ecs_set(world, alt, Binding, { .key = a->alt });
            ecs_enable(world, alt, false);
        }

        ecs_enable(world, action, false);
        actions[i] = action;
        alts[i] = alt;
    }

    return scope;
}

void box_controller_actions_enable(
    ecs_world_t *world,
    const ecs_entity_t *actions,
    const ecs_entity_t *alts,
    int32_t count,
    bool enabled)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ActionState *state = ecs_is_alive(world, actions[i])
            ? ecs_get_mut(world, actions[i], ActionState)
            : NULL;
        if (state) {
            ecs_os_zeromem(state);
            ecs_enable(world, actions[i], enabled);
        }

        if (alts[i] && ecs_is_alive(world, alts[i])) {
            ecs_enable(world, alts[i], enabled);
        }
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
    ECS_IMPORT(world, BoxGeometry);

    ECS_TAG_DEFINE(world, Climbable);
    ecs_add_pair(world, Climbable, EcsOnInstantiate, EcsInherit);
    ECS_TAG_DEFINE(world, TriangleCollider);
    ecs_add_pair(world, TriangleCollider, EcsOnInstantiate, EcsInherit);
    box_collider_triangles_init(world);

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
    map->scope = box_controller_actions_create(world, "camera", box_camera_actions,
        BoxCameraActionCount, map->actions, map->alts);
    box_controllers_ctor(world, ecs_id(CameraController), box_camera_controller_ctor, map->scope);

    FirstPersonActionMap *fp_map = ecs_singleton_ensure(world, FirstPersonActionMap);
    fp_map->scope = box_controller_actions_create(world, "first_person",
        box_first_person_actions, BoxFirstPersonActionCount, fp_map->actions, fp_map->alts);
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

    ECS_META_COMPONENT(world, VesselMode);
    ECS_META_COMPONENT(world, Vessel);
    ECS_META_COMPONENT(world, VesselStatus);
    ECS_COMPONENT_DEFINE(world, HelmActionMap);
    ecs_set_hooks(world, HelmActionMap, { .ctor = flecs_default_ctor });
    ecs_add_pair(world, ecs_id(Vessel), EcsWith, ecs_id(VesselStatus));
    ecs_add_pair(world, ecs_id(Vessel), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(Vessel), EcsWith, ecs_id(Rotation));
    ecs_add_pair(world, ecs_id(Vessel), EcsWith, DynamicTransform);

    HelmActionMap *helm = ecs_singleton_ensure(world, HelmActionMap);
    helm->scope = box_controller_actions_create(world, "helm", box_helm_actions,
        BoxHelmActionCount, helm->actions, helm->alts);
    box_controllers_ctor(world, ecs_id(Vessel), box_vessel_ctor, helm->scope);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "HelmActionsActivate" }),
        .phase = EcsPreFrame,
        .query.terms = {{ .id = ecs_id(Vessel), .inout = EcsIn }},
        .run = BoxHelmActionsActivate,
        .immediate = true
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "VesselUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(Vessel), .inout = EcsIn },
            { .id = ecs_id(VesselStatus), .inout = EcsInOut },
            { .id = ecs_id(Position), .inout = EcsInOut },
            { .id = ecs_id(Rotation), .inout = EcsInOut }
        },
        .callback = BoxVesselUpdate
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
