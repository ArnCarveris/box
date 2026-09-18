/**
 * @file Job.c
 * @brief Benchmarks for job throughput and the budgeted completion drain.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/job/job_internal.h"

/** Jobs the throughput case submits per iteration */
#define BENCH_JOB_COUNT (100000)

/** Jobs queued before every measured frame */
#define BENCH_DRAIN_COUNT (10000)

/** Workers the drain case runs with */
#define BENCH_DRAIN_WORKERS (4)

static int32_t bench_job_runs;

/** Count one job on a worker.
 *
 * @param ctx Unused
 */
static void bench_job_run(
    void *ctx)
{
    (void)ctx;
    ecs_os_ainc(&bench_job_runs);
}

/** Count one completion on the main thread.
 *
 * @param world Unused
 * @param ctx Unused
 */
static void bench_job_complete(
    ecs_world_t *world,
    void *ctx)
{
    (void)world;
    (void)ctx;
    bench_job_runs ++;
}

void Job_submit_100k(bench_t *b)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    box_job_desc_t desc = {0};
    desc.run = bench_job_run;
    bench_set_items(b, BENCH_JOB_COUNT);

    while (bench_iter(b)) {
        int32_t i;
        for (i = 0; i < BENCH_JOB_COUNT; i ++) {
            box_job_submit(world, &desc);
        }

        box_job_flush(world);
        bench_keep(bench_job_runs);
    }

    ecs_fini(world);
}

void Job_frame_drain(bench_t *b)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    JobSettings *settings = ecs_singleton_ensure(world, JobSettings);
    settings->workers = BENCH_DRAIN_WORKERS;
    settings->budget_ms = 2.0;
    ecs_singleton_modified(world, JobSettings);
    ecs_progress(world, 0);

    box_job_desc_t desc = {0};
    desc.run = bench_job_run;
    desc.complete = bench_job_complete;

    while (bench_iter(b)) {
        bench_pause(b);
        int32_t i;
        for (i = 0; i < BENCH_DRAIN_COUNT; i ++) {
            box_job_submit(world, &desc);
        }

        bench_resume(b);
        ecs_progress(world, 0);
        bench_pause(b);
        box_job_flush(world);
        bench_resume(b);
        bench_keep(bench_job_runs);
    }

    ecs_fini(world);
}
