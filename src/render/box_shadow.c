/**
 * @file render/box_shadow.c
 * @brief Soft shadows of the 2D shapes: a second instance of a Rect or Circle with a BoxShadow.
 *
 * A shadow is a slot of the batch of its shape with the AlphaBlend blend mode, no textures and
 * box_shadow set, drawn just before the shape (order.c), so it writes no depth and the shape draws
 * over it. Its pipeline shades it with fs_shadow, which takes no texture samples.
 *
 * Its quad is the shadow box plus 3 standard deviations on each side, which shape2d.wgsl blurs
 * with an erf, and cuts out by the rounded box of the shape as CSS does (in its shading, with the
 * alpha the shape covers it with), so it does not show through a translucent shape.
 */

#include "draw.h"
#include <math.h>
#include <stddef.h>

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
    float element[2];   /**< Half extents of the rounded box of the shape */
    float element_radius; /**< Corner radius of the rounded box of the shape */
    float scale[2];     /**< Scale of the quad of the shape relative to its size, which a layout
                         *   that fits the shape (ShapeMesh.scale) stretches it with */
    float spread;       /**< Growth of the outline of a polygon */
    bool polygon;       /**< The shadow of a polygon: offset is the center of the quad, element
                         *   the center of the bounds of the outline */
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

/** Resolve the shadow quad of a filled polygon: the bounds of its outline moved by the offset and
 * grown by the spread and 3 standard deviations on each side. The shader grows the outline
 * itself by the spread, which rounds its outer corners.
 *
 * @param shape The resolved shape, a polygon
 * @param shadow The shadow
 * @param out Receives the shadow box
 * @return Whether the shadow has an area, never for a stroked open polygon
 */
static bool box_shadow_resolve_polygon(
    const Shape2D *shape,
    const BoxShadow *shadow,
    box_shadow_box_t *out)
{
    float spread = isfinite(shadow->spread) ? shadow->spread : 0.0f;
    float w = shape->size[0] + 2.0f * spread;
    float h = shape->size[1] + 2.0f * spread;
    bool result = shape->width <= 0.0f && w > 0.0f && h > 0.0f && isfinite(w) && isfinite(h);
    if (!result) {
        goto done;
    }

    float blur = isfinite(shadow->blur) ? shadow->blur : 0.0f;
    out->polygon = true;
    out->spread = spread;
    out->sigma = glm_max(blur * 0.5f, BOX_SHADOW_SIGMA_MIN * glm_min(w, h) * 0.5f);
    out->size[0] = w + 6.0f * out->sigma;
    out->size[1] = h + 6.0f * out->sigma;
    out->element[0] = shape->center[0];
    out->element[1] = shape->center[1];
    out->offset[0] = shape->center[0] + (isfinite(shadow->offset_x) ? shadow->offset_x : 0.0f);
    out->offset[1] = shape->center[1] + (isfinite(shadow->offset_y) ? shadow->offset_y : 0.0f);
    out->scale[0] = out->scale[1] = 1.0f;
done:
    return result;
}

/** Resolve the shadow box of a rectangle: grown by the spread, its corner radius with it as CSS
 * does, so a sharp corner stays sharp, and grown by 3 standard deviations on each side.
 *
 * Pure, so the resolver of the render thread resolves the BoxShadow a transition renders with
 * the same function.
 *
 * @param shape The resolved shape, a rectangle
 * @param mesh_scale The x and y of the ShapeMesh.scale of the shape, NULL when it has none
 * @param shadow The shadow
 * @param out Receives the shadow box
 * @return Whether the shadow box has an area
 */
static bool box_shadow_resolve_rect(
    const Shape2D *shape,
    const float *mesh_scale,
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
    out->element[0] = shape->size[0] * 0.5f;
    out->element[1] = shape->size[1] * 0.5f;
    out->element_radius = glm_clamp(shape->radius, 0.0f, half);
    out->scale[0] = mesh_scale ? mesh_scale[0] / shape->size[0] : 1.0f;
    out->scale[1] = mesh_scale ? mesh_scale[1] / shape->size[1] : 1.0f;
    result = isfinite(out->scale[0]) && isfinite(out->scale[1]);
done:
    return result;
}

/** Resolve the shadow box of a shape: of a rectangle (box_shadow_resolve_rect) or of a polygon
 * (box_shadow_resolve_polygon). Pure, as they are.
 *
 * @param shape The resolved shape
 * @param mesh_scale The x and y of the ShapeMesh.scale of the shape, NULL when it has none
 * @param shadow The shadow
 * @param out Receives the shadow box
 * @return Whether the shadow box has an area
 */
static bool box_shadow_resolve(
    const Shape2D *shape,
    const float *mesh_scale,
    const BoxShadow *shadow,
    box_shadow_box_t *out)
{
    ecs_os_memset_t(out, 0, box_shadow_box_t);

    return shape->kind == BOX_SHAPE2D_POLYGON ? box_shadow_resolve_polygon(shape, shadow, out)
        : box_shadow_resolve_rect(shape, mesh_scale, shadow, out);
}

/** Get the ShapeMesh.scale of a shape for box_shadow_resolve.
 *
 * @param mesh The mesh of the shape, NULL when it has none
 * @return Its scale, NULL without a mesh
 */
static const float* box_shadow_mesh_scale(
    const ShapeMesh *mesh)
{
    return mesh ? mesh->scale : NULL;
}

/** Write the transform of a shadow: the world transform of its shape stretched as the quad of
 * the shape is, moved by the offset and scaled to the quad of the shadow.
 *
 * @param out The transform
 * @param transform World matrix of the shape, NULL for the identity
 * @param box The shadow box
 */
static void box_shadow_transform(
    box_render_transform_t *out,
    const vec4 *transform,
    const box_shadow_box_t *box)
{
    float scale[3] = { box->size[0] * box->scale[0], box->size[1] * box->scale[1], 1.0f };
    mat4 m;
    int32_t r, c;
    if (transform) {
        glm_mat4_copy(ECS_CONST_CAST(vec4*, transform), m);
    } else {
        glm_mat4_identity(m);
    }

    for (r = 0; r < 3; r ++) {
        for (c = 0; c < 3; c ++) {
            out->rows[r][c] = m[c][r] * scale[c];
        }

        out->rows[r][3] = m[0][r] * box->scale[0] * box->offset[0] +
            m[1][r] * box->scale[1] * box->offset[1] + m[3][r];
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

float box_draws_box_shadow_cover(
    const Shape2D *shape,
    uint8_t blend,
    float alpha)
{
    bool covers = blend == Opaque || blend == AlphaBlend || blend == PremultipliedAlpha;
    float edge = shape->border > 0.0f ? (float)shape->border_color.a / 255.0f : 1.0f;

    return covers ? glm_clamp(alpha * edge, 0.0f, 1.0f) : 0.0f;
}

/** Write the color of a shadow as linear light with its alpha: mixed toward the tints of the
 * ancestors and the own Tint of its shape as the border color is, its alpha times the resolved
 * opacity of the shape.
 *
 * @param out Four floats receiving the color
 * @param color Display encoded color of the shadow
 * @param material Material components of the shape, of which inherited and tint are read
 * @param alpha Resolved opacity of the shape
 */
static void box_shadow_color(
    float *out,
    const rgba *color,
    const box_draw_material_t *material,
    float alpha)
{
    if (material->inherited || material->tint) {
        box_draws_tinted(out, &color->r, material);
    } else {
        out[0] = box_shadow_linear(color->r);
        out[1] = box_shadow_linear(color->g);
        out[2] = box_shadow_linear(color->b);
    }

    out[3] = (float)color->a / 255.0f * alpha;
}

/** Write the shading of a shadow, which has no border, image or emission: emissive.w is the
 * aspect of its quad, material.x the corner radius of the shadow box and material.y the negated
 * standard deviation, both in units of half the smaller side of the quad.
 *
 * The rounded box of the shape it is cut out by, in the same units: uv.xy its center relative to
 * the shadow, uv.zw its half extents, material.z its corner radius and material.w the alpha it
 * covers the shadow with.
 *
 * The shadow of a polygon holds the index of its polygon record in material.x and its spread
 * in material.z instead, uv.xy the center of the bounds of its outline relative to the shadow
 * and uv.z half the smaller side of the quad, its outline being in the local units of the shape.
 *
 * @param out The shading
 * @param box The shadow box
 * @param linear Linear color and alpha of the shadow, box_shadow_color
 * @param cover Alpha the shape covers the shadow with, box_shadow_cover_alpha
 * @param clip Clip index of the shape
 * @param rank Rank of the shadow
 * @param record Index of the polygon record of a polygon, 0 for none
 */
static void box_shadow_shading(
    box_render_shading_t *out,
    const box_shadow_box_t *box,
    const float *linear,
    float cover,
    int32_t clip,
    uint32_t rank,
    int32_t record)
{
    float half = glm_min(box->size[0], box->size[1]) * 0.5f;
    ecs_os_memset_t(out, 0, box_render_shading_t);
    out->emissive[3] = box->size[0] / box->size[1];
    if (box->polygon) {
        out->material[0] = (float)(record > 0 ? record : 0);
        out->material[1] = -box->sigma / half;
        out->material[2] = box->spread;
        out->material[3] = cover;
        out->uv[0] = (box->element[0] - box->offset[0]) / half;
        out->uv[1] = (box->element[1] - box->offset[1]) / half;
        out->uv[2] = half;
        goto done;
    }

    out->material[0] = glm_clamp(box->radius / half, 0.0f, 1.0f);
    out->material[1] = -box->sigma / half;
    out->material[2] = box->element_radius / half;
    out->material[3] = cover;
    out->uv[0] = -box->offset[0] / half;
    out->uv[1] = -box->offset[1] / half;
    out->uv[2] = box->element[0] / half;
    out->uv[3] = box->element[1] / half;
done:
    out->ids[0] = clip > 0 ? (uint32_t)clip : 0u;
    out->ids[1] = rank;
    box_draws_color_pack(out->color, linear);
}

/** Write the instance of a shadow whose box has no area, which keeps its slot while a BoxShadow
 * transition runs: a quad of no size at the origin of its shape and no color, so it draws
 * nothing.
 *
 * @param transform The transform
 * @param shading The shading
 * @param world World matrix of the shape, NULL for the identity
 * @param clip Clip index of the shape
 * @param rank Rank of the shadow
 */
static void box_shadow_hidden(
    box_render_transform_t *transform,
    box_render_shading_t *shading,
    const vec4 *world,
    int32_t clip,
    uint32_t rank)
{
    box_shadow_box_t box;
    ecs_os_memset_t(&box, 0, box_shadow_box_t);
    box_shadow_transform(transform, world, &box);
    ecs_os_memset_t(shading, 0, box_render_shading_t);
    shading->ids[0] = clip > 0 ? (uint32_t)clip : 0u;
    shading->ids[1] = rank;
}

/** Write streams of the instance of a shadow from its shape.
 *
 * @param d The state
 * @param bs The shadow slot, in a batch
 * @param box The resolved shadow box
 * @param shadow The shadow
 * @param shape The resolved shape
 * @param blend Blend mode of the batch of the shape
 * @param transform World transform of the shape, NULL for the identity
 * @param material Material components of the shape, read when kinds has the shading
 * @param kinds box_draw_kind_t, the streams to write
 */
static void box_shadow_write(
    box_render_draws_t *d,
    const BoxShadowSlot *bs,
    const box_shadow_box_t *box,
    const BoxShadow *shadow,
    const Shape2D *shape,
    uint8_t blend,
    const WorldTransform *transform,
    const box_draw_material_t *material,
    uint8_t kinds)
{
    box_draw_batch_t *b = box_draws_batch(d, bs->batch);
    if (kinds & BOX_DRAW_TRANSFORM) {
        box_shadow_transform(box_draws_slot_transform(b, bs->slot),
            transform ? ECS_CONST_CAST(vec4*, transform->m) : NULL, box);
    }

    if (kinds & BOX_DRAW_SHADING) {
        float alpha = box_draws_alpha(material);
        float linear[4];
        box_shadow_color(linear, &shadow->color, material, alpha);
        box_shadow_shading(box_draws_slot_shading(b, bs->slot), box, linear,
            box_draws_box_shadow_cover(shape, blend, alpha), material->clip, bs->rank,
            material->slice);
    }

    box_draws_slot_changed(b, bs->slot, kinds);
}

const BoxShadowSlot* box_draws_box_shadows(
    const ecs_iter_t *it)
{
    return ecs_table_get_id(it->world, it->table, ecs_id(BoxShadowSlot), it->offset);
}

void box_draws_box_shadow_write(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    const BoxShadowSlot *bs,
    const DrawSlot *s,
    const WorldTransform *transform,
    const box_draw_material_t *material,
    uint8_t kinds)
{
    const BoxShadow *shadow;
    const Shape2D *shape;
    box_shadow_box_t box;
    if (bs->batch < 0 || s->batch < 0 ||
        bs->slot >= ecs_vec_count(&box_draws_batch(d, bs->batch)->entities))
    {
        goto done;
    }

    shadow = ecs_get(world, e, BoxShadow);
    shape = shadow ? ecs_get(world, e, Shape2D) : NULL;
    if (!shape || !box_shadow_resolve(shape,
        box_shadow_mesh_scale(ecs_get(world, e, ShapeMesh)), shadow, &box))
    {
        goto done;
    }

    box_shadow_write(d, bs, &box, shadow, shape, box_draws_batch(d, s->batch)->key.blend,
        transform, material, kinds);
done:
    return;
}

/** Get the instance matrix of a shadow relative to the world transform of its shape: the quad
 * of the shape stretched, moved by the offset and scaled to the quad of the shadow.
 *
 * @param world The world
 * @param e The entity
 * @param shadow The shadow
 * @param out Receives the matrix
 * @return Whether the shadow box has an area
 */
static bool box_shadow_instance(
    const ecs_world_t *world,
    ecs_entity_t e,
    const BoxShadow *shadow,
    mat4 out)
{
    const Shape2D *shape = ecs_get(world, e, Shape2D);
    box_shadow_box_t box;
    bool result = shape && box_shadow_resolve(shape,
        box_shadow_mesh_scale(ecs_get(world, e, ShapeMesh)), shadow, &box);
    if (result) {
        glm_mat4_identity(out);
        out[0][0] = box.size[0] * box.scale[0];
        out[1][1] = box.size[1] * box.scale[1];
        out[3][0] = box.scale[0] * box.offset[0];
        out[3][1] = box.scale[1] * box.offset[1];
    }

    return result;
}

bool box_draws_box_shadow_instance(
    const ecs_world_t *world,
    ecs_entity_t e,
    mat4 out)
{
    const BoxShadow *shadow = ecs_get(world, e, BoxShadow);

    return shadow && box_shadow_instance(world, e, shadow, out);
}

bool box_draws_box_shadow_bound(
    const ecs_world_t *world,
    ecs_entity_t e,
    const float *lo,
    const float *hi,
    mat4 out)
{
    float reach = glm_max(hi[0] - lo[0], hi[1] - lo[1]) * 0.5f;
    BoxShadow wide = {
        .offset_x = (lo[0] + hi[0]) * 0.5f,
        .offset_y = (lo[1] + hi[1]) * 0.5f,
        .blur = glm_max(lo[2], hi[2]),
        .spread = glm_max(lo[3], hi[3]) + reach
    };

    return box_shadow_instance(world, e, &wide, out);
}

bool box_draws_box_shadow_target(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    const DrawSlot *s,
    const box_draw_material_t *material,
    box_draw_transition_material_t *out)
{
    const BoxShadowSlot *bs = ecs_get(world, e, BoxShadowSlot);
    const BoxShadow *shadow = bs && bs->batch >= 0 ? ecs_get(world, e, BoxShadow) : NULL;
    const Shape2D *shape = shadow && s->batch >= 0 ? ecs_get(world, e, Shape2D) : NULL;
    bool result = shape != NULL;
    if (!result) {
        goto done;
    }

    box_draws_transition_target(out, material, BOX_DRAW_TRANSITION_BOX_SHADOW);
    out->shadow = shadow->color;
    out->shadow_blend = box_draws_batch(d, s->batch)->key.blend;
    out->shadow_cover = box_draws_box_shadow_cover(shape, out->shadow_blend, 1.0f);
done:
    return result;
}

bool box_draws_box_shadow_transition(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    const DrawSlot *s,
    const box_draw_material_t *material,
    box_draw_transition_box_shadow_t *out)
{
    const BoxShadow *shadow = ecs_get(world, e, BoxShadow);
    const Shape2D *shape = ecs_get(world, e, Shape2D);
    const ShapeMesh *mesh = ecs_get(world, e, ShapeMesh);
    const WorldTransform *transform = ecs_get(world, e, WorldTransform);
    mat4 identity;
    bool result;
    ecs_os_memset_t(out, 0, box_draw_transition_box_shadow_t);
    result = shadow && shape &&
        box_draws_box_shadow_target(world, d, e, s, material, &out->material);
    if (!result) {
        goto done;
    }

    glm_mat4_identity(identity);
    out->material.shape2d = *shape;
    out->shadow = *shadow;
    out->meshed = mesh != NULL;
    if (mesh) {
        out->mesh_scale[0] = mesh->scale[0];
        out->mesh_scale[1] = mesh->scale[1];
    }

    ecs_os_memcpy(out->world, transform ? (const void*)transform->m : (const void*)identity,
        ECS_SIZEOF(mat4));
    ecs_os_memcpy(out->rel, identity, ECS_SIZEOF(mat4));
done:
    return result;
}

void box_draws_box_shadow_patch(
    const box_draw_material_t *material,
    const box_draw_transition_box_shadow_t *record,
    const BoxShadow *shadow,
    mat4 world,
    void *transform,
    void *shading)
{
    box_render_transform_t t;
    box_render_shading_t out;
    box_shadow_box_t box;
    uint32_t ids[2];
    float words[4];
    ecs_os_memcpy(ids, ECS_OFFSET(shading, offsetof(box_render_shading_t, ids)),
        ECS_SIZEOF(ids));
    ecs_os_memcpy(words, ECS_OFFSET(shading, offsetof(box_render_shading_t, material)),
        ECS_SIZEOF(words));
    if (box_shadow_resolve(&record->material.shape2d,
        record->meshed ? record->mesh_scale : NULL, shadow, &box))
    {
        float alpha = box_draws_alpha(material);
        float linear[4];
        box_shadow_color(linear, &shadow->color, material, alpha);
        box_shadow_shading(&out, &box, linear,
            glm_clamp(alpha * record->material.shadow_cover, 0.0f, 1.0f), (int32_t)ids[0],
            ids[1], box.polygon && isfinite(words[0]) ? (int32_t)words[0] : 0);
        box_shadow_transform(&t, world, &box);
    } else {
        box_shadow_hidden(&t, &out, world, (int32_t)ids[0], ids[1]);
        if (record->material.shape2d.kind == BOX_SHAPE2D_POLYGON) {
            out.material[0] = words[0];
        }
    }

    ecs_os_memcpy(transform, &t, ECS_SIZEOF(t));
    ecs_os_memcpy(shading, &out, ECS_SIZEOF(out));
}

void box_draws_box_shadow_encode(
    const box_draw_material_t *material,
    const box_draw_transition_material_t *target,
    void *dst)
{
    float alpha = box_draws_alpha(material);
    float linear[4];
    uint32_t color[2];
    float cover = glm_clamp(alpha * target->shadow_cover, 0.0f, 1.0f);
    box_shadow_color(linear, &target->shadow, material, alpha);
    box_draws_color_pack(color, linear);
    ecs_os_memcpy(ECS_OFFSET(dst, offsetof(box_render_shading_t, color)), color,
        ECS_SIZEOF(color));
    ecs_os_memcpy(ECS_OFFSET(dst, offsetof(box_render_shading_t, material) +
        3 * sizeof(float)), &cover, ECS_SIZEOF(float));
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
        d->box_shadows --;
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
    uint8_t blend = Opaque;
    if (!bs) {
        goto done;
    }

    bool rect = shape &&
        (shape->kind == BOX_SHAPE2D_RECT || shape->kind == BOX_SHAPE2D_POLYGON);
    bool transitions = rect && ecs_has_id(world, e, TransitionBoxShadow);
    bool area = rect && box_shadow_resolve(shape,
        box_shadow_mesh_scale(ecs_get(world, e, ShapeMesh)), shadow, &box);
    bool drawn = area || transitions;
    if (drawn) {
        ecs_os_memcpy_t(&key, &box_draws_batch(d, s->batch)->key, box_draw_key_t);
        drawn = key.pipeline == BOX_RENDER_SHADER_SHAPE2D;
        blend = key.blend;
        key.blend = AlphaBlend;
        key.box_shadow = true;
        key.slice = false;
        ecs_os_memset(key.textures, 0, ECS_SIZEOF(key.textures));
        if (transitions) {
            key.dynamic_transform = true;
            key.dynamic_material = true;
        }
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
        d->box_shadows ++;
    }

    box_draw_material_t material = {
        .opacity = ecs_get(world, e, Opacity),
        .tint = ecs_get(world, e, Tint),
        .inherited = s->tinted ? &s->tint : NULL,
        .fade = s->fade,
        .clip = s->clip,
        .slice = s->slice
    };
    if (area) {
        box_shadow_write(d, bs, &box, shadow, shape, blend, ecs_get(world, e, WorldTransform),
            &material, BOX_DRAW_BOTH);
    } else {
        const WorldTransform *transform = ecs_get(world, e, WorldTransform);
        box_draw_batch_t *b = box_draws_batch(d, bs->batch);
        box_shadow_hidden(box_draws_slot_transform(b, bs->slot),
            box_draws_slot_shading(b, bs->slot),
            transform ? ECS_CONST_CAST(vec4*, transform->m) : NULL, material.clip, bs->rank);
        if (shape->kind == BOX_SHAPE2D_POLYGON) {
            box_draws_slot_shading(b, bs->slot)->material[0] = (float)s->slice;
        }

        box_draws_slot_changed(b, bs->slot, BOX_DRAW_BOTH);
    }
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
