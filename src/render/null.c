/**
 * @file render/null.c
 * @brief Null backend that renders nothing and records the packets it receives.
 *
 * It keeps the last description of each texture graph and node kind, for tests.
 */

#include "render.h"

/** Remember the thread of the backend and count the call.
 *
 * @param ctx The null backend
 * @return 0
 */
static int box_render_null_init(
    void *ctx)
{
    box_render_null_t *null = ctx;
    null->inits ++;
    null->thread = ecs_os_thread_self();

    return 0;
}

/** Count a call that was not made on the thread that ran init.
 *
 * @param null The null backend
 */
static void box_render_null_check_thread(
    box_render_null_t *null)
{
    if (ecs_os_thread_self() != null->thread) {
        null->wrong_thread ++;
    }
}

/** Replace the copy of a description in a map of the null backend, or drop it.
 *
 * @param map The map, of handle to a copy
 * @param handle The handle
 * @param data The description, NULL to drop the copy
 * @param size Bytes of the description
 */
static void box_render_null_keep(
    ecs_map_t *map,
    uint32_t handle,
    const void *data,
    uint32_t size)
{
    void *old = ecs_map_get_deref(map, void, handle);
    ecs_os_free(old);
    if (data && size) {
        *ecs_map_ensure(map, handle) = (ecs_map_val_t)(uintptr_t)ecs_os_memdup(data,
            (ecs_size_t)size);
    } else if (old) {
        ecs_map_remove(map, handle);
    }
}

/** Record the texture graphs and node kinds of a packet.
 *
 * @param null The null backend
 * @param packet The packet
 */
static void box_render_null_graphs(
    box_render_null_t *null,
    const box_render_packet_t *packet)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&packet->resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &packet->resources, box_render_resource_t, i);
        const void *data = r->op != BOX_RENDER_DESTROY && r->size
            ? box_render_packet_data(packet, r->data)
            : NULL;
        if (r->kind == BOX_RENDER_TEXTURE_GRAPH && r->op == BOX_RENDER_DESTROY) {
            null->texture_graph_destroys ++;
            box_render_null_keep(&null->graphs, r->handle, NULL, 0);
        } else if (r->kind == BOX_RENDER_TEXTURE_GRAPH &&
            r->size >= sizeof(box_render_texture_graph_t))
        {
            null->texture_graphs ++;
            box_render_null_keep(&null->graphs, r->handle, data, r->size);
        } else if (r->kind == BOX_RENDER_TEXTURE_NODE_KIND &&
            r->size >= sizeof(box_render_texture_node_kind_t))
        {
            null->texture_node_kinds ++;
            box_render_null_keep(&null->node_kinds, r->handle, data, r->size);
        }
    }
}

/** Free the copies of a map of the null backend and the map.
 *
 * @param map The map
 */
static void box_render_null_map_fini(
    ecs_map_t *map)
{
    ecs_map_iter_t it = ecs_map_iter(map);
    while (ecs_map_next(&it)) {
        ecs_os_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(map);
}

/** Record a packet and keep a copy of it; the reset before the copy shrinks a large data block
 * of the copy with the same hysteresis as the packets of the ring.
 *
 * @param ctx The null backend
 * @param packet The packet
 */
static void box_render_null_render(
    void *ctx,
    const box_render_packet_t *packet)
{
    box_render_null_t *null = ctx;
    int32_t i;
    box_render_null_check_thread(null);
    if (null->packets && packet->frame != null->frame + 1) {
        null->gaps ++;
    }

    null->packets ++;
    null->frame = packet->frame;
    null->time = packet->time;
    null->delta = packet->delta;
    null->views += ecs_vec_count(&packet->views);
    null->batches += ecs_vec_count(&packet->batches);
    null->uploads += ecs_vec_count(&packet->uploads);
    null->scatters += ecs_vec_count(&packet->scatters);
    null->resources += ecs_vec_count(&packet->resources);
    null->targets += ecs_vec_count(&packet->targets);
    null->transitions += ecs_vec_count(&packet->transitions);
    null->copies += ecs_vec_count(&packet->copies);
    for (i = 0; i < ecs_vec_count(&packet->uploads); i ++) {
        null->bytes += ecs_vec_get_t(&packet->uploads, box_render_upload_t, i)->size;
    }

    for (i = 0; i < ecs_vec_count(&packet->copies); i ++) {
        null->copy_bytes += ecs_vec_get_t(&packet->copies, box_render_copy_t, i)->size;
    }

    for (i = 0; i < ecs_vec_count(&packet->resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &packet->resources, box_render_resource_t, i);
        const box_render_buffer_desc_t *desc = r->size >= sizeof(box_render_buffer_desc_t)
            ? box_render_packet_data(packet, r->data)
            : NULL;
        if (r->kind == BOX_RENDER_BUFFER && r->op == BOX_RENDER_CREATE && desc &&
            (desc->usage & BOX_RENDER_BUFFER_KEEP))
        {
            null->kept ++;
        }
    }

    box_render_null_graphs(null, packet);
    box_render_packet_reset(&null->last);
    box_render_packet_copy(&null->last, packet);
}

/** Count the release of the backend.
 *
 * @param ctx The null backend
 */
static void box_render_null_fini(
    void *ctx)
{
    box_render_null_t *null = ctx;
    box_render_null_check_thread(null);
    null->finis ++;
}

box_render_null_t* box_render_null_new(void)
{
    box_render_null_t *result = ecs_os_calloc_t(box_render_null_t);
    box_render_packet_init(&result->last);
    ecs_map_init(&result->graphs, NULL);
    ecs_map_init(&result->node_kinds, NULL);

    return result;
}

void box_render_null_free(
    void *ctx)
{
    box_render_null_t *null = ctx;
    box_render_packet_fini(&null->last);
    box_render_null_map_fini(&null->graphs);
    box_render_null_map_fini(&null->node_kinds);
    ecs_os_free(null);
}

box_render_backend_t box_render_null_backend(
    box_render_null_t *null)
{
    box_render_backend_t result = {
        .ctx = null,
        .init = box_render_null_init,
        .render = box_render_null_render,
        .fini = box_render_null_fini
    };

    return result;
}

const box_render_texture_graph_node_t* box_render_texture_graph_nodes(
    const box_render_texture_graph_t *graph)
{
    return (const box_render_texture_graph_node_t*)(graph + 1);
}

const uint8_t* box_render_texture_graph_params(
    const box_render_texture_graph_t *graph)
{
    return (const uint8_t*)(box_render_texture_graph_nodes(graph) + graph->node_count);
}

const box_render_texture_graph_texture_t* box_render_texture_graph_textures(
    const box_render_texture_graph_t *graph)
{
    return (const box_render_texture_graph_texture_t*)(box_render_texture_graph_params(graph) +
        graph->param_size);
}

const char* box_render_texture_node_kind_function(
    const box_render_texture_node_kind_t *kind)
{
    return (const char*)(kind + 1);
}

const char* box_render_texture_node_kind_source(
    const box_render_texture_node_kind_t *kind)
{
    return box_render_texture_node_kind_function(kind) + kind->function_size;
}

const box_render_texture_graph_t* box_render_null_texture_graph(
    const box_render_null_t *null,
    uint32_t handle)
{
    return ecs_map_get_deref(&null->graphs, box_render_texture_graph_t, handle);
}

const box_render_texture_node_kind_t* box_render_null_texture_node_kind(
    const box_render_null_t *null,
    uint32_t id)
{
    return ecs_map_get_deref(&null->node_kinds, box_render_texture_node_kind_t, id);
}
