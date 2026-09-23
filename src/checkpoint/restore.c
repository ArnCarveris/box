/**
 * @file checkpoint/restore.c
 * @brief Reloads the script files of a checkpoint folder into a running world.
 *
 * Reads and parses the file of every managed script in the folder, then hands
 * them to the script addon reload, which recreates the entities a script owns.
 * Entities that no script owns are left alone and reported.
 */

#include "checkpoint_internal.h"
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>

/** One script of the world that the checkpoint folder holds a file for */
typedef struct box_restore_file_t {
    ecs_entity_t script; /**< The script entity */
    const char *name;    /**< Name of the file in the checkpoint folder */
    char *code;          /**< Text of the file */
} box_restore_file_t;

/** Read a file into a string.
 *
 * @param path The file
 * @return The contents, NULL when the file cannot be read, the caller frees it
 */
static char* box_restore_read(
    const char *path)
{
    char *result = NULL;
    FILE *file = fopen(path, "r");
    if (!file) {
        goto done;
    }

    int32_t size = 0, cap = 4096;
    result = ecs_os_malloc(cap + 1);

    size_t read;
    while ((read = fread(&result[size], 1, (size_t)(cap - size), file))) {
        size += (int32_t)read;
        if (size == cap) {
            cap *= 2;
            result = ecs_os_realloc(result, cap + 1);
        }
    }

    result[size] = '\0';
    fclose(file);

done:
    return result;
}

/** Turn the file that failed and its message into the report of a restore.
 *
 * @param name Name of the file in the checkpoint folder
 * @param message The parser or evaluation error, NULL for none
 * @return The report, the caller frees it
 */
static char* box_restore_failed(
    const char *name,
    const char *message)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_strbuf_append(&buf, BOX_CHECKPOINT_FAILED "%s\n", name);

    int32_t len = message ? (int32_t)ecs_os_strlen(message) : 0;
    while (len && message[len - 1] == '\n') {
        len --;
    }

    if (len) {
        ecs_strbuf_appendstrn(&buf, message, len);
        ecs_strbuf_appendch(&buf, '\n');
    }

    return ecs_strbuf_get(&buf);
}

/** Find the file of every managed script of the world in the folder.
 *
 * A script whose file the folder does not hold was loaded after the checkpoint
 * was written and is left alone, like the script of the entity the caller
 * keeps.
 *
 * @param ctx The state of the restore
 * @param keep Script that is left loaded, 0 for none
 * @param files Receives the scripts with a file, initialized by the caller
 */
static void box_restore_collect(
    box_checkpoint_ctx_t *ctx,
    ecs_entity_t keep,
    ecs_vec_t *files)
{
    ecs_world_t *world = ctx->world;
    ecs_vec_t scripts;
    ecs_vec_init_t(NULL, &scripts, ecs_entity_t, 0);
    box_checkpoint_managed(world, &scripts);

    int32_t i, count = ecs_vec_count(&scripts);
    ecs_entity_t *array = ecs_vec_first_t(&scripts, ecs_entity_t);
    for (i = 0; i < count; i ++) {
        const char *name = box_checkpoint_name(&ctx->names, world, array[i]);
        char *path = flecs_asprintf("%s/%s", ctx->folder, name);
        char *code = array[i] == keep ? NULL : box_restore_read(path);
        ecs_os_free(path);

        if (code) {
            box_restore_file_t *file =
                ecs_vec_append_t(NULL, files, box_restore_file_t);
            file->script = array[i];
            file->name = name;
            file->code = code;
        }
    }

    ecs_vec_fini_t(NULL, &scripts, ecs_entity_t);
}

/** Free the text of the files of a restore.
 *
 * @param files The scripts of the restore
 */
static void box_restore_files_fini(
    ecs_vec_t *files)
{
    int32_t i, count = ecs_vec_count(files);
    box_restore_file_t *array = ecs_vec_first_t(files, box_restore_file_t);
    for (i = 0; i < count; i ++) {
        ecs_os_free(array[i].code);
    }

    ecs_vec_fini_t(NULL, files, box_restore_file_t);
}

/** Parse every file of a restore before the world is touched.
 *
 * A script that fails to evaluate empties itself, so a checkpoint is parsed as
 * a whole first: a file with a syntax error then fails the restore while every
 * script of the world is still loaded.
 *
 * @param ctx The state of the restore
 * @param files The scripts of the restore
 * @return The report of the first file that does not parse, NULL when every
 *   file parses, the caller frees it
 */
static char* box_restore_parse(
    box_checkpoint_ctx_t *ctx,
    ecs_vec_t *files)
{
    char *result = NULL;
    int32_t i, count = ecs_vec_count(files);
    box_restore_file_t *array = ecs_vec_first_t(files, box_restore_file_t);
    for (i = 0; i < count && !result; i ++) {
        ecs_script_eval_result_t eval = {0};
        ecs_script_t *parsed = ecs_script_parse(
            ctx->world, array[i].name, array[i].code, NULL, &eval);
        if (parsed) {
            ecs_script_free(parsed);
        } else {
            result = box_restore_failed(array[i].name,
                eval.error ? eval.error : "failed to parse");
            ecs_os_free(eval.error);
        }
    }

    return result;
}

/** Reload every script of a restore from the text of its file.
 *
 * @param ctx The state of the restore
 * @param files The scripts of the restore
 * @return The report of the first file that does not evaluate, NULL when every
 *   script reloaded, the caller frees it
 */
static char* box_restore_apply(
    box_checkpoint_ctx_t *ctx,
    ecs_vec_t *files)
{
    char *result = NULL;
    ecs_world_t *world = ctx->world;
    int32_t i, count = ecs_vec_count(files);
    box_restore_file_t *array = ecs_vec_first_t(files, box_restore_file_t);
    for (i = 0; i < count && !result; i ++) {
        if (ecs_script_reload(world, array[i].script, array[i].code)) {
            const EcsScript *code = ecs_get(world, array[i].script, EcsScript);
            result = box_restore_failed(
                array[i].name, code ? code->error : NULL);
        }
    }

    return result;
}

/** Put the counters of the instance back after the scripts were reloaded.
 *
 * A reloaded script that sets the Checkpoint singleton would lose the last
 * folder and the next number. The restored folder becomes the last one, so a
 * restore without a name repeats the same restore.
 *
 * @param world The world
 * @param folder Folder that was restored
 * @param dir Folder the checkpoints went to before the restore
 * @param count Checkpoints the instance had written before the restore
 */
static void box_restore_counters(
    ecs_world_t *world,
    const char *folder,
    const char *dir,
    int32_t count)
{
    Checkpoint *cp = ecs_singleton_ensure(world, Checkpoint);
    ecs_os_strset(&cp->dir, dir);
    ecs_os_strset(&cp->last, folder);
    cp->count = count;
    ecs_singleton_modified(world, Checkpoint);
}

/** Find the folder a restore reads its scripts from.
 *
 * @param world The world
 * @param name Name of the checkpoint, NULL for the last one
 * @param report Receives the reason there is no such folder
 * @return The folder, NULL when there is none, the caller frees it
 */
static char* box_restore_folder(
    ecs_world_t *world,
    const char *name,
    char **report)
{
    char *result = NULL;
    const Checkpoint *cp = ecs_singleton_get(world, Checkpoint);
    if (name && name[0]) {
        char *root = box_checkpoint_dir(world);
        result = flecs_asprintf("%s/%s", root, name);
        ecs_os_free(root);
    } else if (cp && cp->last && cp->last[0]) {
        result = ecs_os_strdup(cp->last);
    } else {
        report[0] = ecs_os_strdup(
            BOX_CHECKPOINT_FAILED "no checkpoint was written\n");
        goto done;
    }

    struct stat st;
    if (stat(result, &st) || !S_ISDIR(st.st_mode)) {
        report[0] = flecs_asprintf(
            BOX_CHECKPOINT_FAILED "no checkpoint at %s\n", result);
        ecs_os_free(result);
        result = NULL;
    }

done:
    return result;
}

char* box_checkpoint_restore(
    ecs_world_t *world,
    const char *name,
    ecs_entity_t keep,
    char **report)
{
    char *text = NULL;
    char *result = box_restore_folder(world, name, &text);
    if (!result) {
        goto done;
    }

    ecs_entity_t owner = keep ?
        ecs_get_target(world, keep, ecs_id(EcsScript), 0) : 0;

    box_checkpoint_ctx_t ctx;
    box_checkpoint_ctx_init(&ctx, world, result, BOX_CHECKPOINT_KEPT);

    ecs_vec_t files;
    ecs_vec_init_t(NULL, &files, box_restore_file_t, 0);
    box_restore_collect(&ctx, owner, &files);

    text = box_restore_parse(&ctx, &files);
    if (!text) {
        const Checkpoint *cp = ecs_singleton_get(world, Checkpoint);
        char *dir = ecs_os_strdup(cp ? cp->dir : NULL);
        int32_t count = cp ? cp->count : 0;
        text = box_restore_apply(&ctx, &files);
        box_restore_counters(world, result, dir, count);
        ecs_os_free(dir);
    }

    if (text) {
        ecs_os_free(result);
        result = NULL;
    } else {
        box_checkpoint_unowned(&ctx);
        text = box_checkpoint_report(&ctx);
    }

    box_restore_files_fini(&files);
    box_checkpoint_ctx_fini(&ctx);

done:
    if (report) {
        report[0] = text;
    } else {
        ecs_os_free(text);
    }

    return result;
}
