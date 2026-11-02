/**
 * @file render/box_shadow.c
 * @brief Soft shadows of the 2D shapes: a second instance of a Rect or Circle with a BoxShadow.
 *
 * A shadow is a slot of the batch of its shape with the AlphaBlend blend mode and no textures,
 * drawn just before the shape (order.c), so it writes no depth and the shape draws over it.
 *
 * Its quad is the shadow box plus 3 standard deviations on each side; shape2d.wgsl tells it apart
 * by its negative border width, the negated standard deviation, and blurs it with an erf.
 */

#include "draw.h"
#include <math.h>

ECS_COMPONENT_DECLARE(BoxShadowSlot);

/** Smallest standard deviation of a shadow relative to half the smaller side of its box, which
 * keeps the mark of a shadow without blur negative */
#define BOX_SHADOW_SIGMA_MIN (1e-4f)

/** The resolved shadow of a shape, in the units of the shape */
typedef struct box_shadow_box_t {
    float size[2];      /**< Width and height of the quad: the shadow box and its margins */
    float offset[2];    /**< Offset of the shadow from the shape */
    float radius;       /**< Corner radius of the shadow box */
    float sigma;        /**< Standard deviation of the edge, at least BOX_SHADOW_SIGMA_MIN */
} box_shadow_box_t;

/** Construct BoxShadowSlot values without a slot.
 *
 * @param ptr The values
 * @param count Number of values
 * @param ti The type info
 */
static void box_shadow_slot_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    BoxShadowSlot *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (BoxShadowSlot){ .batch = -1, .slot = -1 };
    }
}

/** Copy BoxShadowSlot values: a copy is a new entity that has no slot yet.
 *
 * @param dst The destination values
 * @param src The source values
 * @param count Number of values
 * @param ti The type info
 */
static void box_shadow_slot_copy(
    void *dst,
    const void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)src;
    box_shadow_slot_ctor(dst, count, ti);
}

/** Move BoxShadowSlot values with the entity they belong to.
 *
 * @param dst The destination values
 * @param src The source values
 * @param count Number of values
 * @param ti The type info
 */
static void box_shadow_slot_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ecs_os_memcpy(dst, src, ti->size * count);
}

/** Resolve the shadow box of a rectangle: grown by the spread, its corner radius with it as CSS
 * does, so a sharp corner stays sharp, and grown by 3 standard deviations on each side.
 *
 * @param shape The resolved shape, a rectangle
 * @param shadow The shadow
 * @param out Receives the shadow box
 * @return Whether the shadow box has an area
 */
static bool box_shadow_resolve(
    const Shape2D *shape,
    const BoxShadow *shadow,
    box_shadow_box_t *out)
{
    float spread = shadow->spread;
    float w = shape->size[0] + 2.0f * spread;
    float h = shape->size[1] + 2.0f * spread;
    float half = glm_min(shape->size[0], shape->size[1]) * 0.5f;
    float radius = glm_clamp(shape->radius, 0.0f, half);
    bool result = w > 0.0f && h > 0.0f && isfinite(w) && isfinite(h);
    if (!result) {
        goto done;
    }

    if (spread > 0.0f && radius < spread) {
        float ratio = radius / spread - 1.0f;
        radius += spread * (1.0f + ratio * ratio * ratio);
    } else {
        radius += spread;
    }

    float blur = isfinite(shadow->blur) ? shadow->blur : 0.0f;
    out->sigma = glm_max(blur * 0.5f, BOX_SHADOW_SIGMA_MIN * glm_min(w, h) * 0.5f);
    out->radius = glm_clamp(radius, 0.0f, glm_min(w, h) * 0.5f);
    out->size[0] = w + 6.0f * out->sigma;
    out->size[1] = h + 6.0f * out->sigma;
    out->offset[0] = isfinite(shadow->offset_x) ? shadow->offset_x : 0.0f;
    out->offset[1] = isfinite(shadow->offset_y) ? shadow->offset_y : 0.0f;
done:
    return result;
}

/** Write the transform of a shadow: the world transform of its shape moved by the offset and
 * scaled to the quad of the shadow.
 *
 * @param out The transform
 * @param transform World transform of the shape, NULL for the identity
 * @param box The shadow box
 */
static void box_shadow_transform(
    box_render_transform_t *out,
    const WorldTransform *transform,
    const box_shadow_box_t *box)
{
    float scale[3] = { box->size[0], box->size[1], 1.0f };
    mat4 m;
    int32_t r, c;
    if (transform) {
        glm_mat4_copy(ECS_CONST_CAST(vec4*, transform->m), m);
    } else {
        glm_mat4_identity(m);
    }

    for (r = 0; r < 3; r ++) {
        for (c = 0; c < 3; c ++) {
            out->rows[r][c] = m[c][r] * scale[c];
        }

        out->rows[r][3] = m[0][r] * box->offset[0] + m[1][r] * box->offset[1] + m[3][r];
    }
}

/** Convert a display encoded channel to linear light.
 *
 * @param value The channel
 * @return The linear value from 0 to 1
 */
static float box_shadow_linear(
    uint8_t value)
{
    float c = (float)value / 255.0f;

    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/** Write the shading of a shadow, which has no border, image or emission: emissive.w is the
 * aspect of its quad, material.x the corner radius of the shadow box and material.y the negated
 * standard deviation, both in units of half the smaller side of the quad.
 *
 * @param out The shading
 * @param box The shadow box
 * @param color Display encoded color of the shadow
 * @param alpha Resolved opacity of the shape
 * @param clip Clip index of the shape
 * @param rank Rank of the shadow
 */
static void box_shadow_shading(
    box_render_shading_t *out,
    const box_shadow_box_t *box,
    const rgba *color,
    float alpha,
    int32_t clip,
    uint32_t rank)
{
    float half = glm_min(box->size[0], box->size[1]) * 0.5f;
    float linear[4] = {
        box_shadow_linear(color->r),
        box_shadow_linear(color->g),
        box_shadow_linear(color->b),
        (float)color->a / 255.0f * alpha
    };
    ecs_os_memset_t(out, 0, box_render_shading_t);
    out->emissive[3] = box->size[0] / box->size[1];
    out->material[0] = glm_clamp(box->radius / half, 0.0f, 1.0f);
    out->material[1] = -box->sigma / half;
    out->uv[0] = 1.0f;
    out->uv[1] = 1.0f;
    out->ids[0] = clip > 0 ? (uint32_t)clip : 0u;
    out->ids[1] = rank;
    box_draws_color_pack(out->color, linear);
}

void box_draws_box_shadow_remove(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e)
{
    BoxShadowSlot *bs = ecs_get_mut(world, e, BoxShadowSlot);
    if (bs && bs->batch >= 0) {
        int32_t batch = bs->batch, slot = bs->slot;
        bs->batch = -1;
        bs->slot = -1;
        box_draws_slot_remove(world, d, batch, slot);
    }
}

bool box_draws_box_shadow_moved(
    ecs_world_t *world,
    ecs_entity_t moved,
    int32_t batch,
    int32_t from,
    int32_t to)
{
    BoxShadowSlot *bs = ecs_get_mut(world, moved, BoxShadowSlot);
    bool result = bs && bs->batch == batch && bs->slot == from;
    if (result) {
        bs->slot = to;
    }

    return result;
}

void box_draws_box_shadow_update(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    const DrawSlot *s)
{
    BoxShadowSlot *bs = ecs_get_mut(world, e, BoxShadowSlot);
    const BoxShadow *shadow = bs ? ecs_get(world, e, BoxShadow) : NULL;
    const Shape2D *shape = shadow && s->batch >= 0 ? ecs_get(world, e, Shape2D) : NULL;
    box_shadow_box_t box;
    box_draw_key_t key;
    if (!bs) {
        goto done;
    }

    bool drawn = shape && shape->kind == BOX_SHAPE2D_RECT &&
        box_shadow_resolve(shape, shadow, &box);
    if (drawn) {
        ecs_os_memcpy_t(&key, &box_draws_batch(d, s->batch)->key, box_draw_key_t);
        drawn = key.pipeline == BOX_RENDER_SHADER_SHAPE2D;
        key.blend = AlphaBlend;
        ecs_os_memset(key.textures, 0, ECS_SIZEOF(key.textures));
    }

    if (bs->batch >= 0 && (!drawn ||
        ecs_os_memcmp_t(&box_draws_batch(d, bs->batch)->key, &key, box_draw_key_t)))
    {
        box_draws_box_shadow_remove(world, d, e);
        bs = ecs_get_mut(world, e, BoxShadowSlot);
    }

    if (!drawn) {
        goto done;
    }

    if (bs->batch < 0) {
        int32_t batch = box_draws_batch_ensure(d, &key);
        int32_t slot = box_draws_slot_add(d, batch, e);
        bs = ecs_get_mut(world, e, BoxShadowSlot);
        bs->batch = batch;
        bs->slot = slot;
    }

    const Opacity *opacity = ecs_get(world, e, Opacity);
    float alpha = glm_clamp(opacity ? opacity->value : 1.0f, 0.0f, 1.0f) * s->fade;
    box_draw_batch_t *b = box_draws_batch(d, bs->batch);
    box_shadow_transform(box_draws_slot_transform(b, bs->slot),
        ecs_get(world, e, WorldTransform), &box);
    box_shadow_shading(box_draws_slot_shading(b, bs->slot), &box, &shadow->color, alpha,
        s->clip, bs->rank);
    box_draws_slot_changed(b, bs->slot, BOX_DRAW_BOTH);
done:
    return;
}

/** Free the shadow slot of the entities that lose their BoxShadow or are deleted.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnBoxShadowRemove(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        box_draws_box_shadow_remove(it->world, d, it->entities[i]);
    }

done:
    return;
}

/** Mark the entities whose BoxShadow was set as changed.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnBoxShadow(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    int32_t i;
    for (i = 0; !ecs_is_fini(it->world) && i < it->count; i ++) {
        box_draws_mark(it->world, d, it->entities[i], BOX_DRAW_ALL);
    }
}

void box_draws_box_shadow_init(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, BoxShadowSlot);
    ecs_set_hooks(world, BoxShadowSlot, {
        .ctor = box_shadow_slot_ctor,
        .copy = box_shadow_slot_copy,
        .move = box_shadow_slot_move
    });
    ecs_add_pair(world, ecs_id(BoxShadow), EcsWith, ecs_id(BoxShadowSlot));
}

void box_draws_box_shadow_observers(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    box_draws_observer(world, d, "DrawsOnBoxShadow", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(BoxShadow) }},
        .events = { EcsOnSet } }, BoxDrawsOnBoxShadow);
    box_draws_observer(world, d, "DrawsOnBoxShadowRemove", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(BoxShadow) }},
        .query.flags = EcsQueryMatchDisabled,
        .events = { EcsOnRemove } }, BoxDrawsOnBoxShadowRemove);
}
