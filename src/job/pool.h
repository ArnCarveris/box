/**
 * @file job/pool.h
 * @brief Worker pool, priority queue and completion queue of the box.job module.
 *
 * Every field of the pool is guarded by its mutex. Callbacks always run with the
 * mutex released, so a job may submit or cancel other jobs.
 */

#ifndef BOX_JOB_POOL_H
#define BOX_JOB_POOL_H

#include "job_internal.h"

/** A job in the queue, on a worker or waiting for its completion */
typedef struct box_job_entry_t {
    uint64_t id;         /**< Handle of the job */
    box_job_desc_t desc; /**< What to run */
    double distance;     /**< Squared distance to the eye when the job was queued */
    bool ordered;        /**< The distance orders this job within its priority */
} box_job_entry_t;

/** Worker pool of one world */
struct box_job_pool_t {
    ecs_os_mutex_t lock;      /**< Guards every field below */
    ecs_os_cond_t wake;       /**< Signalled when work arrives or the pool stops */
    ecs_os_cond_t idle;       /**< Signalled when the queue and the workers ran dry */
    ecs_world_t *world;       /**< World the completions run on */
    ecs_os_thread_t *threads; /**< Worker threads */
    int32_t thread_count;     /**< Number of worker threads */
    int32_t waiting;          /**< Workers waiting for the next job */
    ecs_vec_t queue;          /**< Binary heap of box_job_entry_t */
    ecs_vec_t done;           /**< box_job_entry_t waiting for the main thread */
    int32_t done_head;        /**< First entry of done that still has to run */
    ecs_vec_t running;        /**< Handles of the jobs on a worker */
    ecs_vec_t cancelled;      /**< Handles whose completion is dropped */
    uint64_t next_id;         /**< Handle of the next job */
    int32_t completed;        /**< Completions run by the last drain */
    int32_t dropped;          /**< Jobs cancelled or dropped since startup */
    double worst_ms;          /**< Longest a completion took on the main thread */
    vec3 eye;                 /**< Position distances are measured from */
    bool prioritize_near;     /**< Order by distance within a priority */
    bool complete_on_shutdown; /**< Run pending completions when the pool is freed */
    bool stop;                /**< Workers exit when they see this */
};

/** Resolve the configured worker count into a number of threads.
 *
 * @param workers The setting, 0 for core count minus one, negative for none
 * @return The number of threads to start
 */
int32_t box_job_pool_worker_count(
    int32_t workers);

/** Create a pool and start its workers.
 *
 * @param world World the completions run on
 * @param workers Number of threads to start
 * @return The pool
 */
box_job_pool_t* box_job_pool_new(
    ecs_world_t *world,
    int32_t workers);

/** Stop the workers, drop or run what is left and free the pool.
 *
 * @param pool The pool
 */
void box_job_pool_free(
    box_job_pool_t *pool);

/** Apply the settings to the pool, resizing the worker pool when it changed.
 *
 * @param pool The pool
 * @param settings The settings
 */
void box_job_pool_configure(
    box_job_pool_t *pool,
    const JobSettings *settings);

/** Add a job to the priority queue.
 *
 * @param pool The pool
 * @param desc The job
 * @return The handle of the job
 */
uint64_t box_job_pool_submit(
    box_job_pool_t *pool,
    const box_job_desc_t *desc);

/** Cancel one job.
 *
 * @param pool The pool
 * @param id The handle of the job
 * @return true when the job was found
 */
bool box_job_pool_cancel(
    box_job_pool_t *pool,
    uint64_t id);

/** Cancel every queued job of a group.
 *
 * @param pool The pool
 * @param group The group
 * @return The number of jobs that were cancelled
 */
int32_t box_job_pool_cancel_group(
    box_job_pool_t *pool,
    uint64_t group);

/** Run jobs without workers and completions on the main thread, within the budget.
 *
 * @param pool The pool
 * @param world The world the completions run on
 * @param budget_ms Milliseconds this drain may take, 0 for no limit
 */
void box_job_pool_drain(
    box_job_pool_t *pool,
    ecs_world_t *world,
    double budget_ms);

/** Run every queued job and completion, ignoring the budget.
 *
 * @param pool The pool
 * @param world The world the completions run on
 */
void box_job_pool_flush(
    box_job_pool_t *pool,
    ecs_world_t *world);

/** Copy the counters of the pool into the stats component.
 *
 * @param pool The pool
 * @param stats The stats
 */
void box_job_pool_stats(
    box_job_pool_t *pool,
    JobStats *stats);

#endif
