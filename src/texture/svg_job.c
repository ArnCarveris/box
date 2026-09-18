/**
 * @file texture/svg_job.c
 * @brief Runs SVG rasterization on the job system and uploads it on the main thread.
 *
 * Setting an Svg queues one job per band of the image; the bands render on
 * workers and the completion of the last one uploads the assembled mip chain.
 */

#include "svg_internal.h"
#include "../shader/shader_internal.h"

/** Rasterizations of a world that are queued, running or waiting to be freed */
struct box_svg_registry_t {
    ecs_map_t images;  /**< box_svg_image_t* of the current rasterization, by entity */
    ecs_vec_t garbage; /**< box_svg_task_t* of the tasks whose owner is gone */
};

/** Singleton that holds the rasterization registry of a world */
typedef struct SvgJobs {
    box_svg_registry_t *reg; /**< Freed by the teardown action of the world */
} SvgJobs;

static ECS_COMPONENT_DECLARE(SvgJobs);

/** Get the registry of a world.
 *
 * @param world The world
 * @return The registry, or NULL once the singleton is gone
 */
static box_svg_registry_t* box_svg_reg(
    ecs_world_t *world)
{
    const SvgJobs *jobs = ecs_get(world, ecs_id(SvgJobs), SvgJobs);

    return jobs ? jobs->reg : NULL;
}

/** Whether the worker pool of a world is still there.
 *
 * @param world The world
 * @return true while jobs can be cancelled and drained
 */
static bool box_svg_pool_alive(
    ecs_world_t *world)
{
    return ecs_get(world, ecs_id(JobPool), JobPool) != NULL;
}

/** Release one reference to an image and free it when the last one goes.
 *
 * @param img The image
 */
static void box_svg_image_release(
    box_svg_image_t *img)
{
    if (ecs_os_adec(&img->refs) > 0) {
        goto done;
    }

    if (img->tree) {
        resvg_tree_destroy(img->tree);
    }

    if (img->ready && !img->published) {
        ecs_os_free(img->out.pixels);
    }

    ecs_os_free(img->pixels);
    ecs_os_free(img->file);
    ecs_os_free(img->data);
    ecs_vec_fini_t(NULL, &img->tasks, box_svg_task_t*);
    ecs_os_mutex_free(img->lock);
    ecs_os_free(img);
done:
    return;
}

/** Take a task out of the list that holds it.
 *
 * @param list The list of the owner or the garbage list
 * @param task The task
 */
static void box_svg_list_remove(
    ecs_vec_t *list,
    const box_svg_task_t *task)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(list); i ++) {
        if (ecs_vec_get_t(list, box_svg_task_t*, i)[0] != task) {
            continue;
        }

        int32_t last = ecs_vec_count(list) - 1;
        ecs_vec_get_t(list, box_svg_task_t*, i)[0] =
            ecs_vec_get_t(list, box_svg_task_t*, last)[0];
        ecs_vec_remove_last(list);
        break;
    }
}

/** Drop a task and release the image it rendered a band of.
 *
 * @param task The task, gone from the pool
 */
static void box_svg_task_free(
    box_svg_task_t *task)
{
    box_svg_image_t *img = task->image;
    box_svg_list_remove(task->orphaned ? &img->reg->garbage : &img->tasks, task);
    ecs_os_free(task);
    box_svg_image_release(img);
}

/** Render the band of a task on a worker thread.
 *
 * The first band that gets the lock parses the SVG and sizes the image, and the
 * band that renders the last row left defringes it and builds the mip chain, so
 * the main thread only has the upload to do.
 *
 * @param ctx The task
 */
static void box_svg_task_run(
    void *ctx)
{
    box_svg_task_t *task = ctx;
    box_svg_image_t *img = task->image;
    ecs_os_mutex_lock(img->lock);
    if (!img->measured && !task->orphaned) {
        box_svg_image_measure(img);
    }

    bool skip = task->orphaned || img->failed || task->band >= img->bands;
    ecs_os_mutex_unlock(img->lock);
    if (skip) {
        goto done;
    }

    box_svg_image_band(img, task->band);
    ecs_os_mutex_lock(img->lock);
    img->left --;
    bool last = img->left == 0;
    ecs_os_mutex_unlock(img->lock);
    if (last) {
        box_svg_image_finish(img);
    }
done:
    return;
}

/** Set the pending flag of a texture entity.
 *
 * @param world The world
 * @param e The texture entity
 * @param pending A rasterization has not published its image yet
 */
static void box_svg_pending(
    ecs_world_t *world,
    ecs_entity_t e,
    bool pending)
{
    if (!ecs_is_alive(world, e) || !ecs_has(world, e, TextureImpl)) {
        goto done;
    }

    TextureImpl *impl = ecs_get_mut(world, e, TextureImpl);
    impl->pending = pending;
done:
    return;
}

/** Upload the image of a rasterization once every band rendered.
 *
 * @param world The world
 * @param img The image
 */
static void box_svg_publish(
    ecs_world_t *world,
    box_svg_image_t *img)
{
    if (img->failed) {
        box_svg_pending(world, img->entity, false);
        goto done;
    }

    if (!img->ready || img->published) {
        goto done;
    }

    if (!ecs_is_alive(world, img->entity) || !ecs_has(world, img->entity, RenderTexture)) {
        goto done;
    }

    img->published = true;
    box_texture_publish(world, img->entity, &img->out, img->keep);
    box_svg_pending(world, img->entity, false);
done:
    return;
}

/** Hand the image of a finished band to the renderer on the main thread.
 *
 * @param world The world
 * @param ctx The task
 */
static void box_svg_task_complete(
    ecs_world_t *world,
    void *ctx)
{
    box_svg_task_t *task = ctx;
    task->completed = true;
    if (!task->orphaned) {
        box_svg_publish(world, task->image);
    }

    box_svg_task_free(task);
}

/** Free the tasks whose job left the pool without running its completion.
 *
 * A task is gone from the pool once a cancel no longer finds it, which is the
 * one moment at which no worker can reach it any more.
 *
 * @param world The world
 * @param reg The registry
 */
static void box_svg_sweep(
    ecs_world_t *world,
    box_svg_registry_t *reg)
{
    int32_t i = 0;
    while (i < ecs_vec_count(&reg->garbage)) {
        box_svg_task_t *task = ecs_vec_get_t(&reg->garbage, box_svg_task_t*, i)[0];
        if (box_job_cancel(world, task->job)) {
            i ++;
            continue;
        }

        box_svg_task_free(task);
    }
}

/** Give up the tasks of the rasterization of an entity.
 *
 * @param world The world
 * @param reg The registry
 * @param e The texture entity
 * @param cancel Cancel the jobs instead of leaving the queued ones to supersede
 */
static void box_svg_orphan(
    ecs_world_t *world,
    box_svg_registry_t *reg,
    ecs_entity_t e,
    bool cancel)
{
    box_svg_image_t *img = ecs_map_get_ptr(&reg->images, e);
    if (!img) {
        goto done;
    }

    ecs_map_remove(&reg->images, e);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&img->tasks); i ++) {
        box_svg_task_t *task = ecs_vec_get_t(&img->tasks, box_svg_task_t*, i)[0];
        ecs_os_mutex_lock(img->lock);
        task->orphaned = true;
        ecs_os_mutex_unlock(img->lock);
        ecs_vec_append_t(NULL, &reg->garbage, box_svg_task_t*)[0] = task;
        if (cancel) {
            box_job_cancel(world, task->job);
        }
    }

    ecs_vec_set_count_t(NULL, &img->tasks, box_svg_task_t*, 0);
    box_svg_image_release(img);
done:
    return;
}

/** Position a rasterization belongs to, for the distance the queue orders by.
 *
 * The material nearest the eye that binds the texture decides, because that is
 * what shows up first; a texture nothing binds falls back to its own position.
 *
 * @param world The world
 * @param e The texture entity
 * @param out Receives the position
 * @return true when a position was found
 */
static bool box_svg_position(
    ecs_world_t *world,
    ecs_entity_t e,
    vec3 *out)
{
    vec3 eye = {0, 0, 0};
    const JobSettings *settings = ecs_singleton_get(world, JobSettings);
    if (settings) {
        eye = settings->eye;
    }

    bool result = box_texture_refs_nearest(world, e, eye, out);
    if (result) {
        goto done;
    }

    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    if (wt) {
        *out = vec3_new(wt->m.v[12], wt->m.v[13], wt->m.v[14]);
        result = true;
        goto done;
    }

    const Position *p = ecs_get(world, e, Position);
    if (p) {
        *out = vec3_new(p->x, p->y, p->z);
        result = true;
    }

done:
    return result;
}

/** Build the image of a new rasterization.
 *
 * @param world The world
 * @param reg The registry
 * @param e The texture entity
 * @param svg The component
 * @return The image, with one reference for the registry
 */
static box_svg_image_t* box_svg_image_new(
    ecs_world_t *world,
    box_svg_registry_t *reg,
    ecs_entity_t e,
    const Svg *svg)
{
    box_svg_image_t *result = ecs_os_calloc_t(box_svg_image_t);
    result->lock = ecs_os_mutex_new();
    result->reg = reg;
    result->entity = e;
    result->refs = 1;
    result->request[0] = svg->width;
    result->request[1] = svg->height;
    box_texture_settings(world, &result->cap, &result->keep);
    ecs_vec_init_t(NULL, &result->tasks, box_svg_task_t*, 0);
    if (svg->file && svg->file[0]) {
        char path[BOX_SHADER_PATH_MAX];
        box_shader_resolve(svg->file, path, sizeof(path));
        result->file = ecs_os_strdup(path);
    } else {
        result->data = ecs_os_strdup(svg->data);
    }

    return result;
}

/** Queue one band of a rasterization.
 *
 * @param world The world
 * @param img The image
 * @param band The band the task renders
 * @param desc The job description, its context is replaced
 * @param first Supersede the queued jobs of the entity instead of adding one
 */
static void box_svg_task_new(
    ecs_world_t *world,
    box_svg_image_t *img,
    int32_t band,
    box_job_desc_t *desc,
    bool first)
{
    box_svg_task_t *task = ecs_os_calloc_t(box_svg_task_t);
    task->image = img;
    task->band = band;
    ecs_os_ainc(&img->refs);
    ecs_vec_append_t(NULL, &img->tasks, box_svg_task_t*)[0] = task;
    desc->ctx = task;
    if (first) {
        task->job = box_job_supersede(world, desc->group, desc);
    } else {
        task->job = box_job_submit(world, desc);
    }
}

void box_svg_submit(
    ecs_world_t *world,
    ecs_entity_t e,
    const Svg *svg)
{
    box_svg_registry_t *reg = box_svg_reg(world);
    if (!reg) {
        goto done;
    }

    box_svg_sweep(world, reg);
    box_svg_orphan(world, reg, e, false);

    box_svg_image_t *img = box_svg_image_new(world, reg, e, svg);
    ecs_map_insert_ptr(&reg->images, e, img);
    box_svg_pending(world, e, true);

    int32_t bound[2];
    box_svg_bound(svg, bound);
    int32_t bands = box_svg_band_count(bound);
    box_job_desc_t desc = {0};
    desc.run = box_svg_task_run;
    desc.complete = box_svg_task_complete;
    desc.group = (uint64_t)e;
    desc.has_position = box_svg_position(world, e, &desc.position);

    int32_t i;
    for (i = 0; i < bands; i ++) {
        box_svg_task_new(world, img, i, &desc, i == 0);
    }
done:
    return;
}

void box_svg_drop(
    ecs_world_t *world,
    ecs_entity_t e)
{
    box_svg_registry_t *reg = box_svg_reg(world);
    if (!reg || !box_svg_pool_alive(world)) {
        goto done;
    }

    box_svg_orphan(world, reg, e, true);
    box_svg_sweep(world, reg);
done:
    return;
}

/** Reclaim the tasks the job system is done with.
 *
 * @param it The iterator of the system
 */
static void BoxSvgSweep(
    ecs_iter_t *it)
{
    box_svg_registry_t *reg = box_svg_reg(it->world);
    if (reg) {
        box_svg_sweep(it->world, reg);
    }
}

/** Give up every rasterization that is still in the registry.
 *
 * @param world The world
 * @param reg The registry
 * @param cancel The pool is still there and the jobs can be cancelled
 */
static void box_svg_orphan_all(
    ecs_world_t *world,
    box_svg_registry_t *reg,
    bool cancel)
{
    ecs_vec_t entities;
    ecs_vec_init_t(NULL, &entities, ecs_entity_t, 0);
    ecs_map_iter_t mit = ecs_map_iter(&reg->images);
    while (ecs_map_next(&mit)) {
        ecs_vec_append_t(NULL, &entities, ecs_entity_t)[0] = ecs_map_key(&mit);
    }

    int32_t i;
    for (i = 0; i < ecs_vec_count(&entities); i ++) {
        box_svg_orphan(world, reg, ecs_vec_get_t(&entities, ecs_entity_t, i)[0], cancel);
    }

    ecs_vec_fini_t(NULL, &entities, ecs_entity_t);
}

/** Free the registry of a world once its workers were joined.
 *
 * The pool is destroyed with the rest of the world before the teardown actions
 * run, so nothing can be on a worker any more and what is left of the garbage
 * goes without asking the pool about it.
 *
 * @param world The world
 * @param ctx The registry
 */
static void box_svg_jobs_fini(
    ecs_world_t *world,
    void *ctx)
{
    box_svg_registry_t *reg = ctx;
    bool alive = box_svg_pool_alive(world);
    box_svg_orphan_all(world, reg, alive);
    if (alive) {
        box_job_flush(world);
        box_svg_sweep(world, reg);
    }

    while (ecs_vec_count(&reg->garbage)) {
        box_svg_task_free(ecs_vec_get_t(&reg->garbage, box_svg_task_t*, 0)[0]);
    }

    ecs_map_fini(&reg->images);
    ecs_vec_fini_t(NULL, &reg->garbage, box_svg_task_t*);
    ecs_os_free(reg);
}

void box_svg_jobs_init(
    ecs_world_t *world)
{
    ECS_IMPORT(world, BoxJob);
    ECS_COMPONENT_DEFINE(world, SvgJobs);
    ecs_set_hooks(world, SvgJobs, { .ctor = flecs_default_ctor });
    ecs_add_pair(world, ecs_id(SvgJobs), EcsOnInstantiate, EcsDontInherit);

    box_svg_registry_t *reg = ecs_os_calloc_t(box_svg_registry_t);
    ecs_map_init(&reg->images, NULL);
    ecs_vec_init_t(NULL, &reg->garbage, box_svg_task_t*, 0);
    SvgJobs *jobs = ecs_singleton_ensure(world, SvgJobs);
    jobs->reg = reg;
    ecs_singleton_modified(world, SvgJobs);

    ecs_atfini(world, box_svg_jobs_fini, reg);
    ECS_SYSTEM(world, BoxSvgSweep, EcsPreUpdate, 0);
}
