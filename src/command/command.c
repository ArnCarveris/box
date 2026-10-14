/**
 * @file command/command.c
 * @brief Registers the box.command module, its components and the command functions.
 */

#define BOX_COMMAND_IMPL
#include "command.h"
#include <string.h>

ECS_COMPONENT_DECLARE(TerminalSession);
ECS_COMPONENT_DECLARE(CommandInfo);
ECS_COMPONENT_DECLARE(TerminalHost);

/** Close the log, remove the folder and free the entries of sessions.
 *
 * @param ptr The sessions
 * @param count Number of sessions
 * @param ti Type info
 */
static void box_terminal_session_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    TerminalSession *s = ptr;
    int32_t i, j;
    for (i = 0; i < count; i ++) {
        if (s[i].log) {
            fclose(s[i].log);
        }

        if (s[i].dir) {
            box_app_rmtree(s[i].dir);
            ecs_os_free(s[i].dir);
        }

        box_command_entry_t *entries = ecs_vec_first(&s[i].entries);
        for (j = 0; j < ecs_vec_count(&s[i].entries); j ++) {
            ecs_os_free(entries[j].line);
            ecs_os_free(entries[j].result);
        }

        ecs_vec_fini_t(NULL, &s[i].entries, box_command_entry_t);
        ecs_os_memset_t(&s[i], 0, TerminalSession);
    }
}

/** Move sessions without closing their log or removing their folder.
 *
 * @param dst_ptr The destination sessions
 * @param src_ptr The source sessions
 * @param count Number of sessions
 * @param ti Type info
 */
static void box_terminal_session_move(
    void *dst_ptr,
    void *src_ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    box_terminal_session_dtor(dst_ptr, count, ti);
    ecs_os_memcpy(dst_ptr, src_ptr, count * ti->size);
    ecs_os_memset(src_ptr, 0, count * ti->size);
}

/** Stop the server and free the host; the session root is removed with the temporary folder.
 *
 * @param world The world
 * @param ctx The host
 */
static void box_terminal_host_fini(
    ecs_world_t *world,
    void *ctx)
{
    (void)world;
    box_terminal_host_t *host = ctx;
    box_endpoint_fini(host);
    ecs_os_free(host->root);

    ecs_os_free(host);
}

box_terminal_host_t* box_terminal_host(
    const ecs_world_t *world)
{
    const TerminalHost *th = ecs_singleton_get(world, TerminalHost);
    ecs_assert(th != NULL, ECS_INVALID_OPERATION, "box.command is not imported");

    return th->host;
}

ecs_entity_t box_command_register(
    ecs_world_t *world,
    const box_command_desc_t *desc)
{
    ecs_function_desc_t fdesc = {
        .name = desc->name,
        .parent = ecs_lookup(world, BOX_COMMAND_SCOPE),
        .return_type = ecs_id(ecs_string_t),
        .callback = desc->callback,
        .ctx = desc->ctx
    };

    CommandInfo info = { .quiet = desc->quiet };
    int32_t i;
    fdesc.params[0].name = "session";
    fdesc.params[0].type = ecs_id(ecs_entity_t);
    for (i = 0; i < FLECS_SCRIPT_FUNCTION_ARGS_MAX - 1 && desc->params[i].name; i ++) {
        const box_command_param_t *p = &desc->params[i];
        ecs_assert(!info.rest, ECS_INVALID_PARAMETER, "rest parameter must be last");
        ecs_assert(p->optional || info.required == i, ECS_INVALID_PARAMETER,
            "optional parameters must be last");
        ecs_assert(!p->rest || p->type == ecs_id(ecs_string_t), ECS_INVALID_PARAMETER,
            "rest parameter must be a string");
        fdesc.params[i + 1].name = p->name;
        fdesc.params[i + 1].type = p->type;
        info.required += !p->optional;
        info.rest = p->rest;
    }

    ecs_entity_t result = ecs_function_init(world, &fdesc);
    ecs_assert(result != 0, ECS_INVALID_OPERATION, "failed to register command");
    ecs_set_ptr(world, result, CommandInfo, &info);
    if (desc->brief) {
        ecs_doc_set_brief(world, result, desc->brief);
    }

    return result;
}

void BoxCommandImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxCommand);
    ECS_IMPORT(world, BoxApp);

    ECS_META_COMPONENT(world, Terminal);
    ECS_META_COMPONENT(world, TerminalStatus);
    box_owned_hooks(world, Terminal, BOX_OWNED_STR(Terminal, cwd));
    box_owned_hooks(world, TerminalStatus, BOX_OWNED_STR(TerminalStatus, dir));
    ECS_META_COMPONENT(world, TerminalServer);
    ECS_COMPONENT_DEFINE(world, TerminalSession);
    ECS_COMPONENT_DEFINE(world, CommandInfo);
    ECS_COMPONENT_DEFINE(world, TerminalHost);

    ecs_set_hooks(world, TerminalSession, {
        .ctor = flecs_default_ctor,
        .dtor = box_terminal_session_dtor,
        .move = box_terminal_session_move
    });

    ecs_add_pair(world, ecs_id(TerminalSession), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(TerminalStatus), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(TerminalHost), EcsOnInstantiate, EcsDontInherit);

    ecs_entity(world, { .name = "commands" });
    ecs_entity(world, { .name = "terminals" });

    box_terminal_host_t *host = ecs_os_calloc_t(box_terminal_host_t);
    host->world = world;
    ecs_singleton_set(world, TerminalHost, { .host = host });
    ecs_atfini(world, box_terminal_host_fini, host);

    box_terminal_init(world, host);
    box_endpoint_init(world, host);
    box_history_init(world);
    box_logs_init(world);
    box_shell_init(world);
    box_search_init(world);
    box_cmd_script_init(world);
    box_cmd_input_init(world);
    box_cmd_render_init(world);
}
