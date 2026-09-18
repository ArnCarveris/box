/**
 * @file job/pool.c
 * @brief Priority queue, worker threads and budgeted completion drain.
 */

#include "pool.h"

#ifndef BOX_PLATFORM_WEB
#include <unistd.h>
#endif

/** Most worker threads a pool starts */
#define BOX_JOB_MAX_WORKERS (64)

/** Order two queued jobs.
 *
 * @param a The first job
 * @param b The second job
 * @return true when a runs before b
 */
static bool box_job_before(
    const box_job_entry_t *a,
    const box_job_entry_t *b)
{
    bool result;
    if (a->desc.priority != b->desc.priority) {
        result = a->desc.priority > b->desc.priority;
    } else if (a->ordered && b->ordered && a->distance != b->distance) {
        result = a->distance < b->distance;
    } else {
        result = a->id < b->id;
    }

    return result;
}

/** Move an entry towards the root until its parent runs before it.
 *
 * @param pool The pool
 * @param index Index of the entry
 */
static void box_job_heap_up(
    box_job_pool_t *pool,
    int32_t index)
{
    box_job_entry_t *array = ecs_vec_first_t(&pool->queue, box_job_entry_t);
    while (index > 0) {
        int32_t parent = (index - 1) / 2;
        if (!box_job_before(&array[index], &array[parent])) {
            break;
        }

        box_job_entry_t tmp = array[parent];
        array[parent] = array[index];
        array[index] = tmp;
        index = parent;
    }
}

/** Move an entry towards the leaves until it runs before its children.
 *
 * @param pool The pool
 * @param index Index of the entry
 */
static void box_job_heap_down(
    box_job_pool_t *pool,
    int32_t index)
{
    box_job_entry_t *array = ecs_vec_first_t(&pool->queue, box_job_entry_t);
    int32_t count = ecs_vec_count(&pool->queue);
    while (index < count) {
        int32_t left = index * 2 + 1;
        int32_t right = left + 1;
        int32_t best = index;
        if (left < count && box_job_before(&array[left], &array[best])) {
            best = left;
        }

        if (right < count && box_job_before(&array[right], &array[best])) {
            best = right;
        }

        if (best == index) {
            break;
        }

        box_job_entry_t tmp = array[best];
        array[best] = array[index];
        array[index] = tmp;
        index = best;
    }
}

/** Remove an entry from the heap, the caller holds the lock.
 *
 * @param pool The pool
 * @param index Index of the entry
 * @return The removed entry
 */
static box_job_entry_t box_job_heap_remove(
    box_job_pool_t *pool,
    int32_t index)
{
    box_job_entry_t *array = ecs_vec_first_t(&pool->queue, box_job_entry_t);
    int32_t count = ecs_vec_count(&pool->queue) - 1;
    box_job_entry_t result = array[index];
    array[index] = array[count];
    ecs_vec_set_count_t(NULL, &pool->queue, box_job_entry_t, count);
    if (index < count) {
        box_job_heap_down(pool, index);
        box_job_heap_up(pool, index);
    }

    return result;
}

/** Add a handle to a list of handles.
 *
 * @param list The list
 * @param id The handle
 */
static void box_job_id_add(
    ecs_vec_t *list,
    uint64_t id)
{
    uint64_t *dst = ecs_vec_append_t(NULL, list, uint64_t);
    *dst = id;
}

/** Find a handle in a list of handles.
 *
 * @param list The list
 * @param id The handle
 * @return Index of the handle, -1 when it is not in the list
 */
static int32_t box_job_id_find(
    ecs_vec_t *list,
    uint64_t id)
{
    int32_t result = -1;
    uint64_t *array = ecs_vec_first_t(list, uint64_t);
    int32_t i, count = ecs_vec_count(list);
    for (i = 0; i < count; i ++) {
        if (array[i] == id) {
            result = i;
            break;
        }
    }

    return result;
}

/** Remove a handle from a list of handles.
 *
 * @param list The list
 * @param id The handle
 * @return true when the handle was in the list
 */
static bool box_job_id_remove(
    ecs_vec_t *list,
    uint64_t id)
{
    bool result = false;
    int32_t index = box_job_id_find(list, id);
    if (index >= 0) {
        uint64_t *array = ecs_vec_first_t(list, uint64_t);
        int32_t count = ecs_vec_count(list) - 1;
        array[index] = array[count];
        ecs_vec_set_count_t(NULL, list, uint64_t, count);
        result = true;
    }

    return result;
}

/** Queue the completion of a finished job, the caller holds the lock.
 *
 * @param pool The pool
 * @param entry The finished job
 */
static void box_job_finish(
    box_job_pool_t *pool,
    const box_job_entry_t *entry)
{
    box_job_id_remove(&pool->running, entry->id);
    bool cancelled = box_job_id_remove(&pool->cancelled, entry->id);
    if (!cancelled && entry->desc.complete) {
        box_job_entry_t *dst = ecs_vec_append_t(NULL, &pool->done, box_job_entry_t);
        *dst = *entry;
    }

    if (!ecs_vec_count(&pool->queue) && !ecs_vec_count(&pool->running)) {
        ecs_os_cond_broadcast(pool->idle);
    }
}

/** Run the job at the root of the heap, the caller holds the lock.
 *
 * @param pool The pool
 */
static void box_job_run_next(
    box_job_pool_t *pool)
{
    box_job_entry_t entry = box_job_heap_remove(pool, 0);
    box_job_id_add(&pool->running, entry.id);
    ecs_os_mutex_unlock(pool->lock);
    entry.desc.run(entry.desc.ctx);
    ecs_os_mutex_lock(pool->lock);
    box_job_finish(pool, &entry);
}

#ifndef BOX_PLATFORM_WEB
/** Take jobs from the queue until the pool stops.
 *
 * @param arg The pool
 * @return NULL
 */
static void* box_job_worker(
    void *arg)
{
    box_job_pool_t *pool = arg;
    ecs_os_mutex_lock(pool->lock);
    while (!pool->stop) {
        if (!ecs_vec_count(&pool->queue)) {
            pool->waiting ++;
            ecs_os_cond_wait(pool->wake, pool->lock);
            pool->waiting --;
            continue;
        }

        box_job_run_next(pool);
    }

    ecs_os_mutex_unlock(pool->lock);

    return NULL;
}
#endif

/** Start worker threads.
 *
 * @param pool The pool
 * @param workers Number of threads to start
 */
static void box_job_pool_start(
    box_job_pool_t *pool,
    int32_t workers)
{
#ifndef BOX_PLATFORM_WEB
    if (workers > 0) {
        pool->threads = ecs_os_calloc_n(ecs_os_thread_t, workers);
        pool->thread_count = workers;
        int32_t i;
        for (i = 0; i < workers; i ++) {
            pool->threads[i] = ecs_os_thread_new(box_job_worker, pool);
        }
    }
#else
    (void)pool;
    (void)workers;
#endif
}

/** Stop and join the worker threads, leaving the queue untouched.
 *
 * @param pool The pool
 */
static void box_job_pool_stop(
    box_job_pool_t *pool)
{
    ecs_os_mutex_lock(pool->lock);
    pool->stop = true;
    ecs_os_cond_broadcast(pool->wake);
    ecs_os_mutex_unlock(pool->lock);
#ifndef BOX_PLATFORM_WEB
    int32_t i;
    for (i = 0; i < pool->thread_count; i ++) {
        ecs_os_thread_join(pool->threads[i]);
    }
#endif
    ecs_os_free(pool->threads);
    pool->threads = NULL;
    pool->thread_count = 0;
    ecs_os_mutex_lock(pool->lock);
    pool->stop = false;
    ecs_os_mutex_unlock(pool->lock);
}

/** Check whether a drain used up its budget.
 *
 * @param start When the drain started
 * @param budget_ms The budget in milliseconds, 0 for no limit
 * @param ran Items the drain handled so far
 * @return true when the drain must stop
 */
static bool box_job_over_budget(
    const ecs_time_t *start,
    double budget_ms,
    int32_t ran)
{
    bool result = false;
    if (ran > 0 && budget_ms > 0) {
        ecs_time_t t = *start;
        result = ecs_time_measure(&t) * 1000.0 >= budget_ms;
    }

    return result;
}

/** Take the oldest queued completion, the caller holds the lock.
 *
 * @param pool The pool
 * @param entry Out parameter for the completion
 * @return true when a completion was taken
 */
static bool box_job_done_pop(
    box_job_pool_t *pool,
    box_job_entry_t *entry)
{
    bool result = false;
    if (pool->done_head < ecs_vec_count(&pool->done)) {
        *entry = *ecs_vec_get_t(&pool->done, box_job_entry_t, pool->done_head);
        pool->done_head ++;
        if (pool->done_head == ecs_vec_count(&pool->done)) {
            ecs_vec_set_count_t(NULL, &pool->done, box_job_entry_t, 0);
            pool->done_head = 0;
        }

        result = true;
    }

    return result;
}

/** Run queued completions until the budget runs out, the caller holds the lock.
 *
 * @param pool The pool
 * @param world The world the completions run on
 * @param start When the drain started
 * @param budget_ms The budget in milliseconds, 0 for no limit
 * @param ran Items the drain handled so far
 * @return Items the drain handled in total
 */
static int32_t box_job_run_completions(
    box_job_pool_t *pool,
    ecs_world_t *world,
    const ecs_time_t *start,
    double budget_ms,
    int32_t ran)
{
    box_job_entry_t entry;
    while (!box_job_over_budget(start, budget_ms, ran)) {
        if (!box_job_done_pop(pool, &entry)) {
            break;
        }

        ecs_time_t measure;
        ecs_os_get_time(&measure);
        ecs_os_mutex_unlock(pool->lock);
        entry.desc.complete(world, entry.desc.ctx);
        double elapsed = ecs_time_measure(&measure) * 1000.0;
        ecs_os_mutex_lock(pool->lock);
        if (elapsed > pool->worst_ms) {
            pool->worst_ms = elapsed;
        }

        pool->completed ++;
        ran ++;
    }

    return ran;
}

int32_t box_job_pool_worker_count(
    int32_t workers)
{
    int32_t result = 0;
#ifndef BOX_PLATFORM_WEB
    if (workers > 0) {
        result = workers;
    } else if (!workers) {
        long cores = sysconf(_SC_NPROCESSORS_ONLN);
        result = cores > 1 ? (int32_t)cores - 1 : 0;
    }

    if (result > BOX_JOB_MAX_WORKERS) {
        result = BOX_JOB_MAX_WORKERS;
    }
#else
    (void)workers;
#endif

    return result;
}

box_job_pool_t* box_job_pool_new(
    ecs_world_t *world,
    int32_t workers)
{
    box_job_pool_t *pool = ecs_os_calloc_t(box_job_pool_t);
    pool->lock = ecs_os_mutex_new();
    pool->wake = ecs_os_cond_new();
    pool->idle = ecs_os_cond_new();
    pool->world = world;
    pool->prioritize_near = true;
    ecs_vec_init_t(NULL, &pool->queue, box_job_entry_t, 0);
    ecs_vec_init_t(NULL, &pool->done, box_job_entry_t, 0);
    ecs_vec_init_t(NULL, &pool->running, uint64_t, 0);
    ecs_vec_init_t(NULL, &pool->cancelled, uint64_t, 0);
    box_job_pool_start(pool, workers);

    return pool;
}

void box_job_pool_free(
    box_job_pool_t *pool)
{
    box_job_pool_stop(pool);
    ecs_os_mutex_lock(pool->lock);
    pool->completed = 0;
    if (pool->complete_on_shutdown) {
        ecs_time_t start;
        ecs_os_get_time(&start);
        box_job_run_completions(pool, pool->world, &start, 0, 0);
    }

    pool->dropped += ecs_vec_count(&pool->queue);
    pool->dropped += ecs_vec_count(&pool->done) - pool->done_head;
    ecs_os_mutex_unlock(pool->lock);
    ecs_vec_fini_t(NULL, &pool->queue, box_job_entry_t);
    ecs_vec_fini_t(NULL, &pool->done, box_job_entry_t);
    ecs_vec_fini_t(NULL, &pool->running, uint64_t);
    ecs_vec_fini_t(NULL, &pool->cancelled, uint64_t);
    ecs_os_cond_free(pool->wake);
    ecs_os_cond_free(pool->idle);
    ecs_os_mutex_free(pool->lock);
    ecs_os_free(pool);
}

void box_job_pool_configure(
    box_job_pool_t *pool,
    const JobSettings *settings)
{
    int32_t workers = box_job_pool_worker_count(settings->workers);
    if (workers != pool->thread_count) {
        box_job_pool_stop(pool);
        box_job_pool_start(pool, workers);
    }

    ecs_os_mutex_lock(pool->lock);
    pool->eye = settings->eye;
    pool->prioritize_near = settings->prioritize_near;
    pool->complete_on_shutdown = settings->complete_on_shutdown;
    ecs_os_cond_broadcast(pool->wake);
    ecs_os_mutex_unlock(pool->lock);
}

uint64_t box_job_pool_submit(
    box_job_pool_t *pool,
    const box_job_desc_t *desc)
{
    ecs_assert(desc->run != NULL, ECS_INVALID_PARAMETER, "job without a run callback");
    ecs_os_mutex_lock(pool->lock);
    box_job_entry_t *entry = ecs_vec_append_t(NULL, &pool->queue, box_job_entry_t);
    entry->id = ++ pool->next_id;
    entry->desc = *desc;
    entry->ordered = desc->has_position && pool->prioritize_near;
    entry->distance = 0;
    if (entry->ordered) {
        double dx = (double)desc->position.x - (double)pool->eye.x;
        double dy = (double)desc->position.y - (double)pool->eye.y;
        double dz = (double)desc->position.z - (double)pool->eye.z;
        entry->distance = dx * dx + dy * dy + dz * dz;
    }

    uint64_t result = entry->id;
    box_job_heap_up(pool, ecs_vec_count(&pool->queue) - 1);
    if (pool->waiting) {
        ecs_os_cond_signal(pool->wake);
    }

    ecs_os_mutex_unlock(pool->lock);

    return result;
}

bool box_job_pool_cancel(
    box_job_pool_t *pool,
    uint64_t id)
{
    bool result = false;
    ecs_os_mutex_lock(pool->lock);
    box_job_entry_t *array = ecs_vec_first_t(&pool->queue, box_job_entry_t);
    int32_t i, count = ecs_vec_count(&pool->queue);
    for (i = 0; i < count; i ++) {
        if (array[i].id == id) {
            box_job_heap_remove(pool, i);
            result = true;
            break;
        }
    }

    array = ecs_vec_first_t(&pool->done, box_job_entry_t);
    count = ecs_vec_count(&pool->done);
    for (i = pool->done_head; i < count && !result; i ++) {
        if (array[i].id == id) {
            ecs_vec_remove_ordered_t(&pool->done, box_job_entry_t, i);
            result = true;
        }
    }

    if (!result && box_job_id_find(&pool->running, id) >= 0) {
        box_job_id_add(&pool->cancelled, id);
        result = true;
    }

    pool->dropped += result;
    ecs_os_mutex_unlock(pool->lock);

    return result;
}

int32_t box_job_pool_cancel_group(
    box_job_pool_t *pool,
    uint64_t group)
{
    int32_t result = 0;
    ecs_os_mutex_lock(pool->lock);
    if (group) {
        int32_t i = 0;
        while (i < ecs_vec_count(&pool->queue)) {
            box_job_entry_t *array = ecs_vec_first_t(&pool->queue, box_job_entry_t);
            if (array[i].desc.group == group) {
                box_job_heap_remove(pool, i);
                result ++;
            } else {
                i ++;
            }
        }
    }

    pool->dropped += result;
    ecs_os_mutex_unlock(pool->lock);

    return result;
}

void box_job_pool_drain(
    box_job_pool_t *pool,
    ecs_world_t *world,
    double budget_ms)
{
    ecs_time_t start;
    ecs_os_get_time(&start);
    int32_t ran = 0;
    ecs_os_mutex_lock(pool->lock);
    pool->completed = 0;
    while (!pool->thread_count && ecs_vec_count(&pool->queue)) {
        if (box_job_over_budget(&start, budget_ms, ran)) {
            break;
        }

        box_job_run_next(pool);
        ran ++;
    }

    box_job_run_completions(pool, world, &start, budget_ms, ran);
    ecs_os_mutex_unlock(pool->lock);
}

void box_job_pool_flush(
    box_job_pool_t *pool,
    ecs_world_t *world)
{
    ecs_os_mutex_lock(pool->lock);
    while (ecs_vec_count(&pool->queue) || ecs_vec_count(&pool->running)) {
        if (!pool->thread_count && ecs_vec_count(&pool->queue)) {
            box_job_run_next(pool);
        } else {
            ecs_os_cond_wait(pool->idle, pool->lock);
        }
    }

    ecs_os_mutex_unlock(pool->lock);
    box_job_pool_drain(pool, world, 0);
}

void box_job_pool_stats(
    box_job_pool_t *pool,
    JobStats *stats)
{
    ecs_os_mutex_lock(pool->lock);
    stats->queued = ecs_vec_count(&pool->queue);
    stats->running = ecs_vec_count(&pool->running);
    stats->completed = pool->completed;
    stats->dropped = pool->dropped;
    stats->worst_completion_ms = pool->worst_ms;
    ecs_os_mutex_unlock(pool->lock);
}
