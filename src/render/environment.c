/**
 * @file render/environment.c
 * @brief Keeps the cubemaps of the environment sources and gives the lit views their environment.
 *
 * A source entity has Environment; the cubemap is a BOX_RENDER_ENVIRONMENT resource created on
 * demand by box_render_environment_handle, which views and sources call during RenderExtract.
 */

#include "environment.h"

/** First handle of the cubemaps of environments */
#define BOX_ENV_HANDLE_FIRST (1)

/** Smallest valid Environment.resolution */
#define BOX_ENV_RESOLUTION_MIN (8)

/** Largest valid Environment.resolution */
#define BOX_ENV_RESOLUTION_MAX (1024)

/** EnvironmentStatus.error of an invalid resolution */
#define BOX_ENV_RESOLUTION_ERROR "resolution must be a power of two from 8 to 1024"

/** The cubemap of an environment source */
typedef struct box_env_t {
    ecs_entity_t entity;    /**< Entity with Environment */
    uint32_t handle;        /**< Handle of the BOX_RENDER_ENVIRONMENT, 0 before it is assigned */
    bool created;           /**< The backend has the cubemap */
    int32_t resolution;     /**< Resolution the cubemap was created with */
    uint32_t filter;        /**< Filter the cubemap was created or updated with */
    int32_t content;        /**< Version the cubemap holds, 0 when it is empty */
    char *error;            /**< Why the source has no content, NULL for none */
    bool seen;              /**< The entity has Environment in this frame */
} box_env_t;

/** Main thread state of the environment extract */
typedef struct box_render_env_t {
    ecs_query_t *environments;  /**< Environment and EnvironmentStatus */
    ecs_map_t sources;          /**< Entity to box_env_t* */
    ecs_vec_t free_handles;     /**< uint32_t, handles to reuse */
    ecs_vec_t gone;             /**< ecs_entity_t, records of this frame without Environment */
    uint32_t next_handle;       /**< Next handle */
    const void *backend;        /**< Context of the backend the cubemaps were created on */
    void (*render)(void*, const box_render_packet_t*); /**< Render callback of that backend */
    int32_t generation;         /**< Generation of that backend, see box_render_backend_t */
} box_render_env_t;

/** Get the state of the environment extract of a world.
 *
 * @param world The world
 * @return The state, NULL without the extract
 */
static box_render_env_t* box_env_state(
    const ecs_world_t *world)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractEnvironment");
    const ecs_system_t *s = system ? ecs_system_get(world, system) : NULL;

    return s ? s->ctx : NULL;
}

/** Find the record of a source.
 *
 * @param state The state
 * @param e The entity
 * @return The record, NULL when the entity has none
 */
static box_env_t* box_env_find(
    const box_render_env_t *state,
    ecs_entity_t e)
{
    return ecs_map_get_deref(&state->sources, box_env_t, e);
}

/** Find or add the record of a source.
 *
 * @param state The state
 * @param e The entity
 * @return The record
 */
static box_env_t* box_env_ensure(
    box_render_env_t *state,
    ecs_entity_t e)
{
    box_env_t *result = box_env_find(state, e);
    if (!result) {
        result = ecs_os_calloc_t(box_env_t);
        result->entity = e;
        ecs_map_insert_ptr(&state->sources, e, result);
    }

    return result;
}

/** Check whether a resolution is a power of two from 8 to 1024.
 *
 * @param resolution The resolution
 * @return Whether a cubemap can be created with it
 */
static bool box_env_valid(
    int32_t resolution)
{
    return resolution >= BOX_ENV_RESOLUTION_MIN && resolution <= BOX_ENV_RESOLUTION_MAX &&
        !(resolution & (resolution - 1));
}

/** Forget the cubemaps of the previous backend when the world renders with another one or the
 * device was recreated, so the next handle request creates them again.
 *
 * @param world The world
 * @param state The state
 */
static void box_env_backend(
    const ecs_world_t *world,
    box_render_env_t *state)
{
    const box_render_backend_t *backend = box_render_get_backend(world);
    if (backend->ctx == state->backend && backend->render == state->render &&
        backend->generation == state->generation)
    {
        goto done;
    }

    ecs_map_iter_t it = ecs_map_iter(&state->sources);
    while (ecs_map_next(&it)) {
        box_env_t *env = ecs_map_ptr(&it);
        env->created = false;
        env->content = 0;
    }

    state->backend = backend->ctx;
    state->render = backend->render;
    state->generation = backend->generation;
done:
    return;
}

/** Destroy the cubemap of a source on the backend and release its handle.
 *
 * @param world The world
 * @param state The state
 * @param env The record
 */
static void box_env_release(
    ecs_world_t *world,
    box_render_env_t *state,
    box_env_t *env)
{
    if (env->created) {
        box_render_packet_resource(box_render_packet(world), BOX_RENDER_ENVIRONMENT,
            BOX_RENDER_DESTROY, env->handle, 0);
    }

    if (env->handle) {
        *ecs_vec_append_t(NULL, &state->free_handles, uint32_t) = env->handle;
    }

    env->handle = 0;
    env->created = false;
    env->content = 0;
}

/** Write EnvironmentStatus.error when it differs from what the status holds.
 *
 * @param world The world
 * @param e The entity
 * @param error The error, NULL for none
 */
static void box_env_status_error(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *error)
{
    EnvironmentStatus *status = ecs_get_mut(world, e, EnvironmentStatus);
    bool same = status && (status->error == error ||
        (status->error && error && !ecs_os_strcmp(status->error, error)));
    if (!status || same) {
        goto done;
    }

    ecs_os_free(status->error);
    status->error = error ? ecs_os_strdup(error) : NULL;
    ecs_modified(world, e, EnvironmentStatus);
done:
    return;
}

/** Visit every Environment: write its error, destroy the cubemap of an invalid resolution and
 * mark the records of the sources that still have one.
 *
 * @param world The world
 * @param state The state
 */
static void box_env_collect(
    ecs_world_t *world,
    box_render_env_t *state)
{
    ecs_iter_t it = ecs_query_iter(world, state->environments);
    while (ecs_query_next(&it)) {
        const Environment *values = ecs_field(&it, Environment, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            box_env_t *env = box_env_find(state, it.entities[i]);
            bool valid = box_env_valid(values[i].resolution);
            const char *error = valid ? (env ? env->error : NULL) : BOX_ENV_RESOLUTION_ERROR;
            if (env) {
                env->seen = true;
            }

            if (env && !valid) {
                box_env_release(world, state, env);
            }

            box_env_status_error(world, it.entities[i], error);
        }
    }
}

/** Give a view of the packet the environment of its LightSettings, when it is lit.
 *
 * @param world The world
 * @param v The view
 */
static void box_env_view(
    ecs_world_t *world,
    box_render_view_t *v)
{
    const View *view = v->view && ecs_is_alive(world, v->view)
        ? ecs_get(world, v->view, View)
        : NULL;
    const EnvironmentSettings *settings = view ? &view->lighting.environment : NULL;
    ecs_os_memset_t(&v->environment, 0, box_render_view_env_t);
    if (!settings || !view->lighting.enabled || !settings->source ||
        !ecs_is_alive(world, settings->source))
    {
        goto done;
    }

    v->environment.handle = box_render_environment_handle(world, settings->source);
    if (v->environment.handle) {
        v->environment.intensity = settings->intensity;
        v->environment.yaw = settings->yaw;
        v->environment.background = settings->background;
    }

done:
    return;
}

/** Keep the cubemaps of the environment sources and give the views their environment.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractEnvironment(
    ecs_iter_t *it)
{
    box_render_env_t *state = it->ctx;
    ecs_world_t *world = it->world;
    box_render_packet_t *p = box_render_packet(world);
    int32_t i;
    box_env_backend(world, state);
    ecs_map_iter_t mit = ecs_map_iter(&state->sources);
    while (ecs_map_next(&mit)) {
        ((box_env_t*)ecs_map_ptr(&mit))->seen = false;
    }

    box_env_collect(world, state);
    ecs_vec_clear(&state->gone);
    mit = ecs_map_iter(&state->sources);
    while (ecs_map_next(&mit)) {
        if (!((box_env_t*)ecs_map_ptr(&mit))->seen) {
            *ecs_vec_append_t(NULL, &state->gone, ecs_entity_t) = ecs_map_key(&mit);
        }
    }

    for (i = 0; i < ecs_vec_count(&state->gone); i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&state->gone, ecs_entity_t, i);
        box_env_t *env = box_env_find(state, e);
        box_env_release(world, state, env);
        ecs_map_remove(&state->sources, e);
        ecs_os_free(env->error);
        ecs_os_free(env);
    }

    for (i = 0; i < ecs_vec_count(&p->views); i ++) {
        box_env_view(world, ecs_vec_get_t(&p->views, box_render_view_t, i));
    }
}

/** Free the state of the environment extract.
 *
 * @param ctx The state
 */
static void box_env_free(
    void *ctx)
{
    box_render_env_t *state = ctx;
    ecs_map_iter_t it = ecs_map_iter(&state->sources);
    while (ecs_map_next(&it)) {
        box_env_t *env = ecs_map_ptr(&it);
        ecs_os_free(env->error);
        ecs_os_free(env);
    }

    ecs_map_fini(&state->sources);
    ecs_vec_fini_t(NULL, &state->free_handles, uint32_t);
    ecs_vec_fini_t(NULL, &state->gone, ecs_entity_t);
    ecs_os_free(state);
}

void box_render_environment_init(
    ecs_world_t *world)
{
    box_render_env_t *state = ecs_os_calloc_t(box_render_env_t);
    ecs_map_init(&state->sources, NULL);
    ecs_vec_init_t(NULL, &state->free_handles, uint32_t, 0);
    ecs_vec_init_t(NULL, &state->gone, ecs_entity_t, 0);
    state->next_handle = BOX_ENV_HANDLE_FIRST;
    state->environments = ecs_query(world, {
        .terms = {
            { .id = ecs_id(Environment), .inout = EcsIn },
            { .id = ecs_id(EnvironmentStatus), .inout = EcsInOutNone }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderExtractEnvironment" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractEnvironment,
        .ctx = state,
        .ctx_free = box_env_free
    });
}

uint32_t box_render_environment_handle(
    ecs_world_t *world,
    ecs_entity_t e)
{
    box_render_env_t *state = box_env_state(world);
    const Environment *value = state && e && ecs_is_alive(world, e)
        ? ecs_get(world, e, Environment)
        : NULL;
    uint32_t result = 0;
    if (!value || !box_env_valid(value->resolution)) {
        goto done;
    }

    box_env_backend(world, state);
    box_env_t *env = box_env_ensure(state, e);
    env->seen = true;
    if (!env->handle) {
        if (ecs_vec_count(&state->free_handles)) {
            env->handle = *ecs_vec_last_t(&state->free_handles, uint32_t);
            ecs_vec_remove_last(&state->free_handles);
        } else {
            env->handle = state->next_handle ++;
        }
    }

    bool create = !env->created || env->resolution != value->resolution;
    if (create || env->filter != (uint32_t)value->filter) {
        box_render_environment_desc_t *desc = box_render_packet_resource(
            box_render_packet(world), BOX_RENDER_ENVIRONMENT,
            create ? BOX_RENDER_CREATE : BOX_RENDER_UPDATE, env->handle,
            sizeof(box_render_environment_desc_t));
        desc->resolution = (uint32_t)value->resolution;
        desc->filter = (uint32_t)value->filter;
        env->created = true;
        env->resolution = value->resolution;
        env->filter = (uint32_t)value->filter;
        env->content = create ? 0 : env->content;
    }

    result = env->handle;
done:
    return result;
}

uint32_t box_render_environment_find(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const box_render_env_t *state = box_env_state(world);
    const box_env_t *env = state ? box_env_find(state, e) : NULL;

    return env && env->created ? env->handle : 0;
}

int32_t box_render_environment_content(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const box_render_env_t *state = box_env_state(world);
    const box_env_t *env = state ? box_env_find(state, e) : NULL;

    return env && env->created ? env->content : 0;
}

int32_t box_render_environment_sent(
    ecs_world_t *world,
    ecs_entity_t e)
{
    box_render_env_t *state = box_env_state(world);
    box_env_t *env = state ? box_env_find(state, e) : NULL;
    EnvironmentStatus *status = ecs_get_mut(world, e, EnvironmentStatus);
    int32_t result = 0;
    if (!status) {
        goto done;
    }

    result = ++ status->version;
    ecs_modified(world, e, EnvironmentStatus);
    if (env) {
        env->content = result;
    }

done:
    return result;
}

void box_render_environment_error(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *error)
{
    box_render_env_t *state = box_env_state(world);
    if (!state || !ecs_is_alive(world, e) || !ecs_has(world, e, Environment)) {
        goto done;
    }

    box_env_t *env = box_env_ensure(state, e);
    env->seen = true;
    ecs_os_free(env->error);
    env->error = error ? ecs_os_strdup(error) : NULL;
    const Environment *value = ecs_get(world, e, Environment);
    if (box_env_valid(value->resolution)) {
        box_env_status_error(world, e, error);
    }

done:
    return;
}

bool box_render_environment_use(
    const box_render_packet_t *p)
{
    bool result = false;
    int32_t i;
    for (i = 0; !result && i < ecs_vec_count(&p->resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &p->resources, box_render_resource_t, i);
        result = r->kind == BOX_RENDER_ENVIRONMENT_IMAGE;
    }

    for (i = 0; !result && i < ecs_vec_count(&p->views); i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, i);
        result = v->environment.handle && v->environment.background;
    }

    return result;
}

bool box_render_environment_processes(
    const box_render_packet_t *p)
{
    bool result = false;
    int32_t i;
    for (i = 0; !result && i < ecs_vec_count(&p->resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &p->resources, box_render_resource_t, i);
        result = r->kind == BOX_RENDER_ENVIRONMENT_IMAGE ||
            r->kind == BOX_RENDER_ATMOSPHERE_ENVIRONMENT ||
            (r->kind == BOX_RENDER_ENVIRONMENT && r->op != BOX_RENDER_DESTROY);
    }

    return result;
}
