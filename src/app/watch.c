/**
 * @file app/watch.c
 * @brief The file watch of box.app: stamps of the files modules loaded, checked by a pass on a
 * watch thread about once per FileWatch.interval (inline when the thread cannot start), and the
 * owners told when one changed.
 */

#include "watch.h"
#include <string.h>
#include <sys/stat.h>

/** What the pass of a registry is doing */
typedef enum box_watch_pass_state_t {
    BoxWatchIdle,               /**< No pass in flight */
    BoxWatchRunning,            /**< The watch thread stats the files of the pass */
    BoxWatchDone                /**< The stamps of the pass wait for the poll */
} box_watch_pass_state_t;

/** An owner of a kind, that watches a file or is told about a change */
typedef struct box_watch_ref_t {
    int32_t kind;               /**< Index of the kind */
    ecs_entity_t owner;         /**< The owner entity */
} box_watch_ref_t;

/** A distinct file the registry watches */
typedef struct box_watch_file_t {
    char *path;                 /**< The file as it was read, read by the watch thread */
    box_file_stamp_t stamp;     /**< Stamp of the last check, the first registration before */
    ecs_vec_t watchers;         /**< box_watch_ref_t, the owners with an entry for the file */
    bool queued;                /**< An owner saw another stamp, the next pass tells it */
    bool in_pass;               /**< In the pass in flight, which frees it once nobody watches */
} box_watch_file_t;

/** A file an owner watches */
typedef struct box_watch_entry_t {
    box_watch_file_t *file;     /**< The file */
    box_file_stamp_t stamp;     /**< Stamp the load of the owner saw, or the change it was told */
} box_watch_entry_t;

/** The files of one owner */
typedef struct box_watch_owner_t {
    ecs_vec_t entries;          /**< box_watch_entry_t */
} box_watch_owner_t;

/** A kind of owner and how it is told about changes */
typedef struct box_watch_kind_t {
    char *name;                 /**< Name of the kind, for traces */
    box_file_changed_t changed; /**< Tells an owner its file changed */
    void *ctx;                  /**< Passed to changed */
    ecs_map_t owners;           /**< Owner entity to box_watch_owner_t* */
} box_watch_kind_t;

/** A pass over the watched files and the thread that runs it */
typedef struct box_watch_pass_t {
    ecs_os_mutex_t lock;        /**< Guards state and quit */
    ecs_os_cond_t wake;         /**< Wakes the thread for a pass or to quit */
    ecs_os_cond_t done;         /**< Wakes the poll that waits for a pass */
    ecs_os_thread_t thread;     /**< The watch thread, 0 before the first pass on it */
    box_watch_pass_state_t state; /**< What the pass is doing */
    bool quit;                  /**< The thread stops */
    ecs_vec_t files;            /**< box_watch_file_t*, the files of the pass, in order */
    ecs_vec_t stamps;           /**< box_file_stamp_t, the stamp of each file of the pass */
} box_watch_pass_t;

/** The watched files of a world */
typedef struct box_file_watch_t {
    ecs_vec_t kinds;            /**< box_watch_kind_t */
    ecs_vec_t files;            /**< box_watch_file_t*, distinct files with watchers, in order */
    ecs_vec_t notify;           /**< box_watch_ref_t, scratch of a poll */
    int32_t queued;             /**< Files an owner registered with another stamp */
    double since;               /**< Unscaled seconds since the last pass started, -1 before */
    bool no_thread;             /**< The watch thread failed to start, passes run inline */
    box_watch_pass_t pass;      /**< The pass in flight */
} box_file_watch_t;

/** Singleton that holds the file watch of a world */
typedef struct FileWatchRegistry {
    box_file_watch_t *watch;    /**< The registry */
} FileWatchRegistry;

static ECS_COMPONENT_DECLARE(FileWatchRegistry);

void box_file_stamp(
    const char *path,
    box_file_stamp_t *stamp)
{
    struct stat st;
    ecs_os_memset_t(stamp, 0, box_file_stamp_t);
    stamp->sec = -1;
    if (!path || stat(path, &st)) {
        goto done;
    }

#ifdef __APPLE__
    stamp->sec = (int64_t)st.st_mtimespec.tv_sec;
    stamp->nsec = (int64_t)st.st_mtimespec.tv_nsec;
#else
    stamp->sec = (int64_t)st.st_mtim.tv_sec;
    stamp->nsec = (int64_t)st.st_mtim.tv_nsec;
#endif
    stamp->size = (int64_t)st.st_size;
done:
    return;
}

bool box_file_stamp_equals(
    const box_file_stamp_t *a,
    const box_file_stamp_t *b)
{
    return a->sec == b->sec && a->nsec == b->nsec && a->size == b->size;
}

/** Free a file and its list of watchers.
 *
 * @param file The file
 */
static void box_watch_file_free(
    box_watch_file_t *file)
{
    ecs_vec_fini_t(NULL, &file->watchers, box_watch_ref_t);
    ecs_os_free(file->path);
    ecs_os_free(file);
}

/** Drop an owner from the watchers of a file, and the file once nobody watches it anymore.
 *
 * A file in the pass in flight stays allocated, the thread may read its path; applying the
 * pass frees it.
 *
 * @param w The registry
 * @param file The file
 * @param kind Index of the kind of the owner
 * @param owner The owner entity
 */
static void box_watch_file_release(
    box_file_watch_t *w,
    box_watch_file_t *file,
    int32_t kind,
    ecs_entity_t owner)
{
    int32_t i;
    box_watch_ref_t *refs = ecs_vec_first(&file->watchers);
    for (i = 0; i < ecs_vec_count(&file->watchers); i ++) {
        if (refs[i].kind == kind && refs[i].owner == owner) {
            ecs_vec_remove_t(&file->watchers, box_watch_ref_t, i);
            break;
        }
    }

    if (ecs_vec_count(&file->watchers)) {
        goto done;
    }

    box_watch_file_t **files = ecs_vec_first(&w->files);
    for (i = 0; i < ecs_vec_count(&w->files); i ++) {
        if (files[i] == file) {
            ecs_vec_remove_t(&w->files, box_watch_file_t*, i);
            break;
        }
    }

    if (file->queued) {
        file->queued = false;
        w->queued --;
    }

    if (!file->in_pass) {
        box_watch_file_free(file);
    }
done:
    return;
}

/** Free the files of an owner, releasing the distinct files nobody watches anymore.
 *
 * @param w The registry
 * @param kind Index of the kind of the owner
 * @param owner The owner entity
 * @param o The files of the owner
 */
static void box_watch_owner_free(
    box_file_watch_t *w,
    int32_t kind,
    ecs_entity_t owner,
    box_watch_owner_t *o)
{
    int32_t i;
    box_watch_entry_t *entries = ecs_vec_first(&o->entries);
    for (i = 0; i < ecs_vec_count(&o->entries); i ++) {
        box_watch_file_release(w, entries[i].file, kind, owner);
    }

    ecs_vec_fini_t(NULL, &o->entries, box_watch_entry_t);
    ecs_os_free(o);
}

/** Stat the files of a pass.
 *
 * @param pass The pass
 */
static void box_watch_pass_run(
    box_watch_pass_t *pass)
{
    int32_t i;
    box_watch_file_t **files = ecs_vec_first(&pass->files);
    box_file_stamp_t *stamps = ecs_vec_first(&pass->stamps);
    for (i = 0; i < ecs_vec_count(&pass->files); i ++) {
        box_file_stamp(files[i]->path, &stamps[i]);
    }
}

/** Run the passes the poll requests until the registry stops.
 *
 * @param arg The pass
 * @return NULL
 */
static void* box_watch_thread(
    void *arg)
{
    box_watch_pass_t *pass = arg;
    ecs_os_mutex_lock(pass->lock);
    while (!pass->quit) {
        if (pass->state == BoxWatchRunning) {
            ecs_os_mutex_unlock(pass->lock);
            box_watch_pass_run(pass);
            ecs_os_mutex_lock(pass->lock);
            pass->state = BoxWatchDone;
            ecs_os_cond_signal(pass->done);
        } else {
            ecs_os_cond_wait(pass->wake, pass->lock);
        }
    }

    ecs_os_mutex_unlock(pass->lock);

    return NULL;
}

/** Free a registry, stopping its watch thread.
 *
 * @param w The registry
 */
static void box_file_watch_free(
    box_file_watch_t *w)
{
    box_watch_pass_t *pass = &w->pass;
    int32_t i;
    if (pass->thread) {
        ecs_os_mutex_lock(pass->lock);
        pass->quit = true;
        ecs_os_cond_signal(pass->wake);
        ecs_os_mutex_unlock(pass->lock);
        ecs_os_thread_join(pass->thread);
    }

    box_watch_kind_t *kinds = ecs_vec_first(&w->kinds);
    for (i = 0; i < ecs_vec_count(&w->kinds); i ++) {
        ecs_map_iter_t it = ecs_map_iter(&kinds[i].owners);
        while (ecs_map_next(&it)) {
            box_watch_owner_t *o = ecs_map_ptr(&it);
            ecs_vec_fini_t(NULL, &o->entries, box_watch_entry_t);
            ecs_os_free(o);
        }

        ecs_map_fini(&kinds[i].owners);
        ecs_os_free(kinds[i].name);
    }

    box_watch_file_t **files = ecs_vec_first(&pass->files);
    for (i = 0; i < ecs_vec_count(&pass->files); i ++) {
        if (!ecs_vec_count(&files[i]->watchers)) {
            box_watch_file_free(files[i]);
        }
    }

    files = ecs_vec_first(&w->files);
    for (i = 0; i < ecs_vec_count(&w->files); i ++) {
        box_watch_file_free(files[i]);
    }

    ecs_vec_fini_t(NULL, &w->kinds, box_watch_kind_t);
    ecs_vec_fini_t(NULL, &w->files, box_watch_file_t*);
    ecs_vec_fini_t(NULL, &w->notify, box_watch_ref_t);
    ecs_vec_fini_t(NULL, &pass->files, box_watch_file_t*);
    ecs_vec_fini_t(NULL, &pass->stamps, box_file_stamp_t);
    ecs_os_cond_free(pass->done);
    ecs_os_cond_free(pass->wake);
    ecs_os_mutex_free(pass->lock);
    ecs_os_free(w);
}

/** Free the registry of a FileWatchRegistry singleton.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_file_watch_registry_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    FileWatchRegistry *r = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        if (r[i].watch) {
            box_file_watch_free(r[i].watch);
        }
    }
}

/** Move a FileWatchRegistry singleton.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_file_watch_registry_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ecs_os_memcpy(dst, src, ti->size * count);
    ecs_os_memset(src, 0, ti->size * count);
}

/** Get the registry of a world.
 *
 * @param world The world or a stage
 * @return The registry, NULL when box.app is not imported or the world is being deleted
 */
static box_file_watch_t* box_file_watch_get(
    const ecs_world_t *world)
{
    const FileWatchRegistry *r = ecs_id(FileWatchRegistry) && !ecs_is_fini(world)
        ? ecs_singleton_get(world, FileWatchRegistry)
        : NULL;

    return r ? r->watch : NULL;
}

/** Get a kind of the registry.
 *
 * @param w The registry, may be NULL
 * @param kind Id of the kind
 * @return The kind, NULL when the id is unknown
 */
static box_watch_kind_t* box_watch_kind_get(
    box_file_watch_t *w,
    int32_t kind)
{
    return w && kind > 0 && kind <= ecs_vec_count(&w->kinds)
        ? ecs_vec_get_t(&w->kinds, box_watch_kind_t, kind - 1)
        : NULL;
}

int32_t box_file_watch_kind(
    ecs_world_t *world,
    const char *name,
    box_file_changed_t changed,
    void *ctx)
{
    box_file_watch_t *w = box_file_watch_get(world);
    int32_t result = 0;
    if (!w) {
        goto done;
    }

    box_watch_kind_t *kind = ecs_vec_append_t(NULL, &w->kinds, box_watch_kind_t);
    kind->name = ecs_os_strdup(name);
    kind->changed = changed;
    kind->ctx = ctx;
    ecs_map_init(&kind->owners, NULL);
    result = ecs_vec_count(&w->kinds);
done:
    return result;
}

void box_file_watch_add(
    ecs_world_t *world,
    int32_t kind,
    ecs_entity_t owner,
    const char *path,
    const box_file_stamp_t *stamp)
{
    box_file_watch_t *w = box_file_watch_get(world);
    box_watch_kind_t *k = box_watch_kind_get(w, kind);
    box_watch_file_t *file = NULL;
    int32_t i;
    if (!k || !owner || !path) {
        goto done;
    }

    box_watch_owner_t *o = ecs_map_get_deref(&k->owners, box_watch_owner_t, owner);
    if (!o) {
        o = ecs_os_calloc_t(box_watch_owner_t);
        ecs_vec_init_t(NULL, &o->entries, box_watch_entry_t, 0);
        ecs_map_insert_ptr(&k->owners, owner, o);
    }

    box_watch_entry_t *entries = ecs_vec_first(&o->entries);
    for (i = 0; i < ecs_vec_count(&o->entries); i ++) {
        if (!strcmp(entries[i].file->path, path)) {
            entries[i].stamp = *stamp;
            file = entries[i].file;
            goto queue;
        }
    }

    box_watch_file_t **files = ecs_vec_first(&w->files);
    for (i = 0; i < ecs_vec_count(&w->files); i ++) {
        if (!strcmp(files[i]->path, path)) {
            file = files[i];
            break;
        }
    }

    if (!file) {
        file = ecs_os_calloc_t(box_watch_file_t);
        file->path = ecs_os_strdup(path);
        file->stamp = *stamp;
        ecs_vec_init_t(NULL, &file->watchers, box_watch_ref_t, 0);
        *ecs_vec_append_t(NULL, &w->files, box_watch_file_t*) = file;
    }

    box_watch_ref_t *ref = ecs_vec_append_t(NULL, &file->watchers, box_watch_ref_t);
    ref->kind = kind - 1;
    ref->owner = owner;
    box_watch_entry_t *entry = ecs_vec_append_t(NULL, &o->entries, box_watch_entry_t);
    entry->file = file;
    entry->stamp = *stamp;
queue:
    if (!file->queued && !box_file_stamp_equals(stamp, &file->stamp)) {
        file->queued = true;
        w->queued ++;
    }
done:
    return;
}

void box_file_watch_clear(
    ecs_world_t *world,
    int32_t kind,
    ecs_entity_t owner)
{
    box_file_watch_t *w = box_file_watch_get(world);
    box_watch_kind_t *k = box_watch_kind_get(w, kind);
    box_watch_owner_t *o = k
        ? ecs_map_get_deref(&k->owners, box_watch_owner_t, owner)
        : NULL;
    if (o) {
        ecs_map_remove(&k->owners, owner);
        box_watch_owner_free(w, kind - 1, owner, o);
    }
}

/** Queue an owner to be told about a change once, however many of its files changed.
 *
 * @param w The registry
 * @param kind Index of the kind
 * @param owner The owner entity
 */
static void box_watch_queue(
    box_file_watch_t *w,
    int32_t kind,
    ecs_entity_t owner)
{
    int32_t i;
    box_watch_ref_t *queued = ecs_vec_first(&w->notify);
    for (i = 0; i < ecs_vec_count(&w->notify); i ++) {
        if (queued[i].kind == kind && queued[i].owner == owner) {
            goto done;
        }
    }

    box_watch_ref_t *n = ecs_vec_append_t(NULL, &w->notify, box_watch_ref_t);
    n->kind = kind;
    n->owner = owner;
done:
    return;
}

/** Queue the owners of a file whose entry differs from the stamp of its last check.
 *
 * Only the watchers of the file are visited, so a pass walks no owner of an unchanged file.
 *
 * @param w The registry
 * @param file The file
 */
static void box_watch_collect(
    box_file_watch_t *w,
    box_watch_file_t *file)
{
    int32_t i, j;
    box_watch_ref_t *refs = ecs_vec_first(&file->watchers);
    for (i = 0; i < ecs_vec_count(&file->watchers); i ++) {
        box_watch_kind_t *k = ecs_vec_get_t(&w->kinds, box_watch_kind_t, refs[i].kind);
        box_watch_owner_t *o = ecs_map_get_deref(&k->owners, box_watch_owner_t,
            refs[i].owner);
        box_watch_entry_t *entries = o ? ecs_vec_first(&o->entries) : NULL;
        for (j = 0; o && j < ecs_vec_count(&o->entries); j ++) {
            bool same = entries[j].file != file ||
                box_file_stamp_equals(&entries[j].stamp, &file->stamp);
            if (same) {
                continue;
            }

            entries[j].stamp = file->stamp;
            box_watch_queue(w, refs[i].kind, refs[i].owner);
        }
    }
}

/** What a pass found, added to FileWatchStatus */
typedef struct box_watch_found_t {
    int32_t passes;             /**< Passes applied */
    int32_t changes;            /**< Files whose stamp changed */
    const char *last;           /**< The file that changed last, NULL for none */
} box_watch_found_t;

/** Start a pass over every watched file, on the watch thread or inline.
 *
 * When the watch thread cannot start, the registry remembers it and the pass runs inline.
 *
 * @param w The registry
 * @param threaded Whether the watch thread runs the pass, else it runs before this returns and
 * the caller applies it
 * @return Whether the watch thread runs the pass, false when the caller applies it
 */
static bool box_watch_pass_start(
    box_file_watch_t *w,
    bool threaded)
{
    box_watch_pass_t *pass = &w->pass;
    int32_t i, count = ecs_vec_count(&w->files);
    box_watch_file_t **files = ecs_vec_first(&w->files);
    ecs_vec_set_count_t(NULL, &pass->files, box_watch_file_t*, count);
    ecs_vec_set_count_t(NULL, &pass->stamps, box_file_stamp_t, count);
    box_watch_file_t **in_pass = ecs_vec_first(&pass->files);
    for (i = 0; i < count; i ++) {
        in_pass[i] = files[i];
        files[i]->in_pass = true;
    }

    w->since = 0;
    if (threaded) {
        ecs_os_mutex_lock(pass->lock);
        pass->state = BoxWatchRunning;
        if (pass->thread) {
            ecs_os_cond_signal(pass->wake);
        } else {
            pass->thread = ecs_os_thread_new(box_watch_thread, pass);
        }

        if (!pass->thread) {
            pass->state = BoxWatchIdle;
        }

        ecs_os_mutex_unlock(pass->lock);
        if (!pass->thread) {
            ecs_warn("file watch: cannot start the watch thread, "
                "checking files on the main thread");
            w->no_thread = true;
            threaded = false;
        }
    }

    if (!threaded) {
        box_watch_pass_run(pass);
    }

    return threaded;
}

/** Get what the pass is doing, waiting for a pass the thread runs when asked to.
 *
 * @param pass The pass
 * @param wait Whether to wait for a pass the thread runs
 * @return The state of the pass
 */
static box_watch_pass_state_t box_watch_pass_get(
    box_watch_pass_t *pass,
    bool wait)
{
    ecs_os_mutex_lock(pass->lock);
    while (wait && pass->state == BoxWatchRunning) {
        ecs_os_cond_wait(pass->done, pass->lock);
    }

    box_watch_pass_state_t result = pass->state;
    ecs_os_mutex_unlock(pass->lock);

    return result;
}

/** Apply the stamps of a finished pass: record the changed files and queue their owners, and
 * the owners of queued files whose entry differs. Frees the files nobody watches anymore.
 *
 * @param w The registry
 * @param found Receives the pass and the changes
 */
static void box_watch_pass_apply(
    box_file_watch_t *w,
    box_watch_found_t *found)
{
    box_watch_pass_t *pass = &w->pass;
    int32_t i;
    box_watch_file_t **files = ecs_vec_first(&pass->files);
    box_file_stamp_t *stamps = ecs_vec_first(&pass->stamps);
    for (i = 0; i < ecs_vec_count(&pass->files); i ++) {
        box_watch_file_t *file = files[i];
        bool collect = file->queued;
        file->in_pass = false;
        if (!ecs_vec_count(&file->watchers)) {
            box_watch_file_free(file);
            continue;
        }

        if (!box_file_stamp_equals(&stamps[i], &file->stamp)) {
            file->stamp = stamps[i];
            found->changes ++;
            found->last = file->path;
            ecs_trace("file watch: '%s' changed%s", file->path,
                stamps[i].sec < 0 ? ", it is missing" : "");
            collect = true;
        }

        if (file->queued) {
            file->queued = false;
            w->queued --;
        }

        if (collect) {
            box_watch_collect(w, file);
        }
    }

    ecs_vec_clear(&pass->files);
    ecs_os_mutex_lock(pass->lock);
    pass->state = BoxWatchIdle;
    ecs_os_mutex_unlock(pass->lock);
    found->passes ++;
}

/** Apply the pass the watch thread finished, start the next pass when one is due, tell the
 * owners of the changed files and update FileWatchStatus when a pass ended or the number of
 * files changed.
 *
 * @param it The iterator of the system
 */
static void BoxFileWatchPoll(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_file_watch_t *w = box_file_watch_get(world);
    const FileWatch *settings = ecs_singleton_get(world, FileWatch);
    float interval = settings ? settings->interval : BOX_FILE_WATCH_INTERVAL;
    box_watch_found_t found = {0};
    int32_t i;
    if (!w || interval < 0) {
        goto done;
    }

#ifdef __EMSCRIPTEN__
    bool threaded = false;
#else
    bool threaded = interval > 0 && !w->no_thread && ecs_os_has_threading();
#endif
    const App *app = ecs_singleton_get(world, App);
    bool wait = !threaded || (app && app->headless);
    box_watch_pass_state_t state = box_watch_pass_get(&w->pass, wait);
    if (state == BoxWatchDone) {
        box_watch_pass_apply(w, &found);
        state = BoxWatchIdle;
    }

    if (w->since >= 0) {
        w->since += (double)ecs_get_world_info(world)->delta_time_raw;
    }

    bool due = w->since < 0 || w->since >= (double)interval || w->queued;
    if (state == BoxWatchIdle && due) {
        if (!box_watch_pass_start(w, threaded)) {
            box_watch_pass_apply(w, &found);
        }
    }

    int32_t files = ecs_vec_count(&w->files);
    const FileWatchStatus *current = ecs_singleton_get(world, FileWatchStatus);
    FileWatchStatus *status = current && (found.passes || current->files != files)
        ? ecs_singleton_get_mut(world, FileWatchStatus)
        : NULL;
    if (status) {
        status->files = files;
        status->polls += found.passes;
        status->changes += found.changes;
        if (found.last) {
            ecs_os_free(status->last);
            status->last = ecs_os_strdup(found.last);
        }

        ecs_singleton_modified(world, FileWatchStatus);
    }

    for (i = 0; i < ecs_vec_count(&w->notify); i ++) {
        box_watch_ref_t *n = ecs_vec_get_t(&w->notify, box_watch_ref_t, i);
        box_watch_kind_t *kind = ecs_vec_get_t(&w->kinds, box_watch_kind_t, n->kind);
        ecs_dbg("file watch: %s %u reloads", kind->name, (uint32_t)n->owner);
        kind->changed(world, kind->ctx, n->owner);
    }

    ecs_vec_clear(&w->notify);
done:
    return;
}

void box_file_watch_init(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FileWatchRegistry);
    ecs_set_hooks(world, FileWatchRegistry, {
        .ctor = flecs_default_ctor,
        .dtor = box_file_watch_registry_dtor,
        .move = box_file_watch_registry_move
    });

    box_file_watch_t *w = ecs_os_calloc_t(box_file_watch_t);
    ecs_vec_init_t(NULL, &w->kinds, box_watch_kind_t, 0);
    ecs_vec_init_t(NULL, &w->files, box_watch_file_t*, 0);
    ecs_vec_init_t(NULL, &w->notify, box_watch_ref_t, 0);
    ecs_vec_init_t(NULL, &w->pass.files, box_watch_file_t*, 0);
    ecs_vec_init_t(NULL, &w->pass.stamps, box_file_stamp_t, 0);
    w->pass.lock = ecs_os_mutex_new();
    w->pass.wake = ecs_os_cond_new();
    w->pass.done = ecs_os_cond_new();
    w->since = -1;
    ecs_singleton_ensure(world, FileWatchRegistry)->watch = w;

    ecs_singleton_set(world, FileWatch, { .interval = BOX_FILE_WATCH_INTERVAL });
    ecs_singleton_set(world, FileWatchStatus, {0});

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "FileWatchPoll" }),
        .phase = EcsOnStore,
        .run = BoxFileWatchPoll
    });
}
