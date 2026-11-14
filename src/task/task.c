/**
 * @file task/task.c
 * @brief Registers the box.task module: the reports of the work modules do in the background
 * and the InProgressTask singleton, their aggregate, written at the end of every frame.
 *
 * The reports live in a registry behind a lock, so a module may report from any thread; only
 * the system at the end of the frame, or box_task_publish, writes the singleton.
 */

#define BOX_TASK_IMPL
#include <box.h>

/** A report of a module */
typedef struct box_task_report_t {
    box_task_id_t id;       /**< Handle of the report */
    const char *name;       /**< Name of the task, interned in the registry */
    bool blocking;          /**< The task blocks the scene */
    int32_t done;           /**< Units that finished */
    int32_t total;          /**< Units of work, 0 when unknown */
} box_task_report_t;

/** The reports of a world */
typedef struct box_tasks_t {
    ecs_os_mutex_t lock;    /**< Protects everything below */
    ecs_vec_t reports;      /**< box_task_report_t, in the order they started */
    ecs_vec_t names;        /**< char*, every name a report used, owned */
    box_task_id_t last;     /**< Last handle given to a report */
    bool dirty;             /**< The reports changed since the last publish */
} box_tasks_t;

/** Singleton that holds the registry of a world */
typedef struct TaskRegistry {
    box_tasks_t *tasks;     /**< The registry */
} TaskRegistry;

static ECS_COMPONENT_DECLARE(TaskRegistry);
ECS_DECLARE(TaskPublish);

/** Get the registry of a world.
 *
 * @param world The world or a stage
 * @return The registry, NULL when box.task is not imported
 */
static box_tasks_t* box_tasks_get(
    const ecs_world_t *world)
{
    const TaskRegistry *r = world && ecs_id(TaskRegistry)
        ? ecs_singleton_get(world, TaskRegistry)
        : NULL;

    return r ? r->tasks : NULL;
}

/** Get the interned copy of a name, adding it on first use. Called under the lock.
 *
 * @param t The registry
 * @param name The name
 * @return The interned name, which lives as long as the registry
 */
static const char* box_task_intern(
    box_tasks_t *t,
    const char *name)
{
    const char *result = NULL;
    int32_t i, count = ecs_vec_count(&t->names);
    for (i = 0; i < count && !result; i ++) {
        const char *n = *ecs_vec_get_t(&t->names, char*, i);
        if (!ecs_os_strcmp(n, name)) {
            result = n;
        }
    }

    if (!result) {
        char *copy = ecs_os_strdup(name);
        *ecs_vec_append_t(NULL, &t->names, char*) = copy;
        result = copy;
    }

    return result;
}

/** Find a report by handle. Called under the lock.
 *
 * @param t The registry
 * @param task The handle
 * @return Index of the report, -1 when it ended
 */
static int32_t box_task_index(
    box_tasks_t *t,
    box_task_id_t task)
{
    int32_t i, result = -1, count = ecs_vec_count(&t->reports);
    for (i = 0; i < count && result < 0; i ++) {
        if (ecs_vec_get_t(&t->reports, box_task_report_t, i)->id == task) {
            result = i;
        }
    }

    return result;
}

box_task_id_t box_task_start(
    ecs_world_t *world,
    const char *name,
    bool blocking,
    int32_t total)
{
    box_tasks_t *t = box_tasks_get(world);
    box_task_id_t result = 0;
    if (!t || !name) {
        goto done;
    }

    ecs_os_mutex_lock(t->lock);
    result = ++ t->last;
    if (!result) {
        result = ++ t->last;
    }

    *ecs_vec_append_t(NULL, &t->reports, box_task_report_t) = (box_task_report_t){
        .id = result,
        .name = box_task_intern(t, name),
        .blocking = blocking,
        .total = total > 0 ? total : 0
    };
    t->dirty = true;
    ecs_os_mutex_unlock(t->lock);
done:
    return result;
}

/** Change the progress of a report.
 *
 * @param world The world or a stage
 * @param task The report
 * @param add Whether done adds to the units that finished instead of replacing them
 * @param done Units that finished
 * @param total Units of work, negative to keep them
 */
static void box_task_update(
    ecs_world_t *world,
    box_task_id_t task,
    bool add,
    int32_t done,
    int32_t total)
{
    box_tasks_t *t = task ? box_tasks_get(world) : NULL;
    int32_t index;
    if (!t) {
        goto done;
    }

    ecs_os_mutex_lock(t->lock);
    index = box_task_index(t, task);
    if (index >= 0) {
        box_task_report_t *r = ecs_vec_get_t(&t->reports, box_task_report_t, index);
        int32_t d = add ? r->done + done : done;
        int32_t n = total >= 0 ? total : r->total;
        if (d != r->done || n != r->total) {
            r->done = d;
            r->total = n;
            t->dirty = true;
        }
    }

    ecs_os_mutex_unlock(t->lock);
done:
    return;
}

void box_task_advance(
    ecs_world_t *world,
    box_task_id_t task,
    int32_t done)
{
    box_task_update(world, task, true, done, -1);
}

void box_task_set(
    ecs_world_t *world,
    box_task_id_t task,
    int32_t done,
    int32_t total)
{
    box_task_update(world, task, false, done, total > 0 ? total : 0);
}

void box_task_end(
    ecs_world_t *world,
    box_task_id_t task)
{
    box_tasks_t *t = task ? box_tasks_get(world) : NULL;
    int32_t index;
    if (!t) {
        goto done;
    }

    ecs_os_mutex_lock(t->lock);
    index = box_task_index(t, task);
    if (index >= 0) {
        ecs_vec_remove_ordered_t(&t->reports, box_task_report_t, index);
        t->dirty = true;
    }

    ecs_os_mutex_unlock(t->lock);
done:
    return;
}

void box_task_remaining(
    ecs_world_t *world,
    box_task_track_t *track,
    const char *name,
    bool blocking,
    int32_t remaining)
{
    if (remaining > track->remaining) {
        track->total += remaining - track->remaining;
    }

    track->remaining = remaining > 0 ? remaining : 0;
    if (track->remaining && !track->id) {
        track->id = box_task_start(world, name, blocking, track->total);
    }

    if (track->remaining) {
        box_task_set(world, track->id, track->total - track->remaining, track->total);
    } else {
        box_task_end(world, track->id);
        track->id = 0;
        track->total = 0;
    }
}

const InProgressTaskItem* box_task_find(
    const InProgressTask *tasks,
    const char *name)
{
    const InProgressTaskItem *result = NULL;
    int32_t i;
    for (i = 0; tasks && name && i < tasks->count && !result; i ++) {
        if (tasks->tasks[i].name && !ecs_os_strcmp(tasks->tasks[i].name, name)) {
            result = &tasks->tasks[i];
        }
    }

    return result;
}

/** Combine the reports into the entries of InProgressTask, blocking ones first. Called under
 * the lock.
 *
 * @param t The registry
 * @param out Receives the aggregate
 */
static void box_task_aggregate(
    box_tasks_t *t,
    InProgressTask *out)
{
    InProgressTaskItem items[BOX_TASK_MAX];
    bool unknown[BOX_TASK_MAX] = {0};
    int32_t i, j, n = 0, count = ecs_vec_count(&t->reports);
    ecs_os_memset_t(out, 0, InProgressTask);
    ecs_os_memset_n(items, 0, InProgressTaskItem, BOX_TASK_MAX);
    for (i = 0; i < count; i ++) {
        const box_task_report_t *r = ecs_vec_get_t(&t->reports, box_task_report_t, i);
        for (j = 0; j < n && items[j].name != r->name; j ++) { }
        if (j == n && n == BOX_TASK_MAX) {
            continue;
        }

        if (j == n) {
            items[n ++].name = ECS_CONST_CAST(char*, r->name);
        }

        items[j].blocking = items[j].blocking || r->blocking;
        items[j].done += r->done;
        items[j].total += r->total;
        unknown[j] = unknown[j] || !r->total;
    }

    for (i = 0; i < n; i ++) {
        if (unknown[i]) {
            items[i].total = 0;
        }

        if (items[i].blocking) {
            out->tasks[out->count ++] = items[i];
            out->blocking ++;
        }
    }

    for (i = 0; i < n; i ++) {
        if (!items[i].blocking) {
            out->tasks[out->count ++] = items[i];
        }
    }
}

/** Whether two InProgressTask values list the same tasks with the same progress.
 *
 * @param a A value
 * @param b The other value
 * @return Whether they are equal
 */
static bool box_task_equal(
    const InProgressTask *a,
    const InProgressTask *b)
{
    bool result = a->count == b->count && a->blocking == b->blocking;
    int32_t i;
    for (i = 0; i < a->count && result; i ++) {
        const InProgressTaskItem *x = &a->tasks[i], *y = &b->tasks[i];
        result = x->blocking == y->blocking && x->done == y->done && x->total == y->total &&
            !ecs_os_strcmp(x->name, y->name);
    }

    return result;
}

void box_task_publish(
    ecs_world_t *world)
{
    box_tasks_t *t = box_tasks_get(world);
    const InProgressTask *cur;
    InProgressTask next;
    bool dirty;
    if (!t) {
        goto done;
    }

    cur = ecs_singleton_get(world, InProgressTask);

    ecs_os_mutex_lock(t->lock);
    dirty = t->dirty;
    t->dirty = false;
    if (dirty) {
        box_task_aggregate(t, &next);
    }

    ecs_os_mutex_unlock(t->lock);
    if (dirty && (!cur || !box_task_equal(cur, &next))) {
        ecs_singleton_set_ptr(world, InProgressTask, &next);
    }

done:
    return;
}

/** Write InProgressTask at the end of the frame.
 *
 * @param it Iterator of the system, which has no terms
 */
static void BoxTaskPublish(
    ecs_iter_t *it)
{
    box_task_publish(it->world);
}

/** Free the registry of a world.
 *
 * @param t The registry
 */
static void box_tasks_free(
    box_tasks_t *t)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&t->names); i ++) {
        ecs_os_free(*ecs_vec_get_t(&t->names, char*, i));
    }

    ecs_vec_fini_t(NULL, &t->names, char*);
    ecs_vec_fini_t(NULL, &t->reports, box_task_report_t);
    ecs_os_mutex_free(t->lock);
    ecs_os_free(t);
}

/** Free the names of InProgressTaskItem values.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti The type info
 */
static void box_task_item_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    InProgressTaskItem *items = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        ecs_os_free(items[i].name);
    }
}

/** Copy InProgressTaskItem values with their own names.
 *
 * @param dst The destination values
 * @param src The source values
 * @param count Number of values
 * @param ti The type info
 */
static void box_task_item_copy(
    void *dst,
    const void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    InProgressTaskItem *d = dst;
    const InProgressTaskItem *s = src;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        char *name = ecs_os_strdup(s[i].name);
        ecs_os_free(d[i].name);
        d[i] = s[i];
        d[i].name = name;
    }
}

/** Move the names of InProgressTaskItem values.
 *
 * @param dst The destination values
 * @param src The source values
 * @param count Number of values
 * @param ti The type info
 */
static void box_task_item_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    InProgressTaskItem *d = dst;
    InProgressTaskItem *s = src;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        if (d[i].name != s[i].name) {
            ecs_os_free(d[i].name);
        }

        d[i] = s[i];
        s[i].name = NULL;
    }
}

/** Free the names of InProgressTask values.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti The type info
 */
static void box_task_tasks_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    InProgressTask *t = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        box_task_item_dtor(t[i].tasks, BOX_TASK_MAX, ti);
    }
}

/** Copy InProgressTask values with their own names.
 *
 * @param dst The destination values
 * @param src The source values
 * @param count Number of values
 * @param ti The type info
 */
static void box_task_tasks_copy(
    void *dst,
    const void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    InProgressTask *d = dst;
    const InProgressTask *s = src;
    int32_t i;
    for (i = 0; i < count; i ++) {
        d[i].count = s[i].count;
        d[i].blocking = s[i].blocking;
        box_task_item_copy(d[i].tasks, s[i].tasks, BOX_TASK_MAX, ti);
    }
}

/** Move the names of InProgressTask values.
 *
 * @param dst The destination values
 * @param src The source values
 * @param count Number of values
 * @param ti The type info
 */
static void box_task_tasks_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    InProgressTask *d = dst;
    InProgressTask *s = src;
    int32_t i;
    for (i = 0; i < count; i ++) {
        d[i].count = s[i].count;
        d[i].blocking = s[i].blocking;
        box_task_item_move(d[i].tasks, s[i].tasks, BOX_TASK_MAX, ti);
    }
}

/** Free the registries of TaskRegistry values.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti The type info
 */
static void box_task_registry_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    TaskRegistry *r = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        if (r[i].tasks) {
            box_tasks_free(r[i].tasks);
        }
    }
}

/** Move TaskRegistry values, the source no longer owns its registry.
 *
 * @param dst The destination values
 * @param src The source values
 * @param count Number of values
 * @param ti The type info
 */
static void box_task_registry_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ecs_os_memcpy(dst, src, ti->size * count);
    ecs_os_memset(src, 0, ti->size * count);
}

void BoxTaskImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxTask);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, InProgressTaskItem);
    ECS_META_COMPONENT(world, InProgressTask);
    ecs_set_hooks(world, InProgressTaskItem, {
        .ctor = flecs_default_ctor,
        .dtor = box_task_item_dtor,
        .copy = box_task_item_copy,
        .move = box_task_item_move
    });
    ecs_set_hooks(world, InProgressTask, {
        .ctor = flecs_default_ctor,
        .dtor = box_task_tasks_dtor,
        .copy = box_task_tasks_copy,
        .move = box_task_tasks_move
    });
    ecs_set_name_prefix(world, "Task");
    ECS_COMPONENT_DEFINE(world, TaskRegistry);
    ecs_set_name_prefix(world, NULL);
    ecs_set_hooks(world, TaskRegistry, {
        .ctor = flecs_default_ctor,
        .dtor = box_task_registry_dtor,
        .move = box_task_registry_move
    });

    box_tasks_t *t = ecs_os_calloc_t(box_tasks_t);
    t->lock = ecs_os_mutex_new();
    ecs_vec_init_t(NULL, &t->reports, box_task_report_t, 0);
    ecs_vec_init_t(NULL, &t->names, char*, 0);
    ecs_singleton_ensure(world, TaskRegistry)->tasks = t;
    ecs_singleton_set(world, InProgressTask, {0});

    TaskPublish = ecs_entity(world, { .name = "Publish" });
    ecs_add_id(world, TaskPublish, EcsPhase);
    ecs_add_pair(world, TaskPublish, EcsDependsOn, EcsPostFrame);
    ecs_id(TaskPublish) = TaskPublish;

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "TaskPublishTasks" }),
        .phase = TaskPublish,
        .run = BoxTaskPublish,
        .immediate = true
    });
}
