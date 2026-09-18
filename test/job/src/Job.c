/**
 * @file Job.c
 * @brief Tests for submission, completion, cancellation and shutdown.
 */

#include <bake_test.h>
#include "../../../src/job/job_internal.h"

static int32_t job_runs;
static int32_t job_completed;
static uint64_t job_run_thread;
static uint64_t job_complete_thread;
static int32_t job_ctx_a;
static int32_t job_ctx_b;

static void job_run(
    void *ctx)
{
    (void)ctx;
    ecs_os_ainc(&job_runs);
    job_run_thread = ecs_os_thread_self();
}

static void job_run_slow(
    void *ctx)
{
    (void)ctx;
    ecs_os_sleep(0, 5000000);
    ecs_os_ainc(&job_runs);
}

static void job_run_ctx(
    void *ctx)
{
    int32_t *counter = ctx;
    ecs_os_ainc(counter);
}

static void job_complete(
    ecs_world_t *world,
    void *ctx)
{
    (void)world;
    (void)ctx;
    job_completed ++;
    job_complete_thread = ecs_os_thread_self();
}

static void job_complete_slow(
    ecs_world_t *world,
    void *ctx)
{
    (void)world;
    (void)ctx;
    ecs_os_sleep(0, 5000000);
    job_completed ++;
}

void Job_submit_and_complete(void)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    job_runs = 0;
    job_completed = 0;

    box_job_desc_t desc = {0};
    desc.run = job_run;
    desc.complete = job_complete;
    box_job_t job = box_job_submit(world, &desc);
    test_assert(job != 0);

    box_job_flush(world);
    test_int(job_runs, 1);
    test_int(job_completed, 1);

    const JobStats *stats = ecs_singleton_get(world, JobStats);
    test_int(stats->completed, 1);
    test_int(stats->queued, 0);
    test_int(stats->running, 0);

    ecs_fini(world);
}

void Job_run_happens_on_a_worker(void)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    job_runs = 0;
    job_run_thread = 0;

    JobSettings *settings = ecs_singleton_ensure(world, JobSettings);
    settings->workers = 2;
    ecs_singleton_modified(world, JobSettings);
    ecs_progress(world, 0);

    box_job_desc_t desc = {0};
    desc.run = job_run;
    box_job_submit(world, &desc);
    box_job_flush(world);

    test_int(job_runs, 1);
    test_assert(job_run_thread != 0);
    test_assert(job_run_thread != ecs_os_thread_self());

    ecs_fini(world);
}

void Job_completion_runs_on_the_main_thread(void)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    job_runs = 0;
    job_completed = 0;
    job_complete_thread = 0;

    JobSettings *settings = ecs_singleton_ensure(world, JobSettings);
    settings->workers = 2;
    ecs_singleton_modified(world, JobSettings);
    ecs_progress(world, 0);

    box_job_desc_t desc = {0};
    desc.run = job_run;
    desc.complete = job_complete;
    box_job_submit(world, &desc);
    box_job_flush(world);

    test_int(job_completed, 1);
    test_uint(job_complete_thread, ecs_os_thread_self());

    ecs_fini(world);
}

void Job_completion_runs_in_preupdate(void)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    job_runs = 0;
    job_completed = 0;

    JobSettings *settings = ecs_singleton_ensure(world, JobSettings);
    settings->workers = -1;
    ecs_singleton_modified(world, JobSettings);
    ecs_progress(world, 0);

    box_job_desc_t desc = {0};
    desc.run = job_run;
    desc.complete = job_complete;
    box_job_submit(world, &desc);
    test_int(job_runs, 0);
    test_int(job_completed, 0);

    ecs_progress(world, 0);
    test_int(job_runs, 1);
    test_int(job_completed, 1);

    ecs_fini(world);
}

void Job_main_thread_fallback_runs_jobs_inline(void)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    job_runs = 0;
    job_run_thread = 0;

    JobSettings *settings = ecs_singleton_ensure(world, JobSettings);
    settings->workers = -1;
    ecs_singleton_modified(world, JobSettings);
    ecs_progress(world, 0);

    box_job_desc_t desc = {0};
    desc.run = job_run;
    box_job_submit(world, &desc);
    box_job_flush(world);

    test_int(job_runs, 1);
    test_uint(job_run_thread, ecs_os_thread_self());

    ecs_fini(world);
}

void Job_cancel_a_queued_job(void)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    job_ctx_a = 0;
    job_ctx_b = 0;

    JobSettings *settings = ecs_singleton_ensure(world, JobSettings);
    settings->workers = -1;
    ecs_singleton_modified(world, JobSettings);
    ecs_progress(world, 0);

    box_job_desc_t desc = {0};
    desc.run = job_run_ctx;
    desc.ctx = &job_ctx_a;
    box_job_t job = box_job_submit(world, &desc);
    desc.ctx = &job_ctx_b;
    box_job_submit(world, &desc);

    test_bool(box_job_cancel(world, job), true);
    ecs_progress(world, 0);

    test_int(job_ctx_a, 0);
    test_int(job_ctx_b, 1);

    const JobStats *stats = ecs_singleton_get(world, JobStats);
    test_int(stats->dropped, 1);

    ecs_fini(world);
}

void Job_cancel_an_unknown_job(void)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);

    test_bool(box_job_cancel(world, 42), false);

    const JobStats *stats = ecs_singleton_get(world, JobStats);
    test_int(stats->dropped, 0);

    ecs_fini(world);
}

void Job_supersede_a_group(void)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    job_ctx_a = 0;
    job_ctx_b = 0;

    JobSettings *settings = ecs_singleton_ensure(world, JobSettings);
    settings->workers = -1;
    ecs_singleton_modified(world, JobSettings);
    ecs_progress(world, 0);

    box_job_desc_t desc = {0};
    desc.run = job_run_ctx;
    desc.ctx = &job_ctx_a;
    desc.group = 7;
    box_job_submit(world, &desc);
    box_job_submit(world, &desc);

    desc.ctx = &job_ctx_b;
    test_assert(box_job_supersede(world, 7, &desc) != 0);

    ecs_progress(world, 0);
    test_int(job_ctx_a, 0);
    test_int(job_ctx_b, 1);

    const JobStats *stats = ecs_singleton_get(world, JobStats);
    test_int(stats->dropped, 2);

    ecs_fini(world);
}

void Job_budget_defers_completions(void)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    job_runs = 0;
    job_completed = 0;

    JobSettings *settings = ecs_singleton_ensure(world, JobSettings);
    settings->workers = -1;
    settings->budget_ms = 1.0;
    ecs_singleton_modified(world, JobSettings);
    ecs_progress(world, 0);

    box_job_desc_t desc = {0};
    desc.run = job_run;
    desc.complete = job_complete_slow;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        box_job_submit(world, &desc);
    }

    ecs_progress(world, 0);
    test_int(job_runs, 3);
    test_int(job_completed, 1);

    ecs_progress(world, 0);
    test_int(job_completed, 2);

    ecs_progress(world, 0);
    test_int(job_completed, 3);

    ecs_fini(world);
}

void Job_stats_report_the_queue(void)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    job_runs = 0;

    JobSettings *settings = ecs_singleton_ensure(world, JobSettings);
    settings->workers = -1;
    settings->budget_ms = 1.0;
    ecs_singleton_modified(world, JobSettings);
    ecs_progress(world, 0);

    box_job_desc_t desc = {0};
    desc.run = job_run_slow;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        box_job_submit(world, &desc);
    }

    ecs_progress(world, 0);

    const JobStats *stats = ecs_singleton_get(world, JobStats);
    test_int(job_runs, 1);
    test_int(stats->queued, 2);
    test_int(stats->running, 0);
    test_int(stats->completed, 0);

    ecs_fini(world);
}

void Job_stats_report_completions(void)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    job_runs = 0;
    job_completed = 0;

    box_job_desc_t desc = {0};
    desc.run = job_run;
    desc.complete = job_complete_slow;
    box_job_submit(world, &desc);
    box_job_flush(world);

    const JobStats *stats = ecs_singleton_get(world, JobStats);
    test_int(stats->completed, 1);
    test_int(stats->dropped, 0);
    test_assert(stats->worst_completion_ms > 1.0);

    ecs_fini(world);
}

void Job_flush_runs_every_job(void)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    job_runs = 0;
    job_completed = 0;

    box_job_desc_t desc = {0};
    desc.run = job_run;
    desc.complete = job_complete;
    int32_t i;
    for (i = 0; i < 64; i ++) {
        box_job_submit(world, &desc);
    }

    box_job_flush(world);
    test_int(job_runs, 64);
    test_int(job_completed, 64);

    ecs_fini(world);
}

void Job_shutdown_with_jobs_in_flight(void)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    job_runs = 0;
    job_completed = 0;

    JobSettings *settings = ecs_singleton_ensure(world, JobSettings);
    settings->workers = 4;
    ecs_singleton_modified(world, JobSettings);
    ecs_progress(world, 0);

    box_job_desc_t desc = {0};
    desc.run = job_run_slow;
    desc.complete = job_complete;
    int32_t i;
    for (i = 0; i < 500; i ++) {
        box_job_submit(world, &desc);
    }

    test_int(ecs_fini(world), 0);
    test_int(job_completed, 0);
    test_assert(job_runs <= 500);
}

void Job_shutdown_runs_completions_when_flagged(void)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    job_runs = 0;
    job_completed = 0;

    JobSettings *settings = ecs_singleton_ensure(world, JobSettings);
    settings->workers = -1;
    settings->budget_ms = 1.0;
    settings->complete_on_shutdown = true;
    ecs_singleton_modified(world, JobSettings);

    box_job_desc_t desc = {0};
    desc.run = job_run;
    desc.complete = job_complete_slow;
    box_job_submit(world, &desc);
    box_job_submit(world, &desc);

    ecs_progress(world, 0);
    test_int(job_runs, 2);
    test_int(job_completed, 1);

    test_int(ecs_fini(world), 0);
    test_int(job_completed, 2);
}

void Job_stress_100k_jobs(void)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    job_runs = 0;

    box_job_desc_t desc = {0};
    desc.run = job_run;
    int32_t i;
    for (i = 0; i < 100000; i ++) {
        desc.priority = i % 8;
        box_job_submit(world, &desc);
    }

    box_job_flush(world);
    test_int(job_runs, 100000);

    const JobStats *stats = ecs_singleton_get(world, JobStats);
    test_int(stats->queued, 0);
    test_int(stats->running, 0);
    test_int(stats->dropped, 0);

    ecs_fini(world);
}
