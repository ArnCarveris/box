/**
 * @file render/texture_preview.c
 * @brief Previews of the nodes of texture graphs and the statistics of their evaluation.
 *
 * A node entity with a TextureNodePreview is read back from the GPU backend at most at its rate,
 * scaled down to its size and written to TextureNodePreviewStatus as a base64 PNG, which a REST
 * client such as tools/texgraph reads.
 *
 * The readbacks of other callers taken along with the ones of the previews wait for
 * box_render_texture_graph_reads. TextureGraphStats is written from the GPU backend counters.
 */

#include "render.h"
#include "../texture/texture.h"
#include <math.h>
#include <stb_image_write.h>

/** Largest dimension of a preview with a size of 0 */
#define BOX_RENDER_PREVIEW_SIZE (128)

/** Readbacks per second of a preview with a rate of 0 */
#define BOX_RENDER_PREVIEW_RATE (4.0)

/** Largest dimension of a preview */
#define BOX_RENDER_PREVIEW_MAX_SIZE (1024)

/** Seconds after which a readback that was not delivered is given up */
#define BOX_RENDER_PREVIEW_TIMEOUT (2.0)

/** Bit set in the tickets of the readbacks of the previews */
#define BOX_RENDER_PREVIEW_TICKET (0x80000000u)

/** Offset basis of the 64 bit FNV-1a hash of the texels of a preview */
#define BOX_RENDER_PREVIEW_FNV_BASIS (14695981039346656037ull)

/** Prime of the 64 bit FNV-1a hash of the texels of a preview */
#define BOX_RENDER_PREVIEW_FNV_PRIME (1099511628211ull)

/** The preview of a node entity */
typedef struct box_render_preview_t {
    ecs_entity_t node;          /**< The node entity */
    uint32_t ticket;            /**< Ticket of the readback in flight, 0 for none */
    double requested;           /**< World time of the last request */
    bool requested_once;        /**< A request was made */
    uint64_t hash;              /**< Hash of the texels and size of the last preview encoded */
    int32_t revision;           /**< Previews encoded */
    bool seen;                  /**< The node had a TextureNodePreview in this frame */
} box_render_preview_t;

/** State of the previews of a world */
typedef struct box_render_previews_t {
    ecs_map_t previews;         /**< Node entity to box_render_preview_t* */
    ecs_vec_t kept;             /**< box_render_texture_read_t, readbacks of other callers taken
                                 *   with the ones of the previews */
    ecs_vec_t delivered;        /**< box_render_texture_read_t, readbacks of the previews that
                                 *   box_render_texture_graph_reads took */
    uint32_t next_ticket;       /**< Number of the next ticket, without the preview bit */
    TextureGraphStats stats;    /**< Statistics written last */
    bool stats_written;         /**< stats was written */
} box_render_previews_t;

/** Singleton with the state of the previews */
typedef struct RenderTexturePreviews {
    box_render_previews_t *previews; /**< The state */
} RenderTexturePreviews;

static ECS_COMPONENT_DECLARE(RenderTexturePreviews);

/** Free the state of the previews.
 *
 * @param p The state, may be NULL
 */
static void box_render_previews_free(
    box_render_previews_t *p)
{
    int32_t i;
    if (!p) {
        goto done;
    }

    ecs_map_iter_t it = ecs_map_iter(&p->previews);
    while (ecs_map_next(&it)) {
        ecs_os_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(&p->previews);
    for (i = 0; i < ecs_vec_count(&p->kept); i ++) {
        ecs_os_free(ecs_vec_get_t(&p->kept, box_render_texture_read_t, i)->texels);
    }

    ecs_vec_fini_t(NULL, &p->kept, box_render_texture_read_t);
    for (i = 0; i < ecs_vec_count(&p->delivered); i ++) {
        ecs_os_free(ecs_vec_get_t(&p->delivered, box_render_texture_read_t, i)->texels);
    }

    ecs_vec_fini_t(NULL, &p->delivered, box_render_texture_read_t);
    ecs_os_free(p);
done:
    return;
}

/** Free the states of the singletons.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_render_texture_previews_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    RenderTexturePreviews *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        box_render_previews_free(values[i].previews);
        values[i].previews = NULL;
    }
}

/** Move the states of the singletons.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_render_texture_previews_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    RenderTexturePreviews *d = dst;
    RenderTexturePreviews *s = src;
    int32_t i;
    for (i = 0; i < count; i ++) {
        if (d[i].previews != s[i].previews) {
            box_render_texture_previews_dtor(&d[i], 1, ti);
        }

        d[i].previews = s[i].previews;
        s[i].previews = NULL;
    }
}

/** Get the state of the previews of a world.
 *
 * @param world The world or a stage
 * @return The state, NULL when box.render is not imported
 */
static box_render_previews_t* box_render_previews_get(
    const ecs_world_t *world)
{
    const RenderTexturePreviews *p = ecs_id(RenderTexturePreviews)
        ? ecs_singleton_get(world, RenderTexturePreviews)
        : NULL;

    return p ? p->previews : NULL;
}

void box_render_texture_previews_kept(
    ecs_world_t *world,
    ecs_vec_t *out)
{
    box_render_previews_t *p = box_render_previews_get(world);
    int32_t i;
    for (i = 0; p && i < ecs_vec_count(&p->kept); i ++) {
        *ecs_vec_append_t(NULL, out, box_render_texture_read_t) =
            *ecs_vec_get_t(&p->kept, box_render_texture_read_t, i);
    }

    if (p) {
        ecs_vec_clear(&p->kept);
    }
}

void box_render_texture_previews_filter(
    ecs_world_t *world,
    ecs_vec_t *out,
    int32_t first)
{
    box_render_previews_t *p = box_render_previews_get(world);
    int32_t i, kept = first, count = ecs_vec_count(out);
    for (i = first; p && i < count; i ++) {
        box_render_texture_read_t *r = ecs_vec_get_t(out, box_render_texture_read_t, i);
        if (r->ticket & BOX_RENDER_PREVIEW_TICKET) {
            *ecs_vec_append_t(NULL, &p->delivered, box_render_texture_read_t) = *r;
        } else {
            *ecs_vec_get_t(out, box_render_texture_read_t, kept ++) = *r;
        }
    }

    if (p) {
        ecs_vec_set_count_t(NULL, out, box_render_texture_read_t, kept);
    }
}

/** Find the index of a node in the compiled graph of a texture.
 *
 * @param tex The texture, may be NULL
 * @param node The node entity
 * @return The index, -1 when the graph does not have it
 */
static int32_t box_render_preview_index(
    const box_texture_t *tex,
    ecs_entity_t node)
{
    int32_t i, result = -1;
    for (i = 0; tex && tex->graph && i < tex->graph->node_count && result < 0; i ++) {
        if (tex->graph->nodes[i].entity == node) {
            result = i;
        }
    }

    return result;
}

/** Find the texture whose compiled graph has a node: its parent when it has it, else the
 * first texture that has it.
 *
 * @param world The world
 * @param t The registry
 * @param node The node entity
 * @return The texture, NULL when no graph has the node
 */
static box_texture_t* box_render_preview_texture(
    const ecs_world_t *world,
    box_textures_t *t,
    ecs_entity_t node)
{
    ecs_entity_t parent = ecs_get_parent(world, node);
    box_texture_t *result = parent
        ? ecs_map_get_deref(&t->textures, box_texture_t, parent)
        : NULL;
    if (result && box_render_preview_index(result, node) >= 0) {
        goto done;
    }

    result = NULL;
    ecs_map_iter_t it = ecs_map_iter(&t->textures);
    while (!result && ecs_map_next(&it)) {
        box_texture_t *tex = ecs_map_ptr(&it);
        if (box_render_preview_index(tex, node) >= 0) {
            result = tex;
        }
    }

done:
    return result;
}

/** Encode a linear channel to sRGB.
 *
 * @param c The channel, clamped to 0..1
 * @return The encoded channel
 */
static float box_render_preview_srgb(
    float c)
{
    float result = c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;

    return result;
}

/** Clamp a channel to 0..1, NaN to 0.
 *
 * @param c The channel
 * @return The clamped channel
 */
static float box_render_preview_clamp(
    float c)
{
    float result = c > 0.0f ? c : 0.0f;
    result = result < 1.0f ? result : 1.0f;

    return result;
}

/** Map a texel of a readback to the display color of the preview.
 *
 * @param in Four floats of the texel
 * @param gray The value is a scalar or a single channel image: x in gray
 * @param encode The value is linear light that the image stores display encoded
 * @param out Receives the display color in 0..1
 */
static void box_render_preview_texel(
    const float *in,
    bool gray,
    bool encode,
    float out[3])
{
    int32_t c;
    for (c = 0; c < 3; c ++) {
        out[c] = box_render_preview_clamp(gray ? in[0] : in[c]);
        out[c] = encode ? box_render_preview_srgb(out[c]) : out[c];
    }
}

/** Scale the texels of a readback down to the pixels of a preview, averaging the texels each
 * pixel covers.
 *
 * @param r The readback
 * @param width Width of the preview
 * @param height Height of the preview
 * @param gray The value is a scalar or a single channel image
 * @param encode The value is linear light that the image stores display encoded
 * @return Opaque RGBA8 pixels, freed by the caller
 */
static uint8_t* box_render_preview_scale(
    const box_render_texture_read_t *r,
    int32_t width,
    int32_t height,
    bool gray,
    bool encode)
{
    uint8_t *result = ecs_os_malloc(width * height * 4);
    int32_t x, y, sx, sy, c;
    for (y = 0; y < height; y ++) {
        int32_t y0 = (int32_t)((int64_t)y * r->height / height);
        int32_t y1 = (int32_t)((int64_t)(y + 1) * r->height / height);
        y1 = y1 > y0 ? y1 : y0 + 1;
        for (x = 0; x < width; x ++) {
            int32_t x0 = (int32_t)((int64_t)x * r->width / width);
            int32_t x1 = (int32_t)((int64_t)(x + 1) * r->width / width);
            float sum[3] = {0, 0, 0}, texel[3];
            x1 = x1 > x0 ? x1 : x0 + 1;
            for (sy = y0; sy < y1; sy ++) {
                for (sx = x0; sx < x1; sx ++) {
                    box_render_preview_texel(&r->texels[(sy * r->width + sx) * 4], gray,
                        encode, texel);
                    sum[0] += texel[0];
                    sum[1] += texel[1];
                    sum[2] += texel[2];
                }
            }

            for (c = 0; c < 3; c ++) {
                float v = sum[c] / (float)((y1 - y0) * (x1 - x0));
                result[(y * width + x) * 4 + c] = (uint8_t)(v * 255.0f + 0.5f);
            }

            result[(y * width + x) * 4 + 3] = 255;
        }
    }

    return result;
}

/** Bytes of a PNG being encoded */
typedef struct box_render_preview_png_t {
    uint8_t *data;              /**< The bytes */
    int32_t size;               /**< Bytes written */
    int32_t capacity;           /**< Bytes allocated */
} box_render_preview_png_t;

/** Append bytes to a PNG being encoded, the write callback of stb_image_write.
 *
 * @param ctx The box_render_preview_png_t
 * @param data The bytes
 * @param size Number of bytes
 */
static void box_render_preview_png_write(
    void *ctx,
    void *data,
    int size)
{
    box_render_preview_png_t *png = ctx;
    if (size <= 0) {
        goto done;
    }

    if (png->size + size > png->capacity) {
        int32_t capacity = png->capacity ? png->capacity : 16 * 1024;
        while (capacity < png->size + size) {
            capacity *= 2;
        }

        png->data = ecs_os_realloc(png->data, capacity);
        png->capacity = capacity;
    }

    ecs_os_memcpy(png->data + png->size, data, size);
    png->size += size;
done:
    return;
}

/** Encode bytes in base64 with padding.
 *
 * @param data The bytes
 * @param size Number of bytes
 * @return The text, freed by the caller
 */
static char* box_render_preview_base64(
    const uint8_t *data,
    int32_t size)
{
    static const char *digits =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char *result = ecs_os_malloc((size + 2) / 3 * 4 + 1);
    int32_t i, o = 0;
    for (i = 0; i < size; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16;
        v |= i + 1 < size ? (uint32_t)data[i + 1] << 8 : 0;
        v |= i + 2 < size ? (uint32_t)data[i + 2] : 0;
        result[o ++] = digits[(v >> 18) & 63];
        result[o ++] = digits[(v >> 12) & 63];
        result[o ++] = i + 1 < size ? digits[(v >> 6) & 63] : '=';
        result[o ++] = i + 2 < size ? digits[v & 63] : '=';
    }

    result[o] = '\0';

    return result;
}

/** Hash the texels and size of a readback.
 *
 * @param r The readback
 * @param width Width of the preview
 * @param height Height of the preview
 * @param mode How the texels map to colors
 * @return The hash
 */
static uint64_t box_render_preview_hash(
    const box_render_texture_read_t *r,
    int32_t width,
    int32_t height,
    int32_t mode)
{
    int32_t header[5] = { r->width, r->height, width, height, mode };
    const uint8_t *bytes = (const uint8_t*)header;
    uint64_t result = BOX_RENDER_PREVIEW_FNV_BASIS;
    int64_t i, count = ECS_SIZEOF(header);
    for (i = 0; i < count; i ++) {
        result = (result ^ bytes[i]) * BOX_RENDER_PREVIEW_FNV_PRIME;
    }

    bytes = (const uint8_t*)r->texels;
    count = (int64_t)r->width * r->height * 4 * ECS_SIZEOF(float);
    for (i = 0; i < count; i ++) {
        result = (result ^ bytes[i]) * BOX_RENDER_PREVIEW_FNV_PRIME;
    }

    return result;
}

/** Encode a delivered readback into the TextureNodePreviewStatus of its node, unless its
 * value and size did not change since the last one.
 *
 * The output node reads as the stored image, other nodes as their value, which the image
 * stores display encoded with GraphColor and GraphHdr.
 *
 * @param world The world or a stage
 * @param t The registry
 * @param preview The preview
 * @param r The readback, with texels
 */
static void box_render_preview_encode(
    ecs_world_t *world,
    box_textures_t *t,
    box_render_preview_t *preview,
    const box_render_texture_read_t *r)
{
    const TextureNodePreview *desc = ecs_get(world, preview->node, TextureNodePreview);
    const box_texture_t *tex = r->texture
        ? ecs_map_get_deref(&t->textures, box_texture_t, r->texture)
        : NULL;
    box_render_preview_png_t png = {0};
    int32_t size, width, height, index, mode;
    bool gray, encode, output;
    uint8_t *pixels = NULL;
    char *text = NULL;
    uint64_t hash;
    if (!desc || !tex || !tex->graph || r->width <= 0 || r->height <= 0) {
        goto done;
    }

    size = desc->size > 0 ? desc->size : BOX_RENDER_PREVIEW_SIZE;
    size = size < BOX_RENDER_PREVIEW_MAX_SIZE ? size : BOX_RENDER_PREVIEW_MAX_SIZE;
    width = r->width >= r->height ? size : (int32_t)((int64_t)size * r->width / r->height);
    height = r->height >= r->width ? size : (int32_t)((int64_t)size * r->height / r->width);
    width = width < r->width ? width : r->width;
    height = height < r->height ? height : r->height;
    width = width > 0 ? width : 1;
    height = height > 0 ? height : 1;
    index = box_render_preview_index(tex, preview->node);
    output = r->index < 0 || r->index == tex->graph->node_count - 1;
    gray = tex->graph->format == GraphSingle || tex->graph->format == GraphFloat ||
        (!output && index >= 0 && tex->graph->nodes[index].output == TexScalar);
    encode = !output && (tex->graph->format == GraphColor || tex->graph->format == GraphHdr);
    mode = (gray ? 1 : 0) | (encode ? 2 : 0);
    hash = box_render_preview_hash(r, width, height, mode);
    if (preview->revision && hash == preview->hash) {
        goto done;
    }

    pixels = box_render_preview_scale(r, width, height, gray, encode);
    if (!stbi_write_png_to_func(box_render_preview_png_write, &png, width, height, 4, pixels,
        width * 4) || !png.size)
    {
        goto done;
    }

    text = box_render_preview_base64(png.data, png.size);
    preview->hash = hash;
    preview->revision ++;
    ecs_set(world, preview->node, TextureNodePreviewStatus, {
        .width = width, .height = height, .revision = preview->revision, .png = text
    });
done:
    ecs_os_free(text);
    ecs_os_free(png.data);
    ecs_os_free(pixels);

    return;
}

/** Find the preview whose readback in flight has a ticket.
 *
 * @param p The state of the previews
 * @param ticket The ticket
 * @return The preview, NULL when none waits for the ticket
 */
static box_render_preview_t* box_render_preview_of_ticket(
    box_render_previews_t *p,
    uint32_t ticket)
{
    box_render_preview_t *result = NULL;
    ecs_map_iter_t it = ecs_map_iter(&p->previews);
    while (!result && ecs_map_next(&it)) {
        box_render_preview_t *preview = ecs_map_ptr(&it);
        if (preview->ticket == ticket) {
            result = preview;
        }
    }

    return result;
}

/** Get the node entity a readback holds the value of: its node, or the output node of its
 * graph for a readback of the image.
 *
 * @param t The registry
 * @param r The readback
 * @return The node entity, 0 when the graph is gone
 */
static ecs_entity_t box_render_preview_node_of(
    box_textures_t *t,
    const box_render_texture_read_t *r)
{
    const box_texture_t *tex = r->texture
        ? ecs_map_get_deref(&t->textures, box_texture_t, r->texture)
        : NULL;
    ecs_entity_t result = r->node;
    if (!result && r->index < 0 && tex && tex->graph && tex->graph->node_count) {
        result = tex->graph->nodes[tex->graph->node_count - 1].entity;
    }

    return result;
}

/** Take the delivered readbacks: the ones of the previews are encoded, the others are kept for
 * box_render_texture_graph_reads.
 *
 * @param world The world
 * @param stage The stage to write with
 * @param t The registry
 * @param p The state of the previews
 */
static void box_render_previews_deliver(
    ecs_world_t *world,
    ecs_world_t *stage,
    box_textures_t *t,
    box_render_previews_t *p)
{
    ecs_vec_t reads;
    int32_t i;
    ecs_vec_init_t(NULL, &reads, box_render_texture_read_t, 0);
    for (i = 0; i < ecs_vec_count(&p->delivered); i ++) {
        *ecs_vec_append_t(NULL, &reads, box_render_texture_read_t) =
            *ecs_vec_get_t(&p->delivered, box_render_texture_read_t, i);
    }

    ecs_vec_clear(&p->delivered);
    box_render_texture_graph_reads_take(world, &reads);
    for (i = 0; i < ecs_vec_count(&reads); i ++) {
        box_render_texture_read_t *r = ecs_vec_get_t(&reads, box_render_texture_read_t, i);
        box_render_preview_t *preview = NULL;
        if (!(r->ticket & BOX_RENDER_PREVIEW_TICKET)) {
            *ecs_vec_append_t(NULL, &p->kept, box_render_texture_read_t) = *r;
            continue;
        }

        preview = box_render_preview_of_ticket(p, r->ticket);
        if (preview) {
            preview->ticket = 0;
            if (r->texels && box_render_preview_node_of(t, r) == preview->node) {
                box_render_preview_encode(stage, t, preview, r);
            }
        }

        ecs_os_free(r->texels);
    }

    ecs_vec_fini_t(NULL, &reads, box_render_texture_read_t);
}

/** Request a readback for a preview whose last one was delivered or given up and whose rate
 * allows another.
 *
 * @param world The world
 * @param t The registry
 * @param p The state of the previews
 * @param preview The preview
 * @param rate Its rate, 0 for the default
 * @param now World time
 */
static void box_render_preview_request(
    ecs_world_t *world,
    box_textures_t *t,
    box_render_previews_t *p,
    box_render_preview_t *preview,
    float rate,
    double now)
{
    double interval = 1.0 / (rate > 0 ? (double)rate : BOX_RENDER_PREVIEW_RATE);
    box_texture_t *tex;
    uint32_t ticket;
    if (preview->ticket && now - preview->requested > BOX_RENDER_PREVIEW_TIMEOUT) {
        preview->ticket = 0;
    }

    if (preview->ticket || (preview->requested_once && now - preview->requested < interval)) {
        goto done;
    }

    tex = box_render_preview_texture(world, t, preview->node);
    if (!tex) {
        goto done;
    }

    ticket = BOX_RENDER_PREVIEW_TICKET | (p->next_ticket & ~BOX_RENDER_PREVIEW_TICKET);
    p->next_ticket = (p->next_ticket + 1) & ~BOX_RENDER_PREVIEW_TICKET;
    p->next_ticket = p->next_ticket ? p->next_ticket : 1;
    if (box_render_texture_graph_read(world, tex->entity, preview->node, ticket)) {
        preview->ticket = ticket;
        preview->requested = now;
        preview->requested_once = true;
    }

done:
    return;
}

/** Forget the previews whose node lost its TextureNodePreview, removing their status.
 *
 * @param world The world
 * @param stage The stage to write with
 * @param p The state of the previews
 */
static void box_render_previews_prune(
    ecs_world_t *world,
    ecs_world_t *stage,
    box_render_previews_t *p)
{
    ecs_vec_t gone;
    int32_t i;
    ecs_vec_init_t(NULL, &gone, ecs_entity_t, 0);
    ecs_map_iter_t it = ecs_map_iter(&p->previews);
    while (ecs_map_next(&it)) {
        box_render_preview_t *preview = ecs_map_ptr(&it);
        if (!preview->seen) {
            *ecs_vec_append_t(NULL, &gone, ecs_entity_t) = preview->node;
        }

        preview->seen = false;
    }

    for (i = 0; i < ecs_vec_count(&gone); i ++) {
        ecs_entity_t node = *ecs_vec_get_t(&gone, ecs_entity_t, i);
        ecs_os_free(ecs_map_get_deref(&p->previews, box_render_preview_t, node));
        ecs_map_remove(&p->previews, node);
        if (ecs_is_alive(world, node) && ecs_has(world, node, TextureNodePreviewStatus)) {
            ecs_remove(stage, node, TextureNodePreviewStatus);
        }
    }

    ecs_vec_fini_t(NULL, &gone, ecs_entity_t);
}

/** Write TextureGraphStats from the counters of the GPU backend when they changed.
 *
 * @param world The world
 * @param stage The stage to write with
 * @param p The state of the previews
 * @param previews Nodes with a TextureNodePreview
 */
static void box_render_previews_stats(
    ecs_world_t *world,
    ecs_world_t *stage,
    box_render_previews_t *p,
    int32_t previews)
{
    box_render_gpu_stats_t s;
    TextureGraphStats stats;
    if (!box_render_gpu_stats(world, &s)) {
        goto done;
    }

    ecs_os_memset_t(&stats, 0, TextureGraphStats);
    stats.graphs = s.texture_graphs;
    stats.programs = s.texture_graph_programs;
    stats.evaluations = s.texture_graph_evaluations;
    stats.dispatches = s.texture_graph_dispatches;
    stats.texels = s.texture_graph_texels;
    stats.gpu_ms = (double)s.texture_graph_ns / 1e6;
    stats.timed = s.texture_graph_timed;
    stats.pool_bytes = s.texture_graph_pool_bytes;
    stats.key_bytes = s.texture_graph_key_bytes;
    stats.blends = s.texture_graph_blends;
    stats.reads = s.texture_graph_reads;
    stats.previews = previews;
    if (p->stats_written && !ecs_os_memcmp_t(&stats, &p->stats, TextureGraphStats)) {
        goto done;
    }

    p->stats = stats;
    p->stats_written = true;
    ecs_set_ptr(stage, ecs_id(TextureGraphStats), TextureGraphStats, &stats);
done:
    return;
}

/** Deliver the readbacks of the previews, request the next ones, forget removed previews and
 * write the statistics of the texture graphs.
 *
 * The nodes are found with ecs_each_id rather than a query made at import, because box.surface
 * imports box.render before box.texture registers TextureNodePreview.
 *
 * @param it The iterator of the system
 */
static void BoxRenderTexturePreviews(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    box_render_previews_t *p = box_render_previews_get(world);
    box_textures_t *t = box_textures_get(world);
    double now = (double)ecs_get_world_info(world)->world_time_total;
    bool inflight = false;
    int32_t i, count = 0;
    if (!p || !t || !ecs_id(TextureNodePreview)) {
        goto done;
    }

    ecs_map_iter_t mit = ecs_map_iter(&p->previews);
    while (!inflight && ecs_map_next(&mit)) {
        inflight = ((box_render_preview_t*)ecs_map_ptr(&mit))->ticket != 0;
    }

    if (inflight || ecs_vec_count(&p->delivered)) {
        box_render_previews_deliver(world, it->stage, t, p);
    }

    ecs_iter_t qit = ecs_each_id(world, ecs_id(TextureNodePreview));
    while (ecs_each_next(&qit)) {
        const TextureNodePreview *desc = ecs_field(&qit, TextureNodePreview, 0);
        for (i = 0; i < qit.count; i ++) {
            ecs_entity_t node = qit.entities[i];
            box_render_preview_t **ptr = (box_render_preview_t**)ecs_map_ensure(
                &p->previews, node);
            if (!*ptr) {
                *ptr = ecs_os_calloc_t(box_render_preview_t);
                (*ptr)->node = node;
            }

            (*ptr)->seen = true;
            box_render_preview_request(world, t, p, *ptr, desc[i].rate, now);
            count ++;
        }
    }

    box_render_previews_prune(world, it->stage, p);
    box_render_previews_stats(world, it->stage, p, count);
done:
    return;
}

void box_render_texture_previews_init(
    ecs_world_t *world)
{
    box_render_previews_t *p = ecs_os_calloc_t(box_render_previews_t);
    ecs_set_name_prefix(world, "Render");
    ECS_COMPONENT_DEFINE(world, RenderTexturePreviews);
    ecs_set_name_prefix(world, NULL);
    ecs_set_hooks(world, RenderTexturePreviews, {
        .ctor = flecs_default_ctor,
        .dtor = box_render_texture_previews_dtor,
        .move = box_render_texture_previews_move
    });
    ecs_map_init(&p->previews, NULL);
    ecs_vec_init_t(NULL, &p->kept, box_render_texture_read_t, 0);
    ecs_vec_init_t(NULL, &p->delivered, box_render_texture_read_t, 0);
    p->next_ticket = 1;
    ecs_singleton_set(world, RenderTexturePreviews, { .previews = p });
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.TexturePreviews" }),
        .phase = RenderExtract,
        .run = BoxRenderTexturePreviews
    });
}
