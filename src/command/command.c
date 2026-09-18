/**
 * @file command/command.c
 * @brief Registers the box.command module, its scopes and its command functions.
 */

#define BOX_COMMAND_IMPL
#include "command_internal.h"

ECS_COMPONENT_DECLARE(TerminalSession);
ECS_COMPONENT_DECLARE(CommandInfo);

/** TerminalSession destructor: close the log and remove the session folder.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_command_session_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    TerminalSession *session = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        box_terminal_session_fini(&session[i]);
    }
}

ecs_entity_t box_command_scope(
    const ecs_world_t *world)
{
    ecs_entity_t result = ecs_lookup(world, "box.command.commands");
    ecs_assert(result != 0, ECS_INVALID_OPERATION, "import box.command first");

    return result;
}

ecs_entity_t box_command_register(
    ecs_world_t *world,
    const char *name,
    const char *brief,
    const box_command_param_t *params,
    int32_t count,
    ecs_function_callback_t callback)
{
    ecs_assert(count < FLECS_SCRIPT_FUNCTION_ARGS_MAX - 1,
        ECS_INVALID_PARAMETER, "too many command parameters");

    ecs_function_desc_t desc = {
        .name = name,
        .parent = box_command_scope(world),
        .return_type = ecs_id(ecs_string_t),
        .callback = callback
    };

    desc.params[0].name = "session";
    desc.params[0].type = ecs_id(ecs_entity_t);

    int32_t i, required = 0;
    for (i = 0; i < count; i ++) {
        desc.params[i + 1].name = params[i].name;
        desc.params[i + 1].type = params[i].type;
        if (!params[i].optional) {
            ecs_assert(required == i, ECS_INVALID_PARAMETER,
                "optional command parameters must come last");
            required ++;
        }
    }

    ecs_entity_t result = ecs_function_init(world, &desc);
    ecs_assert(result != 0, ECS_INVALID_OPERATION, "failed to register command");
    ecs_set(world, result, CommandInfo, { .required = required });
    ecs_doc_set_brief(world, result, brief);

    return result;
}

void BoxCommandImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxCommand);
    ECS_IMPORT(world, BoxCore);

    ECS_META_COMPONENT(world, Terminal);
    ECS_META_COMPONENT(world, TerminalInput);
    ECS_META_COMPONENT(world, TerminalOutput);
    ECS_META_COMPONENT(world, CommandScript);
    ECS_COMPONENT_DEFINE(world, TerminalSession);
    ECS_COMPONENT_DEFINE(world, CommandInfo);

    ecs_set_hooks(world, TerminalSession, {
        .ctor = flecs_default_ctor,
        .dtor = box_command_session_dtor
    });
    ecs_add_pair(world, ecs_id(TerminalSession), EcsOnInstantiate, EcsDontInherit);

    ecs_entity_t module = ecs_set_scope(world, 0);
    ecs_entity(world, { .name = "terminals" });
    ecs_set_scope(world, module);
    ecs_entity(world, { .name = "commands" });

    box_terminal_init(world);
    box_dispatch_init(world);
    box_command_script_init(world);
    box_command_builtin_init(world);
    box_shell_init(world);
    box_search_init(world);
    box_logs_init(world);
    box_script_init(world);
    box_capture_init(world);
    box_http_init(world);
    box_wasm_init(world);
}
