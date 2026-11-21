/**
 * @file render/gpu_cull_glass.c
 * @brief Whether the cull pass of a view kept any of its transmissive instances, read back with
 * its counters, so a view that shows none of them takes no snapshot of its image.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** Frames the counts of a view stay known for */
#define BOX_GPU_CULL_GLASS_FRAMES (30)

/** Count the transmissive instances a cull view tests and list the arguments of their draws.
 *
 * @param c The cull state
 * @param packet The packet
 * @param cv The cull view
 * @param args Receives the draw arguments, NULL to only count
 * @param draws Receives the draws listed
 * @return The instances
 */
static int32_t box_gpu_cull_glass_tested(
    const box_gpu_cull_t *c,
    const box_render_packet_t *packet,
    const box_gpu_cull_view_t *cv,
    ecs_vec_t *args,
    int32_t *draws)
{
    int32_t d, result = 0;
    *draws = 0;
    for (d = cv->first_draw; d < cv->first_draw + cv->draw_count; d ++) {
        const box_gpu_cull_draw_t *draw = ecs_vec_get_t(&c->draws, box_gpu_cull_draw_t, d);
        const box_render_batch_t *b = ecs_vec_get_t(
            &packet->batches, box_render_batch_t, draw->batch);
        if (!b->transmissive || draw->skip || draw->args == UINT32_MAX) {
            continue;
        }

        result += (int32_t)draw->count;
        (*draws) ++;
        if (args) {
            *ecs_vec_append_t(NULL, args, uint32_t) = draw->args;
        }
    }

    return result;
}

int32_t box_gpu_cull_glass_list(
    const box_gpu_cull_t *c,
    const box_render_packet_t *packet,
    box_gpu_cull_readback_t *r)
{
    int32_t i, draws, result = 0, count = ecs_vec_count(&c->views);
    if (r) {
        ecs_vec_clear(&r->glass);
        ecs_vec_clear(&r->glass_args);
    }

    for (i = 0; i < count; i ++) {
        const box_gpu_cull_view_t *cv = ecs_vec_get_t(&c->views, box_gpu_cull_view_t, i);
        if (!cv->report || cv->cascade >= 0) {
            continue;
        }

        int32_t first = r ? ecs_vec_count(&r->glass_args) : 0;
        int32_t tested = box_gpu_cull_glass_tested(c, packet, cv, r ? &r->glass_args : NULL,
            &draws);
        result += draws;
        if (r && draws) {
            *ecs_vec_append_t(NULL, &r->glass, box_gpu_cull_glass_t) = (box_gpu_cull_glass_t){
                .view = cv->view,
                .frame = c->frame,
                .tested = tested,
                .first = first,
                .count = draws
            };
        }
    }

    return result;
}

void box_gpu_cull_glass_publish(
    box_gpu_cull_t *c,
    const box_gpu_cull_readback_t *r,
    const uint32_t *args)
{
    const uint32_t *at = ecs_vec_first(&r->glass_args);
    int32_t i, j, count = ecs_vec_count(&r->glass);
    for (i = 0; i < count; i ++) {
        box_gpu_cull_glass_t g = *ecs_vec_get_t(&r->glass, box_gpu_cull_glass_t, i);
        box_gpu_cull_glass_t *last = NULL;
        for (j = 0; j < g.count; j ++) {
            g.visible += (int32_t)args[at[g.first + j] * BOX_GPU_CULL_ARGS + 1];
        }

        for (j = 0; j < ecs_vec_count(&c->glass); j ++) {
            last = ecs_vec_get_t(&c->glass, box_gpu_cull_glass_t, j);
            if (last->view == g.view) {
                break;
            }

            last = NULL;
        }

        if (!last) {
            last = ecs_vec_append_t(NULL, &c->glass, box_gpu_cull_glass_t);
            last->view = g.view;
            last->frame = -1;
        }

        if (last->frame < g.frame) {
            *last = g;
        }
    }
}

bool box_gpu_cull_glass_hidden(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    int32_t slot)
{
    box_gpu_cull_t *c = &gpu->cull;
    const box_gpu_cull_view_t *cv = box_gpu_cull_view(gpu, slot, -1);
    bool result = false;
    int32_t i, draws, tested;
    if (!cv || !cv->report) {
        goto done;
    }

    tested = box_gpu_cull_glass_tested(c, packet, cv, NULL, &draws);
    for (i = 0; draws && i < ecs_vec_count(&c->glass); i ++) {
        const box_gpu_cull_glass_t *g = ecs_vec_get_t(&c->glass, box_gpu_cull_glass_t, i);
        if (g->view == cv->view) {
            result = !g->visible && g->tested == tested &&
                c->frame - g->frame <= BOX_GPU_CULL_GLASS_FRAMES;
            break;
        }
    }

done:
    return result;
}

#endif
