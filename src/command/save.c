/**
 * @file command/save.c
 * @brief The save command: write the values of the scene back to the scripts that declare them.
 *
 * Every managed script that is not framework state is updated from the current
 * component values of its entities with ecs_script_from_scene.
 *
 * A script that changed is written to its file with ecs_script_save.
 */

#include "command_internal.h"
#include "../checkpoint/checkpoint_internal.h"

/** Name a script by its file, or by its entity when it was not loaded from one.
 *
 * @param world The world
 * @param script The script entity
 * @return The name, the caller frees it
 */
static char* box_save_script_name(
    ecs_world_t *world,
    ecs_entity_t script)
{
    const EcsScript *code = ecs_get(world, script, EcsScript);
    char *result = NULL;
    if (code && code->filename) {
        result = ecs_os_strdup(code->filename);
    } else {
        result = ecs_get_path(world, script);
    }

    return result;
}

/** Update one script from the scene and write it to its file when it changed.
 *
 * @param world The world
 * @param script The script entity
 * @param out Receives a line for a script that changed or failed
 * @return 1 when the script changed, 0 when it did not, -1 when it failed
 */
static int box_save_script(
    ecs_world_t *world,
    ecs_entity_t script,
    ecs_strbuf_t *out)
{
    char *name = box_save_script_name(world, script);
    int result = ecs_script_from_scene(world, script);
    if (result == -1) {
        ecs_strbuf_append(out, "failed to update %s from the scene\n", name);
        goto done;
    }

    if (result == 0) {
        goto done;
    }

    const EcsScript *code = ecs_get(world, script, EcsScript);
    if (!code || !code->filename) {
        ecs_strbuf_append(out, "updated %s, no file to save to\n", name);
        goto done;
    }

    if (ecs_script_save(world, script)) {
        ecs_strbuf_append(out, "failed to write %s\n", name);
        result = -1;
        goto done;
    }

    ecs_strbuf_append(out, "saved %s\n", name);
done:
    ecs_os_free(name);

    return result;
}

/** The save command: write the scene to the scripts it was loaded from.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_save_command(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    ecs_world_t *world = ctx->world;
    ecs_vec_t scripts;
    ecs_vec_init_t(NULL, &scripts, ecs_entity_t, 0);
    box_checkpoint_managed(world, &scripts);

    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t i, count = ecs_vec_count(&scripts), changed = 0, failed = 0;
    ecs_entity_t *array = ecs_vec_first_t(&scripts, ecs_entity_t);
    for (i = 0; i < count; i ++) {
        int status = box_save_script(world, array[i], &buf);
        changed += status == 1;
        failed += status == -1;
    }

    ecs_vec_fini_t(NULL, &scripts, ecs_entity_t);

    char *lines = ecs_strbuf_get(&buf);
    char *out = NULL;
    if (failed) {
        out = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "%s", lines);
    } else if (!changed) {
        out = ecs_os_strdup("no changes\n");
    } else {
        out = ecs_os_strdup(lines);
    }

    ecs_os_free(lines);

    *(char**)result->ptr = out;
}

void box_save_cmd_init(
    ecs_world_t *world)
{
    box_command_register(world, "save",
        "Write the current values of the scene back to the script files that declare them",
        NULL, 0, box_save_command);
}
