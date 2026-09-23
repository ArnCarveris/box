/**
 * @file job/job.c
 * @brief Registers the box.job module and drains completions in PreUpdate.
 *
 * The drain runs as an immediate system, because completions publish results
 * with ecs_modified and the observers of those events may change the world.
 */

#define BOX_JOB_IMPL
#include "pool.h"

ECS_COMPONENT_DECLARE(JobPool);

/** Free the worker pool of a world.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_job_pool_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    JobPool *jp = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        if (jp[i].pool) {
            box_job_pool_free(jp[i].pool);
            jp[i].pool = NULL;
        }
    }
}

/** Get the pool of a world.
 *
 * @param world The world
 * @return The pool
 */
static box_job_pool_t* box_job_get_pool(
    ecs_world_t *world)
{
    const JobPool *jp = ecs_get(world, ecs_id(JobPool), JobPool);
    ecs_assert(jp != NULL, ECS_INVALID_OPERATION, "box.job is not imported");

    return jp->pool;
}

/** Apply the settings, run completions within the budget and report the counters.
 *
 * @param it The iterator of the system
 */
static void BoxJobUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    JobSettings *settings = ecs_get_mut(world, ecs_id(JobSettings), JobSettings);
    JobStats *stats = ecs_get_mut(world, ecs_id(JobStats), JobStats);
    box_job_pool_t *pool = box_job_get_pool(world);
    box_job_pool_configure(pool, settings);
    box_job_pool_drain(pool, world, settings->budget_ms);
    box_job_pool_stats(pool, stats);
}

/** JobSettings observer: apply a change to the settings as soon as it is set.
 *
 * @param it Iterator over the settings that were set
 */
static void box_job_on_settings(
    ecs_iter_t *it)
{
    const JobSettings *settings = ecs_field(it, JobSettings, 0);
    box_job_pool_configure(box_job_get_pool(it->world), settings);
}

box_job_t box_job_submit(
    ecs_world_t *world,
    const box_job_desc_t *desc)
{
    return box_job_pool_submit(box_job_get_pool(world), desc);
}

bool box_job_cancel(
    ecs_world_t *world,
    box_job_t job)
{
    return box_job_pool_cancel(box_job_get_pool(world), job);
}

box_job_t box_job_supersede(
    ecs_world_t *world,
    uint64_t group,
    const box_job_desc_t *desc)
{
    box_job_t result = 0;
    box_job_pool_t *pool = box_job_get_pool(world);
    box_job_pool_cancel_group(pool, group);
    if (desc) {
        result = box_job_pool_submit(pool, desc);
    }

    return result;
}

void box_job_flush(
    ecs_world_t *world)
{
    box_job_pool_t *pool = box_job_get_pool(world);
    box_job_pool_flush(pool, world);
    JobStats *stats = ecs_get_mut(world, ecs_id(JobStats), JobStats);
    box_job_pool_stats(pool, stats);
}

void box_job_set_eye(
    ecs_world_t *world,
    vec3 eye)
{
    JobSettings *settings = ecs_get_mut(world, ecs_id(JobSettings), JobSettings);
    settings->eye = eye;
    box_job_pool_t *pool = box_job_get_pool(world);
    box_job_pool_configure(pool, settings);
}

void BoxJobImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxJob);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, JobSettings);
    ECS_META_COMPONENT(world, JobStats);
    ECS_COMPONENT_DEFINE(world, JobPool);

    ecs_set_hooks(world, JobPool, {
        .ctor = flecs_default_ctor,
        .dtor = box_job_pool_dtor
    });
    ecs_add_pair(world, ecs_id(JobPool), EcsOnInstantiate, EcsDontInherit);

    ecs_singleton_set(world, JobSettings, {
        .workers = 0,
        .budget_ms = 2.0,
        .prioritize_near = true
    });
    ecs_singleton_ensure(world, JobStats);
    ecs_singleton_modified(world, JobStats);

    JobPool *jp = ecs_singleton_ensure(world, JobPool);
    ecs_world_t *raw = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(world));
    jp->pool = box_job_pool_new(raw, box_job_pool_worker_count(0));
    ecs_singleton_modified(world, JobPool);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxJobUpdate" }),
        .phase = EcsPreUpdate,
        .callback = BoxJobUpdate,
        .immediate = true
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(JobSettings), .inout = EcsIn }},
        .events = { EcsOnSet },
        .callback = box_job_on_settings
    });
}
