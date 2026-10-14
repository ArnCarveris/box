/**
 * @file command/terminal.c
 * @brief Session folders, the per session command log and session handles.
 */

#include "command.h"
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

/** Size of the buffer that holds a generated session handle */
#define BOX_TERMINAL_HANDLE_MAX (32)

/** Most idle sessions closed in one frame */
#define BOX_TERMINAL_IDLE_BATCH (64)

/** Create a private folder unless it exists.
 *
 * @param path The folder
 * @return 0 on success, -1 when it could not be created
 */
static int box_terminal_mkdir(
    const char *path)
{
    int result = 0;
    if (mkdir(path, 0700) && errno != EEXIST) {
        ecs_err("cannot create %s: %s", path, strerror(errno));
        result = -1;
    }

    return result;
}

/** Read the wall clock.
 *
 * @return Seconds since an arbitrary epoch
 */
static double box_terminal_now(void)
{
    ecs_time_t now;
    ecs_os_get_time(&now);

    return ecs_time_to_double(now);
}

/** Check that a session name is a single file name inside the session root.
 *
 * @param name The name of the session entity, NULL when it has none
 * @return Whether the name can be used as the session folder
 */
static bool box_terminal_name_is_file(
    const char *name)
{
    bool result = name && name[0] && !strchr(name, '/') && strcmp(name, ".") &&
        strcmp(name, "..");

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
    const TerminalLimits *limits = ecs_singleton_get(world, TerminalLimits);
    const char *root = box_terminal_root(world);
    const char *name = ecs_get_name(world, e);
    char *dir = NULL, *sub = NULL, *log = NULL;
    int result = -1;
    if (!root) {
        goto done;
    }

    if (box_terminal_name_is_file(name)) {
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

    if (limits) {
        out->history = limits->history;
        out->images_max = limits->images;
        out->log_size = limits->log_size;
    }

    out->active = box_terminal_now();

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
        ecs_vec_init_t(NULL, &s.images, box_terminal_image_t, 0);
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

    root = box_app_tmp_open(world);
    if (!root) {
        goto done;
    }

    host->root = root;
    result = root;
done:
    return result;
}

/** Move a full commands.log to commands.log.1 and start a new one.
 *
 * @param s The session
 * @param size Bytes of the entry that is about to be written
 */
static void box_terminal_log_rotate(
    TerminalSession *s,
    int32_t size)
{
    long written = ftell(s->log);
    char *log, *old;
    if (written <= 0 || written + size <= s->log_size) {
        goto done;
    }

    log = flecs_asprintf("%s/%s", s->dir, BOX_TERMINAL_LOG);
    old = flecs_asprintf("%s.1", log);
    fclose(s->log);
    if (rename(log, old)) {
        ecs_warn("cannot move %s: %s", log, strerror(errno));
    }

    s->log = fopen(log, "w");
    if (!s->log) {
        ecs_err("cannot open %s: %s", log, strerror(errno));
    }

    ecs_os_free(old);
    ecs_os_free(log);
done:
    return;
}

/** Add a line to the history ring of a session, replacing the oldest when it is full.
 *
 * @param s The session
 * @param line The command line
 * @param result The output, NULL when it is not kept
 * @param error The command failed
 */
static void box_terminal_history_add(
    TerminalSession *s,
    const char *line,
    const char *result,
    bool error)
{
    box_command_entry_t *entry;
    if (s->history <= 0) {
        goto done;
    }

    if (ecs_vec_count(&s->entries) < s->history) {
        entry = ecs_vec_append_t(NULL, &s->entries, box_command_entry_t);
    } else {
        entry = ecs_vec_get_t(&s->entries, box_command_entry_t,
            (int32_t)(s->total % s->history));
        ecs_os_free(entry->line);
        ecs_os_free(entry->result);
    }

    entry->line = ecs_os_strdup(line);
    entry->result = result ? ecs_os_strdup(result) : NULL;
    entry->error = error;
done:
    return;
}

int32_t box_terminal_history_count(
    const TerminalSession *s)
{
    return ecs_vec_count(&s->entries);
}

const box_command_entry_t* box_terminal_history_get(
    const TerminalSession *s,
    int32_t i)
{
    int32_t count = ecs_vec_count(&s->entries);
    int32_t first = count < s->history ? 0 : (int32_t)(s->total % count);

    return ecs_vec_get_t(&s->entries, box_command_entry_t, (first + i) % count);
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
    char *text;
    if (!s) {
        goto done;
    }

    box_terminal_history_add(s, line, result, error);
    s->total ++;
    s->active = box_terminal_now();
    if (status) {
        status->commands = (int32_t)(s->total < INT32_MAX ? s->total : INT32_MAX);
    }

    if (!s->log) {
        goto done;
    }

    if (result && result[0]) {
        text = flecs_asprintf("$ %s\n%s\n", line, result);
    } else {
        text = flecs_asprintf("$ %s\n", line);
    }

    box_terminal_log_rotate(s, ecs_os_strlen(text));
    if (s->log) {
        fputs(text, s->log);
        fflush(s->log);
    }

    ecs_os_free(text);
done:
    return;
}

/** Delete the oldest image of a session that the renderer is done with.
 *
 * @param s The session
 * @param rendered RenderStatus.rendered, packets the renderer finished
 * @return Whether an image was deleted or dropped
 */
static bool box_terminal_image_evict(
    TerminalSession *s,
    int32_t rendered)
{
    int32_t i, count = ecs_vec_count(&s->images);
    bool result = false;
    for (i = 0; i < count && !result; i ++) {
        box_terminal_image_t *image = ecs_vec_get_t(&s->images, box_terminal_image_t, i);
        result = !unlink(image->path) || (errno == ENOENT && rendered >= image->packet);
        if (result) {
            ecs_os_free(image->path);
            ecs_vec_remove_ordered_t(&s->images, box_terminal_image_t, i);
        }
    }

    return result;
}

int box_terminal_image(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path)
{
    TerminalSession *s = ecs_get_mut(world, session, TerminalSession);
    const RenderStatus *status = ecs_singleton_get(world, RenderStatus);
    int32_t submitted = status ? status->submitted : 0;
    int32_t rendered = status ? status->rendered : 0;
    box_terminal_image_t *image;
    int result = BOX_TERMINAL_IMAGE_NONE;
    if (!s || s->images_max <= 0) {
        goto done;
    }

    result = BOX_TERMINAL_IMAGE_PENDING;
    while (ecs_vec_count(&s->images) >= s->images_max) {
        if (!box_terminal_image_evict(s, rendered)) {
            goto done;
        }
    }

    image = ecs_vec_append_t(NULL, &s->images, box_terminal_image_t);
    image->path = ecs_os_strdup(path);
    image->packet = submitted + BOX_TERMINAL_IMAGE_PACKETS;
    result = 0;
done:
    return result;
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

bool box_terminal_full(
    ecs_world_t *world,
    int32_t *open)
{
    const TerminalLimits *limits = ecs_singleton_get(world, TerminalLimits);
    ecs_entity_t scope = ecs_lookup(world, BOX_TERMINAL_SCOPE);
    ecs_iter_t it = ecs_children(world, scope);
    int32_t count = 0, i;
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            count += ecs_has(world, it.entities[i], TerminalSession);
        }
    }

    if (open) {
        open[0] = count;
    }

    return limits && count >= limits->sessions;
}

ecs_entity_t box_terminal_new(
    ecs_world_t *world)
{
    box_terminal_host_t *host = box_terminal_host(world);
    ecs_entity_t scope = ecs_lookup(world, BOX_TERMINAL_SCOPE);
    char handle[BOX_TERMINAL_HANDLE_MAX];
    ecs_entity_t result = 0;
    int32_t open = 0;
    if (box_terminal_full(world, &open)) {
        ecs_err("cannot open a terminal session: %d are open", open);
        goto done;
    }

    do {
        host->next ++;
        ecs_os_snprintf(handle, BOX_TERMINAL_HANDLE_MAX, "s%d", host->next);
    } while (ecs_lookup_child(world, scope, handle));

    result = ecs_entity(world, { .name = handle, .parent = scope });
    ecs_add(world, result, Terminal);
    if (!ecs_has(world, result, TerminalSession)) {
        ecs_delete(world, result);
        result = 0;
    }

done:
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

/** Close the sessions box_terminal_new opened that ran no command for longer than
 * TerminalLimits.idle_timeout.
 *
 * @param it Iterator of the system
 */
static void BoxTerminalIdle(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    const TerminalLimits *limits = ecs_singleton_get(world, TerminalLimits);
    ecs_entity_t scope = ecs_lookup(world, BOX_TERMINAL_SCOPE);
    ecs_entity_t idle[BOX_TERMINAL_IDLE_BATCH];
    double now = box_terminal_now();
    int32_t count = 0, i;
    ecs_iter_t cit;
    if (!limits || !scope) {
        goto done;
    }

    cit = ecs_children(world, scope);
    while (ecs_children_next(&cit)) {
        for (i = 0; i < cit.count; i ++) {
            const TerminalSession *s = ecs_get(world, cit.entities[i], TerminalSession);
            if (s && count < BOX_TERMINAL_IDLE_BATCH &&
                now - s->active >= limits->idle_timeout)
            {
                idle[count ++] = cit.entities[i];
            }
        }
    }

    for (i = 0; i < count; i ++) {
        ecs_delete(world, idle[i]);
    }

done:
    return;
}

void box_terminal_init(
    ecs_world_t *world,
    box_terminal_host_t *host)
{
    (void)host;
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "TerminalIdle" }),
        .phase = EcsOnLoad,
        .run = BoxTerminalIdle,
        .immediate = true
    });

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
