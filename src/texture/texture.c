/**
 * @file texture/texture.c
 * @brief Texture components and the loads of TexturePath and TextureGen on the job system.
 *
 * Setting a TexturePath submits a decode job, a TextureGen a generation job (gen.c); its completion
 * writes Texture and TextureStatus and queues the pixels in the registry, which the render
 * extract sends. The file watch loads the image again when the file the decode read changes.
 */

#define BOX_TEXTURE_IMPL
#include "texture.h"
#include "../jobs/jobs.h"
#include <math.h>

ECS_COMPONENT_DECLARE(TextureRegistry);
ECS_COMPONENT_DECLARE(TexturePixels);

/** Loading textures a timed out headless wait names in its warning */
#define BOX_TEXTURE_WARN_NAMES (4)

/** Drop a reference to a decode counter, freeing it with the last one.
 *
 * @param sync The counter
 */
static void box_texture_sync_release(
    box_texture_sync_t *sync)
{
    if (-- sync->refs) {
        goto done;
    }

    ecs_os_mutex_free(sync->lock);
    ecs_os_free(sync);
done:
    return;
}

/** Get the number of decodes in flight.
 *
 * @param sync The counter
 * @return Jobs whose decode did not finish
 */
static int32_t box_texture_working(
    box_texture_sync_t *sync)
{
    ecs_os_mutex_lock(sync->lock);
    int32_t result = sync->working;
    ecs_os_mutex_unlock(sync->lock);

    return result;
}

/** Free a registry and the images it still holds.
 *
 * @param t The registry
 */
static void box_textures_free(
    box_textures_t *t)
{
    ecs_map_iter_t it = ecs_map_iter(&t->textures);
    while (ecs_map_next(&it)) {
        box_texture_t *tex = ecs_map_ptr(&it);
        ecs_os_free(tex->pixels);
        ecs_os_free(tex->source_name);
        box_texture_graph_clear(tex);
        ecs_os_free(tex);
    }

    box_texture_graphs_fini(t);
    ecs_map_fini(&t->textures);
    ecs_vec_fini_t(NULL, &t->queued, ecs_entity_t);
    ecs_vec_fini_t(NULL, &t->released, uint32_t);
    ecs_vec_fini_t(NULL, &t->free_handles, uint32_t);
    box_texture_sync_release(t->sync);
    ecs_os_free(t);
}

/** Free the registry of a TextureRegistry singleton.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_texture_registry_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    TextureRegistry *r = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        if (r[i].textures) {
            box_textures_free(r[i].textures);
        }
    }
}

/** Move a TextureRegistry singleton.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_texture_registry_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ecs_os_memcpy(dst, src, ti->size * count);
    ecs_os_memset(src, 0, ti->size * count);
}

box_textures_t* box_textures_get(
    const ecs_world_t *world)
{
    const TextureRegistry *r = ecs_id(TextureRegistry)
        ? ecs_singleton_get(world, TextureRegistry)
        : NULL;

    return r ? r->textures : NULL;
}

uint32_t box_textures_handle(
    const box_textures_t *t,
    ecs_entity_t e)
{
    uint32_t result = 0;
    const box_texture_t *tex;
    if (!t || !e) {
        goto done;
    }

    tex = ecs_map_get_deref(&t->textures, box_texture_t, e);
    if (tex && tex->sent) {
        result = tex->handle;
    }

done:
    return result;
}

uint32_t box_textures_revision(
    const box_textures_t *t,
    ecs_entity_t e)
{
    const box_texture_t *tex = t && e
        ? ecs_map_get_deref(&t->textures, box_texture_t, e)
        : NULL;

    return tex ? tex->revision : 0;
}

int32_t box_textures_quality(
    const ecs_world_t *world,
    const box_textures_t *t)
{
    int32_t result = 0, i;
    ecs_iter_t it = ecs_query_iter(world, t->surfaces);
    while (ecs_query_next(&it)) {
        const Surface *s = ecs_field(&it, Surface, 0);
        for (i = 0; i < it.count; i ++) {
            int32_t q = s[i].texture_quality ? (int32_t)s[i].texture_quality
                : BOX_TEXTURE_QUALITY_DEFAULT;
            result = q > result ? q : result;
        }
    }

    return result ? result : BOX_TEXTURE_QUALITY_DEFAULT;
}

/** Write the status of a texture. A change of its state or revision builds the graphs that
 * sample it again.
 *
 * @param world The world or a stage
 * @param e The texture entity
 * @param state Progress of the load
 * @param revision Loads that succeeded
 * @param error Why the load failed, NULL unless it failed
 */
void box_texture_status(
    ecs_world_t *world,
    ecs_entity_t e,
    TextureState state,
    int32_t revision,
    const char *error)
{
    const TextureStatus *prev = ecs_get(world, e, TextureStatus);
    box_textures_t *t = box_textures_get(world);
    if (t && (!prev || prev->state != state || prev->revision != revision)) {
        box_texture_graphs_touch(t, e);
    }

    ecs_set(world, e, TextureStatus, {
        .state = state,
        .revision = revision,
        .error = ECS_CONST_CAST(char*, error)
    });
}

/** Get the loads of a texture that succeeded.
 *
 * @param world The world or a stage
 * @param e The texture entity
 * @return The revision of its status, 0 without one
 */
int32_t box_texture_revision(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const TextureStatus *status = ecs_get(world, e, TextureStatus);

    return status ? status->revision : 0;
}

/** Decode or generate the image of a job on a worker thread.
 *
 * @param ctx The job
 */
static void box_texture_work(
    void *ctx)
{
    box_texture_job_t *job = ctx;
    if (job->gen) {
        box_texture_generate(job);
    } else {
        box_texture_decode(job);
    }

    ecs_os_mutex_lock(job->sync->lock);
    job->sync->working --;
    ecs_os_mutex_unlock(job->sync->lock);
}

/** Count a job among the loads or gens in flight of the registry.
 *
 * @param t The registry
 * @param job The job
 * @param delta 1 when it is submitted, -1 when it completed
 */
static void box_textures_count(
    box_textures_t *t,
    const box_texture_job_t *job,
    int32_t delta)
{
    if (job->generated) {
        t->gens += delta;
    } else {
        t->loads += delta;
    }
}

/** Hand the result of a decode to its texture, unless a newer load was requested since.
 *
 * @param world The world
 * @param ctx The job
 */
static void box_texture_complete(
    ecs_world_t *world,
    void *ctx)
{
    box_texture_job_t *job = ctx;
    box_textures_t *t = box_textures_get(world);
    box_texture_t *tex = t
        ? ecs_map_get_deref(&t->textures, box_texture_t, job->entity)
        : NULL;
    if (t) {
        box_textures_count(t, job, -1);
    }

    if (!tex || tex->generation != job->generation || !ecs_is_alive(world, job->entity)) {
        goto done;
    }

    if (job->file) {
        box_file_watch_clear(world, t->watch, job->entity);
        box_file_watch_add(world, t->watch, job->entity, job->file, &job->stamp);
    }

    int32_t revision = box_texture_revision(world, job->entity);
    if (job->error) {
        ecs_err("%s", job->error);
        box_texture_status(world, job->entity, TextureFailed, revision, job->error);
        goto done;
    }

    ecs_os_free(tex->pixels);
    tex->pixels = job->pixels;
    tex->width = job->width;
    tex->height = job->height;
    tex->single_channel = job->single_channel;
    tex->loaded = true;
    job->pixels = NULL;
    if (!tex->queued) {
        tex->queued = true;
        *ecs_vec_append_t(NULL, &t->queued, ecs_entity_t) = job->entity;
    }

    ecs_set(world, job->entity, Texture, { .width = job->width, .height = job->height });
    box_texture_status(world, job->entity, TextureReady, revision + 1, NULL);
done:
    return;
}

/** Free a job after its completion, or at shutdown instead of it.
 *
 * @param ctx The job
 */
static void box_texture_job_free(
    void *ctx)
{
    box_texture_job_t *job = ctx;
    ecs_os_free(job->path);
    box_texture_gen_free(job->gen);
    ecs_os_free(job->file);
    ecs_os_free(job->pixels);
    ecs_os_free(job->error);
    box_texture_sync_release(job->sync);
    ecs_os_free(job);
}

/** Get the registry entry of a texture entity, creating it on first use.
 *
 * @param t The registry
 * @param e The texture entity
 * @return The entry
 */
box_texture_t* box_texture_entry(
    box_textures_t *t,
    ecs_entity_t e)
{
    box_texture_t *tex = ecs_map_get_deref(&t->textures, box_texture_t, e);
    if (!tex) {
        tex = ecs_os_calloc_t(box_texture_t);
        tex->entity = e;
        ecs_map_insert_ptr(&t->textures, e, tex);
    }

    return tex;
}

/** Fail the load of a texture without a job, dropping the result of the load in flight.
 *
 * @param world The world or a stage
 * @param t The registry
 * @param e The texture entity
 * @param error Why it failed
 */
void box_texture_fail(
    ecs_world_t *world,
    box_textures_t *t,
    ecs_entity_t e,
    const char *error)
{
    box_texture_t *tex = box_texture_entry(t, e);
    tex->generation ++;
    tex->waiting = false;
    ecs_os_free(tex->source_name);
    tex->source_name = NULL;
    ecs_err("%s", error);
    box_texture_status(world, e, TextureFailed, box_texture_revision(world, e), error);
}

/** Submit the job of a texture: the decode of a file or a generation.
 *
 * @param world The world or a stage
 * @param t The registry
 * @param e The texture entity
 * @param job The job with what to load, takes ownership
 */
static void box_texture_submit(
    ecs_world_t *world,
    box_textures_t *t,
    ecs_entity_t e,
    box_texture_job_t *job)
{
    box_texture_t *tex = box_texture_entry(t, e);
    int32_t revision = box_texture_revision(world, e);
    job->sync = t->sync;
    job->entity = e;
    job->generation = ++ tex->generation;
    job->generated = job->gen != NULL;
    tex->waiting = false;
    ecs_os_free(tex->source_name);
    tex->source_name = NULL;
    t->sync->refs ++;
    ecs_os_mutex_lock(t->sync->lock);
    t->sync->working ++;
    ecs_os_mutex_unlock(t->sync->lock);
    box_texture_status(world, e, TextureLoading, revision, NULL);
    box_textures_count(t, job, 1);
    if (box_job_submit(world, &(box_job_desc_t){
        .work = box_texture_work,
        .complete = box_texture_complete,
        .ctx_free = box_texture_job_free,
        .ctx = job
    })) {
        box_textures_count(t, job, -1);
        ecs_os_mutex_lock(t->sync->lock);
        t->sync->working --;
        ecs_os_mutex_unlock(t->sync->lock);
        box_texture_status(world, e, TextureFailed, revision, "cannot submit the load job");
        box_texture_job_free(job);
    }
}

/** Get the largest dimension of the loads, computing it on first use.
 *
 * @param world The world or a stage
 * @param t The registry
 * @return The texture quality
 */
static int32_t box_texture_quality_of(
    const ecs_world_t *world,
    box_textures_t *t)
{
    if (!t->quality) {
        t->quality = box_textures_quality(world, t);
    }

    return t->quality;
}

void box_texture_capped_size(
    const ecs_world_t *world,
    box_textures_t *t,
    int32_t width,
    int32_t height,
    int32_t *width_out,
    int32_t *height_out)
{
    double w = width > 0 ? width : 512;
    double h = height > 0 ? height : w;
    double largest = w > h ? w : h;
    int32_t quality = box_texture_quality_of(world, t);
    if (quality > 0 && largest > quality) {
        w *= quality / largest;
        h *= quality / largest;
    }

    *width_out = w < 1 ? 1 : (int32_t)floor(w + 0.5);
    *height_out = h < 1 ? 1 : (int32_t)floor(h + 0.5);
}

/** Submit a decode of the TexturePath of an entity.
 *
 * @param world The world or a stage
 * @param t The registry
 * @param e The texture entity
 * @param path Its TexturePath
 */
static void box_texture_request(
    ecs_world_t *world,
    box_textures_t *t,
    ecs_entity_t e,
    const TexturePath *path)
{
    box_texture_job_t *job = ecs_os_calloc_t(box_texture_job_t);
    job->path = path->path ? ecs_os_strdup(path->path) : NULL;
    job->request_width = path->width;
    job->request_height = path->height;
    job->quality = box_texture_quality_of(world, t);
    job->linear = path->linear;
    job->single_channel = path->single_channel;
    box_texture_submit(world, t, e, job);
}

/** Submit a generation of the TextureGen of an entity, with the layers of its source.
 *
 * A source without a TextureGen, such as one declared later in a script, one whose TextureGen
 * is being removed or one being deleted, makes the texture wait in TextureLoading when wait is
 * set, so a texture deleted with its source does not fail.
 *
 * A source that is deleted or has no layers otherwise fails it.
 *
 * @param world The world or a stage
 * @param t The registry
 * @param e The texture entity
 * @param gen Its TextureGen
 * @param wait Wait for a source without a TextureGen instead of failing
 */
static void box_texture_request_gen(
    ecs_world_t *world,
    box_textures_t *t,
    ecs_entity_t e,
    const TextureGen *gen,
    bool wait)
{
    const TextureGen *field = gen;
    if (gen->source) {
        bool exists = ecs_is_alive(world, gen->source);
        bool removed = gen->source == t->removed;
        field = exists && !removed ? ecs_get(world, gen->source, TextureGen) : NULL;
        if (!field && wait && (exists || removed)) {
            box_texture_t *tex = box_texture_entry(t, e);
            tex->generation ++;
            tex->waiting = true;
            ecs_os_free(tex->source_name);
            tex->source_name = removed && exists ? ecs_get_path(world, gen->source) : NULL;
            box_texture_status(world, e, TextureLoading, box_texture_revision(world, e), NULL);
            goto done;
        }

        if (!field || !ecs_vec_count(&field->layers)) {
            box_texture_t *tex = box_texture_entry(t, e);
            char *name = ecs_get_path(world, e);
            char *source = exists ? ecs_get_path(world, gen->source) : NULL;
            const char *source_name = source ? source : tex->source_name;
            char *error = flecs_asprintf("texture '%s': source '%s' has no TextureGen with "
                "layers", name, source_name ? source_name : "(deleted)");
            box_texture_fail(world, t, e, error);
            ecs_os_free(error);
            ecs_os_free(source);
            ecs_os_free(name);
            goto done;
        }
    }

    int32_t quality = box_texture_quality_of(world, t);
    box_texture_gen_t *desc = ecs_os_calloc_t(box_texture_gen_t);
    box_texture_capped_size(world, t, field->width, field->height, &desc->width,
        &desc->height);
    desc->seed = field->seed;
    desc->layer_count = ecs_vec_count(&field->layers);
    if (desc->layer_count) {
        desc->layers = ecs_os_memdup(ecs_vec_first(&field->layers),
            desc->layer_count * ECS_SIZEOF(TextureGenLayer));
    }

    desc->output = gen->output;
    desc->color_count = ecs_vec_count(&gen->colors);
    if (desc->color_count) {
        desc->colors = ecs_os_memdup(ecs_vec_first(&gen->colors),
            desc->color_count * ECS_SIZEOF(TextureGenKey));
    }

    desc->strength = gen->strength;
    if (gen->output == GenAlbedo && ecs_vec_count(&gen->palette)) {
        int32_t index = gen->palette_layer;
        if (index < 0 || index >= desc->layer_count ||
            desc->layers[index].kind == GenNoise)
        {
            char *name = ecs_get_path(world, e);
            char *error = flecs_asprintf("texture '%s': palette_layer %d %s", name, index,
                index < 0 || index >= desc->layer_count
                    ? "does not exist" : "is a GenNoise layer, which has no tones");
            box_texture_fail(world, t, e, error);
            ecs_os_free(error);
            ecs_os_free(name);
            box_texture_gen_free(desc);
            goto done;
        }

        desc->palette_count = ecs_vec_count(&gen->palette);
        desc->palette = ecs_os_memdup(ecs_vec_first(&gen->palette),
            desc->palette_count * ECS_SIZEOF(rgb));
        desc->palette_layer = index;
    }

    box_texture_job_t *job = ecs_os_calloc_t(box_texture_job_t);
    job->gen = desc;
    job->quality = quality;
    job->single_channel = gen->output == GenHeight;
    box_texture_submit(world, t, e, job);
done:
    return;
}

/** Hand the image of a TexturePixels to its texture at once, dropping a load in flight.
 *
 * @param world The world or a stage
 * @param t The registry
 * @param e The texture entity
 * @param pixels Its TexturePixels
 */
static void box_texture_set_pixels(
    ecs_world_t *world,
    box_textures_t *t,
    ecs_entity_t e,
    const TexturePixels *pixels)
{
    box_texture_t *tex = box_texture_entry(t, e);
    int32_t revision = box_texture_revision(world, e);
    int32_t size = pixels->width * pixels->height * (pixels->single_channel ? 1 : 4);
    tex->generation ++;
    tex->waiting = false;
    ecs_os_free(tex->pixels);
    tex->pixels = NULL;
    if (!pixels->pixels || pixels->width <= 0 || pixels->height <= 0) {
        tex->loaded = false;
        box_texture_status(world, e, TextureLoading, revision, NULL);
        goto done;
    }

    tex->pixels = ecs_os_memdup(pixels->pixels, size);
    tex->width = pixels->width;
    tex->height = pixels->height;
    tex->single_channel = pixels->single_channel;
    tex->loaded = true;
    if (!tex->queued) {
        tex->queued = true;
        *ecs_vec_append_t(NULL, &t->queued, ecs_entity_t) = e;
    }

    ecs_set(world, e, Texture, { .width = pixels->width, .height = pixels->height });
    box_texture_status(world, e, TextureReady, revision + 1, NULL);
done:
    return;
}

void box_texture_load(
    ecs_world_t *world,
    box_textures_t *t,
    ecs_entity_t e)
{
    const TexturePath *path = ecs_get(world, e, TexturePath);
    const TextureGen *gen = ecs_get(world, e, TextureGen);
    const TextureGraph *graph = ecs_get(world, e, TextureGraph);
    const TexturePixels *pixels = path || gen || graph
        ? NULL
        : ecs_get(world, e, TexturePixels);
    if (pixels) {
        box_texture_set_pixels(world, t, e, pixels);
    } else if (graph && (path || gen)) {
        char *name = ecs_get_path(world, e);
        char *error = flecs_asprintf("texture '%s' has both a TextureGraph and a %s", name,
            path ? "TexturePath" : "TextureGen");
        box_texture_fail(world, t, e, error);
        ecs_os_free(error);
        ecs_os_free(name);
    } else if (graph) {
        box_texture_graph_request(world, t, e);
    } else if (path && gen) {
        char *name = ecs_get_path(world, e);
        char *error = flecs_asprintf(
            "texture '%s' has both a TexturePath and a TextureGen", name);
        box_texture_fail(world, t, e, error);
        ecs_os_free(error);
        ecs_os_free(name);
    } else if (path) {
        box_texture_request(world, t, e, path);
    } else if (gen) {
        box_texture_request_gen(world, t, e, gen, true);
    }
}

/** Generate the textures again whose TextureGen derives from a source that changed.
 *
 * @param world The world or a stage
 * @param t The registry
 * @param source The generator that changed
 */
static void box_texture_regenerate_derived(
    ecs_world_t *world,
    box_textures_t *t,
    ecs_entity_t source)
{
    ecs_vec_t derived;
    ecs_vec_init_t(NULL, &derived, ecs_entity_t, 0);
    ecs_map_iter_t it = ecs_map_iter(&t->textures);
    while (ecs_map_next(&it)) {
        box_texture_t *tex = ecs_map_ptr(&it);
        const TextureGen *gen = ecs_is_alive(world, tex->entity)
            ? ecs_get(world, tex->entity, TextureGen)
            : NULL;
        if (gen && gen->source == source && tex->entity != source) {
            *ecs_vec_append_t(NULL, &derived, ecs_entity_t) = tex->entity;
        }
    }

    int32_t i, count = ecs_vec_count(&derived);
    ecs_entity_t *entities = ecs_vec_first_t(&derived, ecs_entity_t);
    for (i = 0; i < count; i ++) {
        box_texture_load(world, t, entities[i]);
    }

    ecs_vec_fini_t(NULL, &derived, ecs_entity_t);
}

void box_textures_reload(
    ecs_world_t *world,
    box_textures_t *t,
    bool sent_only)
{
    ecs_map_iter_t it = ecs_map_iter(&t->textures);
    while (ecs_map_next(&it)) {
        box_texture_t *tex = ecs_map_ptr(&it);
        bool reload = !sent_only || tex->sent;
        tex->sent = false;
        if (reload && ecs_is_alive(world, tex->entity)) {
            box_texture_load(world, t, tex->entity);
        }
    }
}

/** Load the image of a texture again whose file changed, called by the file watch.
 *
 * @param world The world
 * @param ctx Unused
 * @param owner The texture entity
 */
static void box_texture_changed(
    ecs_world_t *world,
    void *ctx,
    ecs_entity_t owner)
{
    box_textures_t *t = box_textures_get(world);
    const TexturePath *path = t && ecs_is_alive(world, owner)
        ? ecs_get(world, owner, TexturePath)
        : NULL;
    (void)ctx;
    if (path) {
        box_texture_load(world, t, owner);
    }
}

/** Load the image of every set TexturePath, which stops watching the file of the previous one.

 *
 * @param it The iterator of the observer
 */
static void BoxTextureOnSet(
    ecs_iter_t *it)
{
    box_textures_t *t = box_textures_get(it->world);
    int32_t i;
    if (!t) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        box_file_watch_clear(it->world, t->watch, it->entities[i]);
        box_texture_load(it->stage, t, it->entities[i]);
    }

done:
    return;
}

/** Generate the image of every set TextureGen and of the generators derived from it.
 *
 * @param it The iterator of the observer
 */
static void BoxTextureGenOnSet(
    ecs_iter_t *it)
{
    box_textures_t *t = box_textures_get(it->world);
    int32_t i;
    if (!t) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        box_texture_load(it->stage, t, it->entities[i]);
        box_texture_regenerate_derived(it->stage, t, it->entities[i]);
    }

done:
    return;
}

/** Forget the registry entry of a texture and release its backend image.
 *
 * @param world The world or a stage
 * @param t The registry
 * @param e The texture entity
 * @param alive The entity stays alive, so it loses its Texture and TextureStatus
 */
static void box_texture_forget(
    ecs_world_t *world,
    box_textures_t *t,
    ecs_entity_t e,
    bool alive)
{
    box_texture_t *tex = ecs_map_get_deref(&t->textures, box_texture_t, e);
    if (tex) {
        if (tex->handle && tex->graph_sent) {
            *ecs_vec_append_t(NULL, &t->graph_released, uint32_t) = tex->handle;
        }

        if (tex->handle) {
            *ecs_vec_append_t(NULL, &t->released, uint32_t) = tex->handle;
        }

        ecs_map_remove(&t->textures, e);
        box_texture_graphs_touch(t, e);
        ecs_os_free(tex->pixels);
        ecs_os_free(tex->source_name);
        box_texture_graph_clear(tex);
        ecs_os_free(tex);
    }

    if (alive) {
        ecs_remove(world, e, Texture);
        ecs_remove(world, e, TextureStatus);
    }
}

/** Forget the textures whose TexturePixels is removed, unless a TexturePath or a TextureGen
 * stays to load them.
 *
 * @param it The iterator of the observer
 */
static void BoxTexturePixelsOnRemove(
    ecs_iter_t *it)
{
    box_textures_t *t = box_textures_get(it->world);
    bool alive = it->other_table && ecs_table_get_type(it->other_table)->count;
    bool keep = alive && (ecs_table_has_id(it->world, it->other_table, ecs_id(TexturePath)) ||
        ecs_table_has_id(it->world, it->other_table, ecs_id(TextureGen)) ||
        ecs_table_has_id(it->world, it->other_table, ecs_id(TextureGraph)));
    int32_t i;
    if (!t || keep || ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        box_texture_forget(it->stage, t, it->entities[i], alive);
    }

done:
    return;
}

/** Free the image of TexturePixels components.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_texture_pixels_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    TexturePixels *p = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        ecs_os_free(p[i].pixels);
    }
}

/** Copy TexturePixels components with their images.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_texture_pixels_copy(
    void *dst,
    const void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    TexturePixels *d = dst;
    const TexturePixels *s = src;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        int32_t size = s[i].width * s[i].height * (s[i].single_channel ? 1 : 4);
        uint8_t *pixels = s[i].pixels && size > 0 ? ecs_os_memdup(s[i].pixels, size) : NULL;
        ecs_os_free(d[i].pixels);
        d[i] = s[i];
        d[i].pixels = pixels;
    }
}

/** Move TexturePixels components, the source keeps no image.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_texture_pixels_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    TexturePixels *d = dst;
    TexturePixels *s = src;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        ecs_os_free(d[i].pixels);
        d[i] = s[i];
        s[i].pixels = NULL;
    }
}

/** Forget the textures whose TexturePath or TextureGen is removed and release their backend
 * image; one that keeps the other or a TextureGraph loads from it instead, keeping its entry.
 *
 * The textures derived from a removed TextureGen, also on deletion of its entity, wait for it
 * and fail at the next frame unless they were deleted with it or got a source back. They are
 * updated after the entries of the batch are dropped, so the ones deleted with it are gone.
 *
 * @param it The iterator of the observer
 */
static void BoxTextureOnRemove(
    ecs_iter_t *it)
{
    box_textures_t *t = box_textures_get(it->world);
    bool alive = it->other_table && ecs_table_get_type(it->other_table)->count;
    bool is_path = it->event_id == ecs_id(TexturePath);
    ecs_id_t other = is_path ? ecs_id(TextureGen) : ecs_id(TexturePath);
    bool keep = alive && ecs_table_has_id(it->world, it->other_table, other);
    bool graph = alive &&
        ecs_table_has_id(it->world, it->other_table, ecs_id(TextureGraph));
    int32_t i;
    if (!t || ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        box_file_watch_clear(it->world, t->watch, e);
        if (graph) {
            *ecs_vec_append_t(NULL, &t->reload, ecs_entity_t) = e;
            continue;
        } else if (keep && is_path) {
            box_texture_request_gen(it->stage, t, e, ecs_get(it->world, e, TextureGen), true);
            continue;
        } else if (keep) {
            box_texture_request(it->stage, t, e, ecs_get(it->world, e, TexturePath));
            continue;
        }

        box_texture_forget(it->stage, t, e, alive);
    }

    for (i = 0; !is_path && i < it->count; i ++) {
        t->removed = it->entities[i];
        box_texture_regenerate_derived(it->stage, t, it->entities[i]);
        t->removed = 0;
    }

done:
    return;
}

/** Forget the textures whose TextureGraph is removed and release their backend image and
 * graph; one that keeps a TexturePath or a TextureGen loads from it once the graph is gone.
 *
 * @param it The iterator of the observer
 */
static void BoxTextureGraphOnRemove(
    ecs_iter_t *it)
{
    box_textures_t *t = box_textures_get(it->world);
    bool alive = it->other_table && ecs_table_get_type(it->other_table)->count;
    bool keep = alive && (ecs_table_has_id(it->world, it->other_table, ecs_id(TexturePath)) ||
        ecs_table_has_id(it->world, it->other_table, ecs_id(TextureGen)));
    int32_t i;
    if (!t || ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        box_texture_t *tex = ecs_map_get_deref(&t->textures, box_texture_t, e);
        if (keep && tex) {
            if (tex->handle && tex->graph_sent) {
                *ecs_vec_append_t(NULL, &t->graph_released, uint32_t) = tex->handle;
            }

            box_texture_graph_clear(tex);
            *ecs_vec_append_t(NULL, &t->reload, ecs_entity_t) = e;
            continue;
        }

        box_texture_forget(it->stage, t, e, alive);
    }

done:
    return;
}

/** Warn that a headless frame stopped waiting for textures.
 *
 * The loads whose result is not applied yet are named while decodes are in flight; without one
 * they only wait for their completion, which runs later in the frame, and are only counted.
 *
 * @param world The world
 * @param t The registry
 * @param waited Seconds the frame waited
 */
static void box_textures_warn_timeout(
    ecs_world_t *world,
    box_textures_t *t,
    double waited)
{
    ecs_strbuf_t names = ECS_STRBUF_INIT;
    int32_t loading = 0, i, decoding = box_texture_working(t->sync);
    ecs_iter_t it = ecs_each_id(world, ecs_id(TextureStatus));
    while (ecs_each_next(&it)) {
        const TextureStatus *status = ecs_field(&it, TextureStatus, 0);
        for (i = 0; i < it.count; i ++) {
            if (status[i].state != TextureLoading) {
                continue;
            }

            if (decoding && loading < BOX_TEXTURE_WARN_NAMES) {
                const TexturePath *path = ecs_get(world, it.entities[i], TexturePath);
                char *name = ecs_get_path(world, it.entities[i]);
                ecs_strbuf_appendstr(&names, loading ? ", " : " (");
                ecs_strbuf_appendstr(&names, name);
                if (path && path->path) {
                    ecs_strbuf_append(&names, " %s", path->path);
                }

                ecs_os_free(name);
            }

            loading ++;
        }
    }

    if (decoding && loading > BOX_TEXTURE_WARN_NAMES) {
        ecs_strbuf_append(&names, ", %d more", loading - BOX_TEXTURE_WARN_NAMES);
    }

    if (decoding && loading) {
        ecs_strbuf_appendstr(&names, ")");
    }

    char *list = ecs_strbuf_get(&names);
    ecs_warn("headless texture wait timed out after %.1f s: %d decode(s) in flight, "
        "%d job(s) running, %d texture load(s) not applied yet%s", waited, decoding,
        box_jobs_running(world), loading, list ? list : "");
    ecs_os_free(list);
}

/** Fail the textures that still wait for their source to get a TextureGen, or whose source
 * lost it or was deleted.
 *
 * @param world The world
 * @param t The registry
 */
static void box_textures_fail_waiting(
    ecs_world_t *world,
    box_textures_t *t)
{
    ecs_vec_t waiting;
    ecs_vec_init_t(NULL, &waiting, ecs_entity_t, 0);
    ecs_map_iter_t it = ecs_map_iter(&t->textures);
    while (ecs_map_next(&it)) {
        box_texture_t *tex = ecs_map_ptr(&it);
        if (tex->waiting) {
            tex->waiting = false;
            *ecs_vec_append_t(NULL, &waiting, ecs_entity_t) = tex->entity;
        }
    }

    int32_t i, count = ecs_vec_count(&waiting);
    ecs_entity_t *entities = ecs_vec_first_t(&waiting, ecs_entity_t);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = entities[i];
        const TextureGen *gen = ecs_is_alive(world, e) ? ecs_get(world, e, TextureGen) : NULL;
        if (gen && !ecs_has(world, e, TexturePath)) {
            box_texture_request_gen(world, t, e, gen, false);
        }
    }

    ecs_vec_fini_t(NULL, &waiting, ecs_entity_t);
}

/** Report the loads and generations in flight and fail the textures whose source got no
 * TextureGen since the last frame.
 *
 * Load the textures again when the texture quality of the surfaces changed and, in a headless
 * run, wait for the decodes in flight so a frame sees the same images, warning on a timeout.
 *
 * @param it The iterator of the system
 */
static void BoxTextureFrame(
    ecs_iter_t *it)
{
    box_textures_t *t = box_textures_get(it->world);
    const App *app = ecs_singleton_get(it->world, App);
    ecs_time_t start = {0};
    if (!t) {
        goto done;
    }

    box_task_remaining(it->world, &t->load_task, BOX_TASK_TEXTURES, true, t->loads);
    box_task_remaining(it->world, &t->gen_task, BOX_TASK_TEXTURE_GEN, true, t->gens);
    box_textures_fail_waiting(it->world, t);
    int32_t quality = box_textures_quality(it->world, t);
    if (t->quality && quality != t->quality) {
        t->quality = quality;
        box_textures_reload(it->world, t, false);
    }

    if (!app || !app->headless || app->async_loads) {
        goto done;
    }

    ecs_time_measure(&start);
    while (box_texture_working(t->sync) || box_jobs_running(it->world)) {
        ecs_time_t now = start;
        double waited = ecs_time_measure(&now);
        if (waited > BOX_TEXTURE_HEADLESS_WAIT) {
            box_textures_warn_timeout(it->world, t, waited);
            break;
        }

        ecs_os_sleep(0, 1000 * 1000);
    }

done:
    return;
}

void BoxTextureImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxTexture);
    ECS_IMPORT(world, BoxTask);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxSurface);
    ECS_IMPORT(world, BoxApp);
    ECS_IMPORT(world, BoxJobs);

    ECS_META_COMPONENT(world, Texture);
    ECS_META_COMPONENT(world, TexturePath);
    ECS_META_COMPONENT(world, TextureGenPattern);
    ECS_META_COMPONENT(world, TextureGenOp);
    ECS_META_COMPONENT(world, TextureGenDistance);
    ECS_META_COMPONENT(world, TextureGenToneOrder);
    ECS_META_COMPONENT(world, TextureGenOutput);
    ECS_META_COMPONENT(world, TextureGenLayer);
    ECS_META_COMPONENT(world, TextureGenKey);
    ECS_META_COMPONENT(world, TextureGen);
    ECS_META_COMPONENT(world, TextureGraphDomain);
    ECS_META_COMPONENT(world, TextureGraphFormat);
    ECS_META_COMPONENT(world, TextureValueType);
    ECS_META_COMPONENT(world, TextureGraph);
    ECS_META_COMPONENT(world, TexConstant);
    ECS_META_COMPONENT(world, TexCoords);
    ECS_META_COMPONENT(world, TextureMathOp);
    ECS_META_COMPONENT(world, TexMath);
    ECS_META_COMPONENT(world, TexTime);
    ECS_META_COMPONENT(world, TexSplat);
    ECS_META_COMPONENT(world, TextureAddress);
    ECS_META_COMPONENT(world, TexImage);
    ECS_META_COMPONENT(world, TexLut);
    ECS_META_COMPONENT(world, TextureGraphCache);
    ECS_META_COMPONENT(world, TextureNodePort);
    ECS_META_COMPONENT(world, TextureNodeParam);
    ECS_META_COMPONENT(world, TextureNodeKind);
    ECS_META_COMPONENT(world, TextureState);
    ECS_META_COMPONENT(world, TextureStatus);
    box_owned_hooks(world, TexturePath, BOX_OWNED_STR(TexturePath, path));
    box_owned_hooks(world, TextureGen, BOX_OWNED_VEC(TextureGen, layers, TextureGenLayer),
        BOX_OWNED_VEC(TextureGen, colors, TextureGenKey),
        BOX_OWNED_VEC(TextureGen, palette, rgb));
    box_owned_hooks(world, TextureStatus, BOX_OWNED_STR(TextureStatus, error));
    box_owned_hooks(world, TextureNodePort, BOX_OWNED_STR(TextureNodePort, name));
    box_owned_hooks(world, TextureNodeParam, BOX_OWNED_STR(TextureNodeParam, name));
    box_owned_hooks(world, TextureNodeKind, BOX_OWNED_STR(TextureNodeKind, function),
        BOX_OWNED_STR(TextureNodeKind, texture),
        BOX_OWNED_STR(TextureNodeKind, inputs[0].name),
        BOX_OWNED_STR(TextureNodeKind, inputs[1].name),
        BOX_OWNED_STR(TextureNodeKind, inputs[2].name),
        BOX_OWNED_STR(TextureNodeKind, inputs[3].name),
        BOX_OWNED_STR(TextureNodeKind, params[0].name),
        BOX_OWNED_STR(TextureNodeKind, params[1].name),
        BOX_OWNED_STR(TextureNodeKind, params[2].name),
        BOX_OWNED_STR(TextureNodeKind, params[3].name),
        BOX_OWNED_STR(TextureNodeKind, params[4].name),
        BOX_OWNED_STR(TextureNodeKind, params[5].name),
        BOX_OWNED_STR(TextureNodeKind, params[6].name),
        BOX_OWNED_STR(TextureNodeKind, params[7].name));

    ECS_COMPONENT_DEFINE(world, TextureRegistry);
    ECS_COMPONENT_DEFINE(world, TexturePixels);
    ecs_set_hooks(world, TexturePixels, {
        .ctor = flecs_default_ctor,
        .dtor = box_texture_pixels_dtor,
        .copy = box_texture_pixels_copy,
        .move = box_texture_pixels_move
    });
    ecs_set_hooks(world, TextureRegistry, {
        .ctor = flecs_default_ctor,
        .dtor = box_texture_registry_dtor,
        .move = box_texture_registry_move
    });

    box_texture_decode_init();
    box_textures_t *t = ecs_os_calloc_t(box_textures_t);
    ecs_map_init(&t->textures, NULL);
    ecs_vec_init_t(NULL, &t->queued, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &t->released, uint32_t, 0);
    ecs_vec_init_t(NULL, &t->free_handles, uint32_t, 0);
    t->next_handle = 1;
    t->sync = ecs_os_calloc_t(box_texture_sync_t);
    t->sync->lock = ecs_os_mutex_new();
    t->sync->refs = 1;
    t->surfaces = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "TextureSurfaces" }),
        .terms = {{ .id = ecs_id(Surface), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });
    t->watch = box_file_watch_kind(world, "texture", box_texture_changed, NULL);
    ecs_singleton_ensure(world, TextureRegistry)->textures = t;
    box_texture_graphs_init(world, t);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TextureOnSet" }),
        .query.terms = {{ .id = ecs_id(TexturePath) }},
        .events = { EcsOnSet },
        .callback = BoxTextureOnSet
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TextureOnRemove" }),
        .query.terms = {{ .id = ecs_id(TexturePath) }},
        .events = { EcsOnRemove },
        .callback = BoxTextureOnRemove
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TextureGenOnSet" }),
        .query.terms = {{ .id = ecs_id(TextureGen) }},
        .events = { EcsOnSet },
        .callback = BoxTextureGenOnSet
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TexturePixelsOnSet" }),
        .query.terms = {{ .id = ecs_id(TexturePixels) }},
        .events = { EcsOnSet },
        .callback = BoxTextureOnSet
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TexturePixelsOnRemove" }),
        .query.terms = {{ .id = ecs_id(TexturePixels) }},
        .events = { EcsOnRemove },
        .callback = BoxTexturePixelsOnRemove
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TextureGenOnRemove" }),
        .query.terms = {{ .id = ecs_id(TextureGen) }},
        .events = { EcsOnRemove },
        .callback = BoxTextureOnRemove
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TextureGraphOnSet" }),
        .query.terms = {{ .id = ecs_id(TextureGraph) }},
        .events = { EcsOnSet },
        .callback = BoxTextureOnSet
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TextureGraphOnRemove" }),
        .query.terms = {{ .id = ecs_id(TextureGraph) }},
        .events = { EcsOnRemove },
        .callback = BoxTextureGraphOnRemove
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "TextureFrame" }),
        .phase = EcsPreFrame,
        .run = BoxTextureFrame
    });
}
