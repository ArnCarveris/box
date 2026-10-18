/**
 * @file render/shader.c
 * @brief Registers the box.shader module and sends preprocessed Shader code to the renderer.
 *
 * Shaders are preprocessed in RenderExtract when they were set or a file they read changed,
 * and their code goes into the packet; the backend compiles it on the thread that renders.
 */

#define BOX_SHADER_IMPL
#include "preprocess.h"
#include "environment.h"

/** Engine side of a Shader: its handle, files and the code the backend last received */
typedef struct box_shader_t {
    ecs_entity_t entity;    /**< Entity with the Shader component */
    uint32_t handle;        /**< Handle of the shader in the backend */
    bool builtin;           /**< Engine shader, only sent to the webgpu backend */
    bool on_demand;         /**< Engine shader sent once a batch of the packet uses it */
    bool dirty;             /**< Preprocess before the next send */
    bool sent;              /**< The backend received code under the handle */
    bool pending;           /**< The code changed since the backend received it */
    int32_t revision;       /**< Times preprocessing produced new code */
    char *code;             /**< Last code that preprocessed without error */
    char *gpu_error;        /**< Error of the newest GPU outcome, NULL when it compiled */
    bool prep_failed;       /**< The last preprocessing failed */
    char *label;            /**< Path the code was read from, the label of the module */
    ecs_vec_t files;        /**< box_shader_file_t the last preprocessing read */
    ecs_vec_t sources;      /**< char*, files the lines of the code come from */
    ecs_vec_t lines;        /**< box_shader_loc_t, source position of every line of the code */
} box_shader_t;

/** State of the box.shader module shared by its observers and its extract system */
typedef struct box_shader_state_t {
    ecs_map_t shaders;          /**< Entity to box_shader_t */
    ecs_vec_t destroyed;        /**< uint32_t, handles of removed shaders the backend has */
    uint32_t last_handle;       /**< Last handle given to an application shader */
    const void *backend;        /**< Context of the backend the shaders were sent to */
    void (*render)(void*, const box_render_packet_t*); /**< Render callback of that backend */
    int32_t generation;         /**< Generation of that backend, see box_render_backend_t */
    ecs_time_t last_poll;       /**< Time the files were last checked */
    bool polled;                /**< The files were checked at least once */
    int32_t refs;               /**< Observers and systems that use the state */
} box_shader_state_t;

ECS_COMPONENT_DECLARE(Shader);
ECS_COMPONENT_DECLARE(ShaderStatus);
ECS_COMPONENT_DECLARE(ShaderSettings);

/** Free the strings of Shader components.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_shader_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Shader *s = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        ecs_os_free(s[i].path);
        ecs_os_free(s[i].defines);
    }
}

/** Copy Shader components with their own strings.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_shader_copy(
    void *dst,
    const void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Shader *d = dst;
    const Shader *s = src;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        char *path = ecs_os_strdup(s[i].path);
        char *defines = ecs_os_strdup(s[i].defines);
        ecs_os_free(d[i].path);
        ecs_os_free(d[i].defines);
        d[i].path = path;
        d[i].defines = defines;
    }
}

/** Move the strings of Shader components.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_shader_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Shader *d = dst;
    Shader *s = src;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        if (d[i].path != s[i].path) {
            ecs_os_free(d[i].path);
        }

        if (d[i].defines != s[i].defines) {
            ecs_os_free(d[i].defines);
        }

        d[i] = s[i];
        s[i].path = NULL;
        s[i].defines = NULL;
    }
}

/** Free the error of ShaderStatus components.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_shader_status_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ShaderStatus *s = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        ecs_os_free(s[i].error);
    }
}

/** Copy ShaderStatus components with their own error.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_shader_status_copy(
    void *dst,
    const void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ShaderStatus *d = dst;
    const ShaderStatus *s = src;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        char *error = ecs_os_strdup(s[i].error);
        ecs_os_free(d[i].error);
        d[i] = s[i];
        d[i].error = error;
    }
}

/** Move the error of ShaderStatus components.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_shader_status_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ShaderStatus *d = dst;
    ShaderStatus *s = src;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        if (d[i].error != s[i].error) {
            ecs_os_free(d[i].error);
        }

        d[i] = s[i];
        s[i].error = NULL;
    }
}

/** Free the files of a preprocessing.
 *
 * @param files The box_shader_file_t
 */
static void box_shader_files_fini(
    ecs_vec_t *files)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(files); i ++) {
        ecs_os_free(ecs_vec_get_t(files, box_shader_file_t, i)->path);
    }

    ecs_vec_fini_t(NULL, files, box_shader_file_t);
}

/** Free the line table of a shader.
 *
 * @param sources The source files
 * @param lines The source positions
 */
static void box_shader_lines_fini(
    ecs_vec_t *sources,
    ecs_vec_t *lines)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(sources); i ++) {
        ecs_os_free(*ecs_vec_get_t(sources, char*, i));
    }

    ecs_vec_fini_t(NULL, sources, char*);
    ecs_vec_fini_t(NULL, lines, box_shader_loc_t);
}

/** Free the engine side of a shader.
 *
 * @param s The shader
 */
static void box_shader_free(
    box_shader_t *s)
{
    box_shader_files_fini(&s->files);
    box_shader_lines_fini(&s->sources, &s->lines);
    ecs_os_free(s->code);
    ecs_os_free(s->label);
    ecs_os_free(s->gpu_error);
    ecs_os_free(s);
}

/** Release a reference to the module state, freeing it with the last observer or system.
 *
 * @param ctx The module state
 */
static void box_shader_state_release(
    void *ctx)
{
    box_shader_state_t *state = ctx;
    if (-- state->refs) {
        goto done;
    }

    ecs_map_iter_t it = ecs_map_iter(&state->shaders);
    while (ecs_map_next(&it)) {
        box_shader_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(&state->shaders);
    ecs_vec_fini_t(NULL, &state->destroyed, uint32_t);
    ecs_os_free(state);
done:
    return;
}

/** Get the engine side of a shader, creating it on first use.
 *
 * @param state The module state
 * @param entity The entity with the Shader
 * @param handle Handle of an engine shader, 0 to give it the next application handle
 * @return The engine side
 */
static box_shader_t* box_shader_ensure(
    box_shader_state_t *state,
    ecs_entity_t entity,
    uint32_t handle)
{
    box_shader_t *result = ecs_map_get_deref(&state->shaders, box_shader_t, entity);
    if (result) {
        goto done;
    }

    result = ecs_os_calloc_t(box_shader_t);
    result->entity = entity;
    result->handle = handle ? handle : ++ state->last_handle;
    ecs_vec_init_t(NULL, &result->files, box_shader_file_t, 0);
    ecs_vec_init_t(NULL, &result->sources, char*, 0);
    ecs_vec_init_t(NULL, &result->lines, box_shader_loc_t, 0);
    ecs_map_insert_ptr(&state->shaders, entity, result);
done:
    return result;
}

/** Preprocess a shader and publish the outcome in its ShaderStatus.
 *
 * @param world The world or a stage
 * @param s The shader
 */
static void box_shader_compile(
    ecs_world_t *world,
    box_shader_t *s)
{
    const Shader *shader = ecs_get(world, s->entity, Shader);
    box_shader_output_t out;
    s->dirty = false;
    if (!shader) {
        goto done;
    }

    box_shader_preprocess(&(box_shader_desc_t){
        .path = shader->path,
        .defines = shader->defines
    }, &out);
    box_shader_files_fini(&s->files);
    s->files = out.files;
    ecs_vec_init_t(NULL, &out.files, box_shader_file_t, 0);
    if (out.code) {
        box_shader_lines_fini(&s->sources, &s->lines);
        s->sources = out.sources;
        s->lines = out.lines;
        ecs_vec_init_t(NULL, &out.sources, char*, 0);
        ecs_vec_init_t(NULL, &out.lines, box_shader_loc_t, 0);
    }

    if (out.code && (!s->code || strcmp(s->code, out.code))) {
        ecs_os_free(s->code);
        ecs_os_free(s->label);
        s->code = out.code;
        s->label = ecs_os_strdup(shader->path);
        out.code = NULL;
        s->pending = true;
        s->revision ++;
    }

    if (out.error) {
        ecs_err("%s", out.error);
    }

    s->prep_failed = out.error != NULL;
    char *error = out.error ? out.error : s->gpu_error;
    ecs_set(world, s->entity, ShaderStatus, {
        .revision = s->revision,
        .ok = error == NULL,
        .error = error
    });

    box_shader_output_fini(&out);
done:
    return;
}

/** Append the code of a shader to the packet of the frame.
 *
 * @param world The world or a stage
 * @param s The shader
 */
static void box_shader_send(
    ecs_world_t *world,
    box_shader_t *s)
{
    box_render_packet_t *p = box_render_packet(world);
    uint32_t code_size = (uint32_t)ecs_os_strlen(s->code) + 1;
    uint32_t label_size = (uint32_t)ecs_os_strlen(s->label) + 1;
    box_render_shader_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_SHADER,
        s->sent ? BOX_RENDER_UPDATE : BOX_RENDER_CREATE, s->handle,
        (uint32_t)sizeof(box_render_shader_desc_t) + code_size + label_size);
    if (desc) {
        char *code = (char*)(desc + 1);
        desc->code_size = code_size;
        desc->label_size = label_size;
        desc->revision = s->revision;
        ecs_os_memcpy(code, s->code, code_size);
        ecs_os_memcpy(code + code_size, s->label, label_size);
    }

    s->sent = true;
    s->pending = false;
}

/** Send every shader again when the world renders with another backend.
 *
 * @param state The module state
 * @param backend The backend of the world
 */
static void box_shader_backend(
    box_shader_state_t *state,
    const box_render_backend_t *backend)
{
    if (backend->ctx == state->backend && backend->render == state->render &&
        backend->generation == state->generation)
    {
        goto done;
    }

    ecs_map_iter_t it = ecs_map_iter(&state->shaders);
    while (ecs_map_next(&it)) {
        box_shader_t *s = ecs_map_ptr(&it);
        s->sent = false;
    }

    ecs_vec_clear(&state->destroyed);
    state->backend = backend->ctx;
    state->render = backend->render;
    state->generation = backend->generation;
done:
    return;
}

/** Mark the shaders whose files changed since they were read, at most once per interval.
 *
 * @param world The world or a stage
 * @param state The module state
 */
static void box_shader_poll(
    ecs_world_t *world,
    box_shader_state_t *state)
{
    const ShaderSettings *settings = ecs_singleton_get(world, ShaderSettings);
    float interval = settings ? settings->poll_interval : BOX_SHADER_POLL_INTERVAL;
    ecs_time_t now = state->last_poll;
    double elapsed = ecs_time_measure(&now);
    int32_t i;
    if (interval < 0 || (state->polled && elapsed < interval)) {
        goto done;
    }

    state->last_poll = now;
    state->polled = true;
    ecs_map_iter_t it = ecs_map_iter(&state->shaders);
    while (ecs_map_next(&it)) {
        box_shader_t *s = ecs_map_ptr(&it);
        box_shader_file_t *files = ecs_vec_first(&s->files);
        for (i = 0; !s->dirty && i < ecs_vec_count(&s->files); i ++) {
            box_shader_stamp_t stamp;
            box_shader_stamp(files[i].path, &stamp);
            s->dirty = !box_shader_stamp_equals(&stamp, &files[i].stamp);
        }
    }

done:
    return;
}

/** Check whether a batch or a scatter of a packet runs a shader.
 *
 * @param p The packet
 * @param handle Handle of the shader
 * @return Whether a batch uses the shader as its pipeline, it scatters and runs a scatter, or
 * it assigns lights to clusters or draws shadow casters and a batch draws with the mesh shader,
 * or it culls and a batch draws with the mesh or the 2D shape shader, or an effect pass of a
 * view runs it, or it projects probe captures and the packet captures probes or reflections or
 * may process environments, or it builds atmosphere LUTs and the packet has an atmosphere, or
 * it converts environment images or draws environment backgrounds and the packet carries an
 * image or a view with a background, or it simulates or draws particles
 * and the packet has a particle pool
 */
static bool box_shader_used(
    const box_render_packet_t *p,
    uint32_t handle)
{
    bool result = handle == BOX_RENDER_SHADER_SCATTER && ecs_vec_count(&p->scatters);
    uint32_t pipeline = handle == BOX_RENDER_SHADER_CLUSTER ||
        handle == BOX_RENDER_SHADER_SHADOW || handle == BOX_RENDER_SHADER_CULL
        ? BOX_RENDER_SHADER_MESH
        : handle;
    uint32_t also = handle == BOX_RENDER_SHADER_CULL ? BOX_RENDER_SHADER_SHAPE2D : pipeline;
    int32_t i;
    if (box_render_effects_use(p, handle)) {
        result = true;
    }

    if (handle == BOX_RENDER_SHADER_ATMOSPHERE && box_render_atmosphere_use(p)) {
        result = true;
    }

    if (handle == BOX_RENDER_SHADER_ENVIRONMENT && box_render_environment_use(p)) {
        result = true;
    }

    if (box_render_particles_use(p, handle)) {
        result = true;
    }

    if (handle == BOX_RENDER_SHADER_BAKE && (ecs_vec_count(&p->bakes) ||
        ecs_vec_count(&p->reflections) || box_render_environment_processes(p)))
    {
        result = true;
    }

    for (i = 0; i < ecs_vec_count(&p->batches); i ++) {
        uint32_t used = ecs_vec_get_t(&p->batches, box_render_batch_t, i)->pipeline;
        if (used == pipeline || used == also) {
            result = true;
            break;
        }
    }

    return result;
}

/** Rewrite the line of a GPU message from the preprocessed code to the source file and line.
 *
 * @param s The shader
 * @param message The message as label:line: text, consumed
 * @return The message as file:line: text, or the message when the line is unknown
 */
static char* box_shader_map_message(
    box_shader_t *s,
    char *message)
{
    char *result = message;
    size_t len = s->label ? strlen(s->label) : 0;
    if (!len || strncmp(message, s->label, len) || message[len] != ':') {
        goto done;
    }

    char *end;
    long line = strtol(message + len + 1, &end, 10);
    if (end == message + len + 1 || *end != ':' || line < 1 ||
        line > ecs_vec_count(&s->lines))
    {
        goto done;
    }

    box_shader_loc_t *loc = ecs_vec_get_t(&s->lines, box_shader_loc_t, (int32_t)line - 1);
    result = flecs_asprintf("%s:%d%s", *ecs_vec_get_t(&s->sources, char*, loc->file),
        loc->line, end);
    ecs_os_free(message);
done:
    return result;
}

/** Apply the compile results the GPU backend produced to the shaders and their status.
 *
 * @param world The world or a stage
 * @param state The module state
 */
static void box_shader_results(
    ecs_world_t *world,
    box_shader_state_t *state)
{
    box_render_shader_result_t r;
    while (box_render_gpu_shader_result(world, &r)) {
        ecs_map_iter_t it = ecs_map_iter(&state->shaders);
        while (ecs_map_next(&it)) {
            box_shader_t *s = ecs_map_ptr(&it);
            if (s->handle != r.handle || s->revision != r.revision) {
                continue;
            }

            ecs_os_free(s->gpu_error);
            s->gpu_error = r.message ? box_shader_map_message(s, r.message) : NULL;
            r.message = NULL;
            if (!s->prep_failed && ecs_has(world, s->entity, ShaderStatus)) {
                ecs_set(world, s->entity, ShaderStatus, {
                    .revision = s->revision,
                    .ok = s->gpu_error == NULL,
                    .error = s->gpu_error
                });
            }

            break;
        }

        ecs_os_free(r.message);
    }
}

/** Preprocess changed shaders and send new code and removed shaders to the renderer.
 *
 * @param it The iterator of the system
 */
static void BoxShaderExtract(
    ecs_iter_t *it)
{
    box_shader_state_t *state = it->ctx;
    ecs_world_t *world = it->stage;
    box_render_gpu_stats_t stats;
    bool gpu = box_render_gpu_stats(world, &stats);
    int32_t i;
    box_shader_backend(state, box_render_get_backend(world));
    box_shader_poll(world, state);
    box_shader_results(world, state);
    for (i = 0; i < ecs_vec_count(&state->destroyed); i ++) {
        box_render_packet_resource(box_render_packet(world), BOX_RENDER_SHADER,
            BOX_RENDER_DESTROY, *ecs_vec_get_t(&state->destroyed, uint32_t, i), 0);
    }

    ecs_vec_clear(&state->destroyed);
    ecs_map_iter_t mit = ecs_map_iter(&state->shaders);
    while (ecs_map_next(&mit)) {
        box_shader_t *s = ecs_map_ptr(&mit);
        if (s->builtin && !gpu) {
            continue;
        }

        if (s->on_demand && !s->sent && !box_shader_used(box_render_packet(world), s->handle)) {
            continue;
        }

        if (s->dirty) {
            box_shader_compile(world, s);
        }

        if (s->code && (!s->sent || s->pending)) {
            box_shader_send(world, s);
        }
    }
}

/** Mark set shaders for preprocessing.
 *
 * @param it The iterator of the observer
 */
static void BoxShaderOnSet(
    ecs_iter_t *it)
{
    box_shader_state_t *state = it->ctx;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_shader_ensure(state, it->entities[i], 0)->dirty = true;
    }
}

/** Forget removed shaders and release them in the backend with the next packet.
 *
 * @param it The iterator of the observer
 */
static void BoxShaderOnRemove(
    ecs_iter_t *it)
{
    box_shader_state_t *state = it->ctx;
    bool alive = it->other_table && ecs_table_get_type(it->other_table)->count;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_shader_t *s = ecs_map_get_deref(&state->shaders, box_shader_t, it->entities[i]);
        if (s && s->sent) {
            *ecs_vec_append_t(NULL, &state->destroyed, uint32_t) = s->handle;
        }

        if (s) {
            ecs_map_remove(&state->shaders, it->entities[i]);
            box_shader_free(s);
        }

        if (alive) {
            ecs_remove(it->stage, it->entities[i], ShaderStatus);
        }
    }
}

/** Create an engine shader with a fixed handle.
 *
 * @param world The world
 * @param state The module state
 * @param parent Scope of the engine shaders
 * @param name Name of the shader entity
 * @param path WGSL file
 * @param handle Handle the backend knows the shader by
 * @param on_demand Send the shader once a batch draws with it instead of right away
 */
static void box_shader_builtin(
    ecs_world_t *world,
    box_shader_state_t *state,
    ecs_entity_t parent,
    const char *name,
    const char *path,
    uint32_t handle,
    bool on_demand)
{
    ecs_entity_t e = ecs_entity(world, { .name = name, .parent = parent });
    box_shader_t *s = box_shader_ensure(state, e, handle);
    s->builtin = true;
    s->on_demand = on_demand;
    ecs_set(world, e, Shader, { .path = ECS_CONST_CAST(char*, path) });
}

void BoxShaderImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxShader);
    ECS_IMPORT(world, BoxRender);

    ECS_META_COMPONENT(world, Shader);
    ECS_META_COMPONENT(world, ShaderStatus);
    ECS_META_COMPONENT(world, ShaderSettings);

    ecs_set_hooks(world, Shader, {
        .ctor = flecs_default_ctor,
        .dtor = box_shader_dtor,
        .copy = box_shader_copy,
        .move = box_shader_move
    });

    ecs_set_hooks(world, ShaderStatus, {
        .ctor = flecs_default_ctor,
        .dtor = box_shader_status_dtor,
        .copy = box_shader_status_copy,
        .move = box_shader_status_move
    });

    box_shader_state_t *state = ecs_os_calloc_t(box_shader_state_t);
    ecs_map_init(&state->shaders, NULL);
    ecs_vec_init_t(NULL, &state->destroyed, uint32_t, 0);
    state->last_handle = BOX_RENDER_SHADER_FIRST - 1;
    state->refs = 3;

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "ShaderOnSet" }),
        .query.terms = {{ .id = ecs_id(Shader) }},
        .events = { EcsOnSet },
        .callback = BoxShaderOnSet,
        .ctx = state,
        .ctx_free = box_shader_state_release
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "ShaderOnRemove" }),
        .query.terms = {{ .id = ecs_id(Shader) }},
        .events = { EcsOnRemove },
        .callback = BoxShaderOnRemove,
        .ctx = state,
        .ctx_free = box_shader_state_release
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "ShaderExtract" }),
        .phase = RenderExtract,
        .run = BoxShaderExtract,
        .ctx = state,
        .ctx_free = box_shader_state_release
    });

    ecs_singleton_set(world, ShaderSettings, { .poll_interval = BOX_SHADER_POLL_INTERVAL });

    ecs_entity_t builtin = ecs_entity(world, { .name = "builtin" });
    box_shader_builtin(world, state, builtin, "blit", "etc/shaders/blit.wgsl",
        BOX_RENDER_SHADER_BLIT, false);
    box_shader_builtin(world, state, builtin, "mesh", "etc/shaders/mesh.wgsl",
        BOX_RENDER_SHADER_MESH, true);
    box_shader_builtin(world, state, builtin, "scatter", "etc/shaders/scatter.wgsl",
        BOX_RENDER_SHADER_SCATTER, true);
    box_shader_builtin(world, state, builtin, "cluster", "etc/shaders/cluster.wgsl",
        BOX_RENDER_SHADER_CLUSTER, true);
    box_shader_builtin(world, state, builtin, "shadow", "etc/shaders/shadow.wgsl",
        BOX_RENDER_SHADER_SHADOW, true);
    box_shader_builtin(world, state, builtin, "cull", "etc/shaders/cull.wgsl",
        BOX_RENDER_SHADER_CULL, true);
    box_shader_builtin(world, state, builtin, "shape2d", "etc/shaders/shape2d.wgsl",
        BOX_RENDER_SHADER_SHAPE2D, true);
    box_shader_builtin(world, state, builtin, "tonemap", "etc/shaders/tonemap.wgsl",
        BOX_RENDER_SHADER_TONEMAP, true);
    box_shader_builtin(world, state, builtin, "exposure", "etc/shaders/exposure.wgsl",
        BOX_RENDER_SHADER_EXPOSURE, true);
    box_shader_builtin(world, state, builtin, "bloom", "etc/shaders/bloom.wgsl",
        BOX_RENDER_SHADER_BLOOM, true);
    box_shader_builtin(world, state, builtin, "ssao", "etc/shaders/ssao.wgsl",
        BOX_RENDER_SHADER_SSAO, true);
    box_shader_builtin(world, state, builtin, "fog", "etc/shaders/fog.wgsl",
        BOX_RENDER_SHADER_FOG, true);
    box_shader_builtin(world, state, builtin, "bake_sh", "etc/shaders/bake_sh.wgsl",
        BOX_RENDER_SHADER_BAKE, true);
    box_shader_builtin(world, state, builtin, "atmosphere", "etc/shaders/atmosphere.wgsl",
        BOX_RENDER_SHADER_ATMOSPHERE, true);
    box_shader_builtin(world, state, builtin, "environment", "etc/shaders/environment.wgsl",
        BOX_RENDER_SHADER_ENVIRONMENT, true);
    box_shader_builtin(world, state, builtin, "particle_sim", "etc/shaders/particle_sim.wgsl",
        BOX_RENDER_SHADER_PARTICLE_SIM, true);
    box_shader_builtin(world, state, builtin, "particle_draw",
        "etc/shaders/particle_draw.wgsl", BOX_RENDER_SHADER_PARTICLE_DRAW, true);
}
