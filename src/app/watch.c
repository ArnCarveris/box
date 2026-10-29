/**
 * @file app/watch.c
 * @brief The file watch of box.app: stamps of the files modules loaded, checked a slice per frame
 * so each is checked about once per FileWatch.interval, and the owners told when one changed.
 */

#include "watch.h"
#include <string.h>
#include <sys/stat.h>

/** An owner of a kind, that watches a file or is told about a change */
typedef struct box_watch_ref_t {
    int32_t kind;               /**< Index of the kind */
    ecs_entity_t owner;         /**< The owner entity */
} box_watch_ref_t;

/** A distinct file the registry watches */
typedef struct box_watch_file_t {
    char *path;                 /**< The file as it was read */
    box_file_stamp_t stamp;     /**< Stamp of the last check, the first registration before */
    ecs_vec_t watchers;         /**< box_watch_ref_t, the owners with an entry for the file */
    bool queued;                /**< In the queue of files the next poll checks first */
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

/** The watched files of a world */
typedef struct box_file_watch_t {
    ecs_vec_t kinds;            /**< box_watch_kind_t */
    ecs_vec_t files;            /**< box_watch_file_t*, distinct files with watchers, in order */
    ecs_vec_t queued;           /**< box_watch_file_t*, files an owner saw with another stamp */
    ecs_vec_t notify;           /**< box_watch_ref_t, scratch of a poll */
    int32_t cursor;             /**< Index of the next file the current pass checks */
    double pass;                /**< Fraction of the current pass the frames so far are due */
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

/** Remove a file from a vector of files, keeping the order of the others.
 *
 * @param files box_watch_file_t*, the vector
 * @param file The file
 * @return Index the file had, -1 when the vector does not hold it
 */
static int32_t box_watch_files_remove(
    ecs_vec_t *files,
    const box_watch_file_t *file)
{
    int32_t i, result = -1;
    box_watch_file_t **f = ecs_vec_first(files);
    for (i = 0; i < ecs_vec_count(files); i ++) {
        if (f[i] == file) {
            ecs_vec_remove_ordered_t(files, box_watch_file_t*, i);
            result = i;
            break;
        }
    }

    return result;
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
 * A file that leaves the list before the cursor moves the cursor back, so the pass in progress
 * still checks every file after it.
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

    int32_t index = box_watch_files_remove(&w->files, file);
    if (index >= 0 && index < w->cursor) {
        w->cursor --;
    }

    if (file->queued) {
        box_watch_files_remove(&w->queued, file);
    }

    box_watch_file_free(file);
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

/** Free a registry.
 *
 * @param w The registry
 */
static void box_file_watch_free(
    box_file_watch_t *w)
{
    int32_t i;
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

    box_watch_file_t **files = ecs_vec_first(&w->files);
    for (i = 0; i < ecs_vec_count(&w->files); i ++) {
        box_watch_file_free(files[i]);
    }

    ecs_vec_fini_t(NULL, &w->kinds, box_watch_kind_t);
    ecs_vec_fini_t(NULL, &w->files, box_watch_file_t*);
    ecs_vec_fini_t(NULL, &w->queued, box_watch_file_t*);
    ecs_vec_fini_t(NULL, &w->notify, box_watch_ref_t);
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

/** Queue a file for the next poll to check before its slice, because an owner registered it
 * with a stamp the file does not have; the poll then tells the owners whose stamp differs.
 *
 * @param w The registry
 * @param file The file
 */
static void box_watch_file_queue(
    box_file_watch_t *w,
    box_watch_file_t *file)
{
    if (!file->queued) {
        file->queued = true;
        *ecs_vec_append_t(NULL, &w->queued, box_watch_file_t*) = file;
    }
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
    if (!box_file_stamp_equals(stamp, &file->stamp)) {
        box_watch_file_queue(w, file);
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

/** What the checks of a frame found, added to FileWatchStatus */
typedef struct box_watch_found_t {
    int32_t checks;             /**< Files stat'ed */
    int32_t changes;            /**< Files whose stamp changed */
    const char *last;           /**< The file that changed last, NULL for none */
} box_watch_found_t;

/** Stat a file and record whether it changed since its last check.
 *
 * @param file The file
 * @param found Receives the check and the change
 * @return Whether the stamp of the file changed
 */
static bool box_watch_check(
    box_watch_file_t *file,
    box_watch_found_t *found)
{
    box_file_stamp_t stamp;
    bool result = false;
    box_file_stamp(file->path, &stamp);
    found->checks ++;
    if (box_file_stamp_equals(&stamp, &file->stamp)) {
        goto done;
    }

    file->stamp = stamp;
    found->changes ++;
    found->last = file->path;
    ecs_trace("file watch: '%s' changed%s", file->path,
        stamp.sec < 0 ? ", it is missing" : "");
    result = true;
done:
    return result;
}

/** Queue the owners of a file whose entry differs from the stamp of its last check.
 *
 * Only the watchers of the file are visited, so a poll walks no owner of an unchanged file.
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

/** Check the queued files and the slice of the current pass that the frame is due.
 *
 * A pass checks every file once, in order. Each frame advances it by the unscaled frame delta
 * over the interval, at most one whole pass, and checks the files up to that fraction.
 *
 * @param w The registry
 * @param interval FileWatch.interval, 0 or more
 * @param delta Unscaled seconds of the frame
 * @param found Receives the checks and the changes
 * @return Whether the frame completed a pass
 */
static bool box_watch_slice(
    box_file_watch_t *w,
    float interval,
    double delta,
    box_watch_found_t *found)
{
    int32_t i;
    double due = interval > 0 ? delta / (double)interval : 1.0;
    box_watch_file_t **queued = ecs_vec_first(&w->queued);
    for (i = 0; i < ecs_vec_count(&w->queued); i ++) {
        queued[i]->queued = false;
        box_watch_check(queued[i], found);
        box_watch_collect(w, queued[i]);
    }

    ecs_vec_clear(&w->queued);
    w->pass += due > 1.0 ? 1.0 : (due > 0 ? due : 0);
    bool result = w->pass >= 1.0;
    int32_t count = ecs_vec_count(&w->files);
    int32_t end = result ? count : (int32_t)(w->pass * (double)count);
    box_watch_file_t **files = ecs_vec_first(&w->files);
    for (i = w->cursor; i < end && i < count; i ++) {
        if (box_watch_check(files[i], found)) {
            box_watch_collect(w, files[i]);
        }
    }

    if (end > w->cursor) {
        w->cursor = end;
    }

    if (result) {
        w->pass -= 1.0;
        w->cursor = 0;
    }

    return result;
}

/** Check the slice of the watched files a frame is due, tell the owners of the changed ones
 * and update FileWatchStatus when the frame checked a file or completed a pass.
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

    double delta = (double)ecs_get_world_info(world)->delta_time_raw;
    bool pass = box_watch_slice(w, interval, delta, &found);
    int32_t files = ecs_vec_count(&w->files);
    const FileWatchStatus *current = ecs_singleton_get(world, FileWatchStatus);
    FileWatchStatus *status = current && (pass || found.checks || current->files != files)
        ? ecs_singleton_get_mut(world, FileWatchStatus)
        : NULL;
    if (status) {
        status->files = files;
        status->polls += pass ? 1 : 0;
        status->checks += found.checks;
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
    ecs_vec_init_t(NULL, &w->queued, box_watch_file_t*, 0);
    ecs_vec_init_t(NULL, &w->notify, box_watch_ref_t, 0);
    ecs_singleton_ensure(world, FileWatchRegistry)->watch = w;

    ecs_singleton_set(world, FileWatch, { .interval = BOX_FILE_WATCH_INTERVAL });
    ecs_singleton_set(world, FileWatchStatus, {0});

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "FileWatchPoll" }),
        .phase = EcsOnStore,
        .run = BoxFileWatchPoll
    });
}
