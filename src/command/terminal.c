/**
 * @file command/terminal.c
 * @brief Session folders, the commands.log file and the per session command ring.
 */

#include "command_internal.h"
#include <sys/stat.h>
#include <string.h>
#include <errno.h>

/** Compose the session folder path of a terminal entity.
 *
 * @param world The world
 * @param e The terminal entity
 * @return The folder path, the caller frees it
 */
static char* box_terminal_dir(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const App *app = ecs_singleton_get(world, App);
    ecs_assert(app != NULL, ECS_INVALID_OPERATION, "import box.core before box.command");
    ecs_assert(app->process_dir != NULL, ECS_INVALID_OPERATION, "no process folder");

    const char *name = ecs_get_name(world, e);
    char *result;
    if (name) {
        result = flecs_asprintf("%s/%s", app->process_dir, name);
    } else {
        result = flecs_asprintf("%s/session_%u", app->process_dir, (uint32_t)e);
    }

    return result;
}

/** Create a folder, warn when it cannot be created.
 *
 * @param path The folder
 */
static void box_terminal_mkdir(
    const char *path)
{
    if (mkdir(path, 0777) && errno != EEXIST) {
        ecs_warn("failed to create session folder %s", path);
    }
}

/** Create the session folder with its resources subfolder and open the log.
 *
 * @param stage The stage that queues the session component
 * @param e The terminal entity
 * @param dir The session folder
 */
static void box_terminal_open(
    ecs_world_t *stage,
    ecs_entity_t e,
    const char *dir)
{
    box_terminal_mkdir(dir);

    char *resources = flecs_asprintf("%s/resources", dir);
    box_terminal_mkdir(resources);
    ecs_os_free(resources);

    char *path = flecs_asprintf("%s/commands.log", dir);
    TerminalSession session = { .dir = ecs_os_strdup(dir), .log = fopen(path, "w") };
    if (!session.log) {
        ecs_warn("failed to open session log %s", path);
    }

    ecs_os_free(path);
    ecs_set_ptr(stage, e, TerminalSession, &session);
}

/** Terminal was added: open the session and publish its folder.
 *
 * @param it Iterator over the new terminals
 */
static void box_terminal_on_add(
    ecs_iter_t *it)
{
    Terminal *t = ecs_field(it, Terminal, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        char *dir = box_terminal_dir(it->world, it->entities[i]);
        box_terminal_open(it->stage, it->entities[i], dir);
        ecs_os_free(t[i].session_dir);
        t[i].session_dir = dir;
    }
}

/** Terminal was set: restore the folder that the assigned value overwrote.
 *
 * @param it Iterator over the changed terminals
 */
static void box_terminal_on_set(
    ecs_iter_t *it)
{
    Terminal *t = ecs_field(it, Terminal, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        char *dir = box_terminal_dir(it->world, it->entities[i]);
        ecs_os_free(t[i].session_dir);
        t[i].session_dir = dir;
    }
}

/** Terminal was removed: close the session and remove its folder.
 *
 * @param it Iterator over the removed terminals
 */
static void box_terminal_on_remove(
    ecs_iter_t *it)
{
    bool deleting = !it->other_table || !ecs_table_get_type(it->other_table)->count;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        TerminalSession *session = ecs_get_mut(it->world, e, TerminalSession);
        ecs_assert(session != NULL, ECS_INVALID_OPERATION, "terminal without session");
        box_terminal_session_fini(session);
        if (!deleting) {
            ecs_remove(it->stage, e, TerminalSession);
        }
    }
}

void box_terminal_session_fini(
    TerminalSession *session)
{
    if (session->log) {
        fclose(session->log);
        session->log = NULL;
    }

    if (session->dir) {
        box_rmtree(session->dir);
        ecs_os_free(session->dir);
        session->dir = NULL;
    }

    int32_t i;
    for (i = 0; i < BOX_COMMAND_LOG_MAX; i ++) {
        ecs_os_free(session->ring[i].line);
        ecs_os_free(session->ring[i].result);
        session->ring[i].line = NULL;
        session->ring[i].result = NULL;
    }

    session->count = 0;
}

void box_terminal_log(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line,
    const char *result,
    bool error)
{
    TerminalSession *s = ecs_get_mut(world, session, TerminalSession);
    ecs_assert(s != NULL, ECS_INVALID_PARAMETER, "entity is not a terminal session");

    const char *text = result ? result : "";
    box_command_entry_t *entry = &s->ring[s->count % BOX_COMMAND_LOG_MAX];
    ecs_os_free(entry->line);
    ecs_os_free(entry->result);
    entry->line = ecs_os_strdup(line);
    entry->result = ecs_os_strdup(text);
    entry->error = error;
    s->count ++;

    if (s->log) {
        fprintf(s->log, "$ %s\n%s\n", line, text);
        fflush(s->log);
    }
}

void box_terminal_init(
    ecs_world_t *world)
{
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Terminal) }},
        .events = { EcsOnAdd },
        .callback = box_terminal_on_add
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Terminal) }},
        .events = { EcsOnSet },
        .callback = box_terminal_on_set
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Terminal) }},
        .events = { EcsOnRemove },
        .callback = box_terminal_on_remove
    });
}
