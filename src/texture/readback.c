/**
 * @file texture/readback.c
 * @brief CPU copies of texture images for TextureReadback: their storage, the sampling from C
 * and the box.texture script functions.
 *
 * box.render fills the copies after every new revision of an image, see
 * render/texture_readback.c; this file only keeps and reads them.
 */

#include "texture.h"
#include <math.h>

/** Get the channels of a copy format.
 *
 * @param format The format
 * @return Channels per texel, 0 for ReadbackNone
 */
static int32_t box_texture_copy_channels(
    TextureReadbackFormat format)
{
    return format == ReadbackR8 || format == ReadbackR32Float ? 1
        : format == ReadbackRGBA8 || format == ReadbackRGBA32Float ? 4 : 0;
}

int32_t box_texture_copy_texel_size(
    TextureReadbackFormat format)
{
    return format == ReadbackR8 ? 1
        : format == ReadbackRGBA8 || format == ReadbackR32Float ? 4
        : format == ReadbackRGBA32Float ? 16 : 0;
}

/** Read a channel of the texels of a copy.
 *
 * @param data The texels
 * @param format Their layout
 * @param index Index of the channel among all channels of the texels
 * @return Its value, 8 bit channels as 0..1
 */
static float box_texture_copy_channel(
    const void *data,
    TextureReadbackFormat format,
    size_t index)
{
    return format == ReadbackR8 || format == ReadbackRGBA8
        ? (float)((const uint8_t*)data)[index] / 255.0f
        : ((const float*)data)[index];
}

/** Write a channel of the texels of a copy.
 *
 * @param data The texels
 * @param format Their layout
 * @param index Index of the channel among all channels of the texels
 * @param value The value, 8 bit channels from 0..1
 */
static void box_texture_copy_channel_set(
    void *data,
    TextureReadbackFormat format,
    size_t index,
    float value)
{
    if (format == ReadbackR8 || format == ReadbackRGBA8) {
        float v = value < 0 ? 0 : value > 1 ? 1 : value;
        ((uint8_t*)data)[index] = (uint8_t)(v * 255.0f + 0.5f);
    } else {
        ((float*)data)[index] = value;
    }
}

bool box_texture_copy_texel(
    const box_texture_copy_t *copy,
    int32_t x,
    int32_t y,
    float out[4])
{
    int32_t channels = copy ? box_texture_copy_channels(copy->format) : 0, c;
    bool result = copy && copy->data && channels && copy->width > 0 && copy->height > 0;
    size_t first;
    out[0] = out[1] = out[2] = out[3] = 0;
    if (!result) {
        goto done;
    }

    x = x < 0 ? 0 : x >= copy->width ? copy->width - 1 : x;
    y = y < 0 ? 0 : y >= copy->height ? copy->height - 1 : y;
    first = ((size_t)y * (size_t)copy->width + (size_t)x) * (size_t)channels;
    for (c = 0; c < channels; c ++) {
        out[c] = box_texture_copy_channel(copy->data, copy->format, first + (size_t)c);
    }

    if (channels == 1) {
        out[3] = 1;
    }

done:
    return result;
}

/** Resolve a texel coordinate of a copy for an address mode.
 *
 * @param i The coordinate, may be outside the image
 * @param size Texels along it
 * @param wrap Repeat the image, else clamp to its edge
 * @return The coordinate inside the image
 */
static int32_t box_texture_copy_address(
    int32_t i,
    int32_t size,
    bool wrap)
{
    int32_t result;
    if (wrap) {
        result = i % size;
        result = result < 0 ? result + size : result;
    } else {
        result = i < 0 ? 0 : i >= size ? size - 1 : i;
    }

    return result;
}

bool box_texture_copy_sample(
    const box_texture_copy_t *copy,
    float u,
    float v,
    TextureAddress address,
    float out[4])
{
    bool wrap = address == AddressWrap, result;
    float a[4], b[4], c[4], d[4], fx, fy, tx, ty;
    int32_t x0, y0, x1, y1, i;
    out[0] = out[1] = out[2] = out[3] = 0;
    result = box_texture_copy_texel(copy, 0, 0, a);
    if (!result || !isfinite(u) || !isfinite(v)) {
        goto done;
    }

    fx = u * (float)copy->width - 0.5f;
    fy = v * (float)copy->height - 0.5f;
    tx = floorf(fx);
    ty = floorf(fy);
    fx -= tx;
    fy -= ty;
    if (wrap) {
        tx = fmodf(tx, (float)copy->width);
        ty = fmodf(ty, (float)copy->height);
    } else {
        tx = tx < -1 ? -1 : tx > (float)copy->width ? (float)copy->width : tx;
        ty = ty < -1 ? -1 : ty > (float)copy->height ? (float)copy->height : ty;
    }

    x0 = box_texture_copy_address((int32_t)tx, copy->width, wrap);
    x1 = box_texture_copy_address((int32_t)tx + 1, copy->width, wrap);
    y0 = box_texture_copy_address((int32_t)ty, copy->height, wrap);
    y1 = box_texture_copy_address((int32_t)ty + 1, copy->height, wrap);
    box_texture_copy_texel(copy, x0, y0, a);
    box_texture_copy_texel(copy, x1, y0, b);
    box_texture_copy_texel(copy, x0, y1, c);
    box_texture_copy_texel(copy, x1, y1, d);
    for (i = 0; i < 4; i ++) {
        float top = a[i] + (b[i] - a[i]) * fx;
        float bottom = c[i] + (d[i] - c[i]) * fx;
        out[i] = top + (bottom - top) * fy;
    }

done:
    return result;
}

box_texture_readback_t* box_texture_readback_get(
    const box_textures_t *t,
    ecs_entity_t e)
{
    return t && e ? ecs_map_get_deref(&t->readbacks, box_texture_readback_t, e) : NULL;
}

box_texture_readback_t* box_texture_readback_ensure(
    box_textures_t *t,
    ecs_entity_t e)
{
    box_texture_readback_t **ptr = (box_texture_readback_t**)ecs_map_ensure(
        &t->readbacks, e);
    if (!*ptr) {
        *ptr = ecs_os_calloc_t(box_texture_readback_t);
        (*ptr)->entity = e;
        (*ptr)->serial = &t->readback_serial;
    }

    return *ptr;
}

void box_texture_readback_touch(
    box_textures_t *t,
    ecs_entity_t e)
{
    box_texture_readback_t *rb = box_texture_readback_get(t, e);
    if (rb && !rb->listed) {
        rb->listed = true;
        *ecs_vec_append_t(NULL, &t->readback_dirty, ecs_entity_t) = e;
    }
}

void box_texture_readback_clear(
    box_texture_readback_t *rb)
{
    ecs_os_free(rb->data);
    rb->data = NULL;
    rb->width = 0;
    rb->height = 0;
    rb->format = ReadbackNone;
    rb->revision = 0;
    rb->level = 0;
    if (rb->serial) {
        (*rb->serial) ++;
    }
}

/** Halve the size of texels, averaging 2 x 2 texels; an odd last row or column is averaged
 * with itself.
 *
 * @param data The texels
 * @param width Their width, receives the new width
 * @param height Their height, receives the new height
 * @param format Their layout
 * @return The new texels, freed by the caller
 */
static void* box_texture_readback_halve(
    const void *data,
    int32_t *width,
    int32_t *height,
    TextureReadbackFormat format)
{
    int32_t w = *width, h = *height, channels = box_texture_copy_channels(format);
    int32_t nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1, x, y, c;
    void *result = ecs_os_malloc((ecs_size_t)((size_t)nw * (size_t)nh *
        (size_t)box_texture_copy_texel_size(format)));
    for (y = 0; y < nh; y ++) {
        int32_t y0 = y * 2 < h ? y * 2 : h - 1, y1 = y * 2 + 1 < h ? y * 2 + 1 : y0;
        for (x = 0; x < nw; x ++) {
            int32_t x0 = x * 2 < w ? x * 2 : w - 1, x1 = x * 2 + 1 < w ? x * 2 + 1 : x0;
            for (c = 0; c < channels; c ++) {
                float sum =
                    box_texture_copy_channel(data, format,
                        ((size_t)y0 * (size_t)w + (size_t)x0) * (size_t)channels + (size_t)c) +
                    box_texture_copy_channel(data, format,
                        ((size_t)y0 * (size_t)w + (size_t)x1) * (size_t)channels + (size_t)c) +
                    box_texture_copy_channel(data, format,
                        ((size_t)y1 * (size_t)w + (size_t)x0) * (size_t)channels + (size_t)c) +
                    box_texture_copy_channel(data, format,
                        ((size_t)y1 * (size_t)w + (size_t)x1) * (size_t)channels + (size_t)c);
                box_texture_copy_channel_set(result, format,
                    ((size_t)y * (size_t)nw + (size_t)x) * (size_t)channels + (size_t)c,
                    sum * 0.25f);
            }
        }
    }

    *width = nw;
    *height = nh;

    return result;
}

void box_texture_readback_store(
    box_texture_readback_t *rb,
    void *data,
    int32_t width,
    int32_t height,
    TextureReadbackFormat format,
    uint32_t revision,
    int32_t level)
{
    int32_t i;
    for (i = 0; i < level && (width > 1 || height > 1); i ++) {
        void *half = box_texture_readback_halve(data, &width, &height, format);
        ecs_os_free(data);
        data = half;
    }

    ecs_os_free(rb->data);
    rb->data = data;
    rb->width = width;
    rb->height = height;
    rb->format = format;
    rb->revision = revision;
    rb->level = level;
    if (rb->serial) {
        (*rb->serial) ++;
    }
}

/** Get the copy of a texture entity that is ready.
 *
 * @param world The world
 * @param texture The texture entity
 * @param out Receives the copy, zero when none is ready
 * @return Whether a copy is ready
 */
static bool box_texture_readback_ready(
    const ecs_world_t *world,
    ecs_entity_t texture,
    box_texture_copy_t *out)
{
    const box_texture_readback_t *rb = box_texture_readback_get(box_textures_get(world),
        texture);
    bool result = rb && rb->data;
    ecs_os_memset_t(out, 0, box_texture_copy_t);
    if (result) {
        out->data = rb->data;
        out->width = rb->width;
        out->height = rb->height;
        out->format = rb->format;
        out->revision = (int32_t)rb->revision;
    }

    return result;
}

bool box_texture_readback(
    const ecs_world_t *world,
    ecs_entity_t texture,
    box_texture_copy_t *out)
{
    return box_texture_readback_ready(world, texture, out);
}

bool box_texture_readback_texel(
    const ecs_world_t *world,
    ecs_entity_t texture,
    int32_t x,
    int32_t y,
    float out[4])
{
    box_texture_copy_t copy;
    box_texture_readback_ready(world, texture, &copy);

    return box_texture_copy_texel(&copy, x, y, out);
}

bool box_texture_readback_sample(
    const ecs_world_t *world,
    ecs_entity_t texture,
    float u,
    float v,
    TextureAddress address,
    float out[4])
{
    box_texture_copy_t copy;
    box_texture_readback_ready(world, texture, &copy);

    return box_texture_copy_sample(&copy, u, v, address, out);
}

/** Free a CPU copy and forget it.
 *
 * @param t The registry
 * @param e The texture entity
 */
static void box_texture_readback_free(
    box_textures_t *t,
    ecs_entity_t e)
{
    box_texture_readback_t *rb = box_texture_readback_get(t, e);
    if (rb) {
        ecs_os_free(rb->data);
        ecs_os_free(rb);
        ecs_map_remove(&t->readbacks, e);
        t->readback_serial ++;
    }
}

/** Create the copy of a texture whose TextureReadback is added or set and put it in the list
 * of copies to update, so the copies are only looked at when something changed.
 *
 * @param it The iterator of the observer
 */
static void BoxTextureReadbackOnSet(
    ecs_iter_t *it)
{
    box_textures_t *t = box_textures_get(it->world);
    int32_t i;
    for (i = 0; t && i < it->count; i ++) {
        box_texture_readback_ensure(t, it->entities[i]);
        box_texture_readback_touch(t, it->entities[i]);
    }
}

/** Free the copy of a texture whose TextureReadback is removed or that is deleted, and remove
 * its status.
 *
 * @param it The iterator of the observer
 */
static void BoxTextureReadbackOnRemove(
    ecs_iter_t *it)
{
    box_textures_t *t = box_textures_get(it->world);
    bool deleted = !it->other_table || !ecs_table_get_type(it->other_table)->count;
    int32_t i;
    for (i = 0; t && i < it->count; i ++) {
        box_texture_readback_free(t, it->entities[i]);
    }

    if (deleted || !ecs_table_has_id(it->world, it->table, ecs_id(TextureReadbackStatus))) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        ecs_remove(it->stage, it->entities[i], TextureReadbackStatus);
    }

done:
    return;
}

/** Default TextureReadback: the full image, keeping the previous copy.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_texture_readback_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    TextureReadback *r = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        r[i] = (TextureReadback){ .keep = true };
    }
}

/** Sample the copy of a texture from script, one channel.
 *
 * @param world The world
 * @param argv The texture entity, u and v, then the channel when channel is true
 * @param address AddressWrap or AddressClamp
 * @param channel The arguments have a channel
 * @return The channel, 0 while no copy is ready or for a channel outside 0..3
 */
static double box_texture_script_sample(
    const ecs_world_t *world,
    const ecs_value_t *argv,
    TextureAddress address,
    bool channel)
{
    float out[4];
    int32_t c = channel ? *(const int32_t*)argv[3].ptr : 0;
    box_texture_readback_sample(world, *(const ecs_entity_t*)argv[0].ptr,
        (float)*(const double*)argv[1].ptr, (float)*(const double*)argv[2].ptr, address, out);

    return c >= 0 && c < 4 ? (double)out[c] : 0.0;
}

/** box.texture.sample(texture, u, v): the first channel of the copy, clamped at its edges.
 *
 * @param ctx The function context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the value
 */
static void box_texture_fn_sample(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    *(double*)result->ptr = box_texture_script_sample(ctx->world, argv, AddressClamp, false);
}

/** box.texture.sample_wrap(texture, u, v): the first channel of the copy, repeating it.
 *
 * @param ctx The function context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the value
 */
static void box_texture_fn_sample_wrap(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    *(double*)result->ptr = box_texture_script_sample(ctx->world, argv, AddressWrap, false);
}

/** box.texture.sample_channel(texture, u, v, channel): a channel of the copy, clamped.
 *
 * @param ctx The function context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the value
 */
static void box_texture_fn_sample_channel(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    *(double*)result->ptr = box_texture_script_sample(ctx->world, argv, AddressClamp, true);
}

/** box.texture.readback_ready(texture): whether the texture has a copy to sample.
 *
 * @param ctx The function context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the answer
 */
static void box_texture_fn_readback_ready(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    box_texture_copy_t copy;
    (void)argc;
    *(bool*)result->ptr = box_texture_readback_ready(ctx->world,
        *(const ecs_entity_t*)argv[0].ptr, &copy);
}

void box_texture_readbacks_init(
    ecs_world_t *world,
    box_textures_t *t)
{
    ecs_entity_t module = ecs_get_scope(world);
    ecs_map_init(&t->readbacks, NULL);
    ecs_set_hooks(world, TextureReadback, { .ctor = box_texture_readback_ctor });
    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "ReadbackOnSet" }),
        .query.terms = {{ .id = ecs_id(TextureReadback), .src.id = EcsSelf }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnAdd, EcsOnSet },
        .callback = BoxTextureReadbackOnSet
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "ReadbackOnRemove" }),
        .query.terms = {{ .id = ecs_id(TextureReadback) }},
        .events = { EcsOnRemove },
        .callback = BoxTextureReadbackOnRemove
    });

    ecs_function(world, {
        .name = "sample",
        .parent = module,
        .return_type = ecs_id(ecs_f64_t),
        .params = {
            { "texture", ecs_id(ecs_entity_t) },
            { "u", ecs_id(ecs_f64_t) },
            { "v", ecs_id(ecs_f64_t) }
        },
        .callback = box_texture_fn_sample
    });

    ecs_function(world, {
        .name = "sample_wrap",
        .parent = module,
        .return_type = ecs_id(ecs_f64_t),
        .params = {
            { "texture", ecs_id(ecs_entity_t) },
            { "u", ecs_id(ecs_f64_t) },
            { "v", ecs_id(ecs_f64_t) }
        },
        .callback = box_texture_fn_sample_wrap
    });

    ecs_function(world, {
        .name = "sample_channel",
        .parent = module,
        .return_type = ecs_id(ecs_f64_t),
        .params = {
            { "texture", ecs_id(ecs_entity_t) },
            { "u", ecs_id(ecs_f64_t) },
            { "v", ecs_id(ecs_f64_t) },
            { "channel", ecs_id(ecs_i32_t) }
        },
        .callback = box_texture_fn_sample_channel
    });

    ecs_function(world, {
        .name = "readback_ready",
        .parent = module,
        .return_type = ecs_id(ecs_bool_t),
        .params = {{ "texture", ecs_id(ecs_entity_t) }},
        .callback = box_texture_fn_readback_ready
    });
}

void box_texture_readbacks_fini(
    box_textures_t *t)
{
    ecs_map_iter_t it = ecs_map_iter(&t->readbacks);
    while (ecs_map_next(&it)) {
        box_texture_readback_t *rb = ecs_map_ptr(&it);
        ecs_os_free(rb->data);
        ecs_os_free(rb);
    }

    ecs_map_fini(&t->readbacks);
}
