/**
 * @file texture/texture.c
 * @brief Texture components and the loads of TexturePath on the job system.
 *
 * Setting a TexturePath submits a decode job; its completion writes Texture and
 * TextureStatus and queues the pixels in the registry, which the render extract sends. The
 * file watch loads the image again when the file the decode read changes.
 */

#define BOX_TEXTURE_IMPL
#include "texture.h"
#include "../jobs/jobs.h"

ECS_COMPONENT_DECLARE(TextureRegistry);

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
        ecs_os_free(tex);
    }

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

/** Write the status of a texture.
 *
 * @param world The world or a stage
 * @param e The texture entity
 * @param state Progress of the load
 * @param revision Loads that succeeded
 * @param error Why the load failed, NULL unless it failed
 */
static void box_texture_status(
    ecs_world_t *world,
    ecs_entity_t e,
    TextureState state,
    int32_t revision,
    const char *error)
{
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
static int32_t box_texture_revision(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const TextureStatus *status = ecs_get(world, e, TextureStatus);

    return status ? status->revision : 0;
}

/** Decode the image of a job on a worker thread.
 *
 * @param ctx The job
 */
static void box_texture_work(
    void *ctx)
{
    box_texture_job_t *job = ctx;
    box_texture_decode(job);
    ecs_os_mutex_lock(job->sync->lock);
    job->sync->working --;
    ecs_os_mutex_unlock(job->sync->lock);
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
    ecs_os_free(job->file);
    ecs_os_free(job->pixels);
    ecs_os_free(job->error);
    box_texture_sync_release(job->sync);
    ecs_os_free(job);
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
    box_texture_t *tex = ecs_map_get_deref(&t->textures, box_texture_t, e);
    int32_t revision = box_texture_revision(world, e);
    if (!tex) {
        tex = ecs_os_calloc_t(box_texture_t);
        tex->entity = e;
        ecs_map_insert_ptr(&t->textures, e, tex);
    }

    if (!t->quality) {
        t->quality = box_textures_quality(world, t);
    }

    box_texture_job_t *job = ecs_os_calloc_t(box_texture_job_t);
    job->sync = t->sync;
    job->entity = e;
    job->generation = ++ tex->generation;
    job->path = path->path ? ecs_os_strdup(path->path) : NULL;
    job->request_width = path->width;
    job->request_height = path->height;
    job->quality = t->quality;
    job->linear = path->linear;
    job->single_channel = path->single_channel;
    t->sync->refs ++;
    ecs_os_mutex_lock(t->sync->lock);
    t->sync->working ++;
    ecs_os_mutex_unlock(t->sync->lock);
    box_texture_status(world, e, TextureLoading, revision, NULL);
    if (box_job_submit(world, &(box_job_desc_t){
        .work = box_texture_work,
        .complete = box_texture_complete,
        .ctx_free = box_texture_job_free,
        .ctx = job
    })) {
        ecs_os_mutex_lock(t->sync->lock);
        t->sync->working --;
        ecs_os_mutex_unlock(t->sync->lock);
        box_texture_status(world, e, TextureFailed, revision, "cannot submit the load job");
        box_texture_job_free(job);
    }
}

void box_textures_reload(
    ecs_world_t *world,
    box_textures_t *t,
    bool sent_only)
{
    ecs_map_iter_t it = ecs_map_iter(&t->textures);
    while (ecs_map_next(&it)) {
        box_texture_t *tex = ecs_map_ptr(&it);
        const TexturePath *path = ecs_is_alive(world, tex->entity)
            ? ecs_get(world, tex->entity, TexturePath)
            : NULL;
        bool reload = !sent_only || tex->sent;
        tex->sent = false;
        if (path && reload) {
            box_texture_request(world, t, tex->entity, path);
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
        box_texture_request(world, t, owner, path);
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
    const TexturePath *paths = ecs_field(it, TexturePath, 0);
    int32_t i;
    if (!t) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        box_file_watch_clear(it->world, t->watch, it->entities[i]);
        box_texture_request(it->world, t, it->entities[i], &paths[i]);
    }

done:
    return;
}

/** Forget the textures whose TexturePath is removed and release their backend image.
 *
 * @param it The iterator of the observer
 */
static void BoxTextureOnRemove(
    ecs_iter_t *it)
{
    box_textures_t *t = box_textures_get(it->world);
    bool alive = it->other_table && ecs_table_get_type(it->other_table)->count;
    int32_t i;
    if (!t || ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        box_texture_t *tex = ecs_map_get_deref(&t->textures, box_texture_t, e);
        box_file_watch_clear(it->world, t->watch, e);
        if (tex) {
            if (tex->handle) {
                *ecs_vec_append_t(NULL, &t->released, uint32_t) = tex->handle;
            }

            ecs_map_remove(&t->textures, e);
            ecs_os_free(tex->pixels);
            ecs_os_free(tex);
        }

        if (alive) {
            ecs_remove(it->stage, e, Texture);
            ecs_remove(it->stage, e, TextureStatus);
        }
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

/** Load the textures again when the texture quality of the surfaces changed and, in a
 * headless run, wait for the decodes in flight so a frame always sees the same images, and
 * warn when the wait gives up.
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

    int32_t quality = box_textures_quality(it->world, t);
    if (t->quality && quality != t->quality) {
        t->quality = quality;
        box_textures_reload(it->world, t, false);
    }

    if (!app || !app->headless) {
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
    ECS_IMPORT(world, BoxSurface);
    ECS_IMPORT(world, BoxApp);
    ECS_IMPORT(world, BoxJobs);

    ECS_META_COMPONENT(world, Texture);
    ECS_META_COMPONENT(world, TexturePath);
    ECS_META_COMPONENT(world, TextureState);
    ECS_META_COMPONENT(world, TextureStatus);
    box_owned_hooks(world, TexturePath, BOX_OWNED_STR(TexturePath, path));
    box_owned_hooks(world, TextureStatus, BOX_OWNED_STR(TextureStatus, error));

    ECS_COMPONENT_DEFINE(world, TextureRegistry);
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

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "TextureFrame" }),
        .phase = EcsPreFrame,
        .run = BoxTextureFrame
    });
}
