/**
 * @file Jobs.c
 * @brief Tests for the job system: completion order, many jobs, chained jobs and shutdown.
 */

#include <box.h>
#include <bake_test.h>
#include "jobs.h"

/** Context of a test job */
typedef struct test_job_t {
    int32_t index;                      /**< Position in submission order */
    int32_t sleep_ms;                   /**< Milliseconds the work sleeps */
    int32_t value;                      /**< Result the work writes */
    int32_t seen;                       /**< Result the completion read */
    ecs_os_thread_id_t work_thread;     /**< Thread the work ran on */
    ecs_os_thread_id_t complete_thread; /**< Thread the completion ran on */
} test_job_t;

static int32_t work_count = 0;
static int32_t complete_count = 0;
static int32_t free_count = 0;
static int32_t chain_count = 0;
static int32_t chain_length = 0;
static int32_t seen_work = -1;
static int64_t sum = 0;
static int32_t order[64];
static int32_t order_count = 0;
static char events[8];
static int32_t event_count = 0;

static void WorkCount(
    void *ctx)
{
    ecs_os_ainc(&work_count);
}

static void WorkSleep(
    void *ctx)
{
    ecs_sleepf(0.01);
    ecs_os_ainc(&work_count);
}

static void WorkJob(
    void *ctx)
{
    test_job_t *job = ctx;
    if (job->sleep_ms) {
        ecs_sleepf((double)job->sleep_ms / 1000.0);
    }

    job->value = job->index * 2 + 1;
    job->work_thread = ecs_os_thread_self();
    ecs_os_ainc(&work_count);
}

static void CompleteCount(
    ecs_world_t *world,
    void *ctx)
{
    complete_count ++;
}

static void CompleteJob(
    ecs_world_t *world,
    void *ctx)
{
    test_job_t *job = ctx;
    job->seen = job->value;
    job->complete_thread = ecs_os_thread_self();
    if (order_count < 64) {
        order[order_count ++] = job->index;
    }

    sum += job->value;
    complete_count ++;
}

static void CompleteEvent(
    ecs_world_t *world,
    void *ctx)
{
    events[event_count ++] = 'c';
}

static void CompleteChain(
    ecs_world_t *world,
    void *ctx)
{
    chain_count ++;
    if (chain_count < chain_length) {
        box_job_submit(world, &(box_job_desc_t){
            .work = WorkCount, .complete = CompleteChain });
    }
}

static void FreeCount(
    void *ctx)
{
    free_count ++;
}

static void FreeEvent(
    void *ctx)
{
    events[event_count ++] = 'f';
}

static void RecordWork(
    ecs_iter_t *it)
{
    seen_work = work_count;
}

void Jobs_setup(void)
{
    work_count = 0;
    complete_count = 0;
    free_count = 0;
    chain_count = 0;
    chain_length = 0;
    seen_work = -1;
    sum = 0;
    order_count = 0;
    event_count = 0;
    ecs_os_memset(order, 0, sizeof(order));
    ecs_os_memset(events, 0, sizeof(events));
}

void Jobs_complete_after_work(void)
{
    // engine_core/jobs
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_job_t job = { .index = 20 };
    test_int(box_job_submit(world, &(box_job_desc_t){
        .work = WorkJob, .complete = CompleteJob, .ctx = &job }), 0);
    test_int(complete_count, 0);

    int32_t frames = 0;
    while (box_jobs_pending(world) && frames < 5000) {
        ecs_progress(world, 0);
        ecs_sleepf(0.001);
        frames ++;
    }

    test_int(box_jobs_pending(world), 0);
    test_int(work_count, 1);
    test_int(complete_count, 1);
    test_int(job.value, 41);
    test_int(job.seen, 41);

    ecs_fini(world);
}

void Jobs_complete_on_main_thread(void)
{
    // engine_core/jobs
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_job_t job = { .index = 1 };
    box_job_submit(world, &(box_job_desc_t){
        .work = WorkJob, .complete = CompleteJob, .ctx = &job });

    int32_t frames = 0;
    while (box_jobs_pending(world) && frames < 5000) {
        ecs_progress(world, 0);
        ecs_sleepf(0.001);
        frames ++;
    }

    test_int(complete_count, 1);
    test_assert(job.work_thread != ecs_os_thread_self());
    test_assert(job.complete_thread == ecs_os_thread_self());

    ecs_fini(world);
}

void Jobs_completion_order(void)
{
    // engine_core/jobs
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_int(box_jobs_set_workers(world, 8), 0);

    test_job_t jobs[8] = {{0}};
    for (int32_t i = 0; i < 8; i ++) {
        jobs[i].index = i;
        jobs[i].sleep_ms = (8 - i) * 5;
        box_job_submit(world, &(box_job_desc_t){
            .work = WorkJob, .complete = CompleteJob, .ctx = &jobs[i] });
    }

    int32_t frames = 0;
    while (box_jobs_pending(world) && frames < 5000) {
        ecs_progress(world, 0);
        ecs_sleepf(0.001);
        frames ++;
    }

    test_int(order_count, 8);
    for (int32_t i = 0; i < 8; i ++) {
        test_int(order[i], i);
        test_int(jobs[i].seen, i * 2 + 1);
    }

    ecs_fini(world);
}

void Jobs_completion_order_inline(void)
{
    // engine_core/jobs
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_int(box_jobs_set_workers(world, 0), 0);

    test_job_t jobs[8] = {{0}};
    for (int32_t i = 0; i < 8; i ++) {
        jobs[i].index = i;
        box_job_submit(world, &(box_job_desc_t){
            .work = WorkJob, .complete = CompleteJob, .ctx = &jobs[i] });
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);

    test_int(order_count, 8);
    for (int32_t i = 0; i < 8; i ++) {
        test_int(order[i], i);
        test_assert(jobs[i].work_thread == ecs_os_thread_self());
    }

    ecs_fini(world);
}

void Jobs_many_jobs(void)
{
    // engine_core/jobs
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    int32_t count = 10000;
    test_job_t *jobs = ecs_os_calloc_n(test_job_t, count);
    int64_t expect = 0;
    for (int32_t i = 0; i < count; i ++) {
        jobs[i].index = i;
        expect += i * 2 + 1;
        test_int(box_job_submit(world, &(box_job_desc_t){
            .work = WorkJob, .complete = CompleteJob, .ctx = &jobs[i] }), 0);
    }

    test_assert(box_jobs_pending(world) > 0);

    int32_t frames = 0;
    while (box_jobs_pending(world) && frames < 5000) {
        ecs_progress(world, 0);
        ecs_sleepf(0.001);
        frames ++;
    }

    test_int(box_jobs_pending(world), 0);
    test_int(work_count, count);
    test_int(complete_count, count);
    test_assert(sum == expect);

    ecs_fini(world);
    ecs_os_free(jobs);
}

void Jobs_many_jobs_inline(void)
{
    // engine_core/jobs
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_jobs_set_workers(world, 0);

    for (int32_t i = 0; i < 1000; i ++) {
        box_job_submit(world, &(box_job_desc_t){
            .work = WorkCount, .complete = CompleteCount, .ctx_free = FreeCount });
    }

    ecs_progress(world, 0);
    test_int(work_count, 1000);
    test_int(complete_count, 0);

    ecs_progress(world, 0);
    test_int(complete_count, 1000);
    test_int(free_count, 1000);
    test_int(box_jobs_pending(world), 0);

    ecs_fini(world);
}

void Jobs_submit_from_completion(void)
{
    // engine_core/jobs
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    chain_length = 100;
    box_job_submit(world, &(box_job_desc_t){ .work = WorkCount, .complete = CompleteChain });

    int32_t frames = 0;
    while (box_jobs_pending(world) && frames < 5000) {
        ecs_progress(world, 0);
        ecs_sleepf(0.001);
        frames ++;
    }

    test_int(chain_count, 100);
    test_int(work_count, 100);
    test_int(box_jobs_pending(world), 0);

    ecs_fini(world);
}

void Jobs_submit_from_completion_inline(void)
{
    // engine_core/jobs
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_jobs_set_workers(world, 0);

    chain_length = 10;
    box_job_submit(world, &(box_job_desc_t){ .work = WorkCount, .complete = CompleteChain });

    for (int32_t i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
    }

    test_int(chain_count, 9);
    test_int(work_count, 10);

    ecs_progress(world, 0);
    test_int(chain_count, 10);
    test_int(box_jobs_pending(world), 0);

    ecs_fini(world);
}

void Jobs_no_complete(void)
{
    // engine_core/jobs
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_job_submit(world, &(box_job_desc_t){ .work = WorkCount, .ctx_free = FreeCount });

    int32_t frames = 0;
    while (box_jobs_pending(world) && frames < 5000) {
        ecs_progress(world, 0);
        ecs_sleepf(0.001);
        frames ++;
    }

    test_int(work_count, 1);
    test_int(free_count, 1);

    ecs_fini(world);
    test_int(free_count, 1);
}

void Jobs_ctx_free_after_complete(void)
{
    // engine_core/jobs
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_job_submit(world, &(box_job_desc_t){
        .work = WorkCount, .complete = CompleteEvent, .ctx_free = FreeEvent });

    int32_t frames = 0;
    while (box_jobs_pending(world) && frames < 5000) {
        ecs_progress(world, 0);
        ecs_sleepf(0.001);
        frames ++;
    }

    test_int(event_count, 2);
    test_str(events, "cf");

    ecs_fini(world);
    test_int(event_count, 2);
}

void Jobs_submit_without_work(void)
{
    // engine_core/jobs
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_log_set_level(-4);
    test_assert(box_job_submit(world, &(box_job_desc_t){ .complete = CompleteCount }) != 0);
    test_assert(box_job_submit(world, NULL) != 0);
    test_int(box_jobs_pending(world), 0);

    ecs_progress(world, 0);
    test_int(complete_count, 0);

    ecs_fini(world);
}

void Jobs_pending_count(void)
{
    // engine_core/jobs
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_jobs_set_workers(world, 0);

    test_int(box_jobs_pending(world), 0);
    for (int32_t i = 0; i < 3; i ++) {
        box_job_submit(world, &(box_job_desc_t){ .work = WorkCount });
    }

    test_int(box_jobs_pending(world), 3);

    ecs_progress(world, 0);
    test_int(box_jobs_pending(world), 3);

    ecs_progress(world, 0);
    test_int(box_jobs_pending(world), 0);

    ecs_fini(world);
}

void Jobs_set_workers_after_start(void)
{
    // engine_core/jobs
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_int(box_jobs_set_workers(world, 2), 0);
    test_int(box_jobs_set_workers(world, 3), 0);
    box_job_submit(world, &(box_job_desc_t){ .work = WorkCount });
    test_assert(box_jobs_set_workers(world, 4) != 0);

    ecs_fini(world);
}

void Jobs_inline_runs_at_end_of_frame(void)
{
    // engine_core/jobs
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_jobs_set_workers(world, 0);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RecordWork" }),
        .phase = EcsOnUpdate,
        .run = RecordWork
    });

    box_job_submit(world, &(box_job_desc_t){ .work = WorkCount, .complete = CompleteCount });
    test_int(work_count, 0);

    ecs_progress(world, 0);
    test_int(seen_work, 0);
    test_int(work_count, 1);
    test_int(complete_count, 0);

    ecs_progress(world, 0);
    test_int(complete_count, 1);

    ecs_fini(world);
}

void Jobs_shutdown_with_pending(void)
{
    // engine_core/jobs
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_jobs_set_workers(world, 1);

    for (int32_t i = 0; i < 100; i ++) {
        box_job_submit(world, &(box_job_desc_t){
            .work = WorkSleep, .complete = CompleteCount, .ctx_free = FreeCount });
    }

    ecs_fini(world);

    test_int(complete_count, 0);
    test_int(free_count, 100);
    test_assert(work_count < 100);
}

void Jobs_shutdown_with_finished(void)
{
    // engine_core/jobs
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_job_submit(world, &(box_job_desc_t){
        .work = WorkCount, .complete = CompleteCount, .ctx_free = FreeCount });

    int32_t tries = 0;
    while (*(volatile int32_t*)&work_count == 0 && tries < 5000) {
        ecs_sleepf(0.001);
        tries ++;
    }

    test_int(work_count, 1);

    ecs_fini(world);

    test_int(complete_count, 0);
    test_int(free_count, 1);
}

void Jobs_shutdown_idle_workers(void)
{
    // engine_core/jobs
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_jobs_set_workers(world, 4);

    box_job_submit(world, &(box_job_desc_t){
        .work = WorkCount, .complete = CompleteCount, .ctx_free = FreeCount });

    int32_t frames = 0;
    while (box_jobs_pending(world) && frames < 5000) {
        ecs_progress(world, 0);
        ecs_sleepf(0.001);
        frames ++;
    }

    test_int(complete_count, 1);
    ecs_sleepf(0.01);

    ecs_fini(world);

    test_int(work_count, 1);
    test_int(free_count, 1);
}

void Jobs_worker_count(void)
{
    // engine_core/jobs
    test_int(box_jobs_worker_count(12, 0), 10);
    test_int(box_jobs_worker_count(2, 0), 1);
    test_int(box_jobs_worker_count(1, 0), 1);
    test_int(box_jobs_worker_count(64, 0), BOX_JOBS_MAX_WORKERS);
    test_int(box_jobs_worker_count(12, 50), 1);
    test_int(box_jobs_worker_count(12, 100), 1);
    test_int(box_jobs_worker_count(12, 400), 3);
    test_int(box_jobs_worker_count(4, 1000), 2);
}
