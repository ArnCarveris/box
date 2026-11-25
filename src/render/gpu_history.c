/**
 * @file render/gpu_history.c
 * @brief Temporal foundation of the webgpu backend: the history of every view in a target, which
 * the View uniforms carry, and the ping-pong history textures of the temporal passes.
 *
 * A pair is keyed by target, view, consumer kind and consumer entity. Its read texture is valid
 * when it holds the previous frame the view rendered, in the same size and format, and the
 * history of the view is valid.
 *
 * So a reset of the view (HistoryReset, a resize, a new camera or projection) throws the history
 * of every pass of the view away.
 */

#include "gpu.h"

#ifdef BOX_RENDER_WGPU

/** A ping-pong pair of history textures of a temporal pass */
typedef struct box_gpu_history_pair_t {
    box_gpu_history_desc_t desc;    /**< Key and shape, with the usage resolved */
    WGPUTexture textures[2];        /**< The two textures */
    WGPUTextureView views[2];       /**< Their views */
    int64_t written[2];             /**< Packet frame whose image each texture holds, -1 none */
    int32_t write;                  /**< Texture the current frame writes */
    bool valid;                     /**< The read texture held the previous frame of the view */
    int64_t frame;                  /**< Packet frame the pair was last used in */
} box_gpu_history_pair_t;

void box_gpu_history_init(
    box_gpu_t *gpu)
{
    ecs_vec_init_t(NULL, &gpu->histories, box_render_history_t, 0);
    ecs_vec_init_t(NULL, &gpu->history_pairs, box_gpu_history_pair_t*, 0);
}

/** Release the textures of a pair.
 *
 * @param p The pair
 */
static void box_gpu_history_pair_release(
    box_gpu_history_pair_t *p)
{
    int32_t i;
    for (i = 0; i < 2; i ++) {
        if (p->views[i]) {
            wgpuTextureViewRelease(p->views[i]);
            p->views[i] = NULL;
        }

        if (p->textures[i]) {
            wgpuTextureRelease(p->textures[i]);
            p->textures[i] = NULL;
        }

        p->written[i] = -1;
    }
}

void box_gpu_history_fini(
    box_gpu_t *gpu)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&gpu->history_pairs); i ++) {
        box_gpu_history_pair_t *p = *ecs_vec_get_t(
            &gpu->history_pairs, box_gpu_history_pair_t*, i);
        box_gpu_history_pair_release(p);
        ecs_os_free(p);
    }

    ecs_vec_fini_t(NULL, &gpu->history_pairs, box_gpu_history_pair_t*);
    ecs_vec_fini_t(NULL, &gpu->histories, box_render_history_t);
}

void box_gpu_history_uniforms(
    box_gpu_t *gpu,
    const box_gpu_target_t *t,
    const box_render_packet_t *packet,
    const box_render_view_t *v,
    const mat4 proj,
    box_gpu_view_uniforms_t *u)
{
    const box_render_temporal_t *h = box_render_history_update(&gpu->histories, t->handle, v,
        packet->origin, packet->frame, t->desc.render_width, t->desc.render_height, proj);
    glm_mat4_copy(ECS_CONST_CAST(vec4*, h->prev_view_proj), u->prev_view_proj);
    u->temporal[0] = h->frame_index;
    u->temporal[1] = h->valid ? 1u : 0u;
    u->temporal[2] = 0;
    u->temporal[3] = 0;
}

const box_render_temporal_t* box_gpu_history_view(
    box_gpu_t *gpu,
    uint32_t target,
    ecs_entity_t view)
{
    const box_render_history_t *h = target
        ? box_render_history_find(&gpu->histories, target, view)
        : NULL;

    return h ? &h->current : NULL;
}

/** Find the pair of a key.
 *
 * @param gpu The backend
 * @param desc The key
 * @return The pair, NULL for none
 */
static box_gpu_history_pair_t* box_gpu_history_find(
    box_gpu_t *gpu,
    const box_gpu_history_desc_t *desc)
{
    box_gpu_history_pair_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&gpu->history_pairs) && !result; i ++) {
        box_gpu_history_pair_t *p = *ecs_vec_get_t(
            &gpu->history_pairs, box_gpu_history_pair_t*, i);
        if (p->desc.target == desc->target && p->desc.view == desc->view &&
            p->desc.consumer == desc->consumer && p->desc.entity == desc->entity)
        {
            result = p;
        }
    }

    return result;
}

/** Resolve the shape of a request: its usage and a depth of 1 for 2D.
 *
 * @param desc The request
 * @param out Receives the resolved key and shape
 */
static void box_gpu_history_resolve(
    const box_gpu_history_desc_t *desc,
    box_gpu_history_desc_t *out)
{
    bool volume = desc->dimension == WGPUTextureDimension_3D;
    *out = *desc;
    out->dimension = volume ? WGPUTextureDimension_3D : WGPUTextureDimension_2D;
    out->depth = volume && desc->depth ? desc->depth : 1;
    if (!out->usage) {
        out->usage = WGPUTextureUsage_TextureBinding | (volume
            ? WGPUTextureUsage_StorageBinding
            : WGPUTextureUsage_RenderAttachment);
    }
}

/** Create the textures of a pair for its shape.
 *
 * @param gpu The backend
 * @param p The pair, without textures
 */
static void box_gpu_history_pair_create(
    box_gpu_t *gpu,
    box_gpu_history_pair_t *p)
{
    int32_t i;
    WGPUTextureDescriptor texture = {
        .usage = p->desc.usage,
        .dimension = p->desc.dimension,
        .size = { p->desc.width, p->desc.height, p->desc.depth },
        .format = p->desc.format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    for (i = 0; i < 2; i ++) {
        p->textures[i] = wgpuDeviceCreateTexture(gpu->device, &texture);
        p->views[i] = wgpuTextureCreateView(p->textures[i], NULL);
        p->written[i] = -1;
    }

    p->write = 1;
}

bool box_gpu_history_acquire(
    box_gpu_t *gpu,
    const box_render_packet_t *packet,
    const box_gpu_history_desc_t *desc,
    box_gpu_history_t *out)
{
    box_gpu_history_desc_t shape;
    box_gpu_history_pair_t *p = NULL;
    const box_render_temporal_t *view = NULL;
    bool result = desc->width > 0 && desc->height > 0;
    ecs_os_memset_t(out, 0, box_gpu_history_t);
    if (!result) {
        goto done;
    }

    box_gpu_history_resolve(desc, &shape);
    p = box_gpu_history_find(gpu, &shape);
    if (!p) {
        p = ecs_os_calloc_t(box_gpu_history_pair_t);
        *ecs_vec_append_t(NULL, &gpu->history_pairs, box_gpu_history_pair_t*) = p;
    } else if (p->frame == packet->frame) {
        goto found;
    }

    if (!p->textures[0] || p->desc.width != shape.width || p->desc.height != shape.height ||
        p->desc.depth != shape.depth || p->desc.format != shape.format ||
        p->desc.dimension != shape.dimension || p->desc.usage != shape.usage)
    {
        box_gpu_history_pair_release(p);
        p->desc = shape;
        box_gpu_history_pair_create(gpu, p);
    }

    view = box_gpu_history_view(gpu, shape.target, shape.view);
    p->write ^= 1;
    p->valid = view && view->valid && view->prev_frame >= 0 &&
        p->written[p->write ^ 1] == view->prev_frame;
    p->written[p->write] = packet->frame;
    p->frame = packet->frame;
found:
    out->read_texture = p->textures[p->write ^ 1];
    out->read = p->views[p->write ^ 1];
    out->write_texture = p->textures[p->write];
    out->write = p->views[p->write];
    out->valid = p->valid;
done:
    return result;
}

void box_gpu_history_discard(
    box_gpu_t *gpu,
    const box_gpu_history_desc_t *desc)
{
    box_gpu_history_pair_t *p = box_gpu_history_find(gpu, desc);
    if (p) {
        p->written[p->write] = -1;
    }
}

void box_gpu_history_sweep(
    box_gpu_t *gpu,
    const box_render_packet_t *packet)
{
    int32_t i;
    for (i = ecs_vec_count(&gpu->history_pairs) - 1; i >= 0; i --) {
        box_gpu_history_pair_t *p = *ecs_vec_get_t(
            &gpu->history_pairs, box_gpu_history_pair_t*, i);
        if (packet->frame - p->frame >= BOX_GPU_HISTORY_IDLE) {
            box_gpu_history_pair_release(p);
            ecs_os_free(p);
            ecs_vec_remove_t(&gpu->history_pairs, box_gpu_history_pair_t*, i);
        }
    }

    box_render_history_sweep(&gpu->histories, packet->frame);
}

bool box_render_gpu_view_history(
    ecs_world_t *world,
    ecs_entity_t view,
    box_render_history_t *out)
{
    box_render_gpu_stats_t stats;
    const box_render_history_t *h = NULL;
    ecs_os_memset_t(out, 0, box_render_history_t);
    if (box_render_gpu_stats(world, &stats) && stats.ready) {
        box_gpu_t *gpu = box_render_get_backend(world)->ctx;
        h = box_render_history_find(&gpu->histories, 0, view);
    }

    if (h) {
        *out = *h;
    }

    return h != NULL;
}

int32_t box_render_gpu_history_pairs(
    ecs_world_t *world,
    box_render_gpu_history_pair_t *out,
    int32_t capacity)
{
    box_render_gpu_stats_t stats;
    int32_t i, result = 0;
    box_gpu_t *gpu = NULL;
    if (box_render_gpu_stats(world, &stats) && stats.ready) {
        gpu = box_render_get_backend(world)->ctx;
        result = ecs_vec_count(&gpu->history_pairs);
    }

    for (i = 0; out && i < result && i < capacity; i ++) {
        const box_gpu_history_pair_t *p = *ecs_vec_get_t(
            &gpu->history_pairs, box_gpu_history_pair_t*, i);
        out[i] = (box_render_gpu_history_pair_t){
            .target = p->desc.target,
            .view = p->desc.view,
            .consumer = p->desc.consumer,
            .entity = p->desc.entity,
            .width = p->desc.width,
            .height = p->desc.height,
            .depth = p->desc.depth,
            .format = (uint32_t)p->desc.format,
            .read = p->views[p->write ^ 1],
            .write = p->views[p->write],
            .valid = p->valid,
            .frame = p->frame
        };
    }

    return result;
}

#else

bool box_render_gpu_view_history(
    ecs_world_t *world,
    ecs_entity_t view,
    box_render_history_t *out)
{
    (void)world;
    (void)view;
    ecs_os_memset_t(out, 0, box_render_history_t);

    return false;
}

int32_t box_render_gpu_history_pairs(
    ecs_world_t *world,
    box_render_gpu_history_pair_t *out,
    int32_t capacity)
{
    (void)world;
    (void)out;
    (void)capacity;

    return 0;
}

#endif
