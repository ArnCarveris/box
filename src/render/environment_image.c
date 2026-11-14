/**
 * @file render/environment_image.c
 * @brief The EnvironmentImage source: equirectangular images loaded on the job system and sent
 * into the cubemap of their entity once per version, following the environment source contract.
 * The file watch loads an image again when its file changes.
 */

#include "environment.h"
#include "../texture/texture.h"
#include "../jobs/jobs.h"
#include <math.h>
#include <string.h>

/** Largest finite half float */
#define BOX_ENV_IMAGE_HALF_MAX (65504.0f)

/** A loaded image of an EnvironmentImage */
typedef struct box_env_image_t {
    ecs_entity_t entity;    /**< Entity with the EnvironmentImage */
    uint32_t generation;    /**< Bumped by every load request, older results are dropped */
    uint16_t *texels;       /**< rgba16float texels of the last loaded image, kept so a new
                             *   cubemap gets it again, NULL before the first load */
    int32_t width;          /**< Width of the image */
    int32_t height;         /**< Height of the image */
    bool fresh;             /**< The image was loaded since it was last sent */
} box_env_image_t;

/** Main thread state of the environment images of a world */
typedef struct box_env_images_t {
    ecs_map_t images;       /**< Entity to box_env_image_t* */
    int32_t watch;          /**< Kind of the images in the file watch */
    int32_t loads;          /**< Loads submitted whose completion did not run */
    box_task_track_t task;  /**< Report of the loads */
} box_env_images_t;

/** A decode that runs on a worker: what to load and what came out */
typedef struct box_env_image_job_t {
    ecs_entity_t entity;    /**< Entity with the EnvironmentImage */
    uint32_t generation;    /**< Load request the result belongs to */
    char *path;             /**< Image file */
    char *file;             /**< The file the decode found, NULL when it was not found */
    box_file_stamp_t stamp; /**< Stamp of file, taken before it was read */
    uint16_t *texels;       /**< rgba16float texels of the result, NULL on failure */
    int32_t width;          /**< Width of the result */
    int32_t height;         /**< Height of the result */
    char *error;            /**< Why the decode failed, NULL on success */
} box_env_image_job_t;

/** Get the state of the environment images of a world.
 *
 * @param world The world
 * @return The state, NULL without the extract
 */
static box_env_images_t* box_env_images_state(
    const ecs_world_t *world)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.extract.EnvironmentImage");
    const ecs_system_t *s = system ? ecs_system_get(world, system) : NULL;

    return s ? s->ctx : NULL;
}

/** Convert a float to the bits of the nearest half float, finite values clamped to the
 * largest half and NaN to zero.
 *
 * @param f The value
 * @return The half float
 */
static uint16_t box_env_image_half(
    float f)
{
    uint32_t bits, sign, exponent, mantissa;
    uint16_t result;
    f = f != f ? 0.0f : f;
    f = f > BOX_ENV_IMAGE_HALF_MAX ? BOX_ENV_IMAGE_HALF_MAX : f;
    f = f < -BOX_ENV_IMAGE_HALF_MAX ? -BOX_ENV_IMAGE_HALF_MAX : f;
    memcpy(&bits, &f, sizeof(bits));
    sign = (bits >> 16) & 0x8000u;
    exponent = (bits >> 23) & 0xffu;
    mantissa = bits & 0x7fffffu;
    if (exponent < 103) {
        result = (uint16_t)sign;
    } else if (exponent < 113) {
        mantissa |= 0x800000u;
        uint32_t shift = 126 - exponent;
        uint32_t half = mantissa >> shift;
        uint32_t rest = mantissa & ((1u << shift) - 1);
        uint32_t mid = 1u << (shift - 1);
        half += rest > mid || (rest == mid && (half & 1));
        result = (uint16_t)(sign | half);
    } else {
        uint32_t half = ((exponent - 112) << 10) | (mantissa >> 13);
        uint32_t rest = mantissa & 0x1fffu;
        half += rest > 0x1000u || (rest == 0x1000u && (half & 1));
        result = (uint16_t)(sign | half);
    }

    return result;
}

/** Decode the image of a job into rgba16float texels on a worker thread.
 *
 * @param ctx The job
 */
static void box_env_image_work(
    void *ctx)
{
    box_env_image_job_t *job = ctx;
    float *texels = box_texture_decode_float(job->path, BOX_RENDER_ENVIRONMENT_IMAGE_MAX,
        &job->width, &job->height, &job->error, &job->file, &job->stamp);
    size_t i, count;
    if (!texels) {
        goto done;
    }

    count = (size_t)job->width * job->height * 4;
    job->texels = ecs_os_malloc((ecs_size_t)(count * sizeof(uint16_t)));
    for (i = 0; i < count; i ++) {
        job->texels[i] = box_env_image_half((i & 3) == 3 ? 1.0f : texels[i]);
    }

    ecs_os_free(texels);
done:
    return;
}

/** Hand the result of a decode to its image, unless a newer load was requested since.
 *
 * @param world The world
 * @param ctx The job
 */
static void box_env_image_complete(
    ecs_world_t *world,
    void *ctx)
{
    box_env_image_job_t *job = ctx;
    box_env_images_t *state = box_env_images_state(world);
    box_env_image_t *img = state
        ? ecs_map_get_deref(&state->images, box_env_image_t, job->entity)
        : NULL;
    if (state) {
        state->loads --;
    }

    if (!img || img->generation != job->generation || !ecs_is_alive(world, job->entity)) {
        goto done;
    }

    if (job->file) {
        box_file_watch_clear(world, state->watch, job->entity);
        box_file_watch_add(world, state->watch, job->entity, job->file, &job->stamp);
    }

    if (job->error) {
        ecs_err("%s", job->error);
        box_render_environment_error(world, job->entity, job->error);
        goto done;
    }

    ecs_os_free(img->texels);
    img->texels = job->texels;
    img->width = job->width;
    img->height = job->height;
    img->fresh = true;
    job->texels = NULL;
    box_render_environment_error(world, job->entity, NULL);
done:
    return;
}

/** Free a job after its completion, or at shutdown instead of it.
 *
 * @param ctx The job
 */
static void box_env_image_job_free(
    void *ctx)
{
    box_env_image_job_t *job = ctx;
    ecs_os_free(job->path);
    ecs_os_free(job->file);
    ecs_os_free(job->texels);
    ecs_os_free(job->error);
    ecs_os_free(job);
}

/** Submit a load of the image of an EnvironmentImage, which drops older loads in flight.
 *
 * @param world The world or a stage
 * @param state The state of the images
 * @param e Entity with the EnvironmentImage
 * @param value Its EnvironmentImage
 */
static void box_env_image_request(
    ecs_world_t *world,
    box_env_images_t *state,
    ecs_entity_t e,
    const EnvironmentImage *value)
{
    box_env_image_t *img = ecs_map_get_deref(&state->images, box_env_image_t, e);
    if (!img) {
        img = ecs_os_calloc_t(box_env_image_t);
        img->entity = e;
        ecs_map_insert_ptr(&state->images, e, img);
    }

    box_env_image_job_t *job = ecs_os_calloc_t(box_env_image_job_t);
    job->entity = e;
    job->generation = ++ img->generation;
    job->path = value->path ? ecs_os_strdup(value->path) : NULL;
    state->loads ++;
    if (box_job_submit(world, &(box_job_desc_t){
        .work = box_env_image_work,
        .complete = box_env_image_complete,
        .ctx_free = box_env_image_job_free,
        .ctx = job
    })) {
        state->loads --;
        box_render_environment_error(world, e, "cannot submit the load job");
        box_env_image_job_free(job);
    }
}

/** Load the image of an EnvironmentImage again whose file changed, called by the file watch.
 *
 * @param world The world
 * @param ctx The state of the images
 * @param owner Entity with the EnvironmentImage
 */
static void box_env_image_changed(
    ecs_world_t *world,
    void *ctx,
    ecs_entity_t owner)
{
    const EnvironmentImage *value = ecs_is_alive(world, owner)
        ? ecs_get(world, owner, EnvironmentImage)
        : NULL;
    if (value) {
        box_env_image_request(world, ctx, owner, value);
    }
}

/** Load the image of every set EnvironmentImage, which stops watching the file of the
 * previous one.
 *
 * @param it The iterator of the observer
 */
static void BoxEnvironmentImageOnSet(
    ecs_iter_t *it)
{
    box_env_images_t *state = box_env_images_state(it->world);
    const EnvironmentImage *values = ecs_field(it, EnvironmentImage, 0);
    int32_t i;
    for (i = 0; state && i < it->count; i ++) {
        box_file_watch_clear(it->world, state->watch, it->entities[i]);
        box_env_image_request(it->world, state, it->entities[i], &values[i]);
    }
}

/** Forget the images of the entities that lose their EnvironmentImage.
 *
 * @param it The iterator of the observer
 */
static void BoxEnvironmentImageOnRemove(
    ecs_iter_t *it)
{
    box_env_images_t *state = box_env_images_state(it->world);
    int32_t i;
    for (i = 0; state && !ecs_is_fini(it->world) && i < it->count; i ++) {
        box_env_image_t *img = ecs_map_get_deref(
            &state->images, box_env_image_t, it->entities[i]);
        box_file_watch_clear(it->world, state->watch, it->entities[i]);
        if (img) {
            ecs_map_remove(&state->images, it->entities[i]);
            ecs_os_free(img->texels);
            ecs_os_free(img);
        }
    }
}

/** Send an image into the cubemap of its entity.
 *
 * @param world The world
 * @param img The image
 * @param handle Handle of the cubemap
 */
static void box_env_image_send(
    ecs_world_t *world,
    box_env_image_t *img,
    uint32_t handle)
{
    size_t bytes = (size_t)img->width * img->height * 4 * sizeof(uint16_t);
    int32_t version = box_render_environment_sent(world, img->entity);
    box_render_environment_image_t *desc = box_render_packet_resource(
        box_render_packet(world), BOX_RENDER_ENVIRONMENT_IMAGE, BOX_RENDER_UPDATE, handle,
        (uint32_t)(sizeof(box_render_environment_image_t) + bytes));
    desc->width = (uint32_t)img->width;
    desc->height = (uint32_t)img->height;
    desc->version = version;
    desc->pad = 0;
    ecs_os_memcpy(desc + 1, img->texels, (ecs_size_t)bytes);
    img->fresh = false;
}

/** Report the loads in flight as BOX_TASK_ENVIRONMENT and send every loaded image whose
 * cubemap lacks it: a new image, or a cubemap that was created again empty for a new
 * resolution or a new backend.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractEnvironmentImage(
    ecs_iter_t *it)
{
    box_env_images_t *state = it->ctx;
    box_task_remaining(it->world, &state->task, BOX_TASK_ENVIRONMENT, true, state->loads);
    ecs_map_iter_t mit = ecs_map_iter(&state->images);
    while (ecs_map_next(&mit)) {
        box_env_image_t *img = ecs_map_ptr(&mit);
        if (!img->texels) {
            continue;
        }

        uint32_t handle = box_render_environment_handle(it->world, img->entity);
        if (handle && (img->fresh || !box_render_environment_content(it->world, img->entity)))
        {
            box_env_image_send(it->world, img, handle);
        }
    }
}

/** Free the state of the environment images.
 *
 * @param ctx The state
 */
static void box_env_images_free(
    void *ctx)
{
    box_env_images_t *state = ctx;
    ecs_map_iter_t it = ecs_map_iter(&state->images);
    while (ecs_map_next(&it)) {
        box_env_image_t *img = ecs_map_ptr(&it);
        ecs_os_free(img->texels);
        ecs_os_free(img);
    }

    ecs_map_fini(&state->images);
    ecs_os_free(state);
}

void box_render_environment_image_init(
    ecs_world_t *world)
{
    box_env_images_t *state = ecs_os_calloc_t(box_env_images_t);
    ecs_map_init(&state->images, NULL);
    state->watch = box_file_watch_kind(world, "environment image", box_env_image_changed,
        state);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.EnvironmentImage" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractEnvironmentImage,
        .ctx = state,
        .ctx_free = box_env_images_free
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "EnvironmentImageOnSet" }),
        .query.terms = {{ .id = ecs_id(EnvironmentImage) }},
        .events = { EcsOnSet },
        .callback = BoxEnvironmentImageOnSet
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "EnvironmentImageOnRemove" }),
        .query.terms = {{ .id = ecs_id(EnvironmentImage) }},
        .events = { EcsOnRemove },
        .callback = BoxEnvironmentImageOnRemove
    });
}
