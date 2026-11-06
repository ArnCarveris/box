/**
 * @file command/command.c
 * @brief Registers the box.command module and runs command lines in shell sessions.
 */

#include "command.h"
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

/** The world of the page, the last one that imported box.command */
static ecs_world_t *box_command_web_world;

/** The session of box_command_web_run */
static ecs_entity_t box_command_web_session;

/** The output of the last box_command_web_run */
static char *box_command_web_output;

/** Run a command line in a session of the world of the page, the counterpart of the shell
 * endpoints of the REST API, which the browser does not have. The page calls it with
 * Module.cwrap('box_command_web_run', 'string', ['string']).
 *
 * @param line The command line
 * @return The output as box_command_run formats it, valid until the next call
 */
EMSCRIPTEN_KEEPALIVE const char* box_command_web_run(
    const char *line)
{
    ecs_world_t *world = box_command_web_world;
    ecs_os_free(box_command_web_output);
    box_command_web_output = NULL;
    if (!world) {
        goto done;
    }

    if (!box_command_web_session || !ecs_is_alive(world, box_command_web_session)) {
        box_command_web_session = ecs_shell_session_init(world,
            &(ecs_shell_session_desc_t){0});
    }

    box_command_web_output = box_command_run(world, box_command_web_session, line, NULL);
done:
    return box_command_web_output ? box_command_web_output : "";
}

/** Forget the world of the page when it is deleted.
 *
 * @param world The world
 * @param ctx Unused
 */
static void box_command_web_fini(
    ecs_world_t *world,
    void *ctx)
{
    (void)ctx;
    if (box_command_web_world == world) {
        box_command_web_world = NULL;
        box_command_web_session = 0;
    }
}
#endif

ecs_entity_t box_command_register(
    ecs_world_t *world,
    const char *brief,
    ecs_function_desc_t desc)
{
    desc.parent = ecs_lookup(world, BOX_COMMAND_SCOPE);
    ecs_entity_t result = ecs_function_init(world, &desc);
    ecs_assert(result != 0, ECS_INVALID_OPERATION, "failed to register command");
    ecs_doc_set_brief(world, result, brief);

    return result;
}

ecs_entity_t box_command_scope(
    const ecs_world_t *world)
{
    ecs_entity_t session = ecs_shell_get_session(world);
    const EcsCommandSession *s = session ? ecs_get(world, session, EcsCommandSession) : NULL;
    ecs_entity_t result = 0;
    if (s && s->scope && ecs_is_alive(world, s->scope)) {
        result = s->scope;
    }

    return result;
}

/** Path of the type of the elements that the help command of the shell returns */
#define BOX_COMMAND_INFO_TYPE "flecs.script.shell.CommandInfo"

/** Test whether a parameter repeats the one before it, an optional one with the same name.
 *
 * @param params The parameters
 * @param i The index of the parameter
 * @return Whether it repeats
 */
static bool box_command_param_repeats(
    const ecs_shell_param_info_t *params,
    int32_t i)
{
    return i > 0 && params[i].optional && params[i - 1].optional &&
        !ecs_os_strcmp(params[i].name, params[i - 1].name);
}

/** Append a command that help returned: its usage, its brief and the briefs of its parameters.
 * Optional parameters that repeat a name print once, as [name...].
 *
 * @param info The command
 * @param buf The buffer
 */
static void box_command_format_info(
    const ecs_shell_info_t *info,
    ecs_strbuf_t *buf)
{
    const ecs_shell_param_info_t *params = ecs_vec_first(&info->params);
    int32_t i, count = ecs_vec_count(&info->params);
    ecs_strbuf_appendstr(buf, info->name);
    for (i = 0; i < count; i ++) {
        if (box_command_param_repeats(params, i)) {
            continue;
        }

        ecs_strbuf_append(buf, params[i].optional ? " [%s%s]" : " <%s%s>", params[i].name,
            i + 1 < count && box_command_param_repeats(params, i + 1) ? "..." : "");
    }

    if (info->brief) {
        ecs_strbuf_append(buf, "  %s", info->brief);
    }

    for (i = 0; i < count; i ++) {
        if (params[i].brief && !box_command_param_repeats(params, i)) {
            ecs_strbuf_append(buf, "\n    %s: %s", params[i].name, params[i].brief);
        }
    }
}

char* box_command_resource_file(
    const ecs_world_t *world,
    ecs_entity_t session,
    const char *name)
{
    const EcsCommandSession *s = session && ecs_is_alive(world, session) ?
        ecs_get(world, session, EcsCommandSession) : NULL;
    char *result = NULL;
    if (s && s->resource_path) {
        result = flecs_asprintf("%s/%s", s->resource_path, name);
    }

    return result;
}

/** Append a value to a buffer as box_command_format formats it, a vector or array as one
 * element per line.
 *
 * @param world The world
 * @param type The type of the value
 * @param ptr The value
 * @param buf The buffer
 */
static void box_command_format_buf(
    const ecs_world_t *world,
    ecs_entity_t type,
    const void *ptr,
    ecs_strbuf_t *buf)
{
    const EcsVector *vt = ecs_get(world, type, EcsVector);
    const EcsArray *at = ecs_get(world, type, EcsArray);
    const ecs_type_info_t *ti = NULL;
    const void *elems = NULL;
    int32_t i, count = 0;
    if (type == ecs_lookup(world, BOX_COMMAND_INFO_TYPE)) {
        box_command_format_info(ptr, buf);
    } else if (type == ecs_id(ecs_string_t)) {
        const char *str = *(char* const*)ptr;
        ecs_strbuf_appendstr(buf, str ? str : "");
    } else if (type == ecs_id(ecs_entity_t)) {
        ecs_entity_t e = *(const ecs_entity_t*)ptr;
        char *path = e ? ecs_get_path(world, e) : ecs_os_strdup("#0");
        ecs_strbuf_appendstr(buf, path);
        ecs_os_free(path);
    } else if (vt || at) {
        type = vt ? vt->type : at->type;
        ti = ecs_get_type_info(world, type);
        elems = vt ? ((const ecs_vec_t*)ptr)->array : ptr;
        count = vt ? ((const ecs_vec_t*)ptr)->count : at->count;
        for (i = 0; ti && i < count; i ++) {
            if (i) {
                ecs_strbuf_appendch(buf, '\n');
            }

            box_command_format_buf(world, type, ECS_ELEM(elems, ti->size, i), buf);
        }
    } else if (ecs_ptr_to_json_buf(world, type, ptr, buf)) {
        ecs_strbuf_appendlit(buf, "null");
    }
}

char* box_command_format(
    const ecs_world_t *world,
    const ecs_value_t *value)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    if (value->type && value->ptr) {
        box_command_format_buf(world, value->type, value->ptr, &buf);
    }

    char *result = ecs_strbuf_get(&buf);

    return result ? result : ecs_os_strdup("");
}

const ecs_value_t* box_command_value(
    const ecs_shell_result_t *r)
{
    int32_t count = ecs_vec_count(&r->results);

    return count ? &ecs_vec_get_t(&r->results, ecs_value_t, count - 1)[0] : NULL;
}

char* box_command_run(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line,
    bool *error)
{
    ecs_shell_result_t r = ecs_shell_run(world, session, line);
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    char **errors = ecs_vec_first(&r.errors);
    ecs_value_t *results = ecs_vec_first(&r.results);
    int32_t i, count = ecs_vec_count(&r.errors);
    int32_t result_count = count ? 0 : ecs_vec_count(&r.results);

    for (i = 0; i < result_count; i ++) {
        if (!results[i].type || !results[i].ptr) {
            continue;
        }

        if (ecs_strbuf_written(&buf)) {
            ecs_strbuf_appendch(&buf, '\n');
        }

        box_command_format_buf(world, results[i].type, results[i].ptr, &buf);
    }

    for (i = 0; i < count; i ++) {
        if (ecs_strbuf_written(&buf)) {
            ecs_strbuf_appendch(&buf, '\n');
        }

        ecs_strbuf_append(&buf, BOX_COMMAND_ERROR "%s", errors[i]);
    }

    if (error) {
        error[0] = count != 0;
    }

    ecs_shell_result_fini(world, &r);
    char *result = ecs_strbuf_get(&buf);

    return result ? result : ecs_os_strdup("");
}

/** Add a scope to the front of the command paths of the shell.
 *
 * @param world The world
 * @param scope The scope
 */
static void box_command_path_prepend(
    ecs_world_t *world,
    ecs_entity_t scope)
{
    EcsScriptShell *shell = ecs_singleton_ensure(world, EcsScriptShell);
    ecs_vec_append_t(NULL, &shell->command_paths, ecs_entity_t);
    ecs_entity_t *paths = ecs_vec_first_t(&shell->command_paths, ecs_entity_t);
    int32_t count = ecs_vec_count(&shell->command_paths);
    memmove(&paths[1], paths, (size_t)(count - 1) * sizeof(ecs_entity_t));
    paths[0] = scope;
    ecs_singleton_modified(world, EcsScriptShell);
}

void BoxCommandImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxCommand);
    ECS_IMPORT(world, BoxApp);
    ECS_IMPORT(world, FlecsScriptShell);

    box_command_path_prepend(world, ecs_entity(world, { .name = "commands" }));

    box_logs_init(world);
    box_cmd_input_init(world);
    box_cmd_render_init(world);
    box_cmd_fit_init(world);
    box_cmd_audio_init(world);
    box_cmd_frames_init(world);
    box_cmd_edit_init(world);
#ifdef __EMSCRIPTEN__
    box_command_web_world = world;
    box_command_web_session = 0;
    ecs_atfini(world, box_command_web_fini, NULL);
#endif
}
