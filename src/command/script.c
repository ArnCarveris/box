/**
 * @file command/script.c
 * @brief Registers the script command group that reads and edits loaded scripts.
 */

#include "command_internal.h"
#include "../checkpoint/checkpoint_internal.h"
#include <string.h>

/** Test whether a script has a filename and can be updated.
 *
 * A script the framework loaded under one of its modules, like the widget
 * library of box.ui, is not part of the scene and stays out of the commands.
 *
 * @param world The world
 * @param e The script entity
 * @param script The script component
 * @return true when the script has a filename, is not a template and is not framework state
 */
static bool box_script_editable(
    const ecs_world_t *world,
    ecs_entity_t e,
    const EcsScript *script)
{
    return script->filename != NULL && script->template_ == NULL &&
        !box_checkpoint_in_module(world, e);
}

/** Get the file name of a path without its folders.
 *
 * @param path The path
 * @return The first character after the last slash
 */
static const char* box_script_basename(
    const char *path)
{
    const char *result = strrchr(path, '/');

    return result ? result + 1 : path;
}

/** Find the script whose filename matches a path exactly or by basename.
 *
 * @param world The world
 * @param path The filename or basename
 * @param ambiguous Set to true when more than one basename matches
 * @return The script entity, 0 when there is no unique match
 */
static ecs_entity_t box_script_find(
    ecs_world_t *world,
    const char *path,
    bool *ambiguous)
{
    ecs_entity_t result = 0, basename_match = 0;
    int32_t basename_count = 0;
    const char *base = box_script_basename(path);
    ecs_iter_t it = ecs_each(world, EcsScript);
    while (ecs_each_next(&it)) {
        const EcsScript *script = ecs_field(&it, EcsScript, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (!box_script_editable(world, it.entities[i], &script[i])) {
                continue;
            }

            if (!strcmp(script[i].filename, path)) {
                result = it.entities[i];
            } else if (!strcmp(box_script_basename(script[i].filename), base)) {
                basename_match = it.entities[i];
                basename_count ++;
            }
        }
    }

    if (!result && basename_count == 1) {
        result = basename_match;
    }

    ambiguous[0] = !result && basename_count > 1;

    return result;
}

/** Build the message for a path that does not match a single script.
 *
 * @param path The requested path
 * @param ambiguous More than one script matched the basename
 * @return The message, the caller frees it
 */
static char* box_script_unknown(
    const char *path,
    bool ambiguous)
{
    char *result = NULL;
    if (ambiguous) {
        result = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "ambiguous script '%s'", path);
    } else {
        result = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "unknown script '%s'", path);
    }

    return result;
}

/** Replace the source of a script, keeping the old source when it fails.
 *
 * @param world The world
 * @param script The script entity
 * @param code The new source
 * @return "ok" or the error of the script, the caller frees it
 */
static char* box_script_update(
    ecs_world_t *world,
    ecs_entity_t script,
    const char *code)
{
    char *result = NULL;
    const EcsScript *current = ecs_get(world, script, EcsScript);
    char *old = ecs_os_strdup(current->code ? current->code : "");
    if (!ecs_script_update(world, script, 0, code)) {
        result = ecs_os_strdup("ok");
        goto done;
    }

    EcsScript *updated = ecs_get_mut(world, script, EcsScript);
    ecs_assert(updated != NULL, ECS_INVALID_OPERATION, "script was deleted");
    result = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "%s",
        updated->error ? updated->error : "failed to update");
    ecs_os_strset(&updated->code, old);

done:
    ecs_os_free(old);

    return result;
}

/** Append the lines of a script that contain a text as file:line:text.
 *
 * @param buf The output buffer
 * @param filename Name shown in front of the line number
 * @param code The script source
 * @param text The text to look for
 */
static void box_script_grep_lines(
    ecs_strbuf_t *buf,
    const char *filename,
    const char *code,
    const char *text)
{
    const char *p = code;
    int32_t line = 1;
    while (p[0]) {
        const char *end = strchr(p, '\n');
        const char *found = strstr(p, text);
        int32_t len = end ? (int32_t)(end - p) : (int32_t)strlen(p);
        if (found && (int32_t)(found - p) < len) {
            ecs_strbuf_append(buf, "%s:%d:", filename, line);
            ecs_strbuf_appendstrn(buf, p, len);
            ecs_strbuf_appendch(buf, '\n');
        }

        if (!end) {
            break;
        }

        p = end + 1;
        line ++;
    }
}

/** The script_read command: print the source of a loaded script.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_script_read(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    const char *path = *(char**)argv[1].ptr;
    bool ambiguous = false;
    ecs_entity_t script = box_script_find(world, path, &ambiguous);
    char *out = NULL;
    if (!script) {
        out = box_script_unknown(path, ambiguous);
    } else {
        const EcsScript *code = ecs_get(world, script, EcsScript);
        out = ecs_os_strdup(code->code ? code->code : "");
    }

    *(char**)result->ptr = out;
}

/** The script_write command: replace the source of a loaded script.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_script_write(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    const char *path = *(char**)argv[1].ptr;
    const char *code = *(char**)argv[2].ptr;
    bool ambiguous = false;
    ecs_entity_t script = box_script_find(world, path, &ambiguous);
    char *out = NULL;
    if (!script) {
        out = box_script_unknown(path, ambiguous);
    } else {
        out = box_script_update(world, script, code ? code : "");
    }

    *(char**)result->ptr = out;
}

/** The script_grep command: print matching lines of every loaded script.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_script_grep(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    const char *text = *(char**)argv[1].ptr;
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_iter_t it = ecs_each(world, EcsScript);
    while (ecs_each_next(&it)) {
        const EcsScript *script = ecs_field(&it, EcsScript, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (!box_script_editable(world, it.entities[i], &script[i]) || !script[i].code) {
                continue;
            }

            box_script_grep_lines(
                &buf, script[i].filename, script[i].code, text ? text : "");
        }
    }

    char *out = ecs_strbuf_get(&buf);
    *(char**)result->ptr = out ? out : ecs_os_strdup("");
}

/** The script_patch command: apply a unified diff to a loaded script.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_script_patch(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    const char *path = *(char**)argv[1].ptr;
    const char *patch = *(char**)argv[2].ptr;
    bool ambiguous = false;
    ecs_entity_t script = box_script_find(world, path, &ambiguous);
    char *out = NULL;
    if (!script) {
        out = box_script_unknown(path, ambiguous);
    } else {
        const EcsScript *code = ecs_get(world, script, EcsScript);
        char *error = NULL;
        char *patched = box_patch_apply(
            code->code ? code->code : "", patch ? patch : "", &error);
        if (!patched) {
            out = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "%s", error);
            ecs_os_free(error);
        } else {
            out = box_script_update(world, script, patched);
            ecs_os_free(patched);
        }
    }

    *(char**)result->ptr = out;
}

void box_script_init(
    ecs_world_t *world)
{
    box_command_param_t path[] = {
        { .name = "path", .type = ecs_id(ecs_string_t) }
    };
    box_command_register(world, "script_read",
        "Print the source of a loaded script", path, 1, box_script_read);

    box_command_param_t write[] = {
        { .name = "path", .type = ecs_id(ecs_string_t) },
        { .name = "code", .type = ecs_id(ecs_string_t) }
    };
    box_command_register(world, "script_write",
        "Replace the source of a loaded script", write, 2, box_script_write);

    box_command_param_t text[] = {
        { .name = "text", .type = ecs_id(ecs_string_t) }
    };
    box_command_register(world, "script_grep",
        "Print the lines of every loaded script that match a text", text, 1,
        box_script_grep);

    box_command_param_t patch[] = {
        { .name = "path", .type = ecs_id(ecs_string_t) },
        { .name = "patch", .type = ecs_id(ecs_string_t) }
    };
    box_command_register(world, "script_patch",
        "Apply a unified diff to a loaded script", patch, 2, box_script_patch);
}
