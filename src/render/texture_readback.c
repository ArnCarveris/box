/**
 * @file render/texture_readback.c
 * @brief CPU copies of the images of the textures with a TextureReadback.
 *
 * After every new revision of the image of such a texture its copy is replaced: the image of a
 * TextureGraph is read back from the GPU backend after its evaluation, mapped without waiting as
 * the node previews are; an image made on the CPU is copied from its pixels at once.
 *
 * The copies live in the texture registry, which box.texture samples them from.
 */

#include "render.h"
#include "../texture/texture.h"
#include <string.h>

/** Seconds after which a readback that was not delivered is requested again */
#define BOX_RENDER_READBACK_TIMEOUT (2.0)

/** Mip levels a copy is scaled down by at most */
#define BOX_RENDER_READBACK_LEVEL_MAX (16)

/** Turn a delivered readback of the image of a graph into the texels of a copy.
 *
 * @param r The readback, its texels are taken when the copy keeps them
 * @param format What the image of the graph stores
 * @param layout Receives the layout of the copy
 * @return The texels of the copy
 */
static void* box_render_readback_texels(
    box_render_texture_read_t *r,
    TextureGraphFormat format,
    TextureReadbackFormat *layout)
{
    size_t n = (size_t)r->width * (size_t)r->height, i;
    void *result;
    if (format == GraphFloat4 || format == GraphHdr) {
        *layout = ReadbackRGBA32Float;
        result = r->texels;
        r->texels = NULL;
    } else if (format == GraphFloat) {
        float *out = ecs_os_malloc_n(float, (int32_t)n);
        for (i = 0; i < n; i ++) {
            out[i] = r->texels[i * 4];
        }

        *layout = ReadbackR32Float;
        result = out;
    } else {
        size_t channels = format == GraphSingle ? 1 : 4;
        uint8_t *out = ecs_os_malloc_n(uint8_t, (int32_t)(n * channels));
        for (i = 0; i < n * channels; i ++) {
            float v = r->texels[(i / channels) * 4 + i % channels];
            v = v < 0 ? 0 : v > 1 ? 1 : v;
            out[i] = (uint8_t)(v * 255.0f + 0.5f);
        }

        *layout = channels == 1 ? ReadbackR8 : ReadbackRGBA8;
        result = out;
    }

    return result;
}

/** Store a delivered readback into the copy that requested it, unless a newer one was
 * requested since.
 *
 * @param t The registry
 * @param r The readback
 */
static void box_render_readback_deliver(
    box_textures_t *t,
    box_render_texture_read_t *r)
{
    box_texture_readback_t *rb = box_texture_readback_get(t, r->texture);
    TextureReadbackFormat layout;
    void *data;
    if (!rb || rb->ticket != r->ticket) {
        goto done;
    }

    rb->ticket = 0;
    rb->failed = !r->texels || r->width <= 0 || r->height <= 0;
    if (rb->failed) {
        goto done;
    }

    data = box_render_readback_texels(r, rb->graph_format, &layout);
    box_texture_readback_store(rb, data, r->width, r->height, layout, rb->requested,
        rb->requested_level);
done:
    return;
}

void box_render_texture_readbacks_filter(
    ecs_world_t *world,
    ecs_vec_t *out,
    int32_t first)
{
    box_textures_t *t = box_textures_get(world);
    int32_t i, kept = first, count = ecs_vec_count(out);
    for (i = first; i < count; i ++) {
        box_render_texture_read_t *r = ecs_vec_get_t(out, box_render_texture_read_t, i);
        if ((r->ticket & BOX_RENDER_READBACK_TICKET_MASK) == BOX_RENDER_READBACK_TICKET) {
            if (t) {
                box_render_readback_deliver(t, r);
            }

            ecs_os_free(r->texels);
        } else {
            *ecs_vec_get_t(out, box_render_texture_read_t, kept ++) = *r;
        }
    }

    ecs_vec_set_count_t(NULL, out, box_render_texture_read_t, kept);
}

/** Copy the pixels of an image made on the CPU into a copy.
 *
 * @param rb The copy
 * @param tex The texture, with pixels
 * @param level Mip level of the copy
 */
static void box_render_readback_pixels(
    box_texture_readback_t *rb,
    const box_texture_t *tex,
    int32_t level)
{
    size_t size = (size_t)tex->width * (size_t)tex->height * (tex->single_channel ? 1u : 4u);
    void *data = ecs_os_malloc((ecs_size_t)size);
    ecs_os_memcpy(data, tex->pixels, (ecs_size_t)size);
    box_texture_readback_store(rb, data, tex->width, tex->height,
        tex->single_channel ? ReadbackR8 : ReadbackRGBA8, tex->revision, level);
    rb->failed = false;
}

/** Request the copy of a new revision of the image of a texture, unless one is in flight.
 *
 * @param world The world
 * @param t The registry
 * @param rb The copy
 * @param desc Its TextureReadback
 * @param now World time
 */
static void box_render_readback_update(
    ecs_world_t *world,
    box_textures_t *t,
    box_texture_readback_t *rb,
    const TextureReadback *desc,
    double now)
{
    const box_texture_t *tex = ecs_map_get_deref(&t->textures, box_texture_t, rb->entity);
    int32_t level = desc->level < 0 ? 0
        : desc->level > BOX_RENDER_READBACK_LEVEL_MAX ? BOX_RENDER_READBACK_LEVEL_MAX
        : desc->level;
    uint32_t ticket;
    if (rb->ticket && now - rb->sent > BOX_RENDER_READBACK_TIMEOUT) {
        rb->ticket = 0;
        rb->requested = 0;
    }

    if (!tex || !tex->revision || rb->ticket ||
        (rb->requested == tex->revision && rb->requested_level == level))
    {
        goto done;
    }

    if (!desc->keep && rb->data && (rb->revision != tex->revision || rb->level != level)) {
        box_texture_readback_clear(rb);
    }

    if (!tex->graph) {
        if (tex->pixels && tex->width > 0 && tex->height > 0) {
            box_render_readback_pixels(rb, tex, level);
            rb->requested = tex->revision;
            rb->requested_level = level;
        }

        goto done;
    }

    t->readback_ticket = (t->readback_ticket + 1) & ~BOX_RENDER_READBACK_TICKET_MASK;
    t->readback_ticket = t->readback_ticket ? t->readback_ticket : 1;
    ticket = BOX_RENDER_READBACK_TICKET | t->readback_ticket;
    if (box_render_texture_graph_read(world, rb->entity, 0, ticket)) {
        rb->ticket = ticket;
        rb->sent = now;
        rb->requested = tex->revision;
        rb->requested_level = level;
        rb->graph_format = tex->graph->format;
    }

done:
    return;
}

/** Write the status of a copy when it changed.
 *
 * @param stage The stage to write with
 * @param rb The copy
 */
static void box_render_readback_status(
    ecs_world_t *stage,
    box_texture_readback_t *rb)
{
    TextureReadbackStatus s = {
        .revision = (int32_t)rb->revision,
        .width = rb->width,
        .height = rb->height,
        .format = rb->format,
        .ready = rb->data != NULL
    };
    if (rb->status_written && s.revision == rb->status.revision &&
        s.width == rb->status.width && s.height == rb->status.height &&
        s.format == rb->status.format && s.ready == rb->status.ready)
    {
        goto done;
    }

    rb->status = s;
    rb->status_written = true;
    ecs_set_ptr(stage, rb->entity, TextureReadbackStatus, &s);
done:
    return;
}

/** Take the delivered readbacks of the copies, giving the others back to the previews and
 * the callers of box_render_texture_graph_reads.
 *
 * @param world The world
 */
static void box_render_readbacks_take(
    ecs_world_t *world)
{
    ecs_vec_t reads;
    ecs_vec_init_t(NULL, &reads, box_render_texture_read_t, 0);
    box_render_texture_graph_reads_take(world, &reads);
    box_render_texture_previews_return(world, &reads);
    ecs_vec_fini_t(NULL, &reads, box_render_texture_read_t);
}

/** Store the delivered copies, request the copies of new image revisions and write the
 * statuses of the textures with a TextureReadback.
 *
 * The textures are found with ecs_each_id rather than a query made at import, because
 * box.surface imports box.render before box.texture registers TextureReadback.
 *
 * @param it The iterator of the system
 */
static void BoxRenderTextureReadbacks(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_textures_t *t = box_textures_get(world);
    double now = (double)ecs_get_world_info(world)->world_time_total;
    bool inflight = false;
    int32_t i;
    if (!t || !ecs_id(TextureReadback)) {
        goto done;
    }

    ecs_map_iter_t mit = ecs_map_iter(&t->readbacks);
    while (!inflight && ecs_map_next(&mit)) {
        inflight = ((box_texture_readback_t*)ecs_map_ptr(&mit))->ticket != 0;
    }

    if (inflight) {
        box_render_readbacks_take(world);
    }

    ecs_iter_t qit = ecs_each_id(world, ecs_id(TextureReadback));
    while (ecs_each_next(&qit)) {
        const TextureReadback *desc = ecs_field(&qit, TextureReadback, 0);
        for (i = 0; i < qit.count; i ++) {
            box_texture_readback_t *rb = box_texture_readback_ensure(t, qit.entities[i]);
            box_render_readback_update(world, t, rb, &desc[i], now);
            box_render_readback_status(it->stage, rb);
        }
    }

done:
    return;
}

void box_render_texture_readbacks_init(
    ecs_world_t *world)
{
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.TextureReadbacks" }),
        .phase = RenderExtract,
        .run = BoxRenderTextureReadbacks
    });
}
