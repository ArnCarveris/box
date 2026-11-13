/**
 * @file render/texture.c
 * @brief Sends the images box.texture decoded to the backend and picks the sampler anisotropy.
 *
 * A texture gets a backend handle with its first image; a removed texture releases it. The
 * pixels stay after they are in a packet, so a new backend gets every image in its first one.
 * A texture entity a surface renders into resolves to the image of the target of the surface.
 *
 * The compiled graph of a TextureGraph goes to the handle of its texture, after its node kinds.
 * The GPU backend evaluates it into the image of the handle and reports the outcome, which
 * writes Texture and TextureStatus.
 */

#include "render.h"
#include "../texture/texture.h"

/** State of the texture extract */
typedef struct box_render_textures_t {
    const void *backend;    /**< Context of the backend the textures were sent to */
    void (*render)(void*, const box_render_packet_t*); /**< Render callback of that backend */
    int32_t generation;         /**< Generation of that backend, see box_render_backend_t */
} box_render_textures_t;

/** Singleton with the texture entities surfaces render into */
typedef struct RenderTargetTextures {
    ecs_map_t *targets;     /**< Texture entity to target handle */
} RenderTargetTextures;

static ECS_COMPONENT_DECLARE(RenderTargetTextures);

/** Free the maps of the singletons.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_render_target_textures_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    RenderTargetTextures *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        if (values[i].targets) {
            ecs_map_fini(values[i].targets);
            ecs_os_free(values[i].targets);
        }
    }
}

/** Move the maps of the singletons.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_render_target_textures_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    RenderTargetTextures *d = dst;
    RenderTargetTextures *s = src;
    int32_t i;
    for (i = 0; i < count; i ++) {
        if (d[i].targets != s[i].targets) {
            box_render_target_textures_dtor(&d[i], 1, ti);
        }

        d[i].targets = s[i].targets;
        s[i].targets = NULL;
    }
}

/** Get the map of the texture entities surfaces render into.
 *
 * @param world The world
 * @return The map, NULL when box.render is not imported
 */
static ecs_map_t* box_render_target_textures(
    const ecs_world_t *world)
{
    const RenderTargetTextures *t = ecs_id(RenderTargetTextures)
        ? ecs_singleton_get(world, RenderTargetTextures)
        : NULL;

    return t ? t->targets : NULL;
}

void box_render_texture_targets_clear(
    ecs_world_t *world)
{
    ecs_map_t *map = box_render_target_textures(world);
    if (map) {
        ecs_map_clear(map);
    }
}

void box_render_texture_target(
    ecs_world_t *world,
    ecs_entity_t texture,
    uint32_t target)
{
    ecs_map_t *map = box_render_target_textures(world);
    if (map && texture && target) {
        *ecs_map_ensure(map, texture) = target;
    }
}

uint32_t box_render_texture_target_handle(
    const ecs_world_t *world,
    ecs_entity_t texture)
{
    ecs_map_t *map = box_render_target_textures(world);
    const ecs_map_val_t *target = map && texture ? ecs_map_get(map, texture) : NULL;

    return target ? BOX_RENDER_TEXTURE_TARGET | (uint32_t)*target : 0;
}

/** Get the anisotropic samples of the samplers: the most any surface asks for.
 *
 * @param world The world
 * @param t The registry
 * @return Samples from 1 to 16
 */
static uint32_t box_render_anisotropy(
    const ecs_world_t *world,
    const box_textures_t *t)
{
    uint32_t result = 0;
    int32_t i;
    ecs_iter_t it = ecs_query_iter(world, t->surfaces);
    while (ecs_query_next(&it)) {
        const Surface *s = ecs_field(&it, Surface, 0);
        for (i = 0; i < it.count; i ++) {
            uint32_t a = s[i].anisotropy == AnisotropyDefault
                ? BOX_RENDER_ANISOTROPY_DEFAULT
                : (uint32_t)s[i].anisotropy;
            result = a > result ? a : result;
        }
    }

    if (!result) {
        result = BOX_RENDER_ANISOTROPY_DEFAULT;
    }

    return result > 16 ? 16 : result;
}

/** Get the bytes of the image of a texture: four per pixel, one for a single channel image.
 *
 * @param tex The texture
 * @return Bytes of its pixels
 */
static uint32_t box_render_texture_size(
    const box_texture_t *tex)
{
    return (uint32_t)tex->width * (uint32_t)tex->height * (tex->single_channel ? 1u : 4u);
}

/** Give a texture a backend handle on first use, a released one when there is one.
 *
 * @param t The registry
 * @param tex The texture
 */
static void box_render_texture_handle_of(
    box_textures_t *t,
    box_texture_t *tex)
{
    if (!tex->handle && ecs_vec_count(&t->free_handles)) {
        tex->handle = *ecs_vec_last_t(&t->free_handles, uint32_t);
        ecs_vec_remove_last(&t->free_handles);
    } else if (!tex->handle) {
        tex->handle = t->next_handle ++;
    }
}

/** Send the node kinds the backend does not have.
 *
 * @param p The packet
 * @param t The registry
 */
static void box_render_texture_kinds_send(
    box_render_packet_t *p,
    box_textures_t *t)
{
    int32_t id, i, count = ecs_vec_count(&t->node_kinds);
    for (id = 1; id <= count; id ++) {
        box_texture_node_kind_t *kind = box_texture_node_kind(t, id);
        if (kind->sent) {
            continue;
        }

        uint32_t function_size = (uint32_t)ecs_os_strlen(kind->function) + 1;
        uint32_t source_size = (uint32_t)ecs_os_strlen(kind->source) + 1;
        box_render_texture_node_kind_t *desc = box_render_packet_resource(p,
            BOX_RENDER_TEXTURE_NODE_KIND, BOX_RENDER_CREATE, (uint32_t)id,
            (uint32_t)sizeof(box_render_texture_node_kind_t) + function_size + source_size);
        ecs_os_memset_t(desc, 0, box_render_texture_node_kind_t);
        desc->output = (uint32_t)kind->output;
        desc->input_count = (uint32_t)kind->input_count;
        for (i = 0; i < kind->input_count; i ++) {
            desc->input_types[i] = (uint32_t)kind->inputs[i].type;
            if (kind->inputs[i].coords && !kind->inputs[i].value) {
                desc->coords_inputs |= 1u << i;
            }
        }

        desc->param_size = (uint32_t)kind->param_size;
        desc->flags = kind->flags;
        desc->passes = (uint32_t)kind->passes;
        desc->function_size = function_size;
        desc->source_size = source_size;
        ecs_os_memcpy((char*)(desc + 1), kind->function, (ecs_size_t)function_size);
        ecs_os_memcpy((char*)(desc + 1) + function_size, kind->source,
            (ecs_size_t)source_size);
        kind->sent = true;
    }
}

/** Send the compiled graph of a texture, which the backend evaluates into its image.
 *
 * @param p The packet
 * @param t The registry
 * @param tex The texture
 */
static void box_render_texture_graph_send(
    box_render_packet_t *p,
    box_textures_t *t,
    box_texture_t *tex)
{
    const box_texture_graph_t *graph = tex->graph;
    int32_t i, j;
    uint32_t nodes = (uint32_t)graph->node_count * (uint32_t)sizeof(
        box_render_texture_graph_node_t);
    box_render_texture_handle_of(t, tex);
    box_render_texture_graph_t *desc = box_render_packet_resource(p,
        BOX_RENDER_TEXTURE_GRAPH, BOX_RENDER_CREATE, tex->handle,
        (uint32_t)sizeof(box_render_texture_graph_t) + nodes + (uint32_t)graph->param_size);
    desc->width = (uint32_t)graph->width;
    desc->height = (uint32_t)graph->height;
    desc->domain = (uint32_t)graph->domain;
    desc->format = (uint32_t)graph->format;
    desc->node_count = (uint32_t)graph->node_count;
    desc->param_size = (uint32_t)graph->param_size;
    desc->structure_hash = graph->structure_hash;
    desc->param_hash = graph->param_hash;
    desc->serial = tex->graph_serial;
    desc->rate = graph->rate;
    box_render_texture_graph_node_t *out = (box_render_texture_graph_node_t*)(desc + 1);
    for (i = 0; i < graph->node_count; i ++) {
        const box_texture_graph_node_t *node = &graph->nodes[i];
        out[i].kind = (uint32_t)node->kind;
        out[i].output = (uint32_t)node->output;
        for (j = 0; j < BOX_TEXTURE_NODE_INPUTS; j ++) {
            out[i].inputs[j] = node->inputs[j];
        }

        out[i].params = (uint32_t)node->params;
        out[i].param_size = (uint32_t)node->param_size;
    }

    if (graph->param_size) {
        ecs_os_memcpy((uint8_t*)(out + graph->node_count), graph->params, graph->param_size);
    }

    tex->graph_sent = true;
}

/** Destroy the graphs of removed textures, before their images are destroyed.
 *
 * @param p The packet
 * @param t The registry
 */
static void box_render_texture_graphs_release(
    box_render_packet_t *p,
    box_textures_t *t)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&t->graph_released); i ++) {
        uint32_t handle = *ecs_vec_get_t(&t->graph_released, uint32_t, i);
        box_render_packet_resource(p, BOX_RENDER_TEXTURE_GRAPH, BOX_RENDER_DESTROY, handle, 0);
    }

    ecs_vec_clear(&t->graph_released);
}

/** Send the queued graphs, after the node kinds the backend does not have. Without a graph
 * to send no kind is sent, so a world without graphs sends nothing.
 *
 * @param p The packet
 * @param t The registry
 */
static void box_render_texture_graphs_send(
    box_render_packet_t *p,
    box_textures_t *t)
{
    int32_t i, count = ecs_vec_count(&t->graph_queued);
    ecs_entity_t *queued = ecs_vec_first(&t->graph_queued);
    if (count) {
        box_render_texture_kinds_send(p, t);
    }

    for (i = 0; i < count; i ++) {
        box_texture_t *tex = ecs_map_get_deref(&t->textures, box_texture_t, queued[i]);
        if (tex && tex->graph_queued && tex->graph) {
            box_render_texture_graph_send(p, t, tex);
        }

        if (tex) {
            tex->graph_queued = false;
        }
    }

    ecs_vec_clear(&t->graph_queued);
}

/** Find the texture whose graph went to the backend under a handle.
 *
 * @param t The registry
 * @param handle The texture handle
 * @return The texture, NULL when no texture with a graph has the handle
 */
static box_texture_t* box_render_texture_graph_of(
    box_textures_t *t,
    uint32_t handle)
{
    box_texture_t *result = NULL;
    ecs_map_iter_t it = ecs_map_iter(&t->textures);
    while (handle && !result && ecs_map_next(&it)) {
        box_texture_t *tex = ecs_map_ptr(&it);
        if (tex->handle == handle && tex->graph) {
            result = tex;
        }
    }

    return result;
}

/** Apply an outcome of the GPU backend to the texture of its graph, unless a newer graph was
 * taken since: the first evaluation writes Texture and makes the texture ready with the next
 * revision, a failure fails it with the error and the node it came from.
 *
 * @param world The world or a stage
 * @param t The registry
 * @param r The outcome
 */
static void box_render_texture_graph_outcome(
    ecs_world_t *world,
    box_textures_t *t,
    const box_render_texture_graph_result_t *r)
{
    box_texture_t *tex = box_render_texture_graph_of(t, r->handle);
    int32_t revision;
    if (!tex || tex->graph_serial != r->serial || !ecs_is_alive(world, tex->entity)) {
        goto done;
    }

    tex->graph_pending = false;
    revision = box_texture_revision(world, tex->entity);
    if (r->error) {
        char *name = ecs_get_path(world, tex->entity);
        const box_texture_graph_t *graph = tex->graph;
        ecs_entity_t node = r->node >= 0 && r->node < graph->node_count
            ? graph->nodes[r->node].entity : 0;
        char *node_name = node && ecs_is_alive(world, node) ? ecs_get_path(world, node) : NULL;
        char *error = node_name
            ? flecs_asprintf("texture graph '%s': node '%s': %s", name, node_name, r->error)
            : flecs_asprintf("texture graph '%s': %s", name, r->error);
        ecs_err("%s", error);
        box_texture_status(world, tex->entity, TextureFailed, revision, error);
        ecs_os_free(error);
        ecs_os_free(node_name);
        ecs_os_free(name);
        goto done;
    }

    tex->sent = true;
    tex->revision ++;
    ecs_set(world, tex->entity, Texture, {
        .width = tex->graph->width, .height = tex->graph->height });
    box_texture_status(world, tex->entity, TextureReady, revision + 1, NULL);
done:
    return;
}

/** Apply the outcomes of the graphs the GPU backend evaluated or failed.
 *
 * @param world The world or a stage
 * @param t The registry
 */
static void box_render_texture_graph_outcomes(
    ecs_world_t *world,
    box_textures_t *t)
{
    ecs_vec_t results;
    int32_t i;
    ecs_vec_init_t(NULL, &results, box_render_texture_graph_result_t, 0);
    box_render_gpu_texture_graph_results(world, &results);
    for (i = 0; i < ecs_vec_count(&results); i ++) {
        box_render_texture_graph_result_t *r = ecs_vec_get_t(
            &results, box_render_texture_graph_result_t, i);
        box_render_texture_graph_outcome(world, t, r);
        ecs_os_free(r->error);
    }

    ecs_vec_fini_t(NULL, &results, box_render_texture_graph_result_t);
}

bool box_render_texture_graph_read(
    ecs_world_t *world,
    ecs_entity_t texture,
    ecs_entity_t node,
    uint32_t ticket)
{
    box_textures_t *t = box_textures_get(world);
    const box_texture_t *tex = t && texture
        ? ecs_map_get_deref(&t->textures, box_texture_t, texture)
        : NULL;
    int32_t i, index = -1;
    bool result = false;
    if (!tex || !tex->graph || !tex->handle || !tex->graph_sent) {
        goto done;
    }

    for (i = 0; node && i < tex->graph->node_count && index < 0; i ++) {
        if (tex->graph->nodes[i].entity == node) {
            index = i;
        }
    }

    if (node && index < 0) {
        goto done;
    }

    result = box_render_gpu_texture_graph_read(world, tex->handle, index, ticket);
done:
    return result;
}

bool box_render_texture_graph_reads(
    ecs_world_t *world,
    ecs_vec_t *out)
{
    int32_t first = ecs_vec_count(out);
    bool result;
    box_render_texture_previews_kept(world, out);
    result = box_render_texture_graph_reads_take(world, out);
    box_render_texture_previews_filter(world, out, first);

    return result;
}

bool box_render_texture_graph_reads_take(
    ecs_world_t *world,
    ecs_vec_t *out)
{
    box_textures_t *t = box_textures_get(world);
    int32_t i, first = ecs_vec_count(out);
    bool result = box_render_gpu_texture_graph_reads(world, out);
    for (i = first; t && i < ecs_vec_count(out); i ++) {
        box_render_texture_read_t *r = ecs_vec_get_t(out, box_render_texture_read_t, i);
        const box_texture_t *tex = box_render_texture_graph_of(t, r->handle);
        r->texture = tex ? tex->entity : 0;
        r->node = tex && r->index >= 0 && r->index < tex->graph->node_count
            ? tex->graph->nodes[r->index].entity : 0;
    }

    return result;
}

/** Queue every graph and node kind for a restarted backend.
 *
 * @param t The registry
 */
static void box_render_texture_graphs_requeue(
    box_textures_t *t)
{
    int32_t id, count = ecs_vec_count(&t->node_kinds);
    for (id = 1; id <= count; id ++) {
        box_texture_node_kind(t, id)->sent = false;
    }

    ecs_vec_clear(&t->graph_released);
    ecs_map_iter_t it = ecs_map_iter(&t->textures);
    while (ecs_map_next(&it)) {
        box_texture_t *tex = ecs_map_ptr(&it);
        if (tex->graph && !tex->graph_queued) {
            tex->graph_queued = true;
            *ecs_vec_append_t(NULL, &t->graph_queued, ecs_entity_t) = tex->entity;
        }

        tex->graph_sent = false;
    }
}

/** Send the image of a texture.
 *
 * @param p The packet
 * @param t The registry
 * @param tex The texture
 * @return Bytes of the image
 */
static uint32_t box_render_texture_send(
    box_render_packet_t *p,
    box_textures_t *t,
    box_texture_t *tex)
{
    uint32_t size = box_render_texture_size(tex);
    box_render_texture_handle_of(t, tex);
    box_render_texture_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_TEXTURE,
        BOX_RENDER_CREATE, tex->handle, (uint32_t)sizeof(box_render_texture_desc_t) + size);
    desc->width = (uint32_t)tex->width;
    desc->height = (uint32_t)tex->height;
    desc->single_channel = tex->single_channel;
    ecs_os_memcpy(desc + 1, tex->pixels, (ecs_size_t)size);
    tex->sent = true;
    tex->revision ++;

    return size;
}

/** Send the queued images, at most a budget of bytes per packet unless it is the first one
 * or every image goes to a restarted backend.
 *
 * @param p The packet
 * @param t The registry
 * @param all Whether to send every queued image
 */
static void box_render_textures_send(
    box_render_packet_t *p,
    box_textures_t *t,
    bool all)
{
    int32_t i, kept = 0, count = ecs_vec_count(&t->queued);
    ecs_entity_t *queued = ecs_vec_first(&t->queued);
    uint64_t bytes = 0;
    for (i = 0; i < count; i ++) {
        box_texture_t *tex = ecs_map_get_deref(&t->textures, box_texture_t, queued[i]);
        if (!tex || !tex->pixels) {
            if (tex) {
                tex->queued = false;
            }

            continue;
        }

        if (!all && bytes && bytes + box_render_texture_size(tex) > BOX_RENDER_TEXTURE_BUDGET)
        {
            queued[kept ++] = queued[i];
            continue;
        }

        bytes += box_render_texture_send(p, t, tex);
        tex->queued = false;
    }

    ecs_vec_set_count_t(NULL, &t->queued, ecs_entity_t, kept);
}

/** Queue every image the previous backend had, from the copy the registry keeps, so the
 * first packet to a restarted backend has all of them.
 *
 * @param t The registry
 */
static void box_render_textures_requeue(
    box_textures_t *t)
{
    ecs_map_iter_t it = ecs_map_iter(&t->textures);
    while (ecs_map_next(&it)) {
        box_texture_t *tex = ecs_map_ptr(&it);
        if (tex->sent && tex->pixels && !tex->queued) {
            tex->queued = true;
            *ecs_vec_append_t(NULL, &t->queued, ecs_entity_t) = tex->entity;
        }

        tex->sent = false;
    }
}

/** Release removed textures, send loaded images and set the sampler anisotropy.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractTextures(
    ecs_iter_t *it)
{
    box_render_textures_t *state = it->ctx;
    ecs_world_t *world = it->world;
    box_textures_t *t = box_textures_get(world);
    box_render_packet_t *p = box_render_packet(world);
    const box_render_backend_t *backend = box_render_get_backend(world);
    bool restart = false;
    int32_t i;
    p->anisotropy = BOX_RENDER_ANISOTROPY_DEFAULT;
    if (!t) {
        goto done;
    }

    if (backend->ctx != state->backend || backend->render != state->render ||
        backend->generation != state->generation)
    {
        if (state->backend || state->render) {
            for (i = 0; i < ecs_vec_count(&t->released); i ++) {
                *ecs_vec_append_t(NULL, &t->free_handles, uint32_t) =
                    *ecs_vec_get_t(&t->released, uint32_t, i);
            }

            ecs_vec_clear(&t->released);
            box_render_textures_requeue(t);
            box_render_texture_graphs_requeue(t);
            restart = true;
        }

        state->backend = backend->ctx;
        state->render = backend->render;
        state->generation = backend->generation;
    }

    box_render_texture_graph_outcomes(it->stage, t);
    box_render_texture_graphs_release(p, t);
    for (i = 0; i < ecs_vec_count(&t->released); i ++) {
        uint32_t handle = *ecs_vec_get_t(&t->released, uint32_t, i);
        box_render_packet_resource(p, BOX_RENDER_TEXTURE, BOX_RENDER_DESTROY, handle, 0);
        *ecs_vec_append_t(NULL, &t->free_handles, uint32_t) = handle;
    }

    ecs_vec_clear(&t->released);
    box_render_textures_send(p, t, restart);
    box_render_texture_graphs_send(p, t);
    const TextureGraphCache *cache = ecs_singleton_get(world, TextureGraphCache);
    p->texture_graph_budget = cache ? cache->budget : 0;
    p->anisotropy = box_render_anisotropy(world, t);
done:
    return;
}

void box_render_textures_init(
    ecs_world_t *world)
{
    box_render_textures_t *state = ecs_os_calloc_t(box_render_textures_t);
    ecs_map_t *targets = ecs_os_calloc_t(ecs_map_t);
    ECS_COMPONENT_DEFINE(world, RenderTargetTextures);
    ecs_set_hooks(world, RenderTargetTextures, {
        .ctor = flecs_default_ctor,
        .dtor = box_render_target_textures_dtor,
        .move = box_render_target_textures_move
    });
    ecs_map_init(targets, NULL);
    ecs_singleton_set(world, RenderTargetTextures, { .targets = targets });
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderExtractTextures" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractTextures,
        .ctx = state,
        .ctx_free = ecs_os_api.free_
    });
}
