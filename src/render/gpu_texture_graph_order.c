/**
 * @file render/gpu_texture_graph_order.c
 * @brief Order of the texture graphs of the webgpu backend that sample the images of others.
 *
 * Graphs that sample the images of other graphs evaluate after them in one packet, and again
 * when an image they sample changed; the values after a sampling node are cached under the
 * serial of the image, so a new image runs again only the stages after the sampling nodes.
 */

#include "gpu_texture_graph.h"

#ifdef BOX_RENDER_WGPU

box_gpu_tg_graph_t* box_gpu_tg_source_graph(
    box_gpu_texgraphs_t *tg,
    uint32_t handle)
{
    return handle && !(handle & BOX_RENDER_TEXTURE_TARGET)
        ? ecs_map_get_deref(&tg->graphs, box_gpu_tg_graph_t, handle)
        : NULL;
}

uint64_t box_gpu_tg_source_serial(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    const box_gpu_tg_graph_t *graph,
    int32_t index)
{
    uint32_t handle = graph->textures[index].handle;
    uint64_t result = 0;
    if (!handle || handle == graph->handle) {
        goto done;
    }

    if (handle & BOX_RENDER_TEXTURE_TARGET) {
        result = box_gpu_texture_sampled_view(gpu, handle, false)
            ? (1ull << 63) | (uint64_t)tg->frame : 0;
    } else {
        result = box_gpu_texture_serial(gpu, handle);
    }

done:
    return result;
}

void box_gpu_tg_order(
    box_gpu_texgraphs_t *tg,
    box_gpu_tg_graph_t *graph,
    ecs_vec_t *order)
{
    uint32_t k;
    if (graph->mark == tg->frame) {
        goto done;
    }

    graph->mark = tg->frame;
    graph->eval = -1;
    for (k = 0; k < graph->desc.texture_count && graph->textures; k ++) {
        box_gpu_tg_graph_t *src = box_gpu_tg_source_graph(tg, graph->textures[k].handle);
        if (src && src != graph) {
            box_gpu_tg_order(tg, src, order);
        }
    }

    *ecs_vec_append_t(NULL, order, box_gpu_tg_graph_t*) = graph;
done:
    return;
}

void box_gpu_tg_propagate(
    box_gpu_texgraphs_t *tg,
    box_gpu_tg_graph_t *graph)
{
    uint32_t k;
    for (k = 0; k < graph->desc.texture_count && graph->textures; k ++) {
        box_gpu_tg_graph_t *src = box_gpu_tg_source_graph(tg, graph->textures[k].handle);
        if (src && src != graph && graph->sampled > src->sampled) {
            src->sampled = graph->sampled;
        }
    }
}

bool box_gpu_tg_sources_changed(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    const box_gpu_tg_graph_t *graph)
{
    bool result = false;
    uint32_t k;
    for (k = 0; !result && k < graph->desc.texture_count && graph->textures; k ++) {
        uint32_t handle = graph->textures[k].handle;
        box_gpu_tg_graph_t *src = box_gpu_tg_source_graph(tg, handle);
        if (!handle || handle == graph->handle) {
            continue;
        }

        if (src && src->eval >= 0) {
            result = true;
        } else if (handle & BOX_RENDER_TEXTURE_TARGET) {
            result = graph->sampled >= tg->frame - 1 &&
                box_gpu_tg_source_serial(gpu, tg, graph, (int32_t)k) != graph->bound[k];
        } else {
            result = box_gpu_tg_source_serial(gpu, tg, graph, (int32_t)k) != graph->bound[k];
        }
    }

    return result;
}

int32_t box_gpu_tg_eval_passes(
    box_gpu_texgraphs_t *tg,
    box_gpu_tg_eval_t *evals,
    int32_t count)
{
    int32_t i, pass = 0;
    uint32_t k;
    for (i = 0; i < count; i ++) {
        const box_gpu_tg_graph_t *graph = evals[i].graph;
        bool split = false;
        for (k = 0; k < graph->desc.texture_count && graph->textures; k ++) {
            box_gpu_tg_graph_t *src = box_gpu_tg_source_graph(tg, graph->textures[k].handle);
            split = split || (src && src != graph && src->eval >= 0 && src->eval < i &&
                evals[src->eval].pass == pass && src->desc.format == GraphSingle);
        }

        pass += split;
        evals[i].pass = pass;
    }

    return count ? pass + 1 : 0;
}

#endif
