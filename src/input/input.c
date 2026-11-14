/**
 * @file input/input.c
 * @brief Registers the box.input module: the device singletons, the screen target and the systems.
 */

#define BOX_INPUT_IMPL
#include "input.h"

ECS_COMPONENT_DECLARE(InputDevice);

/** Release the queues and the backend of the device state.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_input_device_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    InputDevice *dev = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        if (dev[i].backend.free) {
            dev[i].backend.free(dev[i].backend.ctx);
        }

        box_input_events_clear(&dev[i].platform);
        box_input_events_clear(&dev[i].queue);
        ecs_vec_fini_t(NULL, &dev[i].platform, InputEvent);
        ecs_vec_fini_t(NULL, &dev[i].queue, InputEvent);
        ecs_os_free(dev[i].clipboard);
        ecs_vec_fini_t(NULL, &dev[i].emits, box_input_emit_t);
        ecs_vec_fini_t(NULL, &dev[i].holds, box_input_hold_t);
        ecs_vec_fini_t(NULL, &dev[i].text, char);
        ecs_vec_fini_t(NULL, &dev[i].surfaces, box_input_surface_t);
    }
}

InputDevice* box_input_device(
    ecs_world_t *world)
{
    ecs_world_t *real = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(world));
    InputDevice *result = ecs_singleton_get_mut(real, InputDevice);
    ecs_assert(result != NULL, ECS_INVALID_OPERATION, "box.input is not imported");

    return result;
}

void box_input_platform_set(
    ecs_world_t *world,
    const box_input_platform_t *platform)
{
    InputDevice *dev = box_input_device(world);
    box_input_capture_release(world, dev);
    if (dev->backend.free) {
        dev->backend.free(dev->backend.ctx);
    }

    ecs_os_zeromem(&dev->backend);
    dev->cursor_applied = CursorDefault;
    if (platform) {
        dev->backend = *platform;
        box_input_cursor_sync(world, dev);
    }
}

void box_input_router_set(
    ecs_world_t *world,
    const box_input_router_t *router)
{
    InputDevice *dev = box_input_device(world);
    ecs_os_zeromem(&dev->router);
    if (router) {
        dev->router = *router;
    }
}

void box_input_claim_set(
    ecs_world_t *world,
    box_input_claim_t claim,
    void *ctx)
{
    InputDevice *dev = box_input_device(world);
    dev->claim = claim;
    dev->claim_ctx = claim ? ctx : NULL;
}

ecs_entity_t box_input_screen(
    ecs_world_t *world)
{
    return box_input_device(world)->screen;
}

/** Make the screen the entity that received the last press, so keyboard events reach it.
 *
 * @param world The world
 * @param screen The screen entity
 */
static void box_input_focus_screen(
    ecs_world_t *world,
    ecs_entity_t screen)
{
    ecs_script_mouse_event(world, &(EcsScriptMouseEvent){ .target = screen, .buttons = 1 });
    ecs_script_mouse_event(world, &(EcsScriptMouseEvent){ .target = screen });
}

/** Import box.input. Its events go out through flecs.script.input, which flecs releases that
 * predate the rename call flecs.script.event.
 *
 * @param world The world
 */
void BoxInputImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxInput);
    ECS_IMPORT(world, BoxTypes);
#if defined(FLECS_SCRIPT_INPUT) || !defined(FLECS_SCRIPT_EVENT_H)
    ECS_IMPORT(world, FlecsScriptInput);
#else
    ECS_IMPORT(world, FlecsScriptEvent);
#endif

    ECS_META_COMPONENT(world, Key);
    ECS_META_COMPONENT(world, KeyState);
    ecs_set_name_prefix(world, "Input");
    ECS_META_COMPONENT(world, InputEventKind);
    ecs_set_name_prefix(world, NULL);
    ECS_META_COMPONENT(world, PointerCursor);
    ecs_set_name_prefix(world, "Input");
    ECS_META_COMPONENT(world, InputEvent);
    ecs_set_name_prefix(world, NULL);
    ECS_META_COMPONENT(world, Mouse);
    ECS_META_COMPONENT(world, Input);
    ECS_META_COMPONENT(world, Action);
    box_owned_hooks(world, Action, BOX_OWNED_STR(Action, description));
    ECS_META_COMPONENT(world, ActionState);
    ECS_META_COMPONENT(world, Binding);
    ECS_META_COMPONENT(world, ActionEvent);
    ecs_set_name_prefix(world, "Input");
    ECS_COMPONENT_DEFINE(world, InputDevice);
    ecs_set_name_prefix(world, NULL);

    box_owned_hooks(world, Input, BOX_OWNED_STR(Input, text), BOX_OWNED_STR(Input, paste));
    box_owned_hooks(world, InputEvent, BOX_OWNED_STR(InputEvent, text));
    ecs_set_hooks(world, InputDevice, {
        .ctor = flecs_default_ctor,
        .dtor = box_input_device_dtor,
        .flags = ECS_TYPE_HOOK_COPY_ILLEGAL | ECS_TYPE_HOOK_MOVE_ILLEGAL
    });

    ecs_entity_t screen = ecs_entity(world, { .name = "screen" });
    InputDevice *dev = ecs_singleton_ensure(world, InputDevice);
    dev->screen = screen;
    ecs_vec_init_t(NULL, &dev->platform, InputEvent, 0);
    ecs_vec_init_t(NULL, &dev->queue, InputEvent, 0);
    ecs_vec_init_t(NULL, &dev->emits, box_input_emit_t, 0);
    ecs_vec_init_t(NULL, &dev->holds, box_input_hold_t, 0);
    ecs_vec_init_t(NULL, &dev->text, char, 0);
    ecs_vec_init_t(NULL, &dev->surfaces, box_input_surface_t, 0);

    Input *in = ecs_singleton_ensure(world, Input);
    in->focused = true;
    in->text = ecs_os_strdup("");
    ecs_singleton_modified(world, Input);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Update" }),
        .phase = EcsOnLoad,
        .run = BoxInputUpdate,
        .immediate = true
    });

    box_input_actions_import(world);
    box_input_focus_screen(world, screen);
}
