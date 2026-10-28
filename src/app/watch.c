/**
 * @file app/watch.c
 * @brief The file watch of box.app: stamps of the files modules loaded, checked once per
 * FileWatch.interval before the render extract, and the owners told when one changed.
 */

#include "watch.h"
#include <string.h>
#include <sys/stat.h>

/** A distinct file the registry watches */
typedef struct box_watch_file_t {
    char *path;                 /**< The file as it was read */
    box_file_stamp_t stamp;     /**< Stamp of the last check, the first registration before */
    int32_t watchers;           /**< Entries of owners that watch the file */
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

/** An owner to tell about a change after the files were checked */
typedef struct box_watch_notify_t {
    int32_t kind;               /**< Index of the kind */
    ecs_entity_t owner;         /**< The owner entity */
} box_watch_notify_t;

/** The watched files of a world */
typedef struct box_file_watch_t {
    ecs_vec_t kinds;            /**< box_watch_kind_t */
    ecs_vec_t files;            /**< box_watch_file_t*, distinct files with watchers */
    ecs_vec_t notify;           /**< box_watch_notify_t, scratch of a poll */
    ecs_time_t last_poll;       /**< Time the files were last checked */
    bool polled;                /**< The files were checked at least once */
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

/** Free the files of an owner, releasing the distinct files nobody watches anymore.
 *
 * @param w The registry
 * @param owner The owner
 */
static void box_watch_owner_free(
    box_file_watch_t *w,
    box_watch_owner_t *owner)
{
    int32_t i, j;
    box_watch_entry_t *entries = ecs_vec_first(&owner->entries);
    for (i = 0; i < ecs_vec_count(&owner->entries); i ++) {
        box_watch_file_t *file = entries[i].file;
        if (-- file->watchers) {
            continue;
        }

        box_watch_file_t **files = ecs_vec_first(&w->files);
        for (j = 0; j < ecs_vec_count(&w->files); j ++) {
            if (files[j] == file) {
                ecs_vec_remove_t(&w->files, box_watch_file_t*, j);
                break;
            }
        }

        ecs_os_free(file->path);
        ecs_os_free(file);
    }

    ecs_vec_fini_t(NULL, &owner->entries, box_watch_entry_t);
    ecs_os_free(owner);
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
            box_watch_owner_free(w, ecs_map_ptr(&it));
        }

        ecs_map_fini(&kinds[i].owners);
        ecs_os_free(kinds[i].name);
    }

    ecs_vec_fini_t(NULL, &w->kinds, box_watch_kind_t);
    ecs_vec_fini_t(NULL, &w->files, box_watch_file_t*);
    ecs_vec_fini_t(NULL, &w->notify, box_watch_notify_t);
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
            goto done;
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
        *ecs_vec_append_t(NULL, &w->files, box_watch_file_t*) = file;
    }

    file->watchers ++;
    box_watch_entry_t *entry = ecs_vec_append_t(NULL, &o->entries, box_watch_entry_t);
    entry->file = file;
    entry->stamp = *stamp;
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
        box_watch_owner_free(w, o);
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
    box_watch_notify_t *queued = ecs_vec_first(&w->notify);
    for (i = 0; i < ecs_vec_count(&w->notify); i ++) {
        if (queued[i].kind == kind && queued[i].owner == owner) {
            goto done;
        }
    }

    box_watch_notify_t *n = ecs_vec_append_t(NULL, &w->notify, box_watch_notify_t);
    n->kind = kind;
    n->owner = owner;
done:
    return;
}

/** Stat every distinct file once and record which changed since the last check.
 *
 * @param w The registry
 * @param status The status, receives the changes
 */
static void box_watch_check(
    box_file_watch_t *w,
    FileWatchStatus *status)
{
    int32_t i;
    box_watch_file_t **files = ecs_vec_first(&w->files);
    for (i = 0; i < ecs_vec_count(&w->files); i ++) {
        box_file_stamp_t stamp;
        box_file_stamp(files[i]->path, &stamp);
        if (box_file_stamp_equals(&stamp, &files[i]->stamp)) {
            continue;
        }

        files[i]->stamp = stamp;
        status->changes ++;
        ecs_os_free(status->last);
        status->last = ecs_os_strdup(files[i]->path);
        ecs_trace("file watch: '%s' changed%s", files[i]->path,
            stamp.sec < 0 ? ", it is missing" : "");
    }
}

/** Find the owners whose files differ from what their load saw and queue them.
 *
 * @param w The registry
 */
static void box_watch_collect(
    box_file_watch_t *w)
{
    int32_t k, i;
    box_watch_kind_t *kinds = ecs_vec_first(&w->kinds);
    for (k = 0; k < ecs_vec_count(&w->kinds); k ++) {
        ecs_map_iter_t it = ecs_map_iter(&kinds[k].owners);
        while (ecs_map_next(&it)) {
            box_watch_owner_t *o = ecs_map_ptr(&it);
            box_watch_entry_t *entries = ecs_vec_first(&o->entries);
            for (i = 0; i < ecs_vec_count(&o->entries); i ++) {
                if (box_file_stamp_equals(&entries[i].stamp, &entries[i].file->stamp)) {
                    continue;
                }

                entries[i].stamp = entries[i].file->stamp;
                box_watch_queue(w, k, (ecs_entity_t)ecs_map_key(&it));
            }
        }
    }
}

/** Check the watched files once the interval of FileWatch elapsed, tell the owners of the
 * changed ones and update FileWatchStatus.
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
    ecs_time_t now = w ? w->last_poll : (ecs_time_t){0};
    double elapsed = ecs_time_measure(&now);
    int32_t i;
    if (!w || interval < 0 || (w->polled && elapsed < interval)) {
        goto done;
    }

    w->last_poll = now;
    w->polled = true;
    FileWatchStatus *status = ecs_singleton_ensure(world, FileWatchStatus);
    status->polls ++;
    box_watch_check(w, status);
    status->files = ecs_vec_count(&w->files);
    ecs_singleton_modified(world, FileWatchStatus);
    box_watch_collect(w);
    for (i = 0; i < ecs_vec_count(&w->notify); i ++) {
        box_watch_notify_t *n = ecs_vec_get_t(&w->notify, box_watch_notify_t, i);
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
    ecs_vec_init_t(NULL, &w->notify, box_watch_notify_t, 0);
    ecs_singleton_ensure(world, FileWatchRegistry)->watch = w;

    ecs_singleton_set(world, FileWatch, { .interval = BOX_FILE_WATCH_INTERVAL });
    ecs_singleton_set(world, FileWatchStatus, {0});

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "FileWatchPoll" }),
        .phase = EcsOnStore,
        .run = BoxFileWatchPoll,
        .immediate = true
    });
}
