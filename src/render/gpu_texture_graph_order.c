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

void box_gpu_tg_track(
    box_gpu_texgraphs_t *tg,
    box_gpu_tg_graph_t *graph,
    box_gpu_tg_track_t track)
{
    ecs_vec_t *from = graph->track == BOX_GPU_TG_ACTIVE ? &tg->active
        : graph->track == BOX_GPU_TG_WATCHER ? &tg->watchers : NULL;
    ecs_vec_t *to = track == BOX_GPU_TG_ACTIVE ? &tg->active
        : track == BOX_GPU_TG_WATCHER ? &tg->watchers : NULL;
    if (graph->track == track) {
        goto done;
    }

    if (from) {
        *ecs_vec_get_t(from, box_gpu_tg_graph_t*, graph->slot) = NULL;
        tg->active_holes += from == &tg->active;
        tg->watcher_holes += from == &tg->watchers;
    }

    if (to) {
        graph->slot = ecs_vec_count(to);
        *ecs_vec_append_t(NULL, to, box_gpu_tg_graph_t*) = graph;
    }

    graph->track = track;
done:
    return;
}

/** Take the entries graphs left out of a list of the graph state, keeping the order of the
 * others.
 *
 * @param list The list
 * @param holes Entries of the list graphs left, reset
 */
static void box_gpu_tg_compact(
    ecs_vec_t *list,
    int32_t *holes)
{
    box_gpu_tg_graph_t **graphs = ecs_vec_first(list);
    int32_t i, kept = 0, count = ecs_vec_count(list);
    if (!*holes) {
        goto done;
    }

    for (i = 0; i < count; i ++) {
        if (graphs[i]) {
            graphs[i]->slot = kept;
            graphs[kept ++] = graphs[i];
        }
    }

    ecs_vec_set_count_t(NULL, list, box_gpu_tg_graph_t*, kept);
    *holes = 0;
done:
    return;
}

/** Order two graphs of a list by their priority, highest first, then by their place in the
 * list.
 *
 * @param a The first graph
 * @param b The second graph
 * @return Negative, zero or positive
 */
static int box_gpu_tg_priority_compare(
    const void *a,
    const void *b)
{
    const box_gpu_tg_graph_t *ga = *(box_gpu_tg_graph_t* const*)a;
    const box_gpu_tg_graph_t *gb = *(box_gpu_tg_graph_t* const*)b;
    float pa = ga->desc.priority, pb = gb->desc.priority;
    int result = (pa < pb) - (pa > pb);

    return result ? result : (ga->slot > gb->slot) - (ga->slot < gb->slot);
}

/** Order a list of the graph state by the priority of its graphs, highest first, keeping the
 * order of graphs of the same priority.
 *
 * @param list The list, without holes
 */
static void box_gpu_tg_prioritize(
    ecs_vec_t *list)
{
    box_gpu_tg_graph_t **graphs = ecs_vec_first(list);
    int32_t i, count = ecs_vec_count(list);
    bool sorted = true;
    for (i = 1; sorted && i < count; i ++) {
        sorted = graphs[i - 1]->desc.priority >= graphs[i]->desc.priority;
    }

    if (sorted) {
        goto done;
    }

    qsort(graphs, (size_t)count, sizeof(box_gpu_tg_graph_t*), box_gpu_tg_priority_compare);
    for (i = 0; i < count; i ++) {
        graphs[i]->slot = i;
    }

done:
    return;
}

void box_gpu_tg_activate(
    box_gpu_texgraphs_t *tg,
    box_gpu_tg_graph_t *graph)
{
    graph->steady = false;
    box_gpu_tg_track(tg, graph, BOX_GPU_TG_ACTIVE);
}

void box_gpu_tg_candidates(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    ecs_vec_t *order)
{
    int32_t i;
    bool watch = gpu->texture_serial != tg->serial;
    box_gpu_tg_compact(&tg->active, &tg->active_holes);
    box_gpu_tg_compact(&tg->watchers, &tg->watcher_holes);
    box_gpu_tg_prioritize(&tg->active);
    for (i = 0; i < ecs_vec_count(&tg->active); i ++) {
        box_gpu_tg_graph_t *graph = *ecs_vec_get_t(&tg->active, box_gpu_tg_graph_t*, i);
        watch = watch || !graph->steady;
        box_gpu_tg_order(tg, graph, order);
    }

    for (i = 0; watch && i < ecs_vec_count(&tg->watchers); i ++) {
        box_gpu_tg_order(tg, *ecs_vec_get_t(&tg->watchers, box_gpu_tg_graph_t*, i), order);
    }
}

void box_gpu_tg_settle(
    box_gpu_t *gpu,
    box_gpu_texgraphs_t *tg,
    box_gpu_tg_graph_t *graph,
    bool settled)
{
    bool live = graph->is_animated && !graph->invalid, samples = false;
    uint32_t k;
    (void)gpu;
    for (k = 0; k < graph->desc.texture_count && graph->textures; k ++) {
        uint32_t handle = graph->textures[k].handle;
        box_gpu_tg_graph_t *src = box_gpu_tg_source_graph(tg, handle);
        if (!handle || handle == graph->handle) {
            continue;
        }

        samples = true;
        live = live || (handle & BOX_RENDER_TEXTURE_TARGET) || (src && src->live);
    }

    settled = settled && !box_gpu_tg_node_reads(tg, graph);
    graph->live = live;
    graph->steady = live && settled;
    box_gpu_tg_track(tg, graph, live || !settled ? BOX_GPU_TG_ACTIVE
        : samples ? BOX_GPU_TG_WATCHER : BOX_GPU_TG_IDLE);
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
