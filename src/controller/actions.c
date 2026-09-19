/**
 * @file controller/actions.c
 * @brief The default actions of the controllers and the bindings they come with.
 */

#include "controller_internal.h"

/** One action the module creates with the key mask it binds by default */
typedef struct box_controller_bind_t {
    const char *name;        /**< Name of the action under the actions scope */
    const char *description; /**< What the action does */
    uint64_t mask;           /**< Key mask of the default binding */
} box_controller_bind_t;

ecs_entity_t box_controller_action(
    ecs_world_t *world,
    const char *name,
    const char *description,
    uint64_t mask)
{
    ecs_entity_t scope = ecs_lookup(world, "actions");
    ecs_assert(scope != 0, ECS_INVALID_OPERATION, "box.input is not imported");
    ecs_entity_t action = ecs_entity(world, { .name = name, .parent = scope });
    if (!ecs_has(world, action, InputAction)) {
        ecs_set(world, action, InputAction, {
            .description = ecs_os_strdup(description) });
    }

    uint64_t bound[1];
    if (!box_input_bindings(world, action, bound, 1)) {
        ecs_entity_t map = ecs_lookup(world, "box.input.map");
        ecs_entity_t binding = ecs_entity(world, {
            .name = name, .parent = map });
        ecs_set(world, binding, Binding, { .mask = mask, .action = action });
    }

    return action;
}

bool box_controller_pressed(
    ecs_world_t *world,
    ecs_entity_t action)
{
    const InputAction *ptr = action ? ecs_get(world, action, InputAction) : NULL;

    return ptr && ptr->pressed;
}

bool box_controller_held(
    ecs_world_t *world,
    ecs_entity_t action)
{
    const InputAction *ptr = action ? ecs_get(world, action, InputAction) : NULL;

    return ptr && ptr->held;
}

void box_controller_clearance(
    ecs_world_t *world,
    ecs_entity_t terrain,
    float clearance,
    Position *p,
    vec3 *velocity)
{
    const AABB *bounds = terrain && ecs_is_alive(world, terrain)
        ? ecs_get(world, terrain, AABB)
        : NULL;
    if (bounds) {
        float floor = bounds->max.y + clearance;
        if (p->y < floor) {
            p->y = floor;
            if (velocity->y < 0) {
                velocity->y = 0;
            }
        }
    }
}

void box_controller_actions_init(
    ecs_world_t *world)
{
    static const box_controller_bind_t binds[] = {
        {"Forward", "Move along the view direction", BoxKeyW},
        {"Back", "Move against the view direction", BoxKeyS},
        {"Left", "Strafe left", BoxKeyA},
        {"Right", "Strafe right", BoxKeyD},
        {"Up", "Move up", BoxKeyE},
        {"Down", "Move down", BoxKeyQ},
        {"Fast", "Move faster while held", BoxKeyShift},
        {"Look", "Turn with the cursor while held", BoxKeyMouseRight},
        {"Jump", "Jump from the ground", BoxKeySpace},
        {"Crouch", "Lower the eye while held", BoxKeyCtrl},
        {"Run", "Walk at the run speed while held", BoxKeyShift},
        {"Fly", "Toggle free flight", BoxKeyF},
        {"ZoomIn", "Step the follow camera one preset closer", BoxKeyEqual},
        {"ZoomOut", "Step the follow camera one preset further", BoxKeyMinus}
    };

    ecs_entity_t created[14];
    int32_t i, count = (int32_t)(sizeof(binds) / sizeof(binds[0]));
    for (i = 0; i < count; i ++) {
        created[i] = box_controller_action(
            world, binds[i].name, binds[i].description, binds[i].mask);
    }

    ControllerActions *actions = ecs_singleton_ensure(world, ControllerActions);
    actions->forward = created[0];
    actions->back = created[1];
    actions->left = created[2];
    actions->right = created[3];
    actions->up = created[4];
    actions->down = created[5];
    actions->fast = created[6];
    actions->look = created[7];
    actions->jump = created[8];
    actions->crouch = created[9];
    actions->run = created[10];
    actions->fly = created[11];
    actions->zoom_in = created[12];
    actions->zoom_out = created[13];
    ecs_singleton_modified(world, ControllerActions);
}
