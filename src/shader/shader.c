/**
 * @file shader/shader.c
 * @brief Registers the box.shader module and compiles a Shader when it is set.
 *
 * The builtin shaders of the engine are ordinary Shader entities under
 * `box.shader.builtin`, so the renderer receives them through the same resource
 * command as a shader a project writes and reloading one is a component write.
 */

#define BOX_SHADER_IMPL
#include "shader_internal.h"

ECS_COMPONENT_DECLARE(ShaderImpl);
ECS_COMPONENT_DECLARE(ShaderState);

/** One shader the renderer builds its own pipelines from */
typedef struct box_shader_builtin_t {
    const char *name;   /**< Entity name under box.shader.builtin */
    const char *path;   /**< WGSL file of the shader */
    uint32_t handle;    /**< Shader slot the renderer expects it in */
} box_shader_builtin_t;

/** The shaders the renderer needs before it can build its pipelines */
static const box_shader_builtin_t box_shader_builtins[] = {
    { "mesh", "etc/shaders/mesh.wgsl", BOX_SHADER_MESH },
    { "cluster", "etc/shaders/cluster.wgsl", BOX_SHADER_CLUSTER },
    { "cull", "etc/shaders/cull.wgsl", BOX_SHADER_CULL },
    { "hiz_copy", "etc/shaders/hiz_copy.wgsl", BOX_SHADER_HIZ_COPY },
    { "hiz_reduce", "etc/shaders/hiz_reduce.wgsl", BOX_SHADER_HIZ_REDUCE },
    { "hiz", "etc/shaders/hiz.wgsl", BOX_SHADER_HIZ },
    { "shadow", "etc/shaders/shadow.wgsl", BOX_SHADER_SHADOW },
    { "environment", "etc/shaders/environment.wgsl", BOX_SHADER_ENV },
    { "env_filter", "etc/shaders/env_filter.wgsl", BOX_SHADER_ENV_FILTER },
    { "bake_grid", "etc/shaders/bake_grid.wgsl", BOX_SHADER_BAKE_GRID },
    { "bake", "etc/shaders/bake.wgsl", BOX_SHADER_BAKE },
    { "blur", "etc/shaders/blur.wgsl", BOX_SHADER_BLUR },
    { "motion", "etc/shaders/motion.wgsl", BOX_SHADER_MOTION }
};

/** Registry destructor releasing the sources that never reached a packet.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_shader_state_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    ShaderState *state = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        int32_t j;
        for (j = 0; j < ecs_vec_count(&state[i].pending); j ++) {
            free(ecs_vec_get_t(&state[i].pending, box_shader_cmd_t, j)->source);
        }

        ecs_vec_fini_t(NULL, &state[i].pending, box_shader_cmd_t);
    }
}

/** Copy a string with the allocator the renderer releases it with.
 *
 * @param str The string
 * @return The copy
 */
static char* box_shader_copy(
    const char *str)
{
    size_t len = strlen(str) + 1;
    char *result = malloc(len);
    memcpy(result, str, len);

    return result;
}

uint32_t box_shader_create(
    ecs_world_t *world,
    uint32_t handle,
    const char *source)
{
    ShaderState *state = ecs_get_mut(world, ecs_id(ShaderState), ShaderState);
    ecs_assert(state != NULL, ECS_INVALID_OPERATION, "box.shader is not imported");
    if (handle == BOX_SHADER_NONE) {
        handle = state->count ++;
    }

    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->pending); i ++) {
        box_shader_cmd_t *cmd = ecs_vec_get_t(&state->pending, box_shader_cmd_t, i);
        if (cmd->handle == handle) {
            free(cmd->source);
            cmd->source = box_shader_copy(source);
            goto done;
        }
    }

    box_shader_cmd_t *cmd = ecs_vec_append_t(NULL, &state->pending, box_shader_cmd_t);
    cmd->handle = handle;
    cmd->source = box_shader_copy(source);
done:
    return handle;
}

void box_shader_extract(
    ecs_world_t *world,
    box_packet_t *p)
{
    ShaderState *state = ecs_get_mut(world, ecs_id(ShaderState), ShaderState);
    if (!state) {
        goto done;
    }

    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->pending); i ++) {
        box_shader_cmd_t *cmd = box_vec_push(&p->shader_create);
        *cmd = *ecs_vec_get_t(&state->pending, box_shader_cmd_t, i);
    }

    ecs_vec_clear(&state->pending);
done:
    return;
}

/** Write the outcome of a preprocessor run to the status of a shader entity.
 *
 * @param world The world
 * @param e The shader entity
 * @param error The error, NULL when the run succeeded
 */
static void box_shader_status(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *error)
{
    ShaderStatus *status = ecs_get_mut(world, e, ShaderStatus);
    if (!status) {
        goto done;
    }

    ecs_os_free(status->error);
    status->error = ecs_os_strdup(error ? error : "");
    status->ok = error == NULL;
    ecs_modified(world, e, ShaderStatus);
done:
    return;
}

/** Preprocess the file of a shader entity and hand the source to the renderer.
 *
 * @param world The world
 * @param e The shader entity
 * @param shader The component
 */
static void box_shader_update(
    ecs_world_t *world,
    ecs_entity_t e,
    const Shader *shader)
{
    if (!shader->path || !shader->path[0]) {
        goto done;
    }

    char resolved[BOX_SHADER_PATH_MAX];
    if (!box_shader_resolve(shader->path, resolved, sizeof(resolved))) {
        ecs_err("box.shader: %s: file not found", shader->path);
        box_shader_status(world, e, "file not found");
        goto done;
    }

    box_shader_result_t result;
    box_shader_preprocess(world, resolved, shader->macros, &result);
    if (result.error) {
        ecs_err("box.shader: %s", result.error);
        box_shader_status(world, e, result.error);
        box_shader_result_fini(&result);
        goto done;
    }

    ShaderImpl *impl = ecs_get_mut(world, e, ShaderImpl);
    ecs_assert(impl != NULL, ECS_INTERNAL_ERROR, "Shader without ShaderImpl");
    uint32_t handle = box_shader_create(world,
        impl->assigned ? impl->handle : BOX_SHADER_NONE, result.source);
    impl->handle = handle;
    impl->assigned = true;
    box_shader_status(world, e, NULL);
    box_shader_result_fini(&result);
done:
    return;
}

void box_shader_reload(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const Shader *shader = ecs_get(world, e, Shader);
    if (shader) {
        box_shader_update(world, e, shader);
    }
}

void box_shader_layers_changed(
    ecs_world_t *world)
{
    int32_t count;
    box_render_layers(world, &count);
    ecs_entity_t mesh = ecs_lookup(world, "box.shader.builtin.mesh");
    if (mesh) {
        box_shader_reload(world, mesh);
    }
}

/** A Shader was set: preprocess its file and send the source to the renderer.
 *
 * @param it Iterator over the changed shaders
 */
static void box_shader_on_set(
    ecs_iter_t *it)
{
    Shader *shader = ecs_field(it, Shader, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_shader_update(it->world, it->entities[i], &shader[i]);
    }
}

/** Create the Shader entities of the shaders the renderer builds its passes from.
 *
 * @param world The world
 */
static void box_shader_builtin_init(
    ecs_world_t *world)
{
    ecs_entity_t scope = ecs_entity(world, { .name = "builtin" });
    int32_t i;
    for (i = 0; i < (int32_t)(sizeof(box_shader_builtins) /
        sizeof(box_shader_builtin_t)); i ++)
    {
        const box_shader_builtin_t *b = &box_shader_builtins[i];
        ecs_entity_t e = ecs_entity(world, { .name = b->name, .parent = scope });
        ecs_set(world, e, ShaderImpl, { .handle = b->handle, .assigned = true });
        Shader *shader = ecs_ensure(world, e, Shader);
        shader->path = ecs_os_strdup(b->path);
        shader->macros = NULL;
        ecs_modified(world, e, Shader);
    }
}

void BoxShaderImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxShader);
    ECS_IMPORT(world, BoxTypes);

    ECS_META_COMPONENT(world, Shader);
    ECS_META_COMPONENT(world, ShaderStatus);
    ECS_COMPONENT_DEFINE(world, ShaderImpl);
    ECS_COMPONENT_DEFINE(world, ShaderState);

    ecs_set_hooks(world, ShaderImpl, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, ShaderState, {
        .ctor = flecs_default_ctor,
        .dtor = box_shader_state_dtor
    });

    ShaderState *state = ecs_singleton_ensure(world, ShaderState);
    ecs_vec_init_t(NULL, &state->pending, box_shader_cmd_t, 0);
    state->count = BOX_SHADER_BUILTIN_MAX;
    ecs_add_pair(world, ecs_id(Shader), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(ShaderImpl), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(ShaderStatus), EcsOnInstantiate, EcsDontInherit);
    ecs_add_pair(world, ecs_id(Shader), EcsWith, ecs_id(ShaderImpl));
    ecs_add_pair(world, ecs_id(Shader), EcsWith, ecs_id(ShaderStatus));

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Shader) }},
        .events = { EcsOnSet },
        .callback = box_shader_on_set
    });

    box_shader_builtin_init(world);
}
