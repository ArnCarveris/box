/**
 * @file command/builtin.c
 * @brief The help command and the commands command that lists the session log.
 */

#include "command_internal.h"
#include <string.h>
#include <stdlib.h>

/** Compare two command names.
 *
 * @param a The first name
 * @param b The second name
 * @return Negative when a sorts first, positive when b does, 0 when equal
 */
static int box_command_compare(
    const void *a,
    const void *b)
{
    return strcmp(*(const char**)a, *(const char**)b);
}

/** Append the signature and brief of a command to a buffer.
 *
 * @param world The world
 * @param cmd The command entity
 * @param buf The buffer
 */
static void box_command_signature(
    ecs_world_t *world,
    ecs_entity_t cmd,
    ecs_strbuf_t *buf)
{
    const EcsScriptFunction *f = ecs_get(world, cmd, EcsScriptFunction);
    const CommandInfo *info = ecs_get(world, cmd, CommandInfo);
    ecs_assert(f != NULL, ECS_INVALID_OPERATION, "command is not a function");
    ecs_assert(info != NULL, ECS_INVALID_OPERATION, "command without parameter info");

    ecs_strbuf_appendstr(buf, ecs_get_name(world, cmd));

    const ecs_script_parameter_t *params = ecs_vec_first(&f->params);
    int32_t i, count = ecs_vec_count(&f->params);
    for (i = 1; i < count; i ++) {
        const char *type = ecs_get_name(world, params[i].type);
        if (i <= info->required) {
            ecs_strbuf_append(buf, " <%s:%s>", params[i].name, type);
        } else {
            ecs_strbuf_append(buf, " [%s:%s]", params[i].name, type);
        }
    }

    const char *brief = ecs_doc_get_brief(world, cmd);
    ecs_strbuf_append(buf, " - %s", brief ? brief : "");
}

/** The help command: list every command with its parameters and brief.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_command_help(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    ecs_world_t *world = ctx->world;
    ecs_entity_t scope = box_command_scope(world);
    ecs_vec_t names;
    ecs_vec_init_t(NULL, &names, const char*, 0);

    ecs_iter_t it = ecs_children(world, scope);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const char **name = ecs_vec_append_t(NULL, &names, const char*);
            name[0] = ecs_get_name(world, it.entities[i]);
        }
    }

    int32_t i, count = ecs_vec_count(&names);
    qsort(ecs_vec_first(&names), (size_t)count, sizeof(const char*),
        box_command_compare);

    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    for (i = 0; i < count; i ++) {
        const char *name = ecs_vec_get_t(&names, const char*, i)[0];
        if (i) {
            ecs_strbuf_appendstr(&buf, "\n");
        }

        box_command_signature(world, ecs_lookup_child(world, scope, name), &buf);
    }

    ecs_vec_fini_t(NULL, &names, const char*);
    *(char**)result->ptr = ecs_strbuf_get(&buf);
}

/** The commands command: list the last command lines and results of a session.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_command_commands(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const TerminalSession *s = ecs_get(ctx->world, session, TerminalSession);
    ecs_assert(s != NULL, ECS_INVALID_PARAMETER, "entity is not a terminal session");

    int32_t count = *(ecs_i32_t*)argv[1].ptr;
    if (count <= 0) {
        count = BOX_COMMAND_LOG_DEFAULT;
    }

    if (count > BOX_COMMAND_LOG_MAX) {
        count = BOX_COMMAND_LOG_MAX;
    }

    int32_t i = s->count - count;
    if (i < 0) {
        i = 0;
    }

    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    for (; i < s->count; i ++) {
        const box_command_entry_t *entry = &s->ring[i % BOX_COMMAND_LOG_MAX];
        ecs_strbuf_append(&buf, "$ %s\n%s\n", entry->line, entry->result);
    }

    *(char**)result->ptr = ecs_strbuf_get(&buf);
}

void box_command_builtin_init(
    ecs_world_t *world)
{
    box_command_register(world, "help",
        "List every command with its parameters", NULL, 0, box_command_help);

    box_command_param_t params[] = {{
        .name = "count", .type = ecs_id(ecs_i32_t), .optional = true
    }};

    box_command_register(world, "commands",
        "Show the last command lines and results of the session", params, 1,
        box_command_commands);
}
