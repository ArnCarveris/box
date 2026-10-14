/**
 * @file command/history.c
 * @brief The commands command, which lists the lines a session ran and their results.
 */

#include "command.h"

/** The commands command: list the last count lines of the session, all when omitted.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session and the count
 * @param result Receives the output
 */
static void box_history_commands(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    int32_t count = *(ecs_i32_t*)argv[1].ptr;
    const TerminalSession *s = ecs_get(ctx->world, session, TerminalSession);
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t i, total;
    if (!s) {
        box_command_error(ctx, result, "not a terminal session");
        goto done;
    }

    total = ecs_vec_count(&s->entries);
    i = count > 0 && count < total ? total - count : 0;
    for (; i < total; i ++) {
        const box_command_entry_t *e = ecs_vec_get_t(&s->entries, box_command_entry_t, i);
        if (ecs_strbuf_written(&buf)) {
            ecs_strbuf_appendch(&buf, '\n');
        }

        ecs_strbuf_append(&buf, "$ %s", e->line);
        if (e->result && e->result[0]) {
            ecs_strbuf_append(&buf, "\n%s", e->result);
        }
    }

    *(char**)result->ptr = ecs_strbuf_get(&buf);
done:
    return;
}

void box_history_init(
    ecs_world_t *world)
{
    box_command_register(world, &(box_command_desc_t){
        .name = "commands",
        .brief = "Show the last count command lines and results, all when omitted",
        .params = {{ .name = "count", .type = ecs_id(ecs_i32_t), .optional = true }},
        .callback = box_history_commands,
        .quiet = true
    });
}
