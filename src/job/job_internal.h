/**
 * @file job/job_internal.h
 * @brief Job API other box modules use to move work off the main thread.
 *
 * A job runs its run callback on a worker and its complete callback on the main
 * thread in PreUpdate. The context is owned by the caller: a job that is
 * cancelled or dropped never reaches its complete callback.
 */

#ifndef BOX_JOB_INTERNAL_H
#define BOX_JOB_INTERNAL_H

#include <box.h>

/** Handle of a submitted job, 0 when the job was not submitted */
typedef uint64_t box_job_t;

/** The worker pool of a world */
typedef struct box_job_pool_t box_job_pool_t;

/** Work executed on a worker thread; it may not touch the world.
 *
 * @param ctx The context of the job
 */
typedef void (*box_job_run_t)(
    void *ctx);

/** Completion executed on the main thread in PreUpdate.
 *
 * @param world The world
 * @param ctx The context of the job
 */
typedef void (*box_job_complete_t)(
    ecs_world_t *world,
    void *ctx);

/** Description of a job to submit */
typedef struct box_job_desc_t {
    box_job_run_t run;           /**< Runs on a worker */
    box_job_complete_t complete; /**< Runs on the main thread, optional */
    void *ctx;                   /**< Passed to both callbacks, owned by the caller */
    int32_t priority;            /**< Higher priority runs first */
    vec3 position;               /**< World position the work belongs to */
    bool has_position;           /**< Order by distance to the eye within a priority */
    uint64_t group;              /**< Group for cancel and supersede, 0 for none */
} box_job_desc_t;

/** Pool of a world, held by a private singleton */
typedef struct JobPool {
    box_job_pool_t *pool; /**< Freed when the component is destroyed */
} JobPool;

extern ECS_COMPONENT_DECLARE(JobPool);

/** Queue a job.
 *
 * @param world The world
 * @param desc The job
 * @return The handle of the job, 0 when it was not queued
 */
box_job_t box_job_submit(
    ecs_world_t *world,
    const box_job_desc_t *desc);

/** Cancel a job. A queued job never runs, a running job finishes without its
 * completion and a completion that is still queued is dropped.
 *
 * @param world The world
 * @param job The handle of the job
 * @return true when the job was found
 */
bool box_job_cancel(
    ecs_world_t *world,
    box_job_t job);

/** Cancel the queued jobs of a group and queue a job in their place.
 *
 * @param world The world
 * @param group The group to cancel, 0 cancels nothing
 * @param desc The job, NULL only cancels
 * @return The handle of the new job, 0 when none was queued
 */
box_job_t box_job_supersede(
    ecs_world_t *world,
    uint64_t group,
    const box_job_desc_t *desc);

/** Run every queued job and completion, ignoring the frame budget.
 *
 * @param world The world
 */
void box_job_flush(
    ecs_world_t *world);

/** Set the position distances are measured from. The render module calls this
 * with the position of the active camera; until it does the eye stays at the
 * origin and only the priority of a job orders the queue.
 *
 * @param world The world
 * @param eye The eye position
 */
void box_job_set_eye(
    ecs_world_t *world,
    vec3 eye);

#endif
