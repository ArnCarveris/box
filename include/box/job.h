/**
 * @file box/job.h
 * @brief Worker pool that runs large tasks without spiking a frame.
 *
 * The box.job module keeps a pool of workers, orders queued work by priority and
 * distance to the eye, and reports completion on the main thread in PreUpdate
 * inside a per frame budget. Modules submit work with the internal API in
 * src/job/job_internal.h; scripts only see the two components below.
 */

#ifndef BOX_JOB_H
#define BOX_JOB_H

#include "types.h"

#ifdef BOX_JOB_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Configuration of the job system, applied at the start of every frame */
ECS_STRUCT(JobSettings, {
    int32_t workers;           /**< Workers, 0 for core count minus one, negative for none */
    double budget_ms;          /**< Main thread milliseconds per frame, 0 for no limit */
    bool prioritize_near;      /**< Order jobs of equal priority by distance to eye */
    bool complete_on_shutdown; /**< Run pending completions when the world is destroyed */
    vec3 eye;                  /**< Position distances are measured from */
});

/** Job counters, written by the engine after every drain */
ECS_STRUCT(JobStats, {
    int32_t queued;             /**< Jobs waiting for a worker */
    int32_t running;            /**< Jobs on a worker */
    int32_t completed;          /**< Completions run by the last drain */
    int32_t dropped;            /**< Jobs cancelled or dropped since startup */
    double worst_completion_ms; /**< Longest a completion took on the main thread */
});

/** Register the box.job module.
 *
 * @param world The world
 */
void BoxJobImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
