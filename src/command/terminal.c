/**
 * @file command/terminal.c
 * @brief Session folders, the per session command log and session handles.
 */

#include "command.h"
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/** Size of the buffer that holds a generated session handle */
#define BOX_TERMINAL_HANDLE_MAX (32)

/** Create a folder unless it exists.
 *
 * @param path The folder
 * @return 0 on success, -1 when it could not be created
 */
static int box_terminal_mkdir(
    const char *path)
{
    int result = 0;
    if (mkdir(path, 0777) && errno != EEXIST) {
        ecs_err("cannot create %s: %s", path, strerror(errno));
        result = -1;
    }

    return result;
}

/** Create the folder, the resources subfolder and the log of a session.
 *
 * @param world The world
 * @param e The session entity
 * @param out Receives the session state
 * @return 0 on success, -1 when the folder could not be created
 */
static int box_terminal_open(
    ecs_world_t *world,
    ecs_entity_t e,
    TerminalSession *out)
{
    const char *root = box_terminal_root(world);
    const char *name = ecs_get_name(world, e);
    char *dir = NULL, *sub = NULL, *log = NULL;
    int result = -1;
    if (!root) {
        goto done;
    }

    if (name) {
        dir = flecs_asprintf("%s/%s", root, name);
    } else {
        dir = flecs_asprintf("%s/%u", root, (uint32_t)e);
    }

    sub = flecs_asprintf("%s/%s", dir, BOX_TERMINAL_RESOURCES);
    log = flecs_asprintf("%s/%s", dir, BOX_TERMINAL_LOG);
    if (box_terminal_mkdir(dir) || box_terminal_mkdir(sub)) {
        goto done;
    }

    out->log = fopen(log, "w");
    if (!out->log) {
        ecs_err("cannot open %s: %s", log, strerror(errno));
    }

    out->dir = dir;
    dir = NULL;
    result = 0;
done:
    ecs_os_free(dir);
    ecs_os_free(sub);
    ecs_os_free(log);

    return result;
}

/** Terminal was added: create the session folder and its state.
 *
 * @param it Iterator over the new sessions
 */
static void box_terminal_on_add(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        TerminalSession s = {0};
        if (box_terminal_open(it->world, e, &s)) {
            continue;
        }

        ecs_vec_init_t(NULL, &s.entries, box_command_entry_t, 0);
        ecs_set(it->stage, e, TerminalStatus, { .dir = s.dir });
        ecs_set_ptr(it->stage, e, TerminalSession, &s);
    }
}

/** Terminal was removed from a live entity: drop the session state and folder.
 *
 * @param it Iterator over the closed sessions
 */
static void box_terminal_on_remove(
    ecs_iter_t *it)
{
    int32_t i;
    if (!it->other_table || !ecs_table_get_type(it->other_table)->count) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        ecs_remove(it->stage, it->entities[i], TerminalSession);
        ecs_remove(it->stage, it->entities[i], TerminalStatus);
    }

done:
    return;
}

void box_terminal_rmtree(
    const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    if (!dir) {
        goto done;
    }

    while ((entry = readdir(dir))) {
        struct stat st;
        char *child;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }

        child = flecs_asprintf("%s/%s", path, entry->d_name);
        if (!lstat(child, &st) && S_ISDIR(st.st_mode)) {
            box_terminal_rmtree(child);
        } else {
            unlink(child);
        }

        ecs_os_free(child);
    }

    closedir(dir);
    rmdir(path);
done:
    return;
}

const char* box_terminal_root(
    ecs_world_t *world)
{
    const AppStatus *status = ecs_singleton_get(world, AppStatus);
    box_terminal_host_t *host = box_terminal_host(world);
    const char *result = NULL;
    char *root;
    if (status && status->tmp_dir) {
        result = status->tmp_dir;
        goto done;
    }

    if (host->root) {
        result = host->root;
        goto done;
    }

    root = flecs_asprintf("%s/%d", BOX_TMP_ROOT, (int)getpid());
    if (box_terminal_mkdir(BOX_TMP_ROOT) || box_terminal_mkdir(root)) {
        ecs_os_free(root);
        goto done;
    }

    host->root = root;
    result = root;
done:
    return result;
}

void box_terminal_log(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line,
    const char *result,
    bool error)
{
    TerminalSession *s = ecs_get_mut(world, session, TerminalSession);
    TerminalStatus *status = ecs_get_mut(world, session, TerminalStatus);
    box_command_entry_t *entry;
    if (!s) {
        goto done;
    }

    entry = ecs_vec_append_t(NULL, &s->entries, box_command_entry_t);
    entry->line = ecs_os_strdup(line);
    entry->result = result ? ecs_os_strdup(result) : NULL;
    entry->error = error;
    if (status) {
        status->commands = ecs_vec_count(&s->entries);
    }

    if (s->log) {
        if (result && result[0]) {
            fprintf(s->log, "$ %s\n%s\n", line, result);
        } else {
            fprintf(s->log, "$ %s\n", line);
        }

        fflush(s->log);
    }

done:
    return;
}

ecs_entity_t box_terminal_cwd(
    ecs_world_t *world,
    ecs_entity_t session)
{
    const Terminal *t = ecs_get(world, session, Terminal);
    ecs_entity_t result = 0;
    if (t && t->cwd && t->cwd[0]) {
        result = ecs_lookup_path_w_sep(world, 0, t->cwd, ".", NULL, false);
    }

    return result;
}

ecs_entity_t box_terminal_new(
    ecs_world_t *world)
{
    box_terminal_host_t *host = box_terminal_host(world);
    ecs_entity_t scope = ecs_lookup(world, BOX_TERMINAL_SCOPE);
    char handle[BOX_TERMINAL_HANDLE_MAX];
    do {
        host->next ++;
        ecs_os_snprintf(handle, BOX_TERMINAL_HANDLE_MAX, "s%d", host->next);
    } while (ecs_lookup_child(world, scope, handle));

    ecs_entity_t result = ecs_entity(world, { .name = handle, .parent = scope });
    ecs_add(world, result, Terminal);
    if (!ecs_has(world, result, TerminalSession)) {
        ecs_delete(world, result);
        result = 0;
    }

    return result;
}

ecs_entity_t box_terminal_lookup(
    ecs_world_t *world,
    const char *handle)
{
    ecs_entity_t scope = ecs_lookup(world, BOX_TERMINAL_SCOPE);
    ecs_entity_t result = 0;
    if (handle && handle[0] && !strchr(handle, '.')) {
        result = ecs_lookup_child(world, scope, handle);
    }

    if (result && !ecs_has(world, result, TerminalSession)) {
        result = 0;
    }

    return result;
}

void box_terminal_init(
    ecs_world_t *world,
    box_terminal_host_t *host)
{
    (void)host;
    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TerminalOnAdd" }),
        .query.terms = {{ .id = ecs_id(Terminal) }},
        .events = { EcsOnAdd },
        .callback = box_terminal_on_add
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TerminalOnRemove" }),
        .query.terms = {{ .id = ecs_id(Terminal) }},
        .events = { EcsOnRemove },
        .callback = box_terminal_on_remove
    });
}
