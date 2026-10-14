/**
 * @file Jobs.c
 * @brief Benchmarks for the throughput of submitting jobs and running their completions.
 */

#include <box.h>
#include <bake_bench.h>
#include "jobs.h"

/** Jobs submitted per iteration */
#define BENCH_JOB_COUNT (1000)

/** Worker threads of the threaded cases */
#define BENCH_JOB_WORKERS (4)

/** Jobs whose work ran */
static int32_t bench_jobs_worked;

/** Jobs whose completion ran */
static int32_t bench_jobs_completed;

/** Count the work of a job on a worker.
 *
 * @param ctx Unused
 */
static void bench_jobs_work(
    void *ctx)
{
    (void)ctx;
    ecs_os_ainc(&bench_jobs_worked);
}

/** Count the completion of a job on the main thread.
 *
 * @param world Unused
 * @param ctx Unused
 */
static void bench_jobs_complete(
    ecs_world_t *world,
    void *ctx)
{
    (void)world;
    (void)ctx;
    bench_jobs_completed ++;
}

/** Create a world whose pool runs with a fixed number of workers.
 *
 * @param workers Worker threads, 0 runs jobs inline at the end of the frame
 * @return The world
 */
static ecs_world_t* bench_jobs_world(
    int32_t workers)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_jobs_set_workers(world, workers);
    ecs_progress(world, 0);

    return world;
}

/** Submit a batch of jobs.
 *
 * @param world The world
 */
static void bench_jobs_submit(
    ecs_world_t *world)
{
    box_job_desc_t desc = {
        .work = bench_jobs_work,
        .complete = bench_jobs_complete
    };

    int32_t i;
    for (i = 0; i < BENCH_JOB_COUNT; i ++) {
        box_job_submit(world, &desc);
    }
}

/** Run frames until every submitted job completed.
 *
 * @param world The world
 */
static void bench_jobs_drain(
    ecs_world_t *world)
{
    while (box_jobs_pending(world)) {
        ecs_progress(world, 0);
    }
}

/** Submit a batch of jobs and run frames until all of them completed.
 *
 * @param b The benchmark
 * @param workers Worker threads, 0 runs jobs inline at the end of the frame
 */
static void bench_jobs_round_trip(
    bench_t *b,
    int32_t workers)
{
    ecs_world_t *world = bench_jobs_world(workers);

    bench_set_items(b, BENCH_JOB_COUNT);

    while (bench_iter(b)) {
        bench_jobs_submit(world);
        bench_jobs_drain(world);
        bench_keep(bench_jobs_completed);
    }

    ecs_fini(world);
}

void Jobs_submit_complete_workers(
    bench_t *b)
{
    // testing/bench
    bench_jobs_round_trip(b, BENCH_JOB_WORKERS);
}

void Jobs_submit_complete_inline(
    bench_t *b)
{
    // testing/bench
    bench_jobs_round_trip(b, 0);
}

void Jobs_submit_only(
    bench_t *b)
{
    // testing/bench
    ecs_world_t *world = bench_jobs_world(BENCH_JOB_WORKERS);

    bench_set_items(b, BENCH_JOB_COUNT);

    while (bench_iter(b)) {
        bench_jobs_submit(world);
        bench_pause(b);
        bench_jobs_drain(world);
        bench_resume(b);
        bench_keep(bench_jobs_worked);
    }

    ecs_fini(world);
}
