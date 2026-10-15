/**
 * @file jobs/jobs.c
 * @brief Registers the box.jobs module, its worker pool and the systems that finish jobs.
 */

#include "jobs.h"
#include <unistd.h>

/** A job from submission until its completion ran */
typedef struct box_job_t {
    box_job_desc_t desc;            /**< What to run */
    struct box_job_t *next;         /**< Next job in submission order, or on the free list */
    struct box_job_t *next_pending; /**< Next job waiting for a worker */
    bool done;                      /**< The work finished */
} box_job_t;

/** Worker pool of one world; every field after threads is guarded by lock */
typedef struct box_jobs_t {
    ecs_os_mutex_t lock;          /**< Guards the queues and flags */
    ecs_os_cond_t wake;           /**< Signalled when work arrives or the pool stops */
    ecs_os_thread_t *threads;     /**< Worker threads */
    int32_t thread_count;         /**< Number of worker threads, 0 runs jobs inline */
    int32_t workers;              /**< Configured worker count or BOX_JOBS_AUTO */
    bool started;                 /**< The workers were started */
    bool quit;                    /**< Workers exit when they see this */
    box_job_t *head;              /**< Oldest job that did not complete */
    box_job_t *tail;              /**< Newest job that did not complete */
    box_job_t *pending_head;      /**< Oldest job waiting for a worker */
    box_job_t *pending_tail;      /**< Newest job waiting for a worker */
    box_job_t *free_list;         /**< Jobs that can be reused */
    int32_t count;                /**< Jobs that did not complete */
    int32_t running;              /**< Jobs taken for work whose done flag is not set */
} box_jobs_t;

/** Private singleton that holds the pool of a world */
typedef struct JobPool {
    box_jobs_t *jobs; /**< Freed when the world is deleted */
} JobPool;

static ECS_COMPONENT_DECLARE(JobPool);

/** Take the oldest job that waits for a worker.
 *
 * @param jobs The pool, locked
 * @return The job, or NULL when none is waiting
 */
static box_job_t* box_jobs_take(
    box_jobs_t *jobs)
{
    box_job_t *result = jobs->pending_head;
    if (result) {
        jobs->pending_head = result->next_pending;
        if (!jobs->pending_head) {
            jobs->pending_tail = NULL;
        }

        result->next_pending = NULL;
        jobs->running ++;
    }

    return result;
}

/** Run jobs until the pool stops.
 *
 * @param arg The pool
 * @return NULL
 */
static void* box_jobs_worker(
    void *arg)
{
    box_jobs_t *jobs = arg;
    ecs_os_mutex_lock(jobs->lock);
    while (!jobs->quit) {
        box_job_t *job = box_jobs_take(jobs);
        if (job) {
            ecs_os_mutex_unlock(jobs->lock);
            job->desc.work(job->desc.ctx);
            ecs_os_mutex_lock(jobs->lock);
            job->done = true;
            jobs->running --;
        } else {
            ecs_os_cond_wait(jobs->wake, jobs->lock);
        }
    }

    ecs_os_mutex_unlock(jobs->lock);

    return NULL;
}

/** Count the online cores of the machine.
 *
 * @return Number of cores, at least 1
 */
static int32_t box_jobs_cores(void)
{
    long result = sysconf(_SC_NPROCESSORS_ONLN);
    if (result < 1) {
        result = 1;
    }

    return (int32_t)result;
}

/** Decide how many workers a world gets.
 *
 * @param world The world
 * @param jobs The pool
 * @return Number of worker threads, 0 to run jobs inline
 */
static int32_t box_jobs_thread_count(
    const ecs_world_t *world,
    const box_jobs_t *jobs)
{
    int32_t result = jobs->workers;
#ifdef __EMSCRIPTEN__
    result = 0;
#else
    if (result == BOX_JOBS_AUTO) {
        const App *app = ecs_singleton_get(world, App);
        float budget = 0;
        if (app && app->headless && !app->unpaced) {
            budget = app->cpu_budget;
        }

        result = box_jobs_worker_count(box_jobs_cores(), budget);
    }
#endif

    return result;
}

/** Start the workers of a pool.
 *
 * @param world The world
 * @param jobs The pool
 */
static void box_jobs_start(
    const ecs_world_t *world,
    box_jobs_t *jobs)
{
    int32_t i, count = box_jobs_thread_count(world, jobs);
    jobs->started = true;
    jobs->thread_count = count;
    if (count) {
        jobs->threads = ecs_os_calloc_n(ecs_os_thread_t, count);
    }

    for (i = 0; i < count; i ++) {
        jobs->threads[i] = ecs_os_thread_new(box_jobs_worker, jobs);
    }
}

/** Stop the workers and free the pool, freeing the context of every job that did not
 * complete.
 *
 * @param world The world
 * @param ctx The pool
 */
static void box_jobs_fini(
    ecs_world_t *world,
    void *ctx)
{
    box_jobs_t *jobs = ctx;
    box_job_t *job, *next;
    int32_t i;
    (void)world;

    ecs_os_mutex_lock(jobs->lock);
    jobs->quit = true;
    ecs_os_cond_broadcast(jobs->wake);
    ecs_os_mutex_unlock(jobs->lock);

    for (i = 0; i < jobs->thread_count; i ++) {
        ecs_os_thread_join(jobs->threads[i]);
    }

    for (job = jobs->head; job; job = next) {
        next = job->next;
        if (job->desc.ctx_free) {
            job->desc.ctx_free(job->desc.ctx);
        }

        ecs_os_free(job);
    }

    for (job = jobs->free_list; job; job = next) {
        next = job->next;
        ecs_os_free(job);
    }

    ecs_os_free(jobs->threads);
    ecs_os_cond_free(jobs->wake);
    ecs_os_mutex_free(jobs->lock);
    ecs_os_free(jobs);
}

/** Find the pool of a world.
 *
 * @param world The world or a stage
 * @return The pool, or NULL when box.jobs is not imported
 */
static box_jobs_t* box_jobs_get(
    const ecs_world_t *world)
{
    const JobPool *pool = NULL;
    box_jobs_t *result = NULL;
    if (ecs_id(JobPool)) {
        pool = ecs_singleton_get(ecs_get_world(world), JobPool);
    }

    if (pool) {
        result = pool->jobs;
    }

    return result;
}

/** Run the queued work of a pool without workers, as the browser build does.
 *
 * @param it The iterator of the system
 */
static void BoxJobsInline(
    ecs_iter_t *it)
{
    box_jobs_t *jobs = it->ctx;
    box_job_t *job;
    if (jobs->thread_count) {
        goto done;
    }

    ecs_os_mutex_lock(jobs->lock);
    while ((job = box_jobs_take(jobs))) {
        ecs_os_mutex_unlock(jobs->lock);
        job->desc.work(job->desc.ctx);
        ecs_os_mutex_lock(jobs->lock);
        job->done = true;
        jobs->running --;
    }

    ecs_os_mutex_unlock(jobs->lock);
done:
    return;
}

/** Run the completions of finished jobs in submission order, stopping at the first job
 * whose work did not finish.
 *
 * @param it The iterator of the system
 */
static void BoxJobsComplete(
    ecs_iter_t *it)
{
    box_jobs_t *jobs = it->ctx;
    box_job_t *first, *last = NULL, *job;
    int32_t count = 0;

    ecs_os_mutex_lock(jobs->lock);
    first = jobs->head;
    for (job = first; job && job->done; job = job->next) {
        last = job;
        count ++;
    }

    if (last) {
        jobs->head = last->next;
        if (!jobs->head) {
            jobs->tail = NULL;
        }

        last->next = NULL;
    }

    ecs_os_mutex_unlock(jobs->lock);
    if (!count) {
        goto done;
    }

    for (job = first; job; job = job->next) {
        if (job->desc.complete) {
            job->desc.complete(it->world, job->desc.ctx);
        }

        if (job->desc.ctx_free) {
            job->desc.ctx_free(job->desc.ctx);
        }
    }

    ecs_os_mutex_lock(jobs->lock);
    last->next = jobs->free_list;
    jobs->free_list = first;
    jobs->count -= count;
    ecs_os_mutex_unlock(jobs->lock);
done:
    return;
}

int32_t box_jobs_worker_count(
    int32_t cores,
    float cpu_budget)
{
    int32_t result = cores - BOX_JOBS_RESERVED_CORES;
    if (cpu_budget > 0) {
        int32_t budget = (int32_t)(cpu_budget / 100.0f) - 1;
        if (budget < result) {
            result = budget;
        }
    }

    if (result < 1) {
        result = 1;
    }

    if (result > BOX_JOBS_MAX_WORKERS) {
        result = BOX_JOBS_MAX_WORKERS;
    }

    return result;
}

int box_job_submit(
    ecs_world_t *world,
    const box_job_desc_t *desc)
{
    box_jobs_t *jobs = box_jobs_get(world);
    box_job_t *job;
    int result = -1;
    if (!jobs) {
        ecs_err("box.jobs is not imported");
        goto done;
    }

    if (!desc || !desc->work) {
        ecs_err("a job needs a work callback");
        goto done;
    }

    ecs_os_mutex_lock(jobs->lock);
    if (!jobs->started) {
        box_jobs_start(ecs_get_world(world), jobs);
    }

    job = jobs->free_list;
    if (job) {
        jobs->free_list = job->next;
    } else {
        job = ecs_os_malloc_t(box_job_t);
    }

    *job = (box_job_t){ .desc = *desc };
    if (jobs->tail) {
        jobs->tail->next = job;
    } else {
        jobs->head = job;
    }

    jobs->tail = job;
    if (jobs->pending_tail) {
        jobs->pending_tail->next_pending = job;
    } else {
        jobs->pending_head = job;
    }

    jobs->pending_tail = job;
    jobs->count ++;
    ecs_os_cond_signal(jobs->wake);
    ecs_os_mutex_unlock(jobs->lock);
    result = 0;
done:
    return result;
}

int box_jobs_set_workers(
    ecs_world_t *world,
    int32_t workers)
{
    box_jobs_t *jobs = box_jobs_get(world);
    int result = -1;
    if (!jobs) {
        goto done;
    }

    ecs_os_mutex_lock(jobs->lock);
    if (!jobs->started) {
        jobs->workers = workers;
        result = 0;
    }

    ecs_os_mutex_unlock(jobs->lock);
done:
    return result;
}

int32_t box_jobs_pending(
    ecs_world_t *world)
{
    box_jobs_t *jobs = box_jobs_get(world);
    int32_t result = 0;
    if (jobs) {
        ecs_os_mutex_lock(jobs->lock);
        result = jobs->count;
        ecs_os_mutex_unlock(jobs->lock);
    }

    return result;
}

void BoxJobsImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxJobs);

    ECS_COMPONENT_DEFINE(world, JobPool);

    box_jobs_t *jobs = ecs_os_calloc_t(box_jobs_t);
    jobs->lock = ecs_os_mutex_new();
    jobs->wake = ecs_os_cond_new();
    jobs->workers = BOX_JOBS_AUTO;
    ecs_singleton_set(world, JobPool, { .jobs = jobs });
    ecs_atfini(world, box_jobs_fini, jobs);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "JobsComplete" }),
        .phase = EcsOnLoad,
        .run = BoxJobsComplete,
        .ctx = jobs,
        .immediate = true
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "JobsInline" }),
        .phase = EcsPostFrame,
        .run = BoxJobsInline,
        .ctx = jobs
    });
}

int32_t box_jobs_running(
    ecs_world_t *world)
{
    box_jobs_t *jobs = box_jobs_get(world);
    int32_t result = 0;
    if (jobs) {
        ecs_os_mutex_lock(jobs->lock);
        result = jobs->running;
        ecs_os_mutex_unlock(jobs->lock);
    }

    return result;
}
