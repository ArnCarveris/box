/**
 * @file jobs/jobs.h
 * @brief Job system that box modules use to run long work such as IO on worker threads.
 *
 * Work runs on a worker without access to the world, completions run on the main thread
 * in the box.jobs.Complete system in submission order. This header is private to box modules.
 */

#ifndef BOX_JOBS_H
#define BOX_JOBS_H

#include <box.h>

/** Worker setting that sizes the pool from the core count and the CPU budget */
#define BOX_JOBS_AUTO (-1)

/** Largest number of worker threads the pool starts */
#define BOX_JOBS_MAX_WORKERS (16)

/** Cores kept free for the main thread, the render thread and the audio thread of the device */
#define BOX_JOBS_RESERVED_CORES (3)

/** Work of a job, run on a worker thread; it may not touch the world.
 *
 * @param ctx The context of the job
 */
typedef void (*box_job_work_t)(
    void *ctx);

/** Completion of a job, run on the main thread after its work finished.
 *
 * @param world The world
 * @param ctx The context of the job
 */
typedef void (*box_job_complete_t)(
    ecs_world_t *world,
    void *ctx);

/** Frees the context of a job after its completion, or at shutdown instead of it.
 *
 * @param ctx The context of the job
 */
typedef void (*box_job_free_t)(
    void *ctx);

/** Description of a job */
typedef struct box_job_desc_t {
    box_job_work_t work;         /**< Runs on a worker thread, required */
    box_job_complete_t complete; /**< Runs on the main thread, optional */
    box_job_free_t ctx_free;     /**< Frees ctx on the main thread, optional */
    void *ctx;                   /**< Passed to every callback */
} box_job_desc_t;

/** Register the box.jobs module.
 *
 * @param world The world
 */
void BoxJobsImport(
    ecs_world_t *world);

/** Queue a job. The first job of a world starts its workers.
 *
 * Jobs that have not completed when the world is deleted never reach their completion:
 * queued work is dropped, running work finishes and ctx_free runs for every job.
 *
 * @param world The world or a stage
 * @param desc The job
 * @return 0 when the job was queued, nonzero when it was not
 */
int box_job_submit(
    ecs_world_t *world,
    const box_job_desc_t *desc);

/** Set the number of workers before the first job starts them.
 *
 * @param world The world
 * @param workers Number of worker threads, 0 runs jobs inline at the end of the frame,
 *                BOX_JOBS_AUTO sizes the pool from the core count and the CPU budget
 * @return 0 when the setting was applied, nonzero when the workers already started
 */
int box_jobs_set_workers(
    ecs_world_t *world,
    int32_t workers);

/** Count the jobs whose completion has not run yet.
 *
 * @param world The world
 * @return Number of submitted jobs that did not complete
 */
int32_t box_jobs_pending(
    ecs_world_t *world);

/** Count the jobs a worker took whose work is not marked finished yet.
 *
 * Work that signals its own module before it returns is not yet visible to box.jobs.Complete, so a
 * headless frame that waits for its module's work also waits for this to reach 0.
 *
 * @param world The world
 * @return Number of jobs whose work runs or just returned
 */
int32_t box_jobs_running(
    ecs_world_t *world);

/** Compute the size of the pool.
 *
 * @param cores Number of online cores
 * @param cpu_budget Percentage of one core the process may use, 0 for no limit
 * @return Number of worker threads, at least 1 and at most BOX_JOBS_MAX_WORKERS
 */
int32_t box_jobs_worker_count(
    int32_t cores,
    float cpu_budget);

#endif
