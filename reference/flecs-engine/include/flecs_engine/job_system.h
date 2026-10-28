#ifndef FLECS_ENGINE_JOB_SYSTEM_H
#define FLECS_ENGINE_JOB_SYSTEM_H

#include <flecs.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*flecs_job_fn_t)(
    void *ctx,
    int32_t begin,
    int32_t end,
    int32_t worker);

typedef uint64_t flecs_job_handle_t;

int32_t flecsEngine_jobsInit(
    int32_t threads);

void flecsEngine_jobsFini(void);

int32_t flecsEngine_jobsThreads(void);

bool flecsEngine_jobsParallel(void);

flecs_job_handle_t flecsEngine_jobsDispatch(
    flecs_job_fn_t fn,
    void *ctx,
    int32_t count,
    int32_t chunk);

void flecsEngine_jobsWait(
    flecs_job_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif
