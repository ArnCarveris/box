/**
 * @file Priority.c
 * @brief Tests for the order the priority queue hands jobs to the workers.
 */

#include <bake_test.h>
#include "../../../src/job/job_internal.h"

static int32_t priority_order[8];
static int32_t priority_count;
static volatile int32_t priority_gate;

static void priority_run(
    void *ctx)
{
    priority_order[priority_count] = (int32_t)(intptr_t)ctx;
    priority_count ++;
}

static void priority_gate_run(
    void *ctx)
{
    while (!priority_gate) {
        ecs_os_sleep(0, 1000000);
    }

    priority_run(ctx);
}

void Priority_higher_priority_runs_first(void)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    priority_count = 0;

    JobSettings *settings = ecs_singleton_ensure(world, JobSettings);
    settings->workers = -1;
    settings->budget_ms = 0;
    ecs_singleton_modified(world, JobSettings);
    ecs_progress(world, 0);

    box_job_desc_t desc = {0};
    desc.run = priority_run;
    desc.priority = 1;
    desc.ctx = (void*)(intptr_t)1;
    box_job_submit(world, &desc);
    desc.priority = 5;
    desc.ctx = (void*)(intptr_t)2;
    box_job_submit(world, &desc);
    desc.priority = 3;
    desc.ctx = (void*)(intptr_t)3;
    box_job_submit(world, &desc);

    ecs_progress(world, 0);
    test_int(priority_count, 3);
    test_int(priority_order[0], 2);
    test_int(priority_order[1], 3);
    test_int(priority_order[2], 1);

    ecs_fini(world);
}

void Priority_near_the_eye_runs_first(void)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    priority_count = 0;

    JobSettings *settings = ecs_singleton_ensure(world, JobSettings);
    settings->workers = -1;
    settings->budget_ms = 0;
    ecs_singleton_modified(world, JobSettings);
    ecs_progress(world, 0);
    box_job_set_eye(world, (vec3){0, 0, 0});

    box_job_desc_t desc = {0};
    desc.run = priority_run;
    desc.has_position = true;
    desc.position = (vec3){10, 0, 0};
    desc.ctx = (void*)(intptr_t)1;
    box_job_submit(world, &desc);
    desc.position = (vec3){1, 0, 0};
    desc.ctx = (void*)(intptr_t)2;
    box_job_submit(world, &desc);
    desc.position = (vec3){5, 0, 0};
    desc.ctx = (void*)(intptr_t)3;
    box_job_submit(world, &desc);

    ecs_progress(world, 0);
    test_int(priority_count, 3);
    test_int(priority_order[0], 2);
    test_int(priority_order[1], 3);
    test_int(priority_order[2], 1);

    ecs_fini(world);
}

void Priority_eye_is_taken_at_submit_time(void)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    priority_count = 0;

    JobSettings *settings = ecs_singleton_ensure(world, JobSettings);
    settings->workers = -1;
    settings->budget_ms = 0;
    ecs_singleton_modified(world, JobSettings);
    ecs_progress(world, 0);
    box_job_set_eye(world, (vec3){0, 0, 0});

    box_job_desc_t desc = {0};
    desc.run = priority_run;
    desc.has_position = true;
    desc.position = (vec3){10, 0, 0};
    desc.ctx = (void*)(intptr_t)1;
    box_job_submit(world, &desc);

    box_job_set_eye(world, (vec3){10, 0, 0});
    desc.position = (vec3){15, 0, 0};
    desc.ctx = (void*)(intptr_t)2;
    box_job_submit(world, &desc);

    ecs_progress(world, 0);
    test_int(priority_count, 2);
    test_int(priority_order[0], 2);
    test_int(priority_order[1], 1);

    ecs_fini(world);
}

void Priority_prioritize_near_off_keeps_submit_order(void)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    priority_count = 0;

    JobSettings *settings = ecs_singleton_ensure(world, JobSettings);
    settings->workers = -1;
    settings->budget_ms = 0;
    settings->prioritize_near = false;
    ecs_singleton_modified(world, JobSettings);
    ecs_progress(world, 0);

    box_job_desc_t desc = {0};
    desc.run = priority_run;
    desc.has_position = true;
    desc.position = (vec3){10, 0, 0};
    desc.ctx = (void*)(intptr_t)1;
    box_job_submit(world, &desc);
    desc.position = (vec3){1, 0, 0};
    desc.ctx = (void*)(intptr_t)2;
    box_job_submit(world, &desc);
    desc.position = (vec3){5, 0, 0};
    desc.ctx = (void*)(intptr_t)3;
    box_job_submit(world, &desc);

    ecs_progress(world, 0);
    test_int(priority_count, 3);
    test_int(priority_order[0], 1);
    test_int(priority_order[1], 2);
    test_int(priority_order[2], 3);

    ecs_fini(world);
}

void Priority_single_worker_runs_in_priority_order(void)
{
    // responsiveness/job_system
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxJob);
    priority_count = 0;
    priority_gate = 0;

    JobSettings *settings = ecs_singleton_ensure(world, JobSettings);
    settings->workers = 1;
    settings->budget_ms = 0;
    ecs_singleton_modified(world, JobSettings);
    ecs_progress(world, 0);

    box_job_desc_t desc = {0};
    desc.run = priority_gate_run;
    desc.priority = 100;
    desc.ctx = (void*)(intptr_t)9;
    box_job_submit(world, &desc);

    desc.run = priority_run;
    desc.priority = 1;
    desc.ctx = (void*)(intptr_t)1;
    box_job_submit(world, &desc);
    desc.priority = 5;
    desc.ctx = (void*)(intptr_t)2;
    box_job_submit(world, &desc);
    desc.priority = 3;
    desc.ctx = (void*)(intptr_t)3;
    box_job_submit(world, &desc);

    priority_gate = 1;
    box_job_flush(world);

    test_int(priority_count, 4);
    test_int(priority_order[0], 9);
    test_int(priority_order[1], 2);
    test_int(priority_order[2], 3);
    test_int(priority_order[3], 1);

    ecs_fini(world);
}
